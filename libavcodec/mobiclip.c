/*
 * MobiClip Video decoder
 * Copyright (c) 2015-2016 Florian Nouwt
 * Copyright (c) 2017 Adib Surani
 * Copyright (c) 2020 Paul B Mahol
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

#include <inttypes.h>

#include "libavutil/avassert.h"
#include "libavutil/thread.h"

#include "avcodec.h"
#include "bswapdsp.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "golomb.h"
#include "mathops.h"

#define MOBI_RL_VLC_BITS 12
#define MOBI_MV_VLC_BITS 6

static const uint8_t zigzag4x4_tab[] =
{
    0x00, 0x04, 0x01, 0x02, 0x05, 0x08, 0x0C, 0x09, 0x06, 0x03, 0x07, 0x0A,
    0x0D, 0x0E, 0x0B, 0x0F
};

static const uint8_t quant4x4_tab[][16] =
{
    { 10, 13, 13, 10, 16, 10, 13, 13, 13, 13, 16, 10, 16, 13, 13, 16 },
    { 11, 14, 14, 11, 18, 11, 14, 14, 14, 14, 18, 11, 18, 14, 14, 18 },
    { 13, 16, 16, 13, 20, 13, 16, 16, 16, 16, 20, 13, 20, 16, 16, 20 },
    { 14, 18, 18, 14, 23, 14, 18, 18, 18, 18, 23, 14, 23, 18, 18, 23 },
    { 16, 20, 20, 16, 25, 16, 20, 20, 20, 20, 25, 16, 25, 20, 20, 25 },
    { 18, 23, 23, 18, 29, 18, 23, 23, 23, 23, 29, 18, 29, 23, 23, 29 },
};

static const uint8_t quant8x8_tab[][64] =
{
    { 20, 19, 19, 25, 18, 25, 19, 24, 24, 19, 20, 18, 32, 18, 20, 19, 19, 24, 24, 19, 19, 25, 18, 25, 18, 25, 18, 25, 19, 24, 24, 19,
      19, 24, 24, 19, 18, 32, 18, 20, 18, 32, 18, 24, 24, 19, 19, 24, 24, 18, 25, 18, 25, 18, 19, 24, 24, 19, 18, 32, 18, 24, 24, 18,},
    { 22, 21, 21, 28, 19, 28, 21, 26, 26, 21, 22, 19, 35, 19, 22, 21, 21, 26, 26, 21, 21, 28, 19, 28, 19, 28, 19, 28, 21, 26, 26, 21,
      21, 26, 26, 21, 19, 35, 19, 22, 19, 35, 19, 26, 26, 21, 21, 26, 26, 19, 28, 19, 28, 19, 21, 26, 26, 21, 19, 35, 19, 26, 26, 19,},
    { 26, 24, 24, 33, 23, 33, 24, 31, 31, 24, 26, 23, 42, 23, 26, 24, 24, 31, 31, 24, 24, 33, 23, 33, 23, 33, 23, 33, 24, 31, 31, 24,
      24, 31, 31, 24, 23, 42, 23, 26, 23, 42, 23, 31, 31, 24, 24, 31, 31, 23, 33, 23, 33, 23, 24, 31, 31, 24, 23, 42, 23, 31, 31, 23,},
    { 28, 26, 26, 35, 25, 35, 26, 33, 33, 26, 28, 25, 45, 25, 28, 26, 26, 33, 33, 26, 26, 35, 25, 35, 25, 35, 25, 35, 26, 33, 33, 26,
      26, 33, 33, 26, 25, 45, 25, 28, 25, 45, 25, 33, 33, 26, 26, 33, 33, 25, 35, 25, 35, 25, 26, 33, 33, 26, 25, 45, 25, 33, 33, 25,},
    { 32, 30, 30, 40, 28, 40, 30, 38, 38, 30, 32, 28, 51, 28, 32, 30, 30, 38, 38, 30, 30, 40, 28, 40, 28, 40, 28, 40, 30, 38, 38, 30,
      30, 38, 38, 30, 28, 51, 28, 32, 28, 51, 28, 38, 38, 30, 30, 38, 38, 28, 40, 28, 40, 28, 30, 38, 38, 30, 28, 51, 28, 38, 38, 28,},
    { 36, 34, 34, 46, 32, 46, 34, 43, 43, 34, 36, 32, 58, 32, 36, 34, 34, 43, 43, 34, 34, 46, 32, 46, 32, 46, 32, 46, 34, 43, 43, 34,
      34, 43, 43, 34, 32, 58, 32, 36, 32, 58, 32, 43, 43, 34, 34, 43, 43, 32, 46, 32, 46, 32, 34, 43, 43, 34, 32, 58, 32, 43, 43, 32,},
};

static const uint8_t block4x4_coefficients_tab[] =
{
    15, 0, 2, 1, 4, 8, 12, 3, 11, 13, 14, 7, 10, 5, 9, 6,
};

static const uint8_t pframe_block4x4_coefficients_tab[] =
{
    0, 4, 1, 8, 2, 12, 3, 5, 10, 15, 7, 13, 14, 11, 9, 6,
};

static const uint8_t block8x8_coefficients_tab[] =
{
    0x00, 0x1F, 0x3F, 0x0F, 0x08, 0x04, 0x02, 0x01, 0x0B, 0x0E, 0x1B, 0x0D,
    0x03, 0x07, 0x0C, 0x17, 0x1D, 0x0A, 0x1E, 0x05, 0x10, 0x2F, 0x37, 0x3B,
    0x13, 0x3D, 0x3E, 0x09, 0x1C, 0x06, 0x15, 0x1A, 0x33, 0x11, 0x12, 0x14,
    0x18, 0x20, 0x3C, 0x35, 0x19, 0x16, 0x3A, 0x30, 0x31, 0x32, 0x27, 0x34,
    0x2B, 0x2D, 0x39, 0x38, 0x23, 0x36, 0x2E, 0x21, 0x25, 0x22, 0x24, 0x2C,
    0x2A, 0x28, 0x29, 0x26,
};

static const uint8_t pframe_block8x8_coefficients_tab[] =
{
    0x00, 0x0F, 0x04, 0x01, 0x08, 0x02, 0x0C, 0x03, 0x05, 0x0A, 0x0D, 0x07, 0x0E, 0x0B, 0x1F, 0x09,
    0x06, 0x10, 0x3F, 0x1E, 0x17, 0x1D, 0x1B, 0x1C, 0x13, 0x18, 0x1A, 0x12, 0x11, 0x14, 0x15, 0x20,
    0x2F, 0x16, 0x19, 0x37, 0x3D, 0x3E, 0x3B, 0x3C, 0x33, 0x35, 0x21, 0x24, 0x22, 0x28, 0x23, 0x2C,
    0x30, 0x27, 0x2D, 0x25, 0x3A, 0x2B, 0x2E, 0x2A, 0x31, 0x34, 0x38, 0x32, 0x29, 0x26, 0x39, 0x36
};

static const uint8_t run_residue[2][256] =
{
    {
       12,  6,  4,  3,  3,  3,  3,  2,  2,  2,  2,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        3,  2,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
        1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        1, 27, 11,  7,  3,  2,  2,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
        1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
        1, 41,  2,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
        1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    },
    {
       27, 10,  5,  4,  3,  3,  3,  3,  2,  2,  1,  1,  1,  1,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        8,  3,  2,  2,  2,  2,  2,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        1, 15, 10,  8,  4,  3,  2,  2,  2,  2,  2,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
        1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
        1, 21,  7,  2,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
        1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    },
};

static const uint8_t bits0[] = {
     9, 11, 11, 11, 11, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,  7, 10, 10,  9,
     9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
     9,  9,  9,  9,  9,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
     8,  8,  8,  7,  7,  7,  7,  7,  7,  7,  7,  6,  6,  6,  6,
     6,  6,  6,  6,  6,  6,  5,  5,  5,  4,  2,  3,  4,  4,
};

static const uint16_t syms0[] = {
    0x0, 0x822, 0x803, 0xB, 0xA, 0xB81, 0xB61, 0xB41, 0xB21, 0x122,
    0x102, 0xE2, 0xC2, 0xA2, 0x63, 0x43, 0x24, 0xC, 0x25, 0x2E1, 0x301,
    0xBA1, 0xBC1, 0xBE1, 0xC01, 0x26, 0x44, 0x83, 0xA3, 0xC3, 0x142,
    0x321, 0x341, 0xC21, 0xC41, 0xC61, 0xC81, 0xCA1, 0xCC1, 0xCE1, 0xD01,
    0x0, 0x9, 0x8, 0xB01, 0xAE1, 0xAC1, 0xAA1, 0xA81, 0xA61, 0xA41, 0xA21,
    0x802, 0x2C1, 0x2A1, 0x281, 0x261, 0x241, 0x221, 0x201, 0x1E1, 0x82,
    0x62, 0x7, 0x6, 0xA01, 0x9E1, 0x9C1, 0x9A1, 0x981, 0x961, 0x941, 0x921,
    0x1C1, 0x1A1, 0x42, 0x23, 0x5, 0x901, 0x8E1, 0x8C1, 0x8A1, 0x181, 0x161,
    0x141, 0x4, 0x881, 0x861, 0x841, 0x821, 0x121, 0x101, 0xE1, 0xC1, 0x22,
    0x3, 0xA1, 0x81, 0x61, 0x801, 0x1, 0x21, 0x41, 0x2,
};

static const uint16_t syms1[] = {
    0x0, 0x807, 0x806, 0x16, 0x15, 0x842, 0x823, 0x805, 0x1A1, 0xA3, 0x102, 0x83,
    0x64, 0x44, 0x27, 0x14, 0x13, 0x17, 0x18, 0x28, 0x122, 0x862, 0x882, 0x9E1, 0xA01,
    0x19, 0x1A, 0x1B, 0x29, 0xC3, 0x2A, 0x45, 0xE3, 0x1C1, 0x808, 0x8A2, 0x8C2, 0xA21,
    0xA41, 0xA61, 0xA81, 0x0, 0x12, 0x11, 0x9C1, 0x9A1, 0x981, 0x961, 0x941, 0x822, 0x804,
    0x181, 0x161, 0xE2, 0xC2, 0xA2, 0x63, 0x43, 0x26, 0x25, 0x10, 0x82, 0xF, 0xE, 0xD, 0x901,
    0x8E1, 0x8C1, 0x803, 0x141, 0x121, 0x101, 0x921, 0x62, 0x24, 0xC, 0xB, 0xA, 0x881, 0x861,
    0xC1, 0x8A1, 0xE1, 0x42, 0x23, 0x9, 0x802, 0xA1, 0x841, 0x821, 0x81, 0x61, 0x8, 0x7, 0x22,
    0x6, 0x41, 0x5, 0x4, 0x801, 0x1, 0x2, 0x21, 0x3,
};

static const uint8_t mv_len[16] =
{
    10, 8, 8, 7, 8, 8, 8, 7, 8, 8, 8, 7, 7, 7, 7, 6,
};

static const uint8_t mv_bits[2][16][10] =
{
    {
        { 2, 3, 3, 5, 5, 4, 4, 5, 5, 2 },
        { 2, 3, 4, 4, 3, 4, 4, 2 },
        { 3, 4, 4, 2, 4, 4, 3, 2 },
        { 1, 3, 4, 5, 5, 3, 3 },
        { 2, 4, 4, 3, 3, 4, 4, 2 },
        { 2, 3, 4, 4, 4, 4, 3, 2 },
        { 2, 3, 4, 4, 4, 4, 3, 2 },
        { 2, 2, 3, 4, 5, 5, 2 },
        { 2, 3, 4, 4, 3, 4, 4, 2 },
        { 2, 4, 4, 3, 4, 4, 3, 2 },
        { 2, 3, 3, 5, 5, 4, 3, 2 },
        { 2, 3, 4, 4, 3, 3, 2 },
        { 1, 4, 4, 3, 3, 4, 4 },
        { 2, 3, 4, 4, 3, 3, 2 },
        { 2, 3, 4, 4, 3, 3, 2 },
        { 3, 3, 2, 2, 3, 3 },
    },
    {
        { 3, 4, 5, 5, 3, 5, 6, 6, 4, 1 },
        { 2, 3, 4, 5, 5, 2, 3, 3 },
        { 2, 4, 4, 3, 3, 4, 4, 2 },
        { 1, 4, 4, 3, 4, 4, 3 },
        { 3, 3, 2, 4, 5, 5, 3, 2 },
        { 3, 4, 4, 3, 3, 3, 3, 2 },
        { 1, 3, 3, 4, 4, 4, 5, 5 },
        { 1, 4, 4, 3, 3, 4, 4 },
        { 2, 4, 4, 3, 3, 4, 4, 2 },
        { 1, 3, 3, 4, 4, 4, 5, 5 },
        { 2, 3, 4, 4, 4, 4, 3, 2 },
        { 2, 3, 3, 4, 4, 3, 2 },
        { 1, 4, 4, 3, 3, 4, 4 },
        { 1, 4, 4, 3, 3, 4, 4 },
        { 2, 3, 3, 4, 4, 3, 2 },
        { 2, 3, 3, 3, 3, 2 },
    }
};

static const uint8_t mv_syms[2][16][10] =
{
    {
        { 1, 8, 9, 4, 3, 2, 7, 5, 6, 0 },
        { 0, 9, 5, 4, 2, 3, 8, 1 },
        { 3, 9, 5, 0, 4, 8, 2, 1 },
        { 1, 3, 4, 8, 5, 2, 0 },
        { 0, 5, 4, 8, 2, 3, 9, 1 },
        { 0, 3, 5, 9, 4, 8, 2, 1 },
        { 0, 3, 9, 5, 8, 4, 2, 1 },
        { 0, 2, 3, 4, 8, 5, 1 },
        { 0, 3, 8, 4, 2, 5, 9, 1 },
        { 2, 8, 9, 3, 5, 4, 0, 1 },
        { 0, 4, 3, 8, 9, 5, 2, 1 },
        { 0, 4, 8, 5, 3, 2, 1 },
        { 1, 9, 4, 2, 0, 5, 3 },
        { 2, 4, 9, 5, 3, 0, 1 },
        { 0, 4, 9, 5, 3, 2, 1 },
        { 5, 4, 1, 0, 3, 2 },
    },
    {
        { 8, 2, 3, 6, 1, 7, 5, 4, 9, 0 },
        { 9, 2, 3, 5, 4, 1, 8, 0 },
        { 0, 5, 4, 2, 9, 3, 8, 1 },
        { 1, 5, 4, 2, 8, 3, 0 },
        { 2, 9, 8, 3, 5, 4, 0, 1 },
        { 3, 5, 4, 2, 9, 8, 0, 1 },
        { 1, 2, 0, 9, 8, 3, 5, 4 },
        { 1, 8, 5, 2, 0, 4, 3 },
        { 0, 5, 4, 2, 8, 3, 9, 1 },
        { 1, 2, 0, 9, 8, 3, 5, 4 },
        { 0, 3, 9, 8, 5, 4, 2, 1 },
        { 0, 4, 3, 8, 5, 2, 1 },
        { 1, 5, 4, 2, 0, 9, 3 },
        { 1, 9, 5, 2, 0, 4, 3 },
        { 0, 5, 3, 9, 4, 2, 1 },
        { 0, 4, 5, 3, 2, 1 },
    }
};

typedef struct BlockXY {
    int w, h;
    int ax, ay;
    int x, y;
    int size;
    uint8_t *block;
    int linesize;
} BlockXY;

typedef struct MotionXY {
    int x, y;
} MotionXY;

typedef struct MobiClipContext {
    AVFrame *pic[6];

    int current_pic;
    int moflex;
    int dct_tab_idx;
    int quantizer;

    GetBitContext gb;

    uint8_t *bitstream;
    int bitstream_size;

    int     qtab[2][64];
    uint8_t pre[32];
    MotionXY *motion;
    int     motion_size;

    BswapDSPContext bdsp;
} MobiClipContext;

static VLC rl_vlc[2];
static VLC mv_vlc[2][16];

static av_cold void mobiclip_init_static(void)
{
    INIT_VLC_STATIC_FROM_LENGTHS(&rl_vlc[0], MOBI_RL_VLC_BITS, 104,
                                 bits0, sizeof(*bits0),
                                 syms0, sizeof(*syms0), sizeof(*syms0),
                                 0, 0, 1 << MOBI_RL_VLC_BITS);
    INIT_VLC_STATIC_FROM_LENGTHS(&rl_vlc[1], MOBI_RL_VLC_BITS, 104,
                                 bits0, sizeof(*bits0),
                                 syms1, sizeof(*syms1), sizeof(*syms1),
                                 0, 0, 1 << MOBI_RL_VLC_BITS);
    for (int i = 0; i < 2; i++) {
        static VLCElem vlc_buf[2 * 16 << MOBI_MV_VLC_BITS];
        for (int j = 0; j < 16; j++) {
            mv_vlc[i][j].table           = &vlc_buf[(16 * i + j) << MOBI_MV_VLC_BITS];
            mv_vlc[i][j].table_allocated = 1 << MOBI_MV_VLC_BITS;
            ff_init_vlc_from_lengths(&mv_vlc[i][j], MOBI_MV_VLC_BITS, mv_len[j],
                                     mv_bits[i][j], sizeof(*mv_bits[i][j]),
                                     mv_syms[i][j], sizeof(*mv_syms[i][j]), sizeof(*mv_syms[i][j]),
                                     0, INIT_VLC_USE_NEW_STATIC, NULL);
        }
    }
}

static av_cold int mobiclip_init(AVCodecContext *avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    MobiClipContext *s = avctx->priv_data;

    if (avctx->width & 15 || avctx->height & 15) {
        av_log(avctx, AV_LOG_ERROR, "width/height not multiple of 16\n");
        return AVERROR_INVALIDDATA;
    }

    ff_bswapdsp_init(&s->bdsp);

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;

    s->motion = av_calloc(avctx->width / 16 + 3, sizeof(MotionXY));
    if (!s->motion)
        return AVERROR(ENOMEM);
    s->motion_size = (avctx->width / 16 + 3) * sizeof(MotionXY);

    for (int i = 0; i < 6; i++) {
        s->pic[i] = av_frame_alloc();
        if (!s->pic[i])
            return AVERROR(ENOMEM);
    }

    ff_thread_once(&init_static_once, mobiclip_init_static);

    return 0;
}

static int setup_qtables(AVCodecContext *avctx, int64_t quantizer)
{
    MobiClipContext *s = avctx->priv_data;
    int qx, qy;

    if (quantizer < 12 || quantizer > 161)
        return AVERROR_INVALIDDATA;

    s->quantizer = quantizer;

    qx = quantizer % 6;
    qy = quantizer / 6;

    for (int i = 0; i < 16; i++)
        s->qtab[0][i] = quant4x4_tab[qx][i] << qy;

    for (int i = 0; i < 64; i++)
        s->qtab[1][i] = quant8x8_tab[qx][i] << (qy - 2);

    for (int i = 0; i < 20; i++)
        s->pre[i] = 9;

    return 0;
}

static void inverse4(unsigned *rs)
{
    unsigned a = rs[0] + rs[2];
    unsigned b = rs[0] - rs[2];
    unsigned c = rs[1] + ((int)rs[3] >> 1);
    unsigned d = ((int)rs[1] >> 1) - rs[3];

    rs[0] = a + c;
    rs[1] = b + d;
    rs[2] = b - d;
    rs[3] = a - c;
}

static void idct(int *arr, int size)
{
    int e, f, g, h;
    unsigned x3, x2, x1, x0;
    int tmp[4];

    if (size == 4) {
        inverse4(arr);
        return;
    }

    tmp[0] = arr[0];
    tmp[1] = arr[2];
    tmp[2] = arr[4];
    tmp[3] = arr[6];

    inverse4(tmp);

    e = (unsigned)arr[7] + arr[1] - arr[3] - (arr[3] >> 1);
    f = (unsigned)arr[7] - arr[1] + arr[5] + (arr[5] >> 1);
    g = (unsigned)arr[5] - arr[3] - arr[7] - (arr[7] >> 1);
    h = (unsigned)arr[5] + arr[3] + arr[1] + (arr[1] >> 1);
    x3 = (unsigned)g + (h >> 2);
    x2 = (unsigned)e + (f >> 2);
    x1 = (e >> 2) - (unsigned)f;
    x0 = (unsigned)h - (g >> 2);

    arr[0] = tmp[0] + x0;
    arr[1] = tmp[1] + x1;
    arr[2] = tmp[2] + x2;
    arr[3] = tmp[3] + x3;
    arr[4] = tmp[3] - x3;
    arr[5] = tmp[2] - x2;
    arr[6] = tmp[1] - x1;
    arr[7] = tmp[0] - x0;
}

static void read_run_encoding(AVCodecContext *avctx,
                              int *last, int *run, int *level)
{
    MobiClipContext *s = avctx->priv_data;
    GetBitContext *gb = &s->gb;
    int n = get_vlc2(gb, rl_vlc[s->dct_tab_idx].table,
                     MOBI_RL_VLC_BITS, 1);

    *last = (n >> 11) == 1;
    *run  = (n >> 5) & 0x3F;
    *level = n & 0x1F;
}

static int add_coefficients(AVCodecContext *avctx, AVFrame *frame,
                            int bx, int by, int size, int plane)
{
    MobiClipContext *s = avctx->priv_data;
    GetBitContext *gb = &s->gb;
    int mat[64] = { 0 };
    const uint8_t *ztab = size == 8 ? ff_zigzag_direct : zigzag4x4_tab;
    const int *qtab = s->qtab[size == 8];
    uint8_t *dst = frame->data[plane] + by * frame->linesize[plane] + bx;

    for (int pos = 0; get_bits_left(gb) > 0; pos++) {
        int qval, last, run, level;

        read_run_encoding(avctx, &last, &run, &level);

        if (level) {
            if (get_bits1(gb))
                level = -level;
        } else if (!get_bits1(gb)) {
            read_run_encoding(avctx, &last, &run, &level);
            level += run_residue[s->dct_tab_idx][(last ? 64 : 0) + run];
            if (get_bits1(gb))
                level = -level;
        } else if (!get_bits1(gb)) {
            read_run_encoding(avctx, &last, &run, &level);
            run += run_residue[s->dct_tab_idx][128 + (last ? 64 : 0) + level];
            if (get_bits1(gb))
                level = -level;
        } else {
            last  = get_bits1(gb);
            run   = get_bits(gb, 6);
            level = get_sbits(gb, 12);
        }

        pos += run;
        if (pos >= size * size)
            return AVERROR_INVALIDDATA;
        qval = qtab[pos];
        mat[ztab[pos]] = qval *(unsigned)level;

        if (last)
            break;
    }

    mat[0] += 32;
    for (int y = 0; y < size; y++)
        idct(&mat[y * size], size);

    for (int y = 0; y < size; y++) {
        for (int x = y + 1; x < size; x++) {
            int a = mat[x * size + y];
            int b = mat[y * size + x];

            mat[y * size + x] = a;
            mat[x * size + y] = b;
        }

        idct(&mat[y * size], size);
        for (int x = 0; x < size; x++)
            dst[x] = av_clip_uint8(dst[x] + (mat[y * size + x] >> 6));
        dst += frame->linesize[plane];
    }

    return 0;
}

static int add_pframe_coefficients(AVCodecContext *avctx, AVFrame *frame,
                                   int bx, int by, int size, int plane)
{
    MobiClipContext *s = avctx->priv_data;
    GetBitContext *gb = &s->gb;
    int ret, idx = get_ue_golomb_31(gb);

    if (idx == 0) {
        return add_coefficients(avctx, frame, bx, by, size, plane);
    } else if ((unsigned)idx < FF_ARRAY_ELEMS(pframe_block4x4_coefficients_tab)) {
        int flags = pframe_block4x4_coefficients_tab[idx];

        for (int y = by; y < by + 8; y += 4) {
            for (int x = bx; x < bx + 8; x += 4) {
                if (flags & 1) {
                    ret = add_coefficients(avctx, frame, x, y, 4, plane);
                    if (ret < 0)
                        return ret;
                }
                flags >>= 1;
            }
        }
        return 0;
    } else {
        return AVERROR_INVALIDDATA;
    }
}

static int adjust(int x, int size)
{
    return size == 16 ? (x + 1) >> 1 : x;
}

static uint8_t pget(BlockXY b)
{
    BlockXY ret = b;
    int x, y;

    if (b.x == -1 && b.y >= b.size) {
        ret.x = -1, ret.y = b.size - 1;
    } else if (b.x >= -1 && b.y >= -1) {
        ret.x = b.x, ret.y = b.y;
    } else if (b.x == -1 && b.y == -2) {
        ret.x = 0, ret.y = -1;
    } else if (b.x == -2 && b.y == -1) {
        ret.x = -1, ret.y = 0;
    }

    y = av_clip(ret.ay + ret.y, 0, ret.h - 1);
    x = av_clip(ret.ax + ret.x, 0, ret.w - 1);

    return ret.block[y * ret.linesize + x];
}

static uint8_t half(int a, int b)
{
    return ((a + b) + 1) / 2;
}

static uint8_t half3(int a, int b, int c)
{
    return ((a + b + b + c) * 2 / 4 + 1) / 2;
}

static uint8_t pick_above(BlockXY bxy)
{
    bxy.y = bxy.y - 1;

    return pget(bxy);
}

static uint8_t pick_left(BlockXY bxy)
{
    bxy.x = bxy.x - 1;

    return pget(bxy);
}

static uint8_t half_horz(BlockXY bxy)
{
    BlockXY a = bxy, b = bxy, c = bxy;

    a.x -= 1;
    c.x += 1;

    return half3(pget(a), pget(b), pget(c));
}

static uint8_t half_vert(BlockXY bxy)
{
    BlockXY a = bxy, b = bxy, c = bxy;

    a.y -= 1;
    c.y += 1;

    return half3(pget(a), pget(b), pget(c));
}

static uint8_t pick_4(BlockXY bxy)
{
    int val;

    if ((bxy.x % 2) == 0) {
        BlockXY ba, bb;
        int a, b;

        ba = bxy;
        ba.x = -1;
        ba.y = bxy.y + bxy.x / 2;
        a = pget(ba);

        bb = bxy;
        bb.x = -1;
        bb.y = bxy.y + bxy.x / 2 + 1;
        b = pget(bb);

        val = half(a, b);
    } else {
        BlockXY ba;

        ba = bxy;
        ba.x = -1;
        ba.y = bxy.y + bxy.x / 2 + 1;
        val = half_vert(ba);
    }

    return val;
}

static uint8_t pick_5(BlockXY bxy)
{
    int val;

    if (bxy.x == 0) {
        BlockXY a = bxy;
        BlockXY b = bxy;

        a.x = -1;
        a.y -= 1;

        b.x = -1;

        val = half(pget(a), pget(b));
    } else if (bxy.y == 0) {
        BlockXY a = bxy;

        a.x -= 2;
        a.y -= 1;

        val = half_horz(a);
    } else if (bxy.x == 1) {
        BlockXY a = bxy;

        a.x -= 2;
        a.y -= 1;

        val = half_vert(a);
    } else {
        BlockXY a = bxy;

        a.x -= 2;
        a.y -= 1;

        val = pget(a);
    }

    return val;
}

static uint8_t pick_6(BlockXY bxy)
{
    int val;

    if (bxy.y == 0) {
        BlockXY a = bxy;
        BlockXY b = bxy;

        a.x -= 1;
        a.y = -1;

        b.y = -1;

        val = half(pget(a), pget(b));
    } else if (bxy.x == 0) {
        BlockXY a = bxy;

        a.x -= 1;
        a.y -= 2;

        val = half_vert(a);
    } else if (bxy.y == 1) {
        BlockXY a = bxy;

        a.x -= 1;
        a.y -= 2;

        val = half_horz(a);
    } else {
        BlockXY a = bxy;

        a.x -= 1;
        a.y -= 2;

        val = pget(a);
    }

    return val;
}

static uint8_t pick_7(BlockXY bxy)
{
    int clr, acc1, acc2;
    BlockXY a = bxy;

    a.x -= 1;
    a.y -= 1;
    clr = pget(a);
    if (bxy.x && bxy.y)
        return clr;

    if (bxy.x == 0) {
        a.x = -1;
        a.y = bxy.y;
    } else {
        a.x = bxy.x - 2;
        a.y = -1;
    }
    acc1 = pget(a);

    if (bxy.y == 0) {
        a.x = bxy.x;
        a.y = -1;
    } else {
        a.x = -1;
        a.y = bxy.y - 2;
    }
    acc2 = pget(a);

    return half3(acc1, clr, acc2);
}

static uint8_t pick_8(BlockXY bxy)
{
    BlockXY ba = bxy;
    BlockXY bb = bxy;
    int val;

    if (bxy.y == 0) {
        int a, b;

        ba.y = -1;
        a = pget(ba);

        bb.x += 1;
        bb.y = -1;

        b = pget(bb);

        val = half(a, b);
    } else if (bxy.y == 1) {
        ba.x += 1;
        ba.y -= 2;

        val = half_horz(ba);
    } else if (bxy.x < bxy.size - 1) {
        ba.x += 1;
        ba.y -= 2;

        val = pget(ba);
    } else if (bxy.y % 2 == 0) {
        int a, b;

        ba.x = bxy.y / 2 + bxy.size - 1;
        ba.y = -1;
        a = pget(ba);

        bb.x = bxy.y / 2 + bxy.size;
        bb.y = -1;

        b = pget(bb);

        val = half(a, b);
    } else {
        ba.x = bxy.y / 2 + bxy.size;
        ba.y = -1;

        val = half_horz(ba);
    }

    return val;
}

static void block_fill_simple(uint8_t *block, int size, int linesize, int fill)
{
    for (int y = 0; y < size; y++) {
        memset(block, fill, size);
        block += linesize;
    }
}

static void block_fill(uint8_t *block, int size, int linesize,
                       int w, int h, int ax, int ay,
                       uint8_t (*pick)(BlockXY bxy))
{
    BlockXY bxy;

    bxy.size = size;
    bxy.block = block;
    bxy.linesize = linesize;
    bxy.w = w;
    bxy.h = h;
    bxy.ay = ay;
    bxy.ax = ax;

    for (int y = 0; y < size; y++) {
        bxy.y = y;
        for (int x = 0; x < size; x++) {
            uint8_t val;

            bxy.x = x;

            val = pick(bxy);

            block[ax + x + (ay + y) * linesize] = val;
        }
    }
}

static int block_sum(const uint8_t *block, int w, int h, int linesize)
{
    int sum = 0;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            sum += block[x];
        }
        block += linesize;
    }

    return sum;
}

static int predict_intra(AVCodecContext *avctx, AVFrame *frame, int ax, int ay,
                          int pmode, int add_coeffs, int size, int plane)
{
    MobiClipContext *s = avctx->priv_data;
    GetBitContext *gb = &s->gb;
    int w = avctx->width >> !!plane, h = avctx->height >> !!plane;
    int ret = 0;

    switch (pmode) {
    case 0:
        block_fill(frame->data[plane], size, frame->linesize[plane], w, h, ax, ay, pick_above);
        break;
    case 1:
        block_fill(frame->data[plane], size, frame->linesize[plane], w, h, ax, ay, pick_left);
        break;
    case 2:
        {
            int arr1[16];
            int arr2[16];
            uint8_t *top = frame->data[plane] + FFMAX(ay - 1, 0) * frame->linesize[plane] + ax;
            uint8_t *left = frame->data[plane] + ay * frame->linesize[plane] + FFMAX(ax - 1, 0);
            int bottommost = frame->data[plane][(ay + size - 1) * frame->linesize[plane] + FFMAX(ax - 1, 0)];
            int rightmost = frame->data[plane][FFMAX(ay - 1, 0) * frame->linesize[plane] + ax + size - 1];
            int avg = (bottommost + rightmost + 1) / 2 + 2 * av_clip(get_se_golomb(gb), -(1<<16), 1<<16);
            int r6 = adjust(avg - bottommost, size);
            int r9 = adjust(avg - rightmost, size);
            int shift = adjust(size, size) == 8 ? 3 : 2;
            uint8_t *block;

            for (int x = 0; x < size; x++) {
                int val = top[x];
                arr1[x] = adjust(((bottommost - val) * (1 << shift)) + r6 * (x + 1), size);
            }

            for (int y = 0; y < size; y++) {
                int val = left[y * frame->linesize[plane]];
                arr2[y] = adjust(((rightmost - val) * (1 << shift)) + r9 * (y + 1), size);
            }

            block = frame->data[plane] + ay * frame->linesize[plane] + ax;
            for (int y = 0; y < size; y++) {
                for (int x = 0; x < size; x++) {
                    block[x] = (((top[x] + left[0] + ((arr1[x] * (y + 1) +
                                                       arr2[y] * (x + 1)) >> 2 * shift)) + 1) / 2) & 0xFF;
                }
                block += frame->linesize[plane];
                left  += frame->linesize[plane];
            }
        }
        break;
    case 3:
        {
            uint8_t fill;

            if (ax == 0 && ay == 0) {
                fill = 0x80;
            } else if (ax >= 1 && ay >= 1) {
                int left = block_sum(frame->data[plane] + ay * frame->linesize[plane] + ax - 1,
                                     1, size, frame->linesize[plane]);
                int top  = block_sum(frame->data[plane] + (ay - 1) * frame->linesize[plane] + ax,
                                     size, 1, frame->linesize[plane]);

                fill = ((left + top) * 2 / (2 * size) + 1) / 2;
            } else if (ax >= 1) {
                fill = (block_sum(frame->data[plane] + ay * frame->linesize[plane] + ax - 1,
                                  1, size, frame->linesize[plane]) * 2 / size + 1) / 2;
            } else if (ay >= 1) {
                fill = (block_sum(frame->data[plane] + (ay - 1) * frame->linesize[plane] + ax,
                                  size, 1, frame->linesize[plane]) * 2 / size + 1) / 2;
            } else {
                return -1;
            }

            block_fill_simple(frame->data[plane] + ay * frame->linesize[plane] + ax,
                              size, frame->linesize[plane], fill);
        }
        break;
    case 4:
        block_fill(frame->data[plane], size, frame->linesize[plane], w, h, ax, ay, pick_4);
        break;
    case 5:
        block_fill(frame->data[plane], size, frame->linesize[plane], w, h, ax, ay, pick_5);
        break;
    case 6:
        block_fill(frame->data[plane], size, frame->linesize[plane], w, h, ax, ay, pick_6);
        break;
    case 7:
        block_fill(frame->data[plane], size, frame->linesize[plane], w, h, ax, ay, pick_7);
        break;
    case 8:
        block_fill(frame->data[plane], size, frame->linesize[plane], w, h, ax, ay, pick_8);
        break;
    }

    if (add_coeffs)
        ret = add_coefficients(avctx, frame, ax, ay, size, plane);

    return ret;
}

static int get_prediction(AVCodecContext *avctx, int x, int y, int size)
{
    MobiClipContext *s = avctx->priv_data;
    GetBitContext *gb = &s->gb;
    int index = (y & 0xC) | (x / 4 % 4);

    uint8_t val = FFMIN(s->pre[index], index % 4 == 0 ? 9 : s->pre[index + 3]);
    if (val == 9)
        val = 3;

    if (!get_bits1(gb)) {
        int x = get_bits(gb, 3);
        val = x + (x >= val ? 1 : 0);
    }

    s->pre[index + 4] = val;
    if (size == 8)
        s->pre[index + 5] = s->pre[index + 8] = s->pre[index + 9] = val;

    return val;
}

static int process_block(AVCodecContext *avctx, AVFrame *frame,
                         int x, int y, int pmode, int has_coeffs, int plane)
{
    MobiClipContext *s = avctx->priv_data;
    GetBitContext *gb = &s->gb;
    int tmp, ret;

    if (!has_coeffs) {
        if (pmode < 0)
            pmode = get_prediction(avctx, x, y, 8);
        return predict_intra(avctx, frame, x, y, pmode, 0, 8, plane);
    }

    tmp = get_ue_golomb_31(gb);
    if ((unsigned)tmp > FF_ARRAY_ELEMS(block4x4_coefficients_tab))
        return AVERROR_INVALIDDATA;

    if (tmp == 0) {
        if (pmode < 0)
            pmode = get_prediction(avctx, x, y, 8);
        ret = predict_intra(avctx, frame, x, y, pmode, 1, 8, plane);
    } else {
        int flags = block4x4_coefficients_tab[tmp - 1];

        for (int by = y; by < y + 8; by += 4) {
            for (int bx = x; bx < x + 8; bx += 4) {
                int new_pmode = pmode;

                if (new_pmode < 0)
                    new_pmode = get_prediction(avctx, bx, by, 4);
                ret = predict_intra(avctx, frame, bx, by, new_pmode, flags & 1, 4, plane);
                if (ret < 0)
                    return ret;
                flags >>= 1;
            }
        }
    }

    return ret;
}

static int decode_macroblock(AVCodecContext *avctx, AVFrame *frame,
                             int x, int y, int predict)
{
    MobiClipContext *s = avctx->priv_data;
    GetBitContext *gb = &s->gb;
    int flags, pmode_uv, idx = get_ue_golomb(gb);
    int ret = 0;

    if (idx < 0 || idx >= FF_ARRAY_ELEMS(block8x8_coefficients_tab))
        return AVERROR_INVALIDDATA;

    flags = block8x8_coefficients_tab[idx];

    if (predict) {
        ret = process_block(avctx, frame, x, y, -1, flags & 1, 0);
        if (ret < 0)
            return ret;
        flags >>= 1;
        ret = process_block(avctx, frame, x + 8, y, -1, flags & 1, 0);
        if (ret < 0)
            return ret;
        flags >>= 1;
        ret = process_block(avctx, frame, x, y + 8, -1, flags & 1, 0);
        if (ret < 0)
            return ret;
        flags >>= 1;
        ret = process_block(avctx, frame, x + 8, y + 8, -1, flags & 1, 0);
        if (ret < 0)
            return ret;
        flags >>= 1;
    } else {
        int pmode = get_bits(gb, 3);

        if (pmode == 2) {
            ret = predict_intra(avctx, frame, x, y, pmode, 0, 16, 0);
            if (ret < 0)
                return ret;
            pmode = 9;
        }

        ret = process_block(avctx, frame, x, y, pmode, flags & 1, 0);
        if (ret < 0)
            return ret;
        flags >>= 1;
        ret = process_block(avctx, frame, x + 8, y, pmode, flags & 1, 0);
        if (ret < 0)
            return ret;
        flags >>= 1;
        ret = process_block(avctx, frame, x, y + 8, pmode, flags & 1, 0);
        if (ret < 0)
            return ret;
        flags >>= 1;
        ret = process_block(avctx, frame, x + 8, y + 8, pmode, flags & 1, 0);
        if (ret < 0)
            return ret;
        flags >>= 1;
    }

    pmode_uv = get_bits(gb, 3);
    if (pmode_uv == 2) {
        ret = predict_intra(avctx, frame, x >> 1, y >> 1, pmode_uv, 0, 8, 1 + !s->moflex);
        if (ret < 0)
            return ret;
        ret = predict_intra(avctx, frame, x >> 1, y >> 1, pmode_uv, 0, 8, 2 - !s->moflex);
        if (ret < 0)
            return ret;
        pmode_uv = 9;
    }

    ret = process_block(avctx, frame, x >> 1, y >> 1, pmode_uv, flags & 1, 1 + !s->moflex);
    if (ret < 0)
        return ret;
    flags >>= 1;
    ret = process_block(avctx, frame, x >> 1, y >> 1, pmode_uv, flags & 1, 2 - !s->moflex);
    if (ret < 0)
        return ret;

    return 0;
}

static int get_index(int x)
{
    return x == 16 ? 0 : x == 8 ? 1 : x == 4 ? 2 : x == 2 ? 3 : 0;
}

static int predict_motion(AVCodecContext *avctx,
                          int width, int height, int index,
                          int offsetm, int offsetx, int offsety)
{
    MobiClipContext *s = avctx->priv_data;
    MotionXY *motion = s->motion;
    GetBitContext *gb = &s->gb;
    int fheight = avctx->height;
    int fwidth = avctx->width;

    if (index <= 5) {
        int sidx = -FFMAX(1, index) + s->current_pic;
        MotionXY mv = s->motion[0];

        if (sidx < 0)
            sidx += 6;

        if (index > 0) {
            mv.x = mv.x + (unsigned)get_se_golomb(gb);
            mv.y = mv.y + (unsigned)get_se_golomb(gb);
        }
        if (mv.x >= INT_MAX || mv.y >= INT_MAX)
            return AVERROR_INVALIDDATA;

        motion[offsetm].x = mv.x;
        motion[offsetm].y = mv.y;

        for (int i = 0; i < 3; i++) {
            int method, src_linesize, dst_linesize;
            uint8_t *src, *dst;

            if (i == 1) {
                offsetx = offsetx >> 1;
                offsety = offsety >> 1;
                mv.x = mv.x >> 1;
                mv.y = mv.y >> 1;
                width = width >> 1;
                height = height >> 1;
                fwidth = fwidth >> 1;
                fheight = fheight >> 1;
            }

            av_assert0(s->pic[sidx]);
            av_assert0(s->pic[s->current_pic]);
            av_assert0(s->pic[s->current_pic]->data[i]);
            if (!s->pic[sidx]->data[i])
                return AVERROR_INVALIDDATA;

            method = (mv.x & 1) | ((mv.y & 1) << 1);
            src_linesize = s->pic[sidx]->linesize[i];
            dst_linesize = s->pic[s->current_pic]->linesize[i];
            dst = s->pic[s->current_pic]->data[i] + offsetx + offsety * dst_linesize;

            if (offsetx + (mv.x >> 1) < 0 ||
                offsety + (mv.y >> 1) < 0 ||
                offsetx + width  + (mv.x + 1 >> 1) > fwidth ||
                offsety + height + (mv.y + 1 >> 1) > fheight)
                return AVERROR_INVALIDDATA;

            switch (method) {
            case 0:
                src = s->pic[sidx]->data[i] + offsetx + (mv.x >> 1) +
                               (offsety + (mv.y >> 1)) * src_linesize;
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++)
                        dst[x] = src[x];
                    dst += dst_linesize;
                    src += src_linesize;
                }
                break;
            case 1:
                src = s->pic[sidx]->data[i] + offsetx + (mv.x >> 1) +
                               (offsety + (mv.y >> 1)) * src_linesize;
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++) {
                        dst[x] = (uint8_t)((src[x] >> 1) + (src[x + 1] >> 1));
                    }

                    dst += dst_linesize;
                    src += src_linesize;
                }
                break;
            case 2:
                src = s->pic[sidx]->data[i] + offsetx + (mv.x >> 1) +
                               (offsety + (mv.y >> 1)) * src_linesize;
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++) {
                        dst[x] = (uint8_t)((src[x] >> 1) + (src[x + src_linesize] >> 1));
                    }

                    dst += dst_linesize;
                    src += src_linesize;
                }
                break;
            case 3:
                src = s->pic[sidx]->data[i] + offsetx + (mv.x >> 1) +
                               (offsety + (mv.y >> 1)) * src_linesize;
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++) {
                        dst[x] = (uint8_t)((((src[x] >> 1) + (src[x + 1] >> 1)) >> 1) +
                                           (((src[x + src_linesize] >> 1) + (src[x + 1 + src_linesize] >> 1)) >> 1));
                    }

                    dst += dst_linesize;
                    src += src_linesize;
                }
                break;
            }
        }
    } else {
        int tidx;
        int adjx = index == 8 ? 0 :  width / 2;
        int adjy = index == 8 ? height / 2 : 0;

        width  = width  - adjx;
        height = height - adjy;
        tidx = get_index(height) * 4 + get_index(width);

        for (int i = 0; i < 2; i++) {
            int ret, idx2;

            idx2 = get_vlc2(gb, mv_vlc[s->moflex][tidx].table,
                            MOBI_MV_VLC_BITS, 1);

            ret = predict_motion(avctx, width, height, idx2,
                                 offsetm, offsetx + i * adjx, offsety + i * adjy);
            if (ret < 0)
                return ret;
        }
    }

    return 0;
}

static int mobiclip_decode(AVCodecContext *avctx, AVFrame *rframe,
                           int *got_frame, AVPacket *pkt)
{
    MobiClipContext *s = avctx->priv_data;
    GetBitContext *gb = &s->gb;
    AVFrame *frame = s->pic[s->current_pic];
    int ret;

    if (avctx->height/16 * (avctx->width/16) * 2 > 8LL*FFALIGN(pkt->size, 2))
        return AVERROR_INVALIDDATA;

    av_fast_padded_malloc(&s->bitstream, &s->bitstream_size,
                          pkt->size);

    if ((ret = ff_reget_buffer(avctx, frame, 0)) < 0)
        return ret;

    s->bdsp.bswap16_buf((uint16_t *)s->bitstream,
                        (uint16_t *)pkt->data,
                        (pkt->size + 1) >> 1);

    ret = init_get_bits8(gb, s->bitstream, FFALIGN(pkt->size, 2));
    if (ret < 0)
        return ret;

    if (get_bits1(gb)) {
        frame->pict_type = AV_PICTURE_TYPE_I;
        frame->key_frame = 1;
        s->moflex = get_bits1(gb);
        s->dct_tab_idx = get_bits1(gb);

        ret = setup_qtables(avctx, get_bits(gb, 6));
        if (ret < 0)
            return ret;

        for (int y = 0; y < avctx->height; y += 16) {
            for (int x = 0; x < avctx->width; x += 16) {
                ret = decode_macroblock(avctx, frame, x, y, get_bits1(gb));
                if (ret < 0)
                    return ret;
            }
        }
    } else {
        MotionXY *motion = s->motion;

        memset(motion, 0, s->motion_size);

        frame->pict_type = AV_PICTURE_TYPE_P;
        frame->key_frame = 0;
        s->dct_tab_idx = 0;

        ret = setup_qtables(avctx, s->quantizer + (int64_t)get_se_golomb(gb));
        if (ret < 0)
            return ret;

        for (int y = 0; y < avctx->height; y += 16) {
            for (int x = 0; x < avctx->width; x += 16) {
                int idx;

                motion[0].x = mid_pred(motion[x / 16 + 1].x, motion[x / 16 + 2].x, motion[x / 16 + 3].x);
                motion[0].y = mid_pred(motion[x / 16 + 1].y, motion[x / 16 + 2].y, motion[x / 16 + 3].y);
                motion[x / 16 + 2].x = 0;
                motion[x / 16 + 2].y = 0;

                idx = get_vlc2(gb, mv_vlc[s->moflex][0].table,
                                   MOBI_MV_VLC_BITS, 1);

                if (idx == 6 || idx == 7) {
                    ret = decode_macroblock(avctx, frame, x, y, idx == 7);
                    if (ret < 0)
                        return ret;
                } else {
                    int flags, idx2;
                    ret = predict_motion(avctx, 16, 16, idx, x / 16 + 2, x, y);
                    if (ret < 0)
                        return ret;
                    idx2 = get_ue_golomb(gb);
                    if (idx2 >= FF_ARRAY_ELEMS(pframe_block8x8_coefficients_tab))
                        return AVERROR_INVALIDDATA;
                    flags = pframe_block8x8_coefficients_tab[idx2];

                    for (int sy = y; sy < y + 16; sy += 8) {
                        for (int sx = x; sx < x + 16; sx += 8) {
                            if (flags & 1)
                                add_pframe_coefficients(avctx, frame, sx, sy, 8, 0);
                            flags >>= 1;
                        }
                    }

                    if (flags & 1)
                        add_pframe_coefficients(avctx, frame, x >> 1, y >> 1, 8, 1 + !s->moflex);
                    flags >>= 1;
                    if (flags & 1)
                        add_pframe_coefficients(avctx, frame, x >> 1, y >> 1, 8, 2 - !s->moflex);
                }
            }
        }
    }

    if (!s->moflex)
        avctx->colorspace = AVCOL_SPC_YCGCO;

    s->current_pic = (s->current_pic + 1) % 6;
    ret = av_frame_ref(rframe, frame);
    if (ret < 0)
        return ret;
    *got_frame = 1;

    return 0;
}

static void mobiclip_flush(AVCodecContext *avctx)
{
    MobiClipContext *s = avctx->priv_data;

    for (int i = 0; i < 6; i++)
        av_frame_unref(s->pic[i]);
}

static av_cold int mobiclip_close(AVCodecContext *avctx)
{
    MobiClipContext *s = avctx->priv_data;

    av_freep(&s->bitstream);
    s->bitstream_size = 0;
    av_freep(&s->motion);
    s->motion_size = 0;

    for (int i = 0; i < 6; i++) {
        av_frame_free(&s->pic[i]);
    }

    return 0;
}

const FFCodec ff_mobiclip_decoder = {
    .p.name         = "mobiclip",
    CODEC_LONG_NAME("MobiClip Video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MOBICLIP,
    .priv_data_size = sizeof(MobiClipContext),
    .init           = mobiclip_init,
    FF_CODEC_DECODE_CB(mobiclip_decode),
    .flush          = mobiclip_flush,
    .close          = mobiclip_close,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
