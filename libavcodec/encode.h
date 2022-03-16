/*
 * generic encoding-related code
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

#ifndef AVCODEC_ENCODE_H
#define AVCODEC_ENCODE_H

#include "libavutil/frame.h"

#include "avcodec.h"
#include "packet.h"

/**
 * Called by encoders to get the next frame for encoding.
 *
 * @param frame An empty frame to be filled with data.
 * @return 0 if a new reference has been successfully written to frame
 *         AVERROR(EAGAIN) if no data is currently available
 *         AVERROR_EOF if end of stream has been reached, so no more data
 *                     will be available
 */
int ff_encode_get_frame(AVCodecContext *avctx, AVFrame *frame);

/**
 * Get a buffer for a packet. This is a wrapper around
 * AVCodecContext.get_encode_buffer() and should be used instead calling get_encode_buffer()
 * directly.
 */
int ff_get_encode_buffer(AVCodecContext *avctx, AVPacket *avpkt, int64_t size, int flags);

/**
 * Check AVPacket size and allocate data.
 *
 * Encoders supporting FFCodec.encode2() can use this as a convenience to
 * obtain a big enough buffer for the encoded bitstream.
 *
 * @param avctx   the AVCodecContext of the encoder
 * @param avpkt   The AVPacket: on success, avpkt->data will point to a buffer
 *                of size at least `size`; the packet will not be refcounted.
 *                This packet must be initially blank.
 * @param size    an upper bound of the size of the packet to encode
 * @return        non negative on success, negative error code on failure
 */
int ff_alloc_packet(AVCodecContext *avctx, AVPacket *avpkt, int64_t size);

/*
 * Perform encoder initialization and validation.
 * Called when opening the encoder, before the FFCodec.init() call.
 */
int ff_encode_preinit(AVCodecContext *avctx);

#endif /* AVCODEC_ENCODE_H */
