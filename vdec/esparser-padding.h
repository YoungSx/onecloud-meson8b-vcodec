/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef MESON_ESPARSER_PADDING_H
#define MESON_ESPARSER_PADDING_H

/* Bytes a payload occupies once padded: at least one parser packet, plus
 * the search window the ESPARSER scans for the 000001ff start code. */
#define ESPARSER_PAD_SPAN(payload) \
	((payload) < 4096 ? 4096 : (payload)) + 512

/* Validate the full write span before padding any user-queued buffer. */
static inline int esparser_pad_buffer(u8 *data, u32 payload,
				     unsigned long capacity)
{
	u32 padded = payload < 4096 ? 4096 : payload;

	if (!data || padded > capacity || capacity - padded < 512)
		return -ENOSPC;
	memset(data + payload, 0, padded - payload + 512);
	data[padded + 2] = 1;
	data[padded + 3] = 0xff;
	return padded - payload;
}
#endif
