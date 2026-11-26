/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef MESON_CAPTURE_GEOMETRY_H
#define MESON_CAPTURE_GEOMETRY_H

/* The decoded source keeps its reference geometry when CAPTURE is scaled. */
struct meson_capture_geometry {
	u32 source_width, source_height;
	u32 width, height;
};

struct meson_capture_size {
	u32 width, height;
};

struct meson_capture_ratio {
	u32 numerator, denominator;
};

static inline struct meson_capture_size
meson_capture_resolve(const struct meson_capture_geometry *capture,
		      u32 source_width, u32 source_height)
{
	struct meson_capture_size size = { source_width, source_height };

	if (capture->source_width == source_width &&
	    capture->source_height == source_height &&
	    capture->width && capture->height) {
		size.width = capture->width;
		size.height = capture->height;
	}
	return size;
}

/* S_FMT permits independent 1..1/4 downscaling, with even NV12 dimensions. */
static inline void
meson_capture_configure(struct meson_capture_geometry *capture,
			u32 source_width, u32 source_height,
			u32 width, u32 height, bool scalable)
{
	u32 min_width = ((source_width + 3) / 4 + 1) & ~1u;
	u32 min_height = ((source_height + 3) / 4 + 1) & ~1u;

	capture->source_width = source_width;
	capture->source_height = source_height;
	capture->width = source_width;
	capture->height = source_height;
	if (!scalable || ((source_width | source_height) & 1) ||
	    source_width < 2 || source_height < 2)
		return;
	width = width ? width : source_width;
	height = height ? height : source_height;
	capture->width = width < min_width ? min_width :
		width > source_width ? source_width : (width + 1) & ~1u;
	capture->height = height < min_height ? min_height :
		height > source_height ? source_height : (height + 1) & ~1u;
}

static inline u32 meson_capture_gcd(u32 a, u32 b)
{
	while (b) {
		u32 remainder = a % b;

		a = b;
		b = remainder;
	}
	return a;
}

/* CROPCAP reports inverse SAR; resampling changes pixel width and height. */
static inline struct meson_capture_ratio
meson_capture_pixelaspect(const struct meson_capture_geometry *capture,
			  u32 source_width, u32 source_height, u32 num, u32 den)
{
	struct meson_capture_size size = meson_capture_resolve(capture,
							    source_width, source_height);
	struct meson_capture_ratio ratio = { den, num };
	u32 n[3] = { den, source_height, size.width };
	u32 d[3] = { num, source_width, size.height };
	u64 numerator, denominator;
	unsigned int i, j;

	if (!num || !den || !source_width || !source_height ||
	    (size.width == source_width && size.height == source_height))
		return ratio;
	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			u32 divisor = meson_capture_gcd(n[i], d[j]);

			n[i] /= divisor;
			d[j] /= divisor;
		}
	}
	numerator = (u64)n[0] * n[1] * n[2];
	denominator = (u64)d[0] * d[1] * d[2];
	while (numerator > (u32)~0u || denominator > (u32)~0u) {
		numerator = (numerator + 1) / 2;
		denominator = (denominator + 1) / 2;
	}
	ratio.numerator = numerator;
	ratio.denominator = denominator;
	return ratio;
}

#endif
