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
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "libavutil/pixdesc.h"
#include "libavutil/colorspace.h"
#include "libavutil/imgutils.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "drawutils.h"

typedef struct {
    const AVClass *class;
    int w, h;
    char *rate_str;
    char *color_str;
    uint8_t color_rgba[4];
    AVRational time_base;
    uint64_t pts;
    FFDrawContext draw;
    FFDrawColor color;
} ColorContext;

#define OFFSET(x) offsetof(ColorContext, x)

static const AVOption color_options[]= {
    { "size",  "set frame size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "320x240"}, 0, 0 },
    { "s",     "set frame size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "320x240"}, 0, 0 },
    { "rate",  "set frame rate", OFFSET(rate_str), AV_OPT_TYPE_STRING, {.str = "25"}, 0, 0 },
    { "r",     "set frame rate", OFFSET(rate_str), AV_OPT_TYPE_STRING, {.str = "25"}, 0, 0 },
    { "color", "set color", OFFSET(color_str), AV_OPT_TYPE_STRING, {.str = "black"}, CHAR_MIN, CHAR_MAX },
    { "c",     "set color", OFFSET(color_str), AV_OPT_TYPE_STRING, {.str = "black"}, CHAR_MIN, CHAR_MAX },
    { NULL },
};

static const AVClass color_class = {
    .class_name = "color",
    .item_name  = av_default_item_name,
    .option     = color_options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_FILTER,
};

static av_cold int color_init(AVFilterContext *ctx, const char *args, void *opaque)
{
    ColorContext *color = ctx->priv;
    char color_string[128] = "black";
    char frame_size  [128] = "320x240";
    char frame_rate  [128] = "25";
    AVRational frame_rate_q;
    char *colon = 0, *equal = 0;
    int ret = 0;

    color->class = &color_class;

    if (args) {
        colon = strchr(args, ':');
        equal = strchr(args, '=');
    }

    if (!args || (equal && (!colon || equal < colon))) {
        av_opt_set_defaults(color);
        if ((ret = av_set_options_string(color, args, "=", ":")) < 0) {
            av_log(ctx, AV_LOG_ERROR, "Error parsing options string: '%s'\n", args);
            goto end;
        }
        if (av_parse_video_rate(&frame_rate_q, color->rate_str) < 0) {
            av_log(ctx, AV_LOG_ERROR, "Invalid frame rate: %s\n", color->rate_str);
            ret = AVERROR(EINVAL);
            goto end;
        }
        if (av_parse_color(color->color_rgba, color->color_str, -1, ctx) < 0) {
            ret = AVERROR(EINVAL);
            goto end;
        }
    } else {
        av_log(ctx, AV_LOG_WARNING, "Flat options syntax is deprecated, use key=value pairs.\n");
        sscanf(args, "%127[^:]:%127[^:]:%127s", color_string, frame_size, frame_rate);
        if (av_parse_video_size(&color->w, &color->h, frame_size) < 0) {
            av_log(ctx, AV_LOG_ERROR, "Invalid frame size: %s\n", frame_size);
            return AVERROR(EINVAL);
        }
        if (av_parse_video_rate(&frame_rate_q, frame_rate) < 0) {
            av_log(ctx, AV_LOG_ERROR, "Invalid frame rate: %s\n", frame_rate);
            return AVERROR(EINVAL);
        }
        if (av_parse_color(color->color_rgba, color_string, -1, ctx) < 0)
            return AVERROR(EINVAL);
    }

    color->time_base.num = frame_rate_q.den;
    color->time_base.den = frame_rate_q.num;

end:
    av_opt_free(color);
    return ret;
}

static int query_formats(AVFilterContext *ctx)
{
    ff_set_common_formats(ctx, ff_draw_supported_pixel_formats(0));
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
    AVFilterBufferRef *picref = ff_get_video_buffer(link, AV_PERM_WRITE, color->w, color->h);
    picref->video->sample_aspect_ratio = (AVRational) {1, 1};
    picref->pts = color->pts++;
    picref->pos = -1;

    ff_start_frame(link, avfilter_ref_buffer(picref, ~0));
    ff_fill_rectangle(&color->draw, &color->color, picref->data, picref->linesize,
                      0, 0, color->w, color->h);
    ff_draw_slice(link, 0, color->h, 1);
    ff_end_frame(link);
    avfilter_unref_buffer(picref);

    return 0;
}

AVFilter avfilter_vsrc_color = {
    .name        = "color",
    .description = NULL_IF_CONFIG_SMALL("Provide an uniformly colored input."),

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
