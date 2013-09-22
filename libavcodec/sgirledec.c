/*
 * SGI RLE 8-bit decoder
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
 * SGI RLE 8-bit decoder
 */

#include "avcodec.h"
#include "internal.h"

typedef struct SGIRLEContext {
    AVFrame *frame;
} SGIRLEContext;

static av_cold int sgirle_decode_init(AVCodecContext *avctx)
{
    SGIRLEContext *s = avctx->priv_data;
    avctx->pix_fmt = AV_PIX_FMT_BGR8;
    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);
    return 0;
}

/**
 * Convert SGI RGB332 pixel into AV_PIX_FMT_BGR8
 * SGI RGB332 is packed RGB 3:3:2, 8bpp, (msb)3R 2B 3G(lsb)
 */
#define RGB332_TO_BGR8(x) (((x << 3) & 0xC0) | ((x << 3) & 0x38) | ((x >> 5) & 7))

static av_always_inline void memcpy_rgb332_to_bgr8(uint8_t *dst, const uint8_t *src, int size)
{
    int i;
    for (i = 0; i < size; i++)
        dst[i] = RGB332_TO_BGR8(src[i]);
}

/**
 * @param[out] dst Destination buffer
 * @param[in] src Source buffer
 * @param src_size Source buffer size (bytes)
 * @param width Width of destination buffer (pixels)
 * @param height Height of destination buffer (pixels)
 * @param linesize Line size of destination buffer (bytes)
 * @return <0 on error
 */
static int decode_sgirle8(AVCodecContext *avctx, uint8_t *dst, const uint8_t *src, int src_size, int width, int height, int linesize)
{
    const uint8_t *src_end = src + src_size;
    int x = 0, y = 0;

#define INC_XY(n) \
    x += n; \
    if (x >= width) { \
        y++; \
        if (y >= height) \
            return 0; \
        x = 0; \
    }

    while (src_end - src >= 2) {
        uint8_t v = *src++;
        if (v > 0 && v < 0xC0) {
            do {
                int length = FFMIN(v, width - x);
                if (length <= 0)
                    break;
                memset(dst + y*linesize + x, RGB332_TO_BGR8(*src), length);
                INC_XY(length);
                v   -= length;
            } while (v > 0);
            src++;
        } else if (v >= 0xC1) {
            v -= 0xC0;
            do {
                int length = FFMIN3(v, width - x, src_end - src);
                if (src_end - src < length || length <= 0)
                    break;
                memcpy_rgb332_to_bgr8(dst + y*linesize + x, src, length);
                INC_XY(length);
                src += length;
                v   -= length;
            } while (v > 0);
        } else {
            avpriv_request_sample(avctx, "opcode %d", v);
            return AVERROR_PATCHWELCOME;
        }
    }
    return 0;
}

static int sgirle_decode_frame(AVCodecContext *avctx,
                            void *data, int *got_frame,
                            AVPacket *avpkt)
{
    SGIRLEContext *s = avctx->priv_data;
    int ret;

    if ((ret = ff_reget_buffer(avctx, s->frame)) < 0)
        return ret;

    ret = decode_sgirle8(avctx, s->frame->data[0], avpkt->data, avpkt->size, avctx->width, avctx->height, s->frame->linesize[0]);
    if (ret < 0)
        return ret;

    *got_frame      = 1;
    if ((ret = av_frame_ref(data, s->frame)) < 0)
        return ret;

    return avpkt->size;
}

static av_cold int sgirle_decode_end(AVCodecContext *avctx)
{
    SGIRLEContext *s = avctx->priv_data;

    av_frame_free(&s->frame);

    return 0;
}

AVCodec ff_sgirle_decoder = {
    .name           = "sgirle",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_SGIRLE,
    .priv_data_size = sizeof(SGIRLEContext),
    .init           = sgirle_decode_init,
    .close          = sgirle_decode_end,
    .decode         = sgirle_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("SGI RLE 8-bit"),
};
