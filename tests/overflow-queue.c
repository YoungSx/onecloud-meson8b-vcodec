// SPDX-License-Identifier: GPL-2.0
/* Queue four oversized pictures before streaming; recovery must stop at three. */
#define main encode_test_main
#include "encode-test.c"
#undef main

#define QUEUED_PICTURES 4

static void prepare_queue(unsigned int type, unsigned int planes)
{
	struct v4l2_requestbuffers request = {
		.count = QUEUED_PICTURES, .type = type, .memory = V4L2_MEMORY_MMAP,
	};

	ctl(VIDIOC_REQBUFS, &request);
	if (request.count < QUEUED_PICTURES) die("queue too small");
	for (unsigned int index = 0; index < QUEUED_PICTURES; index++) {
		struct v4l2_plane p[VIDEO_MAX_PLANES] = {0};
		struct v4l2_buffer buffer = {
			.index = index, .type = type, .memory = V4L2_MEMORY_MMAP,
			.length = planes, .m.planes = p,
			.timestamp = { .tv_sec = index, .tv_usec = 1234 },
		};
		unsigned char *data[3] = {0};
		unsigned int stride[3] = {0};

		ctl(VIDIOC_QUERYBUF, &buffer);
		if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
			for (unsigned int plane = 0; plane < planes; plane++) {
				data[plane] = mmap(NULL, p[plane].length, PROT_READ | PROT_WRITE,
						   MAP_SHARED, fd, p[plane].m.mem_offset);
				if (data[plane] == MAP_FAILED) die("mmap");
				memset(data[plane], 0, p[plane].length);
				stride[plane] = pix.plane_fmt[plane].bytesperline;
				p[plane].bytesused = pix.plane_fmt[plane].sizeimage;
			}
			put_frame(index, data, stride);
			for (unsigned int plane = 0; plane < planes; plane++)
				if (munmap(data[plane], p[plane].length)) die("munmap");
		}
		ctl(VIDIOC_QBUF, &buffer);
	}
}

int main(int argc, char **argv)
{
	struct v4l2_format f = { .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE };
	struct v4l2_control bitrate = { .id = V4L2_CID_MPEG_VIDEO_BITRATE, .value = 1000000 };
	unsigned int type;

	if (argc != 2) return 2;
	alarm(15);
	w = 1920; h = 1080; pattern = NOISE;
	for (unsigned int i = 0; i < 3; i++) make_texture(&tex[i], 2, i);
	fd = open(argv[1], O_RDWR | O_NONBLOCK);
	if (fd < 0) die("open");
	f.fmt.pix_mp.width = w; f.fmt.pix_mp.height = h;
	f.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12M;
	ctl(VIDIOC_S_FMT, &f); pix = f.fmt.pix_mp;
	if (pix.width != w || pix.height != h || pix.num_planes != 2) return 3;
	f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	f.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
	ctl(VIDIOC_S_FMT, &f);
	ctl(VIDIOC_S_CTRL, &bitrate);
	prepare_queue(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, 1);
	prepare_queue(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, pix.num_planes);
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; ctl(VIDIOC_STREAMON, &type);
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE; ctl(VIDIOC_STREAMON, &type);
	for (;;) {
		struct v4l2_plane plane = {0};
		struct v4l2_buffer buffer = {
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, .memory = V4L2_MEMORY_MMAP,
			.length = 1, .m.planes = &plane,
		};

		if (!ioctl(fd, VIDIOC_DQBUF, &buffer)) {
			if (!(buffer.flags & V4L2_BUF_FLAG_ERROR)) return 4;
			continue;
		}
		if (errno == EIO) break;
		if (errno != EAGAIN) die("DQBUF");
		ready(); usleep(1000);
	}
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE; ctl(VIDIOC_STREAMOFF, &type);
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; ctl(VIDIOC_STREAMOFF, &type);
	close(fd);
	puts("four pictures queued; terminal queue error; both STREAMOFF calls succeeded");
	return 0;
}
