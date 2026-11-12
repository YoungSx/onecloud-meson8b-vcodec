// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 */

#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "codec_mpeg12.h"
#include "dos_regs.h"
#include "esparser.h"
#include "vdec_helpers.h"

#define SIZE_WORKSPACE		SZ_128K
/* Offset substracted by the firmware from the workspace paddr */
#define WORKSPACE_OFFSET	(5 * SZ_1K)

/* map firmware registers to known MPEG1/2 functions */
#define MREG_SEQ_INFO		AV_SCRATCH_4
	#define MPEG2_SEQ_DAR_MASK	GENMASK(3, 0)
	#define MPEG2_DAR_4_3		2
	#define MPEG2_DAR_16_9		3
	#define MPEG2_DAR_221_100	4
#define MREG_PIC_INFO		AV_SCRATCH_5
#define MREG_PIC_WIDTH		AV_SCRATCH_6
#define MREG_PIC_HEIGHT		AV_SCRATCH_7
#define MREG_BUFFERIN		AV_SCRATCH_8
#define MREG_BUFFEROUT		AV_SCRATCH_9
#define MREG_CMD		AV_SCRATCH_A
#define MREG_CO_MV_START	AV_SCRATCH_B
#define MREG_ERROR_COUNT	AV_SCRATCH_C
#define MREG_FRAME_OFFSET	AV_SCRATCH_D
#define MREG_WAIT_BUFFER	AV_SCRATCH_E
#define MREG_FATAL_ERROR	AV_SCRATCH_F

#define PICINFO_PROG		0x00008000
#define PICINFO_TOP_FIRST	0x00002000

struct codec_mpeg12 {
	/* Buffer for the MPEG1/2 Workspace */
	void	  *workspace_vaddr;
	dma_addr_t workspace_paddr;

	struct amvdec_session *sess;
	struct delayed_work drain_work;
	bool draining;
};

static const u8 eos_sequence[SZ_1K] = { 0x00, 0x00, 0x01, 0xB7 };

static int codec_mpeg12_can_recycle(struct amvdec_core *core)
{
	return !amvdec_read_dos(core, MREG_BUFFERIN);
}

static void codec_mpeg12_recycle(struct amvdec_core *core, u32 buf_idx)
{
	amvdec_write_dos(core, MREG_BUFFERIN, buf_idx + 1);
}

static void codec_mpeg12_drain_work(struct work_struct *work);

static int codec_mpeg12_start(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_mpeg12 *mpeg12;
	int ret;

	/* Match the vendor vmpeg12_prot_init() reset sequence: GXL
	 * resets the decode blocks inside its own power-up path,
	 * while M8B needs the explicit DOS_SW_RESET0/MDEC_SW_RESET pulses
	 * before the firmware processor runs.
	 */
	if (core->platform->revision == VDEC_REVISION_MESON8B) {
		amvdec_write_dos(core, DOS_SW_RESET0, BIT(7) | BIT(6) | BIT(4));
		amvdec_write_dos(core, DOS_SW_RESET0, 0);
		amvdec_write_dos(core, MDEC_SW_RESET, BIT(7));
		amvdec_write_dos(core, MDEC_SW_RESET, 0);
	}

	mpeg12 = kzalloc(sizeof(*mpeg12), GFP_KERNEL);
	if (!mpeg12)
		return -ENOMEM;

	/* Allocate some memory for the MPEG1/2 decoder's state */
	mpeg12->workspace_vaddr = dma_alloc_coherent(core->dev, SIZE_WORKSPACE,
						     &mpeg12->workspace_paddr,
						     GFP_KERNEL);
	if (!mpeg12->workspace_vaddr) {
		dev_err(core->dev, "Failed to request MPEG 1/2 Workspace\n");
		ret = -ENOMEM;
		goto free_mpeg12;
	}

	amvdec_write_dos(core, POWER_CTL_VLD, BIT(4));
	amvdec_write_dos(core, MREG_CO_MV_START,
			 mpeg12->workspace_paddr + WORKSPACE_OFFSET);

	amvdec_write_dos(core, MPEG1_2_REG, 0);
	amvdec_write_dos(core, PSCALE_CTRL, 0);
	amvdec_write_dos(core, PIC_HEAD_INFO, 0x380);
	amvdec_write_dos(core, M4_CONTROL_REG, 0);
	amvdec_write_dos(core, MREG_BUFFERIN, 0);
	amvdec_write_dos(core, MREG_BUFFEROUT, 0);
	amvdec_write_dos(core, MREG_CMD, (sess->width << 16) | sess->height);
	amvdec_write_dos(core, MREG_ERROR_COUNT, 0);
	amvdec_write_dos(core, MREG_FATAL_ERROR, 0);
	amvdec_write_dos(core, MREG_WAIT_BUFFER, 0);

	INIT_DELAYED_WORK(&mpeg12->drain_work, codec_mpeg12_drain_work);
	mpeg12->sess = sess;

	sess->keyframe_found = 1;
	sess->priv = mpeg12;

	return 0;


free_mpeg12:
	kfree(mpeg12);

	return ret;
}

/* The canvas list must be programmed only once userspace has allocated
 * and queued its capture buffers (stateful m2m: that happens after
 * STREAMON(OUTPUT)). Unlike H.264, the firmware raises no source-change
 * event, so this runs from the first STREAMON(CAPTURE) via the resume
 * gate in vdec_start_streaming.
 *
 * The firmware snapshots the AV_SCRATCH canvas table when its processor
 * is released (MPSR = 1), which already happened at STREAMON(OUTPUT)
 * with the table still empty. No ES data is fed before this point
 * (esparser gate in esparser_queue_all), so restarting the processor
 * re-runs the snapshot against the table programmed above without
 * losing decode state. Vendor amvdec_start() restart sequence.
 */
static void codec_mpeg12_resume(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	if (amvdec_set_canvases(sess, (u32[]){ AV_SCRATCH_0, 0 },
				(u32[]){ 8, 0 })) {
		amvdec_abort(sess);
		return;
	}

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

/* The m8 firmware emits a picture only once the next one starts, so at
 * end of stream the final picture is never completed by hardware.
 * Watch from a work context (the drain ioctl must not block: while it
 * slept, userspace could not recycle capture buffers and the 8 decode
 * canvases ran dry). Once IRQs go quiet and no input remains in flight,
 * hand the queued capture buffers back as an empty LAST frame + EOS.
 */
static void codec_mpeg12_drain_work(struct work_struct *work)
{
	struct codec_mpeg12 *mpeg12 =
		container_of(work, struct codec_mpeg12, drain_work.work);
	struct amvdec_session *sess = mpeg12->sess;
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
	mpeg12->draining = false;
}

static void codec_mpeg12_drain(struct amvdec_session *sess)
{
	struct codec_mpeg12 *mpeg12 = sess->priv;

	if (mpeg12->draining)
		return;

	/* Sequence end: prompts the firmware to emit the last decoded
	 * picture (it only completes one when the next picture starts).
	 */
	esparser_queue_eos(sess->core, eos_sequence, 4);

	mpeg12->draining = true;
	schedule_delayed_work(&mpeg12->drain_work, 0);
}

static int codec_mpeg12_stop(struct amvdec_session *sess)
{
	struct codec_mpeg12 *mpeg12 = sess->priv;
	struct amvdec_core *core = sess->core;

	cancel_delayed_work_sync(&mpeg12->drain_work);

	if (mpeg12->workspace_vaddr)
		dma_free_coherent(core->dev, SIZE_WORKSPACE,
				  mpeg12->workspace_vaddr,
				  mpeg12->workspace_paddr);

	return 0;
}

static void codec_mpeg12_update_dar(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	u32 seq = amvdec_read_dos(core, MREG_SEQ_INFO);
	u32 ar = seq & MPEG2_SEQ_DAR_MASK;

	switch (ar) {
	case MPEG2_DAR_4_3:
		amvdec_set_par_from_dar(sess, 4, 3);
		break;
	case MPEG2_DAR_16_9:
		amvdec_set_par_from_dar(sess, 16, 9);
		break;
	case MPEG2_DAR_221_100:
		amvdec_set_par_from_dar(sess, 221, 100);
		break;
	default:
		sess->pixelaspect.numerator = 1;
		sess->pixelaspect.denominator = 1;
		break;
	}
}

static irqreturn_t codec_mpeg12_threaded_isr(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	u32 reg;
	u32 pic_info;
	u32 is_progressive;
	u32 buffer_index;
	u32 field = V4L2_FIELD_NONE;
	u32 offset;

	amvdec_write_dos(core, ASSIST_MBOX1_CLR_REG, 1);
	reg = amvdec_read_dos(core, MREG_FATAL_ERROR);
	if (reg == 1) {
		dev_err(core->dev, "MPEG1/2 fatal error\n");
		amvdec_abort(sess);
		return IRQ_HANDLED;
	}

	reg = amvdec_read_dos(core, MREG_BUFFEROUT);
	if (!reg)
		return IRQ_HANDLED;

	/* Unclear what this means */
	if ((reg & GENMASK(23, 17)) == GENMASK(23, 17))
		goto end;

	pic_info = amvdec_read_dos(core, MREG_PIC_INFO);
	is_progressive = pic_info & PICINFO_PROG;

	if (!is_progressive)
		field = (pic_info & PICINFO_TOP_FIRST) ?
			V4L2_FIELD_INTERLACED_TB :
			V4L2_FIELD_INTERLACED_BT;

	codec_mpeg12_update_dar(sess);
	buffer_index = ((reg & 0xf) - 1) & 7;
	offset = amvdec_read_dos(core, MREG_FRAME_OFFSET);
	amvdec_dst_buf_done_idx(sess, buffer_index, offset, field);

end:
	amvdec_write_dos(core, MREG_BUFFEROUT, 0);
	return IRQ_HANDLED;
}

static irqreturn_t codec_mpeg12_isr(struct amvdec_session *sess)
{
	return IRQ_WAKE_THREAD;
}

struct amvdec_codec_ops codec_mpeg12_ops = {
	.start = codec_mpeg12_start,
	.stop = codec_mpeg12_stop,
	.isr = codec_mpeg12_isr,
	.threaded_isr = codec_mpeg12_threaded_isr,
	.can_recycle = codec_mpeg12_can_recycle,
	.recycle = codec_mpeg12_recycle,
	.drain = codec_mpeg12_drain,
	.resume = codec_mpeg12_resume,
};
