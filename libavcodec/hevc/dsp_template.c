/*
 * HEVC video decoder
 *
 * Copyright (C) 2012 - 2013 Guillaume Martres
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

#include "get_bits.h"
#include "hevcdec.h"

#include "bit_depth_template.c"
#include "dsp.h"
#include "h26x/h2656_sao_template.c"
#include "h26x/h2656_inter_template.c"

static void FUNC(put_pcm)(uint8_t *_dst, ptrdiff_t stride, int width, int height,
                          GetBitContext *gb, int pcm_bit_depth)
{
    int x, y;
    pixel *dst = (pixel *)_dst;

    stride /= sizeof(pixel);

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = get_bits(gb, pcm_bit_depth) << (BIT_DEPTH - pcm_bit_depth);
        dst += stride;
    }
}

static av_always_inline void FUNC(add_residual)(uint8_t *_dst, const int16_t *res,
                                                ptrdiff_t stride, int size)
{
    int x, y;
    pixel *dst = (pixel *)_dst;

    stride /= sizeof(pixel);

    for (y = 0; y < size; y++) {
        for (x = 0; x < size; x++) {
            dst[x] = av_clip_pixel(dst[x] + *res);
            res++;
        }
        dst += stride;
    }
}

static void FUNC(add_residual4x4)(uint8_t *_dst, const int16_t *res,
                                  ptrdiff_t stride)
{
    FUNC(add_residual)(_dst, res, stride, 4);
}

static void FUNC(add_residual8x8)(uint8_t *_dst, const int16_t *res,
                                  ptrdiff_t stride)
{
    FUNC(add_residual)(_dst, res, stride, 8);
}

static void FUNC(add_residual16x16)(uint8_t *_dst, const int16_t *res,
                                    ptrdiff_t stride)
{
    FUNC(add_residual)(_dst, res, stride, 16);
}

static void FUNC(add_residual32x32)(uint8_t *_dst, const int16_t *res,
                                    ptrdiff_t stride)
{
    FUNC(add_residual)(_dst, res, stride, 32);
}

static void FUNC(transform_rdpcm)(int16_t *_coeffs, int16_t log2_size, int mode)
{
    int16_t *coeffs = (int16_t *) _coeffs;
    int x, y;
    int size = 1 << log2_size;

    if (mode) {
        coeffs += size;
        for (y = 0; y < size - 1; y++) {
            for (x = 0; x < size; x++)
                coeffs[x] += coeffs[x - size];
            coeffs += size;
        }
    } else {
        for (y = 0; y < size; y++) {
            for (x = 1; x < size; x++)
                coeffs[x] += coeffs[x - 1];
            coeffs += size;
        }
    }
}

static void FUNC(dequant)(int16_t *coeffs, int16_t log2_size)
{
    int shift  = 15 - BIT_DEPTH - log2_size;
    int x, y;
    int size = 1 << log2_size;

    if (shift > 0) {
        int offset = 1 << (shift - 1);
        for (y = 0; y < size; y++) {
            for (x = 0; x < size; x++) {
                *coeffs = (*coeffs + offset) >> shift;
                coeffs++;
            }
        }
    } else {
        for (y = 0; y < size; y++) {
            for (x = 0; x < size; x++) {
                *coeffs = *(uint16_t*)coeffs << -shift;
                coeffs++;
            }
        }
    }
}

#define SET(dst, x)   (dst) = (x)
#define SCALE(dst, x) (dst) = av_clip_int16(((x) + add) >> shift)

#define TR_4x4_LUMA(dst, src, step, assign)                             \
    do {                                                                \
        int c0 = src[0 * step] + src[2 * step];                         \
        int c1 = src[2 * step] + src[3 * step];                         \
        int c2 = src[0 * step] - src[3 * step];                         \
        int c3 = 74 * src[1 * step];                                    \
                                                                        \
        assign(dst[2 * step], 74 * (src[0 * step] -                     \
                                    src[2 * step] +                     \
                                    src[3 * step]));                    \
        assign(dst[0 * step], 29 * c0 + 55 * c1 + c3);                  \
        assign(dst[1 * step], 55 * c2 - 29 * c1 + c3);                  \
        assign(dst[3 * step], 55 * c0 + 29 * c2 - c3);                  \
    } while (0)

static void FUNC(transform_4x4_luma)(int16_t *coeffs)
{
    int i;
    int shift    = 7;
    int add      = 1 << (shift - 1);
    int16_t *src = coeffs;

    for (i = 0; i < 4; i++) {
        TR_4x4_LUMA(src, src, 4, SCALE);
        src++;
    }

    shift = 20 - BIT_DEPTH;
    add   = 1 << (shift - 1);
    for (i = 0; i < 4; i++) {
        TR_4x4_LUMA(coeffs, coeffs, 1, SCALE);
        coeffs += 4;
    }
}

#undef TR_4x4_LUMA

#define TR_4(dst, src, dstep, sstep, assign, end)                 \
    do {                                                          \
        const int e0 = 64 * src[0 * sstep] + 64 * src[2 * sstep]; \
        const int e1 = 64 * src[0 * sstep] - 64 * src[2 * sstep]; \
        const int o0 = 83 * src[1 * sstep] + 36 * src[3 * sstep]; \
        const int o1 = 36 * src[1 * sstep] - 83 * src[3 * sstep]; \
                                                                  \
        assign(dst[0 * dstep], e0 + o0);                          \
        assign(dst[1 * dstep], e1 + o1);                          \
        assign(dst[2 * dstep], e1 - o1);                          \
        assign(dst[3 * dstep], e0 - o0);                          \
    } while (0)

#define TR_8(dst, src, dstep, sstep, assign, end)                 \
    do {                                                          \
        int i, j;                                                 \
        int e_8[4];                                               \
        int o_8[4] = { 0 };                                       \
        for (i = 0; i < 4; i++)                                   \
            for (j = 1; j < end; j += 2)                          \
                o_8[i] += transform[4 * j][i] * src[j * sstep];   \
        TR_4(e_8, src, 1, 2 * sstep, SET, 4);                     \
                                                                  \
        for (i = 0; i < 4; i++) {                                 \
            assign(dst[i * dstep], e_8[i] + o_8[i]);              \
            assign(dst[(7 - i) * dstep], e_8[i] - o_8[i]);        \
        }                                                         \
    } while (0)

#define TR_16(dst, src, dstep, sstep, assign, end)                \
    do {                                                          \
        int i, j;                                                 \
        int e_16[8];                                              \
        int o_16[8] = { 0 };                                      \
        for (i = 0; i < 8; i++)                                   \
            for (j = 1; j < end; j += 2)                          \
                o_16[i] += transform[2 * j][i] * src[j * sstep];  \
        TR_8(e_16, src, 1, 2 * sstep, SET, 8);                    \
                                                                  \
        for (i = 0; i < 8; i++) {                                 \
            assign(dst[i * dstep], e_16[i] + o_16[i]);            \
            assign(dst[(15 - i) * dstep], e_16[i] - o_16[i]);     \
        }                                                         \
    } while (0)

#define TR_32(dst, src, dstep, sstep, assign, end)                \
    do {                                                          \
        int i, j;                                                 \
        int e_32[16];                                             \
        int o_32[16] = { 0 };                                     \
        for (i = 0; i < 16; i++)                                  \
            for (j = 1; j < end; j += 2)                          \
                o_32[i] += transform[j][i] * src[j * sstep];      \
        TR_16(e_32, src, 1, 2 * sstep, SET, end / 2);             \
                                                                  \
        for (i = 0; i < 16; i++) {                                \
            assign(dst[i * dstep], e_32[i] + o_32[i]);            \
            assign(dst[(31 - i) * dstep], e_32[i] - o_32[i]);     \
        }                                                         \
    } while (0)

#define IDCT_VAR4(H)                                              \
    int limit2 = FFMIN(col_limit + 4, H)
#define IDCT_VAR8(H)                                              \
    int limit  = FFMIN(col_limit, H);                             \
    int limit2 = FFMIN(col_limit + 4, H)
#define IDCT_VAR16(H)   IDCT_VAR8(H)
#define IDCT_VAR32(H)   IDCT_VAR8(H)

#define IDCT(H)                                                   \
static void FUNC(idct_ ## H ## x ## H )(int16_t *coeffs,          \
                                        int col_limit)            \
{                                                                 \
    int i;                                                        \
    int      shift = 7;                                           \
    int      add   = 1 << (shift - 1);                            \
    int16_t *src   = coeffs;                                      \
    IDCT_VAR ## H(H);                                             \
                                                                  \
    for (i = 0; i < H; i++) {                                     \
        TR_ ## H(src, src, H, H, SCALE, limit2);                  \
        if (limit2 < H && i%4 == 0 && !!i)                        \
            limit2 -= 4;                                          \
        src++;                                                    \
    }                                                             \
                                                                  \
    shift = 20 - BIT_DEPTH;                                       \
    add   = 1 << (shift - 1);                                     \
    for (i = 0; i < H; i++) {                                     \
        TR_ ## H(coeffs, coeffs, 1, 1, SCALE, limit);             \
        coeffs += H;                                              \
    }                                                             \
}

#define IDCT_DC(H)                                                \
static void FUNC(idct_ ## H ## x ## H ## _dc)(int16_t *coeffs)    \
{                                                                 \
    int i, j;                                                     \
    int shift = 14 - BIT_DEPTH;                                   \
    int add   = 1 << (shift - 1);                                 \
    int coeff = (((coeffs[0] + 1) >> 1) + add) >> shift;          \
                                                                  \
    for (j = 0; j < H; j++) {                                     \
        for (i = 0; i < H; i++) {                                 \
            coeffs[i + j * H] = coeff;                            \
        }                                                         \
    }                                                             \
}

IDCT( 4)
IDCT( 8)
IDCT(16)
IDCT(32)

IDCT_DC( 4)
IDCT_DC( 8)
IDCT_DC(16)
IDCT_DC(32)

#undef TR_4
#undef TR_8
#undef TR_16
#undef TR_32

#undef SET
#undef SCALE

////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////
#define ff_hevc_pel_filters ff_hevc_qpel_filters
#define DECL_HV_FILTER(f)                              \
    const int8_t *hf = ff_hevc_ ## f ## _filters[mx]; \
    const int8_t *vf = ff_hevc_ ## f ## _filters[my];

#define FW_PUT(p, f, t)                                                                                   \
static void FUNC(put_hevc_## f)(int16_t *dst, const uint8_t *src, ptrdiff_t srcstride, int height,        \
                                  intptr_t mx, intptr_t my, int width)                                    \
{                                                                                                         \
    DECL_HV_FILTER(p)                                                                                     \
    FUNC(put_ ## t)(dst, src, srcstride, height, hf, vf, width);                                          \
}

#define FW_PUT_UNI(p, f, t)                                                                               \
static void FUNC(put_hevc_ ## f)(uint8_t *dst, ptrdiff_t dststride, const uint8_t *src,                   \
                                  ptrdiff_t srcstride, int height, intptr_t mx, intptr_t my, int width)   \
{                                                                                                         \
    DECL_HV_FILTER(p)                                                                                     \
    FUNC(put_ ## t)(dst, dststride, src, srcstride, height, hf, vf, width);                           \
}

#define FW_PUT_UNI_W(p, f, t)                                                                             \
static void FUNC(put_hevc_ ## f)(uint8_t *dst, ptrdiff_t dststride, const uint8_t *src,                   \
                                  ptrdiff_t srcstride,int height, int denom, int wx, int ox,              \
                                  intptr_t mx, intptr_t my, int width)                                    \
{                                                                                                         \
    DECL_HV_FILTER(p)                                                                                     \
    FUNC(put_ ## t)(dst, dststride, src, srcstride, height, denom, wx, ox, hf, vf, width);            \
}

#define FW_PUT_FUNCS(f, t, dir)                                       \
    FW_PUT(f, f ## _ ## dir, t ## _ ## dir)                     \
    FW_PUT_UNI(f, f ## _uni_ ## dir, uni_ ## t ## _ ## dir)        \
    FW_PUT_UNI_W(f, f ## _uni_w_ ## dir, uni_## t ## _w_ ## dir)

FW_PUT(pel, pel_pixels, pixels)
FW_PUT_UNI(pel, pel_uni_pixels, uni_pixels)
FW_PUT_UNI_W(pel, pel_uni_w_pixels, uni_w_pixels)

FW_PUT_FUNCS(qpel, luma,   h     )
FW_PUT_FUNCS(qpel, luma,   v     )
FW_PUT_FUNCS(qpel, luma,   hv    )
FW_PUT_FUNCS(epel, chroma, h     )
FW_PUT_FUNCS(epel, chroma, v     )
FW_PUT_FUNCS(epel, chroma, hv    )

static void FUNC(put_hevc_pel_bi_pixels)(uint8_t *_dst, ptrdiff_t _dststride, const uint8_t *_src, ptrdiff_t _srcstride,
                                         const int16_t *src2,
                                         int height, intptr_t mx, intptr_t my, int width)
{
    int x, y;
    const pixel *src    = (const pixel *)_src;
    ptrdiff_t srcstride = _srcstride / sizeof(pixel);
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dststride = _dststride / sizeof(pixel);

    int shift = 14  + 1 - BIT_DEPTH;
#if BIT_DEPTH < 14
    int offset = 1 << (shift - 1);
#else
    int offset = 0;
#endif

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((src[x] << (14 - BIT_DEPTH)) + src2[x] + offset) >> shift);
        src  += srcstride;
        dst  += dststride;
        src2 += MAX_PB_SIZE;
    }
}

static void FUNC(put_hevc_pel_bi_w_pixels)(uint8_t *_dst, ptrdiff_t _dststride, const uint8_t *_src, ptrdiff_t _srcstride,
                                           const int16_t *src2,
                                           int height, int denom, int wx0, int wx1,
                                           int ox0, int ox1, intptr_t mx, intptr_t my, int width)
{
    int x, y;
    const pixel *src    = (const pixel *)_src;
    ptrdiff_t srcstride = _srcstride / sizeof(pixel);
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dststride = _dststride / sizeof(pixel);

    int shift = 14  + 1 - BIT_DEPTH;
    int log2Wd = denom + shift - 1;

    ox0     = ox0 * (1 << (BIT_DEPTH - 8));
    ox1     = ox1 * (1 << (BIT_DEPTH - 8));
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            dst[x] = av_clip_pixel(( (src[x] << (14 - BIT_DEPTH)) * wx1 + src2[x] * wx0 + (ox0 + ox1 + 1) * (1 << log2Wd)) >> (log2Wd + 1));
        }
        src  += srcstride;
        dst  += dststride;
        src2 += MAX_PB_SIZE;
    }
}

////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////
#define QPEL_FILTER(src, stride)                                               \
    (filter[0] * src[x - 3 * stride] +                                         \
     filter[1] * src[x - 2 * stride] +                                         \
     filter[2] * src[x -     stride] +                                         \
     filter[3] * src[x             ] +                                         \
     filter[4] * src[x +     stride] +                                         \
     filter[5] * src[x + 2 * stride] +                                         \
     filter[6] * src[x + 3 * stride] +                                         \
     filter[7] * src[x + 4 * stride])

static void FUNC(put_hevc_qpel_bi_h)(uint8_t *_dst, ptrdiff_t _dststride, const uint8_t *_src, ptrdiff_t _srcstride,
                                     const int16_t *src2,
                                     int height, intptr_t mx, intptr_t my, int width)
{
    int x, y;
    const pixel  *src       = (const pixel*)_src;
    ptrdiff_t     srcstride = _srcstride / sizeof(pixel);
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dststride = _dststride / sizeof(pixel);

    const int8_t *filter    = ff_hevc_qpel_filters[mx];

    int shift = 14  + 1 - BIT_DEPTH;
#if BIT_DEPTH < 14
    int offset = 1 << (shift - 1);
#else
    int offset = 0;
#endif

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((QPEL_FILTER(src, 1) >> (BIT_DEPTH - 8)) + src2[x] + offset) >> shift);
        src  += srcstride;
        dst  += dststride;
        src2 += MAX_PB_SIZE;
    }
}

static void FUNC(put_hevc_qpel_bi_v)(uint8_t *_dst, ptrdiff_t _dststride,
                                     const uint8_t *_src, ptrdiff_t _srcstride, const int16_t *src2,
                                     int height, intptr_t mx, intptr_t my, int width)
{
    int x, y;
    const pixel  *src       = (const pixel*)_src;
    ptrdiff_t     srcstride = _srcstride / sizeof(pixel);
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dststride = _dststride / sizeof(pixel);

    const int8_t *filter    = ff_hevc_qpel_filters[my];

    int shift = 14 + 1 - BIT_DEPTH;
#if BIT_DEPTH < 14
    int offset = 1 << (shift - 1);
#else
    int offset = 0;
#endif

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((QPEL_FILTER(src, srcstride) >> (BIT_DEPTH - 8)) + src2[x] + offset) >> shift);
        src  += srcstride;
        dst  += dststride;
        src2 += MAX_PB_SIZE;
    }
}

static void FUNC(put_hevc_qpel_bi_hv)(uint8_t *_dst, ptrdiff_t _dststride,
                                      const uint8_t *_src, ptrdiff_t _srcstride, const int16_t *src2,
                                      int height, intptr_t mx, intptr_t my, int width)
{
    int x, y;
    const int8_t *filter;
    const pixel *src = (const pixel*)_src;
    ptrdiff_t srcstride = _srcstride / sizeof(pixel);
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dststride = _dststride / sizeof(pixel);
    int16_t tmp_array[(MAX_PB_SIZE + QPEL_EXTRA) * MAX_PB_SIZE];
    int16_t *tmp = tmp_array;
    int shift = 14 + 1 - BIT_DEPTH;
#if BIT_DEPTH < 14
    int offset = 1 << (shift - 1);
#else
    int offset = 0;
#endif

    src   -= QPEL_EXTRA_BEFORE * srcstride;
    filter = ff_hevc_qpel_filters[mx];
    for (y = 0; y < height + QPEL_EXTRA; y++) {
        for (x = 0; x < width; x++)
            tmp[x] = QPEL_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += srcstride;
        tmp += MAX_PB_SIZE;
    }

    tmp    = tmp_array + QPEL_EXTRA_BEFORE * MAX_PB_SIZE;
    filter = ff_hevc_qpel_filters[my];

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((QPEL_FILTER(tmp, MAX_PB_SIZE) >> 6) + src2[x] + offset) >> shift);
        tmp  += MAX_PB_SIZE;
        dst  += dststride;
        src2 += MAX_PB_SIZE;
    }
}

static void FUNC(put_hevc_qpel_bi_w_h)(uint8_t *_dst, ptrdiff_t _dststride,
                                       const uint8_t *_src, ptrdiff_t _srcstride, const int16_t *src2,
                                       int height, int denom, int wx0, int wx1,
                                       int ox0, int ox1, intptr_t mx, intptr_t my, int width)
{
    int x, y;
    const pixel  *src       = (const pixel*)_src;
    ptrdiff_t     srcstride = _srcstride / sizeof(pixel);
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dststride = _dststride / sizeof(pixel);

    const int8_t *filter    = ff_hevc_qpel_filters[mx];

    int shift = 14  + 1 - BIT_DEPTH;
    int log2Wd = denom + shift - 1;

    ox0     = ox0 * (1 << (BIT_DEPTH - 8));
    ox1     = ox1 * (1 << (BIT_DEPTH - 8));
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((QPEL_FILTER(src, 1) >> (BIT_DEPTH - 8)) * wx1 + src2[x] * wx0 +
                                    ((ox0 + ox1 + 1) * (1 << log2Wd))) >> (log2Wd + 1));
        src  += srcstride;
        dst  += dststride;
        src2 += MAX_PB_SIZE;
    }
}

static void FUNC(put_hevc_qpel_bi_w_v)(uint8_t *_dst, ptrdiff_t _dststride,
                                       const uint8_t *_src, ptrdiff_t _srcstride, const int16_t *src2,
                                       int height, int denom, int wx0, int wx1,
                                       int ox0, int ox1, intptr_t mx, intptr_t my, int width)
{
    int x, y;
    const pixel  *src       = (const pixel*)_src;
    ptrdiff_t     srcstride = _srcstride / sizeof(pixel);
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dststride = _dststride / sizeof(pixel);

    const int8_t *filter    = ff_hevc_qpel_filters[my];

    int shift = 14 + 1 - BIT_DEPTH;
    int log2Wd = denom + shift - 1;

    ox0     = ox0 * (1 << (BIT_DEPTH - 8));
    ox1     = ox1 * (1 << (BIT_DEPTH - 8));
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((QPEL_FILTER(src, srcstride) >> (BIT_DEPTH - 8)) * wx1 + src2[x] * wx0 +
                                    ((ox0 + ox1 + 1) * (1 << log2Wd))) >> (log2Wd + 1));
        src  += srcstride;
        dst  += dststride;
        src2 += MAX_PB_SIZE;
    }
}

static void FUNC(put_hevc_qpel_bi_w_hv)(uint8_t *_dst, ptrdiff_t _dststride,
                                        const uint8_t *_src, ptrdiff_t _srcstride, const int16_t *src2,
                                        int height, int denom, int wx0, int wx1,
                                        int ox0, int ox1, intptr_t mx, intptr_t my, int width)
{
    int x, y;
    const int8_t *filter;
    const pixel *src = (const pixel*)_src;
    ptrdiff_t srcstride = _srcstride / sizeof(pixel);
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dststride = _dststride / sizeof(pixel);
    int16_t tmp_array[(MAX_PB_SIZE + QPEL_EXTRA) * MAX_PB_SIZE];
    int16_t *tmp = tmp_array;
    int shift = 14 + 1 - BIT_DEPTH;
    int log2Wd = denom + shift - 1;

    src   -= QPEL_EXTRA_BEFORE * srcstride;
    filter = ff_hevc_qpel_filters[mx];
    for (y = 0; y < height + QPEL_EXTRA; y++) {
        for (x = 0; x < width; x++)
            tmp[x] = QPEL_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += srcstride;
        tmp += MAX_PB_SIZE;
    }

    tmp    = tmp_array + QPEL_EXTRA_BEFORE * MAX_PB_SIZE;
    filter = ff_hevc_qpel_filters[my];

    ox0     = ox0 * (1 << (BIT_DEPTH - 8));
    ox1     = ox1 * (1 << (BIT_DEPTH - 8));
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((QPEL_FILTER(tmp, MAX_PB_SIZE) >> 6) * wx1 + src2[x] * wx0 +
                                    ((ox0 + ox1 + 1) * (1 << log2Wd))) >> (log2Wd + 1));
        tmp  += MAX_PB_SIZE;
        dst  += dststride;
        src2 += MAX_PB_SIZE;
    }
}

////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////
#define EPEL_FILTER(src, stride)                                               \
    (filter[0] * src[x - stride] +                                             \
     filter[1] * src[x]          +                                             \
     filter[2] * src[x + stride] +                                             \
     filter[3] * src[x + 2 * stride])

static void FUNC(put_hevc_epel_bi_h)(uint8_t *_dst, ptrdiff_t _dststride,
                                     const uint8_t *_src, ptrdiff_t _srcstride, const int16_t *src2,
                                     int height, intptr_t mx, intptr_t my, int width)
{
    int x, y;
    const pixel *src = (const pixel *)_src;
    ptrdiff_t srcstride  = _srcstride / sizeof(pixel);
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dststride = _dststride / sizeof(pixel);
    const int8_t *filter = ff_hevc_epel_filters[mx];
    int shift = 14 + 1 - BIT_DEPTH;
#if BIT_DEPTH < 14
    int offset = 1 << (shift - 1);
#else
    int offset = 0;
#endif

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            dst[x] = av_clip_pixel(((EPEL_FILTER(src, 1) >> (BIT_DEPTH - 8)) + src2[x] + offset) >> shift);
        }
        dst  += dststride;
        src  += srcstride;
        src2 += MAX_PB_SIZE;
    }
}

static void FUNC(put_hevc_epel_bi_v)(uint8_t *_dst, ptrdiff_t _dststride,
                                     const uint8_t *_src, ptrdiff_t _srcstride, const int16_t *src2,
                                     int height, intptr_t mx, intptr_t my, int width)
{
    int x, y;
    const pixel *src = (const pixel *)_src;
    ptrdiff_t srcstride  = _srcstride / sizeof(pixel);
    const int8_t *filter = ff_hevc_epel_filters[my];
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dststride = _dststride / sizeof(pixel);
    int shift = 14 + 1 - BIT_DEPTH;
#if BIT_DEPTH < 14
    int offset = 1 << (shift - 1);
#else
    int offset = 0;
#endif

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((EPEL_FILTER(src, srcstride) >> (BIT_DEPTH - 8)) + src2[x] + offset) >> shift);
        dst  += dststride;
        src  += srcstride;
        src2 += MAX_PB_SIZE;
    }
}

static void FUNC(put_hevc_epel_bi_hv)(uint8_t *_dst, ptrdiff_t _dststride,
                                      const uint8_t *_src, ptrdiff_t _srcstride, const int16_t *src2,
                                      int height, intptr_t mx, intptr_t my, int width)
{
    int x, y;
    const pixel *src = (const pixel *)_src;
    ptrdiff_t srcstride = _srcstride / sizeof(pixel);
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dststride = _dststride / sizeof(pixel);
    const int8_t *filter = ff_hevc_epel_filters[mx];
    int16_t tmp_array[(MAX_PB_SIZE + EPEL_EXTRA) * MAX_PB_SIZE];
    int16_t *tmp = tmp_array;
    int shift = 14 + 1 - BIT_DEPTH;
#if BIT_DEPTH < 14
    int offset = 1 << (shift - 1);
#else
    int offset = 0;
#endif

    src -= EPEL_EXTRA_BEFORE * srcstride;

    for (y = 0; y < height + EPEL_EXTRA; y++) {
        for (x = 0; x < width; x++)
            tmp[x] = EPEL_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += srcstride;
        tmp += MAX_PB_SIZE;
    }

    tmp      = tmp_array + EPEL_EXTRA_BEFORE * MAX_PB_SIZE;
    filter = ff_hevc_epel_filters[my];

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((EPEL_FILTER(tmp, MAX_PB_SIZE) >> 6) + src2[x] + offset) >> shift);
        tmp  += MAX_PB_SIZE;
        dst  += dststride;
        src2 += MAX_PB_SIZE;
    }
}

static void FUNC(put_hevc_epel_bi_w_h)(uint8_t *_dst, ptrdiff_t _dststride,
                                       const uint8_t *_src, ptrdiff_t _srcstride, const int16_t *src2,
                                       int height, int denom, int wx0, int wx1,
                                       int ox0, int ox1, intptr_t mx, intptr_t my, int width)
{
    int x, y;
    const pixel *src = (const pixel *)_src;
    ptrdiff_t srcstride  = _srcstride / sizeof(pixel);
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dststride = _dststride / sizeof(pixel);
    const int8_t *filter = ff_hevc_epel_filters[mx];
    int shift = 14 + 1 - BIT_DEPTH;
    int log2Wd = denom + shift - 1;

    ox0     = ox0 * (1 << (BIT_DEPTH - 8));
    ox1     = ox1 * (1 << (BIT_DEPTH - 8));
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((EPEL_FILTER(src, 1) >> (BIT_DEPTH - 8)) * wx1 + src2[x] * wx0 +
                                    ((ox0 + ox1 + 1) * (1 << log2Wd))) >> (log2Wd + 1));
        src  += srcstride;
        dst  += dststride;
        src2 += MAX_PB_SIZE;
    }
}

static void FUNC(put_hevc_epel_bi_w_v)(uint8_t *_dst, ptrdiff_t _dststride,
                                       const uint8_t *_src, ptrdiff_t _srcstride, const int16_t *src2,
                                       int height, int denom, int wx0, int wx1,
                                       int ox0, int ox1, intptr_t mx, intptr_t my, int width)
{
    int x, y;
    const pixel *src = (const pixel *)_src;
    ptrdiff_t srcstride  = _srcstride / sizeof(pixel);
    const int8_t *filter = ff_hevc_epel_filters[my];
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dststride = _dststride / sizeof(pixel);
    int shift = 14 + 1 - BIT_DEPTH;
    int log2Wd = denom + shift - 1;

    ox0     = ox0 * (1 << (BIT_DEPTH - 8));
    ox1     = ox1 * (1 << (BIT_DEPTH - 8));
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((EPEL_FILTER(src, srcstride) >> (BIT_DEPTH - 8)) * wx1 + src2[x] * wx0 +
                                    ((ox0 + ox1 + 1) * (1 << log2Wd))) >> (log2Wd + 1));
        src  += srcstride;
        dst  += dststride;
        src2 += MAX_PB_SIZE;
    }
}

static void FUNC(put_hevc_epel_bi_w_hv)(uint8_t *_dst, ptrdiff_t _dststride,
                                        const uint8_t *_src, ptrdiff_t _srcstride, const int16_t *src2,
                                        int height, int denom, int wx0, int wx1,
                                        int ox0, int ox1, intptr_t mx, intptr_t my, int width)
{
    int x, y;
    const pixel *src = (const pixel *)_src;
    ptrdiff_t srcstride = _srcstride / sizeof(pixel);
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dststride = _dststride / sizeof(pixel);
    const int8_t *filter = ff_hevc_epel_filters[mx];
    int16_t tmp_array[(MAX_PB_SIZE + EPEL_EXTRA) * MAX_PB_SIZE];
    int16_t *tmp = tmp_array;
    int shift = 14 + 1 - BIT_DEPTH;
    int log2Wd = denom + shift - 1;

    src -= EPEL_EXTRA_BEFORE * srcstride;

    for (y = 0; y < height + EPEL_EXTRA; y++) {
        for (x = 0; x < width; x++)
            tmp[x] = EPEL_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += srcstride;
        tmp += MAX_PB_SIZE;
    }

    tmp      = tmp_array + EPEL_EXTRA_BEFORE * MAX_PB_SIZE;
    filter = ff_hevc_epel_filters[my];

    ox0     = ox0 * (1 << (BIT_DEPTH - 8));
    ox1     = ox1 * (1 << (BIT_DEPTH - 8));
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((EPEL_FILTER(tmp, MAX_PB_SIZE) >> 6) * wx1 + src2[x] * wx0 +
                                    ((ox0 + ox1 + 1) * (1 << log2Wd))) >> (log2Wd + 1));
        tmp  += MAX_PB_SIZE;
        dst  += dststride;
        src2 += MAX_PB_SIZE;
    }
}

// line zero
#define P3 pix[-4 * xstride]
#define P2 pix[-3 * xstride]
#define P1 pix[-2 * xstride]
#define P0 pix[-1 * xstride]
#define Q0 pix[0 * xstride]
#define Q1 pix[1 * xstride]
#define Q2 pix[2 * xstride]
#define Q3 pix[3 * xstride]

// line three. used only for deblocking decision
#define TP3 pix[-4 * xstride + 3 * ystride]
#define TP2 pix[-3 * xstride + 3 * ystride]
#define TP1 pix[-2 * xstride + 3 * ystride]
#define TP0 pix[-1 * xstride + 3 * ystride]
#define TQ0 pix[0  * xstride + 3 * ystride]
#define TQ1 pix[1  * xstride + 3 * ystride]
#define TQ2 pix[2  * xstride + 3 * ystride]
#define TQ3 pix[3  * xstride + 3 * ystride]

#include "h26x/h2656_deblock_template.c"

static void FUNC(hevc_loop_filter_luma)(uint8_t *_pix,
                                        ptrdiff_t _xstride, ptrdiff_t _ystride,
                                        int beta, const int *_tc,
                                        const uint8_t *_no_p, const uint8_t *_no_q)
{
    ptrdiff_t xstride = _xstride / sizeof(pixel);
    ptrdiff_t ystride = _ystride / sizeof(pixel);

    beta <<= BIT_DEPTH - 8;

    for (int j = 0; j < 2; j++) {
        pixel* pix     = (pixel*)_pix + j * 4 * ystride;
        const int dp0  = abs(P2  - 2 * P1  + P0);
        const int dq0  = abs(Q2  - 2 * Q1  + Q0);
        const int dp3  = abs(TP2 - 2 * TP1 + TP0);
        const int dq3  = abs(TQ2 - 2 * TQ1 + TQ0);
        const int d0   = dp0 + dq0;
        const int d3   = dp3 + dq3;
        const int tc   = _tc[j]   << (BIT_DEPTH - 8);
        const int no_p = _no_p[j];
        const int no_q = _no_q[j];

        if (d0 + d3 < beta) {
            const int beta_3 = beta >> 3;
            const int beta_2 = beta >> 2;
            const int tc25   = ((tc * 5 + 1) >> 1);

            if (abs(P3  -  P0) + abs(Q3  -  Q0) < beta_3 && abs(P0  -  Q0) < tc25 &&
                abs(TP3 - TP0) + abs(TQ3 - TQ0) < beta_3 && abs(TP0 - TQ0) < tc25 &&
                                      (d0 << 1) < beta_2 &&      (d3 << 1) < beta_2) {
                const int tc2 = tc << 1;
                FUNC(loop_filter_luma_strong)(pix, xstride, ystride, tc2, tc2, tc2, no_p, no_q);
            } else {
                int nd_p = 1;
                int nd_q = 1;
                if (dp0 + dp3 < ((beta + (beta >> 1)) >> 3))
                    nd_p = 2;
                if (dq0 + dq3 < ((beta + (beta >> 1)) >> 3))
                    nd_q = 2;
                FUNC(loop_filter_luma_weak)(pix, xstride, ystride, tc, beta, no_p, no_q, nd_p, nd_q);
            }
        }
    }
}

static void FUNC(hevc_loop_filter_chroma)(uint8_t *_pix, ptrdiff_t _xstride,
                                          ptrdiff_t _ystride, const int *_tc,
                                          const uint8_t *_no_p, const uint8_t *_no_q)
{
    int no_p, no_q;
    ptrdiff_t xstride = _xstride / sizeof(pixel);
    ptrdiff_t ystride = _ystride / sizeof(pixel);
    const int size    = 4;

    for (int j = 0; j < 2; j++) {
        pixel *pix   = (pixel *)_pix + j * size * ystride;
        const int tc = _tc[j] << (BIT_DEPTH - 8);
        if (tc > 0) {
            no_p = _no_p[j];
            no_q = _no_q[j];

            FUNC(loop_filter_chroma_weak)(pix, xstride, ystride, size, tc, no_p, no_q);
        }
    }
}

static void FUNC(hevc_h_loop_filter_chroma)(uint8_t *pix, ptrdiff_t stride,
                                            const int32_t *tc, const uint8_t *no_p,
                                            const uint8_t *no_q)
{
    FUNC(hevc_loop_filter_chroma)(pix, stride, sizeof(pixel), tc, no_p, no_q);
}

static void FUNC(hevc_v_loop_filter_chroma)(uint8_t *pix, ptrdiff_t stride,
                                            const int32_t *tc, const uint8_t *no_p,
                                            const uint8_t *no_q)
{
    FUNC(hevc_loop_filter_chroma)(pix, sizeof(pixel), stride, tc, no_p, no_q);
}

static void FUNC(hevc_h_loop_filter_luma)(uint8_t *pix, ptrdiff_t stride,
                                          int beta, const int32_t *tc, const uint8_t *no_p,
                                          const uint8_t *no_q)
{
    FUNC(hevc_loop_filter_luma)(pix, stride, sizeof(pixel),
                                beta, tc, no_p, no_q);
}

static void FUNC(hevc_v_loop_filter_luma)(uint8_t *pix, ptrdiff_t stride,
                                          int beta, const int32_t *tc, const uint8_t *no_p,
                                          const uint8_t *no_q)
{
    FUNC(hevc_loop_filter_luma)(pix, sizeof(pixel), stride,
                                beta, tc, no_p, no_q);
}

#undef P3
#undef P2
#undef P1
#undef P0
#undef Q0
#undef Q1
#undef Q2
#undef Q3

#undef TP3
#undef TP2
#undef TP1
#undef TP0
#undef TQ0
#undef TQ1
#undef TQ2
#undef TQ3
