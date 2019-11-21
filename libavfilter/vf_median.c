/*
 * Copyright (c) 2019 Paul B Mahol
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
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 */

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "median.h"
#include "video.h"

#define DEPTH 8
#include "median_template.c"

#undef DEPTH
#define DEPTH 9
#include "median_template.c"

#undef DEPTH
#define DEPTH 10
#include "median_template.c"

#undef DEPTH
#define DEPTH 12
#include "median_template.c"

#undef DEPTH
#define DEPTH 14
#include "median_template.c"

#undef DEPTH
#define DEPTH 16
#include "median_template.c"

#define OFFSET(x) offsetof(MedianContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption median_options[] = {
    { "radius", "set median radius",    OFFSET(radius), AV_OPT_TYPE_INT,   {.i64=1},     1,  127, FLAGS },
    { "planes", "set planes to filter", OFFSET(planes), AV_OPT_TYPE_INT,   {.i64=0xF},   0,  0xF, FLAGS },
    { "radiusV", "set median vertical radius", OFFSET(radiusV), AV_OPT_TYPE_INT, {.i64=0},0, 127, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(median);

static void hadd(htype *dst, const htype *src, int bins)
{
    for (int i = 0; i < bins; i++)
        dst[i] += src[i];
}

static void hsub(htype *dst, const htype *src, int bins)
{
    for (int i = 0; i < bins; i++)
        dst[i] -= src[i];
}

static void hmuladd(htype *dst, const htype *src, int f, int bins)
{
    for (int i = 0; i < bins; i++)
        dst[i] += f * src[i];
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP, AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9,
        AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9, AV_PIX_FMT_GBRP9,
        AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA444P9,
        AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
        AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV440P12,
        AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14,
        AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
        AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
        AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA444P12,
        AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
        AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
        AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
        AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
        AV_PIX_FMT_NONE
    };

    return ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
}

static void check_params(MedianContext *s, AVFilterLink *inlink)
{
    for (int i = 0; i < s->nb_planes; i++) {
        if (!(s->planes & (1 << i)))
            continue;

        if (s->planewidth[i] < s->radius * 2 + 1) {
            av_log(inlink->dst, AV_LOG_WARNING, "The %d plane width %d must be not less than %d, clipping radius.\n", i, s->planewidth[i], s->radius * 2 + 1);
            s->radius = (s->planewidth[i] - 1) / 2;
        }

        if (s->planeheight[i] < s->radiusV * 2 + 1) {
            av_log(inlink->dst, AV_LOG_WARNING, "The %d plane height %d must be not less than %d, clipping radiusV.\n", i, s->planeheight[i], s->radiusV * 2 + 1);
            s->radiusV = (s->planeheight[i] - 1) / 2;
        }
    }

    s->t = 2 * s->radius * s->radiusV + 2 * s->radius;
}

static int config_input(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    MedianContext *s = inlink->dst->priv;

    s->depth = desc->comp[0].depth;
    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->radiusV = !s->radiusV ? s->radius : s->radiusV;
    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    check_params(s, inlink);

    s->nb_threads = FFMAX(1, FFMIN(s->planeheight[1] / (s->radiusV + 1), ff_filter_get_nb_threads(inlink->dst)));
    s->bins   = 1 << ((s->depth + 1) / 2);
    s->fine_size = s->bins * s->bins * inlink->w;
    s->coarse_size = s->bins * inlink->w;
    s->coarse = av_calloc(s->nb_threads, sizeof(*s->coarse));
    s->fine   = av_calloc(s->nb_threads, sizeof(*s->fine));
    if (!s->coarse || !s->fine)
        return AVERROR(ENOMEM);
    for (int i = 0; i < s->nb_threads; i++) {
        s->coarse[i] = av_malloc_array(s->coarse_size, sizeof(**s->coarse));
        s->fine[i]   = av_malloc_array(s->fine_size, sizeof(**s->fine));
        if (!s->coarse[i] || !s->fine[i])
            return AVERROR(ENOMEM);
    }

    s->hadd = hadd;
    s->hsub = hsub;
    s->hmuladd = hmuladd;

    switch (s->depth) {
    case  8: s->filter_plane = filter_plane_8;  break;
    case  9: s->filter_plane = filter_plane_9;  break;
    case 10: s->filter_plane = filter_plane_10; break;
    case 12: s->filter_plane = filter_plane_12; break;
    case 14: s->filter_plane = filter_plane_14; break;
    case 16: s->filter_plane = filter_plane_16; break;
    }

    return 0;
}

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

static int filter_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    MedianContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;

    for (int plane = 0; plane < s->nb_planes; plane++) {
        const int h = s->planeheight[plane];
        const int w = s->planewidth[plane];
        const int slice_h_start = (h * jobnr) / nb_jobs;
        const int slice_h_end = (h * (jobnr+1)) / nb_jobs;

        if (!(s->planes & (1 << plane))) {
            av_image_copy_plane(out->data[plane] + slice_h_start * out->linesize[plane],
                                out->linesize[plane],
                                in->data[plane] + slice_h_start * in->linesize[plane],
                                in->linesize[plane],
                                w * ((s->depth + 7) / 8),
                                slice_h_end - slice_h_start);
            continue;
        }

        s->filter_plane(ctx, in->data[plane],
                        in->linesize[plane],
                        out->data[plane] + slice_h_start * out->linesize[plane],
                        out->linesize[plane], w, h,
                        slice_h_start, slice_h_end, jobnr);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    MedianContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    ThreadData td;
    AVFrame *out;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    td.in = in; td.out = out;
    ctx->internal->execute(ctx, filter_slice, &td, NULL, s->nb_threads);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    MedianContext *s = ctx->priv;

    for (int i = 0; i < s->nb_threads && s->coarse && s->fine; i++) {
        av_freep(&s->coarse[i]);
        av_freep(&s->fine[i]);
    }

    av_freep(&s->coarse);
    av_freep(&s->fine);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    MedianContext *s = ctx->priv;
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    if (!s->radiusV)
        s->radiusV = s->radius;
    check_params(s, ctx->inputs[0]);

    return 0;
}

static const AVFilterPad median_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad median_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_median = {
    .name          = "median",
    .description   = NULL_IF_CONFIG_SMALL("Apply Median filter."),
    .priv_size     = sizeof(MedianContext),
    .priv_class    = &median_class,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = median_inputs,
    .outputs       = median_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = process_command,
};
