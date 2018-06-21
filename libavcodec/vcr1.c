/*
 * ATI VCR1 codec
 * Copyright (c) 2003 Michael Niedermayer
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
 * ATI VCR1 codec
 */

#include "avcodec.h"
#include "internal.h"
#include "libavutil/avassert.h"
#include "libavutil/internal.h"

typedef struct VCR1Context {
    int delta[16];
    int offset[4];
} VCR1Context;

static av_cold int vcr1_decode_init(AVCodecContext *avctx)
{
    avctx->pix_fmt = AV_PIX_FMT_YUV410P;

    if (avctx->width % 8 || avctx->height%4) {
        avpriv_request_sample(avctx, "odd dimensions (%d x %d) support", avctx->width, avctx->height);
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int vcr1_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_frame, AVPacket *avpkt)
{
    VCR1Context *const a      = avctx->priv_data;
    AVFrame *const p          = data;
    const uint8_t *bytestream = avpkt->data;
    const uint8_t *bytestream_end = bytestream + avpkt->size;
    int i, x, y, ret;

    if(avpkt->size < 32 + avctx->height + avctx->width*avctx->height*5/8){
        av_log(avctx, AV_LOG_ERROR, "Insufficient input data. %d < %d\n", avpkt->size ,  32 + avctx->height + avctx->width*avctx->height*5/8);
        return AVERROR(EINVAL);
    }

    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;
    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;

    for (i = 0; i < 16; i++) {
        a->delta[i] = *bytestream++;
        bytestream++;
    }

    for (y = 0; y < avctx->height; y++) {
        int offset;
        uint8_t *luma = &p->data[0][y * p->linesize[0]];

        if ((y & 3) == 0) {
            uint8_t *cb = &p->data[1][(y >> 2) * p->linesize[1]];
            uint8_t *cr = &p->data[2][(y >> 2) * p->linesize[2]];

            av_assert0 (bytestream_end - bytestream >= 4 + avctx->width);

            for (i = 0; i < 4; i++)
                a->offset[i] = *bytestream++;

            offset = a->offset[0] - a->delta[bytestream[2] & 0xF];
            for (x = 0; x < avctx->width; x += 4) {
                luma[0]     = offset += a->delta[bytestream[2] & 0xF];
                luma[1]     = offset += a->delta[bytestream[2] >>  4];
                luma[2]     = offset += a->delta[bytestream[0] & 0xF];
                luma[3]     = offset += a->delta[bytestream[0] >>  4];
                luma       += 4;

                *cb++       = bytestream[3];
                *cr++       = bytestream[1];

                bytestream += 4;
            }
        } else {
            av_assert0 (bytestream_end - bytestream >= avctx->width / 2);

            offset = a->offset[y & 3] - a->delta[bytestream[2] & 0xF];

            for (x = 0; x < avctx->width; x += 8) {
                luma[0]     = offset += a->delta[bytestream[2] & 0xF];
                luma[1]     = offset += a->delta[bytestream[2] >>  4];
                luma[2]     = offset += a->delta[bytestream[3] & 0xF];
                luma[3]     = offset += a->delta[bytestream[3] >>  4];
                luma[4]     = offset += a->delta[bytestream[0] & 0xF];
                luma[5]     = offset += a->delta[bytestream[0] >>  4];
                luma[6]     = offset += a->delta[bytestream[1] & 0xF];
                luma[7]     = offset += a->delta[bytestream[1] >>  4];
                luma       += 8;
                bytestream += 4;
            }
        }
    }

    *got_frame = 1;

    return bytestream - avpkt->data;
}

AVCodec ff_vcr1_decoder = {
    .name           = "vcr1",
    .long_name      = NULL_IF_CONFIG_SMALL("ATI VCR1"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VCR1,
    .priv_data_size = sizeof(VCR1Context),
    .init           = vcr1_decode_init,
    .decode         = vcr1_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};
