/*
 * Copyright (c) 2013 Paul B Mahol
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

#include "libavutil/common.h"
#include "libavutil/intfloat.h"
#include "avfilter.h"
#include "video.h"
#include "blend.h"

#undef PIXEL
#undef MAX
#undef HALF
#undef CLIP

#if DEPTH == 8
#define PIXEL uint8_t
#define MAX 255
#define HALF 128
#define CLIP(x) (av_clip_uint8(x))
#elif DEPTH == 32
#define PIXEL float
#define MAX 1.f
#define HALF 0.5f
#define CLIP(x) (x)
#else
#define PIXEL uint16_t
#define MAX ((1 << DEPTH) - 1)
#define HALF (1 << (DEPTH - 1))
#define CLIP(x) ((int)av_clip_uintp2(x, DEPTH))
#endif

#undef MULTIPLY
#undef SCREEN
#undef BURN
#undef DODGE
#undef GEOMETRIC
#undef INT2FLOAT
#undef FLOAT2INT
#undef MDIV
#undef LRINTF

#if DEPTH < 32
#define MULTIPLY(x, a, b) ((x) * (((a) * (b)) / MAX))
#define SCREEN(x, a, b)   (MAX - (x) * ((MAX - (a)) * (MAX - (b)) / MAX))
#define BURN(a, b)        (((a) == 0) ? (a) : FFMAX(0, MAX - ((MAX - (b)) << DEPTH) / (a)))
#define DODGE(a, b)       (((a) == MAX) ? (a) : FFMIN(MAX, (((b) << DEPTH) / (MAX - (a)))))
#define GEOMETRIC(a, b)   (lrintf(sqrtf((unsigned)A * B)))
#define INT2FLOAT(x)  (x)
#define FLOAT2INT(x)  (x)
#define MDIV (0.125f * (1 << DEPTH))
#define LRINTF(x) lrintf(x)
#else
#define MULTIPLY(x, a, b) ((x) * (((a) * (b)) / 1.0))
#define SCREEN(x, a, b)   (1.0 - (x) * ((1.0 - (a)) * (1.0 - (b)) / 1.0))
#define BURN(a, b)        (((a) <= 0.0) ? (a) : FFMAX(0.0, 1.0 - (1.0 - (b)) / (a)))
#define DODGE(a, b)       (((a) >= 1.0) ? (a) : FFMIN(1.0, ((b) / (1.0 - (a)))))
#define GEOMETRIC(a, b)   (sqrtf(fmaxf(A, 0) * fmaxf(B, 0)))
#define INT2FLOAT(x) av_int2float(x)
#define FLOAT2INT(x) av_float2int(x)
#define MDIV 0.125f
#define LRINTF(x) (x)
#endif

#define A top[j]
#define B bottom[j]

#define fn2(a, b)          blend_##a##_##b##bit
#define fn1(name, depth)   fn2(name, depth)
#define fn0(name)          fn1(name, DEPTH)

#define fn(NAME, EXPR)                                       \
static void fn0(NAME)(const uint8_t *_top, ptrdiff_t top_linesize, \
     const uint8_t *_bottom, ptrdiff_t bottom_linesize,       \
     uint8_t *_dst, ptrdiff_t dst_linesize,                   \
     ptrdiff_t width, ptrdiff_t height,                       \
     FilterParams *param, SliceParams *sliceparam)            \
{                                                                                   \
    const PIXEL *top = (const PIXEL *)_top;                                         \
    const PIXEL *bottom = (const PIXEL *)_bottom;                                   \
    PIXEL *dst = (PIXEL *)_dst;                                                     \
    const float opacity = param->opacity;                                           \
                                                                                    \
    dst_linesize /= sizeof(PIXEL);                                                  \
    top_linesize /= sizeof(PIXEL);                                                  \
    bottom_linesize /= sizeof(PIXEL);                                               \
                                                                                    \
    for (int i = 0; i < height; i++) {                                              \
        for (int j = 0; j < width; j++) {                                           \
            dst[j] = top[j] + ((EXPR)-top[j]) * opacity;                            \
        }                                                                           \
        dst += dst_linesize;                                                        \
        top += top_linesize;                                                        \
        bottom += bottom_linesize;                                                  \
    }                                                                               \
}

fn(addition,   FFMIN(MAX, A + B))
fn(grainmerge, CLIP(A + B - HALF))
fn(average,    (A + B) / 2)
fn(subtract,   FFMAX(0, A - B))
fn(multiply,   MULTIPLY(1, A, B))
fn(multiply128,CLIP((A - HALF) * B / MDIV + HALF))
fn(negation,   MAX - FFABS(MAX - A - B))
fn(extremity,  FFABS(MAX - A - B))
fn(difference, FFABS(A - B))
fn(grainextract, CLIP(HALF + A - B))
fn(screen,     SCREEN(1, A, B))
fn(overlay,    (A < HALF) ? MULTIPLY(2, A, B) : SCREEN(2, A, B))
fn(hardlight,  (B < HALF) ? MULTIPLY(2, B, A) : SCREEN(2, B, A))
fn(hardmix,    (A < (MAX - B)) ? 0: MAX)
fn(heat,       (A == 0) ? 0 : MAX - FFMIN(((MAX - B) * (MAX - B)) / A, MAX))
fn(freeze,     (B == 0) ? 0 : MAX - FFMIN(((MAX - A) * (MAX - A)) / B, MAX))
fn(darken,     FFMIN(A, B))
fn(lighten,    FFMAX(A, B))
fn(divide,     CLIP(B == 0 ? MAX : MAX * A / B))
fn(dodge,      DODGE(A, B))
fn(burn,       BURN(A, B))
fn(softlight,  CLIP(A * A / MAX + (2 * (B * ((A * (MAX - A)) / MAX) / MAX))))
fn(exclusion,  A + B - 2 * A * B / MAX)
fn(pinlight,   (B < HALF) ? FFMIN(A, 2 * B) : FFMAX(A, 2 * (B - HALF)))
fn(phoenix,    FFMIN(A, B) - FFMAX(A, B) + MAX)
fn(reflect,    (B == MAX) ? B : FFMIN(MAX, (A * A / (MAX - B))))
fn(glow,       (A == MAX) ? A : FFMIN(MAX, (B * B / (MAX - A))))
fn(and,        INT2FLOAT(FLOAT2INT(A) & FLOAT2INT(B)))
fn(or,         INT2FLOAT(FLOAT2INT(A) | FLOAT2INT(B)))
fn(xor,        INT2FLOAT(FLOAT2INT(A) ^ FLOAT2INT(B)))
fn(vividlight, (A < HALF) ? BURN(2 * A, B) : DODGE(2 * (A - HALF), B))
fn(linearlight,CLIP((B < HALF) ? B + 2 * A - MAX : B + 2 * (A - HALF)))
fn(softdifference,CLIP((A > B) ? (B == MAX) ? 0 : (A - B) * MAX / (MAX - B) : (B == 0) ? 0 : (B - A) * MAX / B))
fn(geometric,  GEOMETRIC(A, B))
fn(harmonic,   A == 0 && B == 0 ? 0 : 2LL * A * B / (A + B))
fn(bleach,     (MAX - B) + (MAX - A) - MAX)
fn(stain,      2 * MAX - A - B)
fn(interpolate,LRINTF(MAX * (2 - cosf(A * M_PI / MAX) - cosf(B * M_PI / MAX)) * 0.25f))
fn(hardoverlay,A == MAX ? MAX : FFMIN(MAX, MAX * B / (2 * MAX - 2 * A) * (A > HALF) + 2 * A * B / MAX * (A <= HALF)))
