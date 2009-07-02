/*
 * MPEG Audio parser
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

#include "parser.h"
#include "mpegaudio.h"
#include "mpegaudiodecheader.h"


typedef struct MpegAudioParseContext {
    ParseContext pc;
    int frame_size;
    uint32_t header;
    int header_count;
} MpegAudioParseContext;

#define MPA_HEADER_SIZE 4

/* header + layer + bitrate + freq + lsf/mpeg25 */
#undef SAME_HEADER_MASK /* mpegaudio.h defines different version */
#define SAME_HEADER_MASK \
   (0xffe00000 | (3 << 17) | (3 << 10) | (3 << 19))

/* useful helper to get mpeg audio stream infos. Return -1 if error in
   header, otherwise the coded frame size in bytes */
int ff_mpa_decode_header(AVCodecContext *avctx, uint32_t head, int *sample_rate, int *channels, int *frame_size, int *bit_rate)
{
    MPADecodeHeader s1, *s = &s1;

    if (ff_mpa_check_header(head) != 0)
        return -1;

    if (ff_mpegaudio_decode_header(s, head) != 0) {
        return -1;
    }

    switch(s->layer) {
    case 1:
        avctx->codec_id = CODEC_ID_MP1;
        *frame_size = 384;
        break;
    case 2:
        avctx->codec_id = CODEC_ID_MP2;
        *frame_size = 1152;
        break;
    default:
    case 3:
        avctx->codec_id = CODEC_ID_MP3;
        if (s->lsf)
            *frame_size = 576;
        else
            *frame_size = 1152;
        break;
    }

    *sample_rate = s->sample_rate;
    *channels = s->nb_channels;
    *bit_rate = s->bit_rate;
    avctx->sub_id = s->layer;
    return s->frame_size;
}

static int mpegaudio_parse(AVCodecParserContext *s1,
                           AVCodecContext *avctx,
                           const uint8_t **poutbuf, int *poutbuf_size,
                           const uint8_t *buf, int buf_size)
{
    MpegAudioParseContext *s = s1->priv_data;
    ParseContext *pc = &s->pc;
    uint32_t state= pc->state;
    int i;
    int next= END_NOT_FOUND;

    for(i=0; i<buf_size; ){
        if(s->frame_size){
            int inc= FFMIN(buf_size - i, s->frame_size);
            i += inc;
            s->frame_size -= inc;

            if(!s->frame_size){
                next= i;
                break;
            }
        }else{
            while(i<buf_size){
                int ret, sr, channels, bit_rate, frame_size;

                state= (state<<8) + buf[i++];

                ret = ff_mpa_decode_header(avctx, state, &sr, &channels, &frame_size, &bit_rate);
                if (ret < 4) {
                    s->header_count= -2;
                } else {
                    if((state&SAME_HEADER_MASK) != (s->header&SAME_HEADER_MASK) && s->header)
                        s->header_count= -3;
                    s->header= state;
                    s->header_count++;
                    s->frame_size = ret-4;

                    if(s->header_count > 1){
                        avctx->sample_rate= sr;
                        avctx->channels   = channels;
                        avctx->frame_size = frame_size;
                        avctx->bit_rate   = bit_rate;
                    }
                    break;
                }
            }
        }
    }

    pc->state= state;
    if (ff_combine_frame(pc, next, &buf, &buf_size) < 0) {
        *poutbuf = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }

    *poutbuf = buf;
    *poutbuf_size = buf_size;
    return next;
}


AVCodecParser mpegaudio_parser = {
    { CODEC_ID_MP1, CODEC_ID_MP2, CODEC_ID_MP3 },
    sizeof(MpegAudioParseContext),
    NULL,
    mpegaudio_parse,
    ff_parse_close,
};
