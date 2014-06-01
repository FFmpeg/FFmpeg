/*
 * DPX parser
 * Copyright (c) 2013 Paul B Mahol
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
 * DPX parser
 */

#include "libavutil/bswap.h"
#include "parser.h"

typedef struct DPXParseContext {
    ParseContext pc;
    uint32_t index;
    uint32_t fsize;
    uint32_t remaining_size;
    int is_be;
} DPXParseContext;

static int dpx_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                     const uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size)
{
    DPXParseContext *d = s->priv_data;
    uint32_t state = d->pc.state;
    int next = END_NOT_FOUND;
    int i = 0;

    s->pict_type = AV_PICTURE_TYPE_I;

    *poutbuf_size = 0;
    if (buf_size == 0)
        next = 0;

    if (!d->pc.frame_start_found) {
        for (; i < buf_size; i++) {
            state = (state << 8) | buf[i];
            if (state == MKBETAG('S','D','P','X') ||
                state == MKTAG('S','D','P','X')) {
                d->pc.frame_start_found = 1;
                d->is_be = state == MKBETAG('S','D','P','X');
                d->index = 0;
                break;
            }
        }
        d->pc.state = state;
    } else {
        if (d->remaining_size) {
            i = FFMIN(d->remaining_size, buf_size);
            d->remaining_size -= i;
            if (d->remaining_size)
                goto flush;
        }
    }

    for (;d->pc.frame_start_found && i < buf_size; i++) {
        d->pc.state = (d->pc.state << 8) | buf[i];
        d->index++;
        if (d->index == 17) {
            d->fsize = d->is_be ? d->pc.state : av_bswap32(d->pc.state);
            if (d->fsize <= 1664) {
                d->pc.frame_start_found = 0;
                goto flush;
            }
            if (d->fsize > buf_size - i + 19)
                d->remaining_size = d->fsize - buf_size + i - 19;
            else
                i += d->fsize - 19;

            break;
        } else if (d->index > 17) {
            if (d->pc.state == MKBETAG('S','D','P','X') ||
                d->pc.state == MKTAG('S','D','P','X')) {
                next = i - 3;
                break;
            }
        }
    }

flush:
    if (ff_combine_frame(&d->pc, next, &buf, &buf_size) < 0)
        return buf_size;

    d->pc.frame_start_found = 0;

    *poutbuf      = buf;
    *poutbuf_size = buf_size;
    return next;
}

AVCodecParser ff_dpx_parser = {
    .codec_ids      = { AV_CODEC_ID_DPX },
    .priv_data_size = sizeof(DPXParseContext),
    .parser_parse   = dpx_parse,
    .parser_close   = ff_parse_close,
};
