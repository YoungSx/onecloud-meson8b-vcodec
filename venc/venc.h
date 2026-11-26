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
#include <linux/spinlock.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>
#include <linux/soc/amlogic/meson-canvas.h>

#include "hcodec_regs.h"
#include "rate-control.h"
#include "rate-limit.h"

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

	struct venc_ctx *ctx;		/* hardware owner, separate from open handles */
	struct dentry *debugfs;
	/* frames read straight from a DMA-BUF / copied into staging, copy time */
	u64 direct_frames, copied_frames, copy_ns;
	spinlock_t rc_stats_lock;
	struct {
		u64 frames, bytes, target_bps, current_bps, peak_bps, window_ns;
		u64 saturated_frames, saturated_ns, overshoots, overflows;
		u64 peak_limit_bps, cpb_bytes, credit_bits, budget_violations;
		bool limit_terminal;
		s32 qp;
	} rc_stats;

	u32 hw_status;
	bool need_reset;
	bool running;
	bool done_pending;
	struct completion done;
	struct mutex lock;
	struct mutex session_lock; /* hardware activation and session teardown */
	u32 instances;
	int irq;
};

/* Raw input format; see venc_formats[] */
struct venc_format {
	u32 fourcc;
	u32 alias;		/* single-planar spelling accepted for it, or 0 */
	u8 planes;		/* memory planes */
	u8 halves[3];		/* plane bytes per pixel, in halves */
	u8 vsub[3];		/* plane rows per picture row divisor */
	u8 iformat;		/* MFDIN input format; >= 8 reads linear memory */
	u8 r2y;			/* MFDIN converts RGB to YUV */
	u8 align;		/* staging canvas width alignment, bytes */
	u32 endn;		/* MFDIN byte order, 0 for the default */
	u32 max_width;		/* 0 for the encoder's 1920 */
};

struct venc_ctx {
	struct v4l2_fh fh;
	struct venc_core *core;
	struct v4l2_m2m_ctx *m2m_ctx;
	struct v4l2_ctrl_handler ctrl_handler;
	struct work_struct work;

	u32 width, height;
	u32 coded_width;		/* macroblock-aligned width of the session */
	u32 quant;			/* QP of the picture being encoded */

	/*
	 * Control state, guarded by the control handler lock. The encode work
	 * takes the same lock to pick each picture's type and QP and to
	 * account it, so updates apply on picture boundaries.
	 */
	u32 gop_size;			/* 0: only the first and forced pictures are IDR */
	u32 gop_idx;			/* pictures since the last IDR */
	bool force_key;
	s32 i_qp, p_qp;			/* fixed QPs; the P QP also seeds rate control */
	bool rc_enable;			/* V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE */
	bool rc_restart;		/* reset the rate control before the next picture */
	struct venc_rc rc_config;	/* controls pending until the next picture */
	struct venc_rc rc;		/* active picture config and feedback history */
	bool picture_rc;
	struct venc_rc_recovery recovery;
	u32 peak_bitrate, cpb_size;	/* pending peak bps / CPB kilobytes */
	struct venc_rate_limit limit;
	struct v4l2_ctrl *rate_controls[3]; /* bitrate, peak, CPB cluster */
	struct venc_rc_measurement rc_measure;
	bool rc_unreachable_reported;
	struct v4l2_ctrl *qp_range[2];	/* MIN_QP/MAX_QP cluster */
	bool multi_slice;		/* MULTI_SLICE_MODE is MAX_MB */
	u32 slice_max_mb;
	u32 slice_cfg;			/* FIXED_SLICE_CFG of this picture */
	bool sar_enable;
	s32 sar_idc, sar_width, sar_height;
	struct v4l2_ctrl *sar_controls[4]; /* enable, IDC, extended width/height */
	struct v4l2_fract timeperframe;	/* rate control and SPS coded-picture period */
	const struct venc_format *src;	/* negotiated raw input */
	u32 src_fourcc;			/* its spelling as negotiated */
	u32 dst_fmt;
	u32 jpeg_quality;
	/* OUTPUT colorimetry, propagated to CAPTURE */
	u32 colorspace, xfer_func, ycbcr_enc, quantization;

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
	dma_addr_t input[3];		/* this frame's planes for MFDIN */
	u32 dblk_canvas, ref_canvas;
	u32 idr_pic_id, frame_number, poc_lsb;

	bool sps_pps_valid;
	u32 sps_pps_size;
	void *sps_pps;
	bool initialized;
};

enum venc_buffer_id {
	VENC_INPUT, VENC_REF0, VENC_REF1, VENC_WORKSPACE, VENC_STREAM, VENC_ANALYSIS
};

static inline void *venc_stream_vaddr(struct venc_ctx *ctx)
{
	return ctx->buffers[VENC_STREAM].vaddr;
}

void venc_jpeg_init(struct venc_ctx *ctx);

extern const struct vb2_mem_ops venc_input_memops;
int venc_input_wait(struct vb2_buffer *vb);
dma_addr_t venc_input_dma(struct vb2_buffer *vb, unsigned int plane);
void *venc_input_begin_cpu(struct vb2_buffer *vb, unsigned int plane);
int venc_input_end_cpu(struct vb2_buffer *vb, unsigned int plane);

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
