"""The real diagnostic client counts pictures drained during source changes."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CaptureClientTests(unittest.TestCase):
    def test_every_nonempty_capture_counts_with_or_without_hashing(self):
        source = (ROOT / 'tests/res-switch-test.c').read_text()
        start = source.index('\tfor (;;) {\n\t\tstruct pollfd pfd = { .fd = fd,\n\t\t\t\t      .events = POLLPRI')
        loop = source[start:source.index('\n\t(void)running;', start)]
        code = r'''
#define poll test_poll
#define ioctl test_ioctl
#define main diagnostic_main
#include "res-switch-test.c"
#undef main
#include <assert.h>
#include <stdarg.h>
static int stopped,polls;
int test_poll(struct pollfd *fds,nfds_t n,int timeout) {
 assert(n==1&&timeout==10000&&polls<2);
 fds[0].revents=polls++?POLLIN:POLLOUT|POLLIN;return 1;
}
int test_ioctl(int unused,unsigned long request,...) {
 (void)unused;va_list ap;va_start(ap,request);void *arg=va_arg(ap,void *);va_end(ap);
 if((unsigned int)request==(unsigned int)VIDIOC_DECODER_CMD){assert(!out_queued&&pictures==1);stopped++;return 0;}
 struct v4l2_buffer *b=arg;
 if((unsigned int)request!=(unsigned int)VIDIOC_DQBUF&&(unsigned int)request!=(unsigned int)VIDIOC_QBUF)fprintf(stderr,"unexpected ioctl %#lx\n",request);
 assert((unsigned int)request==(unsigned int)VIDIOC_DQBUF||(unsigned int)request==(unsigned int)VIDIOC_QBUF);
 if(b->type==V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE){assert((unsigned int)request==(unsigned int)VIDIOC_DQBUF&&!stopped);return 0;}
 if((unsigned int)request==(unsigned int)VIDIOC_QBUF)return 0;
 b->index=0;b->m.planes[0].bytesused=stopped?0:4;b->m.planes[1].bytesused=stopped?0:2;
 if(stopped)b->flags=V4L2_BUF_FLAG_LAST;
 return 0;
}
static int run_capture_loop(void) {
 enum v4l2_buf_type out_type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,cap_type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
 size_t chunk_idx=1,nchunks=1,espos=0,chunks[]={6};unsigned char esbuf[6]={0};
 unsigned long resolutions=1;int eos=0;
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
			}
		}

		if (pfd.revents & POLLIN) {
			b.type = cap_type;
			b.memory = V4L2_MEMORY_MMAP;
			b.length = VIDEO_MAX_PLANES;
			b.m.planes = planes;
			if (ioctl(fd, VIDIOC_DQBUF, &b) == 0) {
				dump_frame(&b);
				if (stop_early(pictures))
					return 0;
				if (b.flags & V4L2_BUF_FLAG_LAST) {
					printf("EOS frames=%lu resolutions=%lu\n",
					       pictures, resolutions);
					return 0;
				}
				xioctl(VIDIOC_QBUF, &b, "QBUF(CAPTURE)");
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
 return 1;
}
int main(void) {
 unsigned char y[4]={1,2,3,4},uv[2]={5,6};
 struct v4l2_plane planes[2]={{.bytesused=4},{.bytesused=2}};
 struct v4l2_buffer b={.index=0,.length=2,.m.planes=planes};
 capbufs[0].nplanes=2;
 capbufs[0].planes[0]=(struct plane){y,sizeof(y)};
 capbufs[0].planes[1]=(struct plane){uv,sizeof(uv)};
 assert(dump_frame(&b)==1 && pictures==1);
 hashf=tmpfile();assert(hashf);
 assert(dump_frame(&b)==1 && pictures==2);
 rewind(hashf);unsigned long index;unsigned long long hash;size_t bytes;
 assert(fscanf(hashf,"%lu %zu %llx",&index,&bytes,&hash)==3);
 assert(index==1 && bytes==6);
 planes[0].bytesused=planes[1].bytesused=0;
 assert(!dump_frame(&b) && pictures==2);
 fclose(hashf);hashf=NULL;
 assert(!dump_frame(&b) && pictures==2);
 pictures=0;out_queued=1;ncap=1;assert(!run_capture_loop());
 assert(stopped==1&&polls==2&&pictures==1&&!out_queued);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'test.c').write_text(code)
            subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            '-I', str(ROOT / 'tests'), str(root / 'test.c'),
                            '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)
