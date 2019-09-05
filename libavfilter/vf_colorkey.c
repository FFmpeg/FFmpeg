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

#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct ColorkeyContext {
    const AVClass *class;

    /* color offsets rgba */
    int co[4];

    uint8_t colorkey_rgba[4];
    float similarity;
    float blend;

    int (*do_slice)(AVFilterContext *ctx, void *arg,
                    int jobnr, int nb_jobs);
} ColorkeyContext;

static uint8_t do_colorkey_pixel(ColorkeyContext *ctx, uint8_t r, uint8_t g, uint8_t b)
{
    int dr = (int)r - ctx->colorkey_rgba[0];
    int dg = (int)g - ctx->colorkey_rgba[1];
    int db = (int)b - ctx->colorkey_rgba[2];

    double diff = sqrt((dr * dr + dg * dg + db * db) / (255.0 * 255.0));

    if (ctx->blend > 0.0001) {
        return av_clipd((diff - ctx->similarity) / ctx->blend, 0.0, 1.0) * 255.0;
    } else {
        return (diff > ctx->similarity) ? 255 : 0;
    }
}

static int do_colorkey_slice(AVFilterContext *avctx, void *arg, int jobnr, int nb_jobs)
{
    AVFrame *frame = arg;

    const int slice_start = (frame->height * jobnr) / nb_jobs;
    const int slice_end = (frame->height * (jobnr + 1)) / nb_jobs;

    ColorkeyContext *ctx = avctx->priv;

    int o, x, y;

    for (y = slice_start; y < slice_end; ++y) {
        for (x = 0; x < frame->width; ++x) {
            o = frame->linesize[0] * y + x * 4;

            frame->data[0][o + ctx->co[3]] =
                do_colorkey_pixel(ctx,
                                  frame->data[0][o + ctx->co[0]],
                                  frame->data[0][o + ctx->co[1]],
                                  frame->data[0][o + ctx->co[2]]);
        }
    }

    return 0;
}

static int do_colorhold_slice(AVFilterContext *avctx, void *arg, int jobnr, int nb_jobs)
{
    AVFrame *frame = arg;

    const int slice_start = (frame->height * jobnr) / nb_jobs;
    const int slice_end = (frame->height * (jobnr + 1)) / nb_jobs;

    ColorkeyContext *ctx = avctx->priv;

    int x, y;

    for (y = slice_start; y < slice_end; ++y) {
        for (x = 0; x < frame->width; ++x) {
            int o, t, r, g, b;

            o = frame->linesize[0] * y + x * 4;
            r = frame->data[0][o + ctx->co[0]];
            g = frame->data[0][o + ctx->co[1]];
            b = frame->data[0][o + ctx->co[2]];

            t = do_colorkey_pixel(ctx, r, g, b);

            if (t > 0) {
                int a = (r + g + b) / 3;
                int rt = 255 - t;

                frame->data[0][o + ctx->co[0]] = (a * t + r * rt + 127) >> 8;
                frame->data[0][o + ctx->co[1]] = (a * t + g * rt + 127) >> 8;
                frame->data[0][o + ctx->co[2]] = (a * t + b * rt + 127) >> 8;
            }
        }
    }

    return 0;
}

static av_cold int init_filter(AVFilterContext *avctx)
{
    ColorkeyContext *ctx = avctx->priv;

    if (!strcmp(avctx->filter->name, "colorkey")) {
        ctx->do_slice = do_colorkey_slice;
    } else {
        ctx->do_slice = do_colorhold_slice;
    }

    return 0;
}

static int filter_frame(AVFilterLink *link, AVFrame *frame)
{
    AVFilterContext *avctx = link->dst;
    ColorkeyContext *ctx = avctx->priv;
    int res;

    if (res = av_frame_make_writable(frame))
        return res;

    if (res = avctx->internal->execute(avctx, ctx->do_slice, frame, NULL, FFMIN(frame->height, ff_filter_get_nb_threads(avctx))))
        return res;

    return ff_filter_frame(avctx->outputs[0], frame);
}

static av_cold int config_output(AVFilterLink *outlink)
{
    AVFilterContext *avctx = outlink->src;
    ColorkeyContext *ctx = avctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(outlink->format);
    int i;

    outlink->w = avctx->inputs[0]->w;
    outlink->h = avctx->inputs[0]->h;
    outlink->time_base = avctx->inputs[0]->time_base;

    for (i = 0; i < 4; ++i)
        ctx->co[i] = desc->comp[i].offset;

    return 0;
}

static av_cold int query_formats(AVFilterContext *avctx)
{
    static const enum AVPixelFormat pixel_fmts[] = {
        AV_PIX_FMT_ARGB,
        AV_PIX_FMT_RGBA,
        AV_PIX_FMT_ABGR,
        AV_PIX_FMT_BGRA,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *formats = NULL;

    formats = ff_make_format_list(pixel_fmts);
    if (!formats)
        return AVERROR(ENOMEM);

    return ff_set_common_formats(avctx, formats);
}

static const AVFilterPad colorkey_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad colorkey_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

#define OFFSET(x) offsetof(ColorkeyContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

#if CONFIG_COLORKEY_FILTER

static const AVOption colorkey_options[] = {
    { "color", "set the colorkey key color", OFFSET(colorkey_rgba), AV_OPT_TYPE_COLOR, { .str = "black" }, CHAR_MIN, CHAR_MAX, FLAGS },
    { "similarity", "set the colorkey similarity value", OFFSET(similarity), AV_OPT_TYPE_FLOAT, { .dbl = 0.01 }, 0.01, 1.0, FLAGS },
    { "blend", "set the colorkey key blend value", OFFSET(blend), AV_OPT_TYPE_FLOAT, { .dbl = 0.0 }, 0.0, 1.0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(colorkey);

AVFilter ff_vf_colorkey = {
    .name          = "colorkey",
    .description   = NULL_IF_CONFIG_SMALL("Turns a certain color into transparency. Operates on RGB colors."),
    .priv_size     = sizeof(ColorkeyContext),
    .priv_class    = &colorkey_class,
    .query_formats = query_formats,
    .init          = init_filter,
    .inputs        = colorkey_inputs,
    .outputs       = colorkey_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};

#endif /* CONFIG_COLORKEY_FILTER */
#if CONFIG_COLORHOLD_FILTER

static const AVOption colorhold_options[] = {
    { "color", "set the colorhold key color", OFFSET(colorkey_rgba), AV_OPT_TYPE_COLOR, { .str = "black" }, CHAR_MIN, CHAR_MAX, FLAGS },
    { "similarity", "set the colorhold similarity value", OFFSET(similarity), AV_OPT_TYPE_FLOAT, { .dbl = 0.01 }, 0.01, 1.0, FLAGS },
    { "blend", "set the colorhold blend value", OFFSET(blend), AV_OPT_TYPE_FLOAT, { .dbl = 0.0 }, 0.0, 1.0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(colorhold);

AVFilter ff_vf_colorhold = {
    .name          = "colorhold",
    .description   = NULL_IF_CONFIG_SMALL("Turns a certain color range into gray. Operates on RGB colors."),
    .priv_size     = sizeof(ColorkeyContext),
    .priv_class    = &colorhold_class,
    .query_formats = query_formats,
    .init          = init_filter,
    .inputs        = colorkey_inputs,
    .outputs       = colorkey_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};

#endif /* CONFIG_COLORHOLD_FILTER */
