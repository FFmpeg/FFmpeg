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
 */

#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct LagfunContext {
    const AVClass *class;
    const AVPixFmtDescriptor *desc;
    float decay;
    int planes;

    int depth;
    int nb_planes;
    int linesize[4];
    int height[4];

    AVFrame *old;

    int (*lagfun)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} LagfunContext;

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pixel_fmts[] = {
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9,
        AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14,
        AV_PIX_FMT_GRAY16,
        AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_YUVJ411P,
        AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
        AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
        AV_PIX_FMT_YUV440P10,
        AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV420P12,
        AV_PIX_FMT_YUV440P12,
        AV_PIX_FMT_YUV444P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV420P14,
        AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
        AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *formats = ff_make_format_list(pixel_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, formats);
}

typedef struct ThreadData {
    AVFrame *in, *out, *old;
} ThreadData;

static int lagfun_frame8(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    LagfunContext *s = ctx->priv;
    const float decay = s->decay;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    AVFrame *old = td->old;

    for (int p = 0; p < s->nb_planes; p++) {
        const int slice_start = (s->height[p] * jobnr) / nb_jobs;
        const int slice_end = (s->height[p] * (jobnr+1)) / nb_jobs;
        const uint8_t *src = in->data[p] + slice_start * in->linesize[p];
        const uint8_t *osrc = old->data[p] + slice_start * old->linesize[p];
        uint8_t *dst = out->data[p] + slice_start * out->linesize[p];

        if (!((1 << p) & s->planes)) {
            av_image_copy_plane(dst, out->linesize[p],
                                src, in->linesize[p],
                                s->linesize[p], slice_end - slice_start);
            continue;
        }

        for (int y = slice_start; y < slice_end; y++) {
            for (int x = 0; x < s->linesize[p]; x++)
                dst[x] = FFMAX(src[x], osrc[x] * decay);

            src += in->linesize[p];
            osrc += old->linesize[p];
            dst += out->linesize[p];
        }
    }

    return 0;
}

static int lagfun_frame16(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    LagfunContext *s = ctx->priv;
    const float decay = s->decay;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    AVFrame *old = td->old;

    for (int p = 0; p < s->nb_planes; p++) {
        const int slice_start = (s->height[p] * jobnr) / nb_jobs;
        const int slice_end = (s->height[p] * (jobnr+1)) / nb_jobs;
        const uint16_t *src = (const uint16_t *)in->data[p] + slice_start * in->linesize[p] / 2;
        const uint16_t *osrc = (const uint16_t *)old->data[p] + slice_start * old->linesize[p] / 2;
        uint16_t *dst = (uint16_t *)out->data[p] + slice_start * out->linesize[p] / 2;

        if (!((1 << p) & s->planes)) {
            av_image_copy_plane((uint8_t *)dst, out->linesize[p],
                                (uint8_t *)src, in->linesize[p],
                                s->linesize[p], slice_end - slice_start);
            continue;
        }

        for (int y = slice_start; y < slice_end; y++) {
            for (int x = 0; x < s->linesize[p]; x++)
                dst[x] = FFMAX(src[x], osrc[x] * decay);

            src += in->linesize[p] / 2;
            osrc += old->linesize[p] / 2;
            dst += out->linesize[p] / 2;
        }
    }

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    LagfunContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    int ret;

    s->desc = av_pix_fmt_desc_get(outlink->format);
    if (!s->desc)
        return AVERROR_BUG;
    s->nb_planes = av_pix_fmt_count_planes(outlink->format);
    s->depth = s->desc->comp[0].depth;
    s->lagfun = s->depth <= 8 ? lagfun_frame8 : lagfun_frame16;

    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    s->height[1] = s->height[2] = AV_CEIL_RSHIFT(inlink->h, s->desc->log2_chroma_h);
    s->height[0] = s->height[3] = inlink->h;

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    LagfunContext *s = ctx->priv;
    ThreadData td;
    AVFrame *out;

    if (!s->old) {
        s->old = av_frame_clone(in);
        return ff_filter_frame(outlink, in);
    }

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    out->pts = in->pts;

    td.out = out;
    td.in = in;
    td.old = s->old;
    ctx->internal->execute(ctx, s->lagfun, &td, NULL, FFMIN(s->height[1], ff_filter_get_nb_threads(ctx)));

    av_frame_free(&s->old);
    av_frame_free(&in);
    s->old = av_frame_clone(out);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    LagfunContext *s = ctx->priv;

    av_frame_free(&s->old);
}

#define OFFSET(x) offsetof(LagfunContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM

static const AVOption lagfun_options[] = {
    { "decay",  "set decay",                 OFFSET(decay),  AV_OPT_TYPE_FLOAT, {.dbl=.95},  0,  1,  FLAGS },
    { "planes", "set what planes to filter", OFFSET(planes), AV_OPT_TYPE_FLAGS, {.i64=15},   0, 15,  FLAGS },
    { NULL },
};

static const AVFilterPad inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .filter_frame  = filter_frame,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

AVFILTER_DEFINE_CLASS(lagfun);

AVFilter ff_vf_lagfun = {
    .name          = "lagfun",
    .description   = NULL_IF_CONFIG_SMALL("Slowly update darker pixels."),
    .priv_size     = sizeof(LagfunContext),
    .priv_class    = &lagfun_class,
    .query_formats = query_formats,
    .uninit        = uninit,
    .outputs       = outputs,
    .inputs        = inputs,
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};
