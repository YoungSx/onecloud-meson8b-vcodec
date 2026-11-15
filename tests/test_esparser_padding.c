// SPDX-License-Identifier: GPL-2.0+
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
#include "../vdec/esparser-padding.h"

int main(void)
{
	u8 buffer[8192], copy[8192];
	unsigned long capacities[] = { 0, 64, 4096, 4607 };

	for (unsigned int i = 0; i < sizeof(capacities) / sizeof(capacities[0]); i++) {
		memset(buffer, 0xa5, sizeof(buffer));
		memcpy(copy, buffer, sizeof(buffer));
		assert(esparser_pad_buffer(buffer, 1, capacities[i]) == -ENOSPC);
		assert(memcmp(buffer, copy, sizeof(buffer)) == 0);
	}
	assert(esparser_pad_buffer(buffer, UINT32_MAX, sizeof(buffer)) == -ENOSPC);
	assert(memcmp(buffer, copy, sizeof(buffer)) == 0);
	assert(esparser_pad_buffer(NULL, 100, sizeof(buffer)) == -ENOSPC);
	assert(esparser_pad_buffer(buffer, 3, 4608) == 4093);
	assert(buffer[2] == 0xa5 && buffer[3] == 0 && buffer[4096] == 0);
	assert(buffer[4098] == 1 && buffer[4099] == 0xff);
	assert(buffer[4607] == 0 && buffer[4608] == 0xa5);
	memset(buffer, 0xa5, sizeof(buffer));
	assert(esparser_pad_buffer(buffer, 7680, 8192) == 0);
	assert(buffer[7679] == 0xa5 && buffer[7682] == 1 && buffer[8191] == 0);

	/* The EOS buffer must be allocated with ESPARSER_PAD_SPAN, not
	 * len + 512: a 4-byte sequence-end code is padded up to a 4096-byte
	 * parser packet first, so len + 512 is always too small and the
	 * drain code silently never reached the firmware.
	 */
	assert(ESPARSER_PAD_SPAN(4) == 4608);
	assert(ESPARSER_PAD_SPAN(4096) == 4608);
	assert(ESPARSER_PAD_SPAN(4097) == 4609);
	/* The old len + SEARCH_PATTERN_LEN allocation is too small for every
	 * payload below the 4 KiB parser packet: that is the bug this guards.
	 */
	assert(esparser_pad_buffer(buffer, 4, 4 + 512) == -ENOSPC);
	assert(esparser_pad_buffer(buffer, 44, 44 + 512) == -ENOSPC);
	for (u32 len = 1; len <= 8192; len++) {
		unsigned long span = ESPARSER_PAD_SPAN(len);

		memset(buffer, 0xa5, sizeof(buffer));
		if (span > sizeof(buffer))
			continue;
		assert(esparser_pad_buffer(buffer, len, span) >= 0);
		/* Start code lands after the payload, or after the 4 KiB
		 * minimum packet when the payload is shorter. */
		{
			u32 at = len < 4096 ? 4096 : len;

			assert(buffer[at + 2] == 1 && buffer[at + 3] == 0xff);
		}
		assert(esparser_pad_buffer(buffer, len, span - 1) == -ENOSPC);
	}
	return 0;
}
