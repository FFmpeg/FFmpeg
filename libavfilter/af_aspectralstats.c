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

#include "libavutil/opt.h"
#include "libavutil/tx.h"
#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "internal.h"
#include "window_func.h"

#define MEASURE_ALL       UINT_MAX
#define MEASURE_NONE      0
#define MEASURE_MEAN     (1 <<  0)
#define MEASURE_VARIANCE (1 <<  1)
#define MEASURE_CENTROID (1 <<  2)
#define MEASURE_SPREAD   (1 <<  3)
#define MEASURE_SKEWNESS (1 <<  4)
#define MEASURE_KURTOSIS (1 <<  5)
#define MEASURE_ENTROPY  (1 <<  6)
#define MEASURE_FLATNESS (1 <<  7)
#define MEASURE_CREST    (1 <<  8)
#define MEASURE_FLUX     (1 <<  9)
#define MEASURE_SLOPE    (1 << 10)
#define MEASURE_DECREASE (1 << 11)
#define MEASURE_ROLLOFF  (1 << 12)

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
    unsigned measure;
    int win_size;
    int win_func;
    float overlap;
    int nb_channels;
    int hop_size;
    ChannelSpectralStats *stats;
    float *window_func_lut;
    av_tx_fn tx_fn;
    AVTXContext **fft;
    AVComplexFloat **fft_in;
    AVComplexFloat **fft_out;
    float **prev_magnitude;
    float **magnitude;
    AVFrame *window;
} AudioSpectralStatsContext;

#define OFFSET(x) offsetof(AudioSpectralStatsContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption aspectralstats_options[] = {
    { "win_size", "set the window size", OFFSET(win_size), AV_OPT_TYPE_INT, {.i64=2048}, 32, 65536, A },
    WIN_FUNC_OPTION("win_func", OFFSET(win_func), A, WFUNC_HANNING),
    { "overlap", "set window overlap", OFFSET(overlap), AV_OPT_TYPE_FLOAT, {.dbl=0.5}, 0,  1, A },
    { "measure", "select the parameters which are measured", OFFSET(measure), AV_OPT_TYPE_FLAGS, {.i64=MEASURE_ALL}, 0, UINT_MAX, A, .unit = "measure" },
    { "none",     "", 0, AV_OPT_TYPE_CONST, {.i64=MEASURE_NONE    }, 0, 0, A, .unit = "measure" },
    { "all",      "", 0, AV_OPT_TYPE_CONST, {.i64=MEASURE_ALL     }, 0, 0, A, .unit = "measure" },
    { "mean",     "", 0, AV_OPT_TYPE_CONST, {.i64=MEASURE_MEAN    }, 0, 0, A, .unit = "measure" },
    { "variance", "", 0, AV_OPT_TYPE_CONST, {.i64=MEASURE_VARIANCE}, 0, 0, A, .unit = "measure" },
    { "centroid", "", 0, AV_OPT_TYPE_CONST, {.i64=MEASURE_CENTROID}, 0, 0, A, .unit = "measure" },
    { "spread",   "", 0, AV_OPT_TYPE_CONST, {.i64=MEASURE_SPREAD  }, 0, 0, A, .unit = "measure" },
    { "skewness", "", 0, AV_OPT_TYPE_CONST, {.i64=MEASURE_SKEWNESS}, 0, 0, A, .unit = "measure" },
    { "kurtosis", "", 0, AV_OPT_TYPE_CONST, {.i64=MEASURE_KURTOSIS}, 0, 0, A, .unit = "measure" },
    { "entropy",  "", 0, AV_OPT_TYPE_CONST, {.i64=MEASURE_ENTROPY }, 0, 0, A, .unit = "measure" },
    { "flatness", "", 0, AV_OPT_TYPE_CONST, {.i64=MEASURE_FLATNESS}, 0, 0, A, .unit = "measure" },
    { "crest",    "", 0, AV_OPT_TYPE_CONST, {.i64=MEASURE_CREST   }, 0, 0, A, .unit = "measure" },
    { "flux",     "", 0, AV_OPT_TYPE_CONST, {.i64=MEASURE_FLUX    }, 0, 0, A, .unit = "measure" },
    { "slope",    "", 0, AV_OPT_TYPE_CONST, {.i64=MEASURE_SLOPE   }, 0, 0, A, .unit = "measure" },
    { "decrease", "", 0, AV_OPT_TYPE_CONST, {.i64=MEASURE_DECREASE}, 0, 0, A, .unit = "measure" },
    { "rolloff",  "", 0, AV_OPT_TYPE_CONST, {.i64=MEASURE_ROLLOFF }, 0, 0, A, .unit = "measure" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(aspectralstats);

static int config_output(AVFilterLink *outlink)
{
    AudioSpectralStatsContext *s = outlink->src->priv;
    float overlap, scale = 1.f;
    int ret;

    s->nb_channels = outlink->ch_layout.nb_channels;
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

    s->window = ff_get_audio_buffer(outlink, s->win_size);
    if (!s->window)
        return AVERROR(ENOMEM);

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

        if (s->measure & MEASURE_MEAN)
            set_meta(metadata, ch + 1, "mean",     "%g", stats->mean);
        if (s->measure & MEASURE_VARIANCE)
            set_meta(metadata, ch + 1, "variance", "%g", stats->variance);
        if (s->measure & MEASURE_CENTROID)
            set_meta(metadata, ch + 1, "centroid", "%g", stats->centroid);
        if (s->measure & MEASURE_SPREAD)
            set_meta(metadata, ch + 1, "spread",   "%g", stats->spread);
        if (s->measure & MEASURE_SKEWNESS)
            set_meta(metadata, ch + 1, "skewness", "%g", stats->skewness);
        if (s->measure & MEASURE_KURTOSIS)
            set_meta(metadata, ch + 1, "kurtosis", "%g", stats->kurtosis);
        if (s->measure & MEASURE_ENTROPY)
            set_meta(metadata, ch + 1, "entropy",  "%g", stats->entropy);
        if (s->measure & MEASURE_FLATNESS)
            set_meta(metadata, ch + 1, "flatness", "%g", stats->flatness);
        if (s->measure & MEASURE_CREST)
            set_meta(metadata, ch + 1, "crest",    "%g", stats->crest);
        if (s->measure & MEASURE_FLUX)
            set_meta(metadata, ch + 1, "flux",     "%g", stats->flux);
        if (s->measure & MEASURE_SLOPE)
            set_meta(metadata, ch + 1, "slope",    "%g", stats->slope);
        if (s->measure & MEASURE_DECREASE)
            set_meta(metadata, ch + 1, "decrease", "%g", stats->decrease);
        if (s->measure & MEASURE_ROLLOFF)
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
    const float *window_func_lut = s->window_func_lut;
    AVFrame *in = arg;
    const int channels = s->nb_channels;
    const int start = (channels * jobnr) / nb_jobs;
    const int end = (channels * (jobnr+1)) / nb_jobs;
    const int offset = s->win_size - s->hop_size;

    for (int ch = start; ch < end; ch++) {
        float *window = (float *)s->window->extended_data[ch];
        ChannelSpectralStats *stats = &s->stats[ch];
        AVComplexFloat *fft_out = s->fft_out[ch];
        AVComplexFloat *fft_in = s->fft_in[ch];
        float *magnitude = s->magnitude[ch];
        float *prev_magnitude = s->prev_magnitude[ch];
        const float scale = 1.f / s->win_size;

        memmove(window, &window[s->hop_size], offset * sizeof(float));
        memcpy(&window[offset], in->extended_data[ch], in->nb_samples * sizeof(float));
        memset(&window[offset + in->nb_samples], 0, (s->hop_size - in->nb_samples) * sizeof(float));

        for (int n = 0; n < s->win_size; n++) {
            fft_in[n].re = window[n] * window_func_lut[n];
            fft_in[n].im = 0;
        }

        s->tx_fn(s->fft[ch], fft_out, fft_in, sizeof(*fft_in));

        for (int n = 0; n < s->win_size / 2; n++) {
            fft_out[n].re *= scale;
            fft_out[n].im *= scale;
        }

        for (int n = 0; n < s->win_size / 2; n++)
            magnitude[n] = hypotf(fft_out[n].re, fft_out[n].im);

        if (s->measure & (MEASURE_MEAN | MEASURE_VARIANCE))
            stats->mean     = spectral_mean(magnitude, s->win_size / 2, in->sample_rate / 2);
        if (s->measure & MEASURE_VARIANCE)
            stats->variance = spectral_variance(magnitude, s->win_size / 2, in->sample_rate / 2, stats->mean);
        if (s->measure & (MEASURE_SPREAD | MEASURE_KURTOSIS | MEASURE_SKEWNESS | MEASURE_CENTROID))
            stats->centroid = spectral_centroid(magnitude, s->win_size / 2, in->sample_rate / 2);
        if (s->measure & (MEASURE_SPREAD | MEASURE_KURTOSIS | MEASURE_SKEWNESS))
            stats->spread   = spectral_spread(magnitude, s->win_size / 2, in->sample_rate / 2, stats->centroid);
        if (s->measure & MEASURE_SKEWNESS)
            stats->skewness = spectral_skewness(magnitude, s->win_size / 2, in->sample_rate / 2, stats->centroid, stats->spread);
        if (s->measure & MEASURE_KURTOSIS)
            stats->kurtosis = spectral_kurtosis(magnitude, s->win_size / 2, in->sample_rate / 2, stats->centroid, stats->spread);
        if (s->measure & MEASURE_ENTROPY)
            stats->entropy  = spectral_entropy(magnitude, s->win_size / 2, in->sample_rate / 2);
        if (s->measure & MEASURE_FLATNESS)
            stats->flatness = spectral_flatness(magnitude, s->win_size / 2, in->sample_rate / 2);
        if (s->measure & MEASURE_CREST)
            stats->crest    = spectral_crest(magnitude, s->win_size / 2, in->sample_rate / 2);
        if (s->measure & MEASURE_FLUX)
            stats->flux     = spectral_flux(magnitude, prev_magnitude, s->win_size / 2, in->sample_rate / 2);
        if (s->measure & MEASURE_SLOPE)
            stats->slope    = spectral_slope(magnitude, s->win_size / 2, in->sample_rate / 2);
        if (s->measure & MEASURE_DECREASE)
            stats->decrease = spectral_decrease(magnitude, s->win_size / 2, in->sample_rate / 2);
        if (s->measure & MEASURE_ROLLOFF)
            stats->rolloff  = spectral_rolloff(magnitude, s->win_size / 2, in->sample_rate / 2);

        memcpy(prev_magnitude, magnitude, s->win_size * sizeof(float));
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AudioSpectralStatsContext *s = ctx->priv;
    AVDictionary **metadata;
    AVFrame *out;
    int ret;

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_audio_buffer(outlink, in->nb_samples);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        ret = av_frame_copy_props(out, in);
        if (ret < 0)
            goto fail;
        ret = av_frame_copy(out, in);
        if (ret < 0)
            goto fail;
    }

    metadata = &out->metadata;
    ff_filter_execute(ctx, filter_channel, in, NULL,
                      FFMIN(inlink->ch_layout.nb_channels, ff_filter_get_nb_threads(ctx)));

    set_metadata(s, metadata);

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return ret;
}

static int activate(AVFilterContext *ctx)
{
    AudioSpectralStatsContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFilterLink *inlink = ctx->inputs[0];
    AVFrame *in;
    int ret;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    ret = ff_inlink_consume_samples(inlink, s->hop_size, s->hop_size, &in);
    if (ret < 0)
        return ret;
    if (ret > 0)
        ret = filter_frame(inlink, in);
    if (ret < 0)
        return ret;

    if (ff_inlink_queued_samples(inlink) >= s->hop_size) {
        ff_filter_set_ready(ctx, 10);
        return 0;
    }

    FF_FILTER_FORWARD_STATUS(inlink, outlink);
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
    av_frame_free(&s->window);
}

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
    FILTER_INPUTS(ff_audio_default_filterpad),
    FILTER_OUTPUTS(aspectralstats_outputs),
    FILTER_SINGLE_SAMPLEFMT(AV_SAMPLE_FMT_FLTP),
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};
