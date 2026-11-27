"""Actual worker returns both NV12 planes and keeps DMA completion before teardown."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Ge2dQueueTests(unittest.TestCase):
    def test_two_plane_delivery_error_and_streamoff(self):
        source = (ROOT / "ge2d/meson8b-ge2d.c").read_text()
        helpers = source[source.index("static bool ge2d_is_nv12("):
                         source.index("static void ge2d_work(")]
        worker = source[source.index("static void ge2d_work("):
                        source.index("static void ge2d_run(")]
        stop = source[source.index("static void ge2d_stop("):
                      source.index("static const struct vb2_ops")]
        code = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <linux/videodev2.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;typedef uint64_t dma_addr_t;
#define U32_MAX UINT32_MAX
#define ERESTARTSYS 512
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#include "ge2d-layout.h"
struct work_struct {int unused;};
struct ge2d_ctx;struct meson8b_ge2d {struct work_struct work;struct ge2d_ctx *active;void *m2m;int hardware_lock;bool ready;};
struct vb2_v4l2_buffer;
struct queue {u32 type;struct ge2d_ctx *ctx;struct vb2_v4l2_buffer *buffer;};
struct v4l2_fh {struct m2m *m2m_ctx;};
struct dma_resv {long writer,readers;unsigned waits;};struct dma_buf {struct dma_resv *resv;};
struct vb2_buffer {u32 num_planes,type,memory;struct queue *vb2_queue;u32 payload[2];dma_addr_t address[2];u64 timestamp;
 struct {struct dma_buf *dbuf;} planes[2];};
struct vb2_v4l2_buffer {struct vb2_buffer vb2_buf;u32 sequence;int state;};
struct m2m {struct queue src,dst;};
struct ge2d_ctx {struct v4l2_fh fh;struct meson8b_ge2d *core;struct v4l2_pix_format_mplane input,output;
 struct meson8b_ge2d_crop crop;u32 sequence;};
#define vb2_queue queue
#define VB2_BUF_STATE_DONE 1
#define VB2_BUF_STATE_ERROR 2
static struct meson8b_ge2d core;
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
static int blit_error,finished,flushed;static unsigned commands;
static int locked;
static unsigned msecs_to_jiffies(unsigned ms){assert(ms==1000);return 100;}
static long __attribute__((unused)) dma_resv_wait_timeout(struct dma_resv *r,bool all,bool intr,unsigned timeout){
 assert(!locked&&intr&&timeout==msecs_to_jiffies(1000));r->waits++;
 return r->writer<=0?r->writer:all?r->readers:r->writer;
}
static void mutex_lock(int *l){(void)l;assert(!locked);locked=1;}
static void mutex_unlock(int *l){(void)l;assert(locked);locked=0;}
static void *vb2_get_drv_priv(struct queue *q){return q->ctx;}
static dma_addr_t vb2_dma_contig_plane_dma_addr(struct vb2_buffer *b,unsigned i){assert(i<b->num_planes);return b->address[i];}
static void vb2_set_plane_payload(struct vb2_buffer *b,unsigned i,u32 value){assert(i<b->num_planes);b->payload[i]=value;}
static struct vb2_v4l2_buffer *remove_buffer(struct queue *q){struct vb2_v4l2_buffer *b=q->buffer;q->buffer=NULL;return b;}
static struct vb2_v4l2_buffer *v4l2_m2m_src_buf_remove(struct m2m *m){return remove_buffer(&m->src);}
static struct vb2_v4l2_buffer *v4l2_m2m_dst_buf_remove(struct m2m *m){return remove_buffer(&m->dst);}
static int ge2d_blit(struct meson8b_ge2d *c,const struct meson8b_ge2d_image *s,
 const struct meson8b_ge2d_image *d,const struct meson8b_ge2d_crop *crop){
 (void)c;assert(s->nv12&&d->nv12&&s->address[1]==0x200000&&d->address[1]==0x400000);
 assert(crop->width==512&&crop->height==32);commands++;return blit_error;
}
static void v4l2_m2m_buf_copy_metadata(struct vb2_v4l2_buffer *s,struct vb2_v4l2_buffer *d,bool copy){assert(copy);d->vb2_buf.timestamp=s->vb2_buf.timestamp;}
static void v4l2_m2m_buf_done(struct vb2_v4l2_buffer *b,int state){b->state=state;}
static void v4l2_m2m_job_finish(void *dev,struct m2m *m){(void)dev;(void)m;assert(!core.active);finished++;}
static void flush_work(struct work_struct *w){(void)w;assert(!core.active);flushed++;}
''' + helpers + worker + stop + r'''
int main(void){
 struct m2m m={0};struct ge2d_ctx c={.fh.m2m_ctx=&m,.core=&core,
 .input={.width=512,.height=32,.num_planes=2,.pixelformat=V4L2_PIX_FMT_NV12M,
 .plane_fmt={{.bytesperline=512,.sizeimage=16384},{.bytesperline=512,.sizeimage=8192}}},
 .output={.width=256,.height=16,.num_planes=2,.pixelformat=V4L2_PIX_FMT_NV12M,
 .plane_fmt={{.bytesperline=256,.sizeimage=4096},{.bytesperline=256,.sizeimage=2048}}},
 .crop={.width=512,.height=32}};
 m.src.ctx=m.dst.ctx=&c;m.src.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;m.dst.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
 struct vb2_v4l2_buffer s={.vb2_buf={.num_planes=2,.address={0x100000,0x200000},.timestamp=123}},
 d={.vb2_buf={.num_planes=2,.address={0x300000,0x400000}}};
 core.ready=true;core.active=&c;m.src.buffer=&s;m.dst.buffer=&d;ge2d_work(&core.work);
 assert(s.state==1&&d.state==1&&d.vb2_buf.payload[0]==4096&&d.vb2_buf.payload[1]==2048);
 assert(d.vb2_buf.timestamp==123&&d.sequence==0&&finished==1&&commands==1);
 blit_error=-ETIMEDOUT;core.active=&c;m.src.buffer=&s;m.dst.buffer=&d;ge2d_work(&core.work);
 assert(s.state==2&&d.state==2&&!d.vb2_buf.payload[0]&&!d.vb2_buf.payload[1]&&finished==2);
 core.ready=false;core.active=&c;m.src.buffer=&s;m.dst.buffer=&d;ge2d_work(&core.work);
 assert(commands==2&&finished==3&&s.state==2&&d.state==2&&!locked);
 m.src.buffer=&s;ge2d_stop(&m.src);assert(s.state==2&&flushed==1&&!m.src.buffer);
 m.dst.buffer=&d;ge2d_stop(&m.dst);assert(d.state==2&&flushed==2&&!m.dst.buffer);
 /* Imported source waits for writers; destination also waits for readers. */
 struct dma_resv resv[4]={{1,1,0},{1,1,0},{1,1,0},{1,1,0}};
 struct dma_buf dbuf[4]={{&resv[0]},{&resv[1]},{&resv[2]},{&resv[3]}};
 s.vb2_buf.type=m.src.type;d.vb2_buf.type=m.dst.type;
 s.vb2_buf.memory=d.vb2_buf.memory=V4L2_MEMORY_DMABUF;
 for(unsigned i=0;i<2;i++){s.vb2_buf.planes[i].dbuf=&dbuf[i];d.vb2_buf.planes[i].dbuf=&dbuf[i+2];}
 core.ready=true;blit_error=0;
 for(unsigned plane=0;plane<4;plane++)for(unsigned error=0;error<2;error++){
  unsigned before=commands;resv[plane].writer=error?-ERESTARTSYS:0;
  core.active=&c;m.src.buffer=&s;m.dst.buffer=&d;ge2d_work(&core.work);
  assert(commands==before&&s.state==2&&d.state==2&&!d.vb2_buf.payload[0]&&!d.vb2_buf.payload[1]);
  assert(!core.active&&!locked);resv[plane].writer=1;
 }
 /* A shared reader must not block reading the source, but blocks writing dst. */
 resv[0].readers=resv[1].readers=0;unsigned before=commands;
 core.active=&c;m.src.buffer=&s;m.dst.buffer=&d;ge2d_work(&core.work);
 assert(commands==before+1&&s.state==1&&d.state==1);
 for(unsigned plane=2;plane<4;plane++){
  before=commands;resv[plane].readers=0;
  core.active=&c;m.src.buffer=&s;m.dst.buffer=&d;ge2d_work(&core.work);
  assert(commands==before&&s.state==2&&d.state==2);resv[plane].readers=1;
 }
 /* Requeuing the same imports observes new fences every time. */
 unsigned old_waits[4];for(unsigned i=0;i<4;i++)old_waits[i]=resv[i].waits;
 for(unsigned frame=0;frame<3;frame++){
  core.active=&c;m.src.buffer=&s;m.dst.buffer=&d;ge2d_work(&core.work);
  assert(s.state==1&&d.state==1);
 }
 for(unsigned i=0;i<4;i++)assert(resv[i].waits==old_waits[i]+3);
 /* Each side independently supports MMAP or imports. */
 for(unsigned src_dma=0;src_dma<2;src_dma++)for(unsigned dst_dma=0;dst_dma<2;dst_dma++){
  s.vb2_buf.memory=src_dma?V4L2_MEMORY_DMABUF:V4L2_MEMORY_MMAP;
  d.vb2_buf.memory=dst_dma?V4L2_MEMORY_DMABUF:V4L2_MEMORY_MMAP;
  for(unsigned i=0;i<4;i++)old_waits[i]=resv[i].waits;
  core.active=&c;m.src.buffer=&s;m.dst.buffer=&d;ge2d_work(&core.work);
  assert(s.state==1&&d.state==1);
  for(unsigned i=0;i<4;i++)assert(resv[i].waits==old_waits[i]+(i<2?src_dma:dst_dma));
 }
 /* GREY and contiguous NV12 have one import; preserve its exact wait error. */
 s.vb2_buf.num_planes=d.vb2_buf.num_planes=1;
 unsigned waits=resv[1].waits+resv[3].waits;
 assert(!ge2d_wait_buffer(&s.vb2_buf)&&!ge2d_wait_buffer(&d.vb2_buf));
 assert(resv[1].waits+resv[3].waits==waits);
 resv[0].writer=0;assert(ge2d_wait_buffer(&s.vb2_buf)==-ETIMEDOUT);
 resv[0].writer=-ERESTARTSYS;assert(ge2d_wait_buffer(&s.vb2_buf)==-ERESTARTSYS);
 resv[0].writer=1;resv[2].readers=-ERESTARTSYS;
 assert(ge2d_wait_buffer(&d.vb2_buf)==-ERESTARTSYS);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                            "-O1", "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            "-I", str(ROOT / "ge2d"), str(path / "test.c"),
                            "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)
