/*
 * 8SVX audio decoder
 * Copyright (C) 2008 Jaikrishnan Menon
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * 8svx audio decoder
 * @author Jaikrishnan Menon
 *
 * supports: fibonacci delta encoding
 *         : exponential encoding
 */

#include "avcodec.h"

/** decoder context */
typedef struct EightSvxContext {
    uint8_t fib_acc;
    const int8_t *table;
} EightSvxContext;

static const int8_t fibonacci[16]   = { -34, -21, -13,  -8, -5, -3, -2, -1,
                                          0,   1,   2,   3,  5,  8, 13, 21 };
static const int8_t exponential[16] = { -128, -64, -32, -16, -8, -4, -2, -1,
                                           0,   1,   2,   4,  8, 16, 32, 64 };

/**
 * Delta decode the compressed values in src, and put the resulting
 * decoded samples in dst.
 *
 * @param[in,out] state starting value. it is saved for use in the next call.
 */
static void delta_decode(uint8_t *dst, const uint8_t *src, int src_size,
                         uint8_t *state, const int8_t *table)
{
    uint8_t val = *state;

    while (src_size--) {
        uint8_t d = *src++;
        val = av_clip_uint8(val + table[d & 0xF]);
        *dst++ = val;
        val = av_clip_uint8(val + table[d >> 4]);
        *dst++ = val;
    }

    *state = val;
}

/** decode a frame */
static int eightsvx_decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                                 AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    EightSvxContext *esc = avctx->priv_data;
    uint8_t *out_data = data;
    int consumed = buf_size;

    if(avctx->frame_number == 0) {
        if (buf_size < 2) {
            av_log(avctx, AV_LOG_ERROR, "packet size is too small\n");
            return AVERROR(EINVAL);
        }
        esc->fib_acc = (int8_t)buf[1] + 128;
        buf_size -= 2;
        buf += 2;
    }

    if (*data_size < buf_size * 2)
        return AVERROR(EINVAL);

    delta_decode(out_data, buf, buf_size, &esc->fib_acc, esc->table);

    *data_size = buf_size * 2;

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
    avctx->sample_fmt = AV_SAMPLE_FMT_U8;
    return 0;
}

AVCodec ff_eightsvx_fib_decoder = {
  .name           = "8svx_fib",
  .type           = AVMEDIA_TYPE_AUDIO,
  .id             = CODEC_ID_8SVX_FIB,
  .priv_data_size = sizeof (EightSvxContext),
  .init           = eightsvx_decode_init,
  .decode         = eightsvx_decode_frame,
  .long_name      = NULL_IF_CONFIG_SMALL("8SVX fibonacci"),
};

AVCodec ff_eightsvx_exp_decoder = {
  .name           = "8svx_exp",
  .type           = AVMEDIA_TYPE_AUDIO,
  .id             = CODEC_ID_8SVX_EXP,
  .priv_data_size = sizeof (EightSvxContext),
  .init           = eightsvx_decode_init,
  .decode         = eightsvx_decode_frame,
  .long_name      = NULL_IF_CONFIG_SMALL("8SVX exponential"),
};
