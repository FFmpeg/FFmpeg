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

static int FUNC(byte_alignment)(CodedBitstreamContext *ctx, RWContext *rw)
{
    int err;

    // ISO/IEC 23094-2:2021/FDAM 1:2023(E) 7.3.12
    while (byte_alignment(rw) != 0)
        fixed(1, alignment_bit_equal_to_zero, 0);

    return 0;
}

static int FUNC(rbsp_trailing_bits)(CodedBitstreamContext *ctx, RWContext *rw)
{
    int err;

    fixed(1, rbsp_stop_one_bit, 1);
    while (byte_alignment(rw) != 0)
        fixed(1, rbsp_alignment_zero_bit, 0);

    return 0;
}

static int FUNC(nal_unit_header)(CodedBitstreamContext *ctx, RWContext *rw,
                                 LCEVCRawNALUnitHeader *current,
                                 uint32_t valid_type_mask)
{
    int err;

    fixed(1, forbidden_zero_bit, 0);
    fixed(1, forbidden_one_bit, 1);
    ub(5, nal_unit_type);

    if (!(1 << current->nal_unit_type & valid_type_mask)) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid NAL unit type %d.\n",
               current->nal_unit_type);
        return AVERROR_INVALIDDATA;
    }

    ub(9, reserved_flag);

    return 0;
}

LCEVC_BLOCK_FUNC(global_config, (CodedBitstreamContext *ctx, RWContext *rw,
                                 LCEVCRawGlobalConfig *current,
                                 LCEVCProcessBlockState *state,
                                 int nal_unit_type))
{
    CodedBitstreamLCEVCContext *priv = ctx->priv_data;
    int err;

    HEADER("Global Config");

    flag(processed_planes_type_flag);
    ub(6, resolution_type);
    ub(1, transform_type);
    ub(2, chroma_sampling_type);
    ub(2, base_depth_type);
    ub(2, enhancement_depth_type);
    flag(temporal_step_width_modifier_signalled_flag);
    flag(predicted_residual_mode_flag);
    flag(temporal_tile_intra_signalling_enabled_flag);
    flag(temporal_enabled_flag);
    ub(3, upsample_type);
    flag(level1_filtering_signalled_flag);
    ub(2, scaling_mode_level1);
    ub(2, scaling_mode_level2);
    ub(2, tile_dimensions_type);
    ub(2, user_data_enabled);
    flag(level1_depth_flag);
    flag(chroma_step_width_flag);

    if (current->processed_planes_type_flag) {
        ub(4, planes_type);
        ub(4, reserved_zeros_4bit);
    } else
        infer(planes_type, 0);

    if (current->temporal_step_width_modifier_signalled_flag) {
        ub(8, temporal_step_width_modifier);
    }

    if (current->upsample_type == 4) {
        ub(16, upsampler_coeff1);
        ub(16, upsampler_coeff2);
        ub(16, upsampler_coeff3);
        ub(16, upsampler_coeff4);
    }

    if (current->level1_filtering_signalled_flag) {
        ub(4, level1_filtering_first_coefficient);
        ub(4, level1_filtering_second_coefficient);
    }

    if (current->tile_dimensions_type > 0) {
        if (current->tile_dimensions_type == 3) {
            ub(16, custom_tile_width);
            ub(16, custom_tile_height);
        }
        ub(5, reserved_zeros_5bit);
        flag(compression_type_entropy_enabled_per_tile_flag);
        ub(2, compression_type_size_per_tile);
    }

    if (current->resolution_type == 63) {
        ub(16, custom_resolution_width);
        ub(16, custom_resolution_height);
    }
    if (current->chroma_step_width_flag) {
        ub(8, chroma_step_width_multiplier);
    } else {
        infer(chroma_step_width_multiplier, 64);
    }

    av_refstruct_replace(&priv->gc, current);

    return 0;
}

LCEVC_BLOCK_FUNC(sequence_config, (CodedBitstreamContext *ctx, RWContext *rw,
                                   LCEVCRawSequenceConfig *current,
                                   LCEVCProcessBlockState *state,
                                   int nal_unit_type))
{
    CodedBitstreamLCEVCContext *priv = ctx->priv_data;
    int err;

    HEADER("Sequence Config");

    ub(4, profile_idc);
    ub(4, level_idc);
    ub(2, sublevel_idc);
    flag(conformance_window_flag);
    ub(5, reserved_zeros_5bit);

    if (current->profile_idc == 15 || current->level_idc == 15) {
        ub(3, profile_idc);
        ub(4, level_idc);
        ub(1, reserved_zeros_1bit);
    }
    if (current->conformance_window_flag == 1) {
        mb(conf_win_left_offset);
        mb(conf_win_right_offset);
        mb(conf_win_top_offset);
        mb(conf_win_bottom_offset);
    }

    av_refstruct_replace(&priv->sc, current);

    return 0;
}

LCEVC_BLOCK_FUNC(picture_config, (CodedBitstreamContext *ctx, RWContext *rw,
                                  LCEVCRawPictureConfig *current,
                                  LCEVCProcessBlockState *state,
                                  int nal_unit_type))
{
    CodedBitstreamLCEVCContext *priv = ctx->priv_data;
    int nlayers, err;

    HEADER("Picture Config");

    if (!priv->gc)
        return AVERROR_INVALIDDATA;

    flag(no_enhancement_bit_flag);
    if (current->no_enhancement_bit_flag == 0) {
        ub(3, quant_matrix_mode);
        flag(dequant_offset_signalled_flag);
        flag(picture_type_bit_flag);
        flag(temporal_refresh_bit_flag);
        flag(step_width_sublayer1_enabled_flag);
        ub(15, step_width_sublayer2);
        flag(dithering_control_flag);
        priv->dithering_control_flag = current->dithering_control_flag;
        infer(temporal_signalling_present_flag, priv->gc->temporal_enabled_flag &&
                                                !current->temporal_refresh_bit_flag);
    } else {
        infer(quant_matrix_mode, 0);
        infer(dequant_offset_signalled_flag, 0);
        ub(4, reserved_zeros_4bit);
        flag(picture_type_bit_flag);
        flag(temporal_refresh_bit_flag);
        infer(step_width_sublayer1_enabled_flag, 0);
        infer(dithering_control_flag, nal_unit_type == LCEVC_IDR_NUT
                                     ? 0
                                     : priv->dithering_control_flag);
        priv->dithering_control_flag = current->dithering_control_flag;
        flag(temporal_signalling_present_flag);
    }

    if (current->picture_type_bit_flag == 1) {
        flag(field_type_bit_flag);
        ub(7, reserved_zeros_7bit);
    }

    if (current->step_width_sublayer1_enabled_flag == 1) {
        ub(15, step_width_sublayer1);
        flag(level1_filtering_enabled_flag);
    } else
        infer(level1_filtering_enabled_flag, 0);

    nlayers = priv->gc->transform_type ? 16 : 4;
    if (current->quant_matrix_mode == 2 ||
        current->quant_matrix_mode == 3 ||
        current->quant_matrix_mode == 5) {
        for (int layer_idx = 0; layer_idx < nlayers; layer_idx++)
            ubs(8, qm_coefficient_0[layer_idx], 1, layer_idx);
    }

    if (current->quant_matrix_mode == 4 || current->quant_matrix_mode == 5) {
        for (int layer_idx = 0; layer_idx < nlayers; layer_idx++)
            ubs(8, qm_coefficient_1[layer_idx], 1, layer_idx);
    }

    if (current->dequant_offset_signalled_flag) {
        flag(dequant_offset_mode_flag);
        ub(7, dequant_offset);
    }

    if (current->dithering_control_flag == 1) {
        ub(2, dithering_type);
        ub(1, reserverd_zero);
        if (current->dithering_type != 0) {
            ub(5, dithering_strength);
        } else {
            ub(5, reserved_zeros_5bit);
        }
    }

    av_refstruct_replace(&priv->pc, current);
    av_refstruct_replace(&current->gc, priv->gc);

    return 0;
}

LCEVC_BLOCK_FUNC(encoded_data, (CodedBitstreamContext *ctx, RWContext *rw,
                                LCEVCRawEncodedData *current,
                                LCEVCProcessBlockState *state,
                                int nal_unit_type))
{
    CodedBitstreamLCEVCContext *priv = ctx->priv_data;
    int nplanes, nlayers, err;
#ifdef READ
    int start = get_bits_count(rw);
#endif

    HEADER("Encoded Data");

    if (!priv->gc || !priv->pc)
        return AVERROR_INVALIDDATA;

    nplanes = priv->gc->planes_type ? 3 : 1;
    nlayers = priv->gc->transform_type ? 16 : 4;
    for (int plane_idx = 0; plane_idx < nplanes; plane_idx++) {
        if (priv->pc->no_enhancement_bit_flag == 0) {
            for (int level_idx = 1; level_idx <= 2; level_idx++) {
                for (int layer_idx = 0; layer_idx < nlayers; layer_idx++) {
                    ubs(1, surfaces_entropy_enabled_flag[plane_idx][level_idx][layer_idx], 3, plane_idx, level_idx, layer_idx);
                    ubs(1, surfaces_rle_only_flag[plane_idx][level_idx][layer_idx], 3, plane_idx, level_idx, layer_idx);
                }
            }
        }
        if (priv->pc->temporal_signalling_present_flag == 1) {
            ubs(1, temporal_surfaces_entropy_enabled_flag[plane_idx], 1, plane_idx);
            ubs(1, temporal_surfaces_rle_only_flag[plane_idx], 1, plane_idx);
        }
    }

    CHECK(FUNC(byte_alignment)(ctx, rw));

#ifdef READ
    if (!ff_cbs_h2645_read_more_rbsp_data(rw))
        return AVERROR_INVALIDDATA;

    int pos = get_bits_count(rw) - start;
    int len = state->payload_size;

    current->header_size = pos / 8;
    current->data_size = len - pos / 8;
    skip_bits_long(rw, current->data_size * 8);
#else
    err = ff_cbs_h2645_write_slice_data(ctx, rw, current->data,
                                     current->data_size, 0);
    if (err < 0)
        return err;
#endif

    av_refstruct_replace(&current->sc, priv->sc);
    av_refstruct_replace(&current->gc, priv->gc);
    av_refstruct_replace(&current->pc, priv->pc);

    return 0;
}

static int FUNC(sei_payload)(CodedBitstreamContext *ctx, RWContext *rw,
                             LCEVCRawSEI *current, int payload_size)
{
    SEIRawMessage *message = &current->message;
    int sei_type;
    int i, err;

    ub(8, payload_type);

    if (current->payload_type == 1)
        sei_type = SEI_TYPE_MASTERING_DISPLAY_COLOUR_VOLUME;
    else if (current->payload_type == 2)
        sei_type = SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO;
    else if (current->payload_type == 4)
        sei_type = SEI_TYPE_USER_DATA_REGISTERED_ITU_T_T35;
    else if (current->payload_type == 5)
        sei_type = SEI_TYPE_USER_DATA_UNREGISTERED;
    else {
        uint8_t *data;

#ifdef READ
        current->payload_size = payload_size;
        allocate(current->payload_ref, current->payload_size);
        current->payload = current->payload_ref;
#else
        allocate(current->payload, current->payload_size);
#endif
        data = current->payload;

        for (i = 0; i < current->payload_size; i++)
            xu(8, reserved_sei_message_payload_byte[i], data[i], 0, 255, 1, i);

        return 0;
    }

    message->payload_type = sei_type;
    message->payload_size = payload_size;

    CHECK(FUNC_SEI(message)(ctx, rw, message));

    return 0;
}

static int FUNC(vui_parameters)(CodedBitstreamContext *ctx, RWContext *rw,
                                LCEVCRawVUI *current)
{
    int err;

    HEADER("VUI Parameters");

    flag(aspect_ratio_info_present_flag);
    if (current->aspect_ratio_info_present_flag) {
        ub(8, aspect_ratio_idc);

        if (current->aspect_ratio_idc == 255) {
            ub(16, sar_width);
            ub(16, sar_height);
        }
    } else {
        infer(aspect_ratio_idc, 0);
    }

    flag(overscan_info_present_flag);
    if (current->overscan_info_present_flag)
        flag(overscan_appropriate_flag);
    else
        infer(overscan_appropriate_flag, 0);

    flag(video_signal_type_present_flag);
    if (current->video_signal_type_present_flag) {
        u(3, video_format, 0, 5);
        flag(video_full_range_flag);
        flag(colour_description_present_flag);
        if (current->colour_description_present_flag) {
            ub(8, colour_primaries);
            ub(8, transfer_characteristics);
            ub(8, matrix_coefficients);
        } else {
            infer(colour_primaries, 2);
            infer(transfer_characteristics, 2);
            infer(matrix_coefficients, 2);
        }
    } else {
        infer(video_format, 5);
        infer(video_full_range_flag, 0);
        infer(colour_description_present_flag, 0);
        infer(colour_primaries, 2);
        infer(transfer_characteristics, 2);
        infer(matrix_coefficients, 2);
    }
    flag(chroma_loc_info_present_flag);
    if (current->chroma_loc_info_present_flag) {
        ue(chroma_sample_loc_type_top_field, 0, 5);
        ue(chroma_sample_loc_type_bottom_field, 0, 5);
    } else {
        infer(chroma_sample_loc_type_top_field, 0);
        infer(chroma_sample_loc_type_bottom_field, 0);
    }

    return 0;
}

LCEVC_BLOCK_FUNC(additional_info, (CodedBitstreamContext *ctx, RWContext *rw,
                                   LCEVCRawAdditionalInfo *current,
                                   LCEVCProcessBlockState *state,
                                   int nal_unit_type))
{
    int i, err;

    HEADER("Additional Info");

    ub(8, additional_info_type);

    if (current->additional_info_type == LCEVC_ADDITIONAL_INFO_TYPE_SEI) {
        CHECK(FUNC(sei_payload)(ctx, rw, &current->sei, state->payload_size - 2));
    } else if (current->additional_info_type == LCEVC_ADDITIONAL_INFO_TYPE_VUI)
        CHECK(FUNC(vui_parameters)(ctx, rw, &current->vui));
    else {
        uint8_t *data;

#ifdef READ
        current->payload_size = state->payload_size - 1;
        allocate(current->payload_ref, current->payload_size);
        current->payload = current->payload_ref;
#else
        allocate(current->payload, current->payload_size);
#endif
        data = current->payload;

        for (i = 0; i < current->payload_size; i++)
            xu(8, additional_info_byte[i], data[i], 0, 255, 1, i);
    }

    return 0;
}

LCEVC_BLOCK_FUNC(filler, (CodedBitstreamContext *ctx, RWContext *rw,
                          LCEVCRawFiller *current,
                          LCEVCProcessBlockState *state,
                          int nal_unit_type))
{
    int err;

    HEADER("Filler");


#ifdef READ
    while (show_bits(rw, 8) == 0xaa) {
        fixed(8, filler_byte, 0xaa);
        ++current->filler_size;
    }
    if (state->payload_size != current->filler_size)
        return AVERROR_INVALIDDATA;

#else
    for (int i = 0; i < current->filler_size; i++)
        fixed(8, filler_byte, 0xaa);
#endif

    return 0;
}

static int FUNC(process_block)(CodedBitstreamContext *ctx, RWContext *rw,
                               LCEVCRawProcessBlock *current,
                               int nal_unit_type)
{
    const LCEVCProcessBlockTypeDescriptor *desc;
    int err, i;

    desc = ff_cbs_lcevc_process_block_find_type(ctx, current->payload_type);
    if (desc) {
        LCEVCProcessBlockState state = {
            .payload_type      = current->payload_type,
            .payload_size      = current->payload_size,
            .extension_present = current->extension_bit_length > 0,
        };
        int start_position, current_position, bits_written;

#ifdef READ
        CHECK(ff_cbs_lcevc_alloc_process_block_payload(current, desc));
#endif

        start_position = bit_position(rw);

        CHECK(desc->READWRITE(ctx, rw, current->payload, &state, nal_unit_type));

        current_position = bit_position(rw);
        bits_written = current_position - start_position;

        if (byte_alignment(rw) || state.extension_present ||
            bits_written < 8 * current->payload_size) {
            size_t bits_left;

#ifdef READ
            GetBitContext tmp = *rw;
            int trailing_bits, trailing_zero_bits;

            bits_left = 8 * current->payload_size - bits_written;
            if (bits_left > 8)
                skip_bits_long(&tmp, bits_left - 8);
            trailing_bits = get_bits(&tmp, FFMIN(bits_left, 8));
            if (trailing_bits == 0) {
                // The trailing bits must contain a payload_bit_equal_to_one, so
                // they can't all be zero.
                return AVERROR_INVALIDDATA;
            }
            trailing_zero_bits = ff_ctz(trailing_bits);
            current->extension_bit_length =
                bits_left - 1 - trailing_zero_bits;
#endif

            if (current->extension_bit_length > 0) {
                allocate(current->extension_data,
                         (current->extension_bit_length + 7) / 8);

                bits_left = current->extension_bit_length;
                for (i = 0; bits_left > 0; i++) {
                    int length = FFMIN(bits_left, 8);
                    xu(length, reserved_payload_extension_data,
                       current->extension_data[i],
                       0, MAX_UINT_BITS(length), 0);
                    bits_left -= length;
                }
            }

            fixed(1, payload_bit_equal_to_one, 1);
            while (byte_alignment(rw))
                fixed(1, payload_bit_equal_to_zero, 0);
        }

#ifdef WRITE
        current->payload_size = (put_bits_count(rw) - start_position) / 8;
#endif
    } else {
        uint8_t *data;

#ifdef READ
        allocate(current->payload_ref, current->payload_size);
        current->payload = current->payload_ref;
#else
        allocate(current->payload, current->payload_size);
#endif
        data = current->payload;

        for (i = 0; i < current->payload_size; i++)
            xu(8, payload_byte[i], data[i], 0, 255, 1, i);
    }

    return 0;
}

static int FUNC(process_block_list)(CodedBitstreamContext *ctx, RWContext *rw,
                                    LCEVCRawProcessBlockList *current,
                                    int nal_unit_type)
{
    LCEVCRawProcessBlock *block;
    int err, k;

#ifdef READ
    for (k = 0;; k++) {
        int payload_size_type;
        int payload_type;
        uint32_t payload_size;
        uint32_t tmp;
        GetBitContext payload_gbc;

        HEADER("Process Block");

        xu(3, payload_size_type, tmp, 0, MAX_UINT_BITS(3), 0);
        payload_size_type = tmp;
        xu(5, payload_type, tmp, 0, MAX_UINT_BITS(5), 0);
        payload_type = tmp;

        if (payload_size_type == 6) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "payload_size_type == 6\n");
            return AVERROR_INVALIDDATA;
        }

        payload_size = payload_size_type;
        if (payload_size_type == 7)
            xmb(custom_byte_size, payload_size);

        // There must be space remaining for the payload
        if (payload_size > get_bits_left(rw) / 8) {
            av_log(ctx->log_ctx, AV_LOG_ERROR,
                   "Invalid process block: payload_size too large "
                   "(%"PRIu32" bytes).\n", payload_size);
            return AVERROR_INVALIDDATA;
        }
        CHECK(init_get_bits(&payload_gbc, rw->buffer,
                            get_bits_count(rw) + 8 * payload_size));
        skip_bits_long(&payload_gbc, get_bits_count(rw));

        CHECK(ff_cbs_lcevc_list_add(current, -1));
        block = &current->blocks[k];

        block->payload_type = payload_type;
        block->payload_size = payload_size;

        CHECK(FUNC(process_block)(ctx, &payload_gbc, block, nal_unit_type));

        skip_bits_long(rw, 8 * payload_size);

        if (!ff_cbs_h2645_read_more_rbsp_data(rw))
            break;
    }
#else
    for (k = 0; k < current->nb_blocks; k++) {
        PutBitContext start_state;
        uint32_t tmp;
        int trace, i;

        block = &current->blocks[k];

        // We write the payload twice in order to find the size. Trace
        // output is switched off for the first write.
        trace = ctx->trace_enable;
        ctx->trace_enable = 0;

        start_state = *rw;
        for (i = 0; i < 2; i++) {
            *rw = start_state;

            tmp = FFMIN(block->payload_size, 7);
            xu(3, payload_size_type, tmp, 0, 7, 0);
            xu(5, payload_type, block->payload_type, 0, MAX_UINT_BITS(5), 0);

            if (tmp == 7)
                xmb(custom_byte_size, block->payload_size);

            err = FUNC(process_block)(ctx, rw, block, nal_unit_type);
            ctx->trace_enable = trace;
            if (err < 0)
                return err;
        }
    }
#endif

    return 0;
}

static int FUNC(nal)(CodedBitstreamContext *ctx, RWContext *rw,
                     LCEVCRawNAL *current, int nal_unit_type)
{
    int err;

    if (nal_unit_type == LCEVC_NON_IDR_NUT)
        HEADER("Non IDR");
    else
        HEADER("IDR");

    CHECK(FUNC(nal_unit_header)(ctx, rw, &current->nal_unit_header,
                                (1 << LCEVC_IDR_NUT) | (1 << LCEVC_NON_IDR_NUT)));

    CHECK(FUNC(process_block_list) (ctx, rw, &current->process_block_list,
                                    current->nal_unit_header.nal_unit_type));

    CHECK(FUNC(rbsp_trailing_bits)(ctx, rw));

    return 0;
}
