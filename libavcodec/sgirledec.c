/*
 * Silicon Graphics RLE 8-bit video decoder
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
 * Silicon Graphics RLE 8-bit video decoder
 * @note Data is packed in rbg323 with rle, contained in mv or mov.
 * The algorithm and pixfmt are subtly different from SGI images.
 */

#include "libavutil/common.h"

#include "avcodec.h"
#include "internal.h"

static av_cold int sgirle_decode_init(AVCodecContext *avctx)
{
    avctx->pix_fmt = AV_PIX_FMT_BGR8;
    return 0;
}

/**
 * Convert SGI RBG323 pixel into AV_PIX_FMT_BGR8
 * SGI RGB data is packed as 8bpp, (msb)3R 2B 3G(lsb)
 */
#define RBG323_TO_BGR8(x) ((((x) << 3) & 0xC0) |                                \
                           (((x) << 3) & 0x38) |                                \
                           (((x) >> 5) & 7))
static av_always_inline
void rbg323_to_bgr8(uint8_t *dst, const uint8_t *src, int size)
{
    int i;
    for (i = 0; i < size; i++)
        dst[i] = RBG323_TO_BGR8(src[i]);
}

/**
 * @param[out] dst Destination buffer
 * @param[in] src  Source buffer
 * @param src_size Source buffer size (bytes)
 * @param width    Width of destination buffer (pixels)
 * @param height   Height of destination buffer (pixels)
 * @param linesize Line size of destination buffer (bytes)
 *
 * @return <0 on error
 */
static int decode_sgirle8(AVCodecContext *avctx, uint8_t *dst,
                          const uint8_t *src, int src_size,
                          int width, int height, ptrdiff_t linesize)
{
    const uint8_t *src_end = src + src_size;
    int x = 0, y = 0;

#define INC_XY(n)                                                             \
    x += n;                                                                   \
    if (x >= width) {                                                         \
        y++;                                                                  \
        if (y >= height)                                                      \
            return 0;                                                         \
        x = 0;                                                                \
    }

    while (src_end - src >= 2) {
        uint8_t v = *src++;
        if (v > 0 && v < 0xC0) {
            do {
                int length = FFMIN(v, width - x);
                if (length <= 0)
                    break;
                memset(dst + y * linesize + x, RBG323_TO_BGR8(*src), length);
                INC_XY(length);
                v -= length;
            } while (v > 0);
            src++;
        } else if (v >= 0xC1) {
            v -= 0xC0;
            do {
                int length = FFMIN3(v, width - x, src_end - src);
                if (src_end - src < length || length <= 0)
                    break;
                rbg323_to_bgr8(dst + y * linesize + x, src, length);
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

static int sgirle_decode_frame(AVCodecContext *avctx, void *data,
                               int *got_frame, AVPacket *avpkt)
{
    AVFrame *frame = data;
    int ret;

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    ret = decode_sgirle8(avctx, frame->data[0], avpkt->data, avpkt->size,
                         avctx->width, avctx->height, frame->linesize[0]);
    if (ret < 0)
        return ret;

    frame->pict_type = AV_PICTURE_TYPE_I;
    frame->key_frame = 1;

    *got_frame = 1;

    return avpkt->size;
}

AVCodec ff_sgirle_decoder = {
    .name           = "sgirle",
    .long_name      = NULL_IF_CONFIG_SMALL("Silicon Graphics RLE 8-bit video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_SGIRLE,
    .init           = sgirle_decode_init,
    .decode         = sgirle_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
