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
        t1 = 12 * (src[0] + src[4]) + 4;
        t2 = 12 * (src[0] - src[4]) + 4;
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

        dst[0] = (t5 + t1) >> 3;
        dst[1] = (t6 + t2) >> 3;
        dst[2] = (t7 + t3) >> 3;
        dst[3] = (t8 + t4) >> 3;
        dst[4] = (t8 - t4) >> 3;
        dst[5] = (t7 - t3) >> 3;
        dst[6] = (t6 - t2) >> 3;
        dst[7] = (t5 - t1) >> 3;

        src += 8;
        dst += 8;
    }

    src = block;
    dst = block;
    for(i = 0; i < 8; i++){
        t1 = 12 * (src[ 0] + src[32]) + 64;
        t2 = 12 * (src[ 0] - src[32]) + 64;
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

        dst[ 0] = (t5 + t1) >> 7;
        dst[ 8] = (t6 + t2) >> 7;
        dst[16] = (t7 + t3) >> 7;
        dst[24] = (t8 + t4) >> 7;
        dst[32] = (t8 - t4 + 1) >> 7;
        dst[40] = (t7 - t3 + 1) >> 7;
        dst[48] = (t6 - t2 + 1) >> 7;
        dst[56] = (t5 - t1 + 1) >> 7;

        src++;
        dst++;
    }
}

/** Do inverse transform on 8x4 part of block
*/
static void vc1_inv_trans_8x4_c(uint8_t *dest, int linesize, DCTELEM *block)
{
    int i;
    register int t1,t2,t3,t4,t5,t6,t7,t8;
    DCTELEM *src, *dst;
    const uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;

    src = block;
    dst = block;
    for(i = 0; i < 4; i++){
        t1 = 12 * (src[0] + src[4]) + 4;
        t2 = 12 * (src[0] - src[4]) + 4;
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

        dst[0] = (t5 + t1) >> 3;
        dst[1] = (t6 + t2) >> 3;
        dst[2] = (t7 + t3) >> 3;
        dst[3] = (t8 + t4) >> 3;
        dst[4] = (t8 - t4) >> 3;
        dst[5] = (t7 - t3) >> 3;
        dst[6] = (t6 - t2) >> 3;
        dst[7] = (t5 - t1) >> 3;

        src += 8;
        dst += 8;
    }

    src = block;
    for(i = 0; i < 8; i++){
        t1 = 17 * (src[ 0] + src[16]) + 64;
        t2 = 17 * (src[ 0] - src[16]) + 64;
        t3 = 22 * src[ 8] + 10 * src[24];
        t4 = 22 * src[24] - 10 * src[ 8];

        dest[0*linesize] = cm[dest[0*linesize] + ((t1 + t3) >> 7)];
        dest[1*linesize] = cm[dest[1*linesize] + ((t2 - t4) >> 7)];
        dest[2*linesize] = cm[dest[2*linesize] + ((t2 + t4) >> 7)];
        dest[3*linesize] = cm[dest[3*linesize] + ((t1 - t3) >> 7)];

        src ++;
        dest++;
    }
}

/** Do inverse transform on 4x8 parts of block
*/
static void vc1_inv_trans_4x8_c(uint8_t *dest, int linesize, DCTELEM *block)
{
    int i;
    register int t1,t2,t3,t4,t5,t6,t7,t8;
    DCTELEM *src, *dst;
    const uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;

    src = block;
    dst = block;
    for(i = 0; i < 8; i++){
        t1 = 17 * (src[0] + src[2]) + 4;
        t2 = 17 * (src[0] - src[2]) + 4;
        t3 = 22 * src[1] + 10 * src[3];
        t4 = 22 * src[3] - 10 * src[1];

        dst[0] = (t1 + t3) >> 3;
        dst[1] = (t2 - t4) >> 3;
        dst[2] = (t2 + t4) >> 3;
        dst[3] = (t1 - t3) >> 3;

        src += 8;
        dst += 8;
    }

    src = block;
    for(i = 0; i < 4; i++){
        t1 = 12 * (src[ 0] + src[32]) + 64;
        t2 = 12 * (src[ 0] - src[32]) + 64;
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

        dest[0*linesize] = cm[dest[0*linesize] + ((t5 + t1) >> 7)];
        dest[1*linesize] = cm[dest[1*linesize] + ((t6 + t2) >> 7)];
        dest[2*linesize] = cm[dest[2*linesize] + ((t7 + t3) >> 7)];
        dest[3*linesize] = cm[dest[3*linesize] + ((t8 + t4) >> 7)];
        dest[4*linesize] = cm[dest[4*linesize] + ((t8 - t4 + 1) >> 7)];
        dest[5*linesize] = cm[dest[5*linesize] + ((t7 - t3 + 1) >> 7)];
        dest[6*linesize] = cm[dest[6*linesize] + ((t6 - t2 + 1) >> 7)];
        dest[7*linesize] = cm[dest[7*linesize] + ((t5 - t1 + 1) >> 7)];

        src ++;
        dest++;
    }
}

/** Do inverse transform on 4x4 part of block
*/
static void vc1_inv_trans_4x4_c(uint8_t *dest, int linesize, DCTELEM *block)
{
    int i;
    register int t1,t2,t3,t4;
    DCTELEM *src, *dst;
    const uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;

    src = block;
    dst = block;
    for(i = 0; i < 4; i++){
        t1 = 17 * (src[0] + src[2]) + 4;
        t2 = 17 * (src[0] - src[2]) + 4;
        t3 = 22 * src[1] + 10 * src[3];
        t4 = 22 * src[3] - 10 * src[1];

        dst[0] = (t1 + t3) >> 3;
        dst[1] = (t2 - t4) >> 3;
        dst[2] = (t2 + t4) >> 3;
        dst[3] = (t1 - t3) >> 3;

        src += 8;
        dst += 8;
    }

    src = block;
    for(i = 0; i < 4; i++){
        t1 = 17 * (src[ 0] + src[16]) + 64;
        t2 = 17 * (src[ 0] - src[16]) + 64;
        t3 = 22 * src[ 8] + 10 * src[24];
        t4 = 22 * src[24] - 10 * src[ 8];

        dest[0*linesize] = cm[dest[0*linesize] + ((t1 + t3) >> 7)];
        dest[1*linesize] = cm[dest[1*linesize] + ((t2 - t4) >> 7)];
        dest[2*linesize] = cm[dest[2*linesize] + ((t2 + t4) >> 7)];
        dest[3*linesize] = cm[dest[3*linesize] + ((t1 - t3) >> 7)];

        src ++;
        dest++;
    }
}

/* motion compensation functions */
/** Filter in case of 2 filters */
#define VC1_MSPEL_FILTER_16B(DIR, TYPE)                                 \
static av_always_inline int vc1_mspel_ ## DIR ## _filter_16bits(const TYPE *src, int stride, int mode) \
{                                                                       \
    switch(mode){                                                       \
    case 0: /* no shift - should not occur */                           \
        return 0;                                                       \
    case 1: /* 1/4 shift */                                             \
        return -4*src[-stride] + 53*src[0] + 18*src[stride] - 3*src[stride*2]; \
    case 2: /* 1/2 shift */                                             \
        return -src[-stride] + 9*src[0] + 9*src[stride] - src[stride*2]; \
    case 3: /* 3/4 shift */                                             \
        return -3*src[-stride] + 18*src[0] + 53*src[stride] - 4*src[stride*2]; \
    }                                                                   \
    return 0; /* should not occur */                                    \
}

VC1_MSPEL_FILTER_16B(ver, uint8_t);
VC1_MSPEL_FILTER_16B(hor, int16_t);


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
static void vc1_mspel_mc(uint8_t *dst, const uint8_t *src, int stride, int hmode, int vmode, int rnd)
{
    int     i, j;

    if (vmode) { /* Horizontal filter to apply */
        int r;

        if (hmode) { /* Vertical filter to apply, output to tmp */
            static const int shift_value[] = { 0, 5, 1, 5 };
            int              shift = (shift_value[hmode]+shift_value[vmode])>>1;
            int16_t          tmp[11*8], *tptr = tmp;

            r = (1<<(shift-1)) + rnd-1;

            src -= 1;
            for(j = 0; j < 8; j++) {
                for(i = 0; i < 11; i++)
                    tptr[i] = (vc1_mspel_ver_filter_16bits(src + i, stride, vmode)+r)>>shift;
                src += stride;
                tptr += 11;
            }

            r = 64-rnd;
            tptr = tmp+1;
            for(j = 0; j < 8; j++) {
                for(i = 0; i < 8; i++)
                    dst[i] = av_clip_uint8((vc1_mspel_hor_filter_16bits(tptr + i, 1, hmode)+r)>>7);
                dst += stride;
                tptr += 11;
            }

            return;
        }
        else { /* No horizontal filter, output 8 lines to dst */
            r = 1-rnd;

            for(j = 0; j < 8; j++) {
                for(i = 0; i < 8; i++)
                    dst[i] = av_clip_uint8(vc1_mspel_filter(src + i, stride, vmode, r));
                src += stride;
                dst += stride;
            }
            return;
        }
    }

    /* Horizontal mode with no vertical mode */
    for(j = 0; j < 8; j++) {
        for(i = 0; i < 8; i++)
            dst[i] = av_clip_uint8(vc1_mspel_filter(src + i, 1, hmode, rnd));
        dst += stride;
        src += stride;
    }
}

/* pixel functions - really are entry points to vc1_mspel_mc */

/* this one is defined in dsputil.c */
void ff_put_vc1_mspel_mc00_c(uint8_t *dst, const uint8_t *src, int stride, int rnd);

#define PUT_VC1_MSPEL(a, b)\
static void put_vc1_mspel_mc ## a ## b ##_c(uint8_t *dst, const uint8_t *src, int stride, int rnd) { \
     vc1_mspel_mc(dst, src, stride, a, b, rnd);                         \
}

PUT_VC1_MSPEL(1, 0)
PUT_VC1_MSPEL(2, 0)
PUT_VC1_MSPEL(3, 0)

PUT_VC1_MSPEL(0, 1)
PUT_VC1_MSPEL(1, 1)
PUT_VC1_MSPEL(2, 1)
PUT_VC1_MSPEL(3, 1)

PUT_VC1_MSPEL(0, 2)
PUT_VC1_MSPEL(1, 2)
PUT_VC1_MSPEL(2, 2)
PUT_VC1_MSPEL(3, 2)

PUT_VC1_MSPEL(0, 3)
PUT_VC1_MSPEL(1, 3)
PUT_VC1_MSPEL(2, 3)
PUT_VC1_MSPEL(3, 3)

void ff_vc1dsp_init(DSPContext* dsp, AVCodecContext *avctx) {
    dsp->vc1_inv_trans_8x8 = vc1_inv_trans_8x8_c;
    dsp->vc1_inv_trans_4x8 = vc1_inv_trans_4x8_c;
    dsp->vc1_inv_trans_8x4 = vc1_inv_trans_8x4_c;
    dsp->vc1_inv_trans_4x4 = vc1_inv_trans_4x4_c;
    dsp->vc1_h_overlap = vc1_h_overlap_c;
    dsp->vc1_v_overlap = vc1_v_overlap_c;

    dsp->put_vc1_mspel_pixels_tab[ 0] = ff_put_vc1_mspel_mc00_c;
    dsp->put_vc1_mspel_pixels_tab[ 1] = put_vc1_mspel_mc10_c;
    dsp->put_vc1_mspel_pixels_tab[ 2] = put_vc1_mspel_mc20_c;
    dsp->put_vc1_mspel_pixels_tab[ 3] = put_vc1_mspel_mc30_c;
    dsp->put_vc1_mspel_pixels_tab[ 4] = put_vc1_mspel_mc01_c;
    dsp->put_vc1_mspel_pixels_tab[ 5] = put_vc1_mspel_mc11_c;
    dsp->put_vc1_mspel_pixels_tab[ 6] = put_vc1_mspel_mc21_c;
    dsp->put_vc1_mspel_pixels_tab[ 7] = put_vc1_mspel_mc31_c;
    dsp->put_vc1_mspel_pixels_tab[ 8] = put_vc1_mspel_mc02_c;
    dsp->put_vc1_mspel_pixels_tab[ 9] = put_vc1_mspel_mc12_c;
    dsp->put_vc1_mspel_pixels_tab[10] = put_vc1_mspel_mc22_c;
    dsp->put_vc1_mspel_pixels_tab[11] = put_vc1_mspel_mc32_c;
    dsp->put_vc1_mspel_pixels_tab[12] = put_vc1_mspel_mc03_c;
    dsp->put_vc1_mspel_pixels_tab[13] = put_vc1_mspel_mc13_c;
    dsp->put_vc1_mspel_pixels_tab[14] = put_vc1_mspel_mc23_c;
    dsp->put_vc1_mspel_pixels_tab[15] = put_vc1_mspel_mc33_c;
}
