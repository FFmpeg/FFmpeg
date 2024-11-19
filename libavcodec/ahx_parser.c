/*
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
 * AHX audio parser
 *
 * Splits packets into individual blocks.
 */

#include "libavutil/intreadwrite.h"
#include "parser.h"

typedef struct AHXParseContext {
    ParseContext pc;
    uint32_t header;
    int size;
} AHXParseContext;

static int ahx_parse(AVCodecParserContext *s1,
                     AVCodecContext *avctx,
                     const uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size)
{
    AHXParseContext *s = s1->priv_data;
    ParseContext *pc = &s->pc;
    uint32_t state = pc->state;
    int next = END_NOT_FOUND;

    for (int i = 0; i < buf_size; i++) {
        state = (state << 8) | buf[i];
        s->size++;
        if (s->size == 4 && !s->header)
            s->header = state;
        if (s->size > 4 && state == s->header) {
            next = i - 3;
            s->size = 0;
            break;
        }
    }
    pc->state = state;

    if (ff_combine_frame(pc, next, &buf, &buf_size) < 0) {
        *poutbuf      = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }

    s1->duration = 1152;
    s1->key_frame = 1;

    *poutbuf = buf;
    *poutbuf_size = buf_size;

    return next;
}

const AVCodecParser ff_ahx_parser = {
    .codec_ids      = { AV_CODEC_ID_AHX },
    .priv_data_size = sizeof(AHXParseContext),
    .parser_parse   = ahx_parse,
    .parser_close   = ff_parse_close,
};
