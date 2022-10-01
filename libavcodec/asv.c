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

#include "libavutil/attributes.h"

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
    { 0x2, 2 }, { 0xB, 4 }, { 0xF, 4 }, { 0x3, 4 },
    { 0x5, 3 }, { 0x7, 4 }, { 0x1, 3 }, { 0x0, 2 },
};

const uint8_t ff_asv_ac_ccp_tab[16][2] = {
    { 0x00, 2 }, { 0x37, 6 }, { 0x05, 4 }, { 0x17, 6 },
    { 0x02, 3 }, { 0x27, 6 }, { 0x0F, 6 }, { 0x07, 6 },
    { 0x06, 3 }, { 0x2F, 6 }, { 0x01, 4 }, { 0x1F, 5 },
    { 0x09, 4 }, { 0x0D, 4 }, { 0x0B, 4 }, { 0x03, 4 },
};

const uint16_t ff_asv2_level_tab[63][2] = {
    { 0x3F0, 10 }, { 0x3D0, 10 }, { 0x3B0, 10 }, { 0x390, 10 }, { 0x370, 10 },
    { 0x350, 10 }, { 0x330, 10 }, { 0x310, 10 }, { 0x2F0, 10 }, { 0x2D0, 10 },
    { 0x2B0, 10 }, { 0x290, 10 }, { 0x270, 10 }, { 0x250, 10 }, { 0x230, 10 },
    { 0x210, 10 },
    { 0x0F8,  8 }, { 0x0E8,  8 }, { 0x0D8,  8 }, { 0x0C8,  8 }, { 0x0B8,  8 },
    { 0x0A8,  8 }, { 0x098,  8 }, { 0x088,  8 },
    { 0x03C,  6 }, { 0x034,  6 }, { 0x02C,  6 }, { 0x024,  6 },
    { 0x00E,  4 }, { 0x00A,  4 },
    { 0x003,  2 },
    { 0x000,  5 },
    { 0x001,  2 },
    { 0x002,  4 }, { 0x006,  4 },
    { 0x004,  6 }, { 0x00C,  6 }, { 0x014,  6 }, { 0x01C,  6 },
    { 0x008,  8 }, { 0x018,  8 }, { 0x028,  8 }, { 0x038,  8 }, { 0x048,  8 },
    { 0x058,  8 }, { 0x068,  8 }, { 0x078,  8 },
    { 0x010, 10 }, { 0x030, 10 }, { 0x050, 10 }, { 0x070, 10 }, { 0x090, 10 },
    { 0x0B0, 10 }, { 0x0D0, 10 }, { 0x0F0, 10 }, { 0x110, 10 }, { 0x130, 10 },
    { 0x150, 10 }, { 0x170, 10 }, { 0x190, 10 }, { 0x1B0, 10 }, { 0x1D0, 10 },
    { 0x1F0, 10 }
};

av_cold void ff_asv_common_init(AVCodecContext *avctx)
{
    ASVCommonContext *const a = avctx->priv_data;

    ff_bswapdsp_init(&a->bbdsp);

    a->mb_width   = (avctx->width  + 15) / 16;
    a->mb_height  = (avctx->height + 15) / 16;
    a->mb_width2  = (avctx->width  +  0) / 16;
    a->mb_height2 = (avctx->height +  0) / 16;

    a->avctx = avctx;
}
