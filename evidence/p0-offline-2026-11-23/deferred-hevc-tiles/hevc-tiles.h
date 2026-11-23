/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef MESON_HEVC_TILES_H
#define MESON_HEVC_TILES_H

#define HEVC_MAX_TILE_COLS 5
#define HEVC_MAX_TILE_ROWS 5

struct hevc_tile_layout {
	u32 columns, rows;
	u32 x[HEVC_MAX_TILE_COLS + 1], y[HEVC_MAX_TILE_ROWS + 1];
};

struct hevc_tile {
	u32 column, row;
	u32 left, top, width, height;
};

/* RPM explicit widths/heights are CTU counts, not PPS minus-one values. */
static inline int hevc_tile_build(struct hevc_tile_layout *layout,
				  u32 width, u32 height, u32 flags,
				  u32 columns_minus1, u32 rows_minus1,
				  const u16 *widths, const u16 *heights)
{
	struct hevc_tile_layout next = {};
	u32 i, count, extent, *edges;
	const u16 *sizes;
	unsigned int axis;

	/* S805: at most 1920x1088 with 16-pixel CTUs. */
	if (!width || !height || width > 120 || height > 68 || flags & ~3u)
		return -EINVAL;
	if (!(flags & 1)) {
		next.columns = 1;
		next.rows = 1;
		next.x[1] = width;
		next.y[1] = height;
		*layout = next;
		return 0;
	}
	if (columns_minus1 >= HEVC_MAX_TILE_COLS ||
	    rows_minus1 >= HEVC_MAX_TILE_ROWS)
		return -EINVAL;
	next.columns = columns_minus1 + 1;
	next.rows = rows_minus1 + 1;
	for (axis = 0; axis < 2; axis++) {
		count = axis ? next.rows : next.columns;
		extent = axis ? height : width;
		edges = axis ? next.y : next.x;
		sizes = axis ? heights : widths;
		if (count > extent)
			return -EINVAL;
		for (i = 1; i < count; i++) {
			if (flags & 2)
				edges[i] = i * extent / count;
			else
				edges[i] = edges[i - 1] + sizes[i - 1];
			if (edges[i] <= edges[i - 1] || edges[i] >= extent)
				return -EINVAL;
		}
		edges[count] = extent;
	}
	*layout = next;
	return 0;
}

static inline int hevc_tile_select(const struct hevc_tile_layout *layout,
				   u32 address, struct hevc_tile *tile)
{
	u32 width, height, x, y, column = 0, row = 0;

	if (!layout->columns || layout->columns > HEVC_MAX_TILE_COLS ||
	    !layout->rows || layout->rows > HEVC_MAX_TILE_ROWS)
		return -EINVAL;
	width = layout->x[layout->columns];
	height = layout->y[layout->rows];
	if (!width || !height || width > 120 || height > 68 ||
	    address >= width * height)
		return -EINVAL;
	x = address % width;
	y = address / width;
	while (column + 1 < layout->columns && x >= layout->x[column + 1])
		column++;
	while (row + 1 < layout->rows && y >= layout->y[row + 1])
		row++;
	*tile = (struct hevc_tile) {
		.column = column, .row = row,
		.left = layout->x[column], .top = layout->y[row],
		.width = layout->x[column + 1] - layout->x[column],
		.height = layout->y[row + 1] - layout->y[row],
	};
	return 0;
}

#endif
