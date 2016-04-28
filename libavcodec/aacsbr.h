/*
 * AAC Spectral Band Replication function declarations
 * Copyright (c) 2008-2009 Robert Swain ( rob opendot cl )
 * Copyright (c) 2010      Alex Converse <alex.converse@gmail.com>
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
 * AAC Spectral Band Replication function declarations
 * @author Robert Swain ( rob opendot cl )
 */

#ifndef AVCODEC_AACSBR_H
#define AVCODEC_AACSBR_H

#include "get_bits.h"
#include "aac.h"
#include "sbr.h"

#define ENVELOPE_ADJUSTMENT_OFFSET 2
#define NOISE_FLOOR_OFFSET 6

/**
 * SBR VLC tables
 */
enum {
    T_HUFFMAN_ENV_1_5DB,
    F_HUFFMAN_ENV_1_5DB,
    T_HUFFMAN_ENV_BAL_1_5DB,
    F_HUFFMAN_ENV_BAL_1_5DB,
    T_HUFFMAN_ENV_3_0DB,
    F_HUFFMAN_ENV_3_0DB,
    T_HUFFMAN_ENV_BAL_3_0DB,
    F_HUFFMAN_ENV_BAL_3_0DB,
    T_HUFFMAN_NOISE_3_0DB,
    T_HUFFMAN_NOISE_BAL_3_0DB,
};

/**
 * bs_frame_class - frame class of current SBR frame (14496-3 sp04 p98)
 */
enum {
    FIXFIX,
    FIXVAR,
    VARFIX,
    VARVAR,
};

enum {
    EXTENSION_ID_PS = 2,
};

static const int8_t vlc_sbr_lav[10] =
    { 60, 60, 24, 24, 31, 31, 12, 12, 31, 12 };

#define SBR_INIT_VLC_STATIC(num, size) \
    INIT_VLC_STATIC(&vlc_sbr[num], 9, sbr_tmp[num].table_size / sbr_tmp[num].elem_size,     \
                    sbr_tmp[num].sbr_bits ,                      1,                      1, \
                    sbr_tmp[num].sbr_codes, sbr_tmp[num].elem_size, sbr_tmp[num].elem_size, \
                    size)

#define SBR_VLC_ROW(name) \
    { name ## _codes, name ## _bits, sizeof(name ## _codes), sizeof(name ## _codes[0]) }

/** Initialize SBR. */
void AAC_RENAME(ff_aac_sbr_init)(void);
/** Initialize one SBR context. */
void AAC_RENAME(ff_aac_sbr_ctx_init)(AACContext *ac, SpectralBandReplication *sbr);
/** Close one SBR context. */
void AAC_RENAME(ff_aac_sbr_ctx_close)(SpectralBandReplication *sbr);
/** Decode one SBR element. */
int AAC_RENAME(ff_decode_sbr_extension)(AACContext *ac, SpectralBandReplication *sbr,
                            GetBitContext *gb, int crc, int cnt, int id_aac);
/** Apply one SBR element to one AAC element. */
void AAC_RENAME(ff_sbr_apply)(AACContext *ac, SpectralBandReplication *sbr, int id_aac,
                  INTFLOAT* L, INTFLOAT *R);

void ff_aacsbr_func_ptr_init_mips(AACSBRContext *c);

#endif /* AVCODEC_AACSBR_H */
