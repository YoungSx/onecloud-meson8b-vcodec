"""Run the real HEVC RPS, POC and low-delay functions with firmware inputs."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HevcReferenceStateTests(unittest.TestCase):
    def test_signed_references_and_tid0_anchor(self):
        source = (ROOT / "vdec/codec_hevc.c").read_text()
        enums = source[source.index("enum nal_unit_type {"):
                       source.index("struct hevc_frame {")]
        refs = source[source.index("static int codec_hevc_update_frame_refs("):
                      source.index('/* Tag "old" frames that are no longer referenced */')]
        pocs = source[source.index("static void codec_hevc_update_pocs("):
                      source.index("static void codec_hevc_process_segment_header(")]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
typedef uint16_t u16;typedef uint32_t u32;typedef int32_t s32;
#define MAX_REF_ACTIVE 16
#define RPS_USED_BIT 14
#define dev_dbg(...) ((void)0)
''' + enums + r'''
union rpm_param {struct {u16 CUR_RPS[16],num_ref_idx_l0_active,num_ref_idx_l1_active,
 slice_type,modification_flag,modification_list[32],m_nalUnitType,m_temporalId,
 log2_max_pic_order_cnt_lsb_minus4,POClsb;}p;};
struct hevc_frame {u32 poc,ref_poc_list[2][16],ref_num[2],cur_slice_idx,cur_slice_type;};
struct codec_hevc {union rpm_param rpm_param;struct hevc_frame *cur_frame;u32 curr_poc,prev_tid0_poc,ldc_flag;};
struct amvdec_core {void *dev;};
struct amvdec_session {struct amvdec_core *core;void *priv;};
''' + refs + pocs + r'''
static void picture(struct amvdec_session *s,u32 type,u32 tid,u32 lsb){
 struct codec_hevc *h=s->priv;
 h->rpm_param.p.m_nalUnitType=type;h->rpm_param.p.m_temporalId=tid;h->rpm_param.p.POClsb=lsb;
 codec_hevc_update_pocs(s);
}
int main(int argc,char **argv){
 assert(argc==2);
 struct amvdec_core core={0};struct hevc_frame frame={0};struct codec_hevc h={.cur_frame=&frame};struct amvdec_session s={&core,&h};
 if(!strcmp(argv[1],"ldc")){
 h.rpm_param.p.slice_type=frame.cur_slice_type=P_SLICE;
 h.rpm_param.p.num_ref_idx_l0_active=1;h.rpm_param.p.CUR_RPS[0]=0x7fff;h.rpm_param.p.CUR_RPS[1]=0x8000;
 assert(!codec_hevc_update_frame_refs(&s,&frame));
 assert(frame.ref_poc_list[0][0]==UINT32_MAX);
 codec_hevc_update_ldc_flag(&h);assert(h.ldc_flag==1);
 /* A future POC crossing zero is still a future reference. */
 frame.poc=UINT32_MAX;h.rpm_param.p.CUR_RPS[0]=0x4001;
 assert(!codec_hevc_update_frame_refs(&s,&frame)&&frame.ref_poc_list[0][0]==0);
 codec_hevc_update_ldc_flag(&h);assert(!h.ldc_flag);
 /* B L1 must be compared as signed, independently of L0. */
 frame.poc=0;frame.cur_slice_type=B_SLICE;frame.ref_num[0]=frame.ref_num[1]=1;
 frame.ref_poc_list[0][0]=(u32)-1;frame.ref_poc_list[1][0]=(u32)-2;
 codec_hevc_update_ldc_flag(&h);assert(h.ldc_flag);
 frame.ref_poc_list[1][0]=1;codec_hevc_update_ldc_flag(&h);assert(!h.ldc_flag);
 frame.cur_slice_type=I_SLICE;codec_hevc_update_ldc_flag(&h);assert(!h.ldc_flag);
 return 0;}
 /* RADL/RASL and non-reference temporal-zero pictures cannot replace pocTid0. */
 const u32 excluded[]={0,2,4,6,7,8,9};
 for(u32 i=0;i<sizeof(excluded)/sizeof(excluded[0]);i++){
  h.prev_tid0_poc=0;picture(&s,excluded[i],1,9);
  assert((s32)h.curr_poc==-7&&h.prev_tid0_poc==0);
  picture(&s,1,1,4);assert(h.curr_poc==4&&h.prev_tid0_poc==4);
 }
 h.prev_tid0_poc=0;picture(&s,1,2,9);assert((s32)h.curr_poc==-7&&h.prev_tid0_poc==0);
 h.prev_tid0_poc=14;picture(&s,1,1,1);assert(h.curr_poc==17&&h.prev_tid0_poc==17);
 h.prev_tid0_poc=1;picture(&s,1,1,14);assert((s32)h.curr_poc==-2);
 for(u32 type=16;type<=18;type++){h.prev_tid0_poc=0;picture(&s,type,1,14);assert(h.curr_poc==14);}
 for(u32 type=19;type<=20;type++){h.prev_tid0_poc=100;picture(&s,type,1,14);assert(!h.curr_poc&&!h.prev_tid0_poc);}
 return 0;}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(["cc", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            str(path / "test.c"), "-o", str(path / "test")], check=True)
            for mode in ("ldc", "tid0"):
                with self.subTest(mode=mode):
                    subprocess.run([str(path / "test"), mode], check=True)
