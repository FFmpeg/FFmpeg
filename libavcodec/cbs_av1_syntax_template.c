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

static int FUNC(obu_header)(CodedBitstreamContext *ctx, RWContext *rw,
                            AV1RawOBUHeader *current)
{
    CodedBitstreamAV1Context *priv = ctx->priv_data;
    int err;

    HEADER("OBU header");

    fc(1, obu_forbidden_bit, 0, 0);

    fc(4, obu_type, 0, AV1_OBU_PADDING);
    flag(obu_extension_flag);
    flag(obu_has_size_field);

    fc(1, obu_reserved_1bit, 0, 0);

    if (current->obu_extension_flag) {
        fb(3, temporal_id);
        fb(2, spatial_id);
        fc(3, extension_header_reserved_3bits, 0, 0);
    } else {
        infer(temporal_id, 0);
        infer(spatial_id, 0);
    }

    priv->temporal_id = current->temporal_id;
    priv->spatial_id  = current->spatial_id;

    return 0;
}

static int FUNC(trailing_bits)(CodedBitstreamContext *ctx, RWContext *rw, int nb_bits)
{
    int err;

    av_assert0(nb_bits > 0);

    fixed(1, trailing_one_bit, 1);
    --nb_bits;

    while (nb_bits > 0) {
        fixed(1, trailing_zero_bit, 0);
        --nb_bits;
    }

    return 0;
}

static int FUNC(byte_alignment)(CodedBitstreamContext *ctx, RWContext *rw)
{
    int err;

    while (byte_alignment(rw) != 0)
        fixed(1, zero_bit, 0);

    return 0;
}

static int FUNC(color_config)(CodedBitstreamContext *ctx, RWContext *rw,
                              AV1RawColorConfig *current, int seq_profile)
{
    CodedBitstreamAV1Context *priv = ctx->priv_data;
    int err;

    flag(high_bitdepth);

    if (seq_profile == FF_PROFILE_AV1_PROFESSIONAL &&
        current->high_bitdepth) {
        flag(twelve_bit);
        priv->bit_depth = current->twelve_bit ? 12 : 10;
    } else {
        priv->bit_depth = current->high_bitdepth ? 10 : 8;
    }

    if (seq_profile == FF_PROFILE_AV1_HIGH)
        infer(mono_chrome, 0);
    else
        flag(mono_chrome);
    priv->num_planes = current->mono_chrome ? 1 : 3;

    flag(color_description_present_flag);
    if (current->color_description_present_flag) {
        fb(8, color_primaries);
        fb(8, transfer_characteristics);
        fb(8, matrix_coefficients);
    } else {
        infer(color_primaries,          AVCOL_PRI_UNSPECIFIED);
        infer(transfer_characteristics, AVCOL_TRC_UNSPECIFIED);
        infer(matrix_coefficients,      AVCOL_SPC_UNSPECIFIED);
    }

    if (current->mono_chrome) {
        flag(color_range);

        infer(subsampling_x, 1);
        infer(subsampling_y, 1);
        infer(chroma_sample_position, AV1_CSP_UNKNOWN);
        infer(separate_uv_delta_q, 0);

    } else if (current->color_primaries          == AVCOL_PRI_BT709 &&
               current->transfer_characteristics == AVCOL_TRC_IEC61966_2_1 &&
               current->matrix_coefficients      == AVCOL_SPC_RGB) {
        infer(color_range,   1);
        infer(subsampling_x, 0);
        infer(subsampling_y, 0);
        flag(separate_uv_delta_q);

    } else {
        flag(color_range);

        if (seq_profile == FF_PROFILE_AV1_MAIN) {
            infer(subsampling_x, 1);
            infer(subsampling_y, 1);
        } else if (seq_profile == FF_PROFILE_AV1_HIGH) {
            infer(subsampling_x, 0);
            infer(subsampling_y, 0);
        } else {
            if (priv->bit_depth == 12) {
                fb(1, subsampling_x);
                if (current->subsampling_x)
                    fb(1, subsampling_y);
                else
                    infer(subsampling_y, 0);
            } else {
                infer(subsampling_x, 1);
                infer(subsampling_y, 0);
            }
        }
        if (current->subsampling_x && current->subsampling_y) {
            fc(2, chroma_sample_position, AV1_CSP_UNKNOWN,
                                          AV1_CSP_COLOCATED);
        }

        flag(separate_uv_delta_q);
    }

    return 0;
}

static int FUNC(timing_info)(CodedBitstreamContext *ctx, RWContext *rw,
                             AV1RawTimingInfo *current)
{
    int err;

    fc(32, num_units_in_display_tick, 1, MAX_UINT_BITS(32));
    fc(32, time_scale,                1, MAX_UINT_BITS(32));

    flag(equal_picture_interval);
    if (current->equal_picture_interval)
        uvlc(num_ticks_per_picture_minus_1, 0, MAX_UINT_BITS(32) - 1);

    return 0;
}

static int FUNC(decoder_model_info)(CodedBitstreamContext *ctx, RWContext *rw,
                                    AV1RawDecoderModelInfo *current)
{
    int err;

    fb(5, buffer_delay_length_minus_1);
    fb(32, num_units_in_decoding_tick);
    fb(5,  buffer_removal_time_length_minus_1);
    fb(5,  frame_presentation_time_length_minus_1);

    return 0;
}

static int FUNC(sequence_header_obu)(CodedBitstreamContext *ctx, RWContext *rw,
                                     AV1RawSequenceHeader *current)
{
    int i, err;

    HEADER("Sequence Header");

    fc(3, seq_profile, FF_PROFILE_AV1_MAIN,
                       FF_PROFILE_AV1_PROFESSIONAL);
    flag(still_picture);
    flag(reduced_still_picture_header);

    if (current->reduced_still_picture_header) {
        infer(timing_info_present_flag,           0);
        infer(decoder_model_info_present_flag,    0);
        infer(initial_display_delay_present_flag, 0);
        infer(operating_points_cnt_minus_1,       0);
        infer(operating_point_idc[0],             0);

        fb(5, seq_level_idx[0]);

        infer(seq_tier[0], 0);
        infer(decoder_model_present_for_this_op[0],         0);
        infer(initial_display_delay_present_for_this_op[0], 0);

    } else {
        flag(timing_info_present_flag);
        if (current->timing_info_present_flag) {
            CHECK(FUNC(timing_info)(ctx, rw, &current->timing_info));

            flag(decoder_model_info_present_flag);
            if (current->decoder_model_info_present_flag) {
                CHECK(FUNC(decoder_model_info)
                          (ctx, rw, &current->decoder_model_info));
            }
        } else {
            infer(decoder_model_info_present_flag, 0);
        }

        flag(initial_display_delay_present_flag);

        fb(5, operating_points_cnt_minus_1);
        for (i = 0; i <= current->operating_points_cnt_minus_1; i++) {
            fbs(12, operating_point_idc[i], 1, i);
            fbs(5,  seq_level_idx[i], 1, i);

            if (current->seq_level_idx[i] > 7)
                flags(seq_tier[i], 1, i);
            else
                infer(seq_tier[i], 0);

            if (current->decoder_model_info_present_flag) {
                flags(decoder_model_present_for_this_op[i], 1, i);
                if (current->decoder_model_present_for_this_op[i]) {
                    int n = current->decoder_model_info.buffer_delay_length_minus_1 + 1;
                    fbs(n, decoder_buffer_delay[i], 1, i);
                    fbs(n, encoder_buffer_delay[i], 1, i);
                    flags(low_delay_mode_flag[i], 1, i);
                }
            } else {
                infer(decoder_model_present_for_this_op[i], 0);
            }

            if (current->initial_display_delay_present_flag) {
                flags(initial_display_delay_present_for_this_op[i], 1, i);
                if (current->initial_display_delay_present_for_this_op[i])
                    fbs(4, initial_display_delay_minus_1[i], 1, i);
            }
        }
    }

    fb(4, frame_width_bits_minus_1);
    fb(4, frame_height_bits_minus_1);

    fb(current->frame_width_bits_minus_1  + 1, max_frame_width_minus_1);
    fb(current->frame_height_bits_minus_1 + 1, max_frame_height_minus_1);

    if (current->reduced_still_picture_header)
        infer(frame_id_numbers_present_flag, 0);
    else
        flag(frame_id_numbers_present_flag);
    if (current->frame_id_numbers_present_flag) {
        fb(4, delta_frame_id_length_minus_2);
        fb(3, additional_frame_id_length_minus_1);
    }

    flag(use_128x128_superblock);
    flag(enable_filter_intra);
    flag(enable_intra_edge_filter);

    if (current->reduced_still_picture_header) {
        infer(enable_interintra_compound, 0);
        infer(enable_masked_compound,     0);
        infer(enable_warped_motion,       0);
        infer(enable_dual_filter,         0);
        infer(enable_order_hint,          0);
        infer(enable_jnt_comp,            0);
        infer(enable_ref_frame_mvs,       0);

        infer(seq_force_screen_content_tools,
              AV1_SELECT_SCREEN_CONTENT_TOOLS);
        infer(seq_force_integer_mv,
              AV1_SELECT_INTEGER_MV);
    } else {
        flag(enable_interintra_compound);
        flag(enable_masked_compound);
        flag(enable_warped_motion);
        flag(enable_dual_filter);

        flag(enable_order_hint);
        if (current->enable_order_hint) {
            flag(enable_jnt_comp);
            flag(enable_ref_frame_mvs);
        } else {
            infer(enable_jnt_comp,      0);
            infer(enable_ref_frame_mvs, 0);
        }

        flag(seq_choose_screen_content_tools);
        if (current->seq_choose_screen_content_tools)
            infer(seq_force_screen_content_tools,
                  AV1_SELECT_SCREEN_CONTENT_TOOLS);
        else
            fb(1, seq_force_screen_content_tools);
        if (current->seq_force_screen_content_tools > 0) {
            flag(seq_choose_integer_mv);
            if (current->seq_choose_integer_mv)
                infer(seq_force_integer_mv,
                      AV1_SELECT_INTEGER_MV);
            else
                fb(1, seq_force_integer_mv);
        } else {
            infer(seq_force_integer_mv, AV1_SELECT_INTEGER_MV);
        }

        if (current->enable_order_hint)
            fb(3, order_hint_bits_minus_1);
    }

    flag(enable_superres);
    flag(enable_cdef);
    flag(enable_restoration);

    CHECK(FUNC(color_config)(ctx, rw, &current->color_config,
                             current->seq_profile));

    flag(film_grain_params_present);

    return 0;
}

static int FUNC(temporal_delimiter_obu)(CodedBitstreamContext *ctx, RWContext *rw)
{
    CodedBitstreamAV1Context *priv = ctx->priv_data;

    HEADER("Temporal Delimiter");

    priv->seen_frame_header = 0;

    return 0;
}

static int FUNC(set_frame_refs)(CodedBitstreamContext *ctx, RWContext *rw,
                                AV1RawFrameHeader *current)
{
    CodedBitstreamAV1Context *priv = ctx->priv_data;
    const AV1RawSequenceHeader *seq = priv->sequence_header;
    static const uint8_t ref_frame_list[AV1_NUM_REF_FRAMES - 2] = {
        AV1_REF_FRAME_LAST2, AV1_REF_FRAME_LAST3, AV1_REF_FRAME_BWDREF,
        AV1_REF_FRAME_ALTREF2, AV1_REF_FRAME_ALTREF
    };
    int8_t ref_frame_idx[AV1_REFS_PER_FRAME], used_frame[AV1_NUM_REF_FRAMES];
    int8_t shifted_order_hints[AV1_NUM_REF_FRAMES];
    int cur_frame_hint, latest_order_hint, earliest_order_hint, ref;
    int i, j;

    for (i = 0; i < AV1_REFS_PER_FRAME; i++)
        ref_frame_idx[i] = -1;
    ref_frame_idx[AV1_REF_FRAME_LAST - AV1_REF_FRAME_LAST] = current->last_frame_idx;
    ref_frame_idx[AV1_REF_FRAME_GOLDEN - AV1_REF_FRAME_LAST] = current->golden_frame_idx;

    for (i = 0; i < AV1_NUM_REF_FRAMES; i++)
        used_frame[i] = 0;
    used_frame[current->last_frame_idx] = 1;
    used_frame[current->golden_frame_idx] = 1;

    cur_frame_hint = 1 << (seq->order_hint_bits_minus_1);
    for (i = 0; i < AV1_NUM_REF_FRAMES; i++)
        shifted_order_hints[i] = cur_frame_hint +
                                 cbs_av1_get_relative_dist(seq, priv->ref[i].order_hint,
                                                           priv->order_hint);

    latest_order_hint = shifted_order_hints[current->last_frame_idx];
    earliest_order_hint = shifted_order_hints[current->golden_frame_idx];

    ref = -1;
    for (i = 0; i < AV1_NUM_REF_FRAMES; i++) {
        int hint = shifted_order_hints[i];
        if (!used_frame[i] && hint >= cur_frame_hint &&
            (ref < 0 || hint >= latest_order_hint)) {
            ref = i;
            latest_order_hint = hint;
        }
    }
    if (ref >= 0) {
        ref_frame_idx[AV1_REF_FRAME_ALTREF - AV1_REF_FRAME_LAST] = ref;
        used_frame[ref] = 1;
    }

    ref = -1;
    for (i = 0; i < AV1_NUM_REF_FRAMES; i++) {
        int hint = shifted_order_hints[i];
        if (!used_frame[i] && hint >= cur_frame_hint &&
            (ref < 0 || hint < earliest_order_hint)) {
            ref = i;
            earliest_order_hint = hint;
        }
    }
    if (ref >= 0) {
        ref_frame_idx[AV1_REF_FRAME_BWDREF - AV1_REF_FRAME_LAST] = ref;
        used_frame[ref] = 1;
    }

    ref = -1;
    for (i = 0; i < AV1_NUM_REF_FRAMES; i++) {
        int hint = shifted_order_hints[i];
        if (!used_frame[i] && hint >= cur_frame_hint &&
            (ref < 0 || hint < earliest_order_hint)) {
            ref = i;
            earliest_order_hint = hint;
        }
    }
    if (ref >= 0) {
        ref_frame_idx[AV1_REF_FRAME_ALTREF2 - AV1_REF_FRAME_LAST] = ref;
        used_frame[ref] = 1;
    }

    for (i = 0; i < AV1_REFS_PER_FRAME - 2; i++) {
        int ref_frame = ref_frame_list[i];
        if (ref_frame_idx[ref_frame - AV1_REF_FRAME_LAST] < 0 ) {
            ref = -1;
            for (j = 0; j < AV1_NUM_REF_FRAMES; j++) {
                int hint = shifted_order_hints[j];
                if (!used_frame[j] && hint < cur_frame_hint &&
                    (ref < 0 || hint >= latest_order_hint)) {
                    ref = j;
                    latest_order_hint = hint;
                }
            }
            if (ref >= 0) {
                ref_frame_idx[ref_frame - AV1_REF_FRAME_LAST] = ref;
                used_frame[ref] = 1;
            }
        }
    }

    ref = -1;
    for (i = 0; i < AV1_NUM_REF_FRAMES; i++) {
        int hint = shifted_order_hints[i];
        if (ref < 0 || hint < earliest_order_hint) {
            ref = i;
            earliest_order_hint = hint;
        }
    }
    for (i = 0; i < AV1_REFS_PER_FRAME; i++) {
        if (ref_frame_idx[i] < 0)
            ref_frame_idx[i] = ref;
        infer(ref_frame_idx[i], ref_frame_idx[i]);
    }

    return 0;
}

static int FUNC(superres_params)(CodedBitstreamContext *ctx, RWContext *rw,
                                 AV1RawFrameHeader *current)
{
    CodedBitstreamAV1Context  *priv = ctx->priv_data;
    const AV1RawSequenceHeader *seq = priv->sequence_header;
    int denom, err;

    if (seq->enable_superres)
        flag(use_superres);
    else
        infer(use_superres, 0);

    if (current->use_superres) {
        fb(3, coded_denom);
        denom = current->coded_denom + AV1_SUPERRES_DENOM_MIN;
    } else {
        denom = AV1_SUPERRES_NUM;
    }

    priv->upscaled_width = priv->frame_width;
    priv->frame_width = (priv->upscaled_width * AV1_SUPERRES_NUM +
                         denom / 2) / denom;

    return 0;
}

static int FUNC(frame_size)(CodedBitstreamContext *ctx, RWContext *rw,
                            AV1RawFrameHeader *current)
{
    CodedBitstreamAV1Context  *priv = ctx->priv_data;
    const AV1RawSequenceHeader *seq = priv->sequence_header;
    int err;

    if (current->frame_size_override_flag) {
        fb(seq->frame_width_bits_minus_1 + 1,  frame_width_minus_1);
        fb(seq->frame_height_bits_minus_1 + 1, frame_height_minus_1);
    } else {
        infer(frame_width_minus_1,  seq->max_frame_width_minus_1);
        infer(frame_height_minus_1, seq->max_frame_height_minus_1);
    }

    priv->frame_width  = current->frame_width_minus_1  + 1;
    priv->frame_height = current->frame_height_minus_1 + 1;

    CHECK(FUNC(superres_params)(ctx, rw, current));

    return 0;
}

static int FUNC(render_size)(CodedBitstreamContext *ctx, RWContext *rw,
                             AV1RawFrameHeader *current)
{
    CodedBitstreamAV1Context *priv = ctx->priv_data;
    int err;

    flag(render_and_frame_size_different);

    if (current->render_and_frame_size_different) {
        fb(16, render_width_minus_1);
        fb(16, render_height_minus_1);
    } else {
        infer(render_width_minus_1,  current->frame_width_minus_1);
        infer(render_height_minus_1, current->frame_height_minus_1);
    }

    priv->render_width  = current->render_width_minus_1  + 1;
    priv->render_height = current->render_height_minus_1 + 1;

    return 0;
}

static int FUNC(frame_size_with_refs)(CodedBitstreamContext *ctx, RWContext *rw,
                                      AV1RawFrameHeader *current)
{
    CodedBitstreamAV1Context *priv = ctx->priv_data;
    int i, err;

    for (i = 0; i < AV1_REFS_PER_FRAME; i++) {
        flags(found_ref[i], 1, i);
        if (current->found_ref[i]) {
            AV1ReferenceFrameState *ref =
                &priv->ref[current->ref_frame_idx[i]];

            if (!ref->valid) {
                av_log(ctx->log_ctx, AV_LOG_ERROR,
                       "Missing reference frame needed for frame size "
                       "(ref = %d, ref_frame_idx = %d).\n",
                       i, current->ref_frame_idx[i]);
                return AVERROR_INVALIDDATA;
            }

            infer(frame_width_minus_1,   ref->upscaled_width - 1);
            infer(frame_height_minus_1,  ref->frame_height - 1);
            infer(render_width_minus_1,  ref->render_width - 1);
            infer(render_height_minus_1, ref->render_height - 1);

            priv->upscaled_width = ref->upscaled_width;
            priv->frame_width    = priv->upscaled_width;
            priv->frame_height   = ref->frame_height;
            priv->render_width   = ref->render_width;
            priv->render_height  = ref->render_height;
            break;
        }
    }

    if (i >= AV1_REFS_PER_FRAME) {
        CHECK(FUNC(frame_size)(ctx, rw, current));
        CHECK(FUNC(render_size)(ctx, rw, current));
    } else {
        CHECK(FUNC(superres_params)(ctx, rw, current));
    }

    return 0;
}

static int FUNC(interpolation_filter)(CodedBitstreamContext *ctx, RWContext *rw,
                                      AV1RawFrameHeader *current)
{
    int err;

    flag(is_filter_switchable);
    if (current->is_filter_switchable)
        infer(interpolation_filter,
              AV1_INTERPOLATION_FILTER_SWITCHABLE);
    else
        fb(2, interpolation_filter);

    return 0;
}

static int FUNC(tile_info)(CodedBitstreamContext *ctx, RWContext *rw,
                           AV1RawFrameHeader *current)
{
    CodedBitstreamAV1Context  *priv = ctx->priv_data;
    const AV1RawSequenceHeader *seq = priv->sequence_header;
    int mi_cols, mi_rows, sb_cols, sb_rows, sb_shift, sb_size;
    int max_tile_width_sb, max_tile_height_sb, max_tile_area_sb;
    int min_log2_tile_cols, max_log2_tile_cols, max_log2_tile_rows;
    int min_log2_tiles, min_log2_tile_rows;
    int i, err;

    mi_cols = 2 * ((priv->frame_width  + 7) >> 3);
    mi_rows = 2 * ((priv->frame_height + 7) >> 3);

    sb_cols = seq->use_128x128_superblock ? ((mi_cols + 31) >> 5)
                                          : ((mi_cols + 15) >> 4);
    sb_rows = seq->use_128x128_superblock ? ((mi_rows + 31) >> 5)
                                          : ((mi_rows + 15) >> 4);

    sb_shift = seq->use_128x128_superblock ? 5 : 4;
    sb_size  = sb_shift + 2;

    max_tile_width_sb = AV1_MAX_TILE_WIDTH >> sb_size;
    max_tile_area_sb  = AV1_MAX_TILE_AREA  >> (2 * sb_size);

    min_log2_tile_cols = cbs_av1_tile_log2(max_tile_width_sb, sb_cols);
    max_log2_tile_cols = cbs_av1_tile_log2(1, FFMIN(sb_cols, AV1_MAX_TILE_COLS));
    max_log2_tile_rows = cbs_av1_tile_log2(1, FFMIN(sb_rows, AV1_MAX_TILE_ROWS));
    min_log2_tiles = FFMAX(min_log2_tile_cols,
                           cbs_av1_tile_log2(max_tile_area_sb, sb_rows * sb_cols));

    flag(uniform_tile_spacing_flag);

    if (current->uniform_tile_spacing_flag) {
        int tile_width_sb, tile_height_sb;

        increment(tile_cols_log2, min_log2_tile_cols, max_log2_tile_cols);

        tile_width_sb = (sb_cols + (1 << current->tile_cols_log2) - 1) >>
            current->tile_cols_log2;
        current->tile_cols = (sb_cols + tile_width_sb - 1) / tile_width_sb;

        min_log2_tile_rows = FFMAX(min_log2_tiles - current->tile_cols_log2, 0);

        increment(tile_rows_log2, min_log2_tile_rows, max_log2_tile_rows);

        tile_height_sb = (sb_rows + (1 << current->tile_rows_log2) - 1) >>
            current->tile_rows_log2;
        current->tile_rows = (sb_rows + tile_height_sb - 1) / tile_height_sb;

        for (i = 0; i < current->tile_cols - 1; i++)
            infer(width_in_sbs_minus_1[i], tile_width_sb - 1);
        infer(width_in_sbs_minus_1[i],
              sb_cols - (current->tile_cols - 1) * tile_width_sb - 1);
        for (i = 0; i < current->tile_rows - 1; i++)
            infer(height_in_sbs_minus_1[i], tile_height_sb - 1);
        infer(height_in_sbs_minus_1[i],
              sb_rows - (current->tile_rows - 1) * tile_height_sb - 1);

    } else {
        int widest_tile_sb, start_sb, size_sb, max_width, max_height;

        widest_tile_sb = 0;

        start_sb = 0;
        for (i = 0; start_sb < sb_cols && i < AV1_MAX_TILE_COLS; i++) {
            max_width = FFMIN(sb_cols - start_sb, max_tile_width_sb);
            ns(max_width, width_in_sbs_minus_1[i], 1, i);
            size_sb = current->width_in_sbs_minus_1[i] + 1;
            widest_tile_sb = FFMAX(size_sb, widest_tile_sb);
            start_sb += size_sb;
        }
        current->tile_cols_log2 = cbs_av1_tile_log2(1, i);
        current->tile_cols = i;

        if (min_log2_tiles > 0)
            max_tile_area_sb = (sb_rows * sb_cols) >> (min_log2_tiles + 1);
        else
            max_tile_area_sb = sb_rows * sb_cols;
        max_tile_height_sb = FFMAX(max_tile_area_sb / widest_tile_sb, 1);

        start_sb = 0;
        for (i = 0; start_sb < sb_rows && i < AV1_MAX_TILE_ROWS; i++) {
            max_height = FFMIN(sb_rows - start_sb, max_tile_height_sb);
            ns(max_height, height_in_sbs_minus_1[i], 1, i);
            size_sb = current->height_in_sbs_minus_1[i] + 1;
            start_sb += size_sb;
        }
        current->tile_rows_log2 = cbs_av1_tile_log2(1, i);
        current->tile_rows = i;
    }

    if (current->tile_cols_log2 > 0 ||
        current->tile_rows_log2 > 0) {
        fb(current->tile_cols_log2 + current->tile_rows_log2,
           context_update_tile_id);
        fb(2, tile_size_bytes_minus1);
    } else {
        infer(context_update_tile_id, 0);
    }

    priv->tile_cols = current->tile_cols;
    priv->tile_rows = current->tile_rows;

    return 0;
}

static int FUNC(quantization_params)(CodedBitstreamContext *ctx, RWContext *rw,
                                     AV1RawFrameHeader *current)
{
    CodedBitstreamAV1Context  *priv = ctx->priv_data;
    const AV1RawSequenceHeader *seq = priv->sequence_header;
    int err;

    fb(8, base_q_idx);

    delta_q(delta_q_y_dc);

    if (priv->num_planes > 1) {
        if (seq->color_config.separate_uv_delta_q)
            flag(diff_uv_delta);
        else
            infer(diff_uv_delta, 0);

        delta_q(delta_q_u_dc);
        delta_q(delta_q_u_ac);

        if (current->diff_uv_delta) {
            delta_q(delta_q_v_dc);
            delta_q(delta_q_v_ac);
        } else {
            infer(delta_q_v_dc, current->delta_q_u_dc);
            infer(delta_q_v_ac, current->delta_q_u_ac);
        }
    } else {
        infer(delta_q_u_dc, 0);
        infer(delta_q_u_ac, 0);
        infer(delta_q_v_dc, 0);
        infer(delta_q_v_ac, 0);
    }

    flag(using_qmatrix);
    if (current->using_qmatrix) {
        fb(4, qm_y);
        fb(4, qm_u);
        if (seq->color_config.separate_uv_delta_q)
            fb(4, qm_v);
        else
            infer(qm_v, current->qm_u);
    }

    return 0;
}

static int FUNC(segmentation_params)(CodedBitstreamContext *ctx, RWContext *rw,
                                     AV1RawFrameHeader *current)
{
    CodedBitstreamAV1Context  *priv = ctx->priv_data;
    static const uint8_t bits[AV1_SEG_LVL_MAX] = { 8, 6, 6, 6, 6, 3, 0, 0 };
    static const uint8_t sign[AV1_SEG_LVL_MAX] = { 1, 1, 1, 1, 1, 0, 0, 0 };
    static const uint8_t default_feature_enabled[AV1_SEG_LVL_MAX] = { 0 };
    static const int16_t default_feature_value[AV1_SEG_LVL_MAX] = { 0 };
    int i, j, err;

    flag(segmentation_enabled);

    if (current->segmentation_enabled) {
        if (current->primary_ref_frame == AV1_PRIMARY_REF_NONE) {
            infer(segmentation_update_map,      1);
            infer(segmentation_temporal_update, 0);
            infer(segmentation_update_data,     1);
        } else {
            flag(segmentation_update_map);
            if (current->segmentation_update_map)
                flag(segmentation_temporal_update);
            else
                infer(segmentation_temporal_update, 0);
            flag(segmentation_update_data);
        }

        for (i = 0; i < AV1_MAX_SEGMENTS; i++) {
            const uint8_t *ref_feature_enabled;
            const int16_t *ref_feature_value;

            if (current->primary_ref_frame == AV1_PRIMARY_REF_NONE) {
                ref_feature_enabled = default_feature_enabled;
                ref_feature_value = default_feature_value;
            } else {
                ref_feature_enabled =
                    priv->ref[current->ref_frame_idx[current->primary_ref_frame]].feature_enabled[i];
                ref_feature_value =
                    priv->ref[current->ref_frame_idx[current->primary_ref_frame]].feature_value[i];
            }

            for (j = 0; j < AV1_SEG_LVL_MAX; j++) {
                if (current->segmentation_update_data) {
                    flags(feature_enabled[i][j], 2, i, j);

                    if (current->feature_enabled[i][j] && bits[j] > 0) {
                        if (sign[j])
                            sus(1 + bits[j], feature_value[i][j], 2, i, j);
                        else
                            fbs(bits[j], feature_value[i][j], 2, i, j);
                    } else {
                        infer(feature_value[i][j], 0);
                    }
                } else {
                    infer(feature_enabled[i][j], ref_feature_enabled[j]);
                    infer(feature_value[i][j], ref_feature_value[j]);
                }
            }
        }
    } else {
        for (i = 0; i < AV1_MAX_SEGMENTS; i++) {
            for (j = 0; j < AV1_SEG_LVL_MAX; j++) {
                infer(feature_enabled[i][j], 0);
                infer(feature_value[i][j],   0);
            }
        }
    }

    return 0;
}

static int FUNC(delta_q_params)(CodedBitstreamContext *ctx, RWContext *rw,
                                AV1RawFrameHeader *current)
{
    int err;

    if (current->base_q_idx > 0)
        flag(delta_q_present);
    else
        infer(delta_q_present, 0);

    if (current->delta_q_present)
        fb(2, delta_q_res);

    return 0;
}

static int FUNC(delta_lf_params)(CodedBitstreamContext *ctx, RWContext *rw,
                                 AV1RawFrameHeader *current)
{
    int err;

    if (current->delta_q_present) {
        if (!current->allow_intrabc)
            flag(delta_lf_present);
        else
            infer(delta_lf_present, 0);
        if (current->delta_lf_present) {
            fb(2, delta_lf_res);
            flag(delta_lf_multi);
        } else {
            infer(delta_lf_res,   0);
            infer(delta_lf_multi, 0);
        }
    } else {
        infer(delta_lf_present, 0);
        infer(delta_lf_res,     0);
        infer(delta_lf_multi,   0);
    }

    return 0;
}

static int FUNC(loop_filter_params)(CodedBitstreamContext *ctx, RWContext *rw,
                                    AV1RawFrameHeader *current)
{
    CodedBitstreamAV1Context *priv = ctx->priv_data;
    static const int8_t default_loop_filter_ref_deltas[AV1_TOTAL_REFS_PER_FRAME] =
        { 1, 0, 0, 0, -1, 0, -1, -1 };
    static const int8_t default_loop_filter_mode_deltas[2] = { 0, 0 };
    int i, err;

    if (priv->coded_lossless || current->allow_intrabc) {
        infer(loop_filter_level[0], 0);
        infer(loop_filter_level[1], 0);
        infer(loop_filter_ref_deltas[AV1_REF_FRAME_INTRA],    1);
        infer(loop_filter_ref_deltas[AV1_REF_FRAME_LAST],     0);
        infer(loop_filter_ref_deltas[AV1_REF_FRAME_LAST2],    0);
        infer(loop_filter_ref_deltas[AV1_REF_FRAME_LAST3],    0);
        infer(loop_filter_ref_deltas[AV1_REF_FRAME_BWDREF],   0);
        infer(loop_filter_ref_deltas[AV1_REF_FRAME_GOLDEN],  -1);
        infer(loop_filter_ref_deltas[AV1_REF_FRAME_ALTREF],  -1);
        infer(loop_filter_ref_deltas[AV1_REF_FRAME_ALTREF2], -1);
        for (i = 0; i < 2; i++)
            infer(loop_filter_mode_deltas[i], 0);
        return 0;
    }

    fb(6, loop_filter_level[0]);
    fb(6, loop_filter_level[1]);

    if (priv->num_planes > 1) {
        if (current->loop_filter_level[0] ||
            current->loop_filter_level[1]) {
            fb(6, loop_filter_level[2]);
            fb(6, loop_filter_level[3]);
        }
    }

    fb(3, loop_filter_sharpness);

    flag(loop_filter_delta_enabled);
    if (current->loop_filter_delta_enabled) {
        const int8_t *ref_loop_filter_ref_deltas, *ref_loop_filter_mode_deltas;

        if (current->primary_ref_frame == AV1_PRIMARY_REF_NONE) {
            ref_loop_filter_ref_deltas = default_loop_filter_ref_deltas;
            ref_loop_filter_mode_deltas = default_loop_filter_mode_deltas;
        } else {
            ref_loop_filter_ref_deltas =
                priv->ref[current->ref_frame_idx[current->primary_ref_frame]].loop_filter_ref_deltas;
            ref_loop_filter_mode_deltas =
                priv->ref[current->ref_frame_idx[current->primary_ref_frame]].loop_filter_mode_deltas;
        }

        flag(loop_filter_delta_update);
        for (i = 0; i < AV1_TOTAL_REFS_PER_FRAME; i++) {
            if (current->loop_filter_delta_update)
                flags(update_ref_delta[i], 1, i);
            else
                infer(update_ref_delta[i], 0);
            if (current->update_ref_delta[i])
                sus(1 + 6, loop_filter_ref_deltas[i], 1, i);
            else
                infer(loop_filter_ref_deltas[i], ref_loop_filter_ref_deltas[i]);
        }
        for (i = 0; i < 2; i++) {
            if (current->loop_filter_delta_update)
                flags(update_mode_delta[i], 1, i);
            else
                infer(update_mode_delta[i], 0);
            if (current->update_mode_delta[i])
                sus(1 + 6, loop_filter_mode_deltas[i], 1, i);
            else
                infer(loop_filter_mode_deltas[i], ref_loop_filter_mode_deltas[i]);
        }
    } else {
        for (i = 0; i < AV1_TOTAL_REFS_PER_FRAME; i++)
            infer(loop_filter_ref_deltas[i], default_loop_filter_ref_deltas[i]);
        for (i = 0; i < 2; i++)
            infer(loop_filter_mode_deltas[i], default_loop_filter_mode_deltas[i]);
    }

    return 0;
}

static int FUNC(cdef_params)(CodedBitstreamContext *ctx, RWContext *rw,
                             AV1RawFrameHeader *current)
{
    CodedBitstreamAV1Context  *priv = ctx->priv_data;
    const AV1RawSequenceHeader *seq = priv->sequence_header;
    int i, err;

    if (priv->coded_lossless || current->allow_intrabc ||
        !seq->enable_cdef) {
        infer(cdef_damping_minus_3, 0);
        infer(cdef_bits, 0);
        infer(cdef_y_pri_strength[0],  0);
        infer(cdef_y_sec_strength[0],  0);
        infer(cdef_uv_pri_strength[0], 0);
        infer(cdef_uv_sec_strength[0], 0);

        return 0;
    }

    fb(2, cdef_damping_minus_3);
    fb(2, cdef_bits);

    for (i = 0; i < (1 << current->cdef_bits); i++) {
        fbs(4, cdef_y_pri_strength[i], 1, i);
        fbs(2, cdef_y_sec_strength[i], 1, i);

        if (priv->num_planes > 1) {
            fbs(4, cdef_uv_pri_strength[i], 1, i);
            fbs(2, cdef_uv_sec_strength[i], 1, i);
        }
    }

    return 0;
}

static int FUNC(lr_params)(CodedBitstreamContext *ctx, RWContext *rw,
                           AV1RawFrameHeader *current)
{
    CodedBitstreamAV1Context  *priv = ctx->priv_data;
    const AV1RawSequenceHeader *seq = priv->sequence_header;
    int uses_lr,  uses_chroma_lr;
    int i, err;

    if (priv->all_lossless || current->allow_intrabc ||
        !seq->enable_restoration) {
        return 0;
    }

    uses_lr = uses_chroma_lr = 0;
    for (i = 0; i < priv->num_planes; i++) {
        fbs(2, lr_type[i], 1, i);

        if (current->lr_type[i] != AV1_RESTORE_NONE) {
            uses_lr = 1;
            if (i > 0)
                uses_chroma_lr = 1;
        }
    }

    if (uses_lr) {
        if (seq->use_128x128_superblock)
            increment(lr_unit_shift, 1, 2);
        else
            increment(lr_unit_shift, 0, 2);

        if(seq->color_config.subsampling_x &&
           seq->color_config.subsampling_y && uses_chroma_lr) {
            fb(1, lr_uv_shift);
        } else {
            infer(lr_uv_shift, 0);
        }
    }

    return 0;
}

static int FUNC(read_tx_mode)(CodedBitstreamContext *ctx, RWContext *rw,
                              AV1RawFrameHeader *current)
{
    CodedBitstreamAV1Context *priv = ctx->priv_data;
    int err;

    if (priv->coded_lossless)
        infer(tx_mode, 0);
    else
        increment(tx_mode, 1, 2);

    return 0;
}

static int FUNC(frame_reference_mode)(CodedBitstreamContext *ctx, RWContext *rw,
                                      AV1RawFrameHeader *current)
{
    int err;

    if (current->frame_type == AV1_FRAME_INTRA_ONLY ||
        current->frame_type == AV1_FRAME_KEY)
        infer(reference_select, 0);
    else
        flag(reference_select);

    return 0;
}

static int FUNC(skip_mode_params)(CodedBitstreamContext *ctx, RWContext *rw,
                                  AV1RawFrameHeader *current)
{
    CodedBitstreamAV1Context  *priv = ctx->priv_data;
    const AV1RawSequenceHeader *seq = priv->sequence_header;
    int skip_mode_allowed;
    int err;

    if (current->frame_type == AV1_FRAME_KEY ||
        current->frame_type == AV1_FRAME_INTRA_ONLY ||
        !current->reference_select || !seq->enable_order_hint) {
        skip_mode_allowed = 0;
    } else {
        int forward_idx,  backward_idx;
        int forward_hint, backward_hint;
        int ref_hint, dist, i;

        forward_idx  = -1;
        backward_idx = -1;
        for (i = 0; i < AV1_REFS_PER_FRAME; i++) {
            ref_hint = priv->ref[current->ref_frame_idx[i]].order_hint;
            dist = cbs_av1_get_relative_dist(seq, ref_hint,
                                             priv->order_hint);
            if (dist < 0) {
                if (forward_idx < 0 ||
                    cbs_av1_get_relative_dist(seq, ref_hint,
                                              forward_hint) > 0) {
                    forward_idx  = i;
                    forward_hint = ref_hint;
                }
            } else if (dist > 0) {
                if (backward_idx < 0 ||
                    cbs_av1_get_relative_dist(seq, ref_hint,
                                              backward_hint) < 0) {
                    backward_idx  = i;
                    backward_hint = ref_hint;
                }
            }
        }

        if (forward_idx < 0) {
            skip_mode_allowed = 0;
        } else if (backward_idx >= 0) {
            skip_mode_allowed = 1;
            // Frames for skip mode are forward_idx and backward_idx.
        } else {
            int second_forward_idx;
            int second_forward_hint;

            second_forward_idx = -1;
            for (i = 0; i < AV1_REFS_PER_FRAME; i++) {
                ref_hint = priv->ref[current->ref_frame_idx[i]].order_hint;
                if (cbs_av1_get_relative_dist(seq, ref_hint,
                                              forward_hint) < 0) {
                    if (second_forward_idx < 0 ||
                        cbs_av1_get_relative_dist(seq, ref_hint,
                                                  second_forward_hint) > 0) {
                        second_forward_idx  = i;
                        second_forward_hint = ref_hint;
                    }
                }
            }

            if (second_forward_idx < 0) {
                skip_mode_allowed = 0;
            } else {
                skip_mode_allowed = 1;
                // Frames for skip mode are forward_idx and second_forward_idx.
            }
        }
    }

    if (skip_mode_allowed)
        flag(skip_mode_present);
    else
        infer(skip_mode_present, 0);

    return 0;
}

static int FUNC(global_motion_param)(CodedBitstreamContext *ctx, RWContext *rw,
                                     AV1RawFrameHeader *current,
                                     int type, int ref, int idx)
{
    uint32_t abs_bits, prec_bits, num_syms;
    int err;

    if (idx < 2) {
        if (type == AV1_WARP_MODEL_TRANSLATION) {
            abs_bits  = AV1_GM_ABS_TRANS_ONLY_BITS  - !current->allow_high_precision_mv;
            prec_bits = AV1_GM_TRANS_ONLY_PREC_BITS - !current->allow_high_precision_mv;
        } else {
            abs_bits  = AV1_GM_ABS_TRANS_BITS;
            prec_bits = AV1_GM_TRANS_PREC_BITS;
        }
    } else {
        abs_bits  = AV1_GM_ABS_ALPHA_BITS;
        prec_bits = AV1_GM_ALPHA_PREC_BITS;
    }

    num_syms = 2 * (1 << abs_bits) + 1;
    subexp(gm_params[ref][idx], num_syms, 2, ref, idx);

    // Actual gm_params value is not reconstructed here.
    (void)prec_bits;

    return 0;
}

static int FUNC(global_motion_params)(CodedBitstreamContext *ctx, RWContext *rw,
                                      AV1RawFrameHeader *current)
{
    int ref, type;
    int err;

    if (current->frame_type == AV1_FRAME_KEY ||
        current->frame_type == AV1_FRAME_INTRA_ONLY)
        return 0;

    for (ref = AV1_REF_FRAME_LAST; ref <= AV1_REF_FRAME_ALTREF; ref++) {
        flags(is_global[ref], 1, ref);
        if (current->is_global[ref]) {
            flags(is_rot_zoom[ref], 1, ref);
            if (current->is_rot_zoom[ref]) {
                type = AV1_WARP_MODEL_ROTZOOM;
            } else {
                flags(is_translation[ref], 1, ref);
                type = current->is_translation[ref] ? AV1_WARP_MODEL_TRANSLATION
                                                    : AV1_WARP_MODEL_AFFINE;
            }
        } else {
            type = AV1_WARP_MODEL_IDENTITY;
        }

        if (type >= AV1_WARP_MODEL_ROTZOOM) {
            CHECK(FUNC(global_motion_param)(ctx, rw, current, type, ref, 2));
            CHECK(FUNC(global_motion_param)(ctx, rw, current, type, ref, 3));
            if (type == AV1_WARP_MODEL_AFFINE) {
                CHECK(FUNC(global_motion_param)(ctx, rw, current, type, ref, 4));
                CHECK(FUNC(global_motion_param)(ctx, rw, current, type, ref, 5));
            } else {
                // gm_params[ref][4] = -gm_params[ref][3]
                // gm_params[ref][5] =  gm_params[ref][2]
            }
        }
        if (type >= AV1_WARP_MODEL_TRANSLATION) {
            CHECK(FUNC(global_motion_param)(ctx, rw, current, type, ref, 0));
            CHECK(FUNC(global_motion_param)(ctx, rw, current, type, ref, 1));
        }
    }

    return 0;
}

static int FUNC(film_grain_params)(CodedBitstreamContext *ctx, RWContext *rw,
                                   AV1RawFilmGrainParams *current,
                                   AV1RawFrameHeader *frame_header)
{
    CodedBitstreamAV1Context  *priv = ctx->priv_data;
    const AV1RawSequenceHeader *seq = priv->sequence_header;
    int num_pos_luma, num_pos_chroma;
    int i, err;

    if (!seq->film_grain_params_present ||
        (!frame_header->show_frame && !frame_header->showable_frame))
        return 0;

    flag(apply_grain);

    if (!current->apply_grain)
        return 0;

    fb(16, grain_seed);

    if (frame_header->frame_type == AV1_FRAME_INTER)
        flag(update_grain);
    else
        infer(update_grain, 1);

    if (!current->update_grain) {
        fb(3, film_grain_params_ref_idx);
        return 0;
    }

    fc(4, num_y_points, 0, 14);
    for (i = 0; i < current->num_y_points; i++) {
        fcs(8, point_y_value[i],
            i ? current->point_y_value[i - 1] + 1 : 0,
            MAX_UINT_BITS(8) - (current->num_y_points - i - 1),
            1, i);
        fbs(8, point_y_scaling[i], 1, i);
    }

    if (seq->color_config.mono_chrome)
        infer(chroma_scaling_from_luma, 0);
    else
        flag(chroma_scaling_from_luma);

    if (seq->color_config.mono_chrome ||
        current->chroma_scaling_from_luma ||
        (seq->color_config.subsampling_x == 1 &&
         seq->color_config.subsampling_y == 1 &&
         current->num_y_points == 0)) {
        infer(num_cb_points, 0);
        infer(num_cr_points, 0);
    } else {
        fc(4, num_cb_points, 0, 10);
        for (i = 0; i < current->num_cb_points; i++) {
            fcs(8, point_cb_value[i],
                i ? current->point_cb_value[i - 1] + 1 : 0,
                MAX_UINT_BITS(8) - (current->num_cb_points - i - 1),
                1, i);
            fbs(8, point_cb_scaling[i], 1, i);
        }
        fc(4, num_cr_points, 0, 10);
        for (i = 0; i < current->num_cr_points; i++) {
            fcs(8, point_cr_value[i],
                i ? current->point_cr_value[i - 1] + 1 : 0,
                MAX_UINT_BITS(8) - (current->num_cr_points - i - 1),
                1, i);
            fbs(8, point_cr_scaling[i], 1, i);
        }
    }

    fb(2, grain_scaling_minus_8);
    fb(2, ar_coeff_lag);
    num_pos_luma = 2 * current->ar_coeff_lag * (current->ar_coeff_lag + 1);
    if (current->num_y_points) {
        num_pos_chroma = num_pos_luma + 1;
        for (i = 0; i < num_pos_luma; i++)
            fbs(8, ar_coeffs_y_plus_128[i], 1, i);
    } else {
        num_pos_chroma = num_pos_luma;
    }
    if (current->chroma_scaling_from_luma || current->num_cb_points) {
        for (i = 0; i < num_pos_chroma; i++)
            fbs(8, ar_coeffs_cb_plus_128[i], 1, i);
    }
    if (current->chroma_scaling_from_luma || current->num_cr_points) {
        for (i = 0; i < num_pos_chroma; i++)
            fbs(8, ar_coeffs_cr_plus_128[i], 1, i);
    }
    fb(2, ar_coeff_shift_minus_6);
    fb(2, grain_scale_shift);
    if (current->num_cb_points) {
        fb(8, cb_mult);
        fb(8, cb_luma_mult);
        fb(9, cb_offset);
    }
    if (current->num_cr_points) {
        fb(8, cr_mult);
        fb(8, cr_luma_mult);
        fb(9, cr_offset);
    }

    flag(overlap_flag);
    flag(clip_to_restricted_range);

    return 0;
}

static int FUNC(uncompressed_header)(CodedBitstreamContext *ctx, RWContext *rw,
                                     AV1RawFrameHeader *current)
{
    CodedBitstreamAV1Context *priv = ctx->priv_data;
    const AV1RawSequenceHeader *seq;
    int id_len, diff_len, all_frames, frame_is_intra, order_hint_bits;
    int i, err;

    if (!priv->sequence_header) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "No sequence header available: "
               "unable to decode frame header.\n");
        return AVERROR_INVALIDDATA;
    }
    seq = priv->sequence_header;

    id_len = seq->additional_frame_id_length_minus_1 +
             seq->delta_frame_id_length_minus_2 + 3;
    all_frames = (1 << AV1_NUM_REF_FRAMES) - 1;

    if (seq->reduced_still_picture_header) {
        infer(show_existing_frame, 0);
        infer(frame_type,     AV1_FRAME_KEY);
        infer(show_frame,     1);
        infer(showable_frame, 0);
        frame_is_intra = 1;

    } else {
        flag(show_existing_frame);

        if (current->show_existing_frame) {
            AV1ReferenceFrameState *ref;

            fb(3, frame_to_show_map_idx);
            ref = &priv->ref[current->frame_to_show_map_idx];

            if (!ref->valid) {
                av_log(ctx->log_ctx, AV_LOG_ERROR, "Missing reference frame needed for "
                       "show_existing_frame (frame_to_show_map_idx = %d).\n",
                       current->frame_to_show_map_idx);
                return AVERROR_INVALIDDATA;
            }

            if (seq->decoder_model_info_present_flag &&
                !seq->timing_info.equal_picture_interval) {
                fb(seq->decoder_model_info.frame_presentation_time_length_minus_1 + 1,
                   frame_presentation_time);
            }

            if (seq->frame_id_numbers_present_flag)
                fb(id_len, display_frame_id);

            infer(frame_type, ref->frame_type);
            if (current->frame_type == AV1_FRAME_KEY) {
                infer(refresh_frame_flags, all_frames);

                // Section 7.21
                infer(current_frame_id, ref->frame_id);
                priv->upscaled_width  = ref->upscaled_width;
                priv->frame_width     = ref->frame_width;
                priv->frame_height    = ref->frame_height;
                priv->render_width    = ref->render_width;
                priv->render_height   = ref->render_height;
                priv->bit_depth       = ref->bit_depth;
                priv->order_hint      = ref->order_hint;
            } else
                infer(refresh_frame_flags, 0);

            infer(frame_width_minus_1,   ref->upscaled_width - 1);
            infer(frame_height_minus_1,  ref->frame_height - 1);
            infer(render_width_minus_1,  ref->render_width - 1);
            infer(render_height_minus_1, ref->render_height - 1);

            // Section 7.20
            goto update_refs;
        }

        fb(2, frame_type);
        frame_is_intra = (current->frame_type == AV1_FRAME_INTRA_ONLY ||
                          current->frame_type == AV1_FRAME_KEY);

        flag(show_frame);
        if (current->show_frame &&
            seq->decoder_model_info_present_flag &&
            !seq->timing_info.equal_picture_interval) {
            fb(seq->decoder_model_info.frame_presentation_time_length_minus_1 + 1,
               frame_presentation_time);
        }
        if (current->show_frame)
            infer(showable_frame, current->frame_type != AV1_FRAME_KEY);
        else
            flag(showable_frame);

        if (current->frame_type == AV1_FRAME_SWITCH ||
            (current->frame_type == AV1_FRAME_KEY && current->show_frame))
            infer(error_resilient_mode, 1);
        else
            flag(error_resilient_mode);
    }

    if (current->frame_type == AV1_FRAME_KEY && current->show_frame) {
        for (i = 0; i < AV1_NUM_REF_FRAMES; i++) {
            priv->ref[i].valid = 0;
            priv->ref[i].order_hint = 0;
        }
    }

    flag(disable_cdf_update);

    if (seq->seq_force_screen_content_tools ==
        AV1_SELECT_SCREEN_CONTENT_TOOLS) {
        flag(allow_screen_content_tools);
    } else {
        infer(allow_screen_content_tools,
              seq->seq_force_screen_content_tools);
    }
    if (current->allow_screen_content_tools) {
        if (seq->seq_force_integer_mv == AV1_SELECT_INTEGER_MV)
            flag(force_integer_mv);
        else
            infer(force_integer_mv, seq->seq_force_integer_mv);
    } else {
        infer(force_integer_mv, 0);
    }

    if (seq->frame_id_numbers_present_flag) {
        fb(id_len, current_frame_id);

        diff_len = seq->delta_frame_id_length_minus_2 + 2;
        for (i = 0; i < AV1_NUM_REF_FRAMES; i++) {
            if (current->current_frame_id > (1 << diff_len)) {
                if (priv->ref[i].frame_id > current->current_frame_id ||
                    priv->ref[i].frame_id < (current->current_frame_id -
                                             (1 << diff_len)))
                    priv->ref[i].valid = 0;
            } else {
                if (priv->ref[i].frame_id > current->current_frame_id &&
                    priv->ref[i].frame_id < ((1 << id_len) +
                                             current->current_frame_id -
                                             (1 << diff_len)))
                    priv->ref[i].valid = 0;
            }
        }
    } else {
        infer(current_frame_id, 0);
    }

    if (current->frame_type == AV1_FRAME_SWITCH)
        infer(frame_size_override_flag, 1);
    else if(seq->reduced_still_picture_header)
        infer(frame_size_override_flag, 0);
    else
        flag(frame_size_override_flag);

    order_hint_bits =
        seq->enable_order_hint ? seq->order_hint_bits_minus_1 + 1 : 0;
    if (order_hint_bits > 0)
        fb(order_hint_bits, order_hint);
    else
        infer(order_hint, 0);
    priv->order_hint = current->order_hint;

    if (frame_is_intra || current->error_resilient_mode)
        infer(primary_ref_frame, AV1_PRIMARY_REF_NONE);
    else
        fb(3, primary_ref_frame);

    if (seq->decoder_model_info_present_flag) {
        flag(buffer_removal_time_present_flag);
        if (current->buffer_removal_time_present_flag) {
            for (i = 0; i <= seq->operating_points_cnt_minus_1; i++) {
                if (seq->decoder_model_present_for_this_op[i]) {
                    int op_pt_idc = seq->operating_point_idc[i];
                    int in_temporal_layer = (op_pt_idc >>  priv->temporal_id    ) & 1;
                    int in_spatial_layer  = (op_pt_idc >> (priv->spatial_id + 8)) & 1;
                    if (seq->operating_point_idc[i] == 0 ||
                        (in_temporal_layer && in_spatial_layer)) {
                        fbs(seq->decoder_model_info.buffer_removal_time_length_minus_1 + 1,
                            buffer_removal_time[i], 1, i);
                    }
                }
            }
        }
    }

    if (current->frame_type == AV1_FRAME_SWITCH ||
        (current->frame_type == AV1_FRAME_KEY && current->show_frame))
        infer(refresh_frame_flags, all_frames);
    else
        fb(8, refresh_frame_flags);

    if (!frame_is_intra || current->refresh_frame_flags != all_frames) {
        if (seq->enable_order_hint) {
            for (i = 0; i < AV1_NUM_REF_FRAMES; i++) {
                if (current->error_resilient_mode)
                    fbs(order_hint_bits, ref_order_hint[i], 1, i);
                else
                    infer(ref_order_hint[i], priv->ref[i].order_hint);
                if (current->ref_order_hint[i] != priv->ref[i].order_hint)
                    priv->ref[i].valid = 0;
            }
        }
    }

    if (current->frame_type == AV1_FRAME_KEY ||
        current->frame_type == AV1_FRAME_INTRA_ONLY) {
        CHECK(FUNC(frame_size)(ctx, rw, current));
        CHECK(FUNC(render_size)(ctx, rw, current));

        if (current->allow_screen_content_tools &&
            priv->upscaled_width == priv->frame_width)
            flag(allow_intrabc);
        else
            infer(allow_intrabc, 0);

    } else {
        if (!seq->enable_order_hint) {
            infer(frame_refs_short_signaling, 0);
        } else {
            flag(frame_refs_short_signaling);
            if (current->frame_refs_short_signaling) {
                fb(3, last_frame_idx);
                fb(3, golden_frame_idx);
                CHECK(FUNC(set_frame_refs)(ctx, rw, current));
            }
        }

        for (i = 0; i < AV1_REFS_PER_FRAME; i++) {
            if (!current->frame_refs_short_signaling)
                fbs(3, ref_frame_idx[i], 1, i);
            if (seq->frame_id_numbers_present_flag) {
                fbs(seq->delta_frame_id_length_minus_2 + 2,
                    delta_frame_id_minus1[i], 1, i);
            }
        }

        if (current->frame_size_override_flag &&
            !current->error_resilient_mode) {
            CHECK(FUNC(frame_size_with_refs)(ctx, rw, current));
        } else {
            CHECK(FUNC(frame_size)(ctx, rw, current));
            CHECK(FUNC(render_size)(ctx, rw, current));
        }

        if (current->force_integer_mv)
            infer(allow_high_precision_mv, 0);
        else
            flag(allow_high_precision_mv);

        CHECK(FUNC(interpolation_filter)(ctx, rw, current));

        flag(is_motion_mode_switchable);

        if (current->error_resilient_mode ||
            !seq->enable_ref_frame_mvs)
            infer(use_ref_frame_mvs, 0);
        else
            flag(use_ref_frame_mvs);

        infer(allow_intrabc, 0);
    }

    if (!frame_is_intra) {
        // Derive reference frame sign biases.
    }

    if (seq->reduced_still_picture_header || current->disable_cdf_update)
        infer(disable_frame_end_update_cdf, 1);
    else
        flag(disable_frame_end_update_cdf);

    if (current->primary_ref_frame == AV1_PRIMARY_REF_NONE) {
        // Init non-coeff CDFs.
        // Setup past independence.
    } else {
        // Load CDF tables from previous frame.
        // Load params from previous frame.
    }

    if (current->use_ref_frame_mvs) {
        // Perform motion field estimation process.
    }

    CHECK(FUNC(tile_info)(ctx, rw, current));

    CHECK(FUNC(quantization_params)(ctx, rw, current));

    CHECK(FUNC(segmentation_params)(ctx, rw, current));

    CHECK(FUNC(delta_q_params)(ctx, rw, current));

    CHECK(FUNC(delta_lf_params)(ctx, rw, current));

    // Init coeff CDFs / load previous segments.

    priv->coded_lossless = 1;
    for (i = 0; i < AV1_MAX_SEGMENTS; i++) {
        int qindex;
        if (current->feature_enabled[i][AV1_SEG_LVL_ALT_Q]) {
            qindex = (current->base_q_idx +
                      current->feature_value[i][AV1_SEG_LVL_ALT_Q]);
        } else {
            qindex = current->base_q_idx;
        }
        qindex = av_clip_uintp2(qindex, 8);

        if (qindex                || current->delta_q_y_dc ||
            current->delta_q_u_ac || current->delta_q_u_dc ||
            current->delta_q_v_ac || current->delta_q_v_dc) {
            priv->coded_lossless = 0;
        }
    }
    priv->all_lossless = priv->coded_lossless &&
        priv->frame_width == priv->upscaled_width;

    CHECK(FUNC(loop_filter_params)(ctx, rw, current));

    CHECK(FUNC(cdef_params)(ctx, rw, current));

    CHECK(FUNC(lr_params)(ctx, rw, current));

    CHECK(FUNC(read_tx_mode)(ctx, rw, current));

    CHECK(FUNC(frame_reference_mode)(ctx, rw, current));

    CHECK(FUNC(skip_mode_params)(ctx, rw, current));

    if (frame_is_intra || current->error_resilient_mode ||
        !seq->enable_warped_motion)
        infer(allow_warped_motion, 0);
    else
        flag(allow_warped_motion);

    flag(reduced_tx_set);

    CHECK(FUNC(global_motion_params)(ctx, rw, current));

    CHECK(FUNC(film_grain_params)(ctx, rw, &current->film_grain, current));

    av_log(ctx->log_ctx, AV_LOG_DEBUG, "Frame %d:  size %dx%d  "
           "upscaled %d  render %dx%d  subsample %dx%d  "
           "bitdepth %d  tiles %dx%d.\n", priv->order_hint,
           priv->frame_width, priv->frame_height, priv->upscaled_width,
           priv->render_width, priv->render_height,
           seq->color_config.subsampling_x + 1,
           seq->color_config.subsampling_y + 1, priv->bit_depth,
           priv->tile_rows, priv->tile_cols);

update_refs:
    for (i = 0; i < AV1_NUM_REF_FRAMES; i++) {
        if (current->refresh_frame_flags & (1 << i)) {
            priv->ref[i] = (AV1ReferenceFrameState) {
                .valid          = 1,
                .frame_id       = current->current_frame_id,
                .upscaled_width = priv->upscaled_width,
                .frame_width    = priv->frame_width,
                .frame_height   = priv->frame_height,
                .render_width   = priv->render_width,
                .render_height  = priv->render_height,
                .frame_type     = current->frame_type,
                .subsampling_x  = seq->color_config.subsampling_x,
                .subsampling_y  = seq->color_config.subsampling_y,
                .bit_depth      = priv->bit_depth,
                .order_hint     = priv->order_hint,
            };
            memcpy(priv->ref[i].loop_filter_ref_deltas, current->loop_filter_ref_deltas,
                   sizeof(current->loop_filter_ref_deltas));
            memcpy(priv->ref[i].loop_filter_mode_deltas, current->loop_filter_mode_deltas,
                   sizeof(current->loop_filter_mode_deltas));
            memcpy(priv->ref[i].feature_enabled, current->feature_enabled,
                   sizeof(current->feature_enabled));
            memcpy(priv->ref[i].feature_value, current->feature_value,
                   sizeof(current->feature_value));
        }
    }

    return 0;
}

static int FUNC(frame_header_obu)(CodedBitstreamContext *ctx, RWContext *rw,
                                  AV1RawFrameHeader *current, int redundant,
                                  AVBufferRef *rw_buffer_ref)
{
    CodedBitstreamAV1Context *priv = ctx->priv_data;
    int start_pos, fh_bits, fh_bytes, err;
    uint8_t *fh_start;

    if (priv->seen_frame_header) {
        if (!redundant) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid repeated "
                   "frame header OBU.\n");
            return AVERROR_INVALIDDATA;
        } else {
            GetBitContext fh;
            size_t i, b;
            uint32_t val;

            HEADER("Redundant Frame Header");

            av_assert0(priv->frame_header_ref && priv->frame_header);

            init_get_bits(&fh, priv->frame_header,
                          priv->frame_header_size);
            for (i = 0; i < priv->frame_header_size; i += 8) {
                b = FFMIN(priv->frame_header_size - i, 8);
                val = get_bits(&fh, b);
                xf(b, frame_header_copy[i],
                   val, val, val, 1, i / 8);
            }
        }
    } else {
        if (redundant)
            HEADER("Redundant Frame Header (used as Frame Header)");
        else
            HEADER("Frame Header");

#ifdef READ
        start_pos = get_bits_count(rw);
#else
        start_pos = put_bits_count(rw);
#endif

        CHECK(FUNC(uncompressed_header)(ctx, rw, current));

        priv->tile_num = 0;

        if (current->show_existing_frame) {
            priv->seen_frame_header = 0;
        } else {
            priv->seen_frame_header = 1;

            av_buffer_unref(&priv->frame_header_ref);

#ifdef READ
            fh_bits  = get_bits_count(rw) - start_pos;
            fh_start = (uint8_t*)rw->buffer + start_pos / 8;
#else
            // Need to flush the bitwriter so that we can copy its output,
            // but use a copy so we don't affect the caller's structure.
            {
                PutBitContext tmp = *rw;
                flush_put_bits(&tmp);
            }

            fh_bits  = put_bits_count(rw) - start_pos;
            fh_start = rw->buf + start_pos / 8;
#endif
            fh_bytes = (fh_bits + 7) / 8;

            priv->frame_header_size = fh_bits;

            if (rw_buffer_ref) {
                priv->frame_header_ref = av_buffer_ref(rw_buffer_ref);
                if (!priv->frame_header_ref)
                    return AVERROR(ENOMEM);
                priv->frame_header = fh_start;
            } else {
                priv->frame_header_ref =
                    av_buffer_alloc(fh_bytes + AV_INPUT_BUFFER_PADDING_SIZE);
                if (!priv->frame_header_ref)
                    return AVERROR(ENOMEM);
                priv->frame_header = priv->frame_header_ref->data;
                memcpy(priv->frame_header, fh_start, fh_bytes);
            }
        }
    }

    return 0;
}

static int FUNC(tile_group_obu)(CodedBitstreamContext *ctx, RWContext *rw,
                                AV1RawTileGroup *current)
{
    CodedBitstreamAV1Context *priv = ctx->priv_data;
    int num_tiles, tile_bits;
    int err;

    HEADER("Tile Group");

    num_tiles = priv->tile_cols * priv->tile_rows;
    if (num_tiles > 1)
        flag(tile_start_and_end_present_flag);
    else
        infer(tile_start_and_end_present_flag, 0);

    if (num_tiles == 1 || !current->tile_start_and_end_present_flag) {
        infer(tg_start, 0);
        infer(tg_end, num_tiles - 1);
    } else {
        tile_bits = cbs_av1_tile_log2(1, priv->tile_cols) +
                    cbs_av1_tile_log2(1, priv->tile_rows);
        fc(tile_bits, tg_start, priv->tile_num, num_tiles - 1);
        fc(tile_bits, tg_end, current->tg_start, num_tiles - 1);
    }

    priv->tile_num = current->tg_end + 1;

    CHECK(FUNC(byte_alignment)(ctx, rw));

    // Reset header for next frame.
    if (current->tg_end == num_tiles - 1)
        priv->seen_frame_header = 0;

    // Tile data follows.

    return 0;
}

static int FUNC(frame_obu)(CodedBitstreamContext *ctx, RWContext *rw,
                           AV1RawFrame *current,
                           AVBufferRef *rw_buffer_ref)
{
    int err;

    CHECK(FUNC(frame_header_obu)(ctx, rw, &current->header,
                                 0, rw_buffer_ref));

    CHECK(FUNC(byte_alignment)(ctx, rw));

    CHECK(FUNC(tile_group_obu)(ctx, rw, &current->tile_group));

    return 0;
}

static int FUNC(tile_list_obu)(CodedBitstreamContext *ctx, RWContext *rw,
                               AV1RawTileList *current)
{
    int err;

    fb(8, output_frame_width_in_tiles_minus_1);
    fb(8, output_frame_height_in_tiles_minus_1);

    fb(16, tile_count_minus_1);

    // Tile data follows.

    return 0;
}

static int FUNC(metadata_hdr_cll)(CodedBitstreamContext *ctx, RWContext *rw,
                                  AV1RawMetadataHDRCLL *current)
{
    int err;

    fb(16, max_cll);
    fb(16, max_fall);

    return 0;
}

static int FUNC(metadata_hdr_mdcv)(CodedBitstreamContext *ctx, RWContext *rw,
                                   AV1RawMetadataHDRMDCV *current)
{
    int err, i;

    for (i = 0; i < 3; i++) {
        fbs(16, primary_chromaticity_x[i], 1, i);
        fbs(16, primary_chromaticity_y[i], 1, i);
    }

    fb(16, white_point_chromaticity_x);
    fb(16, white_point_chromaticity_y);

    fc(32, luminance_max, 1, MAX_UINT_BITS(32));
    // luminance_min must be lower than luminance_max. Convert luminance_max from
    // 24.8 fixed point to 18.14 fixed point in order to compare them.
    fc(32, luminance_min, 0, FFMIN(((uint64_t)current->luminance_max << 6) - 1,
                                   MAX_UINT_BITS(32)));

    return 0;
}

static int FUNC(scalability_structure)(CodedBitstreamContext *ctx, RWContext *rw,
                                       AV1RawMetadataScalability *current)
{
    CodedBitstreamAV1Context *priv = ctx->priv_data;
    const AV1RawSequenceHeader *seq;
    int err, i, j;

    if (!priv->sequence_header) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "No sequence header available: "
               "unable to parse scalability metadata.\n");
        return AVERROR_INVALIDDATA;
    }
    seq = priv->sequence_header;

    fb(2, spatial_layers_cnt_minus_1);
    flag(spatial_layer_dimensions_present_flag);
    flag(spatial_layer_description_present_flag);
    flag(temporal_group_description_present_flag);
    fc(3, scalability_structure_reserved_3bits, 0, 0);
    if (current->spatial_layer_dimensions_present_flag) {
        for (i = 0; i <= current->spatial_layers_cnt_minus_1; i++) {
            fcs(16, spatial_layer_max_width[i],
                0, seq->max_frame_width_minus_1 + 1, 1, i);
            fcs(16, spatial_layer_max_height[i],
                0, seq->max_frame_height_minus_1 + 1, 1, i);
        }
    }
    if (current->spatial_layer_description_present_flag) {
        for (i = 0; i <= current->spatial_layers_cnt_minus_1; i++)
            fbs(8, spatial_layer_ref_id[i], 1, i);
    }
    if (current->temporal_group_description_present_flag) {
        fb(8, temporal_group_size);
        for (i = 0; i < current->temporal_group_size; i++) {
            fbs(3, temporal_group_temporal_id[i], 1, i);
            flags(temporal_group_temporal_switching_up_point_flag[i], 1, i);
            flags(temporal_group_spatial_switching_up_point_flag[i], 1, i);
            fbs(3, temporal_group_ref_cnt[i], 1, i);
            for (j = 0; j < current->temporal_group_ref_cnt[i]; j++) {
                fbs(8, temporal_group_ref_pic_diff[i][j], 2, i, j);
            }
        }
    }

    return 0;
}

static int FUNC(metadata_scalability)(CodedBitstreamContext *ctx, RWContext *rw,
                                      AV1RawMetadataScalability *current)
{
    int err;

    fb(8, scalability_mode_idc);

    if (current->scalability_mode_idc == AV1_SCALABILITY_SS)
        CHECK(FUNC(scalability_structure)(ctx, rw, current));

    return 0;
}

static int FUNC(metadata_itut_t35)(CodedBitstreamContext *ctx, RWContext *rw,
                                   AV1RawMetadataITUTT35 *current)
{
    int err;
    size_t i;

    fb(8, itu_t_t35_country_code);
    if (current->itu_t_t35_country_code == 0xff)
        fb(8, itu_t_t35_country_code_extension_byte);

#ifdef READ
    // The payload runs up to the start of the trailing bits, but there might
    // be arbitrarily many trailing zeroes so we need to read through twice.
    current->payload_size = cbs_av1_get_payload_bytes_left(rw);

    current->payload_ref = av_buffer_alloc(current->payload_size);
    if (!current->payload_ref)
        return AVERROR(ENOMEM);
    current->payload = current->payload_ref->data;
#endif

    for (i = 0; i < current->payload_size; i++)
        xf(8, itu_t_t35_payload_bytes[i], current->payload[i],
           0x00, 0xff, 1, i);

    return 0;
}

static int FUNC(metadata_timecode)(CodedBitstreamContext *ctx, RWContext *rw,
                                   AV1RawMetadataTimecode *current)
{
    int err;

    fb(5, counting_type);
    flag(full_timestamp_flag);
    flag(discontinuity_flag);
    flag(cnt_dropped_flag);
    fb(9, n_frames);

    if (current->full_timestamp_flag) {
        fc(6, seconds_value, 0, 59);
        fc(6, minutes_value, 0, 59);
        fc(5, hours_value,   0, 23);
    } else {
        flag(seconds_flag);
        if (current->seconds_flag) {
            fc(6, seconds_value, 0, 59);
            flag(minutes_flag);
            if (current->minutes_flag) {
                fc(6, minutes_value, 0, 59);
                flag(hours_flag);
                if (current->hours_flag)
                    fc(5, hours_value, 0, 23);
            }
        }
    }

    fb(5, time_offset_length);
    if (current->time_offset_length > 0)
        fb(current->time_offset_length, time_offset_value);
    else
        infer(time_offset_length, 0);

    return 0;
}

static int FUNC(metadata_obu)(CodedBitstreamContext *ctx, RWContext *rw,
                              AV1RawMetadata *current)
{
    int err;

    leb128(metadata_type);

    switch (current->metadata_type) {
    case AV1_METADATA_TYPE_HDR_CLL:
        CHECK(FUNC(metadata_hdr_cll)(ctx, rw, &current->metadata.hdr_cll));
        break;
    case AV1_METADATA_TYPE_HDR_MDCV:
        CHECK(FUNC(metadata_hdr_mdcv)(ctx, rw, &current->metadata.hdr_mdcv));
        break;
    case AV1_METADATA_TYPE_SCALABILITY:
        CHECK(FUNC(metadata_scalability)(ctx, rw, &current->metadata.scalability));
        break;
    case AV1_METADATA_TYPE_ITUT_T35:
        CHECK(FUNC(metadata_itut_t35)(ctx, rw, &current->metadata.itut_t35));
        break;
    case AV1_METADATA_TYPE_TIMECODE:
        CHECK(FUNC(metadata_timecode)(ctx, rw, &current->metadata.timecode));
        break;
    default:
        // Unknown metadata type.
        return AVERROR_PATCHWELCOME;
    }

    return 0;
}

static int FUNC(padding_obu)(CodedBitstreamContext *ctx, RWContext *rw,
                             AV1RawPadding *current)
{
    int i, err;

    HEADER("Padding");

#ifdef READ
    // The payload runs up to the start of the trailing bits, but there might
    // be arbitrarily many trailing zeroes so we need to read through twice.
    current->payload_size = cbs_av1_get_payload_bytes_left(rw);

    current->payload_ref = av_buffer_alloc(current->payload_size);
    if (!current->payload_ref)
        return AVERROR(ENOMEM);
    current->payload = current->payload_ref->data;
#endif

    for (i = 0; i < current->payload_size; i++)
        xf(8, obu_padding_byte[i], current->payload[i], 0x00, 0xff, 1, i);

    return 0;
}
