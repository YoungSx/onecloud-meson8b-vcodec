// SPDX-License-Identifier: GPL-2.0+
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <media/videobuf2-core.h>

#include "vdec_helpers.h"
#include "meson8b-copy.h"

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

int amvdec_meson8b_copy_capture(struct amvdec_session *sess,
				struct vb2_buffer *vb)
{
	struct amvdec_shadow *shadow;
	u8 *src;
	void *dst[2];
	u32 size[2];
	unsigned int i;

	if (vb->index >= ARRAY_SIZE(sess->shadow) || vb->num_planes != 2)
		return -EINVAL;
	shadow = &sess->shadow[vb->index];
	if (!shadow->vaddr)
		return -EINVAL;
	size[0] = shadow->luma;
	size[1] = shadow->luma / 2;
	for (i = 0; i < 2; i++) {
		dst[i] = vb2_plane_vaddr(vb, i);
		if (!dst[i] || vb2_plane_size(vb, i) < size[i])
			return -EINVAL;
	}
	/* Drop possibly cached lines so CPU reads see what VDEC wrote. */
	dma_sync_single_for_cpu(sess->core->dev, shadow->paddr, shadow->size,
				DMA_FROM_DEVICE);
	src = shadow->cached;
	for (i = 0; i < 2; i++) {
		if (meson8b_copy_swap64(dst[i], src, size[i]))
			return -EINVAL;
		src += size[i];
	}
	return 0;
}
