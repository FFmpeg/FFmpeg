/*
 * Copyright (c) 2012 Justin Ruggles <justin.ruggles@gmail.com>
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
 * Cook audio parser
 *
 * Determines subpacket duration from extradata.
 */

#include <stdint.h>

#include "libavutil/intreadwrite.h"
#include "parser.h"

typedef struct CookParseContext {
    int duration;
} CookParseContext;

static int cook_parse(AVCodecParserContext *s1, AVCodecContext *avctx,
                      const uint8_t **poutbuf, int *poutbuf_size,
                      const uint8_t *buf, int buf_size)
{
    CookParseContext *s = s1->priv_data;

    if (!s->duration &&
                avctx->extradata && avctx->extradata_size >= 8 && avctx->channels)
        s->duration = AV_RB16(avctx->extradata + 4) / avctx->channels;

    s1->duration = s->duration;

    /* always return the full packet. this parser isn't doing any splitting or
       combining, only setting packet duration */
    *poutbuf      = buf;
    *poutbuf_size = buf_size;
    return buf_size;
}

const AVCodecParser ff_cook_parser = {
    .codec_ids      = { AV_CODEC_ID_COOK },
    .priv_data_size = sizeof(CookParseContext),
    .parser_parse   = cook_parse,
};
