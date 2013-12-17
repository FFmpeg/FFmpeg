/*
 * PNG parser
 * Copyright (c) 2009 Peter Holik
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
 * PNG parser
 */

#include "parser.h"
#include "png.h"

typedef struct PNGParseContext {
    ParseContext pc;
    uint32_t chunk_pos;           ///< position inside current chunk
    uint32_t chunk_length;        ///< length of the current chunk
    uint32_t remaining_size;      ///< remaining size of the current chunk
} PNGParseContext;

static int png_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                     const uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size)
{
    PNGParseContext *ppc = s->priv_data;
    int next = END_NOT_FOUND;
    int i = 0;

    s->pict_type = AV_PICTURE_TYPE_NONE;

    *poutbuf_size = 0;
    if (buf_size == 0)
        return 0;

    if (!ppc->pc.frame_start_found) {
        uint64_t state64 = ppc->pc.state64;
        for (; i < buf_size; i++) {
            state64 = (state64 << 8) | buf[i];
            if (state64 == PNGSIG || state64 == MNGSIG) {
                i++;
                ppc->pc.frame_start_found = 1;
                break;
            }
        }
        ppc->pc.state64 = state64;
    } else if (ppc->remaining_size) {
        i = FFMIN(ppc->remaining_size, buf_size);
        ppc->remaining_size -= i;
        if (ppc->remaining_size)
            goto flush;
        if (ppc->chunk_pos == -1) {
            next = i;
            goto flush;
        }
    }

    for (; ppc->pc.frame_start_found && i < buf_size; i++) {
        ppc->pc.state = (ppc->pc.state << 8) | buf[i];
        if (ppc->chunk_pos == 3) {
            ppc->chunk_length = ppc->pc.state;
            if (ppc->chunk_length > 0x7fffffff) {
                ppc->chunk_pos = ppc->pc.frame_start_found = 0;
                goto flush;
            }
            ppc->chunk_length += 4;
        } else if (ppc->chunk_pos == 7) {
            if (ppc->chunk_length >= buf_size - i)
                ppc->remaining_size = ppc->chunk_length - buf_size + i + 1;
            if (ppc->pc.state == MKBETAG('I', 'E', 'N', 'D')) {
                if (ppc->remaining_size)
                    ppc->chunk_pos = -1;
                else
                    next = ppc->chunk_length + i + 1;
                break;
            } else {
                ppc->chunk_pos = 0;
                if (ppc->remaining_size)
                    break;
                else
                    i += ppc->chunk_length;
                continue;
            }
        }
        ppc->chunk_pos++;
    }

flush:
    if (ff_combine_frame(&ppc->pc, next, &buf, &buf_size) < 0)
        return buf_size;

    ppc->chunk_pos = ppc->pc.frame_start_found = 0;

    *poutbuf      = buf;
    *poutbuf_size = buf_size;
    return next;
}

AVCodecParser ff_png_parser = {
    .codec_ids      = { AV_CODEC_ID_PNG },
    .priv_data_size = sizeof(PNGParseContext),
    .parser_parse   = png_parse,
    .parser_close   = ff_parse_close,
};
