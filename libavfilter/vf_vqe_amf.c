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

/**
 * @file
 * Quality Enhancer video filter with AMF hardware acceleration
 */

#include "libavutil/opt.h"

#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_amf.h"
#include "libavutil/hwcontext_amf_internal.h"

#include "AMF/components/VQEnhancer.h"
#include "vf_amf_common.h"

#include "avfilter.h"
#include "avfilter_internal.h"
#include "formats.h"
#include "video.h"

#if CONFIG_D3D11VA
#include <d3d11.h>
#endif

#if CONFIG_D3D12VA
#include <d3d12.h>
#endif

typedef struct AMFVQEFilterContext {
    AMFFilterContext common;

    int engine_type;
    double attenuation;
} AMFVQEFilterContext;

static int amf_vqe_init(AVFilterContext *avctx) {
    AMFVQEFilterContext *ctx = avctx->priv;

    ctx->common.format = AV_PIX_FMT_NONE;

    return 0;
}

static int amf_filter_query_formats(AVFilterContext *avctx)
{
    const enum AVPixelFormat *output_pix_fmts;
    static const enum AVPixelFormat input_pix_fmts[] = {
        AV_PIX_FMT_AMF_SURFACE,
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_P010,
        AV_PIX_FMT_BGRA,
        AV_PIX_FMT_RGBA,
        AV_PIX_FMT_RGBAF16,
        AV_PIX_FMT_X2BGR10,
        AV_PIX_FMT_NONE,
    };
    static const enum AVPixelFormat output_pix_fmts_default[] = {
        AV_PIX_FMT_AMF_SURFACE,
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_P010,
        AV_PIX_FMT_BGRA,
        AV_PIX_FMT_RGBA,
        AV_PIX_FMT_RGBAF16,
        AV_PIX_FMT_X2BGR10,
        AV_PIX_FMT_NONE,
    };
    output_pix_fmts = output_pix_fmts_default;

    return amf_setup_input_output_formats(avctx, input_pix_fmts, output_pix_fmts);
}

static int amf_vqe_filter_config_output(AVFilterLink *outlink)
{
    AVFilterContext     *avctx = outlink->src;
    AMFComponent        *amf_filter = NULL;
    AVFilterLink        *inlink = avctx->inputs[0];
    AMFVQEFilterContext *vqe_ctx = avctx->priv;
    AMFFilterContext    *amf_ctx = &vqe_ctx->common;
    AVAMFDeviceContext  *device_ctx = NULL;

    int err;
    AMF_RESULT res;
    enum AVPixelFormat in_format;

    err = amf_init_filter_config(outlink, &in_format);
    if (err < 0)
        return err;

    device_ctx = amf_ctx->amf_device_ctx;

    res = AMF_IFACE_CALL(device_ctx->factory, CreateComponent, device_ctx->context, AMFVQEnhancer, &amf_ctx->component);
    AMF_RETURN_IF_FALSE(avctx, res == AMF_OK, AVERROR_FILTER_NOT_FOUND, "CreateComponent(%ls) failed with error %d\n", AMFVQEnhancer, res);

    amf_filter = amf_ctx->component;

    if (vqe_ctx->engine_type != -1)
        AMF_ASSIGN_PROPERTY_INT64(res, amf_filter, AMF_VIDEO_ENHANCER_ENGINE_TYPE, vqe_ctx->engine_type);

    AMF_ASSIGN_PROPERTY_DOUBLE(res, amf_filter, AMF_VE_FCR_ATTENUATION, vqe_ctx->attenuation);
    AMF_RETURN_IF_FALSE(avctx, res == AMF_OK, AVERROR_UNKNOWN, "Failed to set VQ enhancer attenuation: %d\n", res);

    res = AMF_IFACE_CALL(amf_filter, Init, av_av_to_amf_format(in_format), inlink->w, inlink->h);
    AMF_RETURN_IF_FALSE(avctx, res == AMF_OK, AVERROR_UNKNOWN, "AMFVQEnhancer-Init() failed with error %d\n", res);

    return 0;
}

#define OFFSET(x) offsetof(AMFVQEFilterContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
static const AVOption vqe_amf_options[] = {
    { "engine_type",    "Engine type",   OFFSET(engine_type), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, AMF_MEMORY_DX12, .flags = FLAGS, "engine_type" },
    { "dx11",           "DirectX 11",    0,  AV_OPT_TYPE_CONST,   { .i64 = AMF_MEMORY_DX11 }, 0, 0, FLAGS, "engine_type" },
    { "vulkan",         "Vulkan",        0,  AV_OPT_TYPE_CONST,   { .i64 = AMF_MEMORY_VULKAN }, 0, 0, FLAGS, "engine_type" },
    { "dx12",           "DirectX 12",    0,  AV_OPT_TYPE_CONST,   { .i64 = AMF_MEMORY_DX12 }, 0, 0, FLAGS, "engine_type" },

    { "attenuation",    "Control VQEnhancer strength", OFFSET(attenuation), AV_OPT_TYPE_DOUBLE,  { .dbl = 0.1  },  0.02, 0.4, FLAGS, "attenuation" },

    { NULL },
};

AVFILTER_DEFINE_CLASS(vqe_amf);

static const AVFilterPad amf_filter_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = amf_filter_filter_frame,
    }
};

static const AVFilterPad amf_filter_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = amf_vqe_filter_config_output,
    }
};

FFFilter ff_vf_vqe_amf = {
    .p.name         = "vqe_amf",
    .p.description  = NULL_IF_CONFIG_SMALL("AMD AMF VQ Enhancer"),
    .p.priv_class   = &vqe_amf_class,
    .p.flags        = AVFILTER_FLAG_HWDEVICE,
    .priv_size      = sizeof(AMFVQEFilterContext),
    .init           = amf_vqe_init,
    .uninit         = amf_filter_uninit,
    FILTER_INPUTS(amf_filter_inputs),
    FILTER_OUTPUTS(amf_filter_outputs),
    FILTER_QUERY_FUNC(&amf_filter_query_formats),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
