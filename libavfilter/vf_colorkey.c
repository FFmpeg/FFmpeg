/*
 * Copyright (c) 2015 Timo Rothenpieler <timo@rothenpieler.org>
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

#include "config_components.h"

#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct ColorkeyContext {
    const AVClass *class;

    /* color offsets rgba */
    uint8_t co[4];

    uint8_t colorkey_rgba[4];
    float similarity;
    float blend;
    double scale;
    int depth;
    int max;

    int (*do_slice)(AVFilterContext *ctx, void *arg,
                    int jobnr, int nb_jobs);
} ColorkeyContext;

static int do_colorkey_pixel(const uint8_t *colorkey_rgba, int r, int g, int b,
                             float similarity, float iblend, int max, double scale)
{
    double dr, dg, db, diff;

    dr = r * scale - colorkey_rgba[0];
    dg = g * scale - colorkey_rgba[1];
    db = b * scale - colorkey_rgba[2];

    diff = sqrt((dr * dr + dg * dg + db * db) / (255.0 * 255.0 * 3.0));

    if (iblend < 10000.0) {
        return av_clipd((diff - similarity) * iblend, 0.0, 1.0) * max;
    } else {
        return (diff > similarity) ? max : 0;
    }
}

#define COLORKEY_SLICE(name, type)                                 \
static int do_colorkey_slice##name(AVFilterContext *avctx,         \
                                   void *arg,                      \
                                   int jobnr, int nb_jobs)         \
{                                                                  \
    AVFrame *frame = arg;                                          \
    const int slice_start = (frame->height * jobnr) / nb_jobs;     \
    const int slice_end = (frame->height * (jobnr + 1)) / nb_jobs; \
    ColorkeyContext *ctx = avctx->priv;                            \
    const float similarity = ctx->similarity;                      \
    const float iblend = 1.f / ctx->blend;                         \
    const uint8_t *colorkey_rgba = ctx->colorkey_rgba;             \
    const uint8_t *co = ctx->co;                                   \
    const double scale = ctx->scale;                               \
    const int max = ctx->max;                                      \
                                                                   \
    for (int y = slice_start; y < slice_end; y++) {                \
        type *dst = (type *)(frame->data[0] + y * frame->linesize[0]);\
                                                                   \
        for (int x = 0; x < frame->width; x++) {                   \
            const int o = x * 4;                                   \
                                                                   \
            dst[o + co[3]] = do_colorkey_pixel(colorkey_rgba,      \
                             dst[o + co[0]],                       \
                             dst[o + co[1]],                       \
                             dst[o + co[2]],                       \
                             similarity, iblend, max, scale);      \
        }                                                          \
    }                                                              \
                                                                   \
    return 0;                                                      \
}

COLORKEY_SLICE(8, uint8_t)
COLORKEY_SLICE(16, uint16_t)

#define COLORHOLD_SLICE(name, type, htype)                             \
static int do_colorhold_slice##name(AVFilterContext *avctx, void *arg, \
                              int jobnr, int nb_jobs)                  \
{                                                                      \
    AVFrame *frame = arg;                                              \
    const int slice_start = (frame->height * jobnr) / nb_jobs;         \
    const int slice_end = (frame->height * (jobnr + 1)) / nb_jobs;     \
    ColorkeyContext *ctx = avctx->priv;                                \
    const int depth = ctx->depth;                                      \
    const int max = ctx->max;                                          \
    const int half = max / 2;                                          \
    const uint8_t *co = ctx->co;                                       \
    const uint8_t *colorkey_rgba = ctx->colorkey_rgba;                 \
    const float similarity = ctx->similarity;                          \
    const float iblend = 1.f / ctx->blend;                             \
    const double scale = ctx->scale;                                   \
                                                                       \
    for (int y = slice_start; y < slice_end; ++y) {                    \
        type *dst = (type *)(frame->data[0] + y * frame->linesize[0]); \
                                                                       \
        for (int x = 0; x < frame->width; ++x) {                       \
            int o, t, r, g, b;                                         \
                                                                       \
            o = x * 4;                                                 \
            r = dst[o + co[0]];                                        \
            g = dst[o + co[1]];                                        \
            b = dst[o + co[2]];                                        \
                                                                       \
            t = do_colorkey_pixel(colorkey_rgba, r, g, b,              \
                                  similarity, iblend, max, scale);     \
                                                                       \
            if (t > 0) {                                               \
                htype a = (r + g + b) / 3;                             \
                htype rt = max - t;                                    \
                                                                       \
                dst[o + co[0]] = (a * t + r * rt + half) >> depth;     \
                dst[o + co[1]] = (a * t + g * rt + half) >> depth;     \
                dst[o + co[2]] = (a * t + b * rt + half) >> depth;     \
            }                                                          \
        }                                                              \
    }                                                                  \
                                                                       \
    return 0;                                                          \
}

COLORHOLD_SLICE(8, uint8_t, int)
COLORHOLD_SLICE(16, uint16_t, int64_t)

static int filter_frame(AVFilterLink *link, AVFrame *frame)
{
    AVFilterContext *avctx = link->dst;
    ColorkeyContext *ctx = avctx->priv;
    int res;

    if (res = ff_filter_execute(avctx, ctx->do_slice, frame, NULL,
                                FFMIN(frame->height, ff_filter_get_nb_threads(avctx))))
        return res;

    return ff_filter_frame(avctx->outputs[0], frame);
}

static av_cold int config_output(AVFilterLink *outlink)
{
    AVFilterContext *avctx = outlink->src;
    ColorkeyContext *ctx = avctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(avctx->inputs[0]->format);

    ctx->depth = desc->comp[0].depth;
    ctx->max   = (1 << ctx->depth) - 1;
    ctx->scale = 255.0 / ctx->max;
    outlink->w = avctx->inputs[0]->w;
    outlink->h = avctx->inputs[0]->h;
    outlink->time_base = avctx->inputs[0]->time_base;
    ff_fill_rgba_map(ctx->co, outlink->format);

    if (!strcmp(avctx->filter->name, "colorkey")) {
        ctx->do_slice = ctx->max == 255 ? do_colorkey_slice8  : do_colorkey_slice16;
    } else {
        ctx->do_slice = ctx->max == 255 ? do_colorhold_slice8 : do_colorhold_slice16;
    }

    return 0;
}

static const enum AVPixelFormat pixel_fmts[] = {
    AV_PIX_FMT_ARGB,
    AV_PIX_FMT_RGBA,
    AV_PIX_FMT_ABGR,
    AV_PIX_FMT_BGRA,
    AV_PIX_FMT_RGBA64,
    AV_PIX_FMT_BGRA64,
    AV_PIX_FMT_NONE
};

static const AVFilterPad colorkey_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .flags        = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad colorkey_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

#define OFFSET(x) offsetof(ColorkeyContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

#if CONFIG_COLORKEY_FILTER

static const AVOption colorkey_options[] = {
    { "color", "set the colorkey key color", OFFSET(colorkey_rgba), AV_OPT_TYPE_COLOR, { .str = "black" }, 0, 0, FLAGS },
    { "similarity", "set the colorkey similarity value", OFFSET(similarity), AV_OPT_TYPE_FLOAT, { .dbl = 0.01 }, 0.00001, 1.0, FLAGS },
    { "blend", "set the colorkey key blend value", OFFSET(blend), AV_OPT_TYPE_FLOAT, { .dbl = 0.0 }, 0.0, 1.0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(colorkey);

const AVFilter ff_vf_colorkey = {
    .name          = "colorkey",
    .description   = NULL_IF_CONFIG_SMALL("Turns a certain color into transparency. Operates on RGB colors."),
    .priv_size     = sizeof(ColorkeyContext),
    .priv_class    = &colorkey_class,
    FILTER_INPUTS(colorkey_inputs),
    FILTER_OUTPUTS(colorkey_outputs),
    FILTER_PIXFMTS_ARRAY(pixel_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};

#endif /* CONFIG_COLORKEY_FILTER */
#if CONFIG_COLORHOLD_FILTER

static const AVOption colorhold_options[] = {
    { "color", "set the colorhold key color", OFFSET(colorkey_rgba), AV_OPT_TYPE_COLOR, { .str = "black" }, 0, 0, FLAGS },
    { "similarity", "set the colorhold similarity value", OFFSET(similarity), AV_OPT_TYPE_FLOAT, { .dbl = 0.01 }, 0.00001, 1.0, FLAGS },
    { "blend", "set the colorhold blend value", OFFSET(blend), AV_OPT_TYPE_FLOAT, { .dbl = 0.0 }, 0.0, 1.0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(colorhold);

const AVFilter ff_vf_colorhold = {
    .name          = "colorhold",
    .description   = NULL_IF_CONFIG_SMALL("Turns a certain color range into gray. Operates on RGB colors."),
    .priv_size     = sizeof(ColorkeyContext),
    .priv_class    = &colorhold_class,
    FILTER_INPUTS(colorkey_inputs),
    FILTER_OUTPUTS(colorkey_outputs),
    FILTER_PIXFMTS_ARRAY(pixel_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};

#endif /* CONFIG_COLORHOLD_FILTER */
