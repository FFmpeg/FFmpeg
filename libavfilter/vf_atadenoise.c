/*
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

/**
 * @file
 * Adaptive Temporal Averaging Denoiser,
 * based on paper "Video Denoising Based on Adaptive Temporal Averaging" by
 * David Bartovčak and Miroslav Vrankić
 */

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"

#define FF_BUFQUEUE_SIZE 129
#include "bufferqueue.h"

#include "atadenoise.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

#define SIZE FF_BUFQUEUE_SIZE

typedef struct ATADenoiseContext {
    const AVClass *class;

    float fthra[4], fthrb[4];
    float sigma[4];
    int thra[4], thrb[4];
    int algorithm;

    int planes;
    int nb_planes;
    int planewidth[4];
    int planeheight[4];

    struct FFBufQueue q;
    void *data[4][SIZE];
    int linesize[4][SIZE];
    float weights[4][SIZE];
    int size, mid, radius;
    int available;

    int (*filter_slice)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);

    ATADenoiseDSPContext dsp;
} ATADenoiseContext;

#define OFFSET(x) offsetof(ATADenoiseContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM
#define VF AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption atadenoise_options[] = {
    { "0a", "set threshold A for 1st plane", OFFSET(fthra[0]), AV_OPT_TYPE_FLOAT, {.dbl=0.02}, 0, 0.3, FLAGS },
    { "0b", "set threshold B for 1st plane", OFFSET(fthrb[0]), AV_OPT_TYPE_FLOAT, {.dbl=0.04}, 0, 5.0, FLAGS },
    { "1a", "set threshold A for 2nd plane", OFFSET(fthra[1]), AV_OPT_TYPE_FLOAT, {.dbl=0.02}, 0, 0.3, FLAGS },
    { "1b", "set threshold B for 2nd plane", OFFSET(fthrb[1]), AV_OPT_TYPE_FLOAT, {.dbl=0.04}, 0, 5.0, FLAGS },
    { "2a", "set threshold A for 3rd plane", OFFSET(fthra[2]), AV_OPT_TYPE_FLOAT, {.dbl=0.02}, 0, 0.3, FLAGS },
    { "2b", "set threshold B for 3rd plane", OFFSET(fthrb[2]), AV_OPT_TYPE_FLOAT, {.dbl=0.04}, 0, 5.0, FLAGS },
    { "s",  "set how many frames to use",    OFFSET(size),     AV_OPT_TYPE_INT,   {.i64=9},   5, SIZE, VF    },
    { "p",  "set what planes to filter",     OFFSET(planes),   AV_OPT_TYPE_FLAGS, {.i64=7},    0, 15,  FLAGS },
    { "a",  "set variant of algorithm",      OFFSET(algorithm),AV_OPT_TYPE_INT,   {.i64=PARALLEL},  0, NB_ATAA-1, FLAGS, "a" },
    { "p",  "parallel",                      0,                AV_OPT_TYPE_CONST, {.i64=PARALLEL},  0, 0,         FLAGS, "a" },
    { "s",  "serial",                        0,                AV_OPT_TYPE_CONST, {.i64=SERIAL},    0, 0,         FLAGS, "a" },
    { "0s", "set sigma for 1st plane",       OFFSET(sigma[0]), AV_OPT_TYPE_FLOAT, {.dbl=INT16_MAX}, 0, INT16_MAX, FLAGS },
    { "1s", "set sigma for 2nd plane",       OFFSET(sigma[1]), AV_OPT_TYPE_FLOAT, {.dbl=INT16_MAX}, 0, INT16_MAX, FLAGS },
    { "2s", "set sigma for 3rd plane",       OFFSET(sigma[2]), AV_OPT_TYPE_FLOAT, {.dbl=INT16_MAX}, 0, INT16_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(atadenoise);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pixel_fmts[] = {
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
    AVFilterFormats *formats = ff_make_format_list(pixel_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, formats);
}

static av_cold int init(AVFilterContext *ctx)
{
    ATADenoiseContext *s = ctx->priv;

    if (!(s->size & 1)) {
        av_log(ctx, AV_LOG_WARNING, "size %d is invalid. Must be an odd value, setting it to %d.\n", s->size, s->size|1);
        s->size |= 1;
    }
    s->radius = s->size / 2;
    s->mid = s->radius;

    return 0;
}

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

#define WFILTER_ROW(type, name)                                             \
static void fweight_row##name(const uint8_t *ssrc, uint8_t *ddst,           \
                              const uint8_t *ssrcf[SIZE],                   \
                              int w, int mid, int size,                     \
                              int thra, int thrb, const float *weights)     \
{                                                                           \
    const type *src = (const type *)ssrc;                                   \
    const type **srcf = (const type **)ssrcf;                               \
    type *dst = (type *)ddst;                                               \
                                                                            \
    for (int x = 0; x < w; x++) {                                           \
       const int srcx = src[x];                                             \
       unsigned lsumdiff = 0, rsumdiff = 0;                                 \
       unsigned ldiff, rdiff;                                               \
       float sum = srcx;                                                    \
       float wsum = 1.f;                                                    \
       int l = 0, r = 0;                                                    \
       int srcjx, srcix;                                                    \
                                                                            \
       for (int j = mid - 1, i = mid + 1; j >= 0 && i < size; j--, i++) {   \
           srcjx = srcf[j][x];                                              \
                                                                            \
           ldiff = FFABS(srcx - srcjx);                                     \
           lsumdiff += ldiff;                                               \
           if (ldiff > thra ||                                              \
               lsumdiff > thrb)                                             \
               break;                                                       \
           l++;                                                             \
           sum += srcjx * weights[j];                                       \
           wsum += weights[j];                                              \
                                                                            \
           srcix = srcf[i][x];                                              \
                                                                            \
           rdiff = FFABS(srcx - srcix);                                     \
           rsumdiff += rdiff;                                               \
           if (rdiff > thra ||                                              \
               rsumdiff > thrb)                                             \
               break;                                                       \
           r++;                                                             \
           sum += srcix * weights[i];                                       \
           wsum += weights[i];                                              \
       }                                                                    \
                                                                            \
       dst[x] = lrintf(sum / wsum);                                         \
   }                                                                        \
}

WFILTER_ROW(uint8_t, 8)
WFILTER_ROW(uint16_t, 16)

#define WFILTER_ROW_SERIAL(type, name)                                      \
static void fweight_row##name##_serial(const uint8_t *ssrc, uint8_t *ddst,  \
                                       const uint8_t *ssrcf[SIZE],          \
                                       int w, int mid, int size,            \
                                       int thra, int thrb,                  \
                                       const float *weights)                \
{                                                                           \
    const type *src = (const type *)ssrc;                                   \
    const type **srcf = (const type **)ssrcf;                               \
    type *dst = (type *)ddst;                                               \
                                                                            \
    for (int x = 0; x < w; x++) {                                           \
       const int srcx = src[x];                                             \
       unsigned lsumdiff = 0, rsumdiff = 0;                                 \
       unsigned ldiff, rdiff;                                               \
       float sum = srcx;                                                    \
       float wsum = 1.f;                                                    \
       int l = 0, r = 0;                                                    \
       int srcjx, srcix;                                                    \
                                                                            \
       for (int j = mid - 1; j >= 0; j--) {                                 \
           srcjx = srcf[j][x];                                              \
                                                                            \
           ldiff = FFABS(srcx - srcjx);                                     \
           lsumdiff += ldiff;                                               \
           if (ldiff > thra ||                                              \
               lsumdiff > thrb)                                             \
               break;                                                       \
           l++;                                                             \
           sum += srcjx * weights[j];                                       \
           wsum += weights[j];                                              \
       }                                                                    \
                                                                            \
       for (int i = mid + 1; i < size; i++) {                               \
           srcix = srcf[i][x];                                              \
                                                                            \
           rdiff = FFABS(srcx - srcix);                                     \
           rsumdiff += rdiff;                                               \
           if (rdiff > thra ||                                              \
               rsumdiff > thrb)                                             \
               break;                                                       \
           r++;                                                             \
           sum += srcix * weights[i];                                       \
           wsum += weights[i];                                              \
       }                                                                    \
                                                                            \
       dst[x] = lrintf(sum / wsum);                                         \
   }                                                                        \
}

WFILTER_ROW_SERIAL(uint8_t, 8)
WFILTER_ROW_SERIAL(uint16_t, 16)

#define FILTER_ROW(type, name)                                              \
static void filter_row##name(const uint8_t *ssrc, uint8_t *ddst,            \
                             const uint8_t *ssrcf[SIZE],                    \
                             int w, int mid, int size,                      \
                             int thra, int thrb, const float *weights)      \
{                                                                           \
    const type *src = (const type *)ssrc;                                   \
    const type **srcf = (const type **)ssrcf;                               \
    type *dst = (type *)ddst;                                               \
                                                                            \
    for (int x = 0; x < w; x++) {                                           \
       const int srcx = src[x];                                             \
       unsigned lsumdiff = 0, rsumdiff = 0;                                 \
       unsigned ldiff, rdiff;                                               \
       unsigned sum = srcx;                                                 \
       int l = 0, r = 0;                                                    \
       int srcjx, srcix;                                                    \
                                                                            \
       for (int j = mid - 1, i = mid + 1; j >= 0 && i < size; j--, i++) {   \
           srcjx = srcf[j][x];                                              \
                                                                            \
           ldiff = FFABS(srcx - srcjx);                                     \
           lsumdiff += ldiff;                                               \
           if (ldiff > thra ||                                              \
               lsumdiff > thrb)                                             \
               break;                                                       \
           l++;                                                             \
           sum += srcjx;                                                    \
                                                                            \
           srcix = srcf[i][x];                                              \
                                                                            \
           rdiff = FFABS(srcx - srcix);                                     \
           rsumdiff += rdiff;                                               \
           if (rdiff > thra ||                                              \
               rsumdiff > thrb)                                             \
               break;                                                       \
           r++;                                                             \
           sum += srcix;                                                    \
       }                                                                    \
                                                                            \
       dst[x] = (sum + ((r + l + 1) >> 1)) / (r + l + 1);                   \
   }                                                                        \
}

FILTER_ROW(uint8_t, 8)
FILTER_ROW(uint16_t, 16)

#define FILTER_ROW_SERIAL(type, name)                                       \
static void filter_row##name##_serial(const uint8_t *ssrc, uint8_t *ddst,   \
                                      const uint8_t *ssrcf[SIZE],           \
                                      int w, int mid, int size,             \
                                      int thra, int thrb,                   \
                                      const float *weights)                 \
{                                                                           \
    const type *src = (const type *)ssrc;                                   \
    const type **srcf = (const type **)ssrcf;                               \
    type *dst = (type *)ddst;                                               \
                                                                            \
    for (int x = 0; x < w; x++) {                                           \
       const int srcx = src[x];                                             \
       unsigned lsumdiff = 0, rsumdiff = 0;                                 \
       unsigned ldiff, rdiff;                                               \
       unsigned sum = srcx;                                                 \
       int l = 0, r = 0;                                                    \
       int srcjx, srcix;                                                    \
                                                                            \
       for (int j = mid - 1; j >= 0; j--) {                                 \
           srcjx = srcf[j][x];                                              \
                                                                            \
           ldiff = FFABS(srcx - srcjx);                                     \
           lsumdiff += ldiff;                                               \
           if (ldiff > thra ||                                              \
               lsumdiff > thrb)                                             \
               break;                                                       \
           l++;                                                             \
           sum += srcjx;                                                    \
       }                                                                    \
                                                                            \
       for (int i = mid + 1; i < size; i++) {                               \
           srcix = srcf[i][x];                                              \
                                                                            \
           rdiff = FFABS(srcx - srcix);                                     \
           rsumdiff += rdiff;                                               \
           if (rdiff > thra ||                                              \
               rsumdiff > thrb)                                             \
               break;                                                       \
           r++;                                                             \
           sum += srcix;                                                    \
       }                                                                    \
                                                                            \
       dst[x] = (sum + ((r + l + 1) >> 1)) / (r + l + 1);                   \
   }                                                                        \
}

FILTER_ROW_SERIAL(uint8_t, 8)
FILTER_ROW_SERIAL(uint16_t, 16)

static int filter_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ATADenoiseContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int size = s->size;
    const int mid = s->mid;
    int p, y, i;

    for (p = 0; p < s->nb_planes; p++) {
        const float *weights = s->weights[p];
        const int h = s->planeheight[p];
        const int w = s->planewidth[p];
        const int slice_start = (h * jobnr) / nb_jobs;
        const int slice_end = (h * (jobnr+1)) / nb_jobs;
        const uint8_t *src = in->data[p] + slice_start * in->linesize[p];
        uint8_t *dst = out->data[p] + slice_start * out->linesize[p];
        const int thra = s->thra[p];
        const int thrb = s->thrb[p];
        const uint8_t **data = (const uint8_t **)s->data[p];
        const int *linesize = (const int *)s->linesize[p];
        const uint8_t *srcf[SIZE];

        if (!((1 << p) & s->planes)) {
            av_image_copy_plane(dst, out->linesize[p], src, in->linesize[p],
                                w, slice_end - slice_start);
            continue;
        }

        for (i = 0; i < size; i++)
            srcf[i] = data[i] + slice_start * linesize[i];

        for (y = slice_start; y < slice_end; y++) {
            s->dsp.filter_row[p](src, dst, srcf, w, mid, size, thra, thrb, weights);

            dst += out->linesize[p];
            src += in->linesize[p];

            for (i = 0; i < size; i++)
                srcf[i] += linesize[i];
        }
    }

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    AVFilterContext *ctx = inlink->dst;
    ATADenoiseContext *s = ctx->priv;
    int depth;

    s->nb_planes = desc->nb_components;

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planewidth[1]  = s->planewidth[2]  = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;

    depth = desc->comp[0].depth;
    s->filter_slice = filter_slice;

    for (int p = 0; p < s->nb_planes; p++) {
        if (depth == 8 && s->sigma[p] == INT16_MAX)
            s->dsp.filter_row[p] = s->algorithm == PARALLEL ? filter_row8 : filter_row8_serial;
        else if (s->sigma[p] == INT16_MAX)
            s->dsp.filter_row[p] = s->algorithm == PARALLEL ? filter_row16 : filter_row16_serial;
        else if (depth == 8 && s->sigma[p] < INT16_MAX)
            s->dsp.filter_row[p] = s->algorithm == PARALLEL ? fweight_row8 : fweight_row8_serial;
        else if (s->sigma[p] < INT16_MAX)
            s->dsp.filter_row[p] = s->algorithm == PARALLEL ? fweight_row16 : fweight_row16_serial;
    }

    s->thra[0] = s->fthra[0] * (1 << depth) - 1;
    s->thra[1] = s->fthra[1] * (1 << depth) - 1;
    s->thra[2] = s->fthra[2] * (1 << depth) - 1;
    s->thrb[0] = s->fthrb[0] * (1 << depth) - 1;
    s->thrb[1] = s->fthrb[1] * (1 << depth) - 1;
    s->thrb[2] = s->fthrb[2] * (1 << depth) - 1;

    for (int p = 0; p < s->nb_planes; p++) {
        float sigma = s->radius * s->sigma[p];

        s->weights[p][s->radius] = 1.f;
        for (int n = 1; n <= s->radius; n++) {
            s->weights[p][s->radius + n] =
            s->weights[p][s->radius - n] = expf(-0.5 * (n + 1) * (n + 1) / (sigma * sigma));
        }
    }

    if (ARCH_X86)
        ff_atadenoise_init_x86(&s->dsp, depth, s->algorithm, s->sigma);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *buf)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    ATADenoiseContext *s = ctx->priv;
    AVFrame *out, *in;
    int i;

    if (s->q.available != s->size) {
        if (s->q.available < s->mid) {
            for (i = 0; i < s->mid; i++) {
                out = av_frame_clone(buf);
                if (!out) {
                    av_frame_free(&buf);
                    return AVERROR(ENOMEM);
                }
                ff_bufqueue_add(ctx, &s->q, out);
            }
        }
        if (s->q.available < s->size) {
            ff_bufqueue_add(ctx, &s->q, buf);
            s->available++;
        }
        return 0;
    }

    in = ff_bufqueue_peek(&s->q, s->mid);

    if (!ctx->is_disabled) {
        ThreadData td;

        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&buf);
            return AVERROR(ENOMEM);
        }

        for (i = 0; i < s->size; i++) {
            AVFrame *frame = ff_bufqueue_peek(&s->q, i);

            s->data[0][i] = frame->data[0];
            s->data[1][i] = frame->data[1];
            s->data[2][i] = frame->data[2];
            s->linesize[0][i] = frame->linesize[0];
            s->linesize[1][i] = frame->linesize[1];
            s->linesize[2][i] = frame->linesize[2];
        }

        td.in = in; td.out = out;
        ctx->internal->execute(ctx, s->filter_slice, &td, NULL,
                               FFMIN3(s->planeheight[1],
                                      s->planeheight[2],
                                      ff_filter_get_nb_threads(ctx)));
        av_frame_copy_props(out, in);
    } else {
        out = av_frame_clone(in);
        if (!out) {
            av_frame_free(&buf);
            return AVERROR(ENOMEM);
        }
    }

    in = ff_bufqueue_get(&s->q);
    av_frame_free(&in);
    ff_bufqueue_add(ctx, &s->q, buf);

    return ff_filter_frame(outlink, out);
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    ATADenoiseContext *s = ctx->priv;
    int ret = 0;

    ret = ff_request_frame(ctx->inputs[0]);

    if (ret == AVERROR_EOF && !ctx->is_disabled && s->available) {
        AVFrame *buf = av_frame_clone(ff_bufqueue_peek(&s->q, s->available));
        if (!buf)
            return AVERROR(ENOMEM);

        ret = filter_frame(ctx->inputs[0], buf);
        s->available--;
    }

    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ATADenoiseContext *s = ctx->priv;

    ff_bufqueue_discard_all(&s->q);
}

static int process_command(AVFilterContext *ctx,
                           const char *cmd,
                           const char *arg,
                           char *res,
                           int res_len,
                           int flags)
{
    int ret = ff_filter_process_command(ctx, cmd, arg, res, res_len, flags);

    if (ret < 0)
        return ret;

    return config_input(ctx->inputs[0]);
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

AVFilter ff_vf_atadenoise = {
    .name          = "atadenoise",
    .description   = NULL_IF_CONFIG_SMALL("Apply an Adaptive Temporal Averaging Denoiser."),
    .priv_size     = sizeof(ATADenoiseContext),
    .priv_class    = &atadenoise_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = inputs,
    .outputs       = outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = process_command,
};
