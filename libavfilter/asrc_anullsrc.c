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

static int init(AVFilterContext *ctx)
{
    ANullContext *null = ctx->priv;
    int ret;

    if ((ret = ff_parse_sample_rate(&null->sample_rate,
                                     null->sample_rate_str, ctx)) < 0)
        return ret;

    if ((ret = ff_parse_channel_layout(&null->channel_layout,
                                        null->channel_layout_str, ctx)) < 0)
        return ret;

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    ANullContext *null = ctx->priv;
    int64_t chlayouts[] = { null->channel_layout, -1 };
    int sample_rates[] = { null->sample_rate, -1 };

    ff_set_common_formats        (ctx, ff_all_formats(AVMEDIA_TYPE_AUDIO));
    ff_set_common_channel_layouts(ctx, avfilter_make_format64_list(chlayouts));
    ff_set_common_samplerates    (ctx, ff_make_format_list(sample_rates));

    return 0;
}

static int config_props(AVFilterLink *outlink)
{
    ANullContext *null = outlink->src->priv;
    char buf[128];

    av_get_channel_layout_string(buf, sizeof(buf), 0, null->channel_layout);
    av_log(outlink->src, AV_LOG_VERBOSE,
           "sample_rate:%d channel_layout:'%s' nb_samples:%d\n",
           null->sample_rate, buf, null->nb_samples);

    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    int ret;
    ANullContext *null = outlink->src->priv;
    AVFrame *samplesref;

    samplesref = ff_get_audio_buffer(outlink, null->nb_samples);
    if (!samplesref)
        return AVERROR(ENOMEM);

    samplesref->pts = null->pts;
    samplesref->channel_layout = null->channel_layout;
    samplesref->sample_rate = outlink->sample_rate;

    ret = ff_filter_frame(outlink, av_frame_clone(samplesref));
    av_frame_free(&samplesref);
    if (ret < 0)
        return ret;

    null->pts += null->nb_samples;
    return ret;
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
    .query_formats = query_formats,
    .priv_size   = sizeof(ANullContext),

    .inputs      = NULL,

    .outputs     = avfilter_asrc_anullsrc_outputs,
    .priv_class = &anullsrc_class,
};
