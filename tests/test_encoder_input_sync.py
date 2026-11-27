"""Exercise the real imported-plane fence, cache and CPU access helpers."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class EncoderInputSyncTests(unittest.TestCase):
    def test_producer_wait_requeue_cache_and_cpu_error_pairing(self):
        source = (ROOT / 'venc/input.c').read_text()
        body = source[source.index('/* A pinned mapping does not wait'):]
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <stddef.h>
#define V4L2_MEMORY_DMABUF 4
#define DMA_TO_DEVICE 1
#define DMA_FROM_DEVICE 2
typedef uintptr_t dma_addr_t;
struct dma_resv {long result;};struct device {int unused;};
struct dma_buf {struct dma_resv *resv;};
struct dma_buf_attachment {struct dma_buf *dmabuf;struct device *dev;};
struct venc_import {struct dma_buf_attachment *attach;void *sgt;bool contiguous;dma_addr_t dma_addr;};
struct vb2_buffer {unsigned memory,num_planes;struct {void *mem_priv;struct dma_buf *dbuf;void *vaddr;} planes[3];};
static unsigned waits,syncs,begins,ends,vmaps;static int begin_error,end_error;static bool cpu_access;
static unsigned msecs_to_jiffies(unsigned ms){assert(ms==1000);return 100;}
static long dma_resv_wait_timeout(struct dma_resv *r,bool all,bool intr,unsigned timeout){
 assert(!all&&intr&&timeout==100&&!cpu_access);waits++;return r->result;
}
static void dma_sync_sgtable_for_device(struct device *dev,void *sgt,int dir){
 assert(dev&&sgt&&dir==DMA_TO_DEVICE&&!cpu_access);syncs++;
}
static int dma_buf_begin_cpu_access(struct dma_buf *d,int dir){
 assert(d&&dir==DMA_FROM_DEVICE&&!cpu_access);begins++;if(begin_error)return begin_error;cpu_access=true;return 0;
}
static int dma_buf_end_cpu_access(struct dma_buf *d,int dir){
 assert(d&&dir==DMA_FROM_DEVICE&&cpu_access);ends++;cpu_access=false;return end_error;
}
static void *vb2_plane_vaddr(struct vb2_buffer *b,unsigned plane){
 assert(plane<b->num_planes&&(b->memory!=V4L2_MEMORY_DMABUF||cpu_access));vmaps++;return b->planes[plane].vaddr;
}
''' + body + r'''
int main(void){
 struct dma_resv resv[3]={{1},{1},{1}};struct dma_buf dbuf[3]={{&resv[0]},{&resv[1]},{&resv[2]}};
 struct device dev={0};struct dma_buf_attachment attach={&dbuf[0],&dev};
 struct venc_import import={.attach=&attach,.sgt=&attach,.contiguous=true,.dma_addr=0x100000};
 int data=17;struct vb2_buffer b={.memory=V4L2_MEMORY_DMABUF,.num_planes=3,
  .planes={{&import,&dbuf[0],&data},{&import,&dbuf[1],&data},{&import,&dbuf[2],&data}}};
 assert(!venc_input_wait(&b)&&waits==3);
 /* Each queued read checks the producer fence and refreshes its DMA view. */
 for(unsigned frame=0;frame<4;frame++){
  assert(!venc_input_wait(&b));assert(venc_input_dma(&b,0)==0x100000);
 }
 assert(syncs==4&&waits==15);
 resv[0].result=0;unsigned before=waits;
 assert(venc_input_wait(&b)==-ETIMEDOUT&&waits==before+1);
 resv[0].result=1;resv[1].result=-EINTR;before=waits;
 assert(venc_input_wait(&b)==-EINTR&&waits==before+2);
 resv[1].result=1;import.contiguous=false;
 assert(!venc_input_dma(&b,0)&&syncs==4);import.contiguous=true;
 import.sgt=NULL;assert(!venc_input_dma(&b,0)&&syncs==4);import.sgt=&attach;
 assert(venc_input_begin_cpu(&b,0)==&data&&begins==1&&vmaps==1&&cpu_access);
 end_error=-EIO;assert(venc_input_end_cpu(&b,0)==-EIO&&ends==1&&!cpu_access);
 begin_error=-EINTR;before=vmaps;
 assert(!venc_input_begin_cpu(&b,0)&&vmaps==before&&ends==1&&!cpu_access);
 begin_error=0;end_error=0;b.planes[0].vaddr=NULL;
 assert(!venc_input_begin_cpu(&b,0)&&ends==2&&!cpu_access);
 b.planes[0].vaddr=&data;
 assert(venc_input_begin_cpu(&b,0)==&data&&!venc_input_end_cpu(&b,0)&&ends==3&&!cpu_access);
 b.memory=1;unsigned old_waits=waits,old_begins=begins,old_ends=ends;
 assert(!venc_input_wait(&b)&&waits==old_waits);
 assert(!venc_input_dma(&b,0)&&syncs==4);
 assert(venc_input_begin_cpu(&b,0)==&data&&!venc_input_end_cpu(&b,0));
 assert(begins==old_begins&&ends==old_ends&&!cpu_access);
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

    def test_dmabuf_map_contiguity_and_32bit_dma_bounds(self):
        source = (ROOT / 'venc/input.c').read_text()
        body = source[source.index('static int venc_in_map('):
                      source.index('\nstatic void venc_in_unmap(')]
        code = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef uint32_t u32;typedef uint64_t u64;typedef uint64_t dma_addr_t;
#define U32_MAX UINT32_MAX
#define DMA_TO_DEVICE 1
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
struct scatterlist {dma_addr_t addr;unsigned len;};
struct sg_table {unsigned nents;struct scatterlist *sgl;};
struct dma_buf_attachment {int unused;};
struct venc_import {struct dma_buf_attachment *attach;struct sg_table *sgt;dma_addr_t dma_addr;unsigned long size;bool contiguous;};
#define for_each_sgtable_dma_sg(t,sg,i) for((i)=0,(sg)=(t)->sgl;(i)<(t)->nents;(i)++,(sg)++)
static dma_addr_t sg_dma_address(const struct scatterlist *sg){return sg->addr;}
static unsigned sg_dma_len(const struct scatterlist *sg){return sg->len;}
static struct sg_table *current_table;
static struct sg_table *dma_buf_map_attachment(struct dma_buf_attachment *a,int dir){
 (void)a;assert(dir==DMA_TO_DEVICE);return current_table;
}
''' + body + r'''
int main(void){
 struct scatterlist sgs[2];struct sg_table table={2,sgs};
 struct venc_import buf={.size=4096};
 current_table=&table;
 /* Exactly at 4 GiB boundary with two adjacent segments passes. */
 sgs[0]=(struct scatterlist){(dma_addr_t)U32_MAX+1-4096,2048};
 sgs[1]=(struct scatterlist){(dma_addr_t)U32_MAX+1-2048,2048};
 assert(!venc_in_map(&buf)&&buf.contiguous&&buf.dma_addr==sgs[0].addr);
 /* Crossing 4 GiB by 1 byte rejects direct DMA contiguity. */
 sgs[0].addr++;sgs[1].addr++;
 assert(!venc_in_map(&buf)&&!buf.contiguous);
 /* Non-adjacent segments reject direct DMA contiguity. */
 sgs[0]=(struct scatterlist){0x200000,2048};
 sgs[1]=(struct scatterlist){0x202000,2048};
 assert(!venc_in_map(&buf)&&!buf.contiguous);
 /* Empty sg_table or zero base address never marks contiguous. */
 table.nents=0;assert(!venc_in_map(&buf)&&!buf.contiguous&&!buf.dma_addr);
 table.nents=1;sgs[0]=(struct scatterlist){0,4096};
 assert(!venc_in_map(&buf)&&!buf.contiguous);
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
