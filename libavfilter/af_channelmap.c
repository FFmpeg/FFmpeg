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

#include "libavutil/audioconvert.h"
#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

struct ChannelMap {
    uint64_t in_channel;
    uint64_t out_channel;
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
    AVFilterChannelLayouts *channel_layouts;
    char *mapping_str;
    char *channel_layout_str;
    uint64_t output_layout;
    struct ChannelMap map[MAX_CH];
    int nch;
    enum MappingMode mode;
} ChannelMapContext;

#define OFFSET(x) offsetof(ChannelMapContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM
static const AVOption options[] = {
    { "map", "A comma-separated list of input channel numbers in output order.",
          OFFSET(mapping_str),        AV_OPT_TYPE_STRING, .flags = A|F },
    { "channel_layout", "Output channel layout.",
          OFFSET(channel_layout_str), AV_OPT_TYPE_STRING, .flags = A|F },
    { NULL },
};

static const AVClass channelmap_class = {
    .class_name = "channel map filter",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static char* split(char *message, char delim) {
    char *next = strchr(message, delim);
    if (next)
      *next++ = '\0';
    return next;
}

static int get_channel_idx(char **map, int *ch, char delim, int max_ch)
{
    char *next = split(*map, delim);
    int len;
    int n = 0;
    if (!next && delim == '-')
        return AVERROR(EINVAL);
    len = strlen(*map);
    sscanf(*map, "%d%n", ch, &n);
    if (n != len)
        return AVERROR(EINVAL);
    if (*ch < 0 || *ch > max_ch)
        return AVERROR(EINVAL);
    *map = next;
    return 0;
}

static int get_channel(char **map, uint64_t *ch, char delim)
{
    char *next = split(*map, delim);
    if (!next && delim == '-')
        return AVERROR(EINVAL);
    *ch = av_get_channel_layout(*map);
    if (av_get_channel_layout_nb_channels(*ch) != 1)
        return AVERROR(EINVAL);
    *map = next;
    return 0;
}

static av_cold int channelmap_init(AVFilterContext *ctx, const char *args)
{
    ChannelMapContext *s = ctx->priv;
    int ret;
    char *mapping;
    enum mode;
    int map_entries = 0;
    char buf[256];
    enum MappingMode mode;
    uint64_t out_ch_mask = 0;
    int i;

    if (!args) {
        av_log(ctx, AV_LOG_ERROR, "No parameters supplied.\n");
        return AVERROR(EINVAL);
    }

    s->class = &channelmap_class;
    av_opt_set_defaults(s);

    if ((ret = av_set_options_string(s, args, "=", ":")) < 0)
        return ret;

    mapping = s->mapping_str;

    if (!mapping) {
        mode = MAP_NONE;
    } else {
        char *dash = strchr(mapping, '-');
        if (!dash) {  // short mapping
            if (isdigit(*mapping))
                mode = MAP_ONE_INT;
            else
                mode = MAP_ONE_STR;
        } else if (isdigit(*mapping)) {
            if (isdigit(*(dash+1)))
                mode = MAP_PAIR_INT_INT;
            else
                mode = MAP_PAIR_INT_STR;
        } else {
            if (isdigit(*(dash+1)))
                mode = MAP_PAIR_STR_INT;
            else
                mode = MAP_PAIR_STR_STR;
        }
    }

    if (mode != MAP_NONE) {
        char *comma = mapping;
        map_entries = 1;
        while ((comma = strchr(comma, ','))) {
            if (*++comma)  // Allow trailing comma
                map_entries++;
        }
    }

    if (map_entries > MAX_CH) {
        av_log(ctx, AV_LOG_ERROR, "Too many channels mapped: '%d'.\n", map_entries);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    for (i = 0; i < map_entries; i++) {
        int in_ch_idx = -1, out_ch_idx = -1;
        uint64_t in_ch = 0, out_ch = 0;
        static const char err[] = "Failed to parse channel map\n";
        switch (mode) {
        case MAP_ONE_INT:
            if (get_channel_idx(&mapping, &in_ch_idx, ',', MAX_CH) < 0) {
                ret = AVERROR(EINVAL);
                av_log(ctx, AV_LOG_ERROR, err);
                goto fail;
            }
            s->map[i].in_channel_idx  = in_ch_idx;
            s->map[i].out_channel_idx = i;
            break;
        case MAP_ONE_STR:
            if (!get_channel(&mapping, &in_ch, ',')) {
                av_log(ctx, AV_LOG_ERROR, err);
                ret = AVERROR(EINVAL);
                goto fail;
            }
            s->map[i].in_channel      = in_ch;
            s->map[i].out_channel_idx = i;
            break;
        case MAP_PAIR_INT_INT:
            if (get_channel_idx(&mapping, &in_ch_idx, '-', MAX_CH) < 0 ||
                get_channel_idx(&mapping, &out_ch_idx, ',', MAX_CH) < 0) {
                av_log(ctx, AV_LOG_ERROR, err);
                ret = AVERROR(EINVAL);
                goto fail;
            }
            s->map[i].in_channel_idx  = in_ch_idx;
            s->map[i].out_channel_idx = out_ch_idx;
            break;
        case MAP_PAIR_INT_STR:
            if (get_channel_idx(&mapping, &in_ch_idx, '-', MAX_CH) < 0 ||
                get_channel(&mapping, &out_ch, ',') < 0 ||
                out_ch & out_ch_mask) {
                av_log(ctx, AV_LOG_ERROR, err);
                ret = AVERROR(EINVAL);
                goto fail;
            }
            s->map[i].in_channel_idx  = in_ch_idx;
            s->map[i].out_channel     = out_ch;
            out_ch_mask |= out_ch;
            break;
        case MAP_PAIR_STR_INT:
            if (get_channel(&mapping, &in_ch, '-') < 0 ||
                get_channel_idx(&mapping, &out_ch_idx, ',', MAX_CH) < 0) {
                av_log(ctx, AV_LOG_ERROR, err);
                ret = AVERROR(EINVAL);
                goto fail;
            }
            s->map[i].in_channel      = in_ch;
            s->map[i].out_channel_idx = out_ch_idx;
            break;
        case MAP_PAIR_STR_STR:
            if (get_channel(&mapping, &in_ch, '-') < 0 ||
                get_channel(&mapping, &out_ch, ',') < 0 ||
                out_ch & out_ch_mask) {
                av_log(ctx, AV_LOG_ERROR, err);
                ret = AVERROR(EINVAL);
                goto fail;
            }
            s->map[i].in_channel = in_ch;
            s->map[i].out_channel = out_ch;
            out_ch_mask |= out_ch;
            break;
        }
    }
    s->mode          = mode;
    s->nch           = map_entries;
    s->output_layout = out_ch_mask ? out_ch_mask :
                       av_get_default_channel_layout(map_entries);

    if (s->channel_layout_str) {
        uint64_t fmt;
        if ((fmt = av_get_channel_layout(s->channel_layout_str)) == 0) {
            av_log(ctx, AV_LOG_ERROR, "Error parsing channel layout: '%s'.\n",
                   s->channel_layout_str);
            ret = AVERROR(EINVAL);
            goto fail;
        }
        if (mode == MAP_NONE) {
            int i;
            s->nch = av_get_channel_layout_nb_channels(fmt);
            for (i = 0; i < s->nch; i++) {
                s->map[i].in_channel_idx  = i;
                s->map[i].out_channel_idx = i;
            }
        } else if (out_ch_mask && out_ch_mask != fmt) {
            av_get_channel_layout_string(buf, sizeof(buf), 0, out_ch_mask);
            av_log(ctx, AV_LOG_ERROR,
                   "Output channel layout '%s' does not match the list of channel mapped: '%s'.\n",
                   s->channel_layout_str, buf);
            ret = AVERROR(EINVAL);
            goto fail;
        } else if (s->nch != av_get_channel_layout_nb_channels(fmt)) {
            av_log(ctx, AV_LOG_ERROR,
                   "Output channel layout %s does not match the number of channels mapped %d.\n",
                   s->channel_layout_str, s->nch);
            ret = AVERROR(EINVAL);
            goto fail;
        }
        s->output_layout = fmt;
    }
    ff_add_channel_layout(&s->channel_layouts, s->output_layout);

    if (mode == MAP_PAIR_INT_STR || mode == MAP_PAIR_STR_STR) {
        for (i = 0; i < s->nch; i++) {
            s->map[i].out_channel_idx = av_get_channel_layout_channel_index(
                s->output_layout, s->map[i].out_channel);
        }
    }

fail:
    av_opt_free(s);
    return ret;
}

static int channelmap_query_formats(AVFilterContext *ctx)
{
    ChannelMapContext *s = ctx->priv;

    ff_set_common_formats(ctx, ff_planar_sample_fmts());
    ff_set_common_samplerates(ctx, ff_all_samplerates());
    ff_channel_layouts_ref(ff_all_channel_layouts(), &ctx->inputs[0]->out_channel_layouts);
    ff_channel_layouts_ref(s->channel_layouts,       &ctx->outputs[0]->in_channel_layouts);

    return 0;
}

static int channelmap_filter_samples(AVFilterLink *inlink, AVFilterBufferRef *buf)
{
    AVFilterContext  *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    const ChannelMapContext *s = ctx->priv;
    const int nch_in = av_get_channel_layout_nb_channels(inlink->channel_layout);
    const int nch_out = s->nch;
    int ch;
    uint8_t *source_planes[MAX_CH];

    memcpy(source_planes, buf->extended_data,
           nch_in * sizeof(source_planes[0]));

    if (nch_out > nch_in) {
        if (nch_out > FF_ARRAY_ELEMS(buf->data)) {
            uint8_t **new_extended_data =
                av_mallocz(nch_out * sizeof(*buf->extended_data));
            if (!new_extended_data) {
                avfilter_unref_buffer(buf);
                return AVERROR(ENOMEM);
            }
            if (buf->extended_data == buf->data) {
                buf->extended_data = new_extended_data;
            } else {
                buf->extended_data = new_extended_data;
                av_free(buf->extended_data);
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

    return ff_filter_samples(outlink, buf);
}

static int channelmap_config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ChannelMapContext *s = ctx->priv;
    int i, err = 0;
    const char *channel_name;
    char layout_name[256];

    if (s->mode == MAP_PAIR_STR_INT || s->mode == MAP_PAIR_STR_STR) {
        for (i = 0; i < s->nch; i++) {
            s->map[i].in_channel_idx = av_get_channel_layout_channel_index(
                inlink->channel_layout, s->map[i].in_channel);
            if (s->map[i].in_channel_idx < 0) {
                channel_name = av_get_channel_name(s->map[i].in_channel);
                av_get_channel_layout_string(layout_name, sizeof(layout_name),
                                             0, inlink->channel_layout);
                av_log(ctx, AV_LOG_ERROR,
                       "input channel '%s' not available from input layout '%s'\n",
                       channel_name, layout_name);
                err = AVERROR(EINVAL);
            }
        }
    }

    return err;
}

AVFilter avfilter_af_channelmap = {
    .name          = "channelmap",
    .description   = NULL_IF_CONFIG_SMALL("Remap audio channels."),
    .init          = channelmap_init,
    .query_formats = channelmap_query_formats,
    .priv_size     = sizeof(ChannelMapContext),

    .inputs        = (const AVFilterPad[]) {{ .name            = "default",
                                              .type            = AVMEDIA_TYPE_AUDIO,
                                              .min_perms       = AV_PERM_READ | AV_PERM_WRITE,
                                              .filter_samples  = channelmap_filter_samples,
                                              .config_props    = channelmap_config_input },
                                            { .name = NULL }},
    .outputs       = (const AVFilterPad[]) {{ .name            = "default",
                                              .type            = AVMEDIA_TYPE_AUDIO },
                                            { .name = NULL }},
    .priv_class = &channelmap_class,
};
