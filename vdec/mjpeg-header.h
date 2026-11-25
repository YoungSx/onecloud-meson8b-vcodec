/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef MESON8B_MJPEG_HEADER_H
#define MESON8B_MJPEG_HEADER_H

/* Check the firmware's SOF/SOS contract before it can write a negotiated canvas. */
static inline int mjpeg_validate_header(const u8 *data, u32 length,
					u32 width, u32 height)
{
	u32 pos = 2;
	u8 ids[3] = { 0 };
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
			if (data[pos + 2] != 8 || data[pos + 7] != 3)
				return -EOPNOTSUPP;
			if (((u32)data[pos + 3] << 8 | data[pos + 4]) != height ||
			    ((u32)data[pos + 5] << 8 | data[pos + 6]) != width)
				return -EINVAL;
			/* S805 accepts normalized 4:4:4, 4:2:2 and 4:2:0 sampling. */
			if ((data[pos + 9] != 0x11 && data[pos + 9] != 0x21 &&
			     data[pos + 9] != 0x22) || data[pos + 12] != 0x11 ||
			    data[pos + 15] != 0x11)
				return -EOPNOTSUPP;
			for (i = 0; i < 3; i++) {
				ids[i] = data[pos + 8 + i * 3];
				if (data[pos + 10 + i * 3] > 3 ||
				    (i && ids[i] == ids[0]) || (i == 2 && ids[2] == ids[1]))
					return -EINVAL;
			}
			frame = true;
		} else if (marker >= 0xc1 && marker <= 0xcf && marker != 0xc4) {
			return -EOPNOTSUPP;
		} else if (marker == 0xda) {
			u32 seen = 0;

			if (!frame || size < 6 || size != 6u + 2 * data[pos + 2])
				return -EINVAL;
			if (data[pos + 2] != 3 || data[pos + size - 3] ||
			    data[pos + size - 2] != 63 || data[pos + size - 1])
				return -EOPNOTSUPP;
			for (i = 0; i < 3; i++) {
				u32 j;
				u8 tables = data[pos + 4 + i * 2];

				if ((tables >> 4) > 3 || (tables & 15) > 3)
					return -EINVAL;
				for (j = 0; j < 3 && ids[j] != data[pos + 3 + i * 2]; j++)
					;
				if (j == 3 || (seen & (1u << j)))
					return -EINVAL;
				seen |= 1u << j;
			}
			return length - pos > size ? 0 : -EINVAL;
		}
		pos += size;
	}
	return -EINVAL;
}

#endif
