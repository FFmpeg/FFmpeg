/*
 * software YUV to RGB converter
 *
 * Copyright (C) 2009 Konstantin Shishkov
 *
 * 1,4,8bpp support and context / deglobalize stuff
 * by Michael Niedermayer (michaelni@gmx.at)
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

#include <stddef.h>
#include <stdint.h>

#include "libavutil/bswap.h"
#include "libavutil/mem.h"
#include "config.h"
#include "swscale.h"
#include "swscale_internal.h"
#include "libavutil/pixdesc.h"

/* Color space conversion coefficients for YCbCr -> RGB mapping.
 *
 * Entries are {crv, cbu, cgu, cgv}
 *
 *   crv = (255 / 224) * 65536 * (1 - cr) / 0.5
 *   cbu = (255 / 224) * 65536 * (1 - cb) / 0.5
 *   cgu = (255 / 224) * 65536 * (cb / cg) * (1 - cb) / 0.5
 *   cgv = (255 / 224) * 65536 * (cr / cg) * (1 - cr) / 0.5
 *
 * where Y = cr * R + cg * G + cb * B and cr + cg + cb = 1.
 */
const int32_t ff_yuv2rgb_coeffs[11][4] = {
    { 104597, 132201, 25675, 53279 }, /* no sequence_display_extension */
    { 117489, 138438, 13975, 34925 }, /* ITU-R Rec. 709 (1990) */
    { 104597, 132201, 25675, 53279 }, /* unspecified */
    { 104597, 132201, 25675, 53279 }, /* reserved */
    { 104448, 132798, 24759, 53109 }, /* FCC */
    { 104597, 132201, 25675, 53279 }, /* ITU-R Rec. 624-4 System B, G */
    { 104597, 132201, 25675, 53279 }, /* SMPTE 170M */
    { 117579, 136230, 16907, 35559 }, /* SMPTE 240M (1987) */
    {      0                       }, /* YCgCo */
    { 110013, 140363, 12277, 42626 }, /* Bt-2020-NCL */
    { 110013, 140363, 12277, 42626 }, /* Bt-2020-CL */
};

const int *sws_getCoefficients(int colorspace)
{
    if (colorspace > 10 || colorspace < 0 || colorspace == 8)
        colorspace = SWS_CS_DEFAULT;
    return ff_yuv2rgb_coeffs[colorspace];
}

#define LOADCHROMA(l, i)                            \
    U = pu_##l[i];                                  \
    V = pv_##l[i];                                  \
    r = (void *)c->table_rV[V+YUVRGB_TABLE_HEADROOM];                     \
    g = (void *)(c->table_gU[U+YUVRGB_TABLE_HEADROOM] + c->table_gV[V+YUVRGB_TABLE_HEADROOM]);  \
    b = (void *)c->table_bU[U+YUVRGB_TABLE_HEADROOM];

#define PUTRGB(l, i, abase)                         \
    Y                  = py_##l[2 * i];             \
    dst_##l[2 * i]     = r[Y] + g[Y] + b[Y];        \
    Y                  = py_##l[2 * i + 1];         \
    dst_##l[2 * i + 1] = r[Y] + g[Y] + b[Y];

#define PUTRGB24(l, i, abase)                       \
    Y                  = py_##l[2 * i];             \
    dst_##l[6 * i + 0] = r[Y];                      \
    dst_##l[6 * i + 1] = g[Y];                      \
    dst_##l[6 * i + 2] = b[Y];                      \
    Y                  = py_##l[2 * i + 1];         \
    dst_##l[6 * i + 3] = r[Y];                      \
    dst_##l[6 * i + 4] = g[Y];                      \
    dst_##l[6 * i + 5] = b[Y];

#define PUTBGR24(l, i, abase)                       \
    Y                  = py_##l[2 * i];             \
    dst_##l[6 * i + 0] = b[Y];                      \
    dst_##l[6 * i + 1] = g[Y];                      \
    dst_##l[6 * i + 2] = r[Y];                      \
    Y                  = py_##l[2 * i + 1];         \
    dst_##l[6 * i + 3] = b[Y];                      \
    dst_##l[6 * i + 4] = g[Y];                      \
    dst_##l[6 * i + 5] = r[Y];

#define PUTRGBA(l, i, abase)                        \
    Y                  = py_##l[2 * i];             \
    dst_##l[2 * i]     = r[Y] + g[Y] + b[Y] + ((uint32_t)(pa_##l[2 * i])     << abase); \
    Y                  = py_##l[2 * i + 1];         \
    dst_##l[2 * i + 1] = r[Y] + g[Y] + b[Y] + ((uint32_t)(pa_##l[2 * i + 1]) << abase);

#define PUTRGB48(l, i, abase)                       \
    Y                    = py_##l[ 2 * i];          \
    dst_##l[12 * i +  0] = dst_##l[12 * i +  1] = r[Y]; \
    dst_##l[12 * i +  2] = dst_##l[12 * i +  3] = g[Y]; \
    dst_##l[12 * i +  4] = dst_##l[12 * i +  5] = b[Y]; \
    Y                    = py_##l[ 2 * i + 1];      \
    dst_##l[12 * i +  6] = dst_##l[12 * i +  7] = r[Y]; \
    dst_##l[12 * i +  8] = dst_##l[12 * i +  9] = g[Y]; \
    dst_##l[12 * i + 10] = dst_##l[12 * i + 11] = b[Y];

#define PUTBGR48(l, i, abase)                       \
    Y                    = py_##l[2 * i];           \
    dst_##l[12 * i +  0] = dst_##l[12 * i +  1] = b[Y]; \
    dst_##l[12 * i +  2] = dst_##l[12 * i +  3] = g[Y]; \
    dst_##l[12 * i +  4] = dst_##l[12 * i +  5] = r[Y]; \
    Y                    = py_##l[2  * i +  1];     \
    dst_##l[12 * i +  6] = dst_##l[12 * i +  7] = b[Y]; \
    dst_##l[12 * i +  8] = dst_##l[12 * i +  9] = g[Y]; \
    dst_##l[12 * i + 10] = dst_##l[12 * i + 11] = r[Y];

#define PUTGBRP(l, i, abase)                        \
    Y                   = py_##l[2 * i];            \
    dst_##l [2 * i + 0] = g[Y];                     \
    dst1_##l[2 * i + 0] = b[Y];                     \
    dst2_##l[2 * i + 0] = r[Y];                     \
    Y                   = py_##l[2 * i + 1];        \
    dst_##l [2 * i + 1] = g[Y];                     \
    dst1_##l[2 * i + 1] = b[Y];                     \
    dst2_##l[2 * i + 1] = r[Y];

#define YUV2RGBFUNC(func_name, dst_type, alpha, yuv422, nb_dst_planes)      \
    static int func_name(SwsContext *c, const uint8_t *const src[],         \
                         const int srcStride[], int srcSliceY, int srcSliceH, \
                         uint8_t *const dst[], const int dstStride[])       \
    {                                                                       \
        int y;                                                              \
                                                                            \
        for (y = 0; y < srcSliceH; y += 2) {                                \
            int yd = y + srcSliceY;                                         \
            dst_type *dst_1 =                                               \
                (dst_type *)(dst[0] + (yd)     * dstStride[0]);             \
            dst_type *dst_2 =                                               \
                (dst_type *)(dst[0] + (yd + 1) * dstStride[0]);             \
            dst_type av_unused *dst1_1, *dst1_2, *dst2_1, *dst2_2;          \
            dst_type av_unused *r, *g, *b;                                  \
            const uint8_t *py_1 = src[0] +  y       * srcStride[0];         \
            const uint8_t *py_2 = py_1   +            srcStride[0];         \
            const uint8_t av_unused *pu_1 = src[1] + (y >> !yuv422) * srcStride[1]; \
            const uint8_t av_unused *pv_1 = src[2] + (y >> !yuv422) * srcStride[2]; \
            const uint8_t av_unused *pu_2, *pv_2;                           \
            const uint8_t av_unused *pa_1, *pa_2;                           \
            unsigned int h_size = c->dstW >> 3;                             \
            if (nb_dst_planes > 1) {                                        \
                dst1_1 = (dst_type *)(dst[1] + (yd)     * dstStride[1]);    \
                dst1_2 = (dst_type *)(dst[1] + (yd + 1) * dstStride[1]);    \
                dst2_1 = (dst_type *)(dst[2] + (yd)     * dstStride[2]);    \
                dst2_2 = (dst_type *)(dst[2] + (yd + 1) * dstStride[2]);    \
            }                                                               \
            if (yuv422) {                                                   \
                pu_2 = pu_1 + srcStride[1];                                 \
                pv_2 = pv_1 + srcStride[2];                                 \
            }                                                               \
            if (alpha) {                                                    \
                pa_1 = src[3] + y * srcStride[3];                           \
                pa_2 = pa_1   +     srcStride[3];                           \
            }                                                               \
            while (h_size--) {                                              \
                int av_unused U, V, Y;                                      \

#define ENDYUV2RGBLINE(dst_delta, ss, alpha, yuv422, nb_dst_planes) \
    pu_1  += 4 >> ss;                               \
    pv_1  += 4 >> ss;                               \
    if (yuv422) {                                   \
        pu_2 += 4 >> ss;                            \
        pv_2 += 4 >> ss;                            \
    }                                               \
    py_1  += 8 >> ss;                               \
    py_2  += 8 >> ss;                               \
    if (alpha) {                                    \
        pa_1 += 8 >> ss;                            \
        pa_2 += 8 >> ss;                            \
    }                                               \
    dst_1 += dst_delta >> ss;                       \
    dst_2 += dst_delta >> ss;                       \
    if (nb_dst_planes > 1) {                        \
        dst1_1 += dst_delta >> ss;                  \
        dst1_2 += dst_delta >> ss;                  \
        dst2_1 += dst_delta >> ss;                  \
        dst2_2 += dst_delta >> ss;                  \
    }                                               \
    }                                               \
    if (c->dstW & (4 >> ss)) {                      \
        int av_unused Y, U, V;                      \

#define ENDYUV2RGBFUNC()                            \
            }                                       \
        }                                           \
        return srcSliceH;                           \
    }

#define YUV420FUNC(func_name, dst_type, alpha, abase, PUTFUNC, dst_delta, nb_dst_planes) \
    YUV2RGBFUNC(func_name, dst_type, alpha, 0, nb_dst_planes)           \
        LOADCHROMA(1, 0);                                               \
        PUTFUNC(1, 0, abase);                                           \
        PUTFUNC(2, 0, abase);                                           \
                                                                        \
        LOADCHROMA(1, 1);                                               \
        PUTFUNC(2, 1, abase);                                           \
        PUTFUNC(1, 1, abase);                                           \
                                                                        \
        LOADCHROMA(1, 2);                                               \
        PUTFUNC(1, 2, abase);                                           \
        PUTFUNC(2, 2, abase);                                           \
                                                                        \
        LOADCHROMA(1, 3);                                               \
        PUTFUNC(2, 3, abase);                                           \
        PUTFUNC(1, 3, abase);                                           \
    ENDYUV2RGBLINE(dst_delta, 0, alpha, 0, nb_dst_planes)               \
        LOADCHROMA(1, 0);                                               \
        PUTFUNC(1, 0, abase);                                           \
        PUTFUNC(2, 0, abase);                                           \
                                                                        \
        LOADCHROMA(1, 1);                                               \
        PUTFUNC(2, 1, abase);                                           \
        PUTFUNC(1, 1, abase);                                           \
    ENDYUV2RGBLINE(dst_delta, 1, alpha, 0, nb_dst_planes)               \
        LOADCHROMA(1, 0);                                               \
        PUTFUNC(1, 0, abase);                                           \
        PUTFUNC(2, 0, abase);                                           \
    ENDYUV2RGBFUNC()

#define YUV422FUNC(func_name, dst_type, alpha, abase, PUTFUNC, dst_delta, nb_dst_planes) \
    YUV2RGBFUNC(func_name, dst_type, alpha, 1, nb_dst_planes)           \
        LOADCHROMA(1, 0);                                               \
        PUTFUNC(1, 0, abase);                                           \
                                                                        \
        LOADCHROMA(2, 0);                                               \
        PUTFUNC(2, 0, abase);                                           \
                                                                        \
        LOADCHROMA(2, 1);                                               \
        PUTFUNC(2, 1, abase);                                           \
                                                                        \
        LOADCHROMA(1, 1);                                               \
        PUTFUNC(1, 1, abase);                                           \
                                                                        \
        LOADCHROMA(1, 2);                                               \
        PUTFUNC(1, 2, abase);                                           \
                                                                        \
        LOADCHROMA(2, 2);                                               \
        PUTFUNC(2, 2, abase);                                           \
                                                                        \
        LOADCHROMA(2, 3);                                               \
        PUTFUNC(2, 3, abase);                                           \
                                                                        \
        LOADCHROMA(1, 3);                                               \
        PUTFUNC(1, 3, abase);                                           \
    ENDYUV2RGBLINE(dst_delta, 0, alpha, 1, nb_dst_planes)               \
        LOADCHROMA(1, 0);                                               \
        PUTFUNC(1, 0, abase);                                           \
                                                                        \
        LOADCHROMA(2, 0);                                               \
        PUTFUNC(2, 0, abase);                                           \
                                                                        \
        LOADCHROMA(2, 1);                                               \
        PUTFUNC(2, 1, abase);                                           \
                                                                        \
        LOADCHROMA(1, 1);                                               \
        PUTFUNC(1, 1, abase);                                           \
    ENDYUV2RGBLINE(dst_delta, 1, alpha, 1, nb_dst_planes)               \
        LOADCHROMA(1, 0);                                               \
        PUTFUNC(1, 0, abase);                                           \
                                                                        \
        LOADCHROMA(2, 0);                                               \
        PUTFUNC(2, 0, abase);                                           \
    ENDYUV2RGBFUNC()

#define YUV420FUNC_DITHER(func_name, dst_type, LOADDITHER, PUTFUNC, dst_delta) \
    YUV2RGBFUNC(func_name, dst_type, 0, 0, 1)                           \
        LOADDITHER                                                      \
                                                                        \
        LOADCHROMA(1, 0);                                               \
        PUTFUNC(1, 0, 0);                                               \
        PUTFUNC(2, 0, 0 + 8);                                           \
                                                                        \
        LOADCHROMA(1, 1);                                               \
        PUTFUNC(2, 1, 2 + 8);                                           \
        PUTFUNC(1, 1, 2);                                               \
                                                                        \
        LOADCHROMA(1, 2);                                               \
        PUTFUNC(1, 2, 4);                                               \
        PUTFUNC(2, 2, 4 + 8);                                           \
                                                                        \
        LOADCHROMA(1, 3);                                               \
        PUTFUNC(2, 3, 6 + 8);                                           \
        PUTFUNC(1, 3, 6);                                               \
    ENDYUV2RGBLINE(dst_delta, 0, 0, 0, 1)                               \
        LOADDITHER                                                      \
                                                                        \
        LOADCHROMA(1, 0);                                               \
        PUTFUNC(1, 0, 0);                                               \
        PUTFUNC(2, 0, 0 + 8);                                           \
                                                                        \
        LOADCHROMA(1, 1);                                               \
        PUTFUNC(2, 1, 2 + 8);                                           \
        PUTFUNC(1, 1, 2);                                               \
    ENDYUV2RGBLINE(dst_delta, 1, 0, 0, 1)                               \
        LOADDITHER                                                      \
                                                                        \
        LOADCHROMA(1, 0);                                               \
        PUTFUNC(1, 0, 0);                                               \
        PUTFUNC(2, 0, 0 + 8);                                           \
    ENDYUV2RGBFUNC()

#define YUV422FUNC_DITHER(func_name, dst_type, LOADDITHER, PUTFUNC, dst_delta) \
    YUV2RGBFUNC(func_name, dst_type, 0, 1, 1)                           \
        LOADDITHER                                                      \
                                                                        \
        LOADCHROMA(1, 0);                                               \
        PUTFUNC(1, 0, 0);                                               \
                                                                        \
        LOADCHROMA(2, 0);                                               \
        PUTFUNC(2, 0, 0 + 8);                                           \
                                                                        \
        LOADCHROMA(2, 1);                                               \
        PUTFUNC(2, 1, 2 + 8);                                           \
                                                                        \
        LOADCHROMA(1, 1);                                               \
        PUTFUNC(1, 1, 2);                                               \
                                                                        \
        LOADCHROMA(1, 2);                                               \
        PUTFUNC(1, 2, 4);                                               \
                                                                        \
        LOADCHROMA(2, 2);                                               \
        PUTFUNC(2, 2, 4 + 8);                                           \
                                                                        \
        LOADCHROMA(2, 3);                                               \
        PUTFUNC(2, 3, 6 + 8);                                           \
                                                                        \
        LOADCHROMA(1, 3);                                               \
        PUTFUNC(1, 3, 6);                                               \
    ENDYUV2RGBLINE(dst_delta, 0, 0, 1, 1)                               \
        LOADDITHER                                                      \
                                                                        \
        LOADCHROMA(1, 0);                                               \
        PUTFUNC(1, 0, 0);                                               \
                                                                        \
        LOADCHROMA(2, 0);                                               \
        PUTFUNC(2, 0, 0 + 8);                                           \
                                                                        \
        LOADCHROMA(2, 1);                                               \
        PUTFUNC(2, 1, 2 + 8);                                           \
                                                                        \
        LOADCHROMA(1, 1);                                               \
        PUTFUNC(1, 1, 2);                                               \
    ENDYUV2RGBLINE(dst_delta, 1, 0, 1, 1)                               \
        LOADDITHER                                                      \
                                                                        \
        LOADCHROMA(1, 0);                                               \
        PUTFUNC(1, 0, 0);                                               \
                                                                        \
        LOADCHROMA(2, 0);                                               \
        PUTFUNC(2, 0, 0 + 8);                                           \
    ENDYUV2RGBFUNC()

#define LOADDITHER16                                    \
    const uint8_t *d16 = ff_dither_2x2_8[y & 1];        \
    const uint8_t *e16 = ff_dither_2x2_4[y & 1];        \
    const uint8_t *f16 = ff_dither_2x2_8[(y & 1)^1];

#define PUTRGB16(l, i, o)                           \
    Y                  = py_##l[2 * i];             \
    dst_##l[2 * i]     = r[Y + d16[0 + o]] +        \
                         g[Y + e16[0 + o]] +        \
                         b[Y + f16[0 + o]];         \
    Y                  = py_##l[2 * i + 1];         \
    dst_##l[2 * i + 1] = r[Y + d16[1 + o]] +        \
                         g[Y + e16[1 + o]] +        \
                         b[Y + f16[1 + o]];

#define LOADDITHER15                                    \
    const uint8_t *d16 = ff_dither_2x2_8[y & 1];        \
    const uint8_t *e16 = ff_dither_2x2_8[(y & 1)^1];

#define PUTRGB15(l, i, o)                           \
    Y                  = py_##l[2 * i];             \
    dst_##l[2 * i]     = r[Y + d16[0 + o]] +        \
                         g[Y + d16[1 + o]] +        \
                         b[Y + e16[0 + o]];         \
    Y                  = py_##l[2 * i + 1];         \
    dst_##l[2 * i + 1] = r[Y + d16[1 + o]] +        \
                         g[Y + d16[0 + o]] +        \
                         b[Y + e16[1 + o]];

#define LOADDITHER12                                    \
    const uint8_t *d16 = ff_dither_4x4_16[y & 3];

#define PUTRGB12(l, i, o)                           \
    Y                  = py_##l[2 * i];             \
    dst_##l[2 * i]     = r[Y + d16[0 + o]] +        \
                         g[Y + d16[0 + o]] +        \
                         b[Y + d16[0 + o]];         \
    Y                  = py_##l[2 * i + 1];         \
    dst_##l[2 * i + 1] = r[Y + d16[1 + o]] +        \
                         g[Y + d16[1 + o]] +        \
                         b[Y + d16[1 + o]];

#define LOADDITHER8                                     \
    const uint8_t *d32 = ff_dither_8x8_32[yd & 7];      \
    const uint8_t *d64 = ff_dither_8x8_73[yd & 7];

#define PUTRGB8(l, i, o)                            \
    Y                  = py_##l[2 * i];             \
    dst_##l[2 * i]     = r[Y + d32[0 + o]] +        \
                         g[Y + d32[0 + o]] +        \
                         b[Y + d64[0 + o]];         \
    Y                  = py_##l[2 * i + 1];         \
    dst_##l[2 * i + 1] = r[Y + d32[1 + o]] +        \
                         g[Y + d32[1 + o]] +        \
                         b[Y + d64[1 + o]];

#define LOADDITHER4D                                    \
    const uint8_t * d64 = ff_dither_8x8_73[yd & 7];     \
    const uint8_t *d128 = ff_dither_8x8_220[yd & 7];    \
    int acc;

#define PUTRGB4D(l, i, o)                           \
    Y      = py_##l[2 * i];                         \
    acc    = r[Y + d128[0 + o]] +                   \
             g[Y +  d64[0 + o]] +                   \
             b[Y + d128[0 + o]];                    \
    Y      = py_##l[2 * i + 1];                     \
    acc   |= (r[Y + d128[1 + o]] +                  \
              g[Y +  d64[1 + o]] +                  \
              b[Y + d128[1 + o]]) << 4;             \
    dst_##l[i] = acc;

#define LOADDITHER4DB                                   \
    const uint8_t *d64  = ff_dither_8x8_73[yd & 7];     \
    const uint8_t *d128 = ff_dither_8x8_220[yd & 7];

#define PUTRGB4DB(l, i, o)                          \
    Y                  = py_##l[2 * i];             \
    dst_##l[2 * i]     = r[Y + d128[0 + o]] +       \
                         g[Y +  d64[0 + o]] +       \
                         b[Y + d128[0 + o]];        \
    Y                  = py_##l[2 * i + 1];         \
    dst_##l[2 * i + 1] = r[Y + d128[1 + o]] +       \
                         g[Y +  d64[1 + o]] +       \
                         b[Y + d128[1 + o]];

YUV2RGBFUNC(yuv2rgb_c_1_ordered_dither, uint8_t, 0, 0, 1)
    const uint8_t *d128 = ff_dither_8x8_220[yd & 7];
    char out_1 = 0, out_2 = 0;
    g = c->table_gU[128 + YUVRGB_TABLE_HEADROOM] + c->table_gV[128 + YUVRGB_TABLE_HEADROOM];

#define PUTRGB1(out, src, i, o)                     \
    Y    = src[2 * i];                              \
    out += out + g[Y + d128[0 + o]];                \
    Y    = src[2 * i + 1];                          \
    out += out + g[Y + d128[1 + o]];

    PUTRGB1(out_1, py_1, 0, 0);
    PUTRGB1(out_2, py_2, 0, 0 + 8);

    PUTRGB1(out_2, py_2, 1, 2 + 8);
    PUTRGB1(out_1, py_1, 1, 2);

    PUTRGB1(out_1, py_1, 2, 4);
    PUTRGB1(out_2, py_2, 2, 4 + 8);

    PUTRGB1(out_2, py_2, 3, 6 + 8);
    PUTRGB1(out_1, py_1, 3, 6);

    dst_1[0] = out_1;
    dst_2[0] = out_2;

    py_1  += 8;
    py_2  += 8;
    dst_1 += 1;
    dst_2 += 1;
    }
    if (c->dstW & 7) {
        int av_unused Y, U, V;
        int pixels_left = c->dstW & 7;
    const uint8_t *d128 = ff_dither_8x8_220[yd & 7];
    char out_1 = 0, out_2 = 0;
    g = c->table_gU[128 + YUVRGB_TABLE_HEADROOM] + c->table_gV[128 + YUVRGB_TABLE_HEADROOM];

#define PUTRGB1_OR00(out, src, i, o)                \
    if (pixels_left) {                              \
        PUTRGB1(out, src, i, o)                     \
        pixels_left--;                              \
    } else {                                        \
        out <<= 2;                                  \
    }

    PUTRGB1_OR00(out_1, py_1, 0, 0);
    PUTRGB1_OR00(out_2, py_2, 0, 0 + 8);

    PUTRGB1_OR00(out_2, py_2, 1, 2 + 8);
    PUTRGB1_OR00(out_1, py_1, 1, 2);

    PUTRGB1_OR00(out_1, py_1, 2, 4);
    PUTRGB1_OR00(out_2, py_2, 2, 4 + 8);

    PUTRGB1_OR00(out_2, py_2, 3, 6 + 8);
    PUTRGB1_OR00(out_1, py_1, 3, 6);

    dst_1[0] = out_1;
    dst_2[0] = out_2;
ENDYUV2RGBFUNC()

// YUV420
YUV420FUNC(yuv2rgb_c_48,     uint8_t,  0,  0, PUTRGB48, 48, 1)
YUV420FUNC(yuv2rgb_c_bgr48,  uint8_t,  0,  0, PUTBGR48, 48, 1)
YUV420FUNC(yuv2rgb_c_32,     uint32_t, 0,  0, PUTRGB,    8, 1)
#if HAVE_BIGENDIAN
YUV420FUNC(yuva2argb_c,      uint32_t, 1, 24, PUTRGBA,   8, 1)
YUV420FUNC(yuva2rgba_c,      uint32_t, 1,  0, PUTRGBA,   8, 1)
#else
YUV420FUNC(yuva2rgba_c,      uint32_t, 1, 24, PUTRGBA,   8, 1)
YUV420FUNC(yuva2argb_c,      uint32_t, 1,  0, PUTRGBA,   8, 1)
#endif
YUV420FUNC(yuv2rgb_c_24_rgb, uint8_t,  0,  0, PUTRGB24, 24, 1)
YUV420FUNC(yuv2rgb_c_24_bgr, uint8_t,  0,  0, PUTBGR24, 24, 1)
YUV420FUNC(yuv420p_gbrp_c,   uint8_t,  0,  0, PUTGBRP,   8, 3)
YUV420FUNC_DITHER(yuv2rgb_c_16_ordered_dither, uint16_t, LOADDITHER16,  PUTRGB16,  8)
YUV420FUNC_DITHER(yuv2rgb_c_15_ordered_dither, uint16_t, LOADDITHER15,  PUTRGB15,  8)
YUV420FUNC_DITHER(yuv2rgb_c_12_ordered_dither, uint16_t, LOADDITHER12,  PUTRGB12,  8)
YUV420FUNC_DITHER(yuv2rgb_c_8_ordered_dither,  uint8_t,  LOADDITHER8,   PUTRGB8,   8)
YUV420FUNC_DITHER(yuv2rgb_c_4_ordered_dither,  uint8_t,  LOADDITHER4D,  PUTRGB4D,  4)
YUV420FUNC_DITHER(yuv2rgb_c_4b_ordered_dither, uint8_t,  LOADDITHER4DB, PUTRGB4DB, 8)

// YUV422
YUV422FUNC(yuv422p_rgb48_c,  uint8_t,  0,  0, PUTRGB48, 48, 1)
YUV422FUNC(yuv422p_bgr48_c,  uint8_t,  0,  0, PUTBGR48, 48, 1)
YUV422FUNC(yuv422p_rgb32_c,  uint32_t, 0,  0, PUTRGB,    8, 1)
#if HAVE_BIGENDIAN
YUV422FUNC(yuva422p_argb_c,  uint32_t, 1, 24, PUTRGBA,   8, 1)
YUV422FUNC(yuva422p_rgba_c,  uint32_t, 1,  0, PUTRGBA,   8, 1)
#else
YUV422FUNC(yuva422p_rgba_c,  uint32_t, 1, 24, PUTRGBA,   8, 1)
YUV422FUNC(yuva422p_argb_c,  uint32_t, 1,  0, PUTRGBA,   8, 1)
#endif
YUV422FUNC(yuv422p_rgb24_c,  uint8_t,  0,  0, PUTRGB24, 24, 1)
YUV422FUNC(yuv422p_bgr24_c,  uint8_t,  0,  0, PUTBGR24, 24, 1)
YUV422FUNC(yuv422p_gbrp_c,   uint8_t,  0,  0, PUTGBRP,   8, 3)
YUV422FUNC_DITHER(yuv422p_bgr16,     uint16_t, LOADDITHER16,  PUTRGB16,  8)
YUV422FUNC_DITHER(yuv422p_bgr15,     uint16_t, LOADDITHER15,  PUTRGB15,  8)
YUV422FUNC_DITHER(yuv422p_bgr12,     uint16_t, LOADDITHER12,  PUTRGB12,  8)
YUV422FUNC_DITHER(yuv422p_bgr8,      uint8_t,  LOADDITHER8,   PUTRGB8,   8)
YUV422FUNC_DITHER(yuv422p_bgr4,      uint8_t,  LOADDITHER4D,  PUTRGB4D,  4)
YUV422FUNC_DITHER(yuv422p_bgr4_byte, uint8_t,  LOADDITHER4DB, PUTRGB4DB, 8)

SwsFunc ff_yuv2rgb_get_func_ptr(SwsContext *c)
{
    SwsFunc t = NULL;

#if ARCH_PPC
    t = ff_yuv2rgb_init_ppc(c);
#elif ARCH_X86
    t = ff_yuv2rgb_init_x86(c);
#elif ARCH_LOONGARCH64
    t = ff_yuv2rgb_init_loongarch(c);
#endif

    if (t)
        return t;

    av_log(c, AV_LOG_WARNING,
           "No accelerated colorspace conversion found from %s to %s.\n",
           av_get_pix_fmt_name(c->srcFormat), av_get_pix_fmt_name(c->dstFormat));

    if (c->srcFormat == AV_PIX_FMT_YUV422P) {
        switch (c->dstFormat) {
        case AV_PIX_FMT_BGR48BE:
        case AV_PIX_FMT_BGR48LE:
            return yuv422p_bgr48_c;
        case AV_PIX_FMT_RGB48BE:
        case AV_PIX_FMT_RGB48LE:
            return yuv422p_rgb48_c;
        case AV_PIX_FMT_ARGB:
        case AV_PIX_FMT_ABGR:
            if (CONFIG_SWSCALE_ALPHA && isALPHA(c->srcFormat))
                return yuva422p_argb_c;
        case AV_PIX_FMT_RGBA:
        case AV_PIX_FMT_BGRA:
            return (CONFIG_SWSCALE_ALPHA && isALPHA(c->srcFormat)) ? yuva422p_rgba_c : yuv422p_rgb32_c;
        case AV_PIX_FMT_RGB24:
            return yuv422p_rgb24_c;
        case AV_PIX_FMT_BGR24:
            return yuv422p_bgr24_c;
        case AV_PIX_FMT_RGB565:
        case AV_PIX_FMT_BGR565:
            return yuv422p_bgr16;
        case AV_PIX_FMT_RGB555:
        case AV_PIX_FMT_BGR555:
            return yuv422p_bgr15;
        case AV_PIX_FMT_RGB444:
        case AV_PIX_FMT_BGR444:
            return yuv422p_bgr12;
        case AV_PIX_FMT_RGB8:
        case AV_PIX_FMT_BGR8:
            return yuv422p_bgr8;
        case AV_PIX_FMT_RGB4:
        case AV_PIX_FMT_BGR4:
            return yuv422p_bgr4;
        case AV_PIX_FMT_RGB4_BYTE:
        case AV_PIX_FMT_BGR4_BYTE:
            return yuv422p_bgr4_byte;
        case AV_PIX_FMT_MONOBLACK:
            return yuv2rgb_c_1_ordered_dither;
        case AV_PIX_FMT_GBRP:
            return yuv422p_gbrp_c;
        }
    } else {
        switch (c->dstFormat) {
        case AV_PIX_FMT_BGR48BE:
        case AV_PIX_FMT_BGR48LE:
            return yuv2rgb_c_bgr48;
        case AV_PIX_FMT_RGB48BE:
        case AV_PIX_FMT_RGB48LE:
            return yuv2rgb_c_48;
        case AV_PIX_FMT_ARGB:
        case AV_PIX_FMT_ABGR:
            if (CONFIG_SWSCALE_ALPHA && isALPHA(c->srcFormat))
                return yuva2argb_c;
        case AV_PIX_FMT_RGBA:
        case AV_PIX_FMT_BGRA:
            return (CONFIG_SWSCALE_ALPHA && isALPHA(c->srcFormat)) ? yuva2rgba_c : yuv2rgb_c_32;
        case AV_PIX_FMT_RGB24:
            return yuv2rgb_c_24_rgb;
        case AV_PIX_FMT_BGR24:
            return yuv2rgb_c_24_bgr;
        case AV_PIX_FMT_RGB565:
        case AV_PIX_FMT_BGR565:
            return yuv2rgb_c_16_ordered_dither;
        case AV_PIX_FMT_RGB555:
        case AV_PIX_FMT_BGR555:
            return yuv2rgb_c_15_ordered_dither;
        case AV_PIX_FMT_RGB444:
        case AV_PIX_FMT_BGR444:
            return yuv2rgb_c_12_ordered_dither;
        case AV_PIX_FMT_RGB8:
        case AV_PIX_FMT_BGR8:
            return yuv2rgb_c_8_ordered_dither;
        case AV_PIX_FMT_RGB4:
        case AV_PIX_FMT_BGR4:
            return yuv2rgb_c_4_ordered_dither;
        case AV_PIX_FMT_RGB4_BYTE:
        case AV_PIX_FMT_BGR4_BYTE:
            return yuv2rgb_c_4b_ordered_dither;
        case AV_PIX_FMT_MONOBLACK:
            return yuv2rgb_c_1_ordered_dither;
        case AV_PIX_FMT_GBRP:
            return yuv420p_gbrp_c;
        }
    }
    return NULL;
}

static void fill_table(uint8_t* table[256 + 2*YUVRGB_TABLE_HEADROOM], const int elemsize,
                       const int64_t inc, void *y_tab)
{
    int i;
    uint8_t *y_table = y_tab;

    y_table -= elemsize * (inc >> 9);

    for (i = 0; i < 256 + 2*YUVRGB_TABLE_HEADROOM; i++) {
        int64_t cb = av_clip_uint8(i-YUVRGB_TABLE_HEADROOM)*inc;
        table[i] = y_table + elemsize * (cb >> 16);
    }
}

static void fill_gv_table(int table[256 + 2*YUVRGB_TABLE_HEADROOM], const int elemsize, const int64_t inc)
{
    int i;
    int off    = -(inc >> 9);

    for (i = 0; i < 256 + 2*YUVRGB_TABLE_HEADROOM; i++) {
        int64_t cb = av_clip_uint8(i-YUVRGB_TABLE_HEADROOM)*inc;
        table[i] = elemsize * (off + (cb >> 16));
    }
}

static uint16_t roundToInt16(int64_t f)
{
    int r = (f + (1 << 15)) >> 16;

    if (r < -0x7FFF)
        return 0x8000;
    else if (r > 0x7FFF)
        return 0x7FFF;
    else
        return r;
}

av_cold int ff_yuv2rgb_c_init_tables(SwsContext *c, const int inv_table[4],
                                     int fullRange, int brightness,
                                     int contrast, int saturation)
{
    const int isRgb = c->dstFormat == AV_PIX_FMT_RGB32     ||
                      c->dstFormat == AV_PIX_FMT_RGB32_1   ||
                      c->dstFormat == AV_PIX_FMT_BGR24     ||
                      c->dstFormat == AV_PIX_FMT_RGB565BE  ||
                      c->dstFormat == AV_PIX_FMT_RGB565LE  ||
                      c->dstFormat == AV_PIX_FMT_RGB555BE  ||
                      c->dstFormat == AV_PIX_FMT_RGB555LE  ||
                      c->dstFormat == AV_PIX_FMT_RGB444BE  ||
                      c->dstFormat == AV_PIX_FMT_RGB444LE  ||
                      c->dstFormat == AV_PIX_FMT_X2RGB10BE ||
                      c->dstFormat == AV_PIX_FMT_X2RGB10LE ||
                      c->dstFormat == AV_PIX_FMT_RGB8      ||
                      c->dstFormat == AV_PIX_FMT_RGB4      ||
                      c->dstFormat == AV_PIX_FMT_RGB4_BYTE ||
                      c->dstFormat == AV_PIX_FMT_MONOBLACK;
    const int isNotNe = c->dstFormat == AV_PIX_FMT_NE(RGB565LE, RGB565BE) ||
                        c->dstFormat == AV_PIX_FMT_NE(RGB555LE, RGB555BE) ||
                        c->dstFormat == AV_PIX_FMT_NE(RGB444LE, RGB444BE) ||
                        c->dstFormat == AV_PIX_FMT_NE(BGR565LE, BGR565BE) ||
                        c->dstFormat == AV_PIX_FMT_NE(BGR555LE, BGR555BE) ||
                        c->dstFormat == AV_PIX_FMT_NE(BGR444LE, BGR444BE) ||
                        c->dstFormat == AV_PIX_FMT_NE(X2RGB10LE, X2RGB10BE) ||
                        c->dstFormat == AV_PIX_FMT_NE(X2BGR10LE, X2BGR10BE);
    const int bpp = c->dstFormatBpp;
    uint8_t *y_table;
    uint16_t *y_table16;
    uint32_t *y_table32;
    int i, base, rbase, gbase, bbase, av_uninit(abase), needAlpha;
    const int yoffs = (fullRange ? 384 : 326) + YUVRGB_TABLE_LUMA_HEADROOM;
    const int table_plane_size = 1024 + 2*YUVRGB_TABLE_LUMA_HEADROOM;

    int64_t crv =  inv_table[0];
    int64_t cbu =  inv_table[1];
    int64_t cgu = -inv_table[2];
    int64_t cgv = -inv_table[3];
    int64_t cy  = 1 << 16;
    int64_t oy  = 0;
    int64_t yb  = 0;

    if (!fullRange) {
        cy = (cy * 255) / 219;
        oy = 16 << 16;
    } else {
        crv = (crv * 224) / 255;
        cbu = (cbu * 224) / 255;
        cgu = (cgu * 224) / 255;
        cgv = (cgv * 224) / 255;
    }

    cy   = (cy  * contrast)              >> 16;
    crv  = (crv * contrast * saturation) >> 32;
    cbu  = (cbu * contrast * saturation) >> 32;
    cgu  = (cgu * contrast * saturation) >> 32;
    cgv  = (cgv * contrast * saturation) >> 32;
    oy  -= 256LL * brightness;

    c->uOffset = 0x0400040004000400LL;
    c->vOffset = 0x0400040004000400LL;
    c->yCoeff  = roundToInt16(cy  * (1 << 13)) * 0x0001000100010001ULL;
    c->vrCoeff = roundToInt16(crv * (1 << 13)) * 0x0001000100010001ULL;
    c->ubCoeff = roundToInt16(cbu * (1 << 13)) * 0x0001000100010001ULL;
    c->vgCoeff = roundToInt16(cgv * (1 << 13)) * 0x0001000100010001ULL;
    c->ugCoeff = roundToInt16(cgu * (1 << 13)) * 0x0001000100010001ULL;
    c->yOffset = roundToInt16(oy  * (1 <<  3)) * 0x0001000100010001ULL;

    c->yuv2rgb_y_coeff   = (int16_t)roundToInt16(cy  * (1 << 13));
    c->yuv2rgb_y_offset  = (int16_t)roundToInt16(oy  * (1 <<  9));
    c->yuv2rgb_v2r_coeff = (int16_t)roundToInt16(crv * (1 << 13));
    c->yuv2rgb_v2g_coeff = (int16_t)roundToInt16(cgv * (1 << 13));
    c->yuv2rgb_u2g_coeff = (int16_t)roundToInt16(cgu * (1 << 13));
    c->yuv2rgb_u2b_coeff = (int16_t)roundToInt16(cbu * (1 << 13));

    //scale coefficients by cy
    crv = ((crv * (1 << 16)) + 0x8000) / FFMAX(cy, 1);
    cbu = ((cbu * (1 << 16)) + 0x8000) / FFMAX(cy, 1);
    cgu = ((cgu * (1 << 16)) + 0x8000) / FFMAX(cy, 1);
    cgv = ((cgv * (1 << 16)) + 0x8000) / FFMAX(cy, 1);

    av_freep(&c->yuvTable);

#define ALLOC_YUV_TABLE(x)          \
        c->yuvTable = av_malloc(x); \
        if (!c->yuvTable)           \
            return AVERROR(ENOMEM);
    switch (bpp) {
    case 1:
        ALLOC_YUV_TABLE(table_plane_size);
        y_table     = c->yuvTable;
        yb = -(384 << 16) - YUVRGB_TABLE_LUMA_HEADROOM*cy - oy;
        for (i = 0; i < table_plane_size - 110; i++) {
            y_table[i + 110]  = av_clip_uint8((yb + 0x8000) >> 16) >> 7;
            yb               += cy;
        }
        fill_table(c->table_gU, 1, cgu, y_table + yoffs);
        fill_gv_table(c->table_gV, 1, cgv);
        break;
    case 4:
    case 4 | 128:
        rbase       = isRgb ? 3 : 0;
        gbase       = 1;
        bbase       = isRgb ? 0 : 3;
        ALLOC_YUV_TABLE(table_plane_size * 3);
        y_table     = c->yuvTable;
        yb = -(384 << 16) - YUVRGB_TABLE_LUMA_HEADROOM*cy - oy;
        for (i = 0; i < table_plane_size - 110; i++) {
            int yval                = av_clip_uint8((yb + 0x8000) >> 16);
            y_table[i + 110]        = (yval >> 7)        << rbase;
            y_table[i +  37 +   table_plane_size] = ((yval + 43) / 85) << gbase;
            y_table[i + 110 + 2*table_plane_size] = (yval >> 7)        << bbase;
            yb += cy;
        }
        fill_table(c->table_rV, 1, crv, y_table + yoffs);
        fill_table(c->table_gU, 1, cgu, y_table + yoffs +   table_plane_size);
        fill_table(c->table_bU, 1, cbu, y_table + yoffs + 2*table_plane_size);
        fill_gv_table(c->table_gV, 1, cgv);
        break;
    case 8:
        rbase       = isRgb ? 5 : 0;
        gbase       = isRgb ? 2 : 3;
        bbase       = isRgb ? 0 : 6;
        ALLOC_YUV_TABLE(table_plane_size * 3);
        y_table     = c->yuvTable;
        yb = -(384 << 16) - YUVRGB_TABLE_LUMA_HEADROOM*cy - oy;
        for (i = 0; i < table_plane_size - 38; i++) {
            int yval               = av_clip_uint8((yb + 0x8000) >> 16);
            y_table[i + 16]        = ((yval + 18) / 36) << rbase;
            y_table[i + 16 +   table_plane_size] = ((yval + 18) / 36) << gbase;
            y_table[i + 37 + 2*table_plane_size] = ((yval + 43) / 85) << bbase;
            yb += cy;
        }
        fill_table(c->table_rV, 1, crv, y_table + yoffs);
        fill_table(c->table_gU, 1, cgu, y_table + yoffs +   table_plane_size);
        fill_table(c->table_bU, 1, cbu, y_table + yoffs + 2*table_plane_size);
        fill_gv_table(c->table_gV, 1, cgv);
        break;
    case 12:
        rbase       = isRgb ? 8 : 0;
        gbase       = 4;
        bbase       = isRgb ? 0 : 8;
        ALLOC_YUV_TABLE(table_plane_size * 3 * 2);
        y_table16   = c->yuvTable;
        yb = -(384 << 16) - YUVRGB_TABLE_LUMA_HEADROOM*cy - oy;
        for (i = 0; i < table_plane_size; i++) {
            uint8_t yval        = av_clip_uint8((yb + 0x8000) >> 16);
            y_table16[i]        = (yval >> 4) << rbase;
            y_table16[i +   table_plane_size] = (yval >> 4) << gbase;
            y_table16[i + 2*table_plane_size] = (yval >> 4) << bbase;
            yb += cy;
        }
        if (isNotNe)
            for (i = 0; i < table_plane_size * 3; i++)
                y_table16[i] = av_bswap16(y_table16[i]);
        fill_table(c->table_rV, 2, crv, y_table16 + yoffs);
        fill_table(c->table_gU, 2, cgu, y_table16 + yoffs +   table_plane_size);
        fill_table(c->table_bU, 2, cbu, y_table16 + yoffs + 2*table_plane_size);
        fill_gv_table(c->table_gV, 2, cgv);
        break;
    case 15:
    case 16:
        rbase       = isRgb ? bpp - 5 : 0;
        gbase       = 5;
        bbase       = isRgb ? 0 : (bpp - 5);
        ALLOC_YUV_TABLE(table_plane_size * 3 * 2);
        y_table16   = c->yuvTable;
        yb = -(384 << 16) - YUVRGB_TABLE_LUMA_HEADROOM*cy - oy;
        for (i = 0; i < table_plane_size; i++) {
            uint8_t yval        = av_clip_uint8((yb + 0x8000) >> 16);
            y_table16[i]        = (yval >> 3)          << rbase;
            y_table16[i +   table_plane_size] = (yval >> (18 - bpp)) << gbase;
            y_table16[i + 2*table_plane_size] = (yval >> 3)          << bbase;
            yb += cy;
        }
        if (isNotNe)
            for (i = 0; i < table_plane_size * 3; i++)
                y_table16[i] = av_bswap16(y_table16[i]);
        fill_table(c->table_rV, 2, crv, y_table16 + yoffs);
        fill_table(c->table_gU, 2, cgu, y_table16 + yoffs +   table_plane_size);
        fill_table(c->table_bU, 2, cbu, y_table16 + yoffs + 2*table_plane_size);
        fill_gv_table(c->table_gV, 2, cgv);
        break;
    case 24:
    case 48:
        ALLOC_YUV_TABLE(table_plane_size);
        y_table     = c->yuvTable;
        yb = -(384 << 16) - YUVRGB_TABLE_LUMA_HEADROOM*cy - oy;
        for (i = 0; i < table_plane_size; i++) {
            y_table[i]  = av_clip_uint8((yb + 0x8000) >> 16);
            yb         += cy;
        }
        fill_table(c->table_rV, 1, crv, y_table + yoffs);
        fill_table(c->table_gU, 1, cgu, y_table + yoffs);
        fill_table(c->table_bU, 1, cbu, y_table + yoffs);
        fill_gv_table(c->table_gV, 1, cgv);
        break;
    case 30:
        rbase = isRgb ? 20 : 0;
        gbase = 10;
        bbase = isRgb ? 0 : 20;
        needAlpha = CONFIG_SWSCALE_ALPHA && isALPHA(c->srcFormat);
        if (!needAlpha)
            abase = 30;
        ALLOC_YUV_TABLE(table_plane_size * 3 * 4);
        y_table32   = c->yuvTable;
        yb = -(384 << 16) - YUVRGB_TABLE_LUMA_HEADROOM*cy - oy;
        for (i = 0; i < table_plane_size; i++) {
            unsigned yval = av_clip_uintp2((yb + 0x8000) >> 14, 10);
            y_table32[i]= (yval << rbase) + (needAlpha ? 0 : (255u << abase));
            y_table32[i + table_plane_size] = yval << gbase;
            y_table32[i + 2 * table_plane_size] = yval << bbase;
            yb += cy;
        }
        if (isNotNe) {
            for (i = 0; i < table_plane_size * 3; i++)
                y_table32[i] = av_bswap32(y_table32[i]);
        }
        fill_table(c->table_rV, 4, crv, y_table32 + yoffs);
        fill_table(c->table_gU, 4, cgu, y_table32 + yoffs + table_plane_size);
        fill_table(c->table_bU, 4, cbu, y_table32 + yoffs + 2 * table_plane_size);
        fill_gv_table(c->table_gV, 4, cgv);
        break;
    case 32:
    case 64:
        base      = (c->dstFormat == AV_PIX_FMT_RGB32_1 ||
                     c->dstFormat == AV_PIX_FMT_BGR32_1) ? 8 : 0;
        rbase     = base + (isRgb ? 16 : 0);
        gbase     = base + 8;
        bbase     = base + (isRgb ? 0 : 16);
        needAlpha = CONFIG_SWSCALE_ALPHA && isALPHA(c->srcFormat);
        if (!needAlpha)
            abase = (base + 24) & 31;
        ALLOC_YUV_TABLE(table_plane_size * 3 * 4);
        y_table32   = c->yuvTable;
        yb = -(384 << 16) - YUVRGB_TABLE_LUMA_HEADROOM*cy - oy;
        for (i = 0; i < table_plane_size; i++) {
            unsigned yval       = av_clip_uint8((yb + 0x8000) >> 16);
            y_table32[i]        = (yval << rbase) +
                                  (needAlpha ? 0 : (255u << abase));
            y_table32[i +   table_plane_size] =  yval << gbase;
            y_table32[i + 2*table_plane_size] =  yval << bbase;
            yb += cy;
        }
        fill_table(c->table_rV, 4, crv, y_table32 + yoffs);
        fill_table(c->table_gU, 4, cgu, y_table32 + yoffs +   table_plane_size);
        fill_table(c->table_bU, 4, cbu, y_table32 + yoffs + 2*table_plane_size);
        fill_gv_table(c->table_gV, 4, cgv);
        break;
    default:
        if(!isPlanar(c->dstFormat) || bpp <= 24)
            av_log(c, AV_LOG_ERROR, "%ibpp not supported by yuv2rgb\n", bpp);
        return AVERROR(EINVAL);
    }
    return 0;
}
