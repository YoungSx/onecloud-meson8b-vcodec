/* SPDX-License-Identifier: GPL-2.0 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../vdec/meson8b-copy.h"

int main(void)
{
	unsigned char src[4099], saved[4099], dst[4099];
	unsigned int shift, size, i;

	for (i = 0; i < sizeof(src); i++)
		src[i] = (i * 37 + i / 7) & 255;
	memcpy(saved, src, sizeof(src));
	for (shift = 0; shift < 3; shift++) {
		for (size = 0; size <= 4096; size += 8) {
			memset(dst, 0xa5, sizeof(dst));
			assert(!meson8b_copy_swap64(dst + shift, src + shift, size));
			for (i = 0; i < size; i++)
				assert(dst[shift + i] == src[shift + (i & ~7U) + 7 - i % 8]);
			for (i = 0; i < shift; i++)
				assert(dst[i] == 0xa5);
			for (i = shift + size; i < sizeof(dst); i++)
				assert(dst[i] == 0xa5);
			assert(!memcmp(src, saved, sizeof(src)));
		}
	}
	memset(dst, 0xa5, sizeof(dst));
	assert(meson8b_copy_swap64(dst, src, 7));
	for (i = 0; i < sizeof(dst); i++)
		assert(dst[i] == 0xa5);
	return 0;
}
