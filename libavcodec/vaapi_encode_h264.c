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

#include <va/va.h>
#include <va/va_enc_h264.h>

#include "libavutil/avassert.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixfmt.h"

#include "avcodec.h"
#include "h264.h"
#include "internal.h"
#include "vaapi_encode.h"
#include "vaapi_encode_h26x.h"

enum {
    SLICE_TYPE_P  = 0,
    SLICE_TYPE_B  = 1,
    SLICE_TYPE_I  = 2,
    SLICE_TYPE_SP = 3,
    SLICE_TYPE_SI = 4,
};

// This structure contains all possibly-useful per-sequence syntax elements
// which are not already contained in the various VAAPI structures.
typedef struct VAAPIEncodeH264MiscSequenceParams {
    unsigned int profile_idc;
    char constraint_set0_flag;
    char constraint_set1_flag;
    char constraint_set2_flag;
    char constraint_set3_flag;
    char constraint_set4_flag;
    char constraint_set5_flag;

    char separate_colour_plane_flag;
    char qpprime_y_zero_transform_bypass_flag;

    char gaps_in_frame_num_allowed_flag;
    char delta_pic_order_always_zero_flag;
    char bottom_field_pic_order_in_frame_present_flag;

    unsigned int num_slice_groups_minus1;
    unsigned int slice_group_map_type;

    int pic_init_qs_minus26;

    char vui_parameters_present_flag;
} VAAPIEncodeH264MiscSequenceParams;

// This structure contains all possibly-useful per-slice syntax elements
// which are not already contained in the various VAAPI structures.
typedef struct VAAPIEncodeH264MiscSliceParams {
    unsigned int nal_unit_type;
    unsigned int nal_ref_idc;

    unsigned int colour_plane_id;
    char field_pic_flag;
    char bottom_field_flag;

    unsigned int redundant_pic_cnt;

    char sp_for_switch_flag;
    int slice_qs_delta;

    char ref_pic_list_modification_flag_l0;
    char ref_pic_list_modification_flag_l1;

    char no_output_of_prior_pics_flag;
    char long_term_reference_flag;
    char adaptive_ref_pic_marking_mode_flag;
} VAAPIEncodeH264MiscSliceParams;

typedef struct VAAPIEncodeH264Slice {
    VAAPIEncodeH264MiscSliceParams misc_slice_params;
} VAAPIEncodeH264Slice;

typedef struct VAAPIEncodeH264Context {
    VAAPIEncodeH264MiscSequenceParams misc_sequence_params;

    int mb_width;
    int mb_height;

    int fixed_qp_idr;
    int fixed_qp_p;
    int fixed_qp_b;

    int64_t idr_pic_count;
    int64_t last_idr_frame;

    // Rate control configuration.
    struct {
        VAEncMiscParameterBuffer misc;
        VAEncMiscParameterRateControl rc;
    } rc_params;
    struct {
        VAEncMiscParameterBuffer misc;
        VAEncMiscParameterHRD hrd;
    } hrd_params;

#if VA_CHECK_VERSION(0, 36, 0)
    // Speed-quality tradeoff setting.
    struct {
        VAEncMiscParameterBuffer misc;
        VAEncMiscParameterBufferQualityLevel quality;
    } quality_params;
#endif
} VAAPIEncodeH264Context;

typedef struct VAAPIEncodeH264Options {
    int qp;
    int quality;
} VAAPIEncodeH264Options;


#define vseq_var(name)     vseq->name, name
#define vseq_field(name)   vseq->seq_fields.bits.name, name
#define vpic_var(name)     vpic->name, name
#define vpic_field(name)   vpic->pic_fields.bits.name, name
#define vslice_var(name)   vslice->name, name
#define vslice_field(name) vslice->slice_fields.bits.name, name
#define mseq_var(name)     mseq->name, name
#define mslice_var(name)   mslice->name, name

static void vaapi_encode_h264_write_nal_header(PutBitContext *pbc,
                                               int nal_unit_type, int nal_ref_idc)
{
    u(1, 0, forbidden_zero_bit);
    u(2, nal_ref_idc, nal_ref_idc);
    u(5, nal_unit_type, nal_unit_type);
}

static void vaapi_encode_h264_write_trailing_rbsp(PutBitContext *pbc)
{
    u(1, 1, rbsp_stop_one_bit);
    while (put_bits_count(pbc) & 7)
        u(1, 0, rbsp_alignment_zero_bit);
}

static void vaapi_encode_h264_write_sps(PutBitContext *pbc,
                                        VAAPIEncodeContext *ctx)
{
    VAEncSequenceParameterBufferH264  *vseq = ctx->codec_sequence_params;
    VAAPIEncodeH264Context            *priv = ctx->priv_data;
    VAAPIEncodeH264MiscSequenceParams *mseq = &priv->misc_sequence_params;
    int i;

    vaapi_encode_h264_write_nal_header(pbc, NAL_SPS, 3);

    u(8, mseq_var(profile_idc));
    u(1, mseq_var(constraint_set0_flag));
    u(1, mseq_var(constraint_set1_flag));
    u(1, mseq_var(constraint_set2_flag));
    u(1, mseq_var(constraint_set3_flag));
    u(1, mseq_var(constraint_set4_flag));
    u(1, mseq_var(constraint_set5_flag));
    u(2, 0, reserved_zero_2bits);

    u(8, vseq_var(level_idc));

    ue(vseq_var(seq_parameter_set_id));

    if (mseq->profile_idc == 100 || mseq->profile_idc == 110 ||
        mseq->profile_idc == 122 || mseq->profile_idc == 244 ||
        mseq->profile_idc ==  44 || mseq->profile_idc ==  83 ||
        mseq->profile_idc ==  86 || mseq->profile_idc == 118 ||
        mseq->profile_idc == 128 || mseq->profile_idc == 138) {
        ue(vseq_field(chroma_format_idc));

        if (vseq->seq_fields.bits.chroma_format_idc == 3)
            u(1, mseq_var(separate_colour_plane_flag));

        ue(vseq_var(bit_depth_luma_minus8));
        ue(vseq_var(bit_depth_chroma_minus8));

        u(1, mseq_var(qpprime_y_zero_transform_bypass_flag));

        u(1, vseq_field(seq_scaling_matrix_present_flag));
        if (vseq->seq_fields.bits.seq_scaling_matrix_present_flag) {
            av_assert0(0 && "scaling matrices not supported");
        }
    }

    ue(vseq_field(log2_max_frame_num_minus4));
    ue(vseq_field(pic_order_cnt_type));

    if (vseq->seq_fields.bits.pic_order_cnt_type == 0) {
        ue(vseq_field(log2_max_pic_order_cnt_lsb_minus4));
    } else if (vseq->seq_fields.bits.pic_order_cnt_type == 1) {
        u(1, mseq_var(delta_pic_order_always_zero_flag));
        se(vseq_var(offset_for_non_ref_pic));
        se(vseq_var(offset_for_top_to_bottom_field));
        ue(vseq_var(num_ref_frames_in_pic_order_cnt_cycle));

        for (i = 0; i < vseq->num_ref_frames_in_pic_order_cnt_cycle; i++)
            se(vseq_var(offset_for_ref_frame[i]));
    }

    ue(vseq_var(max_num_ref_frames));
    u(1, mseq_var(gaps_in_frame_num_allowed_flag));

    ue(vseq->picture_width_in_mbs  - 1, pic_width_in_mbs_minus1);
    ue(vseq->picture_height_in_mbs - 1, pic_height_in_mbs_minus1);

    u(1, vseq_field(frame_mbs_only_flag));
    if (!vseq->seq_fields.bits.frame_mbs_only_flag)
        u(1, vseq_field(mb_adaptive_frame_field_flag));

    u(1, vseq_field(direct_8x8_inference_flag));

    u(1, vseq_var(frame_cropping_flag));
    if (vseq->frame_cropping_flag) {
        ue(vseq_var(frame_crop_left_offset));
        ue(vseq_var(frame_crop_right_offset));
        ue(vseq_var(frame_crop_top_offset));
        ue(vseq_var(frame_crop_bottom_offset));
    }

    u(1, mseq_var(vui_parameters_present_flag));

    vaapi_encode_h264_write_trailing_rbsp(pbc);
}

static void vaapi_encode_h264_write_pps(PutBitContext *pbc,
                                        VAAPIEncodeContext *ctx)
{
    VAEncPictureParameterBufferH264   *vpic = ctx->codec_picture_params;
    VAAPIEncodeH264Context            *priv = ctx->priv_data;
    VAAPIEncodeH264MiscSequenceParams *mseq = &priv->misc_sequence_params;

    vaapi_encode_h264_write_nal_header(pbc, NAL_PPS, 3);

    ue(vpic_var(pic_parameter_set_id));
    ue(vpic_var(seq_parameter_set_id));

    u(1, vpic_field(entropy_coding_mode_flag));
    u(1, mseq_var(bottom_field_pic_order_in_frame_present_flag));

    ue(mseq_var(num_slice_groups_minus1));
    if (mseq->num_slice_groups_minus1 > 0) {
        ue(mseq_var(slice_group_map_type));
        av_assert0(0 && "slice groups not supported");
    }

    ue(vpic_var(num_ref_idx_l0_active_minus1));
    ue(vpic_var(num_ref_idx_l1_active_minus1));

    u(1, vpic_field(weighted_pred_flag));
    u(2, vpic_field(weighted_bipred_idc));

    se(vpic->pic_init_qp - 26, pic_init_qp_minus26);
    se(mseq_var(pic_init_qs_minus26));
    se(vpic_var(chroma_qp_index_offset));

    u(1, vpic_field(deblocking_filter_control_present_flag));
    u(1, vpic_field(constrained_intra_pred_flag));
    u(1, vpic_field(redundant_pic_cnt_present_flag));
    u(1, vpic_field(transform_8x8_mode_flag));

    u(1, vpic_field(pic_scaling_matrix_present_flag));
    if (vpic->pic_fields.bits.pic_scaling_matrix_present_flag) {
        av_assert0(0 && "scaling matrices not supported");
    }

    se(vpic_var(second_chroma_qp_index_offset));

    vaapi_encode_h264_write_trailing_rbsp(pbc);
}

static void vaapi_encode_h264_write_slice_header2(PutBitContext *pbc,
                                                  VAAPIEncodeContext *ctx,
                                                  VAAPIEncodePicture *pic,
                                                  VAAPIEncodeSlice *slice)
{
    VAEncSequenceParameterBufferH264  *vseq = ctx->codec_sequence_params;
    VAEncPictureParameterBufferH264   *vpic = pic->codec_picture_params;
    VAEncSliceParameterBufferH264   *vslice = slice->codec_slice_params;
    VAAPIEncodeH264Context            *priv = ctx->priv_data;
    VAAPIEncodeH264MiscSequenceParams *mseq = &priv->misc_sequence_params;
    VAAPIEncodeH264Slice            *pslice = slice->priv_data;
    VAAPIEncodeH264MiscSliceParams  *mslice = &pslice->misc_slice_params;

    vaapi_encode_h264_write_nal_header(pbc, mslice->nal_unit_type,
                                       mslice->nal_ref_idc);

    ue(vslice->macroblock_address, first_mb_in_slice);
    ue(vslice_var(slice_type));
    ue(vpic_var(pic_parameter_set_id));

    if (mseq->separate_colour_plane_flag) {
        u(2, mslice_var(colour_plane_id));
    }

    u(4 + vseq->seq_fields.bits.log2_max_frame_num_minus4,
      (vpic->frame_num &
       ((1 << (4 + vseq->seq_fields.bits.log2_max_frame_num_minus4)) - 1)),
      frame_num);

    if (!vseq->seq_fields.bits.frame_mbs_only_flag) {
        u(1, mslice_var(field_pic_flag));
        if (mslice->field_pic_flag)
            u(1, mslice_var(bottom_field_flag));
    }

    if (vpic->pic_fields.bits.idr_pic_flag) {
        ue(vslice_var(idr_pic_id));
    }

    if (vseq->seq_fields.bits.pic_order_cnt_type == 0) {
        u(4 + vseq->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4,
          vslice_var(pic_order_cnt_lsb));
        if (mseq->bottom_field_pic_order_in_frame_present_flag &&
            !mslice->field_pic_flag) {
            se(vslice_var(delta_pic_order_cnt_bottom));
        }
    }

    if (vseq->seq_fields.bits.pic_order_cnt_type == 1 &&
        !vseq->seq_fields.bits.delta_pic_order_always_zero_flag) {
        se(vslice_var(delta_pic_order_cnt[0]));
        if (mseq->bottom_field_pic_order_in_frame_present_flag &&
            !mslice->field_pic_flag) {
            se(vslice_var(delta_pic_order_cnt[1]));
        }
    }

    if (vpic->pic_fields.bits.redundant_pic_cnt_present_flag) {
        ue(mslice_var(redundant_pic_cnt));
    }

    if (vslice->slice_type == SLICE_TYPE_B) {
        u(1, vslice_var(direct_spatial_mv_pred_flag));
    }

    if (vslice->slice_type == SLICE_TYPE_P ||
        vslice->slice_type == SLICE_TYPE_SP ||
        vslice->slice_type == SLICE_TYPE_B) {
        u(1, vslice_var(num_ref_idx_active_override_flag));
        if (vslice->num_ref_idx_active_override_flag) {
            ue(vslice_var(num_ref_idx_l0_active_minus1));
            if (vslice->slice_type == SLICE_TYPE_B)
                ue(vslice_var(num_ref_idx_l1_active_minus1));
        }
    }

    if (mslice->nal_unit_type == 20 || mslice->nal_unit_type == 21) {
        av_assert0(0 && "no MVC support");
    } else {
        if (vslice->slice_type % 5 != 2 && vslice->slice_type % 5 != 4) {
            u(1, mslice_var(ref_pic_list_modification_flag_l0));
            if (mslice->ref_pic_list_modification_flag_l0) {
                av_assert0(0 && "ref pic list modification");
            }
        }
        if (vslice->slice_type % 5 == 1) {
            u(1, mslice_var(ref_pic_list_modification_flag_l1));
            if (mslice->ref_pic_list_modification_flag_l1) {
                av_assert0(0 && "ref pic list modification");
            }
        }
    }

    if ((vpic->pic_fields.bits.weighted_pred_flag &&
         (vslice->slice_type == SLICE_TYPE_P ||
          vslice->slice_type == SLICE_TYPE_SP)) ||
        (vpic->pic_fields.bits.weighted_bipred_idc == 1 &&
         vslice->slice_type == SLICE_TYPE_B)) {
        av_assert0(0 && "prediction weights not supported");
    }

    av_assert0(mslice->nal_ref_idc > 0 ==
               vpic->pic_fields.bits.reference_pic_flag);
    if (mslice->nal_ref_idc != 0) {
        if (vpic->pic_fields.bits.idr_pic_flag) {
            u(1, mslice_var(no_output_of_prior_pics_flag));
            u(1, mslice_var(long_term_reference_flag));
        } else {
            u(1, mslice_var(adaptive_ref_pic_marking_mode_flag));
            if (mslice->adaptive_ref_pic_marking_mode_flag) {
                av_assert0(0 && "MMCOs not supported");
            }
        }
    }

    if (vpic->pic_fields.bits.entropy_coding_mode_flag &&
        vslice->slice_type != SLICE_TYPE_I &&
        vslice->slice_type != SLICE_TYPE_SI) {
        ue(vslice_var(cabac_init_idc));
    }

    se(vslice_var(slice_qp_delta));
    if (vslice->slice_type == SLICE_TYPE_SP ||
        vslice->slice_type == SLICE_TYPE_SI) {
        if (vslice->slice_type == SLICE_TYPE_SP)
            u(1, mslice_var(sp_for_switch_flag));
        se(mslice_var(slice_qs_delta));
    }

    if (vpic->pic_fields.bits.deblocking_filter_control_present_flag) {
        ue(vslice_var(disable_deblocking_filter_idc));
        if (vslice->disable_deblocking_filter_idc != 1) {
            se(vslice_var(slice_alpha_c0_offset_div2));
            se(vslice_var(slice_beta_offset_div2));
        }
    }

    if (mseq->num_slice_groups_minus1 > 0 &&
        mseq->slice_group_map_type >= 3 && mseq->slice_group_map_type <= 5) {
        av_assert0(0 && "slice groups not supported");
    }

    // No alignment - this need not be a byte boundary.
}

static int vaapi_encode_h264_write_sequence_header(AVCodecContext *avctx,
                                                   char *data, size_t *data_len)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    PutBitContext pbc;
    char tmp[256];
    int err;
    size_t nal_len, bit_len, bit_pos, next_len;

    bit_len = *data_len;
    bit_pos = 0;

    init_put_bits(&pbc, tmp, sizeof(tmp));
    vaapi_encode_h264_write_sps(&pbc, ctx);
    nal_len = put_bits_count(&pbc);
    flush_put_bits(&pbc);

    next_len = bit_len - bit_pos;
    err = ff_vaapi_encode_h26x_nal_unit_to_byte_stream(data + bit_pos / 8,
                                                       &next_len,
                                                       tmp, nal_len);
    if (err < 0)
        return err;
    bit_pos += next_len;

    init_put_bits(&pbc, tmp, sizeof(tmp));
    vaapi_encode_h264_write_pps(&pbc, ctx);
    nal_len = put_bits_count(&pbc);
    flush_put_bits(&pbc);

    next_len = bit_len - bit_pos;
    err = ff_vaapi_encode_h26x_nal_unit_to_byte_stream(data + bit_pos / 8,
                                                       &next_len,
                                                       tmp, nal_len);
    if (err < 0)
        return err;
    bit_pos += next_len;

    *data_len = bit_pos;
    return 0;
}

static int vaapi_encode_h264_write_slice_header(AVCodecContext *avctx,
                                                VAAPIEncodePicture *pic,
                                                VAAPIEncodeSlice *slice,
                                                char *data, size_t *data_len)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;
    PutBitContext pbc;
    char tmp[256];
    size_t header_len;

    init_put_bits(&pbc, tmp, sizeof(tmp));
    vaapi_encode_h264_write_slice_header2(&pbc, ctx, pic, slice);
    header_len = put_bits_count(&pbc);
    flush_put_bits(&pbc);

    return ff_vaapi_encode_h26x_nal_unit_to_byte_stream(data, data_len,
                                                        tmp, header_len);
}

static int vaapi_encode_h264_init_sequence_params(AVCodecContext *avctx)
{
    VAAPIEncodeContext                 *ctx = avctx->priv_data;
    VAEncSequenceParameterBufferH264  *vseq = ctx->codec_sequence_params;
    VAEncPictureParameterBufferH264   *vpic = ctx->codec_picture_params;
    VAAPIEncodeH264Context            *priv = ctx->priv_data;
    VAAPIEncodeH264MiscSequenceParams *mseq = &priv->misc_sequence_params;
    int i;

    {
        vseq->seq_parameter_set_id = 0;

        vseq->level_idc = avctx->level;

        vseq->max_num_ref_frames = 2;

        vseq->picture_width_in_mbs  = priv->mb_width;
        vseq->picture_height_in_mbs = priv->mb_height;

        vseq->seq_fields.bits.chroma_format_idc = 1;
        vseq->seq_fields.bits.frame_mbs_only_flag = 1;
        vseq->seq_fields.bits.direct_8x8_inference_flag = 1;
        vseq->seq_fields.bits.log2_max_frame_num_minus4 = 4;
        vseq->seq_fields.bits.pic_order_cnt_type = 0;

        if (ctx->input_width  != ctx->aligned_width ||
            ctx->input_height != ctx->aligned_height) {
            vseq->frame_cropping_flag = 1;

            vseq->frame_crop_left_offset   = 0;
            vseq->frame_crop_right_offset  =
                (ctx->aligned_width - ctx->input_width) / 2;
            vseq->frame_crop_top_offset    = 0;
            vseq->frame_crop_bottom_offset =
                (ctx->aligned_height - ctx->input_height) / 2;
        } else {
            vseq->frame_cropping_flag = 0;
        }

        vseq->bits_per_second = avctx->bit_rate;
        if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
            vseq->num_units_in_tick = avctx->framerate.num;
            vseq->time_scale        = 2 * avctx->framerate.den;
        } else {
            vseq->num_units_in_tick = avctx->time_base.num;
            vseq->time_scale        = 2 * avctx->time_base.den;
        }

        vseq->intra_period     = ctx->p_per_i * (ctx->b_per_p + 1);
        vseq->intra_idr_period = vseq->intra_period;
        vseq->ip_period        = ctx->b_per_p + 1;
    }

    {
        vpic->CurrPic.picture_id = VA_INVALID_ID;
        vpic->CurrPic.flags      = VA_PICTURE_H264_INVALID;

        for (i = 0; i < FF_ARRAY_ELEMS(vpic->ReferenceFrames); i++) {
            vpic->ReferenceFrames[i].picture_id = VA_INVALID_ID;
            vpic->ReferenceFrames[i].flags      = VA_PICTURE_H264_INVALID;
        }

        vpic->coded_buf = VA_INVALID_ID;

        vpic->pic_parameter_set_id = 0;
        vpic->seq_parameter_set_id = 0;

        vpic->num_ref_idx_l0_active_minus1 = 0;
        vpic->num_ref_idx_l1_active_minus1 = 0;

        vpic->pic_fields.bits.entropy_coding_mode_flag =
            ((avctx->profile & 0xff) != 66);
        vpic->pic_fields.bits.weighted_pred_flag = 0;
        vpic->pic_fields.bits.weighted_bipred_idc = 0;
        vpic->pic_fields.bits.transform_8x8_mode_flag =
            ((avctx->profile & 0xff) >= 100);

        vpic->pic_init_qp = priv->fixed_qp_idr;
    }

    {
        mseq->profile_idc = avctx->profile & 0xff;

        if (avctx->profile & FF_PROFILE_H264_CONSTRAINED)
            mseq->constraint_set1_flag = 1;
        if (avctx->profile & FF_PROFILE_H264_INTRA)
            mseq->constraint_set3_flag = 1;
    }

    return 0;
}

static int vaapi_encode_h264_init_picture_params(AVCodecContext *avctx,
                                                 VAAPIEncodePicture *pic)
{
    VAAPIEncodeContext                *ctx = avctx->priv_data;
    VAEncSequenceParameterBufferH264 *vseq = ctx->codec_sequence_params;
    VAEncPictureParameterBufferH264  *vpic = pic->codec_picture_params;
    VAAPIEncodeH264Context           *priv = ctx->priv_data;
    int i;

    if (pic->type == PICTURE_TYPE_IDR) {
        av_assert0(pic->display_order == pic->encode_order);
        priv->last_idr_frame = pic->display_order;
    } else {
        av_assert0(pic->display_order > priv->last_idr_frame);
    }

    vpic->frame_num = (pic->encode_order - priv->last_idr_frame) &
        ((1 << (4 + vseq->seq_fields.bits.log2_max_frame_num_minus4)) - 1);

    vpic->CurrPic.picture_id          = pic->recon_surface;
    vpic->CurrPic.frame_idx           = vpic->frame_num;
    vpic->CurrPic.flags               = 0;
    vpic->CurrPic.TopFieldOrderCnt    = pic->display_order;
    vpic->CurrPic.BottomFieldOrderCnt = pic->display_order;

    for (i = 0; i < pic->nb_refs; i++) {
        VAAPIEncodePicture *ref = pic->refs[i];
        av_assert0(ref && ref->encode_order >= priv->last_idr_frame);
        vpic->ReferenceFrames[i].picture_id = ref->recon_surface;
        vpic->ReferenceFrames[i].frame_idx =
            ref->encode_order - priv->last_idr_frame;
        vpic->ReferenceFrames[i].flags = VA_PICTURE_H264_SHORT_TERM_REFERENCE;
        vpic->ReferenceFrames[i].TopFieldOrderCnt    = ref->display_order;
        vpic->ReferenceFrames[i].BottomFieldOrderCnt = ref->display_order;
    }
    for (; i < FF_ARRAY_ELEMS(vpic->ReferenceFrames); i++) {
        vpic->ReferenceFrames[i].picture_id = VA_INVALID_ID;
        vpic->ReferenceFrames[i].flags = VA_PICTURE_H264_INVALID;
    }

    vpic->coded_buf = pic->output_buffer;

    vpic->pic_fields.bits.idr_pic_flag = (pic->type == PICTURE_TYPE_IDR);
    vpic->pic_fields.bits.reference_pic_flag = (pic->type != PICTURE_TYPE_B);

    pic->nb_slices = 1;

    return 0;
}

static int vaapi_encode_h264_init_slice_params(AVCodecContext *avctx,
                                               VAAPIEncodePicture *pic,
                                               VAAPIEncodeSlice *slice)
{
    VAAPIEncodeContext                 *ctx = avctx->priv_data;
    VAEncSequenceParameterBufferH264  *vseq = ctx->codec_sequence_params;
    VAEncPictureParameterBufferH264   *vpic = pic->codec_picture_params;
    VAEncSliceParameterBufferH264   *vslice = slice->codec_slice_params;
    VAAPIEncodeH264Context            *priv = ctx->priv_data;
    VAAPIEncodeH264Slice            *pslice;
    VAAPIEncodeH264MiscSliceParams  *mslice;
    int i;

    slice->priv_data = av_mallocz(sizeof(*pslice));
    if (!slice->priv_data)
        return AVERROR(ENOMEM);
    pslice = slice->priv_data;
    mslice = &pslice->misc_slice_params;

    if (pic->type == PICTURE_TYPE_IDR)
        mslice->nal_unit_type = NAL_IDR_SLICE;
    else
        mslice->nal_unit_type = NAL_SLICE;

    switch (pic->type) {
    case PICTURE_TYPE_IDR:
        vslice->slice_type  = SLICE_TYPE_I;
        mslice->nal_ref_idc = 3;
        break;
    case PICTURE_TYPE_I:
        vslice->slice_type  = SLICE_TYPE_I;
        mslice->nal_ref_idc = 2;
        break;
    case PICTURE_TYPE_P:
        vslice->slice_type  = SLICE_TYPE_P;
        mslice->nal_ref_idc = 1;
        break;
    case PICTURE_TYPE_B:
        vslice->slice_type  = SLICE_TYPE_B;
        mslice->nal_ref_idc = 0;
        break;
    default:
        av_assert0(0 && "invalid picture type");
    }

    // Only one slice per frame.
    vslice->macroblock_address = 0;
    vslice->num_macroblocks = priv->mb_width * priv->mb_height;

    vslice->macroblock_info = VA_INVALID_ID;

    vslice->pic_parameter_set_id = vpic->pic_parameter_set_id;
    vslice->idr_pic_id = priv->idr_pic_count++;

    vslice->pic_order_cnt_lsb = pic->display_order &
        ((1 << (4 + vseq->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4)) - 1);

    for (i = 0; i < FF_ARRAY_ELEMS(vslice->RefPicList0); i++) {
        vslice->RefPicList0[i].picture_id = VA_INVALID_ID;
        vslice->RefPicList0[i].flags      = VA_PICTURE_H264_INVALID;
        vslice->RefPicList1[i].picture_id = VA_INVALID_ID;
        vslice->RefPicList1[i].flags      = VA_PICTURE_H264_INVALID;
    }

    av_assert0(pic->nb_refs <= 2);
    if (pic->nb_refs >= 1) {
        // Backward reference for P- or B-frame.
        av_assert0(pic->type == PICTURE_TYPE_P ||
                   pic->type == PICTURE_TYPE_B);

        vslice->num_ref_idx_l0_active_minus1 = 0;
        vslice->RefPicList0[0] = vpic->ReferenceFrames[0];
    }
    if (pic->nb_refs >= 2) {
        // Forward reference for B-frame.
        av_assert0(pic->type == PICTURE_TYPE_B);

        vslice->num_ref_idx_l1_active_minus1 = 0;
        vslice->RefPicList1[0] = vpic->ReferenceFrames[1];
    }

    if (pic->type == PICTURE_TYPE_B)
        vslice->slice_qp_delta = priv->fixed_qp_b - vpic->pic_init_qp;
    else if (pic->type == PICTURE_TYPE_P)
        vslice->slice_qp_delta = priv->fixed_qp_p - vpic->pic_init_qp;
    else
        vslice->slice_qp_delta = priv->fixed_qp_idr - vpic->pic_init_qp;

    vslice->direct_spatial_mv_pred_flag = 1;

    return 0;
}

static av_cold int vaapi_encode_h264_init_constant_bitrate(AVCodecContext *avctx)
{
    VAAPIEncodeContext      *ctx = avctx->priv_data;
    VAAPIEncodeH264Context *priv = ctx->priv_data;
    int hrd_buffer_size;
    int hrd_initial_buffer_fullness;

    if (avctx->bit_rate > INT32_MAX) {
        av_log(avctx, AV_LOG_ERROR, "Target bitrate of 2^31 bps or "
               "higher is not supported.\n");
        return AVERROR(EINVAL);
    }

    if (avctx->rc_buffer_size)
        hrd_buffer_size = avctx->rc_buffer_size;
    else
        hrd_buffer_size = avctx->bit_rate;
    if (avctx->rc_initial_buffer_occupancy)
        hrd_initial_buffer_fullness = avctx->rc_initial_buffer_occupancy;
    else
        hrd_initial_buffer_fullness = hrd_buffer_size * 3 / 4;

    priv->rc_params.misc.type = VAEncMiscParameterTypeRateControl;
    priv->rc_params.rc = (VAEncMiscParameterRateControl) {
        .bits_per_second   = avctx->bit_rate,
        .target_percentage = 66,
        .window_size       = 1000,
        .initial_qp        = (avctx->qmax >= 0 ? avctx->qmax : 40),
        .min_qp            = (avctx->qmin >= 0 ? avctx->qmin : 18),
        .basic_unit_size   = 0,
    };
    ctx->global_params[ctx->nb_global_params] =
        &priv->rc_params.misc;
    ctx->global_params_size[ctx->nb_global_params++] =
        sizeof(priv->rc_params);

    priv->hrd_params.misc.type = VAEncMiscParameterTypeHRD;
    priv->hrd_params.hrd = (VAEncMiscParameterHRD) {
        .initial_buffer_fullness = hrd_initial_buffer_fullness,
        .buffer_size             = hrd_buffer_size,
    };
    ctx->global_params[ctx->nb_global_params] =
        &priv->hrd_params.misc;
    ctx->global_params_size[ctx->nb_global_params++] =
        sizeof(priv->hrd_params);

    // These still need to be  set for pic_init_qp/slice_qp_delta.
    priv->fixed_qp_idr = 26;
    priv->fixed_qp_p   = 26;
    priv->fixed_qp_b   = 26;

    av_log(avctx, AV_LOG_DEBUG, "Using constant-bitrate = %"PRId64" bps.\n",
           avctx->bit_rate);
    return 0;
}

static av_cold int vaapi_encode_h264_init_fixed_qp(AVCodecContext *avctx)
{
    VAAPIEncodeContext      *ctx = avctx->priv_data;
    VAAPIEncodeH264Context *priv = ctx->priv_data;
    VAAPIEncodeH264Options  *opt = ctx->codec_options;

    priv->fixed_qp_p = opt->qp;
    if (avctx->i_quant_factor > 0.0)
        priv->fixed_qp_idr = (int)((priv->fixed_qp_p * avctx->i_quant_factor +
                                    avctx->i_quant_offset) + 0.5);
    else
        priv->fixed_qp_idr = priv->fixed_qp_p;
    if (avctx->b_quant_factor > 0.0)
        priv->fixed_qp_b = (int)((priv->fixed_qp_p * avctx->b_quant_factor +
                                  avctx->b_quant_offset) + 0.5);
    else
        priv->fixed_qp_b = priv->fixed_qp_p;

    av_log(avctx, AV_LOG_DEBUG, "Using fixed QP = "
           "%d / %d / %d for IDR- / P- / B-frames.\n",
           priv->fixed_qp_idr, priv->fixed_qp_p, priv->fixed_qp_b);
    return 0;
}

static av_cold int vaapi_encode_h264_init_internal(AVCodecContext *avctx)
{
    static const VAConfigAttrib default_config_attributes[] = {
        { .type  = VAConfigAttribRTFormat,
          .value = VA_RT_FORMAT_YUV420 },
        { .type  = VAConfigAttribEncPackedHeaders,
          .value = (VA_ENC_PACKED_HEADER_SEQUENCE |
                    VA_ENC_PACKED_HEADER_SLICE) },
    };

    VAAPIEncodeContext      *ctx = avctx->priv_data;
    VAAPIEncodeH264Context *priv = ctx->priv_data;
    VAAPIEncodeH264Options  *opt = ctx->codec_options;
    int i, err;

    switch (avctx->profile) {
    case FF_PROFILE_H264_CONSTRAINED_BASELINE:
        ctx->va_profile = VAProfileH264ConstrainedBaseline;
        break;
    case FF_PROFILE_H264_BASELINE:
        ctx->va_profile = VAProfileH264Baseline;
        break;
    case FF_PROFILE_H264_MAIN:
        ctx->va_profile = VAProfileH264Main;
        break;
    case FF_PROFILE_H264_EXTENDED:
        av_log(avctx, AV_LOG_ERROR, "H.264 extended profile "
               "is not supported.\n");
        return AVERROR_PATCHWELCOME;
    case FF_PROFILE_UNKNOWN:
    case FF_PROFILE_H264_HIGH:
        ctx->va_profile = VAProfileH264High;
        break;
    case FF_PROFILE_H264_HIGH_10:
    case FF_PROFILE_H264_HIGH_10_INTRA:
        av_log(avctx, AV_LOG_ERROR, "H.264 10-bit profiles "
               "are not supported.\n");
        return AVERROR_PATCHWELCOME;
    case FF_PROFILE_H264_HIGH_422:
    case FF_PROFILE_H264_HIGH_422_INTRA:
    case FF_PROFILE_H264_HIGH_444:
    case FF_PROFILE_H264_HIGH_444_PREDICTIVE:
    case FF_PROFILE_H264_HIGH_444_INTRA:
    case FF_PROFILE_H264_CAVLC_444:
        av_log(avctx, AV_LOG_ERROR, "H.264 non-4:2:0 profiles "
               "are not supported.\n");
        return AVERROR_PATCHWELCOME;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown H.264 profile %d.\n",
               avctx->profile);
        return AVERROR(EINVAL);
    }
    ctx->va_entrypoint = VAEntrypointEncSlice;

    ctx->input_width    = avctx->width;
    ctx->input_height   = avctx->height;
    ctx->aligned_width  = FFALIGN(ctx->input_width,  16);
    ctx->aligned_height = FFALIGN(ctx->input_height, 16);
    priv->mb_width      = ctx->aligned_width  / 16;
    priv->mb_height     = ctx->aligned_height / 16;

    for (i = 0; i < FF_ARRAY_ELEMS(default_config_attributes); i++) {
        ctx->config_attributes[ctx->nb_config_attributes++] =
            default_config_attributes[i];
    }

    if (avctx->bit_rate > 0) {
        ctx->va_rc_mode = VA_RC_CBR;
        err = vaapi_encode_h264_init_constant_bitrate(avctx);
    } else {
        ctx->va_rc_mode = VA_RC_CQP;
        err = vaapi_encode_h264_init_fixed_qp(avctx);
    }
    if (err < 0)
        return err;

    ctx->config_attributes[ctx->nb_config_attributes++] = (VAConfigAttrib) {
        .type  = VAConfigAttribRateControl,
        .value = ctx->va_rc_mode,
    };

    if (opt->quality > 0) {
#if VA_CHECK_VERSION(0, 36, 0)
        priv->quality_params.misc.type =
            VAEncMiscParameterTypeQualityLevel;
        priv->quality_params.quality.quality_level = opt->quality;

        ctx->global_params[ctx->nb_global_params] =
            &priv->quality_params.misc;
        ctx->global_params_size[ctx->nb_global_params++] =
            sizeof(priv->quality_params);
#else
        av_log(avctx, AV_LOG_WARNING, "The encode quality option is not "
               "supported with this VAAPI version.\n");
#endif
    }

    ctx->nb_recon_frames = 20;

    return 0;
}

static VAAPIEncodeType vaapi_encode_type_h264 = {
    .priv_data_size        = sizeof(VAAPIEncodeH264Context),

    .init                  = &vaapi_encode_h264_init_internal,

    .sequence_params_size  = sizeof(VAEncSequenceParameterBufferH264),
    .init_sequence_params  = &vaapi_encode_h264_init_sequence_params,

    .picture_params_size   = sizeof(VAEncPictureParameterBufferH264),
    .init_picture_params   = &vaapi_encode_h264_init_picture_params,

    .slice_params_size     = sizeof(VAEncSliceParameterBufferH264),
    .init_slice_params     = &vaapi_encode_h264_init_slice_params,

    .sequence_header_type  = VAEncPackedHeaderSequence,
    .write_sequence_header = &vaapi_encode_h264_write_sequence_header,

    .slice_header_type     = VAEncPackedHeaderH264_Slice,
    .write_slice_header    = &vaapi_encode_h264_write_slice_header,
};

static av_cold int vaapi_encode_h264_init(AVCodecContext *avctx)
{
    return ff_vaapi_encode_init(avctx, &vaapi_encode_type_h264);
}

#define OFFSET(x) (offsetof(VAAPIEncodeContext, codec_options_data) + \
                   offsetof(VAAPIEncodeH264Options, x))
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM)
static const AVOption vaapi_encode_h264_options[] = {
    { "qp", "Constant QP (for P-frames; scaled by qfactor/qoffset for I/B)",
      OFFSET(qp), AV_OPT_TYPE_INT, { .i64 = 20 }, 0, 52, FLAGS },
    { "quality", "Set encode quality (trades off against speed, higher is faster)",
      OFFSET(quality), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 2, FLAGS },
    { NULL },
};

static const AVCodecDefault vaapi_encode_h264_defaults[] = {
    { "profile",        "100" },
    { "level",          "51"  },
    { "b",              "0"   },
    { "bf",             "2"   },
    { "g",              "120" },
    { "i_qfactor",      "1.0" },
    { "i_qoffset",      "0.0" },
    { "b_qfactor",      "1.2" },
    { "b_qoffset",      "0.0" },
    { NULL },
};

static const AVClass vaapi_encode_h264_class = {
    .class_name = "h264_vaapi",
    .item_name  = av_default_item_name,
    .option     = vaapi_encode_h264_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_h264_vaapi_encoder = {
    .name           = "h264_vaapi",
    .long_name      = NULL_IF_CONFIG_SMALL("H.264/AVC (VAAPI)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .priv_data_size = (sizeof(VAAPIEncodeContext) +
                       sizeof(VAAPIEncodeH264Options)),
    .init           = &vaapi_encode_h264_init,
    .encode2        = &ff_vaapi_encode2,
    .close          = &ff_vaapi_encode_close,
    .priv_class     = &vaapi_encode_h264_class,
    .capabilities   = AV_CODEC_CAP_DELAY,
    .defaults       = vaapi_encode_h264_defaults,
    .pix_fmts = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_VAAPI,
        AV_PIX_FMT_NONE,
    },
};
