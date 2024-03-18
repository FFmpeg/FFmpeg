/*
 * Copyright (c) 2012 Google, Inc.
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
 * audio channel mapping filter
 */

#include <ctype.h>

#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

struct ChannelMap {
    int in_channel;
    int out_channel;
    int in_channel_idx;
    int out_channel_idx;
};

enum MappingMode {
    MAP_NONE,
    MAP_ONE_INT,
    MAP_ONE_STR,
    MAP_PAIR_INT_INT,
    MAP_PAIR_INT_STR,
    MAP_PAIR_STR_INT,
    MAP_PAIR_STR_STR
};

#define MAX_CH 64
typedef struct ChannelMapContext {
    const AVClass *class;
    char *mapping_str;
    AVChannelLayout output_layout;
    struct ChannelMap map[MAX_CH];
    int nch;
    enum MappingMode mode;
} ChannelMapContext;

#define OFFSET(x) offsetof(ChannelMapContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM
static const AVOption channelmap_options[] = {
    { "map", "A comma-separated list of input channel numbers in output order.",
          OFFSET(mapping_str),        AV_OPT_TYPE_STRING, .flags = A|F },
    { "channel_layout", "Output channel layout.",
          OFFSET(output_layout),      AV_OPT_TYPE_CHLAYOUT, .flags = A|F },
    { NULL }
};

AVFILTER_DEFINE_CLASS(channelmap);

static char* split(char *message, char delim) {
    char *next = strchr(message, delim);
    if (next)
      *next++ = '\0';
    return next;
}

static int get_channel_idx(char **map, int *ch, char delim, int max_nb_channels)
{
    char *next;
    int len;
    int n = 0;
    if (!*map)
        return AVERROR(EINVAL);
    next = split(*map, delim);
    if (!next && delim == '-')
        return AVERROR(EINVAL);
    len = strlen(*map);
    sscanf(*map, "%d%n", ch, &n);
    if (n != len)
        return AVERROR(EINVAL);
    if (*ch < 0 || *ch >= max_nb_channels)
        return AVERROR(EINVAL);
    *map = next;
    return 0;
}

static int get_channel(char **map, int *ch, char delim)
{
    char *next = split(*map, delim);
    if (!next && delim == '-')
        return AVERROR(EINVAL);
    *ch = av_channel_from_string(*map);
    if (*ch < 0)
        return AVERROR(EINVAL);
    *map = next;
    return 0;
}

static int check_idx_and_id(AVFilterContext *ctx, int channel_idx, int channel, AVChannelLayout *ch_layout, const char *io)
{
    char channel_name[64];
    char layout_name[256];
    int nb_channels = ch_layout->nb_channels;

    if (channel_idx < 0 || channel_idx >= nb_channels) {
        av_channel_layout_describe(ch_layout, layout_name, sizeof(layout_name));
        if (channel >= 0) {
            av_channel_name(channel_name, sizeof(channel_name), channel);
            av_log(ctx, AV_LOG_ERROR,
                   "%sput channel '%s' not available from %sput layout '%s'\n",
                   io, channel_name, io, layout_name);
        } else {
            av_log(ctx, AV_LOG_ERROR,
                   "%sput channel #%d not available from %sput layout '%s'\n",
                   io, channel_idx, io, layout_name);
        }
        return AVERROR(EINVAL);
    }

    return 0;
}

static av_cold int channelmap_init(AVFilterContext *ctx)
{
    ChannelMapContext *s = ctx->priv;
    char *mapping, separator = '|';
    int map_entries = 0;
    enum MappingMode mode;
    int64_t out_ch_mask = 0;
    uint64_t presence_mask;
    int i;

    mapping = s->mapping_str;

    if (!mapping) {
        mode = MAP_NONE;
    } else {
        char *dash = strchr(mapping, '-');
        if (!dash) {  // short mapping
            if (av_isdigit(*mapping))
                mode = MAP_ONE_INT;
            else
                mode = MAP_ONE_STR;
        } else if (av_isdigit(*mapping)) {
            if (av_isdigit(*(dash+1)))
                mode = MAP_PAIR_INT_INT;
            else
                mode = MAP_PAIR_INT_STR;
        } else {
            if (av_isdigit(*(dash+1)))
                mode = MAP_PAIR_STR_INT;
            else
                mode = MAP_PAIR_STR_STR;
        }
    }

    if (mode != MAP_NONE) {
        char *sep = mapping;
        map_entries = 1;
        while ((sep = strchr(sep, separator))) {
            if (*++sep)  // Allow trailing comma
                map_entries++;
        }
    }

    if (map_entries > MAX_CH) {
        av_log(ctx, AV_LOG_ERROR, "Too many channels mapped: '%d'.\n", map_entries);
        return AVERROR(EINVAL);
    }

    for (i = 0; i < MAX_CH; i++) {
        s->map[i].in_channel_idx  = -1;
        s->map[i].out_channel_idx = -1;
        s->map[i].in_channel      = -1;
        s->map[i].out_channel     = -1;
    }

    for (i = 0; i < map_entries; i++) {
        int in_ch_idx = -1, out_ch_idx = -1;
        int in_ch = -1, out_ch = -1;
        static const char err[] = "Failed to parse channel map\n";
        switch (mode) {
        case MAP_ONE_INT:
            if (get_channel_idx(&mapping, &in_ch_idx, separator, MAX_CH) < 0) {
                av_log(ctx, AV_LOG_ERROR, err);
                return AVERROR(EINVAL);
            }
            s->map[i].in_channel_idx  = in_ch_idx;
            s->map[i].out_channel_idx = i;
            break;
        case MAP_ONE_STR:
            if (get_channel(&mapping, &in_ch, separator) < 0) {
                av_log(ctx, AV_LOG_ERROR, err);
                return AVERROR(EINVAL);
            }
            s->map[i].in_channel      = in_ch;
            s->map[i].out_channel_idx = i;
            break;
        case MAP_PAIR_INT_INT:
            if (get_channel_idx(&mapping, &in_ch_idx, '-', MAX_CH) < 0 ||
                get_channel_idx(&mapping, &out_ch_idx, separator, MAX_CH) < 0) {
                av_log(ctx, AV_LOG_ERROR, err);
                return AVERROR(EINVAL);
            }
            s->map[i].in_channel_idx  = in_ch_idx;
            s->map[i].out_channel_idx = out_ch_idx;
            break;
        case MAP_PAIR_INT_STR:
            if (get_channel_idx(&mapping, &in_ch_idx, '-', MAX_CH) < 0 ||
                get_channel(&mapping, &out_ch, separator) < 0) {
                av_log(ctx, AV_LOG_ERROR, err);
                return AVERROR(EINVAL);
            }
            s->map[i].in_channel_idx  = in_ch_idx;
            s->map[i].out_channel     = out_ch;
            if (out_ch < 63)
                out_ch_mask |= 1ULL << out_ch;
            else
                out_ch_mask = -1;
            break;
        case MAP_PAIR_STR_INT:
            if (get_channel(&mapping, &in_ch, '-') < 0 ||
                get_channel_idx(&mapping, &out_ch_idx, separator, MAX_CH) < 0) {
                av_log(ctx, AV_LOG_ERROR, err);
                return AVERROR(EINVAL);
            }
            s->map[i].in_channel      = in_ch;
            s->map[i].out_channel_idx = out_ch_idx;
            break;
        case MAP_PAIR_STR_STR:
            if (get_channel(&mapping, &in_ch, '-') < 0 ||
                get_channel(&mapping, &out_ch, separator) < 0) {
                av_log(ctx, AV_LOG_ERROR, err);
                return AVERROR(EINVAL);
            }
            s->map[i].in_channel = in_ch;
            s->map[i].out_channel = out_ch;
            if (out_ch < 63)
                out_ch_mask |= 1ULL << out_ch;
            else
                out_ch_mask = -1;
            break;
        }
    }
    s->mode          = mode;
    s->nch           = map_entries;
    if (s->output_layout.nb_channels == 0) {
        if (out_ch_mask > 0)
            av_channel_layout_from_mask(&s->output_layout, out_ch_mask);
        else if (map_entries)
            av_channel_layout_default(&s->output_layout, map_entries);
    }

    if (mode == MAP_NONE) {
        int i;
        s->nch = s->output_layout.nb_channels;
        for (i = 0; i < s->nch; i++) {
            s->map[i].in_channel_idx  = i;
            s->map[i].out_channel_idx = i;
        }
    } else if (s->nch != s->output_layout.nb_channels) {
        char buf[256];
        av_channel_layout_describe(&s->output_layout, buf, sizeof(buf));
        av_log(ctx, AV_LOG_ERROR,
               "Output channel layout %s does not match the number of channels mapped %d.\n",
               buf, s->nch);
        return AVERROR(EINVAL);
    }

    if (!s->output_layout.nb_channels) {
        av_log(ctx, AV_LOG_ERROR, "Output channel layout is not set and "
               "cannot be guessed from the maps.\n");
        return AVERROR(EINVAL);
    }

    if (mode == MAP_PAIR_INT_STR || mode == MAP_PAIR_STR_STR) {
        for (i = 0; i < s->nch; i++) {
            s->map[i].out_channel_idx = av_channel_layout_index_from_channel(
                &s->output_layout, s->map[i].out_channel);
        }
    }

    presence_mask = 0;
    for (i = 0; i < s->nch; i++) {
        uint64_t idx_mask;
        int ret = check_idx_and_id(ctx, s->map[i].out_channel_idx, s->map[i].out_channel, &s->output_layout, "out");
        if (ret < 0)
            return ret;
        idx_mask = (1ULL << s->map[i].out_channel_idx);
        if (presence_mask & idx_mask) {
            char layout_name[256];
            av_channel_layout_describe(&s->output_layout, layout_name, sizeof(layout_name));
            av_log(ctx, AV_LOG_ERROR, "Mapping %d assigns channel #%d twice in output layout '%s'.\n",
                   i + 1, s->map[i].out_channel_idx, layout_name);
            return AVERROR(EINVAL);
        }
        presence_mask |= idx_mask;
    }

    return 0;
}

static int channelmap_query_formats(AVFilterContext *ctx)
{
    ChannelMapContext *s = ctx->priv;
    AVFilterChannelLayouts *channel_layouts = NULL;
    int ret;

    if ((ret = ff_set_common_formats    (ctx,  ff_planar_sample_fmts()))  < 0 ||
        (ret = ff_set_common_all_samplerates(ctx                              )) < 0 ||
        (ret = ff_add_channel_layout(&channel_layouts, &s->output_layout)) < 0 ||
        (ret = ff_channel_layouts_ref(channel_layouts,
                                      &ctx->outputs[0]->incfg.channel_layouts)) < 0)
        return ret;

    return ff_channel_layouts_ref(ff_all_channel_counts(),
                                  &ctx->inputs[0]->outcfg.channel_layouts);
}

static int channelmap_filter_frame(AVFilterLink *inlink, AVFrame *buf)
{
    AVFilterContext  *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    const ChannelMapContext *s = ctx->priv;
    const int nch_in = inlink->ch_layout.nb_channels;
    const int nch_out = s->nch;
    int ch, ret;
    uint8_t *source_planes[MAX_CH];

    memcpy(source_planes, buf->extended_data,
           nch_in * sizeof(source_planes[0]));

    if (nch_out > nch_in) {
        if (nch_out > FF_ARRAY_ELEMS(buf->data)) {
            uint8_t **new_extended_data =
                av_calloc(nch_out, sizeof(*buf->extended_data));
            if (!new_extended_data) {
                av_frame_free(&buf);
                return AVERROR(ENOMEM);
            }
            if (buf->extended_data == buf->data) {
                buf->extended_data = new_extended_data;
            } else {
                av_free(buf->extended_data);
                buf->extended_data = new_extended_data;
            }
        } else if (buf->extended_data != buf->data) {
            av_free(buf->extended_data);
            buf->extended_data = buf->data;
        }
    }

    for (ch = 0; ch < nch_out; ch++) {
        buf->extended_data[s->map[ch].out_channel_idx] =
            source_planes[s->map[ch].in_channel_idx];
    }

    if (buf->data != buf->extended_data)
        memcpy(buf->data, buf->extended_data,
           FFMIN(FF_ARRAY_ELEMS(buf->data), nch_out) * sizeof(buf->data[0]));

    if ((ret = av_channel_layout_copy(&buf->ch_layout, &outlink->ch_layout)) < 0)
        return ret;

    return ff_filter_frame(outlink, buf);
}

static int channelmap_config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ChannelMapContext *s = ctx->priv;
    int i, err = 0;

    for (i = 0; i < s->nch; i++) {
        struct ChannelMap *m = &s->map[i];

        if (s->mode == MAP_PAIR_STR_INT || s->mode == MAP_PAIR_STR_STR || s->mode == MAP_ONE_STR) {
            m->in_channel_idx = av_channel_layout_index_from_channel(
                &inlink->ch_layout, m->in_channel);
        }

        if (check_idx_and_id(ctx, m->in_channel_idx, m->in_channel, &inlink->ch_layout, "in") < 0)
            err = AVERROR(EINVAL);
    }

    return err;
}

static const AVFilterPad avfilter_af_channelmap_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_AUDIO,
        .flags          = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .filter_frame   = channelmap_filter_frame,
        .config_props   = channelmap_config_input,
    },
};

const AVFilter ff_af_channelmap = {
    .name          = "channelmap",
    .description   = NULL_IF_CONFIG_SMALL("Remap audio channels."),
    .init          = channelmap_init,
    .priv_size     = sizeof(ChannelMapContext),
    .priv_class    = &channelmap_class,
    FILTER_INPUTS(avfilter_af_channelmap_inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_QUERY_FUNC(channelmap_query_formats),
};
