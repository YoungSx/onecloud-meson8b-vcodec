// SPDX-License-Identifier: GPL-2.0+
/*
 * The Meson8b frame pool decides which slot the decoder may write and in
 * which order pictures reach userspace; a wrong answer overwrites a picture
 * before it is copied or reorders the stream. Pin both decoder styles: the
 * firmware owning every slot (H.264, MPEG, VC-1, MJPEG) and the driver
 * taking slots per picture (HEVC).
 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

typedef uint8_t u8;
typedef uint32_t u32;

#include "../vdec/meson8b-pool.h"

static void firmware_owned(void)
{
	struct meson8b_pool pool;
	u32 i;

	assert(meson8b_pool_reset(&pool, MESON8B_POOL_MAX + 1) == -EINVAL);
	assert(!meson8b_pool_reset(&pool, 9));
	/* Nothing is held before the buffer table is given. */
	assert(meson8b_pool_push(&pool, 0) == -EINVAL);
	meson8b_pool_give_all(&pool);
	assert(meson8b_pool_busy(&pool) == 9);
	assert(meson8b_pool_peek(&pool) == -1);

	/* Pictures leave in the order the firmware finished them. */
	assert(!meson8b_pool_push(&pool, 4));
	assert(!meson8b_pool_push(&pool, 1));
	assert(!meson8b_pool_push(&pool, 7));
	/* A slot cannot finish twice before it was delivered... */
	assert(meson8b_pool_push(&pool, 1) == -EINVAL);
	/* ...nor outside the pool. */
	assert(meson8b_pool_push(&pool, 9) == -EINVAL);
	assert(meson8b_pool_peek(&pool) == 4);
	assert(meson8b_pool_pop(&pool) == 4);
	assert(meson8b_pool_pop(&pool) == 1);
	/* Delivered, the slot is the firmware's again and may finish anew. */
	assert(!meson8b_pool_push(&pool, 1));
	assert(meson8b_pool_pop(&pool) == 7);
	assert(meson8b_pool_pop(&pool) == 1);
	assert(meson8b_pool_peek(&pool) == -1);

	/* The queue wraps around without losing order. */
	for (i = 0; i < 10 * MESON8B_POOL_MAX; i++) {
		assert(!meson8b_pool_push(&pool, i % 9));
		assert(!meson8b_pool_push(&pool, (i + 4) % 9));
		assert(meson8b_pool_pop(&pool) == i % 9);
		assert(meson8b_pool_pop(&pool) == (i + 4) % 9);
	}

	/* STREAMOFF forgets waiting pictures; the firmware keeps its slots. */
	assert(!meson8b_pool_push(&pool, 2));
	meson8b_pool_drop(&pool);
	assert(meson8b_pool_peek(&pool) == -1);
	assert(meson8b_pool_busy(&pool) == 9);
	assert(!meson8b_pool_push(&pool, 2));
}

static void driver_owned(void)
{
	struct meson8b_pool pool;
	int slot;
	u32 i;

	assert(!meson8b_pool_reset(&pool, 6));
	for (i = 0; i < 6; i++)
		assert(meson8b_pool_take(&pool) == (int)i);
	assert(meson8b_pool_take(&pool) == -1);

	/* Shown but still a reference: busy for both reasons. */
	assert(!meson8b_pool_push(&pool, 3));
	assert(meson8b_pool_take(&pool) == -1);
	/* No longer referenced, but not delivered: still busy. */
	assert(!meson8b_pool_release(&pool, 3));
	assert(meson8b_pool_release(&pool, 3) == -EINVAL);
	assert(meson8b_pool_take(&pool) == -1);
	assert(meson8b_pool_busy(&pool) == 6);
	/* Delivered and unreferenced: free. */
	assert(meson8b_pool_pop(&pool) == 3);
	assert(meson8b_pool_busy(&pool) == 5);
	slot = meson8b_pool_take(&pool);
	assert(slot == 3);

	/* Delivered first, unreferenced later: free only after both. */
	assert(!meson8b_pool_push(&pool, 0));
	assert(meson8b_pool_pop(&pool) == 0);
	assert(meson8b_pool_take(&pool) == -1);
	assert(!meson8b_pool_release(&pool, 0));
	assert(meson8b_pool_take(&pool) == 0);

	/* A released slot that waits is freed by STREAMOFF's drop. */
	assert(!meson8b_pool_push(&pool, 5));
	assert(!meson8b_pool_release(&pool, 5));
	meson8b_pool_drop(&pool);
	assert(meson8b_pool_take(&pool) == 5);
}

int main(void)
{
	firmware_owned();
	driver_owned();
	puts("pool: ok");
	return 0;
}
