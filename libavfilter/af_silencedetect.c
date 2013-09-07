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

typedef struct {
    const AVClass *class;
    double noise;               ///< noise amplitude ratio
    double duration;            ///< minimum duration of silence until notification
    int64_t nb_null_samples;    ///< current number of continuous zero samples
    int64_t start;              ///< if silence is detected, this value contains the time of the first zero sample
    int last_sample_rate;       ///< last sample rate to check for sample rate changes
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

static int filter_frame(AVFilterLink *inlink, AVFrame *insamples)
{
    int i;
    SilenceDetectContext *silence = inlink->dst->priv;
    const int nb_channels           = inlink->channels;
    const int srate                 = inlink->sample_rate;
    const int nb_samples            = insamples->nb_samples     * nb_channels;
    const int64_t nb_samples_notify = srate * silence->duration * nb_channels;

    // scale number of null samples to the new sample rate
    if (silence->last_sample_rate && silence->last_sample_rate != srate)
        silence->nb_null_samples =
            srate * silence->nb_null_samples / silence->last_sample_rate;
    silence->last_sample_rate = srate;

    // TODO: support more sample formats
    // TODO: document metadata
    if (insamples->format == AV_SAMPLE_FMT_DBL) {
        double *p = (double *)insamples->data[0];

        for (i = 0; i < nb_samples; i++, p++) {
            if (*p < silence->noise && *p > -silence->noise) {
                if (!silence->start) {
                    silence->nb_null_samples++;
                    if (silence->nb_null_samples >= nb_samples_notify) {
                        silence->start = insamples->pts - (int64_t)(silence->duration / av_q2d(inlink->time_base) + .5);
                        av_dict_set(&insamples->metadata, "lavfi.silence_start",
                                    av_ts2timestr(silence->start, &inlink->time_base), 0);
                        av_log(silence, AV_LOG_INFO, "silence_start: %s\n",
                               get_metadata_val(insamples, "lavfi.silence_start"));
                    }
                }
            } else {
                if (silence->start) {
                    av_dict_set(&insamples->metadata, "lavfi.silence_end",
                                av_ts2timestr(insamples->pts, &inlink->time_base), 0);
                    av_dict_set(&insamples->metadata, "lavfi.silence_duration",
                                av_ts2timestr(insamples->pts - silence->start, &inlink->time_base), 0);
                    av_log(silence, AV_LOG_INFO,
                           "silence_end: %s | silence_duration: %s\n",
                           get_metadata_val(insamples, "lavfi.silence_end"),
                           get_metadata_val(insamples, "lavfi.silence_duration"));
                }
                silence->nb_null_samples = silence->start = 0;
            }
        }
    }

    return ff_filter_frame(inlink->dst->outputs[0], insamples);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_DBL,
        AV_SAMPLE_FMT_NONE
    };

    layouts = ff_all_channel_layouts();
    if (!layouts)
        return AVERROR(ENOMEM);
    ff_set_common_channel_layouts(ctx, layouts);

    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ff_set_common_formats(ctx, formats);

    formats = ff_all_samplerates();
    if (!formats)
        return AVERROR(ENOMEM);
    ff_set_common_samplerates(ctx, formats);

    return 0;
}

static const AVFilterPad silencedetect_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
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

AVFilter avfilter_af_silencedetect = {
    .name          = "silencedetect",
    .description   = NULL_IF_CONFIG_SMALL("Detect silence."),
    .priv_size     = sizeof(SilenceDetectContext),
    .query_formats = query_formats,
    .inputs        = silencedetect_inputs,
    .outputs       = silencedetect_outputs,
    .priv_class    = &silencedetect_class,
};
