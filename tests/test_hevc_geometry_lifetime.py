"""Coded-size changes replace references; display-only changes retain them."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def run_c(code):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        (path / "test.c").write_text(code)
        subprocess.run(["cc", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-function", "-Wno-unused-parameter",
                        "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                        "-I", str(ROOT / "vdec"), str(path / "test.c"),
                        "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)


class HevcGeometryLifetimeTests(unittest.TestCase):
    def test_irq_preserves_crop_refs_and_releases_coded_refs(self):
        source = (ROOT / "vdec/codec_hevc.c").read_text()
        irq = source[source.index("static irqreturn_t codec_hevc_threaded_isr("):
                     source.index("static irqreturn_t codec_hevc_isr(")]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t u32;typedef int irqreturn_t;
#define IRQ_HANDLED 1
#define HEVC_SOURCE_CHANGE 1
#define HEVC_REFERENCE_CHANGE 2
#define HEVC_NAL_SEARCH_DONE 9
#define HEVC_SLICE_SEGMENT_DONE 8
#define HEVC_DEC_STATUS_REG HEVC_ASSIST_SCRATCH_0
#define EAGAIN 11
#define dev_dbg(...) ((void)0)
#define dev_err(...) ((void)0)
#include "dos_regs.h"
#include "hevc_regs.h"
enum amvdec_status {STATUS_STOPPED,STATUS_RUNNING,STATUS_NEEDS_RESUME};
struct hevc_frame {int unused;};
struct codec_hevc {int lock;u32 dpb_size,dst_width,dst_height,frames_num;bool client_waited,eos_seen,waiting_for_buffer;};
struct amvdec_core {void *dev_dec;};
struct amvdec_session {struct amvdec_core *core;void *priv;int keyframe_found,streamon_cap;u32 dpb_size;enum amvdec_status status;};
static struct hevc_frame last;
static u32 change,pool_count=16;static int live_refs,flushed,forgotten,delivered,events,setups,segments;
static void mutex_lock(int *p){assert(!*p);*p=1;}static void mutex_unlock(int *p){assert(*p);*p=0;}
static u32 amvdec_read_dos(struct amvdec_core *c,u32 r){(void)c;assert(r==HEVC_DEC_STATUS_REG);return HEVC_SLICE_SEGMENT_DONE;}
static void codec_hevc_fetch_rpm(struct amvdec_session *s){(void)s;}
static int codec_hevc_process_rpm(struct amvdec_session *s){(void)s;return change;}
static struct hevc_frame *codec_hevc_show_pending(struct amvdec_session *s,bool hold){assert(hold);((struct codec_hevc *)s->priv)->frames_num=1;return &last;}
static void codec_hevc_deliver_frame(struct amvdec_session *s,struct hevc_frame *f){assert(s->status==STATUS_NEEDS_RESUME&&f==&last);delivered++;}
static void codec_hevc_forget_frames(struct codec_hevc *h){h->frames_num=0;live_refs=0;forgotten++;}
static void codec_hevc_flush_output(struct amvdec_session *s){assert(((struct codec_hevc *)s->priv)->frames_num==0);live_refs=0;flushed++;}
static void codec_hevc_finish_eos(struct amvdec_session *s){(void)s;assert(0);}
static void codec_hevc_clear_inputs(struct codec_hevc *h){(void)h;assert(0);}
static void amvdec_src_change(struct amvdec_session *s,u32 w,u32 h,u32 n){assert(s->status==STATUS_RUNNING&&w==320&&h==192&&n==16);events++;}
static u32 amvdec_pool_count(struct amvdec_session *s){(void)s;return pool_count;}
static int codec_hevc_setup_pool(struct amvdec_session *s){assert(s->dpb_size==16&&live_refs==8);setups++;return 0;}
static int codec_hevc_process_segment(struct amvdec_session *s){(void)s;segments++;return 0;}
static void amvdec_abort(struct amvdec_session *s){(void)s;assert(0);}
''' + irq + r'''
int main(void){struct codec_hevc h={.dpb_size=16,.dst_width=320,.dst_height=192};struct amvdec_core c={0};struct amvdec_session s={.core=&c,.priv=&h,.streamon_cap=1,.status=STATUS_RUNNING};
 change=HEVC_SOURCE_CHANGE;live_refs=8;
 assert(codec_hevc_threaded_isr(&s)==IRQ_HANDLED&&live_refs==8&&!flushed&&!forgotten&&delivered==1&&events==1&&!h.frames_num);
 change=HEVC_REFERENCE_CHANGE;
 assert(codec_hevc_threaded_isr(&s)==IRQ_HANDLED&&!live_refs&&flushed==1&&!forgotten&&delivered==2&&events==2);
 change=HEVC_SOURCE_CHANGE|HEVC_REFERENCE_CHANGE;live_refs=8;
 assert(codec_hevc_threaded_isr(&s)==IRQ_HANDLED&&!live_refs&&flushed==2&&!forgotten&&delivered==3&&events==3);
 /* A larger DPB alone keeps the existing reference geometry and pending pictures. */
 change=0;pool_count=6;live_refs=8;
 assert(codec_hevc_threaded_isr(&s)==IRQ_HANDLED&&live_refs==8&&setups==1&&segments==1&&events==3&&flushed==2&&!forgotten);
 return 0;}
'''
        run_c(code)

    def test_flush_releases_slots_and_clears_frame_pointers(self):
        source = (ROOT / "vdec/codec_hevc.c").read_text()
        body = source[source.index("static void codec_hevc_flush_output("):
                      source.index("/* Pictures referencing slots of another geometry")]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
typedef uint32_t u32;
#define kfree free
struct list_head {struct list_head *next,*prev;};
#define entry(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define list_for_each_entry_safe(p,n,h,m) \
 for(p=entry((h)->next,__typeof__(*p),m),n=entry(p->m.next,__typeof__(*p),m); \
     &p->m!=(h);p=n,n=entry(n->m.next,__typeof__(*n),m))
static void list_del(struct list_head *p){p->prev->next=p->next;p->next->prev=p->prev;}
struct hevc_frame {struct list_head list;u32 slot;};
struct codec_hevc {struct list_head ref_frames_list;struct hevc_frame *cur_frame,*col_frame;u32 frames_num;};
struct amvdec_session {void *priv;};
static u32 held,released;static int shown;
static struct hevc_frame *codec_hevc_show_pending(struct amvdec_session *s,bool hold){assert(!hold);((struct codec_hevc *)s->priv)->frames_num=0;shown++;return 0;}
static void amvdec_pool_release(struct amvdec_session *s,u32 slot){(void)s;assert(held&(1u<<slot));held&=~(1u<<slot);released|=1u<<slot;}
''' + body + r'''
int main(void){struct codec_hevc h={.frames_num=2};struct amvdec_session s={&h};
 h.ref_frames_list.next=h.ref_frames_list.prev=&h.ref_frames_list;
 for(u32 i=0;i<2;i++){
  struct hevc_frame *f=calloc(1,sizeof(*f));assert(f);f->slot=i;
  f->list.prev=h.ref_frames_list.prev;f->list.next=&h.ref_frames_list;h.ref_frames_list.prev->next=&f->list;h.ref_frames_list.prev=&f->list;
  held|=1u<<i;if(i==0)h.cur_frame=f;else h.col_frame=f;
 }
 codec_hevc_flush_output(&s);assert(!held&&released==3&&!h.cur_frame&&!h.col_frame&&!h.frames_num&&shown==1);
 assert(h.ref_frames_list.next==&h.ref_frames_list&&h.ref_frames_list.prev==&h.ref_frames_list);
 codec_hevc_flush_output(&s);assert(!held&&released==3&&shown==2);
 return 0;}
'''
        run_c(code)
