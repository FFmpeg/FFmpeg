/*
 * Copyright (c) 2023 Intel Corporation
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

#include <stddef.h>
#include "libavutil/macros.h"
#include "av1_levels.h"

/** ignore entries which named in spec but no details. Like level 2.2 and 7.0. */
static const AV1LevelDescriptor av1_levels[] = {
    // Name                      MaxVSize                           MainMbps              MaxTiles
    // |  level_idx                 | MaxDisplayRate                    | HighMbps         | MaxTileCols
    // |      |   MaxPicSize        |       |     MaxDecodeRate         |    |   MainCR    |   |
    // |      |     |    MaxHSize   |       |           | MaxHeaderRate |    |     | HighCR|   |
    // |      |     |        |      |       |           |       |       |    |     |  |    |   |
    { "2.0",  0,   147456,  2048, 1152,   4423680,     5529600, 150,   1.5,     0, 2, 0,   8,  4 },
    { "2.1",  1,   278784,  2816, 1584,   8363520,    10454400, 150,   3.0,     0, 2, 0,   8,  4 },
    { "3.0",  4,   665856,  4352, 2448,  19975680,    24969600, 150,   6.0,     0, 2, 0,  16,  6 },
    { "3.1",  5,  1065024,  5504, 3096,  31950720,    39938400, 150,  10.0,     0, 2, 0,  16,  6 },
    { "4.0",  8,  2359296,  6144, 3456,  70778880,    77856768, 300,  12.0,  30.0, 4, 4,  32,  8 },
    { "4.1",  9,  2359296,  6144, 3456,  141557760,  155713536, 300,  20.0,  50.0, 4, 4,  32,  8 },
    { "5.0", 12,  8912896,  8192, 4352,  267386880,  273715200, 300,  30.0, 100.0, 6, 4,  64,  8 },
    { "5.1", 13,  8912896,  8192, 4352,  534773760,  547430400, 300,  40.0, 160.0, 8, 4,  64,  8 },
    { "5.2", 14,  8912896,  8192, 4352, 1069547520, 1094860800, 300,  60.0, 240.0, 8, 4,  64,  8 },
    { "5.3", 15,  8912896,  8192, 4352, 1069547520, 1176502272, 300,  60.0, 240.0, 8, 4,  64,  8 },
    { "6.0", 16, 35651584, 16384, 8704, 1069547520, 1176502272, 300,  60.0, 240.0, 8, 4, 128, 16 },
    { "6.1", 17, 35651584, 16384, 8704, 2139095040, 2189721600, 300, 100.0, 480.0, 8, 4, 128, 16 },
    { "6.2", 18, 35651584, 16384, 8704, 4278190080, 4379443200, 300, 160.0, 800.0, 8, 4, 128, 16 },
    { "6.3", 19, 35651584, 16384, 8704, 4278190080, 4706009088, 300, 160.0, 800.0, 8, 4, 128, 16 },
};

const AV1LevelDescriptor *ff_av1_guess_level(int64_t bitrate,
                                             int tier,
                                             int width,
                                             int height,
                                             int tiles,
                                             int tile_cols,
                                             float fps)
{
    int pic_size;
    uint64_t display_rate;
    float max_br;

    pic_size = width * height;
    display_rate = (uint64_t)pic_size * fps;

    for (int i = 0; i < FF_ARRAY_ELEMS(av1_levels); i++) {
        const AV1LevelDescriptor *level = &av1_levels[i];
        // Limitation: decode rate, header rate, compress rate, etc. are not considered.
        if (pic_size > level->max_pic_size)
            continue;
        if (width > level->max_h_size)
            continue;
        if (height > level->max_v_size)
            continue;
        if (display_rate > level->max_display_rate)
            continue;

        if (tier)
            max_br = level->high_mbps;
        else
            max_br = level->main_mbps;
        if (!max_br)
            continue;
        if (bitrate > (int64_t)(1000000.0 * max_br))
            continue;

        if (tiles > level->max_tiles)
            continue;
        if (tile_cols > level->max_tile_cols)
            continue;
        return level;
    }

    return NULL;
}
