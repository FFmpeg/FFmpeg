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

#include <float.h>

#include "libavutil/avassert.h"
#include "libavutil/mem.h"

#include "../ops_chain.h"

#define DECL_ENTRY(TYPE, NAME, ...)                                             \
    static const SwsOpEntry op_##NAME = {                                       \
        .type = SWS_PIXEL_##TYPE,                                               \
        __VA_ARGS__                                                             \
    }

#define DECL_ASM(TYPE, NAME, ...)                                               \
    void ff_##NAME(void);                                                       \
    DECL_ENTRY(TYPE, NAME,                                                      \
        .func = ff_##NAME,                                                      \
        __VA_ARGS__)

#define DECL_PATTERN(TYPE, NAME, X, Y, Z, W, ...)                               \
    DECL_ASM(TYPE, p##X##Y##Z##W##_##NAME,                                      \
        .unused = { !X, !Y, !Z, !W },                                           \
        __VA_ARGS__                                                             \
    )

#define REF_PATTERN(NAME, X, Y, Z, W)                                           \
    &op_p##X##Y##Z##W##_##NAME

#define DECL_COMMON_PATTERNS(TYPE, NAME, ...)                                   \
    DECL_PATTERN(TYPE, NAME, 1, 0, 0, 0, __VA_ARGS__);                          \
    DECL_PATTERN(TYPE, NAME, 1, 0, 0, 1, __VA_ARGS__);                          \
    DECL_PATTERN(TYPE, NAME, 1, 1, 1, 0, __VA_ARGS__);                          \
    DECL_PATTERN(TYPE, NAME, 1, 1, 1, 1, __VA_ARGS__)                           \

#define REF_COMMON_PATTERNS(NAME)                                               \
    REF_PATTERN(NAME, 1, 0, 0, 0),                                              \
    REF_PATTERN(NAME, 1, 0, 0, 1),                                              \
    REF_PATTERN(NAME, 1, 1, 1, 0),                                              \
    REF_PATTERN(NAME, 1, 1, 1, 1)

#define DECL_RW(EXT, TYPE, NAME, OP, ELEMS, PACKED, FRAC)                       \
    DECL_ASM(TYPE, NAME##ELEMS##EXT,                                            \
        .op = SWS_OP_##OP,                                                      \
        .rw = { .elems = ELEMS, .packed = PACKED, .frac = FRAC },               \
    );

#define DECL_PACKED_RW(EXT, DEPTH)                                              \
    DECL_RW(EXT, U##DEPTH, read##DEPTH##_packed,  READ,  2, true,  0)           \
    DECL_RW(EXT, U##DEPTH, read##DEPTH##_packed,  READ,  3, true,  0)           \
    DECL_RW(EXT, U##DEPTH, read##DEPTH##_packed,  READ,  4, true,  0)           \
    DECL_RW(EXT, U##DEPTH, write##DEPTH##_packed, WRITE, 2, true,  0)           \
    DECL_RW(EXT, U##DEPTH, write##DEPTH##_packed, WRITE, 3, true,  0)           \
    DECL_RW(EXT, U##DEPTH, write##DEPTH##_packed, WRITE, 4, true,  0)           \

#define DECL_PACK_UNPACK(EXT, TYPE, X, Y, Z, W)                                 \
    DECL_ASM(TYPE, pack_##X##Y##Z##W##EXT,                                      \
        .op = SWS_OP_PACK,                                                      \
        .pack.pattern = {X, Y, Z, W},                                           \
    );                                                                          \
                                                                                \
    DECL_ASM(TYPE, unpack_##X##Y##Z##W##EXT,                                    \
        .op = SWS_OP_UNPACK,                                                    \
        .pack.pattern = {X, Y, Z, W},                                           \
    );                                                                          \

static int setup_swap_bytes(const SwsOp *op, SwsOpPriv *out)
{
    const int mask = ff_sws_pixel_type_size(op->type) - 1;
    for (int i = 0; i < 16; i++)
        out->u8[i] = (i & ~mask) | (mask - (i & mask));
    return 0;
}

#define DECL_SWAP_BYTES(EXT, TYPE, X, Y, Z, W)                                  \
    DECL_ENTRY(TYPE, p##X##Y##Z##W##_swap_bytes_##TYPE##EXT,                    \
        .op = SWS_OP_SWAP_BYTES,                                                \
        .unused = { !X, !Y, !Z, !W },                                           \
        .func = ff_p##X##Y##Z##W##_shuffle##EXT,                                \
        .setup = setup_swap_bytes,                                              \
    );

#define DECL_CLEAR_ALPHA(EXT, IDX)                                              \
    DECL_ASM(U8, clear_alpha##IDX##EXT,                                         \
        .op = SWS_OP_CLEAR,                                                     \
        .clear_value = -1,                                                      \
        .unused[IDX] = true,                                                    \
    );                                                                          \

#define DECL_CLEAR_ZERO(EXT, IDX)                                               \
    DECL_ASM(U8, clear_zero##IDX##EXT,                                          \
        .op = SWS_OP_CLEAR,                                                     \
        .clear_value = 0,                                                       \
        .unused[IDX] = true,                                                    \
    );

static int setup_clear(const SwsOp *op, SwsOpPriv *out)
{
    for (int i = 0; i < 4; i++)
        out->u32[i] = (uint32_t) op->c.q4[i].num;
    return 0;
}

#define DECL_CLEAR(EXT, X, Y, Z, W)                                             \
    DECL_PATTERN(U8, clear##EXT, X, Y, Z, W,                                    \
        .op = SWS_OP_CLEAR,                                                     \
        .setup = setup_clear,                                                   \
        .flexible = true,                                                       \
    );

#define DECL_SWIZZLE(EXT, X, Y, Z, W)                                           \
    DECL_ASM(U8, swizzle_##X##Y##Z##W##EXT,                                     \
        .op = SWS_OP_SWIZZLE,                                                   \
        .swizzle.in = {X, Y, Z, W},                                             \
    );

#define DECL_CONVERT(EXT, FROM, TO)                                             \
    DECL_COMMON_PATTERNS(FROM, convert_##FROM##_##TO##EXT,                      \
        .op = SWS_OP_CONVERT,                                                   \
        .convert.to = SWS_PIXEL_##TO,                                           \
    );

#define DECL_EXPAND(EXT, FROM, TO)                                              \
    DECL_COMMON_PATTERNS(FROM, expand_##FROM##_##TO##EXT,                       \
        .op = SWS_OP_CONVERT,                                                   \
        .convert.to = SWS_PIXEL_##TO,                                           \
        .convert.expand = true,                                                 \
    );

static int setup_shift(const SwsOp *op, SwsOpPriv *out)
{
    out->u16[0] = op->c.u;
    return 0;
}

#define DECL_SHIFT16(EXT)                                                       \
    DECL_COMMON_PATTERNS(U16, lshift16##EXT,                                    \
        .op = SWS_OP_LSHIFT,                                                    \
        .setup = setup_shift,                                                   \
        .flexible = true,                                                       \
    );                                                                          \
                                                                                \
    DECL_COMMON_PATTERNS(U16, rshift16##EXT,                                    \
        .op = SWS_OP_RSHIFT,                                                    \
        .setup = setup_shift,                                                   \
        .flexible = true,                                                       \
    );

#define DECL_MIN_MAX(EXT)                                                       \
    DECL_COMMON_PATTERNS(F32, min##EXT,                                         \
        .op = SWS_OP_MIN,                                                       \
        .setup = ff_sws_setup_q4,                                               \
        .flexible = true,                                                       \
    );                                                                          \
                                                                                \
    DECL_COMMON_PATTERNS(F32, max##EXT,                                         \
        .op = SWS_OP_MAX,                                                       \
        .setup = ff_sws_setup_q4,                                               \
        .flexible = true,                                                       \
    );

#define DECL_SCALE(EXT)                                                         \
    DECL_COMMON_PATTERNS(F32, scale##EXT,                                       \
        .op = SWS_OP_SCALE,                                                     \
        .setup = ff_sws_setup_q,                                                \
    );

/* 2x2 matrix fits inside SwsOpPriv directly; save an indirect in this case */
static_assert(sizeof(SwsOpPriv) >= sizeof(float[2][2]), "2x2 dither matrix too large");
static int setup_dither(const SwsOp *op, SwsOpPriv *out)
{
    const int size = 1 << op->dither.size_log2;
    float *matrix = out->f32;
    if (size > 2) {
        matrix = out->ptr = av_mallocz(size * size * sizeof(*matrix));
        if (!matrix)
            return AVERROR(ENOMEM);
    }

    for (int i = 0; i < size * size; i++)
        matrix[i] = (float) op->dither.matrix[i].num / op->dither.matrix[i].den;

    return 0;
}

#define DECL_DITHER(EXT, SIZE)                                                  \
    DECL_COMMON_PATTERNS(F32, dither##SIZE##EXT,                                \
        .op    = SWS_OP_DITHER,                                                 \
        .setup = setup_dither,                                                  \
        .free  = (1 << SIZE) > 2 ? av_free : NULL,                              \
        .dither_size = SIZE,                                                    \
    );

static int setup_linear(const SwsOp *op, SwsOpPriv *out)
{
    float *matrix = out->ptr = av_mallocz(sizeof(float[4][5]));
    if (!matrix)
        return AVERROR(ENOMEM);

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 5; x++)
            matrix[y * 5 + x] = (float) op->lin.m[y][x].num / op->lin.m[y][x].den;
    }

    return 0;
}

#define DECL_LINEAR(EXT, NAME, MASK)                                            \
    DECL_ASM(F32, NAME##EXT,                                                    \
        .op    = SWS_OP_LINEAR,                                                 \
        .setup = setup_linear,                                                  \
        .free  = av_free,                                                       \
        .linear_mask = (MASK),                                                  \
    );

#define DECL_FUNCS_8(SIZE, EXT, FLAG)                                           \
    DECL_RW(EXT, U8, read_planar,   READ,  1, false, 0)                         \
    DECL_RW(EXT, U8, read_planar,   READ,  2, false, 0)                         \
    DECL_RW(EXT, U8, read_planar,   READ,  3, false, 0)                         \
    DECL_RW(EXT, U8, read_planar,   READ,  4, false, 0)                         \
    DECL_RW(EXT, U8, write_planar,  WRITE, 1, false, 0)                         \
    DECL_RW(EXT, U8, write_planar,  WRITE, 2, false, 0)                         \
    DECL_RW(EXT, U8, write_planar,  WRITE, 3, false, 0)                         \
    DECL_RW(EXT, U8, write_planar,  WRITE, 4, false, 0)                         \
    DECL_RW(EXT, U8, read_nibbles,  READ,  1, false, 1)                         \
    DECL_RW(EXT, U8, read_bits,     READ,  1, false, 3)                         \
    DECL_RW(EXT, U8, write_bits,    WRITE, 1, false, 3)                         \
    DECL_PACKED_RW(EXT, 8)                                                      \
    DECL_PACK_UNPACK(EXT, U8, 1, 2, 1, 0)                                       \
    DECL_PACK_UNPACK(EXT, U8, 3, 3, 2, 0)                                       \
    DECL_PACK_UNPACK(EXT, U8, 2, 3, 3, 0)                                       \
    void ff_p1000_shuffle##EXT(void);                                           \
    void ff_p1001_shuffle##EXT(void);                                           \
    void ff_p1110_shuffle##EXT(void);                                           \
    void ff_p1111_shuffle##EXT(void);                                           \
    DECL_SWIZZLE(EXT, 3, 0, 1, 2)                                               \
    DECL_SWIZZLE(EXT, 3, 0, 2, 1)                                               \
    DECL_SWIZZLE(EXT, 2, 1, 0, 3)                                               \
    DECL_SWIZZLE(EXT, 3, 2, 1, 0)                                               \
    DECL_SWIZZLE(EXT, 3, 1, 0, 2)                                               \
    DECL_SWIZZLE(EXT, 3, 2, 0, 1)                                               \
    DECL_SWIZZLE(EXT, 1, 2, 0, 3)                                               \
    DECL_SWIZZLE(EXT, 1, 0, 2, 3)                                               \
    DECL_SWIZZLE(EXT, 2, 0, 1, 3)                                               \
    DECL_SWIZZLE(EXT, 2, 3, 1, 0)                                               \
    DECL_SWIZZLE(EXT, 2, 1, 3, 0)                                               \
    DECL_SWIZZLE(EXT, 1, 2, 3, 0)                                               \
    DECL_SWIZZLE(EXT, 1, 3, 2, 0)                                               \
    DECL_SWIZZLE(EXT, 0, 2, 1, 3)                                               \
    DECL_SWIZZLE(EXT, 0, 2, 3, 1)                                               \
    DECL_SWIZZLE(EXT, 0, 3, 1, 2)                                               \
    DECL_SWIZZLE(EXT, 3, 1, 2, 0)                                               \
    DECL_SWIZZLE(EXT, 0, 3, 2, 1)                                               \
    DECL_SWIZZLE(EXT, 0, 0, 0, 3)                                               \
    DECL_SWIZZLE(EXT, 3, 0, 0, 0)                                               \
    DECL_SWIZZLE(EXT, 0, 0, 0, 1)                                               \
    DECL_SWIZZLE(EXT, 1, 0, 0, 0)                                               \
    DECL_CLEAR_ALPHA(EXT, 0)                                                    \
    DECL_CLEAR_ALPHA(EXT, 1)                                                    \
    DECL_CLEAR_ALPHA(EXT, 3)                                                    \
    DECL_CLEAR_ZERO(EXT, 0)                                                     \
    DECL_CLEAR_ZERO(EXT, 1)                                                     \
    DECL_CLEAR_ZERO(EXT, 3)                                                     \
    DECL_CLEAR(EXT, 1, 1, 1, 0)                                                 \
    DECL_CLEAR(EXT, 0, 1, 1, 1)                                                 \
    DECL_CLEAR(EXT, 0, 0, 1, 1)                                                 \
    DECL_CLEAR(EXT, 1, 0, 0, 1)                                                 \
    DECL_CLEAR(EXT, 1, 1, 0, 0)                                                 \
    DECL_CLEAR(EXT, 0, 1, 0, 1)                                                 \
    DECL_CLEAR(EXT, 1, 0, 1, 0)                                                 \
    DECL_CLEAR(EXT, 1, 0, 0, 0)                                                 \
    DECL_CLEAR(EXT, 0, 1, 0, 0)                                                 \
    DECL_CLEAR(EXT, 0, 0, 1, 0)                                                 \
                                                                                \
static const SwsOpTable ops8##EXT = {                                           \
    .cpu_flags = AV_CPU_FLAG_##FLAG,                                            \
    .block_size = SIZE,                                                         \
    .entries = {                                                                \
        &op_read_planar1##EXT,                                                  \
        &op_read_planar2##EXT,                                                  \
        &op_read_planar3##EXT,                                                  \
        &op_read_planar4##EXT,                                                  \
        &op_write_planar1##EXT,                                                 \
        &op_write_planar2##EXT,                                                 \
        &op_write_planar3##EXT,                                                 \
        &op_write_planar4##EXT,                                                 \
        &op_read8_packed2##EXT,                                                 \
        &op_read8_packed3##EXT,                                                 \
        &op_read8_packed4##EXT,                                                 \
        &op_write8_packed2##EXT,                                                \
        &op_write8_packed3##EXT,                                                \
        &op_write8_packed4##EXT,                                                \
        &op_read_nibbles1##EXT,                                                 \
        &op_read_bits1##EXT,                                                    \
        &op_write_bits1##EXT,                                                   \
        &op_pack_1210##EXT,                                                     \
        &op_pack_3320##EXT,                                                     \
        &op_pack_2330##EXT,                                                     \
        &op_unpack_1210##EXT,                                                   \
        &op_unpack_3320##EXT,                                                   \
        &op_unpack_2330##EXT,                                                   \
        &op_swizzle_3012##EXT,                                                  \
        &op_swizzle_3021##EXT,                                                  \
        &op_swizzle_2103##EXT,                                                  \
        &op_swizzle_3210##EXT,                                                  \
        &op_swizzle_3102##EXT,                                                  \
        &op_swizzle_3201##EXT,                                                  \
        &op_swizzle_1203##EXT,                                                  \
        &op_swizzle_1023##EXT,                                                  \
        &op_swizzle_2013##EXT,                                                  \
        &op_swizzle_2310##EXT,                                                  \
        &op_swizzle_2130##EXT,                                                  \
        &op_swizzle_1230##EXT,                                                  \
        &op_swizzle_1320##EXT,                                                  \
        &op_swizzle_0213##EXT,                                                  \
        &op_swizzle_0231##EXT,                                                  \
        &op_swizzle_0312##EXT,                                                  \
        &op_swizzle_3120##EXT,                                                  \
        &op_swizzle_0321##EXT,                                                  \
        &op_swizzle_0003##EXT,                                                  \
        &op_swizzle_0001##EXT,                                                  \
        &op_swizzle_3000##EXT,                                                  \
        &op_swizzle_1000##EXT,                                                  \
        &op_clear_alpha0##EXT,                                                  \
        &op_clear_alpha1##EXT,                                                  \
        &op_clear_alpha3##EXT,                                                  \
        &op_clear_zero0##EXT,                                                   \
        &op_clear_zero1##EXT,                                                   \
        &op_clear_zero3##EXT,                                                   \
        REF_PATTERN(clear##EXT, 1, 1, 1, 0),                                    \
        REF_PATTERN(clear##EXT, 0, 1, 1, 1),                                    \
        REF_PATTERN(clear##EXT, 0, 0, 1, 1),                                    \
        REF_PATTERN(clear##EXT, 1, 0, 0, 1),                                    \
        REF_PATTERN(clear##EXT, 1, 1, 0, 0),                                    \
        REF_PATTERN(clear##EXT, 0, 1, 0, 1),                                    \
        REF_PATTERN(clear##EXT, 1, 0, 1, 0),                                    \
        REF_PATTERN(clear##EXT, 1, 0, 0, 0),                                    \
        REF_PATTERN(clear##EXT, 0, 1, 0, 0),                                    \
        REF_PATTERN(clear##EXT, 0, 0, 1, 0),                                    \
        NULL                                                                    \
    },                                                                          \
};

#define DECL_FUNCS_16(SIZE, EXT, FLAG)                                          \
    DECL_PACKED_RW(EXT, 16)                                                     \
    DECL_PACK_UNPACK(EXT, U16, 4, 4, 4, 0)                                      \
    DECL_PACK_UNPACK(EXT, U16, 5, 5, 5, 0)                                      \
    DECL_PACK_UNPACK(EXT, U16, 5, 6, 5, 0)                                      \
    DECL_SWAP_BYTES(EXT, U16, 1, 0, 0, 0)                                       \
    DECL_SWAP_BYTES(EXT, U16, 1, 0, 0, 1)                                       \
    DECL_SWAP_BYTES(EXT, U16, 1, 1, 1, 0)                                       \
    DECL_SWAP_BYTES(EXT, U16, 1, 1, 1, 1)                                       \
    DECL_SHIFT16(EXT)                                                           \
    DECL_CONVERT(EXT,  U8, U16)                                                 \
    DECL_CONVERT(EXT, U16,  U8)                                                 \
    DECL_EXPAND(EXT,   U8, U16)                                                 \
                                                                                \
static const SwsOpTable ops16##EXT = {                                          \
    .cpu_flags = AV_CPU_FLAG_##FLAG,                                            \
    .block_size = SIZE,                                                         \
    .entries = {                                                                \
        &op_read16_packed2##EXT,                                                \
        &op_read16_packed3##EXT,                                                \
        &op_read16_packed4##EXT,                                                \
        &op_write16_packed2##EXT,                                               \
        &op_write16_packed3##EXT,                                               \
        &op_write16_packed4##EXT,                                               \
        &op_pack_4440##EXT,                                                     \
        &op_pack_5550##EXT,                                                     \
        &op_pack_5650##EXT,                                                     \
        &op_unpack_4440##EXT,                                                   \
        &op_unpack_5550##EXT,                                                   \
        &op_unpack_5650##EXT,                                                   \
        REF_COMMON_PATTERNS(swap_bytes_U16##EXT),                               \
        REF_COMMON_PATTERNS(convert_U8_U16##EXT),                               \
        REF_COMMON_PATTERNS(convert_U16_U8##EXT),                               \
        REF_COMMON_PATTERNS(expand_U8_U16##EXT),                                \
        REF_COMMON_PATTERNS(lshift16##EXT),                                     \
        REF_COMMON_PATTERNS(rshift16##EXT),                                     \
        NULL                                                                    \
    },                                                                          \
};

#define DECL_FUNCS_32(SIZE, EXT, FLAG)                                          \
    DECL_PACKED_RW(_m2##EXT, 32)                                                \
    DECL_PACK_UNPACK(_m2##EXT, U32, 10, 10, 10, 2)                              \
    DECL_PACK_UNPACK(_m2##EXT, U32, 2, 10, 10, 10)                              \
    DECL_SWAP_BYTES(_m2##EXT, U32, 1, 0, 0, 0)                                  \
    DECL_SWAP_BYTES(_m2##EXT, U32, 1, 0, 0, 1)                                  \
    DECL_SWAP_BYTES(_m2##EXT, U32, 1, 1, 1, 0)                                  \
    DECL_SWAP_BYTES(_m2##EXT, U32, 1, 1, 1, 1)                                  \
    DECL_CONVERT(EXT,  U8, U32)                                                 \
    DECL_CONVERT(EXT, U32,  U8)                                                 \
    DECL_CONVERT(EXT, U16, U32)                                                 \
    DECL_CONVERT(EXT, U32, U16)                                                 \
    DECL_CONVERT(EXT,  U8, F32)                                                 \
    DECL_CONVERT(EXT, F32,  U8)                                                 \
    DECL_CONVERT(EXT, U16, F32)                                                 \
    DECL_CONVERT(EXT, F32, U16)                                                 \
    DECL_EXPAND(EXT,   U8, U32)                                                 \
    DECL_MIN_MAX(EXT)                                                           \
    DECL_SCALE(EXT)                                                             \
    DECL_DITHER(EXT, 0)                                                         \
    DECL_DITHER(EXT, 1)                                                         \
    DECL_DITHER(EXT, 2)                                                         \
    DECL_DITHER(EXT, 3)                                                         \
    DECL_DITHER(EXT, 4)                                                         \
    DECL_DITHER(EXT, 5)                                                         \
    DECL_DITHER(EXT, 6)                                                         \
    DECL_DITHER(EXT, 7)                                                         \
    DECL_DITHER(EXT, 8)                                                         \
    DECL_LINEAR(EXT, luma,      SWS_MASK_LUMA)                                  \
    DECL_LINEAR(EXT, alpha,     SWS_MASK_ALPHA)                                 \
    DECL_LINEAR(EXT, lumalpha,  SWS_MASK_LUMA | SWS_MASK_ALPHA)                 \
    DECL_LINEAR(EXT, dot3,      0x7)                                            \
    DECL_LINEAR(EXT, row0,      SWS_MASK_ROW(0))                                \
    DECL_LINEAR(EXT, row0a,     SWS_MASK_ROW(0) | SWS_MASK_ALPHA)               \
    DECL_LINEAR(EXT, diag3,     SWS_MASK_DIAG3)                                 \
    DECL_LINEAR(EXT, diag4,     SWS_MASK_DIAG4)                                 \
    DECL_LINEAR(EXT, diagoff3,  SWS_MASK_DIAG3 | SWS_MASK_OFF3)                 \
    DECL_LINEAR(EXT, matrix3,   SWS_MASK_MAT3)                                  \
    DECL_LINEAR(EXT, affine3,   SWS_MASK_MAT3 | SWS_MASK_OFF3)                  \
    DECL_LINEAR(EXT, affine3a,  SWS_MASK_MAT3 | SWS_MASK_OFF3 | SWS_MASK_ALPHA) \
    DECL_LINEAR(EXT, matrix4,   SWS_MASK_MAT4)                                  \
    DECL_LINEAR(EXT, affine4,   SWS_MASK_MAT4 | SWS_MASK_OFF4)                  \
                                                                                \
static const SwsOpTable ops32##EXT = {                                          \
    .cpu_flags = AV_CPU_FLAG_##FLAG,                                            \
    .block_size = SIZE,                                                         \
    .entries = {                                                                \
        &op_read32_packed2_m2##EXT,                                             \
        &op_read32_packed3_m2##EXT,                                             \
        &op_read32_packed4_m2##EXT,                                             \
        &op_write32_packed2_m2##EXT,                                            \
        &op_write32_packed3_m2##EXT,                                            \
        &op_write32_packed4_m2##EXT,                                            \
        &op_pack_1010102_m2##EXT,                                               \
        &op_pack_2101010_m2##EXT,                                               \
        &op_unpack_1010102_m2##EXT,                                             \
        &op_unpack_2101010_m2##EXT,                                             \
        REF_COMMON_PATTERNS(swap_bytes_U32_m2##EXT),                            \
        REF_COMMON_PATTERNS(convert_U8_U32##EXT),                               \
        REF_COMMON_PATTERNS(convert_U32_U8##EXT),                               \
        REF_COMMON_PATTERNS(convert_U16_U32##EXT),                              \
        REF_COMMON_PATTERNS(convert_U32_U16##EXT),                              \
        REF_COMMON_PATTERNS(convert_U8_F32##EXT),                               \
        REF_COMMON_PATTERNS(convert_F32_U8##EXT),                               \
        REF_COMMON_PATTERNS(convert_U16_F32##EXT),                              \
        REF_COMMON_PATTERNS(convert_F32_U16##EXT),                              \
        REF_COMMON_PATTERNS(expand_U8_U32##EXT),                                \
        REF_COMMON_PATTERNS(min##EXT),                                          \
        REF_COMMON_PATTERNS(max##EXT),                                          \
        REF_COMMON_PATTERNS(scale##EXT),                                        \
        REF_COMMON_PATTERNS(dither0##EXT),                                      \
        REF_COMMON_PATTERNS(dither1##EXT),                                      \
        REF_COMMON_PATTERNS(dither2##EXT),                                      \
        REF_COMMON_PATTERNS(dither3##EXT),                                      \
        REF_COMMON_PATTERNS(dither4##EXT),                                      \
        REF_COMMON_PATTERNS(dither5##EXT),                                      \
        REF_COMMON_PATTERNS(dither6##EXT),                                      \
        REF_COMMON_PATTERNS(dither7##EXT),                                      \
        REF_COMMON_PATTERNS(dither8##EXT),                                      \
        &op_luma##EXT,                                                          \
        &op_alpha##EXT,                                                         \
        &op_lumalpha##EXT,                                                      \
        &op_dot3##EXT,                                                          \
        &op_row0##EXT,                                                          \
        &op_row0a##EXT,                                                         \
        &op_diag3##EXT,                                                         \
        &op_diag4##EXT,                                                         \
        &op_diagoff3##EXT,                                                      \
        &op_matrix3##EXT,                                                       \
        &op_affine3##EXT,                                                       \
        &op_affine3a##EXT,                                                      \
        &op_matrix4##EXT,                                                       \
        &op_affine4##EXT,                                                       \
        NULL                                                                    \
    },                                                                          \
};

DECL_FUNCS_8(16, _m1_sse4, SSE4)
DECL_FUNCS_8(32, _m1_avx2, AVX2)
DECL_FUNCS_8(32, _m2_sse4, SSE4)
DECL_FUNCS_8(64, _m2_avx2, AVX2)

DECL_FUNCS_16(16, _m1_avx2, AVX2)
DECL_FUNCS_16(32, _m2_avx2, AVX2)

DECL_FUNCS_32(16, _avx2, AVX2)

static av_const int get_mmsize(const int cpu_flags)
{
    if (cpu_flags & AV_CPU_FLAG_AVX512)
        return 64;
    else if (cpu_flags & AV_CPU_FLAG_AVX2)
        return 32;
    else if (cpu_flags & AV_CPU_FLAG_SSE4)
        return 16;
    else
        return AVERROR(ENOTSUP);
}

/**
 * Returns true if the operation's implementation only depends on the block
 * size, and not the underlying pixel type
 */
static bool op_is_type_invariant(const SwsOp *op)
{
    switch (op->op) {
    case SWS_OP_READ:
    case SWS_OP_WRITE:
        return !op->rw.packed && !op->rw.frac;
    case SWS_OP_SWIZZLE:
    case SWS_OP_CLEAR:
        return true;
    }

    return false;
}

static int solve_shuffle(const SwsOpList *ops, int mmsize, SwsCompiledOp *out)
{
    uint8_t shuffle[16];
    int read_bytes, write_bytes;
    int pixels;

    /* Solve the shuffle mask for one 128-bit lane only */
    pixels = ff_sws_solve_shuffle(ops, shuffle, 16, 0x80, &read_bytes, &write_bytes);
    if (pixels < 0)
        return pixels;

    /* We can't shuffle acress lanes, so restrict the vector size to XMM
     * whenever the read/write size would be a subset of the full vector */
    if (read_bytes < 16 || write_bytes < 16)
        mmsize = 16;

    const int num_lanes = mmsize / 16;
    const int in_total  = num_lanes * read_bytes;
    const int out_total = num_lanes * write_bytes;
    const int read_size = in_total <= 4 ? 4 : /* movd */
                          in_total <= 8 ? 8 : /* movq */
                          mmsize;             /* movu */

    *out = (SwsCompiledOp) {
        .priv       = av_memdup(shuffle, sizeof(shuffle)),
        .free       = av_free,
        .block_size = pixels * num_lanes,
        .over_read  = read_size - in_total,
        .over_write = mmsize - out_total,
        .cpu_flags  = mmsize > 32 ? AV_CPU_FLAG_AVX512 :
                      mmsize > 16 ? AV_CPU_FLAG_AVX2 :
                                    AV_CPU_FLAG_SSE4,
    };

    if (!out->priv)
        return AVERROR(ENOMEM);

#define ASSIGN_SHUFFLE_FUNC(IN, OUT, EXT)                                       \
do {                                                                            \
    SWS_DECL_FUNC(ff_packed_shuffle##IN##_##OUT##_##EXT);                       \
    if (in_total == IN && out_total == OUT)                                     \
        out->func = ff_packed_shuffle##IN##_##OUT##_##EXT;                      \
} while (0)

    ASSIGN_SHUFFLE_FUNC( 5, 15, sse4);
    ASSIGN_SHUFFLE_FUNC( 4, 16, sse4);
    ASSIGN_SHUFFLE_FUNC( 2, 12, sse4);
    ASSIGN_SHUFFLE_FUNC(16,  8, sse4);
    ASSIGN_SHUFFLE_FUNC(10, 15, sse4);
    ASSIGN_SHUFFLE_FUNC( 8, 16, sse4);
    ASSIGN_SHUFFLE_FUNC( 4, 12, sse4);
    ASSIGN_SHUFFLE_FUNC(15, 15, sse4);
    ASSIGN_SHUFFLE_FUNC(12, 16, sse4);
    ASSIGN_SHUFFLE_FUNC( 6, 12, sse4);
    ASSIGN_SHUFFLE_FUNC(16, 12, sse4);
    ASSIGN_SHUFFLE_FUNC(16, 16, sse4);
    ASSIGN_SHUFFLE_FUNC( 8, 12, sse4);
    ASSIGN_SHUFFLE_FUNC(12, 12, sse4);
    ASSIGN_SHUFFLE_FUNC(32, 32, avx2);
    ASSIGN_SHUFFLE_FUNC(64, 64, avx512);
    av_assert1(out->func);
    return 0;
}

/* Normalize clear values into 32-bit integer constants */
static void normalize_clear(SwsOp *op)
{
    static_assert(sizeof(uint32_t) == sizeof(int), "int size mismatch");
    SwsOpPriv priv;
    union {
        uint32_t u32;
        int i;
    } c;

    ff_sws_setup_q4(op, &priv);
    for (int i = 0; i < 4; i++) {
        if (!op->c.q4[i].den)
            continue;
        switch (ff_sws_pixel_type_size(op->type)) {
        case 1: c.u32 = 0x1010101 * priv.u8[i]; break;
        case 2: c.u32 = priv.u16[i] << 16 | priv.u16[i]; break;
        case 4: c.u32 = priv.u32[i]; break;
        }

        op->c.q4[i].num = c.i;
        op->c.q4[i].den = 1;
    }
}

static int compile(SwsContext *ctx, SwsOpList *ops, SwsCompiledOp *out)
{
    const int cpu_flags = av_get_cpu_flags();
    const int mmsize = get_mmsize(cpu_flags);
    if (mmsize < 0)
        return mmsize;

    av_assert1(ops->num_ops > 0);
    const SwsOp read = ops->ops[0];
    const SwsOp write = ops->ops[ops->num_ops - 1];
    int ret;

    /* Special fast path for in-place packed shuffle */
    ret = solve_shuffle(ops, mmsize, out);
    if (ret != AVERROR(ENOTSUP))
        return ret;

    SwsOpChain *chain = ff_sws_op_chain_alloc();
    if (!chain)
        return AVERROR(ENOMEM);

    *out = (SwsCompiledOp) {
        .priv = chain,
        .free = ff_sws_op_chain_free_cb,

        /* Use at most two full YMM regs during the widest precision section */
        .block_size = 2 * FFMIN(mmsize, 32) / ff_sws_op_list_max_size(ops),
    };

    /* 3-component reads/writes process one extra garbage word */
    if (read.rw.packed && read.rw.elems == 3)
        out->over_read = sizeof(uint32_t);
    if (write.rw.packed && write.rw.elems == 3)
        out->over_write = sizeof(uint32_t);

    static const SwsOpTable *const tables[] = {
        &ops8_m1_sse4,
        &ops8_m1_avx2,
        &ops8_m2_sse4,
        &ops8_m2_avx2,
        &ops16_m1_avx2,
        &ops16_m2_avx2,
        &ops32_avx2,
    };

    do {
        int op_block_size = out->block_size;
        SwsOp *op = &ops->ops[0];

        if (op_is_type_invariant(op)) {
            if (op->op == SWS_OP_CLEAR)
                normalize_clear(op);
            op_block_size *= ff_sws_pixel_type_size(op->type);
            op->type = SWS_PIXEL_U8;
        }

        ret = ff_sws_op_compile_tables(tables, FF_ARRAY_ELEMS(tables), ops,
                                       op_block_size, chain);
    } while (ret == AVERROR(EAGAIN));
    if (ret < 0) {
        ff_sws_op_chain_free(chain);
        return ret;
    }

#define ASSIGN_PROCESS_FUNC(NAME)                               \
    do {                                                        \
        SWS_DECL_FUNC(NAME);                                    \
        void NAME##_return(void);                               \
        ret = ff_sws_op_chain_append(chain, NAME##_return,      \
                                     NULL, &(SwsOpPriv) {0});   \
        out->func = NAME;                                       \
    } while (0)

    const int read_planes  = read.rw.packed  ? 1 : read.rw.elems;
    const int write_planes = write.rw.packed ? 1 : write.rw.elems;
    switch (FFMAX(read_planes, write_planes)) {
    case 1: ASSIGN_PROCESS_FUNC(ff_sws_process1_x86); break;
    case 2: ASSIGN_PROCESS_FUNC(ff_sws_process2_x86); break;
    case 3: ASSIGN_PROCESS_FUNC(ff_sws_process3_x86); break;
    case 4: ASSIGN_PROCESS_FUNC(ff_sws_process4_x86); break;
    }

    if (ret < 0) {
        ff_sws_op_chain_free(chain);
        return ret;
    }

    out->cpu_flags = chain->cpu_flags;
    return 0;
}

const SwsOpBackend backend_x86 = {
    .name       = "x86",
    .compile    = compile,
};
