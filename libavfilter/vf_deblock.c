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

/*
 * Based on paper: A Simple and Efficient Deblocking Algorithm for Low Bit-Rate Video Coding.
 */

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

enum FilterType { WEAK, STRONG, NB_FILTER };

typedef struct DeblockContext {
    const AVClass *class;
    const AVPixFmtDescriptor *desc;
    int filter;
    int block;
    int planes;
    float alpha;
    float beta;
    float gamma;
    float delta;

    int ath;
    int bth;
    int gth;
    int dth;
    int max;
    int depth;
    int bpc;
    int nb_planes;
    int planewidth[4];
    int planeheight[4];

    void (*deblockh)(uint8_t *dst, ptrdiff_t dst_linesize, int block,
                     int ath, int bth, int gth, int dth, int max);
    void (*deblockv)(uint8_t *dst, ptrdiff_t dst_linesize, int block,
                     int ath, int bth, int gth, int dth, int max);
} DeblockContext;

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pixel_fmts[] = {
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
    AVFilterFormats *formats = ff_make_format_list(pixel_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, formats);
}

#define WEAK_HFILTER(name, type, ldiv)                                              \
static void deblockh##name##_weak(uint8_t *dstp, ptrdiff_t dst_linesize, int block, \
                                  int ath, int bth, int gth, int dth, int max)      \
{                                                                                   \
    type *dst;                                                                      \
    int x;                                                                          \
                                                                                    \
    dst = (type *)dstp;                                                             \
    dst_linesize /= ldiv;                                                           \
                                                                                    \
    for (x = 0; x < block; x++) {                                                   \
        int delta = dst[x] - dst[x - dst_linesize];                                 \
        int A, B, C, D, a, b, c, d;                                                 \
                                                                                    \
        if (FFABS(delta) >= ath ||                                                  \
            FFABS(dst[x - 1 * dst_linesize] - dst[x - 2 * dst_linesize]) >= bth ||  \
            FFABS(dst[x + 0 * dst_linesize] - dst[x + 1 * dst_linesize]) >= gth)    \
            continue;                                                               \
                                                                                    \
        A = dst[x - 2 * dst_linesize];                                              \
        B = dst[x - 1 * dst_linesize];                                              \
        C = dst[x + 0 * dst_linesize];                                              \
        D = dst[x + 1 * dst_linesize];                                              \
                                                                                    \
        a = A + delta / 8;                                                          \
        b = B + delta / 2;                                                          \
        c = C - delta / 2;                                                          \
        d = D - delta / 8;                                                          \
                                                                                    \
        dst[x - 2 * dst_linesize] = av_clip(a, 0, max);                             \
        dst[x - 1 * dst_linesize] = av_clip(b, 0, max);                             \
        dst[x + 0 * dst_linesize] = av_clip(c, 0, max);                             \
        dst[x + 1 * dst_linesize] = av_clip(d, 0, max);                             \
    }                                                                               \
}

WEAK_HFILTER(8, uint8_t, 1)
WEAK_HFILTER(16, uint16_t, 2)

#define WEAK_VFILTER(name, type, ldiv)                                              \
static void deblockv##name##_weak(uint8_t *dstp, ptrdiff_t dst_linesize, int block, \
                                  int ath, int bth, int gth, int dth, int max)      \
{                                                                                   \
    type *dst;                                                                      \
    int y;                                                                          \
                                                                                    \
    dst = (type *)dstp;                                                             \
    dst_linesize /= ldiv;                                                           \
                                                                                    \
    for (y = 0; y < block; y++) {                                                   \
        int delta = dst[0] - dst[-1];                                               \
        int A, B, C, D, a, b, c, d;                                                 \
                                                                                    \
        if (FFABS(delta) >= ath ||                                                  \
            FFABS(dst[-1] - dst[-2]) >= bth ||                                      \
            FFABS(dst[0] - dst[1]) >= gth)                                          \
            continue;                                                               \
                                                                                    \
        A = dst[-2];                                                                \
        B = dst[-1];                                                                \
        C = dst[+0];                                                                \
        D = dst[+1];                                                                \
                                                                                    \
        a = A + delta / 8;                                                          \
        b = B + delta / 2;                                                          \
        c = C - delta / 2;                                                          \
        d = D - delta / 8;                                                          \
                                                                                    \
        dst[-2] = av_clip(a, 0, max);                                               \
        dst[-1] = av_clip(b, 0, max);                                               \
        dst[+0] = av_clip(c, 0, max);                                               \
        dst[+1] = av_clip(d, 0, max);                                               \
                                                                                    \
        dst += dst_linesize;                                                        \
    }                                                                               \
}

WEAK_VFILTER(8, uint8_t, 1)
WEAK_VFILTER(16, uint16_t, 2)

#define STRONG_HFILTER(name, type, ldiv)                                           \
static void deblockh##name##_strong(uint8_t *dstp, ptrdiff_t dst_linesize, int block,\
                                    int ath, int bth, int gth, int dth, int max)   \
{                                                                                  \
    type *dst;                                                                     \
    int x;                                                                         \
                                                                                   \
    dst = (type *)dstp;                                                            \
    dst_linesize /= ldiv;                                                          \
                                                                                   \
    for (x = 0; x < block; x++) {                                                  \
        int A, B, C, D, E, F, a, b, c, d, e, f;                                    \
        int delta = dst[x] - dst[x - dst_linesize];                                \
                                                                                   \
        if (FFABS(delta) >= ath ||                                                 \
            FFABS(dst[x - 1 * dst_linesize] - dst[x - 2 * dst_linesize]) >= bth || \
            FFABS(dst[x + 1 * dst_linesize] - dst[x + 2 * dst_linesize]) >= gth || \
            FFABS(dst[x + 0 * dst_linesize] - dst[x + 1 * dst_linesize]) >= dth)   \
            continue;                                                              \
                                                                                   \
        A = dst[x - 3 * dst_linesize];                                             \
        B = dst[x - 2 * dst_linesize];                                             \
        C = dst[x - 1 * dst_linesize];                                             \
        D = dst[x + 0 * dst_linesize];                                             \
        E = dst[x + 1 * dst_linesize];                                             \
        F = dst[x + 2 * dst_linesize];                                             \
                                                                                   \
        a = A + delta / 8;                                                         \
        b = B + delta / 4;                                                         \
        c = C + delta / 2;                                                         \
        d = D - delta / 2;                                                         \
        e = E - delta / 4;                                                         \
        f = F - delta / 8;                                                         \
                                                                                   \
        dst[x - 3 * dst_linesize] = av_clip(a, 0, max);                            \
        dst[x - 2 * dst_linesize] = av_clip(b, 0, max);                            \
        dst[x - 1 * dst_linesize] = av_clip(c, 0, max);                            \
        dst[x + 0 * dst_linesize] = av_clip(d, 0, max);                            \
        dst[x + 1 * dst_linesize] = av_clip(e, 0, max);                            \
        dst[x + 2 * dst_linesize] = av_clip(f, 0, max);                            \
    }                                                                              \
}

STRONG_HFILTER(8, uint8_t, 1)
STRONG_HFILTER(16, uint16_t, 2)

#define STRONG_VFILTER(name, type, ldiv)                                           \
static void deblockv##name##_strong(uint8_t *dstp, ptrdiff_t dst_linesize, int block,\
                                    int ath, int bth, int gth, int dth, int max)   \
{                                                                                  \
    type *dst;                                                                     \
    int y;                                                                         \
                                                                                   \
    dst = (type *)dstp;                                                            \
    dst_linesize /= ldiv;                                                          \
                                                                                   \
    for (y = 0; y < block; y++) {                                                  \
        int A, B, C, D, E, F, a, b, c, d, e, f;                                    \
        int delta = dst[0] - dst[-1];                                              \
                                                                                   \
        if (FFABS(delta) >= ath ||                                                 \
            FFABS(dst[-1] - dst[-2]) >= bth ||                                     \
            FFABS(dst[+1] - dst[+2]) >= gth ||                                     \
            FFABS(dst[+0] - dst[+1]) >= dth)                                       \
            continue;                                                              \
                                                                                   \
        A = dst[-3];                                                               \
        B = dst[-2];                                                               \
        C = dst[-1];                                                               \
        D = dst[+0];                                                               \
        E = dst[+1];                                                               \
        F = dst[+2];                                                               \
                                                                                   \
        a = A + delta / 8;                                                         \
        b = B + delta / 4;                                                         \
        c = C + delta / 2;                                                         \
        d = D - delta / 2;                                                         \
        e = E - delta / 4;                                                         \
        f = F - delta / 8;                                                         \
                                                                                   \
        dst[-3] = av_clip(a, 0, max);                                              \
        dst[-2] = av_clip(b, 0, max);                                              \
        dst[-1] = av_clip(c, 0, max);                                              \
        dst[+0] = av_clip(d, 0, max);                                              \
        dst[+1] = av_clip(e, 0, max);                                              \
        dst[+2] = av_clip(f, 0, max);                                              \
                                                                                   \
        dst += dst_linesize;                                                       \
    }                                                                              \
}

STRONG_VFILTER(8, uint8_t, 1)
STRONG_VFILTER(16, uint16_t, 2)

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    DeblockContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];

    s->desc = av_pix_fmt_desc_get(outlink->format);
    if (!s->desc)
        return AVERROR_BUG;
    s->nb_planes = av_pix_fmt_count_planes(outlink->format);
    s->depth = s->desc->comp[0].depth;
    s->bpc = (s->depth + 7) / 8;
    s->max = (1 << s->depth) - 1;
    s->ath = s->alpha * s->max;
    s->bth = s->beta  * s->max;
    s->gth = s->gamma * s->max;
    s->dth = s->delta * s->max;

    if (s->depth <= 8 && s->filter == WEAK) {
        s->deblockh = deblockh8_weak;
        s->deblockv = deblockv8_weak;
    } else if (s->depth > 8 && s->filter == WEAK) {
        s->deblockh = deblockh16_weak;
        s->deblockv = deblockv16_weak;
    }
    if (s->depth <= 8 && s->filter == STRONG) {
        s->deblockh = deblockh8_strong;
        s->deblockv = deblockv8_strong;
    } else if (s->depth > 8 && s->filter == STRONG) {
        s->deblockh = deblockh16_strong;
        s->deblockv = deblockv16_strong;
    }

    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, s->desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, s->desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    DeblockContext *s = ctx->priv;
    const int block = s->block;
    AVFrame *out;
    int plane, x, y;

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
        const int width = s->planewidth[plane];
        const int height = s->planeheight[plane];
        const uint8_t *src = (const uint8_t *)in->data[plane];
        uint8_t *dst = (uint8_t *)out->data[plane];

        if (in != out)
            av_image_copy_plane(dst, out->linesize[plane],
                                src, in->linesize[plane],
                                width * s->bpc, height);

        if (!((1 << plane) & s->planes))
            continue;

        for (x = block; x < width; x += block)
            s->deblockv(dst + x * s->bpc, out->linesize[plane],
                        FFMIN(block, height), s->ath, s->bth, s->gth, s->dth, s->max);

        for (y = block; y < height; y += block) {
            dst += out->linesize[plane] * block;

            s->deblockh(dst, out->linesize[plane],
                        FFMIN(block, width),
                        s->ath, s->bth, s->gth, s->dth, s->max);

            for (x = block; x < width; x += block) {
                s->deblockh(dst + x * s->bpc, out->linesize[plane],
                            FFMIN(block, width - x),
                            s->ath, s->bth, s->gth, s->dth, s->max);
                s->deblockv(dst + x * s->bpc, out->linesize[plane],
                            FFMIN(block, height - y),
                            s->ath, s->bth, s->gth, s->dth, s->max);
            }
        }
    }

    if (in != out)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    return config_output(ctx->outputs[0]);
}

#define OFFSET(x) offsetof(DeblockContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption deblock_options[] = {
    { "filter",    "set type of filter",          OFFSET(filter),    AV_OPT_TYPE_INT,   {.i64=STRONG},0, 1,  FLAGS, "filter" },
    { "weak",      0,                             0,                 AV_OPT_TYPE_CONST, {.i64=WEAK},  0, 0,  FLAGS, "filter" },
    { "strong",    0,                             0,                 AV_OPT_TYPE_CONST, {.i64=STRONG},0, 0,  FLAGS, "filter" },
    { "block",     "set size of block",           OFFSET(block),     AV_OPT_TYPE_INT,   {.i64=8},    4, 512, FLAGS },
    { "alpha",     "set 1st detection threshold", OFFSET(alpha),     AV_OPT_TYPE_FLOAT, {.dbl=.098}, 0,  1,  FLAGS },
    { "beta",      "set 2nd detection threshold", OFFSET(beta),      AV_OPT_TYPE_FLOAT, {.dbl=.05},  0,  1,  FLAGS },
    { "gamma",     "set 3rd detection threshold", OFFSET(gamma),     AV_OPT_TYPE_FLOAT, {.dbl=.05},  0,  1,  FLAGS },
    { "delta",     "set 4th detection threshold", OFFSET(delta),     AV_OPT_TYPE_FLOAT, {.dbl=.05},  0,  1,  FLAGS },
    { "planes",    "set planes to filter",        OFFSET(planes),    AV_OPT_TYPE_INT,   {.i64=15},   0, 15,  FLAGS },
    { NULL },
};

static const AVFilterPad inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .filter_frame   = filter_frame,
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

AVFILTER_DEFINE_CLASS(deblock);

AVFilter ff_vf_deblock = {
    .name          = "deblock",
    .description   = NULL_IF_CONFIG_SMALL("Deblock video."),
    .priv_size     = sizeof(DeblockContext),
    .priv_class    = &deblock_class,
    .query_formats = query_formats,
    .inputs        = inputs,
    .outputs       = outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
    .process_command = process_command,
};
