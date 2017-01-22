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

#include "config.h"

#include "buffer.h"
#include "common.h"
#include "hwcontext.h"
#include "hwcontext_internal.h"
#include "imgutils.h"
#include "log.h"
#include "mem.h"
#include "pixdesc.h"
#include "pixfmt.h"

static const HWContextType *hw_table[] = {
#if CONFIG_CUDA
    &ff_hwcontext_type_cuda,
#endif
#if CONFIG_DXVA2
    &ff_hwcontext_type_dxva2,
#endif
#if CONFIG_QSV
    &ff_hwcontext_type_qsv,
#endif
#if CONFIG_VAAPI
    &ff_hwcontext_type_vaapi,
#endif
#if CONFIG_VDPAU
    &ff_hwcontext_type_vdpau,
#endif
    NULL,
};

static const AVClass hwdevice_ctx_class = {
    .class_name = "AVHWDeviceContext",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

static void hwdevice_ctx_free(void *opaque, uint8_t *data)
{
    AVHWDeviceContext *ctx = (AVHWDeviceContext*)data;

    /* uninit might still want access the hw context and the user
     * free() callback might destroy it, so uninit has to be called first */
    if (ctx->internal->hw_type->device_uninit)
        ctx->internal->hw_type->device_uninit(ctx);

    if (ctx->free)
        ctx->free(ctx);

    av_freep(&ctx->hwctx);
    av_freep(&ctx->internal->priv);
    av_freep(&ctx->internal);
    av_freep(&ctx);
}

AVBufferRef *av_hwdevice_ctx_alloc(enum AVHWDeviceType type)
{
    AVHWDeviceContext *ctx;
    AVBufferRef *buf;
    const HWContextType *hw_type = NULL;
    int i;

    for (i = 0; hw_table[i]; i++) {
        if (hw_table[i]->type == type) {
            hw_type = hw_table[i];
            break;
        }
    }
    if (!hw_type)
        return NULL;

    ctx = av_mallocz(sizeof(*ctx));
    if (!ctx)
        return NULL;

    ctx->internal = av_mallocz(sizeof(*ctx->internal));
    if (!ctx->internal)
        goto fail;

    if (hw_type->device_priv_size) {
        ctx->internal->priv = av_mallocz(hw_type->device_priv_size);
        if (!ctx->internal->priv)
            goto fail;
    }

    if (hw_type->device_hwctx_size) {
        ctx->hwctx = av_mallocz(hw_type->device_hwctx_size);
        if (!ctx->hwctx)
            goto fail;
    }

    buf = av_buffer_create((uint8_t*)ctx, sizeof(*ctx),
                           hwdevice_ctx_free, NULL,
                           AV_BUFFER_FLAG_READONLY);
    if (!buf)
        goto fail;

    ctx->type     = type;
    ctx->av_class = &hwdevice_ctx_class;

    ctx->internal->hw_type = hw_type;

    return buf;

fail:
    if (ctx->internal)
        av_freep(&ctx->internal->priv);
    av_freep(&ctx->internal);
    av_freep(&ctx->hwctx);
    av_freep(&ctx);
    return NULL;
}

int av_hwdevice_ctx_init(AVBufferRef *ref)
{
    AVHWDeviceContext *ctx = (AVHWDeviceContext*)ref->data;
    int ret;

    if (ctx->internal->hw_type->device_init) {
        ret = ctx->internal->hw_type->device_init(ctx);
        if (ret < 0)
            goto fail;
    }

    return 0;
fail:
    if (ctx->internal->hw_type->device_uninit)
        ctx->internal->hw_type->device_uninit(ctx);
    return ret;
}

static const AVClass hwframe_ctx_class = {
    .class_name = "AVHWFramesContext",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

static void hwframe_ctx_free(void *opaque, uint8_t *data)
{
    AVHWFramesContext *ctx = (AVHWFramesContext*)data;

    if (ctx->internal->pool_internal)
        av_buffer_pool_uninit(&ctx->internal->pool_internal);

    if (ctx->internal->hw_type->frames_uninit)
        ctx->internal->hw_type->frames_uninit(ctx);

    if (ctx->free)
        ctx->free(ctx);

    av_buffer_unref(&ctx->device_ref);

    av_freep(&ctx->hwctx);
    av_freep(&ctx->internal->priv);
    av_freep(&ctx->internal);
    av_freep(&ctx);
}

AVBufferRef *av_hwframe_ctx_alloc(AVBufferRef *device_ref_in)
{
    AVHWDeviceContext *device_ctx = (AVHWDeviceContext*)device_ref_in->data;
    const HWContextType  *hw_type = device_ctx->internal->hw_type;
    AVHWFramesContext *ctx;
    AVBufferRef *buf, *device_ref = NULL;

    ctx = av_mallocz(sizeof(*ctx));
    if (!ctx)
        return NULL;

    ctx->internal = av_mallocz(sizeof(*ctx->internal));
    if (!ctx->internal)
        goto fail;

    if (hw_type->frames_priv_size) {
        ctx->internal->priv = av_mallocz(hw_type->frames_priv_size);
        if (!ctx->internal->priv)
            goto fail;
    }

    if (hw_type->frames_hwctx_size) {
        ctx->hwctx = av_mallocz(hw_type->frames_hwctx_size);
        if (!ctx->hwctx)
            goto fail;
    }

    device_ref = av_buffer_ref(device_ref_in);
    if (!device_ref)
        goto fail;

    buf = av_buffer_create((uint8_t*)ctx, sizeof(*ctx),
                           hwframe_ctx_free, NULL,
                           AV_BUFFER_FLAG_READONLY);
    if (!buf)
        goto fail;

    ctx->av_class   = &hwframe_ctx_class;
    ctx->device_ref = device_ref;
    ctx->device_ctx = device_ctx;
    ctx->format     = AV_PIX_FMT_NONE;
    ctx->sw_format  = AV_PIX_FMT_NONE;

    ctx->internal->hw_type = hw_type;

    return buf;

fail:
    if (device_ref)
        av_buffer_unref(&device_ref);
    if (ctx->internal)
        av_freep(&ctx->internal->priv);
    av_freep(&ctx->internal);
    av_freep(&ctx->hwctx);
    av_freep(&ctx);
    return NULL;
}

static int hwframe_pool_prealloc(AVBufferRef *ref)
{
    AVHWFramesContext *ctx = (AVHWFramesContext*)ref->data;
    AVFrame **frames;
    int i, ret = 0;

    frames = av_mallocz_array(ctx->initial_pool_size, sizeof(*frames));
    if (!frames)
        return AVERROR(ENOMEM);

    for (i = 0; i < ctx->initial_pool_size; i++) {
        frames[i] = av_frame_alloc();
        if (!frames[i])
            goto fail;

        ret = av_hwframe_get_buffer(ref, frames[i], 0);
        if (ret < 0)
            goto fail;
    }

fail:
    for (i = 0; i < ctx->initial_pool_size; i++)
        av_frame_free(&frames[i]);
    av_freep(&frames);

    return ret;
}

int av_hwframe_ctx_init(AVBufferRef *ref)
{
    AVHWFramesContext *ctx = (AVHWFramesContext*)ref->data;
    const enum AVPixelFormat *pix_fmt;
    int ret;

    /* validate the pixel format */
    for (pix_fmt = ctx->internal->hw_type->pix_fmts; *pix_fmt != AV_PIX_FMT_NONE; pix_fmt++) {
        if (*pix_fmt == ctx->format)
            break;
    }
    if (*pix_fmt == AV_PIX_FMT_NONE) {
        av_log(ctx, AV_LOG_ERROR,
               "The hardware pixel format '%s' is not supported by the device type '%s'\n",
               av_get_pix_fmt_name(ctx->format), ctx->internal->hw_type->name);
        return AVERROR(ENOSYS);
    }

    /* validate the dimensions */
    ret = av_image_check_size(ctx->width, ctx->height, 0, ctx);
    if (ret < 0)
        return ret;

    /* format-specific init */
    if (ctx->internal->hw_type->frames_init) {
        ret = ctx->internal->hw_type->frames_init(ctx);
        if (ret < 0)
            goto fail;
    }

    if (ctx->internal->pool_internal && !ctx->pool)
        ctx->pool = ctx->internal->pool_internal;

    /* preallocate the frames in the pool, if requested */
    if (ctx->initial_pool_size > 0) {
        ret = hwframe_pool_prealloc(ref);
        if (ret < 0)
            goto fail;
    }

    return 0;
fail:
    if (ctx->internal->hw_type->frames_uninit)
        ctx->internal->hw_type->frames_uninit(ctx);
    return ret;
}

int av_hwframe_transfer_get_formats(AVBufferRef *hwframe_ref,
                                    enum AVHWFrameTransferDirection dir,
                                    enum AVPixelFormat **formats, int flags)
{
    AVHWFramesContext *ctx = (AVHWFramesContext*)hwframe_ref->data;

    if (!ctx->internal->hw_type->transfer_get_formats)
        return AVERROR(ENOSYS);

    return ctx->internal->hw_type->transfer_get_formats(ctx, dir, formats);
}

static int transfer_data_alloc(AVFrame *dst, const AVFrame *src, int flags)
{
    AVHWFramesContext *ctx = (AVHWFramesContext*)src->hw_frames_ctx->data;
    AVFrame *frame_tmp;
    int ret = 0;

    frame_tmp = av_frame_alloc();
    if (!frame_tmp)
        return AVERROR(ENOMEM);

    /* if the format is set, use that
     * otherwise pick the first supported one */
    if (dst->format >= 0) {
        frame_tmp->format = dst->format;
    } else {
        enum AVPixelFormat *formats;

        ret = av_hwframe_transfer_get_formats(src->hw_frames_ctx,
                                              AV_HWFRAME_TRANSFER_DIRECTION_FROM,
                                              &formats, 0);
        if (ret < 0)
            goto fail;
        frame_tmp->format = formats[0];
        av_freep(&formats);
    }
    frame_tmp->width  = ctx->width;
    frame_tmp->height = ctx->height;

    ret = av_frame_get_buffer(frame_tmp, 32);
    if (ret < 0)
        goto fail;

    ret = av_hwframe_transfer_data(frame_tmp, src, flags);
    if (ret < 0)
        goto fail;

    frame_tmp->width  = src->width;
    frame_tmp->height = src->height;

    av_frame_move_ref(dst, frame_tmp);

fail:
    av_frame_free(&frame_tmp);
    return ret;
}

int av_hwframe_transfer_data(AVFrame *dst, const AVFrame *src, int flags)
{
    AVHWFramesContext *ctx;
    int ret;

    if (!dst->buf[0])
        return transfer_data_alloc(dst, src, flags);

    if (src->hw_frames_ctx) {
        ctx = (AVHWFramesContext*)src->hw_frames_ctx->data;

        ret = ctx->internal->hw_type->transfer_data_from(ctx, dst, src);
        if (ret < 0)
            return ret;
    } else if (dst->hw_frames_ctx) {
        ctx = (AVHWFramesContext*)dst->hw_frames_ctx->data;

        ret = ctx->internal->hw_type->transfer_data_to(ctx, dst, src);
        if (ret < 0)
            return ret;
    } else
        return AVERROR(ENOSYS);

    return 0;
}

int av_hwframe_get_buffer(AVBufferRef *hwframe_ref, AVFrame *frame, int flags)
{
    AVHWFramesContext *ctx = (AVHWFramesContext*)hwframe_ref->data;
    int ret;

    if (!ctx->internal->hw_type->frames_get_buffer)
        return AVERROR(ENOSYS);

    if (!ctx->pool)
        return AVERROR(EINVAL);

    frame->hw_frames_ctx = av_buffer_ref(hwframe_ref);
    if (!frame->hw_frames_ctx)
        return AVERROR(ENOMEM);

    ret = ctx->internal->hw_type->frames_get_buffer(ctx, frame);
    if (ret < 0) {
        av_buffer_unref(&frame->hw_frames_ctx);
        return ret;
    }

    return 0;
}

void *av_hwdevice_hwconfig_alloc(AVBufferRef *ref)
{
    AVHWDeviceContext *ctx = (AVHWDeviceContext*)ref->data;
    const HWContextType  *hw_type = ctx->internal->hw_type;

    if (hw_type->device_hwconfig_size == 0)
        return NULL;

    return av_mallocz(hw_type->device_hwconfig_size);
}

AVHWFramesConstraints *av_hwdevice_get_hwframe_constraints(AVBufferRef *ref,
                                                           const void *hwconfig)
{
    AVHWDeviceContext *ctx = (AVHWDeviceContext*)ref->data;
    const HWContextType  *hw_type = ctx->internal->hw_type;
    AVHWFramesConstraints *constraints;

    if (!hw_type->frames_get_constraints)
        return NULL;

    constraints = av_mallocz(sizeof(*constraints));
    if (!constraints)
        return NULL;

    constraints->min_width = constraints->min_height = 0;
    constraints->max_width = constraints->max_height = INT_MAX;

    if (hw_type->frames_get_constraints(ctx, hwconfig, constraints) >= 0) {
        return constraints;
    } else {
        av_hwframe_constraints_free(&constraints);
        return NULL;
    }
}

void av_hwframe_constraints_free(AVHWFramesConstraints **constraints)
{
    if (*constraints) {
        av_freep(&(*constraints)->valid_hw_formats);
        av_freep(&(*constraints)->valid_sw_formats);
    }
    av_freep(constraints);
}

int av_hwdevice_ctx_create(AVBufferRef **pdevice_ref, enum AVHWDeviceType type,
                           const char *device, AVDictionary *opts, int flags)
{
    AVBufferRef *device_ref = NULL;
    AVHWDeviceContext *device_ctx;
    int ret = 0;

    device_ref = av_hwdevice_ctx_alloc(type);
    if (!device_ref) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    device_ctx = (AVHWDeviceContext*)device_ref->data;

    if (!device_ctx->internal->hw_type->device_create) {
        ret = AVERROR(ENOSYS);
        goto fail;
    }

    ret = device_ctx->internal->hw_type->device_create(device_ctx, device,
                                                       opts, flags);
    if (ret < 0)
        goto fail;

    ret = av_hwdevice_ctx_init(device_ref);
    if (ret < 0)
        goto fail;

    *pdevice_ref = device_ref;
    return 0;
fail:
    av_buffer_unref(&device_ref);
    *pdevice_ref = NULL;
    return ret;
}
