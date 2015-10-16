/*
 * AVFrame wrapper
 * Copyright (c) 2015 Luca Barbato
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
 * Simple wrapper to store an AVFrame and forward it as AVPacket.
 */

#include "avcodec.h"
#include "internal.h"

#include "libavutil/internal.h"
#include "libavutil/frame.h"
#include "libavutil/buffer.h"
#include "libavutil/pixdesc.h"

static void wrapped_avframe_release_buffer(void *unused, uint8_t *data)
{
    AVFrame *frame = (AVFrame *)data;

    av_frame_free(&frame);
}

static int wrapped_avframe_encode(AVCodecContext *avctx, AVPacket *pkt,
                     const AVFrame *frame, int *got_packet)
{
    AVFrame *wrapped = av_frame_clone(frame);

    if (!wrapped)
        return AVERROR(ENOMEM);

    pkt->buf = av_buffer_create((uint8_t *)wrapped, sizeof(*wrapped),
                                wrapped_avframe_release_buffer, NULL,
                                AV_BUFFER_FLAG_READONLY);
    if (!pkt->buf) {
        av_frame_free(&wrapped);
        return AVERROR(ENOMEM);
    }

    pkt->data = (uint8_t *)wrapped;
    pkt->size = sizeof(*wrapped);

    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;
    return 0;
}

AVCodec ff_wrapped_avframe_encoder = {
    .name           = "wrapped_avframe",
    .long_name      = NULL_IF_CONFIG_SMALL("AVFrame to AVPacket passthrough"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_WRAPPED_AVFRAME,
    .encode2        = wrapped_avframe_encode,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
