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

#include "libavutil/audioconvert.h"
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

static const AVOption anullsrc_options[]= {
    { "channel_layout", "set channel_layout", OFFSET(channel_layout_str), AV_OPT_TYPE_STRING, {.str = "stereo"}, 0, 0 },
    { "cl",             "set channel_layout", OFFSET(channel_layout_str), AV_OPT_TYPE_STRING, {.str = "stereo"}, 0, 0 },
    { "sample_rate",    "set sample rate",    OFFSET(sample_rate_str)   , AV_OPT_TYPE_STRING, {.str = "44100"}, 0, 0 },
    { "r",              "set sample rate",    OFFSET(sample_rate_str)   , AV_OPT_TYPE_STRING, {.str = "44100"}, 0, 0 },
    { "nb_samples",     "set the number of samples per requested frame", OFFSET(nb_samples), AV_OPT_TYPE_INT, {.dbl = 1024}, 0, INT_MAX },
    { "n",              "set the number of samples per requested frame", OFFSET(nb_samples), AV_OPT_TYPE_INT, {.dbl = 1024}, 0, INT_MAX },
    { NULL },
};

static const char *anullsrc_get_name(void *ctx)
{
    return "anullsrc";
}

static const AVClass anullsrc_class = {
    "ANullSrcContext",
    anullsrc_get_name,
    anullsrc_options
};

static int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    ANullContext *null = ctx->priv;
    int ret;

    null->class = &anullsrc_class;
    av_opt_set_defaults(null);

    if ((ret = (av_set_options_string(null, args, "=", ":"))) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error parsing options string: '%s'\n", args);
        return ret;
    }

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
    av_log(outlink->src, AV_LOG_INFO,
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

    ff_filter_samples(outlink, avfilter_ref_buffer(samplesref, ~0));
    avfilter_unref_buffer(samplesref);

    null->pts += null->nb_samples;
    return 0;
}

AVFilter avfilter_asrc_anullsrc = {
    .name        = "anullsrc",
    .description = NULL_IF_CONFIG_SMALL("Null audio source, return empty audio frames."),

    .init        = init,
    .priv_size   = sizeof(ANullContext),

    .inputs      = (const AVFilterPad[]) {{ .name = NULL}},

    .outputs     = (const AVFilterPad[]) {{ .name = "default",
                                      .type = AVMEDIA_TYPE_AUDIO,
                                      .config_props = config_props,
                                      .request_frame = request_frame, },
                                    { .name = NULL}},
};
