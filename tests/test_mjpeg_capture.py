"""MJPEG PSCALE chroma rows need their own 32-byte canvas alignment."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class MjpegCaptureTests(unittest.TestCase):
    def test_planar_chroma_geometry_and_visible_copy(self):
        source = (ROOT / 'vdec/meson8b-capture.c').read_text()
        helpers = (ROOT / 'vdec/vdec_helpers.c').read_text()
        convert = source[source.index('static int amvdec_copy_window('):
                         source.index('static int amvdec_cpu_capture(')]
        pool = source[source.index('static u32 amvdec_pool_luma('):
                      source.index('static void amvdec_pool_free_slots(')]
        canvas = helpers[helpers.index('static int set_canvas_mjpeg('):
                         helpers.index('static int set_canvas_nv12m(')]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef uint8_t u8;typedef uint32_t u32;typedef uint64_t u64;typedef uintptr_t dma_addr_t;
#define ALIGN(n,a) (((n)+(a)-1)&~((a)-1))
#define SZ_64K 65536
#define V4L2_PIX_FMT_MJPEG 1
#define DMA_FROM_DEVICE 0
#define MESON_CANVAS_WRAP_NONE 0
#define MESON_CANVAS_BLKMODE_LINEAR 0
#include "meson8b-copy.h"
#include "meson8b-window.h"
typedef void (*amvdec_swap64_t)(void *,const void *,unsigned long);
struct format {u32 pixfmt;};
struct amvdec_core {void *dev,*canvas;u64 cropped_frames;};
struct amvdec_session {struct amvdec_core *core;struct format *fmt_out;u32 width,height,reference_width,reference_height;};
struct amvdec_geometry {u32 width,height,reference_width,reference_height,crop_left,crop_top;};
struct amvdec_shadow {dma_addr_t paddr;u32 luma,size;void *vaddr,*cached;};
struct vb2_buffer {u32 num_planes;void *data[2];u32 size[2];};
static u32 amvdec_get_output_size_wh(u32 w,u32 h){return ALIGN(ALIGN(w,32)*ALIGN(h,32),SZ_64K);}
static u32 amvdec_get_output_size(struct amvdec_session *s){return amvdec_get_output_size_wh(s->width,s->height);}
static void *vb2_plane_vaddr(struct vb2_buffer *b,u32 i){return b->data[i];}
static u32 vb2_plane_size(struct vb2_buffer *b,u32 i){return b->size[i];}
static unsigned syncs;
static void dma_sync_single_for_cpu(void *d,dma_addr_t a,u32 n,int dir){(void)d;(void)a;(void)n;(void)dir;syncs++;}
static bool may_use_simd(void){return false;}
static void kernel_neon_begin(void){}
static void kernel_neon_end(void){}
static void amvdec_swap64(void *d,const void *s,unsigned long n){assert(!meson8b_copy_swap64(d,s,n));}
static void amvdec_swap64_neon(void *d,const void *s,unsigned long n){amvdec_swap64(d,s,n);}
static u32 widths[3],heights[3],addresses[3],canvases;
static int canvas_alloc(struct amvdec_session *s,u8 *id){(void)s;*id=canvases++;return 0;}
static int meson_canvas_config(void *c,u8 id,u32 addr,u32 w,u32 h,int wrap,int block,int endian){(void)c;assert(id<3&&!wrap&&!block&&!endian);widths[id]=w;heights[id]=h;addresses[id]=addr;return 0;}
static void amvdec_write_dos(struct amvdec_core *c,u32 reg,u32 val){(void)c;(void)reg;assert(val==0x020100);}
''' + convert + pool + canvas + r'''
static u8 value(u32 plane,u32 x,u32 y){return (x*13+y*29+plane*71)&255;}
int main(void){
 struct amvdec_core core={0};struct format f={V4L2_PIX_FMT_MJPEG};struct amvdec_session s={.core=&core,.fmt_out=&f};
 u32 sizes[][2]={{854,480},{1440,1080},{320,240},{1920,1080},{34,34}};
 for(unsigned cached=0;cached<2;cached++)for(u32 k=0;k<sizeof(sizes)/sizeof(sizes[0]);k++){
  s.width=sizes[k][0];s.height=sizes[k][1];
  u32 luma=amvdec_pool_luma(&s),src_stride=ALIGN(s.width,64)/2,dst_stride=ALIGN(s.width,32);
  assert(src_stride*(ALIGN(s.height,32)/2)<=luma/4);
  u8 *data=malloc(luma+luma/2);assert(data);memset(data,0xa5,luma+luma/2);
  struct amvdec_shadow sh={.paddr=0x100000,.luma=luma,.size=luma+luma/2,.vaddr=data,.cached=cached?data:NULL};
  canvases=0;assert(!set_canvas_mjpeg(&s,&sh,dst_stride,ALIGN(s.height,32),4));
  assert(widths[0]==dst_stride&&widths[1]==src_stride&&widths[2]==src_stride);
  assert(addresses[1]==sh.paddr+luma&&addresses[2]==sh.paddr+luma+luma/4);
  for(u32 p=0;p<2;p++)for(u32 y=0;y<s.height/2;y++)for(u32 x=0;x<s.width/2;x++)
   data[luma+p*luma/4+y*src_stride+(x^7)]=value(p,x,y);
  u32 output=amvdec_get_output_size(&s);struct vb2_buffer b={.num_planes=2,.size={output,output/2}};
  b.data[0]=malloc(output);b.data[1]=malloc(output/2);assert(b.data[0]&&b.data[1]);memset(b.data[1],0xa5,output/2);
  struct amvdec_geometry g={.width=s.width,.height=s.height};
  unsigned before=syncs;
  assert(!amvdec_meson8b_convert(&s,&g,&sh,&b)&&syncs==before+cached);
  for(u32 y=0;y<s.height/2;y++)for(u32 x=0;x<dst_stride;x++){
   u8 expected=x<s.width?value(x&1,x/2,y):0;
   assert(((u8 *)b.data[1])[y*dst_stride+x]==expected);
  }
  for(u32 i=dst_stride*(s.height/2);i<output/2;i++)assert(!((u8 *)b.data[1])[i]);
  sh.luma=src_stride*(s.height/2)*4-4;
  assert(amvdec_meson8b_convert(&s,&g,&sh,&b)==-EINVAL);
  free(b.data[0]);free(b.data[1]);free(data);
 }
 f.pixfmt=0;
 for(unsigned cached=0;cached<2;cached++)for(unsigned crop=0;crop<2;crop++){
  u32 luma=65536;u8 *data=malloc(luma*3/2);assert(data);
  for(u32 p=0;p<2;p++)for(u32 y=0;y<(p?16:32);y++)for(u32 x=0;x<64;x++)
   data[p*luma+y*64+(x^7)]=value(p,x,y);
  struct amvdec_shadow sh={.vaddr=data,.cached=cached?data:NULL,.luma=luma,.size=luma*3/2};
  struct amvdec_geometry g=crop?(struct amvdec_geometry){34,16,64,32,2,4}:(struct amvdec_geometry){.width=64,.height=32};
  struct vb2_buffer b={.num_planes=2,.size={luma,luma/2}};
  b.data[0]=malloc(luma);b.data[1]=malloc(luma/2);assert(b.data[0]&&b.data[1]);
  unsigned before=syncs;
  assert(!amvdec_meson8b_convert(&s,&g,&sh,&b)&&syncs==before+cached);
  for(u32 p=0;p<2;p++)for(u32 y=0;y<(p?g.height/2:g.height);y++)for(u32 x=0;x<g.width;x++)
   assert(((u8 *)b.data[p])[y*64+x]==value(p,x+g.crop_left,y+(p?g.crop_top/2:g.crop_top)));
  if(crop){
   g.crop_left=64;
   assert(amvdec_meson8b_convert(&s,&g,&sh,&b)==-EINVAL);
  }
  free(b.data[0]);free(b.data[1]);free(data);
 }
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'test.c').write_text(code)
            subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            '-I', str(ROOT / 'vdec'), str(root / 'test.c'),
                            '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)
