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

#include "libavutil/avassert.h"
#include "libavutil/mem.h"
#include "libavcodec/av1.h"
#include "libavcodec/av1_parse.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/put_bits.h"
#include "av1.h"
#include "avio.h"
#include "avio_internal.h"

static int av1_filter_obus(AVIOContext *pb, const uint8_t *buf,
                           int size, int *offset)
{
    const uint8_t *start = buf, *end = buf + size;
    int off;
    enum {
        START_NOT_FOUND,
        START_FOUND,
        END_FOUND,
        OFFSET_IMPOSSIBLE,
    } state = START_NOT_FOUND;

    off = size = 0;
    while (buf < end) {
        int64_t obu_size;
        int start_pos, type, temporal_id, spatial_id;
        int len = parse_obu_header(buf, end - buf, &obu_size, &start_pos,
                                   &type, &temporal_id, &spatial_id);
        if (len < 0)
            return len;

        switch (type) {
        case AV1_OBU_TEMPORAL_DELIMITER:
        case AV1_OBU_REDUNDANT_FRAME_HEADER:
        case AV1_OBU_TILE_LIST:
        case AV1_OBU_PADDING:
            if (state == START_FOUND)
                state = END_FOUND;
            break;
        default:
            if (state == START_NOT_FOUND) {
                off   = buf - start;
                state = START_FOUND;
            } else if (state == END_FOUND) {
                state = OFFSET_IMPOSSIBLE;
            }
            if (pb)
                avio_write(pb, buf, len);
            size += len;
            break;
        }
        buf += len;
    }

    if (offset)
        *offset = state != OFFSET_IMPOSSIBLE ? off : -1;

    return size;
}

int ff_av1_filter_obus(AVIOContext *pb, const uint8_t *buf, int size)
{
    return av1_filter_obus(pb, buf, size, NULL);
}

int ff_av1_filter_obus_buf(const uint8_t *in, uint8_t **out,
                           int *size, int *offset)
{
    FFIOContext pb;
    uint8_t *buf;
    int len, off, ret;

    len = ret = av1_filter_obus(NULL, in, *size, &off);
    if (ret < 0) {
        return ret;
    }
    if (off >= 0) {
        *out    = (uint8_t *)in;
        *size   = len;
        *offset = off;

        return 0;
    }

    buf = av_malloc(len + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!buf)
        return AVERROR(ENOMEM);

    ffio_init_context(&pb, buf, len, 1, NULL, NULL, NULL, NULL);

    ret = av1_filter_obus(&pb.pub, in, *size, NULL);
    av_assert1(ret == len);

    memset(buf + len, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    *out  = buf;
    *size = len;
    *offset = 0;

    return 0;
}

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
    int twelve_bit = 0;
    int high_bitdepth = get_bits1(gb);
    if (seq_params->profile == FF_PROFILE_AV1_PROFESSIONAL && high_bitdepth)
        twelve_bit = get_bits1(gb);

    seq_params->bitdepth = 8 + (high_bitdepth * 2) + (twelve_bit * 2);

    if (seq_params->profile == FF_PROFILE_AV1_HIGH)
        seq_params->monochrome = 0;
    else
        seq_params->monochrome = get_bits1(gb);

    seq_params->color_description_present_flag = get_bits1(gb);
    if (seq_params->color_description_present_flag) {
        seq_params->color_primaries          = get_bits(gb, 8);
        seq_params->transfer_characteristics = get_bits(gb, 8);
        seq_params->matrix_coefficients      = get_bits(gb, 8);
    } else {
        seq_params->color_primaries          = AVCOL_PRI_UNSPECIFIED;
        seq_params->transfer_characteristics = AVCOL_TRC_UNSPECIFIED;
        seq_params->matrix_coefficients      = AVCOL_SPC_UNSPECIFIED;
    }

    if (seq_params->monochrome) {
        seq_params->color_range = get_bits1(gb);
        seq_params->chroma_subsampling_x = 1;
        seq_params->chroma_subsampling_y = 1;
        seq_params->chroma_sample_position = 0;
        return 0;
    } else if (seq_params->color_primaries          == AVCOL_PRI_BT709 &&
               seq_params->transfer_characteristics == AVCOL_TRC_IEC61966_2_1 &&
               seq_params->matrix_coefficients      == AVCOL_SPC_RGB) {
        seq_params->chroma_subsampling_x = 0;
        seq_params->chroma_subsampling_y = 0;
    } else {
        seq_params->color_range = get_bits1(gb);

        if (seq_params->profile == FF_PROFILE_AV1_MAIN) {
            seq_params->chroma_subsampling_x = 1;
            seq_params->chroma_subsampling_y = 1;
        } else if (seq_params->profile == FF_PROFILE_AV1_HIGH) {
            seq_params->chroma_subsampling_x = 0;
            seq_params->chroma_subsampling_y = 0;
        } else {
            if (twelve_bit) {
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

    seq_params->profile = get_bits(&gb, 3);

    skip_bits1(&gb); // still_picture
    reduced_still_picture_header = get_bits1(&gb);

    if (reduced_still_picture_header) {
        seq_params->level = get_bits(&gb, 5);
        seq_params->tier = 0;
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
               seq_params->level = seq_level_idx;
               seq_params->tier = seq_tier;
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

        skip_bits(&gb, 4); // enable_interintra_compound (1), enable_masked_compound (1)
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

int ff_av1_parse_seq_header(AV1SequenceParameters *seq, const uint8_t *buf, int size)
{
    int is_av1c;

    if (size <= 0)
        return AVERROR_INVALIDDATA;

    is_av1c = !!(buf[0] & 0x80);
    if (is_av1c) {
        GetBitContext gb;
        int ret, version = buf[0] & 0x7F;

        if (version != 1 || size < 4)
            return AVERROR_INVALIDDATA;

        ret = init_get_bits8(&gb, buf, 4);
        if (ret < 0)
            return ret;

        memset(seq, 0, sizeof(*seq));

        skip_bits(&gb, 8);
        seq->profile    = get_bits(&gb, 3);
        seq->level      = get_bits(&gb, 5);
        seq->tier       = get_bits(&gb, 1);
        seq->bitdepth   = get_bits(&gb, 1) * 2 + 8;
        seq->bitdepth  += get_bits(&gb, 1) * 2;
        seq->monochrome               = get_bits(&gb, 1);
        seq->chroma_subsampling_x     = get_bits(&gb, 1);
        seq->chroma_subsampling_y     = get_bits(&gb, 1);
        seq->chroma_sample_position   = get_bits(&gb, 2);
        seq->color_primaries          = AVCOL_PRI_UNSPECIFIED;
        seq->transfer_characteristics = AVCOL_TRC_UNSPECIFIED;
        seq->matrix_coefficients      = AVCOL_SPC_UNSPECIFIED;

        size -= 4;
        buf  += 4;
    }

    while (size > 0) {
        int64_t obu_size;
        int start_pos, type, temporal_id, spatial_id;
        int len = parse_obu_header(buf, size, &obu_size, &start_pos,
                                   &type, &temporal_id, &spatial_id);
        if (len < 0)
            return len;

        switch (type) {
        case AV1_OBU_SEQUENCE_HEADER:
            if (!obu_size)
                return AVERROR_INVALIDDATA;

            return parse_sequence_header(seq, buf + start_pos, obu_size);
        default:
            break;
        }
        size -= len;
        buf  += len;
    }

    return is_av1c ? 0 : AVERROR_INVALIDDATA;
}

int ff_isom_write_av1c(AVIOContext *pb, const uint8_t *buf, int size,
                       int write_seq_header)
{
    AVIOContext *meta_pb;
    AV1SequenceParameters seq_params;
    PutBitContext pbc;
    uint8_t header[4], *meta;
    const uint8_t *seq;
    int ret, nb_seq = 0, seq_size, meta_size;

    if (size <= 0)
        return AVERROR_INVALIDDATA;

    if (buf[0] & 0x80) {
        // first bit is nonzero, the passed data does not consist purely of
        // OBUs. Expect that the data is already in AV1CodecConfigurationRecord
        // format.
        int config_record_version = buf[0] & 0x7f;
        if (config_record_version != 1 || size < 4) {
            return AVERROR_INVALIDDATA;
        }

        avio_write(pb, buf, size);

        return 0;
    }

    ret = avio_open_dyn_buf(&meta_pb);
    if (ret < 0)
        return ret;

    while (size > 0) {
        int64_t obu_size;
        int start_pos, type, temporal_id, spatial_id;
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

            seq      = buf;
            seq_size = len;
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

    if (!nb_seq) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    init_put_bits(&pbc, header, sizeof(header));

    put_bits(&pbc, 1, 1); // marker
    put_bits(&pbc, 7, 1); // version
    put_bits(&pbc, 3, seq_params.profile);
    put_bits(&pbc, 5, seq_params.level);
    put_bits(&pbc, 1, seq_params.tier);
    put_bits(&pbc, 1, seq_params.bitdepth > 8);
    put_bits(&pbc, 1, seq_params.bitdepth == 12);
    put_bits(&pbc, 1, seq_params.monochrome);
    put_bits(&pbc, 1, seq_params.chroma_subsampling_x);
    put_bits(&pbc, 1, seq_params.chroma_subsampling_y);
    put_bits(&pbc, 2, seq_params.chroma_sample_position);
    put_bits(&pbc, 8, 0); // padding
    flush_put_bits(&pbc);

    avio_write(pb, header, sizeof(header));
    if (write_seq_header) {
        avio_write(pb, seq, seq_size);
    }

    meta_size = avio_get_dyn_buf(meta_pb, &meta);
    if (meta_size)
        avio_write(pb, meta, meta_size);

fail:
    ffio_free_dyn_buf(&meta_pb);

    return ret;
}
