/*
 * HW decode acceleration through NVDEC
 *
 * Copyright (c) 2016 Anton Khirnov
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

#ifndef AVCODEC_NVDEC_H
#define AVCODEC_NVDEC_H

#include "compat/cuda/dynlink_loader.h"

#include <stdint.h>

#include "libavutil/buffer.h"
#include "libavutil/frame.h"

#include "avcodec.h"

#if defined(NVDECAPI_MAJOR_VERSION) && defined(NVDECAPI_MINOR_VERSION)
# define NVDECAPI_CHECK_VERSION(major, minor) \
    ((major) < NVDECAPI_MAJOR_VERSION || ((major) == NVDECAPI_MAJOR_VERSION && (minor) <= NVDECAPI_MINOR_VERSION))
#else
/* version macros were added in SDK 8.1 ffnvcodec */
# define NVDECAPI_CHECK_VERSION(major, minor) \
    ((major) < 8 || ((major) == 8 && (minor) <= 0))
#endif

// SDK 13.0 compile time feature checks
#if NVDECAPI_CHECK_VERSION(13, 0)
#define NVDEC_HAVE_422_SUPPORT
#endif

// SDK 13.1 compile time feature checks
#if NVDECAPI_CHECK_VERSION(13, 1)
#define NVDEC_HAVE_OPAQUE_OUTPUT_SUPPORT
#define NVDEC_HAVE_MVHEVC_DECODE
/* Largest ulNumDecodeSurfaces the hardware can address; the SDK only names the
 * limit from 13.1 on, where it also bounds cuvidRegisterDecodeSurfaces(). */
#define NVDEC_MAX_DECODE_SURFACES MAX_NUM_REGISTERED_DECODE_SURFACES
#else
#define NVDEC_MAX_DECODE_SURFACES 32
#endif

typedef struct NVDECFrame {
    unsigned int idx;
    unsigned int ref_idx;
    unsigned int *idx_ref;         ///< RefStruct reference
    unsigned int *ref_idx_ref;     ///< RefStruct reference
    struct NVDECDecoder  *decoder; ///< RefStruct reference
} NVDECFrame;

typedef struct NVDECContext {
    CUVIDPICPARAMS pic_params;

    struct AVRefStructPool *decoder_pool;

    struct NVDECDecoder  *decoder; ///< RefStruct reference

    const uint8_t *bitstream;
    int           bitstream_len;
    unsigned int  bitstream_allocated;
    uint8_t      *bitstream_internal;

    unsigned     *slice_offsets;
    int           nb_slices;
    unsigned int  slice_offsets_allocated;

    int           supports_444;

    /* Nonzero when every requested decode surface is actually needed, in which
     * case exceeding the hardware limit must fail instead of being capped.
     * The value is the number of surfaces consumed per access unit. */
    int           strict_pool_layers;

    /* Decode surfaces the hwaccel requires, independent of the frames context
     * in use.  frame_params() is skipped for a caller-supplied context, so the
     * pool it carries is not on its own proof that the stream fits. */
    int           strict_pool_min;
} NVDECContext;

int ff_nvdec_decode_init(AVCodecContext *avctx);
int ff_nvdec_decode_uninit(AVCodecContext *avctx);
int ff_nvdec_start_frame(AVCodecContext *avctx, AVFrame *frame);
int ff_nvdec_start_frame_sep_ref(AVCodecContext *avctx, AVFrame *frame, int has_sep_ref);
int ff_nvdec_end_frame(AVCodecContext *avctx);
int ff_nvdec_simple_end_frame(AVCodecContext *avctx);
int ff_nvdec_simple_decode_slice(AVCodecContext *avctx, const uint8_t *buffer,
                                 uint32_t size);
int ff_nvdec_frame_params(AVCodecContext *avctx,
                          AVBufferRef *hw_frames_ctx,
                          enum AVPixelFormat hw_format,
                          int dpb_size,
                          int supports_444);
int ff_nvdec_get_ref_idx(AVFrame *frame);

#ifdef NVDEC_HAVE_OPAQUE_OUTPUT_SUPPORT
void ff_nvdec_fill_cuarray_desc(CUDA_ARRAY3D_DESCRIPTOR *desc,
                                AVCodecContext *avctx,
                                cudaVideoSurfaceFormat output_format);
#endif

#endif /* AVCODEC_NVDEC_H */
