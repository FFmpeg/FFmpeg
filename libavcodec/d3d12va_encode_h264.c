/*
 * Direct3D 12 HW acceleration video encoder
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
#include "cbs_h264.h"
#include "hw_base_encode_h264.h"
#include "h2645data.h"
#include "h264_levels.h"
#include "codec_internal.h"
#include "d3d12va_encode.h"

typedef struct D3D12VAEncodeH264Picture {
    int pic_order_cnt;
    int64_t last_idr_frame;
} D3D12VAEncodeH264Picture;

typedef struct D3D12VAEncodeH264Context {
    D3D12VAEncodeContext common;

    // User options.
    int qp;
    int profile;
    int level;
    int idr_pic_id;

    // Writer structures.
    FFHWBaseEncodeH264 units;
    FFHWBaseEncodeH264Opts unit_opts;

    CodedBitstreamContext *cbc;
    CodedBitstreamFragment current_access_unit;
} D3D12VAEncodeH264Context;

typedef struct D3D12VAEncodeH264Level {
    int level;
    D3D12_VIDEO_ENCODER_LEVELS_H264 d3d12_level;
} D3D12VAEncodeH264Level;

static const D3D12VAEncodeH264Level h264_levels[] = {
    { 10, D3D12_VIDEO_ENCODER_LEVELS_H264_1    },
    { 11, D3D12_VIDEO_ENCODER_LEVELS_H264_11   },
    { 12, D3D12_VIDEO_ENCODER_LEVELS_H264_12   },
    { 13, D3D12_VIDEO_ENCODER_LEVELS_H264_13   },
    { 20, D3D12_VIDEO_ENCODER_LEVELS_H264_2    },
    { 21, D3D12_VIDEO_ENCODER_LEVELS_H264_21   },
    { 22, D3D12_VIDEO_ENCODER_LEVELS_H264_22   },
    { 30, D3D12_VIDEO_ENCODER_LEVELS_H264_3    },
    { 31, D3D12_VIDEO_ENCODER_LEVELS_H264_31   },
    { 32, D3D12_VIDEO_ENCODER_LEVELS_H264_32   },
    { 40, D3D12_VIDEO_ENCODER_LEVELS_H264_4    },
    { 41, D3D12_VIDEO_ENCODER_LEVELS_H264_41   },
    { 42, D3D12_VIDEO_ENCODER_LEVELS_H264_42   },
    { 50, D3D12_VIDEO_ENCODER_LEVELS_H264_5    },
    { 51, D3D12_VIDEO_ENCODER_LEVELS_H264_51   },
    { 52, D3D12_VIDEO_ENCODER_LEVELS_H264_52   },
    { 60, D3D12_VIDEO_ENCODER_LEVELS_H264_6    },
    { 61, D3D12_VIDEO_ENCODER_LEVELS_H264_61   },
    { 62, D3D12_VIDEO_ENCODER_LEVELS_H264_62   },
};

static const D3D12_VIDEO_ENCODER_PROFILE_H264 profile_main      = D3D12_VIDEO_ENCODER_PROFILE_H264_MAIN;
static const D3D12_VIDEO_ENCODER_PROFILE_H264 profile_high      = D3D12_VIDEO_ENCODER_PROFILE_H264_HIGH;
static const D3D12_VIDEO_ENCODER_PROFILE_H264 profile_high_10   = D3D12_VIDEO_ENCODER_PROFILE_H264_HIGH_10;

#define D3D_PROFILE_DESC(name) \
    { sizeof(D3D12_VIDEO_ENCODER_PROFILE_H264), { .pH264Profile = (D3D12_VIDEO_ENCODER_PROFILE_H264 *)&profile_ ## name } }
static const D3D12VAEncodeProfile d3d12va_encode_h264_profiles[] = {
    { AV_PROFILE_H264_MAIN,         8, 3, 1, 1, D3D_PROFILE_DESC(main)      },
    { AV_PROFILE_H264_HIGH,         8, 3, 1, 1, D3D_PROFILE_DESC(high)      },
    { AV_PROFILE_H264_HIGH_10,     10, 3, 1, 1, D3D_PROFILE_DESC(high_10)   },
    { AV_PROFILE_UNKNOWN },
};

static int d3d12va_encode_h264_write_access_unit(AVCodecContext *avctx,
                                                 char *data, size_t *data_len,
                                                 CodedBitstreamFragment *au)
{
    D3D12VAEncodeH264Context *priv = avctx->priv_data;
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

static int d3d12va_encode_h264_add_nal(AVCodecContext *avctx,
                                       CodedBitstreamFragment *au,
                                       void *nal_unit)
{
    H264RawNALUnitHeader *header = nal_unit;
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

static int d3d12va_encode_h264_write_sequence_header(AVCodecContext *avctx,
                                                     char *data, size_t *data_len)
{
    D3D12VAEncodeH264Context *priv = avctx->priv_data;
    CodedBitstreamFragment   *au   = &priv->current_access_unit;
    int err;

    err = d3d12va_encode_h264_add_nal(avctx, au, &priv->units.raw_sps);
    if (err < 0)
        goto fail;

    err = d3d12va_encode_h264_add_nal(avctx, au, &priv->units.raw_pps);
    if (err < 0)
        goto fail;

    err = d3d12va_encode_h264_write_access_unit(avctx, data, data_len, au);
fail:
    ff_cbs_fragment_reset(au);
    return err;
}

static int d3d12va_encode_h264_init_sequence_params(AVCodecContext *avctx)
{
    FFHWBaseEncodeContext     *base_ctx = avctx->priv_data;
    D3D12VAEncodeContext      *ctx  = avctx->priv_data;
    D3D12VAEncodeH264Context  *priv = avctx->priv_data;
    AVD3D12VAFramesContext    *hwctx = base_ctx->input_frames->hwctx;
    H264RawSPS                *sps  = &priv->units.raw_sps;
    H264RawPPS                *pps  = &priv->units.raw_pps;
    H264RawVUI                *vui  = &sps->vui;
    D3D12_VIDEO_ENCODER_PROFILE_H264 profile = D3D12_VIDEO_ENCODER_PROFILE_H264_MAIN;
    D3D12_VIDEO_ENCODER_LEVELS_H264 level = { 0 };
    const AVPixFmtDescriptor *desc;
    HRESULT hr;
    int err;

    D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT support = {
        .NodeIndex                        = 0,
        .Codec                            = D3D12_VIDEO_ENCODER_CODEC_H264,
        .InputFormat                      = hwctx->format,
        .RateControl                      = ctx->rc,
        .IntraRefresh                     = D3D12_VIDEO_ENCODER_INTRA_REFRESH_MODE_NONE,
        .SubregionFrameEncoding           = D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_FULL_FRAME,
        .ResolutionsListCount             = 1,
        .pResolutionList                  = &ctx->resolution,
        .CodecGopSequence                 = ctx->gop,
        .MaxReferenceFramesInDPB          = MAX_DPB_SIZE - 1,
        .CodecConfiguration               = ctx->codec_conf,
        .SuggestedProfile.DataSize        = sizeof(D3D12_VIDEO_ENCODER_PROFILE_H264),
        .SuggestedProfile.pH264Profile    = &profile,
        .SuggestedLevel.DataSize          = sizeof(D3D12_VIDEO_ENCODER_LEVELS_H264),
        .SuggestedLevel.pH264LevelSetting = &level,
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
        ctx->is_texture_array = 1;
        av_log(avctx, AV_LOG_DEBUG, "D3D12 video encode on this device uses texture array mode.\n");
    }

    desc = av_pix_fmt_desc_get(base_ctx->input_frames->sw_format);
    av_assert0(desc);

    sps->pic_width_in_mbs_minus1  = ((base_ctx->surface_width + 0x0F) >> 4) - 1;
    sps->pic_height_in_map_units_minus1 = ((base_ctx->surface_height + 0x0F) >> 4) - 1;

    priv->unit_opts.mb_width  = sps->pic_width_in_mbs_minus1 + 1;
    priv->unit_opts.mb_height = sps->pic_height_in_map_units_minus1 +1;

    err = ff_hw_base_encode_init_params_h264(base_ctx, avctx,
                                             &priv->units, &priv->unit_opts);
    if (err < 0)
        return err;

    avctx->level = priv->units.raw_sps.level_idc;

    ctx->gop.pH264GroupOfPictures->pic_order_cnt_type = sps->pic_order_cnt_type;

    // override the default value according to the gop size
    sps->log2_max_frame_num_minus4 = FFMAX(av_ceil_log2(base_ctx->gop_size) - 4, 0);
    ctx->gop.pH264GroupOfPictures->log2_max_frame_num_minus4 = sps->log2_max_frame_num_minus4;
    pps->deblocking_filter_control_present_flag = 1;

    return 0;
}

static int d3d12va_encode_h264_get_encoder_caps(AVCodecContext *avctx)
{
    HRESULT hr;
    FFHWBaseEncodeContext       *base_ctx = avctx->priv_data;
    D3D12VAEncodeContext        *ctx = avctx->priv_data;
    D3D12VAEncodeH264Context    *priv = avctx->priv_data;

    D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264 *config;
    D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_H264 h264_caps;

    D3D12_FEATURE_DATA_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT codec_caps = {
        .NodeIndex                   = 0,
        .Codec                       = D3D12_VIDEO_ENCODER_CODEC_H264,
        .Profile                     = ctx->profile->d3d12_profile,
        .CodecSupportLimits.DataSize = sizeof(D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_H264),
    };

    codec_caps.CodecSupportLimits.pH264Support = &h264_caps;
    hr = ID3D12VideoDevice3_CheckFeatureSupport(ctx->video_device3, D3D12_FEATURE_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT,
                                                &codec_caps, sizeof(codec_caps));
    if (!(SUCCEEDED(hr) && codec_caps.IsSupported))
        return AVERROR(EINVAL);

    ctx->codec_conf.DataSize = sizeof(D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264);
    ctx->codec_conf.pH264Config = av_mallocz(ctx->codec_conf.DataSize);
    if (!ctx->codec_conf.pH264Config)
        return AVERROR(ENOMEM);

    config = ctx->codec_conf.pH264Config;

    config->ConfigurationFlags = D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264_FLAG_NONE;

    if (h264_caps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_H264_FLAG_CABAC_ENCODING_SUPPORT) {
        config->ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264_FLAG_ENABLE_CABAC_ENCODING;
        priv->unit_opts.cabac = 1;
    }

    base_ctx->surface_width  = FFALIGN(avctx->width,  16);
    base_ctx->surface_height = FFALIGN(avctx->height, 16);

    return 0;
}

static int d3d12va_encode_h264_configure(AVCodecContext *avctx)
{
    FFHWBaseEncodeContext       *base_ctx = avctx->priv_data;
    D3D12VAEncodeContext        *ctx = avctx->priv_data;
    D3D12VAEncodeH264Context    *priv = avctx->priv_data;
    int fixed_qp_idr, fixed_qp_p, fixed_qp_b;
    int err;

    err = ff_cbs_init(&priv->cbc, AV_CODEC_ID_H264, avctx);
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
    priv->unit_opts.fixed_qp_idr = 26;

    // GOP
    ctx->gop.DataSize = sizeof(D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE_H264);
    ctx->gop.pH264GroupOfPictures = av_mallocz(ctx->gop.DataSize);
    if (!ctx->gop.pH264GroupOfPictures)
        return AVERROR(ENOMEM);

    ctx->gop.pH264GroupOfPictures->GOPLength      = base_ctx->gop_size;
    ctx->gop.pH264GroupOfPictures->PPicturePeriod = base_ctx->b_per_p + 1;
    ctx->gop.pH264GroupOfPictures->log2_max_frame_num_minus4 = FFMAX(av_ceil_log2(base_ctx->gop_size) - 4, 0);

    return 0;
}

static int d3d12va_encode_h264_set_level(AVCodecContext *avctx)
{
    D3D12VAEncodeContext        *ctx = avctx->priv_data;
    D3D12VAEncodeH264Context    *priv = avctx->priv_data;
    int i;

    ctx->level.DataSize = sizeof(D3D12_VIDEO_ENCODER_LEVELS_H264);
    ctx->level.pH264LevelSetting = av_mallocz(ctx->level.DataSize);
    if (!ctx->level.pH264LevelSetting)
        return AVERROR(ENOMEM);

    for (i = 0; i < FF_ARRAY_ELEMS(h264_levels); i++) {
        if (avctx->level == h264_levels[i].level) {
            *ctx->level.pH264LevelSetting = h264_levels[i].d3d12_level;
            break;
        }
    }

    if (i == FF_ARRAY_ELEMS(h264_levels)) {
        av_log(avctx, AV_LOG_ERROR, "Invalid level %d.\n", avctx->level);
        return AVERROR(EINVAL);
    }

    return 0;
}

static void d3d12va_encode_h264_free_picture_params(D3D12VAEncodePicture *pic)
{
    if (!pic->pic_ctl.pH264PicData)
        return;

    av_freep(&pic->pic_ctl.pH264PicData->pList0ReferenceFrames);
    av_freep(&pic->pic_ctl.pH264PicData->pList1ReferenceFrames);
    av_freep(&pic->pic_ctl.pH264PicData->pReferenceFramesReconPictureDescriptors);
    av_freep(&pic->pic_ctl.pH264PicData);
}

static int d3d12va_encode_h264_init_picture_params(AVCodecContext *avctx,
                                                   FFHWBaseEncodePicture *base_pic)
{
    D3D12VAEncodeH264Context    *ctx = avctx->priv_data;
    D3D12VAEncodePicture        *pic = base_pic->priv;
    D3D12VAEncodeH264Picture    *hpic = base_pic->codec_priv;
    FFHWBaseEncodePicture       *prev = base_pic->prev;
    D3D12VAEncodeH264Picture    *hprev = prev ? prev->codec_priv : NULL;
    D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_H264 *pd = NULL;
    UINT                        *ref_list0 = NULL, *ref_list1 = NULL;
    int i, idx = 0;

    pic->pic_ctl.DataSize = sizeof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264);
    pic->pic_ctl.pH264PicData = av_mallocz(pic->pic_ctl.DataSize);
    if (!pic->pic_ctl.pH264PicData)
        return AVERROR(ENOMEM);

    if (base_pic->type == FF_HW_PICTURE_TYPE_IDR) {
        av_assert0(base_pic->display_order == base_pic->encode_order);
        hpic->last_idr_frame = base_pic->display_order;
        ctx->idr_pic_id++;
    } else {
        av_assert0(prev);
        hpic->last_idr_frame = hprev->last_idr_frame;
    }
    hpic->pic_order_cnt = base_pic->display_order - hpic->last_idr_frame;

    switch(base_pic->type) {
        case FF_HW_PICTURE_TYPE_IDR:
            pic->pic_ctl.pH264PicData->FrameType = D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_IDR_FRAME;
            pic->pic_ctl.pH264PicData->idr_pic_id = ctx->idr_pic_id;
            break;
        case FF_HW_PICTURE_TYPE_I:
            pic->pic_ctl.pH264PicData->FrameType = D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_I_FRAME;
            break;
        case FF_HW_PICTURE_TYPE_P:
            pic->pic_ctl.pH264PicData->FrameType = D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_P_FRAME;
            break;
        case FF_HW_PICTURE_TYPE_B:
            pic->pic_ctl.pH264PicData->FrameType = D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_B_FRAME;
            break;
        default:
            av_assert0(0 && "invalid picture type");
    }

    pic->pic_ctl.pH264PicData->PictureOrderCountNumber    = hpic->pic_order_cnt;
    pic->pic_ctl.pH264PicData->FrameDecodingOrderNumber   = hpic->pic_order_cnt;

    if (base_pic->type == FF_HW_PICTURE_TYPE_P || base_pic->type == FF_HW_PICTURE_TYPE_B) {
        pd = av_calloc(MAX_PICTURE_REFERENCES, sizeof(*pd));
        if (!pd)
            return AVERROR(ENOMEM);

        ref_list0 = av_calloc(MAX_PICTURE_REFERENCES, sizeof(*ref_list0));
        if (!ref_list0)
            return AVERROR(ENOMEM);

        pic->pic_ctl.pH264PicData->List0ReferenceFramesCount = base_pic->nb_refs[0];
        for (i = 0; i < base_pic->nb_refs[0]; i++) {
            FFHWBaseEncodePicture *ref = base_pic->refs[0][i];
            D3D12VAEncodeH264Picture *href;

            av_assert0(ref && ref->encode_order < base_pic->encode_order);
            href = ref->codec_priv;

            ref_list0[i] = idx;
            pd[idx].ReconstructedPictureResourceIndex = idx;
            pd[idx].PictureOrderCountNumber = href->pic_order_cnt;
            idx++;
        }
    }

    if (base_pic->type == FF_HW_PICTURE_TYPE_B) {
        ref_list1 = av_calloc(MAX_PICTURE_REFERENCES, sizeof(*ref_list1));
        if (!ref_list1)
            return AVERROR(ENOMEM);

        pic->pic_ctl.pH264PicData->List1ReferenceFramesCount = base_pic->nb_refs[1];
        for (i = 0; i < base_pic->nb_refs[1]; i++) {
            FFHWBaseEncodePicture *ref = base_pic->refs[1][i];
            D3D12VAEncodeH264Picture *href;

            av_assert0(ref && ref->encode_order < base_pic->encode_order);
            href = ref->codec_priv;

            ref_list1[i] = idx;
            pd[idx].ReconstructedPictureResourceIndex = idx;
            pd[idx].PictureOrderCountNumber = href->pic_order_cnt;
            idx++;
        }
    }

    pic->pic_ctl.pH264PicData->pList0ReferenceFrames = ref_list0;
    pic->pic_ctl.pH264PicData->pList1ReferenceFrames = ref_list1;
    pic->pic_ctl.pH264PicData->ReferenceFramesReconPictureDescriptorsCount = idx;
    pic->pic_ctl.pH264PicData->pReferenceFramesReconPictureDescriptors = pd;

    return 0;
}

static const D3D12VAEncodeType d3d12va_encode_type_h264 = {
    .profiles               = d3d12va_encode_h264_profiles,

    .d3d12_codec            = D3D12_VIDEO_ENCODER_CODEC_H264,

    .flags                  = FF_HW_FLAG_B_PICTURES |
                              FF_HW_FLAG_B_PICTURE_REFERENCES |
                              FF_HW_FLAG_NON_IDR_KEY_PICTURES,

    .default_quality        = 25,

    .get_encoder_caps       = &d3d12va_encode_h264_get_encoder_caps,

    .configure              = &d3d12va_encode_h264_configure,

    .set_level              = &d3d12va_encode_h264_set_level,

    .picture_priv_data_size = sizeof(D3D12VAEncodeH264Picture),

    .init_sequence_params   = &d3d12va_encode_h264_init_sequence_params,

    .init_picture_params    = &d3d12va_encode_h264_init_picture_params,

    .free_picture_params    = &d3d12va_encode_h264_free_picture_params,

    .write_sequence_header  = &d3d12va_encode_h264_write_sequence_header,
};

static int d3d12va_encode_h264_init(AVCodecContext *avctx)
{
    D3D12VAEncodeContext        *ctx = avctx->priv_data;
    D3D12VAEncodeH264Context    *priv = avctx->priv_data;

    ctx->codec = &d3d12va_encode_type_h264;

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

static int d3d12va_encode_h264_close(AVCodecContext *avctx)
{
    D3D12VAEncodeH264Context *priv = avctx->priv_data;

    ff_cbs_fragment_free(&priv->current_access_unit);
    ff_cbs_close(&priv->cbc);

    av_freep(&priv->common.codec_conf.pH264Config);
    av_freep(&priv->common.gop.pH264GroupOfPictures);
    av_freep(&priv->common.level.pH264LevelSetting);

    if (priv->common.rc.ConfigParams.pConfiguration_CQP != NULL) {
        av_freep(&priv->common.rc.ConfigParams.pConfiguration_CQP);
    }

    return ff_d3d12va_encode_close(avctx);
}

#define OFFSET(x) offsetof(D3D12VAEncodeH264Context, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM)
static const AVOption d3d12va_encode_h264_options[] = {
    HW_BASE_ENCODE_COMMON_OPTIONS,
    D3D12VA_ENCODE_COMMON_OPTIONS,
    D3D12VA_ENCODE_RC_OPTIONS,

    { "qp", "Constant QP (for P-frames; scaled by qfactor/qoffset for I/B)",
      OFFSET(qp), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 52, FLAGS },

    { "profile", "Set profile (general_profile_idc)",
      OFFSET(profile), AV_OPT_TYPE_INT,
      { .i64 = AV_PROFILE_UNKNOWN }, AV_PROFILE_UNKNOWN, 0xff, FLAGS, "profile" },

#define PROFILE(name, value)  name, NULL, 0, AV_OPT_TYPE_CONST, \
      { .i64 = value }, 0, 0, FLAGS, "profile"
    { PROFILE("main",               AV_PROFILE_H264_MAIN) },
    { PROFILE("high",               AV_PROFILE_H264_HIGH) },
    { PROFILE("high10",             AV_PROFILE_H264_HIGH_10) },
#undef PROFILE

    { "level", "Set level (general_level_idc)",
      OFFSET(level), AV_OPT_TYPE_INT,
      { .i64 = AV_LEVEL_UNKNOWN }, AV_LEVEL_UNKNOWN, 0xff, FLAGS, "level" },

#define LEVEL(name, value) name, NULL, 0, AV_OPT_TYPE_CONST, \
      { .i64 = value }, 0, 0, FLAGS, "level"
    { LEVEL("1",    10) },
    { LEVEL("1.1",  11) },
    { LEVEL("1.2",  12) },
    { LEVEL("1.3",  13) },
    { LEVEL("2",    20) },
    { LEVEL("2.1",  21) },
    { LEVEL("2.2",  22) },
    { LEVEL("3",    30) },
    { LEVEL("3.1",  31) },
    { LEVEL("3.2",  32) },
    { LEVEL("4",    40) },
    { LEVEL("4.1",  41) },
    { LEVEL("4.2",  42) },
    { LEVEL("5",    50) },
    { LEVEL("5.1",  51) },
    { LEVEL("5.2",  52) },
    { LEVEL("6",    60) },
    { LEVEL("6.1",  61) },
    { LEVEL("6.2",  62) },
#undef LEVEL

    { NULL },
};

static const FFCodecDefault d3d12va_encode_h264_defaults[] = {
    { "b",              "0"   },
    { "bf",             "2"   },
    { "g",              "120" },
    { "qmin",           "-1"  },
    { "qmax",           "-1"  },
    { NULL },
};

static const AVClass d3d12va_encode_h264_class = {
    .class_name = "h264_d3d12va",
    .item_name  = av_default_item_name,
    .option     = d3d12va_encode_h264_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_h264_d3d12va_encoder = {
    .p.name         = "h264_d3d12va",
    CODEC_LONG_NAME("D3D12VA h264 encoder"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_H264,
    .priv_data_size = sizeof(D3D12VAEncodeH264Context),
    .init           = &d3d12va_encode_h264_init,
    FF_CODEC_RECEIVE_PACKET_CB(&ff_d3d12va_encode_receive_packet),
    .close          = &d3d12va_encode_h264_close,
    .p.priv_class   = &d3d12va_encode_h264_class,
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE |
                      AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
    .defaults       = d3d12va_encode_h264_defaults,
    CODEC_PIXFMTS(AV_PIX_FMT_D3D12),
    .hw_configs     = ff_d3d12va_encode_hw_configs,
    .p.wrapper_name = "d3d12va",
};
