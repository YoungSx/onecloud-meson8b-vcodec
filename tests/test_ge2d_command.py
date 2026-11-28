"""Exercise the actual GE2D command body at a bounded MMIO/DMA boundary."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Ge2dCommandTests(unittest.TestCase):
    def test_canvas_endian_command_and_fault_cleanup(self):
        source = (ROOT / "ge2d/meson8b-ge2d.c").read_text()
        macros = source[source.index("#define GEN_CTRL0"):source.index("struct ge2d_ctx;")]
        body = source[source.index("struct ge2d_hw_fmt {"):source.index("static int ge2d_selftest(")]
        code = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t dma_addr_t;
typedef uint64_t u64;
#define BIT(n) (1u << (n))
#define GENMASK(h,l) ((~0u << (l)) & (~0u >> (31-(h))))
#define U32_MAX UINT32_MAX
#define ALIGN(n,a) (((n)+(a)-1)&~((a)-1))
#define PAGE_ALIGN(n) ALIGN((n), 4096)
#define GFP_KERNEL 0
#define MESON_CANVAS_WRAP_NONE 0
#define MESON_CANVAS_BLKMODE_LINEAR 0
#include "ge2d-layout.h"
#include "ge2d-scale.h"
''' + macros + r'''
struct meson8b_ge2d {void *dev,*canvas,*reset;unsigned char *regs;unsigned char src_canvas,dst_canvas,src_uv_canvas,dst_uv_canvas,src_v_canvas,src2_canvas;void *blend_stage_vaddr;dma_addr_t blend_stage_dma;size_t blend_stage_size;bool offline;};
static unsigned char blend_stage_buf[4096];
static void *dma_alloc_coherent(void *dev,size_t sz,dma_addr_t *dma,int gfp) {(void)dev;(void)sz;(void)gfp;*dma=0x400000;return blend_stage_buf;}
static void dma_free_coherent(void *dev,size_t sz,void *v,dma_addr_t dma) {(void)dev;(void)sz;(void)v;(void)dma;}
static u32 registers[128];
static unsigned resets, starts, canvas_calls, barriers;
static int reset_error, canvas_error, poll_error;
static unsigned poll_error_at;
static unsigned asserted;
static u32 status_samples[16];
static unsigned status_count, status_reads;
static int reset_control_assert(void *r) {(void)r;asserted++;return 0;}
static int ge2d_reset(struct meson8b_ge2d *c) {(void)c;resets++;return reset_error;}
static void ge2d_write(struct meson8b_ge2d *c,u32 r,u32 v) {
 (void)c;registers[r/4]=v;if(r==CMD_CTRL){assert(barriers);starts++;}
}
static int meson_canvas_config(void *c,unsigned i,dma_addr_t a,u32 s,u32 h,int w,int b,int e) {
 (void)c;(void)i;(void)a;assert((s>=32 && s<=8192 && !(s&7)) && (h==32||h==16||h==8||h==512||h==256) && !w && !b && !e);canvas_calls++;return canvas_error;
}
static u32 readl(void *r) {(void)r;return 0;}
#define readl_poll_timeout(r,status,cond,interval,limit) ({ \
	(void)(r);assert((interval)==50 && (limit)==100000); \
 int result=-ETIMEDOUT; \
 if(poll_error_at && starts==poll_error_at) result=-ETIMEDOUT; \
 else if(poll_error) result=poll_error; \
 else for(unsigned attempt=0;attempt<16;attempt++){ \
  (status)=status_count?status_samples[status_reads<status_count?status_reads:status_count-1]:0; \
  status_reads++;if(cond){result=0;break;} \
 } \
 result; \
})
#define dev_err(...) ((void)0)
static void dma_wmb(void) {barriers++;}
static void dma_rmb(void) {barriers++;}
''' + body + r'''
int main(void) {
 struct meson8b_ge2d c={.regs=(unsigned char *)registers,.src_canvas=3,.dst_canvas=4,.src_uv_canvas=5,.dst_uv_canvas=6,.src_v_canvas=7,.src2_canvas=8};
 assert(!ge2d_copy(&c,0x100000,0x200000,512,32,false));
 assert(registers[GEN_CTRL2/4]==BIT(23));
 assert(registers[SRC_CANVAS/4]==3u<<24 && registers[DST_CANVAS/4]==4);
 assert(registers[SRC_X/4]==511 && registers[DST_Y/4]==31);
 assert(!registers[MATRIX_CTRL/4] && !registers[ANTIFLICK/4]);
 assert(starts==1 && resets==1 && canvas_calls==2);
 assert(!ge2d_copy(&c,0x100000,0x200000,512,32,true));
 assert(registers[GEN_CTRL2/4]==(BIT(23)|BIT(7)));
 struct meson8b_ge2d_image input={.width=512,.height=32,.stride=512,.address={0x100000,0x104000},.little_endian=true};
 struct meson8b_ge2d_image output={.width=256,.height=16,.stride=256,.address={0x200000,0x201000},.little_endian=true};
 struct meson8b_ge2d_crop crop={.width=512,.height=32};
 assert(!ge2d_blit(&c,&input,&output,&crop));
 assert(registers[HSC_STEP/4]==2u<<24 && registers[VSC_STEP/4]==2u<<24);
 assert(registers[HSC_INI/4]==BIT(29) && registers[VSC_INI/4]==BIT(29));
 assert(registers[DST_X/4]==255 && registers[DST_Y/4]==15);
 assert(registers[SC_MISC/4]&(BIT(11)|BIT(12)));
 assert(registers[SC_MISC/4]&(BIT(28)));
 assert(((registers[SC_MISC/4]>>15)&0x1fff)==62);
 assert(registers[HSC_ADV/4]==124u<<24);
 input.nv12=output.nv12=true;input.little_endian=false;
 unsigned calls=canvas_calls;
 assert(!ge2d_blit(&c,&input,&output,&crop));
 assert(canvas_calls==calls+4);
 assert(registers[GEN_CTRL0/4]==(BIT(0)|BIT(10)|BIT(11)));
 assert(registers[GEN_CTRL2/4]==(BIT(23)|(15u<<19)|(14u<<3)|2));
 assert(registers[GEN_CTRL3/4]==(BIT(0)|(10u<<19)|BIT(16)|(15u<<10)|BIT(8)));
 assert(registers[SRC_CANVAS/4]==(3u<<24|5u<<16|5u<<8));
 assert(registers[DST_CANVAS/4]==(4u|6u<<16));
 assert(registers[SRC_FMT/4]==(BIT(18)|BIT(16)|(8u<<8)|0x4c));
 crop=(struct meson8b_ge2d_crop){.left=2,.top=2,.width=256,.height=16};
 assert(!ge2d_blit(&c,&input,&output,&crop));
 assert(registers[SRC_CLIPX/4]==(2u<<16|257));
 assert(registers[SRC_X/4]==(2u<<16|257));
 assert(registers[GEN_CTRL0/4]==0);
 assert(registers[GEN_CTRL2/4]==BIT(23));
 assert(!registers[HSC_ADV/4] && !(registers[SC_MISC/4]&BIT(28)));
 assert(!registers[HSC_INI/4] && !registers[VSC_INI/4]);
 assert(registers[SRC_CLIPY/4]==(1u<<16|8));
 assert(canvas_calls==calls+8 && starts==6);
 input.little_endian=true;crop.width=512;crop.height=32;crop.left=crop.top=0;
 output.width=512;output.height=32;output.stride=512;output.address[1]=0x204000;
 assert(!ge2d_blit(&c,&input,&output,&crop));
 assert(registers[GEN_CTRL2/4]==(BIT(23)|BIT(7)));
 crop=(struct meson8b_ge2d_crop){.left=2,.top=2,.width=256,.height=16};
 output.width=256;output.height=16;output.stride=256;output.address[1]=0x201000;
 crop.left=1;unsigned no_start=starts;
 assert(ge2d_blit(&c,&input,&output,&crop)==-EINVAL&&starts==no_start);
 crop.left=2;input.address[1]=UINT32_MAX-7;
 assert(ge2d_blit(&c,&input,&output,&crop)==-EINVAL&&starts==no_start);
 input.address[1]=0x104000;output.address[1]=input.address[0];
 assert(ge2d_blit(&c,&input,&output,&crop)==-EINVAL&&starts==no_start);
 unsigned before=starts;
 assert(ge2d_copy(&c,UINT32_MAX-16,0x200000,512,32,true)==-EINVAL);
 assert(ge2d_copy(&c,1ull<<32,0x200000,512,32,true)==-EINVAL);
 assert(ge2d_copy(&c,0x100001,0x200000,512,32,true)==-EINVAL);
 assert(starts==before);
 /* Only the axis being filtered repeats its first sample. */
 input.nv12=output.nv12=false;crop=(struct meson8b_ge2d_crop){.width=512,.height=32};
 output.width=256;output.height=32;
 assert(!ge2d_blit(&c,&input,&output,&crop));
 assert(registers[HSC_INI/4]==BIT(29) && !registers[VSC_INI/4]);
 output.width=512;output.height=16;output.stride=512;
 assert(!ge2d_blit(&c,&input,&output,&crop));
 assert(!registers[HSC_INI/4] && registers[VSC_INI/4]==BIT(29));
 input.nv12=output.nv12=true;crop=(struct meson8b_ge2d_crop){.left=2,.top=2,.width=256,.height=16};
 output.width=256;output.stride=256;output.address[1]=0x201000;
 /* Queued commands can precede BUSY; write responses can outlive it. */
 u32 pending[]={BIT(2),BIT(4),BIT(1),BIT(0),BIT(17),GENMASK(28,17),BIT(3)|BIT(5)|BIT(6)};
 for(unsigned i=0;i<sizeof(pending)/sizeof(pending[0]);i++)status_samples[i]=pending[i];
 status_count=sizeof(pending)/sizeof(pending[0]);status_reads=0;
 assert(!ge2d_copy(&c,0x100000,0x200000,512,32,true));
 assert(status_reads==status_count&&!c.offline&&!asserted);
 status_count=status_reads=0;before=starts;
 canvas_error=-ENOMEM;
 assert(ge2d_copy(&c,0x100000,0x200000,512,32,true)==-ENOMEM && starts==before);
 canvas_error=0;reset_error=-EIO;
 assert(ge2d_copy(&c,0x100000,0x200000,512,32,true)==-EIO && starts==before);
 reset_error=0;poll_error=-ETIMEDOUT;before=resets;
 assert(ge2d_copy(&c,0x100000,0x200000,512,32,true)==-ETIMEDOUT);
 assert(resets==before+1 && asserted==1 && c.offline);
 before=starts;
 assert(ge2d_copy(&c,0x100000,0x200000,512,32,true)==-EIO && starts==before);
 assert(BUSY_MASK & GENMASK(28,17));
 /* Chroma failure stops the split copy and holds the engine in reset. */
 c.offline=false;poll_error=0;poll_error_at=starts+2;before=starts;
 input.little_endian=true;output.address[1]=0x201000;
 assert(ge2d_blit(&c,&input,&output,&crop)==-ETIMEDOUT);
 assert(starts==before+2&&c.offline&&asserted==2);
 before=starts;assert(ge2d_blit(&c,&input,&output,&crop)==-EIO&&starts==before);
 /* A stuck buffered/pipeline command must take the same reset path. */
 for(unsigned bit=2;bit<=4;bit+=2){
  c.offline=false;poll_error_at=0;status_count=1;status_reads=0;status_samples[0]=BIT(bit);
  unsigned resets_before=asserted;
  assert(ge2d_copy(&c,0x100000,0x200000,512,32,true)==-ETIMEDOUT);
  assert(c.offline&&asserted==resets_before+1&&status_reads==16);
  before=starts;
  assert(ge2d_copy(&c,0x100000,0x200000,512,32,true)==-EIO&&starts==before);
 }
 /* NV21 chroma swap, 3-plane YUV420/YVU420, RGB matrix and flip/rotation. */
 c.offline=false;poll_error=0;poll_error_at=0;status_count=status_reads=0;canvas_error=0;reset_error=0;
 input=(struct meson8b_ge2d_image){.width=512,.height=32,.stride=512,.address={0x100000,0x104000},.little_endian=true,.format=MESON8B_GE2D_FMT_NV12};
 output=(struct meson8b_ge2d_image){.width=512,.height=32,.stride=512,.address={0x200000,0x204000},.little_endian=true,.format=MESON8B_GE2D_FMT_NV21};
 crop=(struct meson8b_ge2d_crop){.width=512,.height=32};
 unsigned starts_before=starts;
 assert(!ge2d_blit(&c,&input,&output,&crop));
 assert(starts==starts_before+1);
 assert(((registers[GEN_CTRL2/4]>>19)&0xf)==14&&((registers[GEN_CTRL3/4]>>19)&0xf)==9);
 /* YUV420 and YVU420 route Cb/Cr to src_uv_canvas vs src_v_canvas. */
 input=(struct meson8b_ge2d_image){.width=512,.height=32,.stride=512,.address={0x100000,0x104000,0x105000},.little_endian=true,.format=MESON8B_GE2D_FMT_YVU420};
 output=(struct meson8b_ge2d_image){.width=512,.height=32,.stride=512,.address={0x200000,0x204000},.little_endian=true,.format=MESON8B_GE2D_FMT_NV12};
 assert(!ge2d_blit(&c,&input,&output,&crop));
 assert(registers[SRC_CANVAS/4]==((3u<<24)|(7u<<16)|(5u<<8)));
 input.format=MESON8B_GE2D_FMT_YUV420;
 assert(!ge2d_blit(&c,&input,&output,&crop));
 assert(registers[SRC_CANVAS/4]==((3u<<24)|(5u<<16)|(7u<<8)));
 /* Planar 3-plane output is rejected cleanly without hardware start. */
 output=input;output.address[0]=0x200000;output.address[1]=0x204000;output.address[2]=0x205000;
 starts_before=starts;
 assert(ge2d_blit(&c,&input,&output,&crop)==-EINVAL&&starts==starts_before);
 /* RGB24 <-> NV12 enables BT.709 matrix and clears matrix for same-family copy. */
 input=(struct meson8b_ge2d_image){.width=512,.height=32,.stride=1536,.address={0x100000},.little_endian=true,.format=MESON8B_GE2D_FMT_RGB24};
 output=(struct meson8b_ge2d_image){.width=512,.height=32,.stride=512,.address={0x200000,0x204000},.little_endian=true,.format=MESON8B_GE2D_FMT_NV12,.colorspace=MESON8B_GE2D_CS_BT709_LIM};
 assert(!ge2d_blit(&c,&input,&output,&crop));
 assert((registers[MATRIX_CTRL/4]&BIT(0))&&((registers[MATRIX_COEF00_01/4]>>16)==0x0bb));
 /* 90-degree rotation swaps scaled axes, sets dst_xy_swap and chroma phase bits. */
 input=(struct meson8b_ge2d_image){.width=512,.height=32,.stride=512,.address={0x100000,0x104000},.little_endian=true,.format=MESON8B_GE2D_FMT_NV12};
 output=(struct meson8b_ge2d_image){.width=32,.height=512,.stride=32,.address={0x200000,0x204000},.little_endian=true,.format=MESON8B_GE2D_FMT_NV12};
 crop=(struct meson8b_ge2d_crop){.width=512,.height=32,.rotate=90,.vflip=true};
 assert(!ge2d_blit(&c,&input,&output,&crop));
 assert((registers[CMD_CTRL/4]&(BIT(7)|BIT(6)|BIT(5)|BIT(0)))==(BIT(7)|BIT(6)|BIT(5)|BIT(0)));
 assert(((registers[SRC_X/4]>>30)&3)==3&&((registers[SRC_Y/4]>>30)&3)==3);
 /* ABGR32 constant-alpha blend and ARGB32 dual-source SRC_OVER onto XRGB32. */
 input=(struct meson8b_ge2d_image){.width=512,.height=32,.stride=2048,.address={0x100000},.little_endian=true,.format=MESON8B_GE2D_FMT_ABGR32};
 output=(struct meson8b_ge2d_image){.width=512,.height=32,.stride=2048,.address={0x200000},.little_endian=true,.format=MESON8B_GE2D_FMT_XRGB32};
 crop=(struct meson8b_ge2d_crop){.width=512,.height=32,.blend=MESON8B_GE2D_BLEND_CONST,.global_alpha=128,.bg_color=0x112233};
 assert(!ge2d_blit(&c,&input,&output,&crop));
 assert(registers[SRC2_DEF_COLOR/4]==0x112233ffu);
 assert(registers[ALU_CONST_COLOR/4]==0x11223380u);
 assert(registers[ALU_CTRL/4]==((12u<<16)|(13u<<12)|(6u<<4)|7u));
 assert((registers[CMD_CTRL/4]&BIT(9))&&((registers[GEN_CTRL1/4]&0xffu)==128));
 input.format=MESON8B_GE2D_FMT_ARGB32;
 crop=(struct meson8b_ge2d_crop){.width=512,.height=32,.blend=MESON8B_GE2D_BLEND_OVER,.global_alpha=255};
 assert(!ge2d_blit(&c,&input,&output,&crop));
 assert(registers[ALU_CTRL/4]==((6u<<16)|(7u<<12)|(1u<<4)|3u));
 assert(!(registers[CMD_CTRL/4]&BIT(9))&&(registers[DST_CANVAS/4]==(4u|(8u<<8))));
 assert(!(registers[CMD_CTRL/4]&(BIT(4)|BIT(3))));
 /* Blend-over with flip/rotation reverses SRC2 alongside SRC and keeps SRC_Y top..bottom. */
 crop=(struct meson8b_ge2d_crop){.width=512,.height=32,.rotate=180,.hflip=true,.blend=MESON8B_GE2D_BLEND_OVER,.global_alpha=200};
 assert(!ge2d_blit(&c,&input,&output,&crop));
 assert((registers[CMD_CTRL/4]&(BIT(7)|BIT(6)|BIT(5)|BIT(4)|BIT(3)|BIT(0)))==(BIT(5)|BIT(3)|BIT(0)));
 assert((registers[SRC_Y/4]&0xffffu)==31u);
 /* 90/270 blend-over runs two passes so SRC2 reads rotated coordinates cleanly. */
 output=(struct meson8b_ge2d_image){.width=32,.height=512,.stride=128,.address={0x200000},.little_endian=true,.format=MESON8B_GE2D_FMT_XRGB32};
 starts_before=starts;
 crop=(struct meson8b_ge2d_crop){.width=512,.height=32,.rotate=270,.vflip=true,.blend=MESON8B_GE2D_BLEND_OVER,.global_alpha=128};
 assert(!ge2d_blit(&c,&input,&output,&crop));
 assert(starts==starts_before+2&&(registers[CMD_CTRL/4]&(BIT(7)|BIT(6)|BIT(5)|BIT(4)|BIT(3)|BIT(0)))==BIT(0));
 /* Blending into planar/semiplanar YUV or invalid blend mode fails cleanly. */
 output=(struct meson8b_ge2d_image){.width=512,.height=32,.stride=512,.address={0x200000,0x204000},.little_endian=true,.format=MESON8B_GE2D_FMT_NV12};
 starts_before=starts;
 assert(ge2d_blit(&c,&input,&output,&crop)==-EINVAL&&starts==starts_before);
 crop.blend=MESON8B_GE2D_BLEND_OVER+1;
 output=(struct meson8b_ge2d_image){.width=512,.height=32,.stride=2048,.address={0x200000},.little_endian=true,.format=MESON8B_GE2D_FMT_XRGB32};
 assert(ge2d_blit(&c,&input,&output,&crop)==-EINVAL&&starts==starts_before);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "test.c").write_text(code)
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                            "-fno-pie", "-no-pie", "-I", str(ROOT / "ge2d"),
                            str(root / "test.c"), "-o", str(root / "test")], check=True)
            subprocess.run([str(root / "test")], check=True)
