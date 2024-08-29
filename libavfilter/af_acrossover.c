/*
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
 * Crossover filter
 *
 * Split an audio stream into several bands.
 */

#include "libavutil/attributes.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/float_dsp.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"

#define MAX_SPLITS 16
#define MAX_BANDS MAX_SPLITS + 1

#define B0 0
#define B1 1
#define B2 2
#define A1 3
#define A2 4

typedef struct BiquadCoeffs {
    double cd[5];
    float cf[5];
} BiquadCoeffs;

typedef struct AudioCrossoverContext {
    const AVClass *class;

    char *splits_str;
    char *gains_str;
    int order_opt;
    float level_in;
    int precision;

    int order;
    int filter_count;
    int first_order;
    int ap_filter_count;
    int nb_splits;
    float splits[MAX_SPLITS];

    float gains[MAX_BANDS];

    BiquadCoeffs lp[MAX_BANDS][20];
    BiquadCoeffs hp[MAX_BANDS][20];
    BiquadCoeffs ap[MAX_BANDS][20];

    AVFrame *xover;

    AVFrame *frames[MAX_BANDS];

    int (*filter_channels)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);

    AVFloatDSPContext *fdsp;
} AudioCrossoverContext;

#define OFFSET(x) offsetof(AudioCrossoverContext, x)
#define AF AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM

static const AVOption acrossover_options[] = {
    { "split", "set split frequencies", OFFSET(splits_str), AV_OPT_TYPE_STRING, {.str="500"}, 0, 0, AF },
    { "order", "set filter order",      OFFSET(order_opt),  AV_OPT_TYPE_INT,    {.i64=1},     0, 9, AF, .unit = "m" },
    { "2nd",   "2nd order (12 dB/8ve)", 0,                  AV_OPT_TYPE_CONST,  {.i64=0},     0, 0, AF, .unit = "m" },
    { "4th",   "4th order (24 dB/8ve)", 0,                  AV_OPT_TYPE_CONST,  {.i64=1},     0, 0, AF, .unit = "m" },
    { "6th",   "6th order (36 dB/8ve)", 0,                  AV_OPT_TYPE_CONST,  {.i64=2},     0, 0, AF, .unit = "m" },
    { "8th",   "8th order (48 dB/8ve)", 0,                  AV_OPT_TYPE_CONST,  {.i64=3},     0, 0, AF, .unit = "m" },
    { "10th",  "10th order (60 dB/8ve)",0,                  AV_OPT_TYPE_CONST,  {.i64=4},     0, 0, AF, .unit = "m" },
    { "12th",  "12th order (72 dB/8ve)",0,                  AV_OPT_TYPE_CONST,  {.i64=5},     0, 0, AF, .unit = "m" },
    { "14th",  "14th order (84 dB/8ve)",0,                  AV_OPT_TYPE_CONST,  {.i64=6},     0, 0, AF, .unit = "m" },
    { "16th",  "16th order (96 dB/8ve)",0,                  AV_OPT_TYPE_CONST,  {.i64=7},     0, 0, AF, .unit = "m" },
    { "18th",  "18th order (108 dB/8ve)",0,                 AV_OPT_TYPE_CONST,  {.i64=8},     0, 0, AF, .unit = "m" },
    { "20th",  "20th order (120 dB/8ve)",0,                 AV_OPT_TYPE_CONST,  {.i64=9},     0, 0, AF, .unit = "m" },
    { "level", "set input gain",        OFFSET(level_in),   AV_OPT_TYPE_FLOAT,  {.dbl=1},     0, 1, AF },
    { "gain",  "set output bands gain", OFFSET(gains_str),  AV_OPT_TYPE_STRING, {.str="1.f"}, 0, 0, AF },
    { "precision",  "set processing precision", OFFSET(precision),   AV_OPT_TYPE_INT,   {.i64=0}, 0, 2, AF, .unit = "precision" },
    {  "auto",  "set auto processing precision",                  0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, AF, .unit = "precision" },
    {  "float", "set single-floating point processing precision", 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, AF, .unit = "precision" },
    {  "double","set double-floating point processing precision", 0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, AF, .unit = "precision" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(acrossover);

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    const AudioCrossoverContext *s = ctx->priv;
    static const enum AVSampleFormat auto_sample_fmts[] = {
        AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_DBLP,
        AV_SAMPLE_FMT_NONE
    };
    enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_NONE
    };
    const enum AVSampleFormat *sample_fmts_list = sample_fmts;
    int ret;

    switch (s->precision) {
    case 0:
        sample_fmts_list = auto_sample_fmts;
        break;
    case 1:
        sample_fmts[0] = AV_SAMPLE_FMT_FLTP;
        break;
    case 2:
        sample_fmts[0] = AV_SAMPLE_FMT_DBLP;
        break;
    default:
        break;
    }
    ret = ff_set_common_formats_from_list2(ctx, cfg_in, cfg_out, sample_fmts_list);
    if (ret < 0)
        return ret;

    return 0;
}

static int parse_gains(AVFilterContext *ctx)
{
    AudioCrossoverContext *s = ctx->priv;
    char *p, *arg, *saveptr = NULL;
    int i, ret = 0;

    saveptr = NULL;
    p = s->gains_str;
    for (i = 0; i < MAX_BANDS; i++) {
        float gain;
        char c[3] = { 0 };

        if (!(arg = av_strtok(p, " |", &saveptr)))
            break;

        p = NULL;

        if (av_sscanf(arg, "%f%2s", &gain, c) < 1) {
            av_log(ctx, AV_LOG_ERROR, "Invalid syntax for gain[%d].\n", i);
            ret = AVERROR(EINVAL);
            break;
        }

        if (c[0] == 'd' && c[1] == 'B')
            s->gains[i] = expf(gain * M_LN10 / 20.f);
        else
            s->gains[i] = gain;
    }

    for (; i < MAX_BANDS; i++)
        s->gains[i] = 1.f;

    return ret;
}

static av_cold int init(AVFilterContext *ctx)
{
    AudioCrossoverContext *s = ctx->priv;
    char *p, *arg, *saveptr = NULL;
    int i, ret = 0;

    s->fdsp = avpriv_float_dsp_alloc(0);
    if (!s->fdsp)
        return AVERROR(ENOMEM);

    p = s->splits_str;
    for (i = 0; i < MAX_SPLITS; i++) {
        float freq;

        if (!(arg = av_strtok(p, " |", &saveptr)))
            break;

        p = NULL;

        if (av_sscanf(arg, "%f", &freq) != 1) {
            av_log(ctx, AV_LOG_ERROR, "Invalid syntax for frequency[%d].\n", i);
            return AVERROR(EINVAL);
        }
        if (freq <= 0) {
            av_log(ctx, AV_LOG_ERROR, "Frequency %f must be positive number.\n", freq);
            return AVERROR(EINVAL);
        }

        if (i > 0 && freq <= s->splits[i-1]) {
            av_log(ctx, AV_LOG_ERROR, "Frequency %f must be in increasing order.\n", freq);
            return AVERROR(EINVAL);
        }

        s->splits[i] = freq;
    }

    s->nb_splits = i;

    ret = parse_gains(ctx);
    if (ret < 0)
        return ret;

    for (i = 0; i <= s->nb_splits; i++) {
        AVFilterPad pad  = { 0 };
        char *name;

        pad.type = AVMEDIA_TYPE_AUDIO;
        name = av_asprintf("out%d", ctx->nb_outputs);
        if (!name)
            return AVERROR(ENOMEM);
        pad.name = name;

        if ((ret = ff_append_outpad_free_name(ctx, &pad)) < 0)
            return ret;
    }

    return ret;
}

static void set_lp(BiquadCoeffs *b, double fc, double q, double sr)
{
    double omega = 2. * M_PI * fc / sr;
    double cosine = cos(omega);
    double alpha = sin(omega) / (2. * q);

    double b0 = (1. - cosine) / 2.;
    double b1 = 1. - cosine;
    double b2 = (1. - cosine) / 2.;
    double a0 = 1. + alpha;
    double a1 = -2. * cosine;
    double a2 = 1. - alpha;

    b->cd[B0] =  b0 / a0;
    b->cd[B1] =  b1 / a0;
    b->cd[B2] =  b2 / a0;
    b->cd[A1] = -a1 / a0;
    b->cd[A2] = -a2 / a0;

    b->cf[B0] = b->cd[B0];
    b->cf[B1] = b->cd[B1];
    b->cf[B2] = b->cd[B2];
    b->cf[A1] = b->cd[A1];
    b->cf[A2] = b->cd[A2];
}

static void set_hp(BiquadCoeffs *b, double fc, double q, double sr)
{
    double omega = 2. * M_PI * fc / sr;
    double cosine = cos(omega);
    double alpha = sin(omega) / (2. * q);

    double b0 = (1. + cosine) / 2.;
    double b1 = -1. - cosine;
    double b2 = (1. + cosine) / 2.;
    double a0 = 1. + alpha;
    double a1 = -2. * cosine;
    double a2 = 1. - alpha;

    b->cd[B0] =  b0 / a0;
    b->cd[B1] =  b1 / a0;
    b->cd[B2] =  b2 / a0;
    b->cd[A1] = -a1 / a0;
    b->cd[A2] = -a2 / a0;

    b->cf[B0] = b->cd[B0];
    b->cf[B1] = b->cd[B1];
    b->cf[B2] = b->cd[B2];
    b->cf[A1] = b->cd[A1];
    b->cf[A2] = b->cd[A2];
}

static void set_ap(BiquadCoeffs *b, double fc, double q, double sr)
{
    double omega = 2. * M_PI * fc / sr;
    double cosine = cos(omega);
    double alpha = sin(omega) / (2. * q);

    double a0 = 1. + alpha;
    double a1 = -2. * cosine;
    double a2 = 1. - alpha;
    double b0 = a2;
    double b1 = a1;
    double b2 = a0;

    b->cd[B0] =  b0 / a0;
    b->cd[B1] =  b1 / a0;
    b->cd[B2] =  b2 / a0;
    b->cd[A1] = -a1 / a0;
    b->cd[A2] = -a2 / a0;

    b->cf[B0] = b->cd[B0];
    b->cf[B1] = b->cd[B1];
    b->cf[B2] = b->cd[B2];
    b->cf[A1] = b->cd[A1];
    b->cf[A2] = b->cd[A2];
}

static void set_ap1(BiquadCoeffs *b, double fc, double sr)
{
    double omega = 2. * M_PI * fc / sr;

    b->cd[A1] = exp(-omega);
    b->cd[A2] = 0.;
    b->cd[B0] = -b->cd[A1];
    b->cd[B1] = 1.;
    b->cd[B2] = 0.;

    b->cf[B0] = b->cd[B0];
    b->cf[B1] = b->cd[B1];
    b->cf[B2] = b->cd[B2];
    b->cf[A1] = b->cd[A1];
    b->cf[A2] = b->cd[A2];
}

static void calc_q_factors(int order, double *q)
{
    double n = order / 2.;

    for (int i = 0; i < n / 2; i++)
        q[i] = 1. / (-2. * cos(M_PI * (2. * (i + 1) + n - 1.) / (2. * n)));
}

#define BIQUAD_PROCESS(name, type)                             \
static void biquad_process_## name(const type *const c,        \
                                   type *b,                    \
                                   type *dst, const type *src, \
                                   int nb_samples)             \
{                                                              \
    const type b0 = c[B0];                                     \
    const type b1 = c[B1];                                     \
    const type b2 = c[B2];                                     \
    const type a1 = c[A1];                                     \
    const type a2 = c[A2];                                     \
    type z1 = b[0];                                            \
    type z2 = b[1];                                            \
                                                               \
    for (int n = 0; n + 1 < nb_samples; n++) {                 \
        type in = src[n];                                      \
        type out;                                              \
                                                               \
        out = in * b0 + z1;                                    \
        z1 = b1 * in + z2 + a1 * out;                          \
        z2 = b2 * in + a2 * out;                               \
        dst[n] = out;                                          \
                                                               \
        n++;                                                   \
        in = src[n];                                           \
        out = in * b0 + z1;                                    \
        z1 = b1 * in + z2 + a1 * out;                          \
        z2 = b2 * in + a2 * out;                               \
        dst[n] = out;                                          \
    }                                                          \
                                                               \
    if (nb_samples & 1) {                                      \
        const int n = nb_samples - 1;                          \
        const type in = src[n];                                \
        type out;                                              \
                                                               \
        out = in * b0 + z1;                                    \
        z1 = b1 * in + z2 + a1 * out;                          \
        z2 = b2 * in + a2 * out;                               \
        dst[n] = out;                                          \
    }                                                          \
                                                               \
    b[0] = z1;                                                 \
    b[1] = z2;                                                 \
}

BIQUAD_PROCESS(fltp, float)
BIQUAD_PROCESS(dblp, double)

#define XOVER_PROCESS(name, type, one, ff)                                                  \
static int filter_channels_## name(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs) \
{                                                                                           \
    AudioCrossoverContext *s = ctx->priv;                                                   \
    AVFrame *in = arg;                                                           \
    AVFrame **frames = s->frames;                                                           \
    const int start = (in->ch_layout.nb_channels * jobnr) / nb_jobs;                        \
    const int end = (in->ch_layout.nb_channels * (jobnr+1)) / nb_jobs;                      \
    const int nb_samples = in->nb_samples;                                                  \
    const int nb_outs = ctx->nb_outputs;                                                    \
    const int first_order = s->first_order;                                                 \
                                                                                            \
    for (int ch = start; ch < end; ch++) {                                                  \
        const type *src = (const type *)in->extended_data[ch];                              \
        type *xover = (type *)s->xover->extended_data[ch];                                  \
                                                                                            \
        s->fdsp->vector_## ff ##mul_scalar((type *)frames[0]->extended_data[ch], src,       \
                                    s->level_in, FFALIGN(nb_samples, sizeof(type)));        \
                                                                                            \
        for (int band = 0; band < nb_outs; band++) {                                        \
            for (int f = 0; band + 1 < nb_outs && f < s->filter_count; f++) {               \
                const type *prv = (const type *)frames[band]->extended_data[ch];            \
                type *dst = (type *)frames[band + 1]->extended_data[ch];                    \
                const type *hsrc = f == 0 ? prv : dst;                                      \
                type *hp = xover + nb_outs * 20 + band * 20 + f * 2;                        \
                const type *const hpc = (type *)&s->hp[band][f].c ## ff;                    \
                                                                                            \
                biquad_process_## name(hpc, hp, dst, hsrc, nb_samples);                     \
            }                                                                               \
                                                                                            \
            for (int f = 0; band + 1 < nb_outs && f < s->filter_count; f++) {               \
                type *dst = (type *)frames[band]->extended_data[ch];                        \
                const type *lsrc = dst;                                                     \
                type *lp = xover + band * 20 + f * 2;                                       \
                const type *const lpc = (type *)&s->lp[band][f].c ## ff;                    \
                                                                                            \
                biquad_process_## name(lpc, lp, dst, lsrc, nb_samples);                     \
            }                                                                               \
                                                                                            \
            for (int aband = band + 1; aband + 1 < nb_outs; aband++) {                      \
                if (first_order) {                                                          \
                    const type *asrc = (const type *)frames[band]->extended_data[ch];       \
                    type *dst = (type *)frames[band]->extended_data[ch];                    \
                    type *ap = xover + nb_outs * 40 + (aband * nb_outs + band) * 20;        \
                    const type *const apc = (type *)&s->ap[aband][0].c ## ff;               \
                                                                                            \
                    biquad_process_## name(apc, ap, dst, asrc, nb_samples);                 \
                }                                                                           \
                                                                                            \
                for (int f = first_order; f < s->ap_filter_count; f++) {                    \
                    const type *asrc = (const type *)frames[band]->extended_data[ch];       \
                    type *dst = (type *)frames[band]->extended_data[ch];                    \
                    type *ap = xover + nb_outs * 40 + (aband * nb_outs + band) * 20 + f * 2;\
                    const type *const apc = (type *)&s->ap[aband][f].c ## ff;               \
                                                                                            \
                    biquad_process_## name(apc, ap, dst, asrc, nb_samples);                 \
                }                                                                           \
            }                                                                               \
        }                                                                                   \
                                                                                            \
        for (int band = 0; band < nb_outs; band++) {                                        \
            const type gain = s->gains[band] * ((band & 1 && first_order) ? -one : one);    \
            type *dst = (type *)frames[band]->extended_data[ch];                            \
                                                                                            \
            s->fdsp->vector_## ff ##mul_scalar(dst, dst, gain,                              \
                                               FFALIGN(nb_samples, sizeof(type)));          \
        }                                                                                   \
    }                                                                                       \
                                                                                            \
    return 0;                                                                               \
}

XOVER_PROCESS(fltp, float, 1.f, f)
XOVER_PROCESS(dblp, double, 1.0, d)

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioCrossoverContext *s = ctx->priv;
    int sample_rate = inlink->sample_rate;
    double q[16];

    s->order = (s->order_opt + 1) * 2;
    s->filter_count = s->order / 2;
    s->first_order = s->filter_count & 1;
    s->ap_filter_count = s->filter_count / 2 + s->first_order;
    calc_q_factors(s->order, q);

    for (int band = 0; band <= s->nb_splits; band++) {
        if (s->first_order) {
            set_lp(&s->lp[band][0], s->splits[band], 0.5, sample_rate);
            set_hp(&s->hp[band][0], s->splits[band], 0.5, sample_rate);
        }

        for (int n = s->first_order; n < s->filter_count; n++) {
            const int idx = s->filter_count / 2 - ((n + s->first_order) / 2 - s->first_order) - 1;

            set_lp(&s->lp[band][n], s->splits[band], q[idx], sample_rate);
            set_hp(&s->hp[band][n], s->splits[band], q[idx], sample_rate);
        }

        if (s->first_order)
            set_ap1(&s->ap[band][0], s->splits[band], sample_rate);

        for (int n = s->first_order; n < s->ap_filter_count; n++) {
            const int idx = (s->filter_count / 2 - ((n * 2 + s->first_order) / 2 - s->first_order) - 1);

            set_ap(&s->ap[band][n], s->splits[band], q[idx], sample_rate);
        }
    }

    switch (inlink->format) {
    case AV_SAMPLE_FMT_FLTP: s->filter_channels = filter_channels_fltp; break;
    case AV_SAMPLE_FMT_DBLP: s->filter_channels = filter_channels_dblp; break;
    default: return AVERROR_BUG;
    }

    s->xover = ff_get_audio_buffer(inlink, 2 * (ctx->nb_outputs * 10 + ctx->nb_outputs * 10 +
                                                ctx->nb_outputs * ctx->nb_outputs * 10));
    if (!s->xover)
        return AVERROR(ENOMEM);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AudioCrossoverContext *s = ctx->priv;
    AVFrame **frames = s->frames;
    int ret = 0;

    for (int i = 0; i < ctx->nb_outputs; i++) {
        frames[i] = ff_get_audio_buffer(ctx->outputs[i], in->nb_samples);
        if (!frames[i]) {
            ret = AVERROR(ENOMEM);
            break;
        }

        frames[i]->pts = in->pts;
    }

    if (ret < 0)
        goto fail;

    ff_filter_execute(ctx, s->filter_channels, in, NULL,
                      FFMIN(inlink->ch_layout.nb_channels, ff_filter_get_nb_threads(ctx)));

    for (int i = 0; i < ctx->nb_outputs; i++) {
        if (ff_outlink_get_status(ctx->outputs[i])) {
            av_frame_free(&frames[i]);
            continue;
        }

        ret = ff_filter_frame(ctx->outputs[i], frames[i]);
        frames[i] = NULL;
        if (ret < 0)
            break;
    }

fail:
    for (int i = 0; i < ctx->nb_outputs; i++)
        av_frame_free(&frames[i]);

    return ret;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    int status, ret;
    AVFrame *in;
    int64_t pts;

    for (int i = 0; i < ctx->nb_outputs; i++) {
        FF_FILTER_FORWARD_STATUS_BACK_ALL(ctx->outputs[i], ctx);
    }

    ret = ff_inlink_consume_frame(inlink, &in);
    if (ret < 0)
        return ret;
    if (ret > 0) {
        ret = filter_frame(inlink, in);
        av_frame_free(&in);
        if (ret < 0)
            return ret;
    }

    if (ff_inlink_acknowledge_status(inlink, &status, &pts)) {
        for (int i = 0; i < ctx->nb_outputs; i++) {
            if (ff_outlink_get_status(ctx->outputs[i]))
                continue;
            ff_outlink_set_status(ctx->outputs[i], status, pts);
        }
        return 0;
    }

    for (int i = 0; i < ctx->nb_outputs; i++) {
        if (ff_outlink_get_status(ctx->outputs[i]))
            continue;

        if (ff_outlink_frame_wanted(ctx->outputs[i])) {
            ff_inlink_request_frame(inlink);
            return 0;
        }
    }

    return FFERROR_NOT_READY;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioCrossoverContext *s = ctx->priv;

    av_freep(&s->fdsp);
    av_frame_free(&s->xover);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
    },
};

const AVFilter ff_af_acrossover = {
    .name           = "acrossover",
    .description    = NULL_IF_CONFIG_SMALL("Split audio into per-bands streams."),
    .priv_size      = sizeof(AudioCrossoverContext),
    .priv_class     = &acrossover_class,
    .init           = init,
    .activate       = activate,
    .uninit         = uninit,
    FILTER_INPUTS(inputs),
    .outputs        = NULL,
    FILTER_QUERY_FUNC2(query_formats),
    .flags          = AVFILTER_FLAG_DYNAMIC_OUTPUTS |
                      AVFILTER_FLAG_SLICE_THREADS,
};
