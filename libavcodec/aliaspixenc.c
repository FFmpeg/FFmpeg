/*
 * Alias PIX image encoder
 * Copyright (C) 2014 Vittorio Giovara <vittorio.giovara@gmail.com>
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
#include "bytestream.h"
#include "codec_internal.h"
#include "encode.h"

#define ALIAS_HEADER_SIZE 10

static int encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *frame, int *got_packet)
{
    int width, height, bits_pixel, length, ret;
    uint8_t *buf;

    width  = avctx->width;
    height = avctx->height;

    if (width > 65535 || height > 65535 ||
        width * height >= INT_MAX / 4 - ALIAS_HEADER_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "Invalid image size %dx%d.\n", width, height);
        return AVERROR_INVALIDDATA;
    }

    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_GRAY8:
        bits_pixel = 8;
        break;
    case AV_PIX_FMT_BGR24:
        bits_pixel = 24;
        break;
    default:
        return AVERROR(EINVAL);
    }

    length = ALIAS_HEADER_SIZE + 4 * width * height; // max possible
    if ((ret = ff_alloc_packet(avctx, pkt, length)) < 0)
        return ret;

    buf = pkt->data;

    /* Encode header. */
    bytestream_put_be16(&buf, width);
    bytestream_put_be16(&buf, height);
    bytestream_put_be32(&buf, 0); /* X, Y offset */
    bytestream_put_be16(&buf, bits_pixel);

    for (int j = 0, bytes_pixel = bits_pixel >> 3; j < height; j++) {
        const uint8_t *in_buf = frame->data[0] + frame->linesize[0] * j;
        const uint8_t *const line_end = in_buf + bytes_pixel * width;
        while (in_buf < line_end) {
            int count = 0;
            int pixel;

            if (avctx->pix_fmt == AV_PIX_FMT_GRAY8) {
                pixel = *in_buf;
                while (count < 255 && in_buf < line_end && pixel == *in_buf) {
                    count++;
                    in_buf++;
                }
                bytestream_put_byte(&buf, count);
                bytestream_put_byte(&buf, pixel);
            } else { /* AV_PIX_FMT_BGR24 */
                pixel = AV_RB24(in_buf);
                while (count < 255 && in_buf < line_end &&
                       pixel == AV_RB24(in_buf)) {
                    count++;
                    in_buf += 3;
                }
                bytestream_put_byte(&buf, count);
                bytestream_put_be24(&buf, pixel);
            }
        }
    }

    /* Total length */
    av_shrink_packet(pkt, buf - pkt->data);
    *got_packet = 1;

    return 0;
}

const FFCodec ff_alias_pix_encoder = {
    .p.name    = "alias_pix",
    CODEC_LONG_NAME("Alias/Wavefront PIX image"),
    .p.type    = AVMEDIA_TYPE_VIDEO,
    .p.id      = AV_CODEC_ID_ALIAS_PIX,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    FF_CODEC_ENCODE_CB(encode_frame),
    CODEC_PIXFMTS(AV_PIX_FMT_BGR24, AV_PIX_FMT_GRAY8),
};
