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

typedef struct LFOContext {
    double freq;
    double offset;
    int srate;
    double amount;
    double pwidth;
    double phase;
} LFOContext;

typedef struct SRContext {
    double target;
    double real;
    double samples;
    double last;
} SRContext;

typedef struct ACrusherContext {
    const AVClass *class;

    double level_in;
    double level_out;
    double bits;
    double mix;
    int mode;
    double dc;
    double idc;
    double aa;
    double samples;
    int is_lfo;
    double lforange;
    double lforate;

    double sqr;
    double aa1;
    double coeff;
    int    round;
    double sov;
    double smin;
    double sdiff;

    LFOContext lfo;
    SRContext *sr;
} ACrusherContext;

#define OFFSET(x) offsetof(ACrusherContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption acrusher_options[] = {
    { "level_in", "set level in",         OFFSET(level_in),  AV_OPT_TYPE_DOUBLE, {.dbl=1},    0.015625, 64, A },
    { "level_out","set level out",        OFFSET(level_out), AV_OPT_TYPE_DOUBLE, {.dbl=1},    0.015625, 64, A },
    { "bits",     "set bit reduction",    OFFSET(bits),      AV_OPT_TYPE_DOUBLE, {.dbl=8},    1,        64, A },
    { "mix",      "set mix",              OFFSET(mix),       AV_OPT_TYPE_DOUBLE, {.dbl=.5},   0,         1, A },
    { "mode",     "set mode",             OFFSET(mode),      AV_OPT_TYPE_INT,    {.i64=0},    0,         1, A, "mode" },
    {   "lin",    "linear",               0,                 AV_OPT_TYPE_CONST,  {.i64=0},    0,         0, A, "mode" },
    {   "log",    "logarithmic",          0,                 AV_OPT_TYPE_CONST,  {.i64=1},    0,         0, A, "mode" },
    { "dc",       "set DC",               OFFSET(dc),        AV_OPT_TYPE_DOUBLE, {.dbl=1},  .25,         4, A },
    { "aa",       "set anti-aliasing",    OFFSET(aa),        AV_OPT_TYPE_DOUBLE, {.dbl=.5},   0,         1, A },
    { "samples",  "set sample reduction", OFFSET(samples),   AV_OPT_TYPE_DOUBLE, {.dbl=1},    1,       250, A },
    { "lfo",      "enable LFO",           OFFSET(is_lfo),    AV_OPT_TYPE_BOOL,   {.i64=0},    0,         1, A },
    { "lforange", "set LFO depth",        OFFSET(lforange),  AV_OPT_TYPE_DOUBLE, {.dbl=20},   1,       250, A },
    { "lforate",  "set LFO rate",         OFFSET(lforate),   AV_OPT_TYPE_DOUBLE, {.dbl=.3}, .01,       200, A },
    { NULL }
};

AVFILTER_DEFINE_CLASS(acrusher);

static double samplereduction(ACrusherContext *s, SRContext *sr, double in)
{
    sr->samples++;
    if (sr->samples >= s->round) {
        sr->target += s->samples;
        sr->real += s->round;
        if (sr->target + s->samples >= sr->real + 1) {
            sr->last = in;
            sr->target = 0;
            sr->real   = 0;
        }
        sr->samples = 0;
    }
    return sr->last;
}

static double add_dc(double s, double dc, double idc)
{
    return s > 0 ? s * dc : s * idc;
}

static double remove_dc(double s, double dc, double idc)
{
    return s > 0 ? s * idc : s * dc;
}

static inline double factor(double y, double k, double aa1, double aa)
{
    return 0.5 * (sin(M_PI * (fabs(y - k) - aa1) / aa - M_PI_2) + 1);
}

static double bitreduction(ACrusherContext *s, double in)
{
    const double sqr = s->sqr;
    const double coeff = s->coeff;
    const double aa = s->aa;
    const double aa1 = s->aa1;
    double y, k;

    // add dc
    in = add_dc(in, s->dc, s->idc);

    // main rounding calculation depending on mode

    // the idea for anti-aliasing:
    // you need a function f which brings you to the scale, where
    // you want to round and the function f_b (with f(f_b)=id) which
    // brings you back to your original scale.
    //
    // then you can use the logic below in the following way:
    // y = f(in) and k = roundf(y)
    // if (y > k + aa1)
    //      k = f_b(k) + ( f_b(k+1) - f_b(k) ) * 0.5 * (sin(x - PI/2) + 1)
    // if (y < k + aa1)
    //      k = f_b(k) - ( f_b(k+1) - f_b(k) ) * 0.5 * (sin(x - PI/2) + 1)
    //
    // whereas x = (fabs(f(in) - k) - aa1) * PI / aa
    // for both cases.

    switch (s->mode) {
    case 0:
    default:
        // linear
        y = in * coeff;
        k = roundf(y);
        if (k - aa1 <= y && y <= k + aa1) {
            k /= coeff;
        } else if (y > k + aa1) {
            k = k / coeff + ((k + 1) / coeff - k / coeff) *
                factor(y, k, aa1, aa);
        } else {
            k = k / coeff - (k / coeff - (k - 1) / coeff) *
                factor(y, k, aa1, aa);
        }
        break;
    case 1:
        // logarithmic
        y = sqr * log(fabs(in)) + sqr * sqr;
        k = roundf(y);
        if(!in) {
            k = 0;
        } else if (k - aa1 <= y && y <= k + aa1) {
            k = in / fabs(in) * exp(k / sqr - sqr);
        } else if (y > k + aa1) {
            double x = exp(k / sqr - sqr);
            k = FFSIGN(in) * (x + (exp((k + 1) / sqr - sqr) - x) *
                factor(y, k, aa1, aa));
        } else {
            double x = exp(k / sqr - sqr);
            k = in / fabs(in) * (x - (x - exp((k - 1) / sqr - sqr)) *
                factor(y, k, aa1, aa));
        }
        break;
    }

    // mix between dry and wet signal
    k += (in - k) * s->mix;

    // remove dc
    k = remove_dc(k, s->dc, s->idc);

    return k;
}

static double lfo_get(LFOContext *lfo)
{
    double phs = FFMIN(100., lfo->phase / FFMIN(1.99, FFMAX(0.01, lfo->pwidth)) + lfo->offset);
    double val;

    if (phs > 1)
        phs = fmod(phs, 1.);

    val = sin((phs * 360.) * M_PI / 180);

    return val * lfo->amount;
}

static void lfo_advance(LFOContext *lfo, unsigned count)
{
    lfo->phase = fabs(lfo->phase + count * lfo->freq * (1. / lfo->srate));
    if (lfo->phase >= 1.)
        lfo->phase = fmod(lfo->phase, 1.);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    ACrusherContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    const double *src = (const double *)in->data[0];
    double *dst;
    const double level_in = s->level_in;
    const double level_out = s->level_out;
    const double mix = s->mix;
    int n, c;

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
    for (n = 0; n < in->nb_samples; n++) {
        if (s->is_lfo) {
            s->samples = s->smin + s->sdiff * (lfo_get(&s->lfo) + 0.5);
            s->round = round(s->samples);
        }

        for (c = 0; c < inlink->channels; c++) {
            double sample = src[c] * level_in;

            sample = mix * samplereduction(s, &s->sr[c], sample) + src[c] * (1. - mix) * level_in;
            dst[c] = bitreduction(s, sample) * level_out;
        }
        src += c;
        dst += c;

        if (s->is_lfo)
            lfo_advance(&s->lfo, 1);
    }

    if (in != out)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_DBL,
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

static av_cold void uninit(AVFilterContext *ctx)
{
    ACrusherContext *s = ctx->priv;

    av_freep(&s->sr);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ACrusherContext *s = ctx->priv;
    double rad, sunder, smax, sover;

    s->idc = 1. / s->dc;
    s->coeff = exp2(s->bits) - 1;
    s->sqr = sqrt(s->coeff / 2);
    s->aa1 = (1. - s->aa) / 2.;
    s->round = round(s->samples);
    rad = s->lforange / 2.;
    s->smin = FFMAX(s->samples - rad, 1.);
    sunder   = s->samples - rad - s->smin;
    smax = FFMIN(s->samples + rad, 250.);
    sover    = s->samples + rad - smax;
    smax    -= sunder;
    s->smin -= sover;
    s->sdiff = smax - s->smin;

    s->lfo.freq = s->lforate;
    s->lfo.pwidth = 1.;
    s->lfo.srate = inlink->sample_rate;
    s->lfo.amount = .5;

    s->sr = av_calloc(inlink->channels, sizeof(*s->sr));
    if (!s->sr)
        return AVERROR(ENOMEM);

    return 0;
}

static const AVFilterPad avfilter_af_acrusher_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_af_acrusher_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_acrusher = {
    .name          = "acrusher",
    .description   = NULL_IF_CONFIG_SMALL("Reduce audio bit resolution."),
    .priv_size     = sizeof(ACrusherContext),
    .priv_class    = &acrusher_class,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = avfilter_af_acrusher_inputs,
    .outputs       = avfilter_af_acrusher_outputs,
};
