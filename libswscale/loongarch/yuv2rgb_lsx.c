/*
 * Copyright (C) 2023 Loongson Technology Co. Ltd.
 * Contributed by Bo Jin(jinbo@loongson.cn)
 * All rights reserved.
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

#include "swscale_loongarch.h"
#include "libavutil/loongarch/loongson_intrinsics.h"

#define YUV2RGB_LOAD_COE                               \
    /* Load x_offset */                                \
    __m128i y_offset = __lsx_vreplgr2vr_d(c->yOffset); \
    __m128i u_offset = __lsx_vreplgr2vr_d(c->uOffset); \
    __m128i v_offset = __lsx_vreplgr2vr_d(c->vOffset); \
    /* Load x_coeff  */                                \
    __m128i ug_coeff = __lsx_vreplgr2vr_d(c->ugCoeff); \
    __m128i vg_coeff = __lsx_vreplgr2vr_d(c->vgCoeff); \
    __m128i y_coeff  = __lsx_vreplgr2vr_d(c->yCoeff);  \
    __m128i ub_coeff = __lsx_vreplgr2vr_d(c->ubCoeff); \
    __m128i vr_coeff = __lsx_vreplgr2vr_d(c->vrCoeff); \

#define LOAD_YUV_16                                                   \
    m_y1 = __lsx_vld(py_1, 0);                                        \
    m_y2 = __lsx_vld(py_2, 0);                                        \
    m_u  = __lsx_vldrepl_d(pu, 0);                                    \
    m_v  = __lsx_vldrepl_d(pv, 0);                                    \
    DUP2_ARG2(__lsx_vilvl_b, m_u, m_u, m_v, m_v, m_u, m_v);           \
    DUP2_ARG2(__lsx_vilvh_b, zero, m_u, zero, m_v, m_u_h, m_v_h);     \
    DUP2_ARG2(__lsx_vilvl_b, zero, m_u, zero, m_v, m_u, m_v);         \
    DUP2_ARG2(__lsx_vilvh_b, zero, m_y1, zero, m_y2, m_y1_h, m_y2_h); \
    DUP2_ARG2(__lsx_vilvl_b, zero, m_y1, zero, m_y2, m_y1, m_y2);     \

/* YUV2RGB method
 * The conversion method is as follows:
 * R = Y' * y_coeff + V' * vr_coeff
 * G = Y' * y_coeff + V' * vg_coeff + U' * ug_coeff
 * B = Y' * y_coeff + U' * ub_coeff
 *
 * where X' = X * 8 - x_offset
 *
 */

#define YUV2RGB(y1, y2, u, v, r1, g1, b1, r2, g2, b2)               \
{                                                                   \
    y1  = __lsx_vslli_h(y1, 3);                                     \
    y2  = __lsx_vslli_h(y2, 3);                                     \
    u   = __lsx_vslli_h(u, 3);                                      \
    v   = __lsx_vslli_h(v, 3);                                      \
    y1  = __lsx_vsub_h(y1, y_offset);                               \
    y2  = __lsx_vsub_h(y2, y_offset);                               \
    u   = __lsx_vsub_h(u, u_offset);                                \
    v   = __lsx_vsub_h(v, v_offset);                                \
    y_1 = __lsx_vmuh_h(y1, y_coeff);                                \
    y_2 = __lsx_vmuh_h(y2, y_coeff);                                \
    u2g = __lsx_vmuh_h(u, ug_coeff);                                \
    u2b = __lsx_vmuh_h(u, ub_coeff);                                \
    v2r = __lsx_vmuh_h(v, vr_coeff);                                \
    v2g = __lsx_vmuh_h(v, vg_coeff);                                \
    r1  = __lsx_vsadd_h(y_1, v2r);                                  \
    v2g = __lsx_vsadd_h(v2g, u2g);                                  \
    g1  = __lsx_vsadd_h(y_1, v2g);                                  \
    b1  = __lsx_vsadd_h(y_1, u2b);                                  \
    r2  = __lsx_vsadd_h(y_2, v2r);                                  \
    g2  = __lsx_vsadd_h(y_2, v2g);                                  \
    b2  = __lsx_vsadd_h(y_2, u2b);                                  \
    DUP4_ARG1(__lsx_vclip255_h, r1, g1, b1, r2, r1, g1, b1, r2);    \
    DUP2_ARG1(__lsx_vclip255_h, g2, b2, g2, b2);                    \
}

#define RGB_PACK(r, g, b, rgb_l, rgb_h)                                 \
{                                                                       \
    __m128i rg;                                                         \
    rg = __lsx_vpackev_b(g, r);                                         \
    DUP2_ARG3(__lsx_vshuf_b, b, rg, shuf2, b, rg, shuf3, rgb_l, rgb_h); \
}

#define RGB32_PACK(a, r, g, b, rgb_l, rgb_h)                         \
{                                                                    \
    __m128i ra, bg;                                                  \
    ra    = __lsx_vpackev_b(r, a);                                   \
    bg    = __lsx_vpackev_b(b, g);                                   \
    rgb_l = __lsx_vilvl_h(bg, ra);                                   \
    rgb_h = __lsx_vilvh_h(bg, ra);                                   \
}

#define RGB_STORE(rgb_l, rgb_h, image)                               \
{                                                                    \
    __lsx_vstelm_d(rgb_l, image, 0,  0);                             \
    __lsx_vstelm_d(rgb_l, image, 8,  1);                             \
    __lsx_vstelm_d(rgb_h, image, 16, 0);                             \
}

#define RGB32_STORE(rgb_l, rgb_h, image)                             \
{                                                                    \
    __lsx_vst(rgb_l, image, 0);                                      \
    __lsx_vst(rgb_h, image, 16);                                     \
}

#define YUV2RGBFUNC(func_name, dst_type, alpha)                                     \
           int func_name(SwsContext *c, const uint8_t *src[],                       \
                         int srcStride[], int srcSliceY, int srcSliceH,             \
                         uint8_t *dst[], int dstStride[])                           \
{                                                                                   \
    int x, y, h_size, vshift, res;                                                  \
    __m128i m_y1, m_y2, m_u, m_v;                                                   \
    __m128i m_y1_h, m_y2_h, m_u_h, m_v_h;                                           \
    __m128i y_1, y_2, u2g, v2g, u2b, v2r, rgb1_l, rgb1_h;                           \
    __m128i rgb2_l, rgb2_h, r1, g1, b1, r2, g2, b2;                                 \
    __m128i shuf2 = {0x0504120302100100, 0x0A18090816070614};                       \
    __m128i shuf3 = {0x1E0F0E1C0D0C1A0B, 0x0101010101010101};                       \
    __m128i zero = __lsx_vldi(0);                                                   \
                                                                                    \
    YUV2RGB_LOAD_COE                                                                \
                                                                                    \
    h_size = c->dstW >> 4;                                                          \
    res = (c->dstW & 15) >> 1;                                                      \
    vshift = c->srcFormat != AV_PIX_FMT_YUV422P;                                    \
    for (y = 0; y < srcSliceH; y += 2) {                                            \
        dst_type av_unused *r, *g, *b;                                              \
        dst_type *image1    = (dst_type *)(dst[0] + (y + srcSliceY) * dstStride[0]);\
        dst_type *image2    = (dst_type *)(image1 +                   dstStride[0]);\
        const uint8_t *py_1 = src[0] +               y * srcStride[0];              \
        const uint8_t *py_2 = py_1   +                   srcStride[0];              \
        const uint8_t *pu   = src[1] +   (y >> vshift) * srcStride[1];              \
        const uint8_t *pv   = src[2] +   (y >> vshift) * srcStride[2];              \
        for(x = 0; x < h_size; x++) {                                               \

#define YUV2RGBFUNC32(func_name, dst_type, alpha)                                   \
           int func_name(SwsContext *c, const uint8_t *src[],                       \
                         int srcStride[], int srcSliceY, int srcSliceH,             \
                         uint8_t *dst[], int dstStride[])                           \
{                                                                                   \
    int x, y, h_size, vshift, res;                                                  \
    __m128i m_y1, m_y2, m_u, m_v;                                                   \
    __m128i m_y1_h, m_y2_h, m_u_h, m_v_h;                                           \
    __m128i y_1, y_2, u2g, v2g, u2b, v2r, rgb1_l, rgb1_h;                           \
    __m128i rgb2_l, rgb2_h, r1, g1, b1, r2, g2, b2;                                 \
    __m128i a = __lsx_vldi(0xFF);                                                   \
    __m128i zero = __lsx_vldi(0);                                                   \
                                                                                    \
    YUV2RGB_LOAD_COE                                                                \
                                                                                    \
    h_size = c->dstW >> 4;                                                          \
    res = (c->dstW & 15) >> 1;                                                      \
    vshift = c->srcFormat != AV_PIX_FMT_YUV422P;                                    \
    for (y = 0; y < srcSliceH; y += 2) {                                            \
        int yd = y + srcSliceY;                                                     \
        dst_type av_unused *r, *g, *b;                                              \
        dst_type *image1    = (dst_type *)(dst[0] + (yd)     * dstStride[0]);       \
        dst_type *image2    = (dst_type *)(dst[0] + (yd + 1) * dstStride[0]);       \
        const uint8_t *py_1 = src[0] +               y * srcStride[0];              \
        const uint8_t *py_2 = py_1   +                   srcStride[0];              \
        const uint8_t *pu   = src[1] +   (y >> vshift) * srcStride[1];              \
        const uint8_t *pv   = src[2] +   (y >> vshift) * srcStride[2];              \
        for(x = 0; x < h_size; x++) {                                               \

#define DEALYUV2RGBREMAIN                                                           \
            py_1 += 16;                                                             \
            py_2 += 16;                                                             \
            pu += 8;                                                                \
            pv += 8;                                                                \
            image1 += 48;                                                           \
            image2 += 48;                                                           \
        }                                                                           \
        for (x = 0; x < res; x++) {                                                 \
            int av_unused U, V, Y;                                                  \
            U = pu[0];                                                              \
            V = pv[0];                                                              \
            r = (void *)c->table_rV[V+YUVRGB_TABLE_HEADROOM];                       \
            g = (void *)(c->table_gU[U+YUVRGB_TABLE_HEADROOM]                       \
                       + c->table_gV[V+YUVRGB_TABLE_HEADROOM]);                     \
            b = (void *)c->table_bU[U+YUVRGB_TABLE_HEADROOM];

#define DEALYUV2RGBREMAIN32                                                         \
            py_1 += 16;                                                             \
            py_2 += 16;                                                             \
            pu += 8;                                                                \
            pv += 8;                                                                \
            image1 += 16;                                                           \
            image2 += 16;                                                           \
        }                                                                           \
        for (x = 0; x < res; x++) {                                                 \
            int av_unused U, V, Y;                                                  \
            U = pu[0];                                                              \
            V = pv[0];                                                              \
            r = (void *)c->table_rV[V+YUVRGB_TABLE_HEADROOM];                       \
            g = (void *)(c->table_gU[U+YUVRGB_TABLE_HEADROOM]                       \
                       + c->table_gV[V+YUVRGB_TABLE_HEADROOM]);                     \
            b = (void *)c->table_bU[U+YUVRGB_TABLE_HEADROOM];                       \

#define PUTRGB24(dst, src)                  \
    Y      = src[0];                        \
    dst[0] = r[Y];                          \
    dst[1] = g[Y];                          \
    dst[2] = b[Y];                          \
    Y      = src[1];                        \
    dst[3] = r[Y];                          \
    dst[4] = g[Y];                          \
    dst[5] = b[Y];

#define PUTBGR24(dst, src)                  \
    Y      = src[0];                        \
    dst[0] = b[Y];                          \
    dst[1] = g[Y];                          \
    dst[2] = r[Y];                          \
    Y      = src[1];                        \
    dst[3] = b[Y];                          \
    dst[4] = g[Y];                          \
    dst[5] = r[Y];

#define PUTRGB(dst, src)                    \
    Y      = src[0];                        \
    dst[0] = r[Y] + g[Y] + b[Y];            \
    Y      = src[1];                        \
    dst[1] = r[Y] + g[Y] + b[Y];            \

#define ENDRES                              \
    pu += 1;                                \
    pv += 1;                                \
    py_1 += 2;                              \
    py_2 += 2;                              \
    image1 += 6;                            \
    image2 += 6;                            \

#define ENDRES32                            \
    pu += 1;                                \
    pv += 1;                                \
    py_1 += 2;                              \
    py_2 += 2;                              \
    image1 += 2;                            \
    image2 += 2;                            \

#define END_FUNC()                          \
        }                                   \
    }                                       \
    return srcSliceH;                       \
}

YUV2RGBFUNC(yuv420_rgb24_lsx, uint8_t, 0)
    LOAD_YUV_16
    YUV2RGB(m_y1, m_y2, m_u, m_v, r1, g1, b1, r2, g2, b2);
    RGB_PACK(r1, g1, b1, rgb1_l, rgb1_h);
    RGB_PACK(r2, g2, b2, rgb2_l, rgb2_h);
    RGB_STORE(rgb1_l, rgb1_h, image1);
    RGB_STORE(rgb2_l, rgb2_h, image2);
    YUV2RGB(m_y1_h, m_y2_h, m_u_h, m_v_h, r1, g1, b1, r2, g2, b2);
    RGB_PACK(r1, g1, b1, rgb1_l, rgb1_h);
    RGB_PACK(r2, g2, b2, rgb2_l, rgb2_h);
    RGB_STORE(rgb1_l, rgb1_h, image1 + 24);
    RGB_STORE(rgb2_l, rgb2_h, image2 + 24);
    DEALYUV2RGBREMAIN
    PUTRGB24(image1, py_1);
    PUTRGB24(image2, py_2);
    ENDRES
    END_FUNC()

YUV2RGBFUNC(yuv420_bgr24_lsx, uint8_t, 0)
    LOAD_YUV_16
    YUV2RGB(m_y1, m_y2, m_u, m_v, r1, g1, b1, r2, g2, b2);
    RGB_PACK(b1, g1, r1, rgb1_l, rgb1_h);
    RGB_PACK(b2, g2, r2, rgb2_l, rgb2_h);
    RGB_STORE(rgb1_l, rgb1_h, image1);
    RGB_STORE(rgb2_l, rgb2_h, image2);
    YUV2RGB(m_y1_h, m_y2_h, m_u_h, m_v_h, r1, g1, b1, r2, g2, b2);
    RGB_PACK(b1, g1, r1, rgb1_l, rgb1_h);
    RGB_PACK(b2, g2, r2, rgb2_l, rgb2_h);
    RGB_STORE(rgb1_l, rgb1_h, image1 + 24);
    RGB_STORE(rgb2_l, rgb2_h, image2 + 24);
    DEALYUV2RGBREMAIN
    PUTBGR24(image1, py_1);
    PUTBGR24(image2, py_2);
    ENDRES
    END_FUNC()

YUV2RGBFUNC32(yuv420_rgba32_lsx, uint32_t, 0)
    LOAD_YUV_16
    YUV2RGB(m_y1, m_y2, m_u, m_v, r1, g1, b1, r2, g2, b2);
    RGB32_PACK(r1, g1, b1, a, rgb1_l, rgb1_h);
    RGB32_PACK(r2, g2, b2, a, rgb2_l, rgb2_h);
    RGB32_STORE(rgb1_l, rgb1_h, image1);
    RGB32_STORE(rgb2_l, rgb2_h, image2);
    YUV2RGB(m_y1_h, m_y2_h, m_u_h, m_v_h, r1, g1, b1, r2, g2, b2);
    RGB32_PACK(r1, g1, b1, a, rgb1_l, rgb1_h);
    RGB32_PACK(r2, g2, b2, a, rgb2_l, rgb2_h);
    RGB32_STORE(rgb1_l, rgb1_h, image1 + 8);
    RGB32_STORE(rgb2_l, rgb2_h, image2 + 8);
    DEALYUV2RGBREMAIN32
    PUTRGB(image1, py_1);
    PUTRGB(image2, py_2);
    ENDRES32
    END_FUNC()

YUV2RGBFUNC32(yuv420_bgra32_lsx, uint32_t, 0)
    LOAD_YUV_16
    YUV2RGB(m_y1, m_y2, m_u, m_v, r1, g1, b1, r2, g2, b2);
    RGB32_PACK(b1, g1, r1, a, rgb1_l, rgb1_h);
    RGB32_PACK(b2, g2, r2, a, rgb2_l, rgb2_h);
    RGB32_STORE(rgb1_l, rgb1_h, image1);
    RGB32_STORE(rgb2_l, rgb2_h, image2);
    YUV2RGB(m_y1_h, m_y2_h, m_u_h, m_v_h, r1, g1, b1, r2, g2, b2);
    RGB32_PACK(b1, g1, r1, a, rgb1_l, rgb1_h);
    RGB32_PACK(b2, g2, r2, a, rgb2_l, rgb2_h);
    RGB32_STORE(rgb1_l, rgb1_h, image1 + 8);
    RGB32_STORE(rgb2_l, rgb2_h, image2 + 8);
    DEALYUV2RGBREMAIN32
    PUTRGB(image1, py_1);
    PUTRGB(image2, py_2);
    ENDRES32
    END_FUNC()

YUV2RGBFUNC32(yuv420_argb32_lsx, uint32_t, 0)
    LOAD_YUV_16
    YUV2RGB(m_y1, m_y2, m_u, m_v, r1, g1, b1, r2, g2, b2);
    RGB32_PACK(a, r1, g1, b1, rgb1_l, rgb1_h);
    RGB32_PACK(a, r2, g2, b2, rgb2_l, rgb2_h);
    RGB32_STORE(rgb1_l, rgb1_h, image1);
    RGB32_STORE(rgb2_l, rgb2_h, image2);
    YUV2RGB(m_y1_h, m_y2_h, m_u_h, m_v_h, r1, g1, b1, r2, g2, b2);
    RGB32_PACK(a, r1, g1, b1, rgb1_l, rgb1_h);
    RGB32_PACK(a, r2, g2, b2, rgb2_l, rgb2_h);
    RGB32_STORE(rgb1_l, rgb1_h, image1 + 8);
    RGB32_STORE(rgb2_l, rgb2_h, image2 + 8);
    DEALYUV2RGBREMAIN32
    PUTRGB(image1, py_1);
    PUTRGB(image2, py_2);
    ENDRES32
    END_FUNC()

YUV2RGBFUNC32(yuv420_abgr32_lsx, uint32_t, 0)
    LOAD_YUV_16
    YUV2RGB(m_y1, m_y2, m_u, m_v, r1, g1, b1, r2, g2, b2);
    RGB32_PACK(a, b1, g1, r1, rgb1_l, rgb1_h);
    RGB32_PACK(a, b2, g2, r2, rgb2_l, rgb2_h);
    RGB32_STORE(rgb1_l, rgb1_h, image1);
    RGB32_STORE(rgb2_l, rgb2_h, image2);
    YUV2RGB(m_y1_h, m_y2_h, m_u_h, m_v_h, r1, g1, b1, r2, g2, b2);
    RGB32_PACK(a, b1, g1, r1, rgb1_l, rgb1_h);
    RGB32_PACK(a, b2, g2, r2, rgb2_l, rgb2_h);
    RGB32_STORE(rgb1_l, rgb1_h, image1 + 8);
    RGB32_STORE(rgb2_l, rgb2_h, image2 + 8);
    DEALYUV2RGBREMAIN32
    PUTRGB(image1, py_1);
    PUTRGB(image2, py_2);
    ENDRES32
    END_FUNC()
