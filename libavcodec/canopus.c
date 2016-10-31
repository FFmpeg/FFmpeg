/*
 * Canopus common routines
 * Copyright (c) 2015 Vittorio Giovara <vittorio.giovara@gmail.com>
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

#include <stdint.h>

#include "libavutil/rational.h"

#include "avcodec.h"
#include "bytestream.h"
#include "canopus.h"

int ff_canopus_parse_info_tag(AVCodecContext *avctx,
                              const uint8_t *src, size_t size)
{
    GetByteContext gbc;
    int par_x, par_y, field_order;

    bytestream2_init(&gbc, src, size);

    /* Parse aspect ratio. */
    bytestream2_skip(&gbc, 8); // unknown, 16 bits 1
    par_x = bytestream2_get_le32(&gbc);
    par_y = bytestream2_get_le32(&gbc);
    if (par_x && par_y)
        av_reduce(&avctx->sample_aspect_ratio.num,
                  &avctx->sample_aspect_ratio.den,
                  par_x, par_y, 255);

    /* Short INFO tag (used in CLLC) has only AR data. */
    if (size == 0x18)
        return 0;

    bytestream2_skip(&gbc, 16); // unknown RDRT tag

    /* Parse FIEL tag. */
    bytestream2_skip(&gbc, 8); // 'FIEL' and 4 bytes 0
    field_order = bytestream2_get_le32(&gbc);
    switch (field_order) {
    case 0: avctx->field_order = AV_FIELD_TT; break;
    case 1: avctx->field_order = AV_FIELD_BB; break;
    case 2: avctx->field_order = AV_FIELD_PROGRESSIVE; break;
    }

    return 0;
}
