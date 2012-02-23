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

#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "drawutils.h"

typedef struct {
    unsigned w, h;
    unsigned current;
    FFDrawContext draw;
    FFDrawColor blank;
} TileContext;

#define REASONABLE_SIZE 1024

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    TileContext *tile = ctx->priv;
    int r;
    char dummy;

    if (!args)
        args = "6x5";
    r = sscanf(args, "%ux%u%c", &tile->w, &tile->h, &dummy);
    if (r != 2 || !tile->w || !tile->h)
        return AVERROR(EINVAL);
    if (tile->w > REASONABLE_SIZE || tile->h > REASONABLE_SIZE) {
        av_log(ctx, AV_LOG_ERROR, "Tile size %ux%u is insane.\n",
               tile->w, tile->h);
        return AVERROR(EINVAL);
    }
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    avfilter_set_common_pixel_formats(ctx, ff_draw_supported_pixel_formats(0));
    return 0;
}

static int config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    TileContext *tile   = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];

    if (inlink->w > INT_MAX / tile->w) {
        av_log(ctx, AV_LOG_ERROR, "Total width %ux%u is too much.\n",
               tile->w, inlink->w);
        return AVERROR(EINVAL);
    }
    if (inlink->h > INT_MAX / tile->h) {
        av_log(ctx, AV_LOG_ERROR, "Total height %ux%u is too much.\n",
               tile->h, inlink->h);
        return AVERROR(EINVAL);
    }
    outlink->w = tile->w * inlink->w;
    outlink->h = tile->h * inlink->h;
    outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;
    ff_draw_init(&tile->draw, inlink->format, 0);
    /* TODO make the color an option, or find an unified way of choosing it */
    ff_draw_color(&tile->draw, &tile->blank, (uint8_t[]){ 0, 0, 0, -1 });

    return 0;
}

/* Note: direct rendering is not possible since there is no guarantee that
 * buffers are fed to start_frame in the order they were obtained from
 * get_buffer (think B-frames). */

static void start_frame(AVFilterLink *inlink, AVFilterBufferRef *picref)
{
    AVFilterContext *ctx  = inlink->dst;
    TileContext *tile    = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    if (tile->current)
        return;
    outlink->out_buf = avfilter_get_video_buffer(outlink, AV_PERM_WRITE,
                                                 outlink->w, outlink->h);
    avfilter_copy_buffer_ref_props(outlink->out_buf, picref);
    outlink->out_buf->video->w = outlink->w;
    outlink->out_buf->video->h = outlink->h;
    avfilter_start_frame(outlink, outlink->out_buf);
}

static void draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir)
{
    AVFilterContext *ctx  = inlink->dst;
    TileContext *tile    = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    unsigned x0 = inlink->w * (tile->current % tile->w);
    unsigned y0 = inlink->h * (tile->current / tile->w);

    ff_copy_rectangle2(&tile->draw,
                       outlink->out_buf->data, outlink->out_buf->linesize,
                       inlink ->cur_buf->data, inlink ->cur_buf->linesize,
                       x0, y0 + y, 0, y, inlink->cur_buf->video->w, h);
    /* TODO if tile->w == 1 && slice_dir is always 1, we could draw_slice
     * immediately. */
}

static void draw_blank_frame(AVFilterContext *ctx)
{
    TileContext *tile    = ctx->priv;
    AVFilterLink *inlink  = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    unsigned x0 = inlink->w * (tile->current % tile->w);
    unsigned y0 = inlink->h * (tile->current / tile->w);

    ff_fill_rectangle(&tile->draw, &tile->blank,
                      outlink->out_buf->data, outlink->out_buf->linesize,
                      x0, y0, inlink->w, inlink->h);
    tile->current++;
}
static void end_last_frame(AVFilterContext *ctx)
{
    TileContext *tile    = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    while (tile->current < tile->w * tile->h)
        draw_blank_frame(ctx);
    avfilter_draw_slice(outlink, 0, outlink->out_buf->video->h, 1);
    avfilter_end_frame(outlink);
    tile->current = 0;
}

static void end_frame(AVFilterLink *inlink)
{
    AVFilterContext *ctx  = inlink->dst;
    TileContext *tile    = ctx->priv;

    avfilter_unref_buffer(inlink->cur_buf);
    if (++tile->current == tile->w * tile->h)
        end_last_frame(ctx);
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    TileContext *tile   = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    int r;

    while (1) {
        r = avfilter_request_frame(inlink);
        if (r < 0) {
            if (r == AVERROR_EOF && tile->current)
                end_last_frame(ctx);
            else
                return r;
            break;
        }
        if (!tile->current) /* done */
            break;
    }
    return 0;
}


AVFilter avfilter_vf_tile = {
    .name          = "tile",
    .description   = NULL_IF_CONFIG_SMALL("Tile several successive frames together."),
    .init          = init,
    .query_formats = query_formats,
    .priv_size     = sizeof(TileContext),
    .inputs = (const AVFilterPad[]) {
        { .name        = "default",
          .type        = AVMEDIA_TYPE_VIDEO,
          .start_frame = start_frame,
          .draw_slice  = draw_slice,
          .end_frame   = end_frame,
          .min_perms   = AV_PERM_READ, },
        { .name = NULL }
    },
    .outputs = (const AVFilterPad[]) {
        { .name          = "default",
          .type          = AVMEDIA_TYPE_VIDEO,
          .config_props  = config_props,
          .request_frame = request_frame },
        { .name = NULL }
    },
};
