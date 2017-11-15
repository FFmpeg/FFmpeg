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

static const HWContextType * const hw_table[] = {
#if CONFIG_CUDA
    &ff_hwcontext_type_cuda,
#endif
#if CONFIG_D3D11VA
    &ff_hwcontext_type_d3d11va,
#endif
#if CONFIG_LIBDRM
    &ff_hwcontext_type_drm,
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
#if CONFIG_VIDEOTOOLBOX
    &ff_hwcontext_type_videotoolbox,
#endif
    NULL,
};

static const char *const hw_type_names[] = {
    [AV_HWDEVICE_TYPE_CUDA]   = "cuda",
    [AV_HWDEVICE_TYPE_DRM]    = "drm",
    [AV_HWDEVICE_TYPE_DXVA2]  = "dxva2",
    [AV_HWDEVICE_TYPE_D3D11VA] = "d3d11va",
    [AV_HWDEVICE_TYPE_QSV]    = "qsv",
    [AV_HWDEVICE_TYPE_VAAPI]  = "vaapi",
    [AV_HWDEVICE_TYPE_VDPAU]  = "vdpau",
    [AV_HWDEVICE_TYPE_VIDEOTOOLBOX] = "videotoolbox",
};

enum AVHWDeviceType av_hwdevice_find_type_by_name(const char *name)
{
    int type;
    for (type = 0; type < FF_ARRAY_ELEMS(hw_type_names); type++) {
        if (hw_type_names[type] && !strcmp(hw_type_names[type], name))
            return type;
    }
    return AV_HWDEVICE_TYPE_NONE;
}

const char *av_hwdevice_get_type_name(enum AVHWDeviceType type)
{
    if (type > AV_HWDEVICE_TYPE_NONE &&
        type < FF_ARRAY_ELEMS(hw_type_names))
        return hw_type_names[type];
    else
        return NULL;
}

enum AVHWDeviceType av_hwdevice_iterate_types(enum AVHWDeviceType prev)
{
    enum AVHWDeviceType next;
    int i, set = 0;
    for (i = 0; hw_table[i]; i++) {
        if (prev != AV_HWDEVICE_TYPE_NONE && hw_table[i]->type <= prev)
            continue;
        if (!set || hw_table[i]->type < next) {
            next = hw_table[i]->type;
            set = 1;
        }
    }
    return set ? next : AV_HWDEVICE_TYPE_NONE;
}

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

    av_buffer_unref(&ctx->internal->source_device);

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

    if (ctx->internal->source_frames) {
        av_buffer_unref(&ctx->internal->source_frames);

    } else {
        if (ctx->internal->pool_internal)
            av_buffer_pool_uninit(&ctx->internal->pool_internal);

        if (ctx->internal->hw_type->frames_uninit)
            ctx->internal->hw_type->frames_uninit(ctx);

        if (ctx->free)
            ctx->free(ctx);
    }

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

    if (ctx->internal->source_frames) {
        /* A derived frame context is already initialised. */
        return 0;
    }

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

    if (ctx->internal->source_frames) {
        // This is a derived frame context, so we allocate in the source
        // and map the frame immediately.
        AVFrame *src_frame;

        frame->format = ctx->format;
        frame->hw_frames_ctx = av_buffer_ref(hwframe_ref);
        if (!frame->hw_frames_ctx)
            return AVERROR(ENOMEM);

        src_frame = av_frame_alloc();
        if (!src_frame)
            return AVERROR(ENOMEM);

        ret = av_hwframe_get_buffer(ctx->internal->source_frames,
                                    src_frame, 0);
        if (ret < 0)
            return ret;

        ret = av_hwframe_map(frame, src_frame,
                             ctx->internal->source_allocation_map_flags);
        if (ret) {
            av_log(ctx, AV_LOG_ERROR, "Failed to map frame into derived "
                   "frame context: %d.\n", ret);
            av_frame_free(&src_frame);
            return ret;
        }

        // Free the source frame immediately - the mapped frame still
        // contains a reference to it.
        av_frame_free(&src_frame);

        return 0;
    }

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

int av_hwdevice_ctx_create_derived(AVBufferRef **dst_ref_ptr,
                                   enum AVHWDeviceType type,
                                   AVBufferRef *src_ref, int flags)
{
    AVBufferRef *dst_ref = NULL, *tmp_ref;
    AVHWDeviceContext *dst_ctx, *tmp_ctx;
    int ret = 0;

    tmp_ref = src_ref;
    while (tmp_ref) {
        tmp_ctx = (AVHWDeviceContext*)tmp_ref->data;
        if (tmp_ctx->type == type) {
            dst_ref = av_buffer_ref(tmp_ref);
            if (!dst_ref) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            goto done;
        }
        tmp_ref = tmp_ctx->internal->source_device;
    }

    dst_ref = av_hwdevice_ctx_alloc(type);
    if (!dst_ref) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    dst_ctx = (AVHWDeviceContext*)dst_ref->data;

    tmp_ref = src_ref;
    while (tmp_ref) {
        tmp_ctx = (AVHWDeviceContext*)tmp_ref->data;
        if (dst_ctx->internal->hw_type->device_derive) {
            ret = dst_ctx->internal->hw_type->device_derive(dst_ctx,
                                                            tmp_ctx,
                                                            flags);
            if (ret == 0) {
                dst_ctx->internal->source_device = av_buffer_ref(src_ref);
                if (!dst_ctx->internal->source_device) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
                goto done;
            }
            if (ret != AVERROR(ENOSYS))
                goto fail;
        }
        tmp_ref = tmp_ctx->internal->source_device;
    }

    ret = AVERROR(ENOSYS);
    goto fail;

done:
    ret = av_hwdevice_ctx_init(dst_ref);
    if (ret < 0)
        goto fail;

    *dst_ref_ptr = dst_ref;
    return 0;

fail:
    av_buffer_unref(&dst_ref);
    *dst_ref_ptr = NULL;
    return ret;
}

static void ff_hwframe_unmap(void *opaque, uint8_t *data)
{
    HWMapDescriptor *hwmap = (HWMapDescriptor*)data;
    AVHWFramesContext *ctx = opaque;

    if (hwmap->unmap)
        hwmap->unmap(ctx, hwmap);

    av_frame_free(&hwmap->source);

    av_buffer_unref(&hwmap->hw_frames_ctx);

    av_free(hwmap);
}

int ff_hwframe_map_create(AVBufferRef *hwframe_ref,
                          AVFrame *dst, const AVFrame *src,
                          void (*unmap)(AVHWFramesContext *ctx,
                                        HWMapDescriptor *hwmap),
                          void *priv)
{
    AVHWFramesContext *ctx = (AVHWFramesContext*)hwframe_ref->data;
    HWMapDescriptor *hwmap;
    int ret;

    hwmap = av_mallocz(sizeof(*hwmap));
    if (!hwmap) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    hwmap->source = av_frame_alloc();
    if (!hwmap->source) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    ret = av_frame_ref(hwmap->source, src);
    if (ret < 0)
        goto fail;

    hwmap->hw_frames_ctx = av_buffer_ref(hwframe_ref);
    if (!hwmap->hw_frames_ctx) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    hwmap->unmap = unmap;
    hwmap->priv  = priv;

    dst->buf[0] = av_buffer_create((uint8_t*)hwmap, sizeof(*hwmap),
                                   &ff_hwframe_unmap, ctx, 0);
    if (!dst->buf[0]) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    return 0;

fail:
    if (hwmap) {
        av_buffer_unref(&hwmap->hw_frames_ctx);
        av_frame_free(&hwmap->source);
    }
    av_free(hwmap);
    return ret;
}

int av_hwframe_map(AVFrame *dst, const AVFrame *src, int flags)
{
    AVHWFramesContext *src_frames, *dst_frames;
    HWMapDescriptor *hwmap;
    int ret;

    if (src->hw_frames_ctx && dst->hw_frames_ctx) {
        src_frames = (AVHWFramesContext*)src->hw_frames_ctx->data;
        dst_frames = (AVHWFramesContext*)dst->hw_frames_ctx->data;

        if ((src_frames == dst_frames &&
             src->format == dst_frames->sw_format &&
             dst->format == dst_frames->format) ||
            (src_frames->internal->source_frames &&
             src_frames->internal->source_frames->data ==
             (uint8_t*)dst_frames)) {
            // This is an unmap operation.  We don't need to directly
            // do anything here other than fill in the original frame,
            // because the real unmap will be invoked when the last
            // reference to the mapped frame disappears.
            if (!src->buf[0]) {
                av_log(src_frames, AV_LOG_ERROR, "Invalid mapping "
                       "found when attempting unmap.\n");
                return AVERROR(EINVAL);
            }
            hwmap = (HWMapDescriptor*)src->buf[0]->data;
            av_frame_unref(dst);
            return av_frame_ref(dst, hwmap->source);
        }
    }

    if (src->hw_frames_ctx) {
        src_frames = (AVHWFramesContext*)src->hw_frames_ctx->data;

        if (src_frames->format == src->format &&
            src_frames->internal->hw_type->map_from) {
            ret = src_frames->internal->hw_type->map_from(src_frames,
                                                          dst, src, flags);
            if (ret != AVERROR(ENOSYS))
                return ret;
        }
    }

    if (dst->hw_frames_ctx) {
        dst_frames = (AVHWFramesContext*)dst->hw_frames_ctx->data;

        if (dst_frames->format == dst->format &&
            dst_frames->internal->hw_type->map_to) {
            ret = dst_frames->internal->hw_type->map_to(dst_frames,
                                                        dst, src, flags);
            if (ret != AVERROR(ENOSYS))
                return ret;
        }
    }

    return AVERROR(ENOSYS);
}

int av_hwframe_ctx_create_derived(AVBufferRef **derived_frame_ctx,
                                  enum AVPixelFormat format,
                                  AVBufferRef *derived_device_ctx,
                                  AVBufferRef *source_frame_ctx,
                                  int flags)
{
    AVBufferRef   *dst_ref = NULL;
    AVHWFramesContext *dst = NULL;
    AVHWFramesContext *src = (AVHWFramesContext*)source_frame_ctx->data;
    int ret;

    if (src->internal->source_frames) {
        AVHWFramesContext *src_src =
            (AVHWFramesContext*)src->internal->source_frames->data;
        AVHWDeviceContext *dst_dev =
            (AVHWDeviceContext*)derived_device_ctx->data;

        if (src_src->device_ctx == dst_dev) {
            // This is actually an unmapping, so we just return a
            // reference to the source frame context.
            *derived_frame_ctx =
                av_buffer_ref(src->internal->source_frames);
            if (!*derived_frame_ctx) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            return 0;
        }
    }

    dst_ref = av_hwframe_ctx_alloc(derived_device_ctx);
    if (!dst_ref) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    dst = (AVHWFramesContext*)dst_ref->data;

    dst->format    = format;
    dst->sw_format = src->sw_format;
    dst->width     = src->width;
    dst->height    = src->height;

    dst->internal->source_frames = av_buffer_ref(source_frame_ctx);
    if (!dst->internal->source_frames) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    dst->internal->source_allocation_map_flags =
        flags & (AV_HWFRAME_MAP_READ      |
                 AV_HWFRAME_MAP_WRITE     |
                 AV_HWFRAME_MAP_OVERWRITE |
                 AV_HWFRAME_MAP_DIRECT);

    ret = AVERROR(ENOSYS);
    if (src->internal->hw_type->frames_derive_from)
        ret = src->internal->hw_type->frames_derive_from(dst, src, flags);
    if (ret == AVERROR(ENOSYS) &&
        dst->internal->hw_type->frames_derive_to)
        ret = dst->internal->hw_type->frames_derive_to(dst, src, flags);
    if (ret == AVERROR(ENOSYS))
        ret = 0;
    if (ret)
        goto fail;

    *derived_frame_ctx = dst_ref;
    return 0;

fail:
    if (dst)
        av_buffer_unref(&dst->internal->source_frames);
    av_buffer_unref(&dst_ref);
    return ret;
}
