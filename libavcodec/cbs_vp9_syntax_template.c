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

static int FUNC(frame_sync_code)(CodedBitstreamContext *ctx, RWContext *rw,
                                 VP9RawFrameHeader *current)
{
    uint8_t frame_sync_byte_0 = VP9_FRAME_SYNC_0;
    uint8_t frame_sync_byte_1 = VP9_FRAME_SYNC_1;
    uint8_t frame_sync_byte_2 = VP9_FRAME_SYNC_2;
    int err;

    xf(8, frame_sync_byte_0, frame_sync_byte_0, 0);
    xf(8, frame_sync_byte_1, frame_sync_byte_1, 0);
    xf(8, frame_sync_byte_2, frame_sync_byte_2, 0);

    if (frame_sync_byte_0 != VP9_FRAME_SYNC_0 ||
        frame_sync_byte_1 != VP9_FRAME_SYNC_1 ||
        frame_sync_byte_2 != VP9_FRAME_SYNC_2) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid frame sync code: "
               "%02x %02x %02x.\n", frame_sync_byte_0,
               frame_sync_byte_1, frame_sync_byte_2);
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int FUNC(color_config)(CodedBitstreamContext *ctx, RWContext *rw,
                              VP9RawFrameHeader *current, int profile)
{
    int err;

    if (profile >= 2)
        f(1, ten_or_twelve_bit);

    f(3, color_space);

    if (current->color_space != VP9_CS_RGB) {
        f(1, color_range);
        if (profile == 1 || profile == 3) {
            f(1, subsampling_x);
            f(1, subsampling_y);
            f(1, color_config_reserved_zero);
        } else {
            infer(subsampling_x, 1);
            infer(subsampling_y, 1);
        }
    } else {
        infer(color_range, 1);
        if (profile == 1 || profile == 3) {
            infer(subsampling_x, 0);
            infer(subsampling_y, 0);
        }
    }

    return 0;
}

static int FUNC(frame_size)(CodedBitstreamContext *ctx, RWContext *rw,
                            VP9RawFrameHeader *current)
{
    CodedBitstreamVP9Context *vp9 = ctx->priv_data;
    int err;

    f(16, frame_width_minus_1);
    f(16, frame_height_minus_1);

    vp9->mi_cols = (current->frame_width_minus_1  + 8) >> 3;
    vp9->mi_rows = (current->frame_height_minus_1 + 8) >> 3;
    vp9->sb64_cols = (vp9->mi_cols + 7) >> 3;
    vp9->sb64_rows = (vp9->mi_rows + 7) >> 3;

    return 0;
}

static int FUNC(render_size)(CodedBitstreamContext *ctx, RWContext *rw,
                             VP9RawFrameHeader *current)
{
    int err;

    f(1, render_and_frame_size_different);

    if (current->render_and_frame_size_different) {
        f(16, render_width_minus_1);
        f(16, render_height_minus_1);
    }

    return 0;
}

static int FUNC(frame_size_with_refs)(CodedBitstreamContext *ctx, RWContext *rw,
                                      VP9RawFrameHeader *current)
{
    int err, i;

    for (i = 0; i < VP9_REFS_PER_FRAME; i++) {
        fs(1, found_ref[i], 1, i);
        if (current->found_ref[i])
            break;
    }
    if (i >= VP9_REFS_PER_FRAME)
        CHECK(FUNC(frame_size)(ctx, rw, current));
    CHECK(FUNC(render_size)(ctx, rw, current));

    return 0;
}

static int FUNC(interpolation_filter)(CodedBitstreamContext *ctx, RWContext *rw,
                                      VP9RawFrameHeader *current)
{
    int err;

    f(1, is_filter_switchable);
    if (!current->is_filter_switchable)
        f(2, raw_interpolation_filter_type);

    return 0;
}

static int FUNC(loop_filter_params)(CodedBitstreamContext *ctx, RWContext *rw,
                                    VP9RawFrameHeader *current)
{
    int err, i;

    f(6, loop_filter_level);
    f(3, loop_filter_sharpness);

    f(1, loop_filter_delta_enabled);
    if (current->loop_filter_delta_enabled) {
        f(1, loop_filter_delta_update);
        if (current->loop_filter_delta_update) {
            for (i = 0; i < VP9_MAX_REF_FRAMES; i++) {
                fs(1, update_ref_delta[i], 1, i);
                if (current->update_ref_delta[i])
                    ss(6, loop_filter_ref_deltas[i], 1, i);
            }
            for (i = 0; i < 2; i++) {
                fs(1, update_mode_delta[i], 1, i);
                if (current->update_mode_delta[i])
                    ss(6, loop_filter_mode_deltas[i], 1, i);
            }
        }
    }

    return 0;
}

static int FUNC(quantization_params)(CodedBitstreamContext *ctx, RWContext *rw,
                                     VP9RawFrameHeader *current)
{
    int err;

    f(8, base_q_idx);

    delta_q(delta_q_y_dc);
    delta_q(delta_q_uv_dc);
    delta_q(delta_q_uv_ac);

    return 0;
}

static int FUNC(segmentation_params)(CodedBitstreamContext *ctx, RWContext *rw,
                                     VP9RawFrameHeader *current)
{
    static const int segmentation_feature_bits[VP9_SEG_LVL_MAX]   = { 8, 6, 2, 0 };
    static const int segmentation_feature_signed[VP9_SEG_LVL_MAX] = { 1, 1, 0, 0 };

    int err, i, j;

    f(1, segmentation_enabled);

    if (current->segmentation_enabled) {
        f(1, segmentation_update_map);
        if (current->segmentation_update_map) {
            for (i = 0; i < 7; i++)
                prob(segmentation_tree_probs[i], 1, i);
            f(1, segmentation_temporal_update);
            for (i = 0; i < 3; i++) {
                if (current->segmentation_temporal_update)
                    prob(segmentation_pred_prob[i], 1, i);
                else
                    infer(segmentation_pred_prob[i], 255);
            }
        }

        f(1, segmentation_update_data);
        if (current->segmentation_update_data) {
            f(1, segmentation_abs_or_delta_update);
            for (i = 0; i < VP9_MAX_SEGMENTS; i++) {
                for (j = 0; j < VP9_SEG_LVL_MAX; j++) {
                    fs(1, feature_enabled[i][j], 2, i, j);
                    if (current->feature_enabled[i][j] &&
                        segmentation_feature_bits[j]) {
                        fs(segmentation_feature_bits[j],
                           feature_value[i][j], 2, i, j);
                        if (segmentation_feature_signed[j])
                            fs(1, feature_sign[i][j], 2, i, j);
                        else
                            infer(feature_sign[i][j], 0);
                    } else {
                        infer(feature_value[i][j], 0);
                        infer(feature_sign[i][j],  0);
                    }
                }
            }
        }
    }

    return 0;
}

static int FUNC(tile_info)(CodedBitstreamContext *ctx, RWContext *rw,
                           VP9RawFrameHeader *current)
{
    CodedBitstreamVP9Context *vp9 = ctx->priv_data;
    int min_log2_tile_cols, max_log2_tile_cols;
    int err;

    min_log2_tile_cols = 0;
    while ((VP9_MAX_TILE_WIDTH_B64 << min_log2_tile_cols) < vp9->sb64_cols)
        ++min_log2_tile_cols;
    max_log2_tile_cols = 0;
    while ((vp9->sb64_cols >> (max_log2_tile_cols + 1)) >= VP9_MIN_TILE_WIDTH_B64)
        ++max_log2_tile_cols;

    increment(tile_cols_log2, min_log2_tile_cols, max_log2_tile_cols);

    increment(tile_rows_log2, 0, 2);

    return 0;
}

static int FUNC(uncompressed_header)(CodedBitstreamContext *ctx, RWContext *rw,
                                     VP9RawFrameHeader *current)
{
    int profile, i;
    int err;

    f(2, frame_marker);

    f(1, profile_low_bit);
    f(1, profile_high_bit);
    profile = (current->profile_high_bit << 1) + current->profile_low_bit;
    if (profile == 3)
        f(1, profile_reserved_zero);

    f(1, show_existing_frame);
    if (current->show_existing_frame) {
        f(3, frame_to_show_map_idx);
        infer(header_size_in_bytes, 0);
        infer(refresh_frame_flags,  0x00);
        infer(loop_filter_level,    0);
        return 0;
    }

    f(1, frame_type);
    f(1, show_frame);
    f(1, error_resilient_mode);

    if (current->frame_type == VP9_KEY_FRAME) {
        CHECK(FUNC(frame_sync_code)(ctx, rw, current));
        CHECK(FUNC(color_config)(ctx, rw, current, profile));
        CHECK(FUNC(frame_size)(ctx, rw, current));
        CHECK(FUNC(render_size)(ctx, rw, current));

        infer(refresh_frame_flags, 0xff);

    } else {
         if (current->show_frame == 0)
             f(1, intra_only);
         else
             infer(intra_only, 0);

         if (current->error_resilient_mode == 0)
             f(2, reset_frame_context);
         else
             infer(reset_frame_context, 0);

         if (current->intra_only == 1) {
             CHECK(FUNC(frame_sync_code)(ctx, rw, current));

             if (profile > 0) {
                 CHECK(FUNC(color_config)(ctx, rw, current, profile));
             } else {
                 infer(color_space,   1);
                 infer(subsampling_x, 1);
                 infer(subsampling_y, 1);
             }

             f(8, refresh_frame_flags);

             CHECK(FUNC(frame_size)(ctx, rw, current));
             CHECK(FUNC(render_size)(ctx, rw, current));
         } else {
             f(8, refresh_frame_flags);

             for (i = 0; i < VP9_REFS_PER_FRAME; i++) {
                 fs(3, ref_frame_idx[i], 1, i);
                 fs(1, ref_frame_sign_bias[VP9_LAST_FRAME + i],
                    1, VP9_LAST_FRAME + i);
             }

             CHECK(FUNC(frame_size_with_refs)(ctx, rw, current));
             f(1, allow_high_precision_mv);
             CHECK(FUNC(interpolation_filter)(ctx, rw, current));
         }
    }

    if (current->error_resilient_mode == 0) {
        f(1, refresh_frame_context);
        f(1, frame_parallel_decoding_mode);
    } else {
        infer(refresh_frame_context,        0);
        infer(frame_parallel_decoding_mode, 1);
    }

    f(2, frame_context_idx);

    CHECK(FUNC(loop_filter_params)(ctx, rw, current));
    CHECK(FUNC(quantization_params)(ctx, rw, current));
    CHECK(FUNC(segmentation_params)(ctx, rw, current));
    CHECK(FUNC(tile_info)(ctx, rw, current));

    f(16, header_size_in_bytes);

    return 0;
}

static int FUNC(trailing_bits)(CodedBitstreamContext *ctx, RWContext *rw)
{
    int err;
    av_unused int zero = 0;
    while (byte_alignment(rw) != 0)
        xf(1, zero_bit, zero, 0);

    return 0;
}

static int FUNC(frame)(CodedBitstreamContext *ctx, RWContext *rw,
                       VP9RawFrame *current)
{
    int err;

    HEADER("Frame");

    CHECK(FUNC(uncompressed_header)(ctx, rw, &current->header));

    CHECK(FUNC(trailing_bits)(ctx, rw));

    return 0;
}

static int FUNC(superframe_index)(CodedBitstreamContext *ctx, RWContext *rw,
                                  VP9RawSuperframeIndex *current)
{
    int err, i;

    HEADER("Superframe Index");

    f(3, superframe_marker);
    f(2, bytes_per_framesize_minus_1);
    f(3, frames_in_superframe_minus_1);

    for (i = 0; i <= current->frames_in_superframe_minus_1; i++) {
        // Surprise little-endian!
        fle(8 * (current->bytes_per_framesize_minus_1 + 1),
            frame_sizes[i], 1, i);
    }

    f(3, superframe_marker);
    f(2, bytes_per_framesize_minus_1);
    f(3, frames_in_superframe_minus_1);

    return 0;
}
