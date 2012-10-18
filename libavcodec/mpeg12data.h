/*
 * MPEG1/2 tables
 * copyright (c) 2000,2001 Fabrice Bellard
 * copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * MPEG1/2 tables.
 */

#ifndef AVCODEC_MPEG12DATA_H
#define AVCODEC_MPEG12DATA_H

#include <stdint.h>
#include "libavutil/rational.h"
#include "rl.h"

extern const uint16_t ff_mpeg1_default_intra_matrix[64];
extern const uint16_t ff_mpeg1_default_non_intra_matrix[64];

extern const uint16_t ff_mpeg12_vlc_dc_lum_code[12];
extern const unsigned char ff_mpeg12_vlc_dc_lum_bits[12];
extern const uint16_t ff_mpeg12_vlc_dc_chroma_code[12];
extern const unsigned char ff_mpeg12_vlc_dc_chroma_bits[12];

extern RLTable ff_rl_mpeg1;
extern RLTable ff_rl_mpeg2;

extern const uint8_t ff_mpeg12_mbAddrIncrTable[36][2];
extern const uint8_t ff_mpeg12_mbPatTable[64][2];

extern const uint8_t ff_mpeg12_mbMotionVectorTable[17][2];

extern const AVRational ff_mpeg12_frame_rate_tab[];

extern const float ff_mpeg1_aspect[16];
extern const AVRational ff_mpeg2_aspect[16];

#endif /* AVCODEC_MPEG12DATA_H */
