/*
 * Copyright (c) Markus Schmidt and Christian Holschuh
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

#include "libavutil/opt.h"
#include "avfilter.h"
#include "internal.h"
#include "audio.h"

typedef struct ChannelParams {
    double blend_old, drive_old;
    double rdrive, rbdr, kpa, kpb, kna, knb, ap,
           an, imr, kc, srct, sq, pwrq;
    double prev_med, prev_out;

    double hp[5], lp[5];
    double hw[4][2], lw[2][2];
} ChannelParams;

typedef struct AExciterContext {
    const AVClass *class;

    double level_in;
    double level_out;
    double amount;
    double drive;
    double blend;
    double freq;
    double ceil;
    int listen;

    ChannelParams *cp;
} AExciterContext;

#define OFFSET(x) offsetof(AExciterContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption aexciter_options[] = {
    { "level_in",  "set level in",    OFFSET(level_in),  AV_OPT_TYPE_DOUBLE, {.dbl=1},           0, 64, A },
    { "level_out", "set level out",   OFFSET(level_out), AV_OPT_TYPE_DOUBLE, {.dbl=1},           0, 64, A },
    { "amount", "set amount",         OFFSET(amount),    AV_OPT_TYPE_DOUBLE, {.dbl=1},           0, 64, A },
    { "drive", "set harmonics",       OFFSET(drive),     AV_OPT_TYPE_DOUBLE, {.dbl=8.5},       0.1, 10, A },
    { "blend", "set blend harmonics", OFFSET(blend),     AV_OPT_TYPE_DOUBLE, {.dbl=0},         -10, 10, A },
    { "freq", "set scope",            OFFSET(freq),      AV_OPT_TYPE_DOUBLE, {.dbl=7500},  2000, 12000, A },
    { "ceil", "set ceiling",          OFFSET(ceil),      AV_OPT_TYPE_DOUBLE, {.dbl=9999},  9999, 20000, A },
    { "listen", "enable listen mode", OFFSET(listen),    AV_OPT_TYPE_BOOL,   {.i64=0},        0,     1, A },
    { NULL }
};

AVFILTER_DEFINE_CLASS(aexciter);

static inline double M(double x)
{
    return (fabs(x) > 0.00000001) ? x : 0.0;
}

static inline double D(double x)
{
    x = fabs(x);

    return (x > 0.00000001) ? sqrt(x) : 0.0;
}

static void set_params(ChannelParams *p,
                       double blend, double drive,
                       double srate, double freq,
                       double ceil)
{
    double a0, a1, a2, b0, b1, b2, w0, alpha;

    p->rdrive = 12.0 / drive;
    p->rbdr = p->rdrive / (10.5 - blend) * 780.0 / 33.0;
    p->kpa = D(2.0 * (p->rdrive*p->rdrive) - 1.0) + 1.0;
    p->kpb = (2.0 - p->kpa) / 2.0;
    p->ap = ((p->rdrive*p->rdrive) - p->kpa + 1.0) / 2.0;
    p->kc = p->kpa / D(2.0 * D(2.0 * (p->rdrive*p->rdrive) - 1.0) - 2.0 * p->rdrive*p->rdrive);

    p->srct = (0.1 * srate) / (0.1 * srate + 1.0);
    p->sq = p->kc*p->kc + 1.0;
    p->knb = -1.0 * p->rbdr / D(p->sq);
    p->kna = 2.0 * p->kc * p->rbdr / D(p->sq);
    p->an = p->rbdr*p->rbdr / p->sq;
    p->imr = 2.0 * p->knb + D(2.0 * p->kna + 4.0 * p->an - 1.0);
    p->pwrq = 2.0 / (p->imr + 1.0);

    w0 = 2 * M_PI * freq / srate;
    alpha = sin(w0) / (2. * 0.707);
    a0 =   1 + alpha;
    a1 =  -2 * cos(w0);
    a2 =   1 - alpha;
    b0 =  (1 + cos(w0)) / 2;
    b1 = -(1 + cos(w0));
    b2 =  (1 + cos(w0)) / 2;

    p->hp[0] =-a1 / a0;
    p->hp[1] =-a2 / a0;
    p->hp[2] = b0 / a0;
    p->hp[3] = b1 / a0;
    p->hp[4] = b2 / a0;

    w0 = 2 * M_PI * ceil / srate;
    alpha = sin(w0) / (2. * 0.707);
    a0 =  1 + alpha;
    a1 = -2 * cos(w0);
    a2 =  1 - alpha;
    b0 = (1 - cos(w0)) / 2;
    b1 =  1 - cos(w0);
    b2 = (1 - cos(w0)) / 2;

    p->lp[0] =-a1 / a0;
    p->lp[1] =-a2 / a0;
    p->lp[2] = b0 / a0;
    p->lp[3] = b1 / a0;
    p->lp[4] = b2 / a0;
}

static double bprocess(double in, const double *const c,
                       double *w1, double *w2)
{
    double out = c[2] * in + *w1;

    *w1 = c[3] * in + *w2 + c[0] * out;
    *w2 = c[4] * in + c[1] * out;

    return out;
}

static double distortion_process(AExciterContext *s, ChannelParams *p, double in)
{
    double proc = in, med;

    proc = bprocess(proc, p->hp, &p->hw[0][0], &p->hw[0][1]);
    proc = bprocess(proc, p->hp, &p->hw[1][0], &p->hw[1][1]);

    if (proc >= 0.0) {
        med = (D(p->ap + proc * (p->kpa - proc)) + p->kpb) * p->pwrq;
    } else {
        med = (D(p->an - proc * (p->kna + proc)) + p->knb) * p->pwrq * -1.0;
    }

    proc = p->srct * (med - p->prev_med + p->prev_out);
    p->prev_med = M(med);
    p->prev_out = M(proc);

    proc = bprocess(proc, p->hp, &p->hw[2][0], &p->hw[2][1]);
    proc = bprocess(proc, p->hp, &p->hw[3][0], &p->hw[3][1]);

    if (s->ceil >= 10000.) {
        proc = bprocess(proc, p->lp, &p->lw[0][0], &p->lw[0][1]);
        proc = bprocess(proc, p->lp, &p->lw[1][0], &p->lw[1][1]);
    }

    return proc;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AExciterContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    const double *src = (const double *)in->data[0];
    const double level_in = s->level_in;
    const double level_out = s->level_out;
    const double amount = s->amount;
    const double listen = 1.0 - s->listen;
    double *dst;

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_audio_buffer(inlink, in->nb_samples);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    dst = (double *)out->data[0];
    for (int n = 0; n < in->nb_samples; n++) {
        for (int c = 0; c < inlink->ch_layout.nb_channels; c++) {
            double sample = src[c] * level_in;

            sample = distortion_process(s, &s->cp[c], sample);
            sample = sample * amount + listen * src[c];

            sample *= level_out;
            if (ctx->is_disabled)
                dst[c] = src[c];
            else
                dst[c] = sample;
        }

        src += inlink->ch_layout.nb_channels;
        dst += inlink->ch_layout.nb_channels;
    }

    if (in != out)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AExciterContext *s = ctx->priv;

    av_freep(&s->cp);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AExciterContext *s = ctx->priv;

    if (!s->cp)
        s->cp = av_calloc(inlink->ch_layout.nb_channels, sizeof(*s->cp));
    if (!s->cp)
        return AVERROR(ENOMEM);

    for (int i = 0; i < inlink->ch_layout.nb_channels; i++)
        set_params(&s->cp[i], s->blend, s->drive, inlink->sample_rate,
                   s->freq, s->ceil);

    return 0;
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    AVFilterLink *inlink = ctx->inputs[0];
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    return config_input(inlink);
}

static const AVFilterPad avfilter_af_aexciter_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad avfilter_af_aexciter_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
};

const AVFilter ff_af_aexciter = {
    .name          = "aexciter",
    .description   = NULL_IF_CONFIG_SMALL("Enhance high frequency part of audio."),
    .priv_size     = sizeof(AExciterContext),
    .priv_class    = &aexciter_class,
    .uninit        = uninit,
    FILTER_INPUTS(avfilter_af_aexciter_inputs),
    FILTER_OUTPUTS(avfilter_af_aexciter_outputs),
    FILTER_SINGLE_SAMPLEFMT(AV_SAMPLE_FMT_DBL),
    .process_command = process_command,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
