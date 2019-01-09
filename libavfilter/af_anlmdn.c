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

#include <float.h>

#include "libavutil/avassert.h"
#include "libavutil/audio_fifo.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "audio.h"
#include "formats.h"

#define SQR(x) ((x) * (x))

typedef struct AudioNLMeansContext {
    const AVClass *class;

    float a;
    int64_t pd;
    int64_t rd;

    int K;
    int S;
    int N;
    int H;

    int offset;
    AVFrame *in;
    AVFrame *cache;

    int64_t pts;

    AVAudioFifo *fifo;

    float (*compute_distance)(const float *f1, const float *f2, int K);
} AudioNLMeansContext;

#define OFFSET(x) offsetof(AudioNLMeansContext, x)
#define AF AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption anlmdn_options[] = {
    { "s", "set denoising strength", OFFSET(a),  AV_OPT_TYPE_FLOAT,    {.dbl=1},       1,   9999, AF },
    { "p", "set patch duration",     OFFSET(pd), AV_OPT_TYPE_DURATION, {.i64=2000}, 1000, 100000, AF },
    { "r", "set research duration",  OFFSET(rd), AV_OPT_TYPE_DURATION, {.i64=6000}, 2000, 300000, AF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(anlmdn);

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_NONE
    };
    int ret;

    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ret = ff_set_common_formats(ctx, formats);
    if (ret < 0)
        return ret;

    layouts = ff_all_channel_counts();
    if (!layouts)
        return AVERROR(ENOMEM);

    ret = ff_set_common_channel_layouts(ctx, layouts);
    if (ret < 0)
        return ret;

    formats = ff_all_samplerates();
    return ff_set_common_samplerates(ctx, formats);
}

static float compute_distance_ssd(const float *f1, const float *f2, int K)
{
    float distance = 0.;

    for (int k = -K; k <= K; k++)
        distance += SQR(f1[k] - f2[k]);

    return distance;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioNLMeansContext *s = ctx->priv;

    s->K = av_rescale(s->pd, outlink->sample_rate, AV_TIME_BASE);
    s->S = av_rescale(s->rd, outlink->sample_rate, AV_TIME_BASE);

    s->pts = AV_NOPTS_VALUE;
    s->H = s->K * 2 + 1;
    s->N = s->H + (s->K + s->S) * 2;

    av_frame_free(&s->in);
    av_frame_free(&s->cache);
    s->in = ff_get_audio_buffer(outlink, s->N);
    if (!s->in)
        return AVERROR(ENOMEM);

    s->cache = ff_get_audio_buffer(outlink, s->S * 2);
    if (!s->cache)
        return AVERROR(ENOMEM);

    s->fifo = av_audio_fifo_alloc(outlink->format, outlink->channels, s->N);
    if (!s->fifo)
        return AVERROR(ENOMEM);

    s->compute_distance = compute_distance_ssd;

    return 0;
}

static int filter_channel(AVFilterContext *ctx, void *arg, int ch, int nb_jobs)
{
    AudioNLMeansContext *s = ctx->priv;
    AVFrame *out = arg;
    const int S = s->S;
    const int K = s->K;
    const float *f = (const float *)(s->in->extended_data[ch]) + K;
    float *cache = (float *)s->cache->extended_data[ch];
    const float sw = 32768.f / s->a;
    float *dst = (float *)out->extended_data[ch] + s->offset;

    for (int i = S; i < s->H + S; i++) {
        float P = 0.f, Q = 0.f;
        int v = 0;

        if (i == S) {
            for (int j = i - S; j <= i + S; j++) {
                if (i == j)
                    continue;
                cache[v++] = s->compute_distance(f + i, f + j, K);
            }
        } else {
            for (int j = i - S; j < i; j++, v++)
                cache[v] = cache[v] - SQR(f[i - K - 1] - f[j - K - 1]) + SQR(f[i + K] - f[j + K]);

            for (int j = i + 1; j <= i + S; j++, v++)
                cache[v] = cache[v] - SQR(f[i - K - 1] - f[j - K - 1]) + SQR(f[i + K] - f[j + K]);
        }

        for (int j = 0; j < v; j++) {
            const float distance = cache[j];
            float w;

            av_assert0(distance >= 0.f);
            w = expf(-distance * sw);
            P += w * f[i - S + j + (j >= S)];
            Q += w;
        }

        P += f[i];
        Q += 1;

        dst[i - S] = P / Q;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AudioNLMeansContext *s = ctx->priv;
    AVFrame *out = NULL;
    int available, wanted, ret;

    if (s->pts == AV_NOPTS_VALUE)
        s->pts = in->pts;

    ret = av_audio_fifo_write(s->fifo, (void **)in->extended_data,
                              in->nb_samples);
    av_frame_free(&in);

    s->offset = 0;
    available = av_audio_fifo_size(s->fifo);
    wanted = (available / s->H) * s->H;

    if (wanted >= s->H && available >= s->N) {
        out = ff_get_audio_buffer(outlink, wanted);
        if (!out)
            return AVERROR(ENOMEM);
    }

    while (available >= s->N) {
        ret = av_audio_fifo_peek(s->fifo, (void **)s->in->extended_data, s->N);
        if (ret < 0)
            break;

        ctx->internal->execute(ctx, filter_channel, out, NULL, inlink->channels);

        av_audio_fifo_drain(s->fifo, s->H);

        s->offset += s->H;
        available -= s->H;
    }

    if (out) {
        out->pts = s->pts;
        out->nb_samples = s->offset;
        s->pts += s->offset;

        return ff_filter_frame(outlink, out);
    }

    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioNLMeansContext *s = ctx->priv;

    av_audio_fifo_free(s->fifo);
    av_frame_free(&s->in);
    av_frame_free(&s->cache);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
    },
    { NULL }
};

AVFilter ff_af_anlmdn = {
    .name          = "anlmdn",
    .description   = NULL_IF_CONFIG_SMALL("Reduce broadband noise from stream using Non-Local Means."),
    .query_formats = query_formats,
    .priv_size     = sizeof(AudioNLMeansContext),
    .priv_class    = &anlmdn_class,
    .uninit        = uninit,
    .inputs        = inputs,
    .outputs       = outputs,
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};
