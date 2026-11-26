// SPDX-License-Identifier: GPL-2.0+
/* Cached MMAP pictures; imported DMA-BUFs retain contiguous DMA mappings. */
#include <linux/slab.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-vmalloc.h>

#include "vdec_helpers.h"

struct amvdec_capture_mem {
	const struct vb2_mem_ops *ops;
	void *priv;
};

static void *capture_alloc(struct vb2_buffer *vb, struct device *dev,
			   unsigned long size)
{
	struct amvdec_capture_mem *mem = kmalloc(sizeof(*mem), GFP_KERNEL);

	if (!mem)
		return ERR_PTR(-ENOMEM);
	mem->ops = &vb2_vmalloc_memops;
	mem->priv = mem->ops->alloc(vb, dev, size);
	if (IS_ERR(mem->priv)) {
		void *error = mem->priv;

		kfree(mem);
		return error;
	}
	return mem;
}

static void capture_put(void *priv)
{
	struct amvdec_capture_mem *mem = priv;

	mem->ops->put(mem->priv);
	kfree(mem);
}

static void *capture_attach(struct vb2_buffer *vb, struct device *dev,
			    struct dma_buf *dbuf, unsigned long size)
{
	struct amvdec_capture_mem *mem = kmalloc(sizeof(*mem), GFP_KERNEL);

	if (!mem)
		return ERR_PTR(-ENOMEM);
	mem->ops = &vb2_dma_contig_memops;
	mem->priv = mem->ops->attach_dmabuf(vb, dev, dbuf, size);
	if (IS_ERR(mem->priv)) {
		void *error = mem->priv;

		kfree(mem);
		return error;
	}
	return mem;
}

static void capture_detach(void *priv)
{
	struct amvdec_capture_mem *mem = priv;

	mem->ops->detach_dmabuf(mem->priv);
	kfree(mem);
}

static int capture_map(void *priv)
{
	struct amvdec_capture_mem *mem = priv;

	return mem->ops->map_dmabuf(mem->priv);
}

static void capture_unmap(void *priv)
{
	struct amvdec_capture_mem *mem = priv;

	mem->ops->unmap_dmabuf(mem->priv);
}

static void capture_prepare(void *priv)
{
	struct amvdec_capture_mem *mem = priv;

	if (mem->ops->prepare)
		mem->ops->prepare(mem->priv);
}

static void capture_finish(void *priv)
{
	struct amvdec_capture_mem *mem = priv;

	if (mem->ops->finish)
		mem->ops->finish(mem->priv);
}

static void *capture_vaddr(struct vb2_buffer *vb, void *priv)
{
	struct amvdec_capture_mem *mem = priv;

	return mem->ops->vaddr(vb, mem->priv);
}

static void *capture_cookie(struct vb2_buffer *vb, void *priv)
{
	struct amvdec_capture_mem *mem = priv;

	return mem->ops->cookie ? mem->ops->cookie(vb, mem->priv) : NULL;
}

static unsigned int capture_num_users(void *priv)
{
	struct amvdec_capture_mem *mem = priv;

	return mem->ops->num_users(mem->priv);
}

static int capture_mmap(void *priv, struct vm_area_struct *vma)
{
	struct amvdec_capture_mem *mem = priv;

	return mem->ops->mmap(mem->priv, vma);
}

static struct dma_buf *capture_get_dmabuf(struct vb2_buffer *vb, void *priv,
					  unsigned long flags)
{
	struct amvdec_capture_mem *mem = priv;

	return mem->ops->get_dmabuf(vb, mem->priv, flags);
}

const struct vb2_mem_ops amvdec_capture_memops = {
	.alloc = capture_alloc,
	.put = capture_put,
	.attach_dmabuf = capture_attach,
	.detach_dmabuf = capture_detach,
	.map_dmabuf = capture_map,
	.unmap_dmabuf = capture_unmap,
	.prepare = capture_prepare,
	.finish = capture_finish,
	.vaddr = capture_vaddr,
	.cookie = capture_cookie,
	.num_users = capture_num_users,
	.mmap = capture_mmap,
	.get_dmabuf = capture_get_dmabuf,
};
