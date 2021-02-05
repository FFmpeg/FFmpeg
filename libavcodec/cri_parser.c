/*
 * CRI parser
 * Copyright (c) 2021 Paul B Mahol
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
 * CRI parser
 */

#include "libavutil/bswap.h"
#include "libavutil/common.h"

#include "parser.h"

typedef struct CRIParser {
    ParseContext pc;
    int count;
    int chunk;
    int read_bytes;
    int skip_bytes;
} CRIParser;

#define KEY (((uint64_t)'\1' << 56) | ((uint64_t)'\0' << 48) | \
             ((uint64_t)'\0' << 40) | ((uint64_t)'\0' << 32) | \
             ((uint64_t)'\4' << 24) | ((uint64_t)'\0' << 16) | \
             ((uint64_t)'\0' <<  8) | ((uint64_t)'\0' <<  0))

static int cri_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                     const uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size)
{
    CRIParser *bpc = s->priv_data;
    uint64_t state = bpc->pc.state64;
    int next = END_NOT_FOUND, i = 0;

    s->pict_type = AV_PICTURE_TYPE_I;
    s->key_frame = 1;
    s->duration  = 1;

    *poutbuf_size = 0;
    *poutbuf = NULL;

    for (; i < buf_size; i++) {
        state = (state << 8) | buf[i];
        bpc->read_bytes++;

        if (bpc->skip_bytes > 0) {
            bpc->skip_bytes--;
            if (bpc->skip_bytes == 0)
                bpc->read_bytes = 0;
        } else {
            if (state != KEY)
                continue;
        }

        if (bpc->skip_bytes == 0 && bpc->read_bytes >= 8) {
            bpc->skip_bytes = av_bswap32(state & 0xFFFFFFFF);
            bpc->chunk = state >> 32;
            bpc->read_bytes = 0;
            bpc->count++;
        }

        if (bpc->chunk == 0x01000000 && bpc->skip_bytes == 4 &&
            bpc->read_bytes == 0 && bpc->count > 1) {
            next = i - 7;
            break;
        }
    }

    bpc->pc.state64 = state;
    if (ff_combine_frame(&bpc->pc, next, &buf, &buf_size) < 0) {
        *poutbuf = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }

    *poutbuf      = buf;
    *poutbuf_size = buf_size;

    return next;
}

AVCodecParser ff_cri_parser = {
    .codec_ids      = { AV_CODEC_ID_CRI },
    .priv_data_size = sizeof(CRIParser),
    .parser_parse   = cri_parse,
    .parser_close   = ff_parse_close,
};
