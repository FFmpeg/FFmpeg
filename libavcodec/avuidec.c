/*
 * AVID Meridien decoder
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
#include "decode.h"
#include "libavutil/intreadwrite.h"

static av_cold int avui_decode_init(AVCodecContext *avctx)
{
    avctx->pix_fmt = AV_PIX_FMT_YUVA422P;
    return 0;
}

static int avui_decode_frame(AVCodecContext *avctx, AVFrame *pic,
                             int *got_frame, AVPacket *avpkt)
{
    int ret;
    const uint8_t *src = avpkt->data, *extradata = avctx->extradata;
    const uint8_t *srca;
    uint8_t *y, *u, *v, *a;
    int transparent, interlaced = 1, skip, opaque_length, i, j, k;
    uint32_t extradata_size = avctx->extradata_size;

    while (extradata_size >= 24) {
        uint32_t atom_size = AV_RB32(extradata);
        if (!memcmp(&extradata[4], "APRGAPRG0001", 12)) {
            interlaced = extradata[19] != 1;
            break;
        }
        if (atom_size && atom_size <= extradata_size) {
            extradata      += atom_size;
            extradata_size -= atom_size;
        } else {
            break;
        }
    }
    if (avctx->height == 486) {
        skip = 10;
    } else {
        skip = 16;
    }
    opaque_length = 2 * avctx->width * (avctx->height + skip) + 4 * interlaced;
    if (avpkt->size < opaque_length) {
        av_log(avctx, AV_LOG_ERROR, "Insufficient input data.\n");
        return AVERROR(EINVAL);
    }
    transparent = avctx->bits_per_coded_sample == 32 &&
                  avpkt->size >= opaque_length * 2 + 4;
    srca = src + opaque_length + 5;

    if ((ret = ff_get_buffer(avctx, pic, 0)) < 0)
        return ret;

    pic->flags |= AV_FRAME_FLAG_KEY;
    pic->pict_type = AV_PICTURE_TYPE_I;

    if (!interlaced) {
        src  += avctx->width * skip;
        srca += avctx->width * skip;
    }

    for (i = 0; i < interlaced + 1; i++) {
        src  += avctx->width * skip;
        srca += avctx->width * skip;
        if (interlaced && avctx->height == 486) {
            y = pic->data[0] + (1 - i) * pic->linesize[0];
            u = pic->data[1] + (1 - i) * pic->linesize[1];
            v = pic->data[2] + (1 - i) * pic->linesize[2];
            a = pic->data[3] + (1 - i) * pic->linesize[3];
        } else {
            y = pic->data[0] + i * pic->linesize[0];
            u = pic->data[1] + i * pic->linesize[1];
            v = pic->data[2] + i * pic->linesize[2];
            a = pic->data[3] + i * pic->linesize[3];
        }

        for (j = 0; j < avctx->height >> interlaced; j++) {
            for (k = 0; k < avctx->width >> 1; k++) {
                u[    k    ] = *src++;
                y[2 * k    ] = *src++;
                a[2 * k    ] = 0xFF - (transparent ? *srca++ : 0);
                srca++;
                v[    k    ] = *src++;
                y[2 * k + 1] = *src++;
                a[2 * k + 1] = 0xFF - (transparent ? *srca++ : 0);
                srca++;
            }

            y += (interlaced + 1) * pic->linesize[0];
            u += (interlaced + 1) * pic->linesize[1];
            v += (interlaced + 1) * pic->linesize[2];
            a += (interlaced + 1) * pic->linesize[3];
        }
        src  += 4;
        srca += 4;
    }
    *got_frame       = 1;

    return avpkt->size;
}

const FFCodec ff_avui_decoder = {
    .p.name         = "avui",
    CODEC_LONG_NAME("Avid Meridien Uncompressed"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_AVUI,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .init         = avui_decode_init,
    FF_CODEC_DECODE_CB(avui_decode_frame),
};
