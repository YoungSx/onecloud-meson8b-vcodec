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
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>
#include <linux/soc/amlogic/meson-canvas.h>

#include "hcodec_regs.h"

/* Vendor 1080P buffspec, 17.4 MiB (encoder always uses this level) */
#define VENC_BUF_SIZE		0x1160000
#define VENC_BITSTREAM_SIZE	0x100000
#define VENC_MC_SIZE		(4096 * 4)
#define VENC_FW_NAME		"meson/venc/meson8b_avc_enc.bin"

struct venc_core {
	struct device *dev;
	struct v4l2_device v4l2_dev;
	struct video_device *vdev_ptr;
	struct v4l2_m2m_dev *m2m_dev;

	void __iomem *dos_base;
	void __iomem *ao_pwr_base;	/* AO_RTI_PWR_CNTL_REG0 window */
	void __iomem *ao_sleep_base;	/* AO_RTI_GEN_PWR_SLEEP0/ISO0 window */
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
	u32 src_fmt;			/* V4L2_PIX_FMT_*, M-variant */

	/* single big buffer laid out per vendor buffspec */
	void *buf_vaddr;
	dma_addr_t buf_paddr;
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

static inline u32 venc_read(struct venc_core *core, u32 reg)
{
	return readl_relaxed(core->dos_base + reg);
}

static inline void venc_write(struct venc_core *core, u32 reg, u32 val)
{
	writel_relaxed(val, core->dos_base + reg);
}

/*
 * AO accessors take aobus byte offsets (AO_RTI_*) and translate them into
 * the ao-pwr window (starts at AO_RTI_PWR_CNTL_REG0) or the ao-sleep window
 * (starts at AO_RTI_GEN_PWR_SLEEP0).
 */
static inline u32 venc_read_ao(struct venc_core *core, u32 reg)
{
	return readl_relaxed(core->ao_pwr_base + reg - AO_RTI_PWR_CNTL_REG0);
}

static inline void venc_write_ao(struct venc_core *core, u32 reg, u32 val)
{
	writel_relaxed(val, core->ao_pwr_base + reg - AO_RTI_PWR_CNTL_REG0);
}

static inline u32 venc_read_ao_sleep(struct venc_core *core, u32 reg)
{
	return readl_relaxed(core->ao_sleep_base + reg - AO_RTI_GEN_PWR_SLEEP0);
}

static inline void venc_write_ao_sleep(struct venc_core *core, u32 reg,
				       u32 val)
{
	writel_relaxed(val, core->ao_sleep_base + reg - AO_RTI_GEN_PWR_SLEEP0);
}

#endif
