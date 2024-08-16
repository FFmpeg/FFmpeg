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
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "filters.h"
#include "video.h"

typedef struct LumakeyContext {
    const AVClass *class;

    double threshold;
    double tolerance;
    double softness;

    int white;
    int black;
    int so;
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
    const int so = s->so;
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
    const int so = s->so;
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
        s->white = av_clip_uint8((s->threshold + s->tolerance) * 255);
        s->black = av_clip_uint8((s->threshold - s->tolerance) * 255);
        s->do_lumakey_slice = do_lumakey_slice8;
        s->so = s->softness * 255;
    } else {
        s->max = (1 << depth) - 1;
        s->white = av_clip((s->threshold + s->tolerance) * s->max, 0, s->max);
        s->black = av_clip((s->threshold - s->tolerance) * s->max, 0, s->max);
        s->do_lumakey_slice = do_lumakey_slice16;
        s->so = s->softness * s->max;
    }

    return 0;
}

static int filter_frame(AVFilterLink *link, AVFrame *frame)
{
    AVFilterContext *ctx = link->dst;
    LumakeyContext *s = ctx->priv;
    int ret;

    if (ret = ff_filter_execute(ctx, s->do_lumakey_slice, frame, NULL,
                                FFMIN(frame->height, ff_filter_get_nb_threads(ctx))))
        return ret;

    return ff_filter_frame(ctx->outputs[0], frame);
}

static const enum AVPixelFormat pixel_fmts[] = {
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA420P9,
    AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA420P10,
    AV_PIX_FMT_YUVA444P12, AV_PIX_FMT_YUVA422P12,
    AV_PIX_FMT_YUVA444P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA420P16,
    AV_PIX_FMT_NONE
};

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    return config_input(ctx->inputs[0]);
}

static const AVFilterPad lumakey_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .flags        = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

#define OFFSET(x) offsetof(LumakeyContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption lumakey_options[] = {
    { "threshold", "set the threshold value", OFFSET(threshold), AV_OPT_TYPE_DOUBLE, {.dbl=0},    0, 1, FLAGS },
    { "tolerance", "set the tolerance value", OFFSET(tolerance), AV_OPT_TYPE_DOUBLE, {.dbl=0.01}, 0, 1, FLAGS },
    { "softness",  "set the softness value",  OFFSET(softness),  AV_OPT_TYPE_DOUBLE, {.dbl=0},    0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(lumakey);

const AVFilter ff_vf_lumakey = {
    .name          = "lumakey",
    .description   = NULL_IF_CONFIG_SMALL("Turns a certain luma into transparency."),
    .priv_size     = sizeof(LumakeyContext),
    .priv_class    = &lumakey_class,
    FILTER_INPUTS(lumakey_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pixel_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = process_command,
};
