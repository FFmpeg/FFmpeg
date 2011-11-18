/*
 * Copyright (c) 2002 Anders Johansson <ajh@atri.curtin.edu.au>
 * Copyright (c) 2011 Clément Bœsch <ubitux@gmail.com>
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
 * GNU General Public License for more details.
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
#include "libavutil/audioconvert.h"
#include "libavutil/avstring.h"
#include "avfilter.h"
#include "internal.h"

#define MAX_CHANNELS 63

typedef struct {
    int64_t out_channel_layout;
    union {
        double d[MAX_CHANNELS][MAX_CHANNELS];
        // i is 1:7:8 fixed-point, i.e. in [-128*256; +128*256[
        int    i[MAX_CHANNELS][MAX_CHANNELS];
    } gain;
    int64_t need_renorm;
    int need_renumber;
    int nb_input_channels;
    int nb_output_channels;
} PanContext;

static int parse_channel_name(char **arg, int *rchannel, int *rnamed)
{
    char buf[8];
    int len, i, channel_id;
    int64_t layout, layout0;

    if (sscanf(*arg, " %7[A-Z] %n", buf, &len)) {
        layout0 = layout = av_get_channel_layout(buf);
        for (i = 32; i > 0; i >>= 1) {
            if (layout >= (int64_t)1 << i) {
                channel_id += i;
                layout >>= i;
            }
        }
        if (channel_id >= MAX_CHANNELS || layout0 != (int64_t)1 << channel_id)
            return AVERROR(EINVAL);
        *rchannel = channel_id;
        *rnamed = 1;
        *arg += len;
        return 0;
    }
    if (sscanf(*arg, " c%d %n", &channel_id, &len) &&
        channel_id >= 0 && channel_id < MAX_CHANNELS) {
        *rchannel = channel_id;
        *rnamed = 0;
        *arg += len;
        return 0;
    }
    return AVERROR(EINVAL);
}

static void skip_spaces(char **arg)
{
    int len = 0;

    sscanf(*arg, " %n", &len);
    *arg += len;
}

static av_cold int init(AVFilterContext *ctx, const char *args0, void *opaque)
{
    PanContext *const pan = ctx->priv;
    char *arg, *arg0, *tokenizer, *args = av_strdup(args0);
    int out_ch_id, in_ch_id, len, named;
    int nb_in_channels[2] = { 0, 0 }; // number of unnamed and named input channels
    double gain;

    if (!args)
        return AVERROR(ENOMEM);
    arg = av_strtok(args, ":", &tokenizer);
    pan->out_channel_layout = av_get_channel_layout(arg);
    if (!pan->out_channel_layout) {
        av_log(ctx, AV_LOG_ERROR, "Unknown channel layout \"%s\"\n", arg);
        return AVERROR(EINVAL);
    }
    pan->nb_output_channels = av_get_channel_layout_nb_channels(pan->out_channel_layout);

    /* parse channel specifications */
    while ((arg = arg0 = av_strtok(NULL, ":", &tokenizer))) {
        /* channel name */
        if (parse_channel_name(&arg, &out_ch_id, &named)) {
            av_log(ctx, AV_LOG_ERROR,
                   "Expected out channel name, got \"%.8s\"\n", arg);
            return AVERROR(EINVAL);
        }
        if (named) {
            if (!((pan->out_channel_layout >> out_ch_id) & 1)) {
                av_log(ctx, AV_LOG_ERROR,
                       "Channel \"%.8s\" does not exist in the chosen layout\n", arg0);
                return AVERROR(EINVAL);
            }
            /* get the channel number in the output channel layout:
             * out_channel_layout & ((1 << out_ch_id) - 1) are all the
             * channels that come before out_ch_id,
             * so their count is the index of out_ch_id */
            out_ch_id = av_get_channel_layout_nb_channels(pan->out_channel_layout & (((int64_t)1 << out_ch_id) - 1));
        }
        if (out_ch_id < 0 || out_ch_id >= pan->nb_output_channels) {
            av_log(ctx, AV_LOG_ERROR,
                   "Invalid out channel name \"%.8s\"\n", arg0);
            return AVERROR(EINVAL);
        }
        if (*arg == '=') {
            arg++;
        } else if (*arg == '<') {
            pan->need_renorm |= (int64_t)1 << out_ch_id;
            arg++;
        } else {
            av_log(ctx, AV_LOG_ERROR,
                   "Syntax error after channel name in \"%.8s\"\n", arg0);
            return AVERROR(EINVAL);
        }
        /* gains */
        while (1) {
            gain = 1;
            if (sscanf(arg, " %lf %n* %n", &gain, &len, &len))
                arg += len;
            if (parse_channel_name(&arg, &in_ch_id, &named)){
                av_log(ctx, AV_LOG_ERROR,
                       "Expected in channel name, got \"%.8s\"\n", arg);
                return AVERROR(EINVAL);
            }
            nb_in_channels[named]++;
            if (nb_in_channels[!named]) {
                av_log(ctx, AV_LOG_ERROR,
                       "Can not mix named and numbered channels\n");
                return AVERROR(EINVAL);
            }
            pan->gain.d[out_ch_id][in_ch_id] = gain;
            if (!*arg)
                break;
            if (*arg != '+') {
                av_log(ctx, AV_LOG_ERROR, "Syntax error near \"%.8s\"\n", arg);
                return AVERROR(EINVAL);
            }
            arg++;
            skip_spaces(&arg);
        }
    }
    pan->need_renumber = !!nb_in_channels[1];

    av_free(args);
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    PanContext *pan = ctx->priv;
    AVFilterLink *inlink  = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AVFilterFormats *formats;

    const enum AVSampleFormat sample_fmts[] = {AV_SAMPLE_FMT_S16, -1};
    const int                packing_fmts[] = {AVFILTER_PACKED,   -1};

    avfilter_set_common_sample_formats (ctx, avfilter_make_format_list(sample_fmts));
    avfilter_set_common_packing_formats(ctx, avfilter_make_format_list(packing_fmts));

    // inlink supports any channel layout
    formats = avfilter_make_all_channel_layouts();
    avfilter_formats_ref(formats, &inlink->out_chlayouts);

    // outlink supports only requested output channel layout
    formats = NULL;
    avfilter_add_format(&formats, pan->out_channel_layout);
    avfilter_formats_ref(formats, &outlink->in_chlayouts);
    return 0;
}

static int config_props(AVFilterLink *link)
{
    AVFilterContext *ctx = link->dst;
    PanContext *pan = ctx->priv;
    char buf[1024], *cur;
    int i, j, k, r;
    double t;

    pan->nb_input_channels = av_get_channel_layout_nb_channels(link->channel_layout);
    if (pan->need_renumber) {
        // input channels were given by their name: renumber them
        for (i = j = 0; i < MAX_CHANNELS; i++) {
            if ((link->channel_layout >> i) & 1) {
                for (k = 0; k < pan->nb_output_channels; k++)
                    pan->gain.d[k][j] = pan->gain.d[k][i];
                j++;
            }
        }
    }
    // renormalize
    for (i = 0; i < pan->nb_output_channels; i++) {
        if (!((pan->need_renorm >> i) & 1))
            continue;
        t = 0;
        for (j = 0; j < pan->nb_input_channels; j++)
            t += pan->gain.d[i][j];
        if (t > -1E-5 && t < 1E-5) {
            // t is almost 0 but not exactly, this is probably a mistake
            if (t)
                av_log(ctx, AV_LOG_WARNING,
                       "Degenerate coefficients while renormalizing\n");
            continue;
        }
        for (j = 0; j < pan->nb_input_channels; j++)
            pan->gain.d[i][j] /= t;
    }
    // summary
    for (i = 0; i < pan->nb_output_channels; i++) {
        cur = buf;
        for (j = 0; j < pan->nb_input_channels; j++) {
            r = snprintf(cur, buf + sizeof(buf) - cur, "%s%.3g i%d",
                         j ? " + " : "", pan->gain.d[i][j], j);
            cur += FFMIN(buf + sizeof(buf) - cur, r);
        }
        av_log(ctx, AV_LOG_INFO, "o%d = %s\n", i, buf);
    }
    // convert to integer
    for (i = 0; i < pan->nb_output_channels; i++) {
        for (j = 0; j < pan->nb_input_channels; j++) {
            if (pan->gain.d[i][j] < -128 || pan->gain.d[i][j] > 128)
                av_log(ctx, AV_LOG_WARNING,
                    "Gain #%d->#%d too large, clamped\n", j, i);
            pan->gain.i[i][j] = av_clipf(pan->gain.d[i][j], -128, 128) * 256.0;
        }
    }
    return 0;
}


static void filter_samples(AVFilterLink *inlink, AVFilterBufferRef *insamples)
{
    PanContext *const pan = inlink->dst->priv;
    int i, o, n = insamples->audio->nb_samples;

    /* input */
    const int16_t *in     = (int16_t *)insamples->data[0];
    const int16_t *in_end = in + n * pan->nb_input_channels;

    /* output */
    AVFilterLink *const outlink = inlink->dst->outputs[0];
    AVFilterBufferRef *outsamples = avfilter_get_audio_buffer(outlink, AV_PERM_WRITE, n);
    int16_t *out = (int16_t *)outsamples->data[0];

    for (; in < in_end; in += pan->nb_input_channels) {
        for (o = 0; o < pan->nb_output_channels; o++) {
            int v = 0;
            for (i = 0; i < pan->nb_input_channels; i++)
                v += pan->gain.i[o][i] * in[i];
            *(out++) = v >> 8;
        }
    }

    avfilter_filter_samples(outlink, outsamples);
    avfilter_unref_buffer(insamples);
}

AVFilter avfilter_af_pan = {
    .name          = "pan",
    .description   = NULL_IF_CONFIG_SMALL("Remix channels with coefficients (panning)"),
    .priv_size     = sizeof(PanContext),
    .init          = init,
    .query_formats = query_formats,

    .inputs    = (const AVFilterPad[]) {
        { .name             = "default",
          .type             = AVMEDIA_TYPE_AUDIO,
          .config_props     = config_props,
          .filter_samples   = filter_samples,
          .min_perms        = AV_PERM_READ, },
        { .name = NULL}
    },
    .outputs   = (const AVFilterPad[]) {
        { .name             = "default",
          .type             = AVMEDIA_TYPE_AUDIO, },
        { .name = NULL}
    },
};
