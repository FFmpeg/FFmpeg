/*
 * IEC 61937 common code
 * Copyright (c) 2009 Bartlomiej Wolowiec
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "spdif.h"
#include "libavutil/bswap.h"

//TODO move to DSP
void ff_spdif_bswap_buf16(uint16_t *dst, const uint16_t *src, int w)
{
    int i;

    for (i = 0; i + 8 <= w; i += 8) {
        dst[i + 0] = av_bswap16(src[i + 0]);
        dst[i + 1] = av_bswap16(src[i + 1]);
        dst[i + 2] = av_bswap16(src[i + 2]);
        dst[i + 3] = av_bswap16(src[i + 3]);
        dst[i + 4] = av_bswap16(src[i + 4]);
        dst[i + 5] = av_bswap16(src[i + 5]);
        dst[i + 6] = av_bswap16(src[i + 6]);
        dst[i + 7] = av_bswap16(src[i + 7]);
    }
    for (; i < w; i++)
        dst[i + 0] = av_bswap16(src[i + 0]);
}
