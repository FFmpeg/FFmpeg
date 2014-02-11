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
#include "mpegaudiodecheader.h"
#include "libavutil/common.h"


typedef struct MpegAudioParseContext {
    ParseContext pc;
    int frame_size;
    uint32_t header;
    int header_count;
    int no_bitrate;
} MpegAudioParseContext;

#define MPA_HEADER_SIZE 4

/* header + layer + bitrate + freq + lsf/mpeg25 */
#define SAME_HEADER_MASK \
   (0xffe00000 | (3 << 17) | (3 << 10) | (3 << 19))

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
            state = 0;

            if(!s->frame_size){
                next= i;
                break;
            }
        }else{
            while(i<buf_size){
                int ret, sr, channels, bit_rate, frame_size;
                enum AVCodecID codec_id;

                state= (state<<8) + buf[i++];

                ret = avpriv_mpa_decode_header2(state, &sr, &channels, &frame_size, &bit_rate, &codec_id);
                if (ret < 4) {
                    if (i > 4)
                        s->header_count = -2;
                } else {
                    if((state&SAME_HEADER_MASK) != (s->header&SAME_HEADER_MASK) && s->header)
                        s->header_count= -3;
                    s->header= state;
                    s->header_count++;
                    s->frame_size = ret-4;

                    if (s->header_count > 0 + (avctx->codec_id != AV_CODEC_ID_NONE && avctx->codec_id != codec_id)) {
                        avctx->sample_rate= sr;
                        avctx->channels   = channels;
                        s1->duration      = frame_size;
                        avctx->codec_id   = codec_id;
                        if (s->no_bitrate || !avctx->bit_rate) {
                            s->no_bitrate = 1;
                            avctx->bit_rate += (bit_rate - avctx->bit_rate) / s->header_count;
                        }
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


AVCodecParser ff_mpegaudio_parser = {
    .codec_ids      = { AV_CODEC_ID_MP1, AV_CODEC_ID_MP2, AV_CODEC_ID_MP3 },
    .priv_data_size = sizeof(MpegAudioParseContext),
    .parser_parse   = mpegaudio_parse,
    .parser_close   = ff_parse_close,
};
