/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef MESON8B_H263_HEADER_H
#define MESON8B_H263_HEADER_H

/* Baseline picture start code and source-format geometry, one AU per buffer. */
static inline int h263_picture_geometry(const u8 *data, u32 length,
					u32 *width, u32 *height)
{
	static const u16 widths[] = { 0, 128, 176, 352, 704 };
	static const u16 heights[] = { 0, 96, 144, 288, 576 };
	u32 format;

	if (!data || length < 7 || data[0] || data[1] || (data[2] & 0xfc) != 0x80 ||
	    (data[3] & 3) != 2)
		return -EINVAL;
	format = (data[4] >> 2) & 7;
	if (!format || format >= ARRAY_SIZE(widths))
		return -EOPNOTSUPP;
	/* Initial port accepts baseline I/P without optional coding tools. */
	if ((data[4] & 0xe1) || (data[5] & 0xe0))
		return -EOPNOTSUPP;
	*width = widths[format];
	*height = heights[format];
	return 0;
}

#endif
