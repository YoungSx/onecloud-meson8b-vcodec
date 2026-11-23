/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MESON8B_GE2D_LAYOUT_H
#define MESON8B_GE2D_LAYOUT_H

#define MESON8B_GE2D_MAX_WIDTH 1920
#define MESON8B_GE2D_MAX_HEIGHT 1088

static inline int meson8b_ge2d_layout(u32 width, u32 height, u32 *stride, u32 *size)
{
	if (!width || !height || width > MESON8B_GE2D_MAX_WIDTH ||
	    height > MESON8B_GE2D_MAX_HEIGHT)
		return -EINVAL;
	*stride = (width + 31) & ~31u;
	*size = *stride * height;
	return 0;
}

#endif
