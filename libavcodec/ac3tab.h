/*
 * AC3 tables
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard.
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

#ifndef AC3TAB_H
#define AC3TAB_H

#include "common.h"

extern const uint16_t ff_ac3_frame_sizes[38][3];
extern const uint8_t  ff_ac3_channels[8];
extern const uint16_t ff_ac3_freqs[3];
extern const uint16_t ff_ac3_bitratetab[19];
extern const int16_t  ff_ac3_window[256];
extern const uint8_t  ff_ac3_latab[260];
extern const uint16_t ff_ac3_hth[50][3];
extern const uint8_t  ff_ac3_baptab[64];
extern const uint8_t  ff_sdecaytab[4];
extern const uint8_t  ff_fdecaytab[4];
extern const uint16_t ff_sgaintab[4];
extern const uint16_t ff_dbkneetab[4];
extern const int16_t  ff_floortab[8];
extern const uint16_t ff_fgaintab[8];
extern const uint8_t  ff_ac3_bndsz[50];

#endif /* AC3TAB_H */
