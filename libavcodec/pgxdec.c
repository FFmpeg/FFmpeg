/*
 * PGX image format
 * Copyright (c) 2020 Gautam Ramakrishnan
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
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"

static int pgx_get_number(AVCodecContext *avctx, GetByteContext *g, int *number) {
    int ret = AVERROR_INVALIDDATA;
    char digit;

    *number = 0;
    while (1) {
        uint64_t temp;
        if (bytestream2_get_bytes_left(g) <= 0)
            return AVERROR_INVALIDDATA;
        digit = bytestream2_get_byteu(g);
        if (digit == ' ' || digit == 0xA || digit == 0xD)
            break;
        else if (digit < '0' || digit > '9')
            return AVERROR_INVALIDDATA;

        temp = (uint64_t)10 * (*number) + (digit - '0');
        if (temp > INT_MAX)
            return AVERROR_INVALIDDATA;
        *number = temp;
        ret = 0;
    }

    return ret;
}

static int pgx_decode_header(AVCodecContext *avctx, GetByteContext *g,
                             int *depth, int *width, int *height,
                             int *sign)
{
    int byte;

    if (bytestream2_get_bytes_left(g) < 12)
        return AVERROR_INVALIDDATA;

    bytestream2_skipu(g, 6);

    // Is the component signed?
    byte = bytestream2_peek_byteu(g);
    if (byte == '+') {
        *sign = 0;
        bytestream2_skipu(g, 1);
    } else if (byte == '-') {
        *sign = 1;
        bytestream2_skipu(g, 1);
    }

    byte = bytestream2_peek_byteu(g);
    if (byte == ' ')
        bytestream2_skipu(g, 1);

    if (pgx_get_number(avctx, g, depth))
        goto error;
    if (pgx_get_number(avctx, g, width))
        goto error;
    if (pgx_get_number(avctx, g, height))
        goto error;

    if (bytestream2_peek_byte(g) == 0xA)
        bytestream2_skip(g, 1);
    return 0;

error:
    av_log(avctx, AV_LOG_ERROR, "Error in decoding header.\n");
    return AVERROR_INVALIDDATA;
}

#define WRITE_FRAME(D, PIXEL, suffix)                                                       \
    static inline void write_frame_ ##D(AVFrame *frame, GetByteContext *g, \
                                        int width, int height, int sign, int depth)         \
    {                                                                                       \
        const unsigned offset = sign ? (1 << (D - 1)) : 0;                                  \
        int i, j;                                                                           \
        for (i = 0; i < height; i++) {                                                      \
            PIXEL *line = (PIXEL*)(frame->data[0] + i * frame->linesize[0]);                \
            for (j = 0; j < width; j++) {                                                   \
                unsigned val = bytestream2_get_ ##suffix##u(g) << (D - depth);              \
                val ^= offset;                                                              \
                *(line + j) = val;                                                          \
            }                                                                               \
        }                                                                                   \
    }                                                                                       \

WRITE_FRAME(8, uint8_t, byte)
WRITE_FRAME(16, uint16_t, be16)

static int pgx_decode_frame(AVCodecContext *avctx, AVFrame *p,
                            int *got_frame, AVPacket *avpkt)
{
    int ret;
    int bpp;
    int width, height, depth;
    int sign = 0;
    GetByteContext g;
    bytestream2_init(&g, avpkt->data, avpkt->size);

    if ((ret = pgx_decode_header(avctx, &g, &depth, &width, &height, &sign)) < 0)
        return ret;

    if ((ret = ff_set_dimensions(avctx, width, height)) < 0)
        return ret;

    if (depth > 0 && depth <= 8) {
        avctx->pix_fmt = AV_PIX_FMT_GRAY8;
        bpp = 8;
    } else if (depth > 0 && depth <= 16) {
        avctx->pix_fmt = AV_PIX_FMT_GRAY16;
        bpp = 16;
    } else {
        av_log(avctx, AV_LOG_ERROR, "depth %d is invalid or unsupported.\n", depth);
        return AVERROR_PATCHWELCOME;
    }
    if (bytestream2_get_bytes_left(&g) < width * height * (bpp >> 3))
        return AVERROR_INVALIDDATA;
    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;
    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;
    avctx->bits_per_raw_sample = depth;
    if (bpp == 8)
        write_frame_8(p, &g, width, height, sign, depth);
    else if (bpp == 16)
        write_frame_16(p, &g, width, height, sign, depth);
    *got_frame = 1;
    return 0;
}

const FFCodec ff_pgx_decoder = {
    .p.name         = "pgx",
    CODEC_LONG_NAME("PGX (JPEG2000 Test Format)"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PGX,
    .p.capabilities = AV_CODEC_CAP_DR1,
    FF_CODEC_DECODE_CB(pgx_decode_frame),
};
