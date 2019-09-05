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

    uir(12, horizontal_size_value);
    uir(12, vertical_size_value);

    mpeg2->horizontal_size = current->horizontal_size_value;
    mpeg2->vertical_size   = current->vertical_size_value;

    uir(4, aspect_ratio_information);
    uir(4, frame_rate_code);
    ui(18, bit_rate_value);

    marker_bit();

    ui(10, vbv_buffer_size_value);
    ui(1,  constrained_parameters_flag);

    ui(1, load_intra_quantiser_matrix);
    if (current->load_intra_quantiser_matrix) {
        for (i = 0; i < 64; i++)
            uirs(8, intra_quantiser_matrix[i], 1, i);
    }

    ui(1, load_non_intra_quantiser_matrix);
    if (current->load_non_intra_quantiser_matrix) {
        for (i = 0; i < 64; i++)
            uirs(8, non_intra_quantiser_matrix[i], 1, i);
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
        current->user_data_ref = av_buffer_allocz(k + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!current->user_data_ref)
            return AVERROR(ENOMEM);
        current->user_data = current->user_data_ref->data;
    }
#endif

    for (k = 0; k < current->user_data_length; k++)
        uis(8, user_data[k], 1, k);

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
#ifdef READ
#define READ_AND_PATCH(name) do { \
        ui(8, name); \
        if (current->name == 0) { \
            current->name = 2; \
            av_log(ctx->log_ctx, AV_LOG_WARNING, "%s in a sequence display " \
                   "extension had the invalid value 0. Setting it to 2 " \
                   "(meaning unknown) instead.\n", #name); \
        } \
    } while (0)
        READ_AND_PATCH(colour_primaries);
        READ_AND_PATCH(transfer_characteristics);
        READ_AND_PATCH(matrix_coefficients);
#undef READ_AND_PATCH
#else
        uir(8, colour_primaries);
        uir(8, transfer_characteristics);
        uir(8, matrix_coefficients);
#endif
    } else {
        infer(colour_primaries,         2);
        infer(transfer_characteristics, 2);
        infer(matrix_coefficients,      2);
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

static int FUNC(extra_information)(CodedBitstreamContext *ctx, RWContext *rw,
                                   MPEG2RawExtraInformation *current,
                                   const char *element_name, const char *marker_name)
{
    int err;
    size_t k;
#ifdef READ
    GetBitContext start = *rw;
    uint8_t bit;

    for (k = 0; nextbits(1, 1, bit); k++)
        skip_bits(rw, 1 + 8);
    current->extra_information_length = k;
    if (k > 0) {
        *rw = start;
        current->extra_information_ref =
            av_buffer_allocz(k + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!current->extra_information_ref)
            return AVERROR(ENOMEM);
        current->extra_information = current->extra_information_ref->data;
    }
#endif

    for (k = 0; k < current->extra_information_length; k++) {
        bit(marker_name, 1);
        xuia(8, element_name,
             current->extra_information[k], 0, 255, 1, k);
    }

    bit(marker_name, 0);

    return 0;
}

static int FUNC(picture_header)(CodedBitstreamContext *ctx, RWContext *rw,
                                MPEG2RawPictureHeader *current)
{
    int err;

    HEADER("Picture Header");

    ui(8,  picture_start_code);

    ui(10, temporal_reference);
    uir(3, picture_coding_type);
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

    CHECK(FUNC(extra_information)(ctx, rw, &current->extra_information_picture,
                                  "extra_information_picture[k]", "extra_bit_picture"));

    return 0;
}

static int FUNC(picture_coding_extension)(CodedBitstreamContext *ctx, RWContext *rw,
                                          MPEG2RawPictureCodingExtension *current)
{
    CodedBitstreamMPEG2Context *mpeg2 = ctx->priv_data;
    int err;

    HEADER("Picture Coding Extension");

    uir(4, f_code[0][0]);
    uir(4, f_code[0][1]);
    uir(4, f_code[1][0]);
    uir(4, f_code[1][1]);

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
            uirs(8, intra_quantiser_matrix[i], 1, i);
    }

    ui(1, load_non_intra_quantiser_matrix);
    if (current->load_non_intra_quantiser_matrix) {
        for (i = 0; i < 64; i++)
            uirs(8, non_intra_quantiser_matrix[i], 1, i);
    }

    ui(1, load_chroma_intra_quantiser_matrix);
    if (current->load_chroma_intra_quantiser_matrix) {
        for (i = 0; i < 64; i++)
            uirs(8, intra_quantiser_matrix[i], 1, i);
    }

    ui(1, load_chroma_non_intra_quantiser_matrix);
    if (current->load_chroma_non_intra_quantiser_matrix) {
        for (i = 0; i < 64; i++)
            uirs(8, chroma_non_intra_quantiser_matrix[i], 1, i);
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
        sis(16, frame_centre_horizontal_offset[i], 1, i);
        marker_bit();
        sis(16, frame_centre_vertical_offset[i],   1, i);
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
    case MPEG2_EXTENSION_SEQUENCE:
        return FUNC(sequence_extension)
            (ctx, rw, &current->data.sequence);
    case MPEG2_EXTENSION_SEQUENCE_DISPLAY:
        return FUNC(sequence_display_extension)
            (ctx, rw, &current->data.sequence_display);
    case MPEG2_EXTENSION_QUANT_MATRIX:
        return FUNC(quant_matrix_extension)
            (ctx, rw, &current->data.quant_matrix);
    case MPEG2_EXTENSION_PICTURE_DISPLAY:
        return FUNC(picture_display_extension)
            (ctx, rw, &current->data.picture_display);
    case MPEG2_EXTENSION_PICTURE_CODING:
        return FUNC(picture_coding_extension)
            (ctx, rw, &current->data.picture_coding);
    default:
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Extension ID %d not supported.\n",
               current->extension_start_code_identifier);
        return AVERROR_PATCHWELCOME;
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

    uir(5, quantiser_scale_code);

    if (nextbits(1, 1, current->slice_extension_flag)) {
        ui(1, slice_extension_flag);
        ui(1, intra_slice);
        ui(1, slice_picture_id_enable);
        ui(6, slice_picture_id);
    }

    CHECK(FUNC(extra_information)(ctx, rw, &current->extra_information_slice,
                                  "extra_information_slice[k]", "extra_bit_slice"));

    return 0;
}

static int FUNC(sequence_end)(CodedBitstreamContext *ctx, RWContext *rw,
                              MPEG2RawSequenceEnd *current)
{
    int err;

    HEADER("Sequence End");

    ui(8, sequence_end_code);

    return 0;
}
