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
        body = source[source.index("static int ge2d_copy("):source.index("static int ge2d_selftest(")]
        code = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef uint32_t u32;
typedef uint64_t dma_addr_t;
#define BIT(n) (1u << (n))
#define GENMASK(h,l) ((~0u << (l)) & (~0u >> (31-(h))))
#define U32_MAX UINT32_MAX
#define MESON_CANVAS_WRAP_NONE 0
#define MESON_CANVAS_BLKMODE_LINEAR 0
#include "ge2d-layout.h"
''' + macros + r'''
struct meson8b_ge2d {void *dev,*canvas,*reset;unsigned char *regs;unsigned char src_canvas,dst_canvas;bool offline;};
static u32 registers[128];
static unsigned resets, starts, canvas_calls, barriers;
static int reset_error, canvas_error, poll_error;
static unsigned asserted;
static int reset_control_assert(void *r) {(void)r;asserted++;return 0;}
static int ge2d_reset(struct meson8b_ge2d *c) {(void)c;resets++;return reset_error;}
static void ge2d_write(struct meson8b_ge2d *c,u32 r,u32 v) {
 (void)c;registers[r/4]=v;if(r==CMD_CTRL){assert(barriers);starts++;}
}
static int meson_canvas_config(void *c,unsigned i,dma_addr_t a,u32 s,u32 h,int w,int b,int e) {
 (void)c;(void)i;(void)a;assert(s==512 && h==32 && !w && !b && !e);canvas_calls++;return canvas_error;
}
static u32 readl(void *r) {(void)r;return 0;}
#define readl_poll_timeout(r,status,cond,interval,limit) ((void)(r), (status)=0, (void)(cond), assert((interval)==50 && (limit)==100000),poll_error)
#define dev_err(...) ((void)0)
static void dma_wmb(void) {barriers++;}
static void dma_rmb(void) {barriers++;}
''' + body + r'''
int main(void) {
 struct meson8b_ge2d c={.regs=(unsigned char *)registers,.src_canvas=3,.dst_canvas=4};
 assert(!ge2d_copy(&c,0x100000,0x200000,512,32,false));
 assert(registers[GEN_CTRL2/4]==BIT(23));
 assert(registers[SRC_CANVAS/4]==3u<<24 && registers[DST_CANVAS/4]==4);
 assert(registers[SRC_X/4]==511 && registers[DST_Y/4]==31);
 assert(!registers[MATRIX_CTRL/4] && !registers[ANTIFLICK/4]);
 assert(starts==1 && resets==1 && canvas_calls==2);
 assert(!ge2d_copy(&c,0x100000,0x200000,512,32,true));
 assert(registers[GEN_CTRL2/4]==(BIT(23)|BIT(7)));
 unsigned before=starts;
 assert(ge2d_copy(&c,UINT32_MAX-16,0x200000,512,32,true)==-EINVAL);
 assert(ge2d_copy(&c,1ull<<32,0x200000,512,32,true)==-EINVAL);
 assert(ge2d_copy(&c,0x100001,0x200000,512,32,true)==-EINVAL);
 assert(starts==before);
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
