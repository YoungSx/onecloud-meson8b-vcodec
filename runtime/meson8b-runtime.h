/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MESON8B_RUNTIME_H
#define MESON8B_RUNTIME_H

#include <linux/types.h>

/* Importing this symbol prevents removing the overlay before its drivers. */
bool meson8b_runtime_ready(void);

#endif
