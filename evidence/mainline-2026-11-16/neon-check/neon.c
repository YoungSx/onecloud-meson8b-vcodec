// SPDX-License-Identifier: GPL-2.0+
/* Host build of meson8b-swap-neon.c: same body, system headers. */
#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;

#include <arm_neon.h>

void meson8b_swap64_neon(void *dst, const void *src, size_t size)
{
	const u8 *s = src;
	u8 *d = dst;

	for (; size >= 64; size -= 64, s += 64, d += 64) {
		uint8x16_t a = vld1q_u8(s);
		uint8x16_t b = vld1q_u8(s + 16);
		uint8x16_t c = vld1q_u8(s + 32);
		uint8x16_t e = vld1q_u8(s + 48);

		__builtin_prefetch(s + 512);
		vst1q_u8(d, vrev64q_u8(a));
		vst1q_u8(d + 16, vrev64q_u8(b));
		vst1q_u8(d + 32, vrev64q_u8(c));
		vst1q_u8(d + 48, vrev64q_u8(e));
	}
	for (; size >= 8; size -= 8, s += 8, d += 8)
		vst1_u8(d, vrev64_u8(vld1_u8(s)));
}
