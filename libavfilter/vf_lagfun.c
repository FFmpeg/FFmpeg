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
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "filters.h"
#include "video.h"

typedef struct LagfunContext {
    const AVClass *class;
    float decay;
    int planes;

    int depth;
    int nb_planes;
    int linesize[4];
    int planewidth[4];
    int planeheight[4];

    float *old[4];

    int (*lagfun[2])(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} LagfunContext;

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
    AV_PIX_FMT_GRAYF32, AV_PIX_FMT_GBRPF32, AV_PIX_FMT_GBRAPF32,
    AV_PIX_FMT_NONE
};

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

#define LAGFUN(name, type, round, disabled)                               \
static int lagfun_frame##name(AVFilterContext *ctx, void *arg,            \
                              int jobnr, int nb_jobs)                     \
{                                                                         \
    LagfunContext *s = ctx->priv;                                         \
    const float decay = s->decay;                                         \
    ThreadData *td = arg;                                                 \
    AVFrame *in = td->in;                                                 \
    AVFrame *out = td->out;                                               \
                                                                          \
    for (int p = 0; p < s->nb_planes; p++) {                              \
        const int slice_start = (s->planeheight[p] * jobnr) / nb_jobs;    \
        const int slice_end = (s->planeheight[p] * (jobnr+1)) / nb_jobs;  \
        const int width = s->planewidth[p];                               \
        const type *src = (const type *)in->data[p] +                     \
                          slice_start * in->linesize[p] / sizeof(type);   \
        float *osrc = s->old[p] + slice_start * s->planewidth[p];         \
        type *dst = (type *)out->data[p] +                                \
                    slice_start * out->linesize[p] / sizeof(type);        \
                                                                          \
        if (!((1 << p) & s->planes)) {                                    \
            av_image_copy_plane((uint8_t *)dst, out->linesize[p],         \
                                (const uint8_t *)src, in->linesize[p],    \
                                s->linesize[p], slice_end - slice_start); \
            continue;                                                     \
        }                                                                 \
                                                                          \
        for (int y = slice_start; y < slice_end; y++) {                   \
            for (int x = 0; x < width; x++) {                             \
                const float v = fmaxf(src[x], osrc[x] * decay);           \
                                                                          \
                osrc[x] = v;                                              \
                if (disabled) {                                           \
                    dst[x] = src[x];                                      \
                } else {                                                  \
                    dst[x] = round(v);                                    \
                }                                                         \
            }                                                             \
                                                                          \
            src += in->linesize[p] / sizeof(type);                        \
            osrc += width;                                                \
            dst += out->linesize[p] / sizeof(type);                       \
        }                                                                 \
    }                                                                     \
                                                                          \
    return 0;                                                             \
}

LAGFUN(8,  uint8_t,  lrintf, 0)
LAGFUN(16, uint16_t, lrintf, 0)
LAGFUN(32, float,          , 0)

LAGFUN(d8,  uint8_t,  lrintf, 1)
LAGFUN(d16, uint16_t, lrintf, 1)
LAGFUN(d32, float,          , 1)

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    LagfunContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    const AVPixFmtDescriptor *desc;
    int ret;

    desc = av_pix_fmt_desc_get(outlink->format);
    if (!desc)
        return AVERROR_BUG;
    s->nb_planes = av_pix_fmt_count_planes(outlink->format);
    s->depth = desc->comp[0].depth;
    s->lagfun[0] = s->depth <= 8 ? lagfun_frame8 : s->depth <= 16 ? lagfun_frame16 : lagfun_frame32;
    s->lagfun[1] = s->depth <= 8 ? lagfun_framed8 : s->depth <= 16 ? lagfun_framed16 : lagfun_framed32;

    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    s->planewidth[1]  = s->planewidth[2]  = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    for (int p = 0; p < s->nb_planes; p++) {
        s->old[p] = av_calloc(s->planewidth[p] * s->planeheight[p], sizeof(*s->old[0]));
        if (!s->old[p])
            return AVERROR(ENOMEM);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    LagfunContext *s = ctx->priv;
    ThreadData td;
    AVFrame *out;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    td.out = out;
    td.in = in;
    ff_filter_execute(ctx, s->lagfun[!!ctx->is_disabled], &td, NULL,
                      FFMIN(s->planeheight[1], ff_filter_get_nb_threads(ctx)));

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    LagfunContext *s = ctx->priv;

    for (int p = 0; p < s->nb_planes; p++)
        av_freep(&s->old[p]);
}

#define OFFSET(x) offsetof(LagfunContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_RUNTIME_PARAM

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
};

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

AVFILTER_DEFINE_CLASS(lagfun);

const FFFilter ff_vf_lagfun = {
    .p.name        = "lagfun",
    .p.description = NULL_IF_CONFIG_SMALL("Slowly update darker pixels."),
    .p.priv_class  = &lagfun_class,
    .p.flags       = AVFILTER_FLAG_SLICE_THREADS | AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
    .priv_size     = sizeof(LagfunContext),
    .uninit        = uninit,
    FILTER_OUTPUTS(outputs),
    FILTER_INPUTS(inputs),
    FILTER_PIXFMTS_ARRAY(pixel_fmts),
    .process_command = ff_filter_process_command,
};
