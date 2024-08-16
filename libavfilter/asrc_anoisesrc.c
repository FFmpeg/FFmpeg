/*
 * Copyright (c) 2015 Kyle Swanson <k@ylo.ph>.
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "libavutil/lfg.h"
#include "libavutil/random_seed.h"

typedef struct ANoiseSrcContext {
    const AVClass *class;
    int sample_rate;
    double amplitude;
    double density;
    int64_t duration;
    int color;
    int64_t seed;
    int nb_samples;

    int64_t pts;
    int infinite;
    double (*filter)(double white, double *buf);
    double buf[7];
    AVLFG c;
} ANoiseSrcContext;

enum NoiseMode {
    NM_WHITE,
    NM_PINK,
    NM_BROWN,
    NM_BLUE,
    NM_VIOLET,
    NM_VELVET,
    NM_NB
};

#define OFFSET(x) offsetof(ANoiseSrcContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption anoisesrc_options[] = {
    { "sample_rate",  "set sample rate",  OFFSET(sample_rate),  AV_OPT_TYPE_INT,       {.i64 = 48000},     15,  INT_MAX,    FLAGS },
    { "r",            "set sample rate",  OFFSET(sample_rate),  AV_OPT_TYPE_INT,       {.i64 = 48000},     15,  INT_MAX,    FLAGS },
    { "amplitude",    "set amplitude",    OFFSET(amplitude),    AV_OPT_TYPE_DOUBLE,    {.dbl = 1.},        0.,  1.,         FLAGS },
    { "a",            "set amplitude",    OFFSET(amplitude),    AV_OPT_TYPE_DOUBLE,    {.dbl = 1.},        0.,  1.,         FLAGS },
    { "duration",     "set duration",     OFFSET(duration),     AV_OPT_TYPE_DURATION,  {.i64 =  0},         0,  INT64_MAX,  FLAGS },
    { "d",            "set duration",     OFFSET(duration),     AV_OPT_TYPE_DURATION,  {.i64 =  0},         0,  INT64_MAX,  FLAGS },
    { "color",        "set noise color",  OFFSET(color),        AV_OPT_TYPE_INT,       {.i64 =  0},         0,  NM_NB - 1,  FLAGS, .unit = "color" },
    { "colour",       "set noise color",  OFFSET(color),        AV_OPT_TYPE_INT,       {.i64 =  0},         0,  NM_NB - 1,  FLAGS, .unit = "color" },
    { "c",            "set noise color",  OFFSET(color),        AV_OPT_TYPE_INT,       {.i64 =  0},         0,  NM_NB - 1,  FLAGS, .unit = "color" },
    {     "white",    0,                  0,                    AV_OPT_TYPE_CONST,     {.i64 = NM_WHITE},   0,  0,          FLAGS, .unit = "color" },
    {     "pink",     0,                  0,                    AV_OPT_TYPE_CONST,     {.i64 = NM_PINK},    0,  0,          FLAGS, .unit = "color" },
    {     "brown",    0,                  0,                    AV_OPT_TYPE_CONST,     {.i64 = NM_BROWN},   0,  0,          FLAGS, .unit = "color" },
    {     "blue",     0,                  0,                    AV_OPT_TYPE_CONST,     {.i64 = NM_BLUE},    0,  0,          FLAGS, .unit = "color" },
    {     "violet",   0,                  0,                    AV_OPT_TYPE_CONST,     {.i64 = NM_VIOLET},  0,  0,          FLAGS, .unit = "color" },
    {     "velvet",   0,                  0,                    AV_OPT_TYPE_CONST,     {.i64 = NM_VELVET},  0,  0,          FLAGS, .unit = "color" },
    { "seed",         "set random seed",  OFFSET(seed),         AV_OPT_TYPE_INT64,     {.i64 = -1},        -1,  UINT_MAX,   FLAGS },
    { "s",            "set random seed",  OFFSET(seed),         AV_OPT_TYPE_INT64,     {.i64 = -1},        -1,  UINT_MAX,   FLAGS },
    { "nb_samples",   "set the number of samples per requested frame", OFFSET(nb_samples), AV_OPT_TYPE_INT, {.i64 = 1024}, 1, INT_MAX, FLAGS },
    { "n",            "set the number of samples per requested frame", OFFSET(nb_samples), AV_OPT_TYPE_INT, {.i64 = 1024}, 1, INT_MAX, FLAGS },
    { "density",      "set density",      OFFSET(density),      AV_OPT_TYPE_DOUBLE,    {.dbl = 0.05},       0., 1.,         FLAGS },
    {NULL}
};

AVFILTER_DEFINE_CLASS(anoisesrc);

static av_cold int query_formats(AVFilterContext *ctx)
{
    ANoiseSrcContext *s = ctx->priv;
    static const AVChannelLayout chlayouts[] = { AV_CHANNEL_LAYOUT_MONO, { 0 } };
    int sample_rates[] = { s->sample_rate, -1 };
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_DBL,
        AV_SAMPLE_FMT_NONE
    };
    int ret = ff_set_common_formats_from_list(ctx, sample_fmts);
    if (ret < 0)
        return ret;

    ret = ff_set_common_channel_layouts_from_list(ctx, chlayouts);
    if (ret < 0)
        return ret;

    return ff_set_common_samplerates_from_list(ctx, sample_rates);
}

static double white_filter(double white, double *buf)
{
    return white;
}

static double pink_filter(double white, double *buf)
{
    double pink;

    /* http://www.musicdsp.org/files/pink.txt */
    buf[0] = 0.99886 * buf[0] + white * 0.0555179;
    buf[1] = 0.99332 * buf[1] + white * 0.0750759;
    buf[2] = 0.96900 * buf[2] + white * 0.1538520;
    buf[3] = 0.86650 * buf[3] + white * 0.3104856;
    buf[4] = 0.55000 * buf[4] + white * 0.5329522;
    buf[5] = -0.7616 * buf[5] - white * 0.0168980;
    pink = buf[0] + buf[1] + buf[2] + buf[3] + buf[4] + buf[5] + buf[6] + white * 0.5362;
    buf[6] = white * 0.115926;
    return pink * 0.11;
}

static double blue_filter(double white, double *buf)
{
    double blue;

    /* Same as pink_filter but subtract the offsets rather than add */
    buf[0] = 0.0555179 * white - 0.99886 * buf[0];
    buf[1] = 0.0750759 * white - 0.99332 * buf[1];
    buf[2] = 0.1538520 * white - 0.96900 * buf[2];
    buf[3] = 0.3104856 * white - 0.86650 * buf[3];
    buf[4] = 0.5329522 * white - 0.55000 * buf[4];
    buf[5] = -0.016898 * white + 0.76160 * buf[5];
    blue = buf[0] + buf[1] + buf[2] + buf[3] + buf[4] + buf[5] + buf[6] + white * 0.5362;
    buf[6] = white * 0.115926;
    return blue * 0.11;
}

static double brown_filter(double white, double *buf)
{
    double brown;

    brown = ((0.02 * white) + buf[0]) / 1.02;
    buf[0] = brown;
    return brown * 3.5;
}

static double violet_filter(double white, double *buf)
{
    double violet;

    violet = ((0.02 * white) - buf[0]) / 1.02;
    buf[0] = violet;
    return violet * 3.5;
}

static double velvet_filter(double white, double *buf)
{
    double awhite = fabs(white);
    return FFDIFFSIGN(white, 0.0) * buf[1] * (awhite < buf[0]);
}

static av_cold int config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    ANoiseSrcContext *s = ctx->priv;

    if (s->seed == -1)
        s->seed = av_get_random_seed();
    av_lfg_init(&s->c, s->seed);

    if (s->duration == 0)
        s->infinite = 1;
    s->duration = av_rescale(s->duration, s->sample_rate, AV_TIME_BASE);

    switch (s->color) {
    case NM_WHITE:  s->filter = white_filter;  break;
    case NM_PINK:   s->filter = pink_filter;   break;
    case NM_BROWN:  s->filter = brown_filter;  break;
    case NM_BLUE:   s->filter = blue_filter;   break;
    case NM_VIOLET: s->filter = violet_filter; break;
    case NM_VELVET: s->buf[0] = s->amplitude * s->density;
                    s->buf[1] = s->amplitude;
                    s->filter = velvet_filter; break;
    }

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *outlink = ctx->outputs[0];
    ANoiseSrcContext *s = ctx->priv;
    AVFrame *frame;
    int nb_samples, i;
    double *dst;

    if (!ff_outlink_frame_wanted(outlink))
        return FFERROR_NOT_READY;

    if (!s->infinite && s->duration <= 0) {
        ff_outlink_set_status(outlink, AVERROR_EOF, s->pts);
        return 0;
    } else if (!s->infinite && s->duration < s->nb_samples) {
        nb_samples = s->duration;
    } else {
        nb_samples = s->nb_samples;
    }

    if (!(frame = ff_get_audio_buffer(outlink, nb_samples)))
        return AVERROR(ENOMEM);

    dst = (double *)frame->data[0];
    for (i = 0; i < nb_samples; i++) {
        double white;
        white = s->amplitude * ((2 * ((double) av_lfg_get(&s->c) / 0xffffffff)) - 1);
        dst[i] = s->filter(white, s->buf);
    }

    if (!s->infinite)
        s->duration -= nb_samples;

    frame->pts = s->pts;
    s->pts    += nb_samples;
    return ff_filter_frame(outlink, frame);
}

static const AVFilterPad anoisesrc_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_props,
    },
};

const AVFilter ff_asrc_anoisesrc = {
    .name          = "anoisesrc",
    .description   = NULL_IF_CONFIG_SMALL("Generate a noise audio signal."),
    .priv_size     = sizeof(ANoiseSrcContext),
    .inputs        = NULL,
    .activate      = activate,
    FILTER_OUTPUTS(anoisesrc_outputs),
    FILTER_QUERY_FUNC(query_formats),
    .priv_class    = &anoisesrc_class,
};
