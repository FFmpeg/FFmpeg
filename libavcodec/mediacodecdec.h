/*
 * Android MediaCodec decoder
 *
 * Copyright (c) 2015-2016 Matthieu Bouron <matthieu.bouron stupeflix.com>
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

#ifndef AVCODEC_MEDIACODECDEC_H
#define AVCODEC_MEDIACODECDEC_H

#include <stdint.h>
#include <sys/types.h>

#include "libavutil/frame.h"
#include "libavutil/pixfmt.h"

#include "avcodec.h"
#include "mediacodec_wrapper.h"

typedef struct MediaCodecDecContext {

    char *codec_name;

    FFAMediaCodec *codec;
    FFAMediaFormat *format;

    int started;
    int flushing;
    int eos;

    int width;
    int height;
    int stride;
    int slice_height;
    int color_format;
    enum AVPixelFormat pix_fmt;
    int crop_top;
    int crop_bottom;
    int crop_left;
    int crop_right;

    uint64_t dequeued_buffer_nb;

    int first_buffer;
    double first_buffer_at;

} MediaCodecDecContext;

int ff_mediacodec_dec_init(AVCodecContext *avctx,
                           MediaCodecDecContext *s,
                           const char *mime,
                           FFAMediaFormat *format);

int ff_mediacodec_dec_decode(AVCodecContext *avctx,
                             MediaCodecDecContext *s,
                             AVFrame *frame,
                             int *got_frame,
                             AVPacket *pkt);

int ff_mediacodec_dec_flush(AVCodecContext *avctx,
                            MediaCodecDecContext *s);

int ff_mediacodec_dec_close(AVCodecContext *avctx,
                            MediaCodecDecContext *s);

#endif /* AVCODEC_MEDIACODECDEC_H */
