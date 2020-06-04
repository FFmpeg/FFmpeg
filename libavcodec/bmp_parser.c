/*
 * BMP parser
 * Copyright (c) 2012 Paul B Mahol
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
 * BMP parser
 */

#include "libavutil/bswap.h"
#include "libavutil/common.h"

#include "parser.h"

typedef struct BMPParseContext {
    ParseContext pc;
    uint32_t fsize;
    uint32_t remaining_size;
} BMPParseContext;

static int bmp_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                     const uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size)
{
    BMPParseContext *bpc = s->priv_data;
    uint64_t state = bpc->pc.state64;
    int next = END_NOT_FOUND;
    int i = 0;

    *poutbuf_size = 0;
    *poutbuf = NULL;

restart:
    if (bpc->pc.frame_start_found <= 2+4+4) {
        for (; i < buf_size; i++) {
            state = (state << 8) | buf[i];
            if (bpc->pc.frame_start_found == 0) {
                if ((state >> 48) == (('B' << 8) | 'M')) {
                    bpc->fsize = av_bswap32(state >> 16);
                    if (bpc->fsize > 17)
                        bpc->pc.frame_start_found = 1;
                }
            } else if (bpc->pc.frame_start_found == 2+4+4) {
//                 unsigned hsize = av_bswap32(state>>32);
                unsigned ihsize = av_bswap32(state);
                if (ihsize < 12 || ihsize > 200) {
                    bpc->pc.frame_start_found = 0;
                    continue;
                }
                bpc->pc.frame_start_found++;
                bpc->remaining_size = bpc->fsize + i - 17;

                if (bpc->pc.index + i > 17) {
                    next = i - 17;
                    state = 0;
                    break;
                } else {
                    bpc->pc.state64 = 0;
                    goto restart;
                }
            } else if (bpc->pc.frame_start_found)
                bpc->pc.frame_start_found++;
        }
        bpc->pc.state64 = state;
    } else {
        if (bpc->remaining_size) {
            i = FFMIN(bpc->remaining_size, buf_size);
            bpc->remaining_size -= i;
            if (bpc->remaining_size)
                goto flush;

            bpc->pc.frame_start_found = 0;
            goto restart;
        }
    }

flush:
    if (ff_combine_frame(&bpc->pc, next, &buf, &buf_size) < 0)
        return buf_size;

    if (next != END_NOT_FOUND && next < 0)
        bpc->pc.frame_start_found = FFMAX(bpc->pc.frame_start_found - i - 1, 0);
    else
        bpc->pc.frame_start_found = 0;

    *poutbuf      = buf;
    *poutbuf_size = buf_size;
    return next;
}

AVCodecParser ff_bmp_parser = {
    .codec_ids      = { AV_CODEC_ID_BMP },
    .priv_data_size = sizeof(BMPParseContext),
    .parser_parse   = bmp_parse,
    .parser_close   = ff_parse_close,
};
