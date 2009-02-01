/*
 * H.26L/H.264/AVC/JVT/14496-10/... parser
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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
 * @file libavcodec/h264_parser.c
 * H.264 / AVC / MPEG4 part10 parser.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "parser.h"
#include "h264_parser.h"

#include <assert.h>


int ff_h264_find_frame_end(H264Context *h, const uint8_t *buf, int buf_size)
{
    int i;
    uint32_t state;
    ParseContext *pc = &(h->s.parse_context);
//printf("first %02X%02X%02X%02X\n", buf[0], buf[1],buf[2],buf[3]);
//    mb_addr= pc->mb_addr - 1;
    state= pc->state;
    if(state>13)
        state= 7;

    for(i=0; i<buf_size; i++){
        if(state==7){
#if HAVE_FAST_UNALIGNED
        /* we check i<buf_size instead of i+3/7 because its simpler
         * and there should be FF_INPUT_BUFFER_PADDING_SIZE bytes at the end
         */
#    if HAVE_FAST_64BIT
            while(i<buf_size && !((~*(uint64_t*)(buf+i) & (*(uint64_t*)(buf+i) - 0x0101010101010101ULL)) & 0x8080808080808080ULL))
                i+=8;
#    else
            while(i<buf_size && !((~*(uint32_t*)(buf+i) & (*(uint32_t*)(buf+i) - 0x01010101U)) & 0x80808080U))
                i+=4;
#    endif
#endif
            for(; i<buf_size; i++){
                if(!buf[i]){
                    state=2;
                    break;
                }
            }
        }else if(state<=2){
            if(buf[i]==1)   state^= 5; //2->7, 1->4, 0->5
            else if(buf[i]) state = 7;
            else            state>>=1; //2->1, 1->0, 0->0
        }else if(state<=5){
            int v= buf[i] & 0x1F;
            if(v==7 || v==8 || v==9){
                if(pc->frame_start_found){
                    i++;
                    goto found;
                }
            }else if(v==1 || v==2 || v==5){
                if(pc->frame_start_found){
                    state+=8;
                    continue;
                }else
                    pc->frame_start_found = 1;
            }
            state= 7;
        }else{
            if(buf[i] & 0x80)
                goto found;
            state= 7;
        }
    }
    pc->state= state;
    return END_NOT_FOUND;

found:
    pc->state=7;
    pc->frame_start_found= 0;
    return i-(state&5);
}

static int h264_parse(AVCodecParserContext *s,
                      AVCodecContext *avctx,
                      const uint8_t **poutbuf, int *poutbuf_size,
                      const uint8_t *buf, int buf_size)
{
    H264Context *h = s->priv_data;
    ParseContext *pc = &h->s.parse_context;
    int next;

    if(s->flags & PARSER_FLAG_COMPLETE_FRAMES){
        next= buf_size;
    }else{
        next= ff_h264_find_frame_end(h, buf, buf_size);

        if (ff_combine_frame(pc, next, &buf, &buf_size) < 0) {
            *poutbuf = NULL;
            *poutbuf_size = 0;
            return buf_size;
        }

        if(next<0 && next != END_NOT_FOUND){
            assert(pc->last_index + next >= 0 );
            ff_h264_find_frame_end(h, &pc->buffer[pc->last_index + next], -next); //update state
        }
    }

    *poutbuf = buf;
    *poutbuf_size = buf_size;
    return next;
}

static int h264_split(AVCodecContext *avctx,
                      const uint8_t *buf, int buf_size)
{
    int i;
    uint32_t state = -1;
    int has_sps= 0;

    for(i=0; i<=buf_size; i++){
        if((state&0xFFFFFF1F) == 0x107)
            has_sps=1;
/*        if((state&0xFFFFFF1F) == 0x101 || (state&0xFFFFFF1F) == 0x102 || (state&0xFFFFFF1F) == 0x105){
        }*/
        if((state&0xFFFFFF00) == 0x100 && (state&0xFFFFFF1F) != 0x107 && (state&0xFFFFFF1F) != 0x108 && (state&0xFFFFFF1F) != 0x109){
            if(has_sps){
                while(i>4 && buf[i-5]==0) i--;
                return i-4;
            }
        }
        if (i<buf_size)
            state= (state<<8) | buf[i];
    }
    return 0;
}

static void close(AVCodecParserContext *s)
{
    H264Context *h = s->priv_data;
    ParseContext *pc = &h->s.parse_context;

    av_free(pc->buffer);
}


AVCodecParser h264_parser = {
    { CODEC_ID_H264 },
    sizeof(H264Context),
    NULL,
    h264_parse,
    close,
    h264_split,
};
