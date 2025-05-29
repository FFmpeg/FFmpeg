/*
 * AC-3 and E-AC-3 decoder tables
 * Copyright (c) 2007 Bartlomiej Wolowiec <bartek.wolowiec@gmail.com>
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

#ifndef AVCODEC_AC3DEC_DATA_H
#define AVCODEC_AC3DEC_DATA_H

#include <stdint.h>

#include "libavutil/attributes_internal.h"

FF_VISIBILITY_PUSH_HIDDEN

extern const uint8_t ff_ac3_ungroup_3_in_5_bits_tab[32][3];
extern       uint8_t ff_ac3_ungroup_3_in_7_bits_tab[128][3];

extern const int     ff_ac3_bap3_mantissas[ 7 + 1];
extern const int     ff_ac3_bap5_mantissas[15 + 1];

/** tables for ungrouping mantissas */
extern int ff_ac3_bap1_mantissas[32][3];
extern int ff_ac3_bap2_mantissas[128][3];
extern int ff_ac3_bap4_mantissas[128][2];

extern const uint8_t ff_ac3_quantization_tab[16];

extern const uint8_t ff_ac3_default_coeffs[8][5][2];

extern const uint8_t ff_eac3_hebap_tab[64];
extern const uint8_t ff_eac3_default_spx_band_struct[17];
extern const float   ff_eac3_gain_levels_lfe[32];

void ff_ac3_init_static(void);

FF_VISIBILITY_POP_HIDDEN

#endif /* AVCODEC_AC3DEC_DATA_H */
