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
#include "mem.h"
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
    [AV_CHAN_SIDE_SURROUND_LEFT   ] = { "SSL",       "side surround left"    },
    [AV_CHAN_SIDE_SURROUND_RIGHT  ] = { "SSR",       "side surround right"   },
    [AV_CHAN_TOP_SURROUND_LEFT    ] = { "TTL",       "top surround left"     },
    [AV_CHAN_TOP_SURROUND_RIGHT   ] = { "TTR",       "top surround right"    },
    [AV_CHAN_BINAURAL_LEFT        ] = { "BIL",       "binaural left"         },
    [AV_CHAN_BINAURAL_RIGHT       ] = { "BIR",       "binaural right"        },
};

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
    else if (channel_id == AV_CHAN_UNKNOWN)
        av_bprintf(bp, "UNK");
    else if (channel_id == AV_CHAN_UNUSED)
        av_bprintf(bp, "UNSD");
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

    if (bp.len >= INT_MAX)
        return AVERROR(ERANGE);
    return bp.len + 1;
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
    else if (channel_id == AV_CHAN_UNKNOWN)
        av_bprintf(bp, "unknown");
    else if (channel_id == AV_CHAN_UNUSED)
        av_bprintf(bp, "unused");
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

    if (bp.len >= INT_MAX)
        return AVERROR(ERANGE);
    return bp.len + 1;
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
    if (!strcmp(str, "UNK"))
        return AV_CHAN_UNKNOWN;
    if (!strcmp(str, "UNSD"))
        return AV_CHAN_UNUSED;

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
    { "3.1.2",          AV_CHANNEL_LAYOUT_3POINT1POINT2       },
    { "hexagonal",      AV_CHANNEL_LAYOUT_HEXAGONAL           },
    { "6.1",            AV_CHANNEL_LAYOUT_6POINT1             },
    { "6.1(back)",      AV_CHANNEL_LAYOUT_6POINT1_BACK        },
    { "6.1(front)",     AV_CHANNEL_LAYOUT_6POINT1_FRONT       },
    { "7.0",            AV_CHANNEL_LAYOUT_7POINT0             },
    { "7.0(front)",     AV_CHANNEL_LAYOUT_7POINT0_FRONT       },
    { "7.1",            AV_CHANNEL_LAYOUT_7POINT1             },
    { "7.1(wide)",      AV_CHANNEL_LAYOUT_7POINT1_WIDE_BACK   },
    { "7.1(wide-side)", AV_CHANNEL_LAYOUT_7POINT1_WIDE        },
    { "5.1.2",          AV_CHANNEL_LAYOUT_5POINT1POINT2       },
    { "5.1.2(back)",    AV_CHANNEL_LAYOUT_5POINT1POINT2_BACK  },
    { "octagonal",      AV_CHANNEL_LAYOUT_OCTAGONAL           },
    { "cube",           AV_CHANNEL_LAYOUT_CUBE                },
    { "5.1.4",          AV_CHANNEL_LAYOUT_5POINT1POINT4_BACK  },
    { "7.1.2",          AV_CHANNEL_LAYOUT_7POINT1POINT2       },
    { "7.1.4",          AV_CHANNEL_LAYOUT_7POINT1POINT4_BACK  },
    { "7.2.3",          AV_CHANNEL_LAYOUT_7POINT2POINT3       },
    { "9.1.4",          AV_CHANNEL_LAYOUT_9POINT1POINT4_BACK  },
    { "9.1.6",          AV_CHANNEL_LAYOUT_9POINT1POINT6       },
    { "hexadecagonal",  AV_CHANNEL_LAYOUT_HEXADECAGONAL       },
    { "binaural",       AV_CHANNEL_LAYOUT_BINAURAL            },
    { "downmix",        AV_CHANNEL_LAYOUT_STEREO_DOWNMIX,     },
    { "22.2",           AV_CHANNEL_LAYOUT_22POINT2,           },
};

int av_channel_layout_custom_init(AVChannelLayout *channel_layout, int nb_channels)
{
    AVChannelCustom *map;

    if (nb_channels <= 0)
        return AVERROR(EINVAL);

    map = av_calloc(nb_channels, sizeof(*channel_layout->u.map));
    if (!map)
        return AVERROR(ENOMEM);
    for (int i = 0; i < nb_channels; i++)
        map[i].id = AV_CHAN_UNKNOWN;

    channel_layout->order       = AV_CHANNEL_ORDER_CUSTOM;
    channel_layout->nb_channels = nb_channels;
    channel_layout->u.map       = map;

    return 0;
}

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

static int parse_channel_list(AVChannelLayout *ch_layout, const char *str)
{
    int ret;
    int nb_channels = 0;
    AVChannelCustom *map = NULL;
    AVChannelCustom custom = {0};

    while (*str) {
        char *channel, *chname;
        int ret = av_opt_get_key_value(&str, "@", "+", AV_OPT_FLAG_IMPLICIT_KEY, &channel, &chname);
        if (ret < 0) {
            av_freep(&map);
            return ret;
        }
        if (*str)
            str++; // skip separator
        if (!channel) {
            channel = chname;
            chname = NULL;
        }
        av_strlcpy(custom.name, chname ? chname : "", sizeof(custom.name));
        custom.id = av_channel_from_string(channel);
        av_free(channel);
        av_free(chname);
        if (custom.id == AV_CHAN_NONE) {
            av_freep(&map);
            return AVERROR(EINVAL);
        }

        av_dynarray2_add((void **)&map, &nb_channels, sizeof(custom), (void *)&custom);
        if (!map)
            return AVERROR(ENOMEM);
    }

    if (!nb_channels)
        return AVERROR(EINVAL);

    ch_layout->order = AV_CHANNEL_ORDER_CUSTOM;
    ch_layout->u.map = map;
    ch_layout->nb_channels = nb_channels;

    ret = av_channel_layout_retype(ch_layout, 0, AV_CHANNEL_LAYOUT_RETYPE_FLAG_CANONICAL);
    av_assert0(ret == 0);

    return 0;
}

int av_channel_layout_from_string(AVChannelLayout *channel_layout,
                                  const char *str)
{
    int i, matches, ret;
    int channels = 0, nb_channels = 0;
    char *chlist, *end;
    uint64_t mask = 0;

    /* channel layout names */
    for (i = 0; i < FF_ARRAY_ELEMS(channel_layout_map); i++) {
        if (channel_layout_map[i].name && !strcmp(str, channel_layout_map[i].name)) {
            *channel_layout = channel_layout_map[i].layout;
            return 0;
        }
    }

    /* This function is a channel layout initializer, so we have to
     * zero-initialize before we start setting fields individually. */
    memset(channel_layout, 0, sizeof(*channel_layout));

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
                        av_channel_layout_uninit(channel_layout);
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
    matches = av_sscanf(str, "%d channels (%[^)]", &nb_channels, chlist);
    ret = parse_channel_list(channel_layout, chlist);
    av_freep(&chlist);
    if (ret < 0 && ret != AVERROR(EINVAL))
        return ret;

    if (ret >= 0) {
        end = strchr(str, ')');
        if (matches == 2 && (nb_channels != channel_layout->nb_channels || !end || *++end)) {
            av_channel_layout_uninit(channel_layout);
            return AVERROR(EINVAL);
        }
        return 0;
    }

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

static int64_t masked_description(const AVChannelLayout *channel_layout, int start_channel)
{
    uint64_t mask = 0;
    for (int i = start_channel; i < channel_layout->nb_channels; i++) {
        enum AVChannel ch = channel_layout->u.map[i].id;
        if (ch >= 0 && ch < 63 && mask < (1ULL << ch))
            mask |= (1ULL << ch);
        else
            return AVERROR(EINVAL);
    }
    return mask;
}

static int has_channel_names(const AVChannelLayout *channel_layout)
{
    if (channel_layout->order != AV_CHANNEL_ORDER_CUSTOM)
        return 0;
    for (int i = 0; i < channel_layout->nb_channels; i++)
        if (channel_layout->u.map[i].name[0])
            return 1;
    return 0;
}

int av_channel_layout_ambisonic_order(const AVChannelLayout *channel_layout)
{
    int i, highest_ambi, order;

    if (channel_layout->order != AV_CHANNEL_ORDER_AMBISONIC &&
        channel_layout->order != AV_CHANNEL_ORDER_CUSTOM)
        return AVERROR(EINVAL);

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

static enum AVChannelOrder canonical_order(AVChannelLayout *channel_layout)
{
    int has_known_channel = 0;
    int order;

    if (channel_layout->order != AV_CHANNEL_ORDER_CUSTOM)
        return channel_layout->order;

    if (has_channel_names(channel_layout))
        return AV_CHANNEL_ORDER_CUSTOM;

    for (int i = 0; i < channel_layout->nb_channels && !has_known_channel; i++)
        if (channel_layout->u.map[i].id != AV_CHAN_UNKNOWN)
            has_known_channel = 1;
    if (!has_known_channel)
        return AV_CHANNEL_ORDER_UNSPEC;

    if (masked_description(channel_layout, 0) > 0)
        return AV_CHANNEL_ORDER_NATIVE;

    order = av_channel_layout_ambisonic_order(channel_layout);
    if (order >= 0 && masked_description(channel_layout, (order + 1) * (order + 1)) >= 0)
        return AV_CHANNEL_ORDER_AMBISONIC;

    return AV_CHANNEL_ORDER_CUSTOM;
}

/**
 * If the custom layout is n-th order standard-order ambisonic, with optional
 * extra non-diegetic channels at the end, write its string description in bp.
 * Return a negative error code otherwise.
 */
static int try_describe_ambisonic(AVBPrint *bp, const AVChannelLayout *channel_layout)
{
    int nb_ambi_channels;
    int order = av_channel_layout_ambisonic_order(channel_layout);
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
            int64_t mask;
            if (!has_channel_names(channel_layout) &&
                (mask = masked_description(channel_layout, nb_ambi_channels)) > 0) {
                extra.order       = AV_CHANNEL_ORDER_NATIVE;
                extra.nb_channels = av_popcount64(mask);
                extra.u.mask      = mask;
            } else {
                extra.order       = AV_CHANNEL_ORDER_CUSTOM;
                extra.nb_channels = channel_layout->nb_channels - nb_ambi_channels;
                extra.u.map       = channel_layout->u.map + nb_ambi_channels;
            }
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
            int64_t mask;
            int res = try_describe_ambisonic(bp, channel_layout);
            if (res >= 0)
                return 0;
            if (!has_channel_names(channel_layout) &&
                (mask = masked_description(channel_layout, 0)) > 0) {
                AVChannelLayout native = { .order       = AV_CHANNEL_ORDER_NATIVE,
                                           .nb_channels = av_popcount64(mask),
                                           .u.mask      = mask };
                return av_channel_layout_describe_bprint(&native, bp);
            }
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

    if (bp.len >= INT_MAX)
        return AVERROR(ERANGE);
    return bp.len + 1;
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

int av_channel_layout_retype(AVChannelLayout *channel_layout, enum AVChannelOrder order, int flags)
{
    int allow_lossy = !(flags & AV_CHANNEL_LAYOUT_RETYPE_FLAG_LOSSLESS);
    int lossy;

    if (!av_channel_layout_check(channel_layout))
        return AVERROR(EINVAL);

    if (flags & AV_CHANNEL_LAYOUT_RETYPE_FLAG_CANONICAL)
        order = canonical_order(channel_layout);

    if (channel_layout->order == order)
        return 0;

    switch (order) {
    case AV_CHANNEL_ORDER_UNSPEC: {
        int nb_channels = channel_layout->nb_channels;
        if (channel_layout->order == AV_CHANNEL_ORDER_CUSTOM) {
            lossy = 0;
            for (int i = 0; i < nb_channels; i++) {
                if (channel_layout->u.map[i].id != AV_CHAN_UNKNOWN || channel_layout->u.map[i].name[0]) {
                    lossy = 1;
                    break;
                }
            }
        } else {
            lossy = 1;
        }
        if (!lossy || allow_lossy) {
            void *opaque = channel_layout->opaque;
            av_channel_layout_uninit(channel_layout);
            channel_layout->order       = AV_CHANNEL_ORDER_UNSPEC;
            channel_layout->nb_channels = nb_channels;
            channel_layout->opaque      = opaque;
            return lossy;
        }
        return AVERROR(ENOSYS);
        }
    case AV_CHANNEL_ORDER_NATIVE:
        if (channel_layout->order == AV_CHANNEL_ORDER_CUSTOM) {
            int64_t mask = masked_description(channel_layout, 0);
            if (mask < 0)
                return AVERROR(ENOSYS);
            lossy = has_channel_names(channel_layout);
            if (!lossy || allow_lossy) {
                void *opaque = channel_layout->opaque;
                av_channel_layout_uninit(channel_layout);
                av_channel_layout_from_mask(channel_layout, mask);
                channel_layout->opaque = opaque;
                return lossy;
            }
        }
        return AVERROR(ENOSYS);
    case AV_CHANNEL_ORDER_CUSTOM: {
        AVChannelLayout custom = { 0 };
        int ret = av_channel_layout_custom_init(&custom, channel_layout->nb_channels);
        void *opaque = channel_layout->opaque;
        if (ret < 0)
            return ret;
        if (channel_layout->order != AV_CHANNEL_ORDER_UNSPEC)
            for (int i = 0; i < channel_layout->nb_channels; i++)
                custom.u.map[i].id = av_channel_layout_channel_from_index(channel_layout, i);
        av_channel_layout_uninit(channel_layout);
        *channel_layout = custom;
        channel_layout->opaque = opaque;
        return 0;
        }
    case AV_CHANNEL_ORDER_AMBISONIC:
        if (channel_layout->order == AV_CHANNEL_ORDER_CUSTOM) {
            int64_t mask;
            int nb_channels = channel_layout->nb_channels;
            int order = av_channel_layout_ambisonic_order(channel_layout);
            if (order < 0)
                return AVERROR(ENOSYS);
            mask = masked_description(channel_layout, (order + 1) * (order + 1));
            if (mask < 0)
                return AVERROR(ENOSYS);
            lossy = has_channel_names(channel_layout);
            if (!lossy || allow_lossy) {
                void *opaque = channel_layout->opaque;
                av_channel_layout_uninit(channel_layout);
                channel_layout->order       = AV_CHANNEL_ORDER_AMBISONIC;
                channel_layout->nb_channels = nb_channels;
                channel_layout->u.mask      = mask;
                channel_layout->opaque      = opaque;
                return lossy;
            }
        }
        return AVERROR(ENOSYS);
    default:
        return AVERROR(EINVAL);
    }
}
