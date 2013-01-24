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
#include "dsputil.h"
#include "internal.h"
#include "libavutil/internal.h"

typedef struct VCR1Context {
    AVFrame picture;
    int delta[16];
    int offset[4];
} VCR1Context;

static av_cold int vcr1_common_init(AVCodecContext *avctx)
{
    VCR1Context *const a = avctx->priv_data;

    avctx->coded_frame = &a->picture;
    avcodec_get_frame_defaults(&a->picture);

    return 0;
}

static av_cold int vcr1_decode_init(AVCodecContext *avctx)
{
    vcr1_common_init(avctx);

    avctx->pix_fmt = AV_PIX_FMT_YUV410P;

    if (avctx->width % 8 || avctx->height%4) {
        av_log_ask_for_sample(avctx, "odd dimensions are not supported\n");
        return AVERROR_PATCHWELCOME;
    }
    return 0;
}

static av_cold int vcr1_decode_end(AVCodecContext *avctx)
{
    VCR1Context *s = avctx->priv_data;

    if (s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);

    return 0;
}

static int vcr1_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf        = avpkt->data;
    int buf_size              = avpkt->size;
    VCR1Context *const a      = avctx->priv_data;
    AVFrame *picture          = data;
    AVFrame *const p          = &a->picture;
    const uint8_t *bytestream = buf;
    int i, x, y, ret;

    if (p->data[0])
        avctx->release_buffer(avctx, p);

    if(buf_size < 16 + avctx->height + avctx->width*avctx->height*5/8){
        av_log(avctx, AV_LOG_ERROR, "Insufficient input data.\n");
        return AVERROR(EINVAL);
    }

    p->reference = 0;
    if ((ret = ff_get_buffer(avctx, p)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }
    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;

    for (i = 0; i < 16; i++) {
        a->delta[i] = *bytestream++;
        bytestream++;
    }

    for (y = 0; y < avctx->height; y++) {
        int offset;
        uint8_t *luma = &a->picture.data[0][y * a->picture.linesize[0]];

        if ((y & 3) == 0) {
            uint8_t *cb = &a->picture.data[1][(y >> 2) * a->picture.linesize[1]];
            uint8_t *cr = &a->picture.data[2][(y >> 2) * a->picture.linesize[2]];

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

    *picture   = a->picture;
    *got_frame = 1;

    return buf_size;
}

AVCodec ff_vcr1_decoder = {
    .name           = "vcr1",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VCR1,
    .priv_data_size = sizeof(VCR1Context),
    .init           = vcr1_decode_init,
    .close          = vcr1_decode_end,
    .decode         = vcr1_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("ATI VCR1"),
};
