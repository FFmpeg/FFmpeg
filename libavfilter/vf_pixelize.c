/*
 * Copyright (c) 2022 Paul B Mahol
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

#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"

#include "avfilter.h"
#include "filters.h"
#include "video.h"

enum PixelizeModes {
    PIXELIZE_AVG,
    PIXELIZE_MIN,
    PIXELIZE_MAX,
    PIXELIZE_MODES
};

typedef struct PixelizeContext {
    const AVClass *class;

    int block_w[4], block_h[4];
    int mode;

    int depth;
    int planes;
    int nb_planes;
    int linesize[4];
    int planewidth[4];
    int planeheight[4];

    int log2_chroma_w;
    int log2_chroma_h;

    int (*pixelize[PIXELIZE_MODES])(const uint8_t *src, uint8_t *dst,
                                    ptrdiff_t src_linesize,
                                    ptrdiff_t dst_linesize,
                                    int w, int h);
} PixelizeContext;

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_GRAY9,
    AV_PIX_FMT_GRAY10,
    AV_PIX_FMT_GRAY12,
    AV_PIX_FMT_GRAY14,
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
    AV_PIX_FMT_YUVA420P,  AV_PIX_FMT_YUVA422P,   AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA444P12, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA422P16,
    AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA420P16,
    AV_PIX_FMT_GBRAP,     AV_PIX_FMT_GBRAP10,    AV_PIX_FMT_GBRAP12,    AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_NONE
};

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

#define PIXELIZE_AVG(name, type, stype) \
static int pixelize_avg##name(const uint8_t *ssrc, uint8_t *ddst, \
                              ptrdiff_t src_linesize, ptrdiff_t dst_linesize, \
                              int w, int h)       \
{                                                 \
    const type *src = (const type *)ssrc;         \
    type *dst = (type *)ddst;                     \
    stype sum = 0;                                \
    type fill;                                    \
    for (int y = 0; y < h; y++) {                 \
        for (int x = 0; x < w; x++)               \
            sum += src[x];                        \
                                                  \
        src += src_linesize / sizeof(type);       \
    }                                             \
                                                  \
    fill = sum / (w * h);                         \
                                                  \
    for (int y = 0; y < h; y++) {                 \
        for (int x = 0; x < w; x++)               \
            dst[x] = fill;                        \
                                                  \
        dst += dst_linesize / sizeof(type);       \
    }                                             \
                                                  \
    return 0;                                     \
}

#define PIXELIZE_MIN(name, type, stype) \
static int pixelize_min##name(const uint8_t *ssrc, uint8_t *ddst, \
                              ptrdiff_t src_linesize, ptrdiff_t dst_linesize, \
                              int w, int h)       \
{                                                 \
    const type *src = (const type *)ssrc;         \
    type *dst = (type *)ddst;                     \
    type fill = src[0];                           \
                                                  \
    for (int y = 0; y < h; y++) {                 \
        for (int x = 0; x < w; x++)               \
            fill = FFMIN(src[x], fill);           \
                                                  \
        src += src_linesize / sizeof(type);       \
    }                                             \
                                                  \
    for (int y = 0; y < h; y++) {                 \
        for (int x = 0; x < w; x++)               \
            dst[x] = fill;                        \
                                                  \
        dst += dst_linesize / sizeof(type);       \
    }                                             \
                                                  \
    return 0;                                     \
}

#define PIXELIZE_MAX(name, type, stype) \
static int pixelize_max##name(const uint8_t *ssrc, uint8_t *ddst, \
                              ptrdiff_t src_linesize, ptrdiff_t dst_linesize, \
                              int w, int h)       \
{                                                 \
    const type *src = (const type *)ssrc;         \
    type *dst = (type *)ddst;                     \
    type fill = src[0];                           \
                                                  \
    for (int y = 0; y < h; y++) {                 \
        for (int x = 0; x < w; x++)               \
            fill = FFMAX(src[x], fill);           \
                                                  \
        src += src_linesize / sizeof(type);       \
    }                                             \
                                                  \
    for (int y = 0; y < h; y++) {                 \
        for (int x = 0; x < w; x++)               \
            dst[x] = fill;                        \
                                                  \
        dst += dst_linesize / sizeof(type);       \
    }                                             \
                                                  \
    return 0;                                     \
}

PIXELIZE_AVG(8,  uint8_t,  unsigned)
PIXELIZE_AVG(16, uint16_t, uint64_t)
PIXELIZE_MIN(8,  uint8_t,  unsigned)
PIXELIZE_MIN(16, uint16_t, uint64_t)
PIXELIZE_MAX(8,  uint8_t,  unsigned)
PIXELIZE_MAX(16, uint16_t, uint64_t)

static int pixelize_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    PixelizeContext *s = ctx->priv;
    const int mode = s->mode;
    ThreadData *td = arg;
    AVFrame *out = td->out;
    AVFrame *in = td->in;

    for (int p = 0; p < s->nb_planes; p++) {
        const int wh = s->planeheight[p];
        const int h = (s->planeheight[p] + s->block_h[p] - 1) / s->block_h[p];
        const int w = (s->planewidth[p] + s->block_w[p] - 1) / s->block_w[p];
        const int wslice_start = (wh * jobnr) / nb_jobs;
        const int wslice_end = (wh * (jobnr+1)) / nb_jobs;
        const int slice_start = (h * jobnr) / nb_jobs;
        const int slice_end = (h * (jobnr+1)) / nb_jobs;
        const ptrdiff_t out_linesize = out->linesize[p];
        const ptrdiff_t in_linesize = in->linesize[p];
        const uint8_t *src = in->data[p];
        uint8_t *dst = out->data[p];

        if (!((1 << p) & s->planes)) {
            av_image_copy_plane(dst + wslice_start * out_linesize,
                                out_linesize,
                                src + wslice_start * in_linesize,
                                in_linesize,
                                s->linesize[p], wslice_end - wslice_start);
            continue;
        }

        for (int y = slice_start; y < slice_end; y++) {
            const int block_h = FFMIN(s->block_h[p], s->planeheight[p] - y * s->block_h[p]);
            for (int x = 0; x < w; x++) {
                const int block_w = FFMIN(s->block_w[p], s->planewidth[p] - x * s->block_w[p]);

                s->pixelize[mode](src + s->block_h[p] * y * in_linesize  +
                                  x * s->block_w[p] * (1 + (s->depth > 8)),
                                  dst + s->block_h[p] * y * out_linesize +
                                  x * s->block_w[p] * (1 + (s->depth > 8)),
                                  in_linesize, out_linesize, block_w, block_h);
            }
        }
    }

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    PixelizeContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    const AVPixFmtDescriptor *desc;
    int ret;

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

    s->log2_chroma_w = desc->log2_chroma_w;
    s->log2_chroma_h = desc->log2_chroma_h;

    s->pixelize[PIXELIZE_AVG] = s->depth <= 8 ? pixelize_avg8 : pixelize_avg16;
    s->pixelize[PIXELIZE_MIN] = s->depth <= 8 ? pixelize_min8 : pixelize_min16;
    s->pixelize[PIXELIZE_MAX] = s->depth <= 8 ? pixelize_max8 : pixelize_max16;

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    PixelizeContext *s = ctx->priv;
    ThreadData td;
    AVFrame *out;
    int ret;

    s->block_w[1] = s->block_w[2] = FFMAX(1, s->block_w[0] >> s->log2_chroma_w);
    s->block_w[3] = s->block_w[0] = s->block_w[1] << s->log2_chroma_w;

    s->block_h[1] = s->block_h[2] = FFMAX(1, s->block_h[0] >> s->log2_chroma_h);
    s->block_h[3] = s->block_h[0] = s->block_h[1] << s->log2_chroma_h;

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        ret = av_frame_copy_props(out, in);
        if (ret < 0) {
            av_frame_free(&out);
            goto fail;
        }
    }

    td.out = out;
    td.in = in;
    ff_filter_execute(ctx, pixelize_slice, &td, NULL,
                      FFMIN((s->planeheight[1] + s->block_h[1] - 1) / s->block_h[1],
                            ff_filter_get_nb_threads(ctx)));

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
fail:
    av_frame_free(&in);
    return ret;
}

#define OFFSET(x) offsetof(PixelizeContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_RUNTIME_PARAM)

static const AVOption pixelize_options[] = {
    { "width",  "set block width",  OFFSET(block_w[0]), AV_OPT_TYPE_INT, {.i64=16}, 1, 1024, FLAGS },
    { "w",      "set block width",  OFFSET(block_w[0]), AV_OPT_TYPE_INT, {.i64=16}, 1, 1024, FLAGS },
    { "height", "set block height", OFFSET(block_h[0]), AV_OPT_TYPE_INT, {.i64=16}, 1, 1024, FLAGS },
    { "h",      "set block height", OFFSET(block_h[0]), AV_OPT_TYPE_INT, {.i64=16}, 1, 1024, FLAGS },
    { "mode",  "set the pixelize mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=0}, 0, PIXELIZE_MODES-1, FLAGS, .unit = "mode" },
    { "m",     "set the pixelize mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=0}, 0, PIXELIZE_MODES-1, FLAGS, .unit = "mode" },
    {  "avg",    "average",  0,  AV_OPT_TYPE_CONST, {.i64=PIXELIZE_AVG}, 0,  0, FLAGS, .unit = "mode" },
    {  "min",    "minimum",  0,  AV_OPT_TYPE_CONST, {.i64=PIXELIZE_MIN}, 0,  0, FLAGS, .unit = "mode" },
    {  "max",    "maximum",  0,  AV_OPT_TYPE_CONST, {.i64=PIXELIZE_MAX}, 0,  0, FLAGS, .unit = "mode" },
    { "planes", "set what planes to filter", OFFSET(planes), AV_OPT_TYPE_FLAGS, {.i64=15}, 0, 15, FLAGS },
    { "p",      "set what planes to filter", OFFSET(planes), AV_OPT_TYPE_FLAGS, {.i64=15}, 0, 15, FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(pixelize);

static const AVFilterPad pixelize_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad pixelize_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const AVFilter ff_vf_pixelize = {
    .name          = "pixelize",
    .description   = NULL_IF_CONFIG_SMALL("Pixelize video."),
    .priv_size     = sizeof(PixelizeContext),
    .priv_class    = &pixelize_class,
    FILTER_INPUTS(pixelize_inputs),
    FILTER_OUTPUTS(pixelize_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
