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
    FFDrawContext draw;
    FFDrawColor blank;
} TileContext;

#define REASONABLE_SIZE 1024

#define OFFSET(x) offsetof(TileContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption tile_options[] = {
    { "layout", "set grid size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE,
        {.str = "6x5"}, 0, 0, FLAGS },
    { "margin",  "set outer border margin in pixels",    OFFSET(margin),
        AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1024, FLAGS },
    { "padding", "set inner border thickness in pixels", OFFSET(padding),
        AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1024, FLAGS },
    {NULL},
};

AVFILTER_DEFINE_CLASS(tile);

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    TileContext *tile = ctx->priv;
    static const char *shorthand[] = { "layout", NULL };
    int ret;

    tile->class = &tile_class;
    av_opt_set_defaults(tile);

    if ((ret = av_opt_set_from_string(tile, args, shorthand, "=", ":")) < 0)
        return ret;

    if (tile->w > REASONABLE_SIZE || tile->h > REASONABLE_SIZE) {
        av_log(ctx, AV_LOG_ERROR, "Tile size %ux%u is insane.\n",
               tile->w, tile->h);
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
    TileContext *tile   = ctx->priv;
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
                                   (AVRational){ 1, tile->w * tile->h });
    ff_draw_init(&tile->draw, inlink->format, 0);
    /* TODO make the color an option, or find an unified way of choosing it */
    ff_draw_color(&tile->draw, &tile->blank, (uint8_t[]){ 0, 0, 0, -1 });

    return 0;
}

/* Note: direct rendering is not possible since there is no guarantee that
 * buffers are fed to start_frame in the order they were obtained from
 * get_buffer (think B-frames). */

static int start_frame(AVFilterLink *inlink, AVFilterBufferRef *picref)
{
    AVFilterContext *ctx  = inlink->dst;
    TileContext *tile    = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    if (tile->current)
        return 0;
    outlink->out_buf = ff_get_video_buffer(outlink, AV_PERM_WRITE,
                                                 outlink->w, outlink->h);
    avfilter_copy_buffer_ref_props(outlink->out_buf, picref);
    outlink->out_buf->video->w = outlink->w;
    outlink->out_buf->video->h = outlink->h;

    /* fill surface once for margin/padding */
    if (tile->margin || tile->padding)
        ff_fill_rectangle(&tile->draw, &tile->blank,
                          outlink->out_buf->data, outlink->out_buf->linesize,
                          0, 0, outlink->w, outlink->h);
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

static int draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir)
{
    AVFilterContext *ctx  = inlink->dst;
    TileContext *tile    = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    unsigned x0, y0;

    get_current_tile_pos(ctx, &x0, &y0);
    ff_copy_rectangle2(&tile->draw,
                       outlink->out_buf->data, outlink->out_buf->linesize,
                       inlink ->cur_buf->data, inlink ->cur_buf->linesize,
                       x0, y0 + y, 0, y, inlink->cur_buf->video->w, h);
    /* TODO if tile->w == 1 && slice_dir is always 1, we could draw_slice
     * immediately. */
    return 0;
}

static void draw_blank_frame(AVFilterContext *ctx, AVFilterBufferRef *out_buf)
{
    TileContext *tile    = ctx->priv;
    AVFilterLink *inlink  = ctx->inputs[0];
    unsigned x0, y0;

    get_current_tile_pos(ctx, &x0, &y0);
    ff_fill_rectangle(&tile->draw, &tile->blank,
                      out_buf->data, out_buf->linesize,
                      x0, y0, inlink->w, inlink->h);
    tile->current++;
}
static void end_last_frame(AVFilterContext *ctx)
{
    TileContext *tile    = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFilterBufferRef *out_buf = outlink->out_buf;

    outlink->out_buf = NULL;
    ff_start_frame(outlink, out_buf);
    while (tile->current < tile->w * tile->h)
        draw_blank_frame(ctx, out_buf);
    ff_draw_slice(outlink, 0, out_buf->video->h, 1);
    ff_end_frame(outlink);
    tile->current = 0;
}

static int end_frame(AVFilterLink *inlink)
{
    AVFilterContext *ctx  = inlink->dst;
    TileContext *tile    = ctx->priv;

    avfilter_unref_bufferp(&inlink->cur_buf);
    if (++tile->current == tile->w * tile->h)
        end_last_frame(ctx);
    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    TileContext *tile   = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    int r;

    while (1) {
        r = ff_request_frame(inlink);
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
    .priv_class = &tile_class,
};
