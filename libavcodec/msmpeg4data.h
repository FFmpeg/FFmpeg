/*
 * MSMPEG4 backend for ffmpeg encoder and decoder
 * copyright (c) 2001 Fabrice Bellard
 * copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * msmpeg4v1 & v2 stuff by Michael Niedermayer <michaelni@gmx.at>
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
 * MSMPEG4 data tables.
 */

#ifndef AVCODEC_MSMPEG4DATA_H
#define AVCODEC_MSMPEG4DATA_H

#include "libavutil/common.h"
#include "get_bits.h"
#include "rl.h"

/* motion vector table */
typedef struct MVTable {
    int n;
    const uint16_t *table_mv_code;
    const uint8_t *table_mv_bits;
    const uint8_t *table_mvx;
    const uint8_t *table_mvy;
    uint16_t *table_mv_index; /* encoding: convert mv to index in table_mv */
    VLC vlc;                /* decoding: vlc */
} MVTable;

extern VLC ff_msmp4_mb_i_vlc;
extern VLC ff_msmp4_dc_luma_vlc[2];
extern VLC ff_msmp4_dc_chroma_vlc[2];

/* intra picture macroblock coded block pattern */
extern const uint16_t ff_msmp4_mb_i_table[64][2];

#define WMV1_SCANTABLE_COUNT 4

extern const uint8_t wmv1_scantable[WMV1_SCANTABLE_COUNT][64];

#define NB_RL_TABLES  6

extern RLTable rl_table[NB_RL_TABLES];

extern const uint8_t wmv1_y_dc_scale_table[32];
extern const uint8_t wmv1_c_dc_scale_table[32];
extern const uint8_t old_ff_y_dc_scale_table[32];

extern MVTable mv_tables[2];

extern const uint8_t v2_mb_type[8][2];
extern const uint8_t v2_intra_cbpc[4][2];

extern const uint32_t table_mb_non_intra[128][2];
extern const uint8_t  table_inter_intra[4][2];

extern const uint32_t ff_table0_dc_lum[120][2];
extern const uint32_t ff_table1_dc_lum[120][2];
extern const uint32_t ff_table0_dc_chroma[120][2];
extern const uint32_t ff_table1_dc_chroma[120][2];

#define WMV2_INTER_CBP_TABLE_COUNT 4
extern const uint32_t (* const wmv2_inter_table[WMV2_INTER_CBP_TABLE_COUNT])[2];

extern const uint8_t wmv2_scantableA[64];
extern const uint8_t wmv2_scantableB[64];

#endif /* AVCODEC_MSMPEG4DATA_H */
