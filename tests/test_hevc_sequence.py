"""An in-band HEVC EOS cannot finish a later client drain or erase read-ahead."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HevcSequenceTests(unittest.TestCase):
    def test_sequence_eos_precedes_the_driver_drain_marker(self):
        source = (ROOT / "vdec/codec_hevc.c").read_text()
        irq = source[source.index("static irqreturn_t codec_hevc_threaded_isr("):
                     source.index("static irqreturn_t codec_hevc_isr(")]
        helper = ""
        if "static void codec_hevc_handle_eos(" in source:
            helper = source[source.index("static void codec_hevc_handle_eos("):
                            source.index("static irqreturn_t codec_hevc_threaded_isr(")]
        terminator = source[source.index("static const u8 *codec_hevc_eos_sequence("):
                            source.index("/* An empty LAST buffer")]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
typedef uint8_t u8;typedef uint32_t u32;typedef int irqreturn_t;
#define IRQ_HANDLED 1
#define BIT(n) (1u<<(n))
#define READ_ONCE(x) (x)
#define INVALID_POC 0x80000000u
#define HEVC_DEC_STATUS_REG HEVC_ASSIST_SCRATCH_0
#define HEVC_WAIT_FLAG HEVC_ASSIST_SCRATCH_E
#define HEVC_ACTION_DONE 255
#define HEVC_DISCARD_NAL 240
#define AMRISC_MAIN_REQ 4
#define HEVC_NAL_SEARCH_DONE 9
#define HEVC_SLICE_SEGMENT_DONE 8
#define HEVC_REFERENCE_CHANGE 2
#define AMVDEC_POOL_RESTART 3
#define dev_dbg(...) ((void)0)
#define dev_err(...) ((void)0)
#include "hevc_regs.h"
enum amvdec_status {STATUS_RUNNING,STATUS_NEEDS_RESUME};
struct hevc_frame {int unused;};
struct codec_hevc {int lock;u32 pending_eos,curr_poc,col_poc,slice_addr,slice_segment_addr,dpb_size,dst_width,dst_height,frames_num;
 bool have_picture,skip_rasl,drain_pending,eos_seen,after_eos,client_waited,waiting_for_buffer;struct hevc_frame *cur_frame,*col_frame;};
struct amvdec_core {void *dev,*dev_dec;};
struct amvdec_session {struct amvdec_core *core;void *priv;int esparser_queue_work;bool stop_requested;u32 width,height,dpb_size;int streamon_cap,keyframe_found;enum amvdec_status status;};
static int flushed,bumped,cleared,last,aborted,writes,scheduled;static int snapshots=2;
static void mutex_lock(int *p){assert(!*p);*p=1;}static void mutex_unlock(int *p){assert(*p);*p=0;}
static u32 amvdec_read_dos(struct amvdec_core *c,u32 r){(void)c;if(r==HEVC_DEC_STATUS_REG)return 9;if(r==HEVC_ASSIST_SCRATCH_J)return 36;return 0;}
static void amvdec_write_dos(struct amvdec_core *c,u32 r,u32 v){(void)c;assert((r==HEVC_DEC_STATUS_REG&&v==240)||(r==HEVC_MCPU_INTR_REQ&&v==4));writes++;}
static void amvdec_clear_dos_bits(struct amvdec_core *c,u32 r,u32 v){(void)c;assert(r==HEVC_WAIT_FLAG&&v==2);writes++;}
static void schedule_work(int *p){(void)p;scheduled++;}
static void codec_hevc_flush_output(struct amvdec_session *s){struct codec_hevc *h=s->priv;flushed++;h->frames_num=0;}
static void codec_hevc_clear_inputs(struct codec_hevc *h){(void)h;cleared++;snapshots=0;}
static void codec_hevc_finish_eos(struct amvdec_session *s){(void)s;last++;}
static void amvdec_abort(struct amvdec_session *s){(void)s;aborted++;}
static void codec_hevc_fetch_rpm(struct amvdec_session *s){(void)s;assert(0);}
static int codec_hevc_process_rpm(struct amvdec_session *s){(void)s;assert(0);return 0;}
static void codec_hevc_show_frames(struct amvdec_session *s){struct codec_hevc *h=s->priv;bumped++;h->frames_num=3;}
static void codec_hevc_discard_prior_output(struct amvdec_session *s){(void)s;assert(0);}
static struct hevc_frame *codec_hevc_show_pending(struct amvdec_session *s,bool hold){(void)s;(void)hold;assert(0);return 0;}
static void codec_hevc_deliver_frame(struct amvdec_session *s,struct hevc_frame *f){(void)s;(void)f;assert(0);}
static void amvdec_pool_end(struct amvdec_session *s,int end){(void)s;(void)end;assert(0);}
static void amvdec_src_change(struct amvdec_session *s,u32 w,u32 h,u32 n){(void)s;(void)w;(void)h;(void)n;assert(0);}
static u32 amvdec_pool_count(struct amvdec_session *s){(void)s;assert(0);return 0;}
static int codec_hevc_setup_pool(struct amvdec_session *s){(void)s;assert(0);return 0;}
static int codec_hevc_process_segment(struct amvdec_session *s){(void)s;assert(0);return 0;}
''' + terminator + helper + irq + r'''
int main(void){struct codec_hevc h={.pending_eos=2,.have_picture=true,.skip_rasl=true,.curr_poc=40,.col_poc=30,.slice_addr=7,.slice_segment_addr=9,.frames_num=3};
 struct amvdec_core c={0};struct amvdec_session s={.core=&c,.priv=&h};struct hevc_frame previous;
 h.cur_frame=h.col_frame=&previous;
 assert(codec_hevc_threaded_isr(&s)==IRQ_HANDLED);
 assert(h.pending_eos==1&&!h.eos_seen&&!h.have_picture&&!h.skip_rasl&&h.curr_poc==INVALID_POC&&h.col_poc==INVALID_POC);
 assert(!h.cur_frame&&!h.col_frame);
 assert(!h.slice_addr&&!h.slice_segment_addr&&h.frames_num==3&&h.after_eos&&!flushed&&bumped==1&&!last&&!cleared&&snapshots==2&&writes==3&&!aborted);
 /* STOP already submitted its marker, but this interrupt is the older natural EOS. */
 s.stop_requested=true;u32 length;const u8 *eos=codec_hevc_eos_sequence(&s,&length);
 assert(h.drain_pending&&!h.eos_seen&&length==4096&&eos[3]==0x48);
 h.have_picture=true;h.curr_poc=50;
 codec_hevc_threaded_isr(&s);
 assert(!h.pending_eos&&h.drain_pending&&!h.eos_seen&&!flushed&&bumped==2&&!last&&!cleared&&snapshots==2&&writes==6&&!aborted);
 codec_hevc_threaded_isr(&s);
 assert(h.eos_seen&&!h.drain_pending&&flushed==1&&bumped==2&&last==1&&cleared==1&&!snapshots&&writes==6&&!aborted);
 /* Terminal drain stays paused; duplicate IRQs cannot abort queued tail copies. */
 codec_hevc_threaded_isr(&s);assert(last==1&&cleared==1&&!aborted&&writes==6);
 assert(scheduled==2);
 return 0;}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(["cc", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-function", "-Wno-unused-parameter",
                            "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            "-I", str(ROOT / "vdec"), str(path / "test.c"),
                            "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)
