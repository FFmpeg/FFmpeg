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

#include "libavutil/common.h"

extern const uint8_t ff_ac3_ungroup_3_in_5_bits_tab[32][3];
extern const uint8_t ff_eac3_hebap_tab[64];
extern const uint8_t ff_eac3_bits_vs_hebap[20];
extern const int16_t ff_eac3_gaq_remap_1[12];
extern const int16_t ff_eac3_gaq_remap_2_4_a[9][2];
extern const int8_t  ff_eac3_gaq_remap_2_4_b[9][2];

extern const int16_t (* const ff_eac3_mantissa_vq[8])[6];
extern const uint8_t ff_eac3_frm_expstr[32][6];
extern const uint8_t ff_eac3_default_cpl_band_struct[18];

extern const uint8_t ff_ac3_rematrix_band_tab[5];

#endif /* AVCODEC_AC3DEC_DATA_H */
