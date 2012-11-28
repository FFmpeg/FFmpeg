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
#include "internal.h"

typedef struct {
    const AVClass *class;
    char   *channel_layout_str;
    uint64_t channel_layout;
    char   *sample_rate_str;
    int     sample_rate;
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
    { "nb_samples",     "set the number of samples per requested frame", OFFSET(nb_samples), AV_OPT_TYPE_INT, {.i64 = 1024}, 0, INT_MAX, FLAGS },
    { "n",              "set the number of samples per requested frame", OFFSET(nb_samples), AV_OPT_TYPE_INT, {.i64 = 1024}, 0, INT_MAX, FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(anullsrc);

static int init(AVFilterContext *ctx, const char *args)
{
    ANullContext *null = ctx->priv;
    int ret;

    null->class = &anullsrc_class;
    av_opt_set_defaults(null);

    if ((ret = (av_set_options_string(null, args, "=", ":"))) < 0)
        return ret;

    if ((ret = ff_parse_sample_rate(&null->sample_rate,
                                     null->sample_rate_str, ctx)) < 0)
        return ret;

    if ((ret = ff_parse_channel_layout(&null->channel_layout,
                                        null->channel_layout_str, ctx)) < 0)
        return ret;

    return 0;
}

static int config_props(AVFilterLink *outlink)
{
    ANullContext *null = outlink->src->priv;
    char buf[128];
    int chans_nb;

    outlink->sample_rate = null->sample_rate;
    outlink->channel_layout = null->channel_layout;

    chans_nb = av_get_channel_layout_nb_channels(null->channel_layout);
    av_get_channel_layout_string(buf, sizeof(buf), chans_nb, null->channel_layout);
    av_log(outlink->src, AV_LOG_VERBOSE,
           "sample_rate:%d channel_layout:'%s' nb_samples:%d\n",
           null->sample_rate, buf, null->nb_samples);

    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    ANullContext *null = outlink->src->priv;
    AVFilterBufferRef *samplesref;

    samplesref =
        ff_get_audio_buffer(outlink, AV_PERM_WRITE, null->nb_samples);
    samplesref->pts = null->pts;
    samplesref->pos = -1;
    samplesref->audio->channel_layout = null->channel_layout;
    samplesref->audio->sample_rate = outlink->sample_rate;

    ff_filter_frame(outlink, avfilter_ref_buffer(samplesref, ~0));
    avfilter_unref_buffer(samplesref);

    null->pts += null->nb_samples;
    return 0;
}

static const AVFilterPad avfilter_asrc_anullsrc_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_props,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter avfilter_asrc_anullsrc = {
    .name        = "anullsrc",
    .description = NULL_IF_CONFIG_SMALL("Null audio source, return empty audio frames."),

    .init        = init,
    .priv_size   = sizeof(ANullContext),

    .inputs      = NULL,

    .outputs     = avfilter_asrc_anullsrc_outputs,
    .priv_class = &anullsrc_class,
};
