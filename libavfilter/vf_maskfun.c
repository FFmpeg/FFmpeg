/*
 * Copyright (c) 2018 Paul B Mahol
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
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "internal.h"
#include "video.h"

typedef struct MaskFunContext {
    const AVClass *class;

    int low, high;
    int planes;
    int fill;
    int sum;

    int linesize[4];
    int planewidth[4], planeheight[4];
    int nb_planes;
    int depth;
    int max;
    uint64_t max_sum;

    AVFrame *in;
    AVFrame *empty;

    int (*getsum)(AVFilterContext *ctx, AVFrame *out);
    int (*maskfun)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} MaskFunContext;

#define OFFSET(x) offsetof(MaskFunContext, x)
#define VFT AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption maskfun_options[] = {
    { "low",    "set low threshold",  OFFSET(low),    AV_OPT_TYPE_INT, {.i64=10},  0, UINT16_MAX, VFT },
    { "high",   "set high threshold", OFFSET(high),   AV_OPT_TYPE_INT, {.i64=10},  0, UINT16_MAX, VFT },
    { "planes", "set planes",         OFFSET(planes), AV_OPT_TYPE_INT, {.i64=0xF}, 0, 0xF,        VFT },
    { "fill",   "set fill value",     OFFSET(fill),   AV_OPT_TYPE_INT, {.i64=0},   0, UINT16_MAX, VFT },
    { "sum",    "set sum value",      OFFSET(sum),    AV_OPT_TYPE_INT, {.i64=10},  0, UINT16_MAX, VFT },
    { NULL }
};

AVFILTER_DEFINE_CLASS(maskfun);

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV440P12,
    AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14,
    AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA444P9,
    AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
    AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
    AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_NONE
};

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    MaskFunContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;

    if (s->getsum(ctx, in)) {
        AVFrame *out = av_frame_clone(s->empty);

        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        out->pts = in->pts;
        av_frame_free(&in);

        return ff_filter_frame(outlink, out);
    }

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out)
            return AVERROR(ENOMEM);
        av_frame_copy_props(out, in);
    }

    s->in = in;
    ff_filter_execute(ctx, s->maskfun, out, NULL,
                      FFMIN(s->planeheight[1], ff_filter_get_nb_threads(ctx)));

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

#define GETSUM(name, type, div)                              \
static int getsum##name(AVFilterContext *ctx, AVFrame *out)  \
{                                                            \
    MaskFunContext *s = ctx->priv;                           \
    uint64_t sum = 0;                                        \
    int p;                                                   \
                                                             \
    for (p = 0; p < s->nb_planes; p++) {                     \
        const int linesize = out->linesize[p] / div;         \
        const int w = s->planewidth[p];                      \
        const int h = s->planeheight[p];                     \
        type *dst = (type *)out->data[p];                    \
                                                             \
        if (!((1 << p) & s->planes))                         \
            continue;                                        \
                                                             \
        for (int y = 0; y < h; y++) {                        \
            for (int x = 0; x < w; x++)                      \
                sum += dst[x];                               \
            if (sum >= s->max_sum)                           \
                return 1;                                    \
            dst += linesize;                                 \
        }                                                    \
    }                                                        \
                                                             \
    return 0;                                                \
}

GETSUM(8, uint8_t, 1)
GETSUM(16, uint16_t, 2)

#define MASKFUN(name, type, div)                             \
static int maskfun##name(AVFilterContext *ctx, void *arg,    \
                         int jobnr, int nb_jobs)             \
{                                                            \
    MaskFunContext *s = ctx->priv;                           \
    AVFrame *in = s->in;                                     \
    AVFrame *out = arg;                                      \
    const int low = s->low;                                  \
    const int high = s->high;                                \
    const int max = s->max;                                  \
    int p;                                                   \
                                                             \
    for (p = 0; p < s->nb_planes; p++) {                     \
        const int src_linesize = in->linesize[p] / div;      \
        const int linesize = out->linesize[p] / div;         \
        const int w = s->planewidth[p];                      \
        const int h = s->planeheight[p];                     \
        const int slice_start = (h * jobnr) / nb_jobs;       \
        const int slice_end = (h * (jobnr+1)) / nb_jobs;     \
        const type *src = (type *)in->data[p] +              \
                           slice_start * src_linesize;       \
        type *dst = (type *)out->data[p] +                   \
                    slice_start * linesize;                  \
                                                             \
        if (!((1 << p) & s->planes))                         \
            continue;                                        \
                                                             \
        for (int y = slice_start; y < slice_end; y++) {      \
            for (int x = 0; x < w; x++) {                    \
                dst[x] = src[x];                             \
                if (dst[x] <= low)                           \
                    dst[x] = 0;                              \
                else if (dst[x] > high)                      \
                    dst[x] = max;                            \
            }                                                \
                                                             \
            src += src_linesize;                             \
            dst += linesize;                                 \
        }                                                    \
    }                                                        \
                                                             \
    return 0;                                                \
}

MASKFUN(8, uint8_t, 1)
MASKFUN(16, uint16_t, 2)

static void fill_frame(AVFilterContext *ctx)
{
    MaskFunContext *s = ctx->priv;

    s->fill = FFMIN(s->fill, s->max);
    if (s->depth == 8) {
        for (int p = 0; p < s->nb_planes; p++) {
            uint8_t *dst = s->empty->data[p];

            for (int y = 0; y < s->planeheight[p]; y++) {
                memset(dst, s->fill, s->planewidth[p]);
                dst += s->empty->linesize[p];
            }
        }
    } else {
        for (int p = 0; p < s->nb_planes; p++) {
            uint16_t *dst = (uint16_t *)s->empty->data[p];

            for (int y = 0; y < s->planeheight[p]; y++) {
                for (int x = 0; x < s->planewidth[p]; x++)
                    dst[x] = s->fill;
                dst += s->empty->linesize[p] / 2;
            }
        }
    }
}

static void set_max_sum(AVFilterContext *ctx)
{
    MaskFunContext *s = ctx->priv;

    s->max_sum = 0;
    for (int p = 0; p < s->nb_planes; p++) {
        if (!((1 << p) & s->planes))
            continue;
        s->max_sum += (uint64_t)s->sum * s->planewidth[p] * s->planeheight[p];
    }
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    MaskFunContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int vsub, hsub, ret;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    hsub = desc->log2_chroma_w;
    vsub = desc->log2_chroma_h;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, vsub);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planewidth[1]  = s->planewidth[2]  = AV_CEIL_RSHIFT(inlink->w, hsub);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;

    s->depth = desc->comp[0].depth;
    s->max = (1 << s->depth) - 1;

    if (s->depth == 8) {
        s->maskfun = maskfun8;
        s->getsum = getsum8;
    } else {
        s->maskfun = maskfun16;
        s->getsum = getsum16;
    }

    s->empty = ff_get_video_buffer(inlink, inlink->w, inlink->h);
    if (!s->empty)
        return AVERROR(ENOMEM);

    fill_frame(ctx);

    set_max_sum(ctx);

    return 0;
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    MaskFunContext *s = ctx->priv;
    int fill = s->fill;
    int sum = s->sum;
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    if (sum != s->sum)
        set_max_sum(ctx);

    if (fill != s->fill)
        fill_frame(ctx);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    MaskFunContext *s = ctx->priv;

    av_frame_free(&s->empty);
}

static const AVFilterPad maskfun_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .flags          = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .filter_frame   = filter_frame,
        .config_props   = config_input,
    },
};

const AVFilter ff_vf_maskfun = {
    .name          = "maskfun",
    .description   = NULL_IF_CONFIG_SMALL("Create Mask."),
    .priv_size     = sizeof(MaskFunContext),
    .uninit        = uninit,
    FILTER_INPUTS(maskfun_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class    = &maskfun_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = process_command,
};
