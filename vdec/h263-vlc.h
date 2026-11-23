/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MESON8B_H263_VLC_H
#define MESON8B_H263_VLC_H
#include "h263-header.h"
#include "h263-tables.h"

struct h263_reader {
	const u8 *data;
	u32 size, bit;
	int error;
};

struct h263_writer {
	u8 *data;
	u32 size, used;
	int error;
};

static inline u32 h263_peek(struct h263_reader *r, u32 count)
{
	u32 value = 0, i;

	if (count > 24 || r->bit > r->size * 8 || count > r->size * 8 - r->bit) {
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

static inline void h263_byte(struct h263_writer *w, u32 value)
{
	if (w->used == w->size) {
		w->error = -ENOSPC;
		return;
	}
	w->data[w->used++] = value;
}

static inline int h263_vlc(struct h263_reader *r, const struct h263_vlc *table,
			   u32 count)
{
	u32 bits, value = 0, i;

	for (bits = 1; bits <= 13 && !r->error; bits++) {
		value = value << 1 | h263_read(r, 1);
		for (i = 0; i < count; i++)
			if (table[i].bits == bits && table[i].code == value)
				return i;
	}
	r->error = -EINVAL;
	return -EINVAL;
}

static inline int h263_mcbpc(struct h263_reader *r, bool intra)
{
	u32 value = 0, bits, i, count = intra ? 9 : 28;
	const u8 *codes = intra ? h263_intra_code : h263_inter_code;
	const u8 *lengths = intra ? h263_intra_bits : h263_inter_bits;

	for (bits = 1; bits <= 13 && !r->error; bits++) {
		value = value << 1 | h263_read(r, 1);
		for (i = 0; i < count; i++)
			if (lengths[i] == bits && codes[i] == value)
				return i;
	}
	return -EINVAL;
}

static inline int h263_mv(struct h263_reader *r, int predictor)
{
	int code = h263_vlc(r, h263_motion, ARRAY_SIZE(h263_motion));
	int value;

	if (code < 0)
		return 0;
	if (code && h263_read(r, 1))
		code = -code;
	value = predictor + code;
	return value > 31 ? value - 64 : value < -32 ? value + 64 : value;
}

static inline int h263_median(int a, int b, int c)
{
	if (b == 999) {
		b = a;
		c = a;
	}
	return a + b + c - max(a, max(b, c)) - min(a, min(b, c));
}

static inline int h263_block(struct h263_reader *r, struct h263_writer *w,
			     bool intra, bool coded, int quant)
{
	u32 position = 0;

	if (intra) {
		u32 dc = h263_read(r, 8);

		if (dc == 128 || !dc)
			return -EINVAL;
		dc = (dc == 255 ? 128 : dc) * 8;
		h263_byte(w, dc >> 8);
		h263_byte(w, dc);
		position = 1;
	}
	if (coded) {
		for (;;) {
			int code = h263_vlc(r, h263_coeff, ARRAY_SIZE(h263_coeff));
			u32 last, run, level;
			int signed_level, reconstructed;

			if (code < 0)
				return -EINVAL;
			if (code == 102) {
				last = h263_read(r, 1);
				run = h263_read(r, 6);
				level = h263_read(r, 8);
				if (!level || level == 128)
					return -EINVAL;
				signed_level = level & 128 ? (int)level - 256 : (int)level;
			} else {
				last = code >= 58;
				run = h263_run[code];
				level = h263_level[code];
				signed_level = h263_read(r, 1) ? -(int)level : (int)level;
			}
			position += run + 1;
			if (r->error || position > 64)
				return -EINVAL;
			reconstructed = quant * (2 * abs(signed_level) + 1) - !(quant & 1);
			if (signed_level < 0)
				reconstructed = -reconstructed;
			if (run >= 15) {
				h263_byte(w, 0xf0);
				h263_byte(w, run);
				h263_byte(w, ((u32)reconstructed >> 8) & 15);
			} else {
				h263_byte(w, run << 4 | (((u32)reconstructed >> 8) & 15));
			}
			h263_byte(w, reconstructed);
			if (w->error)
				return w->error;
			if (last)
				break;
		}
	}
	h263_byte(w, 0xf0);
	h263_byte(w, 0);
	return r->error ? r->error : w->error;
}

/* Baseline I/P -> S805 h263_mc macroblock stream; no shared prediction state. */
static inline int h263_translate(const u8 *input, u32 length, u8 *output, u32 capacity,
				 u32 expected_width, u32 expected_height)
{
	struct h263_reader r = { .data = input, .size = length };
	struct h263_writer w = { .data = output, .size = capacity };
	int top[90][2], left[2];
	u32 width, height, x, y, component, temporal;
	bool picture_intra;
	int quant, ret;

	if (!output || length > 0x100000)
		return -EINVAL;
	ret = h263_picture_geometry(input, length, &width, &height);
	if (ret || width != expected_width || height != expected_height)
		return ret ? ret : -EINVAL;
	h263_read(&r, 22);
	temporal = h263_read(&r, 8);
	h263_read(&r, 8);
	picture_intra = !h263_read(&r, 1);
	h263_read(&r, 4);
	quant = h263_read(&r, 5);
	if (h263_read(&r, 1) || !quant)
		return -EINVAL;
	while (h263_read(&r, 1) && !r.error)
		h263_read(&r, 8);
	if (r.error)
		return r.error;
	h263_byte(&w, 0);
	h263_byte(&w, 0);
	h263_byte(&w, 1);
	h263_byte(&w, 0xb6);
	h263_byte(&w, temporal);
	h263_byte(&w, width >> 8);
	h263_byte(&w, width);
	h263_byte(&w, height >> 8);
	h263_byte(&w, height);
	h263_byte(&w, !picture_intra);
	h263_byte(&w, 0);
	for (x = 0; x < width / 8; x++) {
		top[x][0] = 999;
		top[x][1] = 999;
	}
	top[width / 8][0] = 0;
	top[width / 8][1] = 0;
	for (y = 0; y < height / 16; y++) {
		left[0] = 0;
		left[1] = 0;
		for (x = 0; x < width / 16; x++) {
			bool intra, dquant;
			int mcbpc, cbpy, cbp;

			if (r.error || w.error)
				return r.error ? r.error : w.error;
			if (r.bit + 17 <= r.size * 8 && !h263_peek(&r, 16)) {
				u32 gob;

				/* GOB header: bounded search, then reset the predictor row. */
				while (!r.error && h263_peek(&r, 17) != 1)
					h263_read(&r, 1);
				h263_read(&r, 17);
				gob = h263_read(&r, 5);
				h263_read(&r, 2);
				quant = h263_read(&r, 5);
				if (r.error || !quant || x ||
				    gob * (height == 576 ? 2 : 1) != y)
					return -EINVAL;
				for (component = 0; component < width / 8; component++) {
					top[component][0] = 999;
					top[component][1] = 999;
				}
				left[0] = 0;
				left[1] = 0;
			}
			if (!picture_intra && h263_read(&r, 1)) {
				h263_byte(&w, 0);
				top[x * 2][0] = 0;
				top[x * 2][1] = 0;
				top[x * 2 + 1][0] = 0;
				top[x * 2 + 1][1] = 0;
				left[0] = 0;
				left[1] = 0;
				continue;
			}
			do {
				mcbpc = h263_mcbpc(&r, picture_intra);
			} while (mcbpc == (picture_intra ? 8 : 20) && !r.error);
			if (mcbpc < 0)
				return -EINVAL;
			intra = picture_intra || (mcbpc & 4);
			dquant = mcbpc & (picture_intra ? 4 : 8);
			/* Four-vector mode requires the optional advanced-prediction tool. */
			if (!intra && (mcbpc & 16))
				return -EOPNOTSUPP;
			cbpy = h263_vlc(&r, h263_cbpy, ARRAY_SIZE(h263_cbpy));
			if (cbpy < 0)
				return -EINVAL;
			if (!intra)
				cbpy ^= 15;
			cbp = (cbpy << 2) | (mcbpc & 3);
			if (dquant) {
				static const int delta[] = { -1, -2, 1, 2 };

				quant += delta[h263_read(&r, 2)];
			}
			if (quant < 1 || quant > 31)
				return -EINVAL;
			h263_byte(&w, intra ? 1 : 2);
			for (component = 0; component < 2; component++) {
				int value = 0;

				if (!intra) {
					int predictor = h263_median(left[component],
							  top[x * 2][component],
								  top[(x + 1) * 2][component]);

					value = h263_mv(&r, predictor);
					h263_byte(&w, ((u32)value >> 7) & 63);
					h263_byte(&w, (u32)value * 2);
				}
				top[x * 2][component] = value;
				top[x * 2 + 1][component] = value;
				left[component] = value;
			}
			for (component = 0; component < 6; component++) {
				ret = h263_block(&r, &w, intra,
						 cbp & (1 << (5 - component)), quant);
				if (ret)
					return ret;
			}
		}
	}
	return r.error ? r.error : w.error ? w.error : (int)w.used;
}
#endif
