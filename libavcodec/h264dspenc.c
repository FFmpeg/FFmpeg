/*
 * H.264/MPEG-4 Part 10 (Base profile) encoder.
 *
 * DSP functions
 *
 * Copyright (c) 2006 Expertisecentrum Digitale Media, UHasselt
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
 * @file h264dspenc.c
 * H.264 encoder related DSP utils
 *
 */

#include "dsputil.h"

extern const uint8_t ff_div6[52];
extern const uint8_t ff_rem6[52];

#define  H264_DCT_PART1(X) \
         a = block[0][X]+block[3][X]; \
         c = block[0][X]-block[3][X]; \
         b = block[1][X]+block[2][X]; \
         d = block[1][X]-block[2][X]; \
         pieces[0][X] = a+b; \
         pieces[2][X] = a-b; \
         pieces[1][X] = (c<<1)+d; \
         pieces[3][X] = c-(d<<1);

#define  H264_DCT_PART2(X) \
         a = pieces[X][0]+pieces[X][3]; \
         c = pieces[X][0]-pieces[X][3]; \
         b = pieces[X][1]+pieces[X][2]; \
         d = pieces[X][1]-pieces[X][2]; \
         block[0][X] = a+b; \
         block[2][X] = a-b; \
         block[1][X] = (c<<1)+d; \
         block[3][X] = c-(d<<1);

/**
 * Transform the provided matrix using the H.264 modified DCT.
 * @note
 * we'll always work with transposed input blocks, to avoid having to make a
 * distinction between C and mmx implementations.
 *
 * @param block transposed input block
 */
static void h264_dct_c(DCTELEM block[4][4])
{
    DCTELEM pieces[4][4];
    DCTELEM a, b, c, d;

    H264_DCT_PART1(0);
    H264_DCT_PART1(1);
    H264_DCT_PART1(2);
    H264_DCT_PART1(3);
    H264_DCT_PART2(0);
    H264_DCT_PART2(1);
    H264_DCT_PART2(2);
    H264_DCT_PART2(3);
}

void ff_h264dspenc_init(DSPContext* c, AVCodecContext *avctx)
{
    c->h264_dct = h264_dct_c;
}

