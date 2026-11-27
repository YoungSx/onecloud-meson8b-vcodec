// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
typedef uint32_t u32;
typedef uint64_t dma_addr_t;
typedef uint64_t u64;
#define U32_MAX UINT32_MAX
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
	struct meson8b_ge2d_image image = {
		.width = 1920, .height = 1080, .stride = 1920,
		.address = { 0x1000000, 0x2000000 }, .nv12 = true,
	};
	struct meson8b_ge2d_crop crop = { .left = 2, .top = 4, .width = 1918, .height = 1076 };
	assert(!meson8b_ge2d_image_valid(&image));
	assert(!meson8b_ge2d_crop_valid(&image, &crop));
	crop.left = 1;
	assert(meson8b_ge2d_crop_valid(&image, &crop) == -EINVAL);
	crop.left = UINT32_MAX;
	assert(meson8b_ge2d_crop_valid(&image, &crop) == -EINVAL);
	image.address[1] = UINT32_MAX - 7;
	assert(meson8b_ge2d_image_valid(&image) == -EINVAL);
	image.address[1] = 0x2000001;
	assert(meson8b_ge2d_image_valid(&image) == -EINVAL);
	image.address[1] = 0x2000000;
	image.height = 1079;
	assert(meson8b_ge2d_image_valid(&image) == -EINVAL);
	image.nv12 = false;
	assert(!meson8b_ge2d_image_valid(&image));
	image.nv12 = true;
	image.height = 1080;
	image.address[1] = image.address[0] + 1920 * 1080;
	assert(!meson8b_ge2d_image_valid(&image));
	image.address[1] -= 8;
	assert(meson8b_ge2d_image_valid(&image) == -EINVAL);
	image.address[1] += 8;
	struct meson8b_ge2d_image destination = image;
	assert(meson8b_ge2d_images_overlap(&image, &destination));
	destination.address[0] = 0x3000000;
	destination.address[1] = 0x4000000;
	assert(!meson8b_ge2d_images_overlap(&image, &destination));
	struct meson8b_ge2d_image yv12 = {
		.width = 640, .height = 360, .stride = 640,
		.address = { 0x1000000, 0x1100000, 0x1140000 },
		.format = MESON8B_GE2D_FMT_YUV420,
	};
	assert(!meson8b_ge2d_image_valid(&yv12));
	yv12.address[2] = 0x1108000;
	assert(meson8b_ge2d_image_valid(&yv12) == -EINVAL);
	struct meson8b_ge2d_image rgb = {
		.width = 640, .height = 360, .stride = 1920,
		.address = { 0x1000000 }, .format = MESON8B_GE2D_FMT_RGB24,
	};
	assert(!meson8b_ge2d_image_valid(&rgb));
	crop = (struct meson8b_ge2d_crop){ .width = 640, .height = 360, .rotate = 45 };
	assert(meson8b_ge2d_crop_valid(&rgb, &crop) == -EINVAL);
	crop.rotate = 90;
	assert(!meson8b_ge2d_crop_valid(&rgb, &crop));
	return 0;
}
