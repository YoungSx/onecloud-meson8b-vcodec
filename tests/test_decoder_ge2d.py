"""Actual decoder conversion dispatch, geometry, DMA spans and fallback cleanup."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DecoderGe2dTests(unittest.TestCase):
    def test_conversion_padding_fallback_and_cpu_access(self):
        source = (ROOT / "vdec/meson8b-capture.c").read_text()
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#define V4L2_MEMORY_DMABUF 4
typedef uint32_t u32;typedef uint64_t u64;typedef uint64_t dma_addr_t;typedef int64_t ktime_t;
#define U32_MAX UINT32_MAX
#define ERESTARTSYS 512
#define ALIGN(n,a) (((n)+(a)-1)&~((a)-1))
#define V4L2_PIX_FMT_MJPEG 1
#define DMA_BIDIRECTIONAL 0
#define DMA_FROM_DEVICE 2
#define amvdec_core core
#include "ge2d-layout.h"
#include "../vdec/capture-geometry.h"
struct meson8b_ge2d_api {int (*convert)(void *,const struct meson8b_ge2d_image *,const struct meson8b_ge2d_image *,const struct meson8b_ge2d_crop *);};
struct core {void *dev,*ge2d_dev;const struct meson8b_ge2d_api *ge2d_api;u64 ge2d_frames,converted_frames,convert_ns,pool_bytes;};
struct format {u32 pixfmt;};
struct amvdec_geometry {u32 width,height,reference_width,reference_height,crop_left,crop_top;};
struct amvdec_shadow {dma_addr_t paddr;u32 luma,size,width,height;void *vaddr,*cached;};
struct amvdec_session {struct meson_capture_geometry capture;struct core *core;struct format *fmt_out;struct {struct amvdec_shadow capture_stage;} pool;};
struct dma_resv {long result;unsigned waits;bool write;};
struct dma_buf {struct dma_resv *resv;};
struct vb2_buffer {u32 num_planes,memory;struct {u32 data_offset,size;struct dma_buf *dbuf;void *vaddr;dma_addr_t dma;} planes[2];};
static int gpu_calls,cpu_calls,begin_calls,end_calls,conversion_error,begin_error,end_error;
static int begin_once_error,cpu_error;static unsigned cpu_access,vmaps,resv_waits,sync_calls;
static void *begin_failure,*end_failure;
static unsigned allocations,releases;static int allocation_error;static bool staged;static u32 output_luma=1024;
static u32 expected_width=34,expected_height=16,expected_crop_left=2,expected_crop_top=4;
static unsigned char y[2048],uv[1024];static unsigned char *stage_address,*stage_cached;
static u32 amvdec_get_output_size_wh(u32 w,u32 h){(void)w;(void)h;return output_luma;}
static unsigned msecs_to_jiffies(unsigned ms){assert(ms==1000);return 100;}
static long dma_resv_wait_timeout(struct dma_resv *r,bool all,bool intr,unsigned timeout){
 assert(r&&all&&intr&&timeout==100&&!cpu_access);r->waits++;resv_waits++;return r->result;
}
static void dma_sync_single_for_cpu(void *d,dma_addr_t addr,size_t size,int dir){
 (void)d;assert(addr==0x400000&&dir==DMA_FROM_DEVICE&&stage_cached&&stage_address);
 memcpy(stage_cached,stage_address,size);sync_calls++;
}
static void *vb2_plane_vaddr(struct vb2_buffer *b,unsigned i){assert(i<b->num_planes&&(!b->planes[i].dbuf||cpu_access));vmaps++;return b->planes[i].vaddr;}
static u32 vb2_plane_size(struct vb2_buffer *b,unsigned i){assert(i<b->num_planes);return b->planes[i].size;}
static dma_addr_t vb2_dma_contig_plane_dma_addr(struct vb2_buffer *b,unsigned i){assert(i<b->num_planes);assert(b->memory==V4L2_MEMORY_DMABUF);return b->planes[i].dma;}
static int dma_buf_begin_cpu_access(struct dma_buf *d,int direction){
 assert(!direction);begin_calls++;
 if(!begin_failure||begin_failure==d){if(begin_error)return begin_error;if(begin_once_error){int ret=begin_once_error;begin_once_error=0;return ret;}}
 cpu_access++;return 0;
}
static int dma_buf_end_cpu_access(struct dma_buf *d,int direction){
 assert(!direction&&cpu_access);end_calls++;cpu_access--;
 if(!end_failure||end_failure==d){int ret=end_error;end_error=0;return ret;}return 0;
}
static ktime_t ktime_get(void){return 100;}
static ktime_t ktime_sub(ktime_t a,ktime_t b){return a-b;}
static u64 ktime_to_ns(ktime_t t){return t;}
static int amvdec_meson8b_convert(struct amvdec_session *s,const struct amvdec_geometry *g,const struct amvdec_shadow *sh,struct vb2_buffer *b){
 (void)s;(void)g;(void)sh;for(unsigned i=0;i<b->num_planes;i++)assert(!b->planes[i].dbuf||cpu_access);cpu_calls++;return cpu_error;
}
static void amvdec_shadow_free(struct amvdec_session *s,struct amvdec_shadow *sh){(void)s;free(sh->vaddr);free(sh->cached);stage_cached=NULL;memset(sh,0,sizeof(*sh));releases++;}
static int amvdec_shadow_alloc(struct amvdec_session *s,struct amvdec_shadow *sh,u32 luma,bool cached){(void)s;allocations++;if(allocation_error)return allocation_error;sh->vaddr=malloc(luma*3/2);assert(sh->vaddr);stage_address=sh->vaddr;memset(sh->vaddr,0xcc,luma*3/2);sh->cached=cached?malloc(luma*3/2):NULL;if(cached){assert(sh->cached);memset(sh->cached,0xdd,luma*3/2);}stage_cached=sh->cached;sh->paddr=0x400000;sh->luma=luma;sh->size=luma*3/2;sh->width=0;sh->height=0;return 0;}
static int convert(void *d,const struct meson8b_ge2d_image *in,const struct meson8b_ge2d_image *out,const struct meson8b_ge2d_crop *crop){
 assert(!cpu_access);
 assert(d&&in->width==64&&in->height==32&&in->stride==64&&in->nv12&&!in->little_endian);
 assert(out->width==expected_width&&out->height==expected_height&&out->stride==ALIGN(expected_width,32)&&out->little_endian&&out->nv12);
 assert(in->address[1]==in->address[0]+2048&&crop->left==expected_crop_left&&crop->top==expected_crop_top);
 for(unsigned p=0;p<2;p++){unsigned rows=p?expected_height/2:expected_height;unsigned char *plane=staged?stage_address+(p?output_luma:0):(p?uv:y);
  for(unsigned row=0;row<rows;row++)for(unsigned x=expected_width;x<out->stride;x++)assert(!plane[row*out->stride+x]);
  for(unsigned i=rows*out->stride;i<(p?512:1024);i++)assert(!plane[i]);
 }
 if(staged){assert(out->address[0]==0x400000&&out->address[1]==0x400000+output_luma);
  if(!conversion_error)for(unsigned p=0;p<2;p++)for(unsigned row=0;row<(p?expected_height/2:expected_height);row++)
   memset(stage_address+(p?output_luma:0)+row*out->stride,0x37+p,expected_width);
 }else{assert(out->address[0]==0x200000&&out->address[1]==0x300000);}
 gpu_calls++;return conversion_error;
}
'''
        code += source[source.index("static int amvdec_cpu_capture("):
                       source.index("bool amvdec_has_pool(")]
        code += r'''
int main(void){
 struct meson8b_ge2d_api api={convert};struct core c={.ge2d_dev=&api,.ge2d_api=&api};
 struct format f={2};struct amvdec_session s={.core=&c,.fmt_out=&f};
 struct amvdec_geometry g={34,16,64,32,2,4};struct amvdec_shadow sh={0x100000,2048,3072,0,0,&api,NULL};
 struct dma_resv resv[2]={{.result=1},{.result=1}};
 struct dma_buf dbuf[2]={{&resv[0]},{&resv[1]}};
 struct vb2_buffer b={.num_planes=2,.memory=V4L2_MEMORY_DMABUF,.planes={{.size=1024,.vaddr=y,.dma=0x200000},
 {.size=512,.vaddr=uv,.dma=0x300000}}};
 memset(y,0xa5,sizeof(y));memset(uv,0xa5,sizeof(uv));
 assert(!amvdec_meson8b_copy_capture(&s,&g,&sh,&b)&&gpu_calls==1&&!cpu_calls&&c.ge2d_frames==1);
 assert(y[0]==0xa5&&y[33]==0xa5&&uv[33]==0xa5);
 conversion_error=-ETIMEDOUT;b.planes[0].dbuf=&dbuf[0];b.planes[1].dbuf=&dbuf[1];
 assert(!amvdec_meson8b_copy_capture(&s,&g,&sh,&b)&&gpu_calls==2&&cpu_calls==1&&begin_calls==4&&end_calls==4&&resv_waits==2);
 f.pixfmt=1;assert(!amvdec_meson8b_copy_capture(&s,&g,&sh,&b)&&gpu_calls==2&&cpu_calls==2&&resv_waits==4);
 begin_error=-EIO;assert(amvdec_meson8b_copy_capture(&s,&g,&sh,&b)==-EIO&&cpu_calls==2);
 b.planes[1].dbuf=&dbuf[1];begin_failure=&dbuf[1];int ends=end_calls;
 assert(amvdec_meson8b_copy_capture(&s,&g,&sh,&b)==-EIO&&cpu_calls==2&&end_calls==ends+1);
 c.ge2d_dev=NULL;b.planes[0].dbuf=b.planes[1].dbuf=NULL;
 assert(!amvdec_meson8b_copy_capture(&s,&g,&sh,&b)&&cpu_calls==3);
 assert(c.converted_frames==4&&c.ge2d_frames==1);
 /* Cached MMAP pictures reuse one coherent destination, including padding. */
 c.ge2d_dev=&api;f.pixfmt=2;b.memory=1;b.planes[0].dbuf=b.planes[1].dbuf=NULL;
 begin_error=0;conversion_error=0;staged=true;
 for(unsigned frame=0;frame<3;frame++){
  memset(y,0xa5,sizeof(y));memset(uv,0xa5,sizeof(uv));
  assert(!amvdec_meson8b_copy_capture(&s,&g,&sh,&b));
  assert(allocations==1&&!releases&&c.pool_bytes==1536&&sync_calls==frame+1);
  for(unsigned p=0;p<2;p++)for(unsigned row=0;row<(p?8u:16u);row++)for(unsigned x=0;x<64;x++)
   assert((p?uv:y)[row*64+x]==(x<34?0x37+p:0));
 }
 /* A smaller output sharing the same luma capacity re-clears stale staging pixels. */
 g.width=36;expected_width=36;
 assert(!amvdec_meson8b_copy_capture(&s,&g,&sh,&b));
 for(unsigned p=0;p<2;p++)for(unsigned row=0;row<(p?8u:16u);row++)for(unsigned x=0;x<64;x++)
  assert((p?uv:y)[row*64+x]==(x<36?0x37+p:0));
 g.width=34;expected_width=34;
 assert(!amvdec_meson8b_copy_capture(&s,&g,&sh,&b));
 for(unsigned p=0;p<2;p++)for(unsigned row=0;row<(p?8u:16u);row++)for(unsigned x=0;x<64;x++)
  assert((p?uv:y)[row*64+x]==(x<34?0x37+p:0));
 conversion_error=-ETIMEDOUT;int calls=cpu_calls;
 assert(!amvdec_meson8b_copy_capture(&s,&g,&sh,&b)&&cpu_calls==calls+1);
 assert(allocations==1&&!releases);
 /* Negotiated scaling changes only the output, preserving source crop. */
 conversion_error=0;expected_width=18;expected_height=8;
 meson_capture_configure(&s.capture,34,16,18,8,true);
 int old_calls=cpu_calls;
 assert(!amvdec_meson8b_copy_capture(&s,&g,&sh,&b)&&cpu_calls==old_calls);
 assert(y[0]==0x37&&y[17]==0x37&&y[18]==0&&uv[0]==0x38);
 conversion_error=-ETIMEDOUT;
 assert(amvdec_meson8b_copy_capture(&s,&g,&sh,&b)==-ETIMEDOUT&&cpu_calls==old_calls);
 c.ge2d_dev=NULL;
 assert(amvdec_meson8b_copy_capture(&s,&g,&sh,&b)==-EOPNOTSUPP&&cpu_calls==old_calls);
 c.ge2d_dev=&api;conversion_error=0;b.memory=V4L2_MEMORY_DMABUF;staged=false;
 b.planes[0].dbuf=&dbuf[0];b.planes[1].dbuf=&dbuf[1];
 assert(!amvdec_meson8b_copy_capture(&s,&g,&sh,&b)&&cpu_calls==old_calls);
 conversion_error=-ETIMEDOUT;
 assert(amvdec_meson8b_copy_capture(&s,&g,&sh,&b)==-ETIMEDOUT&&cpu_calls==old_calls);
 b.memory=1;staged=true;b.planes[0].dbuf=b.planes[1].dbuf=NULL;
 s.capture=(struct meson_capture_geometry){0};expected_width=34;expected_height=16;
 /* A smaller display reallocates staging; a failed allocation keeps fallback. */
 output_luma=2048;b.planes[0].size=2048;b.planes[1].size=1024;allocation_error=-ENOMEM;calls=cpu_calls;
 assert(!amvdec_meson8b_copy_capture(&s,&g,&sh,&b)&&cpu_calls==calls+1);
 assert(allocations==2&&releases==1&&!s.pool.capture_stage.vaddr&&!c.pool_bytes);
 /* Exact-size imported DMA-BUFs need no CPU mapping or padding access. */
 output_luma=1024;b.planes[0].size=1024;b.planes[1].size=512;
 b.memory=V4L2_MEMORY_DMABUF;staged=false;b.planes[0].dbuf=&dbuf[0];b.planes[1].dbuf=&dbuf[1];
 conversion_error=0;begin_failure=end_failure=NULL;
 struct amvdec_geometry exact_g={64,16,64,32,0,0};
 expected_width=64;expected_height=16;expected_crop_left=0;expected_crop_top=0;
 memset(y,0,sizeof(y));memset(uv,0,sizeof(uv));
 void *saved_y=b.planes[0].vaddr,*saved_uv=b.planes[1].vaddr;
 b.planes[0].vaddr=NULL;b.planes[1].vaddr=NULL;
 int old_begin=begin_calls,old_end=end_calls;unsigned old_vmaps=vmaps,old_waits=resv_waits;
 assert(!amvdec_meson8b_copy_capture(&s,&exact_g,&sh,&b));
 assert(begin_calls==old_begin&&end_calls==old_end&&vmaps==old_vmaps&&resv_waits==old_waits+2);
 b.planes[0].vaddr=saved_y;b.planes[1].vaddr=saved_uv;
 expected_width=34;expected_height=16;expected_crop_left=2;expected_crop_top=4;
 /* Imported fence timeouts and interrupts fail before any CPU or engine access. */
 for(int scaled=0;scaled<2;scaled++)for(unsigned p=0;p<2;p++)for(int kind=0;kind<2;kind++){
  meson_capture_configure(&s.capture,34,16,18,8,scaled);
  expected_width=scaled?18:34;expected_height=scaled?8:16;
  resv[p].result=kind?-ERESTARTSYS:0;
  old_begin=begin_calls;old_end=end_calls;old_vmaps=vmaps;old_waits=resv_waits;
  int old_gpu=gpu_calls;calls=cpu_calls;u64 converted=c.converted_frames,accelerated=c.ge2d_frames;
  assert(amvdec_meson8b_copy_capture(&s,&g,&sh,&b)==(kind?-ERESTARTSYS:-ETIMEDOUT));
  assert(resv_waits==old_waits+p+1&&begin_calls==old_begin&&end_calls==old_end&&vmaps==old_vmaps);
  assert(gpu_calls==old_gpu&&cpu_calls==calls&&c.converted_frames==converted&&c.ge2d_frames==accelerated&&!cpu_access);
  resv[p].result=1;
 }
 f.pixfmt=1;resv[1].result=0;int old_gpu=gpu_calls;calls=cpu_calls;old_begin=begin_calls;
 assert(amvdec_meson8b_copy_capture(&s,&g,&sh,&b)==-ETIMEDOUT);
 assert(gpu_calls==old_gpu&&cpu_calls==calls&&begin_calls==old_begin&&!cpu_access);
 resv[1].result=1;f.pixfmt=2;s.capture=(struct meson_capture_geometry){0};expected_width=34;expected_height=16;
 /* Exporter synchronization errors must not be hidden by a later CPU retry. */
 conversion_error=0;end_error=-EIO;calls=cpu_calls;old_gpu=gpu_calls;
 assert(amvdec_meson8b_copy_capture(&s,&g,&sh,&b)==-EIO&&cpu_calls==calls&&gpu_calls==old_gpu);
 /* Both planes reject transient begin/end and mapping failures, scaled or not. */
 for(int scaled=0;scaled<2;scaled++)for(int p=0;p<2;p++)for(int failure=0;failure<3;failure++){
  meson_capture_configure(&s.capture,34,16,18,8,scaled);
  begin_failure=end_failure=b.planes[p].dbuf;
  if(failure==0)begin_once_error=-EINTR;
  if(failure==1)end_error=-EIO;
  if(failure==2)b.planes[p].vaddr=NULL;
  old_begin=begin_calls;old_end=end_calls;old_vmaps=vmaps;
  u64 converted=c.converted_frames,accelerated=c.ge2d_frames;
  int expected=failure==0?-EINTR:failure==1?-EIO:-EINVAL;
  assert(amvdec_meson8b_copy_capture(&s,&g,&sh,&b)==expected);
  assert(cpu_calls==calls&&gpu_calls==old_gpu&&!cpu_access);
  assert(begin_calls==old_begin+p+1&&end_calls==old_end+p+(failure!=0));
  assert(vmaps==old_vmaps+p+(failure!=0));
  assert(c.converted_frames==converted&&c.ge2d_frames==accelerated);
  b.planes[p].vaddr=p?(void *)uv:(void *)y;
 }
 /* Invalid offsets/capacity never invoke either conversion path. */
 s.capture=(struct meson_capture_geometry){0};begin_failure=end_failure=NULL;
 b.planes[1].data_offset=8;
 assert(amvdec_meson8b_copy_capture(&s,&g,&sh,&b)==-EINVAL&&cpu_calls==calls&&gpu_calls==old_gpu);
 b.planes[1].data_offset=0;b.planes[1].size=511;
 assert(amvdec_meson8b_copy_capture(&s,&g,&sh,&b)==-EINVAL&&cpu_calls==calls&&gpu_calls==old_gpu);
 b.planes[1].size=512;
 /* An engine error still falls back; CPU errors finish every acquired plane. */
 conversion_error=-ETIMEDOUT;cpu_error=-ERANGE;u64 converted=c.converted_frames;
 assert(amvdec_meson8b_copy_capture(&s,&g,&sh,&b)==-ERANGE&&cpu_calls==calls+1&&!cpu_access);
 assert(c.converted_frames==converted&&gpu_calls==old_gpu+1);
 cpu_error=0;
 assert(!amvdec_meson8b_copy_capture(&s,&g,&sh,&b)&&cpu_calls==calls+2&&!cpu_access);
 assert(c.converted_frames==converted+1&&gpu_calls==old_gpu+2);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "test.c").write_text(code)
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-O1",
                            "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            "-I", str(ROOT / "ge2d"), str(root / "test.c"),
                            "-o", str(root / "test")], check=True)
            subprocess.run([str(root / "test")], check=True)
