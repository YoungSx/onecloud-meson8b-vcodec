/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef MESON_HEVC_SPS_H
#define MESON_HEVC_SPS_H

#include "codec_hevc.h"

struct hevc_sps_geometry {
	u32 id;
	u32 coded_width, coded_height;
	u32 left, top, width, height;
	u32 dpb, reorder;
};

struct hevc_bits {
	const u8 *data;
	u32 size, bit;
	bool error;
};

static inline u32 hevc_read_bits(struct hevc_bits *bits, u32 count)
{
	u32 value = 0, i;

	if (count > 32 || bits->bit > bits->size * 8 ||
	    count > bits->size * 8 - bits->bit) {
		bits->error = true;
		return 0;
	}
	for (i = 0; i < count; i++, bits->bit++)
		value = (value << 1) |
			((bits->data[bits->bit / 8] >> (7 - bits->bit % 8)) & 1);
	return value;
}

static inline void hevc_skip_bits(struct hevc_bits *bits, u32 count)
{
	while (count > 32) {
		hevc_read_bits(bits, 32);
		count -= 32;
	}
	hevc_read_bits(bits, count);
}

static inline u32 hevc_read_ue(struct hevc_bits *bits)
{
	u32 zeros = 0;

	while (!bits->error && !hevc_read_bits(bits, 1)) {
		if (++zeros >= 31) {
			bits->error = true;
			return 0;
		}
	}
	return ((1u << zeros) - 1) + hevc_read_bits(bits, zeros);
}

static inline u32 hevc_rbsp_prefix(const u8 *nal, u32 length, u8 *rbsp, u32 capacity)
{
	u32 n = 0, i, zeros = 0;

	for (i = 2; i < length && n < capacity; i++) {
		if (zeros >= 2 && nal[i] == 3) {
			zeros = 0;
			continue;
		}
		rbsp[n++] = nal[i];
		zeros = nal[i] == 0 ? zeros + 1 : 0;
	}
	return n;
}

/* Supplemental SPS geometry: S805 firmware RPM has no conformance-window
 * or sps_max_dec_pic_buffering fields. Parse only the bounded SPS prefix.
 */
static inline int hevc_parse_sps_geometry(const u8 *nal, u32 length,
					 struct hevc_sps_geometry *geometry)
{
	u8 rbsp[512], profile[7] = {}, level[7] = {};
	struct hevc_bits bits;
	u32 n, i, layers, start, chroma, right = 0, bottom = 0;
	struct hevc_sps_geometry g = {};

	if (length < 3 || ((nal[0] >> 1) & 63) != 33)
		return -EINVAL;
	n = hevc_rbsp_prefix(nal, length, rbsp, sizeof(rbsp));
	bits = (struct hevc_bits){ .data = rbsp, .size = n };
	hevc_read_bits(&bits, 4);
	layers = hevc_read_bits(&bits, 3);
	hevc_read_bits(&bits, 1);
	if (layers > 6)
		return -EINVAL;
	hevc_skip_bits(&bits, 96); /* general_profile_tier_level + general_level */
	for (i = 0; i < layers; i++) {
		profile[i] = hevc_read_bits(&bits, 1);
		level[i] = hevc_read_bits(&bits, 1);
	}
	if (layers)
		hevc_skip_bits(&bits, (8 - layers) * 2);
	for (i = 0; i < layers; i++) {
		if (profile[i])
			hevc_skip_bits(&bits, 88);
		if (level[i])
			hevc_skip_bits(&bits, 8);
	}
	g.id = hevc_read_ue(&bits);
	if (g.id >= 16)
		return -EINVAL;
	chroma = hevc_read_ue(&bits);
	if (chroma != 1)
		return -EOPNOTSUPP;
	g.coded_width = hevc_read_ue(&bits);
	g.coded_height = hevc_read_ue(&bits);
	if (hevc_read_bits(&bits, 1)) {
		g.left = hevc_read_ue(&bits);
		right = hevc_read_ue(&bits);
		g.top = hevc_read_ue(&bits);
		bottom = hevc_read_ue(&bits);
		if (g.left > 960 || right > 960 || g.top > 544 || bottom > 544)
			return -EINVAL;
		g.left *= 2;
		right *= 2;
		g.top *= 2;
		bottom *= 2;
	}
	if (!g.coded_width || !g.coded_height || g.coded_width > 1920 ||
	    g.coded_height > 1088 || g.left > g.coded_width ||
	    right >= g.coded_width - g.left || g.top > g.coded_height ||
	    bottom >= g.coded_height - g.top)
		return -EINVAL;
	g.width = g.coded_width - g.left - right;
	g.height = g.coded_height - g.top - bottom;
	if (hevc_read_ue(&bits) || hevc_read_ue(&bits))
		return -EOPNOTSUPP;
	if (hevc_read_ue(&bits) > 12)
		return -EINVAL;
	start = hevc_read_bits(&bits, 1) ? 0 : layers;
	for (i = start; i <= layers; i++) {
		g.dpb = hevc_read_ue(&bits) + 1;
		g.reorder = hevc_read_ue(&bits);
		hevc_read_ue(&bits);
		if (g.dpb > HEVC_MAX_DPB_SIZE || g.reorder >= g.dpb)
			return -EINVAL;
	}
	if (bits.error)
		return -EINVAL;
	*geometry = g;
	return 0;
}

struct hevc_parameter_sets {
	struct hevc_sps_geometry sps[16];
	u8 pps_sps[64];
	bool pps_valid[64];
	u32 max_dpb;
};

/* Return one picture's immutable SPS prefix, or zero for parameter-only input. */
static inline int hevc_input_geometry(struct hevc_parameter_sets *sets,
				      const u8 *data, u32 length,
				      struct hevc_sps_geometry *geometry)
{
	u32 i, picture_pps = 0;
	bool picture = false;

	for (i = 2; i + 2 < length; i++) {
		u32 end, type, id, sps_id;
		const u8 *nal;
		u8 rbsp[16];
		struct hevc_bits bits;
		struct hevc_sps_geometry g;
		bool first;
		int ret;

		if (data[i - 2] || data[i - 1] || data[i] != 1)
			continue;
		nal = data + i + 1;
		type = (nal[0] >> 1) & 63;
		for (end = i + 4; end + 2 < length; end++)
			if (!data[end] && !data[end + 1] && data[end + 2] == 1)
				break;
		if (end + 2 >= length)
			end = length;
		if (type == 33) {
			ret = hevc_parse_sps_geometry(nal, end - i - 1, &g);
			if (ret)
				return ret;
			sets->sps[g.id] = g;
			if (g.dpb > sets->max_dpb)
				sets->max_dpb = g.dpb;
		} else if (type == 34 || type <= 31) {
			bits = (struct hevc_bits){ .data = rbsp,
				.size = hevc_rbsp_prefix(nal, end - i - 1, rbsp, sizeof(rbsp)) };
			if (type == 34) {
				id = hevc_read_ue(&bits);
				sps_id = hevc_read_ue(&bits);
				if (bits.error || id >= 64 || sps_id >= 16)
					return -EINVAL;
				sets->pps_sps[id] = sps_id;
				sets->pps_valid[id] = true;
			} else {
				first = hevc_read_bits(&bits, 1);
				if (type >= 16 && type <= 23)
					hevc_skip_bits(&bits, 1);
				id = hevc_read_ue(&bits);
				if (bits.error || id >= 64 || !sets->pps_valid[id] ||
				    !sets->sps[sets->pps_sps[id]].coded_width)
					return -EINVAL;
				if (first) {
					if (picture)
						return -EINVAL;
					*geometry = sets->sps[sets->pps_sps[id]];
					picture_pps = id;
					picture = true;
				} else if (picture && id != picture_pps) {
					return -EINVAL;
				}
			}
		}
		i = end - 1;
	}
	return picture;
}
#endif
