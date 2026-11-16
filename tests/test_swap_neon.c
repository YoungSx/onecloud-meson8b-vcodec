// SPDX-License-Identifier: GPL-2.0+
/*
 * Pin the real NEON swap body against the scalar one, on a host whose
 * compiler has arm_neon.h. Including the driver source instead of a copy of
 * it is the point: the evidence copy under mainline-2026-11-16/neon-check
 * cannot drift out of agreement with what ships.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "meson8b-swap-neon.c"

int main(void)
{
	uint8_t src[4096 + 64], want[4096 + 64], got[4096 + 64];
	size_t sizes[] = { 0, 8, 16, 56, 64, 72, 128, 4096, 4104 };
	unsigned i, k;

	for (i = 0; i < sizeof(src); i++)
		src[i] = (uint8_t)(i * 37 + i / 7);
	for (k = 0; k < sizeof(sizes) / sizeof(*sizes); k++) {
		size_t n = sizes[k];

		for (i = 0; i < n; i += 8) {
			uint64_t w;

			memcpy(&w, src + i, 8);
			w = __builtin_bswap64(w);
			memcpy(want + i, &w, 8);
		}
		memset(got, 0xa5, sizeof(got));
		meson8b_swap64_neon(got, src, n);
		assert(!memcmp(got, want, n));
	}
	puts("meson8b_swap64_neon matches scalar swap64 for every size");
	return 0;
}
