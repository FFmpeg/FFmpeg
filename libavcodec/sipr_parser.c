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
 * Sipr audio parser
 */

#include "parser.h"

typedef struct SiprParserContext{
    ParseContext pc;
} SiprParserContext;

static int sipr_split(AVCodecContext *avctx, const uint8_t *buf, int buf_size)
{
    int next;

    switch (avctx->block_align) {
    case 20:
    case 19:
    case 29:
    case 37: next = avctx->block_align; break;
    default:
        if      (avctx->bit_rate > 12200) next = 20;
        else if (avctx->bit_rate > 7500 ) next = 19;
        else if (avctx->bit_rate > 5750 ) next = 29;
        else                              next = 37;
    }

    return FFMIN(next, buf_size);
}

static int sipr_parse(AVCodecParserContext *s1, AVCodecContext *avctx,
                      const uint8_t **poutbuf, int *poutbuf_size,
                      const uint8_t *buf, int buf_size)
{
    SiprParserContext *s = s1->priv_data;
    ParseContext *pc = &s->pc;
    int next;

    next = sipr_split(avctx, buf, buf_size);
    if (ff_combine_frame(pc, next, &buf, &buf_size) < 0) {
        *poutbuf = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }

    *poutbuf      = buf;
    *poutbuf_size = buf_size;
    return next;
}

const AVCodecParser ff_sipr_parser = {
    .codec_ids      = { AV_CODEC_ID_SIPR },
    .priv_data_size = sizeof(SiprParserContext),
    .parser_parse   = sipr_parse,
    .parser_close   = ff_parse_close,
};
