// SPDX-License-Identifier: GPL-2.0+
/*
 * Standalone JPEG picture decoder for Meson8b (S805) using VDEC_1 + PSCALE.
 * Ported from Hardkernel 3.10 vendor jpegdec.c (Tim Yao <timyao@amlogic.com>).
 */

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/swab.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "codec_jpeg.h"
#include "dos_regs.h"
#include "esparser.h"
#include "jpeg-header.h"
#include "vdec-input.h"
#include "vdec_helpers.h"
#include "vdec_1.h"

#define JPEG_DECODE_START	AV_SCRATCH_0
#define JPEG_INFO		AV_SCRATCH_0
#define JPEG_PIC_WIDTH		AV_SCRATCH_1
#define JPEG_PIC_HEIGHT		AV_SCRATCH_2
#define JPEG_DECODE_PARAMETER	AV_SCRATCH_3
#define JPEG_SCREEN_OFFSET_Y	AV_SCRATCH_4
#define JPEG_SCREEN_OFFSET_X	AV_SCRATCH_5

#define JPEG_MCU_CROP_HSTART	PSCALE_PICI_W
#define JPEG_MCU_CROP_HEND	PSCALE_PICI_H
#define JPEG_MCU_CROP_VSTART	PSCALE_PICO_W
#define JPEG_MCU_CROP_VEND	PSCALE_PICO_H

/* PSCALE 128-row 32x32-tile staging buffer: 1920 * 128 * (1 + 0.5 + 0.5) < 512K */
#define SIZE_PSCALE_BUF		SZ_512K

enum codec_jpeg_state {
	JPEG_STATE_WAIT_INFO,
	JPEG_STATE_WAIT_CAPTURE,
	JPEG_STATE_DECODING,
};

struct codec_jpeg {
	void *pscale_vaddr;
	dma_addr_t pscale_paddr;
	void *out_vaddr;
	dma_addr_t out_paddr;
	u32 out_size;
	u32 copy_stride;
	u32 copy_c_stride;
	u32 copy_hw_c_stride;
	u32 copy_height;

	struct amvdec_session *sess;
	struct delayed_work drain_work;
	u64 drain_jiffies;
	bool draining;

	enum codec_jpeg_state state;
	u32 info_width;
	u32 info_height;
	u32 comp_num;

	u8 out_canvas[3];
	u8 pscale_canvas[3];
};

static void codec_jpeg_reset_hw(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	amvdec_write_dos(core, DOS_SW_RESET0, BIT(11));
	amvdec_write_dos(core, DOS_SW_RESET0, 0);

	amvdec_write_dos(core, VDEC_ASSIST_AMR1_INT0, 0x1);
	amvdec_write_dos(core, VDEC_ASSIST_AMR1_INT1, 0xf);
	amvdec_write_dos(core, VDEC_ASSIST_AMR1_INT2, 0x8);
	amvdec_write_dos(core, VDEC_ASSIST_AMR1_INT3, 0xa);
	amvdec_write_dos(core, VDEC_ASSIST_AMR1_INT4, 0x3);
	amvdec_write_dos(core, VDEC_ASSIST_AMR1_INT5, 0x9);
	amvdec_write_dos(core, VDEC_ASSIST_AMR1_INT6, 0x4);

	amvdec_write_dos(core, DOS_SW_RESET0,
			 BIT(11) | BIT(10) | BIT(7) | BIT(6));
	amvdec_write_dos(core, DOS_SW_RESET0, 0);

	amvdec_write_dos(core, PSCALE_RST, 0x7);
	amvdec_write_dos(core, PSCALE_RST, 0x0);

	amvdec_write_dos(core, JPEG_PIC_WIDTH, 0);
	amvdec_write_dos(core, JPEG_PIC_HEIGHT, 0);
	amvdec_write_dos(core, JPEG_DECODE_START, 0);
	amvdec_write_dos(core, MCPU_INTR_MSK, 0xffff);
}

static void codec_jpeg_init_scaler(struct amvdec_core *core,
				   u32 horz_step, u32 vert_step)
{
	static const u32 filt_coef[33] = {
		0x00800000, 0x007e0200, 0x007c0400, 0x007a0600,
		0x00780800, 0x00760a00, 0x00740c00, 0x00720e00,
		0x00701000, 0x006e1200, 0x006c1400, 0x006a1600,
		0x00681800, 0x00661a00, 0x00641c00, 0x00621e00,
		0x00602000, 0x005e2200, 0x005c2400, 0x005a2600,
		0x00582800, 0x00562a00, 0x00542c00, 0x00522e00,
		0x00503000, 0x004e3200, 0x004c3400, 0x004a3600,
		0x00483800, 0x00463a00, 0x00443c00, 0x00423e00,
		0x00404000,
	};
	int i;

	amvdec_write_dos(core, PSCALE_CTRL, 0xc000);
	amvdec_write_dos(core, PSCALE_BMEM_ADDR, 0);
	for (i = 0; i < 33; i++) {
		amvdec_write_dos(core, PSCALE_BMEM_DAT, 0);
		amvdec_write_dos(core, PSCALE_BMEM_DAT, filt_coef[i]);
	}

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

	amvdec_write_dos(core, PSCALE_BMEM_ADDR, 36 * 2 + 1);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, horz_step);
	amvdec_write_dos(core, PSCALE_BMEM_ADDR, 40 * 2 + 1);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, horz_step);
	amvdec_write_dos(core, PSCALE_BMEM_ADDR, 38 * 2 + 1);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, vert_step);
	amvdec_write_dos(core, PSCALE_BMEM_ADDR, 42 * 2 + 1);
	amvdec_write_dos(core, PSCALE_BMEM_DAT, vert_step);
}

static int codec_jpeg_inspect_input(struct amvdec_session *sess,
				    struct vb2_buffer *vb, u32 offset)
{
	(void)offset;
	return jpeg_validate_header(amvdec_input_data(vb),
				    amvdec_input_size(vb),
				    sess->width, sess->height, NULL);
}

static int codec_jpeg_start_picture(struct amvdec_session *sess)
{
	static const u8 addr_mode_tab[4] = { 0x0, 0xd, 0xa, 0x7 };
	struct codec_jpeg *jpeg = sess->priv;
	struct amvdec_core *core = sess->core;
	struct vb2_v4l2_buffer *vbuf;
	u32 src_w = sess->width, src_h = sess->height;
	struct meson_capture_size cap;
	u32 angle, cap_w, cap_h, stride, c_stride, hw_c_stride, pscale_w;
	u32 luma, uv_hw_size, need;
	u32 r0_h, r1_h, r0_v, r1_v, horz_step, vert_step;
	u32 out_canv, psc_canv;
	dma_addr_t addr_y, addr_u, addr_v;
	int ret;

	vbuf = v4l2_m2m_next_dst_buf(sess->m2m_ctx);
	if (!vbuf)
		return -EAGAIN;

	angle = (sess->rotation / 90) & 3;
	if (angle & 1)
		swap(src_w, src_h);
	cap = meson_capture_resolve(&sess->capture, src_w, src_h);
	cap_w = clamp_t(u32, cap.width, 16, 1920);
	cap_h = clamp_t(u32, cap.height, 16, 1088);
	if ((angle & 1) == 0) {
		if (cap_w > jpeg->info_width)
			cap_w = jpeg->info_width;
		if (cap_h > jpeg->info_height)
			cap_h = jpeg->info_height;
	} else {
		if (cap_w > jpeg->info_height)
			cap_w = jpeg->info_height;
		if (cap_h > jpeg->info_width)
			cap_h = jpeg->info_width;
	}
	cap_w = ALIGN(cap_w, 2);
	cap_h = ALIGN(cap_h, 2);
	stride = ALIGN(cap_w, 32);
	c_stride = stride / 2;
	/* PSCALE writes chroma in 32-byte spans independent of luma stride. */
	hw_c_stride = ALIGN(c_stride, 32);

	pscale_w = (angle & 1) ? cap_h : cap_w;
	pscale_w = ALIGN(pscale_w, 64);

	luma = stride * cap_h;
	uv_hw_size = hw_c_stride * (cap_h / 2);
	need = PAGE_ALIGN(luma + 2 * uv_hw_size);
	if (jpeg->out_size < need) {
		if (jpeg->out_vaddr) {
			dma_free_coherent(core->dev, jpeg->out_size,
					  jpeg->out_vaddr,
					  jpeg->out_paddr);
			jpeg->out_vaddr = NULL;
			jpeg->out_size = 0;
		}
		jpeg->out_vaddr = dma_alloc_coherent(core->dev, need,
						     &jpeg->out_paddr,
						     GFP_KERNEL);
		if (!jpeg->out_vaddr)
			return -ENOMEM;
		jpeg->out_size = need;
	}
	addr_y = jpeg->out_paddr;
	addr_u = jpeg->out_paddr + luma;
	addr_v = jpeg->out_paddr + luma + uv_hw_size;
	jpeg->copy_stride = stride;
	jpeg->copy_c_stride = c_stride;
	jpeg->copy_hw_c_stride = hw_c_stride;
	jpeg->copy_height = cap_h;

	ret = meson_canvas_config(core->canvas, jpeg->out_canvas[0],
				  addr_y, stride, cap_h,
				  MESON_CANVAS_WRAP_NONE,
				  MESON_CANVAS_BLKMODE_LINEAR, 0);
	if (ret)
		return ret;
	ret = meson_canvas_config(core->canvas, jpeg->out_canvas[1],
				  addr_u, hw_c_stride, cap_h / 2,
				  MESON_CANVAS_WRAP_NONE,
				  MESON_CANVAS_BLKMODE_LINEAR, 0);
	if (ret)
		return ret;
	ret = meson_canvas_config(core->canvas, jpeg->out_canvas[2],
				  addr_v, hw_c_stride, cap_h / 2,
				  MESON_CANVAS_WRAP_NONE,
				  MESON_CANVAS_BLKMODE_LINEAR, 0);
	if (ret)
		return ret;

	ret = meson_canvas_config(core->canvas, jpeg->pscale_canvas[0],
				  jpeg->pscale_paddr, pscale_w, 128,
				  MESON_CANVAS_WRAP_NONE,
				  MESON_CANVAS_BLKMODE_32x32, 0);
	if (ret)
		return ret;
	ret = meson_canvas_config(core->canvas, jpeg->pscale_canvas[1],
				  jpeg->pscale_paddr + pscale_w * 128,
				  pscale_w / 2, 128,
				  MESON_CANVAS_WRAP_NONE,
				  MESON_CANVAS_BLKMODE_32x32, 0);
	if (ret)
		return ret;
	ret = meson_canvas_config(core->canvas, jpeg->pscale_canvas[2],
				  jpeg->pscale_paddr + pscale_w * 128 +
				  (pscale_w / 2) * 128,
				  pscale_w / 2, 128,
				  MESON_CANVAS_WRAP_NONE,
				  MESON_CANVAS_BLKMODE_32x32, 0);
	if (ret)
		return ret;

	amvdec_write_dos(core, JPEG_SCREEN_OFFSET_X, 0);
	amvdec_write_dos(core, JPEG_SCREEN_OFFSET_Y, 0);
	amvdec_write_dos(core, PSCALE_PICO_SHIFT_XY, 0);

	amvdec_write_dos(core, JPEG_MCU_CROP_HSTART, 0);
	amvdec_write_dos(core, JPEG_MCU_CROP_HEND,
			 ((jpeg->info_width + 15) >> 4) - 1);
	amvdec_write_dos(core, JPEG_MCU_CROP_VSTART, 0);
	amvdec_write_dos(core, JPEG_MCU_CROP_VEND,
			 ((jpeg->info_height + 15) >> 4) - 1);

	if ((angle & 1) == 0) {
		r0_h = jpeg_pscale_filt0_ratio(jpeg->info_width, cap_w, &r1_h);
		horz_step = jpeg_pscale_step(jpeg->info_width, cap_w, r0_h, r1_h);
		r0_v = jpeg_pscale_filt0_ratio(jpeg->info_height, cap_h, &r1_v);
		vert_step = jpeg_pscale_step(jpeg->info_height, cap_h, r0_v, r1_v);
		amvdec_write_dos(core, JPEG_PIC_WIDTH, cap_w);
		amvdec_write_dos(core, JPEG_PIC_HEIGHT, cap_h);
	} else {
		r0_h = jpeg_pscale_filt0_ratio(jpeg->info_width, cap_h, &r1_h);
		horz_step = jpeg_pscale_step(jpeg->info_width, cap_h, r0_h, r1_h);
		r0_v = jpeg_pscale_filt0_ratio(jpeg->info_height, cap_w, &r1_v);
		vert_step = jpeg_pscale_step(jpeg->info_height, cap_w, r0_v, r1_v);
		amvdec_write_dos(core, JPEG_PIC_WIDTH, cap_h);
		amvdec_write_dos(core, JPEG_PIC_HEIGHT, cap_w);
	}

	codec_jpeg_init_scaler(core, horz_step, vert_step);

	amvdec_write_dos(core, JPEG_DECODE_PARAMETER,
			 (r1_h << 14) | (r1_v << 12) |
			 (r0_h << 10) | (r0_v << 8) |
			 (addr_mode_tab[angle] << 4));

	amvdec_write_dos(core, PSCALE_RBUF_START_BLKX, 0);
	amvdec_write_dos(core, PSCALE_RBUF_START_BLKY, 0);

	out_canv = ((u32)jpeg->out_canvas[2] << 16) |
		   ((u32)jpeg->out_canvas[1] << 8) |
		   jpeg->out_canvas[0];
	psc_canv = ((u32)jpeg->pscale_canvas[2] << 16) |
		   ((u32)jpeg->pscale_canvas[1] << 8) |
		   jpeg->pscale_canvas[0];
	amvdec_write_dos(core, PSCALE_CANVAS_WR_ADDR, out_canv);
	amvdec_write_dos(core, PSCALE_CANVAS_RD_ADDR, psc_canv);

	jpeg->state = JPEG_STATE_DECODING;
	amvdec_write_dos(core, JPEG_DECODE_START, 1);
	return 0;
}

static void codec_jpeg_capture_queued(struct amvdec_session *sess)
{
	struct codec_jpeg *jpeg = sess->priv;
	int ret;

	if (!jpeg || jpeg->state != JPEG_STATE_WAIT_CAPTURE)
		return;
	ret = codec_jpeg_start_picture(sess);
	if (ret < 0 && ret != -EAGAIN)
		amvdec_abort(sess);
}

static void codec_jpeg_drain_work(struct work_struct *work)
{
	struct codec_jpeg *jpeg =
		container_of(work, struct codec_jpeg, drain_work.work);
	struct amvdec_session *sess = jpeg->sess;
	static const u32 max_polls = 150; /* 3 s at 20 ms */
	u32 polls = 0;
	bool irq_quiet = false;

	while (polls++ < max_polls) {
		int in_flight = atomic_read(&sess->esparser_queued_bufs);
		u64 quiet_after = max_t(u64, READ_ONCE(sess->last_irq_jiffies),
					jpeg->drain_jiffies);

		irq_quiet = !READ_ONCE(sess->irq_active) &&
			jpeg->state != JPEG_STATE_DECODING &&
			!time_is_after_jiffies64(quiet_after + msecs_to_jiffies(100));

		if (irq_quiet && !in_flight)
			break;

		msleep(20);
	}

	jpeg->draining = false;
	if (!irq_quiet) {
		dev_warn(sess->core->dev, "jpeg decode drain timeout (state=%d)\n",
			 jpeg->state);
		amvdec_abort(sess);
		return;
	}
	amvdec_flush_capture_buffers(sess);
}

static void codec_jpeg_drain(struct amvdec_session *sess)
{
	struct codec_jpeg *jpeg = sess->priv;

	if (jpeg->draining)
		return;

	jpeg->drain_jiffies = get_jiffies_64();
	jpeg->draining = true;
	schedule_delayed_work(&jpeg->drain_work, 0);
}

static int codec_jpeg_start(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_jpeg *jpeg;
	int ret, i;

	jpeg = kzalloc(sizeof(*jpeg), GFP_KERNEL);
	if (!jpeg)
		return -ENOMEM;

	jpeg->pscale_vaddr = dma_alloc_coherent(core->dev, SIZE_PSCALE_BUF,
						&jpeg->pscale_paddr,
						GFP_KERNEL);
	if (!jpeg->pscale_vaddr) {
		ret = -ENOMEM;
		goto free_jpeg;
	}

	for (i = 0; i < 3; i++) {
		ret = amvdec_canvas_alloc(sess, &jpeg->out_canvas[i]);
		if (ret)
			goto free_dma;
	}
	for (i = 0; i < 3; i++) {
		ret = amvdec_canvas_alloc(sess, &jpeg->pscale_canvas[i]);
		if (ret)
			goto free_dma;
	}

	codec_jpeg_reset_hw(sess);

	INIT_DELAYED_WORK(&jpeg->drain_work, codec_jpeg_drain_work);
	jpeg->sess = sess;
	jpeg->state = JPEG_STATE_WAIT_INFO;
	sess->keyframe_found = 1;
	sess->priv = jpeg;

	return 0;

free_dma:
	dma_free_coherent(core->dev, SIZE_PSCALE_BUF,
			  jpeg->pscale_vaddr, jpeg->pscale_paddr);
free_jpeg:
	kfree(jpeg);
	return ret;
}

static int codec_jpeg_stop(struct amvdec_session *sess)
{
	struct codec_jpeg *jpeg = sess->priv;
	struct amvdec_core *core = sess->core;

	cancel_delayed_work_sync(&jpeg->drain_work);
	if (jpeg->out_vaddr)
		dma_free_coherent(core->dev, jpeg->out_size,
				  jpeg->out_vaddr, jpeg->out_paddr);
	if (jpeg->pscale_vaddr)
		dma_free_coherent(core->dev, SIZE_PSCALE_BUF,
				  jpeg->pscale_vaddr, jpeg->pscale_paddr);
	return 0;
}

static void codec_jpeg_fill_gray_chroma(struct amvdec_session *sess,
					struct vb2_v4l2_buffer *vbuf)
{
	struct vb2_buffer *vb = &vbuf->vb2_buf;
	u32 src_w = sess->width, src_h = sess->height;
	struct meson_capture_size cap;
	u32 chroma_size;
	void *u;
	void *v;

	if ((sess->rotation / 90) & 1)
		swap(src_w, src_h);
	cap = meson_capture_resolve(&sess->capture, src_w, src_h);
	chroma_size = amvdec_get_output_size_wh(cap.width, cap.height) / 4;
	u = vb2_plane_vaddr(vb, 1);
	v = vb2_plane_vaddr(vb, 2);

	if (u && chroma_size <= vb2_plane_size(vb, 1))
		memset(u, 0x80, chroma_size);
	if (v && chroma_size <= vb2_plane_size(vb, 2))
		memset(v, 0x80, chroma_size);
}

static void codec_jpeg_swap64_row(u8 *dst, const u8 *src, u32 bytes)
{
	u64 *d = (u64 *)dst;
	const u64 *s = (const u64 *)src;
	u32 i;

	for (i = 0; i < bytes / 8; i++)
		d[i] = swab64(s[i]);
}

static irqreturn_t codec_jpeg_threaded_isr(struct amvdec_session *sess)
{
	struct codec_jpeg *jpeg = sess->priv;
	struct amvdec_core *core = sess->core;

	amvdec_write_dos(core, ASSIST_MBOX1_CLR_REG, 1);

	if (jpeg->state == JPEG_STATE_WAIT_INFO) {
		jpeg->info_width = amvdec_read_dos(core, JPEG_PIC_WIDTH);
		jpeg->info_height = amvdec_read_dos(core, JPEG_PIC_HEIGHT);
		jpeg->comp_num = amvdec_read_dos(core, JPEG_INFO) >> 1;

		if (!jpeg->info_width || !jpeg->info_height ||
		    (jpeg->comp_num != 1 && jpeg->comp_num != 3)) {
			amvdec_abort(sess);
			return IRQ_HANDLED;
		}
		jpeg->state = JPEG_STATE_WAIT_CAPTURE;
		{
			int ret = codec_jpeg_start_picture(sess);

			if (ret < 0 && ret != -EAGAIN)
				amvdec_abort(sess);
		}
	} else if (jpeg->state == JPEG_STATE_DECODING) {
		u32 status = amvdec_read_dos(core, JPEG_DECODE_START);
		struct vb2_v4l2_buffer *vbuf;

		if (status == 2) {
			struct vb2_buffer *vb;
			u32 luma = jpeg->copy_stride * jpeg->copy_height;
			u32 ch = jpeg->copy_height / 2;
			u32 cs = jpeg->copy_c_stride;
			u32 hs = jpeg->copy_hw_c_stride;
			u32 chroma = cs * ch;
			u32 uv_hw_size = hs * ch;
			const u8 *src_u = (const u8 *)jpeg->out_vaddr + luma;
			const u8 *src_v = src_u + uv_hw_size;
			void *dst_y, *dst_u, *dst_v;
			unsigned int i, acquired = 0;
			int ret = 0;

			vbuf = v4l2_m2m_dst_buf_remove(sess->m2m_ctx);
			if (!vbuf) {
				amvdec_abort(sess);
				return IRQ_HANDLED;
			}
			vb = &vbuf->vb2_buf;
			for (i = 0; i < 3; i++) {
				if (!vb->planes[i].dbuf)
					continue;
				ret = dma_buf_begin_cpu_access(vb->planes[i].dbuf,
							       DMA_BIDIRECTIONAL);
				if (ret)
					break;
				acquired++;
			}
			dst_y = vb2_plane_vaddr(vb, 0);
			dst_u = vb2_plane_vaddr(vb, 1);
			dst_v = vb2_plane_vaddr(vb, 2);

			if (ret || !dst_y || !dst_u || !dst_v ||
			    luma > vb2_plane_size(vb, 0) ||
			    chroma > vb2_plane_size(vb, 1) ||
			    chroma > vb2_plane_size(vb, 2)) {
				while (acquired--)
					dma_buf_end_cpu_access(vb->planes[acquired].dbuf,
							       DMA_BIDIRECTIONAL);
				v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
				amvdec_abort(sess);
				return IRQ_HANDLED;
			}
			codec_jpeg_swap64_row(dst_y, jpeg->out_vaddr, luma);
			if (jpeg->comp_num == 1) {
				codec_jpeg_fill_gray_chroma(sess, vbuf);
			} else {
				u32 row;

				for (row = 0; row < ch; row++) {
					codec_jpeg_swap64_row((u8 *)dst_u + row * cs,
							      src_u + row * hs, cs);
					codec_jpeg_swap64_row((u8 *)dst_v + row * cs,
							      src_v + row * hs, cs);
				}
			}
			for (i = 0; i < 3; i++)
				if (vb->planes[i].dbuf)
					dma_buf_end_cpu_access(vb->planes[i].dbuf,
							       DMA_BIDIRECTIONAL);
			amvdec_dst_buf_done(sess, vbuf, V4L2_FIELD_NONE);
			jpeg->state = JPEG_STATE_WAIT_INFO;
		} else {
			amvdec_abort(sess);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t codec_jpeg_isr(struct amvdec_session *sess)
{
	return IRQ_WAKE_THREAD;
}

struct amvdec_codec_ops codec_jpeg_ops = {
	.start = codec_jpeg_start,
	.stop = codec_jpeg_stop,
	.isr = codec_jpeg_isr,
	.threaded_isr = codec_jpeg_threaded_isr,
	.drain = codec_jpeg_drain,
	.capture_queued = codec_jpeg_capture_queued,
	.inspect_input = codec_jpeg_inspect_input,
};
