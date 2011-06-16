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

#include "avcodec.h"

/** decoder context */
typedef struct EightSvxContext {
    const int8_t *table;

    /* buffer used to store the whole audio decoded/interleaved chunk,
     * which is sent with the first packet */
    uint8_t *samples;
    size_t samples_size;
    int samples_idx;
} EightSvxContext;

static const int8_t fibonacci[16]   = { -34,  -21, -13,  -8, -5, -3, -2, -1, 0, 1, 2, 3, 5, 8,  13, 21 };
static const int8_t exponential[16] = { -128, -64, -32, -16, -8, -4, -2, -1, 0, 1, 2, 4, 8, 16, 32, 64 };

#define MAX_FRAME_SIZE 2048

/**
 * Interleave samples in buffer containing all left channel samples
 * at the beginning, and right channel samples at the end.
 * Each sample is assumed to be in signed 8-bit format.
 *
 * @param size the size in bytes of the dst and src buffer
 */
static void interleave_stereo(uint8_t *dst, const uint8_t *src, int size)
{
    uint8_t *dst_end = dst + size;
    size = size>>1;

    while (dst < dst_end) {
        *dst++ = *src;
        *dst++ = *(src+size);
        src++;
    }
}

/**
 * Delta decode the compressed values in src, and put the resulting
 * decoded n samples in dst.
 *
 * @param val starting value assumed by the delta sequence
 * @param table delta sequence table
 * @return size in bytes of the decoded data, must be src_size*2
 */
static int delta_decode(int8_t *dst, const uint8_t *src, int src_size,
                        int8_t val, const int8_t *table)
{
    int n = src_size;
    int8_t *dst0 = dst;

    while (n--) {
        uint8_t d = *src++;
        val = av_clip(val + table[d & 0x0f], -127, 128);
        *dst++ = val;
        val = av_clip(val + table[d >> 4]  , -127, 128);
        *dst++ = val;
    }

    return dst-dst0;
}

static int eightsvx_decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                                 AVPacket *avpkt)
{
    EightSvxContext *esc = avctx->priv_data;
    int out_data_size, n;
    uint8_t *src, *dst;

    /* decode and interleave the first packet */
    if (!esc->samples && avpkt) {
        uint8_t *deinterleaved_samples;

        esc->samples_size = avctx->codec->id == CODEC_ID_8SVX_RAW ?
            avpkt->size : avctx->channels + (avpkt->size-avctx->channels) * 2;
        if (!(esc->samples = av_malloc(esc->samples_size)))
            return AVERROR(ENOMEM);

        /* decompress */
        if (avctx->codec->id == CODEC_ID_8SVX_FIB || avctx->codec->id == CODEC_ID_8SVX_EXP) {
            const uint8_t *buf = avpkt->data;
            int buf_size = avpkt->size;
            int n = esc->samples_size;

            if (!(deinterleaved_samples = av_mallocz(n)))
                return AVERROR(ENOMEM);

            /* the uncompressed starting value is contained in the first byte */
            if (avctx->channels == 2) {
                delta_decode(deinterleaved_samples      , buf+1, buf_size/2-1, buf[0], esc->table);
                buf += buf_size/2;
                delta_decode(deinterleaved_samples+n/2-1, buf+1, buf_size/2-1, buf[0], esc->table);
            } else
                delta_decode(deinterleaved_samples      , buf+1, buf_size-1  , buf[0], esc->table);
        } else {
            deinterleaved_samples = avpkt->data;
        }

        if (avctx->channels == 2)
            interleave_stereo(esc->samples, deinterleaved_samples, esc->samples_size);
        else
            memcpy(esc->samples, deinterleaved_samples, esc->samples_size);
    }

    /* return single packed with fixed size */
    out_data_size = FFMIN(MAX_FRAME_SIZE, esc->samples_size - esc->samples_idx);
    if (*data_size < out_data_size) {
        av_log(avctx, AV_LOG_ERROR, "Provided buffer with size %d is too small.\n", *data_size);
        return AVERROR(EINVAL);
    }

    *data_size = out_data_size;
    dst = data;
    src = esc->samples + esc->samples_idx;
    for (n = out_data_size; n > 0; n--)
        *dst++ = *src++ + 128;
    esc->samples_idx += *data_size;

    return avctx->codec->id == CODEC_ID_8SVX_FIB || avctx->codec->id == CODEC_ID_8SVX_EXP ?
        (avctx->frame_number == 0)*2 + out_data_size / 2 :
        out_data_size;
}

static av_cold int eightsvx_decode_init(AVCodecContext *avctx)
{
    EightSvxContext *esc = avctx->priv_data;

    if (avctx->channels > 2) {
        av_log(avctx, AV_LOG_ERROR, "8SVX does not support more than 2 channels\n");
        return AVERROR_INVALIDDATA;
    }

    switch (avctx->codec->id) {
    case CODEC_ID_8SVX_FIB: esc->table = fibonacci;    break;
    case CODEC_ID_8SVX_EXP: esc->table = exponential;  break;
    case CODEC_ID_8SVX_RAW: esc->table = NULL;         break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Invalid codec id %d.\n", avctx->codec->id);
        return AVERROR_INVALIDDATA;
    }
    avctx->sample_fmt = AV_SAMPLE_FMT_U8;

    return 0;
}

static av_cold int eightsvx_decode_close(AVCodecContext *avctx)
{
    EightSvxContext *esc = avctx->priv_data;

    av_freep(&esc->samples);
    esc->samples_size = 0;
    esc->samples_idx = 0;

    return 0;
}

AVCodec ff_eightsvx_fib_decoder = {
  .name           = "8svx_fib",
  .type           = AVMEDIA_TYPE_AUDIO,
  .id             = CODEC_ID_8SVX_FIB,
  .priv_data_size = sizeof (EightSvxContext),
  .init           = eightsvx_decode_init,
  .decode         = eightsvx_decode_frame,
  .close          = eightsvx_decode_close,
  .long_name      = NULL_IF_CONFIG_SMALL("8SVX fibonacci"),
};

AVCodec ff_eightsvx_exp_decoder = {
  .name           = "8svx_exp",
  .type           = AVMEDIA_TYPE_AUDIO,
  .id             = CODEC_ID_8SVX_EXP,
  .priv_data_size = sizeof (EightSvxContext),
  .init           = eightsvx_decode_init,
  .decode         = eightsvx_decode_frame,
  .close          = eightsvx_decode_close,
  .long_name      = NULL_IF_CONFIG_SMALL("8SVX exponential"),
};

AVCodec ff_eightsvx_raw_decoder = {
  .name           = "8svx_raw",
  .type           = AVMEDIA_TYPE_AUDIO,
  .id             = CODEC_ID_8SVX_RAW,
  .priv_data_size = sizeof(EightSvxContext),
  .init           = eightsvx_decode_init,
  .decode         = eightsvx_decode_frame,
  .close          = eightsvx_decode_close,
  .long_name      = NULL_IF_CONFIG_SMALL("8SVX rawaudio"),
};
