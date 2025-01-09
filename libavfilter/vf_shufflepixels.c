/*
 * Copyright (c) 2020 Paul B Mahol
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

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/imgutils.h"
#include "libavutil/lfg.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/random_seed.h"

#include "avfilter.h"
#include "filters.h"
#include "video.h"

typedef struct ShufflePixelsContext {
    const AVClass *class;

    int block_w, block_h;
    int mode;
    int direction;
    int64_t seed;

    int depth;
    int nb_planes;
    int linesize[4];
    int planewidth[4];
    int planeheight[4];

    int nb_blocks;

    uint8_t *used;
    int32_t *map;

    AVLFG c;

    int (*shuffle_pixels)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} ShufflePixelsContext;

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRAP10,
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
    AV_PIX_FMT_GBRAP16, AV_PIX_FMT_GBRAP,
    AV_PIX_FMT_YUV444P9, AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUVA444P10,
    AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV444P14, AV_PIX_FMT_YUV444P16, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_NONE
};

static void make_horizontal_map(AVFilterContext *ctx)
{
    ShufflePixelsContext *s = ctx->priv;
    const int nb_blocks = s->nb_blocks;
    AVLFG *c = &s->c;
    uint8_t *used = s->used;
    int32_t *map = s->map;

    for (int x = 0; x < s->planewidth[0];) {
        int rand = av_lfg_get(c) % nb_blocks;

        if (used[rand] == 0) {
            int width;

            if (s->direction) {
                width = FFMIN(s->block_w, s->planewidth[0] - x);
                map[rand * s->block_w] = x;
            } else {
                width = FFMIN(s->block_w, s->planewidth[0] - rand * s->block_w);
                map[x] = rand * s->block_w;
            }
            used[rand] = 1;

            if (s->direction) {
                for (int i = 1; i < width; i++) {
                    map[rand * s->block_w + i] = map[rand * s->block_w] + i;
                }
            } else {
                for (int i = 1; i < width; i++) {
                    map[x + i] = map[x] + i;
                }
            }

            x += width;
        }
    }
}

static void make_vertical_map(AVFilterContext *ctx)
{
    ShufflePixelsContext *s = ctx->priv;
    const int nb_blocks = s->nb_blocks;
    AVLFG *c = &s->c;
    uint8_t *used = s->used;
    int32_t *map = s->map;

    for (int y = 0; y < s->planeheight[0];) {
        int rand = av_lfg_get(c) % nb_blocks;

        if (used[rand] == 0) {
            int height;

            if (s->direction) {
                height = FFMIN(s->block_h, s->planeheight[0] - y);
                map[rand * s->block_h] = y;
            } else {
                height = FFMIN(s->block_h, s->planeheight[0] - rand * s->block_h);
                map[y] = rand * s->block_h;
            }
            used[rand] = 1;

            if (s->direction) {
                for (int i = 1; i < height; i++) {
                    map[rand * s->block_h + i] = map[rand * s->block_h] + i;
                }
            } else {
                for (int i = 1; i < height; i++) {
                    map[y + i] = map[y] + i;
                }
            }

            y += height;
        }
    }
}

static void make_block_map(AVFilterContext *ctx)
{
    ShufflePixelsContext *s = ctx->priv;
    const int nb_blocks = s->nb_blocks;
    int nb_blocks_w = s->planewidth[0]  / s->block_w;
    AVLFG *c = &s->c;
    uint8_t *used = s->used;
    int32_t *map = s->map;

    for (int i = 0; i < nb_blocks;) {
        int rand = av_lfg_get(c) % nb_blocks;

        if (used[rand] == 0) {
            int yin = i / nb_blocks_w;
            int xin = i % nb_blocks_w;
            int in = yin * s->block_h * s->planewidth[0] + xin * s->block_w;
            int yout = rand / nb_blocks_w;
            int xout = rand % nb_blocks_w;
            int out = yout * s->block_h * s->planewidth[0] + xout * s->block_w;

            if (s->direction) {
                map[out] = in;
            } else {
                map[in] = out;
            }
            used[rand] = 1;

            if (s->direction) {
                for (int y = 0; y < s->block_h; y++) {
                    for (int x = 0; x < s->block_w; x++) {
                        map[out + y * s->planewidth[0] + x] = map[out] + x + y * s->planewidth[0];
                    }
                }
            } else {
                for (int y = 0; y < s->block_h; y++) {
                    for (int x = 0; x < s->block_w; x++) {
                        map[in + y * s->planewidth[0] + x] = map[in] + x + y * s->planewidth[0];
                    }
                }
            }

            i++;
        }
    }
}

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;


#define SHUFFLE_HORIZONTAL(name, type)                                       \
static int shuffle_horizontal## name(AVFilterContext *ctx, void *arg,        \
                                     int jobnr, int nb_jobs)                 \
{                                                                            \
    ShufflePixelsContext *s = ctx->priv;                                     \
    ThreadData *td = arg;                                                    \
    AVFrame *in = td->in;                                                    \
    AVFrame *out = td->out;                                                  \
                                                                             \
    for (int p = 0; p < s->nb_planes; p++) {                                 \
        const int slice_start = (s->planeheight[p] * jobnr) / nb_jobs;       \
        const int slice_end = (s->planeheight[p] * (jobnr+1)) / nb_jobs;     \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]); \
        const type *src = (const type *)(in->data[p] +                       \
                                         slice_start * in->linesize[p]);     \
        const int32_t *map = s->map;                                         \
                                                                             \
        for (int y = slice_start; y < slice_end; y++) {                      \
            for (int x = 0; x < s->planewidth[p]; x++) {                     \
                dst[x] = src[map[x]];                                        \
            }                                                                \
                                                                             \
            dst += out->linesize[p] / sizeof(type);                          \
            src += in->linesize[p] / sizeof(type);                           \
        }                                                                    \
    }                                                                        \
                                                                             \
    return 0;                                                                \
}

SHUFFLE_HORIZONTAL(8, uint8_t)
SHUFFLE_HORIZONTAL(16, uint16_t)

#define SHUFFLE_VERTICAL(name, type)                                         \
static int shuffle_vertical## name(AVFilterContext *ctx, void *arg,          \
                            int jobnr, int nb_jobs)                          \
{                                                                            \
    ShufflePixelsContext *s = ctx->priv;                                     \
    ThreadData *td = arg;                                                    \
    AVFrame *in = td->in;                                                    \
    AVFrame *out = td->out;                                                  \
                                                                             \
    for (int p = 0; p < s->nb_planes; p++) {                                 \
        const int slice_start = (s->planeheight[p] * jobnr) / nb_jobs;       \
        const int slice_end = (s->planeheight[p] * (jobnr+1)) / nb_jobs;     \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]); \
        const int32_t *map = s->map;                                         \
                                                                             \
        for (int y = slice_start; y < slice_end; y++) {                      \
            const type *src = (const type *)(in->data[p] +                   \
                                             map[y] * in->linesize[p]);      \
                                                                             \
            memcpy(dst, src, s->linesize[p]);                                \
            dst += out->linesize[p] / sizeof(type);                          \
        }                                                                    \
    }                                                                        \
                                                                             \
    return 0;                                                                \
}

SHUFFLE_VERTICAL(8, uint8_t)
SHUFFLE_VERTICAL(16, uint16_t)

#define SHUFFLE_BLOCK(name, type)                                            \
static int shuffle_block## name(AVFilterContext *ctx, void *arg,             \
                         int jobnr, int nb_jobs)                             \
{                                                                            \
    ShufflePixelsContext *s = ctx->priv;                                     \
    ThreadData *td = arg;                                                    \
    AVFrame *in = td->in;                                                    \
    AVFrame *out = td->out;                                                  \
                                                                             \
    for (int p = 0; p < s->nb_planes; p++) {                                 \
        const int slice_start = (s->planeheight[p] * jobnr) / nb_jobs;       \
        const int slice_end = (s->planeheight[p] * (jobnr+1)) / nb_jobs;     \
        type *dst = (type *)(out->data[p] + slice_start * out->linesize[p]); \
        const type *src = (const type *)in->data[p];                         \
        const int32_t *map = s->map + slice_start * s->planewidth[p];        \
                                                                             \
        for (int y = slice_start; y < slice_end; y++) {                      \
            for (int x = 0; x < s->planewidth[p]; x++) {                     \
                int ymap = map[x] / s->planewidth[p];                        \
                int xmap = map[x] % s->planewidth[p];                        \
                                                                             \
                dst[x] = src[xmap + ymap * in->linesize[p] / sizeof(type)];  \
            }                                                                \
                                                                             \
            dst += out->linesize[p] / sizeof(type);                          \
            map += s->planewidth[p];                                         \
        }                                                                    \
    }                                                                        \
                                                                             \
    return 0;                                                                \
}

SHUFFLE_BLOCK(8, uint8_t)
SHUFFLE_BLOCK(16, uint16_t)

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    ShufflePixelsContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    const AVPixFmtDescriptor *desc;
    int ret;

    if (s->seed == -1)
        s->seed = av_get_random_seed();
    av_lfg_init(&s->c, s->seed);

    desc = av_pix_fmt_desc_get(outlink->format);
    if (!desc)
        return AVERROR_BUG;
    s->nb_planes = av_pix_fmt_count_planes(outlink->format);
    s->depth = desc->comp[0].depth;

    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->map = av_calloc(inlink->w * inlink->h, sizeof(*s->map));
    if (!s->map)
        return AVERROR(ENOMEM);

    switch (s->mode) {
    case 0:
        s->shuffle_pixels = s->depth <= 8 ? shuffle_horizontal8 : shuffle_horizontal16;
        s->nb_blocks = (s->planewidth[0] + s->block_w - 1) / s->block_w;
        break;
    case 1:
        s->shuffle_pixels = s->depth <= 8 ? shuffle_vertical8 : shuffle_vertical16;
        s->nb_blocks = (s->planeheight[0] + s->block_h - 1) / s->block_h;
        break;
    case 2:
        s->shuffle_pixels = s->depth <= 8 ? shuffle_block8 : shuffle_block16;
        s->nb_blocks = (s->planeheight[0] / s->block_h) *
                       (s->planewidth[0]  / s->block_w);
        break;
    default:
        av_assert0(0);
    }

    s->used = av_calloc(s->nb_blocks, sizeof(*s->used));
    if (!s->used)
        return AVERROR(ENOMEM);

    switch (s->mode) {
    case 0:
        make_horizontal_map(ctx);
        break;
    case 1:
        make_vertical_map(ctx);
        break;
    case 2:
        make_block_map(ctx);
        break;
    default:
        av_assert0(0);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    ShufflePixelsContext *s = ctx->priv;
    AVFrame *out = ff_get_video_buffer(ctx->outputs[0], in->width, in->height);
    ThreadData td;
    int ret;

    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = av_frame_copy_props(out, in);
    if (ret < 0) {
        av_frame_free(&out);
        goto fail;
    }

    td.out = out;
    td.in = in;
    ff_filter_execute(ctx, s->shuffle_pixels, &td, NULL,
                      FFMIN(s->planeheight[1], ff_filter_get_nb_threads(ctx)));

    av_frame_free(&in);
    return ff_filter_frame(ctx->outputs[0], out);
fail:
    av_frame_free(&in);
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ShufflePixelsContext *s = ctx->priv;

    av_freep(&s->map);
    av_freep(&s->used);
}

#define OFFSET(x) offsetof(ShufflePixelsContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption shufflepixels_options[] = {
    { "direction",  "set shuffle direction",  OFFSET(direction), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS, .unit = "dir" },
    { "d",          "set shuffle direction",  OFFSET(direction), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS, .unit = "dir" },
    {  "forward",    0,  0,  AV_OPT_TYPE_CONST,     {.i64=0}, 0,  0, FLAGS, .unit = "dir" },
    {  "inverse",    0,  0,  AV_OPT_TYPE_CONST,     {.i64=1}, 0,  0, FLAGS, .unit = "dir" },
    { "mode",       "set shuffle mode",  OFFSET(mode), AV_OPT_TYPE_INT, {.i64=0}, 0, 2, FLAGS, .unit = "mode" },
    { "m",          "set shuffle mode",  OFFSET(mode), AV_OPT_TYPE_INT, {.i64=0}, 0, 2, FLAGS, .unit = "mode" },
    {  "horizontal",  0,  0,  AV_OPT_TYPE_CONST,     {.i64=0}, 0,  0, FLAGS, .unit = "mode" },
    {  "vertical",    0,  0,  AV_OPT_TYPE_CONST,     {.i64=1}, 0,  0, FLAGS, .unit = "mode" },
    {  "block",       0,  0,  AV_OPT_TYPE_CONST,     {.i64=2}, 0,  0, FLAGS, .unit = "mode" },
    { "width",      "set block width",  OFFSET(block_w), AV_OPT_TYPE_INT, {.i64=10}, 1, 8000, FLAGS },
    { "w",          "set block width",  OFFSET(block_w), AV_OPT_TYPE_INT, {.i64=10}, 1, 8000, FLAGS },
    { "height",     "set block height", OFFSET(block_h), AV_OPT_TYPE_INT, {.i64=10}, 1, 8000, FLAGS },
    { "h",          "set block height", OFFSET(block_h), AV_OPT_TYPE_INT, {.i64=10}, 1, 8000, FLAGS },
    { "seed",       "set random seed",  OFFSET(seed),   AV_OPT_TYPE_INT64, {.i64=-1}, -1, UINT_MAX, FLAGS },
    { "s",          "set random seed",  OFFSET(seed),   AV_OPT_TYPE_INT64, {.i64=-1}, -1, UINT_MAX, FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(shufflepixels);

static const AVFilterPad shufflepixels_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad shufflepixels_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const FFFilter ff_vf_shufflepixels = {
    .p.name        = "shufflepixels",
    .p.description = NULL_IF_CONFIG_SMALL("Shuffle video pixels."),
    .p.priv_class  = &shufflepixels_class,
    .p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .priv_size     = sizeof(ShufflePixelsContext),
    .uninit        = uninit,
    FILTER_INPUTS(shufflepixels_inputs),
    FILTER_OUTPUTS(shufflepixels_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
};
