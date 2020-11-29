/*
 * Silicon Graphics Motion Video Compressor 1 & 2 decoder
 * Copyright (c) 2012 Peter Ross
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
 * Silicon Graphics Motion Video Compressor 1 & 2 decoder
 */

#include "libavutil/intreadwrite.h"

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"

typedef struct MvcContext {
    int vflip;
} MvcContext;

static av_cold int mvc_decode_init(AVCodecContext *avctx)
{
    MvcContext *s = avctx->priv_data;
    int width     = avctx->width;
    int height    = avctx->height;
    int ret;

    if (avctx->codec_id == AV_CODEC_ID_MVC1) {
        width  += 3;
        height += 3;
    }
    width  &= ~3;
    height &= ~3;
    if ((ret = ff_set_dimensions(avctx, width, height)) < 0)
        return ret;

    avctx->pix_fmt = (avctx->codec_id == AV_CODEC_ID_MVC1) ? AV_PIX_FMT_RGB555
                                                           : AV_PIX_FMT_RGB32;
    s->vflip = avctx->extradata_size >= 9 &&
               !memcmp(avctx->extradata + avctx->extradata_size - 9, "BottomUp", 9);
    return 0;
}

static int decode_mvc1(AVCodecContext *avctx, GetByteContext *gb,
                       uint8_t *dst_start, int width, int height, int linesize)
{
    uint8_t *dst;
    uint16_t v[8];
    int mask, x, y, i;

    for (y = 0; y < height; y += 4) {
        for (x = 0; x < width; x += 4) {
            if (bytestream2_get_bytes_left(gb) < 6)
                return 0;

            mask = bytestream2_get_be16u(gb);
            v[0] = bytestream2_get_be16u(gb);
            v[1] = bytestream2_get_be16u(gb);
            if ((v[0] & 0x8000)) {
                if (bytestream2_get_bytes_left(gb) < 12) {
                    av_log(avctx, AV_LOG_WARNING, "buffer overflow\n");
                    return AVERROR_INVALIDDATA;
                }
                for (i = 2; i < 8; i++)
                    v[i] = bytestream2_get_be16u(gb);
            } else {
                v[2] = v[4] = v[6] = v[0];
                v[3] = v[5] = v[7] = v[1];
            }

#define PIX16(target, true, false)                                            \
    i = (mask & target) ? true : false;                                       \
    AV_WN16A(dst, v[i] & 0x7FFF);                                             \
    dst += 2;

#define ROW16(row, a1, a0, b1, b0)                                            \
    dst = dst_start + (y + row) * linesize + x * 2;                           \
    PIX16(1 << (row * 4), a1, a0)                                             \
    PIX16(1 << (row * 4 + 1), a1, a0)                                         \
    PIX16(1 << (row * 4 + 2), b1, b0)                                         \
    PIX16(1 << (row * 4 + 3), b1, b0)

            ROW16(0, 0, 1, 2, 3);
            ROW16(1, 0, 1, 2, 3);
            ROW16(2, 4, 5, 6, 7);
            ROW16(3, 4, 5, 6, 7);
        }
    }
    return 0;
}

static void set_4x4_block(uint8_t *dst, int linesize, uint32_t pixel)
{
    int i, j;
    for (j = 0; j < 4; j++)
        for (i = 0; i < 4; i++)
            AV_WN32A(dst + j * linesize + i * 4, pixel);
}

#define PIX32(target, true, false)                                            \
    AV_WN32A(dst, (mask & target) ? v[true] : v[false]);                      \
    dst += 4;

#define ROW32(row, a1, a0, b1, b0)                                            \
    dst = dst_start + (y + row) * linesize + x * 4;                           \
    PIX32(1 << (row * 4), a1, a0)                                             \
    PIX32(1 << (row * 4 + 1), a1, a0)                                         \
    PIX32(1 << (row * 4 + 2), b1, b0)                                         \
    PIX32(1 << (row * 4 + 3), b1, b0)

#define MVC2_BLOCK                                                            \
    ROW32(0, 1, 0, 3, 2);                                                     \
    ROW32(1, 1, 0, 3, 2);                                                     \
    ROW32(2, 5, 4, 7, 6);                                                     \
    ROW32(3, 5, 4, 7, 6);

static int decode_mvc2(AVCodecContext *avctx, GetByteContext *gb,
                       uint8_t *dst_start, int width, int height,
                       int linesize, int vflip)
{
    uint8_t *dst;
    uint32_t color[128], v[8];
    int w, h, nb_colors, i, x, y, p0, p1, mask;

    if (bytestream2_get_bytes_left(gb) < 6)
        return AVERROR_INVALIDDATA;

    w = bytestream2_get_be16u(gb);
    h = bytestream2_get_be16u(gb);
    if ((w & ~3) != width || (h & ~3) != height)
        av_log(avctx, AV_LOG_WARNING, "dimension mismatch\n");

    if (bytestream2_get_byteu(gb)) {
        avpriv_request_sample(avctx, "bitmap feature");
        return AVERROR_PATCHWELCOME;
    }

    nb_colors = bytestream2_get_byteu(gb);
    if (bytestream2_get_bytes_left(gb) < nb_colors * 3)
        return AVERROR_INVALIDDATA;
    for (i = 0; i < FFMIN(nb_colors, 128); i++)
        color[i] = 0xFF000000 | bytestream2_get_be24u(gb);
    if (nb_colors > 128)
        bytestream2_skip(gb, (nb_colors - 128) * 3);

    if (vflip) {
        dst_start += (height - 1) * linesize;
        linesize   = -linesize;
    }
    x = y = 0;
    while (bytestream2_get_bytes_left(gb) >= 1) {
        p0 = bytestream2_get_byteu(gb);
        if ((p0 & 0x80)) {
            if ((p0 & 0x40)) {
                p0 &= 0x3F;
                p0  = (p0 << 2) | (p0 >> 4);
                set_4x4_block(dst_start + y * linesize + x * 4, linesize,
                              0xFF000000 | (p0 << 16) | (p0 << 8) | p0);
            } else {
                int g, r;
                p0 &= 0x3F;
                p0  = (p0 << 2) | (p0 >> 4);
                if (bytestream2_get_bytes_left(gb) < 2)
                    return AVERROR_INVALIDDATA;
                g = bytestream2_get_byteu(gb);
                r = bytestream2_get_byteu(gb);
                set_4x4_block(dst_start + y * linesize + x * 4, linesize,
                              0xFF000000 | (r << 16) | (g << 8) | p0);
            }
        } else {
            if (bytestream2_get_bytes_left(gb) < 1)
                return AVERROR_INVALIDDATA;
            p1 = bytestream2_get_byteu(gb);
            if ((p1 & 0x80)) {
                if ((p0 & 0x7F) == (p1 & 0x7F)) {
                    set_4x4_block(dst_start + y * linesize + x * 4, linesize,
                                  color[p0 & 0x7F]);
                } else {
                    if (bytestream2_get_bytes_left(gb) < 2)
                        return AVERROR_INVALIDDATA;
                    v[0] = v[2] = v[4] = v[6] = color[p0 & 0x7F];
                    v[1] = v[3] = v[5] = v[7] = color[p1 & 0x7F];
                    mask = bytestream2_get_le16u(gb);
                    MVC2_BLOCK
                }
            } else {
                if (bytestream2_get_bytes_left(gb) < 8)
                    return AVERROR_INVALIDDATA;
                v[0] = color[p0 & 0x7F];
                v[1] = color[p1 & 0x7F];
                for (i = 2; i < 8; i++)
                    v[i] = color[bytestream2_get_byteu(gb) & 0x7F];
                mask = bytestream2_get_le16u(gb);
                MVC2_BLOCK
            }
        }

        x += 4;
        if (x >= width) {
            y += 4;
            if (y >= height)
                break;
            x = 0;
        }
    }
    return 0;
}

static int mvc_decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                            AVPacket *avpkt)
{
    MvcContext *s = avctx->priv_data;
    AVFrame *frame = data;
    GetByteContext gb;
    int ret;

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    bytestream2_init(&gb, avpkt->data, avpkt->size);
    if (avctx->codec_id == AV_CODEC_ID_MVC1)
        ret = decode_mvc1(avctx, &gb, frame->data[0],
                          avctx->width, avctx->height, frame->linesize[0]);
    else
        ret = decode_mvc2(avctx, &gb, frame->data[0],
                          avctx->width, avctx->height, frame->linesize[0],
                          s->vflip);
    if (ret < 0)
        return ret;

    frame->pict_type = AV_PICTURE_TYPE_I;
    frame->key_frame = 1;

    *got_frame = 1;

    return avpkt->size;
}

#if CONFIG_MVC1_DECODER
const AVCodec ff_mvc1_decoder = {
    .name           = "mvc1",
    .long_name      = NULL_IF_CONFIG_SMALL("Silicon Graphics Motion Video Compressor 1"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MVC1,
    .priv_data_size = sizeof(MvcContext),
    .init           = mvc_decode_init,
    .decode         = mvc_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
#endif

#if CONFIG_MVC2_DECODER
const AVCodec ff_mvc2_decoder = {
    .name           = "mvc2",
    .long_name      = NULL_IF_CONFIG_SMALL("Silicon Graphics Motion Video Compressor 2"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MVC2,
    .priv_data_size = sizeof(MvcContext),
    .init           = mvc_decode_init,
    .decode         = mvc_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
#endif
