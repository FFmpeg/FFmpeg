/*
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

#ifndef AVCODEC_VAAPI_DECODE_H
#define AVCODEC_VAAPI_DECODE_H

#include <va/va.h>

#include "libavutil/frame.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_vaapi.h"

#include "avcodec.h"

#include "version.h"
#if FF_API_STRUCT_VAAPI_CONTEXT
#include "vaapi.h"
#endif

static inline VASurfaceID ff_vaapi_get_surface_id(AVFrame *pic)
{
    return (uintptr_t)pic->data[3];
}

enum {
    MAX_PARAM_BUFFERS = 16,
};

typedef struct VAAPIDecodePicture {
    VASurfaceID           output_surface;

    int                nb_param_buffers;
    VABufferID            param_buffers[MAX_PARAM_BUFFERS];

    int                nb_slices;
    VABufferID           *slice_buffers;
    int                   slices_allocated;
} VAAPIDecodePicture;

typedef struct VAAPIDecodeContext {
    VAConfigID            va_config;
    VAContextID           va_context;

#if FF_API_STRUCT_VAAPI_CONTEXT
FF_DISABLE_DEPRECATION_WARNINGS
    int                   have_old_context;
    struct vaapi_context *old_context;
    AVBufferRef          *device_ref;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    AVHWDeviceContext    *device;
    AVVAAPIDeviceContext *hwctx;

    AVHWFramesContext    *frames;
    AVVAAPIFramesContext *hwfc;

    enum AVPixelFormat    surface_format;
    int                   surface_count;

    VASurfaceAttrib       pixel_format_attribute;
} VAAPIDecodeContext;


int ff_vaapi_decode_make_param_buffer(AVCodecContext *avctx,
                                      VAAPIDecodePicture *pic,
                                      int type,
                                      const void *data,
                                      size_t size);

int ff_vaapi_decode_make_slice_buffer(AVCodecContext *avctx,
                                      VAAPIDecodePicture *pic,
                                      const void *params_data,
                                      size_t params_size,
                                      const void *slice_data,
                                      size_t slice_size);

int ff_vaapi_decode_issue(AVCodecContext *avctx,
                          VAAPIDecodePicture *pic);
int ff_vaapi_decode_cancel(AVCodecContext *avctx,
                           VAAPIDecodePicture *pic);

int ff_vaapi_decode_init(AVCodecContext *avctx);
int ff_vaapi_decode_uninit(AVCodecContext *avctx);

int ff_vaapi_common_frame_params(AVCodecContext *avctx,
                                 AVBufferRef *hw_frames_ctx);

#endif /* AVCODEC_VAAPI_DECODE_H */
