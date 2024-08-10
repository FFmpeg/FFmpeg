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

static void FUNC(add_residual_joint)(uint8_t *_dst, const int *res,
    const int w, const int h, const ptrdiff_t _stride, const int c_sign, const int shift)
{
    pixel *dst = (pixel *)_dst;

    const int stride = _stride / sizeof(pixel);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int r = ((*res) * c_sign) >> shift;
            dst[x] = av_clip_pixel(dst[x] + r);
            res++;
        }
        dst += stride;
    }
}

static void FUNC(pred_residual_joint)(int *buf, const int w, const int h,
    const int c_sign, const int shift)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            *buf = ((*buf) * c_sign) >> shift;
            buf++;
        }
    }
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
    itx->add_residual_joint          = FUNC(add_residual_joint);
    itx->pred_residual_joint         = FUNC(pred_residual_joint);
    itx->transform_bdpcm             = FUNC(transform_bdpcm);
    VVC_ITX(DCT2, dct2, 2)
    VVC_ITX(DCT2, dct2, 64)
    VVC_ITX_COMMON(DCT2, dct2)
    VVC_ITX_COMMON(DCT8, dct8)
    VVC_ITX_COMMON(DST7, dst7)

#undef VVC_ITX
#undef VVC_ITX_COMMON
}
