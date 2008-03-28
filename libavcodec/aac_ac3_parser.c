/*
 * Common AAC and AC3 parser
 * Copyright (c) 2003 Fabrice Bellard.
 * Copyright (c) 2003 Michael Niedermayer.
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

#include "parser.h"
#include "aac_ac3_parser.h"

int ff_aac_ac3_parse(AVCodecParserContext *s1,
                     AVCodecContext *avctx,
                     const uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size)
{
    AACAC3ParseContext *s = s1->priv_data;
    AACAC3FrameFlag frame_flag;
    const uint8_t *buf_ptr;
    int len;

    *poutbuf = NULL;
    *poutbuf_size = 0;

    buf_ptr = buf;
    while (buf_size > 0) {
        int size_needed= s->frame_size ? s->frame_size : s->header_size;
        len = s->inbuf_ptr - s->inbuf;

        if(len<size_needed){
            len = FFMIN(size_needed - len, buf_size);
            memcpy(s->inbuf_ptr, buf_ptr, len);
            buf_ptr      += len;
            s->inbuf_ptr += len;
            buf_size     -= len;
        }

        if (s->frame_size == 0) {
            if ((s->inbuf_ptr - s->inbuf) == s->header_size) {
                len = s->sync(s, &frame_flag);
                if (len == 0) {
                    /* no sync found : move by one byte (inefficient, but simple!) */
                    memmove(s->inbuf, s->inbuf + 1, s->header_size - 1);
                    s->inbuf_ptr--;
                } else {
                    s->frame_size = len;
                    /* update codec info */
                    avctx->sample_rate = s->sample_rate;
                    /* allow downmixing to stereo (or mono for AC3) */
                    if(avctx->request_channels > 0 &&
                            avctx->request_channels < s->channels &&
                            (avctx->request_channels <= 2 ||
                            (avctx->request_channels == 1 &&
                            avctx->codec_id == CODEC_ID_AC3))) {
                        avctx->channels = avctx->request_channels;
                    } else {
                        avctx->channels = s->channels;
                    }
                    avctx->bit_rate = s->bit_rate;
                    avctx->frame_size = s->samples;
                }
            }
        } else {
            if(s->inbuf_ptr - s->inbuf == s->frame_size){
                *poutbuf = s->inbuf;
                *poutbuf_size = s->frame_size;
                s->inbuf_ptr = s->inbuf;
                s->frame_size = 0;
                break;
            }
        }
    }
    return buf_ptr - buf;
}
