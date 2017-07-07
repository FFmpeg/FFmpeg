/*
 * Android MediaCodec public API
 *
 * Copyright (c) 2016 Matthieu Bouron <matthieu.bouron stupeflix.com>
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

#ifndef AVCODEC_MEDIACODEC_H
#define AVCODEC_MEDIACODEC_H

#include "libavcodec/avcodec.h"

/**
 * This structure holds a reference to a android/view/Surface object that will
 * be used as output by the decoder.
 *
 */
typedef struct AVMediaCodecContext {

    /**
     * android/view/Surface object reference.
     */
    void *surface;

} AVMediaCodecContext;

/**
 * Allocate and initialize a MediaCodec context.
 *
 * When decoding with MediaCodec is finished, the caller must free the
 * MediaCodec context with av_mediacodec_default_free.
 *
 * @return a pointer to a newly allocated AVMediaCodecContext on success, NULL otherwise
 */
AVMediaCodecContext *av_mediacodec_alloc_context(void);

/**
 * Convenience function that sets up the MediaCodec context.
 *
 * @param avctx codec context
 * @param ctx MediaCodec context to initialize
 * @param surface reference to an android/view/Surface
 * @return 0 on success, < 0 otherwise
 */
int av_mediacodec_default_init(AVCodecContext *avctx, AVMediaCodecContext *ctx, void *surface);

/**
 * This function must be called to free the MediaCodec context initialized with
 * av_mediacodec_default_init().
 *
 * @param avctx codec context
 */
void av_mediacodec_default_free(AVCodecContext *avctx);

/**
 * Opaque structure representing a MediaCodec buffer to render.
 */
typedef struct MediaCodecBuffer AVMediaCodecBuffer;

/**
 * Release a MediaCodec buffer and render it to the surface that is associated
 * with the decoder. This function should only be called once on a given
 * buffer, once released the underlying buffer returns to the codec, thus
 * subsequent calls to this function will have no effect.
 *
 * @param buffer the buffer to render
 * @param render 1 to release and render the buffer to the surface or 0 to
 * discard the buffer
 * @return 0 on success, < 0 otherwise
 */
int av_mediacodec_release_buffer(AVMediaCodecBuffer *buffer, int render);

#endif /* AVCODEC_MEDIACODEC_H */
