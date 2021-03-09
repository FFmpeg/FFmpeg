/*
 * generic decoding-related code
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

#ifndef AVCODEC_DECODE_H
#define AVCODEC_DECODE_H

#include "libavutil/buffer.h"
#include "libavutil/frame.h"
#include "libavutil/hwcontext.h"

#include "avcodec.h"

/**
 * This struct stores per-frame lavc-internal data and is attached to it via
 * private_ref.
 */
typedef struct FrameDecodeData {
    /**
     * The callback to perform some delayed processing on the frame right
     * before it is returned to the caller.
     *
     * @note This code is called at some unspecified point after the frame is
     * returned from the decoder's decode/receive_frame call. Therefore it cannot rely
     * on AVCodecContext being in any specific state, so it does not get to
     * access AVCodecContext directly at all. All the state it needs must be
     * stored in the post_process_opaque object.
     */
    int (*post_process)(void *logctx, AVFrame *frame);
    void *post_process_opaque;
    void (*post_process_opaque_free)(void *opaque);

    /**
     * Per-frame private data for hwaccels.
     */
    void *hwaccel_priv;
    void (*hwaccel_priv_free)(void *priv);
} FrameDecodeData;

/**
 * Called by decoders to get the next packet for decoding.
 *
 * @param pkt An empty packet to be filled with data.
 * @return 0 if a new reference has been successfully written to pkt
 *         AVERROR(EAGAIN) if no data is currently available
 *         AVERROR_EOF if and end of stream has been reached, so no more data
 *                     will be available
 */
int ff_decode_get_packet(AVCodecContext *avctx, AVPacket *pkt);

/**
 * Set various frame properties from the codec context / packet data.
 */
int ff_decode_frame_props(AVCodecContext *avctx, AVFrame *frame);

/**
 * Make sure avctx.hw_frames_ctx is set. If it's not set, the function will
 * try to allocate it from hw_device_ctx. If that is not possible, an error
 * message is printed, and an error code is returned.
 */
int ff_decode_get_hw_frames_ctx(AVCodecContext *avctx,
                                enum AVHWDeviceType dev_type);

int ff_attach_decode_data(AVFrame *frame);

/**
 * Perform decoder initialization and validation.
 * Called when opening the decoder, before the AVCodec.init() call.
 */
int ff_decode_preinit(AVCodecContext *avctx);

#endif /* AVCODEC_DECODE_H */
