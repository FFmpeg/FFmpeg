/*
 * H.26L/H.264/AVC/JVT/14496-10/... sei decoding
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * H.264 / AVC / MPEG4 part10 sei decoding.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "avcodec.h"
#include "golomb.h"
#include "h264.h"
#include "internal.h"

static const uint8_t sei_num_clock_ts_table[9] = {
    1, 1, 1, 2, 2, 3, 3, 2, 3
};

void ff_h264_reset_sei(H264Context *h)
{
    h->sei_recovery_frame_cnt       = -1;
    h->sei_dpb_output_delay         =  0;
    h->sei_cpb_removal_delay        = -1;
    h->sei_buffering_period_present =  0;
    h->sei_frame_packing_present    =  0;
}

static int decode_picture_timing(H264Context *h)
{
    if (h->sps.nal_hrd_parameters_present_flag ||
        h->sps.vcl_hrd_parameters_present_flag) {
        h->sei_cpb_removal_delay = get_bits(&h->gb,
                                            h->sps.cpb_removal_delay_length);
        h->sei_dpb_output_delay  = get_bits(&h->gb,
                                            h->sps.dpb_output_delay_length);
    }
    if (h->sps.pic_struct_present_flag) {
        unsigned int i, num_clock_ts;

        h->sei_pic_struct = get_bits(&h->gb, 4);
        h->sei_ct_type    = 0;

        if (h->sei_pic_struct > SEI_PIC_STRUCT_FRAME_TRIPLING)
            return AVERROR_INVALIDDATA;

        num_clock_ts = sei_num_clock_ts_table[h->sei_pic_struct];

        for (i = 0; i < num_clock_ts; i++) {
            if (get_bits(&h->gb, 1)) {                /* clock_timestamp_flag */
                unsigned int full_timestamp_flag;

                h->sei_ct_type |= 1 << get_bits(&h->gb, 2);
                skip_bits(&h->gb, 1);                 /* nuit_field_based_flag */
                skip_bits(&h->gb, 5);                 /* counting_type */
                full_timestamp_flag = get_bits(&h->gb, 1);
                skip_bits(&h->gb, 1);                 /* discontinuity_flag */
                skip_bits(&h->gb, 1);                 /* cnt_dropped_flag */
                skip_bits(&h->gb, 8);                 /* n_frames */
                if (full_timestamp_flag) {
                    skip_bits(&h->gb, 6);             /* seconds_value 0..59 */
                    skip_bits(&h->gb, 6);             /* minutes_value 0..59 */
                    skip_bits(&h->gb, 5);             /* hours_value 0..23 */
                } else {
                    if (get_bits(&h->gb, 1)) {        /* seconds_flag */
                        skip_bits(&h->gb, 6);         /* seconds_value range 0..59 */
                        if (get_bits(&h->gb, 1)) {    /* minutes_flag */
                            skip_bits(&h->gb, 6);     /* minutes_value 0..59 */
                            if (get_bits(&h->gb, 1))  /* hours_flag */
                                skip_bits(&h->gb, 5); /* hours_value 0..23 */
                        }
                    }
                }
                if (h->sps.time_offset_length > 0)
                    skip_bits(&h->gb,
                              h->sps.time_offset_length); /* time_offset */
            }
        }

        if (h->avctx->debug & FF_DEBUG_PICT_INFO)
            av_log(h->avctx, AV_LOG_DEBUG, "ct_type:%X pic_struct:%d\n",
                   h->sei_ct_type, h->sei_pic_struct);
    }
    return 0;
}

static int decode_unregistered_user_data(H264Context *h, int size)
{
    uint8_t user_data[16 + 256];
    int e, build, i;

    if (size < 16)
        return AVERROR_INVALIDDATA;

    for (i = 0; i < sizeof(user_data) - 1 && i < size; i++)
        user_data[i] = get_bits(&h->gb, 8);

    user_data[i] = 0;
    e = sscanf(user_data + 16, "x264 - core %d", &build);
    if (e == 1 && build > 0)
        h->x264_build = build;

    if (h->avctx->debug & FF_DEBUG_BUGS)
        av_log(h->avctx, AV_LOG_DEBUG, "user data:\"%s\"\n", user_data + 16);

    for (; i < size; i++)
        skip_bits(&h->gb, 8);

    return 0;
}

static int decode_recovery_point(H264Context *h)
{
    h->sei_recovery_frame_cnt = get_ue_golomb(&h->gb);

    /* 1b exact_match_flag,
     * 1b broken_link_flag,
     * 2b changing_slice_group_idc */
    skip_bits(&h->gb, 4);

    return 0;
}

static int decode_buffering_period(H264Context *h)
{
    unsigned int sps_id;
    int sched_sel_idx;
    SPS *sps;

    sps_id = get_ue_golomb_31(&h->gb);
    if (sps_id > 31 || !h->sps_buffers[sps_id]) {
        av_log(h->avctx, AV_LOG_ERROR,
               "non-existing SPS %d referenced in buffering period\n", sps_id);
        return AVERROR_INVALIDDATA;
    }
    sps = h->sps_buffers[sps_id];

    // NOTE: This is really so duplicated in the standard... See H.264, D.1.1
    if (sps->nal_hrd_parameters_present_flag) {
        for (sched_sel_idx = 0; sched_sel_idx < sps->cpb_cnt; sched_sel_idx++) {
            h->initial_cpb_removal_delay[sched_sel_idx] =
                get_bits(&h->gb, sps->initial_cpb_removal_delay_length);
            // initial_cpb_removal_delay_offset
            skip_bits(&h->gb, sps->initial_cpb_removal_delay_length);
        }
    }
    if (sps->vcl_hrd_parameters_present_flag) {
        for (sched_sel_idx = 0; sched_sel_idx < sps->cpb_cnt; sched_sel_idx++) {
            h->initial_cpb_removal_delay[sched_sel_idx] =
                get_bits(&h->gb, sps->initial_cpb_removal_delay_length);
            // initial_cpb_removal_delay_offset
            skip_bits(&h->gb, sps->initial_cpb_removal_delay_length);
        }
    }

    h->sei_buffering_period_present = 1;
    return 0;
}

static int decode_frame_packing_arrangement(H264Context *h)
{
    get_ue_golomb(&h->gb);              // frame_packing_arrangement_id
    h->sei_frame_packing_present = !get_bits1(&h->gb);

    if (h->sei_frame_packing_present) {
        h->frame_packing_arrangement_type = get_bits(&h->gb, 7);
        h->quincunx_subsampling           = get_bits1(&h->gb);
        h->content_interpretation_type    = get_bits(&h->gb, 6);

        // the following skips: spatial_flipping_flag, frame0_flipped_flag,
        // field_views_flag, current_frame_is_frame0_flag,
        // frame0_self_contained_flag, frame1_self_contained_flag
        skip_bits(&h->gb, 6);

        if (!h->quincunx_subsampling && h->frame_packing_arrangement_type != 5)
            skip_bits(&h->gb, 16);      // frame[01]_grid_position_[xy]
        skip_bits(&h->gb, 8);           // frame_packing_arrangement_reserved_byte
        get_ue_golomb(&h->gb);          // frame_packing_arrangement_repetition_period
    }
    skip_bits1(&h->gb);                 // frame_packing_arrangement_extension_flag

    return 0;
}

int ff_h264_decode_sei(H264Context *h)
{
    while (get_bits_left(&h->gb) > 16) {
        int size = 0;
        int type = 0;
        int ret  = 0;
        int last = 0;

        while (get_bits_left(&h->gb) >= 8 &&
               (last = get_bits(&h->gb, 8)) == 255) {
            type += 255;
        }
        type += last;

        last = 0;
        while (get_bits_left(&h->gb) >= 8 &&
               (last = get_bits(&h->gb, 8)) == 255) {
            size += 255;
        }
        size += last;

        if (size > get_bits_left(&h->gb) / 8) {
            av_log(h->avctx, AV_LOG_ERROR, "SEI type %d truncated at %d\n",
                   type, get_bits_left(&h->gb));
            return AVERROR_INVALIDDATA;
        }

        switch (type) {
        case SEI_TYPE_PIC_TIMING: // Picture timing SEI
            ret = decode_picture_timing(h);
            if (ret < 0)
                return ret;
            break;
        case SEI_TYPE_USER_DATA_UNREGISTERED:
            ret = decode_unregistered_user_data(h, size);
            if (ret < 0)
                return ret;
            break;
        case SEI_TYPE_RECOVERY_POINT:
            ret = decode_recovery_point(h);
            if (ret < 0)
                return ret;
            break;
        case SEI_TYPE_BUFFERING_PERIOD:
            ret = decode_buffering_period(h);
            if (ret < 0)
                return ret;
            break;
        case SEI_TYPE_FRAME_PACKING:
            ret = decode_frame_packing_arrangement(h);
            if (ret < 0)
                return ret;
            break;
        default:
            av_log(h->avctx, AV_LOG_DEBUG, "unknown SEI type %d\n", type);
            skip_bits(&h->gb, 8 * size);
        }

        // FIXME check bits here
        align_get_bits(&h->gb);
    }

    return 0;
}
