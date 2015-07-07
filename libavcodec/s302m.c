/*
 * SMPTE 302M decoder
 * Copyright (c) 2008 Laurent Aimar <fenrir@videolan.org>
 * Copyright (c) 2009 Baptiste Coudurier <baptiste.coudurier@gmail.com>
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

#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "avcodec.h"
#include "internal.h"
#include "mathops.h"

#define AES3_HEADER_LEN 4

static int s302m_parse_frame_header(AVCodecContext *avctx, const uint8_t *buf,
                                    int buf_size)
{
    uint32_t h;
    int frame_size, channels, bits;

    if (buf_size <= AES3_HEADER_LEN) {
        av_log(avctx, AV_LOG_ERROR, "frame is too short\n");
        return AVERROR_INVALIDDATA;
    }

    /*
     * AES3 header :
     * size:            16
     * number channels   2
     * channel_id        8
     * bits per samples  2
     * alignments        4
     */

    h = AV_RB32(buf);
    frame_size =  (h >> 16) & 0xffff;
    channels   = ((h >> 14) & 0x0003) * 2 +  2;
    bits       = ((h >>  4) & 0x0003) * 4 + 16;

    if (AES3_HEADER_LEN + frame_size != buf_size || bits > 24) {
        av_log(avctx, AV_LOG_ERROR, "frame has invalid header\n");
        return AVERROR_INVALIDDATA;
    }

    /* Set output properties */
    avctx->bits_per_coded_sample = bits;
    if (bits > 16)
        avctx->sample_fmt = AV_SAMPLE_FMT_S32;
    else
        avctx->sample_fmt = AV_SAMPLE_FMT_S16;

    avctx->channels    = channels;
    avctx->sample_rate = 48000;
    avctx->bit_rate    = 48000 * avctx->channels * (avctx->bits_per_coded_sample + 4) +
                         32 * (48000 / (buf_size * 8 /
                                        (avctx->channels *
                                         (avctx->bits_per_coded_sample + 4))));

    return frame_size;
}

static int s302m_decode_frame(AVCodecContext *avctx, void *data,
                              int *got_frame_ptr, AVPacket *avpkt)
{
    AVFrame *frame     = data;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    int block_size, ret;

    int frame_size = s302m_parse_frame_header(avctx, buf, buf_size);
    if (frame_size < 0)
        return frame_size;

    buf_size -= AES3_HEADER_LEN;
    buf      += AES3_HEADER_LEN;

    /* get output buffer */
    block_size = (avctx->bits_per_coded_sample + 4) / 4;
    frame->nb_samples = 2 * (buf_size / block_size) / avctx->channels;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }

    buf_size = (frame->nb_samples * avctx->channels / 2) * block_size;

    if (avctx->bits_per_coded_sample == 24) {
        uint32_t *o = (uint32_t *)frame->data[0];
        for (; buf_size > 6; buf_size -= 7) {
            *o++ = (ff_reverse[buf[2]]        << 24) |
                   (ff_reverse[buf[1]]        << 16) |
                   (ff_reverse[buf[0]]        <<  8);
            *o++ = (ff_reverse[buf[6] & 0xf0] << 28) |
                   (ff_reverse[buf[5]]        << 20) |
                   (ff_reverse[buf[4]]        << 12) |
                   (ff_reverse[buf[3] & 0x0f] <<  4);
            buf += 7;
        }
    } else if (avctx->bits_per_coded_sample == 20) {
        uint32_t *o = (uint32_t *)frame->data[0];
        for (; buf_size > 5; buf_size -= 6) {
            *o++ = (ff_reverse[buf[2] & 0xf0] << 28) |
                   (ff_reverse[buf[1]]        << 20) |
                   (ff_reverse[buf[0]]        << 12);
            *o++ = (ff_reverse[buf[5] & 0xf0] << 28) |
                   (ff_reverse[buf[4]]        << 20) |
                   (ff_reverse[buf[3]]        << 12);
            buf += 6;
        }
    } else {
        uint16_t *o = (uint16_t *)frame->data[0];
        for (; buf_size > 4; buf_size -= 5) {
            *o++ = (ff_reverse[buf[1]]        <<  8) |
                    ff_reverse[buf[0]];
            *o++ = (ff_reverse[buf[4] & 0xf0] << 12) |
                   (ff_reverse[buf[3]]        <<  4) |
                   (ff_reverse[buf[2]]        >>  4);
            buf += 5;
        }
    }

    *got_frame_ptr = 1;

    return avpkt->size;
}

AVCodec ff_s302m_decoder = {
    .name           = "s302m",
    .long_name      = NULL_IF_CONFIG_SMALL("SMPTE 302M"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_S302M,
    .decode         = s302m_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};
