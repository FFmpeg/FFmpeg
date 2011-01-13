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
 * audio conversion routines
 */

#include "libavutil/avstring.h"
#include "audioconvert.h"

static const char * const channel_names[] = {
    "FL", "FR", "FC", "LFE", "BL",  "BR",  "FLC", "FRC",
    "BC", "SL", "SR", "TC",  "TFL", "TFC", "TFR", "TBL",
    "TBC", "TBR",
    [29] = "DL",
    [30] = "DR",
};

static const char *get_channel_name(int channel_id)
{
    if (channel_id < 0 || channel_id >= FF_ARRAY_ELEMS(channel_names))
        return NULL;
    return channel_names[channel_id];
}

static const struct {
    const char *name;
    int         nb_channels;
    int64_t     layout;
} channel_layout_map[] = {
    { "mono",        1,  AV_CH_LAYOUT_MONO },
    { "stereo",      2,  AV_CH_LAYOUT_STEREO },
    { "4.0",         4,  AV_CH_LAYOUT_4POINT0 },
    { "quad",        4,  AV_CH_LAYOUT_QUAD },
    { "5.0",         5,  AV_CH_LAYOUT_5POINT0 },
    { "5.0",         5,  AV_CH_LAYOUT_5POINT0_BACK },
    { "5.1",         6,  AV_CH_LAYOUT_5POINT1 },
    { "5.1",         6,  AV_CH_LAYOUT_5POINT1_BACK },
    { "5.1+downmix", 8,  AV_CH_LAYOUT_5POINT1|AV_CH_LAYOUT_STEREO_DOWNMIX, },
    { "7.1",         8,  AV_CH_LAYOUT_7POINT1 },
    { "7.1(wide)",   8,  AV_CH_LAYOUT_7POINT1_WIDE },
    { "7.1+downmix", 10, AV_CH_LAYOUT_7POINT1|AV_CH_LAYOUT_STEREO_DOWNMIX, },
    { 0 }
};

int64_t av_get_channel_layout(const char *name)
{
    int i = 0;
    do {
        if (!strcmp(channel_layout_map[i].name, name))
            return channel_layout_map[i].layout;
        i++;
    } while (channel_layout_map[i].name);

    return 0;
}

void av_get_channel_layout_string(char *buf, int buf_size,
                                  int nb_channels, int64_t channel_layout)
{
    int i;

    if (nb_channels <= 0)
        nb_channels = av_get_channel_layout_nb_channels(channel_layout);

    for (i = 0; channel_layout_map[i].name; i++)
        if (nb_channels    == channel_layout_map[i].nb_channels &&
            channel_layout == channel_layout_map[i].layout) {
            av_strlcpy(buf, channel_layout_map[i].name, buf_size);
            return;
        }

    snprintf(buf, buf_size, "%d channels", nb_channels);
    if (channel_layout) {
        int i,ch;
        av_strlcat(buf, " (", buf_size);
        for(i=0,ch=0; i<64; i++) {
            if ((channel_layout & (1L<<i))) {
                const char *name = get_channel_name(i);
                if (name) {
                    if (ch>0) av_strlcat(buf, "|", buf_size);
                    av_strlcat(buf, name, buf_size);
                }
                ch++;
            }
        }
        av_strlcat(buf, ")", buf_size);
    }
}

int av_get_channel_layout_nb_channels(int64_t channel_layout)
{
    int count;
    uint64_t x = channel_layout;
    for (count = 0; x; count++)
        x &= x-1; // unset lowest set bit
    return count;
}
