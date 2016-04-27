/*
 * HEVC Supplementary Enhancement Information messages
 *
 * Copyright (C) 2012 - 2013 Guillaume Martres
 * Copyright (C) 2012 - 2013 Gildas Cocherel
 * Copyright (C) 2013 Vittorio Giovara
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
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
    GetBitContext *gb = &s->HEVClc.gb;
    uint8_t hash_type = get_bits(gb, 8);

    for (cIdx = 0; cIdx < 3; cIdx++) {
        if (hash_type == 0) {
            s->is_md5 = 1;
            for (i = 0; i < 16; i++)
                s->md5[cIdx][i] = get_bits(gb, 8);
        } else if (hash_type == 1) {
            // picture_crc = get_bits(gb, 16);
            skip_bits(gb, 16);
        } else if (hash_type == 2) {
            // picture_checksum = get_bits(gb, 32);
            skip_bits(gb, 32);
        }
    }
    return 0;
}

static int decode_nal_sei_frame_packing_arrangement(HEVCContext *s)
{
    GetBitContext *gb = &s->HEVClc.gb;

    get_ue_golomb(gb);                  // frame_packing_arrangement_id
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
    GetBitContext *gb = &s->HEVClc.gb;

    s->sei_display_orientation_present = !get_bits1(gb);

    if (s->sei_display_orientation_present) {
        s->sei_hflip = get_bits1(gb);     // hor_flip
        s->sei_vflip = get_bits1(gb);     // ver_flip

        s->sei_anticlockwise_rotation = get_bits(gb, 16);
        skip_bits1(gb);     // display_orientation_persistence_flag
    }

    return 0;
}

static int decode_nal_sei_prefix(HEVCContext *s, int type, int size)
{
    GetBitContext *gb = &s->HEVClc.gb;

    switch (type) {
    case 256:  // Mismatched value from HM 8.1
        return decode_nal_sei_decoded_picture_hash(s);
    case SEI_TYPE_FRAME_PACKING:
        return decode_nal_sei_frame_packing_arrangement(s);
    case SEI_TYPE_DISPLAY_ORIENTATION:
        return decode_nal_sei_display_orientation(s);
    default:
        av_log(s->avctx, AV_LOG_DEBUG, "Skipped PREFIX SEI %d\n", type);
        skip_bits_long(gb, 8 * size);
        return 0;
    }
}

static int decode_nal_sei_suffix(HEVCContext *s, int type, int size)
{
    GetBitContext *gb = &s->HEVClc.gb;

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
    GetBitContext *gb = &s->HEVClc.gb;

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
    return 0;
}

static int more_rbsp_data(GetBitContext *gb)
{
    return get_bits_left(gb) > 0 && show_bits(gb, 8) != 0x80;
}

int ff_hevc_decode_nal_sei(HEVCContext *s)
{
    do {
        decode_nal_sei_message(s);
    } while (more_rbsp_data(&s->HEVClc.gb));
    return 0;
}
