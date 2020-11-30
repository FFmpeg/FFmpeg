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
#include "libavutil/eval.h"
#include "libavutil/float_dsp.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

#define MAX_SPLITS 16
#define MAX_BANDS MAX_SPLITS + 1

typedef struct BiquadCoeffs {
    double b0, b1, b2;
    double a1, a2;
} BiquadCoeffs;

typedef struct BiquadContext {
    double z1, z2;
} BiquadContext;

typedef struct CrossoverChannel {
    BiquadContext lp[MAX_BANDS][20];
    BiquadContext hp[MAX_BANDS][20];
    BiquadContext ap[MAX_BANDS][MAX_BANDS][20];
} CrossoverChannel;

typedef struct AudioCrossoverContext {
    const AVClass *class;

    char *splits_str;
    int order_opt;
    float level_in;

    int order;
    int filter_count;
    int first_order;
    int ap_filter_count;
    int nb_splits;
    float *splits;

    BiquadCoeffs lp[MAX_BANDS][20];
    BiquadCoeffs hp[MAX_BANDS][20];
    BiquadCoeffs ap[MAX_BANDS][20];

    CrossoverChannel *xover;

    AVFrame *input_frame;
    AVFrame *frames[MAX_BANDS];

    int (*filter_channels)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);

    AVFloatDSPContext *fdsp;
} AudioCrossoverContext;

#define OFFSET(x) offsetof(AudioCrossoverContext, x)
#define AF AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM

static const AVOption acrossover_options[] = {
    { "split", "set split frequencies", OFFSET(splits_str), AV_OPT_TYPE_STRING, {.str="500"}, 0, 0, AF },
    { "order", "set order",             OFFSET(order_opt),  AV_OPT_TYPE_INT,    {.i64=1},     0, 9, AF, "m" },
    { "2nd",   "2nd order",             0,                  AV_OPT_TYPE_CONST,  {.i64=0},     0, 0, AF, "m" },
    { "4th",   "4th order",             0,                  AV_OPT_TYPE_CONST,  {.i64=1},     0, 0, AF, "m" },
    { "6th",   "6th order",             0,                  AV_OPT_TYPE_CONST,  {.i64=2},     0, 0, AF, "m" },
    { "8th",   "8th order",             0,                  AV_OPT_TYPE_CONST,  {.i64=3},     0, 0, AF, "m" },
    { "10th",  "10th order",            0,                  AV_OPT_TYPE_CONST,  {.i64=4},     0, 0, AF, "m" },
    { "12th",  "12th order",            0,                  AV_OPT_TYPE_CONST,  {.i64=5},     0, 0, AF, "m" },
    { "14th",  "14th order",            0,                  AV_OPT_TYPE_CONST,  {.i64=6},     0, 0, AF, "m" },
    { "16th",  "16th order",            0,                  AV_OPT_TYPE_CONST,  {.i64=7},     0, 0, AF, "m" },
    { "18th",  "18th order",            0,                  AV_OPT_TYPE_CONST,  {.i64=8},     0, 0, AF, "m" },
    { "20th",  "20th order",            0,                  AV_OPT_TYPE_CONST,  {.i64=9},     0, 0, AF, "m" },
    { "level", "set input gain",        OFFSET(level_in),   AV_OPT_TYPE_FLOAT,  {.dbl=1},     0, 1, AF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(acrossover);

static av_cold int init(AVFilterContext *ctx)
{
    AudioCrossoverContext *s = ctx->priv;
    char *p, *arg, *saveptr = NULL;
    int i, ret = 0;

    s->fdsp = avpriv_float_dsp_alloc(0);
    if (!s->fdsp)
        return AVERROR(ENOMEM);

    s->splits = av_calloc(MAX_SPLITS, sizeof(*s->splits));
    if (!s->splits)
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

    for (i = 0; i <= s->nb_splits; i++) {
        AVFilterPad pad  = { 0 };
        char *name;

        pad.type = AVMEDIA_TYPE_AUDIO;
        name = av_asprintf("out%d", ctx->nb_outputs);
        if (!name)
            return AVERROR(ENOMEM);
        pad.name = name;

        if ((ret = ff_insert_outpad(ctx, i, &pad)) < 0) {
            av_freep(&pad.name);
            return ret;
        }
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

    b->b0 =  b0 / a0;
    b->b1 =  b1 / a0;
    b->b2 =  b2 / a0;
    b->a1 = -a1 / a0;
    b->a2 = -a2 / a0;
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

    b->b0 =  b0 / a0;
    b->b1 =  b1 / a0;
    b->b2 =  b2 / a0;
    b->a1 = -a1 / a0;
    b->a2 = -a2 / a0;
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

    b->b0 =  b0 / a0;
    b->b1 =  b1 / a0;
    b->b2 =  b2 / a0;
    b->a1 = -a1 / a0;
    b->a2 = -a2 / a0;
}

static void set_ap1(BiquadCoeffs *b, double fc, double sr)
{
    double omega = 2. * M_PI * fc / sr;

    b->a1 = exp(-omega);
    b->a2 = 0.;
    b->b0 = -b->a1;
    b->b1 = 1.;
    b->b2 = 0.;
}

static void calc_q_factors(int order, double *q)
{
    double n = order / 2.;

    for (int i = 0; i < n / 2; i++)
        q[i] = 1. / (-2. * cos(M_PI * (2. * (i + 1) + n - 1.) / (2. * n)));
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_DBLP,
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

#define BIQUAD_PROCESS(name, type)                             \
static void biquad_process_## name(const BiquadCoeffs *const c,\
                                   BiquadContext *b,           \
                                   type *dst, const type *src, \
                                   int nb_samples)             \
{                                                              \
    const type b0 = c->b0;                                     \
    const type b1 = c->b1;                                     \
    const type b2 = c->b2;                                     \
    const type a1 = c->a1;                                     \
    const type a2 = c->a2;                                     \
    type z1 = b->z1;                                           \
    type z2 = b->z2;                                           \
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
    b->z1 = z1;                                                \
    b->z2 = z2;                                                \
}

BIQUAD_PROCESS(fltp, float)
BIQUAD_PROCESS(dblp, double)

#define XOVER_PROCESS(name, type, one, ff)                                                  \
static int filter_channels_## name(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs) \
{                                                                                           \
    AudioCrossoverContext *s = ctx->priv;                                                   \
    AVFrame *in = s->input_frame;                                                           \
    AVFrame **frames = s->frames;                                                           \
    const int start = (in->channels * jobnr) / nb_jobs;                                     \
    const int end = (in->channels * (jobnr+1)) / nb_jobs;                                   \
    const int nb_samples = in->nb_samples;                                                  \
                                                                                            \
    for (int ch = start; ch < end; ch++) {                                                  \
        const type *src = (const type *)in->extended_data[ch];                              \
        CrossoverChannel *xover = &s->xover[ch];                                            \
                                                                                            \
        s->fdsp->vector_## ff ##mul_scalar((type *)frames[0]->extended_data[ch], src,       \
                                    s->level_in, FFALIGN(nb_samples, sizeof(type)));        \
                                                                                            \
        for (int band = 0; band < ctx->nb_outputs; band++) {                                \
            for (int f = 0; band + 1 < ctx->nb_outputs && f < s->filter_count; f++) {       \
                const type *prv = (const type *)frames[band]->extended_data[ch];            \
                type *dst = (type *)frames[band + 1]->extended_data[ch];                    \
                const type *hsrc = f == 0 ? prv : dst;                                      \
                BiquadContext *hp = &xover->hp[band][f];                                    \
                BiquadCoeffs *hpc = &s->hp[band][f];                                        \
                                                                                            \
                biquad_process_## name(hpc, hp, dst, hsrc, nb_samples);                     \
            }                                                                               \
                                                                                            \
            for (int f = 0; band + 1 < ctx->nb_outputs && f < s->filter_count; f++) {       \
                type *dst = (type *)frames[band]->extended_data[ch];                        \
                const type *lsrc = dst;                                                     \
                BiquadContext *lp = &xover->lp[band][f];                                    \
                BiquadCoeffs *lpc = &s->lp[band][f];                                        \
                                                                                            \
                biquad_process_## name(lpc, lp, dst, lsrc, nb_samples);                     \
            }                                                                               \
                                                                                            \
            for (int aband = band + 1; aband + 1 < ctx->nb_outputs; aband++) {              \
                if (s->first_order) {                                                       \
                    const type *asrc = (const type *)frames[band]->extended_data[ch];       \
                    type *dst = (type *)frames[band]->extended_data[ch];                    \
                    BiquadContext *ap = &xover->ap[band][aband][0];                         \
                    BiquadCoeffs *apc = &s->ap[aband][0];                                   \
                                                                                            \
                    biquad_process_## name(apc, ap, dst, asrc, nb_samples);                 \
                }                                                                           \
                                                                                            \
                for (int f = s->first_order; f < s->ap_filter_count; f++) {                 \
                    const type *asrc = (const type *)frames[band]->extended_data[ch];       \
                    type *dst = (type *)frames[band]->extended_data[ch];                    \
                    BiquadContext *ap = &xover->ap[band][aband][f];                         \
                    BiquadCoeffs *apc = &s->ap[aband][f];                                   \
                                                                                            \
                    biquad_process_## name(apc, ap, dst, asrc, nb_samples);                 \
                }                                                                           \
            }                                                                               \
        }                                                                                   \
                                                                                            \
        for (int band = 0; band < ctx->nb_outputs && s->first_order; band++) {              \
            if (band & 1) {                                                                 \
                type *dst = (type *)frames[band]->extended_data[ch];                        \
                s->fdsp->vector_## ff ##mul_scalar(dst, dst, -one,                          \
                                                   FFALIGN(nb_samples, sizeof(type)));      \
            }                                                                               \
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

    s->xover = av_calloc(inlink->channels, sizeof(*s->xover));
    if (!s->xover)
        return AVERROR(ENOMEM);

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
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AudioCrossoverContext *s = ctx->priv;
    AVFrame **frames = s->frames;
    int i, ret = 0;

    for (i = 0; i < ctx->nb_outputs; i++) {
        frames[i] = ff_get_audio_buffer(ctx->outputs[i], in->nb_samples);

        if (!frames[i]) {
            ret = AVERROR(ENOMEM);
            break;
        }

        frames[i]->pts = in->pts;
    }

    if (ret < 0)
        goto fail;

    s->input_frame = in;
    ctx->internal->execute(ctx, s->filter_channels, NULL, NULL, FFMIN(inlink->channels,
                                                                      ff_filter_get_nb_threads(ctx)));

    for (i = 0; i < ctx->nb_outputs; i++) {
        ret = ff_filter_frame(ctx->outputs[i], frames[i]);
        frames[i] = NULL;
        if (ret < 0)
            break;
    }

fail:
    for (i = 0; i < ctx->nb_outputs; i++)
        av_frame_free(&frames[i]);
    av_frame_free(&in);
    s->input_frame = NULL;

    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioCrossoverContext *s = ctx->priv;
    int i;

    av_freep(&s->fdsp);
    av_freep(&s->splits);
    av_freep(&s->xover);

    for (i = 0; i < ctx->nb_outputs; i++)
        av_freep(&ctx->output_pads[i].name);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

AVFilter ff_af_acrossover = {
    .name           = "acrossover",
    .description    = NULL_IF_CONFIG_SMALL("Split audio into per-bands streams."),
    .priv_size      = sizeof(AudioCrossoverContext),
    .priv_class     = &acrossover_class,
    .init           = init,
    .uninit         = uninit,
    .query_formats  = query_formats,
    .inputs         = inputs,
    .outputs        = NULL,
    .flags          = AVFILTER_FLAG_DYNAMIC_OUTPUTS |
                      AVFILTER_FLAG_SLICE_THREADS,
};
