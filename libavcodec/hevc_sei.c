/*
 * HEVC Supplementary Enhancement Information messages
 *
 * Copyright (C) 2012 - 2013 Guillaume Martres
 * Copyright (C) 2012 - 2013 Gildas Cocherel
 * Copyright (C) 2013 Vittorio Giovara
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

#include "golomb.h"
#include "hevc.h"

enum HEVC_SEI_TYPE {
    SEI_TYPE_BUFFERING_PERIOD                     = 0,
    SEI_TYPE_PICTURE_TIMING                       = 1,
    SEI_TYPE_PAN_SCAN_RECT                        = 2,
    SEI_TYPE_FILLER_PAYLOAD                       = 3,
    SEI_TYPE_USER_DATA_REGISTERED_ITU_T_T35       = 4,
    SEI_TYPE_USER_DATA_UNREGISTERED               = 5,
    SEI_TYPE_RECOVERY_POINT                       = 6,
    SEI_TYPE_SCENE_INFO                           = 9,
    SEI_TYPE_FULL_FRAME_SNAPSHOT                  = 15,
    SEI_TYPE_PROGRESSIVE_REFINEMENT_SEGMENT_START = 16,
    SEI_TYPE_PROGRESSIVE_REFINEMENT_SEGMENT_END   = 17,
    SEI_TYPE_FILM_GRAIN_CHARACTERISTICS           = 19,
    SEI_TYPE_POST_FILTER_HINT                     = 22,
    SEI_TYPE_TONE_MAPPING_INFO                    = 23,
    SEI_TYPE_FRAME_PACKING                        = 45,
    SEI_TYPE_DISPLAY_ORIENTATION                  = 47,
    SEI_TYPE_SOP_DESCRIPTION                      = 128,
    SEI_TYPE_ACTIVE_PARAMETER_SETS                = 129,
    SEI_TYPE_DECODING_UNIT_INFO                   = 130,
    SEI_TYPE_TEMPORAL_LEVEL0_INDEX                = 131,
    SEI_TYPE_DECODED_PICTURE_HASH                 = 132,
    SEI_TYPE_SCALABLE_NESTING                     = 133,
    SEI_TYPE_REGION_REFRESH_INFO                  = 134,
    SEI_TYPE_MASTERING_DISPLAY_INFO               = 137,
    SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO             = 144,
};

static int decode_nal_sei_decoded_picture_hash(HEVCContext *s)
{
    int cIdx, i;
    uint8_t hash_type;
    //uint16_t picture_crc;
    //uint32_t picture_checksum;
    GetBitContext *gb = &s->HEVClc->gb;
    hash_type = get_bits(gb, 8);

    for (cIdx = 0; cIdx < 3/*((s->sps->chroma_format_idc == 0) ? 1 : 3)*/; cIdx++) {
        if (hash_type == 0) {
            s->is_md5 = 1;
            for (i = 0; i < 16; i++)
                s->md5[cIdx][i] = get_bits(gb, 8);
        } else if (hash_type == 1) {
            // picture_crc = get_bits(gb, 16);
            skip_bits(gb, 16);
        } else if (hash_type == 2) {
            // picture_checksum = get_bits_long(gb, 32);
            skip_bits(gb, 32);
        }
    }
    return 0;
}

static int decode_nal_sei_mastering_display_info(HEVCContext *s)
{
    GetBitContext *gb = &s->HEVClc->gb;
    int i;
    // Mastering primaries
    for (i = 0; i < 3; i++) {
        s->display_primaries[i][0] = get_bits(gb, 16);
        s->display_primaries[i][1] = get_bits(gb, 16);
    }
    // White point (x, y)
    s->white_point[0] = get_bits(gb, 16);
    s->white_point[1] = get_bits(gb, 16);

    // Max and min luminance of mastering display
    s->max_mastering_luminance = get_bits_long(gb, 32);
    s->min_mastering_luminance = get_bits_long(gb, 32);

    // As this SEI message comes before the first frame that references it,
    // initialize the flag to 2 and decrement on IRAP access unit so it
    // persists for the coded video sequence (e.g., between two IRAPs)
    s->sei_mastering_display_info_present = 2;
    return 0;
}

static int decode_nal_sei_frame_packing_arrangement(HEVCContext *s)
{
    GetBitContext *gb = &s->HEVClc->gb;

    get_ue_golomb_long(gb);             // frame_packing_arrangement_id
    s->sei_frame_packing_present = !get_bits1(gb);

    if (s->sei_frame_packing_present) {
        s->frame_packing_arrangement_type = get_bits(gb, 7);
        s->quincunx_subsampling           = get_bits1(gb);
        s->content_interpretation_type    = get_bits(gb, 6);

        // the following skips spatial_flipping_flag frame0_flipped_flag
        // field_views_flag current_frame_is_frame0_flag
        // frame0_self_contained_flag frame1_self_contained_flag
        skip_bits(gb, 6);

        if (!s->quincunx_subsampling && s->frame_packing_arrangement_type != 5)
            skip_bits(gb, 16);  // frame[01]_grid_position_[xy]
        skip_bits(gb, 8);       // frame_packing_arrangement_reserved_byte
        skip_bits1(gb);         // frame_packing_arrangement_persistence_flag
    }
    skip_bits1(gb);             // upsampled_aspect_ratio_flag
    return 0;
}

static int decode_nal_sei_display_orientation(HEVCContext *s)
{
    GetBitContext *gb = &s->HEVClc->gb;

    s->sei_display_orientation_present = !get_bits1(gb);

    if (s->sei_display_orientation_present) {
        s->sei_hflip = get_bits1(gb);     // hor_flip
        s->sei_vflip = get_bits1(gb);     // ver_flip

        s->sei_anticlockwise_rotation = get_bits(gb, 16);
        skip_bits1(gb);     // display_orientation_persistence_flag
    }

    return 0;
}

static int decode_pic_timing(HEVCContext *s)
{
    GetBitContext *gb = &s->HEVClc->gb;
    HEVCSPS *sps;

    if (!s->ps.sps_list[s->active_seq_parameter_set_id])
        return(AVERROR(ENOMEM));
    sps = (HEVCSPS*)s->ps.sps_list[s->active_seq_parameter_set_id]->data;

    if (sps->vui.frame_field_info_present_flag) {
        int pic_struct = get_bits(gb, 4);
        s->picture_struct = AV_PICTURE_STRUCTURE_UNKNOWN;
        if (pic_struct == 2) {
            av_log(s->avctx, AV_LOG_DEBUG, "BOTTOM Field\n");
            s->picture_struct = AV_PICTURE_STRUCTURE_BOTTOM_FIELD;
        } else if (pic_struct == 1) {
            av_log(s->avctx, AV_LOG_DEBUG, "TOP Field\n");
            s->picture_struct = AV_PICTURE_STRUCTURE_TOP_FIELD;
        }
        get_bits(gb, 2);                   // source_scan_type
        get_bits(gb, 1);                   // duplicate_flag
    }
    return 1;
}

static int decode_registered_user_data_closed_caption(HEVCContext *s, int size)
{
    int flag;
    int user_data_type_code;
    int cc_count;

    GetBitContext *gb = &s->HEVClc->gb;

    if (size < 3)
       return AVERROR(EINVAL);

    user_data_type_code = get_bits(gb, 8);
    if (user_data_type_code == 0x3) {
        skip_bits(gb, 1); // reserved

        flag = get_bits(gb, 1); // process_cc_data_flag
        if (flag) {
            skip_bits(gb, 1);
            cc_count = get_bits(gb, 5);
            skip_bits(gb, 8); // reserved
            size -= 2;

            if (cc_count && size >= cc_count * 3) {
                const uint64_t new_size = (s->a53_caption_size + cc_count
                                           * UINT64_C(3));
                int i, ret;

                if (new_size > INT_MAX)
                    return AVERROR(EINVAL);

                /* Allow merging of the cc data from two fields. */
                ret = av_reallocp(&s->a53_caption, new_size);
                if (ret < 0)
                    return ret;

                for (i = 0; i < cc_count; i++) {
                    s->a53_caption[s->a53_caption_size++] = get_bits(gb, 8);
                    s->a53_caption[s->a53_caption_size++] = get_bits(gb, 8);
                    s->a53_caption[s->a53_caption_size++] = get_bits(gb, 8);
                }
                skip_bits(gb, 8); // marker_bits
            }
        }
    } else {
        int i;
        for (i = 0; i < size - 1; i++)
            skip_bits(gb, 8);
    }

    return 0;
}

static int decode_nal_sei_user_data_registered_itu_t_t35(HEVCContext *s, int size)
{
    uint32_t country_code;
    uint32_t user_identifier;

    GetBitContext *gb = &s->HEVClc->gb;

    if (size < 7)
        return AVERROR(EINVAL);
    size -= 7;

    country_code = get_bits(gb, 8);
    if (country_code == 0xFF) {
        skip_bits(gb, 8);
        size--;
    }

    skip_bits(gb, 8);
    skip_bits(gb, 8);

    user_identifier = get_bits_long(gb, 32);

    switch (user_identifier) {
        case MKBETAG('G', 'A', '9', '4'):
            return decode_registered_user_data_closed_caption(s, size);
        default:
            skip_bits_long(gb, size * 8);
            break;
    }
    return 0;
}

static int active_parameter_sets(HEVCContext *s)
{
    GetBitContext *gb = &s->HEVClc->gb;
    int num_sps_ids_minus1;
    int i;
    unsigned active_seq_parameter_set_id;

    get_bits(gb, 4); // active_video_parameter_set_id
    get_bits(gb, 1); // self_contained_cvs_flag
    get_bits(gb, 1); // num_sps_ids_minus1
    num_sps_ids_minus1 = get_ue_golomb_long(gb); // num_sps_ids_minus1

    if (num_sps_ids_minus1 < 0 || num_sps_ids_minus1 > 15) {
        av_log(s->avctx, AV_LOG_ERROR, "num_sps_ids_minus1 %d invalid\n", num_sps_ids_minus1);
        return AVERROR_INVALIDDATA;
    }

    active_seq_parameter_set_id = get_ue_golomb_long(gb);
    if (active_seq_parameter_set_id >= MAX_SPS_COUNT) {
        av_log(s->avctx, AV_LOG_ERROR, "active_parameter_set_id %d invalid\n", active_seq_parameter_set_id);
        return AVERROR_INVALIDDATA;
    }
    s->active_seq_parameter_set_id = active_seq_parameter_set_id;

    for (i = 1; i <= num_sps_ids_minus1; i++)
        get_ue_golomb_long(gb); // active_seq_parameter_set_id[i]

    return 0;
}

static int decode_nal_sei_prefix(HEVCContext *s, int type, int size)
{
    GetBitContext *gb = &s->HEVClc->gb;

    switch (type) {
    case 256:  // Mismatched value from HM 8.1
        return decode_nal_sei_decoded_picture_hash(s);
    case SEI_TYPE_FRAME_PACKING:
        return decode_nal_sei_frame_packing_arrangement(s);
    case SEI_TYPE_DISPLAY_ORIENTATION:
        return decode_nal_sei_display_orientation(s);
    case SEI_TYPE_PICTURE_TIMING:
        {
            int ret = decode_pic_timing(s);
            av_log(s->avctx, AV_LOG_DEBUG, "Skipped PREFIX SEI %d\n", type);
            skip_bits(gb, 8 * size);
            return ret;
        }
    case SEI_TYPE_MASTERING_DISPLAY_INFO:
        return decode_nal_sei_mastering_display_info(s);
    case SEI_TYPE_ACTIVE_PARAMETER_SETS:
        active_parameter_sets(s);
        av_log(s->avctx, AV_LOG_DEBUG, "Skipped PREFIX SEI %d\n", type);
        return 0;
    case SEI_TYPE_USER_DATA_REGISTERED_ITU_T_T35:
        return decode_nal_sei_user_data_registered_itu_t_t35(s, size);
    default:
        av_log(s->avctx, AV_LOG_DEBUG, "Skipped PREFIX SEI %d\n", type);
        skip_bits_long(gb, 8 * size);
        return 0;
    }
}

static int decode_nal_sei_suffix(HEVCContext *s, int type, int size)
{
    GetBitContext *gb = &s->HEVClc->gb;

    switch (type) {
    case SEI_TYPE_DECODED_PICTURE_HASH:
        return decode_nal_sei_decoded_picture_hash(s);
    default:
        av_log(s->avctx, AV_LOG_DEBUG, "Skipped SUFFIX SEI %d\n", type);
        skip_bits_long(gb, 8 * size);
        return 0;
    }
}

static int decode_nal_sei_message(HEVCContext *s)
{
    GetBitContext *gb = &s->HEVClc->gb;

    int payload_type = 0;
    int payload_size = 0;
    int byte = 0xFF;
    av_log(s->avctx, AV_LOG_DEBUG, "Decoding SEI\n");

    while (byte == 0xFF) {
        byte          = get_bits(gb, 8);
        payload_type += byte;
    }
    byte = 0xFF;
    while (byte == 0xFF) {
        byte          = get_bits(gb, 8);
        payload_size += byte;
    }
    if (s->nal_unit_type == NAL_SEI_PREFIX) {
        return decode_nal_sei_prefix(s, payload_type, payload_size);
    } else { /* nal_unit_type == NAL_SEI_SUFFIX */
        return decode_nal_sei_suffix(s, payload_type, payload_size);
    }
    return 1;
}

static int more_rbsp_data(GetBitContext *gb)
{
    return get_bits_left(gb) > 0 && show_bits(gb, 8) != 0x80;
}

int ff_hevc_decode_nal_sei(HEVCContext *s)
{
    int ret;

    do {
        ret = decode_nal_sei_message(s);
        if (ret < 0)
            return(AVERROR(ENOMEM));
    } while (more_rbsp_data(&s->HEVClc->gb));
    return 1;
}

void ff_hevc_reset_sei(HEVCContext *s)
{
    s->a53_caption_size = 0;
    av_freep(&s->a53_caption);
}
