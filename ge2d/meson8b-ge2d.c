// SPDX-License-Identifier: GPL-2.0
/* S805 canvas GE2D; register protocol from Hardkernel Linux 5aed95d35d25.
 * A byte-plane self-test proves endian conversion before exposing the device.
 */
#include <linux/clk.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/dma-resv.h>
#include <linux/iopoll.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/soc/amlogic/meson-canvas.h>
#include <linux/workqueue.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>
#include "../runtime/meson8b-runtime.h"
#include "ge2d-layout.h"
#include "ge2d-scale.h"
#include "ge2d-api.h"

#define GEN_CTRL0 0x00
#define GEN_CTRL1 0x04
#define GEN_CTRL2 0x08
#define CMD_CTRL 0x0c
#define STATUS0 0x10
#define SRC_CLIPX 0x1c
#define SRC_CLIPY 0x20
#define SRC_CANVAS 0x24
#define SRC_X 0x28
#define SRC_Y 0x2c
#define SRC_FMT 0x38
#define SRC2_DEF_COLOR 0x3c
#define SRC2_CLIPX 0x40
#define SRC2_CLIPY 0x44
#define SRC2_X 0x48
#define SRC2_Y 0x4c
#define DST_CLIPX 0x50
#define DST_CLIPY 0x54
#define DST_X 0x58
#define DST_Y 0x5c
#define DST_CANVAS 0x60
#define VSC_STEP 0x64
#define VSC_SLOPE 0x68
#define VSC_INI 0x6c
#define HSC_STEP 0x70
#define HSC_SLOPE 0x74
#define HSC_INI 0x78
#define HSC_ADV 0x7c
#define SC_MISC 0x80
#define MATRIX_PRE_OFFSET 0x94
#define MATRIX_COEF00_01 0x98
#define MATRIX_COEF02_10 0x9c
#define MATRIX_COEF11_12 0xa0
#define MATRIX_COEF20_21 0xa4
#define MATRIX_CTRL 0xa8
#define MATRIX_OFFSET 0xac
#define ALU_CTRL 0xb0
#define ALU_CONST_COLOR 0xb4
#define DP_ONOFF 0xcc
#define SCALE_IDX 0xd0
#define SCALE_COEF 0xd4
#define OUTSIDE_ALPHA 0xd8
#define ANTIFLICK 0xe0
#define GEN_CTRL3 0x120
#define SOFT_RESET BIT(31)
/* Include buffered/pipeline commands and outstanding destination writes. */
#define BUSY_MASK (BIT(0) | BIT(1) | BIT(2) | BIT(4) | GENMASK(28, 17))
#define V4L2_CID_MESON8B_GE2D_BLEND (V4L2_CID_USER_BASE + 0x10)

struct ge2d_ctx;

struct meson8b_ge2d {
	struct device *dev;
	void __iomem *regs;
	struct clk *clk;
	struct reset_control *reset;
	struct meson_canvas *canvas;
	u8 src_canvas, dst_canvas, src_uv_canvas, dst_uv_canvas, src_v_canvas, src2_canvas;
	struct v4l2_device v4l2;
	struct video_device *video;
	struct v4l2_m2m_dev *m2m;
	struct mutex lock; /* V4L2 ioctl and vb2 queue serialization. */
	struct mutex hardware_lock; /* Serialize userspace and in-kernel commands. */
	struct work_struct work;
	struct ge2d_ctx *active;
	void *blend_stage_vaddr;
	dma_addr_t blend_stage_dma;
	size_t blend_stage_size;
	bool offline;
	bool ready;
};

struct ge2d_ctx {
	struct v4l2_fh fh;
	struct v4l2_ctrl_handler ctrl_handler;
	struct meson8b_ge2d *core;
	struct v4l2_pix_format_mplane input, output;
	struct meson8b_ge2d_crop crop;
	u32 sequence;
};

static void ge2d_write(struct meson8b_ge2d *core, u32 reg, u32 value)
{
	writel(value, core->regs + reg);
}

static int ge2d_reset(struct meson8b_ge2d *core)
{
	u32 status;
	int ret;

	ret = reset_control_reset(core->reset);
	if (ret)
		return ret;
	ge2d_write(core, GEN_CTRL1, SOFT_RESET);
	readl(core->regs + GEN_CTRL1);
	ge2d_write(core, GEN_CTRL1, 0);
	return readl_poll_timeout(core->regs + STATUS0, status,
				 !(status & BUSY_MASK), 10, 10000);
}

struct ge2d_hw_fmt {
	u8 hw_fmt;
	u8 cmap_le;
	u8 cmap_be;
	bool sep_en;
	bool x_yc_ratio;
	bool y_yc_ratio;
	bool dst2_en;
};

static const struct ge2d_hw_fmt *ge2d_find_hw_fmt(enum meson8b_ge2d_format fmt)
{
	static const struct ge2d_hw_fmt table[] = {
		[MESON8B_GE2D_FMT_GREY] = { 0, 0, 0, false, false, false, false },
		[MESON8B_GE2D_FMT_NV12] = { 2, 15, 14, true, true, true, true },
		[MESON8B_GE2D_FMT_NV21] = { 2, 14, 15, true, true, true, true },
		[MESON8B_GE2D_FMT_YUV420] = { 0, 0, 0, true, true, true, false },
		[MESON8B_GE2D_FMT_YVU420] = { 0, 0, 0, true, true, true, false },
		[MESON8B_GE2D_FMT_UYVY] = { 1, 0, 0, false, true, false, false },
		[MESON8B_GE2D_FMT_RGB24] = { 2, 5, 5, false, false, false, false },
		[MESON8B_GE2D_FMT_BGR24] = { 2, 0, 0, false, false, false, false },
		[MESON8B_GE2D_FMT_XBGR32] = { 3, 1, 1, false, false, false, false },
		[MESON8B_GE2D_FMT_RGBX32] = { 3, 2, 2, false, false, false, false },
		[MESON8B_GE2D_FMT_XRGB32] = { 3, 3, 3, false, false, false, false },
		[MESON8B_GE2D_FMT_ABGR32] = { 3, 1, 1, false, false, false, false },
		[MESON8B_GE2D_FMT_RGBA32] = { 3, 2, 2, false, false, false, false },
		[MESON8B_GE2D_FMT_ARGB32] = { 3, 3, 3, false, false, false, false },
	};

	if (fmt <= MESON8B_GE2D_FMT_DEFAULT || fmt > MESON8B_GE2D_FMT_ARGB32)
		return NULL;
	return &table[fmt];
}

struct ge2d_matrix_spec {
	u16 coef[9];
	u32 pre_offset;
	u32 offset;
	bool sat_in_en;
};

static void ge2d_write_matrix(struct meson8b_ge2d *core,
			      enum meson8b_ge2d_format src_fmt,
			      enum meson8b_ge2d_format dst_fmt,
			      enum meson8b_ge2d_colorspace src_cs,
			      enum meson8b_ge2d_colorspace dst_cs)
{
	/* Vendor ge2d_hw.c YCC<->RGB tables plus ITU-R BT.709 Q10 matrices. */
	static const struct ge2d_matrix_spec yuv_to_rgb[] = {
		[MESON8B_GE2D_CS_BT601_LIM] = {
			.coef = { 0x4a8, 0x000, 0x662, 0x4a8, 0x1e6f, 0x1cbf,
				  0x4a8, 0x811, 0x000 },
			.pre_offset = (0x1f0u << 20) | (0x180u << 10) | 0x180u,
			.sat_in_en = true,
		},
		[MESON8B_GE2D_CS_BT601_FULL] = {
			.coef = { 0x400, 0x000, 0x59c, 0x400, 0x1ea0, 0x1d25,
				  0x400, 0x717, 0x000 },
			.pre_offset = (0x180u << 10) | 0x180u,
		},
		[MESON8B_GE2D_CS_BT709_LIM] = {
			.coef = { 0x4a8, 0x000, 0x72c, 0x4a8, 0x1f26, 0x1dde,
				  0x4a8, 0x873, 0x000 },
			.pre_offset = (0x1f0u << 20) | (0x180u << 10) | 0x180u,
			.sat_in_en = true,
		},
		[MESON8B_GE2D_CS_BT709_FULL] = {
			.coef = { 0x400, 0x000, 0x64d, 0x400, 0x1f40, 0x1e21,
				  0x400, 0x76d, 0x000 },
			.pre_offset = (0x180u << 10) | 0x180u,
		},
	};
	static const struct ge2d_matrix_spec rgb_to_yuv[] = {
		[MESON8B_GE2D_CS_BT601_LIM] = {
			.coef = { 0x107, 0x204, 0x064, 0x1f68, 0x1ed6, 0x1c2,
				  0x1c2, 0x1e87, 0x1fb7 },
			.offset = (16u << 20) | (128u << 10) | 128u,
		},
		[MESON8B_GE2D_CS_BT601_FULL] = {
			.coef = { 0x132, 0x259, 0x075, 0x1f53, 0x1ead, 0x200,
				  0x200, 0x1e53, 0x1fad },
			.offset = (128u << 10) | 128u,
		},
		[MESON8B_GE2D_CS_BT709_LIM] = {
			.coef = { 0x0bb, 0x275, 0x03f, 0x1f99, 0x1ea5, 0x1c2,
				  0x1c2, 0x1e67, 0x1fd7 },
			.offset = (16u << 20) | (128u << 10) | 128u,
		},
		[MESON8B_GE2D_CS_BT709_FULL] = {
			.coef = { 0x0da, 0x2dc, 0x04a, 0x1f8b, 0x1e75, 0x200,
				  0x200, 0x1e2f, 0x1fd1 },
			.offset = (128u << 10) | 128u,
		},
	};
	bool src_yuv = meson8b_ge2d_format_is_yuv(src_fmt);
	bool dst_yuv = meson8b_ge2d_format_is_yuv(dst_fmt);
	bool src_rgb = meson8b_ge2d_format_is_rgb(src_fmt);
	bool dst_rgb = meson8b_ge2d_format_is_rgb(dst_fmt);
	const struct ge2d_matrix_spec *m;
	enum meson8b_ge2d_colorspace cs;

	if (!((src_yuv && dst_rgb) || (src_rgb && dst_yuv))) {
		ge2d_write(core, MATRIX_CTRL, 0);
		return;
	}
	cs = (src_yuv ? src_cs : dst_cs);
	if (cs == MESON8B_GE2D_CS_DEFAULT)
		cs = MESON8B_GE2D_CS_BT601_LIM;
	m = src_yuv ? &yuv_to_rgb[cs] : &rgb_to_yuv[cs];
	ge2d_write(core, MATRIX_PRE_OFFSET, m->pre_offset);
	ge2d_write(core, MATRIX_COEF00_01, ((u32)m->coef[0] << 16) | m->coef[1]);
	ge2d_write(core, MATRIX_COEF02_10, ((u32)m->coef[2] << 16) | m->coef[3]);
	ge2d_write(core, MATRIX_COEF11_12, ((u32)m->coef[4] << 16) | m->coef[5]);
	ge2d_write(core, MATRIX_COEF20_21, ((u32)m->coef[6] << 16) | m->coef[7]);
	ge2d_write(core, MATRIX_CTRL,
		   ((u32)m->coef[8] << 16) | (m->sat_in_en ? BIT(7) : 0) | BIT(0));
	ge2d_write(core, MATRIX_OFFSET, m->offset);
}

static void ge2d_write_alu(struct meson8b_ge2d *core,
			   enum meson8b_ge2d_format src_fmt,
			   const struct meson8b_ge2d_crop *crop)
{
	u32 const_color = ((crop->bg_color & 0xffffffu) << 8) | crop->global_alpha;
	u32 alu = (5u << 20) | (1u << 12) | (5u << 8) | 1u;

	ge2d_write(core, ALU_CONST_COLOR, const_color);
	if (crop->blend != MESON8B_GE2D_BLEND_COPY) {
		if (meson8b_ge2d_format_has_alpha(src_fmt) &&
		    crop->global_alpha == 255)
			alu = (6u << 16) | (7u << 12) | (1u << 4) | 3u;
		else
			alu = (12u << 16) | (13u << 12) | (6u << 4) | 7u;
	}
	ge2d_write(core, ALU_CTRL, alu);
}

static void ge2d_resolve_dir(const struct meson8b_ge2d_crop *crop,
			     bool *swap, bool *x_rev, bool *y_rev)
{
	*swap = crop->rotate == 90 || crop->rotate == 270;
	*x_rev = crop->rotate == 90 || crop->rotate == 180;
	*y_rev = crop->rotate == 180 || crop->rotate == 270;
	*x_rev ^= crop->hflip;
	*y_rev ^= crop->vflip;
}

/* Source and destination remain held until command completion or reset. */
static int ge2d_blit_one(struct meson8b_ge2d *core,
			 const struct meson8b_ge2d_image *src,
			 const struct meson8b_ge2d_image *dst,
			 const struct meson8b_ge2d_crop *crop)
{
	enum meson8b_ge2d_format src_fmt = meson8b_ge2d_resolved_format(src);
	enum meson8b_ge2d_format dst_fmt = meson8b_ge2d_resolved_format(dst);
	const struct ge2d_hw_fmt *s = ge2d_find_hw_fmt(src_fmt);
	const struct ge2d_hw_fmt *d = ge2d_find_hw_fmt(dst_fmt);
	u32 width = crop->width, height = crop->height;
	u32 out_width = dst->width, out_height = dst->height;
	u32 scale_w, scale_h;
	u32 x = crop->left + width - 1, y = crop->top + height - 1;
	u32 status, hstep, vstep, misc, phase, advance = 0;
	u32 src_cmap, dst_cmap, cb_canvas = 0, cr_canvas = 0, dst2_canvas = 0;
	u32 src2_canvas = 0, src2_ctrl = 0, src2_fill = 0;
	u32 x_start_ex, x_end_ex, y_start_ex, y_end_ex;
	bool preh, prev, swap, x_rev, y_rev;
	int ret;

	if (core->offline)
		return -EIO;
	if (!s || !d || meson8b_ge2d_format_is_planar3(dst_fmt) ||
	    dst_fmt == MESON8B_GE2D_FMT_UYVY ||
	    meson8b_ge2d_grey_mismatch(src_fmt, dst_fmt) ||
	    meson8b_ge2d_image_valid(src) || meson8b_ge2d_image_valid(dst) ||
	    meson8b_ge2d_crop_valid(src, crop) || meson8b_ge2d_images_overlap(src, dst))
		return -EINVAL;
	if (crop->blend != MESON8B_GE2D_BLEND_COPY &&
	    (!meson8b_ge2d_format_is_rgb(dst_fmt) || d->dst2_en))
		return -EINVAL;
	ge2d_resolve_dir(crop, &swap, &x_rev, &y_rev);
	scale_w = swap ? out_height : out_width;
	scale_h = swap ? out_width : out_height;
	if (meson8b_ge2d_phase(width, scale_w, &hstep, &preh) ||
	    meson8b_ge2d_phase(height, scale_h, &vstep, &prev))
		return -EINVAL;
	ret = ge2d_reset(core);
	if (ret)
		return ret;
	ret = meson_canvas_config(core->canvas, core->src_canvas, src->address[0],
				  meson8b_ge2d_plane_stride(src, 0),
				  meson8b_ge2d_plane_rows(src, 0),
				  MESON_CANVAS_WRAP_NONE, MESON_CANVAS_BLKMODE_LINEAR, 0);
	if (ret)
		return ret;
	ret = meson_canvas_config(core->canvas, core->dst_canvas, dst->address[0],
				  meson8b_ge2d_plane_stride(dst, 0),
				  meson8b_ge2d_plane_rows(dst, 0),
				  MESON_CANVAS_WRAP_NONE, MESON_CANVAS_BLKMODE_LINEAR, 0);
	if (ret)
		return ret;

	if (meson8b_ge2d_format_is_420(src_fmt)) {
		ret = meson_canvas_config(core->canvas, core->src_uv_canvas, src->address[1],
					  meson8b_ge2d_plane_stride(src, 1),
					  src->height / 2, MESON_CANVAS_WRAP_NONE,
					  MESON_CANVAS_BLKMODE_LINEAR, 0);
		if (ret)
			return ret;
		cb_canvas = core->src_uv_canvas;
		cr_canvas = core->src_uv_canvas;
		if (meson8b_ge2d_format_is_planar3(src_fmt)) {
			ret = meson_canvas_config(core->canvas, core->src_v_canvas,
						  src->address[2],
						  meson8b_ge2d_plane_stride(src, 2),
						  src->height / 2, MESON_CANVAS_WRAP_NONE,
						  MESON_CANVAS_BLKMODE_LINEAR, 0);
			if (ret)
				return ret;
			if (src_fmt == MESON8B_GE2D_FMT_YVU420)
				cb_canvas = core->src_v_canvas;
			else
				cr_canvas = core->src_v_canvas;
		}
	}
	if (d->dst2_en) {
		ret = meson_canvas_config(core->canvas, core->dst_uv_canvas, dst->address[1],
					  meson8b_ge2d_plane_stride(dst, 1),
					  dst->height / 2, MESON_CANVAS_WRAP_NONE,
					  MESON_CANVAS_BLKMODE_LINEAR, 0);
		if (ret)
			return ret;
		dst2_canvas = core->dst_uv_canvas;
	}
	src_cmap = src->little_endian ? s->cmap_le : s->cmap_be;
	dst_cmap = dst->little_endian ? d->cmap_le : d->cmap_be;
	if (crop->blend == MESON8B_GE2D_BLEND_OVER) {
		ret = meson_canvas_config(core->canvas, core->src2_canvas, dst->address[0],
					  meson8b_ge2d_plane_stride(dst, 0),
					  meson8b_ge2d_plane_rows(dst, 0),
					  MESON_CANVAS_WRAP_NONE, MESON_CANVAS_BLKMODE_LINEAR, 0);
		if (ret)
			return ret;
		src2_canvas = core->src2_canvas;
		src2_ctrl = (dst->little_endian ? BIT(15) : 0) |
			    (dst_cmap << 11) | ((u32)d->hw_fmt << 8);
	} else if (crop->blend == MESON8B_GE2D_BLEND_CONST) {
		ge2d_write(core, SRC2_DEF_COLOR, ((crop->bg_color & 0xffffffu) << 8) | 0xffu);
		src2_ctrl = BIT(15) | (2u << 11) | (3u << 8);
		src2_fill = BIT(9);
	}
	ge2d_write(core, GEN_CTRL0, (s->sep_en ? BIT(0) : 0) |
		   (s->y_yc_ratio ? BIT(10) : 0) | (s->x_yc_ratio ? BIT(11) : 0));
	ge2d_write(core, GEN_CTRL1, (3 << 22) | (0x3f << 16) | crop->global_alpha);
	ge2d_write(core, GEN_CTRL2, (dst->little_endian ? BIT(23) : 0) |
		   (dst_cmap << 19) | ((d->dst2_en ? 0u : (u32)d->hw_fmt) << 16) |
		   src2_ctrl | (src->little_endian ? BIT(7) : 0) | (src_cmap << 3) | s->hw_fmt);
	ge2d_write(core, GEN_CTRL3, BIT(0) |
		   (d->dst2_en ? ((dst_cmap - 5) << 19) | BIT(16) | (15 << 10) | BIT(8) : 0));
	ge2d_write(core, SRC_CANVAS, ((u32)core->src_canvas << 24) |
		   (cb_canvas << 16) | (cr_canvas << 8));
	ge2d_write(core, DST_CANVAS, core->dst_canvas | (src2_canvas << 8) | (dst2_canvas << 16));
	ge2d_write(core, SRC_CLIPX, (crop->left << 16) | x);
	ge2d_write(core, SRC_CLIPY, (crop->top << 16) | y);
	ge2d_write(core, DST_CLIPX, out_width - 1);
	ge2d_write(core, DST_CLIPY, out_height - 1);
	ge2d_write(core, SRC2_CLIPX, out_width - 1);
	ge2d_write(core, SRC2_CLIPY, out_height - 1);
	x_start_ex = s->x_yc_ratio ? (x_rev ? 3u : 2u) : 0;
	x_end_ex = s->x_yc_ratio ? (x_rev ? 2u : 3u) : 0;
	y_start_ex = s->y_yc_ratio ? (y_rev ? 3u : 2u) : 0;
	y_end_ex = s->y_yc_ratio ? (y_rev ? 2u : 3u) : 0;
	ge2d_write(core, SRC_X, (crop->left << 16) | x |
		   (x_start_ex << 30) | (x_end_ex << 14));
	/* Vendor command extends the read region; clipping retains the image. */
	ge2d_write(core, SRC_Y, (crop->top << 16) | (y_rev ? y : (y + 1)) |
		   (y_start_ex << 30) | (y_end_ex << 14));
	ge2d_write(core, SRC2_X, out_width - 1);
	ge2d_write(core, SRC2_Y, out_height - 1);
	ge2d_write(core, DST_X, out_width - 1);
	ge2d_write(core, DST_Y, out_height - 1);
	ge2d_write(core, SRC_FMT,
		   (s->x_yc_ratio ? (BIT(18) | (8u << 8)) : 0) |
		   (s->y_yc_ratio ? (BIT(16) | 0x4cu) : 0));
	ge2d_write(core, OUTSIDE_ALPHA, 0);
	ge2d_write_matrix(core, src_fmt, dst_fmt, src->colorspace, dst->colorspace);
	ge2d_write(core, ANTIFLICK, 0);
	ge2d_write(core, DP_ONOFF, 0);
	misc = (4 << 4) | 4;
	misc |= (preh ? BIT(14) : 0) | (prev ? BIT(13) : 0);
	misc |= (width != scale_w ? BIT(11) : 0) | (height != scale_h ? BIT(12) : 0);
	if (width != scale_w) {
		u32 length = (124u << 24) / hstep;

		/* Divide horizontal reads to fit the scaler's 128-pixel line buffer. */
		misc |= BIT(28) | (length << 15);
		advance = hstep * length;
	}
	if (width != scale_w || height != scale_h) {
		ge2d_write(core, SCALE_IDX, 0);
		for (phase = 0; phase < 33; phase++)
			ge2d_write(core, SCALE_COEF, meson8b_ge2d_bilinear(phase));
		ge2d_write(core, SCALE_IDX, 0x100);
		for (phase = 0; phase < 33; phase++)
			ge2d_write(core, SCALE_COEF, meson8b_ge2d_bilinear(phase));
	}
	ge2d_write(core, SC_MISC, misc);
	ge2d_write(core, HSC_STEP, hstep);
	ge2d_write(core, VSC_STEP, vstep);
	ge2d_write(core, HSC_SLOPE, 0);
	ge2d_write(core, VSC_SLOPE, 0);
	/* Vendor stretchblt repeats the first sample for the four-tap bank. */
	ge2d_write(core, HSC_INI, width != scale_w ? BIT(29) : 0);
	ge2d_write(core, VSC_INI, height != scale_h ? BIT(29) : 0);
	ge2d_write(core, HSC_ADV, advance);
	ge2d_write_alu(core, src_fmt, crop);
	dma_wmb();
	ge2d_write(core, CMD_CTRL, 1u | src2_fill | (swap ? BIT(7) : 0) |
		   (x_rev ? BIT(6) : 0) | (y_rev ? BIT(5) : 0) |
		   (crop->blend == MESON8B_GE2D_BLEND_OVER ?
		    ((x_rev ? BIT(4) : 0) | (y_rev ? BIT(3) : 0)) : 0));
	readl(core->regs + CMD_CTRL);
	ret = readl_poll_timeout(core->regs + STATUS0, status,
				 !(status & BUSY_MASK), 50, 100000);
	if (ret) {
		/* Hold the engine in reset before vb2 can recycle DMA buffers. */
		reset_control_assert(core->reset);
		readl(core->regs + GEN_CTRL1);
		core->offline = true;
		return -ETIMEDOUT;
	}
	dma_rmb();
	return 0;
}

/* Bypass chroma interpolation when only copying or cropping identical NV12/NV21 bytes. */
static int ge2d_blit(struct meson8b_ge2d *core,
		     const struct meson8b_ge2d_image *src,
		     const struct meson8b_ge2d_image *dst,
		     const struct meson8b_ge2d_crop *crop)
{
	enum meson8b_ge2d_format src_fmt = meson8b_ge2d_resolved_format(src);
	enum meson8b_ge2d_format dst_fmt = meson8b_ge2d_resolved_format(dst);
	u32 plane;

	if (!meson8b_ge2d_format_is_semiplanar(src_fmt) || src_fmt != dst_fmt ||
	    crop->blend != MESON8B_GE2D_BLEND_COPY ||
	    crop->rotate || crop->hflip || crop->vflip ||
	    crop->width != dst->width || crop->height != dst->height) {
		if (crop->blend == MESON8B_GE2D_BLEND_OVER &&
		    (crop->rotate == 90 || crop->rotate == 270)) {
			const struct ge2d_hw_fmt *s = ge2d_find_hw_fmt(src_fmt);
			const struct ge2d_hw_fmt *d = ge2d_find_hw_fmt(dst_fmt);
			struct meson8b_ge2d_image stage;
			struct meson8b_ge2d_crop pass;
			size_t need;
			int ret;

			if (!s || !d || !meson8b_ge2d_format_is_rgb(dst_fmt) ||
			    d->dst2_en || meson8b_ge2d_image_valid(src) ||
			    meson8b_ge2d_image_valid(dst) ||
			    meson8b_ge2d_crop_valid(src, crop) ||
			    meson8b_ge2d_images_overlap(src, dst))
				return -EINVAL;
			stage = (struct meson8b_ge2d_image){
				.width = dst->width,
				.height = dst->height,
				.stride = ALIGN(dst->width * 4u, 32),
				.little_endian = true,
				.format = MESON8B_GE2D_FMT_ARGB32,
				.colorspace = dst->colorspace,
			};
			need = PAGE_ALIGN((size_t)stage.stride * stage.height);
			if (core->blend_stage_size < need) {
				if (core->blend_stage_vaddr) {
					dma_free_coherent(core->dev, core->blend_stage_size,
							  core->blend_stage_vaddr,
							  core->blend_stage_dma);
					core->blend_stage_vaddr = NULL;
					core->blend_stage_size = 0;
				}
				core->blend_stage_vaddr =
					dma_alloc_coherent(core->dev, need,
							   &core->blend_stage_dma,
							   GFP_KERNEL);
				if (!core->blend_stage_vaddr)
					return -ENOMEM;
				core->blend_stage_size = need;
			}
			stage.address[0] = core->blend_stage_dma;
			pass = *crop;
			pass.blend = MESON8B_GE2D_BLEND_COPY;
			pass.global_alpha = 255;
			pass.bg_color = 0;
			/* SRC2 lacks xy_swap in CMD_CTRL: rotate into stage before OVER blend. */
			ret = ge2d_blit_one(core, src, &stage, &pass);
			if (ret)
				return ret;
			pass = (struct meson8b_ge2d_crop){
				.width = dst->width,
				.height = dst->height,
				.blend = MESON8B_GE2D_BLEND_OVER,
				.global_alpha = crop->global_alpha,
				.bg_color = crop->bg_color,
			};
			return ge2d_blit_one(core, &stage, dst, &pass);
		}
		return ge2d_blit_one(core, src, dst, crop);
	}
	if (meson8b_ge2d_image_valid(src) || meson8b_ge2d_image_valid(dst) ||
	    meson8b_ge2d_crop_valid(src, crop) || meson8b_ge2d_images_overlap(src, dst))
		return -EINVAL;
	for (plane = 0; plane < 2; plane++) {
		struct meson8b_ge2d_image input = *src, output = *dst;
		struct meson8b_ge2d_crop region = *crop;
		int ret;

		input.nv12 = false;
		output.nv12 = false;
		input.format = MESON8B_GE2D_FMT_GREY;
		output.format = MESON8B_GE2D_FMT_GREY;
		input.address[0] = src->address[plane];
		output.address[0] = dst->address[plane];
		if (plane) {
			input.height /= 2;
			output.height /= 2;
			region.top /= 2;
			region.height /= 2;
		}
		ret = ge2d_blit_one(core, &input, &output, &region);
		if (ret)
			return ret;
	}
	return 0;
}

static int ge2d_copy(struct meson8b_ge2d *core, dma_addr_t src, dma_addr_t dst,
		     u32 width, u32 height, bool src_little_endian)
{
	struct meson8b_ge2d_image input = {
		.width = width, .height = height, .stride = ALIGN(width, 32),
		.address = { src }, .little_endian = src_little_endian,
	};
	struct meson8b_ge2d_image output = input;
	struct meson8b_ge2d_crop crop = { .width = width, .height = height };

	output.address[0] = dst;
	output.little_endian = true;
	return ge2d_blit(core, &input, &output, &crop);
}

static int ge2d_selftest(struct meson8b_ge2d *core)
{
	const u32 luma = 512 * 32, size = luma * 3 / 2;
	dma_addr_t src_dma, dst_dma;
	u8 *src, *dst;
	u32 i;
	int ret = -ENOMEM;

	src = dma_alloc_coherent(core->dev, size, &src_dma, GFP_KERNEL);
	if (!src)
		return ret;
	dst = dma_alloc_coherent(core->dev, size, &dst_dma, GFP_KERNEL);
	if (!dst)
		goto free_src;
	for (i = 0; i < luma; i++)
		src[i ^ 7] = (i * 37 + i / 512 * 13) & 0xff;
	memset(dst, 0xa5, size);
	ret = ge2d_copy(core, src_dma, dst_dma, 512, 32, false);
	if (!ret)
		for (i = 0; i < luma; i++)
			if (dst[i] != ((i * 37 + i / 512 * 13) & 0xff)) {
				ret = -EIO;
				break;
			}
	if (!ret) {
		struct meson8b_ge2d_image input = {
			.width = 512, .height = 32, .stride = 512,
			.address = { src_dma }, .little_endian = true,
		};
		struct meson8b_ge2d_image output = {
			.width = 256, .height = 16, .stride = 256,
			.address = { dst_dma }, .little_endian = true,
		};
		struct meson8b_ge2d_crop crop = { .width = 512, .height = 32 };

		/* A constant plane must survive horizontal and vertical filtering. */
		memset(src, 0x39, size);
		memset(dst, 0xa5, size);
		ret = ge2d_blit(core, &input, &output, &crop);
		if (!ret)
			for (i = 0; i < 256 * 16; i++)
				if (dst[i] != 0x39) {
					ret = -EIO;
					break;
				}
	}
	if (!ret) {
		struct meson8b_ge2d_image input = {
			.width = 512, .height = 32, .stride = 512,
			.address = { src_dma, src_dma + luma }, .nv12 = true,
		};
		struct meson8b_ge2d_image output = input;
		struct meson8b_ge2d_crop crop = { .width = 512, .height = 32 };

		for (i = 0; i < size; i++) {
			u8 value = i < luma ? (i * 37 + i / 512 * 13) & 255 :
				   (i * 37 + i / 512 * 13 + (i & 1 ? 0xb7 : 0x35)) & 255;

			src[i ^ 7] = value;
		}
		output.address[0] = dst_dma;
		output.address[1] = dst_dma + luma;
		output.little_endian = true;
		memset(dst, 0xa5, size);
		ret = ge2d_blit(core, &input, &output, &crop);
		if (!ret)
			for (i = 0; i < size; i++)
				if (dst[i] != src[i ^ 7]) {
					ret = -EIO;
					break;
				}
		if (!ret) {
			memset(src, 0x39, luma);
			for (i = luma; i < size; i++)
				src[i] = i & 1 ? 0xb7 : 0x35;
			input.little_endian = true;
			output.width = 256;
			output.height = 16;
			output.stride = 256;
			output.address[1] = dst_dma + 256 * 16;
			memset(dst, 0xa5, size);
			ret = ge2d_blit(core, &input, &output, &crop);
			if (!ret)
				for (i = 0; i < 256 * 16 * 3 / 2; i++) {
					u8 value = i < 256 * 16 ? 0x39 : (i & 1 ? 0xb7 : 0x35);

					if (dst[i] != value) {
						ret = -EIO;
						break;
					}
				}
		}
	}
	dma_free_coherent(core->dev, size, dst, dst_dma);
free_src:
	dma_free_coherent(core->dev, size, src, src_dma);
	return ret;
}

static bool ge2d_is_nv12(u32 format)
{
	return format == V4L2_PIX_FMT_NV12 || format == V4L2_PIX_FMT_NV12M;
}

struct ge2d_v4l2_fmt {
	u32 fourcc;
	enum meson8b_ge2d_format fmt;
	u8 planes;
	bool cap_supported;
};

static const struct ge2d_v4l2_fmt ge2d_v4l2_formats[] = {
	{ V4L2_PIX_FMT_GREY, MESON8B_GE2D_FMT_GREY, 1, true },
	{ V4L2_PIX_FMT_NV12, MESON8B_GE2D_FMT_NV12, 1, true },
	{ V4L2_PIX_FMT_NV12M, MESON8B_GE2D_FMT_NV12, 2, true },
	{ V4L2_PIX_FMT_NV21, MESON8B_GE2D_FMT_NV21, 1, true },
	{ V4L2_PIX_FMT_NV21M, MESON8B_GE2D_FMT_NV21, 2, true },
	{ V4L2_PIX_FMT_YUV420, MESON8B_GE2D_FMT_YUV420, 1, false },
	{ V4L2_PIX_FMT_YUV420M, MESON8B_GE2D_FMT_YUV420, 3, false },
	{ V4L2_PIX_FMT_YVU420, MESON8B_GE2D_FMT_YVU420, 1, false },
	{ V4L2_PIX_FMT_YVU420M, MESON8B_GE2D_FMT_YVU420, 3, false },
	{ V4L2_PIX_FMT_UYVY, MESON8B_GE2D_FMT_UYVY, 1, false },
	{ V4L2_PIX_FMT_RGB24, MESON8B_GE2D_FMT_RGB24, 1, true },
	{ V4L2_PIX_FMT_BGR24, MESON8B_GE2D_FMT_BGR24, 1, true },
	{ V4L2_PIX_FMT_XBGR32, MESON8B_GE2D_FMT_XBGR32, 1, true },
	{ V4L2_PIX_FMT_RGBX32, MESON8B_GE2D_FMT_RGBX32, 1, true },
	{ V4L2_PIX_FMT_XRGB32, MESON8B_GE2D_FMT_XRGB32, 1, true },
	{ V4L2_PIX_FMT_ABGR32, MESON8B_GE2D_FMT_ABGR32, 1, true },
	{ V4L2_PIX_FMT_RGBA32, MESON8B_GE2D_FMT_RGBA32, 1, true },
	{ V4L2_PIX_FMT_ARGB32, MESON8B_GE2D_FMT_ARGB32, 1, true },
};

static const struct ge2d_v4l2_fmt *ge2d_find_v4l2_fmt(u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ge2d_v4l2_formats); i++)
		if (ge2d_v4l2_formats[i].fourcc == fourcc)
			return &ge2d_v4l2_formats[i];
	return NULL;
}

static enum meson8b_ge2d_colorspace
ge2d_image_colorspace(const struct v4l2_pix_format_mplane *format)
{
	bool bt709 = format->ycbcr_enc == V4L2_YCBCR_ENC_709 ||
		     (format->ycbcr_enc == V4L2_YCBCR_ENC_DEFAULT &&
		      format->colorspace == V4L2_COLORSPACE_REC709);
	bool full = format->quantization == V4L2_QUANTIZATION_FULL_RANGE;

	if (bt709)
		return full ? MESON8B_GE2D_CS_BT709_FULL : MESON8B_GE2D_CS_BT709_LIM;
	return full ? MESON8B_GE2D_CS_BT601_FULL : MESON8B_GE2D_CS_BT601_LIM;
}

static struct meson8b_ge2d_image
ge2d_buffer_image(struct vb2_buffer *vb, const struct v4l2_pix_format_mplane *format)
{
	const struct ge2d_v4l2_fmt *f = ge2d_find_v4l2_fmt(format->pixelformat);
	enum meson8b_ge2d_format fmt = f ? f->fmt : MESON8B_GE2D_FMT_GREY;
	u32 y_size = format->plane_fmt[0].bytesperline * format->height;
	struct meson8b_ge2d_image image = {
		.width = format->width, .height = format->height,
		.stride = format->plane_fmt[0].bytesperline,
		.address = { vb2_dma_contig_plane_dma_addr(vb, 0) },
		.nv12 = ge2d_is_nv12(format->pixelformat), .little_endian = true,
		.format = fmt, .colorspace = ge2d_image_colorspace(format),
	};

	if (format->num_planes == 1 &&
	    ((u64)image.address[0] + format->plane_fmt[0].sizeimage > (u64)U32_MAX + 1)) {
		image.width = 0;
		return image;
	}
	if (meson8b_ge2d_format_is_semiplanar(fmt)) {
		image.plane_stride[0] = image.stride;
		image.plane_stride[1] = format->num_planes == 2 ?
			format->plane_fmt[1].bytesperline : image.stride;
		image.address[1] = format->num_planes == 2 ?
			vb2_dma_contig_plane_dma_addr(vb, 1) :
			image.address[0] + y_size;
	} else if (meson8b_ge2d_format_is_planar3(fmt)) {
		u32 uv_stride = format->num_planes == 3 ?
			format->plane_fmt[1].bytesperline : image.stride / 2;

		image.plane_stride[0] = image.stride;
		image.plane_stride[1] = uv_stride;
		image.plane_stride[2] = format->num_planes == 3 ?
			format->plane_fmt[2].bytesperline : uv_stride;
		if (format->num_planes == 3) {
			image.address[1] = vb2_dma_contig_plane_dma_addr(vb, 1);
			image.address[2] = vb2_dma_contig_plane_dma_addr(vb, 2);
		} else {
			image.address[1] = image.address[0] + y_size;
			image.address[2] = image.address[1] + uv_stride * (image.height / 2);
		}
	}
	return image;
}

/* Read inputs after their writer; overwrite outputs only after all users. */
static int ge2d_wait_buffer(struct vb2_buffer *buffer)
{
	bool write = V4L2_TYPE_IS_CAPTURE(buffer->type);
	unsigned int i;

	if (buffer->memory != V4L2_MEMORY_DMABUF)
		return 0;
	for (i = 0; i < buffer->num_planes; i++) {
		long ret = dma_resv_wait_timeout(buffer->planes[i].dbuf->resv, write,
						true, msecs_to_jiffies(1000));

		if (ret <= 0)
			return ret ? ret : -ETIMEDOUT;
	}
	return 0;
}

static void ge2d_work(struct work_struct *work)
{
	struct meson8b_ge2d *core = container_of(work, struct meson8b_ge2d, work);
	struct ge2d_ctx *ctx = core->active;
	struct vb2_v4l2_buffer *src = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	struct vb2_v4l2_buffer *dst = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	int ret = -EINVAL;

	if (src && dst) {
		struct meson8b_ge2d_image input = ge2d_buffer_image(&src->vb2_buf, &ctx->input);
		struct meson8b_ge2d_image output = ge2d_buffer_image(&dst->vb2_buf, &ctx->output);
		unsigned int i;

		ret = ge2d_wait_buffer(&src->vb2_buf);
		if (!ret)
			ret = ge2d_wait_buffer(&dst->vb2_buf);
		if (!ret) {
			mutex_lock(&core->hardware_lock);
			ret = core->ready ? ge2d_blit(core, &input, &output, &ctx->crop) : -ENODEV;
			mutex_unlock(&core->hardware_lock);
		}
		v4l2_m2m_buf_copy_metadata(src, dst, true);
		dst->sequence = ctx->sequence++;
		for (i = 0; i < ctx->output.num_planes; i++)
			vb2_set_plane_payload(&dst->vb2_buf, i,
					      ret ? 0 : ctx->output.plane_fmt[i].sizeimage);
	}
	if (src)
		v4l2_m2m_buf_done(src, ret ? VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);
	if (dst)
		v4l2_m2m_buf_done(dst, ret ? VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);
	core->active = NULL;
	v4l2_m2m_job_finish(core->m2m, ctx->fh.m2m_ctx);
}

static void ge2d_run(void *priv)
{
	struct ge2d_ctx *ctx = priv;

	ctx->core->active = ctx;
	schedule_work(&ctx->core->work);
}

static const struct v4l2_m2m_ops ge2d_m2m_ops = {
	.device_run = ge2d_run,
};

static int ge2d_queue_setup(struct vb2_queue *queue, unsigned int *buffers,
			    unsigned int *planes, unsigned int sizes[],
			    struct device *alloc_devs[])
{
	struct ge2d_ctx *ctx = vb2_get_drv_priv(queue);
	const struct v4l2_pix_format_mplane *format = V4L2_TYPE_IS_OUTPUT(queue->type) ?
		&ctx->input : &ctx->output;
	unsigned int i;

	if (*planes) {
		if (*planes != format->num_planes)
			return -EINVAL;
		for (i = 0; i < *planes; i++)
			if (sizes[i] < format->plane_fmt[i].sizeimage)
				return -EINVAL;
		return 0;
	}
	*planes = format->num_planes;
	for (i = 0; i < *planes; i++)
		sizes[i] = format->plane_fmt[i].sizeimage;
	return 0;
}

static int ge2d_prepare(struct vb2_buffer *buffer)
{
	struct ge2d_ctx *ctx = vb2_get_drv_priv(buffer->vb2_queue);
	const struct v4l2_pix_format_mplane *format = V4L2_TYPE_IS_OUTPUT(buffer->type) ?
		&ctx->input : &ctx->output;
	struct meson8b_ge2d_image image;
	unsigned int i;

	if (buffer->num_planes != format->num_planes)
		return -EINVAL;
	for (i = 0; i < format->num_planes; i++) {
		u32 size = format->plane_fmt[i].sizeimage;

		if (buffer->planes[i].data_offset || vb2_plane_size(buffer, i) < size ||
		    (V4L2_TYPE_IS_OUTPUT(buffer->type) && vb2_get_plane_payload(buffer, i) < size))
			return -EINVAL;
	}
	image = ge2d_buffer_image(buffer, format);
	return meson8b_ge2d_image_valid(&image);
}

static void ge2d_queue(struct vb2_buffer *buffer)
{
	struct ge2d_ctx *ctx = vb2_get_drv_priv(buffer->vb2_queue);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, to_vb2_v4l2_buffer(buffer));
}

static int ge2d_start(struct vb2_queue *queue, unsigned int count)
{
	struct ge2d_ctx *ctx = vb2_get_drv_priv(queue);
	const struct ge2d_v4l2_fmt *out = ge2d_find_v4l2_fmt(ctx->output.pixelformat);
	u32 step, scale_w, scale_h;
	bool pre;

	if (ctx->crop.blend != MESON8B_GE2D_BLEND_COPY &&
	    (!out || !meson8b_ge2d_format_is_rgb(out->fmt)))
		return -EINVAL;
	meson8b_ge2d_target_size(ctx->crop.rotate, ctx->output.width,
				 ctx->output.height, &scale_w, &scale_h);
	if (meson8b_ge2d_phase(ctx->crop.width, scale_w, &step, &pre) ||
	    meson8b_ge2d_phase(ctx->crop.height, scale_h, &step, &pre))
		return -EINVAL;
	if (V4L2_TYPE_IS_CAPTURE(queue->type))
		ctx->sequence = 0;
	return 0;
}

static void ge2d_stop(struct vb2_queue *queue)
{
	struct ge2d_ctx *ctx = vb2_get_drv_priv(queue);
	struct vb2_v4l2_buffer *buffer;

	/* Finish a running DMA before vb2 can release its source or destination. */
	flush_work(&ctx->core->work);
	for (;;) {
		buffer = V4L2_TYPE_IS_OUTPUT(queue->type) ?
			v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx) :
			v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
		if (!buffer)
			break;
		v4l2_m2m_buf_done(buffer, VB2_BUF_STATE_ERROR);
	}
}

static const struct vb2_ops ge2d_queue_ops = {
	.queue_setup = ge2d_queue_setup,
	.buf_prepare = ge2d_prepare,
	.buf_queue = ge2d_queue,
	.start_streaming = ge2d_start,
	.stop_streaming = ge2d_stop,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
};

static int ge2d_queue_init(void *priv, struct vb2_queue *src, struct vb2_queue *dst)
{
	struct ge2d_ctx *ctx = priv;
	struct vb2_queue *queues[] = { src, dst };
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(queues); i++) {
		struct vb2_queue *queue = queues[i];

		queue->type = i ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE :
				 V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		queue->io_modes = VB2_MMAP | VB2_DMABUF;
		queue->drv_priv = ctx;
		queue->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
		queue->ops = &ge2d_queue_ops;
		queue->mem_ops = &vb2_dma_contig_memops;
		queue->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
		queue->lock = &ctx->core->lock;
		queue->dev = ctx->core->dev;
		ret = vb2_queue_init(queue);
		if (ret)
			return ret;
	}
	return 0;
}

static int ge2d_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
	strscpy(cap->driver, "meson8b-ge2d", sizeof(cap->driver));
	strscpy(cap->card, "S805 GE2D image converter", sizeof(cap->card));
	strscpy(cap->bus_info, "platform:meson8b-ge2d", sizeof(cap->bus_info));
	return 0;
}

static int ge2d_enum_fmt(struct file *file, void *priv, struct v4l2_fmtdesc *desc)
{
	bool capture = V4L2_TYPE_IS_CAPTURE(desc->type);
	u32 i, match = 0;

	for (i = 0; i < ARRAY_SIZE(ge2d_v4l2_formats); i++) {
		if (capture && !ge2d_v4l2_formats[i].cap_supported)
			continue;
		if (match++ == desc->index) {
			desc->pixelformat = ge2d_v4l2_formats[i].fourcc;
			return 0;
		}
	}
	return -EINVAL;
}

static int ge2d_try_fmt(struct file *file, void *priv, struct v4l2_format *format)
{
	struct v4l2_pix_format_mplane *pix = &format->fmt.pix_mp;
	bool capture = V4L2_TYPE_IS_CAPTURE(format->type);
	const struct ge2d_v4l2_fmt *entry = ge2d_find_v4l2_fmt(pix->pixelformat);
	u32 width, height, stride, bpp, align, y_size;
	u32 requested_stride = pix->plane_fmt[0].bytesperline;
	u32 colorspace = pix->colorspace, encoding = pix->ycbcr_enc;
	u32 quantization = pix->quantization, transfer = pix->xfer_func;

	if (!entry || (capture && !entry->cap_supported))
		entry = &ge2d_v4l2_formats[0];
	width = clamp_t(u32, pix->width, 32, MESON8B_GE2D_MAX_WIDTH);
	height = clamp_t(u32, pix->height, 1, MESON8B_GE2D_MAX_HEIGHT);
	if (entry->fmt == MESON8B_GE2D_FMT_GREY) {
		width = ALIGN(width, 32);
	} else if (meson8b_ge2d_format_is_420(entry->fmt)) {
		width = ALIGN(width, 2);
		height = ALIGN(height, 2);
	} else if (entry->fmt == MESON8B_GE2D_FMT_UYVY) {
		width = ALIGN(width, 2);
	}
	bpp = meson8b_ge2d_format_bpp(entry->fmt);
	align = meson8b_ge2d_format_is_planar3(entry->fmt) ? 64u : 32u;
	stride = ALIGN(width * bpp, align);
	stride = ALIGN(clamp_t(u32, requested_stride, stride, 8192), align);
	y_size = stride * height;
	memset(pix, 0, sizeof(*pix));
	pix->width = width;
	pix->height = height;
	pix->pixelformat = entry->fourcc;
	pix->field = V4L2_FIELD_NONE;
	pix->num_planes = entry->planes;
	pix->colorspace = meson8b_ge2d_format_is_yuv(entry->fmt) ||
			  meson8b_ge2d_format_is_rgb(entry->fmt) ?
			  colorspace : V4L2_COLORSPACE_RAW;
	pix->ycbcr_enc = encoding;
	pix->quantization = quantization;
	pix->xfer_func = transfer;
	pix->plane_fmt[0].bytesperline = stride;
	pix->plane_fmt[0].sizeimage = y_size;
	if (entry->planes == 1 && meson8b_ge2d_format_is_420(entry->fmt)) {
		pix->plane_fmt[0].sizeimage = y_size + y_size / 2;
	} else if (entry->planes == 2) {
		pix->plane_fmt[1].bytesperline = stride;
		pix->plane_fmt[1].sizeimage = y_size / 2;
	} else if (entry->planes == 3) {
		pix->plane_fmt[1].bytesperline = stride / 2;
		pix->plane_fmt[1].sizeimage = y_size / 4;
		pix->plane_fmt[2].bytesperline = stride / 2;
		pix->plane_fmt[2].sizeimage = y_size / 4;
	}
	if (capture && priv) {
		struct ge2d_ctx *ctx = container_of(priv, struct ge2d_ctx, fh);

		const struct ge2d_v4l2_fmt *in = ge2d_find_v4l2_fmt(ctx->input.pixelformat);
		bool cross = in && meson8b_ge2d_format_is_yuv(in->fmt) !=
				   meson8b_ge2d_format_is_yuv(entry->fmt);

		if (cross) {
			pix->colorspace = colorspace ? : V4L2_COLORSPACE_REC709;
			pix->ycbcr_enc = encoding;
			pix->quantization = quantization;
			pix->xfer_func = transfer;
		} else {
			pix->colorspace = ctx->input.colorspace;
			pix->ycbcr_enc = ctx->input.ycbcr_enc;
			pix->quantization = ctx->input.quantization;
			pix->xfer_func = ctx->input.xfer_func;
		}
	}
	return 0;
}

static int ge2d_get_fmt(struct file *file, void *priv, struct v4l2_format *format)
{
	struct ge2d_ctx *ctx = container_of(priv, struct ge2d_ctx, fh);

	format->fmt.pix_mp = V4L2_TYPE_IS_OUTPUT(format->type) ? ctx->input : ctx->output;
	return 0;
}

static int ge2d_set_fmt(struct file *file, void *priv, struct v4l2_format *format)
{
	struct ge2d_ctx *ctx = container_of(priv, struct ge2d_ctx, fh);

	if (vb2_is_busy(v4l2_m2m_get_src_vq(ctx->fh.m2m_ctx)) ||
	    vb2_is_busy(v4l2_m2m_get_dst_vq(ctx->fh.m2m_ctx)))
		return -EBUSY;
	ge2d_try_fmt(file, priv, format);
	if (V4L2_TYPE_IS_OUTPUT(format->type)) {
		ctx->input = format->fmt.pix_mp;
		if (!ge2d_find_v4l2_fmt(ctx->input.pixelformat)->cap_supported) {
			struct v4l2_format cap = {
				.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
				.fmt.pix_mp = {
					.width = ctx->input.width,
					.height = ctx->input.height,
					.pixelformat = V4L2_PIX_FMT_NV12M,
					.colorspace = ctx->input.colorspace,
					.ycbcr_enc = ctx->input.ycbcr_enc,
					.quantization = ctx->input.quantization,
					.xfer_func = ctx->input.xfer_func,
				},
			};

			ge2d_try_fmt(file, priv, &cap);
			ctx->output = cap.fmt.pix_mp;
		} else {
			ctx->output = ctx->input;
		}
		ctx->crop.left = 0;
		ctx->crop.top = 0;
		ctx->crop.width = ctx->input.width;
		ctx->crop.height = ctx->input.height;
	} else {
		const struct ge2d_v4l2_fmt *in = ge2d_find_v4l2_fmt(ctx->input.pixelformat);
		const struct ge2d_v4l2_fmt *out =
			ge2d_find_v4l2_fmt(format->fmt.pix_mp.pixelformat);
		u32 step, scale_w, scale_h;
		bool pre;

		meson8b_ge2d_target_size(ctx->crop.rotate, format->fmt.pix_mp.width,
					 format->fmt.pix_mp.height, &scale_w, &scale_h);
		if (!in || !out || !out->cap_supported ||
		    meson8b_ge2d_grey_mismatch(in->fmt, out->fmt) ||
		    meson8b_ge2d_phase(ctx->crop.width, scale_w, &step, &pre) ||
		    meson8b_ge2d_phase(ctx->crop.height, scale_h, &step, &pre))
			return -EINVAL;
		ctx->output = format->fmt.pix_mp;
	}
	return 0;
}

static int ge2d_get_selection(struct file *file, void *priv, struct v4l2_selection *selection)
{
	struct ge2d_ctx *ctx = container_of(priv, struct ge2d_ctx, fh);

	if (selection->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;
	if (selection->target == V4L2_SEL_TGT_CROP) {
		selection->r = (struct v4l2_rect) {
			.left = ctx->crop.left, .top = ctx->crop.top,
			.width = ctx->crop.width, .height = ctx->crop.height,
		};
	} else if (selection->target == V4L2_SEL_TGT_CROP_DEFAULT ||
		   selection->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		selection->r = (struct v4l2_rect) {
			.width = ctx->input.width, .height = ctx->input.height,
		};
	} else {
		return -EINVAL;
	}
	return 0;
}

static int ge2d_set_selection(struct file *file, void *priv, struct v4l2_selection *selection)
{
	struct ge2d_ctx *ctx = container_of(priv, struct ge2d_ctx, fh);
	const struct ge2d_v4l2_fmt *in = ge2d_find_v4l2_fmt(ctx->input.pixelformat);
	struct meson8b_ge2d_image input = {
		.width = ctx->input.width, .height = ctx->input.height,
		.format = in ? in->fmt : MESON8B_GE2D_FMT_GREY,
	};
	struct meson8b_ge2d_crop crop = ctx->crop;
	u32 step, scale_w, scale_h;
	bool pre;

	crop.left = selection->r.left;
	crop.top = selection->r.top;
	crop.width = selection->r.width;
	crop.height = selection->r.height;

	meson8b_ge2d_target_size(ctx->crop.rotate, ctx->output.width,
				 ctx->output.height, &scale_w, &scale_h);
	if (selection->type != V4L2_BUF_TYPE_VIDEO_OUTPUT ||
	    selection->target != V4L2_SEL_TGT_CROP ||
	    (selection->flags & ~(V4L2_SEL_FLAG_GE | V4L2_SEL_FLAG_LE)) ||
	    meson8b_ge2d_crop_valid(&input, &crop) ||
	    meson8b_ge2d_phase(crop.width, scale_w, &step, &pre) ||
	    meson8b_ge2d_phase(crop.height, scale_h, &step, &pre))
		return -EINVAL;
	if (vb2_is_busy(v4l2_m2m_get_src_vq(ctx->fh.m2m_ctx)) ||
	    vb2_is_busy(v4l2_m2m_get_dst_vq(ctx->fh.m2m_ctx)))
		return -EBUSY;
	ctx->crop = crop;
	return 0;
}

static const struct v4l2_ioctl_ops ge2d_ioctl_ops = {
	.vidioc_querycap = ge2d_querycap,
	.vidioc_enum_fmt_vid_cap = ge2d_enum_fmt,
	.vidioc_enum_fmt_vid_out = ge2d_enum_fmt,
	.vidioc_g_fmt_vid_cap_mplane = ge2d_get_fmt,
	.vidioc_g_fmt_vid_out_mplane = ge2d_get_fmt,
	.vidioc_try_fmt_vid_cap_mplane = ge2d_try_fmt,
	.vidioc_try_fmt_vid_out_mplane = ge2d_try_fmt,
	.vidioc_s_fmt_vid_cap_mplane = ge2d_set_fmt,
	.vidioc_s_fmt_vid_out_mplane = ge2d_set_fmt,
	.vidioc_g_selection = ge2d_get_selection,
	.vidioc_s_selection = ge2d_set_selection,
	.vidioc_reqbufs = v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf = v4l2_m2m_ioctl_querybuf,
	.vidioc_prepare_buf = v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs = v4l2_m2m_ioctl_create_bufs,
	.vidioc_qbuf = v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf = v4l2_m2m_ioctl_dqbuf,
	.vidioc_expbuf = v4l2_m2m_ioctl_expbuf,
	.vidioc_streamon = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff = v4l2_m2m_ioctl_streamoff,
};

static int ge2d_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ge2d_ctx *ctx =
		container_of(ctrl->handler, struct ge2d_ctx, ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		ctx->crop.hflip = ctrl->val;
		return 0;
	case V4L2_CID_VFLIP:
		ctx->crop.vflip = ctrl->val;
		return 0;
	case V4L2_CID_ROTATE:
		if (vb2_is_busy(v4l2_m2m_get_src_vq(ctx->fh.m2m_ctx)) ||
		    vb2_is_busy(v4l2_m2m_get_dst_vq(ctx->fh.m2m_ctx)))
			return -EBUSY;
		ctx->crop.rotate = ctrl->val;
		return 0;
	case V4L2_CID_ALPHA_COMPONENT:
		ctx->crop.global_alpha = ctrl->val;
		return 0;
	case V4L2_CID_BG_COLOR:
		ctx->crop.bg_color = ctrl->val;
		return 0;
	case V4L2_CID_MESON8B_GE2D_BLEND:
		ctx->crop.blend = ctrl->val;
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct v4l2_ctrl_ops ge2d_ctrl_ops = {
	.s_ctrl = ge2d_s_ctrl,
};

static const struct v4l2_ctrl_config ge2d_blend_ctrl = {
	.ops = &ge2d_ctrl_ops,
	.id = V4L2_CID_MESON8B_GE2D_BLEND,
	.name = "Alpha Blend Mode",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = MESON8B_GE2D_BLEND_COPY,
	.max = MESON8B_GE2D_BLEND_OVER,
	.step = 1,
	.def = MESON8B_GE2D_BLEND_COPY,
};

static int ge2d_open(struct file *file)
{
	struct meson8b_ge2d *core = video_drvdata(file);
	struct ge2d_ctx *ctx;
	struct v4l2_format format = { .fmt.pix_mp = { .width = 512, .height = 32 } };
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx->core = core;
	ge2d_try_fmt(file, NULL, &format);
	ctx->input = format.fmt.pix_mp;
	ctx->output = ctx->input;
	ctx->crop = (struct meson8b_ge2d_crop) {
		.width = ctx->input.width, .height = ctx->input.height,
		.global_alpha = 255,
	};
	ret = v4l2_ctrl_handler_init(&ctx->ctrl_handler, 6);
	if (ret) {
		kfree(ctx);
		return ret;
	}
	v4l2_ctrl_new_std(&ctx->ctrl_handler, &ge2d_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&ctx->ctrl_handler, &ge2d_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&ctx->ctrl_handler, &ge2d_ctrl_ops,
			  V4L2_CID_ROTATE, 0, 270, 90, 0);
	v4l2_ctrl_new_std(&ctx->ctrl_handler, &ge2d_ctrl_ops,
			  V4L2_CID_ALPHA_COMPONENT, 0, 255, 1, 255);
	v4l2_ctrl_new_std(&ctx->ctrl_handler, &ge2d_ctrl_ops,
			  V4L2_CID_BG_COLOR, 0, 0xffffff, 1, 0);
	v4l2_ctrl_new_custom(&ctx->ctrl_handler, &ge2d_blend_ctrl, NULL);
	if (ctx->ctrl_handler.error) {
		ret = ctx->ctrl_handler.error;
		v4l2_ctrl_handler_free(&ctx->ctrl_handler);
		kfree(ctx);
		return ret;
	}
	v4l2_fh_init(&ctx->fh, core->video);
	ctx->fh.ctrl_handler = &ctx->ctrl_handler;
	mutex_lock(&core->lock);
	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(core->m2m, ctx, ge2d_queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		mutex_unlock(&core->lock);
		v4l2_fh_exit(&ctx->fh);
		v4l2_ctrl_handler_free(&ctx->ctrl_handler);
		kfree(ctx);
		return ret;
	}
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);
	mutex_unlock(&core->lock);
	return 0;
}

static int ge2d_release(struct file *file)
{
	struct ge2d_ctx *ctx = container_of(file->private_data, struct ge2d_ctx, fh);

	mutex_lock(&ctx->core->lock);
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	mutex_unlock(&ctx->core->lock);
	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	kfree(ctx);
	return 0;
}

static const struct v4l2_file_operations ge2d_fops = {
	.owner = THIS_MODULE,
	.open = ge2d_open,
	.release = ge2d_release,
	.poll = v4l2_m2m_fop_poll,
	.mmap = v4l2_m2m_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
};

static struct device *ge2d_get_device(struct device *consumer)
{
	struct platform_device *supplier;
	struct meson8b_ge2d *core;
	struct device_node *node;

	node = of_parse_phandle(consumer->of_node, "amlogic,ge2d", 0);
	if (!node)
		return ERR_PTR(-ENODEV);
	if (!of_device_is_compatible(node, "amlogic,meson8b-ge2d")) {
		of_node_put(node);
		return ERR_PTR(-ENODEV);
	}
	supplier = of_find_device_by_node(node);
	of_node_put(node);
	if (!supplier)
		return ERR_PTR(-EPROBE_DEFER);
	core = platform_get_drvdata(supplier);
	if (!core || !READ_ONCE(core->ready)) {
		put_device(&supplier->dev);
		return ERR_PTR(-EPROBE_DEFER);
	}
	if (!device_link_add(consumer, &supplier->dev, DL_FLAG_AUTOREMOVE_CONSUMER)) {
		put_device(&supplier->dev);
		return ERR_PTR(-ENOMEM);
	}
	return &supplier->dev;
}

static void ge2d_put_device(struct device *supplier)
{
	put_device(supplier);
}

static int ge2d_convert(struct device *supplier, const struct meson8b_ge2d_image *input,
			const struct meson8b_ge2d_image *output,
			const struct meson8b_ge2d_crop *crop)
{
	struct meson8b_ge2d *core = dev_get_drvdata(supplier);
	int ret;

	mutex_lock(&core->hardware_lock);
	ret = core->ready ? ge2d_blit(core, input, output, crop) : -ENODEV;
	mutex_unlock(&core->hardware_lock);
	return ret;
}

const struct meson8b_ge2d_api *meson8b_ge2d_get_api(void)
{
	static const struct meson8b_ge2d_api api = {
		.get = ge2d_get_device, .put = ge2d_put_device, .convert = ge2d_convert,
	};

	return &api;
}
EXPORT_SYMBOL_GPL(meson8b_ge2d_get_api);

static int ge2d_probe(struct platform_device *pdev)
{
	struct meson8b_ge2d *core;
	int ret;

	if (!meson8b_runtime_ready())
		return -EPROBE_DEFER;
	core = devm_kzalloc(&pdev->dev, sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;
	core->dev = &pdev->dev;
	mutex_init(&core->hardware_lock);
	core->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(core->regs))
		return PTR_ERR(core->regs);
	core->clk = devm_clk_get(core->dev, NULL);
	if (IS_ERR(core->clk))
		return PTR_ERR(core->clk);
	core->reset = devm_reset_control_get_exclusive(core->dev, NULL);
	if (IS_ERR(core->reset))
		return PTR_ERR(core->reset);
	core->canvas = meson_canvas_get(core->dev);
	if (IS_ERR(core->canvas))
		return PTR_ERR(core->canvas);
	ret = dma_set_mask_and_coherent(core->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;
	ret = meson_canvas_alloc(core->canvas, &core->src_canvas);
	if (ret)
		return ret;
	ret = meson_canvas_alloc(core->canvas, &core->dst_canvas);
	if (ret)
		goto free_src;
	ret = meson_canvas_alloc(core->canvas, &core->src_uv_canvas);
	if (ret)
		goto free_dst;
	ret = meson_canvas_alloc(core->canvas, &core->dst_uv_canvas);
	if (ret)
		goto free_src_uv;
	ret = meson_canvas_alloc(core->canvas, &core->src_v_canvas);
	if (ret)
		goto free_dst_uv;
	ret = meson_canvas_alloc(core->canvas, &core->src2_canvas);
	if (ret)
		goto free_src_v;
	ret = clk_prepare_enable(core->clk);
	if (ret)
		goto free_src2;
	ret = reset_control_deassert(core->reset);
	if (ret)
		goto disable_clk;
	ret = ge2d_selftest(core);
	if (ret) {
		dev_err(core->dev, "byte-order hardware self-test failed: %d\n", ret);
		goto disable_clk;
	}
	mutex_init(&core->lock);
	INIT_WORK(&core->work, ge2d_work);
	ret = v4l2_device_register(core->dev, &core->v4l2);
	if (ret)
		goto disable_clk;
	core->m2m = v4l2_m2m_init(&ge2d_m2m_ops);
	if (IS_ERR(core->m2m)) {
		ret = PTR_ERR(core->m2m);
		goto unregister_v4l2;
	}
	core->video = video_device_alloc();
	if (!core->video) {
		ret = -ENOMEM;
		goto release_m2m;
	}
	strscpy(core->video->name, "meson8b-ge2d", sizeof(core->video->name));
	core->video->fops = &ge2d_fops;
	core->video->ioctl_ops = &ge2d_ioctl_ops;
	core->video->v4l2_dev = &core->v4l2;
	core->video->lock = &core->lock;
	core->video->release = video_device_release;
	core->video->vfl_dir = VFL_DIR_M2M;
	core->video->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;
	video_set_drvdata(core->video, core);
	ret = video_register_device(core->video, VFL_TYPE_VIDEO, -1);
	if (ret)
		goto release_video;
	platform_set_drvdata(pdev, core);
	WRITE_ONCE(core->ready, true);
	dev_info(core->dev, "byte-order self-test passed; registered /dev/video%d\n",
		 core->video->num);
	return 0;
release_video:
	video_device_release(core->video);
release_m2m:
	v4l2_m2m_release(core->m2m);
unregister_v4l2:
	v4l2_device_unregister(&core->v4l2);
disable_clk:
	clk_disable_unprepare(core->clk);
free_src2:
	meson_canvas_free(core->canvas, core->src2_canvas);
free_src_v:
	meson_canvas_free(core->canvas, core->src_v_canvas);
free_dst_uv:
	meson_canvas_free(core->canvas, core->dst_uv_canvas);
free_src_uv:
	meson_canvas_free(core->canvas, core->src_uv_canvas);
free_dst:
	meson_canvas_free(core->canvas, core->dst_canvas);
free_src:
	meson_canvas_free(core->canvas, core->src_canvas);
	return ret;
}

static int ge2d_remove(struct platform_device *pdev)
{
	struct meson8b_ge2d *core = platform_get_drvdata(pdev);

	mutex_lock(&core->hardware_lock);
	WRITE_ONCE(core->ready, false);
	mutex_unlock(&core->hardware_lock);
	video_unregister_device(core->video);
	flush_work(&core->work);
	v4l2_m2m_release(core->m2m);
	v4l2_device_unregister(&core->v4l2);
	reset_control_assert(core->reset);
	clk_disable_unprepare(core->clk);
	if (core->blend_stage_vaddr)
		dma_free_coherent(core->dev, core->blend_stage_size,
				  core->blend_stage_vaddr, core->blend_stage_dma);
	meson_canvas_free(core->canvas, core->src2_canvas);
	meson_canvas_free(core->canvas, core->src_v_canvas);
	meson_canvas_free(core->canvas, core->dst_uv_canvas);
	meson_canvas_free(core->canvas, core->src_uv_canvas);
	meson_canvas_free(core->canvas, core->dst_canvas);
	meson_canvas_free(core->canvas, core->src_canvas);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static const struct of_device_id ge2d_match[] = {
	{ .compatible = "amlogic,meson8b-ge2d" },
	{},
};
MODULE_DEVICE_TABLE(of, ge2d_match);

static struct platform_driver ge2d_driver = {
	.probe = ge2d_probe,
	.remove = ge2d_remove,
	.driver = { .name = "meson8b-ge2d", .of_match_table = ge2d_match,
		    .suppress_bind_attrs = true },
};
module_platform_driver(ge2d_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("S805 canvas GE2D byte-plane verification driver");
