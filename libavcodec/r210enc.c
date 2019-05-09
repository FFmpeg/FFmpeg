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
#include "internal.h"
#include "bytestream.h"

static av_cold int encode_init(AVCodecContext *avctx)
{
    int aligned_width = FFALIGN(avctx->width,
                                avctx->codec_id == AV_CODEC_ID_R10K ? 1 : 64);

    avctx->bits_per_coded_sample = 32;
    if (avctx->width > 0)
        avctx->bit_rate = ff_guess_coded_bitrate(avctx) * aligned_width / avctx->width;

    return 0;
}

static int encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *pic, int *got_packet)
{
    int i, j, ret;
    int aligned_width = FFALIGN(avctx->width,
                                avctx->codec_id == AV_CODEC_ID_R10K ? 1 : 64);
    int pad = (aligned_width - avctx->width) * 4;
    uint8_t *srcr_line, *srcg_line, *srcb_line;
    uint8_t *dst;

    if ((ret = ff_alloc_packet2(avctx, pkt, 4 * aligned_width * avctx->height, 0)) < 0)
        return ret;

    srcg_line = pic->data[0];
    srcb_line = pic->data[1];
    srcr_line = pic->data[2];
    dst = pkt->data;

    for (i = 0; i < avctx->height; i++) {
        uint16_t *srcr = (uint16_t *)srcr_line;
        uint16_t *srcg = (uint16_t *)srcg_line;
        uint16_t *srcb = (uint16_t *)srcb_line;
        for (j = 0; j < avctx->width; j++) {
            uint32_t pixel;
            uint16_t r = *srcr++;
            uint16_t g = *srcg++;
            uint16_t b = *srcb++;
            if (avctx->codec_id == AV_CODEC_ID_R210)
                pixel = (r << 20) | (g << 10) | b;
            else
                pixel = (r << 22) | (g << 12) | (b << 2);
            if (avctx->codec_id == AV_CODEC_ID_AVRP)
                bytestream_put_le32(&dst, pixel);
            else
                bytestream_put_be32(&dst, pixel);
        }
        memset(dst, 0, pad);
        dst += pad;
        srcr_line += pic->linesize[2];
        srcg_line += pic->linesize[0];
        srcb_line += pic->linesize[1];
    }

    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;
    return 0;
}


#if CONFIG_R210_ENCODER
AVCodec ff_r210_encoder = {
    .name           = "r210",
    .long_name      = NULL_IF_CONFIG_SMALL("Uncompressed RGB 10-bit"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_R210,
    .init           = encode_init,
    .encode2        = encode_frame,
    .pix_fmts       = (const enum AVPixelFormat[]) { AV_PIX_FMT_GBRP10, AV_PIX_FMT_NONE },
    .capabilities   = AV_CODEC_CAP_INTRA_ONLY,
};
#endif
#if CONFIG_R10K_ENCODER
AVCodec ff_r10k_encoder = {
    .name           = "r10k",
    .long_name      = NULL_IF_CONFIG_SMALL("AJA Kona 10-bit RGB Codec"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_R10K,
    .init           = encode_init,
    .encode2        = encode_frame,
    .pix_fmts       = (const enum AVPixelFormat[]) { AV_PIX_FMT_GBRP10, AV_PIX_FMT_NONE },
    .capabilities   = AV_CODEC_CAP_INTRA_ONLY,
};
#endif
#if CONFIG_AVRP_ENCODER
AVCodec ff_avrp_encoder = {
    .name           = "avrp",
    .long_name      = NULL_IF_CONFIG_SMALL("Avid 1:1 10-bit RGB Packer"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_AVRP,
    .init           = encode_init,
    .encode2        = encode_frame,
    .pix_fmts       = (const enum AVPixelFormat[]) { AV_PIX_FMT_GBRP10, AV_PIX_FMT_NONE },
    .capabilities   = AV_CODEC_CAP_INTRA_ONLY,
};
#endif
