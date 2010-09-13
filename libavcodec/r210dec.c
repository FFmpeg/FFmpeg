/*
 * R210 decoder
 *
 * Copyright (c) 2009 Reimar Doeffinger <Reimar.Doeffinger@gmx.de>
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
#include "libavutil/bswap.h"

static av_cold int decode_init(AVCodecContext *avctx)
{
    avctx->pix_fmt             = PIX_FMT_RGB48;
    avctx->bits_per_raw_sample = 10;

    avctx->coded_frame         = avcodec_alloc_frame();

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                        AVPacket *avpkt)
{
    int h, w;
    AVFrame *pic = avctx->coded_frame;
    const uint32_t *src = (const uint32_t *)avpkt->data;
    int aligned_width = FFALIGN(avctx->width, 64);
    uint8_t *dst_line;

    if (pic->data[0])
        avctx->release_buffer(avctx, pic);

    if (avpkt->size < 4 * aligned_width * avctx->height) {
        av_log(avctx, AV_LOG_ERROR, "packet too small\n");
        return -1;
    }

    pic->reference = 0;
    if (avctx->get_buffer(avctx, pic) < 0)
        return -1;

    pic->pict_type = FF_I_TYPE;
    pic->key_frame = 1;
    dst_line = pic->data[0];

    for (h = 0; h < avctx->height; h++) {
        uint16_t *dst = (uint16_t *)dst_line;
        for (w = 0; w < avctx->width; w++) {
            uint32_t pixel = av_be2ne32(*src++);
            uint16_t r, g, b;
            if (avctx->codec_id==CODEC_ID_R210) {
                b =  pixel <<  6;
                g = (pixel >>  4) & 0xffc0;
                r = (pixel >> 14) & 0xffc0;
            } else {
                b =  pixel <<  4;
                g = (pixel >>  6) & 0xffc0;
                r = (pixel >> 16) & 0xffc0;
            }
            *dst++ = r | (r >> 10);
            *dst++ = g | (g >> 10);
            *dst++ = b | (b >> 10);
        }
        src += aligned_width - avctx->width;
        dst_line += pic->linesize[0];
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = *avctx->coded_frame;

    return avpkt->size;
}

static av_cold int decode_close(AVCodecContext *avctx)
{
    AVFrame *pic = avctx->coded_frame;
    if (pic->data[0])
        avctx->release_buffer(avctx, pic);
    av_freep(&avctx->coded_frame);

    return 0;
}

#if CONFIG_R210_DECODER
AVCodec r210_decoder = {
    "r210",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_R210,
    0,
    decode_init,
    NULL,
    decode_close,
    decode_frame,
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Uncompressed RGB 10-bit"),
};
#endif
#if CONFIG_R10K_DECODER
AVCodec r10k_decoder = {
    "r10k",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_R10K,
    0,
    decode_init,
    NULL,
    decode_close,
    decode_frame,
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("AJA Kona 10-bit RGB Codec"),
};
#endif
