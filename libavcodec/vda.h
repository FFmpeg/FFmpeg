/*
 * VDA HW acceleration
 *
 * copyright (c) 2011 Sebastien Zwickert
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_VDA_H
#define AVCODEC_VDA_H

/**
 * @file
 * @ingroup lavc_codec_hwaccel_vda
 * Public libavcodec VDA header.
 */

#include "libavcodec/avcodec.h"
#include "libavcodec/version.h"

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

/**
 * This structure is used to provide the necessary configurations and data
 * to the VDA Libav HWAccel implementation.
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
     * unused
     */
    uint8_t             *priv_bitstream;

    /**
     * unused
     */
    int                 priv_bitstream_size;

    /**
     * unused
     */
    int                 priv_allocated_size;
};

/** Create the video decoder. */
int ff_vda_create_decoder(struct vda_context *vda_ctx,
                          uint8_t *extradata,
                          int extradata_size);

/** Destroy the video decoder. */
int ff_vda_destroy_decoder(struct vda_context *vda_ctx);

/**
 * This struct holds all the information that needs to be passed
 * between the caller and libavcodec for initializing VDA decoding.
 * Its size is not a part of the public ABI, it must be allocated with
 * av_vda_alloc_context() and freed with av_free().
 */
typedef struct AVVDAContext {
    /**
     * VDA decoder object. Created and freed by the caller.
     */
    VDADecoder decoder;

    /**
     * The output callback that must be passed to VDADecoderCreate.
     * Set by av_vda_alloc_context().
     */
    VDADecoderOutputCallback output_callback;

    /**
     * CVPixelBuffer Format Type that VDA will use for decoded frames; set by
     * the caller.
     */
    OSType cv_pix_fmt_type;
} AVVDAContext;

/**
 * Allocate and initialize a VDA context.
 *
 * This function should be called from the get_format() callback when the caller
 * selects the AV_PIX_FMT_VDA format. The caller must then create the decoder
 * object (using the output callback provided by libavcodec) that will be used
 * for VDA-accelerated decoding.
 *
 * When decoding with VDA is finished, the caller must destroy the decoder
 * object and free the VDA context using av_free().
 *
 * @return the newly allocated context or NULL on failure
 */
AVVDAContext *av_vda_alloc_context(void);

/**
 * This is a convenience function that creates and sets up the VDA context using
 * an internal implementation.
 *
 * @param avctx the corresponding codec context
 *
 * @return >= 0 on success, a negative AVERROR code on failure
 */
int av_vda_default_init(AVCodecContext *avctx);

/**
 * This is a convenience function that creates and sets up the VDA context using
 * an internal implementation.
 *
 * @param avctx the corresponding codec context
 * @param vdactx the VDA context to use
 *
 * @return >= 0 on success, a negative AVERROR code on failure
 */
int av_vda_default_init2(AVCodecContext *avctx, AVVDAContext *vdactx);

/**
 * This function must be called to free the VDA context initialized with
 * av_vda_default_init().
 *
 * @param avctx the corresponding codec context
 */
void av_vda_default_free(AVCodecContext *avctx);

/**
 * @}
 */

#endif /* AVCODEC_VDA_H */
