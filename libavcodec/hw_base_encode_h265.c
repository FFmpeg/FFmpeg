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

#include "hw_base_encode_h265.h"

#include "h2645data.h"
#include "h265_profile_level.h"

#include "libavutil/pixdesc.h"

int ff_hw_base_encode_init_params_h265(FFHWBaseEncodeContext *base_ctx,
                                       AVCodecContext *avctx,
                                       FFHWBaseEncodeH265 *common,
                                       FFHWBaseEncodeH265Opts *opts)
{
    H265RawVPS                        *vps = &common->raw_vps;
    H265RawSPS                        *sps = &common->raw_sps;
    H265RawPPS                        *pps = &common->raw_pps;
    H265RawProfileTierLevel           *ptl = &vps->profile_tier_level;
    H265RawVUI                        *vui = &sps->vui;

    const AVPixFmtDescriptor *desc;
    int chroma_format, bit_depth;
    int i;

    memset(vps, 0, sizeof(*vps));
    memset(sps, 0, sizeof(*sps));
    memset(pps, 0, sizeof(*pps));

    desc = av_pix_fmt_desc_get(base_ctx->input_frames->sw_format);
    av_assert0(desc);
    if (desc->nb_components == 1) {
        chroma_format = 0;
    } else {
        if (desc->log2_chroma_w == 1 && desc->log2_chroma_h == 1) {
            chroma_format = 1;
        } else if (desc->log2_chroma_w == 1 && desc->log2_chroma_h == 0) {
            chroma_format = 2;
        } else if (desc->log2_chroma_w == 0 && desc->log2_chroma_h == 0) {
            chroma_format = 3;
        } else {
            av_log(avctx, AV_LOG_ERROR, "Chroma format of input pixel format "
                   "%s is not supported.\n", desc->name);
            return AVERROR(EINVAL);
        }
    }
    bit_depth = desc->comp[0].depth;


    // VPS

    vps->nal_unit_header = (H265RawNALUnitHeader) {
        .nal_unit_type         = HEVC_NAL_VPS,
        .nuh_layer_id          = 0,
        .nuh_temporal_id_plus1 = 1,
    };

    vps->vps_video_parameter_set_id = 0;

    vps->vps_base_layer_internal_flag  = 1;
    vps->vps_base_layer_available_flag = 1;
    vps->vps_max_layers_minus1         = 0;
    vps->vps_max_sub_layers_minus1     = 0;
    vps->vps_temporal_id_nesting_flag  = 1;

    ptl->general_profile_space = 0;
    ptl->general_profile_idc   = avctx->profile;
    ptl->general_tier_flag     = opts->tier;

    ptl->general_profile_compatibility_flag[ptl->general_profile_idc] = 1;

    if (ptl->general_profile_compatibility_flag[1])
        ptl->general_profile_compatibility_flag[2] = 1;
    if (ptl->general_profile_compatibility_flag[3]) {
        ptl->general_profile_compatibility_flag[1] = 1;
        ptl->general_profile_compatibility_flag[2] = 1;
    }

    ptl->general_progressive_source_flag    = 1;
    ptl->general_interlaced_source_flag     = 0;
    ptl->general_non_packed_constraint_flag = 1;
    ptl->general_frame_only_constraint_flag = 1;

    ptl->general_max_14bit_constraint_flag = bit_depth <= 14;
    ptl->general_max_12bit_constraint_flag = bit_depth <= 12;
    ptl->general_max_10bit_constraint_flag = bit_depth <= 10;
    ptl->general_max_8bit_constraint_flag  = bit_depth ==  8;

    ptl->general_max_422chroma_constraint_flag  = chroma_format <= 2;
    ptl->general_max_420chroma_constraint_flag  = chroma_format <= 1;
    ptl->general_max_monochrome_constraint_flag = chroma_format == 0;

    ptl->general_intra_constraint_flag = base_ctx->gop_size == 1;
    ptl->general_one_picture_only_constraint_flag = 0;

    ptl->general_lower_bit_rate_constraint_flag = 1;

    if (avctx->level != AV_LEVEL_UNKNOWN) {
        ptl->general_level_idc = avctx->level;
    } else {
        const H265LevelDescriptor *level;

        level = ff_h265_guess_level(ptl, avctx->bit_rate,
                                    base_ctx->surface_width, base_ctx->surface_height,
                                    opts->nb_slices, opts->tile_rows, opts->tile_cols,
                                    (base_ctx->b_per_p > 0) + 1);
        if (level) {
            av_log(avctx, AV_LOG_VERBOSE, "Using level %s.\n", level->name);
            ptl->general_level_idc = level->level_idc;
        } else {
            av_log(avctx, AV_LOG_VERBOSE, "Stream will not conform to "
                   "any normal level; using level 8.5.\n");
            ptl->general_level_idc = 255;
            // The tier flag must be set in level 8.5.
            ptl->general_tier_flag = 1;
        }
    }

    vps->vps_sub_layer_ordering_info_present_flag = 0;
    vps->vps_max_dec_pic_buffering_minus1[0]      = base_ctx->max_b_depth + 1;
    vps->vps_max_num_reorder_pics[0]              = base_ctx->max_b_depth;
    vps->vps_max_latency_increase_plus1[0]        = 0;

    vps->vps_max_layer_id             = 0;
    vps->vps_num_layer_sets_minus1    = 0;
    vps->layer_id_included_flag[0][0] = 1;

    vps->vps_timing_info_present_flag = 1;
    if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
        vps->vps_num_units_in_tick  = avctx->framerate.den;
        vps->vps_time_scale         = avctx->framerate.num;
        vps->vps_poc_proportional_to_timing_flag = 1;
        vps->vps_num_ticks_poc_diff_one_minus1   = 0;
    } else {
        vps->vps_num_units_in_tick  = avctx->time_base.num;
        vps->vps_time_scale         = avctx->time_base.den;
        vps->vps_poc_proportional_to_timing_flag = 0;
    }
    vps->vps_num_hrd_parameters = 0;


    // SPS

    sps->nal_unit_header = (H265RawNALUnitHeader) {
        .nal_unit_type         = HEVC_NAL_SPS,
        .nuh_layer_id          = 0,
        .nuh_temporal_id_plus1 = 1,
    };

    sps->sps_video_parameter_set_id = vps->vps_video_parameter_set_id;

    sps->sps_max_sub_layers_minus1    = vps->vps_max_sub_layers_minus1;
    sps->sps_temporal_id_nesting_flag = vps->vps_temporal_id_nesting_flag;

    sps->profile_tier_level = vps->profile_tier_level;

    sps->sps_seq_parameter_set_id = 0;

    sps->chroma_format_idc          = chroma_format;
    sps->separate_colour_plane_flag = 0;

    sps->pic_width_in_luma_samples  = base_ctx->surface_width;
    sps->pic_height_in_luma_samples = base_ctx->surface_height;

    if (avctx->width  != base_ctx->surface_width ||
        avctx->height != base_ctx->surface_height) {
        sps->conformance_window_flag = 1;
        sps->conf_win_left_offset   = 0;
        sps->conf_win_right_offset  =
            (base_ctx->surface_width - avctx->width) >> desc->log2_chroma_w;
        sps->conf_win_top_offset    = 0;
        sps->conf_win_bottom_offset =
            (base_ctx->surface_height - avctx->height) >> desc->log2_chroma_h;
    } else {
        sps->conformance_window_flag = 0;
    }

    sps->bit_depth_luma_minus8   = bit_depth - 8;
    sps->bit_depth_chroma_minus8 = bit_depth - 8;

    sps->log2_max_pic_order_cnt_lsb_minus4 = 8;

    sps->sps_sub_layer_ordering_info_present_flag =
        vps->vps_sub_layer_ordering_info_present_flag;
    for (i = 0; i <= sps->sps_max_sub_layers_minus1; i++) {
        sps->sps_max_dec_pic_buffering_minus1[i] =
            vps->vps_max_dec_pic_buffering_minus1[i];
        sps->sps_max_num_reorder_pics[i] =
            vps->vps_max_num_reorder_pics[i];
        sps->sps_max_latency_increase_plus1[i] =
            vps->vps_max_latency_increase_plus1[i];
    }

    // These values come from the capabilities of the first encoder
    // implementation in the i965 driver on Intel Skylake.  They may
    // fail badly with other platforms or drivers.
    // CTB size from 8x8 to 32x32.
    sps->log2_min_luma_coding_block_size_minus3   = 0;
    sps->log2_diff_max_min_luma_coding_block_size = 2;
    // Transform size from 4x4 to 32x32.
    sps->log2_min_luma_transform_block_size_minus2   = 0;
    sps->log2_diff_max_min_luma_transform_block_size = 3;
    // Full transform hierarchy allowed (2-5).
    sps->max_transform_hierarchy_depth_inter = 3;
    sps->max_transform_hierarchy_depth_intra = 3;
    // AMP works.
    sps->amp_enabled_flag = 1;
    // SAO and temporal MVP do not work.
    sps->sample_adaptive_offset_enabled_flag = 0;
    sps->sps_temporal_mvp_enabled_flag       = 0;

    sps->pcm_enabled_flag = 0;

    // STRPSs should ideally be here rather than defined individually in
    // each slice, but the structure isn't completely fixed so for now
    // don't bother.
    sps->num_short_term_ref_pic_sets     = 0;
    sps->long_term_ref_pics_present_flag = 0;

    sps->vui_parameters_present_flag = 1;

    if (avctx->sample_aspect_ratio.num != 0 &&
        avctx->sample_aspect_ratio.den != 0) {
        int num, den, i;
        av_reduce(&num, &den, avctx->sample_aspect_ratio.num,
                  avctx->sample_aspect_ratio.den, 65535);
        for (i = 0; i < FF_ARRAY_ELEMS(ff_h2645_pixel_aspect); i++) {
            if (num == ff_h2645_pixel_aspect[i].num &&
                den == ff_h2645_pixel_aspect[i].den) {
                vui->aspect_ratio_idc = i;
                break;
            }
        }
        if (i >= FF_ARRAY_ELEMS(ff_h2645_pixel_aspect)) {
            vui->aspect_ratio_idc = 255;
            vui->sar_width  = num;
            vui->sar_height = den;
        }
        vui->aspect_ratio_info_present_flag = 1;
    }

    // Unspecified video format, from table E-2.
    vui->video_format             = 5;
    vui->video_full_range_flag    =
        avctx->color_range == AVCOL_RANGE_JPEG;
    vui->colour_primaries         = avctx->color_primaries;
    vui->transfer_characteristics = avctx->color_trc;
    vui->matrix_coefficients      = avctx->colorspace;
    if (avctx->color_primaries != AVCOL_PRI_UNSPECIFIED ||
        avctx->color_trc       != AVCOL_TRC_UNSPECIFIED ||
        avctx->colorspace      != AVCOL_SPC_UNSPECIFIED)
        vui->colour_description_present_flag = 1;
    if (avctx->color_range     != AVCOL_RANGE_UNSPECIFIED ||
        vui->colour_description_present_flag)
        vui->video_signal_type_present_flag = 1;

    if (avctx->chroma_sample_location != AVCHROMA_LOC_UNSPECIFIED) {
        vui->chroma_loc_info_present_flag = 1;
        vui->chroma_sample_loc_type_top_field    =
        vui->chroma_sample_loc_type_bottom_field =
            avctx->chroma_sample_location - 1;
    }

    vui->vui_timing_info_present_flag        = 1;
    vui->vui_num_units_in_tick               = vps->vps_num_units_in_tick;
    vui->vui_time_scale                      = vps->vps_time_scale;
    vui->vui_poc_proportional_to_timing_flag = vps->vps_poc_proportional_to_timing_flag;
    vui->vui_num_ticks_poc_diff_one_minus1   = vps->vps_num_ticks_poc_diff_one_minus1;
    vui->vui_hrd_parameters_present_flag     = 0;

    vui->bitstream_restriction_flag    = 1;
    vui->motion_vectors_over_pic_boundaries_flag = 1;
    vui->restricted_ref_pic_lists_flag = 1;
    vui->max_bytes_per_pic_denom       = 0;
    vui->max_bits_per_min_cu_denom     = 0;
    vui->log2_max_mv_length_horizontal = 15;
    vui->log2_max_mv_length_vertical   = 15;


    // PPS

    pps->nal_unit_header = (H265RawNALUnitHeader) {
        .nal_unit_type         = HEVC_NAL_PPS,
        .nuh_layer_id          = 0,
        .nuh_temporal_id_plus1 = 1,
    };

    pps->pps_pic_parameter_set_id = 0;
    pps->pps_seq_parameter_set_id = sps->sps_seq_parameter_set_id;

    pps->num_ref_idx_l0_default_active_minus1 = 0;
    pps->num_ref_idx_l1_default_active_minus1 = 0;

    pps->init_qp_minus26 = opts->fixed_qp_idr - 26;

    pps->cu_qp_delta_enabled_flag = opts->cu_qp_delta_enabled_flag;
    pps->diff_cu_qp_delta_depth   = 0;

    if (opts->tile_rows && opts->tile_cols) {
        int uniform_spacing;

        pps->tiles_enabled_flag      = 1;
        pps->num_tile_columns_minus1 = opts->tile_cols - 1;
        pps->num_tile_rows_minus1    = opts->tile_rows - 1;

        // Test whether the spacing provided matches the H.265 uniform
        // spacing, and set the flag if it does.
        uniform_spacing = 1;
        for (i = 0; i <= pps->num_tile_columns_minus1 &&
                    uniform_spacing; i++) {
            if (opts->col_width[i] !=
                (i + 1) * opts->slice_block_cols / opts->tile_cols -
                 i      * opts->slice_block_cols / opts->tile_cols)
                uniform_spacing = 0;
        }
        for (i = 0; i <= pps->num_tile_rows_minus1 &&
                    uniform_spacing; i++) {
            if (opts->row_height[i] !=
                (i + 1) * opts->slice_block_rows / opts->tile_rows -
                 i      * opts->slice_block_rows / opts->tile_rows)
                uniform_spacing = 0;
        }
        pps->uniform_spacing_flag = uniform_spacing;

        for (i = 0; i <= pps->num_tile_columns_minus1; i++)
            pps->column_width_minus1[i] = opts->col_width[i] - 1;
        for (i = 0; i <= pps->num_tile_rows_minus1; i++)
            pps->row_height_minus1[i]   = opts->row_height[i] - 1;

        pps->loop_filter_across_tiles_enabled_flag = 1;
    }

    pps->pps_loop_filter_across_slices_enabled_flag = 1;

    return 0;
}
