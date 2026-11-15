// SPDX-License-Identifier: GPL-2.0+
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <media/videobuf2-core.h>

#include <asm/neon.h>
#include <asm/simd.h>

#include "vdec_helpers.h"
#include "meson8b-copy.h"
#include "meson8b-swap-neon.h"
#include "meson8b-window.h"

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
 * buffer; the CPU only reads it. Exported-API alternative: keep the
 * coherent block for the canvas and read through a cached vmap() alias,
 * invalidating before each read (dma_sync_single_for_cpu). Safe because
 * the CPU never writes through the alias, so no dirty lines can be lost.
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

void amvdec_meson8b_free_capture(struct amvdec_session *sess)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sess->shadow); i++) {
		if (sess->shadow[i].vaddr)
			amvdec_shadow_free(sess, &sess->shadow[i]);
	}
}

int amvdec_meson8b_capture_addr(struct amvdec_session *sess,
				struct vb2_buffer *vb, dma_addr_t addr[2])
{
	struct amvdec_shadow *shadow;
	u32 luma = amvdec_get_output_size(sess);
	if (sess->reference_width && sess->reference_height)
		luma = ALIGN(ALIGN(sess->reference_width, 64) *
			     ALIGN(sess->reference_height, 64), SZ_64K);

	if (vb->index >= ARRAY_SIZE(sess->shadow) ||
	    sess->pixfmt_cap != V4L2_PIX_FMT_NV12M)
		return -EINVAL;
	shadow = &sess->shadow[vb->index];
	if (shadow->vaddr && shadow->luma != luma) {
		/* Resolution changed mid-session: the shadow canvas is
		 * sized for the old format. Reallocate for the new one
		 * rather than failing the resume.
		 */
		amvdec_shadow_free(sess, shadow);
	}
	if (!shadow->vaddr && amvdec_shadow_alloc(sess, shadow, luma))
		return -ENOMEM;
	addr[0] = shadow->paddr;
	addr[1] = addr[0] + luma;
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
			      const struct amvdec_shadow *shadow,
			      void *dst[2], const u32 size[2], bool simd)
{
	amvdec_swap64_t copy = simd ? amvdec_swap64_neon : amvdec_swap64;
	u32 src_stride = ALIGN(sess->reference_width, 32);
	u32 dst_stride = ALIGN(sess->width, 32);
	u32 left = sess->crop_left, width = sess->width;
	u32 plane;

	if (left + width > sess->reference_width ||
	    sess->crop_top + sess->height > sess->reference_height)
		return -EINVAL;
	for (plane = 0; plane < 2; plane++) {
		u32 rows = plane ? sess->height / 2 : sess->height;
		u32 top = plane ? sess->crop_top / 2 : sess->crop_top;
		const u8 *src = (const u8 *)shadow->cached +
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
				  struct vb2_buffer *vb)
{
	bool mjpeg = sess->fmt_out->pixfmt == V4L2_PIX_FMT_MJPEG;
	struct amvdec_shadow *shadow;
	bool crop, simd;
	u8 *src;
	void *dst[2];
	u32 size[2];
	unsigned int i;
	int ret = 0;

	if (vb->index >= ARRAY_SIZE(sess->shadow) || vb->num_planes != 2)
		return -EINVAL;
	shadow = &sess->shadow[vb->index];
	if (!shadow->vaddr)
		return -EINVAL;
	/* Multiples of 64 KiB, so the word copy needs no tail handling. */
	size[0] = amvdec_get_output_size(sess);
	size[1] = size[0] / 2;
	for (i = 0; i < 2; i++) {
		dst[i] = vb2_plane_vaddr(vb, i);
		if (!dst[i] || vb2_plane_size(vb, i) < size[i])
			return -EINVAL;
	}
	/* Drop possibly cached lines so CPU reads see what VDEC wrote. */
	dma_sync_single_for_cpu(sess->core->dev, shadow->paddr, shadow->size,
				DMA_FROM_DEVICE);
	src = shadow->cached;
	crop = sess->reference_width &&
	       (sess->crop_left || sess->crop_top ||
		ALIGN(sess->reference_width, 32) != ALIGN(sess->width, 32));

	/*
	 * NEON halves the copy (about 15 ms per 1080p picture with the scalar
	 * loop). Callers run in threaded handlers or work items, where
	 * may_use_simd() holds; the scalar loop covers anything else.
	 */
	simd = may_use_simd();
	if (simd)
		kernel_neon_begin();
	if (crop) {
		ret = amvdec_copy_window(sess, shadow, dst, size, simd);
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

	/* MJPEG writes planar U then V (luma/4 each): interleave them
	 * into the NV12M UV plane.
	 */
	src += shadow->luma;
	for (i = 0; i < size[1] / 2; i += 8) {
		u64 u, v;
		u8 *out = dst[1];
		int j;

		if (meson8b_copy_swap64(&u, src + i, 8) ||
		    meson8b_copy_swap64(&v, src + size[1] / 2 + i, 8))
			return -EINVAL;
		for (j = 0; j < 8; j++) {
			out[i * 2 + j * 2] = ((u8 *)&u)[j];
			out[i * 2 + j * 2 + 1] = ((u8 *)&v)[j];
		}
	}
	return 0;
}

/* Convert one decoded picture into @vb, accounting the CPU time. */
int amvdec_meson8b_copy_capture(struct amvdec_session *sess,
				struct vb2_buffer *vb)
{
	ktime_t start = ktime_get();
	int ret = amvdec_meson8b_convert(sess, vb);

	if (!ret) {
		sess->core->converted_frames++;
		sess->core->convert_ns += ktime_to_ns(ktime_sub(ktime_get(), start));
	}
	return ret;
}
