/*
 * Copyright (C) 2018 Philip Langdale <philipl@overt.org>
 *               2020 Aman Karmani <aman@tmm1.net>
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

#include "filters.h"
#include "metal/utils.h"
#include "yadif.h"
#include "libavutil/avassert.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_videotoolbox.h"
#include "libavutil/objc.h"

#include <assert.h>

extern char ff_vf_yadif_videotoolbox_metallib_data[];
extern unsigned int ff_vf_yadif_videotoolbox_metallib_len;

typedef struct API_AVAILABLE(macos(10.11), ios(8.0)) YADIFVTContext {
    YADIFContext yadif;

    AVBufferRef       *device_ref;
    AVBufferRef       *input_frames_ref;
    AVHWFramesContext *input_frames;

    id<MTLDevice> mtlDevice;
    id<MTLLibrary> mtlLibrary;
    id<MTLCommandQueue> mtlQueue;
    id<MTLComputePipelineState> mtlPipeline;
    id<MTLFunction> mtlFunction;
    id<MTLBuffer> mtlParamsBuffer;

    CVMetalTextureCacheRef textureCache;
} YADIFVTContext API_AVAILABLE(macos(10.11), ios(8.0));

// Using sizeof(YADIFVTContext) outside of an availability check will error
// if we're targeting an older OS version, so we need to calculate the size ourselves
// (we'll statically verify it's correct in yadif_videotoolbox_init behind a check)
#define YADIF_VT_CTX_SIZE (sizeof(YADIFContext) + sizeof(void*) * 10)

struct mtlYadifParams {
    uint channels;
    uint parity;
    uint tff;
    bool is_second_field;
    bool skip_spatial_check;
    int field_mode;
};

static void call_kernel(AVFilterContext *ctx,
                        id<MTLTexture> dst,
                        id<MTLTexture> prev,
                        id<MTLTexture> cur,
                        id<MTLTexture> next,
                        int channels,
                        int parity,
                        int tff) API_AVAILABLE(macos(10.11), ios(8.0))
{
    YADIFVTContext *s = ctx->priv;
    id<MTLCommandBuffer> buffer = s->mtlQueue.commandBuffer;
    id<MTLComputeCommandEncoder> encoder = buffer.computeCommandEncoder;
    struct mtlYadifParams *params = (struct mtlYadifParams *)s->mtlParamsBuffer.contents;
    *params = (struct mtlYadifParams){
        .channels = channels,
        .parity = parity,
        .tff = tff,
        .is_second_field = !(parity ^ tff),
        .skip_spatial_check = s->yadif.mode&2,
        .field_mode = s->yadif.current_field
    };

    [encoder setTexture:dst  atIndex:0];
    [encoder setTexture:prev atIndex:1];
    [encoder setTexture:cur  atIndex:2];
    [encoder setTexture:next atIndex:3];
    [encoder setBuffer:s->mtlParamsBuffer offset:0 atIndex:4];
    ff_metal_compute_encoder_dispatch(s->mtlDevice, s->mtlPipeline, encoder, dst.width, dst.height);
    [encoder endEncoding];

    [buffer commit];
    [buffer waitUntilCompleted];

    ff_objc_release(&encoder);
    ff_objc_release(&buffer);
}

static void filter(AVFilterContext *ctx, AVFrame *dst,
                   int parity, int tff) API_AVAILABLE(macos(10.11), ios(8.0))
{
    YADIFVTContext *s = ctx->priv;
    YADIFContext *y = &s->yadif;
    int i;

    for (i = 0; i < y->csp->nb_components; i++) {
        int pixel_size, channels;
        const AVComponentDescriptor *comp = &y->csp->comp[i];
        CVMetalTextureRef prev, cur, next, dest;
        id<MTLTexture> tex_prev, tex_cur, tex_next, tex_dest;
        MTLPixelFormat format;

        if (comp->plane < i) {
            // We process planes as a whole, so don't reprocess
            // them for additional components
            continue;
        }

        pixel_size = (comp->depth + comp->shift) / 8;
        channels = comp->step / pixel_size;
        if (pixel_size > 2 || channels > 2) {
            av_log(ctx, AV_LOG_ERROR, "Unsupported pixel format: %s\n", y->csp->name);
            goto exit;
        }
        switch (pixel_size) {
        case 1:
            format = channels == 1 ? MTLPixelFormatR8Unorm : MTLPixelFormatRG8Unorm;
            break;
        case 2:
            format = channels == 1 ? MTLPixelFormatR16Unorm : MTLPixelFormatRG16Unorm;
            break;
        default:
            av_log(ctx, AV_LOG_ERROR, "Unsupported pixel format: %s\n", y->csp->name);
            goto exit;
        }
        av_log(ctx, AV_LOG_TRACE,
               "Deinterlacing plane %d: pixel_size: %d channels: %d\n",
               comp->plane, pixel_size, channels);

        prev = ff_metal_texture_from_pixbuf(ctx, s->textureCache, (CVPixelBufferRef)y->prev->data[3], i, format);
        cur  = ff_metal_texture_from_pixbuf(ctx, s->textureCache, (CVPixelBufferRef)y->cur->data[3], i, format);
        next = ff_metal_texture_from_pixbuf(ctx, s->textureCache, (CVPixelBufferRef)y->next->data[3], i, format);
        dest = ff_metal_texture_from_pixbuf(ctx, s->textureCache, (CVPixelBufferRef)dst->data[3], i, format);

        tex_prev = CVMetalTextureGetTexture(prev);
        tex_cur  = CVMetalTextureGetTexture(cur);
        tex_next = CVMetalTextureGetTexture(next);
        tex_dest = CVMetalTextureGetTexture(dest);

        call_kernel(ctx, tex_dest, tex_prev, tex_cur, tex_next,
                         channels, parity, tff);

        CFRelease(prev);
        CFRelease(cur);
        CFRelease(next);
        CFRelease(dest);
    }

    CVBufferPropagateAttachments((CVPixelBufferRef)y->cur->data[3], (CVPixelBufferRef)dst->data[3]);

    if (y->current_field == YADIF_FIELD_END) {
        y->current_field = YADIF_FIELD_NORMAL;
    }

exit:
    return;
}

static av_cold void do_uninit(AVFilterContext *ctx) API_AVAILABLE(macos(10.11), ios(8.0))
{
    YADIFVTContext *s = ctx->priv;

    ff_yadif_uninit(ctx);

    av_buffer_unref(&s->device_ref);
    av_buffer_unref(&s->input_frames_ref);
    s->input_frames = NULL;

    ff_objc_release(&s->mtlParamsBuffer);
    ff_objc_release(&s->mtlFunction);
    ff_objc_release(&s->mtlPipeline);
    ff_objc_release(&s->mtlQueue);
    ff_objc_release(&s->mtlLibrary);
    ff_objc_release(&s->mtlDevice);

    if (s->textureCache) {
        CFRelease(s->textureCache);
        s->textureCache = NULL;
    }
}


static av_cold void yadif_videotoolbox_uninit(AVFilterContext *ctx)
{
    if (@available(macOS 10.11, iOS 8.0, *)) {
        do_uninit(ctx);
    }
}

static av_cold int do_init(AVFilterContext *ctx) API_AVAILABLE(macos(10.11), ios(8.0))
{
    YADIFVTContext *s = ctx->priv;
    NSError *err = nil;
    CVReturn ret;

    s->mtlDevice = MTLCreateSystemDefaultDevice();
    if (!s->mtlDevice) {
        av_log(ctx, AV_LOG_ERROR, "Unable to find Metal device\n");
        goto fail;
    }

    av_log(ctx, AV_LOG_INFO, "Using Metal device: %s\n", s->mtlDevice.name.UTF8String);

    dispatch_data_t libData = dispatch_data_create(
        ff_vf_yadif_videotoolbox_metallib_data,
        ff_vf_yadif_videotoolbox_metallib_len,
        nil,
        nil);
    s->mtlLibrary = [s->mtlDevice newLibraryWithData:libData error:&err];
    dispatch_release(libData);
    libData = nil;
    if (err) {
        av_log(ctx, AV_LOG_ERROR, "Failed to load Metal library: %s\n", err.description.UTF8String);
        goto fail;
    }

    s->mtlFunction = [s->mtlLibrary newFunctionWithName:@"deint"];
    if (!s->mtlFunction) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create Metal function!\n");
        goto fail;
    }

    s->mtlQueue = s->mtlDevice.newCommandQueue;
    if (!s->mtlQueue) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create Metal command queue!\n");
        goto fail;
    }

    s->mtlPipeline = [s->mtlDevice
        newComputePipelineStateWithFunction:s->mtlFunction
        error:&err];
    if (err) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create Metal compute pipeline: %s\n", err.description.UTF8String);
        goto fail;
    }

    s->mtlParamsBuffer = [s->mtlDevice
        newBufferWithLength:sizeof(struct mtlYadifParams)
        options:MTLResourceStorageModeShared];
    if (!s->mtlParamsBuffer) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create Metal buffer for parameters\n");
        goto fail;
    }

    ret = CVMetalTextureCacheCreate(
        NULL,
        NULL,
        s->mtlDevice,
        NULL,
        &s->textureCache
    );
    if (ret != kCVReturnSuccess) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create CVMetalTextureCache: %d\n", ret);
        goto fail;
    }

    return 0;
fail:
    yadif_videotoolbox_uninit(ctx);
    return AVERROR_EXTERNAL;
}

static av_cold int yadif_videotoolbox_init(AVFilterContext *ctx)
{
    if (@available(macOS 10.11, iOS 8.0, *)) {
        // Ensure we calculated YADIF_VT_CTX_SIZE correctly
        static_assert(YADIF_VT_CTX_SIZE == sizeof(YADIFVTContext), "Incorrect YADIF_VT_CTX_SIZE value!");
        return do_init(ctx);
    } else {
        av_log(ctx, AV_LOG_ERROR, "Metal is not available on this OS version\n");
        return AVERROR(ENOSYS);
    }
}

static int do_config_input(AVFilterLink *inlink) API_AVAILABLE(macos(10.11), ios(8.0))
{
    FilterLink *l = ff_filter_link(inlink);
    AVFilterContext *ctx = inlink->dst;
    YADIFVTContext *s = ctx->priv;

    if (!l->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "A hardware frames reference is "
               "required to associate the processing device.\n");
        return AVERROR(EINVAL);
    }

    s->input_frames_ref = av_buffer_ref(l->hw_frames_ctx);
    if (!s->input_frames_ref) {
        av_log(ctx, AV_LOG_ERROR, "A input frames reference create "
               "failed.\n");
        return AVERROR(ENOMEM);
    }
    s->input_frames = (AVHWFramesContext*)s->input_frames_ref->data;

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    if (@available(macOS 10.11, iOS 8.0, *)) {
        return do_config_input(inlink);
    } else {
        av_log(ctx, AV_LOG_ERROR, "Metal is not available on this OS version\n");
        return AVERROR(ENOSYS);
    }
}

static int do_config_output(AVFilterLink *link) API_AVAILABLE(macos(10.11), ios(8.0))
{
    FilterLink *l = ff_filter_link(link);
    FilterLink *il = ff_filter_link(link->src->inputs[0]);
    AVHWFramesContext *output_frames, *input_frames;
    AVFilterContext *ctx = link->src;
    YADIFVTContext *s = ctx->priv;
    YADIFContext *y = &s->yadif;
    int ret = 0;

    av_assert0(s->input_frames);
    s->device_ref = av_buffer_ref(s->input_frames->device_ref);
    if (!s->device_ref) {
        av_log(ctx, AV_LOG_ERROR, "A device reference create "
               "failed.\n");
        return AVERROR(ENOMEM);
    }

    l->hw_frames_ctx = av_hwframe_ctx_alloc(s->device_ref);
    if (!l->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create HW frame context "
               "for output.\n");
        ret = AVERROR(ENOMEM);
        goto exit;
    }

    input_frames = (AVHWFramesContext*)il->hw_frames_ctx->data;
    output_frames = (AVHWFramesContext*)l->hw_frames_ctx->data;

    output_frames->format    = AV_PIX_FMT_VIDEOTOOLBOX;
    output_frames->sw_format = s->input_frames->sw_format;
    output_frames->width     = ctx->inputs[0]->w;
    output_frames->height    = ctx->inputs[0]->h;
    ((AVVTFramesContext *)output_frames->hwctx)->color_range = ((AVVTFramesContext *)input_frames->hwctx)->color_range;

    ret = ff_filter_init_hw_frames(ctx, link, 10);
    if (ret < 0)
        goto exit;

    ret = av_hwframe_ctx_init(l->hw_frames_ctx);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to initialise VideoToolbox frame "
               "context for output: %d\n", ret);
        goto exit;
    }

    ret = ff_yadif_config_output_common(link);
    if (ret < 0)
        goto exit;

    y->csp = av_pix_fmt_desc_get(output_frames->sw_format);
    y->filter = filter;

exit:
    return ret;
}

static int config_output(AVFilterLink *link)
{
    AVFilterContext *ctx = link->src;
    if (@available(macOS 10.11, iOS 8.0, *)) {
        return do_config_output(link);
    } else {
        av_log(ctx, AV_LOG_ERROR, "Metal is not available on this OS version\n");
        return AVERROR(ENOSYS);
    }
}

#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define CONST(name, help, val, unit) { name, help, 0, AV_OPT_TYPE_CONST, {.i64=val}, INT_MIN, INT_MAX, FLAGS, unit }

static const AVOption yadif_videotoolbox_options[] = {
    #define OFFSET(x) offsetof(YADIFContext, x)
    { "mode",   "specify the interlacing mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=YADIF_MODE_SEND_FRAME}, 0, 3, FLAGS, .unit = "mode"},
    CONST("send_frame",           "send one frame for each frame",                                     YADIF_MODE_SEND_FRAME,           "mode"),
    CONST("send_field",           "send one frame for each field",                                     YADIF_MODE_SEND_FIELD,           "mode"),
    CONST("send_frame_nospatial", "send one frame for each frame, but skip spatial interlacing check", YADIF_MODE_SEND_FRAME_NOSPATIAL, "mode"),
    CONST("send_field_nospatial", "send one frame for each field, but skip spatial interlacing check", YADIF_MODE_SEND_FIELD_NOSPATIAL, "mode"),

    { "parity", "specify the assumed picture field parity", OFFSET(parity), AV_OPT_TYPE_INT, {.i64=YADIF_PARITY_AUTO}, -1, 1, FLAGS, .unit = "parity" },
    CONST("tff",  "assume top field first",    YADIF_PARITY_TFF,  "parity"),
    CONST("bff",  "assume bottom field first", YADIF_PARITY_BFF,  "parity"),
    CONST("auto", "auto detect parity",        YADIF_PARITY_AUTO, "parity"),

    { "deint", "specify which frames to deinterlace", OFFSET(deint), AV_OPT_TYPE_INT, {.i64=YADIF_DEINT_ALL}, 0, 1, FLAGS, .unit = "deint" },
    CONST("all",        "deinterlace all frames",                       YADIF_DEINT_ALL,        "deint"),
    CONST("interlaced", "only deinterlace frames marked as interlaced", YADIF_DEINT_INTERLACED, "deint"),
    #undef OFFSET

    { NULL }
};

AVFILTER_DEFINE_CLASS(yadif_videotoolbox);

static const AVFilterPad yadif_videotoolbox_inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .filter_frame  = ff_yadif_filter_frame,
        .config_props  = config_input,
    },
};

static const AVFilterPad yadif_videotoolbox_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = ff_yadif_request_frame,
        .config_props  = config_output,
    },
};

const AVFilter ff_vf_yadif_videotoolbox = {
    .name           = "yadif_videotoolbox",
    .description    = NULL_IF_CONFIG_SMALL("YADIF for VideoToolbox frames using Metal compute"),
    .priv_size      = YADIF_VT_CTX_SIZE,
    .priv_class     = &yadif_videotoolbox_class,
    .init           = yadif_videotoolbox_init,
    .uninit         = yadif_videotoolbox_uninit,
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VIDEOTOOLBOX),
    FILTER_INPUTS(yadif_videotoolbox_inputs),
    FILTER_OUTPUTS(yadif_videotoolbox_outputs),
    .flags          = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
