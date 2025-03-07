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

#include "config_components.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"
#include "internal.h"
#include "bytestream.h"

static av_cold int encode_init(AVCodecContext *avctx)
{
    int aligned_width = FFALIGN(avctx->width,
                                avctx->codec_id == AV_CODEC_ID_R10K ? 1 : 64);

    avctx->bits_per_coded_sample = 32;
    if (avctx->width > 0)
        avctx->bit_rate = av_rescale(ff_guess_coded_bitrate(avctx), aligned_width, avctx->width);

    return 0;
}

static int encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *pic, int *got_packet)
{
    int i, j, ret;
    int aligned_width = FFALIGN(avctx->width,
                                avctx->codec_id == AV_CODEC_ID_R10K ? 1 : 64);
    int pad = (aligned_width - avctx->width) * 4;
    const uint8_t *srcr_line, *srcg_line, *srcb_line;
    uint8_t *dst;

    ret = ff_get_encode_buffer(avctx, pkt, 4 * aligned_width * avctx->height, 0);
    if (ret < 0)
        return ret;

    srcg_line = pic->data[0];
    srcb_line = pic->data[1];
    srcr_line = pic->data[2];
    dst = pkt->data;

    for (i = 0; i < avctx->height; i++) {
        const uint16_t *srcr = (const uint16_t *)srcr_line;
        const uint16_t *srcg = (const uint16_t *)srcg_line;
        const uint16_t *srcb = (const uint16_t *)srcb_line;
        for (j = 0; j < avctx->width; j++) {
            uint32_t pixel;
            unsigned r = *srcr++;
            unsigned g = *srcg++;
            unsigned b = *srcb++;
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

    *got_packet = 1;
    return 0;
}

static const enum AVPixelFormat pix_fmt[] = { AV_PIX_FMT_GBRP10, AV_PIX_FMT_NONE };

#if CONFIG_R210_ENCODER
const FFCodec ff_r210_encoder = {
    .p.name         = "r210",
    CODEC_LONG_NAME("Uncompressed RGB 10-bit"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_R210,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .init           = encode_init,
    FF_CODEC_ENCODE_CB(encode_frame),
    CODEC_PIXFMTS_ARRAY(pix_fmt),
};
#endif
#if CONFIG_R10K_ENCODER
const FFCodec ff_r10k_encoder = {
    .p.name         = "r10k",
    CODEC_LONG_NAME("AJA Kona 10-bit RGB Codec"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_R10K,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .init           = encode_init,
    FF_CODEC_ENCODE_CB(encode_frame),
    CODEC_PIXFMTS_ARRAY(pix_fmt),
};
#endif
#if CONFIG_AVRP_ENCODER
const FFCodec ff_avrp_encoder = {
    .p.name         = "avrp",
    CODEC_LONG_NAME("Avid 1:1 10-bit RGB Packer"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_AVRP,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .init           = encode_init,
    FF_CODEC_ENCODE_CB(encode_frame),
    CODEC_PIXFMTS_ARRAY(pix_fmt),
};
#endif
