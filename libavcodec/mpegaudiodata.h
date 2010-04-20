/*
 * MPEG Audio common tables
 * copyright (c) 2002 Fabrice Bellard
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

/**
 * @file
 * mpeg audio layer common tables.
 */

#ifndef AVCODEC_MPEGAUDIODATA_H
#define AVCODEC_MPEGAUDIODATA_H

#include "libavutil/common.h"

#define MODE_EXT_MS_STEREO 2
#define MODE_EXT_I_STEREO  1

extern const uint16_t ff_mpa_bitrate_tab[2][3][15];
extern const uint16_t ff_mpa_freq_tab[3];
extern const int32_t ff_mpa_enwindow[257];
extern const int ff_mpa_sblimit_table[5];
extern const int ff_mpa_quant_steps[17];
extern const int ff_mpa_quant_bits[17];
extern const unsigned char * const ff_mpa_alloc_tables[5];

#endif /* AVCODEC_MPEGAUDIODATA_H */
