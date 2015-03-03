/*
 * V210 encoder
 *
 * Copyright (C) 2009 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2009 Baptiste Coudurier <baptiste dot coudurier at gmail dot com>
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
#include "internal.h"
#include "v210enc.h"

#define CLIP(v) av_clip(v, 4, 1019)
#define CLIP8(v) av_clip(v, 1, 254)

#define WRITE_PIXELS(a, b, c)           \
    do {                                \
        val  =  CLIP(*a++);             \
        val |= (CLIP(*b++) << 10) |     \
               (CLIP(*c++) << 20);      \
        AV_WL32(dst, val);              \
        dst += 4;                       \
    } while (0)

#define WRITE_PIXELS8(a, b, c)          \
    do {                                \
        val  = (CLIP8(*a++) << 2);      \
        val |= (CLIP8(*b++) << 12) |    \
               (CLIP8(*c++) << 22);     \
        AV_WL32(dst, val);              \
        dst += 4;                       \
    } while (0)

static void v210_planar_pack_8_c(const uint8_t *y, const uint8_t *u,
                                 const uint8_t *v, uint8_t *dst,
                                 ptrdiff_t width)
{
    uint32_t val;
    int i;

    /* unroll this to match the assembly */
    for (i = 0; i < width - 11; i += 12) {
        WRITE_PIXELS8(u, y, v);
        WRITE_PIXELS8(y, u, y);
        WRITE_PIXELS8(v, y, u);
        WRITE_PIXELS8(y, v, y);
        WRITE_PIXELS8(u, y, v);
        WRITE_PIXELS8(y, u, y);
        WRITE_PIXELS8(v, y, u);
        WRITE_PIXELS8(y, v, y);
    }
}

static void v210_planar_pack_10_c(const uint16_t *y, const uint16_t *u,
                                  const uint16_t *v, uint8_t *dst,
                                  ptrdiff_t width)
{
    uint32_t val;
    int i;

    for (i = 0; i < width - 5; i += 6) {
        WRITE_PIXELS(u, y, v);
        WRITE_PIXELS(y, u, y);
        WRITE_PIXELS(v, y, u);
        WRITE_PIXELS(y, v, y);
    }
}

static av_cold int encode_init(AVCodecContext *avctx)
{
    V210EncContext *s = avctx->priv_data;

    if (avctx->width & 1) {
        av_log(avctx, AV_LOG_ERROR, "v210 needs even width\n");
        return AVERROR(EINVAL);
    }

    avctx->coded_frame = av_frame_alloc();
    if (!avctx->coded_frame)
        return AVERROR(ENOMEM);

    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;

    s->pack_line_8  = v210_planar_pack_8_c;
    s->pack_line_10 = v210_planar_pack_10_c;

    if (ARCH_X86)
        ff_v210enc_init_x86(s);

    return 0;
}

static int encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *pic, int *got_packet)
{
    V210EncContext *s = avctx->priv_data;
    int aligned_width = ((avctx->width + 47) / 48) * 48;
    int stride = aligned_width * 8 / 3;
    int line_padding = stride - ((avctx->width * 8 + 11) / 12) * 4;
    int h, w, ret;
    uint8_t *dst;

    ret = ff_alloc_packet(pkt, avctx->height * stride);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error getting output packet.\n");
        return ret;
    }
    dst = pkt->data;

    if (pic->format == AV_PIX_FMT_YUV422P10) {
        const uint16_t *y = (const uint16_t *)pic->data[0];
        const uint16_t *u = (const uint16_t *)pic->data[1];
        const uint16_t *v = (const uint16_t *)pic->data[2];
        for (h = 0; h < avctx->height; h++) {
            uint32_t val;
            w = (avctx->width / 6) * 6;
            s->pack_line_10(y, u, v, dst, w);

            y += w;
            u += w >> 1;
            v += w >> 1;
            dst += (w / 6) * 16;
            if (w < avctx->width - 1) {
                WRITE_PIXELS(u, y, v);

                val = CLIP(*y++);
                if (w == avctx->width - 2) {
                    AV_WL32(dst, val);
                    dst += 4;
                }
            }
            if (w < avctx->width - 3) {
                val |= (CLIP(*u++) << 10) | (CLIP(*y++) << 20);
                AV_WL32(dst, val);
                dst += 4;

                val = CLIP(*v++) | (CLIP(*y++) << 10);
                AV_WL32(dst, val);
                dst += 4;
            }

            memset(dst, 0, line_padding);
            dst += line_padding;
            y += pic->linesize[0] / 2 - avctx->width;
            u += pic->linesize[1] / 2 - avctx->width / 2;
            v += pic->linesize[2] / 2 - avctx->width / 2;
        }
    } else if(pic->format == AV_PIX_FMT_YUV422P) {
        const uint8_t *y = pic->data[0];
        const uint8_t *u = pic->data[1];
        const uint8_t *v = pic->data[2];
        for (h = 0; h < avctx->height; h++) {
            uint32_t val;
            w = (avctx->width / 12) * 12;
            s->pack_line_8(y, u, v, dst, w);

            y += w;
            u += w >> 1;
            v += w >> 1;
            dst += (w / 12) * 32;

            for (; w < avctx->width - 5; w += 6) {
                WRITE_PIXELS8(u, y, v);
                WRITE_PIXELS8(y, u, y);
                WRITE_PIXELS8(v, y, u);
                WRITE_PIXELS8(y, v, y);
            }
            if (w < avctx->width - 1) {
                WRITE_PIXELS8(u, y, v);

                val = CLIP8(*y++) << 2;
                if (w == avctx->width - 2) {
                    AV_WL32(dst, val);
                    dst += 4;
                }
            }
            if (w < avctx->width - 3) {
                val |= (CLIP8(*u++) << 12) | (CLIP8(*y++) << 22);
                AV_WL32(dst, val);
                dst += 4;

                val = (CLIP8(*v++) << 2) | (CLIP8(*y++) << 12);
                AV_WL32(dst, val);
                dst += 4;
            }
            memset(dst, 0, line_padding);
            dst += line_padding;

            y += pic->linesize[0] - avctx->width;
            u += pic->linesize[1] - avctx->width / 2;
            v += pic->linesize[2] - avctx->width / 2;
        }
    }

    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;
    return 0;
}

static av_cold int encode_close(AVCodecContext *avctx)
{
    av_frame_free(&avctx->coded_frame);

    return 0;
}

AVCodec ff_v210_encoder = {
    .name           = "v210",
    .long_name      = NULL_IF_CONFIG_SMALL("Uncompressed 4:2:2 10-bit"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_V210,
    .priv_data_size = sizeof(V210EncContext),
    .init           = encode_init,
    .encode2        = encode_frame,
    .close          = encode_close,
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV422P, AV_PIX_FMT_NONE },
};
