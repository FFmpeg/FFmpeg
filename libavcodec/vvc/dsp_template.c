/*
 * VVC transform and residual DSP
 *
 * Copyright (C) 2021 Nuo Mi
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
#include "libavutil/frame.h"
#include "libavcodec/bit_depth_template.c"

#include "dec.h"
#include "data.h"

#include "inter_template.c"
#include "intra_template.c"
#include "filter_template.c"

static void FUNC(add_residual)(uint8_t *_dst, const int *res,
    const int w, const int h, const ptrdiff_t _stride)
{
    pixel *dst          = (pixel *)_dst;

    const int stride    = _stride / sizeof(pixel);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            dst[x] = av_clip_pixel(dst[x] + *res);
            res++;
        }
        dst += stride;
    }
}

static void FUNC(pred_residual_joint)(int *dst, const int *src, const int w, const int h,
    const int c_sign, const int shift)
{
    const int size = w * h;
    for (int i = 0; i < size; i++)
        dst[i] = (src[i] * c_sign) >> shift;
}

static void FUNC(transform_bdpcm)(int *coeffs, const int width, const int height,
    const int vertical, const int log2_transform_range)
{
    int x, y;

    if (vertical) {
        coeffs += width;
        for (y = 0; y < height - 1; y++) {
            for (x = 0; x < width; x++)
                coeffs[x] = av_clip_intp2(coeffs[x] + coeffs[x - width], log2_transform_range);
            coeffs += width;
        }
    } else {
        for (y = 0; y < height; y++) {
            for (x = 1; x < width; x++)
                coeffs[x] = av_clip_intp2(coeffs[x] + coeffs[x - 1], log2_transform_range);
            coeffs += width;
        }
    }
}

// 8.7.4.6 Residual modification process for blocks using colour space conversion
static void FUNC(adaptive_color_transform)(int *y, int *u, int *v, const int width, const int height)
{
    const int size = width * height;
    const int bits = BIT_DEPTH + 1;

    for (int i = 0; i < size; i++) {
        const int y0 = av_clip_intp2(y[i], bits);
        const int cg = av_clip_intp2(u[i], bits);
        const int co = av_clip_intp2(v[i], bits);
        const int t  = y0 - (cg >> 1);

        y[i] = cg + t;
        u[i] = t - (co >> 1);
        v[i] = co + u[i];
    }
}

static void FUNC(ff_vvc_itx_dsp_init)(VVCItxDSPContext *const itx)
{
#define VVC_ITX(TYPE, type, s)                                                  \
        itx->itx[VVC_##TYPE][VVC_##TX_SIZE_##s]      = ff_vvc_inv_##type##_##s;             \

#define VVC_ITX_COMMON(TYPE, type)                                              \
        VVC_ITX(TYPE, type, 4);                                                 \
        VVC_ITX(TYPE, type, 8);                                                 \
        VVC_ITX(TYPE, type, 16);                                                \
        VVC_ITX(TYPE, type, 32);

    itx->add_residual                = FUNC(add_residual);
    itx->pred_residual_joint         = FUNC(pred_residual_joint);
    itx->transform_bdpcm             = FUNC(transform_bdpcm);
    VVC_ITX(DCT2, dct2, 2)
    VVC_ITX(DCT2, dct2, 64)
    VVC_ITX_COMMON(DCT2, dct2)
    VVC_ITX_COMMON(DCT8, dct8)
    VVC_ITX_COMMON(DST7, dst7)

    itx->adaptive_color_transform = FUNC(adaptive_color_transform);

#undef VVC_ITX
#undef VVC_ITX_COMMON
}
