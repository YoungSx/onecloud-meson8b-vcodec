// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 * Copyright (C) 2026 meson8b-vcodec port from the Hardkernel 3.10
 * vendor vmjpeg.c (Tim Yao <timyao@amlogic.com>)
 */

#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "codec_mjpeg.h"
#include "dos_regs.h"
#include "esparser.h"
#include "vdec_helpers.h"

/* MJPEG needs no workspace: unlike mpeg12/mpeg4/vc1 the microcode keeps
 * all its state in scratch registers and LMEM (vendor vmjpeg.c).
 */

/* map firmware registers to known MJPEG functions (vendor vmjpeg.c) */
#define MREG_DECODE_PARAM	AV_SCRATCH_2
#define MREG_TO_AMRISC		AV_SCRATCH_8
#define MREG_FROM_AMRISC	AV_SCRATCH_9
#define MREG_FRAME_OFFSET	AV_SCRATCH_A

/* canvas snapshot table: one packed Y/U/V canvas id per slot,
 * written as v<<16 | v<<8 | y by set_canvas_nv12m
 */
#define MJPEG_CANVAS_TABLE	AV_SCRATCH_4

#define PICINFO_BUF_IDX_MASK	0x0007
#define PICINFO_INTERLACE	0x0020
#define PICINFO_INTERLACE_FIRST	0x0010

#define MJPEG_NUM_BUFFERS	4

struct codec_mjpeg {
	struct amvdec_session *sess;
	struct delayed_work drain_work;
	bool draining;
};

static int codec_mjpeg_can_recycle(struct amvdec_core *core)
{
	return !amvdec_read_dos(core, MREG_TO_AMRISC);
}

static void codec_mjpeg_recycle(struct amvdec_core *core, u32 buf_idx)
{
	amvdec_write_dos(core, MREG_TO_AMRISC, buf_idx + 1);
}

static void codec_mjpeg_drain_work(struct work_struct *work);

static int codec_mjpeg_start(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_mjpeg *mjpeg;

	/* Vendor vmjpeg_prot_init() reset sequence for M8B */
	if (core->platform->revision == VDEC_REVISION_MESON8B) {
		amvdec_write_dos(core, DOS_SW_RESET0, BIT(7) | BIT(6));
		amvdec_write_dos(core, DOS_SW_RESET0, 0);
	}

	mjpeg = kzalloc(sizeof(*mjpeg), GFP_KERNEL);
	if (!mjpeg)
		return -ENOMEM;

	INIT_DELAYED_WORK(&mjpeg->drain_work, codec_mjpeg_drain_work);
	mjpeg->sess = sess;

	sess->keyframe_found = 1;
	sess->priv = mjpeg;

	return 0;
}

/* Same firmware behaviour as MPEG1/2 and MPEG-4: the firmware snapshots
 * the AV_SCRATCH canvas table when its processor is released, so program
 * the four decode slots and restart it at the first capture STREAMON
 * (vendor amvdec_start sequence).
 *
 * MJPEG register setup that must precede the restart (vendor
 * vmjpeg_prot_init()):
 * - MREG_DECODE_PARAM = (height << 4) | 0x8000: the ucode gets the frame
 *   height and its buffer address mode from the host, there is no
 *   in-stream size like MPEG sequence headers.
 * - The PSCALE triangle filter table: the ucode runs every picture
 *   through the post-scaler, an unprogrammed BMEM yields corrupt output.
 */
static void codec_mjpeg_init_hw(struct amvdec_session *sess)
{
	static const u32 filt_coef[33] = {
		0x20402000, 0x20402000, 0x1f3f2101, 0x1f3f2101,
		0x1e3e2202, 0x1e3e2202, 0x1d3d2303, 0x1d3d2303,
		0x1c3c2404, 0x1c3c2404, 0x1b3b2505, 0x1b3b2505,
		0x1a3a2606, 0x1a3a2606, 0x19392707, 0x19392707,
		0x18382808, 0x18382808, 0x17372909, 0x17372909,
		0x16362a0a, 0x16362a0a, 0x15352b0b, 0x15352b0b,
		0x14342c0c, 0x14342c0c, 0x13332d0d, 0x13332d0d,
		0x12322e0e, 0x12322e0e, 0x11312f0f, 0x11312f0f,
		0x10303010,
	};
	struct amvdec_core *core = sess->core;
	int i;

	/* initial display-buffer fifo depth and pointer (vendor
	 * vmjpeg_prot_init: AV_SCRATCH_0/1)
	 */
	amvdec_write_dos(core, AV_SCRATCH_0, 12);
	amvdec_write_dos(core, AV_SCRATCH_1, 0x031a);

	/* tell the ucode the frame geometry (no stream headers to parse) */
	amvdec_write_dos(core, MREG_DECODE_PARAM,
			 (sess->height << 4) | 0x8000);

	/* pscale enable + PSCALE cbus bmem enable; 4-point triangle */
	amvdec_write_dos(core, PSCALE_CTRL, 0xc000);
	amvdec_write_dos(core, PSCALE_BMEM_ADDR, 0);
	for (i = 0; i < 33; i++) {
		amvdec_write_dos(core, PSCALE_BMEM_DAT, 0);
		amvdec_write_dos(core, PSCALE_BMEM_DAT, filt_coef[i]);
	}

	/* Y/C horizontal + vertical initial info (vendor init_scaler) */
	amvdec_write_dos(core, PSCALE_BMEM_ADDR, 37 * 2);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x0008);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x60000000);

	amvdec_write_dos(core, PSCALE_BMEM_ADDR, 41 * 2);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x0008);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x60000000);

	amvdec_write_dos(core, PSCALE_BMEM_ADDR, 39 * 2);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x0008);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x60000000);

	amvdec_write_dos(core, PSCALE_BMEM_ADDR, 43 * 2);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x0008);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x60000000);

	/* Y/C horizontal + vertical phase steps: 1:1 scaling */
	amvdec_write_dos(core, PSCALE_BMEM_ADDR, 36 * 2 + 1);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x10000);
	amvdec_write_dos(core, PSCALE_BMEM_ADDR, 40 * 2 + 1);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x10000);
	amvdec_write_dos(core, PSCALE_BMEM_ADDR, 38 * 2 + 1);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x10000);
	amvdec_write_dos(core, PSCALE_BMEM_ADDR, 42 * 2 + 1);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, 0x10000);

	/* reset pscaler */
	amvdec_write_dos(core, DOS_SW_RESET0, BIT(10));
	amvdec_write_dos(core, DOS_SW_RESET0, 0);
	amvdec_write_dos(core, PSCALE_RST, 0x7);
	amvdec_write_dos(core, PSCALE_RST, 0x0);

	amvdec_write_dos(core, MREG_TO_AMRISC, 0);
	amvdec_write_dos(core, MREG_FROM_AMRISC, 0);
	amvdec_write_dos(core, MCPU_INTR_MSK, 0xffff);
	/* set interrupt mapping for vld */
	amvdec_write_dos(core, VDEC_ASSIST_AMR1_INT8, 8);
}

static void codec_mjpeg_resume(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	if (amvdec_set_canvases(sess, (u32[]){ MJPEG_CANVAS_TABLE, 0 },
				(u32[]){ MJPEG_NUM_BUFFERS, 0 })) {
		amvdec_abort(sess);
		return;
	}
	codec_mjpeg_init_hw(sess);

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

/* Motion JPEG carry no inter-stream end marker: just flush once IRQs
 * go quiet. See codec_mpeg12_drain_work for the shared rationale.
 */
static void codec_mjpeg_drain_work(struct work_struct *work)
{
	struct codec_mjpeg *mjpeg =
		container_of(work, struct codec_mjpeg, drain_work.work);
	struct amvdec_session *sess = mjpeg->sess;
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
	mjpeg->draining = false;
}

static void codec_mjpeg_drain(struct amvdec_session *sess)
{
	struct codec_mjpeg *mjpeg = sess->priv;

	if (mjpeg->draining)
		return;

	mjpeg->draining = true;
	schedule_delayed_work(&mjpeg->drain_work, 0);
}

static int codec_mjpeg_stop(struct amvdec_session *sess)
{
	struct codec_mjpeg *mjpeg = sess->priv;

	cancel_delayed_work_sync(&mjpeg->drain_work);

	return 0;
}

static irqreturn_t codec_mjpeg_threaded_isr(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	u32 reg;
	u32 buffer_index;
	u32 field = V4L2_FIELD_NONE;
	u32 offset;

	amvdec_write_dos(core, ASSIST_MBOX1_CLR_REG, 1);
	reg = amvdec_read_dos(core, MREG_FROM_AMRISC);
	if (!(reg & PICINFO_BUF_IDX_MASK))
		return IRQ_HANDLED;

	buffer_index = ((reg & PICINFO_BUF_IDX_MASK) - 1) & (MJPEG_NUM_BUFFERS - 1);
	offset = amvdec_read_dos(core, MREG_FRAME_OFFSET);

	if (reg & PICINFO_INTERLACE)
		field = (reg & PICINFO_INTERLACE_FIRST) ?
			V4L2_FIELD_INTERLACED_TB :
			V4L2_FIELD_INTERLACED_BT;

	amvdec_dst_buf_done_idx(sess, buffer_index, offset, field);

	/* the ucode raises the next picinfo only once this is cleared
	 * (vendor vmjpeg_isr)
	 */
	amvdec_write_dos(core, MREG_FROM_AMRISC, 0);

	return IRQ_HANDLED;
}

static irqreturn_t codec_mjpeg_isr(struct amvdec_session *sess)
{
	return IRQ_WAKE_THREAD;
}

struct amvdec_codec_ops codec_mjpeg_ops = {
	.start = codec_mjpeg_start,
	.stop = codec_mjpeg_stop,
	.isr = codec_mjpeg_isr,
	.threaded_isr = codec_mjpeg_threaded_isr,
	.can_recycle = codec_mjpeg_can_recycle,
	.recycle = codec_mjpeg_recycle,
	.drain = codec_mjpeg_drain,
	.resume = codec_mjpeg_resume,
};
