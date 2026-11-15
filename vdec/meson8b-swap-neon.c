// SPDX-License-Identifier: GPL-2.0+
/*
 * NEON body of the Meson8b output conversion: reverse the bytes of every
 * 64-bit word, 64 bytes per step with a read-ahead hint. Built with NEON
 * flags; callers bracket it with kernel_neon_begin()/kernel_neon_end().
 */
#include "meson8b-swap-neon.h"

/* Last, as Documentation/arm/kernel_mode_neon.rst requires */
#include <arm_neon.h>

void meson8b_swap64_neon(void *destination, const void *source,
			 unsigned long size)
{
	const uint8_t *src = source;
	uint8_t *dst = destination;

	for (; size >= 64; size -= 64, src += 64, dst += 64) {
		uint8x16_t a = vld1q_u8(src), b = vld1q_u8(src + 16);
		uint8x16_t c = vld1q_u8(src + 32), d = vld1q_u8(src + 48);

		__builtin_prefetch(src + 512);
		vst1q_u8(dst, vrev64q_u8(a));
		vst1q_u8(dst + 16, vrev64q_u8(b));
		vst1q_u8(dst + 32, vrev64q_u8(c));
		vst1q_u8(dst + 48, vrev64q_u8(d));
	}
	for (; size >= 8; size -= 8, src += 8, dst += 8)
		vst1_u8(dst, vrev64_u8(vld1_u8(src)));
}
