/*
 * Microsoft Paint (MSP) version 2 decoder
 * Copyright (c) 2020 Peter Ross (pross@xvid.org)
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
 * Microsoft Paint (MSP) version 2 decoder
 */

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "internal.h"

static int msp2_decode_frame(AVCodecContext *avctx, AVFrame *p,
                             int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    int ret;
    unsigned int x, y, width = (avctx->width + 7) / 8;
    GetByteContext idx, gb;

    if (buf_size <= 2 * avctx->height)
        return AVERROR_INVALIDDATA;

    avctx->pix_fmt = AV_PIX_FMT_MONOBLACK;

    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;

    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;

    bytestream2_init(&idx, buf, 2 * avctx->height);
    buf += 2 * avctx->height;
    buf_size -= 2 * avctx->height;

    for (y = 0; y < avctx->height; y++) {
        unsigned int pkt_size = bytestream2_get_le16(&idx);
        if (!pkt_size) {
            memset(p->data[0] + y * p->linesize[0], 0xFF, width);
            continue;
        }

        if (pkt_size > buf_size) {
            av_log(avctx, AV_LOG_WARNING, "image probably corrupt\n");
            pkt_size = buf_size;
        }

        bytestream2_init(&gb, buf, pkt_size);
        x = 0;
        while (bytestream2_get_bytes_left(&gb) && x < width) {
            int size = bytestream2_get_byte(&gb);
            if (size) {
                size = FFMIN(size, bytestream2_get_bytes_left(&gb));
                memcpy(p->data[0] + y * p->linesize[0] + x, gb.buffer, FFMIN(size, width - x));
                bytestream2_skip(&gb, size);
            } else {
                int value;
                size = bytestream2_get_byte(&gb);
                if (!size)
                    avpriv_request_sample(avctx, "escape value");
                value = bytestream2_get_byte(&gb);
                memset(p->data[0] + y * p->linesize[0] + x, value, FFMIN(size, width - x));
            }
            x += size;
        }

        buf += pkt_size;
        buf_size -= pkt_size;
    }

    *got_frame = 1;
    return buf_size;
}

const FFCodec ff_msp2_decoder = {
    .p.name         = "msp2",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Microsoft Paint (MSP) version 2"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MSP2,
    .p.capabilities = AV_CODEC_CAP_DR1,
    FF_CODEC_DECODE_CB(msp2_decode_frame),
};
