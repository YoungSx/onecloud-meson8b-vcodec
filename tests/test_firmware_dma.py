"""Microcode DMA must see initialized memory and stop before timeout cleanup."""

import pathlib, subprocess, tempfile, unittest

ROOT = pathlib.Path(__file__).parents[1]


class FirmwareDMATests(unittest.TestCase):
    def test_ordering_and_timeout_cancel(self):
        for name in ["vdec_1", "vdec_hevc"]:
            with self.subTest(engine=name):
                s = (ROOT / ("vdec/" + name + ".c")).read_text()
                a = s.index(name + "_load_firmware(")
                a = s.rfind("static int", 0, a)
                b = s.index("\nstatic ", a + 12)
                code = (
                    r"""
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef uint32_t u32;typedef uintptr_t dma_addr_t;
#define MC_SIZE 16384
#define GFP_KERNEL 0
#define BIT(n) (1u<<(n))
#define dev_err(...) ((void)0)
#define dev_info(...) ((void)0)
enum {MPSR,CPSR,MDEC_PIC_DC_CTRL,IMEM_DMA_ADR,IMEM_DMA_COUNT,IMEM_DMA_CTRL,HEVC_MPSR,HEVC_CPSR,HEVC_IMEM_DMA_ADR,HEVC_IMEM_DMA_COUNT,HEVC_IMEM_DMA_CTRL};
struct device {int unused;};struct firmware {size_t size;const unsigned char *data;};
struct amvdec_session;struct amvdec_codec_ops {int (*load_extended_firmware)(struct amvdec_session *,const unsigned char *,unsigned int);};
struct format {struct amvdec_codec_ops *codec_ops;};struct amvdec_core {struct device *dev,*dev_dec;};struct amvdec_session {struct amvdec_core *core;struct format *fmt_out;};
static int timeout,active,barrier,cancels,reads;static unsigned char data[MC_SIZE];static const struct firmware firmware={sizeof(data),data};
static int request_firmware(const struct firmware **f,const char *name,struct device *d){(void)name;(void)d;*f=&firmware;return 0;}
static void release_firmware(const struct firmware *f){(void)f;}
static void *dma_alloc_coherent(struct device *d,size_t n,dma_addr_t *addr,int flags){(void)d;(void)flags;*addr=1;return calloc(1,n);}
static void dma_free_coherent(struct device *d,size_t n,void *p,dma_addr_t addr){(void)d;(void)n;(void)addr;assert(!active);if(timeout)assert(cancels==1 && reads==1);free(p);}
#define dma_wmb() (barrier=1)
static void amvdec_write_dos(struct amvdec_core *c,int reg,u32 val){(void)c;if(reg==IMEM_DMA_CTRL || reg==HEVC_IMEM_DMA_CTRL){if(val&0x8000){assert(barrier);active=1;}else{active=0;cancels++;}}}
static u32 amvdec_read_dos(struct amvdec_core *c,int reg){(void)c;(void)reg;reads++;return active?0x8000:0;}
#define amvdec_clear_dos_bits(c,r,v) ((void)(c),(void)(r),(void)(v))
#define read_poll_timeout(op,val,condition,delay,limit,sleep,c,r) ((val)=(timeout?0x8000:0),(void)(val),active=timeout,timeout?-ETIMEDOUT:0)
"""
                    + s[a:b]
                    + r"""
int main(void){struct amvdec_core core={0};struct amvdec_codec_ops ops={0};struct format fmt={&ops};struct amvdec_session session={&core,&fmt};assert(!LOAD(&session,"test"));timeout=1;active=barrier=cancels=reads=0;assert(LOAD(&session,"test")==-ETIMEDOUT);return 0;}
""".replace("LOAD", name + "_load_firmware")
                )
                with tempfile.TemporaryDirectory() as directory:
                    p = pathlib.Path(directory)
                    (p / "test.c").write_text(code)
                    subprocess.run(
                        [
                            "cc",
                            "-Wall",
                            "-Wextra",
                            "-Werror",
                            "-fsanitize=address,undefined",
                            "-fno-pie",
                            "-no-pie",
                            str(p / "test.c"),
                            "-o",
                            str(p / "test"),
                        ],
                        check=True,
                    )
                    subprocess.run([str(p / "test")], check=True)
