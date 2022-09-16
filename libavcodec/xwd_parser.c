/*
 * XWD parser
 * Copyright (c) 2022 Paul B Mahol
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
 * XWD parser
 **/

#include "libavutil/intreadwrite.h"
#include "parser.h"
#include "xwd.h"

typedef struct XWDParseContext {
    ParseContext  pc;
    int           left;
    int           idx;
    uint8_t       hdr[XWD_HEADER_SIZE];
} XWDParseContext;

static int xwd_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                     const uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size)
{
    XWDParseContext *t = s->priv_data;
    ParseContext *pc   = &t->pc;
    int next           = END_NOT_FOUND;

    s->pict_type = AV_PICTURE_TYPE_NONE;

    *poutbuf      = NULL;
    *poutbuf_size = 0;

    if (s->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        next = buf_size;
    } else {
        for (int i = 0; i < buf_size; i++) {
            if (t->left > 0) {
                t->left--;
                if (t->left == 0) {
                    next = i;
                    break;
                }
                continue;
            }

            if (t->idx >= 100) {
                t->idx = 99;
                memmove(&t->hdr[0], &t->hdr[1], XWD_HEADER_SIZE-1);
            }

            t->hdr[t->idx++] = buf[i];

            if (t->idx >= 100 && AV_RB32(t->hdr + 4) == XWD_VERSION) {
                uint32_t header_size  = AV_RB32(t->hdr + 0);
                uint32_t height       = AV_RB32(t->hdr + 20);
                uint32_t lsize        = AV_RB32(t->hdr + 48);
                uint32_t ncolors      = AV_RB32(t->hdr + 76);
                uint32_t size         = header_size + ncolors * XWD_CMAP_SIZE + height * lsize;
                pc->frame_start_found = 1;
                t->left               = size - XWD_HEADER_SIZE + 1;
                t->idx                = 0;
                memset(t->hdr, 0, sizeof(t->hdr));
            }
        }

        if (ff_combine_frame(pc, next, &buf, &buf_size) < 0)
            return buf_size;
    }

    *poutbuf      = buf;
    *poutbuf_size = buf_size;

    s->pict_type = AV_PICTURE_TYPE_I;
    s->key_frame = 1;
    s->duration  = 1;

    return next;
}

const AVCodecParser ff_xwd_parser = {
    .codec_ids      = { AV_CODEC_ID_XWD },
    .priv_data_size = sizeof(XWDParseContext),
    .parser_parse   = xwd_parse,
    .parser_close   = ff_parse_close,
};
