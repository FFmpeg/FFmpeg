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

/** Extract VASurfaceID from an AVFrame */
static inline VASurfaceID ff_vaapi_get_surface_id(AVFrame *pic)
{
    return (uintptr_t)pic->data[3];
}

/** Common AVHWAccel.end_frame() implementation */
void ff_vaapi_common_end_frame(AVCodecContext *avctx);

/** Allocate a new picture parameter buffer */
void *ff_vaapi_alloc_pic_param(struct vaapi_context *vactx, unsigned int size);

/** Allocate a new IQ matrix buffer */
void *ff_vaapi_alloc_iq_matrix(struct vaapi_context *vactx, unsigned int size);

/** Allocate a new bit-plane buffer */
uint8_t *ff_vaapi_alloc_bitplane(struct vaapi_context *vactx, uint32_t size);

/**
 * Allocate a new slice descriptor for the input slice.
 *
 * @param vactx the VA API context
 * @param buffer the slice data buffer base
 * @param size the size of the slice in bytes
 * @return the newly allocated slice parameter
 */
VASliceParameterBufferBase *ff_vaapi_alloc_slice(struct vaapi_context *vactx, const uint8_t *buffer, uint32_t size);

int ff_vaapi_mpeg_end_frame(AVCodecContext *avctx);
int ff_vaapi_commit_slices(struct vaapi_context *vactx);
int ff_vaapi_render_picture(struct vaapi_context *vactx, VASurfaceID surface);

/* @} */

#endif /* AVCODEC_VAAPI_INTERNAL_H */
