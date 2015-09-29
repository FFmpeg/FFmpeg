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

#include "vaapi_internal.h"
#include "hevc.h"
#include "mpegutils.h"

/**
 * @file
 * This file implements the glue code between FFmpeg's and VA API's
 * structures for HEVC decoding.
 */

typedef struct vaapi_hevc_frame_data {
    VAPictureParameterBufferHEVC *pic_param;
    VASliceParameterBufferHEVC *last_slice_param;
} vaapi_hevc_frame_data;

/**
 * Initialize an empty VA API picture.
 *
 * VA API requires a fixed-size reference picture array.
 */
static void init_vaapi_pic(VAPictureHEVC *va_pic)
{
    va_pic->picture_id = VA_INVALID_ID;
    va_pic->flags = VA_PICTURE_HEVC_INVALID;
    va_pic->pic_order_cnt = 0;
}

static void fill_vaapi_pic(VAPictureHEVC *va_pic, const HEVCFrame *pic, int rps_type)
{
    va_pic->picture_id = ff_vaapi_get_surface_id(pic->frame);
    va_pic->pic_order_cnt = pic->poc;
    va_pic->flags = rps_type;

    if (pic->flags & HEVC_FRAME_FLAG_LONG_REF)
        va_pic->flags |= VA_PICTURE_HEVC_LONG_TERM_REFERENCE;

    if (pic->frame->interlaced_frame) {
        va_pic->flags |= VA_PICTURE_HEVC_FIELD_PIC;

        if (!pic->frame->top_field_first) {
            va_pic->flags |= VA_PICTURE_HEVC_BOTTOM_FIELD;
        }
    }
}

static int find_frame_rps_type(const HEVCContext *h, const HEVCFrame *pic)
{
    VASurfaceID pic_surf = ff_vaapi_get_surface_id(pic->frame);
    int i;

    for (i = 0; i < h->rps[ST_CURR_BEF].nb_refs; ++i) {
        if (pic_surf == ff_vaapi_get_surface_id(h->rps[ST_CURR_BEF].ref[i]->frame))
            return VA_PICTURE_HEVC_RPS_ST_CURR_BEFORE;
    }

    for (i = 0; i < h->rps[ST_CURR_AFT].nb_refs; ++i) {
        if (pic_surf == ff_vaapi_get_surface_id(h->rps[ST_CURR_AFT].ref[i]->frame))
            return VA_PICTURE_HEVC_RPS_ST_CURR_AFTER;
    }

    for (i = 0; i < h->rps[LT_CURR].nb_refs; ++i) {
        if (pic_surf == ff_vaapi_get_surface_id(h->rps[LT_CURR].ref[i]->frame))
            return VA_PICTURE_HEVC_RPS_LT_CURR;
    }

    return 0;
}

static void fill_vaapi_ReferenceFrames(const HEVCContext *h, VAPictureParameterBufferHEVC *pp)
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

static uint8_t get_ref_pic_index(const HEVCContext *h, const HEVCFrame *frame)
{
    vaapi_hevc_frame_data *frame_data = h->ref->hwaccel_picture_private;
    VAPictureParameterBufferHEVC *pp = frame_data->pic_param;
    uint8_t i;

    if (!frame)
        return 0xff;

    for (i = 0; i < FF_ARRAY_ELEMS(pp->ReferenceFrames); ++i) {
        VASurfaceID pid = pp->ReferenceFrames[i].picture_id;
        int poc = pp->ReferenceFrames[i].pic_order_cnt;
        if (pid != VA_INVALID_ID && pid == ff_vaapi_get_surface_id(frame->frame) && poc == frame->poc)
            return i;
    }

    return 0xff;
}

static void fill_picture_parameters(const HEVCContext *h, VAPictureParameterBufferHEVC *pp)
{
    int i;

    pp->pic_fields.value = 0;
    pp->slice_parsing_fields.value = 0;

    fill_vaapi_pic(&pp->CurrPic, h->ref, 0);
    fill_vaapi_ReferenceFrames(h, pp);

    pp->pic_width_in_luma_samples  = h->ps.sps->width;
    pp->pic_height_in_luma_samples = h->ps.sps->height;

    pp->log2_min_luma_coding_block_size_minus3 = h->ps.sps->log2_min_cb_size - 3;

    pp->pic_fields.bits.chroma_format_idc = h->ps.sps->chroma_format_idc;

    pp->sps_max_dec_pic_buffering_minus1 = h->ps.sps->temporal_layer[h->ps.sps->max_sub_layers - 1].max_dec_pic_buffering - 1;
    pp->log2_diff_max_min_luma_coding_block_size = h->ps.sps->log2_diff_max_min_coding_block_size;
    pp->log2_min_transform_block_size_minus2 = h->ps.sps->log2_min_tb_size - 2;
    pp->log2_diff_max_min_transform_block_size = h->ps.sps->log2_max_trafo_size  - h->ps.sps->log2_min_tb_size;
    pp->max_transform_hierarchy_depth_inter = h->ps.sps->max_transform_hierarchy_depth_inter;
    pp->max_transform_hierarchy_depth_intra = h->ps.sps->max_transform_hierarchy_depth_intra;
    pp->num_short_term_ref_pic_sets = h->ps.sps->nb_st_rps;
    pp->num_long_term_ref_pic_sps = h->ps.sps->num_long_term_ref_pics_sps;

    pp->num_ref_idx_l0_default_active_minus1 = h->ps.pps->num_ref_idx_l0_default_active - 1;
    pp->num_ref_idx_l1_default_active_minus1 = h->ps.pps->num_ref_idx_l1_default_active - 1;
    pp->init_qp_minus26 = h->ps.pps->pic_init_qp_minus26;

    pp->pps_cb_qp_offset = h->ps.pps->cb_qp_offset;
    pp->pps_cr_qp_offset = h->ps.pps->cr_qp_offset;

    pp->pic_fields.bits.tiles_enabled_flag = h->ps.pps->tiles_enabled_flag;
    pp->pic_fields.bits.separate_colour_plane_flag = h->ps.sps->separate_colour_plane_flag;
    pp->pic_fields.bits.pcm_enabled_flag = h->ps.sps->pcm_enabled_flag;
    pp->pic_fields.bits.scaling_list_enabled_flag = h->ps.sps->scaling_list_enable_flag;
    pp->pic_fields.bits.transform_skip_enabled_flag = h->ps.pps->transform_skip_enabled_flag;
    pp->pic_fields.bits.amp_enabled_flag = h->ps.sps->amp_enabled_flag;
    pp->pic_fields.bits.strong_intra_smoothing_enabled_flag = h->ps.sps->sps_strong_intra_smoothing_enable_flag;
    pp->pic_fields.bits.sign_data_hiding_enabled_flag = h->ps.pps->sign_data_hiding_flag;
    pp->pic_fields.bits.constrained_intra_pred_flag = h->ps.pps->constrained_intra_pred_flag;
    pp->pic_fields.bits.cu_qp_delta_enabled_flag = h->ps.pps->cu_qp_delta_enabled_flag;
    pp->pic_fields.bits.weighted_pred_flag = h->ps.pps->weighted_pred_flag;
    pp->pic_fields.bits.weighted_bipred_flag = h->ps.pps->weighted_bipred_flag;
    pp->pic_fields.bits.transquant_bypass_enabled_flag = h->ps.pps->transquant_bypass_enable_flag;
    pp->pic_fields.bits.entropy_coding_sync_enabled_flag = h->ps.pps->entropy_coding_sync_enabled_flag;
    pp->pic_fields.bits.pps_loop_filter_across_slices_enabled_flag = h->ps.pps->seq_loop_filter_across_slices_enabled_flag;
    pp->pic_fields.bits.loop_filter_across_tiles_enabled_flag = h->ps.pps->loop_filter_across_tiles_enabled_flag;

    pp->pic_fields.bits.pcm_loop_filter_disabled_flag = h->ps.sps->pcm.loop_filter_disable_flag;
    pp->pcm_sample_bit_depth_luma_minus1 = h->ps.sps->pcm.bit_depth - 1;
    pp->pcm_sample_bit_depth_chroma_minus1 = h->ps.sps->pcm.bit_depth_chroma - 1;
    pp->log2_min_pcm_luma_coding_block_size_minus3 = h->ps.sps->pcm.log2_min_pcm_cb_size - 3;
    pp->log2_diff_max_min_pcm_luma_coding_block_size = h->ps.sps->pcm.log2_max_pcm_cb_size - h->ps.sps->pcm.log2_min_pcm_cb_size;

    memset(pp->column_width_minus1, 0, sizeof(pp->column_width_minus1));
    memset(pp->row_height_minus1, 0, sizeof(pp->row_height_minus1));

    if (h->ps.pps->tiles_enabled_flag) {
        pp->num_tile_columns_minus1 = h->ps.pps->num_tile_columns - 1;
        pp->num_tile_rows_minus1 = h->ps.pps->num_tile_rows - 1;

        for (i = 0; i < h->ps.pps->num_tile_columns; i++)
            pp->column_width_minus1[i] = h->ps.pps->column_width[i] - 1;

        for (i = 0; i < h->ps.pps->num_tile_rows; i++)
            pp->row_height_minus1[i] = h->ps.pps->row_height[i] - 1;
    }

    pp->diff_cu_qp_delta_depth = h->ps.pps->diff_cu_qp_delta_depth;
    pp->pps_beta_offset_div2 = h->ps.pps->beta_offset / 2;
    pp->pps_tc_offset_div2 = h->ps.pps->tc_offset / 2;
    pp->log2_parallel_merge_level_minus2 = h->ps.pps->log2_parallel_merge_level - 2;

    /* Different chroma/luma bit depths are currently not supported by ffmpeg. */
    pp->bit_depth_luma_minus8 = h->ps.sps->bit_depth - 8;
    pp->bit_depth_chroma_minus8 = h->ps.sps->bit_depth - 8;

    pp->slice_parsing_fields.bits.lists_modification_present_flag = h->ps.pps->lists_modification_present_flag;
    pp->slice_parsing_fields.bits.long_term_ref_pics_present_flag = h->ps.sps->long_term_ref_pics_present_flag;
    pp->slice_parsing_fields.bits.sps_temporal_mvp_enabled_flag = h->ps.sps->sps_temporal_mvp_enabled_flag;
    pp->slice_parsing_fields.bits.cabac_init_present_flag = h->ps.pps->cabac_init_present_flag;
    pp->slice_parsing_fields.bits.output_flag_present_flag = h->ps.pps->output_flag_present_flag;
    pp->slice_parsing_fields.bits.dependent_slice_segments_enabled_flag = h->ps.pps->dependent_slice_segments_enabled_flag;
    pp->slice_parsing_fields.bits.pps_slice_chroma_qp_offsets_present_flag = h->ps.pps->pic_slice_level_chroma_qp_offsets_present_flag;
    pp->slice_parsing_fields.bits.sample_adaptive_offset_enabled_flag = h->ps.sps->sao_enabled;
    pp->slice_parsing_fields.bits.deblocking_filter_override_enabled_flag = h->ps.pps->deblocking_filter_override_enabled_flag;
    pp->slice_parsing_fields.bits.pps_disable_deblocking_filter_flag = h->ps.pps->disable_dbf;
    pp->slice_parsing_fields.bits.slice_segment_header_extension_present_flag = h->ps.pps->slice_header_extension_present_flag;

    pp->log2_max_pic_order_cnt_lsb_minus4 = h->ps.sps->log2_max_poc_lsb - 4;
    pp->num_extra_slice_header_bits = h->ps.pps->num_extra_slice_header_bits;

    if (h->nal_unit_type >= NAL_BLA_W_LP && h->nal_unit_type <= NAL_CRA_NUT) {
        pp->slice_parsing_fields.bits.RapPicFlag = 1;
    } else {
        pp->slice_parsing_fields.bits.RapPicFlag = 0;
    }

    if (IS_IDR(h)) {
        pp->slice_parsing_fields.bits.IdrPicFlag = 1;
    } else {
        pp->slice_parsing_fields.bits.IdrPicFlag = 0;
    }

    if (IS_IRAP(h)) {
        pp->slice_parsing_fields.bits.IntraPicFlag = 1;
    } else {
        pp->slice_parsing_fields.bits.IntraPicFlag = 0;
    }

    if (h->sh.short_term_ref_pic_set_sps_flag == 0 && h->sh.short_term_rps) {
        pp->st_rps_bits = h->sh.short_term_ref_pic_set_size;
    } else {
        pp->st_rps_bits = 0;
    }

    /* TODO */
    pp->pic_fields.bits.NoPicReorderingFlag = 0;
    pp->pic_fields.bits.NoBiPredFlag = 0;
}


/** Initialize and start decoding a frame with VA API. */
static int vaapi_hevc_start_frame(AVCodecContext          *avctx,
                                  av_unused const uint8_t *buffer,
                                  av_unused uint32_t       size)
{
    HEVCContext * const h = avctx->priv_data;
    FFVAContext * const vactx = ff_vaapi_get_context(avctx);
    vaapi_hevc_frame_data *frame_data = h->ref->hwaccel_picture_private;
    VAPictureParameterBufferHEVC *pic_param;
    VAIQMatrixBufferHEVC *iq_matrix;
    ScalingList const * scaling_list;
    int i, j, pos;

    ff_dlog(avctx, "vaapi_hevc_start_frame()\n");

    vactx->slice_param_size = sizeof(VASliceParameterBufferHEVC);

    /* Fill in VAPictureParameterBufferHEVC. */
    pic_param = ff_vaapi_alloc_pic_param(vactx, sizeof(VAPictureParameterBufferHEVC));
    if (!pic_param)
        return -1;
    fill_picture_parameters(h, pic_param);
    frame_data->pic_param = pic_param;

    /* Fill in VAIQMatrixBufferHEVC. */
    if (h->ps.pps->scaling_list_data_present_flag) {
        scaling_list = &h->ps.pps->scaling_list;
    } else if (h->ps.sps->scaling_list_enable_flag) {
        scaling_list = &h->ps.sps->scaling_list;
    } else {
        return 0;
    }

    iq_matrix = ff_vaapi_alloc_iq_matrix(vactx, sizeof(VAIQMatrixBufferHEVC));
    if (!iq_matrix)
        return -1;

    for (i = 0; i < 6; ++i) {
        for (j = 0; j < 16; ++j) {
            pos = 4 * ff_hevc_diag_scan4x4_y[j] + ff_hevc_diag_scan4x4_x[j];
            iq_matrix->ScalingList4x4[i][j] = scaling_list->sl[0][i][pos];
        }
        for (j = 0; j < 64; ++j) {
            pos = 8 * ff_hevc_diag_scan8x8_y[j] + ff_hevc_diag_scan8x8_x[j];
            iq_matrix->ScalingList8x8[i][j] = scaling_list->sl[1][i][pos];
            iq_matrix->ScalingList16x16[i][j] = scaling_list->sl[2][i][pos];
            if (i < 2) {
                iq_matrix->ScalingList32x32[i][j] = scaling_list->sl[3][i * 3][pos];
            }
        }
        iq_matrix->ScalingListDC16x16[i] = scaling_list->sl_dc[0][i];
        if (i < 2) {
            iq_matrix->ScalingListDC32x32[i] = scaling_list->sl_dc[1][i * 3];
        }
    }

    return 0;
}

/** End a hardware decoding based frame. */
static int vaapi_hevc_end_frame(AVCodecContext *avctx)
{
    FFVAContext * const vactx = ff_vaapi_get_context(avctx);
    HEVCContext * const h = avctx->priv_data;
    vaapi_hevc_frame_data *frame_data = h->ref->hwaccel_picture_private;
    int ret;

    ff_dlog(avctx, "vaapi_hevc_end_frame()\n");

    frame_data->last_slice_param->LongSliceFlags.fields.LastSliceOfPic = 1;

    ret = ff_vaapi_commit_slices(vactx);
    if (ret < 0)
        goto finish;

    ret = ff_vaapi_render_picture(vactx, ff_vaapi_get_surface_id(h->ref->frame));
    if (ret < 0)
        goto finish;

finish:
    ff_vaapi_common_end_frame(avctx);
    return ret;
}

static int fill_pred_weight_table(HEVCContext * const h,
                                  VASliceParameterBufferHEVC *slice_param,
                                  SliceHeader * const sh)
{
    int i;

    memset(slice_param->delta_luma_weight_l0, 0, sizeof(slice_param->delta_luma_weight_l0));
    memset(slice_param->delta_luma_weight_l1, 0, sizeof(slice_param->delta_luma_weight_l1));
    memset(slice_param->luma_offset_l0, 0, sizeof(slice_param->luma_offset_l0));
    memset(slice_param->luma_offset_l1, 0, sizeof(slice_param->luma_offset_l1));
    memset(slice_param->delta_chroma_weight_l0, 0, sizeof(slice_param->delta_chroma_weight_l0));
    memset(slice_param->delta_chroma_weight_l1, 0, sizeof(slice_param->delta_chroma_weight_l1));
    memset(slice_param->ChromaOffsetL0, 0, sizeof(slice_param->ChromaOffsetL0));
    memset(slice_param->ChromaOffsetL1, 0, sizeof(slice_param->ChromaOffsetL1));

    slice_param->delta_chroma_log2_weight_denom = 0;
    slice_param->luma_log2_weight_denom = 0;

    if (  sh->slice_type == I_SLICE
      || (sh->slice_type == P_SLICE && !h->ps.pps->weighted_pred_flag)
      || (sh->slice_type == B_SLICE && !h->ps.pps->weighted_bipred_flag)) {
        return 0;
    }

    slice_param->luma_log2_weight_denom = sh->luma_log2_weight_denom;

    if (h->ps.sps->chroma_format_idc) {
        slice_param->delta_chroma_log2_weight_denom = sh->chroma_log2_weight_denom - sh->luma_log2_weight_denom;
    }

    for (i = 0; i < 15 && i < sh->nb_refs[L0]; ++i) {
        slice_param->delta_luma_weight_l0[i] = sh->luma_weight_l0[i] - (1 << sh->luma_log2_weight_denom);
        slice_param->luma_offset_l0[i] = sh->luma_offset_l0[i];
        slice_param->delta_chroma_weight_l0[i][0] = sh->chroma_weight_l0[i][0] - (1 << sh->chroma_log2_weight_denom);
        slice_param->delta_chroma_weight_l0[i][1] = sh->chroma_weight_l0[i][1] - (1 << sh->chroma_log2_weight_denom);
        slice_param->ChromaOffsetL0[i][0] = sh->chroma_offset_l0[i][0];
        slice_param->ChromaOffsetL0[i][1] = sh->chroma_offset_l0[i][1];
    }

    if (sh->slice_type == B_SLICE) {
        for (i = 0; i < 15 && i < sh->nb_refs[L1]; ++i) {
            slice_param->delta_luma_weight_l1[i] = sh->luma_weight_l1[i] - (1 << sh->luma_log2_weight_denom);
            slice_param->luma_offset_l1[i] = sh->luma_offset_l1[i];
            slice_param->delta_chroma_weight_l1[i][0] = sh->chroma_weight_l1[i][0] - (1 << sh->chroma_log2_weight_denom);
            slice_param->delta_chroma_weight_l1[i][1] = sh->chroma_weight_l1[i][1] - (1 << sh->chroma_log2_weight_denom);
            slice_param->ChromaOffsetL1[i][0] = sh->chroma_offset_l1[i][0];
            slice_param->ChromaOffsetL1[i][1] = sh->chroma_offset_l1[i][1];
        }
    }

    return 0;
}

/** Decode the given hevc slice with VA API. */
static int vaapi_hevc_decode_slice(AVCodecContext *avctx,
                                   const uint8_t  *buffer,
                                   uint32_t        size)
{
    FFVAContext * const vactx = ff_vaapi_get_context(avctx);
    HEVCContext * const h = avctx->priv_data;
    vaapi_hevc_frame_data *frame_data = h->ref->hwaccel_picture_private;
    SliceHeader * const sh = &h->sh;
    VASliceParameterBufferHEVC *slice_param;
    int i, list_idx;
    uint8_t nb_list = sh->slice_type == B_SLICE ? 2 : 1;

    if (sh->slice_type == I_SLICE)
        nb_list = 0;

    ff_dlog(avctx, "vaapi_hevc_decode_slice(): buffer %p, size %d\n", buffer, size);

    /* Fill in VASliceParameterBufferH264. */
    slice_param = (VASliceParameterBufferHEVC *)ff_vaapi_alloc_slice(vactx, buffer, size);
    if (!slice_param)
        return -1;

    frame_data->last_slice_param = slice_param;

    /* The base structure changed, so this has to be re-set in order to be valid on every byte order. */
    slice_param->slice_data_flag = VA_SLICE_DATA_FLAG_ALL;

    /* Add 1 to the bits count here to account for the byte_alignment bit, which allways is at least one bit and not accounted for otherwise. */
    slice_param->slice_data_byte_offset = (get_bits_count(&h->HEVClc->gb) + 1 + 7) / 8;

    slice_param->slice_segment_address = sh->slice_segment_addr;

    slice_param->LongSliceFlags.value = 0;
    slice_param->LongSliceFlags.fields.dependent_slice_segment_flag = sh->dependent_slice_segment_flag;
    slice_param->LongSliceFlags.fields.slice_type = sh->slice_type;
    slice_param->LongSliceFlags.fields.color_plane_id = sh->colour_plane_id;
    slice_param->LongSliceFlags.fields.mvd_l1_zero_flag = sh->mvd_l1_zero_flag;
    slice_param->LongSliceFlags.fields.cabac_init_flag = sh->cabac_init_flag;
    slice_param->LongSliceFlags.fields.slice_temporal_mvp_enabled_flag = sh->slice_temporal_mvp_enabled_flag;
    slice_param->LongSliceFlags.fields.slice_deblocking_filter_disabled_flag = sh->disable_deblocking_filter_flag;
    slice_param->LongSliceFlags.fields.collocated_from_l0_flag = sh->collocated_list == L0 ? 1 : 0;
    slice_param->LongSliceFlags.fields.slice_loop_filter_across_slices_enabled_flag = sh->slice_loop_filter_across_slices_enabled_flag;

    slice_param->LongSliceFlags.fields.slice_sao_luma_flag = sh->slice_sample_adaptive_offset_flag[0];
    if (h->ps.sps->chroma_format_idc) {
        slice_param->LongSliceFlags.fields.slice_sao_chroma_flag = sh->slice_sample_adaptive_offset_flag[1];
    }

    if (sh->slice_temporal_mvp_enabled_flag) {
        slice_param->collocated_ref_idx = sh->collocated_ref_idx;
    } else {
        slice_param->collocated_ref_idx = 0xFF;
    }

    slice_param->slice_qp_delta = sh->slice_qp_delta;
    slice_param->slice_cb_qp_offset = sh->slice_cb_qp_offset;
    slice_param->slice_cr_qp_offset = sh->slice_cr_qp_offset;
    slice_param->slice_beta_offset_div2 = sh->beta_offset / 2;
    slice_param->slice_tc_offset_div2 = sh->tc_offset / 2;

    if (sh->slice_type == I_SLICE) {
        slice_param->five_minus_max_num_merge_cand = 0;
    } else {
        slice_param->five_minus_max_num_merge_cand = 5 - sh->max_num_merge_cand;
    }

    slice_param->num_ref_idx_l0_active_minus1 = sh->nb_refs[L0] ? sh->nb_refs[L0] - 1 : 0;
    slice_param->num_ref_idx_l1_active_minus1 = sh->nb_refs[L1] ? sh->nb_refs[L1] - 1 : 0;

    memset(slice_param->RefPicList, 0xFF, sizeof(slice_param->RefPicList));

    /* h->ref->refPicList is updated befor calling each slice */
    for (list_idx = 0; list_idx < nb_list; ++list_idx) {
        RefPicList *rpl = &h->ref->refPicList[list_idx];

        for (i = 0; i < rpl->nb_refs; ++i) {
            slice_param->RefPicList[list_idx][i] = get_ref_pic_index(h, rpl->ref[i]);
        }
    }

    return fill_pred_weight_table(h, slice_param, sh);
}

AVHWAccel ff_hevc_vaapi_hwaccel = {
    .name                 = "hevc_vaapi",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_HEVC,
    .pix_fmt              = AV_PIX_FMT_VAAPI,
    .start_frame          = vaapi_hevc_start_frame,
    .end_frame            = vaapi_hevc_end_frame,
    .decode_slice         = vaapi_hevc_decode_slice,
    .init                 = ff_vaapi_context_init,
    .uninit               = ff_vaapi_context_fini,
    .priv_data_size       = sizeof(FFVAContext),
    .frame_priv_data_size = sizeof(vaapi_hevc_frame_data),
};
