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

#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/pixdesc.h"
#include "formats.h"
#include "internal.h"
#include "vaapi_vpp.h"

int ff_vaapi_vpp_query_formats(AVFilterContext *avctx)
{
    enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_VAAPI, AV_PIX_FMT_NONE,
    };
    int err;

    if ((err = ff_formats_ref(ff_make_format_list(pix_fmts),
                              &avctx->inputs[0]->out_formats)) < 0)
        return err;
    if ((err = ff_formats_ref(ff_make_format_list(pix_fmts),
                              &avctx->outputs[0]->in_formats)) < 0)
        return err;

    return 0;
}

void ff_vaapi_vpp_pipeline_uninit(AVFilterContext *avctx)
{
    VAAPIVPPContext *ctx   = avctx->priv;
    int i;
    for (i = 0; i < ctx->nb_filter_buffers; i++) {
        if (ctx->filter_buffers[i] != VA_INVALID_ID) {
            vaDestroyBuffer(ctx->hwctx->display, ctx->filter_buffers[i]);
            ctx->filter_buffers[i] = VA_INVALID_ID;
        }
    }
    ctx->nb_filter_buffers = 0;

    if (ctx->va_context != VA_INVALID_ID) {
        vaDestroyContext(ctx->hwctx->display, ctx->va_context);
        ctx->va_context = VA_INVALID_ID;
    }

    if (ctx->va_config != VA_INVALID_ID) {
        vaDestroyConfig(ctx->hwctx->display, ctx->va_config);
        ctx->va_config = VA_INVALID_ID;
    }

    av_buffer_unref(&ctx->device_ref);
    ctx->hwctx = NULL;
}

int ff_vaapi_vpp_config_input(AVFilterLink *inlink)
{
    AVFilterContext *avctx = inlink->dst;
    VAAPIVPPContext *ctx   = avctx->priv;

    if (ctx->pipeline_uninit)
        ctx->pipeline_uninit(avctx);

    if (!inlink->hw_frames_ctx) {
        av_log(avctx, AV_LOG_ERROR, "A hardware frames reference is "
               "required to associate the processing device.\n");
        return AVERROR(EINVAL);
    }

    ctx->input_frames_ref = av_buffer_ref(inlink->hw_frames_ctx);
    if (!ctx->input_frames_ref) {
        av_log(avctx, AV_LOG_ERROR, "A input frames reference create "
               "failed.\n");
        return AVERROR(ENOMEM);
    }
    ctx->input_frames = (AVHWFramesContext*)ctx->input_frames_ref->data;

    return 0;
}

int ff_vaapi_vpp_config_output(AVFilterLink *outlink)
{
    AVFilterContext *avctx = outlink->src;
    VAAPIVPPContext *ctx   = avctx->priv;
    AVVAAPIHWConfig *hwconfig = NULL;
    AVHWFramesConstraints *constraints = NULL;
    AVHWFramesContext *output_frames;
    AVVAAPIFramesContext *va_frames;
    VAStatus vas;
    int err, i;

    if (ctx->pipeline_uninit)
        ctx->pipeline_uninit(avctx);

    if (!ctx->output_width)
        ctx->output_width  = avctx->inputs[0]->w;
    if (!ctx->output_height)
        ctx->output_height = avctx->inputs[0]->h;

    av_assert0(ctx->input_frames);
    ctx->device_ref = av_buffer_ref(ctx->input_frames->device_ref);
    if (!ctx->device_ref) {
        av_log(avctx, AV_LOG_ERROR, "A device reference create "
               "failed.\n");
        return AVERROR(ENOMEM);
    }
    ctx->hwctx = ((AVHWDeviceContext*)ctx->device_ref->data)->hwctx;

    av_assert0(ctx->va_config == VA_INVALID_ID);
    vas = vaCreateConfig(ctx->hwctx->display, VAProfileNone,
                         VAEntrypointVideoProc, NULL, 0, &ctx->va_config);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create processing pipeline "
               "config: %d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        goto fail;
    }

    hwconfig = av_hwdevice_hwconfig_alloc(ctx->device_ref);
    if (!hwconfig) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    hwconfig->config_id = ctx->va_config;

    constraints = av_hwdevice_get_hwframe_constraints(ctx->device_ref,
                                                      hwconfig);
    if (!constraints) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    if (ctx->output_format == AV_PIX_FMT_NONE)
        ctx->output_format = ctx->input_frames->sw_format;
    if (constraints->valid_sw_formats) {
        for (i = 0; constraints->valid_sw_formats[i] != AV_PIX_FMT_NONE; i++) {
            if (ctx->output_format == constraints->valid_sw_formats[i])
                break;
        }
        if (constraints->valid_sw_formats[i] == AV_PIX_FMT_NONE) {
            av_log(avctx, AV_LOG_ERROR, "Hardware does not support output "
                   "format %s.\n", av_get_pix_fmt_name(ctx->output_format));
            err = AVERROR(EINVAL);
            goto fail;
        }
    }

    if (ctx->output_width  < constraints->min_width  ||
        ctx->output_height < constraints->min_height ||
        ctx->output_width  > constraints->max_width  ||
        ctx->output_height > constraints->max_height) {
        av_log(avctx, AV_LOG_ERROR, "Hardware does not support scaling to "
               "size %dx%d (constraints: width %d-%d height %d-%d).\n",
               ctx->output_width, ctx->output_height,
               constraints->min_width,  constraints->max_width,
               constraints->min_height, constraints->max_height);
        err = AVERROR(EINVAL);
        goto fail;
    }

    outlink->hw_frames_ctx = av_hwframe_ctx_alloc(ctx->device_ref);
    if (!outlink->hw_frames_ctx) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create HW frame context "
               "for output.\n");
        err = AVERROR(ENOMEM);
        goto fail;
    }

    output_frames = (AVHWFramesContext*)outlink->hw_frames_ctx->data;

    output_frames->format    = AV_PIX_FMT_VAAPI;
    output_frames->sw_format = ctx->output_format;
    output_frames->width     = ctx->output_width;
    output_frames->height    = ctx->output_height;

    output_frames->initial_pool_size = 4;

    err = ff_filter_init_hw_frames(avctx, outlink, 10);
    if (err < 0)
        goto fail;

    err = av_hwframe_ctx_init(outlink->hw_frames_ctx);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to initialise VAAPI frame "
               "context for output: %d\n", err);
        goto fail;
    }

    va_frames = output_frames->hwctx;

    av_assert0(ctx->va_context == VA_INVALID_ID);
    vas = vaCreateContext(ctx->hwctx->display, ctx->va_config,
                          ctx->output_width, ctx->output_height,
                          VA_PROGRESSIVE,
                          va_frames->surface_ids, va_frames->nb_surfaces,
                          &ctx->va_context);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create processing pipeline "
               "context: %d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR(EIO);
    }

    outlink->w = ctx->output_width;
    outlink->h = ctx->output_height;

    if (ctx->build_filter_params) {
        err = ctx->build_filter_params(avctx);
        if (err < 0)
            goto fail;
    }

    av_freep(&hwconfig);
    av_hwframe_constraints_free(&constraints);
    return 0;

fail:
    av_buffer_unref(&outlink->hw_frames_ctx);
    av_freep(&hwconfig);
    av_hwframe_constraints_free(&constraints);
    return err;
}

int ff_vaapi_vpp_colour_standard(enum AVColorSpace av_cs)
{
    switch(av_cs) {
#define CS(av, va) case AVCOL_SPC_ ## av: return VAProcColorStandard ## va;
        CS(BT709,     BT709);
        CS(BT470BG,   BT601);
        CS(SMPTE170M, SMPTE170M);
        CS(SMPTE240M, SMPTE240M);
#undef CS
    default:
        return VAProcColorStandardNone;
    }
}

int ff_vaapi_vpp_make_param_buffers(AVFilterContext *avctx,
                                    int type,
                                    const void *data,
                                    size_t size,
                                    int count)
{
    VAStatus vas;
    VABufferID buffer;
    VAAPIVPPContext *ctx   = avctx->priv;

    av_assert0(ctx->nb_filter_buffers + 1 <= VAProcFilterCount);

    vas = vaCreateBuffer(ctx->hwctx->display, ctx->va_context,
                         type, size, count, (void*)data, &buffer);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create parameter "
               "buffer (type %d): %d (%s).\n",
               type, vas, vaErrorStr(vas));
        return AVERROR(EIO);
    }

    ctx->filter_buffers[ctx->nb_filter_buffers++] = buffer;

    av_log(avctx, AV_LOG_DEBUG, "Param buffer (type %d, %zu bytes, count %d) "
           "is %#x.\n", type, size, count, buffer);
    return 0;
}


int ff_vaapi_vpp_render_picture(AVFilterContext *avctx,
                                VAProcPipelineParameterBuffer *params,
                                VASurfaceID output_surface)
{
    VABufferID params_id;
    VAStatus vas;
    int err = 0;
    VAAPIVPPContext *ctx   = avctx->priv;

    vas = vaBeginPicture(ctx->hwctx->display,
                         ctx->va_context, output_surface);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to attach new picture: "
               "%d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        goto fail;
    }

    vas = vaCreateBuffer(ctx->hwctx->display, ctx->va_context,
                         VAProcPipelineParameterBufferType,
                         sizeof(*params), 1, params, &params_id);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create parameter buffer: "
               "%d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        goto fail_after_begin;
    }
    av_log(avctx, AV_LOG_DEBUG, "Pipeline parameter buffer is %#x.\n",
           params_id);

    vas = vaRenderPicture(ctx->hwctx->display, ctx->va_context,
                          &params_id, 1);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to render parameter buffer: "
               "%d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        goto fail_after_begin;
    }

    vas = vaEndPicture(ctx->hwctx->display, ctx->va_context);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to start picture processing: "
               "%d (%s).\n", vas, vaErrorStr(vas));
        err = AVERROR(EIO);
        goto fail_after_render;
    }

    if (CONFIG_VAAPI_1 || ctx->hwctx->driver_quirks &
        AV_VAAPI_DRIVER_QUIRK_RENDER_PARAM_BUFFERS) {
        vas = vaDestroyBuffer(ctx->hwctx->display, params_id);
        if (vas != VA_STATUS_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "Failed to free parameter buffer: "
                   "%d (%s).\n", vas, vaErrorStr(vas));
            // And ignore.
        }
    }

    return 0;

    // We want to make sure that if vaBeginPicture has been called, we also
    // call vaRenderPicture and vaEndPicture.  These calls may well fail or
    // do something else nasty, but once we're in this failure case there
    // isn't much else we can do.
fail_after_begin:
    vaRenderPicture(ctx->hwctx->display, ctx->va_context, &params_id, 1);
fail_after_render:
    vaEndPicture(ctx->hwctx->display, ctx->va_context);
fail:
    return err;
}

void ff_vaapi_vpp_ctx_init(AVFilterContext *avctx)
{
    int i;
    VAAPIVPPContext *ctx   = avctx->priv;

    ctx->va_config  = VA_INVALID_ID;
    ctx->va_context = VA_INVALID_ID;
    ctx->valid_ids  = 1;

    for (i = 0; i < VAProcFilterCount; i++)
        ctx->filter_buffers[i] = VA_INVALID_ID;
    ctx->nb_filter_buffers = 0;
}

void ff_vaapi_vpp_ctx_uninit(AVFilterContext *avctx)
{
    VAAPIVPPContext *ctx   = avctx->priv;
    if (ctx->valid_ids && ctx->pipeline_uninit)
        ctx->pipeline_uninit(avctx);

    av_buffer_unref(&ctx->input_frames_ref);
    av_buffer_unref(&ctx->device_ref);
}
