/*
 * AV1 helper functions for muxers
 * Copyright (c) 2018 James Almer <jamrial@gmail.com>
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

#include "libavutil/mem.h"
#include "libavcodec/av1.h"
#include "libavcodec/av1_parse.h"
#include "libavcodec/profiles.h"
#include "libavcodec/put_bits.h"
#include "av1.h"
#include "avio.h"

int ff_av1_filter_obus(AVIOContext *pb, const uint8_t *buf, int size)
{
    const uint8_t *end = buf + size;
    int64_t obu_size;
    int start_pos, type, temporal_id, spatial_id;

    size = 0;
    while (buf < end) {
        int len = parse_obu_header(buf, end - buf, &obu_size, &start_pos,
                                   &type, &temporal_id, &spatial_id);
        if (len < 0)
            return len;

        switch (type) {
        case AV1_OBU_TEMPORAL_DELIMITER:
        case AV1_OBU_REDUNDANT_FRAME_HEADER:
        case AV1_OBU_TILE_LIST:
        case AV1_OBU_PADDING:
            break;
        default:
            avio_write(pb, buf, len);
            size += len;
            break;
        }
        buf += len;
    }

    return size;
}

int ff_av1_filter_obus_buf(const uint8_t *buf, uint8_t **out, int *size)
{
    AVIOContext *pb;
    int ret;

    ret = avio_open_dyn_buf(&pb);
    if (ret < 0)
        return ret;

    ret = ff_av1_filter_obus(pb, buf, *size);
    if (ret < 0)
        return ret;

    av_freep(out);
    *size = avio_close_dyn_buf(pb, out);

    return ret;
}

typedef struct AV1SequenceParameters {
    uint8_t seq_profile;
    uint8_t seq_level_idx_0;
    uint8_t seq_tier_0;
    uint8_t high_bitdepth;
    uint8_t twelve_bit;
    uint8_t monochrome;
    uint8_t chroma_subsampling_x;
    uint8_t chroma_subsampling_y;
    uint8_t chroma_sample_position;
} AV1SequenceParameters;

static inline void uvlc(GetBitContext *gb)
{
    int leading_zeros = 0;

    while (get_bits_left(gb)) {
        if (get_bits1(gb))
            break;
        leading_zeros++;
    }

    if (leading_zeros >= 32)
        return;

    skip_bits_long(gb, leading_zeros);
}

static int parse_color_config(AV1SequenceParameters *seq_params, GetBitContext *gb)
{
    int color_primaries, transfer_characteristics, matrix_coefficients;

    seq_params->high_bitdepth = get_bits1(gb);
    if (seq_params->seq_profile == FF_PROFILE_AV1_PROFESSIONAL && seq_params->high_bitdepth)
        seq_params->twelve_bit = get_bits1(gb);

    if (seq_params->seq_profile == FF_PROFILE_AV1_HIGH)
        seq_params->monochrome = 0;
    else
        seq_params->monochrome = get_bits1(gb);

    if (get_bits1(gb)) { // color_description_present_flag
        color_primaries          = get_bits(gb, 8);
        transfer_characteristics = get_bits(gb, 8);
        matrix_coefficients      = get_bits(gb, 8);
    } else {
        color_primaries          = AVCOL_PRI_UNSPECIFIED;
        transfer_characteristics = AVCOL_TRC_UNSPECIFIED;
        matrix_coefficients      = AVCOL_SPC_UNSPECIFIED;
    }

    if (seq_params->monochrome) {
        skip_bits1(gb); // color_range
        seq_params->chroma_subsampling_x = 1;
        seq_params->chroma_subsampling_y = 1;
        seq_params->chroma_sample_position = 0;
        return 0;
    } else if (color_primaries          == AVCOL_PRI_BT709 &&
               transfer_characteristics == AVCOL_TRC_IEC61966_2_1 &&
               matrix_coefficients      == AVCOL_SPC_RGB) {
        seq_params->chroma_subsampling_x = 0;
        seq_params->chroma_subsampling_y = 0;
    } else {
        skip_bits1(gb); // color_range

        if (seq_params->seq_profile == FF_PROFILE_AV1_MAIN) {
            seq_params->chroma_subsampling_x = 1;
            seq_params->chroma_subsampling_y = 1;
        } else if (seq_params->seq_profile == FF_PROFILE_AV1_HIGH) {
            seq_params->chroma_subsampling_x = 0;
            seq_params->chroma_subsampling_y = 0;
        } else {
            if (seq_params->twelve_bit) {
                seq_params->chroma_subsampling_x = get_bits1(gb);
                if (seq_params->chroma_subsampling_x)
                    seq_params->chroma_subsampling_y = get_bits1(gb);
                else
                    seq_params->chroma_subsampling_y = 0;
            } else {
                seq_params->chroma_subsampling_x = 1;
                seq_params->chroma_subsampling_y = 0;
            }
        }
        if (seq_params->chroma_subsampling_x && seq_params->chroma_subsampling_y)
            seq_params->chroma_sample_position = get_bits(gb, 2);
    }

    skip_bits1(gb); // separate_uv_delta_q

    return 0;
}

static int parse_sequence_header(AV1SequenceParameters *seq_params, const uint8_t *buf, int size)
{
    GetBitContext gb;
    int reduced_still_picture_header;
    int frame_width_bits_minus_1, frame_height_bits_minus_1;
    int size_bits, ret;

    size_bits = get_obu_bit_length(buf, size, AV1_OBU_SEQUENCE_HEADER);
    if (size_bits < 0)
        return size_bits;

    ret = init_get_bits(&gb, buf, size_bits);
    if (ret < 0)
        return ret;

    memset(seq_params, 0, sizeof(*seq_params));

    seq_params->seq_profile = get_bits(&gb, 3);

    skip_bits1(&gb); // still_picture
    reduced_still_picture_header = get_bits1(&gb);

    if (reduced_still_picture_header) {
        seq_params->seq_level_idx_0 = get_bits(&gb, 5);
        seq_params->seq_tier_0 = 0;
    } else {
        int initial_display_delay_present_flag, operating_points_cnt_minus_1;
        int decoder_model_info_present_flag, buffer_delay_length_minus_1;

        if (get_bits1(&gb)) { // timing_info_present_flag
            skip_bits_long(&gb, 32); // num_units_in_display_tick
            skip_bits_long(&gb, 32); // time_scale

            if (get_bits1(&gb)) // equal_picture_interval
                uvlc(&gb); // num_ticks_per_picture_minus_1

            decoder_model_info_present_flag = get_bits1(&gb);
            if (decoder_model_info_present_flag) {
                buffer_delay_length_minus_1 = get_bits(&gb, 5);
                skip_bits_long(&gb, 32); // num_units_in_decoding_tick
                skip_bits(&gb, 10); // buffer_removal_time_length_minus_1 (5)
                                    // frame_presentation_time_length_minus_1 (5)
            }
        } else
            decoder_model_info_present_flag = 0;

        initial_display_delay_present_flag = get_bits1(&gb);

        operating_points_cnt_minus_1 = get_bits(&gb, 5);
        for (int i = 0; i <= operating_points_cnt_minus_1; i++) {
            int seq_level_idx, seq_tier;

            skip_bits(&gb, 12); // operating_point_idc
            seq_level_idx = get_bits(&gb, 5);

            if (seq_level_idx > 7)
                seq_tier = get_bits1(&gb);
            else
                seq_tier = 0;

            if (decoder_model_info_present_flag) {
                if (get_bits1(&gb)) { // decoder_model_present_for_this_op
                    skip_bits_long(&gb, buffer_delay_length_minus_1 + 1); // decoder_buffer_delay
                    skip_bits_long(&gb, buffer_delay_length_minus_1 + 1); // encoder_buffer_delay
                    skip_bits1(&gb); // low_delay_mode_flag
                }
            }

            if (initial_display_delay_present_flag) {
                if (get_bits1(&gb)) // initial_display_delay_present_for_this_op
                    skip_bits(&gb, 4); // initial_display_delay_minus_1
            }

            if (i == 0) {
               seq_params->seq_level_idx_0 = seq_level_idx;
               seq_params->seq_tier_0 = seq_tier;
            }
        }
    }

    frame_width_bits_minus_1  = get_bits(&gb, 4);
    frame_height_bits_minus_1 = get_bits(&gb, 4);

    skip_bits(&gb, frame_width_bits_minus_1 + 1); // max_frame_width_minus_1
    skip_bits(&gb, frame_height_bits_minus_1 + 1); // max_frame_height_minus_1

    if (!reduced_still_picture_header) {
        if (get_bits1(&gb)) // frame_id_numbers_present_flag
            skip_bits(&gb, 7); // delta_frame_id_length_minus_2 (4), additional_frame_id_length_minus_1 (3)
    }

    skip_bits(&gb, 3); // use_128x128_superblock (1), enable_filter_intra (1), enable_intra_edge_filter (1)

    if (!reduced_still_picture_header) {
        int enable_order_hint, seq_force_screen_content_tools;

        skip_bits(&gb, 4); // enable_intraintra_compound (1), enable_masked_compound (1)
                           // enable_warped_motion (1), enable_dual_filter (1)

        enable_order_hint = get_bits1(&gb);
        if (enable_order_hint)
            skip_bits(&gb, 2); // enable_jnt_comp (1), enable_ref_frame_mvs (1)

        if (get_bits1(&gb)) // seq_choose_screen_content_tools
            seq_force_screen_content_tools = 2;
        else
            seq_force_screen_content_tools = get_bits1(&gb);

        if (seq_force_screen_content_tools) {
            if (!get_bits1(&gb)) // seq_choose_integer_mv
                skip_bits1(&gb); // seq_force_integer_mv
        }

        if (enable_order_hint)
            skip_bits(&gb, 3); // order_hint_bits_minus_1
    }

    skip_bits(&gb, 3); // enable_superres (1), enable_cdef (1), enable_restoration (1)

    parse_color_config(seq_params, &gb);

    skip_bits1(&gb); // film_grain_params_present

    if (get_bits_left(&gb))
        return AVERROR_INVALIDDATA;

    return 0;
}

int ff_isom_write_av1c(AVIOContext *pb, const uint8_t *buf, int size)
{
    AVIOContext *seq_pb = NULL, *meta_pb = NULL;
    AV1SequenceParameters seq_params;
    PutBitContext pbc;
    uint8_t header[4];
    uint8_t *seq = NULL, *meta = NULL;
    int64_t obu_size;
    int start_pos, type, temporal_id, spatial_id;
    int ret, nb_seq = 0, seq_size, meta_size;

    if (size <= 0)
        return AVERROR_INVALIDDATA;

    ret = avio_open_dyn_buf(&seq_pb);
    if (ret < 0)
        return ret;
    ret = avio_open_dyn_buf(&meta_pb);
    if (ret < 0)
        goto fail;

    while (size > 0) {
        int len = parse_obu_header(buf, size, &obu_size, &start_pos,
                                   &type, &temporal_id, &spatial_id);
        if (len < 0) {
            ret = len;
            goto fail;
        }

        switch (type) {
        case AV1_OBU_SEQUENCE_HEADER:
            nb_seq++;
            if (!obu_size || nb_seq > 1) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            ret = parse_sequence_header(&seq_params, buf + start_pos, obu_size);
            if (ret < 0)
                goto fail;

            avio_write(seq_pb, buf, len);
            break;
        case AV1_OBU_METADATA:
            if (!obu_size) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            avio_write(meta_pb, buf, len);
            break;
        default:
            break;
        }
        size -= len;
        buf  += len;
    }

    seq_size  = avio_close_dyn_buf(seq_pb, &seq);
    if (!seq_size) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    init_put_bits(&pbc, header, sizeof(header));

    put_bits(&pbc, 1, 1); // marker
    put_bits(&pbc, 7, 1); // version
    put_bits(&pbc, 3, seq_params.seq_profile);
    put_bits(&pbc, 5, seq_params.seq_level_idx_0);
    put_bits(&pbc, 1, seq_params.seq_tier_0);
    put_bits(&pbc, 1, seq_params.high_bitdepth);
    put_bits(&pbc, 1, seq_params.twelve_bit);
    put_bits(&pbc, 1, seq_params.monochrome);
    put_bits(&pbc, 1, seq_params.chroma_subsampling_x);
    put_bits(&pbc, 1, seq_params.chroma_subsampling_y);
    put_bits(&pbc, 2, seq_params.chroma_sample_position);
    flush_put_bits(&pbc);

    avio_write(pb, header, sizeof(header));
    avio_write(pb, seq, seq_size);

    meta_size = avio_close_dyn_buf(meta_pb, &meta);
    if (meta_size)
        avio_write(pb, meta, meta_size);

fail:
    if (!seq)
        avio_close_dyn_buf(seq_pb, &seq);
    if (!meta)
        avio_close_dyn_buf(meta_pb, &meta);
    av_free(seq);
    av_free(meta);

    return ret;
}
