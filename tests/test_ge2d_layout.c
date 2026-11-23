// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <errno.h>
#include <stdint.h>
typedef uint32_t u32;
#include "../ge2d/ge2d-layout.h"

int main(void)
{
	u32 stride, size;

	assert(meson8b_ge2d_layout(0, 32, &stride, &size) == -EINVAL);
	assert(meson8b_ge2d_layout(512, 0, &stride, &size) == -EINVAL);
	assert(meson8b_ge2d_layout(UINT32_MAX, 32, &stride, &size) == -EINVAL);
	assert(meson8b_ge2d_layout(32, UINT32_MAX, &stride, &size) == -EINVAL);
	for (u32 width = 1; width <= 1920; width++) {
		assert(!meson8b_ge2d_layout(width, 1088, &stride, &size));
		assert(stride >= width && stride % 32 == 0);
		assert(size == stride * 1088);
	}
	return 0;
}
