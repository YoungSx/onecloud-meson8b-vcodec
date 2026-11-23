/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef MESON8B_HEVC_TILES_H
#define MESON8B_HEVC_TILES_H

#define HEVC_TILE_AXIS_MAX 5

struct hevc_tile_grid {
	u32 columns, rows, picture_width, picture_height;
	u32 x[HEVC_TILE_AXIS_MAX + 1], y[HEVC_TILE_AXIS_MAX + 1];
};

struct hevc_tile {
	u32 column, row, left, top, width, height;
};

static inline int hevc_tile_axis(u32 total, u32 count, bool uniform,
				 const u16 sizes[4], u32 boundary[6])
{
	u32 i;

	if (!total || !count || count > HEVC_TILE_AXIS_MAX || count > total)
		return -EINVAL;
	boundary[0] = 0;
	for (i = 1; i < count; i++) {
		/* H.265 uniform spacing distributes the remainder across boundaries. */
		if (uniform) {
			boundary[i] = i * total / count;
		} else {
			if (!sizes[i - 1] || sizes[i - 1] >= total - boundary[i - 1])
				return -EINVAL;
			boundary[i] = boundary[i - 1] + sizes[i - 1];
		}
	}
	boundary[count] = total;
	return 0;
}

static inline int hevc_tile_grid_init(struct hevc_tile_grid *grid,
				      u32 width, u32 height, u32 flags,
				      u32 columns_minus1, u32 rows_minus1,
				      const u16 widths[4], const u16 heights[4])
{
	struct hevc_tile_grid g = {};
	bool uniform = flags & 2;

	if (!width || !height || width > 120 || height > 68 || (flags & ~3u))
		return -EINVAL;
	if (!(flags & 1)) {
		g.columns = 1;
		g.rows = 1;
	} else {
		if (columns_minus1 >= HEVC_TILE_AXIS_MAX || rows_minus1 >= HEVC_TILE_AXIS_MAX)
			return -EINVAL;
		g.columns = columns_minus1 + 1;
		g.rows = rows_minus1 + 1;
	}
	if (hevc_tile_axis(width, g.columns, uniform, widths, g.x) ||
	    hevc_tile_axis(height, g.rows, uniform, heights, g.y))
		return -EINVAL;
	g.picture_width = width;
	g.picture_height = height;
	*grid = g;
	return 0;
}

/* Firmware slice_segment_address is a raster-scan CTU address. */
static inline int hevc_tile_at(const struct hevc_tile_grid *grid, u32 address,
			       struct hevc_tile *tile)
{
	u32 x, y, column, row;

	if (!grid->columns || !grid->rows ||
	    address >= grid->picture_width * grid->picture_height)
		return -EINVAL;
	x = address % grid->picture_width;
	y = address / grid->picture_width;
	for (column = 0; column + 1 < grid->columns && x >= grid->x[column + 1]; column++)
		;
	for (row = 0; row + 1 < grid->rows && y >= grid->y[row + 1]; row++)
		;
	*tile = (struct hevc_tile) {
		.column = column, .row = row,
		.left = grid->x[column], .top = grid->y[row],
		.width = grid->x[column + 1] - grid->x[column],
		.height = grid->y[row + 1] - grid->y[row],
	};
	return 0;
}

#endif
