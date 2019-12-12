/*
 * gsm 06.10 decoder, Microsoft variant
 * Copyright (c) 2010 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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

#define BITSTREAM_READER_LE
#include "avcodec.h"
#include "gsm.h"
#include "msgsmdec.h"

#include "gsmdec_template.c"

int ff_msgsm_decode_block(AVCodecContext *avctx, int16_t *samples,
                          const uint8_t *buf, int mode)
{
    int res;
    GetBitContext gb;
    init_get_bits(&gb, buf, GSM_MS_BLOCK_SIZE * 8);
    res = gsm_decode_block(avctx, samples, &gb, mode);
    if (res < 0)
        return res;
    return gsm_decode_block(avctx, samples + GSM_FRAME_SIZE, &gb, mode);
}
