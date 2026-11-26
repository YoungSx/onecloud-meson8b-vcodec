// SPDX-License-Identifier: GPL-2.0+
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include <asm/neon.h>
#include <asm/simd.h>

#include "vdec_helpers.h"
#include "meson8b-copy.h"
#include "meson8b-swap-neon.h"
#include "meson8b-window.h"
#include "../ge2d/ge2d-api.h"

static void amvdec_shadow_free(struct amvdec_session *sess,
			       struct amvdec_shadow *shadow)
{
	if (shadow->cached)
		vunmap(shadow->cached);
	dma_free_coherent(sess->core->dev, shadow->size,
			  shadow->vaddr, shadow->paddr);
	memset(shadow, 0, sizeof(*shadow));
}

/*
 * The VDEC canvas must be physically contiguous, so it lives in a
 * dma_alloc_coherent() block. On this non-coherent SoC that mapping is
 * uncached, which made the CPU byte-order copy read at uncached speed and
 * dominate the whole decode cost. Hardware is the only writer of this
 * buffer; the CPU only reads it. The default NEON path retains a cached
 * read alias pending a HighMem-capable noncoherent allocator. GE2D needs
 * no alias; its CPU fallback reads the coherent mapping directly.
 */
static int amvdec_shadow_alloc(struct amvdec_session *sess,
			       struct amvdec_shadow *shadow, u32 luma)
{
	unsigned int npages = PAGE_ALIGN(luma + luma / 2) >> PAGE_SHIFT;
	struct page **pages;
	unsigned int i;
	struct device *dev = sess->core->dev;

	shadow->size = luma + luma / 2;
	shadow->vaddr = dma_alloc_coherent(dev, shadow->size,
					   &shadow->paddr, GFP_KERNEL);
	if (!shadow->vaddr) {
		/* CMA exhaustion: fail loudly rather than decode into
		 * missing canvases (observed as silent tail-frame loss). */
		dev_err(dev, "shadow canvas alloc of %u bytes failed (CMA pressure?)\n",
			shadow->size);
		return -ENOMEM;
	}
	shadow->luma = luma;
	if (sess->core->ge2d_dev)
		return 0;

	pages = kmalloc_array(npages, sizeof(*pages), GFP_KERNEL);
	if (!pages) {
		amvdec_shadow_free(sess, shadow);
		return -ENOMEM;
	}
	/* dma_alloc_coherent() on this SoC hands back either the direct
	 * map remapped uncached (CMA allocator) or an uncached vmalloc
	 * alias (remap allocator). Recover the backing struct page either
	 * way. */
	for (i = 0; i < npages; i++) {
		void *va = shadow->vaddr + i * PAGE_SIZE;
		pages[i] = is_vmalloc_addr(va) ? vmalloc_to_page(va)
					       : virt_to_page(va);
	}
	shadow->cached = vmap(pages, npages, VM_MAP, PAGE_KERNEL);
	kfree(pages);
	if (!shadow->cached) {
		amvdec_shadow_free(sess, shadow);
		return -ENOMEM;
	}

	return 0;
}

typedef void (*amvdec_swap64_t)(void *, const void *, unsigned long);

/* Reverse the bytes of every 64-bit word while copying; @size % 8 == 0. */
static void amvdec_swap64(void *dst, const void *src, unsigned long size)
{
	meson8b_copy_swap64(dst, src, size);
}

static void amvdec_swap64_neon(void *dst, const void *src, unsigned long size)
{
	meson8b_swap64_neon(dst, src, size);
}

/*
 * Copy the display window out of a larger HEVC reference picture. Rows
 * whose window starts on a 64-bit word take the word copy; other left
 * offsets go byte by byte. Bytes outside the window are zeroed.
 */
static int amvdec_copy_window(struct amvdec_session *sess,
			      const struct amvdec_geometry *geo,
			      const struct amvdec_shadow *shadow,
			      void *dst[2], const u32 size[2], bool simd)
{
	amvdec_swap64_t copy = simd ? amvdec_swap64_neon : amvdec_swap64;
	const u8 *source = shadow->cached ? shadow->cached : shadow->vaddr;
	u32 src_stride = ALIGN(geo->reference_width, 32);
	u32 dst_stride = ALIGN(geo->width, 32);
	u32 left = geo->crop_left, width = geo->width;
	u32 plane;

	if (left + width > geo->reference_width ||
	    geo->crop_top + geo->height > geo->reference_height)
		return -EINVAL;
	for (plane = 0; plane < 2; plane++) {
		u32 rows = plane ? geo->height / 2 : geo->height;
		u32 top = plane ? geo->crop_top / 2 : geo->crop_top;
		const u8 *src = source +
				(plane ? shadow->luma : 0) + top * src_stride;

		/* The chroma plane is interleaved, so it keeps the luma left. */
		if (meson8b_copy_window_plane(dst[plane], src, src_stride,
					      dst_stride, left, width, rows,
					      size[plane], copy))
			return -EINVAL;
	}
	sess->core->cropped_frames++;
	return 0;
}

static int amvdec_meson8b_convert(struct amvdec_session *sess,
				  const struct amvdec_geometry *geo,
				  const struct amvdec_shadow *shadow,
				  struct vb2_buffer *vb)
{
	bool mjpeg = sess->fmt_out->pixfmt == V4L2_PIX_FMT_MJPEG;
	bool crop, simd;
	u8 *src;
	void *dst[2];
	u32 size[2];
	unsigned int i;
	int ret = 0;

	if (vb->num_planes != 2 || !shadow->vaddr)
		return -EINVAL;
	/* Multiples of 64 KiB, so the word copy needs no tail handling. */
	size[0] = amvdec_get_output_size_wh(geo->width, geo->height);
	size[1] = size[0] / 2;
	for (i = 0; i < 2; i++) {
		dst[i] = vb2_plane_vaddr(vb, i);
		if (!dst[i] || vb2_plane_size(vb, i) < size[i])
			return -EINVAL;
	}
	if (shadow->cached)
		dma_sync_single_for_cpu(sess->core->dev, shadow->paddr, shadow->size,
					DMA_FROM_DEVICE);
	src = shadow->cached ? shadow->cached : shadow->vaddr;
	crop = geo->reference_width &&
	       (geo->crop_left || geo->crop_top ||
		ALIGN(geo->reference_width, 32) != ALIGN(geo->width, 32));

	/*
	 * NEON halves the copy (about 15 ms per 1080p picture with the scalar
	 * loop). Callers run in threaded handlers or work items, where
	 * may_use_simd() holds; the scalar loop covers anything else.
	 */
	simd = may_use_simd();
	if (simd)
		kernel_neon_begin();
	if (crop) {
		ret = amvdec_copy_window(sess, geo, shadow, dst, size, simd);
	} else {
		amvdec_swap64_t copy = simd ? amvdec_swap64_neon : amvdec_swap64;

		copy(dst[0], src, size[0]);
		if (!mjpeg)
			copy(dst[1], src + shadow->luma, size[1]);
	}
	if (simd)
		kernel_neon_end();
	if (ret || !mjpeg)
		return ret;

	/* PSCALE writes chroma in 32-byte spans, independent of luma stride. */
	{
		u32 source_stride = ALIGN(geo->width, 64) / 2;
		u32 dest_stride = ALIGN(geo->width, 32);
		u32 row, x;

		if (source_stride * (geo->height / 2) > shadow->luma / 4)
			return -EINVAL;
		memset(dst[1], 0, size[1]);
		for (row = 0; row < geo->height / 2; row++) {
			const u8 *u = src + shadow->luma + row * source_stride;
			const u8 *v = u + shadow->luma / 4;
			u8 *out = dst[1] + row * dest_stride;

			for (x = 0; x < geo->width / 2; x++) {
				out[x * 2] = u[x ^ 7];
				out[x * 2 + 1] = v[x ^ 7];
			}
		}
	}
	return 0;
}

/* Convert one decoded picture into @vb, accounting the CPU time. */
static int amvdec_ge2d_capture(struct amvdec_session *sess,
			     const struct amvdec_geometry *geo,
			     const struct amvdec_shadow *shadow, struct vb2_buffer *vb)
{
	struct amvdec_core *core = sess->core;
	struct amvdec_shadow *stage = &sess->pool.capture_stage;
	bool imported = vb->memory == V4L2_MEMORY_DMABUF;
	struct meson_capture_size capture = meson_capture_resolve(&sess->capture,
							 geo->width, geo->height);
	u32 luma = amvdec_get_output_size_wh(capture.width, capture.height);
	u32 width = geo->reference_width ? geo->reference_width : geo->width;
	u32 stride = ALIGN(width, 32);
	u32 height = geo->reference_height ? geo->reference_height : geo->height;
	struct meson8b_ge2d_image input = {
		.width = width, .height = height, .stride = stride,
		.address = { shadow->paddr, shadow->paddr + shadow->luma }, .nv12 = true,
	};
	struct meson8b_ge2d_image output = {
		.width = capture.width, .height = capture.height,
		.stride = ALIGN(capture.width, 32),
		.nv12 = true, .little_endian = true,
	};
	struct meson8b_ge2d_crop crop = {
		.left = geo->crop_left, .top = geo->crop_top,
		.width = geo->width, .height = geo->height,
	};
	unsigned int i;
	int ret;

	if (sess->fmt_out->pixfmt == V4L2_PIX_FMT_MJPEG)
		return -EOPNOTSUPP;
	if (vb->num_planes != 2 || !shadow->vaddr || shadow->luma < stride * height ||
	    shadow->size < shadow->luma + stride * height / 2 ||
	    (u64)shadow->paddr + shadow->size > (u64)U32_MAX + 1)
		return -EINVAL;
	for (i = 0; i < 2; i++) {
		u32 rows = i ? output.height / 2 : output.height;
		u32 payload = luma / (i ? 2 : 1);

		if (vb->planes[i].data_offset || !vb2_plane_vaddr(vb, i) ||
		    vb2_plane_size(vb, i) < payload || payload < output.stride * rows)
			return -EINVAL;
	}
	/* Delivery holds delivery_lock, so one DMA destination serves every MMAP. */
	if (!imported && stage->luma != luma) {
		if (stage->vaddr) {
			core->pool_bytes -= stage->size;
			amvdec_shadow_free(sess, stage);
		}
		ret = amvdec_shadow_alloc(sess, stage, luma);
		if (ret)
			return ret;
		core->pool_bytes += stage->size;
	}
	for (i = 0; i < 2; i++) {
		u32 rows = i ? output.height / 2 : output.height;
		u32 payload = luma / (i ? 2 : 1);
		void *address = imported ? vb2_plane_vaddr(vb, i) :
			stage->vaddr + (i ? luma : 0);
		u32 row;

		if (vb->planes[i].dbuf) {
			ret = dma_buf_begin_cpu_access(vb->planes[i].dbuf, DMA_BIDIRECTIONAL);
			if (ret)
				return ret;
		}
		/* GE2D writes visible pixels; do not expose a previous image in padding. */
		for (row = 0; row < rows; row++)
			memset(address + row * output.stride + output.width, 0,
			       output.stride - output.width);
		memset(address + output.stride * rows, 0, payload - output.stride * rows);
		if (vb->planes[i].dbuf) {
			ret = dma_buf_end_cpu_access(vb->planes[i].dbuf, DMA_BIDIRECTIONAL);
			if (ret)
				return ret;
		}
		output.address[i] = imported ? vb2_dma_contig_plane_dma_addr(vb, i) :
			stage->paddr + (i ? luma : 0);
	}
	ret = core->ge2d_api->convert(core->ge2d_dev, &input, &output, &crop);
	if (!ret && !imported) {
		memcpy(vb2_plane_vaddr(vb, 0), stage->vaddr, luma);
		memcpy(vb2_plane_vaddr(vb, 1), stage->vaddr + luma, luma / 2);
	}
	return ret;
}

static int amvdec_cpu_capture(struct amvdec_session *sess,
			     const struct amvdec_geometry *geo,
			     const struct amvdec_shadow *shadow, struct vb2_buffer *vb)
{
	unsigned int i, acquired = 0;
	int ret;

	/* A DMA-BUF fallback writes through its CPU mapping after GE2D has stopped. */
	for (i = 0; i < vb->num_planes; i++) {
		if (!vb->planes[i].dbuf)
			continue;
		ret = dma_buf_begin_cpu_access(vb->planes[i].dbuf, DMA_BIDIRECTIONAL);
		if (ret)
			goto finish;
		acquired = i + 1;
	}
	ret = amvdec_meson8b_convert(sess, geo, shadow, vb);
finish:
	for (i = 0; i < acquired; i++)
		if (vb->planes[i].dbuf) {
			int end_ret = dma_buf_end_cpu_access(vb->planes[i].dbuf, DMA_BIDIRECTIONAL);

			if (!ret)
				ret = end_ret;
		}
	return ret;
}

static int amvdec_meson8b_copy_capture(struct amvdec_session *sess,
				       const struct amvdec_geometry *geo,
				       const struct amvdec_shadow *shadow,
				       struct vb2_buffer *vb)
{
	ktime_t start = ktime_get();
	struct meson_capture_size capture = meson_capture_resolve(&sess->capture,
							 geo->width, geo->height);
	bool scaled = capture.width != geo->width || capture.height != geo->height;
	int ret;

	if (sess->core->ge2d_dev) {
		ret = amvdec_ge2d_capture(sess, geo, shadow, vb);
		if (!ret)
			sess->core->ge2d_frames++;
		else if (!scaled)
			ret = amvdec_cpu_capture(sess, geo, shadow, vb);
	} else {
		if (scaled)
			return -EOPNOTSUPP;
		ret = amvdec_meson8b_convert(sess, geo, shadow, vb);
	}

	if (!ret) {
		sess->core->converted_frames++;
		sess->core->convert_ns += ktime_to_ns(ktime_sub(ktime_get(), start));
	}
	return ret;
}

bool amvdec_has_pool(const struct amvdec_session *sess)
{
	return sess->core->platform->revision == VDEC_REVISION_MESON8B;
}

/* Bytes of one luma plane in a slot; the chroma plane follows at half that. */
static u32 amvdec_pool_luma(struct amvdec_session *sess)
{
	/* Each planar MJPEG chroma stride must remain a multiple of 32. */
	if (sess->fmt_out->pixfmt == V4L2_PIX_FMT_MJPEG)
		return ALIGN(ALIGN(sess->width, 64) * ALIGN(sess->height, 32), SZ_64K);
	if (sess->reference_width && sess->reference_height)
		return ALIGN(ALIGN(sess->reference_width, 64) *
			     ALIGN(sess->reference_height, 64), SZ_64K);
	return amvdec_get_output_size(sess);
}

static void amvdec_pool_free_slots(struct amvdec_session *sess)
{
	struct amvdec_frame_pool *pool = &sess->pool;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pool->slot); i++)
		if (pool->slot[i].vaddr)
			amvdec_shadow_free(sess, &pool->slot[i]);
	if (pool->capture_stage.vaddr)
		amvdec_shadow_free(sess, &pool->capture_stage);
	meson8b_pool_reset(&pool->state, 0);
	sess->core->pool_slots = 0;
	sess->core->pool_bytes = 0;
}

static void amvdec_pool_work(struct work_struct *work);

void amvdec_pool_init(struct amvdec_session *sess)
{
	mutex_init(&sess->pool.lock);
	mutex_init(&sess->pool.delivery_lock);
	INIT_WORK(&sess->pool.work, amvdec_pool_work);
}

/*
 * Make @count slots for the current geometry available. Slots of the same
 * geometry are kept with whatever they hold; a new geometry replaces all of
 * them. Returns 1 when every slot is new, 0 when the existing ones were kept.
 */
int amvdec_pool_prepare(struct amvdec_session *sess, u32 count)
{
	struct amvdec_frame_pool *pool = &sess->pool;
	u32 luma = amvdec_pool_luma(sess);
	bool fresh;
	u32 i;
	int ret = 0;

	if (!count || count > MESON8B_POOL_MAX)
		return -EINVAL;

	mutex_lock(&pool->delivery_lock);
	mutex_lock(&pool->lock);
	fresh = !pool->state.count || pool->slot[0].luma != luma;
	if (fresh) {
		amvdec_pool_free_slots(sess);
		pool->end = AMVDEC_POOL_RUNNING;
	} else if (count <= pool->state.count) {
		goto unlock;
	}

	for (i = fresh ? 0 : pool->state.count; i < count; i++) {
		ret = amvdec_shadow_alloc(sess, &pool->slot[i], luma);
		if (ret) {
			dev_err(sess->core->dev,
				"frame pool of %u slots needs %u bytes of CMA each; got %u slots\n",
				count, luma + luma / 2, i);
			amvdec_pool_free_slots(sess);
			goto unlock;
		}
		sess->core->pool_bytes += pool->slot[i].size;
	}

	/* New slots start free; the ones kept hold what they held. */
	pool->state.count = count;
	sess->core->pool_slots = count;
	ret = fresh;
unlock:
	mutex_unlock(&pool->lock);
	mutex_unlock(&pool->delivery_lock);
	return ret;
}

/* For codecs whose firmware owns every slot from the buffer table on. */
void amvdec_pool_give_all(struct amvdec_session *sess)
{
	struct amvdec_frame_pool *pool = &sess->pool;

	mutex_lock(&pool->delivery_lock);
	mutex_lock(&pool->lock);
	if (pool->state.len) {
		/* A table reset lets the firmware overwrite any slot. */
		dev_warn(sess->core->dev,
			 "dropping %u undelivered pictures at a parameter change\n",
			 pool->state.len);
		meson8b_pool_drop(&pool->state);
	}
	meson8b_pool_give_all(&pool->state);
	mutex_unlock(&pool->lock);
	mutex_unlock(&pool->delivery_lock);
}

u32 amvdec_pool_count(struct amvdec_session *sess)
{
	return READ_ONCE(sess->pool.state.count);
}

const struct amvdec_shadow *amvdec_pool_slot(struct amvdec_session *sess,
					     u32 slot)
{
	return &sess->pool.slot[slot];
}

/* A free slot for a picture the driver tracks itself (HEVC), or -EAGAIN. */
int amvdec_pool_take(struct amvdec_session *sess)
{
	int slot;

	mutex_lock(&sess->pool.lock);
	slot = meson8b_pool_take(&sess->pool.state);
	mutex_unlock(&sess->pool.lock);
	return slot < 0 ? -EAGAIN : slot;
}

void amvdec_pool_release(struct amvdec_session *sess, u32 slot)
{
	mutex_lock(&sess->pool.lock);
	if (meson8b_pool_release(&sess->pool.state, slot))
		dev_warn(sess->core->dev, "released unheld pool slot %u\n", slot);
	mutex_unlock(&sess->pool.lock);
}

static void amvdec_pool_end_locked(struct amvdec_session *sess)
{
	struct amvdec_frame_pool *pool = &sess->pool;
	struct vb2_v4l2_buffer *vbuf, *last = NULL;
	unsigned int i;

	switch (pool->end) {
	case AMVDEC_POOL_RUNNING:
		return;
	case AMVDEC_POOL_FLUSH:
		while ((vbuf = v4l2_m2m_dst_buf_remove(sess->m2m_ctx))) {
			for (i = 0; i < vbuf->vb2_buf.num_planes; i++)
				vb2_set_plane_payload(&vbuf->vb2_buf, i, 0);
			vbuf->field = V4L2_FIELD_NONE;
			if (last)
				v4l2_m2m_buf_done(last, VB2_BUF_STATE_DONE);
			last = vbuf;
		}
		if (!last)
			return;
		last->flags = V4L2_BUF_FLAG_LAST;
		container_of(last, struct amvdec_vb2_buffer, vb)->eos = true;
		v4l2_m2m_buf_done(last, VB2_BUF_STATE_DONE);
		break;
	case AMVDEC_POOL_LAST:
	case AMVDEC_POOL_RESTART:
		vbuf = v4l2_m2m_dst_buf_remove(sess->m2m_ctx);
		if (!vbuf)
			return;
		for (i = 0; i < vbuf->vb2_buf.num_planes; i++)
			vb2_set_plane_payload(&vbuf->vb2_buf, i, 0);
		container_of(vbuf, struct amvdec_vb2_buffer, vb)->eos =
			pool->end == AMVDEC_POOL_LAST;
		if (pool->end == AMVDEC_POOL_RESTART) {
			vbuf->flags = V4L2_BUF_FLAG_LAST;
			vbuf->field = V4L2_FIELD_NONE;
			sess->sequence_cap = 0;
			v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_DONE);
		} else {
			sess->should_stop = 1;
			v4l2_m2m_last_buffer_done(sess->m2m_ctx, vbuf);
		}
		break;
	}
	pool->end = AMVDEC_POOL_RUNNING;
}

/* Convert waiting pictures into queued CAPTURE buffers, oldest first. */
static void amvdec_pool_deliver_locked(struct amvdec_session *sess)
{
	struct amvdec_codec_ops *codec_ops = sess->fmt_out->codec_ops;
	struct amvdec_frame_pool *pool = &sess->pool;
	int slot;

	while ((slot = meson8b_pool_peek(&pool->state)) >= 0) {
		struct amvdec_picture *pic = &pool->picture[slot];
		struct vb2_v4l2_buffer *vbuf;
		struct meson_capture_size capture = meson_capture_resolve(&sess->capture,
								 pic->geo.width, pic->geo.height);
		u32 luma = amvdec_get_output_size_wh(capture.width, capture.height);
		int ret;

		vbuf = v4l2_m2m_dst_buf_remove(sess->m2m_ctx);
		if (!vbuf)
			return;

		/* Keep the slot queued until copying ends; IRQs may queue other slots. */
		mutex_unlock(&pool->lock);
		ret = amvdec_meson8b_copy_capture(sess, &pic->geo, &pool->slot[slot],
						  &vbuf->vb2_buf);
		mutex_lock(&pool->lock);
		meson8b_pool_pop(&pool->state);
		/* The picture is out of the slot; the firmware may reuse it. */
		if (codec_ops->recycle)
			amvdec_recycle(sess, slot);
		if (ret) {
			/* One picture that does not fit its buffer is no reason
			 * to end a session that is otherwise decoding. */
			dev_warn(sess->core->dev,
				 "dropping %ux%u picture: %d\n",
				 pic->geo.width, pic->geo.height, ret);
			vb2_set_plane_payload(&vbuf->vb2_buf, 0, 0);
			vb2_set_plane_payload(&vbuf->vb2_buf, 1, 0);
			v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
			continue;
		}

		vb2_set_plane_payload(&vbuf->vb2_buf, 0, luma);
		vb2_set_plane_payload(&vbuf->vb2_buf, 1, luma / 2);
		vbuf->vb2_buf.timestamp = pic->timestamp;
		vbuf->sequence = sess->sequence_cap++;
		vbuf->flags = pic->flags;
		vbuf->timecode = pic->timecode;
		vbuf->field = pic->field;
		if (pic->restart)
			sess->sequence_cap = 0;
		container_of(vbuf, struct amvdec_vb2_buffer, vb)->eos = pic->eos;
		v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_DONE);
	}

	amvdec_pool_end_locked(sess);
}

/* The picture in @slot is finished; deliver it now or when a buffer comes. */
void amvdec_pool_push(struct amvdec_session *sess, u32 slot,
		      const struct amvdec_picture *pic)
{
	struct amvdec_frame_pool *pool = &sess->pool;

	mutex_lock(&pool->lock);
	if (meson8b_pool_push(&pool->state, slot)) {
		mutex_unlock(&pool->lock);
		dev_err(sess->core->dev, "picture in unheld pool slot %u\n", slot);
		amvdec_abort(sess);
		return;
	}
	pool->picture[slot] = *pic;
	sess->core->pool_waiting_peak = max(sess->core->pool_waiting_peak,
					    pool->state.len);
	mutex_unlock(&pool->lock);
	/* Let the IRQ acknowledge firmware while conversion runs separately. */
	schedule_work(&pool->work);
}

static void amvdec_pool_work(struct work_struct *work)
{
	struct amvdec_session *sess =
		container_of(work, struct amvdec_session, pool.work);
	struct amvdec_codec_ops *codec_ops = sess->fmt_out->codec_ops;

	if (!READ_ONCE(sess->streamon_cap))
		return;
	mutex_lock(&sess->pool.delivery_lock);
	mutex_lock(&sess->pool.lock);
	amvdec_pool_deliver_locked(sess);
	mutex_unlock(&sess->pool.lock);
	mutex_unlock(&sess->pool.delivery_lock);

	/* Slots may be free again: let a codec waiting for one continue. */
	if (READ_ONCE(sess->streamon_cap) && codec_ops->capture_queued)
		codec_ops->capture_queued(sess);
}

/* Userspace queued a CAPTURE buffer. Copying takes milliseconds, so it runs
 * outside the ioctl, which holds the lock of both queues.
 */
void amvdec_pool_capture_queued(struct amvdec_session *sess)
{
	schedule_work(&sess->pool.work);
}

/* End the CAPTURE stream as @end describes, after every waiting picture. */
void amvdec_pool_end(struct amvdec_session *sess, enum amvdec_pool_end end)
{
	mutex_lock(&sess->pool.lock);
	sess->pool.end = end;
	mutex_unlock(&sess->pool.lock);
	schedule_work(&sess->pool.work);
}

/*
 * STREAMOFF of the CAPTURE queue discards what was not delivered and hands
 * back the queued CAPTURE buffers. A delivery takes its buffer off the
 * queue and copies into it under the pool lock, so returning the rest under
 * the same lock leaves no copy running into a buffer that vb2 frees next.
 */
void amvdec_pool_capture_stop(struct amvdec_session *sess)
{
	struct vb2_v4l2_buffer *buf;

	cancel_work_sync(&sess->pool.work);
	mutex_lock(&sess->pool.delivery_lock);
	mutex_lock(&sess->pool.lock);
	meson8b_pool_drop(&sess->pool.state);
	sess->pool.end = AMVDEC_POOL_RUNNING;
	while ((buf = v4l2_m2m_dst_buf_remove(sess->m2m_ctx)))
		v4l2_m2m_buf_done(buf, VB2_BUF_STATE_ERROR);
	mutex_unlock(&sess->pool.lock);
	mutex_unlock(&sess->pool.delivery_lock);
}

void amvdec_pool_free(struct amvdec_session *sess)
{
	cancel_work_sync(&sess->pool.work);
	mutex_lock(&sess->pool.delivery_lock);
	mutex_lock(&sess->pool.lock);
	amvdec_pool_free_slots(sess);
	sess->pool.end = AMVDEC_POOL_RUNNING;
	mutex_unlock(&sess->pool.lock);
	mutex_unlock(&sess->pool.delivery_lock);
}
