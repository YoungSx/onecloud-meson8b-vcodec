/* SPDX-License-Identifier: GPL-2.0+ */
/* Media-time peak-rate envelope; an oversized access unit ends the session. */
#ifndef MESON_VENC_RATE_LIMIT_H
#define MESON_VENC_RATE_LIMIT_H

struct venc_rate_limit {
	u64 peak_bps, capacity_bits, credit_bits, remainder;
	u64 violations;
	u32 numerator, denominator;
	bool active, terminal;
};

static inline void venc_rate_limit_configure(struct venc_rate_limit *limit,
					    u64 peak_bps, u64 cpb_bytes,
					    u32 numerator, u32 denominator,
					    bool restart)
{
	u64 capacity = cpb_bytes * 8;
	bool active = peak_bps && capacity;

	if (restart) {
		limit->terminal = false;
		limit->violations = 0;
	}
	if (restart || !limit->active)
		limit->credit_bits = capacity;
	else if (limit->credit_bits > capacity)
		limit->credit_bits = capacity;
	if (restart || !limit->active || limit->denominator != denominator)
		limit->remainder = 0;
	limit->peak_bps = peak_bps;
	limit->capacity_bits = capacity;
	limit->numerator = numerator;
	limit->denominator = denominator;
	limit->active = active;
}

static inline u64 venc_rate_limit_begin(struct venc_rate_limit *limit)
{
	u64 bits, added;

	if (!limit->active)
		return 0;
	bits = limit->peak_bps * limit->numerator + limit->remainder;
	added = div64_u64(bits, limit->denominator);
	limit->remainder = bits - added * limit->denominator;
	if (added >= limit->capacity_bits - limit->credit_bits)
		limit->credit_bits = limit->capacity_bits;
	else
		limit->credit_bits += added;
	return limit->credit_bits;
}

static inline bool venc_rate_limit_accept(struct venc_rate_limit *limit, u32 bytes)
{
	u64 bits = (u64)bytes * 8;

	if (limit->terminal)
		return false;
	if (!limit->active)
		return true;
	if (bits > limit->credit_bits) {
		limit->terminal = true;
		limit->violations++;
		return false;
	}
	limit->credit_bits -= bits;
	return true;
}

#endif
