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

#include "avstring.h"
#include "avutil.h"
#include "channel_layout.h"
#include "bprint.h"
#include "common.h"

struct channel_name {
    const char *name;
    const char *description;
};

static const struct channel_name channel_names[] = {
     [0] = { "FL",        "front left"            },
     [1] = { "FR",        "front right"           },
     [2] = { "FC",        "front center"          },
     [3] = { "LFE",       "low frequency"         },
     [4] = { "BL",        "back left"             },
     [5] = { "BR",        "back right"            },
     [6] = { "FLC",       "front left-of-center"  },
     [7] = { "FRC",       "front right-of-center" },
     [8] = { "BC",        "back center"           },
     [9] = { "SL",        "side left"             },
    [10] = { "SR",        "side right"            },
    [11] = { "TC",        "top center"            },
    [12] = { "TFL",       "top front left"        },
    [13] = { "TFC",       "top front center"      },
    [14] = { "TFR",       "top front right"       },
    [15] = { "TBL",       "top back left"         },
    [16] = { "TBC",       "top back center"       },
    [17] = { "TBR",       "top back right"        },
    [29] = { "DL",        "downmix left"          },
    [30] = { "DR",        "downmix right"         },
    [31] = { "WL",        "wide left"             },
    [32] = { "WR",        "wide right"            },
    [33] = { "SDL",       "surround direct left"  },
    [34] = { "SDR",       "surround direct right" },
    [35] = { "LFE2",      "low frequency 2"       },
};

static const char *get_channel_name(int channel_id)
{
    if (channel_id < 0 || channel_id >= FF_ARRAY_ELEMS(channel_names))
        return NULL;
    return channel_names[channel_id].name;
}

static const struct {
    const char *name;
    int         nb_channels;
    uint64_t     layout;
} channel_layout_map[] = {
    { "mono",        1,  AV_CH_LAYOUT_MONO },
    { "stereo",      2,  AV_CH_LAYOUT_STEREO },
    { "2.1",         3,  AV_CH_LAYOUT_2POINT1 },
    { "3.0",         3,  AV_CH_LAYOUT_SURROUND },
    { "3.0(back)",   3,  AV_CH_LAYOUT_2_1 },
    { "4.0",         4,  AV_CH_LAYOUT_4POINT0 },
    { "quad",        4,  AV_CH_LAYOUT_QUAD },
    { "quad(side)",  4,  AV_CH_LAYOUT_2_2 },
    { "3.1",         4,  AV_CH_LAYOUT_3POINT1 },
    { "5.0",         5,  AV_CH_LAYOUT_5POINT0_BACK },
    { "5.0(side)",   5,  AV_CH_LAYOUT_5POINT0 },
    { "4.1",         5,  AV_CH_LAYOUT_4POINT1 },
    { "5.1",         6,  AV_CH_LAYOUT_5POINT1_BACK },
    { "5.1(side)",   6,  AV_CH_LAYOUT_5POINT1 },
    { "6.0",         6,  AV_CH_LAYOUT_6POINT0 },
    { "6.0(front)",  6,  AV_CH_LAYOUT_6POINT0_FRONT },
    { "hexagonal",   6,  AV_CH_LAYOUT_HEXAGONAL },
    { "6.1",         7,  AV_CH_LAYOUT_6POINT1 },
    { "6.1",         7,  AV_CH_LAYOUT_6POINT1_BACK },
    { "6.1(front)",  7,  AV_CH_LAYOUT_6POINT1_FRONT },
    { "7.0",         7,  AV_CH_LAYOUT_7POINT0 },
    { "7.0(front)",  7,  AV_CH_LAYOUT_7POINT0_FRONT },
    { "7.1",         8,  AV_CH_LAYOUT_7POINT1 },
    { "7.1(wide)",   8,  AV_CH_LAYOUT_7POINT1_WIDE_BACK },
    { "7.1(wide-side)",   8,  AV_CH_LAYOUT_7POINT1_WIDE },
    { "octagonal",   8,  AV_CH_LAYOUT_OCTAGONAL },
    { "downmix",     2,  AV_CH_LAYOUT_STEREO_DOWNMIX, },
};

static uint64_t get_channel_layout_single(const char *name, int name_len)
{
    int i;
    char *end;
    int64_t layout;

    for (i = 0; i < FF_ARRAY_ELEMS(channel_layout_map); i++) {
        if (strlen(channel_layout_map[i].name) == name_len &&
            !memcmp(channel_layout_map[i].name, name, name_len))
            return channel_layout_map[i].layout;
    }
    for (i = 0; i < FF_ARRAY_ELEMS(channel_names); i++)
        if (channel_names[i].name &&
            strlen(channel_names[i].name) == name_len &&
            !memcmp(channel_names[i].name, name, name_len))
            return (int64_t)1 << i;
    i = strtol(name, &end, 10);
    if (end - name == name_len ||
        (end + 1 - name == name_len && *end  == 'c'))
        return av_get_default_channel_layout(i);
    layout = strtoll(name, &end, 0);
    if (end - name == name_len)
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

void av_bprint_channel_layout(struct AVBPrint *bp,
                              int nb_channels, uint64_t channel_layout)
{
    int i;

    if (nb_channels <= 0)
        nb_channels = av_get_channel_layout_nb_channels(channel_layout);

    for (i = 0; i < FF_ARRAY_ELEMS(channel_layout_map); i++)
        if (nb_channels    == channel_layout_map[i].nb_channels &&
            channel_layout == channel_layout_map[i].layout) {
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
        if (nb_channels == channel_layout_map[i].nb_channels)
            return channel_layout_map[i].layout;
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
    if (layout) *layout = channel_layout_map[index].layout;
    if (name)   *name   = channel_layout_map[index].name;
    return 0;
}
