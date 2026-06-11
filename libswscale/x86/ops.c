/**
 * Copyright (C) 2025-2026 Niklas Haas
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
#include "libavutil/x86/cpu.h"

#include "../ops_chain.h"
#include "../uops.h"
#include "../uops_macros.h"

static int setup_rw_packed(const SwsImplParams *params, SwsImplResult *out)
{
    const SwsUOp *uop = params->uop;

    /* 3-component packed reads/writes process one extra garbage word */
    if (uop->mask == SWS_COMP_ELEMS(3)) {
        switch (uop->uop) {
        case SWS_UOP_READ_PACKED:  out->over_read[0]  = sizeof(uint32_t); break;
        case SWS_UOP_WRITE_PACKED: out->over_write[0] = sizeof(uint32_t); break;
        }
    }

    return 0;
}

static int setup_filter_v(const SwsImplParams *params, SwsImplResult *out)
{
    const SwsFilterWeights *filter = params->uop->data.kernel;
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

static int hscale_sizeof_weight(const SwsUOp *uop)
{
    switch (uop->type) {
    case SWS_PIXEL_U8:  return sizeof(int16_t);
    case SWS_PIXEL_U16: return sizeof(int16_t);
    case SWS_PIXEL_F32: return sizeof(float);
    default:            return 0;
    }
}

static int setup_filter_h(const SwsImplParams *params, SwsImplResult *out)
{
    const SwsUOp *uop = params->uop;
    const SwsFilterWeights *filter = uop->data.kernel;

    /**
     * `vpgatherdd` gathers 32 bits at a time; so if we're filtering a smaller
     * size, we need to gather 2/4 taps simultaneously and unroll the inner
     * loop over several packed samples.
     */
    const int pixel_size = ff_sws_pixel_type_size(uop->type);
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

    const int sizeof_weight = hscale_sizeof_weight(uop);
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
                if (uop->type == SWS_PIXEL_U8) {
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

                switch (uop->type) {
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

    for (int i = 0; i < 4; i++) {
        if (uop->mask & SWS_COMP(i))
            out->over_read[i] = (aligned_size - filter_size) * pixel_size;
    }
    return 0;
}

static bool check_filter_h_4x4(const SwsImplParams *params)
{
    SwsContext *ctx = params->ctx;
    const SwsUOp *uop = params->uop;
    if ((ctx->flags & SWS_BITEXACT) && uop->type == SWS_PIXEL_F32)
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
    const SwsFilterWeights *filter = uop->data.kernel;
    return uop->type == SWS_PIXEL_U8  && filter->filter_size > 12 ||
           uop->type == SWS_PIXEL_U16 && filter->filter_size > 4  ||
           uop->type == SWS_PIXEL_F32 && filter->filter_size > 1;
}

static int setup_filter_h_4x4(const SwsImplParams *params, SwsImplResult *out)
{
    const SwsUOp *uop = params->uop;
    const SwsFilterWeights *filter = uop->data.kernel;
    const int pixel_size = ff_sws_pixel_type_size(uop->type);
    const int sizeof_weights = hscale_sizeof_weight(uop);
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

            switch (uop->type) {
            case SWS_PIXEL_U8:  weights.i16[idx] = w; break;
            case SWS_PIXEL_U16: weights.i16[idx] = w; break;
            case SWS_PIXEL_F32: weights.f32[idx] = w; break;
            }
        }
    }

    out->priv.ptr = weights.ptr;
    out->priv.uptr[1] = aligned_size * sizeof_weights;
    out->free = ff_op_priv_free;

    for (int i = 0; i < 4; i++) {
        if (uop->mask & SWS_COMP(i))
            out->over_read[i] = (aligned_size - filter_size) * pixel_size;
    }
    return 0;
}

static int setup_scale(const SwsImplParams *params, SwsImplResult *out)
{
    const SwsUOp *uop = params->uop;
    switch (uop->type) {
    case SWS_PIXEL_U8:  out->priv.u16[0] = uop->data.scalar.u8;  break; /* for pmullw */
    case SWS_PIXEL_U16: out->priv.u16[0] = uop->data.scalar.u16; break;
    case SWS_PIXEL_U32: out->priv.u32[0] = uop->data.scalar.u32; break;
    case SWS_PIXEL_F32: out->priv.f32[0] = uop->data.scalar.f32; break;
    default: return AVERROR(EINVAL);
    }

    return 0;
}

static int setup_clear(const SwsImplParams *params, SwsImplResult *out)
{
    const SwsUOp *uop = params->uop;
    for (int i = 0; i < 4; i++)
        out->priv.u32[i] = uop->data.vec4[i].u32;
    return 0;
}

static int setup_dither(const SwsImplParams *params, SwsImplResult *out)
{
    out->priv.ptr = av_refstruct_ref(params->uop->data.ptr);
    out->free = ff_op_priv_unref;
    return 0;
}

static int setup_linear(const SwsImplParams *params, SwsImplResult *out)
{
    const SwsUOp *uop = params->uop;
    out->priv.ptr = av_memdup(uop->data.mat4, sizeof(uop->data.mat4));
    out->free = ff_op_priv_free;
    return out->priv.ptr ? 0 : AVERROR(ENOMEM);
}

static bool uop_is_type_invariant(const SwsUOpType uop)
{
    switch (uop) {
    case SWS_UOP_READ_PLANAR:
    case SWS_UOP_WRITE_PLANAR:
    case SWS_UOP_CLEAR:
        return true;
    default:
        return false;
    }
}

#define REF_ENTRY(EXT, NAME, ...) &op_##NAME##EXT,
#define DECL_ENTRY(EXT, CHECK, SETUP, NAME, ...)                                \
    void ff_##NAME##EXT(void);                                                  \
    static const SwsOpEntry op_##NAME##EXT = {                                  \
        .func = (SwsFuncPtr) ff_##NAME##EXT,                                    \
        .check = CHECK,                                                         \
        .setup = SETUP,                                                         \
        __VA_ARGS__,                                                            \
    };

/* Define all UOPs except conversion ops and type-invariant ops */
#define DECL_OPS_COMMON(EXT, TYPE)                                              \
SWS_FOR_STRUCT(TYPE, READ_PACKED,     DECL_ENTRY, EXT, NULL, setup_rw_packed)   \
SWS_FOR_STRUCT(TYPE, READ_NIBBLE,     DECL_ENTRY, EXT, NULL, NULL)              \
SWS_FOR_STRUCT(TYPE, READ_BIT,        DECL_ENTRY, EXT, NULL, NULL)              \
SWS_FOR_STRUCT(TYPE, WRITE_PACKED,    DECL_ENTRY, EXT, NULL, setup_rw_packed)   \
SWS_FOR_STRUCT(TYPE, WRITE_NIBBLE,    DECL_ENTRY, EXT, NULL, NULL)              \
SWS_FOR_STRUCT(TYPE, WRITE_BIT,       DECL_ENTRY, EXT, NULL, NULL)              \
SWS_FOR_STRUCT(TYPE, SWAP_BYTES,      DECL_ENTRY, EXT, NULL, NULL)              \
SWS_FOR_STRUCT(TYPE, EXPAND_BIT,      DECL_ENTRY, EXT, NULL, NULL)              \
SWS_FOR_STRUCT(TYPE, MOVE,            DECL_ENTRY, EXT, NULL, NULL)              \
SWS_FOR_STRUCT(TYPE, SCALE,           DECL_ENTRY, EXT, NULL, setup_scale)       \
SWS_FOR_STRUCT(TYPE, ADD,             DECL_ENTRY, EXT, NULL, ff_sws_setup_vec4) \
SWS_FOR_STRUCT(TYPE, MIN,             DECL_ENTRY, EXT, NULL, ff_sws_setup_vec4) \
SWS_FOR_STRUCT(TYPE, MAX,             DECL_ENTRY, EXT, NULL, ff_sws_setup_vec4) \
SWS_FOR_STRUCT(TYPE, UNPACK,          DECL_ENTRY, EXT, NULL, NULL)              \
SWS_FOR_STRUCT(TYPE, PACK,            DECL_ENTRY, EXT, NULL, NULL)              \
SWS_FOR_STRUCT(TYPE, LSHIFT,          DECL_ENTRY, EXT, NULL, NULL)              \
SWS_FOR_STRUCT(TYPE, RSHIFT,          DECL_ENTRY, EXT, NULL, NULL)              \
SWS_FOR_STRUCT(TYPE, LINEAR_FMA,      DECL_ENTRY, EXT, NULL, setup_linear)      \
SWS_FOR_STRUCT(TYPE, DITHER,          DECL_ENTRY, EXT, NULL, setup_dither)      \
/* end of macro */

#define REF_OPS_COMMON(EXT, TYPE)                                               \
    SWS_FOR(TYPE, READ_PACKED,    REF_ENTRY, EXT)                               \
    SWS_FOR(TYPE, READ_NIBBLE,    REF_ENTRY, EXT)                               \
    SWS_FOR(TYPE, READ_BIT,       REF_ENTRY, EXT)                               \
    SWS_FOR(TYPE, WRITE_PACKED,   REF_ENTRY, EXT)                               \
    SWS_FOR(TYPE, WRITE_NIBBLE,   REF_ENTRY, EXT)                               \
    SWS_FOR(TYPE, WRITE_BIT,      REF_ENTRY, EXT)                               \
    SWS_FOR(TYPE, SWAP_BYTES,     REF_ENTRY, EXT)                               \
    SWS_FOR(TYPE, EXPAND_BIT,     REF_ENTRY, EXT)                               \
    SWS_FOR(TYPE, MOVE,           REF_ENTRY, EXT)                               \
    SWS_FOR(TYPE, SCALE,          REF_ENTRY, EXT)                               \
    SWS_FOR(TYPE, ADD,            REF_ENTRY, EXT)                               \
    SWS_FOR(TYPE, MIN,            REF_ENTRY, EXT)                               \
    SWS_FOR(TYPE, MAX,            REF_ENTRY, EXT)                               \
    SWS_FOR(TYPE, UNPACK,         REF_ENTRY, EXT)                               \
    SWS_FOR(TYPE, PACK,           REF_ENTRY, EXT)                               \
    SWS_FOR(TYPE, LSHIFT,         REF_ENTRY, EXT)                               \
    SWS_FOR(TYPE, RSHIFT,         REF_ENTRY, EXT)                               \
    SWS_FOR(TYPE, LINEAR_FMA,     REF_ENTRY, EXT)                               \
    SWS_FOR(TYPE, DITHER,         REF_ENTRY, EXT)                               \
    /* end of macro */

#define DECL_TABLE_U8(EXT, SIZE, FLAG)                                          \
DECL_OPS_COMMON(EXT, U8)                                                        \
SWS_FOR_STRUCT(U8, READ_PLANAR,     DECL_ENTRY, EXT, NULL, NULL)                \
SWS_FOR_STRUCT(U8, WRITE_PLANAR,    DECL_ENTRY, EXT, NULL, NULL)                \
SWS_FOR_STRUCT(U8, CLEAR,           DECL_ENTRY, EXT, NULL, setup_clear)         \
                                                                                \
static const SwsOpTable ops_u8##EXT = {                                         \
    .cpu_flags = AV_CPU_FLAG_##FLAG,                                            \
    .block_size = SIZE,                                                         \
    .entries = {                                                                \
        REF_OPS_COMMON(EXT, U8)                                                 \
        SWS_FOR(U8, READ_PLANAR,    REF_ENTRY, EXT)                             \
        SWS_FOR(U8, WRITE_PLANAR,   REF_ENTRY, EXT)                             \
        SWS_FOR(U8, CLEAR,          REF_ENTRY, EXT)                             \
        NULL                                                                    \
    },                                                                          \
};

#define DECL_TABLE_U16(EXT, SIZE, FLAG)                                         \
DECL_OPS_COMMON(EXT, U16)                                                       \
SWS_FOR_STRUCT(U8,  TO_U16, DECL_ENTRY, EXT, NULL, NULL)                        \
SWS_FOR_STRUCT(U16, TO_U8,  DECL_ENTRY, EXT, NULL, NULL)                        \
SWS_FOR_STRUCT(U8,  EXPAND_PAIR, DECL_ENTRY, EXT, NULL, NULL)                   \
                                                                                \
static const SwsOpTable ops_u16##EXT = {                                        \
    .cpu_flags = AV_CPU_FLAG_##FLAG,                                            \
    .block_size = SIZE,                                                         \
    .entries = {                                                                \
        REF_OPS_COMMON(EXT, U16)                                                \
        SWS_FOR(U8,  TO_U16, REF_ENTRY, EXT)                                    \
        SWS_FOR(U16, TO_U8,  REF_ENTRY, EXT)                                    \
        SWS_FOR(U8,  EXPAND_PAIR, REF_ENTRY, EXT)                               \
        NULL                                                                    \
    },                                                                          \
};

#define DECL_TABLE_U32(EXT, SIZE, FLAG)                                         \
DECL_OPS_COMMON(EXT, U32)                                                       \
SWS_FOR_STRUCT(U8,  TO_U32, DECL_ENTRY, EXT, NULL, NULL)                        \
SWS_FOR_STRUCT(U32, TO_U8,  DECL_ENTRY, EXT, NULL, NULL)                        \
SWS_FOR_STRUCT(U16, TO_U32, DECL_ENTRY, EXT, NULL, NULL)                        \
SWS_FOR_STRUCT(U32, TO_U16, DECL_ENTRY, EXT, NULL, NULL)                        \
SWS_FOR_STRUCT(U8,  EXPAND_QUAD, DECL_ENTRY, EXT, NULL, NULL)                   \
                                                                                \
static const SwsOpTable ops_u32##EXT = {                                        \
    .cpu_flags = AV_CPU_FLAG_##FLAG,                                            \
    .block_size = SIZE,                                                         \
    .entries = {                                                                \
        REF_OPS_COMMON(EXT, U32)                                                \
        SWS_FOR(U8,  TO_U32, REF_ENTRY, EXT)                                    \
        SWS_FOR(U32, TO_U8,  REF_ENTRY, EXT)                                    \
        SWS_FOR(U16, TO_U32, REF_ENTRY, EXT)                                    \
        SWS_FOR(U32, TO_U16, REF_ENTRY, EXT)                                    \
        SWS_FOR(U8,  EXPAND_QUAD, REF_ENTRY, EXT)                               \
        NULL                                                                    \
    },                                                                          \
};

#define DECL_TABLE_F32(EXT, SIZE, FLAG)                                         \
DECL_OPS_COMMON(EXT, F32)                                                       \
SWS_FOR_STRUCT(U8,  TO_F32, DECL_ENTRY, EXT, NULL, NULL)                        \
SWS_FOR_STRUCT(F32, TO_U8,  DECL_ENTRY, EXT, NULL, NULL)                        \
SWS_FOR_STRUCT(U16, TO_F32, DECL_ENTRY, EXT, NULL, NULL)                        \
SWS_FOR_STRUCT(F32, TO_U16, DECL_ENTRY, EXT, NULL, NULL)                        \
SWS_FOR_STRUCT(U8,  READ_PLANAR_FH, DECL_ENTRY, EXT, NULL, setup_filter_h)      \
SWS_FOR_STRUCT(U16, READ_PLANAR_FH, DECL_ENTRY, EXT, NULL, setup_filter_h)      \
SWS_FOR_STRUCT(F32, READ_PLANAR_FH, DECL_ENTRY, EXT, NULL, setup_filter_h)      \
SWS_FOR_STRUCT(U8,  READ_PLANAR_FH, DECL_ENTRY, _4x4##EXT,                      \
               check_filter_h_4x4, setup_filter_h_4x4)                          \
SWS_FOR_STRUCT(U16, READ_PLANAR_FH, DECL_ENTRY, _4x4##EXT,                      \
               check_filter_h_4x4, setup_filter_h_4x4)                          \
SWS_FOR_STRUCT(F32, READ_PLANAR_FH, DECL_ENTRY, _4x4##EXT,                      \
               check_filter_h_4x4, setup_filter_h_4x4)                          \
SWS_FOR_STRUCT(U8,  READ_PLANAR_FV, DECL_ENTRY, EXT, NULL, setup_filter_v)      \
SWS_FOR_STRUCT(U16, READ_PLANAR_FV, DECL_ENTRY, EXT, NULL, setup_filter_v)      \
SWS_FOR_STRUCT(F32, READ_PLANAR_FV, DECL_ENTRY, EXT, NULL, setup_filter_v)      \
SWS_FOR_STRUCT(U8,  READ_PLANAR_FV_FMA, DECL_ENTRY, EXT, NULL, setup_filter_v)  \
SWS_FOR_STRUCT(U16, READ_PLANAR_FV_FMA, DECL_ENTRY, EXT, NULL, setup_filter_v)  \
SWS_FOR_STRUCT(F32, READ_PLANAR_FV_FMA, DECL_ENTRY, EXT, NULL, setup_filter_v)  \
                                                                                \
static const SwsOpTable ops_f32##EXT = {                                        \
    .cpu_flags = AV_CPU_FLAG_##FLAG,                                            \
    .block_size = SIZE,                                                         \
    .entries = {                                                                \
        REF_OPS_COMMON(EXT, F32)                                                \
        SWS_FOR(U8,  TO_F32, REF_ENTRY, EXT)                                    \
        SWS_FOR(F32, TO_U8,  REF_ENTRY, EXT)                                    \
        SWS_FOR(U16, TO_F32, REF_ENTRY, EXT)                                    \
        SWS_FOR(F32, TO_U16, REF_ENTRY, EXT)                                    \
        SWS_FOR(U8,  READ_PLANAR_FH, REF_ENTRY, _4x4##EXT)                      \
        SWS_FOR(U16, READ_PLANAR_FH, REF_ENTRY, _4x4##EXT)                      \
        SWS_FOR(F32, READ_PLANAR_FH, REF_ENTRY, _4x4##EXT)                      \
        SWS_FOR(U8,  READ_PLANAR_FH, REF_ENTRY, EXT)                            \
        SWS_FOR(U16, READ_PLANAR_FH, REF_ENTRY, EXT)                            \
        SWS_FOR(F32, READ_PLANAR_FH, REF_ENTRY, EXT)                            \
        SWS_FOR(U8,  READ_PLANAR_FV, REF_ENTRY, EXT)                            \
        SWS_FOR(U16, READ_PLANAR_FV, REF_ENTRY, EXT)                            \
        SWS_FOR(F32, READ_PLANAR_FV, REF_ENTRY, EXT)                            \
        SWS_FOR(U8,  READ_PLANAR_FV_FMA, REF_ENTRY, EXT)                        \
        SWS_FOR(U16, READ_PLANAR_FV_FMA, REF_ENTRY, EXT)                        \
        SWS_FOR(F32, READ_PLANAR_FV_FMA, REF_ENTRY, EXT)                        \
        NULL                                                                    \
    },                                                                          \
};

DECL_TABLE_U8( _m1_sse4, 16, SSE4)
DECL_TABLE_U8( _m1_avx2, 32, AVX2)
DECL_TABLE_U8( _m2_sse4, 32, SSE4)
DECL_TABLE_U8( _m2_avx2, 64, AVX2)
DECL_TABLE_U16(_m1_avx2, 16, AVX2)
DECL_TABLE_U16(_m2_avx2, 32, AVX2)
DECL_TABLE_U32(_m2_avx2, 16, AVX2)
DECL_TABLE_F32(_m2_avx2, 16, AVX2)

static const SwsOpTable *const tables[] = {
    &ops_u8_m1_sse4,
    &ops_u8_m1_avx2, /* order before _m2_sse4 */
    &ops_u8_m2_sse4,
    &ops_u8_m2_avx2,
    &ops_u16_m1_avx2,
    &ops_u16_m2_avx2,
    &ops_u32_m2_avx2,
    &ops_f32_m2_avx2,
};

SWS_DECL_FUNC(ff_sws_process1_x86);
SWS_DECL_FUNC(ff_sws_process2_x86);
SWS_DECL_FUNC(ff_sws_process3_x86);
SWS_DECL_FUNC(ff_sws_process4_x86);

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

    /* We can't shuffle across lanes, so restrict the vector size to XMM
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
        .over_read   = { movsize(in_total,  mmsize) - in_total },
        .over_write  = { movsize(out_total, mmsize) - out_total },
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

/* Expand pixel value to 32-bits by repeating as necessary */
static uint32_t expand32(const SwsPixelType type, const SwsPixel value)
{
    switch (type) {
    case SWS_PIXEL_U8:  return value.u8  * 0x01010101u;
    case SWS_PIXEL_U16: return value.u16 * 0x00010001u;
    case SWS_PIXEL_U32: return value.u32;
    case SWS_PIXEL_F32: return value.u32; /* reinterpret */
    default: return 0;
    }
}

static void normalize_clear(SwsUOp *uop)
{
    for (int i = 0; i < 4; i++)
        uop->data.vec4[i].u32 = expand32(uop->type, uop->data.vec4[i]);
}

static int compile(SwsContext *ctx, const SwsOpList *ops, SwsCompiledOp *out)
{
    const int cpu_flags = av_get_cpu_flags();
    int ret, mmsize;
    if (EXTERNAL_AVX512(cpu_flags))
        mmsize = 64;
    else if (EXTERNAL_AVX2(cpu_flags))
        mmsize = 32;
    else if (EXTERNAL_SSE4(cpu_flags))
        mmsize = 16;
    else
        return AVERROR(ENOTSUP);

    /* Special fast path for in-place packed shuffle */
    ret = solve_shuffle(ops, mmsize, out);
    if (ret != AVERROR(ENOTSUP))
        return ret;

    SwsOpChain *chain = ff_sws_op_chain_alloc();
    if (!chain)
        return AVERROR(ENOMEM);

    SwsUOpList *uops = ff_sws_uop_list_alloc();
    if (!uops) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    SwsUOpFlags flags = SWS_UOP_FLAG_MOVE;
    if (EXTERNAL_FMA3(cpu_flags))
        flags |= SWS_UOP_FLAG_FMA;

    ret = ff_sws_ops_translate(ctx, ops, flags, uops);
    if (ret < 0)
        goto fail;

    *out = (SwsCompiledOp) {
        /* Use at most two full YMM regs during the widest precision section */
        .block_size  = 2 * FFMIN(mmsize, 32) / ff_sws_op_list_max_size(ops),
        .slice_align = 1,
        .free        = ff_sws_op_chain_free_cb,
        .priv        = chain,
    };

    for (int i = 0; i < uops->num_ops; i++) {
        SwsUOp *uop = &uops->ops[i];
        int op_block_size = out->block_size;

        if (uop_is_type_invariant(uop->uop)) {
            if (uop->uop == SWS_UOP_CLEAR)
                normalize_clear(uop);
            op_block_size *= ff_sws_pixel_type_size(uop->type);
            uop->type = SWS_PIXEL_U8;
        }

        ret = ff_sws_uop_lookup(ctx, tables, FF_ARRAY_ELEMS(tables), uop,
                                op_block_size, chain);
        if (ret < 0)
            goto fail;
    }

    const SwsOp *read      = ff_sws_op_list_input(ops);
    const SwsOp *write     = ff_sws_op_list_output(ops);
    const int read_planes  = read ? ff_sws_rw_op_planes(read) : 0;
    const int write_planes = ff_sws_rw_op_planes(write);
    switch (FFMAX(read_planes, write_planes)) {
    case 1: out->func = ff_sws_process1_x86; break;
    case 2: out->func = ff_sws_process2_x86; break;
    case 3: out->func = ff_sws_process3_x86; break;
    case 4: out->func = ff_sws_process4_x86; break;
    }

    if (ret < 0) {
        ff_sws_op_chain_free(chain);
        return ret;
    }

    out->cpu_flags = chain->cpu_flags;
    memcpy(out->over_read,  chain->over_read,  sizeof(out->over_read));
    memcpy(out->over_write, chain->over_write, sizeof(out->over_write));
    ff_sws_uop_list_free(&uops);
    return 0;

fail:
    ff_sws_uop_list_free(&uops);
    ff_sws_op_chain_free(chain);
    return ret;
}

const SwsOpBackend backend_x86 = {
    .name       = "x86",
    .flags      = SWS_BACKEND_X86,
    .compile    = compile,
    .hw_format  = AV_PIX_FMT_NONE,
};
