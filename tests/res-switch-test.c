// SPDX-License-Identifier: GPL-2.0
/* Minimal stateful V4L2 m2m H.264 decode client for driver validation.
 * Implements the source-change flow the kernel expects:
 * poll(POLLPRI) -> DQEVENT(SOURCE_CHANGE) -> STREAMOFF(CAPTURE)
 * -> G_FMT/S_FMT(CAPTURE) -> REQBUFS(CAPTURE) -> STREAMON(CAPTURE).
 * Prints one line per decoded frame; exits 0 on clean EOS.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/videodev2.h>

#define N_OUT 4
#define MAX_CAP 32

struct plane {
	void *start;
	size_t length;
};

struct buffer {
	struct plane planes[VIDEO_MAX_PLANES];
	int nplanes;
};

static int fd;
static struct buffer outbufs[N_OUT], capbufs[MAX_CAP];
static int ncap;
/* when FRAME_DUMP is set, every decoded frame's planes are appended
 * there (Y then UV) so the output can be diffed against software
 * decode frame by frame */
static FILE *dumpf;

static void dump_frame(struct v4l2_buffer *b)
{
	if (!dumpf)
		return;
	for (int j = 0; j < capbufs[b->index].nplanes; j++)
		fwrite(capbufs[b->index].planes[j].start, 1,
		       b->m.planes[j].bytesused, dumpf);
}

static void xioctl(int req, void *arg, const char *what)
{
	if (ioctl(fd, req, arg) < 0) {
		fprintf(stderr, "%s: %s\n", what, strerror(errno));
		exit(1);
	}
}

static unsigned out_fourcc = V4L2_PIX_FMT_H264;

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

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	xioctl(VIDIOC_G_FMT, &fmt, "G_FMT(CAPTURE)");

	/* Wider than current sizeimage is fine, narrower is not */
	xioctl(VIDIOC_S_FMT, &fmt, "S_FMT(CAPTURE)");

	rb.count = fmt.fmt.pix_mp.num_planes ? 8 : 8;
	rb.type = fmt.type;
	rb.memory = V4L2_MEMORY_MMAP;
	xioctl(VIDIOC_REQBUFS, &rb, "REQBUFS(CAPTURE)");
	ncap = rb.count;
	for (int i = 0; i < ncap; i++) {
		struct v4l2_buffer b = {0};
		struct v4l2_plane planes[VIDEO_MAX_PLANES] = {0};

		b.type = fmt.type;
		b.memory = V4L2_MEMORY_MMAP;
		b.index = i;
		b.length = VIDEO_MAX_PLANES;
		b.m.planes = planes;
		xioctl(VIDIOC_QUERYBUF, &b, "QUERYBUF(CAPTURE)");
		capbufs[i].nplanes = b.length;
		for (unsigned j = 0; j < b.length; j++) {
			capbufs[i].planes[j].length = planes[j].length;
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
		b.memory = V4L2_MEMORY_MMAP;
		b.index = i;
		b.length = capbufs[i].nplanes;
		b.m.planes = planes;
		xioctl(VIDIOC_QBUF, &b, "QBUF(CAPTURE)");
	}
}

static void reinit_capture(void)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	xioctl(VIDIOC_STREAMOFF, &type, "STREAMOFF(CAPTURE)");
	for (int i = 0; i < ncap; i++)
		for (int j = 0; j < capbufs[i].nplanes; j++)
			munmap(capbufs[i].planes[j].start,
			       capbufs[i].planes[j].length);
	{
		struct v4l2_requestbuffers rb = {0};

		rb.type = type;
		rb.memory = V4L2_MEMORY_MMAP;
		xioctl(VIDIOC_REQBUFS, &rb, "REQBUFS(0,CAPTURE)");
	}
	setup_capture();
	xioctl(VIDIOC_STREAMON, &type, "STREAMON(CAPTURE)");
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
	unsigned long frames = 0, resolutions = 1;
	int eos = 0, running = 0;

	/* Chunks split on H.264 start-code boundaries: the firmware
	 * needs per-access-unit feeds to re-sync after a resolution
	 * change, and the OUTPUT plane must leave room for the
	 * ESPARSER search padding after every chunk. */
	size_t *chunks;
	size_t nchunks = 0, chunk_idx = 0, espos = 0;

	if (argc < 3 || argc > 6) {
		fprintf(stderr, "usage: %s DEVICE ELEMENTARY_STREAM "
			"[FOURCC [WIDTH HEIGHT]]\n", argv[0]);
		return 2;
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

	/* Aggregate into access units: a new AU starts at a
	 * slice (nal 1/5) or SPS (7) start code; SEI/AUD/PPS are
	 * folded into the AU they precede. Boundaries are 3- or
	 * 4-byte start codes (00 00 01 / 00 00 00 01).
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
		} else
		while (pos < eslen) {
			long next = eslen;
			long scan;

			for (scan = pos + 4; scan < eslen - 1; scan++) {
				int sc3, nal;

				if (esbuf[scan - 2] || esbuf[scan - 1] || esbuf[scan] != 1)
					continue;
				sc3 = 1;
				if (scan >= 4 && esbuf[scan - 3] == 0)
					sc3 = 0; /* 4-byte start code */
				nal = esbuf[scan + 1] & 0x1f;
				if (nal != 1 && nal != 5 && nal != 7)
					continue;
				next = scan - (sc3 ? 2 : 3);
				break;
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
	setup_capture();
	xioctl(VIDIOC_SUBSCRIBE_EVENT, &sub, "SUBSCRIBE_EVENT");
	xioctl(VIDIOC_STREAMON, &out_type, "STREAMON(OUTPUT)");
	xioctl(VIDIOC_STREAMON, &cap_type, "STREAMON(CAPTURE)");
	running = 1;

	/* Queue the first chunk unconditionally: poll() on an m2m fd
	 * does not report POLLOUT until at least one OUTPUT buffer has
	 * been queued. */
	{
		struct v4l2_buffer b = {0};
		struct v4l2_plane planes[1] = {0};

		memcpy(outbufs[0].planes[0].start, esbuf, chunks[0]);
		b.type = out_type;
		b.memory = V4L2_MEMORY_MMAP;
		b.index = 0;
		b.length = 1;
		b.m.planes = planes;
		planes[0].bytesused = chunks[0];
		xioctl(VIDIOC_QBUF, &b, "QBUF(OUTPUT)");
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
			fprintf(stderr, "timeout; frames=%lu\n", frames);
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
			} else if (ev.type == V4L2_EVENT_EOS) {
				printf("EOS frames=%lu resolutions=%lu\n",
				       frames, resolutions);
				return 0;
			}
			continue;
		}

		if (pfd.revents & POLLOUT && chunk_idx < nchunks) {
			/* Dequeue one completed OUTPUT buffer, refill with
			 * the next start-code-aligned chunk, requeue. One at
			 * a time keeps the esparser from running ahead of
			 * the firmware's parser state. */
			b.type = out_type;
			b.memory = V4L2_MEMORY_MMAP;
			b.length = 1;
			b.m.planes = planes;
			if (ioctl(fd, VIDIOC_DQBUF, &b) == 0) {
				size_t chunk = chunks[chunk_idx];

				if (chunk > outbufs[b.index].planes[0].length)
					chunk = outbufs[b.index].planes[0].length;
				memcpy(outbufs[b.index].planes[0].start,
				       esbuf + espos, chunk);
				espos += chunk;
				chunk_idx++;

				b.type = out_type;
				b.memory = V4L2_MEMORY_MMAP;
				b.length = 1;
				b.m.planes = planes;
				planes[0].bytesused = chunk;
				xioctl(VIDIOC_QBUF, &b, "QBUF(OUTPUT)");
			}
			continue;
		}

		if (pfd.revents & POLLIN) {
			b.type = cap_type;
			b.memory = V4L2_MEMORY_MMAP;
			b.length = VIDEO_MAX_PLANES;
			b.m.planes = planes;
			if (ioctl(fd, VIDIOC_DQBUF, &b) == 0) {
				frames++;
				dump_frame(&b);
				if (b.flags & V4L2_BUF_FLAG_LAST) {
					printf("EOS frames=%lu resolutions=%lu\n",
					       frames, resolutions);
					return 0;
				}
				xioctl(VIDIOC_QBUF, &b, "QBUF(CAPTURE)");
			}
			continue;
		}

		/* All chunks queued AND all OUTPUT buffers completed:
		 * the esparser has consumed the whole stream, so it is
		 * safe to enter the drain sequence. */
		/* All chunks queued: drain remaining OUTPUT completions so
		 * the esparser queue is empty, then enter drain mode. */
		if (chunk_idx >= nchunks && !eos) {
			struct v4l2_decoder_cmd cmd = { .cmd = V4L2_DEC_CMD_STOP };
			int out_pending = N_OUT;

			/* At this point every OUTPUT buffer was queued; some
			 * are still in flight. Non-blocking DQBUF(OUTPUT)
			 * returns EAGAIN until more complete, so poll the fd
			 * and keep draining both queues.
			 */
			while (out_pending > 0) {
				struct pollfd dpfd = { .fd = fd,
					.events = POLLOUT | POLLIN | POLLPRI };

				if (poll(&dpfd, 1, 5000) < 1)
					break;
				for (;;) {
					b.type = out_type;
					b.memory = V4L2_MEMORY_MMAP;
					b.length = 1;
					b.m.planes = planes;
					if (ioctl(fd, VIDIOC_DQBUF, &b) < 0)
						break;
					out_pending--;
				}
				for (;;) {
					b.type = cap_type;
					b.memory = V4L2_MEMORY_MMAP;
					b.length = VIDEO_MAX_PLANES;
					b.m.planes = planes;
					if (ioctl(fd, VIDIOC_DQBUF, &b) < 0)
						break;
					frames++;
					dump_frame(&b);
					xioctl(VIDIOC_QBUF, &b, "QBUF(CAPTURE)");
				}
			}
			xioctl(VIDIOC_DECODER_CMD, &cmd, "DEC_CMD_STOP");
			eos = 1;
		}
	}
	(void)running;
}
