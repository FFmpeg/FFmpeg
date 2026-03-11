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
 * VPP video filter with AMF hardware acceleration
 */

#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/internal.h"

#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_amf.h"
#include "libavutil/hwcontext_amf_internal.h"

#include "AMF/components/VideoConverter.h"
#include "vf_amf_common.h"

#include "avfilter.h"
#include "scale_eval.h"
#include "avfilter_internal.h"

#if CONFIG_DXVA2
#include <d3d9.h>
#endif

#if CONFIG_D3D11VA
#include <d3d11.h>
#endif

static int amf_filter_query_formats(AVFilterContext *avctx)
{
    const enum AVPixelFormat *output_pix_fmts;
    static const enum AVPixelFormat input_pix_fmts[] = {
        AV_PIX_FMT_AMF_SURFACE,
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_P010,
        AV_PIX_FMT_0RGB,
        AV_PIX_FMT_BGR0,
        AV_PIX_FMT_BGRA,
        AV_PIX_FMT_RGB0,
        AV_PIX_FMT_RGBA,
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV420P10,
        AV_PIX_FMT_YUYV422,
        AV_PIX_FMT_NONE,
    };
    static const enum AVPixelFormat output_pix_fmts_default[] = {
        AV_PIX_FMT_AMF_SURFACE,
        AV_PIX_FMT_D3D11,
        AV_PIX_FMT_DXVA2_VLD,
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_BGRA,
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_NONE,
    };
    output_pix_fmts = output_pix_fmts_default;

    return amf_setup_input_output_formats(avctx, input_pix_fmts, output_pix_fmts);
}

static int amf_filter_config_output(AVFilterLink *outlink)
{
    AVFilterContext   *avctx = outlink->src;
    AVFilterLink      *inlink = avctx->inputs[0];
    AVHWFramesContext *hwframes_out = NULL;
    AMFFilterContext  *ctx = avctx->priv;
    AMFBuffer         *hdrmeta_buffer = NULL;
    AMFHDRMetadata    *hdrmeta = NULL;
    AMFSize out_size;
    size_t size = 0;
    int ret;
    AMF_RESULT res;
    enum AMF_VIDEO_CONVERTER_COLOR_PROFILE_ENUM amf_color_profile;
    enum AVPixelFormat in_format;
    const int chroma_den = 50000;
    const int luma_den = 10000;
    const int total_max_cll_args = 2;
    const int total_disp_meta_args = 10;

    ret = amf_init_filter_config(outlink, &in_format);
    if (ret < 0)
        return ret;
    // FIXME: add checks whether we have HW context
    hwframes_out = (AVHWFramesContext*)ctx->hwframes_out_ref->data;
    res = ctx->amf_device_ctx->factory->pVtbl->CreateComponent(ctx->amf_device_ctx->factory, ctx->amf_device_ctx->context, AMFVideoConverter, &ctx->component);
    AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_FILTER_NOT_FOUND, "CreateComponent(%ls) failed with error %d\n", AMFVideoConverter, res);
    // FIXME: add checks whether we have HW context
    AMF_ASSIGN_PROPERTY_INT64(res, ctx->component, AMF_VIDEO_CONVERTER_OUTPUT_FORMAT, (amf_int32)av_av_to_amf_format(hwframes_out->sw_format));
    AMF_RETURN_IF_FALSE(avctx, res == AMF_OK, AVERROR_UNKNOWN, "AMFConverter-SetProperty() failed with error %d\n", res);

    out_size.width = outlink->w;
    out_size.height = outlink->h;
    AMF_ASSIGN_PROPERTY_SIZE(res, ctx->component, AMF_VIDEO_CONVERTER_OUTPUT_SIZE, out_size);
    AMF_RETURN_IF_FALSE(avctx, res == AMF_OK, AVERROR_UNKNOWN, "AMFConverter-SetProperty() failed with error %d\n", res);

    AMF_ASSIGN_PROPERTY_INT64(res, ctx->component, AMF_VIDEO_CONVERTER_SCALE, (amf_int32)ctx->scale_type);
    AMF_RETURN_IF_FALSE(avctx, res == AMF_OK, AVERROR_UNKNOWN, "AMFConverter-SetProperty() failed with error %d\n", res);

    amf_color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_UNKNOWN;

    switch(ctx->color_profile) {
    case AMF_VIDEO_CONVERTER_COLOR_PROFILE_601:
        if (ctx->out_color_range == AMF_COLOR_RANGE_FULL) {
            amf_color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_601;
        } else {
            amf_color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_601;
        }
        break;
    case AMF_VIDEO_CONVERTER_COLOR_PROFILE_709:
        if (ctx->out_color_range == AMF_COLOR_RANGE_FULL) {
            amf_color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_709;
        } else {
            amf_color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_709;
        }
        break;
    case AMF_VIDEO_CONVERTER_COLOR_PROFILE_2020:
        if (ctx->out_color_range == AMF_COLOR_RANGE_FULL) {
            amf_color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_2020;
        } else {
            amf_color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_2020;
        }
        break;
    default:
        amf_color_profile = AMF_VIDEO_CONVERTER_COLOR_PROFILE_UNKNOWN;
        break;
    }

    if (ctx->in_color_range != AMF_COLOR_RANGE_UNDEFINED) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->component, AMF_VIDEO_CONVERTER_INPUT_COLOR_RANGE, ctx->in_color_range);
    }

    if (ctx->in_primaries != AMF_COLOR_PRIMARIES_UNDEFINED) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->component, AMF_VIDEO_CONVERTER_INPUT_COLOR_PRIMARIES, ctx->in_primaries);
    }

    if (ctx->in_trc != AMF_COLOR_TRANSFER_CHARACTERISTIC_UNDEFINED) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->component, AMF_VIDEO_CONVERTER_INPUT_TRANSFER_CHARACTERISTIC, ctx->in_trc);
    }

    if (ctx->disp_master) {
        ctx->master_display = av_mastering_display_metadata_alloc();
        if (!ctx->master_display)
            return AVERROR(ENOMEM);

        ret = sscanf_s(ctx->disp_master,
            "G(%hu,%hu)B(%hu,%hu)R(%hu,%hu)WP(%hu,%hu)L(%u,%u)",
            (uint16_t*)&ctx->master_display->display_primaries[1][0].num,
            (uint16_t*)&ctx->master_display->display_primaries[1][1].num,
            (uint16_t*)&ctx->master_display->display_primaries[2][0].num,
            (uint16_t*)&ctx->master_display->display_primaries[2][1].num,
            (uint16_t*)&ctx->master_display->display_primaries[0][0].num,
            (uint16_t*)&ctx->master_display->display_primaries[0][1].num,
            (uint16_t*)&ctx->master_display->white_point[0].num,
            (uint16_t*)&ctx->master_display->white_point[1].num,
            (unsigned*)&ctx->master_display->max_luminance.num,
            (unsigned*)&ctx->master_display->min_luminance.num
        );

        if (ret != total_disp_meta_args) {
            av_freep(&ctx->master_display);
            av_log(avctx, AV_LOG_ERROR, "failed to parse mastering_display option\n");
            return AVERROR(EINVAL);
        }

        ctx->master_display->display_primaries[1][0].den = chroma_den;
        ctx->master_display->display_primaries[1][1].den = chroma_den;
        ctx->master_display->display_primaries[2][0].den = chroma_den;
        ctx->master_display->display_primaries[2][1].den = chroma_den;
        ctx->master_display->display_primaries[0][0].den = chroma_den;
        ctx->master_display->display_primaries[0][1].den = chroma_den;
        ctx->master_display->white_point[0].den = chroma_den;
        ctx->master_display->white_point[1].den = chroma_den;
        ctx->master_display->max_luminance.den = luma_den;
        ctx->master_display->min_luminance.den = luma_den;

        ctx->master_display->has_primaries = 1;
        ctx->master_display->has_luminance = 1;
    }


    if (ctx->max_cll) {
        ctx->light_meta = av_content_light_metadata_alloc(&size);
        if (!ctx->light_meta)
            return AVERROR(ENOMEM);

        ret = sscanf_s(ctx->max_cll,
            "%hu,%hu",
            (uint16_t*)&ctx->light_meta->MaxCLL,
            (uint16_t*)&ctx->light_meta->MaxFALL
        );

        if (ret != total_max_cll_args) {
            av_freep(ctx->light_meta);
            ctx->light_meta = NULL;
            av_log(avctx, AV_LOG_ERROR, "failed to parse max_cll option\n");
            return AVERROR(EINVAL);
        }
    }

    if (ctx->light_meta || ctx->master_display) {
        if (ctx->in_trc == AVCOL_TRC_SMPTEST2084) {
            res = ctx->amf_device_ctx->context->pVtbl->AllocBuffer(ctx->amf_device_ctx->context, AMF_MEMORY_HOST, sizeof(AMFHDRMetadata), &hdrmeta_buffer);
            if (res == AMF_OK) {
                hdrmeta = (AMFHDRMetadata*)hdrmeta_buffer->pVtbl->GetNative(hdrmeta_buffer);

                av_amf_display_mastering_meta_to_hdrmeta(ctx->master_display, hdrmeta);
                av_amf_light_metadata_to_hdrmeta(ctx->light_meta, hdrmeta);

                AMF_ASSIGN_PROPERTY_INTERFACE(res, ctx->component, AMF_VIDEO_CONVERTER_INPUT_HDR_METADATA, hdrmeta_buffer);

                hdrmeta_buffer->pVtbl->Release(hdrmeta_buffer);
            }
        } else {
            av_log(avctx, AV_LOG_WARNING, "master_display/max_cll options are applicable only if in_trc is set to SMPTE2084\n");
        }
    }

    if (amf_color_profile != AMF_VIDEO_CONVERTER_COLOR_PROFILE_UNKNOWN) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->component, AMF_VIDEO_CONVERTER_COLOR_PROFILE, amf_color_profile);
    }

    if (ctx->out_color_range != AMF_COLOR_RANGE_UNDEFINED) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->component, AMF_VIDEO_CONVERTER_OUTPUT_COLOR_RANGE, ctx->out_color_range);
    }

    if (ctx->out_primaries != AMF_COLOR_PRIMARIES_UNDEFINED) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->component, AMF_VIDEO_CONVERTER_OUTPUT_COLOR_PRIMARIES, ctx->out_primaries);
    }

    if (ctx->out_trc != AMF_COLOR_TRANSFER_CHARACTERISTIC_UNDEFINED) {
        AMF_ASSIGN_PROPERTY_INT64(res, ctx->component, AMF_VIDEO_CONVERTER_OUTPUT_TRANSFER_CHARACTERISTIC, ctx->out_trc);
    }

    res = ctx->component->pVtbl->Init(ctx->component, av_av_to_amf_format(in_format), inlink->w, inlink->h);
    AMF_RETURN_IF_FALSE(avctx, res == AMF_OK, AVERROR_UNKNOWN, "AMFConverter-Init() failed with error %d\n", res);

    return 0;
}

#define OFFSET(x) offsetof(AMFFilterContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
static const AVOption vpp_amf_options[] = {
    { "w",              "Output video width",   OFFSET(w_expr),     AV_OPT_TYPE_STRING, { .str = "iw"   }, .flags = FLAGS },
    { "h",              "Output video height",  OFFSET(h_expr),     AV_OPT_TYPE_STRING, { .str = "ih"   }, .flags = FLAGS },
    { "format",         "Output pixel format",  OFFSET(format_str), AV_OPT_TYPE_STRING, { .str = "same" }, .flags = FLAGS },

    { "scale_type",     "Scale type",           OFFSET(scale_type),      AV_OPT_TYPE_INT,   { .i64 = AMF_VIDEO_CONVERTER_SCALE_BILINEAR }, AMF_VIDEO_CONVERTER_SCALE_BILINEAR, AMF_VIDEO_CONVERTER_SCALE_BICUBIC, FLAGS, "scale_type" },
    { "bilinear",       "Bilinear",         0,  AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_CONVERTER_SCALE_BILINEAR }, 0, 0, FLAGS, "scale_type" },
    { "bicubic",        "Bicubic",          0,  AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_CONVERTER_SCALE_BICUBIC },  0, 0, FLAGS, "scale_type" },

    { "color_profile",  "Color profile",        OFFSET(color_profile), AV_OPT_TYPE_INT,   { .i64 = AMF_VIDEO_CONVERTER_COLOR_PROFILE_UNKNOWN }, AMF_VIDEO_CONVERTER_COLOR_PROFILE_UNKNOWN, AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_2020, FLAGS, "color_profile" },
    { "bt601",          "BT.601",           0,  AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_CONVERTER_COLOR_PROFILE_601 }, 0, 0, FLAGS, "color_profile" },
    { "bt709",          "BT.709",           0,  AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_CONVERTER_COLOR_PROFILE_709 },  0, 0, FLAGS, "color_profile" },
    { "bt2020",         "BT.2020",          0,  AV_OPT_TYPE_CONST, { .i64 = AMF_VIDEO_CONVERTER_COLOR_PROFILE_2020 },  0, 0, FLAGS, "color_profile" },

    { "in_color_range",  "Input color range",        OFFSET(in_color_range),  AV_OPT_TYPE_INT, { .i64 = AMF_COLOR_RANGE_UNDEFINED }, AMF_COLOR_RANGE_UNDEFINED, AMF_COLOR_RANGE_FULL, FLAGS, "color_range" },
    { "out_color_range", "Output color range",       OFFSET(out_color_range), AV_OPT_TYPE_INT, { .i64 = AMF_COLOR_RANGE_UNDEFINED }, AMF_COLOR_RANGE_UNDEFINED, AMF_COLOR_RANGE_FULL, FLAGS, "color_range" },
    { "studio",          "Studio",                   0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_RANGE_STUDIO }, 0, 0, FLAGS, "color_range" },
    { "full",            "Full",                     0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_RANGE_FULL }, 0, 0, FLAGS, "color_range" },

    { "in_primaries",   "Input color primaries",    OFFSET(in_primaries),  AV_OPT_TYPE_INT, { .i64 = AMF_COLOR_PRIMARIES_UNDEFINED }, AMF_COLOR_PRIMARIES_UNDEFINED, AMF_COLOR_PRIMARIES_JEDEC_P22, FLAGS, "primaries" },
    { "out_primaries",  "Output color primaries",   OFFSET(out_primaries), AV_OPT_TYPE_INT, { .i64 = AMF_COLOR_PRIMARIES_UNDEFINED }, AMF_COLOR_PRIMARIES_UNDEFINED, AMF_COLOR_PRIMARIES_JEDEC_P22, FLAGS, "primaries" },
    { "bt709",          "BT.709",                   0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_PRIMARIES_BT709 }, 0, 0, FLAGS, "primaries" },
    { "bt470m",         "BT.470M",                  0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_PRIMARIES_BT470M }, 0, 0, FLAGS, "primaries" },
    { "bt470bg",        "BT.470BG",                 0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_PRIMARIES_BT470BG }, 0, 0, FLAGS, "primaries" },
    { "smpte170m",      "SMPTE170M",                0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_PRIMARIES_SMPTE170M }, 0, 0, FLAGS, "primaries" },
    { "smpte240m",      "SMPTE240M",                0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_PRIMARIES_SMPTE240M }, 0, 0, FLAGS, "primaries" },
    { "film",           "FILM",                     0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_PRIMARIES_FILM }, 0, 0, FLAGS, "primaries" },
    { "bt2020",         "BT2020",                   0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_PRIMARIES_BT2020 }, 0, 0, FLAGS, "primaries" },
    { "smpte428",       "SMPTE428",                 0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_PRIMARIES_SMPTE428 }, 0, 0, FLAGS, "primaries" },
    { "smpte431",       "SMPTE431",                 0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_PRIMARIES_SMPTE431 }, 0, 0, FLAGS, "primaries" },
    { "smpte432",       "SMPTE432",                 0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_PRIMARIES_SMPTE432 }, 0, 0, FLAGS, "primaries" },
    { "jedec-p22",      "JEDEC_P22",                0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_PRIMARIES_JEDEC_P22 }, 0, 0, FLAGS, "primaries" },

    { "in_trc",         "Input transfer characteristics",   OFFSET(in_trc),  AV_OPT_TYPE_INT, { .i64 = AMF_COLOR_TRANSFER_CHARACTERISTIC_UNDEFINED }, AMF_COLOR_TRANSFER_CHARACTERISTIC_UNDEFINED, AMF_COLOR_TRANSFER_CHARACTERISTIC_ARIB_STD_B67, FLAGS, "trc" },
    { "out_trc",        "Output transfer characteristics",  OFFSET(out_trc), AV_OPT_TYPE_INT, { .i64 = AMF_COLOR_TRANSFER_CHARACTERISTIC_UNDEFINED }, AMF_COLOR_TRANSFER_CHARACTERISTIC_UNDEFINED, AMF_COLOR_TRANSFER_CHARACTERISTIC_ARIB_STD_B67, FLAGS, "trc" },
    { "bt709",          "BT.709",                   0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_TRANSFER_CHARACTERISTIC_BT709 }, 0, 0, FLAGS, "trc" },
    { "gamma22",        "GAMMA22",                  0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_TRANSFER_CHARACTERISTIC_GAMMA22 }, 0, 0, FLAGS, "trc" },
    { "gamma28",        "GAMMA28",                  0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_TRANSFER_CHARACTERISTIC_GAMMA28 }, 0, 0, FLAGS, "trc" },
    { "smpte170m",      "SMPTE170M",                0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE170M }, 0, 0, FLAGS, "trc" },
    { "smpte240m",      "SMPTE240M",                0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE240M }, 0, 0, FLAGS, "trc" },
    { "linear",         "Linear",                   0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_TRANSFER_CHARACTERISTIC_LINEAR }, 0, 0, FLAGS, "trc" },
    { "log",            "LOG",                      0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_TRANSFER_CHARACTERISTIC_LOG }, 0, 0, FLAGS, "trc" },
    { "log-sqrt",       "LOG_SQRT",                 0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_TRANSFER_CHARACTERISTIC_LOG_SQRT }, 0, 0, FLAGS, "trc" },
    { "iec61966-2-4",   "IEC61966_2_4",             0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_TRANSFER_CHARACTERISTIC_IEC61966_2_4 }, 0, 0, FLAGS, "trc" },
    { "bt1361-ecg",     "BT1361_ECG",               0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_TRANSFER_CHARACTERISTIC_BT1361_ECG }, 0, 0, FLAGS, "trc" },
    { "iec61966-2-1",   "IEC61966_2_1",             0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_TRANSFER_CHARACTERISTIC_IEC61966_2_1 }, 0, 0, FLAGS, "trc" },
    { "bt2020-10",      "BT.2020_10",               0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_TRANSFER_CHARACTERISTIC_BT2020_10 }, 0, 0, FLAGS, "trc" },
    { "bt2020-12",      "BT.2020-12",               0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_TRANSFER_CHARACTERISTIC_BT2020_12 }, 0, 0, FLAGS, "trc" },
    { "smpte2084",      "SMPTE2084",                0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE2084 }, 0, 0, FLAGS, "trc" },
    { "smpte428",       "SMPTE428",                 0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE428 }, 0, 0, FLAGS, "trc" },
    { "arib-std-b67",   "ARIB_STD_B67",             0,  AV_OPT_TYPE_CONST, { .i64 = AMF_COLOR_TRANSFER_CHARACTERISTIC_ARIB_STD_B67 }, 0, 0, FLAGS, "trc" },

    { "force_original_aspect_ratio", "decrease or increase w/h if necessary to keep the original AR", OFFSET(force_original_aspect_ratio), AV_OPT_TYPE_INT, { .i64 = 0}, 0, SCALE_FORCE_OAR_NB-1, FLAGS, "force_oar" },
    { "disable",  NULL, 0, AV_OPT_TYPE_CONST, {.i64 = SCALE_FORCE_OAR_DISABLE  }, 0, 0, FLAGS, "force_oar" },
    { "decrease", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = SCALE_FORCE_OAR_DECREASE }, 0, 0, FLAGS, "force_oar" },
    { "increase", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = SCALE_FORCE_OAR_INCREASE }, 0, 0, FLAGS, "force_oar" },
    { "force_divisible_by", "enforce that the output resolution is divisible by a defined integer when force_original_aspect_ratio is used", OFFSET(force_divisible_by), AV_OPT_TYPE_INT, { .i64 = 1}, 1, 256, FLAGS },
    { "reset_sar", "reset SAR to 1 and scale to square pixels if scaling proportionally", OFFSET(reset_sar), AV_OPT_TYPE_BOOL, { .i64 = 0}, 0, 1, FLAGS },

    { "master_display",
      "set SMPTE2084 mastering display color volume info using libx265-style parameter string (G(%hu,%hu)B(%hu,%hu)R(%hu,%hu)WP(%hu,%hu)L(%u,%u)).",
       OFFSET(disp_master), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, FLAGS
    },

    { "max_cll", "set SMPTE2084 Max CLL and Max FALL values using libx265-style parameter string (%hu,%hu)", OFFSET(max_cll), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, FLAGS },

    { NULL },
};


AVFILTER_DEFINE_CLASS(vpp_amf);

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
        .config_props = amf_filter_config_output,
    }
};

FFFilter ff_vf_vpp_amf = {
    .p.name      = "vpp_amf",
    .p.description = NULL_IF_CONFIG_SMALL("AMF video scaling and format conversion"),
    .p.priv_class = &vpp_amf_class,
    .p.flags          = AVFILTER_FLAG_HWDEVICE,
    .priv_size = sizeof(AMFFilterContext),
    .init          = amf_filter_init,
    .uninit        = amf_filter_uninit,
    FILTER_INPUTS(amf_filter_inputs),
    FILTER_OUTPUTS(amf_filter_outputs),
    FILTER_QUERY_FUNC(amf_filter_query_formats),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
