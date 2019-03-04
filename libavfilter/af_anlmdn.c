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

#include "af_anlmdndsp.h"

#define MAX_DIFF         11.f
#define WEIGHT_LUT_NBITS 20
#define WEIGHT_LUT_SIZE  (1<<WEIGHT_LUT_NBITS)

#define SQR(x) ((x) * (x))

typedef struct AudioNLMeansContext {
    const AVClass *class;

    float a;
    int64_t pd;
    int64_t rd;
    int om;

    float pdiff_lut_scale;
    float weight_lut[WEIGHT_LUT_SIZE];

    int K;
    int S;
    int N;
    int H;

    int offset;
    AVFrame *in;
    AVFrame *cache;

    int64_t pts;

    AVAudioFifo *fifo;
    int eof_left;

    AudioNLMDNDSPContext dsp;
} AudioNLMeansContext;

enum OutModes {
    IN_MODE,
    OUT_MODE,
    NOISE_MODE,
    NB_MODES
};

#define OFFSET(x) offsetof(AudioNLMeansContext, x)
#define AF AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption anlmdn_options[] = {
    { "s", "set denoising strength", OFFSET(a),  AV_OPT_TYPE_FLOAT,    {.dbl=0.00001},0.00001, 10, AF },
    { "p", "set patch duration",     OFFSET(pd), AV_OPT_TYPE_DURATION, {.i64=2000}, 1000, 100000, AF },
    { "r", "set research duration",  OFFSET(rd), AV_OPT_TYPE_DURATION, {.i64=6000}, 2000, 300000, AF },
    { "o", "set output mode",        OFFSET(om), AV_OPT_TYPE_INT,      {.i64=OUT_MODE},  0, NB_MODES-1, AF, "mode" },
    {  "i", "input",                 0,          AV_OPT_TYPE_CONST,    {.i64=IN_MODE},   0,  0, AF, "mode" },
    {  "o", "output",                0,          AV_OPT_TYPE_CONST,    {.i64=OUT_MODE},  0,  0, AF, "mode" },
    {  "n", "noise",                 0,          AV_OPT_TYPE_CONST,    {.i64=NOISE_MODE},0,  0, AF, "mode" },
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

static float compute_distance_ssd_c(const float *f1, const float *f2, ptrdiff_t K)
{
    float distance = 0.;

    for (int k = -K; k <= K; k++)
        distance += SQR(f1[k] - f2[k]);

    return distance;
}

static void compute_cache_c(float *cache, const float *f,
                            ptrdiff_t S, ptrdiff_t K,
                            ptrdiff_t i, ptrdiff_t jj)
{
    int v = 0;

    for (int j = jj; j < jj + S; j++, v++)
        cache[v] += -SQR(f[i - K - 1] - f[j - K - 1]) + SQR(f[i + K] - f[j + K]);
}

void ff_anlmdn_init(AudioNLMDNDSPContext *dsp)
{
    dsp->compute_distance_ssd = compute_distance_ssd_c;
    dsp->compute_cache        = compute_cache_c;

    if (ARCH_X86)
        ff_anlmdn_init_x86(dsp);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioNLMeansContext *s = ctx->priv;
    int ret;

    s->K = av_rescale(s->pd, outlink->sample_rate, AV_TIME_BASE);
    s->S = av_rescale(s->rd, outlink->sample_rate, AV_TIME_BASE);

    s->eof_left = -1;
    s->pts = AV_NOPTS_VALUE;
    s->H = s->K * 2 + 1;
    s->N = s->H + (s->K + s->S) * 2;

    av_log(ctx, AV_LOG_DEBUG, "K:%d S:%d H:%d N:%d\n", s->K, s->S, s->H, s->N);

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

    ret = av_audio_fifo_write(s->fifo, (void **)s->in->extended_data, s->K + s->S);
    if (ret < 0)
        return ret;

    s->pdiff_lut_scale = 1.f / MAX_DIFF * WEIGHT_LUT_SIZE;
    for (int i = 0; i < WEIGHT_LUT_SIZE; i++) {
        float w = -i / s->pdiff_lut_scale;

        s->weight_lut[i] = expf(w);
    }

    ff_anlmdn_init(&s->dsp);

    return 0;
}

static int filter_channel(AVFilterContext *ctx, void *arg, int ch, int nb_jobs)
{
    AudioNLMeansContext *s = ctx->priv;
    AVFrame *out = arg;
    const int S = s->S;
    const int K = s->K;
    const int om = s->om;
    const float *f = (const float *)(s->in->extended_data[ch]) + K;
    float *cache = (float *)s->cache->extended_data[ch];
    const float sw = (65536.f / (4 * K + 2)) / sqrtf(s->a);
    float *dst = (float *)out->extended_data[ch] + s->offset;

    for (int i = S; i < s->H + S; i++) {
        float P = 0.f, Q = 0.f;
        int v = 0;

        if (i == S) {
            for (int j = i - S; j <= i + S; j++) {
                if (i == j)
                    continue;
                cache[v++] = s->dsp.compute_distance_ssd(f + i, f + j, K);
            }
        } else {
            s->dsp.compute_cache(cache, f, S, K, i, i - S);
            s->dsp.compute_cache(cache + S, f, S, K, i, i + 1);
        }

        for (int j = 0; j < 2 * S && !ctx->is_disabled; j++) {
            const float distance = cache[j];
            unsigned weight_lut_idx;
            float w;

            av_assert2(distance >= 0.f);
            w = distance * sw;
            if (w >= MAX_DIFF)
                continue;
            weight_lut_idx = w * s->pdiff_lut_scale;
            av_assert2(weight_lut_idx < WEIGHT_LUT_SIZE);
            w = s->weight_lut[weight_lut_idx];
            P += w * f[i - S + j + (j >= S)];
            Q += w;
        }

        P += f[i];
        Q += 1;

        switch (om) {
        case IN_MODE:    dst[i - S] = f[i];           break;
        case OUT_MODE:   dst[i - S] = P / Q;          break;
        case NOISE_MODE: dst[i - S] = f[i] - (P / Q); break;
        }
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
        if (s->eof_left >= 0) {
            out->nb_samples = FFMIN(s->eof_left, s->offset);
            s->eof_left -= out->nb_samples;
        }
        s->pts += s->offset;

        return ff_filter_frame(outlink, out);
    }

    return ret;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioNLMeansContext *s = ctx->priv;
    int ret;

    ret = ff_request_frame(ctx->inputs[0]);

    if (ret == AVERROR_EOF && s->eof_left != 0) {
        AVFrame *in;

        if (s->eof_left < 0)
            s->eof_left = av_audio_fifo_size(s->fifo) - (s->S + s->K);
        if (s->eof_left < 0)
            return AVERROR_EOF;
        in = ff_get_audio_buffer(outlink, s->H);
        if (!in)
            return AVERROR(ENOMEM);

        return filter_frame(ctx->inputs[0], in);
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
        .request_frame = request_frame,
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
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                     AVFILTER_FLAG_SLICE_THREADS,
};
