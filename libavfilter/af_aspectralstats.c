/*
 * Copyright (c) 2021 Paul B Mahol
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
#include <math.h>

#include "libavutil/audio_fifo.h"
#include "libavutil/opt.h"
#include "libavutil/tx.h"
#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "internal.h"
#include "window_func.h"

typedef struct ChannelSpectralStats {
    float mean;
    float variance;
    float centroid;
    float spread;
    float skewness;
    float kurtosis;
    float entropy;
    float flatness;
    float crest;
    float flux;
    float slope;
    float decrease;
    float rolloff;
} ChannelSpectralStats;

typedef struct AudioSpectralStatsContext {
    const AVClass *class;
    int win_size;
    int win_func;
    float overlap;
    int nb_channels;
    int hop_size;
    ChannelSpectralStats *stats;
    AVAudioFifo *fifo;
    float *window_func_lut;
    int64_t pts;
    int eof;
    av_tx_fn tx_fn;
    AVTXContext **fft;
    AVComplexFloat **fft_in;
    AVComplexFloat **fft_out;
    float **prev_magnitude;
    float **magnitude;
} AudioSpectralStatsContext;

#define OFFSET(x) offsetof(AudioSpectralStatsContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption aspectralstats_options[] = {
    { "win_size", "set the window size", OFFSET(win_size), AV_OPT_TYPE_INT, {.i64=2048}, 32, 65536, A },
    WIN_FUNC_OPTION("win_func", OFFSET(win_func), A, WFUNC_HANNING),
    { "overlap", "set window overlap", OFFSET(overlap), AV_OPT_TYPE_FLOAT, {.dbl=0.5}, 0,  1, A },
    { NULL }
};

AVFILTER_DEFINE_CLASS(aspectralstats);

static int config_output(AVFilterLink *outlink)
{
    AudioSpectralStatsContext *s = outlink->src->priv;
    float overlap, scale;
    int ret;

    s->nb_channels = outlink->channels;
    s->fifo = av_audio_fifo_alloc(outlink->format, s->nb_channels, s->win_size);
    if (!s->fifo)
        return AVERROR(ENOMEM);

    s->window_func_lut = av_realloc_f(s->window_func_lut, s->win_size,
                                      sizeof(*s->window_func_lut));
    if (!s->window_func_lut)
        return AVERROR(ENOMEM);
    generate_window_func(s->window_func_lut, s->win_size, s->win_func, &overlap);
    if (s->overlap == 1.f)
        s->overlap = overlap;

    s->hop_size = s->win_size * (1.f - s->overlap);
    if (s->hop_size <= 0)
        return AVERROR(EINVAL);

    s->stats = av_calloc(s->nb_channels, sizeof(*s->stats));
    if (!s->stats)
        return AVERROR(ENOMEM);

    s->fft = av_calloc(s->nb_channels, sizeof(*s->fft));
    if (!s->fft)
        return AVERROR(ENOMEM);

    s->magnitude = av_calloc(s->nb_channels, sizeof(*s->magnitude));
    if (!s->magnitude)
        return AVERROR(ENOMEM);

    s->prev_magnitude = av_calloc(s->nb_channels, sizeof(*s->prev_magnitude));
    if (!s->prev_magnitude)
        return AVERROR(ENOMEM);

    s->fft_in = av_calloc(s->nb_channels, sizeof(*s->fft_in));
    if (!s->fft_in)
        return AVERROR(ENOMEM);

    s->fft_out = av_calloc(s->nb_channels, sizeof(*s->fft_out));
    if (!s->fft_out)
        return AVERROR(ENOMEM);

    for (int ch = 0; ch < s->nb_channels; ch++) {
        ret = av_tx_init(&s->fft[ch], &s->tx_fn, AV_TX_FLOAT_FFT, 0, s->win_size, &scale, 0);
        if (ret < 0)
            return ret;

        s->fft_in[ch] = av_calloc(s->win_size, sizeof(**s->fft_in));
        if (!s->fft_in[ch])
            return AVERROR(ENOMEM);

        s->fft_out[ch] = av_calloc(s->win_size, sizeof(**s->fft_out));
        if (!s->fft_out[ch])
            return AVERROR(ENOMEM);

        s->magnitude[ch] = av_calloc(s->win_size, sizeof(**s->magnitude));
        if (!s->magnitude[ch])
            return AVERROR(ENOMEM);

        s->prev_magnitude[ch] = av_calloc(s->win_size, sizeof(**s->prev_magnitude));
        if (!s->prev_magnitude[ch])
            return AVERROR(ENOMEM);
    }

    return 0;
}

static void set_meta(AVDictionary **metadata, int chan, const char *key,
                     const char *fmt, float val)
{
    uint8_t value[128];
    uint8_t key2[128];

    snprintf(value, sizeof(value), fmt, val);
    if (chan)
        snprintf(key2, sizeof(key2), "lavfi.aspectralstats.%d.%s", chan, key);
    else
        snprintf(key2, sizeof(key2), "lavfi.aspectralstats.%s", key);
    av_dict_set(metadata, key2, value, 0);
}

static void set_metadata(AudioSpectralStatsContext *s, AVDictionary **metadata)
{
    for (int ch = 0; ch < s->nb_channels; ch++) {
        ChannelSpectralStats *stats = &s->stats[ch];

        set_meta(metadata, ch + 1, "mean",     "%g", stats->mean);
        set_meta(metadata, ch + 1, "variance", "%g", stats->variance);
        set_meta(metadata, ch + 1, "centroid", "%g", stats->centroid);
        set_meta(metadata, ch + 1, "spread",   "%g", stats->spread);
        set_meta(metadata, ch + 1, "skewness", "%g", stats->skewness);
        set_meta(metadata, ch + 1, "kurtosis", "%g", stats->kurtosis);
        set_meta(metadata, ch + 1, "entropy",  "%g", stats->entropy);
        set_meta(metadata, ch + 1, "flatness", "%g", stats->flatness);
        set_meta(metadata, ch + 1, "crest",    "%g", stats->crest);
        set_meta(metadata, ch + 1, "flux",     "%g", stats->flux);
        set_meta(metadata, ch + 1, "slope",    "%g", stats->slope);
        set_meta(metadata, ch + 1, "decrease", "%g", stats->decrease);
        set_meta(metadata, ch + 1, "rolloff",  "%g", stats->rolloff);
    }
}

static float spectral_mean(const float *const spectral, int size, int max_freq)
{
    float sum = 0.f;

    for (int n = 0; n < size; n++)
        sum += spectral[n];

    return sum / size;
}

static float sqrf(float a)
{
    return a * a;
}

static float spectral_variance(const float *const spectral, int size, int max_freq, float mean)
{
    float sum = 0.f;

    for (int n = 0; n < size; n++)
        sum += sqrf(spectral[n] - mean);

    return sum / size;
}

static float spectral_centroid(const float *const spectral, int size, int max_freq)
{
    const float scale = max_freq / (float)size;
    float num = 0.f, den = 0.f;

    for (int n = 0; n < size; n++) {
        num += spectral[n] * n * scale;
        den += spectral[n];
    }

    if (den <= FLT_EPSILON)
        return 1.f;
    return num / den;
}

static float spectral_spread(const float *const spectral, int size, int max_freq, float centroid)
{
    const float scale = max_freq / (float)size;
    float num = 0.f, den = 0.f;

    for (int n = 0; n < size; n++) {
        num += spectral[n] * sqrf(n * scale - centroid);
        den += spectral[n];
    }

    if (den <= FLT_EPSILON)
        return 1.f;
    return sqrtf(num / den);
}

static float cbrf(float a)
{
    return a * a * a;
}

static float spectral_skewness(const float *const spectral, int size, int max_freq, float centroid, float spread)
{
    const float scale = max_freq / (float)size;
    float num = 0.f, den = 0.f;

    for (int n = 0; n < size; n++) {
        num += spectral[n] * cbrf(n * scale - centroid);
        den += spectral[n];
    }

    den *= cbrf(spread);
    if (den <= FLT_EPSILON)
        return 1.f;
    return num / den;
}

static float spectral_kurtosis(const float *const spectral, int size, int max_freq, float centroid, float spread)
{
    const float scale = max_freq / (float)size;
    float num = 0.f, den = 0.f;

    for (int n = 0; n < size; n++) {
        num += spectral[n] * sqrf(sqrf(n * scale - centroid));
        den += spectral[n];
    }

    den *= sqrf(sqrf(spread));
    if (den <= FLT_EPSILON)
        return 1.f;
    return num / den;
}

static float spectral_entropy(const float *const spectral, int size, int max_freq)
{
    float num = 0.f, den = 0.f;

    for (int n = 0; n < size; n++) {
        num += spectral[n] * logf(spectral[n] + FLT_EPSILON);
    }

    den = logf(size);
    if (den <= FLT_EPSILON)
        return 1.f;
    return -num / den;
}

static float spectral_flatness(const float *const spectral, int size, int max_freq)
{
    float num = 0.f, den = 0.f;

    for (int n = 0; n < size; n++) {
        float v = FLT_EPSILON + spectral[n];
        num += logf(v);
        den += v;
    }

    num /= size;
    den /= size;
    num = expf(num);
    if (den <= FLT_EPSILON)
        return 0.f;
    return num / den;
}

static float spectral_crest(const float *const spectral, int size, int max_freq)
{
    float max = 0.f, mean = 0.f;

    for (int n = 0; n < size; n++) {
        max = fmaxf(max, spectral[n]);
        mean += spectral[n];
    }

    mean /= size;
    if (mean <= FLT_EPSILON)
        return 0.f;
    return max / mean;
}

static float spectral_flux(const float *const spectral, const float *const prev_spectral,
                           int size, int max_freq)
{
    float sum = 0.f;

    for (int n = 0; n < size; n++)
        sum += sqrf(spectral[n] - prev_spectral[n]);

    return sqrtf(sum);
}

static float spectral_slope(const float *const spectral, int size, int max_freq)
{
    const float mean_freq = size * 0.5f;
    float mean_spectral = 0.f, num = 0.f, den = 0.f;

    for (int n = 0; n < size; n++)
        mean_spectral += spectral[n];
    mean_spectral /= size;

    for (int n = 0; n < size; n++) {
        num += ((n - mean_freq) / mean_freq) * (spectral[n] - mean_spectral);
        den += sqrf((n - mean_freq) / mean_freq);
    }

    if (fabsf(den) <= FLT_EPSILON)
        return 0.f;
    return num / den;
}

static float spectral_decrease(const float *const spectral, int size, int max_freq)
{
    float num = 0.f, den = 0.f;

    for (int n = 1; n < size; n++) {
        num += (spectral[n] - spectral[0]) / n;
        den += spectral[n];
    }

    if (den <= FLT_EPSILON)
        return 0.f;
    return num / den;
}

static float spectral_rolloff(const float *const spectral, int size, int max_freq)
{
    const float scale = max_freq / (float)size;
    float norm = 0.f, sum = 0.f;
    int idx = 0.f;

    for (int n = 0; n < size; n++)
        norm += spectral[n];
    norm *= 0.85f;

    for (int n = 0; n < size; n++) {
        sum += spectral[n];
        if (sum >= norm) {
            idx = n;
            break;
        }
    }

    return idx * scale;
}

static int filter_channel(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    AudioSpectralStatsContext *s = ctx->priv;
    AVFrame *in = arg;
    const int channels = s->nb_channels;
    const int samples = in->nb_samples;
    const int start = (channels * jobnr) / nb_jobs;
    const int end = (channels * (jobnr+1)) / nb_jobs;

    for (int ch = start; ch < end; ch++) {
        const float *const src = (const float *const)in->extended_data[ch];
        ChannelSpectralStats *stats = &s->stats[ch];
        AVComplexFloat *fft_out = s->fft_out[ch];
        AVComplexFloat *fft_in = s->fft_in[ch];
        float *magnitude = s->magnitude[ch];
        float *prev_magnitude = s->prev_magnitude[ch];
        const float scale = 1.f / s->win_size;

        for (int n = 0; n < samples; n++) {
            fft_in[n].re = src[n] * s->window_func_lut[n];
            fft_in[n].im = 0;
        }

        for (int n = in->nb_samples; n < s->win_size; n++) {
            fft_in[n].re = 0;
            fft_in[n].im = 0;
        }

        s->tx_fn(s->fft[ch], fft_out, fft_in, sizeof(float));

        for (int n = 0; n < s->win_size / 2; n++) {
            fft_out[n].re *= scale;
            fft_out[n].im *= scale;
        }

        for (int n = 0; n < s->win_size / 2; n++)
            magnitude[n] = hypotf(fft_out[n].re, fft_out[n].im);

        stats->mean     = spectral_mean(magnitude, s->win_size / 2, in->sample_rate / 2);
        stats->variance = spectral_variance(magnitude, s->win_size / 2, in->sample_rate / 2, stats->mean);
        stats->centroid = spectral_centroid(magnitude, s->win_size / 2, in->sample_rate / 2);
        stats->spread   = spectral_spread(magnitude, s->win_size / 2, in->sample_rate / 2, stats->centroid);
        stats->skewness = spectral_skewness(magnitude, s->win_size / 2, in->sample_rate / 2, stats->centroid, stats->spread);
        stats->kurtosis = spectral_kurtosis(magnitude, s->win_size / 2, in->sample_rate / 2, stats->centroid, stats->spread);
        stats->entropy  = spectral_entropy(magnitude, s->win_size / 2, in->sample_rate / 2);
        stats->flatness = spectral_flatness(magnitude, s->win_size / 2, in->sample_rate / 2);
        stats->crest    = spectral_crest(magnitude, s->win_size / 2, in->sample_rate / 2);
        stats->flux     = spectral_flux(magnitude, prev_magnitude, s->win_size / 2, in->sample_rate / 2);
        stats->slope    = spectral_slope(magnitude, s->win_size / 2, in->sample_rate / 2);
        stats->decrease = spectral_decrease(magnitude, s->win_size / 2, in->sample_rate / 2);
        stats->rolloff  = spectral_rolloff(magnitude, s->win_size / 2, in->sample_rate / 2);

        memcpy(prev_magnitude, magnitude, s->win_size * sizeof(float));
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AudioSpectralStatsContext *s = ctx->priv;
    AVDictionary **metadata;
    AVFrame *out, *in = NULL;
    int ret = 0;

    out = ff_get_audio_buffer(outlink, s->hop_size);
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (!in) {
        in = ff_get_audio_buffer(outlink, s->win_size);
        if (!in)
            return AVERROR(ENOMEM);
    }

    ret = av_audio_fifo_peek(s->fifo, (void **)in->extended_data, s->win_size);
    if (ret < 0)
        goto fail;

    metadata = &out->metadata;
    ff_filter_execute(ctx, filter_channel, in, NULL,
                      FFMIN(inlink->channels, ff_filter_get_nb_threads(ctx)));

    set_metadata(s, metadata);

    out->pts = s->pts;
    s->pts += av_rescale_q(s->hop_size, (AVRational){1, outlink->sample_rate}, outlink->time_base);

    av_audio_fifo_read(s->fifo, (void **)out->extended_data, s->hop_size);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
fail:
    av_frame_free(&in);
    return ret < 0 ? ret : 0;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AudioSpectralStatsContext *s = ctx->priv;
    AVFrame *in = NULL;
    int ret = 0, status;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    if (!s->eof && av_audio_fifo_size(s->fifo) < s->win_size) {
        ret = ff_inlink_consume_frame(inlink, &in);
        if (ret < 0)
            return ret;

        if (ret > 0) {
            ret = av_audio_fifo_write(s->fifo, (void **)in->extended_data,
                                      in->nb_samples);
            if (ret >= 0 && s->pts == AV_NOPTS_VALUE)
                s->pts = in->pts;

            av_frame_free(&in);
            if (ret < 0)
                return ret;
        }
    }

    if ((av_audio_fifo_size(s->fifo) >= s->win_size) ||
        (av_audio_fifo_size(s->fifo) > 0 && s->eof)) {
        ret = filter_frame(inlink);
        if (av_audio_fifo_size(s->fifo) >= s->win_size)
            ff_filter_set_ready(ctx, 100);
        return ret;
    }

    if (!s->eof && ff_inlink_acknowledge_status(inlink, &status, &pts)) {
        if (status == AVERROR_EOF) {
            s->eof = 1;
            if (av_audio_fifo_size(s->fifo) >= 0) {
                ff_filter_set_ready(ctx, 100);
                return 0;
            }
        }
    }

    if (s->eof && av_audio_fifo_size(s->fifo) <= 0) {
        ff_outlink_set_status(outlink, AVERROR_EOF, s->pts);
        return 0;
    }

    if (!s->eof)
        FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioSpectralStatsContext *s = ctx->priv;

    for (int ch = 0; ch < s->nb_channels; ch++) {
        if (s->fft)
            av_tx_uninit(&s->fft[ch]);
        if (s->fft_in)
            av_freep(&s->fft_in[ch]);
        if (s->fft_out)
            av_freep(&s->fft_out[ch]);
        if (s->magnitude)
            av_freep(&s->magnitude[ch]);
        if (s->prev_magnitude)
            av_freep(&s->prev_magnitude[ch]);
    }

    av_freep(&s->fft);
    av_freep(&s->magnitude);
    av_freep(&s->prev_magnitude);
    av_freep(&s->fft_in);
    av_freep(&s->fft_out);
    av_freep(&s->stats);

    av_freep(&s->window_func_lut);
    av_audio_fifo_free(s->fifo);
    s->fifo = NULL;
}

static const AVFilterPad aspectralstats_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
    },
};

static const AVFilterPad aspectralstats_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
};

const AVFilter ff_af_aspectralstats = {
    .name          = "aspectralstats",
    .description   = NULL_IF_CONFIG_SMALL("Show frequency domain statistics about audio frames."),
    .priv_size     = sizeof(AudioSpectralStatsContext),
    .priv_class    = &aspectralstats_class,
    .uninit        = uninit,
    .activate      = activate,
    FILTER_INPUTS(aspectralstats_inputs),
    FILTER_OUTPUTS(aspectralstats_outputs),
    FILTER_SINGLE_SAMPLEFMT(AV_SAMPLE_FMT_FLTP),
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};
