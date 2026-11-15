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
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gcd.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>

#include "../runtime/meson8b-runtime.h"
#include "hcodec_regs.h"
#include "venc.h"
#include "jpeg-header.h"
#include "h264-sps.h"

#define ENC_QUANT_DEFAULT	26
#define ENC_GOP_DEFAULT		12
#define ENC_TIMEOUT_MS		5000
#define ENC_WAIT_SLICE_MS	100
#define ENC_VB_FULL_BYTES	16	/* VLC_VB_LEFT of a stalled, full buffer */

/*
 * QPPICTURE seeds the PPS pic_init_qp when the SEQUENCE/PICTURE commands
 * run and is the base the microcode codes slice_qp_delta against, so it
 * must stay constant while the quantizer QP changes per picture (vendor
 * init_qppicture). Rewriting it per picture yields slice_qp_delta 0 and a
 * stream that decodes every picture at the session's first QP.
 */
#define ENC_PIC_INIT_QP		26

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

/*
 * Raw input formats and their MFDIN modes (vendor set_input_format,
 * LOCAL_BUFF path). Each frame is copied into the staging buffer, plane
 * after plane, at the canvas width with 16-aligned rows. Plane widths are
 * in half bytes per pixel so 4:2:0 planar chroma fits.
 *
 * Byte orders were probed on the target: iformat 0 reads Y0 U Y1 V,
 * iformat 1 R G B, linear iformat 12 X R G B. REG9_ENDN permutes the bytes
 * of each 64-bit word on the way in (output byte i takes input byte field
 * i; the default reverses the word), which reaches UYVY and the other
 * 32-bit orders. Three-byte BGR cannot be reached that way.
 */
#define ENDN(b0, b1, b2, b3, b4, b5, b6, b7) \
	((b0) | (b1) << 3 | (b2) << 6 | (b3) << 9 | (b4) << 12 | (b5) << 15 | \
	 (b6) << 18 | (b7) << 21)
#define ENDN_DEFAULT	ENDN(7, 6, 5, 4, 3, 2, 1, 0)

static const struct venc_format venc_formats[] = {
	/* FFmpeg sends single-planar fourccs on MPLANE queues: accept them. */
	{ V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_NV12, 2, { 2, 2 }, { 1, 2 }, 3, 0, 32 },
	{ V4L2_PIX_FMT_NV21M, V4L2_PIX_FMT_NV21, 2, { 2, 2 }, { 1, 2 }, 2, 0, 32 },
	{ V4L2_PIX_FMT_YUV420M, V4L2_PIX_FMT_YUV420, 3, { 2, 1, 1 }, { 1, 2, 2 }, 4, 0, 64 },
	{ V4L2_PIX_FMT_YUV444M, 0, 3, { 2, 2, 2 }, { 1, 1, 1 }, 5, 0, 32 },
	{ V4L2_PIX_FMT_YUYV, 0, 1, { 4 }, { 1 }, 0, 0, 32 },
	{ V4L2_PIX_FMT_UYVY, 0, 1, { 4 }, { 1 }, 0, 0, 32, ENDN(6, 7, 4, 5, 2, 3, 0, 1) },
	/* iformat 1 garbles lines over 1376 pixels (4128 bytes) */
	{ V4L2_PIX_FMT_RGB24, 0, 1, { 6 }, { 1 }, 1, 1, 32, 0, 1376 },
	/* FFmpeg spells 0rgb and bgr0 with the deprecated RGB32/BGR32 */
	{ V4L2_PIX_FMT_XRGB32, V4L2_PIX_FMT_RGB32, 1, { 8 }, { 1 }, 12, 1, 128,
	  ENDN(3, 2, 1, 0, 7, 6, 5, 4) },
	{ V4L2_PIX_FMT_XBGR32, V4L2_PIX_FMT_BGR32, 1, { 8 }, { 1 }, 12, 1, 128,
	  ENDN(0, 1, 2, 3, 4, 5, 6, 7) },
	{ V4L2_PIX_FMT_RGBX32, 0, 1, { 8 }, { 1 }, 12, 1, 128, ENDN(2, 1, 0, 3, 6, 5, 4, 7) },
	{ V4L2_PIX_FMT_BGRX32, 0, 1, { 8 }, { 1 }, 12, 1, 128, ENDN(1, 2, 3, 0, 5, 6, 7, 4) },
};

static const struct venc_format *venc_find_format(u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(venc_formats); i++)
		if (fourcc == venc_formats[i].fourcc || fourcc == venc_formats[i].alias)
			return &venc_formats[i];
	return NULL;
}

/* Staging canvas width in bytes of plane @i, from the vendor alignments */
static u32 venc_canvas_bytes(const struct venc_format *fmt, u32 width, unsigned int i)
{
	u32 luma = ALIGN(ALIGN(width, 16) * fmt->halves[0] / 2, fmt->align);

	return luma * fmt->halves[i] / fmt->halves[0];
}

/* Bytes the staging buffer needs for one frame */
static u32 venc_staging_bytes(const struct venc_format *fmt, u32 width, u32 height)
{
	u32 rows = ALIGN(height, 16), bytes = 0;
	unsigned int i;

	for (i = 0; i < fmt->planes; i++)
		bytes += venc_canvas_bytes(fmt, width, i) * rows / fmt->vsub[i];
	return bytes;
}

/*
 * Plane geometry as reported to userspace. FFmpeg's frame pool gives
 * multi-planar frames a linesize of FFALIGN(width, 32) (chroma /2 for
 * YUV420) and FFALIGN(height, 32) rows and copies whole plane buffers into
 * the queued V4L2 buffers, so the 4:2:0 planes keep exactly that geometry.
 * Other formats use the same 32-byte line alignment.
 */
static void venc_fill_out_fmt(struct v4l2_pix_format_mplane *pix,
			      const struct venc_format *fmt)
{
	u32 rows = ALIGN(pix->height, 32);
	u32 bpl = ALIGN(pix->width * fmt->halves[0] / 2, 32);
	unsigned int i;

	pix->pixelformat = fmt->fourcc;
	pix->field = V4L2_FIELD_NONE;
	pix->num_planes = fmt->planes;
	for (i = 0; i < fmt->planes; i++) {
		pix->plane_fmt[i].bytesperline = bpl * fmt->halves[i] / fmt->halves[0];
		pix->plane_fmt[i].sizeimage =
			ALIGN(pix->plane_fmt[i].bytesperline * rows / fmt->vsub[i] + 64, 8);
	}
}

/*
 * OUTPUT colorimetry is kept as the client sets it. RGB is taken as full
 * range and converted to limited-range BT.709 when ycbcr_enc asks for it
 * (or defaults to it from the colorspace), otherwise BT.601.
 */
static void venc_try_colorimetry(struct v4l2_pix_format_mplane *pix,
				 const struct venc_format *fmt)
{
	if (!fmt->r2y)
		return;
	if (pix->ycbcr_enc == V4L2_YCBCR_ENC_DEFAULT)
		pix->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(pix->colorspace);
	if (pix->ycbcr_enc != V4L2_YCBCR_ENC_709)
		pix->ycbcr_enc = V4L2_YCBCR_ENC_601;
	pix->quantization = V4L2_QUANTIZATION_FULL_RANGE;
}

/* The coded stream carries the input's colorimetry, after any conversion. */
static void venc_fill_cap_fmt(struct venc_ctx *ctx, struct v4l2_pix_format_mplane *pix,
			      u32 fourcc)
{
	pix->pixelformat = fourcc;
	pix->field = V4L2_FIELD_NONE;
	pix->num_planes = 1;
	pix->plane_fmt[0].bytesperline = 0;
	pix->plane_fmt[0].sizeimage = VENC_BITSTREAM_SIZE;
	if (fourcc == V4L2_PIX_FMT_JPEG) {
		pix->colorspace = V4L2_COLORSPACE_JPEG;
		pix->ycbcr_enc = V4L2_YCBCR_ENC_601;
		pix->quantization = V4L2_QUANTIZATION_FULL_RANGE;
		return;
	}
	pix->colorspace = ctx->colorspace;
	pix->xfer_func = ctx->xfer_func;
	pix->ycbcr_enc = ctx->ycbcr_enc;
	pix->quantization = ctx->src->r2y ? V4L2_QUANTIZATION_LIM_RANGE :
			    ctx->quantization;
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

	memcpy(ctx->buffers[VENC_WORKSPACE].vaddr,
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
	u32 cw = ALIGN(ctx->coded_width, 32);	/* references are always NV12 */
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
	venc_write(core, HENC_SCRATCH_B, ENC_PIC_INIT_QP);	/* QPPICTURE */
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
 * Vendor avc_prot_init (UCODE_MODE_FULL, me pixel mode 0):
 * per-picture QDCT/VLC/IE/ME setup.
 */
static void venc_prot_init(struct venc_ctx *ctx, bool idr)
{
	struct venc_core *core = ctx->core;
	u32 qp = ctx->quant;
	u32 qp_c = venc_chroma_qp[min(qp, (u32)51)];

	venc_write(core, VLC_PIC_SIZE, ctx->coded_width | (ctx->height << 16));
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
 * RGB2YUV modes fitted on the target from an RGB palette: 1 is BT.601 and
 * 3 is BT.709, both limited range (error under 0.6 levels). Modes 0 and 2
 * give full-range luma with unusable chroma and are not used.
 */
#define MFDIN_R2Y_BT601		1
#define MFDIN_R2Y_BT709		3

/*
 * Vendor mfdin_basic with 4:2:0 output: 4:2:2 and 4:4:4 inputs are
 * downsampled and RGB is converted. H.264 sets the RGB2YUV mode only with
 * conversion on (vendor encoder.c); JPEG always sets mode 1 and picks the
 * 8x8 block order (vendor jpegenc.c). Linear inputs (iformat >= 8) take
 * the staging address directly; 3 in LNR0 selects 4 bytes per pixel.
 */
static void venc_mfdin_basic(struct venc_ctx *ctx, u32 input)
{
	struct venc_core *core = ctx->core;
	const struct venc_format *fmt = ctx->src;
	u32 iformat = fmt->iformat;
	bool linear = iformat >= 8;
	bool ifmt444 = iformat == 1 || iformat == 5 || iformat == 12;
	bool ifmt422 = iformat == 0;
	bool jpeg = ctx->dst_fmt == V4L2_PIX_FMT_JPEG;
	u32 idx0_bppx = iformat == 1 ? 3 : iformat == 0 ? 2 : 1;
	u32 idx1_bppx = iformat == 4 ? 0 : 1;
	u32 idx1_bppy = iformat == 5 ? 1 : 0;
	u32 r2y_mode = 0;

	if (fmt->r2y)
		r2y_mode = ctx->ycbcr_enc == V4L2_YCBCR_ENC_709 ?
			   MFDIN_R2Y_BT709 : MFDIN_R2Y_BT601;
	else if (jpeg)
		r2y_mode = MFDIN_R2Y_BT601;
	venc_write(core, HCODEC_MFDIN_REG1_CTRL,
		   iformat | ((ifmt444 || ifmt422) << 6) | (fmt->r2y << 12) |
		   (r2y_mode << 13) | (jpeg ? 2u << 29 : 0));
	venc_write(core, HCODEC_MFDIN_REG3_CANV,
		   (linear ? 0 : input & 0xffffff) | (idx1_bppy << 30) | (1u << 28) |
		   (idx1_bppx << 26) | (idx0_bppx << 24));
	venc_write(core, HCODEC_MFDIN_REG4_LNR0, linear ?
		   (3 << 16) | venc_canvas_bytes(fmt, ctx->coded_width, 0) : 0);
	venc_write(core, HCODEC_MFDIN_REG5_LNR1, linear ? ctx->input[0] : 0);
	venc_write(core, HCODEC_MFDIN_REG8_DMBL,
		   (ctx->coded_width << 12) | ALIGN(ctx->height, 16));
	venc_write(core, HCODEC_MFDIN_REG9_ENDN, fmt->endn ?: ENDN_DEFAULT);
}

/*
 * Vendor set_input_format, LOCAL_BUFF path: point one MFDIN canvas at each
 * plane venc_stage_input() chose, staged or in place.
 */
static void venc_set_input_format(struct venc_ctx *ctx)
{
	struct venc_core *core = ctx->core;
	const struct venc_format *fmt = ctx->src;
	u32 rows = ALIGN(ctx->height, 16), input = 0;
	unsigned int i;

	for (i = 0; i < fmt->planes; i++) {
		meson_canvas_config(core->canvas, ctx->canvas[6 + i], ctx->input[i],
				    venc_canvas_bytes(fmt, ctx->coded_width, i),
				    rows / fmt->vsub[i], MESON_CANVAS_WRAP_NONE,
				    MESON_CANVAS_BLKMODE_LINEAR, 0);
		input |= ctx->canvas[6 + i] << (8 * i);
	}
	venc_mfdin_basic(ctx, input);
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
	ret = regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0 - 0xe0,
				 0x3, 0);
	if (ret)
		goto disable_clocks;
	udelay(10);

	venc_write(core, DOS_SW_RESET1, 0xffffffff);
	venc_write(core, DOS_SW_RESET1, 0);

	/* HDEC clock gates 12..26 of DOS_GCLK_EN0 (vendor hvdec_clock_enable) */
	meson8b_runtime_update_dos_bits(DOS_GCLK_EN0, 0x7fff << 12, 0x7fff << 12);

	/* power up HCODEC memories */
	venc_write(core, DOS_MEM_PD_HCODEC, 0);

	/* remove HCODEC isolation */
	ret = regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_ISO0 - 0xe0,
				 0x30, 0);
	if (ret) {
		venc_write(core, DOS_MEM_PD_HCODEC, 0xffffffff);
		meson8b_runtime_update_dos_bits(DOS_GCLK_EN0, 0x7fff << 12, 0);
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0 - 0xe0,
				   0x3, 0x3);
		goto disable_clocks;
	}
	udelay(10);

	/* auto-clock-gate pulse */
	meson8b_runtime_update_dos_bits(DOS_GEN_CTRL0, BIT(0), BIT(0));
	meson8b_runtime_update_dos_bits(DOS_GEN_CTRL0, BIT(0), 0);

	mdelay(10);
	return 0;
disable_clocks:
	clk_disable_unprepare(core->hcodec_clk);
	clk_disable_unprepare(core->dos_clk);
	return ret;
}

/* Vendor avc_poweroff (Meson8b path) */
static void venc_poweroff(struct venc_core *core)
{
	/* enable HCODEC isolation */
	regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_ISO0 - 0xe0,
			   0x30, 0x30);
	/* power off HCODEC memories */
	venc_write(core, DOS_MEM_PD_HCODEC, 0xffffffff);
	/* HCODEC clock gates off */
	meson8b_runtime_update_dos_bits(DOS_GCLK_EN0, 0x7fff << 12, 0);
	clk_disable_unprepare(core->hcodec_clk);
	/* power off HCODEC */
	regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0 - 0xe0,
			   0x3, 0x3);
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

/*
 * When a picture outgrows the bitstream buffer the VLC stops at its end
 * (nothing is written past VLC_VB_END_PTR) and the microcode waits for
 * room forever. Report that as -ENOSPC after a short wait instead of the
 * full command timeout.
 */
static int venc_wait_cmd(struct venc_core *core, u32 done_code)
{
	unsigned int slices = ENC_TIMEOUT_MS / ENC_WAIT_SLICE_MS;

	while (!wait_for_completion_timeout(&core->done,
					    msecs_to_jiffies(ENC_WAIT_SLICE_MS))) {
		if (venc_read(core, VLC_VB_LEFT) < ENC_VB_FULL_BYTES)
			return -ENOSPC;
		if (!--slices)
			return -ETIMEDOUT;
	}
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
	if (core->ctx->dst_fmt == V4L2_PIX_FMT_JPEG) {
		if (status == JPEGENC_DONE) {
			core->done_pending = false;
			complete(&core->done);
		}
		return IRQ_HANDLED;
	}

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
	dma_addr_t base = ctx->buffers[VENC_WORKSPACE].paddr;
	u32 cw = ALIGN(ctx->coded_width, 32);	/* references are always NV12 */
	u32 ch = ALIGN(ctx->height, 16);

	ctx->mem.dct_start = ctx->buffers[VENC_INPUT].paddr;
	ctx->mem.dct_end = ctx->mem.dct_start + ctx->buffers[VENC_INPUT].size - 1;
	ctx->mem.dec0_y = ctx->buffers[VENC_REF0].paddr;
	ctx->mem.dec0_uv = ctx->mem.dec0_y + cw * ch;
	ctx->mem.dec1_y = ctx->buffers[VENC_REF1].paddr;
	ctx->mem.dec1_uv = ctx->mem.dec1_y + cw * ch;
	ctx->mem.assit = base;
	ctx->mem.bitstream_start = ctx->buffers[VENC_STREAM].paddr;
	ctx->mem.bitstream_end = ctx->mem.bitstream_start + VENC_BITSTREAM_SIZE - 1;
	base = ctx->buffers[VENC_ANALYSIS].paddr;
	ctx->mem.inter_bits = base;
	ctx->mem.inter_mv = base + 0x8000;
	ctx->mem.intra_bits = base + 0x88000;
	ctx->mem.intra_pred = base + 0x90000;

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
	if (ctx->dst_fmt == V4L2_PIX_FMT_JPEG) {
		venc_write(core, HENC_SCRATCH_0, ENCODER_IDLE);
		return 0;
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

static void venc_free_buffers(struct venc_ctx *ctx)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ctx->buffers); i++) {
		if (!ctx->buffers[i].vaddr)
			continue;
		dma_free_coherent(ctx->core->dev, ctx->buffers[i].size,
				  ctx->buffers[i].vaddr, ctx->buffers[i].paddr);
		ctx->buffers[i].vaddr = NULL;
	}
}

static int venc_alloc_buffers(struct venc_ctx *ctx)
{
	static const u32 sizes[] = {
		0x6ba000, 0x300000, 0x300000, 0xc0000, 0x100000, 0x110000
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ctx->buffers); i++) {
		ctx->buffers[i].size = sizes[i];
		/* MFDIN reads raw planes, not the vendor private MB input ABI. */
		if (i == VENC_INPUT)
			ctx->buffers[i].size = PAGE_ALIGN(venc_staging_bytes(ctx->src,
					ctx->coded_width, ctx->height));
		ctx->buffers[i].vaddr = dma_alloc_coherent(ctx->core->dev,
			ctx->buffers[i].size, &ctx->buffers[i].paddr, GFP_KERNEL);
		if (!ctx->buffers[i].vaddr) {
			dev_err(ctx->core->dev, "DMA region %u (%u bytes) unavailable\n",
				i, ctx->buffers[i].size);
			venc_free_buffers(ctx);
			return -ENOMEM;
		}
	}

	memset(ctx->buffers[VENC_INPUT].vaddr, 0, ctx->buffers[VENC_INPUT].size);
	return 0;
}

/*
 * Coded width in whole macroblocks. On the target the AVC microcode times
 * out on pictures 1 or 65 macroblocks wide (widths 1..120 scanned),
 * garbles 2-wide ones and corrupts about one 3-wide session in ten in any
 * input format; 4 and 5 passed 20 of 20. Those widths get padding
 * macroblocks that the SPS crops.
 */
static u32 venc_coded_width(struct venc_ctx *ctx)
{
	u32 mbs = DIV_ROUND_UP(ctx->width, 16);

	if (ctx->dst_fmt != V4L2_PIX_FMT_JPEG) {
		if (mbs < 4)
			mbs = 4;
		else if (mbs == 65)
			mbs = 66;
	}
	return mbs * 16;
}

/*
 * The SEQUENCE command leaves "00 00 00 01 SPS 00 00 00 01 PPS". Crop the
 * padding columns when the coded width differs from the visible one.
 */
static int venc_crop_sps(struct venc_ctx *ctx)
{
	u8 sps[2 * VENC_SPS_MAX], *headers = ctx->sps_pps, *merged;
	u32 size = ctx->sps_pps_size, end, len;

	if (ctx->coded_width == ctx->width)
		return 0;
	for (end = 4; end + 4 <= size && memcmp(headers + end, "\0\0\0\1", 4); end++)
		;
	if (size < 4 || memcmp(headers, "\0\0\0\1", 4) || end + 4 > size)
		return -EINVAL;
	len = venc_sps_crop_right(headers + 4, end - 4,
				  (ctx->coded_width - ctx->width) / 2, sps);
	if (!len)
		return -EINVAL;
	merged = kmalloc(4 + len + size - end, GFP_KERNEL);
	if (!merged)
		return -ENOMEM;
	memcpy(merged, headers, 4);
	memcpy(merged + 4, sps, len);
	memcpy(merged + 4 + len, headers + end, size - end);
	kfree(ctx->sps_pps);
	ctx->sps_pps = merged;
	ctx->sps_pps_size = 4 + len + size - end;
	return 0;
}

static int venc_hw_start(struct venc_ctx *ctx)
{
	struct venc_core *core = ctx->core;
	const char *fw_name = ctx->dst_fmt == V4L2_PIX_FMT_JPEG ?
		VENC_JPEG_FW_NAME : VENC_FW_NAME;
	int ret, i;

	ctx->coded_width = venc_coded_width(ctx);
	ret = venc_alloc_buffers(ctx);
	if (ret)
		return ret;

	ret = request_firmware(&ctx->fw, fw_name, core->dev);
	if (ret) {
		dev_err(core->dev, "missing firmware %s\n", fw_name);
		goto err_free;
	}
	if (ctx->fw->size != VENC_MC_SIZE) {
		ret = -EINVAL;
		goto err_firmware;
	}

	for (i = 0; i < 9; i++) {
		ret = meson_canvas_alloc(core->canvas, &ctx->canvas[i]);
		if (ret)
			goto err_canvas;
	}

	ctx->idr_pic_id = 0;
	ctx->frame_number = 0;
	ctx->poc_lsb = 0;

	ret = venc_hw_init(ctx);
	if (ret)
		goto err_canvas;

	core->running = true;
	dev_info(core->dev, "loaded firmware %s (%zu bytes)\n",
		 fw_name, ctx->fw->size);
	if (ctx->dst_fmt == V4L2_PIX_FMT_JPEG)
		return 0;

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
		       venc_stream_vaddr(ctx),
		       ctx->sps_pps_size + pps);
		ctx->sps_pps_size += pps;
		ctx->sps_pps_valid = true;
		ret = venc_crop_sps(ctx);
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
err_firmware:
	release_firmware(ctx->fw);
	ctx->fw = NULL;
err_free:
	venc_free_buffers(ctx);
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
	venc_free_buffers(ctx);
	core->running = false;
}

static void venc_hw_stop(struct venc_ctx *ctx)
{
	ctx->core->done_pending = false;
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
 * Point ctx->input at this frame's planes. A DMA-BUF NV12/NV21 frame whose
 * planes each map to one contiguous range with the canvas line width feeds
 * MFDIN in place. Anything else is copied into the staging buffer as the
 * canvases of venc_set_input_format() expect it; rows and columns outside
 * the visible size stay as zeroed at allocation, so edge macroblocks encode
 * deterministic data. Source strides are the negotiated ones.
 */
static int venc_stage_input(struct venc_ctx *ctx, struct vb2_buffer *b)
{
	const struct venc_format *fmt = ctx->src;
	struct v4l2_pix_format_mplane pix = {
		.width = ctx->width, .height = ctx->height,
	};
	dma_addr_t staging = ctx->buffers[VENC_INPUT].paddr;
	u8 *dst = ctx->buffers[VENC_INPUT].vaddr, *src;
	u32 rows = ALIGN(ctx->height, 16), width;
	bool direct = fmt->planes == 2;
	unsigned int i;
	ktime_t start;

	venc_fill_out_fmt(&pix, fmt);
	for (i = 0; i < fmt->planes; i++) {
		ctx->input[i] = venc_input_dma(b, i);
		direct &= ctx->input[i] && !(ctx->input[i] & 7) &&
			  pix.plane_fmt[i].bytesperline ==
			  venc_canvas_bytes(fmt, ctx->coded_width, i);
	}
	if (direct) {
		ctx->core->direct_frames++;
		return 0;
	}
	start = ktime_get();
	for (i = 0; i < fmt->planes; i++) {
		width = venc_canvas_bytes(fmt, ctx->coded_width, i);
		src = venc_input_begin_cpu(b, i);
		if (!src)
			return -EFAULT;
		venc_copy_rows(dst, width, src, pix.plane_fmt[i].bytesperline,
			       ctx->width * fmt->halves[i] / 2,
			       ctx->height / fmt->vsub[i], vb2_plane_size(b, i));
		venc_input_end_cpu(b, i);
		ctx->input[i] = staging;
		staging += width * rows / fmt->vsub[i];
		dst += width * rows / fmt->vsub[i];
	}
	ctx->core->copied_frames++;
	ctx->core->copy_ns += ktime_to_ns(ktime_sub(ktime_get(), start));
	return 0;
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

/* Encode one picture of the type and ctx->quant the work picked. */
static int venc_encode_frame(struct venc_ctx *ctx, struct vb2_buffer *src,
			     struct vb2_buffer *dst, bool idr)
{
	struct venc_core *core = ctx->core;
	u32 done_code = idr ? ENCODER_IDR_DONE : ENCODER_NON_IDR_DONE;
	u32 es_size;
	int ret;

	ret = venc_stage_input(ctx, src);
	if (ret)
		return ret;
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
		if (ret == -ENOSPC)
			dev_warn_ratelimited(core->dev,
					     "%s picture at QP %u exceeds the %u KiB bitstream buffer\n",
					     idr ? "IDR" : "P", ctx->quant,
					     VENC_BITSTREAM_SIZE / 1024);
		else
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
		       venc_stream_vaddr(ctx), es_size);
		vb2_set_plane_payload(dst, 0, hdr + es_size);
	} else {
		memcpy(vb2_plane_vaddr(dst, 0),
		       venc_stream_vaddr(ctx), es_size);
		vb2_set_plane_payload(dst, 0, es_size);
	}

	venc_frame_done(ctx, idr);
	return 0;
}

static int venc_encode_jpeg(struct venc_ctx *ctx, struct vb2_buffer *src,
			    struct vb2_buffer *dst)
{
	struct venc_core *core = ctx->core;
	u8 *stream = venc_stream_vaddr(ctx);
	u32 size, ptr;
	int ret;

	ret = venc_stage_input(ctx, src);
	if (ret)
		return ret;
	venc_amrisc_stop(core);
	venc_set_input_format(ctx);
	venc_jpeg_init(ctx);
	venc_write(core, HCODEC_ASSIST_MBOX2_CLR_REG, 1);
	venc_write(core, HCODEC_ASSIST_MBOX2_MASK, 1);
	core->hw_status = ENCODER_IDLE;
	reinit_completion(&core->done);
	core->done_pending = true;
	venc_write(core, HENC_SCRATCH_0, ENCODER_IDLE);
	dma_wmb();
	venc_amrisc_start(core);
	ret = venc_wait_cmd(core, JPEGENC_DONE);
	core->done_pending = false;
	if (ret) {
		if (ret == -ENOSPC)
			dev_warn_ratelimited(core->dev,
					     "JPEG at quality %u exceeds the %u KiB bitstream buffer\n",
					     ctx->jpeg_quality, VENC_BITSTREAM_SIZE / 1024);
		else
			dev_err(core->dev, "JPEG encode failed (%d), hw_status %u\n",
				ret, core->hw_status);
		venc_hw_stop(ctx);
		return ret;
	}
	dma_rmb();
	size = venc_read(core, VLC_TOTAL_BYTES);
	ptr = venc_read(core, VLC_VB_WR_PTR);
	if (size <= VENC_JPEG_HEADER_SIZE || size > VENC_BITSTREAM_SIZE ||
	    size > vb2_plane_size(dst, 0) ||
	    ptr < ctx->mem.bitstream_start + VENC_JPEG_HEADER_SIZE ||
	    stream[size - 2] != 0xff || stream[size - 1] != 0xd9) {
		dev_err(core->dev, "invalid JPEG output: size %u, write pointer %#x\n",
			size, ptr);
		return -EIO;
	}
	memcpy(vb2_plane_vaddr(dst, 0), stream, size);
	vb2_set_plane_payload(dst, 0, size);
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

	unsigned int i;

	if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
		pix.width = ctx->width;
		pix.height = ctx->height;
		venc_fill_out_fmt(&pix, ctx->src);
	} else {
		venc_fill_cap_fmt(ctx, &pix, ctx->dst_fmt);
	}
	if (*nplanes) {
		if (*nplanes != pix.num_planes)
			return -EINVAL;
		for (i = 0; i < *nplanes; i++)
			if (sizes[i] < pix.plane_fmt[i].sizeimage)
				return -EINVAL;
		return 0;
	}
	*nplanes = pix.num_planes;
	for (i = 0; i < *nplanes; i++)
		sizes[i] = pix.plane_fmt[i].sizeimage;

	return 0;
}

static int venc_buf_prepare(struct vb2_buffer *vb)
{
	struct venc_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct v4l2_pix_format_mplane pix = {
		.width = ctx->width, .height = ctx->height,
	};
	unsigned int i;

	if (V4L2_TYPE_IS_OUTPUT(vb->type))
		venc_fill_out_fmt(&pix, ctx->src);
	else
		venc_fill_cap_fmt(ctx, &pix, ctx->dst_fmt);
	for (i = 0; i < pix.num_planes; i++) {
		if (vb2_plane_size(vb, i) < pix.plane_fmt[i].sizeimage)
			return -EINVAL;
		if (V4L2_TYPE_IS_OUTPUT(vb->type)) {
			/* Visible rows are copied; macroblock padding stays zero. */
			if (vb->planes[i].data_offset ||
			    vb2_get_plane_payload(vb, i) <
			    pix.plane_fmt[i].bytesperline * ctx->height / ctx->src->vsub[i])
				return -EINVAL;
		} else {
			vb2_set_plane_payload(vb, i, 0);
		}
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
	.buf_prepare = venc_buf_prepare,
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
	src_vq->mem_ops = &venc_input_memops;
	src_vq->dev = ctx->core->dev;	/* DMA-BUF imports attach to the encoder */
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

/*
 * Pick the next picture's type and QP from the control state. A new
 * session restarts the GOP.
 */
static bool venc_pick_picture(struct venc_ctx *ctx, bool new_session)
{
	bool idr;

	mutex_lock(ctx->ctrl_handler.lock);
	if (new_session)
		ctx->gop_idx = 0;
	idr = ctx->force_key || !ctx->gop_idx ||
	      (ctx->gop_size && ctx->gop_idx >= ctx->gop_size);
	if (ctx->rc_enable && ctx->rc.bitrate) {
		if (ctx->rc_restart) {
			venc_rc_reset(&ctx->rc, ctx->p_qp);
			ctx->rc_restart = false;
		}
		ctx->rc.i_offset = ctx->i_qp - ctx->p_qp;
		ctx->quant = venc_rc_picture_qp(&ctx->rc, idr);
	} else {
		ctx->quant = venc_rc_clamp(&ctx->rc, idr ? ctx->i_qp : ctx->p_qp);
	}
	mutex_unlock(ctx->ctrl_handler.lock);
	return idr;
}

/*
 * Account a finished picture. After a failure the next picture is an IDR;
 * one that overflowed the bitstream buffer also coarsens rate control.
 */
static void venc_account_picture(struct venc_ctx *ctx, bool idr, u32 bytes,
				 int result)
{
	bool rc;

	mutex_lock(ctx->ctrl_handler.lock);
	rc = ctx->rc_enable && ctx->rc.bitrate && !ctx->rc_restart;
	if (result) {
		ctx->gop_idx = 0;
		/* past the ceiling if need be: a lost picture is worse */
		if (rc && result == -ENOSPC) {
			ctx->rc.fullness = venc_rc_capacity(&ctx->rc);
			ctx->rc.qp = venc_rc_clamp(&ctx->rc, ctx->rc.qp + VENC_RC_MAX_UP);
		}
	} else {
		if (idr) {
			ctx->force_key = false;
			ctx->gop_idx = 0;
		}
		if (ctx->gop_idx < U32_MAX)
			ctx->gop_idx++;
		if (rc)
			venc_rc_update(&ctx->rc, bytes, ctx->quant, idr);
	}
	mutex_unlock(ctx->ctrl_handler.lock);
}

static void venc_work(struct work_struct *work)
{
	struct venc_ctx *ctx =
		container_of(work, struct venc_ctx, work);
	struct venc_core *core = ctx->core;
	struct v4l2_m2m_ctx *m2m_ctx = ctx->m2m_ctx;
	struct vb2_v4l2_buffer *src, *dst;
	enum vb2_buffer_state state = VB2_BUF_STATE_DONE;
	bool new_session = false, idr = true;
	int ret;

	src = v4l2_m2m_next_src_buf(m2m_ctx);
	dst = v4l2_m2m_next_dst_buf(m2m_ctx);
	if (!src || !dst) {
		v4l2_m2m_job_finish(core->m2m_dev, m2m_ctx);
		return;
	}

	if (!core->running) {
		ret = venc_hw_start(ctx);
		if (ret) {
			dev_err(core->dev, "session start failed (%d)\n", ret);
			/* Allocation/firmware failure is terminal for these queues. */
			vb2_queue_error(v4l2_m2m_get_src_vq(m2m_ctx));
			vb2_queue_error(v4l2_m2m_get_dst_vq(m2m_ctx));
			state = VB2_BUF_STATE_ERROR;
			goto out;
		}
		new_session = true;
	}

	if (ctx->dst_fmt == V4L2_PIX_FMT_JPEG) {
		ret = venc_encode_jpeg(ctx, &src->vb2_buf, &dst->vb2_buf);
	} else {
		idr = venc_pick_picture(ctx, new_session);
		ret = venc_encode_frame(ctx, &src->vb2_buf, &dst->vb2_buf, idr);
		venc_account_picture(ctx, idr,
				     vb2_get_plane_payload(&dst->vb2_buf, 0), ret);
	}
	if (ret)
		state = VB2_BUF_STATE_ERROR;

out:
	v4l2_m2m_buf_copy_metadata(src, dst, false);
	if (state == VB2_BUF_STATE_DONE && idr)
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
	strscpy(cap->card, "Meson8b H.264/JPEG Encoder",
		sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:meson-venc");
	return 0;
}

static struct venc_ctx *venc_fh_to_ctx(struct file *file)
{
	return container_of(file->private_data, struct venc_ctx, fh);
}

/*
 * JPEG keeps its full-range input contract, so RGB inputs, which MFDIN
 * converts to limited range, are offered for H.264 only.
 */
static bool venc_format_allowed(struct venc_ctx *ctx, const struct venc_format *fmt)
{
	return !fmt->r2y || ctx->dst_fmt != V4L2_PIX_FMT_JPEG;
}

static int venc_enum_fmt(struct file *file, void *fh,
			 struct v4l2_fmtdesc *f)
{
	struct venc_ctx *ctx = venc_fh_to_ctx(file);
	unsigned int i, index = 0;

	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		for (i = 0; i < ARRAY_SIZE(venc_formats); i++) {
			if (!venc_format_allowed(ctx, &venc_formats[i]))
				continue;
			if (index++ == f->index) {
				f->pixelformat = venc_formats[i].fourcc;
				return 0;
			}
		}
		return -EINVAL;
	} else {
		if (f->index > 1)
			return -EINVAL;
		f->pixelformat = f->index ? V4L2_PIX_FMT_JPEG : V4L2_PIX_FMT_H264;
	}
	return 0;
}

static int venc_g_fmt(struct file *file, void *fh,
		      struct v4l2_format *f)
{
	struct venc_ctx *ctx = venc_fh_to_ctx(file);

	f->fmt.pix_mp.width = ctx->width;
	f->fmt.pix_mp.height = ctx->height;
	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		venc_fill_out_fmt(&f->fmt.pix_mp, ctx->src);
		f->fmt.pix_mp.pixelformat = ctx->src_fourcc;
		f->fmt.pix_mp.colorspace = ctx->colorspace;
		f->fmt.pix_mp.xfer_func = ctx->xfer_func;
		f->fmt.pix_mp.ycbcr_enc = ctx->ycbcr_enc;
		f->fmt.pix_mp.quantization = ctx->quantization;
	} else {
		venc_fill_cap_fmt(ctx, &f->fmt.pix_mp, ctx->dst_fmt);
	}
	return 0;
}

static int venc_try_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct venc_ctx *ctx = venc_fh_to_ctx(file);
	const struct venc_format *fmt;
	u32 fourcc;

	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		fmt = venc_find_format(f->fmt.pix_mp.pixelformat);
		if (!fmt || !venc_format_allowed(ctx, fmt))
			fmt = &venc_formats[0];
		f->fmt.pix_mp.width = clamp(f->fmt.pix_mp.width, 16u,
					    fmt->max_width ?: 1920u);
		f->fmt.pix_mp.height = clamp(f->fmt.pix_mp.height, 16u, 1088u);
		f->fmt.pix_mp.width = ALIGN(f->fmt.pix_mp.width, 2);
		f->fmt.pix_mp.height = ALIGN(f->fmt.pix_mp.height, 2);
		/* FFmpeg only knows packed RGB32/BGR32 by their old names */
		fourcc = fmt->planes == 1 && f->fmt.pix_mp.pixelformat == fmt->alias ?
			 fmt->alias : fmt->fourcc;
		venc_fill_out_fmt(&f->fmt.pix_mp, fmt);
		f->fmt.pix_mp.pixelformat = fourcc;
		venc_try_colorimetry(&f->fmt.pix_mp, fmt);
	} else {
		f->fmt.pix_mp.width = clamp(f->fmt.pix_mp.width, 16u, 1920u);
		f->fmt.pix_mp.height = clamp(f->fmt.pix_mp.height, 16u, 1088u);
		fourcc = f->fmt.pix_mp.pixelformat == V4L2_PIX_FMT_JPEG ?
			V4L2_PIX_FMT_JPEG : V4L2_PIX_FMT_H264;
		venc_fill_cap_fmt(ctx, &f->fmt.pix_mp, fourcc);
	}
	return 0;
}

static int venc_s_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct venc_ctx *ctx = venc_fh_to_ctx(file);
	int ret;

	/* Geometry and codec determine both queues and the live firmware. */
	if (vb2_is_busy(v4l2_m2m_get_src_vq(ctx->m2m_ctx)) ||
	    vb2_is_busy(v4l2_m2m_get_dst_vq(ctx->m2m_ctx)))
		return -EBUSY;
	ret = venc_try_fmt(file, fh, f);
	if (ret)
		return ret;

	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		ctx->src = venc_find_format(f->fmt.pix_mp.pixelformat);
		ctx->src_fourcc = f->fmt.pix_mp.pixelformat;
		ctx->width = f->fmt.pix_mp.width;
		ctx->height = f->fmt.pix_mp.height;
		ctx->colorspace = f->fmt.pix_mp.colorspace;
		ctx->xfer_func = f->fmt.pix_mp.xfer_func;
		ctx->ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
		ctx->quantization = f->fmt.pix_mp.quantization;
	} else {
		ctx->dst_fmt = f->fmt.pix_mp.pixelformat;
		/* a JPEG session cannot keep an RGB input */
		if (!venc_format_allowed(ctx, ctx->src)) {
			ctx->src = &venc_formats[0];
			ctx->src_fourcc = ctx->src->fourcc;
		}
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
	struct v4l2_fract *t = &a->parm.output.timeperframe;
	u32 divisor;

	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;
	if (t->numerator && t->denominator) {
		/* bounds keep the rate-control arithmetic in 64 bits */
		divisor = gcd(t->numerator, t->denominator);
		t->numerator = clamp(t->numerator / divisor, 1u, 65535u);
		t->denominator = clamp(t->denominator / divisor, 1u, 65535u);
		/* frame rate only feeds the rate control, not the hardware */
		mutex_lock(ctx->ctrl_handler.lock);
		ctx->timeperframe = *t;
		ctx->rc.numerator = t->numerator;
		ctx->rc.denominator = t->denominator;
		mutex_unlock(ctx->ctrl_handler.lock);
	}
	return venc_g_parm(file, fh, a);
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

static int venc_encoder_cmd(struct file *file, void *fh,
			    struct v4l2_encoder_cmd *cmd)
{
	struct venc_ctx *ctx = venc_fh_to_ctx(file);
	static const struct v4l2_event eos = { .type = V4L2_EVENT_EOS };
	int ret;

	ret = v4l2_m2m_encoder_cmd(file, ctx->m2m_ctx, cmd);
	/* Empty OUTPUT: the helper completes LAST without invoking our work. */
	if (!ret && cmd->cmd == V4L2_ENC_CMD_STOP &&
	    v4l2_m2m_has_stopped(ctx->m2m_ctx))
		v4l2_event_queue_fh(&ctx->fh, &eos);
	return ret;
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
	.vidioc_encoder_cmd = venc_encoder_cmd,
	.vidioc_subscribe_event = venc_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

/* Runs with the control handler lock held, like venc_s_ctrl(). */
static int venc_try_ctrl(struct v4l2_ctrl *ctrl)
{
	struct venc_ctx *ctx =
		container_of(ctrl->handler, struct venc_ctx, ctrl_handler);

	/* MIN_QP masters the QP range cluster; both values are the new ones. */
	if (ctrl->id == V4L2_CID_MPEG_VIDEO_H264_MIN_QP &&
	    ctx->qp_range[0]->val > ctx->qp_range[1]->val)
		return -ERANGE;
	return 0;
}

/*
 * Control values become driver state under the handler lock; the encode
 * work reads them under the same lock between pictures. Never look up
 * controls here: v4l2_ctrl_find() takes the lock this callback holds.
 */
static int venc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct venc_ctx *ctx =
		container_of(ctrl->handler, struct venc_ctx, ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_JPEG_COMPRESSION_QUALITY:
		ctx->jpeg_quality = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		ctx->gop_size = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE:
		/* a new target keeps the QP and estimates, the bucket restarts */
		if (!ctx->rc.bitrate)
			ctx->rc_restart = true;
		ctx->rc.bitrate = ctrl->val;
		ctx->rc.fullness = 0;
		break;
	case V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE:
		ctx->rc_enable = ctrl->val;
		ctx->rc_restart = true;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_MIN_QP:
		ctx->rc.qp_min = ctx->qp_range[0]->val;
		ctx->rc.qp_max = ctx->qp_range[1]->val;
		ctx->rc.qp = venc_rc_clamp_p(&ctx->rc, ctx->rc.qp);
		break;
	case V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP:
		ctx->i_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP:
		ctx->p_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME:
		ctx->force_key = true;
		break;
	default:
		/* B_FRAMES and HEADER_MODE accept their single supported value. */
		break;
	}
	return 0;
}

static const struct v4l2_ctrl_ops venc_ctrl_ops = {
	.try_ctrl = venc_try_ctrl,
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
	ctx->src = &venc_formats[0];
	ctx->src_fourcc = ctx->src->fourcc;
	ctx->dst_fmt = V4L2_PIX_FMT_H264;
	ctx->quant = ENC_QUANT_DEFAULT;
	ctx->timeperframe.numerator = 1;
	ctx->timeperframe.denominator = 30;
	ctx->rc.numerator = 1;
	ctx->rc.denominator = 30;

	hdl = &ctx->ctrl_handler;
	ret = v4l2_ctrl_handler_init(hdl, 12);
	if (ret)
		goto err_free;
	v4l2_ctrl_new_std(hdl, &venc_ctrl_ops, V4L2_CID_JPEG_COMPRESSION_QUALITY,
			  1, 100, 1, 90);

	v4l2_ctrl_new_std(hdl, &venc_ctrl_ops, V4L2_CID_MPEG_VIDEO_B_FRAMES,
			  0, 0, 1, 0);
	/* 0 keeps fixed QPs, as FFmpeg's -b:v 0 expects */
	v4l2_ctrl_new_std(hdl, &venc_ctrl_ops, V4L2_CID_MPEG_VIDEO_BITRATE,
			  0, 100000000, 1, 0);
	v4l2_ctrl_new_std(hdl, &venc_ctrl_ops, V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE,
			  0, 1, 1, 1);
	/* 0: only the first picture and forced key frames are IDR */
	v4l2_ctrl_new_std(hdl, &venc_ctrl_ops, V4L2_CID_MPEG_VIDEO_GOP_SIZE,
			  0, 65535, 1, ENC_GOP_DEFAULT);
	ctx->qp_range[0] = v4l2_ctrl_new_std(hdl, &venc_ctrl_ops,
					     V4L2_CID_MPEG_VIDEO_H264_MIN_QP,
					     0, 51, 1, 0);
	ctx->qp_range[1] = v4l2_ctrl_new_std(hdl, &venc_ctrl_ops,
					     V4L2_CID_MPEG_VIDEO_H264_MAX_QP,
					     0, 51, 1, 51);
	v4l2_ctrl_new_std(hdl, &venc_ctrl_ops, V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP,
			  0, 51, 1, ENC_QUANT_DEFAULT);
	v4l2_ctrl_new_std(hdl, &venc_ctrl_ops, V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP,
			  0, 51, 1, ENC_QUANT_DEFAULT);
	/* button control: the next queued frame becomes an IDR */
	v4l2_ctrl_new_std(hdl, &venc_ctrl_ops, V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME,
			  0, 0, 0, 0);
	/* SPS/PPS lead every IDR; a separate header buffer is not produced */
	v4l2_ctrl_new_std_menu(hdl, &venc_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_HEADER_MODE,
			       V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME,
			       BIT(V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE),
			       V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME);
	if (hdl->error) {
		ret = hdl->error;
		goto err_ctrl;
	}
	v4l2_ctrl_cluster(ARRAY_SIZE(ctx->qp_range), ctx->qp_range);
	/* state starts from the control defaults */
	ret = v4l2_ctrl_handler_setup(hdl);
	if (ret)
		goto err_ctrl;

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
	if (!r || r->start != 0xd0050000 || resource_size(r) != SZ_64K)
		return -EINVAL;
	core->dos_base = meson8b_runtime_dos_base();

	/*
	 * Only PWR_CNTL needs a dedicated mapping. Sleep/isolation share
	 * the decoder's PMU regmap and its atomic masked updates.
	 */
	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ao-pwr");
	core->ao_pwr_base = devm_ioremap_resource(dev, r);
	if (IS_ERR(core->ao_pwr_base))
		return PTR_ERR(core->ao_pwr_base);

	core->regmap_ao = syscon_regmap_lookup_by_phandle(dev->of_node,
						       "amlogic,ao-sysctrl");
	if (IS_ERR(core->regmap_ao))
		return PTR_ERR(core->regmap_ao);

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

	core->debugfs = debugfs_create_dir("meson-venc", NULL);
	debugfs_create_u64("direct_frames", 0444, core->debugfs, &core->direct_frames);
	debugfs_create_u64("copied_frames", 0444, core->debugfs, &core->copied_frames);
	debugfs_create_u64("copy_ns", 0444, core->debugfs, &core->copy_ns);
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

	debugfs_remove_recursive(core->debugfs);
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

MODULE_DESCRIPTION("Meson8b HCODEC H.264/JPEG V4L2 m2m encoder");
MODULE_FIRMWARE(VENC_FW_NAME);
MODULE_FIRMWARE(VENC_JPEG_FW_NAME);
MODULE_AUTHOR("YoungSx");
MODULE_LICENSE("GPL");
