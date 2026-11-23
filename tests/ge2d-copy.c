// SPDX-License-Identifier: GPL-2.0
/* Standard V4L2 copy/crop/content test for GREY, NV12 and NV12M. */
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <linux/dma-buf.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
typedef uint32_t u32;

struct mapped {
	void *data[2];
	size_t size[2];
	u32 nplanes;
	enum v4l2_buf_type type;
	enum v4l2_memory memory;
	int dma_fd[2];
};

static void checked(int result, const char *operation)
{
	if (result < 0) {
		perror(operation);
		exit(1);
	}
}

static unsigned int number(const char *text, unsigned int limit)
{
	char *end;
	unsigned long value;

	errno = 0;
	value = strtoul(text, &end, 10);
	if (errno || !*text || *end || value > limit) {
		fprintf(stderr, "invalid argument: %s\n", text);
		exit(2);
	}
	return value;
}

static struct mapped allocate(int fd, enum v4l2_buf_type type, u32 nplanes)
{
	struct v4l2_requestbuffers request = {
		.count = 1, .type = type, .memory = V4L2_MEMORY_MMAP,
	};
	struct v4l2_plane planes[2] = {};
	struct v4l2_buffer buffer = {
		.type = type, .memory = V4L2_MEMORY_MMAP,
		.length = nplanes, .m.planes = planes,
	};
	struct mapped mapping = {
		.type = type, .nplanes = nplanes, .memory = V4L2_MEMORY_MMAP,
	};
	u32 i;

	checked(ioctl(fd, VIDIOC_REQBUFS, &request), "REQBUFS");
	checked(ioctl(fd, VIDIOC_QUERYBUF, &buffer), "QUERYBUF");
	for (i = 0; i < nplanes; i++) {
		mapping.size[i] = planes[i].length;
		mapping.data[i] = mmap(NULL, planes[i].length, PROT_READ | PROT_WRITE,
				       MAP_SHARED, fd, planes[i].m.mem_offset);
		if (mapping.data[i] == MAP_FAILED) {
			perror("mmap");
			exit(1);
		}
	}
	return mapping;
}

/* Import buffers exported from a separate non-streaming GE2D file handle. */
static void import_mapping(int fd, int exporter, struct mapped *mapping)
{
	struct v4l2_requestbuffers request = {
		.count = 1, .type = mapping->type, .memory = V4L2_MEMORY_DMABUF,
	};
	u32 i;

	for (i = 0; i < mapping->nplanes; i++) {
		struct v4l2_exportbuffer export = {
			.type = mapping->type, .plane = i, .flags = O_RDWR | O_CLOEXEC,
		};

		checked(ioctl(exporter, VIDIOC_EXPBUF, &export), "EXPBUF");
		mapping->dma_fd[i] = export.fd;
	}
	checked(ioctl(fd, VIDIOC_REQBUFS, &request), "REQBUFS DMABUF");
	mapping->memory = V4L2_MEMORY_DMABUF;
}

static void cpu_access(struct mapped *mapping, uint64_t flags)
{
	u32 i;

	if (mapping->memory != V4L2_MEMORY_DMABUF)
		return;
	for (i = 0; i < mapping->nplanes; i++) {
		struct dma_buf_sync sync = { .flags = flags };

		checked(ioctl(mapping->dma_fd[i], DMA_BUF_IOCTL_SYNC, &sync), "DMA_BUF_SYNC");
	}
}

static void queue(int fd, struct mapped *mapping,
		  const struct v4l2_pix_format_mplane *format, unsigned int frame)
{
	struct v4l2_plane planes[2] = {};
	struct v4l2_buffer buffer = {
		.type = mapping->type, .memory = mapping->memory,
		.length = mapping->nplanes, .m.planes = planes, .field = V4L2_FIELD_NONE,
		.timestamp = { .tv_sec = frame + 1 },
	};
	u32 i;

	for (i = 0; i < mapping->nplanes; i++) {
		planes[i].length = mapping->size[i];
		if (mapping->memory == V4L2_MEMORY_DMABUF)
			planes[i].m.fd = mapping->dma_fd[i];
		if (V4L2_TYPE_IS_OUTPUT(mapping->type))
			planes[i].bytesused = format->plane_fmt[i].sizeimage;
	}
	checked(ioctl(fd, VIDIOC_QBUF, &buffer), "QBUF");
}

static void dequeue(int fd, struct mapped *mapping,
		    const struct v4l2_pix_format_mplane *format, unsigned int frame)
{
	struct v4l2_plane planes[2] = {};
	struct v4l2_buffer buffer = {
		.type = mapping->type, .memory = mapping->memory,
		.length = mapping->nplanes, .m.planes = planes,
	};
	u32 i;

	checked(ioctl(fd, VIDIOC_DQBUF, &buffer), "DQBUF");
	if ((buffer.flags & V4L2_BUF_FLAG_ERROR) ||
	    (V4L2_TYPE_IS_CAPTURE(mapping->type) &&
	     (buffer.timestamp.tv_sec != frame + 1 || buffer.sequence != frame))) {
		fprintf(stderr, "invalid frame %u flags=%#x sequence=%u timestamp=%ld\n",
			frame, buffer.flags, buffer.sequence, (long)buffer.timestamp.tv_sec);
		exit(1);
	}
	for (i = 0; i < mapping->nplanes; i++)
		if (V4L2_TYPE_IS_CAPTURE(mapping->type) &&
		    planes[i].bytesused != format->plane_fmt[i].sizeimage) {
			fprintf(stderr, "invalid frame %u plane %u bytes=%u\n", frame, i,
				planes[i].bytesused);
			exit(1);
		}
}

static unsigned char *plane_data(struct mapped *mapping,
				 const struct v4l2_pix_format_mplane *format, u32 plane)
{
	u32 offset = format->plane_fmt[0].bytesperline * format->height;

	return plane && format->num_planes == 1 ? (unsigned char *)mapping->data[0] + offset :
		mapping->data[plane];
}

static unsigned char pixel(u32 x, u32 y, u32 plane, u32 frame, int scale)
{
	if (scale)
		return plane ? ((x & 1) ? 183 + frame : 53 + frame) : 57 + frame;
	return (x * 37 + y * 13 + plane * 107 + frame * 71) & 255;
}

static void fill_frame(struct mapped *mapping, const struct v4l2_pix_format_mplane *format,
		       u32 frame, int scale)
{
	u32 planes = format->pixelformat == V4L2_PIX_FMT_GREY ? 1 : 2;
	u32 plane, x, y, stride = format->plane_fmt[0].bytesperline;

	for (plane = 0; plane < mapping->nplanes; plane++)
		memset(mapping->data[plane], 0, mapping->size[plane]);
	for (plane = 0; plane < planes; plane++) {
		unsigned char *data = plane_data(mapping, format, plane);
		u32 rows = plane ? format->height / 2 : format->height;

		for (y = 0; y < rows; y++)
			for (x = 0; x < format->width; x++)
				data[y * stride + x] = pixel(x, y, plane, frame, scale);
	}
}

static int check_frame(struct mapped *mapping, const struct v4l2_pix_format_mplane *format,
		       const struct v4l2_rect *crop, u32 frame, int scale)
{
	u32 planes = format->pixelformat == V4L2_PIX_FMT_GREY ? 1 : 2;
	u32 plane, x, y, stride = format->plane_fmt[0].bytesperline;

	for (plane = 0; plane < planes; plane++) {
		const unsigned char *data = plane_data(mapping, format, plane);
		u32 rows = plane ? format->height / 2 : format->height;
		u32 top = plane ? crop->top / 2 : crop->top;

		for (y = 0; y < rows; y++)
			for (x = 0; x < format->width; x++)
				if (data[y * stride + x] != pixel(x + crop->left, y + top,
								 plane, frame, scale)) {
					fprintf(stderr, "frame %u plane %u byte (%u,%u) mismatch\n",
						frame, plane, x, y);
					return 1;
				}
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct v4l2_format format = { .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE };
	struct v4l2_pix_format_mplane input, output;
	enum v4l2_buf_type out = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	enum v4l2_buf_type cap = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	struct v4l2_selection selection = {
		.type = out, .target = V4L2_SEL_TGT_CROP,
	};
	struct mapped src, dst;
	u32 width, height, out_width, out_height, frames, fourcc = V4L2_PIX_FMT_GREY;
	u32 i;
	int scale, fd, exporter = -1;

	if (argc != 5 && argc != 7 && argc != 8 && argc != 12) {
		fprintf(stderr, "usage: %s DEVICE WIDTH HEIGHT FRAMES [OUT_WIDTH OUT_HEIGHT "
			"[GREY|NV12|NV12M [LEFT TOP CROP_WIDTH CROP_HEIGHT]]]\n", argv[0]);
		return 2;
	}
	width = number(argv[2], 1920);
	height = number(argv[3], 1088);
	frames = number(argv[4], 1000);
	out_width = argc >= 7 ? number(argv[5], 1920) : width;
	out_height = argc >= 7 ? number(argv[6], 1088) : height;
	if (argc >= 8) {
		if (!strcmp(argv[7], "NV12"))
			fourcc = V4L2_PIX_FMT_NV12;
		else if (!strcmp(argv[7], "NV12M"))
			fourcc = V4L2_PIX_FMT_NV12M;
		else if (strcmp(argv[7], "GREY"))
			return 2;
	}
	if (!width || !height || !out_width || !out_height || !frames ||
	    (fourcc == V4L2_PIX_FMT_GREY && (width % 32 || out_width % 32)) ||
	    (fourcc != V4L2_PIX_FMT_GREY && ((width | height | out_width | out_height) & 1))) {
		fprintf(stderr, "invalid plane geometry\n");
		return 2;
	}
	fd = open(argv[1], O_RDWR | O_NONBLOCK);
	checked(fd, "open");
	format.fmt.pix_mp.width = width;
	format.fmt.pix_mp.height = height;
	format.fmt.pix_mp.pixelformat = fourcc;
	checked(ioctl(fd, VIDIOC_S_FMT, &format), "S_FMT OUTPUT");
	input = format.fmt.pix_mp;
	format.type = cap;
	format.fmt.pix_mp.width = out_width;
	format.fmt.pix_mp.height = out_height;
	format.fmt.pix_mp.plane_fmt[0].bytesperline = 0;
	checked(ioctl(fd, VIDIOC_S_FMT, &format), "S_FMT CAPTURE");
	output = format.fmt.pix_mp;
	if (input.width != width || input.height != height || input.pixelformat != fourcc ||
	    output.width != out_width || output.height != out_height || output.pixelformat != fourcc) {
		fprintf(stderr, "unexpected negotiated layout\n");
		return 1;
	}
	selection.r.width = width;
	selection.r.height = height;
	if (argc == 12) {
		selection.r.left = number(argv[8], width);
		selection.r.top = number(argv[9], height);
		selection.r.width = number(argv[10], width);
		selection.r.height = number(argv[11], height);
		checked(ioctl(fd, VIDIOC_S_SELECTION, &selection), "S_SELECTION");
	}
	scale = output.width != selection.r.width || output.height != selection.r.height;
	if (getenv("GE2D_DMABUF")) {
		exporter = open(argv[1], O_RDWR | O_NONBLOCK);
		checked(exporter, "exporter open");
		format.type = out;
		format.fmt.pix_mp = input;
		checked(ioctl(exporter, VIDIOC_S_FMT, &format), "exporter S_FMT OUTPUT");
		format.type = cap;
		format.fmt.pix_mp = output;
		checked(ioctl(exporter, VIDIOC_S_FMT, &format), "exporter S_FMT CAPTURE");
	}
	src = allocate(exporter >= 0 ? exporter : fd, out, input.num_planes);
	dst = allocate(exporter >= 0 ? exporter : fd, cap, output.num_planes);
	if (exporter >= 0) {
		import_mapping(fd, exporter, &src);
		import_mapping(fd, exporter, &dst);
	}
	checked(ioctl(fd, VIDIOC_STREAMON, &out), "STREAMON OUTPUT");
	checked(ioctl(fd, VIDIOC_STREAMON, &cap), "STREAMON CAPTURE");
	for (u32 frame = 0; frame < frames; frame++) {
		struct pollfd pollfd = { .fd = fd, .events = POLLIN };
		int mismatch;

		cpu_access(&src, DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE);
		fill_frame(&src, &input, frame, scale);
		cpu_access(&src, DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE);
		cpu_access(&dst, DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE);
		for (i = 0; i < dst.nplanes; i++)
			memset(dst.data[i], 0xa5, dst.size[i]);
		cpu_access(&dst, DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE);
		queue(fd, &dst, &output, frame);
		queue(fd, &src, &input, frame);
		if (poll(&pollfd, 1, 2000) != 1 || !(pollfd.revents & POLLIN)) {
			fprintf(stderr, "frame %u timed out or queue failed\n", frame);
			return 1;
		}
		dequeue(fd, &dst, &output, frame);
		dequeue(fd, &src, &input, frame);
		cpu_access(&dst, DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ);
		mismatch = check_frame(&dst, &output, &selection.r, frame, scale);
		cpu_access(&dst, DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);
		if (mismatch)
			return 1;
	}
	checked(ioctl(fd, VIDIOC_STREAMOFF, &cap), "STREAMOFF CAPTURE");
	checked(ioctl(fd, VIDIOC_STREAMOFF, &out), "STREAMOFF OUTPUT");
	for (i = 0; i < src.nplanes; i++) {
		munmap(src.data[i], src.size[i]);
		if (exporter >= 0)
			close(src.dma_fd[i]);
	}
	for (i = 0; i < dst.nplanes; i++) {
		munmap(dst.data[i], dst.size[i]);
		if (exporter >= 0)
			close(dst.dma_fd[i]);
	}
	close(fd);
	if (exporter >= 0)
		close(exporter);
	printf("PASS: %u frames %ux%u -> %ux%u, %s and timestamps\n",
	       frames, width, height, out_width, out_height,
	       scale ? "constant Y/UV pixels" : "complete visible pixels");
	return 0;
}
