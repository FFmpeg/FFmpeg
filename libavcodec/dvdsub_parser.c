/*
 * DVD subtitle decoding for ffmpeg
 * Copyright (c) 2005 Fabrice Bellard
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

#include "libavutil/intreadwrite.h"
#include "avcodec.h"

/* parser definition */
typedef struct DVDSubParseContext {
    uint8_t *packet;
    int packet_len;
    int packet_index;
} DVDSubParseContext;

static av_cold int dvdsub_parse_init(AVCodecParserContext *s)
{
    return 0;
}

static int dvdsub_parse(AVCodecParserContext *s,
                        AVCodecContext *avctx,
                        const uint8_t **poutbuf, int *poutbuf_size,
                        const uint8_t *buf, int buf_size)
{
    DVDSubParseContext *pc = s->priv_data;

    if (pc->packet_index == 0) {
        if (buf_size < 2)
            return 0;
        pc->packet_len = AV_RB16(buf);
        if (pc->packet_len == 0) /* HD-DVD subpicture packet */
            pc->packet_len = AV_RB32(buf+2);
        av_freep(&pc->packet);
        pc->packet = av_malloc(pc->packet_len);
    }
    if (pc->packet) {
        if (pc->packet_index + buf_size <= pc->packet_len) {
            memcpy(pc->packet + pc->packet_index, buf, buf_size);
            pc->packet_index += buf_size;
            if (pc->packet_index >= pc->packet_len) {
                *poutbuf = pc->packet;
                *poutbuf_size = pc->packet_len;
                pc->packet_index = 0;
                return buf_size;
            }
        } else {
            /* erroneous size */
            pc->packet_index = 0;
        }
    }
    *poutbuf = NULL;
    *poutbuf_size = 0;
    return buf_size;
}

static av_cold void dvdsub_parse_close(AVCodecParserContext *s)
{
    DVDSubParseContext *pc = s->priv_data;
    av_freep(&pc->packet);
}

AVCodecParser dvdsub_parser = {
    { CODEC_ID_DVD_SUBTITLE },
    sizeof(DVDSubParseContext),
    dvdsub_parse_init,
    dvdsub_parse,
    dvdsub_parse_close,
};
