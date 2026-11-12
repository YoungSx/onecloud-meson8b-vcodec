// SPDX-License-Identifier: GPL-2.0+
#include <linux/dma-mapping.h>
#include <media/videobuf2-core.h>

#include "vdec_helpers.h"
#include "meson8b-copy.h"

void amvdec_meson8b_free_capture(struct amvdec_session *sess)
{
	struct amvdec_shadow *shadow;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sess->shadow); i++) {
		shadow = &sess->shadow[i];
		if (!shadow->vaddr)
			continue;
		dma_free_coherent(sess->core->dev, shadow->size,
				  shadow->vaddr, shadow->paddr);
		memset(shadow, 0, sizeof(*shadow));
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
	if (shadow->vaddr && shadow->luma != luma)
		return -EINVAL;
	if (!shadow->vaddr) {
		shadow->size = luma + luma / 2;
		shadow->vaddr = dma_alloc_coherent(sess->core->dev, shadow->size,
						 &shadow->paddr, GFP_KERNEL);
		if (!shadow->vaddr)
			return -ENOMEM;
		shadow->luma = luma;
	}
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
	dma_rmb();
	src = shadow->vaddr;
	for (i = 0; i < 2; i++) {
		if (meson8b_copy_swap64(dst[i], src, size[i]))
			return -EINVAL;
		src += size[i];
	}
	return 0;
}
