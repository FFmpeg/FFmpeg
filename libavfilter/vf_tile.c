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

#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "video.h"
#include "internal.h"

typedef struct {
    const AVClass *class;
    unsigned w, h;
    unsigned margin;
    unsigned padding;
    unsigned current;
    unsigned nb_frames;
    FFDrawContext draw;
    FFDrawColor blank;
    AVFrame *out_ref;
} TileContext;

#define REASONABLE_SIZE 1024

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
    {NULL},
};

AVFILTER_DEFINE_CLASS(tile);

static av_cold int init(AVFilterContext *ctx)
{
    TileContext *tile = ctx->priv;

    if (tile->w > REASONABLE_SIZE || tile->h > REASONABLE_SIZE) {
        av_log(ctx, AV_LOG_ERROR, "Tile size %ux%u is insane.\n",
               tile->w, tile->h);
        return AVERROR(EINVAL);
    }

    if (tile->nb_frames == 0) {
        tile->nb_frames = tile->w * tile->h;
    } else if (tile->nb_frames > tile->w * tile->h) {
        av_log(ctx, AV_LOG_ERROR, "nb_frames must be less than or equal to %dx%d=%d\n",
               tile->w, tile->h, tile->w * tile->h);
        return AVERROR(EINVAL);
    }

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    ff_set_common_formats(ctx, ff_draw_supported_pixel_formats(0));
    return 0;
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
                                   (AVRational){ 1, tile->nb_frames });
    ff_draw_init(&tile->draw, inlink->format, 0);
    /* TODO make the color an option, or find an unified way of choosing it */
    ff_draw_color(&tile->draw, &tile->blank, (uint8_t[]){ 0, 0, 0, -1 });

    outlink->flags |= FF_LINK_FLAG_REQUEST_LOOP;

    return 0;
}

static void get_current_tile_pos(AVFilterContext *ctx, unsigned *x, unsigned *y)
{
    TileContext *tile    = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    const unsigned tx = tile->current % tile->w;
    const unsigned ty = tile->current / tile->w;

    *x = tile->margin + (inlink->w + tile->padding) * tx;
    *y = tile->margin + (inlink->h + tile->padding) * ty;
}

static void draw_blank_frame(AVFilterContext *ctx, AVFrame *out_buf)
{
    TileContext *tile    = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    unsigned x0, y0;

    get_current_tile_pos(ctx, &x0, &y0);
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
    ret = ff_filter_frame(outlink, out_buf);
    tile->current = 0;
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

    if (!tile->current) {
        tile->out_ref = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!tile->out_ref)
            return AVERROR(ENOMEM);
        av_frame_copy_props(tile->out_ref, picref);
        tile->out_ref->width  = outlink->w;
        tile->out_ref->height = outlink->h;

        /* fill surface once for margin/padding */
        if (tile->margin || tile->padding)
            ff_fill_rectangle(&tile->draw, &tile->blank,
                              tile->out_ref->data,
                              tile->out_ref->linesize,
                              0, 0, outlink->w, outlink->h);
    }

    get_current_tile_pos(ctx, &x0, &y0);
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
    if (r == AVERROR_EOF && tile->current)
        r = end_last_frame(ctx);
    return r;
}

static const AVFilterPad tile_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad tile_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_props,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter avfilter_vf_tile = {
    .name          = "tile",
    .description   = NULL_IF_CONFIG_SMALL("Tile several successive frames together."),
    .init          = init,
    .query_formats = query_formats,
    .priv_size     = sizeof(TileContext),
    .inputs        = tile_inputs,
    .outputs       = tile_outputs,
    .priv_class    = &tile_class,
};
