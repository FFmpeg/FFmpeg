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
 * XMA2 audio parser
 */

#include "parser.h"

typedef struct XMAParserContext{
    int skip_packets;
} XMAParserContext;

static int xma_parse(AVCodecParserContext *s1, AVCodecContext *avctx,
                     const uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size)
{
    XMAParserContext *s = s1->priv_data;

    if (buf_size % 2048 == 0) {
        int duration = 0, packet, nb_packets = buf_size / 2048;

        for (packet = 0; packet < nb_packets; packet++) {
            if (s->skip_packets == 0) {
                duration += buf[packet * 2048] * 128;
                s->skip_packets = buf[packet * 2048 + 3] + 1;
            }
            s->skip_packets--;
        }

        s1->duration = duration;
        s1->key_frame = !!duration;
    }

    /* always return the full packet. this parser isn't doing any splitting or
       combining, only packet analysis */
    *poutbuf      = buf;
    *poutbuf_size = buf_size;
    return buf_size;
}

const AVCodecParser ff_xma_parser = {
    .codec_ids      = { AV_CODEC_ID_XMA2 },
    .priv_data_size = sizeof(XMAParserContext),
    .parser_parse   = xma_parse,
};
