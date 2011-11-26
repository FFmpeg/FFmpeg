/*
 * Copyright (c) 2011  Justin Ruggles
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * ADX audio parser
 *
 * Reads header to extradata and splits packets into individual blocks.
 */

#include "libavutil/intreadwrite.h"
#include "parser.h"
#include "adx.h"

typedef struct ADXParseContext {
    ParseContext pc;
    int header_size;
    int block_size;
    int buf_pos;
} ADXParseContext;

#define MIN_HEADER_SIZE 24

static int adx_parse(AVCodecParserContext *s1,
                           AVCodecContext *avctx,
                           const uint8_t **poutbuf, int *poutbuf_size,
                           const uint8_t *buf, int buf_size)
{
    ADXParseContext *s = s1->priv_data;
    ParseContext *pc = &s->pc;
    int next = END_NOT_FOUND;

    if (!avctx->extradata_size) {
        int ret;

        ff_combine_frame(pc, END_NOT_FOUND, &buf, &buf_size);

        if (!s->header_size && pc->index >= MIN_HEADER_SIZE) {
            if (ret = avpriv_adx_decode_header(avctx, pc->buffer, pc->index,
                                               &s->header_size, NULL))
                return AVERROR_INVALIDDATA;
            s->block_size = BLOCK_SIZE * avctx->channels;
        }
        if (s->header_size && s->header_size <= pc->index) {
            avctx->extradata = av_mallocz(s->header_size + FF_INPUT_BUFFER_PADDING_SIZE);
            if (!avctx->extradata)
                return AVERROR(ENOMEM);
            avctx->extradata_size = s->header_size;
            memcpy(avctx->extradata, pc->buffer, s->header_size);
            memmove(pc->buffer, pc->buffer + s->header_size, s->header_size);
            pc->index -= s->header_size;
        }
        *poutbuf      = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }

    if (pc->index - s->buf_pos >= s->block_size) {
        *poutbuf      = &pc->buffer[s->buf_pos];
        *poutbuf_size = s->block_size;
        s->buf_pos   += s->block_size;
        return 0;
    }
    if (pc->index && s->buf_pos) {
        memmove(pc->buffer, &pc->buffer[s->buf_pos], pc->index - s->buf_pos);
        pc->index -= s->buf_pos;
        s->buf_pos = 0;
    }
    if (buf_size + pc->index >= s->block_size)
        next = s->block_size - pc->index;

    if (ff_combine_frame(pc, next, &buf, &buf_size) < 0 || !buf_size) {
        *poutbuf      = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }
    *poutbuf = buf;
    *poutbuf_size = buf_size;
    return next;
}

AVCodecParser ff_adx_parser = {
    .codec_ids      = { CODEC_ID_ADPCM_ADX },
    .priv_data_size = sizeof(ADXParseContext),
    .parser_parse   = adx_parse,
    .parser_close   = ff_parse_close,
};
