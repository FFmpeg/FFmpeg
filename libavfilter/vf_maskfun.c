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
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct MaskFunContext {
    const AVClass *class;

    int low, high;
    int planes;
    int fill;
    int sum;

    int linesize[4];
    int width[4], height[4];
    int nb_planes;
    int depth;
    int max;
    uint64_t max_sum;

    AVFrame *empty;
    int (*getsum)(AVFilterContext *ctx, AVFrame *out);
    int (*maskfun)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} MaskFunContext;

#define OFFSET(x) offsetof(MaskFunContext, x)
#define VF AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption maskfun_options[] = {
    { "low",    "set low threshold",  OFFSET(low),    AV_OPT_TYPE_INT, {.i64=10},  0, UINT16_MAX, VF },
    { "high",   "set high threshold", OFFSET(high),   AV_OPT_TYPE_INT, {.i64=10},  0, UINT16_MAX, VF },
    { "planes", "set planes",         OFFSET(planes), AV_OPT_TYPE_INT, {.i64=0xF}, 0, 0xF,        VF },
    { "fill",   "set fill value",     OFFSET(fill),   AV_OPT_TYPE_INT, {.i64=0},   0, UINT16_MAX, VF },
    { "sum",    "set sum value",      OFFSET(sum),    AV_OPT_TYPE_INT, {.i64=10},  0, UINT16_MAX, VF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(maskfun);

static int query_formats(AVFilterContext *ctx)
{
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

    return ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    MaskFunContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    if (s->getsum(ctx, frame)) {
        AVFrame *out = av_frame_clone(s->empty);

        if (!out) {
            av_frame_free(&frame);
            return AVERROR(ENOMEM);
        }
        out->pts = frame->pts;
        av_frame_free(&frame);

        return ff_filter_frame(outlink, out);
    }

    ctx->internal->execute(ctx, s->maskfun, frame, NULL,
                           FFMIN(s->height[1], ff_filter_get_nb_threads(ctx)));

    return ff_filter_frame(outlink, frame);
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
        const int w = s->width[p];                           \
        const int h = s->height[p];                          \
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
    AVFrame *out = arg;                                      \
    const int low = s->low;                                  \
    const int high = s->high;                                \
    const int max = s->max;                                  \
    int p;                                                   \
                                                             \
    for (p = 0; p < s->nb_planes; p++) {                     \
        const int linesize = out->linesize[p] / div;         \
        const int w = s->width[p];                           \
        const int h = s->height[p];                          \
        const int slice_start = (h * jobnr) / nb_jobs;       \
        const int slice_end = (h * (jobnr+1)) / nb_jobs;     \
        type *dst = (type *)out->data[p] + slice_start * linesize; \
                                                             \
        if (!((1 << p) & s->planes))                         \
            continue;                                        \
                                                             \
        for (int y = slice_start; y < slice_end; y++) {      \
            for (int x = 0; x < w; x++) {                    \
                if (dst[x] <= low)                           \
                    dst[x] = 0;                              \
                else if (dst[x] > high)                      \
                    dst[x] = max;                            \
            }                                                \
                                                             \
            dst += linesize;                                 \
        }                                                    \
    }                                                        \
                                                             \
    return 0;                                                \
}

MASKFUN(8, uint8_t, 1)
MASKFUN(16, uint16_t, 2)

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
    s->height[1] = s->height[2] = AV_CEIL_RSHIFT(inlink->h, vsub);
    s->height[0] = s->height[3] = inlink->h;
    s->width[1]  = s->width[2]  = AV_CEIL_RSHIFT(inlink->w, hsub);
    s->width[0]  = s->width[3]  = inlink->w;

    s->depth = desc->comp[0].depth;
    s->max = (1 << s->depth) - 1;
    s->fill = FFMIN(s->fill, s->max);

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

    if (s->depth == 8) {
        for (int p = 0; p < s->nb_planes; p++) {
            uint8_t *dst = s->empty->data[p];

            for (int y = 0; y < s->height[p]; y++) {
                memset(dst, s->fill, s->width[p]);
                dst += s->empty->linesize[p];
            }
        }
    } else {
        for (int p = 0; p < s->nb_planes; p++) {
            uint16_t *dst = (uint16_t *)s->empty->data[p];

            for (int y = 0; y < s->height[p]; y++) {
                for (int x = 0; x < s->width[p]; x++)
                    dst[x] = s->fill;
                dst += s->empty->linesize[p] / 2;
            }
        }
    }

    s->max_sum = 0;
    for (int p = 0; p < s->nb_planes; p++) {
        if (!((1 << p) & s->planes))
            continue;
        s->max_sum += (uint64_t)s->sum * s->width[p] * s->height[p];
    }

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
        .filter_frame   = filter_frame,
        .config_props   = config_input,
        .needs_writable = 1,
    },
    { NULL }
};

static const AVFilterPad maskfun_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_maskfun = {
    .name          = "maskfun",
    .description   = NULL_IF_CONFIG_SMALL("Create Mask."),
    .priv_size     = sizeof(MaskFunContext),
    .query_formats = query_formats,
    .uninit        = uninit,
    .inputs        = maskfun_inputs,
    .outputs       = maskfun_outputs,
    .priv_class    = &maskfun_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};
