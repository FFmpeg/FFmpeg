/*
 * Dxtory decoder
 *
 * Copyright (c) 2011 Konstantin Shishkov
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "avcodec.h"
#include "libavutil/intreadwrite.h"

static av_cold int decode_init(AVCodecContext *avctx)
{
    avctx->pix_fmt     = PIX_FMT_YUV420P;
    avctx->coded_frame = avcodec_alloc_frame();
    if (!avctx->coded_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                        AVPacket *avpkt)
{
    int h, w;
    AVFrame *pic = avctx->coded_frame;
    const uint8_t *src = avpkt->data;
    uint8_t *Y1, *Y2, *U, *V;
    int ret;

    if (pic->data[0])
        avctx->release_buffer(avctx, pic);

    if (avpkt->size < avctx->width * avctx->height * 3 / 2 + 16) {
        av_log(avctx, AV_LOG_ERROR, "packet too small\n");
        return AVERROR_INVALIDDATA;
    }

    pic->reference = 0;
    if ((ret = avctx->get_buffer(avctx, pic)) < 0)
        return ret;

    pic->pict_type = AV_PICTURE_TYPE_I;
    pic->key_frame = 1;

    if (AV_RL32(src) != 0x01000002) {
        av_log_ask_for_sample(avctx, "Unknown frame header %X\n", AV_RL32(src));
        return AVERROR_PATCHWELCOME;
    }
    src += 16;

    Y1 = pic->data[0];
    Y2 = pic->data[0] + pic->linesize[0];
    U  = pic->data[1];
    V  = pic->data[2];
    for (h = 0; h < avctx->height; h += 2) {
        for (w = 0; w < avctx->width; w += 2) {
            AV_WN16A(Y1 + w, AV_RN16A(src));
            AV_WN16A(Y2 + w, AV_RN16A(src + 2));
            U[w >> 1] = src[4] + 0x80;
            V[w >> 1] = src[5] + 0x80;
            src += 6;
        }
        Y1 += pic->linesize[0] << 1;
        Y2 += pic->linesize[0] << 1;
        U  += pic->linesize[1];
        V  += pic->linesize[2];
    }

    *data_size      = sizeof(AVFrame);
    *(AVFrame*)data = *pic;

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

AVCodec ff_dxtory_decoder = {
    .name           = "dxtory",
    .long_name      = NULL_IF_CONFIG_SMALL("Dxtory"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_DXTORY,
    .init           = decode_init,
    .close          = decode_close,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
