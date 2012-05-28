/*
 * AVID Meridien encoder
 *
 * Copyright (c) 2012 Carl Eugen Hoyos
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

static av_cold int avui_encode_init(AVCodecContext *avctx)
{
    avctx->coded_frame = avcodec_alloc_frame();

    if (avctx->width != 720 || avctx->height != 486 && avctx->height != 576) {
        av_log(avctx, AV_LOG_ERROR, "Only 720x486 and 720x576 are supported.\n");
        return AVERROR(EINVAL);
    }
    if (!avctx->coded_frame) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate frame.\n");
        return AVERROR(ENOMEM);
    }
    if (!(avctx->extradata = av_mallocz(24 + FF_INPUT_BUFFER_PADDING_SIZE)))
        return AVERROR(ENOMEM);
    avctx->extradata_size = 24;
    memcpy(avctx->extradata, "\0\0\0\x18""APRGAPRG0001", 16);
    if (avctx->field_order > AV_FIELD_PROGRESSIVE) {
        avctx->extradata[19] = 2;
    } else {
        avctx->extradata[19] = 1;
    }


    return 0;
}

static int avui_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *pic, int *got_packet)
{
    uint8_t *dst, *src = pic->data[0];
    int i, j, skip, ret, size, interlaced;

    interlaced = avctx->field_order > AV_FIELD_PROGRESSIVE;

    if (avctx->height == 486) {
        skip = 10;
    } else {
        skip = 16;
    }
    size = 2 * avctx->width * (avctx->height + skip) + 8 * interlaced;
    if ((ret = ff_alloc_packet2(avctx, pkt, size)) < 0)
        return ret;
    dst = pkt->data;
    if (!interlaced) {
        dst += avctx->width * skip;
    }

    avctx->coded_frame->reference = 0;
    avctx->coded_frame->key_frame = 1;
    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;

    for (i = 0; i <= interlaced; i++) {
        if (interlaced && avctx->height == 486) {
            src = pic->data[0] + (1 - i) * pic->linesize[0];
        } else {
            src = pic->data[0] + i * pic->linesize[0];
        }
        dst += avctx->width * skip + 4 * i;
        for (j = 0; j < avctx->height; j += interlaced + 1) {
            memcpy(dst, src, avctx->width * 2);
            src += (interlaced + 1) * pic->linesize[0];
            dst += avctx->width * 2;
        }
    }

    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;
    return 0;
}

static av_cold int avui_encode_close(AVCodecContext *avctx)
{
    av_freep(&avctx->coded_frame);

    return 0;
}

AVCodec ff_avui_encoder = {
    .name         = "avui",
    .type         = AVMEDIA_TYPE_VIDEO,
    .id           = CODEC_ID_AVUI,
    .init         = avui_encode_init,
    .encode2      = avui_encode_frame,
    .close        = avui_encode_close,
    .capabilities = CODEC_CAP_EXPERIMENTAL,
    .pix_fmts     = (const enum PixelFormat[]){ PIX_FMT_UYVY422, PIX_FMT_NONE },
    .long_name    = NULL_IF_CONFIG_SMALL("Avid Meridien Uncompressed"),
};
