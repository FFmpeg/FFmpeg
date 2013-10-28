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

#include "hevc.h"
#include "golomb.h"

static void decode_nal_sei_decoded_picture_hash(HEVCContext *s, int payload_size)
{
    int cIdx, i;
    uint8_t hash_type;
    //uint16_t picture_crc;
    //uint32_t picture_checksum;
    GetBitContext *gb = &s->HEVClc->gb;
    hash_type = get_bits(gb, 8);


    for( cIdx = 0; cIdx < 3/*((s->sps->chroma_format_idc == 0) ? 1 : 3)*/; cIdx++ ) {
        if ( hash_type == 0 ) {
            s->is_md5 = 1;
            for( i = 0; i < 16; i++) {
                s->md5[cIdx][i] = get_bits(gb, 8);
            }
        } else if( hash_type == 1 ) {
            // picture_crc = get_bits(gb, 16);
            skip_bits(gb, 16);
        } else if( hash_type == 2 ) {
            // picture_checksum = get_bits(gb, 32);
            skip_bits(gb, 32);
        }
    }
}

static void decode_nal_sei_frame_packing_arrangement(HEVCLocalContext *lc)
{
    GetBitContext *gb = &lc->gb;
    int cancel, type, quincunx;

    get_ue_golomb(gb);                      // frame_packing_arrangement_id
    cancel = get_bits1(gb);                 // frame_packing_cancel_flag
    if ( cancel == 0 )
    {
        type = get_bits(gb, 7);             // frame_packing_arrangement_type
        quincunx = get_bits1(gb);           // quincunx_sampling_flag
        skip_bits(gb, 6);                   // content_interpretation_type

        // the following skips spatial_flipping_flag frame0_flipped_flag
        // field_views_flag current_frame_is_frame0_flag
        // frame0_self_contained_flag frame1_self_contained_flag
        skip_bits(gb, 6);

        if ( quincunx == 0 && type != 5 )
            skip_bits(gb, 16);              // frame[01]_grid_position_[xy]
        skip_bits(gb, 8);                   // frame_packing_arrangement_reserved_byte
        skip_bits1(gb);                     // frame_packing_arrangement_persistance_flag
    }
    skip_bits1(gb);                         // upsampled_aspect_ratio_flag
}

static int decode_nal_sei_message(HEVCContext *s)
{
    GetBitContext *gb = &s->HEVClc->gb;

    int payload_type = 0;
    int payload_size = 0;
    int byte = 0xFF;
    av_log(s->avctx, AV_LOG_DEBUG, "Decoding SEI\n");

    while (byte == 0xFF) {
        byte = get_bits(gb, 8);
        payload_type += byte;
    }
    byte = 0xFF;
    while (byte == 0xFF) {
        byte = get_bits(gb, 8);
        payload_size += byte;
    }
    if (s->nal_unit_type == NAL_SEI_PREFIX) {
        if (payload_type == 256 /*&& s->decode_checksum_sei*/)
            decode_nal_sei_decoded_picture_hash(s, payload_size);
        else if (payload_type == 45)
            decode_nal_sei_frame_packing_arrangement(s->HEVClc);
        else {
            av_log(s->avctx, AV_LOG_DEBUG, "Skipped PREFIX SEI %d\n", payload_type);
            skip_bits(gb, 8*payload_size);
        }
    } else { /* nal_unit_type == NAL_SEI_SUFFIX */
        if (payload_type == 132 /* && s->decode_checksum_sei */)
            decode_nal_sei_decoded_picture_hash(s, payload_size);
        else {
            av_log(s->avctx, AV_LOG_DEBUG, "Skipped SUFFIX SEI %d\n", payload_type);
            skip_bits(gb, 8*payload_size);
        }
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
    } while (more_rbsp_data(&s->HEVClc->gb));
    return 0;
}
