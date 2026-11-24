"""HEVC pictures can be references without being returned on CAPTURE."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HevcOutputTests(unittest.TestCase):
    def test_irap_discards_only_pending_output_once(self):
        source = (ROOT / "vdec/codec_hevc.c").read_text()
        body = source[source.index("static bool codec_hevc_is_output_boundary("):
                      source.index("/* Pictures referencing slots of another geometry")]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef uint32_t u32;
#define NAL_UNIT_CODED_SLICE_BLA 16
#define NAL_UNIT_CODED_SLICE_IDR_N_LP 20
#define NAL_UNIT_CODED_SLICE_CRA 21
struct list_head {struct list_head *next,*prev;};
#define entry(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define list_for_each_entry(p,h,m) \
 for(p=entry((h)->next,__typeof__(*p),m);&p->m!=(h);p=entry(p->m.next,__typeof__(*p),m))
struct hevc_frame {struct list_head list;u32 offset;int show;};
struct codec_hevc {struct {struct {u32 m_nalUnitType,slice_segment_address;}p;}rpm_param;bool skip_rasl,picture_no_prior;u32 frames_num;struct list_head ref_frames_list;};
struct amvdec_session {void *priv;};
static int drops;static u32 dropped[4];
static void amvdec_drop_picture_offset(struct amvdec_session *s,u32 o){(void)s;assert(drops<4);dropped[drops++]=o;}
''' + body + r'''
int main(void){struct codec_hevc h={0};struct amvdec_session s={&h};struct hevc_frame frames[3]={0};
 h.ref_frames_list.next=h.ref_frames_list.prev=&h.ref_frames_list;
 for(int i=0;i<3;i++){frames[i].offset=100+i*10;frames[i].list.prev=h.ref_frames_list.prev;frames[i].list.next=&h.ref_frames_list;h.ref_frames_list.prev->next=&frames[i].list;h.ref_frames_list.prev=&frames[i].list;}
 frames[0].show=frames[2].show=1;h.frames_num=2;h.picture_no_prior=true;h.rpm_param.p.m_nalUnitType=19;
 codec_hevc_discard_prior_output(&s);assert(drops==2&&!h.frames_num&&dropped[0]==100&&dropped[1]==120);
 /* A second evaluation after CAPTURE retry cannot drop timestamps twice. */
 codec_hevc_discard_prior_output(&s);assert(drops==2&&!h.frames_num);
 frames[0].show=frames[2].show=1;h.frames_num=2;h.picture_no_prior=false;
 codec_hevc_discard_prior_output(&s);assert(drops==2&&h.frames_num==2);
 frames[0].show=1;frames[2].show=0;h.frames_num=1;h.picture_no_prior=true;h.rpm_param.p.m_nalUnitType=21;
 codec_hevc_discard_prior_output(&s);assert(drops==2&&h.frames_num==1);
 h.skip_rasl=true;h.rpm_param.p.slice_segment_address=1;
 codec_hevc_discard_prior_output(&s);assert(drops==2&&h.frames_num==1);
 h.rpm_param.p.slice_segment_address=0;codec_hevc_discard_prior_output(&s);
 assert(drops==3&&dropped[2]==100&&!h.frames_num);
 return 0;}
'''
        self.run_c(code)

    def test_hidden_reference_keeps_slot_without_counting_as_output(self):
        source = (ROOT / "vdec/codec_hevc.c").read_text()
        body = source[source.index("static struct hevc_frame *\ncodec_hevc_prepare_new_frame("):
                      source.index("static void\ncodec_hevc_set_sao(")]
        output = source[source.index("static struct hevc_frame *\ncodec_hevc_get_next_ready_frame("):
                        source.index("static int\ncodec_hevc_setup_workspace(")]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <limits.h>
typedef uint32_t u32;typedef int32_t s32;
#define GFP_KERNEL 0
#define kzalloc(n,f) calloc(1,n)
#define kfree free
#define dev_dbg(...) ((void)0)
#include "hevc_regs.h"
struct list_head {struct list_head *next,*prev;};
#define entry(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define list_for_each_entry(p,h,m) \
 for(p=entry((h)->next,__typeof__(*p),m);&p->m!=(h);p=entry(p->m.next,__typeof__(*p),m))
#define list_for_each_entry_safe(p,n,h,m) \
 for(p=entry((h)->next,__typeof__(*p),m),n=entry(p->m.next,__typeof__(*p),m); \
     &p->m!=(h);p=n,n=entry(n->m.next,__typeof__(*n),m))
static void list_del(struct list_head *p){p->prev->next=p->next;p->next->prev=p->prev;}
static void list_add_tail(struct list_head *p,struct list_head *h){p->prev=h->prev;p->next=h;h->prev->next=p;h->prev=p;}
struct amvdec_geometry {u32 width,height,reference_width,reference_height,crop_left,crop_top;};
struct hevc_frame {struct list_head list;u32 slot,poc,cur_slice_type,num_reorder_pic,offset;int referenced,show;struct amvdec_geometry geo;};
union rpm_param {struct {u32 slice_type,sps_num_reorder_pics_0;}p;};
struct codec_hevc {union rpm_param rpm_param;u32 curr_poc,frames_num;struct list_head ref_frames_list;bool picture_output;};
struct amvdec_core {void *dev;};struct amvdec_session {void *priv;struct amvdec_core *core;u32 width,height,reference_width,reference_height,crop_left,crop_top;};
static int pool_result=1,drops;static u32 dropped;
static int delivered,released;
static int amvdec_pool_take(struct amvdec_session *s){(void)s;return pool_result;}
static u32 amvdec_read_dos(struct amvdec_core *c,u32 reg){(void)c;assert(reg==HEVC_SHIFT_BYTE_COUNT);return 123;}
static void amvdec_drop_picture_offset(struct amvdec_session *s,u32 offset){(void)s;dropped=offset;drops++;}
static void amvdec_pool_release(struct amvdec_session *s,u32 slot){(void)s;assert(slot==1||slot==2);released++;}
''' + output + body + r'''
static void codec_hevc_deliver_frame(struct amvdec_session *s,struct hevc_frame *f){(void)s;assert(f->slot==2);delivered++;}
int main(void){struct codec_hevc h={.curr_poc=7};struct amvdec_core c={0};struct amvdec_session s={.priv=&h,.core=&c,.width=320,.height=192};
 h.ref_frames_list.next=h.ref_frames_list.prev=&h.ref_frames_list;
 struct hevc_frame *hidden=codec_hevc_prepare_new_frame(&s);
 assert(hidden&&hidden->referenced&&!hidden->show&&!h.frames_num&&drops==1&&dropped==123);
 h.picture_output=true;pool_result=2;struct hevc_frame *visible=codec_hevc_prepare_new_frame(&s);
 assert(visible&&visible->referenced&&visible->show&&h.frames_num==1&&drops==1);
 pool_result=-1;h.picture_output=false;assert(!codec_hevc_prepare_new_frame(&s)&&drops==1&&h.frames_num==1);
 codec_hevc_show_frames(&s);assert(delivered==1&&!released&&!h.frames_num&&hidden->referenced&&!hidden->show);
 hidden->referenced=visible->referenced=0;codec_hevc_show_frames(&s);
 assert(delivered==1&&released==2&&!h.frames_num&&drops==1);
 assert(h.ref_frames_list.next==&h.ref_frames_list&&h.ref_frames_list.prev==&h.ref_frames_list);
 return 0;}
'''
        self.run_c(code)

    def test_irap_completes_previous_picture_before_discarding_reorder_tail(self):
        source = (ROOT / "vdec/codec_hevc.c").read_text()
        pending = source[source.index("static struct hevc_frame *\ncodec_hevc_get_next_ready_frame("):
                         source.index("static int\ncodec_hevc_setup_workspace(")]
        flush = source[source.index("static struct hevc_frame *codec_hevc_show_pending("):
                       source.index("/* Pictures referencing slots of another geometry")]
        prepare = source[source.index("static struct hevc_frame *\ncodec_hevc_prepare_new_frame("):
                         source.index("static void\ncodec_hevc_set_sao(")]
        segment = source[source.index("static int codec_hevc_process_segment("):
                         source.index("static int codec_hevc_process_rpm(")]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef int32_t s32;
#define BIT(n) (1u<<(n))
#define GFP_KERNEL 0
#define kzalloc(n,f) calloc(1,n)
#define kfree free
#define dev_dbg(...) ((void)0)
#define NAL_UNIT_CODED_SLICE_BLA 16
#define NAL_UNIT_CODED_SLICE_IDR_N_LP 20
#define NAL_UNIT_CODED_SLICE_CRA 21
#define MAX_SLICE_NUM 800
#define HEVC_WAIT_FLAG HEVC_ASSIST_SCRATCH_E
#define HEVC_DEC_STATUS_REG HEVC_ASSIST_SCRATCH_0
#define HEVC_CODED_SLICE_SEGMENT_DAT 5
#define HEVC_ACTION_DONE 255
#define AMRISC_MAIN_REQ 4
#include "hevc_regs.h"
#include "hevc-tiles.h"
struct list_head {struct list_head *next,*prev;};
#define entry(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define list_for_each_entry(p,h,m) \
 for(p=entry((h)->next,__typeof__(*p),m);&p->m!=(h);p=entry(p->m.next,__typeof__(*p),m))
#define list_for_each_entry_safe(p,n,h,m) \
 for(p=entry((h)->next,__typeof__(*p),m),n=entry(p->m.next,__typeof__(*p),m); \
     &p->m!=(h);p=n,n=entry(n->m.next,__typeof__(*n),m))
static void list_del(struct list_head *p){p->prev->next=p->next;p->next->prev=p->prev;}
static void list_add_tail(struct list_head *p,struct list_head *h){p->prev=h->prev;p->next=h;h->prev->next=p;h->prev=p;}
struct amvdec_geometry {u32 width,height,reference_width,reference_height,crop_left,crop_top;};
struct hevc_frame {struct list_head list;u32 slot,poc,cur_slice_type,cur_slice_idx,num_reorder_pic,offset;int referenced,show;struct amvdec_geometry geo;};
union rpm_param {struct {u32 slice_type,sps_num_reorder_pics_0,slice_segment_address,m_nalUnitType;}p;};
struct codec_hevc {union rpm_param rpm_param;u32 curr_poc,frames_num;struct list_head ref_frames_list;bool picture_output,picture_no_prior,skip_rasl,new_tile;struct hevc_frame *cur_frame,*col_frame;struct hevc_tile_grid tiles;struct hevc_tile tile;};
struct amvdec_core {void *dev;};struct amvdec_session {void *priv;struct amvdec_core *core;u32 width,height,reference_width,reference_height,crop_left,crop_top;};
static u32 held,offset,output_offsets[64],discarded_offsets[16];static int delivered,discarded;
static int amvdec_pool_take(struct amvdec_session *s){(void)s;for(int i=0;i<16;i++)if(!(held&(1u<<i))){held|=1u<<i;return i;}return -1;}
static void amvdec_pool_release(struct amvdec_session *s,u32 slot){(void)s;assert(held&(1u<<slot));held&=~(1u<<slot);}
static u32 amvdec_read_dos(struct amvdec_core *c,u32 r){(void)c;assert(r==HEVC_SHIFT_BYTE_COUNT);return offset;}
static void amvdec_drop_picture_offset(struct amvdec_session *s,u32 o){(void)s;assert(discarded<16);discarded_offsets[discarded++]=o;}
static void codec_hevc_deliver_frame(struct amvdec_session *s,struct hevc_frame *f){(void)s;assert(delivered<64);output_offsets[delivered++]=f->offset;}
static bool codec_hevc_is_skipped_rasl(struct codec_hevc *h){(void)h;return false;}
static void codec_hevc_update_referenced(struct codec_hevc *h){struct hevc_frame *f;list_for_each_entry(f,&h->ref_frames_list,list)f->referenced=0;}
static int codec_hevc_update_frame_refs(struct amvdec_session *s,struct hevc_frame *f){(void)s;(void)f;return 0;}
static int codec_hevc_update_col_frame(struct codec_hevc *h){h->col_frame=h->cur_frame;return 0;}
static void codec_hevc_update_ldc_flag(struct codec_hevc *h){(void)h;}
static int codec_hevc_set_mc(struct amvdec_session *s,struct hevc_frame *f){(void)s;(void)f;return 0;}
static void codec_hevc_set_mcrcc(struct amvdec_session *s){(void)s;}
static void codec_hevc_set_mpred(struct amvdec_session *s,struct hevc_frame *f,struct hevc_frame *c){(void)s;(void)f;(void)c;}
static void codec_hevc_set_sao(struct amvdec_session *s,struct hevc_frame *f){(void)s;(void)f;}
static void amvdec_clear_dos_bits(struct amvdec_core *c,u32 r,u32 v){(void)c;(void)r;(void)v;assert(0);}
static void amvdec_write_dos_bits(struct amvdec_core *c,u32 r,u32 v){(void)c;(void)r;(void)v;}
static void amvdec_write_dos(struct amvdec_core *c,u32 r,u32 v){(void)c;(void)r;(void)v;}
''' + pending + flush + prepare + segment + r'''
int main(void){struct codec_hevc h={.picture_output=true};struct amvdec_core c={0};struct amvdec_session s={.priv=&h,.core=&c,.width=320,.height=192};
 h.ref_frames_list.next=h.ref_frames_list.prev=&h.ref_frames_list;
 u16 widths[4]={0},heights[4]={0};assert(!hevc_tile_grid_init(&h.tiles,5,3,0,0,0,widths,heights));
 h.rpm_param.p.sps_num_reorder_pics_0=3;
 for(u32 i=0;i<50;i++){
  offset=100+i*100;h.curr_poc=i%25;h.rpm_param.p.m_nalUnitType=i%25?1:19;
  h.rpm_param.p.slice_type=i%25?1:2;h.picture_no_prior=i==25;
  assert(!codec_hevc_process_segment(&s));
 }
 codec_hevc_flush_output(&s);
 assert(delivered==47&&discarded==3&&!held&&!h.frames_num&&!h.cur_frame&&!h.col_frame);
 for(u32 i=0;i<22;i++)assert(output_offsets[i]==100+i*100);
 for(u32 i=0;i<3;i++)assert(discarded_offsets[i]==100+(22+i)*100);
 for(u32 i=22;i<47;i++)assert(output_offsets[i]==100+(i+3)*100);
 return 0;}
'''
        self.run_c(code)

    @staticmethod
    def run_c(code):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(["cc", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-function", "-fsanitize=address,undefined",
                            "-fno-pie", "-no-pie", "-I", str(ROOT / "vdec"),
                            str(path / "test.c"), "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)
