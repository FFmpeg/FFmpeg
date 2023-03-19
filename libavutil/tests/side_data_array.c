/*
 * Copyright (c) 2023 Jan Ekström <jeebjp@gmail.com>
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

#include <stdio.h>
#include "libavutil/frame.c"
#include "libavutil/mastering_display_metadata.h"

static void print_clls(const AVFrameSideData **sd, const int nb_sd)
{
    for (int i = 0; i < nb_sd; i++) {
        const AVFrameSideData *entry = sd[i];

        printf("sd %d, %s",
               i, av_frame_side_data_name(entry->type));

        if (entry->type != AV_FRAME_DATA_CONTENT_LIGHT_LEVEL) {
            putchar('\n');
            continue;
        }

        printf(": MaxCLL: %u\n",
               ((AVContentLightMetadata *)entry->data)->MaxCLL);
    }
}

typedef struct FrameSideDataSet {
    AVFrameSideData **sd;
    int            nb_sd;
} FrameSideDataSet;

int main(void)
{
    FrameSideDataSet set = { 0 };

    av_assert0(
        av_frame_side_data_new(&set.sd, &set.nb_sd,
                               AV_FRAME_DATA_AMBIENT_VIEWING_ENVIRONMENT,
                               0, 0));

    // test entries in the middle
    for (int value = 1; value < 4; value++) {
        AVFrameSideData *sd = av_frame_side_data_new(
            &set.sd, &set.nb_sd, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL,
            sizeof(AVContentLightMetadata), 0);

        av_assert0(sd);

        ((AVContentLightMetadata *)sd->data)->MaxCLL = value;
    }

    av_assert0(
        av_frame_side_data_new(
            &set.sd, &set.nb_sd, AV_FRAME_DATA_SPHERICAL, 0, 0));

    // test entries at the end
    for (int value = 1; value < 4; value++) {
        AVFrameSideData *sd = av_frame_side_data_new(
            &set.sd, &set.nb_sd, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL,
            sizeof(AVContentLightMetadata), 0);

        av_assert0(sd);

        ((AVContentLightMetadata *)sd->data)->MaxCLL = value + 3;
    }

    puts("Initial addition results with duplicates:");
    print_clls((const AVFrameSideData **)set.sd, set.nb_sd);

    {
        AVFrameSideData *sd = av_frame_side_data_new(
            &set.sd, &set.nb_sd, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL,
            sizeof(AVContentLightMetadata),
            AV_FRAME_SIDE_DATA_FLAG_UNIQUE);

        av_assert0(sd);

        ((AVContentLightMetadata *)sd->data)->MaxCLL = 1337;
    }

    puts("\nFinal state after a single 'no-duplicates' addition:");
    print_clls((const AVFrameSideData **)set.sd, set.nb_sd);

    av_frame_side_data_free(&set.sd, &set.nb_sd);

    return 0;
}
