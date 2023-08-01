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
 * Set various frame properties from the provided packet.
 */
int ff_decode_frame_props_from_pkt(const AVCodecContext *avctx,
                                   AVFrame *frame, const AVPacket *pkt);

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
 * Check whether the side-data of src contains a palette of
 * size AVPALETTE_SIZE; if so, copy it to dst and return 1;
 * else return 0.
 * Also emit an error message upon encountering a palette
 * with invalid size.
 */
int ff_copy_palette(void *dst, const AVPacket *src, void *logctx);

/**
 * Check that the provided frame dimensions are valid and set them on the codec
 * context.
 */
int ff_set_dimensions(AVCodecContext *s, int width, int height);

/**
 * Check that the provided sample aspect ratio is valid and set it on the codec
 * context.
 */
int ff_set_sar(AVCodecContext *avctx, AVRational sar);

/**
 * Select the (possibly hardware accelerated) pixel format.
 * This is a wrapper around AVCodecContext.get_format() and should be used
 * instead of calling get_format() directly.
 *
 * The list of pixel formats must contain at least one valid entry, and is
 * terminated with AV_PIX_FMT_NONE.  If it is possible to decode to software,
 * the last entry in the list must be the most accurate software format.
 * If it is not possible to decode to software, AVCodecContext.sw_pix_fmt
 * must be set before calling this function.
 */
int ff_get_format(AVCodecContext *avctx, const enum AVPixelFormat *fmt);

/**
 * Get a buffer for a frame. This is a wrapper around
 * AVCodecContext.get_buffer() and should be used instead calling get_buffer()
 * directly.
 */
int ff_get_buffer(AVCodecContext *avctx, AVFrame *frame, int flags);

#define FF_REGET_BUFFER_FLAG_READONLY 1 ///< the returned buffer does not need to be writable
/**
 * Identical in function to ff_get_buffer(), except it reuses the existing buffer
 * if available.
 */
int ff_reget_buffer(AVCodecContext *avctx, AVFrame *frame, int flags);

/**
 * Add or update AV_FRAME_DATA_MATRIXENCODING side data.
 */
int ff_side_data_update_matrix_encoding(AVFrame *frame,
                                        enum AVMatrixEncoding matrix_encoding);

/**
 * Allocate a hwaccel frame private data if the provided avctx
 * uses a hwaccel method that needs it. The private data will
 * be refcounted via the AVBuffer API (if allocated).
 *
 * @param  avctx                   The codec context
 * @param  hwaccel_picture_private Pointer to return hwaccel_picture_private
 * @param  hwaccel_priv_buf        Pointer to return the AVBufferRef owning
 *                                 hwaccel_picture_private
 * @return 0 on success, < 0 on error
 */
int ff_hwaccel_frame_priv_alloc(AVCodecContext *avctx, void **hwaccel_picture_private,
                                AVBufferRef **hwaccel_priv_buf);

#endif /* AVCODEC_DECODE_H */
