/*
 * 012v decoder
 *
 * Copyright (C) 2012 Carl Eugen Hoyos
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

#include "avcodec.h"
#include "internal.h"
#include "libavutil/intreadwrite.h"

static av_cold int zero12v_decode_init(AVCodecContext *avctx)
{
    avctx->pix_fmt             = AV_PIX_FMT_YUV422P16;
    avctx->bits_per_raw_sample = 10;

    if (avctx->codec_tag == MKTAG('a', '1', '2', 'v'))
        avpriv_request_sample(avctx, "transparency");

    return 0;
}

static int zero12v_decode_frame(AVCodecContext *avctx, void *data,
                                int *got_frame, AVPacket *avpkt)
{
    int line = 0, ret;
    const int width = avctx->width;
    AVFrame *pic = data;
    uint16_t *y, *u, *v;
    const uint8_t *line_end, *src = avpkt->data;
    int stride = avctx->width * 8 / 3;

    if (width == 1) {
        av_log(avctx, AV_LOG_ERROR, "Width 1 not supported.\n");
        return AVERROR_INVALIDDATA;
    }
    if (avpkt->size < avctx->height * stride) {
        av_log(avctx, AV_LOG_ERROR, "Packet too small: %d instead of %d\n",
               avpkt->size, avctx->height * stride);
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_get_buffer(avctx, pic, 0)) < 0)
        return ret;

    pic->pict_type = AV_PICTURE_TYPE_I;
    pic->key_frame = 1;

    y = (uint16_t *)pic->data[0];
    u = (uint16_t *)pic->data[1];
    v = (uint16_t *)pic->data[2];
    line_end = avpkt->data + stride;

    while (line++ < avctx->height) {
        while (1) {
            uint32_t t = AV_RL32(src);
            src += 4;
            *u++ = t <<  6 & 0xFFC0;
            *y++ = t >>  4 & 0xFFC0;
            *v++ = t >> 14 & 0xFFC0;

            if (src >= line_end - 1) {
                *y = 0x80;
                src++;
                line_end += stride;
                y = (uint16_t *)(pic->data[0] + line * pic->linesize[0]);
                u = (uint16_t *)(pic->data[1] + line * pic->linesize[1]);
                v = (uint16_t *)(pic->data[2] + line * pic->linesize[2]);
                break;
            }

            t = AV_RL32(src);
            src += 4;
            *y++ = t <<  6 & 0xFFC0;
            *u++ = t >>  4 & 0xFFC0;
            *y++ = t >> 14 & 0xFFC0;
            if (src >= line_end - 2) {
                if (!(width & 1)) {
                    *y = 0x80;
                    src += 2;
                }
                line_end += stride;
                y = (uint16_t *)(pic->data[0] + line * pic->linesize[0]);
                u = (uint16_t *)(pic->data[1] + line * pic->linesize[1]);
                v = (uint16_t *)(pic->data[2] + line * pic->linesize[2]);
                break;
            }

            t = AV_RL32(src);
            src += 4;
            *v++ = t <<  6 & 0xFFC0;
            *y++ = t >>  4 & 0xFFC0;
            *u++ = t >> 14 & 0xFFC0;

            if (src >= line_end - 1) {
                *y = 0x80;
                src++;
                line_end += stride;
                y = (uint16_t *)(pic->data[0] + line * pic->linesize[0]);
                u = (uint16_t *)(pic->data[1] + line * pic->linesize[1]);
                v = (uint16_t *)(pic->data[2] + line * pic->linesize[2]);
                break;
            }

            t = AV_RL32(src);
            src += 4;
            *y++ = t <<  6 & 0xFFC0;
            *v++ = t >>  4 & 0xFFC0;
            *y++ = t >> 14 & 0xFFC0;

            if (src >= line_end - 2) {
                if (width & 1) {
                    *y = 0x80;
                    src += 2;
                }
                line_end += stride;
                y = (uint16_t *)(pic->data[0] + line * pic->linesize[0]);
                u = (uint16_t *)(pic->data[1] + line * pic->linesize[1]);
                v = (uint16_t *)(pic->data[2] + line * pic->linesize[2]);
                break;
            }
        }
    }

    *got_frame = 1;

    return avpkt->size;
}

AVCodec ff_zero12v_decoder = {
    .name           = "012v",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_012V,
    .init           = zero12v_decode_init,
    .decode         = zero12v_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Uncompressed 4:2:2 10-bit"),
};
