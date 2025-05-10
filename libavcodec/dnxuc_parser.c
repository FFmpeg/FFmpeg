/*
 * Avid DNxUncomressed / SMPTE RDD 50 parser
 * Copyright (c) 2024 Martin Schitter
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

/*
 * This parser for DNxUncompressed video data is mostly based on the public
 * SMPTE RDD 50:2019 specification.
 */

#include "parser.h"
#include "libavutil/bswap.h"

typedef struct DNxUcParseContext {
    ParseContext pc;
    uint32_t remaining;
} DNxUcParseContext;

static int dnxuc_parse(AVCodecParserContext *s,
                    AVCodecContext *avctx,
                    const uint8_t **poutbuf, int *poutbuf_size,
                    const uint8_t *buf, int buf_size)
{
    DNxUcParseContext *ipc = s->priv_data;
    int next = END_NOT_FOUND;

    s->pict_type = AV_PICTURE_TYPE_NONE;

    *poutbuf_size = 0;
    *poutbuf = NULL;

    if (s->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        next = buf_size;
    } else {
        if (ipc->remaining == 0) {
            uint64_t state = ipc->pc.state64;
            for (int i = 0; i < buf_size; i++) {
                state = (state << 8) | buf[i];
                if (ipc->pc.index + i >= 7 && (uint32_t)state == MKBETAG('p','a','c','k')) {
                    uint32_t size = av_bswap32(state >> 32);
                    if (size >= 8) {
                         next = i - 7;
                         ipc->remaining = size + FFMIN(next, 0);
                         break;
                    }
                }
            }
            ipc->pc.state64 = state;
        } else if (ipc->remaining <= buf_size) {
            next = ipc->remaining;
            ipc->remaining = 0;
        } else {
            ipc->remaining -= buf_size;
        }
        if (ff_combine_frame(&ipc->pc, next, &buf, &buf_size) < 0) {
            *poutbuf = NULL;
            *poutbuf_size = 0;
            return buf_size;
        }
    }

    *poutbuf      = buf;
    *poutbuf_size = buf_size;

    return next;
}

const AVCodecParser ff_dnxuc_parser = {
    .codec_ids      = { AV_CODEC_ID_DNXUC },
    .priv_data_size = sizeof(DNxUcParseContext),
    .parser_parse   = dnxuc_parse,
    .parser_close   = ff_parse_close,
};
