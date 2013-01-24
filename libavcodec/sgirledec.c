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

#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "bytestream.h"

static av_cold int sgirle_decode_init(AVCodecContext *avctx)
{
    avctx->pix_fmt = AV_PIX_FMT_BGR8;
    avctx->coded_frame = avcodec_alloc_frame();
    if (!avctx->coded_frame)
        return AVERROR(ENOMEM);
    return 0;
}

/**
 * Convert SGI RGB332 pixel into PIX_FMT_BGR8
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
                memset(dst + y*linesize + x, RGB332_TO_BGR8(*src), length);
                INC_XY(length);
                v   -= length;
            } while (v > 0);
            src++;
        } else if (v >= 0xC1) {
            v -= 0xC0;
            do {
                int length = FFMIN3(v, width - x, src_end - src);
                if (src_end - src < length)
                    break;
                memcpy_rgb332_to_bgr8(dst + y*linesize + x, src, length);
                INC_XY(length);
                src += length;
                v   -= length;
            } while (v > 0);
        } else {
            av_log_ask_for_sample(avctx, "unknown opcode\n");
            return AVERROR_PATCHWELCOME;
        }
    }
    return 0;
}

static int sgirle_decode_frame(AVCodecContext *avctx,
                            void *data, int *got_frame,
                            AVPacket *avpkt)
{
    AVFrame *frame = avctx->coded_frame;
    int ret;

    frame->reference = 3;
    frame->buffer_hints = FF_BUFFER_HINTS_VALID | FF_BUFFER_HINTS_PRESERVE |
                          FF_BUFFER_HINTS_REUSABLE | FF_BUFFER_HINTS_READABLE;
    ret = avctx->reget_buffer(avctx, frame);
    if (ret < 0) {
        av_log (avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return ret;
    }

    ret = decode_sgirle8(avctx, frame->data[0], avpkt->data, avpkt->size, avctx->width, avctx->height, frame->linesize[0]);
    if (ret < 0)
        return ret;

    *got_frame      = 1;
    *(AVFrame*)data = *frame;

    return avpkt->size;
}

static av_cold int sgirle_decode_end(AVCodecContext *avctx)
{
    if (avctx->coded_frame->data[0])
        avctx->release_buffer(avctx, avctx->coded_frame);
    av_freep(&avctx->coded_frame);
    return 0;
}

AVCodec ff_sgirle_decoder = {
    .name           = "sgirle",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_SGIRLE,
    .init           = sgirle_decode_init,
    .close          = sgirle_decode_end,
    .decode         = sgirle_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("SGI RLE 8-bit"),
};
