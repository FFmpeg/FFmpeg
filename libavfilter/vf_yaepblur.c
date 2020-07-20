/*
 * Copyright (C) 2019 Leo Zhang <leozhang@qiyi.com>

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
 * yaep(yet another edge preserving) blur filter
 *
 * This implementation is based on an algorithm described in
 * "J. S. Lee, Digital image enhancement and noise filtering by use of local statistics, IEEE Trans. Pattern
 * Anal. Mach. Intell. PAMI-2, 1980."
 */

#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "avfilter.h"
#include "internal.h"

typedef struct YAEPContext {
    const AVClass *class;

    int planes;
    int radius;
    int sigma;

    int nb_planes;
    int planewidth[4];
    int planeheight[4];
    int depth;

    uint64_t *sat;        ///< summed area table
    uint64_t *square_sat; ///< square summed area table
    int sat_linesize;

    int (*pre_calculate_row)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
    int (*filter_slice     )(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} YAEPContext;

static av_cold void uninit(AVFilterContext *ctx)
{
    YAEPContext *s = ctx->priv;
    av_freep(&s->sat);
    av_freep(&s->square_sat);
}

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
        AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA444P12,
        AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
        AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
        AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
        AV_PIX_FMT_NONE
    };

    return ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
}

typedef struct ThreadData {
    int width;
    int height;
    int src_linesize;
    int dst_linesize;
    uint8_t *src;
    uint8_t *dst;
} ThreadData;

#define PRE_CALCULATE_ROW(type, name)                                    \
static int pre_calculate_row_##name(AVFilterContext *ctx, void *arg,     \
                                   int jobnr, int nb_jobs)               \
{                                                                        \
    ThreadData *td = arg;                                                \
    YAEPContext *s = ctx->priv;                                          \
                                                                         \
    const int width        = td->width;                                  \
    const int height       = td->height;                                 \
    const int linesize     = td->src_linesize / sizeof(type);            \
    const int sat_linesize = s->sat_linesize;                            \
                                                                         \
    const int starty = height * jobnr     / nb_jobs;                     \
    const int endy   = height * (jobnr+1) / nb_jobs;                     \
                                                                         \
    uint64_t *sat        = s->sat + (starty + 1) * sat_linesize;         \
    uint64_t *square_sat = s->square_sat + (starty + 1) * sat_linesize;  \
    const type *src      = (const type *)td->src + starty * linesize;    \
                                                                         \
    int x, y;                                                            \
                                                                         \
    for (y = starty; y < endy; y++) {                                    \
        for (x = 0; x < width; x++) {                                    \
            sat[x+1]        = sat[x] + src[x];                           \
            square_sat[x+1] = square_sat[x] + (uint64_t)src[x] * src[x]; \
        }                                                                \
        sat               += sat_linesize;                               \
        square_sat        += sat_linesize;                               \
        src               += linesize;                                   \
    }                                                                    \
                                                                         \
    return 0;                                                            \
}

PRE_CALCULATE_ROW(uint8_t,  byte)
PRE_CALCULATE_ROW(uint16_t, word)

static int pre_calculate_col(AVFilterContext *ctx, void *arg,
                             int jobnr, int nb_jobs)
{
    ThreadData *td = arg;
    YAEPContext *s = ctx->priv;

    const int width        = td->width;
    const int height       = td->height;
    const int sat_linesize = s->sat_linesize;

    const int startx = width * jobnr       / nb_jobs;
    const int endx   = width * (jobnr + 1) / nb_jobs;

    uint64_t *sat, *square_sat;
    int x, y;

    for (x = startx; x < endx; x++) {
        sat = s->sat + x + 1;
        square_sat = s->square_sat + x + 1;
        for (y = 0; y < height; y++) {
            *(sat+sat_linesize)        += *sat;
            *(square_sat+sat_linesize) += *square_sat;
            sat         += sat_linesize;
            square_sat  += sat_linesize;
        }
    }

    return 0;
}

#define FILTER_SLICE(type, name)                                                                          \
static int filter_slice_##name(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)                   \
{                                                                                                         \
    ThreadData *td = arg;                                                                                 \
    YAEPContext *s = ctx->priv;                                                                           \
                                                                                                          \
    const int width = td->width;                                                                          \
    const int height = td->height;                                                                        \
    const int src_linesize = td->src_linesize / sizeof(type);                                             \
    const int dst_linesize = td->dst_linesize / sizeof(type);                                             \
    const int sat_linesize = s->sat_linesize;                                                             \
    const int sigma = s->sigma;                                                                           \
    const int radius = s->radius;                                                                         \
                                                                                                          \
    uint64_t *sat = s->sat;                                                                               \
    uint64_t *square_sat = s->square_sat;                                                                 \
    const type *src = (const type *)td->src;                                                              \
    type *dst = (type *)td->dst;                                                                          \
                                                                                                          \
    const int starty = height * jobnr       / nb_jobs;                                                    \
    const int endy   = height * (jobnr + 1) / nb_jobs;                                                    \
                                                                                                          \
    int x, y;                                                                                             \
    int lower_x, higher_x;                                                                                \
    int lower_y, higher_y;                                                                                \
    int dist_y, count;                                                                                    \
    uint64_t sum, square_sum, mean, var;                                                                  \
                                                                                                          \
    for (y = starty; y < endy; y++) {                                                                     \
        lower_y  = y - radius     < 0      ? 0      : y - radius;                                         \
        higher_y = y + radius + 1 > height ? height : y + radius + 1;                                     \
        dist_y = higher_y - lower_y;                                                                      \
        for (x = 0; x < width; x++) {                                                                     \
            lower_x  = x - radius     < 0     ? 0     : x - radius;                                       \
            higher_x = x + radius + 1 > width ? width : x + radius + 1;                                   \
            count = dist_y * (higher_x - lower_x);                                                        \
            sum = sat[higher_y * sat_linesize + higher_x]                                                 \
                - sat[higher_y * sat_linesize + lower_x]                                                  \
                - sat[lower_y  * sat_linesize + higher_x]                                                 \
                + sat[lower_y  * sat_linesize + lower_x];                                                 \
            square_sum = square_sat[higher_y * sat_linesize + higher_x]                                   \
                       - square_sat[higher_y * sat_linesize + lower_x]                                    \
                       - square_sat[lower_y  * sat_linesize + higher_x]                                   \
                       + square_sat[lower_y  * sat_linesize + lower_x];                                   \
            mean = sum / count;                                                                           \
            var = (square_sum - sum * sum / count) / count;                                               \
            dst[y * dst_linesize + x] = (sigma * mean + var * src[y * src_linesize + x]) / (sigma + var); \
        }                                                                                                 \
    }                                                                                                     \
    return 0;                                                                                             \
}

FILTER_SLICE(uint8_t,  byte)
FILTER_SLICE(uint16_t, word)

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    YAEPContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int plane;
    const int nb_threads = ff_filter_get_nb_threads(ctx);
    ThreadData td;

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    for (plane = 0; plane < s->nb_planes; plane++) {
        if (!s->radius || !(s->planes & (1<<plane))) {
            if (out != in) {
                av_image_copy_plane(out->data[plane], out->linesize[plane],
                                    in->data[plane], in->linesize[plane],
                                    s->planewidth[plane] * ((s->depth + 7) / 8),
                                    s->planeheight[plane]);
            }
            continue;
        }

        td.width        = s->planewidth[plane];
        td.height       = s->planeheight[plane];
        td.src          = in->data[plane];
        td.src_linesize = in->linesize[plane];
        ctx->internal->execute(ctx, s->pre_calculate_row, &td, NULL, FFMIN(td.height, nb_threads));
        ctx->internal->execute(ctx, pre_calculate_col, &td, NULL, FFMIN(td.width,  nb_threads));

        td.dst          = out->data[plane];
        td.dst_linesize = out->linesize[plane];
        ctx->internal->execute(ctx, s->filter_slice, &td, NULL, FFMIN(td.height, nb_threads));
    }

    if (out != in)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static int config_input(AVFilterLink *inlink)
{
    YAEPContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    s->depth = desc->comp[0].depth;
    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    s->radius = FFMIN(s->radius, AV_CEIL_RSHIFT(FFMIN(inlink->w, inlink->h), 1));

    if (s->depth <= 8) {
        s->pre_calculate_row = pre_calculate_row_byte;
        s->filter_slice      = filter_slice_byte;
    } else {
        s->pre_calculate_row = pre_calculate_row_word;
        s->filter_slice      = filter_slice_word;
    }

    // padding one row on the top, and padding one col on the left, that is why + 1 below
    s->sat_linesize = inlink->w + 1;
    s->sat = av_mallocz_array(inlink->h + 1, s->sat_linesize*sizeof(*s->sat));
    if (!s->sat)
        return AVERROR(ENOMEM);

    s->square_sat = av_mallocz_array(inlink->h + 1, s->sat_linesize*sizeof(*s->square_sat));
    if (!s->square_sat)
        return AVERROR(ENOMEM);

    return 0;
}

static const AVFilterPad yaep_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad yaep_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

#define OFFSET(x) offsetof(YAEPContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption yaepblur_options[] = {
    { "radius", "set window radius",    OFFSET(radius), AV_OPT_TYPE_INT, {.i64=3},   0, INT_MAX, .flags=FLAGS },
    { "r"     , "set window radius",    OFFSET(radius), AV_OPT_TYPE_INT, {.i64=3},   0, INT_MAX, .flags=FLAGS },
    { "planes", "set planes to filter", OFFSET(planes), AV_OPT_TYPE_INT, {.i64=1},   0,     0xF, .flags=FLAGS },
    { "p",      "set planes to filter", OFFSET(planes), AV_OPT_TYPE_INT, {.i64=1},   0,     0xF, .flags=FLAGS },
    { "sigma",  "set blur strength",    OFFSET(sigma),  AV_OPT_TYPE_INT, {.i64=128}, 1, INT_MAX, .flags=FLAGS },
    { "s",      "set blur strength",    OFFSET(sigma),  AV_OPT_TYPE_INT, {.i64=128}, 1, INT_MAX, .flags=FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(yaepblur);

AVFilter ff_vf_yaepblur = {
    .name            = "yaepblur",
    .description     = NULL_IF_CONFIG_SMALL("Yet another edge preserving blur filter."),
    .priv_size       = sizeof(YAEPContext),
    .priv_class      = &yaepblur_class,
    .uninit          = uninit,
    .query_formats   = query_formats,
    .inputs          = yaep_inputs,
    .outputs         = yaep_outputs,
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
