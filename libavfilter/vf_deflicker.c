/*
 * Copyright (c) 2017 Paul B Mahol
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
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/qsort.h"
#include "avfilter.h"

#define FF_BUFQUEUE_SIZE 129
#include "bufferqueue.h"

#include "formats.h"
#include "internal.h"
#include "video.h"

#define SIZE FF_BUFQUEUE_SIZE

enum smooth_mode {
    ARITHMETIC_MEAN,
    GEOMETRIC_MEAN,
    HARMONIC_MEAN,
    QUADRATIC_MEAN,
    CUBIC_MEAN,
    POWER_MEAN,
    MEDIAN,
    NB_SMOOTH_MODE,
};

typedef struct DeflickerContext {
    const AVClass *class;

    int size;
    int mode;
    int bypass;

    int eof;
    int depth;
    int nb_planes;
    int planewidth[4];
    int planeheight[4];

    uint64_t *histogram;
    float luminance[SIZE];
    float sorted[SIZE];

    struct FFBufQueue q;
    int available;

    void (*get_factor)(AVFilterContext *ctx, float *f);
    float (*calc_avgy)(AVFilterContext *ctx, AVFrame *in);
    int (*deflicker)(AVFilterContext *ctx, const uint8_t *src, ptrdiff_t src_linesize,
                     uint8_t *dst, ptrdiff_t dst_linesize, int w, int h, float f);
} DeflickerContext;

#define OFFSET(x) offsetof(DeflickerContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption deflicker_options[] = {
    { "size",  "set how many frames to use",  OFFSET(size), AV_OPT_TYPE_INT, {.i64=5}, 2, SIZE, FLAGS },
    { "s",     "set how many frames to use",  OFFSET(size), AV_OPT_TYPE_INT, {.i64=5}, 2, SIZE, FLAGS },
    { "mode",  "set how to smooth luminance", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_SMOOTH_MODE-1, FLAGS, "mode" },
    { "m",     "set how to smooth luminance", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_SMOOTH_MODE-1, FLAGS, "mode" },
        { "am",      "arithmetic mean", 0, AV_OPT_TYPE_CONST, {.i64=ARITHMETIC_MEAN},  0, 0, FLAGS, "mode" },
        { "gm",      "geometric mean",  0, AV_OPT_TYPE_CONST, {.i64=GEOMETRIC_MEAN},   0, 0, FLAGS, "mode" },
        { "hm",      "harmonic mean",   0, AV_OPT_TYPE_CONST, {.i64=HARMONIC_MEAN},    0, 0, FLAGS, "mode" },
        { "qm",      "quadratic mean",  0, AV_OPT_TYPE_CONST, {.i64=QUADRATIC_MEAN},   0, 0, FLAGS, "mode" },
        { "cm",      "cubic mean",      0, AV_OPT_TYPE_CONST, {.i64=CUBIC_MEAN},       0, 0, FLAGS, "mode" },
        { "pm",      "power mean",      0, AV_OPT_TYPE_CONST, {.i64=POWER_MEAN},       0, 0, FLAGS, "mode" },
        { "median",  "median",          0, AV_OPT_TYPE_CONST, {.i64=MEDIAN},           0, 0, FLAGS, "mode" },
    { "bypass", "leave frames unchanged",  OFFSET(bypass), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(deflicker);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pixel_fmts[] = {
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY10,
        AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY16,
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
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *formats = ff_make_format_list(pixel_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, formats);
}

static int deflicker8(AVFilterContext *ctx,
                      const uint8_t *src, ptrdiff_t src_linesize,
                      uint8_t *dst, ptrdiff_t dst_linesize,
                      int w, int h, float f)
{
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            dst[x] = av_clip_uint8(src[x] * f);
        }

        dst += dst_linesize;
        src += src_linesize;
    }

    return 0;
}

static int deflicker16(AVFilterContext *ctx,
                       const uint8_t *ssrc, ptrdiff_t src_linesize,
                       uint8_t *ddst, ptrdiff_t dst_linesize,
                       int w, int h, float f)
{
    DeflickerContext *s = ctx->priv;
    const uint16_t *src = (const uint16_t *)ssrc;
    uint16_t *dst = (uint16_t *)ddst;
    const int max = (1 << s->depth) - 1;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            dst[x] = av_clip(src[x] * f, 0, max);
        }

        dst += dst_linesize / 2;
        src += src_linesize / 2;
    }

    return 0;
}

static float calc_avgy8(AVFilterContext *ctx, AVFrame *in)
{
    DeflickerContext *s = ctx->priv;
    const uint8_t *src = in->data[0];
    int64_t sum = 0;
    int y, x;

    memset(s->histogram, 0, (1 << s->depth) * sizeof(*s->histogram));

    for (y = 0; y < s->planeheight[0]; y++) {
        for (x = 0; x < s->planewidth[0]; x++) {
            s->histogram[src[x]]++;
        }
        src += in->linesize[0];
    }

    for (y = 0; y < 1 << s->depth; y++) {
        sum += s->histogram[y] * y;
    }

    return 1.0f * sum / (s->planeheight[0] * s->planewidth[0]);
}

static float calc_avgy16(AVFilterContext *ctx, AVFrame *in)
{
    DeflickerContext *s = ctx->priv;
    const uint16_t *src = (const uint16_t *)in->data[0];
    int64_t sum = 0;
    int y, x;

    memset(s->histogram, 0, (1 << s->depth) * sizeof(*s->histogram));

    for (y = 0; y < s->planeheight[0]; y++) {
        for (x = 0; x < s->planewidth[0]; x++) {
            s->histogram[src[x]]++;
        }
        src += in->linesize[0] / 2;
    }

    for (y = 0; y < 1 << s->depth; y++) {
        sum += s->histogram[y] * y;
    }

    return 1.0f * sum / (s->planeheight[0] * s->planewidth[0]);
}

static void get_am_factor(AVFilterContext *ctx, float *f)
{
    DeflickerContext *s = ctx->priv;
    int y;

    *f = 0.0f;

    for (y = 0; y < s->size; y++) {
        *f += s->luminance[y];
    }

    *f /= s->size;
    *f /= s->luminance[0];
}

static void get_gm_factor(AVFilterContext *ctx, float *f)
{
    DeflickerContext *s = ctx->priv;
    int y;

    *f = 1;

    for (y = 0; y < s->size; y++) {
        *f *= s->luminance[y];
    }

    *f = pow(*f, 1.0f / s->size);
    *f /= s->luminance[0];
}

static void get_hm_factor(AVFilterContext *ctx, float *f)
{
    DeflickerContext *s = ctx->priv;
    int y;

    *f = 0.0f;

    for (y = 0; y < s->size; y++) {
        *f += 1.0f / s->luminance[y];
    }

    *f = s->size / *f;
    *f /= s->luminance[0];
}

static void get_qm_factor(AVFilterContext *ctx, float *f)
{
    DeflickerContext *s = ctx->priv;
    int y;

    *f = 0.0f;

    for (y = 0; y < s->size; y++) {
        *f += s->luminance[y] * s->luminance[y];
    }

    *f /= s->size;
    *f  = sqrtf(*f);
    *f /= s->luminance[0];
}

static void get_cm_factor(AVFilterContext *ctx, float *f)
{
    DeflickerContext *s = ctx->priv;
    int y;

    *f = 0.0f;

    for (y = 0; y < s->size; y++) {
        *f += s->luminance[y] * s->luminance[y] * s->luminance[y];
    }

    *f /= s->size;
    *f  = cbrtf(*f);
    *f /= s->luminance[0];
}

static void get_pm_factor(AVFilterContext *ctx, float *f)
{
    DeflickerContext *s = ctx->priv;
    int y;

    *f = 0.0f;

    for (y = 0; y < s->size; y++) {
        *f += powf(s->luminance[y], s->size);
    }

    *f /= s->size;
    *f  = powf(*f, 1.0f / s->size);
    *f /= s->luminance[0];
}

static int comparef(const void *a, const void *b)
{
    const float *aa = a, *bb = b;
    return round(aa - bb);
}

static void get_median_factor(AVFilterContext *ctx, float *f)
{
    DeflickerContext *s = ctx->priv;

    memcpy(s->sorted, s->luminance, sizeof(s->sorted));
    AV_QSORT(s->sorted, s->size, float, comparef);

    *f = s->sorted[s->size >> 1] / s->luminance[0];
}

static int config_input(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    AVFilterContext *ctx = inlink->dst;
    DeflickerContext *s = ctx->priv;

    s->nb_planes = desc->nb_components;

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planewidth[1]  = s->planewidth[2]  = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;

    s->depth = desc->comp[0].depth;
    if (s->depth == 8) {
        s->deflicker = deflicker8;
        s->calc_avgy = calc_avgy8;
    } else {
        s->deflicker = deflicker16;
        s->calc_avgy = calc_avgy16;
    }

    s->histogram = av_calloc(1 << s->depth, sizeof(*s->histogram));
    if (!s->histogram)
        return AVERROR(ENOMEM);

    switch (s->mode) {
    case MEDIAN:          s->get_factor = get_median_factor; break;
    case ARITHMETIC_MEAN: s->get_factor = get_am_factor;     break;
    case GEOMETRIC_MEAN:  s->get_factor = get_gm_factor;     break;
    case HARMONIC_MEAN:   s->get_factor = get_hm_factor;     break;
    case QUADRATIC_MEAN:  s->get_factor = get_qm_factor;     break;
    case CUBIC_MEAN:      s->get_factor = get_cm_factor;     break;
    case POWER_MEAN:      s->get_factor = get_pm_factor;     break;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *buf)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    DeflickerContext *s = ctx->priv;
    AVDictionary **metadata;
    AVFrame *out, *in;
    float f;
    int y;

    if (s->q.available < s->size && !s->eof) {
        s->luminance[s->available] = s->calc_avgy(ctx, buf);
        ff_bufqueue_add(ctx, &s->q, buf);
        s->available++;
        return 0;
    }

    in = ff_bufqueue_peek(&s->q, 0);

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&buf);
        return AVERROR(ENOMEM);
    }

    s->get_factor(ctx, &f);
    if (!s->bypass)
        s->deflicker(ctx, in->data[0], in->linesize[0], out->data[0], out->linesize[0],
                     outlink->w, outlink->h, f);
    for (y = 1 - s->bypass; y < s->nb_planes; y++) {
        av_image_copy_plane(out->data[y], out->linesize[y],
                            in->data[y], in->linesize[y],
                            s->planewidth[y] * (1 + (s->depth > 8)), s->planeheight[y]);
    }

    av_frame_copy_props(out, in);
    metadata = &out->metadata;
    if (metadata) {
        uint8_t value[128];

        snprintf(value, sizeof(value), "%f", s->luminance[0]);
        av_dict_set(metadata, "lavfi.deflicker.luminance", value, 0);

        snprintf(value, sizeof(value), "%f", s->luminance[0] * f);
        av_dict_set(metadata, "lavfi.deflicker.new_luminance", value, 0);

        snprintf(value, sizeof(value), "%f", f - 1.0f);
        av_dict_set(metadata, "lavfi.deflicker.relative_change", value, 0);
    }

    in = ff_bufqueue_get(&s->q);
    av_frame_free(&in);
    memmove(&s->luminance[0], &s->luminance[1], sizeof(*s->luminance) * (s->size - 1));
    s->luminance[s->available - 1] = s->calc_avgy(ctx, buf);
    ff_bufqueue_add(ctx, &s->q, buf);

    return ff_filter_frame(outlink, out);
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    DeflickerContext *s = ctx->priv;
    int ret;

    ret = ff_request_frame(ctx->inputs[0]);
    if (ret == AVERROR_EOF && s->available > 0) {
        AVFrame *buf = av_frame_clone(ff_bufqueue_peek(&s->q, s->size - 1));
        if (!buf)
            return AVERROR(ENOMEM);

        s->eof = 1;
        ret = filter_frame(ctx->inputs[0], buf);
        s->available--;
    }

    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    DeflickerContext *s = ctx->priv;

    ff_bufqueue_discard_all(&s->q);
    av_freep(&s->histogram);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_vf_deflicker = {
    .name          = "deflicker",
    .description   = NULL_IF_CONFIG_SMALL("Remove temporal frame luminance variations."),
    .priv_size     = sizeof(DeflickerContext),
    .priv_class    = &deflicker_class,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = inputs,
    .outputs       = outputs,
};
