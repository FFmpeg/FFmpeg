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

#include "vf_amf_common.h"

#include "libavutil/avassert.h"
#include "avfilter.h"
#include "avfilter_internal.h"
#include "formats.h"
#include "libavutil/mem.h"
#include "libavutil/imgutils.h"

#include "libavutil/hwcontext_amf.h"
#include "libavutil/hwcontext_amf_internal.h"
#include "AMF/components/ColorSpace.h"
#include "scale_eval.h"

#if CONFIG_DXVA2
#include <d3d9.h>
#endif

#if CONFIG_D3D11VA
#include <d3d11.h>
#endif

int amf_filter_init(AVFilterContext *avctx)
{
    AMFFilterContext     *ctx = avctx->priv;

    if (!strcmp(ctx->format_str, "same")) {
        ctx->format = AV_PIX_FMT_NONE;
    } else {
        ctx->format = av_get_pix_fmt(ctx->format_str);
        if (ctx->format == AV_PIX_FMT_NONE) {
            av_log(avctx, AV_LOG_ERROR, "Unrecognized pixel format: %s\n", ctx->format_str);
            return AVERROR(EINVAL);
        }
    }

    return 0;
}

void amf_filter_uninit(AVFilterContext *avctx)
{
    AMFFilterContext *ctx = avctx->priv;

    if (ctx->component) {
        ctx->component->pVtbl->Terminate(ctx->component);
        ctx->component->pVtbl->Release(ctx->component);
        ctx->component = NULL;
    }

    av_buffer_unref(&ctx->amf_device_ref);
    av_buffer_unref(&ctx->hwdevice_ref);
    av_buffer_unref(&ctx->hwframes_in_ref);
    av_buffer_unref(&ctx->hwframes_out_ref);
}

int amf_filter_filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext             *avctx = inlink->dst;
    AMFFilterContext             *ctx = avctx->priv;
    AVFilterLink                *outlink = avctx->outputs[0];
    AMF_RESULT  res;
    AMFSurface *surface_in;
    AMFSurface *surface_out;
    AMFData *data_out = NULL;
    enum AVColorSpace out_colorspace;
    enum AVColorRange out_color_range;

    AVFrame *out = NULL;
    int ret = 0;

    if (!ctx->component)
        return AVERROR(EINVAL);

    ret = amf_avframe_to_amfsurface(avctx, in, &surface_in);
    if (ret < 0)
        goto fail;

    res = ctx->component->pVtbl->SubmitInput(ctx->component, (AMFData*)surface_in);
    surface_in->pVtbl->Release(surface_in); // release surface after use
    AMF_GOTO_FAIL_IF_FALSE(avctx, res == AMF_OK, AVERROR_UNKNOWN, "SubmitInput() failed with error %d\n", res);
    res = ctx->component->pVtbl->QueryOutput(ctx->component, &data_out);
    AMF_GOTO_FAIL_IF_FALSE(avctx, res == AMF_OK, AVERROR_UNKNOWN, "QueryOutput() failed with error %d\n", res);

    if (data_out) {
        AMFGuid guid = IID_AMFSurface();
        data_out->pVtbl->QueryInterface(data_out, &guid, (void**)&surface_out); // query for buffer interface
        data_out->pVtbl->Release(data_out);
    }

    out = amf_amfsurface_to_avframe(avctx, surface_out);

    ret = av_frame_copy_props(out, in);
    av_frame_unref(in);

    out_colorspace = AVCOL_SPC_UNSPECIFIED;

    if (ctx->color_profile != AMF_VIDEO_CONVERTER_COLOR_PROFILE_UNKNOWN) {
        switch(ctx->color_profile) {
        case AMF_VIDEO_CONVERTER_COLOR_PROFILE_601:
            out_colorspace = AVCOL_SPC_SMPTE170M;
        break;
        case AMF_VIDEO_CONVERTER_COLOR_PROFILE_709:
            out_colorspace = AVCOL_SPC_BT709;
        break;
        case AMF_VIDEO_CONVERTER_COLOR_PROFILE_2020:
            out_colorspace = AVCOL_SPC_BT2020_NCL;
        break;
        case AMF_VIDEO_CONVERTER_COLOR_PROFILE_JPEG:
            out_colorspace = AVCOL_SPC_RGB;
        break;
        default:
            out_colorspace = AVCOL_SPC_UNSPECIFIED;
        break;
        }
        out->colorspace = out_colorspace;
    }

    out_color_range = AVCOL_RANGE_UNSPECIFIED;
    if (ctx->color_range == AMF_COLOR_RANGE_FULL)
        out_color_range = AVCOL_RANGE_JPEG;
    else if (ctx->color_range == AMF_COLOR_RANGE_STUDIO)
        out_color_range = AVCOL_RANGE_MPEG;

    if (ctx->color_range != AMF_COLOR_RANGE_UNDEFINED)
        out->color_range = out_color_range;

    if (ctx->primaries != AMF_COLOR_PRIMARIES_UNDEFINED)
        out->color_primaries = ctx->primaries;

    if (ctx->trc != AMF_COLOR_TRANSFER_CHARACTERISTIC_UNDEFINED)
        out->color_trc = ctx->trc;


    if (ret < 0)
        goto fail;

    out->hw_frames_ctx = av_buffer_ref(ctx->hwframes_out_ref);
    if (!out->hw_frames_ctx) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return ret;
}



int amf_setup_input_output_formats(AVFilterContext *avctx,
                                    const enum AVPixelFormat *input_pix_fmts,
                                    const enum AVPixelFormat *output_pix_fmts)
{
    int err;
    AVFilterFormats *input_formats;
    AVFilterFormats *output_formats;

    //in case if hw_device_ctx is set to DXVA2 we change order of pixel formats to set DXVA2 be choosen by default
    //The order is ignored if hw_frames_ctx is not NULL on the config_output stage
    if (avctx->hw_device_ctx) {
        AVHWDeviceContext *device_ctx = (AVHWDeviceContext*)avctx->hw_device_ctx->data;

        switch (device_ctx->type) {
    #if CONFIG_D3D11VA
        case AV_HWDEVICE_TYPE_D3D11VA:
            {
                static const enum AVPixelFormat output_pix_fmts_d3d11[] = {
                    AV_PIX_FMT_D3D11,
                    AV_PIX_FMT_NONE,
                };
                output_pix_fmts = output_pix_fmts_d3d11;
            }
            break;
    #endif
    #if CONFIG_DXVA2
        case AV_HWDEVICE_TYPE_DXVA2:
            {
                static const enum AVPixelFormat output_pix_fmts_dxva2[] = {
                    AV_PIX_FMT_DXVA2_VLD,
                    AV_PIX_FMT_NONE,
                };
                output_pix_fmts = output_pix_fmts_dxva2;
            }
            break;
    #endif
        case AV_HWDEVICE_TYPE_AMF:
            break;
        default:
            {
                av_log(avctx, AV_LOG_ERROR, "Unsupported device : %s\n", av_hwdevice_get_type_name(device_ctx->type));
                return AVERROR(EINVAL);
            }
            break;
        }
    }

    input_formats = ff_make_format_list(output_pix_fmts);
    if (!input_formats) {
        return AVERROR(ENOMEM);
    }
    output_formats = ff_make_format_list(output_pix_fmts);
    if (!output_formats) {
        return AVERROR(ENOMEM);
    }

    if ((err = ff_formats_ref(input_formats, &avctx->inputs[0]->outcfg.formats)) < 0)
        return err;

    if ((err = ff_formats_ref(output_formats, &avctx->outputs[0]->incfg.formats)) < 0)
        return err;
    return 0;
}

int amf_copy_surface(AVFilterContext *avctx, const AVFrame *frame,
    AMFSurface* surface)
{
    AMFPlane *plane;
    uint8_t  *dst_data[4];
    int       dst_linesize[4];
    int       planes;
    int       i;

    planes = (int)surface->pVtbl->GetPlanesCount(surface);
    av_assert0(planes < FF_ARRAY_ELEMS(dst_data));

    for (i = 0; i < planes; i++) {
        plane = surface->pVtbl->GetPlaneAt(surface, i);
        dst_data[i] = plane->pVtbl->GetNative(plane);
        dst_linesize[i] = plane->pVtbl->GetHPitch(plane);
    }
    av_image_copy(dst_data, dst_linesize,
        (const uint8_t**)frame->data, frame->linesize, frame->format,
        frame->width, frame->height);

    return 0;
}

int amf_init_filter_config(AVFilterLink *outlink, enum AVPixelFormat *in_format)
{
    int err;
    AMF_RESULT res;
    AVFilterContext *avctx = outlink->src;
    AVFilterLink   *inlink = avctx->inputs[0];
    AMFFilterContext  *ctx = avctx->priv;
    AVHWFramesContext *hwframes_out;
    AVHWDeviceContext   *hwdev_ctx;
    enum AVPixelFormat in_sw_format = inlink->format;
    enum AVPixelFormat out_sw_format = ctx->format;
    FilterLink        *inl = ff_filter_link(inlink);
    FilterLink        *outl = ff_filter_link(outlink);
    double w_adj = 1.0;

    if ((err = ff_scale_eval_dimensions(avctx,
                                        ctx->w_expr, ctx->h_expr,
                                        inlink, outlink,
                                        &ctx->width, &ctx->height)) < 0)
        return err;

    if (ctx->reset_sar && inlink->sample_aspect_ratio.num)
        w_adj = (double) inlink->sample_aspect_ratio.num / inlink->sample_aspect_ratio.den;

    ff_scale_adjust_dimensions(inlink, &ctx->width, &ctx->height,
                               ctx->force_original_aspect_ratio, ctx->force_divisible_by, w_adj);

    av_buffer_unref(&ctx->amf_device_ref);
    av_buffer_unref(&ctx->hwframes_in_ref);
    av_buffer_unref(&ctx->hwframes_out_ref);
    ctx->local_context = 0;
    if (inl->hw_frames_ctx) {
        AVHWFramesContext *frames_ctx = (AVHWFramesContext*)inl->hw_frames_ctx->data;
        if (av_av_to_amf_format(frames_ctx->sw_format) == AMF_SURFACE_UNKNOWN) {
            av_log(avctx, AV_LOG_ERROR, "Format of input frames context (%s) is not supported by AMF.\n",
                   av_get_pix_fmt_name(frames_ctx->sw_format));
            return AVERROR(EINVAL);
        }

        err = av_hwdevice_ctx_create_derived(&ctx->amf_device_ref, AV_HWDEVICE_TYPE_AMF, frames_ctx->device_ref, 0);
        if (err < 0)
            return err;

        ctx->hwframes_in_ref = av_buffer_ref(inl->hw_frames_ctx);
        if (!ctx->hwframes_in_ref)
            return AVERROR(ENOMEM);

        in_sw_format = frames_ctx->sw_format;
    } else if (avctx->hw_device_ctx) {
        err = av_hwdevice_ctx_create_derived(&ctx->amf_device_ref, AV_HWDEVICE_TYPE_AMF, avctx->hw_device_ctx, 0);
        if (err < 0)
            return err;
        ctx->hwdevice_ref = av_buffer_ref(avctx->hw_device_ctx);
        if (!ctx->hwdevice_ref)
            return AVERROR(ENOMEM);
    } else {
        res = av_hwdevice_ctx_create(&ctx->amf_device_ref, AV_HWDEVICE_TYPE_AMF, NULL, NULL, 0);
        AMF_RETURN_IF_FALSE(avctx, res == 0, res, "Failed to create  hardware device context (AMF) : %s\n", av_err2str(res));

    }
    if(out_sw_format == AV_PIX_FMT_NONE){
        if(outlink->format == AV_PIX_FMT_AMF_SURFACE)
            out_sw_format = in_sw_format;
        else
            out_sw_format = outlink->format;
    }
    ctx->hwframes_out_ref = av_hwframe_ctx_alloc(ctx->amf_device_ref);
    if (!ctx->hwframes_out_ref)
        return AVERROR(ENOMEM);
    hwframes_out = (AVHWFramesContext*)ctx->hwframes_out_ref->data;
    hwdev_ctx = (AVHWDeviceContext*)ctx->amf_device_ref->data;
    if (hwdev_ctx->type == AV_HWDEVICE_TYPE_AMF)
    {
        ctx->amf_device_ctx =  hwdev_ctx->hwctx;
    }
    hwframes_out->format    = AV_PIX_FMT_AMF_SURFACE;
    hwframes_out->sw_format = out_sw_format;

    if (inlink->format == AV_PIX_FMT_AMF_SURFACE) {
        *in_format = in_sw_format;
    } else {
        *in_format = inlink->format;
    }
    outlink->w = ctx->width;
    outlink->h = ctx->height;

    if (ctx->reset_sar)
        outlink->sample_aspect_ratio = (AVRational){1, 1};
    else if (inlink->sample_aspect_ratio.num) {
        outlink->sample_aspect_ratio = av_mul_q((AVRational){outlink->h * inlink->w, outlink->w * inlink->h}, inlink->sample_aspect_ratio);
    } else
        outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;

    hwframes_out->width = outlink->w;
    hwframes_out->height = outlink->h;

    err = av_hwframe_ctx_init(ctx->hwframes_out_ref);
    if (err < 0)
        return err;

    outl->hw_frames_ctx = av_buffer_ref(ctx->hwframes_out_ref);
    if (!outl->hw_frames_ctx) {
        return AVERROR(ENOMEM);
    }
    return 0;
}

void amf_free_amfsurface(void *opaque, uint8_t *data)
{
    AMFSurface *surface = (AMFSurface*)data;
    surface->pVtbl->Release(surface);
}

AVFrame *amf_amfsurface_to_avframe(AVFilterContext *avctx, AMFSurface* pSurface)
{
    AVFrame *frame = av_frame_alloc();
    AMFFilterContext  *ctx = avctx->priv;

    if (!frame)
        return NULL;

    if (ctx->hwframes_out_ref) {
        AVHWFramesContext *hwframes_out = (AVHWFramesContext *)ctx->hwframes_out_ref->data;
        if (hwframes_out->format == AV_PIX_FMT_AMF_SURFACE) {
            int ret = av_hwframe_get_buffer(ctx->hwframes_out_ref, frame, 0);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "Get hw frame failed.\n");
                av_frame_free(&frame);
                return NULL;
            }
            frame->data[0] = (uint8_t *)pSurface;
            frame->buf[1] = av_buffer_create((uint8_t *)pSurface, sizeof(AMFSurface),
                                            amf_free_amfsurface,
                                            (void*)avctx,
                                            AV_BUFFER_FLAG_READONLY);
        } else { // FIXME: add processing of other hw formats
            av_log(ctx, AV_LOG_ERROR, "Unknown pixel format\n");
            return NULL;
        }
    } else {

        switch (pSurface->pVtbl->GetMemoryType(pSurface))
        {
    #if CONFIG_D3D11VA
            case AMF_MEMORY_DX11:
            {
                AMFPlane *plane0 = pSurface->pVtbl->GetPlaneAt(pSurface, 0);
                frame->data[0] = plane0->pVtbl->GetNative(plane0);
                frame->data[1] = (uint8_t*)(intptr_t)0;

                frame->buf[0] = av_buffer_create(NULL,
                                        0,
                                        amf_free_amfsurface,
                                        pSurface,
                                        AV_BUFFER_FLAG_READONLY);
            }
            break;
    #endif
    #if CONFIG_DXVA2
            case AMF_MEMORY_DX9:
            {
                AMFPlane *plane0 = pSurface->pVtbl->GetPlaneAt(pSurface, 0);
                frame->data[3] = plane0->pVtbl->GetNative(plane0);

                frame->buf[0] = av_buffer_create(NULL,
                                        0,
                                        amf_free_amfsurface,
                                        pSurface,
                                        AV_BUFFER_FLAG_READONLY);
            }
            break;
    #endif
        default:
            {
                av_log(avctx, AV_LOG_ERROR, "Unsupported memory type : %d\n", pSurface->pVtbl->GetMemoryType(pSurface));
                return NULL;
            }
        }
    }

    return frame;
}

int amf_avframe_to_amfsurface(AVFilterContext *avctx, const AVFrame *frame, AMFSurface** ppSurface)
{
    AMFFilterContext *ctx = avctx->priv;
    AMFSurface *surface;
    AMF_RESULT  res;
    int hw_surface = 0;

    switch (frame->format) {
#if CONFIG_D3D11VA
    case AV_PIX_FMT_D3D11:
        {
            static const GUID AMFTextureArrayIndexGUID = { 0x28115527, 0xe7c3, 0x4b66, { 0x99, 0xd3, 0x4f, 0x2a, 0xe6, 0xb4, 0x7f, 0xaf } };
            ID3D11Texture2D *texture = (ID3D11Texture2D*)frame->data[0]; // actual texture
            int index = (intptr_t)frame->data[1]; // index is a slice in texture array is - set to tell AMF which slice to use
            texture->lpVtbl->SetPrivateData(texture, &AMFTextureArrayIndexGUID, sizeof(index), &index);

            res = ctx->amf_device_ctx->context->pVtbl->CreateSurfaceFromDX11Native(ctx->amf_device_ctx->context, texture, &surface, NULL); // wrap to AMF surface
            AMF_RETURN_IF_FALSE(avctx, res == AMF_OK, AVERROR(ENOMEM), "CreateSurfaceFromDX11Native() failed  with error %d\n", res);
            hw_surface = 1;
        }
        break;
#endif
    case AV_PIX_FMT_AMF_SURFACE:
        {
            surface = (AMFSurface*)frame->data[0]; // actual surface
            surface->pVtbl->Acquire(surface); // returned surface has to be to be ref++
            hw_surface = 1;
        }
        break;

#if CONFIG_DXVA2
    case AV_PIX_FMT_DXVA2_VLD:
        {
            IDirect3DSurface9 *texture = (IDirect3DSurface9 *)frame->data[3]; // actual texture

            res = ctx->amf_device_ctx->context->pVtbl->CreateSurfaceFromDX9Native(ctx->amf_device_ctx->context, texture, &surface, NULL); // wrap to AMF surface
            AMF_RETURN_IF_FALSE(avctx, res == AMF_OK, AVERROR(ENOMEM), "CreateSurfaceFromDX9Native() failed  with error %d\n", res);
            hw_surface = 1;
        }
        break;
#endif
    default:
        {
            AMF_SURFACE_FORMAT amf_fmt = av_av_to_amf_format(frame->format);
            res = ctx->amf_device_ctx->context->pVtbl->AllocSurface(ctx->amf_device_ctx->context, AMF_MEMORY_HOST, amf_fmt, frame->width, frame->height, &surface);
            AMF_RETURN_IF_FALSE(avctx, res == AMF_OK, AVERROR(ENOMEM), "AllocSurface() failed  with error %d\n", res);
            amf_copy_surface(avctx, frame, surface);
        }
        break;
    }

    if (frame->crop_left || frame->crop_right || frame->crop_top || frame->crop_bottom) {
        size_t crop_x = frame->crop_left;
        size_t crop_y = frame->crop_top;
        size_t crop_w = frame->width - (frame->crop_left + frame->crop_right);
        size_t crop_h = frame->height - (frame->crop_top + frame->crop_bottom);
        AVFilterLink *outlink = avctx->outputs[0];
        if (crop_x || crop_y) {
            if (crop_w == outlink->w && crop_h == outlink->h) {
                AMFData *cropped_buffer = NULL;
                res = surface->pVtbl->Duplicate(surface, surface->pVtbl->GetMemoryType(surface), &cropped_buffer);
                AMF_RETURN_IF_FALSE(avctx, res == AMF_OK, AVERROR(ENOMEM), "Duplicate() failed  with error %d\n", res);
                surface->pVtbl->Release(surface);
                surface = (AMFSurface*)cropped_buffer;
            }
            else
                surface->pVtbl->SetCrop(surface, (amf_int32)crop_x, (amf_int32)crop_y, (amf_int32)crop_w, (amf_int32)crop_h);
        }
        else
            surface->pVtbl->SetCrop(surface, (amf_int32)crop_x, (amf_int32)crop_y, (amf_int32)crop_w, (amf_int32)crop_h);
    }
    else if (hw_surface) {
        // input HW surfaces can be vertically aligned by 16; tell AMF the real size
        surface->pVtbl->SetCrop(surface, 0, 0, frame->width, frame->height);
    }

    surface->pVtbl->SetPts(surface, frame->pts);
    *ppSurface = surface;
    return 0;
}
