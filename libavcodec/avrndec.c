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
#include "internal.h"
#include "mjpeg.h"
#include "mjpegdec.h"
#include "libavutil/imgutils.h"

typedef struct {
    MJpegDecodeContext mjpeg_ctx;
    AVFrame frame;
    int is_mjpeg;
    int interlace; //FIXME use frame.interlaced_frame
    int tff;
} AVRnContext;

static av_cold int init(AVCodecContext *avctx)
{
    AVRnContext *a = avctx->priv_data;
    int ret;

    // Support "Resolution 1:1" for Avid AVI Codec
    a->is_mjpeg = avctx->extradata_size < 31 || memcmp(&avctx->extradata[28], "1:1", 3);

    if(!a->is_mjpeg && avctx->lowres) {
        av_log(avctx, AV_LOG_ERROR, "lowres is not possible with rawvideo\n");
        return AVERROR(EINVAL);
    }

    if(a->is_mjpeg)
        return ff_mjpeg_decode_init(avctx);

    if ((ret = av_image_check_size(avctx->width, avctx->height, 0, avctx)) < 0)
        return ret;

    avcodec_get_frame_defaults(&a->frame);
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

static av_cold int end(AVCodecContext *avctx)
{
    AVRnContext *a = avctx->priv_data;
    AVFrame *p = &a->frame;

    if(p->data[0])
        avctx->release_buffer(avctx, p);

    if(a->is_mjpeg)
        ff_mjpeg_decode_end(avctx);

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data,
                        int *got_frame, AVPacket *avpkt)
{
    AVRnContext *a = avctx->priv_data;
    AVFrame *p = &a->frame;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    int y, ret, true_height;

    if(a->is_mjpeg)
        return ff_mjpeg_decode_frame(avctx, data, got_frame, avpkt);

    true_height    = buf_size / (2*avctx->width);
    if(p->data[0])
        avctx->release_buffer(avctx, p);

    if(buf_size < 2*avctx->width * avctx->height) {
        av_log(avctx, AV_LOG_ERROR, "packet too small\n");
        return AVERROR_INVALIDDATA;
    }

    if((ret = ff_get_buffer(avctx, p)) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }
    p->pict_type= AV_PICTURE_TYPE_I;
    p->key_frame= 1;

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

    *(AVFrame*)data = a->frame;
    *got_frame      = 1;
    return buf_size;
}

AVCodec ff_avrn_decoder = {
    .name           = "avrn",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_AVRN,
    .priv_data_size = sizeof(AVRnContext),
    .init           = init,
    .close          = end,
    .decode         = decode_frame,
    .long_name      = NULL_IF_CONFIG_SMALL("Avid AVI Codec"),
    .capabilities   = CODEC_CAP_DR1,
    .max_lowres     = 3,
};

