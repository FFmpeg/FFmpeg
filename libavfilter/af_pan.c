/*
 * Copyright (c) 2002 Anders Johansson <ajh@atri.curtin.edu.au>
 * Copyright (c) 2011 Clément Bœsch <u pkh me>
 * Copyright (c) 2011 Nicolas George <nicolas.george@normalesup.org>
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Audio panning filter (channels mixing)
 * Original code written by Anders Johansson for MPlayer,
 * reimplemented for FFmpeg.
 */

#include <stdio.h>
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libswresample/swresample.h"
#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

#define MAX_CHANNELS 64

typedef struct PanContext {
    const AVClass *class;
    char *args;
    AVChannelLayout out_channel_layout;
    double gain[MAX_CHANNELS][MAX_CHANNELS];
    int64_t need_renorm;
    int need_renumber;
    int nb_output_channels;

    int pure_gains;
    /* channel mapping specific */
    int channel_map[MAX_CHANNELS];
    struct SwrContext *swr;
} PanContext;

static void skip_spaces(char **arg)
{
    int len = 0;

    sscanf(*arg, " %n", &len);
    *arg += len;
}

static int parse_channel_name(char **arg, int *rchannel, int *rnamed)
{
    char buf[8];
    int len, channel_id = 0;

    skip_spaces(arg);
    /* try to parse a channel name, e.g. "FL" */
    if (sscanf(*arg, "%7[A-Z]%n", buf, &len)) {
        channel_id = av_channel_from_string(buf);
        if (channel_id < 0)
            return channel_id;

        *rchannel = channel_id;
        *rnamed = 1;
        *arg += len;
        return 0;
    }
    /* try to parse a channel number, e.g. "c2" */
    if (sscanf(*arg, "c%d%n", &channel_id, &len) &&
        channel_id >= 0 && channel_id < MAX_CHANNELS) {
        *rchannel = channel_id;
        *rnamed = 0;
        *arg += len;
        return 0;
    }
    return AVERROR(EINVAL);
}

static av_cold int init(AVFilterContext *ctx)
{
    PanContext *const pan = ctx->priv;
    char *arg, *arg0, *tokenizer, *args = av_strdup(pan->args);
    int out_ch_id, in_ch_id, len, named, ret, sign = 1;
    int nb_in_channels[2] = { 0, 0 }; // number of unnamed and named input channels
    int used_out_ch[MAX_CHANNELS] = {0};
    double gain;

    if (!pan->args) {
        av_log(ctx, AV_LOG_ERROR,
               "pan filter needs a channel layout and a set "
               "of channel definitions as parameter\n");
        return AVERROR(EINVAL);
    }
    if (!args)
        return AVERROR(ENOMEM);
    arg = av_strtok(args, "|", &tokenizer);
    if (!arg) {
        av_log(ctx, AV_LOG_ERROR, "Channel layout not specified\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }
    ret = ff_parse_channel_layout(&pan->out_channel_layout,
                                  &pan->nb_output_channels, arg, ctx);
    if (ret < 0)
        goto fail;

    /* parse channel specifications */
    while ((arg = arg0 = av_strtok(NULL, "|", &tokenizer))) {
        int used_in_ch[MAX_CHANNELS] = {0};
        /* channel name */
        if (parse_channel_name(&arg, &out_ch_id, &named)) {
            av_log(ctx, AV_LOG_ERROR,
                   "Expected out channel name, got \"%.8s\"\n", arg);
            ret = AVERROR(EINVAL);
            goto fail;
        }
        if (named) {
            if ((out_ch_id = av_channel_layout_index_from_channel(&pan->out_channel_layout, out_ch_id)) < 0) {
                av_log(ctx, AV_LOG_ERROR,
                       "Channel \"%.8s\" does not exist in the chosen layout\n", arg0);
                ret = AVERROR(EINVAL);
                goto fail;
            }
        }
        if (out_ch_id < 0 || out_ch_id >= pan->nb_output_channels) {
            av_log(ctx, AV_LOG_ERROR,
                   "Invalid out channel name \"%.8s\"\n", arg0);
            ret = AVERROR(EINVAL);
            goto fail;
        }
        if (used_out_ch[out_ch_id]) {
            av_log(ctx, AV_LOG_ERROR,
                   "Can not reference out channel %d twice\n", out_ch_id);
            ret = AVERROR(EINVAL);
            goto fail;
        }
        used_out_ch[out_ch_id] = 1;
        skip_spaces(&arg);
        if (*arg == '=') {
            arg++;
        } else if (*arg == '<') {
            pan->need_renorm |= (int64_t)1 << out_ch_id;
            arg++;
        } else {
            av_log(ctx, AV_LOG_ERROR,
                   "Syntax error after channel name in \"%.8s\"\n", arg0);
            ret = AVERROR(EINVAL);
            goto fail;
        }
        /* gains */
        sign = 1;
        while (1) {
            gain = 1;
            if (sscanf(arg, "%lf%n *%n", &gain, &len, &len))
                arg += len;
            if (parse_channel_name(&arg, &in_ch_id, &named)){
                av_log(ctx, AV_LOG_ERROR,
                       "Expected in channel name, got \"%.8s\"\n", arg);
                 ret = AVERROR(EINVAL);
                 goto fail;
            }
            nb_in_channels[named]++;
            if (nb_in_channels[!named]) {
                av_log(ctx, AV_LOG_ERROR,
                       "Can not mix named and numbered channels\n");
                ret = AVERROR(EINVAL);
                goto fail;
            }
            if (used_in_ch[in_ch_id]) {
                av_log(ctx, AV_LOG_ERROR,
                       "Can not reference in channel %d twice\n", in_ch_id);
                ret = AVERROR(EINVAL);
                goto fail;
            }
            used_in_ch[in_ch_id] = 1;
            pan->gain[out_ch_id][in_ch_id] = sign * gain;
            skip_spaces(&arg);
            if (!*arg)
                break;
            if (*arg == '-') {
                sign = -1;
            } else if (*arg != '+') {
                av_log(ctx, AV_LOG_ERROR, "Syntax error near \"%.8s\"\n", arg);
                ret = AVERROR(EINVAL);
                goto fail;
            } else {
                sign = 1;
            }
            arg++;
        }
    }
    pan->need_renumber = !!nb_in_channels[1];

    ret = 0;
fail:
    av_free(args);
    return ret;
}

static int are_gains_pure(const PanContext *pan)
{
    int i, j;

    for (i = 0; i < MAX_CHANNELS; i++) {
        int nb_gain = 0;

        for (j = 0; j < MAX_CHANNELS; j++) {
            double gain = pan->gain[i][j];

            /* channel mapping is effective only if 0% or 100% of a channel is
             * selected... */
            if (gain != 0. && gain != 1.)
                return 0;
            /* ...and if the output channel is only composed of one input */
            if (gain && nb_gain++)
                return 0;
        }
    }
    return 1;
}

static int query_formats(AVFilterContext *ctx)
{
    PanContext *pan = ctx->priv;
    AVFilterLink *inlink  = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AVFilterChannelLayouts *layouts;
    int ret;

    pan->pure_gains = are_gains_pure(pan);
    /* libswr supports any sample and packing formats */
    if ((ret = ff_set_common_formats(ctx, ff_all_formats(AVMEDIA_TYPE_AUDIO))) < 0)
        return ret;

    if ((ret = ff_set_common_all_samplerates(ctx)) < 0)
        return ret;

    // inlink supports any channel layout
    layouts = ff_all_channel_counts();
    if ((ret = ff_channel_layouts_ref(layouts, &inlink->outcfg.channel_layouts)) < 0)
        return ret;

    // outlink supports only requested output channel layout
    layouts = NULL;
    if ((ret = ff_add_channel_layout(&layouts, &pan->out_channel_layout)) < 0)
        return ret;
    return ff_channel_layouts_ref(layouts, &outlink->incfg.channel_layouts);
}

static int config_props(AVFilterLink *link)
{
    AVFilterContext *ctx = link->dst;
    PanContext *pan = ctx->priv;
    char buf[1024], *cur;
    int i, j, k, r, ret;
    double t;

    if (pan->need_renumber) {
        // input channels were given by their name: renumber them
        for (i = j = 0; i < MAX_CHANNELS; i++) {
            if (av_channel_layout_index_from_channel(&link->ch_layout, i) >= 0) {
                for (k = 0; k < pan->nb_output_channels; k++)
                    pan->gain[k][j] = pan->gain[k][i];
                j++;
            }
        }
    }

    // sanity check; can't be done in query_formats since the inlink
    // channel layout is unknown at that time
    if (link->ch_layout.nb_channels > MAX_CHANNELS ||
        pan->nb_output_channels > MAX_CHANNELS) {
        av_log(ctx, AV_LOG_ERROR,
               "af_pan supports a maximum of %d channels. "
               "Feel free to ask for a higher limit.\n", MAX_CHANNELS);
        return AVERROR_PATCHWELCOME;
    }

    // init libswresample context
    ret = swr_alloc_set_opts2(&pan->swr,
                              &pan->out_channel_layout, link->format, link->sample_rate,
                              &link->ch_layout, link->format, link->sample_rate,
                              0, ctx);
    if (ret < 0)
        return AVERROR(ENOMEM);

    // gains are pure, init the channel mapping
    if (pan->pure_gains) {

        // get channel map from the pure gains
        for (i = 0; i < pan->nb_output_channels; i++) {
            int ch_id = -1;
            for (j = 0; j < link->ch_layout.nb_channels; j++) {
                if (pan->gain[i][j]) {
                    ch_id = j;
                    break;
                }
            }
            pan->channel_map[i] = ch_id;
        }

        av_opt_set_chlayout(pan->swr, "uchl", &pan->out_channel_layout, 0);
        swr_set_channel_mapping(pan->swr, pan->channel_map);
    } else {
        // renormalize
        for (i = 0; i < pan->nb_output_channels; i++) {
            if (!((pan->need_renorm >> i) & 1))
                continue;
            t = 0;
            for (j = 0; j < link->ch_layout.nb_channels; j++)
                t += fabs(pan->gain[i][j]);
            if (t > -1E-5 && t < 1E-5) {
                // t is almost 0 but not exactly, this is probably a mistake
                if (t)
                    av_log(ctx, AV_LOG_WARNING,
                           "Degenerate coefficients while renormalizing\n");
                continue;
            }
            for (j = 0; j < link->ch_layout.nb_channels; j++)
                pan->gain[i][j] /= t;
        }
        swr_set_matrix(pan->swr, pan->gain[0], pan->gain[1] - pan->gain[0]);
    }

    r = swr_init(pan->swr);
    if (r < 0)
        return r;

    // summary
    for (i = 0; i < pan->nb_output_channels; i++) {
        cur = buf;
        for (j = 0; j < link->ch_layout.nb_channels; j++) {
            r = snprintf(cur, buf + sizeof(buf) - cur, "%s%.3g i%d",
                         j ? " + " : "", pan->gain[i][j], j);
            cur += FFMIN(buf + sizeof(buf) - cur, r);
        }
        av_log(ctx, AV_LOG_VERBOSE, "o%d = %s\n", i, buf);
    }
    // add channel mapping summary if possible
    if (pan->pure_gains) {
        av_log(ctx, AV_LOG_INFO, "Pure channel mapping detected:");
        for (i = 0; i < pan->nb_output_channels; i++)
            if (pan->channel_map[i] < 0)
                av_log(ctx, AV_LOG_INFO, " M");
            else
                av_log(ctx, AV_LOG_INFO, " %d", pan->channel_map[i]);
        av_log(ctx, AV_LOG_INFO, "\n");
        return 0;
    }
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *insamples)
{
    int ret;
    int n = insamples->nb_samples;
    AVFilterLink *const outlink = inlink->dst->outputs[0];
    AVFrame *outsamples = ff_get_audio_buffer(outlink, n);
    PanContext *pan = inlink->dst->priv;

    if (!outsamples) {
        av_frame_free(&insamples);
        return AVERROR(ENOMEM);
    }
    swr_convert(pan->swr, outsamples->extended_data, n,
                (void *)insamples->extended_data, n);
    av_frame_copy_props(outsamples, insamples);
#if FF_API_OLD_CHANNEL_LAYOUT
FF_DISABLE_DEPRECATION_WARNINGS
    outsamples->channel_layout = outlink->channel_layout;
    outsamples->channels = outlink->ch_layout.nb_channels;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    if ((ret = av_channel_layout_copy(&outsamples->ch_layout, &outlink->ch_layout)) < 0)
        return ret;

    ret = ff_filter_frame(outlink, outsamples);
    av_frame_free(&insamples);
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    PanContext *pan = ctx->priv;
    swr_free(&pan->swr);
}

#define OFFSET(x) offsetof(PanContext, x)

static const AVOption pan_options[] = {
    { "args", NULL, OFFSET(args), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM },
    { NULL }
};

AVFILTER_DEFINE_CLASS(pan);

static const AVFilterPad pan_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_props,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad pan_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
};

const AVFilter ff_af_pan = {
    .name          = "pan",
    .description   = NULL_IF_CONFIG_SMALL("Remix channels with coefficients (panning)."),
    .priv_size     = sizeof(PanContext),
    .priv_class    = &pan_class,
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(pan_inputs),
    FILTER_OUTPUTS(pan_outputs),
    FILTER_QUERY_FUNC(query_formats),
};
