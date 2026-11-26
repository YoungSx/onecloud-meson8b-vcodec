// SPDX-License-Identifier: GPL-2.0
/*
 * Bounded V4L2 encoder client: formats, HCODEC ownership, timestamps,
 * KEYFRAME, controls, drain and busy-format checks, per-frame latency and
 * deterministic NV12 references. Content is integer-only, so -g writes the
 * same reference without a device, e.g. into an ffmpeg PSNR pipe.
 *
 * usage: encode-test [-p PATTERN] [-r FPS[/DIVISOR]] [-m 601|709] [-a FORMAT]
 *        [-c CONTROLS]... [-x CONTROLS]... [-d dumb|lima] [-g] [-e]
 *        [-s FRAME] [-f FRAME]
 *        DEVICE STREAM REF WIDTH HEIGHT FRAMES FORMAT QUALITY JPEG|H264
 * CONTROLS is NAME=VALUE[,NAME=VALUE...][@FRAME], set in one VIDIOC_S_EXT_CTRLS
 * before streaming or before queueing FRAME; -x requires the driver to
 * reject the transaction; -e reports ERROR buffers as "frame N error" and
 * continues. -m picks the RGB conversion for RGB formats, -a sends FORMAT's
 * bytes under another fourcc and -d queues OUTPUT planes as DMA-BUFs from
 * meson-drm dumb buffers (contiguous) or lima GEM objects (scattered).
 * FORMAT NV12-1 uses one contiguous memory plane; NV12 uses two (NV12M).
 * Each frame prints "frame N bytes B flags F latency_us L" (OUTPUT QBUF to
 * CAPTURE DQBUF).
 * -s restarts both queues on the same fd before FRAME; -f requests an IDR
 * five milliseconds after queueing FRAME, for the in-flight control check.
 */
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <drm/lima_drm.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/videodev2.h>
#include <linux/dma-buf.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define MARGIN		64	/* texture border the moving windows pan over */
#define SCENE_FRAMES	30

struct mapped { unsigned char *data; unsigned int size; int fd; };
struct texture { unsigned char *y, *u, *v; unsigned int stride; };
struct control { unsigned int id, frame, group; int value, reject; };

static const struct { const char *name; unsigned int id; } names[] = {
	{ "bitrate", V4L2_CID_MPEG_VIDEO_BITRATE },
	{ "profile", V4L2_CID_MPEG_VIDEO_H264_PROFILE },
	{ "level", V4L2_CID_MPEG_VIDEO_H264_LEVEL },
	{ "peak-bitrate", V4L2_CID_MPEG_VIDEO_BITRATE_PEAK },
	{ "cpb-size", V4L2_CID_MPEG_VIDEO_H264_CPB_SIZE },
	{ "bitrate-mode", V4L2_CID_MPEG_VIDEO_BITRATE_MODE },
	{ "rc", V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE },
	{ "gop", V4L2_CID_MPEG_VIDEO_GOP_SIZE },
	{ "qp-min", V4L2_CID_MPEG_VIDEO_H264_MIN_QP },
	{ "qp-max", V4L2_CID_MPEG_VIDEO_H264_MAX_QP },
	{ "i-qp", V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP },
	{ "p-qp", V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP },
	{ "header-mode", V4L2_CID_MPEG_VIDEO_HEADER_MODE },
	{ "force-key", V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME },
	{ "b-frames", V4L2_CID_MPEG_VIDEO_B_FRAMES },
	{ "slice-mode", V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE },
	{ "slice-mb", V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB },
	{ "sar-enable", V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_ENABLE },
	{ "sar-idc", V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_IDC },
	{ "sar-width", V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_WIDTH },
	{ "sar-height", V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_HEIGHT },
	{ "quality", V4L2_CID_JPEG_COMPRESSION_QUALITY },
};
/* palette: 16 fixed RGB bars for probing RGB inputs (static texture otherwise) */
enum { GRADIENT, STATIC, MOTION, NOISE, SCENE, PALETTE, PATTERNS };
static const char *const patterns[] = { "gradient", "static", "motion", "noise", "scene", "palette" };
static const unsigned char palette[16][3] = {
	{ 0, 0, 0 }, { 255, 255, 255 }, { 255, 0, 0 }, { 0, 255, 0 },
	{ 0, 0, 255 }, { 255, 255, 0 }, { 0, 255, 255 }, { 255, 0, 255 },
	{ 128, 128, 128 }, { 64, 64, 64 }, { 192, 192, 192 }, { 200, 100, 50 },
	{ 50, 100, 200 }, { 100, 200, 50 }, { 30, 60, 90 }, { 240, 180, 120 },
};
enum { SEMI_UV, SEMI_VU, PLANAR_420, PACKED_YUYV, PACKED_UYVY, PLANAR_444, PACKED_RGB };
static const struct format {
	const char *name;
	unsigned int fourcc, planes, layout;
	const char *order;		/* PACKED_RGB bytes in memory */
} formats[] = {
	{ "NV12", V4L2_PIX_FMT_NV12M, 2, SEMI_UV, NULL },
	{ "NV12-1", V4L2_PIX_FMT_NV12, 1, SEMI_UV, NULL },
	{ "NV21", V4L2_PIX_FMT_NV21M, 2, SEMI_VU, NULL },
	{ "YU12", V4L2_PIX_FMT_YUV420M, 3, PLANAR_420, NULL },
	{ "YUYV", V4L2_PIX_FMT_YUYV, 1, PACKED_YUYV, NULL },
	{ "UYVY", V4L2_PIX_FMT_UYVY, 1, PACKED_UYVY, NULL },
	{ "YM24", V4L2_PIX_FMT_YUV444M, 3, PLANAR_444, NULL },
	{ "RGB3", V4L2_PIX_FMT_RGB24, 1, PACKED_RGB, "RGB" },
	{ "BGR3", V4L2_PIX_FMT_BGR24, 1, PACKED_RGB, "BGR" },
	{ "XR24", V4L2_PIX_FMT_XBGR32, 1, PACKED_RGB, "BGRX" },
	{ "XB24", V4L2_PIX_FMT_RGBX32, 1, PACKED_RGB, "RGBX" },
	{ "BX24", V4L2_PIX_FMT_XRGB32, 1, PACKED_RGB, "XRGB" },
	{ "RX24", V4L2_PIX_FMT_BGRX32, 1, PACKED_RGB, "XBGR" },
};
static const struct format *format = &formats[0];

static int fd = -1;
static struct mapped in[VIDEO_MAX_PLANES], out;
static struct v4l2_pix_format_mplane pix;
static unsigned int w, h, pattern, ncontrols;
static struct control controls[64];
static struct texture tex[3];

static void die(const char *what) { perror(what); exit(1); }
static void ctl(unsigned long req, void *arg) { if (ioctl(fd, req, arg)) die("ioctl"); }
static uint64_t now_us(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}
static uint32_t hash(uint32_t x, uint32_t y, uint32_t seed)
{
	uint32_t v = x * 0x9e3779b1u ^ y * 0x85ebca77u ^ seed * 0xc2b2ae3du;

	v ^= v >> 15; v *= 0x2c1b3c6du; v ^= v >> 12; v *= 0x297a2d39u;
	return v ^ v >> 15;
}
static unsigned int triangle(unsigned int position, unsigned int range)
{
	position %= 2 * range;
	return position <= range ? position : 2 * range - position;
}

/*
 * Kind 0/1: gradient, blocks and fine detail; kind 3 adds a 2 px checker and
 * stronger detail. Kind 2 is full-band noise: about 1 byte per pixel at QP 26,
 * so keep it at small sizes (the encoder's frame buffer is 1 MiB).
 * Y stays in 16..235 and chroma in 16..240.
 */
static void make_texture(struct texture *t, unsigned int kind, unsigned int seed)
{
	unsigned int tw = w + 2 * MARGIN, th = h + 2 * MARGIN, cw = tw / 2;

	t->stride = tw;
	t->y = malloc(tw * th); t->u = malloc(cw * (th / 2)); t->v = malloc(cw * (th / 2));
	if (!t->y || !t->u || !t->v) die("texture");
	for (unsigned int y = 0; y < th; y++)
		for (unsigned int x = 0; x < tw; x++)
			t->y[y * tw + x] = kind == 2 ? 16 + hash(x, y, 100 + seed) % 220 :
				kind == 3 ? 16 + (x + 2 * y) * 127 / (tw + 2 * th) +
				((x ^ y) & 2 ? 16 : 0) + (hash(x, y, 3) >> 29) :
				16 + (kind ? tw - x + y : x + y) * 160 / (tw + th) +
				(((x >> (5 - kind)) ^ (y >> (5 - kind))) & 1) * 40 +
				(hash(x, y, kind) >> 29);
	for (unsigned int y = 0; y < th / 2; y++)
		for (unsigned int x = 0; x < cw; x++) {
			unsigned int i = y * cw + x;

			if (kind == 2) {
				t->u[i] = 16 + hash(x, y, 200 + seed) % 225;
				t->v[i] = 16 + hash(x, y, 300 + seed) % 225;
			} else if (kind == 1) {
				t->u[i] = 200 - x * 128 / cw;
				t->v[i] = 80 + ((x / 8 + y / 8) & 3) * 30;
			} else {
				t->u[i] = 64 + x * 128 / cw;
				t->v[i] = 64 + y * 128 / (th / 2) + (((x >> 4) ^ (y >> 4)) & 1) * 16;
			}
		}
}
/* Visible 4:2:0 window of a frame: rows into a texture or the gradient scratch. */
struct source { const unsigned char *y, *u, *v; unsigned int ystride, cstride; };
static unsigned char *gradient[3];

static struct source source(unsigned int frame)
{
	const struct texture *t = &tex[pattern == NOISE ? frame % 3 :
				      pattern == SCENE ? frame / SCENE_FRAMES % 3 : 0];
	unsigned int ox = MARGIN, oy = MARGIN, local = frame;

	if (pattern == GRADIENT) {
		for (unsigned int y = 0; y < h; y++)
			for (unsigned int x = 0; x < w; x++)
				gradient[0][y * w + x] = (x * 173 / w + y * 47 / h + frame * 11) % 224 + 16;
		return (struct source){ gradient[0], gradient[1], gradient[2], w, w / 2 };
	}
	if (pattern == SCENE)
		local = frame % SCENE_FRAMES;
	if (pattern == MOTION || pattern == SCENE) {
		/* even offsets keep luma and chroma windows aligned */
		ox = triangle(local * 4, 2 * MARGIN);
		oy = triangle(local * 2, 2 * MARGIN);
	}
	return (struct source){ t->y + oy * t->stride + ox,
		t->u + oy / 2 * (t->stride / 2) + ox / 2,
		t->v + oy / 2 * (t->stride / 2) + ox / 2, t->stride, t->stride / 2 };
}

/*
 * RGB inputs: the limited-range source becomes full-range RGB with the
 * inverse of the matrix the encoder applies (Q10), clipped to 0..255;
 * the reference converts that RGB back, as the encoder does.
 */
static const struct matrix {
	const char *name;
	unsigned int ycbcr_enc;
	int cr, gu, gv, cb;		/* R = Y + cr V, G = Y - gu U - gv V, B = Y + cb U */
	int forward[3][3];		/* RGB to Y, Cb, Cr */
} matrices[] = {
	{ "601", V4L2_YCBCR_ENC_601, 1634, 401, 832, 2066,
	  { { 263, 516, 100 }, { -152, -298, 450 }, { 450, -377, -73 } } },
	{ "709", V4L2_YCBCR_ENC_709, 1836, 218, 546, 2163,
	  { { 187, 629, 63 }, { -103, -347, 450 }, { 450, -409, -41 } } },
};
static const struct matrix *matrix = &matrices[0];

static unsigned char clip(int value)
{
	value = (value + 512) >> 10;
	return value < 0 ? 0 : value > 255 ? 255 : value;
}

static void pixel_rgb(const struct source *s, unsigned int x, unsigned int y,
		      unsigned char rgb[3])
{
	int luma = (s->y[y * s->ystride + x] - 16) * 1192;
	int u = s->u[y / 2 * s->cstride + x / 2] - 128;
	int v = s->v[y / 2 * s->cstride + x / 2] - 128;

	if (pattern == PALETTE) {
		memcpy(rgb, palette[x * 16 / w], 3);
		return;
	}
	rgb[0] = clip(luma + matrix->cr * v);
	rgb[1] = clip(luma - matrix->gu * u - matrix->gv * v);
	rgb[2] = clip(luma + matrix->cb * u);
}

/* Y, Cb or Cr (@c) of an RGB pixel, limited range, Q10 like the encoder */
static int rgb_component(const unsigned char rgb[3], unsigned int c)
{
	const int *k = matrix->forward[c];

	return ((k[0] * rgb[0] + k[1] * rgb[1] + k[2] * rgb[2] + 512) >> 10) + (c ? 128 : 16);
}

/* Packed RGB in @order (R, G, B, X bytes) */
static void put_rgb(const struct source *s, unsigned char *dst, unsigned int stride,
		    const char *order)
{
	unsigned int bytes = strlen(order);

	for (unsigned int y = 0; y < h; y++) {
		unsigned char *row = dst + y * stride;

		for (unsigned int x = 0; x < w; x++, row += bytes) {
			unsigned char rgb[3];

			pixel_rgb(s, x, y, rgb);
			for (unsigned int i = 0; i < bytes; i++)
				row[i] = order[i] == 'X' ? 0xff : rgb[order[i] == 'G' ? 1 : order[i] == 'B' ? 2 : 0];
		}
	}
}

static void put_frame(unsigned int frame, unsigned char *const plane[3],
		      const unsigned int stride[3])
{
	struct source s = source(frame);
	unsigned int layout = format->layout;

	if (layout == PACKED_RGB) {
		put_rgb(&s, plane[0], stride[0], format->order);
		return;
	}
	for (unsigned int y = 0; y < h; y++) {
		const unsigned char *luma = s.y + y * s.ystride;
		const unsigned char *u = s.u + y / 2 * s.cstride, *v = s.v + y / 2 * s.cstride;
		unsigned char *row = plane[0] + y * stride[0];
		unsigned int yo = layout == PACKED_UYVY;

		switch (layout) {
		case PACKED_YUYV:
		case PACKED_UYVY:
			/* 4:2:2 with each chroma row used twice */
			for (unsigned int x = 0; x < w; x += 2, row += 4) {
				row[yo] = luma[x]; row[yo + 2] = luma[x + 1];
				row[!yo] = u[x / 2]; row[!yo + 2] = v[x / 2];
			}
			break;
		case PLANAR_444:
			memcpy(row, luma, w);
			for (unsigned int x = 0; x < w; x++) {
				plane[1][y * stride[1] + x] = u[x / 2];
				plane[2][y * stride[2] + x] = v[x / 2];
			}
			break;
		default:
			memcpy(row, luma, w);
			if (y & 1)
				break;
			row = format->fourcc == V4L2_PIX_FMT_NV12 ?
				plane[0] + (h + y / 2) * stride[0] : plane[1] + y / 2 * stride[1];
			if (layout == PLANAR_420) {
				memcpy(row, u, w / 2);
				memcpy(plane[2] + y / 2 * stride[2], v, w / 2);
				break;
			}
			for (unsigned int x = 0, vu = layout == SEMI_VU; x < w / 2; x++) {
				row[2 * x + vu] = u[x];
				row[2 * x + !vu] = v[x];
			}
		}
	}
}

/*
 * The reference is the NV12 source; for RGB inputs it is the RGB the
 * encoder receives converted back, with each 2x2 chroma block averaged.
 */
static void write_reference(FILE *ref, unsigned int frame, unsigned char *uv)
{
	struct source s = source(frame);
	unsigned char rgb[4][3];

	for (unsigned int y = 0; y < h; y++) {
		if (format->layout != PACKED_RGB) {
			if (fwrite(s.y + y * s.ystride, 1, w, ref) != w) die("reference Y");
			continue;
		}
		for (unsigned int x = 0; x < w; x++) {
			pixel_rgb(&s, x, y, rgb[0]);
			uv[x] = rgb_component(rgb[0], 0);
		}
		if (fwrite(uv, 1, w, ref) != w) die("reference Y");
	}
	for (unsigned int y = 0; y < h / 2; y++) {
		for (unsigned int x = 0; x < w / 2; x++) {
			if (format->layout != PACKED_RGB) {
				uv[2 * x] = s.u[y * s.cstride + x];
				uv[2 * x + 1] = s.v[y * s.cstride + x];
				continue;
			}
			for (unsigned int i = 0; i < 4; i++)
				pixel_rgb(&s, 2 * x + (i & 1), 2 * y + i / 2, rgb[i]);
			for (unsigned int c = 1; c < 3; c++)
				uv[2 * x + c - 1] = (rgb_component(rgb[0], c) + rgb_component(rgb[1], c) +
						     rgb_component(rgb[2], c) + rgb_component(rgb[3], c) + 2) / 4;
		}
		if (fwrite(uv, 1, w, ref) != w) die("reference UV");
	}
}

/* NAME=VALUE[,NAME=VALUE...][@FRAME]: one VIDIOC_S_EXT_CTRLS transaction */
static void add_control(char *spec, int reject)
{
	static unsigned int groups;
	char *at = strchr(spec, '@'), *item, *end, *save = NULL;
	unsigned int frame = UINT_MAX;	/* before streaming */

	if (at) {
		*at++ = 0;
		frame = strtoul(at, &end, 10);
		if (*end || !*at)
			goto invalid;
	}
	for (item = strtok_r(spec, ",", &save); item; item = strtok_r(NULL, ",", &save)) {
		char *value = strchr(item, '=');
		struct control *c = &controls[ncontrols];

		if (!value || ncontrols == sizeof(controls) / sizeof(controls[0]))
			goto invalid;
		*value++ = 0;
		c->id = 0;
		for (unsigned int i = 0; i < sizeof(names) / sizeof(names[0]); i++)
			if (!strcmp(item, names[i].name))
				c->id = names[i].id;
		if (!c->id && (!(c->id = strtoul(item, &end, 0)) || *end))
			goto invalid;
		c->value = strtol(value, &end, 0);
		if (*end || !*value)
			goto invalid;
		c->frame = frame;
		c->group = groups;
		c->reject = reject;
		ncontrols++;
	}
	groups++;
	return;
invalid:
	fprintf(stderr, "invalid control %s\n", spec);
	exit(2);
}

static void apply_controls(unsigned int frame)
{
	for (unsigned int i = 0; i < ncontrols;) {
		struct v4l2_ext_control e[sizeof(controls) / sizeof(controls[0])] = {{0}};
		struct v4l2_ext_controls s = { .controls = e };
		struct control *c = &controls[i];
		int ret;

		for (; i < ncontrols && controls[i].group == c->group; i++) {
			e[s.count].id = controls[i].id;
			e[s.count++].value = controls[i].value;
		}
		if (c->frame != frame)
			continue;
		ret = ioctl(fd, VIDIOC_S_EXT_CTRLS, &s);
		if (c->reject ? !ret || (errno != EINVAL && errno != ERANGE && errno != EBUSY) : ret) {
			fprintf(stderr, "controls from %#x=%d: %s\n", c->id, c->value,
				ret ? strerror(errno) : "accepted, expected rejection");
			exit(1);
		}
	}
}

static void ready(void)
{
	struct pollfd p = { .fd = fd, .events = POLLIN | POLLOUT | POLLPRI };

	if (poll(&p, 1, 10000) <= 0) die("poll timeout");
}
/* -d: OUTPUT planes are DMA-BUFs from meson-drm dumb buffers or lima GEM */
static const char *dmabuf;
static unsigned int memory(unsigned int type)
{
	return dmabuf && type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
}

static void sync_plane(struct mapped *plane, uint64_t flags)
{
	struct dma_buf_sync sync = { .flags = flags };
	int ret;

	if (!dmabuf)
		return;
	do {
		ret = ioctl(plane->fd, DMA_BUF_IOCTL_SYNC, &sync);
	} while (ret < 0 && (errno == EINTR || errno == EAGAIN));
	if (ret)
		die("DMA_BUF_SYNC");
}

static int drm_open(const char *driver)
{
	for (int i = 0; i < 4; i++) {
		char path[32], name[16] = "";
		struct drm_version v = { .name = name, .name_len = sizeof(name) - 1 };
		int drm;

		snprintf(path, sizeof(path), "/dev/dri/card%d", i);
		drm = open(path, O_RDWR | O_CLOEXEC);
		if (drm < 0)
			continue;
		if (!ioctl(drm, DRM_IOCTL_VERSION, &v) && !strcmp(name, driver))
			return drm;
		close(drm);
	}
	fprintf(stderr, "no %s DRM device\n", driver);
	exit(1);
}

/*
 * A CPU-mapped DMA-BUF of at least @size bytes: a meson-drm dumb buffer is
 * one contiguous CMA range, a lima GEM object is scattered shmem pages.
 */
static void drm_buffer(int drm, struct mapped *plane, unsigned int size)
{
	struct drm_prime_handle prime = { .flags = DRM_CLOEXEC | DRM_RDWR };
	uint64_t offset;

	if (!strcmp(dmabuf, "lima")) {
		struct drm_lima_gem_create create = { .size = size };
		struct drm_lima_gem_info info = { 0 };

		if (ioctl(drm, DRM_IOCTL_LIMA_GEM_CREATE, &create)) die("lima create");
		info.handle = create.handle;
		if (ioctl(drm, DRM_IOCTL_LIMA_GEM_INFO, &info)) die("lima info");
		prime.handle = create.handle;
		offset = info.offset;
		plane->size = (size + 4095) & ~4095u;
	} else {
		struct drm_mode_create_dumb create = { .width = 4096, .height = (size + 4095) / 4096, .bpp = 8 };
		struct drm_mode_map_dumb map = { 0 };

		if (ioctl(drm, DRM_IOCTL_MODE_CREATE_DUMB, &create)) die("dumb create");
		map.handle = create.handle;
		if (ioctl(drm, DRM_IOCTL_MODE_MAP_DUMB, &map)) die("dumb map");
		prime.handle = create.handle;
		offset = map.offset;
		plane->size = create.size;
	}
	if (ioctl(drm, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime)) die("prime export");
	plane->fd = prime.fd;
	plane->data = mmap(NULL, plane->size, PROT_READ | PROT_WRITE, MAP_SHARED, drm, offset);
	if (plane->data == MAP_FAILED) die("drm mmap");
}

/*
 * Buffers start zeroed like the driver's staging buffer: an in-place
 * DMA-BUF exposes its padding rows to the encoder, and only matching
 * padding keeps its stream bit-exact with the copied MMAP path.
 */
static void setup(unsigned int type, struct mapped *planes, unsigned int n)
{
	struct v4l2_requestbuffers r = { .count = 1, .type = type, .memory = memory(type) };
	struct v4l2_plane p[VIDEO_MAX_PLANES] = {0};
	struct v4l2_buffer b = { .type = type, .memory = memory(type), .length = n, .m.planes = p };

	ctl(VIDIOC_REQBUFS, &r);
	if (memory(type) == V4L2_MEMORY_DMABUF) {
		int drm = drm_open(strcmp(dmabuf, "lima") ? "meson" : "lima");

		for (unsigned int i = 0; i < n; i++) {
			drm_buffer(drm, &planes[i], pix.plane_fmt[i].sizeimage);
			sync_plane(&planes[i], DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE);
			memset(planes[i].data, 0, planes[i].size);
			sync_plane(&planes[i], DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE);
		}
		return;
	}
	ctl(VIDIOC_QUERYBUF, &b);
	for (unsigned int i = 0; i < n; i++) {
		planes[i].size = p[i].length;
		planes[i].data = mmap(NULL, p[i].length, PROT_READ | PROT_WRITE, MAP_SHARED,
				      fd, p[i].m.mem_offset);
		if (planes[i].data == MAP_FAILED) die("mmap");
		memset(planes[i].data, 0, planes[i].size);
	}
}
static void queue(unsigned int type, unsigned int n, unsigned int frame)
{
	struct v4l2_plane p[VIDEO_MAX_PLANES] = {0};
	struct v4l2_buffer b = { .type = type, .memory = memory(type), .length = n, .m.planes = p,
		.timestamp = { .tv_sec = frame, .tv_usec = 1234 } };

	for (unsigned int i = 0; i < n; i++) {
		p[i].length = type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE ? in[i].size : out.size;
		if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) p[i].bytesused = pix.plane_fmt[i].sizeimage;
		if (memory(type) == V4L2_MEMORY_DMABUF) p[i].m.fd = in[i].fd;
	}
	ctl(VIDIOC_QBUF, &b);
}
static struct v4l2_buffer dequeue(unsigned int type, unsigned int n, struct v4l2_plane *p)
{
	struct v4l2_buffer b = { .type = type, .memory = memory(type), .length = n, .m.planes = p };

	for (int tries = 0; tries < 100; tries++) {
		if (!ioctl(fd, VIDIOC_DQBUF, &b)) return b;
		if (errno != EAGAIN) die("DQBUF");
		ready(); usleep(1000);
	}
	errno = ETIMEDOUT; die("DQBUF"); return b;
}

static void usage(const char *name)
{
	fprintf(stderr, "usage: %s [-p gradient|static|motion|noise|scene|palette] [-r FPS[/DIVISOR]]\n"
		"  [-m 601|709] [-a FORMAT] [-c NAME=VALUE[,NAME=VALUE...][@FRAME]]...\n"
		"  [-x ...]... [-d dumb|lima] [-g] [-e]\n"
		"  [-s RESTART_FRAME] [-f FORCE_DURING_FRAME]\n"
		"  DEVICE STREAM REF WIDTH HEIGHT FRAMES FORMAT QUALITY JPEG|H264\n"
		"FORMAT: NV12 NV12-1 NV21 YU12 YUYV UYVY YM24 RGB3 BGR3 XR24 XB24 BX24 RX24\n", name);
	exit(2);
}

/* -g: write the reference only; DEVICE and STREAM are ignored, REF may be "-" */
static int generate(FILE *ref, unsigned int frames)
{
	unsigned char *uv = malloc(w);

	if (!uv) die("frame");
	for (unsigned int frame = 0; frame < frames; frame++)
		write_reference(ref, frame, uv);
	free(uv);
	return fclose(ref) ? 1 : 0;
}
int main(int argc, char **argv)
{
	unsigned int fps = 0, divisor = 1, generate_only = 0, tolerate_errors = 0, frames, quality;
	unsigned int restart_frame = UINT_MAX, inflight_force = UINT_MAX;
	const char *program = argv[0], *negotiate = NULL;
	const struct format *as;
	char *end;
	int option, jpeg;

	while ((option = getopt(argc, argv, "p:r:m:a:d:c:x:s:f:ge")) != -1) {
		switch (option) {
		case 'p':
			for (pattern = 0; pattern < PATTERNS && strcmp(optarg, patterns[pattern]); pattern++)
				;
			if (pattern == PATTERNS) usage(program);
			break;
		case 'r':
			fps = strtoul(optarg, &end, 10);
			if (*end == '/') divisor = strtoul(end + 1, &end, 10);
			if (*end || !fps || !divisor) usage(program);
			break;
		case 'm':
			for (matrix = matrices; strcmp(optarg, matrix->name); matrix++)
				if (matrix == &matrices[1]) usage(program);
			break;
		case 'a': negotiate = optarg; break;
		case 'd':
			if (strcmp(optarg, "dumb") && strcmp(optarg, "lima")) usage(program);
			dmabuf = optarg;
			break;
		case 'c': case 'x': add_control(optarg, option == 'x'); break;
		case 'g': generate_only = 1; break;
		case 'e': tolerate_errors = 1; break;
		case 's': case 'f': {
			unsigned long value = strtoul(optarg, &end, 10);

			if (!*optarg || *end || value >= UINT_MAX) usage(program);
			if (option == 's') restart_frame = value;
			else inflight_force = value;
			break;
		}
		default: usage(program);
		}
	}
	argc -= optind - 1; argv += optind - 1;
	if (argc != 10) usage(program);
	w = strtoul(argv[4], NULL, 10); h = strtoul(argv[5], NULL, 10);
	frames = strtoul(argv[6], NULL, 10); quality = strtoul(argv[8], NULL, 10);
	/* argv[9] is JPEG or H264, for regression with this same client. */
	jpeg = !strcmp(argv[9], "JPEG");
	if (!w || !h || (w & 1) || (h & 1) || !frames || quality < 1 || quality > 100) return 2;
	for (format = formats; strcmp(argv[7], format->name); format++)
		if (format == &formats[sizeof(formats) / sizeof(formats[0]) - 1]) usage(program);
	/* -a: send FORMAT's bytes under another fourcc, to probe byte orders */
	as = format;
	if (negotiate)
		for (as = formats; strcmp(negotiate, as->name); as++)
			if (as == &formats[sizeof(formats) / sizeof(formats[0]) - 1]) usage(program);
	if (as->planes != format->planes) usage(program);
	for (unsigned int i = 0; i < 3; i++)
		if (!(gradient[i] = malloc(i ? w * h / 4 : w * h))) die("gradient");
	for (unsigned int y = 0; y < h / 2; y++)
		for (unsigned int x = 0; x < w / 2; x++) {
			gradient[1][y * (w / 2) + x] = 68 + x * 120 / (w / 2);
			gradient[2][y * (w / 2) + x] = 68 + y * 120 / (h / 2);
		}
	if (pattern == NOISE)
		for (unsigned int i = 0; i < 3; i++) make_texture(&tex[i], 2, i);
	else if (pattern == SCENE)
		for (unsigned int i = 0; i < 3; i++) make_texture(&tex[i], i == 2 ? 3 : i, 0);
	else if (pattern != GRADIENT)
		make_texture(&tex[0], 0, 0);
	FILE *ref = !strcmp(argv[3], "/dev/null") ? NULL : !strcmp(argv[3], "-") ? stdout : fopen(argv[3], "wb");
	if (generate_only) {
		if (!ref) usage(program);
		return generate(ref, frames);
	}
	FILE *stream = fopen(argv[2], "wb");
	if (!stream || (!ref && strcmp(argv[3], "/dev/null"))) die("fopen");
	fd = open(argv[1], O_RDWR | O_NONBLOCK); if (fd < 0) die("open");
	int other = open(argv[1], O_RDWR | O_NONBLOCK);
	if (other >= 0) close(other);
	else if (errno != EBUSY) die("second encoder open");
	struct v4l2_format f = { .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE };
	f.fmt.pix_mp.width = w; f.fmt.pix_mp.height = h;
	f.fmt.pix_mp.pixelformat = as->fourcc;
	/* -m picks the RGB conversion; it is meaningless for YUV inputs */
	if (format->layout == PACKED_RGB) f.fmt.pix_mp.ycbcr_enc = matrix->ycbcr_enc;
	ctl(VIDIOC_S_FMT, &f); pix = f.fmt.pix_mp;
	if (pix.pixelformat != as->fourcc || pix.num_planes != format->planes) {
		fprintf(stderr, "driver does not take %s\n", format->name);
		return 3;
	}
	if (pix.width != w || pix.height != h) {
		fprintf(stderr, "driver adjusted %ux%u to %ux%u for %s\n", w, h, pix.width,
			pix.height, format->name);
		return 3;
	}
	f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	f.fmt.pix_mp.pixelformat = jpeg ? V4L2_PIX_FMT_JPEG : V4L2_PIX_FMT_H264;
	ctl(VIDIOC_S_FMT, &f);
	if (f.fmt.pix_mp.pixelformat != (jpeg ? V4L2_PIX_FMT_JPEG : V4L2_PIX_FMT_H264)) return 1;
	/* the coded format may veto the raw one (JPEG takes no RGB) */
	struct v4l2_format o = { .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE };
	ctl(VIDIOC_G_FMT, &o);
	if (o.fmt.pix_mp.pixelformat != as->fourcc) {
		fprintf(stderr, "driver does not take %s with %s\n", format->name, argv[9]);
		return 3;
	}
	if (format->layout == PACKED_RGB && !jpeg &&
	    (f.fmt.pix_mp.ycbcr_enc != matrix->ycbcr_enc ||
	     f.fmt.pix_mp.quantization != V4L2_QUANTIZATION_LIM_RANGE)) {
		fprintf(stderr, "coded colorimetry %u/%u, expected %u limited\n",
			f.fmt.pix_mp.ycbcr_enc, f.fmt.pix_mp.quantization, matrix->ycbcr_enc);
		return 1;
	}
	if (fps) {
		struct v4l2_streamparm parm = { .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE };

		parm.parm.output.timeperframe.numerator = divisor;
		parm.parm.output.timeperframe.denominator = fps;
		ctl(VIDIOC_S_PARM, &parm);
		fprintf(stderr, "timeperframe %u/%u\n", parm.parm.output.timeperframe.numerator,
			parm.parm.output.timeperframe.denominator);
	}
	struct v4l2_control c = { .id = V4L2_CID_JPEG_COMPRESSION_QUALITY, .value = quality };
	ctl(VIDIOC_S_CTRL, &c);
	apply_controls(UINT_MAX);
	setup(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, in, pix.num_planes);
	setup(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, &out, 1);
	if (ioctl(fd, VIDIOC_S_FMT, &f) != -1 || errno != EBUSY) { fprintf(stderr, "busy S_FMT failed\n"); return 1; }
	struct v4l2_event_subscription sub = { .type = V4L2_EVENT_EOS }; ctl(VIDIOC_SUBSCRIBE_EVENT, &sub);
	unsigned int type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; ctl(VIDIOC_STREAMON, &type);
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE; ctl(VIDIOC_STREAMON, &type);
	unsigned char *plane[3] = { in[0].data, in[1].data, in[2].data }, *uv = malloc(w);
	unsigned int stride[3];
	for (unsigned int i = 0; i < 3; i++) stride[i] = pix.plane_fmt[i].bytesperline;
	if (!uv) die("malloc");
	uint64_t start = now_us();
	for (unsigned int frame = 0; frame < frames; frame++) {
		struct v4l2_plane p[VIDEO_MAX_PLANES] = {0};

		if (frame == restart_frame) {
			type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE; ctl(VIDIOC_STREAMOFF, &type);
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; ctl(VIDIOC_STREAMOFF, &type);
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; ctl(VIDIOC_STREAMON, &type);
			type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE; ctl(VIDIOC_STREAMON, &type);
			fprintf(stderr, "same-fd restart before frame %u\n", frame);
		}
		apply_controls(frame);
		for (unsigned int i = 0; i < pix.num_planes; i++)
			sync_plane(&in[i], DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE);
		put_frame(frame, plane, stride);
		for (unsigned int i = 0; i < pix.num_planes; i++)
			sync_plane(&in[i], DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE);
		if (ref) write_reference(ref, frame, uv);
		queue(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, 1, frame);
		uint64_t queued = now_us();
		queue(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, pix.num_planes, frame);
		if (frame == inflight_force) {
			struct v4l2_control force = { .id = V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME };

			usleep(5000);
			ctl(VIDIOC_S_CTRL, &force);
			fprintf(stderr, "force key after queueing frame %u\n", frame);
		}
		struct v4l2_buffer b = dequeue(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, 1, p);
		uint64_t latency = now_us() - queued;
		if (tolerate_errors && (b.flags & V4L2_BUF_FLAG_ERROR) && b.timestamp.tv_sec == frame) {
			fprintf(stderr, "frame %u error flags %#x latency_us %llu\n", frame, b.flags,
				(unsigned long long)latency);
			dequeue(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, pix.num_planes, p);
			continue;
		}
		if ((b.flags & V4L2_BUF_FLAG_ERROR) || !p[0].bytesused || p[0].bytesused > out.size ||
		    b.timestamp.tv_sec != frame || b.timestamp.tv_usec != 1234 ||
		    (jpeg && !(b.flags & V4L2_BUF_FLAG_KEYFRAME))) {
			fprintf(stderr, "invalid capture frame %u flags %#x size %u\n", frame, b.flags, p[0].bytesused);
			return 1;
		}
		if (fwrite(out.data, 1, p[0].bytesused, stream) != p[0].bytesused) die("stream write");
		fprintf(stderr, "frame %u bytes %u flags %#x latency_us %llu\n", frame, p[0].bytesused,
			b.flags, (unsigned long long)latency);
		dequeue(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, pix.num_planes, p);
	}
	uint64_t elapsed = now_us() - start;
	queue(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, 1, frames);
	struct v4l2_encoder_cmd cmd = { .cmd = V4L2_ENC_CMD_STOP }; ctl(VIDIOC_ENCODER_CMD, &cmd);
	struct v4l2_plane p[VIDEO_MAX_PLANES] = {0};
	struct v4l2_buffer b = dequeue(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, 1, p);
	if (!(b.flags & V4L2_BUF_FLAG_LAST) || p[0].bytesused) { fprintf(stderr, "drain failed\n"); return 1; }
	struct v4l2_event event; ctl(VIDIOC_DQEVENT, &event); if (event.type != V4L2_EVENT_EOS) return 1;
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE; ctl(VIDIOC_STREAMOFF, &type);
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; ctl(VIDIOC_STREAMOFF, &type);
	close(fd);
	if (fclose(stream) || (ref && fclose(ref))) die("fclose");
	fprintf(stderr, "PASS: %u frames, ownership, busy format, timestamps, EOS, loop_us %llu\n",
		frames, (unsigned long long)elapsed);
	return 0;
}
