/*
 * Copyright (c) 2019 The FFmpeg Project
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
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "audio.h"
#include "filters.h"

#define MAX_OVERSAMPLE 64

enum ASoftClipTypes {
    ASC_HARD = -1,
    ASC_TANH,
    ASC_ATAN,
    ASC_CUBIC,
    ASC_EXP,
    ASC_ALG,
    ASC_QUINTIC,
    ASC_SIN,
    ASC_ERF,
    NB_TYPES,
};

typedef struct Lowpass {
    float  fb0, fb1, fb2;
    float  fa0, fa1, fa2;

    double db0, db1, db2;
    double da0, da1, da2;
} Lowpass;

typedef struct ASoftClipContext {
    const AVClass *class;

    int type;
    int oversample;
    int64_t delay;
    double threshold;
    double output;
    double param;

    Lowpass lowpass[MAX_OVERSAMPLE];
    AVFrame *frame[2];

    void (*filter)(struct ASoftClipContext *s, void **dst, const void **src,
                   int nb_samples, int channels, int start, int end);
} ASoftClipContext;

#define OFFSET(x) offsetof(ASoftClipContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption asoftclip_options[] = {
    { "type", "set softclip type", OFFSET(type), AV_OPT_TYPE_INT,    {.i64=0},         -1, NB_TYPES-1, A, .unit = "types" },
    { "hard",                NULL,            0, AV_OPT_TYPE_CONST,  {.i64=ASC_HARD},   0,          0, A, .unit = "types" },
    { "tanh",                NULL,            0, AV_OPT_TYPE_CONST,  {.i64=ASC_TANH},   0,          0, A, .unit = "types" },
    { "atan",                NULL,            0, AV_OPT_TYPE_CONST,  {.i64=ASC_ATAN},   0,          0, A, .unit = "types" },
    { "cubic",               NULL,            0, AV_OPT_TYPE_CONST,  {.i64=ASC_CUBIC},  0,          0, A, .unit = "types" },
    { "exp",                 NULL,            0, AV_OPT_TYPE_CONST,  {.i64=ASC_EXP},    0,          0, A, .unit = "types" },
    { "alg",                 NULL,            0, AV_OPT_TYPE_CONST,  {.i64=ASC_ALG},    0,          0, A, .unit = "types" },
    { "quintic",             NULL,            0, AV_OPT_TYPE_CONST,  {.i64=ASC_QUINTIC},0,          0, A, .unit = "types" },
    { "sin",                 NULL,            0, AV_OPT_TYPE_CONST,  {.i64=ASC_SIN},    0,          0, A, .unit = "types" },
    { "erf",                 NULL,            0, AV_OPT_TYPE_CONST,  {.i64=ASC_ERF},    0,          0, A, .unit = "types" },
    { "threshold", "set softclip threshold", OFFSET(threshold), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0.000001, 1, A },
    { "output", "set softclip output gain", OFFSET(output), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0.000001, 16, A },
    { "param", "set softclip parameter", OFFSET(param), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0.01,        3, A },
    { "oversample", "set oversample factor", OFFSET(oversample), AV_OPT_TYPE_INT, {.i64=1}, 1, MAX_OVERSAMPLE, A },
    { NULL }
};

AVFILTER_DEFINE_CLASS(asoftclip);

static void get_lowpass(Lowpass *s,
                        double frequency,
                        double sample_rate)
{
    double w0 = 2 * M_PI * frequency / sample_rate;
    double alpha = sin(w0) / (2 * 0.8);
    double factor;

    s->da0 =  1 + alpha;
    s->da1 = -2 * cos(w0);
    s->da2 =  1 - alpha;
    s->db0 = (1 - cos(w0)) / 2;
    s->db1 =  1 - cos(w0);
    s->db2 = (1 - cos(w0)) / 2;

    s->da1 /= s->da0;
    s->da2 /= s->da0;
    s->db0 /= s->da0;
    s->db1 /= s->da0;
    s->db2 /= s->da0;
    s->da0 /= s->da0;

    factor = (s->da0 + s->da1 + s->da2) / (s->db0 + s->db1 + s->db2);
    s->db0 *= factor;
    s->db1 *= factor;
    s->db2 *= factor;

    s->fa0 = s->da0;
    s->fa1 = s->da1;
    s->fa2 = s->da2;
    s->fb0 = s->db0;
    s->fb1 = s->db1;
    s->fb2 = s->db2;
}

static inline float run_lowpassf(const Lowpass *const s,
                                 float src, float *w)
{
    float dst;

    dst = src * s->fb0 + w[0];
    w[0] = s->fb1 * src + w[1] - s->fa1 * dst;
    w[1] = s->fb2 * src - s->fa2 * dst;

    return dst;
}

static void filter_flt(ASoftClipContext *s,
                       void **dptr, const void **sptr,
                       int nb_samples, int channels,
                       int start, int end)
{
    const int oversample = s->oversample;
    const int nb_osamples = nb_samples * oversample;
    const float scale = oversample > 1 ? oversample * 0.5f : 1.f;
    float threshold = s->threshold;
    float gain = s->output * threshold;
    float factor = 1.f / threshold;
    float param = s->param;

    for (int c = start; c < end; c++) {
        float *w = (float *)(s->frame[0]->extended_data[c]) + 2 * (oversample - 1);
        const float *src = sptr[c];
        float *dst = dptr[c];

        for (int n = 0; n < nb_samples; n++) {
            dst[oversample * n] = src[n];

            for (int m = 1; m < oversample; m++)
                dst[oversample * n + m] = 0.f;
        }

        for (int n = 0; n < nb_osamples && oversample > 1; n++)
            dst[n] = run_lowpassf(&s->lowpass[oversample - 1], dst[n], w);

        switch (s->type) {
        case ASC_HARD:
            for (int n = 0; n < nb_osamples; n++) {
                dst[n] = av_clipf(dst[n] * factor, -1.f, 1.f);
                dst[n] *= gain;
            }
            break;
        case ASC_TANH:
            for (int n = 0; n < nb_osamples; n++) {
                dst[n] = tanhf(dst[n] * factor * param);
                dst[n] *= gain;
            }
            break;
        case ASC_ATAN:
            for (int n = 0; n < nb_osamples; n++) {
                dst[n] = 2.f / M_PI * atanf(dst[n] * factor * param);
                dst[n] *= gain;
            }
            break;
        case ASC_CUBIC:
            for (int n = 0; n < nb_osamples; n++) {
                float sample = dst[n] * factor;

                if (FFABS(sample) >= 1.5f)
                    dst[n] = FFSIGN(sample);
                else
                    dst[n] = sample - 0.1481f * powf(sample, 3.f);
                dst[n] *= gain;
            }
            break;
        case ASC_EXP:
            for (int n = 0; n < nb_osamples; n++) {
                dst[n] = 2.f / (1.f + expf(-2.f * dst[n] * factor)) - 1.;
                dst[n] *= gain;
            }
            break;
        case ASC_ALG:
            for (int n = 0; n < nb_osamples; n++) {
                float sample = dst[n] * factor;

                dst[n] = sample / (sqrtf(param + sample * sample));
                dst[n] *= gain;
            }
            break;
        case ASC_QUINTIC:
            for (int n = 0; n < nb_osamples; n++) {
                float sample = dst[n] * factor;

                if (FFABS(sample) >= 1.25)
                    dst[n] = FFSIGN(sample);
                else
                    dst[n] = sample - 0.08192f * powf(sample, 5.f);
                dst[n] *= gain;
            }
            break;
        case ASC_SIN:
            for (int n = 0; n < nb_osamples; n++) {
                float sample = dst[n] * factor;

                if (FFABS(sample) >= M_PI_2)
                    dst[n] = FFSIGN(sample);
                else
                    dst[n] = sinf(sample);
                dst[n] *= gain;
            }
            break;
        case ASC_ERF:
            for (int n = 0; n < nb_osamples; n++) {
                dst[n] = erff(dst[n] * factor);
                dst[n] *= gain;
            }
            break;
        default:
            av_assert0(0);
        }

        w = (float *)(s->frame[1]->extended_data[c]) + 2 * (oversample - 1);
        for (int n = 0; n < nb_osamples && oversample > 1; n++)
            dst[n] = run_lowpassf(&s->lowpass[oversample - 1], dst[n], w);

        for (int n = 0; n < nb_samples; n++)
            dst[n] = dst[n * oversample] * scale;
    }
}

static inline double run_lowpassd(const Lowpass *const s,
                                  double src, double *w)
{
    double dst;

    dst = src * s->db0 + w[0];
    w[0] = s->db1 * src + w[1] - s->da1 * dst;
    w[1] = s->db2 * src - s->da2 * dst;

    return dst;
}

static void filter_dbl(ASoftClipContext *s,
                       void **dptr, const void **sptr,
                       int nb_samples, int channels,
                       int start, int end)
{
    const int oversample = s->oversample;
    const int nb_osamples = nb_samples * oversample;
    const double scale = oversample > 1 ? oversample * 0.5 : 1.;
    double threshold = s->threshold;
    double gain = s->output * threshold;
    double factor = 1. / threshold;
    double param = s->param;

    for (int c = start; c < end; c++) {
        double *w = (double *)(s->frame[0]->extended_data[c]) + 2 * (oversample - 1);
        const double *src = sptr[c];
        double *dst = dptr[c];

        for (int n = 0; n < nb_samples; n++) {
            dst[oversample * n] = src[n];

            for (int m = 1; m < oversample; m++)
                dst[oversample * n + m] = 0.f;
        }

        for (int n = 0; n < nb_osamples && oversample > 1; n++)
            dst[n] = run_lowpassd(&s->lowpass[oversample - 1], dst[n], w);

        switch (s->type) {
        case ASC_HARD:
            for (int n = 0; n < nb_osamples; n++) {
                dst[n] = av_clipd(dst[n] * factor, -1., 1.);
                dst[n] *= gain;
            }
            break;
        case ASC_TANH:
            for (int n = 0; n < nb_osamples; n++) {
                dst[n] = tanh(dst[n] * factor * param);
                dst[n] *= gain;
            }
            break;
        case ASC_ATAN:
            for (int n = 0; n < nb_osamples; n++) {
                dst[n] = 2. / M_PI * atan(dst[n] * factor * param);
                dst[n] *= gain;
            }
            break;
        case ASC_CUBIC:
            for (int n = 0; n < nb_osamples; n++) {
                double sample = dst[n] * factor;

                if (FFABS(sample) >= 1.5)
                    dst[n] = FFSIGN(sample);
                else
                    dst[n] = sample - 0.1481 * pow(sample, 3.);
                dst[n] *= gain;
            }
            break;
        case ASC_EXP:
            for (int n = 0; n < nb_osamples; n++) {
                dst[n] = 2. / (1. + exp(-2. * dst[n] * factor)) - 1.;
                dst[n] *= gain;
            }
            break;
        case ASC_ALG:
            for (int n = 0; n < nb_osamples; n++) {
                double sample = dst[n] * factor;

                dst[n] = sample / (sqrt(param + sample * sample));
                dst[n] *= gain;
            }
            break;
        case ASC_QUINTIC:
            for (int n = 0; n < nb_osamples; n++) {
                double sample = dst[n] * factor;

                if (FFABS(sample) >= 1.25)
                    dst[n] = FFSIGN(sample);
                else
                    dst[n] = sample - 0.08192 * pow(sample, 5.);
                dst[n] *= gain;
            }
            break;
        case ASC_SIN:
            for (int n = 0; n < nb_osamples; n++) {
                double sample = dst[n] * factor;

                if (FFABS(sample) >= M_PI_2)
                    dst[n] = FFSIGN(sample);
                else
                    dst[n] = sin(sample);
                dst[n] *= gain;
            }
            break;
        case ASC_ERF:
            for (int n = 0; n < nb_osamples; n++) {
                dst[n] = erf(dst[n] * factor);
                dst[n] *= gain;
            }
            break;
        default:
            av_assert0(0);
        }

        w = (double *)(s->frame[1]->extended_data[c]) + 2 * (oversample - 1);
        for (int n = 0; n < nb_osamples && oversample > 1; n++)
            dst[n] = run_lowpassd(&s->lowpass[oversample - 1], dst[n], w);

        for (int n = 0; n < nb_samples; n++)
            dst[n] = dst[n * oversample] * scale;
    }
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ASoftClipContext *s = ctx->priv;

    switch (inlink->format) {
    case AV_SAMPLE_FMT_FLTP: s->filter = filter_flt; break;
    case AV_SAMPLE_FMT_DBLP: s->filter = filter_dbl; break;
    default: av_assert0(0);
    }

    s->frame[0] = ff_get_audio_buffer(inlink, 2 * MAX_OVERSAMPLE);
    s->frame[1] = ff_get_audio_buffer(inlink, 2 * MAX_OVERSAMPLE);
    if (!s->frame[0] || !s->frame[1])
        return AVERROR(ENOMEM);

    for (int i = 0; i < MAX_OVERSAMPLE; i++) {
        get_lowpass(&s->lowpass[i], inlink->sample_rate / 2, inlink->sample_rate * (i + 1));
    }

    return 0;
}

typedef struct ThreadData {
    AVFrame *in, *out;
    int nb_samples;
    int channels;
} ThreadData;

static int filter_channels(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ASoftClipContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *out = td->out;
    AVFrame *in = td->in;
    const int channels = td->channels;
    const int nb_samples = td->nb_samples;
    const int start = (channels * jobnr) / nb_jobs;
    const int end = (channels * (jobnr+1)) / nb_jobs;

    s->filter(s, (void **)out->extended_data, (const void **)in->extended_data,
              nb_samples, channels, start, end);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    ASoftClipContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int nb_samples, channels;
    ThreadData td;
    AVFrame *out;

    if (av_frame_is_writable(in) && s->oversample == 1) {
        out = in;
    } else {
        out = ff_get_audio_buffer(outlink, in->nb_samples * s->oversample);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    nb_samples = in->nb_samples;
    channels = in->ch_layout.nb_channels;

    td.in = in;
    td.out = out;
    td.nb_samples = nb_samples;
    td.channels = channels;
    ff_filter_execute(ctx, filter_channels, &td, NULL,
                      FFMIN(channels, ff_filter_get_nb_threads(ctx)));

    if (out != in)
        av_frame_free(&in);

    out->nb_samples /= s->oversample;
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ASoftClipContext *s = ctx->priv;

    av_frame_free(&s->frame[0]);
    av_frame_free(&s->frame[1]);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

const FFFilter ff_af_asoftclip = {
    .p.name         = "asoftclip",
    .p.description  = NULL_IF_CONFIG_SMALL("Audio Soft Clipper."),
    .p.priv_class   = &asoftclip_class,
    .p.flags        = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC |
                      AVFILTER_FLAG_SLICE_THREADS,
    .priv_size      = sizeof(ASoftClipContext),
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_SAMPLEFMTS(AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_DBLP),
    .uninit         = uninit,
    .process_command = ff_filter_process_command,
};
