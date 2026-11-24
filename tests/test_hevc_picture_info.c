/* SPDX-License-Identifier: GPL-2.0+ */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
#include "../vdec/hevc-sps.h"

static void put(u8 *p, u32 *bit, u32 value, u32 count)
{
	while (count--) {
		p[*bit / 8] |= ((value >> count) & 1) << (7 - *bit % 8);
		(*bit)++;
	}
}

static void ue(u8 *p, u32 *bit, u32 value)
{
	u32 n = 0;

	for (u32 v = value + 1; v > 1; v >>= 1)
		n++;
	put(p, bit, 0, n);
	put(p, bit, value + 1, n + 1);
}

static u32 pps(u8 *packet, u32 id, bool output, u32 extra)
{
	u32 bit = 48;

	memset(packet, 0, 32);
	packet[3] = 1;
	packet[4] = 0x44;
	packet[5] = 1;
	ue(packet, &bit, id);
	ue(packet, &bit, 0);
	put(packet, &bit, 1, 1);
	put(packet, &bit, output, 1);
	put(packet, &bit, extra, 3);
	put(packet, &bit, 1, 1);
	return (bit + 7) / 8;
}

static u32 slice(u8 *packet, u32 id, u32 type, bool no_prior,
		 u32 extra, bool output, bool output_present)
{
	u32 bit = 48;

	memset(packet, 0, 32);
	packet[3] = 1;
	packet[4] = type << 1;
	packet[5] = 1;
	put(packet, &bit, 1, 1);
	if (type >= 16 && type <= 23)
		put(packet, &bit, no_prior, 1);
	ue(packet, &bit, id);
	put(packet, &bit, 0, extra);
	ue(packet, &bit, type >= 16 && type <= 23 ? 2 : 1);
	if (output_present)
		put(packet, &bit, output, 1);
	put(packet, &bit, 1, 1);
	return (bit + 7) / 8;
}

int main(void)
{
	struct hevc_parameter_sets sets = {};
	struct hevc_picture_info info = {};
	u8 packet[32];
	u32 length;

	sets.sps[0] = (struct hevc_sps_geometry) {
		.coded_width = 320, .coded_height = 192, .width = 320, .height = 192,
	};
	for (u32 extra = 0; extra < 8; extra++) {
		length = pps(packet, 63, true, extra);
		assert(!hevc_input_picture_info(&sets, packet, length, &info));
		for (u32 type = 0; type <= 21; type++) {
			for (u32 output = 0; output <= 1; output++) {
				length = slice(packet, 63, type, true, extra, output, true);
				assert(hevc_input_picture_info(&sets, packet, length, &info) == 1);
				assert(info.output == !!output);
				assert(info.no_output_of_prior == (type >= 16));
				assert(info.geometry.width == 320);
				for (u32 short_length = 4; short_length < length - 1; short_length++)
					assert(hevc_input_picture_info(&sets, packet, short_length, &info) != 1);
			}
		}
	}
	/* Replacing a PPS changes presence, and an absent flag means output. */
	length = pps(packet, 63, false, 0);
	assert(!hevc_input_picture_info(&sets, packet, length, &info));
	length = slice(packet, 63, 19, false, 0, false, false);
	assert(hevc_input_picture_info(&sets, packet, length, &info) == 1);
	assert(info.output && !info.no_output_of_prior);
	/* Sequence-end counts include standalone NALs and boundaries before/after VCL. */
	u8 boundary[80] = { 0, 0, 1, 0x48, 1, 0x80 };
	assert(!hevc_input_picture_info(&sets, boundary, 6, &info));
	assert(info.sequence_ends == 1);
	length = slice(packet, 63, 21, false, 0, true, false);
	memcpy(boundary + 6, packet, length);
	memcpy(boundary + 6 + length, boundary, 6);
	assert(hevc_input_picture_info(&sets, boundary, 12 + length, &info) == 1);
	assert(info.sequence_ends == 2 && info.output && !info.no_output_of_prior);
	assert(!hevc_input_picture_info(&sets, boundary, 0, &info));
	assert(!info.sequence_ends);
	return 0;
}
