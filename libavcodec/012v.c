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
#include "codec_internal.h"
#include "decode.h"
#include "libavutil/intreadwrite.h"

static av_cold int zero12v_decode_init(AVCodecContext *avctx)
{
    avctx->pix_fmt             = AV_PIX_FMT_YUV422P16;
    avctx->bits_per_raw_sample = 10;

    if (avctx->codec_tag == MKTAG('a', '1', '2', 'v'))
        avpriv_request_sample(avctx, "transparency");

    return 0;
}

static int zero12v_decode_frame(AVCodecContext *avctx, AVFrame *pic,
                                int *got_frame, AVPacket *avpkt)
{
    int line, ret;
    const int width = avctx->width;
    uint16_t *y, *u, *v;
    const uint8_t *line_end, *src = avpkt->data;
    int stride = avctx->width * 8 / 3;

    if (width <= 1 || avctx->height <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Dimensions %dx%d not supported.\n", width, avctx->height);
        return AVERROR_INVALIDDATA;
    }

    if (   avctx->codec_tag == MKTAG('0', '1', '2', 'v')
        && avpkt->size % avctx->height == 0
        && avpkt->size / avctx->height * 3 >= width * 8)
        stride = avpkt->size / avctx->height;

    if (avpkt->size < avctx->height * stride) {
        av_log(avctx, AV_LOG_ERROR, "Packet too small: %d instead of %d\n",
               avpkt->size, avctx->height * stride);
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_get_buffer(avctx, pic, 0)) < 0)
        return ret;

    line_end = avpkt->data + stride;
    for (line = 0; line < avctx->height; line++) {
        uint16_t y_temp[6] = {0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000};
        uint16_t u_temp[3] = {0x8000, 0x8000, 0x8000};
        uint16_t v_temp[3] = {0x8000, 0x8000, 0x8000};
        int x;
        y = (uint16_t *)(pic->data[0] + line * pic->linesize[0]);
        u = (uint16_t *)(pic->data[1] + line * pic->linesize[1]);
        v = (uint16_t *)(pic->data[2] + line * pic->linesize[2]);

        for (x = 0; x < width; x += 6) {
            uint32_t t;

            if (width - x < 6 || line_end - src < 16) {
                y = y_temp;
                u = u_temp;
                v = v_temp;
            }

            if (line_end - src < 4)
                break;

            t = AV_RL32(src);
            src += 4;
            *u++ = t <<  6 & 0xFFC0;
            *y++ = t >>  4 & 0xFFC0;
            *v++ = t >> 14 & 0xFFC0;

            if (line_end - src < 4)
                break;

            t = AV_RL32(src);
            src += 4;
            *y++ = t <<  6 & 0xFFC0;
            *u++ = t >>  4 & 0xFFC0;
            *y++ = t >> 14 & 0xFFC0;

            if (line_end - src < 4)
                break;

            t = AV_RL32(src);
            src += 4;
            *v++ = t <<  6 & 0xFFC0;
            *y++ = t >>  4 & 0xFFC0;
            *u++ = t >> 14 & 0xFFC0;

            if (line_end - src < 4)
                break;

            t = AV_RL32(src);
            src += 4;
            *y++ = t <<  6 & 0xFFC0;
            *v++ = t >>  4 & 0xFFC0;
            *y++ = t >> 14 & 0xFFC0;

            if (width - x < 6)
                break;
        }

        if (x < width) {
            y = x   + (uint16_t *)(pic->data[0] + line * pic->linesize[0]);
            u = x/2 + (uint16_t *)(pic->data[1] + line * pic->linesize[1]);
            v = x/2 + (uint16_t *)(pic->data[2] + line * pic->linesize[2]);
            memcpy(y, y_temp, sizeof(*y) * (width - x));
            memcpy(u, u_temp, sizeof(*u) * ((width - x + 1) / 2));
            memcpy(v, v_temp, sizeof(*v) * ((width - x + 1) / 2));
        }

        line_end += stride;
        src = line_end - stride;
    }

    *got_frame = 1;

    return avpkt->size;
}

const FFCodec ff_zero12v_decoder = {
    .p.name         = "012v",
    CODEC_LONG_NAME("Uncompressed 4:2:2 10-bit"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_012V,
    .init           = zero12v_decode_init,
    FF_CODEC_DECODE_CB(zero12v_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
};
