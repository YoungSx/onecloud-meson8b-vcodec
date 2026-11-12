/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef MESON8B_COPY_H
#define MESON8B_COPY_H

/* Match the GX canvas SWAP64 operation without modifying hardware references. */
static inline int meson8b_copy_swap64(void *destination, const void *source,
				    unsigned long size)
{
	unsigned char *dst = destination;
	const unsigned char *src = source;
	unsigned long offset;
	unsigned long long word;

	if (size & 7)
		return -1;
	for (offset = 0; offset < size; offset += 8) {
		__builtin_memcpy(&word, src + offset, 8);
		word = __builtin_bswap64(word);
		__builtin_memcpy(dst + offset, &word, 8);
	}
	return 0;
}

#endif
