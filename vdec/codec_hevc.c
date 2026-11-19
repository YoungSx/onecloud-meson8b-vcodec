// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Maxime Jourdan <mjourdan@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 */

#include <media/v4l2-mem2mem.h>
#include <media/v4l2-event.h>
#include <linux/workqueue.h>
#include <media/videobuf2-dma-contig.h>

#include "codec_hevc.h"
#include "dos_regs.h"
#include "hevc_regs.h"
#include "vdec_helpers.h"
#include "codec_hevc_common.h"
#include "hevc-sps.h"

/* HEVC reg mapping */
#define HEVC_DEC_STATUS_REG	HEVC_ASSIST_SCRATCH_0
	#define HEVC_ACTION_DONE	0xff
#define HEVC_RPM_BUFFER		HEVC_ASSIST_SCRATCH_1
#define HEVC_SHORT_TERM_RPS	HEVC_ASSIST_SCRATCH_2
#define HEVC_VPS_BUFFER		HEVC_ASSIST_SCRATCH_3
#define HEVC_SPS_BUFFER		HEVC_ASSIST_SCRATCH_4
#define HEVC_PPS_BUFFER		HEVC_ASSIST_SCRATCH_5
#define HEVC_SAO_UP		HEVC_ASSIST_SCRATCH_6
#define HEVC_STREAM_SWAP_BUFFER HEVC_ASSIST_SCRATCH_7
#define H265_MMU_MAP_BUFFER	HEVC_ASSIST_SCRATCH_7
#define HEVC_STREAM_SWAP_BUFFER2 HEVC_ASSIST_SCRATCH_8
#define HEVC_sao_mem_unit	HEVC_ASSIST_SCRATCH_9
#define HEVC_SAO_ABV		HEVC_ASSIST_SCRATCH_A
#define HEVC_sao_vb_size	HEVC_ASSIST_SCRATCH_B
#define HEVC_SAO_VB		HEVC_ASSIST_SCRATCH_C
#define HEVC_SCALELUT		HEVC_ASSIST_SCRATCH_D
#define HEVC_WAIT_FLAG		HEVC_ASSIST_SCRATCH_E
#define RPM_CMD_REG		HEVC_ASSIST_SCRATCH_F
#define LMEM_DUMP_ADR		HEVC_ASSIST_SCRATCH_F
#define DEBUG_REG1		HEVC_ASSIST_SCRATCH_G
#define HEVC_DECODE_MODE2	HEVC_ASSIST_SCRATCH_H
#define NAL_SEARCH_CTL		HEVC_ASSIST_SCRATCH_I
#define HEVC_DECODE_MODE	HEVC_ASSIST_SCRATCH_J
	#define DECODE_MODE_SINGLE 0
#define DECODE_STOP_POS		HEVC_ASSIST_SCRATCH_K
#define HEVC_AUX_ADR		HEVC_ASSIST_SCRATCH_L
#define HEVC_AUX_DATA_SIZE	HEVC_ASSIST_SCRATCH_M
#define HEVC_DECODE_SIZE	HEVC_ASSIST_SCRATCH_N

#define AMRISC_MAIN_REQ		 0x04

/* HEVC Constants */
#define MAX_REF_PIC_NUM		24
#define MAX_REF_ACTIVE		16
#define MAX_TILE_COL_NUM	4
#define MAX_TILE_ROW_NUM	4
#define MAX_SLICE_NUM		800
#define INVALID_POC		0x80000000

/* HEVC Workspace layout */
#define MPRED_MV_BUF_SIZE 0x40000

#define IPP_SIZE	0x4000
#define SAO_ABV_SIZE	0x30000
#define SAO_VB_SIZE	0x30000
#define SH_TM_RPS_SIZE	0x800
#define VPS_SIZE	0x800
#define SPS_SIZE	0x800
#define PPS_SIZE	0x2000
#define SAO_UP_SIZE	0x2800
#define SWAP_BUF_SIZE	0x800
#define SWAP_BUF2_SIZE	0x800
#define SCALELUT_SIZE	0x8000
#define DBLK_PARA_SIZE	0x20000
#define DBLK_DATA_SIZE	0x40000
#define DBLK_DATA2_SIZE	0x80000
#define MMU_VBH_SIZE	0x5000
#define MPRED_ABV_SIZE	0x8000
#define MPRED_MV_SIZE	(MPRED_MV_BUF_SIZE * 12)
#define RPM_BUF_SIZE	0x100
#define LMEM_SIZE	0xA00

#define IPP_OFFSET       0x00
#define SAO_ABV_OFFSET   (IPP_OFFSET + IPP_SIZE)
#define SAO_VB_OFFSET    (SAO_ABV_OFFSET + SAO_ABV_SIZE)
#define SH_TM_RPS_OFFSET (SAO_VB_OFFSET + SAO_VB_SIZE)
#define VPS_OFFSET       (SH_TM_RPS_OFFSET + SH_TM_RPS_SIZE)
#define SPS_OFFSET       (VPS_OFFSET + VPS_SIZE)
#define PPS_OFFSET       (SPS_OFFSET + SPS_SIZE)
#define SAO_UP_OFFSET    (PPS_OFFSET + PPS_SIZE)
#define SWAP_BUF_OFFSET  (SAO_UP_OFFSET + SAO_UP_SIZE)
#define SWAP_BUF2_OFFSET (SWAP_BUF_OFFSET + SWAP_BUF_SIZE)
#define SCALELUT_OFFSET  (SWAP_BUF2_OFFSET + SWAP_BUF2_SIZE)
#define DBLK_PARA_OFFSET (SCALELUT_OFFSET + SCALELUT_SIZE)
#define DBLK_DATA_OFFSET (DBLK_PARA_OFFSET + DBLK_PARA_SIZE)
#define DBLK_DATA2_OFFSET (DBLK_DATA_OFFSET + DBLK_DATA_SIZE)
#define MMU_VBH_OFFSET   (DBLK_DATA2_OFFSET + DBLK_DATA2_SIZE)
#define MPRED_ABV_OFFSET (MMU_VBH_OFFSET + MMU_VBH_SIZE)
#define MPRED_MV_OFFSET  (MPRED_ABV_OFFSET + MPRED_ABV_SIZE)
#define RPM_OFFSET       (MPRED_MV_OFFSET + MPRED_MV_SIZE)
#define LMEM_OFFSET      (RPM_OFFSET + RPM_BUF_SIZE)

/* ISR decode status */
#define HEVC_DEC_IDLE                        0x0
#define HEVC_NAL_UNIT_VPS                    0x1
#define HEVC_NAL_UNIT_SPS                    0x2
#define HEVC_NAL_UNIT_PPS                    0x3
#define HEVC_NAL_UNIT_CODED_SLICE_SEGMENT    0x4
#define HEVC_CODED_SLICE_SEGMENT_DAT         0x5
#define HEVC_SLICE_DECODING                  0x6
#define HEVC_NAL_UNIT_SEI                    0x7
#define HEVC_SLICE_SEGMENT_DONE              0x8
#define HEVC_NAL_SEARCH_DONE                 0x9
#define HEVC_DECPIC_DATA_DONE                0xa
#define HEVC_DECPIC_DATA_ERROR               0xb
#define HEVC_SEI_DAT                         0xc
#define HEVC_SEI_DAT_DONE                    0xd

/* RPM misc_flag0 */
#define PCM_LOOP_FILTER_DISABLED_FLAG_BIT		0
#define PCM_ENABLE_FLAG_BIT				1
#define LOOP_FILER_ACROSS_TILES_ENABLED_FLAG_BIT	2
#define PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG_BIT	3
#define DEBLOCKING_FILTER_OVERRIDE_ENABLED_FLAG_BIT	4
#define PPS_DEBLOCKING_FILTER_DISABLED_FLAG_BIT		5
#define DEBLOCKING_FILTER_OVERRIDE_FLAG_BIT		6
#define SLICE_DEBLOCKING_FILTER_DISABLED_FLAG_BIT	7
#define SLICE_SAO_LUMA_FLAG_BIT				8
#define SLICE_SAO_CHROMA_FLAG_BIT			9
#define SLICE_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG_BIT 10

/* Constants for HEVC_MPRED_CTRL1 */
#define AMVP_MAX_NUM_CANDS_MEM	3
#define AMVP_MAX_NUM_CANDS	2
#define NUM_CHROMA_MODE		5
#define DM_CHROMA_IDX		36

/* Buffer sizes */
#define SIZE_WORKSPACE ALIGN(LMEM_OFFSET + LMEM_SIZE, 64 * SZ_1K)
#define SIZE_AUX (SZ_1K * 16)
#define SIZE_FRAME_MMU (0x1200 * 4)
#define RPM_SIZE 0x80
#define RPS_USED_BIT 14

/* Data received from the HW in this form, do not rearrange */
union rpm_param {
	struct {
		u16 data[RPM_SIZE];
	} l;
	struct {
		u16 CUR_RPS[MAX_REF_ACTIVE];
		u16 num_ref_idx_l0_active;
		u16 num_ref_idx_l1_active;
		u16 slice_type;
		u16 slice_temporal_mvp_enable_flag;
		u16 dependent_slice_segment_flag;
		u16 slice_segment_address;
		u16 num_title_rows_minus1;
		u16 pic_width_in_luma_samples;
		u16 pic_height_in_luma_samples;
		u16 log2_min_coding_block_size_minus3;
		u16 log2_diff_max_min_coding_block_size;
		u16 log2_max_pic_order_cnt_lsb_minus4;
		u16 POClsb;
		u16 collocated_from_l0_flag;
		u16 collocated_ref_idx;
		u16 log2_parallel_merge_level;
		u16 five_minus_max_num_merge_cand;
		u16 sps_num_reorder_pics_0;
		u16 modification_flag;
		u16 tiles_flags;
		u16 num_tile_columns_minus1;
		u16 num_tile_rows_minus1;
		u16 tile_width[4];
		u16 tile_height[4];
		u16 misc_flag0;
		u16 pps_beta_offset_div2;
		u16 pps_tc_offset_div2;
		u16 slice_beta_offset_div2;
		u16 slice_tc_offset_div2;
		u16 pps_cb_qp_offset;
		u16 pps_cr_qp_offset;
		u16 first_slice_segment_in_pic_flag;
		u16 m_temporalId;
		u16 m_nalUnitType;
		u16 vui_num_units_in_tick_hi;
		u16 vui_num_units_in_tick_lo;
		u16 vui_time_scale_hi;
		u16 vui_time_scale_lo;
		u16 bit_depth;
		u16 profile_etc;
		u16 sei_frame_field_info;
		u16 video_signal_type;
		u16 modification_list[0x20];
	} p;
};

enum nal_unit_type {
	NAL_UNIT_CODED_SLICE_BLA	= 16,
	NAL_UNIT_CODED_SLICE_BLANT	= 17,
	NAL_UNIT_CODED_SLICE_BLA_N_LP	= 18,
	NAL_UNIT_CODED_SLICE_IDR	= 19,
	NAL_UNIT_CODED_SLICE_IDR_N_LP	= 20,
};

enum slice_type {
	B_SLICE = 0,
	P_SLICE = 1,
	I_SLICE = 2,
};

/* A frame being decoded */
struct hevc_frame {
	struct list_head list;
	u32 slot;	/* frame pool slot holding the picture */
	u32 offset;
	u32 poc;
	/* What it was decoded at: the reorder buffer hands it out later. */
	struct amvdec_geometry geo;

	int referenced;
	int show;
	u32 num_reorder_pic;

	u32 cur_slice_idx;
	u32 cur_slice_type;

	/* Current slice lists, retained for collocated-picture lookup. */
	u32 ref_poc_list[2][MAX_REF_ACTIVE];
	u32 ref_num[2];
};

struct codec_hevc {
	struct mutex lock;
	struct amvdec_session *sess;
	struct delayed_work watchdog;

	/* Buffer for the HEVC Workspace */
	void      *workspace_vaddr;
	dma_addr_t workspace_paddr;

	/* Contains many information parsed from the bitstream */
	union rpm_param rpm_param;

	/* Information computed from the RPM */
	u32 lcu_size; // Largest Coding Unit
	u32 lcu_x_num;
	u32 lcu_y_num;
	u32 lcu_total;

	/* Current Frame being handled */
	struct hevc_frame *cur_frame;
	u32 curr_poc;
	/* Collocated Reference Picture */
	struct hevc_frame *col_frame;
	u32 col_poc;

	/* All ref frames used by the HW at a given time */
	struct list_head ref_frames_list;
	u32 frames_num;

	/* Coded resolution reported by the hardware */
	u32 width, height;
	/* Resolution minus the conformance window offsets */
	u32 dst_width, dst_height;

	u32 dpb_size;
	bool waiting_for_buffer;
	bool eos_seen;
	/* the last watchdog check found the client waiting on the decoder */
	bool client_waited;
	/* Last SPS parsed out of the queued input, not necessarily this picture's. */
	struct hevc_sps_geometry sps;
	/* SPS that resolved the current coded size (see codec_hevc_process_rpm). */
	struct hevc_sps_geometry applied;
	u32 prev_tid0_poc;
	u32 slice_segment_addr;
	u32 slice_addr;
	u32 ldc_flag;

	/* Whether we detected the bitstream as 10-bit */
	int is_10bit;
};

static u32 codec_hevc_num_pending_bufs(struct amvdec_session *sess)
{
	struct codec_hevc *hevc;
	u32 ret;

	hevc = sess->priv;
	if (!hevc)
		return 0;

	mutex_lock(&hevc->lock);
	ret = hevc->frames_num;
	mutex_unlock(&hevc->lock);

	return ret;
}

/* Update the L0 and L1 reference lists for a given frame */
static int codec_hevc_update_frame_refs(struct amvdec_session *sess,
					 struct hevc_frame *frame)
{
	struct codec_hevc *hevc = sess->priv;
	union rpm_param *params = &hevc->rpm_param;
	int num_ref_idx_l0_active =
		(params->p.num_ref_idx_l0_active > MAX_REF_ACTIVE) ?
		MAX_REF_ACTIVE : params->p.num_ref_idx_l0_active;
	int num_ref_idx_l1_active =
		(params->p.num_ref_idx_l1_active > MAX_REF_ACTIVE) ?
		MAX_REF_ACTIVE : params->p.num_ref_idx_l1_active;
	int ref_picset0[MAX_REF_ACTIVE] = { 0 };
	int ref_picset1[MAX_REF_ACTIVE] = { 0 };
	u16 *mod_list = params->p.modification_list;
	int num_neg = 0;
	int num_pos = 0;
	int total_num;
	int i;

	for (i = 0; i < MAX_REF_ACTIVE; i++) {
		frame->ref_poc_list[0][i] = 0;
		frame->ref_poc_list[1][i] = 0;
	}

	for (i = 0; i < MAX_REF_ACTIVE; i++) {
		u16 cur_rps = params->p.CUR_RPS[i];
		int delt = cur_rps & ((1 << (RPS_USED_BIT - 1)) - 1);

		if (cur_rps & 0x8000)
			break;

		if (!((cur_rps >> RPS_USED_BIT) & 1))
			continue;

		if ((cur_rps >> (RPS_USED_BIT - 1)) & 1) {
			ref_picset0[num_neg] =
			       frame->poc - ((1 << (RPS_USED_BIT - 1)) - delt);
			num_neg++;
		} else {
			ref_picset1[num_pos] = frame->poc + delt;
			num_pos++;
		}
	}

	total_num = num_neg + num_pos;

	if (total_num <= 0)
		goto end;

	for (i = 0; i < num_ref_idx_l0_active; i++) {
		int cidx;
		if (params->p.modification_flag & 0x1)
			cidx = mod_list[i];
		else
			cidx = i % total_num;
		if (cidx >= total_num)
			return -EINVAL;

		frame->ref_poc_list[0][i] =
			cidx >= num_neg ? ref_picset1[cidx - num_neg] :
			ref_picset0[cidx];
	}

	if (params->p.slice_type != B_SLICE)
		goto end;

	if (params->p.modification_flag & 0x2) {
		for (i = 0; i < num_ref_idx_l1_active; i++) {
			int cidx;
			if (params->p.modification_flag & 0x1)
				cidx = mod_list[num_ref_idx_l0_active + i];
			else
				cidx = mod_list[i];
			if (cidx >= total_num)
				return -EINVAL;

			frame->ref_poc_list[1][i] =
				(cidx >= num_pos) ? ref_picset0[cidx - num_pos]
				: ref_picset1[cidx];
		}
	} else {
		for (i = 0; i < num_ref_idx_l1_active; i++) {
			int cidx = i % total_num;
			frame->ref_poc_list[1][i] =
				cidx >= num_pos ? ref_picset0[cidx - num_pos] :
				ref_picset1[cidx];
		}
	}

end:
	if (params->p.slice_type != I_SLICE && total_num <= 0)
		return -EINVAL;
	frame->ref_num[0] = num_ref_idx_l0_active;
	frame->ref_num[1] = num_ref_idx_l1_active;

	dev_dbg(sess->core->dev,
		"Frame %u; slice %u; slice_type %u; num_l0 %u; num_l1 %u\n",
		frame->poc, frame->cur_slice_idx, params->p.slice_type,
		frame->ref_num[0], frame->ref_num[1]);
	return 0;
}

static void codec_hevc_update_ldc_flag(struct codec_hevc *hevc)
{
	struct hevc_frame *frame = hevc->cur_frame;
	u32 slice_type = frame->cur_slice_type;
	int i;

	hevc->ldc_flag = 0;

	if (slice_type == I_SLICE)
		return;

	hevc->ldc_flag = 1;
	for (i = 0; (i < frame->ref_num[0]) && hevc->ldc_flag; i++) {
		if (frame->ref_poc_list[0][i] > frame->poc) {
			hevc->ldc_flag = 0;
			break;
		}
	}

	if (slice_type == P_SLICE)
		return;

	for (i = 0; (i < frame->ref_num[1]) && hevc->ldc_flag; i++) {
		if (frame->ref_poc_list[1][i] > frame->poc) {
			hevc->ldc_flag = 0;
			break;
		}
	}
}

/* Tag "old" frames that are no longer referenced */
static void codec_hevc_update_referenced(struct codec_hevc *hevc)
{
	union rpm_param *param = &hevc->rpm_param;
	struct hevc_frame *frame;
	int i;
	u32 curr_poc = hevc->curr_poc;

	list_for_each_entry(frame, &hevc->ref_frames_list, list) {
		int is_referenced = 0;
		u32 poc_tmp;

		if (!frame->referenced)
			continue;

		for (i = 0; i < MAX_REF_ACTIVE; i++) {
			int delt;
			if (param->p.CUR_RPS[i] & 0x8000)
				break;

			delt = param->p.CUR_RPS[i] &
			       ((1 << (RPS_USED_BIT - 1)) - 1);
			if (param->p.CUR_RPS[i] & (1 << (RPS_USED_BIT - 1))) {
				poc_tmp = curr_poc -
				      ((1 << (RPS_USED_BIT - 1)) - delt);
			} else {
				poc_tmp = curr_poc + delt;
			}

			if (poc_tmp == frame->poc) {
				is_referenced = 1;
				break;
			}
		}

		frame->referenced = is_referenced;
	}
}

static struct hevc_frame *
codec_hevc_get_next_ready_frame(struct codec_hevc *hevc)
{
	struct hevc_frame *tmp, *ret = NULL;
	s32 poc = INT_MAX;

	list_for_each_entry(tmp, &hevc->ref_frames_list, list) {
		if (((s32)tmp->poc < poc) && tmp->show) {
			ret = tmp;
			poc = (s32)tmp->poc;
		}
	}

	return ret;
}

static void codec_hevc_deliver_frame(struct amvdec_session *sess,
				     struct hevc_frame *frame);

/* Try to output as many frames as possible */
static void codec_hevc_show_frames(struct amvdec_session *sess)
{
	struct hevc_frame *tmp, *n;
	struct codec_hevc *hevc = sess->priv;

	while ((tmp = codec_hevc_get_next_ready_frame(hevc))) {
		if (hevc->curr_poc &&
		   (hevc->frames_num <= tmp->num_reorder_pic))
			break;

		dev_dbg(sess->core->dev, "DONE frame poc %u; slot %u\n",
			tmp->poc, tmp->slot);
		codec_hevc_deliver_frame(sess, tmp);

		tmp->show = 0;
		hevc->frames_num--;
	}

	/* clean output frame buffer */
	list_for_each_entry_safe(tmp, n, &hevc->ref_frames_list, list) {
		if (tmp->referenced || tmp->show)
			continue;

		amvdec_pool_release(sess, tmp->slot);
		list_del(&tmp->list);
		kfree(tmp);
	}
}

static int
codec_hevc_setup_workspace(struct amvdec_session *sess,
			   struct codec_hevc *hevc)
{
	struct amvdec_core *core = sess->core;
	dma_addr_t wkaddr;

	/* Allocate some memory for the HEVC decoder's state */
	hevc->workspace_vaddr = dma_alloc_coherent(core->dev, SIZE_WORKSPACE,
						   &wkaddr, GFP_KERNEL);
	if (!hevc->workspace_vaddr)
		return -ENOMEM;

	hevc->workspace_paddr = wkaddr;
	memset(hevc->workspace_vaddr, 0, SIZE_WORKSPACE);

	amvdec_write_dos(core, HEVCD_IPP_LINEBUFF_BASE, wkaddr + IPP_OFFSET);
	amvdec_write_dos(core, HEVC_RPM_BUFFER, wkaddr + RPM_OFFSET);
	amvdec_write_dos(core, HEVC_SHORT_TERM_RPS, wkaddr + SH_TM_RPS_OFFSET);
	amvdec_write_dos(core, HEVC_VPS_BUFFER, wkaddr + VPS_OFFSET);
	amvdec_write_dos(core, HEVC_SPS_BUFFER, wkaddr + SPS_OFFSET);
	amvdec_write_dos(core, HEVC_PPS_BUFFER, wkaddr + PPS_OFFSET);
	amvdec_write_dos(core, HEVC_SAO_UP, wkaddr + SAO_UP_OFFSET);

	amvdec_write_dos(core, HEVC_STREAM_SWAP_BUFFER, wkaddr + SWAP_BUF_OFFSET);
	amvdec_write_dos(core, HEVC_STREAM_SWAP_BUFFER2, wkaddr + SWAP_BUF2_OFFSET);

	amvdec_write_dos(core, HEVC_SCALELUT, wkaddr + SCALELUT_OFFSET);
	amvdec_write_dos(core, HEVC_DBLK_CFG4, wkaddr + DBLK_PARA_OFFSET);
	amvdec_write_dos(core, HEVC_DBLK_CFG5, wkaddr + DBLK_DATA_OFFSET);


	return 0;
}

/*
 * The firmware reports no error for a bitstream it cannot parse; it just
 * stops interrupting. That is a stall only while the client waits on the
 * decoder, with input the VIFIFO cannot take or a drain in progress.
 * Otherwise the firmware is waiting for more bitstream, which a slow source
 * or a paused client may take any time to send. The client must have been
 * waiting over a whole period without an interrupt, so a drain requested
 * just before a check still gets its full period.
 */
#define HEVC_WATCHDOG_MS	5000

static void codec_hevc_watchdog(struct work_struct *work)
{
	struct codec_hevc *hevc = container_of(to_delayed_work(work),
						struct codec_hevc, watchdog);
	struct amvdec_session *sess = hevc->sess;
	struct amvdec_core *core = sess->core;
	u32 queued = v4l2_m2m_num_src_bufs_ready(sess->m2m_ctx);
	bool drain = READ_ONCE(sess->stop_requested);
	bool stalled = false, waited;
	u32 held, undelivered;

	mutex_lock(&hevc->lock);
	waited = !hevc->eos_seen && !hevc->waiting_for_buffer &&
		 sess->status != STATUS_NEEDS_RESUME &&
		 atomic_read(&sess->esparser_queued_bufs) > 0 &&
		 (queued || drain);
	if (waited && hevc->client_waited) {
		amvdec_write_dos(core, HEVC_ASSIST_MBOX1_MASK, 0);
		amvdec_write_dos(core, HEVC_MPSR, 0);
		stalled = true;
	}
	hevc->client_waited = waited;
	mutex_unlock(&hevc->lock);

	if (!stalled) {
		/* Keep watching while pictures are still owed. */
		if (atomic_read(&sess->esparser_queued_bufs) > 0)
			mod_delayed_work(system_wq, &hevc->watchdog,
					 msecs_to_jiffies(HEVC_WATCHDOG_MS));
		return;
	}

	mutex_lock(&sess->pool.lock);
	held = sess->pool.state.decoder;
	undelivered = sess->pool.state.waiting;
	mutex_unlock(&sess->pool.lock);
	dev_warn(core->dev,
		 "HEVC bitstream stalled; ending session (status %x, VIFIFO level %u, %d pictures owed, %u in DPB, slots held %06x undelivered %06x, %u input buffers queued, drain %d)\n",
		 amvdec_read_dos(core, HEVC_DEC_STATUS_REG),
		 amvdec_read_dos(core, HEVC_STREAM_LEVEL),
		 atomic_read(&sess->esparser_queued_bufs), hevc->frames_num,
		 held, undelivered, queued, drain);
	amvdec_abort(sess);
}

static int codec_hevc_start(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_hevc *hevc;
	u32 val;
	int i;
	int ret;

	hevc = kzalloc(sizeof(*hevc), GFP_KERNEL);
	if (!hevc)
		return -ENOMEM;

	INIT_LIST_HEAD(&hevc->ref_frames_list);
	INIT_DELAYED_WORK(&hevc->watchdog, codec_hevc_watchdog);
	hevc->sess = sess;
	hevc->curr_poc = INVALID_POC;

	ret = codec_hevc_setup_workspace(sess, hevc);
	if (ret)
		goto free_hevc;

	val = BIT(0); /* stream_fetch_enable */
	if (core->platform->revision >= VDEC_REVISION_G12A)
		val |= (0xf << 25); /* arwlen_axi_max */
	amvdec_write_dos_bits(core, HEVC_STREAM_CONTROL, val);

	val = amvdec_read_dos(core, HEVC_PARSER_INT_CONTROL) & 0x03ffffff;
	val |= (3 << 29) | BIT(27) | BIT(24) | BIT(22) | BIT(7) | BIT(4) |
	       BIT(0);
	amvdec_write_dos(core, HEVC_PARSER_INT_CONTROL, val);
	amvdec_write_dos_bits(core, HEVC_SHIFT_STATUS, BIT(1) | BIT(0));
	amvdec_write_dos(core, HEVC_SHIFT_CONTROL,
			 (3 << 6) | BIT(5) | BIT(2) | BIT(0));
	amvdec_write_dos(core, HEVC_CABAC_CONTROL, 1);
	amvdec_write_dos(core, HEVC_PARSER_CORE_CONTROL, 1);
	amvdec_write_dos(core, HEVC_DEC_STATUS_REG, 0);

	amvdec_write_dos(core, HEVC_IQIT_SCALELUT_WR_ADDR, 0);
	for (i = 0; i < 1024; ++i)
		amvdec_write_dos(core, HEVC_IQIT_SCALELUT_DATA, 0);

	amvdec_write_dos(core, HEVC_ASSIST_SCRATCH_N, U32_MAX);
	amvdec_write_dos(core, HEVC_ASSIST_SCRATCH_M, 0);
	amvdec_write_dos(core, HEVC_ASSIST_SCRATCH_L, 0);

	amvdec_write_dos(core, HEVC_PARSER_CMD_WRITE, BIT(16));
	for (i = 0; i < ARRAY_SIZE(vdec_hevc_parser_cmd); ++i)
		amvdec_write_dos(core, HEVC_PARSER_CMD_WRITE,
				 vdec_hevc_parser_cmd[i]);

	amvdec_write_dos(core, HEVC_PARSER_CMD_SKIP_0, PARSER_CMD_SKIP_CFG_0);
	amvdec_write_dos(core, HEVC_PARSER_CMD_SKIP_1, PARSER_CMD_SKIP_CFG_1);
	amvdec_write_dos(core, HEVC_PARSER_CMD_SKIP_2, PARSER_CMD_SKIP_CFG_2);
	amvdec_write_dos(core, HEVC_PARSER_IF_CONTROL,
			 BIT(5) | BIT(2) | BIT(0));

	amvdec_write_dos(core, HEVCD_IPP_TOP_CNTL, BIT(0));
	amvdec_write_dos(core, HEVCD_IPP_TOP_CNTL, BIT(1));

	amvdec_write_dos(core, HEVC_WAIT_FLAG, 1);

	/* clear mailbox interrupt */
	amvdec_write_dos(core, HEVC_ASSIST_MBOX1_CLR_REG, 1);
	/* enable mailbox interrupt */
	amvdec_write_dos(core, HEVC_ASSIST_MBOX1_MASK, 1);
	/* disable PSCALE for hardware sharing */
	amvdec_write_dos(core, HEVC_PSCALE_CTRL, 0);
	/* Let the uCode do all the parsing */
	amvdec_write_dos(core, NAL_SEARCH_CTL, 0xc);

	amvdec_write_dos(core, DECODE_STOP_POS, 0);
	amvdec_write_dos(core, HEVC_DECODE_MODE, DECODE_MODE_SINGLE);
	amvdec_write_dos(core, HEVC_DECODE_MODE2, 0);

	mutex_init(&hevc->lock);
	sess->priv = hevc;

	return 0;

free_hevc:
	if (hevc->workspace_vaddr)
		dma_free_coherent(core->dev, SIZE_WORKSPACE, hevc->workspace_vaddr,
				  hevc->workspace_paddr);
	kfree(hevc);
	return ret;
}

/*
 * Hand a reorder-buffer picture to the frame pool. The pool copies it with
 * the session's geometry, which by then may be the next segment's, so put
 * the one the picture was decoded at back in place: a resolution change
 * must not turn an old tail frame into a copy at the new size.
 */
static void codec_hevc_deliver_frame(struct amvdec_session *sess,
				     struct hevc_frame *frame)
{
	struct amvdec_geometry geo;

	geo.width = sess->width;
	geo.height = sess->height;
	geo.reference_width = sess->reference_width;
	geo.reference_height = sess->reference_height;
	geo.crop_left = sess->crop_left;
	geo.crop_top = sess->crop_top;

	sess->width = frame->geo.width;
	sess->height = frame->geo.height;
	sess->reference_width = frame->geo.reference_width;
	sess->reference_height = frame->geo.reference_height;
	sess->crop_left = frame->geo.crop_left;
	sess->crop_top = frame->geo.crop_top;
	amvdec_pool_picture_done(sess, frame->slot, frame->offset,
				 V4L2_FIELD_NONE);
	sess->width = geo.width;
	sess->height = geo.height;
	sess->reference_width = geo.reference_width;
	sess->reference_height = geo.reference_height;
	sess->crop_left = geo.crop_left;
	sess->crop_top = geo.crop_top;
}

/* Hand every picture userspace is still owed to the frame pool. With
 * @hold_last the final one is returned undelivered, so the caller can mark
 * it as the end of the old stream by changing the session status first.
 */
static struct hevc_frame *codec_hevc_show_pending(struct amvdec_session *sess,
						  bool hold_last)
{
	struct codec_hevc *hevc = sess->priv;
	struct hevc_frame *tmp, *next;

	while ((tmp = codec_hevc_get_next_ready_frame(hevc))) {
		tmp->show = 0;
		next = codec_hevc_get_next_ready_frame(hevc);
		if (hold_last && !next)
			return tmp;
		codec_hevc_deliver_frame(sess, tmp);
		hevc->frames_num--;
	}
	return NULL;
}

static void codec_hevc_flush_output(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	struct hevc_frame *tmp, *n;

	codec_hevc_show_pending(sess, false);

	list_for_each_entry_safe(tmp, n, &hevc->ref_frames_list, list) {
		amvdec_pool_release(sess, tmp->slot);
		list_del(&tmp->list);
		kfree(tmp);
	}
}

/* Pictures referencing slots of another geometry are gone with them. */
static void codec_hevc_forget_frames(struct codec_hevc *hevc)
{
	struct hevc_frame *tmp, *n;

	list_for_each_entry_safe(tmp, n, &hevc->ref_frames_list, list) {
		list_del(&tmp->list);
		kfree(tmp);
	}
	hevc->frames_num = 0;
	hevc->cur_frame = NULL;
	hevc->col_frame = NULL;
}

static const u8 *codec_hevc_eos_sequence(u32 *length)
{
	static const u8 eos[4096] = { 0, 0, 1, 0x48, 1, 0x80 };

	*length = sizeof(eos);
	return eos;
}

/* An empty LAST buffer follows the pictures still waiting for one. */
static void codec_hevc_finish_eos(struct amvdec_session *sess)
{
	amvdec_pool_end(sess, AMVDEC_POOL_LAST);
}

static int codec_hevc_stop(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	struct amvdec_core *core = sess->core;

	cancel_delayed_work_sync(&hevc->watchdog);
	mutex_lock(&hevc->lock);
	codec_hevc_flush_output(sess);

	if (hevc->workspace_vaddr)
		dma_free_coherent(core->dev, SIZE_WORKSPACE,
				  hevc->workspace_vaddr,
				  hevc->workspace_paddr);


	mutex_unlock(&hevc->lock);
	mutex_destroy(&hevc->lock);

	return 0;
}

static struct hevc_frame *
codec_hevc_get_frame_by_poc(struct codec_hevc *hevc, u32 poc)
{
	struct hevc_frame *tmp;

	list_for_each_entry(tmp, &hevc->ref_frames_list, list) {
		if (tmp->poc == poc)
			return tmp;
	}

	return NULL;
}

static struct hevc_frame *
codec_hevc_prepare_new_frame(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct hevc_frame *new_frame = NULL;
	struct codec_hevc *hevc = sess->priv;
	union rpm_param *params = &hevc->rpm_param;
	int slot;

	new_frame = kzalloc(sizeof(*new_frame), GFP_KERNEL);
	if (!new_frame)
		return NULL;

	/* Every slot busy: wait until a delivered picture frees one. */
	slot = amvdec_pool_take(sess);
	if (slot < 0) {
		kfree(new_frame);
		return NULL;
	}

	new_frame->slot = slot;
	new_frame->referenced = 1;
	new_frame->show = 1;
	new_frame->poc = hevc->curr_poc;
	new_frame->cur_slice_type = params->p.slice_type;
	new_frame->num_reorder_pic = params->p.sps_num_reorder_pics_0;
	new_frame->geo.width = sess->width;
	new_frame->geo.height = sess->height;
	new_frame->geo.reference_width = sess->reference_width;
	new_frame->geo.reference_height = sess->reference_height;
	new_frame->geo.crop_left = sess->crop_left;
	new_frame->geo.crop_top = sess->crop_top;
	dev_dbg(core->dev, "HEVC picture POC %d slot %u reorder %u\n",
		 (s32)hevc->curr_poc, slot, new_frame->num_reorder_pic);
	new_frame->offset = amvdec_read_dos(core, HEVC_SHIFT_BYTE_COUNT);

	list_add_tail(&new_frame->list, &hevc->ref_frames_list);
	hevc->frames_num++;

	return new_frame;
}

static void
codec_hevc_set_sao(struct amvdec_session *sess, struct hevc_frame *frame)
{
	struct amvdec_core *core = sess->core;
	struct codec_hevc *hevc = sess->priv;
	const struct amvdec_shadow *slot = amvdec_pool_slot(sess, frame->slot);
	union rpm_param *param = &hevc->rpm_param;
	u32 pic_height_cu =
		(hevc->height + hevc->lcu_size - 1) / hevc->lcu_size;
	u32 sao_mem_unit = (hevc->lcu_size == 16 ? 9 :
			   hevc->lcu_size == 32 ? 14 : 24) << 4;
	u32 sao_vb_size = (sao_mem_unit + (2 << 4)) * pic_height_cu;
	u32 misc_flag0 = param->p.misc_flag0;
	dma_addr_t buf_y_paddr;
	dma_addr_t buf_u_v_paddr;
	u32 slice_deblocking_filter_disabled_flag;
	u32 val, val_2;

	val = (amvdec_read_dos(core, HEVC_SAO_CTRL0) & ~0xf) |
	      ilog2(hevc->lcu_size);
	amvdec_write_dos(core, HEVC_SAO_CTRL0, val);

	amvdec_write_dos(core, HEVC_SAO_PIC_SIZE,
			 hevc->width | (hevc->height << 16));
	amvdec_write_dos(core, HEVC_SAO_PIC_SIZE_LCU,
			 (hevc->lcu_x_num - 1) | (hevc->lcu_y_num - 1) << 16);

	buf_y_paddr = slot->paddr;
	buf_u_v_paddr = buf_y_paddr + slot->luma;
	amvdec_write_dos(core, HEVC_SAO_Y_START_ADDR, buf_y_paddr);
	amvdec_write_dos(core, HEVC_SAO_C_START_ADDR, buf_u_v_paddr);
	amvdec_write_dos(core, HEVC_SAO_Y_WPTR, buf_y_paddr);
	amvdec_write_dos(core, HEVC_SAO_C_WPTR, buf_u_v_paddr);

	amvdec_write_dos(core, HEVC_SAO_Y_LENGTH, slot->luma);
	amvdec_write_dos(core, HEVC_SAO_C_LENGTH, slot->luma / 2);

	if (frame->cur_slice_idx == 0) {

		amvdec_write_dos(core, HEVC_DBLK_CFG2,
				 hevc->width | (hevc->height << 16));

		val = 0;
		if ((misc_flag0 >> PCM_ENABLE_FLAG_BIT) & 0x1)
			val |= ((misc_flag0 >>
				 PCM_LOOP_FILTER_DISABLED_FLAG_BIT) & 0x1) << 3;

		val |= (param->p.pps_cb_qp_offset & 0x1f) << 4;
		val |= (param->p.pps_cr_qp_offset & 0x1f) << 9;
		val |= (hevc->lcu_size == 64) ? 0 :
		       ((hevc->lcu_size == 32) ? 1 : 2);
		amvdec_write_dos(core, HEVC_DBLK_CFG1, val);
	}

	val = amvdec_read_dos(core, HEVC_SAO_CTRL1) & ~0x3ff3;
	val |= 0x880; /* S805 big-endian 64-bit writes. */
	val |= BIT(0); /* Disable compressed output. */

	amvdec_write_dos(core, HEVC_SAO_CTRL1, val);


	val = amvdec_read_dos(core, HEVCD_IPP_AXIIF_CONFIG) & ~0x30;
	val = (val & ~0xf) | 0x8;
	amvdec_write_dos(core, HEVCD_IPP_AXIIF_CONFIG, val);

	val = 0;
	val_2 = amvdec_read_dos(core, HEVC_SAO_CTRL0);
	val_2 &= (~0x300);

	slice_deblocking_filter_disabled_flag = (misc_flag0 >>
			SLICE_DEBLOCKING_FILTER_DISABLED_FLAG_BIT) & 0x1;
	if ((misc_flag0 & (1 << DEBLOCKING_FILTER_OVERRIDE_ENABLED_FLAG_BIT))
		&& (misc_flag0 & (1 << DEBLOCKING_FILTER_OVERRIDE_FLAG_BIT))) {
		val |= slice_deblocking_filter_disabled_flag << 2;

		if (!slice_deblocking_filter_disabled_flag) {
			val |= (param->p.slice_beta_offset_div2 & 0xf) << 3;
			val |= (param->p.slice_tc_offset_div2 & 0xf) << 7;
		}
	} else {
		val |=
			((misc_flag0 >>
			  PPS_DEBLOCKING_FILTER_DISABLED_FLAG_BIT) & 0x1) << 2;

		if (((misc_flag0 >> PPS_DEBLOCKING_FILTER_DISABLED_FLAG_BIT) &
			 0x1) == 0) {
			val |= (param->p.pps_beta_offset_div2 & 0xf) << 3;
			val |= (param->p.pps_tc_offset_div2 & 0xf) << 7;
		}
	}
	if ((misc_flag0 & (1 << PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG_BIT))
		&& ((misc_flag0 & (1 << SLICE_SAO_LUMA_FLAG_BIT))
			|| (misc_flag0 & (1 << SLICE_SAO_CHROMA_FLAG_BIT))
			|| (!slice_deblocking_filter_disabled_flag))) {
		val |=
			((misc_flag0 >>
			  SLICE_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG_BIT)
			 & 0x1)	<< 1;
		val_2 |=
			((misc_flag0 >>
			  SLICE_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG_BIT)
			& 0x1) << 9;
	} else {
		val |=
			((misc_flag0 >>
			  PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG_BIT)
			 & 0x1) << 1;
		val_2 |=
			((misc_flag0 >>
			  PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG_BIT)
			 & 0x1) << 9;
	}

	amvdec_write_dos(core, HEVC_DBLK_CFG9, val);
	amvdec_write_dos(core, HEVC_SAO_CTRL0, val_2);

	amvdec_write_dos(core, HEVC_sao_mem_unit, sao_mem_unit);
	amvdec_write_dos(core, HEVC_SAO_ABV,
			 hevc->workspace_paddr + SAO_ABV_OFFSET);
	amvdec_write_dos(core, HEVC_sao_vb_size, sao_vb_size);
	amvdec_write_dos(core, HEVC_SAO_VB,
			 hevc->workspace_paddr + SAO_VB_OFFSET);
}

static dma_addr_t codec_hevc_get_frame_mv_paddr(struct codec_hevc *hevc,
						struct hevc_frame *frame)
{
	return hevc->workspace_paddr + MPRED_MV_OFFSET +
		(frame->slot * MPRED_MV_BUF_SIZE);
}

static void
codec_hevc_set_mpred_ctrl(struct amvdec_core *core, struct codec_hevc *hevc)
{
	union rpm_param *param = &hevc->rpm_param;
	u32 slice_type = param->p.slice_type;
	u32 lcu_size_log2 = ilog2(hevc->lcu_size);
	u32 val;

	val = slice_type |
	      MPRED_CTRL0_ABOVE_EN |
	      MPRED_CTRL0_MV_WR_EN |
	      MPRED_CTRL0_BUF_LINEAR |
	      (lcu_size_log2 << 16) |
	      (3 << 20) | /* cu_size_log2 */
	      (param->p.log2_parallel_merge_level << 24);

	if (slice_type != I_SLICE)
		val |= MPRED_CTRL0_MV_RD_EN;

	if (param->p.collocated_from_l0_flag)
		val |= MPRED_CTRL0_COL_FROM_L0;

	if (param->p.slice_temporal_mvp_enable_flag)
		val |= MPRED_CTRL0_TMVP;

	if (hevc->ldc_flag)
		val |= MPRED_CTRL0_LDC;

	if (param->p.dependent_slice_segment_flag)
		val |= MPRED_CTRL0_NEW_SLI_SEG;

	if (param->p.slice_segment_address == 0)
		val |= MPRED_CTRL0_NEW_PIC |
		       MPRED_CTRL0_NEW_TILE;

	amvdec_write_dos(core, HEVC_MPRED_CTRL0, val);

	val = (5 - param->p.five_minus_max_num_merge_cand) |
	      (AMVP_MAX_NUM_CANDS << 4) |
	      (AMVP_MAX_NUM_CANDS_MEM << 8) |
	      (NUM_CHROMA_MODE << 12) |
	      (DM_CHROMA_IDX << 16);
	amvdec_write_dos(core, HEVC_MPRED_CTRL1, val);
}

static void codec_hevc_set_mpred_mv(struct amvdec_core *core,
				    struct codec_hevc *hevc,
				    struct hevc_frame *frame,
				    struct hevc_frame *col_frame)
{
	union rpm_param *param = &hevc->rpm_param;
	u32 lcu_size_log2 = ilog2(hevc->lcu_size);
	u32 mv_mem_unit = lcu_size_log2 == 6 ? 0x200 :
			  lcu_size_log2 == 5 ? 0x80 : 0x20;
	dma_addr_t col_mv_rd_start_addr, col_mv_rd_ptr, col_mv_rd_end_addr;
	dma_addr_t mpred_mv_wr_ptr;
	u32 val;

	val = amvdec_read_dos(core, HEVC_MPRED_CURR_LCU);

	col_mv_rd_start_addr = codec_hevc_get_frame_mv_paddr(hevc, col_frame);
	mpred_mv_wr_ptr = codec_hevc_get_frame_mv_paddr(hevc, frame) +
			  (hevc->slice_addr * mv_mem_unit);
	col_mv_rd_ptr = col_mv_rd_start_addr +
			(hevc->slice_addr * mv_mem_unit);
	col_mv_rd_end_addr = col_mv_rd_start_addr +
			     (hevc->lcu_total * mv_mem_unit);

	amvdec_write_dos(core, HEVC_MPRED_MV_WR_START_ADDR,
			 codec_hevc_get_frame_mv_paddr(hevc, frame));
	amvdec_write_dos(core, HEVC_MPRED_MV_RD_START_ADDR,
			 col_mv_rd_start_addr);

	if (param->p.slice_segment_address == 0) {
		amvdec_write_dos(core, HEVC_MPRED_ABV_START_ADDR,
				 hevc->workspace_paddr + MPRED_ABV_OFFSET);
		amvdec_write_dos(core, HEVC_MPRED_MV_WPTR, mpred_mv_wr_ptr);
		amvdec_write_dos(core, HEVC_MPRED_MV_RPTR,
				 col_mv_rd_start_addr);
	} else {
		amvdec_write_dos(core, HEVC_MPRED_MV_RPTR, col_mv_rd_ptr);
	}

	amvdec_write_dos(core, HEVC_MPRED_MV_RD_END_ADDR, col_mv_rd_end_addr);
}

/* Update motion prediction with the current slice */
static void codec_hevc_set_mpred(struct amvdec_session *sess,
				 struct hevc_frame *frame,
				 struct hevc_frame *col_frame)
{
	struct amvdec_core *core = sess->core;
	struct codec_hevc *hevc = sess->priv;
	u32 *ref_num = frame->ref_num;
	u32 *ref_poc_l0 = frame->ref_poc_list[0];
	u32 *ref_poc_l1 = frame->ref_poc_list[1];
	u32 val;
	int i;

	codec_hevc_set_mpred_ctrl(core, hevc);
	codec_hevc_set_mpred_mv(core, hevc, frame, col_frame);

	amvdec_write_dos(core, HEVC_MPRED_PIC_SIZE,
			 hevc->width | (hevc->height << 16));

	val = ((hevc->lcu_x_num - 1) | (hevc->lcu_y_num - 1) << 16);
	amvdec_write_dos(core, HEVC_MPRED_PIC_SIZE_LCU, val);
	/* S805 requires the full tile rectangle even for a single tile. */
	amvdec_write_dos(core, HEVC_MPRED_TILE_START, 0);
	amvdec_write_dos(core, HEVC_MPRED_TILE_SIZE_LCU,
			 hevc->lcu_x_num | (hevc->lcu_y_num << 16));
	amvdec_write_dos(core, HEVC_MPRED_MV_WR_ROW_JUMP, 0);
	amvdec_write_dos(core, HEVC_MPRED_MV_RD_ROW_JUMP, 0);

	amvdec_write_dos(core, HEVC_MPRED_REF_NUM,
			 (ref_num[1] << 8) | ref_num[0]);
	amvdec_write_dos(core, HEVC_MPRED_REF_EN_L0, (1 << ref_num[0]) - 1);
	amvdec_write_dos(core, HEVC_MPRED_REF_EN_L1, (1 << ref_num[1]) - 1);

	amvdec_write_dos(core, HEVC_MPRED_CUR_POC, hevc->curr_poc);
	amvdec_write_dos(core, HEVC_MPRED_COL_POC, hevc->col_poc);

	for (i = 0; i < MAX_REF_ACTIVE; ++i) {
		amvdec_write_dos(core, HEVC_MPRED_L0_REF00_POC + i * 4,
				 ref_poc_l0[i]);
		amvdec_write_dos(core, HEVC_MPRED_L1_REF00_POC + i * 4,
				 ref_poc_l1[i]);
	}
}

/*  motion compensation reference cache controller */
static void codec_hevc_set_mcrcc(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_hevc *hevc = sess->priv;
	u32 val, val_2;
	int l0_cnt = 0;
	int l1_cnt = 0x7fff;

	l0_cnt = hevc->cur_frame->ref_num[0];
	l1_cnt = hevc->cur_frame->ref_num[1];

	if (hevc->cur_frame->cur_slice_type == I_SLICE) {
		amvdec_write_dos(core, HEVCD_MCRCC_CTL1, 0);
		return;
	}

	if (hevc->cur_frame->cur_slice_type == P_SLICE) {
		amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR,
				 BIT(1));
		val = amvdec_read_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR);
		val &= 0xffff;
		val |= (val << 16);
		amvdec_write_dos(core, HEVCD_MCRCC_CTL2, val);

		if (l0_cnt == 1) {
			amvdec_write_dos(core, HEVCD_MCRCC_CTL3, val);
		} else {
			val = amvdec_read_dos(core,
					      HEVCD_MPP_ANC_CANVAS_DATA_ADDR);
			val &= 0xffff;
			val |= (val << 16);
			amvdec_write_dos(core, HEVCD_MCRCC_CTL3, val);
		}
	} else { /* B_SLICE */
		amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, 0);
		val = amvdec_read_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR);
		val &= 0xffff;
		val |= (val << 16);
		amvdec_write_dos(core, HEVCD_MCRCC_CTL2, val);

		amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR,
				 BIT(12) | BIT(1));
		val_2 = amvdec_read_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR);
		val_2 &= 0xffff;
		val_2 |= (val_2 << 16);
		if (val == val_2 && l1_cnt > 1) {
			val_2 = amvdec_read_dos(core,
						HEVCD_MPP_ANC_CANVAS_DATA_ADDR);
			val_2 &= 0xffff;
			val_2 |= (val_2 << 16);
		}
		amvdec_write_dos(core, HEVCD_MCRCC_CTL3, val_2);
	}

	/* enable mcrcc progressive-mode */
	amvdec_write_dos(core, HEVCD_MCRCC_CTL1, 0xff0);
}

static void codec_hevc_set_ref_list(struct amvdec_session *sess,
				u32 ref_num, u32 *ref_poc_list)
{
	struct codec_hevc *hevc = sess->priv;
	struct hevc_frame *ref_frame;
	struct amvdec_core *core = sess->core;
	int i;
	u32 buf_id_y;
	u32 buf_id_uv;

	for (i = 0; i < ref_num; i++) {
		ref_frame = codec_hevc_get_frame_by_poc(hevc, ref_poc_list[i]);

		if (!ref_frame) {
			dev_warn(core->dev, "Couldn't find ref. frame %u\n",
				ref_poc_list[i]);
			continue;
		}

		buf_id_y = ref_frame->slot * 2;
		buf_id_uv = buf_id_y + 1;

		amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR,
				 (buf_id_uv << 16) |
				 (buf_id_uv << 8) |
				 buf_id_y);
	}
}

static void codec_hevc_set_mc(struct amvdec_session *sess,
			      struct hevc_frame *frame)
{
	struct amvdec_core *core = sess->core;

	if (frame->cur_slice_type == I_SLICE)
		return;

	amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, 1);
	codec_hevc_set_ref_list(sess, frame->ref_num[0],
		frame->ref_poc_list[0]);

	if (frame->cur_slice_type == P_SLICE)
		return;

	amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR,
			 BIT(12) | BIT(0));
	codec_hevc_set_ref_list(sess, frame->ref_num[1],
		frame->ref_poc_list[1]);
}

static void codec_hevc_update_col_frame(struct codec_hevc *hevc)
{
	struct hevc_frame *cur_frame = hevc->cur_frame;
	union rpm_param *param = &hevc->rpm_param;
	u32 list_no = 0;
	u32 col_ref = param->p.collocated_ref_idx;
	u32 col_from_l0 = param->p.collocated_from_l0_flag;

	if (cur_frame->cur_slice_type == B_SLICE)
		list_no = 1 - col_from_l0;

	if (col_ref >= cur_frame->ref_num[list_no])
		hevc->col_poc = INVALID_POC;
	else
		hevc->col_poc = cur_frame->ref_poc_list[list_no]
						       [col_ref];

	if (cur_frame->cur_slice_type == I_SLICE)
		goto end;

	if (hevc->col_poc != INVALID_POC)
		hevc->col_frame = codec_hevc_get_frame_by_poc(hevc,
							      hevc->col_poc);
	else
		hevc->col_frame = hevc->cur_frame;

end:
	if (!hevc->col_frame)
		hevc->col_frame = hevc->cur_frame;
}

static void codec_hevc_update_pocs(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	union rpm_param *param = &hevc->rpm_param;
	u32 nal_unit_type = param->p.m_nalUnitType;
	u32 temporal_id = param->p.m_temporalId & 0x7;
	int max_poc_lsb =
		1 << (param->p.log2_max_pic_order_cnt_lsb_minus4 + 4);
	int prev_poc_lsb;
	int prev_poc_msb;
	int poc_msb;
	int poc_lsb = param->p.POClsb;

	if (nal_unit_type == NAL_UNIT_CODED_SLICE_IDR ||
	    nal_unit_type == NAL_UNIT_CODED_SLICE_IDR_N_LP) {
		hevc->curr_poc = 0;
		if ((temporal_id - 1) == 0)
			hevc->prev_tid0_poc = hevc->curr_poc;

		return;
	}

	prev_poc_lsb = hevc->prev_tid0_poc % max_poc_lsb;
	prev_poc_msb = hevc->prev_tid0_poc - prev_poc_lsb;

	if ((poc_lsb < prev_poc_lsb) &&
	    ((prev_poc_lsb - poc_lsb) >= (max_poc_lsb / 2)))
		poc_msb = prev_poc_msb + max_poc_lsb;
	else if ((poc_lsb > prev_poc_lsb) &&
		 ((poc_lsb - prev_poc_lsb) > (max_poc_lsb / 2)))
		poc_msb = prev_poc_msb - max_poc_lsb;
	else
		poc_msb = prev_poc_msb;

	if (nal_unit_type == NAL_UNIT_CODED_SLICE_BLA   ||
	    nal_unit_type == NAL_UNIT_CODED_SLICE_BLANT ||
	    nal_unit_type == NAL_UNIT_CODED_SLICE_BLA_N_LP)
		poc_msb = 0;

	hevc->curr_poc = (poc_msb + poc_lsb);
	if ((temporal_id - 1) == 0)
		hevc->prev_tid0_poc = hevc->curr_poc;
}

static void codec_hevc_process_segment_header(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	union rpm_param *param = &hevc->rpm_param;

	if (param->p.first_slice_segment_in_pic_flag == 0) {
		hevc->slice_segment_addr = param->p.slice_segment_address;
		if (!param->p.dependent_slice_segment_flag)
			hevc->slice_addr = hevc->slice_segment_addr;
	} else {
		hevc->slice_segment_addr = 0;
		hevc->slice_addr = 0;
	}

	codec_hevc_update_pocs(sess);
}

static int codec_hevc_process_segment(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	struct amvdec_core *core = sess->core;
	union rpm_param *param = &hevc->rpm_param;
	u32 slice_segment_address = param->p.slice_segment_address;

	/* First slice: new frame */
	if (slice_segment_address == 0) {
		codec_hevc_update_referenced(hevc);
		codec_hevc_show_frames(sess);

		hevc->cur_frame = codec_hevc_prepare_new_frame(sess);
		if (!hevc->cur_frame)
			return -EAGAIN;
	} else {
		if (!hevc->cur_frame || hevc->cur_frame->cur_slice_idx >= MAX_SLICE_NUM - 1)
			return -EINVAL;
		hevc->cur_frame->cur_slice_idx++;
	}

	if (codec_hevc_update_frame_refs(sess, hevc->cur_frame))
		return -EINVAL;
	codec_hevc_update_col_frame(hevc);
	codec_hevc_update_ldc_flag(hevc);

	codec_hevc_set_mc(sess, hevc->cur_frame);
	codec_hevc_set_mcrcc(sess);
	codec_hevc_set_mpred(sess, hevc->cur_frame, hevc->col_frame);
	codec_hevc_set_sao(sess, hevc->cur_frame);

	amvdec_write_dos_bits(core, HEVC_WAIT_FLAG, BIT(1));
	amvdec_write_dos(core, HEVC_DEC_STATUS_REG,
			 HEVC_CODED_SLICE_SEGMENT_DAT);

	/* Interrupt the firmware's processor */
	amvdec_write_dos(core, HEVC_MCPU_INTR_REQ, AMRISC_MAIN_REQ);

	return 0;
}

static int codec_hevc_process_rpm(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	union rpm_param *param = &hevc->rpm_param;
	int src_changed = 0;
	u32 dst_width, dst_height;
	u32 lcu_size;
	u32 is_10bit = 0;

	if (param->p.slice_segment_address	||
	    !param->p.pic_width_in_luma_samples	||
	    !param->p.pic_height_in_luma_samples)
		return 0;

	if (param->p.bit_depth)
		return -EOPNOTSUPP; /* S805 linear path supports Main 8-bit. */
	if (param->p.pic_width_in_luma_samples > sess->fmt_out->max_width ||
	    param->p.pic_height_in_luma_samples > sess->fmt_out->max_height ||
	    param->p.log2_min_coding_block_size_minus3 > 3 ||
	    param->p.log2_diff_max_min_coding_block_size > 3 ||
	    param->p.log2_max_pic_order_cnt_lsb_minus4 > 12 ||
	    param->p.num_ref_idx_l0_active > MAX_REF_ACTIVE ||
	    param->p.num_ref_idx_l1_active > MAX_REF_ACTIVE ||
	    param->p.slice_type > I_SLICE ||
	    !(param->p.m_temporalId & 7) ||
	    param->p.collocated_from_l0_flag > 1 ||
	    param->p.log2_parallel_merge_level > 6 ||
	    param->p.five_minus_max_num_merge_cand > 4 ||
	    (param->p.tiles_flags & 1) ||
	    param->p.num_tile_columns_minus1 >= 4 ||
	    param->p.num_tile_rows_minus1 >= 4)
		return -EINVAL;

	hevc->width = param->p.pic_width_in_luma_samples;
	hevc->height = param->p.pic_height_in_luma_samples;
	dst_width = hevc->width;
	dst_height = hevc->height;
	sess->crop_left = 0;
	sess->crop_top = 0;
	/*
	 * The conformance window only applies to the SPS this picture uses.
	 * Input inspection runs ahead of the decoder, so hevc->sps can already
	 * be the next segment's; once a coded size is resolved, keep its window
	 * instead of reporting the uncropped size as a source change.
	 */
	if (hevc->applied.coded_width == hevc->width &&
	    hevc->applied.coded_height == hevc->height) {
		dst_width = hevc->applied.width;
		dst_height = hevc->applied.height;
		sess->crop_left = hevc->applied.left;
		sess->crop_top = hevc->applied.top;
	} else if (hevc->sps.coded_width == hevc->width &&
		   hevc->sps.coded_height == hevc->height) {
		hevc->applied = hevc->sps;
		dst_width = hevc->applied.width;
		dst_height = hevc->applied.height;
		sess->crop_left = hevc->applied.left;
		sess->crop_top = hevc->applied.top;
	} else {
		/* New coded size: no window resolved for it yet. */
		memset(&hevc->applied, 0, sizeof(hevc->applied));
	}
	sess->reference_width = hevc->width;
	sess->reference_height = hevc->height;

	lcu_size = 1 << (param->p.log2_min_coding_block_size_minus3 +
		   3 + param->p.log2_diff_max_min_coding_block_size);
	if (lcu_size < 16 || lcu_size > 64 ||
	    param->p.sps_num_reorder_pics_0 > 6)
		return -EINVAL;
	hevc->dpb_size = max_t(u32, 6, param->p.sps_num_reorder_pics_0 + 6);
	dev_dbg(sess->core->dev, "HEVC RPM size %ux%u poc %u slice %u first %u reorder %u dpb %u\n",
		 hevc->width, hevc->height, param->p.POClsb, param->p.slice_type,
		 param->p.first_slice_segment_in_pic_flag, param->p.sps_num_reorder_pics_0,
		 hevc->dpb_size);

	hevc->lcu_x_num = (hevc->width + lcu_size - 1) / lcu_size;
	hevc->lcu_y_num = (hevc->height + lcu_size - 1) / lcu_size;
	hevc->lcu_total = hevc->lcu_x_num * hevc->lcu_y_num;

	if (dst_width != hevc->dst_width ||
	    dst_height != hevc->dst_height ||
	    lcu_size != hevc->lcu_size ||
	    is_10bit != hevc->is_10bit)
		src_changed = 1;

	hevc->dst_width = dst_width;
	hevc->dst_height = dst_height;
	hevc->lcu_size = lcu_size;
	hevc->is_10bit = is_10bit;

	return src_changed;
}

/*
 * The RPM section within the workspace contains
 * many information regarding the parsed bitstream
 */
static void codec_hevc_fetch_rpm(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	u16 *rpm_vaddr = hevc->workspace_vaddr + RPM_OFFSET;
	int i, j;

	for (i = 0; i < RPM_SIZE; i += 4) {
		for (j = 0; j < 4; j++)
			hevc->rpm_param.l.data[i + j] =
				rpm_vaddr[i + 3 - j];
	}
}

static int codec_hevc_inspect_input(struct amvdec_session *sess, struct vb2_buffer *vb)
{
	struct codec_hevc *hevc = sess->priv;
	const u8 *data = vb2_plane_vaddr(vb, 0);
	u32 length = vb2_get_plane_payload(vb, 0), i;
	mod_delayed_work(system_wq, &hevc->watchdog, msecs_to_jiffies(HEVC_WATCHDOG_MS));

	for (i = 2; i + 2 < length; i++) {
		u32 end;
		struct hevc_sps_geometry geometry;
		int ret;

		if (data[i - 2] || data[i - 1] || data[i] != 1 ||
		    ((data[i + 1] >> 1) & 63) != 33)
			continue;
		for (end = i + 4; end + 2 < length; end++)
			if (!data[end] && !data[end + 1] && data[end + 2] == 1)
				break;
		if (end + 2 >= length)
			end = length;
		ret = hevc_parse_sps_geometry(data + i + 1, end - i - 1, &geometry);
		if (ret)
			return ret;
		mutex_lock(&hevc->lock);
		hevc->sps = geometry;
		mutex_unlock(&hevc->lock);
	}
	return 0;
}

static void codec_hevc_capture_queued(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	int ret;

	if (!hevc)
		return;
	mutex_lock(&hevc->lock);
	if (hevc->waiting_for_buffer) {
		ret = codec_hevc_process_segment(sess);
		if (!ret)
			hevc->waiting_for_buffer = false;
		else if (ret != -EAGAIN)
			amvdec_abort(sess);
	}
	mutex_unlock(&hevc->lock);
}

static void codec_hevc_resume(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	struct amvdec_core *core = sess->core;
	const struct amvdec_format *fmt_out = sess->fmt_out;
	u32 count = clamp(sess->dpb_size, fmt_out->min_buffers,
			  fmt_out->max_buffers);
	dma_addr_t address[2] = {};
	unsigned int i;
	int ret;

	/* The DPB lives in frame pool slots; slot n uses canvases 2n, 2n+1. */
	ret = amvdec_pool_prepare(sess, count);
	if (ret < 0) {
		amvdec_abort(sess);
		return;
	}
	if (ret)
		codec_hevc_forget_frames(hevc);

	amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR, 0);
	for (i = 0; i < count; i++) {
		const struct amvdec_shadow *slot = amvdec_pool_slot(sess, i);

		address[0] = slot->paddr;
		address[1] = slot->paddr + slot->luma;
		amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR,
				 address[0] | (i * 2 << 8) | 1);
		amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR,
				 address[1] | ((i * 2 + 1) << 8) | 1);
	}
	for (i = count; i < 24; i++) {
		amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR,
				 address[0] | ((count - 1) * 2 << 8) | 1);
		amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR,
				 address[1] | (((count - 1) * 2 + 1) << 8) | 1);
	}
	amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR, 1);
	amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, 1);
	for (i = 0; i < 32; i++)
		amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR, 0);
	codec_hevc_process_segment_header(sess);
	{
		int ret = codec_hevc_process_segment(sess);
		if (ret == -EAGAIN)
			hevc->waiting_for_buffer = true;
		else if (ret)
			amvdec_abort(sess);
	}
}

static irqreturn_t codec_hevc_threaded_isr(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_hevc *hevc = sess->priv;
	u32 dec_status = amvdec_read_dos(core, HEVC_DEC_STATUS_REG);

	if (!hevc)
		return IRQ_HANDLED;

	mutex_lock(&hevc->lock);
	/* The firmware made progress: the watchdog starts over. */
	hevc->client_waited = false;
	if (dec_status == HEVC_NAL_SEARCH_DONE &&
	    amvdec_read_dos(core, HEVC_ASSIST_SCRATCH_J) == 36) {
		dev_dbg(core->dev, "HEVC firmware EOS: pending pictures %u\n", hevc->frames_num);
		hevc->eos_seen = true;
		codec_hevc_flush_output(sess);
		codec_hevc_finish_eos(sess);
		goto unlock;
	}
	if (dec_status != HEVC_SLICE_SEGMENT_DONE) {
		dev_err(core->dev_dec, "Unrecognized dec_status: %08X\n",
			dec_status);
		amvdec_abort(sess);
		goto unlock;
	}

	sess->keyframe_found = 1;
	codec_hevc_fetch_rpm(sess);
	{
		int ret = codec_hevc_process_rpm(sess);
		if (ret < 0) {
			amvdec_abort(sess);
			goto unlock;
		}
		if (ret > 0) {
			/* The old segment's pictures are still owed to
			 * userspace, and only its geometry can copy them.
			 * Deliver them now; the last one ends that stream,
			 * which the client drains before it renegotiates. */
			struct hevc_frame *last = codec_hevc_show_pending(sess, true);
			enum amvdec_status status = sess->status;

			sess->status = STATUS_NEEDS_RESUME;
			if (last)
				codec_hevc_deliver_frame(sess, last);
			sess->status = status;
			codec_hevc_forget_frames(hevc);
			amvdec_src_change(sess, hevc->dst_width, hevc->dst_height,
					 hevc->dpb_size);
			goto unlock;
		}
	}

	codec_hevc_process_segment_header(sess);
	{
		int ret = codec_hevc_process_segment(sess);
		if (ret == -EAGAIN)
			hevc->waiting_for_buffer = true;
		else if (ret)
			amvdec_abort(sess);
	}

unlock:
	mutex_unlock(&hevc->lock);
	return IRQ_HANDLED;
}

static irqreturn_t codec_hevc_isr(struct amvdec_session *sess)
{
	struct codec_hevc *hevc = sess->priv;
	if (hevc)
		mod_delayed_work(system_wq, &hevc->watchdog, msecs_to_jiffies(HEVC_WATCHDOG_MS));
	amvdec_write_dos(sess->core, HEVC_ASSIST_MBOX1_CLR_REG, 1);
	return IRQ_WAKE_THREAD;
}

struct amvdec_codec_ops codec_hevc_ops = {
	.start = codec_hevc_start,
	.stop = codec_hevc_stop,
	.isr = codec_hevc_isr,
	.threaded_isr = codec_hevc_threaded_isr,
	.num_pending_bufs = codec_hevc_num_pending_bufs,
	.eos_sequence = codec_hevc_eos_sequence,
	.firmware_eos = true,
	.capture_queued = codec_hevc_capture_queued,
	.inspect_input = codec_hevc_inspect_input,
	.resume = codec_hevc_resume,
};
