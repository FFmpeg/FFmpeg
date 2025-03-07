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
#include "codec_internal.h"
#include "encode.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"

static av_cold int avui_encode_init(AVCodecContext *avctx)
{
    if (avctx->width != 720 || avctx->height != 486 && avctx->height != 576) {
        av_log(avctx, AV_LOG_ERROR, "Only 720x486 and 720x576 are supported.\n");
        return AVERROR(EINVAL);
    }
    if (!(avctx->extradata = av_mallocz(144 + AV_INPUT_BUFFER_PADDING_SIZE)))
        return AVERROR(ENOMEM);
    avctx->extradata_size = 144;
    memcpy(avctx->extradata, "\0\0\0\x18""APRGAPRG0001", 16);
    if (avctx->field_order > AV_FIELD_PROGRESSIVE) {
        avctx->extradata[19] = 2;
    } else {
        avctx->extradata[19] = 1;
    }
    memcpy(avctx->extradata + 24, "\0\0\0\x78""ARESARES0001""\0\0\0\x98", 20);
    AV_WB32(avctx->extradata + 44, avctx->width);
    AV_WB32(avctx->extradata + 48, avctx->height);
    memcpy(avctx->extradata + 52, "\0\0\0\x1\0\0\0\x20\0\0\0\x2", 12);


    return 0;
}

static int avui_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *pic, int *got_packet)
{
    uint8_t *dst;
    int i, j, skip, ret, size, interlaced;

    interlaced = avctx->field_order > AV_FIELD_PROGRESSIVE;

    if (avctx->height == 486) {
        skip = 10;
    } else {
        skip = 16;
    }
    size = 2 * avctx->width * (avctx->height + skip) + 8 * interlaced;
    if ((ret = ff_get_encode_buffer(avctx, pkt, size, 0)) < 0)
        return ret;
    dst = pkt->data;
    if (!interlaced) {
        memset(dst, 0, avctx->width * skip);
        dst += avctx->width * skip;
    }

    for (i = 0; i <= interlaced; i++) {
        const uint8_t *src;
        if (interlaced && avctx->height == 486) {
            src = pic->data[0] + (1 - i) * pic->linesize[0];
        } else {
            src = pic->data[0] + i * pic->linesize[0];
        }
        memset(dst, 0, avctx->width * skip + 4 * i);
        dst += avctx->width * skip + 4 * i;
        for (j = 0; j < avctx->height; j += interlaced + 1) {
            memcpy(dst, src, avctx->width * 2);
            src += (interlaced + 1) * pic->linesize[0];
            dst += avctx->width * 2;
        }
    }

    *got_packet = 1;
    return 0;
}

const FFCodec ff_avui_encoder = {
    .p.name         = "avui",
    CODEC_LONG_NAME("Avid Meridien Uncompressed"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_AVUI,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_EXPERIMENTAL |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    CODEC_PIXFMTS(AV_PIX_FMT_UYVY422),
    .color_ranges   = AVCOL_RANGE_MPEG,
    .init         = avui_encode_init,
    FF_CODEC_ENCODE_CB(avui_encode_frame),
};
