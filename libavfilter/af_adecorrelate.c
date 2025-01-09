/*
 * Copyright (c) 2013-2020 Michael Barbour <barbour.michael.0@gmail.com>
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

#include "libavutil/channel_layout.h"
#include "libavutil/ffmath.h"
#include "libavutil/lfg.h"
#include "libavutil/mem.h"
#include "libavutil/random_seed.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "audio.h"
#include "filters.h"

#define MAX_STAGES 16
#define FILTER_FC  1100.0
#define RT60_LF    0.1
#define RT60_HF    0.008

typedef struct APContext {
    int len, p;
    double *mx, *my;
    double b0, b1, a0, a1;
} APContext;

typedef struct ADecorrelateContext {
    const AVClass *class;

    int stages;
    int64_t seed;

    int nb_channels;
    APContext (*ap)[MAX_STAGES];

    AVLFG c;

    void (*filter_channel)(AVFilterContext *ctx,
                           int channel,
                           AVFrame *in, AVFrame *out);
} ADecorrelateContext;

static int ap_init(APContext *ap, int fs, double delay)
{
    const int delay_samples = lrint(round(delay * fs));
    const double gain_lf = -60.0 / (RT60_LF * fs) * delay_samples;
    const double gain_hf = -60.0 / (RT60_HF * fs) * delay_samples;
    const double w0 = 2.0 * M_PI * FILTER_FC / fs;
    const double t = tan(w0 / 2.0);
    const double g_hf = ff_exp10(gain_hf / 20.0);
    const double gd = ff_exp10((gain_lf-gain_hf) / 20.0);
    const double sgd = sqrt(gd);

    ap->len = delay_samples + 1;
    ap->p = 0;
    ap->mx = av_calloc(ap->len, sizeof(*ap->mx));
    ap->my = av_calloc(ap->len, sizeof(*ap->my));
    if (!ap->mx || !ap->my)
        return AVERROR(ENOMEM);

    ap->a0 = t + sgd;
    ap->a1 = (t - sgd) / ap->a0;
    ap->b0 = (gd*t - sgd) / ap->a0 * g_hf;
    ap->b1 = (gd*t + sgd) / ap->a0 * g_hf;
    ap->a0 = 1.0;

    return 0;
}

static void ap_free(APContext *ap)
{
    av_freep(&ap->mx);
    av_freep(&ap->my);
}

static double ap_run(APContext *ap, double x)
{
    const int i0 = ((ap->p < 1) ? ap->len : ap->p)-1, i_n1 = ap->p, i_n2 = (ap->p+1 >= ap->len) ? 0 : ap->p+1;
    const double r = ap->b1*x + ap->b0*ap->mx[i0] + ap->a1*ap->mx[i_n2] + ap->a0*ap->mx[i_n1] -
                     ap->a1*ap->my[i0] - ap->b0*ap->my[i_n2] - ap->b1*ap->my[i_n1];

    ap->mx[ap->p] = x;
    ap->my[ap->p] = r;
    ap->p = (ap->p+1 >= ap->len) ? 0 : ap->p+1;

    return r;
}

static void filter_channel_dbl(AVFilterContext *ctx, int ch,
                               AVFrame *in, AVFrame *out)
{
    ADecorrelateContext *s = ctx->priv;
    const double *src = (const double *)in->extended_data[ch];
    double *dst = (double *)out->extended_data[ch];
    const int nb_samples = in->nb_samples;
    const int stages = s->stages;
    APContext *ap0 = &s->ap[ch][0];

    for (int n = 0; n < nb_samples; n++) {
        dst[n] = ap_run(ap0, src[n]);
        for (int i = 1; i < stages; i++) {
            APContext *ap = &s->ap[ch][i];

            dst[n] = ap_run(ap, dst[n]);
        }
    }
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ADecorrelateContext *s = ctx->priv;
    int ret;

    if (s->seed == -1)
        s->seed = av_get_random_seed();
    av_lfg_init(&s->c, s->seed);

    s->nb_channels = inlink->ch_layout.nb_channels;
    s->ap = av_calloc(inlink->ch_layout.nb_channels, sizeof(*s->ap));
    if (!s->ap)
        return AVERROR(ENOMEM);

    for (int i = 0; i < inlink->ch_layout.nb_channels; i++) {
        for (int j = 0; j < s->stages; j++) {
            ret = ap_init(&s->ap[i][j], inlink->sample_rate,
                          (double)av_lfg_get(&s->c) / 0xffffffff * 2.2917e-3 + 0.83333e-3);
            if (ret < 0)
                return ret;
        }
    }

    s->filter_channel = filter_channel_dbl;

    return 0;
}

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

static int filter_channels(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ADecorrelateContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *out = td->out;
    AVFrame *in = td->in;
    const int start = (in->ch_layout.nb_channels * jobnr) / nb_jobs;
    const int end = (in->ch_layout.nb_channels * (jobnr+1)) / nb_jobs;

    for (int ch = start; ch < end; ch++)
        s->filter_channel(ctx, ch, in, out);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    ThreadData td;

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_audio_buffer(outlink, in->nb_samples);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    td.in = in; td.out = out;
    ff_filter_execute(ctx, filter_channels, &td, NULL,
                      FFMIN(inlink->ch_layout.nb_channels, ff_filter_get_nb_threads(ctx)));

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ADecorrelateContext *s = ctx->priv;

    if (s->ap) {
        for (int ch = 0; ch < s->nb_channels; ch++) {
            for (int stage = 0; stage < s->stages; stage++)
                ap_free(&s->ap[ch][stage]);
        }
    }

    av_freep(&s->ap);
}

#define OFFSET(x) offsetof(ADecorrelateContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption adecorrelate_options[] = {
    { "stages", "set filtering stages", OFFSET(stages), AV_OPT_TYPE_INT,    {.i64=6},   1, MAX_STAGES, FLAGS },
    { "seed",   "set random seed",      OFFSET(seed),   AV_OPT_TYPE_INT64,  {.i64=-1}, -1,   UINT_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(adecorrelate);

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

const FFFilter ff_af_adecorrelate = {
    .p.name          = "adecorrelate",
    .p.description   = NULL_IF_CONFIG_SMALL("Apply decorrelation to input audio."),
    .p.priv_class    = &adecorrelate_class,
    .p.flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC |
                       AVFILTER_FLAG_SLICE_THREADS,
    .priv_size       = sizeof(ADecorrelateContext),
    .uninit          = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_SINGLE_SAMPLEFMT(AV_SAMPLE_FMT_DBLP),
};
