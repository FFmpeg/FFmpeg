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
    int64_t duration;           ///< minimum duration of silence until notification
    int mono;                   ///< mono mode : check each channel separately (default = check when ALL channels are silent)
    int channels;               ///< number of channels
    int independent_channels;   ///< number of entries in following arrays (always 1 in mono mode)
    int64_t *nb_null_samples;   ///< (array) current number of continuous zero samples
    int64_t *start;             ///< (array) if silence is detected, this value contains the time of the first zero sample (default/unset = INT64_MIN)
    int64_t frame_end;          ///< pts of the end of the current frame (used to compute duration of silence at EOS)
    int last_sample_rate;       ///< last sample rate to check for sample rate changes
    AVRational time_base;       ///< time_base

    void (*silencedetect)(struct SilenceDetectContext *s, AVFrame *insamples,
                          int nb_samples, int64_t nb_samples_notify,
                          AVRational time_base);
} SilenceDetectContext;

#define MAX_DURATION (24*3600*1000000LL)
#define OFFSET(x) offsetof(SilenceDetectContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_AUDIO_PARAM
static const AVOption silencedetect_options[] = {
    { "n",         "set noise tolerance",              OFFSET(noise),     AV_OPT_TYPE_DOUBLE, {.dbl=0.001},          0, DBL_MAX,  FLAGS },
    { "noise",     "set noise tolerance",              OFFSET(noise),     AV_OPT_TYPE_DOUBLE, {.dbl=0.001},          0, DBL_MAX,  FLAGS },
    { "d",         "set minimum duration in seconds",  OFFSET(duration),  AV_OPT_TYPE_DURATION, {.i64=2000000},      0, MAX_DURATION,FLAGS },
    { "duration",  "set minimum duration in seconds",  OFFSET(duration),  AV_OPT_TYPE_DURATION, {.i64=2000000},      0, MAX_DURATION,FLAGS },
    { "mono",      "check each channel separately",    OFFSET(mono),      AV_OPT_TYPE_BOOL,   {.i64=0},              0, 1,        FLAGS },
    { "m",         "check each channel separately",    OFFSET(mono),      AV_OPT_TYPE_BOOL,   {.i64=0},              0, 1,        FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(silencedetect);

static void set_meta(AVFrame *insamples, int channel, const char *key, char *value)
{
    char key2[128];

    if (channel)
        snprintf(key2, sizeof(key2), "lavfi.%s.%d", key, channel);
    else
        snprintf(key2, sizeof(key2), "lavfi.%s", key);
    av_dict_set(&insamples->metadata, key2, value, 0);
}
static av_always_inline void update(SilenceDetectContext *s, AVFrame *insamples,
                                    int is_silence, int current_sample, int64_t nb_samples_notify,
                                    AVRational time_base)
{
    int channel = current_sample % s->independent_channels;
    if (is_silence) {
        if (s->start[channel] == INT64_MIN) {
            s->nb_null_samples[channel]++;
            if (s->nb_null_samples[channel] >= nb_samples_notify) {
                s->start[channel] = insamples->pts + av_rescale_q(current_sample / s->channels + 1 - nb_samples_notify * s->independent_channels / s->channels,
                        (AVRational){ 1, s->last_sample_rate }, time_base);
                set_meta(insamples, s->mono ? channel + 1 : 0, "silence_start",
                        av_ts2timestr(s->start[channel], &time_base));
                if (s->mono)
                    av_log(s, AV_LOG_INFO, "channel: %d | ", channel);
                av_log(s, AV_LOG_INFO, "silence_start: %s\n",
                        av_ts2timestr(s->start[channel], &time_base));
            }
        }
    } else {
        if (s->start[channel] > INT64_MIN) {
            int64_t end_pts = insamples ? insamples->pts + av_rescale_q(current_sample / s->channels,
                    (AVRational){ 1, s->last_sample_rate }, time_base)
                    : s->frame_end;
            int64_t duration_ts = end_pts - s->start[channel];
            if (insamples) {
                set_meta(insamples, s->mono ? channel + 1 : 0, "silence_end",
                        av_ts2timestr(end_pts, &time_base));
                set_meta(insamples, s->mono ? channel + 1 : 0, "silence_duration",
                        av_ts2timestr(duration_ts, &time_base));
            }
            if (s->mono)
                av_log(s, AV_LOG_INFO, "channel: %d | ", channel);
            av_log(s, AV_LOG_INFO, "silence_end: %s | silence_duration: %s\n",
                    av_ts2timestr(end_pts, &time_base),
                    av_ts2timestr(duration_ts, &time_base));
        }
        s->nb_null_samples[channel] = 0;
        s->start[channel] = INT64_MIN;
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
        update(s, insamples, *p < noise && *p > -noise, i,                       \
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
    int c;

    s->channels = inlink->channels;
    s->duration = av_rescale(s->duration, inlink->sample_rate, AV_TIME_BASE);
    s->independent_channels = s->mono ? s->channels : 1;
    s->nb_null_samples = av_mallocz_array(sizeof(*s->nb_null_samples), s->independent_channels);
    if (!s->nb_null_samples)
        return AVERROR(ENOMEM);
    s->start = av_malloc_array(sizeof(*s->start), s->independent_channels);
    if (!s->start)
        return AVERROR(ENOMEM);
    for (c = 0; c < s->independent_channels; c++)
        s->start[c] = INT64_MIN;

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
    const int64_t nb_samples_notify = s->duration * (s->mono ? 1 : nb_channels);
    int c;

    // scale number of null samples to the new sample rate
    if (s->last_sample_rate && s->last_sample_rate != srate)
        for (c = 0; c < s->independent_channels; c++) {
            s->nb_null_samples[c] = srate * s->nb_null_samples[c] / s->last_sample_rate;
        }
    s->last_sample_rate = srate;
    s->time_base = inlink->time_base;
    s->frame_end = insamples->pts + av_rescale_q(insamples->nb_samples,
            (AVRational){ 1, s->last_sample_rate }, inlink->time_base);

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

static av_cold void uninit(AVFilterContext *ctx)
{
    SilenceDetectContext *s = ctx->priv;
    int c;

    for (c = 0; c < s->independent_channels; c++)
        if (s->start[c] > INT64_MIN)
            update(s, NULL, 0, c, 0, s->time_base);
    av_freep(&s->nb_null_samples);
    av_freep(&s->start);
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
    .uninit        = uninit,
    .inputs        = silencedetect_inputs,
    .outputs       = silencedetect_outputs,
    .priv_class    = &silencedetect_class,
};
