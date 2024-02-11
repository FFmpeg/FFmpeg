/*
 * Copyright (c) 2001-2010 Krzysztof Foltman, Markus Schmidt, Thor Harald Johansen and others
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
#include "formats.h"
#include "internal.h"
#include "audio.h"

enum PulsatorModes { SINE, TRIANGLE, SQUARE, SAWUP, SAWDOWN, NB_MODES };
enum PulsatorTimings { UNIT_BPM, UNIT_MS, UNIT_HZ, NB_TIMINGS };

typedef struct SimpleLFO {
    double phase;
    double freq;
    double offset;
    double amount;
    double pwidth;
    int mode;
    int srate;
} SimpleLFO;

typedef struct AudioPulsatorContext {
    const AVClass *class;
    int mode;
    double level_in;
    double level_out;
    double amount;
    double offset_l;
    double offset_r;
    double pwidth;
    double bpm;
    double hertz;
    int ms;
    int timing;

    SimpleLFO lfoL, lfoR;
} AudioPulsatorContext;

#define OFFSET(x) offsetof(AudioPulsatorContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption apulsator_options[] = {
    { "level_in",   "set input gain", OFFSET(level_in),  AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0.015625, 64, FLAGS, },
    { "level_out", "set output gain", OFFSET(level_out), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0.015625, 64, FLAGS, },
    { "mode",             "set mode", OFFSET(mode),      AV_OPT_TYPE_INT,    {.i64=SINE}, SINE,   NB_MODES-1, FLAGS, .unit = "mode" },
    {   "sine",                 NULL, 0,                 AV_OPT_TYPE_CONST,  {.i64=SINE},    0,            0, FLAGS, .unit = "mode" },
    {   "triangle",             NULL, 0,                 AV_OPT_TYPE_CONST,  {.i64=TRIANGLE},0,            0, FLAGS, .unit = "mode" },
    {   "square",               NULL, 0,                 AV_OPT_TYPE_CONST,  {.i64=SQUARE},  0,            0, FLAGS, .unit = "mode" },
    {   "sawup",                NULL, 0,                 AV_OPT_TYPE_CONST,  {.i64=SAWUP},   0,            0, FLAGS, .unit = "mode" },
    {   "sawdown",              NULL, 0,                 AV_OPT_TYPE_CONST,  {.i64=SAWDOWN}, 0,            0, FLAGS, .unit = "mode" },
    { "amount",     "set modulation", OFFSET(amount),    AV_OPT_TYPE_DOUBLE, {.dbl=1},       0,            1, FLAGS },
    { "offset_l",     "set offset L", OFFSET(offset_l),  AV_OPT_TYPE_DOUBLE, {.dbl=0},       0,            1, FLAGS },
    { "offset_r",     "set offset R", OFFSET(offset_r),  AV_OPT_TYPE_DOUBLE, {.dbl=.5},      0,            1, FLAGS },
    { "width",     "set pulse width", OFFSET(pwidth),    AV_OPT_TYPE_DOUBLE, {.dbl=1},       0,            2, FLAGS },
    { "timing",         "set timing", OFFSET(timing),    AV_OPT_TYPE_INT,    {.i64=2},       0, NB_TIMINGS-1, FLAGS, .unit = "timing" },
    {   "bpm",                  NULL, 0,                 AV_OPT_TYPE_CONST,  {.i64=UNIT_BPM},  0,          0, FLAGS, .unit = "timing" },
    {   "ms",                   NULL, 0,                 AV_OPT_TYPE_CONST,  {.i64=UNIT_MS},   0,          0, FLAGS, .unit = "timing" },
    {   "hz",                   NULL, 0,                 AV_OPT_TYPE_CONST,  {.i64=UNIT_HZ},   0,          0, FLAGS, .unit = "timing" },
    { "bpm",               "set BPM", OFFSET(bpm),       AV_OPT_TYPE_DOUBLE, {.dbl=120},    30,          300, FLAGS },
    { "ms",                 "set ms", OFFSET(ms),        AV_OPT_TYPE_INT,    {.i64=500},    10,         2000, FLAGS },
    { "hz",          "set frequency", OFFSET(hertz),     AV_OPT_TYPE_DOUBLE, {.dbl=2},    0.01,          100, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(apulsator);

static void lfo_advance(SimpleLFO *lfo, unsigned count)
{
    lfo->phase = fabs(lfo->phase + count * lfo->freq / lfo->srate);
    if (lfo->phase >= 1)
        lfo->phase = fmod(lfo->phase, 1);
}

static double lfo_get_value(SimpleLFO *lfo)
{
    double phs = FFMIN(100, lfo->phase / FFMIN(1.99, FFMAX(0.01, lfo->pwidth)) + lfo->offset);
    double val;

    if (phs > 1)
        phs = fmod(phs, 1.);

    switch (lfo->mode) {
    case SINE:
        val = sin(phs * 2 * M_PI);
        break;
    case TRIANGLE:
        if (phs > 0.75)
            val = (phs - 0.75) * 4 - 1;
        else if (phs > 0.25)
            val = -4 * phs + 2;
        else
            val = phs * 4;
        break;
    case SQUARE:
        val = phs < 0.5 ? -1 : +1;
        break;
    case SAWUP:
        val = phs * 2 - 1;
        break;
    case SAWDOWN:
        val = 1 - phs * 2;
        break;
    default: av_assert0(0);
    }

    return val * lfo->amount;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AudioPulsatorContext *s = ctx->priv;
    const double *src = (const double *)in->data[0];
    const int nb_samples = in->nb_samples;
    const double level_out = s->level_out;
    const double level_in = s->level_in;
    const double amount = s->amount;
    AVFrame *out;
    double *dst;
    int n;

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

    for (n = 0; n < nb_samples; n++) {
        double outL;
        double outR;
        double inL = src[0] * level_in;
        double inR = src[1] * level_in;
        double procL = inL;
        double procR = inR;

        procL *= lfo_get_value(&s->lfoL) * 0.5 + amount / 2;
        procR *= lfo_get_value(&s->lfoR) * 0.5 + amount / 2;

        outL = procL + inL * (1 - amount);
        outR = procR + inR * (1 - amount);

        outL *= level_out;
        outR *= level_out;

        dst[0] = outL;
        dst[1] = outR;

        lfo_advance(&s->lfoL, 1);
        lfo_advance(&s->lfoR, 1);

        dst += 2;
        src += 2;
    }

    if (in != out)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterChannelLayouts *layout = NULL;
    AVFilterFormats *formats = NULL;
    int ret;

    if ((ret = ff_add_format                 (&formats, AV_SAMPLE_FMT_DBL  )) < 0 ||
        (ret = ff_set_common_formats         (ctx     , formats            )) < 0 ||
        (ret = ff_add_channel_layout         (&layout , &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO)) < 0 ||
        (ret = ff_set_common_channel_layouts (ctx     , layout             )) < 0)
        return ret;

    return ff_set_common_all_samplerates(ctx);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioPulsatorContext *s = ctx->priv;
    double freq;

    switch (s->timing) {
    case UNIT_BPM:  freq = s->bpm / 60;         break;
    case UNIT_MS:   freq = 1 / (s->ms / 1000.); break;
    case UNIT_HZ:   freq = s->hertz;            break;
    default: av_assert0(0);
    }

    s->lfoL.freq   = freq;
    s->lfoR.freq   = freq;
    s->lfoL.mode   = s->mode;
    s->lfoR.mode   = s->mode;
    s->lfoL.offset = s->offset_l;
    s->lfoR.offset = s->offset_r;
    s->lfoL.srate  = inlink->sample_rate;
    s->lfoR.srate  = inlink->sample_rate;
    s->lfoL.amount = s->amount;
    s->lfoR.amount = s->amount;
    s->lfoL.pwidth = s->pwidth;
    s->lfoR.pwidth = s->pwidth;

    return 0;
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
};

const AVFilter ff_af_apulsator = {
    .name          = "apulsator",
    .description   = NULL_IF_CONFIG_SMALL("Audio pulsator."),
    .priv_size     = sizeof(AudioPulsatorContext),
    .priv_class    = &apulsator_class,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_QUERY_FUNC(query_formats),
};
