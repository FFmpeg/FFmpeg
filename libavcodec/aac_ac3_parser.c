/*
 * Common AAC and AC-3 parser
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2003 Michael Niedermayer
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

#include "libavutil/common.h"
#include "parser.h"
#include "aac_ac3_parser.h"

int ff_aac_ac3_parse(AVCodecParserContext *s1,
                     AVCodecContext *avctx,
                     const uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size)
{
    AACAC3ParseContext *s = s1->priv_data;
    ParseContext *pc = &s->pc;
    int len, i;
    int new_frame_start;

get_next:
    i=END_NOT_FOUND;
    if(s->remaining_size <= buf_size){
        if(s->remaining_size && !s->need_next_header){
            i= s->remaining_size;
            s->remaining_size = 0;
        }else{ //we need a header first
            len=0;
            for(i=s->remaining_size; i<buf_size; i++){
                s->state = (s->state<<8) + buf[i];
                if((len=s->sync(s->state, s, &s->need_next_header, &new_frame_start)))
                    break;
            }
            if(len<=0){
                i=END_NOT_FOUND;
            }else{
                s->state=0;
                i-= s->header_size -1;
                s->remaining_size = len;
                if(!new_frame_start || pc->index+i<=0){
                    s->remaining_size += i;
                    goto get_next;
                }
            }
        }
    }

    if(ff_combine_frame(pc, i, &buf, &buf_size)<0){
        s->remaining_size -= FFMIN(s->remaining_size, buf_size);
        *poutbuf = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }

    *poutbuf = buf;
    *poutbuf_size = buf_size;

    /* update codec info */
    if(s->codec_id)
        avctx->codec_id = s->codec_id;

    /* Due to backwards compatible HE-AAC the sample rate, channel count,
       and total number of samples found in an AAC ADTS header are not
       reliable. Bit rate is still accurate because the total frame duration in
       seconds is still correct (as is the number of bits in the frame). */
    if (avctx->codec_id != AV_CODEC_ID_AAC) {
        avctx->sample_rate = s->sample_rate;

        /* allow downmixing to stereo (or mono for AC-3) */
        if(avctx->request_channels > 0 &&
                avctx->request_channels < s->channels &&
                (avctx->request_channels <= 2 ||
                (avctx->request_channels == 1 &&
                (avctx->codec_id == AV_CODEC_ID_AC3 ||
                 avctx->codec_id == AV_CODEC_ID_EAC3)))) {
            avctx->channels = avctx->request_channels;
        } else {
            avctx->channels = s->channels;
            avctx->channel_layout = s->channel_layout;
        }
        s1->duration = s->samples;
        avctx->audio_service_type = s->service_type;
    }

    avctx->bit_rate = s->bit_rate;

    return i;
}
