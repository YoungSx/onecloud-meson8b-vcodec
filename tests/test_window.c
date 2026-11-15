// SPDX-License-Identifier: GPL-2.0+
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
#include "../vdec/meson8b-copy.h"
#include "../vdec/meson8b-window.h"

#define SRC_STRIDE 192
#define DST_STRIDE 128
#define ROWS 12
#define SIZE 2048

static u8 logical[SRC_STRIDE * ROWS], stored[SRC_STRIDE * ROWS];

/* The driver wraps the int-returning helper the same way. */
static void word_copy(void *dst, const void *src, unsigned long size)
{
	assert(!meson8b_copy_swap64(dst, src, size));
}

static void fill(void)
{
	unsigned int i;

	for (i = 0; i < sizeof(logical); i++)
		logical[i] = (i * 41 + i / 5) & 255;
	/* What the canvas holds: every 64-bit word byte-reversed. */
	assert(!meson8b_copy_swap64(stored, logical, sizeof(stored)));
}

static void check(u32 left, u32 width, u32 rows, u32 size)
{
	u8 dst[SIZE];
	u32 row, col;

	memset(dst, 0xa5, sizeof(dst));
	assert(!meson8b_copy_window_plane(dst, stored, SRC_STRIDE, DST_STRIDE,
					  left, width, rows, size,
					  word_copy));
	for (row = 0; row < rows; row++) {
		for (col = 0; col < width; col++)
			assert(dst[row * DST_STRIDE + col] ==
			       logical[row * SRC_STRIDE + left + col]);
		for (col = width; col < DST_STRIDE; col++)
			assert(dst[row * DST_STRIDE + col] == 0);
	}
	for (col = rows * DST_STRIDE; col < size; col++)
		assert(dst[col] == 0);
}

int main(void)
{
	u8 dst[SIZE];

	fill();
	check(0, DST_STRIDE, ROWS, SIZE);
	check(64, 96, ROWS, SIZE);
	check(0, 120, ROWS, SIZE);
	/* Unaligned left offsets take the byte loop. */
	check(6, 96, ROWS, SIZE);
	check(30, 90, ROWS, SIZE);
	check(1, 1, ROWS, SIZE);

	/* Reject windows that would read or write past the plane. */
	memset(dst, 0xa5, sizeof(dst));
	assert(meson8b_copy_window_plane(dst, stored, SRC_STRIDE, DST_STRIDE,
					 136, 64, ROWS, SIZE,
					 word_copy) == -EINVAL);
	assert(meson8b_copy_window_plane(dst, stored, SRC_STRIDE, DST_STRIDE,
					 0, DST_STRIDE + 8, ROWS, SIZE,
					 word_copy) == -EINVAL);
	assert(meson8b_copy_window_plane(dst, stored, SRC_STRIDE, DST_STRIDE,
					 0, DST_STRIDE, ROWS + 1, ROWS * DST_STRIDE,
					 word_copy) == -EINVAL);
	for (unsigned int i = 0; i < sizeof(dst); i++)
		assert(dst[i] == 0xa5);
	return 0;
}
