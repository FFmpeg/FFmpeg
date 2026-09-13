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
#include "filters.h"
#include "formats.h"
#include "libavutil/mem.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time.h"

#include "AMF/components/VideoDecoderUVD.h"
#include "libavutil/hwcontext_amf.h"
#include "libavutil/hwcontext_amf_internal.h"
#include "scale_eval.h"

#if CONFIG_DXVA2
#include <d3d9.h>
#endif

#if CONFIG_D3D11VA
#include <d3d11.h>
#include "libavutil/hwcontext_d3d11va.h"
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
    ctx->format_opt = ctx->format;
    ctx->shader_input = 1;

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

    if (ctx->pre_converter) {
        ctx->pre_converter->pVtbl->Terminate(ctx->pre_converter);
        ctx->pre_converter->pVtbl->Release(ctx->pre_converter);
        ctx->pre_converter = NULL;
    }

    if (ctx->pending) {
        AVFrame *props;
        while (av_fifo_read(ctx->pending, &props, 1) >= 0)
            av_frame_free(&props);
        av_fifo_freep2(&ctx->pending);
    }

    if (ctx->master_display)
        av_freep(&ctx->master_display);

    if (ctx->light_meta)
        av_freep(&ctx->light_meta);

    av_buffer_unref(&ctx->amf_device_ref);
    av_buffer_unref(&ctx->hwdevice_ref);
    av_buffer_unref(&ctx->hwframes_in_ref);
    av_buffer_unref(&ctx->hwframes_out_ref);
}

static int amf_queue_props(AVFilterContext *avctx, const AVFrame *in)
{
    AMFFilterContext *ctx = avctx->priv;
    AVFrame *props = av_frame_alloc();
    int ret;

    if (!props)
        return AVERROR(ENOMEM);
    props->width  = in->width;
    props->height = in->height;
    ret = av_frame_copy_props(props, in);
    if (ret >= 0)
        ret = av_fifo_write(ctx->pending, &props, 1);
    if (ret < 0)
        av_frame_free(&props);
    return ret;
}

static int amf_receive_surface(AVFilterContext *avctx, AMFComponent *component, AMFSurface **surface)
{
    AMFGuid guid = IID_AMFSurface();
    AMFData *data = NULL;
    AMF_RESULT res;

    *surface = NULL;
    res = component->pVtbl->QueryOutput(component, &data);
    AMF_RETURN_IF_FALSE(avctx, res == AMF_OK || res == AMF_REPEAT || res == AMF_EOF,
                        AVERROR_UNKNOWN, "QueryOutput() failed with error %d\n", res);
    if (!data)
        return res == AMF_EOF ? AVERROR_EOF : 0;

    res = data->pVtbl->QueryInterface(data, &guid, (void**)surface);
    data->pVtbl->Release(data);
    AMF_RETURN_IF_FALSE(avctx, res == AMF_OK, AVERROR_UNKNOWN, "QueryInterface(IID_AMFSurface) failed with error %d\n", res);
    return 1;
}

static int amf_deliver_output(AVFilterContext *avctx)
{
    AMFFilterContext             *ctx = avctx->priv;
    AVFilterLink             *outlink = avctx->outputs[0];
    AMFSurface *surface;
    AVFrame *props, *out;
    enum AVColorSpace out_colorspace;
    enum AVColorRange out_color_range;
    int64_t pts;
    int ret, count = 0;

    while ((ret = amf_receive_surface(avctx, ctx->component, &surface)) > 0) {
        pts = surface->pVtbl->GetPts(surface);
        out = amf_amfsurface_to_avframe(avctx, surface);
        if (!out)
            return AVERROR(ENOMEM);

        if (ctx->outputs_per_input > 1) {
            while (av_fifo_can_read(ctx->pending) > 1) {
                av_fifo_peek(ctx->pending, &props, 1, 1);
                if (props->pts > pts)
                    break;
                av_fifo_read(ctx->pending, &props, 1);
                av_frame_free(&props);
            }
            props = NULL;
            if (av_fifo_can_read(ctx->pending))
                av_fifo_peek(ctx->pending, &props, 1, 0);
            ret = props ? av_frame_copy_props(out, props) : 0;
        } else {
            props = NULL;
            av_fifo_read(ctx->pending, &props, 1);
            ret = props ? av_frame_copy_props(out, props) : 0;
            av_frame_free(&props);
        }
        if (ret < 0) {
            av_frame_free(&out);
            return ret;
        }
        out->pts = pts;
        if (ctx->outputs_per_input > 1)
            out->duration /= ctx->outputs_per_input;

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
        if (ctx->out_color_range == AMF_COLOR_RANGE_FULL)
            out_color_range = AVCOL_RANGE_JPEG;
        else if (ctx->out_color_range == AMF_COLOR_RANGE_STUDIO)
            out_color_range = AVCOL_RANGE_MPEG;

        if (ctx->out_color_range != AMF_COLOR_RANGE_UNDEFINED)
            out->color_range = out_color_range;

        if (ctx->out_primaries != AMF_COLOR_PRIMARIES_UNDEFINED)
            out->color_primaries = ctx->out_primaries;

        if (ctx->out_trc != AMF_COLOR_TRANSFER_CHARACTERISTIC_UNDEFINED)
            out->color_trc = ctx->out_trc;

        if (ctx->pre_converter)
            out->colorspace = AVCOL_SPC_RGB;

        ret = ff_filter_frame(outlink, out);
        if (ret < 0)
            return ret;
        count++;
    }

    return ret < 0 ? ret : count;
}

static int amf_submit_surface(AVFilterContext *avctx, AMFComponent *component, AMFSurface *surface,
                              int (*deliver)(AVFilterContext *avctx))
{
    AMF_RESULT res;
    int ret = 0;

    while ((res = component->pVtbl->SubmitInput(component, (AMFData*)surface)) == AMF_INPUT_FULL) {
        ret = deliver(avctx);
        if (ret < 0)
            break;
        if (!ret)
            av_usleep(100);
    }
    surface->pVtbl->Release(surface);
    if (ret < 0)
        return ret;
    AMF_RETURN_IF_FALSE(avctx, res == AMF_OK, AVERROR_UNKNOWN, "SubmitInput() failed with error %d\n", res);
    return 0;
}

static int amf_forward_converted(AVFilterContext *avctx)
{
    AMFFilterContext *ctx = avctx->priv;
    AMFSurface *surface;
    int ret, count = 0;

    while ((ret = amf_receive_surface(avctx, ctx->pre_converter, &surface)) > 0) {
        ret = amf_submit_surface(avctx, ctx->component, surface, amf_deliver_output);
        if (ret < 0)
            return ret;
        count++;
    }

    return ret < 0 ? ret : count;
}

static int amf_drain_component(AVFilterContext *avctx, AMFComponent *component,
                               int (*deliver)(AVFilterContext *avctx))
{
    AMF_RESULT res;
    int ret;

    while ((res = component->pVtbl->Drain(component)) == AMF_INPUT_FULL) {
        ret = deliver(avctx);
        if (ret < 0)
            return ret;
        if (!ret)
            av_usleep(100);
    }
    AMF_RETURN_IF_FALSE(avctx, res == AMF_OK, AVERROR_UNKNOWN, "Drain() failed with error %d\n", res);

    while ((ret = deliver(avctx)) >= 0)
        if (!ret)
            av_usleep(100);

    return ret == AVERROR_EOF ? 0 : ret;
}

int amf_filter_filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext             *avctx = inlink->dst;
    AMFFilterContext             *ctx = avctx->priv;
    AMFSurface *surface_in;
    int ret;

    if (!ctx->component) {
        av_frame_free(&in);
        return AVERROR(EINVAL);
    }

    ret = amf_avframe_to_amfsurface(avctx, in, &surface_in);
    if (ret < 0)
        goto fail;

    ret = amf_queue_props(avctx, in);
    if (ret < 0) {
        surface_in->pVtbl->Release(surface_in);
        goto fail;
    }

    if (ctx->pre_converter) {
        ret = amf_submit_surface(avctx, ctx->pre_converter, surface_in, amf_forward_converted);
        if (ret >= 0)
            ret = amf_forward_converted(avctx);
    } else
        ret = amf_submit_surface(avctx, ctx->component, surface_in, amf_deliver_output);
    if (ret >= 0)
        ret = amf_deliver_output(avctx);
fail:
    av_frame_free(&in);
    return ret;
}

static int amf_poll_output(AVFilterContext *avctx)
{
    AMFFilterContext *ctx = avctx->priv;
    int ret = 0;

    if (!av_fifo_can_read(ctx->pending))
        return 0;
    if (ctx->pre_converter)
        ret = amf_forward_converted(avctx);
    if (ret >= 0)
        ret = amf_deliver_output(avctx);
    return ret;
}

int amf_filter_activate(AVFilterContext *avctx)
{
    AMFFilterContext             *ctx = avctx->priv;
    AVFilterLink              *inlink = avctx->inputs[0];
    AVFilterLink             *outlink = avctx->outputs[0];
    AVFrame *in = NULL;
    int ret;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    if (!ctx->eof) {
        if (ctx->component) {
            ret = amf_poll_output(avctx);
            if (ret < 0)
                return ret;
        }
        ret = ff_inlink_consume_frame(inlink, &in);
        if (ret < 0)
            return ret;
        if (in) {
            ret = amf_filter_filter_frame(inlink, in);
            if (ret < 0)
                return ret;
        } else if (ff_inlink_acknowledge_status(inlink, &ctx->status, &ctx->status_pts))
            ctx->eof = 1;
    }

    if (ctx->eof) {
        if (ctx->component && !ctx->drained) {
            ctx->drained = 1;
            if (ctx->pre_converter) {
                ret = amf_drain_component(avctx, ctx->pre_converter, amf_forward_converted);
                if (ret < 0)
                    return ret;
            }
            ret = amf_drain_component(avctx, ctx->component, amf_deliver_output);
            if (ret < 0)
                return ret;
        }
        ff_outlink_set_status(outlink, ctx->status, ctx->status_pts);
        return 0;
    }

    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

int amf_setup_input_output_formats(AVFilterContext *avctx,
                                    const enum AVPixelFormat *input_pix_fmts)
{
    int err;
    AVFilterFormats *input_formats;
    AVFilterFormats *output_formats;
    static const enum AVPixelFormat output_pix_fmts[] = {
        AV_PIX_FMT_AMF_SURFACE,
        AV_PIX_FMT_NONE,
    };

    //in case if hw_device_ctx is set to DXVA2 we change order of pixel formats to set DXVA2 be chosen by default
    //The order is ignored if hw_frames_ctx is not NULL on the config_output stage
    if (avctx->hw_device_ctx) {
        AVHWDeviceContext *device_ctx = (AVHWDeviceContext*)avctx->hw_device_ctx->data;

        switch (device_ctx->type) {
    #if CONFIG_D3D11VA
        case AV_HWDEVICE_TYPE_D3D11VA:
            {
                static const enum AVPixelFormat pix_fmts_d3d11[] = {
                    AV_PIX_FMT_D3D11,
                    AV_PIX_FMT_NONE,
                };
                input_pix_fmts  = pix_fmts_d3d11;
            }
            break;
    #endif
    #if CONFIG_DXVA2
        case AV_HWDEVICE_TYPE_DXVA2:
            {
                static const enum AVPixelFormat pix_fmts_dxva2[] = {
                    AV_PIX_FMT_DXVA2_VLD,
                    AV_PIX_FMT_NONE,
                };
                input_pix_fmts  = pix_fmts_dxva2;
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

    input_formats = ff_make_pixel_format_list(input_pix_fmts);
    if (!input_formats) {
        return AVERROR(ENOMEM);
    }
    output_formats = ff_make_pixel_format_list(output_pix_fmts);
    if (!output_formats) {
        ff_formats_unref(&input_formats);
        return AVERROR(ENOMEM);
    }

    if ((err = ff_formats_ref(input_formats, &avctx->inputs[0]->outcfg.formats)) < 0) {
        ff_formats_unref(&output_formats);
        return err;
    }

    return ff_formats_ref(output_formats, &avctx->outputs[0]->incfg.formats);
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

enum AVPixelFormat amf_inlink_sw_format(AVFilterLink *inlink)
{
    FilterLink *inl = ff_filter_link(inlink);

    if (inl->hw_frames_ctx)
        return ((AVHWFramesContext*)inl->hw_frames_ctx->data)->sw_format;
    return inlink->format;
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

    if (ctx->w_expr && ctx->h_expr) {
        if ((err = ff_scale_eval_dimensions(avctx,
                                            ctx->w_expr, ctx->h_expr,
                                            inlink, outlink,
                                            &ctx->width, &ctx->height)) < 0)
            return err;
    } else {
        ctx->width = inlink->w;
        ctx->height = inlink->h;
    }

    if (ctx->reset_sar && inlink->sample_aspect_ratio.num)
        w_adj = (double) inlink->sample_aspect_ratio.num / inlink->sample_aspect_ratio.den;

    err = ff_scale_adjust_dimensions(inlink, &ctx->width, &ctx->height,
                                     ctx->force_original_aspect_ratio,
                                     ctx->force_divisible_by, w_adj);
    if (err < 0)
        return err;

    if (!ctx->pending) {
        ctx->pending = av_fifo_alloc2(1, sizeof(AVFrame*), AV_FIFO_FLAG_AUTO_GROW);
        if (!ctx->pending)
            return AVERROR(ENOMEM);
    }

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
    if (out_sw_format == AV_PIX_FMT_NONE) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(outlink->format);
        if (desc && (desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
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

    // in_sw_format is inlink->format for software input and the underlying
    // sw_format for any hw frames input (AMF, D3D11, DXVA2); the component
    // must always be initialized with a software surface format.
    *in_format = in_sw_format;
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

    if (!frame) {
        pSurface->pVtbl->Release(pSurface);
        return NULL;
    }

    if (ctx->hwframes_out_ref) {
        AVHWFramesContext *hwframes_out = (AVHWFramesContext *)ctx->hwframes_out_ref->data;
        if (hwframes_out->format == AV_PIX_FMT_AMF_SURFACE) {
            int ret = av_hwframe_get_buffer(ctx->hwframes_out_ref, frame, 0);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "Get hw frame failed.\n");
                goto fail;
            }
            frame->data[0] = (uint8_t *)pSurface;
            frame->buf[1] = av_buffer_create((uint8_t *)pSurface, sizeof(AMFSurface),
                                            amf_free_amfsurface,
                                            (void*)avctx,
                                            AV_BUFFER_FLAG_READONLY);
            if (!frame->buf[1])
                goto fail;
        } else { // FIXME: add processing of other hw formats
            av_log(ctx, AV_LOG_ERROR, "Unknown pixel format\n");
            goto fail;
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
                goto fail;
            }
        }
    }


    return frame;
fail:
    pSurface->pVtbl->Release(pSurface);
    av_frame_free(&frame);
    return NULL;
}

#if CONFIG_D3D11VA
/* The AMF filter components read their input with a shader, so they reject a
 * texture created without D3D11_BIND_SHADER_RESOURCE, which is what a D3D11VA
 * decoder pool gives us. Copy the slice into an AMF allocated surface, which
 * carries the flags the components need. CopySubresourceRegion() uses the copy
 * engine, so it can read the decoder texture that a shader cannot. */
static int amf_copy_d3d11_texture(AVFilterContext *avctx, const AVFrame *frame,
                                  int index, AMFSurface **ppSurface)
{
    AMFFilterContext        *ctx = avctx->priv;
    AVHWFramesContext    *frames = (AVHWFramesContext*)frame->hw_frames_ctx->data;
    AVD3D11VADeviceContext *hwctx = frames->device_ctx->hwctx;
    ID3D11Texture2D      *texture = (ID3D11Texture2D*)frame->data[0];
    AMFSurface           *surface = NULL;
    AMFPlane               *plane;
    D3D11_TEXTURE2D_DESC     desc;
    D3D11_BOX                 box;
    AMF_RESULT                res;

    res = ctx->amf_device_ctx->context->pVtbl->AllocSurface(ctx->amf_device_ctx->context,
              AMF_MEMORY_DX11, av_av_to_amf_format(frames->sw_format),
              frame->width, frame->height, &surface);
    AMF_RETURN_IF_FALSE(avctx, res == AMF_OK, AVERROR(ENOMEM), "AllocSurface() failed with error %d\n", res);

    plane = surface->pVtbl->GetPlaneAt(surface, 0);
    if (!plane) {
        surface->pVtbl->Release(surface);
        return AVERROR(ENOMEM);
    }

    // The decoder pool is allocated with aligned dimensions, so copy the coded
    // area rather than the whole source subresource. D3D11 wants even bounds
    // for a planar format, and the source is at least that large.
    texture->lpVtbl->GetDesc(texture, &desc);
    box.left   = 0;
    box.top    = 0;
    box.front  = 0;
    box.right  = FFMIN(FFALIGN(frame->width,  2), desc.Width);
    box.bottom = FFMIN(FFALIGN(frame->height, 2), desc.Height);
    box.back   = 1;

    hwctx->lock(hwctx->lock_ctx);
    hwctx->device_context->lpVtbl->CopySubresourceRegion(hwctx->device_context,
        (ID3D11Resource*)plane->pVtbl->GetNative(plane), 0, 0, 0, 0,
        (ID3D11Resource*)texture, index, &box);
    hwctx->unlock(hwctx->lock_ctx);

    *ppSurface = surface;
    return 0;
}
#endif

#if CONFIG_D3D11VA || CONFIG_DXVA2
typedef struct AMFFrameHolder {
    AMFSurfaceObserver observer;
    AVFrame *frame;
} AMFFrameHolder;

static void AMF_STD_CALL amf_release_held_frame(AMFSurfaceObserver *observer, AMFSurface *surface)
{
    AMFFrameHolder *holder = (AMFFrameHolder*)observer;

    av_frame_free(&holder->frame);
    av_free(holder);
}

static const AMFSurfaceObserverVtbl amf_frame_holder_vtbl = { amf_release_held_frame };

static int amf_hold_frame(const AVFrame *frame, AMFSurfaceObserver **observer)
{
    AMFFrameHolder *holder = av_mallocz(sizeof(*holder));

    if (!holder)
        return AVERROR(ENOMEM);
    holder->frame = av_frame_clone(frame);
    if (!holder->frame) {
        av_free(holder);
        return AVERROR(ENOMEM);
    }
    holder->observer.pVtbl = &amf_frame_holder_vtbl;
    *observer = &holder->observer;
    return 0;
}
#endif

int amf_avframe_to_amfsurface(AVFilterContext *avctx, const AVFrame *frame, AMFSurface** ppSurface)
{
    AMFVariantStruct var = { 0 };
    AMFFilterContext *ctx = avctx->priv;
    AMFBuffer  *hdrmeta_buffer = NULL;
    AMFSurface *surface;
    AMF_RESULT  res;
    int hw_surface = 0;
#if CONFIG_D3D11VA || CONFIG_DXVA2
    AMFSurfaceObserver *observer;
#endif

    switch (frame->format) {
#if CONFIG_D3D11VA
    case AV_PIX_FMT_D3D11:
        {
            static const GUID AMFTextureArrayIndexGUID = { 0x28115527, 0xe7c3, 0x4b66, { 0x99, 0xd3, 0x4f, 0x2a, 0xe6, 0xb4, 0x7f, 0xaf } };
            ID3D11Texture2D *texture = (ID3D11Texture2D*)frame->data[0]; // actual texture
            int index = (intptr_t)frame->data[1]; // index is a slice in texture array is - set to tell AMF which slice to use
            D3D11_TEXTURE2D_DESC desc;
            int ret;

            texture->lpVtbl->GetDesc(texture, &desc);
            if (ctx->shader_input && !(desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) && frame->hw_frames_ctx) {
                ret = amf_copy_d3d11_texture(avctx, frame, index, &surface);
                if (ret < 0)
                    return ret;
                hw_surface = 1;
                break;
            }

            texture->lpVtbl->SetPrivateData(texture, &AMFTextureArrayIndexGUID, sizeof(index), &index);

            ret = amf_hold_frame(frame, &observer);
            if (ret < 0)
                return ret;
            res = ctx->amf_device_ctx->context->pVtbl->CreateSurfaceFromDX11Native(ctx->amf_device_ctx->context, texture, &surface, observer); // wrap to AMF surface
            if (res != AMF_OK)
                amf_release_held_frame(observer, NULL);
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
            int ret;

            ret = amf_hold_frame(frame, &observer);
            if (ret < 0)
                return ret;
            res = ctx->amf_device_ctx->context->pVtbl->CreateSurfaceFromDX9Native(ctx->amf_device_ctx->context, texture, &surface, observer); // wrap to AMF surface
            if (res != AMF_OK)
                amf_release_held_frame(observer, NULL);
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

    // If AMFSurface comes from other AMF components, it may have various
    // properties already set. These properties can be used by other AMF
    // components to perform their tasks. In the context of the AMF video
    // filter, that other component could be an AMFVideoConverter. By default,
    // AMFVideoConverter will use HDR related properties assigned to a surface
    // by an AMFDecoder. If frames (surfaces) originated from any other source,
    // i.e. from hevcdec, assign those properties from avframe; do not
    // overwrite these properties if they already have a value.
    res = surface->pVtbl->GetProperty(surface, AMF_VIDEO_DECODER_COLOR_TRANSFER_CHARACTERISTIC, &var);

    if (res == AMF_NOT_FOUND && frame->color_trc != AVCOL_TRC_UNSPECIFIED)
        // Note: as of now(Feb 2026), most AV and AMF enums are interchangeable.
        // TBD: can enums change their values in the future?
        // For better future-proofing it's better to have dedicated
        // enum mapping functions.
        AMF_ASSIGN_PROPERTY_INT64(res, surface, AMF_VIDEO_DECODER_COLOR_TRANSFER_CHARACTERISTIC, frame->color_trc);

    res = surface->pVtbl->GetProperty(surface, AMF_VIDEO_DECODER_COLOR_PRIMARIES, &var);
    if (res == AMF_NOT_FOUND && frame->color_primaries != AVCOL_PRI_UNSPECIFIED)
        AMF_ASSIGN_PROPERTY_INT64(res, surface, AMF_VIDEO_DECODER_COLOR_PRIMARIES, frame->color_primaries);

    res = surface->pVtbl->GetProperty(surface, AMF_VIDEO_DECODER_COLOR_RANGE, &var);
    if (res == AMF_NOT_FOUND && frame->color_range != AVCOL_RANGE_UNSPECIFIED)
        AMF_ASSIGN_PROPERTY_INT64(res, surface, AMF_VIDEO_DECODER_COLOR_RANGE, frame->color_range);

    // Color range for older drivers
    if (frame->color_range == AVCOL_RANGE_JPEG) {
        AMF_ASSIGN_PROPERTY_BOOL(res, surface, AMF_VIDEO_DECODER_FULL_RANGE_COLOR, 1);
    } else if (frame->color_range != AVCOL_RANGE_UNSPECIFIED)
        AMF_ASSIGN_PROPERTY_BOOL(res, surface, AMF_VIDEO_DECODER_FULL_RANGE_COLOR, 0);

    // Color profile for newer drivers
    res = surface->pVtbl->GetProperty(surface, AMF_VIDEO_DECODER_COLOR_PROFILE, &var);
    if (res == AMF_NOT_FOUND && frame->color_range != AVCOL_RANGE_UNSPECIFIED && frame->colorspace != AVCOL_SPC_UNSPECIFIED) {
        amf_int64 color_profile = color_profile = av_amf_get_color_profile(frame->color_range, frame->colorspace);

        if (color_profile != AMF_VIDEO_CONVERTER_COLOR_PROFILE_UNKNOWN)
            AMF_ASSIGN_PROPERTY_INT64(res, surface, AMF_VIDEO_DECODER_COLOR_PROFILE, color_profile);
    }

    if (ctx->in_trc == AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE2084 && (ctx->master_display || ctx->light_meta)) {
        res = ctx->amf_device_ctx->context->pVtbl->AllocBuffer(ctx->amf_device_ctx->context, AMF_MEMORY_HOST, sizeof(AMFHDRMetadata), &hdrmeta_buffer);
        if (res == AMF_OK) {
            AMFHDRMetadata *hdrmeta = (AMFHDRMetadata*)hdrmeta_buffer->pVtbl->GetNative(hdrmeta_buffer);

            av_amf_display_mastering_meta_to_hdrmeta(ctx->master_display, hdrmeta);
            av_amf_light_metadata_to_hdrmeta(ctx->light_meta, hdrmeta);
            AMF_ASSIGN_PROPERTY_INTERFACE(res, surface, AMF_VIDEO_DECODER_HDR_METADATA, hdrmeta_buffer);
        }
    } else if (frame->color_trc == AVCOL_TRC_SMPTE2084) {
        res = surface->pVtbl->GetProperty(surface, AMF_VIDEO_DECODER_HDR_METADATA, &var);
        if (res == AMF_NOT_FOUND) {
            res = ctx->amf_device_ctx->context->pVtbl->AllocBuffer(ctx->amf_device_ctx->context, AMF_MEMORY_HOST, sizeof(AMFHDRMetadata), &hdrmeta_buffer);
            if (res == AMF_OK) {
                AMFHDRMetadata *hdrmeta = (AMFHDRMetadata*)hdrmeta_buffer->pVtbl->GetNative(hdrmeta_buffer);

                if (av_amf_extract_hdr_metadata(frame, hdrmeta) == 0)
                    AMF_ASSIGN_PROPERTY_INTERFACE(res, surface, AMF_VIDEO_DECODER_HDR_METADATA, hdrmeta_buffer);
            }
        }
    }

    if (hdrmeta_buffer) {
        hdrmeta_buffer->pVtbl->Release(hdrmeta_buffer);
        hdrmeta_buffer = NULL;
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
