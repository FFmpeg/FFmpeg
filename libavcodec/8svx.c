/*
 * Copyright (C) 2008 Jaikrishnan Menon
 * Copyright (C) 2011 Stefano Sabatini
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
 *
 * supports: fibonacci delta encoding
 *         : exponential encoding
 *
 * For more information about the 8SVX format:
 * http://netghost.narod.ru/gff/vendspec/iff/iff.txt
 * http://sox.sourceforge.net/AudioFormats-11.html
 * http://aminet.net/package/mus/misc/wavepak
 * http://amigan.1emu.net/reg/8SVX.txt
 *
 * Samples can be found here:
 * http://aminet.net/mods/smpl/
 */

#include "config_components.h"

#include "libavutil/avassert.h"
#include "libavutil/mem.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
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

static const int8_t fibonacci[16]   = { -34,  -21, -13,  -8, -5, -3, -2, -1, 0, 1, 2, 3, 5, 8,  13, 21 };
static const int8_t exponential[16] = { -128, -64, -32, -16, -8, -4, -2, -1, 0, 1, 2, 4, 8, 16, 32, 64 };

#define MAX_FRAME_SIZE 2048

/**
 * Delta decode the compressed values in src, and put the resulting
 * decoded samples in dst.
 *
 * @param[in,out] state starting value. it is saved for use in the next call.
 * @param table delta sequence table
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
static int eightsvx_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                                 int *got_frame_ptr, AVPacket *avpkt)
{
    EightSvxContext *esc = avctx->priv_data;
    int channels         = avctx->ch_layout.nb_channels;
    int buf_size;
    int ch, ret;
    int hdr_size = 2;

    /* decode and interleave the first packet */
    if (!esc->data[0] && avpkt) {
        int chan_size = avpkt->size / channels - hdr_size;

        if (avpkt->size % channels) {
            av_log(avctx, AV_LOG_WARNING, "Packet with odd size, ignoring last byte\n");
        }
        if (avpkt->size < (hdr_size + 1) * channels) {
            av_log(avctx, AV_LOG_ERROR, "packet size is too small\n");
            return AVERROR_INVALIDDATA;
        }

        esc->fib_acc[0] = avpkt->data[1] + 128;
        if (channels == 2)
            esc->fib_acc[1] = avpkt->data[2+chan_size+1] + 128;

        esc->data_idx  = 0;
        esc->data_size = chan_size;
        if (!(esc->data[0] = av_malloc(chan_size)))
            return AVERROR(ENOMEM);
        if (channels == 2) {
            if (!(esc->data[1] = av_malloc(chan_size))) {
                av_freep(&esc->data[0]);
                return AVERROR(ENOMEM);
            }
        }
        memcpy(esc->data[0], &avpkt->data[hdr_size], chan_size);
        if (channels == 2)
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
    frame->nb_samples = buf_size * 2;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    for (ch = 0; ch < channels; ch++) {
        delta_decode(frame->data[ch], &esc->data[ch][esc->data_idx],
                     buf_size, &esc->fib_acc[ch], esc->table);
    }

    esc->data_idx += buf_size;

    *got_frame_ptr = 1;

    return ((avctx->frame_num == 0) * hdr_size + buf_size) * channels;
}

static av_cold int eightsvx_decode_init(AVCodecContext *avctx)
{
    EightSvxContext *esc = avctx->priv_data;

    if (avctx->ch_layout.nb_channels < 1 || avctx->ch_layout.nb_channels > 2) {
        av_log(avctx, AV_LOG_ERROR, "8SVX does not support more than 2 channels\n");
        return AVERROR_INVALIDDATA;
    }

    switch (avctx->codec->id) {
    case AV_CODEC_ID_8SVX_FIB: esc->table = fibonacci;    break;
    case AV_CODEC_ID_8SVX_EXP: esc->table = exponential;  break;
    default:
        av_assert1(0);
    }
    avctx->sample_fmt = AV_SAMPLE_FMT_U8P;

    return 0;
}

static av_cold int eightsvx_decode_close(AVCodecContext *avctx)
{
    EightSvxContext *esc = avctx->priv_data;

    av_freep(&esc->data[0]);
    av_freep(&esc->data[1]);
    esc->data_size = 0;
    esc->data_idx = 0;

    return 0;
}

#if CONFIG_EIGHTSVX_FIB_DECODER
const FFCodec ff_eightsvx_fib_decoder = {
  .p.name         = "8svx_fib",
  CODEC_LONG_NAME("8SVX fibonacci"),
  .p.type         = AVMEDIA_TYPE_AUDIO,
  .p.id           = AV_CODEC_ID_8SVX_FIB,
  .priv_data_size = sizeof (EightSvxContext),
  .init           = eightsvx_decode_init,
  FF_CODEC_DECODE_CB(eightsvx_decode_frame),
  .close          = eightsvx_decode_close,
  .p.capabilities = AV_CODEC_CAP_DR1,
    CODEC_SAMPLEFMTS(AV_SAMPLE_FMT_U8P),
};
#endif
#if CONFIG_EIGHTSVX_EXP_DECODER
const FFCodec ff_eightsvx_exp_decoder = {
  .p.name         = "8svx_exp",
  CODEC_LONG_NAME("8SVX exponential"),
  .p.type         = AVMEDIA_TYPE_AUDIO,
  .p.id           = AV_CODEC_ID_8SVX_EXP,
  .priv_data_size = sizeof (EightSvxContext),
  .init           = eightsvx_decode_init,
  FF_CODEC_DECODE_CB(eightsvx_decode_frame),
  .close          = eightsvx_decode_close,
  .p.capabilities = AV_CODEC_CAP_DR1,
    CODEC_SAMPLEFMTS(AV_SAMPLE_FMT_U8P),
};
#endif
