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
#include "libavutil/avassert.h"
#include "libavutil/attributes.h"
#include "libavutil/channel_layout.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"

typedef struct ChannelSplitContext {
    const AVClass *class;

    AVChannelLayout channel_layout;
    char    *channels_str;

    int     *map;
} ChannelSplitContext;

#define OFFSET(x) offsetof(ChannelSplitContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM
static const AVOption channelsplit_options[] = {
    { "channel_layout", "Input channel layout.", OFFSET(channel_layout),   AV_OPT_TYPE_CHLAYOUT, { .str = "stereo" }, .flags = A|F },
    { "channels",        "Channels to extract.", OFFSET(channels_str),       AV_OPT_TYPE_STRING, { .str = "all" },    .flags = A|F },
    { NULL }
};

AVFILTER_DEFINE_CLASS(channelsplit);

static av_cold int init(AVFilterContext *ctx)
{
    ChannelSplitContext *s = ctx->priv;
    AVChannelLayout channel_layout = { 0 };
    int all = 0, ret = 0, i;

    if (!strcmp(s->channels_str, "all")) {
        if ((ret = av_channel_layout_copy(&channel_layout, &s->channel_layout)) < 0)
            goto fail;
        all = 1;
    } else {
        if ((ret = av_channel_layout_from_string(&channel_layout, s->channels_str)) < 0)
            goto fail;
    }

    s->map = av_calloc(channel_layout.nb_channels, sizeof(*s->map));
    if (!s->map)
        return AVERROR(ENOMEM);

    for (i = 0; i < channel_layout.nb_channels; i++) {
        enum AVChannel channel = av_channel_layout_channel_from_index(&channel_layout, i);
        char buf[64];
        AVFilterPad pad = { .flags = AVFILTERPAD_FLAG_FREE_NAME };

        av_channel_name(buf, sizeof(buf), channel);
        pad.type = AVMEDIA_TYPE_AUDIO;
        pad.name = av_strdup(buf);
        if (!pad.name) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        if (all) {
            s->map[i] = i;
        } else {
            char buf[128];
            av_channel_layout_describe(&s->channel_layout, buf, sizeof(buf));
            if ((ret = av_channel_layout_index_from_channel(&s->channel_layout, channel)) < 0) {
                av_log(ctx, AV_LOG_ERROR, "Channel name '%s' not present in channel layout '%s'.\n",
                       pad.name, buf);
                av_freep(&pad.name);
                goto fail;
            }

            s->map[i] = ret;
        }

        if ((ret = ff_append_outpad(ctx, &pad)) < 0)
            goto fail;
    }

fail:
    av_channel_layout_uninit(&channel_layout);
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ChannelSplitContext *s = ctx->priv;

    av_channel_layout_uninit(&s->channel_layout);
    av_freep(&s->map);
}

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    ChannelSplitContext *s = ctx->priv;
    AVFilterChannelLayouts *in_layouts = NULL;
    int i, ret;

    ret = ff_set_common_formats2(ctx, cfg_in, cfg_out, ff_planar_sample_fmts());
    if (ret < 0)
        return ret;

    if ((ret = ff_add_channel_layout(&in_layouts, &s->channel_layout)) < 0 ||
        (ret = ff_channel_layouts_ref(in_layouts, &cfg_in[0]->channel_layouts)) < 0)
        return ret;

    for (i = 0; i < ctx->nb_outputs; i++) {
        AVChannelLayout channel_layout = { 0 };
        AVFilterChannelLayouts *out_layouts = NULL;
        enum AVChannel channel = av_channel_layout_channel_from_index(&s->channel_layout, s->map[i]);

        channel_layout.u.map = av_mallocz(sizeof(*channel_layout.u.map));
        if (!channel_layout.u.map)
            return AVERROR(ENOMEM);

        channel_layout.u.map[0].id = channel;
        channel_layout.nb_channels = 1;
        channel_layout.order       = AV_CHANNEL_ORDER_CUSTOM;

        ret = av_channel_layout_retype(&channel_layout, 0, AV_CHANNEL_LAYOUT_RETYPE_FLAG_CANONICAL);
        if (ret < 0) {
            av_channel_layout_uninit(&channel_layout);
            return ret;
        }

        ret = ff_add_channel_layout(&out_layouts, &channel_layout);
        av_channel_layout_uninit(&channel_layout);
        if (ret < 0)
            return ret;

        ret = ff_channel_layouts_ref(out_layouts, &cfg_out[i]->channel_layouts);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int filter_frame(AVFilterLink *outlink, AVFrame *buf)
{
    AVFrame *buf_out;
    AVFilterContext *ctx = outlink->src;
    ChannelSplitContext *s = ctx->priv;
    const int i = FF_OUTLINK_IDX(outlink);
    int ret;

    buf_out = av_frame_clone(buf);
    if (!buf_out)
        return AVERROR(ENOMEM);

    buf_out->data[0] = buf_out->extended_data[0] = buf_out->extended_data[s->map[i]];

    av_channel_layout_uninit(&buf_out->ch_layout);
    ret = av_channel_layout_copy(&buf_out->ch_layout, &outlink->ch_layout);
    if (ret < 0) {
        av_frame_free(&buf_out);
        return ret;
    }

    return ff_filter_frame(ctx->outputs[i], buf_out);
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    int status, ret;
    AVFrame *in;
    int64_t pts;

    for (int i = 0; i < ctx->nb_outputs; i++) {
        FF_FILTER_FORWARD_STATUS_BACK_ALL(ctx->outputs[i], ctx);
    }

    ret = ff_inlink_consume_frame(inlink, &in);
    if (ret < 0)
        return ret;
    if (ret > 0) {
        for (int i = 0; i < ctx->nb_outputs; i++) {
            if (ff_outlink_get_status(ctx->outputs[i]))
                continue;

            ret = filter_frame(ctx->outputs[i], in);
            if (ret < 0)
                break;
        }

        av_frame_free(&in);
        if (ret < 0)
            return ret;
    }

    if (ff_inlink_acknowledge_status(inlink, &status, &pts)) {
        for (int i = 0; i < ctx->nb_outputs; i++) {
            if (ff_outlink_get_status(ctx->outputs[i]))
                continue;
            ff_outlink_set_status(ctx->outputs[i], status, pts);
        }
        return 0;
    }

    for (int i = 0; i < ctx->nb_outputs; i++) {
        if (ff_outlink_get_status(ctx->outputs[i]))
            continue;

        if (ff_outlink_frame_wanted(ctx->outputs[i])) {
            ff_inlink_request_frame(inlink);
            return 0;
        }
    }

    return FFERROR_NOT_READY;
}

const AVFilter ff_af_channelsplit = {
    .name           = "channelsplit",
    .description    = NULL_IF_CONFIG_SMALL("Split audio into per-channel streams."),
    .priv_size      = sizeof(ChannelSplitContext),
    .priv_class     = &channelsplit_class,
    .init           = init,
    .activate       = activate,
    .uninit         = uninit,
    FILTER_INPUTS(ff_audio_default_filterpad),
    .outputs        = NULL,
    FILTER_QUERY_FUNC2(query_formats),
    .flags          = AVFILTER_FLAG_DYNAMIC_OUTPUTS,
};
