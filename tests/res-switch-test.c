// SPDX-License-Identifier: GPL-2.0
/* Minimal stateful V4L2 m2m decode client for driver validation.
 * Implements the source-change flow the kernel expects:
 * poll(POLLPRI) -> DQEVENT(SOURCE_CHANGE) -> STREAMOFF(CAPTURE)
 * -> G_FMT/S_FMT(CAPTURE) -> REQBUFS(CAPTURE) -> STREAMON(CAPTURE).
 * Prints one line per decoded frame; exits 0 on clean EOS.
 * FRAME_DUMP=FILE appends every picture to FILE; FRAME_HASH=FILE writes one
 * line per picture (index, bytes, FNV-1a 64 of the planes); CAPTURE_BUFFERS=N
 * requests N CAPTURE buffers (default 8); STOP_DELAY_MS=N waits N ms after
 * the last input before DEC_CMD_STOP, as a stalled or paused source would.
 * CAPTURE_DMABUF=/dev/dri/cardN imports DRM dumb buffers into CAPTURE.
 * CAPTURE_SIZE=WxH requests an exact scaled output after SOURCE_CHANGE.
 * ENCODE_DEVICE=PATH ENCODE_STREAM=FILE encodes each imported NV12M picture
 * before returning it to the decoder; requires CAPTURE_DMABUF.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/videodev2.h>
#include <linux/dma-buf.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
typedef uint8_t u8;
typedef uint32_t u32;
#include "../vdec/wmv3-header.h"

#define N_OUT 4
#define MAX_CAP 32

struct plane {
	void *start;
	size_t length;
	int dma_fd;
	u32 handle;
};

struct buffer {
	struct plane planes[VIDEO_MAX_PLANES];
	int nplanes;
};

static int fd;
static int capture_drm = -1;
static struct buffer outbufs[N_OUT], capbufs[MAX_CAP];
static int ncap;
/* CAPTURE buffers to request; CAPTURE_BUFFERS overrides */
static unsigned int capture_count = 8;
static unsigned int capture_width, capture_height;
/* pause between the last input and DEC_CMD_STOP; STOP_DELAY_MS overrides */
static unsigned int stop_delay_ms;
/* OUTPUT buffers queued and not yet dequeued */
static int out_queued;
/* when FRAME_DUMP is set, every decoded frame's planes are appended
 * there (Y then UV) so the output can be diffed against software
 * decode frame by frame */
static FILE *dumpf;
/* when FRAME_HASH is set, one line per non-empty picture goes there, so
 * runs can be compared picture by picture without storing the pictures */
static FILE *hashf;
static unsigned long pictures;
static unsigned long streamoff_after;
static int streamoff_capture_first;
static unsigned out_fourcc = V4L2_PIX_FMT_H264;
static const char *encode_device;
static FILE *encode_stream;
static int encode_fd = -1;
static void *encode_data;
static unsigned int encode_size;
static unsigned int encode_streaming;
static struct v4l2_pix_format_mplane encode_format;

static void encode_ioctl(unsigned long request, void *argument, const char *what)
{
	if (ioctl(encode_fd, request, argument)) {
		perror(what);
		exit(1);
	}
}

static struct v4l2_buffer encode_dequeue(unsigned int type, unsigned int count,
				       struct v4l2_plane *planes)
{
	struct v4l2_buffer b = { .type = type, .length = count, .m.planes = planes,
		.memory = V4L2_TYPE_IS_OUTPUT(type) ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP };

	for (unsigned int tries = 0; tries < 50; tries++) {
		if (!ioctl(encode_fd, VIDIOC_DQBUF, &b))
			return b;
		if (errno != EAGAIN)
			break;
		struct pollfd pfd = { .fd = encode_fd, .events = POLLIN | POLLOUT };

		if (poll(&pfd, 1, 100) < 0 || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)))
			break;
		usleep(1000);
	}
	fprintf(stderr, "encoder DQBUF failed or timed out\n");
	exit(1);
}

static void encode_capture_queue(void)
{
	struct v4l2_plane plane = { .length = encode_size };
	struct v4l2_buffer b = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		.memory = V4L2_MEMORY_MMAP, .length = 1, .m.planes = &plane };

	encode_ioctl(VIDIOC_QBUF, &b, "encoder CAPTURE QBUF");
}

/* Stop the consumer before releasing any imported producer buffers. */
static void encode_stop(bool drain)
{
	unsigned int type;

	if (encode_fd < 0)
		return;
	if (drain) {
		struct v4l2_encoder_cmd command = { .cmd = V4L2_ENC_CMD_STOP };
		struct v4l2_plane plane = {0};
		struct v4l2_buffer b;

		encode_capture_queue();
		encode_ioctl(VIDIOC_ENCODER_CMD, &command, "encoder STOP");
		b = encode_dequeue(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, 1, &plane);
		if (!(b.flags & V4L2_BUF_FLAG_LAST) || (b.flags & V4L2_BUF_FLAG_ERROR) || plane.bytesused) {
			fprintf(stderr, "encoder drain did not return an empty LAST\n");
			exit(1);
		}
	}
	if (encode_streaming & 1) {
		type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		encode_ioctl(VIDIOC_STREAMOFF, &type, "encoder OUTPUT STREAMOFF");
	}
	if (encode_streaming & 2) {
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		encode_ioctl(VIDIOC_STREAMOFF, &type, "encoder CAPTURE STREAMOFF");
	}
	if (encode_data && encode_data != MAP_FAILED)
		munmap(encode_data, encode_size);
	close(encode_fd);
	encode_fd = -1;
	encode_streaming = 0;
	encode_data = NULL;
}

static void encode_setup(const struct v4l2_pix_format_mplane *source)
{
	struct v4l2_format f = { .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE };
	struct v4l2_requestbuffers r = { .type = f.type, .memory = V4L2_MEMORY_DMABUF, .count = 1 };
	struct v4l2_plane plane = {0};
	struct v4l2_buffer b = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		.memory = V4L2_MEMORY_MMAP, .length = 1, .m.planes = &plane };
	unsigned int type;

	if (!encode_device)
		return;
	if (capture_drm < 0 || source->pixelformat != V4L2_PIX_FMT_NV12M || source->num_planes != 2) {
		fprintf(stderr, "shared encoding requires imported NV12M CAPTURE\n");
		exit(1);
	}
	encode_fd = open(encode_device, O_RDWR | O_NONBLOCK);
	if (encode_fd < 0) {
		perror("encoder open");
		exit(1);
	}
	f.fmt.pix_mp = *source;
	encode_ioctl(VIDIOC_S_FMT, &f, "encoder OUTPUT S_FMT");
	encode_format = f.fmt.pix_mp;
	if (encode_format.pixelformat != source->pixelformat || encode_format.num_planes != 2 ||
	    encode_format.width != source->width || encode_format.height != source->height) {
		fprintf(stderr, "decoder and encoder formats differ\n");
		exit(1);
	}
	for (unsigned int i = 0; i < 2; i++)
		if (encode_format.plane_fmt[i].bytesperline != source->plane_fmt[i].bytesperline ||
		    encode_format.plane_fmt[i].sizeimage > source->plane_fmt[i].sizeimage) {
			fprintf(stderr, "decoder and encoder plane layouts differ\n");
			exit(1);
		}
	f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	memset(&f.fmt.pix_mp, 0, sizeof(f.fmt.pix_mp));
	f.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
	encode_ioctl(VIDIOC_S_FMT, &f, "encoder CAPTURE S_FMT");
	if (f.fmt.pix_mp.pixelformat != V4L2_PIX_FMT_H264 || f.fmt.pix_mp.num_planes != 1) {
		fprintf(stderr, "encoder did not accept H264\n");
		exit(1);
	}
	encode_ioctl(VIDIOC_REQBUFS, &r, "encoder OUTPUT REQBUFS");
	r.type = b.type;
	r.memory = b.memory;
	r.count = 1;
	encode_ioctl(VIDIOC_REQBUFS, &r, "encoder CAPTURE REQBUFS");
	encode_ioctl(VIDIOC_QUERYBUF, &b, "encoder CAPTURE QUERYBUF");
	encode_size = plane.length;
	encode_data = mmap(NULL, encode_size, PROT_READ | PROT_WRITE, MAP_SHARED,
			   encode_fd, plane.m.mem_offset);
	if (encode_data == MAP_FAILED) {
		perror("encoder capture mmap");
		exit(1);
	}
	type = b.type;
	encode_ioctl(VIDIOC_STREAMON, &type, "encoder CAPTURE STREAMON");
	encode_streaming |= 2;
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	encode_ioctl(VIDIOC_STREAMON, &type, "encoder OUTPUT STREAMON");
	encode_streaming |= 1;
}

/* The decoder's DQBUF grants ownership until both encoder queues return. */
static void encode_frame(const struct v4l2_buffer *source)
{
	struct v4l2_plane planes[2] = {0};
	struct v4l2_buffer b = { .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.memory = V4L2_MEMORY_DMABUF, .length = 2, .m.planes = planes,
		.timestamp = source->timestamp };
	const struct buffer *buffer;

	if (!encode_device || !source->m.planes[0].bytesused)
		return;
	if (encode_fd < 0 || source->index >= (unsigned int)ncap || source->length != 2 ||
	    (source->flags & V4L2_BUF_FLAG_ERROR)) {
		fprintf(stderr, "invalid decoder picture for encoder\n");
		exit(1);
	}
	buffer = &capbufs[source->index];
	if (buffer->nplanes != 2) {
		fprintf(stderr, "invalid shared encoder plane count\n");
		exit(1);
	}
	for (unsigned int i = 0; i < 2; i++) {
		if (source->m.planes[i].data_offset ||
		    source->m.planes[i].bytesused < encode_format.plane_fmt[i].sizeimage ||
		    source->m.planes[i].bytesused > buffer->planes[i].length) {
			fprintf(stderr, "invalid shared encoder payload\n");
			exit(1);
		}
		planes[i].m.fd = buffer->planes[i].dma_fd;
		planes[i].length = buffer->planes[i].length;
		planes[i].bytesused = source->m.planes[i].bytesused;
	}
	encode_capture_queue();
	encode_ioctl(VIDIOC_QBUF, &b, "encoder OUTPUT QBUF");
	memset(planes, 0, sizeof(planes));
	b = encode_dequeue(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, 1, planes);
	if (b.index || (b.flags & V4L2_BUF_FLAG_ERROR) || !planes[0].bytesused ||
	    planes[0].bytesused > encode_size ||
	    b.timestamp.tv_sec != source->timestamp.tv_sec ||
	    b.timestamp.tv_usec != source->timestamp.tv_usec) {
		fprintf(stderr, "invalid encoder completion\n");
		exit(1);
	}
	if (fwrite(encode_data, 1, planes[0].bytesused, encode_stream) != planes[0].bytesused) {
		perror("encoded stream write");
		exit(1);
	}
	b = encode_dequeue(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, 2, planes);
	if (b.index || (b.flags & V4L2_BUF_FLAG_ERROR)) {
		fprintf(stderr, "encoder did not return shared input\n");
		exit(1);
	}
}

static int parse_capture_size(const char *text)
{
	const char *height_text;
	char *end;
	unsigned long width, height;

	if (!text)
		return 0;
	if (*text < '0' || *text > '9')
		return -1;
	errno = 0;
	width = strtoul(text, &end, 10);
	if (errno || *end != 'x')
		return -1;
	height_text = end + 1;
	if (*height_text < '0' || *height_text > '9')
		return -1;
	height = strtoul(height_text, &end, 10);
	if (errno || *end ||
	    !width || !height || width > 1920 || height > 1088 ||
	    ((width | height) & 1))
		return -1;
	capture_width = width;
	capture_height = height;
	return 0;
}

static unsigned int capture_memory(void)
{
	return capture_drm >= 0 ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
}

static void capture_sync(struct plane *plane, uint64_t flags)
{
	struct dma_buf_sync sync = { .flags = flags };

	if (capture_drm >= 0 && ioctl(plane->dma_fd, DMA_BUF_IOCTL_SYNC, &sync)) {
		perror("CAPTURE DMA_BUF_SYNC");
		exit(1);
	}
}

static void capture_cleanup(void)
{
	encode_stop(false);
	for (int i = 0; i < ncap; i++)
		for (int j = 0; j < capbufs[i].nplanes; j++) {
			struct plane *plane = &capbufs[i].planes[j];

			if (plane->start && plane->start != MAP_FAILED)
				munmap(plane->start, plane->length);
			if (capture_drm >= 0 && plane->handle) {
				struct drm_mode_destroy_dumb destroy = { .handle = plane->handle };

				if (plane->dma_fd >= 0)
					close(plane->dma_fd);
				ioctl(capture_drm, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
			}
			memset(plane, 0, sizeof(*plane));
		}
	ncap = 0;
}

static void capture_dumb(struct plane *plane)
{
	struct drm_mode_create_dumb create = {
		.width = 4096, .height = (plane->length + 4095) / 4096, .bpp = 8,
	};
	struct drm_mode_map_dumb map = {0};
	struct drm_prime_handle prime = { .flags = DRM_CLOEXEC | DRM_RDWR };

	plane->dma_fd = -1;
	if (ioctl(capture_drm, DRM_IOCTL_MODE_CREATE_DUMB, &create)) {
		perror("CAPTURE dumb create");
		exit(1);
	}
	map.handle = create.handle;
	prime.handle = create.handle;
	plane->handle = create.handle;
	if (create.size < plane->length ||
	    ioctl(capture_drm, DRM_IOCTL_MODE_MAP_DUMB, &map) ||
	    ioctl(capture_drm, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime)) {
		perror("CAPTURE dumb export");
		exit(1);
	}
	plane->dma_fd = prime.fd;
	plane->length = create.size;
	plane->start = mmap(NULL, plane->length, PROT_READ | PROT_WRITE,
			    MAP_SHARED, capture_drm, map.offset);
	if (plane->start == MAP_FAILED) {
		perror("CAPTURE dumb mmap");
		exit(1);
	}
}

static void capture_import_planes(struct v4l2_buffer *b)
{
	if (capture_drm < 0)
		return;
	for (unsigned int j = 0; j < b->length; j++) {
		b->m.planes[j].m.fd = capbufs[b->index].planes[j].dma_fd;
		b->m.planes[j].length = capbufs[b->index].planes[j].length;
	}
}

static void packet_timestamp(struct v4l2_buffer *buffer, const unsigned char *packet,
			     size_t size, size_t index)
{
	size_t offset = index ? 0 : 36;
	uint32_t milliseconds;

	if (out_fourcc != V4L2_PIX_FMT_VC1_ANNEX_L || size < offset + 8)
		return;
	milliseconds = wmv3_le32(packet + offset + 4);
	buffer->timestamp.tv_sec = milliseconds / 1000;
	buffer->timestamp.tv_usec = milliseconds % 1000 * 1000;
	if (packet[offset + 3] & 0x80)
		buffer->flags |= V4L2_BUF_FLAG_KEYFRAME;
}

static int dump_frame(struct v4l2_buffer *b)
{
	unsigned long long hash = 0xcbf29ce484222325ULL;
	size_t bytes = 0;

	for (int j = 0; j < capbufs[b->index].nplanes; j++) {
		const unsigned char *p = capbufs[b->index].planes[j].start;
		size_t n = b->m.planes[j].bytesused;

		if (dumpf || hashf)
			capture_sync(&capbufs[b->index].planes[j], DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ);
		if (dumpf)
			fwrite(p, 1, n, dumpf);
		if (hashf)
			for (size_t i = 0; i < n; i++)
				hash = (hash ^ p[i]) * 0x100000001b3ULL;
		bytes += n;
		if (dumpf || hashf)
			capture_sync(&capbufs[b->index].planes[j], DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);
	}
	if (hashf && bytes)
		fprintf(hashf, "%lu %zu %016llx\n", pictures, bytes, hash);
	if (bytes)
		pictures++;
	if (bytes)
		encode_frame(b);
	return bytes != 0;
}

static void xioctl(int req, void *arg, const char *what)
{
	if (ioctl(fd, req, arg) < 0) {
		fprintf(stderr, "%s: %s\n", what, strerror(errno));
		exit(1);
	}
}

/* Exercise both queue shutdown orders while decoded pictures still exist. */
static int stop_early(unsigned long frames)
{
	enum v4l2_buf_type out = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	enum v4l2_buf_type cap = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	if (!streamoff_after || frames < streamoff_after)
		return 0;
	xioctl(VIDIOC_STREAMOFF, streamoff_capture_first ? &cap : &out,
	       "early STREAMOFF(first)");
	xioctl(VIDIOC_STREAMOFF, streamoff_capture_first ? &out : &cap,
	       "early STREAMOFF(second)");
	printf("STREAMOFF frames=%lu capture_first=%d\n", frames,
	       streamoff_capture_first);
	return 1;
}

static void setup_output(int w, int h)
{
	struct v4l2_format fmt = {0};
	struct v4l2_requestbuffers rb = {0};

	fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	fmt.fmt.pix_mp.pixelformat = out_fourcc;
	fmt.fmt.pix_mp.width = w;
	fmt.fmt.pix_mp.height = h;
	fmt.fmt.pix_mp.plane_fmt[0].sizeimage = 1 << 20;
	fmt.fmt.pix_mp.num_planes = 1;
	xioctl(VIDIOC_S_FMT, &fmt, "S_FMT(OUTPUT)");

	rb.count = N_OUT;
	rb.type = fmt.type;
	rb.memory = V4L2_MEMORY_MMAP;
	xioctl(VIDIOC_REQBUFS, &rb, "REQBUFS(OUTPUT)");
	if (!ioctl(fd, VIDIOC_S_FMT, &fmt) || errno != EBUSY) {
		fprintf(stderr, "OUTPUT S_FMT must reject allocated buffers\n");
		exit(1);
	}
	for (unsigned i = 0; i < rb.count; i++) {
		struct v4l2_buffer b = {0};
		struct v4l2_plane planes[1] = {0};

		b.type = fmt.type;
		b.memory = V4L2_MEMORY_MMAP;
		b.index = i;
		b.length = 1;
		b.m.planes = planes;
		xioctl(VIDIOC_QUERYBUF, &b, "QUERYBUF(OUTPUT)");
		outbufs[i].nplanes = 1;
		outbufs[i].planes[0].length = planes[0].length;
		outbufs[i].planes[0].start =
			mmap(NULL, planes[0].length, PROT_READ | PROT_WRITE,
			     MAP_SHARED, fd, planes[0].m.mem_offset);
		if (outbufs[i].planes[0].start == MAP_FAILED) {
			perror("mmap(OUTPUT)");
			exit(1);
		}
	}
}

static void setup_capture(void)
{
	struct v4l2_format fmt = {0};
	struct v4l2_requestbuffers rb = {0};
	u32 width, height;

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	xioctl(VIDIOC_G_FMT, &fmt, "G_FMT(CAPTURE)");
	width = fmt.fmt.pix_mp.width;
	height = fmt.fmt.pix_mp.height;
	if (capture_width) {
		struct v4l2_selection crop = {
			.type = fmt.type, .target = V4L2_SEL_TGT_CROP,
		};

		xioctl(VIDIOC_G_SELECTION, &crop, "G_SELECTION(source crop)");
		width = crop.r.width;
		height = crop.r.height;
	}

	/* Until scaling is negotiated, CAPTURE cannot alter source geometry. */
	fmt.fmt.pix_mp.width = width + 32;
	fmt.fmt.pix_mp.height = height + 32;
	xioctl(VIDIOC_TRY_FMT, &fmt, "TRY_FMT(CAPTURE geometry)");
	if (fmt.fmt.pix_mp.width != width || fmt.fmt.pix_mp.height != height) {
		fprintf(stderr, "CAPTURE TRY_FMT changed the source geometry\n");
		exit(1);
	}
	fmt.fmt.pix_mp.width = width + 32;
	fmt.fmt.pix_mp.height = height + 32;
	xioctl(VIDIOC_S_FMT, &fmt, "S_FMT(CAPTURE)");
	if (fmt.fmt.pix_mp.width != width || fmt.fmt.pix_mp.height != height) {
		fprintf(stderr, "CAPTURE S_FMT changed the source geometry\n");
		exit(1);
	}
	if (capture_width) {
		struct v4l2_format current = { .type = fmt.type };

		fmt.fmt.pix_mp.width = capture_width;
		fmt.fmt.pix_mp.height = capture_height;
		xioctl(VIDIOC_S_FMT, &fmt, "S_FMT(CAPTURE scaled)");
		xioctl(VIDIOC_G_FMT, &current, "G_FMT(CAPTURE scaled)");
		if (fmt.fmt.pix_mp.width != capture_width ||
		    fmt.fmt.pix_mp.height != capture_height ||
		    current.fmt.pix_mp.width != capture_width ||
		    current.fmt.pix_mp.height != capture_height) {
			fprintf(stderr, "requested CAPTURE_SIZE is not supported\n");
			exit(1);
		}
		printf("CAPTURE_SIZE %ux%u\n", capture_width, capture_height);
	}
	encode_setup(&fmt.fmt.pix_mp);

	rb.count = capture_count;
	rb.type = fmt.type;
	rb.memory = capture_memory();
	xioctl(VIDIOC_REQBUFS, &rb, "REQBUFS(CAPTURE)");
	if (!ioctl(fd, VIDIOC_S_FMT, &fmt) || errno != EBUSY) {
		fprintf(stderr, "CAPTURE S_FMT must reject allocated buffers\n");
		exit(1);
	}
	{
		struct v4l2_format out = { .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE };

		xioctl(VIDIOC_G_FMT, &out, "G_FMT(OUTPUT busy)");
		if (!ioctl(fd, VIDIOC_S_FMT, &out) || errno != EBUSY) {
			fprintf(stderr, "OUTPUT S_FMT must preserve allocated CAPTURE\n");
			exit(1);
		}
	}
	ncap = rb.count;
	for (int i = 0; i < ncap; i++) {
		struct v4l2_buffer b = {0};
		struct v4l2_plane planes[VIDEO_MAX_PLANES] = {0};

		b.type = fmt.type;
		b.memory = capture_memory();
		b.index = i;
		b.length = VIDEO_MAX_PLANES;
		b.m.planes = planes;
		xioctl(VIDIOC_QUERYBUF, &b, "QUERYBUF(CAPTURE)");
		capbufs[i].nplanes = b.length;
		for (unsigned j = 0; j < b.length; j++) {
			capbufs[i].planes[j].length = planes[j].length;
			if (capture_drm >= 0) {
				capture_dumb(&capbufs[i].planes[j]);
				continue;
			}
			capbufs[i].planes[j].start =
				mmap(NULL, planes[j].length,
				     PROT_READ | PROT_WRITE, MAP_SHARED,
				     fd, planes[j].m.mem_offset);
			if (capbufs[i].planes[j].start == MAP_FAILED) {
				perror("mmap(CAPTURE)");
				exit(1);
			}
		}
	}

	for (int i = 0; i < ncap; i++) {
		struct v4l2_buffer b = {0};
		struct v4l2_plane planes[VIDEO_MAX_PLANES] = {0};

		b.type = fmt.type;
		b.memory = capture_memory();
		b.index = i;
		b.length = capbufs[i].nplanes;
		b.m.planes = planes;
		capture_import_planes(&b);
		xioctl(VIDIOC_QBUF, &b, "QBUF(CAPTURE)");
	}
}

/* Dequeue the pictures the old resolution still owes, up to the LAST one. */
static void drain_capture(enum v4l2_buf_type type)
{
	for (;;) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		struct v4l2_buffer b = {0};
		struct v4l2_plane planes[VIDEO_MAX_PLANES] = {0};

		if (poll(&pfd, 1, 5000) < 1) {
			fprintf(stderr, "drain: no picture within 5s\n");
			return;
		}
		b.type = type;
		b.memory = capture_memory();
		b.length = VIDEO_MAX_PLANES;
		b.m.planes = planes;
		/* POLLIN also reports a finished OUTPUT buffer, which makes
		 * this return EAGAIN; anything else ends the drain. */
		if (ioctl(fd, VIDIOC_DQBUF, &b) < 0) {
			if (errno == EAGAIN)
				continue;
			fprintf(stderr, "drain DQBUF: %s\n", strerror(errno));
			return;
		}
		dump_frame(&b);
		if (b.flags & V4L2_BUF_FLAG_LAST)
			return;
		capture_import_planes(&b);
		xioctl(VIDIOC_QBUF, &b, "QBUF(CAPTURE)");
	}
}

static void reinit_capture(void)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	/* Initial source change has no running CAPTURE queue to drain. */
	if (ncap) {
		drain_capture(type);
		encode_stop(true);
		xioctl(VIDIOC_STREAMOFF, &type, "STREAMOFF(CAPTURE)");
	}
	capture_cleanup();
	{
		struct v4l2_requestbuffers rb = {0};

		rb.type = type;
		rb.memory = capture_memory();
		xioctl(VIDIOC_REQBUFS, &rb, "REQBUFS(0,CAPTURE)");
	}
	setup_capture();
	xioctl(VIDIOC_STREAMON, &type, "STREAMON(CAPTURE)");
}

/* Keep one coded picture per OUTPUT buffer for every tested format. */
static int access_unit_start(const unsigned char *p, unsigned int fourcc,
			     int *picture)
{
	unsigned int code = p[0];

	if (fourcc == V4L2_PIX_FMT_MPEG1 || fourcc == V4L2_PIX_FMT_MPEG2) {
		*picture = code == 0;
		return *picture || code == 0xb3 || code == 0xb8;
	}
	if (fourcc == V4L2_PIX_FMT_MPEG4) {
		*picture = code == 0xb6;
		return *picture || code == 0xb0 || code == 0xb3 ||
		       (code >= 0x20 && code <= 0x2f);
	}
	if (fourcc == V4L2_PIX_FMT_VC1_ANNEX_G) {
		*picture = code == 0x0d;
		return *picture || code == 0x0f || code == 0x0e;
	}
	if (fourcc == V4L2_PIX_FMT_HEVC) {
		code = (code >> 1) & 0x3f;
		*picture = code <= 31;
		return *picture ? p[2] & 0x80 :
		       (code >= 32 && code <= 35) || code == 39;
	}
	code &= 0x1f;
	*picture = code == 1 || code == 5;
	return *picture ? p[1] & 0x80 : code >= 6 && code <= 9;
}

int main(int argc, char **argv)
{
	enum v4l2_buf_type out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	enum v4l2_buf_type cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	struct v4l2_event_subscription sub = {
		.type = V4L2_EVENT_SOURCE_CHANGE };
	FILE *es;
	unsigned char *esbuf;
	long eslen;
	unsigned long resolutions = 1;
	bool dynamic = false;
	int eos = 0, running = 0;

	/* Chunks split on H.264 start-code boundaries: the firmware
	 * needs per-access-unit feeds to re-sync after a resolution
	 * change, and the OUTPUT plane must leave room for the
	 * ESPARSER search padding after every chunk. */
	size_t *chunks;
	size_t nchunks = 0, chunk_idx = 0, espos = 0;

	if (getenv("CAPTURE_DMABUF")) {
		capture_drm = open(getenv("CAPTURE_DMABUF"), O_RDWR | O_CLOEXEC);
		if (capture_drm < 0) {
			perror("open CAPTURE DRM");
			return 1;
		}
	}
	atexit(capture_cleanup);
	if (parse_capture_size(getenv("CAPTURE_SIZE"))) {
		fprintf(stderr, "CAPTURE_SIZE must be an even WxH up to 1920x1088\n");
		return 2;
	}
	encode_device = getenv("ENCODE_DEVICE");
	if (encode_device) {
		const char *path = getenv("ENCODE_STREAM");

		if (capture_drm < 0 || !path) {
			fprintf(stderr, "ENCODE_DEVICE requires CAPTURE_DMABUF and ENCODE_STREAM\n");
			return 2;
		}
		encode_stream = fopen(path, "wb");
		if (!encode_stream) {
			perror("open encoded stream");
			return 1;
		}
	}

	if (getenv("STREAMOFF_AFTER"))
		streamoff_after = strtoul(getenv("STREAMOFF_AFTER"), NULL, 10);
	if (getenv("STREAMOFF_CAPTURE_FIRST"))
		streamoff_capture_first = !!strtoul(getenv("STREAMOFF_CAPTURE_FIRST"), NULL, 10);

	if (argc < 3 || argc > 6) {
		fprintf(stderr, "usage: %s DEVICE ELEMENTARY_STREAM "
			"[FOURCC [WIDTH HEIGHT]]\n", argv[0]);
		return 2;
	}
	{
		const char *n = getenv("CAPTURE_BUFFERS");

		if (n) {
			capture_count = strtoul(n, NULL, 10);
			if (capture_count < 1 || capture_count > MAX_CAP) {
				fprintf(stderr, "CAPTURE_BUFFERS must be 1..%d\n", MAX_CAP);
				return 2;
			}
		}
		n = getenv("STOP_DELAY_MS");
		if (n)
			stop_delay_ms = strtoul(n, NULL, 10);
	}
	{
		const char *p = getenv("FRAME_DUMP");

		if (p) {
			dumpf = fopen(p, "wb");
			if (!dumpf) {
				perror(p);
				return 1;
			}
		}
		p = getenv("FRAME_HASH");
		if (p) {
			hashf = fopen(p, "w");
			if (!hashf) {
				perror(p);
				return 1;
			}
		}
	}
	fd = open(argv[1], O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		perror("open");
		return 1;
	}
	if (argc >= 4) {
		const char *f = argv[3];

		if (strlen(f) != 4) {
			fprintf(stderr, "fourcc must be 4 chars\n");
			return 2;
		}
		out_fourcc = f[0] | f[1] << 8 | f[2] << 16 | f[3] << 24;
	}
	if (argc == 6)
		setup_output(atoi(argv[4]), atoi(argv[5]));
	es = fopen(argv[2], "rb");
	if (!es) {
		perror(argv[2]);
		return 1;
	}
	fseek(es, 0, SEEK_END);
	eslen = ftell(es);
	rewind(es);
	esbuf = malloc(eslen);
	if (!esbuf || fread(esbuf, 1, eslen, es) != (size_t)eslen) {
		fprintf(stderr, "cannot read %s\n", argv[2]);
		return 1;
	}
	fclose(es);

	/* Aggregate into access units, one per OUTPUT buffer as the
	 * driver expects. Once the current AU holds a slice, the next
	 * AU starts at a parameter set, AUD or prefix SEI, or at a slice
	 * that begins a picture (first_mb_in_slice 0 for H.264,
	 * first_slice_segment_in_pic_flag for HEVC). Boundaries are 3-
	 * or 4-byte start codes (00 00 01 / 00 00 00 01).
	 * Motion-JPEG instead delimits frames with SOI (ff d8 ff),
	 * so split there. */
	chunks = malloc(sizeof(size_t) * (eslen + 2));
	{
		long pos = 0;

		if (out_fourcc == V4L2_PIX_FMT_MJPEG) {
			while (pos < eslen) {
				long next = eslen;

				for (long scan = pos + 3; scan < eslen - 2; scan++)
					if (esbuf[scan] == 0xff && esbuf[scan + 1] == 0xd8 &&
					    esbuf[scan + 2] == 0xff) {
						next = scan;
						break;
					}
				chunks[nchunks++] = next - pos;
				pos = next;
			}
		} else if (out_fourcc == V4L2_PIX_FMT_VC1_ANNEX_L) {
			if (eslen < 44 || esbuf[3] != 0xc5 ||
			    esbuf[4] != 4 || esbuf[5] || esbuf[6] || esbuf[7]) {
				fprintf(stderr, "expected Annex L sequence and first frame\n");
				return 2;
			}
			while (pos < eslen) {
				long frame = pos ? pos : 36;
				size_t size;

				if (frame + 8 > eslen)
					return 2;
				size = esbuf[frame] | (size_t)esbuf[frame + 1] << 8 |
				       (size_t)esbuf[frame + 2] << 16;
				if (!size || size > (size_t)(eslen - frame - 8)) {
					fprintf(stderr, "truncated Annex L frame\n");
					return 2;
				}
				chunks[nchunks++] = frame + 8 + size - pos;
				pos = frame + 8 + size;
			}
		} else if (out_fourcc == V4L2_PIX_FMT_H263) {
			while (pos < eslen) {
				long next = eslen;

				for (long scan = pos + 3; scan + 2 < eslen; scan++)
					if (!esbuf[scan] && !esbuf[scan + 1] &&
					    (esbuf[scan + 2] & 0xfc) == 0x80) {
						next = scan;
						break;
					}
				chunks[nchunks++] = next - pos;
				pos = next;
			}
		} else
		while (pos < eslen) {
			long next = eslen;
			long scan;
			int have_slice = 0;

			for (scan = pos + 2; scan < eslen - 3; scan++) {
				int slice, starts_au;

				if (esbuf[scan - 2] || esbuf[scan - 1] || esbuf[scan] != 1)
					continue;
				starts_au = access_unit_start(esbuf + scan + 1,
							     out_fourcc, &slice);
				if (have_slice && starts_au) {
					/* 4-byte start codes keep their leading zero */
					next = scan - 2;
					if (next > pos && esbuf[next - 1] == 0)
						next--;
					break;
				}
				have_slice |= slice;
			}
			if (next > pos) {
				chunks[nchunks++] = next - pos;
				pos = next;
			} else {
				chunks[nchunks++] = 4;
				pos += 4;
			}
		}
	}

	if (argc < 6)
		setup_output(1920, 1080);
	dynamic = out_fourcc == V4L2_PIX_FMT_H264 || out_fourcc == V4L2_PIX_FMT_HEVC;
	resolutions = dynamic ? 0 : 1;
	if (!dynamic)
		setup_capture();
	xioctl(VIDIOC_SUBSCRIBE_EVENT, &sub, "SUBSCRIBE_EVENT");
	xioctl(VIDIOC_STREAMON, &out_type, "STREAMON(OUTPUT)");
	if (!dynamic)
		xioctl(VIDIOC_STREAMON, &cap_type, "STREAMON(CAPTURE)");
	running = 1;

	/* Queue the first chunk unconditionally: poll() on an m2m fd
	 * does not report POLLOUT until at least one OUTPUT buffer has
	 * been queued. */
	{
		struct v4l2_buffer b = {0};
		struct v4l2_plane planes[1] = {0};

		if (!nchunks || chunks[0] > outbufs[0].planes[0].length) {
			fprintf(stderr, "first access unit exceeds OUTPUT capacity\n");
			return 1;
		}
		memcpy(outbufs[0].planes[0].start, esbuf, chunks[0]);
		b.type = out_type;
		b.memory = V4L2_MEMORY_MMAP;
		b.index = 0;
		b.length = 1;
		b.m.planes = planes;
		planes[0].bytesused = chunks[0];
		packet_timestamp(&b, esbuf, chunks[0], 0);
		xioctl(VIDIOC_QBUF, &b, "QBUF(OUTPUT)");
		out_queued++;
		chunk_idx = 1;
		espos = chunks[0];
	}

	for (;;) {
		struct pollfd pfd = { .fd = fd,
				      .events = POLLPRI | POLLOUT | POLLIN };
		struct v4l2_buffer b = {0};
		struct v4l2_plane planes[VIDEO_MAX_PLANES] = {0};
		struct v4l2_event ev;

		if (poll(&pfd, 1, 10000) < 1) {
			fprintf(stderr, "timeout; frames=%lu\n", pictures);
			return 1;
		}
		if (ncap && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
			fprintf(stderr, "decoder queue failed; frames=%lu\n", pictures);
			return 1;
		}

		if (pfd.revents & POLLPRI) {
			if (ioctl(fd, VIDIOC_DQEVENT, &ev) == 0 &&
			    ev.type == V4L2_EVENT_SOURCE_CHANGE) {
				struct v4l2_format fmt = {0};

				fmt.type = cap_type;
				xioctl(VIDIOC_G_FMT, &fmt, "G_FMT");
				printf("SRC_CHANGE %ux%u\n",
				       fmt.fmt.pix_mp.width,
				       fmt.fmt.pix_mp.height);
				resolutions++;
				reinit_capture();
			}
			/* EOS may precede dequeuing LAST; finish on that buffer. */
			continue;
		}

		if (pfd.revents & POLLOUT) {
			b.type = out_type;
			b.memory = V4L2_MEMORY_MMAP;
			b.length = 1;
			b.m.planes = planes;
			if (ioctl(fd, VIDIOC_DQBUF, &b) == 0) {
				if (b.flags & V4L2_BUF_FLAG_ERROR) {
					fprintf(stderr, "decoder rejected input access unit\n");
					return 1;
				}
				out_queued--;
				if (chunk_idx < nchunks) {
					size_t chunk = chunks[chunk_idx];

					if (chunk > outbufs[b.index].planes[0].length) {
						fprintf(stderr, "access unit exceeds OUTPUT capacity\n");
						return 1;
					}
					memcpy(outbufs[b.index].planes[0].start, esbuf + espos, chunk);
					planes[0].bytesused = chunk;
					packet_timestamp(&b, esbuf + espos, chunk, chunk_idx);
					xioctl(VIDIOC_QBUF, &b, "QBUF(OUTPUT)");
					out_queued++;
					espos += chunk;
					chunk_idx++;
				}
			} else if (errno != EAGAIN) {
				perror("DQBUF(OUTPUT)");
				return 1;
			}
		}

		if (pfd.revents & POLLIN) {
			b.type = cap_type;
			b.memory = capture_memory();
			b.length = VIDEO_MAX_PLANES;
			b.m.planes = planes;
			if (ioctl(fd, VIDIOC_DQBUF, &b) == 0) {
				if (b.flags & V4L2_BUF_FLAG_ERROR) {
					fprintf(stderr, "decoder returned a corrupt picture\n");
					return 1;
				}
				dump_frame(&b);
				if (stop_early(pictures))
					return 0;
				if (b.flags & V4L2_BUF_FLAG_LAST) {
					encode_stop(true);
					if (encode_stream && fclose(encode_stream)) {
						perror("encoded stream close");
						return 1;
					}
					printf("EOS frames=%lu resolutions=%lu\n",
					       pictures, resolutions);
					return 0;
				}
				capture_import_planes(&b);
				xioctl(VIDIOC_QBUF, &b, "QBUF(CAPTURE)");
			} else if (errno != EAGAIN) {
				perror("DQBUF(CAPTURE)");
				return 1;
			}
		}

		/* STOP follows every completed input; CAPTURE keeps using this same loop. */
		if (chunk_idx >= nchunks && !out_queued && !eos && ncap) {
			struct v4l2_decoder_cmd cmd = { .cmd = V4L2_DEC_CMD_STOP };

			if (stop_delay_ms)
				usleep(stop_delay_ms * 1000);
			xioctl(VIDIOC_DECODER_CMD, &cmd, "DEC_CMD_STOP");
			eos = 1;
		}

	}
	(void)running;
}
