/*
 * Westwood SNDx codecs
 * Copyright (c) 2005 Konstantin Shishkov
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "avcodec.h"

/**
 * @file ws-snd.c
 * Westwood SNDx codecs.
 *
 * Reference documents about VQA format and its audio codecs
 * can be found here:
 * http://www.multimedia.cx
 */

typedef struct {
} WSSNDContext;

static const char ws_adpcm_2bit[] = { -2, -1, 0, 1};
static const char ws_adpcm_4bit[] = {
    -9, -8, -6, -5, -4, -3, -2, -1,
     0,  1,  2,  3,  4,  5,  6,  8 };

#define CLIP8(a) if(a>127)a=127;if(a<-128)a=-128;

static int ws_snd_decode_init(AVCodecContext * avctx)
{
//    WSSNDContext *c = avctx->priv_data;
    
    return 0;
}

static int ws_snd_decode_frame(AVCodecContext *avctx,
                void *data, int *data_size,
                uint8_t *buf, int buf_size)
{
//    WSSNDContext *c = avctx->priv_data;
    
    int in_size, out_size;
    int sample = 0;
    int i;
    short *samples = data;
    
    if (!buf_size)
        return 0;

    out_size = LE_16(&buf[0]);
    *data_size = out_size * 2;
    in_size = LE_16(&buf[2]);
    buf += 4;
    
    if (in_size == out_size) {
        for (i = 0; i < out_size; i++)
            *samples++ = (*buf++ - 0x80) << 8;
        return buf_size;
    }
    
    while (out_size > 0) {
        int code;
        uint8_t count;
        code = (*buf) >> 6;
        count = (*buf) & 0x3F;
        buf++;
        switch(code) {
        case 0: /* ADPCM 2-bit */
            for (count++; count > 0; count--) {
                code = *buf++;
                sample += ws_adpcm_2bit[code & 0x3];
                CLIP8(sample);
                *samples++ = sample << 8;
                sample += ws_adpcm_2bit[(code >> 2) & 0x3];
                CLIP8(sample);
                *samples++ = sample << 8;
                sample += ws_adpcm_2bit[(code >> 4) & 0x3];
                CLIP8(sample);
                *samples++ = sample << 8;
                sample += ws_adpcm_2bit[(code >> 6) & 0x3];
                CLIP8(sample);
                *samples++ = sample << 8;
                out_size -= 4;
            }
            break;
        case 1: /* ADPCM 4-bit */
            for (count++; count > 0; count--) {
                code = *buf++;
                sample += ws_adpcm_4bit[code & 0xF];
                CLIP8(sample);
                *samples++ = sample << 8;
                sample += ws_adpcm_4bit[code >> 4];
                CLIP8(sample);
                *samples++ = sample << 8;
                out_size -= 2;
            }
            break;
        case 2: /* no compression */
            if (count & 0x20) { /* big delta */
                char t;
                t = count;
                t <<= 3;
                sample += t >> 3;
                *samples++ = sample << 8;
                out_size--;
            } else { /* copy */
                for (count++; count > 0; count--) {
                    *samples++ = (*buf++ - 0x80) << 8;
                    out_size--;
                }
                sample = buf[-1] - 0x80;
            }
            break;
        default: /* run */
            for(count++; count > 0; count--) {
                *samples++ = sample << 8;
                out_size--;
            }
        }
    }
    
    return buf_size;
}

AVCodec ws_snd1_decoder = {
    "ws_snd1",
    CODEC_TYPE_AUDIO,
    CODEC_ID_WESTWOOD_SND1,
    sizeof(WSSNDContext),
    ws_snd_decode_init,
    NULL,
    NULL,
    ws_snd_decode_frame,
};
