/*
 * Copyright (c) 2012 Stefano Sabatini
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
 * bounding box detection filter
 */

#include "libavutil/pixdesc.h"
#include "libavutil/timestamp.h"
#include "avfilter.h"
#include "bbox.h"
#include "internal.h"

typedef struct {
    unsigned int frame;
    int vsub, hsub;
} BBoxContext;

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    BBoxContext *bbox = ctx->priv;
    bbox->frame = 0;
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_YUV420P,
        PIX_FMT_YUV444P,
        PIX_FMT_YUV440P,
        PIX_FMT_YUV422P,
        PIX_FMT_YUV411P,
        PIX_FMT_NONE,
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int end_frame(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    BBoxContext *bbox = ctx->priv;
    AVFilterBufferRef *picref = inlink->cur_buf;
    FFBoundingBox box;
    int has_bbox, w, h;

    has_bbox =
        ff_calculate_bounding_box(&box,
                                  picref->data[0], picref->linesize[0],
                                  inlink->w, inlink->h, 16);
    w = box.x2 - box.x1 + 1;
    h = box.y2 - box.y1 + 1;

    av_log(ctx, AV_LOG_INFO,
           "n:%d pts:%s pts_time:%s", bbox->frame,
           av_ts2str(picref->pts), av_ts2timestr(picref->pts, &inlink->time_base));

    if (has_bbox) {
        av_log(ctx, AV_LOG_INFO,
               " x1:%d x2:%d y1:%d y2:%d w:%d h:%d"
               " crop=%d:%d:%d:%d drawbox=%d:%d:%d:%d",
               box.x1, box.x2, box.y1, box.y2, w, h,
               w, h, box.x1, box.y1,    /* crop params */
               box.x1, box.y1, w, h);   /* drawbox params */
    }
    av_log(ctx, AV_LOG_INFO, "\n");

    bbox->frame++;
    return ff_end_frame(inlink->dst->outputs[0]);
}

AVFilter avfilter_vf_bbox = {
    .name          = "bbox",
    .description   = NULL_IF_CONFIG_SMALL("Compute bounding box for each frame."),
    .priv_size     = sizeof(BBoxContext),
    .query_formats = query_formats,
    .init          = init,

    .inputs = (const AVFilterPad[]) {
        { .name             = "default",
          .type             = AVMEDIA_TYPE_VIDEO,
          .get_video_buffer = ff_null_get_video_buffer,
          .start_frame      = ff_null_start_frame,
          .end_frame        = end_frame,
          .min_perms        = AV_PERM_READ, },
        { .name = NULL }
    },

    .outputs = (const AVFilterPad[]) {
        { .name             = "default",
          .type             = AVMEDIA_TYPE_VIDEO },
        { .name = NULL }
    },
};
