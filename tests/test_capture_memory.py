"""Cached MMAP and contiguous imports keep their allocator and sync lifetimes."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CaptureMemoryTests(unittest.TestCase):
    def test_allocator_dispatch_sync_export_and_failure_cleanup(self):
        source = (ROOT / 'vdec/meson8b-capture-mem.c').read_text()
        body = source[source.index('struct amvdec_capture_mem {'):]
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#define GFP_KERNEL 0
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
struct vb2_buffer {int unused;};struct device {int unused;};
struct dma_buf {int unused;};struct vm_area_struct {int unused;};
struct vb2_mem_ops {
 void *(*alloc)(struct vb2_buffer *,struct device *,unsigned long);
 void (*put)(void *);
 struct dma_buf *(*get_dmabuf)(struct vb2_buffer *,void *,unsigned long);
 void *(*attach_dmabuf)(struct vb2_buffer *,struct device *,struct dma_buf *,unsigned long);
 void (*detach_dmabuf)(void *);int (*map_dmabuf)(void *);void (*unmap_dmabuf)(void *);
 void (*prepare)(void *);void (*finish)(void *);
 void *(*vaddr)(struct vb2_buffer *,void *);void *(*cookie)(struct vb2_buffer *,void *);
 unsigned int (*num_users)(void *);int (*mmap)(void *,struct vm_area_struct *);
};
static bool wrapper_failure;static int backing_error,map_error;
static unsigned wrappers,allocs,puts,attaches,detaches,maps,unmaps,prepares,finishes,exports;
static int mmap_backing,dma_backing;static uintptr_t dma_address=0x400000;
static struct dma_buf exported;
static void *kmalloc(size_t n,int flags){assert(!flags);if(wrapper_failure)return NULL;wrappers++;return malloc(n);}
static void kfree(void *p){assert(p&&wrappers);wrappers--;free(p);}
static void *fake_alloc(struct vb2_buffer *vb,struct device *dev,unsigned long size){
 (void)vb;(void)dev;assert(size==3145728);allocs++;return backing_error?ERR_PTR(backing_error):&mmap_backing;
}
static void fake_put(void *p){assert(p==&mmap_backing);puts++;}
static void *fake_attach(struct vb2_buffer *vb,struct device *dev,struct dma_buf *dbuf,unsigned long size){
 (void)vb;(void)dev;assert(dbuf==&exported&&size==3145728);attaches++;return backing_error?ERR_PTR(backing_error):&dma_backing;
}
static void fake_detach(void *p){assert(p==&dma_backing);detaches++;}
static int fake_map(void *p){assert(p==&dma_backing);maps++;return map_error;}
static void fake_unmap(void *p){assert(p==&dma_backing);unmaps++;}
static void fake_prepare(void *p){assert(p==&dma_backing);prepares++;}
static void fake_finish(void *p){assert(p==&dma_backing);finishes++;}
static void *fake_vaddr(struct vb2_buffer *vb,void *p){(void)vb;assert(p==&mmap_backing||p==&dma_backing);return p;}
static void *fake_cookie(struct vb2_buffer *vb,void *p){(void)vb;assert(p==&dma_backing);return &dma_address;}
static unsigned fake_users(void *p){assert(p==&mmap_backing||p==&dma_backing);return p==&mmap_backing?3:1;}
static int fake_mmap(void *p,struct vm_area_struct *vma){assert(p==&mmap_backing&&vma);return 17;}
static struct dma_buf *fake_export(struct vb2_buffer *vb,void *p,unsigned long flags){
 (void)vb;assert(p==&mmap_backing&&flags==19);exports++;return &exported;
}
static const struct vb2_mem_ops vb2_vmalloc_memops={.alloc=fake_alloc,.put=fake_put,.vaddr=fake_vaddr,
 .mmap=fake_mmap,.num_users=fake_users,.get_dmabuf=fake_export};
static const struct vb2_mem_ops vb2_dma_contig_memops={.attach_dmabuf=fake_attach,.detach_dmabuf=fake_detach,
 .map_dmabuf=fake_map,.unmap_dmabuf=fake_unmap,.prepare=fake_prepare,.finish=fake_finish,
 .vaddr=fake_vaddr,.cookie=fake_cookie,.num_users=fake_users};
''' + body + r'''
int main(void){
 const struct vb2_mem_ops *ops=&amvdec_capture_memops;
 struct vb2_buffer vb={0};struct device dev={0};struct vm_area_struct vma={0};
 void *mmap=ops->alloc(&vb,&dev,3145728);assert(!IS_ERR(mmap)&&wrappers==1&&allocs==1&&!attaches);
 assert(ops->vaddr(&vb,mmap)==&mmap_backing&&!ops->cookie(&vb,mmap));
 assert(ops->num_users(mmap)==3&&ops->mmap(mmap,&vma)==17);
 assert(ops->get_dmabuf(&vb,mmap,19)==&exported&&exports==1);
 ops->prepare(mmap);ops->finish(mmap);assert(!prepares&&!finishes);
 void *dma=ops->attach_dmabuf(&vb,&dev,&exported,3145728);assert(!IS_ERR(dma)&&wrappers==2&&attaches==1);
 map_error=-EIO;assert(ops->map_dmabuf(dma)==-EIO);map_error=0;assert(!ops->map_dmabuf(dma)&&maps==2);
 assert(ops->vaddr(&vb,dma)==&dma_backing&&ops->cookie(&vb,dma)==&dma_address&&ops->num_users(dma)==1);
 ops->prepare(dma);ops->finish(dma);assert(prepares==1&&finishes==1);
 ops->unmap_dmabuf(dma);ops->detach_dmabuf(dma);assert(unmaps==1&&detaches==1&&wrappers==1);
 ops->put(mmap);assert(puts==1&&!wrappers);
 wrapper_failure=true;unsigned a=allocs,d=attaches;
 assert(PTR_ERR(ops->alloc(&vb,&dev,3145728))==-ENOMEM);
 assert(PTR_ERR(ops->attach_dmabuf(&vb,&dev,&exported,3145728))==-ENOMEM);
 assert(allocs==a&&attaches==d&&!wrappers);
 wrapper_failure=false;backing_error=-ENOSPC;
 assert(PTR_ERR(ops->alloc(&vb,&dev,3145728))==-ENOSPC&&!wrappers);
 assert(PTR_ERR(ops->attach_dmabuf(&vb,&dev,&exported,3145728))==-ENOSPC&&!wrappers);
 assert(puts==1&&detaches==1);return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'test.c').write_text(code)
            subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            str(root / 'test.c'), '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)
