// SPDX-License-Identifier: GPL-2.0
/* Standard V4L2 GREY copy/content test; each frame has a different byte pattern. */
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

struct mapped {
	void *data;
	size_t size;
	enum v4l2_buf_type type;
};

static void checked(int result, const char *operation)
{
	if (result < 0) {
		perror(operation);
		exit(1);
	}
}

static unsigned number(const char *text, unsigned limit)
{
	char *end;
	unsigned long value;

	errno = 0;
	value = strtoul(text, &end, 10);
	if (errno || !*text || *end || !value || value > limit) {
		fprintf(stderr, "invalid positive number: %s\n", text);
		exit(2);
	}
	return value;
}

static struct mapped allocate(int fd, enum v4l2_buf_type type)
{
	struct v4l2_requestbuffers request = {
		.count = 1, .type = type, .memory = V4L2_MEMORY_MMAP,
	};
	struct v4l2_plane plane = {};
	struct v4l2_buffer buffer = {
		.type = type, .memory = V4L2_MEMORY_MMAP,
		.length = 1, .m.planes = &plane,
	};
	struct mapped mapping = { .type = type };

	checked(ioctl(fd, VIDIOC_REQBUFS, &request), "REQBUFS");
	checked(ioctl(fd, VIDIOC_QUERYBUF, &buffer), "QUERYBUF");
	mapping.size = plane.length;
	mapping.data = mmap(NULL, plane.length, PROT_READ | PROT_WRITE, MAP_SHARED,
			    fd, plane.m.mem_offset);
	if (mapping.data == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	return mapping;
}

static void queue(int fd, struct mapped *mapping, unsigned bytes, unsigned frame)
{
	struct v4l2_plane plane = { .bytesused = bytes, .length = mapping->size };
	struct v4l2_buffer buffer = {
		.type = mapping->type, .memory = V4L2_MEMORY_MMAP,
		.length = 1, .m.planes = &plane, .field = V4L2_FIELD_NONE,
		.timestamp = { .tv_sec = frame + 1 },
	};

	checked(ioctl(fd, VIDIOC_QBUF, &buffer), "QBUF");
}

static void dequeue(int fd, struct mapped *mapping, unsigned size, unsigned frame)
{
	struct v4l2_plane plane = {};
	struct v4l2_buffer buffer = {
		.type = mapping->type, .memory = V4L2_MEMORY_MMAP,
		.length = 1, .m.planes = &plane,
	};

	checked(ioctl(fd, VIDIOC_DQBUF, &buffer), "DQBUF");
	if ((buffer.flags & V4L2_BUF_FLAG_ERROR) ||
	    (V4L2_TYPE_IS_CAPTURE(mapping->type) &&
	     (plane.bytesused != size || buffer.timestamp.tv_sec != frame + 1))) {
		fprintf(stderr, "invalid frame %u flags=%#x bytes=%u timestamp=%ld\n",
			frame, buffer.flags, plane.bytesused, (long)buffer.timestamp.tv_sec);
		exit(1);
	}
}

int main(int argc, char **argv)
{
	struct v4l2_format format = { .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE };
	enum v4l2_buf_type out = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	enum v4l2_buf_type cap = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	struct mapped src, dst;
	unsigned width, height, out_width, out_height, frames, size, out_size;
	int scale;
	int fd;

	if (argc != 5 && argc != 7) {
		fprintf(stderr, "usage: %s DEVICE WIDTH HEIGHT FRAMES [OUT_WIDTH OUT_HEIGHT]\n", argv[0]);
		return 2;
	}
	width = number(argv[2], 1920);
	height = number(argv[3], 1088);
	frames = number(argv[4], 1000);
	out_width = argc == 7 ? number(argv[5], 1920) : width;
	out_height = argc == 7 ? number(argv[6], 1088) : height;
	scale = out_width != width || out_height != height;
	if (width % 32 || out_width % 32) {
		fprintf(stderr, "width must be a multiple of 32\n");
		return 2;
	}
	fd = open(argv[1], O_RDWR | O_NONBLOCK);
	checked(fd, "open");
	format.fmt.pix_mp.width = width;
	format.fmt.pix_mp.height = height;
	format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_GREY;
	checked(ioctl(fd, VIDIOC_S_FMT, &format), "S_FMT OUTPUT");
	format.type = cap;
	format.fmt.pix_mp.width = out_width;
	format.fmt.pix_mp.height = out_height;
	checked(ioctl(fd, VIDIOC_S_FMT, &format), "S_FMT CAPTURE");
	if (format.fmt.pix_mp.width != out_width || format.fmt.pix_mp.height != out_height ||
	    format.fmt.pix_mp.plane_fmt[0].bytesperline != out_width) {
		fprintf(stderr, "unexpected negotiated layout\n");
		return 1;
	}
	size = width * height;
	out_size = out_width * out_height;
	src = allocate(fd, out);
	dst = allocate(fd, cap);
	checked(ioctl(fd, VIDIOC_STREAMON, &out), "STREAMON OUTPUT");
	checked(ioctl(fd, VIDIOC_STREAMON, &cap), "STREAMON CAPTURE");
	for (unsigned frame = 0; frame < frames; frame++) {
		struct pollfd pollfd = { .fd = fd, .events = POLLIN };
		uint8_t *input = src.data;

		for (unsigned i = 0; i < size; i++)
			input[i] = scale ? (frame * 71) & 255 :
				   (i * 37 + i / width * 13 + frame * 71) & 255;
		memset(dst.data, 0xa5, out_size);
		queue(fd, &dst, 0, frame);
		queue(fd, &src, size, frame);
		if (poll(&pollfd, 1, 2000) != 1 || !(pollfd.revents & POLLIN)) {
			fprintf(stderr, "frame %u timed out or queue failed\n", frame);
			return 1;
		}
		dequeue(fd, &dst, out_size, frame);
		dequeue(fd, &src, size, frame);
		int mismatch = 0;
		if (scale) {
			const uint8_t *output = dst.data;

			for (unsigned i = 0; i < out_size; i++)
				mismatch |= output[i] != ((frame * 71) & 255);
		} else {
			mismatch = memcmp(src.data, dst.data, size);
		}
		if (mismatch) {
			fprintf(stderr, "frame %u content mismatch\n", frame);
			return 1;
		}
	}
	checked(ioctl(fd, VIDIOC_STREAMOFF, &cap), "STREAMOFF CAPTURE");
	checked(ioctl(fd, VIDIOC_STREAMOFF, &out), "STREAMOFF OUTPUT");
	munmap(src.data, src.size);
	munmap(dst.data, dst.size);
	close(fd);
	printf("PASS: %u GREY frames %ux%u -> %ux%u, %s and timestamps\n",
	       frames, width, height, out_width, out_height,
	       scale ? "constant-plane bytes" : "complete bytes");
	return 0;
}
