/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Amlogic Meson8b (S805) HCODEC H.264 hardware encoder,
 * V4L2 mem2mem driver.
 */
#ifndef __MESON_VENC_H_
#define __MESON_VENC_H_

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/firmware.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/regmap.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>
#include <linux/soc/amlogic/meson-canvas.h>

#include "hcodec_regs.h"

#define VENC_BITSTREAM_SIZE	0x100000
#define VENC_MC_SIZE		(4096 * 4)
#define VENC_FW_NAME		"meson/venc/meson8b_avc_enc.bin"
#define VENC_JPEG_FW_NAME	"meson/venc/meson8b_jpeg_enc.bin"

struct venc_core {
	struct device *dev;
	struct v4l2_device v4l2_dev;
	struct video_device *vdev_ptr;
	struct v4l2_m2m_dev *m2m_dev;

	void __iomem *dos_base;
	void __iomem *ao_pwr_base;	/* AO_RTI_PWR_CNTL_REG0 window */
	struct regmap *regmap_ao;	/* shared Meson8b PMU */
	struct meson_canvas *canvas;
	struct clk *dos_clk;
	struct clk *hcodec_clk;

	struct venc_ctx *ctx;		/* single active instance */

	u32 hw_status;
	bool need_reset;
	bool running;
	bool done_pending;
	struct completion done;
	struct mutex lock;
};

struct venc_ctx {
	struct v4l2_fh fh;
	struct venc_core *core;
	struct v4l2_m2m_ctx *m2m_ctx;
	struct v4l2_ctrl_handler ctrl_handler;
	struct work_struct work;

	u32 width, height;
	u32 quant;
	u32 gop_size, gop_idx;

	/* optional CBR (V4L2_CID_MPEG_VIDEO_BITRATE); 0 = fixed QP */
	u32 bitrate;
	u64 rc_bits;			/* bits encoded since rate-control start */
	u32 rc_frames;			/* frames since rate-control start */
	s32 rc_qp;			/* QP the controller walks */
	struct v4l2_fract timeperframe;	/* feeds the CBR budget only */
	struct v4l2_ctrl *qp_range[2];	/* MIN_QP, MAX_QP */
	u32 src_fmt;			/* V4L2_PIX_FMT_*, M-variant */
	u32 dst_fmt;
	u32 jpeg_quality;

	/* Preserve vendor region sizes without one 17 MiB CMA allocation. */
	struct {
		void *vaddr;
		dma_addr_t paddr;
		u32 size;
	} buffers[6]; /* input, reference 0/1, firmware, bitstream, analysis */
	const struct firmware *fw;

	struct {
		u32 dct_start, dct_end;
		u32 dec0_y, dec0_uv;
		u32 dec1_y, dec1_uv;
		u32 assit;		/* also IMEM DMA source */
		u32 bitstream_start, bitstream_end;
		u32 inter_bits, inter_mv;
		u32 intra_bits, intra_pred;
	} mem;

	u8 canvas[9];			/* 0-2 dec0, 3-5 dec1, 6-8 input */
	u32 dblk_canvas, ref_canvas;
	u32 idr_pic_id, frame_number, poc_lsb;

	bool sps_pps_valid;
	u32 sps_pps_size;
	void *sps_pps;
};

enum venc_buffer_id {
	VENC_INPUT, VENC_REF0, VENC_REF1, VENC_WORKSPACE, VENC_STREAM, VENC_ANALYSIS
};

static inline void *venc_stream_vaddr(struct venc_ctx *ctx)
{
	return ctx->buffers[VENC_STREAM].vaddr;
}

void venc_jpeg_init(struct venc_ctx *ctx);

static inline u32 venc_read(struct venc_core *core, u32 reg)
{
	return readl_relaxed(core->dos_base + reg);
}

static inline void venc_write(struct venc_core *core, u32 reg, u32 val)
{
	writel_relaxed(val, core->dos_base + reg);
}

/*
 * The dedicated AO power-control window starts at AO_RTI_PWR_CNTL_REG0.
 * Sleep/isolation updates use the shared PMU regmap.
 */
static inline u32 venc_read_ao(struct venc_core *core, u32 reg)
{
	return readl_relaxed(core->ao_pwr_base + reg - AO_RTI_PWR_CNTL_REG0);
}

static inline void venc_write_ao(struct venc_core *core, u32 reg, u32 val)
{
	writel_relaxed(val, core->ao_pwr_base + reg - AO_RTI_PWR_CNTL_REG0);
}

#endif
