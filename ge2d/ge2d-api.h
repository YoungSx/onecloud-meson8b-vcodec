/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MESON8B_GE2D_API_H
#define MESON8B_GE2D_API_H
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include "ge2d-layout.h"

/* Caller holds DMA buffers until convert returns; addresses share the S805 bus. */
struct meson8b_ge2d_api {
	struct device *(*get)(struct device *consumer);
	void (*put)(struct device *supplier);
	int (*convert)(struct device *supplier, const struct meson8b_ge2d_image *input,
		       const struct meson8b_ge2d_image *output,
		       const struct meson8b_ge2d_crop *crop);
};

const struct meson8b_ge2d_api *meson8b_ge2d_get_api(void);
#endif
