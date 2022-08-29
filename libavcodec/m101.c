/*
 * Copyright (c) 2016 Michael Niedermayer
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

#include "libavutil/intreadwrite.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"


static av_cold int m101_decode_init(AVCodecContext *avctx)
{
    if (avctx->extradata_size < 6*4) {
        avpriv_request_sample(avctx, "Missing or too small extradata (size %d)", avctx->extradata_size);
        return AVERROR_INVALIDDATA;
    }

    if (avctx->extradata[2*4] == 10)
        avctx->pix_fmt = AV_PIX_FMT_YUV422P10;
    else if (avctx->extradata[2*4] == 8) {
        avctx->pix_fmt = AV_PIX_FMT_YUYV422;
    } else {
        avpriv_request_sample(avctx, "BPS %d", avctx->extradata[2*4]);
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int m101_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                             int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int stride, ret;
    int x, y;
    int min_stride = 2 * avctx->width;
    int bits = avctx->extradata[2*4];

    stride = AV_RL32(avctx->extradata + 5*4);

    if (avctx->pix_fmt == AV_PIX_FMT_YUV422P10)
        min_stride = (avctx->width + 15) / 16 * 40;

    if (stride < min_stride || avpkt->size < stride * (uint64_t)avctx->height) {
        av_log(avctx, AV_LOG_ERROR, "stride (%d) is invalid for packet sized %d\n",
               stride, avpkt->size);
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    frame->pict_type = AV_PICTURE_TYPE_I;
    frame->key_frame = 1;
    frame->interlaced_frame = ((avctx->extradata[3*4] & 3) != 3);
    if (frame->interlaced_frame)
        frame->top_field_first = avctx->extradata[3*4] & 1;

    for (y = 0; y < avctx->height; y++) {
        int src_y = y;
        if (frame->interlaced_frame)
            src_y = ((y&1)^frame->top_field_first) ? y/2 : (y/2 + avctx->height/2);
        if (bits == 8) {
            uint8_t *line = frame->data[0] + y*frame->linesize[0];
            memcpy(line, buf + src_y*stride, 2*avctx->width);
        } else {
            int block;
            uint16_t *luma = (uint16_t*)&frame->data[0][y*frame->linesize[0]];
            uint16_t *cb   = (uint16_t*)&frame->data[1][y*frame->linesize[1]];
            uint16_t *cr   = (uint16_t*)&frame->data[2][y*frame->linesize[2]];
            for (block = 0; 16*block < avctx->width; block ++) {
                const uint8_t *buf_src = buf + src_y*stride + 40*block;
                for (x = 0; x < 16 && x + 16*block < avctx->width; x++) {
                    int xd = x + 16*block;
                    if (x&1) {
                        luma [xd] = (4*buf_src[2*x + 0]) + ((buf_src[32 + (x>>1)]>>4)&3);
                    } else {
                        luma [xd] = (4*buf_src[2*x + 0]) +  (buf_src[32 + (x>>1)]    &3);
                        cb[xd>>1] = (4*buf_src[2*x + 1]) + ((buf_src[32 + (x>>1)]>>2)&3);
                        cr[xd>>1] = (4*buf_src[2*x + 3]) +  (buf_src[32 + (x>>1)]>>6);
                    }
                }
            }
        }
    }

    *got_frame = 1;
    return avpkt->size;
}

const FFCodec ff_m101_decoder = {
    .p.name         = "m101",
    CODEC_LONG_NAME("Matrox Uncompressed SD"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_M101,
    .init           = m101_decode_init,
    FF_CODEC_DECODE_CB(m101_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
};
