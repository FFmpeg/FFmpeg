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

static int setup_rw(const SwsImplParams *params, SwsImplResult *out)
{
    const SwsOp *op = params->op;

    /* 3-component reads/writes process one extra garbage word */
    if (op->rw.packed && op->rw.elems == 3) {
        switch (op->op) {
        case SWS_OP_READ:  out->over_read  = sizeof(uint32_t); break;
        case SWS_OP_WRITE: out->over_write = sizeof(uint32_t); break;
        }
    }

    return 0;
}

#define DECL_RW(EXT, TYPE, NAME, OP, ELEMS, PACKED, FRAC)                       \
    DECL_ASM(TYPE, NAME##ELEMS##EXT,                                            \
        .op = SWS_OP_##OP,                                                      \
        .rw = { .elems = ELEMS, .packed = PACKED, .frac = FRAC },               \
        .setup = setup_rw,                                                      \
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

static int setup_swap_bytes(const SwsImplParams *params, SwsImplResult *out)
{
    const int mask = ff_sws_pixel_type_size(params->op->type) - 1;
    for (int i = 0; i < 16; i++)
        out->priv.u8[i] = (i & ~mask) | (mask - (i & mask));
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
        .clear.mask = SWS_COMP(IDX),                                            \
        .clear.value[IDX] = { -1, 1 },                                          \
    );                                                                          \

#define DECL_CLEAR_ZERO(EXT, IDX)                                               \
    DECL_ASM(U8, clear_zero##IDX##EXT,                                          \
        .op = SWS_OP_CLEAR,                                                     \
        .clear.mask = SWS_COMP(IDX),                                            \
        .clear.value[IDX] = { 0, 1 },                                           \
    );

static int setup_clear(const SwsImplParams *params, SwsImplResult *out)
{
    const SwsOp *op = params->op;
    for (int i = 0; i < 4; i++)
        out->priv.u32[i] = (uint32_t) op->clear.value[i].num;
    return 0;
}

#define DECL_CLEAR(EXT, X, Y, Z, W)                                             \
    DECL_PATTERN(U8, clear##EXT, X, Y, Z, W,                                    \
        .op = SWS_OP_CLEAR,                                                     \
        .setup = setup_clear,                                                   \
        .clear.mask = SWS_COMP_MASK(!X, !Y, !Z, !W),                            \
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

static int setup_shift(const SwsImplParams *params, SwsImplResult *out)
{
    out->priv.u16[0] = params->op->shift.amount;
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
        .setup = ff_sws_setup_clamp,                                            \
        .flexible = true,                                                       \
    );                                                                          \
                                                                                \
    DECL_COMMON_PATTERNS(F32, max##EXT,                                         \
        .op = SWS_OP_MAX,                                                       \
        .setup = ff_sws_setup_clamp,                                            \
        .flexible = true,                                                       \
    );

#define DECL_SCALE(EXT)                                                         \
    DECL_COMMON_PATTERNS(F32, scale##EXT,                                       \
        .op = SWS_OP_SCALE,                                                     \
        .setup = ff_sws_setup_scale,                                            \
        .flexible = true,                                                       \
    );

#define DECL_EXPAND_BITS(EXT, BITS)                                             \
    DECL_ASM(U##BITS, expand_bits##BITS##EXT,                                   \
        .op = SWS_OP_SCALE,                                                     \
        .scale = { .num = ((1 << (BITS)) - 1), .den = 1 },                      \
        .unused = { false, true, true, true },                                  \
    );

static int setup_dither(const SwsImplParams *params, SwsImplResult *out)
{
    const SwsOp *op = params->op;
    /* 1x1 matrix / single constant */
    if (!op->dither.size_log2) {
        const AVRational k = op->dither.matrix[0];
        out->priv.f32[0] = (float) k.num / k.den;
        return 0;
    }

    const int size = 1 << op->dither.size_log2;
    const int8_t *off = op->dither.y_offset;
    int max_offset = 0;
    for (int i = 0; i < 4; i++) {
        if (off[i] >= 0)
            max_offset = FFMAX(max_offset, off[i] & (size - 1));
    }

    /* Allocate extra rows to allow over-reading for row offsets. Note that
     * max_offset is currently never larger than 5, so the extra space needed
     * for this over-allocation is bounded by 5 * size * sizeof(float),
     * typically 320 bytes for a 16x16 dither matrix. */
    const int stride = size * sizeof(float);
    const int num_rows = size + max_offset;
    float *matrix = out->priv.ptr = av_mallocz(num_rows * stride);
    if (!matrix)
        return AVERROR(ENOMEM);
    out->free = ff_op_priv_free;

    for (int i = 0; i < size * size; i++)
        matrix[i] = (float) op->dither.matrix[i].num / op->dither.matrix[i].den;

    memcpy(&matrix[size * size], matrix, max_offset * stride);

    /* Store relative pointer offset to each row inside extra space */
    static_assert(sizeof(out->priv.ptr) <= sizeof(int16_t[4]),
                  ">8 byte pointers not supported");
    assert(max_offset * stride <= INT16_MAX);
    int16_t *off_out = &out->priv.i16[4];
    for (int i = 0; i < 4; i++)
        off_out[i] = off[i] >= 0 ? (off[i] & (size - 1)) * stride : -1;

    return 0;
}

#define DECL_DITHER(DECL_MACRO, EXT, SIZE)                                      \
    DECL_MACRO(F32, dither##SIZE##EXT,                                          \
        .op    = SWS_OP_DITHER,                                                 \
        .setup = setup_dither,                                                  \
        .dither_size = SIZE,                                                    \
    );

static int setup_linear(const SwsImplParams *params, SwsImplResult *out)
{
    const SwsOp *op = params->op;

    float *matrix = out->priv.ptr = av_mallocz(sizeof(float[4][5]));
    if (!matrix)
        return AVERROR(ENOMEM);
    out->free = ff_op_priv_free;

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
        .linear_mask = (MASK),                                                  \
    );

static bool check_filter_fma(const SwsImplParams *params)
{
    const SwsOp *op = params->op;
    SwsContext *ctx = params->ctx;
    if (!(ctx->flags & SWS_BITEXACT))
        return true;

    if (!ff_sws_pixel_type_is_int(op->type))
        return false;

    /* Check if maximum/minimum partial sum fits losslessly inside float */
    AVRational max_range = {   1 << 24,  1 };
    AVRational min_range = { -(1 << 24), 1 };
    const AVRational scale = Q(SWS_FILTER_SCALE);

    for (int i = 0; i < op->rw.elems; i++) {
        const AVRational min = av_mul_q(op->comps.min[i], scale);
        const AVRational max = av_mul_q(op->comps.max[i], scale);
        if (av_cmp_q(min, min_range) < 0 || av_cmp_q(max_range, max) < 0)
            return false;
    }

    return true;
}

static int setup_filter_v(const SwsImplParams *params, SwsImplResult *out)
{
    const SwsFilterWeights *filter = params->op->rw.kernel;
    static_assert(sizeof(out->priv.ptr) <= sizeof(int32_t[2]),
                  ">8 byte pointers not supported");

    /* Pre-convert weights to float */
    float *weights = av_calloc(filter->num_weights, sizeof(float));
    if (!weights)
        return AVERROR(ENOMEM);

    for (int i = 0; i < filter->num_weights; i++)
        weights[i] = (float) filter->weights[i] / SWS_FILTER_SCALE;

    out->priv.ptr = weights;
    out->priv.uptr[1] = filter->filter_size;
    out->free = ff_op_priv_free;
    return 0;
}

static int hscale_sizeof_weight(const SwsOp *op)
{
    switch (op->type) {
    case SWS_PIXEL_U8:  return sizeof(int16_t);
    case SWS_PIXEL_U16: return sizeof(int16_t);
    case SWS_PIXEL_F32: return sizeof(float);
    default:            return 0;
    }
}

static int setup_filter_h(const SwsImplParams *params, SwsImplResult *out)
{
    const SwsOp *op = params->op;
    const SwsFilterWeights *filter = op->rw.kernel;

    /**
     * `vpgatherdd` gathers 32 bits at a time; so if we're filtering a smaller
     * size, we need to gather 2/4 taps simultaneously and unroll the inner
     * loop over several packed samples.
     */
    const int pixel_size = ff_sws_pixel_type_size(op->type);
    const int taps_align = sizeof(int32_t) / pixel_size;
    const int filter_size = filter->filter_size;
    const int block_size = params->table->block_size;
    const size_t aligned_size = FFALIGN(filter_size, taps_align);
    const size_t line_size = FFALIGN(filter->dst_size, block_size);
    av_assert1(FFALIGN(line_size, taps_align) == line_size);
    if (aligned_size > INT_MAX)
        return AVERROR(EINVAL);

    union {
        void *ptr;
        int16_t *i16;
        float *f32;
    } weights;

    const int sizeof_weight = hscale_sizeof_weight(op);
    weights.ptr = av_calloc(line_size, sizeof_weight * aligned_size);
    if (!weights.ptr)
        return AVERROR(ENOMEM);

    /**
     * Transpose filter weights to group (aligned) taps by block
     */
    const int mmsize = block_size * 2;
    const int gather_size = mmsize / sizeof(int32_t); /* pixels per vpgatherdd */
    for (size_t x = 0; x < line_size; x += block_size) {
        const int elems = FFMIN(block_size, filter->dst_size - x);
        for (int j = 0; j < filter_size; j++) {
            const int jb = j & ~(taps_align - 1);
            const int ji = j - jb;
            const size_t idx_base = x * aligned_size + jb * block_size + ji;
            for (int i = 0; i < elems; i++) {
                const int w = filter->weights[(x + i) * filter_size + j];
                size_t idx = idx_base;
                if (op->type == SWS_PIXEL_U8) {
                    /* Interleave the pixels within each lane, i.e.:
                     *  [a0 a1 a2 a3 | b0 b1 b2 b3 ] pixels 0-1, taps 0-3 (lane 0)
                     *  [e0 e1 e2 e3 | f0 f1 f2 f3 ] pixels 4-5, taps 0-3 (lane 1)
                     *  [c0 c1 c2 c3 | d0 d1 d2 d3 ] pixels 2-3, taps 0-3 (lane 0)
                     *  [g0 g1 g2 g3 | h0 h1 h2 h3 ] pixels 6-7, taps 0-3 (lane 1)
                     *  [i0 i1 i2 i3 | j0 j1 j2 j3 ] pixels 8-9, taps 0-3 (lane 0)
                     *  ...
                     *  [o0 o1 o2 o3 | p0 p1 p2 p3 ] pixels 14-15, taps 0-3 (lane 1)
                     *  (repeat for taps 4-7, etc.)
                     */
                    const int gather_base = i & ~(gather_size - 1);
                    const int gather_pos  = i - gather_base;
                    const int lane_idx    = gather_pos >> 2;
                    const int pos_in_lane = gather_pos & 3;
                    idx += gather_base * 4 /* which gather (m0 or m1) */
                         + (pos_in_lane >> 1) * (mmsize / 2) /* lo/hi unpack */
                         + lane_idx * 8 /* 8 ints per lane */
                         + (pos_in_lane & 1) * 4; /* 4 taps per pair */
                } else {
                    idx += i * taps_align;
                }

                switch (op->type) {
                case SWS_PIXEL_U8:  weights.i16[idx] = w; break;
                case SWS_PIXEL_U16: weights.i16[idx] = w; break;
                case SWS_PIXEL_F32: weights.f32[idx] = w; break;
                }
            }
        }
    }

    out->priv.ptr = weights.ptr;
    out->priv.uptr[1] = aligned_size;
    out->free = ff_op_priv_free;
    out->over_read = (aligned_size - filter_size) * pixel_size;
    return 0;
}

static bool check_filter_4x4_h(const SwsImplParams *params)
{
    SwsContext *ctx = params->ctx;
    const SwsOp *op = params->op;
    if ((ctx->flags & SWS_BITEXACT) && op->type == SWS_PIXEL_F32)
        return false; /* different accumulation order due to 4x4 transpose */

    const int cpu_flags = av_get_cpu_flags();
    if (cpu_flags & AV_CPU_FLAG_SLOW_GATHER)
        return true; /* always prefer over gathers if gathers are slow */

    /**
     * Otherwise, prefer it above a certain filter size. Empirically, this
     * kernel seems to be faster whenever the reference/gather kernel crosses
     * a breakpoint for the number of gathers needed, but this filter doesn't.
     *
     * Tested on a Lunar Lake (Intel Core Ultra 7 258V) system.
     */
    const SwsFilterWeights *filter = op->rw.kernel;
    return op->type == SWS_PIXEL_U8  && filter->filter_size > 12 ||
           op->type == SWS_PIXEL_U16 && filter->filter_size > 4  ||
           op->type == SWS_PIXEL_F32 && filter->filter_size > 1;
}

static int setup_filter_4x4_h(const SwsImplParams *params, SwsImplResult *out)
{
    const SwsOp *op = params->op;
    const SwsFilterWeights *filter = op->rw.kernel;
    const int pixel_size = ff_sws_pixel_type_size(op->type);
    const int sizeof_weights = hscale_sizeof_weight(op);
    const int block_size = params->table->block_size;
    const int taps_align = 16 / sizeof_weights; /* taps per iteration (XMM) */
    const int pixels_align = 4; /* pixels per iteration */
    const int filter_size = filter->filter_size;
    const size_t aligned_size = FFALIGN(filter_size, taps_align);
    const int line_size = FFALIGN(filter->dst_size, block_size);
    av_assert1(FFALIGN(line_size, pixels_align) == line_size);

    union {
        void *ptr;
        int16_t *i16;
        float *f32;
    } weights;

    weights.ptr = av_calloc(line_size, aligned_size * sizeof_weights);
    if (!weights.ptr)
        return AVERROR(ENOMEM);

    /**
     * Desired memory layout: [w][taps][pixels_align][taps_align]
     *
     * Example with taps_align=8, pixels_align=4:
     *   [a0, a1, ... a7]  weights for pixel 0, taps 0..7
     *   [b0, b1, ... b7]  weights for pixel 1, taps 0..7
     *   [c0, c1, ... c7]  weights for pixel 2, taps 0..7
     *   [d0, d1, ... d7]  weights for pixel 3, taps 0..7
     *   [a8, a9, ... a15] weights for pixel 0, taps 8..15
     *   ...
     *   repeat for all taps, then move on to pixels 4..7, etc.
     */
    for (int x = 0; x < filter->dst_size; x++) {
        for (int j = 0; j < filter_size; j++) {
            const int xb = x & ~(pixels_align - 1);
            const int jb = j & ~(taps_align - 1);
            const int xi = x - xb, ji = j - jb;
            const int w = filter->weights[x * filter_size + j];
            const int idx = xb * aligned_size + jb * pixels_align + xi * taps_align + ji;

            switch (op->type) {
            case SWS_PIXEL_U8:  weights.i16[idx] = w; break;
            case SWS_PIXEL_U16: weights.i16[idx] = w; break;
            case SWS_PIXEL_F32: weights.f32[idx] = w; break;
            }
        }
    }

    out->priv.ptr = weights.ptr;
    out->priv.uptr[1] = aligned_size * sizeof_weights;
    out->free = ff_op_priv_free;
    out->over_read = (aligned_size - filter_size) * pixel_size;
    return 0;
}

#define DECL_FILTER(EXT, TYPE, DIR, NAME, ELEMS, ...)                           \
    DECL_ASM(TYPE, NAME##ELEMS##_##TYPE##EXT,                                   \
        .op = SWS_OP_READ,                                                      \
        .rw.elems = ELEMS,                                                      \
        .rw.filter = SWS_OP_FILTER_##DIR,                                       \
        __VA_ARGS__                                                             \
    );

#define DECL_FILTERS(EXT, TYPE, DIR, NAME, ...)                                 \
    DECL_FILTER(EXT, TYPE, DIR, NAME, 1, __VA_ARGS__)                           \
    DECL_FILTER(EXT, TYPE, DIR, NAME, 2, __VA_ARGS__)                           \
    DECL_FILTER(EXT, TYPE, DIR, NAME, 3, __VA_ARGS__)                           \
    DECL_FILTER(EXT, TYPE, DIR, NAME, 4, __VA_ARGS__)

#define DECL_FILTERS_GENERIC(EXT, TYPE)                                         \
    DECL_FILTERS(EXT, TYPE, V, filter_v,     .setup = setup_filter_v)           \
    DECL_FILTERS(EXT, TYPE, V, filter_fma_v, .setup = setup_filter_v,           \
                 .check = check_filter_fma)                                     \
    DECL_FILTERS(EXT, TYPE, H, filter_h,     .setup = setup_filter_h)           \
    DECL_FILTERS(EXT, TYPE, H, filter_4x4_h, .setup = setup_filter_4x4_h,       \
                 .check = check_filter_4x4_h)

#define REF_FILTERS(NAME, SUFFIX)                                               \
    &op_##NAME##1##SUFFIX,                                                      \
    &op_##NAME##2##SUFFIX,                                                      \
    &op_##NAME##3##SUFFIX,                                                      \
    &op_##NAME##4##SUFFIX

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
    DECL_EXPAND_BITS(EXT, 8)                                                    \
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
        &op_expand_bits8##EXT,                                                  \
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
    DECL_EXPAND_BITS(EXT, 16)                                                   \
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
        &op_expand_bits16##EXT,                                                 \
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
    DECL_DITHER(DECL_COMMON_PATTERNS, EXT, 0)                                   \
    DECL_DITHER(DECL_ASM, EXT, 1)                                               \
    DECL_DITHER(DECL_ASM, EXT, 2)                                               \
    DECL_DITHER(DECL_ASM, EXT, 3)                                               \
    DECL_DITHER(DECL_ASM, EXT, 4)                                               \
    DECL_DITHER(DECL_ASM, EXT, 5)                                               \
    DECL_DITHER(DECL_ASM, EXT, 6)                                               \
    DECL_DITHER(DECL_ASM, EXT, 7)                                               \
    DECL_DITHER(DECL_ASM, EXT, 8)                                               \
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
    DECL_FILTERS_GENERIC(EXT,  U8)                                              \
    DECL_FILTERS_GENERIC(EXT, U16)                                              \
    DECL_FILTERS_GENERIC(EXT, F32)                                              \
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
        &op_dither1##EXT,                                                       \
        &op_dither2##EXT,                                                       \
        &op_dither3##EXT,                                                       \
        &op_dither4##EXT,                                                       \
        &op_dither5##EXT,                                                       \
        &op_dither6##EXT,                                                       \
        &op_dither7##EXT,                                                       \
        &op_dither8##EXT,                                                       \
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
        REF_FILTERS(filter_fma_v, _U8##EXT),                                    \
        REF_FILTERS(filter_fma_v, _U16##EXT),                                   \
        REF_FILTERS(filter_fma_v, _F32##EXT),                                   \
        REF_FILTERS(filter_4x4_h, _U8##EXT),                                    \
        REF_FILTERS(filter_4x4_h, _U16##EXT),                                   \
        REF_FILTERS(filter_4x4_h, _F32##EXT),                                   \
        REF_FILTERS(filter_v, _U8##EXT),                                        \
        REF_FILTERS(filter_v, _U16##EXT),                                       \
        REF_FILTERS(filter_v, _F32##EXT),                                       \
        REF_FILTERS(filter_h, _U8##EXT),                                        \
        REF_FILTERS(filter_h, _U16##EXT),                                       \
        REF_FILTERS(filter_h, _F32##EXT),                                       \
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

static const SwsOpTable *const tables[] = {
    &ops8_m1_sse4,
    &ops8_m1_avx2,
    &ops8_m2_sse4,
    &ops8_m2_avx2,
    &ops16_m1_avx2,
    &ops16_m2_avx2,
    &ops32_avx2,
};

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
        return !(op->rw.elems > 1 && op->rw.packed) && !op->rw.frac && !op->rw.filter;
    case SWS_OP_SWIZZLE:
    case SWS_OP_CLEAR:
        return true;
    }

    return false;
}

static int movsize(const int bytes, const int mmsize)
{
    return bytes <= 4 ? 4 : /* movd */
           bytes <= 8 ? 8 : /* movq */
           mmsize;          /* movu */
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

    *out = (SwsCompiledOp) {
        .priv        = av_memdup(shuffle, sizeof(shuffle)),
        .free        = av_free,
        .slice_align = 1,
        .block_size  = pixels * num_lanes,
        .over_read   = movsize(in_total,  mmsize) - in_total,
        .over_write  = movsize(out_total, mmsize) - out_total,
        .cpu_flags   = mmsize > 32 ? AV_CPU_FLAG_AVX512 :
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
    ASSIGN_SHUFFLE_FUNC(15,  5, sse4);
    ASSIGN_SHUFFLE_FUNC(15, 15, sse4);
    ASSIGN_SHUFFLE_FUNC(12, 16, sse4);
    ASSIGN_SHUFFLE_FUNC( 6, 12, sse4);
    ASSIGN_SHUFFLE_FUNC(16,  4, sse4);
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
    SwsImplResult res;
    union {
        uint32_t u32;
        int i;
    } c;

    ff_sws_setup_clear(&(const SwsImplParams) { .op = op }, &res);

    for (int i = 0; i < 4; i++) {
        if (!SWS_COMP_TEST(op->clear.mask, i))
            continue;
        switch (ff_sws_pixel_type_size(op->type)) {
        case 1: c.u32 = 0x1010101U * res.priv.u8[i]; break;
        case 2: c.u32 = (uint32_t) res.priv.u16[i] << 16 | res.priv.u16[i]; break;
        case 4: c.u32 = res.priv.u32[i]; break;
        }

        op->clear.value[i].num = c.i;
        op->clear.value[i].den = 1;
    }
}

static int compile(SwsContext *ctx, SwsOpList *ops, SwsCompiledOp *out)
{
    int ret;
    const int cpu_flags = av_get_cpu_flags();
    const int mmsize = get_mmsize(cpu_flags);
    if (mmsize < 0)
        return mmsize;

    /* Special fast path for in-place packed shuffle */
    ret = solve_shuffle(ops, mmsize, out);
    if (ret != AVERROR(ENOTSUP))
        return ret;

    SwsOpChain *chain = ff_sws_op_chain_alloc();
    if (!chain)
        return AVERROR(ENOMEM);

    *out = (SwsCompiledOp) {
        .priv = chain,
        .slice_align = 1,
        .free = ff_sws_op_chain_free_cb,

        /* Use at most two full YMM regs during the widest precision section */
        .block_size = 2 * FFMIN(mmsize, 32) / ff_sws_op_list_max_size(ops),
    };

    for (int i = 0; i < ops->num_ops; i++) {
        int op_block_size = out->block_size;
        SwsOp *op = &ops->ops[i];

        if (op_is_type_invariant(op)) {
            if (op->op == SWS_OP_CLEAR)
                normalize_clear(op);
            op_block_size *= ff_sws_pixel_type_size(op->type);
            op->type = SWS_PIXEL_U8;
        }

        ret = ff_sws_op_compile_tables(ctx, tables, FF_ARRAY_ELEMS(tables),
                                       ops, i, op_block_size, chain);
        if (ret < 0) {
            av_log(ctx, AV_LOG_TRACE, "Failed to compile op %d\n", i);
            ff_sws_op_chain_free(chain);
            return ret;
        }
    }

#define ASSIGN_PROCESS_FUNC(NAME)                               \
    do {                                                        \
        SWS_DECL_FUNC(NAME);                                    \
        out->func = NAME;                                       \
    } while (0)

    const SwsOp *read      = ff_sws_op_list_input(ops);
    const SwsOp *write     = ff_sws_op_list_output(ops);
    const int read_planes  = read ? (read->rw.packed ? 1 : read->rw.elems) : 0;
    const int write_planes = write->rw.packed ? 1 : write->rw.elems;
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

    out->cpu_flags  = chain->cpu_flags;
    out->over_read  = chain->over_read;
    out->over_write = chain->over_write;
    return 0;
}

const SwsOpBackend backend_x86 = {
    .name       = "x86",
    .compile    = compile,
    .hw_format  = AV_PIX_FMT_NONE,
};
