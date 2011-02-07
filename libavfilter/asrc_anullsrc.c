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
 * null audio source
 */

#include "avfilter.h"
#include "libavutil/audioconvert.h"

typedef struct {
    int64_t channel_layout;
    int64_t sample_rate;
} ANullContext;

static int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    ANullContext *priv = ctx->priv;
    char channel_layout_str[128] = "";

    priv->sample_rate = 44100;
    priv->channel_layout = AV_CH_LAYOUT_STEREO;

    if (args)
        sscanf(args, "%"PRId64":%s", &priv->sample_rate, channel_layout_str);

    if (priv->sample_rate < 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid negative sample rate: %"PRId64"\n", priv->sample_rate);
        return AVERROR(EINVAL);
    }

    if (*channel_layout_str)
        if (!(priv->channel_layout = av_get_channel_layout(channel_layout_str))
            && sscanf(channel_layout_str, "%"PRId64, &priv->channel_layout) != 1) {
            av_log(ctx, AV_LOG_ERROR, "Invalid value '%s' for channel layout\n",
                   channel_layout_str);
            return AVERROR(EINVAL);
        }

    return 0;
}

static int config_props(AVFilterLink *outlink)
{
    ANullContext *priv = outlink->src->priv;
    char buf[128];
    int chans_nb;

    outlink->sample_rate = priv->sample_rate;
    outlink->channel_layout = priv->channel_layout;

    chans_nb = av_get_channel_layout_nb_channels(priv->channel_layout);
    av_get_channel_layout_string(buf, sizeof(buf), chans_nb, priv->channel_layout);
    av_log(outlink->src, AV_LOG_INFO,
           "sample_rate:%"PRId64 " channel_layout:%"PRId64 " channel_layout_description:'%s'\n",
           priv->sample_rate, priv->channel_layout, buf);

    return 0;
}

static int request_frame(AVFilterLink *link)
{
    return -1;
}

AVFilter avfilter_asrc_anullsrc = {
    .name        = "anullsrc",
    .description = NULL_IF_CONFIG_SMALL("Null audio source, never return audio frames."),

    .init        = init,
    .priv_size   = sizeof(ANullContext),

    .inputs      = (AVFilterPad[]) {{ .name = NULL}},

    .outputs     = (AVFilterPad[]) {{ .name = "default",
                                      .type = AVMEDIA_TYPE_AUDIO,
                                      .config_props = config_props,
                                      .request_frame = request_frame, },
                                    { .name = NULL}},
};
