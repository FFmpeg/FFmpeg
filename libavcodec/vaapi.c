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

#include "libavutil/log.h"
#include "mpegvideo.h"
#include "vaapi_internal.h"

/**
 * @addtogroup VAAPI_Decoding
 *
 * @{
 */

static void destroy_buffers(VADisplay display, VABufferID *buffers, unsigned int n_buffers)
{
    unsigned int i;
    for (i = 0; i < n_buffers; i++) {
        if (buffers[i] != VA_INVALID_ID) {
            vaDestroyBuffer(display, buffers[i]);
            buffers[i] = VA_INVALID_ID;
        }
    }
}

int ff_vaapi_context_init(AVCodecContext *avctx)
{
    FFVAContext * const vactx = ff_vaapi_get_context(avctx);
    const struct vaapi_context * const user_vactx = avctx->hwaccel_context;

    if (!user_vactx) {
        av_log(avctx, AV_LOG_ERROR, "Hardware acceleration context (hwaccel_context) does not exist.\n");
        return AVERROR(ENOSYS);
    }

    vactx->display              = user_vactx->display;
    vactx->config_id            = user_vactx->config_id;
    vactx->context_id           = user_vactx->context_id;

    vactx->pic_param_buf_id     = VA_INVALID_ID;
    vactx->iq_matrix_buf_id     = VA_INVALID_ID;
    vactx->bitplane_buf_id      = VA_INVALID_ID;

    return 0;
}

int ff_vaapi_context_fini(AVCodecContext *avctx)
{
    return 0;
}

int ff_vaapi_render_picture(FFVAContext *vactx, VASurfaceID surface)
{
    VABufferID va_buffers[3];
    unsigned int n_va_buffers = 0;

    if (vactx->pic_param_buf_id == VA_INVALID_ID)
        return 0;

    vaUnmapBuffer(vactx->display, vactx->pic_param_buf_id);
    va_buffers[n_va_buffers++] = vactx->pic_param_buf_id;

    if (vactx->iq_matrix_buf_id != VA_INVALID_ID) {
        vaUnmapBuffer(vactx->display, vactx->iq_matrix_buf_id);
        va_buffers[n_va_buffers++] = vactx->iq_matrix_buf_id;
    }

    if (vactx->bitplane_buf_id != VA_INVALID_ID) {
        vaUnmapBuffer(vactx->display, vactx->bitplane_buf_id);
        va_buffers[n_va_buffers++] = vactx->bitplane_buf_id;
    }

    if (vaBeginPicture(vactx->display, vactx->context_id,
                       surface) != VA_STATUS_SUCCESS)
        return -1;

    if (vaRenderPicture(vactx->display, vactx->context_id,
                        va_buffers, n_va_buffers) != VA_STATUS_SUCCESS)
        return -1;

    if (vaRenderPicture(vactx->display, vactx->context_id,
                        vactx->slice_buf_ids,
                        vactx->n_slice_buf_ids) != VA_STATUS_SUCCESS)
        return -1;

    if (vaEndPicture(vactx->display, vactx->context_id) != VA_STATUS_SUCCESS)
        return -1;

    return 0;
}

int ff_vaapi_commit_slices(FFVAContext *vactx)
{
    VABufferID *slice_buf_ids;
    VABufferID slice_param_buf_id, slice_data_buf_id;

    if (vactx->slice_count == 0)
        return 0;

    slice_buf_ids =
        av_fast_realloc(vactx->slice_buf_ids,
                        &vactx->slice_buf_ids_alloc,
                        (vactx->n_slice_buf_ids + 2) * sizeof(slice_buf_ids[0]));
    if (!slice_buf_ids)
        return -1;
    vactx->slice_buf_ids = slice_buf_ids;

    slice_param_buf_id = VA_INVALID_ID;
    if (vaCreateBuffer(vactx->display, vactx->context_id,
                       VASliceParameterBufferType,
                       vactx->slice_param_size,
                       vactx->slice_count, vactx->slice_params,
                       &slice_param_buf_id) != VA_STATUS_SUCCESS)
        return -1;
    vactx->slice_count = 0;

    slice_data_buf_id = VA_INVALID_ID;
    if (vaCreateBuffer(vactx->display, vactx->context_id,
                       VASliceDataBufferType,
                       vactx->slice_data_size,
                       1, (void *)vactx->slice_data,
                       &slice_data_buf_id) != VA_STATUS_SUCCESS)
        return -1;
    vactx->slice_data = NULL;
    vactx->slice_data_size = 0;

    slice_buf_ids[vactx->n_slice_buf_ids++] = slice_param_buf_id;
    slice_buf_ids[vactx->n_slice_buf_ids++] = slice_data_buf_id;
    return 0;
}

static void *alloc_buffer(FFVAContext *vactx, int type, unsigned int size, uint32_t *buf_id)
{
    void *data = NULL;

    *buf_id = VA_INVALID_ID;
    if (vaCreateBuffer(vactx->display, vactx->context_id,
                       type, size, 1, NULL, buf_id) == VA_STATUS_SUCCESS)
        vaMapBuffer(vactx->display, *buf_id, &data);

    return data;
}

void *ff_vaapi_alloc_pic_param(FFVAContext *vactx, unsigned int size)
{
    return alloc_buffer(vactx, VAPictureParameterBufferType, size, &vactx->pic_param_buf_id);
}

void *ff_vaapi_alloc_iq_matrix(FFVAContext *vactx, unsigned int size)
{
    return alloc_buffer(vactx, VAIQMatrixBufferType, size, &vactx->iq_matrix_buf_id);
}

uint8_t *ff_vaapi_alloc_bitplane(FFVAContext *vactx, uint32_t size)
{
    return alloc_buffer(vactx, VABitPlaneBufferType, size, &vactx->bitplane_buf_id);
}

VASliceParameterBufferBase *ff_vaapi_alloc_slice(FFVAContext *vactx, const uint8_t *buffer, uint32_t size)
{
    uint8_t *slice_params;
    VASliceParameterBufferBase *slice_param;

    if (!vactx->slice_data)
        vactx->slice_data = buffer;
    if (vactx->slice_data + vactx->slice_data_size != buffer) {
        if (ff_vaapi_commit_slices(vactx) < 0)
            return NULL;
        vactx->slice_data = buffer;
    }

    slice_params =
        av_fast_realloc(vactx->slice_params,
                        &vactx->slice_params_alloc,
                        (vactx->slice_count + 1) * vactx->slice_param_size);
    if (!slice_params)
        return NULL;
    vactx->slice_params = slice_params;

    slice_param = (VASliceParameterBufferBase *)(slice_params + vactx->slice_count * vactx->slice_param_size);
    slice_param->slice_data_size   = size;
    slice_param->slice_data_offset = vactx->slice_data_size;
    slice_param->slice_data_flag   = VA_SLICE_DATA_FLAG_ALL;

    vactx->slice_count++;
    vactx->slice_data_size += size;
    return slice_param;
}

void ff_vaapi_common_end_frame(AVCodecContext *avctx)
{
    FFVAContext * const vactx = ff_vaapi_get_context(avctx);

    destroy_buffers(vactx->display, &vactx->pic_param_buf_id, 1);
    destroy_buffers(vactx->display, &vactx->iq_matrix_buf_id, 1);
    destroy_buffers(vactx->display, &vactx->bitplane_buf_id, 1);
    destroy_buffers(vactx->display, vactx->slice_buf_ids, vactx->n_slice_buf_ids);
    av_freep(&vactx->slice_buf_ids);
    av_freep(&vactx->slice_params);
    vactx->n_slice_buf_ids     = 0;
    vactx->slice_buf_ids_alloc = 0;
    vactx->slice_count         = 0;
    vactx->slice_params_alloc  = 0;
}

#if CONFIG_H263_VAAPI_HWACCEL  || CONFIG_MPEG1_VAAPI_HWACCEL || \
    CONFIG_MPEG2_VAAPI_HWACCEL || CONFIG_MPEG4_VAAPI_HWACCEL || \
    CONFIG_VC1_VAAPI_HWACCEL   || CONFIG_WMV3_VAAPI_HWACCEL
int ff_vaapi_mpeg_end_frame(AVCodecContext *avctx)
{
    FFVAContext * const vactx = ff_vaapi_get_context(avctx);
    MpegEncContext *s = avctx->priv_data;
    int ret;

    ret = ff_vaapi_commit_slices(vactx);
    if (ret < 0)
        goto finish;

    ret = ff_vaapi_render_picture(vactx,
                                  ff_vaapi_get_surface_id(s->current_picture_ptr->f));
    if (ret < 0)
        goto finish;

    ff_mpeg_draw_horiz_band(s, 0, s->avctx->height);

finish:
    ff_vaapi_common_end_frame(avctx);
    return ret;
}
#endif

/* @} */
