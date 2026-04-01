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
 * Video Frame Rate Converter filter with AMF hardware acceleration
 */

#include "libavutil/internal.h"
#include "libavutil/opt.h"

#include "avfilter_internal.h"

#include "vf_amf_common.h"
#include "AMF/components/FRC.h"
#include "libavutil/hwcontext_amf_internal.h"

#if CONFIG_D3D11VA
#include <d3d11.h>
#endif

#if CONFIG_D3D12VA
#include <d3d12.h>
#endif

// TODO: move this elsewhere.
#define AMF_FRC_ASSIGN_PROPERTY_INT64_CHECK(avctx, this, name, val) {\
    AMF_ASSIGN_PROPERTY_INT64(res, this, name, val);\
    if (res != AMF_OK)\
        av_log(avctx, AV_LOG_WARNING, "AMFFRC->SetProperty(%ls, %d) failed with error %d\n", name, val, res);\
}

typedef struct AMFFRCFilterContext {
    AMFFilterContext common;

    int engine_type;
    int enable;
    int fallback;
    int indicator;
    int profile;
    int mv_search_mode;
    int use_future_frame;
} AMFFRCFilterContext;

static int amf_frc_init(AVFilterContext *avctx) {
    AMFFRCFilterContext *ctx = avctx->priv;

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

static int amf_frc_filter_config_output(AVFilterLink *outlink)
{
    AVFilterContext  *avctx = outlink->src;
    AMFComponent     *amf_filter = NULL;
    AVFilterLink     *inlink = avctx->inputs[0];
    FilterLink       *il = ff_filter_link(inlink);
    FilterLink       *ol = ff_filter_link(outlink);
    AMFFRCFilterContext  *frc_ctx = avctx->priv;
    AMFFilterContext     *amf_ctx = &frc_ctx->common;
    AVAMFDeviceContext   *device_ctx = NULL;

    int err;
    AMF_RESULT res;
    enum AVPixelFormat in_format;

    err = amf_init_filter_config(outlink, &in_format);
    if (err < 0)
        return err;

    device_ctx = amf_ctx->amf_device_ctx;

    res = AMF_IFACE_CALL(device_ctx->factory, CreateComponent, device_ctx->context, AMFFRC, &amf_ctx->component);
    AMF_RETURN_IF_FALSE(avctx, res == AMF_OK, AVERROR_FILTER_NOT_FOUND, "CreateComponent(%ls) failed with error %d\n", AMFFRC, res);

    amf_filter = amf_ctx->component;

    outlink->time_base = inlink->time_base;
    ol->frame_rate = il->frame_rate;
    ol->frame_rate.num *= 2;

    // Possible bug: FRC must be initialized enabled to be toggleable on the fly after init.
    AMF_FRC_ASSIGN_PROPERTY_INT64_CHECK(avctx, amf_filter, AMF_FRC_MODE, FRC_x2_PRESENT);

    if (frc_ctx->engine_type != -1)
        AMF_FRC_ASSIGN_PROPERTY_INT64_CHECK(avctx, amf_filter, AMF_FRC_ENGINE_TYPE, frc_ctx->engine_type);

    AMF_FRC_ASSIGN_PROPERTY_INT64_CHECK(avctx, amf_filter, AMF_FRC_ENABLE_FALLBACK, frc_ctx->fallback);

    AMF_FRC_ASSIGN_PROPERTY_INT64_CHECK(avctx, amf_filter, AMF_FRC_INDICATOR, frc_ctx->indicator);

    AMF_FRC_ASSIGN_PROPERTY_INT64_CHECK(avctx, amf_filter, AMF_FRC_PROFILE, frc_ctx->profile);

    AMF_FRC_ASSIGN_PROPERTY_INT64_CHECK(avctx, amf_filter, AMF_FRC_MV_SEARCH_MODE, frc_ctx->mv_search_mode);

    AMF_FRC_ASSIGN_PROPERTY_INT64_CHECK(avctx, amf_filter, AMF_FRC_USE_FUTURE_FRAME, frc_ctx->use_future_frame);

    res = AMF_IFACE_CALL(amf_filter, Init, av_av_to_amf_format(in_format), inlink->w, inlink->h);
    AMF_RETURN_IF_FALSE(avctx, res == AMF_OK, AVERROR_UNKNOWN, "AMFFRC->Init() failed with error %d\n", res);

    return 0;
}

#define OFFSET(x) offsetof(AMFFRCFilterContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
static const AVOption frc_amf_options[] = {
    { "engine_type",    "Engine type",   OFFSET(engine_type), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, FRC_ENGINE_DX11, .flags = FLAGS, "engine_type" },
    { "dx11",           "DirectX 11",    0,  AV_OPT_TYPE_CONST,   { .i64 = FRC_ENGINE_DX11 }, 0, 0, FLAGS, "engine_type" },
    { "dx12",           "DirectX 12",    0,  AV_OPT_TYPE_CONST,   { .i64 = FRC_ENGINE_DX12 }, 0, 0, FLAGS, "engine_type" },

    { "enable",         "Enable FRC", OFFSET(enable), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, .flags = FLAGS },

    { "fallback_mode",  "Fallback behavior in case of low interpolation confidence", OFFSET(fallback), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 1, .flags = FLAGS, "fallback_mode" },
    { "duplicate",      "Duplicate frame",           0,  AV_OPT_TYPE_CONST, { .i64 = 0 }, 0, 0, FLAGS, "fallback_mode" },
    { "blend",          "Blend two frames together", 0,  AV_OPT_TYPE_CONST, { .i64 = 1 }, 0, 0, FLAGS, "fallback_mode" },

    { "indicator",      "Show FRC indicator square in the top left corner of the video.", OFFSET(indicator), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, .flags = FLAGS },

    { "profile",        "Level of hierarchical motion search",  OFFSET(profile), AV_OPT_TYPE_INT, { .i64 = FRC_PROFILE_HIGH }, FRC_PROFILE_LOW, FRC_PROFILE_SUPER, FLAGS, "profile" },
    { "low",            "Less levels of hierarchical motion search. "
                        "Only recommended for extremely low resolutions.",  0,  AV_OPT_TYPE_CONST, { .i64 = FRC_PROFILE_LOW },    0, 0, FLAGS, "profile" },
    { "high",           "Recommended for any resolution up to 1440p.",      0,  AV_OPT_TYPE_CONST, { .i64 = FRC_PROFILE_HIGH },   0, 0, FLAGS, "profile" },
    { "super",          "More levels of hierarchical motion search. "
                        "Recommended for resolutions 1440p or higher.",     0,  AV_OPT_TYPE_CONST, { .i64 = FRC_PROFILE_SUPER },  0, 0, FLAGS, "profile" },

    { "mv_search_mode", "Performance mode of the motion search",  OFFSET(mv_search_mode), AV_OPT_TYPE_INT,   { .i64 = FRC_MV_SEARCH_NATIVE }, FRC_MV_SEARCH_NATIVE, FRC_MV_SEARCH_PERFORMANCE, FLAGS, "mv_search_mode" },
    { "native",         "Conduct motion search on the full resolution of source images.",  0, AV_OPT_TYPE_CONST, { .i64 = FRC_MV_SEARCH_NATIVE },       0, 0, FLAGS, "mv_search_mode" },
    { "performance",    "Conduct motion search on the down scaled source images. "
                        "Recommended for APU or low end GPU for better performance.",      0, AV_OPT_TYPE_CONST, { .i64 = FRC_MV_SEARCH_PERFORMANCE },  0, 0, FLAGS, "mv_search_mode" },

    { "use_future_frame", "Enable dependency on future frame, improves quality for the cost of latency", OFFSET(use_future_frame), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, .flags = FLAGS },

    { NULL },
};

AVFILTER_DEFINE_CLASS(frc_amf);

static int amf_frc_filter_avframe(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext     *avctx = inlink->dst;
    AMFFRCFilterContext *frc_ctx = avctx->priv;
    AMFFilterContext *amf_ctx = &frc_ctx->common;
    AMFComponent     *amf_filter = amf_ctx->component;
    AVFilterLink     *outlink = avctx->outputs[0];
    AMFSurface       *surface_out = NULL;
    AMFSurface       *surface_in = NULL;
    FilterLink       *il = ff_filter_link(inlink);
    FilterLink       *ol = ff_filter_link(outlink);
    AMF_RESULT       res = AMF_FAIL;
    AMFData          *data_out = NULL;
    AVFrame          *out = NULL;
    int              ret = 0;

    if (!amf_filter)
        return AVERROR(EINVAL);

    ret = amf_avframe_to_amfsurface(avctx, in, &surface_in);
    if (ret < 0)
        goto fail;

    if (frc_ctx->enable) {
        AMF_ASSIGN_PROPERTY_INT64(res, amf_filter, AMF_FRC_MODE, FRC_x2_PRESENT);
        ol->frame_rate.num = il->frame_rate.num * 2;
    } else {
        AMF_ASSIGN_PROPERTY_INT64(res, amf_filter, AMF_FRC_MODE, FRC_OFF);
        ol->frame_rate.num = il->frame_rate.num;
    }
    AMF_GOTO_FAIL_IF_FALSE(avctx, res == AMF_OK, AVERROR_UNKNOWN, "SubmitInput(): Failed to %s FRC, error:%d\n", frc_ctx->enable ? "enable" : "disable", res);

    res = AMF_IFACE_CALL(amf_filter, SubmitInput, (AMFData*)surface_in);
    AMF_IFACE_CALL(surface_in, Release);
    surface_in = NULL;
    AMF_GOTO_FAIL_IF_FALSE(avctx, (res == AMF_OK || res == AMF_INPUT_FULL), AVERROR_UNKNOWN, "SubmitInput() failed with error %d\n", res);

    while (true) {
        res = AMF_IFACE_CALL(amf_filter, QueryOutput, &data_out);

        AMF_GOTO_FAIL_IF_FALSE(avctx, (res == AMF_OK || res == AMF_REPEAT), AVERROR_UNKNOWN, "QueryOutput() failed with error %d\n", res);
        if (data_out == NULL)
            break;

        AMFGuid guid = IID_AMFSurface();
        res = AMF_IFACE_CALL(data_out, QueryInterface, &guid, (void**)&surface_out);
        AMF_IFACE_CALL(data_out, Release);
        data_out = NULL;
        AMF_RETURN_IF_FALSE(avctx, res == AMF_OK, AVERROR_UNKNOWN, "QueryInterface(IID_AMFSurface) failed with error %d\n", res);

        out = amf_amfsurface_to_avframe(avctx, surface_out);
        AMF_GOTO_FAIL_IF_FALSE(avctx, out != NULL, AVERROR(ENOMEM), "Failed to convert AMFSurface to AVFrame\n");

        ret = av_frame_copy_props(out, in);
        AMF_GOTO_FAIL_IF_FALSE(avctx, ret >= 0, AVERROR(ENOMEM), "Failed to copy frame properties\n");

        out->pts = AMF_IFACE_CALL(surface_out, GetPts);

        if (frc_ctx->enable)
            out->duration /= 2;

        out->hw_frames_ctx = av_buffer_ref(amf_ctx->hwframes_out_ref);
        if (!out->hw_frames_ctx) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        ret = ff_filter_frame(outlink, out);
        out = NULL;
        if (ret < 0)
            goto fail;
    }

fail:
    av_frame_unref(in);
    av_frame_free(&in);
    if (out != NULL)
        av_frame_free(&out);

    return ret;
}

static const AVFilterPad amf_filter_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = amf_frc_filter_avframe,
    }
};


static const AVFilterPad amf_filter_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = amf_frc_filter_config_output,
    }
};

FFFilter ff_vf_frc_amf = {
    .p.name        = "frc_amf",
    .p.description = NULL_IF_CONFIG_SMALL("AMF video Frame Rate Converter"),
    .p.priv_class  = &frc_amf_class,
    .p.flags       = AVFILTER_FLAG_HWDEVICE,
    .priv_size     = sizeof(AMFFRCFilterContext),
    .init          = amf_frc_init,
    .uninit        = amf_filter_uninit,
    FILTER_INPUTS(amf_filter_inputs),
    FILTER_OUTPUTS(amf_filter_outputs),
    FILTER_QUERY_FUNC(amf_filter_query_formats),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
