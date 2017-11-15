/*
 * Copyright (c) 2017 Paul B Mahol
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

typedef struct LumakeyContext {
    const AVClass *class;

    int threshold;
    int tolerance;
    int softness;

    int white;
    int black;
    int max;

    int (*do_lumakey_slice)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} LumakeyContext;

static int do_lumakey_slice8(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    LumakeyContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int slice_start = (frame->height * jobnr) / nb_jobs;
    const int slice_end = (frame->height * (jobnr + 1)) / nb_jobs;
    uint8_t *alpha = frame->data[3] + slice_start * frame->linesize[3];
    const uint8_t *luma = frame->data[0] + slice_start * frame->linesize[0];
    const int so = s->softness;
    const int w = s->white;
    const int b = s->black;
    int x, y;

    for (y = slice_start; y < slice_end; y++) {
        for (x = 0; x < frame->width; x++) {
            if (luma[x] >= b && luma[x] <= w) {
                alpha[x] = 0;
            } else if (luma[x] > b - so && luma[x] < w + so) {
                if (luma[x] < b) {
                    alpha[x] = 255 - (luma[x] - b + so) * 255 / so;
                } else {
                    alpha[x] = (luma[x] - w) * 255 / so;
                }
            }
        }
        luma += frame->linesize[0];
        alpha += frame->linesize[3];
    }

    return 0;
}

static int do_lumakey_slice16(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    LumakeyContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int slice_start = (frame->height * jobnr) / nb_jobs;
    const int slice_end = (frame->height * (jobnr + 1)) / nb_jobs;
    uint16_t *alpha = (uint16_t *)(frame->data[3] + slice_start * frame->linesize[3]);
    const uint16_t *luma = (const uint16_t *)(frame->data[0] + slice_start * frame->linesize[0]);
    const int so = s->softness;
    const int w = s->white;
    const int b = s->black;
    const int m = s->max;
    int x, y;

    for (y = slice_start; y < slice_end; y++) {
        for (x = 0; x < frame->width; x++) {
            if (luma[x] >= b && luma[x] <= w) {
                alpha[x] = 0;
            } else if (luma[x] > b - so && luma[x] < w + so) {
                if (luma[x] < b) {
                    alpha[x] = m - (luma[x] - b + so) * m / so;
                } else {
                    alpha[x] = (luma[x] - w) * m / so;
                }
            }
        }
        luma += frame->linesize[0] / 2;
        alpha += frame->linesize[3] / 2;
    }

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    AVFilterContext *ctx = inlink->dst;
    LumakeyContext *s = ctx->priv;
    int depth;

    depth = desc->comp[0].depth;
    if (depth == 8) {
        s->white = av_clip_uint8(s->threshold + s->tolerance);
        s->black = av_clip_uint8(s->threshold - s->tolerance);
        s->do_lumakey_slice = do_lumakey_slice8;
    } else {
        s->max = (1 << depth) - 1;
        s->white = av_clip(s->threshold + s->tolerance, 0, s->max);
        s->black = av_clip(s->threshold - s->tolerance, 0, s->max);
        s->do_lumakey_slice = do_lumakey_slice16;
    }

    return 0;
}

static int filter_frame(AVFilterLink *link, AVFrame *frame)
{
    AVFilterContext *ctx = link->dst;
    LumakeyContext *s = ctx->priv;
    int ret;

    if (ret = av_frame_make_writable(frame))
        return ret;

    if (ret = ctx->internal->execute(ctx, s->do_lumakey_slice, frame, NULL, FFMIN(frame->height, ff_filter_get_nb_threads(ctx))))
        return ret;

    return ff_filter_frame(ctx->outputs[0], frame);
}

static av_cold int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pixel_fmts[] = {
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA420P9,
        AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA420P10,
        AV_PIX_FMT_YUVA444P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA420P16,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *formats;

    formats = ff_make_format_list(pixel_fmts);
    if (!formats)
        return AVERROR(ENOMEM);

    return ff_set_common_formats(ctx, formats);
}

static const AVFilterPad lumakey_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad lumakey_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

#define OFFSET(x) offsetof(LumakeyContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption lumakey_options[] = {
    { "threshold", "set the threshold value", OFFSET(threshold), AV_OPT_TYPE_INT, {.i64=0}, 0, UINT16_MAX, FLAGS },
    { "tolerance", "set the tolerance value", OFFSET(tolerance), AV_OPT_TYPE_INT, {.i64=1}, 0, UINT16_MAX, FLAGS },
    { "softness",  "set the softness value",  OFFSET(softness),  AV_OPT_TYPE_INT, {.i64=0}, 0, UINT16_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(lumakey);

AVFilter ff_vf_lumakey = {
    .name          = "lumakey",
    .description   = NULL_IF_CONFIG_SMALL("Turns a certain luma into transparency."),
    .priv_size     = sizeof(LumakeyContext),
    .priv_class    = &lumakey_class,
    .query_formats = query_formats,
    .inputs        = lumakey_inputs,
    .outputs       = lumakey_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};
