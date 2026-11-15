/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef MESON8B_WINDOW_H
#define MESON8B_WINDOW_H

/*
 * Copy one plane of the display window out of a reference picture whose
 * rows are @src_stride bytes and whose 64-bit words are byte-reversed.
 * Rows whose window starts on a word boundary take @word_copy, which reverses
 * the bytes of each word; other left offsets are copied byte by byte.
 * Bytes outside the window are zeroed. @left is in bytes on both planes:
 * the reference chroma plane is interleaved, so it shares the luma stride.
 */
static inline int meson8b_copy_window_plane(
	u8 *dst, const u8 *src, u32 src_stride, u32 dst_stride, u32 left,
	u32 width, u32 rows, u32 size,
	void (*word_copy)(void *, const void *, unsigned long))
{
	u32 row, col;

	if (((left + width + 7) & ~7u) > src_stride || width > dst_stride ||
	    rows * dst_stride > size)
		return -EINVAL;

	for (row = 0; row < rows; row++) {
		if (left & 7) {
			for (col = 0; col < width; col++)
				dst[col] = src[(left + col) ^ 7];
		} else {
			word_copy(dst, src + left, (width + 7) & ~7u);
		}
		memset(dst + width, 0, dst_stride - width);
		src += src_stride;
		dst += dst_stride;
	}
	memset(dst, 0, size - rows * dst_stride);
	return 0;
}

#endif
