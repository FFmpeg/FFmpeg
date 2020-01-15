/*
 * HEVC HW decode acceleration through VA API
 *
 * Copyright (C) 2015 Timo Rothenpieler <timo@rothenpieler.org>
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

#include <va/va.h>
#include <va/va_dec_hevc.h>

#include "avcodec.h"
#include "hevcdec.h"
#include "hwaccel.h"
#include "vaapi_decode.h"
#include "vaapi_hevc.h"
#include "h265_profile_level.h"

typedef struct VAAPIDecodePictureHEVC {
#if VA_CHECK_VERSION(1, 2, 0)
    VAPictureParameterBufferHEVCExtension pic_param;
    VASliceParameterBufferHEVCExtension last_slice_param;
#else
    VAPictureParameterBufferHEVC pic_param;
    VASliceParameterBufferHEVC last_slice_param;
#endif
    const uint8_t *last_buffer;
    size_t         last_size;

    VAAPIDecodePicture pic;
} VAAPIDecodePictureHEVC;

static void init_vaapi_pic(VAPictureHEVC *va_pic)
{
    va_pic->picture_id    = VA_INVALID_ID;
    va_pic->flags         = VA_PICTURE_HEVC_INVALID;
    va_pic->pic_order_cnt = 0;
}

static void fill_vaapi_pic(VAPictureHEVC *va_pic, const HEVCFrame *pic, int rps_type)
{
    va_pic->picture_id    = ff_vaapi_get_surface_id(pic->frame);
    va_pic->pic_order_cnt = pic->poc;
    va_pic->flags         = rps_type;

    if (pic->flags & HEVC_FRAME_FLAG_LONG_REF)
        va_pic->flags |= VA_PICTURE_HEVC_LONG_TERM_REFERENCE;

    if (pic->frame->interlaced_frame) {
        va_pic->flags |= VA_PICTURE_HEVC_FIELD_PIC;

        if (!pic->frame->top_field_first)
            va_pic->flags |= VA_PICTURE_HEVC_BOTTOM_FIELD;
    }
}

static int find_frame_rps_type(const HEVCContext *h, const HEVCFrame *pic)
{
    VASurfaceID pic_surf = ff_vaapi_get_surface_id(pic->frame);
    int i;

    for (i = 0; i < h->rps[ST_CURR_BEF].nb_refs; i++) {
        if (pic_surf == ff_vaapi_get_surface_id(h->rps[ST_CURR_BEF].ref[i]->frame))
            return VA_PICTURE_HEVC_RPS_ST_CURR_BEFORE;
    }

    for (i = 0; i < h->rps[ST_CURR_AFT].nb_refs; i++) {
        if (pic_surf == ff_vaapi_get_surface_id(h->rps[ST_CURR_AFT].ref[i]->frame))
            return VA_PICTURE_HEVC_RPS_ST_CURR_AFTER;
    }

    for (i = 0; i < h->rps[LT_CURR].nb_refs; i++) {
        if (pic_surf == ff_vaapi_get_surface_id(h->rps[LT_CURR].ref[i]->frame))
            return VA_PICTURE_HEVC_RPS_LT_CURR;
    }

    return 0;
}

static void fill_vaapi_reference_frames(const HEVCContext *h, VAPictureParameterBufferHEVC *pp)
{
    const HEVCFrame *current_picture = h->ref;
    int i, j, rps_type;

    for (i = 0, j = 0; i < FF_ARRAY_ELEMS(pp->ReferenceFrames); i++) {
        const HEVCFrame *frame = NULL;

        while (!frame && j < FF_ARRAY_ELEMS(h->DPB)) {
            if (&h->DPB[j] != current_picture && (h->DPB[j].flags & (HEVC_FRAME_FLAG_LONG_REF | HEVC_FRAME_FLAG_SHORT_REF)))
                frame = &h->DPB[j];
            j++;
        }

        init_vaapi_pic(&pp->ReferenceFrames[i]);

        if (frame) {
            rps_type = find_frame_rps_type(h, frame);
            fill_vaapi_pic(&pp->ReferenceFrames[i], frame, rps_type);
        }
    }
}

static int vaapi_hevc_start_frame(AVCodecContext          *avctx,
                                  av_unused const uint8_t *buffer,
                                  av_unused uint32_t       size)
{
    const HEVCContext        *h = avctx->priv_data;
    VAAPIDecodePictureHEVC *pic = h->ref->hwaccel_picture_private;
    const HEVCSPS          *sps = h->ps.sps;
    const HEVCPPS          *pps = h->ps.pps;

    const ScalingList *scaling_list = NULL;
    int pic_param_size, err, i;

    VAPictureParameterBufferHEVC *pic_param = (VAPictureParameterBufferHEVC *)&pic->pic_param;

    pic->pic.output_surface = ff_vaapi_get_surface_id(h->ref->frame);

    *pic_param = (VAPictureParameterBufferHEVC) {
        .pic_width_in_luma_samples                    = sps->width,
        .pic_height_in_luma_samples                   = sps->height,
        .log2_min_luma_coding_block_size_minus3       = sps->log2_min_cb_size - 3,
        .sps_max_dec_pic_buffering_minus1             = sps->temporal_layer[sps->max_sub_layers - 1].max_dec_pic_buffering - 1,
        .log2_diff_max_min_luma_coding_block_size     = sps->log2_diff_max_min_coding_block_size,
        .log2_min_transform_block_size_minus2         = sps->log2_min_tb_size - 2,
        .log2_diff_max_min_transform_block_size       = sps->log2_max_trafo_size  - sps->log2_min_tb_size,
        .max_transform_hierarchy_depth_inter          = sps->max_transform_hierarchy_depth_inter,
        .max_transform_hierarchy_depth_intra          = sps->max_transform_hierarchy_depth_intra,
        .num_short_term_ref_pic_sets                  = sps->nb_st_rps,
        .num_long_term_ref_pic_sps                    = sps->num_long_term_ref_pics_sps,
        .num_ref_idx_l0_default_active_minus1         = pps->num_ref_idx_l0_default_active - 1,
        .num_ref_idx_l1_default_active_minus1         = pps->num_ref_idx_l1_default_active - 1,
        .init_qp_minus26                              = pps->pic_init_qp_minus26,
        .pps_cb_qp_offset                             = pps->cb_qp_offset,
        .pps_cr_qp_offset                             = pps->cr_qp_offset,
        .pcm_sample_bit_depth_luma_minus1             = sps->pcm.bit_depth - 1,
        .pcm_sample_bit_depth_chroma_minus1           = sps->pcm.bit_depth_chroma - 1,
        .log2_min_pcm_luma_coding_block_size_minus3   = sps->pcm.log2_min_pcm_cb_size - 3,
        .log2_diff_max_min_pcm_luma_coding_block_size = sps->pcm.log2_max_pcm_cb_size - sps->pcm.log2_min_pcm_cb_size,
        .diff_cu_qp_delta_depth                       = pps->diff_cu_qp_delta_depth,
        .pps_beta_offset_div2                         = pps->beta_offset / 2,
        .pps_tc_offset_div2                           = pps->tc_offset / 2,
        .log2_parallel_merge_level_minus2             = pps->log2_parallel_merge_level - 2,
        .bit_depth_luma_minus8                        = sps->bit_depth - 8,
        .bit_depth_chroma_minus8                      = sps->bit_depth - 8,
        .log2_max_pic_order_cnt_lsb_minus4            = sps->log2_max_poc_lsb - 4,
        .num_extra_slice_header_bits                  = pps->num_extra_slice_header_bits,
        .pic_fields.bits = {
            .chroma_format_idc                          = sps->chroma_format_idc,
            .tiles_enabled_flag                         = pps->tiles_enabled_flag,
            .separate_colour_plane_flag                 = sps->separate_colour_plane_flag,
            .pcm_enabled_flag                           = sps->pcm_enabled_flag,
            .scaling_list_enabled_flag                  = sps->scaling_list_enable_flag,
            .transform_skip_enabled_flag                = pps->transform_skip_enabled_flag,
            .amp_enabled_flag                           = sps->amp_enabled_flag,
            .strong_intra_smoothing_enabled_flag        = sps->sps_strong_intra_smoothing_enable_flag,
            .sign_data_hiding_enabled_flag              = pps->sign_data_hiding_flag,
            .constrained_intra_pred_flag                = pps->constrained_intra_pred_flag,
            .cu_qp_delta_enabled_flag                   = pps->cu_qp_delta_enabled_flag,
            .weighted_pred_flag                         = pps->weighted_pred_flag,
            .weighted_bipred_flag                       = pps->weighted_bipred_flag,
            .transquant_bypass_enabled_flag             = pps->transquant_bypass_enable_flag,
            .entropy_coding_sync_enabled_flag           = pps->entropy_coding_sync_enabled_flag,
            .pps_loop_filter_across_slices_enabled_flag = pps->seq_loop_filter_across_slices_enabled_flag,
            .loop_filter_across_tiles_enabled_flag      = pps->loop_filter_across_tiles_enabled_flag,
            .pcm_loop_filter_disabled_flag              = sps->pcm.loop_filter_disable_flag,
        },
        .slice_parsing_fields.bits = {
            .lists_modification_present_flag             = pps->lists_modification_present_flag,
            .long_term_ref_pics_present_flag             = sps->long_term_ref_pics_present_flag,
            .sps_temporal_mvp_enabled_flag               = sps->sps_temporal_mvp_enabled_flag,
            .cabac_init_present_flag                     = pps->cabac_init_present_flag,
            .output_flag_present_flag                    = pps->output_flag_present_flag,
            .dependent_slice_segments_enabled_flag       = pps->dependent_slice_segments_enabled_flag,
            .pps_slice_chroma_qp_offsets_present_flag    = pps->pic_slice_level_chroma_qp_offsets_present_flag,
            .sample_adaptive_offset_enabled_flag         = sps->sao_enabled,
            .deblocking_filter_override_enabled_flag     = pps->deblocking_filter_override_enabled_flag,
            .pps_disable_deblocking_filter_flag          = pps->disable_dbf,
            .slice_segment_header_extension_present_flag = pps->slice_header_extension_present_flag,
            .RapPicFlag                                  = IS_IRAP(h),
            .IdrPicFlag                                  = IS_IDR(h),
            .IntraPicFlag                                = IS_IRAP(h),
        },
    };

    fill_vaapi_pic(&pic_param->CurrPic, h->ref, 0);
    fill_vaapi_reference_frames(h, pic_param);

    if (pps->tiles_enabled_flag) {
        pic_param->num_tile_columns_minus1 = pps->num_tile_columns - 1;
        pic_param->num_tile_rows_minus1    = pps->num_tile_rows - 1;

        for (i = 0; i < pps->num_tile_columns; i++)
            pic_param->column_width_minus1[i] = pps->column_width[i] - 1;

        for (i = 0; i < pps->num_tile_rows; i++)
            pic_param->row_height_minus1[i] = pps->row_height[i] - 1;
    }

    if (h->sh.short_term_ref_pic_set_sps_flag == 0 && h->sh.short_term_rps) {
        pic_param->st_rps_bits = h->sh.short_term_ref_pic_set_size;
    } else {
        pic_param->st_rps_bits = 0;
    }

#if VA_CHECK_VERSION(1, 2, 0)
    if (avctx->profile == FF_PROFILE_HEVC_REXT) {
        pic->pic_param.rext = (VAPictureParameterBufferHEVCRext) {
            .range_extension_pic_fields.bits  = {
                .transform_skip_rotation_enabled_flag       = sps->transform_skip_rotation_enabled_flag,
                .transform_skip_context_enabled_flag        = sps->transform_skip_context_enabled_flag,
                .implicit_rdpcm_enabled_flag                = sps->implicit_rdpcm_enabled_flag,
                .explicit_rdpcm_enabled_flag                = sps->explicit_rdpcm_enabled_flag,
                .extended_precision_processing_flag         = sps->extended_precision_processing_flag,
                .intra_smoothing_disabled_flag              = sps->intra_smoothing_disabled_flag,
                .high_precision_offsets_enabled_flag        = sps->high_precision_offsets_enabled_flag,
                .persistent_rice_adaptation_enabled_flag    = sps->persistent_rice_adaptation_enabled_flag,
                .cabac_bypass_alignment_enabled_flag        = sps->cabac_bypass_alignment_enabled_flag,
                .cross_component_prediction_enabled_flag    = pps->cross_component_prediction_enabled_flag,
                .chroma_qp_offset_list_enabled_flag         = pps->chroma_qp_offset_list_enabled_flag,
            },
            .diff_cu_chroma_qp_offset_depth                 = pps->diff_cu_chroma_qp_offset_depth,
            .chroma_qp_offset_list_len_minus1               = pps->chroma_qp_offset_list_len_minus1,
            .log2_sao_offset_scale_luma                     = pps->log2_sao_offset_scale_luma,
            .log2_sao_offset_scale_chroma                   = pps->log2_sao_offset_scale_chroma,
            .log2_max_transform_skip_block_size_minus2      = pps->log2_max_transform_skip_block_size - 2,
        };

        for (i = 0; i < 6; i++)
            pic->pic_param.rext.cb_qp_offset_list[i]        = pps->cb_qp_offset_list[i];
        for (i = 0; i < 6; i++)
            pic->pic_param.rext.cr_qp_offset_list[i]        = pps->cr_qp_offset_list[i];
    }
#endif
    pic_param_size = avctx->profile == FF_PROFILE_HEVC_REXT ?
                            sizeof(pic->pic_param) : sizeof(VAPictureParameterBufferHEVC);

    err = ff_vaapi_decode_make_param_buffer(avctx, &pic->pic,
                                            VAPictureParameterBufferType,
                                            &pic->pic_param, pic_param_size);
    if (err < 0)
        goto fail;

    if (pps->scaling_list_data_present_flag)
        scaling_list = &pps->scaling_list;
    else if (sps->scaling_list_enable_flag)
        scaling_list = &sps->scaling_list;

    if (scaling_list) {
        VAIQMatrixBufferHEVC iq_matrix;
        int j;

        for (i = 0; i < 6; i++) {
            for (j = 0; j < 16; j++)
                iq_matrix.ScalingList4x4[i][j] = scaling_list->sl[0][i][j];
            for (j = 0; j < 64; j++) {
                iq_matrix.ScalingList8x8[i][j]   = scaling_list->sl[1][i][j];
                iq_matrix.ScalingList16x16[i][j] = scaling_list->sl[2][i][j];
                if (i < 2)
                    iq_matrix.ScalingList32x32[i][j] = scaling_list->sl[3][i * 3][j];
            }
            iq_matrix.ScalingListDC16x16[i] = scaling_list->sl_dc[0][i];
            if (i < 2)
                iq_matrix.ScalingListDC32x32[i] = scaling_list->sl_dc[1][i * 3];
        }

        err = ff_vaapi_decode_make_param_buffer(avctx, &pic->pic,
                                                VAIQMatrixBufferType,
                                                &iq_matrix, sizeof(iq_matrix));
        if (err < 0)
            goto fail;
    }

    return 0;

fail:
    ff_vaapi_decode_cancel(avctx, &pic->pic);
    return err;
}

static int vaapi_hevc_end_frame(AVCodecContext *avctx)
{
    const HEVCContext        *h = avctx->priv_data;
    VAAPIDecodePictureHEVC *pic = h->ref->hwaccel_picture_private;
    VASliceParameterBufferHEVC *last_slice_param = (VASliceParameterBufferHEVC *)&pic->last_slice_param;
    int ret;

    int slice_param_size = avctx->profile == FF_PROFILE_HEVC_REXT ?
                            sizeof(pic->last_slice_param) : sizeof(VASliceParameterBufferHEVC);

    if (pic->last_size) {
        last_slice_param->LongSliceFlags.fields.LastSliceOfPic = 1;
        ret = ff_vaapi_decode_make_slice_buffer(avctx, &pic->pic,
                                                &pic->last_slice_param, slice_param_size,
                                                pic->last_buffer, pic->last_size);
        if (ret < 0)
            goto fail;
    }


    ret = ff_vaapi_decode_issue(avctx, &pic->pic);
    if (ret < 0)
        goto fail;

    return 0;
fail:
    ff_vaapi_decode_cancel(avctx, &pic->pic);
    return ret;
}

static void fill_pred_weight_table(const HEVCContext *h,
                                   const SliceHeader *sh,
                                   VASliceParameterBufferHEVC *slice_param)
{
    int i;

    memset(slice_param->delta_luma_weight_l0,   0, sizeof(slice_param->delta_luma_weight_l0));
    memset(slice_param->delta_luma_weight_l1,   0, sizeof(slice_param->delta_luma_weight_l1));
    memset(slice_param->luma_offset_l0,         0, sizeof(slice_param->luma_offset_l0));
    memset(slice_param->luma_offset_l1,         0, sizeof(slice_param->luma_offset_l1));
    memset(slice_param->delta_chroma_weight_l0, 0, sizeof(slice_param->delta_chroma_weight_l0));
    memset(slice_param->delta_chroma_weight_l1, 0, sizeof(slice_param->delta_chroma_weight_l1));
    memset(slice_param->ChromaOffsetL0,         0, sizeof(slice_param->ChromaOffsetL0));
    memset(slice_param->ChromaOffsetL1,         0, sizeof(slice_param->ChromaOffsetL1));

    slice_param->delta_chroma_log2_weight_denom = 0;
    slice_param->luma_log2_weight_denom         = 0;

    if (sh->slice_type == HEVC_SLICE_I ||
        (sh->slice_type == HEVC_SLICE_P && !h->ps.pps->weighted_pred_flag) ||
        (sh->slice_type == HEVC_SLICE_B && !h->ps.pps->weighted_bipred_flag))
        return;

    slice_param->luma_log2_weight_denom = sh->luma_log2_weight_denom;

    if (h->ps.sps->chroma_format_idc) {
        slice_param->delta_chroma_log2_weight_denom = sh->chroma_log2_weight_denom - sh->luma_log2_weight_denom;
    }

    for (i = 0; i < 15 && i < sh->nb_refs[L0]; i++) {
        slice_param->delta_luma_weight_l0[i] = sh->luma_weight_l0[i] - (1 << sh->luma_log2_weight_denom);
        slice_param->luma_offset_l0[i] = sh->luma_offset_l0[i];
        slice_param->delta_chroma_weight_l0[i][0] = sh->chroma_weight_l0[i][0] - (1 << sh->chroma_log2_weight_denom);
        slice_param->delta_chroma_weight_l0[i][1] = sh->chroma_weight_l0[i][1] - (1 << sh->chroma_log2_weight_denom);
        slice_param->ChromaOffsetL0[i][0] = sh->chroma_offset_l0[i][0];
        slice_param->ChromaOffsetL0[i][1] = sh->chroma_offset_l0[i][1];
    }

    if (sh->slice_type == HEVC_SLICE_B) {
        for (i = 0; i < 15 && i < sh->nb_refs[L1]; i++) {
            slice_param->delta_luma_weight_l1[i] = sh->luma_weight_l1[i] - (1 << sh->luma_log2_weight_denom);
            slice_param->luma_offset_l1[i] = sh->luma_offset_l1[i];
            slice_param->delta_chroma_weight_l1[i][0] = sh->chroma_weight_l1[i][0] - (1 << sh->chroma_log2_weight_denom);
            slice_param->delta_chroma_weight_l1[i][1] = sh->chroma_weight_l1[i][1] - (1 << sh->chroma_log2_weight_denom);
            slice_param->ChromaOffsetL1[i][0] = sh->chroma_offset_l1[i][0];
            slice_param->ChromaOffsetL1[i][1] = sh->chroma_offset_l1[i][1];
        }
    }
}

static uint8_t get_ref_pic_index(const HEVCContext *h, const HEVCFrame *frame)
{
    VAAPIDecodePictureHEVC *pic = h->ref->hwaccel_picture_private;
    VAPictureParameterBufferHEVC *pp = (VAPictureParameterBufferHEVC *)&pic->pic_param;
    uint8_t i;

    if (!frame)
        return 0xff;

    for (i = 0; i < FF_ARRAY_ELEMS(pp->ReferenceFrames); i++) {
        VASurfaceID pid = pp->ReferenceFrames[i].picture_id;
        int poc = pp->ReferenceFrames[i].pic_order_cnt;
        if (pid != VA_INVALID_ID && pid == ff_vaapi_get_surface_id(frame->frame) && poc == frame->poc)
            return i;
    }

    return 0xff;
}

static int vaapi_hevc_decode_slice(AVCodecContext *avctx,
                                   const uint8_t  *buffer,
                                   uint32_t        size)
{
    const HEVCContext        *h = avctx->priv_data;
    const SliceHeader       *sh = &h->sh;
    VAAPIDecodePictureHEVC *pic = h->ref->hwaccel_picture_private;
    VASliceParameterBufferHEVC *last_slice_param = (VASliceParameterBufferHEVC *)&pic->last_slice_param;

    int slice_param_size = avctx->profile == FF_PROFILE_HEVC_REXT ?
                            sizeof(pic->last_slice_param) : sizeof(VASliceParameterBufferHEVC);

    int nb_list = (sh->slice_type == HEVC_SLICE_B) ?
                  2 : (sh->slice_type == HEVC_SLICE_I ? 0 : 1);

    int err, i, list_idx;

    if (!sh->first_slice_in_pic_flag) {
        err = ff_vaapi_decode_make_slice_buffer(avctx, &pic->pic,
                                                &pic->last_slice_param, slice_param_size,
                                                pic->last_buffer, pic->last_size);
        pic->last_buffer = NULL;
        pic->last_size   = 0;
        if (err) {
            ff_vaapi_decode_cancel(avctx, &pic->pic);
            return err;
        }
    }

    *last_slice_param = (VASliceParameterBufferHEVC) {
        .slice_data_size               = size,
        .slice_data_offset             = 0,
        .slice_data_flag               = VA_SLICE_DATA_FLAG_ALL,
        /* Add 1 to the bits count here to account for the byte_alignment bit, which
         * always is at least one bit and not accounted for otherwise. */
        .slice_data_byte_offset        = (get_bits_count(&h->HEVClc->gb) + 1 + 7) / 8,
        .slice_segment_address         = sh->slice_segment_addr,
        .slice_qp_delta                = sh->slice_qp_delta,
        .slice_cb_qp_offset            = sh->slice_cb_qp_offset,
        .slice_cr_qp_offset            = sh->slice_cr_qp_offset,
        .slice_beta_offset_div2        = sh->beta_offset / 2,
        .slice_tc_offset_div2          = sh->tc_offset / 2,
        .collocated_ref_idx            = sh->slice_temporal_mvp_enabled_flag ? sh->collocated_ref_idx : 0xFF,
        .five_minus_max_num_merge_cand = sh->slice_type == HEVC_SLICE_I ? 0 : 5 - sh->max_num_merge_cand,
        .num_ref_idx_l0_active_minus1  = sh->nb_refs[L0] ? sh->nb_refs[L0] - 1 : 0,
        .num_ref_idx_l1_active_minus1  = sh->nb_refs[L1] ? sh->nb_refs[L1] - 1 : 0,

        .LongSliceFlags.fields = {
            .dependent_slice_segment_flag                 = sh->dependent_slice_segment_flag,
            .slice_type                                   = sh->slice_type,
            .color_plane_id                               = sh->colour_plane_id,
            .mvd_l1_zero_flag                             = sh->mvd_l1_zero_flag,
            .cabac_init_flag                              = sh->cabac_init_flag,
            .slice_temporal_mvp_enabled_flag              = sh->slice_temporal_mvp_enabled_flag,
            .slice_deblocking_filter_disabled_flag        = sh->disable_deblocking_filter_flag,
            .collocated_from_l0_flag                      = sh->collocated_list == L0 ? 1 : 0,
            .slice_loop_filter_across_slices_enabled_flag = sh->slice_loop_filter_across_slices_enabled_flag,
            .slice_sao_luma_flag                          = sh->slice_sample_adaptive_offset_flag[0],
            .slice_sao_chroma_flag                        = sh->slice_sample_adaptive_offset_flag[1],
        },
    };

    memset(last_slice_param->RefPicList, 0xFF, sizeof(last_slice_param->RefPicList));

    for (list_idx = 0; list_idx < nb_list; list_idx++) {
        RefPicList *rpl = &h->ref->refPicList[list_idx];

        for (i = 0; i < rpl->nb_refs; i++)
            last_slice_param->RefPicList[list_idx][i] = get_ref_pic_index(h, rpl->ref[i]);
    }

    fill_pred_weight_table(h, sh, last_slice_param);

#if VA_CHECK_VERSION(1, 2, 0)
    if (avctx->profile == FF_PROFILE_HEVC_REXT) {
        pic->last_slice_param.rext = (VASliceParameterBufferHEVCRext) {
            .slice_ext_flags.bits = {
                .cu_chroma_qp_offset_enabled_flag = sh->cu_chroma_qp_offset_enabled_flag,
            },
        };

        memcpy(pic->last_slice_param.rext.luma_offset_l0, pic->last_slice_param.base.luma_offset_l0,
                                                    sizeof(pic->last_slice_param.base.luma_offset_l0));
        memcpy(pic->last_slice_param.rext.luma_offset_l1, pic->last_slice_param.base.luma_offset_l1,
                                                    sizeof(pic->last_slice_param.base.luma_offset_l1));
        memcpy(pic->last_slice_param.rext.ChromaOffsetL0, pic->last_slice_param.base.ChromaOffsetL0,
                                                    sizeof(pic->last_slice_param.base.ChromaOffsetL0));
        memcpy(pic->last_slice_param.rext.ChromaOffsetL1, pic->last_slice_param.base.ChromaOffsetL1,
                                                    sizeof(pic->last_slice_param.base.ChromaOffsetL1));
    }
#endif

    pic->last_buffer = buffer;
    pic->last_size   = size;

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
 * Find exact va_profile for HEVC Range Extension
 */
VAProfile ff_vaapi_parse_hevc_rext_profile(AVCodecContext *avctx)
{
    const HEVCContext *h = avctx->priv_data;
    const HEVCSPS *sps = h->ps.sps;
    const PTL *ptl = &sps->ptl;
    const PTLCommon *general_ptl = &ptl->general_ptl;
    const H265ProfileDescriptor *profile;
    H265RawProfileTierLevel h265_raw_ptl = {0};

    /* convert PTLCommon to H265RawProfileTierLevel */
    ptl_convert(general_ptl, &h265_raw_ptl);

    profile = ff_h265_get_profile(&h265_raw_ptl);
    if (!profile) {
        av_log(avctx, AV_LOG_WARNING, "HEVC profile is not found.\n");
        goto end;
    } else {
        av_log(avctx, AV_LOG_VERBOSE, "HEVC profile %s is found.\n", profile->name);
    }

#if VA_CHECK_VERSION(1, 2, 0)
    if (!strcmp(profile->name, "Main 4:2:2 10") ||
        !strcmp(profile->name, "Main 4:2:2 10 Intra"))
        return VAProfileHEVCMain422_10;
    else if (!strcmp(profile->name, "Main 4:4:4") ||
             !strcmp(profile->name, "Main 4:4:4 Intra"))
        return VAProfileHEVCMain444;
    else if (!strcmp(profile->name, "Main 4:4:4 10") ||
             !strcmp(profile->name, "Main 4:4:4 10 Intra"))
        return VAProfileHEVCMain444_10;
#else
    av_log(avctx, AV_LOG_WARNING, "HEVC profile %s is "
           "not supported with this VA version.\n", profile->name);
#endif

end:
    if (avctx->hwaccel_flags & AV_HWACCEL_FLAG_ALLOW_PROFILE_MISMATCH) {
        // Default to selecting Main profile if profile mismatch is allowed
        return VAProfileHEVCMain;
    } else
        return VAProfileNone;
}

const AVHWAccel ff_hevc_vaapi_hwaccel = {
    .name                 = "hevc_vaapi",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_HEVC,
    .pix_fmt              = AV_PIX_FMT_VAAPI,
    .start_frame          = vaapi_hevc_start_frame,
    .end_frame            = vaapi_hevc_end_frame,
    .decode_slice         = vaapi_hevc_decode_slice,
    .frame_priv_data_size = sizeof(VAAPIDecodePictureHEVC),
    .init                 = ff_vaapi_decode_init,
    .uninit               = ff_vaapi_decode_uninit,
    .frame_params         = ff_vaapi_common_frame_params,
    .priv_data_size       = sizeof(VAAPIDecodeContext),
    .caps_internal        = HWACCEL_CAP_ASYNC_SAFE,
};
