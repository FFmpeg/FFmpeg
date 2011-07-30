/*
 * SMPTE 302M decoder
 * Copyright (c) 2008 Laurent Aimar <fenrir@videolan.org>
 * Copyright (c) 2009 Baptiste Coudurier <baptiste.coudurier@gmail.com>
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

#include "libavutil/intreadwrite.h"
#include "avcodec.h"

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
    switch(channels) {
        case 2:
            avctx->channel_layout = AV_CH_LAYOUT_STEREO;
            break;
        case 4:
            avctx->channel_layout = AV_CH_LAYOUT_QUAD;
            break;
        case 8:
            avctx->channel_layout = AV_CH_LAYOUT_5POINT1_BACK | AV_CH_LAYOUT_STEREO_DOWNMIX;
    }
    avctx->sample_rate = 48000;
    avctx->bit_rate    = 48000 * avctx->channels * (avctx->bits_per_coded_sample + 4) +
                         32 * (48000 / (buf_size * 8 /
                                        (avctx->channels *
                                         (avctx->bits_per_coded_sample + 4))));

    return frame_size;
}

static int s302m_decode_frame(AVCodecContext *avctx, void *data,
                              int *data_size, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;

    int frame_size = s302m_parse_frame_header(avctx, buf, buf_size);
    if (frame_size < 0)
        return frame_size;

    buf_size -= AES3_HEADER_LEN;
    buf      += AES3_HEADER_LEN;

    if (*data_size < 4 * buf_size * 8 / (avctx->bits_per_coded_sample + 4))
        return -1;

    if (avctx->bits_per_coded_sample == 24) {
        uint32_t *o = data;
        for (; buf_size > 6; buf_size -= 7) {
            *o++ = (av_reverse[buf[2]]        << 24) |
                   (av_reverse[buf[1]]        << 16) |
                   (av_reverse[buf[0]]        <<  8);
            *o++ = (av_reverse[buf[6] & 0xf0] << 28) |
                   (av_reverse[buf[5]]        << 20) |
                   (av_reverse[buf[4]]        << 12) |
                   (av_reverse[buf[3] & 0x0f] <<  4);
            buf += 7;
        }
        *data_size = (uint8_t*) o - (uint8_t*) data;
    } else if (avctx->bits_per_coded_sample == 20) {
        uint32_t *o = data;
        for (; buf_size > 5; buf_size -= 6) {
            *o++ = (av_reverse[buf[2] & 0xf0] << 28) |
                   (av_reverse[buf[1]]        << 20) |
                   (av_reverse[buf[0]]        << 12);
            *o++ = (av_reverse[buf[5] & 0xf0] << 28) |
                   (av_reverse[buf[4]]        << 20) |
                   (av_reverse[buf[3]]        << 12);
            buf += 6;
        }
        *data_size = (uint8_t*) o - (uint8_t*) data;
    } else {
        uint16_t *o = data;
        for (; buf_size > 4; buf_size -= 5) {
            *o++ = (av_reverse[buf[1]]        <<  8) |
                    av_reverse[buf[0]];
            *o++ = (av_reverse[buf[4] & 0xf0] << 12) |
                   (av_reverse[buf[3]]        <<  4) |
                   (av_reverse[buf[2]]        >>  4);
            buf += 5;
        }
        *data_size = (uint8_t*) o - (uint8_t*) data;
    }

    return buf - avpkt->data;
}


AVCodec ff_s302m_decoder = {
    .name           = "s302m",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_S302M,
    .priv_data_size = 0,
    .decode         = s302m_decode_frame,
    .long_name      = NULL_IF_CONFIG_SMALL("SMPTE 302M"),
};
