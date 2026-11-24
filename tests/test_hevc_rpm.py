"""Validate firmware fields before every slice's POC and reference processing."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HevcRpmTests(unittest.TestCase):
    def test_continued_slice_uses_common_field_validation(self):
        source = (ROOT / "vdec/codec_hevc.c").read_text()
        rpm = source[source.index("union rpm_param {"):
                     source.index("enum nal_unit_type {")]
        body = source[source.index("static int codec_hevc_process_rpm("):
                      source.index("static void codec_hevc_fetch_rpm(")]
        code = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef uint32_t u32;typedef uint16_t u16;typedef uint8_t u8;
#define RPM_SIZE 0x80
#define MAX_REF_ACTIVE 16
#define I_SLICE 2
#define SAO_VB_SIZE 0x30000
#define SAO_ABV_SIZE 0x30000
#define max(a,b) ((a)>(b)?(a):(b))
#define max_t(t,a,b) ((t)(a)>(t)(b)?(t)(a):(t)(b))
#define dev_dbg(...) ((void)0)
#include "hevc-sps.h"
#include "hevc-tiles.h"
''' + rpm + r'''
struct codec_hevc {union rpm_param rpm_param;u32 width,height,dst_width,dst_height,dpb_size,sps_dpb_size,lcu_x_num,lcu_y_num,lcu_total,lcu_size;int is_10bit;
 struct hevc_sps_geometry sps,applied;struct hevc_tile_grid tiles;};
struct format {u32 max_width,max_height;};
struct core {void *dev;};
struct amvdec_session {void *priv;struct core *core;struct format *fmt_out;u32 reference_width,reference_height,crop_left,crop_top;};
''' + body + r'''
#define BAD(field,value) do{u16 old=h.rpm_param.p.field;h.rpm_param.p.field=value;assert(codec_hevc_process_rpm(&s)==-EINVAL);h.rpm_param.p.field=old;}while(0)
int main(void){
 struct codec_hevc h={0};struct format f={1920,1088};struct core core={0};struct amvdec_session s={.priv=&h,.core=&core,.fmt_out=&f};
 h.rpm_param.p.pic_width_in_luma_samples=1920;h.rpm_param.p.pic_height_in_luma_samples=1088;
 h.rpm_param.p.log2_diff_max_min_coding_block_size=3;h.rpm_param.p.log2_max_pic_order_cnt_lsb_minus4=4;
 h.rpm_param.p.m_temporalId=1;h.rpm_param.p.slice_type=1;h.rpm_param.p.num_ref_idx_l0_active=1;
 h.rpm_param.p.first_slice_segment_in_pic_flag=1;
 assert(codec_hevc_process_rpm(&s)==1&&h.lcu_total==510&&h.lcu_size==64);
 /* Low-delay SPS can require ten retained pictures with no output reorder. */
 h.applied=(struct hevc_sps_geometry){.coded_width=1920,.coded_height=1088,.width=1920,.height=1088,.dpb=10};
 assert(!codec_hevc_process_rpm(&s)&&h.dpb_size>=h.applied.dpb);
 h.sps_dpb_size=10;h.applied.dpb=5;
 assert(!codec_hevc_process_rpm(&s)&&h.dpb_size>=10);
 /* Subsequent slices keep geometry; the same fields still feed POC/MPRED. */
 h.rpm_param.p.slice_segment_address=1;h.rpm_param.p.first_slice_segment_in_pic_flag=0;
 assert(!codec_hevc_process_rpm(&s)&&h.width==1920&&h.height==1088);
 h.rpm_param.p.dependent_slice_segment_flag=1;assert(!codec_hevc_process_rpm(&s));
 BAD(log2_max_pic_order_cnt_lsb_minus4,32);
 BAD(collocated_from_l0_flag,2);
 BAD(num_ref_idx_l0_active,17);
 BAD(num_ref_idx_l1_active,17);
 BAD(slice_type,3);
 BAD(log2_parallel_merge_level,7);
 BAD(five_minus_max_num_merge_cand,5);
 BAD(first_slice_segment_in_pic_flag,2);
 BAD(dependent_slice_segment_flag,2);
 assert(!codec_hevc_process_rpm(&s)&&h.lcu_size==64&&h.lcu_total==510);
 /* Geometry-less notifications must not bypass unsafe-field checks either. */
 h.rpm_param.p.slice_segment_address=0;h.rpm_param.p.pic_width_in_luma_samples=0;
 BAD(log2_max_pic_order_cnt_lsb_minus4,65535);
 assert(!codec_hevc_process_rpm(&s));
 return 0;}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(["cc", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-sign-compare",
                            "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            "-I", str(ROOT / "vdec"), str(path / "test.c"),
                            "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)


    def test_maximum_mpred_addresses_fit_declared_workspace(self):
        source = (ROOT / "vdec/codec_hevc.c").read_text()
        base = source[source.index("static dma_addr_t codec_hevc_get_frame_mv_paddr("):
                      source.index("static void\ncodec_hevc_set_mpred_ctrl(")]
        mv = source[source.index("static void codec_hevc_set_mpred_mv("):
                    source.index("/* Update motion prediction with the current slice */")]
        platform = (ROOT / "vdec/vdec_platform.c").read_text()
        fmt = platform[platform.index(".pixfmt = V4L2_PIX_FMT_HEVC"):
                       platform.index(".pixfmt = V4L2_PIX_FMT_H264")]
        slots = int(re.search(r"\.max_buffers\s*=\s*(\d+)", fmt)[1])
        definitions = "\n".join(line for line in source.splitlines()
                                if line.startswith(("#define MPRED_MV_BUF_SIZE ", "#define MPRED_MV_SIZE")))
        code = r'''
#include <assert.h>
#include <stdint.h>
typedef uint32_t u32;typedef uint16_t u16;typedef uint64_t dma_addr_t;
#define MPRED_MV_OFFSET 0x10000
#define MPRED_ABV_OFFSET 0x1000
#include "dos_regs.h"
#include "hevc_regs.h"
''' + definitions + f'\n#define DECLARED_SLOTS {slots}\n' + r'''
union rpm_param {struct {u16 slice_segment_address;}p;};
struct codec_hevc {dma_addr_t workspace_paddr;u32 lcu_size,lcu_total,slice_addr;union rpm_param rpm_param;};
struct hevc_frame {u32 slot;};struct amvdec_core {int unused;};
static u32 registers[65536];
static u32 amvdec_read_dos(struct amvdec_core *c,u32 r){(void)c;return registers[r/4];}
static void amvdec_write_dos(struct amvdec_core *c,u32 r,u32 v){(void)c;registers[r/4]=v;}
static u32 ilog2(u32 n){return 31-__builtin_clz(n);}
''' + base + mv + r'''
int main(void){
 assert(DECLARED_SLOTS*MPRED_MV_BUF_SIZE<=MPRED_MV_SIZE);
 struct amvdec_core c={0};struct codec_hevc h={.workspace_paddr=0x100000};
 for(u32 size=16;size<=64;size*=2){
  h.lcu_size=size;h.lcu_total=((1920+size-1)/size)*((1088+size-1)/size);
  u32 unit=size==64?0x200:size==32?0x80:0x20;
  assert(h.lcu_total*unit<=MPRED_MV_BUF_SIZE);
  for(u32 slot=0;slot<DECLARED_SLOTS;slot++){
   struct hevc_frame frame={slot},col={DECLARED_SLOTS-slot-1};
   u32 start=h.workspace_paddr+MPRED_MV_OFFSET+slot*MPRED_MV_BUF_SIZE;
   u32 colstart=h.workspace_paddr+MPRED_MV_OFFSET+col.slot*MPRED_MV_BUF_SIZE;
   h.slice_addr=0;h.rpm_param.p.slice_segment_address=0;
   codec_hevc_set_mpred_mv(&c,&h,&frame,&col);
   assert(registers[HEVC_MPRED_MV_WR_START_ADDR/4]==start);
   assert(registers[HEVC_MPRED_MV_RD_START_ADDR/4]==colstart);
   assert(registers[HEVC_MPRED_MV_WPTR/4]==start&&registers[HEVC_MPRED_MV_RPTR/4]==colstart);
   assert(registers[HEVC_MPRED_MV_RD_END_ADDR/4]<=colstart+MPRED_MV_BUF_SIZE);
   h.slice_addr=h.lcu_total-1;h.rpm_param.p.slice_segment_address=h.slice_addr;
   codec_hevc_set_mpred_mv(&c,&h,&frame,&col);
   assert(registers[HEVC_MPRED_MV_RPTR/4]==colstart+h.slice_addr*unit);
   assert(registers[HEVC_MPRED_MV_RPTR/4]<registers[HEVC_MPRED_MV_RD_END_ADDR/4]);
   assert(start+MPRED_MV_BUF_SIZE<=h.workspace_paddr+MPRED_MV_OFFSET+MPRED_MV_SIZE);
  }
 }
 return 0;}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(["cc", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-but-set-variable", "-fsanitize=address,undefined",
                            "-fno-pie", "-no-pie", "-I", str(ROOT / "vdec"),
                            str(path / "test.c"), "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)
