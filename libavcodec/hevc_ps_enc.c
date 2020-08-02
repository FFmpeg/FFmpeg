/*
 * HEVC Parameter Set encoding
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
#include "hevc_ps.h"
#include "put_bits.h"

static void write_ptl_layer(PutBitContext *pb, PTLCommon *ptl)
{
    int i;

    put_bits(pb, 2, ptl->profile_space);
    put_bits(pb, 1, ptl->tier_flag);
    put_bits(pb, 5, ptl->profile_idc);
    for (i = 0; i < 32; i++)
        put_bits(pb, 1, ptl->profile_compatibility_flag[i]);
    put_bits(pb, 1, ptl->progressive_source_flag);
    put_bits(pb, 1, ptl->interlaced_source_flag);
    put_bits(pb, 1, ptl->non_packed_constraint_flag);
    put_bits(pb, 1, ptl->frame_only_constraint_flag);
    put_bits32(pb, 0);   // reserved
    put_bits(pb, 12, 0); // reserved
}

static void write_ptl(PutBitContext *pb, PTL *ptl, int max_num_sub_layers)
{
    int i;

    write_ptl_layer(pb, &ptl->general_ptl);
    put_bits(pb, 8, ptl->general_ptl.level_idc);

    for (i = 0; i < max_num_sub_layers - 1; i++) {
        put_bits(pb, 1, ptl->sub_layer_profile_present_flag[i]);
        put_bits(pb, 1, ptl->sub_layer_level_present_flag[i]);
    }

    if (max_num_sub_layers > 1)
        for (i = max_num_sub_layers - 1; i < 8; i++)
            put_bits(pb, 2, 0); // reserved

    for (i = 0; i < max_num_sub_layers - 1; i++) {
        if (ptl->sub_layer_profile_present_flag[i])
            write_ptl_layer(pb, &ptl->sub_layer_ptl[i]);
        if (ptl->sub_layer_level_present_flag[i])
            put_bits(pb, 8, ptl->sub_layer_ptl[i].level_idc);
    }
}

int ff_hevc_encode_nal_vps(HEVCVPS *vps, unsigned int id,
                           uint8_t *buf, int buf_size)
{
    PutBitContext pb;
    int i, data_size;

    init_put_bits(&pb, buf, buf_size);
    put_bits(&pb,  4, id);
    put_bits(&pb,  2, 3);                               // reserved
    put_bits(&pb,  6, vps->vps_max_layers - 1);
    put_bits(&pb,  3, vps->vps_max_sub_layers - 1);
    put_bits(&pb,  1, vps->vps_temporal_id_nesting_flag);
    put_bits(&pb, 16, 0xffff);                          // reserved

    write_ptl(&pb, &vps->ptl, vps->vps_max_sub_layers);

    put_bits(&pb, 1, vps->vps_sub_layer_ordering_info_present_flag);
    for (i = vps->vps_sub_layer_ordering_info_present_flag ? 0 : vps->vps_max_layers - 1;
         i < vps->vps_max_sub_layers; i++) {
        set_ue_golomb(&pb, vps->vps_max_dec_pic_buffering[i] - 1);
        set_ue_golomb(&pb, vps->vps_num_reorder_pics[i]);
        set_ue_golomb(&pb, vps->vps_max_latency_increase[i] + 1);
    }

    put_bits(&pb, 6, vps->vps_max_layer_id);
    set_ue_golomb(&pb, vps->vps_num_layer_sets - 1);

    if (vps->vps_num_layer_sets > 1) {
        avpriv_report_missing_feature(NULL, "Writing layer_id_included_flag");
        return AVERROR_PATCHWELCOME;
    }

    put_bits(&pb, 1, vps->vps_timing_info_present_flag);
    if (vps->vps_timing_info_present_flag) {
        put_bits32(&pb, vps->vps_num_units_in_tick);
        put_bits32(&pb, vps->vps_time_scale);
        put_bits(&pb, 1, vps->vps_poc_proportional_to_timing_flag);
        if (vps->vps_poc_proportional_to_timing_flag)
            set_ue_golomb(&pb, vps->vps_num_ticks_poc_diff_one - 1);

        set_ue_golomb(&pb, vps->vps_num_hrd_parameters);
        if (vps->vps_num_hrd_parameters) {
            avpriv_report_missing_feature(NULL, "Writing HRD parameters");
            return AVERROR_PATCHWELCOME;
        }
    }

    put_bits(&pb, 1, 0);    // extension flag

    put_bits(&pb, 1, 1);    // stop bit
    flush_put_bits(&pb);

    data_size = put_bits_count(&pb) / 8;

    return data_size;
}
