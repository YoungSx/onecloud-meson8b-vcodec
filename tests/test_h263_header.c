// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#include "../vdec/h263-header.h"

int main(void)
{
	u8 header[] = { 0, 0, 0x80, 2, 8, 4, 0x1e };
	u32 width, height;
	const u32 widths[] = { 0, 128, 176, 352, 704 };
	const u32 heights[] = { 0, 96, 144, 288, 576 };

	assert(h263_picture_geometry(NULL, 7, &width, &height) == -EINVAL);
	for (u32 size = 0; size < sizeof(header); size++)
		assert(h263_picture_geometry(header, size, &width, &height) == -EINVAL);
	for (u32 format = 1; format <= 4; format++) {
		header[4] = format << 2;
		assert(!h263_picture_geometry(header, sizeof(header), &width, &height));
		assert(width == widths[format] && height == heights[format]);
	}
	header[4] = 7 << 2;
	assert(h263_picture_geometry(header, sizeof(header), &width, &height) == -EOPNOTSUPP);
	header[4] = 8;
	header[2] = 0xfc;
	assert(h263_picture_geometry(header, sizeof(header), &width, &height) == -EINVAL);
	header[2] = 0x80;
	for (u32 tool = 0x20; tool <= 0x80; tool <<= 1) {
		header[5] = tool;
		assert(h263_picture_geometry(header, sizeof(header), &width, &height) == -EOPNOTSUPP);
	}
	header[5] = 4;
	header[3] = 0;
	assert(h263_picture_geometry(header, sizeof(header), &width, &height) == -EINVAL);
	return 0;
}
