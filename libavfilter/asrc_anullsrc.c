/*
 * Copyright 2010 S.N. Hemanth Meenakshisundaram <smeenaks ucsd edu>
 * Copyright 2010 Stefano Sabatini <stefano.sabatini-lala poste it>
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
 * null audio source
 */

#include <inttypes.h>
#include <stdio.h>

#include "libavutil/channel_layout.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "internal.h"

typedef struct ANullContext {
    const AVClass *class;
    char   *channel_layout_str;
    uint64_t channel_layout;
    char   *sample_rate_str;
    int     sample_rate;
    int64_t duration;
    int nb_samples;             ///< number of samples per requested frame
    int64_t pts;
} ANullContext;

#define OFFSET(x) offsetof(ANullContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption anullsrc_options[]= {
    { "channel_layout", "set channel_layout", OFFSET(channel_layout_str), AV_OPT_TYPE_STRING, {.str = "stereo"}, 0, 0, FLAGS },
    { "cl",             "set channel_layout", OFFSET(channel_layout_str), AV_OPT_TYPE_STRING, {.str = "stereo"}, 0, 0, FLAGS },
    { "sample_rate",    "set sample rate",    OFFSET(sample_rate_str)   , AV_OPT_TYPE_STRING, {.str = "44100"}, 0, 0, FLAGS },
    { "r",              "set sample rate",    OFFSET(sample_rate_str)   , AV_OPT_TYPE_STRING, {.str = "44100"}, 0, 0, FLAGS },
    { "nb_samples",     "set the number of samples per requested frame", OFFSET(nb_samples), AV_OPT_TYPE_INT, {.i64 = 1024}, 1, UINT16_MAX, FLAGS },
    { "n",              "set the number of samples per requested frame", OFFSET(nb_samples), AV_OPT_TYPE_INT, {.i64 = 1024}, 1, UINT16_MAX, FLAGS },
    { "duration",       "set the audio duration",                        OFFSET(duration),   AV_OPT_TYPE_DURATION, {.i64 = -1}, -1, INT64_MAX, FLAGS },
    { "d",              "set the audio duration",                        OFFSET(duration),   AV_OPT_TYPE_DURATION, {.i64 = -1}, -1, INT64_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(anullsrc);

static av_cold int init(AVFilterContext *ctx)
{
    ANullContext *null = ctx->priv;
    int ret;

    if ((ret = ff_parse_sample_rate(&null->sample_rate,
                                     null->sample_rate_str, ctx)) < 0)
        return ret;

    if ((ret = ff_parse_channel_layout(&null->channel_layout, NULL,
                                        null->channel_layout_str, ctx)) < 0)
        return ret;

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    ANullContext *null = ctx->priv;
    int64_t chlayouts[] = { null->channel_layout, -1 };
    int sample_rates[] = { null->sample_rate, -1 };
    int ret;

    if ((ret = ff_set_common_formats         (ctx, ff_all_formats              (AVMEDIA_TYPE_AUDIO))) < 0 ||
        (ret = ff_set_common_samplerates     (ctx, ff_make_format_list         (sample_rates      ))) < 0)
        return ret;

    return ff_set_common_channel_layouts(ctx, ff_make_format64_list(chlayouts));
}

static av_cold int config_props(AVFilterLink *outlink)
{
    ANullContext *null = outlink->src->priv;

    if (null->duration >= 0)
        null->duration = av_rescale(null->duration, null->sample_rate, AV_TIME_BASE);

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    ANullContext *null = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    if (null->duration >= 0 && null->pts >= null->duration) {
        ff_outlink_set_status(outlink, AVERROR_EOF, null->pts);
        return 0;
    }

    if (ff_outlink_frame_wanted(outlink)) {
        AVFrame *samplesref = ff_get_audio_buffer(outlink, null->duration >= 0 ? FFMIN(null->nb_samples, null->duration - null->pts) : null->nb_samples);

        if (!samplesref)
            return AVERROR(ENOMEM);

        samplesref->pts = null->pts;
        null->pts += samplesref->nb_samples;

        return ff_filter_frame(outlink, samplesref);
    }

    return FFERROR_NOT_READY;
}

static const AVFilterPad avfilter_asrc_anullsrc_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_props,
    },
    { NULL }
};

AVFilter ff_asrc_anullsrc = {
    .name          = "anullsrc",
    .description   = NULL_IF_CONFIG_SMALL("Null audio source, return empty audio frames."),
    .init          = init,
    .query_formats = query_formats,
    .priv_size     = sizeof(ANullContext),
    .inputs        = NULL,
    .outputs       = avfilter_asrc_anullsrc_outputs,
    .activate      = activate,
    .priv_class    = &anullsrc_class,
};
