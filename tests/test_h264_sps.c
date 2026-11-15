// SPDX-License-Identifier: GPL-2.0+
/* SPS crop rewrite on SPS units the S805 microcode emitted on the target. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
#include "../venc/h264-sps.h"

/* 854x480: no cropping; 1920x1080: bottom offset 4 */
static const u8 sps854[] = { 0x67, 0x42, 0x00, 0x28, 0xf4, 0x06, 0xc1, 0xec, 0x80 };
static const u8 sps1080[] = { 0x67, 0x42, 0x00, 0x28, 0xf4, 0x03, 0xc0, 0x11, 0x3f, 0x2a };

struct fields { u32 width_mbs, height_mbs, crop[4]; };

static struct fields parse(const u8 *nal, u32 len)
{
	u8 rbsp[VENC_SPS_MAX];
	struct venc_bits b = { rbsp, venc_rbsp(nal + 1, len - 1, rbsp), 0, false };
	struct fields f = { 0 };

	assert(venc_get_bits(&b, 24) == 0x420028);
	for (int i = 0; i < 5; i++)	/* id, frame_num, poc type, poc lsb, refs */
		venc_get_ue(&b);
	venc_get_bits(&b, 1);
	f.width_mbs = venc_get_ue(&b) + 1;
	f.height_mbs = venc_get_ue(&b) + 1;
	assert(venc_get_bits(&b, 2) == 3);	/* frame_mbs_only, direct_8x8 */
	if (venc_get_bits(&b, 1))
		for (int i = 0; i < 4; i++)
			f.crop[i] = venc_get_ue(&b);
	assert(venc_get_bits(&b, 1) == 0 && venc_get_bits(&b, 1) == 1 && !b.error);
	return f;
}

int main(void)
{
	u8 out[2 * VENC_SPS_MAX], padded[] = { 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x03 };
	u8 escaped[16], back[16];
	struct fields f;
	u32 len;

	f = parse(sps854, sizeof(sps854));
	assert(f.width_mbs == 54 && f.height_mbs == 30 && !f.crop[1]);
	/* an unchanged window reproduces the microcode's bytes exactly */
	len = venc_sps_crop_right(sps1080, sizeof(sps1080), 0, out);
	assert(len == sizeof(sps1080) && !memcmp(out, sps1080, len));
	len = venc_sps_crop_right(sps854, sizeof(sps854), 0, out);
	assert(len == sizeof(sps854) && !memcmp(out, sps854, len));
	/* 864 coded, 854 visible: right offset 5 chroma samples */
	len = venc_sps_crop_right(sps854, sizeof(sps854), 5, out);
	f = parse(out, len);
	assert(f.width_mbs == 54 && f.height_mbs == 30 && f.crop[0] == 0 &&
	       f.crop[1] == 5 && f.crop[2] == 0 && f.crop[3] == 0);
	/* the bottom crop survives a right crop */
	len = venc_sps_crop_right(sps1080, sizeof(sps1080), 8, out);
	f = parse(out, len);
	assert(f.width_mbs == 120 && f.height_mbs == 68 && f.crop[1] == 8 && f.crop[3] == 4);
	/* anything else is refused */
	memcpy(out, sps854, sizeof(sps854));
	out[1] = 100;	/* High profile carries chroma_format_idc */
	assert(!venc_sps_crop_right(out, sizeof(sps854), 5, out + VENC_SPS_MAX));
	assert(!venc_sps_crop_right(sps854 + 1, sizeof(sps854) - 1, 5, out));
	/* emulation prevention round trip */
	len = venc_ebsp(padded, sizeof(padded), escaped);
	assert(len == 10 && escaped[2] == 3 && escaped[8] == 3);
	assert(venc_rbsp(escaped, len, back) == sizeof(padded) && !memcmp(back, padded, sizeof(padded)));
	puts("H.264 SPS crop checks passed");
	return 0;
}
