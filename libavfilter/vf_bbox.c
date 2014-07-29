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

#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/timestamp.h"
#include "avfilter.h"
#include "bbox.h"
#include "internal.h"

typedef struct {
    const AVClass *class;
    int min_val;
} BBoxContext;

#define OFFSET(x) offsetof(BBoxContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption bbox_options[] = {
    { "min_val", "set minimum luminance value for bounding box", OFFSET(min_val), AV_OPT_TYPE_INT, { .i64 = 16 }, 0, 254, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(bbox);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_NONE,
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

#define SET_META(key, value) \
    av_dict_set_int(metadata, key, value, 0);

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    BBoxContext *bbox = ctx->priv;
    FFBoundingBox box;
    int has_bbox, w, h;

    has_bbox =
        ff_calculate_bounding_box(&box,
                                  frame->data[0], frame->linesize[0],
                                  inlink->w, inlink->h, bbox->min_val);
    w = box.x2 - box.x1 + 1;
    h = box.y2 - box.y1 + 1;

    av_log(ctx, AV_LOG_INFO,
           "n:%"PRId64" pts:%s pts_time:%s", inlink->frame_count,
           av_ts2str(frame->pts), av_ts2timestr(frame->pts, &inlink->time_base));

    if (has_bbox) {
        AVDictionary **metadata = avpriv_frame_get_metadatap(frame);

        SET_META("lavfi.bbox.x1", box.x1)
        SET_META("lavfi.bbox.x2", box.x2)
        SET_META("lavfi.bbox.y1", box.y1)
        SET_META("lavfi.bbox.y2", box.y2)
        SET_META("lavfi.bbox.w",  w)
        SET_META("lavfi.bbox.h",  h)

        av_log(ctx, AV_LOG_INFO,
               " x1:%d x2:%d y1:%d y2:%d w:%d h:%d"
               " crop=%d:%d:%d:%d drawbox=%d:%d:%d:%d",
               box.x1, box.x2, box.y1, box.y2, w, h,
               w, h, box.x1, box.y1,    /* crop params */
               box.x1, box.y1, w, h);   /* drawbox params */
    }
    av_log(ctx, AV_LOG_INFO, "\n");

    return ff_filter_frame(inlink->dst->outputs[0], frame);
}

static const AVFilterPad bbox_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad bbox_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_bbox = {
    .name          = "bbox",
    .description   = NULL_IF_CONFIG_SMALL("Compute bounding box for each frame."),
    .priv_size     = sizeof(BBoxContext),
    .priv_class    = &bbox_class,
    .query_formats = query_formats,
    .inputs        = bbox_inputs,
    .outputs       = bbox_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
