/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MESON8B_GE2D_SCALE_H
#define MESON8B_GE2D_SCALE_H

static inline int meson8b_ge2d_phase(u32 input, u32 output, u32 *step, bool *prescale)
{
	u32 effective;

	if (!input || !output || input > 1920 || output > 1920 ||
	    output > input || input > output * 4)
		return -EINVAL;
	*prescale = input > output * 2;
	effective = *prescale ? (input + 1) / 2 : input;
	*step = ((effective << 18) / output) << 6;
	return 0;
}

/* Vendor bilinear phase table: two coefficients sum to 128. */
static inline u32 meson8b_ge2d_bilinear(u32 phase)
{
	return ((128 - phase * 2) << 16) | (phase * 2 << 8);
}

#endif
