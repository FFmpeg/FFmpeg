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

typedef struct ChromakeyContext {
    const AVClass *class;

    uint8_t chromakey_rgba[4];
    uint8_t chromakey_uv[2];

    float similarity;
    float blend;

    int is_yuv;

    int hsub_log2;
    int vsub_log2;

    int (*do_slice)(AVFilterContext *ctx, void *arg,
                    int jobnr, int nb_jobs);
} ChromakeyContext;

static uint8_t do_chromakey_pixel(ChromakeyContext *ctx, uint8_t u[9], uint8_t v[9])
{
    double diff = 0.0;
    int du, dv, i;

    for (i = 0; i < 9; ++i) {
        du = (int)u[i] - ctx->chromakey_uv[0];
        dv = (int)v[i] - ctx->chromakey_uv[1];

        diff += sqrt((du * du + dv * dv) / (255.0 * 255.0));
    }

    diff /= 9.0;

    if (ctx->blend > 0.0001) {
        return av_clipd((diff - ctx->similarity) / ctx->blend, 0.0, 1.0) * 255.0;
    } else {
        return (diff > ctx->similarity) ? 255 : 0;
    }
}

static av_always_inline void get_pixel_uv(AVFrame *frame, int hsub_log2, int vsub_log2, int x, int y, uint8_t *u, uint8_t *v)
{
    if (x < 0 || x >= frame->width || y < 0 || y >= frame->height)
        return;

    x >>= hsub_log2;
    y >>= vsub_log2;

    *u = frame->data[1][frame->linesize[1] * y + x];
    *v = frame->data[2][frame->linesize[2] * y + x];
}

static int do_chromakey_slice(AVFilterContext *avctx, void *arg, int jobnr, int nb_jobs)
{
    AVFrame *frame = arg;

    const int slice_start = (frame->height * jobnr) / nb_jobs;
    const int slice_end = (frame->height * (jobnr + 1)) / nb_jobs;

    ChromakeyContext *ctx = avctx->priv;

    int x, y, xo, yo;
    uint8_t u[9], v[9];

    memset(u, ctx->chromakey_uv[0], sizeof(u));
    memset(v, ctx->chromakey_uv[1], sizeof(v));

    for (y = slice_start; y < slice_end; ++y) {
        for (x = 0; x < frame->width; ++x) {
            for (yo = 0; yo < 3; ++yo) {
                for (xo = 0; xo < 3; ++xo) {
                    get_pixel_uv(frame, ctx->hsub_log2, ctx->vsub_log2, x + xo - 1, y + yo - 1, &u[yo * 3 + xo], &v[yo * 3 + xo]);
                }
            }

            frame->data[3][frame->linesize[3] * y + x] = do_chromakey_pixel(ctx, u, v);
        }
    }

    return 0;
}

static int do_chromahold_slice(AVFilterContext *avctx, void *arg, int jobnr, int nb_jobs)
{
    ChromakeyContext *ctx = avctx->priv;
    AVFrame *frame = arg;
    const int slice_start = ((frame->height >> ctx->vsub_log2) * jobnr) / nb_jobs;
    const int slice_end = ((frame->height >> ctx->vsub_log2) * (jobnr + 1)) / nb_jobs;

    int x, y, alpha;

    for (y = slice_start; y < slice_end; ++y) {
        for (x = 0; x < frame->width >> ctx->hsub_log2; ++x) {
            int u = frame->data[1][frame->linesize[1] * y + x];
            int v = frame->data[2][frame->linesize[2] * y + x];
            double diff;
            int du, dv;

            du = u - ctx->chromakey_uv[0];
            dv = v - ctx->chromakey_uv[1];

            diff = sqrt((du * du + dv * dv) / (255.0 * 255.0));

            alpha = diff > ctx->similarity;
            if (alpha) {
                frame->data[1][frame->linesize[1] * y + x] = 128;
                frame->data[2][frame->linesize[2] * y + x] = 128;
            }
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *link, AVFrame *frame)
{
    AVFilterContext *avctx = link->dst;
    ChromakeyContext *ctx = avctx->priv;
    int res;

    if (res = avctx->internal->execute(avctx, ctx->do_slice, frame, NULL, FFMIN(frame->height, ff_filter_get_nb_threads(avctx))))
        return res;

    return ff_filter_frame(avctx->outputs[0], frame);
}

#define FIXNUM(x) lrint((x) * (1 << 10))
#define RGB_TO_U(rgb) (((- FIXNUM(0.16874) * rgb[0] - FIXNUM(0.33126) * rgb[1] + FIXNUM(0.50000) * rgb[2] + (1 << 9) - 1) >> 10) + 128)
#define RGB_TO_V(rgb) (((  FIXNUM(0.50000) * rgb[0] - FIXNUM(0.41869) * rgb[1] - FIXNUM(0.08131) * rgb[2] + (1 << 9) - 1) >> 10) + 128)

static av_cold int initialize_chromakey(AVFilterContext *avctx)
{
    ChromakeyContext *ctx = avctx->priv;

    if (ctx->is_yuv) {
        ctx->chromakey_uv[0] = ctx->chromakey_rgba[1];
        ctx->chromakey_uv[1] = ctx->chromakey_rgba[2];
    } else {
        ctx->chromakey_uv[0] = RGB_TO_U(ctx->chromakey_rgba);
        ctx->chromakey_uv[1] = RGB_TO_V(ctx->chromakey_rgba);
    }

    if (!strcmp(avctx->filter->name, "chromakey")) {
        ctx->do_slice = do_chromakey_slice;
    } else {
        ctx->do_slice = do_chromahold_slice;
    }

    return 0;
}

static av_cold int query_formats(AVFilterContext *avctx)
{
    static const enum AVPixelFormat pixel_fmts[] = {
        AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_YUVA422P,
        AV_PIX_FMT_YUVA444P,
        AV_PIX_FMT_NONE
    };

    static const enum AVPixelFormat hold_pixel_fmts[] = {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_YUVA422P,
        AV_PIX_FMT_YUVA444P,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *formats = NULL;

    formats = ff_make_format_list(!strcmp(avctx->filter->name, "chromahold") ? hold_pixel_fmts : pixel_fmts);
    if (!formats)
        return AVERROR(ENOMEM);

    return ff_set_common_formats(avctx, formats);
}

static av_cold int config_input(AVFilterLink *inlink)
{
    AVFilterContext *avctx = inlink->dst;
    ChromakeyContext *ctx = avctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    ctx->hsub_log2 = desc->log2_chroma_w;
    ctx->vsub_log2 = desc->log2_chroma_h;

    return 0;
}

static const AVFilterPad chromakey_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .needs_writable = 1,
        .filter_frame   = filter_frame,
        .config_props   = config_input,
    },
    { NULL }
};

static const AVFilterPad chromakey_outputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

#define OFFSET(x) offsetof(ChromakeyContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption chromakey_options[] = {
    { "color", "set the chromakey key color", OFFSET(chromakey_rgba), AV_OPT_TYPE_COLOR, { .str = "black" }, CHAR_MIN, CHAR_MAX, FLAGS },
    { "similarity", "set the chromakey similarity value", OFFSET(similarity), AV_OPT_TYPE_FLOAT, { .dbl = 0.01 }, 0.01, 1.0, FLAGS },
    { "blend", "set the chromakey key blend value", OFFSET(blend), AV_OPT_TYPE_FLOAT, { .dbl = 0.0 }, 0.0, 1.0, FLAGS },
    { "yuv", "color parameter is in yuv instead of rgb", OFFSET(is_yuv), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(chromakey);

AVFilter ff_vf_chromakey = {
    .name          = "chromakey",
    .description   = NULL_IF_CONFIG_SMALL("Turns a certain color into transparency. Operates on YUV colors."),
    .priv_size     = sizeof(ChromakeyContext),
    .priv_class    = &chromakey_class,
    .init          = initialize_chromakey,
    .query_formats = query_formats,
    .inputs        = chromakey_inputs,
    .outputs       = chromakey_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};

static const AVOption chromahold_options[] = {
    { "color", "set the chromahold key color", OFFSET(chromakey_rgba), AV_OPT_TYPE_COLOR, { .str = "black" }, CHAR_MIN, CHAR_MAX, FLAGS },
    { "similarity", "set the chromahold similarity value", OFFSET(similarity), AV_OPT_TYPE_FLOAT, { .dbl = 0.01 }, 0.01, 1.0, FLAGS },
    { "yuv", "color parameter is in yuv instead of rgb", OFFSET(is_yuv), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { NULL }
};

static const AVFilterPad chromahold_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .needs_writable = 1,
        .filter_frame   = filter_frame,
        .config_props   = config_input,
    },
    { NULL }
};

static const AVFilterPad chromahold_outputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFILTER_DEFINE_CLASS(chromahold);

AVFilter ff_vf_chromahold = {
    .name          = "chromahold",
    .description   = NULL_IF_CONFIG_SMALL("Turns a certain color range into gray."),
    .priv_size     = sizeof(ChromakeyContext),
    .priv_class    = &chromahold_class,
    .init          = initialize_chromakey,
    .query_formats = query_formats,
    .inputs        = chromahold_inputs,
    .outputs       = chromahold_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};
