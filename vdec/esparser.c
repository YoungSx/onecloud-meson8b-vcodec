// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 *
 * The Elementary Stream Parser is a HW bitstream parser.
 * It reads bitstream buffers and feeds them to the VIFIFO
 */

#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/reset.h>
#include <linux/interrupt.h>
#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include <linux/jiffies.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-mem2mem.h>

#include "dos_regs.h"
#include "esparser.h"
#include "vdec-input.h"
#include "vdec_helpers.h"
#include "esparser-padding.h"

/* PARSER REGS (CBUS) */
#define PARSER_CONTROL 0x00
	#define ES_PACK_SIZE_BIT	8
	#define ES_WRITE		BIT(5)
	#define ES_SEARCH		BIT(1)
	#define ES_PARSER_START		BIT(0)
#define PARSER_FETCH_ADDR	0x4
#define PARSER_FETCH_CMD	0x8
#define PARSER_CONFIG 0x14
	#define PS_CFG_MAX_FETCH_CYCLE_BIT	0
	#define PS_CFG_STARTCODE_WID_24_BIT	10
	#define PS_CFG_MAX_ES_WR_CYCLE_BIT	12
	#define PS_CFG_PFIFO_EMPTY_CNT_BIT	16
#define PFIFO_WR_PTR 0x18
#define PFIFO_RD_PTR 0x1c
#define PARSER_SEARCH_PATTERN 0x24
	#define ES_START_CODE_PATTERN 0x00000100
#define PARSER_SEARCH_MASK 0x28
	#define ES_START_CODE_MASK	0xffffff00
	#define FETCH_ENDIAN_BIT	27
#define PARSER_INT_ENABLE	0x2c
	#define PARSER_INT_HOST_EN_BIT	8
#define PARSER_INT_STATUS	0x30
	#define PARSER_INTSTAT_SC_FOUND	1
#define PARSER_ES_CONTROL	0x5c
#define PARSER_VIDEO_START_PTR	0x80
#define PARSER_VIDEO_END_PTR	0x84
#define PARSER_VIDEO_WP		0x88
#define PARSER_VIDEO_HOLE	0x90

#define SEARCH_PATTERN_LEN	512
#define VP9_HEADER_SIZE		16

static DECLARE_WAIT_QUEUE_HEAD(wq);
static int search_done;

static irqreturn_t esparser_isr(int irq, void *dev)
{
	int int_status;
	struct amvdec_core *core = dev;

	int_status = amvdec_read_parser(core, PARSER_INT_STATUS);
	amvdec_write_parser(core, PARSER_INT_STATUS, int_status);

	if (int_status & PARSER_INTSTAT_SC_FOUND) {
		amvdec_write_parser(core, PFIFO_RD_PTR, 0);
		amvdec_write_parser(core, PFIFO_WR_PTR, 0);
		WRITE_ONCE(search_done, 1);
		wake_up_interruptible(&wq);
	}

	return IRQ_HANDLED;
}

/*
 * VP9 frame headers need to be appended by a 16-byte long
 * Amlogic custom header
 */
static int vp9_update_header(struct amvdec_core *core, struct vb2_buffer *buf)
{
	u8 *dp;
	u8 marker;
	int dsize;
	int num_frames, cur_frame;
	int cur_mag, mag, mag_ptr;
	int frame_size[8], tot_frame_size[8];
	int total_datasize = 0;
	int new_frame_size;
	unsigned char *old_header = NULL;

	dp = amvdec_input_data(buf);
	dsize = amvdec_input_size(buf);

	if (!dp || dsize <= 0 || dsize >= amvdec_input_capacity(buf)) {
		dev_warn(core->dev, "%s: unable to update header\n", __func__);
		return 0;
	}

	marker = dp[dsize - 1];
	if ((marker & 0xe0) == 0xc0) {
		num_frames = (marker & 0x7) + 1;
		mag = ((marker >> 3) & 0x3) + 1;
		mag_ptr = dsize - mag * num_frames - 2;
		if (mag_ptr < 0)
			return 0;
		if (dp[mag_ptr] != marker)
			return 0;

		mag_ptr++;
		for (cur_frame = 0; cur_frame < num_frames; cur_frame++) {
			frame_size[cur_frame] = 0;
			for (cur_mag = 0; cur_mag < mag; cur_mag++) {
				frame_size[cur_frame] |=
					(dp[mag_ptr] << (cur_mag * 8));
				mag_ptr++;
			}
			if (cur_frame == 0)
				tot_frame_size[cur_frame] =
					frame_size[cur_frame];
			else
				tot_frame_size[cur_frame] =
					tot_frame_size[cur_frame - 1] +
					frame_size[cur_frame];
			total_datasize += frame_size[cur_frame];
		}
	} else {
		num_frames = 1;
		frame_size[0] = dsize;
		tot_frame_size[0] = dsize;
		total_datasize = dsize;
	}

	new_frame_size = total_datasize + num_frames * VP9_HEADER_SIZE;

	if (new_frame_size >= amvdec_input_capacity(buf)) {
		dev_warn(core->dev, "%s: unable to update header\n", __func__);
		return 0;
	}

	for (cur_frame = num_frames - 1; cur_frame >= 0; cur_frame--) {
		int framesize = frame_size[cur_frame];
		int framesize_header = framesize + 4;
		int oldframeoff = tot_frame_size[cur_frame] - framesize;
		int outheaderoff =  oldframeoff + cur_frame * VP9_HEADER_SIZE;
		u8 *fdata = dp + outheaderoff;
		u8 *old_framedata = dp + oldframeoff;

		memmove(fdata + VP9_HEADER_SIZE, old_framedata, framesize);

		fdata[0] = (framesize_header >> 24) & 0xff;
		fdata[1] = (framesize_header >> 16) & 0xff;
		fdata[2] = (framesize_header >> 8) & 0xff;
		fdata[3] = (framesize_header >> 0) & 0xff;
		fdata[4] = ((framesize_header >> 24) & 0xff) ^ 0xff;
		fdata[5] = ((framesize_header >> 16) & 0xff) ^ 0xff;
		fdata[6] = ((framesize_header >> 8) & 0xff) ^ 0xff;
		fdata[7] = ((framesize_header >> 0) & 0xff) ^ 0xff;
		fdata[8] = 0;
		fdata[9] = 0;
		fdata[10] = 0;
		fdata[11] = 1;
		fdata[12] = 'A';
		fdata[13] = 'M';
		fdata[14] = 'L';
		fdata[15] = 'V';

		if (!old_header) {
			/* nothing */
		} else if (old_header > fdata + 16 + framesize) {
			dev_dbg(core->dev, "%s: data has gaps, setting to 0\n",
				__func__);
			memset(fdata + 16 + framesize, 0,
			       (old_header - fdata + 16 + framesize));
		} else if (old_header < fdata + 16 + framesize) {
			dev_err(core->dev, "%s: data overwritten\n", __func__);
		}
		old_header = fdata;
	}

	return new_frame_size;
}

/* Pad the packet to at least 4KiB bytes otherwise the VDEC unit won't trigger
 * ISRs.
 * Also append a start code 000001ff at the end to trigger
 * the ESPARSER interrupt.
 */
static int esparser_pad_start_code(struct amvdec_core *core,
				   struct vb2_buffer *vb,
				   u32 payload_size)
{
	u8 *vaddr = amvdec_input_data(vb);
	int pad_size = esparser_pad_buffer(vaddr, payload_size, amvdec_input_capacity(vb));

	if (pad_size < 0)
		dev_warn(core->dev, "%s: unable to pad start code (payload %u plane %lu)\n",
			 __func__, payload_size, vb2_plane_size(vb, 0));

	return pad_size;
}

static int
esparser_write_data(struct amvdec_core *core, dma_addr_t addr, u32 size)
{
	int ret;

	mutex_lock(&core->esparser_lock);
	/* Clear before the fetch kick: its IRQ may arrive before the wait. */
	WRITE_ONCE(search_done, 0);
	amvdec_write_parser(core, PFIFO_RD_PTR, 0);
	amvdec_write_parser(core, PFIFO_WR_PTR, 0);
	amvdec_write_parser(core, PARSER_CONTROL,
			    ES_WRITE |
			    ES_PARSER_START |
			    ES_SEARCH |
			    (size << ES_PACK_SIZE_BIT));

	amvdec_write_parser(core, PARSER_FETCH_ADDR, addr);
	amvdec_write_parser(core, PARSER_FETCH_CMD,
			    (7 << FETCH_ENDIAN_BIT) |
			    (size + SEARCH_PATTERN_LEN));

	ret = wait_event_interruptible_timeout(wq, READ_ONCE(search_done), (HZ / 5));
	if (ret <= 0)
		amvdec_write_parser(core, PARSER_FETCH_CMD, 0);
	mutex_unlock(&core->esparser_lock);
	return ret;
}

static u32 esparser_vififo_get_free_space(struct amvdec_session *sess)
{
	u32 vififo_usage;
	struct amvdec_ops *vdec_ops = sess->fmt_out->vdec_ops;
	struct amvdec_core *core = sess->core;

	vififo_usage  = vdec_ops->vififo_level(sess);
	vififo_usage += amvdec_read_parser(core, PARSER_VIDEO_HOLE);
	vififo_usage += (6 * SZ_1K); // 6 KiB internal fifo

	if (vififo_usage > sess->vififo_size) {
		dev_warn(sess->core->dev,
			 "VIFIFO usage (%u) > VIFIFO size (%u)\n",
			 vififo_usage, sess->vififo_size);
		return 0;
	}

	return sess->vififo_size - vififo_usage;
}

int esparser_queue_eos(struct amvdec_core *core, const u8 *data, u32 len)
{
	struct device *dev = core->dev;
	u32 span = ESPARSER_PAD_SPAN(len);
	void *eos_vaddr;
	dma_addr_t eos_paddr;
	int pad_size;
	int ret;

	eos_vaddr = dma_alloc_coherent(dev, span, &eos_paddr, GFP_KERNEL);
	if (!eos_vaddr)
		return -ENOMEM;

	memcpy(eos_vaddr, data, len);
	pad_size = esparser_pad_buffer(eos_vaddr, len, span);
	if (pad_size < 0) {
		dma_free_coherent(dev, span, eos_vaddr, eos_paddr);
		return -EINVAL;
	}
	dma_wmb();
	/* Write the padding too: the 000001ff terminator has to reach the
	 * ESPARSER or it never raises the search interrupt, and the firmware
	 * never sees the sequence end.
	 */
	ret = esparser_write_data(core, eos_paddr, len + pad_size);
	dma_free_coherent(dev, span, eos_vaddr, eos_paddr);

	/* A timed-out write used to return 0, hiding a drain terminator that
	 * never reached the parser; the final picture then stays in the
	 * firmware with no kernel trace. Propagate it so the caller fails the
	 * queues instead of announcing a successful drain.
	 */
	if (ret <= 0) {
		dev_err(dev, "esparser: EOS write %s, drain terminator not delivered\n",
			ret ? "was interrupted" : "timed out");
		return ret ? : -ETIMEDOUT;
	}

	return 0;
}

static u32 esparser_get_offset(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	u32 offset = amvdec_read_parser(core, PARSER_VIDEO_WP) -
		     sess->vififo_paddr;

	if (offset < sess->last_offset)
		sess->wrap_count++;

	sess->last_offset = offset;
	offset += (sess->wrap_count * sess->vififo_size);

	return offset;
}

static int esparser_prepare_input(struct amvdec_session *sess, struct vb2_buffer *vb,
				  u32 offset, u32 *payload_size)
{
	struct amvdec_core *core = sess->core;
	struct amvdec_codec_ops *codec_ops = sess->fmt_out->codec_ops;
	struct dma_buf *dbuf = vb->planes[0].dbuf;
	int ret, end_ret;

	if (dbuf) {
		long wait_ret = dma_resv_wait_timeout(dbuf->resv, false, true,
						      msecs_to_jiffies(1000));

		if (wait_ret <= 0)
			return wait_ret ? wait_ret : -ETIMEDOUT;
		ret = dma_buf_begin_cpu_access(dbuf, DMA_BIDIRECTIONAL);
		if (ret)
			return ret;
	}
	ret = codec_ops->inspect_input ? codec_ops->inspect_input(sess, vb, offset) : 0;
	if (ret) {
		ret = ret < 0 ? ret : -EINVAL;
		goto finish;
	}
	/* Input preparation may replace the userspace header or whole payload. */
	*payload_size = amvdec_input_size(vb);
	if (sess->fmt_out->pixfmt == V4L2_PIX_FMT_VP9) {
		*payload_size = vp9_update_header(core, vb);
		if (!*payload_size) {
			ret = -EINVAL;
			goto finish;
		}
	}
	ret = esparser_pad_start_code(core, vb, *payload_size);
finish:
	if (dbuf) {
		end_ret = dma_buf_end_cpu_access(dbuf, DMA_BIDIRECTIONAL);
		if (ret >= 0 && end_ret)
			ret = end_ret;
	}
	return ret;
}

static int
esparser_queue(struct amvdec_session *sess, struct vb2_v4l2_buffer *vbuf)
{
	int ret;
	struct vb2_buffer *vb = &vbuf->vb2_buf;
	struct amvdec_core *core = sess->core;
	struct amvdec_codec_ops *codec_ops = sess->fmt_out->codec_ops;
	u32 payload_size = amvdec_input_size(vb);
	dma_addr_t phy = vb2_dma_contig_plane_dma_addr(vb, 0) + vb->planes[0].data_offset;
	u32 num_dst_bufs = 0;
	u32 offset;
	int pad_size;

	/*
	 * When max ref frame is held by VP9, this should be -= 3 to prevent a
	 * shortage of CAPTURE buffers on the decoder side.
	 * For the future, a good enhancement of the way this is handled could
	 * be to notify new capture buffers to the decoding modules, so that
	 * they could pause when there is no capture buffer available and
	 * resume on this notification.
	 */
	if (sess->fmt_out->pixfmt == V4L2_PIX_FMT_VP9) {
		if (codec_ops->num_pending_bufs)
			num_dst_bufs = codec_ops->num_pending_bufs(sess);

		num_dst_bufs += v4l2_m2m_num_dst_bufs_ready(sess->m2m_ctx);
		if (sess->fmt_out->pixfmt == V4L2_PIX_FMT_VP9)
			num_dst_bufs -= 3;

		if (esparser_vififo_get_free_space(sess) < payload_size ||
		    atomic_read(&sess->esparser_queued_bufs) >= num_dst_bufs)
			return -EAGAIN;
	} else if (sess->fmt_out->pixfmt == V4L2_PIX_FMT_H263 &&
		   esparser_vififo_get_free_space(sess) < SZ_1M) {
		/* Reserve the bounded translated payload before removing its raw AU. */
		return -EAGAIN;
	} else if (esparser_vififo_get_free_space(sess) < payload_size +
		   (sess->fmt_out->pixfmt == V4L2_PIX_FMT_VC1_ANNEX_L ? 14 :
		    sess->fmt_out->pixfmt == V4L2_PIX_FMT_JPEG ?
		    ESPARSER_PAD_SPAN(2) : 0)) {
		return -EAGAIN;
	}

	v4l2_m2m_src_buf_remove_by_buf(sess->m2m_ctx, vbuf);
	offset = esparser_get_offset(sess);
	pad_size = esparser_prepare_input(sess, vb, offset, &payload_size);
	if (pad_size < 0) {
		v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
		amvdec_abort(sess);
		return 0;
	}

	ret = amvdec_add_ts(sess, vb->timestamp, vbuf->timecode, offset, vbuf->flags);
	if (ret) {
		v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
		amvdec_abort(sess);
		return ret;
	}

	dev_dbg(core->dev, "esparser: ts = %llu pld_size = %u offset = %08X flags = %08X\n",
		vb->timestamp, payload_size, offset, vbuf->flags);

	vbuf->flags = 0;
	vbuf->field = V4L2_FIELD_NONE;
	vbuf->sequence = sess->sequence_out++;

	/* Publish before the kick; the decoder can finish during the wait. */
	dma_wmb();
	atomic_inc(&sess->esparser_queued_bufs);
	ret = esparser_write_data(core, phy, payload_size + pad_size);

	if (ret <= 0) {
		dev_warn(core->dev, "esparser: input parsing error\n");
		amvdec_remove_ts(sess, vb->timestamp);
		v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
		/* Consumption is uncertain after a timeout: end this session. */
		amvdec_abort(sess);

		return ret ? : -ETIMEDOUT;
	}
	/* JPEG VLD leaves a trailing pre-fetch window in VIFIFO unless WP advances past EOI. */
	if (sess->fmt_out->pixfmt == V4L2_PIX_FMT_JPEG) {
		static const u8 jpeg_tail[2] = { 0, 0 };

		ret = esparser_queue_eos(core, jpeg_tail, sizeof(jpeg_tail));
		if (ret) {
			amvdec_remove_ts(sess, vb->timestamp);
			v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
			amvdec_abort(sess);
			return ret;
		}
	}

	v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_DONE);

	return 0;
}

/* Feed every m2m OUTPUT buffer waiting in the queue into the ESPARSER.
 * Stops once STOP has closed input, the session has ended, or when the
 * VIFIFO is full (-EAGAIN leaves the buffer queued for the next run). */
void esparser_queue_all(struct amvdec_session *sess)
{
	struct v4l2_m2m_buffer *buf, *n;

	mutex_lock(&sess->lock);
	v4l2_m2m_for_each_src_buf_safe(sess->m2m_ctx, buf, n) {
		if (READ_ONCE(sess->should_stop) || READ_ONCE(sess->stop_requested))
			break;

		/* STREAMOFF of either queue while running ends the session
		 * and frees its VIFIFO, which the ESPARSER still points at;
		 * input queued on the other queue must not be written there.
		 */
		if (sess->status == STATUS_STOPPED)
			break;

		/* For formats without dynamic-resolution events, the
		 * firmware decodes straight into the capture canvases;
		 * don't feed it before those exist.
		 */
		if (!(sess->fmt_out->flags & V4L2_FMT_FLAG_DYN_RESOLUTION) &&
		    !sess->streamon_cap)
			break;

		if (esparser_queue(sess, &buf->vb) < 0)
			break;
	}
	mutex_unlock(&sess->lock);
}

void esparser_queue_all_src(struct work_struct *work)
{
	struct amvdec_session *sess =
		container_of(work, struct amvdec_session, esparser_queue_work);

	esparser_queue_all(sess);
}

int esparser_power_up(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct amvdec_ops *vdec_ops = sess->fmt_out->vdec_ops;

	reset_control_reset(core->esparser_reset);
	amvdec_write_parser(core, PARSER_CONFIG,
			    (10 << PS_CFG_PFIFO_EMPTY_CNT_BIT) |
			    (1  << PS_CFG_MAX_ES_WR_CYCLE_BIT) |
			    (16 << PS_CFG_MAX_FETCH_CYCLE_BIT));

	amvdec_write_parser(core, PFIFO_RD_PTR, 0);
	amvdec_write_parser(core, PFIFO_WR_PTR, 0);

	amvdec_write_parser(core, PARSER_SEARCH_PATTERN,
			    ES_START_CODE_PATTERN);
	amvdec_write_parser(core, PARSER_SEARCH_MASK, ES_START_CODE_MASK);

	amvdec_write_parser(core, PARSER_CONFIG,
			    (10 << PS_CFG_PFIFO_EMPTY_CNT_BIT) |
			    (1  << PS_CFG_MAX_ES_WR_CYCLE_BIT) |
			    (16 << PS_CFG_MAX_FETCH_CYCLE_BIT) |
			    (2  << PS_CFG_STARTCODE_WID_24_BIT));

	amvdec_write_parser(core, PARSER_CONTROL,
			    (ES_SEARCH | ES_PARSER_START));

	amvdec_write_parser(core, PARSER_VIDEO_START_PTR, sess->vififo_paddr);
	amvdec_write_parser(core, PARSER_VIDEO_END_PTR,
			    sess->vififo_paddr + sess->vififo_size - 8);
	amvdec_write_parser(core, PARSER_ES_CONTROL,
			    amvdec_read_parser(core, PARSER_ES_CONTROL) & ~1);

	if (vdec_ops->conf_esparser)
		vdec_ops->conf_esparser(sess);

	amvdec_write_parser(core, PARSER_INT_STATUS, 0xffff);
	amvdec_write_parser(core, PARSER_INT_ENABLE,
			    BIT(PARSER_INT_HOST_EN_BIT));

	return 0;
}

int esparser_init(struct platform_device *pdev, struct amvdec_core *core)
{
	struct device *dev = &pdev->dev;
	int ret;
	int irq;

	irq = platform_get_irq_byname(pdev, "esparser");
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, esparser_isr, IRQF_SHARED,
			       "esparserirq", core);
	if (ret) {
		dev_err(dev, "Failed requesting ESPARSER IRQ\n");
		return ret;
	}

	core->esparser_reset =
		devm_reset_control_get_exclusive(dev, "esparser");
	if (IS_ERR(core->esparser_reset)) {
		dev_err(dev, "Failed to get esparser_reset\n");
		return PTR_ERR(core->esparser_reset);
	}

	return 0;
}
