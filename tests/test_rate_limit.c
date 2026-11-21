/* SPDX-License-Identifier: GPL-2.0 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t u32;
typedef uint64_t u64;
#define div64_u64(a,b) ((a)/(b))
#include "../venc/rate-limit.h"

int main(void)
{
	struct venc_rate_limit limit = {0};
	u64 total = 0;

	venc_rate_limit_configure(&limit, 8000000, 128 * 1024, 1, 30, true);
	assert(venc_rate_limit_begin(&limit) == 128 * 1024 * 8);
	assert(venc_rate_limit_accept(&limit, 128 * 1024));
	for (u32 i = 0; i < 30; i++) {
		u64 budget = venc_rate_limit_begin(&limit);

		total += budget;
		limit.credit_bits = 0;
	}
	assert(total == 8000000 && limit.remainder == 20);
	venc_rate_limit_configure(&limit, 4000000, 256 * 1024, 1, 30, false);
	assert(!limit.credit_bits); /* Enlarging the buffer does not grant a burst. */
	assert(venc_rate_limit_begin(&limit) == 133334);
	assert(!venc_rate_limit_accept(&limit, 16667) && limit.terminal && limit.violations == 1);
	assert(!venc_rate_limit_accept(&limit, 0) && limit.violations == 1);
	venc_rate_limit_configure(&limit, 4000000, 256 * 1024, 1001, 30000, true);
	assert(!limit.terminal && !limit.violations);
	limit.credit_bits = 0;
	total = 0;
	for (u32 i = 0; i < 30000; i++) {
		total += venc_rate_limit_begin(&limit);
		limit.credit_bits = 0;
	}
	assert(total == 4000000ULL * 1001 && !limit.remainder);
	limit.credit_bits = 1000000;
	venc_rate_limit_configure(&limit, 4000000, 1024, 1, 30, false);
	assert(limit.credit_bits == 8192);
	venc_rate_limit_configure(&limit, 0, 0, 1, 30, true);
	assert(!limit.active && venc_rate_limit_accept(&limit, UINT32_MAX));
	puts("media-time rate envelope checks passed");
	return 0;
}
