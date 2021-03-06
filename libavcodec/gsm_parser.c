/*
 * Copyright (c) 2012  Justin Ruggles
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
 * GSM audio parser
 *
 * Splits packets into individual blocks.
 */

#include "libavutil/avassert.h"
#include "parser.h"
#include "gsm.h"

typedef struct GSMParseContext {
    ParseContext pc;
    int block_size;
    int duration;
    int remaining;
} GSMParseContext;

static int gsm_parse(AVCodecParserContext *s1, AVCodecContext *avctx,
                     const uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size)
{
    GSMParseContext *s = s1->priv_data;
    ParseContext *pc = &s->pc;
    int next;

    if (!s->block_size) {
        switch (avctx->codec_id) {
        case AV_CODEC_ID_GSM:
            s->block_size = GSM_BLOCK_SIZE;
            s->duration   = GSM_FRAME_SIZE;
            break;
        case AV_CODEC_ID_GSM_MS:
            s->block_size = avctx->block_align ? avctx->block_align
                                               : GSM_MS_BLOCK_SIZE;
            s->duration   = GSM_FRAME_SIZE * 2;
            break;
        default:
            av_assert0(0);
        }
    }

    if (!s->remaining)
        s->remaining = s->block_size;
    if (s->remaining <= buf_size) {
        next = s->remaining;
        s->remaining = 0;
    } else {
        next = END_NOT_FOUND;
        s->remaining -= buf_size;
    }

    if (ff_combine_frame(pc, next, &buf, &buf_size) < 0 || !buf_size) {
        *poutbuf      = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }

    s1->duration = s->duration;

    *poutbuf      = buf;
    *poutbuf_size = buf_size;
    return next;
}

const AVCodecParser ff_gsm_parser = {
    .codec_ids      = { AV_CODEC_ID_GSM, AV_CODEC_ID_GSM_MS },
    .priv_data_size = sizeof(GSMParseContext),
    .parser_parse   = gsm_parse,
    .parser_close   = ff_parse_close,
};
