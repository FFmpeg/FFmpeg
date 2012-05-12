/*
 * Copyright (c) 2012 Clément Bœsch <ubitux@gmail.com>
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

#include "libavutil/opt.h"
#include "libavutil/timestamp.h"
#include "audio.h"
#include "formats.h"
#include "avfilter.h"

typedef struct {
    const AVClass *class;
    char *noise_str;            ///< noise option string
    double noise;               ///< noise amplitude ratio
    int duration;               ///< minimum duration of silence until notification
    int64_t nb_null_samples;    ///< current number of continuous zero samples
    int64_t start;              ///< if silence is detected, this value contains the time of the first zero sample
    int last_sample_rate;       ///< last sample rate to check for sample rate changes
} SilenceDetectContext;

#define OFFSET(x) offsetof(SilenceDetectContext, x)
static const AVOption silencedetect_options[] = {
    { "n",         "set noise tolerance",              OFFSET(noise_str), AV_OPT_TYPE_STRING, {.str="-60dB"}, CHAR_MIN, CHAR_MAX },
    { "noise",     "set noise tolerance",              OFFSET(noise_str), AV_OPT_TYPE_STRING, {.str="-60dB"}, CHAR_MIN, CHAR_MAX },
    { "d",         "set minimum duration in seconds",  OFFSET(duration),  AV_OPT_TYPE_INT,    {.dbl=2},    0, INT_MAX},
    { "duration",  "set minimum duration in seconds",  OFFSET(duration),  AV_OPT_TYPE_INT,    {.dbl=2},    0, INT_MAX},
    { NULL },
};

static const char *silencedetect_get_name(void *ctx)
{
    return "silencedetect";
}

static const AVClass silencedetect_class = {
    .class_name = "SilenceDetectContext",
    .item_name  = silencedetect_get_name,
    .option     = silencedetect_options,
};

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    int ret;
    char *tail;
    SilenceDetectContext *silence = ctx->priv;

    silence->class = &silencedetect_class;
    av_opt_set_defaults(silence);

    if ((ret = av_set_options_string(silence, args, "=", ":")) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error parsing options string: '%s'\n", args);
        return ret;
    }

    silence->noise = strtod(silence->noise_str, &tail);
    if (!strcmp(tail, "dB")) {
        silence->noise = pow(10, silence->noise/20);
    } else if (*tail) {
        av_log(ctx, AV_LOG_ERROR, "Invalid value '%s' for noise parameter.\n",
               silence->noise_str);
        return AVERROR(EINVAL);
    }

    return 0;
}

static void filter_samples(AVFilterLink *inlink, AVFilterBufferRef *insamples)
{
    int i;
    SilenceDetectContext *silence = inlink->dst->priv;
    const int nb_channels           = av_get_channel_layout_nb_channels(inlink->channel_layout);
    const int srate                 = inlink->sample_rate;
    const int nb_samples            = insamples->audio->nb_samples * nb_channels;
    const int64_t nb_samples_notify = srate * silence->duration    * nb_channels;

    // scale number of null samples to the new sample rate
    if (silence->last_sample_rate && silence->last_sample_rate != srate)
        silence->nb_null_samples =
            srate * silence->nb_null_samples / silence->last_sample_rate;
    silence->last_sample_rate = srate;

    // TODO: support more sample formats
    if (insamples->format == AV_SAMPLE_FMT_DBL) {
        double *p = (double *)insamples->data[0];

        for (i = 0; i < nb_samples; i++, p++) {
            if (*p < silence->noise && *p > -silence->noise) {
                if (!silence->start) {
                    silence->nb_null_samples++;
                    if (silence->nb_null_samples >= nb_samples_notify) {
                        silence->start = insamples->pts - silence->duration / av_q2d(inlink->time_base);
                        av_log(silence, AV_LOG_INFO,
                               "silence_start: %s\n", av_ts2timestr(silence->start, &inlink->time_base));
                    }
                }
            } else {
                if (silence->start)
                    av_log(silence, AV_LOG_INFO,
                           "silence_end: %s | silence_duration: %s\n",
                           av_ts2timestr(insamples->pts,                  &inlink->time_base),
                           av_ts2timestr(insamples->pts - silence->start, &inlink->time_base));
                silence->nb_null_samples = silence->start = 0;
            }
        }
    }

    ff_filter_samples(inlink->dst->outputs[0], insamples);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_DBL,
        AV_SAMPLE_FMT_NONE
    };

    layouts = ff_all_channel_layouts();
    if (!layouts)
        return AVERROR(ENOMEM);
    ff_set_common_channel_layouts(ctx, layouts);

    formats = avfilter_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    avfilter_set_common_sample_formats(ctx, formats);

    formats = ff_all_samplerates();
    if (!formats)
        return AVERROR(ENOMEM);
    ff_set_common_samplerates(ctx, formats);

    return 0;
}

AVFilter avfilter_af_silencedetect = {
    .name          = "silencedetect",
    .description   = NULL_IF_CONFIG_SMALL("Detect silence."),
    .priv_size     = sizeof(SilenceDetectContext),
    .init          = init,
    .query_formats = query_formats,

    .inputs = (const AVFilterPad[]) {
        { .name             = "default",
          .type             = AVMEDIA_TYPE_AUDIO,
          .get_audio_buffer = ff_null_get_audio_buffer,
          .filter_samples   = filter_samples, },
        { .name = NULL }
    },
    .outputs = (const AVFilterPad[]) {
        { .name = "default",
          .type = AVMEDIA_TYPE_AUDIO, },
        { .name = NULL }
    },
};
