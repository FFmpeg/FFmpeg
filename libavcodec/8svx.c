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

#include "libavutil/avassert.h"
#include "avcodec.h"
#include "libavutil/common.h"

/** decoder context */
typedef struct EightSvxContext {
    AVFrame frame;
    const int8_t *table;

    /* buffer used to store the whole audio decoded/interleaved chunk,
     * which is sent with the first packet */
    uint8_t *samples;
    int64_t samples_size;
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

/** decode a frame */
static int eightsvx_decode_frame(AVCodecContext *avctx, void *data,
                                 int *got_frame_ptr, AVPacket *avpkt)
{
    EightSvxContext *esc = avctx->priv_data;
    int n, out_data_size, ret;
    uint8_t *src, *dst;

    /* decode and interleave the first packet */
    if (!esc->samples && avpkt) {
        uint8_t *deinterleaved_samples, *p = NULL;
        int packet_size = avpkt->size;

        if (packet_size % avctx->channels) {
            av_log(avctx, AV_LOG_WARNING, "Packet with odd size, ignoring last byte\n");
            if (packet_size < avctx->channels)
                return packet_size;
            packet_size -= packet_size % avctx->channels;
        }
        esc->samples_size = !esc->table ?
            packet_size : avctx->channels + (packet_size-avctx->channels) * 2;
        if (!(esc->samples = av_malloc(esc->samples_size)))
            return AVERROR(ENOMEM);

        /* decompress */
        if (esc->table) {
            const uint8_t *buf = avpkt->data;
            uint8_t *dst;
            int buf_size = avpkt->size;
            int i, n = esc->samples_size;

            if (buf_size < 2) {
                av_log(avctx, AV_LOG_ERROR, "packet size is too small\n");
                return AVERROR(EINVAL);
            }
            if (!(deinterleaved_samples = av_mallocz(n)))
                return AVERROR(ENOMEM);
            dst = p = deinterleaved_samples;

            /* the uncompressed starting value is contained in the first byte */
            dst = deinterleaved_samples;
            for (i = 0; i < avctx->channels; i++) {
                delta_decode(dst, buf + 1, buf_size / avctx->channels - 1, buf[0], esc->table);
                buf += buf_size / avctx->channels;
                dst += n / avctx->channels - 1;
            }
        } else {
            deinterleaved_samples = avpkt->data;
        }

        if (avctx->channels == 2)
            interleave_stereo(esc->samples, deinterleaved_samples, esc->samples_size);
        else
            memcpy(esc->samples, deinterleaved_samples, esc->samples_size);
        av_freep(&p);
    }

    /* get output buffer */
    av_assert1(!(esc->samples_size % avctx->channels || esc->samples_idx % avctx->channels));
    esc->frame.nb_samples = FFMIN(MAX_FRAME_SIZE, esc->samples_size - esc->samples_idx)  / avctx->channels;
    if ((ret = avctx->get_buffer(avctx, &esc->frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }

    *got_frame_ptr   = 1;
    *(AVFrame *)data = esc->frame;

    dst = esc->frame.data[0];
    src = esc->samples + esc->samples_idx;
    out_data_size = esc->frame.nb_samples * avctx->channels;
    for (n = out_data_size; n > 0; n--)
        *dst++ = *src++ + 128;
    esc->samples_idx += out_data_size;

    return esc->table ?
        (avctx->frame_number == 0)*2 + out_data_size / 2 :
        out_data_size;
}

static av_cold int eightsvx_decode_init(AVCodecContext *avctx)
{
    EightSvxContext *esc = avctx->priv_data;

    if (avctx->channels < 1 || avctx->channels > 2) {
        av_log(avctx, AV_LOG_ERROR, "8SVX does not support more than 2 channels\n");
        return AVERROR_INVALIDDATA;
    }

    switch (avctx->codec->id) {
    case AV_CODEC_ID_8SVX_FIB: esc->table = fibonacci;    break;
    case AV_CODEC_ID_8SVX_EXP: esc->table = exponential;  break;
    case AV_CODEC_ID_PCM_S8_PLANAR:
    case AV_CODEC_ID_8SVX_RAW: esc->table = NULL;         break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Invalid codec id %d.\n", avctx->codec->id);
        return AVERROR_INVALIDDATA;
    }
    avctx->sample_fmt = AV_SAMPLE_FMT_U8;

    avcodec_get_frame_defaults(&esc->frame);
    avctx->coded_frame = &esc->frame;

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

#if CONFIG_EIGHTSVX_FIB_DECODER
AVCodec ff_eightsvx_fib_decoder = {
  .name           = "8svx_fib",
  .type           = AVMEDIA_TYPE_AUDIO,
  .id             = AV_CODEC_ID_8SVX_FIB,
  .priv_data_size = sizeof (EightSvxContext),
  .init           = eightsvx_decode_init,
  .decode         = eightsvx_decode_frame,
  .close          = eightsvx_decode_close,
  .capabilities   = CODEC_CAP_DR1,
  .long_name      = NULL_IF_CONFIG_SMALL("8SVX fibonacci"),
};
#endif
#if CONFIG_EIGHTSVX_EXP_DECODER
AVCodec ff_eightsvx_exp_decoder = {
  .name           = "8svx_exp",
  .type           = AVMEDIA_TYPE_AUDIO,
  .id             = AV_CODEC_ID_8SVX_EXP,
  .priv_data_size = sizeof (EightSvxContext),
  .init           = eightsvx_decode_init,
  .decode         = eightsvx_decode_frame,
  .close          = eightsvx_decode_close,
  .capabilities   = CODEC_CAP_DR1,
  .long_name      = NULL_IF_CONFIG_SMALL("8SVX exponential"),
};
#endif
#if CONFIG_PCM_S8_PLANAR_DECODER
AVCodec ff_pcm_s8_planar_decoder = {
    .name           = "pcm_s8_planar",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_PCM_S8_PLANAR,
    .priv_data_size = sizeof(EightSvxContext),
    .init           = eightsvx_decode_init,
    .close          = eightsvx_decode_close,
    .decode         = eightsvx_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("PCM signed 8-bit planar"),
};
#endif
