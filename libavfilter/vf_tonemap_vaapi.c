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
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/mastering_display_metadata.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "vaapi_vpp.h"

typedef struct HDRVAAPIContext {
    VAAPIVPPContext vpp_ctx; // must be the first field

    char *output_format_string;

    char *color_primaries_string;
    char *color_transfer_string;
    char *color_matrix_string;

    enum AVColorPrimaries color_primaries;
    enum AVColorTransferCharacteristic color_transfer;
    enum AVColorSpace color_matrix;

    VAHdrMetaDataHDR10  in_metadata;

    AVFrameSideData    *src_display;
    AVFrameSideData    *src_light;
} HDRVAAPIContext;

static int tonemap_vaapi_save_metadata(AVFilterContext *avctx, AVFrame *input_frame)
{
    HDRVAAPIContext *ctx = avctx->priv;
    AVMasteringDisplayMetadata *hdr_meta;
    AVContentLightMetadata *light_meta;

    if (input_frame->color_trc != AVCOL_TRC_SMPTE2084) {
        av_log(avctx, AV_LOG_WARNING, "Only support HDR10 as input for vaapi tone-mapping\n");
    }

    ctx->src_display = av_frame_get_side_data(input_frame,
                                              AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
    if (ctx->src_display) {
        hdr_meta = (AVMasteringDisplayMetadata *)ctx->src_display->data;
        if (!hdr_meta) {
            av_log(avctx, AV_LOG_ERROR, "No mastering display data\n");
            return AVERROR(EINVAL);
        }

        if (hdr_meta->has_luminance) {
            const int luma_den = 10000;
            ctx->in_metadata.max_display_mastering_luminance =
                lrint(luma_den * av_q2d(hdr_meta->max_luminance));
            ctx->in_metadata.min_display_mastering_luminance =
                FFMIN(lrint(luma_den * av_q2d(hdr_meta->min_luminance)),
                      ctx->in_metadata.max_display_mastering_luminance);

            av_log(avctx, AV_LOG_DEBUG,
                   "Mastering Display Metadata(in luminance):\n");
            av_log(avctx, AV_LOG_DEBUG,
                   "min_luminance=%u, max_luminance=%u\n",
                   ctx->in_metadata.min_display_mastering_luminance,
                   ctx->in_metadata.max_display_mastering_luminance);
        }

        if (hdr_meta->has_primaries) {
            int i;
            const int mapping[3] = {1, 2, 0};  //green, blue, red
            const int chroma_den = 50000;

            for (i = 0; i < 3; i++) {
                const int j = mapping[i];
                ctx->in_metadata.display_primaries_x[i] =
                    FFMIN(lrint(chroma_den *
                                av_q2d(hdr_meta->display_primaries[j][0])),
                          chroma_den);
                ctx->in_metadata.display_primaries_y[i] =
                    FFMIN(lrint(chroma_den *
                                av_q2d(hdr_meta->display_primaries[j][1])),
                          chroma_den);
            }

            ctx->in_metadata.white_point_x =
                FFMIN(lrint(chroma_den * av_q2d(hdr_meta->white_point[0])),
                      chroma_den);
            ctx->in_metadata.white_point_y =
                FFMIN(lrint(chroma_den * av_q2d(hdr_meta->white_point[1])),
                      chroma_den);

            av_log(avctx, AV_LOG_DEBUG,
                   "Mastering Display Metadata(in primaries):\n");
            av_log(avctx, AV_LOG_DEBUG,
                   "G(%u,%u) B(%u,%u) R(%u,%u) WP(%u,%u)\n",
                   ctx->in_metadata.display_primaries_x[0],
                   ctx->in_metadata.display_primaries_y[0],
                   ctx->in_metadata.display_primaries_x[1],
                   ctx->in_metadata.display_primaries_y[1],
                   ctx->in_metadata.display_primaries_x[2],
                   ctx->in_metadata.display_primaries_y[2],
                   ctx->in_metadata.white_point_x,
                   ctx->in_metadata.white_point_y);
        }
    } else {
        av_log(avctx, AV_LOG_ERROR, "No mastering display data from input\n");
        return AVERROR(EINVAL);
    }

    ctx->src_light = av_frame_get_side_data(input_frame,
                                            AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
    if (ctx->src_light) {
        light_meta = (AVContentLightMetadata *)ctx->src_light->data;
        if (!light_meta) {
            av_log(avctx, AV_LOG_ERROR, "No light metadata\n");
            return AVERROR(EINVAL);
        }

        ctx->in_metadata.max_content_light_level = light_meta->MaxCLL;
        ctx->in_metadata.max_pic_average_light_level = light_meta->MaxFALL;

        av_log(avctx, AV_LOG_DEBUG,
               "Mastering Content Light Level (in):\n");
        av_log(avctx, AV_LOG_DEBUG,
               "MaxCLL(%u) MaxFALL(%u)\n",
               ctx->in_metadata.max_content_light_level,
               ctx->in_metadata.max_pic_average_light_level);
    } else {
        av_log(avctx, AV_LOG_DEBUG, "No content light level from input\n");
    }
    return 0;
}

static int tonemap_vaapi_set_filter_params(AVFilterContext *avctx, AVFrame *input_frame)
{
    VAAPIVPPContext *vpp_ctx   = avctx->priv;
    HDRVAAPIContext *ctx       = avctx->priv;
    VAStatus vas;
    VAProcFilterParameterBufferHDRToneMapping *hdrtm_param;

    vas = vaMapBuffer(vpp_ctx->hwctx->display, vpp_ctx->filter_buffers[0],
                      (void**)&hdrtm_param);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to map "
               "buffer (%d): %d (%s).\n",
               vpp_ctx->filter_buffers[0], vas, vaErrorStr(vas));
        return AVERROR(EIO);
    }

    memcpy(hdrtm_param->data.metadata, &ctx->in_metadata, sizeof(VAHdrMetaDataHDR10));

    vas = vaUnmapBuffer(vpp_ctx->hwctx->display, vpp_ctx->filter_buffers[0]);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to unmap output buffers: "
               "%d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR(EIO);
    }

    return 0;
}

static int tonemap_vaapi_build_filter_params(AVFilterContext *avctx)
{
    VAAPIVPPContext *vpp_ctx   = avctx->priv;
    HDRVAAPIContext *ctx       = avctx->priv;
    VAStatus vas;
    VAProcFilterParameterBufferHDRToneMapping hdrtm_param;
    VAProcFilterCapHighDynamicRange hdr_cap[VAProcHighDynamicRangeMetadataTypeCount];
    int num_query_caps;
    int i;

    memset(&hdrtm_param, 0, sizeof(hdrtm_param));
    memset(&ctx->in_metadata, 0, sizeof(ctx->in_metadata));

    num_query_caps = VAProcHighDynamicRangeMetadataTypeCount;
    vas = vaQueryVideoProcFilterCaps(vpp_ctx->hwctx->display,
                                     vpp_ctx->va_context,
                                     VAProcFilterHighDynamicRangeToneMapping,
                                     &hdr_cap, &num_query_caps);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query HDR caps "
               "context: %d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR(EIO);
    }

    for (i = 0; i < num_query_caps; i++) {
        if (hdr_cap[i].metadata_type != VAProcHighDynamicRangeMetadataNone)
            break;
    }

    if (i >= num_query_caps) {
        av_log(avctx, AV_LOG_ERROR, "VAAPI driver doesn't support HDR\n");
        return AVERROR(EINVAL);
    }

    for (i = 0; i < num_query_caps; i++) {
        if (VA_TONE_MAPPING_HDR_TO_SDR & hdr_cap[i].caps_flag)
            break;
    }

    if (i >= num_query_caps) {
        av_log(avctx, AV_LOG_ERROR,
               "VAAPI driver doesn't support HDR to SDR\n");
        return AVERROR(EINVAL);
    }

    hdrtm_param.type = VAProcFilterHighDynamicRangeToneMapping;
    hdrtm_param.data.metadata_type = VAProcHighDynamicRangeMetadataHDR10;
    hdrtm_param.data.metadata      = &ctx->in_metadata;
    hdrtm_param.data.metadata_size = sizeof(VAHdrMetaDataHDR10);

    return ff_vaapi_vpp_make_param_buffers(avctx,
                                           VAProcFilterParameterBufferType,
                                           &hdrtm_param, sizeof(hdrtm_param), 1);
}

static int tonemap_vaapi_filter_frame(AVFilterLink *inlink, AVFrame *input_frame)
{
    AVFilterContext *avctx     = inlink->dst;
    AVFilterLink *outlink      = avctx->outputs[0];
    VAAPIVPPContext *vpp_ctx   = avctx->priv;
    HDRVAAPIContext *ctx       = avctx->priv;
    AVFrame *output_frame      = NULL;
    VASurfaceID input_surface, output_surface;

    VAProcPipelineParameterBuffer params;
    int err;

    av_log(avctx, AV_LOG_DEBUG, "Filter input: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(input_frame->format),
           input_frame->width, input_frame->height, input_frame->pts);

    if (vpp_ctx->va_context == VA_INVALID_ID){
        av_frame_free(&input_frame);
        return AVERROR(EINVAL);
    }

    err = tonemap_vaapi_save_metadata(avctx, input_frame);
    if (err < 0)
        goto fail;

    err = tonemap_vaapi_set_filter_params(avctx, input_frame);
    if (err < 0)
        goto fail;

    input_surface = (VASurfaceID)(uintptr_t)input_frame->data[3];
    av_log(avctx, AV_LOG_DEBUG, "Using surface %#x for tonemap vpp input.\n",
           input_surface);

    output_frame = ff_get_video_buffer(outlink, vpp_ctx->output_width,
                                       vpp_ctx->output_height);
    if (!output_frame) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    output_surface = (VASurfaceID)(uintptr_t)output_frame->data[3];
    av_log(avctx, AV_LOG_DEBUG, "Using surface %#x for tonemap vpp output.\n",
           output_surface);
    memset(&params, 0, sizeof(params));

    err = av_frame_copy_props(output_frame, input_frame);
    if (err < 0)
        goto fail;

    if (ctx->color_primaries != AVCOL_PRI_UNSPECIFIED)
        output_frame->color_primaries = ctx->color_primaries;

    if (ctx->color_transfer != AVCOL_TRC_UNSPECIFIED)
        output_frame->color_trc = ctx->color_transfer;
    else
        output_frame->color_trc = AVCOL_TRC_BT709;

    if (ctx->color_matrix != AVCOL_SPC_UNSPECIFIED)
        output_frame->colorspace = ctx->color_matrix;

    err = ff_vaapi_vpp_init_params(avctx, &params,
                                   input_frame, output_frame);
    if (err < 0)
        goto fail;

    err = ff_vaapi_vpp_render_picture(avctx, &params, output_frame);
    if (err < 0)
        goto fail;

    av_frame_free(&input_frame);

    av_log(avctx, AV_LOG_DEBUG, "Filter output: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(output_frame->format),
           output_frame->width, output_frame->height, output_frame->pts);

    return ff_filter_frame(outlink, output_frame);

fail:
    av_frame_free(&input_frame);
    av_frame_free(&output_frame);
    return err;
}

static av_cold int tonemap_vaapi_init(AVFilterContext *avctx)
{
    VAAPIVPPContext *vpp_ctx = avctx->priv;
    HDRVAAPIContext *ctx     = avctx->priv;

    ff_vaapi_vpp_ctx_init(avctx);
    vpp_ctx->build_filter_params = tonemap_vaapi_build_filter_params;
    vpp_ctx->pipeline_uninit = ff_vaapi_vpp_pipeline_uninit;

    if (ctx->output_format_string) {
        vpp_ctx->output_format = av_get_pix_fmt(ctx->output_format_string);
        switch (vpp_ctx->output_format) {
        case AV_PIX_FMT_NV12:
        case AV_PIX_FMT_P010:
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Invalid output format.\n");
            return AVERROR(EINVAL);
        }
    } else {
        vpp_ctx->output_format = AV_PIX_FMT_NV12;
        av_log(avctx, AV_LOG_WARNING, "Output format not set, use default format NV12\n");
    }

#define STRING_OPTION(var_name, func_name, default_value) do { \
        if (ctx->var_name ## _string) { \
            int var = av_ ## func_name ## _from_name(ctx->var_name ## _string); \
            if (var < 0) { \
                av_log(avctx, AV_LOG_ERROR, "Invalid %s.\n", #var_name); \
                return AVERROR(EINVAL); \
            } \
            ctx->var_name = var; \
        } else { \
            ctx->var_name = default_value; \
        } \
    } while (0)

    STRING_OPTION(color_primaries, color_primaries, AVCOL_PRI_UNSPECIFIED);
    STRING_OPTION(color_transfer,  color_transfer,  AVCOL_TRC_UNSPECIFIED);
    STRING_OPTION(color_matrix,    color_space,     AVCOL_SPC_UNSPECIFIED);

    return 0;
}

#define OFFSET(x) offsetof(HDRVAAPIContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM)
static const AVOption tonemap_vaapi_options[] = {
    { "format", "Output pixel format set", OFFSET(output_format_string), AV_OPT_TYPE_STRING, .flags = FLAGS, "format" },
    { "matrix", "Output color matrix coefficient set",
      OFFSET(color_matrix_string), AV_OPT_TYPE_STRING,
      { .str = NULL }, .flags = FLAGS, "matrix" },
    { "m",      "Output color matrix coefficient set",
      OFFSET(color_matrix_string), AV_OPT_TYPE_STRING,
      { .str = NULL }, .flags = FLAGS, "matrix" },
    { "primaries", "Output color primaries set",
      OFFSET(color_primaries_string), AV_OPT_TYPE_STRING,
      { .str = NULL }, .flags = FLAGS, "primaries" },
    { "p",         "Output color primaries set",
      OFFSET(color_primaries_string), AV_OPT_TYPE_STRING,
      { .str = NULL }, .flags = FLAGS, "primaries" },
    { "transfer", "Output color transfer characteristics set",
      OFFSET(color_transfer_string),  AV_OPT_TYPE_STRING,
      { .str = NULL }, .flags = FLAGS, "transfer" },
    { "t",        "Output color transfer characteristics set",
      OFFSET(color_transfer_string),  AV_OPT_TYPE_STRING,
      { .str = NULL }, .flags = FLAGS, "transfer" },
    { NULL }
};


AVFILTER_DEFINE_CLASS(tonemap_vaapi);

static const AVFilterPad tonemap_vaapi_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &tonemap_vaapi_filter_frame,
        .config_props = &ff_vaapi_vpp_config_input,
    },
    { NULL }
};

static const AVFilterPad tonemap_vaapi_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &ff_vaapi_vpp_config_output,
    },
    { NULL }
};

AVFilter ff_vf_tonemap_vaapi = {
    .name           = "tonemap_vaapi",
    .description    = NULL_IF_CONFIG_SMALL("VAAPI VPP for tone-mapping"),
    .priv_size      = sizeof(HDRVAAPIContext),
    .init           = &tonemap_vaapi_init,
    .uninit         = &ff_vaapi_vpp_ctx_uninit,
    .query_formats  = &ff_vaapi_vpp_query_formats,
    .inputs         = tonemap_vaapi_inputs,
    .outputs        = tonemap_vaapi_outputs,
    .priv_class     = &tonemap_vaapi_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
