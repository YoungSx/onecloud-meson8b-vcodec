// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t u32;
#include "../ge2d/ge2d-scale.h"

int main(void)
{
	u32 step;
	bool pre;

	assert(!meson8b_ge2d_phase(1920, 1280, &step, &pre));
	assert(step == 0x1800000 && !pre);
	assert(!meson8b_ge2d_phase(1920, 640, &step, &pre));
	assert(step == 0x1800000 && pre);
	assert(meson8b_ge2d_phase(0, 640, &step, &pre) == -EINVAL);
	assert(meson8b_ge2d_phase(640, 1920, &step, &pre) == -EINVAL);
	assert(meson8b_ge2d_phase(1920, 400, &step, &pre) == -EINVAL);
	for (u32 phase = 0; phase <= 32; phase++) {
		u32 value = meson8b_ge2d_bilinear(phase);

		assert(((value >> 16) & 255) + ((value >> 8) & 255) == 128);
	}
	return 0;
}
