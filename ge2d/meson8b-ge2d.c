// SPDX-License-Identifier: GPL-2.0
/* S805 canvas GE2D; register protocol from Hardkernel Linux 5aed95d35d25.
 * A byte-plane self-test proves endian conversion before exposing the device.
 */
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/soc/amlogic/meson-canvas.h>
#include <linux/workqueue.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>
#include "../runtime/meson8b-runtime.h"
#include "ge2d-layout.h"
#include "ge2d-scale.h"

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
#define MATRIX_CTRL 0xa8
#define ALU_CTRL 0xb0
#define DP_ONOFF 0xcc
#define SCALE_IDX 0xd0
#define SCALE_COEF 0xd4
#define OUTSIDE_ALPHA 0xd8
#define ANTIFLICK 0xe0
#define GEN_CTRL3 0x120
#define SOFT_RESET BIT(31)
#define BUSY_MASK (BIT(0) | BIT(1) | GENMASK(28, 17))

struct ge2d_ctx;

struct meson8b_ge2d {
	struct device *dev;
	void __iomem *regs;
	struct clk *clk;
	struct reset_control *reset;
	struct meson_canvas *canvas;
	u8 src_canvas, dst_canvas;
	struct v4l2_device v4l2;
	struct video_device *video;
	struct v4l2_m2m_dev *m2m;
	struct mutex lock; /* V4L2 ioctl and vb2 queue serialization. */
	struct work_struct work;
	struct ge2d_ctx *active;
	bool offline;
};

struct ge2d_ctx {
	struct v4l2_fh fh;
	struct meson8b_ge2d *core;
	struct v4l2_pix_format_mplane input, output;
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

/* Source and destination remain held until command completion or reset. */
static int ge2d_blit(struct meson8b_ge2d *core, dma_addr_t src, dma_addr_t dst,
		     u32 width, u32 height, u32 out_width, u32 out_height,
		     bool src_little_endian)
{
	u32 stride, size, out_stride, out_size, status, x = width - 1, y = height - 1;
	u32 hstep, vstep, misc, phase;
	bool preh, prev;
	int ret;

	if (core->offline)
		return -EIO;
	ret = meson8b_ge2d_layout(width, height, &stride, &size);
	if (ret || meson8b_ge2d_layout(out_width, out_height, &out_stride, &out_size) ||
	    meson8b_ge2d_phase(width, out_width, &hstep, &preh) ||
	    meson8b_ge2d_phase(height, out_height, &vstep, &prev))
		return -EINVAL;
	if (ret || ((src | dst) & 7) || src > U32_MAX || dst > U32_MAX ||
	    size - 1 > U32_MAX - src || out_size - 1 > U32_MAX - dst)
		return -EINVAL;
	ret = ge2d_reset(core);
	if (ret)
		return ret;
	ret = meson_canvas_config(core->canvas, core->src_canvas, src, stride,
				  height, MESON_CANVAS_WRAP_NONE,
				  MESON_CANVAS_BLKMODE_LINEAR, 0);
	if (ret)
		return ret;
	ret = meson_canvas_config(core->canvas, core->dst_canvas, dst, out_stride,
				  out_height, MESON_CANVAS_WRAP_NONE,
				  MESON_CANVAS_BLKMODE_LINEAR, 0);
	if (ret)
		return ret;

	ge2d_write(core, GEN_CTRL0, 0);
	ge2d_write(core, GEN_CTRL1, (3 << 22) | (0x3f << 16));
	ge2d_write(core, GEN_CTRL2, BIT(23) | (src_little_endian ? BIT(7) : 0));
	ge2d_write(core, GEN_CTRL3, BIT(0));
	ge2d_write(core, SRC_CANVAS, (u32)core->src_canvas << 24);
	ge2d_write(core, DST_CANVAS, core->dst_canvas);
	ge2d_write(core, SRC_CLIPX, x);
	ge2d_write(core, SRC_CLIPY, y);
	ge2d_write(core, DST_CLIPX, out_width - 1);
	ge2d_write(core, DST_CLIPY, out_height - 1);
	ge2d_write(core, SRC2_CLIPX, out_width - 1);
	ge2d_write(core, SRC2_CLIPY, out_height - 1);
	ge2d_write(core, SRC_X, x);
	/* Vendor command extends the read region; clipping retains the image. */
	ge2d_write(core, SRC_Y, height);
	ge2d_write(core, SRC2_X, out_width - 1);
	ge2d_write(core, SRC2_Y, out_height - 1);
	ge2d_write(core, DST_X, out_width - 1);
	ge2d_write(core, DST_Y, out_height - 1);
	ge2d_write(core, SRC_FMT, 0);
	ge2d_write(core, OUTSIDE_ALPHA, 0);
	ge2d_write(core, MATRIX_CTRL, 0);
	ge2d_write(core, ANTIFLICK, 0);
	ge2d_write(core, DP_ONOFF, 0);
	misc = (4 << 4) | 4;
	misc |= (preh ? BIT(14) : 0) | (prev ? BIT(13) : 0);
	misc |= (width != out_width ? BIT(11) : 0) | (height != out_height ? BIT(12) : 0);
	if (width != out_width || height != out_height) {
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
	ge2d_write(core, HSC_INI, 0);
	ge2d_write(core, VSC_INI, 0);
	ge2d_write(core, HSC_ADV, 0);
	/* Logic COPY for color and alpha; no color matrix or filtering. */
	ge2d_write(core, ALU_CTRL, (5 << 20) | (1 << 12) | (5 << 8) | 1);
	dma_wmb();
	ge2d_write(core, CMD_CTRL, 1);
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

static int ge2d_copy(struct meson8b_ge2d *core, dma_addr_t src, dma_addr_t dst,
		     u32 width, u32 height, bool src_little_endian)
{
	return ge2d_blit(core, src, dst, width, height, width, height, src_little_endian);
}

static int ge2d_selftest(struct meson8b_ge2d *core)
{
	const u32 size = 512 * 32;
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
	for (i = 0; i < size; i++)
		src[i ^ 7] = (i * 37 + i / 512 * 13) & 0xff;
	memset(dst, 0xa5, size);
	ret = ge2d_copy(core, src_dma, dst_dma, 512, 32, false);
	if (!ret)
		for (i = 0; i < size; i++)
			if (dst[i] != ((i * 37 + i / 512 * 13) & 0xff)) {
				ret = -EIO;
				break;
			}
	if (!ret) {
		/* A constant plane must survive horizontal and vertical filtering. */
		memset(src, 0x39, size);
		memset(dst, 0xa5, size);
		ret = ge2d_blit(core, src_dma, dst_dma, 512, 32, 256, 16, true);
		if (!ret)
			for (i = 0; i < 256 * 16; i++)
				if (dst[i] != 0x39) {
					ret = -EIO;
					break;
				}
	}
	dma_free_coherent(core->dev, size, dst, dst_dma);
free_src:
	dma_free_coherent(core->dev, size, src, src_dma);
	return ret;
}

static void ge2d_work(struct work_struct *work)
{
	struct meson8b_ge2d *core = container_of(work, struct meson8b_ge2d, work);
	struct ge2d_ctx *ctx = core->active;
	struct vb2_v4l2_buffer *src = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	struct vb2_v4l2_buffer *dst = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	int ret = -EINVAL;

	if (src && dst) {
		ret = ge2d_blit(core, vb2_dma_contig_plane_dma_addr(&src->vb2_buf, 0),
				vb2_dma_contig_plane_dma_addr(&dst->vb2_buf, 0),
				ctx->input.width, ctx->input.height,
				ctx->output.width, ctx->output.height, true);
		v4l2_m2m_buf_copy_metadata(src, dst, true);
		dst->sequence = ctx->sequence++;
		vb2_set_plane_payload(&dst->vb2_buf, 0,
				      ret ? 0 : ctx->output.plane_fmt[0].sizeimage);
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
	u32 size = V4L2_TYPE_IS_OUTPUT(queue->type) ? ctx->input.plane_fmt[0].sizeimage :
		   ctx->output.plane_fmt[0].sizeimage;

	if (*planes)
		return *planes == 1 && sizes[0] >= size ? 0 : -EINVAL;
	*planes = 1;
	sizes[0] = size;
	return 0;
}

static int ge2d_prepare(struct vb2_buffer *buffer)
{
	struct ge2d_ctx *ctx = vb2_get_drv_priv(buffer->vb2_queue);
	u32 size = V4L2_TYPE_IS_OUTPUT(buffer->type) ? ctx->input.plane_fmt[0].sizeimage :
		   ctx->output.plane_fmt[0].sizeimage;

	if (buffer->planes[0].data_offset || vb2_plane_size(buffer, 0) < size ||
	    (V4L2_TYPE_IS_OUTPUT(buffer->type) &&
	     vb2_get_plane_payload(buffer, 0) < size))
		return -EINVAL;
	return 0;
}

static void ge2d_queue(struct vb2_buffer *buffer)
{
	struct ge2d_ctx *ctx = vb2_get_drv_priv(buffer->vb2_queue);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, to_vb2_v4l2_buffer(buffer));
}

static int ge2d_start(struct vb2_queue *queue, unsigned int count)
{
	struct ge2d_ctx *ctx = vb2_get_drv_priv(queue);

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
	strscpy(cap->card, "S805 GE2D byte planes", sizeof(cap->card));
	strscpy(cap->bus_info, "platform:meson8b-ge2d", sizeof(cap->bus_info));
	return 0;
}

static int ge2d_enum_fmt(struct file *file, void *priv, struct v4l2_fmtdesc *desc)
{
	if (desc->index)
		return -EINVAL;
	desc->pixelformat = V4L2_PIX_FMT_GREY;
	return 0;
}

static int ge2d_try_fmt(struct file *file, void *priv, struct v4l2_format *format)
{
	struct v4l2_pix_format_mplane *pix = &format->fmt.pix_mp;
	u32 stride, size, width, height;

	width = ALIGN(clamp_t(u32, pix->width, 32, MESON8B_GE2D_MAX_WIDTH), 32);
	height = clamp_t(u32, pix->height, 1, MESON8B_GE2D_MAX_HEIGHT);
	memset(pix, 0, sizeof(*pix));
	pix->width = width;
	pix->height = height;
	meson8b_ge2d_layout(pix->width, pix->height, &stride, &size);
	pix->pixelformat = V4L2_PIX_FMT_GREY;
	pix->field = V4L2_FIELD_NONE;
	pix->num_planes = 1;
	pix->colorspace = V4L2_COLORSPACE_RAW;
	pix->plane_fmt[0].bytesperline = stride;
	pix->plane_fmt[0].sizeimage = size;
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
		ctx->output = ctx->input;
	} else {
		u32 step;
		bool pre;

		if (meson8b_ge2d_phase(ctx->input.width, format->fmt.pix_mp.width, &step, &pre) ||
		    meson8b_ge2d_phase(ctx->input.height, format->fmt.pix_mp.height, &step, &pre))
			return -EINVAL;
		ctx->output = format->fmt.pix_mp;
	}
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
	.vidioc_reqbufs = v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf = v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf = v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf = v4l2_m2m_ioctl_dqbuf,
	.vidioc_expbuf = v4l2_m2m_ioctl_expbuf,
	.vidioc_streamon = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff = v4l2_m2m_ioctl_streamoff,
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
	v4l2_fh_init(&ctx->fh, core->video);
	mutex_lock(&core->lock);
	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(core->m2m, ctx, ge2d_queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		mutex_unlock(&core->lock);
		v4l2_fh_exit(&ctx->fh);
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
	ret = clk_prepare_enable(core->clk);
	if (ret)
		goto free_dst;
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
free_dst:
	meson_canvas_free(core->canvas, core->dst_canvas);
free_src:
	meson_canvas_free(core->canvas, core->src_canvas);
	return ret;
}

static int ge2d_remove(struct platform_device *pdev)
{
	struct meson8b_ge2d *core = platform_get_drvdata(pdev);

	video_unregister_device(core->video);
	flush_work(&core->work);
	v4l2_m2m_release(core->m2m);
	v4l2_device_unregister(&core->v4l2);
	reset_control_assert(core->reset);
	clk_disable_unprepare(core->clk);
	meson_canvas_free(core->canvas, core->dst_canvas);
	meson_canvas_free(core->canvas, core->src_canvas);
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
