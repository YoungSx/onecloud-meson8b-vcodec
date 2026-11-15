/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Add a right-edge crop to the microcode's SPS. The microcode writes
 * frame_crop_bottom_offset for heights that are not whole macroblocks but
 * never a right offset, so a 854-wide picture decodes 864 wide. The driver
 * also pads unencodable macroblock widths, which needs the same crop.
 * Only the Baseline/Main, frame-only, VUI-less SPS the microcode emits is
 * accepted. Plain C shared with tests/test_h264_sps.c.
 */
#ifndef MESON_VENC_H264_SPS_H
#define MESON_VENC_H264_SPS_H

#define VENC_SPS_MAX		64	/* bytes of one SPS NAL unit */

struct venc_bits {
	u8 *data;
	u32 size, bit;
	bool error;
};

static inline u32 venc_get_bits(struct venc_bits *b, u32 count)
{
	u32 value = 0;

	if (count > 32 || b->bit + count > b->size * 8) {
		b->error = true;
		return 0;
	}
	for (; count; count--, b->bit++)
		value = value << 1 | ((b->data[b->bit / 8] >> (7 - b->bit % 8)) & 1);
	return value;
}

static inline u32 venc_get_ue(struct venc_bits *b)
{
	u32 zeros = 0;

	while (!b->error && !venc_get_bits(b, 1))
		if (++zeros > 16) {
			b->error = true;
			return 0;
		}
	return (1u << zeros) - 1 + venc_get_bits(b, zeros);
}

static inline void venc_put_bits(struct venc_bits *b, u32 value, u32 count)
{
	if (b->bit + count > b->size * 8) {
		b->error = true;
		return;
	}
	while (count--) {
		u8 mask = 0x80 >> (b->bit % 8);

		if (value >> count & 1)
			b->data[b->bit / 8] |= mask;
		else
			b->data[b->bit / 8] &= ~mask;
		b->bit++;
	}
}

static inline void venc_put_ue(struct venc_bits *b, u32 value)
{
	u32 bits = 0;

	while ((value + 1) >> (bits + 1))
		bits++;
	venc_put_bits(b, 0, bits);
	venc_put_bits(b, value + 1, bits + 1);
}
/* Drop emulation prevention bytes (00 00 03 becomes 00 00). */
static inline u32 venc_rbsp(const u8 *in, u32 len, u8 *out)
{
	u32 i, n = 0, zeros = 0;

	for (i = 0; i < len; i++) {
		if (zeros >= 2 && in[i] == 3) {
			zeros = 0;
			continue;
		}
		zeros = in[i] ? 0 : zeros + 1;
		out[n++] = in[i];
	}
	return n;
}

/* Insert emulation prevention bytes; @out holds 3 * @len / 2 + 1 bytes. */
static inline u32 venc_ebsp(const u8 *in, u32 len, u8 *out)
{
	u32 i, n = 0, zeros = 0;

	for (i = 0; i < len; i++) {
		if (zeros >= 2 && in[i] <= 3) {
			out[n++] = 3;
			zeros = 0;
		}
		zeros = in[i] ? 0 : zeros + 1;
		out[n++] = in[i];
	}
	return n;
}

/*
 * Rewrite SPS NAL unit @nal (header byte, no start code) with
 * frame_crop_right_offset @crop_right, in chroma samples, keeping the
 * other offsets. @out holds 2 * VENC_SPS_MAX bytes. Returns the new NAL
 * unit length, or 0 if the SPS is not of the expected form.
 */
static inline u32 venc_sps_crop_right(const u8 *nal, u32 len, u32 crop_right, u8 *out)
{
	u8 in[VENC_SPS_MAX], rbsp[VENC_SPS_MAX] = { 0 };
	struct venc_bits r = { in, 0, 0, false }, w = { rbsp, VENC_SPS_MAX, 0, false };
	u32 profile, value, crop[4] = { 0 }, i;

	if (len < 4 || len > VENC_SPS_MAX || (nal[0] & 0x1f) != 7)
		return 0;
	r.size = venc_rbsp(nal + 1, len - 1, in);
	profile = venc_get_bits(&r, 8);
	if (profile != 66 && profile != 77 && profile != 88)
		return 0;	/* no chroma_format_idc or scaling lists */
	venc_put_bits(&w, profile, 8);
	venc_put_bits(&w, venc_get_bits(&r, 16), 16);	/* constraints, level_idc */
	venc_put_ue(&w, venc_get_ue(&r));		/* seq_parameter_set_id */
	venc_put_ue(&w, venc_get_ue(&r));		/* log2_max_frame_num_minus4 */
	if (venc_get_ue(&r))				/* pic_order_cnt_type */
		return 0;
	venc_put_ue(&w, 0);
	venc_put_ue(&w, venc_get_ue(&r));		/* log2_max_pic_order_cnt_lsb_minus4 */
	venc_put_ue(&w, venc_get_ue(&r));		/* max_num_ref_frames */
	venc_put_bits(&w, venc_get_bits(&r, 1), 1);	/* gaps_in_frame_num_allowed */
	venc_put_ue(&w, venc_get_ue(&r));		/* pic_width_in_mbs_minus1 */
	venc_put_ue(&w, venc_get_ue(&r));		/* pic_height_in_map_units_minus1 */
	if (!venc_get_bits(&r, 1))			/* frame_mbs_only_flag */
		return 0;
	venc_put_bits(&w, 1, 1);
	venc_put_bits(&w, venc_get_bits(&r, 1), 1);	/* direct_8x8_inference_flag */
	if (venc_get_bits(&r, 1))			/* frame_cropping_flag */
		for (i = 0; i < 4; i++)
			crop[i] = venc_get_ue(&r);	/* left, right, top, bottom */
	/* no VUI, then exactly the RBSP stop bit and alignment */
	if (venc_get_bits(&r, 1) || venc_get_bits(&r, 1) != 1 || r.error)
		return 0;
	crop[1] = crop_right;
	value = crop[0] | crop[1] | crop[2] | crop[3];
	venc_put_bits(&w, !!value, 1);
	for (i = 0; value && i < 4; i++)
		venc_put_ue(&w, crop[i]);
	venc_put_bits(&w, 0, 1);			/* vui_parameters_present_flag */
	venc_put_bits(&w, 1, 1);			/* rbsp_stop_one_bit */
	while (w.bit % 8)
		venc_put_bits(&w, 0, 1);
	if (w.error)
		return 0;
	out[0] = nal[0];
	return 1 + venc_ebsp(rbsp, w.bit / 8, out + 1);
}

#endif
