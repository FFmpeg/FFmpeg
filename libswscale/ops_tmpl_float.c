/**
 * Copyright (C) 2025 Niklas Haas
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

#include "libavutil/avassert.h"

#include "ops_backend.h"

#ifndef BIT_DEPTH
#  define BIT_DEPTH 32
#endif

#if BIT_DEPTH == 32
#  define PIXEL_TYPE SWS_PIXEL_F32
#  define PIXEL_MAX  FLT_MAX
#  define PIXEL_MIN  FLT_MIN
#  define pixel_t    float
#  define block_t    f32block_t
#  define px         f32
#else
#  error Invalid BIT_DEPTH
#endif

#define IS_FLOAT 1
#define FMT_CHAR f
#include "ops_tmpl_common.c"

DECL_SETUP(setup_dither)
{
    const int size = 1 << op->dither.size_log2;
    if (!size) {
        /* We special case this value */
        av_assert1(!av_cmp_q(op->dither.matrix[0], av_make_q(1, 2)));
        out->ptr = NULL;
        return 0;
    }

    const int width = FFMAX(size, SWS_BLOCK_SIZE);
    pixel_t *matrix = out->ptr = av_malloc(sizeof(pixel_t) * size * width);
    if (!matrix)
        return AVERROR(ENOMEM);

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++)
            matrix[y * width + x] = av_q2pixel(op->dither.matrix[y * size + x]);
        for (int x = size; x < width; x++) /* pad to block size */
            matrix[y * width + x] = matrix[y * width + (x % size)];
    }

    return 0;
}

DECL_FUNC(dither, const int size_log2)
{
    const pixel_t *restrict matrix = impl->priv.ptr;
    const int mask = (1 << size_log2) - 1;
    const int y_line = iter->y;
    const int row0 = (y_line +  0) & mask;
    const int row1 = (y_line +  3) & mask;
    const int row2 = (y_line +  2) & mask;
    const int row3 = (y_line +  5) & mask;
    const int size = 1 << size_log2;
    const int width = FFMAX(size, SWS_BLOCK_SIZE);
    const int base = iter->x & ~(SWS_BLOCK_SIZE - 1) & (size - 1);

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        x[i] += size_log2 ? matrix[row0 * width + base + i] : (pixel_t) 0.5;
        y[i] += size_log2 ? matrix[row1 * width + base + i] : (pixel_t) 0.5;
        z[i] += size_log2 ? matrix[row2 * width + base + i] : (pixel_t) 0.5;
        w[i] += size_log2 ? matrix[row3 * width + base + i] : (pixel_t) 0.5;
    }

    CONTINUE(block_t, x, y, z, w);
}

#define WRAP_DITHER(N)                                                          \
DECL_IMPL(dither##N)                                                            \
{                                                                               \
    CALL(dither, N);                                                            \
}                                                                               \
                                                                                \
DECL_ENTRY(dither##N,                                                           \
    .op = SWS_OP_DITHER,                                                        \
    .dither_size = N,                                                           \
    .setup = fn(setup_dither),                                                  \
    .free = av_free,                                                            \
);

WRAP_DITHER(0)
WRAP_DITHER(1)
WRAP_DITHER(2)
WRAP_DITHER(3)
WRAP_DITHER(4)
WRAP_DITHER(5)
WRAP_DITHER(6)
WRAP_DITHER(7)
WRAP_DITHER(8)

typedef struct {
    /* Stored in split form for convenience */
    pixel_t m[4][4];
    pixel_t k[4];
} fn(LinCoeffs);

DECL_SETUP(setup_linear)
{
    fn(LinCoeffs) c;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++)
            c.m[i][j] = av_q2pixel(op->lin.m[i][j]);
        c.k[i] = av_q2pixel(op->lin.m[i][4]);
    }

    return SETUP_MEMDUP(c);
}

/**
 * Fully general case for a 5x5 linear affine transformation. Should never be
 * called without constant `mask`. This function will compile down to the
 * appropriately optimized version for the required subset of operations when
 * called with a constant mask.
 */
DECL_FUNC(linear_mask, const uint32_t mask)
{
    const fn(LinCoeffs) c = *(const fn(LinCoeffs) *) impl->priv.ptr;

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        const pixel_t xx = x[i];
        const pixel_t yy = y[i];
        const pixel_t zz = z[i];
        const pixel_t ww = w[i];

        x[i]  = (mask & SWS_MASK_OFF(0)) ? c.k[0] : 0;
        x[i] += (mask & SWS_MASK(0, 0))  ? c.m[0][0] * xx : xx;
        x[i] += (mask & SWS_MASK(0, 1))  ? c.m[0][1] * yy : 0;
        x[i] += (mask & SWS_MASK(0, 2))  ? c.m[0][2] * zz : 0;
        x[i] += (mask & SWS_MASK(0, 3))  ? c.m[0][3] * ww : 0;

        y[i]  = (mask & SWS_MASK_OFF(1)) ? c.k[1] : 0;
        y[i] += (mask & SWS_MASK(1, 0))  ? c.m[1][0] * xx : 0;
        y[i] += (mask & SWS_MASK(1, 1))  ? c.m[1][1] * yy : yy;
        y[i] += (mask & SWS_MASK(1, 2))  ? c.m[1][2] * zz : 0;
        y[i] += (mask & SWS_MASK(1, 3))  ? c.m[1][3] * ww : 0;

        z[i]  = (mask & SWS_MASK_OFF(2)) ? c.k[2] : 0;
        z[i] += (mask & SWS_MASK(2, 0))  ? c.m[2][0] * xx : 0;
        z[i] += (mask & SWS_MASK(2, 1))  ? c.m[2][1] * yy : 0;
        z[i] += (mask & SWS_MASK(2, 2))  ? c.m[2][2] * zz : zz;
        z[i] += (mask & SWS_MASK(2, 3))  ? c.m[2][3] * ww : 0;

        w[i]  = (mask & SWS_MASK_OFF(3)) ? c.k[3] : 0;
        w[i] += (mask & SWS_MASK(3, 0))  ? c.m[3][0] * xx : 0;
        w[i] += (mask & SWS_MASK(3, 1))  ? c.m[3][1] * yy : 0;
        w[i] += (mask & SWS_MASK(3, 2))  ? c.m[3][2] * zz : 0;
        w[i] += (mask & SWS_MASK(3, 3))  ? c.m[3][3] * ww : ww;
    }

    CONTINUE(block_t, x, y, z, w);
}

#define WRAP_LINEAR(NAME, MASK)                                                 \
DECL_IMPL(linear_##NAME)                                                        \
{                                                                               \
    CALL(linear_mask, MASK);                                                    \
}                                                                               \
                                                                                \
DECL_ENTRY(linear_##NAME,                                                       \
    .op    = SWS_OP_LINEAR,                                                     \
    .setup = fn(setup_linear),                                                  \
    .free  = av_free,                                                           \
    .linear_mask = (MASK),                                                      \
);

WRAP_LINEAR(luma,      SWS_MASK_LUMA)
WRAP_LINEAR(alpha,     SWS_MASK_ALPHA)
WRAP_LINEAR(lumalpha,  SWS_MASK_LUMA | SWS_MASK_ALPHA)
WRAP_LINEAR(dot3,      0x7)
WRAP_LINEAR(row0,      SWS_MASK_ROW(0))
WRAP_LINEAR(row0a,     SWS_MASK_ROW(0) | SWS_MASK_ALPHA)
WRAP_LINEAR(diag3,     SWS_MASK_DIAG3)
WRAP_LINEAR(diag4,     SWS_MASK_DIAG4)
WRAP_LINEAR(diagoff3,  SWS_MASK_DIAG3 | SWS_MASK_OFF3)
WRAP_LINEAR(matrix3,   SWS_MASK_MAT3)
WRAP_LINEAR(affine3,   SWS_MASK_MAT3 | SWS_MASK_OFF3)
WRAP_LINEAR(affine3a,  SWS_MASK_MAT3 | SWS_MASK_OFF3 | SWS_MASK_ALPHA)
WRAP_LINEAR(matrix4,   SWS_MASK_MAT4)
WRAP_LINEAR(affine4,   SWS_MASK_MAT4 | SWS_MASK_OFF4)

static const SwsOpTable fn(op_table_float) = {
    .block_size = SWS_BLOCK_SIZE,
    .entries = {
        REF_COMMON_PATTERNS(convert_uint8),
        REF_COMMON_PATTERNS(convert_uint16),
        REF_COMMON_PATTERNS(convert_uint32),

        &fn(op_clear_1110),
        REF_COMMON_PATTERNS(min),
        REF_COMMON_PATTERNS(max),
        REF_COMMON_PATTERNS(scale),

        &fn(op_dither0),
        &fn(op_dither1),
        &fn(op_dither2),
        &fn(op_dither3),
        &fn(op_dither4),
        &fn(op_dither5),
        &fn(op_dither6),
        &fn(op_dither7),
        &fn(op_dither8),

        &fn(op_linear_luma),
        &fn(op_linear_alpha),
        &fn(op_linear_lumalpha),
        &fn(op_linear_dot3),
        &fn(op_linear_row0),
        &fn(op_linear_row0a),
        &fn(op_linear_diag3),
        &fn(op_linear_diag4),
        &fn(op_linear_diagoff3),
        &fn(op_linear_matrix3),
        &fn(op_linear_affine3),
        &fn(op_linear_affine3a),
        &fn(op_linear_matrix4),
        &fn(op_linear_affine4),

        NULL
    },
};

#undef PIXEL_TYPE
#undef PIXEL_MAX
#undef PIXEL_MIN
#undef pixel_t
#undef block_t
#undef px

#undef FMT_CHAR
#undef IS_FLOAT
