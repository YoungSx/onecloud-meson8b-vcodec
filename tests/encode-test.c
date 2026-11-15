// SPDX-License-Identifier: GPL-2.0
/*
 * Bounded V4L2 encoder client: formats, HCODEC ownership, timestamps,
 * KEYFRAME, controls, drain and busy-format checks, per-frame latency and
 * deterministic NV12 references. Content is integer-only, so -g writes the
 * same reference without a device, e.g. into an ffmpeg PSNR pipe.
 *
 * usage: encode-test [-p PATTERN] [-r FPS[/DIVISOR]] [-c CONTROLS]... [-x CONTROLS]...
 *        [-g] [-e] DEVICE STREAM REF WIDTH HEIGHT FRAMES NV12|NV21|YU12 QUALITY JPEG|H264
 * CONTROLS is NAME=VALUE[,NAME=VALUE...][@FRAME], set in one VIDIOC_S_EXT_CTRLS
 * before streaming or before queueing FRAME; -x requires the driver to
 * reject the transaction; -e reports ERROR buffers as "frame N error" and
 * continues. Each frame prints
 * "frame N bytes B flags F latency_us L" (OUTPUT QBUF to CAPTURE DQBUF).
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/videodev2.h>
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

struct mapped { unsigned char *data; unsigned int size; };
struct texture { unsigned char *y, *u, *v; unsigned int stride; };
struct control { unsigned int id, frame, group; int value, reject; };

static const struct { const char *name; unsigned int id; } names[] = {
	{ "bitrate", V4L2_CID_MPEG_VIDEO_BITRATE },
	{ "rc", V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE },
	{ "gop", V4L2_CID_MPEG_VIDEO_GOP_SIZE },
	{ "qp-min", V4L2_CID_MPEG_VIDEO_H264_MIN_QP },
	{ "qp-max", V4L2_CID_MPEG_VIDEO_H264_MAX_QP },
	{ "i-qp", V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP },
	{ "p-qp", V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP },
	{ "header-mode", V4L2_CID_MPEG_VIDEO_HEADER_MODE },
	{ "force-key", V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME },
	{ "b-frames", V4L2_CID_MPEG_VIDEO_B_FRAMES },
	{ "quality", V4L2_CID_JPEG_COMPRESSION_QUALITY },
};
enum { GRADIENT, STATIC, MOTION, NOISE, SCENE };
static const char *const patterns[] = { "gradient", "static", "motion", "noise", "scene" };

static int fd = -1;
static struct mapped in[VIDEO_MAX_PLANES], out;
static struct v4l2_pix_format_mplane pix;
static unsigned int w, h, planar, vu, pattern, ncontrols;
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
static void put_frame(unsigned int frame, unsigned char *const plane[3],
		      const unsigned int stride[3])
{
	const struct texture *t = &tex[pattern == NOISE ? frame % 3 :
				      pattern == SCENE ? frame / SCENE_FRAMES % 3 : 0];
	unsigned int ox = MARGIN, oy = MARGIN, local = frame;

	if (pattern == SCENE)
		local = frame % SCENE_FRAMES;
	if (pattern == MOTION || pattern == SCENE) {
		/* even offsets keep luma and chroma windows aligned */
		ox = triangle(local * 4, 2 * MARGIN);
		oy = triangle(local * 2, 2 * MARGIN);
	}
	for (unsigned int y = 0; y < h; y++) {
		unsigned char *row = plane[0] + y * stride[0];

		if (pattern != GRADIENT) {
			memcpy(row, t->y + (oy + y) * t->stride + ox, w);
			continue;
		}
		for (unsigned int x = 0; x < w; x++)
			row[x] = (x * 173 / w + y * 47 / h + frame * 11) % 224 + 16;
	}
	for (unsigned int y = 0; y < h / 2; y++) {
		unsigned char *row = plane[1] + y * stride[1];
		unsigned char *vrow = planar ? plane[2] + y * stride[2] : NULL;
		unsigned int c = (oy / 2 + y) * (t->stride / 2) + ox / 2;

		for (unsigned int x = 0; x < w / 2; x++) {
			unsigned char u = pattern == GRADIENT ? 68 + x * 120 / (w / 2) : t->u[c + x];
			unsigned char v = pattern == GRADIENT ? 68 + y * 120 / (h / 2) : t->v[c + x];

			if (planar) { row[x] = u; vrow[x] = v; }
			else { row[2 * x + vu] = u; row[2 * x + !vu] = v; }
		}
	}
}

/* Reference is always NV12, independent of the negotiated source. */
static void write_reference(FILE *ref, unsigned char *const plane[3],
			    const unsigned int stride[3], unsigned char *uv)
{
	for (unsigned int y = 0; y < h; y++)
		if (fwrite(plane[0] + y * stride[0], 1, w, ref) != w) die("reference Y");
	for (unsigned int y = 0; y < h / 2; y++) {
		const unsigned char *row = plane[1] + y * stride[1];

		for (unsigned int x = 0; x < w / 2; x++) {
			uv[2 * x] = planar ? row[x] : row[2 * x + vu];
			uv[2 * x + 1] = planar ? plane[2][y * stride[2] + x] : row[2 * x + !vu];
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
		if (c->reject ? !ret || (errno != EINVAL && errno != ERANGE) : ret) {
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
static void setup(unsigned int type, struct mapped *planes, unsigned int n)
{
	struct v4l2_requestbuffers r = { .count = 1, .type = type, .memory = V4L2_MEMORY_MMAP };
	struct v4l2_plane p[VIDEO_MAX_PLANES] = {0};
	struct v4l2_buffer b = { .type = type, .memory = V4L2_MEMORY_MMAP, .length = n, .m.planes = p };

	ctl(VIDIOC_REQBUFS, &r);
	ctl(VIDIOC_QUERYBUF, &b);
	for (unsigned int i = 0; i < n; i++) {
		planes[i].size = p[i].length;
		planes[i].data = mmap(NULL, p[i].length, PROT_READ | PROT_WRITE, MAP_SHARED,
				      fd, p[i].m.mem_offset);
		if (planes[i].data == MAP_FAILED) die("mmap");
		memset(planes[i].data, 128, planes[i].size);
	}
}
static void queue(unsigned int type, unsigned int n, unsigned int frame)
{
	struct v4l2_plane p[VIDEO_MAX_PLANES] = {0};
	struct v4l2_buffer b = { .type = type, .memory = V4L2_MEMORY_MMAP, .length = n, .m.planes = p,
		.timestamp = { .tv_sec = frame, .tv_usec = 1234 } };

	for (unsigned int i = 0; i < n; i++) {
		p[i].length = type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE ? in[i].size : out.size;
		if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) p[i].bytesused = p[i].length;
	}
	ctl(VIDIOC_QBUF, &b);
}
static struct v4l2_buffer dequeue(unsigned int type, unsigned int n, struct v4l2_plane *p)
{
	struct v4l2_buffer b = { .type = type, .memory = V4L2_MEMORY_MMAP, .length = n, .m.planes = p };

	for (int tries = 0; tries < 100; tries++) {
		if (!ioctl(fd, VIDIOC_DQBUF, &b)) return b;
		if (errno != EAGAIN) die("DQBUF");
		ready(); usleep(1000);
	}
	errno = ETIMEDOUT; die("DQBUF"); return b;
}

static void usage(const char *name)
{
	fprintf(stderr, "usage: %s [-p gradient|static|motion|noise|scene] [-r FPS[/DIVISOR]]\n"
		"  [-c NAME=VALUE[,NAME=VALUE...][@FRAME]]... [-x ...]... [-g] [-e]\n"
		"  DEVICE STREAM REF WIDTH HEIGHT FRAMES NV12|NV21|YU12 QUALITY JPEG|H264\n", name);
	exit(2);
}

/* -g: write the reference only; DEVICE and STREAM are ignored, REF may be "-" */
static int generate(FILE *ref, unsigned int frames)
{
	unsigned char *plane[3] = { malloc(w * h), malloc(w * h / 2), NULL }, *uv = malloc(w);
	unsigned int stride[3] = { w, w, 0 };

	if (!plane[0] || !plane[1] || !uv) die("frame");
	planar = vu = 0;
	for (unsigned int frame = 0; frame < frames; frame++) {
		put_frame(frame, plane, stride);
		write_reference(ref, plane, stride, uv);
	}
	free(plane[0]); free(plane[1]); free(uv);
	return fclose(ref) ? 1 : 0;
}
int main(int argc, char **argv)
{
	unsigned int fps = 0, divisor = 1, generate_only = 0, tolerate_errors = 0, frames, quality;
	const char *program = argv[0];
	char *end;
	int option, jpeg;

	while ((option = getopt(argc, argv, "p:r:c:x:ge")) != -1) {
		switch (option) {
		case 'p':
			for (pattern = 0; pattern < 5 && strcmp(optarg, patterns[pattern]); pattern++)
				;
			if (pattern == 5) usage(program);
			break;
		case 'r':
			fps = strtoul(optarg, &end, 10);
			if (*end == '/') divisor = strtoul(end + 1, &end, 10);
			if (*end || !fps || !divisor) usage(program);
			break;
		case 'c': case 'x': add_control(optarg, option == 'x'); break;
		case 'g': generate_only = 1; break;
		case 'e': tolerate_errors = 1; break;
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
	if (strcmp(argv[7], "NV12") && strcmp(argv[7], "NV21") && strcmp(argv[7], "YU12")) usage(program);
	planar = !strcmp(argv[7], "YU12"); vu = !strcmp(argv[7], "NV21");
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
	if (other >= 0 || errno != EBUSY) { fprintf(stderr, "HCODEC ownership failed\n"); return 1; }
	struct v4l2_format f = { .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE };
	f.fmt.pix_mp.width = w; f.fmt.pix_mp.height = h;
	f.fmt.pix_mp.pixelformat = planar ? V4L2_PIX_FMT_YUV420M : vu ? V4L2_PIX_FMT_NV21M : V4L2_PIX_FMT_NV12M;
	ctl(VIDIOC_S_FMT, &f); pix = f.fmt.pix_mp;
	f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	f.fmt.pix_mp.pixelformat = jpeg ? V4L2_PIX_FMT_JPEG : V4L2_PIX_FMT_H264;
	ctl(VIDIOC_S_FMT, &f);
	if (f.fmt.pix_mp.pixelformat != (jpeg ? V4L2_PIX_FMT_JPEG : V4L2_PIX_FMT_H264)) return 1;
	if (fps) {
		struct v4l2_streamparm parm = { .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE };

		parm.parm.output.timeperframe.numerator = divisor;
		parm.parm.output.timeperframe.denominator = fps;
		ctl(VIDIOC_S_PARM, &parm);
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

		apply_controls(frame);
		put_frame(frame, plane, stride);
		if (ref) write_reference(ref, plane, stride, uv);
		queue(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, 1, frame);
		uint64_t queued = now_us();
		queue(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, pix.num_planes, frame);
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
