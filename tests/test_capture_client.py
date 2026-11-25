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
static int stopped,polls,fault;
int test_poll(struct pollfd *fds,nfds_t n,int timeout) {
 assert(n==1&&timeout==10000&&polls<2);
 fds[0].revents=fault==1?POLLERR:(polls?POLLIN:POLLOUT|POLLIN);polls++;return 1;
}
int test_ioctl(int unused,unsigned long request,...) {
 (void)unused;va_list ap;va_start(ap,request);void *arg=va_arg(ap,void *);va_end(ap);
 if((unsigned int)request==(unsigned int)VIDIOC_DECODER_CMD){assert(!out_queued&&pictures==1);stopped++;return 0;}
 struct v4l2_buffer *b=arg;
 assert((unsigned int)request==(unsigned int)VIDIOC_DQBUF||(unsigned int)request==(unsigned int)VIDIOC_QBUF);
 if(b->type==V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE){assert((unsigned int)request==(unsigned int)VIDIOC_DQBUF&&!stopped);if(fault==2)b->flags=V4L2_BUF_FLAG_ERROR;return 0;}
 if((unsigned int)request==(unsigned int)VIDIOC_QBUF)return 0;
 b->index=0;b->m.planes[0].bytesused=stopped?0:4;b->m.planes[1].bytesused=stopped?0:2;
 if(stopped)b->flags=V4L2_BUF_FLAG_LAST;
 return 0;
}
static int run_capture_loop(void) {
 enum v4l2_buf_type out_type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,cap_type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
 size_t chunk_idx=1,nchunks=1,espos=0,chunks[]={6};unsigned char esbuf[6]={0};
 unsigned long resolutions=1;int eos=0;
''' + loop + r'''
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
 for(fault=1;fault<=2;fault++){
  polls=stopped=0;pictures=0;out_queued=1;
  assert(run_capture_loop()==1 && polls==1 && !stopped && !pictures);
 }
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
