/*
 * R210 encoder
 *
 * Copyright (c) 2012 Paul B Mahol
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

static av_cold int encode_init(AVCodecContext *avctx)
{
    avctx->coded_frame = avcodec_alloc_frame();

    if (!avctx->coded_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static int encode_frame(AVCodecContext *avctx, uint8_t *buf,
                        int buf_size, void *data)
{
    AVFrame *pic = data;
    int i, j;
    int aligned_width = FFALIGN(avctx->width, 64);
    uint8_t *src_line;
    uint8_t *dst = buf;

    if (buf_size < 4 * aligned_width * avctx->height) {
        av_log(avctx, AV_LOG_ERROR, "output buffer too small\n");
        return AVERROR(ENOMEM);
    }

    avctx->coded_frame->reference = 0;
    avctx->coded_frame->key_frame = 1;
    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;
    src_line = pic->data[0];

    for (i = 0; i < avctx->height; i++) {
        uint16_t *src = (uint16_t *)src_line;
        for (j = 0; j < avctx->width; j++) {
            uint32_t pixel;
            uint16_t r = *src++ >> 6;
            uint16_t g = *src++ >> 6;
            uint16_t b = *src++ >> 4;
            if (avctx->codec_id == CODEC_ID_R210)
                pixel = (r << 20) | (g << 10) | b >> 2;
            else
                pixel = (r << 22) | (g << 12) | b;
            if (avctx->codec_id == CODEC_ID_AVRP)
                bytestream_put_le32(&dst, pixel);
            else
                bytestream_put_be32(&dst, pixel);
        }
        dst += aligned_width - avctx->width;
        src_line += pic->linesize[0];
    }

    return 4 * aligned_width * avctx->height;
}

static av_cold int encode_close(AVCodecContext *avctx)
{
    av_freep(&avctx->coded_frame);

    return 0;
}

#if CONFIG_R210_ENCODER
AVCodec ff_r210_encoder = {
    .name           = "r210",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_R210,
    .init           = encode_init,
    .encode         = encode_frame,
    .close          = encode_close,
    .pix_fmts       = (const enum PixelFormat[]) { PIX_FMT_RGB48, PIX_FMT_NONE },
    .long_name      = NULL_IF_CONFIG_SMALL("Uncompressed RGB 10-bit"),
};
#endif
#if CONFIG_R10K_ENCODER
AVCodec ff_r10k_encoder = {
    .name           = "r10k",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_R10K,
    .init           = encode_init,
    .encode         = encode_frame,
    .close          = encode_close,
    .pix_fmts       = (const enum PixelFormat[]) { PIX_FMT_RGB48, PIX_FMT_NONE },
    .long_name      = NULL_IF_CONFIG_SMALL("AJA Kona 10-bit RGB Codec"),
};
#endif
#if CONFIG_AVRP_ENCODER
AVCodec ff_avrp_encoder = {
    .name           = "avrp",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_AVRP,
    .init           = encode_init,
    .encode         = encode_frame,
    .close          = encode_close,
    .pix_fmts       = (const enum PixelFormat[]) { PIX_FMT_RGB48, PIX_FMT_NONE },
    .long_name      = NULL_IF_CONFIG_SMALL("Avid 1:1 10-bit RGB Packer"),
};
#endif
