/*
 * Copyright (c) 2012 Clément Bœsch <u pkh me>
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
 * Audio silence detector
 */

#include <float.h> /* DBL_MAX */

#include "libavutil/opt.h"
#include "libavutil/timestamp.h"
#include "audio.h"
#include "formats.h"
#include "avfilter.h"
#include "internal.h"

typedef struct SilenceDetectContext {
    const AVClass *class;
    double noise;               ///< noise amplitude ratio
    double duration;            ///< minimum duration of silence until notification
    int64_t nb_null_samples;    ///< current number of continuous zero samples
    int64_t start;              ///< if silence is detected, this value contains the time of the first zero sample
    int last_sample_rate;       ///< last sample rate to check for sample rate changes

    void (*silencedetect)(struct SilenceDetectContext *s, AVFrame *insamples,
                          int nb_samples, int64_t nb_samples_notify,
                          AVRational time_base);
} SilenceDetectContext;

#define OFFSET(x) offsetof(SilenceDetectContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_AUDIO_PARAM
static const AVOption silencedetect_options[] = {
    { "n",         "set noise tolerance",              OFFSET(noise),     AV_OPT_TYPE_DOUBLE, {.dbl=0.001},          0, DBL_MAX,  FLAGS },
    { "noise",     "set noise tolerance",              OFFSET(noise),     AV_OPT_TYPE_DOUBLE, {.dbl=0.001},          0, DBL_MAX,  FLAGS },
    { "d",         "set minimum duration in seconds",  OFFSET(duration),  AV_OPT_TYPE_DOUBLE, {.dbl=2.},             0, 24*60*60, FLAGS },
    { "duration",  "set minimum duration in seconds",  OFFSET(duration),  AV_OPT_TYPE_DOUBLE, {.dbl=2.},             0, 24*60*60, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(silencedetect);

static char *get_metadata_val(AVFrame *insamples, const char *key)
{
    AVDictionaryEntry *e = av_dict_get(insamples->metadata, key, NULL, 0);
    return e && e->value ? e->value : NULL;
}

static av_always_inline void update(SilenceDetectContext *s, AVFrame *insamples,
                                    int is_silence, int64_t nb_samples_notify,
                                    AVRational time_base)
{
    if (is_silence) {
        if (!s->start) {
            s->nb_null_samples++;
            if (s->nb_null_samples >= nb_samples_notify) {
                s->start = insamples->pts - (int64_t)(s->duration / av_q2d(time_base) + .5);
                av_dict_set(&insamples->metadata, "lavfi.silence_start",
                            av_ts2timestr(s->start, &time_base), 0);
                av_log(s, AV_LOG_INFO, "silence_start: %s\n",
                       get_metadata_val(insamples, "lavfi.silence_start"));
            }
        }
    } else {
        if (s->start) {
            av_dict_set(&insamples->metadata, "lavfi.silence_end",
                        av_ts2timestr(insamples->pts, &time_base), 0);
            av_dict_set(&insamples->metadata, "lavfi.silence_duration",
                        av_ts2timestr(insamples->pts - s->start, &time_base), 0);
            av_log(s, AV_LOG_INFO,
                   "silence_end: %s | silence_duration: %s\n",
                   get_metadata_val(insamples, "lavfi.silence_end"),
                   get_metadata_val(insamples, "lavfi.silence_duration"));
        }
        s->nb_null_samples = s->start = 0;
    }
}

#define SILENCE_DETECT(name, type)                                               \
static void silencedetect_##name(SilenceDetectContext *s, AVFrame *insamples,    \
                                 int nb_samples, int64_t nb_samples_notify,      \
                                 AVRational time_base)                           \
{                                                                                \
    const type *p = (const type *)insamples->data[0];                            \
    const type noise = s->noise;                                                 \
    int i;                                                                       \
                                                                                 \
    for (i = 0; i < nb_samples; i++, p++)                                        \
        update(s, insamples, *p < noise && *p > -noise,                          \
               nb_samples_notify, time_base);                                    \
}

SILENCE_DETECT(dbl, double)
SILENCE_DETECT(flt, float)
SILENCE_DETECT(s32, int32_t)
SILENCE_DETECT(s16, int16_t)

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    SilenceDetectContext *s = ctx->priv;

    switch (inlink->format) {
    case AV_SAMPLE_FMT_DBL: s->silencedetect = silencedetect_dbl; break;
    case AV_SAMPLE_FMT_FLT: s->silencedetect = silencedetect_flt; break;
    case AV_SAMPLE_FMT_S32:
        s->noise *= INT32_MAX;
        s->silencedetect = silencedetect_s32;
        break;
    case AV_SAMPLE_FMT_S16:
        s->noise *= INT16_MAX;
        s->silencedetect = silencedetect_s16;
        break;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *insamples)
{
    SilenceDetectContext *s         = inlink->dst->priv;
    const int nb_channels           = inlink->channels;
    const int srate                 = inlink->sample_rate;
    const int nb_samples            = insamples->nb_samples     * nb_channels;
    const int64_t nb_samples_notify = srate * s->duration * nb_channels;

    // scale number of null samples to the new sample rate
    if (s->last_sample_rate && s->last_sample_rate != srate)
        s->nb_null_samples = srate * s->nb_null_samples / s->last_sample_rate;
    s->last_sample_rate = srate;

    // TODO: document metadata
    s->silencedetect(s, insamples, nb_samples, nb_samples_notify,
                     inlink->time_base);

    return ff_filter_frame(inlink->dst->outputs[0], insamples);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_DBL,
        AV_SAMPLE_FMT_FLT,
        AV_SAMPLE_FMT_S32,
        AV_SAMPLE_FMT_S16,
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

static const AVFilterPad silencedetect_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad silencedetect_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_silencedetect = {
    .name          = "silencedetect",
    .description   = NULL_IF_CONFIG_SMALL("Detect silence."),
    .priv_size     = sizeof(SilenceDetectContext),
    .query_formats = query_formats,
    .inputs        = silencedetect_inputs,
    .outputs       = silencedetect_outputs,
    .priv_class    = &silencedetect_class,
};
