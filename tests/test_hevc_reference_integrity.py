"""A missing HEVC picture cannot compact or alias a hardware reference list."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HevcReferenceIntegrityTests(unittest.TestCase):
    def test_complete_reference_lists_and_collocated_picture(self):
        source = (ROOT / "vdec/codec_hevc.c").read_text()
        start = source.index("static void codec_hevc_set_ref_list(") if "static void codec_hevc_set_ref_list(" in source else source.index("static int codec_hevc_resolve_ref_list(")
        body = source[start:source.index("static void codec_hevc_update_pocs(")]
        old_mc = "static void codec_hevc_set_mc(" in body
        old_col = "static void codec_hevc_update_col_frame(" in body
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
typedef uint32_t u32;typedef int32_t s32;
#define MAX_REF_ACTIVE 16
#define I_SLICE 2
#define P_SLICE 1
#define B_SLICE 0
#define INVALID_POC 0x80000000
#define BIT(n) (1u<<(n))
#define dev_warn(...) ((void)0)
#define dev_err(...) ((void)0)
#include "dos_regs.h"
#include "hevc_regs.h"
struct hevc_frame {u32 poc,slot,cur_slice_type,ref_num[2],ref_poc_list[2][16];int referenced;};
union rpm_param {struct {u32 collocated_ref_idx,collocated_from_l0_flag,slice_temporal_mvp_enable_flag;}p;};
struct codec_hevc {struct hevc_frame *cur_frame,*col_frame;u32 col_poc;union rpm_param rpm_param;};
struct amvdec_core {void *dev;};struct amvdec_session {struct amvdec_core *core;void *priv;};
static struct hevc_frame current={.poc=12,.slot=0,.referenced=1},refs[2]={{.poc=10,.slot=1,.referenced=1},{.poc=11,.slot=2,.referenced=1}};
static int missing=-1,writes;static u32 values[64],registers[64],pool_count=12;
static u32 amvdec_pool_count(struct amvdec_session *s){(void)s;return pool_count;}
static struct hevc_frame *codec_hevc_get_frame_by_poc(struct codec_hevc *h,u32 poc){
 (void)h;if(poc==current.poc)return &current;
 for(int i=0;i<2;i++)if(i!=missing&&refs[i].poc==poc)return &refs[i];return 0;
}
static void amvdec_write_dos(struct amvdec_core *c,u32 reg,u32 value){(void)c;assert(writes<64);registers[writes]=reg;values[writes++]=value;}
''' + body + "\n"
        code += "static int mc(struct amvdec_session *s,struct hevc_frame *f){" + (
            "codec_hevc_set_mc(s,f);return 0;}\n" if old_mc else "return codec_hevc_set_mc(s,f);}\n")
        code += "static int col(struct codec_hevc *h){" + (
            "codec_hevc_update_col_frame(h);return 0;}\n" if old_col else "return codec_hevc_update_col_frame(h);}\n")
        code += r'''
int main(void){struct amvdec_core core={0};struct codec_hevc h={.cur_frame=&current};struct amvdec_session s={&core,&h};
 current.cur_slice_type=P_SLICE;current.ref_num[0]=2;current.ref_poc_list[0][0]=10;current.ref_poc_list[0][1]=11;
 assert(!mc(&s,&current)&&writes==3);assert(values[1]==0x030302&&values[2]==0x050504);
 for(int i=0;i<2;i++){missing=i;writes=0;assert(mc(&s,&current)==-EINVAL&&!writes);}missing=-1;
 current.cur_slice_type=B_SLICE;current.ref_num[1]=2;current.ref_poc_list[1][0]=11;current.ref_poc_list[1][1]=10;
 writes=0;assert(!mc(&s,&current)&&writes==6);assert(values[4]==0x050504&&values[5]==0x030302);
 current.ref_poc_list[1][1]=99;writes=0;assert(mc(&s,&current)==-EINVAL&&!writes);current.ref_poc_list[1][1]=10;
 refs[0].referenced=0;assert(mc(&s,&current)==-EINVAL&&!writes);refs[0].referenced=1;
 refs[0].slot=12;assert(mc(&s,&current)==-EINVAL&&!writes);refs[0].slot=1;
 current.ref_poc_list[0][0]=12;assert(mc(&s,&current)==-EINVAL&&!writes);current.ref_poc_list[0][0]=10;
 current.ref_num[0]=0;assert(mc(&s,&current)==-EINVAL&&!writes);current.ref_num[0]=2;
 current.cur_slice_type=I_SLICE;h.col_frame=(struct hevc_frame *)(uintptr_t)1;
 assert(!col(&h)&&h.col_frame==&current&&h.col_poc==INVALID_POC);
 writes=0;assert(!mc(&s,&current)&&!writes);
 current.cur_slice_type=P_SLICE;h.rpm_param.p.slice_temporal_mvp_enable_flag=1;h.rpm_param.p.collocated_ref_idx=1;
 assert(!col(&h)&&h.col_frame==&refs[1]&&h.col_poc==11);
 h.rpm_param.p.collocated_ref_idx=2;assert(col(&h)==-EINVAL);
 h.rpm_param.p.slice_temporal_mvp_enable_flag=0;assert(!col(&h)&&h.col_frame==&current);
 h.rpm_param.p.slice_temporal_mvp_enable_flag=1;h.rpm_param.p.collocated_ref_idx=1;missing=1;
 assert(col(&h)==-EINVAL);missing=-1;
 return 0;}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(["cc", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-Wno-sign-compare", "-Wno-unused-function", "-Wno-misleading-indentation",
                            "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            "-I", str(ROOT / "vdec"), str(path / "test.c"),
                            "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)


    def test_segment_stops_before_hardware_and_tracks_current_slice_type(self):
        source = (ROOT / "vdec/codec_hevc.c").read_text()
        body = source[source.index("static int codec_hevc_process_segment("):
                      source.index("static int codec_hevc_process_rpm(")]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
typedef uint32_t u32;typedef uint16_t u16;
#define MAX_SLICE_NUM 800
#define BIT(n) (1u<<(n))
#define AMRISC_MAIN_REQ 4
#define HEVC_WAIT_FLAG HEVC_ASSIST_SCRATCH_E
#define HEVC_DEC_STATUS_REG HEVC_ASSIST_SCRATCH_0
#define HEVC_CODED_SLICE_SEGMENT_DAT 5
#define HEVC_ACTION_DONE 0xff
#include "hevc-tiles.h"
#include "dos_regs.h"
#include "hevc_regs.h"
struct hevc_frame {u32 cur_slice_idx,cur_slice_type;};
union rpm_param {struct {u32 slice_segment_address,slice_type;}p;};
struct codec_hevc {union rpm_param rpm_param;struct hevc_tile_grid tiles;struct hevc_tile tile;bool new_tile;struct hevc_frame *cur_frame,*col_frame;};
struct amvdec_core {int unused;};struct amvdec_session {struct amvdec_core *core;void *priv;};
static int ref_error,col_error,mc_error,programmed,requests;
static bool codec_hevc_is_output_boundary(struct codec_hevc *h){(void)h;return false;}
static void codec_hevc_discard_prior_output(struct amvdec_session *s){(void)s;assert(0);}
static void codec_hevc_flush_output(struct amvdec_session *s){(void)s;assert(0);}
static bool codec_hevc_is_skipped_rasl(struct codec_hevc *h){(void)h;return false;}
static void amvdec_drop_picture_offset(struct amvdec_session *s,u32 o){(void)s;(void)o;assert(0);}
static u32 amvdec_read_dos(struct amvdec_core *c,u32 r){(void)c;(void)r;assert(0);return 0;}
static void amvdec_clear_dos_bits(struct amvdec_core *c,u32 r,u32 v){(void)c;(void)r;(void)v;assert(0);}
static void codec_hevc_update_referenced(struct codec_hevc *h){(void)h;assert(0);}
static void codec_hevc_show_frames(struct amvdec_session *s){(void)s;assert(0);}
static struct hevc_frame *codec_hevc_prepare_new_frame(struct amvdec_session *s){(void)s;assert(0);return 0;}
static int codec_hevc_update_frame_refs(struct amvdec_session *s,struct hevc_frame *f){struct codec_hevc *h=s->priv;assert(f->cur_slice_type==h->rpm_param.p.slice_type);return ref_error;}
static int codec_hevc_update_col_frame(struct codec_hevc *h){(void)h;return col_error;}
static void codec_hevc_update_ldc_flag(struct codec_hevc *h){(void)h;}
static int codec_hevc_set_mc(struct amvdec_session *s,struct hevc_frame *f){(void)s;(void)f;return mc_error;}
static void codec_hevc_set_mcrcc(struct amvdec_session *s){(void)s;programmed++;}
static void codec_hevc_set_mpred(struct amvdec_session *s,struct hevc_frame *f,struct hevc_frame *c){(void)s;(void)f;(void)c;programmed++;}
static void codec_hevc_set_sao(struct amvdec_session *s,struct hevc_frame *f){(void)s;(void)f;programmed++;}
static void amvdec_write_dos_bits(struct amvdec_core *c,u32 r,u32 v){(void)c;(void)r;(void)v;requests++;}
static void amvdec_write_dos(struct amvdec_core *c,u32 r,u32 v){(void)c;(void)r;(void)v;requests++;}
''' + body + r'''
int main(void){
 struct hevc_frame frame={.cur_slice_type=2};struct codec_hevc h={.cur_frame=&frame,.col_frame=&frame};struct amvdec_core c={0};struct amvdec_session s={&c,&h};
 u16 widths[4]={0},heights[4]={0};assert(!hevc_tile_grid_init(&h.tiles,10,10,0,0,0,widths,heights));
 h.rpm_param.p.slice_segment_address=1;h.rpm_param.p.slice_type=1;
 col_error=-EINVAL;assert(codec_hevc_process_segment(&s)==-EINVAL&&!programmed&&!requests);
 col_error=0;mc_error=-EINVAL;assert(codec_hevc_process_segment(&s)==-EINVAL&&!programmed&&!requests);
 mc_error=0;ref_error=-EINVAL;assert(codec_hevc_process_segment(&s)==-EINVAL&&!programmed&&!requests);
 ref_error=0;assert(!codec_hevc_process_segment(&s)&&frame.cur_slice_type==1&&programmed==3&&requests==3);
 h.rpm_param.p.slice_type=0;assert(!codec_hevc_process_segment(&s)&&frame.cur_slice_type==0&&programmed==6&&requests==6);
 return 0;}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(["cc", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            "-I", str(ROOT / "vdec"), str(path / "test.c"),
                            "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)
