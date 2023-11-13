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

static int FUNC(update_segmentation)(CodedBitstreamContext *ctx,
                                     CBSVP8BoolCodingRW *bool_coding_rw,
                                     VP8RawFrameHeader *current)
{
    bc_f(1, update_segment_map);
    bc_f(1, update_segment_feature_data);

    if (current->update_segment_feature_data) {
        bc_f(1, segment_feature_mode);
        // quantizer
        for (int i = 0; i < 4; i++) {
            bc_b(segment_qp_update[i]);
            if (current->segment_qp_update[i])
                bc_ss(7, segment_qp[i], 1, i);
        }
        // loop filter
        for (int i = 0; i < 4; i++) {
            bc_b(segment_loop_filter_level_update[i]);
            if (current->segment_loop_filter_level_update[i])
                bc_ss(6, segment_loop_filter_level[i], 1, i);
        }
    }

    if (current->update_segment_map) {
        for (int i = 0; i < 3; i++) {
            bc_b(segment_probs_update[i]);
            if (current->segment_probs_update[i])
                bc_fs(8, segment_probs[i], 1, i);
        }
    }

    return 0;
}

static int FUNC(mode_ref_lf_deltas)(CodedBitstreamContext *ctx,
                                    CBSVP8BoolCodingRW *bool_coding_rw,
                                    VP8RawFrameHeader *current)
{
    bc_f(1, mode_ref_lf_delta_enable);
    if (current->mode_ref_lf_delta_enable) {
        bc_b(mode_ref_lf_delta_update);
        if (current->mode_ref_lf_delta_update) {
            // ref_lf_deltas
            for (int i = 0; i < 4; i++) {
                bc_b(ref_lf_deltas_update[i]);
                if (current->ref_lf_deltas_update[i])
                    bc_ss(6, ref_lf_deltas[i], 1, i);
            }
            // mode_lf_deltas
            for (int i = 0; i < 4; i++) {
                bc_b(mode_lf_deltas_update[i]);
                if (current->mode_lf_deltas_update[i])
                    bc_ss(6, mode_lf_deltas[i], 1, i);
            }
        }
    }

    return 0;
}

static int FUNC(quantization_params)(CodedBitstreamContext *ctx,
                                     CBSVP8BoolCodingRW *bool_coding_rw,
                                     VP8RawFrameHeader *current)
{
    bc_f(7, base_qindex);

    bc_b(y1dc_delta_q_present);
    if (current->y1dc_delta_q_present)
        bc_s(4, y1dc_delta_q);

    bc_b(y2dc_delta_q_present);
    if (current->y2dc_delta_q_present)
        bc_s(4, y2dc_delta_q);

    bc_b(y2ac_delta_q_present);
    if (current->y2ac_delta_q_present)
        bc_s(4, y2ac_delta_q);

    bc_b(uvdc_delta_q_present);
    if (current->uvdc_delta_q_present)
        bc_s(4, uvdc_delta_q);

    bc_b(uvac_delta_q_present);
    if (current->uvac_delta_q_present)
        bc_s(4, uvac_delta_q);

    return 0;
}

static int FUNC(update_token_probs)(CodedBitstreamContext *ctx,
                                    CBSVP8BoolCodingRW *bool_coding_rw,
                                    VP8RawFrameHeader *current)
{
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 8; ++j) {
            for (int k = 0; k < 3; ++k) {
                for (int l = 0; l < 11; ++l) {
                    bc_b_prob(ff_vp8_token_update_probs[i][j][k][l],
                              coeff_prob_update[i][j][k][l]);
                    if (current->coeff_prob_update[i][j][k][l])
                        bc_fs(8, coeff_prob[i][j][k][l], 4, i, j, k, l);
                }
            }
        }
    }

    return 0;
}

static int FUNC(update_mv_probs)(CodedBitstreamContext *ctx,
                                 CBSVP8BoolCodingRW *bool_coding_rw,
                                 VP8RawFrameHeader *current)
{
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 19; ++j) {
            bc_b(mv_prob_update[i][j]);
            if (current->mv_prob_update[i][j])
                bc_fs(7, mv_prob[i][j], 2, i, j);
        }
    }

    return 0;
}

static int FUNC(frame_tag)(CodedBitstreamContext *ctx, RWContext *rw,
                           VP8RawFrameHeader *current)
{
    f(1, frame_type);
    f(3, profile);
    f(1, show_frame);
    f(19, first_partition_length_in_bytes);

    if (current->frame_type == VP8_KEY_FRAME) {
        fixed(8, start_code_0, VP8_START_CODE_0);
        fixed(8, start_code_1, VP8_START_CODE_1);
        fixed(8, start_code_2, VP8_START_CODE_2);

        f(14, width);
        f(2, horizontal_scale);
        f(14, height);
        f(2, vertical_scale);
    }

    return 0;
}

static int FUNC(frame_header)(CodedBitstreamContext *ctx,
                              CBSVP8BoolCodingRW *bool_coding_rw,
                              VP8RawFrameHeader *current)
{
    if (current->frame_type == VP8_KEY_FRAME) {
        bc_f(1, color_space);
        bc_f(1, clamping_type);
    }

    bc_f(1, segmentation_enable);
    if (current->segmentation_enable)
        CHECK(FUNC(update_segmentation)(ctx, bool_coding_rw, current));

    bc_f(1, loop_filter_type);
    bc_f(6, loop_filter_level);
    bc_f(3, loop_filter_sharpness);

    CHECK(FUNC(mode_ref_lf_deltas)(ctx, bool_coding_rw, current));

    bc_f(2, log2_token_partitions);

    CHECK(FUNC(quantization_params)(ctx, bool_coding_rw, current));

    if (current->frame_type != VP8_KEY_FRAME) {
        bc_f(1, refresh_golden_frame);
        bc_f(1, refresh_alternate_frame);
        if (!current->refresh_golden_frame)
            bc_f(2, copy_buffer_to_golden);
        if (!current->refresh_alternate_frame)
            bc_f(2, copy_buffer_to_alternate);
        bc_f(1, ref_frame_sign_bias_golden);
        bc_f(1, ref_frame_sign_bias_alternate);
    }
    bc_f(1, refresh_entropy_probs);
    if (current->frame_type != VP8_KEY_FRAME)
        bc_f(1, refresh_last_frame);

    CHECK(FUNC(update_token_probs)(ctx, bool_coding_rw, current));

    bc_f(1, mb_no_skip_coeff);
    if (current->mb_no_skip_coeff)
        bc_f(8, prob_skip_false);

    if (current->frame_type != VP8_KEY_FRAME) {
        bc_f(8, prob_intra);
        bc_f(8, prob_last);
        bc_f(8, prob_golden);

        // intra_16x16_prob
        bc_b(intra_16x16_prob_update);
        if (current->intra_16x16_prob_update)
            for (int i = 0; i < 4; i++)
                bc_fs(8, intra_16x16_prob[i], 1, i);

        // intra_chroma_prob
        bc_b(intra_chrome_prob_update);
        if (current->intra_chrome_prob_update)
            for (int i = 0; i < 3; i++)
                bc_fs(8, intra_chrome_prob[i], 1, i);

        CHECK(FUNC(update_mv_probs)(ctx, bool_coding_rw, current));
    }

    return 0;
}

static int FUNC(uncompressed_header)(CodedBitstreamContext *ctx, RWContext *rw,
                                     VP8RawFrame *current)
{
    HEADER("Frame");

    CHECK(FUNC(frame_tag)(ctx, rw, &current->header));

    return 0;
}

static int FUNC(compressed_header)(CodedBitstreamContext *ctx,
                                   CBSVP8BoolCodingRW *bool_coding_rw,
                                   VP8RawFrame *current)
{
    CHECK(FUNC(frame_header)(ctx, bool_coding_rw, &current->header));

    return 0;
}
