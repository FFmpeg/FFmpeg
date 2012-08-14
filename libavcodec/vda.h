/*
 * VDA HW acceleration
 *
 * copyright (c) 2011 Sebastien Zwickert
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

#ifndef AVCODEC_VDA_H
#define AVCODEC_VDA_H

/**
 * @file
 * @ingroup lavc_codec_hwaccel_vda
 * Public libavcodec VDA header.
 */

#include "libavcodec/version.h"

#if FF_API_VDA_ASYNC
#include <pthread.h>
#endif

#include <stdint.h>

// emmintrin.h is unable to compile with -std=c99 -Werror=missing-prototypes
// http://openradar.appspot.com/8026390
#undef __GNUC_STDC_INLINE__

#define Picture QuickdrawPicture
#include <VideoDecodeAcceleration/VDADecoder.h>
#undef Picture

/**
 * @defgroup lavc_codec_hwaccel_vda VDA
 * @ingroup lavc_codec_hwaccel
 *
 * @{
 */

#if FF_API_VDA_ASYNC
/**
 * This structure is used to store decoded frame information and data.
 *
 * @deprecated Use synchronous decoding mode.
 */
typedef struct {
    /**
     * The PTS of the frame.
     *
     * - encoding: unused
     * - decoding: Set/Unset by libavcodec.
     */
    int64_t             pts;

    /**
     * The CoreVideo buffer that contains the decoded data.
     *
     * - encoding: unused
     * - decoding: Set/Unset by libavcodec.
     */
    CVPixelBufferRef    cv_buffer;

    /**
     * A pointer to the next frame.
     *
     * - encoding: unused
     * - decoding: Set/Unset by libavcodec.
     */
    struct vda_frame    *next_frame;
} vda_frame;
#endif

/**
 * This structure is used to provide the necessary configurations and data
 * to the VDA FFmpeg HWAccel implementation.
 *
 * The application must make it available as AVCodecContext.hwaccel_context.
 */
struct vda_context {
    /**
     * VDA decoder object.
     *
     * - encoding: unused
     * - decoding: Set/Unset by libavcodec.
     */
    VDADecoder          decoder;

    /**
     * The Core Video pixel buffer that contains the current image data.
     *
     * encoding: unused
     * decoding: Set by libavcodec. Unset by user.
     */
    CVPixelBufferRef    cv_buffer;

    /**
     * Use the hardware decoder in synchronous mode.
     *
     * encoding: unused
     * decoding: Set by user.
     */
    int                 use_sync_decoding;

#if FF_API_VDA_ASYNC
    /**
     * VDA frames queue ordered by presentation timestamp.
     *
     * @deprecated Use synchronous decoding mode.
     *
     * - encoding: unused
     * - decoding: Set/Unset by libavcodec.
     */
    vda_frame           *queue;

    /**
     * Mutex for locking queue operations.
     *
     * @deprecated Use synchronous decoding mode.
     *
     * - encoding: unused
     * - decoding: Set/Unset by libavcodec.
     */
    pthread_mutex_t     queue_mutex;
#endif

    /**
     * The frame width.
     *
     * - encoding: unused
     * - decoding: Set/Unset by user.
     */
    int                 width;

    /**
     * The frame height.
     *
     * - encoding: unused
     * - decoding: Set/Unset by user.
     */
    int                 height;

    /**
     * The frame format.
     *
     * - encoding: unused
     * - decoding: Set/Unset by user.
     */
    int                 format;

    /**
     * The pixel format for output image buffers.
     *
     * - encoding: unused
     * - decoding: Set/Unset by user.
     */
    OSType              cv_pix_fmt_type;

    /**
     * The current bitstream buffer.
     *
     * - encoding: unused
     * - decoding: Set/Unset by libavcodec.
     */
    uint8_t             *priv_bitstream;

    /**
     * The current size of the bitstream.
     *
     * - encoding: unused
     * - decoding: Set/Unset by libavcodec.
     */
    int                 priv_bitstream_size;

    /**
     * The reference size used for fast reallocation.
     *
     * - encoding: unused
     * - decoding: Set/Unset by libavcodec.
     */
    int                 priv_allocated_size;
};

/** Create the video decoder. */
int ff_vda_create_decoder(struct vda_context *vda_ctx,
                          uint8_t *extradata,
                          int extradata_size);

/** Destroy the video decoder. */
int ff_vda_destroy_decoder(struct vda_context *vda_ctx);

#if FF_API_VDA_ASYNC
/**
 * Return the top frame of the queue.
 *
 * @deprecated Use synchronous decoding mode.
 */
vda_frame *ff_vda_queue_pop(struct vda_context *vda_ctx);

/**
 * Release the given frame.
 *
 * @deprecated Use synchronous decoding mode.
 */
void ff_vda_release_vda_frame(vda_frame *frame);
#endif

/**
 * @}
 */

#endif /* AVCODEC_VDA_H */
