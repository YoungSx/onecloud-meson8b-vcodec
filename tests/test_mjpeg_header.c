// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
#include "../vdec/mjpeg-header.h"

/* SOF/SOS only: this test exercises metadata, not JPEG entropy decoding. */
static const u8 header[] = {
	0xff, 0xd8,
	0xff, 0xfe, 0x00, 0x04, 0x00, 0x00,
	0xff, 0xc0, 0x00, 0x11, 0x08, 0x00, 0xf0, 0x01, 0x40, 0x03,
	0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01,
	0xff, 0xda, 0x00, 0x0c, 0x03, 0x01, 0x00, 0x02, 0x11,
	0x03, 0x11, 0x00, 0x3f, 0x00, 0x7f, 0xff, 0xd9,
};

int main(void)
{
	u8 data[sizeof(header) + 8], saved[sizeof(data)];
	const u32 sof = 8, sos = 27;

	memset(data, 0xa5, sizeof(data));
	memcpy(data, header, sizeof(header));
	memcpy(saved, data, sizeof(data));
	assert(!mjpeg_validate_header(data, sizeof(header), 320, 240));
	assert(!memcmp(data, saved, sizeof(data)));
	assert(mjpeg_validate_header(NULL, sizeof(header), 320, 240) == -EINVAL);
	assert(mjpeg_validate_header(data, sizeof(header), 640, 240) == -EINVAL);
	assert(mjpeg_validate_header(data, sizeof(header), 320, 480) == -EINVAL);
	assert(mjpeg_validate_header(data, sizeof(header), 0, 240) == -EINVAL);
	for (u32 n = 0; n <= sos + 14; n++) {
		u8 *short_data = malloc(n ? n : 1);

		assert(short_data);
		memcpy(short_data, data, n);
		assert(mjpeg_validate_header(short_data, n, 320, 240) == -EINVAL);
		free(short_data);
	}
	data[sof + 11] = 0x21;
	assert(!mjpeg_validate_header(data, sizeof(header), 320, 240));
	data[sof + 11] = 0x11;
	assert(!mjpeg_validate_header(data, sizeof(header), 320, 240));
	for (u32 sampling = 0; sampling < 256; sampling++) {
		data[sof + 11] = sampling;
		if (sampling != 0x11 && sampling != 0x21 && sampling != 0x22)
			assert(mjpeg_validate_header(data, sizeof(header), 320, 240) == -EOPNOTSUPP);
	}
	memcpy(data, header, sizeof(header));
	data[sof + 14] = 0x12;
	data[sof + 17] = 0x12;
	assert(mjpeg_validate_header(data, sizeof(header), 320, 240) == -EOPNOTSUPP);
	memcpy(data, header, sizeof(header));
	data[sof + 1] = 0xc2;
	assert(mjpeg_validate_header(data, sizeof(header), 320, 240) == -EOPNOTSUPP);
	data[sof + 1] = 0xc1;
	assert(mjpeg_validate_header(data, sizeof(header), 320, 240) == -EOPNOTSUPP);
	data[sof + 1] = 0xc0;
	data[sof + 4] = 12;
	assert(mjpeg_validate_header(data, sizeof(header), 320, 240) == -EOPNOTSUPP);
	memcpy(data, header, sizeof(header));
	data[sof + 13] = data[sof + 10];
	assert(mjpeg_validate_header(data, sizeof(header), 320, 240) == -EINVAL);
	memcpy(data, header, sizeof(header));
	data[sos + 7] = data[sos + 5];
	assert(mjpeg_validate_header(data, sizeof(header), 320, 240) == -EINVAL);
	memcpy(data, header, sizeof(header));
	data[sos + 5] = 0x77;
	assert(mjpeg_validate_header(data, sizeof(header), 320, 240) == -EINVAL);
	memcpy(data, header, sizeof(header));
	data[sos + 6] = 0x40;
	assert(mjpeg_validate_header(data, sizeof(header), 320, 240) == -EINVAL);
	memcpy(data, header, sizeof(header));
	data[sos + 11] = 1;
	assert(mjpeg_validate_header(data, sizeof(header), 320, 240) == -EOPNOTSUPP);
	for (u32 size = 0; size < 65536; size++) {
		memcpy(data, header, sizeof(header));
		data[4] = size >> 8;
		data[5] = size;
		if (size != 4)
			assert(mjpeg_validate_header(data, sizeof(header), 320, 240) == -EINVAL);
	}
	/* Marker fill bytes and TEM have no segment length; every loop advances. */
	memcpy(data, header, 2);
	data[2] = data[3] = 0xff;
	data[4] = 1;
	memcpy(data + 5, header + 2, sizeof(header) - 2);
	assert(!mjpeg_validate_header(data, sizeof(header) + 3, 320, 240));
	return 0;
}
