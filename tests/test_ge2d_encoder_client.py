"""The real client hands one DMA-BUF from GE2D DQBUF to encoder QBUF."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Ge2dEncoderClientTests(unittest.TestCase):
    def test_shared_fd_completion_order_payload_and_teardown(self):
        code = r'''
#define ioctl mock_ioctl
#define mmap mock_mmap
#define munmap mock_munmap
#define poll mock_poll
#define open mock_open
#define close mock_close
#define main diagnostic_main
#include "encode-test.c"
#undef main
#include <assert.h>
#include <stdarg.h>
#include <sys/wait.h>
static unsigned char source_memory[4096],shared_memory[4096];
static unsigned queued,finished,stopped,unmapped,closed;
static int fault;static unsigned current_frame;
int mock_open(const char *path,int flags,...){assert(!strcmp(path,"/dev/ge2d-test")&&flags==(O_RDWR|O_NONBLOCK));return 33;}
void *mock_mmap(void *address,size_t size,int prot,int flags,int file,off_t offset){
 assert(!address&&size==sizeof(source_memory)&&prot==(PROT_READ|PROT_WRITE)&&flags==MAP_SHARED&&file==33&&offset==4096);return source_memory;
}
int mock_munmap(void *address,size_t size){assert(stopped==2&&address==source_memory&&size==sizeof(source_memory));unmapped++;return 0;}
int mock_close(int file){assert(file==33&&unmapped==1);closed++;return 0;}
int mock_poll(struct pollfd *p,nfds_t count,int timeout){assert(count==1&&timeout==1000);p->revents=fault==5?POLLERR:POLLIN;return 1;}
int mock_ioctl(int file,unsigned long request,...){
 va_list ap;va_start(ap,request);void *arg=va_arg(ap,void *);va_end(ap);
 if(file==44){
  struct v4l2_buffer *b=arg;
  assert(request==VIDIOC_QBUF&&finished==2&&b->type==V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
  assert(b->memory==V4L2_MEMORY_DMABUF&&b->length==1&&b->m.planes[0].m.fd==17);return 0;
 }
 assert(file==33);
 if(request==VIDIOC_S_FMT){struct v4l2_format *f=arg;assert(f->fmt.pix_mp.pixelformat==V4L2_PIX_FMT_NV12);
  if(fault==1&&V4L2_TYPE_IS_CAPTURE(f->type))f->fmt.pix_mp.plane_fmt[0].bytesperline+=32;
  return 0;}
 if(request==VIDIOC_REQBUFS){struct v4l2_requestbuffers *r=arg;assert(r->count==1);
  assert(r->memory==(V4L2_TYPE_IS_CAPTURE(r->type)?V4L2_MEMORY_DMABUF:V4L2_MEMORY_MMAP));return 0;}
 if(request==VIDIOC_QUERYBUF){struct v4l2_buffer *b=arg;b->m.planes[0].length=sizeof(source_memory);b->m.planes[0].m.mem_offset=4096;return 0;}
 if(request==VIDIOC_STREAMON)return 0;
 if(request==VIDIOC_STREAMOFF){stopped++;return 0;}
 struct v4l2_buffer *b=arg;
 if(request==VIDIOC_QBUF){
  if(V4L2_TYPE_IS_CAPTURE(b->type)){assert(!queued&&b->memory==V4L2_MEMORY_DMABUF&&b->m.planes[0].m.fd==17);queued=1;}
  else{assert(queued==1&&b->memory==V4L2_MEMORY_MMAP&&b->m.planes[0].bytesused==3072);queued=2;current_frame=b->timestamp.tv_sec;}
  return 0;
 }
 assert(request==VIDIOC_DQBUF&&queued==2);
 if(fault==5||fault==6){errno=EAGAIN;return -1;}
 b->timestamp.tv_sec=current_frame;b->m.planes[0].bytesused=3072;
 if(fault==2)b->flags=V4L2_BUF_FLAG_ERROR;
 if(fault==3)b->m.planes[0].bytesused--;
 if(fault==4)b->timestamp.tv_sec++;
 finished++;return 0;
}
static void scenario(void){
 w=64;h=32;pattern=GRADIENT;format=&formats[1];assert(format->fourcc==V4L2_PIX_FMT_NV12);
 producer_device="/dev/ge2d-test";fd=44;dmabuf="dumb";
 pix=(struct v4l2_pix_format_mplane){.width=w,.height=h,.pixelformat=V4L2_PIX_FMT_NV12,.num_planes=1,
  .plane_fmt={{.bytesperline=64,.sizeimage=3072}}};
 in[0]=(struct mapped){shared_memory,sizeof(shared_memory),17};
 for(unsigned p=0;p<3;p++){gradient[p]=malloc(p?w*h/4:w*h);assert(gradient[p]);memset(gradient[p],100,p?w*h/4:w*h);}
 memset(shared_memory,0xa5,sizeof(shared_memory));producer_setup();
 for(unsigned frame=0;frame<3;frame++){
  queued=finished=0;producer_frame(frame);assert(finished==2);
  for(unsigned i=0;i<sizeof(shared_memory);i++)assert(shared_memory[i]==0xa5);
  queue(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,1,frame);
 }
 producer_cleanup();producer_cleanup();assert(stopped==2&&unmapped==1&&closed==1);
 for(unsigned p=0;p<3;p++)free(gradient[p]);
}
int main(void){
 for(int f=1;f<=6;f++){
  pid_t child=fork();assert(child>=0);
  if(!child){fault=f;scenario();_exit(99);}
  int status;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==1);
 }
 scenario();return 0;
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

    def test_rejects_incompatible_producer_modes_before_open(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'client'
            subprocess.run(['cc', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'tests/encode-test.c'), '-o', str(binary)], check=True)
            for extra, fmt in [([], 'NV12-1'), (['-d', 'lima'], 'NV12-1'),
                               (['-d', 'dumb'], 'NV12'), (['-d', 'dumb', '-g'], 'NV12-1')]:
                result = subprocess.run([str(binary), '-G', '/dev/absent', *extra,
                                         '/dev/absent', '-', '/dev/null', '64', '32', '1',
                                         fmt, '85', 'H264'], capture_output=True)
                self.assertEqual(result.returncode, 2, (extra, fmt))
