// SPDX-License-Identifier: GPL-2.0
/* Verify a busy decoder rejects a second STREAMON and accepts it after stop. */
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>

static void check(int rc, const char *what)
{
	if (rc < 0) {
		perror(what);
		exit(EXIT_FAILURE);
	}
}

static void prepare(int fd)
{
	struct v4l2_format fmt = { .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE };
	struct v4l2_requestbuffers req = {
		.type = fmt.type, .memory = V4L2_MEMORY_MMAP, .count = 1,
	};
	static const unsigned char aud[] = { 0, 0, 0, 1, 9, 0xf0 };
	struct v4l2_plane plane = { 0 };
	void *memory;
	struct v4l2_buffer buf = {
		.type = fmt.type, .memory = req.memory, .length = 1,
		.m.planes = &plane,
	};

	fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
	fmt.fmt.pix_mp.width = 320;
	fmt.fmt.pix_mp.height = 240;
	fmt.fmt.pix_mp.num_planes = 1;
	fmt.fmt.pix_mp.plane_fmt[0].sizeimage = 1 << 20;
	check(ioctl(fd, VIDIOC_S_FMT, &fmt), "S_FMT");
	check(ioctl(fd, VIDIOC_REQBUFS, &req), "REQBUFS");
	check(ioctl(fd, VIDIOC_QUERYBUF, &buf), "QUERYBUF");
	memory = mmap(NULL, plane.length, PROT_READ | PROT_WRITE, MAP_SHARED,
		      fd, plane.m.mem_offset);
	if (memory == MAP_FAILED) {
		perror("mmap");
		exit(EXIT_FAILURE);
	}
	memcpy(memory, aud, sizeof(aud));
	check(munmap(memory, plane.length), "munmap");
	plane.bytesused = sizeof(aud);
	check(ioctl(fd, VIDIOC_QBUF, &buf), "QBUF");
}

int main(int argc, char **argv)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	struct v4l2_capability cap;
	int first, second;

	if (argc != 2)
		return 2;
	first = open(argv[1], O_RDWR | O_NONBLOCK);
	check(first, "open first");
	second = open(argv[1], O_RDWR | O_NONBLOCK);
	check(second, "open second");
	prepare(first);
	prepare(second);
	check(ioctl(first, VIDIOC_STREAMON, &type), "STREAMON first");
	check(ioctl(second, VIDIOC_QUERYCAP, &cap), "QUERYCAP while busy");
	if (ioctl(second, VIDIOC_STREAMON, &type) == 0 || errno != EBUSY) {
		fprintf(stderr, "second STREAMON must return EBUSY\n");
		return 1;
	}
	check(ioctl(first, VIDIOC_STREAMOFF, &type), "STREAMOFF first");
	check(ioctl(second, VIDIOC_STREAMON, &type), "STREAMON after release");
	check(ioctl(second, VIDIOC_STREAMOFF, &type), "STREAMOFF second");
	close(second);
	close(first);
	puts("PASS: QUERYCAP while busy, exclusive STREAMON, then takeover");
	return 0;
}
