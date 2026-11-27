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

static void bit_copy(u8 *output, u32 *bit, const u8 *input, u32 start, u32 end)
{
	for (u32 i = start; i < end; i++, (*bit)++)
		output[*bit / 8] |= ((input[i / 8] >> (7 - i % 8)) & 1) << (7 - *bit % 8);
}

static void set_bits(u8 *data, u32 start, u32 count, u32 value)
{
	for (u32 i = 0; i < count; i++) {
		u32 bit = start + i;

		data[bit / 8] &= ~(1 << (7 - bit % 8));
		data[bit / 8] |= ((value >> (count - 1 - i)) & 1) << (7 - bit % 8);
	}
}

static void test_hd(const u8 *header)
{
	u8 input[7000] = { 0 };
	u8 *output = malloc(150000);
	struct h263_context context = { 0 };
	u32 bit = 0, gob_bit = 0;

	assert(output);
	bit_copy(input, &bit, header, 0, 108);
	set_bits(input, 73, 9, 1916 / 4 - 1);
	set_bits(input, 83, 9, 1080 / 4);
	/* 120x68 INTER MBs, no residual and zero MV: exercise the last predictor. */
	for (u32 i = 0; i < 120 * 68; i++, bit += 6) {
		if (i == 120 * 4) {
			gob_bit = bit;
			set_bits(input, bit, 17, 1);
			set_bits(input, bit + 17, 5, 1);
			set_bits(input, bit + 24, 5, 10);
			bit += 29;
		}
		set_bits(input, bit, 6, 31);
	}
	assert(h263_translate(&context, input, (bit + 7) / 8, output, 150000,
			      1916, 1080) == 11 + 120 * 68 * 17);
	assert(output[5] == (1916 >> 8) && output[6] == (1916 & 255));
	assert(output[7] == (1080 >> 8) && output[8] == (1080 & 255));
	for (u32 i = 0; i < 120 * 68; i++) {
		const u8 *mb = output + 11 + i * 17;

		assert(mb[0] == 2 && !mb[1] && !mb[2] && !mb[3] && !mb[4]);
		for (u32 j = 0; j < 6; j++)
			assert(mb[5 + 2 * j] == 0xf0 && !mb[6 + 2 * j]);
	}
	assert(h263_translate(&context, input, (bit + 7) / 8 - 1, output, 150000,
			      1916, 1080) == -EINVAL);
	set_bits(input, gob_bit + 17, 5, 2);
	assert(h263_translate(&context, input, (bit + 7) / 8, output, 150000,
			      1916, 1080) == -EINVAL);
	free(output);
	/* GN=0 GOB headers are forbidden before the first macroblock row. */
	u8 gob0[64] = { 0 }, gob0_out[64];
	u32 gob0_bit = 0;

	bit_copy(gob0, &gob0_bit, header, 0, 108);
	set_bits(gob0, 73, 9, 32 / 4 - 1);
	set_bits(gob0, 83, 9, 32 / 4);
	set_bits(gob0, gob0_bit, 17, 1);
	set_bits(gob0, gob0_bit + 24, 5, 10);
	gob0_bit += 29;
	set_bits(gob0, gob0_bit, 16, 0xffff);
	assert(h263_translate(&context, gob0, (gob0_bit + 16 + 7) / 8,
			      gob0_out, sizeof(gob0_out), 32, 32) == -EINVAL);
}


static void test_extended_aspect(const u8 *picture, u32 size)
{
	u8 input[5000] = { 0 }, output[10000];
	struct h263_context context = { 0 };
	u32 bit = 0;

	bit_copy(input, &bit, picture, 0, 73);
	set_bits(input, 69, 4, 15);
	bit_copy(input, &bit, picture, 73, 92);
	set_bits(input, bit, 16, 0x0b0c);
	bit += 16;
	bit_copy(input, &bit, picture, 92, size * 8);
	assert(h263_translate(&context, input, (bit + 7) / 8, output,
			      sizeof(output), 164, 100) == 7760);
	assert(context.header.pixel_ratio == 0x0b0c);
	assert(hash(output, 7760) == 0x2e68fab1b2d6f38bULL);
	/* An unsuccessful update cannot change either dimensions or PAR. */
	assert(h263_translate(&context, picture, size, output, 100, 164, 100) == -ENOSPC);
	assert(context.header.pixel_ratio == 0x0b0c);
}

static void test_plus(const char *path)
{
	u8 input[5000], output[10000], saved[10000], continuation[1000] = { 0 };
	const u32 start[] = { 0, 3391, 3968, 4558 };
	const int lengths[] = { 7760, 1856, 1962 };
	const uint64_t hashes[] = { 0x2e68fab1b2d6f38bULL, 0xa96eb9aba111d134ULL,
				    0x2acd1edade89f8daULL };
	struct h263_context context = { 0 }, other = { 0 };
	FILE *file = fopen(path, "rb");
	u32 bit = 0;

	assert(file);
	assert(fread(input, 1, sizeof(input), file) == 4558 && feof(file));
	fclose(file);
	for (u32 i = 0; i < 3; i++) {
		int n = h263_translate(&context, input + start[i], start[i + 1] - start[i],
				       output, sizeof(output), 164, 100);

		assert(n == lengths[i] && hash(output, n) == hashes[i]);
		assert(context.header.valid && context.header.custom_clock);
		assert(output[10] == (i == 1));
	}
	assert(h263_translate(&other, input, start[1], output, sizeof(output),
			      164, 100) == lengths[0]);
	assert(hash(output, lengths[0]) == hashes[0]);
	/* Replace frame 1's full PLUS update with UFEP=0, preserving its MB bits. */
	bit_copy(continuation, &bit, input + start[1], 0, 38);
	bit += 3;
	bit_copy(continuation, &bit, input + start[1], 59, 69);
	bit_copy(continuation, &bit, input + start[1], 100, (start[2] - start[1]) * 8);
	assert(h263_translate(&other, continuation, (bit + 7) / 8, output,
			      sizeof(output), 164, 100) == lengths[1]);
	assert(hash(output, lengths[1]) == hashes[1]);
	memcpy(saved, output, lengths[1]);
	/* Failed full updates never establish continuation state, including ENOSPC. */
	memset(&other, 0, sizeof(other));
	assert(h263_translate(&other, input, start[1], output, 100, 164, 100) == -ENOSPC);
	assert(!other.header.valid);
	assert(h263_translate(&other, continuation, (bit + 7) / 8, output,
			      sizeof(output), 164, 100) == -EINVAL);
	assert(h263_translate(&context, input, 14, output, sizeof(output), 164, 100) < 0);
	assert(h263_translate(&context, input, start[1], output,
			      sizeof(output), 176, 100) == -EINVAL);
	assert(h263_translate(&context, continuation, (bit + 7) / 8, output,
			      sizeof(output), 164, 100) == lengths[1]);
	assert(!memcmp(saved, output, lengths[1]));
	test_hd(input + start[1]);
	test_extended_aspect(input, start[1]);
	/* A successful baseline header invalidates the PLUS continuation state. */
	u8 baseline[8] = { 0, 0, 0x80, 2, 4, 4, 0 };
	struct h263_reader reader = { .data = baseline, .size = sizeof(baseline) };
	struct h263_picture_header picture;

	assert(!h263_parse_picture_header(&reader, &context.header, &picture));
	assert(!context.header.valid && !context.header.custom_clock);
}

int main(int argc, char **argv)
{
	u8 input[10000], output[20000], saved[20000];
	FILE *file;
	size_t size, second = 0;
	int first_size, second_size;
	struct h263_context context = { 0 };

	assert(argc == 3);
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
	first_size = h263_translate(&context, input, second, output, sizeof(output), 128, 96);
	assert(first_size == 9985);
	assert(hash(output, first_size) == 0x37009dd19c403a69ULL);
	memcpy(saved, output, first_size);
	second_size = h263_translate(&context, input + second, size - second, output, sizeof(output), 128, 96);
	assert(second_size == 5757);
	assert(hash(output, second_size) == 0x5de90ea7351d0106ULL);
	/* Repeated and interleaved calls cannot inherit motion predictor state. */
	assert(h263_translate(&context, input, second, output, sizeof(output), 128, 96) == first_size);
	assert(!memcmp(saved, output, first_size));
	assert(h263_translate(&context, input, second, output, 100, 128, 96) == -ENOSPC);
	/* Damaged VLCs must stay within both buffers, regardless of accept/reject. */
	for (u32 i = 7; i < second; i += 31) {
		u8 old = input[i];
		int result;

		input[i] ^= 0xff;
		result = h263_translate(&context, input, second, output, sizeof(output), 128, 96);
		assert(result < 0 || result <= (int)sizeof(output));
		input[i] = old;
	}
	for (u32 n = 0; n < second / 2; n += 17)
		assert(h263_translate(&context, input, n, output, sizeof(output), 128, 96) < 0);
	assert(h263_translate(&context, input, second, output, sizeof(output), 176, 144) == -EINVAL);
	memset(input + 7, 0, second - 7);
	assert(h263_translate(&context, input, second, output, sizeof(output), 128, 96) < 0);
	test_plus(argv[2]);
	return 0;
}
