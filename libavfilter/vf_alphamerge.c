/*
 * Copyright (c) 2012 Steven Robertson
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
 * copy an alpha component from another video's luma
 */

#include <string.h>

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixfmt.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "filters.h"
#include "internal.h"
#include "video.h"

enum { Y, U, V, A };

typedef struct AlphaMergeContext {
    const AVClass *class;

    int is_packed_rgb;
    uint8_t rgba_map[4];
    AVFrame *main_frame;
    AVFrame *alpha_frame;
} AlphaMergeContext;

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat main_fmts[] = {
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_GBRAP,
        AV_PIX_FMT_RGBA, AV_PIX_FMT_BGRA, AV_PIX_FMT_ARGB, AV_PIX_FMT_ABGR,
        AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat alpha_fmts[] = { AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE };
    AVFilterFormats *main_formats = NULL, *alpha_formats = NULL;
    int ret;

    if (!(main_formats = ff_make_format_list(main_fmts)) ||
        !(alpha_formats = ff_make_format_list(alpha_fmts))) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    if ((ret = ff_formats_ref(main_formats , &ctx->inputs[0]->out_formats)) < 0 ||
        (ret = ff_formats_ref(alpha_formats, &ctx->inputs[1]->out_formats)) < 0 ||
        (ret = ff_formats_ref(main_formats , &ctx->outputs[0]->in_formats)) < 0)
            goto fail;
    return 0;
fail:
    if (main_formats)
        av_freep(&main_formats->formats);
    av_freep(&main_formats);
    if (alpha_formats)
        av_freep(&alpha_formats->formats);
    av_freep(&alpha_formats);
    return ret;
}

static int config_input_main(AVFilterLink *inlink)
{
    AlphaMergeContext *s = inlink->dst->priv;
    s->is_packed_rgb =
        ff_fill_rgba_map(s->rgba_map, inlink->format) >= 0 &&
        inlink->format != AV_PIX_FMT_GBRAP;
    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *mainlink = ctx->inputs[0];
    AVFilterLink *alphalink = ctx->inputs[1];
    if (mainlink->w != alphalink->w || mainlink->h != alphalink->h) {
        av_log(ctx, AV_LOG_ERROR,
               "Input frame sizes do not match (%dx%d vs %dx%d).\n",
               mainlink->w, mainlink->h,
               alphalink->w, alphalink->h);
        return AVERROR(EINVAL);
    }

    outlink->w = mainlink->w;
    outlink->h = mainlink->h;
    outlink->time_base = mainlink->time_base;
    outlink->sample_aspect_ratio = mainlink->sample_aspect_ratio;
    outlink->frame_rate = mainlink->frame_rate;
    return 0;
}

static void draw_frame(AVFilterContext *ctx,
                       AVFrame *main_buf,
                       AVFrame *alpha_buf)
{
    AlphaMergeContext *s = ctx->priv;
    int h = main_buf->height;

    if (s->is_packed_rgb) {
        int x, y;
        uint8_t *pin, *pout;
        for (y = 0; y < h; y++) {
            pin = alpha_buf->data[0] + y * alpha_buf->linesize[0];
            pout = main_buf->data[0] + y * main_buf->linesize[0] + s->rgba_map[A];
            for (x = 0; x < main_buf->width; x++) {
                *pout = *pin;
                pin += 1;
                pout += 4;
            }
        }
    } else {
        const int main_linesize = main_buf->linesize[A];
        const int alpha_linesize = alpha_buf->linesize[Y];
        av_image_copy_plane(main_buf->data[A], main_linesize,
                            alpha_buf->data[Y], alpha_linesize,
                            FFMIN(main_linesize, alpha_linesize), alpha_buf->height);
    }
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *outlink = ctx->outputs[0];
    AlphaMergeContext *s = ctx->priv;
    int ret;

    FF_FILTER_FORWARD_STATUS_BACK_ALL(outlink, ctx);

    if (!s->main_frame) {
        ret = ff_inlink_consume_frame(ctx->inputs[0], &s->main_frame);
        if (ret < 0)
            return ret;
    }

    if (!s->alpha_frame) {
        ret = ff_inlink_consume_frame(ctx->inputs[1], &s->alpha_frame);
        if (ret < 0)
            return ret;
    }

    if (s->main_frame && s->alpha_frame) {
        if (!ctx->is_disabled)
            draw_frame(ctx, s->main_frame, s->alpha_frame);
        ret = ff_filter_frame(outlink, s->main_frame);
        av_frame_free(&s->alpha_frame);
        s->main_frame = NULL;
        return ret;
    }

    FF_FILTER_FORWARD_STATUS(ctx->inputs[0], outlink);
    FF_FILTER_FORWARD_STATUS(ctx->inputs[1], outlink);

    if (ff_outlink_frame_wanted(ctx->outputs[0]) &&
        !ff_outlink_get_status(ctx->inputs[0]) &&
        !s->main_frame) {
        ff_inlink_request_frame(ctx->inputs[0]);
        return 0;
    }

    if (ff_outlink_frame_wanted(ctx->outputs[0]) &&
        !ff_outlink_get_status(ctx->inputs[1]) &&
        !s->alpha_frame) {
        ff_inlink_request_frame(ctx->inputs[1]);
        return 0;
    }

    return FFERROR_NOT_READY;
}

static const AVFilterPad alphamerge_inputs[] = {
    {
        .name             = "main",
        .type             = AVMEDIA_TYPE_VIDEO,
        .config_props     = config_input_main,
        .needs_writable   = 1,
    },{
        .name             = "alpha",
        .type             = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

static const AVFilterPad alphamerge_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

static const AVOption alphamerge_options[] = {
    { NULL }
};

AVFILTER_DEFINE_CLASS(alphamerge);

AVFilter ff_vf_alphamerge = {
    .name           = "alphamerge",
    .description    = NULL_IF_CONFIG_SMALL("Copy the luma value of the second "
                      "input into the alpha channel of the first input."),
    .priv_size      = sizeof(AlphaMergeContext),
    .priv_class     = &alphamerge_class,
    .query_formats  = query_formats,
    .inputs         = alphamerge_inputs,
    .outputs        = alphamerge_outputs,
    .activate       = activate,
    .flags          = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
