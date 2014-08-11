/*
 * Copyright (C) 2007 Richard Spindler (author of frei0r plugin from which this was derived)
 * Copyright (C) 2014 Daniel Oberhoff
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
 * Lenscorrection filter, algorithm from the frei0r plugin with the same name
*/
#include <stdlib.h>
#include <math.h>

#include "libavutil/opt.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "internal.h"
#include "video.h"

typedef struct LenscorrectionCtx {
    const AVClass *av_class;
    unsigned int width;
    unsigned int height;
    int hsub, vsub;
    int nb_planes;
    double cx, cy, k1, k2;
} LenscorrectionCtx;

#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption lenscorrection_options[] = {
    { "cx",     "set relative center x", offsetof(LenscorrectionCtx, cx), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 1, .flags=FLAGS },
    { "cy",     "set relative center y", offsetof(LenscorrectionCtx, cy), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 1, .flags=FLAGS },
    { "k1",     "set quadratic distortion factor", offsetof(LenscorrectionCtx, k1), AV_OPT_TYPE_DOUBLE, {.dbl=0.0}, -1, 1, .flags=FLAGS },
    { "k2",     "set double quadratic distortion factor", offsetof(LenscorrectionCtx, k2), AV_OPT_TYPE_DOUBLE, {.dbl=0.0}, -1, 1, .flags=FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(lenscorrection);

typedef struct ThreadData {
    AVFrame *in, *out;
    float w, h;
    int plane;
    float xcenter, ycenter;
    float k1, k2;
} ThreadData;

static int filter_slice(AVFilterContext *ctx, void *arg, int job, int nb_jobs)
{
    ThreadData *td = (ThreadData*)arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;

    const float w = td->w, h = td->h;
    const float xcenter = td->xcenter;
    const float ycenter = td->ycenter;
    const float r2inv = 4.0 / (w * w + h * h);
    const float k1 = td->k1;
    const float k2 = td->k2;
    const int start = (h *  job   ) / nb_jobs;
    const int end   = (h * (job+1)) / nb_jobs;
    const int plane = td->plane;
    const int inlinesize = in->linesize[plane];
    const int outlinesize = out->linesize[plane];
    const uint8_t *indata = in->data[plane];
    uint8_t *outrow = out->data[plane] + start * outlinesize;
    int i;
    for (i = start; i < end; i++, outrow += outlinesize) {
        const float off_y = i - ycenter;
        const float off_y2 = off_y * off_y;
        uint8_t *out = outrow;
        int j;
        for (j = 0; j < w; j++) {
            const float off_x = j - xcenter;
            const float r2 = (off_x * off_x + off_y2) * r2inv;
            const float radius_mult = 1.0f + r2 * k1 + r2 * r2 * k2;
            const int x = xcenter + radius_mult * off_x + 0.5f;
            const int y = ycenter + radius_mult * off_y + 0.5f;
            const char isvalid = x > 0 && x < w - 1 && y > 0 && y < h - 1;
            *out++ =  isvalid ? indata[y * inlinesize + x] : 0;
        }
    }
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_YUV420P,  AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    LenscorrectionCtx *rect = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    const AVPixFmtDescriptor *pixdesc = av_pix_fmt_desc_get(inlink->format);
    rect->hsub = pixdesc->log2_chroma_w;
    rect->vsub = pixdesc->log2_chroma_h;
    outlink->w = rect->width = inlink->w;
    outlink->h = rect->height = inlink->h;
    rect->nb_planes = av_pix_fmt_count_planes(inlink->format);
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    LenscorrectionCtx *rect = (LenscorrectionCtx*)ctx->priv;
    AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    int plane;

    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    av_frame_copy_props(out, in);

    for (plane = 0; plane < rect->nb_planes; ++plane) {
        int hsub = plane == 1 || plane == 2 ? rect->hsub : 0;
        int vsub = plane == 1 || plane == 2 ? rect->vsub : 0;
        float hdiv = 1 << hsub;
        float vdiv = 1 << vsub;
        float w = rect->width / hdiv;
        float h = rect->height / vdiv;
        ThreadData td = {
            .in = in,
            .out  = out,
            .w  = w,
            .h  = h,
            .xcenter = rect->cx * w,
            .ycenter = rect->cy * h,
            .k1 = rect->k1,
            .k2 = rect->k2,
            .plane = plane};
        ctx->internal->execute(ctx, filter_slice, &td, NULL, FFMIN(h, ctx->graph->nb_threads));
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad lenscorrection_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad lenscorrection_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
    },
    { NULL }
};

AVFilter ff_vf_lenscorrection = {
    .name          = "lenscorrection",
    .description   = NULL_IF_CONFIG_SMALL("Rectify the image by correcting for lens distortion."),
    .priv_size     = sizeof(LenscorrectionCtx),
    .query_formats = query_formats,
    .inputs        = lenscorrection_inputs,
    .outputs       = lenscorrection_outputs,
    .priv_class    = &lenscorrection_class,
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};
