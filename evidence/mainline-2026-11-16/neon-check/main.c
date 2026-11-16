#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
void meson8b_swap64_neon(void *dst, const void *src, size_t size);
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
	puts("neon body matches scalar swap64 for every size");
	return 0;
}
