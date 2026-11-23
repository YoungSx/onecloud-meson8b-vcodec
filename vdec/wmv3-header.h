/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef MESON8B_WMV3_HEADER_H
#define MESON8B_WMV3_HEADER_H

static inline u32 wmv3_le32(const u8 *data)
{
	return data[0] | (u32)data[1] << 8 | (u32)data[2] << 16 | (u32)data[3] << 24;
}

/* Amlogic WMV3 frame/sequence prefix, documented by libamcodec 4eebd67b1428. */
static inline void wmv3_prefix(u8 *header, u32 code, u32 size)
{
	u32 i, sum = 0;

	memset(header, 0, 22);
	header[2] = 1;
	header[3] = code;
	header[5] = size >> 16;
	header[6] = 0x88;
	header[7] = size >> 8;
	header[8] = size;
	header[9] = 0x88;
	header[10] = 0xff;
	header[11] = 0xff;
	header[12] = 0x88;
	header[13] = 0xff;
	header[14] = 0xff;
	header[15] = 0x88;
	for (i = 4; i < 16; i++)
		sum += header[i];
	header[16] = sum >> 8;
	header[17] = sum;
	header[18] = 0x88;
	header[19] = sum >> 8;
	header[20] = sum;
	header[21] = 0x88;
}

/* Annex L: first buffer contains the 36-byte sequence and one complete frame;
 * subsequent buffers each contain an 8-byte frame header and its picture.
 */
static inline int wmv3_prepare_packet(u8 *data, u32 length, u32 capacity,
				      u32 width, u32 height, bool *sequence_seen)
{
	u8 header[52];
	u32 offset = *sequence_seen ? 0 : 36;
	u32 prefix = *sequence_seen ? 22 : 52;
	u32 frame_size, flags;

	if (!data || length > capacity || length < offset + 8)
		return -EINVAL;
	if (!*sequence_seen) {
		if (data[3] != 0xc5 || wmv3_le32(data + 4) != 4 ||
		    wmv3_le32(data + 20) != 12 ||
		    wmv3_le32(data + 12) != height || wmv3_le32(data + 16) != width ||
		    !width || !height || width > 1920 || height > 1080)
			return -EINVAL;
		if ((data[8] >> 6) > 1)
			return -EOPNOTSUPP;
		wmv3_prefix(header, 0x10, 8);
		header[22] = width >> 8;
		header[23] = width;
		header[24] = height >> 8;
		header[25] = height;
		memcpy(header + 26, data + 8, 4);
	}
	flags = wmv3_le32(data + offset);
	frame_size = flags & 0xffffff;
	if (!frame_size || (flags & 0x7f000000) ||
	    frame_size != length - offset - 8)
		return -EINVAL;
	if (capacity < prefix || frame_size > capacity - prefix)
		return -ENOSPC;
	wmv3_prefix(header + prefix - 22, 0x0d, frame_size);
	memmove(data + prefix, data + offset + 8, frame_size);
	memcpy(data, header, prefix);
	*sequence_seen = true;
	return prefix + frame_size;
}

#endif
