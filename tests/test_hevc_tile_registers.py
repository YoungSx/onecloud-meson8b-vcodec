"""Actual HEVC MPRED/SAO function bodies, with MMIO and frame buffers replaced."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HevcTileRegisterTests(unittest.TestCase):
    def test_mpred_rectangle_jump_and_sao_filter_bits(self):
        source = (ROOT / "vdec/codec_hevc.c").read_text()
        sao = source[source.index("static void\ncodec_hevc_set_sao("):
                     source.index("static dma_addr_t codec_hevc_get_frame_mv_paddr(")]
        control = source[source.index("static void\ncodec_hevc_set_mpred_ctrl("):
                         source.index("static void codec_hevc_set_mpred_mv(")]
        mpred = source[source.index("static void codec_hevc_set_mpred("):
                       source.index("/*  motion compensation reference cache controller */")]
        definitions = []
        for line in source.splitlines():
            if line.startswith("#define ") and ("_BIT" in line or "HEVC_SAO_" in line or
                                               "HEVC_sao_" in line or "SAO_ABV_OFFSET" in line or
                                               "SAO_VB_OFFSET" in line):
                # Scratch offsets use the workspace layout; give the harness fixed offsets.
                if "OFFSET" not in line:
                    definitions.append(line)
        code = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t u32;typedef uint16_t u16;typedef uint64_t dma_addr_t;
#define BIT(n) (1u<<(n))
#define GENMASK(h,l) ((~0u << (l)) & (~0u >> (31-(h))))
#define MAX_REF_ACTIVE 16
#define AMVP_MAX_NUM_CANDS 2
#define AMVP_MAX_NUM_CANDS_MEM 3
#define NUM_CHROMA_MODE 5
#define DM_CHROMA_IDX 36
#define I_SLICE 2
#define SAO_ABV_OFFSET 0x10000
#define SAO_VB_OFFSET 0x40000
#include "dos_regs.h"
#include "hevc_regs.h"
#include "hevc-tiles.h"
''' + "\n".join(definitions) + r'''
union rpm_param {struct {u16 slice_type,log2_parallel_merge_level,collocated_from_l0_flag,
 slice_temporal_mvp_enable_flag,dependent_slice_segment_flag,slice_segment_address,
 five_minus_max_num_merge_cand,tiles_flags,misc_flag0,pps_cb_qp_offset,pps_cr_qp_offset,
 slice_beta_offset_div2,slice_tc_offset_div2,pps_beta_offset_div2,pps_tc_offset_div2;}p;};
struct amvdec_core {int unused;};
struct hevc_frame {unsigned slot,cur_slice_idx;u32 ref_num[2],ref_poc_list[2][16];};
struct codec_hevc {union rpm_param rpm_param;u32 width,height,lcu_size,lcu_x_num,lcu_y_num,
 curr_poc,col_poc,ldc_flag;dma_addr_t workspace_paddr;struct hevc_tile tile;bool new_tile;};
struct amvdec_shadow {dma_addr_t paddr;u32 luma;};
struct amvdec_session {struct amvdec_core *core;void *priv;};
static u32 regs[65536];
static struct amvdec_shadow slot={.paddr=0x100000,.luma=0x300000};
static u32 amvdec_read_dos(struct amvdec_core *c,u32 r){(void)c;return regs[r/4];}
static void amvdec_write_dos(struct amvdec_core *c,u32 r,u32 v){(void)c;assert(r/4<65536);regs[r/4]=v;}
static const struct amvdec_shadow *amvdec_pool_slot(struct amvdec_session *s,unsigned i){(void)s;(void)i;return &slot;}
static u32 ilog2(u32 n){return 31-__builtin_clz(n);}
static void codec_hevc_set_mpred_mv(struct amvdec_core *c,struct codec_hevc *h,struct hevc_frame *f,struct hevc_frame *col){(void)c;(void)h;(void)f;(void)col;}
''' + sao + control + mpred + r'''
int main(void) {
 struct amvdec_core core={0};struct codec_hevc h={.width=1920,.height=1080,.lcu_size=64,
 .lcu_x_num=30,.lcu_y_num=17,.tile={.left=7,.top=8,.width=8,.height=9},.new_tile=true,
 .workspace_paddr=0x800000};struct amvdec_session s={&core,&h};struct hevc_frame f={0};
 h.rpm_param.p.slice_type=I_SLICE;h.rpm_param.p.tiles_flags=3;
 h.rpm_param.p.misc_flag0=1u<<LOOP_FILER_ACROSS_TILES_ENABLED_FLAG_BIT;
 regs[HEVC_MPRED_LT_REF/4]=0xffffffff;
 codec_hevc_set_mpred(&s,&f,&f);
 assert(!regs[HEVC_MPRED_LT_REF/4]);
 assert(regs[HEVC_MPRED_TILE_START/4]==(7u|(8u<<16)));
 assert(regs[HEVC_MPRED_TILE_SIZE_LCU/4]==(8u|(9u<<16)));
 assert(regs[HEVC_MPRED_MV_WR_ROW_JUMP/4]==(30-8)*0x200);
 assert(regs[HEVC_MPRED_CTRL0/4]&MPRED_CTRL0_NEW_TILE);
 codec_hevc_set_sao(&s,&f);
 assert(regs[HEVC_DBLK_CFG9/4]&1);
 assert(regs[HEVC_SAO_CTRL0/4]&BIT(8));
 assert(regs[HEVC_sao_mem_unit/4]==320);
 assert(regs[HEVC_sao_vb_size/4]==(320+32)*17);
 for(u32 lcu=16;lcu<=64;lcu*=2){
  h.lcu_size=lcu;codec_hevc_set_sao(&s,&f);
  assert(regs[HEVC_sao_mem_unit/4]==((lcu/8)*2+4)*16);
 }
 h.lcu_size=64;
 h.rpm_param.p.tiles_flags=0;h.rpm_param.p.misc_flag0=0;h.new_tile=false;
 h.rpm_param.p.slice_segment_address=30;
 codec_hevc_set_sao(&s,&f);codec_hevc_set_mpred(&s,&f,&f);
 assert(!(regs[HEVC_DBLK_CFG9/4]&1) && !(regs[HEVC_SAO_CTRL0/4]&BIT(8)));
 assert(!(regs[HEVC_MPRED_CTRL0/4]&MPRED_CTRL0_NEW_TILE));
 h.rpm_param.p.slice_type=1;h.ldc_flag=1;codec_hevc_set_mpred(&s,&f,&f);
 assert(regs[HEVC_MPRED_CTRL0/4]&MPRED_CTRL0_LDC);
 h.ldc_flag=0;codec_hevc_set_mpred(&s,&f,&f);assert(!(regs[HEVC_MPRED_CTRL0/4]&MPRED_CTRL0_LDC));
 /* Mixed independent and dependent slices across tile boundaries keep exact flags. */
 h.rpm_param.p.tiles_flags=3;h.new_tile=true;
 h.rpm_param.p.dependent_slice_segment_flag=1;
 h.rpm_param.p.slice_segment_address=8;
 codec_hevc_set_mpred(&s,&f,&f);
 assert((regs[HEVC_MPRED_CTRL0/4]&(MPRED_CTRL0_NEW_TILE|MPRED_CTRL0_NEW_SLI_SEG|MPRED_CTRL0_NEW_PIC))==
  (MPRED_CTRL0_NEW_TILE|MPRED_CTRL0_NEW_SLI_SEG));
 h.new_tile=false;h.rpm_param.p.dependent_slice_segment_flag=0;
 codec_hevc_set_mpred(&s,&f,&f);
 assert(!(regs[HEVC_MPRED_CTRL0/4]&(MPRED_CTRL0_NEW_TILE|MPRED_CTRL0_NEW_SLI_SEG|MPRED_CTRL0_NEW_PIC)));
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                            "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            "-I", str(ROOT / "vdec"), str(path / "test.c"),
                            "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)
