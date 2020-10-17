/*
 * Copyright (c) Paul B Mahol
 * Copyright (c) Laurent de Soras, 2005
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
#include "libavutil/opt.h"
#include "avfilter.h"
#include "audio.h"
#include "formats.h"

#define NB_COEFS 16

typedef struct AFreqShift {
    const AVClass *class;

    double shift;

    double c[NB_COEFS];

    int64_t in_samples;

    AVFrame *i1, *o1;
    AVFrame *i2, *o2;

    void (*filter_channel)(AVFilterContext *ctx,
                           int nb_samples,
                           int sample_rate,
                           const double *src, double *dst,
                           double *i1, double *o1,
                           double *i2, double *o2);
} AFreqShift;

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_DBLP,
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

static void pfilter_channel(AVFilterContext *ctx,
                            int nb_samples,
                            int sample_rate,
                            const double *src, double *dst,
                            double *i1, double *o1,
                            double *i2, double *o2)
{
    AFreqShift *s = ctx->priv;
    double *c = s->c;
    double shift = s->shift * M_PI;
    double cos_theta = cos(shift);
    double sin_theta = sin(shift);

    for (int n = 0; n < nb_samples; n++) {
        double xn1 = src[n], xn2 = src[n];
        double I, Q;

        for (int j = 0; j < NB_COEFS / 2; j++) {
            I = c[j] * (xn1 + o2[j]) - i2[j];
            i2[j] = i1[j];
            i1[j] = xn1;
            o2[j] = o1[j];
            o1[j] = I;
            xn1 = I;
        }

        for (int j = NB_COEFS / 2; j < NB_COEFS; j++) {
            Q = c[j] * (xn2 + o2[j]) - i2[j];
            i2[j] = i1[j];
            i1[j] = xn2;
            o2[j] = o1[j];
            o1[j] = Q;
            xn2 = Q;
        }
        Q = o2[NB_COEFS - 1];

        dst[n] = I * cos_theta - Q * sin_theta;
    }
}

static void ffilter_channel(AVFilterContext *ctx,
                            int nb_samples,
                            int sample_rate,
                            const double *src, double *dst,
                            double *i1, double *o1,
                            double *i2, double *o2)
{
    AFreqShift *s = ctx->priv;
    double *c = s->c;
    double ts = 1. / sample_rate;
    double shift = s->shift;
    int64_t N = s->in_samples;

    for (int n = 0; n < nb_samples; n++) {
        double xn1 = src[n], xn2 = src[n];
        double I, Q, theta;

        for (int j = 0; j < NB_COEFS / 2; j++) {
            I = c[j] * (xn1 + o2[j]) - i2[j];
            i2[j] = i1[j];
            i1[j] = xn1;
            o2[j] = o1[j];
            o1[j] = I;
            xn1 = I;
        }

        for (int j = NB_COEFS / 2; j < NB_COEFS; j++) {
            Q = c[j] * (xn2 + o2[j]) - i2[j];
            i2[j] = i1[j];
            i1[j] = xn2;
            o2[j] = o1[j];
            o1[j] = Q;
            xn2 = Q;
        }
        Q = o2[NB_COEFS - 1];

        theta = 2. * M_PI * fmod(shift * (N + n) * ts, 1.);
        dst[n] = I * cos(theta) - Q * sin(theta);
    }
}

static void compute_transition_param(double *K, double *Q, double transition)
{
    double kksqrt, e, e2, e4, k, q;

    k  = tan((1. - transition * 2.) * M_PI / 4.);
    k *= k;
    kksqrt = pow(1 - k * k, 0.25);
    e = 0.5 * (1. - kksqrt) / (1. + kksqrt);
    e2 = e * e;
    e4 = e2 * e2;
    q = e * (1. + e4 * (2. + e4 * (15. + 150. * e4)));

    *Q = q;
    *K = k;
}

static double ipowp(double x, int64_t n)
{
    double z = 1.;

    while (n != 0) {
        if (n & 1)
            z *= x;
        n >>= 1;
        x *= x;
    }

    return z;
}

static double compute_acc_num(double q, int order, int c)
{
    int64_t i = 0;
    int j = 1;
    double acc = 0.;
    double q_ii1;

    do {
        q_ii1  = ipowp(q, i * (i + 1));
        q_ii1 *= sin((i * 2 + 1) * c * M_PI / order) * j;
        acc   += q_ii1;

        j = -j;
        i++;
    } while (fabs(q_ii1) > 1e-100);

    return acc;
}

static double compute_acc_den(double q, int order, int c)
{
    int64_t i = 1;
    int j = -1;
    double acc = 0.;
    double q_i2;

    do {
        q_i2  = ipowp(q, i * i);
        q_i2 *= cos(i * 2 * c * M_PI / order) * j;
        acc  += q_i2;

        j = -j;
        i++;
    } while (fabs(q_i2) > 1e-100);

    return acc;
}

static double compute_coef(int index, double k, double q, int order)
{
    const int    c    = index + 1;
    const double num  = compute_acc_num(q, order, c) * pow(q, 0.25);
    const double den  = compute_acc_den(q, order, c) + 0.5;
    const double ww   = num / den;
    const double wwsq = ww * ww;

    const double x    = sqrt((1 - wwsq * k) * (1 - wwsq / k)) / (1 + wwsq);
    const double coef = (1 - x) / (1 + x);

    return coef;
}

static void compute_coefs(double *coef_arr, int nbr_coefs, double transition)
{
    const int order = nbr_coefs * 2 + 1;
    double k, q;

    compute_transition_param(&k, &q, transition);

    for (int n = 0; n < nbr_coefs; n++)
        coef_arr[(n / 2) + (n & 1) * nbr_coefs / 2] = compute_coef(n, k, q, order);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AFreqShift *s = ctx->priv;

    compute_coefs(s->c, NB_COEFS, 2. * 20. / inlink->sample_rate);

    s->i1 = ff_get_audio_buffer(inlink, NB_COEFS);
    s->o1 = ff_get_audio_buffer(inlink, NB_COEFS);
    s->i2 = ff_get_audio_buffer(inlink, NB_COEFS);
    s->o2 = ff_get_audio_buffer(inlink, NB_COEFS);
    if (!s->i1 || !s->o1 || !s->i2 || !s->o2)
        return AVERROR(ENOMEM);

    if (!strcmp(ctx->filter->name, "afreqshift"))
        s->filter_channel = ffilter_channel;
    else
        s->filter_channel = pfilter_channel;

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AFreqShift *s = ctx->priv;
    AVFrame *out;

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

    for (int ch = 0; ch < in->channels; ch++) {
        s->filter_channel(ctx, in->nb_samples,
                          in->sample_rate,
                          (const double *)in->extended_data[ch],
                          (double *)out->extended_data[ch],
                          (double *)s->i1->extended_data[ch],
                          (double *)s->o1->extended_data[ch],
                          (double *)s->i2->extended_data[ch],
                          (double *)s->o2->extended_data[ch]);
    }

    s->in_samples += in->nb_samples;

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AFreqShift *s = ctx->priv;

    av_frame_free(&s->i1);
    av_frame_free(&s->o1);
    av_frame_free(&s->i2);
    av_frame_free(&s->o2);
}

#define OFFSET(x) offsetof(AFreqShift, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption afreqshift_options[] = {
    { "shift", "set frequency shift", OFFSET(shift), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -INT_MAX, INT_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(afreqshift);

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_afreqshift = {
    .name            = "afreqshift",
    .description     = NULL_IF_CONFIG_SMALL("Apply frequency shifting to input audio."),
    .query_formats   = query_formats,
    .priv_size       = sizeof(AFreqShift),
    .priv_class      = &afreqshift_class,
    .uninit          = uninit,
    .inputs          = inputs,
    .outputs         = outputs,
    .process_command = ff_filter_process_command,
};

static const AVOption aphaseshift_options[] = {
    { "shift", "set phase shift", OFFSET(shift), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1.0, 1.0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(aphaseshift);

AVFilter ff_af_aphaseshift = {
    .name            = "aphaseshift",
    .description     = NULL_IF_CONFIG_SMALL("Apply phase shifting to input audio."),
    .query_formats   = query_formats,
    .priv_size       = sizeof(AFreqShift),
    .priv_class      = &aphaseshift_class,
    .uninit          = uninit,
    .inputs          = inputs,
    .outputs         = outputs,
    .process_command = ff_filter_process_command,
};
