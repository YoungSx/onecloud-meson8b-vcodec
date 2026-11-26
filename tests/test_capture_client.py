"""The real diagnostic client counts pictures drained during source changes."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CaptureClientTests(unittest.TestCase):
    def test_input_presentation_timestamps_in_coded_order(self):
        code = r'''
#define main diagnostic_main
#include "res-switch-test.c"
#undef main
#include <assert.h>
static int parse(const char *text,uint64_t *pts,size_t count){
 FILE *file=tmpfile();assert(file);assert(fputs(text,file)>=0);rewind(file);
 int result=read_input_pts(file,pts,count);fclose(file);return result;
}
int main(void){
 uint64_t pts[4]={0};
 assert(!parse("0\n100000\n33333\n66666\n",pts,4));
 assert(pts[0]==0&&pts[1]==100000&&pts[2]==33333&&pts[3]==66666);
 input_pts=pts;struct v4l2_buffer b={0};
 packet_timestamp(&b,NULL,0,1);assert(!b.timestamp.tv_sec&&b.timestamp.tv_usec==100000);
 pts[0]=123456789;packet_timestamp(&b,NULL,0,0);assert(b.timestamp.tv_sec==123&&b.timestamp.tv_usec==456789);
 const char *bad[]={"", "-1\n", "+1\n", " 1\n", "3ms\n", "1\n2\n", "2147483648000000\n", "18446744073709551616\n", "99999999999999999999999999999999999999999999999999999999999999999999999999999999999\n"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(parse(bad[i],pts,1)==-1);
 assert(parse("1\n",pts,2)==-1);
 assert(parse("000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",pts,2)==-1);
 assert(!parse("2147483647999999",pts,1)&&pts[0]==2147483647999999ULL);
 assert(!parse("1\n2\n \t\r\n",pts,2)&&pts[0]==1&&pts[1]==2);
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

    def test_bounded_capture_delay_parser_and_hold(self):
        code = r'''
#define usleep mock_usleep
#define main diagnostic_main
#include "res-switch-test.c"
#undef main
#include <assert.h>
static unsigned slept;
int mock_usleep(useconds_t delay){slept+=delay;return 0;}
int main(void){
 assert(!parse_capture_delay(NULL)&&!capture_delay_ms);
 assert(!parse_capture_delay("1000")&&capture_delay_ms==1000);
 const char *bad[]={"","-1","+1"," 1","1001","2ms","999999999999999999999999999"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(parse_capture_delay(bad[i])==-1&&capture_delay_ms==1000);
 assert(!parse_capture_delay("50")&&capture_delay_ms==50);
 unsigned char pixels[4]={1,2,3,4};ncap=1;capbufs[0].nplanes=1;
 capbufs[0].planes[0]=(struct plane){.start=pixels,.length=sizeof(pixels)};
 struct v4l2_plane plane={.bytesused=4};struct v4l2_buffer b={.index=0,.length=1,.m.planes=&plane};
 assert(dump_frame(&b)&&pictures==1&&slept==50000);
 plane.bytesused=0;assert(!dump_frame(&b)&&slept==50000);
 assert(!parse_capture_delay("0"));plane.bytesused=4;assert(dump_frame(&b)&&slept==50000);
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

    def test_scaled_size_argument(self):
        code = r'''
#define main diagnostic_main
#include "res-switch-test.c"
#undef main
#include <assert.h>
int main(void){
 assert(!parse_capture_size(NULL)&&!capture_width&&!capture_height);
 assert(!parse_capture_size("1280x720")&&capture_width==1280&&capture_height==720);
 const char *invalid[]={"0x720","1279x720","1280x721","1922x1080","2x1090","1280x720junk","1280","-2x720","99999999x2","4294967298x2","2x-18446744073709551614","184467440737095516162x2"};
 for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){
  assert(parse_capture_size(invalid[i])==-1&&capture_width==1280&&capture_height==720);
 }
 assert(!parse_capture_size("2x2")&&capture_width==2&&capture_height==2);
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

    def test_drm_capture_planes_sync_and_release(self):
        code = r'''
#define ioctl test_ioctl
#define mmap test_mmap
#define munmap test_munmap
#define close test_close
#define main diagnostic_main
#include "res-switch-test.c"
#undef main
#include <assert.h>
#include <stdarg.h>
static unsigned char storage[65536];
static int syncs,unmaps,closes,destroys;
int test_ioctl(int file,unsigned long request,...) {
 va_list ap;va_start(ap,request);void *arg=va_arg(ap,void *);va_end(ap);
 if(request==DRM_IOCTL_MODE_CREATE_DUMB){struct drm_mode_create_dumb *d=arg;
  assert(file==33&&d->width==4096&&d->height==16&&d->bpp==8);d->handle=9;d->size=sizeof(storage);return 0;}
 if(request==DRM_IOCTL_MODE_MAP_DUMB){struct drm_mode_map_dumb *d=arg;assert(file==33&&d->handle==9);d->offset=0x2000;return 0;}
 if(request==DRM_IOCTL_PRIME_HANDLE_TO_FD){struct drm_prime_handle *d=arg;assert(file==33&&d->handle==9);d->fd=17;return 0;}
 if(request==DMA_BUF_IOCTL_SYNC){struct dma_buf_sync *d=arg;assert(file==17);
  assert(d->flags==(unsigned)(syncs?DMA_BUF_SYNC_END|DMA_BUF_SYNC_READ:DMA_BUF_SYNC_START|DMA_BUF_SYNC_READ));syncs++;return 0;}
 if(request==DRM_IOCTL_MODE_DESTROY_DUMB){struct drm_mode_destroy_dumb *d=arg;assert(file==33&&d->handle==9);destroys++;return 0;}
 assert(0);return -1;
}
void *test_mmap(void *addr,size_t n,int prot,int flags,int file,off_t offset){
 assert(!addr&&n==sizeof(storage)&&prot==(PROT_READ|PROT_WRITE)&&flags==MAP_SHARED&&file==33&&offset==0x2000);return storage;}
int test_munmap(void *p,size_t n){assert(p==storage&&n==sizeof(storage));unmaps++;return 0;}
int test_close(int file){assert(file==17);closes++;return 0;}
int main(void){
 assert(capture_memory()==V4L2_MEMORY_MMAP);capture_drm=33;
 assert(capture_memory()==V4L2_MEMORY_DMABUF);
 ncap=1;capbufs[0].nplanes=1;capbufs[0].planes[0].length=sizeof(storage);
 capture_dumb(&capbufs[0].planes[0]);
 struct v4l2_plane plane={.bytesused=8};struct v4l2_buffer b={.index=0,.length=1,.m.planes=&plane};
 capture_import_planes(&b);assert(plane.m.fd==17&&plane.length==sizeof(storage));
 hashf=tmpfile();assert(hashf);
 assert(dump_frame(&b)&&pictures==1&&syncs==2);fclose(hashf);hashf=NULL;
 capture_cleanup();assert(!ncap&&unmaps==1&&closes==1&&destroys==1);
 capture_cleanup();assert(unmaps==1&&closes==1&&destroys==1);return 0;
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
 capbufs[0].planes[0]=(struct plane){.start=y,.length=sizeof(y)};
 capbufs[0].planes[1]=(struct plane){.start=uv,.length=sizeof(uv)};
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
