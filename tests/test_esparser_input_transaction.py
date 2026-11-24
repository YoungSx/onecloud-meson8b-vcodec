"""Input preparation and its offset must commit together before a DMA kick."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ParserInputTransactionTests(unittest.TestCase):
    def test_prepared_input_failures_abort_without_kicking(self):
        source = (ROOT / "vdec/esparser.c").read_text()
        body = source[source.index("static int\nesparser_queue("):
                      source.index("/* Feed every m2m OUTPUT buffer")]
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <errno.h>
typedef uint32_t u32;typedef uint64_t u64;typedef uintptr_t dma_addr_t;
#define V4L2_PIX_FMT_VP9 1
#define V4L2_PIX_FMT_H263 2
#define V4L2_PIX_FMT_VC1_ANNEX_L 3
#define SZ_1M (1024u*1024)
#define VB2_BUF_STATE_ERROR 1
#define VB2_BUF_STATE_DONE 2
#define V4L2_FIELD_NONE 0
#define dev_dbg(...) ((void)0)
#define dev_warn(...) ((void)0)
struct vb2_buffer {u32 size;u64 timestamp;};
struct vb2_v4l2_buffer {struct vb2_buffer vb2_buf;u32 flags,field,sequence;int timecode;};
struct amvdec_session;
struct amvdec_codec_ops {int (*inspect_input)(struct amvdec_session *,struct vb2_buffer *,u32);u32 (*num_pending_bufs)(struct amvdec_session *);};
struct amvdec_format {u32 pixfmt;struct amvdec_codec_ops *codec_ops;};
struct amvdec_core {void *dev;};
struct amvdec_session {struct amvdec_core *core;struct amvdec_format *fmt_out;void *m2m_ctx;u32 sequence_out;int esparser_queued_bufs;};
static int ts_result,pad_result,aborted,kicks,removed,taken,prepared,done,error;
static u32 vb2_get_plane_payload(struct vb2_buffer *b,int p){(void)p;return b->size;}
static dma_addr_t vb2_dma_contig_plane_dma_addr(struct vb2_buffer *b,int p){(void)b;(void)p;return 0x1000;}
static u32 v4l2_m2m_num_dst_bufs_ready(void *p){(void)p;return 16;}
static u32 esparser_vififo_get_free_space(struct amvdec_session *s){(void)s;return 2*SZ_1M;}
static void v4l2_m2m_src_buf_remove_by_buf(void *p,struct vb2_v4l2_buffer *b){(void)p;(void)b;taken++;}
static void v4l2_m2m_buf_done(struct vb2_v4l2_buffer *b,int state){(void)b;if(state==VB2_BUF_STATE_DONE)done++;else error++;}
static int atomic_read(int *p){return *p;}
static void atomic_inc(int *p){(*p)++;}
static void amvdec_abort(struct amvdec_session *s){(void)s;aborted++;}
static u32 esparser_get_offset(struct amvdec_session *s){(void)s;return 0x80;}
static int prepare(struct amvdec_session *s,struct vb2_buffer *b,u32 offset){(void)s;assert(offset==0x80);b->size=32;prepared++;return 0;}
static int amvdec_add_ts(struct amvdec_session *s,u64 ts,int tc,u32 offset,u32 flags){(void)s;(void)ts;(void)tc;(void)flags;assert(offset==0x80);return ts_result;}
static void amvdec_remove_ts(struct amvdec_session *s,u64 ts){(void)s;(void)ts;removed++;}
static u32 vp9_update_header(struct amvdec_core *c,struct vb2_buffer *b){(void)c;return b->size;}
static int esparser_pad_start_code(struct amvdec_core *c,struct vb2_buffer *b,u32 size){(void)c;(void)b;assert(size==32);return pad_result;}
static int esparser_write_data(struct amvdec_core *c,dma_addr_t p,u32 size){(void)c;assert(p==0x1000&&size==48);kicks++;return 1;}
''' + body + r'''
int main(void){struct amvdec_codec_ops ops={.inspect_input=prepare};struct amvdec_format f={4,&ops};struct amvdec_core c={0};struct amvdec_session s={.core=&c,.fmt_out=&f};struct vb2_v4l2_buffer b={.vb2_buf={16,10}};
 ts_result=-ENOMEM;assert(esparser_queue(&s,&b)==-ENOMEM&&prepared==1&&aborted==1&&error==1&&!kicks&&!removed&&!s.esparser_queued_bufs);
 ts_result=0;pad_result=-ENOSPC;assert(!esparser_queue(&s,&b)&&prepared==2&&aborted==2&&error==2&&!kicks&&removed==1&&!s.esparser_queued_bufs);
 pad_result=16;assert(!esparser_queue(&s,&b)&&prepared==3&&aborted==2&&kicks==1&&done==1&&taken==3&&s.esparser_queued_bufs==1);
 return 0;}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(["cc", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-Wno-sign-compare",
                            "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            str(path / "test.c"), "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)
