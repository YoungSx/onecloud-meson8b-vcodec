// SPDX-License-Identifier: GPL-2.0+
/*
 * OUTPUT (raw frame) buffer memory. MMAP buffers stay vmalloc and are
 * copied into the staging buffer, so queued frames cost no CMA. DMA-BUF
 * imports are attached to the encoder for DMA: one that maps to a single
 * contiguous range can feed MFDIN directly, any other is read through a
 * CPU mapping into the staging buffer.
 */
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

#include "venc.h"

struct venc_import {
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;		/* while mapped */
	struct dma_buf_map map;		/* CPU mapping, made on first use */
	dma_addr_t dma_addr;
	unsigned long size;
	bool contiguous;
};

static void *venc_in_attach(struct vb2_buffer *vb, struct device *dev,
			    struct dma_buf *dbuf, unsigned long size)
{
	struct venc_import *buf;

	if (dbuf->size < size)
		return ERR_PTR(-EFAULT);
	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);
	buf->size = size;
	buf->attach = dma_buf_attach(dbuf, dev);
	if (IS_ERR(buf->attach)) {
		void *error = buf->attach;

		kfree(buf);
		return error;
	}
	return buf;
}

static void venc_in_detach(void *buf_priv)
{
	struct venc_import *buf = buf_priv;

	dma_buf_detach(buf->attach->dmabuf, buf->attach);
	kfree(buf);
}

static int venc_in_map(void *buf_priv)
{
	struct venc_import *buf = buf_priv;
	struct scatterlist *sg;
	dma_addr_t expected;
	unsigned long contiguous = 0;
	unsigned int i;

	buf->sgt = dma_buf_map_attachment(buf->attach, DMA_TO_DEVICE);
	if (IS_ERR(buf->sgt)) {
		int ret = PTR_ERR(buf->sgt);

		buf->sgt = NULL;
		return ret;
	}
	expected = sg_dma_address(buf->sgt->sgl);
	for_each_sgtable_dma_sg(buf->sgt, sg, i) {
		if (sg_dma_address(sg) != expected)
			break;
		expected += sg_dma_len(sg);
		contiguous += sg_dma_len(sg);
	}
	buf->dma_addr = sg_dma_address(buf->sgt->sgl);
	buf->contiguous = contiguous >= buf->size;
	return 0;
}

static void venc_in_unmap(void *buf_priv)
{
	struct venc_import *buf = buf_priv;

	if (buf->map.vaddr) {
		dma_buf_vunmap(buf->attach->dmabuf, &buf->map);
		dma_buf_map_clear(&buf->map);
	}
	dma_buf_unmap_attachment(buf->attach, buf->sgt, DMA_TO_DEVICE);
	buf->sgt = NULL;
}
static void *venc_in_vaddr(struct vb2_buffer *vb, void *buf_priv)
{
	struct venc_import *buf = buf_priv;

	if (vb->memory != V4L2_MEMORY_DMABUF)
		return vb2_vmalloc_memops.vaddr(vb, buf_priv);
	if (!buf->map.vaddr && dma_buf_vmap(buf->attach->dmabuf, &buf->map))
		return NULL;
	return buf->map.is_iomem ? NULL : buf->map.vaddr;
}

/* MMAP buffers are plain vb2-vmalloc ones. */
static void *venc_in_alloc(struct vb2_buffer *vb, struct device *dev,
			   unsigned long size)
{
	return vb2_vmalloc_memops.alloc(vb, dev, size);
}

static void venc_in_put(void *buf_priv)
{
	vb2_vmalloc_memops.put(buf_priv);
}

static struct dma_buf *venc_in_get_dmabuf(struct vb2_buffer *vb, void *buf_priv,
					  unsigned long flags)
{
	return vb2_vmalloc_memops.get_dmabuf(vb, buf_priv, flags);
}

static int venc_in_mmap(void *buf_priv, struct vm_area_struct *vma)
{
	return vb2_vmalloc_memops.mmap(buf_priv, vma);
}

static unsigned int venc_in_num_users(void *buf_priv)
{
	return vb2_vmalloc_memops.num_users(buf_priv);
}

const struct vb2_mem_ops venc_input_memops = {
	.alloc = venc_in_alloc,
	.put = venc_in_put,
	.get_dmabuf = venc_in_get_dmabuf,
	.mmap = venc_in_mmap,
	.num_users = venc_in_num_users,
	.attach_dmabuf = venc_in_attach,
	.detach_dmabuf = venc_in_detach,
	.map_dmabuf = venc_in_map,
	.unmap_dmabuf = venc_in_unmap,
	.vaddr = venc_in_vaddr,
};

/* Bus address of a DMA-BUF plane mapped as one contiguous range, else 0 */
dma_addr_t venc_input_dma(struct vb2_buffer *vb, unsigned int plane)
{
	struct venc_import *buf = vb->planes[plane].mem_priv;

	if (vb->memory != V4L2_MEMORY_DMABUF || !buf->sgt || !buf->contiguous)
		return 0;
	return buf->dma_addr;
}

/* CPU view of a plane for the copy path; imports are bracketed for access. */
void *venc_input_begin_cpu(struct vb2_buffer *vb, unsigned int plane)
{
	void *vaddr = vb2_plane_vaddr(vb, plane);

	if (vaddr && vb->memory == V4L2_MEMORY_DMABUF &&
	    dma_buf_begin_cpu_access(vb->planes[plane].dbuf, DMA_FROM_DEVICE))
		return NULL;
	return vaddr;
}

void venc_input_end_cpu(struct vb2_buffer *vb, unsigned int plane)
{
	if (vb->memory == V4L2_MEMORY_DMABUF)
		dma_buf_end_cpu_access(vb->planes[plane].dbuf, DMA_FROM_DEVICE);
}
