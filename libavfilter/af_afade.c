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

#include "config_components.h"

#include "libavutil/opt.h"
#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "internal.h"

typedef struct AudioFadeContext {
    const AVClass *class;
    int type;
    int curve, curve2;
    int64_t nb_samples;
    int64_t start_sample;
    int64_t duration;
    int64_t start_time;
    int overlap;
    int cf0_eof;
    int crossfade_is_over;
    int64_t pts;

    void (*fade_samples)(uint8_t **dst, uint8_t * const *src,
                         int nb_samples, int channels, int direction,
                         int64_t start, int64_t range, int curve);
    void (*crossfade_samples)(uint8_t **dst, uint8_t * const *cf0,
                              uint8_t * const *cf1,
                              int nb_samples, int channels,
                              int curve0, int curve1);
} AudioFadeContext;

enum CurveType { NONE = -1, TRI, QSIN, ESIN, HSIN, LOG, IPAR, QUA, CUB, SQU, CBR, PAR, EXP, IQSIN, IHSIN, DESE, DESI, LOSI, SINC, ISINC, NB_CURVES };

#define OFFSET(x) offsetof(AudioFadeContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define TFLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S16P,
        AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_S32P,
        AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_DBLP,
        AV_SAMPLE_FMT_NONE
    };

static double fade_gain(int curve, int64_t index, int64_t range)
{
#define CUBE(a) ((a)*(a)*(a))
    double gain;

    gain = av_clipd(1.0 * index / range, 0, 1.0);

    switch (curve) {
    case QSIN:
        gain = sin(gain * M_PI / 2.0);
        break;
    case IQSIN:
        /* 0.6... = 2 / M_PI */
        gain = 0.6366197723675814 * asin(gain);
        break;
    case ESIN:
        gain = 1.0 - cos(M_PI / 4.0 * (CUBE(2.0*gain - 1) + 1));
        break;
    case HSIN:
        gain = (1.0 - cos(gain * M_PI)) / 2.0;
        break;
    case IHSIN:
        /* 0.3... = 1 / M_PI */
        gain = 0.3183098861837907 * acos(1 - 2 * gain);
        break;
    case EXP:
        /* -11.5... = 5*ln(0.1) */
        gain = exp(-11.512925464970227 * (1 - gain));
        break;
    case LOG:
        gain = av_clipd(1 + 0.2 * log10(gain), 0, 1.0);
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
        gain = CUBE(gain);
        break;
    case SQU:
        gain = sqrt(gain);
        break;
    case CBR:
        gain = cbrt(gain);
        break;
    case DESE:
        gain = gain <= 0.5 ? cbrt(2 * gain) / 2: 1 - cbrt(2 * (1 - gain)) / 2;
        break;
    case DESI:
        gain = gain <= 0.5 ? CUBE(2 * gain) / 2: 1 - CUBE(2 * (1 - gain)) / 2;
        break;
    case LOSI: {
                   const double a = 1. / (1. - 0.787) - 1;
                   double A = 1. / (1.0 + exp(0 -((gain-0.5) * a * 2.0)));
                   double B = 1. / (1.0 + exp(a));
                   double C = 1. / (1.0 + exp(0-a));
                   gain = (A - B) / (C - B);
               }
        break;
    case SINC:
        gain = gain >= 1.0 ? 1.0 : sin(M_PI * (1.0 - gain)) / (M_PI * (1.0 - gain));
        break;
    case ISINC:
        gain = gain <= 0.0 ? 0.0 : 1.0 - sin(M_PI * gain) / (M_PI * gain);
        break;
    case NONE:
        gain = 1.0;
        break;
    }

    return gain;
}

#define FADE_PLANAR(name, type)                                             \
static void fade_samples_## name ##p(uint8_t **dst, uint8_t * const *src,   \
                                     int nb_samples, int channels, int dir, \
                                     int64_t start, int64_t range, int curve) \
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
                                  int64_t start, int64_t range, int curve)  \
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
    s->duration = 0;
    if (s->start_time)
        s->start_sample = av_rescale(s->start_time, outlink->sample_rate, AV_TIME_BASE);
    s->start_time = 0;

    return 0;
}

#if CONFIG_AFADE_FILTER

static const AVOption afade_options[] = {
    { "type",         "set the fade direction",                      OFFSET(type),         AV_OPT_TYPE_INT,    {.i64 = 0    }, 0, 1, TFLAGS, "type" },
    { "t",            "set the fade direction",                      OFFSET(type),         AV_OPT_TYPE_INT,    {.i64 = 0    }, 0, 1, TFLAGS, "type" },
    { "in",           "fade-in",                                     0,                    AV_OPT_TYPE_CONST,  {.i64 = 0    }, 0, 0, TFLAGS, "type" },
    { "out",          "fade-out",                                    0,                    AV_OPT_TYPE_CONST,  {.i64 = 1    }, 0, 0, TFLAGS, "type" },
    { "start_sample", "set number of first sample to start fading",  OFFSET(start_sample), AV_OPT_TYPE_INT64,  {.i64 = 0    }, 0, INT64_MAX, TFLAGS },
    { "ss",           "set number of first sample to start fading",  OFFSET(start_sample), AV_OPT_TYPE_INT64,  {.i64 = 0    }, 0, INT64_MAX, TFLAGS },
    { "nb_samples",   "set number of samples for fade duration",     OFFSET(nb_samples),   AV_OPT_TYPE_INT64,  {.i64 = 44100}, 1, INT64_MAX, TFLAGS },
    { "ns",           "set number of samples for fade duration",     OFFSET(nb_samples),   AV_OPT_TYPE_INT64,  {.i64 = 44100}, 1, INT64_MAX, TFLAGS },
    { "start_time",   "set time to start fading",                    OFFSET(start_time),   AV_OPT_TYPE_DURATION, {.i64 = 0 },  0, INT64_MAX, TFLAGS },
    { "st",           "set time to start fading",                    OFFSET(start_time),   AV_OPT_TYPE_DURATION, {.i64 = 0 },  0, INT64_MAX, TFLAGS },
    { "duration",     "set fade duration",                           OFFSET(duration),     AV_OPT_TYPE_DURATION, {.i64 = 0 },  0, INT64_MAX, TFLAGS },
    { "d",            "set fade duration",                           OFFSET(duration),     AV_OPT_TYPE_DURATION, {.i64 = 0 },  0, INT64_MAX, TFLAGS },
    { "curve",        "set fade curve type",                         OFFSET(curve),        AV_OPT_TYPE_INT,    {.i64 = TRI  }, NONE, NB_CURVES - 1, TFLAGS, "curve" },
    { "c",            "set fade curve type",                         OFFSET(curve),        AV_OPT_TYPE_INT,    {.i64 = TRI  }, NONE, NB_CURVES - 1, TFLAGS, "curve" },
    { "nofade",       "no fade; keep audio as-is",                   0,                    AV_OPT_TYPE_CONST,  {.i64 = NONE }, 0, 0, TFLAGS, "curve" },
    { "tri",          "linear slope",                                0,                    AV_OPT_TYPE_CONST,  {.i64 = TRI  }, 0, 0, TFLAGS, "curve" },
    { "qsin",         "quarter of sine wave",                        0,                    AV_OPT_TYPE_CONST,  {.i64 = QSIN }, 0, 0, TFLAGS, "curve" },
    { "esin",         "exponential sine wave",                       0,                    AV_OPT_TYPE_CONST,  {.i64 = ESIN }, 0, 0, TFLAGS, "curve" },
    { "hsin",         "half of sine wave",                           0,                    AV_OPT_TYPE_CONST,  {.i64 = HSIN }, 0, 0, TFLAGS, "curve" },
    { "log",          "logarithmic",                                 0,                    AV_OPT_TYPE_CONST,  {.i64 = LOG  }, 0, 0, TFLAGS, "curve" },
    { "ipar",         "inverted parabola",                           0,                    AV_OPT_TYPE_CONST,  {.i64 = IPAR }, 0, 0, TFLAGS, "curve" },
    { "qua",          "quadratic",                                   0,                    AV_OPT_TYPE_CONST,  {.i64 = QUA  }, 0, 0, TFLAGS, "curve" },
    { "cub",          "cubic",                                       0,                    AV_OPT_TYPE_CONST,  {.i64 = CUB  }, 0, 0, TFLAGS, "curve" },
    { "squ",          "square root",                                 0,                    AV_OPT_TYPE_CONST,  {.i64 = SQU  }, 0, 0, TFLAGS, "curve" },
    { "cbr",          "cubic root",                                  0,                    AV_OPT_TYPE_CONST,  {.i64 = CBR  }, 0, 0, TFLAGS, "curve" },
    { "par",          "parabola",                                    0,                    AV_OPT_TYPE_CONST,  {.i64 = PAR  }, 0, 0, TFLAGS, "curve" },
    { "exp",          "exponential",                                 0,                    AV_OPT_TYPE_CONST,  {.i64 = EXP  }, 0, 0, TFLAGS, "curve" },
    { "iqsin",        "inverted quarter of sine wave",               0,                    AV_OPT_TYPE_CONST,  {.i64 = IQSIN}, 0, 0, TFLAGS, "curve" },
    { "ihsin",        "inverted half of sine wave",                  0,                    AV_OPT_TYPE_CONST,  {.i64 = IHSIN}, 0, 0, TFLAGS, "curve" },
    { "dese",         "double-exponential seat",                     0,                    AV_OPT_TYPE_CONST,  {.i64 = DESE }, 0, 0, TFLAGS, "curve" },
    { "desi",         "double-exponential sigmoid",                  0,                    AV_OPT_TYPE_CONST,  {.i64 = DESI }, 0, 0, TFLAGS, "curve" },
    { "losi",         "logistic sigmoid",                            0,                    AV_OPT_TYPE_CONST,  {.i64 = LOSI }, 0, 0, TFLAGS, "curve" },
    { "sinc",         "sine cardinal function",                      0,                    AV_OPT_TYPE_CONST,  {.i64 = SINC }, 0, 0, TFLAGS, "curve" },
    { "isinc",        "inverted sine cardinal function",             0,                    AV_OPT_TYPE_CONST,  {.i64 = ISINC}, 0, 0, TFLAGS, "curve" },
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
        ( s->type && (cur_sample + nb_samples < s->start_sample)))
        return ff_filter_frame(outlink, buf);

    if (av_frame_is_writable(buf)) {
        out_buf = buf;
    } else {
        out_buf = ff_get_audio_buffer(outlink, nb_samples);
        if (!out_buf)
            return AVERROR(ENOMEM);
        av_frame_copy_props(out_buf, buf);
    }

    if ((!s->type && (cur_sample + nb_samples < s->start_sample)) ||
        ( s->type && (s->start_sample + s->nb_samples < cur_sample))) {
        av_samples_set_silence(out_buf->extended_data, 0, nb_samples,
                               out_buf->ch_layout.nb_channels, out_buf->format);
    } else {
        int64_t start;

        if (!s->type)
            start = cur_sample - s->start_sample;
        else
            start = s->start_sample + s->nb_samples - cur_sample;

        s->fade_samples(out_buf->extended_data, buf->extended_data,
                        nb_samples, buf->ch_layout.nb_channels,
                        s->type ? -1 : 1, start,
                        s->nb_samples, s->curve);
    }

    if (buf != out_buf)
        av_frame_free(&buf);

    return ff_filter_frame(outlink, out_buf);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    return config_output(ctx->outputs[0]);
}

static const AVFilterPad avfilter_af_afade_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad avfilter_af_afade_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
};

const AVFilter ff_af_afade = {
    .name          = "afade",
    .description   = NULL_IF_CONFIG_SMALL("Fade in/out input audio."),
    .priv_size     = sizeof(AudioFadeContext),
    .init          = init,
    FILTER_INPUTS(avfilter_af_afade_inputs),
    FILTER_OUTPUTS(avfilter_af_afade_outputs),
    FILTER_SAMPLEFMTS_ARRAY(sample_fmts),
    .priv_class    = &afade_class,
    .process_command = process_command,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};

#endif /* CONFIG_AFADE_FILTER */

#if CONFIG_ACROSSFADE_FILTER

static const AVOption acrossfade_options[] = {
    { "nb_samples",   "set number of samples for cross fade duration", OFFSET(nb_samples),   AV_OPT_TYPE_INT,    {.i64 = 44100}, 1, INT32_MAX/10, FLAGS },
    { "ns",           "set number of samples for cross fade duration", OFFSET(nb_samples),   AV_OPT_TYPE_INT,    {.i64 = 44100}, 1, INT32_MAX/10, FLAGS },
    { "duration",     "set cross fade duration",                       OFFSET(duration),     AV_OPT_TYPE_DURATION, {.i64 = 0 },  0, 60000000, FLAGS },
    { "d",            "set cross fade duration",                       OFFSET(duration),     AV_OPT_TYPE_DURATION, {.i64 = 0 },  0, 60000000, FLAGS },
    { "overlap",      "overlap 1st stream end with 2nd stream start",  OFFSET(overlap),      AV_OPT_TYPE_BOOL,   {.i64 = 1    }, 0,  1, FLAGS },
    { "o",            "overlap 1st stream end with 2nd stream start",  OFFSET(overlap),      AV_OPT_TYPE_BOOL,   {.i64 = 1    }, 0,  1, FLAGS },
    { "curve1",       "set fade curve type for 1st stream",            OFFSET(curve),        AV_OPT_TYPE_INT,    {.i64 = TRI  }, NONE, NB_CURVES - 1, FLAGS, "curve" },
    { "c1",           "set fade curve type for 1st stream",            OFFSET(curve),        AV_OPT_TYPE_INT,    {.i64 = TRI  }, NONE, NB_CURVES - 1, FLAGS, "curve" },
    {     "nofade",   "no fade; keep audio as-is",                     0,                    AV_OPT_TYPE_CONST,  {.i64 = NONE }, 0, 0, FLAGS, "curve" },
    {     "tri",      "linear slope",                                  0,                    AV_OPT_TYPE_CONST,  {.i64 = TRI  }, 0, 0, FLAGS, "curve" },
    {     "qsin",     "quarter of sine wave",                          0,                    AV_OPT_TYPE_CONST,  {.i64 = QSIN }, 0, 0, FLAGS, "curve" },
    {     "esin",     "exponential sine wave",                         0,                    AV_OPT_TYPE_CONST,  {.i64 = ESIN }, 0, 0, FLAGS, "curve" },
    {     "hsin",     "half of sine wave",                             0,                    AV_OPT_TYPE_CONST,  {.i64 = HSIN }, 0, 0, FLAGS, "curve" },
    {     "log",      "logarithmic",                                   0,                    AV_OPT_TYPE_CONST,  {.i64 = LOG  }, 0, 0, FLAGS, "curve" },
    {     "ipar",     "inverted parabola",                             0,                    AV_OPT_TYPE_CONST,  {.i64 = IPAR }, 0, 0, FLAGS, "curve" },
    {     "qua",      "quadratic",                                     0,                    AV_OPT_TYPE_CONST,  {.i64 = QUA  }, 0, 0, FLAGS, "curve" },
    {     "cub",      "cubic",                                         0,                    AV_OPT_TYPE_CONST,  {.i64 = CUB  }, 0, 0, FLAGS, "curve" },
    {     "squ",      "square root",                                   0,                    AV_OPT_TYPE_CONST,  {.i64 = SQU  }, 0, 0, FLAGS, "curve" },
    {     "cbr",      "cubic root",                                    0,                    AV_OPT_TYPE_CONST,  {.i64 = CBR  }, 0, 0, FLAGS, "curve" },
    {     "par",      "parabola",                                      0,                    AV_OPT_TYPE_CONST,  {.i64 = PAR  }, 0, 0, FLAGS, "curve" },
    {     "exp",      "exponential",                                   0,                    AV_OPT_TYPE_CONST,  {.i64 = EXP  }, 0, 0, FLAGS, "curve" },
    {     "iqsin",    "inverted quarter of sine wave",                 0,                    AV_OPT_TYPE_CONST,  {.i64 = IQSIN}, 0, 0, FLAGS, "curve" },
    {     "ihsin",    "inverted half of sine wave",                    0,                    AV_OPT_TYPE_CONST,  {.i64 = IHSIN}, 0, 0, FLAGS, "curve" },
    {     "dese",     "double-exponential seat",                       0,                    AV_OPT_TYPE_CONST,  {.i64 = DESE }, 0, 0, FLAGS, "curve" },
    {     "desi",     "double-exponential sigmoid",                    0,                    AV_OPT_TYPE_CONST,  {.i64 = DESI }, 0, 0, FLAGS, "curve" },
    {     "losi",     "logistic sigmoid",                              0,                    AV_OPT_TYPE_CONST,  {.i64 = LOSI }, 0, 0, FLAGS, "curve" },
    {     "sinc",     "sine cardinal function",                        0,                    AV_OPT_TYPE_CONST,  {.i64 = SINC }, 0, 0, FLAGS, "curve" },
    {     "isinc",    "inverted sine cardinal function",               0,                    AV_OPT_TYPE_CONST,  {.i64 = ISINC}, 0, 0, FLAGS, "curve" },
    { "curve2",       "set fade curve type for 2nd stream",            OFFSET(curve2),       AV_OPT_TYPE_INT,    {.i64 = TRI  }, NONE, NB_CURVES - 1, FLAGS, "curve" },
    { "c2",           "set fade curve type for 2nd stream",            OFFSET(curve2),       AV_OPT_TYPE_INT,    {.i64 = TRI  }, NONE, NB_CURVES - 1, FLAGS, "curve" },
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

static int activate(AVFilterContext *ctx)
{
    AudioFadeContext *s   = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *in = NULL, *out, *cf[2] = { NULL };
    int ret = 0, nb_samples, status;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK_ALL(outlink, ctx);

    if (s->crossfade_is_over) {
        ret = ff_inlink_consume_frame(ctx->inputs[1], &in);
        if (ret > 0) {
            in->pts = s->pts;
            s->pts += av_rescale_q(in->nb_samples,
                      (AVRational){ 1, outlink->sample_rate }, outlink->time_base);
            return ff_filter_frame(outlink, in);
        } else if (ret < 0) {
            return ret;
        } else if (ff_inlink_acknowledge_status(ctx->inputs[1], &status, &pts)) {
            ff_outlink_set_status(ctx->outputs[0], status, pts);
            return 0;
        } else if (!ret) {
            if (ff_outlink_frame_wanted(ctx->outputs[0])) {
                ff_inlink_request_frame(ctx->inputs[1]);
                return 0;
            }
        }
    }

    nb_samples = ff_inlink_queued_samples(ctx->inputs[0]);
    if (nb_samples  > s->nb_samples) {
        nb_samples -= s->nb_samples;
        ret = ff_inlink_consume_samples(ctx->inputs[0], nb_samples, nb_samples, &in);
        if (ret < 0)
            return ret;
        in->pts = s->pts;
        s->pts += av_rescale_q(in->nb_samples,
            (AVRational){ 1, outlink->sample_rate }, outlink->time_base);
        return ff_filter_frame(outlink, in);
    } else if (s->cf0_eof && nb_samples >= s->nb_samples &&
               ff_inlink_queued_samples(ctx->inputs[1]) >= s->nb_samples) {
        if (s->overlap) {
            out = ff_get_audio_buffer(outlink, s->nb_samples);
            if (!out)
                return AVERROR(ENOMEM);

            ret = ff_inlink_consume_samples(ctx->inputs[0], s->nb_samples, s->nb_samples, &cf[0]);
            if (ret < 0) {
                av_frame_free(&out);
                return ret;
            }

            ret = ff_inlink_consume_samples(ctx->inputs[1], s->nb_samples, s->nb_samples, &cf[1]);
            if (ret < 0) {
                av_frame_free(&out);
                return ret;
            }

            s->crossfade_samples(out->extended_data, cf[0]->extended_data,
                                 cf[1]->extended_data,
                                 s->nb_samples, out->ch_layout.nb_channels,
                                 s->curve, s->curve2);
            out->pts = s->pts;
            s->pts += av_rescale_q(s->nb_samples,
                (AVRational){ 1, outlink->sample_rate }, outlink->time_base);
            s->crossfade_is_over = 1;
            av_frame_free(&cf[0]);
            av_frame_free(&cf[1]);
            return ff_filter_frame(outlink, out);
        } else {
            out = ff_get_audio_buffer(outlink, s->nb_samples);
            if (!out)
                return AVERROR(ENOMEM);

            ret = ff_inlink_consume_samples(ctx->inputs[0], s->nb_samples, s->nb_samples, &cf[0]);
            if (ret < 0) {
                av_frame_free(&out);
                return ret;
            }

            s->fade_samples(out->extended_data, cf[0]->extended_data, s->nb_samples,
                            outlink->ch_layout.nb_channels, -1, s->nb_samples - 1, s->nb_samples, s->curve);
            out->pts = s->pts;
            s->pts += av_rescale_q(s->nb_samples,
                (AVRational){ 1, outlink->sample_rate }, outlink->time_base);
            av_frame_free(&cf[0]);
            ret = ff_filter_frame(outlink, out);
            if (ret < 0)
                return ret;

            out = ff_get_audio_buffer(outlink, s->nb_samples);
            if (!out)
                return AVERROR(ENOMEM);

            ret = ff_inlink_consume_samples(ctx->inputs[1], s->nb_samples, s->nb_samples, &cf[1]);
            if (ret < 0) {
                av_frame_free(&out);
                return ret;
            }

            s->fade_samples(out->extended_data, cf[1]->extended_data, s->nb_samples,
                            outlink->ch_layout.nb_channels, 1, 0, s->nb_samples, s->curve2);
            out->pts = s->pts;
            s->pts += av_rescale_q(s->nb_samples,
                (AVRational){ 1, outlink->sample_rate }, outlink->time_base);
            s->crossfade_is_over = 1;
            av_frame_free(&cf[1]);
            return ff_filter_frame(outlink, out);
        }
    } else if (ff_outlink_frame_wanted(ctx->outputs[0])) {
        if (!s->cf0_eof && ff_outlink_get_status(ctx->inputs[0])) {
            s->cf0_eof = 1;
        }
        if (ff_outlink_get_status(ctx->inputs[1])) {
            ff_outlink_set_status(ctx->outputs[0], AVERROR_EOF, AV_NOPTS_VALUE);
            return 0;
        }
        if (!s->cf0_eof)
            ff_inlink_request_frame(ctx->inputs[0]);
        else
            ff_inlink_request_frame(ctx->inputs[1]);
        return 0;
    }

    return ret;
}

static int acrossfade_config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioFadeContext *s  = ctx->priv;

    outlink->time_base   = ctx->inputs[0]->time_base;

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

    return 0;
}

static const AVFilterPad avfilter_af_acrossfade_inputs[] = {
    {
        .name         = "crossfade0",
        .type         = AVMEDIA_TYPE_AUDIO,
    },
    {
        .name         = "crossfade1",
        .type         = AVMEDIA_TYPE_AUDIO,
    },
};

static const AVFilterPad avfilter_af_acrossfade_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = acrossfade_config_output,
    },
};

const AVFilter ff_af_acrossfade = {
    .name          = "acrossfade",
    .description   = NULL_IF_CONFIG_SMALL("Cross fade two input audio streams."),
    .priv_size     = sizeof(AudioFadeContext),
    .activate      = activate,
    .priv_class    = &acrossfade_class,
    FILTER_INPUTS(avfilter_af_acrossfade_inputs),
    FILTER_OUTPUTS(avfilter_af_acrossfade_outputs),
    FILTER_SAMPLEFMTS_ARRAY(sample_fmts),
};

#endif /* CONFIG_ACROSSFADE_FILTER */
