/*
 * Copyright (c) 2013-2015 Paul B Mahol
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

/**
 * @file
 * fade audio filter
 */

#include "libavutil/audio_fifo.h"
#include "libavutil/opt.h"
#include "audio.h"
#include "avfilter.h"
#include "internal.h"

typedef struct {
    const AVClass *class;
    int type;
    int curve, curve2;
    int nb_samples;
    int64_t start_sample;
    int64_t duration;
    int64_t start_time;
    int overlap;
    int cf0_eof;
    int crossfade_is_over;
    AVAudioFifo *fifo[2];
    int64_t pts;

    void (*fade_samples)(uint8_t **dst, uint8_t * const *src,
                         int nb_samples, int channels, int direction,
                         int64_t start, int range, int curve);
    void (*crossfade_samples)(uint8_t **dst, uint8_t * const *cf0,
                              uint8_t * const *cf1,
                              int nb_samples, int channels,
                              int curve0, int curve1);
} AudioFadeContext;

enum CurveType { TRI, QSIN, ESIN, HSIN, LOG, IPAR, QUA, CUB, SQU, CBR, PAR, EXP, IQSIN, IHSIN, DESE, DESI, NB_CURVES };

#define OFFSET(x) offsetof(AudioFadeContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S16P,
        AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_S32P,
        AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_DBLP,
        AV_SAMPLE_FMT_NONE
    };
    int ret;

    layouts = ff_all_channel_layouts();
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

static double fade_gain(int curve, int64_t index, int range)
{
    double gain;

    gain = av_clipd(1.0 * index / range, 0, 1.0);

    switch (curve) {
    case QSIN:
        gain = sin(gain * M_PI / 2.0);
        break;
    case IQSIN:
        gain = 0.636943 * asin(gain);
        break;
    case ESIN:
        gain = 1.0 - cos(M_PI / 4.0 * (pow(2.0*gain - 1, 3) + 1));
        break;
    case HSIN:
        gain = (1.0 - cos(gain * M_PI)) / 2.0;
        break;
    case IHSIN:
        gain = 0.318471 * acos(1 - 2 * gain);
        break;
    case EXP:
        gain = pow(0.1, (1 - gain) * 5.0);
        break;
    case LOG:
        gain = av_clipd(0.0868589 * log(100000 * gain), 0, 1.0);
        break;
    case PAR:
        gain = 1 - sqrt(1 - gain);
        break;
    case IPAR:
        gain = (1 - (1 - gain) * (1 - gain));
        break;
    case QUA:
        gain *= gain;
        break;
    case CUB:
        gain = gain * gain * gain;
        break;
    case SQU:
        gain = sqrt(gain);
        break;
    case CBR:
        gain = cbrt(gain);
        break;
    case DESE:
        gain = gain <= 0.5 ? pow(2 * gain, 1/3.) / 2: 1 - pow(2 * (1 - gain), 1/3.) / 2;
        break;
    case DESI:
        gain = gain <= 0.5 ? pow(2 * gain, 3) / 2: 1 - pow(2 * (1 - gain), 3) / 2;
        break;
    }

    return gain;
}

#define FADE_PLANAR(name, type)                                             \
static void fade_samples_## name ##p(uint8_t **dst, uint8_t * const *src,   \
                                     int nb_samples, int channels, int dir, \
                                     int64_t start, int range, int curve)   \
{                                                                           \
    int i, c;                                                               \
                                                                            \
    for (i = 0; i < nb_samples; i++) {                                      \
        double gain = fade_gain(curve, start + i * dir, range);             \
        for (c = 0; c < channels; c++) {                                    \
            type *d = (type *)dst[c];                                       \
            const type *s = (type *)src[c];                                 \
                                                                            \
            d[i] = s[i] * gain;                                             \
        }                                                                   \
    }                                                                       \
}

#define FADE(name, type)                                                    \
static void fade_samples_## name (uint8_t **dst, uint8_t * const *src,      \
                                  int nb_samples, int channels, int dir,    \
                                  int64_t start, int range, int curve)      \
{                                                                           \
    type *d = (type *)dst[0];                                               \
    const type *s = (type *)src[0];                                         \
    int i, c, k = 0;                                                        \
                                                                            \
    for (i = 0; i < nb_samples; i++) {                                      \
        double gain = fade_gain(curve, start + i * dir, range);             \
        for (c = 0; c < channels; c++, k++)                                 \
            d[k] = s[k] * gain;                                             \
    }                                                                       \
}

FADE_PLANAR(dbl, double)
FADE_PLANAR(flt, float)
FADE_PLANAR(s16, int16_t)
FADE_PLANAR(s32, int32_t)

FADE(dbl, double)
FADE(flt, float)
FADE(s16, int16_t)
FADE(s32, int32_t)

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioFadeContext *s  = ctx->priv;

    switch (outlink->format) {
    case AV_SAMPLE_FMT_DBL:  s->fade_samples = fade_samples_dbl;  break;
    case AV_SAMPLE_FMT_DBLP: s->fade_samples = fade_samples_dblp; break;
    case AV_SAMPLE_FMT_FLT:  s->fade_samples = fade_samples_flt;  break;
    case AV_SAMPLE_FMT_FLTP: s->fade_samples = fade_samples_fltp; break;
    case AV_SAMPLE_FMT_S16:  s->fade_samples = fade_samples_s16;  break;
    case AV_SAMPLE_FMT_S16P: s->fade_samples = fade_samples_s16p; break;
    case AV_SAMPLE_FMT_S32:  s->fade_samples = fade_samples_s32;  break;
    case AV_SAMPLE_FMT_S32P: s->fade_samples = fade_samples_s32p; break;
    }

    if (s->duration)
        s->nb_samples = av_rescale(s->duration, outlink->sample_rate, AV_TIME_BASE);
    if (s->start_time)
        s->start_sample = av_rescale(s->start_time, outlink->sample_rate, AV_TIME_BASE);

    return 0;
}

#if CONFIG_AFADE_FILTER

static const AVOption afade_options[] = {
    { "type",         "set the fade direction",                      OFFSET(type),         AV_OPT_TYPE_INT,    {.i64 = 0    }, 0, 1, FLAGS, "type" },
    { "t",            "set the fade direction",                      OFFSET(type),         AV_OPT_TYPE_INT,    {.i64 = 0    }, 0, 1, FLAGS, "type" },
    { "in",           "fade-in",                                     0,                    AV_OPT_TYPE_CONST,  {.i64 = 0    }, 0, 0, FLAGS, "type" },
    { "out",          "fade-out",                                    0,                    AV_OPT_TYPE_CONST,  {.i64 = 1    }, 0, 0, FLAGS, "type" },
    { "start_sample", "set number of first sample to start fading",  OFFSET(start_sample), AV_OPT_TYPE_INT64,  {.i64 = 0    }, 0, INT64_MAX, FLAGS },
    { "ss",           "set number of first sample to start fading",  OFFSET(start_sample), AV_OPT_TYPE_INT64,  {.i64 = 0    }, 0, INT64_MAX, FLAGS },
    { "nb_samples",   "set number of samples for fade duration",     OFFSET(nb_samples),   AV_OPT_TYPE_INT,    {.i64 = 44100}, 1, INT32_MAX, FLAGS },
    { "ns",           "set number of samples for fade duration",     OFFSET(nb_samples),   AV_OPT_TYPE_INT,    {.i64 = 44100}, 1, INT32_MAX, FLAGS },
    { "start_time",   "set time to start fading",                    OFFSET(start_time),   AV_OPT_TYPE_DURATION, {.i64 = 0. }, 0, INT32_MAX, FLAGS },
    { "st",           "set time to start fading",                    OFFSET(start_time),   AV_OPT_TYPE_DURATION, {.i64 = 0. }, 0, INT32_MAX, FLAGS },
    { "duration",     "set fade duration",                           OFFSET(duration),     AV_OPT_TYPE_DURATION, {.i64 = 0. }, 0, INT32_MAX, FLAGS },
    { "d",            "set fade duration",                           OFFSET(duration),     AV_OPT_TYPE_DURATION, {.i64 = 0. }, 0, INT32_MAX, FLAGS },
    { "curve",        "set fade curve type",                         OFFSET(curve),        AV_OPT_TYPE_INT,    {.i64 = TRI  }, 0, NB_CURVES - 1, FLAGS, "curve" },
    { "c",            "set fade curve type",                         OFFSET(curve),        AV_OPT_TYPE_INT,    {.i64 = TRI  }, 0, NB_CURVES - 1, FLAGS, "curve" },
    { "tri",          "linear slope",                                0,                    AV_OPT_TYPE_CONST,  {.i64 = TRI  }, 0, 0, FLAGS, "curve" },
    { "qsin",         "quarter of sine wave",                        0,                    AV_OPT_TYPE_CONST,  {.i64 = QSIN }, 0, 0, FLAGS, "curve" },
    { "esin",         "exponential sine wave",                       0,                    AV_OPT_TYPE_CONST,  {.i64 = ESIN }, 0, 0, FLAGS, "curve" },
    { "hsin",         "half of sine wave",                           0,                    AV_OPT_TYPE_CONST,  {.i64 = HSIN }, 0, 0, FLAGS, "curve" },
    { "log",          "logarithmic",                                 0,                    AV_OPT_TYPE_CONST,  {.i64 = LOG  }, 0, 0, FLAGS, "curve" },
    { "ipar",         "inverted parabola",                           0,                    AV_OPT_TYPE_CONST,  {.i64 = IPAR }, 0, 0, FLAGS, "curve" },
    { "qua",          "quadratic",                                   0,                    AV_OPT_TYPE_CONST,  {.i64 = QUA  }, 0, 0, FLAGS, "curve" },
    { "cub",          "cubic",                                       0,                    AV_OPT_TYPE_CONST,  {.i64 = CUB  }, 0, 0, FLAGS, "curve" },
    { "squ",          "square root",                                 0,                    AV_OPT_TYPE_CONST,  {.i64 = SQU  }, 0, 0, FLAGS, "curve" },
    { "cbr",          "cubic root",                                  0,                    AV_OPT_TYPE_CONST,  {.i64 = CBR  }, 0, 0, FLAGS, "curve" },
    { "par",          "parabola",                                    0,                    AV_OPT_TYPE_CONST,  {.i64 = PAR  }, 0, 0, FLAGS, "curve" },
    { "exp",          "exponential",                                 0,                    AV_OPT_TYPE_CONST,  {.i64 = EXP  }, 0, 0, FLAGS, "curve" },
    { "iqsin",        "inverted quarter of sine wave",               0,                    AV_OPT_TYPE_CONST,  {.i64 = IQSIN}, 0, 0, FLAGS, "curve" },
    { "ihsin",        "inverted half of sine wave",                  0,                    AV_OPT_TYPE_CONST,  {.i64 = IHSIN}, 0, 0, FLAGS, "curve" },
    { "dese",         "double-exponential seat",                     0,                    AV_OPT_TYPE_CONST,  {.i64 = DESE }, 0, 0, FLAGS, "curve" },
    { "desi",         "double-exponential sigmoid",                  0,                    AV_OPT_TYPE_CONST,  {.i64 = DESI }, 0, 0, FLAGS, "curve" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(afade);

static av_cold int init(AVFilterContext *ctx)
{
    AudioFadeContext *s = ctx->priv;

    if (INT64_MAX - s->nb_samples < s->start_sample)
        return AVERROR(EINVAL);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *buf)
{
    AudioFadeContext *s     = inlink->dst->priv;
    AVFilterLink *outlink   = inlink->dst->outputs[0];
    int nb_samples          = buf->nb_samples;
    AVFrame *out_buf;
    int64_t cur_sample = av_rescale_q(buf->pts, inlink->time_base, (AVRational){1, inlink->sample_rate});

    if ((!s->type && (s->start_sample + s->nb_samples < cur_sample)) ||
        ( s->type && (cur_sample + s->nb_samples < s->start_sample)))
        return ff_filter_frame(outlink, buf);

    if (av_frame_is_writable(buf)) {
        out_buf = buf;
    } else {
        out_buf = ff_get_audio_buffer(inlink, nb_samples);
        if (!out_buf)
            return AVERROR(ENOMEM);
        av_frame_copy_props(out_buf, buf);
    }

    if ((!s->type && (cur_sample + nb_samples < s->start_sample)) ||
        ( s->type && (s->start_sample + s->nb_samples < cur_sample))) {
        av_samples_set_silence(out_buf->extended_data, 0, nb_samples,
                               av_frame_get_channels(out_buf), out_buf->format);
    } else {
        int64_t start;

        if (!s->type)
            start = cur_sample - s->start_sample;
        else
            start = s->start_sample + s->nb_samples - cur_sample;

        s->fade_samples(out_buf->extended_data, buf->extended_data,
                        nb_samples, av_frame_get_channels(buf),
                        s->type ? -1 : 1, start,
                        s->nb_samples, s->curve);
    }

    if (buf != out_buf)
        av_frame_free(&buf);

    return ff_filter_frame(outlink, out_buf);
}

static const AVFilterPad avfilter_af_afade_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_af_afade_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_af_afade = {
    .name          = "afade",
    .description   = NULL_IF_CONFIG_SMALL("Fade in/out input audio."),
    .query_formats = query_formats,
    .priv_size     = sizeof(AudioFadeContext),
    .init          = init,
    .inputs        = avfilter_af_afade_inputs,
    .outputs       = avfilter_af_afade_outputs,
    .priv_class    = &afade_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};

#endif /* CONFIG_AFADE_FILTER */

#if CONFIG_ACROSSFADE_FILTER

static const AVOption acrossfade_options[] = {
    { "nb_samples",   "set number of samples for cross fade duration", OFFSET(nb_samples),   AV_OPT_TYPE_INT,    {.i64 = 44100}, 1, INT32_MAX/10, FLAGS },
    { "ns",           "set number of samples for cross fade duration", OFFSET(nb_samples),   AV_OPT_TYPE_INT,    {.i64 = 44100}, 1, INT32_MAX/10, FLAGS },
    { "duration",     "set cross fade duration",                       OFFSET(duration),     AV_OPT_TYPE_DURATION, {.i64 = 0. }, 0, 60, FLAGS },
    { "d",            "set cross fade duration",                       OFFSET(duration),     AV_OPT_TYPE_DURATION, {.i64 = 0. }, 0, 60, FLAGS },
    { "overlap",      "overlap 1st stream end with 2nd stream start",  OFFSET(overlap),      AV_OPT_TYPE_INT,    {.i64 = 1    }, 0,  1, FLAGS },
    { "o",            "overlap 1st stream end with 2nd stream start",  OFFSET(overlap),      AV_OPT_TYPE_INT,    {.i64 = 1    }, 0,  1, FLAGS },
    { "curve1",       "set fade curve type for 1st stream",            OFFSET(curve),        AV_OPT_TYPE_INT,    {.i64 = TRI  }, 0, NB_CURVES - 1, FLAGS, "curve1" },
    { "c1",           "set fade curve type for 1st stream",            OFFSET(curve),        AV_OPT_TYPE_INT,    {.i64 = TRI  }, 0, NB_CURVES - 1, FLAGS, "curve1" },
    {     "tri",      "linear slope",                                  0,                    AV_OPT_TYPE_CONST,  {.i64 = TRI  }, 0, 0, FLAGS, "curve1" },
    {     "qsin",     "quarter of sine wave",                          0,                    AV_OPT_TYPE_CONST,  {.i64 = QSIN }, 0, 0, FLAGS, "curve1" },
    {     "esin",     "exponential sine wave",                         0,                    AV_OPT_TYPE_CONST,  {.i64 = ESIN }, 0, 0, FLAGS, "curve1" },
    {     "hsin",     "half of sine wave",                             0,                    AV_OPT_TYPE_CONST,  {.i64 = HSIN }, 0, 0, FLAGS, "curve1" },
    {     "log",      "logarithmic",                                   0,                    AV_OPT_TYPE_CONST,  {.i64 = LOG  }, 0, 0, FLAGS, "curve1" },
    {     "ipar",     "inverted parabola",                             0,                    AV_OPT_TYPE_CONST,  {.i64 = IPAR }, 0, 0, FLAGS, "curve1" },
    {     "qua",      "quadratic",                                     0,                    AV_OPT_TYPE_CONST,  {.i64 = QUA  }, 0, 0, FLAGS, "curve1" },
    {     "cub",      "cubic",                                         0,                    AV_OPT_TYPE_CONST,  {.i64 = CUB  }, 0, 0, FLAGS, "curve1" },
    {     "squ",      "square root",                                   0,                    AV_OPT_TYPE_CONST,  {.i64 = SQU  }, 0, 0, FLAGS, "curve1" },
    {     "cbr",      "cubic root",                                    0,                    AV_OPT_TYPE_CONST,  {.i64 = CBR  }, 0, 0, FLAGS, "curve1" },
    {     "par",      "parabola",                                      0,                    AV_OPT_TYPE_CONST,  {.i64 = PAR  }, 0, 0, FLAGS, "curve1" },
    {     "exp",      "exponential",                                   0,                    AV_OPT_TYPE_CONST,  {.i64 = EXP  }, 0, 0, FLAGS, "curve1" },
    {     "iqsin",    "inverted quarter of sine wave",                 0,                    AV_OPT_TYPE_CONST,  {.i64 = IQSIN}, 0, 0, FLAGS, "curve1" },
    {     "ihsin",    "inverted half of sine wave",                    0,                    AV_OPT_TYPE_CONST,  {.i64 = IHSIN}, 0, 0, FLAGS, "curve1" },
    {     "dese",     "double-exponential seat",                       0,                    AV_OPT_TYPE_CONST,  {.i64 = DESE }, 0, 0, FLAGS, "curve1" },
    {     "desi",     "double-exponential sigmoid",                    0,                    AV_OPT_TYPE_CONST,  {.i64 = DESI }, 0, 0, FLAGS, "curve1" },
    { "curve2",       "set fade curve type for 2nd stream",            OFFSET(curve2),       AV_OPT_TYPE_INT,    {.i64 = TRI  }, 0, NB_CURVES - 1, FLAGS, "curve2" },
    { "c2",           "set fade curve type for 2nd stream",            OFFSET(curve2),       AV_OPT_TYPE_INT,    {.i64 = TRI  }, 0, NB_CURVES - 1, FLAGS, "curve2" },
    {     "tri",      "linear slope",                                  0,                    AV_OPT_TYPE_CONST,  {.i64 = TRI  }, 0, 0, FLAGS, "curve2" },
    {     "qsin",     "quarter of sine wave",                          0,                    AV_OPT_TYPE_CONST,  {.i64 = QSIN }, 0, 0, FLAGS, "curve2" },
    {     "esin",     "exponential sine wave",                         0,                    AV_OPT_TYPE_CONST,  {.i64 = ESIN }, 0, 0, FLAGS, "curve2" },
    {     "hsin",     "half of sine wave",                             0,                    AV_OPT_TYPE_CONST,  {.i64 = HSIN }, 0, 0, FLAGS, "curve2" },
    {     "log",      "logarithmic",                                   0,                    AV_OPT_TYPE_CONST,  {.i64 = LOG  }, 0, 0, FLAGS, "curve2" },
    {     "ipar",     "inverted parabola",                             0,                    AV_OPT_TYPE_CONST,  {.i64 = IPAR }, 0, 0, FLAGS, "curve2" },
    {     "qua",      "quadratic",                                     0,                    AV_OPT_TYPE_CONST,  {.i64 = QUA  }, 0, 0, FLAGS, "curve2" },
    {     "cub",      "cubic",                                         0,                    AV_OPT_TYPE_CONST,  {.i64 = CUB  }, 0, 0, FLAGS, "curve2" },
    {     "squ",      "square root",                                   0,                    AV_OPT_TYPE_CONST,  {.i64 = SQU  }, 0, 0, FLAGS, "curve2" },
    {     "cbr",      "cubic root",                                    0,                    AV_OPT_TYPE_CONST,  {.i64 = CBR  }, 0, 0, FLAGS, "curve2" },
    {     "par",      "parabola",                                      0,                    AV_OPT_TYPE_CONST,  {.i64 = PAR  }, 0, 0, FLAGS, "curve2" },
    {     "exp",      "exponential",                                   0,                    AV_OPT_TYPE_CONST,  {.i64 = EXP  }, 0, 0, FLAGS, "curve2" },
    {     "iqsin",    "inverted quarter of sine wave",                 0,                    AV_OPT_TYPE_CONST,  {.i64 = IQSIN}, 0, 0, FLAGS, "curve2" },
    {     "ihsin",    "inverted half of sine wave",                    0,                    AV_OPT_TYPE_CONST,  {.i64 = IHSIN}, 0, 0, FLAGS, "curve2" },
    {     "dese",     "double-exponential seat",                       0,                    AV_OPT_TYPE_CONST,  {.i64 = DESE }, 0, 0, FLAGS, "curve2" },
    {     "desi",     "double-exponential sigmoid",                    0,                    AV_OPT_TYPE_CONST,  {.i64 = DESI }, 0, 0, FLAGS, "curve2" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(acrossfade);

#define CROSSFADE_PLANAR(name, type)                                           \
static void crossfade_samples_## name ##p(uint8_t **dst, uint8_t * const *cf0, \
                                          uint8_t * const *cf1,                \
                                          int nb_samples, int channels,        \
                                          int curve0, int curve1)              \
{                                                                              \
    int i, c;                                                                  \
                                                                               \
    for (i = 0; i < nb_samples; i++) {                                         \
        double gain0 = fade_gain(curve0, nb_samples - 1 - i, nb_samples);      \
        double gain1 = fade_gain(curve1, i, nb_samples);                       \
        for (c = 0; c < channels; c++) {                                       \
            type *d = (type *)dst[c];                                          \
            const type *s0 = (type *)cf0[c];                                   \
            const type *s1 = (type *)cf1[c];                                   \
                                                                               \
            d[i] = s0[i] * gain0 + s1[i] * gain1;                              \
        }                                                                      \
    }                                                                          \
}

#define CROSSFADE(name, type)                                               \
static void crossfade_samples_## name (uint8_t **dst, uint8_t * const *cf0, \
                                       uint8_t * const *cf1,                \
                                       int nb_samples, int channels,        \
                                       int curve0, int curve1)              \
{                                                                           \
    type *d = (type *)dst[0];                                               \
    const type *s0 = (type *)cf0[0];                                        \
    const type *s1 = (type *)cf1[0];                                        \
    int i, c, k = 0;                                                        \
                                                                            \
    for (i = 0; i < nb_samples; i++) {                                      \
        double gain0 = fade_gain(curve0, nb_samples - 1 - i, nb_samples);   \
        double gain1 = fade_gain(curve1, i, nb_samples);                    \
        for (c = 0; c < channels; c++, k++)                                 \
            d[k] = s0[k] * gain0 + s1[k] * gain1;                           \
    }                                                                       \
}

CROSSFADE_PLANAR(dbl, double)
CROSSFADE_PLANAR(flt, float)
CROSSFADE_PLANAR(s16, int16_t)
CROSSFADE_PLANAR(s32, int32_t)

CROSSFADE(dbl, double)
CROSSFADE(flt, float)
CROSSFADE(s16, int16_t)
CROSSFADE(s32, int32_t)

static int acrossfade_filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx  = inlink->dst;
    AudioFadeContext *s   = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out, *cf[2] = { NULL };
    int ret = 0, nb_samples;

    if (s->crossfade_is_over) {
        in->pts = s->pts;
        s->pts += av_rescale_q(in->nb_samples,
            (AVRational){ 1, outlink->sample_rate }, outlink->time_base);
        return ff_filter_frame(outlink, in);
    } else if (inlink == ctx->inputs[0]) {
        av_audio_fifo_write(s->fifo[0], (void **)in->extended_data, in->nb_samples);

        nb_samples = av_audio_fifo_size(s->fifo[0]) - s->nb_samples;
        if (nb_samples > 0) {
            out = ff_get_audio_buffer(outlink, nb_samples);
            if (!out) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            av_audio_fifo_read(s->fifo[0], (void **)out->extended_data, nb_samples);
            out->pts = s->pts;
            s->pts += av_rescale_q(nb_samples,
                (AVRational){ 1, outlink->sample_rate }, outlink->time_base);
            ret = ff_filter_frame(outlink, out);
        }
    } else if (av_audio_fifo_size(s->fifo[1]) < s->nb_samples) {
        if (!s->overlap && av_audio_fifo_size(s->fifo[0]) > 0) {
            nb_samples = av_audio_fifo_size(s->fifo[0]);

            cf[0] = ff_get_audio_buffer(outlink, nb_samples);
            out = ff_get_audio_buffer(outlink, nb_samples);
            if (!out || !cf[0]) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            av_audio_fifo_read(s->fifo[0], (void **)cf[0]->extended_data, nb_samples);

            s->fade_samples(out->extended_data, cf[0]->extended_data, nb_samples,
                            outlink->channels, -1, nb_samples - 1, nb_samples, s->curve);
            out->pts = s->pts;
            s->pts += av_rescale_q(nb_samples,
                (AVRational){ 1, outlink->sample_rate }, outlink->time_base);
            ret = ff_filter_frame(outlink, out);
            if (ret < 0)
                goto fail;
        }

        av_audio_fifo_write(s->fifo[1], (void **)in->extended_data, in->nb_samples);
    } else if (av_audio_fifo_size(s->fifo[1]) >= s->nb_samples) {
        if (s->overlap) {
            cf[0] = ff_get_audio_buffer(outlink, s->nb_samples);
            cf[1] = ff_get_audio_buffer(outlink, s->nb_samples);
            out = ff_get_audio_buffer(outlink, s->nb_samples);
            if (!out || !cf[0] || !cf[1]) {
                av_frame_free(&out);
                ret = AVERROR(ENOMEM);
                goto fail;
            }

            av_audio_fifo_read(s->fifo[0], (void **)cf[0]->extended_data, s->nb_samples);
            av_audio_fifo_read(s->fifo[1], (void **)cf[1]->extended_data, s->nb_samples);

            s->crossfade_samples(out->extended_data, cf[0]->extended_data,
                                 cf[1]->extended_data,
                                 s->nb_samples, av_frame_get_channels(in),
                                 s->curve, s->curve2);
            out->pts = s->pts;
            s->pts += av_rescale_q(s->nb_samples,
                (AVRational){ 1, outlink->sample_rate }, outlink->time_base);
            ret = ff_filter_frame(outlink, out);
            if (ret < 0)
                goto fail;
        } else {
            out = ff_get_audio_buffer(outlink, s->nb_samples);
            cf[1] = ff_get_audio_buffer(outlink, s->nb_samples);
            if (!out || !cf[1]) {
                ret = AVERROR(ENOMEM);
                av_frame_free(&out);
                goto fail;
            }

            av_audio_fifo_read(s->fifo[1], (void **)cf[1]->extended_data, s->nb_samples);

            s->fade_samples(out->extended_data, cf[1]->extended_data, s->nb_samples,
                            outlink->channels, 1, 0, s->nb_samples, s->curve2);
            out->pts = s->pts;
            s->pts += av_rescale_q(s->nb_samples,
                (AVRational){ 1, outlink->sample_rate }, outlink->time_base);
            ret = ff_filter_frame(outlink, out);
            if (ret < 0)
                goto fail;
        }

        nb_samples = av_audio_fifo_size(s->fifo[1]);
        if (nb_samples > 0) {
            out = ff_get_audio_buffer(outlink, nb_samples);
            if (!out) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }

            av_audio_fifo_read(s->fifo[1], (void **)out->extended_data, nb_samples);
            out->pts = s->pts;
            s->pts += av_rescale_q(nb_samples,
                (AVRational){ 1, outlink->sample_rate }, outlink->time_base);
            ret = ff_filter_frame(outlink, out);
        }
        s->crossfade_is_over = 1;
    }

fail:
    av_frame_free(&in);
    av_frame_free(&cf[0]);
    av_frame_free(&cf[1]);
    return ret;
}

static int acrossfade_request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioFadeContext *s = ctx->priv;
    int ret = 0;

    if (!s->cf0_eof) {
        AVFilterLink *cf0 = ctx->inputs[0];
        ret = ff_request_frame(cf0);
        if (ret < 0 && ret != AVERROR_EOF)
            return ret;
        if (ret == AVERROR_EOF) {
            s->cf0_eof = 1;
            ret = 0;
        }
    } else {
        AVFilterLink *cf1 = ctx->inputs[1];
        int nb_samples = av_audio_fifo_size(s->fifo[1]);

        ret = ff_request_frame(cf1);
        if (ret == AVERROR_EOF && nb_samples > 0) {
            AVFrame *out = ff_get_audio_buffer(outlink, nb_samples);
            if (!out)
                return AVERROR(ENOMEM);

            av_audio_fifo_read(s->fifo[1], (void **)out->extended_data, nb_samples);
            ret = ff_filter_frame(outlink, out);
        }
    }

    return ret;
}

static int acrossfade_config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioFadeContext *s  = ctx->priv;

    if (ctx->inputs[0]->sample_rate != ctx->inputs[1]->sample_rate) {
        av_log(ctx, AV_LOG_ERROR,
               "Inputs must have the same sample rate "
               "%d for in0 vs %d for in1\n",
               ctx->inputs[0]->sample_rate, ctx->inputs[1]->sample_rate);
        return AVERROR(EINVAL);
    }

    outlink->sample_rate = ctx->inputs[0]->sample_rate;
    outlink->time_base   = ctx->inputs[0]->time_base;
    outlink->channel_layout = ctx->inputs[0]->channel_layout;
    outlink->channels = ctx->inputs[0]->channels;
    outlink->flags |= FF_LINK_FLAG_REQUEST_LOOP;

    switch (outlink->format) {
    case AV_SAMPLE_FMT_DBL:  s->crossfade_samples = crossfade_samples_dbl;  break;
    case AV_SAMPLE_FMT_DBLP: s->crossfade_samples = crossfade_samples_dblp; break;
    case AV_SAMPLE_FMT_FLT:  s->crossfade_samples = crossfade_samples_flt;  break;
    case AV_SAMPLE_FMT_FLTP: s->crossfade_samples = crossfade_samples_fltp; break;
    case AV_SAMPLE_FMT_S16:  s->crossfade_samples = crossfade_samples_s16;  break;
    case AV_SAMPLE_FMT_S16P: s->crossfade_samples = crossfade_samples_s16p; break;
    case AV_SAMPLE_FMT_S32:  s->crossfade_samples = crossfade_samples_s32;  break;
    case AV_SAMPLE_FMT_S32P: s->crossfade_samples = crossfade_samples_s32p; break;
    }

    config_output(outlink);

    s->fifo[0] = av_audio_fifo_alloc(outlink->format, outlink->channels, s->nb_samples);
    s->fifo[1] = av_audio_fifo_alloc(outlink->format, outlink->channels, s->nb_samples);
    if (!s->fifo[0] || !s->fifo[1])
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioFadeContext *s = ctx->priv;

    av_audio_fifo_free(s->fifo[0]);
    av_audio_fifo_free(s->fifo[1]);
}

static const AVFilterPad avfilter_af_acrossfade_inputs[] = {
    {
        .name         = "crossfade0",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = acrossfade_filter_frame,
    },
    {
        .name         = "crossfade1",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = acrossfade_filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_af_acrossfade_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .request_frame = acrossfade_request_frame,
        .config_props  = acrossfade_config_output,
    },
    { NULL }
};

AVFilter ff_af_acrossfade = {
    .name          = "acrossfade",
    .description   = NULL_IF_CONFIG_SMALL("Cross fade two input audio streams."),
    .query_formats = query_formats,
    .priv_size     = sizeof(AudioFadeContext),
    .uninit        = uninit,
    .priv_class    = &acrossfade_class,
    .inputs        = avfilter_af_acrossfade_inputs,
    .outputs       = avfilter_af_acrossfade_outputs,
};

#endif /* CONFIG_ACROSSFADE_FILTER */
