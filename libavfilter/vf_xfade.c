/*
 * Copyright (c) 2020 Paul B Mahol
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

#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "avfilter.h"
#include "filters.h"
#include "video.h"

enum XFadeTransitions {
    CUSTOM = -1,
    FADE,
    WIPELEFT,
    WIPERIGHT,
    WIPEUP,
    WIPEDOWN,
    SLIDELEFT,
    SLIDERIGHT,
    SLIDEUP,
    SLIDEDOWN,
    CIRCLECROP,
    RECTCROP,
    DISTANCE,
    FADEBLACK,
    FADEWHITE,
    RADIAL,
    SMOOTHLEFT,
    SMOOTHRIGHT,
    SMOOTHUP,
    SMOOTHDOWN,
    CIRCLEOPEN,
    CIRCLECLOSE,
    VERTOPEN,
    VERTCLOSE,
    HORZOPEN,
    HORZCLOSE,
    DISSOLVE,
    PIXELIZE,
    DIAGTL,
    DIAGTR,
    DIAGBL,
    DIAGBR,
    HLSLICE,
    HRSLICE,
    VUSLICE,
    VDSLICE,
    HBLUR,
    FADEGRAYS,
    WIPETL,
    WIPETR,
    WIPEBL,
    WIPEBR,
    SQUEEZEH,
    SQUEEZEV,
    ZOOMIN,
    FADEFAST,
    FADESLOW,
    HLWIND,
    HRWIND,
    VUWIND,
    VDWIND,
    COVERLEFT,
    COVERRIGHT,
    COVERUP,
    COVERDOWN,
    REVEALLEFT,
    REVEALRIGHT,
    REVEALUP,
    REVEALDOWN,
    NB_TRANSITIONS,
};

typedef struct XFadeContext {
    const AVClass *class;

    int     transition;
    int64_t duration;
    int64_t offset;
    char   *custom_str;

    int nb_planes;
    int depth;
    int is_rgb;

    // PTS when the fade should start (in first inputs timebase)
    int64_t start_pts;

    // PTS offset between first and second input
    int64_t inputs_offset_pts;

    // Duration of the transition
    int64_t duration_pts;

    // Current PTS of the first input
    int64_t pts;

    // If frames are currently just passed through unmodified,
    // like before and after the actual transition.
    int passthrough;

    int status[2];
    AVFrame *xf[2];
    int max_value;
    uint16_t black[4];
    uint16_t white[4];

    void (*transitionf)(AVFilterContext *ctx, const AVFrame *a, const AVFrame *b, AVFrame *out, float progress,
                        int slice_start, int slice_end, int jobnr);

    AVExpr *e;
} XFadeContext;

static const char *const var_names[] = {   "X",   "Y",   "W",   "H",   "A",   "B",   "PLANE",          "P",        NULL };
enum                                   { VAR_X, VAR_Y, VAR_W, VAR_H, VAR_A, VAR_B, VAR_PLANE, VAR_PROGRESS, VAR_VARS_NB };

typedef struct ThreadData {
    const AVFrame *xf[2];
    AVFrame *out;
    float progress;
} ThreadData;

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP, AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_GBRP9,
    AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUVA444P10,
    AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GRAY10,
    AV_PIX_FMT_YUV444P12,
    AV_PIX_FMT_YUVA444P12,
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GRAY12,
    AV_PIX_FMT_YUV444P14, AV_PIX_FMT_GBRP14,
    AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_GBRP16, AV_PIX_FMT_GBRAP16, AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_NONE
};

static av_cold void uninit(AVFilterContext *ctx)
{
    XFadeContext *s = ctx->priv;

    av_expr_free(s->e);
}

#define OFFSET(x) offsetof(XFadeContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)

static const AVOption xfade_options[] = {
    { "transition", "set cross fade transition", OFFSET(transition), AV_OPT_TYPE_INT, {.i64=FADE}, -1, NB_TRANSITIONS-1, FLAGS, .unit = "transition" },
    {   "custom",    "custom transition",     0, AV_OPT_TYPE_CONST, {.i64=CUSTOM},    0, 0, FLAGS, .unit = "transition" },
    {   "fade",      "fade transition",       0, AV_OPT_TYPE_CONST, {.i64=FADE},      0, 0, FLAGS, .unit = "transition" },
    {   "wipeleft",  "wipe left transition",  0, AV_OPT_TYPE_CONST, {.i64=WIPELEFT},  0, 0, FLAGS, .unit = "transition" },
    {   "wiperight", "wipe right transition", 0, AV_OPT_TYPE_CONST, {.i64=WIPERIGHT}, 0, 0, FLAGS, .unit = "transition" },
    {   "wipeup",    "wipe up transition",    0, AV_OPT_TYPE_CONST, {.i64=WIPEUP},    0, 0, FLAGS, .unit = "transition" },
    {   "wipedown",  "wipe down transition",  0, AV_OPT_TYPE_CONST, {.i64=WIPEDOWN},  0, 0, FLAGS, .unit = "transition" },
    {   "slideleft",  "slide left transition",  0, AV_OPT_TYPE_CONST, {.i64=SLIDELEFT},  0, 0, FLAGS, .unit = "transition" },
    {   "slideright", "slide right transition", 0, AV_OPT_TYPE_CONST, {.i64=SLIDERIGHT}, 0, 0, FLAGS, .unit = "transition" },
    {   "slideup",    "slide up transition",    0, AV_OPT_TYPE_CONST, {.i64=SLIDEUP},    0, 0, FLAGS, .unit = "transition" },
    {   "slidedown",  "slide down transition",  0, AV_OPT_TYPE_CONST, {.i64=SLIDEDOWN},  0, 0, FLAGS, .unit = "transition" },
    {   "circlecrop", "circle crop transition", 0, AV_OPT_TYPE_CONST, {.i64=CIRCLECROP}, 0, 0, FLAGS, .unit = "transition" },
    {   "rectcrop",   "rect crop transition",   0, AV_OPT_TYPE_CONST, {.i64=RECTCROP},   0, 0, FLAGS, .unit = "transition" },
    {   "distance",   "distance transition",    0, AV_OPT_TYPE_CONST, {.i64=DISTANCE},   0, 0, FLAGS, .unit = "transition" },
    {   "fadeblack",  "fadeblack transition",   0, AV_OPT_TYPE_CONST, {.i64=FADEBLACK},  0, 0, FLAGS, .unit = "transition" },
    {   "fadewhite",  "fadewhite transition",   0, AV_OPT_TYPE_CONST, {.i64=FADEWHITE},  0, 0, FLAGS, .unit = "transition" },
    {   "radial",     "radial transition",      0, AV_OPT_TYPE_CONST, {.i64=RADIAL},     0, 0, FLAGS, .unit = "transition" },
    {   "smoothleft", "smoothleft transition",  0, AV_OPT_TYPE_CONST, {.i64=SMOOTHLEFT}, 0, 0, FLAGS, .unit = "transition" },
    {   "smoothright","smoothright transition", 0, AV_OPT_TYPE_CONST, {.i64=SMOOTHRIGHT},0, 0, FLAGS, .unit = "transition" },
    {   "smoothup",   "smoothup transition",    0, AV_OPT_TYPE_CONST, {.i64=SMOOTHUP},   0, 0, FLAGS, .unit = "transition" },
    {   "smoothdown", "smoothdown transition",  0, AV_OPT_TYPE_CONST, {.i64=SMOOTHDOWN}, 0, 0, FLAGS, .unit = "transition" },
    {   "circleopen", "circleopen transition",  0, AV_OPT_TYPE_CONST, {.i64=CIRCLEOPEN}, 0, 0, FLAGS, .unit = "transition" },
    {   "circleclose","circleclose transition", 0, AV_OPT_TYPE_CONST, {.i64=CIRCLECLOSE},0, 0, FLAGS, .unit = "transition" },
    {   "vertopen",   "vert open transition",   0, AV_OPT_TYPE_CONST, {.i64=VERTOPEN},   0, 0, FLAGS, .unit = "transition" },
    {   "vertclose",  "vert close transition",  0, AV_OPT_TYPE_CONST, {.i64=VERTCLOSE},  0, 0, FLAGS, .unit = "transition" },
    {   "horzopen",   "horz open transition",   0, AV_OPT_TYPE_CONST, {.i64=HORZOPEN},   0, 0, FLAGS, .unit = "transition" },
    {   "horzclose",  "horz close transition",  0, AV_OPT_TYPE_CONST, {.i64=HORZCLOSE},  0, 0, FLAGS, .unit = "transition" },
    {   "dissolve",   "dissolve transition",    0, AV_OPT_TYPE_CONST, {.i64=DISSOLVE},   0, 0, FLAGS, .unit = "transition" },
    {   "pixelize",   "pixelize transition",    0, AV_OPT_TYPE_CONST, {.i64=PIXELIZE},   0, 0, FLAGS, .unit = "transition" },
    {   "diagtl",     "diag tl transition",     0, AV_OPT_TYPE_CONST, {.i64=DIAGTL},     0, 0, FLAGS, .unit = "transition" },
    {   "diagtr",     "diag tr transition",     0, AV_OPT_TYPE_CONST, {.i64=DIAGTR},     0, 0, FLAGS, .unit = "transition" },
    {   "diagbl",     "diag bl transition",     0, AV_OPT_TYPE_CONST, {.i64=DIAGBL},     0, 0, FLAGS, .unit = "transition" },
    {   "diagbr",     "diag br transition",     0, AV_OPT_TYPE_CONST, {.i64=DIAGBR},     0, 0, FLAGS, .unit = "transition" },
    {   "hlslice",    "hl slice transition",    0, AV_OPT_TYPE_CONST, {.i64=HLSLICE},    0, 0, FLAGS, .unit = "transition" },
    {   "hrslice",    "hr slice transition",    0, AV_OPT_TYPE_CONST, {.i64=HRSLICE},    0, 0, FLAGS, .unit = "transition" },
    {   "vuslice",    "vu slice transition",    0, AV_OPT_TYPE_CONST, {.i64=VUSLICE},    0, 0, FLAGS, .unit = "transition" },
    {   "vdslice",    "vd slice transition",    0, AV_OPT_TYPE_CONST, {.i64=VDSLICE},    0, 0, FLAGS, .unit = "transition" },
    {   "hblur",      "hblur transition",       0, AV_OPT_TYPE_CONST, {.i64=HBLUR},      0, 0, FLAGS, .unit = "transition" },
    {   "fadegrays",  "fadegrays transition",   0, AV_OPT_TYPE_CONST, {.i64=FADEGRAYS},  0, 0, FLAGS, .unit = "transition" },
    {   "wipetl",     "wipe tl transition",     0, AV_OPT_TYPE_CONST, {.i64=WIPETL},     0, 0, FLAGS, .unit = "transition" },
    {   "wipetr",     "wipe tr transition",     0, AV_OPT_TYPE_CONST, {.i64=WIPETR},     0, 0, FLAGS, .unit = "transition" },
    {   "wipebl",     "wipe bl transition",     0, AV_OPT_TYPE_CONST, {.i64=WIPEBL},     0, 0, FLAGS, .unit = "transition" },
    {   "wipebr",     "wipe br transition",     0, AV_OPT_TYPE_CONST, {.i64=WIPEBR},     0, 0, FLAGS, .unit = "transition" },
    {   "squeezeh",   "squeeze h transition",   0, AV_OPT_TYPE_CONST, {.i64=SQUEEZEH},   0, 0, FLAGS, .unit = "transition" },
    {   "squeezev",   "squeeze v transition",   0, AV_OPT_TYPE_CONST, {.i64=SQUEEZEV},   0, 0, FLAGS, .unit = "transition" },
    {   "zoomin",     "zoom in transition",     0, AV_OPT_TYPE_CONST, {.i64=ZOOMIN},     0, 0, FLAGS, .unit = "transition" },
    {   "fadefast",   "fast fade transition",   0, AV_OPT_TYPE_CONST, {.i64=FADEFAST},   0, 0, FLAGS, .unit = "transition" },
    {   "fadeslow",   "slow fade transition",   0, AV_OPT_TYPE_CONST, {.i64=FADESLOW},   0, 0, FLAGS, .unit = "transition" },
    {   "hlwind",     "hl wind transition",     0, AV_OPT_TYPE_CONST, {.i64=HLWIND},     0, 0, FLAGS, .unit = "transition" },
    {   "hrwind",     "hr wind transition",     0, AV_OPT_TYPE_CONST, {.i64=HRWIND},     0, 0, FLAGS, .unit = "transition" },
    {   "vuwind",     "vu wind transition",     0, AV_OPT_TYPE_CONST, {.i64=VUWIND},     0, 0, FLAGS, .unit = "transition" },
    {   "vdwind",     "vd wind transition",     0, AV_OPT_TYPE_CONST, {.i64=VDWIND},     0, 0, FLAGS, .unit = "transition" },
    {   "coverleft",  "cover left transition",  0, AV_OPT_TYPE_CONST, {.i64=COVERLEFT},  0, 0, FLAGS, .unit = "transition" },
    {   "coverright", "cover right transition", 0, AV_OPT_TYPE_CONST, {.i64=COVERRIGHT}, 0, 0, FLAGS, .unit = "transition" },
    {   "coverup",    "cover up transition",    0, AV_OPT_TYPE_CONST, {.i64=COVERUP},    0, 0, FLAGS, .unit = "transition" },
    {   "coverdown",  "cover down transition",  0, AV_OPT_TYPE_CONST, {.i64=COVERDOWN},  0, 0, FLAGS, .unit = "transition" },
    {   "revealleft", "reveal left transition", 0, AV_OPT_TYPE_CONST, {.i64=REVEALLEFT}, 0, 0, FLAGS, .unit = "transition" },
    {   "revealright","reveal right transition",0, AV_OPT_TYPE_CONST, {.i64=REVEALRIGHT},0, 0, FLAGS, .unit = "transition" },
    {   "revealup",   "reveal up transition",   0, AV_OPT_TYPE_CONST, {.i64=REVEALUP},   0, 0, FLAGS, .unit = "transition" },
    {   "revealdown", "reveal down transition", 0, AV_OPT_TYPE_CONST, {.i64=REVEALDOWN}, 0, 0, FLAGS, .unit = "transition" },
    { "duration", "set cross fade duration", OFFSET(duration), AV_OPT_TYPE_DURATION, {.i64=1000000}, 0, 60000000, FLAGS },
    { "offset",   "set cross fade start relative to first input stream", OFFSET(offset), AV_OPT_TYPE_DURATION, {.i64=0}, INT64_MIN, INT64_MAX, FLAGS },
    { "expr",   "set expression for custom transition", OFFSET(custom_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(xfade);

#define CUSTOM_TRANSITION(name, type, div)                                           \
static void custom##name##_transition(AVFilterContext *ctx,                          \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int height = slice_end - slice_start;                                      \
    const int width = out->width;                                                    \
                                                                                     \
    double values[VAR_VARS_NB];                                                      \
    values[VAR_W] = width;                                                           \
    values[VAR_H] = out->height;                                                     \
    values[VAR_PROGRESS] = progress;                                                 \
                                                                                     \
    for (int p = 0; p < s->nb_planes; p++) {                                         \
        const type *xf0 = (const type *)(a->data[p] + slice_start * a->linesize[p]); \
        const type *xf1 = (const type *)(b->data[p] + slice_start * b->linesize[p]); \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);         \
                                                                                     \
        values[VAR_PLANE] = p;                                                       \
                                                                                     \
        for (int y = 0; y < height; y++) {                                           \
            values[VAR_Y] = slice_start + y;                                         \
            for (int x = 0; x < width; x++) {                                        \
                values[VAR_X] = x;                                                   \
                values[VAR_A] = xf0[x];                                              \
                values[VAR_B] = xf1[x];                                              \
                dst[x] = av_expr_eval(s->e, values, s);                              \
            }                                                                        \
                                                                                     \
            dst += out->linesize[p] / div;                                           \
            xf0 += a->linesize[p] / div;                                             \
            xf1 += b->linesize[p] / div;                                             \
        }                                                                            \
    }                                                                                \
}

CUSTOM_TRANSITION(8, uint8_t, 1)
CUSTOM_TRANSITION(16, uint16_t, 2)

static inline float mix(float a, float b, float mix)
{
    return a * mix + b * (1.f - mix);
}

static inline float fract(float a)
{
    return a - floorf(a);
}

static inline float smoothstep(float edge0, float edge1, float x)
{
    float t;

    t = av_clipf((x - edge0) / (edge1 - edge0), 0.f, 1.f);

    return t * t * (3.f - 2.f * t);
}

#define FADE_TRANSITION(name, type, div)                                             \
static void fade##name##_transition(AVFilterContext *ctx,                            \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int height = slice_end - slice_start;                                      \
    const int width = out->width;                                                    \
                                                                                     \
    for (int p = 0; p < s->nb_planes; p++) {                                         \
        const type *xf0 = (const type *)(a->data[p] + slice_start * a->linesize[p]); \
        const type *xf1 = (const type *)(b->data[p] + slice_start * b->linesize[p]); \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);         \
                                                                                     \
        for (int y = 0; y < height; y++) {                                           \
            for (int x = 0; x < width; x++) {                                        \
                dst[x] = mix(xf0[x], xf1[x], progress);                              \
            }                                                                        \
                                                                                     \
            dst += out->linesize[p] / div;                                           \
            xf0 += a->linesize[p] / div;                                             \
            xf1 += b->linesize[p] / div;                                             \
        }                                                                            \
    }                                                                                \
}

FADE_TRANSITION(8, uint8_t, 1)
FADE_TRANSITION(16, uint16_t, 2)

#define WIPELEFT_TRANSITION(name, type, div)                                         \
static void wipeleft##name##_transition(AVFilterContext *ctx,                        \
                                const AVFrame *a, const AVFrame *b, AVFrame *out,    \
                                float progress,                                      \
                                int slice_start, int slice_end, int jobnr)           \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int height = slice_end - slice_start;                                      \
    const int width = out->width;                                                    \
    const int z = width * progress;                                                  \
                                                                                     \
    for (int p = 0; p < s->nb_planes; p++) {                                         \
        const type *xf0 = (const type *)(a->data[p] + slice_start * a->linesize[p]); \
        const type *xf1 = (const type *)(b->data[p] + slice_start * b->linesize[p]); \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);         \
                                                                                     \
        for (int y = 0; y < height; y++) {                                           \
            for (int x = 0; x < width; x++) {                                        \
                dst[x] = x > z ? xf1[x] : xf0[x];                                    \
            }                                                                        \
                                                                                     \
            dst += out->linesize[p] / div;                                           \
            xf0 += a->linesize[p] / div;                                             \
            xf1 += b->linesize[p] / div;                                             \
        }                                                                            \
    }                                                                                \
}

WIPELEFT_TRANSITION(8, uint8_t, 1)
WIPELEFT_TRANSITION(16, uint16_t, 2)

#define WIPERIGHT_TRANSITION(name, type, div)                                        \
static void wiperight##name##_transition(AVFilterContext *ctx,                       \
                                 const AVFrame *a, const AVFrame *b, AVFrame *out,   \
                                 float progress,                                     \
                                 int slice_start, int slice_end, int jobnr)          \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int height = slice_end - slice_start;                                      \
    const int width = out->width;                                                    \
    const int z = width * (1.f - progress);                                          \
                                                                                     \
    for (int p = 0; p < s->nb_planes; p++) {                                         \
        const type *xf0 = (const type *)(a->data[p] + slice_start * a->linesize[p]); \
        const type *xf1 = (const type *)(b->data[p] + slice_start * b->linesize[p]); \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);         \
                                                                                     \
        for (int y = 0; y < height; y++) {                                           \
            for (int x = 0; x < width; x++) {                                        \
                dst[x] = x > z ? xf0[x] : xf1[x];                                    \
            }                                                                        \
                                                                                     \
            dst += out->linesize[p] / div;                                           \
            xf0 += a->linesize[p] / div;                                             \
            xf1 += b->linesize[p] / div;                                             \
        }                                                                            \
    }                                                                                \
}

WIPERIGHT_TRANSITION(8, uint8_t, 1)
WIPERIGHT_TRANSITION(16, uint16_t, 2)

#define WIPEUP_TRANSITION(name, type, div)                                           \
static void wipeup##name##_transition(AVFilterContext *ctx,                          \
                              const AVFrame *a, const AVFrame *b, AVFrame *out,      \
                              float progress,                                        \
                              int slice_start, int slice_end, int jobnr)             \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int height = slice_end - slice_start;                                      \
    const int width = out->width;                                                    \
    const int z = out->height * progress;                                            \
                                                                                     \
    for (int p = 0; p < s->nb_planes; p++) {                                         \
        const type *xf0 = (const type *)(a->data[p] + slice_start * a->linesize[p]); \
        const type *xf1 = (const type *)(b->data[p] + slice_start * b->linesize[p]); \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);         \
                                                                                     \
        for (int y = 0; y < height; y++) {                                           \
            for (int x = 0; x < width; x++) {                                        \
                dst[x] = slice_start + y > z ? xf1[x] : xf0[x];                      \
            }                                                                        \
                                                                                     \
            dst += out->linesize[p] / div;                                           \
            xf0 += a->linesize[p] / div;                                             \
            xf1 += b->linesize[p] / div;                                             \
        }                                                                            \
    }                                                                                \
}

WIPEUP_TRANSITION(8, uint8_t, 1)
WIPEUP_TRANSITION(16, uint16_t, 2)

#define WIPEDOWN_TRANSITION(name, type, div)                                         \
static void wipedown##name##_transition(AVFilterContext *ctx,                        \
                                const AVFrame *a, const AVFrame *b, AVFrame *out,    \
                                float progress,                                      \
                                int slice_start, int slice_end, int jobnr)           \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int height = slice_end - slice_start;                                      \
    const int width = out->width;                                                    \
    const int z = out->height * (1.f - progress);                                    \
                                                                                     \
    for (int p = 0; p < s->nb_planes; p++) {                                         \
        const type *xf0 = (const type *)(a->data[p] + slice_start * a->linesize[p]); \
        const type *xf1 = (const type *)(b->data[p] + slice_start * b->linesize[p]); \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);         \
                                                                                     \
        for (int y = 0; y < height; y++) {                                           \
            for (int x = 0; x < width; x++) {                                        \
                dst[x] = slice_start + y > z ? xf0[x] : xf1[x];                      \
            }                                                                        \
                                                                                     \
            dst += out->linesize[p] / div;                                           \
            xf0 += a->linesize[p] / div;                                             \
            xf1 += b->linesize[p] / div;                                             \
        }                                                                            \
    }                                                                                \
}

WIPEDOWN_TRANSITION(8, uint8_t, 1)
WIPEDOWN_TRANSITION(16, uint16_t, 2)

#define SLIDELEFT_TRANSITION(name, type, div)                                        \
static void slideleft##name##_transition(AVFilterContext *ctx,                       \
                                 const AVFrame *a, const AVFrame *b, AVFrame *out,   \
                                 float progress,                                     \
                                 int slice_start, int slice_end, int jobnr)          \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int height = slice_end - slice_start;                                      \
    const int width = out->width;                                                    \
    const int z = -progress * width;                                                 \
                                                                                     \
    for (int p = 0; p < s->nb_planes; p++) {                                         \
        const type *xf0 = (const type *)(a->data[p] + slice_start * a->linesize[p]); \
        const type *xf1 = (const type *)(b->data[p] + slice_start * b->linesize[p]); \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);         \
                                                                                     \
        for (int y = 0; y < height; y++) {                                           \
            for (int x = 0; x < width; x++) {                                        \
                const int zx = z + x;                                                \
                const int zz = zx % width + width * (zx < 0);                        \
                dst[x] = (zx >= 0) && (zx < width) ? xf1[zz] : xf0[zz];              \
            }                                                                        \
                                                                                     \
            dst += out->linesize[p] / div;                                           \
            xf0 += a->linesize[p] / div;                                             \
            xf1 += b->linesize[p] / div;                                             \
        }                                                                            \
    }                                                                                \
}

SLIDELEFT_TRANSITION(8, uint8_t, 1)
SLIDELEFT_TRANSITION(16, uint16_t, 2)

#define SLIDERIGHT_TRANSITION(name, type, div)                                       \
static void slideright##name##_transition(AVFilterContext *ctx,                      \
                                  const AVFrame *a, const AVFrame *b, AVFrame *out,  \
                                  float progress,                                    \
                                  int slice_start, int slice_end, int jobnr)         \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int height = slice_end - slice_start;                                      \
    const int width = out->width;                                                    \
    const int z = progress * width;                                                  \
                                                                                     \
    for (int p = 0; p < s->nb_planes; p++) {                                         \
        const type *xf0 = (const type *)(a->data[p] + slice_start * a->linesize[p]); \
        const type *xf1 = (const type *)(b->data[p] + slice_start * b->linesize[p]); \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);         \
                                                                                     \
        for (int y = 0; y < height; y++) {                                           \
            for (int x = 0; x < width; x++) {                                        \
                const int zx = z + x;                                                \
                const int zz = zx % width + width * (zx < 0);                        \
                dst[x] = (zx >= 0) && (zx < width) ? xf1[zz] : xf0[zz];              \
            }                                                                        \
                                                                                     \
            dst += out->linesize[p] / div;                                           \
            xf0 += a->linesize[p] / div;                                             \
            xf1 += b->linesize[p] / div;                                             \
        }                                                                            \
    }                                                                                \
}

SLIDERIGHT_TRANSITION(8, uint8_t, 1)
SLIDERIGHT_TRANSITION(16, uint16_t, 2)

#define SLIDEUP_TRANSITION(name, type, div)                                         \
static void slideup##name##_transition(AVFilterContext *ctx,                        \
                               const AVFrame *a, const AVFrame *b, AVFrame *out,    \
                               float progress,                                      \
                               int slice_start, int slice_end, int jobnr)           \
{                                                                                   \
    XFadeContext *s = ctx->priv;                                                    \
    const int height = out->height;                                                 \
    const int width = out->width;                                                   \
    const int z = -progress * height;                                               \
                                                                                    \
    for (int p = 0; p < s->nb_planes; p++) {                                        \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);        \
                                                                                    \
        for (int y = slice_start; y < slice_end; y++) {                             \
            const int zy = z + y;                                                   \
            const int zz = zy % height + height * (zy < 0);                         \
            const type *xf0 = (const type *)(a->data[p] + zz * a->linesize[p]);     \
            const type *xf1 = (const type *)(b->data[p] + zz * b->linesize[p]);     \
                                                                                    \
            for (int x = 0; x < width; x++) {                                       \
                dst[x] = (zy >= 0) && (zy < height) ? xf1[x] : xf0[x];              \
            }                                                                       \
                                                                                    \
            dst += out->linesize[p] / div;                                          \
        }                                                                           \
    }                                                                               \
}

SLIDEUP_TRANSITION(8, uint8_t, 1)
SLIDEUP_TRANSITION(16, uint16_t, 2)

#define SLIDEDOWN_TRANSITION(name, type, div)                                       \
static void slidedown##name##_transition(AVFilterContext *ctx,                      \
                                 const AVFrame *a, const AVFrame *b, AVFrame *out,  \
                                 float progress,                                    \
                                 int slice_start, int slice_end, int jobnr)         \
{                                                                                   \
    XFadeContext *s = ctx->priv;                                                    \
    const int height = out->height;                                                 \
    const int width = out->width;                                                   \
    const int z = progress * height;                                                \
                                                                                    \
    for (int p = 0; p < s->nb_planes; p++) {                                        \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);        \
                                                                                    \
        for (int y = slice_start; y < slice_end; y++) {                             \
            const int zy = z + y;                                                   \
            const int zz = zy % height + height * (zy < 0);                         \
            const type *xf0 = (const type *)(a->data[p] + zz * a->linesize[p]);     \
            const type *xf1 = (const type *)(b->data[p] + zz * b->linesize[p]);     \
                                                                                    \
            for (int x = 0; x < width; x++) {                                       \
                dst[x] = (zy >= 0) && (zy < height) ? xf1[x] : xf0[x];              \
            }                                                                       \
                                                                                    \
            dst += out->linesize[p] / div;                                          \
        }                                                                           \
    }                                                                               \
}

SLIDEDOWN_TRANSITION(8, uint8_t, 1)
SLIDEDOWN_TRANSITION(16, uint16_t, 2)

#define CIRCLECROP_TRANSITION(name, type, div)                                      \
static void circlecrop##name##_transition(AVFilterContext *ctx,                     \
                                 const AVFrame *a, const AVFrame *b, AVFrame *out,  \
                                 float progress,                                    \
                                 int slice_start, int slice_end, int jobnr)         \
{                                                                                   \
    XFadeContext *s = ctx->priv;                                                    \
    const int width = out->width;                                                   \
    const int height = out->height;                                                 \
    float z = powf(2.f * fabsf(progress - 0.5f), 3.f) * hypotf(width/2, height/2);  \
                                                                                    \
    for (int p = 0; p < s->nb_planes; p++) {                                        \
        const int bg = s->black[p];                                                 \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);        \
                                                                                    \
        for (int y = slice_start; y < slice_end; y++) {                             \
            const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);      \
            const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);      \
                                                                                    \
            for (int x = 0; x < width; x++) {                                       \
                float dist = hypotf(x - width / 2, y - height / 2);                 \
                int val = progress < 0.5f ? xf1[x] : xf0[x];                        \
                dst[x] = (z < dist) ? bg : val;                                     \
            }                                                                       \
                                                                                    \
            dst += out->linesize[p] / div;                                          \
        }                                                                           \
    }                                                                               \
}

CIRCLECROP_TRANSITION(8, uint8_t, 1)
CIRCLECROP_TRANSITION(16, uint16_t, 2)

#define RECTCROP_TRANSITION(name, type, div)                                        \
static void rectcrop##name##_transition(AVFilterContext *ctx,                       \
                                 const AVFrame *a, const AVFrame *b, AVFrame *out,  \
                                 float progress,                                    \
                                 int slice_start, int slice_end, int jobnr)         \
{                                                                                   \
    XFadeContext *s = ctx->priv;                                                    \
    const int width = out->width;                                                   \
    const int height = out->height;                                                 \
    int zh = fabsf(progress - 0.5f) * height;                                       \
    int zw = fabsf(progress - 0.5f) * width;                                        \
                                                                                    \
    for (int p = 0; p < s->nb_planes; p++) {                                        \
        const int bg = s->black[p];                                                 \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);        \
                                                                                    \
        for (int y = slice_start; y < slice_end; y++) {                             \
            const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);      \
            const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);      \
                                                                                    \
            for (int x = 0; x < width; x++) {                                       \
                int dist = FFABS(x - width  / 2) < zw &&                            \
                           FFABS(y - height / 2) < zh;                              \
                int val = progress < 0.5f ? xf1[x] : xf0[x];                        \
                dst[x] = !dist ? bg : val;                                          \
            }                                                                       \
                                                                                    \
            dst += out->linesize[p] / div;                                          \
        }                                                                           \
    }                                                                               \
}

RECTCROP_TRANSITION(8, uint8_t, 1)
RECTCROP_TRANSITION(16, uint16_t, 2)

#define DISTANCE_TRANSITION(name, type, div)                                        \
static void distance##name##_transition(AVFilterContext *ctx,                       \
                                 const AVFrame *a, const AVFrame *b, AVFrame *out,  \
                                 float progress,                                    \
                                 int slice_start, int slice_end, int jobnr)         \
{                                                                                   \
    XFadeContext *s = ctx->priv;                                                    \
    const int width = out->width;                                                   \
    const float max = s->max_value;                                                 \
                                                                                    \
    for (int y = slice_start; y < slice_end; y++) {                                 \
        for (int x = 0; x < width; x++) {                                           \
            float dist = 0.f;                                                       \
            for (int p = 0; p < s->nb_planes; p++) {                                \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);  \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);  \
                                                                                    \
                dist += (xf0[x] / max - xf1[x] / max) *                             \
                        (xf0[x] / max - xf1[x] / max);                              \
            }                                                                       \
                                                                                    \
            dist = sqrtf(dist) <= progress;                                         \
            for (int p = 0; p < s->nb_planes; p++) {                                \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);  \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);  \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);          \
                dst[x] = mix(mix(xf0[x], xf1[x], dist), xf1[x], progress);          \
            }                                                                       \
        }                                                                           \
    }                                                                               \
}

DISTANCE_TRANSITION(8, uint8_t, 1)
DISTANCE_TRANSITION(16, uint16_t, 2)

#define FADEBLACK_TRANSITION(name, type, div)                                        \
static void fadeblack##name##_transition(AVFilterContext *ctx,                       \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int height = slice_end - slice_start;                                      \
    const int width = out->width;                                                    \
    const float phase = 0.2f;                                                        \
                                                                                     \
    for (int p = 0; p < s->nb_planes; p++) {                                         \
        const type *xf0 = (const type *)(a->data[p] + slice_start * a->linesize[p]); \
        const type *xf1 = (const type *)(b->data[p] + slice_start * b->linesize[p]); \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);         \
        const int bg = s->black[p];                                                  \
                                                                                     \
        for (int y = 0; y < height; y++) {                                           \
            for (int x = 0; x < width; x++) {                                        \
                dst[x] = mix(mix(xf0[x], bg, smoothstep(1.f-phase, 1.f, progress)),  \
                         mix(bg, xf1[x], smoothstep(phase, 1.f, progress)),          \
                             progress);                                              \
            }                                                                        \
                                                                                     \
            dst += out->linesize[p] / div;                                           \
            xf0 += a->linesize[p] / div;                                             \
            xf1 += b->linesize[p] / div;                                             \
        }                                                                            \
    }                                                                                \
}

FADEBLACK_TRANSITION(8, uint8_t, 1)
FADEBLACK_TRANSITION(16, uint16_t, 2)

#define FADEWHITE_TRANSITION(name, type, div)                                        \
static void fadewhite##name##_transition(AVFilterContext *ctx,                       \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int height = slice_end - slice_start;                                      \
    const int width = out->width;                                                    \
    const float phase = 0.2f;                                                        \
                                                                                     \
    for (int p = 0; p < s->nb_planes; p++) {                                         \
        const type *xf0 = (const type *)(a->data[p] + slice_start * a->linesize[p]); \
        const type *xf1 = (const type *)(b->data[p] + slice_start * b->linesize[p]); \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);         \
        const int bg = s->white[p];                                                  \
                                                                                     \
        for (int y = 0; y < height; y++) {                                           \
            for (int x = 0; x < width; x++) {                                        \
                dst[x] = mix(mix(xf0[x], bg, smoothstep(1.f-phase, 1.f, progress)),  \
                         mix(bg, xf1[x], smoothstep(phase, 1.f, progress)),          \
                             progress);                                              \
            }                                                                        \
                                                                                     \
            dst += out->linesize[p] / div;                                           \
            xf0 += a->linesize[p] / div;                                             \
            xf1 += b->linesize[p] / div;                                             \
        }                                                                            \
    }                                                                                \
}

FADEWHITE_TRANSITION(8, uint8_t, 1)
FADEWHITE_TRANSITION(16, uint16_t, 2)

#define RADIAL_TRANSITION(name, type, div)                                           \
static void radial##name##_transition(AVFilterContext *ctx,                          \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int width = out->width;                                                    \
    const int height = out->height;                                                  \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
        for (int x = 0; x < width; x++) {                                            \
            const float smooth = atan2f(x - width / 2, y - height / 2) -             \
                                 (progress - 0.5f) * (M_PI * 2.5f);                  \
            for (int p = 0; p < s->nb_planes; p++) {                                 \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);   \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);   \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = mix(xf1[x], xf0[x], smoothstep(0.f, 1.f, smooth));          \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

RADIAL_TRANSITION(8, uint8_t, 1)
RADIAL_TRANSITION(16, uint16_t, 2)

#define SMOOTHLEFT_TRANSITION(name, type, div)                                       \
static void smoothleft##name##_transition(AVFilterContext *ctx,                      \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int width = out->width;                                                    \
    const float w = width;                                                           \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
        for (int x = 0; x < width; x++) {                                            \
            const float smooth = 1.f + x / w - progress * 2.f;                       \
                                                                                     \
            for (int p = 0; p < s->nb_planes; p++) {                                 \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);   \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);   \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = mix(xf1[x], xf0[x], smoothstep(0.f, 1.f, smooth));          \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

SMOOTHLEFT_TRANSITION(8, uint8_t, 1)
SMOOTHLEFT_TRANSITION(16, uint16_t, 2)

#define SMOOTHRIGHT_TRANSITION(name, type, div)                                      \
static void smoothright##name##_transition(AVFilterContext *ctx,                     \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int width = out->width;                                                    \
    const float w = width;                                                           \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
        for (int x = 0; x < width; x++) {                                            \
            const float smooth = 1.f + (w - 1 - x) / w - progress * 2.f;             \
                                                                                     \
            for (int p = 0; p < s->nb_planes; p++) {                                 \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);   \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);   \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = mix(xf1[x], xf0[x], smoothstep(0.f, 1.f, smooth));          \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

SMOOTHRIGHT_TRANSITION(8, uint8_t, 1)
SMOOTHRIGHT_TRANSITION(16, uint16_t, 2)

#define SMOOTHUP_TRANSITION(name, type, div)                                         \
static void smoothup##name##_transition(AVFilterContext *ctx,                        \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int width = out->width;                                                    \
    const float h = out->height;                                                     \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
        const float smooth = 1.f + y / h - progress * 2.f;                           \
        for (int x = 0; x < width; x++) {                                            \
            for (int p = 0; p < s->nb_planes; p++) {                                 \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);   \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);   \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = mix(xf1[x], xf0[x], smoothstep(0.f, 1.f, smooth));          \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

SMOOTHUP_TRANSITION(8, uint8_t, 1)
SMOOTHUP_TRANSITION(16, uint16_t, 2)

#define SMOOTHDOWN_TRANSITION(name, type, div)                                       \
static void smoothdown##name##_transition(AVFilterContext *ctx,                      \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int width = out->width;                                                    \
    const float h = out->height;                                                     \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
        const float smooth = 1.f + (h - 1 - y) / h - progress * 2.f;                 \
        for (int x = 0; x < width; x++) {                                            \
            for (int p = 0; p < s->nb_planes; p++) {                                 \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);   \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);   \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = mix(xf1[x], xf0[x], smoothstep(0.f, 1.f, smooth));          \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

SMOOTHDOWN_TRANSITION(8, uint8_t, 1)
SMOOTHDOWN_TRANSITION(16, uint16_t, 2)

#define CIRCLEOPEN_TRANSITION(name, type, div)                                       \
static void circleopen##name##_transition(AVFilterContext *ctx,                      \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int width = out->width;                                                    \
    const int height = out->height;                                                  \
    const float z = hypotf(width / 2, height / 2);                                   \
    const float p = (progress - 0.5f) * 3.f;                                         \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
        for (int x = 0; x < width; x++) {                                            \
            const float smooth = hypotf(x - width / 2, y - height / 2) / z + p;      \
            for (int p = 0; p < s->nb_planes; p++) {                                 \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);   \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);   \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = mix(xf0[x], xf1[x], smoothstep(0.f, 1.f, smooth));          \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

CIRCLEOPEN_TRANSITION(8, uint8_t, 1)
CIRCLEOPEN_TRANSITION(16, uint16_t, 2)

#define CIRCLECLOSE_TRANSITION(name, type, div)                                      \
static void circleclose##name##_transition(AVFilterContext *ctx,                     \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int width = out->width;                                                    \
    const int height = out->height;                                                  \
    const float z = hypotf(width / 2, height / 2);                                   \
    const float p = (1.f - progress - 0.5f) * 3.f;                                   \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
        for (int x = 0; x < width; x++) {                                            \
            const float smooth = hypotf(x - width / 2, y - height / 2) / z + p;      \
            for (int p = 0; p < s->nb_planes; p++) {                                 \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);   \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);   \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = mix(xf1[x], xf0[x], smoothstep(0.f, 1.f, smooth));          \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

CIRCLECLOSE_TRANSITION(8, uint8_t, 1)
CIRCLECLOSE_TRANSITION(16, uint16_t, 2)

#define VERTOPEN_TRANSITION(name, type, div)                                         \
static void vertopen##name##_transition(AVFilterContext *ctx,                        \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int width = out->width;                                                    \
    const float w2 = out->width / 2.0;                                                 \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
        for (int x = 0; x < width; x++) {                                            \
            const float smooth = 2.f - fabsf((x - w2) / w2) - progress * 2.f;        \
            for (int p = 0; p < s->nb_planes; p++) {                                 \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);   \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);   \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = mix(xf1[x], xf0[x], smoothstep(0.f, 1.f, smooth));          \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

VERTOPEN_TRANSITION(8, uint8_t, 1)
VERTOPEN_TRANSITION(16, uint16_t, 2)

#define VERTCLOSE_TRANSITION(name, type, div)                                        \
static void vertclose##name##_transition(AVFilterContext *ctx,                       \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int nb_planes = s->nb_planes;                                              \
    const int width = out->width;                                                    \
    const float w2 = out->width / 2.0;                                                 \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
        for (int x = 0; x < width; x++) {                                            \
            const float smooth = 1.f + fabsf((x - w2) / w2) - progress * 2.f;        \
            for (int p = 0; p < nb_planes; p++) {                                    \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);   \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);   \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = mix(xf1[x], xf0[x], smoothstep(0.f, 1.f, smooth));          \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

VERTCLOSE_TRANSITION(8, uint8_t, 1)
VERTCLOSE_TRANSITION(16, uint16_t, 2)

#define HORZOPEN_TRANSITION(name, type, div)                                         \
static void horzopen##name##_transition(AVFilterContext *ctx,                        \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int nb_planes = s->nb_planes;                                              \
    const int width = out->width;                                                    \
    const float h2 = out->height / 2.0;                                                \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
        const float smooth = 2.f - fabsf((y - h2) / h2) - progress * 2.f;            \
        for (int x = 0; x < width; x++) {                                            \
            for (int p = 0; p < nb_planes; p++) {                                    \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);   \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);   \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = mix(xf1[x], xf0[x], smoothstep(0.f, 1.f, smooth));          \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

HORZOPEN_TRANSITION(8, uint8_t, 1)
HORZOPEN_TRANSITION(16, uint16_t, 2)

#define HORZCLOSE_TRANSITION(name, type, div)                                        \
static void horzclose##name##_transition(AVFilterContext *ctx,                       \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int nb_planes = s->nb_planes;                                              \
    const int width = out->width;                                                    \
    const float h2 = out->height / 2.0;                                                \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
        const float smooth = 1.f + fabsf((y - h2) / h2) - progress * 2.f;            \
        for (int x = 0; x < width; x++) {                                            \
            for (int p = 0; p < nb_planes; p++) {                                    \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);   \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);   \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = mix(xf1[x], xf0[x], smoothstep(0.f, 1.f, smooth));          \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

HORZCLOSE_TRANSITION(8, uint8_t, 1)
HORZCLOSE_TRANSITION(16, uint16_t, 2)

static float frand(int x, int y)
{
    const float r = sinf(x * 12.9898f + y * 78.233f) * 43758.545f;

    return r - floorf(r);
}

#define DISSOLVE_TRANSITION(name, type, div)                                         \
static void dissolve##name##_transition(AVFilterContext *ctx,                        \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int nb_planes = s->nb_planes;                                              \
    const int width = out->width;                                                    \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
        for (int x = 0; x < width; x++) {                                            \
            const float smooth = frand(x, y) * 2.f + progress * 2.f - 1.5f;          \
            for (int p = 0; p < nb_planes; p++) {                                    \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);   \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);   \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = smooth >= 0.5f ? xf0[x] : xf1[x];                           \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

DISSOLVE_TRANSITION(8, uint8_t, 1)
DISSOLVE_TRANSITION(16, uint16_t, 2)

#define PIXELIZE_TRANSITION(name, type, div)                                         \
static void pixelize##name##_transition(AVFilterContext *ctx,                        \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int nb_planes = s->nb_planes;                                              \
    const int w = out->width;                                                        \
    const int h = out->height;                                                       \
    const float d = fminf(progress, 1.f - progress);                                 \
    const float dist = ceilf(d * 50.f) / 50.f;                                       \
    const float sqx = 2.f * dist * FFMIN(w, h) / 20.f;                               \
    const float sqy = 2.f * dist * FFMIN(w, h) / 20.f;                               \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
        for (int x = 0; x < w; x++) {                                                \
            int sx = dist > 0.f ? FFMIN((floorf(x / sqx) + .5f) * sqx, w - 1) : x;   \
            int sy = dist > 0.f ? FFMIN((floorf(y / sqy) + .5f) * sqy, h - 1) : y;   \
            for (int p = 0; p < nb_planes; p++) {                                    \
                const type *xf0 = (const type *)(a->data[p] + sy * a->linesize[p]);  \
                const type *xf1 = (const type *)(b->data[p] + sy * b->linesize[p]);  \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = mix(xf0[sx], xf1[sx], progress);                            \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

PIXELIZE_TRANSITION(8, uint8_t, 1)
PIXELIZE_TRANSITION(16, uint16_t, 2)

#define DIAGTL_TRANSITION(name, type, div)                                           \
static void diagtl##name##_transition(AVFilterContext *ctx,                          \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int nb_planes = s->nb_planes;                                              \
    const int width = out->width;                                                    \
    const float w = width;                                                           \
    const float h = out->height;                                                     \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
        for (int x = 0; x < width; x++) {                                            \
            const float smooth = 1.f + x / w * y / h - progress * 2.f;               \
                                                                                     \
            for (int p = 0; p < nb_planes; p++) {                                    \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);   \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);   \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = mix(xf1[x], xf0[x], smoothstep(0.f, 1.f, smooth));          \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

DIAGTL_TRANSITION(8, uint8_t, 1)
DIAGTL_TRANSITION(16, uint16_t, 2)

#define DIAGTR_TRANSITION(name, type, div)                                           \
static void diagtr##name##_transition(AVFilterContext *ctx,                          \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int nb_planes = s->nb_planes;                                              \
    const int width = out->width;                                                    \
    const float w = width;                                                           \
    const float h = out->height;                                                     \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
        for (int x = 0; x < width; x++) {                                            \
            const float smooth = 1.f + (w - 1 - x) / w * y / h - progress * 2.f;     \
                                                                                     \
            for (int p = 0; p < nb_planes; p++) {                                    \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);   \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);   \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = mix(xf1[x], xf0[x], smoothstep(0.f, 1.f, smooth));          \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

DIAGTR_TRANSITION(8, uint8_t, 1)
DIAGTR_TRANSITION(16, uint16_t, 2)

#define DIAGBL_TRANSITION(name, type, div)                                           \
static void diagbl##name##_transition(AVFilterContext *ctx,                          \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int nb_planes = s->nb_planes;                                              \
    const int width = out->width;                                                    \
    const float w = width;                                                           \
    const float h = out->height;                                                     \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
        for (int x = 0; x < width; x++) {                                            \
            const float smooth = 1.f + x / w * (h - 1 - y) / h - progress * 2.f;     \
                                                                                     \
            for (int p = 0; p < nb_planes; p++) {                                    \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);   \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);   \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = mix(xf1[x], xf0[x], smoothstep(0.f, 1.f, smooth));          \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

DIAGBL_TRANSITION(8, uint8_t, 1)
DIAGBL_TRANSITION(16, uint16_t, 2)

#define DIAGBR_TRANSITION(name, type, div)                                           \
static void diagbr##name##_transition(AVFilterContext *ctx,                          \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int nb_planes = s->nb_planes;                                              \
    const int width = out->width;                                                    \
    const float w = width;                                                           \
    const float h = out->height;                                                     \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
        for (int x = 0; x < width; x++) {                                            \
            const float smooth = 1.f + (w - 1 - x) / w * (h - 1 - y) / h -           \
                                 progress * 2.f;                                     \
                                                                                     \
            for (int p = 0; p < nb_planes; p++) {                                    \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);   \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);   \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = mix(xf1[x], xf0[x], smoothstep(0.f, 1.f, smooth));          \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

DIAGBR_TRANSITION(8, uint8_t, 1)
DIAGBR_TRANSITION(16, uint16_t, 2)

#define HLSLICE_TRANSITION(name, type, div)                                          \
static void hlslice##name##_transition(AVFilterContext *ctx,                         \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int nb_planes = s->nb_planes;                                              \
    const int width = out->width;                                                    \
    const float w = width;                                                           \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
        for (int x = 0; x < width; x++) {                                            \
            const float smooth = smoothstep(-0.5f, 0.f, x / w - progress * 1.5f);    \
            const float ss = smooth <= fract(10.f * x / w) ? 0.f : 1.f;              \
                                                                                     \
            for (int p = 0; p < nb_planes; p++) {                                    \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);   \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);   \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = mix(xf1[x], xf0[x], ss);                                    \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

HLSLICE_TRANSITION(8, uint8_t, 1)
HLSLICE_TRANSITION(16, uint16_t, 2)

#define HRSLICE_TRANSITION(name, type, div)                                          \
static void hrslice##name##_transition(AVFilterContext *ctx,                         \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int nb_planes = s->nb_planes;                                              \
    const int width = out->width;                                                    \
    const float w = width;                                                           \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
        for (int x = 0; x < width; x++) {                                            \
            const float xx = (w - 1 - x) / w;                                        \
            const float smooth = smoothstep(-0.5f, 0.f, xx - progress * 1.5f);       \
            const float ss = smooth <= fract(10.f * xx) ? 0.f : 1.f;                 \
                                                                                     \
            for (int p = 0; p < nb_planes; p++) {                                    \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);   \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);   \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = mix(xf1[x], xf0[x], ss);                                    \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

HRSLICE_TRANSITION(8, uint8_t, 1)
HRSLICE_TRANSITION(16, uint16_t, 2)

#define VUSLICE_TRANSITION(name, type, div)                                          \
static void vuslice##name##_transition(AVFilterContext *ctx,                         \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int nb_planes = s->nb_planes;                                              \
    const int width = out->width;                                                    \
    const float h = out->height;                                                     \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
         const float smooth = smoothstep(-0.5f, 0.f, y / h - progress * 1.5f);       \
         const float ss = smooth <= fract(10.f * y / h) ? 0.f : 1.f;                 \
                                                                                     \
         for (int x = 0; x < width; x++) {                                           \
            for (int p = 0; p < nb_planes; p++) {                                    \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);   \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);   \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = mix(xf1[x], xf0[x], ss);                                    \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

VUSLICE_TRANSITION(8, uint8_t, 1)
VUSLICE_TRANSITION(16, uint16_t, 2)

#define VDSLICE_TRANSITION(name, type, div)                                          \
static void vdslice##name##_transition(AVFilterContext *ctx,                         \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int nb_planes = s->nb_planes;                                              \
    const int width = out->width;                                                    \
    const float h = out->height;                                                     \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
         const float yy = (h - 1 - y) / h;                                           \
         const float smooth = smoothstep(-0.5f, 0.f, yy - progress * 1.5f);          \
         const float ss = smooth <= fract(10.f * yy) ? 0.f : 1.f;                    \
                                                                                     \
         for (int x = 0; x < width; x++) {                                           \
            for (int p = 0; p < nb_planes; p++) {                                    \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);   \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);   \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = mix(xf1[x], xf0[x], ss);                                    \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

VDSLICE_TRANSITION(8, uint8_t, 1)
VDSLICE_TRANSITION(16, uint16_t, 2)

#define HBLUR_TRANSITION(name, type, div)                                            \
static void hblur##name##_transition(AVFilterContext *ctx,                           \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int nb_planes = s->nb_planes;                                              \
    const int width = out->width;                                                    \
    const float prog = progress <= 0.5f ? progress * 2.f : (1.f - progress) * 2.f;   \
    const int size = 1 + (width / 2) * prog;                                         \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
        for (int p = 0; p < nb_planes; p++) {                                        \
            const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);       \
            const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);       \
            type *dst = (type *)(out->data[p] + y * out->linesize[p]);               \
            float sum0 = 0.f;                                                        \
            float sum1 = 0.f;                                                        \
            float cnt = size;                                                        \
                                                                                     \
            for (int x = 0; x < size; x++) {                                         \
                sum0 += xf0[x];                                                      \
                sum1 += xf1[x];                                                      \
            }                                                                        \
                                                                                     \
            for (int x = 0; x < width; x++) {                                        \
                dst[x] = mix(sum0 / cnt, sum1 / cnt, progress);                      \
                                                                                     \
                if (x + size < width) {                                              \
                    sum0 += xf0[x + size] - xf0[x];                                  \
                    sum1 += xf1[x + size] - xf1[x];                                  \
                } else {                                                             \
                    sum0 -= xf0[x];                                                  \
                    sum1 -= xf1[x];                                                  \
                    cnt--;                                                           \
                }                                                                    \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

HBLUR_TRANSITION(8, uint8_t, 1)
HBLUR_TRANSITION(16, uint16_t, 2)

#define FADEGRAYS_TRANSITION(name, type, div)                                        \
static void fadegrays##name##_transition(AVFilterContext *ctx,                       \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int width = out->width;                                                    \
    const int is_rgb = s->is_rgb;                                                    \
    const int mid = (s->max_value + 1) / 2;                                          \
    const float phase = 0.2f;                                                        \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
        for (int x = 0; x < width; x++) {                                            \
            int bg[2][4];                                                            \
            if (is_rgb) {                                                            \
                bg[0][0] = bg[1][0] = 0;                                             \
                for (int p = 0; p < s->nb_planes; p++) {                             \
                    const type *xf0 = (const type *)(a->data[p] +                    \
                                                     y * a->linesize[p]);            \
                    const type *xf1 = (const type *)(b->data[p] +                    \
                                                     y * b->linesize[p]);            \
                    if (p == 3) {                                                    \
                        bg[0][3] = xf0[x];                                           \
                        bg[1][3] = xf1[x];                                           \
                    } else  {                                                        \
                        bg[0][0] += xf0[x];                                          \
                        bg[1][0] += xf1[x];                                          \
                    }                                                                \
                }                                                                    \
                bg[0][0] = bg[0][0] / 3;                                             \
                bg[1][0] = bg[1][0] / 3;                                             \
                bg[0][1] = bg[0][2] = bg[0][0];                                      \
                bg[1][1] = bg[1][2] = bg[1][0];                                      \
            } else {                                                                 \
                const type *yf0 = (const type *)(a->data[0] +                        \
                                                 y * a->linesize[0]);                \
                const type *yf1 = (const type *)(b->data[0] +                        \
                                                 y * a->linesize[0]);                \
                bg[0][0] = yf0[x];                                                   \
                bg[1][0] = yf1[x];                                                   \
                if (s->nb_planes == 4) {                                             \
                    const type *af0 = (const type *)(a->data[3] +                    \
                                                     y * a->linesize[3]);            \
                    const type *af1 = (const type *)(b->data[3] +                    \
                                                     y * a->linesize[3]);            \
                    bg[0][3] = af0[x];                                               \
                    bg[1][3] = af1[x];                                               \
                }                                                                    \
                bg[0][1] = bg[1][1] = mid;                                           \
                bg[0][2] = bg[1][2] = mid;                                           \
            }                                                                        \
                                                                                     \
            for (int p = 0; p < s->nb_planes; p++) {                                 \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);   \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);   \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = mix(mix(xf0[x], bg[0][p],                                   \
                                 smoothstep(1.f-phase, 1.f, progress)),              \
                         mix(bg[1][p], xf1[x], smoothstep(phase, 1.f, progress)),    \
                             progress);                                              \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

FADEGRAYS_TRANSITION(8, uint8_t, 1)
FADEGRAYS_TRANSITION(16, uint16_t, 2)

#define WIPETL_TRANSITION(name, type, div)                                           \
static void wipetl##name##_transition(AVFilterContext *ctx,                          \
                                const AVFrame *a, const AVFrame *b, AVFrame *out,    \
                                float progress,                                      \
                                int slice_start, int slice_end, int jobnr)           \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int height = slice_end - slice_start;                                      \
    const int width = out->width;                                                    \
    const int zw = out->width * progress;                                            \
    const int zh = out->height * progress;                                           \
                                                                                     \
    for (int p = 0; p < s->nb_planes; p++) {                                         \
        const type *xf0 = (const type *)(a->data[p] + slice_start * a->linesize[p]); \
        const type *xf1 = (const type *)(b->data[p] + slice_start * b->linesize[p]); \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);         \
                                                                                     \
        for (int y = 0; y < height; y++) {                                           \
            for (int x = 0; x < width; x++) {                                        \
                dst[x] = slice_start + y <= zh &&                                    \
                         x <= zw ? xf0[x] : xf1[x];                                  \
            }                                                                        \
                                                                                     \
            dst += out->linesize[p] / div;                                           \
            xf0 += a->linesize[p] / div;                                             \
            xf1 += b->linesize[p] / div;                                             \
        }                                                                            \
    }                                                                                \
}

WIPETL_TRANSITION(8, uint8_t, 1)
WIPETL_TRANSITION(16, uint16_t, 2)

#define WIPETR_TRANSITION(name, type, div)                                           \
static void wipetr##name##_transition(AVFilterContext *ctx,                          \
                                const AVFrame *a, const AVFrame *b, AVFrame *out,    \
                                float progress,                                      \
                                int slice_start, int slice_end, int jobnr)           \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int height = slice_end - slice_start;                                      \
    const int width = out->width;                                                    \
    const int zw = width * (1.f - progress);                                         \
    const int zh = out->height * progress;                                           \
                                                                                     \
    for (int p = 0; p < s->nb_planes; p++) {                                         \
        const type *xf0 = (const type *)(a->data[p] + slice_start * a->linesize[p]); \
        const type *xf1 = (const type *)(b->data[p] + slice_start * b->linesize[p]); \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);         \
                                                                                     \
        for (int y = 0; y < height; y++) {                                           \
            for (int x = 0; x < width; x++) {                                        \
                dst[x] = slice_start + y <= zh &&                                    \
                         x > zw ? xf0[x] : xf1[x];                                   \
            }                                                                        \
                                                                                     \
            dst += out->linesize[p] / div;                                           \
            xf0 += a->linesize[p] / div;                                             \
            xf1 += b->linesize[p] / div;                                             \
        }                                                                            \
    }                                                                                \
}

WIPETR_TRANSITION(8, uint8_t, 1)
WIPETR_TRANSITION(16, uint16_t, 2)

#define WIPEBL_TRANSITION(name, type, div)                                           \
static void wipebl##name##_transition(AVFilterContext *ctx,                          \
                                const AVFrame *a, const AVFrame *b, AVFrame *out,    \
                                float progress,                                      \
                                int slice_start, int slice_end, int jobnr)           \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int height = slice_end - slice_start;                                      \
    const int width = out->width;                                                    \
    const int zw = width * progress;                                                 \
    const int zh = out->height * (1.f - progress);                                   \
                                                                                     \
    for (int p = 0; p < s->nb_planes; p++) {                                         \
        const type *xf0 = (const type *)(a->data[p] + slice_start * a->linesize[p]); \
        const type *xf1 = (const type *)(b->data[p] + slice_start * b->linesize[p]); \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);         \
                                                                                     \
        for (int y = 0; y < height; y++) {                                           \
            for (int x = 0; x < width; x++) {                                        \
                dst[x] = slice_start + y > zh &&                                     \
                         x <= zw ? xf0[x] : xf1[x];                                  \
            }                                                                        \
                                                                                     \
            dst += out->linesize[p] / div;                                           \
            xf0 += a->linesize[p] / div;                                             \
            xf1 += b->linesize[p] / div;                                             \
        }                                                                            \
    }                                                                                \
}

WIPEBL_TRANSITION(8, uint8_t, 1)
WIPEBL_TRANSITION(16, uint16_t, 2)

#define WIPEBR_TRANSITION(name, type, div)                                           \
static void wipebr##name##_transition(AVFilterContext *ctx,                          \
                                const AVFrame *a, const AVFrame *b, AVFrame *out,    \
                                float progress,                                      \
                                int slice_start, int slice_end, int jobnr)           \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int height = slice_end - slice_start;                                      \
    const int zh = out->height * (1.f - progress);                                   \
    const int width = out->width;                                                    \
    const int zw = width * (1.f - progress);                                         \
                                                                                     \
    for (int p = 0; p < s->nb_planes; p++) {                                         \
        const type *xf0 = (const type *)(a->data[p] + slice_start * a->linesize[p]); \
        const type *xf1 = (const type *)(b->data[p] + slice_start * b->linesize[p]); \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);         \
                                                                                     \
        for (int y = 0; y < height; y++) {                                           \
            for (int x = 0; x < width; x++) {                                        \
                dst[x] = slice_start + y > zh &&                                     \
                         x > zw ? xf0[x] : xf1[x];                                   \
            }                                                                        \
                                                                                     \
            dst += out->linesize[p] / div;                                           \
            xf0 += a->linesize[p] / div;                                             \
            xf1 += b->linesize[p] / div;                                             \
        }                                                                            \
    }                                                                                \
}

WIPEBR_TRANSITION(8, uint8_t, 1)
WIPEBR_TRANSITION(16, uint16_t, 2)

#define SQUEEZEH_TRANSITION(name, type, div)                                         \
static void squeezeh##name##_transition(AVFilterContext *ctx,                        \
                                const AVFrame *a, const AVFrame *b, AVFrame *out,    \
                                float progress,                                      \
                                int slice_start, int slice_end, int jobnr)           \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const float h = out->height;                                                     \
    const int height = slice_end - slice_start;                                      \
    const int width = out->width;                                                    \
                                                                                     \
    for (int p = 0; p < s->nb_planes; p++) {                                         \
        const type *xf1 = (const type *)(b->data[p] + slice_start * b->linesize[p]); \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);         \
                                                                                     \
        for (int y = 0; y < height; y++) {                                           \
            const float z = .5f + ((slice_start + y) / h - .5f) / progress;          \
                                                                                     \
            if (z < 0.f || z > 1.f) {                                                \
                for (int x = 0; x < width; x++)                                      \
                    dst[x] = xf1[x];                                                 \
            } else {                                                                 \
                const int yy = lrintf(z * (h - 1.f));                                \
                const type *xf0 = (const type *)(a->data[p] + yy * a->linesize[p]);  \
                                                                                     \
                for (int x = 0; x < width; x++)                                      \
                    dst[x] = xf0[x];                                                 \
            }                                                                        \
                                                                                     \
            dst += out->linesize[p] / div;                                           \
            xf1 += b->linesize[p] / div;                                             \
        }                                                                            \
    }                                                                                \
}

SQUEEZEH_TRANSITION(8, uint8_t, 1)
SQUEEZEH_TRANSITION(16, uint16_t, 2)

#define SQUEEZEV_TRANSITION(name, type, div)                                         \
static void squeezev##name##_transition(AVFilterContext *ctx,                        \
                                const AVFrame *a, const AVFrame *b, AVFrame *out,    \
                                float progress,                                      \
                                int slice_start, int slice_end, int jobnr)           \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int width = out->width;                                                    \
    const float w = width;                                                           \
    const int height = slice_end - slice_start;                                      \
                                                                                     \
    for (int p = 0; p < s->nb_planes; p++) {                                         \
        const type *xf0 = (const type *)(a->data[p] + slice_start * a->linesize[p]); \
        const type *xf1 = (const type *)(b->data[p] + slice_start * b->linesize[p]); \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);         \
                                                                                     \
        for (int y = 0; y < height; y++) {                                           \
            for (int x = 0; x < width; x++) {                                        \
                const float z = .5f + (x / w - .5f) / progress;                      \
                                                                                     \
                if (z < 0.f || z > 1.f) {                                            \
                    dst[x] = xf1[x];                                                 \
                } else {                                                             \
                    const int xx = lrintf(z * (w - 1.f));                            \
                                                                                     \
                    dst[x] = xf0[xx];                                                \
                }                                                                    \
            }                                                                        \
                                                                                     \
            dst += out->linesize[p] / div;                                           \
            xf0 += a->linesize[p] / div;                                             \
            xf1 += b->linesize[p] / div;                                             \
        }                                                                            \
    }                                                                                \
}

SQUEEZEV_TRANSITION(8, uint8_t, 1)
SQUEEZEV_TRANSITION(16, uint16_t, 2)

static void zoom(float *u, float *v, float amount)
{
    *u = 0.5f + ((*u - 0.5f) * amount);
    *v = 0.5f + ((*v - 0.5f) * amount);
}

#define ZOOMIN_TRANSITION(name, type, div)                                           \
static void zoomin##name##_transition(AVFilterContext *ctx,                          \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int width = out->width;                                                    \
    const float w = width;                                                           \
    const float h = out->height;                                                     \
    const float zf = smoothstep(0.5f, 1.f, progress);                                \
                                                                                     \
    for (int p = 0; p < s->nb_planes; p++) {                                         \
        const type *xf0 = (const type *)(a->data[p]);                                \
        const type *xf1 = (const type *)(b->data[p] + slice_start * b->linesize[p]); \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);         \
                                                                                     \
        for (int y = slice_start; y < slice_end; y++) {                              \
            for (int x = 0; x < width; x++) {                                        \
                float zv, u, v;                                                      \
                int iu, iv;                                                          \
                                                                                     \
                u = x / w;                                                           \
                v = y / h;                                                           \
                zoom(&u, &v, zf);                                                    \
                iu = ceilf(u * (w - 1));                                             \
                iv = ceilf(v * (h - 1));                                             \
                zv = xf0[iu + iv * a->linesize[p] / div];                            \
                dst[x] = mix(zv, xf1[x], smoothstep(0.f, 0.5f, progress));           \
            }                                                                        \
            dst += out->linesize[p] / div;                                           \
            xf1 += b->linesize[p] / div;                                             \
        }                                                                            \
    }                                                                                \
}

ZOOMIN_TRANSITION(8, uint8_t, 1)
ZOOMIN_TRANSITION(16, uint16_t, 2)

#define FADEFAST_TRANSITION(name, type, div)                                         \
static void fadefast##name##_transition(AVFilterContext *ctx,                        \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int height = slice_end - slice_start;                                      \
    const int width = out->width;                                                    \
    const float imax = 1.f / s->max_value;                                           \
                                                                                     \
    for (int p = 0; p < s->nb_planes; p++) {                                         \
        const type *xf0 = (const type *)(a->data[p] + slice_start * a->linesize[p]); \
        const type *xf1 = (const type *)(b->data[p] + slice_start * b->linesize[p]); \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);         \
                                                                                     \
        for (int y = 0; y < height; y++) {                                           \
            for (int x = 0; x < width; x++) {                                        \
                dst[x] = mix(xf0[x], xf1[x], powf(progress, 1.f +                    \
                                                  logf(1.f+FFABS(xf0[x]-xf1[x])*imax)\
                                                  ));                                \
            }                                                                        \
                                                                                     \
            dst += out->linesize[p] / div;                                           \
            xf0 += a->linesize[p] / div;                                             \
            xf1 += b->linesize[p] / div;                                             \
        }                                                                            \
    }                                                                                \
}

FADEFAST_TRANSITION(8, uint8_t, 1)
FADEFAST_TRANSITION(16, uint16_t, 2)

#define FADESLOW_TRANSITION(name, type, div)                                         \
static void fadeslow##name##_transition(AVFilterContext *ctx,                        \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int height = slice_end - slice_start;                                      \
    const int width = out->width;                                                    \
    const float imax = 1.f / s->max_value;                                           \
                                                                                     \
    for (int p = 0; p < s->nb_planes; p++) {                                         \
        const type *xf0 = (const type *)(a->data[p] + slice_start * a->linesize[p]); \
        const type *xf1 = (const type *)(b->data[p] + slice_start * b->linesize[p]); \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);         \
                                                                                     \
        for (int y = 0; y < height; y++) {                                           \
            for (int x = 0; x < width; x++) {                                        \
                dst[x] = mix(xf0[x], xf1[x], powf(progress, 1.f +                    \
                                                  logf(2.f-FFABS(xf0[x]-xf1[x])*imax)\
                                                  ));                                \
            }                                                                        \
                                                                                     \
            dst += out->linesize[p] / div;                                           \
            xf0 += a->linesize[p] / div;                                             \
            xf1 += b->linesize[p] / div;                                             \
        }                                                                            \
    }                                                                                \
}

FADESLOW_TRANSITION(8, uint8_t, 1)
FADESLOW_TRANSITION(16, uint16_t, 2)

#define HWIND_TRANSITION(name, z, type, div, expr)                                   \
static void h##z##wind##name##_transition(AVFilterContext *ctx,                      \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int width = out->width;                                                    \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
        const float r = frand(0, y);                                                 \
        for (int x = 0; x < width; x++) {                                            \
            const float fx = expr x / (float)width;                                  \
            for (int p = 0; p < s->nb_planes; p++) {                                 \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);   \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);   \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = mix(xf1[x], xf0[x], smoothstep(0.f,-0.2f,  fx * (1.f - 0.2f)\
                                                        + 0.2f * r - (1.f - progress)\
                                                        * (1.f + 0.2f)));            \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

HWIND_TRANSITION(8,  l, uint8_t,  1, 1.f - )
HWIND_TRANSITION(16, l, uint16_t, 2, 1.f - )
HWIND_TRANSITION(8,  r, uint8_t,  1, )
HWIND_TRANSITION(16, r, uint16_t, 2, )

#define VWIND_TRANSITION(name, z, type, div, expr)                                   \
static void v##z##wind##name##_transition(AVFilterContext *ctx,                      \
                            const AVFrame *a, const AVFrame *b, AVFrame *out,        \
                            float progress,                                          \
                            int slice_start, int slice_end, int jobnr)               \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int width = out->width;                                                    \
                                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                                  \
        const float fy = expr y / (float)out->height;                                \
        for (int x = 0; x < width; x++) {                                            \
            const float r = frand(x, 0);                                             \
            for (int p = 0; p < s->nb_planes; p++) {                                 \
                const type *xf0 = (const type *)(a->data[p] + y * a->linesize[p]);   \
                const type *xf1 = (const type *)(b->data[p] + y * b->linesize[p]);   \
                type *dst = (type *)(out->data[p] + y * out->linesize[p]);           \
                                                                                     \
                dst[x] = mix(xf1[x], xf0[x], smoothstep(0.f,-0.2f, fy * (1.f - 0.2f) \
                                                        + 0.2f * r - (1.f - progress)\
                                                        * (1.f + 0.2f)));            \
            }                                                                        \
        }                                                                            \
    }                                                                                \
}

VWIND_TRANSITION(8,  u, uint8_t,  1, 1.f - )
VWIND_TRANSITION(16, u, uint16_t, 2, 1.f - )
VWIND_TRANSITION(8,  d, uint8_t,  1, )
VWIND_TRANSITION(16, d, uint16_t, 2, )

#define COVERH_TRANSITION(dir, name, type, div, expr)                                \
static void cover##dir##name##_transition(AVFilterContext *ctx,                      \
                                 const AVFrame *a, const AVFrame *b, AVFrame *out,   \
                                 float progress,                                     \
                                 int slice_start, int slice_end, int jobnr)          \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int height = slice_end - slice_start;                                      \
    const int width = out->width;                                                    \
    const int z = (expr progress) * width;                                           \
                                                                                     \
    for (int p = 0; p < s->nb_planes; p++) {                                         \
        const type *xf0 = (const type *)(a->data[p] + slice_start * a->linesize[p]); \
        const type *xf1 = (const type *)(b->data[p] + slice_start * b->linesize[p]); \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);         \
                                                                                     \
        for (int y = 0; y < height; y++) {                                           \
            for (int x = 0; x < width; x++) {                                        \
                const int zx = z + x;                                                \
                const int zz = zx % width + width * (zx < 0);                        \
                dst[x] = (zx >= 0) && (zx < width) ? xf1[zz] : xf0[x];               \
            }                                                                        \
                                                                                     \
            dst += out->linesize[p] / div;                                           \
            xf0 += a->linesize[p] / div;                                             \
            xf1 += b->linesize[p] / div;                                             \
        }                                                                            \
    }                                                                                \
}

COVERH_TRANSITION(left,   8, uint8_t,  1, -)
COVERH_TRANSITION(left,  16, uint16_t, 2, -)
COVERH_TRANSITION(right,  8, uint8_t,  1, )
COVERH_TRANSITION(right, 16, uint16_t, 2, )

#define COVERV_TRANSITION(dir, name, type, div, expr)                               \
static void cover##dir##name##_transition(AVFilterContext *ctx,                     \
                                 const AVFrame *a, const AVFrame *b, AVFrame *out,  \
                                 float progress,                                    \
                                 int slice_start, int slice_end, int jobnr)         \
{                                                                                   \
    XFadeContext *s = ctx->priv;                                                    \
    const int height = out->height;                                                 \
    const int width = out->width;                                                   \
    const int z = (expr progress) * height;                                         \
                                                                                    \
    for (int p = 0; p < s->nb_planes; p++) {                                        \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);        \
                                                                                    \
        for (int y = slice_start; y < slice_end; y++) {                             \
            const int zy = z + y;                                                   \
            const int zz = zy % height + height * (zy < 0);                         \
            const type *xf0 = (const type *)(a->data[p] +  y * a->linesize[p]);     \
            const type *xf1 = (const type *)(b->data[p] + zz * b->linesize[p]);     \
                                                                                    \
            for (int x = 0; x < width; x++)                                         \
                dst[x] = (zy >= 0) && (zy < height) ? xf1[x] : xf0[x];              \
                                                                                    \
            dst += out->linesize[p] / div;                                          \
        }                                                                           \
    }                                                                               \
}

COVERV_TRANSITION(up,    8, uint8_t,  1, -)
COVERV_TRANSITION(up,   16, uint16_t, 2, -)
COVERV_TRANSITION(down,  8, uint8_t,  1, )
COVERV_TRANSITION(down, 16, uint16_t, 2, )

#define REVEALH_TRANSITION(dir, name, type, div, expr)                               \
static void reveal##dir##name##_transition(AVFilterContext *ctx,                     \
                                 const AVFrame *a, const AVFrame *b, AVFrame *out,   \
                                 float progress,                                     \
                                 int slice_start, int slice_end, int jobnr)          \
{                                                                                    \
    XFadeContext *s = ctx->priv;                                                     \
    const int height = slice_end - slice_start;                                      \
    const int width = out->width;                                                    \
    const int z = (expr progress) * width;                                           \
                                                                                     \
    for (int p = 0; p < s->nb_planes; p++) {                                         \
        const type *xf0 = (const type *)(a->data[p] + slice_start * a->linesize[p]); \
        const type *xf1 = (const type *)(b->data[p] + slice_start * b->linesize[p]); \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);         \
                                                                                     \
        for (int y = 0; y < height; y++) {                                           \
            for (int x = 0; x < width; x++) {                                        \
                const int zx = z + x;                                                \
                const int zz = zx % width + width * (zx < 0);                        \
                dst[x] = (zx >= 0) && (zx < width) ? xf1[x] : xf0[zz];               \
            }                                                                        \
                                                                                     \
            dst += out->linesize[p] / div;                                           \
            xf0 += a->linesize[p] / div;                                             \
            xf1 += b->linesize[p] / div;                                             \
        }                                                                            \
    }                                                                                \
}

REVEALH_TRANSITION(left,   8, uint8_t,  1, -)
REVEALH_TRANSITION(left,  16, uint16_t, 2, -)
REVEALH_TRANSITION(right,  8, uint8_t,  1, )
REVEALH_TRANSITION(right, 16, uint16_t, 2, )

#define REVEALV_TRANSITION(dir, name, type, div, expr)                              \
static void reveal##dir##name##_transition(AVFilterContext *ctx,                    \
                                 const AVFrame *a, const AVFrame *b, AVFrame *out,  \
                                 float progress,                                    \
                                 int slice_start, int slice_end, int jobnr)         \
{                                                                                   \
    XFadeContext *s = ctx->priv;                                                    \
    const int height = out->height;                                                 \
    const int width = out->width;                                                   \
    const int z = (expr progress) * height;                                         \
                                                                                    \
    for (int p = 0; p < s->nb_planes; p++) {                                        \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]);        \
                                                                                    \
        for (int y = slice_start; y < slice_end; y++) {                             \
            const int zy = z + y;                                                   \
            const int zz = zy % height + height * (zy < 0);                         \
            const type *xf0 = (const type *)(a->data[p] + zz * a->linesize[p]);     \
            const type *xf1 = (const type *)(b->data[p] +  y * b->linesize[p]);     \
                                                                                    \
            for (int x = 0; x < width; x++)                                         \
                dst[x] = (zy >= 0) && (zy < height) ? xf1[x] : xf0[x];              \
                                                                                    \
            dst += out->linesize[p] / div;                                          \
        }                                                                           \
    }                                                                               \
}

REVEALV_TRANSITION(up,    8, uint8_t,  1, -)
REVEALV_TRANSITION(up,   16, uint16_t, 2, -)
REVEALV_TRANSITION(down,  8, uint8_t,  1, )
REVEALV_TRANSITION(down, 16, uint16_t, 2, )

static inline double getpix(void *priv, double x, double y, int plane, int nb)
{
    XFadeContext *s = priv;
    AVFrame *in = s->xf[nb];
    const uint8_t *src = in->data[FFMIN(plane, s->nb_planes - 1)];
    int linesize = in->linesize[FFMIN(plane, s->nb_planes - 1)];
    const int w = in->width;
    const int h = in->height;

    int xi, yi;

    xi = av_clipd(x, 0, w - 1);
    yi = av_clipd(y, 0, h - 1);

    if (s->depth > 8) {
        const uint16_t *src16 = (const uint16_t*)src;

        linesize /= 2;
        return src16[xi + yi * linesize];
    } else {
        return src[xi + yi * linesize];
    }
}

static double a0(void *priv, double x, double y) { return getpix(priv, x, y, 0, 0); }
static double a1(void *priv, double x, double y) { return getpix(priv, x, y, 1, 0); }
static double a2(void *priv, double x, double y) { return getpix(priv, x, y, 2, 0); }
static double a3(void *priv, double x, double y) { return getpix(priv, x, y, 3, 0); }

static double b0(void *priv, double x, double y) { return getpix(priv, x, y, 0, 1); }
static double b1(void *priv, double x, double y) { return getpix(priv, x, y, 1, 1); }
static double b2(void *priv, double x, double y) { return getpix(priv, x, y, 2, 1); }
static double b3(void *priv, double x, double y) { return getpix(priv, x, y, 3, 1); }

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink0 = ctx->inputs[0];
    AVFilterLink *inlink1 = ctx->inputs[1];
    FilterLink      *inl0 = ff_filter_link(inlink0);
    FilterLink      *inl1 = ff_filter_link(inlink1);
    FilterLink        *ol = ff_filter_link(outlink);
    XFadeContext *s = ctx->priv;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(inlink0->format);

    if (inlink0->w != inlink1->w || inlink0->h != inlink1->h) {
        av_log(ctx, AV_LOG_ERROR, "First input link %s parameters "
               "(size %dx%d) do not match the corresponding "
               "second input link %s parameters (size %dx%d)\n",
               ctx->input_pads[0].name, inlink0->w, inlink0->h,
               ctx->input_pads[1].name, inlink1->w, inlink1->h);
        return AVERROR(EINVAL);
    }

    if (inlink0->time_base.num != inlink1->time_base.num ||
        inlink0->time_base.den != inlink1->time_base.den) {
        av_log(ctx, AV_LOG_ERROR, "First input link %s timebase "
               "(%d/%d) do not match the corresponding "
               "second input link %s timebase (%d/%d)\n",
               ctx->input_pads[0].name, inlink0->time_base.num, inlink0->time_base.den,
               ctx->input_pads[1].name, inlink1->time_base.num, inlink1->time_base.den);
        return AVERROR(EINVAL);
    }

    if (!inl0->frame_rate.num || !inl0->frame_rate.den) {
        av_log(ctx, AV_LOG_ERROR, "The inputs needs to be a constant frame rate; "
               "current rate of %d/%d is invalid\n", inl0->frame_rate.num, inl0->frame_rate.den);
        return AVERROR(EINVAL);
    }

    if (inl0->frame_rate.num != inl1->frame_rate.num ||
        inl0->frame_rate.den != inl1->frame_rate.den) {
        av_log(ctx, AV_LOG_ERROR, "First input link %s frame rate "
               "(%d/%d) do not match the corresponding "
               "second input link %s frame rate (%d/%d)\n",
               ctx->input_pads[0].name, inl0->frame_rate.num, inl0->frame_rate.den,
               ctx->input_pads[1].name, inl1->frame_rate.num, inl1->frame_rate.den);
        return AVERROR(EINVAL);
    }

    outlink->w = inlink0->w;
    outlink->h = inlink0->h;
    outlink->time_base = inlink0->time_base;
    outlink->sample_aspect_ratio = inlink0->sample_aspect_ratio;
    ol->frame_rate = inl0->frame_rate;

    s->depth = pix_desc->comp[0].depth;
    s->is_rgb = !!(pix_desc->flags & AV_PIX_FMT_FLAG_RGB);
    s->nb_planes = av_pix_fmt_count_planes(inlink0->format);
    s->max_value = (1 << s->depth) - 1;
    s->black[0] = 0;
    s->black[1] = s->black[2] = s->is_rgb ? 0 : s->max_value / 2;
    s->black[3] = s->max_value;
    s->white[0] = s->white[3] = s->max_value;
    s->white[1] = s->white[2] = s->is_rgb ? s->max_value : s->max_value / 2;

    s->start_pts = s->inputs_offset_pts = AV_NOPTS_VALUE;

    if (s->duration)
        s->duration_pts = av_rescale_q(s->duration, AV_TIME_BASE_Q, outlink->time_base);

    switch (s->transition) {
    case CUSTOM:     s->transitionf = s->depth <= 8 ? custom8_transition     : custom16_transition;     break;
    case FADE:       s->transitionf = s->depth <= 8 ? fade8_transition       : fade16_transition;       break;
    case WIPELEFT:   s->transitionf = s->depth <= 8 ? wipeleft8_transition   : wipeleft16_transition;   break;
    case WIPERIGHT:  s->transitionf = s->depth <= 8 ? wiperight8_transition  : wiperight16_transition;  break;
    case WIPEUP:     s->transitionf = s->depth <= 8 ? wipeup8_transition     : wipeup16_transition;     break;
    case WIPEDOWN:   s->transitionf = s->depth <= 8 ? wipedown8_transition   : wipedown16_transition;   break;
    case SLIDELEFT:  s->transitionf = s->depth <= 8 ? slideleft8_transition  : slideleft16_transition;  break;
    case SLIDERIGHT: s->transitionf = s->depth <= 8 ? slideright8_transition : slideright16_transition; break;
    case SLIDEUP:    s->transitionf = s->depth <= 8 ? slideup8_transition    : slideup16_transition;    break;
    case SLIDEDOWN:  s->transitionf = s->depth <= 8 ? slidedown8_transition  : slidedown16_transition;  break;
    case CIRCLECROP: s->transitionf = s->depth <= 8 ? circlecrop8_transition : circlecrop16_transition; break;
    case RECTCROP:   s->transitionf = s->depth <= 8 ? rectcrop8_transition   : rectcrop16_transition;   break;
    case DISTANCE:   s->transitionf = s->depth <= 8 ? distance8_transition   : distance16_transition;   break;
    case FADEBLACK:  s->transitionf = s->depth <= 8 ? fadeblack8_transition  : fadeblack16_transition;  break;
    case FADEWHITE:  s->transitionf = s->depth <= 8 ? fadewhite8_transition  : fadewhite16_transition;  break;
    case RADIAL:     s->transitionf = s->depth <= 8 ? radial8_transition     : radial16_transition;     break;
    case SMOOTHLEFT: s->transitionf = s->depth <= 8 ? smoothleft8_transition : smoothleft16_transition; break;
    case SMOOTHRIGHT:s->transitionf = s->depth <= 8 ? smoothright8_transition: smoothright16_transition;break;
    case SMOOTHUP:   s->transitionf = s->depth <= 8 ? smoothup8_transition   : smoothup16_transition;   break;
    case SMOOTHDOWN: s->transitionf = s->depth <= 8 ? smoothdown8_transition : smoothdown16_transition; break;
    case CIRCLEOPEN: s->transitionf = s->depth <= 8 ? circleopen8_transition : circleopen16_transition; break;
    case CIRCLECLOSE:s->transitionf = s->depth <= 8 ? circleclose8_transition: circleclose16_transition;break;
    case VERTOPEN:   s->transitionf = s->depth <= 8 ? vertopen8_transition   : vertopen16_transition;   break;
    case VERTCLOSE:  s->transitionf = s->depth <= 8 ? vertclose8_transition  : vertclose16_transition;  break;
    case HORZOPEN:   s->transitionf = s->depth <= 8 ? horzopen8_transition   : horzopen16_transition;   break;
    case HORZCLOSE:  s->transitionf = s->depth <= 8 ? horzclose8_transition  : horzclose16_transition;  break;
    case DISSOLVE:   s->transitionf = s->depth <= 8 ? dissolve8_transition   : dissolve16_transition;   break;
    case PIXELIZE:   s->transitionf = s->depth <= 8 ? pixelize8_transition   : pixelize16_transition;   break;
    case DIAGTL:     s->transitionf = s->depth <= 8 ? diagtl8_transition     : diagtl16_transition;     break;
    case DIAGTR:     s->transitionf = s->depth <= 8 ? diagtr8_transition     : diagtr16_transition;     break;
    case DIAGBL:     s->transitionf = s->depth <= 8 ? diagbl8_transition     : diagbl16_transition;     break;
    case DIAGBR:     s->transitionf = s->depth <= 8 ? diagbr8_transition     : diagbr16_transition;     break;
    case HLSLICE:    s->transitionf = s->depth <= 8 ? hlslice8_transition    : hlslice16_transition;    break;
    case HRSLICE:    s->transitionf = s->depth <= 8 ? hrslice8_transition    : hrslice16_transition;    break;
    case VUSLICE:    s->transitionf = s->depth <= 8 ? vuslice8_transition    : vuslice16_transition;    break;
    case VDSLICE:    s->transitionf = s->depth <= 8 ? vdslice8_transition    : vdslice16_transition;    break;
    case HBLUR:      s->transitionf = s->depth <= 8 ? hblur8_transition      : hblur16_transition;      break;
    case FADEGRAYS:  s->transitionf = s->depth <= 8 ? fadegrays8_transition  : fadegrays16_transition;  break;
    case WIPETL:     s->transitionf = s->depth <= 8 ? wipetl8_transition     : wipetl16_transition;     break;
    case WIPETR:     s->transitionf = s->depth <= 8 ? wipetr8_transition     : wipetr16_transition;     break;
    case WIPEBL:     s->transitionf = s->depth <= 8 ? wipebl8_transition     : wipebl16_transition;     break;
    case WIPEBR:     s->transitionf = s->depth <= 8 ? wipebr8_transition     : wipebr16_transition;     break;
    case SQUEEZEH:   s->transitionf = s->depth <= 8 ? squeezeh8_transition   : squeezeh16_transition;   break;
    case SQUEEZEV:   s->transitionf = s->depth <= 8 ? squeezev8_transition   : squeezev16_transition;   break;
    case ZOOMIN:     s->transitionf = s->depth <= 8 ? zoomin8_transition     : zoomin16_transition;     break;
    case FADEFAST:   s->transitionf = s->depth <= 8 ? fadefast8_transition   : fadefast16_transition;   break;
    case FADESLOW:   s->transitionf = s->depth <= 8 ? fadeslow8_transition   : fadeslow16_transition;   break;
    case HLWIND:     s->transitionf = s->depth <= 8 ? hlwind8_transition     : hlwind16_transition;     break;
    case HRWIND:     s->transitionf = s->depth <= 8 ? hrwind8_transition     : hrwind16_transition;     break;
    case VUWIND:     s->transitionf = s->depth <= 8 ? vuwind8_transition     : vuwind16_transition;     break;
    case VDWIND:     s->transitionf = s->depth <= 8 ? vdwind8_transition     : vdwind16_transition;     break;
    case COVERLEFT:  s->transitionf = s->depth <= 8 ? coverleft8_transition  : coverleft16_transition;  break;
    case COVERRIGHT: s->transitionf = s->depth <= 8 ? coverright8_transition : coverright16_transition; break;
    case COVERUP:    s->transitionf = s->depth <= 8 ? coverup8_transition    : coverup16_transition;    break;
    case COVERDOWN:  s->transitionf = s->depth <= 8 ? coverdown8_transition  : coverdown16_transition;  break;
    case REVEALLEFT: s->transitionf = s->depth <= 8 ? revealleft8_transition : revealleft16_transition; break;
    case REVEALRIGHT:s->transitionf = s->depth <= 8 ? revealright8_transition: revealright16_transition;break;
    case REVEALUP:   s->transitionf = s->depth <= 8 ? revealup8_transition   : revealup16_transition;   break;
    case REVEALDOWN: s->transitionf = s->depth <= 8 ? revealdown8_transition : revealdown16_transition; break;
    default: return AVERROR_BUG;
    }

    if (s->transition == CUSTOM) {
        static const char *const func2_names[]    = {
            "a0", "a1", "a2", "a3",
            "b0", "b1", "b2", "b3",
            NULL
        };
        double (*func2[])(void *, double, double) = {
            a0, a1, a2, a3,
            b0, b1, b2, b3,
            NULL };
        int ret;

        if (!s->custom_str)
            return AVERROR(EINVAL);
        ret = av_expr_parse(&s->e, s->custom_str, var_names,
                            NULL, NULL, func2_names, func2, 0, ctx);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int xfade_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    XFadeContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    ThreadData *td = arg;
    int slice_start = (outlink->h *  jobnr   ) / nb_jobs;
    int slice_end   = (outlink->h * (jobnr+1)) / nb_jobs;

    s->transitionf(ctx, td->xf[0], td->xf[1], td->out, td->progress, slice_start, slice_end, jobnr);

    return 0;
}

static int xfade_frame(AVFilterContext *ctx, AVFrame *a, AVFrame *b)
{
    XFadeContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    float progress = av_clipf(1.f - ((float)(s->pts - s->start_pts) / s->duration_pts), 0.f, 1.f);
    ThreadData td;
    AVFrame *out;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out)
        return AVERROR(ENOMEM);
    av_frame_copy_props(out, a);

    td.xf[0] = a, td.xf[1] = b, td.out = out, td.progress = progress;
    ff_filter_execute(ctx, xfade_slice, &td, NULL,
                      FFMIN(outlink->h, ff_filter_get_nb_threads(ctx)));

    out->pts = s->pts;

    return ff_filter_frame(outlink, out);
}

static int forward_frame(XFadeContext *s,
                         AVFilterLink *inlink, AVFilterLink *outlink)
{
    int64_t status_pts;
    int ret = 0, status;
    AVFrame *frame = NULL;

    ret = ff_inlink_consume_frame(inlink, &frame);
    if (ret < 0)
        return ret;

    if (ret > 0) {
        // If we do not have an offset yet, it's because we
        // never got a first input. Just offset to 0
        if (s->inputs_offset_pts == AV_NOPTS_VALUE)
            s->inputs_offset_pts = -frame->pts;

        // We got a frame, nothing to do other than adjusting the timestamp
        frame->pts += s->inputs_offset_pts;
        return ff_filter_frame(outlink, frame);
    }

    // Forward status with our timestamp
    if (ff_inlink_acknowledge_status(inlink, &status, &status_pts)) {
        if (s->inputs_offset_pts == AV_NOPTS_VALUE)
            s->inputs_offset_pts = -status_pts;

        ff_outlink_set_status(outlink, status, status_pts + s->inputs_offset_pts);
        return 0;
    }

    // No frame available, request one if needed
    if (ff_outlink_frame_wanted(outlink))
        ff_inlink_request_frame(inlink);

    return 0;
}

static int xfade_activate(AVFilterContext *avctx)
{
    XFadeContext *s = avctx->priv;
    AVFilterLink *in_a = avctx->inputs[0];
    AVFilterLink *in_b = avctx->inputs[1];
    AVFilterLink *outlink = avctx->outputs[0];
    int64_t status_pts;

    FF_FILTER_FORWARD_STATUS_BACK_ALL(outlink, avctx);

    // Check if we already transitioned or first input ended prematurely,
    // in which case just forward the frames from second input with adjusted
    // timestamps until EOF.
    if (s->status[0] && !s->status[1])
        return forward_frame(s, in_b, outlink);

    // We did not finish transitioning yet and the first stream
    // did not end either, so check if there are more frames to consume.
    if (ff_inlink_check_available_frame(in_a)) {
        AVFrame *peeked_frame = ff_inlink_peek_frame(in_a, 0);
        s->pts = peeked_frame->pts;

        if (s->start_pts == AV_NOPTS_VALUE)
            s->start_pts =
                s->pts + av_rescale_q(s->offset, AV_TIME_BASE_Q, in_a->time_base);

        // Check if we are not yet transitioning, in which case
        // just request and forward the input frame.
        if (s->start_pts > s->pts) {
            int ret;
            s->passthrough = 1;
            ret = ff_inlink_consume_frame(in_a, &s->xf[0]);
            if (ret < 0)
                return ret;
            return ff_filter_frame(outlink, s->xf[0]);
        }
        s->passthrough = 0;

        // We are transitioning, so we need a frame from second input
        if (ff_inlink_check_available_frame(in_b)) {
            int ret;
            ret = ff_inlink_consume_frame(avctx->inputs[0], &s->xf[0]);
            if (ret < 0)
                return ret;
            ret = ff_inlink_consume_frame(avctx->inputs[1], &s->xf[1]);
            if (ret < 0) {
                av_frame_free(&s->xf[0]);
                return ret;
            }

            // Calculate PTS offset to first input
            if (s->inputs_offset_pts == AV_NOPTS_VALUE)
                s->inputs_offset_pts = s->pts - s->xf[1]->pts;

            // Check if we finished transitioning, in which case we
            // report back EOF to first input as it is no longer needed.
            if (s->pts - s->start_pts > s->duration_pts) {
                s->status[0] = AVERROR_EOF;
                ff_inlink_set_status(in_a, AVERROR_EOF);
                s->passthrough = 1;
            }
            ret = xfade_frame(avctx, s->xf[0], s->xf[1]);
            av_frame_free(&s->xf[0]);
            av_frame_free(&s->xf[1]);
            return ret;
        }

        // We did not get a frame from second input, check its status.
        if (ff_inlink_acknowledge_status(in_b, &s->status[1], &status_pts)) {
            // We should transition, but second input is EOF so just report EOF output now.
            ff_outlink_set_status(outlink, s->status[1], s->pts);
            return 0;
        }

        // We did not get a frame for second input but no EOF either, so just request more.
        if (ff_outlink_frame_wanted(outlink)) {
            ff_inlink_request_frame(in_b);
            return 0;
        }
    }

    // We did not get a frame from first input, check its status.
    if (ff_inlink_acknowledge_status(in_a, &s->status[0], &status_pts)) {
        // No more frames from first input, do not report EOF though, we will just
        // forward the second input frames in the next activate calls.
        s->passthrough = 1;
        ff_filter_set_ready(avctx, 100);
        return 0;
    }

    // We have no frames yet from first input and no EOF, so request some.
    if (ff_outlink_frame_wanted(outlink)) {
        ff_inlink_request_frame(in_a);
        return 0;
    }

    return FFERROR_NOT_READY;
}

static AVFrame *get_video_buffer(AVFilterLink *inlink, int w, int h)
{
    XFadeContext *s = inlink->dst->priv;

    return s->passthrough ?
        ff_null_get_video_buffer   (inlink, w, h) :
        ff_default_get_video_buffer(inlink, w, h);
}

static const AVFilterPad xfade_inputs[] = {
    {
        .name          = "main",
        .type          = AVMEDIA_TYPE_VIDEO,
        .get_buffer.video = get_video_buffer,
    },
    {
        .name          = "xfade",
        .type          = AVMEDIA_TYPE_VIDEO,
        .get_buffer.video = get_video_buffer,
    },
};

static const AVFilterPad xfade_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const AVFilter ff_vf_xfade = {
    .name          = "xfade",
    .description   = NULL_IF_CONFIG_SMALL("Cross fade one video with another video."),
    .priv_size     = sizeof(XFadeContext),
    .priv_class    = &xfade_class,
    .activate      = xfade_activate,
    .uninit        = uninit,
    FILTER_INPUTS(xfade_inputs),
    FILTER_OUTPUTS(xfade_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};
