/*
 * Copyright (c) 2011  Justin Ruggles
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
 * ADX audio parser
 *
 * Splits packets into individual blocks.
 */

#include "libavutil/intreadwrite.h"
#include "parser.h"
#include "adx.h"

typedef struct ADXParseContext {
    ParseContext pc;
    int header_size;
    int block_size;
    int remaining;
} ADXParseContext;

static int adx_parse(AVCodecParserContext *s1,
                           AVCodecContext *avctx,
                           const uint8_t **poutbuf, int *poutbuf_size,
                           const uint8_t *buf, int buf_size)
{
    ADXParseContext *s = s1->priv_data;
    ParseContext *pc = &s->pc;
    int next = END_NOT_FOUND;
    int i;
    uint64_t state = pc->state64;

    if (!s->header_size) {
        for (i = 0; i < buf_size; i++) {
            state = (state << 8) | buf[i];
            /* check for fixed fields in ADX header for possible match */
            if ((state & 0xFFFF0000FFFFFF00) == 0x8000000003120400ULL) {
                int channels    = state & 0xFF;
                int header_size = ((state >> 32) & 0xFFFF) + 4;
                if (channels > 0 && header_size >= 8) {
                    s->header_size = header_size;
                    s->block_size  = BLOCK_SIZE * channels;
                    s->remaining   = i - 7 + s->header_size + s->block_size;
                    break;
                }
            }
        }
        pc->state64 = state;
    }

    if (s->header_size) {
        if (!s->remaining)
            s->remaining = s->block_size;
        if (s->remaining <= buf_size) {
            next = s->remaining;
            s->remaining = 0;
        } else
            s->remaining -= buf_size;
    }

    if (ff_combine_frame(pc, next, &buf, &buf_size) < 0 || !buf_size) {
        *poutbuf      = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }
    *poutbuf = buf;
    *poutbuf_size = buf_size;
    return next;
}

AVCodecParser ff_adx_parser = {
    .codec_ids      = { CODEC_ID_ADPCM_ADX },
    .priv_data_size = sizeof(ADXParseContext),
    .parser_parse   = adx_parse,
    .parser_close   = ff_parse_close,
};
