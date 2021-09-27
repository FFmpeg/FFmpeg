/*
 * Copyright (c) 2012 Nicolas George
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
 * tile video filter
 */

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "video.h"
#include "internal.h"

typedef struct TileContext {
    const AVClass *class;
    unsigned w, h;
    unsigned margin;
    unsigned padding;
    unsigned overlap;
    unsigned init_padding;
    unsigned current;
    unsigned nb_frames;
    FFDrawContext draw;
    FFDrawColor blank;
    AVFrame *out_ref;
    AVFrame *prev_out_ref;
    uint8_t rgba_color[4];
} TileContext;

#define OFFSET(x) offsetof(TileContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption tile_options[] = {
    { "layout", "set grid size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE,
        {.str = "6x5"}, 0, 0, FLAGS },
    { "nb_frames", "set maximum number of frame to render", OFFSET(nb_frames),
        AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, FLAGS },
    { "margin",  "set outer border margin in pixels",    OFFSET(margin),
        AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1024, FLAGS },
    { "padding", "set inner border thickness in pixels", OFFSET(padding),
        AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1024, FLAGS },
    { "color",   "set the color of the unused area", OFFSET(rgba_color), AV_OPT_TYPE_COLOR, {.str = "black"}, .flags = FLAGS },
    { "overlap", "set how many frames to overlap for each render", OFFSET(overlap),
        AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, FLAGS },
    { "init_padding", "set how many frames to initially pad", OFFSET(init_padding),
        AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(tile);

static av_cold int init(AVFilterContext *ctx)
{
    TileContext *tile = ctx->priv;

    if (tile->w > UINT_MAX / tile->h) {
        av_log(ctx, AV_LOG_ERROR, "Tile size %ux%u is insane.\n",
               tile->w, tile->h);
        return AVERROR(EINVAL);
    }

    if (tile->padding) {
        if ((tile->w - 1 > (UINT32_MAX - 2 * tile->margin) / tile->padding) ||
            (tile->h - 1 > (UINT32_MAX - 2 * tile->margin) / tile->padding)) {
            av_log(ctx, AV_LOG_ERROR, "Combination of Tile size %ux%u, padding %d and margin %d overflows.\n",
                   tile->w, tile->h, tile->padding, tile->margin);
            return AVERROR(EINVAL);
        }
    }

    if (tile->nb_frames == 0) {
        tile->nb_frames = tile->w * tile->h;
    } else if (tile->nb_frames > tile->w * tile->h) {
        av_log(ctx, AV_LOG_ERROR, "nb_frames must be less than or equal to %dx%d=%d\n",
               tile->w, tile->h, tile->w * tile->h);
        return AVERROR(EINVAL);
    }

    if (tile->overlap >= tile->nb_frames) {
        av_log(ctx, AV_LOG_WARNING, "overlap must be less than %d\n", tile->nb_frames);
        tile->overlap = tile->nb_frames - 1;
    }

    if (tile->init_padding >= tile->nb_frames) {
        av_log(ctx, AV_LOG_WARNING, "init_padding must be less than %d\n", tile->nb_frames);
    } else {
        tile->current = tile->init_padding;
    }

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    return ff_set_common_formats(ctx, ff_draw_supported_pixel_formats(0));
}

static int config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    TileContext *tile    = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    const unsigned total_margin_w = (tile->w - 1) * tile->padding + 2*tile->margin;
    const unsigned total_margin_h = (tile->h - 1) * tile->padding + 2*tile->margin;

    if (inlink->w > (INT_MAX - total_margin_w) / tile->w) {
        av_log(ctx, AV_LOG_ERROR, "Total width %ux%u is too much.\n",
               tile->w, inlink->w);
        return AVERROR(EINVAL);
    }
    if (inlink->h > (INT_MAX - total_margin_h) / tile->h) {
        av_log(ctx, AV_LOG_ERROR, "Total height %ux%u is too much.\n",
               tile->h, inlink->h);
        return AVERROR(EINVAL);
    }
    outlink->w = tile->w * inlink->w + total_margin_w;
    outlink->h = tile->h * inlink->h + total_margin_h;
    outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;
    outlink->frame_rate = av_mul_q(inlink->frame_rate,
                                   av_make_q(1, tile->nb_frames - tile->overlap));
    ff_draw_init(&tile->draw, inlink->format, 0);
    ff_draw_color(&tile->draw, &tile->blank, tile->rgba_color);

    return 0;
}

static void get_tile_pos(AVFilterContext *ctx, unsigned *x, unsigned *y, unsigned current)
{
    TileContext *tile    = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    const unsigned tx = current % tile->w;
    const unsigned ty = current / tile->w;

    *x = tile->margin + (inlink->w + tile->padding) * tx;
    *y = tile->margin + (inlink->h + tile->padding) * ty;
}

static void draw_blank_frame(AVFilterContext *ctx, AVFrame *out_buf)
{
    TileContext *tile    = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    unsigned x0, y0;

    get_tile_pos(ctx, &x0, &y0, tile->current);
    ff_fill_rectangle(&tile->draw, &tile->blank,
                      out_buf->data, out_buf->linesize,
                      x0, y0, inlink->w, inlink->h);
    tile->current++;
}

static int end_last_frame(AVFilterContext *ctx)
{
    TileContext *tile     = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out_buf = tile->out_ref;
    int ret;

    while (tile->current < tile->nb_frames)
        draw_blank_frame(ctx, out_buf);
    tile->current = tile->overlap;
    if (tile->current) {
        av_frame_free(&tile->prev_out_ref);
        tile->prev_out_ref = av_frame_clone(out_buf);
    }
    ret = ff_filter_frame(outlink, out_buf);
    tile->out_ref = NULL;
    return ret;
}

/* Note: direct rendering is not possible since there is no guarantee that
 * buffers are fed to filter_frame in the order they were obtained from
 * get_buffer (think B-frames). */

static int filter_frame(AVFilterLink *inlink, AVFrame *picref)
{
    AVFilterContext *ctx  = inlink->dst;
    TileContext *tile     = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    unsigned x0, y0;

    if (!tile->out_ref) {
        tile->out_ref = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!tile->out_ref) {
            av_frame_free(&picref);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(tile->out_ref, picref);
        tile->out_ref->width  = outlink->w;
        tile->out_ref->height = outlink->h;

        /* fill surface once for margin/padding */
        if (tile->margin || tile->padding || tile->init_padding)
            ff_fill_rectangle(&tile->draw, &tile->blank,
                              tile->out_ref->data,
                              tile->out_ref->linesize,
                              0, 0, outlink->w, outlink->h);
        tile->init_padding = 0;
    }

    if (tile->prev_out_ref) {
        unsigned x1, y1, i;

        for (i = tile->nb_frames - tile->overlap; i < tile->nb_frames; i++) {
            get_tile_pos(ctx, &x1, &y1, i);
            get_tile_pos(ctx, &x0, &y0, i - (tile->nb_frames - tile->overlap));
            ff_copy_rectangle2(&tile->draw,
                               tile->out_ref->data, tile->out_ref->linesize,
                               tile->prev_out_ref->data, tile->prev_out_ref->linesize,
                               x0, y0, x1, y1, inlink->w, inlink->h);

        }
    }

    get_tile_pos(ctx, &x0, &y0, tile->current);
    ff_copy_rectangle2(&tile->draw,
                       tile->out_ref->data, tile->out_ref->linesize,
                       picref->data, picref->linesize,
                       x0, y0, 0, 0, inlink->w, inlink->h);

    av_frame_free(&picref);
    if (++tile->current == tile->nb_frames)
        return end_last_frame(ctx);

    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    TileContext *tile    = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    int r;

    r = ff_request_frame(inlink);
    if (r == AVERROR_EOF && tile->current && tile->out_ref)
        r = end_last_frame(ctx);
    return r;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    TileContext *tile = ctx->priv;

    av_frame_free(&tile->out_ref);
    av_frame_free(&tile->prev_out_ref);
}

static const AVFilterPad tile_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad tile_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_props,
        .request_frame = request_frame,
    },
};

const AVFilter ff_vf_tile = {
    .name          = "tile",
    .description   = NULL_IF_CONFIG_SMALL("Tile several successive frames together."),
    .init          = init,
    .uninit        = uninit,
    .priv_size     = sizeof(TileContext),
    FILTER_INPUTS(tile_inputs),
    FILTER_OUTPUTS(tile_outputs),
    FILTER_QUERY_FUNC(query_formats),
    .priv_class    = &tile_class,
};
