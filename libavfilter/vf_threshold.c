/*
 * Copyright (c) 2016 Paul B Mahol
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
 * threshold video filter
 */

#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "framesync.h"
#include "internal.h"
#include "video.h"
#include "threshold.h"
#include "vf_threshold_init.h"

#define OFFSET(x) offsetof(ThresholdContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption threshold_options[] = {
    { "planes", "set planes to filter", OFFSET(planes), AV_OPT_TYPE_INT,  {.i64=15}, 0, 15, FLAGS},
    { NULL }
};

AVFILTER_DEFINE_CLASS(threshold);

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV440P10, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV440P12, AV_PIX_FMT_YUV444P12,
    AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA444P9,
    AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
    AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA444P12,
    AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
    AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12 , AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10,
    AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_NONE
};

typedef struct ThreadData {
    AVFrame *in;
    AVFrame *threshold;
    AVFrame *min;
    AVFrame *max;
    AVFrame *out;
} ThreadData;

static int filter_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ThresholdContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *min = td->min;
    AVFrame *max = td->max;
    AVFrame *threshold = td->threshold;
    AVFrame *in = td->in;
    AVFrame *out = td->out;

    for (int p = 0; p < s->nb_planes; p++) {
        const int h = s->height[p];
        const int slice_start = (h * jobnr) / nb_jobs;
        const int slice_end = (h * (jobnr+1)) / nb_jobs;

        if (!(s->planes & (1 << p))) {
            av_image_copy_plane(out->data[p] + slice_start * out->linesize[p],
                                out->linesize[p],
                                in->data[p] + slice_start * in->linesize[p],
                                in->linesize[p],
                                s->width[p] * s->bpc,
                                slice_end - slice_start);
            continue;
        }
        s->threshold(in->data[p] + slice_start * in->linesize[p],
                     threshold->data[p] + slice_start * threshold->linesize[p],
                     min->data[p] + slice_start * min->linesize[p],
                     max->data[p] + slice_start * max->linesize[p],
                     out->data[p] + slice_start * out->linesize[p],
                     in->linesize[p], threshold->linesize[p],
                     min->linesize[p], max->linesize[p],
                     out->linesize[p],
                     s->width[p], slice_end - slice_start);
    }

    return 0;
}

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    ThresholdContext *s = fs->opaque;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out, *in, *threshold, *min, *max;
    ThreadData td;
    int ret;

    if ((ret = ff_framesync_get_frame(&s->fs, 0, &in,        0)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 1, &threshold, 0)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 2, &min,       0)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 3, &max,       0)) < 0)
        return ret;

    if (ctx->is_disabled) {
        out = av_frame_clone(in);
        if (!out)
            return AVERROR(ENOMEM);
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out)
            return AVERROR(ENOMEM);
        av_frame_copy_props(out, in);

        td.out = out;
        td.in = in;
        td.threshold = threshold;
        td.min = min;
        td.max = max;
        ff_filter_execute(ctx, filter_slice, &td, NULL,
                          FFMIN(s->height[2], ff_filter_get_nb_threads(ctx)));
    }

    out->pts = av_rescale_q(s->fs.pts, s->fs.time_base, outlink->time_base);

    return ff_filter_frame(outlink, out);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ThresholdContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int vsub, hsub;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    hsub = desc->log2_chroma_w;
    vsub = desc->log2_chroma_h;
    s->height[1] = s->height[2] = AV_CEIL_RSHIFT(inlink->h, vsub);
    s->height[0] = s->height[3] = inlink->h;
    s->width[1]  = s->width[2]  = AV_CEIL_RSHIFT(inlink->w, hsub);
    s->width[0]  = s->width[3]  = inlink->w;
    s->depth = desc->comp[0].depth;

    ff_threshold_init(s);

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    ThresholdContext *s = ctx->priv;
    AVFilterLink *base = ctx->inputs[0];
    AVFilterLink *threshold = ctx->inputs[1];
    AVFilterLink *min = ctx->inputs[2];
    AVFilterLink *max = ctx->inputs[3];
    FFFrameSyncIn *in;
    int ret;

    if (base->w                       != threshold->w ||
        base->h                       != threshold->h ||
        base->w                       != min->w ||
        base->h                       != min->h ||
        base->w                       != max->w ||
        base->h                       != max->h) {
        av_log(ctx, AV_LOG_ERROR, "First input link %s parameters "
               "(size %dx%d) do not match the corresponding "
               "second input link %s parameters (%dx%d) "
               "and/or third input link %s parameters (%dx%d) "
               "and/or fourth input link %s parameters (%dx%d)\n",
               ctx->input_pads[0].name, base->w, base->h,
               ctx->input_pads[1].name, threshold->w, threshold->h,
               ctx->input_pads[2].name, min->w, min->h,
               ctx->input_pads[3].name, max->w, max->h);
        return AVERROR(EINVAL);
    }

    outlink->w = base->w;
    outlink->h = base->h;
    outlink->sample_aspect_ratio = base->sample_aspect_ratio;
    outlink->frame_rate = base->frame_rate;

    if ((ret = ff_framesync_init(&s->fs, ctx, 4)) < 0)
        return ret;

    in = s->fs.in;
    in[0].time_base = base->time_base;
    in[1].time_base = threshold->time_base;
    in[2].time_base = min->time_base;
    in[3].time_base = max->time_base;
    in[0].sync   = 1;
    in[0].before = EXT_STOP;
    in[0].after  = EXT_STOP;
    in[1].sync   = 1;
    in[1].before = EXT_STOP;
    in[1].after  = EXT_STOP;
    in[2].sync   = 1;
    in[2].before = EXT_STOP;
    in[2].after  = EXT_STOP;
    in[3].sync   = 1;
    in[3].before = EXT_STOP;
    in[3].after  = EXT_STOP;
    s->fs.opaque   = s;
    s->fs.on_event = process_frame;

    ret = ff_framesync_configure(&s->fs);
    outlink->time_base = s->fs.time_base;

    return ret;
}

static int activate(AVFilterContext *ctx)
{
    ThresholdContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ThresholdContext *s = ctx->priv;

    ff_framesync_uninit(&s->fs);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
    },
    {
        .name         = "threshold",
        .type         = AVMEDIA_TYPE_VIDEO,
    },
    {
        .name         = "min",
        .type         = AVMEDIA_TYPE_VIDEO,
    },
    {
        .name         = "max",
        .type         = AVMEDIA_TYPE_VIDEO,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const AVFilter ff_vf_threshold = {
    .name          = "threshold",
    .description   = NULL_IF_CONFIG_SMALL("Threshold first video stream using other video streams."),
    .priv_size     = sizeof(ThresholdContext),
    .priv_class    = &threshold_class,
    .uninit        = uninit,
    .activate      = activate,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
