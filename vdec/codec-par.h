/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (C) 2019 BayLibre, SAS. */
/* Standard PAR codes shared by H.264, VC-1 and MPEG-4/H.263. */
#ifndef MESON8B_CODEC_PAR_H
#define MESON8B_CODEC_PAR_H

static const struct v4l2_fract amvdec_par_table[] = {
	{ 1, 1 },   { 1, 1 },    { 12, 11 }, { 10, 11 },
	{ 16, 11 }, { 40, 33 },  { 24, 11 }, { 20, 11 },
	{ 32, 11 }, { 80, 33 },  { 18, 11 }, { 15, 11 },
	{ 64, 33 }, { 160, 99 }, { 4, 3 },   { 3, 2 },
	{ 2, 1 }
};

#endif
