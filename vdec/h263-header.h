/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef MESON8B_H263_HEADER_H
#define MESON8B_H263_HEADER_H

#define H263_MAX_WIDTH 1920
#define H263_MAX_HEIGHT 1080

struct h263_reader {
	const u8 *data;
	u32 size, bit;
	int error;
};

struct h263_header_state {
	u32 width, height;
	u32 pixel_ratio;
	bool valid, custom_clock;
};

struct h263_picture_header {
	u32 width, height, temporal, rounding, quantizer;
	bool intra;
};

static inline u32 h263_peek(struct h263_reader *r, u32 count)
{
	u32 value = 0, i;

	if (count > 24 || r->size > 0x100000 || r->bit > r->size * 8 ||
	    count > r->size * 8 - r->bit) {
		r->error = -EINVAL;
		return 0;
	}
	for (i = 0; i < count; i++)
		value = value << 1 | ((r->data[(r->bit + i) / 8] >> (7 - (r->bit + i) % 8)) & 1);
	return value;
}

static inline u32 h263_read(struct h263_reader *r, u32 count)
{
	u32 value = h263_peek(r, count);

	if (!r->error)
		r->bit += count;
	return value;
}

/* PLUSPTYPE with I/P, CPFMT and custom clock; optional coding tools stay rejected. */
static inline int h263_parse_picture_header(struct h263_reader *r,
					    struct h263_header_state *state,
					    struct h263_picture_header *picture)
{
	static const u16 widths[] = { 0, 128, 176, 352, 704, 1408 };
	static const u16 heights[] = { 0, 96, 144, 288, 576, 1152 };
	struct h263_header_state next = *state;
	struct h263_picture_header p = {};
	u32 format, ufep = 0, picture_type;

	if (!r->data || r->size < 7 || h263_read(r, 22) != 0x20)
		return -EINVAL;
	p.temporal = h263_read(r, 8);
	if (h263_read(r, 5) != 16)
		return r->error ? r->error : -EINVAL;
	format = h263_read(r, 3);
	if (format == 7) {
		ufep = h263_read(r, 3);
		if (ufep > 1)
			return -EINVAL;
		if (ufep) {
			format = h263_read(r, 3);
			next.custom_clock = h263_read(r, 1);
			/* Ten optional tools, anti-emulation marker and three reserved bits. */
			if (h263_read(r, 14) != 8)
				return r->error ? r->error : -EOPNOTSUPP;
		} else if (!next.valid) {
			return -EINVAL;
		}
		picture_type = h263_read(r, 3);
		if (picture_type > 1)
			return -EOPNOTSUPP;
		p.intra = !picture_type;
		if (h263_read(r, 2))
			return -EOPNOTSUPP;
		p.rounding = h263_read(r, 1);
		if (h263_read(r, 3) != 1 || h263_read(r, 1))
			return -EINVAL;
		if (ufep) {
			if (format == 6) {
				u32 aspect = h263_read(r, 4);

				if (!aspect || (aspect > 5 && aspect != 15))
					return -EINVAL;
				next.pixel_ratio = aspect;
				next.width = (h263_read(r, 9) + 1) * 4;
				if (h263_read(r, 1) != 1)
					return -EINVAL;
				next.height = h263_read(r, 9) * 4;
				if (aspect == 15) {
					u32 numerator = h263_read(r, 8);
					u32 denominator = h263_read(r, 8);

					if (!numerator || !denominator)
						return -EINVAL;
					next.pixel_ratio = numerator << 8 | denominator;
				}
			} else if (format && format < ARRAY_SIZE(widths)) {
				next.width = widths[format];
				next.height = heights[format];
				next.pixel_ratio = 2; /* Standard H.263 source formats use 12:11. */
			} else {
				return -EINVAL;
			}
			if (next.custom_clock) {
				h263_read(r, 1);
				if (!h263_read(r, 7))
					return -EINVAL;
			}
			next.valid = true;
		}
		if (next.custom_clock)
			p.temporal |= h263_read(r, 2) << 8;
		p.quantizer = h263_read(r, 5);
	} else {
		if (!format || format >= ARRAY_SIZE(widths))
			return -EOPNOTSUPP;
		next.width = widths[format];
		next.height = heights[format];
		next.pixel_ratio = 2;
		next.valid = false;
		next.custom_clock = false;
		p.intra = !h263_read(r, 1);
		if (h263_read(r, 4))
			return -EOPNOTSUPP;
		p.quantizer = h263_read(r, 5);
		if (h263_read(r, 1))
			return -EOPNOTSUPP;
	}
	if (r->error || !p.quantizer || next.width < 32 || next.height < 32 ||
	    next.width > H263_MAX_WIDTH || next.height > H263_MAX_HEIGHT)
		return -EINVAL;
	while (h263_read(r, 1) && !r->error)
		h263_read(r, 8);
	if (r->error)
		return r->error;
	p.width = next.width;
	p.height = next.height;
	*state = next;
	*picture = p;
	return 0;
}

#endif
