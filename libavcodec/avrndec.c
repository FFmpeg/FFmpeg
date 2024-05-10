/*
 * AVRn decoder
 * Copyright (c) 2012 Michael Niedermayer
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
#include "libavutil/imgutils.h"

typedef struct {
    int interlace;
    int tff;
} AVRnContext;

static av_cold int init(AVCodecContext *avctx)
{
    AVRnContext *a = avctx->priv_data;
    int ret;

    if ((ret = av_image_check_size(avctx->width, avctx->height, 0, avctx)) < 0)
        return ret;

    avctx->pix_fmt = AV_PIX_FMT_UYVY422;

    if(avctx->extradata_size >= 9 && avctx->extradata[4]+28 < avctx->extradata_size) {
        int ndx = avctx->extradata[4] + 4;
        a->interlace = !memcmp(avctx->extradata + ndx, "1:1(", 4);
        if(a->interlace) {
            a->tff = avctx->extradata[ndx + 24] == 1;
        }
    }

    return 0;
}

static int decode_frame(AVCodecContext *avctx, AVFrame *p,
                        int *got_frame, AVPacket *avpkt)
{
    AVRnContext *a = avctx->priv_data;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    int y, ret, true_height;

    true_height    = buf_size / (2*avctx->width);

    if(buf_size < 2*avctx->width * avctx->height) {
        av_log(avctx, AV_LOG_ERROR, "packet too small\n");
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;

    if(a->interlace) {
        buf += (true_height - avctx->height)*avctx->width;
        for(y = 0; y < avctx->height-1; y+=2) {
            memcpy(p->data[0] + (y+ a->tff)*p->linesize[0], buf                             , 2*avctx->width);
            memcpy(p->data[0] + (y+!a->tff)*p->linesize[0], buf + avctx->width*true_height+4, 2*avctx->width);
            buf += 2*avctx->width;
        }
    } else {
        buf += (true_height - avctx->height)*avctx->width*2;
        for(y = 0; y < avctx->height; y++) {
            memcpy(p->data[0] + y*p->linesize[0], buf, 2*avctx->width);
            buf += 2*avctx->width;
        }
    }

    *got_frame      = 1;
    return buf_size;
}

const FFCodec ff_avrn_decoder = {
    .p.name         = "avrn",
    CODEC_LONG_NAME("Avid AVI Codec"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_AVRN,
    .priv_data_size = sizeof(AVRnContext),
    .init           = init,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
