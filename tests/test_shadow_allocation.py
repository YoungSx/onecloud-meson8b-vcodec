"""GE2D slots need no cache alias; NEON and capture_stage keep a read alias."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ShadowAllocationTests(unittest.TestCase):
    def test_coherent_ge2d_and_cached_neon_cleanup(self):
        source = (ROOT / 'vdec/meson8b-capture.c').read_text()
        body = source[source.index('static void amvdec_shadow_free('):
                      source.index('typedef void (*amvdec_swap64_t)')]
        code = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;typedef uintptr_t dma_addr_t;
#define PAGE_SIZE 4096
#define PAGE_SHIFT 12
#define PAGE_ALIGN(n) (((n)+PAGE_SIZE-1)&~(PAGE_SIZE-1))
#define GFP_KERNEL 0
#define VM_MAP 1
#define PAGE_KERNEL 2
#define dev_err(...) ((void)0)
struct page {int index;};
struct device {int unused;};
struct amvdec_core {struct device *dev;void *ge2d_dev;u32 pool_slots,pool_bytes;};

struct amvdec_shadow {void *vaddr,*cached;dma_addr_t paddr;u32 size,luma,width,height;};
struct amvdec_frame_pool {struct amvdec_shadow slot[2],capture_stage;int state;};
struct amvdec_session {struct amvdec_core *core;struct amvdec_frame_pool pool;};
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
static void meson8b_pool_reset(int *state,int n){*state=n;}
static unsigned allocations,frees,arrays,maps,unmaps,pages_seen;
static int fail_at;
static bool highmem;
static unsigned char canvas[3145728],cached_buf[3145728];
static struct page pages[768];
static void *dma_alloc_coherent(struct device *d,u32 n,dma_addr_t *a,int g){
 (void)d;assert(n==sizeof(canvas)&&!g);allocations++;if(fail_at==1)return NULL;*a=0x30000000;return canvas;
}
static void dma_free_coherent(struct device *d,u32 n,void *v,dma_addr_t a){
 (void)d;assert(n==sizeof(canvas)&&v==canvas&&a==0x30000000);frees++;
}
static void *kmalloc_array(unsigned n,unsigned size,int g){
 assert(n==768&&size==sizeof(struct page *)&&!g);arrays++;return fail_at==2?NULL:calloc(n,size);
}
static void kfree(void *p){free(p);}
static bool is_vmalloc_addr(void *p){assert(p>= (void *)canvas&&p<(void *)(canvas+sizeof(canvas)));return highmem;}
static struct page *vmalloc_to_page(void *p){assert(highmem);pages_seen++;return &pages[((unsigned char *)p-canvas)/PAGE_SIZE];}
static struct page *virt_to_page(void *p){assert(!highmem);pages_seen++;return &pages[((unsigned char *)p-canvas)/PAGE_SIZE];}
static void *vmap(struct page **p,unsigned n,int flags,int prot){
 assert(n==768&&flags==VM_MAP&&prot==PAGE_KERNEL);for(unsigned i=0;i<n;i++)assert(p[i]==&pages[i]);maps++;return fail_at==3?NULL:cached_buf;
}
static void vunmap(void *p){assert(p==cached_buf);unmaps++;}
''' + body + source[source.index('static void amvdec_pool_free_slots('):source.index('static void amvdec_pool_work(')] + r'''
int main(void){
 struct device d={0};struct amvdec_core c={.dev=&d};struct amvdec_session s={.core=&c};
 for(unsigned cached=0;cached<2;cached++)for(unsigned high=0;high<2;high++){
  c.ge2d_dev=cached?NULL:&c;highmem=high;fail_at=0;struct amvdec_shadow sh={0};
  unsigned a=arrays,m=maps,u=unmaps,f=frees,p=pages_seen;
  assert(!amvdec_shadow_alloc(&s,&sh,2097152,cached));
  assert(sh.vaddr==canvas&&sh.paddr==0x30000000&&sh.luma==2097152&&sh.size==sizeof(canvas));
  assert(sh.cached==(cached?cached_buf:NULL));
  assert(arrays==a+cached&&maps==m+cached&&pages_seen==p+(cached?768:0));
  amvdec_shadow_free(&s,&sh);assert(frees==f+1&&unmaps==u+cached&&!sh.vaddr&&!sh.cached&&!sh.size);
 }
 c.ge2d_dev=NULL;
 for(fail_at=1;fail_at<=3;fail_at++){
  struct amvdec_shadow sh={0};unsigned f=frees,u=unmaps;
  assert(amvdec_shadow_alloc(&s,&sh,2097152,true)==-ENOMEM);
  assert(!sh.vaddr&&!sh.cached&&frees==f+(fail_at!=1)&&unmaps==u);
 }
 assert(allocations==7);
 c.ge2d_dev=&c;fail_at=0;
 assert(!amvdec_shadow_alloc(&s,&s.pool.slot[0],2097152,false));
 assert(!s.pool.slot[0].cached);
 assert(!amvdec_shadow_alloc(&s,&s.pool.capture_stage,2097152,true));
 assert(s.pool.capture_stage.cached==cached_buf);
 c.pool_slots=1;c.pool_bytes=sizeof(canvas)*2;s.pool.state=1;unsigned before=frees;
 amvdec_pool_free_slots(&s);
 assert(frees==before+2&&!c.pool_bytes&&!c.pool_slots&&!s.pool.state);
 assert(!s.pool.slot[0].vaddr&&!s.pool.capture_stage.vaddr);return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'test.c').write_text(code)
            subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            str(root / 'test.c'), '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)
