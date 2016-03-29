/*
 * Copyright (c) 2016 Paul B Mahol
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

#include "avcodec.h"
#include "bytestream.h"
#include "dca_syncwords.h"
#include "libavutil/mem.h"

static int dca_core(AVBitStreamFilterContext *bsfc,
                    AVCodecContext *avctx, const char *args,
                    uint8_t **poutbuf, int *poutbuf_size,
                    const uint8_t *buf, int buf_size,
                    int keyframe)
{
    GetByteContext gb;
    uint32_t syncword;
    int core_size = 0;

    bytestream2_init(&gb, buf, buf_size);
    syncword = bytestream2_get_be32(&gb);
    bytestream2_skip(&gb, 1);

    switch (syncword) {
    case DCA_SYNCWORD_CORE_BE:
        core_size = ((bytestream2_get_be24(&gb) >> 4) & 0x3fff) + 1;
        break;
    }

    *poutbuf = (uint8_t *)buf;

    if (core_size > 0 && core_size <= buf_size) {
        *poutbuf_size = core_size;
    } else {
        *poutbuf_size = buf_size;
    }

    return 0;
}

AVBitStreamFilter ff_dca_core_bsf = {
    .name   = "dca_core",
    .filter = dca_core,
};
