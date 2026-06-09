/**
 * Copyright (C) 2026 Niklas Haas
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

#include <libavutil/bswap.h>

#include "uops_tmpl.h"

#ifndef BIT_DEPTH
#  define BIT_DEPTH 8
#endif

#if IS_FLOAT && BIT_DEPTH == 32
#  define PIXEL_TYPE SWS_PIXEL_F32
#  define pixel_t    float
#  define inter_t    float
#  define PX         F32
#  define px         f32
#elif BIT_DEPTH == 32
#  define PIXEL_MAX  0xFFFFFFFFu
#  define PIXEL_SWAP av_bswap32
#  define pixel_t    uint32_t
#  define inter_t    int64_t
#  define PX         U32
#  define px         u32
#elif BIT_DEPTH == 16
#  define PIXEL_MAX  0xFFFFu
#  define PIXEL_SWAP av_bswap16
#  define pixel_t    uint16_t
#  define inter_t    int64_t
#  define PX         U16
#  define px         u16
#elif BIT_DEPTH == 8
#  define PIXEL_MAX  0xFFu
#  define pixel_t    uint8_t
#  define inter_t    int32_t
#  define PX         U8
#  define px         u8
#else
#  error Invalid BIT_DEPTH
#endif

/*********************************
 * Generic read/write operations *
 *********************************/

DECL_READ(read_planar, const SwsCompMask mask)
{
    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        if (X) x[i] = in0[i];
        if (Y) y[i] = in1[i];
        if (Z) z[i] = in2[i];
        if (W) w[i] = in3[i];
    }

    if (X) iter->in[0] += SIZEOF_BLOCK;
    if (Y) iter->in[1] += SIZEOF_BLOCK;
    if (Z) iter->in[2] += SIZEOF_BLOCK;
    if (W) iter->in[3] += SIZEOF_BLOCK;

    CONTINUE(x, y, z, w);
}

DECL_READ(read_packed, const SwsCompMask mask)
{
    const int elems = W ? 4 : Z ? 3 : Y ? 2 : 1;

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        if (X) x[i] = in0[elems * i + 0];
        if (Y) y[i] = in0[elems * i + 1];
        if (Z) z[i] = in0[elems * i + 2];
        if (W) w[i] = in0[elems * i + 3];
    }

    iter->in[0] += SIZEOF_BLOCK * elems;
    CONTINUE(x, y, z, w);
}

DECL_WRITE(write_planar, const SwsCompMask mask)
{
    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        if (X) out0[i] = x[i];
        if (Y) out1[i] = y[i];
        if (Z) out2[i] = z[i];
        if (W) out3[i] = w[i];
    }

    if (X) iter->out[0] += SIZEOF_BLOCK;
    if (Y) iter->out[1] += SIZEOF_BLOCK;
    if (Z) iter->out[2] += SIZEOF_BLOCK;
    if (W) iter->out[3] += SIZEOF_BLOCK;
}

DECL_WRITE(write_packed, const SwsCompMask mask)
{
    const int elems = W ? 4 : Z ? 3 : Y ? 2 : 1;

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        if (X) out0[elems * i + 0] = x[i];
        if (Y) out0[elems * i + 1] = y[i];
        if (Z) out0[elems * i + 2] = z[i];
        if (W) out0[elems * i + 3] = w[i];
    }

    iter->out[0] += SIZEOF_BLOCK * elems;
}

#if BIT_DEPTH == 8

DECL_READ(read_bit, const SwsCompMask mask)
{
    av_assert2(mask == SWS_COMP_ELEMS(1));

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

    iter->in[0] += SIZEOF_BLOCK >> 3;
    CONTINUE(x, y, z, w);
}

DECL_READ(read_nibble, const SwsCompMask mask)
{
    av_assert2(mask == SWS_COMP_ELEMS(1));

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i += 2) {
        const pixel_t val = in0[i >> 1];
        x[i + 0] = val >> 4;  /* high nibble */
        x[i + 1] = val & 0xF; /* low nibble */
    }

    iter->in[0] += SIZEOF_BLOCK >> 1;
    CONTINUE(x, y, z, w);
}

DECL_READ(read_palette, const SwsCompMask mask)
{
    av_assert2(mask == SWS_COMP_ELEMS(4));

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        const pixel_t index = in0[i];
        const pixel_t *value = &in1[index * 4];
        x[i] = value[0];
        y[i] = value[1];
        z[i] = value[2];
        w[i] = value[3];
    }

    iter->in[0] += SIZEOF_BLOCK;
    CONTINUE(x, y, z, w);
}

DECL_WRITE(write_bit, const SwsCompMask mask)
{
    av_assert2(mask == SWS_COMP_ELEMS(1));

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

    iter->out[0] += SIZEOF_BLOCK >> 3;
}

DECL_WRITE(write_nibble, const SwsCompMask mask)
{
    av_assert2(mask == SWS_COMP_ELEMS(1));

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i += 2)
        out0[i >> 1] = x[i] << 4 | x[i + 1];

    iter->out[0] += SIZEOF_BLOCK >> 1;
}

#endif /* BIT_DEPTH == 8 */

SWS_FOR(PX, READ_PLANAR,    DECL_IMPL_READ,     read_planar)
SWS_FOR(PX, READ_PACKED,    DECL_IMPL_READ,     read_packed)
SWS_FOR(PX, READ_NIBBLE,    DECL_IMPL_READ,     read_nibble)
SWS_FOR(PX, READ_BIT,       DECL_IMPL_READ,     read_bit)
SWS_FOR(PX, READ_PALETTE,   DECL_IMPL_READ,     read_palette)
SWS_FOR(PX, WRITE_PLANAR,   DECL_IMPL_WRITE,    write_planar)
SWS_FOR(PX, WRITE_PACKED,   DECL_IMPL_WRITE,    write_packed)
SWS_FOR(PX, WRITE_NIBBLE,   DECL_IMPL_WRITE,    write_nibble)
SWS_FOR(PX, WRITE_BIT,      DECL_IMPL_WRITE,    write_bit)

SWS_FOR_STRUCT(PX, READ_PLANAR,     DECL_ENTRY)
SWS_FOR_STRUCT(PX, READ_PACKED,     DECL_ENTRY)
SWS_FOR_STRUCT(PX, READ_NIBBLE,     DECL_ENTRY)
SWS_FOR_STRUCT(PX, READ_BIT,        DECL_ENTRY)
SWS_FOR_STRUCT(PX, READ_PALETTE,    DECL_ENTRY)
SWS_FOR_STRUCT(PX, WRITE_PLANAR,    DECL_ENTRY)
SWS_FOR_STRUCT(PX, WRITE_PACKED,    DECL_ENTRY)
SWS_FOR_STRUCT(PX, WRITE_NIBBLE,    DECL_ENTRY)
SWS_FOR_STRUCT(PX, WRITE_BIT,       DECL_ENTRY)

/*****************************
 * Scaling / filtering reads *
 *****************************/

DECL_SETUP(setup_filter_v, params, out)
{
    if (params->uop->par.filter.type != SWS_PIXEL_F32)
        return AVERROR(ENOTSUP);

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
    out->priv.i32[2] = filter->filter_size;
    out->free = ff_op_priv_free;
    return 0;
}

/* Fully general vertical planar filter case */
DECL_READ(read_planar_fv, const SwsCompMask mask, const SwsPixelType type)
{
    av_assert2(type == SWS_PIXEL_F32);
    const SwsOpExec *exec = iter->exec;
    const float *restrict weights = impl->priv.ptr;
    const int filter_size = impl->priv.i32[2];
    weights += filter_size * iter->y;

    block_t xs, ys, zs, ws;
    if (X) memset(&xs.f32, 0, sizeof(xs.f32));
    if (Y) memset(&ys.f32, 0, sizeof(ys.f32));
    if (Z) memset(&zs.f32, 0, sizeof(zs.f32));
    if (W) memset(&ws.f32, 0, sizeof(ws.f32));

    for (int j = 0; j < filter_size; j++) {
        const float weight = weights[j];

        SWS_LOOP
        for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
            if (X) xs.f32[i] += weight * in0[i];
            if (Y) ys.f32[i] += weight * in1[i];
            if (Z) zs.f32[i] += weight * in2[i];
            if (W) ws.f32[i] += weight * in3[i];
        }

        if (X) in0 = bump_ptr(in0, exec->in_stride[0]);
        if (Y) in1 = bump_ptr(in1, exec->in_stride[1]);
        if (Z) in2 = bump_ptr(in2, exec->in_stride[2]);
        if (W) in3 = bump_ptr(in3, exec->in_stride[3]);
    }

    if (X) iter->in[0] += SIZEOF_BLOCK;
    if (Y) iter->in[1] += SIZEOF_BLOCK;
    if (Z) iter->in[2] += SIZEOF_BLOCK;
    if (W) iter->in[3] += SIZEOF_BLOCK;

    CONTINUE(&xs, &ys, &zs, &ws);
}

DECL_SETUP(setup_filter_h, params, out)
{
    if (params->uop->par.filter.type != SWS_PIXEL_F32)
        return AVERROR(ENOTSUP);

    SwsFilterWeights *filter = params->uop->data.kernel;
    out->priv.ptr = av_refstruct_ref(filter->weights);
    out->priv.i32[2] = filter->filter_size;
    out->free = ff_op_priv_unref;
    return 0;
}

/* Fully general horizontal planar filter case */
DECL_READ(read_planar_fh, const SwsCompMask mask, const SwsPixelType type)
{
    av_assert2(type == SWS_PIXEL_F32);
    const SwsOpExec *exec = iter->exec;
    const int *restrict weights = impl->priv.ptr;
    const int filter_size = impl->priv.i32[2];
    const float scale = 1.0f / SWS_FILTER_SCALE;
    const int xpos = iter->x;
    weights += filter_size * iter->x;

    block_t xs, ys, zs, ws;
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        const int offset = exec->in_offset_x[xpos + i];
        pixel_t *start0 = bump_ptr(in0, offset);
        pixel_t *start1 = bump_ptr(in1, offset);
        pixel_t *start2 = bump_ptr(in2, offset);
        pixel_t *start3 = bump_ptr(in3, offset);

        inter_t sx = 0, sy = 0, sz = 0, sw = 0;
        for (int j = 0; j < filter_size; j++) {
            const int weight = weights[j];
            if (X) sx += weight * start0[j];
            if (Y) sy += weight * start1[j];
            if (Z) sz += weight * start2[j];
            if (W) sw += weight * start3[j];
        }

        if (X) xs.f32[i] = (float) sx * scale;
        if (Y) ys.f32[i] = (float) sy * scale;
        if (Z) zs.f32[i] = (float) sz * scale;
        if (W) ws.f32[i] = (float) sw * scale;

        weights += filter_size;
    }

    CONTINUE(&xs, &ys, &zs, &ws);
}

SWS_FOR(PX, READ_PLANAR_FV, DECL_IMPL_READ, read_planar_fv)
SWS_FOR(PX, READ_PLANAR_FH, DECL_IMPL_READ, read_planar_fh)
SWS_FOR_STRUCT(PX, READ_PLANAR_FV, DECL_ENTRY, .setup = fn(setup_filter_v) )
SWS_FOR_STRUCT(PX, READ_PLANAR_FH, DECL_ENTRY, .setup = fn(setup_filter_h) )

/***************************
 * Permutation and copying *
 ***************************/

/* Permute by directly swapping the order of arguments to the continuation. */
#define DECL_PERMUTE(DUMMY, NAME, TYPE, UOP, MASK, IDX0, IDX1, IDX2, IDX3)      \
    static void NAME##_c(SwsOpIter *restrict iter,                              \
                         const SwsOpImpl *restrict impl,                        \
                         void *restrict in0, void *restrict in1,                \
                         void *restrict in2, void *restrict in3)                \
    {                                                                           \
        CONTINUE(in##IDX0, in##IDX1, in##IDX2, in##IDX3);                       \
    }

#define DECL_COPY(DUMMY, NAME, TYPE, UOP, MASK, IDX0, IDX1, IDX2, IDX3)         \
    static void NAME##_c(SwsOpIter *restrict iter,                              \
                         const SwsOpImpl *restrict impl,                        \
                         void *restrict in0, void *restrict in1,                \
                         void *restrict in2, void *restrict in3)                \
    {                                                                           \
        const SwsCompMask mask = (MASK);                                        \
        block_t x, y, z, w;                                                     \
                                                                                \
        if (X) memcpy(&x.px, in##IDX0, SIZEOF_BLOCK);                           \
        if (Y) memcpy(&y.px, in##IDX1, SIZEOF_BLOCK);                           \
        if (Z) memcpy(&z.px, in##IDX2, SIZEOF_BLOCK);                           \
        if (W) memcpy(&w.px, in##IDX3, SIZEOF_BLOCK);                           \
                                                                                \
        CONTINUE(X ? &x : in0, Y ? &y : in1, Z ? &z : in2, W ? &w : in3);       \
    }

SWS_FOR(PX, PERMUTE, DECL_PERMUTE)
SWS_FOR(PX, COPY,    DECL_COPY)
SWS_FOR_STRUCT(PX, PERMUTE, DECL_ENTRY)
SWS_FOR_STRUCT(PX, COPY,    DECL_ENTRY)

/*********************
 * Format conversion *
 *********************/

#define DECL_CAST(DST, dst)                                                     \
    DECL_FUNC(to_##dst, const SwsCompMask mask)                                 \
    {                                                                           \
        block_t xx, yy, zz, ww;                                                 \
                                                                                \
        SWS_LOOP                                                                \
        for (int i = 0; i < SWS_BLOCK_SIZE; i++) {                              \
            if (X) xx.dst[i] = x[i];                                            \
            if (Y) yy.dst[i] = y[i];                                            \
            if (Z) zz.dst[i] = z[i];                                            \
            if (W) ww.dst[i] = w[i];                                            \
        }                                                                       \
                                                                                \
        CONTINUE(&xx, &yy, &zz, &ww);                                           \
    }                                                                           \
                                                                                \
    SWS_FOR(PX, TO_##DST, DECL_IMPL, to_##dst)                                  \
    SWS_FOR_STRUCT(PX, TO_##DST, DECL_ENTRY)

DECL_CAST(U8,  u8)
DECL_CAST(U16, u16)
DECL_CAST(U32, u32)
DECL_CAST(F32, f32)

/********************
 * Bit manipulation *
 ********************/

#if !IS_FLOAT
DECL_FUNC(lshift, const SwsCompMask mask, const uint8_t amount)
{
    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        if (X) x[i] <<= amount;
        if (Y) y[i] <<= amount;
        if (Z) z[i] <<= amount;
        if (W) w[i] <<= amount;
    }

    CONTINUE(x, y, z, w);
}

DECL_FUNC(rshift, const SwsCompMask mask, const uint8_t amount)
{
    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        if (X) x[i] >>= amount;
        if (Y) y[i] >>= amount;
        if (Z) z[i] >>= amount;
        if (W) w[i] >>= amount;
    }

    CONTINUE(x, y, z, w);
}
#endif

SWS_FOR(PX, LSHIFT, DECL_IMPL, lshift)
SWS_FOR(PX, RSHIFT, DECL_IMPL, rshift)

SWS_FOR_STRUCT(PX, LSHIFT, DECL_ENTRY)
SWS_FOR_STRUCT(PX, RSHIFT, DECL_ENTRY)

#ifdef PIXEL_SWAP
DECL_FUNC(swap_bytes, const SwsCompMask mask)
{
    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        if (X) x[i] = PIXEL_SWAP(x[i]);
        if (Y) y[i] = PIXEL_SWAP(y[i]);
        if (Z) z[i] = PIXEL_SWAP(z[i]);
        if (W) w[i] = PIXEL_SWAP(w[i]);
    }

    CONTINUE(x, y, z, w);
}
#endif /* PIXEL_SWAP */

SWS_FOR(PX, SWAP_BYTES, DECL_IMPL, swap_bytes)
SWS_FOR_STRUCT(PX, SWAP_BYTES, DECL_ENTRY)

#ifdef PIXEL_MAX
DECL_FUNC(expand_bit, const SwsCompMask mask)
{
    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        if (X) x[i] = x[i] ? PIXEL_MAX : 0;
        if (Y) y[i] = y[i] ? PIXEL_MAX : 0;
        if (Z) z[i] = z[i] ? PIXEL_MAX : 0;
        if (W) w[i] = w[i] ? PIXEL_MAX : 0;
    }

    CONTINUE(x, y, z, w);
}
#endif

#if BIT_DEPTH == 8
DECL_FUNC(expand_pair, const SwsCompMask mask)
{
    block_t x16, y16, z16, w16;

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        if (X) x16.u16[i] = x[i] << 8 | x[i];
        if (Y) y16.u16[i] = y[i] << 8 | y[i];
        if (Z) z16.u16[i] = z[i] << 8 | z[i];
        if (W) w16.u16[i] = w[i] << 8 | w[i];
    }

    CONTINUE(&x16, &y16, &z16, &w16);
}

DECL_FUNC(expand_quad, const SwsCompMask mask)
{
    block_t x32, y32, z32, w32;

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        if (X) x32.u32[i] = (uint32_t) x[i] << 24 | x[i] << 16 | x[i] << 8 | x[i];
        if (Y) y32.u32[i] = (uint32_t) y[i] << 24 | y[i] << 16 | y[i] << 8 | y[i];
        if (Z) z32.u32[i] = (uint32_t) z[i] << 24 | z[i] << 16 | z[i] << 8 | z[i];
        if (W) w32.u32[i] = (uint32_t) w[i] << 24 | w[i] << 16 | w[i] << 8 | w[i];
    }

    CONTINUE(&x32, &y32, &z32, &w32);
}
#endif /* BIT_DEPTH == 8 */

SWS_FOR(PX, EXPAND_BIT,  DECL_IMPL, expand_bit)
SWS_FOR(PX, EXPAND_PAIR, DECL_IMPL, expand_pair)
SWS_FOR(PX, EXPAND_QUAD, DECL_IMPL, expand_quad)
SWS_FOR_STRUCT(PX, EXPAND_BIT,  DECL_ENTRY)
SWS_FOR_STRUCT(PX, EXPAND_PAIR, DECL_ENTRY)
SWS_FOR_STRUCT(PX, EXPAND_QUAD, DECL_ENTRY)

/*************************
 * Packing and unpacking *
 ************************/

#if !IS_FLOAT
DECL_FUNC(unpack, const SwsCompMask mask,
                  const uint8_t bx, const uint8_t by,
                  const uint8_t bz, const uint8_t bw)
{
    const uint8_t sx = bw + bz + by;
    const uint8_t sy = bw + bz;
    const uint8_t sz = bw;
    const uint8_t sw = 0;

    const pixel_t mx = (1 << bx) - 1;
    const pixel_t my = (1 << by) - 1;
    const pixel_t mz = (1 << bz) - 1;
    const pixel_t mw = (1 << bw) - 1;

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        const pixel_t val = x[i];
        if (X) x[i] = (val >> sx) & mx;
        if (Y) y[i] = (val >> sy) & my;
        if (Z) z[i] = (val >> sz) & mz;
        if (W) w[i] = (val >> sw) & mw;
    }

    CONTINUE(x, y, z, w);
}

DECL_FUNC(pack, const SwsCompMask mask,
                const uint8_t bx, const uint8_t by,
                const uint8_t bz, const uint8_t bw)
{
    const uint8_t sx = bw + bz + by;
    const uint8_t sy = bw + bz;
    const uint8_t sz = bw;
    const uint8_t sw = 0;

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        pixel_t val = 0;
        if (X) val |= x[i] << sx;
        if (Y) val |= y[i] << sy;
        if (Z) val |= z[i] << sz;
        if (W) val |= w[i] << sw;
        x[i] = val;
    }

    CONTINUE(x, y, z, w);
}
#endif /* !IS_FLOAT */

SWS_FOR(PX, UNPACK, DECL_IMPL, unpack)
SWS_FOR(PX, PACK,   DECL_IMPL, pack)
SWS_FOR_STRUCT(PX, UNPACK,  DECL_ENTRY)
SWS_FOR_STRUCT(PX, PACK,    DECL_ENTRY)

/***********************
 * Pixel data clearing *
 ***********************/

#ifdef PIXEL_MAX
DECL_FUNC(clear, const SwsCompMask mask, const SwsCompMask one,
                 const SwsCompMask zero)
{
    #define ONE(N)  SWS_COMP_TEST(one, N)
    #define ZERO(N) SWS_COMP_TEST(zero, N)
    const pixel_t cx = ONE(0) ? PIXEL_MAX : ZERO(0) ? 0 : impl->priv.px[0];
    const pixel_t cy = ONE(1) ? PIXEL_MAX : ZERO(1) ? 0 : impl->priv.px[1];
    const pixel_t cz = ONE(2) ? PIXEL_MAX : ZERO(2) ? 0 : impl->priv.px[2];
    const pixel_t cw = ONE(3) ? PIXEL_MAX : ZERO(3) ? 0 : impl->priv.px[3];

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        if (X) x[i] = cx;
        if (Y) y[i] = cy;
        if (Z) z[i] = cz;
        if (W) w[i] = cw;
    }

    CONTINUE(x, y, z, w);
}
#endif

SWS_FOR(PX, CLEAR, DECL_IMPL, clear)
SWS_FOR_STRUCT(PX, CLEAR, DECL_ENTRY, .setup = ff_sws_setup_vec4)

/*************************
 * Arithmetic operations *
 *************************/

DECL_FUNC(scale, const SwsCompMask mask)
{
    const pixel_t scale = impl->priv.px[0];

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        if (X) x[i] *= scale;
        if (Y) y[i] *= scale;
        if (Z) z[i] *= scale;
        if (W) w[i] *= scale;
    }

    CONTINUE(x, y, z, w);
}

DECL_FUNC(add, const SwsCompMask mask)
{
    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        if (X) x[i] += impl->priv.px[0];
        if (Y) y[i] += impl->priv.px[1];
        if (Z) z[i] += impl->priv.px[2];
        if (W) w[i] += impl->priv.px[3];
    }

    CONTINUE(x, y, z, w);
}

DECL_FUNC(min, const SwsCompMask mask)
{
    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        if (X) x[i] = FFMIN(x[i], impl->priv.px[0]);
        if (Y) y[i] = FFMIN(y[i], impl->priv.px[1]);
        if (Z) z[i] = FFMIN(z[i], impl->priv.px[2]);
        if (W) w[i] = FFMIN(w[i], impl->priv.px[3]);
    }

    CONTINUE(x, y, z, w);
}

DECL_FUNC(max, const SwsCompMask mask)
{
    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        if (X) x[i] = FFMAX(x[i], impl->priv.px[0]);
        if (Y) y[i] = FFMAX(y[i], impl->priv.px[1]);
        if (Z) z[i] = FFMAX(z[i], impl->priv.px[2]);
        if (W) w[i] = FFMAX(w[i], impl->priv.px[3]);
    }

    CONTINUE(x, y, z, w);
}

SWS_FOR(PX, SCALE, DECL_IMPL, scale)
SWS_FOR(PX, ADD,   DECL_IMPL, add)
SWS_FOR(PX, MIN,   DECL_IMPL, min)
SWS_FOR(PX, MAX,   DECL_IMPL, max)
SWS_FOR_STRUCT(PX, SCALE, DECL_ENTRY, .setup = ff_sws_setup_scalar )
SWS_FOR_STRUCT(PX, ADD,   DECL_ENTRY, .setup = ff_sws_setup_vec4 )
SWS_FOR_STRUCT(PX, MIN,   DECL_ENTRY, .setup = ff_sws_setup_vec4 )
SWS_FOR_STRUCT(PX, MAX,   DECL_ENTRY, .setup = ff_sws_setup_vec4 )

/*************
 * Dithering *
 *************/

DECL_SETUP(setup_dither, params, out)
{
    const SwsUOp *uop = params->uop;
    const SwsDitherUOp *dither = &uop->par.dither;
    const int size = 1 << dither->size_log2;
    if (size >= SWS_BLOCK_SIZE) {
        /* No extra padding needed */
        out->priv.ptr = av_refstruct_ref(uop->data.ptr);
        out->free = ff_op_priv_unref;
        return 0;
    }

    const int stride = FFMAX(size, SWS_BLOCK_SIZE);
    const int height = ff_sws_dither_height(dither);
    pixel_t *matrix = av_malloc(sizeof(pixel_t) * height * stride);
    if (!matrix)
        return AVERROR(ENOMEM);
    out->priv.ptr = matrix;
    out->free = ff_op_priv_free;

    /* Pad to multiple of block size. We don't need extra padding for the
     * height because ff_sws_dither_height() already includes any padding
     * necessary for the y_offset */
    for (int y = 0; y < height; y++) {
        pixel_t *row = &matrix[y * stride];
        for (int x = 0; x < size; x++)
            row[x] = uop->data.ptr[y * size + x].px;
        for (int x = size; x < stride; x++)
            row[x] = row[x % size];
    }

    return 0;
}

DECL_FUNC(dither, const SwsCompMask mask,
                  const uint8_t off0, const uint8_t off1,
                  const uint8_t off2, const uint8_t off3,
                  const uint8_t size_log2)
{
    const int size   = 1 << size_log2;
    const int stride = FFMAX(size, SWS_BLOCK_SIZE);

    const pixel_t *matrix = impl->priv.ptr;
    matrix += (iter->y & (size - 1)) * stride;
    matrix += (iter->x & (size - 1)) & ~(SWS_BLOCK_SIZE - 1);

    const pixel_t *const row0 = &matrix[off0 * stride];
    const pixel_t *const row1 = &matrix[off1 * stride];
    const pixel_t *const row2 = &matrix[off2 * stride];
    const pixel_t *const row3 = &matrix[off3 * stride];

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        if (X) x[i] += row0[i];
        if (Y) y[i] += row1[i];
        if (Z) z[i] += row2[i];
        if (W) w[i] += row3[i];
    }

    CONTINUE(x, y, z, w);
}

SWS_FOR(PX, DITHER, DECL_IMPL, dither)
SWS_FOR_STRUCT(PX, DITHER, DECL_ENTRY, .setup = fn(setup_dither) )

/*********************
 * Linear operations *
 *********************/

typedef struct {
    /* Stored in split form for convenience */
    pixel_t m[4][4];
    pixel_t k[4];
} fn(LinCoeffs);

DECL_SETUP(setup_linear, params, out)
{
    const SwsUOp *uop = params->uop;
    fn(LinCoeffs) c;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++)
            c.m[i][j] = uop->data.mat4[i][j].px;
        c.k[i] = uop->data.mat4[i][4].px;
    }

    out->priv.ptr = av_memdup(&c, sizeof(c));
    out->free = ff_op_priv_free;
    return out->priv.ptr ? 0 : AVERROR(ENOMEM);
}

/**
 * Fully general case for a 5x5 linear affine transformation. Should never be
 * called without constant `mask`. This function will compile down to the
 * appropriately optimized version for the required subset of operations when
 * called with a constant mask.
 */
DECL_FUNC(linear, const SwsCompMask mask, const uint32_t one, const uint32_t zero)
{
    const fn(LinCoeffs) c = *(const fn(LinCoeffs) *) impl->priv.ptr;

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        const pixel_t xx = x[i];
        const pixel_t yy = y[i];
        const pixel_t zz = z[i];
        const pixel_t ww = w[i];

#define LIN_VAL(I, J, val) \
    ((one & SWS_MASK(I, J)) ? (val) : c.m[I][J] * (val))

#define LIN_ROW(I, var) do {                                    \
    var[i] = (zero & SWS_MASK(I, 4)) ? 0 : c.k[I];              \
    if (!(zero & SWS_MASK(I, 0))) var[i] += LIN_VAL(I, 0, xx);  \
    if (!(zero & SWS_MASK(I, 1))) var[i] += LIN_VAL(I, 1, yy);  \
    if (!(zero & SWS_MASK(I, 2))) var[i] += LIN_VAL(I, 2, zz);  \
    if (!(zero & SWS_MASK(I, 3))) var[i] += LIN_VAL(I, 3, ww);  \
} while (0)

        if (X) LIN_ROW(0, x);
        if (Y) LIN_ROW(1, y);
        if (Z) LIN_ROW(2, z);
        if (W) LIN_ROW(3, w);
    }

    CONTINUE(x, y, z, w);
}

SWS_FOR(PX, LINEAR, DECL_IMPL, linear)
SWS_FOR_STRUCT(PX, LINEAR, DECL_ENTRY, .setup = fn(setup_linear) )

#undef PIXEL_MAX
#undef PIXEL_SWAP
#undef pixel_t
#undef inter_t
#undef block_t
#undef PX
#undef px
