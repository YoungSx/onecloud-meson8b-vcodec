// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 * Copyright (C) 2026 meson8b-vcodec port from the Hardkernel 3.10
 * vendor vmpeg4.c (Tim Yao <timyao@amlogic.com>)
 */

#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "codec_mpeg4.h"
#include "dos_regs.h"
#include "esparser.h"
#include "vdec_helpers.h"

/* Size of the vendor's MPEG4 decoder workspace (one SD decode slot) */
#define SIZE_WORKSPACE		SZ_1M

/*
 * The ucode addresses its workspace at ORI_BUFFER_START_ADDR + 4 MB
 * (the slot after the four SD decode buffers of the vendor memory map)
 * and translates to a physical address by adding MEM_OFFSET_REG.
 */
#define UCODE_WORKSPACE_BASE	0x01400000

/* map firmware registers to known MPEG4 functions (vendor vmpeg4.c) */
#define MP4_RATE		AV_SCRATCH_3
#define MP4_PIC_RATIO		AV_SCRATCH_5
#define MP4_ERR_COUNT		AV_SCRATCH_6
#define MP4_PIC_WH		AV_SCRATCH_7
#define MREG_BUFFERIN		AV_SCRATCH_8
#define MREG_BUFFEROUT		AV_SCRATCH_9
#define MP4_NOT_CODED_CNT	AV_SCRATCH_A
#define MP4_VOP_TIME_INC	AV_SCRATCH_B
#define MP4_OFFSET_REG		AV_SCRATCH_C
#define MEM_OFFSET_REG		AV_SCRATCH_F

#define INTERLACE_FLAG		BIT(7)
#define BOTTOM_FIELD_FIRST_FLAG	BIT(6)

#define MP4_NUM_BUFFERS		4

struct codec_mpeg4 {
	/* Buffer for the MPEG4 decoder's state */
	void	  *workspace_vaddr;
	dma_addr_t workspace_paddr;

	struct amvdec_session *sess;
	struct delayed_work drain_work;
	bool draining;
};

static const u8 eos_sequence[SZ_1K] = { 0x00, 0x00, 0x01, 0xB1 };

static int codec_mpeg4_can_recycle(struct amvdec_core *core)
{
	return !amvdec_read_dos(core, MREG_BUFFERIN);
}

static void codec_mpeg4_recycle(struct amvdec_core *core, u32 buf_idx)
{
	amvdec_write_dos(core, MREG_BUFFERIN, ~(1 << buf_idx));
}

static void codec_mpeg4_drain_work(struct work_struct *work);

static int codec_mpeg4_start(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_mpeg4 *mpeg4;
	int ret;

	/* Vendor vmpeg4_prot_init() reset sequence for M8B */
	if (core->platform->revision == VDEC_REVISION_MESON8B) {
		amvdec_write_dos(core, DOS_SW_RESET0, BIT(7) | BIT(6));
		amvdec_write_dos(core, DOS_SW_RESET0, 0);
	}

	mpeg4 = kzalloc(sizeof(*mpeg4), GFP_KERNEL);
	if (!mpeg4)
		return -ENOMEM;

	mpeg4->workspace_vaddr = dma_alloc_coherent(core->dev, SIZE_WORKSPACE,
						    &mpeg4->workspace_paddr,
						    GFP_KERNEL);
	if (!mpeg4->workspace_vaddr) {
		dev_err(core->dev, "Failed to request MPEG4 workspace\n");
		ret = -ENOMEM;
		goto free_mpeg4;
	}

	amvdec_write_dos(core, POWER_CTL_VLD, BIT(4));
	amvdec_write_dos(core, M4_CONTROL_REG, 0);
	amvdec_write_dos(core, PSCALE_CTRL, 0);
	amvdec_write_dos(core, MREG_BUFFERIN, 0);
	amvdec_write_dos(core, MREG_BUFFEROUT, 0);
	amvdec_write_dos(core, MP4_NOT_CODED_CNT, 0);
	amvdec_write_dos(core, MP4_PIC_WH,
			 (sess->width << 16) | sess->height);
	amvdec_write_dos(core, MDEC_PIC_DC_THRESH, 0x404038aa);

	INIT_DELAYED_WORK(&mpeg4->drain_work, codec_mpeg4_drain_work);
	mpeg4->sess = sess;

	sess->keyframe_found = 1;
	sess->priv = mpeg4;

	return 0;

free_mpeg4:
	kfree(mpeg4);

	return ret;
}

/* Like MPEG1/2: the firmware snapshots the AV_SCRATCH canvas table
 * when its processor is released, so program the canvases and restart
 * it at the first capture STREAMON (vendor amvdec_start sequence).
 * See codec_mpeg12_resume for the full rationale.
 */
static void codec_mpeg4_resume(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_mpeg4 *mpeg4 = sess->priv;

	if (amvdec_set_canvases(sess, (u32[]){ AV_SCRATCH_0, 0 },
				(u32[]){ MP4_NUM_BUFFERS, 0 })) {
		amvdec_abort(sess);
		return;
	}

	/* tell the ucode where its workspace really lives */
	amvdec_write_dos(core, MEM_OFFSET_REG,
			 mpeg4->workspace_paddr - UCODE_WORKSPACE_BASE);

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

/* Same firmware behaviour as MPEG1/2: the last picture completes only
 * when the next one starts. Visual object sequence end code (0xB1),
 * then flush once IRQs go quiet. See codec_mpeg12_drain_work.
 */
static void codec_mpeg4_drain_work(struct work_struct *work)
{
	struct codec_mpeg4 *mpeg4 =
		container_of(work, struct codec_mpeg4, drain_work.work);
	struct amvdec_session *sess = mpeg4->sess;
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
	mpeg4->draining = false;
}

static void codec_mpeg4_drain(struct amvdec_session *sess)
{
	struct codec_mpeg4 *mpeg4 = sess->priv;

	if (mpeg4->draining)
		return;

	/* Visual object sequence end */
	esparser_queue_eos(sess->core, eos_sequence, 4);

	mpeg4->draining = true;
	schedule_delayed_work(&mpeg4->drain_work, 0);
}

static int codec_mpeg4_stop(struct amvdec_session *sess)
{
	struct codec_mpeg4 *mpeg4 = sess->priv;
	struct amvdec_core *core = sess->core;

	cancel_delayed_work_sync(&mpeg4->drain_work);

	if (mpeg4->workspace_vaddr)
		dma_free_coherent(core->dev, SIZE_WORKSPACE,
				  mpeg4->workspace_vaddr,
				  mpeg4->workspace_paddr);

	return 0;
}

static irqreturn_t codec_mpeg4_threaded_isr(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	u32 reg;
	u32 buffer_index;
	u32 field = V4L2_FIELD_NONE;
	u32 offset;

	amvdec_write_dos(core, ASSIST_MBOX1_CLR_REG, 1);
	reg = amvdec_read_dos(core, MREG_BUFFEROUT);
	if (!reg)
		return IRQ_HANDLED;

	buffer_index = ((reg & 0x7) - 1) & (MP4_NUM_BUFFERS - 1);
	offset = amvdec_read_dos(core, MP4_OFFSET_REG);

	if (reg & INTERLACE_FLAG)
		field = (reg & BOTTOM_FIELD_FIRST_FLAG) ?
			V4L2_FIELD_INTERLACED_BT :
			V4L2_FIELD_INTERLACED_TB;

	amvdec_dst_buf_done_idx(sess, buffer_index, offset, field);

	amvdec_write_dos(core, MREG_BUFFEROUT, 0);
	return IRQ_HANDLED;
}

static irqreturn_t codec_mpeg4_isr(struct amvdec_session *sess)
{
	return IRQ_WAKE_THREAD;
}

struct amvdec_codec_ops codec_mpeg4_ops = {
	.start = codec_mpeg4_start,
	.stop = codec_mpeg4_stop,
	.isr = codec_mpeg4_isr,
	.threaded_isr = codec_mpeg4_threaded_isr,
	.can_recycle = codec_mpeg4_can_recycle,
	.recycle = codec_mpeg4_recycle,
	.drain = codec_mpeg4_drain,
	.resume = codec_mpeg4_resume,
};
