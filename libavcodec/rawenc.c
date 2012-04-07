/*
 * Raw Video Encoder
 * Copyright (c) 2001 Fabrice Bellard
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
 * Raw Video Encoder
 */

#include "avcodec.h"
#include "raw.h"
#include "internal.h"
#include "libavutil/pixdesc.h"
#include "libavutil/intreadwrite.h"

static av_cold int raw_init_encoder(AVCodecContext *avctx)
{
    avctx->coded_frame            = avctx->priv_data;
    avcodec_get_frame_defaults(avctx->coded_frame);
    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;
    avctx->bits_per_coded_sample = av_get_bits_per_pixel(&av_pix_fmt_descriptors[avctx->pix_fmt]);
    if(!avctx->codec_tag)
        avctx->codec_tag = avcodec_pix_fmt_to_codec_tag(avctx->pix_fmt);
    return 0;
}

static int raw_encode(AVCodecContext *avctx, AVPacket *pkt,
                      const AVFrame *frame, int *got_packet)
{
    int ret = avpicture_get_size(avctx->pix_fmt, avctx->width, avctx->height);

    if (ret < 0)
        return ret;

    if ((ret = ff_alloc_packet2(avctx, pkt, ret)) < 0)
        return ret;
    if ((ret = avpicture_layout((const AVPicture *)frame, avctx->pix_fmt, avctx->width,
                                avctx->height, pkt->data, pkt->size)) < 0)
        return ret;

    if(avctx->codec_tag == AV_RL32("yuv2") && ret > 0 &&
       avctx->pix_fmt   == PIX_FMT_YUYV422) {
        int x;
        for(x = 1; x < avctx->height*avctx->width*2; x += 2)
            pkt->data[x] ^= 0x80;
    }
    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;
    return 0;
}

AVCodec ff_rawvideo_encoder = {
    .name           = "rawvideo",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_RAWVIDEO,
    .priv_data_size = sizeof(AVFrame),
    .init           = raw_init_encoder,
    .encode2        = raw_encode,
    .long_name      = NULL_IF_CONFIG_SMALL("raw video"),
};
