"""Input preparation and its offset must commit together before a DMA kick."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ParserInputTransactionTests(unittest.TestCase):
    def test_stop_holds_later_input_behind_the_drain_marker(self):
        source = (ROOT / "vdec/esparser.c").read_text()
        body = source[source.index("void esparser_queue_all(struct"):
                      source.index("void esparser_queue_all_src(")]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#define READ_ONCE(x) (x)
#define STATUS_STOPPED 0
#define V4L2_FMT_FLAG_DYN_RESOLUTION 1
struct v4l2_m2m_buffer {int vb;};
struct format {int flags;};
struct amvdec_session {bool should_stop,stop_requested,streamon_cap;int status,lock;void *m2m_ctx;struct format *fmt_out;};
static struct v4l2_m2m_buffer buffers[2];static int cursor,submitted;
#define v4l2_m2m_for_each_src_buf_safe(ctx,buf,n) \
 for(cursor=0,buf=&buffers[0],n=&buffers[1];cursor<2;cursor++,buf=&buffers[cursor])
static void mutex_lock(int *p){assert(!*p);*p=1;}
static void mutex_unlock(int *p){assert(*p);*p=0;}
static int esparser_queue(struct amvdec_session *s,int *b){assert(s->lock&&!s->stop_requested);(void)b;submitted++;return 0;}
''' + body + r'''
int main(void){struct format f={.flags=V4L2_FMT_FLAG_DYN_RESOLUTION};struct amvdec_session s={.status=1,.fmt_out=&f};
 s.stop_requested=true;esparser_queue_all(&s);assert(!submitted&&!s.lock);
 s.stop_requested=false;esparser_queue_all(&s);assert(submitted==2&&!s.lock);
 s.should_stop=true;esparser_queue_all(&s);assert(submitted==2&&!s.lock);
 s.should_stop=false;s.status=STATUS_STOPPED;esparser_queue_all(&s);assert(submitted==2&&!s.lock);
 return 0;}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(["cc", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-but-set-variable", "-fsanitize=address,undefined",
                            "-fno-pie", "-no-pie", str(path / "test.c"),
                            "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)

    def test_prepared_input_failures_abort_without_kicking(self):
        source = (ROOT / "vdec/esparser.c").read_text()
        body = source[source.index("static int\nesparser_queue("):
                      source.index("/* Feed every m2m OUTPUT buffer")]
        prepare_body = ""
        if "static int esparser_prepare_input(" in source:
            prepare_body = source[source.index("static int esparser_prepare_input("):
                                  source.index("static int\nesparser_queue(")]
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <errno.h>
#include <stddef.h>
#include <stdbool.h>
typedef uint8_t u8;typedef uint32_t u32;typedef uint64_t u64;typedef uintptr_t dma_addr_t;
#define V4L2_PIX_FMT_VP9 1
#define V4L2_PIX_FMT_H263 2
#define V4L2_PIX_FMT_VC1_ANNEX_L 3
#define SZ_1M (1024u*1024)
#define VB2_BUF_STATE_ERROR 1
#define VB2_BUF_STATE_DONE 2
#define V4L2_FIELD_NONE 0
#define dev_dbg(...) ((void)0)
#define dev_warn(...) ((void)0)
#define DMA_BIDIRECTIONAL 0
struct dma_buf {int unused;};
struct vb2_buffer {u32 size;u64 timestamp;struct {u32 data_offset;struct dma_buf *dbuf;}planes[1];};
struct vb2_v4l2_buffer {struct vb2_buffer vb2_buf;u32 flags,field,sequence;int timecode;};
struct amvdec_session;
struct amvdec_codec_ops {int (*inspect_input)(struct amvdec_session *,struct vb2_buffer *,u32);u32 (*num_pending_bufs)(struct amvdec_session *);};
struct amvdec_format {u32 pixfmt;struct amvdec_codec_ops *codec_ops;};
struct amvdec_core {void *dev;};
struct amvdec_session {struct amvdec_core *core;struct amvdec_format *fmt_out;void *m2m_ctx;u32 sequence_out;int esparser_queued_bufs;};
static int ts_result,pad_result,aborted,kicks,removed,taken,prepared,done,error;
static int begin_error,end_error,begins,ends,inspect_error;
static bool cpu_access,imported,published;
static int dma_buf_begin_cpu_access(struct dma_buf *d,int dir){assert(d&&dir==DMA_BIDIRECTIONAL&&!cpu_access);begins++;if(begin_error)return begin_error;cpu_access=true;return 0;}
static int dma_buf_end_cpu_access(struct dma_buf *d,int dir){assert(d&&dir==DMA_BIDIRECTIONAL&&cpu_access);ends++;cpu_access=false;return end_error;}
static void dma_wmb(void){assert(!cpu_access);published=true;}
static u32 expected_data_offset;
static u32 vb2_get_plane_payload(struct vb2_buffer *b,int p){(void)p;return b->size;}
static unsigned long vb2_plane_size(struct vb2_buffer *b,int p){(void)b;(void)p;return 8192;}
static void *vb2_plane_vaddr(struct vb2_buffer *b,int p){(void)b;(void)p;return 0;}
static void vb2_set_plane_payload(struct vb2_buffer *b,int p,u32 n){(void)p;b->size=n;}
#include "vdec-input.h"
static dma_addr_t vb2_dma_contig_plane_dma_addr(struct vb2_buffer *b,int p){(void)b;(void)p;return 0x1000;}
static u32 v4l2_m2m_num_dst_bufs_ready(void *p){(void)p;return 16;}
static u32 esparser_vififo_get_free_space(struct amvdec_session *s){(void)s;return 2*SZ_1M;}
static void v4l2_m2m_src_buf_remove_by_buf(void *p,struct vb2_v4l2_buffer *b){(void)p;(void)b;taken++;}
static void v4l2_m2m_buf_done(struct vb2_v4l2_buffer *b,int state){(void)b;assert(!cpu_access);if(state==VB2_BUF_STATE_DONE)done++;else error++;}
static int atomic_read(int *p){return *p;}
static void atomic_inc(int *p){(*p)++;}
static void amvdec_abort(struct amvdec_session *s){(void)s;aborted++;}
static u32 esparser_get_offset(struct amvdec_session *s){(void)s;return 0x80;}
static int prepare(struct amvdec_session *s,struct vb2_buffer *b,u32 offset){(void)s;assert(!imported||cpu_access);assert(offset==0x80);assert(amvdec_input_size(b)==16);amvdec_input_set_size(b,32);prepared++;return inspect_error;}
static int amvdec_add_ts(struct amvdec_session *s,u64 ts,int tc,u32 offset,u32 flags){(void)s;(void)ts;(void)tc;(void)flags;assert(offset==0x80);return ts_result;}
static void amvdec_remove_ts(struct amvdec_session *s,u64 ts){(void)s;(void)ts;removed++;}
static u32 vp9_update_header(struct amvdec_core *c,struct vb2_buffer *b){(void)c;return b->size;}
static int esparser_pad_start_code(struct amvdec_core *c,struct vb2_buffer *b,u32 size){(void)c;(void)b;assert((!imported||cpu_access)&&size==32);return pad_result;}
static int esparser_write_data(struct amvdec_core *c,dma_addr_t p,u32 size){(void)c;assert(published&&!cpu_access&&p==0x1000+expected_data_offset&&size==48);published=false;kicks++;return 1;}
''' + prepare_body + body + r'''
int main(void){struct amvdec_codec_ops ops={.inspect_input=prepare};struct amvdec_format f={4,&ops};struct amvdec_core c={0};struct amvdec_session s={.core=&c,.fmt_out=&f};struct vb2_v4l2_buffer b={.vb2_buf={.size=16,.timestamp=10}};
 pad_result=16;ts_result=-ENOMEM;assert(esparser_queue(&s,&b)==-ENOMEM&&prepared==1&&aborted==1&&error==1&&!kicks&&!removed&&!s.esparser_queued_bufs);
 b.vb2_buf.size=16;ts_result=0;pad_result=-ENOSPC;assert(!esparser_queue(&s,&b)&&prepared==2&&aborted==2&&error==2&&!kicks&&!s.esparser_queued_bufs);
 b.vb2_buf.size=16;pad_result=16;assert(!esparser_queue(&s,&b)&&prepared==3&&aborted==2&&kicks==1&&done==1&&taken==3&&s.esparser_queued_bufs==1);
 expected_data_offset=b.vb2_buf.planes[0].data_offset=37;b.vb2_buf.size=37+16;
 assert(!esparser_queue(&s,&b)&&prepared==4&&aborted==2&&kicks==2&&done==2&&taken==4&&s.esparser_queued_bufs==2);
 assert(b.vb2_buf.size==37+32&&b.vb2_buf.planes[0].data_offset==37);
 struct dma_buf dbuf={0};b.vb2_buf.planes[0].dbuf=&dbuf;imported=true;b.vb2_buf.size=37+16;
 assert(!esparser_queue(&s,&b)&&begins==1&&ends==1&&prepared==5&&kicks==3&&!cpu_access);
 begin_error=-EINTR;b.vb2_buf.size=37+16;
 assert(!esparser_queue(&s,&b)&&begins==2&&ends==1&&prepared==5&&kicks==3&&!cpu_access);
 begin_error=0;end_error=-EIO;b.vb2_buf.size=37+16;
 assert(!esparser_queue(&s,&b)&&begins==3&&ends==2&&prepared==6&&kicks==3&&!cpu_access);
 end_error=0;inspect_error=-EINVAL;b.vb2_buf.size=37+16;
 assert(!esparser_queue(&s,&b)&&begins==4&&ends==3&&prepared==7&&kicks==3&&!cpu_access);
 inspect_error=0;pad_result=-ENOSPC;b.vb2_buf.size=37+16;
 assert(!esparser_queue(&s,&b)&&begins==5&&ends==4&&kicks==3&&!cpu_access);
 pad_result=16;ts_result=-ENOMEM;b.vb2_buf.size=37+16;
 assert(esparser_queue(&s,&b)==-ENOMEM&&begins==6&&ends==5&&kicks==3&&!cpu_access);
 ts_result=0;inspect_error=1;b.vb2_buf.size=37+16;
 assert(!esparser_queue(&s,&b)&&begins==7&&ends==6&&kicks==3&&!cpu_access);
 ops.inspect_input=0;b.vb2_buf.size=37+32;
 assert(!esparser_queue(&s,&b)&&begins==8&&ends==7&&kicks==4&&!cpu_access);
 ops.inspect_input=prepare;inspect_error=-EINVAL;end_error=-EIO;b.vb2_buf.size=37+16;
 u32 payload;assert(esparser_prepare_input(&s,&b.vb2_buf,0x80,&payload)==-EINVAL&&begins==9&&ends==8&&!cpu_access);
 return 0;}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(["cc", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-Wno-sign-compare", "-Wno-unused-function", "-I", str(ROOT / "vdec"),
                            "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            str(path / "test.c"), "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)
