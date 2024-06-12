/*
 * MPEG-H Part 2 / HEVC / H.265 HW decode acceleration through VDPAU
 *
 * Copyright (c) 2013 Philip Langdale
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
 * License along with FFmpeg; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <vdpau/vdpau.h>

#include "avcodec.h"
#include "hevc/data.h"
#include "hevc/hevcdec.h"
#include "hwaccel_internal.h"
#include "vdpau.h"
#include "vdpau_internal.h"
#include "h265_profile_level.h"


static int vdpau_hevc_start_frame(AVCodecContext *avctx,
                                  const uint8_t *buffer, uint32_t size)
{
    HEVCContext *h = avctx->priv_data;
    const HEVCLayerContext *l = &h->layers[h->cur_layer];
    HEVCFrame *pic = h->cur_frame;
    struct vdpau_picture_context *pic_ctx = pic->hwaccel_picture_private;

    VdpPictureInfoHEVC *info = &pic_ctx->info.hevc;
#ifdef VDP_YCBCR_FORMAT_Y_U_V_444
    VdpPictureInfoHEVC444 *info2 = &pic_ctx->info.hevc_444;
#endif

    const HEVCPPS *pps = h->pps;
    const HEVCSPS *sps = pps->sps;
    const SliceHeader *sh = &h->sh;
    const ScalingList *sl = pps->scaling_list_data_present_flag ?
                            &pps->scaling_list : &sps->scaling_list;

    /* init VdpPictureInfoHEVC */

    /* SPS */
    info->chroma_format_idc = sps->chroma_format_idc;
    info->separate_colour_plane_flag = sps->separate_colour_plane;
    info->pic_width_in_luma_samples = sps->width;
    info->pic_height_in_luma_samples = sps->height;
    info->bit_depth_luma_minus8 = sps->bit_depth - 8;
    info->bit_depth_chroma_minus8 = sps->bit_depth - 8;
    info->log2_max_pic_order_cnt_lsb_minus4 = sps->log2_max_poc_lsb - 4;
    /* Provide the value corresponding to the nuh_temporal_id of the frame
       to be decoded. */
    info->sps_max_dec_pic_buffering_minus1 = sps->temporal_layer[sps->max_sub_layers - 1].max_dec_pic_buffering - 1;
    info->log2_min_luma_coding_block_size_minus3 = sps->log2_min_cb_size - 3;
    info->log2_diff_max_min_luma_coding_block_size = sps->log2_diff_max_min_coding_block_size;
    info->log2_min_transform_block_size_minus2 = sps->log2_min_tb_size - 2;
    info->log2_diff_max_min_transform_block_size = sps->log2_max_trafo_size - sps->log2_min_tb_size;
    info->max_transform_hierarchy_depth_inter = sps->max_transform_hierarchy_depth_inter;
    info->max_transform_hierarchy_depth_intra = sps->max_transform_hierarchy_depth_intra;
    info->scaling_list_enabled_flag = sps->scaling_list_enabled;
    /* Scaling lists, in diagonal order, to be used for this frame. */
    for (size_t i = 0; i < 6; i++) {
        for (size_t j = 0; j < 16; j++) {
            /* Scaling List for 4x4 quantization matrix,
               indexed as ScalingList4x4[matrixId][i]. */
            uint8_t pos = 4 * ff_hevc_diag_scan4x4_y[j] + ff_hevc_diag_scan4x4_x[j];
            info->ScalingList4x4[i][j] = sl->sl[0][i][pos];
        }
        for (size_t j = 0; j < 64; j++) {
            uint8_t pos = 8 * ff_hevc_diag_scan8x8_y[j] + ff_hevc_diag_scan8x8_x[j];
            /* Scaling List for 8x8 quantization matrix,
               indexed as ScalingList8x8[matrixId][i]. */
            info->ScalingList8x8[i][j] = sl->sl[1][i][pos];
            /* Scaling List for 16x16 quantization matrix,
               indexed as ScalingList16x16[matrixId][i]. */
            info->ScalingList16x16[i][j] = sl->sl[2][i][pos];
            if (i < 2) {
                /* Scaling List for 32x32 quantization matrix,
                   indexed as ScalingList32x32[matrixId][i]. */
                info->ScalingList32x32[i][j] = sl->sl[3][i * 3][pos];
            }
        }
        /* Scaling List DC Coefficients for 16x16,
           indexed as ScalingListDCCoeff16x16[matrixId]. */
        info->ScalingListDCCoeff16x16[i] = sl->sl_dc[0][i];
        if (i < 2) {
            /* Scaling List DC Coefficients for 32x32,
               indexed as ScalingListDCCoeff32x32[matrixId]. */
            info->ScalingListDCCoeff32x32[i] = sl->sl_dc[1][i * 3];
        }
    }
    info->amp_enabled_flag = sps->amp_enabled;
    info->sample_adaptive_offset_enabled_flag = sps->sao_enabled;
    info->pcm_enabled_flag = sps->pcm_enabled;
    if (info->pcm_enabled_flag) {
        /* Only needs to be set if pcm_enabled_flag is set. Ignored otherwise. */
        info->pcm_sample_bit_depth_luma_minus1 = sps->pcm.bit_depth - 1;
        /* Only needs to be set if pcm_enabled_flag is set. Ignored otherwise. */
        info->pcm_sample_bit_depth_chroma_minus1 = sps->pcm.bit_depth_chroma - 1;
        /* Only needs to be set if pcm_enabled_flag is set. Ignored otherwise. */
        info->log2_min_pcm_luma_coding_block_size_minus3 = sps->pcm.log2_min_pcm_cb_size - 3;
        /* Only needs to be set if pcm_enabled_flag is set. Ignored otherwise. */
        info->log2_diff_max_min_pcm_luma_coding_block_size = sps->pcm.log2_max_pcm_cb_size - sps->pcm.log2_min_pcm_cb_size;
        /* Only needs to be set if pcm_enabled_flag is set. Ignored otherwise. */
        info->pcm_loop_filter_disabled_flag = sps->pcm_loop_filter_disabled;
    }
    /* Per spec, when zero, assume short_term_ref_pic_set_sps_flag
       is also zero. */
    info->num_short_term_ref_pic_sets = sps->nb_st_rps;
    info->long_term_ref_pics_present_flag = sps->long_term_ref_pics_present;
    /* Only needed if long_term_ref_pics_present_flag is set. Ignored
       otherwise. */
    info->num_long_term_ref_pics_sps = sps->num_long_term_ref_pics_sps;
    info->sps_temporal_mvp_enabled_flag = sps->temporal_mvp_enabled;
    info->strong_intra_smoothing_enabled_flag = sps->strong_intra_smoothing_enabled;

    /* Copy the HEVC Picture Parameter Set bitstream fields. */
    info->dependent_slice_segments_enabled_flag = pps->dependent_slice_segments_enabled_flag;
    info->output_flag_present_flag = pps->output_flag_present_flag;
    info->num_extra_slice_header_bits = pps->num_extra_slice_header_bits;
    info->sign_data_hiding_enabled_flag = pps->sign_data_hiding_flag;
    info->cabac_init_present_flag = pps->cabac_init_present_flag;
    info->num_ref_idx_l0_default_active_minus1 = pps->num_ref_idx_l0_default_active - 1;
    info->num_ref_idx_l1_default_active_minus1 = pps->num_ref_idx_l1_default_active - 1;
    info->init_qp_minus26 = pps->pic_init_qp_minus26;
    info->constrained_intra_pred_flag = pps->constrained_intra_pred_flag;
    info->transform_skip_enabled_flag = pps->transform_skip_enabled_flag;
    info->cu_qp_delta_enabled_flag = pps->cu_qp_delta_enabled_flag;
    /* Only needed if cu_qp_delta_enabled_flag is set. Ignored otherwise. */
    info->diff_cu_qp_delta_depth = pps->diff_cu_qp_delta_depth;
    info->pps_cb_qp_offset = pps->cb_qp_offset;
    info->pps_cr_qp_offset = pps->cr_qp_offset;
    info->pps_slice_chroma_qp_offsets_present_flag = pps->pic_slice_level_chroma_qp_offsets_present_flag;
    info->weighted_pred_flag = pps->weighted_pred_flag;
    info->weighted_bipred_flag = pps->weighted_bipred_flag;
    info->transquant_bypass_enabled_flag = pps->transquant_bypass_enable_flag;
    info->tiles_enabled_flag = pps->tiles_enabled_flag;
    info->entropy_coding_sync_enabled_flag = pps->entropy_coding_sync_enabled_flag;
    if (info->tiles_enabled_flag) {
        /* Only valid if tiles_enabled_flag is set. Ignored otherwise. */
        info->num_tile_columns_minus1 = pps->num_tile_columns - 1;
        /* Only valid if tiles_enabled_flag is set. Ignored otherwise. */
        info->num_tile_rows_minus1 = pps->num_tile_rows - 1;
        /* Only valid if tiles_enabled_flag is set. Ignored otherwise. */
        info->uniform_spacing_flag = pps->uniform_spacing_flag;
        /* Only need to set 0..num_tile_columns_minus1. The struct
           definition reserves up to the maximum of 20. Invalid values are
           ignored. */
        for (ssize_t i = 0; i < pps->num_tile_columns; i++) {
            info->column_width_minus1[i] = pps->column_width[i] - 1;
        }
        /* Only need to set 0..num_tile_rows_minus1. The struct
           definition reserves up to the maximum of 22. Invalid values are
           ignored.*/
        for (ssize_t i = 0; i < pps->num_tile_rows; i++) {
            info->row_height_minus1[i] = pps->row_height[i] - 1;
        }
        /* Only needed if tiles_enabled_flag is set. Invalid values are
           ignored. */
        info->loop_filter_across_tiles_enabled_flag = pps->loop_filter_across_tiles_enabled_flag;
    }
    info->pps_loop_filter_across_slices_enabled_flag = pps->seq_loop_filter_across_slices_enabled_flag;
    info->deblocking_filter_control_present_flag = pps->deblocking_filter_control_present_flag;
    /* Only valid if deblocking_filter_control_present_flag is set. Ignored
       otherwise. */
    info->deblocking_filter_override_enabled_flag = pps->deblocking_filter_override_enabled_flag;
    /* Only valid if deblocking_filter_control_present_flag is set. Ignored
       otherwise. */
    info->pps_deblocking_filter_disabled_flag = pps->disable_dbf;
    /* Only valid if deblocking_filter_control_present_flag is set and
       pps_deblocking_filter_disabled_flag is not set. Ignored otherwise.*/
    info->pps_beta_offset_div2 = pps->beta_offset / 2;
    /* Only valid if deblocking_filter_control_present_flag is set and
       pps_deblocking_filter_disabled_flag is not set. Ignored otherwise. */
    info->pps_tc_offset_div2 = pps->tc_offset / 2;
    info->lists_modification_present_flag = pps->lists_modification_present_flag;
    info->log2_parallel_merge_level_minus2 = pps->log2_parallel_merge_level - 2;
    info->slice_segment_header_extension_present_flag = pps->slice_header_extension_present_flag;

    /* Set to 1 if nal_unit_type is equal to IDR_W_RADL or IDR_N_LP.
       Set to zero otherwise. */
    info->IDRPicFlag = IS_IDR(h);
    /* Set to 1 if nal_unit_type in the range of BLA_W_LP to
       RSV_IRAP_VCL23, inclusive. Set to zero otherwise.*/
    info->RAPPicFlag = IS_IRAP(h);
    /* See section 7.4.7.1 of the specification. */
    info->CurrRpsIdx = sps->nb_st_rps;
    if (sh->short_term_ref_pic_set_sps_flag == 1) {
        for (size_t i = 0; i < sps->nb_st_rps; i++) {
            if (sh->short_term_rps == &sps->st_rps[i]) {
                info->CurrRpsIdx = i;
                break;
            }
        }
    }
    /* See section 7.4.7.2 of the specification. */
    info->NumPocTotalCurr = ff_hevc_frame_nb_refs(&h->sh, pps, h->cur_layer);
    if (sh->short_term_ref_pic_set_sps_flag == 0 && sh->short_term_rps) {
        /* Corresponds to specification field, NumDeltaPocs[RefRpsIdx].
           Only applicable when short_term_ref_pic_set_sps_flag == 0.
           Implementations will ignore this value in other cases. See 7.4.8. */
        info->NumDeltaPocsOfRefRpsIdx = sh->short_term_rps->rps_idx_num_delta_pocs;
    }
    /* Section 7.6.3.1 of the H.265/HEVC Specification defines the syntax of
       the slice_segment_header. This header contains information that
       some VDPAU implementations may choose to skip. The VDPAU API
       requires client applications to track the number of bits used in the
       slice header for structures associated with short term and long term
       reference pictures. First, VDPAU requires the number of bits used by
       the short_term_ref_pic_set array in the slice_segment_header. */
    info->NumShortTermPictureSliceHeaderBits = sh->short_term_ref_pic_set_size;
    /* Second, VDPAU requires the number of bits used for long term reference
       pictures in the slice_segment_header. This is equal to the number
       of bits used for the contents of the block beginning with
       "if(long_term_ref_pics_present_flag)". */
    info->NumLongTermPictureSliceHeaderBits = sh->long_term_ref_pic_set_size;

    /* The value of PicOrderCntVal of the picture in the access unit
       containing the SEI message. The picture being decoded. */
    info->CurrPicOrderCntVal = h->poc;

    /* Slice Decoding Process - Reference Picture Sets */
    for (size_t i = 0; i < 16; i++) {
        info->RefPics[i] = VDP_INVALID_HANDLE;
        info->PicOrderCntVal[i] = 0;
        info->IsLongTerm[i] = 0;
    }
    for (size_t i = 0, j = 0; i < FF_ARRAY_ELEMS(l->DPB); i++) {
        const HEVCFrame *frame = &l->DPB[i];
        if (frame != h->cur_frame && (frame->flags & (HEVC_FRAME_FLAG_LONG_REF |
                                                HEVC_FRAME_FLAG_SHORT_REF))) {
            if (j > 15) {
                av_log(avctx, AV_LOG_WARNING,
                     "VDPAU only supports up to 16 references in the DPB. "
                     "This frame may not be decoded correctly.\n");
                break;
            }
            /* Array of video reference surfaces.
               Set any unused positions to VDP_INVALID_HANDLE. */
            info->RefPics[j] = ff_vdpau_get_surface_id(frame->f);
            /* Array of picture order counts. These correspond to positions
               in the RefPics array. */
            info->PicOrderCntVal[j] = frame->poc;
            /* Array used to specify whether a particular RefPic is
               a long term reference. A value of "1" indicates a long-term
               reference. */
            // XXX: Setting this caused glitches in the nvidia implementation
            // Always setting it to zero, produces correct results
            //info->IsLongTerm[j] = frame->flags & HEVC_FRAME_FLAG_LONG_REF;
            info->IsLongTerm[j] = 0;
            j++;
        }
    }
    /* Copy of specification field, see Section 8.3.2 of the
       H.265/HEVC Specification. */
    info->NumPocStCurrBefore = h->rps[ST_CURR_BEF].nb_refs;
    if (info->NumPocStCurrBefore > 8) {
        av_log(avctx, AV_LOG_WARNING,
             "VDPAU only supports up to 8 references in StCurrBefore. "
             "This frame may not be decoded correctly.\n");
        info->NumPocStCurrBefore = 8;
    }
    /* Copy of specification field, see Section 8.3.2 of the
       H.265/HEVC Specification. */
    info->NumPocStCurrAfter = h->rps[ST_CURR_AFT].nb_refs;
    if (info->NumPocStCurrAfter > 8) {
        av_log(avctx, AV_LOG_WARNING,
             "VDPAU only supports up to 8 references in StCurrAfter. "
             "This frame may not be decoded correctly.\n");
        info->NumPocStCurrAfter = 8;
    }
    /* Copy of specification field, see Section 8.3.2 of the
       H.265/HEVC Specification. */
    info->NumPocLtCurr = h->rps[LT_CURR].nb_refs;
    if (info->NumPocLtCurr > 8) {
        av_log(avctx, AV_LOG_WARNING,
             "VDPAU only supports up to 8 references in LtCurr. "
             "This frame may not be decoded correctly.\n");
        info->NumPocLtCurr = 8;
    }
    /* Reference Picture Set list, one of the short-term RPS. These
       correspond to positions in the RefPics array. */
    for (ssize_t i = 0, j = 0; i < h->rps[ST_CURR_BEF].nb_refs; i++) {
        HEVCFrame *frame = h->rps[ST_CURR_BEF].ref[i];
        if (frame) {
            uint8_t found = 0;
            uintptr_t id = ff_vdpau_get_surface_id(frame->f);
            for (size_t k = 0; k < 16; k++) {
                if (id == info->RefPics[k]) {
                    info->RefPicSetStCurrBefore[j] = k;
                    j++;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                av_log(avctx, AV_LOG_WARNING, "missing surface: %p\n",
                       (void *)id);
            }
        } else {
            av_log(avctx, AV_LOG_WARNING, "missing STR Before frame: %zd\n", i);
        }
    }
    /* Reference Picture Set list, one of the short-term RPS. These
       correspond to positions in the RefPics array. */
    for (ssize_t i = 0, j = 0; i < h->rps[ST_CURR_AFT].nb_refs; i++) {
        HEVCFrame *frame = h->rps[ST_CURR_AFT].ref[i];
        if (frame) {
            uint8_t found = 0;
            uintptr_t id = ff_vdpau_get_surface_id(frame->f);
            for (size_t k = 0; k < 16; k++) {
                if (id == info->RefPics[k]) {
                    info->RefPicSetStCurrAfter[j] = k;
                    j++;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                av_log(avctx, AV_LOG_WARNING, "missing surface: %p\n",
                       (void *)id);
            }
        } else {
            av_log(avctx, AV_LOG_WARNING, "missing STR After frame: %zd\n", i);
        }
    }
    /* Reference Picture Set list, one of the long-term RPS. These
       correspond to positions in the RefPics array. */
    for (ssize_t i = 0, j = 0; i < h->rps[LT_CURR].nb_refs; i++) {
        HEVCFrame *frame = h->rps[LT_CURR].ref[i];
        if (frame) {
            uint8_t found = 0;
            uintptr_t id = ff_vdpau_get_surface_id(frame->f);
            for (size_t k = 0; k < 16; k++) {
                if (id == info->RefPics[k]) {
                    info->RefPicSetLtCurr[j] = k;
                    j++;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                av_log(avctx, AV_LOG_WARNING, "missing surface: %p\n",
                       (void *)id);
            }
        } else {
            av_log(avctx, AV_LOG_WARNING, "missing LTR frame: %zd\n", i);
        }
    }

#ifdef VDP_YCBCR_FORMAT_Y_U_V_444
    if (sps->range_extension) {
        info2->sps_range_extension_flag             = 1;
        info2->transformSkipRotationEnableFlag      = sps->transform_skip_rotation_enabled;
        info2->transformSkipContextEnableFlag       = sps->transform_skip_context_enabled;
        info2->implicitRdpcmEnableFlag              = sps->implicit_rdpcm_enabled;
        info2->explicitRdpcmEnableFlag              = sps->explicit_rdpcm_enabled;
        info2->extendedPrecisionProcessingFlag      = sps->extended_precision_processing;
        info2->intraSmoothingDisabledFlag           = sps->intra_smoothing_disabled;
        info2->highPrecisionOffsetsEnableFlag       = sps->high_precision_offsets_enabled;
        info2->persistentRiceAdaptationEnableFlag   = sps->persistent_rice_adaptation_enabled;
        info2->cabacBypassAlignmentEnableFlag       = sps->cabac_bypass_alignment_enabled;
    } else {
        info2->sps_range_extension_flag = 0;
    }
    if (pps->pps_range_extensions_flag) {
        info2->pps_range_extension_flag             = 1;
        info2->log2MaxTransformSkipSize             = pps->log2_max_transform_skip_block_size;
        info2->crossComponentPredictionEnableFlag   = pps->cross_component_prediction_enabled_flag;
        info2->chromaQpAdjustmentEnableFlag         = pps->chroma_qp_offset_list_enabled_flag;
        info2->diffCuChromaQpAdjustmentDepth        = pps->diff_cu_chroma_qp_offset_depth;
        info2->chromaQpAdjustmentTableSize          = pps->chroma_qp_offset_list_len_minus1 + 1;
        info2->log2SaoOffsetScaleLuma               = pps->log2_sao_offset_scale_luma;
        info2->log2SaoOffsetScaleChroma             = pps->log2_sao_offset_scale_chroma;
        for (ssize_t i = 0; i < info2->chromaQpAdjustmentTableSize; i++)
        {
            info2->cb_qp_adjustment[i] = pps->cb_qp_offset_list[i];
            info2->cr_qp_adjustment[i] = pps->cr_qp_offset_list[i];
        }

    } else {
        info2->pps_range_extension_flag = 0;
    }
#endif

    return ff_vdpau_common_start_frame(pic_ctx, buffer, size);
}

static const uint8_t start_code_prefix[3] = { 0x00, 0x00, 0x01 };

static int vdpau_hevc_decode_slice(AVCodecContext *avctx,
                                   const uint8_t *buffer, uint32_t size)
{
    HEVCContext *h = avctx->priv_data;
    struct vdpau_picture_context *pic_ctx = h->cur_frame->hwaccel_picture_private;
    int val;

    val = ff_vdpau_add_buffer(pic_ctx, start_code_prefix, 3);
    if (val)
        return val;

    val = ff_vdpau_add_buffer(pic_ctx, buffer, size);
    if (val)
        return val;

    return 0;
}

static int vdpau_hevc_end_frame(AVCodecContext *avctx)
{
    HEVCContext *h = avctx->priv_data;
    struct vdpau_picture_context *pic_ctx = h->cur_frame->hwaccel_picture_private;
    int val;

    val = ff_vdpau_common_end_frame(avctx, h->cur_frame->f, pic_ctx);
    if (val < 0)
        return val;

    return 0;
}



static int ptl_convert(const PTLCommon *general_ptl, H265RawProfileTierLevel *h265_raw_ptl)
{
    h265_raw_ptl->general_profile_space = general_ptl->profile_space;
    h265_raw_ptl->general_tier_flag     = general_ptl->tier_flag;
    h265_raw_ptl->general_profile_idc   = general_ptl->profile_idc;

    memcpy(h265_raw_ptl->general_profile_compatibility_flag,
                                  general_ptl->profile_compatibility_flag, 32 * sizeof(uint8_t));

#define copy_field(name) h265_raw_ptl->general_ ## name = general_ptl->name
    copy_field(progressive_source_flag);
    copy_field(interlaced_source_flag);
    copy_field(non_packed_constraint_flag);
    copy_field(frame_only_constraint_flag);
    copy_field(max_12bit_constraint_flag);
    copy_field(max_10bit_constraint_flag);
    copy_field(max_8bit_constraint_flag);
    copy_field(max_422chroma_constraint_flag);
    copy_field(max_420chroma_constraint_flag);
    copy_field(max_monochrome_constraint_flag);
    copy_field(intra_constraint_flag);
    copy_field(one_picture_only_constraint_flag);
    copy_field(lower_bit_rate_constraint_flag);
    copy_field(max_14bit_constraint_flag);
    copy_field(inbld_flag);
    copy_field(level_idc);
#undef copy_field

    return 0;
}

/*
 * Find exact vdpau_profile for HEVC Range Extension
 */
static int vdpau_hevc_parse_rext_profile(AVCodecContext *avctx, VdpDecoderProfile *vdp_profile)
{
    const HEVCContext *h = avctx->priv_data;
    const HEVCSPS *sps = h->pps->sps;
    const PTL *ptl = &sps->ptl;
    const PTLCommon *general_ptl = &ptl->general_ptl;
    const H265ProfileDescriptor *profile;
    H265RawProfileTierLevel h265_raw_ptl = {0};

    /* convert PTLCommon to H265RawProfileTierLevel */
    ptl_convert(general_ptl, &h265_raw_ptl);

    profile = ff_h265_get_profile(&h265_raw_ptl);
    if (!profile) {
        av_log(avctx, AV_LOG_WARNING, "HEVC profile is not found.\n");
        if (avctx->hwaccel_flags & AV_HWACCEL_FLAG_ALLOW_PROFILE_MISMATCH) {
            // Default to selecting Main profile if profile mismatch is allowed
            *vdp_profile = VDP_DECODER_PROFILE_HEVC_MAIN;
            return 0;
        } else
            return AVERROR(ENOTSUP);
    }

    if (!strcmp(profile->name, "Main 12") ||
        !strcmp(profile->name, "Main 12 Intra"))
        *vdp_profile = VDP_DECODER_PROFILE_HEVC_MAIN_12;
#ifdef VDP_DECODER_PROFILE_HEVC_MAIN_444
    else if (!strcmp(profile->name, "Main 4:4:4") ||
             !strcmp(profile->name, "Main 4:4:4 Intra"))
        *vdp_profile = VDP_DECODER_PROFILE_HEVC_MAIN_444;
#endif
#ifdef VDP_DECODER_PROFILE_HEVC_MAIN_444_10
    else if (!strcmp(profile->name, "Main 4:4:4 10") ||
             !strcmp(profile->name, "Main 4:4:4 10 Intra"))
        *vdp_profile = VDP_DECODER_PROFILE_HEVC_MAIN_444_10;
    else if (!strcmp(profile->name, "Main 4:4:4 12") ||
             !strcmp(profile->name, "Main 4:4:4 12 Intra"))
        *vdp_profile = VDP_DECODER_PROFILE_HEVC_MAIN_444_12;
#endif
    else
        return AVERROR(ENOTSUP);

    return 0;
}


static int vdpau_hevc_init(AVCodecContext *avctx)
{
    VdpDecoderProfile profile;
    uint32_t level = avctx->level;
    int ret;

    switch (avctx->profile) {
    case AV_PROFILE_HEVC_MAIN:
        profile = VDP_DECODER_PROFILE_HEVC_MAIN;
        break;
    case AV_PROFILE_HEVC_MAIN_10:
        profile = VDP_DECODER_PROFILE_HEVC_MAIN_10;
        break;
    case AV_PROFILE_HEVC_MAIN_STILL_PICTURE:
        profile = VDP_DECODER_PROFILE_HEVC_MAIN_STILL;
        break;
    case AV_PROFILE_HEVC_REXT:
        ret = vdpau_hevc_parse_rext_profile(avctx, &profile);
        if (ret)
            return AVERROR(ENOTSUP);
        break;
    default:
        return AVERROR(ENOTSUP);
    }

    return ff_vdpau_common_init(avctx, profile, level);
}

const FFHWAccel ff_hevc_vdpau_hwaccel = {
    .p.name         = "hevc_vdpau",
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_HEVC,
    .p.pix_fmt      = AV_PIX_FMT_VDPAU,
    .start_frame    = vdpau_hevc_start_frame,
    .end_frame      = vdpau_hevc_end_frame,
    .decode_slice   = vdpau_hevc_decode_slice,
    .frame_priv_data_size = sizeof(struct vdpau_picture_context),
    .init           = vdpau_hevc_init,
    .uninit         = ff_vdpau_common_uninit,
    .frame_params   = ff_vdpau_common_frame_params,
    .priv_data_size = sizeof(VDPAUContext),
    .caps_internal  = HWACCEL_CAP_ASYNC_SAFE,
};
