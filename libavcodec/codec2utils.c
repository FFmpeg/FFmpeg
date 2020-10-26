/*
 * codec2 utility functions
 * Copyright (c) 2017 Tomas Härdin
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

#include <string.h>
#include "internal.h"
#include "libavcodec/codec2utils.h"

#if LIBAVCODEC_VERSION_MAJOR < 59
int avpriv_codec2_mode_bit_rate(void *logctx, int mode)
{
    int frame_size  = avpriv_codec2_mode_frame_size(logctx, mode);
    int block_align = avpriv_codec2_mode_block_align(logctx, mode);

    if (frame_size <= 0 || block_align <= 0) {
        return 0;
    }

    return 8 * 8000 * block_align / frame_size;
}

int avpriv_codec2_mode_frame_size(void *logctx, int mode)
{
    int frame_size_table[CODEC2_MODE_MAX+1] = {
        160,    // 3200
        160,    // 2400
        320,    // 1600
        320,    // 1400
        320,    // 1300
        320,    // 1200
        320,    // 700
        320,    // 700B
        320,    // 700C
    };

    if (mode < 0 || mode > CODEC2_MODE_MAX) {
        av_log(logctx, AV_LOG_ERROR, "unknown codec2 mode %i, can't find frame_size\n", mode);
        return 0;
    } else {
        return frame_size_table[mode];
    }
}

int avpriv_codec2_mode_block_align(void *logctx, int mode)
{
    int block_align_table[CODEC2_MODE_MAX+1] = {
        8,      // 3200
        6,      // 2400
        8,      // 1600
        7,      // 1400
        7,      // 1300
        6,      // 1200
        4,      // 700
        4,      // 700B
        4,      // 700C
    };

    if (mode < 0 || mode > CODEC2_MODE_MAX) {
        av_log(logctx, AV_LOG_ERROR, "unknown codec2 mode %i, can't find block_align\n", mode);
        return 0;
    } else {
        return block_align_table[mode];
    }
}
#endif
