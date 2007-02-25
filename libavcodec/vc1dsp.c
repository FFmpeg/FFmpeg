/*
 * VC-1 and WMV3 decoder - DSP functions
 * Copyright (c) 2006 Konstantin Shishkov
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
 *
 */

/**
* @file vc1dsp.c
 * VC-1 and WMV3 decoder
 *
 */

#include "dsputil.h"


/** Apply overlap transform to horizontal edge
*/
static void vc1_v_overlap_c(uint8_t* src, int stride)
{
    int i;
    int a, b, c, d;
    int d1, d2;
    int rnd = 1;
    for(i = 0; i < 8; i++) {
        a = src[-2*stride];
        b = src[-stride];
        c = src[0];
        d = src[stride];
        d1 = (a - d + 3 + rnd) >> 3;
        d2 = (a - d + b - c + 4 - rnd) >> 3;

        src[-2*stride] = a - d1;
        src[-stride] = b - d2;
        src[0] = c + d2;
        src[stride] = d + d1;
        src++;
        rnd = !rnd;
    }
}

/** Apply overlap transform to vertical edge
*/
static void vc1_h_overlap_c(uint8_t* src, int stride)
{
    int i;
    int a, b, c, d;
    int d1, d2;
    int rnd = 1;
    for(i = 0; i < 8; i++) {
        a = src[-2];
        b = src[-1];
        c = src[0];
        d = src[1];
        d1 = (a - d + 3 + rnd) >> 3;
        d2 = (a - d + b - c + 4 - rnd) >> 3;

        src[-2] = a - d1;
        src[-1] = b - d2;
        src[0] = c + d2;
        src[1] = d + d1;
        src += stride;
        rnd = !rnd;
    }
}


/** Do inverse transform on 8x8 block
*/
static void vc1_inv_trans_8x8_c(DCTELEM block[64])
{
    int i;
    register int t1,t2,t3,t4,t5,t6,t7,t8;
    DCTELEM *src, *dst;

    src = block;
    dst = block;
    for(i = 0; i < 8; i++){
        t1 = 12 * (src[0] + src[4]);
        t2 = 12 * (src[0] - src[4]);
        t3 = 16 * src[2] +  6 * src[6];
        t4 =  6 * src[2] - 16 * src[6];

        t5 = t1 + t3;
        t6 = t2 + t4;
        t7 = t2 - t4;
        t8 = t1 - t3;

        t1 = 16 * src[1] + 15 * src[3] +  9 * src[5] +  4 * src[7];
        t2 = 15 * src[1] -  4 * src[3] - 16 * src[5] -  9 * src[7];
        t3 =  9 * src[1] - 16 * src[3] +  4 * src[5] + 15 * src[7];
        t4 =  4 * src[1] -  9 * src[3] + 15 * src[5] - 16 * src[7];

        dst[0] = (t5 + t1 + 4) >> 3;
        dst[1] = (t6 + t2 + 4) >> 3;
        dst[2] = (t7 + t3 + 4) >> 3;
        dst[3] = (t8 + t4 + 4) >> 3;
        dst[4] = (t8 - t4 + 4) >> 3;
        dst[5] = (t7 - t3 + 4) >> 3;
        dst[6] = (t6 - t2 + 4) >> 3;
        dst[7] = (t5 - t1 + 4) >> 3;

        src += 8;
        dst += 8;
    }

    src = block;
    dst = block;
    for(i = 0; i < 8; i++){
        t1 = 12 * (src[ 0] + src[32]);
        t2 = 12 * (src[ 0] - src[32]);
        t3 = 16 * src[16] +  6 * src[48];
        t4 =  6 * src[16] - 16 * src[48];

        t5 = t1 + t3;
        t6 = t2 + t4;
        t7 = t2 - t4;
        t8 = t1 - t3;

        t1 = 16 * src[ 8] + 15 * src[24] +  9 * src[40] +  4 * src[56];
        t2 = 15 * src[ 8] -  4 * src[24] - 16 * src[40] -  9 * src[56];
        t3 =  9 * src[ 8] - 16 * src[24] +  4 * src[40] + 15 * src[56];
        t4 =  4 * src[ 8] -  9 * src[24] + 15 * src[40] - 16 * src[56];

        dst[ 0] = (t5 + t1 + 64) >> 7;
        dst[ 8] = (t6 + t2 + 64) >> 7;
        dst[16] = (t7 + t3 + 64) >> 7;
        dst[24] = (t8 + t4 + 64) >> 7;
        dst[32] = (t8 - t4 + 64 + 1) >> 7;
        dst[40] = (t7 - t3 + 64 + 1) >> 7;
        dst[48] = (t6 - t2 + 64 + 1) >> 7;
        dst[56] = (t5 - t1 + 64 + 1) >> 7;

        src++;
        dst++;
    }
}

/** Do inverse transform on 8x4 part of block
*/
static void vc1_inv_trans_8x4_c(DCTELEM block[64], int n)
{
    int i;
    register int t1,t2,t3,t4,t5,t6,t7,t8;
    DCTELEM *src, *dst;
    int off;

    off = n * 32;
    src = block + off;
    dst = block + off;
    for(i = 0; i < 4; i++){
        t1 = 12 * (src[0] + src[4]);
        t2 = 12 * (src[0] - src[4]);
        t3 = 16 * src[2] +  6 * src[6];
        t4 =  6 * src[2] - 16 * src[6];

        t5 = t1 + t3;
        t6 = t2 + t4;
        t7 = t2 - t4;
        t8 = t1 - t3;

        t1 = 16 * src[1] + 15 * src[3] +  9 * src[5] +  4 * src[7];
        t2 = 15 * src[1] -  4 * src[3] - 16 * src[5] -  9 * src[7];
        t3 =  9 * src[1] - 16 * src[3] +  4 * src[5] + 15 * src[7];
        t4 =  4 * src[1] -  9 * src[3] + 15 * src[5] - 16 * src[7];

        dst[0] = (t5 + t1 + 4) >> 3;
        dst[1] = (t6 + t2 + 4) >> 3;
        dst[2] = (t7 + t3 + 4) >> 3;
        dst[3] = (t8 + t4 + 4) >> 3;
        dst[4] = (t8 - t4 + 4) >> 3;
        dst[5] = (t7 - t3 + 4) >> 3;
        dst[6] = (t6 - t2 + 4) >> 3;
        dst[7] = (t5 - t1 + 4) >> 3;

        src += 8;
        dst += 8;
    }

    src = block + off;
    dst = block + off;
    for(i = 0; i < 8; i++){
        t1 = 17 * (src[ 0] + src[16]);
        t2 = 17 * (src[ 0] - src[16]);
        t3 = 22 * src[ 8];
        t4 = 22 * src[24];
        t5 = 10 * src[ 8];
        t6 = 10 * src[24];

        dst[ 0] = (t1 + t3 + t6 + 64) >> 7;
        dst[ 8] = (t2 - t4 + t5 + 64) >> 7;
        dst[16] = (t2 + t4 - t5 + 64) >> 7;
        dst[24] = (t1 - t3 - t6 + 64) >> 7;

        src ++;
        dst ++;
    }
}

/** Do inverse transform on 4x8 parts of block
*/
static void vc1_inv_trans_4x8_c(DCTELEM block[64], int n)
{
    int i;
    register int t1,t2,t3,t4,t5,t6,t7,t8;
    DCTELEM *src, *dst;
    int off;

    off = n * 4;
    src = block + off;
    dst = block + off;
    for(i = 0; i < 8; i++){
        t1 = 17 * (src[0] + src[2]);
        t2 = 17 * (src[0] - src[2]);
        t3 = 22 * src[1];
        t4 = 22 * src[3];
        t5 = 10 * src[1];
        t6 = 10 * src[3];

        dst[0] = (t1 + t3 + t6 + 4) >> 3;
        dst[1] = (t2 - t4 + t5 + 4) >> 3;
        dst[2] = (t2 + t4 - t5 + 4) >> 3;
        dst[3] = (t1 - t3 - t6 + 4) >> 3;

        src += 8;
        dst += 8;
    }

    src = block + off;
    dst = block + off;
    for(i = 0; i < 4; i++){
        t1 = 12 * (src[ 0] + src[32]);
        t2 = 12 * (src[ 0] - src[32]);
        t3 = 16 * src[16] +  6 * src[48];
        t4 =  6 * src[16] - 16 * src[48];

        t5 = t1 + t3;
        t6 = t2 + t4;
        t7 = t2 - t4;
        t8 = t1 - t3;

        t1 = 16 * src[ 8] + 15 * src[24] +  9 * src[40] +  4 * src[56];
        t2 = 15 * src[ 8] -  4 * src[24] - 16 * src[40] -  9 * src[56];
        t3 =  9 * src[ 8] - 16 * src[24] +  4 * src[40] + 15 * src[56];
        t4 =  4 * src[ 8] -  9 * src[24] + 15 * src[40] - 16 * src[56];

        dst[ 0] = (t5 + t1 + 64) >> 7;
        dst[ 8] = (t6 + t2 + 64) >> 7;
        dst[16] = (t7 + t3 + 64) >> 7;
        dst[24] = (t8 + t4 + 64) >> 7;
        dst[32] = (t8 - t4 + 64 + 1) >> 7;
        dst[40] = (t7 - t3 + 64 + 1) >> 7;
        dst[48] = (t6 - t2 + 64 + 1) >> 7;
        dst[56] = (t5 - t1 + 64 + 1) >> 7;

        src++;
        dst++;
    }
}

/** Do inverse transform on 4x4 part of block
*/
static void vc1_inv_trans_4x4_c(DCTELEM block[64], int n)
{
    int i;
    register int t1,t2,t3,t4,t5,t6;
    DCTELEM *src, *dst;
    int off;

    off = (n&1) * 4 + (n&2) * 16;
    src = block + off;
    dst = block + off;
    for(i = 0; i < 4; i++){
        t1 = 17 * (src[0] + src[2]);
        t2 = 17 * (src[0] - src[2]);
        t3 = 22 * src[1];
        t4 = 22 * src[3];
        t5 = 10 * src[1];
        t6 = 10 * src[3];

        dst[0] = (t1 + t3 + t6 + 4) >> 3;
        dst[1] = (t2 - t4 + t5 + 4) >> 3;
        dst[2] = (t2 + t4 - t5 + 4) >> 3;
        dst[3] = (t1 - t3 - t6 + 4) >> 3;

        src += 8;
        dst += 8;
    }

    src = block + off;
    dst = block + off;
    for(i = 0; i < 4; i++){
        t1 = 17 * (src[ 0] + src[16]);
        t2 = 17 * (src[ 0] - src[16]);
        t3 = 22 * src[ 8];
        t4 = 22 * src[24];
        t5 = 10 * src[ 8];
        t6 = 10 * src[24];

        dst[ 0] = (t1 + t3 + t6 + 64) >> 7;
        dst[ 8] = (t2 - t4 + t5 + 64) >> 7;
        dst[16] = (t2 + t4 - t5 + 64) >> 7;
        dst[24] = (t1 - t3 - t6 + 64) >> 7;

        src ++;
        dst ++;
    }
}

/* motion compensation functions */

/** Filter used to interpolate fractional pel values
 */
static av_always_inline int vc1_mspel_filter(const uint8_t *src, int stride, int mode, int r)
{
    switch(mode){
    case 0: //no shift
        return src[0];
    case 1: // 1/4 shift
        return (-4*src[-stride] + 53*src[0] + 18*src[stride] - 3*src[stride*2] + 32 - r) >> 6;
    case 2: // 1/2 shift
        return (-src[-stride] + 9*src[0] + 9*src[stride] - src[stride*2] + 8 - r) >> 4;
    case 3: // 3/4 shift
        return (-3*src[-stride] + 18*src[0] + 53*src[stride] - 4*src[stride*2] + 32 - r) >> 6;
    }
    return 0; //should not occur
}

/** Function used to do motion compensation with bicubic interpolation
 */
static void vc1_mspel_mc(uint8_t *dst, const uint8_t *src, int stride, int mode, int rnd)
{
    int i, j;
    uint8_t tmp[8*11], *tptr;
    int m, r;

    m = (mode & 3);
    r = rnd;
    src -= stride;
    tptr = tmp;
    for(j = 0; j < 11; j++) {
        for(i = 0; i < 8; i++)
            tptr[i] = av_clip_uint8(vc1_mspel_filter(src + i, 1, m, r));
        src += stride;
        tptr += 8;
    }
    r = 1 - rnd;
    m = (mode >> 2) & 3;

    tptr = tmp + 8;
    for(j = 0; j < 8; j++) {
        for(i = 0; i < 8; i++)
            dst[i] = av_clip_uint8(vc1_mspel_filter(tptr + i, 8, m, r));
        dst += stride;
        tptr += 8;
    }
}

/* pixel functions - really are entry points to vc1_mspel_mc */

/* this one is defined in dsputil.c */
void ff_put_vc1_mspel_mc00_c(uint8_t *dst, const uint8_t *src, int stride, int rnd);

static void ff_put_vc1_mspel_mc10_c(uint8_t *dst, const uint8_t *src, int stride, int rnd) {
    vc1_mspel_mc(dst, src, stride, 0x1, rnd);
}

static void ff_put_vc1_mspel_mc20_c(uint8_t *dst, const uint8_t *src, int stride, int rnd) {
    vc1_mspel_mc(dst, src, stride, 0x2, rnd);
}

static void ff_put_vc1_mspel_mc30_c(uint8_t *dst, const uint8_t *src, int stride, int rnd) {
    vc1_mspel_mc(dst, src, stride, 0x3, rnd);
}

static void ff_put_vc1_mspel_mc01_c(uint8_t *dst, const uint8_t *src, int stride, int rnd) {
    vc1_mspel_mc(dst, src, stride, 0x4, rnd);
}

static void ff_put_vc1_mspel_mc11_c(uint8_t *dst, const uint8_t *src, int stride, int rnd) {
    vc1_mspel_mc(dst, src, stride, 0x5, rnd);
}

static void ff_put_vc1_mspel_mc21_c(uint8_t *dst, const uint8_t *src, int stride, int rnd) {
    vc1_mspel_mc(dst, src, stride, 0x6, rnd);
}

static void ff_put_vc1_mspel_mc31_c(uint8_t *dst, const uint8_t *src, int stride, int rnd) {
    vc1_mspel_mc(dst, src, stride, 0x7, rnd);
}

static void ff_put_vc1_mspel_mc02_c(uint8_t *dst, const uint8_t *src, int stride, int rnd) {
    vc1_mspel_mc(dst, src, stride, 0x8, rnd);
}

static void ff_put_vc1_mspel_mc12_c(uint8_t *dst, const uint8_t *src, int stride, int rnd) {
    vc1_mspel_mc(dst, src, stride, 0x9, rnd);
}

static void ff_put_vc1_mspel_mc22_c(uint8_t *dst, const uint8_t *src, int stride, int rnd) {
    vc1_mspel_mc(dst, src, stride, 0xA, rnd);
}

static void ff_put_vc1_mspel_mc32_c(uint8_t *dst, const uint8_t *src, int stride, int rnd) {
    vc1_mspel_mc(dst, src, stride, 0xB, rnd);
}

static void ff_put_vc1_mspel_mc03_c(uint8_t *dst, const uint8_t *src, int stride, int rnd) {
    vc1_mspel_mc(dst, src, stride, 0xC, rnd);
}

static void ff_put_vc1_mspel_mc13_c(uint8_t *dst, const uint8_t *src, int stride, int rnd) {
    vc1_mspel_mc(dst, src, stride, 0xD, rnd);
}

static void ff_put_vc1_mspel_mc23_c(uint8_t *dst, const uint8_t *src, int stride, int rnd) {
    vc1_mspel_mc(dst, src, stride, 0xE, rnd);
}

static void ff_put_vc1_mspel_mc33_c(uint8_t *dst, const uint8_t *src, int stride, int rnd) {
    vc1_mspel_mc(dst, src, stride, 0xF, rnd);
}

void ff_vc1dsp_init(DSPContext* dsp, AVCodecContext *avctx) {
    dsp->vc1_inv_trans_8x8 = vc1_inv_trans_8x8_c;
    dsp->vc1_inv_trans_4x8 = vc1_inv_trans_4x8_c;
    dsp->vc1_inv_trans_8x4 = vc1_inv_trans_8x4_c;
    dsp->vc1_inv_trans_4x4 = vc1_inv_trans_4x4_c;
    dsp->vc1_h_overlap = vc1_h_overlap_c;
    dsp->vc1_v_overlap = vc1_v_overlap_c;

    dsp->put_vc1_mspel_pixels_tab[ 0] = ff_put_vc1_mspel_mc00_c;
    dsp->put_vc1_mspel_pixels_tab[ 1] = ff_put_vc1_mspel_mc10_c;
    dsp->put_vc1_mspel_pixels_tab[ 2] = ff_put_vc1_mspel_mc20_c;
    dsp->put_vc1_mspel_pixels_tab[ 3] = ff_put_vc1_mspel_mc30_c;
    dsp->put_vc1_mspel_pixels_tab[ 4] = ff_put_vc1_mspel_mc01_c;
    dsp->put_vc1_mspel_pixels_tab[ 5] = ff_put_vc1_mspel_mc11_c;
    dsp->put_vc1_mspel_pixels_tab[ 6] = ff_put_vc1_mspel_mc21_c;
    dsp->put_vc1_mspel_pixels_tab[ 7] = ff_put_vc1_mspel_mc31_c;
    dsp->put_vc1_mspel_pixels_tab[ 8] = ff_put_vc1_mspel_mc02_c;
    dsp->put_vc1_mspel_pixels_tab[ 9] = ff_put_vc1_mspel_mc12_c;
    dsp->put_vc1_mspel_pixels_tab[10] = ff_put_vc1_mspel_mc22_c;
    dsp->put_vc1_mspel_pixels_tab[11] = ff_put_vc1_mspel_mc32_c;
    dsp->put_vc1_mspel_pixels_tab[12] = ff_put_vc1_mspel_mc03_c;
    dsp->put_vc1_mspel_pixels_tab[13] = ff_put_vc1_mspel_mc13_c;
    dsp->put_vc1_mspel_pixels_tab[14] = ff_put_vc1_mspel_mc23_c;
    dsp->put_vc1_mspel_pixels_tab[15] = ff_put_vc1_mspel_mc33_c;
}
