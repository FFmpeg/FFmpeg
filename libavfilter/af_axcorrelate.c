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

#include "libavutil/audio_fifo.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "internal.h"

typedef struct AudioXCorrelateContext {
    const AVClass *class;

    int size;
    int algo;
    int64_t pts;

    AVAudioFifo *fifo[2];
    AVFrame *cache[2];
    AVFrame *mean_sum[2];
    AVFrame *num_sum;
    AVFrame *den_sum[2];
    int used;
    int eof;

    int (*xcorrelate)(AVFilterContext *ctx, AVFrame *out, int available);
} AudioXCorrelateContext;

#define MEAN_SUM(suffix, type, zero)           \
static type mean_sum_##suffix(const type *in,  \
                              int size)        \
{                                              \
    type mean_sum = zero;                      \
                                               \
    for (int i = 0; i < size; i++)             \
        mean_sum += in[i];                     \
                                               \
    return mean_sum;                           \
}

MEAN_SUM(f, float,  0.f)
MEAN_SUM(d, double, 0.0)

#define SQUARE_SUM(suffix, type, zero)         \
static type square_sum_##suffix(const type *x, \
                        const type *y,         \
                        int size)              \
{                                              \
    type square_sum = zero;                    \
                                               \
    for (int i = 0; i < size; i++)             \
        square_sum += x[i] * y[i];             \
                                               \
    return square_sum;                         \
}

SQUARE_SUM(f, float,  0.f)
SQUARE_SUM(d, double, 0.0)

#define XCORRELATE(suffix, type, zero, small, sqrtfun)\
static type xcorrelate_##suffix(const type *x,       \
                                const type *y,       \
                                type sumx,           \
                                type sumy, int size) \
{                                                    \
    const type xm = sumx / size, ym = sumy / size;   \
    type num = zero, den, den0 = zero, den1 = zero;  \
                                                     \
    for (int i = 0; i < size; i++) {                 \
        type xd = x[i] - xm;                         \
        type yd = y[i] - ym;                         \
                                                     \
        num += xd * yd;                              \
        den0 += xd * xd;                             \
        den1 += yd * yd;                             \
    }                                                \
                                                     \
    num /= size;                                     \
    den  = sqrtfun((den0 * den1) / size / size);     \
                                                     \
    return den <= small ? zero : num / den;          \
}

XCORRELATE(f, float,  0.f, 1e-6f, sqrtf)
XCORRELATE(d, double, 0.0, 1e-9,  sqrt)

#define XCORRELATE_SLOW(suffix, type)                     \
static int xcorrelate_slow_##suffix(AVFilterContext *ctx, \
                           AVFrame *out, int available)   \
{                                                         \
    AudioXCorrelateContext *s = ctx->priv;                \
    const int size = s->size;                             \
    int used;                                             \
                                                          \
    for (int ch = 0; ch < out->ch_layout.nb_channels; ch++) {         \
        const type *x = (const type *)s->cache[0]->extended_data[ch]; \
        const type *y = (const type *)s->cache[1]->extended_data[ch]; \
        type *sumx = (type *)s->mean_sum[0]->extended_data[ch];       \
        type *sumy = (type *)s->mean_sum[1]->extended_data[ch];       \
        type *dst = (type *)out->extended_data[ch];                   \
                                                          \
        used = s->used;                                   \
        if (!used) {                                      \
            sumx[0] = mean_sum_##suffix(x, size);         \
            sumy[0] = mean_sum_##suffix(y, size);         \
            used = 1;                                     \
        }                                                 \
                                                          \
        for (int n = 0; n < out->nb_samples; n++) {       \
            const int idx = n + size;                     \
                                                          \
            dst[n] = xcorrelate_##suffix(x + n, y + n,    \
                                         sumx[0], sumy[0],\
                                         size);           \
                                                          \
            sumx[0] -= x[n];     \
            sumx[0] += x[idx];   \
            sumy[0] -= y[n];     \
            sumy[0] += y[idx];   \
        }                        \
    }                            \
                                 \
    return used;                 \
}

XCORRELATE_SLOW(f, float)
XCORRELATE_SLOW(d, double)

#define clipf(x) (av_clipf(x, -1.f, 1.f))
#define clipd(x) (av_clipd(x, -1.0, 1.0))

#define XCORRELATE_FAST(suffix, type, zero, small, sqrtfun, CLIP)              \
static int xcorrelate_fast_##suffix(AVFilterContext *ctx, AVFrame *out,        \
                                    int available)                             \
{                                                                              \
    AudioXCorrelateContext *s = ctx->priv;                                     \
    const int size = s->size;                                                  \
    int used;                                                                  \
                                                                               \
    for (int ch = 0; ch < out->ch_layout.nb_channels; ch++) {                  \
        const type *x = (const type *)s->cache[0]->extended_data[ch];          \
        const type *y = (const type *)s->cache[1]->extended_data[ch];          \
        type *num_sum = (type *)s->num_sum->extended_data[ch];                 \
        type *den_sumx = (type *)s->den_sum[0]->extended_data[ch];             \
        type *den_sumy = (type *)s->den_sum[1]->extended_data[ch];             \
        type *dst = (type *)out->extended_data[ch];                            \
                                                                               \
        used = s->used;                                                        \
        if (!used) {                                                           \
            num_sum[0]  = square_sum_##suffix(x, y, size);                     \
            den_sumx[0] = square_sum_##suffix(x, x, size);                     \
            den_sumy[0] = square_sum_##suffix(y, y, size);                     \
            used = 1;                                                          \
        }                                                                      \
                                                                               \
        for (int n = 0; n < out->nb_samples; n++) {                            \
            const int idx = n + size;                                          \
            type num, den;                                                     \
                                                                               \
            num = num_sum[0] / size;                                           \
            den = sqrtfun((den_sumx[0] * den_sumy[0]) / size / size);          \
                                                                               \
            dst[n] = den <= small ? zero : CLIP(num / den);                    \
                                                                               \
            num_sum[0]  -= x[n] * y[n];                                        \
            num_sum[0]  += x[idx] * y[idx];                                    \
            den_sumx[0] -= x[n] * x[n];                                        \
            den_sumx[0] += x[idx] * x[idx];                                    \
            den_sumx[0]  = FFMAX(den_sumx[0], zero);                           \
            den_sumy[0] -= y[n] * y[n];                                        \
            den_sumy[0] += y[idx] * y[idx];                                    \
            den_sumy[0]  = FFMAX(den_sumy[0], zero);                           \
        }                                                                      \
    }                                                                          \
                                                                               \
    return used;                                                               \
}

XCORRELATE_FAST(f, float,  0.f, 1e-6f, sqrtf, clipf)
XCORRELATE_FAST(d, double, 0.0, 1e-9,  sqrt, clipd)

#define XCORRELATE_BEST(suffix, type, zero, small, sqrtfun, FMAX, CLIP)        \
static int xcorrelate_best_##suffix(AVFilterContext *ctx, AVFrame *out,        \
                                    int available)                             \
{                                                                              \
    AudioXCorrelateContext *s = ctx->priv;                                     \
    const int size = s->size;                                                  \
    int used;                                                                  \
                                                                               \
    for (int ch = 0; ch < out->ch_layout.nb_channels; ch++) {                  \
        const type *x = (const type *)s->cache[0]->extended_data[ch];          \
        const type *y = (const type *)s->cache[1]->extended_data[ch];          \
        type *mean_sumx = (type *)s->mean_sum[0]->extended_data[ch];           \
        type *mean_sumy = (type *)s->mean_sum[1]->extended_data[ch];           \
        type *num_sum = (type *)s->num_sum->extended_data[ch];                 \
        type *den_sumx = (type *)s->den_sum[0]->extended_data[ch];             \
        type *den_sumy = (type *)s->den_sum[1]->extended_data[ch];             \
        type *dst = (type *)out->extended_data[ch];                            \
                                                                               \
        used = s->used;                                                        \
        if (!used) {                                                           \
            num_sum[0]  = square_sum_##suffix(x, y, size);                     \
            den_sumx[0] = square_sum_##suffix(x, x, size);                     \
            den_sumy[0] = square_sum_##suffix(y, y, size);                     \
            mean_sumx[0] = mean_sum_##suffix(x, size);                         \
            mean_sumy[0] = mean_sum_##suffix(y, size);                         \
            used = 1;                                                          \
        }                                                                      \
                                                                               \
        for (int n = 0; n < out->nb_samples; n++) {                            \
            const int idx = n + size;                                          \
            type num, den, xm, ym;                                             \
                                                                               \
            xm = mean_sumx[0] / size;                                          \
            ym = mean_sumy[0] / size;                                          \
            num = num_sum[0] - size * xm * ym;                                 \
            den = sqrtfun(FMAX(den_sumx[0] - size * xm * xm, zero)) *          \
                  sqrtfun(FMAX(den_sumy[0] - size * ym * ym, zero));           \
                                                                               \
            dst[n] = den <= small ? zero : CLIP(num / den);                    \
                                                                               \
            mean_sumx[0]-= x[n];                                               \
            mean_sumx[0]+= x[idx];                                             \
            mean_sumy[0]-= y[n];                                               \
            mean_sumy[0]+= y[idx];                                             \
            num_sum[0]  -= x[n] * y[n];                                        \
            num_sum[0]  += x[idx] * y[idx];                                    \
            den_sumx[0] -= x[n] * x[n];                                        \
            den_sumx[0] += x[idx] * x[idx];                                    \
            den_sumx[0]  = FMAX(den_sumx[0], zero);                            \
            den_sumy[0] -= y[n] * y[n];                                        \
            den_sumy[0] += y[idx] * y[idx];                                    \
            den_sumy[0]  = FMAX(den_sumy[0], zero);                            \
        }                                                                      \
    }                                                                          \
                                                                               \
    return used;                                                               \
}

XCORRELATE_BEST(f, float,  0.f, 1e-6f, sqrtf, fmaxf, clipf)
XCORRELATE_BEST(d, double, 0.0, 1e-9,  sqrt, fmax, clipd)

static int activate(AVFilterContext *ctx)
{
    AudioXCorrelateContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *frame = NULL;
    int ret, status;
    int available;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK_ALL(outlink, ctx);

    for (int i = 0; i < 2 && !s->eof; i++) {
        ret = ff_inlink_consume_frame(ctx->inputs[i], &frame);
        if (ret > 0) {
            if (s->pts == AV_NOPTS_VALUE)
                s->pts = frame->pts;
            ret = av_audio_fifo_write(s->fifo[i], (void **)frame->extended_data,
                                      frame->nb_samples);
            av_frame_free(&frame);
            if (ret < 0)
                return ret;
        }
    }

    available = FFMIN(av_audio_fifo_size(s->fifo[0]), av_audio_fifo_size(s->fifo[1]));
    if (available > s->size) {
        const int out_samples = available - s->size;
        AVFrame *out;

        if (!s->cache[0] || s->cache[0]->nb_samples < available) {
            av_frame_free(&s->cache[0]);
            s->cache[0] = ff_get_audio_buffer(outlink, available);
            if (!s->cache[0])
                return AVERROR(ENOMEM);
        }

        if (!s->cache[1] || s->cache[1]->nb_samples < available) {
            av_frame_free(&s->cache[1]);
            s->cache[1] = ff_get_audio_buffer(outlink, available);
            if (!s->cache[1])
                return AVERROR(ENOMEM);
        }

        ret = av_audio_fifo_peek(s->fifo[0], (void **)s->cache[0]->extended_data, available);
        if (ret < 0)
            return ret;

        ret = av_audio_fifo_peek(s->fifo[1], (void **)s->cache[1]->extended_data, available);
        if (ret < 0)
            return ret;

        out = ff_get_audio_buffer(outlink, out_samples);
        if (!out)
            return AVERROR(ENOMEM);

        s->used = s->xcorrelate(ctx, out, available);

        out->pts = s->pts;
        s->pts += out_samples;

        av_audio_fifo_drain(s->fifo[0], out_samples);
        av_audio_fifo_drain(s->fifo[1], out_samples);

        return ff_filter_frame(outlink, out);
    }

    for (int i = 0; i < 2 && !s->eof; i++) {
        if (ff_inlink_acknowledge_status(ctx->inputs[i], &status, &pts)) {
            AVFrame *silence = ff_get_audio_buffer(outlink, s->size);

            s->eof = 1;
            if (!silence)
                return AVERROR(ENOMEM);

            av_audio_fifo_write(s->fifo[0], (void **)silence->extended_data,
                                silence->nb_samples);

            av_audio_fifo_write(s->fifo[1], (void **)silence->extended_data,
                                silence->nb_samples);

            av_frame_free(&silence);
        }
    }

    if (s->eof &&
        (av_audio_fifo_size(s->fifo[0]) <= s->size ||
         av_audio_fifo_size(s->fifo[1]) <= s->size)) {
        ff_outlink_set_status(outlink, AVERROR_EOF, s->pts);
        return 0;
    }

    if ((av_audio_fifo_size(s->fifo[0]) > s->size &&
         av_audio_fifo_size(s->fifo[1]) > s->size) || s->eof) {
        ff_filter_set_ready(ctx, 10);
        return 0;
    }

    if (ff_outlink_frame_wanted(outlink) && !s->eof) {
        for (int i = 0; i < 2; i++) {
            if (av_audio_fifo_size(s->fifo[i]) > s->size)
                continue;
            ff_inlink_request_frame(ctx->inputs[i]);
            return 0;
        }
    }

    return FFERROR_NOT_READY;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioXCorrelateContext *s = ctx->priv;

    s->pts = AV_NOPTS_VALUE;

    s->fifo[0] = av_audio_fifo_alloc(outlink->format, outlink->ch_layout.nb_channels, s->size);
    s->fifo[1] = av_audio_fifo_alloc(outlink->format, outlink->ch_layout.nb_channels, s->size);
    if (!s->fifo[0] || !s->fifo[1])
        return AVERROR(ENOMEM);

    s->mean_sum[0] = ff_get_audio_buffer(outlink, 1);
    s->mean_sum[1] = ff_get_audio_buffer(outlink, 1);
    s->num_sum = ff_get_audio_buffer(outlink, 1);
    s->den_sum[0] = ff_get_audio_buffer(outlink, 1);
    s->den_sum[1] = ff_get_audio_buffer(outlink, 1);
    if (!s->mean_sum[0] || !s->mean_sum[1] || !s->num_sum ||
        !s->den_sum[0] || !s->den_sum[1])
        return AVERROR(ENOMEM);

    switch (s->algo) {
    case 0: s->xcorrelate = xcorrelate_slow_f; break;
    case 1: s->xcorrelate = xcorrelate_fast_f; break;
    case 2: s->xcorrelate = xcorrelate_best_f; break;
    }

    if (outlink->format == AV_SAMPLE_FMT_DBLP) {
        switch (s->algo) {
        case 0: s->xcorrelate = xcorrelate_slow_d; break;
        case 1: s->xcorrelate = xcorrelate_fast_d; break;
        case 2: s->xcorrelate = xcorrelate_best_d; break;
        }
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioXCorrelateContext *s = ctx->priv;

    av_audio_fifo_free(s->fifo[0]);
    av_audio_fifo_free(s->fifo[1]);
    av_frame_free(&s->cache[0]);
    av_frame_free(&s->cache[1]);
    av_frame_free(&s->mean_sum[0]);
    av_frame_free(&s->mean_sum[1]);
    av_frame_free(&s->num_sum);
    av_frame_free(&s->den_sum[0]);
    av_frame_free(&s->den_sum[1]);
}

static const AVFilterPad inputs[] = {
    {
        .name = "axcorrelate0",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    {
        .name = "axcorrelate1",
        .type = AVMEDIA_TYPE_AUDIO,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
};

#define AF AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define OFFSET(x) offsetof(AudioXCorrelateContext, x)

static const AVOption axcorrelate_options[] = {
    { "size", "set the segment size", OFFSET(size), AV_OPT_TYPE_INT, {.i64=256}, 2, 131072, AF },
    { "algo", "set the algorithm",    OFFSET(algo), AV_OPT_TYPE_INT, {.i64=2},   0,      2, AF, .unit = "algo" },
    { "slow", "slow algorithm",   0,            AV_OPT_TYPE_CONST, {.i64=0},   0,      0, AF, .unit = "algo" },
    { "fast", "fast algorithm",   0,            AV_OPT_TYPE_CONST, {.i64=1},   0,      0, AF, .unit = "algo" },
    { "best", "best algorithm",   0,            AV_OPT_TYPE_CONST, {.i64=2},   0,      0, AF, .unit = "algo" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(axcorrelate);

const AVFilter ff_af_axcorrelate = {
    .name           = "axcorrelate",
    .description    = NULL_IF_CONFIG_SMALL("Cross-correlate two audio streams."),
    .priv_size      = sizeof(AudioXCorrelateContext),
    .priv_class     = &axcorrelate_class,
    .activate       = activate,
    .uninit         = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_SAMPLEFMTS(AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_DBLP),
};
