// SPDX-License-Identifier: GPL-2.0+
/*
 * The byte-order conversion is the only new logic in the Meson8b capture
 * path, so pin it here: the word swap must be its own inverse, and the two
 * planes must come from the right offsets of the canvas block.
 */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* Mirrors swap64() in vdec/meson8b-capture.c. */
static void swap64(void *dst, const void *src, size_t size)
{
	const u64 *s = src;
	u64 *d = dst;

	for (; size >= sizeof(u64); size -= sizeof(u64))
		*d++ = __builtin_bswap64(*s++);
}

/* Mirrors the plane split of amvdec_meson8b_copy_capture(). */
static void convert(u8 *dst_y, u8 *dst_uv, const u8 *block, u32 luma)
{
	swap64(dst_y, block, luma);
	swap64(dst_uv, block + luma, luma / 2);
}

#define LUMA 4096

int main(void)
{
	u8 block[LUMA + LUMA / 2], want[LUMA + LUMA / 2];
	u8 y[LUMA], uv[LUMA / 2], back[LUMA + LUMA / 2];
	u32 i;

	/* Logical picture, as the firmware would have written it. */
	for (i = 0; i < sizeof(block); i++)
		block[i] = (i * 53 + i / 11) & 255;

	/* What the canvas holds: every 64-bit word reversed. */
	for (i = 0; i < sizeof(block); i += 8)
		memcpy(want + i, block + i, 8), swap64(want + i, block + i, 8);

	convert(y, uv, want, LUMA);

	/* Each plane must be the logical picture, plane by plane. */
	assert(!memcmp(y, block, LUMA));
	assert(!memcmp(uv, block + LUMA, LUMA / 2));

	/* And the conversion is its own inverse. */
	memcpy(back, y, LUMA);
	memcpy(back + LUMA, uv, LUMA / 2);
	swap64(back, back, LUMA);
	swap64(back + LUMA, back + LUMA, LUMA / 2);
	assert(!memcmp(back, want, sizeof(back)));

	/* Chroma must not be taken from the luma plane. */
	assert(memcmp(uv, y, LUMA / 2) != 0);

	puts("meson8b capture conversion: ok");
	return 0;
}
