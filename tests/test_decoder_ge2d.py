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
typedef uint32_t u32;typedef uint64_t u64;typedef uint64_t dma_addr_t;typedef int64_t ktime_t;
#define U32_MAX UINT32_MAX
#define ALIGN(n,a) (((n)+(a)-1)&~((a)-1))
#define V4L2_PIX_FMT_MJPEG 1
#define DMA_BIDIRECTIONAL 0
#define amvdec_core core
#include "ge2d-layout.h"
struct meson8b_ge2d_api {int (*convert)(void *,const struct meson8b_ge2d_image *,const struct meson8b_ge2d_image *,const struct meson8b_ge2d_crop *);};
struct core {void *ge2d_dev;const struct meson8b_ge2d_api *ge2d_api;u64 ge2d_frames,converted_frames,convert_ns;};
struct format {u32 pixfmt;};
struct amvdec_session {struct core *core;struct format *fmt_out;};
struct amvdec_geometry {u32 width,height,reference_width,reference_height,crop_left,crop_top;};
struct amvdec_shadow {dma_addr_t paddr;u32 luma,size;void *vaddr;};
struct vb2_buffer {u32 num_planes;struct {u32 data_offset,size;void *dbuf,*vaddr;dma_addr_t dma;} planes[2];};
static int gpu_calls,cpu_calls,begin_calls,end_calls,conversion_error,begin_error;
static void *begin_failure;
static unsigned char y[1024],uv[512];
static u32 amvdec_get_output_size_wh(u32 w,u32 h){(void)w;(void)h;return 1024;}
static void *vb2_plane_vaddr(struct vb2_buffer *b,unsigned i){assert(i<b->num_planes);return b->planes[i].vaddr;}
static u32 vb2_plane_size(struct vb2_buffer *b,unsigned i){assert(i<b->num_planes);return b->planes[i].size;}
static dma_addr_t vb2_dma_contig_plane_dma_addr(struct vb2_buffer *b,unsigned i){assert(i<b->num_planes);return b->planes[i].dma;}
static int dma_buf_begin_cpu_access(void *d,int direction){assert(!direction);begin_calls++;return begin_error&&(!begin_failure||begin_failure==d)?begin_error:0;}
static int dma_buf_end_cpu_access(void *d,int direction){(void)d;assert(!direction);end_calls++;return 0;}
static ktime_t ktime_get(void){return 100;}
static ktime_t ktime_sub(ktime_t a,ktime_t b){return a-b;}
static u64 ktime_to_ns(ktime_t t){return t;}
static int amvdec_meson8b_convert(struct amvdec_session *s,const struct amvdec_geometry *g,const struct amvdec_shadow *sh,struct vb2_buffer *b){(void)s;(void)g;(void)sh;(void)b;cpu_calls++;return 0;}
static int convert(void *d,const struct meson8b_ge2d_image *in,const struct meson8b_ge2d_image *out,const struct meson8b_ge2d_crop *crop){
 assert(d&&in->width==64&&in->height==32&&in->stride==64&&in->nv12&&!in->little_endian);
 assert(out->width==34&&out->height==16&&out->stride==64&&out->little_endian&&out->nv12);
 assert(in->address[1]==in->address[0]+2048&&crop->left==2&&crop->top==4);
 for(unsigned p=0;p<2;p++){unsigned rows=p?8:16;unsigned char *plane=p?uv:y;
  for(unsigned row=0;row<rows;row++)for(unsigned x=34;x<64;x++)assert(!plane[row*64+x]);
  for(unsigned i=rows*64;i<(p?512:1024);i++)assert(!plane[i]);
 }
 gpu_calls++;return conversion_error;
}
'''
        code += source[source.index("static int amvdec_ge2d_capture("):
                       source.index("bool amvdec_has_pool(")]
        code += r'''
int main(void){
 struct meson8b_ge2d_api api={convert};struct core c={.ge2d_dev=&api,.ge2d_api=&api};
 struct format f={2};struct amvdec_session s={&c,&f};
 struct amvdec_geometry g={34,16,64,32,2,4};struct amvdec_shadow sh={0x100000,2048,3072,&api};
 struct vb2_buffer b={.num_planes=2,.planes={{.size=1024,.vaddr=y,.dma=0x200000},
 {.size=512,.vaddr=uv,.dma=0x300000}}};
 memset(y,0xa5,sizeof(y));memset(uv,0xa5,sizeof(uv));
 assert(!amvdec_meson8b_copy_capture(&s,&g,&sh,&b)&&gpu_calls==1&&!cpu_calls&&c.ge2d_frames==1);
 assert(y[0]==0xa5&&y[33]==0xa5&&uv[33]==0xa5);
 conversion_error=-ETIMEDOUT;b.planes[0].dbuf=&api;b.planes[1].dbuf=&api;
 assert(!amvdec_meson8b_copy_capture(&s,&g,&sh,&b)&&gpu_calls==2&&cpu_calls==1&&begin_calls==4&&end_calls==4);
 f.pixfmt=1;assert(!amvdec_meson8b_copy_capture(&s,&g,&sh,&b)&&gpu_calls==2&&cpu_calls==2);
 begin_error=-EIO;assert(amvdec_meson8b_copy_capture(&s,&g,&sh,&b)==-EIO&&cpu_calls==2);
 b.planes[1].dbuf=&sh;begin_failure=&sh;int ends=end_calls;
 assert(amvdec_meson8b_copy_capture(&s,&g,&sh,&b)==-EIO&&cpu_calls==2&&end_calls==ends+1);
 c.ge2d_dev=NULL;assert(!amvdec_meson8b_copy_capture(&s,&g,&sh,&b)&&cpu_calls==3);
 assert(c.converted_frames==4&&c.ge2d_frames==1);
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
