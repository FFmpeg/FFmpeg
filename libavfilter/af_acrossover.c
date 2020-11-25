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
#include "libavutil/internal.h"
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

#define MAX_SPLITS 16
#define MAX_BANDS MAX_SPLITS + 1

typedef struct BiquadContext {
    double a0, a1, a2;
    double b1, b2;
    double i1, i2;
    double o1, o2;
} BiquadContext;

typedef struct CrossoverChannel {
    BiquadContext lp[MAX_BANDS][16];
    BiquadContext hp[MAX_BANDS][16];
} CrossoverChannel;

typedef struct AudioCrossoverContext {
    const AVClass *class;

    char *splits_str;
    int order_opt;

    int order;
    int filter_count;
    int nb_splits;
    float *splits;

    CrossoverChannel *xover;

    AVFrame *input_frame;
    AVFrame *frames[MAX_BANDS];
} AudioCrossoverContext;

#define OFFSET(x) offsetof(AudioCrossoverContext, x)
#define AF AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM

static const AVOption acrossover_options[] = {
    { "split", "set split frequencies", OFFSET(splits_str), AV_OPT_TYPE_STRING, {.str="500"}, 0, 0, AF },
    { "order", "set order",             OFFSET(order_opt),  AV_OPT_TYPE_INT,    {.i64=1},     0, 4, AF, "m" },
    { "2nd",   "2nd order",             0,                  AV_OPT_TYPE_CONST,  {.i64=0},     0, 0, AF, "m" },
    { "4th",   "4th order",             0,                  AV_OPT_TYPE_CONST,  {.i64=1},     0, 0, AF, "m" },
    { "8th",   "8th order",             0,                  AV_OPT_TYPE_CONST,  {.i64=2},     0, 0, AF, "m" },
    { "12th",  "12th order",            0,                  AV_OPT_TYPE_CONST,  {.i64=3},     0, 0, AF, "m" },
    { "16th",  "16th order",            0,                  AV_OPT_TYPE_CONST,  {.i64=4},     0, 0, AF, "m" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(acrossover);

static av_cold int init(AVFilterContext *ctx)
{
    AudioCrossoverContext *s = ctx->priv;
    char *p, *arg, *saveptr = NULL;
    int i, ret = 0;

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

static void set_lp(BiquadContext *b, double fc, double q, double sr)
{
    double thetac = 2.0 * M_PI * fc / sr;
    double d = 1.0 / q;
    double beta = 0.5 * (1.0 - (d / 2.0) * sin(thetac)) / (1.0 + (d / 2.0) * sin(thetac));
    double gamma = (0.5 + beta) * cos(thetac);

    b->a0 = (0.5 + beta - gamma) / 2.0;
    b->a1 = 0.5 + beta - gamma;
    b->a2 = b->a1 / 2.0;
    b->b1 = -2.0 * gamma;
    b->b2 = 2.0 * beta;
}

static void set_hp(BiquadContext *b, double fc, double q, double sr)
{
    double thetac = 2.0 * M_PI * fc / sr;
    double d = 1.0 / q;
    double beta = 0.5 * (1.0 - (d / 2.0) * sin(thetac)) / (1.0 + (d / 2.0) * sin(thetac));
    double gamma = (0.5 + beta) * cos(thetac);

    b->a0 = (0.5 + beta + gamma) / 2.0;
    b->a1 = -(0.5 + beta + gamma);
    b->a2 = b->a0;
    b->b1 = -2.0 * gamma;
    b->b2 = 2.0 * beta;
}

static void calc_q_factors(int order, double *q)
{
    int num = 1, den = 4 * order;

    for (int i = 0; i < order; i++) {
        q[i] = fabs(1. / (2. * cos(num * M_PI / den)));
        num += 2;
    }
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioCrossoverContext *s = ctx->priv;
    int sample_rate = inlink->sample_rate;
    double q[16] = { 0.5 };

    s->xover = av_calloc(inlink->channels, sizeof(*s->xover));
    if (!s->xover)
        return AVERROR(ENOMEM);

    s->order = FFMAX(2, s->order_opt * 4);
    s->filter_count = s->order / 2;
    calc_q_factors(s->filter_count / 2, q + (s->order == 2));

    for (int ch = 0; ch < inlink->channels; ch++) {
        for (int band = 0; band <= s->nb_splits; band++) {
            for (int n = 0; n < s->filter_count; n++) {
                const int idx = (n + (s->order == 2)) / 2;

                set_lp(&s->xover[ch].lp[band][n], s->splits[band], q[idx], sample_rate);
                set_hp(&s->xover[ch].hp[band][n], s->splits[band], q[idx], sample_rate);
            }
        }
    }

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_DBLP,
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

static double biquad_process(BiquadContext *b, double in)
{
    double out = in * b->a0 + b->i1 * b->a1 + b->i2 * b->a2 - b->o1 * b->b1 - b->o2 * b->b2;

    b->i2 = b->i1;
    b->o2 = b->o1;
    b->i1 = in;
    b->o1 = out;

    return out;
}

static int filter_channels(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    AudioCrossoverContext *s = ctx->priv;
    AVFrame *in = s->input_frame;
    AVFrame **frames = s->frames;
    const int start = (in->channels * jobnr) / nb_jobs;
    const int end = (in->channels * (jobnr+1)) / nb_jobs;
    int f, band;

    for (int ch = start; ch < end; ch++) {
        const double *src = (const double *)in->extended_data[ch];
        CrossoverChannel *xover = &s->xover[ch];

        for (int i = 0; i < in->nb_samples; i++) {
            double sample = src[i], lo, hi;

            for (band = 0; band < ctx->nb_outputs; band++) {
                double *dst = (double *)frames[band]->extended_data[ch];

                lo = sample;
                hi = sample;
                for (f = 0; band + 1 < ctx->nb_outputs && f < s->filter_count; f++) {
                    BiquadContext *lp = &xover->lp[band][f];
                    lo = biquad_process(lp, lo);
                }

                for (f = 0; band + 1 < ctx->nb_outputs && f < s->filter_count; f++) {
                    BiquadContext *hp = &xover->hp[band][f];
                    hi = biquad_process(hp, hi);
                }

                dst[i] = lo;

                sample = hi;
            }
        }
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
    ctx->internal->execute(ctx, filter_channels, NULL, NULL, FFMIN(inlink->channels,
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
