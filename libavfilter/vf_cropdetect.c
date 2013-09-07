/*
 * Copyright (c) 2002 A'rpi
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @file
 * border detection filter
 * Ported from MPlayer libmpcodecs/vf_cropdetect.c.
 */

#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct {
    const AVClass *class;
    int x1, y1, x2, y2;
    int limit;
    int round;
    int reset_count;
    int frame_nb;
    int max_pixsteps[4];
} CropDetectContext;

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_YUV411P, AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_NV12,    AV_PIX_FMT_NV21,
        AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int checkline(void *ctx, const unsigned char *src, int stride, int len, int bpp)
{
    int total = 0;
    int div = len;

    switch (bpp) {
    case 1:
        while (--len >= 0) {
            total += src[0];
            src += stride;
        }
        break;
    case 3:
    case 4:
        while (--len >= 0) {
            total += src[0] + src[1] + src[2];
            src += stride;
        }
        div *= 3;
        break;
    }
    total /= div;

    av_log(ctx, AV_LOG_DEBUG, "total:%d\n", total);
    return total;
}

static av_cold int init(AVFilterContext *ctx)
{
    CropDetectContext *s = ctx->priv;

    s->frame_nb = -2;

    av_log(ctx, AV_LOG_VERBOSE, "limit:%d round:%d reset_count:%d\n",
           s->limit, s->round, s->reset_count);

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    CropDetectContext *s = ctx->priv;

    av_image_fill_max_pixsteps(s->max_pixsteps, NULL,
                               av_pix_fmt_desc_get(inlink->format));

    s->x1 = inlink->w - 1;
    s->y1 = inlink->h - 1;
    s->x2 = 0;
    s->y2 = 0;

    return 0;
}

#define SET_META(key, value) \
    snprintf(buf, sizeof(buf), "%d", value);  \
    av_dict_set(metadata, key, buf, 0)

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    CropDetectContext *s = ctx->priv;
    int bpp = s->max_pixsteps[0];
    int w, h, x, y, shrink_by;
    AVDictionary **metadata;
    char buf[32];

    // ignore first 2 frames - they may be empty
    if (++s->frame_nb > 0) {
        metadata = avpriv_frame_get_metadatap(frame);

        // Reset the crop area every reset_count frames, if reset_count is > 0
        if (s->reset_count > 0 && s->frame_nb > s->reset_count) {
            s->x1 = frame->width  - 1;
            s->y1 = frame->height - 1;
            s->x2 = 0;
            s->y2 = 0;
            s->frame_nb = 1;
        }

        for (y = 0; y < s->y1; y++) {
            if (checkline(ctx, frame->data[0] + frame->linesize[0] * y, bpp, frame->width, bpp) > s->limit) {
                s->y1 = y;
                break;
            }
        }

        for (y = frame->height - 1; y > s->y2; y--) {
            if (checkline(ctx, frame->data[0] + frame->linesize[0] * y, bpp, frame->width, bpp) > s->limit) {
                s->y2 = y;
                break;
            }
        }

        for (y = 0; y < s->x1; y++) {
            if (checkline(ctx, frame->data[0] + bpp*y, frame->linesize[0], frame->height, bpp) > s->limit) {
                s->x1 = y;
                break;
            }
        }

        for (y = frame->width - 1; y > s->x2; y--) {
            if (checkline(ctx, frame->data[0] + bpp*y, frame->linesize[0], frame->height, bpp) > s->limit) {
                s->x2 = y;
                break;
            }
        }

        // round x and y (up), important for yuv colorspaces
        // make sure they stay rounded!
        x = (s->x1+1) & ~1;
        y = (s->y1+1) & ~1;

        w = s->x2 - x + 1;
        h = s->y2 - y + 1;

        // w and h must be divisible by 2 as well because of yuv
        // colorspace problems.
        if (s->round <= 1)
            s->round = 16;
        if (s->round % 2)
            s->round *= 2;

        shrink_by = w % s->round;
        w -= shrink_by;
        x += (shrink_by/2 + 1) & ~1;

        shrink_by = h % s->round;
        h -= shrink_by;
        y += (shrink_by/2 + 1) & ~1;

        SET_META("lavfi.cropdetect.x1", s->x1);
        SET_META("lavfi.cropdetect.x2", s->x2);
        SET_META("lavfi.cropdetect.y1", s->y1);
        SET_META("lavfi.cropdetect.y2", s->y2);
        SET_META("lavfi.cropdetect.w",  w);
        SET_META("lavfi.cropdetect.h",  h);
        SET_META("lavfi.cropdetect.x",  x);
        SET_META("lavfi.cropdetect.y",  y);

        av_log(ctx, AV_LOG_INFO,
               "x1:%d x2:%d y1:%d y2:%d w:%d h:%d x:%d y:%d pts:%"PRId64" t:%f crop=%d:%d:%d:%d\n",
               s->x1, s->x2, s->y1, s->y2, w, h, x, y, frame->pts,
               frame->pts == AV_NOPTS_VALUE ? -1 : frame->pts * av_q2d(inlink->time_base),
               w, h, x, y);
    }

    return ff_filter_frame(inlink->dst->outputs[0], frame);
}

#define OFFSET(x) offsetof(CropDetectContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption cropdetect_options[] = {
    { "limit", "Threshold below which the pixel is considered black", OFFSET(limit),       AV_OPT_TYPE_INT, { .i64 = 24 }, 0, 255, FLAGS },
    { "round", "Value by which the width/height should be divisible", OFFSET(round),       AV_OPT_TYPE_INT, { .i64 = 16 }, 0, INT_MAX, FLAGS },
    { "reset", "Recalculate the crop area after this many frames",    OFFSET(reset_count), AV_OPT_TYPE_INT, { .i64 = 0 },  0, INT_MAX, FLAGS },
    { "reset_count", "Recalculate the crop area after this many frames",OFFSET(reset_count),AV_OPT_TYPE_INT,{ .i64 = 0 },  0, INT_MAX, FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(cropdetect);

static const AVFilterPad avfilter_vf_cropdetect_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .config_props     = config_input,
        .filter_frame     = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_cropdetect_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO
    },
    { NULL }
};

AVFilter avfilter_vf_cropdetect = {
    .name        = "cropdetect",
    .description = NULL_IF_CONFIG_SMALL("Auto-detect crop size."),

    .priv_size = sizeof(CropDetectContext),
    .priv_class = &cropdetect_class,
    .init      = init,
    .query_formats = query_formats,
    .inputs    = avfilter_vf_cropdetect_inputs,
    .outputs   = avfilter_vf_cropdetect_outputs,
    .flags     = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
