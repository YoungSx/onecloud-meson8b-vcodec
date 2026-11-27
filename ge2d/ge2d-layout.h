/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MESON8B_GE2D_LAYOUT_H
#define MESON8B_GE2D_LAYOUT_H

#define MESON8B_GE2D_MAX_WIDTH 1920
#define MESON8B_GE2D_MAX_HEIGHT 1088

enum meson8b_ge2d_format {
	MESON8B_GE2D_FMT_DEFAULT = 0,
	MESON8B_GE2D_FMT_GREY,
	MESON8B_GE2D_FMT_NV12,
	MESON8B_GE2D_FMT_NV21,
	MESON8B_GE2D_FMT_YUV420,
	MESON8B_GE2D_FMT_YVU420,
	MESON8B_GE2D_FMT_UYVY,
	MESON8B_GE2D_FMT_RGB24,
	MESON8B_GE2D_FMT_BGR24,
	MESON8B_GE2D_FMT_XBGR32,
	MESON8B_GE2D_FMT_RGBX32,
	MESON8B_GE2D_FMT_XRGB32,
	MESON8B_GE2D_FMT_ABGR32,
	MESON8B_GE2D_FMT_RGBA32,
	MESON8B_GE2D_FMT_ARGB32,
};

enum meson8b_ge2d_blend {
	MESON8B_GE2D_BLEND_COPY = 0,
	MESON8B_GE2D_BLEND_CONST,
	MESON8B_GE2D_BLEND_OVER,
};

enum meson8b_ge2d_colorspace {
	MESON8B_GE2D_CS_DEFAULT = 0,
	MESON8B_GE2D_CS_BT601_LIM,
	MESON8B_GE2D_CS_BT601_FULL,
	MESON8B_GE2D_CS_BT709_LIM,
	MESON8B_GE2D_CS_BT709_FULL,
};

struct meson8b_ge2d_image {
	u32 width, height, stride;
	dma_addr_t address[3];
	u32 plane_stride[3];
	bool nv12, little_endian;
	enum meson8b_ge2d_format format;
	enum meson8b_ge2d_colorspace colorspace;
};

struct meson8b_ge2d_crop {
	u32 left, top, width, height;
	u32 rotate;
	bool hflip, vflip;
	enum meson8b_ge2d_blend blend;
	u32 global_alpha;
	u32 bg_color;
};

static inline enum meson8b_ge2d_format
meson8b_ge2d_resolved_format(const struct meson8b_ge2d_image *image)
{
	if (image->format != MESON8B_GE2D_FMT_DEFAULT)
		return image->format;
	return image->nv12 ? MESON8B_GE2D_FMT_NV12 : MESON8B_GE2D_FMT_GREY;
}

static inline bool meson8b_ge2d_format_is_420(enum meson8b_ge2d_format fmt)
{
	return fmt == MESON8B_GE2D_FMT_NV12 || fmt == MESON8B_GE2D_FMT_NV21 ||
	       fmt == MESON8B_GE2D_FMT_YUV420 || fmt == MESON8B_GE2D_FMT_YVU420;
}

static inline bool meson8b_ge2d_format_is_semiplanar(enum meson8b_ge2d_format fmt)
{
	return fmt == MESON8B_GE2D_FMT_NV12 || fmt == MESON8B_GE2D_FMT_NV21;
}

static inline bool meson8b_ge2d_format_is_planar3(enum meson8b_ge2d_format fmt)
{
	return fmt == MESON8B_GE2D_FMT_YUV420 || fmt == MESON8B_GE2D_FMT_YVU420;
}

static inline bool meson8b_ge2d_format_is_yuv(enum meson8b_ge2d_format fmt)
{
	return meson8b_ge2d_format_is_420(fmt) || fmt == MESON8B_GE2D_FMT_UYVY;
}

static inline bool meson8b_ge2d_format_is_rgb(enum meson8b_ge2d_format fmt)
{
	return fmt >= MESON8B_GE2D_FMT_RGB24 && fmt <= MESON8B_GE2D_FMT_ARGB32;
}

static inline bool meson8b_ge2d_format_has_alpha(enum meson8b_ge2d_format fmt)
{
	return fmt >= MESON8B_GE2D_FMT_ABGR32 && fmt <= MESON8B_GE2D_FMT_ARGB32;
}

static inline bool meson8b_ge2d_grey_mismatch(enum meson8b_ge2d_format a,
					      enum meson8b_ge2d_format b)
{
	bool a_grey = a == MESON8B_GE2D_FMT_GREY;
	bool b_grey = b == MESON8B_GE2D_FMT_GREY;

	return a_grey != b_grey;
}

static inline u32 meson8b_ge2d_format_bpp(enum meson8b_ge2d_format fmt)
{
	switch (fmt) {
	case MESON8B_GE2D_FMT_UYVY:
		return 2;
	case MESON8B_GE2D_FMT_RGB24:
	case MESON8B_GE2D_FMT_BGR24:
		return 3;
	case MESON8B_GE2D_FMT_XBGR32:
	case MESON8B_GE2D_FMT_RGBX32:
	case MESON8B_GE2D_FMT_XRGB32:
	case MESON8B_GE2D_FMT_ABGR32:
	case MESON8B_GE2D_FMT_RGBA32:
	case MESON8B_GE2D_FMT_ARGB32:
		return 4;
	default:
		return 1;
	}
}

static inline u32 meson8b_ge2d_format_planes(enum meson8b_ge2d_format fmt)
{
	if (meson8b_ge2d_format_is_semiplanar(fmt))
		return 2;
	if (meson8b_ge2d_format_is_planar3(fmt))
		return 3;
	return 1;
}

static inline u32 meson8b_ge2d_plane_stride(const struct meson8b_ge2d_image *image,
					    u32 plane)
{
	enum meson8b_ge2d_format fmt = meson8b_ge2d_resolved_format(image);

	if (plane < 3 && image->plane_stride[plane])
		return image->plane_stride[plane];
	if (plane && meson8b_ge2d_format_is_planar3(fmt))
		return image->stride / 2;
	return image->stride;
}

static inline u32 meson8b_ge2d_plane_rows(const struct meson8b_ge2d_image *image,
					  u32 plane)
{
	enum meson8b_ge2d_format fmt = meson8b_ge2d_resolved_format(image);

	return (plane && meson8b_ge2d_format_is_420(fmt)) ?
		image->height / 2 : image->height;
}

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
	enum meson8b_ge2d_format fmt = meson8b_ge2d_resolved_format(image);
	u32 stride, size, bpp, planes, min_stride, i, j;

	if ((u32)image->format > MESON8B_GE2D_FMT_ARGB32 ||
	    (u32)image->colorspace > MESON8B_GE2D_CS_BT709_FULL)
		return -EINVAL;
	if (meson8b_ge2d_layout(image->width, image->height, &stride, &size))
		return -EINVAL;
	if (meson8b_ge2d_format_is_420(fmt) && ((image->width | image->height) & 1))
		return -EINVAL;
	if (fmt == MESON8B_GE2D_FMT_UYVY && (image->width & 1))
		return -EINVAL;
	bpp = meson8b_ge2d_format_bpp(fmt);
	min_stride = (image->width * bpp + 31) & ~31u;
	if (image->stride < min_stride || image->stride > 8192 || (image->stride & 31))
		return -EINVAL;

	planes = meson8b_ge2d_format_planes(fmt);
	for (i = 0; i < planes; i++) {
		u32 pstride = meson8b_ge2d_plane_stride(image, i);
		u32 rows = meson8b_ge2d_plane_rows(image, i);
		u32 min_pstride = (i && meson8b_ge2d_format_is_planar3(fmt)) ?
				  ((image->width / 2 + 7) & ~7u) : min_stride;
		u32 bytes;

		if (pstride < min_pstride || pstride > 8192 || (pstride & 7))
			return -EINVAL;
		bytes = pstride * rows;
		if ((image->address[i] & 7) || image->address[i] > U32_MAX ||
		    bytes - 1 > U32_MAX - image->address[i])
			return -EINVAL;
	}
	for (i = 0; i < planes; i++) {
		u64 end_i = (u64)image->address[i] +
			    (u64)meson8b_ge2d_plane_stride(image, i) *
			    meson8b_ge2d_plane_rows(image, i);

		for (j = i + 1; j < planes; j++) {
			u64 end_j = (u64)image->address[j] +
				    (u64)meson8b_ge2d_plane_stride(image, j) *
				    meson8b_ge2d_plane_rows(image, j);

			if (image->address[i] < end_j && image->address[j] < end_i)
				return -EINVAL;
		}
	}
	return 0;
}

static inline bool meson8b_ge2d_images_overlap(const struct meson8b_ge2d_image *src,
					       const struct meson8b_ge2d_image *dst)
{
	u32 spl = meson8b_ge2d_format_planes(meson8b_ge2d_resolved_format(src));
	u32 dpl = meson8b_ge2d_format_planes(meson8b_ge2d_resolved_format(dst));
	u32 i, j;

	for (i = 0; i < spl; i++) {
		u64 src_end = (u64)src->address[i] +
			      (u64)meson8b_ge2d_plane_stride(src, i) *
			      meson8b_ge2d_plane_rows(src, i);

		for (j = 0; j < dpl; j++) {
			u64 dst_end = (u64)dst->address[j] +
				      (u64)meson8b_ge2d_plane_stride(dst, j) *
				      meson8b_ge2d_plane_rows(dst, j);

			if (src->address[i] < dst_end && dst->address[j] < src_end)
				return true;
		}
	}
	return false;
}

static inline int meson8b_ge2d_crop_valid(const struct meson8b_ge2d_image *image,
					  const struct meson8b_ge2d_crop *crop)
{
	enum meson8b_ge2d_format fmt = meson8b_ge2d_resolved_format(image);

	if (crop->rotate != 0 && crop->rotate != 90 &&
	    crop->rotate != 180 && crop->rotate != 270)
		return -EINVAL;
	if ((u32)crop->blend > MESON8B_GE2D_BLEND_OVER || crop->global_alpha > 255)
		return -EINVAL;
	if (!crop->width || !crop->height || crop->left >= image->width ||
	    crop->top >= image->height || crop->width > image->width - crop->left ||
	    crop->height > image->height - crop->top)
		return -EINVAL;
	if (meson8b_ge2d_format_is_420(fmt) &&
	    ((crop->left | crop->top | crop->width | crop->height) & 1))
		return -EINVAL;
	if (fmt == MESON8B_GE2D_FMT_UYVY && ((crop->left | crop->width) & 1))
		return -EINVAL;
	return 0;
}

static inline void meson8b_ge2d_target_size(u32 rotate, u32 width, u32 height,
					    u32 *scale_w, u32 *scale_h)
{
	bool swap = rotate == 90 || rotate == 270;

	*scale_w = swap ? height : width;
	*scale_h = swap ? width : height;
}

#endif
