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
