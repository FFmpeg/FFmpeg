/*
 * 8SVX audio decoder
 * Copyright (C) 2008 Jaikrishnan Menon
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
 * @file
 * 8svx audio decoder
 * @author Jaikrishnan Menon
 * supports: fibonacci delta encoding
 *         : exponential encoding
 */

#include "avcodec.h"

/** decoder context */
typedef struct EightSvxContext {
    int16_t fib_acc;
    const int16_t *table;
} EightSvxContext;

static const int16_t fibonacci[16]   = { -34<<8, -21<<8, -13<<8,  -8<<8, -5<<8, -3<<8, -2<<8, -1<<8,
                                          0, 1<<8, 2<<8, 3<<8, 5<<8, 8<<8, 13<<8, 21<<8 };
static const int16_t exponential[16] = { -128<<8, -64<<8, -32<<8, -16<<8, -8<<8, -4<<8, -2<<8, -1<<8,
                                          0, 1<<8, 2<<8, 4<<8, 8<<8, 16<<8, 32<<8, 64<<8 };

/** decode a frame */
static int eightsvx_decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                                 AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    EightSvxContext *esc = avctx->priv_data;
    int16_t *out_data = data;
    int consumed = buf_size;
    const uint8_t *buf_end = buf + buf_size;

    if((*data_size >> 2) < buf_size)
        return -1;

    if(avctx->frame_number == 0) {
        esc->fib_acc = buf[1] << 8;
        buf_size -= 2;
        buf += 2;
    }

    *data_size = buf_size << 2;

    while(buf < buf_end) {
        uint8_t d = *buf++;
        esc->fib_acc += esc->table[d & 0x0f];
        *out_data++ = esc->fib_acc;
        esc->fib_acc += esc->table[d >> 4];
        *out_data++ = esc->fib_acc;
    }

    return consumed;
}

/** initialize 8svx decoder */
static av_cold int eightsvx_decode_init(AVCodecContext *avctx)
{
    EightSvxContext *esc = avctx->priv_data;

    switch(avctx->codec->id) {
        case CODEC_ID_8SVX_FIB:
          esc->table = fibonacci;
          break;
        case CODEC_ID_8SVX_EXP:
          esc->table = exponential;
          break;
        default:
          return -1;
    }
    avctx->sample_fmt = AV_SAMPLE_FMT_S16;
    return 0;
}

AVCodec eightsvx_fib_decoder = {
  .name           = "8svx_fib",
  .type           = AVMEDIA_TYPE_AUDIO,
  .id             = CODEC_ID_8SVX_FIB,
  .priv_data_size = sizeof (EightSvxContext),
  .init           = eightsvx_decode_init,
  .decode         = eightsvx_decode_frame,
  .long_name      = NULL_IF_CONFIG_SMALL("8SVX fibonacci"),
};

AVCodec eightsvx_exp_decoder = {
  .name           = "8svx_exp",
  .type           = AVMEDIA_TYPE_AUDIO,
  .id             = CODEC_ID_8SVX_EXP,
  .priv_data_size = sizeof (EightSvxContext),
  .init           = eightsvx_decode_init,
  .decode         = eightsvx_decode_frame,
  .long_name      = NULL_IF_CONFIG_SMALL("8SVX exponential"),
};
