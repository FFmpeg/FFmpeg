/*
 * HEVC video decoder
 *
 * Copyright (C) 2012 - 2013 Guillaume Martres
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "get_bits.h"
#include "hevc.h"

#include "bit_depth_template.c"

static void FUNC(put_pcm)(uint8_t *_dst, ptrdiff_t stride, int size,
                          GetBitContext *gb, int pcm_bit_depth)
{
    int x, y;
    pixel *dst = (pixel *)_dst;

    stride /= sizeof(pixel);

    for (y = 0; y < size; y++) {
        for (x = 0; x < size; x++)
            dst[x] = get_bits(gb, pcm_bit_depth) << (BIT_DEPTH - pcm_bit_depth);
        dst += stride;
    }
}

static av_always_inline void FUNC(add_residual)(uint8_t *_dst, int16_t *res,
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

static void FUNC(add_residual4x4)(uint8_t *_dst, int16_t *res,
                                  ptrdiff_t stride)
{
    FUNC(add_residual)(_dst, res, stride, 4);
}

static void FUNC(add_residual8x8)(uint8_t *_dst, int16_t *res,
                                  ptrdiff_t stride)
{
    FUNC(add_residual)(_dst, res, stride, 8);
}

static void FUNC(add_residual16x16)(uint8_t *_dst, int16_t *res,
                                    ptrdiff_t stride)
{
    FUNC(add_residual)(_dst, res, stride, 16);
}

static void FUNC(add_residual32x32)(uint8_t *_dst, int16_t *res,
                                    ptrdiff_t stride)
{
    FUNC(add_residual)(_dst, res, stride, 32);
}

static void FUNC(dequant)(int16_t *coeffs)
{
    int shift  = 13 - BIT_DEPTH;
#if BIT_DEPTH <= 13
    int offset = 1 << (shift - 1);
#else
    int offset = 0;
#endif
    int x, y;

    for (y = 0; y < 4 * 4; y += 4) {
        for (x = 0; x < 4; x++)
            coeffs[y + x] = (coeffs[y + x] + offset) >> shift;
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

#define TR_4(dst, src, dstep, sstep, assign, end)                       \
    do {                                                                \
        const int e0 = transform[8 * 0][0] * src[0 * sstep] +           \
                       transform[8 * 2][0] * src[2 * sstep];            \
        const int e1 = transform[8 * 0][1] * src[0 * sstep] +           \
                       transform[8 * 2][1] * src[2 * sstep];            \
        const int o0 = transform[8 * 1][0] * src[1 * sstep] +           \
                       transform[8 * 3][0] * src[3 * sstep];            \
        const int o1 = transform[8 * 1][1] * src[1 * sstep] +           \
                       transform[8 * 3][1] * src[3 * sstep];            \
                                                                        \
        assign(dst[0 * dstep], e0 + o0);                                \
        assign(dst[1 * dstep], e1 + o1);                                \
        assign(dst[2 * dstep], e1 - o1);                                \
        assign(dst[3 * dstep], e0 - o0);                                \
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
#undef ADD_AND_SCALE

static void FUNC(sao_band_filter)(uint8_t *_dst, uint8_t *_src,
                                  ptrdiff_t stride, SAOParams *sao,
                                  int *borders, int width, int height,
                                  int c_idx, int class)
{
    pixel *dst = (pixel *)_dst;
    pixel *src = (pixel *)_src;
    int offset_table[32] = { 0 };
    int k, y, x;
    int chroma = !!c_idx;
    int shift  = BIT_DEPTH - 5;
    int *sao_offset_val = sao->offset_val[c_idx];
    int sao_left_class  = sao->band_position[c_idx];
    int init_y = 0, init_x = 0;

    stride /= sizeof(pixel);

    switch (class) {
    case 0:
        if (!borders[2])
            width -= (8 >> chroma) + 2;
        if (!borders[3])
            height -= (4 >> chroma) + 2;
        break;
    case 1:
        init_y = -(4 >> chroma) - 2;
        if (!borders[2])
            width -= (8 >> chroma) + 2;
        height = (4 >> chroma) + 2;
        break;
    case 2:
        init_x = -(8 >> chroma) - 2;
        width  =  (8 >> chroma) + 2;
        if (!borders[3])
            height -= (4 >> chroma) + 2;
        break;
    case 3:
        init_y = -(4 >> chroma) - 2;
        init_x = -(8 >> chroma) - 2;
        width  =  (8 >> chroma) + 2;
        height =  (4 >> chroma) + 2;
        break;
    }

    dst = dst + (init_y * stride + init_x);
    src = src + (init_y * stride + init_x);
    for (k = 0; k < 4; k++)
        offset_table[(k + sao_left_class) & 31] = sao_offset_val[k + 1];
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel(src[x] + offset_table[src[x] >> shift]);
        dst += stride;
        src += stride;
    }
}

static void FUNC(sao_band_filter_0)(uint8_t *dst, uint8_t *src,
                                    ptrdiff_t stride, SAOParams *sao,
                                    int *borders, int width, int height,
                                    int c_idx)
{
    FUNC(sao_band_filter)(dst, src, stride, sao, borders,
                          width, height, c_idx, 0);
}

static void FUNC(sao_band_filter_1)(uint8_t *dst, uint8_t *src,
                                    ptrdiff_t stride, SAOParams *sao,
                                    int *borders, int width, int height,
                                    int c_idx)
{
    FUNC(sao_band_filter)(dst, src, stride, sao, borders,
                          width, height, c_idx, 1);
}

static void FUNC(sao_band_filter_2)(uint8_t *dst, uint8_t *src,
                                    ptrdiff_t stride, SAOParams *sao,
                                    int *borders, int width, int height,
                                    int c_idx)
{
    FUNC(sao_band_filter)(dst, src, stride, sao, borders,
                          width, height, c_idx, 2);
}

static void FUNC(sao_band_filter_3)(uint8_t *_dst, uint8_t *_src,
                                    ptrdiff_t stride, SAOParams *sao,
                                    int *borders, int width, int height,
                                    int c_idx)
{
    FUNC(sao_band_filter)(_dst, _src, stride, sao, borders,
                          width, height, c_idx, 3);
}

static void FUNC(sao_edge_filter_0)(uint8_t *_dst, uint8_t *_src,
                                    ptrdiff_t stride, SAOParams *sao,
                                    int *borders, int _width, int _height,
                                    int c_idx, uint8_t vert_edge,
                                    uint8_t horiz_edge, uint8_t diag_edge)
{
    int x, y;
    pixel *dst = (pixel *)_dst;
    pixel *src = (pixel *)_src;
    int chroma = !!c_idx;
    int *sao_offset_val = sao->offset_val[c_idx];
    int sao_eo_class    = sao->eo_class[c_idx];
    int init_x = 0, init_y = 0, width = _width, height = _height;

    static const int8_t pos[4][2][2] = {
        { { -1,  0 }, {  1, 0 } }, // horizontal
        { {  0, -1 }, {  0, 1 } }, // vertical
        { { -1, -1 }, {  1, 1 } }, // 45 degree
        { {  1, -1 }, { -1, 1 } }, // 135 degree
    };
    static const uint8_t edge_idx[] = { 1, 2, 0, 3, 4 };

#define CMP(a, b) ((a) > (b) ? 1 : ((a) == (b) ? 0 : -1))

    stride /= sizeof(pixel);

    if (!borders[2])
        width -= (8 >> chroma) + 2;
    if (!borders[3])
        height -= (4 >> chroma) + 2;

    dst = dst + (init_y * stride + init_x);
    src = src + (init_y * stride + init_x);
    init_y = init_x = 0;
    if (sao_eo_class != SAO_EO_VERT) {
        if (borders[0]) {
            int offset_val = sao_offset_val[0];
            int y_stride   = 0;
            for (y = 0; y < height; y++) {
                dst[y_stride] = av_clip_pixel(src[y_stride] + offset_val);
                y_stride     += stride;
            }
            init_x = 1;
        }
        if (borders[2]) {
            int offset_val = sao_offset_val[0];
            int x_stride   = width - 1;
            for (x = 0; x < height; x++) {
                dst[x_stride] = av_clip_pixel(src[x_stride] + offset_val);
                x_stride     += stride;
            }
            width--;
        }
    }
    if (sao_eo_class != SAO_EO_HORIZ) {
        if (borders[1]) {
            int offset_val = sao_offset_val[0];
            for (x = init_x; x < width; x++)
                dst[x] = av_clip_pixel(src[x] + offset_val);
            init_y = 1;
        }
        if (borders[3]) {
            int offset_val = sao_offset_val[0];
            int y_stride   = stride * (height - 1);
            for (x = init_x; x < width; x++)
                dst[x + y_stride] = av_clip_pixel(src[x + y_stride] + offset_val);
            height--;
        }
    }
    {
        int y_stride = init_y * stride;
        int pos_0_0  = pos[sao_eo_class][0][0];
        int pos_0_1  = pos[sao_eo_class][0][1];
        int pos_1_0  = pos[sao_eo_class][1][0];
        int pos_1_1  = pos[sao_eo_class][1][1];

        int y_stride_0_1 = (init_y + pos_0_1) * stride;
        int y_stride_1_1 = (init_y + pos_1_1) * stride;
        for (y = init_y; y < height; y++) {
            for (x = init_x; x < width; x++) {
                int diff0         = CMP(src[x + y_stride], src[x + pos_0_0 + y_stride_0_1]);
                int diff1         = CMP(src[x + y_stride], src[x + pos_1_0 + y_stride_1_1]);
                int offset_val    = edge_idx[2 + diff0 + diff1];
                dst[x + y_stride] = av_clip_pixel(src[x + y_stride] + sao_offset_val[offset_val]);
            }
            y_stride     += stride;
            y_stride_0_1 += stride;
            y_stride_1_1 += stride;
        }
    }

    {
        // Restore pixels that can't be modified
        int save_upper_left = !diag_edge && sao_eo_class == SAO_EO_135D && !borders[0] && !borders[1];
        if (vert_edge && sao_eo_class != SAO_EO_VERT)
            for (y = init_y+save_upper_left; y< height; y++)
                dst[y*stride] = src[y*stride];
        if(horiz_edge && sao_eo_class != SAO_EO_HORIZ)
            for(x = init_x+save_upper_left; x<width; x++)
                dst[x] = src[x];
        if(diag_edge && sao_eo_class == SAO_EO_135D)
            dst[0] = src[0];
    }

#undef CMP
}

static void FUNC(sao_edge_filter_1)(uint8_t *_dst, uint8_t *_src,
                                    ptrdiff_t stride, SAOParams *sao,
                                    int *borders, int _width, int _height,
                                    int c_idx, uint8_t vert_edge,
                                    uint8_t horiz_edge, uint8_t diag_edge)
{
    int x, y;
    pixel *dst = (pixel *)_dst;
    pixel *src = (pixel *)_src;
    int chroma = !!c_idx;
    int *sao_offset_val = sao->offset_val[c_idx];
    int sao_eo_class    = sao->eo_class[c_idx];
    int init_x = 0, init_y = 0, width = _width, height = _height;

    static const int8_t pos[4][2][2] = {
        { { -1, 0  }, { 1,  0 } }, // horizontal
        { { 0,  -1 }, { 0,  1 } }, // vertical
        { { -1, -1 }, { 1,  1 } }, // 45 degree
        { { 1,  -1 }, { -1, 1 } }, // 135 degree
    };
    static const uint8_t edge_idx[] = { 1, 2, 0, 3, 4 };

#define CMP(a, b) ((a) > (b) ? 1 : ((a) == (b) ? 0 : -1))

    stride /= sizeof(pixel);

    init_y = -(4 >> chroma) - 2;
    if (!borders[2])
        width -= (8 >> chroma) + 2;
    height = (4 >> chroma) + 2;

    dst = dst + (init_y * stride + init_x);
    src = src + (init_y * stride + init_x);
    init_y = init_x = 0;
    if (sao_eo_class != SAO_EO_VERT) {
        if (borders[0]) {
            int offset_val = sao_offset_val[0];
            int y_stride   = 0;
            for (y = 0; y < height; y++) {
                dst[y_stride] = av_clip_pixel(src[y_stride] + offset_val);
                y_stride     += stride;
            }
            init_x = 1;
        }
        if (borders[2]) {
            int offset_val = sao_offset_val[0];
            int x_stride   = width - 1;
            for (x = 0; x < height; x++) {
                dst[x_stride] = av_clip_pixel(src[x_stride] + offset_val);
                x_stride     += stride;
            }
            width--;
        }
    }
    {
        int y_stride = init_y * stride;
        int pos_0_0  = pos[sao_eo_class][0][0];
        int pos_0_1  = pos[sao_eo_class][0][1];
        int pos_1_0  = pos[sao_eo_class][1][0];
        int pos_1_1  = pos[sao_eo_class][1][1];

        int y_stride_0_1 = (init_y + pos_0_1) * stride;
        int y_stride_1_1 = (init_y + pos_1_1) * stride;
        for (y = init_y; y < height; y++) {
            for (x = init_x; x < width; x++) {
                int diff0         = CMP(src[x + y_stride], src[x + pos_0_0 + y_stride_0_1]);
                int diff1         = CMP(src[x + y_stride], src[x + pos_1_0 + y_stride_1_1]);
                int offset_val    = edge_idx[2 + diff0 + diff1];
                dst[x + y_stride] = av_clip_pixel(src[x + y_stride] + sao_offset_val[offset_val]);
            }
            y_stride     += stride;
            y_stride_0_1 += stride;
            y_stride_1_1 += stride;
        }
    }

    {
        // Restore pixels that can't be modified
        int save_lower_left = !diag_edge && sao_eo_class == SAO_EO_45D && !borders[0];
        if(vert_edge && sao_eo_class != SAO_EO_VERT)
            for(y = init_y; y< height-save_lower_left; y++)
                dst[y*stride] = src[y*stride];
        if(horiz_edge && sao_eo_class != SAO_EO_HORIZ)
            for(x = init_x+save_lower_left; x<width; x++)
                dst[(height-1)*stride+x] = src[(height-1)*stride+x];
        if(diag_edge && sao_eo_class == SAO_EO_45D)
            dst[stride*(height-1)] = src[stride*(height-1)];
    }

#undef CMP
}

static void FUNC(sao_edge_filter_2)(uint8_t *_dst, uint8_t *_src,
                                    ptrdiff_t stride, SAOParams *sao,
                                    int *borders, int _width, int _height,
                                    int c_idx, uint8_t vert_edge,
                                    uint8_t horiz_edge, uint8_t diag_edge)
{
    int x, y;
    pixel *dst = (pixel *)_dst;
    pixel *src = (pixel *)_src;
    int chroma = !!c_idx;
    int *sao_offset_val = sao->offset_val[c_idx];
    int sao_eo_class    = sao->eo_class[c_idx];
    int init_x = 0, init_y = 0, width = _width, height = _height;

    static const int8_t pos[4][2][2] = {
        { { -1,  0 }, {  1, 0 } }, // horizontal
        { {  0, -1 }, {  0, 1 } }, // vertical
        { { -1, -1 }, {  1, 1 } }, // 45 degree
        { {  1, -1 }, { -1, 1 } }, // 135 degree
    };
    static const uint8_t edge_idx[] = { 1, 2, 0, 3, 4 };

#define CMP(a, b) ((a) > (b) ? 1 : ((a) == (b) ? 0 : -1))

    stride /= sizeof(pixel);

    init_x = -(8 >> chroma) - 2;
    width  =  (8 >> chroma) + 2;
    if (!borders[3])
        height -= (4 >> chroma) + 2;

    dst = dst + (init_y * stride + init_x);
    src = src + (init_y * stride + init_x);
    init_y = init_x = 0;
    if (sao_eo_class != SAO_EO_HORIZ) {
        if (borders[1]) {
            int offset_val = sao_offset_val[0];
            for (x = init_x; x < width; x++)
                dst[x] = av_clip_pixel(src[x] + offset_val);
            init_y = 1;
        }
        if (borders[3]) {
            int offset_val = sao_offset_val[0];
            int y_stride   = stride * (height - 1);
            for (x = init_x; x < width; x++)
                dst[x + y_stride] = av_clip_pixel(src[x + y_stride] + offset_val);
            height--;
        }
    }
    {
        int y_stride = init_y * stride;
        int pos_0_0  = pos[sao_eo_class][0][0];
        int pos_0_1  = pos[sao_eo_class][0][1];
        int pos_1_0  = pos[sao_eo_class][1][0];
        int pos_1_1  = pos[sao_eo_class][1][1];

        int y_stride_0_1 = (init_y + pos_0_1) * stride;
        int y_stride_1_1 = (init_y + pos_1_1) * stride;
        for (y = init_y; y < height; y++) {
            for (x = init_x; x < width; x++) {
                int diff0         = CMP(src[x + y_stride], src[x + pos_0_0 + y_stride_0_1]);
                int diff1         = CMP(src[x + y_stride], src[x + pos_1_0 + y_stride_1_1]);
                int offset_val    = edge_idx[2 + diff0 + diff1];
                dst[x + y_stride] = av_clip_pixel(src[x + y_stride] + sao_offset_val[offset_val]);
            }
            y_stride     += stride;
            y_stride_0_1 += stride;
            y_stride_1_1 += stride;
        }
    }

    {
        // Restore pixels that can't be modified
        int save_upper_right = !diag_edge && sao_eo_class == SAO_EO_45D && !borders[1];
        if(vert_edge && sao_eo_class != SAO_EO_VERT)
            for(y = init_y+save_upper_right; y< height; y++)
                dst[y*stride+width-1] = src[y*stride+width-1];
        if(horiz_edge && sao_eo_class != SAO_EO_HORIZ)
            for(x = init_x; x<width-save_upper_right; x++)
                dst[x] = src[x];
        if(diag_edge && sao_eo_class == SAO_EO_45D)
            dst[width-1] = src[width-1];
    }
#undef CMP
}

static void FUNC(sao_edge_filter_3)(uint8_t *_dst, uint8_t *_src,
                                    ptrdiff_t stride, SAOParams *sao,
                                    int *borders, int _width, int _height,
                                    int c_idx, uint8_t vert_edge,
                                    uint8_t horiz_edge, uint8_t diag_edge)
{
    int x, y;
    pixel *dst = (pixel *)_dst;
    pixel *src = (pixel *)_src;
    int chroma = !!c_idx;
    int *sao_offset_val = sao->offset_val[c_idx];
    int sao_eo_class    = sao->eo_class[c_idx];
    int init_x = 0, init_y = 0, width = _width, height = _height;

    static const int8_t pos[4][2][2] = {
        { { -1,  0 }, {  1, 0 } }, // horizontal
        { {  0, -1 }, {  0, 1 } }, // vertical
        { { -1, -1 }, {  1, 1 } }, // 45 degree
        { {  1, -1 }, { -1, 1 } }, // 135 degree
    };
    static const uint8_t edge_idx[] = { 1, 2, 0, 3, 4 };

#define CMP(a, b) ((a) > (b) ? 1 : ((a) == (b) ? 0 : -1))

    stride /= sizeof(pixel);

    init_y = -(4 >> chroma) - 2;
    init_x = -(8 >> chroma) - 2;
    width  =  (8 >> chroma) + 2;
    height =  (4 >> chroma) + 2;


    dst    = dst + (init_y * stride + init_x);
    src    = src + (init_y * stride + init_x);
    init_y = init_x = 0;

    {
        int y_stride = init_y * stride;
        int pos_0_0  = pos[sao_eo_class][0][0];
        int pos_0_1  = pos[sao_eo_class][0][1];
        int pos_1_0  = pos[sao_eo_class][1][0];
        int pos_1_1  = pos[sao_eo_class][1][1];

        int y_stride_0_1 = (init_y + pos_0_1) * stride;
        int y_stride_1_1 = (init_y + pos_1_1) * stride;

        for (y = init_y; y < height; y++) {
            for (x = init_x; x < width; x++) {
                int diff0         = CMP(src[x + y_stride], src[x + pos_0_0 + y_stride_0_1]);
                int diff1         = CMP(src[x + y_stride], src[x + pos_1_0 + y_stride_1_1]);
                int offset_val    = edge_idx[2 + diff0 + diff1];
                dst[x + y_stride] = av_clip_pixel(src[x + y_stride] + sao_offset_val[offset_val]);
            }
            y_stride     += stride;
            y_stride_0_1 += stride;
            y_stride_1_1 += stride;
        }
    }

    {
        // Restore pixels that can't be modified
        int save_lower_right = !diag_edge && sao_eo_class == SAO_EO_135D;
        if(vert_edge && sao_eo_class != SAO_EO_VERT)
            for(y = init_y; y< height-save_lower_right; y++)
                dst[y*stride+width-1] = src[y*stride+width-1];
        if(horiz_edge && sao_eo_class != SAO_EO_HORIZ)
            for(x = init_x; x<width-save_lower_right; x++)
                dst[(height-1)*stride+x] = src[(height-1)*stride+x];
        if(diag_edge && sao_eo_class == SAO_EO_135D)
            dst[stride*(height-1)+width-1] = src[stride*(height-1)+width-1];
    }
#undef CMP
}

#undef SET
#undef SCALE
#undef TR_4
#undef TR_8
#undef TR_16
#undef TR_32

static av_always_inline void
FUNC(put_hevc_qpel_pixels)(int16_t *dst, ptrdiff_t dststride,
                           uint8_t *_src, ptrdiff_t _srcstride,
                           int width, int height, int mx, int my,
                           int16_t* mcbuffer)
{
    int x, y;
    pixel *src          = (pixel *)_src;
    ptrdiff_t srcstride = _srcstride / sizeof(pixel);

    dststride /= sizeof(*dst);
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = src[x] << (14 - BIT_DEPTH);
        src += srcstride;
        dst += dststride;
    }
}

#define QPEL_FILTER_1(src, stride)      \
    (1 * -src[x - 3 * stride] +         \
     4 *  src[x - 2 * stride] -         \
    10 *  src[x -     stride] +         \
    58 *  src[x]              +         \
    17 *  src[x +     stride] -         \
     5 *  src[x + 2 * stride] +         \
     1 *  src[x + 3 * stride])

#define QPEL_FILTER_2(src, stride)      \
    (1  * -src[x - 3 * stride] +        \
     4  *  src[x - 2 * stride] -        \
    11  *  src[x -     stride] +        \
    40  *  src[x]              +        \
    40  *  src[x +     stride] -        \
    11  *  src[x + 2 * stride] +        \
     4  *  src[x + 3 * stride] -        \
     1  *  src[x + 4 * stride])

#define QPEL_FILTER_3(src, stride)      \
    (1  * src[x - 2 * stride] -         \
     5  * src[x -     stride] +         \
    17  * src[x]              +         \
    58  * src[x + stride]     -         \
    10  * src[x + 2 * stride] +         \
     4  * src[x + 3 * stride] -         \
     1  * src[x + 4 * stride])


#define PUT_HEVC_QPEL_H(H)                                                     \
static void FUNC(put_hevc_qpel_h ## H)(int16_t *dst,  ptrdiff_t dststride,     \
                                       uint8_t *_src, ptrdiff_t _srcstride,    \
                                       int width, int height,                  \
                                       int16_t* mcbuffer)                      \
{                                                                              \
    int x, y;                                                                  \
    pixel *src = (pixel*)_src;                                                 \
    ptrdiff_t srcstride = _srcstride / sizeof(pixel);                          \
                                                                               \
    dststride /= sizeof(*dst);                                                 \
    for (y = 0; y < height; y++) {                                             \
        for (x = 0; x < width; x++)                                            \
            dst[x] = QPEL_FILTER_ ## H(src, 1) >> (BIT_DEPTH - 8);             \
        src += srcstride;                                                      \
        dst += dststride;                                                      \
    }                                                                          \
}

#define PUT_HEVC_QPEL_V(V)                                                     \
static void FUNC(put_hevc_qpel_v ## V)(int16_t *dst,  ptrdiff_t dststride,     \
                                       uint8_t *_src, ptrdiff_t _srcstride,    \
                                       int width, int height,                  \
                                       int16_t* mcbuffer)                      \
{                                                                              \
    int x, y;                                                                  \
    pixel *src = (pixel*)_src;                                                 \
    ptrdiff_t srcstride = _srcstride / sizeof(pixel);                          \
                                                                               \
    dststride /= sizeof(*dst);                                                 \
    for (y = 0; y < height; y++)  {                                            \
        for (x = 0; x < width; x++)                                            \
            dst[x] = QPEL_FILTER_ ## V(src, srcstride) >> (BIT_DEPTH - 8);     \
        src += srcstride;                                                      \
        dst += dststride;                                                      \
    }                                                                          \
}

#define PUT_HEVC_QPEL_HV(H, V)                                                 \
static void FUNC(put_hevc_qpel_h ## H ## v ## V)(int16_t *dst,                 \
                                                 ptrdiff_t dststride,          \
                                                 uint8_t *_src,                \
                                                 ptrdiff_t _srcstride,         \
                                                 int width, int height,        \
                                                 int16_t* mcbuffer)            \
{                                                                              \
    int x, y;                                                                  \
    pixel *src = (pixel*)_src;                                                 \
    ptrdiff_t srcstride = _srcstride / sizeof(pixel);                          \
                                                                               \
    int16_t tmp_array[(MAX_PB_SIZE + 7) * MAX_PB_SIZE];                        \
    int16_t *tmp = tmp_array;                                                  \
                                                                               \
    dststride /= sizeof(*dst);                                                 \
    src -= ff_hevc_qpel_extra_before[V] * srcstride;                           \
                                                                               \
    for (y = 0; y < height + ff_hevc_qpel_extra[V]; y++) {                     \
        for (x = 0; x < width; x++)                                            \
            tmp[x] = QPEL_FILTER_ ## H(src, 1) >> (BIT_DEPTH - 8);             \
        src += srcstride;                                                      \
        tmp += MAX_PB_SIZE;                                                    \
    }                                                                          \
                                                                               \
    tmp = tmp_array + ff_hevc_qpel_extra_before[V] * MAX_PB_SIZE;              \
                                                                               \
    for (y = 0; y < height; y++) {                                             \
        for (x = 0; x < width; x++)                                            \
            dst[x] = QPEL_FILTER_ ## V(tmp, MAX_PB_SIZE) >> 6;                 \
        tmp += MAX_PB_SIZE;                                                    \
        dst += dststride;                                                      \
    }                                                                          \
}

PUT_HEVC_QPEL_H(1)
PUT_HEVC_QPEL_H(2)
PUT_HEVC_QPEL_H(3)
PUT_HEVC_QPEL_V(1)
PUT_HEVC_QPEL_V(2)
PUT_HEVC_QPEL_V(3)
PUT_HEVC_QPEL_HV(1, 1)
PUT_HEVC_QPEL_HV(1, 2)
PUT_HEVC_QPEL_HV(1, 3)
PUT_HEVC_QPEL_HV(2, 1)
PUT_HEVC_QPEL_HV(2, 2)
PUT_HEVC_QPEL_HV(2, 3)
PUT_HEVC_QPEL_HV(3, 1)
PUT_HEVC_QPEL_HV(3, 2)
PUT_HEVC_QPEL_HV(3, 3)

#define QPEL(W)                                                                             \
static void FUNC(put_hevc_qpel_pixels_ ## W)(int16_t *dst, ptrdiff_t dststride,             \
                                             uint8_t *src, ptrdiff_t srcstride,             \
                                             int height, int mx, int my,                    \
                                             int16_t *mcbuffer)                             \
{                                                                                           \
    FUNC(put_hevc_qpel_pixels)(dst, dststride, src, srcstride, W, height,                   \
                               mx, my, mcbuffer);                                           \
}                                                                                           \
                                                                                            \
static void FUNC(put_hevc_qpel_h_ ## W)(int16_t *dst, ptrdiff_t dststride,                  \
                                        uint8_t *src, ptrdiff_t srcstride,                  \
                                        int height, int mx, int my,                         \
                                        int16_t *mcbuffer)                                  \
{                                                                                           \
    if (mx == 1)                                                                            \
        FUNC(put_hevc_qpel_h1)(dst, dststride, src, srcstride, W, height, mcbuffer);        \
    else if (mx == 2)                                                                       \
        FUNC(put_hevc_qpel_h2)(dst, dststride, src, srcstride, W, height, mcbuffer);        \
    else                                                                                    \
        FUNC(put_hevc_qpel_h3)(dst, dststride, src, srcstride, W, height, mcbuffer);        \
}                                                                                           \
                                                                                            \
static void FUNC(put_hevc_qpel_v_ ## W)(int16_t *dst, ptrdiff_t dststride,                  \
                                             uint8_t *src, ptrdiff_t srcstride,             \
                                             int height, int mx, int my,                    \
                                             int16_t *mcbuffer)                             \
{                                                                                           \
    if (my == 1)                                                                            \
        FUNC(put_hevc_qpel_v1)(dst, dststride, src, srcstride, W, height, mcbuffer);        \
    else if (my == 2)                                                                       \
        FUNC(put_hevc_qpel_v2)(dst, dststride, src, srcstride, W, height, mcbuffer);        \
    else                                                                                    \
        FUNC(put_hevc_qpel_v3)(dst, dststride, src, srcstride, W, height, mcbuffer);        \
}                                                                                           \
                                                                                            \
static void FUNC(put_hevc_qpel_hv_ ## W)(int16_t *dst, ptrdiff_t dststride,                 \
                                             uint8_t *src, ptrdiff_t srcstride,             \
                                             int height, int mx, int my,                    \
                                             int16_t *mcbuffer)                             \
{                                                                                           \
    if (my == 1) {                                                                          \
        if (mx == 1)                                                                        \
            FUNC(put_hevc_qpel_h1v1)(dst, dststride, src, srcstride, W, height, mcbuffer);  \
        else if (mx == 2)                                                                   \
            FUNC(put_hevc_qpel_h2v1)(dst, dststride, src, srcstride, W, height, mcbuffer);  \
        else                                                                                \
            FUNC(put_hevc_qpel_h3v1)(dst, dststride, src, srcstride, W, height, mcbuffer);  \
    } else if (my == 2) {                                                                   \
        if (mx == 1)                                                                        \
            FUNC(put_hevc_qpel_h1v2)(dst, dststride, src, srcstride, W, height, mcbuffer);  \
        else if (mx == 2)                                                                   \
            FUNC(put_hevc_qpel_h2v2)(dst, dststride, src, srcstride, W, height, mcbuffer);  \
        else                                                                                \
            FUNC(put_hevc_qpel_h3v2)(dst, dststride, src, srcstride, W, height, mcbuffer);  \
    } else {                                                                                \
        if (mx == 1)                                                                        \
            FUNC(put_hevc_qpel_h1v3)(dst, dststride, src, srcstride, W, height, mcbuffer);  \
        else if (mx == 2)                                                                   \
            FUNC(put_hevc_qpel_h2v3)(dst, dststride, src, srcstride, W, height, mcbuffer);  \
        else                                                                                \
            FUNC(put_hevc_qpel_h3v3)(dst, dststride, src, srcstride, W, height, mcbuffer);  \
    }                                                                                       \
}

QPEL(64)
QPEL(48)
QPEL(32)
QPEL(24)
QPEL(16)
QPEL(12)
QPEL(8)
QPEL(4)

static inline void FUNC(put_hevc_epel_pixels)(int16_t *dst, ptrdiff_t dststride,
                                              uint8_t *_src, ptrdiff_t _srcstride,
                                              int width, int height, int mx, int my,
                                              int16_t* mcbuffer)
{
    int x, y;
    pixel *src          = (pixel *)_src;
    ptrdiff_t srcstride = _srcstride / sizeof(pixel);

    dststride /= sizeof(*dst);
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = src[x] << (14 - BIT_DEPTH);
        src += srcstride;
        dst += dststride;
    }
}

#define EPEL_FILTER(src, stride)                \
    (filter_0 * src[x - stride] +               \
     filter_1 * src[x]          +               \
     filter_2 * src[x + stride] +               \
     filter_3 * src[x + 2 * stride])

static inline void FUNC(put_hevc_epel_h)(int16_t *dst, ptrdiff_t dststride,
                                         uint8_t *_src, ptrdiff_t _srcstride,
                                         int width, int height, int mx, int my,
                                         int16_t* mcbuffer)
{
    int x, y;
    pixel *src = (pixel *)_src;
    ptrdiff_t srcstride  = _srcstride / sizeof(pixel);
    const int16_t *filter = ff_hevc_epel_coeffs[mx - 1];
    int8_t filter_0 = filter[0];
    int8_t filter_1 = filter[1];
    int8_t filter_2 = filter[2];
    int8_t filter_3 = filter[3];
    dststride /= sizeof(*dst);
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = EPEL_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += srcstride;
        dst += dststride;
    }
}

static inline void FUNC(put_hevc_epel_v)(int16_t *dst, ptrdiff_t dststride,
                                         uint8_t *_src, ptrdiff_t _srcstride,
                                         int width, int height, int mx, int my,
                                         int16_t* mcbuffer)
{
    int x, y;
    pixel *src = (pixel *)_src;
    ptrdiff_t srcstride = _srcstride / sizeof(pixel);
    const int16_t *filter = ff_hevc_epel_coeffs[my - 1];
    int8_t filter_0 = filter[0];
    int8_t filter_1 = filter[1];
    int8_t filter_2 = filter[2];
    int8_t filter_3 = filter[3];

    dststride /= sizeof(*dst);
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = EPEL_FILTER(src, srcstride) >> (BIT_DEPTH - 8);
        src += srcstride;
        dst += dststride;
    }
}

static inline void FUNC(put_hevc_epel_hv)(int16_t *dst, ptrdiff_t dststride,
                                          uint8_t *_src, ptrdiff_t _srcstride,
                                          int width, int height, int mx, int my,
                                          int16_t* mcbuffer)
{
    int x, y;
    pixel *src = (pixel *)_src;
    ptrdiff_t srcstride = _srcstride / sizeof(pixel);
    const int16_t *filter_h = ff_hevc_epel_coeffs[mx - 1];
    const int16_t *filter_v = ff_hevc_epel_coeffs[my - 1];
    int8_t filter_0 = filter_h[0];
    int8_t filter_1 = filter_h[1];
    int8_t filter_2 = filter_h[2];
    int8_t filter_3 = filter_h[3];
    int16_t tmp_array[(MAX_PB_SIZE + 3) * MAX_PB_SIZE];
    int16_t *tmp = tmp_array;

    dststride /= sizeof(*dst);
    src -= EPEL_EXTRA_BEFORE * srcstride;

    for (y = 0; y < height + EPEL_EXTRA; y++) {
        for (x = 0; x < width; x++)
            tmp[x] = EPEL_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += srcstride;
        tmp += MAX_PB_SIZE;
    }

    tmp      = tmp_array + EPEL_EXTRA_BEFORE * MAX_PB_SIZE;
    filter_0 = filter_v[0];
    filter_1 = filter_v[1];
    filter_2 = filter_v[2];
    filter_3 = filter_v[3];
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = EPEL_FILTER(tmp, MAX_PB_SIZE) >> 6;
        tmp += MAX_PB_SIZE;
        dst += dststride;
    }
}

#define EPEL(W)                                                                 \
static void FUNC(put_hevc_epel_pixels_ ## W)(int16_t *dst, ptrdiff_t dststride, \
                                             uint8_t *src, ptrdiff_t srcstride, \
                                             int height, int mx, int my,        \
                                             int16_t *mcbuffer)                 \
{                                                                               \
    FUNC(put_hevc_epel_pixels)(dst, dststride, src, srcstride,                  \
                               W, height, mx, my, mcbuffer);                    \
}                                                                               \
static void FUNC(put_hevc_epel_h_ ## W)(int16_t *dst, ptrdiff_t dststride,      \
                                        uint8_t *src, ptrdiff_t srcstride,      \
                                        int height, int mx, int my,             \
                                        int16_t *mcbuffer)                      \
{                                                                               \
    FUNC(put_hevc_epel_h)(dst, dststride, src, srcstride,                       \
                          W, height, mx, my, mcbuffer);                         \
}                                                                               \
static void FUNC(put_hevc_epel_v_ ## W)(int16_t *dst, ptrdiff_t dststride,      \
                                        uint8_t *src, ptrdiff_t srcstride,      \
                                        int height, int mx, int my,             \
                                        int16_t *mcbuffer)                      \
{                                                                               \
    FUNC(put_hevc_epel_v)(dst, dststride, src, srcstride,                       \
                          W, height, mx, my, mcbuffer);                         \
}                                                                               \
static void FUNC(put_hevc_epel_hv_ ## W)(int16_t *dst, ptrdiff_t dststride,     \
                                         uint8_t *src, ptrdiff_t srcstride,     \
                                         int height, int mx, int my,            \
                                         int16_t *mcbuffer)                     \
{                                                                               \
    FUNC(put_hevc_epel_hv)(dst, dststride, src, srcstride,                      \
                           W, height, mx, my, mcbuffer);                        \
}

EPEL(32)
EPEL(24)
EPEL(16)
EPEL(12)
EPEL(8)
EPEL(6)
EPEL(4)
EPEL(2)

static av_always_inline void
FUNC(put_unweighted_pred)(uint8_t *_dst, ptrdiff_t _dststride,
                          int16_t *src, ptrdiff_t srcstride,
                          int width, int height)
{
    int x, y;
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dststride = _dststride / sizeof(pixel);

    int shift = 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    int offset = 1 << (shift - 1);
#else
    int offset = 0;
#endif
    srcstride /= sizeof(*src);
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel((src[x] + offset) >> shift);
        dst += dststride;
        src += srcstride;
    }
}

static av_always_inline void
FUNC(put_unweighted_pred_avg)(uint8_t *_dst, ptrdiff_t _dststride,
                              int16_t *src1, int16_t *src2,
                              ptrdiff_t srcstride,
                              int width, int height)
{
    int x, y;
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dststride = _dststride / sizeof(pixel);

    int shift = 14 + 1 - BIT_DEPTH;
#if BIT_DEPTH < 14
    int offset = 1 << (shift - 1);
#else
    int offset = 0;
#endif

    srcstride /= sizeof(*src1);
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel((src1[x] + src2[x] + offset) >> shift);
        dst  += dststride;
        src1 += srcstride;
        src2 += srcstride;
    }
}

static av_always_inline void
FUNC(weighted_pred)(uint8_t denom, int16_t wlxFlag, int16_t olxFlag,
                    uint8_t *_dst, ptrdiff_t _dststride,
                    int16_t *src, ptrdiff_t srcstride,
                    int width, int height)
{
    int shift, log2Wd, wx, ox, x, y, offset;
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dststride = _dststride / sizeof(pixel);

    shift  = 14 - BIT_DEPTH;
    log2Wd = denom + shift;
    offset = 1 << (log2Wd - 1);
    wx     = wlxFlag;
    ox     = olxFlag * (1 << (BIT_DEPTH - 8));

    srcstride /= sizeof(*src);
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            if (log2Wd >= 1) {
                dst[x] = av_clip_pixel(((src[x] * wx + offset) >> log2Wd) + ox);
            } else {
                dst[x] = av_clip_pixel(src[x] * wx + ox);
            }
        }
        dst += dststride;
        src += srcstride;
    }
}

static av_always_inline void
FUNC(weighted_pred_avg)(uint8_t denom,
                        int16_t wl0Flag, int16_t wl1Flag,
                        int16_t ol0Flag, int16_t ol1Flag,
                        uint8_t *_dst, ptrdiff_t _dststride,
                        int16_t *src1, int16_t *src2,
                        ptrdiff_t srcstride,
                        int width, int height)
{
    int shift, log2Wd, w0, w1, o0, o1, x, y;
    pixel *dst = (pixel *)_dst;
    ptrdiff_t dststride = _dststride / sizeof(pixel);

    shift  = 14 - BIT_DEPTH;
    log2Wd = denom + shift;
    w0     = wl0Flag;
    w1     = wl1Flag;
    o0     = ol0Flag * (1 << (BIT_DEPTH - 8));
    o1     = ol1Flag * (1 << (BIT_DEPTH - 8));

    srcstride /= sizeof(*src1);
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel((src1[x] * w0 + src2[x] * w1 +
                                    ((o0 + o1 + 1) << log2Wd)) >> (log2Wd + 1));
        dst  += dststride;
        src1 += srcstride;
        src2 += srcstride;
    }
}

#define PUT_PRED(w)                                                                            \
static void FUNC(put_unweighted_pred_ ## w)(uint8_t *dst, ptrdiff_t dststride,                 \
                                            int16_t *src, ptrdiff_t srcstride,                 \
                                            int height)                                        \
{                                                                                              \
    FUNC(put_unweighted_pred)(dst, dststride, src, srcstride, w, height);                      \
}                                                                                              \
static void FUNC(put_unweighted_pred_avg_ ## w)(uint8_t *dst, ptrdiff_t dststride,             \
                                                int16_t *src1, int16_t *src2,                  \
                                                ptrdiff_t srcstride, int height)               \
{                                                                                              \
    FUNC(put_unweighted_pred_avg)(dst, dststride, src1, src2, srcstride, w, height);           \
}                                                                                              \
static void FUNC(put_weighted_pred_ ## w)(uint8_t denom, int16_t weight, int16_t offset,       \
                                          uint8_t *dst, ptrdiff_t dststride,                   \
                                          int16_t *src, ptrdiff_t srcstride, int height)       \
{                                                                                              \
    FUNC(weighted_pred)(denom, weight, offset,                                                 \
                        dst, dststride, src, srcstride, w, height);                            \
}                                                                                              \
static void FUNC(put_weighted_pred_avg_ ## w)(uint8_t denom, int16_t weight0, int16_t weight1, \
                                              int16_t offset0, int16_t offset1,                \
                                              uint8_t *dst, ptrdiff_t dststride,               \
                                              int16_t *src1, int16_t *src2,                    \
                                              ptrdiff_t srcstride, int height)                 \
{                                                                                              \
    FUNC(weighted_pred_avg)(denom, weight0, weight1, offset0, offset1,                         \
                            dst, dststride, src1, src2, srcstride, w, height);                 \
}

PUT_PRED(64)
PUT_PRED(48)
PUT_PRED(32)
PUT_PRED(24)
PUT_PRED(16)
PUT_PRED(12)
PUT_PRED(8)
PUT_PRED(6)
PUT_PRED(4)
PUT_PRED(2)

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

static void FUNC(hevc_loop_filter_luma)(uint8_t *_pix,
                                        ptrdiff_t _xstride, ptrdiff_t _ystride,
                                        int beta, int *_tc,
                                        uint8_t *_no_p, uint8_t *_no_q)
{
    int d, j;
    pixel *pix        = (pixel *)_pix;
    ptrdiff_t xstride = _xstride / sizeof(pixel);
    ptrdiff_t ystride = _ystride / sizeof(pixel);

    beta <<= BIT_DEPTH - 8;

    for (j = 0; j < 2; j++) {
        const int dp0  = abs(P2  - 2 * P1  + P0);
        const int dq0  = abs(Q2  - 2 * Q1  + Q0);
        const int dp3  = abs(TP2 - 2 * TP1 + TP0);
        const int dq3  = abs(TQ2 - 2 * TQ1 + TQ0);
        const int d0   = dp0 + dq0;
        const int d3   = dp3 + dq3;
        const int tc   = _tc[j]   << (BIT_DEPTH - 8);
        const int no_p = _no_p[j];
        const int no_q = _no_q[j];

        if (d0 + d3 >= beta) {
            pix += 4 * ystride;
            continue;
        } else {
            const int beta_3 = beta >> 3;
            const int beta_2 = beta >> 2;
            const int tc25   = ((tc * 5 + 1) >> 1);

            if (abs(P3  -  P0) + abs(Q3  -  Q0) < beta_3 && abs(P0  -  Q0) < tc25 &&
                abs(TP3 - TP0) + abs(TQ3 - TQ0) < beta_3 && abs(TP0 - TQ0) < tc25 &&
                                      (d0 << 1) < beta_2 &&      (d3 << 1) < beta_2) {
                // strong filtering
                const int tc2 = tc << 1;
                for (d = 0; d < 4; d++) {
                    const int p3 = P3;
                    const int p2 = P2;
                    const int p1 = P1;
                    const int p0 = P0;
                    const int q0 = Q0;
                    const int q1 = Q1;
                    const int q2 = Q2;
                    const int q3 = Q3;
                    if (!no_p) {
                        P0 = p0 + av_clip(((p2 + 2 * p1 + 2 * p0 + 2 * q0 + q1 + 4) >> 3) - p0, -tc2, tc2);
                        P1 = p1 + av_clip(((p2 + p1 + p0 + q0 + 2) >> 2) - p1, -tc2, tc2);
                        P2 = p2 + av_clip(((2 * p3 + 3 * p2 + p1 + p0 + q0 + 4) >> 3) - p2, -tc2, tc2);
                    }
                    if (!no_q) {
                        Q0 = q0 + av_clip(((p1 + 2 * p0 + 2 * q0 + 2 * q1 + q2 + 4) >> 3) - q0, -tc2, tc2);
                        Q1 = q1 + av_clip(((p0 + q0 + q1 + q2 + 2) >> 2) - q1, -tc2, tc2);
                        Q2 = q2 + av_clip(((2 * q3 + 3 * q2 + q1 + q0 + p0 + 4) >> 3) - q2, -tc2, tc2);
                    }
                    pix += ystride;
                }
            } else { // normal filtering
                int nd_p = 1;
                int nd_q = 1;
                const int tc_2 = tc >> 1;
                if (dp0 + dp3 < ((beta + (beta >> 1)) >> 3))
                    nd_p = 2;
                if (dq0 + dq3 < ((beta + (beta >> 1)) >> 3))
                    nd_q = 2;

                for (d = 0; d < 4; d++) {
                    const int p2 = P2;
                    const int p1 = P1;
                    const int p0 = P0;
                    const int q0 = Q0;
                    const int q1 = Q1;
                    const int q2 = Q2;
                    int delta0   = (9 * (q0 - p0) - 3 * (q1 - p1) + 8) >> 4;
                    if (abs(delta0) < 10 * tc) {
                        delta0 = av_clip(delta0, -tc, tc);
                        if (!no_p)
                            P0 = av_clip_pixel(p0 + delta0);
                        if (!no_q)
                            Q0 = av_clip_pixel(q0 - delta0);
                        if (!no_p && nd_p > 1) {
                            const int deltap1 = av_clip((((p2 + p0 + 1) >> 1) - p1 + delta0) >> 1, -tc_2, tc_2);
                            P1 = av_clip_pixel(p1 + deltap1);
                        }
                        if (!no_q && nd_q > 1) {
                            const int deltaq1 = av_clip((((q2 + q0 + 1) >> 1) - q1 - delta0) >> 1, -tc_2, tc_2);
                            Q1 = av_clip_pixel(q1 + deltaq1);
                        }
                    }
                    pix += ystride;
                }
            }
        }
    }
}

static void FUNC(hevc_loop_filter_chroma)(uint8_t *_pix, ptrdiff_t _xstride,
                                          ptrdiff_t _ystride, int *_tc,
                                          uint8_t *_no_p, uint8_t *_no_q)
{
    int d, j, no_p, no_q;
    pixel *pix        = (pixel *)_pix;
    ptrdiff_t xstride = _xstride / sizeof(pixel);
    ptrdiff_t ystride = _ystride / sizeof(pixel);

    for (j = 0; j < 2; j++) {
        const int tc = _tc[j] << (BIT_DEPTH - 8);
        if (tc <= 0) {
            pix += 4 * ystride;
            continue;
        }
        no_p = _no_p[j];
        no_q = _no_q[j];

        for (d = 0; d < 4; d++) {
            int delta0;
            const int p1 = P1;
            const int p0 = P0;
            const int q0 = Q0;
            const int q1 = Q1;
            delta0 = av_clip((((q0 - p0) * 4) + p1 - q1 + 4) >> 3, -tc, tc);
            if (!no_p)
                P0 = av_clip_pixel(p0 + delta0);
            if (!no_q)
                Q0 = av_clip_pixel(q0 - delta0);
            pix += ystride;
        }
    }
}

static void FUNC(hevc_h_loop_filter_chroma)(uint8_t *pix, ptrdiff_t stride,
                                            int *tc, uint8_t *no_p,
                                            uint8_t *no_q)
{
    FUNC(hevc_loop_filter_chroma)(pix, stride, sizeof(pixel), tc, no_p, no_q);
}

static void FUNC(hevc_v_loop_filter_chroma)(uint8_t *pix, ptrdiff_t stride,
                                            int *tc, uint8_t *no_p,
                                            uint8_t *no_q)
{
    FUNC(hevc_loop_filter_chroma)(pix, sizeof(pixel), stride, tc, no_p, no_q);
}

static void FUNC(hevc_h_loop_filter_luma)(uint8_t *pix, ptrdiff_t stride,
                                          int beta, int *tc, uint8_t *no_p,
                                          uint8_t *no_q)
{
    FUNC(hevc_loop_filter_luma)(pix, stride, sizeof(pixel),
                                beta, tc, no_p, no_q);
}

static void FUNC(hevc_v_loop_filter_luma)(uint8_t *pix, ptrdiff_t stride,
                                          int beta, int *tc, uint8_t *no_p,
                                          uint8_t *no_q)
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
