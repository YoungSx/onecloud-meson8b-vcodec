/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef MESON8B_POOL_H
#define MESON8B_POOL_H

/*
 * Bookkeeping of the Meson8b decoder frame pool.
 *
 * The firmware writes pictures into pool slots in its own byte order; the
 * driver converts a finished picture into whichever CAPTURE buffer userspace
 * queues next. A slot is busy while the decoder holds it (the firmware's
 * buffer table, or a picture in the HEVC DPB) and while its picture waits for
 * a CAPTURE buffer. Pictures are delivered in the order they were finished.
 */
#define MESON8B_POOL_MAX 24

struct meson8b_pool {
	u32 count;	/* slots of this session */
	u32 decoder;	/* bit per slot held by the decoder */
	u32 waiting;	/* bit per slot whose picture waits for delivery */
	u8 queue[MESON8B_POOL_MAX];
	u32 head, len;
};

static inline int meson8b_pool_reset(struct meson8b_pool *pool, u32 count)
{
	if (count > MESON8B_POOL_MAX)
		return -EINVAL;
	__builtin_memset(pool, 0, sizeof(*pool));
	pool->count = count;
	return 0;
}

/* The firmware owns every slot from the moment it gets the buffer table. */
static inline void meson8b_pool_give_all(struct meson8b_pool *pool)
{
	pool->decoder = (1u << pool->count) - 1;
}

/* A free slot for a new picture, or -1 while every slot is busy. */
static inline int meson8b_pool_take(struct meson8b_pool *pool)
{
	u32 slot;

	for (slot = 0; slot < pool->count; slot++) {
		if ((pool->decoder | pool->waiting) & (1u << slot))
			continue;
		pool->decoder |= 1u << slot;
		return slot;
	}
	return -1;
}

/* The decoder no longer references @slot. */
static inline int meson8b_pool_release(struct meson8b_pool *pool, u32 slot)
{
	if (slot >= pool->count || !(pool->decoder & (1u << slot)))
		return -EINVAL;
	pool->decoder &= ~(1u << slot);
	return 0;
}

/* The picture in @slot is finished and waits for a CAPTURE buffer. */
static inline int meson8b_pool_push(struct meson8b_pool *pool, u32 slot)
{
	if (slot >= pool->count || !(pool->decoder & (1u << slot)) ||
	    (pool->waiting & (1u << slot)))
		return -EINVAL;
	pool->waiting |= 1u << slot;
	pool->queue[(pool->head + pool->len) % MESON8B_POOL_MAX] = slot;
	pool->len++;
	return 0;
}

/* The oldest waiting picture, or -1. */
static inline int meson8b_pool_peek(const struct meson8b_pool *pool)
{
	return pool->len ? pool->queue[pool->head] : -1;
}

/* Its picture was delivered; the slot stays held while the decoder has it. */
static inline u32 meson8b_pool_pop(struct meson8b_pool *pool)
{
	u32 slot = pool->queue[pool->head];

	pool->head = (pool->head + 1) % MESON8B_POOL_MAX;
	pool->len--;
	pool->waiting &= ~(1u << slot);
	return slot;
}

/* Forget the waiting pictures, as STREAMOFF of the CAPTURE queue does. */
static inline void meson8b_pool_drop(struct meson8b_pool *pool)
{
	pool->waiting = 0;
	pool->head = 0;
	pool->len = 0;
}

/* Number of busy slots; no builtin, which needs libgcc on ARMv7. */
static inline u32 meson8b_pool_busy(const struct meson8b_pool *pool)
{
	u32 mask = pool->decoder | pool->waiting, n = 0;

	for (; mask; mask &= mask - 1)
		n++;
	return n;
}

#endif
