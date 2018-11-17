/*
 * Copyright (c) 2012-2013 Oka Motofumi (chikuzen.mo at gmail dot com)
 * Copyright (c) 2015 Paul B Mahol
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
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

typedef struct NContext {
    const AVClass *class;
    int planeheight[4];
    int planewidth[4];
    int nb_planes;
    int threshold[4];
    int coordinates;

    int depth;
    int max;
    int bpc;

    void (*filter)(uint8_t *dst, const uint8_t *p1, int width,
                   int threshold, const uint8_t *coordinates[], int coord,
                   int maxc);
} NContext;

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

static void erosion(uint8_t *dst, const uint8_t *p1, int width,
                    int threshold, const uint8_t *coordinates[], int coord,
                    int maxc)
{
    int x, i;

    for (x = 0; x < width; x++) {
        int min = p1[x];
        int limit = FFMAX(min - threshold, 0);

        for (i = 0; i < 8; i++) {
            if (coord & (1 << i)) {
                min = FFMIN(min, *(coordinates[i] + x));
            }
            min = FFMAX(min, limit);
        }

        dst[x] = min;
    }
}

static void erosion16(uint8_t *dstp, const uint8_t *p1, int width,
                      int threshold, const uint8_t *coordinates[], int coord,
                      int maxc)
{
    uint16_t *dst = (uint16_t *)dstp;
    int x, i;

    for (x = 0; x < width; x++) {
        int min = AV_RN16A(&p1[2 * x]);
        int limit = FFMAX(min - threshold, 0);

        for (i = 0; i < 8; i++) {
            if (coord & (1 << i)) {
                min = FFMIN(min, AV_RN16A(coordinates[i] + x * 2));
            }
            min = FFMAX(min, limit);
        }

        dst[x] = min;
    }
}

static void dilation(uint8_t *dst, const uint8_t *p1, int width,
                     int threshold, const uint8_t *coordinates[], int coord,
                     int maxc)
{
    int x, i;

    for (x = 0; x < width; x++) {
        int max = p1[x];
        int limit = FFMIN(max + threshold, 255);

        for (i = 0; i < 8; i++) {
            if (coord & (1 << i)) {
                max = FFMAX(max, *(coordinates[i] + x));
            }
            max = FFMIN(max, limit);
        }

        dst[x] = max;
    }
}

static void dilation16(uint8_t *dstp, const uint8_t *p1, int width,
                       int threshold, const uint8_t *coordinates[], int coord,
                       int maxc)
{
    uint16_t *dst = (uint16_t *)dstp;
    int x, i;

    for (x = 0; x < width; x++) {
        int max = AV_RN16A(&p1[x * 2]);
        int limit = FFMIN(max + threshold, maxc);

        for (i = 0; i < 8; i++) {
            if (coord & (1 << i)) {
                max = FFMAX(max, AV_RN16A(coordinates[i] + x * 2));
            }
            max = FFMIN(max, limit);
        }

        dst[x] = max;
    }
}

static void deflate(uint8_t *dst, const uint8_t *p1, int width,
                    int threshold, const uint8_t *coordinates[], int coord,
                    int maxc)
{
    int x, i;

    for (x = 0; x < width; x++) {
        int sum = 0;
        int limit = FFMAX(p1[x] - threshold, 0);

        for (i = 0; i < 8; sum += *(coordinates[i++] + x));

        dst[x] = FFMAX(FFMIN(sum / 8, p1[x]), limit);
    }
}

static void deflate16(uint8_t *dstp, const uint8_t *p1, int width,
                      int threshold, const uint8_t *coordinates[], int coord,
                      int maxc)
{
    uint16_t *dst = (uint16_t *)dstp;
    int x, i;

    for (x = 0; x < width; x++) {
        int sum = 0;
        int limit = FFMAX(AV_RN16A(&p1[2 * x]) - threshold, 0);

        for (i = 0; i < 8; sum += AV_RN16A(coordinates[i++] + x * 2));

        dst[x] = FFMAX(FFMIN(sum / 8, AV_RN16A(&p1[2 * x])), limit);
    }
}

static void inflate(uint8_t *dst, const uint8_t *p1, int width,
                    int threshold, const uint8_t *coordinates[], int coord,
                    int maxc)
{
    int x, i;

    for (x = 0; x < width; x++) {
        int sum = 0;
        int limit = FFMIN(p1[x] + threshold, 255);

        for (i = 0; i < 8; sum += *(coordinates[i++] + x));

        dst[x] = FFMIN(FFMAX(sum / 8, p1[x]), limit);
    }
}

static void inflate16(uint8_t *dstp, const uint8_t *p1, int width,
                      int threshold, const uint8_t *coordinates[], int coord,
                      int maxc)
{
    uint16_t *dst = (uint16_t *)dstp;
    int x, i;

    for (x = 0; x < width; x++) {
        int sum = 0;
        int limit = FFMIN(AV_RN16A(&p1[2 * x]) + threshold, maxc);

        for (i = 0; i < 8; sum += AV_RN16A(coordinates[i++] + x * 2));

        dst[x] = FFMIN(FFMAX(sum / 8, AV_RN16A(&p1[x * 2])), limit);
    }
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    NContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    s->depth = desc->comp[0].depth;
    s->max = (1 << s->depth) - 1;
    s->bpc = (s->depth + 7) / 8;

    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    if (!strcmp(ctx->filter->name, "erosion"))
        s->filter = s->depth > 8 ? erosion16 : erosion;
    else if (!strcmp(ctx->filter->name, "dilation"))
        s->filter = s->depth > 8 ? dilation16 : dilation;
    else if (!strcmp(ctx->filter->name, "deflate"))
        s->filter = s->depth > 8 ? deflate16 : deflate;
    else if (!strcmp(ctx->filter->name, "inflate"))
        s->filter = s->depth > 8 ? inflate16 : inflate;

    return 0;
}

static int filter_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    NContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *out = td->out;
    AVFrame *in = td->in;
    int plane, y;

    for (plane = 0; plane < s->nb_planes; plane++) {
        const int bpc = s->bpc;
        const int threshold = s->threshold[plane];
        const int stride = in->linesize[plane];
        const int dstride = out->linesize[plane];
        const int height = s->planeheight[plane];
        const int width  = s->planewidth[plane];
        const int slice_start = (height * jobnr) / nb_jobs;
        const int slice_end = (height * (jobnr+1)) / nb_jobs;
        const uint8_t *src = (const uint8_t *)in->data[plane] + slice_start * stride;
        uint8_t *dst = out->data[plane] + slice_start * dstride;

        if (!threshold) {
            av_image_copy_plane(dst, dstride, src, stride, width * bpc, slice_end - slice_start);
            continue;
        }

        for (y = slice_start; y < slice_end; y++) {
            const int nh = y > 0;
            const int ph = y < height - 1;
            const uint8_t *coordinates[] = { src - nh * stride, src + 1 * bpc - nh * stride, src + 2 * bpc - nh * stride,
                                             src,                                            src + 2 * bpc,
                                             src + ph * stride, src + 1 * bpc + ph * stride, src + 2 * bpc + ph * stride};

            const uint8_t *coordinateslb[] = { src + 1 * bpc - nh * stride, src - nh * stride, src + 1 * bpc - nh * stride,
                                               src + 1 * bpc,                                  src + 1 * bpc,
                                               src + 1 * bpc + ph * stride, src + ph * stride, src + 1 * bpc + ph * stride};

            const uint8_t *coordinatesrb[] = { src + (width - 2) * bpc - nh * stride, src + (width - 1) * bpc - nh * stride, src + (width - 2) * bpc - nh * stride,
                                               src + (width - 2) * bpc,                                                      src + (width - 2) * bpc,
                                               src + (width - 2) * bpc + ph * stride, src + (width - 1) * bpc + ph * stride, src + (width - 2) * bpc + ph * stride};

            s->filter(dst,                     src,                     1,         threshold, coordinateslb, s->coordinates, s->max);
            s->filter(dst          + 1  * bpc, src          + 1  * bpc, width - 2, threshold, coordinates,   s->coordinates, s->max);
            s->filter(dst + (width - 1) * bpc, src + (width - 1) * bpc, 1,         threshold, coordinatesrb, s->coordinates, s->max);

            src += stride;
            dst += dstride;
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    NContext *s = ctx->priv;
    ThreadData td;
    AVFrame *out;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    td.in = in;
    td.out = out;
    ctx->internal->execute(ctx, filter_slice, &td, NULL, FFMIN(s->planeheight[1], ff_filter_get_nb_threads(ctx)));

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad neighbor_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad neighbor_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

#define OFFSET(x) offsetof(NContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

#define DEFINE_NEIGHBOR_FILTER(name_, description_)          \
AVFILTER_DEFINE_CLASS(name_);                                \
                                                             \
AVFilter ff_vf_##name_ = {                                   \
    .name          = #name_,                                 \
    .description   = NULL_IF_CONFIG_SMALL(description_),     \
    .priv_size     = sizeof(NContext),                       \
    .priv_class    = &name_##_class,                         \
    .query_formats = query_formats,                          \
    .inputs        = neighbor_inputs,                        \
    .outputs       = neighbor_outputs,                       \
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC| \
                     AVFILTER_FLAG_SLICE_THREADS,            \
}

#if CONFIG_EROSION_FILTER

static const AVOption erosion_options[] = {
    { "threshold0",  "set threshold for 1st plane",   OFFSET(threshold[0]),   AV_OPT_TYPE_INT, {.i64=65535}, 0, 65535, FLAGS },
    { "threshold1",  "set threshold for 2nd plane",   OFFSET(threshold[1]),   AV_OPT_TYPE_INT, {.i64=65535}, 0, 65535, FLAGS },
    { "threshold2",  "set threshold for 3rd plane",   OFFSET(threshold[2]),   AV_OPT_TYPE_INT, {.i64=65535}, 0, 65535, FLAGS },
    { "threshold3",  "set threshold for 4th plane",   OFFSET(threshold[3]),   AV_OPT_TYPE_INT, {.i64=65535}, 0, 65535, FLAGS },
    { "coordinates", "set coordinates",               OFFSET(coordinates),    AV_OPT_TYPE_INT, {.i64=255},   0, 255,   FLAGS },
    { NULL }
};

DEFINE_NEIGHBOR_FILTER(erosion, "Apply erosion effect.");

#endif /* CONFIG_EROSION_FILTER */

#if CONFIG_DILATION_FILTER

static const AVOption dilation_options[] = {
    { "threshold0",  "set threshold for 1st plane",   OFFSET(threshold[0]),   AV_OPT_TYPE_INT, {.i64=65535}, 0, 65535, FLAGS },
    { "threshold1",  "set threshold for 2nd plane",   OFFSET(threshold[1]),   AV_OPT_TYPE_INT, {.i64=65535}, 0, 65535, FLAGS },
    { "threshold2",  "set threshold for 3rd plane",   OFFSET(threshold[2]),   AV_OPT_TYPE_INT, {.i64=65535}, 0, 65535, FLAGS },
    { "threshold3",  "set threshold for 4th plane",   OFFSET(threshold[3]),   AV_OPT_TYPE_INT, {.i64=65535}, 0, 65535, FLAGS },
    { "coordinates", "set coordinates",               OFFSET(coordinates),    AV_OPT_TYPE_INT, {.i64=255},   0, 255,   FLAGS },
    { NULL }
};

DEFINE_NEIGHBOR_FILTER(dilation, "Apply dilation effect.");

#endif /* CONFIG_DILATION_FILTER */

#if CONFIG_DEFLATE_FILTER

static const AVOption deflate_options[] = {
    { "threshold0", "set threshold for 1st plane",   OFFSET(threshold[0]),   AV_OPT_TYPE_INT, {.i64=65535}, 0, 65535, FLAGS },
    { "threshold1", "set threshold for 2nd plane",   OFFSET(threshold[1]),   AV_OPT_TYPE_INT, {.i64=65535}, 0, 65535, FLAGS },
    { "threshold2", "set threshold for 3rd plane",   OFFSET(threshold[2]),   AV_OPT_TYPE_INT, {.i64=65535}, 0, 65535, FLAGS },
    { "threshold3", "set threshold for 4th plane",   OFFSET(threshold[3]),   AV_OPT_TYPE_INT, {.i64=65535}, 0, 65535, FLAGS },
    { NULL }
};

DEFINE_NEIGHBOR_FILTER(deflate, "Apply deflate effect.");

#endif /* CONFIG_DEFLATE_FILTER */

#if CONFIG_INFLATE_FILTER

static const AVOption inflate_options[] = {
    { "threshold0", "set threshold for 1st plane",   OFFSET(threshold[0]),   AV_OPT_TYPE_INT, {.i64=65535}, 0, 65535, FLAGS },
    { "threshold1", "set threshold for 2nd plane",   OFFSET(threshold[1]),   AV_OPT_TYPE_INT, {.i64=65535}, 0, 65535, FLAGS },
    { "threshold2", "set threshold for 3rd plane",   OFFSET(threshold[2]),   AV_OPT_TYPE_INT, {.i64=65535}, 0, 65535, FLAGS },
    { "threshold3", "set threshold for 4th plane",   OFFSET(threshold[3]),   AV_OPT_TYPE_INT, {.i64=65535}, 0, 65535, FLAGS },
    { NULL }
};

DEFINE_NEIGHBOR_FILTER(inflate, "Apply inflate effect.");

#endif /* CONFIG_INFLATE_FILTER */
