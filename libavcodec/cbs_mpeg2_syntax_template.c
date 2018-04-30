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

static int FUNC(sequence_header)(CodedBitstreamContext *ctx, RWContext *rw,
                                 MPEG2RawSequenceHeader *current)
{
    CodedBitstreamMPEG2Context *mpeg2 = ctx->priv_data;
    int err, i;

    HEADER("Sequence Header");

    ui(8,  sequence_header_code);

    ui(12, horizontal_size_value);
    ui(12, vertical_size_value);

    mpeg2->horizontal_size = current->horizontal_size_value;
    mpeg2->vertical_size   = current->vertical_size_value;

    ui(4,  aspect_ratio_information);
    ui(4,  frame_rate_code);
    ui(18, bit_rate_value);

    marker_bit();

    ui(10, vbv_buffer_size_value);
    ui(1,  constrained_parameters_flag);

    ui(1, load_intra_quantiser_matrix);
    if (current->load_intra_quantiser_matrix) {
        for (i = 0; i < 64; i++)
            uis(8, intra_quantiser_matrix[i], 1, i);
    }

    ui(1, load_non_intra_quantiser_matrix);
    if (current->load_non_intra_quantiser_matrix) {
        for (i = 0; i < 64; i++)
            uis(8, non_intra_quantiser_matrix[i], 1, i);
    }

    return 0;
}

static int FUNC(user_data)(CodedBitstreamContext *ctx, RWContext *rw,
                           MPEG2RawUserData *current)
{
    size_t k;
    int err;

    HEADER("User Data");

    ui(8, user_data_start_code);

#ifdef READ
    k = get_bits_left(rw);
    av_assert0(k % 8 == 0);
    current->user_data_length = k /= 8;
    if (k > 0) {
        current->user_data_ref = av_buffer_alloc(k);
        if (!current->user_data_ref)
            return AVERROR(ENOMEM);
        current->user_data = current->user_data_ref->data;
    }
#endif

    for (k = 0; k < current->user_data_length; k++)
        xui(8, user_data, current->user_data[k], 0);

    return 0;
}

static int FUNC(sequence_extension)(CodedBitstreamContext *ctx, RWContext *rw,
                                    MPEG2RawSequenceExtension *current)
{
    CodedBitstreamMPEG2Context *mpeg2 = ctx->priv_data;
    int err;

    HEADER("Sequence Extension");

    ui(8,  profile_and_level_indication);
    ui(1,  progressive_sequence);
    ui(2,  chroma_format);
    ui(2,  horizontal_size_extension);
    ui(2,  vertical_size_extension);

    mpeg2->horizontal_size = (mpeg2->horizontal_size & 0xfff) |
        current->horizontal_size_extension << 12;
    mpeg2->vertical_size = (mpeg2->vertical_size & 0xfff) |
        current->vertical_size_extension << 12;
    mpeg2->progressive_sequence = current->progressive_sequence;

    ui(12, bit_rate_extension);
    marker_bit();
    ui(8,  vbv_buffer_size_extension);
    ui(1,  low_delay);
    ui(2,  frame_rate_extension_n);
    ui(5,  frame_rate_extension_d);

    return 0;
}

static int FUNC(sequence_display_extension)(CodedBitstreamContext *ctx, RWContext *rw,
                                            MPEG2RawSequenceDisplayExtension *current)
{
    int err;

    HEADER("Sequence Display Extension");

    ui(3, video_format);

    ui(1, colour_description);
    if (current->colour_description) {
        ui(8, colour_primaries);
        ui(8, transfer_characteristics);
        ui(8, matrix_coefficients);
    }

    ui(14, display_horizontal_size);
    marker_bit();
    ui(14, display_vertical_size);

    return 0;
}

static int FUNC(group_of_pictures_header)(CodedBitstreamContext *ctx, RWContext *rw,
                                          MPEG2RawGroupOfPicturesHeader *current)
{
    int err;

    HEADER("Group of Pictures Header");

    ui(8,  group_start_code);

    ui(25, time_code);
    ui(1,  closed_gop);
    ui(1,  broken_link);

    return 0;
}

static int FUNC(picture_header)(CodedBitstreamContext *ctx, RWContext *rw,
                                MPEG2RawPictureHeader *current)
{
    int err;

    HEADER("Picture Header");

    ui(8,  picture_start_code);

    ui(10, temporal_reference);
    ui(3,  picture_coding_type);
    ui(16, vbv_delay);

    if (current->picture_coding_type == 2 ||
        current->picture_coding_type == 3) {
        ui(1, full_pel_forward_vector);
        ui(3, forward_f_code);
    }

    if (current->picture_coding_type == 3) {
        ui(1, full_pel_backward_vector);
        ui(3, backward_f_code);
    }

    ui(1, extra_bit_picture);

    return 0;
}

static int FUNC(picture_coding_extension)(CodedBitstreamContext *ctx, RWContext *rw,
                                          MPEG2RawPictureCodingExtension *current)
{
    CodedBitstreamMPEG2Context *mpeg2 = ctx->priv_data;
    int err;

    HEADER("Picture Coding Extension");

    ui(4, f_code[0][0]);
    ui(4, f_code[0][1]);
    ui(4, f_code[1][0]);
    ui(4, f_code[1][1]);

    ui(2, intra_dc_precision);
    ui(2, picture_structure);
    ui(1, top_field_first);
    ui(1, frame_pred_frame_dct);
    ui(1, concealment_motion_vectors);
    ui(1, q_scale_type);
    ui(1, intra_vlc_format);
    ui(1, alternate_scan);
    ui(1, repeat_first_field);
    ui(1, chroma_420_type);
    ui(1, progressive_frame);

    if (mpeg2->progressive_sequence) {
        if (current->repeat_first_field) {
            if (current->top_field_first)
                mpeg2->number_of_frame_centre_offsets = 3;
            else
                mpeg2->number_of_frame_centre_offsets = 2;
        } else {
            mpeg2->number_of_frame_centre_offsets = 1;
        }
    } else {
        if (current->picture_structure == 1 || // Top field.
            current->picture_structure == 2) { // Bottom field.
            mpeg2->number_of_frame_centre_offsets = 1;
        } else {
            if (current->repeat_first_field)
                mpeg2->number_of_frame_centre_offsets = 3;
            else
                mpeg2->number_of_frame_centre_offsets = 2;
        }
    }

    ui(1, composite_display_flag);
    if (current->composite_display_flag) {
        ui(1, v_axis);
        ui(3, field_sequence);
        ui(1, sub_carrier);
        ui(7, burst_amplitude);
        ui(8, sub_carrier_phase);
    }

    return 0;
}

static int FUNC(quant_matrix_extension)(CodedBitstreamContext *ctx, RWContext *rw,
                                        MPEG2RawQuantMatrixExtension *current)
{
    int err, i;

    HEADER("Quant Matrix Extension");

    ui(1, load_intra_quantiser_matrix);
    if (current->load_intra_quantiser_matrix) {
        for (i = 0; i < 64; i++)
            uis(8, intra_quantiser_matrix[i], 1, i);
    }

    ui(1, load_non_intra_quantiser_matrix);
    if (current->load_non_intra_quantiser_matrix) {
        for (i = 0; i < 64; i++)
            uis(8, non_intra_quantiser_matrix[i], 1, i);
    }

    ui(1, load_chroma_intra_quantiser_matrix);
    if (current->load_chroma_intra_quantiser_matrix) {
        for (i = 0; i < 64; i++)
            uis(8, intra_quantiser_matrix[i], 1, i);
    }

    ui(1, load_chroma_non_intra_quantiser_matrix);
    if (current->load_chroma_non_intra_quantiser_matrix) {
        for (i = 0; i < 64; i++)
            uis(8, chroma_non_intra_quantiser_matrix[i], 1, i);
    }

    return 0;
}

static int FUNC(picture_display_extension)(CodedBitstreamContext *ctx, RWContext *rw,
                                           MPEG2RawPictureDisplayExtension *current)
{
    CodedBitstreamMPEG2Context *mpeg2 = ctx->priv_data;
    int err, i;

    HEADER("Picture Display Extension");

    for (i = 0; i < mpeg2->number_of_frame_centre_offsets; i++) {
        ui(16, frame_centre_horizontal_offset[i]);
        marker_bit();
        ui(16, frame_centre_vertical_offset[i]);
        marker_bit();
    }

    return 0;
}

static int FUNC(extension_data)(CodedBitstreamContext *ctx, RWContext *rw,
                                MPEG2RawExtensionData *current)
{
    int err;

    HEADER("Extension Data");

    ui(8, extension_start_code);
    ui(4, extension_start_code_identifier);

    switch (current->extension_start_code_identifier) {
    case 1:
        return FUNC(sequence_extension)
            (ctx, rw, &current->data.sequence);
    case 2:
        return FUNC(sequence_display_extension)
            (ctx, rw, &current->data.sequence_display);
    case 3:
        return FUNC(quant_matrix_extension)
            (ctx, rw, &current->data.quant_matrix);
    case 7:
        return FUNC(picture_display_extension)
            (ctx, rw, &current->data.picture_display);
    case 8:
        return FUNC(picture_coding_extension)
            (ctx, rw, &current->data.picture_coding);
    default:
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid extension ID %d.\n",
               current->extension_start_code_identifier);
        return AVERROR_INVALIDDATA;
    }
}

static int FUNC(slice_header)(CodedBitstreamContext *ctx, RWContext *rw,
                              MPEG2RawSliceHeader *current)
{
    CodedBitstreamMPEG2Context *mpeg2 = ctx->priv_data;
    int err;

    HEADER("Slice Header");

    ui(8, slice_vertical_position);

    if (mpeg2->vertical_size > 2800)
        ui(3, slice_vertical_position_extension);
    if (mpeg2->scalable) {
        if (mpeg2->scalable_mode == 0)
            ui(7, priority_breakpoint);
    }

    ui(5, quantiser_scale_code);

    if (nextbits(1, 1, current->slice_extension_flag)) {
        ui(1, slice_extension_flag);
        ui(1, intra_slice);
        ui(1, slice_picture_id_enable);
        ui(6, slice_picture_id);

        {
            size_t k;
#ifdef READ
            GetBitContext start;
            uint8_t bit;
            start = *rw;
            for (k = 0; nextbits(1, 1, bit); k++)
                skip_bits(rw, 8);
            current->extra_information_length = k;
            if (k > 0) {
                *rw = start;
                current->extra_information =
                    av_malloc(current->extra_information_length);
                if (!current->extra_information)
                    return AVERROR(ENOMEM);
                for (k = 0; k < current->extra_information_length; k++) {
                    xui(1, extra_bit_slice, bit, 0);
                    xui(8, extra_information_slice[k],
                        current->extra_information[k], 1, k);
                }
            }
#else
            for (k = 0; k < current->extra_information_length; k++) {
                xui(1, extra_bit_slice, 1, 0);
                xui(8, extra_information_slice[k],
                    current->extra_information[k], 1, k);
            }
#endif
        }
    }
    ui(1, extra_bit_slice);

    return 0;
}
