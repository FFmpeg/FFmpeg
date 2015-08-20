/*
 * Video Acceleration API (video decoding)
 * HW decode acceleration for MPEG-2, MPEG-4, H.264 and VC-1
 *
 * Copyright (C) 2008-2009 Splitted-Desktop Systems
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

#ifndef AVCODEC_VAAPI_INTERNAL_H
#define AVCODEC_VAAPI_INTERNAL_H

#include <va/va.h>
#include "vaapi.h"
#include "avcodec.h"
#include "mpegvideo.h"

/**
 * @addtogroup VAAPI_Decoding
 *
 * @{
 */

typedef struct {
    VADisplay display;                  ///< Windowing system dependent handle
    VAConfigID config_id;               ///< Configuration ID
    VAContextID context_id;             ///< Context ID (video decode pipeline)
    VABufferID pic_param_buf_id;        ///< Picture parameter buffer
    VABufferID iq_matrix_buf_id;        ///< Inverse quantiser matrix buffer
    VABufferID bitplane_buf_id;         ///< Bitplane buffer (for VC-1 decoding)
    VABufferID *slice_buf_ids;          ///< Slice parameter/data buffers
    unsigned int n_slice_buf_ids;       ///< Number of effective slice buffers
    unsigned int slice_buf_ids_alloc;   ///< Number of allocated slice buffers
    void *slice_params;                 ///< Pointer to slice parameter buffers
    unsigned int slice_param_size;      ///< Size of a slice parameter element
    unsigned int slice_params_alloc;    ///< Number of allocated slice parameters
    unsigned int slice_count;           ///< Number of slices currently filled in
    const uint8_t *slice_data;          ///< Pointer to slice data buffer base
    unsigned int slice_data_size;       ///< Current size of slice data
} FFVAContext;

/** Extract vaapi_context from an AVCodecContext */
static inline FFVAContext *ff_vaapi_get_context(AVCodecContext *avctx)
{
    return avctx->internal->hwaccel_priv_data;
}

/** Extract VASurfaceID from an AVFrame */
static inline VASurfaceID ff_vaapi_get_surface_id(AVFrame *pic)
{
    return (uintptr_t)pic->data[3];
}

/** Common AVHWAccel.init() implementation */
int ff_vaapi_context_init(AVCodecContext *avctx);

/** Common AVHWAccel.uninit() implementation */
int ff_vaapi_context_fini(AVCodecContext *avctx);

/** Common AVHWAccel.end_frame() implementation */
void ff_vaapi_common_end_frame(AVCodecContext *avctx);

/** Allocate a new picture parameter buffer */
void *ff_vaapi_alloc_pic_param(FFVAContext *vactx, unsigned int size);

/** Allocate a new IQ matrix buffer */
void *ff_vaapi_alloc_iq_matrix(FFVAContext *vactx, unsigned int size);

/** Allocate a new bit-plane buffer */
uint8_t *ff_vaapi_alloc_bitplane(FFVAContext *vactx, uint32_t size);

/**
 * Allocate a new slice descriptor for the input slice.
 *
 * @param vactx the VA API context
 * @param buffer the slice data buffer base
 * @param size the size of the slice in bytes
 * @return the newly allocated slice parameter
 */
VASliceParameterBufferBase *ff_vaapi_alloc_slice(FFVAContext *vactx, const uint8_t *buffer, uint32_t size);

int ff_vaapi_mpeg_end_frame(AVCodecContext *avctx);
int ff_vaapi_commit_slices(FFVAContext *vactx);
int ff_vaapi_render_picture(FFVAContext *vactx, VASurfaceID surface);

/* @} */

#endif /* AVCODEC_VAAPI_INTERNAL_H */
