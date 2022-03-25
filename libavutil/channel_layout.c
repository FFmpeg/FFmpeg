/*
 * Copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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
 * audio channel layout utility functions
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "avassert.h"
#include "channel_layout.h"
#include "bprint.h"
#include "common.h"
#include "error.h"
#include "macros.h"
#include "opt.h"

#define CHAN_IS_AMBI(x) ((x) >= AV_CHAN_AMBISONIC_BASE &&\
                         (x) <= AV_CHAN_AMBISONIC_END)

struct channel_name {
    const char *name;
    const char *description;
};

static const struct channel_name channel_names[] = {
    [AV_CHAN_FRONT_LEFT           ] = { "FL",        "front left"            },
    [AV_CHAN_FRONT_RIGHT          ] = { "FR",        "front right"           },
    [AV_CHAN_FRONT_CENTER         ] = { "FC",        "front center"          },
    [AV_CHAN_LOW_FREQUENCY        ] = { "LFE",       "low frequency"         },
    [AV_CHAN_BACK_LEFT            ] = { "BL",        "back left"             },
    [AV_CHAN_BACK_RIGHT           ] = { "BR",        "back right"            },
    [AV_CHAN_FRONT_LEFT_OF_CENTER ] = { "FLC",       "front left-of-center"  },
    [AV_CHAN_FRONT_RIGHT_OF_CENTER] = { "FRC",       "front right-of-center" },
    [AV_CHAN_BACK_CENTER          ] = { "BC",        "back center"           },
    [AV_CHAN_SIDE_LEFT            ] = { "SL",        "side left"             },
    [AV_CHAN_SIDE_RIGHT           ] = { "SR",        "side right"            },
    [AV_CHAN_TOP_CENTER           ] = { "TC",        "top center"            },
    [AV_CHAN_TOP_FRONT_LEFT       ] = { "TFL",       "top front left"        },
    [AV_CHAN_TOP_FRONT_CENTER     ] = { "TFC",       "top front center"      },
    [AV_CHAN_TOP_FRONT_RIGHT      ] = { "TFR",       "top front right"       },
    [AV_CHAN_TOP_BACK_LEFT        ] = { "TBL",       "top back left"         },
    [AV_CHAN_TOP_BACK_CENTER      ] = { "TBC",       "top back center"       },
    [AV_CHAN_TOP_BACK_RIGHT       ] = { "TBR",       "top back right"        },
    [AV_CHAN_STEREO_LEFT          ] = { "DL",        "downmix left"          },
    [AV_CHAN_STEREO_RIGHT         ] = { "DR",        "downmix right"         },
    [AV_CHAN_WIDE_LEFT            ] = { "WL",        "wide left"             },
    [AV_CHAN_WIDE_RIGHT           ] = { "WR",        "wide right"            },
    [AV_CHAN_SURROUND_DIRECT_LEFT ] = { "SDL",       "surround direct left"  },
    [AV_CHAN_SURROUND_DIRECT_RIGHT] = { "SDR",       "surround direct right" },
    [AV_CHAN_LOW_FREQUENCY_2      ] = { "LFE2",      "low frequency 2"       },
    [AV_CHAN_TOP_SIDE_LEFT        ] = { "TSL",       "top side left"         },
    [AV_CHAN_TOP_SIDE_RIGHT       ] = { "TSR",       "top side right"        },
    [AV_CHAN_BOTTOM_FRONT_CENTER  ] = { "BFC",       "bottom front center"   },
    [AV_CHAN_BOTTOM_FRONT_LEFT    ] = { "BFL",       "bottom front left"     },
    [AV_CHAN_BOTTOM_FRONT_RIGHT   ] = { "BFR",       "bottom front right"    },
};

static const char *get_channel_name(enum AVChannel channel_id)
{
    if ((unsigned) channel_id >= FF_ARRAY_ELEMS(channel_names) ||
        !channel_names[channel_id].name)
        return NULL;
    return channel_names[channel_id].name;
}

void av_channel_name_bprint(AVBPrint *bp, enum AVChannel channel_id)
{
    if (channel_id >= AV_CHAN_AMBISONIC_BASE &&
        channel_id <= AV_CHAN_AMBISONIC_END)
        av_bprintf(bp, "AMBI%d", channel_id - AV_CHAN_AMBISONIC_BASE);
    else if ((unsigned)channel_id < FF_ARRAY_ELEMS(channel_names) &&
             channel_names[channel_id].name)
        av_bprintf(bp, "%s", channel_names[channel_id].name);
    else if (channel_id == AV_CHAN_NONE)
        av_bprintf(bp, "NONE");
    else
        av_bprintf(bp, "USR%d", channel_id);
}

int av_channel_name(char *buf, size_t buf_size, enum AVChannel channel_id)
{
    AVBPrint bp;

    if (!buf && buf_size)
        return AVERROR(EINVAL);

    av_bprint_init_for_buffer(&bp, buf, buf_size);
    av_channel_name_bprint(&bp, channel_id);

    return bp.len;
}

void av_channel_description_bprint(AVBPrint *bp, enum AVChannel channel_id)
{
    if (channel_id >= AV_CHAN_AMBISONIC_BASE &&
        channel_id <= AV_CHAN_AMBISONIC_END)
        av_bprintf(bp, "ambisonic ACN %d", channel_id - AV_CHAN_AMBISONIC_BASE);
    else if ((unsigned)channel_id < FF_ARRAY_ELEMS(channel_names) &&
             channel_names[channel_id].description)
        av_bprintf(bp, "%s", channel_names[channel_id].description);
    else if (channel_id == AV_CHAN_NONE)
        av_bprintf(bp, "none");
    else
        av_bprintf(bp, "user %d", channel_id);
}

int av_channel_description(char *buf, size_t buf_size, enum AVChannel channel_id)
{
    AVBPrint bp;

    if (!buf && buf_size)
        return AVERROR(EINVAL);

    av_bprint_init_for_buffer(&bp, buf, buf_size);
    av_channel_description_bprint(&bp, channel_id);

    return bp.len;
}

enum AVChannel av_channel_from_string(const char *str)
{
    int i;
    char *endptr = (char *)str;
    enum AVChannel id = AV_CHAN_NONE;

    if (!strncmp(str, "AMBI", 4)) {
        i = strtol(str + 4, NULL, 0);
        if (i < 0 || i > AV_CHAN_AMBISONIC_END - AV_CHAN_AMBISONIC_BASE)
            return AV_CHAN_NONE;
        return AV_CHAN_AMBISONIC_BASE + i;
    }

    for (i = 0; i < FF_ARRAY_ELEMS(channel_names); i++) {
        if (channel_names[i].name && !strcmp(str, channel_names[i].name))
            return i;
    }
    if (!strncmp(str, "USR", 3)) {
        const char *p = str + 3;
        id = strtol(p, &endptr, 0);
    }
    if (id >= 0 && !*endptr)
        return id;

    return AV_CHAN_NONE;
}

struct channel_layout_name {
    const char *name;
    AVChannelLayout layout;
};

static const struct channel_layout_name channel_layout_map[] = {
    { "mono",           AV_CHANNEL_LAYOUT_MONO                },
    { "stereo",         AV_CHANNEL_LAYOUT_STEREO              },
    { "2.1",            AV_CHANNEL_LAYOUT_2POINT1             },
    { "3.0",            AV_CHANNEL_LAYOUT_SURROUND            },
    { "3.0(back)",      AV_CHANNEL_LAYOUT_2_1                 },
    { "4.0",            AV_CHANNEL_LAYOUT_4POINT0             },
    { "quad",           AV_CHANNEL_LAYOUT_QUAD                },
    { "quad(side)",     AV_CHANNEL_LAYOUT_2_2                 },
    { "3.1",            AV_CHANNEL_LAYOUT_3POINT1             },
    { "5.0",            AV_CHANNEL_LAYOUT_5POINT0_BACK        },
    { "5.0(side)",      AV_CHANNEL_LAYOUT_5POINT0             },
    { "4.1",            AV_CHANNEL_LAYOUT_4POINT1             },
    { "5.1",            AV_CHANNEL_LAYOUT_5POINT1_BACK        },
    { "5.1(side)",      AV_CHANNEL_LAYOUT_5POINT1             },
    { "6.0",            AV_CHANNEL_LAYOUT_6POINT0             },
    { "6.0(front)",     AV_CHANNEL_LAYOUT_6POINT0_FRONT       },
    { "hexagonal",      AV_CHANNEL_LAYOUT_HEXAGONAL           },
    { "6.1",            AV_CHANNEL_LAYOUT_6POINT1             },
    { "6.1(back)",      AV_CHANNEL_LAYOUT_6POINT1_BACK        },
    { "6.1(front)",     AV_CHANNEL_LAYOUT_6POINT1_FRONT       },
    { "7.0",            AV_CHANNEL_LAYOUT_7POINT0             },
    { "7.0(front)",     AV_CHANNEL_LAYOUT_7POINT0_FRONT       },
    { "7.1",            AV_CHANNEL_LAYOUT_7POINT1             },
    { "7.1(wide)",      AV_CHANNEL_LAYOUT_7POINT1_WIDE_BACK   },
    { "7.1(wide-side)", AV_CHANNEL_LAYOUT_7POINT1_WIDE        },
    { "octagonal",      AV_CHANNEL_LAYOUT_OCTAGONAL           },
    { "hexadecagonal",  AV_CHANNEL_LAYOUT_HEXADECAGONAL       },
    { "downmix",        AV_CHANNEL_LAYOUT_STEREO_DOWNMIX,     },
    { "22.2",           AV_CHANNEL_LAYOUT_22POINT2,           },
};

#if FF_API_OLD_CHANNEL_LAYOUT
FF_DISABLE_DEPRECATION_WARNINGS
static uint64_t get_channel_layout_single(const char *name, int name_len)
{
    int i;
    char *end;
    int64_t layout;

    for (i = 0; i < FF_ARRAY_ELEMS(channel_layout_map); i++) {
        if (strlen(channel_layout_map[i].name) == name_len &&
            !memcmp(channel_layout_map[i].name, name, name_len))
            return channel_layout_map[i].layout.u.mask;
    }
    for (i = 0; i < FF_ARRAY_ELEMS(channel_names); i++)
        if (channel_names[i].name &&
            strlen(channel_names[i].name) == name_len &&
            !memcmp(channel_names[i].name, name, name_len))
            return (int64_t)1 << i;

    errno = 0;
    i = strtol(name, &end, 10);

    if (!errno && (end + 1 - name == name_len && *end  == 'c'))
        return av_get_default_channel_layout(i);

    errno = 0;
    layout = strtoll(name, &end, 0);
    if (!errno && end - name == name_len)
        return FFMAX(layout, 0);
    return 0;
}

uint64_t av_get_channel_layout(const char *name)
{
    const char *n, *e;
    const char *name_end = name + strlen(name);
    int64_t layout = 0, layout_single;

    for (n = name; n < name_end; n = e + 1) {
        for (e = n; e < name_end && *e != '+' && *e != '|'; e++);
        layout_single = get_channel_layout_single(n, e - n);
        if (!layout_single)
            return 0;
        layout |= layout_single;
    }
    return layout;
}

int av_get_extended_channel_layout(const char *name, uint64_t* channel_layout, int* nb_channels)
{
    int nb = 0;
    char *end;
    uint64_t layout = av_get_channel_layout(name);

    if (layout) {
        *channel_layout = layout;
        *nb_channels = av_get_channel_layout_nb_channels(layout);
        return 0;
    }

    nb = strtol(name, &end, 10);
    if (!errno && *end  == 'C' && *(end + 1) == '\0' && nb > 0 && nb < 64) {
        *channel_layout = 0;
        *nb_channels = nb;
        return 0;
    }

    return AVERROR(EINVAL);
}

void av_bprint_channel_layout(struct AVBPrint *bp,
                              int nb_channels, uint64_t channel_layout)
{
    int i;

    if (nb_channels <= 0)
        nb_channels = av_get_channel_layout_nb_channels(channel_layout);

    for (i = 0; i < FF_ARRAY_ELEMS(channel_layout_map); i++)
        if (nb_channels    == channel_layout_map[i].layout.nb_channels &&
            channel_layout == channel_layout_map[i].layout.u.mask) {
            av_bprintf(bp, "%s", channel_layout_map[i].name);
            return;
        }

    av_bprintf(bp, "%d channels", nb_channels);
    if (channel_layout) {
        int i, ch;
        av_bprintf(bp, " (");
        for (i = 0, ch = 0; i < 64; i++) {
            if ((channel_layout & (UINT64_C(1) << i))) {
                const char *name = get_channel_name(i);
                if (name) {
                    if (ch > 0)
                        av_bprintf(bp, "+");
                    av_bprintf(bp, "%s", name);
                }
                ch++;
            }
        }
        av_bprintf(bp, ")");
    }
}

void av_get_channel_layout_string(char *buf, int buf_size,
                                  int nb_channels, uint64_t channel_layout)
{
    AVBPrint bp;

    av_bprint_init_for_buffer(&bp, buf, buf_size);
    av_bprint_channel_layout(&bp, nb_channels, channel_layout);
}

int av_get_channel_layout_nb_channels(uint64_t channel_layout)
{
    return av_popcount64(channel_layout);
}

int64_t av_get_default_channel_layout(int nb_channels) {
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(channel_layout_map); i++)
        if (nb_channels == channel_layout_map[i].layout.nb_channels)
            return channel_layout_map[i].layout.u.mask;
    return 0;
}

int av_get_channel_layout_channel_index(uint64_t channel_layout,
                                        uint64_t channel)
{
    if (!(channel_layout & channel) ||
        av_get_channel_layout_nb_channels(channel) != 1)
        return AVERROR(EINVAL);
    channel_layout &= channel - 1;
    return av_get_channel_layout_nb_channels(channel_layout);
}

const char *av_get_channel_name(uint64_t channel)
{
    int i;
    if (av_get_channel_layout_nb_channels(channel) != 1)
        return NULL;
    for (i = 0; i < 64; i++)
        if ((1ULL<<i) & channel)
            return get_channel_name(i);
    return NULL;
}

const char *av_get_channel_description(uint64_t channel)
{
    int i;
    if (av_get_channel_layout_nb_channels(channel) != 1)
        return NULL;
    for (i = 0; i < FF_ARRAY_ELEMS(channel_names); i++)
        if ((1ULL<<i) & channel)
            return channel_names[i].description;
    return NULL;
}

uint64_t av_channel_layout_extract_channel(uint64_t channel_layout, int index)
{
    int i;

    if (av_get_channel_layout_nb_channels(channel_layout) <= index)
        return 0;

    for (i = 0; i < 64; i++) {
        if ((1ULL << i) & channel_layout && !index--)
            return 1ULL << i;
    }
    return 0;
}

int av_get_standard_channel_layout(unsigned index, uint64_t *layout,
                                   const char **name)
{
    if (index >= FF_ARRAY_ELEMS(channel_layout_map))
        return AVERROR_EOF;
    if (layout) *layout = channel_layout_map[index].layout.u.mask;
    if (name)   *name   = channel_layout_map[index].name;
    return 0;
}
FF_ENABLE_DEPRECATION_WARNINGS
#endif

int av_channel_layout_from_mask(AVChannelLayout *channel_layout,
                                uint64_t mask)
{
    if (!mask)
        return AVERROR(EINVAL);

    channel_layout->order       = AV_CHANNEL_ORDER_NATIVE;
    channel_layout->nb_channels = av_popcount64(mask);
    channel_layout->u.mask      = mask;

    return 0;
}

int av_channel_layout_from_string(AVChannelLayout *channel_layout,
                                  const char *str)
{
    int i;
    int channels = 0, nb_channels = 0, native = 1;
    enum AVChannel highest_channel = AV_CHAN_NONE;
    const char *dup;
    char *chlist, *end;
    uint64_t mask = 0;

    /* channel layout names */
    for (i = 0; i < FF_ARRAY_ELEMS(channel_layout_map); i++) {
        if (channel_layout_map[i].name && !strcmp(str, channel_layout_map[i].name)) {
            *channel_layout = channel_layout_map[i].layout;
            return 0;
        }
    }

    /* ambisonic */
    if (!strncmp(str, "ambisonic ", 10)) {
        const char *p = str + 10;
        char *endptr;
        AVChannelLayout extra = {0};
        int order;

        order = strtol(p, &endptr, 0);
        if (order < 0 || order + 1  > INT_MAX / (order + 1) ||
            (*endptr && *endptr != '+'))
            return AVERROR(EINVAL);

        channel_layout->order       = AV_CHANNEL_ORDER_AMBISONIC;
        channel_layout->nb_channels = (order + 1) * (order + 1);

        if (*endptr) {
            int ret = av_channel_layout_from_string(&extra, endptr + 1);
            if (ret < 0)
                return ret;
            if (extra.nb_channels >= INT_MAX - channel_layout->nb_channels) {
                av_channel_layout_uninit(&extra);
                return AVERROR(EINVAL);
            }

            if (extra.order == AV_CHANNEL_ORDER_NATIVE) {
                channel_layout->u.mask = extra.u.mask;
            } else {
                channel_layout->order = AV_CHANNEL_ORDER_CUSTOM;
                channel_layout->u.map =
                    av_calloc(channel_layout->nb_channels + extra.nb_channels,
                              sizeof(*channel_layout->u.map));
                if (!channel_layout->u.map) {
                    av_channel_layout_uninit(&extra);
                    return AVERROR(ENOMEM);
                }

                for (i = 0; i < channel_layout->nb_channels; i++)
                    channel_layout->u.map[i].id = AV_CHAN_AMBISONIC_BASE + i;
                for (i = 0; i < extra.nb_channels; i++) {
                    enum AVChannel ch = av_channel_layout_channel_from_index(&extra, i);
                    if (CHAN_IS_AMBI(ch)) {
                        av_channel_layout_uninit(&extra);
                        return AVERROR(EINVAL);
                    }
                    channel_layout->u.map[channel_layout->nb_channels + i].id = ch;
                    if (extra.order == AV_CHANNEL_ORDER_CUSTOM &&
                        extra.u.map[i].name[0])
                        av_strlcpy(channel_layout->u.map[channel_layout->nb_channels + i].name,
                                   extra.u.map[i].name,
                                   sizeof(channel_layout->u.map[channel_layout->nb_channels + i].name));
                }
            }
            channel_layout->nb_channels += extra.nb_channels;
            av_channel_layout_uninit(&extra);
        }

        return 0;
    }

    chlist = av_strdup(str);
    if (!chlist)
        return AVERROR(ENOMEM);

    /* channel names */
    av_sscanf(str, "%d channels (%[^)]", &nb_channels, chlist);
    end = strchr(str, ')');

    dup = chlist;
    while (*dup) {
        char *channel, *chname;
        int ret = av_opt_get_key_value(&dup, "@", "+", AV_OPT_FLAG_IMPLICIT_KEY, &channel, &chname);
        if (ret < 0) {
            av_free(chlist);
            return ret;
        }
        if (*dup)
            dup++; // skip separator
        if (channel && !*channel)
            av_freep(&channel);
        for (i = 0; i < FF_ARRAY_ELEMS(channel_names); i++) {
            if (channel_names[i].name && !strcmp(channel ? channel : chname, channel_names[i].name)) {
                if (channel || i < highest_channel || mask & (1ULL << i))
                    native = 0; // Not a native layout, use a custom one
                highest_channel = i;
                mask |= 1ULL << i;
                break;
            }
        }

        if (!channel && i >= FF_ARRAY_ELEMS(channel_names)) {
            char *endptr = chname;
            enum AVChannel id = AV_CHAN_NONE;

            if (!strncmp(chname, "USR", 3)) {
                const char *p = chname + 3;
                id = strtol(p, &endptr, 0);
            }
            if (id < 0 || *endptr) {
                native = 0; // Unknown channel name
                channels = 0;
                mask = 0;
                av_free(chname);
                break;
            }
            if (id > 63)
                native = 0; // Not a native layout, use a custom one
            else {
                if (id < highest_channel || mask & (1ULL << id))
                    native = 0; // Not a native layout, use a custom one
                highest_channel = id;
                mask |= 1ULL << id;
            }
        }
        channels++;
        av_free(channel);
        av_free(chname);
    }

    if (mask && native) {
        av_free(chlist);
        if (nb_channels && ((nb_channels != channels) || (!end || *++end)))
            return AVERROR(EINVAL);
        av_channel_layout_from_mask(channel_layout, mask);
        return 0;
    }

    /* custom layout of channel names */
    if (channels && !native) {
        int idx = 0;

        if (nb_channels && ((nb_channels != channels) || (!end || *++end))) {
            av_free(chlist);
            return AVERROR(EINVAL);
        }

        channel_layout->u.map = av_calloc(channels, sizeof(*channel_layout->u.map));
        if (!channel_layout->u.map) {
            av_free(chlist);
            return AVERROR(ENOMEM);
        }

        channel_layout->order = AV_CHANNEL_ORDER_CUSTOM;
        channel_layout->nb_channels = channels;

        dup = chlist;
        while (*dup) {
            char *channel, *chname;
            int ret = av_opt_get_key_value(&dup, "@", "+", AV_OPT_FLAG_IMPLICIT_KEY, &channel, &chname);
            if (ret < 0) {
                av_freep(&channel_layout->u.map);
                av_free(chlist);
                return ret;
            }
            if (*dup)
                dup++; // skip separator
            for (i = 0; i < FF_ARRAY_ELEMS(channel_names); i++) {
                if (channel_names[i].name && !strcmp(channel ? channel : chname, channel_names[i].name)) {
                    channel_layout->u.map[idx].id = i;
                    if (channel)
                        av_strlcpy(channel_layout->u.map[idx].name, chname, sizeof(channel_layout->u.map[idx].name));
                    idx++;
                    break;
                }
            }
            if (i >= FF_ARRAY_ELEMS(channel_names)) {
                const char *p = (channel ? channel : chname) + 3;
                channel_layout->u.map[idx].id = strtol(p, NULL, 0);
                if (channel)
                    av_strlcpy(channel_layout->u.map[idx].name, chname, sizeof(channel_layout->u.map[idx].name));
                idx++;
            }
            av_free(channel);
            av_free(chname);
        }
        av_free(chlist);

        return 0;
    }
    av_freep(&chlist);

    errno = 0;
    mask = strtoull(str, &end, 0);

    /* channel layout mask */
    if (!errno && !*end && !strchr(str, '-') && mask) {
        av_channel_layout_from_mask(channel_layout, mask);
        return 0;
    }

    errno = 0;
    channels = strtol(str, &end, 10);

    /* number of channels */
    if (!errno && !strcmp(end, "c") && channels > 0) {
        av_channel_layout_default(channel_layout, channels);
        if (channel_layout->order == AV_CHANNEL_ORDER_NATIVE)
            return 0;
    }

    /* number of unordered channels */
    if (!errno && (!strcmp(end, "C") || !strcmp(end, " channels"))
        && channels > 0) {
        channel_layout->order = AV_CHANNEL_ORDER_UNSPEC;
        channel_layout->nb_channels = channels;
        return 0;
    }

    return AVERROR(EINVAL);
}

void av_channel_layout_uninit(AVChannelLayout *channel_layout)
{
    if (channel_layout->order == AV_CHANNEL_ORDER_CUSTOM)
        av_freep(&channel_layout->u.map);
    memset(channel_layout, 0, sizeof(*channel_layout));
}

int av_channel_layout_copy(AVChannelLayout *dst, const AVChannelLayout *src)
{
    av_channel_layout_uninit(dst);
    *dst = *src;
    if (src->order == AV_CHANNEL_ORDER_CUSTOM) {
        dst->u.map = av_malloc_array(src->nb_channels, sizeof(*dst->u.map));
        if (!dst->u.map)
            return AVERROR(ENOMEM);
        memcpy(dst->u.map, src->u.map, src->nb_channels * sizeof(*src->u.map));
    }
    return 0;
}

/**
 * If the layout is n-th order standard-order ambisonic, with optional
 * extra non-diegetic channels at the end, return the order.
 * Return a negative error code otherwise.
 */
static int ambisonic_order(const AVChannelLayout *channel_layout)
{
    int i, highest_ambi, order;

    highest_ambi = -1;
    if (channel_layout->order == AV_CHANNEL_ORDER_AMBISONIC)
        highest_ambi = channel_layout->nb_channels - av_popcount64(channel_layout->u.mask) - 1;
    else {
        const AVChannelCustom *map = channel_layout->u.map;
        av_assert0(channel_layout->order == AV_CHANNEL_ORDER_CUSTOM);

        for (i = 0; i < channel_layout->nb_channels; i++) {
            int is_ambi = CHAN_IS_AMBI(map[i].id);

            /* ambisonic following non-ambisonic */
            if (i > 0 && is_ambi && !CHAN_IS_AMBI(map[i - 1].id))
                return AVERROR(EINVAL);

            /* non-default ordering */
            if (is_ambi && map[i].id - AV_CHAN_AMBISONIC_BASE != i)
                return AVERROR(EINVAL);

            if (CHAN_IS_AMBI(map[i].id))
                highest_ambi = i;
        }
    }
    /* no ambisonic channels*/
    if (highest_ambi < 0)
        return AVERROR(EINVAL);

    order = floor(sqrt(highest_ambi));
    /* incomplete order - some harmonics are missing */
    if ((order + 1) * (order + 1) != highest_ambi + 1)
        return AVERROR(EINVAL);

    return order;
}

/**
 * If the custom layout is n-th order standard-order ambisonic, with optional
 * extra non-diegetic channels at the end, write its string description in bp.
 * Return a negative error code otherwise.
 */
static int try_describe_ambisonic(AVBPrint *bp, const AVChannelLayout *channel_layout)
{
    int nb_ambi_channels;
    int order = ambisonic_order(channel_layout);
    if (order < 0)
        return order;

    av_bprintf(bp, "ambisonic %d", order);

    /* extra channels present */
    nb_ambi_channels = (order + 1) * (order + 1);
    if (nb_ambi_channels < channel_layout->nb_channels) {
        AVChannelLayout extra = { 0 };

        if (channel_layout->order == AV_CHANNEL_ORDER_AMBISONIC) {
            extra.order       = AV_CHANNEL_ORDER_NATIVE;
            extra.nb_channels = av_popcount64(channel_layout->u.mask);
            extra.u.mask      = channel_layout->u.mask;
        } else {
            extra.order       = AV_CHANNEL_ORDER_CUSTOM;
            extra.nb_channels = channel_layout->nb_channels - nb_ambi_channels;
            extra.u.map       = channel_layout->u.map + nb_ambi_channels;
        }

        av_bprint_chars(bp, '+', 1);
        av_channel_layout_describe_bprint(&extra, bp);
        /* Not calling uninit here on extra because we don't own the u.map pointer */
    }

    return 0;
}

int av_channel_layout_describe_bprint(const AVChannelLayout *channel_layout,
                                      AVBPrint *bp)
{
    int i;

    switch (channel_layout->order) {
    case AV_CHANNEL_ORDER_NATIVE:
        for (i = 0; i < FF_ARRAY_ELEMS(channel_layout_map); i++)
            if (channel_layout->u.mask == channel_layout_map[i].layout.u.mask) {
                av_bprintf(bp, "%s", channel_layout_map[i].name);
                return 0;
            }
        // fall-through
    case AV_CHANNEL_ORDER_CUSTOM:
        if (channel_layout->order == AV_CHANNEL_ORDER_CUSTOM) {
            int res = try_describe_ambisonic(bp, channel_layout);
            if (res >= 0)
                return 0;
        }
        if (channel_layout->nb_channels)
            av_bprintf(bp, "%d channels (", channel_layout->nb_channels);
        for (i = 0; i < channel_layout->nb_channels; i++) {
            enum AVChannel ch = av_channel_layout_channel_from_index(channel_layout, i);

            if (i)
                av_bprintf(bp, "+");
            av_channel_name_bprint(bp, ch);
            if (channel_layout->order == AV_CHANNEL_ORDER_CUSTOM &&
                channel_layout->u.map[i].name[0])
                av_bprintf(bp, "@%s", channel_layout->u.map[i].name);
        }
        if (channel_layout->nb_channels) {
            av_bprintf(bp, ")");
            return 0;
        }
        // fall-through
    case AV_CHANNEL_ORDER_UNSPEC:
        av_bprintf(bp, "%d channels", channel_layout->nb_channels);
        return 0;
    case AV_CHANNEL_ORDER_AMBISONIC:
        return try_describe_ambisonic(bp, channel_layout);
    default:
        return AVERROR(EINVAL);
    }
}

int av_channel_layout_describe(const AVChannelLayout *channel_layout,
                               char *buf, size_t buf_size)
{
    AVBPrint bp;
    int ret;

    if (!buf && buf_size)
        return AVERROR(EINVAL);

    av_bprint_init_for_buffer(&bp, buf, buf_size);
    ret = av_channel_layout_describe_bprint(channel_layout, &bp);
    if (ret < 0)
        return ret;

    return bp.len;
}

enum AVChannel
av_channel_layout_channel_from_index(const AVChannelLayout *channel_layout,
                                     unsigned int idx)
{
    int i;

    if (idx >= channel_layout->nb_channels)
        return AV_CHAN_NONE;

    switch (channel_layout->order) {
    case AV_CHANNEL_ORDER_CUSTOM:
        return channel_layout->u.map[idx].id;
    case AV_CHANNEL_ORDER_AMBISONIC: {
        int ambi_channels = channel_layout->nb_channels - av_popcount64(channel_layout->u.mask);
        if (idx < ambi_channels)
            return AV_CHAN_AMBISONIC_BASE + idx;
        idx -= ambi_channels;
        }
    // fall-through
    case AV_CHANNEL_ORDER_NATIVE:
        for (i = 0; i < 64; i++) {
            if ((1ULL << i) & channel_layout->u.mask && !idx--)
                return i;
        }
    default:
        return AV_CHAN_NONE;
    }
}

enum AVChannel
av_channel_layout_channel_from_string(const AVChannelLayout *channel_layout,
                                      const char *str)
{
    int index = av_channel_layout_index_from_string(channel_layout, str);

    if (index < 0)
        return AV_CHAN_NONE;

    return av_channel_layout_channel_from_index(channel_layout, index);
}

int av_channel_layout_index_from_channel(const AVChannelLayout *channel_layout,
                                         enum AVChannel channel)
{
    int i;

    if (channel == AV_CHAN_NONE)
        return AVERROR(EINVAL);

    switch (channel_layout->order) {
    case AV_CHANNEL_ORDER_CUSTOM:
        for (i = 0; i < channel_layout->nb_channels; i++)
            if (channel_layout->u.map[i].id == channel)
                return i;
        return AVERROR(EINVAL);
    case AV_CHANNEL_ORDER_AMBISONIC:
    case AV_CHANNEL_ORDER_NATIVE: {
        uint64_t mask = channel_layout->u.mask;
        int ambi_channels = channel_layout->nb_channels - av_popcount64(mask);
        if (channel_layout->order == AV_CHANNEL_ORDER_AMBISONIC &&
            channel >= AV_CHAN_AMBISONIC_BASE) {
            if (channel - AV_CHAN_AMBISONIC_BASE >= ambi_channels)
                return AVERROR(EINVAL);
            return channel - AV_CHAN_AMBISONIC_BASE;
        }
        if ((unsigned)channel > 63 || !(mask & (1ULL << channel)))
            return AVERROR(EINVAL);
        mask &= (1ULL << channel) - 1;
        return av_popcount64(mask) + ambi_channels;
        }
    default:
        return AVERROR(EINVAL);
    }
}

int av_channel_layout_index_from_string(const AVChannelLayout *channel_layout,
                                        const char *str)
{
    char *chname;
    enum AVChannel ch = AV_CHAN_NONE;

    switch (channel_layout->order) {
    case AV_CHANNEL_ORDER_CUSTOM:
        chname = strstr(str, "@");
        if (chname) {
            char buf[16];
            chname++;
            av_strlcpy(buf, str, FFMIN(sizeof(buf), chname - str));
            if (!*chname)
                chname = NULL;
            ch = av_channel_from_string(buf);
            if (ch == AV_CHAN_NONE && *buf)
                return AVERROR(EINVAL);
        }
        for (int i = 0; chname && i < channel_layout->nb_channels; i++) {
            if (!strcmp(chname, channel_layout->u.map[i].name) &&
                (ch == AV_CHAN_NONE || ch == channel_layout->u.map[i].id))
                return i;
        }
        // fall-through
    case AV_CHANNEL_ORDER_AMBISONIC:
    case AV_CHANNEL_ORDER_NATIVE:
        ch = av_channel_from_string(str);
        if (ch == AV_CHAN_NONE)
            return AVERROR(EINVAL);
        return av_channel_layout_index_from_channel(channel_layout, ch);
    }

    return AVERROR(EINVAL);
}

int av_channel_layout_check(const AVChannelLayout *channel_layout)
{
    if (channel_layout->nb_channels <= 0)
        return 0;

    switch (channel_layout->order) {
    case AV_CHANNEL_ORDER_NATIVE:
        return av_popcount64(channel_layout->u.mask) == channel_layout->nb_channels;
    case AV_CHANNEL_ORDER_CUSTOM:
        if (!channel_layout->u.map)
            return 0;
        for (int i = 0; i < channel_layout->nb_channels; i++) {
            if (channel_layout->u.map[i].id == AV_CHAN_NONE)
                return 0;
        }
        return 1;
    case AV_CHANNEL_ORDER_AMBISONIC:
        /* If non-diegetic channels are present, ensure they are taken into account */
        return av_popcount64(channel_layout->u.mask) < channel_layout->nb_channels;
    case AV_CHANNEL_ORDER_UNSPEC:
        return 1;
    default:
        return 0;
    }
}

int av_channel_layout_compare(const AVChannelLayout *chl, const AVChannelLayout *chl1)
{
    int i;

    /* different channel counts -> not equal */
    if (chl->nb_channels != chl1->nb_channels)
        return 1;

    /* if only one is unspecified -> not equal */
    if ((chl->order  == AV_CHANNEL_ORDER_UNSPEC) !=
        (chl1->order == AV_CHANNEL_ORDER_UNSPEC))
        return 1;
    /* both are unspecified -> equal */
    else if (chl->order == AV_CHANNEL_ORDER_UNSPEC)
        return 0;

    /* can compare masks directly */
    if ((chl->order == AV_CHANNEL_ORDER_NATIVE ||
         chl->order == AV_CHANNEL_ORDER_AMBISONIC) &&
        chl->order == chl1->order)
        return chl->u.mask != chl1->u.mask;

    /* compare channel by channel */
    for (i = 0; i < chl->nb_channels; i++)
        if (av_channel_layout_channel_from_index(chl,  i) !=
            av_channel_layout_channel_from_index(chl1, i))
            return 1;
    return 0;
}

void av_channel_layout_default(AVChannelLayout *ch_layout, int nb_channels)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(channel_layout_map); i++)
        if (nb_channels == channel_layout_map[i].layout.nb_channels) {
            *ch_layout = channel_layout_map[i].layout;
            return;
        }

    ch_layout->order       = AV_CHANNEL_ORDER_UNSPEC;
    ch_layout->nb_channels = nb_channels;
}

const AVChannelLayout *av_channel_layout_standard(void **opaque)
{
    uintptr_t i = (uintptr_t)*opaque;
    const AVChannelLayout *ch_layout = NULL;

    if (i < FF_ARRAY_ELEMS(channel_layout_map)) {
        ch_layout = &channel_layout_map[i].layout;
        *opaque = (void*)(i + 1);
    }

    return ch_layout;
}

uint64_t av_channel_layout_subset(const AVChannelLayout *channel_layout,
                                  uint64_t mask)
{
    uint64_t ret = 0;
    int i;

    switch (channel_layout->order) {
    case AV_CHANNEL_ORDER_NATIVE:
    case AV_CHANNEL_ORDER_AMBISONIC:
        return channel_layout->u.mask & mask;
    case AV_CHANNEL_ORDER_CUSTOM:
        for (i = 0; i < 64; i++)
            if (mask & (1ULL << i) && av_channel_layout_index_from_channel(channel_layout, i) >= 0)
                ret |= (1ULL << i);
        break;
    }

    return ret;
}
