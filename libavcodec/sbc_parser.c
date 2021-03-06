/*
 * SBC parser
 *
 * Copyright (C) 2017  Aurelien Jacobs <aurel@gnuage.org>
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

#include "sbc.h"
#include "parser.h"

typedef struct SBCParseContext {
    ParseContext pc;
    uint8_t header[3];
    int header_size;
    int buffered_size;
} SBCParseContext;

static int sbc_parse_header(AVCodecParserContext *s, AVCodecContext *avctx,
                            const uint8_t *data, size_t len)
{
    static const int sample_rates[4] = { 16000, 32000, 44100, 48000 };
    int sr, blocks, mode, subbands, bitpool, channels, joint;
    int length;

    if (len < 3)
        return -1;

    if (data[0] == MSBC_SYNCWORD && data[1] == 0 && data[2] == 0) {
        avctx->channels = 1;
        avctx->sample_rate = 16000;
        avctx->frame_size = 120;
        s->duration = avctx->frame_size;
        return 57;
    }

    if (data[0] != SBC_SYNCWORD)
        return -2;

    sr       =   (data[1] >> 6) & 0x03;
    blocks   = (((data[1] >> 4) & 0x03) + 1) << 2;
    mode     =   (data[1] >> 2) & 0x03;
    subbands = (((data[1] >> 0) & 0x01) + 1) << 2;
    bitpool  = data[2];

    channels = mode == SBC_MODE_MONO ? 1 : 2;
    joint    = mode == SBC_MODE_JOINT_STEREO;

    length = 4 + (subbands * channels) / 2
             + ((((mode == SBC_MODE_DUAL_CHANNEL) + 1) * blocks * bitpool
                 + (joint * subbands)) + 7) / 8;

    avctx->channels = channels;
    avctx->sample_rate = sample_rates[sr];
    avctx->frame_size = subbands * blocks;
    s->duration = avctx->frame_size;
    return length;
}

static int sbc_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                     const uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size)
{
    SBCParseContext *pc = s->priv_data;
    int next;

    if (s->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        next = buf_size;
    } else {
        if (pc->header_size) {
            memcpy(pc->header + pc->header_size, buf,
                   sizeof(pc->header) - pc->header_size);
            next = sbc_parse_header(s, avctx, pc->header, sizeof(pc->header))
                 - pc->buffered_size;
            pc->header_size = 0;
        } else {
            next = sbc_parse_header(s, avctx, buf, buf_size);
            if (next >= buf_size)
                next = -1;
        }

        if (next < 0) {
            pc->header_size = FFMIN(sizeof(pc->header), buf_size);
            memcpy(pc->header, buf, pc->header_size);
            pc->buffered_size = buf_size;
            next = END_NOT_FOUND;
        }

        if (ff_combine_frame(&pc->pc, next, &buf, &buf_size) < 0) {
            *poutbuf      = NULL;
            *poutbuf_size = 0;
            return buf_size;
        }
    }

    *poutbuf      = buf;
    *poutbuf_size = buf_size;
    return next;
}

const AVCodecParser ff_sbc_parser = {
    .codec_ids      = { AV_CODEC_ID_SBC },
    .priv_data_size = sizeof(SBCParseContext),
    .parser_parse   = sbc_parse,
    .parser_close   = ff_parse_close,
};
