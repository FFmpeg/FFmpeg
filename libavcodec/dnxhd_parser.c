/*
 * DNxHD/VC-3 parser
 * Copyright (c) 2008 Baptiste Coudurier <baptiste.coudurier@free.fr>
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
 * DNxHD/VC-3 parser
 */

#include "parser.h"

#define DNXHD_HEADER_PREFIX 0x000002800100

typedef struct {
    ParseContext pc;
    int interlaced;
    int cur_field; /* first field is 0, second is 1 */
} DNXHDParserContext;

static int dnxhd_find_frame_end(DNXHDParserContext *dctx,
                                const uint8_t *buf, int buf_size)
{
    ParseContext *pc = &dctx->pc;
    uint64_t state = pc->state64;
    int pic_found = pc->frame_start_found;
    int i = 0;
    int interlaced = dctx->interlaced;
    int cur_field = dctx->cur_field;

    if (!pic_found) {
        for (i = 0; i < buf_size; i++) {
            state = (state << 8) | buf[i];
            if ((state & 0xffffffffff00LL) == DNXHD_HEADER_PREFIX) {
                i++;
                pic_found = 1;
                interlaced = (state&2)>>1; /* byte following the 5-byte header prefix */
                cur_field = state&1;
                break;
            }
        }
    }

    if (pic_found) {
        if (!buf_size) /* EOF considered as end of frame */
            return 0;
        for (; i < buf_size; i++) {
            state = (state << 8) | buf[i];
            if ((state & 0xffffffffff00LL) == DNXHD_HEADER_PREFIX) {
                if (!interlaced || dctx->cur_field) {
                    pc->frame_start_found = 0;
                    pc->state64 = -1;
                    dctx->interlaced = interlaced;
                    dctx->cur_field = 0;
                    return i - 5;
                } else {
                    /* continue, to get the second field */
                    dctx->interlaced = interlaced = (state&2)>>1;
                    dctx->cur_field = cur_field = state&1;
                }
            }
        }
    }
    pc->frame_start_found = pic_found;
    pc->state64 = state;
    dctx->interlaced = interlaced;
    dctx->cur_field = cur_field;
    return END_NOT_FOUND;
}

static int dnxhd_parse(AVCodecParserContext *s,
                       AVCodecContext *avctx,
                       const uint8_t **poutbuf, int *poutbuf_size,
                       const uint8_t *buf, int buf_size)
{
    DNXHDParserContext *dctx = s->priv_data;
    ParseContext *pc = &dctx->pc;
    int next;

    if (s->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        next = buf_size;
    } else {
        next = dnxhd_find_frame_end(dctx, buf, buf_size);
        if (ff_combine_frame(pc, next, &buf, &buf_size) < 0) {
            *poutbuf      = NULL;
            *poutbuf_size = 0;
            return buf_size;
        }
    }
    *poutbuf      = buf;
    *poutbuf_size = buf_size;
    return next;
}

AVCodecParser ff_dnxhd_parser = {
    .codec_ids      = { AV_CODEC_ID_DNXHD },
    .priv_data_size = sizeof(DNXHDParserContext),
    .parser_parse   = dnxhd_parse,
    .parser_close   = ff_parse_close,
};
