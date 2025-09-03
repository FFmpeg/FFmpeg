/*
 * Copyright (c) 2016 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_CBRT_DATA_H
#define AVCODEC_CBRT_DATA_H

#include <stdint.h>

#define LUT_SIZE     (1 << 13)

#ifndef BUILD_TABLES
#include "config.h"
#define BUILD_TABLES !CONFIG_HARDCODED_TABLES
#endif

#if !BUILD_TABLES
#define ff_cbrt_tableinit_fixed()
#define ff_cbrt_tableinit()
extern const uint32_t ff_cbrt_tab[LUT_SIZE];
extern const uint32_t ff_cbrt_tab_fixed[LUT_SIZE];
#else
void ff_cbrt_tableinit(void);
void ff_cbrt_tableinit_fixed(void);

#define TMP_LUT_SIZE (LUT_SIZE / 2)
/**
 * Creates a LUT (of doubles) for the powers of
 * the odd integers: tmp_lut[idx] will be set to (2 * idx + 1)^{4/3}.
 */
void ff_cbrt_dbl_tableinit(double tmp_lut[TMP_LUT_SIZE]);

extern union CBRT {
    uint32_t cbrt_tab[LUT_SIZE];
    double tmp[TMP_LUT_SIZE];
} ff_cbrt_tab_internal, ff_cbrt_tab_internal_fixed;

#define ff_cbrt_tab       ff_cbrt_tab_internal.cbrt_tab
#define ff_cbrt_tab_fixed ff_cbrt_tab_internal_fixed.cbrt_tab
#endif

#endif
