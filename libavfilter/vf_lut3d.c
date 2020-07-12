/*
 * Copyright (c) 2013 Clément Bœsch
 * Copyright (c) 2018 Paul B Mahol
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

/**
 * @file
 * 3D Lookup table filter
 */

#include "float.h"

#include "libavutil/opt.h"
#include "libavutil/file.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/intfloat.h"
#include "libavutil/avassert.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avstring.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "framesync.h"
#include "internal.h"
#include "video.h"

#define R 0
#define G 1
#define B 2
#define A 3

enum interp_mode {
    INTERPOLATE_NEAREST,
    INTERPOLATE_TRILINEAR,
    INTERPOLATE_TETRAHEDRAL,
    NB_INTERP_MODE
};

struct rgbvec {
    float r, g, b;
};

/* 3D LUT don't often go up to level 32, but it is common to have a Hald CLUT
 * of 512x512 (64x64x64) */
#define MAX_LEVEL 256
#define PRELUT_SIZE 65536

typedef struct Lut3DPreLut {
    int size;
    float min[3];
    float max[3];
    float scale[3];
    float* lut[3];
} Lut3DPreLut;

typedef struct LUT3DContext {
    const AVClass *class;
    int interpolation;          ///<interp_mode
    char *file;
    uint8_t rgba_map[4];
    int step;
    avfilter_action_func *interp;
    struct rgbvec scale;
    struct rgbvec *lut;
    int lutsize;
    int lutsize2;
    Lut3DPreLut prelut;
#if CONFIG_HALDCLUT_FILTER
    uint8_t clut_rgba_map[4];
    int clut_step;
    int clut_bits;
    int clut_planar;
    int clut_float;
    int clut_width;
    FFFrameSync fs;
#endif
} LUT3DContext;

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

#define OFFSET(x) offsetof(LUT3DContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
#define COMMON_OPTIONS \
    { "interp", "select interpolation mode", OFFSET(interpolation), AV_OPT_TYPE_INT, {.i64=INTERPOLATE_TETRAHEDRAL}, 0, NB_INTERP_MODE-1, FLAGS, "interp_mode" }, \
        { "nearest",     "use values from the nearest defined points",            0, AV_OPT_TYPE_CONST, {.i64=INTERPOLATE_NEAREST},     INT_MIN, INT_MAX, FLAGS, "interp_mode" }, \
        { "trilinear",   "interpolate values using the 8 points defining a cube", 0, AV_OPT_TYPE_CONST, {.i64=INTERPOLATE_TRILINEAR},   INT_MIN, INT_MAX, FLAGS, "interp_mode" }, \
        { "tetrahedral", "interpolate values using a tetrahedron",                0, AV_OPT_TYPE_CONST, {.i64=INTERPOLATE_TETRAHEDRAL}, INT_MIN, INT_MAX, FLAGS, "interp_mode" }, \
    { NULL }

#define EXPONENT_MASK 0x7F800000
#define MANTISSA_MASK 0x007FFFFF
#define SIGN_MASK     0x7FFFFFFF

static inline float sanitizef(float f)
{
    union av_intfloat32 t;
    t.f = f;

    if ((t.i & EXPONENT_MASK) == EXPONENT_MASK) {
        if ((t.i & MANTISSA_MASK) != 0) {
            // NAN
            return 0.0f;
        } else if (t.i & SIGN_MASK) {
            // -INF
            return FLT_MIN;
        } else {
            // +INF
            return FLT_MAX;
        }
    }
    return f;
}

static inline float lerpf(float v0, float v1, float f)
{
    return v0 + (v1 - v0) * f;
}

static inline struct rgbvec lerp(const struct rgbvec *v0, const struct rgbvec *v1, float f)
{
    struct rgbvec v = {
        lerpf(v0->r, v1->r, f), lerpf(v0->g, v1->g, f), lerpf(v0->b, v1->b, f)
    };
    return v;
}

#define NEAR(x) ((int)((x) + .5))
#define PREV(x) ((int)(x))
#define NEXT(x) (FFMIN((int)(x) + 1, lut3d->lutsize - 1))

/**
 * Get the nearest defined point
 */
static inline struct rgbvec interp_nearest(const LUT3DContext *lut3d,
                                           const struct rgbvec *s)
{
    return lut3d->lut[NEAR(s->r) * lut3d->lutsize2 + NEAR(s->g) * lut3d->lutsize + NEAR(s->b)];
}

/**
 * Interpolate using the 8 vertices of a cube
 * @see https://en.wikipedia.org/wiki/Trilinear_interpolation
 */
static inline struct rgbvec interp_trilinear(const LUT3DContext *lut3d,
                                             const struct rgbvec *s)
{
    const int lutsize2 = lut3d->lutsize2;
    const int lutsize  = lut3d->lutsize;
    const int prev[] = {PREV(s->r), PREV(s->g), PREV(s->b)};
    const int next[] = {NEXT(s->r), NEXT(s->g), NEXT(s->b)};
    const struct rgbvec d = {s->r - prev[0], s->g - prev[1], s->b - prev[2]};
    const struct rgbvec c000 = lut3d->lut[prev[0] * lutsize2 + prev[1] * lutsize + prev[2]];
    const struct rgbvec c001 = lut3d->lut[prev[0] * lutsize2 + prev[1] * lutsize + next[2]];
    const struct rgbvec c010 = lut3d->lut[prev[0] * lutsize2 + next[1] * lutsize + prev[2]];
    const struct rgbvec c011 = lut3d->lut[prev[0] * lutsize2 + next[1] * lutsize + next[2]];
    const struct rgbvec c100 = lut3d->lut[next[0] * lutsize2 + prev[1] * lutsize + prev[2]];
    const struct rgbvec c101 = lut3d->lut[next[0] * lutsize2 + prev[1] * lutsize + next[2]];
    const struct rgbvec c110 = lut3d->lut[next[0] * lutsize2 + next[1] * lutsize + prev[2]];
    const struct rgbvec c111 = lut3d->lut[next[0] * lutsize2 + next[1] * lutsize + next[2]];
    const struct rgbvec c00  = lerp(&c000, &c100, d.r);
    const struct rgbvec c10  = lerp(&c010, &c110, d.r);
    const struct rgbvec c01  = lerp(&c001, &c101, d.r);
    const struct rgbvec c11  = lerp(&c011, &c111, d.r);
    const struct rgbvec c0   = lerp(&c00,  &c10,  d.g);
    const struct rgbvec c1   = lerp(&c01,  &c11,  d.g);
    const struct rgbvec c    = lerp(&c0,   &c1,   d.b);
    return c;
}

/**
 * Tetrahedral interpolation. Based on code found in Truelight Software Library paper.
 * @see http://www.filmlight.ltd.uk/pdf/whitepapers/FL-TL-TN-0057-SoftwareLib.pdf
 */
static inline struct rgbvec interp_tetrahedral(const LUT3DContext *lut3d,
                                               const struct rgbvec *s)
{
    const int lutsize2 = lut3d->lutsize2;
    const int lutsize  = lut3d->lutsize;
    const int prev[] = {PREV(s->r), PREV(s->g), PREV(s->b)};
    const int next[] = {NEXT(s->r), NEXT(s->g), NEXT(s->b)};
    const struct rgbvec d = {s->r - prev[0], s->g - prev[1], s->b - prev[2]};
    const struct rgbvec c000 = lut3d->lut[prev[0] * lutsize2 + prev[1] * lutsize + prev[2]];
    const struct rgbvec c111 = lut3d->lut[next[0] * lutsize2 + next[1] * lutsize + next[2]];
    struct rgbvec c;
    if (d.r > d.g) {
        if (d.g > d.b) {
            const struct rgbvec c100 = lut3d->lut[next[0] * lutsize2 + prev[1] * lutsize + prev[2]];
            const struct rgbvec c110 = lut3d->lut[next[0] * lutsize2 + next[1] * lutsize + prev[2]];
            c.r = (1-d.r) * c000.r + (d.r-d.g) * c100.r + (d.g-d.b) * c110.r + (d.b) * c111.r;
            c.g = (1-d.r) * c000.g + (d.r-d.g) * c100.g + (d.g-d.b) * c110.g + (d.b) * c111.g;
            c.b = (1-d.r) * c000.b + (d.r-d.g) * c100.b + (d.g-d.b) * c110.b + (d.b) * c111.b;
        } else if (d.r > d.b) {
            const struct rgbvec c100 = lut3d->lut[next[0] * lutsize2 + prev[1] * lutsize + prev[2]];
            const struct rgbvec c101 = lut3d->lut[next[0] * lutsize2 + prev[1] * lutsize + next[2]];
            c.r = (1-d.r) * c000.r + (d.r-d.b) * c100.r + (d.b-d.g) * c101.r + (d.g) * c111.r;
            c.g = (1-d.r) * c000.g + (d.r-d.b) * c100.g + (d.b-d.g) * c101.g + (d.g) * c111.g;
            c.b = (1-d.r) * c000.b + (d.r-d.b) * c100.b + (d.b-d.g) * c101.b + (d.g) * c111.b;
        } else {
            const struct rgbvec c001 = lut3d->lut[prev[0] * lutsize2 + prev[1] * lutsize + next[2]];
            const struct rgbvec c101 = lut3d->lut[next[0] * lutsize2 + prev[1] * lutsize + next[2]];
            c.r = (1-d.b) * c000.r + (d.b-d.r) * c001.r + (d.r-d.g) * c101.r + (d.g) * c111.r;
            c.g = (1-d.b) * c000.g + (d.b-d.r) * c001.g + (d.r-d.g) * c101.g + (d.g) * c111.g;
            c.b = (1-d.b) * c000.b + (d.b-d.r) * c001.b + (d.r-d.g) * c101.b + (d.g) * c111.b;
        }
    } else {
        if (d.b > d.g) {
            const struct rgbvec c001 = lut3d->lut[prev[0] * lutsize2 + prev[1] * lutsize + next[2]];
            const struct rgbvec c011 = lut3d->lut[prev[0] * lutsize2 + next[1] * lutsize + next[2]];
            c.r = (1-d.b) * c000.r + (d.b-d.g) * c001.r + (d.g-d.r) * c011.r + (d.r) * c111.r;
            c.g = (1-d.b) * c000.g + (d.b-d.g) * c001.g + (d.g-d.r) * c011.g + (d.r) * c111.g;
            c.b = (1-d.b) * c000.b + (d.b-d.g) * c001.b + (d.g-d.r) * c011.b + (d.r) * c111.b;
        } else if (d.b > d.r) {
            const struct rgbvec c010 = lut3d->lut[prev[0] * lutsize2 + next[1] * lutsize + prev[2]];
            const struct rgbvec c011 = lut3d->lut[prev[0] * lutsize2 + next[1] * lutsize + next[2]];
            c.r = (1-d.g) * c000.r + (d.g-d.b) * c010.r + (d.b-d.r) * c011.r + (d.r) * c111.r;
            c.g = (1-d.g) * c000.g + (d.g-d.b) * c010.g + (d.b-d.r) * c011.g + (d.r) * c111.g;
            c.b = (1-d.g) * c000.b + (d.g-d.b) * c010.b + (d.b-d.r) * c011.b + (d.r) * c111.b;
        } else {
            const struct rgbvec c010 = lut3d->lut[prev[0] * lutsize2 + next[1] * lutsize + prev[2]];
            const struct rgbvec c110 = lut3d->lut[next[0] * lutsize2 + next[1] * lutsize + prev[2]];
            c.r = (1-d.g) * c000.r + (d.g-d.r) * c010.r + (d.r-d.b) * c110.r + (d.b) * c111.r;
            c.g = (1-d.g) * c000.g + (d.g-d.r) * c010.g + (d.r-d.b) * c110.g + (d.b) * c111.g;
            c.b = (1-d.g) * c000.b + (d.g-d.r) * c010.b + (d.r-d.b) * c110.b + (d.b) * c111.b;
        }
    }
    return c;
}

static inline float prelut_interp_1d_linear(const Lut3DPreLut *prelut,
                                            int idx, const float s)
{
    const int lut_max = prelut->size - 1;
    const float scaled = (s - prelut->min[idx]) * prelut->scale[idx];
    const float x = av_clipf(scaled, 0.0f, lut_max);
    const int prev = PREV(x);
    const int next = FFMIN((int)(x) + 1, lut_max);
    const float p = prelut->lut[idx][prev];
    const float n = prelut->lut[idx][next];
    const float d = x - (float)prev;
    return lerpf(p, n, d);
}

static inline struct rgbvec apply_prelut(const Lut3DPreLut *prelut,
                                         const struct rgbvec *s)
{
    struct rgbvec c;

    if (prelut->size <= 0)
        return *s;

    c.r = prelut_interp_1d_linear(prelut, 0, s->r);
    c.g = prelut_interp_1d_linear(prelut, 1, s->g);
    c.b = prelut_interp_1d_linear(prelut, 2, s->b);
    return c;
}

#define DEFINE_INTERP_FUNC_PLANAR(name, nbits, depth)                                                  \
static int interp_##nbits##_##name##_p##depth(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs) \
{                                                                                                      \
    int x, y;                                                                                          \
    const LUT3DContext *lut3d = ctx->priv;                                                             \
    const Lut3DPreLut *prelut = &lut3d->prelut;                                                        \
    const ThreadData *td = arg;                                                                        \
    const AVFrame *in  = td->in;                                                                       \
    const AVFrame *out = td->out;                                                                      \
    const int direct = out == in;                                                                      \
    const int slice_start = (in->height *  jobnr   ) / nb_jobs;                                        \
    const int slice_end   = (in->height * (jobnr+1)) / nb_jobs;                                        \
    uint8_t *grow = out->data[0] + slice_start * out->linesize[0];                                     \
    uint8_t *brow = out->data[1] + slice_start * out->linesize[1];                                     \
    uint8_t *rrow = out->data[2] + slice_start * out->linesize[2];                                     \
    uint8_t *arow = out->data[3] + slice_start * out->linesize[3];                                     \
    const uint8_t *srcgrow = in->data[0] + slice_start * in->linesize[0];                              \
    const uint8_t *srcbrow = in->data[1] + slice_start * in->linesize[1];                              \
    const uint8_t *srcrrow = in->data[2] + slice_start * in->linesize[2];                              \
    const uint8_t *srcarow = in->data[3] + slice_start * in->linesize[3];                              \
    const float lut_max = lut3d->lutsize - 1;                                                          \
    const float scale_f = 1.0f / ((1<<depth) - 1);                                                     \
    const float scale_r = lut3d->scale.r * lut_max;                                                    \
    const float scale_g = lut3d->scale.g * lut_max;                                                    \
    const float scale_b = lut3d->scale.b * lut_max;                                                    \
                                                                                                       \
    for (y = slice_start; y < slice_end; y++) {                                                        \
        uint##nbits##_t *dstg = (uint##nbits##_t *)grow;                                               \
        uint##nbits##_t *dstb = (uint##nbits##_t *)brow;                                               \
        uint##nbits##_t *dstr = (uint##nbits##_t *)rrow;                                               \
        uint##nbits##_t *dsta = (uint##nbits##_t *)arow;                                               \
        const uint##nbits##_t *srcg = (const uint##nbits##_t *)srcgrow;                                \
        const uint##nbits##_t *srcb = (const uint##nbits##_t *)srcbrow;                                \
        const uint##nbits##_t *srcr = (const uint##nbits##_t *)srcrrow;                                \
        const uint##nbits##_t *srca = (const uint##nbits##_t *)srcarow;                                \
        for (x = 0; x < in->width; x++) {                                                              \
            const struct rgbvec rgb = {srcr[x] * scale_f,                                              \
                                       srcg[x] * scale_f,                                              \
                                       srcb[x] * scale_f};                                             \
            const struct rgbvec prelut_rgb = apply_prelut(prelut, &rgb);                               \
            const struct rgbvec scaled_rgb = {av_clipf(prelut_rgb.r * scale_r, 0, lut_max),            \
                                              av_clipf(prelut_rgb.g * scale_g, 0, lut_max),            \
                                              av_clipf(prelut_rgb.b * scale_b, 0, lut_max)};           \
            struct rgbvec vec = interp_##name(lut3d, &scaled_rgb);                                     \
            dstr[x] = av_clip_uintp2(vec.r * (float)((1<<depth) - 1), depth);                          \
            dstg[x] = av_clip_uintp2(vec.g * (float)((1<<depth) - 1), depth);                          \
            dstb[x] = av_clip_uintp2(vec.b * (float)((1<<depth) - 1), depth);                          \
            if (!direct && in->linesize[3])                                                            \
                dsta[x] = srca[x];                                                                     \
        }                                                                                              \
        grow += out->linesize[0];                                                                      \
        brow += out->linesize[1];                                                                      \
        rrow += out->linesize[2];                                                                      \
        arow += out->linesize[3];                                                                      \
        srcgrow += in->linesize[0];                                                                    \
        srcbrow += in->linesize[1];                                                                    \
        srcrrow += in->linesize[2];                                                                    \
        srcarow += in->linesize[3];                                                                    \
    }                                                                                                  \
    return 0;                                                                                          \
}

DEFINE_INTERP_FUNC_PLANAR(nearest,     8, 8)
DEFINE_INTERP_FUNC_PLANAR(trilinear,   8, 8)
DEFINE_INTERP_FUNC_PLANAR(tetrahedral, 8, 8)

DEFINE_INTERP_FUNC_PLANAR(nearest,     16, 9)
DEFINE_INTERP_FUNC_PLANAR(trilinear,   16, 9)
DEFINE_INTERP_FUNC_PLANAR(tetrahedral, 16, 9)

DEFINE_INTERP_FUNC_PLANAR(nearest,     16, 10)
DEFINE_INTERP_FUNC_PLANAR(trilinear,   16, 10)
DEFINE_INTERP_FUNC_PLANAR(tetrahedral, 16, 10)

DEFINE_INTERP_FUNC_PLANAR(nearest,     16, 12)
DEFINE_INTERP_FUNC_PLANAR(trilinear,   16, 12)
DEFINE_INTERP_FUNC_PLANAR(tetrahedral, 16, 12)

DEFINE_INTERP_FUNC_PLANAR(nearest,     16, 14)
DEFINE_INTERP_FUNC_PLANAR(trilinear,   16, 14)
DEFINE_INTERP_FUNC_PLANAR(tetrahedral, 16, 14)

DEFINE_INTERP_FUNC_PLANAR(nearest,     16, 16)
DEFINE_INTERP_FUNC_PLANAR(trilinear,   16, 16)
DEFINE_INTERP_FUNC_PLANAR(tetrahedral, 16, 16)

#define DEFINE_INTERP_FUNC_PLANAR_FLOAT(name, depth)                                                   \
static int interp_##name##_pf##depth(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)          \
{                                                                                                      \
    int x, y;                                                                                          \
    const LUT3DContext *lut3d = ctx->priv;                                                             \
    const Lut3DPreLut *prelut = &lut3d->prelut;                                                        \
    const ThreadData *td = arg;                                                                        \
    const AVFrame *in  = td->in;                                                                       \
    const AVFrame *out = td->out;                                                                      \
    const int direct = out == in;                                                                      \
    const int slice_start = (in->height *  jobnr   ) / nb_jobs;                                        \
    const int slice_end   = (in->height * (jobnr+1)) / nb_jobs;                                        \
    uint8_t *grow = out->data[0] + slice_start * out->linesize[0];                                     \
    uint8_t *brow = out->data[1] + slice_start * out->linesize[1];                                     \
    uint8_t *rrow = out->data[2] + slice_start * out->linesize[2];                                     \
    uint8_t *arow = out->data[3] + slice_start * out->linesize[3];                                     \
    const uint8_t *srcgrow = in->data[0] + slice_start * in->linesize[0];                              \
    const uint8_t *srcbrow = in->data[1] + slice_start * in->linesize[1];                              \
    const uint8_t *srcrrow = in->data[2] + slice_start * in->linesize[2];                              \
    const uint8_t *srcarow = in->data[3] + slice_start * in->linesize[3];                              \
    const float lut_max = lut3d->lutsize - 1;                                                          \
    const float scale_r = lut3d->scale.r * lut_max;                                                    \
    const float scale_g = lut3d->scale.g * lut_max;                                                    \
    const float scale_b = lut3d->scale.b * lut_max;                                                    \
                                                                                                       \
    for (y = slice_start; y < slice_end; y++) {                                                        \
        float *dstg = (float *)grow;                                                                   \
        float *dstb = (float *)brow;                                                                   \
        float *dstr = (float *)rrow;                                                                   \
        float *dsta = (float *)arow;                                                                   \
        const float *srcg = (const float *)srcgrow;                                                    \
        const float *srcb = (const float *)srcbrow;                                                    \
        const float *srcr = (const float *)srcrrow;                                                    \
        const float *srca = (const float *)srcarow;                                                    \
        for (x = 0; x < in->width; x++) {                                                              \
            const struct rgbvec rgb = {sanitizef(srcr[x]),                                             \
                                       sanitizef(srcg[x]),                                             \
                                       sanitizef(srcb[x])};                                            \
            const struct rgbvec prelut_rgb = apply_prelut(prelut, &rgb);                               \
            const struct rgbvec scaled_rgb = {av_clipf(prelut_rgb.r * scale_r, 0, lut_max),            \
                                              av_clipf(prelut_rgb.g * scale_g, 0, lut_max),            \
                                              av_clipf(prelut_rgb.b * scale_b, 0, lut_max)};           \
            struct rgbvec vec = interp_##name(lut3d, &scaled_rgb);                                     \
            dstr[x] = vec.r;                                                                           \
            dstg[x] = vec.g;                                                                           \
            dstb[x] = vec.b;                                                                           \
            if (!direct && in->linesize[3])                                                            \
                dsta[x] = srca[x];                                                                     \
        }                                                                                              \
        grow += out->linesize[0];                                                                      \
        brow += out->linesize[1];                                                                      \
        rrow += out->linesize[2];                                                                      \
        arow += out->linesize[3];                                                                      \
        srcgrow += in->linesize[0];                                                                    \
        srcbrow += in->linesize[1];                                                                    \
        srcrrow += in->linesize[2];                                                                    \
        srcarow += in->linesize[3];                                                                    \
    }                                                                                                  \
    return 0;                                                                                          \
}

DEFINE_INTERP_FUNC_PLANAR_FLOAT(nearest,     32)
DEFINE_INTERP_FUNC_PLANAR_FLOAT(trilinear,   32)
DEFINE_INTERP_FUNC_PLANAR_FLOAT(tetrahedral, 32)

#define DEFINE_INTERP_FUNC(name, nbits)                                                             \
static int interp_##nbits##_##name(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)         \
{                                                                                                   \
    int x, y;                                                                                       \
    const LUT3DContext *lut3d = ctx->priv;                                                          \
    const Lut3DPreLut *prelut = &lut3d->prelut;                                                     \
    const ThreadData *td = arg;                                                                     \
    const AVFrame *in  = td->in;                                                                    \
    const AVFrame *out = td->out;                                                                   \
    const int direct = out == in;                                                                   \
    const int step = lut3d->step;                                                                   \
    const uint8_t r = lut3d->rgba_map[R];                                                           \
    const uint8_t g = lut3d->rgba_map[G];                                                           \
    const uint8_t b = lut3d->rgba_map[B];                                                           \
    const uint8_t a = lut3d->rgba_map[A];                                                           \
    const int slice_start = (in->height *  jobnr   ) / nb_jobs;                                     \
    const int slice_end   = (in->height * (jobnr+1)) / nb_jobs;                                     \
    uint8_t       *dstrow = out->data[0] + slice_start * out->linesize[0];                          \
    const uint8_t *srcrow = in ->data[0] + slice_start * in ->linesize[0];                          \
    const float lut_max = lut3d->lutsize - 1;                                                       \
    const float scale_f = 1.0f / ((1<<nbits) - 1);                                                  \
    const float scale_r = lut3d->scale.r * lut_max;                                                 \
    const float scale_g = lut3d->scale.g * lut_max;                                                 \
    const float scale_b = lut3d->scale.b * lut_max;                                                 \
                                                                                                    \
    for (y = slice_start; y < slice_end; y++) {                                                     \
        uint##nbits##_t *dst = (uint##nbits##_t *)dstrow;                                           \
        const uint##nbits##_t *src = (const uint##nbits##_t *)srcrow;                               \
        for (x = 0; x < in->width * step; x += step) {                                              \
            const struct rgbvec rgb = {src[x + r] * scale_f,                                        \
                                       src[x + g] * scale_f,                                        \
                                       src[x + b] * scale_f};                                       \
            const struct rgbvec prelut_rgb = apply_prelut(prelut, &rgb);                            \
            const struct rgbvec scaled_rgb = {av_clipf(prelut_rgb.r * scale_r, 0, lut_max),         \
                                              av_clipf(prelut_rgb.g * scale_g, 0, lut_max),         \
                                              av_clipf(prelut_rgb.b * scale_b, 0, lut_max)};        \
            struct rgbvec vec = interp_##name(lut3d, &scaled_rgb);                                  \
            dst[x + r] = av_clip_uint##nbits(vec.r * (float)((1<<nbits) - 1));                      \
            dst[x + g] = av_clip_uint##nbits(vec.g * (float)((1<<nbits) - 1));                      \
            dst[x + b] = av_clip_uint##nbits(vec.b * (float)((1<<nbits) - 1));                      \
            if (!direct && step == 4)                                                               \
                dst[x + a] = src[x + a];                                                            \
        }                                                                                           \
        dstrow += out->linesize[0];                                                                 \
        srcrow += in ->linesize[0];                                                                 \
    }                                                                                               \
    return 0;                                                                                       \
}

DEFINE_INTERP_FUNC(nearest,     8)
DEFINE_INTERP_FUNC(trilinear,   8)
DEFINE_INTERP_FUNC(tetrahedral, 8)

DEFINE_INTERP_FUNC(nearest,     16)
DEFINE_INTERP_FUNC(trilinear,   16)
DEFINE_INTERP_FUNC(tetrahedral, 16)

#define MAX_LINE_SIZE 512

static int skip_line(const char *p)
{
    while (*p && av_isspace(*p))
        p++;
    return !*p || *p == '#';
}

static char* fget_next_word(char* dst, int max, FILE* f)
{
    int c;
    char *p = dst;

    /* for null */
    max--;
    /* skip until next non whitespace char */
    while ((c = fgetc(f)) != EOF) {
        if (av_isspace(c))
            continue;

        *p++ = c;
        max--;
        break;
    }

    /* get max bytes or up until next whitespace char */
    for (; max > 0; max--) {
        if ((c = fgetc(f)) == EOF)
            break;

        if (av_isspace(c))
            break;

        *p++ = c;
    }

    *p = 0;
    if (p == dst)
        return NULL;
    return p;
}

#define NEXT_LINE(loop_cond) do {                           \
    if (!fgets(line, sizeof(line), f)) {                    \
        av_log(ctx, AV_LOG_ERROR, "Unexpected EOF\n");      \
        return AVERROR_INVALIDDATA;                         \
    }                                                       \
} while (loop_cond)

#define NEXT_LINE_OR_GOTO(loop_cond, label) do {            \
    if (!fgets(line, sizeof(line), f)) {                    \
        av_log(ctx, AV_LOG_ERROR, "Unexpected EOF\n");      \
        ret = AVERROR_INVALIDDATA;                          \
        goto label;                                         \
    }                                                       \
} while (loop_cond)

static int allocate_3dlut(AVFilterContext *ctx, int lutsize, int prelut)
{
    LUT3DContext *lut3d = ctx->priv;
    int i;
    if (lutsize < 2 || lutsize > MAX_LEVEL) {
        av_log(ctx, AV_LOG_ERROR, "Too large or invalid 3D LUT size\n");
        return AVERROR(EINVAL);
    }

    av_freep(&lut3d->lut);
    lut3d->lut = av_malloc_array(lutsize * lutsize * lutsize, sizeof(*lut3d->lut));
    if (!lut3d->lut)
        return AVERROR(ENOMEM);

    if (prelut) {
        lut3d->prelut.size = PRELUT_SIZE;
        for (i = 0; i < 3; i++) {
            av_freep(&lut3d->prelut.lut[i]);
            lut3d->prelut.lut[i] = av_malloc_array(PRELUT_SIZE, sizeof(*lut3d->prelut.lut[0]));
            if (!lut3d->prelut.lut[i])
                return AVERROR(ENOMEM);
        }
    } else {
        lut3d->prelut.size = 0;
        for (i = 0; i < 3; i++) {
            av_freep(&lut3d->prelut.lut[i]);
        }
    }
    lut3d->lutsize = lutsize;
    lut3d->lutsize2 = lutsize * lutsize;
    return 0;
}

/* Basically r g and b float values on each line, with a facultative 3DLUTSIZE
 * directive; seems to be generated by Davinci */
static int parse_dat(AVFilterContext *ctx, FILE *f)
{
    LUT3DContext *lut3d = ctx->priv;
    char line[MAX_LINE_SIZE];
    int ret, i, j, k, size, size2;

    lut3d->lutsize = size = 33;
    size2 = size * size;

    NEXT_LINE(skip_line(line));
    if (!strncmp(line, "3DLUTSIZE ", 10)) {
        size = strtol(line + 10, NULL, 0);

        NEXT_LINE(skip_line(line));
    }

    ret = allocate_3dlut(ctx, size, 0);
    if (ret < 0)
        return ret;

    for (k = 0; k < size; k++) {
        for (j = 0; j < size; j++) {
            for (i = 0; i < size; i++) {
                struct rgbvec *vec = &lut3d->lut[k * size2 + j * size + i];
                if (k != 0 || j != 0 || i != 0)
                    NEXT_LINE(skip_line(line));
                if (av_sscanf(line, "%f %f %f", &vec->r, &vec->g, &vec->b) != 3)
                    return AVERROR_INVALIDDATA;
            }
        }
    }
    return 0;
}

/* Iridas format */
static int parse_cube(AVFilterContext *ctx, FILE *f)
{
    LUT3DContext *lut3d = ctx->priv;
    char line[MAX_LINE_SIZE];
    float min[3] = {0.0, 0.0, 0.0};
    float max[3] = {1.0, 1.0, 1.0};

    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, "LUT_3D_SIZE", 11)) {
            int ret, i, j, k;
            const int size = strtol(line + 12, NULL, 0);
            const int size2 = size * size;

            ret = allocate_3dlut(ctx, size, 0);
            if (ret < 0)
                return ret;

            for (k = 0; k < size; k++) {
                for (j = 0; j < size; j++) {
                    for (i = 0; i < size; i++) {
                        struct rgbvec *vec = &lut3d->lut[i * size2 + j * size + k];

                        do {
try_again:
                            NEXT_LINE(0);
                            if (!strncmp(line, "DOMAIN_", 7)) {
                                float *vals = NULL;
                                if      (!strncmp(line + 7, "MIN ", 4)) vals = min;
                                else if (!strncmp(line + 7, "MAX ", 4)) vals = max;
                                if (!vals)
                                    return AVERROR_INVALIDDATA;
                                av_sscanf(line + 11, "%f %f %f", vals, vals + 1, vals + 2);
                                av_log(ctx, AV_LOG_DEBUG, "min: %f %f %f | max: %f %f %f\n",
                                       min[0], min[1], min[2], max[0], max[1], max[2]);
                                goto try_again;
                            } else if (!strncmp(line, "TITLE", 5)) {
                                goto try_again;
                            }
                        } while (skip_line(line));
                        if (av_sscanf(line, "%f %f %f", &vec->r, &vec->g, &vec->b) != 3)
                            return AVERROR_INVALIDDATA;
                    }
                }
            }
            break;
        }
    }

    lut3d->scale.r = av_clipf(1. / (max[0] - min[0]), 0.f, 1.f);
    lut3d->scale.g = av_clipf(1. / (max[1] - min[1]), 0.f, 1.f);
    lut3d->scale.b = av_clipf(1. / (max[2] - min[2]), 0.f, 1.f);

    return 0;
}

/* Assume 17x17x17 LUT with a 16-bit depth
 * FIXME: it seems there are various 3dl formats */
static int parse_3dl(AVFilterContext *ctx, FILE *f)
{
    char line[MAX_LINE_SIZE];
    LUT3DContext *lut3d = ctx->priv;
    int ret, i, j, k;
    const int size = 17;
    const int size2 = 17 * 17;
    const float scale = 16*16*16;

    lut3d->lutsize = size;

    ret = allocate_3dlut(ctx, size, 0);
    if (ret < 0)
        return ret;

    NEXT_LINE(skip_line(line));
    for (k = 0; k < size; k++) {
        for (j = 0; j < size; j++) {
            for (i = 0; i < size; i++) {
                int r, g, b;
                struct rgbvec *vec = &lut3d->lut[k * size2 + j * size + i];

                NEXT_LINE(skip_line(line));
                if (av_sscanf(line, "%d %d %d", &r, &g, &b) != 3)
                    return AVERROR_INVALIDDATA;
                vec->r = r / scale;
                vec->g = g / scale;
                vec->b = b / scale;
            }
        }
    }
    return 0;
}

/* Pandora format */
static int parse_m3d(AVFilterContext *ctx, FILE *f)
{
    LUT3DContext *lut3d = ctx->priv;
    float scale;
    int ret, i, j, k, size, size2, in = -1, out = -1;
    char line[MAX_LINE_SIZE];
    uint8_t rgb_map[3] = {0, 1, 2};

    while (fgets(line, sizeof(line), f)) {
        if      (!strncmp(line, "in",  2)) in  = strtol(line + 2, NULL, 0);
        else if (!strncmp(line, "out", 3)) out = strtol(line + 3, NULL, 0);
        else if (!strncmp(line, "values", 6)) {
            const char *p = line + 6;
#define SET_COLOR(id) do {                  \
    while (av_isspace(*p))                  \
        p++;                                \
    switch (*p) {                           \
    case 'r': rgb_map[id] = 0; break;       \
    case 'g': rgb_map[id] = 1; break;       \
    case 'b': rgb_map[id] = 2; break;       \
    }                                       \
    while (*p && !av_isspace(*p))           \
        p++;                                \
} while (0)
            SET_COLOR(0);
            SET_COLOR(1);
            SET_COLOR(2);
            break;
        }
    }

    if (in == -1 || out == -1) {
        av_log(ctx, AV_LOG_ERROR, "in and out must be defined\n");
        return AVERROR_INVALIDDATA;
    }
    if (in < 2 || out < 2 ||
        in  > MAX_LEVEL*MAX_LEVEL*MAX_LEVEL ||
        out > MAX_LEVEL*MAX_LEVEL*MAX_LEVEL) {
        av_log(ctx, AV_LOG_ERROR, "invalid in (%d) or out (%d)\n", in, out);
        return AVERROR_INVALIDDATA;
    }
    for (size = 1; size*size*size < in; size++);
    lut3d->lutsize = size;
    size2 = size * size;

    ret = allocate_3dlut(ctx, size, 0);
    if (ret < 0)
        return ret;

    scale = 1. / (out - 1);

    for (k = 0; k < size; k++) {
        for (j = 0; j < size; j++) {
            for (i = 0; i < size; i++) {
                struct rgbvec *vec = &lut3d->lut[k * size2 + j * size + i];
                float val[3];

                NEXT_LINE(0);
                if (av_sscanf(line, "%f %f %f", val, val + 1, val + 2) != 3)
                    return AVERROR_INVALIDDATA;
                vec->r = val[rgb_map[0]] * scale;
                vec->g = val[rgb_map[1]] * scale;
                vec->b = val[rgb_map[2]] * scale;
            }
        }
    }
    return 0;
}

static int nearest_sample_index(float *data, float x, int low, int hi)
{
    int mid;
    if (x < data[low])
        return low;

    if (x > data[hi])
        return hi;

    for (;;) {
        av_assert0(x >= data[low]);
        av_assert0(x <= data[hi]);
        av_assert0((hi-low) > 0);

        if (hi - low == 1)
            return low;

        mid = (low + hi) / 2;

        if (x < data[mid])
            hi = mid;
        else
            low = mid;
    }

    return 0;
}

#define NEXT_FLOAT_OR_GOTO(value, label)                    \
    if (!fget_next_word(line, sizeof(line) ,f)) {           \
        ret = AVERROR_INVALIDDATA;                          \
        goto label;                                         \
    }                                                       \
    if (av_sscanf(line, "%f", &value) != 1) {               \
        ret = AVERROR_INVALIDDATA;                          \
        goto label;                                         \
    }

static int parse_cinespace(AVFilterContext *ctx, FILE *f)
{
    LUT3DContext *lut3d = ctx->priv;
    char line[MAX_LINE_SIZE];
    float in_min[3]  = {0.0, 0.0, 0.0};
    float in_max[3]  = {1.0, 1.0, 1.0};
    float out_min[3] = {0.0, 0.0, 0.0};
    float out_max[3] = {1.0, 1.0, 1.0};
    int inside_metadata = 0, size, size2;
    int prelut = 0;
    int ret = 0;

    int prelut_sizes[3] = {0, 0, 0};
    float *in_prelut[3]  = {NULL, NULL, NULL};
    float *out_prelut[3] = {NULL, NULL, NULL};

    NEXT_LINE_OR_GOTO(skip_line(line), end);
    if (strncmp(line, "CSPLUTV100", 10)) {
        av_log(ctx, AV_LOG_ERROR, "Not cineSpace LUT format\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    NEXT_LINE_OR_GOTO(skip_line(line), end);
    if (strncmp(line, "3D", 2)) {
        av_log(ctx, AV_LOG_ERROR, "Not 3D LUT format\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    while (1) {
        NEXT_LINE_OR_GOTO(skip_line(line), end);

        if (!strncmp(line, "BEGIN METADATA", 14)) {
            inside_metadata = 1;
            continue;
        }
        if (!strncmp(line, "END METADATA", 12)) {
            inside_metadata = 0;
            continue;
        }
        if (inside_metadata == 0) {
            int size_r, size_g, size_b;

            for (int i = 0; i < 3; i++) {
                int npoints = strtol(line, NULL, 0);

                if (npoints > 2) {
                    float v,last;

                    if (npoints > PRELUT_SIZE) {
                        av_log(ctx, AV_LOG_ERROR, "Prelut size too large.\n");
                        ret = AVERROR_INVALIDDATA;
                        goto end;
                    }

                    if (in_prelut[i] || out_prelut[i]) {
                        av_log(ctx, AV_LOG_ERROR, "Invalid file has multiple preluts.\n");
                        ret = AVERROR_INVALIDDATA;
                        goto end;
                    }

                    in_prelut[i]  = (float*)av_malloc(npoints * sizeof(float));
                    out_prelut[i] = (float*)av_malloc(npoints * sizeof(float));
                    if (!in_prelut[i] || !out_prelut[i]) {
                        ret = AVERROR(ENOMEM);
                        goto end;
                    }

                    prelut_sizes[i] = npoints;
                    in_min[i] = FLT_MAX;
                    in_max[i] = FLT_MIN;
                    out_min[i] = FLT_MAX;
                    out_max[i] = FLT_MIN;

                    last = FLT_MIN;

                    for (int j = 0; j < npoints; j++) {
                        NEXT_FLOAT_OR_GOTO(v, end)
                        in_min[i] = FFMIN(in_min[i], v);
                        in_max[i] = FFMAX(in_max[i], v);
                        in_prelut[i][j] = v;
                        if (v < last) {
                            av_log(ctx, AV_LOG_ERROR, "Invalid file, non increasing prelut.\n");
                            ret = AVERROR(ENOMEM);
                            goto end;
                        }
                        last = v;
                    }

                    for (int j = 0; j < npoints; j++) {
                        NEXT_FLOAT_OR_GOTO(v, end)
                        out_min[i] = FFMIN(out_min[i], v);
                        out_max[i] = FFMAX(out_max[i], v);
                        out_prelut[i][j] = v;
                    }

                } else if (npoints == 2)  {
                    NEXT_LINE_OR_GOTO(skip_line(line), end);
                    if (av_sscanf(line, "%f %f", &in_min[i], &in_max[i]) != 2) {
                        ret = AVERROR_INVALIDDATA;
                        goto end;
                    }
                    NEXT_LINE_OR_GOTO(skip_line(line), end);
                    if (av_sscanf(line, "%f %f", &out_min[i], &out_max[i]) != 2) {
                        ret = AVERROR_INVALIDDATA;
                        goto end;
                    }

                } else {
                    av_log(ctx, AV_LOG_ERROR, "Unsupported number of pre-lut points.\n");
                    ret = AVERROR_PATCHWELCOME;
                    goto end;
                }

                NEXT_LINE_OR_GOTO(skip_line(line), end);
            }

            if (av_sscanf(line, "%d %d %d", &size_r, &size_g, &size_b) != 3) {
                ret = AVERROR(EINVAL);
                goto end;
            }
            if (size_r != size_g || size_r != size_b) {
                av_log(ctx, AV_LOG_ERROR, "Unsupported size combination: %dx%dx%d.\n", size_r, size_g, size_b);
                ret = AVERROR_PATCHWELCOME;
                goto end;
            }

            size = size_r;
            size2 = size * size;

            if (prelut_sizes[0] && prelut_sizes[1] && prelut_sizes[2])
                prelut = 1;

            ret = allocate_3dlut(ctx, size, prelut);
            if (ret < 0)
                return ret;

            for (int k = 0; k < size; k++) {
                for (int j = 0; j < size; j++) {
                    for (int i = 0; i < size; i++) {
                        struct rgbvec *vec = &lut3d->lut[i * size2 + j * size + k];

                        NEXT_LINE_OR_GOTO(skip_line(line), end);
                        if (av_sscanf(line, "%f %f %f", &vec->r, &vec->g, &vec->b) != 3) {
                            ret = AVERROR_INVALIDDATA;
                            goto end;
                        }

                        vec->r *= out_max[0] - out_min[0];
                        vec->g *= out_max[1] - out_min[1];
                        vec->b *= out_max[2] - out_min[2];
                    }
                }
            }

            break;
        }
    }

    if (prelut) {
        for (int c = 0; c < 3; c++) {

            lut3d->prelut.min[c] = in_min[c];
            lut3d->prelut.max[c] = in_max[c];
            lut3d->prelut.scale[c] =  (1.0f / (float)(in_max[c] - in_min[c])) * (lut3d->prelut.size - 1);

            for (int i = 0; i < lut3d->prelut.size; ++i) {
                float mix = (float) i / (float)(lut3d->prelut.size - 1);
                float x = lerpf(in_min[c], in_max[c], mix), a, b;

                int idx = nearest_sample_index(in_prelut[c], x, 0, prelut_sizes[c]-1);
                av_assert0(idx + 1 < prelut_sizes[c]);

                a   = out_prelut[c][idx + 0];
                b   = out_prelut[c][idx + 1];
                mix = x - in_prelut[c][idx];

                lut3d->prelut.lut[c][i] = sanitizef(lerpf(a, b, mix));
            }
        }
        lut3d->scale.r = 1.00f;
        lut3d->scale.g = 1.00f;
        lut3d->scale.b = 1.00f;

    } else {
        lut3d->scale.r = av_clipf(1. / (in_max[0] - in_min[0]), 0.f, 1.f);
        lut3d->scale.g = av_clipf(1. / (in_max[1] - in_min[1]), 0.f, 1.f);
        lut3d->scale.b = av_clipf(1. / (in_max[2] - in_min[2]), 0.f, 1.f);
    }

end:
    for (int c = 0; c < 3; c++) {
        av_freep(&in_prelut[c]);
        av_freep(&out_prelut[c]);
    }
    return ret;
}

static int set_identity_matrix(AVFilterContext *ctx, int size)
{
    LUT3DContext *lut3d = ctx->priv;
    int ret, i, j, k;
    const int size2 = size * size;
    const float c = 1. / (size - 1);

    ret = allocate_3dlut(ctx, size, 0);
    if (ret < 0)
        return ret;

    for (k = 0; k < size; k++) {
        for (j = 0; j < size; j++) {
            for (i = 0; i < size; i++) {
                struct rgbvec *vec = &lut3d->lut[k * size2 + j * size + i];
                vec->r = k * c;
                vec->g = j * c;
                vec->b = i * c;
            }
        }
    }

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_RGB24,  AV_PIX_FMT_BGR24,
        AV_PIX_FMT_RGBA,   AV_PIX_FMT_BGRA,
        AV_PIX_FMT_ARGB,   AV_PIX_FMT_ABGR,
        AV_PIX_FMT_0RGB,   AV_PIX_FMT_0BGR,
        AV_PIX_FMT_RGB0,   AV_PIX_FMT_BGR0,
        AV_PIX_FMT_RGB48,  AV_PIX_FMT_BGR48,
        AV_PIX_FMT_RGBA64, AV_PIX_FMT_BGRA64,
        AV_PIX_FMT_GBRP,   AV_PIX_FMT_GBRAP,
        AV_PIX_FMT_GBRP9,
        AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRAP10,
        AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRAP12,
        AV_PIX_FMT_GBRP14,
        AV_PIX_FMT_GBRP16,  AV_PIX_FMT_GBRAP16,
        AV_PIX_FMT_GBRPF32, AV_PIX_FMT_GBRAPF32,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int config_input(AVFilterLink *inlink)
{
    int depth, is16bit, isfloat, planar;
    LUT3DContext *lut3d = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    depth = desc->comp[0].depth;
    is16bit = desc->comp[0].depth > 8;
    planar = desc->flags & AV_PIX_FMT_FLAG_PLANAR;
    isfloat = desc->flags & AV_PIX_FMT_FLAG_FLOAT;
    ff_fill_rgba_map(lut3d->rgba_map, inlink->format);
    lut3d->step = av_get_padded_bits_per_pixel(desc) >> (3 + is16bit);

#define SET_FUNC(name) do {                                     \
    if (planar && !isfloat) {                                   \
        switch (depth) {                                        \
        case  8: lut3d->interp = interp_8_##name##_p8;   break; \
        case  9: lut3d->interp = interp_16_##name##_p9;  break; \
        case 10: lut3d->interp = interp_16_##name##_p10; break; \
        case 12: lut3d->interp = interp_16_##name##_p12; break; \
        case 14: lut3d->interp = interp_16_##name##_p14; break; \
        case 16: lut3d->interp = interp_16_##name##_p16; break; \
        }                                                       \
    } else if (isfloat) { lut3d->interp = interp_##name##_pf32; \
    } else if (is16bit) { lut3d->interp = interp_16_##name;     \
    } else {       lut3d->interp = interp_8_##name; }           \
} while (0)

    switch (lut3d->interpolation) {
    case INTERPOLATE_NEAREST:     SET_FUNC(nearest);        break;
    case INTERPOLATE_TRILINEAR:   SET_FUNC(trilinear);      break;
    case INTERPOLATE_TETRAHEDRAL: SET_FUNC(tetrahedral);    break;
    default:
        av_assert0(0);
    }

    return 0;
}

static AVFrame *apply_lut(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    LUT3DContext *lut3d = ctx->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFrame *out;
    ThreadData td;

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return NULL;
        }
        av_frame_copy_props(out, in);
    }

    td.in  = in;
    td.out = out;
    ctx->internal->execute(ctx, lut3d->interp, &td, NULL, FFMIN(outlink->h, ff_filter_get_nb_threads(ctx)));

    if (out != in)
        av_frame_free(&in);

    return out;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFrame *out = apply_lut(inlink, in);
    if (!out)
        return AVERROR(ENOMEM);
    return ff_filter_frame(outlink, out);
}

#if CONFIG_LUT3D_FILTER
static const AVOption lut3d_options[] = {
    { "file", "set 3D LUT file name", OFFSET(file), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    COMMON_OPTIONS
};

AVFILTER_DEFINE_CLASS(lut3d);

static av_cold int lut3d_init(AVFilterContext *ctx)
{
    int ret;
    FILE *f;
    const char *ext;
    LUT3DContext *lut3d = ctx->priv;

    lut3d->scale.r = lut3d->scale.g = lut3d->scale.b = 1.f;

    if (!lut3d->file) {
        return set_identity_matrix(ctx, 32);
    }

    f = av_fopen_utf8(lut3d->file, "r");
    if (!f) {
        ret = AVERROR(errno);
        av_log(ctx, AV_LOG_ERROR, "%s: %s\n", lut3d->file, av_err2str(ret));
        return ret;
    }

    ext = strrchr(lut3d->file, '.');
    if (!ext) {
        av_log(ctx, AV_LOG_ERROR, "Unable to guess the format from the extension\n");
        ret = AVERROR_INVALIDDATA;
        goto end;
    }
    ext++;

    if (!av_strcasecmp(ext, "dat")) {
        ret = parse_dat(ctx, f);
    } else if (!av_strcasecmp(ext, "3dl")) {
        ret = parse_3dl(ctx, f);
    } else if (!av_strcasecmp(ext, "cube")) {
        ret = parse_cube(ctx, f);
    } else if (!av_strcasecmp(ext, "m3d")) {
        ret = parse_m3d(ctx, f);
    } else if (!av_strcasecmp(ext, "csp")) {
        ret = parse_cinespace(ctx, f);
    } else {
        av_log(ctx, AV_LOG_ERROR, "Unrecognized '.%s' file type\n", ext);
        ret = AVERROR(EINVAL);
    }

    if (!ret && !lut3d->lutsize) {
        av_log(ctx, AV_LOG_ERROR, "3D LUT is empty\n");
        ret = AVERROR_INVALIDDATA;
    }

end:
    fclose(f);
    return ret;
}

static av_cold void lut3d_uninit(AVFilterContext *ctx)
{
    LUT3DContext *lut3d = ctx->priv;
    int i;
    av_freep(&lut3d->lut);

    for (i = 0; i < 3; i++) {
        av_freep(&lut3d->prelut.lut[i]);
    }
}

static const AVFilterPad lut3d_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad lut3d_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_lut3d = {
    .name          = "lut3d",
    .description   = NULL_IF_CONFIG_SMALL("Adjust colors using a 3D LUT."),
    .priv_size     = sizeof(LUT3DContext),
    .init          = lut3d_init,
    .uninit        = lut3d_uninit,
    .query_formats = query_formats,
    .inputs        = lut3d_inputs,
    .outputs       = lut3d_outputs,
    .priv_class    = &lut3d_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};
#endif

#if CONFIG_HALDCLUT_FILTER

static void update_clut_packed(LUT3DContext *lut3d, const AVFrame *frame)
{
    const uint8_t *data = frame->data[0];
    const int linesize  = frame->linesize[0];
    const int w = lut3d->clut_width;
    const int step = lut3d->clut_step;
    const uint8_t *rgba_map = lut3d->clut_rgba_map;
    const int level = lut3d->lutsize;
    const int level2 = lut3d->lutsize2;

#define LOAD_CLUT(nbits) do {                                           \
    int i, j, k, x = 0, y = 0;                                          \
                                                                        \
    for (k = 0; k < level; k++) {                                       \
        for (j = 0; j < level; j++) {                                   \
            for (i = 0; i < level; i++) {                               \
                const uint##nbits##_t *src = (const uint##nbits##_t *)  \
                    (data + y*linesize + x*step);                       \
                struct rgbvec *vec = &lut3d->lut[i * level2 + j * level + k]; \
                vec->r = src[rgba_map[0]] / (float)((1<<(nbits)) - 1);  \
                vec->g = src[rgba_map[1]] / (float)((1<<(nbits)) - 1);  \
                vec->b = src[rgba_map[2]] / (float)((1<<(nbits)) - 1);  \
                if (++x == w) {                                         \
                    x = 0;                                              \
                    y++;                                                \
                }                                                       \
            }                                                           \
        }                                                               \
    }                                                                   \
} while (0)

    switch (lut3d->clut_bits) {
    case  8: LOAD_CLUT(8);  break;
    case 16: LOAD_CLUT(16); break;
    }
}

static void update_clut_planar(LUT3DContext *lut3d, const AVFrame *frame)
{
    const uint8_t *datag = frame->data[0];
    const uint8_t *datab = frame->data[1];
    const uint8_t *datar = frame->data[2];
    const int glinesize  = frame->linesize[0];
    const int blinesize  = frame->linesize[1];
    const int rlinesize  = frame->linesize[2];
    const int w = lut3d->clut_width;
    const int level = lut3d->lutsize;
    const int level2 = lut3d->lutsize2;

#define LOAD_CLUT_PLANAR(nbits, depth) do {                             \
    int i, j, k, x = 0, y = 0;                                          \
                                                                        \
    for (k = 0; k < level; k++) {                                       \
        for (j = 0; j < level; j++) {                                   \
            for (i = 0; i < level; i++) {                               \
                const uint##nbits##_t *gsrc = (const uint##nbits##_t *) \
                    (datag + y*glinesize);                              \
                const uint##nbits##_t *bsrc = (const uint##nbits##_t *) \
                    (datab + y*blinesize);                              \
                const uint##nbits##_t *rsrc = (const uint##nbits##_t *) \
                    (datar + y*rlinesize);                              \
                struct rgbvec *vec = &lut3d->lut[i * level2 + j * level + k]; \
                vec->r = gsrc[x] / (float)((1<<(depth)) - 1);           \
                vec->g = bsrc[x] / (float)((1<<(depth)) - 1);           \
                vec->b = rsrc[x] / (float)((1<<(depth)) - 1);           \
                if (++x == w) {                                         \
                    x = 0;                                              \
                    y++;                                                \
                }                                                       \
            }                                                           \
        }                                                               \
    }                                                                   \
} while (0)

    switch (lut3d->clut_bits) {
    case  8: LOAD_CLUT_PLANAR(8, 8);   break;
    case  9: LOAD_CLUT_PLANAR(16, 9);  break;
    case 10: LOAD_CLUT_PLANAR(16, 10); break;
    case 12: LOAD_CLUT_PLANAR(16, 12); break;
    case 14: LOAD_CLUT_PLANAR(16, 14); break;
    case 16: LOAD_CLUT_PLANAR(16, 16); break;
    }
}

static void update_clut_float(LUT3DContext *lut3d, const AVFrame *frame)
{
    const uint8_t *datag = frame->data[0];
    const uint8_t *datab = frame->data[1];
    const uint8_t *datar = frame->data[2];
    const int glinesize  = frame->linesize[0];
    const int blinesize  = frame->linesize[1];
    const int rlinesize  = frame->linesize[2];
    const int w = lut3d->clut_width;
    const int level = lut3d->lutsize;
    const int level2 = lut3d->lutsize2;

    int i, j, k, x = 0, y = 0;

    for (k = 0; k < level; k++) {
        for (j = 0; j < level; j++) {
            for (i = 0; i < level; i++) {
                const float *gsrc = (const float *)(datag + y*glinesize);
                const float *bsrc = (const float *)(datab + y*blinesize);
                const float *rsrc = (const float *)(datar + y*rlinesize);
                struct rgbvec *vec = &lut3d->lut[i * level2 + j * level + k];
                vec->r = rsrc[x];
                vec->g = gsrc[x];
                vec->b = bsrc[x];
                if (++x == w) {
                    x = 0;
                    y++;
                }
            }
        }
    }
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    LUT3DContext *lut3d = ctx->priv;
    int ret;

    ret = ff_framesync_init_dualinput(&lut3d->fs, ctx);
    if (ret < 0)
        return ret;
    outlink->w = ctx->inputs[0]->w;
    outlink->h = ctx->inputs[0]->h;
    outlink->time_base = ctx->inputs[0]->time_base;
    if ((ret = ff_framesync_configure(&lut3d->fs)) < 0)
        return ret;
    return 0;
}

static int activate(AVFilterContext *ctx)
{
    LUT3DContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static int config_clut(AVFilterLink *inlink)
{
    int size, level, w, h;
    AVFilterContext *ctx = inlink->dst;
    LUT3DContext *lut3d = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    av_assert0(desc);

    lut3d->clut_bits = desc->comp[0].depth;
    lut3d->clut_planar = av_pix_fmt_count_planes(inlink->format) > 1;
    lut3d->clut_float = desc->flags & AV_PIX_FMT_FLAG_FLOAT;

    lut3d->clut_step = av_get_padded_bits_per_pixel(desc) >> 3;
    ff_fill_rgba_map(lut3d->clut_rgba_map, inlink->format);

    if (inlink->w > inlink->h)
        av_log(ctx, AV_LOG_INFO, "Padding on the right (%dpx) of the "
               "Hald CLUT will be ignored\n", inlink->w - inlink->h);
    else if (inlink->w < inlink->h)
        av_log(ctx, AV_LOG_INFO, "Padding at the bottom (%dpx) of the "
               "Hald CLUT will be ignored\n", inlink->h - inlink->w);
    lut3d->clut_width = w = h = FFMIN(inlink->w, inlink->h);

    for (level = 1; level*level*level < w; level++);
    size = level*level*level;
    if (size != w) {
        av_log(ctx, AV_LOG_WARNING, "The Hald CLUT width does not match the level\n");
        return AVERROR_INVALIDDATA;
    }
    av_assert0(w == h && w == size);
    level *= level;
    if (level > MAX_LEVEL) {
        const int max_clut_level = sqrt(MAX_LEVEL);
        const int max_clut_size  = max_clut_level*max_clut_level*max_clut_level;
        av_log(ctx, AV_LOG_ERROR, "Too large Hald CLUT "
               "(maximum level is %d, or %dx%d CLUT)\n",
               max_clut_level, max_clut_size, max_clut_size);
        return AVERROR(EINVAL);
    }

    return allocate_3dlut(ctx, level, 0);
}

static int update_apply_clut(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    LUT3DContext *lut3d = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFrame *master, *second, *out;
    int ret;

    ret = ff_framesync_dualinput_get(fs, &master, &second);
    if (ret < 0)
        return ret;
    if (!second)
        return ff_filter_frame(ctx->outputs[0], master);
    if (lut3d->clut_float)
        update_clut_float(ctx->priv, second);
    else if (lut3d->clut_planar)
        update_clut_planar(ctx->priv, second);
    else
        update_clut_packed(ctx->priv, second);
    out = apply_lut(inlink, master);
    return ff_filter_frame(ctx->outputs[0], out);
}

static av_cold int haldclut_init(AVFilterContext *ctx)
{
    LUT3DContext *lut3d = ctx->priv;
    lut3d->scale.r = lut3d->scale.g = lut3d->scale.b = 1.f;
    lut3d->fs.on_event = update_apply_clut;
    return 0;
}

static av_cold void haldclut_uninit(AVFilterContext *ctx)
{
    LUT3DContext *lut3d = ctx->priv;
    ff_framesync_uninit(&lut3d->fs);
    av_freep(&lut3d->lut);
}

static const AVOption haldclut_options[] = {
    COMMON_OPTIONS
};

FRAMESYNC_DEFINE_CLASS(haldclut, LUT3DContext, fs);

static const AVFilterPad haldclut_inputs[] = {
    {
        .name         = "main",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
    },{
        .name         = "clut",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_clut,
    },
    { NULL }
};

static const AVFilterPad haldclut_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

AVFilter ff_vf_haldclut = {
    .name          = "haldclut",
    .description   = NULL_IF_CONFIG_SMALL("Adjust colors using a Hald CLUT."),
    .priv_size     = sizeof(LUT3DContext),
    .preinit       = haldclut_framesync_preinit,
    .init          = haldclut_init,
    .uninit        = haldclut_uninit,
    .query_formats = query_formats,
    .activate      = activate,
    .inputs        = haldclut_inputs,
    .outputs       = haldclut_outputs,
    .priv_class    = &haldclut_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_SLICE_THREADS,
};
#endif

#if CONFIG_LUT1D_FILTER

enum interp_1d_mode {
    INTERPOLATE_1D_NEAREST,
    INTERPOLATE_1D_LINEAR,
    INTERPOLATE_1D_CUBIC,
    INTERPOLATE_1D_COSINE,
    INTERPOLATE_1D_SPLINE,
    NB_INTERP_1D_MODE
};

#define MAX_1D_LEVEL 65536

typedef struct LUT1DContext {
    const AVClass *class;
    char *file;
    int interpolation;          ///<interp_1d_mode
    struct rgbvec scale;
    uint8_t rgba_map[4];
    int step;
    float lut[3][MAX_1D_LEVEL];
    int lutsize;
    avfilter_action_func *interp;
} LUT1DContext;

#undef OFFSET
#define OFFSET(x) offsetof(LUT1DContext, x)

static void set_identity_matrix_1d(LUT1DContext *lut1d, int size)
{
    const float c = 1. / (size - 1);
    int i;

    lut1d->lutsize = size;
    for (i = 0; i < size; i++) {
        lut1d->lut[0][i] = i * c;
        lut1d->lut[1][i] = i * c;
        lut1d->lut[2][i] = i * c;
    }
}

static int parse_cinespace_1d(AVFilterContext *ctx, FILE *f)
{
    LUT1DContext *lut1d = ctx->priv;
    char line[MAX_LINE_SIZE];
    float in_min[3]  = {0.0, 0.0, 0.0};
    float in_max[3]  = {1.0, 1.0, 1.0};
    float out_min[3] = {0.0, 0.0, 0.0};
    float out_max[3] = {1.0, 1.0, 1.0};
    int inside_metadata = 0, size;

    NEXT_LINE(skip_line(line));
    if (strncmp(line, "CSPLUTV100", 10)) {
        av_log(ctx, AV_LOG_ERROR, "Not cineSpace LUT format\n");
        return AVERROR(EINVAL);
    }

    NEXT_LINE(skip_line(line));
    if (strncmp(line, "1D", 2)) {
        av_log(ctx, AV_LOG_ERROR, "Not 1D LUT format\n");
        return AVERROR(EINVAL);
    }

    while (1) {
        NEXT_LINE(skip_line(line));

        if (!strncmp(line, "BEGIN METADATA", 14)) {
            inside_metadata = 1;
            continue;
        }
        if (!strncmp(line, "END METADATA", 12)) {
            inside_metadata = 0;
            continue;
        }
        if (inside_metadata == 0) {
            for (int i = 0; i < 3; i++) {
                int npoints = strtol(line, NULL, 0);

                if (npoints != 2) {
                    av_log(ctx, AV_LOG_ERROR, "Unsupported number of pre-lut points.\n");
                    return AVERROR_PATCHWELCOME;
                }

                NEXT_LINE(skip_line(line));
                if (av_sscanf(line, "%f %f", &in_min[i], &in_max[i]) != 2)
                    return AVERROR_INVALIDDATA;
                NEXT_LINE(skip_line(line));
                if (av_sscanf(line, "%f %f", &out_min[i], &out_max[i]) != 2)
                    return AVERROR_INVALIDDATA;
                NEXT_LINE(skip_line(line));
            }

            size = strtol(line, NULL, 0);

            if (size < 2 || size > MAX_1D_LEVEL) {
                av_log(ctx, AV_LOG_ERROR, "Too large or invalid 1D LUT size\n");
                return AVERROR(EINVAL);
            }

            lut1d->lutsize = size;

            for (int i = 0; i < size; i++) {
                NEXT_LINE(skip_line(line));
                if (av_sscanf(line, "%f %f %f", &lut1d->lut[0][i], &lut1d->lut[1][i], &lut1d->lut[2][i]) != 3)
                    return AVERROR_INVALIDDATA;
                lut1d->lut[0][i] *= out_max[0] - out_min[0];
                lut1d->lut[1][i] *= out_max[1] - out_min[1];
                lut1d->lut[2][i] *= out_max[2] - out_min[2];
            }

            break;
        }
    }

    lut1d->scale.r = av_clipf(1. / (in_max[0] - in_min[0]), 0.f, 1.f);
    lut1d->scale.g = av_clipf(1. / (in_max[1] - in_min[1]), 0.f, 1.f);
    lut1d->scale.b = av_clipf(1. / (in_max[2] - in_min[2]), 0.f, 1.f);

    return 0;
}

static int parse_cube_1d(AVFilterContext *ctx, FILE *f)
{
    LUT1DContext *lut1d = ctx->priv;
    char line[MAX_LINE_SIZE];
    float min[3] = {0.0, 0.0, 0.0};
    float max[3] = {1.0, 1.0, 1.0};

    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, "LUT_1D_SIZE", 11)) {
            const int size = strtol(line + 12, NULL, 0);
            int i;

            if (size < 2 || size > MAX_1D_LEVEL) {
                av_log(ctx, AV_LOG_ERROR, "Too large or invalid 1D LUT size\n");
                return AVERROR(EINVAL);
            }
            lut1d->lutsize = size;
            for (i = 0; i < size; i++) {
                do {
try_again:
                    NEXT_LINE(0);
                    if (!strncmp(line, "DOMAIN_", 7)) {
                        float *vals = NULL;
                        if      (!strncmp(line + 7, "MIN ", 4)) vals = min;
                        else if (!strncmp(line + 7, "MAX ", 4)) vals = max;
                        if (!vals)
                            return AVERROR_INVALIDDATA;
                        av_sscanf(line + 11, "%f %f %f", vals, vals + 1, vals + 2);
                        av_log(ctx, AV_LOG_DEBUG, "min: %f %f %f | max: %f %f %f\n",
                               min[0], min[1], min[2], max[0], max[1], max[2]);
                        goto try_again;
                    } else if (!strncmp(line, "LUT_1D_INPUT_RANGE ", 19)) {
                        av_sscanf(line + 19, "%f %f", min, max);
                        min[1] = min[2] = min[0];
                        max[1] = max[2] = max[0];
                        goto try_again;
                    } else if (!strncmp(line, "TITLE", 5)) {
                        goto try_again;
                    }
                } while (skip_line(line));
                if (av_sscanf(line, "%f %f %f", &lut1d->lut[0][i], &lut1d->lut[1][i], &lut1d->lut[2][i]) != 3)
                    return AVERROR_INVALIDDATA;
            }
            break;
        }
    }

    lut1d->scale.r = av_clipf(1. / (max[0] - min[0]), 0.f, 1.f);
    lut1d->scale.g = av_clipf(1. / (max[1] - min[1]), 0.f, 1.f);
    lut1d->scale.b = av_clipf(1. / (max[2] - min[2]), 0.f, 1.f);

    return 0;
}

static const AVOption lut1d_options[] = {
    { "file", "set 1D LUT file name", OFFSET(file), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "interp", "select interpolation mode", OFFSET(interpolation),    AV_OPT_TYPE_INT, {.i64=INTERPOLATE_1D_LINEAR}, 0, NB_INTERP_1D_MODE-1, FLAGS, "interp_mode" },
        { "nearest", "use values from the nearest defined points", 0, AV_OPT_TYPE_CONST, {.i64=INTERPOLATE_1D_NEAREST},   INT_MIN, INT_MAX, FLAGS, "interp_mode" },
        { "linear",  "use values from the linear interpolation",   0, AV_OPT_TYPE_CONST, {.i64=INTERPOLATE_1D_LINEAR},    INT_MIN, INT_MAX, FLAGS, "interp_mode" },
        { "cosine",  "use values from the cosine interpolation",   0, AV_OPT_TYPE_CONST, {.i64=INTERPOLATE_1D_COSINE},    INT_MIN, INT_MAX, FLAGS, "interp_mode" },
        { "cubic",   "use values from the cubic interpolation",    0, AV_OPT_TYPE_CONST, {.i64=INTERPOLATE_1D_CUBIC},     INT_MIN, INT_MAX, FLAGS, "interp_mode" },
        { "spline",  "use values from the spline interpolation",   0, AV_OPT_TYPE_CONST, {.i64=INTERPOLATE_1D_SPLINE},    INT_MIN, INT_MAX, FLAGS, "interp_mode" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(lut1d);

static inline float interp_1d_nearest(const LUT1DContext *lut1d,
                                      int idx, const float s)
{
    return lut1d->lut[idx][NEAR(s)];
}

#define NEXT1D(x) (FFMIN((int)(x) + 1, lut1d->lutsize - 1))

static inline float interp_1d_linear(const LUT1DContext *lut1d,
                                     int idx, const float s)
{
    const int prev = PREV(s);
    const int next = NEXT1D(s);
    const float d = s - prev;
    const float p = lut1d->lut[idx][prev];
    const float n = lut1d->lut[idx][next];

    return lerpf(p, n, d);
}

static inline float interp_1d_cosine(const LUT1DContext *lut1d,
                                     int idx, const float s)
{
    const int prev = PREV(s);
    const int next = NEXT1D(s);
    const float d = s - prev;
    const float p = lut1d->lut[idx][prev];
    const float n = lut1d->lut[idx][next];
    const float m = (1.f - cosf(d * M_PI)) * .5f;

    return lerpf(p, n, m);
}

static inline float interp_1d_cubic(const LUT1DContext *lut1d,
                                    int idx, const float s)
{
    const int prev = PREV(s);
    const int next = NEXT1D(s);
    const float mu = s - prev;
    float a0, a1, a2, a3, mu2;

    float y0 = lut1d->lut[idx][FFMAX(prev - 1, 0)];
    float y1 = lut1d->lut[idx][prev];
    float y2 = lut1d->lut[idx][next];
    float y3 = lut1d->lut[idx][FFMIN(next + 1, lut1d->lutsize - 1)];


    mu2 = mu * mu;
    a0 = y3 - y2 - y0 + y1;
    a1 = y0 - y1 - a0;
    a2 = y2 - y0;
    a3 = y1;

    return a0 * mu * mu2 + a1 * mu2 + a2 * mu + a3;
}

static inline float interp_1d_spline(const LUT1DContext *lut1d,
                                     int idx, const float s)
{
    const int prev = PREV(s);
    const int next = NEXT1D(s);
    const float x = s - prev;
    float c0, c1, c2, c3;

    float y0 = lut1d->lut[idx][FFMAX(prev - 1, 0)];
    float y1 = lut1d->lut[idx][prev];
    float y2 = lut1d->lut[idx][next];
    float y3 = lut1d->lut[idx][FFMIN(next + 1, lut1d->lutsize - 1)];

    c0 = y1;
    c1 = .5f * (y2 - y0);
    c2 = y0 - 2.5f * y1 + 2.f * y2 - .5f * y3;
    c3 = .5f * (y3 - y0) + 1.5f * (y1 - y2);

    return ((c3 * x + c2) * x + c1) * x + c0;
}

#define DEFINE_INTERP_FUNC_PLANAR_1D(name, nbits, depth)                     \
static int interp_1d_##nbits##_##name##_p##depth(AVFilterContext *ctx,       \
                                                 void *arg, int jobnr,       \
                                                 int nb_jobs)                \
{                                                                            \
    int x, y;                                                                \
    const LUT1DContext *lut1d = ctx->priv;                                   \
    const ThreadData *td = arg;                                              \
    const AVFrame *in  = td->in;                                             \
    const AVFrame *out = td->out;                                            \
    const int direct = out == in;                                            \
    const int slice_start = (in->height *  jobnr   ) / nb_jobs;              \
    const int slice_end   = (in->height * (jobnr+1)) / nb_jobs;              \
    uint8_t *grow = out->data[0] + slice_start * out->linesize[0];           \
    uint8_t *brow = out->data[1] + slice_start * out->linesize[1];           \
    uint8_t *rrow = out->data[2] + slice_start * out->linesize[2];           \
    uint8_t *arow = out->data[3] + slice_start * out->linesize[3];           \
    const uint8_t *srcgrow = in->data[0] + slice_start * in->linesize[0];    \
    const uint8_t *srcbrow = in->data[1] + slice_start * in->linesize[1];    \
    const uint8_t *srcrrow = in->data[2] + slice_start * in->linesize[2];    \
    const uint8_t *srcarow = in->data[3] + slice_start * in->linesize[3];    \
    const float factor = (1 << depth) - 1;                                   \
    const float scale_r = (lut1d->scale.r / factor) * (lut1d->lutsize - 1);  \
    const float scale_g = (lut1d->scale.g / factor) * (lut1d->lutsize - 1);  \
    const float scale_b = (lut1d->scale.b / factor) * (lut1d->lutsize - 1);  \
                                                                             \
    for (y = slice_start; y < slice_end; y++) {                              \
        uint##nbits##_t *dstg = (uint##nbits##_t *)grow;                     \
        uint##nbits##_t *dstb = (uint##nbits##_t *)brow;                     \
        uint##nbits##_t *dstr = (uint##nbits##_t *)rrow;                     \
        uint##nbits##_t *dsta = (uint##nbits##_t *)arow;                     \
        const uint##nbits##_t *srcg = (const uint##nbits##_t *)srcgrow;      \
        const uint##nbits##_t *srcb = (const uint##nbits##_t *)srcbrow;      \
        const uint##nbits##_t *srcr = (const uint##nbits##_t *)srcrrow;      \
        const uint##nbits##_t *srca = (const uint##nbits##_t *)srcarow;      \
        for (x = 0; x < in->width; x++) {                                    \
            float r = srcr[x] * scale_r;                                     \
            float g = srcg[x] * scale_g;                                     \
            float b = srcb[x] * scale_b;                                     \
            r = interp_1d_##name(lut1d, 0, r);                               \
            g = interp_1d_##name(lut1d, 1, g);                               \
            b = interp_1d_##name(lut1d, 2, b);                               \
            dstr[x] = av_clip_uintp2(r * factor, depth);                     \
            dstg[x] = av_clip_uintp2(g * factor, depth);                     \
            dstb[x] = av_clip_uintp2(b * factor, depth);                     \
            if (!direct && in->linesize[3])                                  \
                dsta[x] = srca[x];                                           \
        }                                                                    \
        grow += out->linesize[0];                                            \
        brow += out->linesize[1];                                            \
        rrow += out->linesize[2];                                            \
        arow += out->linesize[3];                                            \
        srcgrow += in->linesize[0];                                          \
        srcbrow += in->linesize[1];                                          \
        srcrrow += in->linesize[2];                                          \
        srcarow += in->linesize[3];                                          \
    }                                                                        \
    return 0;                                                                \
}

DEFINE_INTERP_FUNC_PLANAR_1D(nearest,     8, 8)
DEFINE_INTERP_FUNC_PLANAR_1D(linear,      8, 8)
DEFINE_INTERP_FUNC_PLANAR_1D(cosine,      8, 8)
DEFINE_INTERP_FUNC_PLANAR_1D(cubic,       8, 8)
DEFINE_INTERP_FUNC_PLANAR_1D(spline,      8, 8)

DEFINE_INTERP_FUNC_PLANAR_1D(nearest,     16, 9)
DEFINE_INTERP_FUNC_PLANAR_1D(linear,      16, 9)
DEFINE_INTERP_FUNC_PLANAR_1D(cosine,      16, 9)
DEFINE_INTERP_FUNC_PLANAR_1D(cubic,       16, 9)
DEFINE_INTERP_FUNC_PLANAR_1D(spline,      16, 9)

DEFINE_INTERP_FUNC_PLANAR_1D(nearest,     16, 10)
DEFINE_INTERP_FUNC_PLANAR_1D(linear,      16, 10)
DEFINE_INTERP_FUNC_PLANAR_1D(cosine,      16, 10)
DEFINE_INTERP_FUNC_PLANAR_1D(cubic,       16, 10)
DEFINE_INTERP_FUNC_PLANAR_1D(spline,      16, 10)

DEFINE_INTERP_FUNC_PLANAR_1D(nearest,     16, 12)
DEFINE_INTERP_FUNC_PLANAR_1D(linear,      16, 12)
DEFINE_INTERP_FUNC_PLANAR_1D(cosine,      16, 12)
DEFINE_INTERP_FUNC_PLANAR_1D(cubic,       16, 12)
DEFINE_INTERP_FUNC_PLANAR_1D(spline,      16, 12)

DEFINE_INTERP_FUNC_PLANAR_1D(nearest,     16, 14)
DEFINE_INTERP_FUNC_PLANAR_1D(linear,      16, 14)
DEFINE_INTERP_FUNC_PLANAR_1D(cosine,      16, 14)
DEFINE_INTERP_FUNC_PLANAR_1D(cubic,       16, 14)
DEFINE_INTERP_FUNC_PLANAR_1D(spline,      16, 14)

DEFINE_INTERP_FUNC_PLANAR_1D(nearest,     16, 16)
DEFINE_INTERP_FUNC_PLANAR_1D(linear,      16, 16)
DEFINE_INTERP_FUNC_PLANAR_1D(cosine,      16, 16)
DEFINE_INTERP_FUNC_PLANAR_1D(cubic,       16, 16)
DEFINE_INTERP_FUNC_PLANAR_1D(spline,      16, 16)

#define DEFINE_INTERP_FUNC_PLANAR_1D_FLOAT(name, depth)                      \
static int interp_1d_##name##_pf##depth(AVFilterContext *ctx,                \
                                                 void *arg, int jobnr,       \
                                                 int nb_jobs)                \
{                                                                            \
    int x, y;                                                                \
    const LUT1DContext *lut1d = ctx->priv;                                   \
    const ThreadData *td = arg;                                              \
    const AVFrame *in  = td->in;                                             \
    const AVFrame *out = td->out;                                            \
    const int direct = out == in;                                            \
    const int slice_start = (in->height *  jobnr   ) / nb_jobs;              \
    const int slice_end   = (in->height * (jobnr+1)) / nb_jobs;              \
    uint8_t *grow = out->data[0] + slice_start * out->linesize[0];           \
    uint8_t *brow = out->data[1] + slice_start * out->linesize[1];           \
    uint8_t *rrow = out->data[2] + slice_start * out->linesize[2];           \
    uint8_t *arow = out->data[3] + slice_start * out->linesize[3];           \
    const uint8_t *srcgrow = in->data[0] + slice_start * in->linesize[0];    \
    const uint8_t *srcbrow = in->data[1] + slice_start * in->linesize[1];    \
    const uint8_t *srcrrow = in->data[2] + slice_start * in->linesize[2];    \
    const uint8_t *srcarow = in->data[3] + slice_start * in->linesize[3];    \
    const float lutsize = lut1d->lutsize - 1;                                \
    const float scale_r = lut1d->scale.r * lutsize;                          \
    const float scale_g = lut1d->scale.g * lutsize;                          \
    const float scale_b = lut1d->scale.b * lutsize;                          \
                                                                             \
    for (y = slice_start; y < slice_end; y++) {                              \
        float *dstg = (float *)grow;                                         \
        float *dstb = (float *)brow;                                         \
        float *dstr = (float *)rrow;                                         \
        float *dsta = (float *)arow;                                         \
        const float *srcg = (const float *)srcgrow;                          \
        const float *srcb = (const float *)srcbrow;                          \
        const float *srcr = (const float *)srcrrow;                          \
        const float *srca = (const float *)srcarow;                          \
        for (x = 0; x < in->width; x++) {                                    \
            float r = av_clipf(sanitizef(srcr[x]) * scale_r, 0.0f, lutsize); \
            float g = av_clipf(sanitizef(srcg[x]) * scale_g, 0.0f, lutsize); \
            float b = av_clipf(sanitizef(srcb[x]) * scale_b, 0.0f, lutsize); \
            r = interp_1d_##name(lut1d, 0, r);                               \
            g = interp_1d_##name(lut1d, 1, g);                               \
            b = interp_1d_##name(lut1d, 2, b);                               \
            dstr[x] = r;                                                     \
            dstg[x] = g;                                                     \
            dstb[x] = b;                                                     \
            if (!direct && in->linesize[3])                                  \
                dsta[x] = srca[x];                                           \
        }                                                                    \
        grow += out->linesize[0];                                            \
        brow += out->linesize[1];                                            \
        rrow += out->linesize[2];                                            \
        arow += out->linesize[3];                                            \
        srcgrow += in->linesize[0];                                          \
        srcbrow += in->linesize[1];                                          \
        srcrrow += in->linesize[2];                                          \
        srcarow += in->linesize[3];                                          \
    }                                                                        \
    return 0;                                                                \
}

DEFINE_INTERP_FUNC_PLANAR_1D_FLOAT(nearest, 32)
DEFINE_INTERP_FUNC_PLANAR_1D_FLOAT(linear,  32)
DEFINE_INTERP_FUNC_PLANAR_1D_FLOAT(cosine,  32)
DEFINE_INTERP_FUNC_PLANAR_1D_FLOAT(cubic,   32)
DEFINE_INTERP_FUNC_PLANAR_1D_FLOAT(spline,  32)

#define DEFINE_INTERP_FUNC_1D(name, nbits)                                   \
static int interp_1d_##nbits##_##name(AVFilterContext *ctx, void *arg,       \
                                      int jobnr, int nb_jobs)                \
{                                                                            \
    int x, y;                                                                \
    const LUT1DContext *lut1d = ctx->priv;                                   \
    const ThreadData *td = arg;                                              \
    const AVFrame *in  = td->in;                                             \
    const AVFrame *out = td->out;                                            \
    const int direct = out == in;                                            \
    const int step = lut1d->step;                                            \
    const uint8_t r = lut1d->rgba_map[R];                                    \
    const uint8_t g = lut1d->rgba_map[G];                                    \
    const uint8_t b = lut1d->rgba_map[B];                                    \
    const uint8_t a = lut1d->rgba_map[A];                                    \
    const int slice_start = (in->height *  jobnr   ) / nb_jobs;              \
    const int slice_end   = (in->height * (jobnr+1)) / nb_jobs;              \
    uint8_t       *dstrow = out->data[0] + slice_start * out->linesize[0];   \
    const uint8_t *srcrow = in ->data[0] + slice_start * in ->linesize[0];   \
    const float factor = (1 << nbits) - 1;                                   \
    const float scale_r = (lut1d->scale.r / factor) * (lut1d->lutsize - 1);  \
    const float scale_g = (lut1d->scale.g / factor) * (lut1d->lutsize - 1);  \
    const float scale_b = (lut1d->scale.b / factor) * (lut1d->lutsize - 1);  \
                                                                             \
    for (y = slice_start; y < slice_end; y++) {                              \
        uint##nbits##_t *dst = (uint##nbits##_t *)dstrow;                    \
        const uint##nbits##_t *src = (const uint##nbits##_t *)srcrow;        \
        for (x = 0; x < in->width * step; x += step) {                       \
            float rr = src[x + r] * scale_r;                                 \
            float gg = src[x + g] * scale_g;                                 \
            float bb = src[x + b] * scale_b;                                 \
            rr = interp_1d_##name(lut1d, 0, rr);                             \
            gg = interp_1d_##name(lut1d, 1, gg);                             \
            bb = interp_1d_##name(lut1d, 2, bb);                             \
            dst[x + r] = av_clip_uint##nbits(rr * factor);                   \
            dst[x + g] = av_clip_uint##nbits(gg * factor);                   \
            dst[x + b] = av_clip_uint##nbits(bb * factor);                   \
            if (!direct && step == 4)                                        \
                dst[x + a] = src[x + a];                                     \
        }                                                                    \
        dstrow += out->linesize[0];                                          \
        srcrow += in ->linesize[0];                                          \
    }                                                                        \
    return 0;                                                                \
}

DEFINE_INTERP_FUNC_1D(nearest,     8)
DEFINE_INTERP_FUNC_1D(linear,      8)
DEFINE_INTERP_FUNC_1D(cosine,      8)
DEFINE_INTERP_FUNC_1D(cubic,       8)
DEFINE_INTERP_FUNC_1D(spline,      8)

DEFINE_INTERP_FUNC_1D(nearest,     16)
DEFINE_INTERP_FUNC_1D(linear,      16)
DEFINE_INTERP_FUNC_1D(cosine,      16)
DEFINE_INTERP_FUNC_1D(cubic,       16)
DEFINE_INTERP_FUNC_1D(spline,      16)

static int config_input_1d(AVFilterLink *inlink)
{
    int depth, is16bit, isfloat, planar;
    LUT1DContext *lut1d = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    depth = desc->comp[0].depth;
    is16bit = desc->comp[0].depth > 8;
    planar = desc->flags & AV_PIX_FMT_FLAG_PLANAR;
    isfloat = desc->flags & AV_PIX_FMT_FLAG_FLOAT;
    ff_fill_rgba_map(lut1d->rgba_map, inlink->format);
    lut1d->step = av_get_padded_bits_per_pixel(desc) >> (3 + is16bit);

#define SET_FUNC_1D(name) do {                                     \
    if (planar && !isfloat) {                                      \
        switch (depth) {                                           \
        case  8: lut1d->interp = interp_1d_8_##name##_p8;   break; \
        case  9: lut1d->interp = interp_1d_16_##name##_p9;  break; \
        case 10: lut1d->interp = interp_1d_16_##name##_p10; break; \
        case 12: lut1d->interp = interp_1d_16_##name##_p12; break; \
        case 14: lut1d->interp = interp_1d_16_##name##_p14; break; \
        case 16: lut1d->interp = interp_1d_16_##name##_p16; break; \
        }                                                          \
    } else if (isfloat) { lut1d->interp = interp_1d_##name##_pf32; \
    } else if (is16bit) { lut1d->interp = interp_1d_16_##name;     \
    } else {              lut1d->interp = interp_1d_8_##name; }    \
} while (0)

    switch (lut1d->interpolation) {
    case INTERPOLATE_1D_NEAREST:     SET_FUNC_1D(nearest);  break;
    case INTERPOLATE_1D_LINEAR:      SET_FUNC_1D(linear);   break;
    case INTERPOLATE_1D_COSINE:      SET_FUNC_1D(cosine);   break;
    case INTERPOLATE_1D_CUBIC:       SET_FUNC_1D(cubic);    break;
    case INTERPOLATE_1D_SPLINE:      SET_FUNC_1D(spline);   break;
    default:
        av_assert0(0);
    }

    return 0;
}

static av_cold int lut1d_init(AVFilterContext *ctx)
{
    int ret;
    FILE *f;
    const char *ext;
    LUT1DContext *lut1d = ctx->priv;

    lut1d->scale.r = lut1d->scale.g = lut1d->scale.b = 1.f;

    if (!lut1d->file) {
        set_identity_matrix_1d(lut1d, 32);
        return 0;
    }

    f = av_fopen_utf8(lut1d->file, "r");
    if (!f) {
        ret = AVERROR(errno);
        av_log(ctx, AV_LOG_ERROR, "%s: %s\n", lut1d->file, av_err2str(ret));
        return ret;
    }

    ext = strrchr(lut1d->file, '.');
    if (!ext) {
        av_log(ctx, AV_LOG_ERROR, "Unable to guess the format from the extension\n");
        ret = AVERROR_INVALIDDATA;
        goto end;
    }
    ext++;

    if (!av_strcasecmp(ext, "cube") || !av_strcasecmp(ext, "1dlut")) {
        ret = parse_cube_1d(ctx, f);
    } else if (!av_strcasecmp(ext, "csp")) {
        ret = parse_cinespace_1d(ctx, f);
    } else {
        av_log(ctx, AV_LOG_ERROR, "Unrecognized '.%s' file type\n", ext);
        ret = AVERROR(EINVAL);
    }

    if (!ret && !lut1d->lutsize) {
        av_log(ctx, AV_LOG_ERROR, "1D LUT is empty\n");
        ret = AVERROR_INVALIDDATA;
    }

end:
    fclose(f);
    return ret;
}

static AVFrame *apply_1d_lut(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    LUT1DContext *lut1d = ctx->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFrame *out;
    ThreadData td;

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return NULL;
        }
        av_frame_copy_props(out, in);
    }

    td.in  = in;
    td.out = out;
    ctx->internal->execute(ctx, lut1d->interp, &td, NULL, FFMIN(outlink->h, ff_filter_get_nb_threads(ctx)));

    if (out != in)
        av_frame_free(&in);

    return out;
}

static int filter_frame_1d(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFrame *out = apply_1d_lut(inlink, in);
    if (!out)
        return AVERROR(ENOMEM);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad lut1d_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame_1d,
        .config_props = config_input_1d,
    },
    { NULL }
};

static const AVFilterPad lut1d_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_lut1d = {
    .name          = "lut1d",
    .description   = NULL_IF_CONFIG_SMALL("Adjust colors using a 1D LUT."),
    .priv_size     = sizeof(LUT1DContext),
    .init          = lut1d_init,
    .query_formats = query_formats,
    .inputs        = lut1d_inputs,
    .outputs       = lut1d_outputs,
    .priv_class    = &lut1d_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};
#endif
