/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef MESON8B_SWAP_NEON_H
#define MESON8B_SWAP_NEON_H

/* @size is a multiple of 8; call between kernel_neon_begin()/end() */
void meson8b_swap64_neon(void *destination, const void *source,
			 unsigned long size);

#endif
