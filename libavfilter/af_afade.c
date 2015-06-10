/*
 * Copyright (c) 2013 Paul B Mahol
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

#include "libavutil/opt.h"
#include "audio.h"
#include "avfilter.h"
#include "internal.h"

typedef struct {
    const AVClass *class;
    int type;
    int curve;
    int nb_samples;
    int64_t start_sample;
    int64_t duration;
    int64_t start_time;

    void (*fade_samples)(uint8_t **dst, uint8_t * const *src,
                         int nb_samples, int channels, int direction,
                         int64_t start, int range, int curve);
} AudioFadeContext;

enum CurveType { TRI, QSIN, ESIN, HSIN, LOG, PAR, QUA, CUB, SQU, CBR };

#define OFFSET(x) offsetof(AudioFadeContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

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
    { "curve",        "set fade curve type",                         OFFSET(curve),        AV_OPT_TYPE_INT,    {.i64 = TRI  }, TRI, CBR, FLAGS, "curve" },
    { "c",            "set fade curve type",                         OFFSET(curve),        AV_OPT_TYPE_INT,    {.i64 = TRI  }, TRI, CBR, FLAGS, "curve" },
    { "tri",          "linear slope",                                0,                    AV_OPT_TYPE_CONST,  {.i64 = TRI  }, 0, 0, FLAGS, "curve" },
    { "qsin",         "quarter of sine wave",                        0,                    AV_OPT_TYPE_CONST,  {.i64 = QSIN }, 0, 0, FLAGS, "curve" },
    { "esin",         "exponential sine wave",                       0,                    AV_OPT_TYPE_CONST,  {.i64 = ESIN }, 0, 0, FLAGS, "curve" },
    { "hsin",         "half of sine wave",                           0,                    AV_OPT_TYPE_CONST,  {.i64 = HSIN }, 0, 0, FLAGS, "curve" },
    { "log",          "logarithmic",                                 0,                    AV_OPT_TYPE_CONST,  {.i64 = LOG  }, 0, 0, FLAGS, "curve" },
    { "par",          "inverted parabola",                           0,                    AV_OPT_TYPE_CONST,  {.i64 = PAR  }, 0, 0, FLAGS, "curve" },
    { "qua",          "quadratic",                                   0,                    AV_OPT_TYPE_CONST,  {.i64 = QUA  }, 0, 0, FLAGS, "curve" },
    { "cub",          "cubic",                                       0,                    AV_OPT_TYPE_CONST,  {.i64 = CUB  }, 0, 0, FLAGS, "curve" },
    { "squ",          "square root",                                 0,                    AV_OPT_TYPE_CONST,  {.i64 = SQU  }, 0, 0, FLAGS, "curve" },
    { "cbr",          "cubic root",                                  0,                    AV_OPT_TYPE_CONST,  {.i64 = CBR  }, 0, 0, FLAGS, "curve" },
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

    gain = FFMAX(0.0, FFMIN(1.0, 1.0 * index / range));

    switch (curve) {
    case QSIN:
        gain = sin(gain * M_PI / 2.0);
        break;
    case ESIN:
        gain = 1.0 - cos(M_PI / 4.0 * (pow(2.0*gain - 1, 3) + 1));
        break;
    case HSIN:
        gain = (1.0 - cos(gain * M_PI)) / 2.0;
        break;
    case LOG:
        gain = pow(0.1, (1 - gain) * 5.0);
        break;
    case PAR:
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

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioFadeContext *s  = ctx->priv;

    switch (inlink->format) {
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
        s->nb_samples = av_rescale(s->duration, inlink->sample_rate, AV_TIME_BASE);
    if (s->start_time)
        s->start_sample = av_rescale(s->start_time, inlink->sample_rate, AV_TIME_BASE);

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
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad avfilter_af_afade_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
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
