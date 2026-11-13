// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 * Copyright (C) 2026 meson8b-vcodec port from the Hardkernel 3.10
 * vendor vvc1.c (Qi Wang <qi.wang@amlogic.com>)
 */

#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "codec_vc1.h"
#include "dos_regs.h"
#include "esparser.h"
#include "vdec_helpers.h"

/*
 * The ucode addresses its workspace at DCAC_BUFF_START_ADDR and
 * translates to a physical address by adding MEM_OFFSET_REG
 * (GXL vvc1.c model; the M8 vendor driver instead packs the
 * workspace behind the four decode slots inside the reserved
 * video region).
 */
#define UCODE_WORKSPACE_BASE	0x01f00000

/* Vendor GXL vvc1.c: 2 MB workspace, unlike MPEG-4's 1 MB */
#define SIZE_WORKSPACE		(2 * SZ_1M)

/* map firmware registers to known VC1 functions (vendor vvc1.c) */
#define VC1_PIC_RATIO		AV_SCRATCH_0
#define VC1_ERROR_COUNT		AV_SCRATCH_6
#define VC1_SOS_COUNT		AV_SCRATCH_7
#define VC1_BUFFERIN		AV_SCRATCH_8
#define VC1_BUFFEROUT		AV_SCRATCH_9
#define VC1_REPEAT_COUNT	AV_SCRATCH_A
#define VC1_TIME_STAMP		AV_SCRATCH_B
#define VC1_OFFSET_REG		AV_SCRATCH_C
#define MEM_OFFSET_REG		AV_SCRATCH_F

/* WMV3 (simple/main profile) = 0, WVC1 (advanced profile) = 1 */
#define VC1_FORMAT_REG		AV_SCRATCH_4

#define INTERLACE_FLAG		BIT(7)
#define BOTTOM_FIELD_FIRST_FLAG	BIT(6)

#define VC1_NUM_BUFFERS		4

struct codec_vc1 {
	/* Buffer for the VC1 decoder's state */
	void	  *workspace_vaddr;
	dma_addr_t workspace_paddr;

	struct amvdec_session *sess;
	struct delayed_work drain_work;
	bool draining;
};

static const u8 eos_sequence[SZ_1K] = { 0x00, 0x00, 0x01, 0x0A };

static int codec_vc1_can_recycle(struct amvdec_core *core)
{
	return !amvdec_read_dos(core, VC1_BUFFERIN);
}

static void codec_vc1_recycle(struct amvdec_core *core, u32 buf_idx)
{
	amvdec_write_dos(core, VC1_BUFFERIN, ~(1 << buf_idx));
}

static void codec_vc1_drain_work(struct work_struct *work);

static int codec_vc1_start(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_vc1 *vc1;
	int ret;

	/* Vendor vvc1_prot_init() reset sequence for M8B */
	if (core->platform->revision == VDEC_REVISION_MESON8B) {
		amvdec_write_dos(core, DOS_SW_RESET0, BIT(7) | BIT(6) | BIT(4));
		amvdec_write_dos(core, DOS_SW_RESET0, 0);
		amvdec_read_dos(core, DOS_SW_RESET0);
		amvdec_write_dos(core, DOS_SW_RESET0, BIT(7) | BIT(6) | BIT(4));
		amvdec_write_dos(core, DOS_SW_RESET0, 0);
		amvdec_write_dos(core, DOS_SW_RESET0, BIT(9) | BIT(8));
		amvdec_write_dos(core, DOS_SW_RESET0, 0);
	}

	vc1 = kzalloc(sizeof(*vc1), GFP_KERNEL);
	if (!vc1)
		return -ENOMEM;

	vc1->workspace_vaddr = dma_alloc_coherent(core->dev, SIZE_WORKSPACE,
						  &vc1->workspace_paddr,
						  GFP_KERNEL);
	if (!vc1->workspace_vaddr) {
		dev_err(core->dev, "Failed to request VC1 workspace\n");
		ret = -ENOMEM;
		goto free_vc1;
	}

	amvdec_write_dos(core, POWER_CTL_VLD, BIT(4));
	/* vendor vvc1 raises the VIFIFO fill level (2/8) unlike mpeg12/4 */
	amvdec_write_dos_bits(core, VLD_MEM_VIFIFO_CONTROL,
			      (2 << MEM_FIFO_CNT_BIT) | (8 << 18));
	amvdec_write_dos(core, PSCALE_CTRL, 0);
	/* advanced profile (WVC1) stream format; 0 would select WMV3 */
	amvdec_write_dos(core, VC1_FORMAT_REG, 1);
	amvdec_write_dos(core, VC1_SOS_COUNT, 0);
	amvdec_write_dos(core, VC1_BUFFERIN, 0);
	amvdec_write_dos(core, VC1_BUFFEROUT, 0);

	INIT_DELAYED_WORK(&vc1->drain_work, codec_vc1_drain_work);
	vc1->sess = sess;

	sess->keyframe_found = 1;
	sess->priv = vc1;

	return 0;

free_vc1:
	kfree(vc1);

	return ret;
}

/* Like MPEG1/2 and MPEG-4: the firmware snapshots the AV_SCRATCH canvas
 * table when its processor is released, so program the canvases and
 * restart it at the first capture STREAMON (vendor amvdec_start
 * sequence). See codec_mpeg12_resume for the full rationale.
 */
static void codec_vc1_resume(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_vc1 *vc1 = sess->priv;

	if (amvdec_set_canvases(sess, (u32[]){ AV_SCRATCH_0, 0 },
				(u32[]){ VC1_NUM_BUFFERS, 0 })) {
		amvdec_abort(sess);
		return;
	}

	/* tell the ucode where its workspace really lives */
	amvdec_write_dos(core, MEM_OFFSET_REG,
			 vc1->workspace_paddr - UCODE_WORKSPACE_BASE);

	if (core->platform->revision == VDEC_REVISION_MESON8B) {
		amvdec_write_dos(core, MPSR, 0);
		amvdec_write_dos(core, CPSR, 0);
		amvdec_read_dos(core, DOS_SW_RESET0);
		amvdec_read_dos(core, DOS_SW_RESET0);
		amvdec_read_dos(core, DOS_SW_RESET0);
		amvdec_write_dos(core, DOS_SW_RESET0, BIT(12) | BIT(11));
		amvdec_write_dos(core, DOS_SW_RESET0, 0);
		amvdec_read_dos(core, DOS_SW_RESET0);
		amvdec_read_dos(core, DOS_SW_RESET0);
		amvdec_read_dos(core, DOS_SW_RESET0);
		amvdec_write_dos(core, MPSR, 1);
	}
}

/* Same firmware behaviour as MPEG1/2 and MPEG-4: the last picture
 * completes only when the next one starts. End-of-sequence code
 * (0x0A), then flush once IRQs go quiet. See codec_mpeg12_drain_work.
 */
static void codec_vc1_drain_work(struct work_struct *work)
{
	struct codec_vc1 *vc1 =
		container_of(work, struct codec_vc1, drain_work.work);
	struct amvdec_session *sess = vc1->sess;
	static const u32 max_polls = 150; /* 3 s at 20 ms */
	u32 polls = 0;

	while (polls++ < max_polls) {
		bool irq_quiet = !time_is_after_jiffies64(
			sess->last_irq_jiffies + msecs_to_jiffies(100));
		int in_flight = atomic_read(&sess->esparser_queued_bufs);

		if (irq_quiet && in_flight <= 1)
			break;

		msleep(20);
	}

	amvdec_flush_capture_buffers(sess);
	vc1->draining = false;
}

static void codec_vc1_drain(struct amvdec_session *sess)
{
	struct codec_vc1 *vc1 = sess->priv;

	if (vc1->draining)
		return;

	/* VC1 end-of-sequence */
	esparser_queue_eos(sess->core, eos_sequence, 4);

	vc1->draining = true;
	schedule_delayed_work(&vc1->drain_work, 0);
}

static int codec_vc1_stop(struct amvdec_session *sess)
{
	struct codec_vc1 *vc1 = sess->priv;
	struct amvdec_core *core = sess->core;

	cancel_delayed_work_sync(&vc1->drain_work);

	if (vc1->workspace_vaddr)
		dma_free_coherent(core->dev, SIZE_WORKSPACE,
				  vc1->workspace_vaddr,
				  vc1->workspace_paddr);

	return 0;
}

static irqreturn_t codec_vc1_threaded_isr(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	u32 reg;
	u32 buffer_index;
	u32 field = V4L2_FIELD_NONE;
	u32 offset;

	amvdec_write_dos(core, ASSIST_MBOX1_CLR_REG, 1);
	reg = amvdec_read_dos(core, VC1_BUFFEROUT);
	if (!reg)
		return IRQ_HANDLED;

	buffer_index = ((reg & 0x7) - 1) & (VC1_NUM_BUFFERS - 1);
	offset = amvdec_read_dos(core, VC1_OFFSET_REG);

	if (reg & INTERLACE_FLAG)
		field = (reg & BOTTOM_FIELD_FIRST_FLAG) ?
			V4L2_FIELD_INTERLACED_BT :
			V4L2_FIELD_INTERLACED_TB;

	amvdec_dst_buf_done_idx(sess, buffer_index, offset, field);

	amvdec_write_dos(core, VC1_BUFFEROUT, 0);
	return IRQ_HANDLED;
}

static irqreturn_t codec_vc1_isr(struct amvdec_session *sess)
{
	return IRQ_WAKE_THREAD;
}

struct amvdec_codec_ops codec_vc1_ops = {
	.start = codec_vc1_start,
	.stop = codec_vc1_stop,
	.isr = codec_vc1_isr,
	.threaded_isr = codec_vc1_threaded_isr,
	.can_recycle = codec_vc1_can_recycle,
	.recycle = codec_vc1_recycle,
	.drain = codec_vc1_drain,
	.resume = codec_vc1_resume,
};
