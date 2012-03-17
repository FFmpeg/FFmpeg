/*
 * Copyright (c) 2010 Stefano Sabatini
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
 * color source
 */

#include "avfilter.h"
#include "libavutil/pixdesc.h"
#include "libavutil/colorspace.h"
#include "libavutil/imgutils.h"
#include "libavutil/mathematics.h"
#include "libavutil/parseutils.h"
#include "drawutils.h"

typedef struct {
    int w, h;
    uint8_t color_rgba[4];
    AVRational time_base;
    uint64_t pts;
    FFDrawContext draw;
    FFDrawColor color;
} ColorContext;

static av_cold int color_init(AVFilterContext *ctx, const char *args, void *opaque)
{
    ColorContext *color = ctx->priv;
    char color_string[128] = "black";
    char frame_size  [128] = "320x240";
    char frame_rate  [128] = "25";
    AVRational frame_rate_q;
    int ret;

    if (args)
        sscanf(args, "%127[^:]:%127[^:]:%127s", color_string, frame_size, frame_rate);

    if (av_parse_video_size(&color->w, &color->h, frame_size) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid frame size: %s\n", frame_size);
        return AVERROR(EINVAL);
    }

    if (av_parse_video_rate(&frame_rate_q, frame_rate) < 0 ||
        frame_rate_q.den <= 0 || frame_rate_q.num <= 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid frame rate: %s\n", frame_rate);
        return AVERROR(EINVAL);
    }
    color->time_base.num = frame_rate_q.den;
    color->time_base.den = frame_rate_q.num;

    if ((ret = av_parse_color(color->color_rgba, color_string, -1, ctx)) < 0)
        return ret;

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    avfilter_set_common_pixel_formats(ctx, ff_draw_supported_pixel_formats(0));
    return 0;
}

static int color_config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->src;
    ColorContext *color = ctx->priv;

    ff_draw_init(&color->draw, inlink->format, 0);
    ff_draw_color(&color->draw, &color->color, color->color_rgba);

    color->w = ff_draw_round_to_sub(&color->draw, 0, -1, color->w);
    color->h = ff_draw_round_to_sub(&color->draw, 1, -1, color->h);
    if (av_image_check_size(color->w, color->h, 0, ctx) < 0)
        return AVERROR(EINVAL);

    av_log(ctx, AV_LOG_INFO, "w:%d h:%d r:%d/%d color:0x%02x%02x%02x%02x\n",
           color->w, color->h, color->time_base.den, color->time_base.num,
           color->color_rgba[0], color->color_rgba[1], color->color_rgba[2], color->color_rgba[3]);
    inlink->w = color->w;
    inlink->h = color->h;
    inlink->time_base = color->time_base;

    return 0;
}

static int color_request_frame(AVFilterLink *link)
{
    ColorContext *color = link->src->priv;
    AVFilterBufferRef *picref = avfilter_get_video_buffer(link, AV_PERM_WRITE, color->w, color->h);
    picref->video->sample_aspect_ratio = (AVRational) {1, 1};
    picref->pts = color->pts++;
    picref->pos = -1;

    avfilter_start_frame(link, avfilter_ref_buffer(picref, ~0));
    ff_fill_rectangle(&color->draw, &color->color, picref->data, picref->linesize,
                      0, 0, color->w, color->h);
    avfilter_draw_slice(link, 0, color->h, 1);
    avfilter_end_frame(link);
    avfilter_unref_buffer(picref);

    return 0;
}

AVFilter avfilter_vsrc_color = {
    .name        = "color",
    .description = NULL_IF_CONFIG_SMALL("Provide an uniformly colored input, syntax is: [color[:size[:rate]]]."),

    .priv_size = sizeof(ColorContext),
    .init      = color_init,

    .query_formats = query_formats,

    .inputs    = (const AVFilterPad[]) {{ .name = NULL}},

    .outputs   = (const AVFilterPad[]) {{ .name      = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO,
                                    .request_frame   = color_request_frame,
                                    .config_props    = color_config_props },
                                  { .name = NULL}},
};
