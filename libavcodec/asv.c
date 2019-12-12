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
 * ASUS V1/V2 encoder/decoder common data
 */

#include <stdint.h>

#include "asv.h"
#include "avcodec.h"
#include "bswapdsp.h"

const uint8_t ff_asv_scantab[64] = {
    0x00, 0x08, 0x01, 0x09, 0x10, 0x18, 0x11, 0x19,
    0x02, 0x0A, 0x03, 0x0B, 0x12, 0x1A, 0x13, 0x1B,
    0x04, 0x0C, 0x05, 0x0D, 0x20, 0x28, 0x21, 0x29,
    0x06, 0x0E, 0x07, 0x0F, 0x14, 0x1C, 0x15, 0x1D,
    0x22, 0x2A, 0x23, 0x2B, 0x30, 0x38, 0x31, 0x39,
    0x16, 0x1E, 0x17, 0x1F, 0x24, 0x2C, 0x25, 0x2D,
    0x32, 0x3A, 0x33, 0x3B, 0x26, 0x2E, 0x27, 0x2F,
    0x34, 0x3C, 0x35, 0x3D, 0x36, 0x3E, 0x37, 0x3F,
};

const uint8_t ff_asv_ccp_tab[17][2] = {
    { 0x2, 2 }, { 0x7, 5 }, { 0xB, 5 }, { 0x3, 5 },
    { 0xD, 5 }, { 0x5, 5 }, { 0x9, 5 }, { 0x1, 5 },
    { 0xE, 5 }, { 0x6, 5 }, { 0xA, 5 }, { 0x2, 5 },
    { 0xC, 5 }, { 0x4, 5 }, { 0x8, 5 }, { 0x3, 2 },
    { 0xF, 5 }, // EOB
};

const uint8_t ff_asv_level_tab[7][2] = {
    { 3, 4 }, { 3, 3 }, { 3, 2 }, { 0, 3 }, { 2, 2 }, { 2, 3 }, { 2, 4 }
};

const uint8_t ff_asv_dc_ccp_tab[8][2] = {
    { 0x1, 2 }, { 0xD, 4 }, { 0xF, 4 }, { 0xC, 4 },
    { 0x5, 3 }, { 0xE, 4 }, { 0x4, 3 }, { 0x0, 2 },
};

const uint8_t ff_asv_ac_ccp_tab[16][2] = {
    { 0x00, 2 }, { 0x3B, 6 }, { 0x0A, 4 }, { 0x3A, 6 },
    { 0x02, 3 }, { 0x39, 6 }, { 0x3C, 6 }, { 0x38, 6 },
    { 0x03, 3 }, { 0x3D, 6 }, { 0x08, 4 }, { 0x1F, 5 },
    { 0x09, 4 }, { 0x0B, 4 }, { 0x0D, 4 }, { 0x0C, 4 },
};

const uint8_t ff_asv2_level_tab[63][2] = {
    { 0x3F, 10 }, { 0x2F, 10 }, { 0x37, 10 }, { 0x27, 10 }, { 0x3B, 10 }, { 0x2B, 10 }, { 0x33, 10 }, { 0x23, 10 },
    { 0x3D, 10 }, { 0x2D, 10 }, { 0x35, 10 }, { 0x25, 10 }, { 0x39, 10 }, { 0x29, 10 }, { 0x31, 10 }, { 0x21, 10 },
    { 0x1F,  8 }, { 0x17,  8 }, { 0x1B,  8 }, { 0x13,  8 }, { 0x1D,  8 }, { 0x15,  8 }, { 0x19,  8 }, { 0x11,  8 },
    { 0x0F,  6 }, { 0x0B,  6 }, { 0x0D,  6 }, { 0x09,  6 },
    { 0x07,  4 }, { 0x05,  4 },
    { 0x03,  2 },
    { 0x00,  5 },
    { 0x02,  2 },
    { 0x04,  4 }, { 0x06,  4 },
    { 0x08,  6 }, { 0x0C,  6 }, { 0x0A,  6 }, { 0x0E,  6 },
    { 0x10,  8 }, { 0x18,  8 }, { 0x14,  8 }, { 0x1C,  8 }, { 0x12,  8 }, { 0x1A,  8 }, { 0x16,  8 }, { 0x1E,  8 },
    { 0x20, 10 }, { 0x30, 10 }, { 0x28, 10 }, { 0x38, 10 }, { 0x24, 10 }, { 0x34, 10 }, { 0x2C, 10 }, { 0x3C, 10 },
    { 0x22, 10 }, { 0x32, 10 }, { 0x2A, 10 }, { 0x3A, 10 }, { 0x26, 10 }, { 0x36, 10 }, { 0x2E, 10 }, { 0x3E, 10 },
};

av_cold void ff_asv_common_init(AVCodecContext *avctx)
{
    ASV1Context *const a = avctx->priv_data;

    ff_bswapdsp_init(&a->bbdsp);

    a->mb_width   = (avctx->width  + 15) / 16;
    a->mb_height  = (avctx->height + 15) / 16;
    a->mb_width2  = (avctx->width  +  0) / 16;
    a->mb_height2 = (avctx->height +  0) / 16;

    a->avctx = avctx;
}
