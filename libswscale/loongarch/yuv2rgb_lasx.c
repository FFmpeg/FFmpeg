/*
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 * Contributed by Hao Chen(chenhao@loongson.cn)
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

#define YUV2RGB_LOAD_COE                                     \
    /* Load x_offset */                                      \
    __m256i y_offset = __lasx_xvreplgr2vr_d(c->yOffset);     \
    __m256i u_offset = __lasx_xvreplgr2vr_d(c->uOffset);     \
    __m256i v_offset = __lasx_xvreplgr2vr_d(c->vOffset);     \
    /* Load x_coeff  */                                      \
    __m256i ug_coeff = __lasx_xvreplgr2vr_d(c->ugCoeff);     \
    __m256i vg_coeff = __lasx_xvreplgr2vr_d(c->vgCoeff);     \
    __m256i y_coeff  = __lasx_xvreplgr2vr_d(c->yCoeff);      \
    __m256i ub_coeff = __lasx_xvreplgr2vr_d(c->ubCoeff);     \
    __m256i vr_coeff = __lasx_xvreplgr2vr_d(c->vrCoeff);     \

#define LOAD_YUV_16                                          \
    m_y1 = __lasx_xvld(py_1, 0);                             \
    m_y2 = __lasx_xvld(py_2, 0);                             \
    m_u  = __lasx_xvldrepl_d(pu, 0);                         \
    m_v  = __lasx_xvldrepl_d(pv, 0);                         \
    m_u  = __lasx_xvilvl_b(m_u, m_u);                        \
    m_v  = __lasx_xvilvl_b(m_v, m_v);                        \
    DUP4_ARG1(__lasx_vext2xv_hu_bu, m_y1, m_y2, m_u, m_v,    \
              m_y1, m_y2, m_u, m_v);                         \

/* YUV2RGB method
 * The conversion method is as follows:
 * R = Y' * y_coeff + V' * vr_coeff
 * G = Y' * y_coeff + V' * vg_coeff + U' * ug_coeff
 * B = Y' * y_coeff + U' * ub_coeff
 *
 * where X' = X * 8 - x_offset
 *
 */

#define YUV2RGB                                                            \
    m_y1 = __lasx_xvslli_h(m_y1, 3);                                       \
    m_y2 = __lasx_xvslli_h(m_y2, 3);                                       \
    m_u  = __lasx_xvslli_h(m_u, 3);                                        \
    m_v  = __lasx_xvslli_h(m_v, 3);                                        \
    m_y1 = __lasx_xvsub_h(m_y1, y_offset);                                 \
    m_y2 = __lasx_xvsub_h(m_y2, y_offset);                                 \
    m_u  = __lasx_xvsub_h(m_u, u_offset);                                  \
    m_v  = __lasx_xvsub_h(m_v, v_offset);                                  \
    y_1  = __lasx_xvmuh_h(m_y1, y_coeff);                                  \
    y_2  = __lasx_xvmuh_h(m_y2, y_coeff);                                  \
    u2g  = __lasx_xvmuh_h(m_u, ug_coeff);                                  \
    u2b  = __lasx_xvmuh_h(m_u, ub_coeff);                                  \
    v2r  = __lasx_xvmuh_h(m_v, vr_coeff);                                  \
    v2g  = __lasx_xvmuh_h(m_v, vg_coeff);                                  \
    r1   = __lasx_xvsadd_h(y_1, v2r);                                      \
    v2g  = __lasx_xvsadd_h(v2g, u2g);                                      \
    g1   = __lasx_xvsadd_h(y_1, v2g);                                      \
    b1   = __lasx_xvsadd_h(y_1, u2b);                                      \
    r2   = __lasx_xvsadd_h(y_2, v2r);                                      \
    g2   = __lasx_xvsadd_h(y_2, v2g);                                      \
    b2   = __lasx_xvsadd_h(y_2, u2b);                                      \
    DUP4_ARG1(__lasx_xvclip255_h, r1, g1, b1, r2, r1, g1, b1, r2);         \
    DUP2_ARG1(__lasx_xvclip255_h, g2, b2, g2, b2);                         \

#define YUV2RGB_RES                                                        \
    m_y1 = __lasx_xvldrepl_d(py_1, 0);                       \
    m_y2 = __lasx_xvldrepl_d(py_2, 0);                       \
    m_u  = __lasx_xvldrepl_d(pu, 0);                         \
    m_v  = __lasx_xvldrepl_d(pv, 0);                         \
    m_y1 = __lasx_xvilvl_d(m_y2, m_y1);                      \
    m_u  = __lasx_xvilvl_b(m_u, m_u);                        \
    m_v  = __lasx_xvilvl_b(m_v, m_v);                        \
    m_y1 = __lasx_vext2xv_hu_bu(m_y1);                       \
    m_u  = __lasx_vext2xv_hu_bu(m_u);                        \
    m_v  = __lasx_vext2xv_hu_bu(m_v);                        \
    m_y1 = __lasx_xvslli_h(m_y1, 3);                         \
    m_u  = __lasx_xvslli_h(m_u, 3);                          \
    m_v  = __lasx_xvslli_h(m_v, 3);                          \
    m_y1 = __lasx_xvsub_h(m_y1, y_offset);                   \
    m_u  = __lasx_xvsub_h(m_u, u_offset);                    \
    m_v  = __lasx_xvsub_h(m_v, v_offset);                    \
    y_1  = __lasx_xvmuh_h(m_y1, y_coeff);                    \
    u2g  = __lasx_xvmuh_h(m_u, ug_coeff);                    \
    u2b  = __lasx_xvmuh_h(m_u, ub_coeff);                    \
    v2r  = __lasx_xvmuh_h(m_v, vr_coeff);                    \
    v2g  = __lasx_xvmuh_h(m_v, vg_coeff);                    \
    r1   = __lasx_xvsadd_h(y_1, v2r);                        \
    v2g  = __lasx_xvsadd_h(v2g, u2g);                        \
    g1   = __lasx_xvsadd_h(y_1, v2g);                        \
    b1   = __lasx_xvsadd_h(y_1, u2b);                        \
    r1   = __lasx_xvclip255_h(r1);                           \
    g1   = __lasx_xvclip255_h(g1);                           \
    b1   = __lasx_xvclip255_h(b1);                           \

#define RGB_PACK(r, g, b, rgb_l, rgb_h)                                    \
{                                                                          \
    __m256i rg;                                                            \
    rg = __lasx_xvpackev_b(g, r);                                          \
    DUP2_ARG3(__lasx_xvshuf_b, b, rg, shuf2, b, rg, shuf3, rgb_l, rgb_h);  \
}

#define RGB32_PACK(a, r, g, b, rgb_l, rgb_h)                               \
{                                                                          \
    __m256i ra, bg, tmp0, tmp1;                                            \
    ra    = __lasx_xvpackev_b(r, a);                                       \
    bg    = __lasx_xvpackev_b(b, g);                                       \
    tmp0  = __lasx_xvilvl_h(bg, ra);                                       \
    tmp1  = __lasx_xvilvh_h(bg, ra);                                       \
    rgb_l = __lasx_xvpermi_q(tmp1, tmp0, 0x20);                            \
    rgb_h = __lasx_xvpermi_q(tmp1, tmp0, 0x31);                            \
}

#define RGB_STORE_RES(rgb_l, rgb_h, image_1, image_2)                      \
{                                                                          \
    __lasx_xvstelm_d(rgb_l, image_1, 0,  0);                               \
    __lasx_xvstelm_d(rgb_l, image_1, 8,  1);                               \
    __lasx_xvstelm_d(rgb_h, image_1, 16, 0);                               \
    __lasx_xvstelm_d(rgb_l, image_2, 0,  2);                               \
    __lasx_xvstelm_d(rgb_l, image_2, 8,  3);                               \
    __lasx_xvstelm_d(rgb_h, image_2, 16, 2);                               \
}

#define RGB_STORE(rgb_l, rgb_h, image)                                       \
{                                                                            \
    __lasx_xvstelm_d(rgb_l, image, 0,  0);                                   \
    __lasx_xvstelm_d(rgb_l, image, 8,  1);                                   \
    __lasx_xvstelm_d(rgb_h, image, 16, 0);                                   \
    __lasx_xvstelm_d(rgb_l, image, 24, 2);                                   \
    __lasx_xvstelm_d(rgb_l, image, 32, 3);                                   \
    __lasx_xvstelm_d(rgb_h, image, 40, 2);                                   \
}

#define RGB32_STORE(rgb_l, rgb_h, image)                                     \
{                                                                            \
    __lasx_xvst(rgb_l, image, 0);                                            \
    __lasx_xvst(rgb_h, image, 32);                                           \
}

#define RGB32_STORE_RES(rgb_l, rgb_h, image_1, image_2)                      \
{                                                                            \
    __lasx_xvst(rgb_l, image_1, 0);                                          \
    __lasx_xvst(rgb_h, image_2, 0);                                          \
}

#define YUV2RGBFUNC(func_name, dst_type, alpha)                                     \
           int func_name(SwsContext *c, const uint8_t *src[],                       \
                         int srcStride[], int srcSliceY, int srcSliceH,             \
                         uint8_t *dst[], int dstStride[])                           \
{                                                                                   \
    int x, y, h_size, vshift, res;                                                  \
    __m256i m_y1, m_y2, m_u, m_v;                                                   \
    __m256i y_1, y_2, u2g, v2g, u2b, v2r, rgb1_l, rgb1_h;                           \
    __m256i rgb2_l, rgb2_h, r1, g1, b1, r2, g2, b2;                                 \
    __m256i shuf2 = {0x0504120302100100, 0x0A18090816070614,                        \
                     0x0504120302100100, 0x0A18090816070614};                       \
    __m256i shuf3 = {0x1E0F0E1C0D0C1A0B, 0x0101010101010101,                        \
                     0x1E0F0E1C0D0C1A0B, 0x0101010101010101};                       \
    YUV2RGB_LOAD_COE                                                                \
    y      = (c->dstW + 7) & ~7;                                                    \
    h_size = y >> 4;                                                                \
    res    = y & 15;                                                                \
                                                                                    \
    vshift = c->srcFormat != AV_PIX_FMT_YUV422P;                                    \
    for (y = 0; y < srcSliceH; y += 2) {                                            \
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
    __m256i m_y1, m_y2, m_u, m_v;                                                   \
    __m256i y_1, y_2, u2g, v2g, u2b, v2r, rgb1_l, rgb1_h;                           \
    __m256i rgb2_l, rgb2_h, r1, g1, b1, r2, g2, b2;                                 \
    __m256i a = __lasx_xvldi(0xFF);                                                 \
                                                                                    \
    YUV2RGB_LOAD_COE                                                                \
    y      = (c->dstW + 7) & ~7;                                                    \
    h_size = y >> 4;                                                                \
    res    = y & 15;                                                                \
                                                                                    \
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
        if (res) {                                                                  \

#define DEALYUV2RGBREMAIN32                                                         \
            py_1 += 16;                                                             \
            py_2 += 16;                                                             \
            pu += 8;                                                                \
            pv += 8;                                                                \
            image1 += 16;                                                           \
            image2 += 16;                                                           \
        }                                                                           \
        if (res) {                                                                  \


#define END_FUNC()                          \
        }                                   \
    }                                       \
    return srcSliceH;                       \
}

YUV2RGBFUNC(yuv420_rgb24_lasx, uint8_t, 0)
    LOAD_YUV_16
    YUV2RGB
    RGB_PACK(r1, g1, b1, rgb1_l, rgb1_h);
    RGB_PACK(r2, g2, b2, rgb2_l, rgb2_h);
    RGB_STORE(rgb1_l, rgb1_h, image1);
    RGB_STORE(rgb2_l, rgb2_h, image2);
    DEALYUV2RGBREMAIN
    YUV2RGB_RES
    RGB_PACK(r1, g1, b1, rgb1_l, rgb1_h);
    RGB_STORE_RES(rgb1_l, rgb1_h, image1, image2);
    END_FUNC()

YUV2RGBFUNC(yuv420_bgr24_lasx, uint8_t, 0)
    LOAD_YUV_16
    YUV2RGB
    RGB_PACK(b1, g1, r1, rgb1_l, rgb1_h);
    RGB_PACK(b2, g2, r2, rgb2_l, rgb2_h);
    RGB_STORE(rgb1_l, rgb1_h, image1);
    RGB_STORE(rgb2_l, rgb2_h, image2);
    DEALYUV2RGBREMAIN
    YUV2RGB_RES
    RGB_PACK(b1, g1, r1, rgb1_l, rgb1_h);
    RGB_STORE_RES(rgb1_l, rgb1_h, image1, image2);
    END_FUNC()

YUV2RGBFUNC32(yuv420_rgba32_lasx, uint32_t, 0)
    LOAD_YUV_16
    YUV2RGB
    RGB32_PACK(r1, g1, b1, a, rgb1_l, rgb1_h);
    RGB32_PACK(r2, g2, b2, a, rgb2_l, rgb2_h);
    RGB32_STORE(rgb1_l, rgb1_h, image1);
    RGB32_STORE(rgb2_l, rgb2_h, image2);
    DEALYUV2RGBREMAIN32
    YUV2RGB_RES
    RGB32_PACK(r1, g1, b1, a, rgb1_l, rgb1_h);
    RGB32_STORE_RES(rgb1_l, rgb1_h, image1, image2);
    END_FUNC()

YUV2RGBFUNC32(yuv420_bgra32_lasx, uint32_t, 0)
    LOAD_YUV_16
    YUV2RGB
    RGB32_PACK(b1, g1, r1, a, rgb1_l, rgb1_h);
    RGB32_PACK(b2, g2, r2, a, rgb2_l, rgb2_h);
    RGB32_STORE(rgb1_l, rgb1_h, image1);
    RGB32_STORE(rgb2_l, rgb2_h, image2);
    DEALYUV2RGBREMAIN32
    YUV2RGB_RES
    RGB32_PACK(b1, g1, r1, a, rgb1_l, rgb1_h);
    RGB32_STORE_RES(rgb1_l, rgb1_h, image1, image2);
    END_FUNC()

YUV2RGBFUNC32(yuv420_argb32_lasx, uint32_t, 0)
    LOAD_YUV_16
    YUV2RGB
    RGB32_PACK(a, r1, g1, b1, rgb1_l, rgb1_h);
    RGB32_PACK(a, r2, g2, b2, rgb2_l, rgb2_h);
    RGB32_STORE(rgb1_l, rgb1_h, image1);
    RGB32_STORE(rgb2_l, rgb2_h, image2);
    DEALYUV2RGBREMAIN32
    YUV2RGB_RES
    RGB32_PACK(a, r1, g1, b1, rgb1_l, rgb1_h);
    RGB32_STORE_RES(rgb1_l, rgb1_h, image1, image2);
    END_FUNC()

YUV2RGBFUNC32(yuv420_abgr32_lasx, uint32_t, 0)
    LOAD_YUV_16
    YUV2RGB
    RGB32_PACK(a, b1, g1, r1, rgb1_l, rgb1_h);
    RGB32_PACK(a, b2, g2, r2, rgb2_l, rgb2_h);
    RGB32_STORE(rgb1_l, rgb1_h, image1);
    RGB32_STORE(rgb2_l, rgb2_h, image2);
    DEALYUV2RGBREMAIN32
    YUV2RGB_RES
    RGB32_PACK(a, b1, g1, r1, rgb1_l, rgb1_h);
    RGB32_STORE_RES(rgb1_l, rgb1_h, image1, image2);
    END_FUNC()
