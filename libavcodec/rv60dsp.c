/*
 * RV60 dsp routines
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

#include "rv60dsp.h"
#include "libavutil/common.h"

void ff_rv60_idct4x4_add(const int16_t * block, uint8_t * dst, int dst_stride)
{
    int tmp[16];
#define IDCT4X4(src, src_stride, src_step, dst, dst_stride, dst_step) \
    for (int y = 0; y < 4; y++) { \
        int a = src[y*src_stride + 0*src_step]; \
        int b = src[y*src_stride + 1*src_step]; \
        int c = src[y*src_stride + 2*src_step]; \
        int d = src[y*src_stride + 3*src_step]; \
        int t0 = 13 * (a + c); \
        int t1 = 13 * (a - c); \
        int t2 = 7 * b - 17 * d; \
        int t3 = 7 * d + 17 * b; \
        STORE(dst[y*dst_stride + 0*dst_step], (t0 + t3 + 16) >> 5); \
        STORE(dst[y*dst_stride + 1*dst_step], (t1 + t2 + 16) >> 5); \
        STORE(dst[y*dst_stride + 2*dst_step], (t1 - t2 + 16) >> 5); \
        STORE(dst[y*dst_stride + 3*dst_step], (t0 - t3 + 16) >> 5); \
    }
#define STORE(a, b) a = b
    IDCT4X4(block, 1, 4, tmp, 1, 4)
#undef STORE
#define STORE(a, b) a = av_clip_uint8(a + (b))
    IDCT4X4(tmp, 4, 1, dst, dst_stride, 1)
#undef STORE
}

void ff_rv60_idct8x8_add(const int16_t * block, uint8_t * dst, int dst_stride)
{
    int tmp[64];
#define IDCT8X8(src, src_stride, src_step, dst, dst_stride, dst_step) \
    for (int y = 0; y < 8; y++) { \
        int a = src[y*src_stride + 0*src_step]; \
        int b = src[y*src_stride + 1*src_step]; \
        int c = src[y*src_stride + 2*src_step]; \
        int d = src[y*src_stride + 3*src_step]; \
        int e = src[y*src_stride + 4*src_step]; \
        int f = src[y*src_stride + 5*src_step]; \
        int g = src[y*src_stride + 6*src_step]; \
        int h = src[y*src_stride + 7*src_step]; \
        int t0 = 37 * (a + e); \
        int t1 = 37 * (a - e); \
        int t2 = 48 * c + 20 * g; \
        int t3 = 20 * c - 48 * g; \
        int t4 = t0 + t2; \
        int t5 = t0 - t2; \
        int t6 = t1 + t3; \
        int t7 = t1 - t3;  \
        int t8 = 51 * b + 43 * d + 29 * f + 10 * h; \
        int t9 = 43 * b - 10 * d - 51 * f - 29 * h; \
        int ta = 29 * b - 51 * d + 10 * f + 43 * h; \
        int tb = 10 * b - 29 * d + 43 * f - 51 * h; \
        STORE(dst[y*dst_stride + 0*dst_step], (t4 + t8 + 64) >> 7); \
        STORE(dst[y*dst_stride + 1*dst_step], (t6 + t9 + 64) >> 7); \
        STORE(dst[y*dst_stride + 2*dst_step], (t7 + ta + 64) >> 7); \
        STORE(dst[y*dst_stride + 3*dst_step], (t5 + tb + 64) >> 7); \
        STORE(dst[y*dst_stride + 4*dst_step], (t5 - tb + 64) >> 7); \
        STORE(dst[y*dst_stride + 5*dst_step], (t7 - ta + 64) >> 7); \
        STORE(dst[y*dst_stride + 6*dst_step], (t6 - t9 + 64) >> 7); \
        STORE(dst[y*dst_stride + 7*dst_step], (t4 - t8 + 64) >> 7); \
    }
#define STORE(a, b) a = b
    IDCT8X8(block, 1, 8, tmp, 1, 8)
#undef STORE
#define STORE(a, b) a = av_clip_uint8(a + (b))
    IDCT8X8(tmp, 8, 1, dst, dst_stride, 1)
#undef STORE
}

void ff_rv60_idct16x16_add(const int16_t * block, uint8_t * dst, int dst_stride)
{
    int16_t tmp[256];
#define IDCT16X16(src, src_stride, src_step, dst, dst_stride, dst_step) \
    for (int y = 0; y < 16; y++) { \
        int a = src[y*src_stride +  0*src_step]; \
        int b = src[y*src_stride +  1*src_step]; \
        int c = src[y*src_stride +  2*src_step]; \
        int d = src[y*src_stride +  3*src_step]; \
        int e = src[y*src_stride +  4*src_step]; \
        int f = src[y*src_stride +  5*src_step]; \
        int g = src[y*src_stride +  6*src_step]; \
        int h = src[y*src_stride +  7*src_step]; \
        int i = src[y*src_stride +  8*src_step]; \
        int j = src[y*src_stride +  9*src_step]; \
        int k = src[y*src_stride + 10*src_step]; \
        int l = src[y*src_stride + 11*src_step]; \
        int m = src[y*src_stride + 12*src_step]; \
        int n = src[y*src_stride + 13*src_step]; \
        int o = src[y*src_stride + 14*src_step]; \
        int p = src[y*src_stride + 15*src_step]; \
        int t0 = 26 * (a + i); \
        int t1 = 26 * (a - i); \
        int t2 = 14 * e - 34 * m; \
        int t3 = 34 * e + 14 * m; \
        int t4 = t0 + t3; \
        int t5 = t0 - t3; \
        int t6 = t1 + t2; \
        int t7 = t1 - t2; \
        int tmp00 = 31 * c -  7 * g - 36 * k - 20 * o; \
        int tmp01 = 36 * c + 31 * g + 20 * k +  7 * o; \
        int tmp02 = 20 * c - 36 * g +  7 * k + 31 * o; \
        int tmp03 =  7 * c - 20 * g + 31 * k - 36 * o; \
        int tm0 = t4 + tmp01; \
        int tm1 = t4 - tmp01; \
        int tm2 = t5 + tmp03; \
        int tm3 = t5 - tmp03; \
        int tm4 = t6 + tmp00; \
        int tm5 = t6 - tmp00; \
        int tm6 = t7 + tmp02; \
        int tm7 = t7 - tmp02; \
        int tt0 = 37 * b + 35 * d + 32 * f + 28 * h + 23 * j + 17 * l + 11 * n +  4 * p; \
        int tt1 = 35 * b + 23 * d +  4 * f - 17 * h - 32 * j - 37 * l - 28 * n - 11 * p; \
        int tt2 = 32 * b +  4 * d - 28 * f - 35 * h - 11 * j + 23 * l + 37 * n + 17 * p; \
        int tt3 = 28 * b - 17 * d - 35 * f +  4 * h + 37 * j + 11 * l - 32 * n - 23 * p; \
        int tt4 = 23 * b - 32 * d - 11 * f + 37 * h -  4 * j - 35 * l + 17 * n + 28 * p; \
        int tt5 = 17 * b - 37 * d + 23 * f + 11 * h - 35 * j + 28 * l +  4 * n - 32 * p; \
        int tt6 = 11 * b - 28 * d + 37 * f - 32 * h + 17 * j +  4 * l - 23 * n + 35 * p; \
        int tt7 =  4 * b - 11 * d + 17 * f - 23 * h + 28 * j - 32 * l + 35 * n - 37 * p; \
        STORE(dst[y*dst_stride+  0*dst_step], (tm0 + tt0 + 64) >> 7); \
        STORE(dst[y*dst_stride+  1*dst_step], (tm4 + tt1 + 64) >> 7); \
        STORE(dst[y*dst_stride+  2*dst_step], (tm6 + tt2 + 64) >> 7); \
        STORE(dst[y*dst_stride+  3*dst_step], (tm2 + tt3 + 64) >> 7); \
        STORE(dst[y*dst_stride+  4*dst_step], (tm3 + tt4 + 64) >> 7); \
        STORE(dst[y*dst_stride+  5*dst_step], (tm7 + tt5 + 64) >> 7); \
        STORE(dst[y*dst_stride+  6*dst_step], (tm5 + tt6 + 64) >> 7); \
        STORE(dst[y*dst_stride+  7*dst_step], (tm1 + tt7 + 64) >> 7); \
        STORE(dst[y*dst_stride+  8*dst_step], (tm1 - tt7 + 64) >> 7); \
        STORE(dst[y*dst_stride+  9*dst_step], (tm5 - tt6 + 64) >> 7); \
        STORE(dst[y*dst_stride+ 10*dst_step], (tm7 - tt5 + 64) >> 7); \
        STORE(dst[y*dst_stride+ 11*dst_step], (tm3 - tt4 + 64) >> 7); \
        STORE(dst[y*dst_stride+ 12*dst_step], (tm2 - tt3 + 64) >> 7); \
        STORE(dst[y*dst_stride+ 13*dst_step], (tm6 - tt2 + 64) >> 7); \
        STORE(dst[y*dst_stride+ 14*dst_step], (tm4 - tt1 + 64) >> 7); \
        STORE(dst[y*dst_stride+ 15*dst_step], (tm0 - tt0 + 64) >> 7); \
    }
#define STORE(a, x) a = av_clip_intp2(x, 15)
    IDCT16X16(block, 1, 16, tmp, 1, 16)
#undef STORE
#define STORE(a, x) a = av_clip_uint8(a + (x))
    IDCT16X16(tmp, 16, 1, dst, dst_stride, 1)
#undef STORE
}
