// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#include "../vdec/h263-header.h"

static void put(u8 *data, u32 *bit, u32 count, u32 value)
{
	while (count--) {
		data[*bit / 8] |= ((value >> count) & 1) << (7 - *bit % 8);
		(*bit)++;
	}
}

static u32 plus(u8 *data, u32 ufep, u32 width, u32 height, bool clock,
		 u32 type, u32 aspect)
{
	u32 bit = 0;

	memset(data, 0, 64);
	put(data, &bit, 22, 0x20);
	put(data, &bit, 8, 1);
	put(data, &bit, 5, 16);
	put(data, &bit, 3, 7);
	put(data, &bit, 3, ufep);
	if (ufep) {
		put(data, &bit, 3, 6);
		put(data, &bit, 1, clock);
		put(data, &bit, 14, 8);
	}
	put(data, &bit, 3, type);
	put(data, &bit, 2, 0);
	put(data, &bit, 1, 1);
	put(data, &bit, 3, 1);
	put(data, &bit, 1, 0);
	if (ufep) {
		put(data, &bit, 4, aspect);
		put(data, &bit, 9, width / 4 - 1);
		put(data, &bit, 1, 1);
		put(data, &bit, 9, height / 4);
		if (aspect == 15) {
			put(data, &bit, 8, 11);
			put(data, &bit, 8, 12);
		}
		if (clock) {
			put(data, &bit, 1, 0);
			put(data, &bit, 7, 72);
		}
	}
	if (clock)
		put(data, &bit, 2, 3);
	put(data, &bit, 5, 10);
	put(data, &bit, 1, 0);
	return bit;
}

static int parse(const u8 *data, u32 size, struct h263_header_state *state,
		 struct h263_picture_header *picture, u32 bits)
{
	struct h263_reader r = { .data = data, .size = size };
	int ret = h263_parse_picture_header(&r, state, picture);

	if (!ret && bits)
		assert(r.bit == bits);
	return ret;
}

int main(void)
{
	u8 header[] = { 0, 0, 0x80, 2, 8, 4, 0x1e }, data[64];
	struct h263_header_state state = { 0 }, empty = { 0 };
	struct h263_picture_header picture;
	const u32 widths[] = { 0, 128, 176, 352, 704 };
	const u32 heights[] = { 0, 96, 144, 288, 576 };
	u32 bits;

	assert(parse(NULL, 7, &state, &picture, 0) == -EINVAL);
	for (u32 size = 0; size < sizeof(header); size++)
		assert(parse(header, size, &state, &picture, 0) == -EINVAL);
	for (u32 format = 1; format <= 4; format++) {
		header[4] = format << 2;
		assert(!parse(header, sizeof(header), &state, &picture, 50));
		assert(picture.width == widths[format] && picture.height == heights[format]);
		assert(!state.valid && state.pixel_ratio == 2);
	}
	header[2] = 0xfc;
	assert(parse(header, sizeof(header), &state, &picture, 0) == -EINVAL);
	header[2] = 0x80;
	for (u32 tool = 0x20; tool <= 0x80; tool <<= 1) {
		header[5] = tool;
		assert(parse(header, sizeof(header), &state, &picture, 0) == -EOPNOTSUPP);
	}
	bits = plus(data, 1, 164, 100, true, 0, 15);
	assert(!parse(data, sizeof(data), &state, &picture, bits));
	assert(state.valid && state.custom_clock && picture.intra && state.pixel_ratio == 0x0b0c);
	assert(picture.width == 164 && picture.height == 100 && picture.temporal == 769);
	assert(picture.rounding == 1 && picture.quantizer == 10);
	bits = plus(data, 0, 0, 0, true, 1, 1);
	assert(parse(data, sizeof(data), &empty, &picture, 0) == -EINVAL);
	assert(!parse(data, sizeof(data), &state, &picture, bits));
	assert(!picture.intra && picture.width == 164 && picture.height == 100);
	assert(state.pixel_ratio == 0x0b0c);
	/* Incomplete updates must preserve an already valid continuation state. */
	bits = plus(data, 1, 1920, 1080, false, 1, 1);
	for (u32 size = 0; size < (bits + 7) / 8; size++) {
		assert(parse(data, size, &state, &picture, 0) < 0);
		assert(state.width == 164 && state.height == 100 && state.custom_clock);
		assert(state.pixel_ratio == 0x0b0c);
	}
	assert(!parse(data, sizeof(data), &state, &picture, bits));
	assert(!state.custom_clock);
	for (u32 type = 2; type < 8; type++) {
		plus(data, 1, 164, 100, false, type, 1);
		assert(parse(data, sizeof(data), &state, &picture, 0) == -EOPNOTSUPP);
	}
	/* Optional tools, reserved UFEP, CPFMT marker/aspect and zero clock divisor. */
	for (u32 tool = 45; tool < 55; tool++) {
		plus(data, 1, 164, 100, false, 0, 1);
		data[tool / 8] ^= 1 << (7 - tool % 8);
		assert(parse(data, sizeof(data), &state, &picture, 0) == -EOPNOTSUPP);
	}
	plus(data, 2, 164, 100, false, 0, 1);
	assert(parse(data, sizeof(data), &state, &picture, 0) == -EINVAL);
	plus(data, 1, 164, 100, false, 0, 0);
	assert(parse(data, sizeof(data), &state, &picture, 0) == -EINVAL);
	plus(data, 1, 164, 100, false, 0, 1);
	data[82 / 8] ^= 1 << (7 - 82 % 8);
	assert(parse(data, sizeof(data), &state, &picture, 0) == -EINVAL);
	plus(data, 1, 164, 100, true, 0, 1);
	for (u32 i = 93; i < 100; i++)
		data[i / 8] &= ~(1 << (7 - i % 8));
	assert(parse(data, sizeof(data), &state, &picture, 0) == -EINVAL);
	plus(data, 1, 1924, 1080, false, 0, 1);
	assert(parse(data, sizeof(data), &state, &picture, 0) == -EINVAL);
	plus(data, 1, 32, 28, false, 0, 1);
	assert(parse(data, sizeof(data), &state, &picture, 0) == -EINVAL);
	bits = plus(data, 1, 32, 32, false, 0, 1);
	assert(!parse(data, sizeof(data), &state, &picture, bits));
	return 0;
}
