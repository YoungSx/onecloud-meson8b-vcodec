"""Replay HEVC random access through the real slice and IRQ control flow."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def section(source, start, end):
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


def run_c(code):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        (path / "test.c").write_text(code)
        subprocess.run(["cc", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-function", "-Wno-sign-compare",
                        "-Wno-unused-parameter", "-fsanitize=address,undefined",
                        "-fno-pie", "-no-pie", "-I", str(ROOT / "vdec"),
                        str(path / "test.c"), "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)


class HevcRandomAccessTests(unittest.TestCase):
    def test_cra_bla_leading_pictures_and_buffer_retry(self):
        source = (ROOT / "vdec/codec_hevc.c").read_text()
        types = section(source, "union rpm_param {", "/* A frame being decoded */")
        slices = section(source, "static void codec_hevc_update_pocs(",
                         "static void codec_hevc_fetch_rpm(")
        retry = section(source, "static void codec_hevc_capture_queued(",
                        "static int codec_hevc_setup_pool(")
        irq = section(source, "static void codec_hevc_resume(",
                      "static irqreturn_t codec_hevc_isr(")
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef int32_t s32;
typedef int irqreturn_t;
#define IRQ_HANDLED 1
#define RPM_SIZE 0x80
#define MAX_REF_ACTIVE 16
#define MAX_SLICE_NUM 800
#define INVALID_POC 0x80000000
#define SAO_VB_SIZE 0x30000
#define SAO_ABV_SIZE 0x30000
#define HEVC_WAIT_FLAG HEVC_ASSIST_SCRATCH_E
#define HEVC_DEC_STATUS_REG HEVC_ASSIST_SCRATCH_0
#define HEVC_CODED_SLICE_SEGMENT_DAT 5
#define HEVC_SLICE_SEGMENT_DONE 8
#define HEVC_NAL_SEARCH_DONE 9
#define HEVC_ACTION_DONE 0xff
#define HEVC_SOURCE_CHANGE 1
#define HEVC_REFERENCE_CHANGE 2
#define AMRISC_MAIN_REQ 4
#define BIT(n) (1u<<(n))
#define max(a,b) ((a)>(b)?(a):(b))
#define max_t(t,a,b) ((t)(a)>(t)(b)?(t)(a):(t)(b))
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define dev_dbg(...) ((void)0)
#define dev_err(...) ((void)0)
#include "hevc-sps.h"
#include "hevc-tiles.h"
#include "dos_regs.h"
#include "hevc_regs.h"
''' + types + r'''
enum amvdec_status {STATUS_STOPPED,STATUS_RUNNING,STATUS_NEEDS_RESUME};
struct hevc_frame {u32 poc,cur_slice_idx,cur_slice_type;};
struct codec_hevc {
 int lock;union rpm_param rpm_param;struct hevc_frame *cur_frame,*col_frame;
 u32 width,height,dst_width,dst_height,dpb_size,lcu_x_num,lcu_y_num,lcu_total,lcu_size,frames_num;
 u32 curr_poc,prev_tid0_poc,slice_segment_addr,slice_addr,random_access_poc;
 bool have_picture,skip_rasl,new_tile,waiting_for_buffer,client_waited,eos_seen;
 int is_10bit;struct hevc_sps_geometry sps,applied;struct hevc_tile_grid tiles;struct hevc_tile tile;
};
struct amvdec_core {void *dev,*dev_dec;};
struct amvdec_format {u32 max_width,max_height;};
struct amvdec_session {
 struct amvdec_core *core;void *priv;struct amvdec_format *fmt_out;
 u32 width,height,reference_width,reference_height,crop_left,crop_top,dpb_size;
 int streamon_cap,keyframe_found;enum amvdec_status status;
};
static int prepared,referenced,shown,refs,cols,mc,stages,drops,requests,aborts,changes,setups,allocation_error;
static u32 drop_offsets[8],registers[0x10000];static struct hevc_frame frame;
static void mutex_lock(int *p){assert(!*p);*p=1;}
static void mutex_unlock(int *p){assert(*p);*p=0;}
static u32 amvdec_read_dos(struct amvdec_core *c,u32 r){(void)c;return registers[r/4];}
static void amvdec_write_dos(struct amvdec_core *c,u32 r,u32 v){(void)c;registers[r/4]=v;requests++;}
static void amvdec_write_dos_bits(struct amvdec_core *c,u32 r,u32 v){amvdec_write_dos(c,r,registers[r/4]|v);}
static void amvdec_clear_dos_bits(struct amvdec_core *c,u32 r,u32 v){amvdec_write_dos(c,r,registers[r/4]&~v);}
static void amvdec_drop_picture_offset(struct amvdec_session *s,u32 offset){(void)s;assert(drops<8);drop_offsets[drops++]=offset;}
static void codec_hevc_update_referenced(struct codec_hevc *h){(void)h;referenced++;}
static void codec_hevc_show_frames(struct amvdec_session *s){(void)s;shown++;}
static struct hevc_frame *codec_hevc_prepare_new_frame(struct amvdec_session *s){
 if(allocation_error)return 0;
 prepared++;frame=(struct hevc_frame){.poc=((struct codec_hevc *)s->priv)->curr_poc};return &frame;
}
static int codec_hevc_update_frame_refs(struct amvdec_session *s,struct hevc_frame *f){(void)s;(void)f;refs++;return 0;}
static int codec_hevc_update_col_frame(struct codec_hevc *h){h->col_frame=h->cur_frame;cols++;return 0;}
static void codec_hevc_update_ldc_flag(struct codec_hevc *h){(void)h;}
static int codec_hevc_set_mc(struct amvdec_session *s,struct hevc_frame *f){(void)s;(void)f;mc++;return 0;}
static void codec_hevc_set_mcrcc(struct amvdec_session *s){(void)s;stages++;}
static void codec_hevc_set_mpred(struct amvdec_session *s,struct hevc_frame *f,struct hevc_frame *c){(void)s;(void)f;(void)c;stages++;}
static void codec_hevc_set_sao(struct amvdec_session *s,struct hevc_frame *f){(void)s;(void)f;stages++;}
static void amvdec_abort(struct amvdec_session *s){(void)s;aborts++;}
static u32 amvdec_pool_count(struct amvdec_session *s){(void)s;return 12;}
static int codec_hevc_setup_pool(struct amvdec_session *s){(void)s;setups++;return 0;}
static void codec_hevc_fetch_rpm(struct amvdec_session *s){(void)s;}
static struct hevc_frame *codec_hevc_show_pending(struct amvdec_session *s,bool hold){(void)s;assert(hold);return 0;}
static void codec_hevc_deliver_frame(struct amvdec_session *s,struct hevc_frame *f){(void)s;(void)f;assert(0);}
static void codec_hevc_forget_frames(struct codec_hevc *h){h->cur_frame=h->col_frame=0;}
static void amvdec_src_change(struct amvdec_session *s,u32 w,u32 h,u32 n){changes++;s->width=w;s->height=h;s->dpb_size=n;}
static void codec_hevc_flush_output(struct amvdec_session *s){struct codec_hevc *h=s->priv;assert(!h->frames_num);h->cur_frame=h->col_frame=0;}
static void codec_hevc_finish_eos(struct amvdec_session *s){(void)s;assert(0);}
static void codec_hevc_clear_inputs(struct codec_hevc *h){(void)h;assert(0);}
static bool codec_hevc_geometry_for_offset(struct codec_hevc *h,u32 o,struct hevc_sps_geometry *g){
 (void)o;*g=(struct hevc_sps_geometry){.coded_width=h->rpm_param.p.pic_width_in_luma_samples,.coded_height=h->rpm_param.p.pic_height_in_luma_samples,
 .width=h->rpm_param.p.pic_width_in_luma_samples,.height=h->rpm_param.p.pic_height_in_luma_samples};return true;
}
''' + slices + retry + irq + r'''
static void init(struct codec_hevc *h){
 *h=(struct codec_hevc){.curr_poc=INVALID_POC,.width=704,.height=576,.dst_width=704,.dst_height=576,.dpb_size=6,
                      .lcu_size=64,.lcu_x_num=11,.lcu_y_num=9,.lcu_total=99};
 h->rpm_param.p.pic_width_in_luma_samples=704;h->rpm_param.p.pic_height_in_luma_samples=576;
 h->rpm_param.p.log2_diff_max_min_coding_block_size=3;h->rpm_param.p.m_temporalId=1;
 memset(registers,0,sizeof(registers));prepared=referenced=shown=refs=cols=mc=stages=drops=requests=aborts=changes=setups=allocation_error=0;
}
static void picture(struct amvdec_session *s,u32 type,u32 lsb,u32 address,u32 offset){
 struct codec_hevc *h=s->priv;h->rpm_param.p.m_nalUnitType=type;h->rpm_param.p.POClsb=lsb;
 h->rpm_param.p.slice_segment_address=address;h->rpm_param.p.first_slice_segment_in_pic_flag=!address;
 h->rpm_param.p.slice_type=type>=16&&type<=21?I_SLICE:B_SLICE;
 registers[HEVC_SHIFT_BYTE_COUNT/4]=offset;registers[HEVC_DEC_STATUS_REG/4]=HEVC_SLICE_SEGMENT_DONE;
 assert(codec_hevc_threaded_isr(s)==IRQ_HANDLED);
}
static void skipped(struct amvdec_session *s,u32 type,u32 lsb,u32 address,u32 offset){
 struct codec_hevc *h=s->priv;struct hevc_frame *current=h->cur_frame;
 int p=prepared,r=referenced,sh=shown,rf=refs,c=cols,m=mc,st=stages,d=drops,q=requests;
 registers[HEVC_WAIT_FLAG/4]=3;picture(s,type,lsb,address,offset);
 assert(!aborts&&!changes&&prepared==p&&referenced==r&&shown==sh&&refs==rf&&cols==c&&mc==m&&stages==st);
 assert(h->cur_frame==current&&requests==q+3&&registers[HEVC_WAIT_FLAG/4]==1);
 assert(registers[HEVC_DEC_STATUS_REG/4]==0xff&&registers[HEVC_MCPU_INTR_REQ/4]==4);
 assert(drops==d+!address);if(!address)assert(drop_offsets[d]==offset);
}
int main(void){
 struct codec_hevc h;struct amvdec_core c={0};struct amvdec_format f={1920,1088};
 struct amvdec_session s={.core=&c,.priv=&h,.fmt_out=&f,.width=704,.height=576,.streamon_cap=1,.status=STATUS_RUNNING};
 /* A fresh CRA can have a signed POC; its RASL pictures need unavailable old refs. */
 init(&h);picture(&s,21,14,0,100);assert((s32)h.curr_poc==-2&&prepared==1);
 skipped(&s,8,13,0,200);assert((s32)h.curr_poc==-3);
 /* Discarded slices must not change CAPTURE geometry or request a new pool. */
 h.rpm_param.p.pic_width_in_luma_samples=512;h.rpm_param.p.pic_height_in_luma_samples=288;
 h.rpm_param.p.dependent_slice_segment_flag=1;skipped(&s,8,13,1,220);
 assert(h.width==704&&h.height==576&&!setups);h.rpm_param.p.dependent_slice_segment_flag=0;
 skipped(&s,9,12,0,300);assert(h.width==704&&h.height==576&&!setups);
 h.rpm_param.p.pic_width_in_luma_samples=704;h.rpm_param.p.pic_height_in_luma_samples=576;
 picture(&s,6,13,0,400);assert(prepared==2&&drops==2); /* RADL still decodes. */
 picture(&s,1,1,0,500);assert(prepared==3&&h.curr_poc==1&&!aborts);
 /* A CRA encountered during continuous decoding retains its leading pictures. */
 init(&h);picture(&s,19,0,0,100);picture(&s,1,6,0,200);picture(&s,21,8,0,300);
 assert(!h.skip_rasl);picture(&s,9,7,0,400);assert(prepared==4&&!drops&&!aborts);
 for(u32 type=16;type<=18;type++){picture(&s,type,14,0,500);assert(h.skip_rasl&&h.random_access_poc==14);skipped(&s,8,13,0,600);}
 picture(&s,20,0,0,700);assert(!h.skip_rasl);
 /* CAPTURE retry must not parse the first CRA header twice and clear its guard. */
 init(&h);allocation_error=1;picture(&s,21,6,0,100);assert(h.waiting_for_buffer&&h.skip_rasl&&!prepared);
 allocation_error=0;codec_hevc_capture_queued(&s);assert(!h.waiting_for_buffer&&h.skip_rasl&&prepared==1);
 skipped(&s,9,5,0,200);
 /* Resolution negotiation also resumes the already-parsed CRA header. */
 init(&h);h.width=h.height=h.dst_width=h.dst_height=0;picture(&s,21,6,0,100);
 assert(changes==1&&!prepared&&h.skip_rasl);codec_hevc_resume(&s);assert(prepared==1&&h.skip_rasl);
 changes=0;skipped(&s,8,5,0,200);
 /* Field validation still precedes state updates and firmware skip requests. */
 h.rpm_param.p.log2_max_pic_order_cnt_lsb_minus4=32;int q=requests,d=drops;u32 poc=h.curr_poc;
 picture(&s,8,4,0,300);assert(aborts==1&&requests==q&&drops==d&&h.curr_poc==poc);
 return 0;}
'''
        run_c(code)

    def test_discard_consumes_only_its_offset_timestamp(self):
        source = (ROOT / "vdec/vdec_helpers.c").read_text()
        take = section(source, "static bool amvdec_take_offset_ts(",
                       "static void dst_buf_done(")
        drop = section(source, "void amvdec_drop_picture_offset(",
                       "void amvdec_dst_buf_done_idx(")
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
typedef uint32_t u32;typedef uint64_t u64;
#define EXPORT_SYMBOL_GPL(x)
#define kfree free
struct list_head {struct list_head *next,*prev;};
#define entry(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define list_for_each_entry_safe(p,n,h,m) \
 for(p=entry((h)->next,__typeof__(*p),m),n=entry(p->m.next,__typeof__(*p),m); \
     &p->m!=(h);p=n,n=entry(n->m.next,__typeof__(*n),m))
static void list_del(struct list_head *p){p->prev->next=p->next;p->next->prev=p->prev;}
struct amvdec_timestamp {struct list_head list;u32 offset,used_count,flags;u64 ts;int tc;};
struct amvdec_picture {u64 timestamp;u32 flags;int timecode;};
struct amvdec_session {struct list_head timestamps;int ts_spinlock,esparser_queued_bufs,esparser_queue_work;};
#define spin_lock_irqsave(p,f) do{assert(!*(p));*(p)=1;(f)=0;}while(0)
#define spin_unlock_irqrestore(p,f) do{assert(*(p)&&!(f));*(p)=0;}while(0)
static int scheduled;
static void atomic_dec(int *p){assert(*p>0);(*p)--;}
static void schedule_work(int *p){(void)p;scheduled++;}
''' + take + drop + r'''
static void add(struct amvdec_session *s,u32 offset,u64 ts){
 struct amvdec_timestamp *t=calloc(1,sizeof(*t));assert(t);t->offset=offset;t->ts=ts;
 t->list.prev=s->timestamps.prev;t->list.next=&s->timestamps;s->timestamps.prev->next=&t->list;s->timestamps.prev=&t->list;
 s->esparser_queued_bufs++;
}
int main(void){struct amvdec_session s={0};struct amvdec_picture p={0};
 s.timestamps.next=s.timestamps.prev=&s.timestamps;
 add(&s,100,777);add(&s,200,2000);add(&s,300,3000);add(&s,400,777);
 amvdec_drop_picture_offset(&s,50);assert(s.esparser_queued_bufs==4&&scheduled==1);
 amvdec_drop_picture_offset(&s,212);assert(s.esparser_queued_bufs==3&&scheduled==2);
 amvdec_drop_picture_offset(&s,320);assert(s.esparser_queued_bufs==2&&scheduled==3);
 /* An old reordered picture and a later packet keep their own records, even with equal PTS. */
 assert(amvdec_take_offset_ts(&s,100,&p)&&p.timestamp==777);atomic_dec(&s.esparser_queued_bufs);
 assert(amvdec_take_offset_ts(&s,400,&p)&&p.timestamp==777);atomic_dec(&s.esparser_queued_bufs);
 assert(s.timestamps.next==&s.timestamps&&s.timestamps.prev==&s.timestamps);
 amvdec_drop_picture_offset(&s,500);assert(!s.esparser_queued_bufs&&scheduled==4&&!s.ts_spinlock);
 return 0;}
'''
        run_c(code)
