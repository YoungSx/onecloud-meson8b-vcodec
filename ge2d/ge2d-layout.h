/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MESON8B_GE2D_LAYOUT_H
#define MESON8B_GE2D_LAYOUT_H

#define MESON8B_GE2D_MAX_WIDTH 1920
#define MESON8B_GE2D_MAX_HEIGHT 1088

struct meson8b_ge2d_image {
	u32 width, height, stride;
	dma_addr_t address[2];
	bool nv12, little_endian;
};

struct meson8b_ge2d_crop {
	u32 left, top, width, height;
};

static inline int meson8b_ge2d_layout(u32 width, u32 height, u32 *stride, u32 *size)
{
	if (!width || !height || width > MESON8B_GE2D_MAX_WIDTH ||
	    height > MESON8B_GE2D_MAX_HEIGHT)
		return -EINVAL;
	*stride = (width + 31) & ~31u;
	*size = *stride * height;
	return 0;
}

static inline int meson8b_ge2d_image_valid(const struct meson8b_ge2d_image *image)
{
	u32 stride, size, planes = image->nv12 ? 2 : 1, i;

	if (meson8b_ge2d_layout(image->width, image->height, &stride, &size) ||
	    image->stride < stride || image->stride > 8192 || (image->stride & 31) ||
	    (image->nv12 && ((image->width | image->height) & 1)))
		return -EINVAL;
	for (i = 0; i < planes; i++) {
		u32 bytes = image->stride * (i ? image->height / 2 : image->height);

		if ((image->address[i] & 7) || image->address[i] > U32_MAX ||
		    bytes - 1 > U32_MAX - image->address[i])
			return -EINVAL;
	}
	if (image->nv12) {
		u64 y_end = (u64)image->address[0] + image->stride * image->height;
		u64 uv_end = (u64)image->address[1] + image->stride * image->height / 2;

		if (image->address[0] < uv_end && image->address[1] < y_end)
			return -EINVAL;
	}
	return 0;
}

static inline bool meson8b_ge2d_images_overlap(const struct meson8b_ge2d_image *src,
					       const struct meson8b_ge2d_image *dst)
{
	u32 i, j;

	for (i = 0; i < (src->nv12 ? 2u : 1u); i++)
		for (j = 0; j < (dst->nv12 ? 2u : 1u); j++) {
			u64 src_end = (u64)src->address[i] +
					src->stride * (i ? src->height / 2 : src->height);
			u64 dst_end = (u64)dst->address[j] +
					dst->stride * (j ? dst->height / 2 : dst->height);

			if (src->address[i] < dst_end && dst->address[j] < src_end)
				return true;
		}
	return false;
}

static inline int meson8b_ge2d_crop_valid(const struct meson8b_ge2d_image *image,
					  const struct meson8b_ge2d_crop *crop)
{
	if (!crop->width || !crop->height || crop->left >= image->width ||
	    crop->top >= image->height || crop->width > image->width - crop->left ||
	    crop->height > image->height - crop->top ||
	    (image->nv12 && ((crop->left | crop->top | crop->width | crop->height) & 1)))
		return -EINVAL;
	return 0;
}

#endif
