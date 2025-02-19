/*
 * Direct3D 12 HW acceleration video encoder
 *
 * Copyright (c) 2024 Intel Corporation
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
#include "libavutil/opt.h"
#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/hwcontext_d3d12va_internal.h"

#include "avcodec.h"
#include "cbs.h"
#include "cbs_h265.h"
#include "hw_base_encode_h265.h"
#include "h2645data.h"
#include "h265_profile_level.h"
#include "codec_internal.h"
#include "d3d12va_encode.h"

typedef struct D3D12VAEncodeHEVCPicture {
    int pic_order_cnt;
    int64_t last_idr_frame;
} D3D12VAEncodeHEVCPicture;

typedef struct D3D12VAEncodeHEVCContext {
    D3D12VAEncodeContext common;

    // User options.
    int qp;
    int profile;
    int level;

    // Writer structures.
    FFHWBaseEncodeH265 units;
    FFHWBaseEncodeH265Opts unit_opts;

    CodedBitstreamContext *cbc;
    CodedBitstreamFragment current_access_unit;
} D3D12VAEncodeHEVCContext;

typedef struct D3D12VAEncodeHEVCLevel {
    int level;
    D3D12_VIDEO_ENCODER_LEVELS_HEVC d3d12_level;
} D3D12VAEncodeHEVCLevel;

static const D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC hevc_config_support_sets[] =
{
    {
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_NONE,
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_8x8,
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_32x32,
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_4x4,
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_32x32,
        3,
        3,
    },
    {
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_NONE,
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_8x8,
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_32x32,
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_4x4,
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_32x32,
        0,
        0,
    },
    {
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_NONE,
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_8x8,
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_32x32,
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_4x4,
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_32x32,
        2,
        2,
    },
    {
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_NONE,
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_8x8,
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_64x64,
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_4x4,
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_32x32,
        2,
        2,
    },
    {
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_NONE,
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_8x8,
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_64x64,
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_4x4,
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_32x32,
        4,
        4,
    },
};

static const D3D12VAEncodeHEVCLevel hevc_levels[] = {
    { 30,  D3D12_VIDEO_ENCODER_LEVELS_HEVC_1  },
    { 60,  D3D12_VIDEO_ENCODER_LEVELS_HEVC_2  },
    { 63,  D3D12_VIDEO_ENCODER_LEVELS_HEVC_21 },
    { 90,  D3D12_VIDEO_ENCODER_LEVELS_HEVC_3  },
    { 93,  D3D12_VIDEO_ENCODER_LEVELS_HEVC_31 },
    { 120, D3D12_VIDEO_ENCODER_LEVELS_HEVC_4  },
    { 123, D3D12_VIDEO_ENCODER_LEVELS_HEVC_41 },
    { 150, D3D12_VIDEO_ENCODER_LEVELS_HEVC_5  },
    { 153, D3D12_VIDEO_ENCODER_LEVELS_HEVC_51 },
    { 156, D3D12_VIDEO_ENCODER_LEVELS_HEVC_52 },
    { 180, D3D12_VIDEO_ENCODER_LEVELS_HEVC_6  },
    { 183, D3D12_VIDEO_ENCODER_LEVELS_HEVC_61 },
    { 186, D3D12_VIDEO_ENCODER_LEVELS_HEVC_62 },
};

static const D3D12_VIDEO_ENCODER_PROFILE_HEVC profile_main   = D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN;
static const D3D12_VIDEO_ENCODER_PROFILE_HEVC profile_main10 = D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN10;

#define D3D_PROFILE_DESC(name) \
    { sizeof(D3D12_VIDEO_ENCODER_PROFILE_HEVC), { .pHEVCProfile = (D3D12_VIDEO_ENCODER_PROFILE_HEVC *)&profile_ ## name } }
static const D3D12VAEncodeProfile d3d12va_encode_hevc_profiles[] = {
    { AV_PROFILE_HEVC_MAIN,     8, 3, 1, 1, D3D_PROFILE_DESC(main)   },
    { AV_PROFILE_HEVC_MAIN_10, 10, 3, 1, 1, D3D_PROFILE_DESC(main10) },
    { AV_PROFILE_UNKNOWN },
};

static uint8_t d3d12va_encode_hevc_map_cusize(D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE cusize)
{
    switch (cusize) {
        case D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_8x8:   return 8;
        case D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_16x16: return 16;
        case D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_32x32: return 32;
        case D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_64x64: return 64;
        default: av_assert0(0);
    }
    return 0;
}

static uint8_t d3d12va_encode_hevc_map_tusize(D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE tusize)
{
    switch (tusize) {
        case D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_4x4:   return 4;
        case D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_8x8:   return 8;
        case D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_16x16: return 16;
        case D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_32x32: return 32;
        default: av_assert0(0);
    }
    return 0;
}

static int d3d12va_encode_hevc_write_access_unit(AVCodecContext *avctx,
                                                 char *data, size_t *data_len,
                                                 CodedBitstreamFragment *au)
{
    D3D12VAEncodeHEVCContext *priv = avctx->priv_data;
    int err;

    err = ff_cbs_write_fragment_data(priv->cbc, au);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to write packed header.\n");
        return err;
    }

    if (*data_len < 8 * au->data_size - au->data_bit_padding) {
        av_log(avctx, AV_LOG_ERROR, "Access unit too large: "
               "%zu < %zu.\n", *data_len,
               8 * au->data_size - au->data_bit_padding);
        return AVERROR(ENOSPC);
    }

    memcpy(data, au->data, au->data_size);
    *data_len = 8 * au->data_size - au->data_bit_padding;

    return 0;
}

static int d3d12va_encode_hevc_add_nal(AVCodecContext *avctx,
                                       CodedBitstreamFragment *au,
                                       void *nal_unit)
{
    H265RawNALUnitHeader *header = nal_unit;
    int err;

    err = ff_cbs_insert_unit_content(au, -1,
                                     header->nal_unit_type, nal_unit, NULL);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to add NAL unit: "
               "type = %d.\n", header->nal_unit_type);
        return err;
    }

    return 0;
}

static int d3d12va_encode_hevc_write_sequence_header(AVCodecContext *avctx,
                                                     char *data, size_t *data_len)
{
    D3D12VAEncodeHEVCContext *priv = avctx->priv_data;
    CodedBitstreamFragment   *au   = &priv->current_access_unit;
    int err;

    err = d3d12va_encode_hevc_add_nal(avctx, au, &priv->units.raw_vps);
    if (err < 0)
        goto fail;

    err = d3d12va_encode_hevc_add_nal(avctx, au, &priv->units.raw_sps);
    if (err < 0)
        goto fail;

    err = d3d12va_encode_hevc_add_nal(avctx, au, &priv->units.raw_pps);
    if (err < 0)
        goto fail;

    err = d3d12va_encode_hevc_write_access_unit(avctx, data, data_len, au);
fail:
    ff_cbs_fragment_reset(au);
    return err;

}

static int d3d12va_encode_hevc_init_sequence_params(AVCodecContext *avctx)
{
    FFHWBaseEncodeContext *base_ctx = avctx->priv_data;
    D3D12VAEncodeContext      *ctx  = avctx->priv_data;
    D3D12VAEncodeHEVCContext  *priv = avctx->priv_data;
    AVD3D12VAFramesContext   *hwctx = base_ctx->input_frames->hwctx;
    H265RawSPS                *sps  = &priv->units.raw_sps;
    H265RawPPS                *pps  = &priv->units.raw_pps;
    H265RawVUI                *vui  = &sps->vui;
    D3D12_VIDEO_ENCODER_PROFILE_HEVC profile = D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN;
    D3D12_VIDEO_ENCODER_LEVEL_TIER_CONSTRAINTS_HEVC level = { 0 };
    const AVPixFmtDescriptor *desc;
    uint8_t min_cu_size, max_cu_size, min_tu_size, max_tu_size;
    HRESULT hr;
    int err;

    D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT support = {
        .NodeIndex                        = 0,
        .Codec                            = D3D12_VIDEO_ENCODER_CODEC_HEVC,
        .InputFormat                      = hwctx->format,
        .RateControl                      = ctx->rc,
        .IntraRefresh                     = D3D12_VIDEO_ENCODER_INTRA_REFRESH_MODE_NONE,
        .SubregionFrameEncoding           = D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_FULL_FRAME,
        .ResolutionsListCount             = 1,
        .pResolutionList                  = &ctx->resolution,
        .CodecGopSequence                 = ctx->gop,
        .MaxReferenceFramesInDPB          = MAX_DPB_SIZE - 1,
        .CodecConfiguration               = ctx->codec_conf,
        .SuggestedProfile.DataSize        = sizeof(D3D12_VIDEO_ENCODER_PROFILE_HEVC),
        .SuggestedProfile.pHEVCProfile    = &profile,
        .SuggestedLevel.DataSize          = sizeof(D3D12_VIDEO_ENCODER_LEVEL_TIER_CONSTRAINTS_HEVC),
        .SuggestedLevel.pHEVCLevelSetting = &level,
        .pResolutionDependentSupport      = &ctx->res_limits,
     };

    hr = ID3D12VideoDevice3_CheckFeatureSupport(ctx->video_device3, D3D12_FEATURE_VIDEO_ENCODER_SUPPORT,
                                                &support, sizeof(support));

    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to check encoder support(%lx).\n", (long)hr);
        return AVERROR(EINVAL);
    }

    if (!(support.SupportFlags & D3D12_VIDEO_ENCODER_SUPPORT_FLAG_GENERAL_SUPPORT_OK)) {
        av_log(avctx, AV_LOG_ERROR, "Driver does not support some request features. %#x\n",
               support.ValidationFlags);
        return AVERROR(EINVAL);
    }

    if (support.SupportFlags & D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RECONSTRUCTED_FRAMES_REQUIRE_TEXTURE_ARRAYS) {
        av_log(avctx, AV_LOG_ERROR, "D3D12 video encode on this device requires texture array support, "
               "but it's not implemented.\n");
        return AVERROR_PATCHWELCOME;
    }

    desc = av_pix_fmt_desc_get(base_ctx->input_frames->sw_format);
    av_assert0(desc);

    min_cu_size = d3d12va_encode_hevc_map_cusize(ctx->codec_conf.pHEVCConfig->MinLumaCodingUnitSize);
    max_cu_size = d3d12va_encode_hevc_map_cusize(ctx->codec_conf.pHEVCConfig->MaxLumaCodingUnitSize);
    min_tu_size = d3d12va_encode_hevc_map_tusize(ctx->codec_conf.pHEVCConfig->MinLumaTransformUnitSize);
    max_tu_size = d3d12va_encode_hevc_map_tusize(ctx->codec_conf.pHEVCConfig->MaxLumaTransformUnitSize);

    // cu_qp_delta always required to be 1 in https://github.com/microsoft/DirectX-Specs/blob/master/d3d/D3D12VideoEncoding.md
    priv->unit_opts.cu_qp_delta_enabled_flag = 1;
    priv->unit_opts.nb_slices = 1;

    err = ff_hw_base_encode_init_params_h265(base_ctx, avctx,
                                             &priv->units, &priv->unit_opts);
    if (err < 0)
        return err;

    avctx->level = priv->units.raw_vps.profile_tier_level.general_level_idc;

    av_assert0(ctx->res_limits.SubregionBlockPixelsSize % min_cu_size == 0);

    sps->pic_width_in_luma_samples  = FFALIGN(base_ctx->surface_width,
                                              ctx->res_limits.SubregionBlockPixelsSize);
    sps->pic_height_in_luma_samples = FFALIGN(base_ctx->surface_height,
                                              ctx->res_limits.SubregionBlockPixelsSize);

    if (avctx->width  != sps->pic_width_in_luma_samples ||
        avctx->height != sps->pic_height_in_luma_samples) {
        sps->conformance_window_flag = 1;
        sps->conf_win_left_offset   = 0;
        sps->conf_win_right_offset  =
            (sps->pic_width_in_luma_samples - avctx->width) >> desc->log2_chroma_w;
        sps->conf_win_top_offset    = 0;
        sps->conf_win_bottom_offset =
            (sps->pic_height_in_luma_samples - avctx->height) >> desc->log2_chroma_h;
    } else {
        sps->conformance_window_flag = 0;
    }

    sps->log2_max_pic_order_cnt_lsb_minus4 = ctx->gop.pHEVCGroupOfPictures->log2_max_pic_order_cnt_lsb_minus4;

    sps->log2_min_luma_coding_block_size_minus3      = (uint8_t)(av_log2(min_cu_size) - 3);
    sps->log2_diff_max_min_luma_coding_block_size    = (uint8_t)(av_log2(max_cu_size) - av_log2(min_cu_size));
    sps->log2_min_luma_transform_block_size_minus2   = (uint8_t)(av_log2(min_tu_size) - 2);
    sps->log2_diff_max_min_luma_transform_block_size = (uint8_t)(av_log2(max_tu_size) - av_log2(min_tu_size));

    sps->max_transform_hierarchy_depth_inter = ctx->codec_conf.pHEVCConfig->max_transform_hierarchy_depth_inter;
    sps->max_transform_hierarchy_depth_intra = ctx->codec_conf.pHEVCConfig->max_transform_hierarchy_depth_intra;

    sps->amp_enabled_flag = !!(ctx->codec_conf.pHEVCConfig->ConfigurationFlags &
                               D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_USE_ASYMETRIC_MOTION_PARTITION);
    sps->sample_adaptive_offset_enabled_flag = !!(ctx->codec_conf.pHEVCConfig->ConfigurationFlags &
                                                  D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_ENABLE_SAO_FILTER);

    pps->cabac_init_present_flag = 1;

    pps->init_qp_minus26 = 0;

    pps->transform_skip_enabled_flag = !!(ctx->codec_conf.pHEVCConfig->ConfigurationFlags &
                                          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_ENABLE_TRANSFORM_SKIPPING);

    pps->pps_slice_chroma_qp_offsets_present_flag = 1;

    pps->tiles_enabled_flag = 0; // no tiling in D3D12

    pps->pps_loop_filter_across_slices_enabled_flag = !(ctx->codec_conf.pHEVCConfig->ConfigurationFlags &
                                                        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_DISABLE_LOOP_FILTER_ACROSS_SLICES);

    pps->deblocking_filter_control_present_flag = 1;

    return 0;
}

static int d3d12va_encode_hevc_get_encoder_caps(AVCodecContext *avctx)
{
    int i;
    HRESULT hr;
    uint8_t min_cu_size, max_cu_size;
    FFHWBaseEncodeContext *base_ctx = avctx->priv_data;
    D3D12VAEncodeContext     *ctx = avctx->priv_data;
    D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC *config;
    D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC hevc_caps;

    D3D12_FEATURE_DATA_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT codec_caps = {
        .NodeIndex                   = 0,
        .Codec                       = D3D12_VIDEO_ENCODER_CODEC_HEVC,
        .Profile                     = ctx->profile->d3d12_profile,
        .CodecSupportLimits.DataSize = sizeof(D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC),
    };

    for (i = 0; i < FF_ARRAY_ELEMS(hevc_config_support_sets); i++) {
        hevc_caps = hevc_config_support_sets[i];
        codec_caps.CodecSupportLimits.pHEVCSupport = &hevc_caps;
        hr = ID3D12VideoDevice3_CheckFeatureSupport(ctx->video_device3, D3D12_FEATURE_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT,
                                                    &codec_caps, sizeof(codec_caps));
        if (SUCCEEDED(hr) && codec_caps.IsSupported)
            break;
    }

    if (i == FF_ARRAY_ELEMS(hevc_config_support_sets)) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported codec configuration\n");
        return AVERROR(EINVAL);
    }

    ctx->codec_conf.DataSize = sizeof(D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC);
    ctx->codec_conf.pHEVCConfig = av_mallocz(ctx->codec_conf.DataSize);
    if (!ctx->codec_conf.pHEVCConfig)
        return AVERROR(ENOMEM);

    config = ctx->codec_conf.pHEVCConfig;

    config->ConfigurationFlags                  = D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_NONE;
    config->MinLumaCodingUnitSize               = hevc_caps.MinLumaCodingUnitSize;
    config->MaxLumaCodingUnitSize               = hevc_caps.MaxLumaCodingUnitSize;
    config->MinLumaTransformUnitSize            = hevc_caps.MinLumaTransformUnitSize;
    config->MaxLumaTransformUnitSize            = hevc_caps.MaxLumaTransformUnitSize;
    config->max_transform_hierarchy_depth_inter = hevc_caps.max_transform_hierarchy_depth_inter;
    config->max_transform_hierarchy_depth_intra = hevc_caps.max_transform_hierarchy_depth_intra;

    if (hevc_caps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_ASYMETRIC_MOTION_PARTITION_SUPPORT ||
        hevc_caps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_ASYMETRIC_MOTION_PARTITION_REQUIRED)
        config->ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_USE_ASYMETRIC_MOTION_PARTITION;

    if (hevc_caps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_SAO_FILTER_SUPPORT)
        config->ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_ENABLE_SAO_FILTER;

    if (hevc_caps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_DISABLING_LOOP_FILTER_ACROSS_SLICES_SUPPORT)
        config->ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_DISABLE_LOOP_FILTER_ACROSS_SLICES;

    if (hevc_caps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_TRANSFORM_SKIP_SUPPORT)
        config->ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_ENABLE_TRANSFORM_SKIPPING;

    if (hevc_caps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_P_FRAMES_IMPLEMENTED_AS_LOW_DELAY_B_FRAMES)
        ctx->bi_not_empty = 1;

    // block sizes
    min_cu_size = d3d12va_encode_hevc_map_cusize(hevc_caps.MinLumaCodingUnitSize);
    max_cu_size = d3d12va_encode_hevc_map_cusize(hevc_caps.MaxLumaCodingUnitSize);

    av_log(avctx, AV_LOG_VERBOSE, "Using CTU size %dx%d, "
           "min CB size %dx%d.\n", max_cu_size, max_cu_size,
           min_cu_size, min_cu_size);

    base_ctx->surface_width  = FFALIGN(avctx->width,  min_cu_size);
    base_ctx->surface_height = FFALIGN(avctx->height, min_cu_size);

    return 0;
}

static int d3d12va_encode_hevc_configure(AVCodecContext *avctx)
{
    FFHWBaseEncodeContext  *base_ctx = avctx->priv_data;
    D3D12VAEncodeContext      *ctx = avctx->priv_data;
    D3D12VAEncodeHEVCContext *priv = avctx->priv_data;
    int fixed_qp_idr, fixed_qp_p, fixed_qp_b;
    int err;

    err = ff_cbs_init(&priv->cbc, AV_CODEC_ID_HEVC, avctx);
    if (err < 0)
        return err;

    // Rate control
    if (ctx->rc.Mode == D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP) {
        D3D12_VIDEO_ENCODER_RATE_CONTROL_CQP *cqp_ctl;
        fixed_qp_p = av_clip(ctx->rc_quality, 1, 51);
        if (avctx->i_quant_factor > 0.0)
            fixed_qp_idr = av_clip((avctx->i_quant_factor * fixed_qp_p +
                                    avctx->i_quant_offset) + 0.5, 1, 51);
        else
            fixed_qp_idr = fixed_qp_p;
        if (avctx->b_quant_factor > 0.0)
            fixed_qp_b = av_clip((avctx->b_quant_factor * fixed_qp_p +
                                  avctx->b_quant_offset) + 0.5, 1, 51);
        else
            fixed_qp_b = fixed_qp_p;

        av_log(avctx, AV_LOG_DEBUG, "Using fixed QP = "
               "%d / %d / %d for IDR- / P- / B-frames.\n",
               fixed_qp_idr, fixed_qp_p, fixed_qp_b);

        ctx->rc.ConfigParams.DataSize = sizeof(D3D12_VIDEO_ENCODER_RATE_CONTROL_CQP);
        cqp_ctl = av_mallocz(ctx->rc.ConfigParams.DataSize);
        if (!cqp_ctl)
            return AVERROR(ENOMEM);

        cqp_ctl->ConstantQP_FullIntracodedFrame                  = fixed_qp_idr;
        cqp_ctl->ConstantQP_InterPredictedFrame_PrevRefOnly      = fixed_qp_p;
        cqp_ctl->ConstantQP_InterPredictedFrame_BiDirectionalRef = fixed_qp_b;

        ctx->rc.ConfigParams.pConfiguration_CQP = cqp_ctl;
    }

    // GOP
    ctx->gop.DataSize = sizeof(D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE_HEVC);
    ctx->gop.pHEVCGroupOfPictures = av_mallocz(ctx->gop.DataSize);
    if (!ctx->gop.pHEVCGroupOfPictures)
        return AVERROR(ENOMEM);

    ctx->gop.pHEVCGroupOfPictures->GOPLength      = base_ctx->gop_size;
    ctx->gop.pHEVCGroupOfPictures->PPicturePeriod = base_ctx->b_per_p + 1;
    // Power of 2
    if (base_ctx->gop_size & base_ctx->gop_size - 1 == 0)
        ctx->gop.pHEVCGroupOfPictures->log2_max_pic_order_cnt_lsb_minus4 =
            FFMAX(av_log2(base_ctx->gop_size) - 4, 0);
    else
        ctx->gop.pHEVCGroupOfPictures->log2_max_pic_order_cnt_lsb_minus4 =
            FFMAX(av_log2(base_ctx->gop_size) - 3, 0);

    return 0;
}

static int d3d12va_encode_hevc_set_level(AVCodecContext *avctx)
{
    D3D12VAEncodeContext      *ctx = avctx->priv_data;
    D3D12VAEncodeHEVCContext *priv = avctx->priv_data;
    int i;

    ctx->level.DataSize = sizeof(D3D12_VIDEO_ENCODER_LEVEL_TIER_CONSTRAINTS_HEVC);
    ctx->level.pHEVCLevelSetting = av_mallocz(ctx->level.DataSize);
    if (!ctx->level.pHEVCLevelSetting)
        return AVERROR(ENOMEM);

    for (i = 0; i < FF_ARRAY_ELEMS(hevc_levels); i++) {
        if (avctx->level == hevc_levels[i].level) {
            ctx->level.pHEVCLevelSetting->Level = hevc_levels[i].d3d12_level;
            break;
        }
    }

    if (i == FF_ARRAY_ELEMS(hevc_levels)) {
        av_log(avctx, AV_LOG_ERROR, "Invalid level %d.\n", avctx->level);
        return AVERROR(EINVAL);
    }

    ctx->level.pHEVCLevelSetting->Tier = priv->units.raw_vps.profile_tier_level.general_tier_flag == 0 ?
                                         D3D12_VIDEO_ENCODER_TIER_HEVC_MAIN :
                                         D3D12_VIDEO_ENCODER_TIER_HEVC_HIGH;

    return 0;
}

static void d3d12va_encode_hevc_free_picture_params(D3D12VAEncodePicture *pic)
{
    if (!pic->pic_ctl.pHEVCPicData)
        return;

    av_freep(&pic->pic_ctl.pHEVCPicData->pList0ReferenceFrames);
    av_freep(&pic->pic_ctl.pHEVCPicData->pList1ReferenceFrames);
    av_freep(&pic->pic_ctl.pHEVCPicData->pReferenceFramesReconPictureDescriptors);
    av_freep(&pic->pic_ctl.pHEVCPicData);
}

static int d3d12va_encode_hevc_init_picture_params(AVCodecContext *avctx,
                                                   FFHWBaseEncodePicture *base_pic)
{
    D3D12VAEncodePicture                                 *pic = base_pic->priv;
    D3D12VAEncodeHEVCPicture                            *hpic = base_pic->codec_priv;
    FFHWBaseEncodePicture                               *prev = base_pic->prev;
    D3D12VAEncodeHEVCPicture                           *hprev = prev ? prev->codec_priv : NULL;
    D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_HEVC *pd = NULL;
    UINT                                           *ref_list0 = NULL, *ref_list1 = NULL;
    int i, idx = 0;

    pic->pic_ctl.DataSize = sizeof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC);
    pic->pic_ctl.pHEVCPicData = av_mallocz(pic->pic_ctl.DataSize);
    if (!pic->pic_ctl.pHEVCPicData)
        return AVERROR(ENOMEM);

    if (base_pic->type == FF_HW_PICTURE_TYPE_IDR) {
        av_assert0(base_pic->display_order == base_pic->encode_order);
        hpic->last_idr_frame = base_pic->display_order;
    } else {
        av_assert0(prev);
        hpic->last_idr_frame = hprev->last_idr_frame;
    }
    hpic->pic_order_cnt = base_pic->display_order - hpic->last_idr_frame;

    switch(base_pic->type) {
        case FF_HW_PICTURE_TYPE_IDR:
            pic->pic_ctl.pHEVCPicData->FrameType = D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_IDR_FRAME;
            break;
        case FF_HW_PICTURE_TYPE_I:
            pic->pic_ctl.pHEVCPicData->FrameType = D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_I_FRAME;
            break;
        case FF_HW_PICTURE_TYPE_P:
            pic->pic_ctl.pHEVCPicData->FrameType = D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_P_FRAME;
            break;
        case FF_HW_PICTURE_TYPE_B:
            pic->pic_ctl.pHEVCPicData->FrameType = D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_B_FRAME;
            break;
        default:
            av_assert0(0 && "invalid picture type");
    }

    pic->pic_ctl.pHEVCPicData->slice_pic_parameter_set_id = 0;
    pic->pic_ctl.pHEVCPicData->PictureOrderCountNumber    = hpic->pic_order_cnt;

    if (base_pic->type == FF_HW_PICTURE_TYPE_P || base_pic->type == FF_HW_PICTURE_TYPE_B) {
        pd = av_calloc(MAX_PICTURE_REFERENCES, sizeof(*pd));
        if (!pd)
            return AVERROR(ENOMEM);

        ref_list0 = av_calloc(MAX_PICTURE_REFERENCES, sizeof(*ref_list0));
        if (!ref_list0)
            return AVERROR(ENOMEM);

        pic->pic_ctl.pHEVCPicData->List0ReferenceFramesCount = base_pic->nb_refs[0];
        for (i = 0; i < base_pic->nb_refs[0]; i++) {
            FFHWBaseEncodePicture *ref = base_pic->refs[0][i];
            D3D12VAEncodeHEVCPicture *href;

            av_assert0(ref && ref->encode_order < base_pic->encode_order);
            href = ref->codec_priv;

            ref_list0[i] = idx;
            pd[idx].ReconstructedPictureResourceIndex = idx;
            pd[idx].IsRefUsedByCurrentPic = TRUE;
            pd[idx].PictureOrderCountNumber = href->pic_order_cnt;
            idx++;
        }
    }

    if (base_pic->type == FF_HW_PICTURE_TYPE_B) {
        ref_list1 = av_calloc(MAX_PICTURE_REFERENCES, sizeof(*ref_list1));
        if (!ref_list1)
            return AVERROR(ENOMEM);

        pic->pic_ctl.pHEVCPicData->List1ReferenceFramesCount = base_pic->nb_refs[1];
        for (i = 0; i < base_pic->nb_refs[1]; i++) {
            FFHWBaseEncodePicture *ref = base_pic->refs[1][i];
            D3D12VAEncodeHEVCPicture *href;

            av_assert0(ref && ref->encode_order < base_pic->encode_order);
            href = ref->codec_priv;

            ref_list1[i] = idx;
            pd[idx].ReconstructedPictureResourceIndex = idx;
            pd[idx].IsRefUsedByCurrentPic = TRUE;
            pd[idx].PictureOrderCountNumber = href->pic_order_cnt;
            idx++;
        }
    }

    pic->pic_ctl.pHEVCPicData->pList0ReferenceFrames = ref_list0;
    pic->pic_ctl.pHEVCPicData->pList1ReferenceFrames = ref_list1;
    pic->pic_ctl.pHEVCPicData->ReferenceFramesReconPictureDescriptorsCount = idx;
    pic->pic_ctl.pHEVCPicData->pReferenceFramesReconPictureDescriptors = pd;

    return 0;
}

static const D3D12VAEncodeType d3d12va_encode_type_hevc = {
    .profiles               = d3d12va_encode_hevc_profiles,

    .d3d12_codec            = D3D12_VIDEO_ENCODER_CODEC_HEVC,

    .flags                  = FF_HW_FLAG_B_PICTURES |
                              FF_HW_FLAG_B_PICTURE_REFERENCES |
                              FF_HW_FLAG_NON_IDR_KEY_PICTURES,

    .default_quality        = 25,

    .get_encoder_caps       = &d3d12va_encode_hevc_get_encoder_caps,

    .configure              = &d3d12va_encode_hevc_configure,

    .set_level              = &d3d12va_encode_hevc_set_level,

    .picture_priv_data_size = sizeof(D3D12VAEncodeHEVCPicture),

    .init_sequence_params   = &d3d12va_encode_hevc_init_sequence_params,

    .init_picture_params    = &d3d12va_encode_hevc_init_picture_params,

    .free_picture_params    = &d3d12va_encode_hevc_free_picture_params,

    .write_sequence_header  = &d3d12va_encode_hevc_write_sequence_header,
};

static int d3d12va_encode_hevc_init(AVCodecContext *avctx)
{
    D3D12VAEncodeContext      *ctx = avctx->priv_data;
    D3D12VAEncodeHEVCContext *priv = avctx->priv_data;

    ctx->codec = &d3d12va_encode_type_hevc;

    if (avctx->profile == AV_PROFILE_UNKNOWN)
        avctx->profile = priv->profile;
    if (avctx->level == AV_LEVEL_UNKNOWN)
        avctx->level = priv->level;

    if (avctx->level != AV_LEVEL_UNKNOWN && avctx->level & ~0xff) {
        av_log(avctx, AV_LOG_ERROR, "Invalid level %d: must fit "
               "in 8-bit unsigned integer.\n", avctx->level);
        return AVERROR(EINVAL);
    }

    if (priv->qp > 0)
        ctx->explicit_qp = priv->qp;

    return ff_d3d12va_encode_init(avctx);
}

static int d3d12va_encode_hevc_close(AVCodecContext *avctx)
{
    D3D12VAEncodeHEVCContext *priv = avctx->priv_data;

    ff_cbs_fragment_free(&priv->current_access_unit);
    ff_cbs_close(&priv->cbc);

    av_freep(&priv->common.codec_conf.pHEVCConfig);
    av_freep(&priv->common.gop.pHEVCGroupOfPictures);
    av_freep(&priv->common.level.pHEVCLevelSetting);

    return ff_d3d12va_encode_close(avctx);
}

#define OFFSET(x) offsetof(D3D12VAEncodeHEVCContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM)
static const AVOption d3d12va_encode_hevc_options[] = {
    HW_BASE_ENCODE_COMMON_OPTIONS,
    D3D12VA_ENCODE_RC_OPTIONS,

    { "qp", "Constant QP (for P-frames; scaled by qfactor/qoffset for I/B)",
      OFFSET(qp), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 52, FLAGS },

    { "profile", "Set profile (general_profile_idc)",
      OFFSET(profile), AV_OPT_TYPE_INT,
      { .i64 = AV_PROFILE_UNKNOWN }, AV_PROFILE_UNKNOWN, 0xff, FLAGS, "profile" },

#define PROFILE(name, value)  name, NULL, 0, AV_OPT_TYPE_CONST, \
      { .i64 = value }, 0, 0, FLAGS, "profile"
    { PROFILE("main",               AV_PROFILE_HEVC_MAIN) },
    { PROFILE("main10",             AV_PROFILE_HEVC_MAIN_10) },
#undef PROFILE

    { "tier", "Set tier (general_tier_flag)",
      OFFSET(unit_opts.tier), AV_OPT_TYPE_INT,
      { .i64 = 0 }, 0, 1, FLAGS, "tier" },
    { "main", NULL, 0, AV_OPT_TYPE_CONST,
      { .i64 = 0 }, 0, 0, FLAGS, "tier" },
    { "high", NULL, 0, AV_OPT_TYPE_CONST,
      { .i64 = 1 }, 0, 0, FLAGS, "tier" },

    { "level", "Set level (general_level_idc)",
      OFFSET(level), AV_OPT_TYPE_INT,
      { .i64 = AV_LEVEL_UNKNOWN }, AV_LEVEL_UNKNOWN, 0xff, FLAGS, "level" },

#define LEVEL(name, value) name, NULL, 0, AV_OPT_TYPE_CONST, \
      { .i64 = value }, 0, 0, FLAGS, "level"
    { LEVEL("1",    30) },
    { LEVEL("2",    60) },
    { LEVEL("2.1",  63) },
    { LEVEL("3",    90) },
    { LEVEL("3.1",  93) },
    { LEVEL("4",   120) },
    { LEVEL("4.1", 123) },
    { LEVEL("5",   150) },
    { LEVEL("5.1", 153) },
    { LEVEL("5.2", 156) },
    { LEVEL("6",   180) },
    { LEVEL("6.1", 183) },
    { LEVEL("6.2", 186) },
#undef LEVEL

    { NULL },
};

static const FFCodecDefault d3d12va_encode_hevc_defaults[] = {
    { "b",              "0"   },
    { "bf",             "2"   },
    { "g",              "120" },
    { "i_qfactor",      "1"   },
    { "i_qoffset",      "0"   },
    { "b_qfactor",      "1"   },
    { "b_qoffset",      "0"   },
    { "qmin",           "-1"  },
    { "qmax",           "-1"  },
    { NULL },
};

static const AVClass d3d12va_encode_hevc_class = {
    .class_name = "hevc_d3d12va",
    .item_name  = av_default_item_name,
    .option     = d3d12va_encode_hevc_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_hevc_d3d12va_encoder = {
    .p.name         = "hevc_d3d12va",
    CODEC_LONG_NAME("D3D12VA hevc encoder"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_HEVC,
    .priv_data_size = sizeof(D3D12VAEncodeHEVCContext),
    .init           = &d3d12va_encode_hevc_init,
    FF_CODEC_RECEIVE_PACKET_CB(&ff_d3d12va_encode_receive_packet),
    .close          = &d3d12va_encode_hevc_close,
    .p.priv_class   = &d3d12va_encode_hevc_class,
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE |
                      AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
    .defaults       = d3d12va_encode_hevc_defaults,
    CODEC_PIXFMTS(AV_PIX_FMT_D3D12),
    .hw_configs     = ff_d3d12va_encode_hw_configs,
    .p.wrapper_name = "d3d12va",
};
