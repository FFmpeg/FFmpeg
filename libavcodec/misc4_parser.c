/*
 * Micronas SC-4 parser
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

#include "parser.h"

typedef struct MISC4Context {
    ParseContext pc;
} MISC4Context;

static int misc4_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                       const uint8_t **poutbuf, int *poutbuf_size,
                       const uint8_t *buf, int buf_size)
{
    MISC4Context *ctx = s->priv_data;
    uint32_t state = ctx->pc.state;
    int next = END_NOT_FOUND, i = 0;

    *poutbuf_size = 0;
    *poutbuf = NULL;

    if (s->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        next = buf_size;
    } else {
        uint32_t marker = 0;

        switch (avctx->sample_rate) {
        case 8000:
        case 11025:
            marker = 0x11b;
            break;
        case 16000:
        case 32000:
            marker = 0x2b2;
            break;
        }

        for (; i < buf_size; i++) {
            state = (state << 8) | buf[i];
            if (state == marker && i > 3) {
                next = i - 3;
                break;
            }
        }

        ctx->pc.state = state;
        if (ff_combine_frame(&ctx->pc, next, &buf, &buf_size) < 0) {
            *poutbuf = NULL;
            *poutbuf_size = 0;
            return buf_size;
        }
    }

    *poutbuf      = buf;
    *poutbuf_size = buf_size;

    return next;
}

const AVCodecParser ff_misc4_parser = {
    .codec_ids      = { AV_CODEC_ID_MISC4 },
    .priv_data_size = sizeof(MISC4Context),
    .parser_parse   = misc4_parse,
    .parser_close   = ff_parse_close,
};
