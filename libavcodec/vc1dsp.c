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
 * @file
 * VC-1 and WMV3 decoder
 */

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "h264chroma.h"
#include "qpeldsp.h"
#include "rnd_avg.h"
#include "vc1dsp.h"
#include "startcode.h"

/* Apply overlap transform to horizontal edge */
static void vc1_v_overlap_c(uint8_t *src, int stride)
{
    int i;
    int a, b, c, d;
    int d1, d2;
    int rnd = 1;
    for (i = 0; i < 8; i++) {
        a  = src[-2 * stride];
        b  = src[-stride];
        c  = src[0];
        d  = src[stride];
        d1 = (a - d + 3 + rnd) >> 3;
        d2 = (a - d + b - c + 4 - rnd) >> 3;

        src[-2 * stride] = a - d1;
        src[-stride]     = av_clip_uint8(b - d2);
        src[0]           = av_clip_uint8(c + d2);
        src[stride]      = d + d1;
        src++;
        rnd = !rnd;
    }
}

/* Apply overlap transform to vertical edge */
static void vc1_h_overlap_c(uint8_t *src, int stride)
{
    int i;
    int a, b, c, d;
    int d1, d2;
    int rnd = 1;
    for (i = 0; i < 8; i++) {
        a  = src[-2];
        b  = src[-1];
        c  = src[0];
        d  = src[1];
        d1 = (a - d + 3 + rnd) >> 3;
        d2 = (a - d + b - c + 4 - rnd) >> 3;

        src[-2] = a - d1;
        src[-1] = av_clip_uint8(b - d2);
        src[0]  = av_clip_uint8(c + d2);
        src[1]  = d + d1;
        src    += stride;
        rnd     = !rnd;
    }
}

static void vc1_v_s_overlap_c(int16_t *top, int16_t *bottom)
{
    int i;
    int a, b, c, d;
    int d1, d2;
    int rnd1 = 4, rnd2 = 3;
    for (i = 0; i < 8; i++) {
        a  = top[48];
        b  = top[56];
        c  = bottom[0];
        d  = bottom[8];
        d1 = a - d;
        d2 = a - d + b - c;

        top[48]   = ((a * 8) - d1 + rnd1) >> 3;
        top[56]   = ((b * 8) - d2 + rnd2) >> 3;
        bottom[0] = ((c * 8) + d2 + rnd1) >> 3;
        bottom[8] = ((d * 8) + d1 + rnd2) >> 3;

        bottom++;
        top++;
        rnd2 = 7 - rnd2;
        rnd1 = 7 - rnd1;
    }
}

static void vc1_h_s_overlap_c(int16_t *left, int16_t *right)
{
    int i;
    int a, b, c, d;
    int d1, d2;
    int rnd1 = 4, rnd2 = 3;
    for (i = 0; i < 8; i++) {
        a  = left[6];
        b  = left[7];
        c  = right[0];
        d  = right[1];
        d1 = a - d;
        d2 = a - d + b - c;

        left[6]  = ((a * 8) - d1 + rnd1) >> 3;
        left[7]  = ((b * 8) - d2 + rnd2) >> 3;
        right[0] = ((c * 8) + d2 + rnd1) >> 3;
        right[1] = ((d * 8) + d1 + rnd2) >> 3;

        right += 8;
        left  += 8;
        rnd2   = 7 - rnd2;
        rnd1   = 7 - rnd1;
    }
}

/**
 * VC-1 in-loop deblocking filter for one line
 * @param src source block type
 * @param stride block stride
 * @param pq block quantizer
 * @return whether other 3 pairs should be filtered or not
 * @see 8.6
 */
static av_always_inline int vc1_filter_line(uint8_t *src, int stride, int pq)
{
    int a0 = (2 * (src[-2 * stride] - src[1 * stride]) -
              5 * (src[-1 * stride] - src[0 * stride]) + 4) >> 3;
    int a0_sign = a0 >> 31;        /* Store sign */

    a0 = (a0 ^ a0_sign) - a0_sign; /* a0 = FFABS(a0); */
    if (a0 < pq) {
        int a1 = FFABS((2 * (src[-4 * stride] - src[-1 * stride]) -
                        5 * (src[-3 * stride] - src[-2 * stride]) + 4) >> 3);
        int a2 = FFABS((2 * (src[ 0 * stride] - src[ 3 * stride]) -
                        5 * (src[ 1 * stride] - src[ 2 * stride]) + 4) >> 3);
        if (a1 < a0 || a2 < a0) {
            int clip      = src[-1 * stride] - src[0 * stride];
            int clip_sign = clip >> 31;

            clip = ((clip ^ clip_sign) - clip_sign) >> 1;
            if (clip) {
                int a3     = FFMIN(a1, a2);
                int d      = 5 * (a3 - a0);
                int d_sign = (d >> 31);

                d       = ((d ^ d_sign) - d_sign) >> 3;
                d_sign ^= a0_sign;

                if (d_sign ^ clip_sign)
                    d = 0;
                else {
                    d = FFMIN(d, clip);
                    d = (d ^ d_sign) - d_sign; /* Restore sign */
                    src[-1 * stride] = av_clip_uint8(src[-1 * stride] - d);
                    src[ 0 * stride] = av_clip_uint8(src[ 0 * stride] + d);
                }
                return 1;
            }
        }
    }
    return 0;
}

/**
 * VC-1 in-loop deblocking filter
 * @param src source block type
 * @param step distance between horizontally adjacent elements
 * @param stride distance between vertically adjacent elements
 * @param len edge length to filter (4 or 8 pixels)
 * @param pq block quantizer
 * @see 8.6
 */
static inline void vc1_loop_filter(uint8_t *src, int step, int stride,
                                   int len, int pq)
{
    int i;
    int filt3;

    for (i = 0; i < len; i += 4) {
        filt3 = vc1_filter_line(src + 2 * step, stride, pq);
        if (filt3) {
            vc1_filter_line(src + 0 * step, stride, pq);
            vc1_filter_line(src + 1 * step, stride, pq);
            vc1_filter_line(src + 3 * step, stride, pq);
        }
        src += step * 4;
    }
}

static void vc1_v_loop_filter4_c(uint8_t *src, int stride, int pq)
{
    vc1_loop_filter(src, 1, stride, 4, pq);
}

static void vc1_h_loop_filter4_c(uint8_t *src, int stride, int pq)
{
    vc1_loop_filter(src, stride, 1, 4, pq);
}

static void vc1_v_loop_filter8_c(uint8_t *src, int stride, int pq)
{
    vc1_loop_filter(src, 1, stride, 8, pq);
}

static void vc1_h_loop_filter8_c(uint8_t *src, int stride, int pq)
{
    vc1_loop_filter(src, stride, 1, 8, pq);
}

static void vc1_v_loop_filter16_c(uint8_t *src, int stride, int pq)
{
    vc1_loop_filter(src, 1, stride, 16, pq);
}

static void vc1_h_loop_filter16_c(uint8_t *src, int stride, int pq)
{
    vc1_loop_filter(src, stride, 1, 16, pq);
}

/* Do inverse transform on 8x8 block */
static void vc1_inv_trans_8x8_dc_c(uint8_t *dest, ptrdiff_t stride, int16_t *block)
{
    int i;
    int dc = block[0];

    dc = (3 * dc +  1) >> 1;
    dc = (3 * dc + 16) >> 5;

    for (i = 0; i < 8; i++) {
        dest[0] = av_clip_uint8(dest[0] + dc);
        dest[1] = av_clip_uint8(dest[1] + dc);
        dest[2] = av_clip_uint8(dest[2] + dc);
        dest[3] = av_clip_uint8(dest[3] + dc);
        dest[4] = av_clip_uint8(dest[4] + dc);
        dest[5] = av_clip_uint8(dest[5] + dc);
        dest[6] = av_clip_uint8(dest[6] + dc);
        dest[7] = av_clip_uint8(dest[7] + dc);
        dest += stride;
    }
}

static void vc1_inv_trans_8x8_c(int16_t block[64])
{
    int i;
    register int t1, t2, t3, t4, t5, t6, t7, t8;
    int16_t *src, *dst, temp[64];

    src = block;
    dst = temp;
    for (i = 0; i < 8; i++) {
        t1 = 12 * (src[ 0] + src[32]) + 4;
        t2 = 12 * (src[ 0] - src[32]) + 4;
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

        dst[0] = (t5 + t1) >> 3;
        dst[1] = (t6 + t2) >> 3;
        dst[2] = (t7 + t3) >> 3;
        dst[3] = (t8 + t4) >> 3;
        dst[4] = (t8 - t4) >> 3;
        dst[5] = (t7 - t3) >> 3;
        dst[6] = (t6 - t2) >> 3;
        dst[7] = (t5 - t1) >> 3;

        src += 1;
        dst += 8;
    }

    src = temp;
    dst = block;
    for (i = 0; i < 8; i++) {
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

/* Do inverse transform on 8x4 part of block */
static void vc1_inv_trans_8x4_dc_c(uint8_t *dest, ptrdiff_t stride, int16_t *block)
{
    int i;
    int dc = block[0];

    dc =  (3 * dc +  1) >> 1;
    dc = (17 * dc + 64) >> 7;

    for (i = 0; i < 4; i++) {
        dest[0] = av_clip_uint8(dest[0] + dc);
        dest[1] = av_clip_uint8(dest[1] + dc);
        dest[2] = av_clip_uint8(dest[2] + dc);
        dest[3] = av_clip_uint8(dest[3] + dc);
        dest[4] = av_clip_uint8(dest[4] + dc);
        dest[5] = av_clip_uint8(dest[5] + dc);
        dest[6] = av_clip_uint8(dest[6] + dc);
        dest[7] = av_clip_uint8(dest[7] + dc);
        dest += stride;
    }
}

static void vc1_inv_trans_8x4_c(uint8_t *dest, ptrdiff_t stride, int16_t *block)
{
    int i;
    register int t1, t2, t3, t4, t5, t6, t7, t8;
    int16_t *src, *dst;

    src = block;
    dst = block;

    for (i = 0; i < 4; i++) {
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
    for (i = 0; i < 8; i++) {
        t1 = 17 * (src[ 0] + src[16]) + 64;
        t2 = 17 * (src[ 0] - src[16]) + 64;
        t3 = 22 * src[ 8] + 10 * src[24];
        t4 = 22 * src[24] - 10 * src[ 8];

        dest[0 * stride] = av_clip_uint8(dest[0 * stride] + ((t1 + t3) >> 7));
        dest[1 * stride] = av_clip_uint8(dest[1 * stride] + ((t2 - t4) >> 7));
        dest[2 * stride] = av_clip_uint8(dest[2 * stride] + ((t2 + t4) >> 7));
        dest[3 * stride] = av_clip_uint8(dest[3 * stride] + ((t1 - t3) >> 7));

        src++;
        dest++;
    }
}

/* Do inverse transform on 4x8 parts of block */
static void vc1_inv_trans_4x8_dc_c(uint8_t *dest, ptrdiff_t stride, int16_t *block)
{
    int i;
    int dc = block[0];

    dc = (17 * dc +  4) >> 3;
    dc = (12 * dc + 64) >> 7;

    for (i = 0; i < 8; i++) {
        dest[0] = av_clip_uint8(dest[0] + dc);
        dest[1] = av_clip_uint8(dest[1] + dc);
        dest[2] = av_clip_uint8(dest[2] + dc);
        dest[3] = av_clip_uint8(dest[3] + dc);
        dest += stride;
    }
}

static void vc1_inv_trans_4x8_c(uint8_t *dest, ptrdiff_t stride, int16_t *block)
{
    int i;
    register int t1, t2, t3, t4, t5, t6, t7, t8;
    int16_t *src, *dst;

    src = block;
    dst = block;

    for (i = 0; i < 8; i++) {
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
    for (i = 0; i < 4; i++) {
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

        dest[0 * stride] = av_clip_uint8(dest[0 * stride] + ((t5 + t1)     >> 7));
        dest[1 * stride] = av_clip_uint8(dest[1 * stride] + ((t6 + t2)     >> 7));
        dest[2 * stride] = av_clip_uint8(dest[2 * stride] + ((t7 + t3)     >> 7));
        dest[3 * stride] = av_clip_uint8(dest[3 * stride] + ((t8 + t4)     >> 7));
        dest[4 * stride] = av_clip_uint8(dest[4 * stride] + ((t8 - t4 + 1) >> 7));
        dest[5 * stride] = av_clip_uint8(dest[5 * stride] + ((t7 - t3 + 1) >> 7));
        dest[6 * stride] = av_clip_uint8(dest[6 * stride] + ((t6 - t2 + 1) >> 7));
        dest[7 * stride] = av_clip_uint8(dest[7 * stride] + ((t5 - t1 + 1) >> 7));

        src++;
        dest++;
    }
}

/* Do inverse transform on 4x4 part of block */
static void vc1_inv_trans_4x4_dc_c(uint8_t *dest, ptrdiff_t stride, int16_t *block)
{
    int i;
    int dc = block[0];

    dc = (17 * dc +  4) >> 3;
    dc = (17 * dc + 64) >> 7;

    for (i = 0; i < 4; i++) {
        dest[0] = av_clip_uint8(dest[0] + dc);
        dest[1] = av_clip_uint8(dest[1] + dc);
        dest[2] = av_clip_uint8(dest[2] + dc);
        dest[3] = av_clip_uint8(dest[3] + dc);
        dest += stride;
    }
}

static void vc1_inv_trans_4x4_c(uint8_t *dest, ptrdiff_t stride, int16_t *block)
{
    int i;
    register int t1, t2, t3, t4;
    int16_t *src, *dst;

    src = block;
    dst = block;
    for (i = 0; i < 4; i++) {
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
    for (i = 0; i < 4; i++) {
        t1 = 17 * (src[0] + src[16]) + 64;
        t2 = 17 * (src[0] - src[16]) + 64;
        t3 = 22 * src[8] + 10 * src[24];
        t4 = 22 * src[24] - 10 * src[8];

        dest[0 * stride] = av_clip_uint8(dest[0 * stride] + ((t1 + t3) >> 7));
        dest[1 * stride] = av_clip_uint8(dest[1 * stride] + ((t2 - t4) >> 7));
        dest[2 * stride] = av_clip_uint8(dest[2 * stride] + ((t2 + t4) >> 7));
        dest[3 * stride] = av_clip_uint8(dest[3 * stride] + ((t1 - t3) >> 7));

        src++;
        dest++;
    }
}

/* motion compensation functions */

/* Filter in case of 2 filters */
#define VC1_MSPEL_FILTER_16B(DIR, TYPE)                                       \
static av_always_inline int vc1_mspel_ ## DIR ## _filter_16bits(const TYPE *src, \
                                                                int stride,   \
                                                                int mode)     \
{                                                                             \
    switch(mode) {                                                            \
    case 0: /* no shift - should not occur */                                 \
        return 0;                                                             \
    case 1: /* 1/4 shift */                                                   \
        return -4 * src[-stride] + 53 * src[0] +                              \
               18 * src[stride]  -  3 * src[stride * 2];                      \
    case 2: /* 1/2 shift */                                                   \
        return -1 * src[-stride] +  9 * src[0] +                              \
                9 * src[stride]  -  1 * src[stride * 2];                      \
    case 3: /* 3/4 shift */                                                   \
        return -3 * src[-stride] + 18 * src[0] +                              \
               53 * src[stride]  -  4 * src[stride * 2];                      \
    }                                                                         \
    return 0; /* should not occur */                                          \
}

VC1_MSPEL_FILTER_16B(ver, uint8_t)
VC1_MSPEL_FILTER_16B(hor, int16_t)

/* Filter used to interpolate fractional pel values */
static av_always_inline int vc1_mspel_filter(const uint8_t *src, int stride,
                                             int mode, int r)
{
    switch (mode) {
    case 0: // no shift
        return src[0];
    case 1: // 1/4 shift
        return (-4 * src[-stride] + 53 * src[0] +
                18 * src[stride]  -  3 * src[stride * 2] + 32 - r) >> 6;
    case 2: // 1/2 shift
        return (-1 * src[-stride] +  9 * src[0] +
                 9 * src[stride]  -  1 * src[stride * 2] + 8 - r) >> 4;
    case 3: // 3/4 shift
        return (-3 * src[-stride] + 18 * src[0] +
                53 * src[stride]  -  4 * src[stride * 2] + 32 - r) >> 6;
    }
    return 0; // should not occur
}

/* Function used to do motion compensation with bicubic interpolation */
#define VC1_MSPEL_MC(OP, OP4, OPNAME)                                         \
static av_always_inline void OPNAME ## vc1_mspel_mc(uint8_t *dst,             \
                                                    const uint8_t *src,       \
                                                    ptrdiff_t stride,         \
                                                    int hmode,                \
                                                    int vmode,                \
                                                    int rnd)                  \
{                                                                             \
    int i, j;                                                                 \
                                                                              \
    if (vmode) { /* Horizontal filter to apply */                             \
        int r;                                                                \
                                                                              \
        if (hmode) { /* Vertical filter to apply, output to tmp */            \
            static const int shift_value[] = { 0, 5, 1, 5 };                  \
            int shift = (shift_value[hmode] + shift_value[vmode]) >> 1;       \
            int16_t tmp[11 * 8], *tptr = tmp;                                 \
                                                                              \
            r = (1 << (shift - 1)) + rnd - 1;                                 \
                                                                              \
            src -= 1;                                                         \
            for (j = 0; j < 8; j++) {                                         \
                for (i = 0; i < 11; i++)                                      \
                    tptr[i] = (vc1_mspel_ver_filter_16bits(src + i, stride, vmode) + r) >> shift; \
                src  += stride;                                               \
                tptr += 11;                                                   \
            }                                                                 \
                                                                              \
            r    = 64 - rnd;                                                  \
            tptr = tmp + 1;                                                   \
            for (j = 0; j < 8; j++) {                                         \
                for (i = 0; i < 8; i++)                                       \
                    OP(dst[i], (vc1_mspel_hor_filter_16bits(tptr + i, 1, hmode) + r) >> 7); \
                dst  += stride;                                               \
                tptr += 11;                                                   \
            }                                                                 \
                                                                              \
            return;                                                           \
        } else { /* No horizontal filter, output 8 lines to dst */            \
            r = 1 - rnd;                                                      \
                                                                              \
            for (j = 0; j < 8; j++) {                                         \
                for (i = 0; i < 8; i++)                                       \
                    OP(dst[i], vc1_mspel_filter(src + i, stride, vmode, r));  \
                src += stride;                                                \
                dst += stride;                                                \
            }                                                                 \
            return;                                                           \
        }                                                                     \
    }                                                                         \
                                                                              \
    /* Horizontal mode with no vertical mode */                               \
    for (j = 0; j < 8; j++) {                                                 \
        for (i = 0; i < 8; i++)                                               \
            OP(dst[i], vc1_mspel_filter(src + i, 1, hmode, rnd));             \
        dst += stride;                                                        \
        src += stride;                                                        \
    }                                                                         \
}\
static av_always_inline void OPNAME ## vc1_mspel_mc_16(uint8_t *dst,          \
                                                       const uint8_t *src,    \
                                                       ptrdiff_t stride,      \
                                                       int hmode,             \
                                                       int vmode,             \
                                                       int rnd)               \
{                                                                             \
    int i, j;                                                                 \
                                                                              \
    if (vmode) { /* Horizontal filter to apply */                             \
        int r;                                                                \
                                                                              \
        if (hmode) { /* Vertical filter to apply, output to tmp */            \
            static const int shift_value[] = { 0, 5, 1, 5 };                  \
            int shift = (shift_value[hmode] + shift_value[vmode]) >> 1;       \
            int16_t tmp[19 * 16], *tptr = tmp;                                \
                                                                              \
            r = (1 << (shift - 1)) + rnd - 1;                                 \
                                                                              \
            src -= 1;                                                         \
            for (j = 0; j < 16; j++) {                                        \
                for (i = 0; i < 19; i++)                                      \
                    tptr[i] = (vc1_mspel_ver_filter_16bits(src + i, stride, vmode) + r) >> shift; \
                src  += stride;                                               \
                tptr += 19;                                                   \
            }                                                                 \
                                                                              \
            r    = 64 - rnd;                                                  \
            tptr = tmp + 1;                                                   \
            for (j = 0; j < 16; j++) {                                        \
                for (i = 0; i < 16; i++)                                      \
                    OP(dst[i], (vc1_mspel_hor_filter_16bits(tptr + i, 1, hmode) + r) >> 7); \
                dst  += stride;                                               \
                tptr += 19;                                                   \
            }                                                                 \
                                                                              \
            return;                                                           \
        } else { /* No horizontal filter, output 8 lines to dst */            \
            r = 1 - rnd;                                                      \
                                                                              \
            for (j = 0; j < 16; j++) {                                        \
                for (i = 0; i < 16; i++)                                      \
                    OP(dst[i], vc1_mspel_filter(src + i, stride, vmode, r));  \
                src += stride;                                                \
                dst += stride;                                                \
            }                                                                 \
            return;                                                           \
        }                                                                     \
    }                                                                         \
                                                                              \
    /* Horizontal mode with no vertical mode */                               \
    for (j = 0; j < 16; j++) {                                                \
        for (i = 0; i < 16; i++)                                              \
            OP(dst[i], vc1_mspel_filter(src + i, 1, hmode, rnd));             \
        dst += stride;                                                        \
        src += stride;                                                        \
    }                                                                         \
}\
static void OPNAME ## pixels8x8_c(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int rnd){\
    int i;\
    for(i=0; i<8; i++){\
        OP4(*(uint32_t*)(block  ), AV_RN32(pixels  ));\
        OP4(*(uint32_t*)(block+4), AV_RN32(pixels+4));\
        pixels+=line_size;\
        block +=line_size;\
    }\
}\
static void OPNAME ## pixels16x16_c(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int rnd){\
    int i;\
    for(i=0; i<16; i++){\
        OP4(*(uint32_t*)(block   ), AV_RN32(pixels   ));\
        OP4(*(uint32_t*)(block+ 4), AV_RN32(pixels+ 4));\
        OP4(*(uint32_t*)(block+ 8), AV_RN32(pixels+ 8));\
        OP4(*(uint32_t*)(block+12), AV_RN32(pixels+12));\
        pixels+=line_size;\
        block +=line_size;\
    }\
}

#define op_put(a, b) (a) = av_clip_uint8(b)
#define op_avg(a, b) (a) = ((a) + av_clip_uint8(b) + 1) >> 1
#define op4_avg(a, b) (a) = rnd_avg32(a, b)
#define op4_put(a, b) (a) = (b)

VC1_MSPEL_MC(op_put, op4_put, put_)
VC1_MSPEL_MC(op_avg, op4_avg, avg_)

/* pixel functions - really are entry points to vc1_mspel_mc */

#define PUT_VC1_MSPEL(a, b)                                                   \
static void put_vc1_mspel_mc ## a ## b ## _c(uint8_t *dst,                    \
                                             const uint8_t *src,              \
                                             ptrdiff_t stride, int rnd)       \
{                                                                             \
    put_vc1_mspel_mc(dst, src, stride, a, b, rnd);                            \
}                                                                             \
static void avg_vc1_mspel_mc ## a ## b ## _c(uint8_t *dst,                    \
                                             const uint8_t *src,              \
                                             ptrdiff_t stride, int rnd)       \
{                                                                             \
    avg_vc1_mspel_mc(dst, src, stride, a, b, rnd);                            \
}                                                                             \
static void put_vc1_mspel_mc ## a ## b ## _16_c(uint8_t *dst,                 \
                                                const uint8_t *src,           \
                                                ptrdiff_t stride, int rnd)    \
{                                                                             \
    put_vc1_mspel_mc_16(dst, src, stride, a, b, rnd);                         \
}                                                                             \
static void avg_vc1_mspel_mc ## a ## b ## _16_c(uint8_t *dst,                 \
                                                const uint8_t *src,           \
                                                ptrdiff_t stride, int rnd)    \
{                                                                             \
    avg_vc1_mspel_mc_16(dst, src, stride, a, b, rnd);                         \
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

#define chroma_mc(a) \
    ((A * src[a] + B * src[a + 1] + \
      C * src[stride + a] + D * src[stride + a + 1] + 32 - 4) >> 6)
static void put_no_rnd_vc1_chroma_mc8_c(uint8_t *dst /* align 8 */,
                                        uint8_t *src /* align 1 */,
                                        ptrdiff_t stride, int h, int x, int y)
{
    const int A = (8 - x) * (8 - y);
    const int B =     (x) * (8 - y);
    const int C = (8 - x) *     (y);
    const int D =     (x) *     (y);
    int i;

    av_assert2(x < 8 && y < 8 && x >= 0 && y >= 0);

    for (i = 0; i < h; i++) {
        dst[0] = chroma_mc(0);
        dst[1] = chroma_mc(1);
        dst[2] = chroma_mc(2);
        dst[3] = chroma_mc(3);
        dst[4] = chroma_mc(4);
        dst[5] = chroma_mc(5);
        dst[6] = chroma_mc(6);
        dst[7] = chroma_mc(7);
        dst += stride;
        src += stride;
    }
}

static void put_no_rnd_vc1_chroma_mc4_c(uint8_t *dst, uint8_t *src,
                                        ptrdiff_t stride, int h, int x, int y)
{
    const int A = (8 - x) * (8 - y);
    const int B =     (x) * (8 - y);
    const int C = (8 - x) *     (y);
    const int D =     (x) *     (y);
    int i;

    av_assert2(x < 8 && y < 8 && x >= 0 && y >= 0);

    for (i = 0; i < h; i++) {
        dst[0] = chroma_mc(0);
        dst[1] = chroma_mc(1);
        dst[2] = chroma_mc(2);
        dst[3] = chroma_mc(3);
        dst += stride;
        src += stride;
    }
}

#define avg2(a, b) (((a) + (b) + 1) >> 1)
static void avg_no_rnd_vc1_chroma_mc8_c(uint8_t *dst /* align 8 */,
                                        uint8_t *src /* align 1 */,
                                        ptrdiff_t stride, int h, int x, int y)
{
    const int A = (8 - x) * (8 - y);
    const int B =     (x) * (8 - y);
    const int C = (8 - x) *     (y);
    const int D =     (x) *     (y);
    int i;

    av_assert2(x < 8 && y < 8 && x >= 0 && y >= 0);

    for (i = 0; i < h; i++) {
        dst[0] = avg2(dst[0], chroma_mc(0));
        dst[1] = avg2(dst[1], chroma_mc(1));
        dst[2] = avg2(dst[2], chroma_mc(2));
        dst[3] = avg2(dst[3], chroma_mc(3));
        dst[4] = avg2(dst[4], chroma_mc(4));
        dst[5] = avg2(dst[5], chroma_mc(5));
        dst[6] = avg2(dst[6], chroma_mc(6));
        dst[7] = avg2(dst[7], chroma_mc(7));
        dst += stride;
        src += stride;
    }
}

static void avg_no_rnd_vc1_chroma_mc4_c(uint8_t *dst /* align 8 */,
                                        uint8_t *src /* align 1 */,
                                        ptrdiff_t stride, int h, int x, int y)
{
    const int A = (8 - x) * (8 - y);
    const int B = (    x) * (8 - y);
    const int C = (8 - x) * (    y);
    const int D = (    x) * (    y);
    int i;

    av_assert2(x < 8 && y < 8 && x >= 0 && y >= 0);

    for (i = 0; i < h; i++) {
        dst[0] = avg2(dst[0], chroma_mc(0));
        dst[1] = avg2(dst[1], chroma_mc(1));
        dst[2] = avg2(dst[2], chroma_mc(2));
        dst[3] = avg2(dst[3], chroma_mc(3));
        dst += stride;
        src += stride;
    }
}

#if CONFIG_WMV3IMAGE_DECODER || CONFIG_VC1IMAGE_DECODER

static void sprite_h_c(uint8_t *dst, const uint8_t *src, int offset,
                       int advance, int count)
{
    while (count--) {
        int a = src[(offset >> 16)];
        int b = src[(offset >> 16) + 1];
        *dst++  = a + ((b - a) * (offset & 0xFFFF) >> 16);
        offset += advance;
    }
}

static av_always_inline void sprite_v_template(uint8_t *dst,
                                               const uint8_t *src1a,
                                               const uint8_t *src1b,
                                               int offset1,
                                               int two_sprites,
                                               const uint8_t *src2a,
                                               const uint8_t *src2b,
                                               int offset2,
                                               int alpha, int scaled,
                                               int width)
{
    int a1, b1, a2, b2;
    while (width--) {
        a1 = *src1a++;
        if (scaled) {
            b1 = *src1b++;
            a1 = a1 + ((b1 - a1) * offset1 >> 16);
        }
        if (two_sprites) {
            a2 = *src2a++;
            if (scaled > 1) {
                b2 = *src2b++;
                a2 = a2 + ((b2 - a2) * offset2 >> 16);
            }
            a1 = a1 + ((a2 - a1) * alpha >> 16);
        }
        *dst++ = a1;
    }
}

static void sprite_v_single_c(uint8_t *dst, const uint8_t *src1a,
                              const uint8_t *src1b,
                              int offset, int width)
{
    sprite_v_template(dst, src1a, src1b, offset, 0, NULL, NULL, 0, 0, 1, width);
}

static void sprite_v_double_noscale_c(uint8_t *dst, const uint8_t *src1a,
                                      const uint8_t *src2a,
                                      int alpha, int width)
{
    sprite_v_template(dst, src1a, NULL, 0, 1, src2a, NULL, 0, alpha, 0, width);
}

static void sprite_v_double_onescale_c(uint8_t *dst,
                                       const uint8_t *src1a,
                                       const uint8_t *src1b,
                                       int offset1,
                                       const uint8_t *src2a,
                                       int alpha, int width)
{
    sprite_v_template(dst, src1a, src1b, offset1, 1, src2a, NULL, 0, alpha, 1,
                      width);
}

static void sprite_v_double_twoscale_c(uint8_t *dst,
                                       const uint8_t *src1a,
                                       const uint8_t *src1b,
                                       int offset1,
                                       const uint8_t *src2a,
                                       const uint8_t *src2b,
                                       int offset2,
                                       int alpha,
                                       int width)
{
    sprite_v_template(dst, src1a, src1b, offset1, 1, src2a, src2b, offset2,
                      alpha, 2, width);
}

#endif /* CONFIG_WMV3IMAGE_DECODER || CONFIG_VC1IMAGE_DECODER */
#define FN_ASSIGN(X, Y) \
    dsp->put_vc1_mspel_pixels_tab[1][X+4*Y] = put_vc1_mspel_mc##X##Y##_c; \
    dsp->put_vc1_mspel_pixels_tab[0][X+4*Y] = put_vc1_mspel_mc##X##Y##_16_c; \
    dsp->avg_vc1_mspel_pixels_tab[1][X+4*Y] = avg_vc1_mspel_mc##X##Y##_c; \
    dsp->avg_vc1_mspel_pixels_tab[0][X+4*Y] = avg_vc1_mspel_mc##X##Y##_16_c

av_cold void ff_vc1dsp_init(VC1DSPContext *dsp)
{
    dsp->vc1_inv_trans_8x8    = vc1_inv_trans_8x8_c;
    dsp->vc1_inv_trans_4x8    = vc1_inv_trans_4x8_c;
    dsp->vc1_inv_trans_8x4    = vc1_inv_trans_8x4_c;
    dsp->vc1_inv_trans_4x4    = vc1_inv_trans_4x4_c;
    dsp->vc1_inv_trans_8x8_dc = vc1_inv_trans_8x8_dc_c;
    dsp->vc1_inv_trans_4x8_dc = vc1_inv_trans_4x8_dc_c;
    dsp->vc1_inv_trans_8x4_dc = vc1_inv_trans_8x4_dc_c;
    dsp->vc1_inv_trans_4x4_dc = vc1_inv_trans_4x4_dc_c;

    dsp->vc1_h_overlap        = vc1_h_overlap_c;
    dsp->vc1_v_overlap        = vc1_v_overlap_c;
    dsp->vc1_h_s_overlap      = vc1_h_s_overlap_c;
    dsp->vc1_v_s_overlap      = vc1_v_s_overlap_c;

    dsp->vc1_v_loop_filter4   = vc1_v_loop_filter4_c;
    dsp->vc1_h_loop_filter4   = vc1_h_loop_filter4_c;
    dsp->vc1_v_loop_filter8   = vc1_v_loop_filter8_c;
    dsp->vc1_h_loop_filter8   = vc1_h_loop_filter8_c;
    dsp->vc1_v_loop_filter16  = vc1_v_loop_filter16_c;
    dsp->vc1_h_loop_filter16  = vc1_h_loop_filter16_c;

    dsp->put_vc1_mspel_pixels_tab[0][0] = put_pixels16x16_c;
    dsp->avg_vc1_mspel_pixels_tab[0][0] = avg_pixels16x16_c;
    dsp->put_vc1_mspel_pixels_tab[1][0] = put_pixels8x8_c;
    dsp->avg_vc1_mspel_pixels_tab[1][0] = avg_pixels8x8_c;
    FN_ASSIGN(0, 1);
    FN_ASSIGN(0, 2);
    FN_ASSIGN(0, 3);

    FN_ASSIGN(1, 0);
    FN_ASSIGN(1, 1);
    FN_ASSIGN(1, 2);
    FN_ASSIGN(1, 3);

    FN_ASSIGN(2, 0);
    FN_ASSIGN(2, 1);
    FN_ASSIGN(2, 2);
    FN_ASSIGN(2, 3);

    FN_ASSIGN(3, 0);
    FN_ASSIGN(3, 1);
    FN_ASSIGN(3, 2);
    FN_ASSIGN(3, 3);

    dsp->put_no_rnd_vc1_chroma_pixels_tab[0] = put_no_rnd_vc1_chroma_mc8_c;
    dsp->avg_no_rnd_vc1_chroma_pixels_tab[0] = avg_no_rnd_vc1_chroma_mc8_c;
    dsp->put_no_rnd_vc1_chroma_pixels_tab[1] = put_no_rnd_vc1_chroma_mc4_c;
    dsp->avg_no_rnd_vc1_chroma_pixels_tab[1] = avg_no_rnd_vc1_chroma_mc4_c;

#if CONFIG_WMV3IMAGE_DECODER || CONFIG_VC1IMAGE_DECODER
    dsp->sprite_h                 = sprite_h_c;
    dsp->sprite_v_single          = sprite_v_single_c;
    dsp->sprite_v_double_noscale  = sprite_v_double_noscale_c;
    dsp->sprite_v_double_onescale = sprite_v_double_onescale_c;
    dsp->sprite_v_double_twoscale = sprite_v_double_twoscale_c;
#endif /* CONFIG_WMV3IMAGE_DECODER || CONFIG_VC1IMAGE_DECODER */

    dsp->startcode_find_candidate = ff_startcode_find_candidate_c;

    if (ARCH_AARCH64)
        ff_vc1dsp_init_aarch64(dsp);
    if (ARCH_ARM)
        ff_vc1dsp_init_arm(dsp);
    if (ARCH_PPC)
        ff_vc1dsp_init_ppc(dsp);
    if (ARCH_X86)
        ff_vc1dsp_init_x86(dsp);
    if (ARCH_MIPS)
        ff_vc1dsp_init_mips(dsp);
}
