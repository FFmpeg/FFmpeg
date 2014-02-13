/*
 * huffyuv codec for libavcodec
 *
 * Copyright (c) 2002-2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * see http://www.pcisys.net/~melanson/codecs/huffyuv.txt for a description of
 * the algorithm used
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

/**
 * @file
 * huffyuv codec for libavcodec.
 */

#include <stdint.h>

#include "libavutil/mem.h"

#include "avcodec.h"
#include "bswapdsp.h"
#include "huffyuv.h"

int ff_huffyuv_generate_bits_table(uint32_t *dst, const uint8_t *len_table)
{
    int len, index;
    uint32_t bits = 0;

    for (len = 32; len > 0; len--) {
        for (index = 0; index < 256; index++) {
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

av_cold int ff_huffyuv_alloc_temp(HYuvContext *s)
{
    int i;

    if (s->bitstream_bpp<24) {
        for (i=0; i<3; i++) {
            s->temp[i]= av_malloc(s->width + 16);
            if (!s->temp[i])
                return AVERROR(ENOMEM);
        }
    } else {
        s->temp[0]= av_mallocz(4*s->width + 16);
        if (!s->temp[0])
            return AVERROR(ENOMEM);
    }
    return 0;
}

av_cold void ff_huffyuv_common_init(AVCodecContext *avctx)
{
    HYuvContext *s = avctx->priv_data;

    s->avctx = avctx;
    s->flags = avctx->flags;

    ff_bswapdsp_init(&s->bdsp);

    s->width = avctx->width;
    s->height = avctx->height;
    assert(s->width>0 && s->height>0);
}

void ff_huffyuv_common_end(HYuvContext *s)
{
    int i;

    for(i = 0; i < 3; i++) {
        av_freep(&s->temp[i]);
    }
}
