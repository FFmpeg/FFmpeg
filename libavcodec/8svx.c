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
#include "internal.h"
#include "libavutil/common.h"

/** decoder context */
typedef struct EightSvxContext {
    uint8_t fib_acc[2];
    const int8_t *table;

    /* buffer used to store the whole first packet.
       data is only sent as one large packet */
    uint8_t *data[2];
    int data_size;
    int data_idx;
} EightSvxContext;

static const int8_t fibonacci[16]   = { -34, -21, -13,  -8, -5, -3, -2, -1,
                                          0,   1,   2,   3,  5,  8, 13, 21 };
static const int8_t exponential[16] = { -128, -64, -32, -16, -8, -4, -2, -1,
                                           0,   1,   2,   4,  8, 16, 32, 64 };

#define MAX_FRAME_SIZE 32768

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

static void raw_decode(uint8_t *dst, const int8_t *src, int src_size)
{
    while (src_size--)
        *dst++ = *src++ + 128;
}

/** decode a frame */
static int eightsvx_decode_frame(AVCodecContext *avctx, void *data,
                                 int *got_frame_ptr, AVPacket *avpkt)
{
    EightSvxContext *esc = avctx->priv_data;
    AVFrame *frame       = data;
    int buf_size;
    int ch, ret;
    int is_compr = (avctx->codec_id != AV_CODEC_ID_PCM_S8_PLANAR);

    /* for the first packet, copy data to buffer */
    if (avpkt->data) {
        int hdr_size  = is_compr ? 2 : 0;
        int chan_size = (avpkt->size - hdr_size * avctx->channels) / avctx->channels;

        if (avpkt->size < hdr_size * avctx->channels) {
            av_log(avctx, AV_LOG_ERROR, "packet size is too small\n");
            return AVERROR_INVALIDDATA;
        }
        if (esc->data[0]) {
            av_log(avctx, AV_LOG_ERROR, "unexpected data after first packet\n");
            return AVERROR_INVALIDDATA;
        }

        if (is_compr) {
        esc->fib_acc[0] = avpkt->data[1] + 128;
        if (avctx->channels == 2)
            esc->fib_acc[1] = avpkt->data[2+chan_size+1] + 128;
        }

        esc->data_idx  = 0;
        esc->data_size = chan_size;
        if (!(esc->data[0] = av_malloc(chan_size)))
            return AVERROR(ENOMEM);
        if (avctx->channels == 2) {
            if (!(esc->data[1] = av_malloc(chan_size))) {
                av_freep(&esc->data[0]);
                return AVERROR(ENOMEM);
            }
        }
        memcpy(esc->data[0], &avpkt->data[hdr_size], chan_size);
        if (avctx->channels == 2)
            memcpy(esc->data[1], &avpkt->data[2*hdr_size+chan_size], chan_size);
    }
    if (!esc->data[0]) {
        av_log(avctx, AV_LOG_ERROR, "unexpected empty packet\n");
        return AVERROR_INVALIDDATA;
    }

    /* decode next piece of data from the buffer */
    buf_size = FFMIN(MAX_FRAME_SIZE, esc->data_size - esc->data_idx);
    if (buf_size <= 0) {
        *got_frame_ptr = 0;
        return avpkt->size;
    }

    /* get output buffer */
    frame->nb_samples = buf_size * (is_compr + 1);
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }

    for (ch = 0; ch < avctx->channels; ch++) {
        if (is_compr) {
            delta_decode(frame->data[ch], &esc->data[ch][esc->data_idx],
                         buf_size, &esc->fib_acc[ch], esc->table);
        } else {
            raw_decode(frame->data[ch], &esc->data[ch][esc->data_idx],
                       buf_size);
        }
    }

    esc->data_idx += buf_size;

    *got_frame_ptr = 1;

    return avpkt->size;
}

/** initialize 8svx decoder */
static av_cold int eightsvx_decode_init(AVCodecContext *avctx)
{
    EightSvxContext *esc = avctx->priv_data;

    if (avctx->channels < 1 || avctx->channels > 2) {
        av_log(avctx, AV_LOG_ERROR, "8SVX does not support more than 2 channels\n");
        return AVERROR_INVALIDDATA;
    }

    switch(avctx->codec->id) {
        case AV_CODEC_ID_8SVX_FIB:
          esc->table = fibonacci;
          break;
        case AV_CODEC_ID_8SVX_EXP:
          esc->table = exponential;
          break;
        case AV_CODEC_ID_PCM_S8_PLANAR:
            break;
        default:
          return AVERROR_INVALIDDATA;
    }
    avctx->sample_fmt = AV_SAMPLE_FMT_U8P;

    return 0;
}

static av_cold int eightsvx_decode_close(AVCodecContext *avctx)
{
    EightSvxContext *esc = avctx->priv_data;

    av_freep(&esc->data[0]);
    av_freep(&esc->data[1]);

    return 0;
}

AVCodec ff_eightsvx_fib_decoder = {
  .name           = "8svx_fib",
  .long_name      = NULL_IF_CONFIG_SMALL("8SVX fibonacci"),
  .type           = AVMEDIA_TYPE_AUDIO,
  .id             = AV_CODEC_ID_8SVX_FIB,
  .priv_data_size = sizeof (EightSvxContext),
  .init           = eightsvx_decode_init,
  .close          = eightsvx_decode_close,
  .decode         = eightsvx_decode_frame,
  .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_DR1,
  .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_U8P,
                                                    AV_SAMPLE_FMT_NONE },
};

AVCodec ff_eightsvx_exp_decoder = {
  .name           = "8svx_exp",
  .long_name      = NULL_IF_CONFIG_SMALL("8SVX exponential"),
  .type           = AVMEDIA_TYPE_AUDIO,
  .id             = AV_CODEC_ID_8SVX_EXP,
  .priv_data_size = sizeof (EightSvxContext),
  .init           = eightsvx_decode_init,
  .close          = eightsvx_decode_close,
  .decode         = eightsvx_decode_frame,
  .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_DR1,
  .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_U8P,
                                                    AV_SAMPLE_FMT_NONE },
};

AVCodec ff_pcm_s8_planar_decoder = {
    .name           = "pcm_s8_planar",
    .long_name      = NULL_IF_CONFIG_SMALL("PCM signed 8-bit planar"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_PCM_S8_PLANAR,
    .priv_data_size = sizeof(EightSvxContext),
    .init           = eightsvx_decode_init,
    .close          = eightsvx_decode_close,
    .decode         = eightsvx_decode_frame,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_DR1,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_U8P,
                                                      AV_SAMPLE_FMT_NONE },
};
