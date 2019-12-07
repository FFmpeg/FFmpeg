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

#include "libavutil/avassert.h"
#include "libavutil/audio_fifo.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
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

    int (*xcorrelate)(AVFilterContext *ctx, AVFrame *out);
} AudioXCorrelateContext;

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_NONE
    };
    int ret;

    layouts = ff_all_channel_counts();
    if (!layouts)
        return AVERROR(ENOMEM);
    ret = ff_set_common_channel_layouts(ctx, layouts);
    if (ret < 0)
        return ret;

    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ret = ff_set_common_formats(ctx, formats);
    if (ret < 0)
        return ret;

    formats = ff_all_samplerates();
    if (!formats)
        return AVERROR(ENOMEM);
    return ff_set_common_samplerates(ctx, formats);
}

static float mean_sum(const float *in, int size)
{
    float mean_sum = 0.f;

    for (int i = 0; i < size; i++)
        mean_sum += in[i];

    return mean_sum;
}

static float square_sum(const float *x, const float *y, int size)
{
    float square_sum = 0.f;

    for (int i = 0; i < size; i++)
        square_sum += x[i] * y[i];

    return square_sum;
}

static float xcorrelate(const float *x, const float *y, float sumx, float sumy, int size)
{
    const float xm = sumx / size, ym = sumy / size;
    float num = 0.f, den, den0 = 0.f, den1 = 0.f;

    for (int i = 0; i < size; i++) {
        float xd = x[i] - xm;
        float yd = y[i] - ym;

        num += xd * yd;
        den0 += xd * xd;
        den1 += yd * yd;
    }

    num /= size;
    den  = sqrtf((den0 * den1) / (size * size));

    return den <= 1e-6f ? 0.f : num / den;
}

static int xcorrelate_slow(AVFilterContext *ctx, AVFrame *out)
{
    AudioXCorrelateContext *s = ctx->priv;
    const int size = s->size;
    int used;

    for (int ch = 0; ch < out->channels; ch++) {
        const float *x = (const float *)s->cache[0]->extended_data[ch];
        const float *y = (const float *)s->cache[1]->extended_data[ch];
        float *sumx = (float *)s->mean_sum[0]->extended_data[ch];
        float *sumy = (float *)s->mean_sum[1]->extended_data[ch];
        float *dst = (float *)out->extended_data[ch];

        used = s->used;
        if (!used) {
            sumx[0] = mean_sum(x, size);
            sumy[0] = mean_sum(y, size);
            used = 1;
        }

        for (int n = 0; n < out->nb_samples; n++) {
            dst[n] = xcorrelate(x + n, y + n, sumx[0], sumy[0], size);

            sumx[0] -= x[n];
            sumx[0] += x[n + size];
            sumy[0] -= y[n];
            sumy[0] += y[n + size];
        }
    }

    return used;
}

static int xcorrelate_fast(AVFilterContext *ctx, AVFrame *out)
{
    AudioXCorrelateContext *s = ctx->priv;
    const int size = s->size;
    int used;

    for (int ch = 0; ch < out->channels; ch++) {
        const float *x = (const float *)s->cache[0]->extended_data[ch];
        const float *y = (const float *)s->cache[1]->extended_data[ch];
        float *num_sum = (float *)s->num_sum->extended_data[ch];
        float *den_sumx = (float *)s->den_sum[0]->extended_data[ch];
        float *den_sumy = (float *)s->den_sum[1]->extended_data[ch];
        float *dst = (float *)out->extended_data[ch];

        used = s->used;
        if (!used) {
            num_sum[0]  = square_sum(x, y, size);
            den_sumx[0] = square_sum(x, x, size);
            den_sumy[0] = square_sum(y, y, size);
            used = 1;
        }

        for (int n = 0; n < out->nb_samples; n++) {
            float num, den;

            num = num_sum[0] / size;
            den = sqrtf((den_sumx[0] * den_sumy[0]) / (size * size));

            dst[n] = den <= 1e-6f ? 0.f : num / den;

            num_sum[0]  -= x[n] * y[n];
            num_sum[0]  += x[n + size] * y[n + size];
            den_sumx[0] -= x[n] * x[n];
            den_sumx[0]  = FFMAX(den_sumx[0], 0.f);
            den_sumx[0] += x[n + size] * x[n + size];
            den_sumy[0] -= y[n] * y[n];
            den_sumy[0]  = FFMAX(den_sumy[0], 0.f);
            den_sumy[0] += y[n + size] * y[n + size];
        }
    }

    return used;
}

static int activate(AVFilterContext *ctx)
{
    AudioXCorrelateContext *s = ctx->priv;
    AVFrame *frame = NULL;
    int ret, status;
    int available;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK_ALL(ctx->outputs[0], ctx);

    for (int i = 0; i < 2; i++) {
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
            s->cache[0] = ff_get_audio_buffer(ctx->outputs[0], available);
            if (!s->cache[0])
                return AVERROR(ENOMEM);
        }

        if (!s->cache[1] || s->cache[1]->nb_samples < available) {
            av_frame_free(&s->cache[1]);
            s->cache[1] = ff_get_audio_buffer(ctx->outputs[0], available);
            if (!s->cache[1])
                return AVERROR(ENOMEM);
        }

        ret = av_audio_fifo_peek(s->fifo[0], (void **)s->cache[0]->extended_data, available);
        if (ret < 0)
            return ret;

        ret = av_audio_fifo_peek(s->fifo[1], (void **)s->cache[1]->extended_data, available);
        if (ret < 0)
            return ret;

        out = ff_get_audio_buffer(ctx->outputs[0], out_samples);
        if (!out)
            return AVERROR(ENOMEM);

        s->used = s->xcorrelate(ctx, out);

        out->pts = s->pts;
        s->pts += out_samples;

        av_audio_fifo_drain(s->fifo[0], out_samples);
        av_audio_fifo_drain(s->fifo[1], out_samples);

        return ff_filter_frame(ctx->outputs[0], out);
    }

    if (av_audio_fifo_size(s->fifo[0]) > s->size &&
        av_audio_fifo_size(s->fifo[1]) > s->size) {
        ff_filter_set_ready(ctx, 10);
        return 0;
    }

    for (int i = 0; i < 2; i++) {
        if (ff_inlink_acknowledge_status(ctx->inputs[i], &status, &pts)) {
            ff_outlink_set_status(ctx->outputs[0], status, pts);
            return 0;
        }
    }

    if (ff_outlink_frame_wanted(ctx->outputs[0])) {
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
    AVFilterLink *inlink = ctx->inputs[0];
    AudioXCorrelateContext *s = ctx->priv;

    s->pts = AV_NOPTS_VALUE;

    outlink->format = inlink->format;
    outlink->channels = inlink->channels;
    s->fifo[0] = av_audio_fifo_alloc(outlink->format, outlink->channels, s->size);
    s->fifo[1] = av_audio_fifo_alloc(outlink->format, outlink->channels, s->size);
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
    case 0: s->xcorrelate = xcorrelate_slow; break;
    case 1: s->xcorrelate = xcorrelate_fast; break;
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
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
    { NULL }
};

#define AF AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define OFFSET(x) offsetof(AudioXCorrelateContext, x)

static const AVOption axcorrelate_options[] = {
    { "size", "set segment size", OFFSET(size), AV_OPT_TYPE_INT,   {.i64=256}, 2, 131072, AF },
    { "algo", "set alghorithm",   OFFSET(algo), AV_OPT_TYPE_INT,   {.i64=0},   0,      1, AF, "algo" },
    { "slow", "slow algorithm",   0,            AV_OPT_TYPE_CONST, {.i64=0},   0,      0, AF, "algo" },
    { "fast", "fast algorithm",   0,            AV_OPT_TYPE_CONST, {.i64=1},   0,      0, AF, "algo" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(axcorrelate);

AVFilter ff_af_axcorrelate = {
    .name           = "axcorrelate",
    .description    = NULL_IF_CONFIG_SMALL("Cross-correlate two audio streams."),
    .priv_size      = sizeof(AudioXCorrelateContext),
    .priv_class     = &axcorrelate_class,
    .query_formats  = query_formats,
    .activate       = activate,
    .uninit         = uninit,
    .inputs         = inputs,
    .outputs        = outputs,
};
