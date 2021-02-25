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
    avctx->pix_fmt = AV_PIX_FMT_GBRP10;
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
    uint8_t *g_line, *b_line, *r_line;
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
    g_line = pic->data[0];
    b_line = pic->data[1];
    r_line = pic->data[2];

    for (h = 0; h < avctx->height; h++) {
        uint16_t *dstg = (uint16_t *)g_line;
        uint16_t *dstb = (uint16_t *)b_line;
        uint16_t *dstr = (uint16_t *)r_line;
        for (w = 0; w < avctx->width; w++) {
            uint32_t pixel;
            uint16_t r, g, b;
            if (avctx->codec_id == AV_CODEC_ID_AVRP || r10 || le) {
                pixel = av_le2ne32(*src++);
            } else {
                pixel = av_be2ne32(*src++);
            }
            if (avctx->codec_id == AV_CODEC_ID_R210) {
                b =  pixel & 0x3ff;
                g = (pixel >> 10) & 0x3ff;
                r = (pixel >> 20) & 0x3ff;
            } else if (r10) {
                r =  pixel & 0x3ff;
                g = (pixel >> 10) & 0x3ff;
                b = (pixel >> 20) & 0x3ff;
            } else {
                b = (pixel >>  2) & 0x3ff;
                g = (pixel >> 12) & 0x3ff;
                r = (pixel >> 22) & 0x3ff;
            }
            *dstr++ = r;
            *dstg++ = g;
            *dstb++ = b;
        }
        src += aligned_width - avctx->width;
        g_line += pic->linesize[0];
        b_line += pic->linesize[1];
        r_line += pic->linesize[2];
    }

    *got_frame      = 1;

    return avpkt->size;
}

#if CONFIG_R210_DECODER
const AVCodec ff_r210_decoder = {
    .name           = "r210",
    .long_name      = NULL_IF_CONFIG_SMALL("Uncompressed RGB 10-bit"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_R210,
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
#endif
#if CONFIG_R10K_DECODER
const AVCodec ff_r10k_decoder = {
    .name           = "r10k",
    .long_name      = NULL_IF_CONFIG_SMALL("AJA Kona 10-bit RGB Codec"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_R10K,
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
#endif
#if CONFIG_AVRP_DECODER
const AVCodec ff_avrp_decoder = {
    .name           = "avrp",
    .long_name      = NULL_IF_CONFIG_SMALL("Avid 1:1 10-bit RGB Packer"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_AVRP,
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
#endif
