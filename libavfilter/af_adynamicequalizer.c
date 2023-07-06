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
    double attack;
    double release;
    double attack_coef;
    double release_coef;
    int mode;
    int direction;
    int detection;
    int tftype;
    int dftype;
    int precision;
    int format;

    int (*filter_prepare)(AVFilterContext *ctx);
    int (*filter_channels)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);

    double da_double[3], dm_double[3];
    float da_float[3], dm_float[3];

    AVFrame *state;
} AudioDynamicEqualizerContext;

static int query_formats(AVFilterContext *ctx)
{
    AudioDynamicEqualizerContext *s = ctx->priv;
    static const enum AVSampleFormat sample_fmts[3][3] = {
        { AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_DBLP, AV_SAMPLE_FMT_NONE },
        { AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_NONE },
        { AV_SAMPLE_FMT_DBLP, AV_SAMPLE_FMT_NONE },
    };
    int ret;

    if ((ret = ff_set_common_all_channel_counts(ctx)) < 0)
        return ret;

    if ((ret = ff_set_common_formats_from_list(ctx, sample_fmts[s->precision])) < 0)
        return ret;

    return ff_set_common_all_samplerates(ctx);
}

static double get_coef(double x, double sr)
{
    return 1.0 - exp(-1000. / (x * sr));
}

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

#define DEPTH 32
#include "adynamicequalizer_template.c"

#undef DEPTH
#define DEPTH 64
#include "adynamicequalizer_template.c"

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioDynamicEqualizerContext *s = ctx->priv;

    s->format = inlink->format;
    s->state = ff_get_audio_buffer(inlink, 16);
    if (!s->state)
        return AVERROR(ENOMEM);

    switch (s->format) {
    case AV_SAMPLE_FMT_DBLP:
        s->filter_prepare  = filter_prepare_double;
        s->filter_channels = filter_channels_double;
        break;
    case AV_SAMPLE_FMT_FLTP:
        s->filter_prepare  = filter_prepare_float;
        s->filter_channels = filter_channels_float;
        break;
    }

    return 0;
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

    td.in = in;
    td.out = out;
    s->filter_prepare(ctx);
    ff_filter_execute(ctx, s->filter_channels, &td, NULL,
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
#define AF AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption adynamicequalizer_options[] = {
    { "threshold",  "set detection threshold", OFFSET(threshold),  AV_OPT_TYPE_DOUBLE, {.dbl=0},        0, 100,     FLAGS },
    { "dfrequency", "set detection frequency", OFFSET(dfrequency), AV_OPT_TYPE_DOUBLE, {.dbl=1000},     2, 1000000, FLAGS },
    { "dqfactor",   "set detection Q factor",  OFFSET(dqfactor),   AV_OPT_TYPE_DOUBLE, {.dbl=1},    0.001, 1000,    FLAGS },
    { "tfrequency", "set target frequency",    OFFSET(tfrequency), AV_OPT_TYPE_DOUBLE, {.dbl=1000},     2, 1000000, FLAGS },
    { "tqfactor",   "set target Q factor",     OFFSET(tqfactor),   AV_OPT_TYPE_DOUBLE, {.dbl=1},    0.001, 1000,    FLAGS },
    { "attack",     "set attack duration",     OFFSET(attack),     AV_OPT_TYPE_DOUBLE, {.dbl=20},       1, 2000,    FLAGS },
    { "release",    "set release duration",    OFFSET(release),    AV_OPT_TYPE_DOUBLE, {.dbl=200},      1, 2000,    FLAGS },
    { "ratio",      "set ratio factor",        OFFSET(ratio),      AV_OPT_TYPE_DOUBLE, {.dbl=1},        0, 30,      FLAGS },
    { "makeup",     "set makeup gain",         OFFSET(makeup),     AV_OPT_TYPE_DOUBLE, {.dbl=0},        0, 100,     FLAGS },
    { "range",      "set max gain",            OFFSET(range),      AV_OPT_TYPE_DOUBLE, {.dbl=50},       1, 200,     FLAGS },
    { "mode",       "set mode",                OFFSET(mode),       AV_OPT_TYPE_INT,    {.i64=0},       -1, 1,       FLAGS, "mode" },
    {   "listen",   0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=-1},       0, 0,       FLAGS, "mode" },
    {   "cut",      0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=0},        0, 0,       FLAGS, "mode" },
    {   "boost",    0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=1},        0, 0,       FLAGS, "mode" },
    { "dftype",     "set detection filter type",OFFSET(dftype),    AV_OPT_TYPE_INT,    {.i64=0},        0, 3,       FLAGS, "dftype" },
    {   "bandpass", 0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=0},        0, 0,       FLAGS, "dftype" },
    {   "lowpass",  0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=1},        0, 0,       FLAGS, "dftype" },
    {   "highpass", 0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=2},        0, 0,       FLAGS, "dftype" },
    {   "peak",     0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=3},        0, 0,       FLAGS, "dftype" },
    { "tftype",     "set target filter type",  OFFSET(tftype),     AV_OPT_TYPE_INT,    {.i64=0},        0, 2,       FLAGS, "tftype" },
    {   "bell",     0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=0},        0, 0,       FLAGS, "tftype" },
    {   "lowshelf", 0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=1},        0, 0,       FLAGS, "tftype" },
    {   "highshelf",0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=2},        0, 0,       FLAGS, "tftype" },
    { "direction",  "set direction",           OFFSET(direction),  AV_OPT_TYPE_INT,    {.i64=0},        0, 1,       FLAGS, "direction" },
    {   "downward", 0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=0},        0, 0,       FLAGS, "direction" },
    {   "upward",   0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=1},        0, 0,       FLAGS, "direction" },
    { "auto",       "set auto threshold",      OFFSET(detection),  AV_OPT_TYPE_INT,    {.i64=-1},      -1, 1,       FLAGS, "auto" },
    {   "disabled", 0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=-1},       0, 0,       FLAGS, "auto" },
    {   "off",      0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=0},        0, 0,       FLAGS, "auto" },
    {   "on",       0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=1},        0, 0,       FLAGS, "auto" },
    { "precision", "set processing precision", OFFSET(precision),  AV_OPT_TYPE_INT,    {.i64=0},        0, 2,       AF, "precision" },
    {   "auto",  "set auto processing precision",                  0, AV_OPT_TYPE_CONST, {.i64=0},      0, 0,       AF, "precision" },
    {   "float", "set single-floating point processing precision", 0, AV_OPT_TYPE_CONST, {.i64=1},      0, 0,       AF, "precision" },
    {   "double","set double-floating point processing precision", 0, AV_OPT_TYPE_CONST, {.i64=2},      0, 0,       AF, "precision" },
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
    FILTER_QUERY_FUNC(query_formats),
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                       AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
