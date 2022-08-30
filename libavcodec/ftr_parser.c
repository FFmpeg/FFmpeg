/*
 * FTR parser
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
 * FTR parser
 */

#include "parser.h"
#include "get_bits.h"
#include "adts_header.h"
#include "adts_parser.h"
#include "mpeg4audio.h"

typedef struct FTRParseContext {
    ParseContext pc;
    int skip;
    int split;
    int frame_index;
} FTRParseContext;

static int ftr_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                     const uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size)
{
    uint8_t tmp[8 + AV_INPUT_BUFFER_PADDING_SIZE];
    FTRParseContext *ftr = s->priv_data;
    uint64_t state = ftr->pc.state64;
    int next = END_NOT_FOUND;
    GetBitContext bits;
    AACADTSHeaderInfo hdr;
    int size;

    *poutbuf_size = 0;
    *poutbuf = NULL;

    if (s->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        next = buf_size;
    } else {
        for (int i = 0; i < buf_size; i++) {
            if (ftr->skip > 0) {
                ftr->skip--;
                if (ftr->skip == 0 && ftr->split) {
                    ftr->split = 0;
                    next = i;
                    s->duration = 1024;
                    s->key_frame = 1;
                    break;
                } else if (ftr->skip > 0) {
                    continue;
                }
            }

            state = (state << 8) | buf[i];
            AV_WB64(tmp, state);
            init_get_bits(&bits, tmp + 8 - AV_AAC_ADTS_HEADER_SIZE,
                          AV_AAC_ADTS_HEADER_SIZE * 8);

            if ((size = ff_adts_header_parse(&bits, &hdr)) > 0) {
                ftr->skip = size - 6;
                ftr->frame_index += ff_mpeg4audio_channels[hdr.chan_config];
                if (ftr->frame_index >= avctx->ch_layout.nb_channels) {
                    ftr->frame_index = 0;
                    ftr->split = 1;
                }
            }
        }

        ftr->pc.state64 = state;

        if (ff_combine_frame(&ftr->pc, next, &buf, &buf_size) < 0) {
            *poutbuf = NULL;
            *poutbuf_size = 0;
            return buf_size;
        }
    }

    *poutbuf      = buf;
    *poutbuf_size = buf_size;

    return next;
}

const AVCodecParser ff_ftr_parser = {
    .codec_ids      = { AV_CODEC_ID_FTR },
    .priv_data_size = sizeof(FTRParseContext),
    .parser_parse   = ftr_parse,
    .parser_close   = ff_parse_close,
};
