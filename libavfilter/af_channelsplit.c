/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Channel split filter
 *
 * Split an audio stream into per-channel streams.
 */

#include "libavutil/audioconvert.h"
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
} ChannelSplitContext;

#define OFFSET(x) offsetof(ChannelSplitContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM
static const AVOption channelsplit_options[] = {
    { "channel_layout", "Input channel layout.", OFFSET(channel_layout_str), AV_OPT_TYPE_STRING, { .str = "stereo" }, .flags = A|F },
    { NULL },
};

AVFILTER_DEFINE_CLASS(channelsplit);

static int init(AVFilterContext *ctx, const char *arg)
{
    ChannelSplitContext *s = ctx->priv;
    int nb_channels;
    int ret = 0, i;

    s->class = &channelsplit_class;
    av_opt_set_defaults(s);
    if ((ret = av_set_options_string(s, arg, "=", ":")) < 0)
        return ret;
    if (!(s->channel_layout = av_get_channel_layout(s->channel_layout_str))) {
        av_log(ctx, AV_LOG_ERROR, "Error parsing channel layout '%s'.\n",
               s->channel_layout_str);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    nb_channels = av_get_channel_layout_nb_channels(s->channel_layout);
    for (i = 0; i < nb_channels; i++) {
        uint64_t channel = av_channel_layout_extract_channel(s->channel_layout, i);
        AVFilterPad pad  = { 0 };

        pad.type = AVMEDIA_TYPE_AUDIO;
        pad.name = av_get_channel_name(channel);

        ff_insert_outpad(ctx, i, &pad);
    }

fail:
    av_opt_free(s);
    return ret;
}

static int query_formats(AVFilterContext *ctx)
{
    ChannelSplitContext *s = ctx->priv;
    AVFilterChannelLayouts *in_layouts = NULL;
    int i;

    ff_set_common_formats    (ctx, ff_planar_sample_fmts());
    ff_set_common_samplerates(ctx, ff_all_samplerates());

    ff_add_channel_layout(&in_layouts, s->channel_layout);
    ff_channel_layouts_ref(in_layouts, &ctx->inputs[0]->out_channel_layouts);

    for (i = 0; i < ctx->nb_outputs; i++) {
        AVFilterChannelLayouts *out_layouts = NULL;
        uint64_t channel = av_channel_layout_extract_channel(s->channel_layout, i);

        ff_add_channel_layout(&out_layouts, channel);
        ff_channel_layouts_ref(out_layouts, &ctx->outputs[i]->in_channel_layouts);
    }

    return 0;
}

static int filter_samples(AVFilterLink *inlink, AVFilterBufferRef *buf)
{
    AVFilterContext *ctx = inlink->dst;
    int i, ret = 0;

    for (i = 0; i < ctx->nb_outputs; i++) {
        AVFilterBufferRef *buf_out = avfilter_ref_buffer(buf, ~AV_PERM_WRITE);

        if (!buf_out) {
            ret = AVERROR(ENOMEM);
            break;
        }

        buf_out->data[0] = buf_out->extended_data[0] = buf_out->extended_data[i];
        buf_out->audio->channel_layout =
            av_channel_layout_extract_channel(buf->audio->channel_layout, i);

        ret = ff_filter_samples(ctx->outputs[i], buf_out);
        if (ret < 0)
            break;
    }
    avfilter_unref_buffer(buf);
    return ret;
}

AVFilter avfilter_af_channelsplit = {
    .name           = "channelsplit",
    .description    = NULL_IF_CONFIG_SMALL("Split audio into per-channel streams"),
    .priv_size      = sizeof(ChannelSplitContext),

    .init           = init,
    .query_formats  = query_formats,

    .inputs  = (const AVFilterPad[]){{ .name           = "default",
                                       .type           = AVMEDIA_TYPE_AUDIO,
                                       .filter_samples = filter_samples, },
                                     { NULL }},
    .outputs = NULL,
    .priv_class = &channelsplit_class,
};
