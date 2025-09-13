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
#include "libavutil/bswap.h"

#include "ops_backend.h"

#ifndef BIT_DEPTH
#  define BIT_DEPTH 8
#endif

#if BIT_DEPTH == 32
#  define PIXEL_TYPE SWS_PIXEL_U32
#  define PIXEL_MAX  0xFFFFFFFFu
#  define SWAP_BYTES av_bswap32
#  define pixel_t    uint32_t
#  define block_t    u32block_t
#  define px         u32
#elif BIT_DEPTH == 16
#  define PIXEL_TYPE SWS_PIXEL_U16
#  define PIXEL_MAX  0xFFFFu
#  define SWAP_BYTES av_bswap16
#  define pixel_t    uint16_t
#  define block_t    u16block_t
#  define px         u16
#elif BIT_DEPTH == 8
#  define PIXEL_TYPE SWS_PIXEL_U8
#  define PIXEL_MAX  0xFFu
#  define pixel_t    uint8_t
#  define block_t    u8block_t
#  define px         u8
#else
#  error Invalid BIT_DEPTH
#endif

#define IS_FLOAT  0
#define FMT_CHAR  u
#define PIXEL_MIN 0
#include "ops_tmpl_common.c"

DECL_READ(read_planar, const int elems)
{
    block_t x, y, z, w;

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        x[i] = in0[i];
        if (elems > 1)
            y[i] = in1[i];
        if (elems > 2)
            z[i] = in2[i];
        if (elems > 3)
            w[i] = in3[i];
    }

    CONTINUE(block_t, x, y, z, w);
}

DECL_READ(read_packed, const int elems)
{
    block_t x, y, z, w;

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        x[i] = in0[elems * i + 0];
        if (elems > 1)
            y[i] = in0[elems * i + 1];
        if (elems > 2)
            z[i] = in0[elems * i + 2];
        if (elems > 3)
            w[i] = in0[elems * i + 3];
    }

    CONTINUE(block_t, x, y, z, w);
}

DECL_WRITE(write_planar, const int elems)
{
    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        out0[i] = x[i];
        if (elems > 1)
            out1[i] = y[i];
        if (elems > 2)
            out2[i] = z[i];
        if (elems > 3)
            out3[i] = w[i];
    }
}

DECL_WRITE(write_packed, const int elems)
{
    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        out0[elems * i + 0] = x[i];
        if (elems > 1)
            out0[elems * i + 1] = y[i];
        if (elems > 2)
            out0[elems * i + 2] = z[i];
        if (elems > 3)
            out0[elems * i + 3] = w[i];
    }
}

#define WRAP_READ(FUNC, ELEMS, FRAC, PACKED)                                    \
DECL_IMPL_READ(FUNC##ELEMS)                                                     \
{                                                                               \
    CALL_READ(FUNC, ELEMS);                                                     \
    for (int i = 0; i < (PACKED ? 1 : ELEMS); i++)                              \
        iter->in[i] += sizeof(block_t) * (PACKED ? ELEMS : 1) >> FRAC;          \
}                                                                               \
                                                                                \
DECL_ENTRY(FUNC##ELEMS,                                                         \
    .op = SWS_OP_READ,                                                          \
    .rw = {                                                                     \
        .elems  = ELEMS,                                                        \
        .packed = PACKED,                                                       \
        .frac   = FRAC,                                                         \
    },                                                                          \
);

WRAP_READ(read_planar, 1, 0, false)
WRAP_READ(read_planar, 2, 0, false)
WRAP_READ(read_planar, 3, 0, false)
WRAP_READ(read_planar, 4, 0, false)
WRAP_READ(read_packed, 2, 0, true)
WRAP_READ(read_packed, 3, 0, true)
WRAP_READ(read_packed, 4, 0, true)

#define WRAP_WRITE(FUNC, ELEMS, FRAC, PACKED)                                   \
DECL_IMPL(FUNC##ELEMS)                                                          \
{                                                                               \
    CALL_WRITE(FUNC, ELEMS);                                                    \
    for (int i = 0; i < (PACKED ? 1 : ELEMS); i++)                              \
        iter->out[i] += sizeof(block_t) * (PACKED ? ELEMS : 1) >> FRAC;         \
}                                                                               \
                                                                                \
DECL_ENTRY(FUNC##ELEMS,                                                         \
    .op = SWS_OP_WRITE,                                                         \
    .rw = {                                                                     \
        .elems  = ELEMS,                                                        \
        .packed = PACKED,                                                       \
        .frac   = FRAC,                                                         \
    },                                                                          \
);

WRAP_WRITE(write_planar, 1, 0, false)
WRAP_WRITE(write_planar, 2, 0, false)
WRAP_WRITE(write_planar, 3, 0, false)
WRAP_WRITE(write_planar, 4, 0, false)
WRAP_WRITE(write_packed, 2, 0, true)
WRAP_WRITE(write_packed, 3, 0, true)
WRAP_WRITE(write_packed, 4, 0, true)

#if BIT_DEPTH == 8
DECL_READ(read_nibbles, const int elems)
{
    block_t x, y, z, w;

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i += 2) {
        const pixel_t val = ((const pixel_t *) in0)[i >> 1];
        x[i + 0] = val >> 4;  /* high nibble */
        x[i + 1] = val & 0xF; /* low nibble */
    }

    CONTINUE(block_t, x, y, z, w);
}

DECL_READ(read_bits, const int elems)
{
    block_t x, y, z, w;

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i += 8) {
        const pixel_t val = ((const pixel_t *) in0)[i >> 3];
        x[i + 0] = (val >> 7) & 1;
        x[i + 1] = (val >> 6) & 1;
        x[i + 2] = (val >> 5) & 1;
        x[i + 3] = (val >> 4) & 1;
        x[i + 4] = (val >> 3) & 1;
        x[i + 5] = (val >> 2) & 1;
        x[i + 6] = (val >> 1) & 1;
        x[i + 7] = (val >> 0) & 1;
    }

    CONTINUE(block_t, x, y, z, w);
}

WRAP_READ(read_nibbles, 1, 1, false)
WRAP_READ(read_bits,    1, 3, false)

DECL_WRITE(write_nibbles, const int elems)
{
    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i += 2)
        out0[i >> 1] = x[i] << 4 | x[i + 1];
}

DECL_WRITE(write_bits, const int elems)
{
    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i += 8) {
        out0[i >> 3] = x[i + 0] << 7 |
                       x[i + 1] << 6 |
                       x[i + 2] << 5 |
                       x[i + 3] << 4 |
                       x[i + 4] << 3 |
                       x[i + 5] << 2 |
                       x[i + 6] << 1 |
                       x[i + 7];
    }
}

WRAP_WRITE(write_nibbles, 1, 1, false)
WRAP_WRITE(write_bits,    1, 3, false)
#endif /* BIT_DEPTH == 8 */

#ifdef SWAP_BYTES
DECL_PATTERN(swap_bytes)
{
    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        if (X)
            x[i] = SWAP_BYTES(x[i]);
        if (Y)
            y[i] = SWAP_BYTES(y[i]);
        if (Z)
            z[i] = SWAP_BYTES(z[i]);
        if (W)
            w[i] = SWAP_BYTES(w[i]);
    }

    CONTINUE(block_t, x, y, z, w);
}

WRAP_COMMON_PATTERNS(swap_bytes, .op = SWS_OP_SWAP_BYTES);
#endif /* SWAP_BYTES */

#if BIT_DEPTH == 8
DECL_PATTERN(expand16)
{
    u16block_t x16, y16, z16, w16;

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        if (X)
            x16[i] = x[i] << 8 | x[i];
        if (Y)
            y16[i] = y[i] << 8 | y[i];
        if (Z)
            z16[i] = z[i] << 8 | z[i];
        if (W)
            w16[i] = w[i] << 8 | w[i];
    }

    CONTINUE(u16block_t, x16, y16, z16, w16);
}

WRAP_COMMON_PATTERNS(expand16,
    .op = SWS_OP_CONVERT,
    .convert.to = SWS_PIXEL_U16,
    .convert.expand = true,
);

DECL_PATTERN(expand32)
{
    u32block_t x32, y32, z32, w32;

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        x32[i] = x[i] << 24 | x[i] << 16 | x[i] << 8 | x[i];
        y32[i] = y[i] << 24 | y[i] << 16 | y[i] << 8 | y[i];
        z32[i] = z[i] << 24 | z[i] << 16 | z[i] << 8 | z[i];
        w32[i] = w[i] << 24 | w[i] << 16 | w[i] << 8 | w[i];
    }

    CONTINUE(u32block_t, x32, y32, z32, w32);
}

WRAP_COMMON_PATTERNS(expand32,
    .op = SWS_OP_CONVERT,
    .convert.to = SWS_PIXEL_U32,
    .convert.expand = true,
);
#endif

#define WRAP_PACK_UNPACK(X, Y, Z, W)                                            \
inline DECL_IMPL(pack_##X##Y##Z##W)                                             \
{                                                                               \
    SWS_LOOP                                                                    \
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {                                  \
        x[i] = x[i] << (Y+Z+W);                                                 \
        if (Y)                                                                  \
            x[i] |= y[i] << (Z+W);                                              \
        if (Z)                                                                  \
            x[i] |= z[i] << W;                                                  \
        if (W)                                                                  \
            x[i] |= w[i];                                                       \
    }                                                                           \
                                                                                \
    CONTINUE(block_t, x, y, z, w);                                              \
}                                                                               \
                                                                                \
DECL_ENTRY(pack_##X##Y##Z##W,                                                   \
    .op = SWS_OP_PACK,                                                          \
    .pack.pattern = { X, Y, Z, W },                                             \
);                                                                              \
                                                                                \
inline DECL_IMPL(unpack_##X##Y##Z##W)                                           \
{                                                                               \
    SWS_LOOP                                                                    \
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {                                  \
        const pixel_t val = x[i];                                               \
        x[i] = val >> (Y+Z+W);                                                  \
        if (Y)                                                                  \
            y[i] = (val >> (Z+W)) & ((1 << Y) - 1);                             \
        if (Z)                                                                  \
            z[i] = (val >> W) & ((1 << Z) - 1);                                 \
        if (W)                                                                  \
            w[i] = val & ((1 << W) - 1);                                        \
    }                                                                           \
                                                                                \
    CONTINUE(block_t, x, y, z, w);                                              \
}                                                                               \
                                                                                \
DECL_ENTRY(unpack_##X##Y##Z##W,                                                 \
    .op = SWS_OP_UNPACK,                                                        \
    .pack.pattern = { X, Y, Z, W },                                             \
);

WRAP_PACK_UNPACK( 3,  3,  2,  0)
WRAP_PACK_UNPACK( 2,  3,  3,  0)
WRAP_PACK_UNPACK( 1,  2,  1,  0)
WRAP_PACK_UNPACK( 5,  6,  5,  0)
WRAP_PACK_UNPACK( 5,  5,  5,  0)
WRAP_PACK_UNPACK( 4,  4,  4,  0)
WRAP_PACK_UNPACK( 2, 10, 10, 10)
WRAP_PACK_UNPACK(10, 10, 10,  2)

#if BIT_DEPTH != 8
DECL_PATTERN(lshift)
{
    const uint8_t amount = impl->priv.u8[0];

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        x[i] <<= amount;
        y[i] <<= amount;
        z[i] <<= amount;
        w[i] <<= amount;
    }

    CONTINUE(block_t, x, y, z, w);
}

DECL_PATTERN(rshift)
{
    const uint8_t amount = impl->priv.u8[0];

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        x[i] >>= amount;
        y[i] >>= amount;
        z[i] >>= amount;
        w[i] >>= amount;
    }

    CONTINUE(block_t, x, y, z, w);
}

WRAP_COMMON_PATTERNS(lshift,
    .op       = SWS_OP_LSHIFT,
    .setup    = ff_sws_setup_u8,
    .flexible = true,
);

WRAP_COMMON_PATTERNS(rshift,
    .op       = SWS_OP_RSHIFT,
    .setup    = ff_sws_setup_u8,
    .flexible = true,
);
#endif /* BIT_DEPTH != 8 */

DECL_PATTERN(convert_float)
{
    f32block_t xf, yf, zf, wf;

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        xf[i] = x[i];
        yf[i] = y[i];
        zf[i] = z[i];
        wf[i] = w[i];
    }

    CONTINUE(f32block_t, xf, yf, zf, wf);
}

WRAP_COMMON_PATTERNS(convert_float,
    .op = SWS_OP_CONVERT,
    .convert.to = SWS_PIXEL_F32,
);

/**
 * Swizzle by directly swapping the order of arguments to the continuation.
 * Note that this is only safe to do if no arguments are duplicated.
 */
#define DECL_SWIZZLE(X, Y, Z, W)                                                \
static SWS_FUNC void                                                            \
fn(swizzle_##X##Y##Z##W)(SwsOpIter *restrict iter,                              \
                         const SwsOpImpl *restrict impl,                        \
                         block_t c0, block_t c1, block_t c2, block_t c3)        \
{                                                                               \
    CONTINUE(block_t, c##X, c##Y, c##Z, c##W);                                  \
}                                                                               \
                                                                                \
DECL_ENTRY(swizzle_##X##Y##Z##W,                                                \
    .op = SWS_OP_SWIZZLE,                                                       \
    .swizzle.in = { X, Y, Z, W },                                               \
);

DECL_SWIZZLE(3, 0, 1, 2)
DECL_SWIZZLE(3, 0, 2, 1)
DECL_SWIZZLE(2, 1, 0, 3)
DECL_SWIZZLE(3, 2, 1, 0)
DECL_SWIZZLE(3, 1, 0, 2)
DECL_SWIZZLE(3, 2, 0, 1)
DECL_SWIZZLE(1, 2, 0, 3)
DECL_SWIZZLE(1, 0, 2, 3)
DECL_SWIZZLE(2, 0, 1, 3)
DECL_SWIZZLE(2, 3, 1, 0)
DECL_SWIZZLE(2, 1, 3, 0)
DECL_SWIZZLE(1, 2, 3, 0)
DECL_SWIZZLE(1, 3, 2, 0)
DECL_SWIZZLE(0, 2, 1, 3)
DECL_SWIZZLE(0, 2, 3, 1)
DECL_SWIZZLE(0, 3, 1, 2)
DECL_SWIZZLE(3, 1, 2, 0)
DECL_SWIZZLE(0, 3, 2, 1)

/* Broadcast luma -> rgb (only used for y(a) -> rgb(a)) */
#define DECL_EXPAND_LUMA(X, W, T0, T1)                                          \
static SWS_FUNC void                                                            \
fn(expand_luma_##X##W)(SwsOpIter *restrict iter,                                \
                       const SwsOpImpl *restrict impl,                          \
                       block_t c0, block_t c1,  block_t c2, block_t c3)         \
{                                                                               \
    SWS_LOOP                                                                    \
    for (int i = 0; i < SWS_BLOCK_SIZE; i++)                                    \
        T0[i] = T1[i] = c0[i];                                                  \
                                                                                \
    CONTINUE(block_t, c##X, T0, T1, c##W);                                      \
}                                                                               \
                                                                                \
DECL_ENTRY(expand_luma_##X##W,                                                  \
    .op = SWS_OP_SWIZZLE,                                                       \
    .swizzle.in = { X, 0, 0, W },                                               \
);

DECL_EXPAND_LUMA(0, 3, c1, c2)
DECL_EXPAND_LUMA(3, 0, c1, c2)
DECL_EXPAND_LUMA(1, 0, c2, c3)
DECL_EXPAND_LUMA(0, 1, c2, c3)

static const SwsOpTable fn(op_table_int) = {
    .block_size = SWS_BLOCK_SIZE,
    .entries = {
        &fn(op_read_planar1),
        &fn(op_read_planar2),
        &fn(op_read_planar3),
        &fn(op_read_planar4),
        &fn(op_read_packed2),
        &fn(op_read_packed3),
        &fn(op_read_packed4),

        &fn(op_write_planar1),
        &fn(op_write_planar2),
        &fn(op_write_planar3),
        &fn(op_write_planar4),
        &fn(op_write_packed2),
        &fn(op_write_packed3),
        &fn(op_write_packed4),

#if BIT_DEPTH == 8
        &fn(op_read_bits1),
        &fn(op_read_nibbles1),
        &fn(op_write_bits1),
        &fn(op_write_nibbles1),

        &fn(op_pack_1210),
        &fn(op_pack_2330),
        &fn(op_pack_3320),

        &fn(op_unpack_1210),
        &fn(op_unpack_2330),
        &fn(op_unpack_3320),

        REF_COMMON_PATTERNS(expand16),
        REF_COMMON_PATTERNS(expand32),
#elif BIT_DEPTH == 16
        &fn(op_pack_4440),
        &fn(op_pack_5550),
        &fn(op_pack_5650),
        &fn(op_unpack_4440),
        &fn(op_unpack_5550),
        &fn(op_unpack_5650),
#elif BIT_DEPTH == 32
        &fn(op_pack_2101010),
        &fn(op_pack_1010102),
        &fn(op_unpack_2101010),
        &fn(op_unpack_1010102),
#endif

#ifdef SWAP_BYTES
        REF_COMMON_PATTERNS(swap_bytes),
#endif

        REF_COMMON_PATTERNS(min),
        REF_COMMON_PATTERNS(max),
        REF_COMMON_PATTERNS(scale),
        REF_COMMON_PATTERNS(convert_float),

        &fn(op_clear_1110),
        &fn(op_clear_0111),
        &fn(op_clear_0011),
        &fn(op_clear_1001),
        &fn(op_clear_1100),
        &fn(op_clear_0101),
        &fn(op_clear_1010),
        &fn(op_clear_1000),
        &fn(op_clear_0100),
        &fn(op_clear_0010),

        &fn(op_swizzle_3012),
        &fn(op_swizzle_3021),
        &fn(op_swizzle_2103),
        &fn(op_swizzle_3210),
        &fn(op_swizzle_3102),
        &fn(op_swizzle_3201),
        &fn(op_swizzle_1203),
        &fn(op_swizzle_1023),
        &fn(op_swizzle_2013),
        &fn(op_swizzle_2310),
        &fn(op_swizzle_2130),
        &fn(op_swizzle_1230),
        &fn(op_swizzle_1320),
        &fn(op_swizzle_0213),
        &fn(op_swizzle_0231),
        &fn(op_swizzle_0312),
        &fn(op_swizzle_3120),
        &fn(op_swizzle_0321),

        &fn(op_expand_luma_03),
        &fn(op_expand_luma_30),
        &fn(op_expand_luma_10),
        &fn(op_expand_luma_01),

#if BIT_DEPTH != 8
        REF_COMMON_PATTERNS(lshift),
        REF_COMMON_PATTERNS(rshift),
        REF_COMMON_PATTERNS(convert_uint8),
#endif /* BIT_DEPTH != 8 */

#if BIT_DEPTH != 16
        REF_COMMON_PATTERNS(convert_uint16),
#endif
#if BIT_DEPTH != 32
        REF_COMMON_PATTERNS(convert_uint32),
#endif

        NULL
    },
};

#undef PIXEL_TYPE
#undef PIXEL_MAX
#undef PIXEL_MIN
#undef SWAP_BYTES
#undef pixel_t
#undef block_t
#undef px

#undef FMT_CHAR
#undef IS_FLOAT
