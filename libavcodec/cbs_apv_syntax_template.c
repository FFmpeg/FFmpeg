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

static int FUNC(pbu_header)(CodedBitstreamContext *ctx, RWContext *rw,
                            APVRawPBUHeader *current)
{
    int err;

    ub(8,  pbu_type);
    ub(16, group_id);
    u(8, reserved_zero_8bits, 0, 0);

    return 0;
}

static int FUNC(byte_alignment)(CodedBitstreamContext *ctx, RWContext *rw)
{
    int err;

    while (byte_alignment(rw) != 0)
        fixed(1, alignment_bit_equal_to_zero, 0);

    return 0;
}

static int FUNC(filler)(CodedBitstreamContext *ctx, RWContext *rw,
                        APVRawFiller *current)
{
    int err;

#ifdef READ
    current->filler_size = 0;
    while (show_bits(rw, 8) == 0xff) {
        fixed(8, ff_byte, 0xff);
        ++current->filler_size;
    }
#else
    {
        uint32_t i;
        for (i = 0; i < current->filler_size; i++)
            fixed(8, ff_byte, 0xff);
    }
#endif

    return 0;
}

static int FUNC(frame_info)(CodedBitstreamContext *ctx, RWContext *rw,
                            APVRawFrameInfo *current)
{
    int err;

    ub(8,  profile_idc);
    ub(8,  level_idc);
    ub(3,  band_idc);

    u(5, reserved_zero_5bits, 0, 0);

    ub(24, frame_width);
    ub(24, frame_height);

    u(4, chroma_format_idc, 0, 4);
    if (current->chroma_format_idc == 1) {
        av_log(ctx->log_ctx, AV_LOG_ERROR,
               "chroma_format_idc 1 for 4:2:0 is not allowed in APV.\n");
        return AVERROR_INVALIDDATA;
    }

    u(4, bit_depth_minus8, 2, 8);

    ub(8, capture_time_distance);

    u(8, reserved_zero_8bits, 0, 0);

    return 0;
}

static int FUNC(quantization_matrix)(CodedBitstreamContext *ctx,
                                     RWContext *rw,
                                     APVRawQuantizationMatrix *current)
{
    const CodedBitstreamAPVContext *priv = ctx->priv_data;
    int err;

    for (int c = 0; c < priv->num_comp; c++) {
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8 ; x++) {
                us(8, q_matrix[c][x][y], 1, 255, 3, c, x, y);
            }
        }
    }

    return 0;
}

static int FUNC(tile_info)(CodedBitstreamContext *ctx, RWContext *rw,
                           APVRawTileInfo *current,
                           const APVRawFrameHeader *fh)
{
    CodedBitstreamAPVContext *priv = ctx->priv_data;
    int frame_width_in_mbs   = (fh->frame_info.frame_width  + 15) / 16;
    int frame_height_in_mbs  = (fh->frame_info.frame_height + 15) / 16;
    uint32_t min_tile_width  = FFMAX(APV_MIN_TILE_WIDTH_IN_MBS,
                                     (frame_width_in_mbs + APV_MAX_TILE_COLS - 1) /
                                     APV_MAX_TILE_COLS);
    uint32_t min_tile_height = FFMAX(APV_MIN_TILE_HEIGHT_IN_MBS,
                                     (frame_height_in_mbs + APV_MAX_TILE_ROWS - 1) /
                                     APV_MAX_TILE_ROWS);
    int err;

    u(20, tile_width_in_mbs,  min_tile_width,  MAX_UINT_BITS(20));
    u(20, tile_height_in_mbs, min_tile_height, MAX_UINT_BITS(20));

    ub(1, tile_size_present_in_fh_flag);

    cbs_apv_derive_tile_info(&priv->tile_info, fh);

    if (current->tile_size_present_in_fh_flag) {
        for (int t = 0; t < priv->tile_info.num_tiles; t++) {
            us(32, tile_size_in_fh[t], 10, MAX_UINT_BITS(32), 1, t);
        }
    }

    return 0;
}

static int FUNC(frame_header)(CodedBitstreamContext *ctx, RWContext *rw,
                              APVRawFrameHeader *current)
{
    CodedBitstreamAPVContext *priv = ctx->priv_data;
    int err;

    CHECK(FUNC(frame_info)(ctx, rw, &current->frame_info));

    u(8, reserved_zero_8bits, 0, 0);

    ub(1, color_description_present_flag);
    if (current->color_description_present_flag) {
        ub(8, color_primaries);
        ub(8, transfer_characteristics);
        ub(8, matrix_coefficients);
        ub(1, full_range_flag);
    } else {
        infer(color_primaries,          2);
        infer(transfer_characteristics, 2);
        infer(matrix_coefficients,      2);
        infer(full_range_flag,          0);
    }

    priv->bit_depth = current->frame_info.bit_depth_minus8 + 8;
    priv->num_comp  = cbs_apv_get_num_comp(current);

    ub(1, use_q_matrix);
    if (current->use_q_matrix) {
        CHECK(FUNC(quantization_matrix)(ctx, rw,
                                        &current->quantization_matrix));
    } else {
        for (int c = 0; c < priv->num_comp; c++) {
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8 ; x++) {
                    infer(quantization_matrix.q_matrix[c][y][x], 16);
                }
            }
        }
    }

    CHECK(FUNC(tile_info)(ctx, rw, &current->tile_info, current));

    u(8, reserved_zero_8bits_2, 0, 0);

    CHECK(FUNC(byte_alignment)(ctx, rw));

    return 0;
}

static int FUNC(tile_header)(CodedBitstreamContext *ctx, RWContext *rw,
                             APVRawTileHeader *current,
                             int tile_idx, uint32_t tile_size)
{
    const CodedBitstreamAPVContext *priv = ctx->priv_data;
    uint16_t expected_tile_header_size;
    uint32_t tile_size_remaining;
    uint8_t max_qp;
    int err;

    expected_tile_header_size = 4 + priv->num_comp * (4 + 1) + 1;

    u(16, tile_header_size,
      expected_tile_header_size, expected_tile_header_size);

    u(16, tile_index, tile_idx, tile_idx);

    tile_size_remaining = tile_size - current->tile_header_size;
    for (int c = 0; c < priv->num_comp; c++) {
        us(32, tile_data_size[c], 1, tile_size_remaining, 1, c);
        tile_size_remaining -= current->tile_data_size[c];
    }

    max_qp = 3 + priv->bit_depth * 6;
    for (int c = 0; c < priv->num_comp; c++) {
        us(8, tile_qp[c], 0, max_qp, 1, c);
    }

    u(8, reserved_zero_8bits, 0, 0);

    return 0;
}

static int FUNC(tile)(CodedBitstreamContext *ctx, RWContext *rw,
                      APVRawTile *current,
                      int tile_idx, uint32_t tile_size)
{
    const CodedBitstreamAPVContext *priv = ctx->priv_data;
    int err;

    CHECK(FUNC(tile_header)(ctx, rw, &current->tile_header,
                            tile_idx, tile_size));

    for (int c = 0; c < priv->num_comp; c++) {
        uint32_t comp_size = current->tile_header.tile_data_size[c];
#ifdef READ
        int pos = get_bits_count(rw);
        av_assert0(pos % 8 == 0);
        current->tile_data[c] = (uint8_t*)align_get_bits(rw);
        skip_bits_long(rw, 8 * comp_size);
#else
        if (put_bytes_left(rw, 0) < comp_size)
            return AVERROR(ENOSPC);
        ff_copy_bits(rw, current->tile_data[c], comp_size * 8);
#endif
    }

    return 0;
}

static int FUNC(frame)(CodedBitstreamContext *ctx, RWContext *rw,
                       APVRawFrame *current)
{
    const CodedBitstreamAPVContext *priv = ctx->priv_data;
    int err;

    HEADER("Frame");

    CHECK(FUNC(pbu_header)(ctx, rw, &current->pbu_header));

    CHECK(FUNC(frame_header)(ctx, rw, &current->frame_header));

    for (int t = 0; t < priv->tile_info.num_tiles; t++) {
        us(32, tile_size[t], 10, MAX_UINT_BITS(32), 1, t);

        CHECK(FUNC(tile)(ctx, rw, &current->tile[t],
                         t, current->tile_size[t]));
    }

    CHECK(FUNC(filler)(ctx, rw, &current->filler));

    return 0;
}

static int FUNC(au_info)(CodedBitstreamContext *ctx, RWContext *rw,
                         APVRawAUInfo *current)
{
    int err;

    HEADER("Access Unit Information");

    u(16, num_frames, 1, CBS_APV_MAX_AU_FRAMES);

    for (int i = 0; i < current->num_frames; i++) {
        ubs(8, pbu_type[i], 1, i);
        ubs(8, group_id[i], 1, i);

        us(8, reserved_zero_8bits[i], 0, 0, 1, i);

        CHECK(FUNC(frame_info)(ctx, rw, &current->frame_info[i]));
    }

    u(8, reserved_zero_8bits_2, 0, 0);

    return 0;
}

static int FUNC(metadata_itu_t_t35)(CodedBitstreamContext *ctx,
                                    RWContext *rw,
                                    APVRawMetadataITUTT35 *current,
                                    size_t payload_size)
{
    int err;
    size_t read_size = payload_size - 1;

    HEADER("ITU-T T.35 Metadata");

    ub(8, itu_t_t35_country_code);

    if (current->itu_t_t35_country_code == 0xff) {
        ub(8, itu_t_t35_country_code_extension);
        --read_size;
    }

#ifdef READ
    current->data_size = read_size;
    current->data_ref  = av_buffer_alloc(current->data_size);
    if (!current->data_ref)
        return AVERROR(ENOMEM);
    current->data = current->data_ref->data;
#else
    if (current->data_size != read_size) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Write size mismatch: "
               "payload %zu but expecting %zu\n",
               current->data_size, read_size);
        return AVERROR(EINVAL);
    }
#endif

    for (size_t i = 0; i < current->data_size; i++) {
        xu(8, itu_t_t35_payload[i],
           current->data[i], 0x00, 0xff, 1, i);
    }

    return 0;
}

static int FUNC(metadata_mdcv)(CodedBitstreamContext *ctx,
                               RWContext *rw,
                               APVRawMetadataMDCV *current)
{
    int err, i;

    HEADER("MDCV Metadata");

    for (i = 0; i < 3; i++) {
        ubs(16, primary_chromaticity_x[i], 1, i);
        ubs(16, primary_chromaticity_y[i], 1, i);
    }

    ub(16, white_point_chromaticity_x);
    ub(16, white_point_chromaticity_y);

    ub(32, max_mastering_luminance);
    ub(32, min_mastering_luminance);

    return 0;
}

static int FUNC(metadata_cll)(CodedBitstreamContext *ctx,
                              RWContext *rw,
                              APVRawMetadataCLL *current)
{
    int err;

    HEADER("CLL Metadata");

    ub(16, max_cll);
    ub(16, max_fall);

    return 0;
}

static int FUNC(metadata_filler)(CodedBitstreamContext *ctx,
                                 RWContext *rw,
                                 APVRawMetadataFiller *current,
                                 size_t payload_size)
{
    int err;

    HEADER("Filler Metadata");

    for (size_t i = 0; i < payload_size; i++)
        fixed(8, ff_byte, 0xff);

    return 0;
}

static int FUNC(metadata_user_defined)(CodedBitstreamContext *ctx,
                                       RWContext *rw,
                                       APVRawMetadataUserDefined *current,
                                       size_t payload_size)
{
    int err;

    HEADER("User-Defined Metadata");

    for (int i = 0; i < 16; i++)
        ubs(8, uuid[i], 1, i);

#ifdef READ
    current->data_size = payload_size - 16;
    current->data_ref  = av_buffer_alloc(current->data_size);
    if (!current->data_ref)
        return AVERROR(ENOMEM);
    current->data = current->data_ref->data;
#else
    if (current->data_size != payload_size - 16) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Write size mismatch: "
               "payload %zu but expecting %zu\n",
               current->data_size, payload_size - 16);
        return AVERROR(EINVAL);
    }
#endif

    for (size_t i = 0; i < current->data_size; i++) {
        xu(8, user_defined_data_payload[i],
           current->data[i], 0x00, 0xff, 1, i);
    }

    return 0;
}

static int FUNC(metadata_undefined)(CodedBitstreamContext *ctx,
                                    RWContext *rw,
                                    APVRawMetadataUndefined *current,
                                    size_t payload_size)
{
    int err;

    HEADER("Undefined Metadata");

#ifdef READ
    current->data_size = payload_size;
    current->data_ref  = av_buffer_alloc(current->data_size);
    if (!current->data_ref)
        return AVERROR(ENOMEM);
    current->data = current->data_ref->data;
#else
    if (current->data_size != payload_size) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Write size mismatch: "
               "payload %zu but expecting %zu\n",
               current->data_size, payload_size - 16);
        return AVERROR(EINVAL);
    }
#endif

    for (size_t i = 0; i < current->data_size; i++) {
        xu(8, undefined_metadata_payload_byte[i],
           current->data[i], 0x00, 0xff, 1, i);
    }

    return 0;
}

static int FUNC(metadata_payload)(CodedBitstreamContext *ctx,
                                  RWContext *rw,
                                  APVRawMetadataPayload *current)
{
    int err;

    switch (current->payload_type) {
    case APV_METADATA_ITU_T_T35:
        CHECK(FUNC(metadata_itu_t_t35)(ctx, rw,
                                       &current->itu_t_t35,
                                       current->payload_size));
        break;
    case APV_METADATA_MDCV:
        CHECK(FUNC(metadata_mdcv)(ctx, rw, &current->mdcv));
        break;
    case APV_METADATA_CLL:
        CHECK(FUNC(metadata_cll)(ctx, rw, &current->cll));
        break;
    case APV_METADATA_FILLER:
        CHECK(FUNC(metadata_filler)(ctx, rw,
                                    &current->filler,
                                    current->payload_size));
        break;
    case APV_METADATA_USER_DEFINED:
        CHECK(FUNC(metadata_user_defined)(ctx, rw,
                                          &current->user_defined,
                                          current->payload_size));
        break;
    default:
        CHECK(FUNC(metadata_undefined)(ctx, rw,
                                       &current->undefined,
                                       current->payload_size));
    }

    return 0;
}

static int FUNC(metadata)(CodedBitstreamContext *ctx, RWContext *rw,
                          APVRawMetadata *current)
{
    int err;

#ifdef READ
    uint32_t metadata_bytes_left;
#else
    PutBitContext metadata_start_state;
    uint32_t metadata_start_position;
    int trace;
#endif

    HEADER("Metadata");

    CHECK(FUNC(pbu_header)(ctx, rw, &current->pbu_header));

#ifdef READ
    ub(32, metadata_size);

    metadata_bytes_left = current->metadata_size;

    for (int p = 0; p < CBS_APV_MAX_METADATA_PAYLOADS; p++) {
        APVRawMetadataPayload *pl = &current->payloads[p];
        uint32_t tmp;

        pl->payload_type = 0;
        while (show_bits(rw, 8) == 0xff) {
            fixed(8, ff_byte, 0xff);
            pl->payload_type += 255;
            --metadata_bytes_left;
        }
        xu(8, metadata_payload_type, tmp, 0, 254, 0);
        pl->payload_type += tmp;
        --metadata_bytes_left;

        pl->payload_size = 0;
        while (show_bits(rw, 8) == 0xff) {
            fixed(8, ff_byte, 0xff);
            pl->payload_size += 255;
            --metadata_bytes_left;
        }
        xu(8, metadata_payload_size, tmp, 0, 254, 0);
        pl->payload_size += tmp;
        --metadata_bytes_left;

        if (pl->payload_size > metadata_bytes_left) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid metadata: "
                   "payload_size larger than remaining metadata size "
                   "(%"PRIu32" bytes).\n", pl->payload_size);
            return AVERROR_INVALIDDATA;
        }

        current->metadata_count = p + 1;

        CHECK(FUNC(metadata_payload)(ctx, rw, pl));

        metadata_bytes_left -= pl->payload_size;
        if (metadata_bytes_left == 0)
            break;
    }
#else
    // Two passes: the first write finds the size (with tracing
    // disabled), the second write does the real write.

    metadata_start_state = *rw;
    metadata_start_position = put_bits_count(rw);

    trace = ctx->trace_enable;
    ctx->trace_enable = 0;

    for (int pass = 1; pass <= 2; pass++) {
        *rw = metadata_start_state;

        ub(32, metadata_size);

        for (int p = 0; p < current->metadata_count; p++) {
            APVRawMetadataPayload *pl = &current->payloads[p];
            uint32_t payload_start_position;
            uint32_t tmp;

            tmp = pl->payload_type;
            while (tmp >= 255) {
                fixed(8, ff_byte, 0xff);
                tmp -= 255;
            }
            xu(8, metadata_payload_type, tmp, 0, 254, 0);

            tmp = pl->payload_size;
            while (tmp >= 255) {
                fixed(8, ff_byte, 0xff);
                tmp -= 255;
            }
            xu(8, metadata_payload_size, tmp, 0, 254, 0);

            payload_start_position = put_bits_count(rw);

            err = FUNC(metadata_payload)(ctx, rw, pl);
            ctx->trace_enable = trace;
            if (err < 0)
                return err;

            if (pass == 1) {
                pl->payload_size = (put_bits_count(rw) -
                                    payload_start_position) / 8;
            }
        }

        if (pass == 1) {
            current->metadata_size = (put_bits_count(rw) -
                                      metadata_start_position) / 8 - 4;
            ctx->trace_enable = trace;
        }
    }
#endif

    CHECK(FUNC(filler)(ctx, rw, &current->filler));

    return 0;
}
