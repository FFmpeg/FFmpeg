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

#include "libavutil/ffmath.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "audio.h"
#include "filters.h"
#include "formats.h"

enum DetectionModes {
    DET_UNSET = 0,
    DET_DISABLED,
    DET_OFF,
    DET_ON,
    DET_ADAPTIVE,
    NB_DMODES,
};

enum FilterModes {
    LISTEN = -1,
    CUT_BELOW,
    CUT_ABOVE,
    BOOST_BELOW,
    BOOST_ABOVE,
    NB_FMODES,
};

typedef struct ChannelContext {
    double fa_double[3], fm_double[3];
    double dstate_double[2];
    double fstate_double[2];
    double tstate_double[2];
    double lin_gain_double;
    double detect_double;
    double threshold_log_double;
    double new_threshold_log_double;
    double log_sum_double;
    double sum_double;
    float fa_float[3], fm_float[3];
    float dstate_float[2];
    float fstate_float[2];
    float tstate_float[2];
    float lin_gain_float;
    float detect_float;
    float threshold_log_float;
    float new_threshold_log_float;
    float log_sum_float;
    float sum_float;
    void *dqueue;
    void *queue;
    int position;
    int size;
    int front;
    int back;
    int detection;
    int init;
} ChannelContext;

typedef struct AudioDynamicEqualizerContext {
    const AVClass *class;

    double threshold;
    double threshold_log;
    double dfrequency;
    double dqfactor;
    double tfrequency;
    double tqfactor;
    double ratio;
    double range;
    double makeup;
    double dattack;
    double drelease;
    double dattack_coef;
    double drelease_coef;
    double gattack_coef;
    double grelease_coef;
    int mode;
    int detection;
    int tftype;
    int dftype;
    int precision;
    int format;
    int nb_channels;

    int (*filter_prepare)(AVFilterContext *ctx);
    int (*filter_channels)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);

    double da_double[3], dm_double[3];
    float da_float[3], dm_float[3];

    ChannelContext *cc;
} AudioDynamicEqualizerContext;

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    const AudioDynamicEqualizerContext *s = ctx->priv;
    static const enum AVSampleFormat sample_fmts[3][3] = {
        { AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_DBLP, AV_SAMPLE_FMT_NONE },
        { AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_NONE },
        { AV_SAMPLE_FMT_DBLP, AV_SAMPLE_FMT_NONE },
    };
    int ret;

    if ((ret = ff_set_common_formats_from_list2(ctx, cfg_in, cfg_out,
                                                sample_fmts[s->precision])) < 0)
        return ret;

    return 0;
}

static double get_coef(double x, double sr)
{
    return 1.0 - exp(-1.0 / (0.001 * x * sr));
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
    s->cc = av_calloc(inlink->ch_layout.nb_channels, sizeof(*s->cc));
    if (!s->cc)
        return AVERROR(ENOMEM);
    s->nb_channels = inlink->ch_layout.nb_channels;

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

    for (int ch = 0; ch < s->nb_channels; ch++) {
        ChannelContext *cc = &s->cc[ch];
        cc->queue = av_calloc(inlink->sample_rate, sizeof(double));
        cc->dqueue = av_calloc(inlink->sample_rate, sizeof(double));
        if (!cc->queue || !cc->dqueue)
            return AVERROR(ENOMEM);
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

    for (int ch = 0; ch < s->nb_channels; ch++) {
        ChannelContext *cc = &s->cc[ch];
        av_freep(&cc->queue);
        av_freep(&cc->dqueue);
    }
    av_freep(&s->cc);
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
    { "attack", "set detection attack duration", OFFSET(dattack),  AV_OPT_TYPE_DOUBLE, {.dbl=20},    0.01, 2000,    FLAGS },
    { "release","set detection release duration",OFFSET(drelease), AV_OPT_TYPE_DOUBLE, {.dbl=200},   0.01, 2000,    FLAGS },
    { "ratio",      "set ratio factor",        OFFSET(ratio),      AV_OPT_TYPE_DOUBLE, {.dbl=1},        0, 30,      FLAGS },
    { "makeup",     "set makeup gain",         OFFSET(makeup),     AV_OPT_TYPE_DOUBLE, {.dbl=0},        0, 1000,    FLAGS },
    { "range",      "set max gain",            OFFSET(range),      AV_OPT_TYPE_DOUBLE, {.dbl=50},       1, 2000,    FLAGS },
    { "mode",       "set mode",                OFFSET(mode),       AV_OPT_TYPE_INT,    {.i64=0},  LISTEN,NB_FMODES-1,FLAGS, .unit = "mode" },
    {   "listen",   0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=LISTEN},   0, 0,       FLAGS, .unit = "mode" },
    {   "cutbelow", 0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=CUT_BELOW},0, 0,       FLAGS, .unit = "mode" },
    {   "cutabove", 0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=CUT_ABOVE},0, 0,       FLAGS, .unit = "mode" },
    { "boostbelow", 0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=BOOST_BELOW},0, 0,     FLAGS, .unit = "mode" },
    { "boostabove", 0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=BOOST_ABOVE},0, 0,     FLAGS, .unit = "mode" },
    { "dftype",     "set detection filter type",OFFSET(dftype),    AV_OPT_TYPE_INT,    {.i64=0},        0, 3,       FLAGS, .unit = "dftype" },
    {   "bandpass", 0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=0},        0, 0,       FLAGS, .unit = "dftype" },
    {   "lowpass",  0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=1},        0, 0,       FLAGS, .unit = "dftype" },
    {   "highpass", 0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=2},        0, 0,       FLAGS, .unit = "dftype" },
    {   "peak",     0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=3},        0, 0,       FLAGS, .unit = "dftype" },
    { "tftype",     "set target filter type",  OFFSET(tftype),     AV_OPT_TYPE_INT,    {.i64=0},        0, 2,       FLAGS, .unit = "tftype" },
    {   "bell",     0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=0},        0, 0,       FLAGS, .unit = "tftype" },
    {   "lowshelf", 0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=1},        0, 0,       FLAGS, .unit = "tftype" },
    {   "highshelf",0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=2},        0, 0,       FLAGS, .unit = "tftype" },
    { "auto",       "set auto threshold",      OFFSET(detection),  AV_OPT_TYPE_INT,    {.i64=DET_OFF},DET_DISABLED,NB_DMODES-1,FLAGS, .unit = "auto" },
    {   "disabled", 0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=DET_DISABLED}, 0, 0,   FLAGS, .unit = "auto" },
    {   "off",      0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=DET_OFF},      0, 0,   FLAGS, .unit = "auto" },
    {   "on",       0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=DET_ON},       0, 0,   FLAGS, .unit = "auto" },
    {   "adaptive", 0,                         0,                  AV_OPT_TYPE_CONST,  {.i64=DET_ADAPTIVE}, 0, 0,   FLAGS, .unit = "auto" },
    { "precision", "set processing precision", OFFSET(precision),  AV_OPT_TYPE_INT,    {.i64=0},        0, 2,       AF, .unit = "precision" },
    {   "auto",  "set auto processing precision",                  0, AV_OPT_TYPE_CONST, {.i64=0},      0, 0,       AF, .unit = "precision" },
    {   "float", "set single-floating point processing precision", 0, AV_OPT_TYPE_CONST, {.i64=1},      0, 0,       AF, .unit = "precision" },
    {   "double","set double-floating point processing precision", 0, AV_OPT_TYPE_CONST, {.i64=2},      0, 0,       AF, .unit = "precision" },
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

const FFFilter ff_af_adynamicequalizer = {
    .p.name          = "adynamicequalizer",
    .p.description   = NULL_IF_CONFIG_SMALL("Apply Dynamic Equalization of input audio."),
    .p.priv_class    = &adynamicequalizer_class,
    .p.flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                       AVFILTER_FLAG_SLICE_THREADS,
    .priv_size       = sizeof(AudioDynamicEqualizerContext),
    .uninit          = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_QUERY_FUNC2(query_formats),
    .process_command = ff_filter_process_command,
};
