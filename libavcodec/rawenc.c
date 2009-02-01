/*
 * Raw Video Encoder
 * Copyright (c) 2001 Fabrice Bellard
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
 * @file libavcodec/rawenc.c
 * Raw Video Encoder
 */

#include "avcodec.h"
#include "raw.h"

static av_cold int raw_init_encoder(AVCodecContext *avctx)
{
    avctx->coded_frame = (AVFrame *)avctx->priv_data;
    avctx->coded_frame->pict_type = FF_I_TYPE;
    avctx->coded_frame->key_frame = 1;
    if(!avctx->codec_tag)
        avctx->codec_tag = avcodec_pix_fmt_to_codec_tag(avctx->pix_fmt);
    return 0;
}

static int raw_encode(AVCodecContext *avctx,
                            unsigned char *frame, int buf_size, void *data)
{
    return avpicture_layout((AVPicture *)data, avctx->pix_fmt, avctx->width,
                                               avctx->height, frame, buf_size);
}

AVCodec rawvideo_encoder = {
    "rawvideo",
    CODEC_TYPE_VIDEO,
    CODEC_ID_RAWVIDEO,
    sizeof(AVFrame),
    raw_init_encoder,
    raw_encode,
    .long_name = NULL_IF_CONFIG_SMALL("raw video"),
};
