// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t u32;
typedef uint16_t u16;
#include "../vdec/hevc-tiles.h"

int main(void)
{
	struct hevc_tile_grid grid = {};
	struct hevc_tile tile;
	u16 width[4] = { 3, 8, 5, 2 }, height[4] = { 4, 7, 2, 1 };

	assert(!hevc_tile_grid_init(&grid, 30, 17, 3, 3, 1, width, height));
	assert(grid.x[0] == 0 && grid.x[1] == 7 && grid.x[2] == 15 &&
	       grid.x[3] == 22 && grid.x[4] == 30);
	assert(!hevc_tile_at(&grid, 14, &tile) && tile.column == 1);
	assert(!hevc_tile_at(&grid, 15, &tile) && tile.column == 2);
	assert(!hevc_tile_at(&grid, 30 * 8 + 29, &tile) && tile.row == 1 && tile.column == 3);
	assert(hevc_tile_at(&grid, 30 * 17, &tile) == -EINVAL);
	assert(!hevc_tile_grid_init(&grid, 30, 17, 1, 4, 4, width, height));
	assert(grid.x[4] == 18 && grid.x[5] == 30 && grid.y[4] == 14 && grid.y[5] == 17);
	width[0] = 0;
	assert(hevc_tile_grid_init(&grid, 30, 17, 1, 1, 0, width, height) == -EINVAL);
	width[0] = 30;
	assert(hevc_tile_grid_init(&grid, 30, 17, 1, 1, 0, width, height) == -EINVAL);
	assert(hevc_tile_grid_init(&grid, 30, 17, 3, UINT32_MAX, 0, width, height) == -EINVAL);
	assert(hevc_tile_grid_init(&grid, 30, 17, 4, 0, 0, width, height) == -EINVAL);
	assert(!hevc_tile_grid_init(&grid, 30, 17, 0, 65535, 65535, width, height));
	assert(grid.columns == 1 && grid.rows == 1 && grid.x[1] == 30);
	for (u32 x = 1; x <= 120; x++)
		for (u32 y = 1; y <= 68; y++)
			for (u32 columns = 1; columns <= 5 && columns <= x; columns++)
				for (u32 rows = 1; rows <= 5 && rows <= y; rows++) {
					assert(!hevc_tile_grid_init(&grid, x, y, 3, columns - 1,
								 rows - 1, width, height));
					assert(!hevc_tile_at(&grid, x * y - 1, &tile));
					assert(tile.left + tile.width == x && tile.top + tile.height == y);
				}
	return 0;
}
