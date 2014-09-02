/*
 * Copyright (c) 2003 Michael Niedermayer
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
 * ASUS V1/V2 encoder/decoder common data.
 */

#ifndef AVCODEC_ASV_H
#define AVCODEC_ASV_H

#include <stdint.h>

#include "libavutil/mem.h"

#include "avcodec.h"
#include "blockdsp.h"
#include "bswapdsp.h"
#include "fdctdsp.h"
#include "idctdsp.h"
#include "get_bits.h"
#include "pixblockdsp.h"
#include "put_bits.h"

typedef struct ASV1Context {
    AVCodecContext *avctx;
    BlockDSPContext bdsp;
    BswapDSPContext bbdsp;
    FDCTDSPContext fdsp;
    IDCTDSPContext idsp;
    PixblockDSPContext pdsp;
    PutBitContext pb;
    GetBitContext gb;
    ScanTable scantable;
    int inv_qscale;
    int mb_width;
    int mb_height;
    int mb_width2;
    int mb_height2;
    DECLARE_ALIGNED(16, int16_t, block)[6][64];
    uint16_t intra_matrix[64];
    int q_intra_matrix[64];
    uint8_t *bitstream_buffer;
    unsigned int bitstream_buffer_size;
} ASV1Context;

extern const uint8_t ff_asv_scantab[64];
extern const uint8_t ff_asv_ccp_tab[17][2];
extern const uint8_t ff_asv_level_tab[7][2];
extern const uint8_t ff_asv_dc_ccp_tab[8][2];
extern const uint8_t ff_asv_ac_ccp_tab[16][2];
extern const uint8_t ff_asv2_level_tab[63][2];

void ff_asv_common_init(AVCodecContext *avctx);

#endif /* AVCODEC_ASV_H */
