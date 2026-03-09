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

#include "ops_backend.h"

#ifndef BIT_DEPTH
#  error Should only be included from ops_tmpl_*.c!
#endif

#define WRAP_CONVERT_UINT(N)                                                    \
DECL_PATTERN(convert_uint##N)                                                   \
{                                                                               \
    u##N##block_t xu, yu, zu, wu;                                               \
                                                                                \
    SWS_LOOP                                                                    \
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {                                  \
        if (X)                                                                  \
            xu[i] = x[i];                                                       \
        if (Y)                                                                  \
            yu[i] = y[i];                                                       \
        if (Z)                                                                  \
            zu[i] = z[i];                                                       \
        if (W)                                                                  \
            wu[i] = w[i];                                                       \
    }                                                                           \
                                                                                \
    CONTINUE(u##N##block_t, xu, yu, zu, wu);                                    \
}                                                                               \
                                                                                \
WRAP_COMMON_PATTERNS(convert_uint##N,                                           \
    .op = SWS_OP_CONVERT,                                                       \
    .convert.to = SWS_PIXEL_U##N,                                               \
);

#if BIT_DEPTH != 8
WRAP_CONVERT_UINT(8)
#endif

#if BIT_DEPTH != 16
WRAP_CONVERT_UINT(16)
#endif

#if BIT_DEPTH != 32 || defined(IS_FLOAT)
WRAP_CONVERT_UINT(32)
#endif

DECL_PATTERN(clear)
{
    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        if (!X)
            x[i] = impl->priv.px[0];
        if (!Y)
            y[i] = impl->priv.px[1];
        if (!Z)
            z[i] = impl->priv.px[2];
        if (!W)
            w[i] = impl->priv.px[3];
    }

    CONTINUE(block_t, x, y, z, w);
}

#define WRAP_CLEAR(X, Y, Z, W)                                                  \
DECL_IMPL(clear##_##X##Y##Z##W)                                                 \
{                                                                               \
    CALL(clear, X, Y, Z, W);                                                    \
}                                                                               \
                                                                                \
DECL_ENTRY(clear##_##X##Y##Z##W,                                                \
    .setup = ff_sws_setup_q4,                                                   \
    .op = SWS_OP_CLEAR,                                                         \
    .flexible = true,                                                           \
    .unused = { !X, !Y, !Z, !W },                                               \
);

WRAP_CLEAR(1, 1, 1, 0) /* rgba alpha */
WRAP_CLEAR(0, 1, 1, 1) /* argb alpha */
WRAP_CLEAR(1, 0, 1, 1) /* ya alpha */

WRAP_CLEAR(0, 0, 1, 1) /* vuya chroma */
WRAP_CLEAR(1, 0, 0, 1) /* yuva chroma */
WRAP_CLEAR(1, 1, 0, 0) /* ayuv chroma */
WRAP_CLEAR(0, 1, 0, 1) /* uyva chroma */
WRAP_CLEAR(1, 0, 1, 0) /* xvyu chroma */

WRAP_CLEAR(1, 0, 0, 0) /* gray -> yuva */
WRAP_CLEAR(0, 1, 0, 0) /* gray -> ayuv */
WRAP_CLEAR(0, 0, 1, 0) /* gray -> vuya */

DECL_PATTERN(min)
{
    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        if (X)
            x[i] = FFMIN(x[i], impl->priv.px[0]);
        if (Y)
            y[i] = FFMIN(y[i], impl->priv.px[1]);
        if (Z)
            z[i] = FFMIN(z[i], impl->priv.px[2]);
        if (W)
            w[i] = FFMIN(w[i], impl->priv.px[3]);
    }

    CONTINUE(block_t, x, y, z, w);
}

DECL_PATTERN(max)
{
    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        if (X)
            x[i] = FFMAX(x[i], impl->priv.px[0]);
        if (Y)
            y[i] = FFMAX(y[i], impl->priv.px[1]);
        if (Z)
            z[i] = FFMAX(z[i], impl->priv.px[2]);
        if (W)
            w[i] = FFMAX(w[i], impl->priv.px[3]);
    }

    CONTINUE(block_t, x, y, z, w);
}

WRAP_COMMON_PATTERNS(min,
    .op = SWS_OP_MIN,
    .setup = ff_sws_setup_q4,
    .flexible = true,
);

WRAP_COMMON_PATTERNS(max,
    .op = SWS_OP_MAX,
    .setup = ff_sws_setup_q4,
    .flexible = true,
);

DECL_PATTERN(scale)
{
    const pixel_t scale = impl->priv.px[0];

    SWS_LOOP
    for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
        if (X)
            x[i] *= scale;
        if (Y)
            y[i] *= scale;
        if (Z)
            z[i] *= scale;
        if (W)
            w[i] *= scale;
    }

    CONTINUE(block_t, x, y, z, w);
}

WRAP_COMMON_PATTERNS(scale,
    .op = SWS_OP_SCALE,
    .setup = ff_sws_setup_q,
    .flexible = true,
);

DECL_SETUP(setup_filter_v, params, out)
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
    out->priv.i32[2] = filter->filter_size;
    out->free = ff_op_priv_free;
    return 0;
}

/* Fully general vertical planar filter case */
DECL_READ(filter_v, const int elems)
{
    const SwsOpExec *exec = iter->exec;
    const float *restrict weights = impl->priv.ptr;
    const int filter_size = impl->priv.i32[2];
    weights += filter_size * iter->y;

    f32block_t xs, ys, zs, ws;
    memset(xs, 0, sizeof(xs));
    if (elems > 1)
        memset(ys, 0, sizeof(ys));
    if (elems > 2)
        memset(zs, 0, sizeof(zs));
    if (elems > 3)
        memset(ws, 0, sizeof(ws));

    for (int j = 0; j < filter_size; j++) {
        const float weight = weights[j];

        SWS_LOOP
        for (int i = 0; i < SWS_BLOCK_SIZE; i++) {
            xs[i] += weight * in0[i];
            if (elems > 1)
                ys[i] += weight * in1[i];
            if (elems > 2)
                zs[i] += weight * in2[i];
            if (elems > 3)
                ws[i] += weight * in3[i];
        }

        in0 = bump_ptr(in0, exec->in_stride[0]);
        if (elems > 1)
            in1 = bump_ptr(in1, exec->in_stride[1]);
        if (elems > 2)
            in2 = bump_ptr(in2, exec->in_stride[2]);
        if (elems > 3)
            in3 = bump_ptr(in3, exec->in_stride[3]);
    }

    for (int i = 0; i < elems; i++)
        iter->in[i] += sizeof(block_t);

    CONTINUE(f32block_t, xs, ys, zs, ws);
}

#define WRAP_FILTER(FUNC, DIR, ELEMS, SUFFIX)                                   \
DECL_IMPL(FUNC##ELEMS##SUFFIX)                                                  \
{                                                                               \
    CALL_READ(FUNC##SUFFIX, ELEMS);                                             \
}                                                                               \
                                                                                \
DECL_ENTRY(FUNC##ELEMS##SUFFIX,                                                 \
    .op = SWS_OP_READ,                                                          \
    .setup = fn(setup_filter##SUFFIX),                                          \
    .rw.elems = ELEMS,                                                          \
    .rw.filter = SWS_OP_FILTER_##DIR,                                           \
);

WRAP_FILTER(filter, V, 1, _v)
WRAP_FILTER(filter, V, 2, _v)
WRAP_FILTER(filter, V, 3, _v)
WRAP_FILTER(filter, V, 4, _v)

static void fn(process)(const SwsOpExec *exec, const void *priv,
                        const int bx_start, const int y_start,
                        int bx_end, int y_end)
{
    const SwsOpChain *chain = priv;
    const SwsOpImpl *impl = chain->impl;
    u32block_t x, y, z, w; /* allocate enough space for any intermediate */

    SwsOpIter iterdata;
    SwsOpIter *iter = &iterdata; /* for CONTINUE() macro to work */
    iter->exec = exec;
    for (int i = 0; i < 4; i++) {
        iter->in[i]  = (uintptr_t) exec->in[i];
        iter->out[i] = (uintptr_t) exec->out[i];
    }

    for (iter->y = y_start; iter->y < y_end; iter->y++) {
        for (int block = bx_start; block < bx_end; block++) {
            iter->x = block * SWS_BLOCK_SIZE;
            CONTINUE(block_t, (void *) x, (void *) y, (void *) z, (void *) w);
        }

        const int y_bump = exec->in_bump_y ? exec->in_bump_y[iter->y] : 0;
        for (int i = 0; i < 4; i++) {
            iter->in[i]  += exec->in_bump[i] + y_bump * exec->in_stride[i];
            iter->out[i] += exec->out_bump[i];
        }
    }
}
