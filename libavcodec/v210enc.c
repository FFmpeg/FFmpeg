/*
 * V210 encoder
 *
 * Copyright (C) 2009 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2009 Baptiste Coudurier <baptiste dot coudurier at gmail dot com>
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
#include "bytestream.h"
#include "codec_internal.h"
#include "encode.h"
#include "internal.h"
#include "v210enc.h"
#include "v210enc_init.h"

#define TYPE uint8_t
#define DEPTH 8
#define BYTES_PER_PIXEL 1
#define RENAME(a) a ## _ ## 8
#include "v210_template.c"
#undef RENAME
#undef DEPTH
#undef BYTES_PER_PIXEL
#undef TYPE

#define TYPE uint16_t
#define DEPTH 10
#define BYTES_PER_PIXEL 2
#define RENAME(a) a ## _ ## 10
#include "v210_template.c"
#undef RENAME
#undef DEPTH
#undef BYTES_PER_PIXEL
#undef TYPE

static av_cold int encode_init(AVCodecContext *avctx)
{
    V210EncContext *s = avctx->priv_data;

    if (avctx->width & 1) {
        av_log(avctx, AV_LOG_ERROR, "v210 needs even width\n");
        return AVERROR(EINVAL);
    }

    ff_v210enc_init(s);

    avctx->bits_per_coded_sample = 20;
    avctx->bit_rate = ff_guess_coded_bitrate(avctx) * 16 / 15;

    return 0;
}

static int encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *pic, int *got_packet)
{
    int aligned_width = ((avctx->width + 47) / 48) * 48;
    int stride = aligned_width * 8 / 3;
    AVFrameSideData *side_data;
    int ret;
    uint8_t *dst;

    ret = ff_get_encode_buffer(avctx, pkt, avctx->height * stride, 0);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error getting output packet.\n");
        return ret;
    }
    dst = pkt->data;

    if (pic->format == AV_PIX_FMT_YUV422P10)
        v210_enc_10(avctx, dst, pic);
    else if(pic->format == AV_PIX_FMT_YUV422P)
        v210_enc_8(avctx, dst, pic);

    side_data = av_frame_get_side_data(pic, AV_FRAME_DATA_A53_CC);
    if (side_data && side_data->size) {
        uint8_t *buf = av_packet_new_side_data(pkt, AV_PKT_DATA_A53_CC, side_data->size);
        if (!buf)
            return AVERROR(ENOMEM);
        memcpy(buf, side_data->data, side_data->size);
    }

    side_data = av_frame_get_side_data(pic, AV_FRAME_DATA_AFD);
    if (side_data && side_data->size) {
        uint8_t *buf = av_packet_new_side_data(pkt, AV_PKT_DATA_AFD, side_data->size);
        if (!buf)
            return AVERROR(ENOMEM);
        memcpy(buf, side_data->data, side_data->size);
    }

    *got_packet = 1;
    return 0;
}

const FFCodec ff_v210_encoder = {
    .p.name         = "v210",
    CODEC_LONG_NAME("Uncompressed 4:2:2 10-bit"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_V210,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .priv_data_size = sizeof(V210EncContext),
    .init           = encode_init,
    FF_CODEC_ENCODE_CB(encode_frame),
    .p.pix_fmts     = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV422P, AV_PIX_FMT_NONE },
};
