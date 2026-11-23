// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
#include "../vdec/wmv3-header.h"

static void put_le(u8 *data, u32 value)
{
	for (unsigned i = 0; i < 4; i++)
		data[i] = value >> (i * 8);
}

static void first_packet(u8 *data)
{
	memset(data, 0, 80);
	data[3] = 0xc5;
	put_le(data + 4, 4);
	data[8] = 0x40;
	put_le(data + 12, 240);
	put_le(data + 16, 320);
	put_le(data + 20, 12);
	put_le(data + 36, 0x80000003);
	memcpy(data + 44, "abc", 3);
}

int main(void)
{
	u8 data[80], saved[80];
	bool seen = false;

	first_packet(data);
	memcpy(saved, data, sizeof(data));
	assert(wmv3_prepare_packet(data, 47, 54, 320, 240, &seen) == -ENOSPC);
	assert(!seen && !memcmp(data, saved, sizeof(data)));
	assert(wmv3_prepare_packet(data, 47, 80, 640, 240, &seen) == -EINVAL);
	assert(!seen);
	assert(wmv3_prepare_packet(data, 47, 80, 320, 240, &seen) == 55 && seen);
	assert(data[3] == 0x10 && data[8] == 8 && data[22] == 1 && data[23] == 64);
	assert(data[26] == 0x40 && data[33] == 0x0d && data[38] == 3);
	assert(!memcmp(data + 52, "abc", 3));
	memset(data, 0, sizeof(data));
	put_le(data, 3);
	memcpy(data + 8, "xyz", 3);
	assert(wmv3_prepare_packet(data, 11, 80, 320, 240, &seen) == 25);
	assert(data[3] == 0x0d && !memcmp(data + 22, "xyz", 3));
	for (u32 length = 0; length < 44; length++) {
		seen = false;
		first_packet(data);
		assert(wmv3_prepare_packet(data, length, 80, 320, 240, &seen) == -EINVAL);
		assert(!seen);
	}
	seen = false;
	first_packet(data);
	data[8] = 0xc0;
	assert(wmv3_prepare_packet(data, 47, 80, 320, 240, &seen) == -EOPNOTSUPP);
	first_packet(data);
	put_le(data + 36, 0x81000003);
	assert(wmv3_prepare_packet(data, 47, 80, 320, 240, &seen) == -EINVAL);
	return 0;
}
