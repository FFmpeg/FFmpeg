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

#include "libavutil/pixfmt.h"
#include "avfilter.h"
#include "bufferqueue.h"
#include "drawutils.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

enum { Y, U, V, A };

typedef struct {
    int frame_requested;
    int is_packed_rgb;
    uint8_t rgba_map[4];
    struct FFBufQueue queue_main;
    struct FFBufQueue queue_alpha;
} AlphaMergeContext;

static av_cold void uninit(AVFilterContext *ctx)
{
    AlphaMergeContext *merge = ctx->priv;
    ff_bufqueue_discard_all(&merge->queue_main);
    ff_bufqueue_discard_all(&merge->queue_alpha);
}

static int query_formats(AVFilterContext *ctx)
{
    enum PixelFormat main_fmts[] = {
        PIX_FMT_YUVA444P, PIX_FMT_YUVA422P, PIX_FMT_YUVA420P,
        PIX_FMT_RGBA, PIX_FMT_BGRA, PIX_FMT_ARGB, PIX_FMT_ABGR,
        PIX_FMT_NONE
    };
    enum PixelFormat alpha_fmts[] = { PIX_FMT_GRAY8, PIX_FMT_NONE };
    AVFilterFormats *main_formats = ff_make_format_list(main_fmts);
    AVFilterFormats *alpha_formats = ff_make_format_list(alpha_fmts);
    ff_formats_ref(main_formats, &ctx->inputs[0]->out_formats);
    ff_formats_ref(alpha_formats, &ctx->inputs[1]->out_formats);
    ff_formats_ref(main_formats, &ctx->outputs[0]->in_formats);
    return 0;
}

static int config_input_main(AVFilterLink *inlink)
{
    AlphaMergeContext *merge = inlink->dst->priv;
    merge->is_packed_rgb =
        ff_fill_rgba_map(merge->rgba_map, inlink->format) >= 0;
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

static int start_frame(AVFilterLink *inlink, AVFilterBufferRef *picref) {return 0;}
static int draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir) {return 0;}

static void draw_frame(AVFilterContext *ctx,
                       AVFilterBufferRef *main_buf,
                       AVFilterBufferRef *alpha_buf)
{
    AlphaMergeContext *merge = ctx->priv;
    int h = main_buf->video->h;

    if (merge->is_packed_rgb) {
        int x, y;
        uint8_t *pin, *pout;
        for (y = 0; y < h; y++) {
            pin = alpha_buf->data[0] + y * alpha_buf->linesize[0];
            pout = main_buf->data[0] + y * main_buf->linesize[0] + merge->rgba_map[A];
            for (x = 0; x < main_buf->video->w; x++) {
                *pout = *pin;
                pin += 1;
                pout += 4;
            }
        }
    } else {
        int y;
        const int main_linesize = main_buf->linesize[A];
        const int alpha_linesize = alpha_buf->linesize[Y];
        for (y = 0; y < h && y < alpha_buf->video->h; y++) {
            memcpy(main_buf->data[A] + y * main_linesize,
                   alpha_buf->data[Y] + y * alpha_linesize,
                   FFMIN(main_linesize, alpha_linesize));
        }
    }
    ff_draw_slice(ctx->outputs[0], 0, h, 1);
}

static int end_frame(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AlphaMergeContext *merge = ctx->priv;

    int is_alpha = (inlink == ctx->inputs[1]);
    struct FFBufQueue *queue =
        (is_alpha ? &merge->queue_alpha : &merge->queue_main);
    ff_bufqueue_add(ctx, queue, inlink->cur_buf);
    inlink->cur_buf = NULL;

    while (1) {
        AVFilterBufferRef *main_buf, *alpha_buf;

        if (!ff_bufqueue_peek(&merge->queue_main, 0) ||
            !ff_bufqueue_peek(&merge->queue_alpha, 0)) break;

        main_buf = ff_bufqueue_get(&merge->queue_main);
        alpha_buf = ff_bufqueue_get(&merge->queue_alpha);

        ctx->outputs[0]->out_buf = main_buf;
        ff_start_frame(ctx->outputs[0], avfilter_ref_buffer(main_buf, ~0));
        merge->frame_requested = 0;
        draw_frame(ctx, main_buf, alpha_buf);
        ff_end_frame(ctx->outputs[0]);
        avfilter_unref_buffer(alpha_buf);
    }
    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AlphaMergeContext *merge = ctx->priv;
    int in, ret;

    merge->frame_requested = 1;
    while (merge->frame_requested) {
        in = ff_bufqueue_peek(&merge->queue_main, 0) ? 1 : 0;
        ret = ff_request_frame(ctx->inputs[in]);
        if (ret < 0)
            return ret;
    }
    return 0;
}

AVFilter avfilter_vf_alphamerge = {
    .name           = "alphamerge",
    .description    = NULL_IF_CONFIG_SMALL("Copy the luma value of the second "
                      "input into the alpha channel of the first input."),
    .uninit         = uninit,
    .priv_size      = sizeof(AlphaMergeContext),
    .query_formats  = query_formats,

    .inputs    = (const AVFilterPad[]) {
        { .name             = "main",
          .type             = AVMEDIA_TYPE_VIDEO,
          .config_props     = config_input_main,
          .get_video_buffer = ff_null_get_video_buffer,
          .start_frame      = start_frame,
          .draw_slice       = draw_slice,
          .end_frame        = end_frame,
          .min_perms        = AV_PERM_READ | AV_PERM_WRITE | AV_PERM_PRESERVE },
        { .name             = "alpha",
          .type             = AVMEDIA_TYPE_VIDEO,
          .start_frame      = start_frame,
          .draw_slice       = draw_slice,
          .end_frame        = end_frame,
          .min_perms        = AV_PERM_READ | AV_PERM_PRESERVE },
        { .name = NULL }
    },
    .outputs   = (const AVFilterPad[]) {
      { .name               = "default",
        .type               = AVMEDIA_TYPE_VIDEO,
        .config_props       = config_output,
        .request_frame      = request_frame },
      { .name = NULL }
    },
};
