// SPDX-License-Identifier: GPL-2.0+
/*
 * H.264 hardware encoder for Amlogic Meson8b (S805) HCODEC block,
 * V4L2 mem2mem interface.
 *
 * Register sequence ported from the vendor amvenc_avc driver
 * (Hardkernel linux 5aed95d3, drivers/amlogic/amports/encoder.c,
 * author Simon Zheng), running the vendor FULL-mode microcode
 * (mix_dump_mc_dblk) on the block's ARM core.
 */
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>

#include "../runtime/meson8b-runtime.h"
#include "hcodec_regs.h"
#include "venc.h"

#define ENC_QUANT_DEFAULT	26
#define ENC_GOP_DEFAULT		12
#define ENC_TIMEOUT_MS		5000
#define ENC_RC_MIN_QP		10
#define ENC_RC_MAX_QP		48
#define ENC_RC_MAX_STEP		4

/*
 * Vendor HDEC_319M: fclk_div4 (2550 MHz / 4) / 2. Must be exact: the hcodec
 * mux is CLK_SET_RATE_PARENT with MPLL parents, so an inexact request makes
 * the clk core reparent it and reprogram a shared MPLL (MPLL2 feeds the
 * Ethernet RGMII clock on OneCloud).
 */
#define ENC_HCODEC_RATE		318750000

/*
 * Chroma QP derived from luma QP (H.264 chroma QP table, clipped at 39
 * like the vendor driver).
 */
static const u8 venc_chroma_qp[52] = {
	 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
	16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 29, 30,
	31, 32, 32, 33, 34, 34, 35, 35, 36, 36, 37, 37, 37, 38, 38, 38,
	39, 39, 39, 39,
};

static u32 venc_plane_count(u32 fourcc)
{
	switch (fourcc) {
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_NV21M:
		return 2;
	case V4L2_PIX_FMT_YUV420M:
		return 3;
	default:
		return 0;
	}
}

/*
 * Map an OUTPUT fourcc to the supported multi-planar format, 0 if unsupported.
 * ffmpeg 4.2 sends splane fourccs on MPLANE queues; accept them as aliases.
 */
static u32 venc_normalize_out(u32 fourcc)
{
	switch (fourcc) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV12M:
		return V4L2_PIX_FMT_NV12M;
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV21M:
		return V4L2_PIX_FMT_NV21M;
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YUV420M:
		return V4L2_PIX_FMT_YUV420M;
	default:
		return 0;
	}
}

/* Alignments used by the vendor mfdin input path (canvas stride) */
static u32 venc_in_canvas_w(u32 fourcc, u32 width)
{
	/* YUV420 uses a 64-byte aligned stride, NV12 32-byte */
	return fourcc == V4L2_PIX_FMT_YUV420M ?
		ALIGN(width, 64) : ALIGN(width, 32);
}

/*
 * Plane geometry as reported to userspace.  ffmpeg's frame pool gives
 * incoming frames a linesize of FFALIGN(width, 32) (chroma /2 for
 * YUV420) and FFALIGN(height, 32) rows, and copies whole plane
 * buffers into the queued V4L2 buffers, so these values must match
 * exactly.  Widths not a multiple of 32 are not supported.
 */
static void venc_fill_out_fmt(struct v4l2_pix_format_mplane *pix, u32 fourcc)
{
	u32 rows = ALIGN(pix->height, 32);
	u32 bpl = ALIGN(pix->width, 32);
	u32 crows = rows / 2;
	u32 i;

	pix->pixelformat = fourcc;
	pix->field = V4L2_FIELD_NONE;
	pix->num_planes = venc_plane_count(fourcc);

	pix->plane_fmt[0].bytesperline = bpl;
	pix->plane_fmt[0].sizeimage = bpl * rows + 64;

	if (fourcc == V4L2_PIX_FMT_YUV420M) {
		pix->plane_fmt[1].bytesperline = bpl / 2;
		pix->plane_fmt[1].sizeimage = bpl / 2 * crows + 64;
		pix->plane_fmt[2].bytesperline = bpl / 2;
		pix->plane_fmt[2].sizeimage = bpl / 2 * crows + 64;
	} else {
		pix->plane_fmt[1].bytesperline = bpl;
		pix->plane_fmt[1].sizeimage = bpl * crows + 64;
	}
	for (i = 0; i < pix->num_planes; i++)
		pix->plane_fmt[i].sizeimage =
			ALIGN(pix->plane_fmt[i].sizeimage, 8);
}

static void venc_fill_cap_fmt(struct v4l2_pix_format_mplane *pix)
{
	pix->pixelformat = V4L2_PIX_FMT_H264;
	pix->field = V4L2_FIELD_NONE;
	pix->num_planes = 1;
	pix->plane_fmt[0].bytesperline = 0;
	pix->plane_fmt[0].sizeimage = VENC_BITSTREAM_SIZE;
}

/* ---------------------------------------------------------------------
 * HCODEC low-level port (vendor amvenc_* / avc_* functions)
 */

static void venc_amrisc_reset(struct venc_core *core)
{
	/* three dummy reads before and after, for timing */
	venc_read(core, DOS_SW_RESET1);
	venc_read(core, DOS_SW_RESET1);
	venc_read(core, DOS_SW_RESET1);
	venc_write(core, DOS_SW_RESET1,
		   BIT(2) | BIT(6) | BIT(7) | BIT(8) | BIT(16) | BIT(17));
	venc_write(core, DOS_SW_RESET1, 0);
	venc_read(core, DOS_SW_RESET1);
	venc_read(core, DOS_SW_RESET1);
	venc_read(core, DOS_SW_RESET1);
}

static void venc_amrisc_start(struct venc_core *core)
{
	venc_read(core, DOS_SW_RESET1);
	venc_read(core, DOS_SW_RESET1);
	venc_read(core, DOS_SW_RESET1);
	venc_write(core, DOS_SW_RESET1, BIT(12) | BIT(11));
	venc_write(core, DOS_SW_RESET1, 0);
	venc_read(core, DOS_SW_RESET1);
	venc_read(core, DOS_SW_RESET1);
	venc_read(core, DOS_SW_RESET1);

	venc_write(core, HCODEC_MPSR, 1);
}

static void venc_amrisc_stop(struct venc_core *core)
{
	unsigned long timeout = jiffies + HZ;

	venc_write(core, HCODEC_MPSR, 0);
	venc_write(core, HCODEC_CPSR, 0);
	while (venc_read(core, HCODEC_IMEM_DMA_CTRL) & 0x8000) {
		if (time_after(jiffies, timeout))
			break;
		cpu_relax();
	}

	venc_read(core, DOS_SW_RESET1);
	venc_read(core, DOS_SW_RESET1);
	venc_read(core, DOS_SW_RESET1);
	venc_write(core, DOS_SW_RESET1,
		   BIT(12) | BIT(11) | BIT(2) | BIT(6) | BIT(7) | BIT(8) |
		   BIT(16) | BIT(17));
	venc_write(core, DOS_SW_RESET1, 0);
	venc_read(core, DOS_SW_RESET1);
	venc_read(core, DOS_SW_RESET1);
	venc_read(core, DOS_SW_RESET1);
}

/* Copy the microcode into the assit region and DMA it into HCODEC IMEM */
static int venc_loadmc(struct venc_ctx *ctx)
{
	struct venc_core *core = ctx->core;
	unsigned long timeout;

	memcpy(ctx->buf_vaddr + (ctx->mem.assit - ctx->buf_paddr),
	       ctx->fw->data, VENC_MC_SIZE);

	venc_write(core, HCODEC_MPSR, 0);
	venc_write(core, HCODEC_CPSR, 0);
	/* CBUS reads for timing */
	venc_read(core, HCODEC_MPSR);
	venc_read(core, HCODEC_MPSR);

	timeout = jiffies + HZ;
	venc_write(core, HCODEC_IMEM_DMA_ADR, ctx->mem.assit);
	venc_write(core, HCODEC_IMEM_DMA_COUNT, 0x1000);
	venc_write(core, HCODEC_IMEM_DMA_CTRL, 0x8000 | (7 << 16));

	while (venc_read(core, HCODEC_IMEM_DMA_CTRL) & 0x8000) {
		if (time_before(jiffies, timeout)) {
			schedule();
		} else {
			dev_err(core->dev, "hcodec microcode load failed\n");
			return -EBUSY;
		}
	}
	return 0;
}

/* Vendor avc_canvas_init: reference (dec0/dec1) canvases, re-done per frame */
static void venc_canvas_init(struct venc_ctx *ctx)
{
	struct venc_core *core = ctx->core;
	u32 cw = venc_in_canvas_w(V4L2_PIX_FMT_NV12M, ctx->width);
	u32 ch = ALIGN(ctx->height, 16);
	int endian = 0; /* no byte swap */

	/* ctx->mem holds bus addresses (see venc_buffspec_init) */
	meson_canvas_config(core->canvas, ctx->canvas[0], ctx->mem.dec0_y,
			    cw, ch, MESON_CANVAS_WRAP_NONE,
			    MESON_CANVAS_BLKMODE_LINEAR, endian);
	meson_canvas_config(core->canvas, ctx->canvas[1], ctx->mem.dec0_uv,
			    cw, ch / 2, MESON_CANVAS_WRAP_NONE,
			    MESON_CANVAS_BLKMODE_LINEAR, endian);
	meson_canvas_config(core->canvas, ctx->canvas[2], ctx->mem.dec0_uv,
			    cw, ch / 2, MESON_CANVAS_WRAP_NONE,
			    MESON_CANVAS_BLKMODE_LINEAR, endian);
	meson_canvas_config(core->canvas, ctx->canvas[3], ctx->mem.dec1_y,
			    cw, ch, MESON_CANVAS_WRAP_NONE,
			    MESON_CANVAS_BLKMODE_LINEAR, endian);
	meson_canvas_config(core->canvas, ctx->canvas[4], ctx->mem.dec1_uv,
			    cw, ch / 2, MESON_CANVAS_WRAP_NONE,
			    MESON_CANVAS_BLKMODE_LINEAR, endian);
	meson_canvas_config(core->canvas, ctx->canvas[5], ctx->mem.dec1_uv,
			    cw, ch / 2, MESON_CANVAS_WRAP_NONE,
			    MESON_CANVAS_BLKMODE_LINEAR, endian);
}

/* Vendor avc_init_encoder: per-frame encoder state, idr = next picture type */
static void venc_init_encoder(struct venc_ctx *ctx, bool idr)
{
	struct venc_core *core = ctx->core;

	venc_write(core, VLC_TOTAL_BYTES, 0);
	venc_write(core, VLC_CONFIG, 0x07);
	venc_write(core, VLC_INT_CONTROL, 0);
	venc_write(core, HCODEC_ASSIST_AMR1_INT0, 0x15);
	/* MULTI_SLICE_MC, !dblk_fix_flag (Meson8b) */
	venc_write(core, HCODEC_ASSIST_AMR1_INT1, 0x19);
	venc_write(core, HCODEC_ASSIST_AMR1_INT2, 0x8);
	venc_write(core, HCODEC_ASSIST_AMR1_INT3, 0x14);
	/* INTRA_IN_P_TOP */
	venc_write(core, HCODEC_ASSIST_DMA_INT_MSK, 0xfd);
	venc_write(core, HCODEC_ASSIST_DMA_INT_MSK2, 0xff);
	venc_write(core, HCODEC_ASSIST_AMR1_INT4, 0x18);

	venc_write(core, HENC_SCRATCH_5, ctx->idr_pic_id);	/* IDR_PIC_ID */
	venc_write(core, HENC_SCRATCH_6, idr ? 0 : ctx->frame_number);
	venc_write(core, HENC_SCRATCH_7, idr ? 0 : ctx->poc_lsb);
	venc_write(core, HENC_SCRATCH_8, 4);	/* log2_max_pic_order_cnt_lsb */
	venc_write(core, HENC_SCRATCH_9, 4);	/* log2_max_frame_num */
	venc_write(core, HENC_SCRATCH_A, 0);	/* ANC0_BUFFER_ID */
	venc_write(core, HENC_SCRATCH_B, ctx->quant);	/* QPPICTURE */
}

/* Vendor avc_init_input_buffer */
static void venc_init_input_buffer(struct venc_ctx *ctx)
{
	struct venc_core *core = ctx->core;

	venc_write(core, QDCT_MB_START_PTR, ctx->mem.dct_start);
	venc_write(core, QDCT_MB_END_PTR, ctx->mem.dct_end);
	venc_write(core, QDCT_MB_WR_PTR, ctx->mem.dct_start);
	venc_write(core, QDCT_MB_RD_PTR, ctx->mem.dct_start);
	venc_write(core, QDCT_MB_BUFF, 0);
}

/* Vendor avc_init_output_buffer */
static void venc_init_output_buffer(struct venc_ctx *ctx)
{
	struct venc_core *core = ctx->core;

	venc_write(core, VLC_VB_MEM_CTL, BIT(31) | (0x3f << 24) | (0x20 << 16) | 2);
	venc_write(core, VLC_VB_START_PTR, ctx->mem.bitstream_start);
	venc_write(core, VLC_VB_WR_PTR, ctx->mem.bitstream_start);
	venc_write(core, VLC_VB_SW_RD_PTR, ctx->mem.bitstream_start);
	venc_write(core, VLC_VB_END_PTR, ctx->mem.bitstream_end);
	venc_write(core, VLC_VB_CONTROL, 1);
	venc_write(core, VLC_VB_CONTROL, (7 << 3) | (1 << 1));
}

static void venc_init_reference_buffer(struct venc_ctx *ctx)
{
	struct venc_core *core = ctx->core;

	venc_write(core, HCODEC_ANC0_CANVAS_ADDR, ctx->ref_canvas);
	venc_write(core, VLC_HCMD_CONFIG, 0);
}

static void venc_init_dblk_buffer(struct venc_ctx *ctx)
{
	struct venc_core *core = ctx->core;

	venc_write(core, HCODEC_REC_CANVAS_ADDR, ctx->dblk_canvas);
	venc_write(core, HCODEC_DBKR_CANVAS_ADDR, ctx->dblk_canvas);
	venc_write(core, HCODEC_DBKW_CANVAS_ADDR, ctx->dblk_canvas);
}

static void venc_init_assit_buffer(struct venc_ctx *ctx)
{
	/* MEM_OFFSET_REG */
	venc_write(ctx->core, HENC_SCRATCH_1, ctx->mem.assit);
}

/*
 * Optional CBR controller. The vendor driver applies whatever per-frame
 * quant userspace supplies; the hardware has no built-in rate control.
 * When userspace sets V4L2_CID_MPEG_VIDEO_BITRATE we walk the QP around
 * the target instead: after each picture, compare accumulated bits with
 * the budget and step QP (bounded) toward convergence. Deliberately
 * conservative and single-loop — no lookahead, no per-MB adaptation.
 * ponytail: additive controller; replace with a PID if drift matters.
 */
static void venc_rc_picture_done(struct venc_ctx *ctx, u32 es_bytes, u32 fps)
{
	u64 per_frame, budget;

	if (!ctx->bitrate || !fps)
		return;

	ctx->rc_bits += (u64)es_bytes * 8;
	ctx->rc_frames++;

	per_frame = div64_u64(ctx->bitrate, fps);
	budget = per_frame * ctx->rc_frames;

	if (ctx->rc_bits > budget)
		ctx->rc_qp += ENC_RC_MAX_STEP;		/* over budget: coarser */
	else if (budget - ctx->rc_bits > per_frame)
		ctx->rc_qp -= 1;			/* under budget: finer */

	ctx->rc_qp = clamp(ctx->rc_qp, ENC_RC_MIN_QP, ENC_RC_MAX_QP);
	ctx->quant = ctx->rc_qp;
}

/*
 * Vendor avc_prot_init (UCODE_MODE_FULL, me pixel mode 0):
 * per-picture QDCT/VLC/IE/ME setup.
 */
static void venc_prot_init(struct venc_ctx *ctx, bool idr)
{
	struct venc_core *core = ctx->core;
	u32 qp = ctx->quant;
	u32 qp_c = venc_chroma_qp[min(qp, (u32)51)];

	venc_write(core, VLC_PIC_SIZE, ctx->width | (ctx->height << 16));
	venc_write(core, VLC_PIC_POSITION, 0);

	venc_write(core, QDCT_Q_QUANT_I,
		   (qp_c << 22) | (qp << 16) | ((qp_c % 6) << 12) |
		   ((qp_c / 6) << 8) | ((qp % 6) << 4) | (qp / 6));
	venc_write(core, QDCT_Q_QUANT_P,
		   (qp_c << 22) | (qp << 16) | ((qp_c % 6) << 12) |
		   ((qp_c / 6) << 8) | ((qp % 6) << 4) | (qp / 6));

	venc_write(core, IGNORE_CONFIG,
		   (1 << 31) | (1 << 26) | (1 << 21) | (2 << 16) |
		   (1 << 15) | (1 << 10) | (1 << 5) | (2 << 0));
	venc_write(core, IGNORE_CONFIG_2,
		   (1 << 31) | (1 << 26) | (1 << 21) | (5 << 16));

	venc_write(core, QDCT_MB_CONTROL, (1 << 9) | (1 << 0));
	/* full-ucode mode */
	venc_write(core, QDCT_MB_CONTROL,
		   (1 << 29) | (1 << 20) | (1 << 19) | (1 << 18) | (1 << 17) |
		   (1 << 14) | (1 << 13) | (1 << 12) | (1 << 10));

	venc_write(core, SAD_CONTROL, (1 << 2) | (1 << 0));
	venc_write(core, IE_RESULT_BUFFER, 0);
	venc_write(core, SAD_CONTROL, (1 << 3) | (1 << 1));
	venc_write(core, IE_CONTROL, (1 << 0));
	venc_write(core, IE_CONTROL, 0);

	venc_write(core, ME_SAD_ENOUGH_01, 0);
	venc_write(core, ME_SAD_ENOUGH_23, 0);
	venc_write(core, ME_STEP0_CLOSE_MV, (0x100 << 10) | (2 << 5) | 2);
	/* me pixel mode 0 */
	venc_write(core, ME_SKIP_LINE, (4 << 24) | (4 << 18) | (2 << 12));
	venc_write(core, ME_F_SKIP_SAD,
		   (0x40 << 24) | (0x30 << 16) | (0x30 << 8) | 0x10);
	venc_write(core, ME_F_SKIP_WEIGHT, 0x18181818);
	venc_write(core, IE_DATA_FEED_BUFF_INFO, 0);

	venc_write(core, HCODEC_CURR_CANVAS_CTRL, 0);
	venc_write(core, VLC_CONFIG, venc_read(core, VLC_CONFIG) | 1);

	if (idr) {
		venc_write(core, HENC_SCRATCH_M, ctx->mem.intra_bits);
		venc_write(core, HENC_SCRATCH_N, ctx->mem.intra_pred);
	} else {
		venc_write(core, HENC_SCRATCH_M, ctx->mem.inter_bits);
		venc_write(core, HENC_SCRATCH_N, ctx->mem.inter_mv);
	}

	/* clear + enable the mailbox interrupt */
	venc_write(core, HCODEC_ASSIST_MBOX2_CLR_REG, 1);
	venc_write(core, HCODEC_ASSIST_MBOX2_MASK, 1);
}

/*
 * Vendor avc_init_ie_me_parameter (UCODE_MODE_FULL, ie_pippeline_block 3):
 * motion estimation setup.  IE_ME_MB_TYPE was already written by the
 * caller per picture type.
 */
static void venc_init_ie_me_parameter(struct venc_ctx *ctx)
{
	struct venc_core *core = ctx->core;

	venc_write(core, HENC_SCRATCH_E, 3);	/* IE_ME_MODE */
	venc_write(core, HENC_SCRATCH_F,		/* IE_REF_SEL */
		   (1 << 13) | (1 << 12) | (1 << 9) | (1 << 8));
	venc_write(core, ME_MV_MERGE_CTL,
		   (1 << 31) | (1 << 30) | (1 << 29) | (1 << 28) | (1 << 27) |
		   (1 << 26) | (1 << 25) | (1 << 24) | (0x12 << 18) |
		   (0x2b << 12) | 0x80);
	venc_write(core, ME_STEP0_CLOSE_MV, 0x003ffc21);
	venc_write(core, ME_MV_WEIGHT_01,
		   (0x40 << 24) | (0x30 << 16) | (0x20 << 8) | 0x30);
	venc_write(core, ME_MV_WEIGHT_23, (0x40 << 8) | 0x30);
	venc_write(core, ME_SAD_RANGE_INC, 0x03030303);
	venc_write(core, HENC_SCRATCH_L, 0);	/* FIXED_SLICE_CFG */

	venc_write(core, HENC_SCRATCH_G, (30 << 16) | 0xffff); /* P_INTRA_CONFIG */
	venc_write(core, HENC_SCRATCH_I,		/* P_MB_QUANT_CONFIG */
		   (20 << 16) | (ctx->quant << 8) | ctx->quant);
	venc_write(core, HENC_SCRATCH_J,
		   (20 << 24) | (15 << 16) | (10 << 8) | 5);
	venc_write(core, HENC_SCRATCH_K,
		   (60 << 24) | (40 << 16) | (30 << 8) | 20);
}

/*
 * Vendor mfdin_basic, restricted to the LOCAL_BUFF planar input path
 * (oformat 0, r2y_en 0, iformat < 8).
 */
static void venc_mfdin_basic(struct venc_core *core, u32 input, u32 iformat,
			     u32 picsize_x, u32 picsize_y)
{
	u32 canv_idx1_bppx = (iformat == 4) ? 0 : 1;

	venc_write(core, HCODEC_MFDIN_REG1_CTRL, iformat);
	venc_write(core, HCODEC_MFDIN_REG3_CANV,
		   (input & 0xffffff) | (1u << 28) | (canv_idx1_bppx << 26) |
		   (1u << 24));
	venc_write(core, HCODEC_MFDIN_REG4_LNR0, 0);
	venc_write(core, HCODEC_MFDIN_REG5_LNR1, 0);
	venc_write(core, HCODEC_MFDIN_REG8_DMBL,
		   (picsize_x << 12) | picsize_y);
	venc_write(core, HCODEC_MFDIN_REG9_ENDN,
		   (7 << 0) | (6 << 3) | (5 << 6) | (4 << 9) | (3 << 12) |
		   (2 << 15) | (1 << 18));
}

/*
 * Vendor set_input_format, LOCAL_BUFF path only: the frame has been
 * staged into the dct buffer, point the mfdin input canvases at it.
 */
static void venc_set_input_format(struct venc_ctx *ctx)
{
	struct venc_core *core = ctx->core;
	u32 input = ctx->mem.dct_start;
	u32 picsize_x = ALIGN(ctx->width, 16);
	u32 picsize_y = ALIGN(ctx->height, 16);
	u32 cw = venc_in_canvas_w(ctx->src_fmt, ctx->width);
	u32 iformat;
	int endian = 0;

	switch (ctx->src_fmt) {
	case V4L2_PIX_FMT_NV21M:
		iformat = 2;
		break;
	case V4L2_PIX_FMT_NV12M:
		iformat = 3;
		break;
	default:
		iformat = 4;	/* YUV420M */
		break;
	}

	meson_canvas_config(core->canvas, ctx->canvas[6], input, cw, picsize_y,
			    MESON_CANVAS_WRAP_NONE,
			    MESON_CANVAS_BLKMODE_LINEAR, endian);
	if (iformat == 4) {
		meson_canvas_config(core->canvas, ctx->canvas[7],
				    input + cw * picsize_y, cw / 2, picsize_y / 2,
				    MESON_CANVAS_WRAP_NONE,
				    MESON_CANVAS_BLKMODE_LINEAR, endian);
		meson_canvas_config(core->canvas, ctx->canvas[8],
				    input + cw * picsize_y * 5 / 4, cw / 2,
				    picsize_y / 2, MESON_CANVAS_WRAP_NONE,
				    MESON_CANVAS_BLKMODE_LINEAR, endian);
		input = (ctx->canvas[8] << 16) | (ctx->canvas[7] << 8) |
			ctx->canvas[6];
	} else {
		meson_canvas_config(core->canvas, ctx->canvas[7],
				    input + cw * picsize_y, cw, picsize_y / 2,
				    MESON_CANVAS_WRAP_NONE,
				    MESON_CANVAS_BLKMODE_LINEAR, endian);
		input = (ctx->canvas[7] << 8) | ctx->canvas[6];
	}

	venc_mfdin_basic(core, input, iformat, picsize_x, picsize_y);
}

/*
 * Only set the rate when the hcodec mux already runs at 2 * ENC_HCODEC_RATE:
 * the divider then takes the exact-match path and never touches the mux or
 * its parents. Anything else could reparent onto a shared MPLL.
 */
static int venc_set_hcodec_rate(struct venc_core *core)
{
	struct clk *div = clk_get_parent(core->hcodec_clk);
	struct clk *mux = div ? clk_get_parent(div) : NULL;
	unsigned long mux_rate = mux ? clk_get_rate(mux) : 0;

	if (mux_rate != 2 * ENC_HCODEC_RATE) {
		dev_err(core->dev,
			"hcodec mux at %lu Hz, expected fclk_div4; not reparenting\n",
			mux_rate);
		return -EINVAL;
	}
	return clk_set_rate(core->hcodec_clk, ENC_HCODEC_RATE);
}

/* Vendor avc_poweron (Meson8b path) */
static int venc_poweron(struct venc_core *core)
{
	u32 data32;
	int ret;

	ret = venc_set_hcodec_rate(core);
	if (ret)
		return ret;
	ret = clk_prepare_enable(core->dos_clk);
	if (ret)
		return ret;
	ret = clk_prepare_enable(core->hcodec_clk);
	if (ret) {
		clk_disable_unprepare(core->dos_clk);
		return ret;
	}

	data32 = venc_read_ao(core, AO_RTI_PWR_CNTL_REG0);
	venc_write_ao(core, AO_RTI_PWR_CNTL_REG0, data32 & ~0x18);
	udelay(10);
	/* power up HCODEC */
	data32 = venc_read_ao_sleep(core, AO_RTI_GEN_PWR_SLEEP0);
	venc_write_ao_sleep(core, AO_RTI_GEN_PWR_SLEEP0, data32 & ~0x3);
	udelay(10);

	venc_write(core, DOS_SW_RESET1, 0xffffffff);
	venc_write(core, DOS_SW_RESET1, 0);

	/* HDEC clock gates 12..26 of DOS_GCLK_EN0 (vendor hvdec_clock_enable) */
	venc_write(core, DOS_GCLK_EN0,
		   venc_read(core, DOS_GCLK_EN0) | (0x7fff << 12));

	/* power up HCODEC memories */
	venc_write(core, DOS_MEM_PD_HCODEC, 0);

	/* remove HCODEC isolation */
	data32 = venc_read_ao_sleep(core, AO_RTI_GEN_PWR_ISO0);
	venc_write_ao_sleep(core, AO_RTI_GEN_PWR_ISO0, data32 & ~0x30);
	udelay(10);

	/* auto-clock-gate pulse */
	venc_write(core, DOS_GEN_CTRL0, venc_read(core, DOS_GEN_CTRL0) | 1);
	venc_write(core, DOS_GEN_CTRL0, venc_read(core, DOS_GEN_CTRL0) & ~1);

	mdelay(10);
	return 0;
}

/* Vendor avc_poweroff (Meson8b path) */
static void venc_poweroff(struct venc_core *core)
{
	/* enable HCODEC isolation */
	venc_write_ao_sleep(core, AO_RTI_GEN_PWR_ISO0,
			    venc_read_ao_sleep(core, AO_RTI_GEN_PWR_ISO0) | 0x30);
	/* power off HCODEC memories */
	venc_write(core, DOS_MEM_PD_HCODEC, 0xffffffff);
	/* HCODEC clock gates off */
	venc_write(core, DOS_GCLK_EN0,
		   venc_read(core, DOS_GCLK_EN0) & ~(0x7fff << 12));
	clk_disable_unprepare(core->hcodec_clk);
	/* power off HCODEC */
	venc_write_ao_sleep(core, AO_RTI_GEN_PWR_SLEEP0,
			    venc_read_ao_sleep(core, AO_RTI_GEN_PWR_SLEEP0) | 0x3);
	clk_disable_unprepare(core->dos_clk);
}

/* ---------------------------------------------------------------------
 * Encoder session (vendor amvenc_avc_start/stop + per-frame command flow)
 */

/*
 * Vendor amvenc_avc_start_cmd: prepare registers for one command and
 * kick the microcode by writing the status mailbox.
 */
static int venc_start_cmd(struct venc_ctx *ctx, u32 cmd)
{
	struct venc_core *core = ctx->core;
	bool idr = cmd == ENCODER_IDR;
	u32 mb_type;

	if (core->need_reset) {
		core->need_reset = false;
		core->hw_status = ENCODER_IDLE;
		venc_amrisc_reset(core);
		venc_canvas_init(ctx);
		venc_init_encoder(ctx, idr);
		venc_init_input_buffer(ctx);
		venc_init_output_buffer(ctx);
		venc_prot_init(ctx, idr);
		venc_init_assit_buffer(ctx);
	}

	if (cmd == ENCODER_IDR || cmd == ENCODER_NON_IDR) {
		venc_init_dblk_buffer(ctx);
		venc_init_reference_buffer(ctx);
		venc_set_input_format(ctx);
	}

	if (cmd == ENCODER_IDR)
		mb_type = HENC_MB_Type_I4MB;
	else if (cmd == ENCODER_NON_IDR)
		mb_type = (HENC_SKIP_RUN_AUTO << 16) | (HENC_MB_Type_AUTO << 4) |
			  HENC_MB_Type_AUTO;
	else
		mb_type = 0;
	venc_write(core, HENC_SCRATCH_D, mb_type);	/* IE_ME_MB_TYPE */
	venc_init_ie_me_parameter(ctx);

	core->hw_status = cmd;
	reinit_completion(&core->done);
	core->done_pending = true;
	venc_write(core, HENC_SCRATCH_0, cmd);	/* ENCODER_STATUS */
	venc_write(core, HENC_SCRATCH_L, 0);	/* FIXED_SLICE_CFG */

	return 0;
}

static int venc_wait_cmd(struct venc_core *core, u32 done_code)
{
	int ret;

	ret = wait_for_completion_timeout(&core->done,
					  msecs_to_jiffies(ENC_TIMEOUT_MS));
	if (!ret)
		return -ETIMEDOUT;
	if (core->hw_status != done_code)
		return -EIO;
	return 0;
}

static irqreturn_t venc_isr(int irq, void *data)
{
	struct venc_core *core = data;
	u32 status;

	/* clear mailbox interrupt first, then sample the status */
	venc_write(core, HCODEC_ASSIST_MBOX2_CLR_REG, 1);
	status = venc_read(core, HENC_SCRATCH_0);
	core->hw_status = status;

	if (!core->done_pending)
		return IRQ_HANDLED;

	switch (status) {
	case ENCODER_SEQUENCE_DONE:
	case ENCODER_PICTURE_DONE:
	case ENCODER_IDR_DONE:
	case ENCODER_NON_IDR_DONE:
		core->done_pending = false;
		if (status != ENCODER_SEQUENCE_DONE)
			core->need_reset = true;
		complete(&core->done);
		break;
	default:
		break;
	}
	return IRQ_HANDLED;
}

/* Vendor avc_buffspec_init: 1080P layout inside the big buffer */
static void venc_buffspec_init(struct venc_ctx *ctx)
{
	dma_addr_t base = ctx->buf_paddr;
	u32 cw = venc_in_canvas_w(V4L2_PIX_FMT_NV12M, ctx->width);
	u32 ch = ALIGN(ctx->height, 16);

	ctx->mem.dct_start = base;
	ctx->mem.dct_end = base + 0x6ba000 - 1;
	ctx->mem.dec0_y = base + 0x6d0000;
	ctx->mem.dec0_uv = ctx->mem.dec0_y + cw * ch;
	ctx->mem.dec1_y = base + 0x9d0000;
	ctx->mem.dec1_uv = ctx->mem.dec1_y + cw * ch;
	ctx->mem.assit = base + 0xd10000;
	ctx->mem.bitstream_start = base + 0xe00000;
	ctx->mem.bitstream_end = ctx->mem.bitstream_start + VENC_BITSTREAM_SIZE - 1;
	ctx->mem.inter_bits = base + 0xf00000;
	ctx->mem.inter_mv = base + 0xf08000;
	ctx->mem.intra_bits = base + 0xf88000;
	ctx->mem.intra_pred = base + 0xf90000;

	ctx->dblk_canvas = (ctx->canvas[2] << 16) | (ctx->canvas[1] << 8) |
			   ctx->canvas[0];
	ctx->ref_canvas = (ctx->canvas[5] << 16) | (ctx->canvas[4] << 8) |
			   ctx->canvas[3];
}

/* Vendor amvenc_avc_start: full init from power-off state */
static int venc_hw_init(struct venc_ctx *ctx)
{
	struct venc_core *core = ctx->core;
	int ret;

	ret = venc_poweron(core);
	if (ret)
		return ret;
	venc_buffspec_init(ctx);
	venc_canvas_init(ctx);
	venc_write(core, HCODEC_ASSIST_MMC_CTRL1, 0x2);

	ret = venc_loadmc(ctx);
	if (ret) {
		venc_poweroff(core);
		return ret;
	}

	core->need_reset = true;
	core->hw_status = ENCODER_IDLE;
	venc_amrisc_reset(core);
	venc_init_encoder(ctx, true);
	venc_init_input_buffer(ctx);
	venc_init_output_buffer(ctx);
	venc_prot_init(ctx, true);
	venc_init_dblk_buffer(ctx);
	venc_init_reference_buffer(ctx);
	venc_init_assit_buffer(ctx);
	venc_write(core, HENC_SCRATCH_D, 0);	/* IE_ME_MB_TYPE */
	venc_init_ie_me_parameter(ctx);
	venc_write(core, HENC_SCRATCH_0, ENCODER_IDLE);
	venc_write(core, HENC_SCRATCH_L, 0);	/* FIXED_SLICE_CFG */
	venc_amrisc_start(core);

	return 0;
}

static int venc_hw_start(struct venc_ctx *ctx)
{
	struct venc_core *core = ctx->core;
	int ret, i;

	ctx->buf_vaddr = dma_alloc_coherent(core->dev, VENC_BUF_SIZE,
					     &ctx->buf_paddr, GFP_KERNEL);
	if (!ctx->buf_vaddr)
		return -ENOMEM;

	/* zero the input staging area so pad rows/bytes encode as black */
	memset(ctx->buf_vaddr, 0, 0x6ba000);

	ret = request_firmware(&ctx->fw, VENC_FW_NAME, core->dev);
	if (ret) {
		dev_err(core->dev, "missing firmware %s\n", VENC_FW_NAME);
		goto err_free;
	}

	for (i = 0; i < 9; i++) {
		ret = meson_canvas_alloc(core->canvas, &ctx->canvas[i]);
		if (ret)
			goto err_canvas;
	}

	ctx->idr_pic_id = 0;
	ctx->frame_number = 0;
	ctx->poc_lsb = 0;
	ctx->gop_idx = 0;

	ret = venc_hw_init(ctx);
	if (ret)
		goto err_canvas;

	core->running = true;

	/* harvest SPS + PPS with the vendor SEQUENCE/PICTURE commands */
	ret = venc_start_cmd(ctx, ENCODER_SEQUENCE);
	if (!ret)
		ret = venc_wait_cmd(core, ENCODER_SEQUENCE_DONE);
	if (!ret) {
		ctx->sps_pps_size = venc_read(core, VLC_TOTAL_BYTES);
		ret = venc_start_cmd(ctx, ENCODER_PICTURE);
	}
	if (!ret)
		ret = venc_wait_cmd(core, ENCODER_PICTURE_DONE);
	if (!ret) {
		u32 pps = venc_read(core, VLC_TOTAL_BYTES) - ctx->sps_pps_size;

		ctx->sps_pps = kmalloc(ctx->sps_pps_size + pps, GFP_KERNEL);
		if (!ctx->sps_pps) {
			ret = -ENOMEM;
			goto err_stop_hw;
		}
		memcpy(ctx->sps_pps,
		       ctx->buf_vaddr + (ctx->mem.bitstream_start -
					 ctx->buf_paddr),
		       ctx->sps_pps_size + pps);
		ctx->sps_pps_size += pps;
		ctx->sps_pps_valid = true;
	}
	if (ret) {
		dev_err(core->dev, "SPS/PPS generation failed (%d), hw_status %u\n",
			ret, core->hw_status);
		goto err_stop_hw;
	}

	return 0;

err_stop_hw:
	kfree(ctx->sps_pps);
	ctx->sps_pps = NULL;
	ctx->sps_pps_valid = false;
	venc_amrisc_stop(core);
	venc_poweroff(core);
	core->running = false;
err_canvas:
	for (i--; i >= 0; i--)
		meson_canvas_free(core->canvas, ctx->canvas[i]);
	release_firmware(ctx->fw);
	ctx->fw = NULL;
err_free:
	dma_free_coherent(core->dev, VENC_BUF_SIZE, ctx->buf_vaddr,
			  ctx->buf_paddr);
	ctx->buf_vaddr = NULL;
	return ret;
}

/* Free the session resources; the hardware must already be powered off */
static void venc_session_release(struct venc_ctx *ctx)
{
	struct venc_core *core = ctx->core;
	int i;

	for (i = 0; i < 9; i++) {
		meson_canvas_free(core->canvas, ctx->canvas[i]);
		ctx->canvas[i] = 0;
	}
	kfree(ctx->sps_pps);
	ctx->sps_pps = NULL;
	ctx->sps_pps_valid = false;
	release_firmware(ctx->fw);
	ctx->fw = NULL;
	dma_free_coherent(core->dev, VENC_BUF_SIZE, ctx->buf_vaddr,
			  ctx->buf_paddr);
	ctx->buf_vaddr = NULL;
	core->running = false;
}

static void venc_hw_stop(struct venc_ctx *ctx)
{
	venc_amrisc_stop(ctx->core);
	venc_poweroff(ctx->core);
	venc_session_release(ctx);
}

/*
 * Vendor light reset: stop, power off, then a full re-init. If the re-init
 * fails the hardware is left powered off and the session is released, so
 * the next job restarts it from scratch.
 */
static void venc_light_reset(struct venc_ctx *ctx)
{
	struct venc_core *core = ctx->core;
	int ret;

	venc_amrisc_stop(core);
	venc_poweroff(core);
	mdelay(30);
	ctx->gop_idx = 0;	/* next frame must be an IDR */
	ret = venc_hw_init(ctx);
	if (ret) {
		dev_err(core->dev, "hardware re-init failed (%d)\n", ret);
		venc_session_release(ctx);
	}
}

/* ---------------------------------------------------------------------
 * Frame processing
 */

static void venc_copy_rows(u8 *dst, u32 dst_stride, u8 *src, u32 src_stride,
			  u32 row_bytes, u32 rows, u32 src_size)
{
	u32 i;

	for (i = 0; i < rows; i++) {
		u32 left = src_size - i * src_stride;
		u32 n = min(row_bytes, left);

		memcpy(dst, src, n);
		dst += dst_stride;
		src += src_stride;
	}
}

/*
 * Copy the queued source frame into the dct staging area at the
 * vendor LOCAL_BUFF layout.  Rows outside the visible size come from
 * the zeroed staging buffer, so edge macroblocks encode deterministic
 * data.  Strides are taken from the negotiated format (ffmpeg
 * allocates exactly what G_FMT reported).
 */
static void venc_stage_input(struct venc_ctx *ctx, struct vb2_buffer *b)
{
	u32 ps_y = ALIGN(ctx->height, 16);
	u32 cw = venc_in_canvas_w(ctx->src_fmt, ctx->width);
	u32 bpl = ALIGN(ctx->width, 32);
	u8 *dst = ctx->buf_vaddr;
	unsigned long plane_sz;
	u8 *src;
	u32 i;

	plane_sz = vb2_plane_size(b, 0);
	src = vb2_plane_vaddr(b, 0);
	venc_copy_rows(dst, cw, src, bpl, ctx->width, ps_y, plane_sz);

	if (ctx->src_fmt == V4L2_PIX_FMT_YUV420M) {
		/* U at cw*ps_y, V at cw*ps_y*5/4, stride cw/2 */
		dst = ctx->buf_vaddr + cw * ps_y;
		for (i = 1; i <= 2; i++, dst += cw * ps_y / 4) {
			plane_sz = vb2_plane_size(b, i);
			src = vb2_plane_vaddr(b, i);
			venc_copy_rows(dst, cw / 2, src, bpl / 2,
				       ctx->width / 2, ps_y / 2, plane_sz);
		}
	} else {
		/*
		 * NV12/NV21 interleaved chroma at cw*ps_y:
		 * w/2 U + w/2 V samples = width bytes per row.
		 */
		dst = ctx->buf_vaddr + cw * ps_y;
		plane_sz = vb2_plane_size(b, 1);
		src = vb2_plane_vaddr(b, 1);
		venc_copy_rows(dst, cw, src, bpl, ctx->width, ps_y / 2,
			       plane_sz);
	}
}

static void venc_frame_done(struct venc_ctx *ctx, bool idr)
{
	u32 tmp;

	if (idr) {
		ctx->idr_pic_id = (ctx->idr_pic_id + 1) & 0xffff;
		ctx->poc_lsb = 2;
		ctx->frame_number = 1;
	} else {
		ctx->frame_number = (ctx->frame_number + 1) & 0xffff;
		ctx->poc_lsb += 2;
	}
	/* the current dblk buffer becomes the next reference */
	tmp = ctx->dblk_canvas;
	ctx->dblk_canvas = ctx->ref_canvas;
	ctx->ref_canvas = tmp;
}

static int venc_encode_frame(struct venc_ctx *ctx, struct vb2_buffer *src,
			     struct vb2_buffer *dst)
{
	struct venc_core *core = ctx->core;
	u32 es_size;
	u32 done_code;
	bool idr;
	int ret;

	idr = ctx->gop_idx == 0;
	done_code = idr ? ENCODER_IDR_DONE : ENCODER_NON_IDR_DONE;

	venc_stage_input(ctx, src);

	ret = venc_start_cmd(ctx, idr ? ENCODER_IDR : ENCODER_NON_IDR);
	if (ret)
		return ret;

	ret = venc_wait_cmd(core, done_code);
	/*
	 * The microcode reports IDR_DONE for P pictures too; like the vendor
	 * driver, accept either picture done code.
	 */
	if (ret == -EIO && (core->hw_status == ENCODER_IDR_DONE ||
			    core->hw_status == ENCODER_NON_IDR_DONE))
		ret = 0;
	if (ret) {
		dev_err(core->dev, "encode %s failed (%d), hw_status %u\n",
			idr ? "IDR" : "NON_IDR", ret, core->hw_status);
		venc_light_reset(ctx);
		return ret;
	}

	es_size = venc_read(core, VLC_TOTAL_BYTES);
	es_size = min_t(u32, es_size, VENC_BITSTREAM_SIZE);
	es_size = min(es_size, (u32)vb2_plane_size(dst, 0));

	if (idr && ctx->sps_pps_valid) {
		u32 hdr = min((u32)ctx->sps_pps_size,
			      (u32)vb2_plane_size(dst, 0));

		memcpy(vb2_plane_vaddr(dst, 0), ctx->sps_pps, hdr);
		es_size = min(es_size, (u32)vb2_plane_size(dst, 0) - hdr);
		memcpy(vb2_plane_vaddr(dst, 0) + hdr,
		       ctx->buf_vaddr + (ctx->mem.bitstream_start -
					 ctx->buf_paddr), es_size);
		vb2_set_plane_payload(dst, 0, hdr + es_size);
	} else {
		memcpy(vb2_plane_vaddr(dst, 0),
		       ctx->buf_vaddr + (ctx->mem.bitstream_start -
					 ctx->buf_paddr), es_size);
		vb2_set_plane_payload(dst, 0, es_size);
	}

	venc_frame_done(ctx, idr);
	/* timeperframe is N/M seconds per frame, so fps = M/N */
	venc_rc_picture_done(ctx, es_size,
			     ctx->timeperframe.denominator /
			     max(ctx->timeperframe.numerator, 1u));

	if (++ctx->gop_idx >= ctx->gop_size)
		ctx->gop_idx = 0;

	return 0;
}

/* ---------------------------------------------------------------------
 * V4L2 / vb2 plumbing
 */

static int venc_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
			    unsigned int *nplanes, unsigned int sizes[],
			    struct device *alloc_devs[])
{
	struct venc_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_pix_format_mplane pix = {};

	if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
		int i;

		pix.width = ctx->width;
		pix.height = ctx->height;
		venc_fill_out_fmt(&pix, ctx->src_fmt);
		*nplanes = pix.num_planes;
		for (i = 0; i < *nplanes; i++)
			sizes[i] = pix.plane_fmt[i].sizeimage;
	} else {
		*nplanes = 1;
		sizes[0] = VENC_BITSTREAM_SIZE;
	}

	return 0;
}

static int venc_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct venc_ctx *ctx = vb2_get_drv_priv(vq);

	v4l2_m2m_update_start_streaming_state(ctx->m2m_ctx, vq);
	return 0;
}

/* Complete a drain: flag @dst as the last buffer and signal EOS */
static void venc_signal_eos(struct venc_ctx *ctx, struct vb2_v4l2_buffer *dst)
{
	static const struct v4l2_event eos = { .type = V4L2_EVENT_EOS };

	dst->flags |= V4L2_BUF_FLAG_LAST;
	v4l2_event_queue_fh(&ctx->fh, &eos);
	v4l2_m2m_mark_stopped(ctx->m2m_ctx);
}

static void venc_stop_streaming(struct vb2_queue *vq)
{
	struct venc_ctx *ctx = vb2_get_drv_priv(vq);
	struct venc_core *core = ctx->core;

	struct vb2_v4l2_buffer *buf;

	cancel_work_sync(&ctx->work);
	if (core->running)
		venc_hw_stop(ctx);

	if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
		while ((buf = v4l2_m2m_src_buf_remove(ctx->m2m_ctx)))
			v4l2_m2m_buf_done(buf, VB2_BUF_STATE_ERROR);
	} else {
		while ((buf = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx)))
			v4l2_m2m_buf_done(buf, VB2_BUF_STATE_ERROR);
	}
	v4l2_m2m_update_stop_streaming_state(ctx->m2m_ctx, vq);
}

static void venc_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct venc_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	/* a drain finished while no CAPTURE buffer was queued */
	if (V4L2_TYPE_IS_CAPTURE(vb->vb2_queue->type) &&
	    v4l2_m2m_dst_buf_is_last(ctx->m2m_ctx)) {
		vb2_set_plane_payload(vb, 0, 0);
		venc_signal_eos(ctx, vbuf);
		vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
		return;
	}

	v4l2_m2m_buf_queue(ctx->m2m_ctx, vbuf);
}

static const struct vb2_ops venc_vb2_ops = {
	.queue_setup = venc_queue_setup,
	.start_streaming = venc_start_streaming,
	.stop_streaming = venc_stop_streaming,
	.buf_queue = venc_buf_queue,
};

static int venc_queue_init(void *priv, struct vb2_queue *src_vq,
			   struct vb2_queue *dst_vq)
{
	struct venc_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &venc_vb2_ops;
	src_vq->mem_ops = &vb2_vmalloc_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->core->lock;
	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &venc_vb2_ops;
	dst_vq->mem_ops = &vb2_vmalloc_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->core->lock;

	return vb2_queue_init(dst_vq);
}

static void venc_device_run(void *priv)
{
	struct venc_ctx *ctx = priv;

	schedule_work(&ctx->work);
}

static void venc_job_abort(void *priv)
{
	/*
	 * Nothing to signal: the work handler finishes the job with
	 * VB2_BUF_STATE_ERROR if the session is gone, and venc_release()
	 * cancels the work before freeing the context.
	 */
}

static const struct v4l2_m2m_ops venc_m2m_ops = {
	.device_run = venc_device_run,
	.job_abort = venc_job_abort,
};

static void venc_work(struct work_struct *work)
{
	struct venc_ctx *ctx =
		container_of(work, struct venc_ctx, work);
	struct venc_core *core = ctx->core;
	struct v4l2_m2m_ctx *m2m_ctx = ctx->m2m_ctx;
	struct vb2_v4l2_buffer *src, *dst;
	enum vb2_buffer_state state = VB2_BUF_STATE_DONE;
	bool keyframe;
	int ret;

	src = v4l2_m2m_next_src_buf(m2m_ctx);
	dst = v4l2_m2m_next_dst_buf(m2m_ctx);
	if (!src || !dst) {
		v4l2_m2m_job_finish(core->m2m_dev, m2m_ctx);
		return;
	}

	keyframe = ctx->gop_idx == 0;

	if (!core->running) {
		ret = venc_hw_start(ctx);
		if (ret) {
			dev_err(core->dev, "session start failed (%d)\n", ret);
			state = VB2_BUF_STATE_ERROR;
			goto out;
		}
	}

	ret = venc_encode_frame(ctx, &src->vb2_buf, &dst->vb2_buf);
	if (ret)
		state = VB2_BUF_STATE_ERROR;

out:
	v4l2_m2m_buf_copy_metadata(src, dst, false);
	if (state == VB2_BUF_STATE_DONE && keyframe)
		dst->flags |= V4L2_BUF_FLAG_KEYFRAME;
	if (v4l2_m2m_is_last_draining_src_buf(m2m_ctx, src))
		venc_signal_eos(ctx, dst);
	v4l2_m2m_buf_done_and_job_finish(core->m2m_dev, m2m_ctx, state);
}

/* ---------------------------------------------------------------------
 * V4L2 ioctls
 */

static int venc_querycap(struct file *file, void *fh,
			 struct v4l2_capability *cap)
{
	strscpy(cap->driver, "meson-venc", sizeof(cap->driver));
	strscpy(cap->card, "Meson8b H.264 Hardware Encoder",
		sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:meson-venc");
	return 0;
}

static int venc_enum_fmt(struct file *file, void *fh,
			 struct v4l2_fmtdesc *f)
{
	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		static const u32 out_fmts[] = {
			V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_NV21M,
			V4L2_PIX_FMT_YUV420M,
		};

		if (f->index >= ARRAY_SIZE(out_fmts))
			return -EINVAL;
		f->pixelformat = out_fmts[f->index];
	} else {
		if (f->index)
			return -EINVAL;
		f->pixelformat = V4L2_PIX_FMT_H264;
	}
	return 0;
}

static struct venc_ctx *venc_fh_to_ctx(struct file *file)
{
	return container_of(file->private_data, struct venc_ctx, fh);
}

static int venc_g_fmt(struct file *file, void *fh,
		      struct v4l2_format *f)
{
	struct venc_ctx *ctx = venc_fh_to_ctx(file);

	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		f->fmt.pix_mp.pixelformat = ctx->src_fmt;
		f->fmt.pix_mp.width = ctx->width;
		f->fmt.pix_mp.height = ctx->height;
		venc_fill_out_fmt(&f->fmt.pix_mp, ctx->src_fmt);
	} else {
		f->fmt.pix_mp.width = ctx->width;
		f->fmt.pix_mp.height = ctx->height;
		venc_fill_cap_fmt(&f->fmt.pix_mp);
	}
	return 0;
}

static int venc_try_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	u32 fourcc;

	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		fourcc = venc_normalize_out(f->fmt.pix_mp.pixelformat);
		if (!fourcc)
			fourcc = V4L2_PIX_FMT_NV12M;
		f->fmt.pix_mp.width = clamp(f->fmt.pix_mp.width, 16u, 1920u);
		f->fmt.pix_mp.height = clamp(f->fmt.pix_mp.height, 16u, 1088u);
		f->fmt.pix_mp.width = ALIGN(f->fmt.pix_mp.width, 2);
		f->fmt.pix_mp.height = ALIGN(f->fmt.pix_mp.height, 2);
		venc_fill_out_fmt(&f->fmt.pix_mp, fourcc);
	} else {
		f->fmt.pix_mp.width = clamp(f->fmt.pix_mp.width, 16u, 1920u);
		f->fmt.pix_mp.height = clamp(f->fmt.pix_mp.height, 16u, 1088u);
		venc_fill_cap_fmt(&f->fmt.pix_mp);
	}
	return 0;
}

static int venc_s_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct venc_ctx *ctx = venc_fh_to_ctx(file);
	u32 fourcc;
	int ret;

	ret = venc_try_fmt(file, fh, f);
	if (ret)
		return ret;

	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		fourcc = venc_normalize_out(f->fmt.pix_mp.pixelformat) ?:
			 V4L2_PIX_FMT_NV12M;
		ctx->src_fmt = fourcc;
		ctx->width = f->fmt.pix_mp.width;
		ctx->height = f->fmt.pix_mp.height;
	}
	return 0;
}

static int venc_g_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct venc_ctx *ctx = venc_fh_to_ctx(file);

	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;
	memset(&a->parm, 0, sizeof(a->parm));
	a->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
	a->parm.output.timeperframe = ctx->timeperframe;

	return 0;
}

static int venc_s_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct venc_ctx *ctx = venc_fh_to_ctx(file);
	u32 num, den;

	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;
	num = a->parm.output.timeperframe.numerator;
	den = a->parm.output.timeperframe.denominator;
	if (num && den) {
		ctx->timeperframe.numerator = num;
		ctx->timeperframe.denominator = den;
		/* frame rate only feeds the CBR budget, not the hardware */
	}
	return 0;
}

static int venc_subscribe_event(struct v4l2_fh *fh,
				const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_EOS:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subscribe_event(fh, sub);
	default:
		return -EINVAL;
	}
}

static const struct v4l2_ioctl_ops venc_ioctl_ops = {
	.vidioc_querycap = venc_querycap,
	.vidioc_enum_fmt_vid_cap = venc_enum_fmt,
	.vidioc_enum_fmt_vid_out = venc_enum_fmt,
	.vidioc_g_fmt_vid_cap_mplane = venc_g_fmt,
	.vidioc_g_fmt_vid_out_mplane = venc_g_fmt,
	.vidioc_try_fmt_vid_cap_mplane = venc_try_fmt,
	.vidioc_try_fmt_vid_out_mplane = venc_try_fmt,
	.vidioc_s_fmt_vid_cap_mplane = venc_s_fmt,
	.vidioc_s_fmt_vid_out_mplane = venc_s_fmt,
	.vidioc_g_parm = venc_g_parm,
	.vidioc_s_parm = venc_s_parm,
	.vidioc_reqbufs = v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf = v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf = v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf = v4l2_m2m_ioctl_dqbuf,
	.vidioc_prepare_buf = v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs = v4l2_m2m_ioctl_create_bufs,
	.vidioc_expbuf = v4l2_m2m_ioctl_expbuf,
	.vidioc_streamon = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff = v4l2_m2m_ioctl_streamoff,
	.vidioc_try_encoder_cmd = v4l2_m2m_ioctl_try_encoder_cmd,
	.vidioc_encoder_cmd = v4l2_m2m_ioctl_encoder_cmd,
	.vidioc_subscribe_event = venc_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static int venc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct venc_ctx *ctx =
		container_of(ctrl->handler, struct venc_ctx, ctrl_handler);
	struct v4l2_ctrl *min_qp, *max_qp;

	switch (ctrl->id) {
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		ctx->gop_size = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE:
		ctx->bitrate = ctrl->val;
		if (ctx->bitrate) {
			/* reset the controller and resume walking from QP 26 */
			ctx->rc_bits = 0;
			ctx->rc_frames = 0;
			ctx->rc_qp = ENC_QUANT_DEFAULT;
			ctx->quant = ctx->rc_qp;
		} else {
			/* CBR off: back to the fixed quant the QP range asks for */
			ctx->quant = ENC_QUANT_DEFAULT;
		}
		break;
	case V4L2_CID_MPEG_VIDEO_H264_MIN_QP:
	case V4L2_CID_MPEG_VIDEO_H264_MAX_QP:
		/* clamp the fixed quant into the requested QP range */
		min_qp = v4l2_ctrl_find(ctrl->handler,
					V4L2_CID_MPEG_VIDEO_H264_MIN_QP);
		max_qp = v4l2_ctrl_find(ctrl->handler,
					V4L2_CID_MPEG_VIDEO_H264_MAX_QP);
		ctx->quant = clamp(ENC_QUANT_DEFAULT, min_qp->val, max_qp->val);
		break;
	default:
		/* B_FRAMES/HEADER_MODE/PROFILE: stored, no-op */
		break;
	}
	return 0;
}

static const struct v4l2_ctrl_ops venc_ctrl_ops = {
	.s_ctrl = venc_s_ctrl,
};

static int venc_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct venc_core *core = video_get_drvdata(vdev);
	struct venc_ctx *ctx;
	struct v4l2_ctrl_handler *hdl;
	int ret;

	mutex_lock(&core->lock);
	if (core->ctx) {
		mutex_unlock(&core->lock);
		return -EBUSY;
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		mutex_unlock(&core->lock);
		return -ENOMEM;
	}

	ctx->core = core;
	ctx->width = 1920;
	ctx->height = 1088;
	ctx->src_fmt = V4L2_PIX_FMT_NV12M;
	ctx->quant = ENC_QUANT_DEFAULT;
	ctx->gop_size = ENC_GOP_DEFAULT;
	ctx->timeperframe.numerator = 1;
	ctx->timeperframe.denominator = 30;

	hdl = &ctx->ctrl_handler;
	ret = v4l2_ctrl_handler_init(hdl, 6);
	if (ret)
		goto err_free;

	v4l2_ctrl_new_std(hdl, &venc_ctrl_ops, V4L2_CID_MPEG_VIDEO_B_FRAMES,
			  0, 0, 1, 0);
	v4l2_ctrl_new_std(hdl, &venc_ctrl_ops, V4L2_CID_MPEG_VIDEO_BITRATE,
			  0, 100000000, 1, 0);
	v4l2_ctrl_new_std(hdl, &venc_ctrl_ops, V4L2_CID_MPEG_VIDEO_GOP_SIZE,
			  0, 65535, 1, ENC_GOP_DEFAULT);
	v4l2_ctrl_new_std(hdl, &venc_ctrl_ops, V4L2_CID_MPEG_VIDEO_H264_MIN_QP,
			  0, 51, 1, 0);
	v4l2_ctrl_new_std(hdl, &venc_ctrl_ops, V4L2_CID_MPEG_VIDEO_H264_MAX_QP,
			  0, 51, 1, 51);
	v4l2_ctrl_new_std_menu(hdl, &venc_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_HEADER_MODE,
			       V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME,
			       0, V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE);
	if (hdl->error) {
		ret = hdl->error;
		goto err_ctrl;
	}

	ctx->m2m_ctx = v4l2_m2m_ctx_init(core->m2m_dev, ctx, &venc_queue_init);
	if (IS_ERR(ctx->m2m_ctx)) {
		ret = PTR_ERR(ctx->m2m_ctx);
		goto err_ctrl;
	}

	INIT_WORK(&ctx->work, venc_work);
	v4l2_fh_init(&ctx->fh, vdev);
	file->private_data = &ctx->fh;
	ctx->fh.m2m_ctx = ctx->m2m_ctx;
	ctx->fh.ctrl_handler = hdl;
	v4l2_fh_add(&ctx->fh);

	core->ctx = ctx;
	mutex_unlock(&core->lock);

	return 0;

err_ctrl:
	v4l2_ctrl_handler_free(hdl);
err_free:
	kfree(ctx);
	mutex_unlock(&core->lock);
	return ret;
}

static int venc_release(struct file *file)
{
	struct venc_ctx *ctx = venc_fh_to_ctx(file);
	struct venc_core *core = ctx->core;

	cancel_work_sync(&ctx->work);
	if (core->running)
		venc_hw_stop(ctx);

	v4l2_m2m_ctx_release(ctx->m2m_ctx);
	v4l2_fh_del(&ctx->fh);
	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	v4l2_fh_exit(&ctx->fh);

	mutex_lock(&core->lock);
	if (core->ctx == ctx)
		core->ctx = NULL;
	mutex_unlock(&core->lock);

	kfree(ctx);
	return 0;
}

static const struct v4l2_file_operations venc_fops = {
	.owner = THIS_MODULE,
	.open = venc_open,
	.release = venc_release,
	.poll = v4l2_m2m_fop_poll,
	.mmap = v4l2_m2m_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
};

/* ---------------------------------------------------------------------
 * Probe / remove
 */

static const struct of_device_id venc_dt_match[] = {
	{ .compatible = "amlogic,meson8b-venc" },
	{ }
};
MODULE_DEVICE_TABLE(of, venc_dt_match);

static int venc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct venc_core *core;
	struct video_device *vdev;
	struct resource *r;
	int irq;
	int ret;

	if (!meson8b_runtime_ready())
		return -EPROBE_DEFER;

	core = devm_kzalloc(dev, sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;

	core->dev = dev;
	platform_set_drvdata(pdev, core);
	init_completion(&core->done);
	mutex_init(&core->lock);

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dos");
	core->dos_base = devm_ioremap_resource(dev, r);
	if (IS_ERR(core->dos_base))
		return PTR_ERR(core->dos_base);

	/*
	 * The two AO windows hold only the registers this driver touches
	 * (PWR_CNTL at +0x10, SLEEP0/ISO0 at +0xe8/+0xec); mapping the
	 * whole aobus would collide with the pinctrl/serial drivers.
	 */
	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ao-pwr");
	core->ao_pwr_base = devm_ioremap_resource(dev, r);
	if (IS_ERR(core->ao_pwr_base))
		return PTR_ERR(core->ao_pwr_base);

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ao-sleep");
	core->ao_sleep_base = devm_ioremap_resource(dev, r);
	if (IS_ERR(core->ao_sleep_base))
		return PTR_ERR(core->ao_sleep_base);

	core->dos_clk = devm_clk_get(dev, "dos");
	if (IS_ERR(core->dos_clk))
		return PTR_ERR(core->dos_clk);

	core->hcodec_clk = devm_clk_get(dev, "vdec_hcodec");
	if (IS_ERR(core->hcodec_clk))
		return PTR_ERR(core->hcodec_clk);

	core->canvas = meson_canvas_get(dev);
	if (IS_ERR(core->canvas))
		return PTR_ERR(core->canvas);

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	irq = platform_get_irq_byname(pdev, "venc");
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, venc_isr, 0, "venc", core);
	if (ret)
		return ret;

	ret = v4l2_device_register(dev, &core->v4l2_dev);
	if (ret) {
		dev_err(dev, "couldn't register v4l2 device\n");
		return ret;
	}

	vdev = video_device_alloc();
	if (!vdev) {
		ret = -ENOMEM;
		goto err_v4l2;
	}

	strscpy(vdev->name, "meson-video-encoder", sizeof(vdev->name));
	vdev->release = video_device_release;
	vdev->fops = &venc_fops;
	vdev->ioctl_ops = &venc_ioctl_ops;
	vdev->vfl_dir = VFL_DIR_M2M;
	vdev->v4l2_dev = &core->v4l2_dev;
	vdev->lock = &core->lock;
	vdev->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;
	video_set_drvdata(vdev, core);
	core->vdev_ptr = vdev;

	core->m2m_dev = v4l2_m2m_init(&venc_m2m_ops);
	if (IS_ERR(core->m2m_dev)) {
		ret = PTR_ERR(core->m2m_dev);
		goto err_vdev;
	}

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(dev, "failed to register video device\n");
		goto err_m2m;
	}

	dev_info(dev, "registered %s as /dev/video%d\n",
		 vdev->name, vdev->num);
	return 0;

err_m2m:
	v4l2_m2m_release(core->m2m_dev);
err_vdev:
	video_device_release(vdev);
err_v4l2:
	v4l2_device_unregister(&core->v4l2_dev);
	return ret;
}

static int venc_remove(struct platform_device *pdev)
{
	struct venc_core *core = platform_get_drvdata(pdev);

	video_unregister_device(core->vdev_ptr);
	v4l2_m2m_release(core->m2m_dev);
	v4l2_device_unregister(&core->v4l2_dev);
	return 0;
}

static struct platform_driver meson_venc_driver = {
	.probe = venc_probe,
	.remove = venc_remove,
	.driver = {
		.name = "meson-venc",
		.of_match_table = venc_dt_match,
	},
};
module_platform_driver(meson_venc_driver);

MODULE_DESCRIPTION("Meson8b HCODEC H.264 V4L2 m2m encoder");
MODULE_AUTHOR("YoungSx");
MODULE_LICENSE("GPL");
