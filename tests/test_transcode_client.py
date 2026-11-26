"""The real decode client retains shared planes until the encoder returns them."""
from pathlib import Path
import subprocess
import tempfile
import os
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TranscodeClientTests(unittest.TestCase):
    def test_shared_planes_completion_drain_and_rejection(self):
        code = r'''
#define ioctl mock_ioctl
#define mmap mock_mmap
#define munmap mock_munmap
#define poll mock_poll
#define open mock_open
#define close mock_close
#define main diagnostic_main
#include "res-switch-test.c"
#undef main
#include <assert.h>
#include <stdarg.h>
#include <sys/wait.h>
static unsigned char encoded[4096],luma[65536],chroma[32768];
static unsigned queued,finished,stopped,unmapped,closed,syncs;
static int fault,draining;
int mock_open(const char *path,int flags,...){assert(!strcmp(path,"/dev/encoder-test")&&flags==(O_RDWR|O_NONBLOCK));return 33;}
int mock_close(int file){assert(file==33);closed++;return 0;}
void *mock_mmap(void *p,size_t n,int prot,int flags,int file,off_t offset){
 assert(!p&&n==sizeof(encoded)&&prot==(PROT_READ|PROT_WRITE)&&flags==MAP_SHARED&&file==33&&offset==8192);return encoded;
}
int mock_munmap(void *p,size_t n){assert(p==encoded&&n==sizeof(encoded));unmapped++;return 0;}
int mock_poll(struct pollfd *p,nfds_t count,int timeout){
 assert(count==1&&timeout==100);p->revents=fault==9?POLLERR:POLLIN;return 1;
}
int mock_ioctl(int file,unsigned long request,...){
 va_list ap;va_start(ap,request);void *arg=va_arg(ap,void *);va_end(ap);
 if(request==DMA_BUF_IOCTL_SYNC){assert(file==17||file==18);syncs++;return 0;}
 assert(file==33);
 if(request==VIDIOC_S_FMT){struct v4l2_format *f=arg;
  if(V4L2_TYPE_IS_OUTPUT(f->type)){
   assert(f->fmt.pix_mp.pixelformat==V4L2_PIX_FMT_NV12M&&f->fmt.pix_mp.num_planes==2);
   f->fmt.pix_mp.plane_fmt[0].sizeimage=2048;f->fmt.pix_mp.plane_fmt[1].sizeimage=1024;
   if(fault==1)f->fmt.pix_mp.plane_fmt[1].bytesperline=32;
  }else f->fmt.pix_mp.num_planes=1;
  return 0;
 }
 if(request==VIDIOC_REQBUFS){struct v4l2_requestbuffers *r=arg;
  assert(r->count==1&&r->memory==(V4L2_TYPE_IS_OUTPUT(r->type)?V4L2_MEMORY_DMABUF:V4L2_MEMORY_MMAP));return 0;
 }
 if(request==VIDIOC_QUERYBUF){struct v4l2_buffer *b=arg;b->m.planes[0].length=sizeof(encoded);b->m.planes[0].m.mem_offset=8192;return 0;}
 if(request==VIDIOC_STREAMON)return 0;
 if(request==VIDIOC_STREAMOFF){assert(finished==2);stopped++;return 0;}
 if(request==VIDIOC_ENCODER_CMD){assert(finished==2);draining=1;return 0;}
 struct v4l2_buffer *b=arg;
 if(request==VIDIOC_QBUF){
  if(V4L2_TYPE_IS_CAPTURE(b->type)){assert(!queued);queued=1;}
  else{
   assert(queued==1&&b->memory==V4L2_MEMORY_DMABUF&&b->length==2);
   assert(b->m.planes[0].m.fd==17&&b->m.planes[1].m.fd==18);
   assert(b->m.planes[0].length==sizeof(luma)&&b->m.planes[1].length==sizeof(chroma));
   assert(b->m.planes[0].bytesused==sizeof(luma)&&b->m.planes[1].bytesused==sizeof(chroma));queued=2;
  }return 0;
 }
 assert(request==VIDIOC_DQBUF);
 if(fault==9||fault==10){errno=EAGAIN;return -1;}
 if(draining){assert(V4L2_TYPE_IS_CAPTURE(b->type)&&queued==1);b->flags=V4L2_BUF_FLAG_LAST;b->m.planes[0].bytesused=0;queued=0;return 0;}
 assert(queued==2);
 if(V4L2_TYPE_IS_CAPTURE(b->type)){
  assert(!finished);b->m.planes[0].bytesused=fault==6?4097:33;b->timestamp.tv_sec=fault==7?999:123;b->timestamp.tv_usec=456;
  if(fault==5)b->flags=V4L2_BUF_FLAG_ERROR;
 }else{assert(finished==1);if(fault==8)b->flags=V4L2_BUF_FLAG_ERROR;queued=0;}
 finished++;return 0;
}
static void scenario(void){
 capture_drm=44;encode_device="/dev/encoder-test";ncap=1;
 capbufs[0].nplanes=2;capbufs[0].planes[0]=(struct plane){.start=luma,.length=sizeof(luma),.dma_fd=17};
 capbufs[0].planes[1]=(struct plane){.start=chroma,.length=sizeof(chroma),.dma_fd=18};
 memset(luma,0xa5,sizeof(luma));memset(chroma,0x5a,sizeof(chroma));
 struct v4l2_pix_format_mplane pix={.width=64,.height=32,.pixelformat=V4L2_PIX_FMT_NV12M,.num_planes=2,
  .plane_fmt={{.sizeimage=65536,.bytesperline=64},{.sizeimage=32768,.bytesperline=64}}};
 encode_stream=tmpfile();assert(encode_stream);encode_setup(&pix);
 struct v4l2_plane planes[2]={{.bytesused=sizeof(luma)},{.bytesused=sizeof(chroma)}};
 struct v4l2_buffer b={.index=0,.length=2,.m.planes=planes,.timestamp={.tv_sec=123,.tv_usec=456}};
 if(fault==2)planes[1].bytesused=1;
 if(fault==3)planes[0].data_offset=8;
 if(fault==4)b.flags=V4L2_BUF_FLAG_ERROR;
 for(unsigned frame=0;frame<3;frame++){
  finished=0;assert(dump_frame(&b)&&finished==2&&!queued);
  for(unsigned i=0;i<sizeof(luma);i++)assert(luma[i]==0xa5);
  for(unsigned i=0;i<sizeof(chroma);i++)assert(chroma[i]==0x5a);
 }
 assert(pictures==3&&!syncs&&ftell(encode_stream)==99);
 encode_stop(true);assert(stopped==2&&unmapped==1&&closed==1&&encode_fd==-1);
 encode_stop(false);assert(stopped==2&&unmapped==1&&closed==1);fclose(encode_stream);
}
int main(void){
 for(int f=1;f<=10;f++){
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

    def test_encoder_requires_shared_capture_and_stream_path(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'client'
            subprocess.run(['cc', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'tests/res-switch-test.c'), '-o', str(binary)], check=True)
            env = {k: v for k, v in os.environ.items()
                   if k not in ['CAPTURE_DMABUF', 'ENCODE_DEVICE', 'ENCODE_STREAM']}
            env['ENCODE_DEVICE'] = '/dev/absent'
            result = subprocess.run([str(binary), '/dev/absent', '/dev/absent'],
                                    env=env, capture_output=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn(b'requires CAPTURE_DMABUF and ENCODE_STREAM', result.stderr)
