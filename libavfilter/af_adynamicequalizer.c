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

#include <float.h>

#include "libavutil/opt.h"
#include "avfilter.h"
#include "audio.h"
#include "formats.h"

typedef struct AudioDynamicEqualizerContext {
    const AVClass *class;

    double threshold;
    double dfrequency;
    double dqfactor;
    double tfrequency;
    double tqfactor;
    double ratio;
    double range;
    double makeup;
    double knee;
    double slew;
    double attack;
    double release;
    double attack_coef;
    double release_coef;
    int mode;
    int type;

    AVFrame *state;
} AudioDynamicEqualizerContext;

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioDynamicEqualizerContext *s = ctx->priv;

    s->state = ff_get_audio_buffer(inlink, 8);
    if (!s->state)
        return AVERROR(ENOMEM);

    return 0;
}

static double get_svf(double in, double *m, double *a, double *b)
{
    const double v0 = in;
    const double v3 = v0 - b[1];
    const double v1 = a[0] * b[0] + a[1] * v3;
    const double v2 = b[1] + a[1] * b[0] + a[2] * v3;

    b[0] = 2. * v1 - b[0];
    b[1] = 2. * v2 - b[1];

    return m[0] * v0 + m[1] * v1 + m[2] * v2;
}

static inline double from_dB(double x)
{
    return exp(0.05 * x * M_LN10);
}

static inline double to_dB(double x)
{
    return 20. * log10(x);
}

static inline double sqr(double x)
{
    return x * x;
}

static double get_gain(double in, double srate, double makeup,
                       double aattack, double iratio, double knee, double range,
                       double thresdb, double slewfactor, double *state,
                       double attack_coeff, double release_coeff, double nc)
{
    double width = (6. * knee) + 0.01;
    double cdb = 0.;
    double Lgain = 1.;
    double Lxg, Lxl, Lyg, Lyl, Ly1;
    double checkwidth = 0.;
    double slewwidth = 1.8;
    int attslew = 0;

    Lyg = 0.;
    Lxg = to_dB(fabs(in) + DBL_EPSILON);

    Lyg = Lxg + (iratio - 1.) * sqr(Lxg - thresdb + width * .5) / (2. * width);

    checkwidth = 2. * fabs(Lxg - thresdb);
    if (2. * (Lxg - thresdb) < -width) {
        Lyg = Lxg;
    } else if (checkwidth <= width) {
        Lyg = thresdb + (Lxg - thresdb) * iratio;
        if (checkwidth <= slewwidth) {
            if (Lyg >= state[2])
                attslew = 1;
        }
    } else if (2. * (Lxg - thresdb) > width) {
        Lyg = thresdb + (Lxg - thresdb) * iratio;
    }

    attack_coeff = attslew ? aattack : attack_coeff;

    Lxl = Lxg - Lyg;

    Ly1 = fmax(Lxl, release_coeff * state[1] +(1. - release_coeff) * Lxl);
    Lyl = attack_coeff * state[0] + (1. - attack_coeff) * Ly1;

    cdb = -Lyl;
    Lgain = from_dB(nc * fmin(cdb - makeup, range));

    state[0] = Lyl;
    state[1] = Ly1;
    state[2] = Lyg;

    return Lgain;
}

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

static int filter_channels(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    AudioDynamicEqualizerContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const double sample_rate = in->sample_rate;
    const double makeup = s->makeup;
    const double iratio = 1. / s->ratio;
    const double range = s->range;
    const double dfrequency = fmin(s->dfrequency, sample_rate * 0.5);
    const double tfrequency = fmin(s->tfrequency, sample_rate * 0.5);
    const double threshold = to_dB(s->threshold + DBL_EPSILON);
    const double release = s->release_coef;
    const double attack = s->attack_coef;
    const double dqfactor = s->dqfactor;
    const double tqfactor = s->tqfactor;
    const double fg = tan(M_PI * tfrequency / sample_rate);
    const double dg = tan(M_PI * dfrequency / sample_rate);
    const int start = (in->ch_layout.nb_channels * jobnr) / nb_jobs;
    const int end = (in->ch_layout.nb_channels * (jobnr+1)) / nb_jobs;
    const int mode = s->mode;
    const int type = s->type;
    const double knee = s->knee;
    const double slew = s->slew;
    const double aattack = exp(-1000. / ((s->attack + 2.0 * (slew - 1.)) * sample_rate));
    const double nc = mode == 0 ? 1. : -1.;
    double da[3], dm[3];

    {
        double k = 1. / dqfactor;

        da[0] = 1. / (1. + dg * (dg + k));
        da[1] = dg * da[0];
        da[2] = dg * da[1];

        dm[0] = 0.;
        dm[1] = 1.;
        dm[2] = 0.;
    }

    for (int ch = start; ch < end; ch++) {
        const double *src = (const double *)in->extended_data[ch];
        double *dst = (double *)out->extended_data[ch];
        double *state = (double *)s->state->extended_data[ch];

        for (int n = 0; n < out->nb_samples; n++) {
            double detect, gain, v, listen;
            double fa[3], fm[3];
            double k, g;

            detect = listen = get_svf(src[n], dm, da, state);
            detect = fabs(detect);

            gain = get_gain(detect, sample_rate, makeup,
                            aattack, iratio, knee, range, threshold, slew,
                            &state[4], attack, release, nc);

            switch (type) {
            case 0:
                k = 1. / (tqfactor * gain);

                fa[0] = 1. / (1. + fg * (fg + k));
                fa[1] = fg * fa[0];
                fa[2] = fg * fa[1];

                fm[0] = 1.;
                fm[1] = k * (gain * gain - 1.);
                fm[2] = 0.;
                break;
            case 1:
                k = 1. / tqfactor;
                g = fg / sqrt(gain);

                fa[0] = 1. / (1. + g * (g + k));
                fa[1] = g * fa[0];
                fa[2] = g * fa[1];

                fm[0] = 1.;
                fm[1] = k * (gain - 1.);
                fm[2] = gain * gain - 1.;
                break;
            case 2:
                k = 1. / tqfactor;
                g = fg / sqrt(gain);

                fa[0] = 1. / (1. + g * (g + k));
                fa[1] = g * fa[0];
                fa[2] = g * fa[1];

                fm[0] = gain * gain;
                fm[1] = k * (1. - gain) * gain;
                fm[2] = 1. - gain * gain;
                break;
            }

            v = get_svf(src[n], fm, fa, &state[2]);
            v = mode == -1 ? listen : v;
            dst[n] = ctx->is_disabled ? src[n] : v;
        }
    }

    return 0;
}

static double get_coef(double x, double sr)
{
    return exp(-1000. / (x * sr));
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AudioDynamicEqualizerContext *s = ctx->priv;
    ThreadData td;
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

    s->attack_coef = get_coef(s->attack, in->sample_rate);
    s->release_coef = get_coef(s->release, in->sample_rate);

    td.in = in;
    td.out = out;
    ff_filter_execute(ctx, filter_channels, &td, NULL,
                     FFMIN(outlink->ch_layout.nb_channels, ff_filter_get_nb_threads(ctx)));

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioDynamicEqualizerContext *s = ctx->priv;

    av_frame_free(&s->state);
}

#define OFFSET(x) offsetof(AudioDynamicEqualizerContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption adynamicequalizer_options[] = {
    { "threshold",  "set detection threshold", OFFSET(threshold),  AV_OPT_TYPE_DOUBLE, {.dbl=0},        0, 100,     FLAGS },
    { "dfrequency", "set detection frequency", OFFSET(dfrequency), AV_OPT_TYPE_DOUBLE, {.dbl=1000},     2, 1000000, FLAGS },
    { "dqfactor",   "set detection Q factor",  OFFSET(dqfactor),   AV_OPT_TYPE_DOUBLE, {.dbl=1},    0.001, 1000,    FLAGS },
    { "tfrequency", "set target frequency",    OFFSET(tfrequency), AV_OPT_TYPE_DOUBLE, {.dbl=1000},     2, 1000000, FLAGS },
    { "tqfactor",   "set target Q factor",     OFFSET(tqfactor),   AV_OPT_TYPE_DOUBLE, {.dbl=1},    0.001, 1000,    FLAGS },
    { "attack",     "set attack duration",     OFFSET(attack),     AV_OPT_TYPE_DOUBLE, {.dbl=20},       1, 2000,    FLAGS },
    { "release",    "set release duration",    OFFSET(release),    AV_OPT_TYPE_DOUBLE, {.dbl=200},      1, 2000,    FLAGS },
    { "knee",       "set knee factor",         OFFSET(knee),       AV_OPT_TYPE_DOUBLE, {.dbl=1},        0, 8,       FLAGS },
    { "ratio",      "set ratio factor",        OFFSET(ratio),      AV_OPT_TYPE_DOUBLE, {.dbl=1},        1, 20,      FLAGS },
    { "makeup",     "set makeup gain",         OFFSET(makeup),     AV_OPT_TYPE_DOUBLE, {.dbl=0},        0, 30,      FLAGS },
    { "range",      "set max gain",            OFFSET(range),      AV_OPT_TYPE_DOUBLE, {.dbl=0},        0, 200,     FLAGS },
    { "slew",       "set slew factor",         OFFSET(slew),       AV_OPT_TYPE_DOUBLE, {.dbl=1},        1, 200,     FLAGS },
    { "mode",       "set mode",                OFFSET(mode),       AV_OPT_TYPE_INT,    {.i64=0},       -1, 1,       FLAGS, "mode" },
    {   "listen",   0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=-1},       0, 0,       FLAGS, "mode" },
    {   "cut",      0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=0},        0, 0,       FLAGS, "mode" },
    {   "boost",    0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=1},        0, 0,       FLAGS, "mode" },
    { "tftype",     "set target filter type",  OFFSET(type),       AV_OPT_TYPE_INT,    {.i64=0},        0, 2,       FLAGS, "type" },
    {   "bell",     0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=0},        0, 0,       FLAGS, "type" },
    {   "lowshelf", 0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=1},        0, 0,       FLAGS, "type" },
    {   "highshelf",0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=2},        0, 0,       FLAGS, "type" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(adynamicequalizer);

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
};

const AVFilter ff_af_adynamicequalizer = {
    .name            = "adynamicequalizer",
    .description     = NULL_IF_CONFIG_SMALL("Apply Dynamic Equalization of input audio."),
    .priv_size       = sizeof(AudioDynamicEqualizerContext),
    .priv_class      = &adynamicequalizer_class,
    .uninit          = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_SINGLE_SAMPLEFMT(AV_SAMPLE_FMT_DBLP),
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                       AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
