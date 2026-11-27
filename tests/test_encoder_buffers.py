"""Reference DMA allocations must cover the negotiated NV12 canvases."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class EncoderBufferTests(unittest.TestCase):
    def test_reference_geometry_and_partial_allocation_cleanup(self):
        source = (ROOT / 'venc/venc.c').read_text()
        body = source[source.index('static void venc_free_buffers('):
                      source.index('/*\n * FIXED_SLICE_CFG')]
        code = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;typedef uintptr_t dma_addr_t;
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define ALIGN(n,a) (((n)+(a)-1)&~((a)-1))
#define PAGE_ALIGN(n) ALIGN(n,4096)
#define GFP_KERNEL 0
#define DMA_ATTR_WRITE_COMBINE 1
#define V4L2_PIX_FMT_JPEG 1
#define dev_err(...) ((void)0)
enum {VENC_INPUT,VENC_REF0,VENC_REF1,VENC_WORKSPACE,VENC_STREAM,VENC_ANALYSIS};
struct buffer {void *vaddr;dma_addr_t paddr;u32 size;};
struct core {void *dev;};
struct venc_ctx {struct core *core;void *src;u32 coded_width,height,dst_fmt;struct buffer buffers[6];};
static unsigned allocated,freed,live,attempts,fail_at;
static u32 sizes[6];static void *addresses[6];static unsigned indices[6];
static u32 venc_staging_bytes(void *fmt,u32 width,u32 height){(void)fmt;return ALIGN(width,32)*ALIGN(height,16)*3/2;}
static void *dma_alloc_attrs(void *dev,u32 n,dma_addr_t *dma,int g,unsigned attrs){
 (void)dev;
 unsigned slot=0;
 while(slot<6&&addresses[slot])slot++;
 unsigned call=attempts++;if(fail_at==call+1)return NULL;
 assert(slot<6&&!g&&attrs==(slot==VENC_INPUT&&!allocated?DMA_ATTR_WRITE_COMBINE:0));
 sizes[slot]=n;void *p=malloc(n);assert(p);addresses[slot]=p;indices[slot]=slot;*dma=(uintptr_t)p;allocated++;live++;return p;
}
static void dma_free_attrs(void *dev,u32 n,void *p,dma_addr_t dma,unsigned attrs){
 (void)dev;unsigned i;for(i=0;i<6;i++)if(addresses[i]==p)break;
 assert(i<6&&n==sizes[i]&&dma==(uintptr_t)p&&attrs==(i==VENC_INPUT?DMA_ATTR_WRITE_COMBINE:0));
 addresses[i]=NULL;freed++;live--;free(p);
}
''' + body + r'''
int main(void){
 struct core core={0};u32 geometry[][2]={{32,32},{96,64},{320,240},{854,480},{1280,720},{1440,1080},{1920,1080},{1920,1088}};
 for(unsigned k=0;k<ARRAY_SIZE(geometry);k++){
  struct venc_ctx c={.core=&core,.coded_width=ALIGN(geometry[k][0],16),.height=geometry[k][1]};
  attempts=allocated=freed=0;assert(!venc_alloc_buffers(&c));
  u32 stride=ALIGN(c.coded_width,32),rows=ALIGN(c.height,16),bytes=stride*rows*3/2;
  assert(c.buffers[VENC_REF0].size==PAGE_ALIGN(bytes));
  assert(c.buffers[VENC_REF1].size==PAGE_ALIGN(bytes));
  /* Touch the final byte of each Y/UV canvas under ASan. */
  for(unsigned i=VENC_REF0;i<=VENC_REF1;i++){
   unsigned char *p=c.buffers[i].vaddr;p[stride*rows-1]=0x11;p[bytes-1]=0x22;
  }
  assert(c.buffers[VENC_WORKSPACE].size==0xc0000);
  assert(c.buffers[VENC_STREAM].size==0x100000&&c.buffers[VENC_ANALYSIS].size==0x110000);
  assert(allocated==6);
  venc_free_buffers(&c);assert(!live&&allocated==freed);
  for(unsigned i=0;i<6;i++)assert(!c.buffers[i].vaddr&&!c.buffers[i].paddr&&!c.buffers[i].size);

  /* JPEG encodes intra-only and skips reference/analysis CMA allocations. */
  c.dst_fmt=V4L2_PIX_FMT_JPEG;attempts=allocated=freed=0;
  assert(!venc_alloc_buffers(&c)&&allocated==3);
  assert(c.buffers[VENC_INPUT].vaddr&&!c.buffers[VENC_REF0].vaddr&&!c.buffers[VENC_REF1].vaddr);
  assert(c.buffers[VENC_WORKSPACE].vaddr&&c.buffers[VENC_STREAM].vaddr&&!c.buffers[VENC_ANALYSIS].vaddr);
  venc_free_buffers(&c);assert(!live&&allocated==freed);
  for(unsigned i=0;i<6;i++)assert(!c.buffers[i].vaddr&&!c.buffers[i].paddr&&!c.buffers[i].size);
 }
 for(fail_at=1;fail_at<=6;fail_at++){
  struct venc_ctx c={.core=&core,.coded_width=1280,.height=720};attempts=allocated=freed=0;
  assert(venc_alloc_buffers(&c)==-ENOMEM&&!live&&allocated==freed);
  for(unsigned i=0;i<6;i++)assert(!c.buffers[i].vaddr&&!c.buffers[i].paddr&&!c.buffers[i].size);
 }
 for(fail_at=1;fail_at<=3;fail_at++){
  struct venc_ctx c={.core=&core,.coded_width=1920,.height=1080,.dst_fmt=V4L2_PIX_FMT_JPEG};attempts=allocated=freed=0;
  assert(venc_alloc_buffers(&c)==-ENOMEM&&!live&&allocated==freed);
  for(unsigned i=0;i<6;i++)assert(!c.buffers[i].vaddr&&!c.buffers[i].paddr&&!c.buffers[i].size);
 }
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'test.c').write_text(code)
            subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            str(root / 'test.c'), '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)
