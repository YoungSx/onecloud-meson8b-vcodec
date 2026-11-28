/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef MESON8B_JPEG_HEADER_H
#define MESON8B_JPEG_HEADER_H

/* Validate standalone baseline JPEG SOF0/SOS layout for jpegdec_mc. */
static inline int jpeg_validate_header(const u8 *data, u32 length,
				       u32 width, u32 height, u8 *comp_out)
{
	u32 pos = 2;
	u8 ids[3] = { 0 };
	u8 comp_num = 0;
	bool frame = false;

	if (!data || !width || !height || length < 4 ||
	    data[0] != 0xff || data[1] != 0xd8)
		return -EINVAL;
	while (pos < length) {
		u32 size, i;
		u8 marker;

		if (data[pos++] != 0xff)
			return -EINVAL;
		while (pos < length && data[pos] == 0xff)
			pos++;
		if (pos == length)
			return -EINVAL;
		marker = data[pos++];
		if (marker == 0x01)
			continue;
		if (!marker || marker == 0xd8 || marker == 0xd9 ||
		    (marker >= 0xd0 && marker <= 0xd7) || length - pos < 2)
			return -EINVAL;
		size = (u32)data[pos] << 8 | data[pos + 1];
		if (size < 2 || size > length - pos)
			return -EINVAL;
		if (marker == 0xc0) {
			if (frame || size < 8 || size != 8u + 3 * data[pos + 7])
				return -EINVAL;
			comp_num = data[pos + 7];
			if (data[pos + 2] != 8 ||
			    (comp_num != 1 && comp_num != 3))
				return -EOPNOTSUPP;
			if (((u32)data[pos + 3] << 8 | data[pos + 4]) != height ||
			    ((u32)data[pos + 5] << 8 | data[pos + 6]) != width)
				return -EINVAL;
			if (comp_num == 1) {
				if (data[pos + 9] != 0x11)
					return -EOPNOTSUPP;
			} else {
				u8 yh = data[pos + 9] >> 4;
				u8 yv = data[pos + 9] & 0x0f;

				if (!yh || yh > 2 || !yv || yv > 2 ||
				    data[pos + 12] != 0x11 ||
				    data[pos + 15] != 0x11)
					return -EOPNOTSUPP;
			}
			for (i = 0; i < comp_num; i++) {
				ids[i] = data[pos + 8 + i * 3];
				if (data[pos + 10 + i * 3] > 3 ||
				    (i && ids[i] == ids[0]) ||
				    (i == 2 && ids[2] == ids[1]))
					return -EINVAL;
			}
			frame = true;
		} else if (marker >= 0xc1 && marker <= 0xcf && marker != 0xc4) {
			return -EOPNOTSUPP;
		} else if (marker == 0xda) {
			u32 seen = 0;

			if (!frame || size < 6 || size != 6u + 2 * data[pos + 2])
				return -EINVAL;
			if (data[pos + 2] != comp_num ||
			    data[pos + size - 3] ||
			    data[pos + size - 2] != 63 ||
			    data[pos + size - 1])
				return -EOPNOTSUPP;
			for (i = 0; i < comp_num; i++) {
				u32 j;
				u8 tables = data[pos + 4 + i * 2];

				if ((tables >> 4) > 3 || (tables & 15) > 3)
					return -EINVAL;
				for (j = 0; j < comp_num && ids[j] != data[pos + 3 + i * 2]; j++)
					;
				if (j == comp_num || (seen & (1u << j)))
					return -EINVAL;
				seen |= 1u << j;
			}
			if (length - pos <= size)
				return -EINVAL;
			if (comp_out)
				*comp_out = comp_num;
			return 0;
		}
		pos += size;
	}
	return -EINVAL;
}

static inline u32 jpeg_pscale_filt0_ratio(u32 in_dim, u32 out_dim, u32 *round1)
{
	static const u8 filt0_table[9] = { 0, 0, 1, 1, 2, 2, 2, 2, 2 };
	u32 r, r0, r1;

	if (!out_dim || out_dim > in_dim) {
		if (round1)
			*round1 = 0;
		return 0;
	}
	r = min_t(u32, in_dim / out_dim, 8u);
	r0 = filt0_table[r];
	r1 = filt0_table[min_t(u32, (in_dim / out_dim) >> (2 + r0), 8u)];
	if (round1)
		*round1 = r1;
	return r0;
}

static inline u32 jpeg_pscale_step(u32 in_dim, u32 out_dim, u32 r0, u32 r1)
{
	u32 shift = 16 - r0 - r1;

	if (!out_dim)
		return 0x10000;
	return (in_dim << shift) / out_dim;
}

#endif
