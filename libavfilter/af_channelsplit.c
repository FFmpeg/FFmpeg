/*
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
 * Channel split filter
 *
 * Split an audio stream into per-channel streams.
 */

#include "libavutil/attributes.h"
#include "libavutil/channel_layout.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

typedef struct ChannelSplitContext {
    const AVClass *class;

    uint64_t channel_layout;
    char    *channel_layout_str;
    char    *channels_str;

    int      map[64];
} ChannelSplitContext;

#define OFFSET(x) offsetof(ChannelSplitContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM
static const AVOption channelsplit_options[] = {
    { "channel_layout", "Input channel layout.", OFFSET(channel_layout_str), AV_OPT_TYPE_STRING, { .str = "stereo" }, .flags = A|F },
    { "channels",        "Channels to extract.", OFFSET(channels_str),       AV_OPT_TYPE_STRING, { .str = "all" },    .flags = A|F },
    { NULL }
};

AVFILTER_DEFINE_CLASS(channelsplit);

static av_cold int init(AVFilterContext *ctx)
{
    ChannelSplitContext *s = ctx->priv;
    uint64_t channel_layout;
    int nb_channels;
    int all = 0, ret = 0, i;

    if (!(s->channel_layout = av_get_channel_layout(s->channel_layout_str))) {
        av_log(ctx, AV_LOG_ERROR, "Error parsing channel layout '%s'.\n",
               s->channel_layout_str);
        ret = AVERROR(EINVAL);
        goto fail;
    }


    if (!strcmp(s->channels_str, "all")) {
        nb_channels = av_get_channel_layout_nb_channels(s->channel_layout);
        channel_layout = s->channel_layout;
        all = 1;
    } else {
        if ((ret = av_get_extended_channel_layout(s->channels_str, &channel_layout, &nb_channels)) < 0)
            return ret;
    }

    for (i = 0; i < nb_channels; i++) {
        uint64_t channel = av_channel_layout_extract_channel(channel_layout, i);
        AVFilterPad pad  = { 0 };

        pad.type = AVMEDIA_TYPE_AUDIO;
        pad.name = av_get_channel_name(channel);

        if (all) {
            s->map[i] = i;
        } else {
            if ((ret = av_get_channel_layout_channel_index(s->channel_layout, channel)) < 0) {
                av_log(ctx, AV_LOG_ERROR, "Channel name '%s' not present in channel layout '%s'.\n",
                       av_get_channel_name(channel), s->channel_layout_str);
                return ret;
            }

            s->map[i] = ret;
        }

        if ((ret = ff_append_outpad(ctx, &pad)) < 0)
            return ret;
    }

fail:
    return ret;
}

static int query_formats(AVFilterContext *ctx)
{
    ChannelSplitContext *s = ctx->priv;
    AVFilterChannelLayouts *in_layouts = NULL;
    int i, ret;

    if ((ret = ff_set_common_formats(ctx, ff_planar_sample_fmts())) < 0 ||
        (ret = ff_set_common_all_samplerates(ctx)) < 0)
        return ret;

    if ((ret = ff_add_channel_layout(&in_layouts, s->channel_layout)) < 0 ||
        (ret = ff_channel_layouts_ref(in_layouts, &ctx->inputs[0]->outcfg.channel_layouts)) < 0)
        return ret;

    for (i = 0; i < ctx->nb_outputs; i++) {
        AVFilterChannelLayouts *out_layouts = NULL;
        uint64_t channel = av_channel_layout_extract_channel(s->channel_layout, s->map[i]);

        if ((ret = ff_add_channel_layout(&out_layouts, channel)) < 0 ||
            (ret = ff_channel_layouts_ref(out_layouts, &ctx->outputs[i]->incfg.channel_layouts)) < 0)
            return ret;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *buf)
{
    AVFilterContext *ctx = inlink->dst;
    ChannelSplitContext *s = ctx->priv;
    int i, ret = 0;

    for (i = 0; i < ctx->nb_outputs; i++) {
        AVFrame *buf_out = av_frame_clone(buf);

        if (!buf_out) {
            ret = AVERROR(ENOMEM);
            break;
        }

        buf_out->data[0] = buf_out->extended_data[0] = buf_out->extended_data[s->map[i]];
        buf_out->channel_layout =
            av_channel_layout_extract_channel(buf->channel_layout, s->map[i]);
        buf_out->channels = 1;

        ret = ff_filter_frame(ctx->outputs[i], buf_out);
        if (ret < 0)
            break;
    }
    av_frame_free(&buf);
    return ret;
}

static const AVFilterPad avfilter_af_channelsplit_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
};

const AVFilter ff_af_channelsplit = {
    .name           = "channelsplit",
    .description    = NULL_IF_CONFIG_SMALL("Split audio into per-channel streams."),
    .priv_size      = sizeof(ChannelSplitContext),
    .priv_class     = &channelsplit_class,
    .init           = init,
    FILTER_INPUTS(avfilter_af_channelsplit_inputs),
    .outputs        = NULL,
    FILTER_QUERY_FUNC(query_formats),
    .flags          = AVFILTER_FLAG_DYNAMIC_OUTPUTS,
};
