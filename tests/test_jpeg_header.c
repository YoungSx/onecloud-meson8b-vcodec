// SPDX-License-Identifier: GPL-2.0+
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define min_t(type, a, b) ((type)(a) < (type)(b) ? (type)(a) : (type)(b))
#include "../vdec/jpeg-header.h"

static u32 build_jpeg(u8 *buf, u16 w, u16 h, u8 comp_num, u8 sof_marker, u8 yh_yv)
{
	u32 pos = 0;

	buf[pos++] = 0xff;
	buf[pos++] = 0xd8;
	buf[pos++] = 0xff;
	buf[pos++] = sof_marker;
	u16 sof_len = 8 + 3 * comp_num;
	buf[pos++] = sof_len >> 8;
	buf[pos++] = sof_len & 0xff;
	buf[pos++] = 8;
	buf[pos++] = h >> 8;
	buf[pos++] = h & 0xff;
	buf[pos++] = w >> 8;
	buf[pos++] = w & 0xff;
	buf[pos++] = comp_num;
	for (u8 i = 0; i < comp_num; i++) {
		buf[pos++] = i + 1;
		buf[pos++] = i == 0 ? yh_yv : 0x11;
		buf[pos++] = i == 0 ? 0 : 1;
	}
	buf[pos++] = 0xff;
	buf[pos++] = 0xda;
	u16 sos_len = 6 + 2 * comp_num;
	buf[pos++] = sos_len >> 8;
	buf[pos++] = sos_len & 0xff;
	buf[pos++] = comp_num;
	for (u8 i = 0; i < comp_num; i++) {
		buf[pos++] = i + 1;
		buf[pos++] = i == 0 ? 0x00 : 0x11;
	}
	buf[pos++] = 0;
	buf[pos++] = 63;
	buf[pos++] = 0;
	buf[pos++] = 0x12;
	buf[pos++] = 0xff;
	buf[pos++] = 0xd9;
	return pos;
}

int main(void)
{
	u8 buf[128], comp = 0;
	u32 len, r0, r1;

	len = build_jpeg(buf, 640, 480, 3, 0xc0, 0x22);
	assert(!jpeg_validate_header(buf, len, 640, 480, &comp) && comp == 3);
	assert(!jpeg_validate_header(buf, len, 640, 480, NULL));
	assert(jpeg_validate_header(buf, len, 320, 240, &comp) == -EINVAL);

	len = build_jpeg(buf, 640, 480, 1, 0xc0, 0x11);
	assert(!jpeg_validate_header(buf, len, 640, 480, &comp) && comp == 1);

	len = build_jpeg(buf, 640, 480, 3, 0xc0, 0x21);
	assert(!jpeg_validate_header(buf, len, 640, 480, &comp));
	len = build_jpeg(buf, 640, 480, 3, 0xc0, 0x11);
	assert(!jpeg_validate_header(buf, len, 640, 480, &comp));

	len = build_jpeg(buf, 640, 480, 3, 0xc2, 0x22);
	assert(jpeg_validate_header(buf, len, 640, 480, &comp) == -EOPNOTSUPP);

	len = build_jpeg(buf, 640, 480, 3, 0xc0, 0x41);
	assert(jpeg_validate_header(buf, len, 640, 480, &comp) == -EOPNOTSUPP);

	assert(jpeg_validate_header(buf, 12, 640, 480, &comp) == -EINVAL);

	r0 = jpeg_pscale_filt0_ratio(1920, 1920, &r1);
	assert(r0 == 0 && r1 == 0 && jpeg_pscale_step(1920, 1920, r0, r1) == 0x10000);
	r0 = jpeg_pscale_filt0_ratio(1920, 960, &r1);
	assert(r0 == 1 && r1 == 0 && jpeg_pscale_step(1920, 960, r0, r1) == 0x10000);
	r0 = jpeg_pscale_filt0_ratio(1920, 480, &r1);
	assert(r0 == 2 && r1 == 0 && jpeg_pscale_step(1920, 480, r0, r1) == 0x10000);

	puts("JPEG standalone header and PSCALE ratio checks passed");
	return 0;
}
