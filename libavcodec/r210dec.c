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
#include "internal.h"
#include "libavutil/bswap.h"
#include "libavutil/common.h"

static av_cold int decode_init(AVCodecContext *avctx)
{
    if ((avctx->codec_tag & 0xFFFFFF) == MKTAG('r', '1', '0', 0)) {
        avctx->pix_fmt = AV_PIX_FMT_BGR48;
    } else {
        avctx->pix_fmt = AV_PIX_FMT_RGB48;
    }
    avctx->bits_per_raw_sample = 10;

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                        AVPacket *avpkt)
{
    int h, w, ret;
    AVFrame *pic = data;
    const uint32_t *src = (const uint32_t *)avpkt->data;
    int aligned_width = FFALIGN(avctx->width,
                                avctx->codec_id == AV_CODEC_ID_R10K ? 1 : 64);
    uint8_t *dst_line;
    int r10 = (avctx->codec_tag & 0xFFFFFF) == MKTAG('r', '1', '0', 0);
    int le = avctx->codec_tag == MKTAG('R', '1', '0', 'k') &&
             avctx->extradata_size >= 12 && !memcmp(&avctx->extradata[4], "DpxE", 4) &&
             !avctx->extradata[11];

    if (avpkt->size < 4 * aligned_width * avctx->height) {
        av_log(avctx, AV_LOG_ERROR, "packet too small\n");
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_get_buffer(avctx, pic, 0)) < 0)
        return ret;

    pic->pict_type = AV_PICTURE_TYPE_I;
    pic->key_frame = 1;
    dst_line = pic->data[0];

    for (h = 0; h < avctx->height; h++) {
        uint16_t *dst = (uint16_t *)dst_line;
        for (w = 0; w < avctx->width; w++) {
            uint32_t pixel;
            uint16_t r, g, b;
            if (avctx->codec_id == AV_CODEC_ID_AVRP || r10 || le) {
                pixel = av_le2ne32(*src++);
            } else {
                pixel = av_be2ne32(*src++);
            }
            if (avctx->codec_id == AV_CODEC_ID_R210 || r10) {
                b =  pixel <<  6;
                g = (pixel >>  4) & 0xffc0;
                r = (pixel >> 14) & 0xffc0;
            } else {
                b = (pixel <<  4) & 0xffc0;
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

    *got_frame      = 1;

    return avpkt->size;
}

#if CONFIG_R210_DECODER
AVCodec ff_r210_decoder = {
    .name           = "r210",
    .long_name      = NULL_IF_CONFIG_SMALL("Uncompressed RGB 10-bit"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_R210,
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};
#endif
#if CONFIG_R10K_DECODER
AVCodec ff_r10k_decoder = {
    .name           = "r10k",
    .long_name      = NULL_IF_CONFIG_SMALL("AJA Kona 10-bit RGB Codec"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_R10K,
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};
#endif
#if CONFIG_AVRP_DECODER
AVCodec ff_avrp_decoder = {
    .name           = "avrp",
    .long_name      = NULL_IF_CONFIG_SMALL("Avid 1:1 10-bit RGB Packer"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_AVRP,
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};
#endif
