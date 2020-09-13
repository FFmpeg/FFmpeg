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
    AVCodecContext *mjpeg_avctx;
    int is_mjpeg;
    int interlace;
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

    if(a->is_mjpeg) {
        const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
        AVDictionary *thread_opt = NULL;
        if (!codec) {
            av_log(avctx, AV_LOG_ERROR, "MJPEG codec not found\n");
            return AVERROR_DECODER_NOT_FOUND;
        }

        a->mjpeg_avctx = avcodec_alloc_context3(codec);
        if (!a->mjpeg_avctx)
            return AVERROR(ENOMEM);

        av_dict_set(&thread_opt, "threads", "1", 0); // Is this needed ?
        a->mjpeg_avctx->refcounted_frames = 1;
        a->mjpeg_avctx->flags = avctx->flags;
        a->mjpeg_avctx->idct_algo = avctx->idct_algo;
        a->mjpeg_avctx->lowres = avctx->lowres;
        a->mjpeg_avctx->width = avctx->width;
        a->mjpeg_avctx->height = avctx->height;

        if ((ret = ff_codec_open2_recursive(a->mjpeg_avctx, codec, &thread_opt)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "MJPEG codec failed to open\n");
        }
        av_dict_free(&thread_opt);

        return ret;
    }

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

static av_cold int end(AVCodecContext *avctx)
{
    AVRnContext *a = avctx->priv_data;

    avcodec_free_context(&a->mjpeg_avctx);

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data,
                        int *got_frame, AVPacket *avpkt)
{
    AVRnContext *a = avctx->priv_data;
    AVFrame *p = data;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    int y, ret, true_height;

    if(a->is_mjpeg) {
        ret = avcodec_decode_video2(a->mjpeg_avctx, data, got_frame, avpkt);

        if (ret >= 0 && *got_frame && avctx->width <= p->width && avctx->height <= p->height) {
            int shift = p->height - avctx->height;
            int subsample_h, subsample_v;

            av_pix_fmt_get_chroma_sub_sample(p->format, &subsample_h, &subsample_v);

            p->data[0] += p->linesize[0] * shift;
            if (p->data[2]) {
                p->data[1] += p->linesize[1] * (shift>>subsample_v);
                p->data[2] += p->linesize[2] * (shift>>subsample_v);
            }

            p->width  = avctx->width;
            p->height = avctx->height;
        }
        avctx->pix_fmt = a->mjpeg_avctx->pix_fmt;
        return ret;
    }

    true_height    = buf_size / (2*avctx->width);

    if(buf_size < 2*avctx->width * avctx->height) {
        av_log(avctx, AV_LOG_ERROR, "packet too small\n");
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;
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

    *got_frame      = 1;
    return buf_size;
}

AVCodec ff_avrn_decoder = {
    .name           = "avrn",
    .long_name      = NULL_IF_CONFIG_SMALL("Avid AVI Codec"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_AVRN,
    .priv_data_size = sizeof(AVRnContext),
    .init           = init,
    .close          = end,
    .decode         = decode_frame,
    .max_lowres     = 3,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};
