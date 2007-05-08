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
    const uint8_t *buf_ptr;
    int len, sample_rate, bit_rate, channels, samples;

    *poutbuf = NULL;
    *poutbuf_size = 0;

    buf_ptr = buf;
    while (buf_size > 0) {
        len = s->inbuf_ptr - s->inbuf;
        if (s->frame_size == 0) {
            /* no header seen : find one. We need at least s->header_size
               bytes to parse it */
            len = FFMIN(s->header_size - len, buf_size);

            memcpy(s->inbuf_ptr, buf_ptr, len);
            buf_ptr += len;
            s->inbuf_ptr += len;
            buf_size -= len;
            if ((s->inbuf_ptr - s->inbuf) == s->header_size) {
                len = s->sync(s->inbuf, &channels, &sample_rate, &bit_rate,
                              &samples);
                if (len == 0) {
                    /* no sync found : move by one byte (inefficient, but simple!) */
                    memmove(s->inbuf, s->inbuf + 1, s->header_size - 1);
                    s->inbuf_ptr--;
                } else {
                    s->frame_size = len;
                    /* update codec info */
                    avctx->sample_rate = sample_rate;
                    /* set channels,except if the user explicitly requests 1 or 2 channels, XXX/FIXME this is a bit ugly */
                    if(avctx->codec_id == CODEC_ID_AC3){
                        if(avctx->channels!=1 && avctx->channels!=2){
                            avctx->channels = channels;
                        }
                    } else {
                        avctx->channels = channels;
                    }
                    avctx->bit_rate = bit_rate;
                    avctx->frame_size = samples;
                }
            }
        } else {
            len = FFMIN(s->frame_size - len, buf_size);

            memcpy(s->inbuf_ptr, buf_ptr, len);
            buf_ptr += len;
            s->inbuf_ptr += len;
            buf_size -= len;

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
