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
#include "libavutil/internal.h"

static void print_entries(const AVFrameSideData **sd, const int nb_sd)
{
    for (int i = 0; i < nb_sd; i++) {
        const AVFrameSideData *entry = sd[i];

        printf("sd %d (size %"SIZE_SPECIFIER"), %s",
               i, entry->size, av_frame_side_data_name(entry->type));

        if (entry->type != AV_FRAME_DATA_SEI_UNREGISTERED) {
            putchar('\n');
            continue;
        }

        printf(": %d\n", *(int32_t *)entry->data);
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
                               AV_FRAME_DATA_CONTENT_LIGHT_LEVEL,
                               sizeof(int64_t), 0));
    av_assert0(
        av_frame_side_data_new(&set.sd, &set.nb_sd,
                               AV_FRAME_DATA_CONTENT_LIGHT_LEVEL,
                               sizeof(int32_t), AV_FRAME_SIDE_DATA_FLAG_REPLACE));

    // test entries in the middle
    for (int value = 1; value < 4; value++) {
        AVFrameSideData *sd = av_frame_side_data_new(
            &set.sd, &set.nb_sd, AV_FRAME_DATA_SEI_UNREGISTERED,
            sizeof(int32_t), 0);

        av_assert0(sd);

        *(int32_t *)sd->data = value;
    }

    av_assert0(
        av_frame_side_data_new(
            &set.sd, &set.nb_sd, AV_FRAME_DATA_SPHERICAL,
           sizeof(int64_t), 0));

    av_assert0(
        av_frame_side_data_new(
            &set.sd, &set.nb_sd, AV_FRAME_DATA_SPHERICAL,
            sizeof(int32_t), AV_FRAME_SIDE_DATA_FLAG_REPLACE));

    // test entries at the end
    for (int value = 1; value < 4; value++) {
        AVFrameSideData *sd = av_frame_side_data_new(
            &set.sd, &set.nb_sd, AV_FRAME_DATA_SEI_UNREGISTERED,
            sizeof(int32_t), 0);

        av_assert0(sd);

        *(int32_t *)sd->data = value + 3;
    }

    puts("Initial addition results with duplicates:");
    print_entries((const AVFrameSideData **)set.sd, set.nb_sd);

    {
        AVFrameSideData *sd = av_frame_side_data_new(
            &set.sd, &set.nb_sd, AV_FRAME_DATA_SEI_UNREGISTERED,
            sizeof(int32_t), AV_FRAME_SIDE_DATA_FLAG_UNIQUE);

        av_assert0(sd);

        *(int32_t *)sd->data = 1337;
    }

    puts("\nFinal state after a single 'no-duplicates' addition:");
    print_entries((const AVFrameSideData **)set.sd, set.nb_sd);

    av_frame_side_data_free(&set.sd, &set.nb_sd);

    return 0;
}
