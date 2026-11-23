// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))
#include "../vdec/h263-vlc.h"

static uint64_t hash(const u8 *data, u32 size)
{
	uint64_t h = 0xcbf29ce484222325ULL;

	for (u32 i = 0; i < size; i++)
		h = (h ^ data[i]) * 0x100000001b3ULL;
	return h;
}

int main(int argc, char **argv)
{
	u8 input[10000], output[20000], saved[20000];
	FILE *file;
	size_t size, second = 0;
	int first_size, second_size;

	assert(argc == 2);
	file = fopen(argv[1], "rb");
	assert(file);
	size = fread(input, 1, sizeof(input), file);
	assert(!ferror(file) && feof(file));
	fclose(file);
	for (size_t i = 3; i + 2 < size; i++)
		if (!input[i] && !input[i + 1] && (input[i + 2] & 0xfc) == 0x80) {
			second = i;
			break;
		}
	assert(second == 5573 && size == 8271);
	first_size = h263_translate(input, second, output, sizeof(output), 128, 96);
	assert(first_size == 9985);
	assert(hash(output, first_size) == 0x37009dd19c403a69ULL);
	memcpy(saved, output, first_size);
	second_size = h263_translate(input + second, size - second, output, sizeof(output), 128, 96);
	assert(second_size == 5757);
	assert(hash(output, second_size) == 0x5de90ea7351d0106ULL);
	/* Repeated and interleaved calls cannot inherit motion predictor state. */
	assert(h263_translate(input, second, output, sizeof(output), 128, 96) == first_size);
	assert(!memcmp(saved, output, first_size));
	assert(h263_translate(input, second, output, 100, 128, 96) == -ENOSPC);
	/* Damaged VLCs must stay within both buffers, regardless of accept/reject. */
	for (u32 i = 7; i < second; i += 31) {
		u8 old = input[i];
		int result;

		input[i] ^= 0xff;
		result = h263_translate(input, second, output, sizeof(output), 128, 96);
		assert(result < 0 || result <= (int)sizeof(output));
		input[i] = old;
	}
	for (u32 n = 0; n < second / 2; n += 17)
		assert(h263_translate(input, n, output, sizeof(output), 128, 96) < 0);
	assert(h263_translate(input, second, output, sizeof(output), 176, 144) == -EINVAL);
	memset(input + 7, 0, second - 7);
	assert(h263_translate(input, second, output, sizeof(output), 128, 96) < 0);
	return 0;
}
