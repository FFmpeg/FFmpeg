/*
 * huffyuv codec for libavcodec
 *
 * Copyright (c) 2002-2014 Michael Niedermayer <michaelni@gmx.at>
 *
 * see http://www.pcisys.net/~melanson/codecs/huffyuv.txt for a description of
 * the algorithm used
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
 * huffyuv codec for libavcodec.
 */

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/error.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"

#include "huffyuv.h"

int ff_huffyuv_generate_bits_table(uint32_t *dst, const uint8_t *len_table, int n)
{
    int len, index;
    uint32_t bits = 0;

    for (len = 32; len > 0; len--) {
        for (index = 0; index < n; index++) {
            if (len_table[index] == len)
                dst[index] = bits++;
        }
        if (bits & 1) {
            av_log(NULL, AV_LOG_ERROR, "Error generating huffman table\n");
            return -1;
        }
        bits >>= 1;
    }
    return 0;
}

av_cold int ff_huffyuv_alloc_temp(uint8_t *temp[3], uint16_t *temp16[3], int width)
{
    int i;

    for (i=0; i<3; i++) {
        temp[i] = av_malloc(4 * width + 16);
        if (!temp[i])
            return AVERROR(ENOMEM);
        temp16[i] = (uint16_t*)temp[i];
    }
    return 0;
}

av_cold void ff_huffyuv_common_end(uint8_t *temp[3], uint16_t *temp16[3])
{
    int i;

    for(i = 0; i < 3; i++) {
        av_freep(&temp[i]);
        temp16[i] = NULL;
    }
}
