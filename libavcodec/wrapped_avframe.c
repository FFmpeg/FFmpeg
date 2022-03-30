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
#include "codec_internal.h"
#include "decode.h"

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
    uint8_t *data;
    int size = sizeof(*wrapped) + AV_INPUT_BUFFER_PADDING_SIZE;

    if (!wrapped)
        return AVERROR(ENOMEM);

    data = av_mallocz(size);
    if (!data) {
        av_frame_free(&wrapped);
        return AVERROR(ENOMEM);
    }

    pkt->buf = av_buffer_create(data, size,
                                wrapped_avframe_release_buffer, NULL,
                                AV_BUFFER_FLAG_READONLY);
    if (!pkt->buf) {
        av_frame_free(&wrapped);
        av_freep(&data);
        return AVERROR(ENOMEM);
    }

    av_frame_move_ref((AVFrame*)data, wrapped);
    av_frame_free(&wrapped);

    pkt->data = data;
    pkt->size = sizeof(*wrapped);

    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;
    return 0;
}

static int wrapped_avframe_decode(AVCodecContext *avctx, AVFrame *out,
                                  int *got_frame, AVPacket *pkt)
{
    AVFrame *in;
    int err;

    if (!(pkt->flags & AV_PKT_FLAG_TRUSTED)) {
        // This decoder is not usable with untrusted input.
        return AVERROR(EPERM);
    }

    if (pkt->size < sizeof(AVFrame))
        return AVERROR(EINVAL);

    in  = (AVFrame*)pkt->data;

    err = ff_decode_frame_props(avctx, out);
    if (err < 0)
        return err;

    av_frame_move_ref(out, in);

    err = ff_attach_decode_data(out);
    if (err < 0) {
        av_frame_unref(out);
        return err;
    }

    *got_frame = 1;
    return 0;
}

const FFCodec ff_wrapped_avframe_encoder = {
    .p.name         = "wrapped_avframe",
    .p.long_name    = NULL_IF_CONFIG_SMALL("AVFrame to AVPacket passthrough"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_WRAPPED_AVFRAME,
    FF_CODEC_ENCODE_CB(wrapped_avframe_encode),
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};

const FFCodec ff_wrapped_avframe_decoder = {
    .p.name         = "wrapped_avframe",
    .p.long_name    = NULL_IF_CONFIG_SMALL("AVPacket to AVFrame passthrough"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_WRAPPED_AVFRAME,
    FF_CODEC_DECODE_CB(wrapped_avframe_decode),
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
