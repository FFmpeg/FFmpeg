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
#include "dnxhddata.h"

typedef struct {
    ParseContext pc;
    int cur_byte;
    int remaining;
    int w, h;
} DNXHDParserContext;

static int dnxhd_get_hr_frame_size(int cid, int w, int h)
{
    int result, i = ff_dnxhd_get_cid_table(cid);

    if (i < 0)
        return i;

    result = ((h + 15) / 16) * ((w + 15) / 16) * ff_dnxhd_cid_table[i].packet_scale.num / ff_dnxhd_cid_table[i].packet_scale.den;
    result = (result + 2048) / 4096 * 4096;

    return FFMAX(result, 8192);
}

static int dnxhd_find_frame_end(DNXHDParserContext *dctx,
                                const uint8_t *buf, int buf_size)
{
    ParseContext *pc = &dctx->pc;
    uint64_t state = pc->state64;
    int pic_found = pc->frame_start_found;
    int i = 0;

    if (!pic_found) {
        for (i = 0; i < buf_size; i++) {
            state = (state << 8) | buf[i];
            if (ff_dnxhd_check_header_prefix(state & 0xffffffffff00LL) != 0) {
                i++;
                pic_found = 1;
                dctx->cur_byte = 0;
                dctx->remaining = 0;
                break;
            }
        }
    }

    if (pic_found && !dctx->remaining) {
        if (!buf_size) /* EOF considered as end of frame */
            return 0;
        for (; i < buf_size; i++) {
            dctx->cur_byte++;
            state = (state << 8) | buf[i];

            if (dctx->cur_byte == 24) {
                dctx->h = (state >> 32) & 0xFFFF;
            } else if (dctx->cur_byte == 26) {
                dctx->w = (state >> 32) & 0xFFFF;
            } else if (dctx->cur_byte == 42) {
                int cid = (state >> 32) & 0xFFFFFFFF;
                int remaining;

                if (cid <= 0)
                    continue;

                remaining = avpriv_dnxhd_get_frame_size(cid);
                if (remaining <= 0) {
                    remaining = dnxhd_get_hr_frame_size(cid, dctx->w, dctx->h);
                    if (remaining <= 0)
                        continue;
                }
                dctx->remaining = remaining;
                if (buf_size - i + 47 >= dctx->remaining) {
                    int remaining = dctx->remaining;

                    pc->frame_start_found = 0;
                    pc->state64 = -1;
                    dctx->cur_byte = 0;
                    dctx->remaining = 0;
                    return remaining;
                } else {
                    dctx->remaining -= buf_size;
                }
            }
        }
    } else if (pic_found) {
        if (dctx->remaining > buf_size) {
            dctx->remaining -= buf_size;
        } else {
            int remaining = dctx->remaining;

            pc->frame_start_found = 0;
            pc->state64 = -1;
            dctx->cur_byte = 0;
            dctx->remaining = 0;
            return remaining;
        }
    }
    pc->frame_start_found = pic_found;
    pc->state64 = state;
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
