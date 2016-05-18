/*
 * H.26L/H.264/AVC/JVT/14496-10/... SEI decoding
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
 * H.264 / AVC / MPEG-4 part10 SEI decoding.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "avcodec.h"
#include "get_bits.h"
#include "golomb.h"
#include "h264_ps.h"
#include "h264_sei.h"
#include "internal.h"

static const uint8_t sei_num_clock_ts_table[9] = {
    1, 1, 1, 2, 2, 3, 3, 2, 3
};

void ff_h264_sei_uninit(H264SEIContext *h)
{
    h->unregistered.x264_build           = -1;
    h->recovery_point.recovery_frame_cnt = -1;

    h->picture_timing.dpb_output_delay  = 0;
    h->picture_timing.cpb_removal_delay = -1;

    h->buffering_period.present    = 0;
    h->frame_packing.present       = 0;
    h->display_orientation.present = 0;
    h->afd.present                 =  0;

    h->a53_caption.a53_caption_size = 0;
    av_freep(&h->a53_caption.a53_caption);
}

static int decode_picture_timing(H264SEIPictureTiming *h, GetBitContext *gb,
                                 const SPS *sps, void *logctx)
{
    if (!sps)
        return AVERROR_INVALIDDATA;

    if (sps->nal_hrd_parameters_present_flag ||
        sps->vcl_hrd_parameters_present_flag) {
        h->cpb_removal_delay = get_bits(gb, sps->cpb_removal_delay_length);
        h->dpb_output_delay  = get_bits(gb, sps->dpb_output_delay_length);
    }
    if (sps->pic_struct_present_flag) {
        unsigned int i, num_clock_ts;

        h->pic_struct = get_bits(gb, 4);
        h->ct_type    = 0;

        if (h->pic_struct > SEI_PIC_STRUCT_FRAME_TRIPLING)
            return AVERROR_INVALIDDATA;

        num_clock_ts = sei_num_clock_ts_table[h->pic_struct];

        for (i = 0; i < num_clock_ts; i++) {
            if (get_bits(gb, 1)) {                /* clock_timestamp_flag */
                unsigned int full_timestamp_flag;

                h->ct_type |= 1 << get_bits(gb, 2);
                skip_bits(gb, 1);                 /* nuit_field_based_flag */
                skip_bits(gb, 5);                 /* counting_type */
                full_timestamp_flag = get_bits(gb, 1);
                skip_bits(gb, 1);                 /* discontinuity_flag */
                skip_bits(gb, 1);                 /* cnt_dropped_flag */
                skip_bits(gb, 8);                 /* n_frames */
                if (full_timestamp_flag) {
                    skip_bits(gb, 6);             /* seconds_value 0..59 */
                    skip_bits(gb, 6);             /* minutes_value 0..59 */
                    skip_bits(gb, 5);             /* hours_value 0..23 */
                } else {
                    if (get_bits(gb, 1)) {        /* seconds_flag */
                        skip_bits(gb, 6);         /* seconds_value range 0..59 */
                        if (get_bits(gb, 1)) {    /* minutes_flag */
                            skip_bits(gb, 6);     /* minutes_value 0..59 */
                            if (get_bits(gb, 1))  /* hours_flag */
                                skip_bits(gb, 5); /* hours_value 0..23 */
                        }
                    }
                }
                if (sps->time_offset_length > 0)
                    skip_bits(gb,
                              sps->time_offset_length); /* time_offset */
            }
        }

        av_log(logctx, AV_LOG_DEBUG, "ct_type:%X pic_struct:%d\n",
               h->ct_type, h->pic_struct);
    }
    return 0;
}

static int decode_registered_user_data_afd(H264SEIAFD *h, GetBitContext *gb, int size)
{
    int flag;

    if (size-- < 1)
        return AVERROR_INVALIDDATA;
    skip_bits(gb, 1);               // 0
    flag = get_bits(gb, 1);         // active_format_flag
    skip_bits(gb, 6);               // reserved

    if (flag) {
        if (size-- < 1)
            return AVERROR_INVALIDDATA;
        skip_bits(gb, 4);           // reserved
        h->active_format_description = get_bits(gb, 4);
        h->present                   = 1;
    }

    return 0;
}

static int decode_registered_user_data_closed_caption(H264SEIA53Caption *h,
                                                     GetBitContext *gb, void *logctx,
                                                     int size)
{
    int flag;
    int user_data_type_code;
    int cc_count;

    if (size < 3)
        return AVERROR(EINVAL);

    user_data_type_code = get_bits(gb, 8);
    if (user_data_type_code == 0x3) {
        skip_bits(gb, 1);           // reserved

        flag = get_bits(gb, 1);     // process_cc_data_flag
        if (flag) {
            skip_bits(gb, 1);       // zero bit
            cc_count = get_bits(gb, 5);
            skip_bits(gb, 8);       // reserved
            size -= 2;

            if (cc_count && size >= cc_count * 3) {
                const uint64_t new_size = (h->a53_caption_size + cc_count
                                           * UINT64_C(3));
                int i, ret;

                if (new_size > INT_MAX)
                    return AVERROR(EINVAL);

                /* Allow merging of the cc data from two fields. */
                ret = av_reallocp(&h->a53_caption, new_size);
                if (ret < 0)
                    return ret;

                for (i = 0; i < cc_count; i++) {
                    h->a53_caption[h->a53_caption_size++] = get_bits(gb, 8);
                    h->a53_caption[h->a53_caption_size++] = get_bits(gb, 8);
                    h->a53_caption[h->a53_caption_size++] = get_bits(gb, 8);
                }

                skip_bits(gb, 8);   // marker_bits
            }
        }
    } else {
        int i;
        avpriv_request_sample(logctx, "Subtitles with data type 0x%02x",
                              user_data_type_code);
        for (i = 0; i < size - 1; i++)
            skip_bits(gb, 8);
    }

    return 0;
}

static int decode_registered_user_data(H264SEIContext *h, GetBitContext *gb,
                                       void *logctx, int size)
{
    uint32_t country_code;
    uint32_t user_identifier;

    if (size < 7)
        return AVERROR_INVALIDDATA;
    size -= 7;

    country_code = get_bits(gb, 8); // itu_t_t35_country_code
    if (country_code == 0xFF) {
        skip_bits(gb, 8);           // itu_t_t35_country_code_extension_byte
        size--;
    }

    /* itu_t_t35_payload_byte follows */
    skip_bits(gb, 8);              // terminal provider code
    skip_bits(gb, 8);              // terminal provider oriented code
    user_identifier = get_bits_long(gb, 32);

    switch (user_identifier) {
        case MKBETAG('D', 'T', 'G', '1'):       // afd_data
            return decode_registered_user_data_afd(&h->afd, gb, size);
        case MKBETAG('G', 'A', '9', '4'):       // closed captions
            return decode_registered_user_data_closed_caption(&h->a53_caption, gb,
                                                              logctx, size);
        default:
            skip_bits(gb, size * 8);
            break;
    }

    return 0;
}

static int decode_unregistered_user_data(H264SEIUnregistered *h, GetBitContext *gb,
                                         void *logctx, int size)
{
    uint8_t *user_data;
    int e, build, i;

    if (size < 16 || size >= INT_MAX - 16)
        return AVERROR_INVALIDDATA;

    user_data = av_malloc(16 + size + 1);
    if (!user_data)
        return AVERROR(ENOMEM);

    for (i = 0; i < size + 16; i++)
        user_data[i] = get_bits(gb, 8);

    user_data[i] = 0;
    e = sscanf(user_data + 16, "x264 - core %d", &build);
    if (e == 1 && build > 0)
        h->x264_build = build;

    if (strlen(user_data + 16) > 0)
        av_log(logctx, AV_LOG_DEBUG, "user data:\"%s\"\n", user_data + 16);

    av_free(user_data);
    return 0;
}

static int decode_recovery_point(H264SEIRecoveryPoint *h, GetBitContext *gb)
{
    h->recovery_frame_cnt = get_ue_golomb(gb);

    /* 1b exact_match_flag,
     * 1b broken_link_flag,
     * 2b changing_slice_group_idc */
    skip_bits(gb, 4);

    return 0;
}

static int decode_buffering_period(H264SEIBufferingPeriod *h, GetBitContext *gb,
                                   const H264ParamSets *ps, void *logctx)
{
    unsigned int sps_id;
    int sched_sel_idx;
    SPS *sps;

    sps_id = get_ue_golomb_31(gb);
    if (sps_id > 31 || !ps->sps_list[sps_id]) {
        av_log(logctx, AV_LOG_ERROR,
               "non-existing SPS %d referenced in buffering period\n", sps_id);
        return AVERROR_INVALIDDATA;
    }
    sps = (SPS*)ps->sps_list[sps_id]->data;

    // NOTE: This is really so duplicated in the standard... See H.264, D.1.1
    if (sps->nal_hrd_parameters_present_flag) {
        for (sched_sel_idx = 0; sched_sel_idx < sps->cpb_cnt; sched_sel_idx++) {
            h->initial_cpb_removal_delay[sched_sel_idx] =
                get_bits(gb, sps->initial_cpb_removal_delay_length);
            // initial_cpb_removal_delay_offset
            skip_bits(gb, sps->initial_cpb_removal_delay_length);
        }
    }
    if (sps->vcl_hrd_parameters_present_flag) {
        for (sched_sel_idx = 0; sched_sel_idx < sps->cpb_cnt; sched_sel_idx++) {
            h->initial_cpb_removal_delay[sched_sel_idx] =
                get_bits(gb, sps->initial_cpb_removal_delay_length);
            // initial_cpb_removal_delay_offset
            skip_bits(gb, sps->initial_cpb_removal_delay_length);
        }
    }

    h->present = 1;
    return 0;
}

static int decode_frame_packing_arrangement(H264SEIFramePacking *h,
                                            GetBitContext *gb)
{
    get_ue_golomb(gb);              // frame_packing_arrangement_id
    h->present = !get_bits1(gb);

    if (h->present) {
        h->arrangement_type = get_bits(gb, 7);
        h->quincunx_subsampling           = get_bits1(gb);
        h->content_interpretation_type    = get_bits(gb, 6);

        // the following skips: spatial_flipping_flag, frame0_flipped_flag,
        // field_views_flag, current_frame_is_frame0_flag,
        // frame0_self_contained_flag, frame1_self_contained_flag
        skip_bits(gb, 6);

        if (!h->quincunx_subsampling && h->arrangement_type != 5)
            skip_bits(gb, 16);      // frame[01]_grid_position_[xy]
        skip_bits(gb, 8);           // frame_packing_arrangement_reserved_byte
        get_ue_golomb(gb);          // frame_packing_arrangement_repetition_period
    }
    skip_bits1(gb);                 // frame_packing_arrangement_extension_flag

    return 0;
}

static int decode_display_orientation(H264SEIDisplayOrientation *h,
                                      GetBitContext *gb)
{
    h->present = !get_bits1(gb);

    if (h->present) {
        h->hflip = get_bits1(gb);     // hor_flip
        h->vflip = get_bits1(gb);     // ver_flip

        h->anticlockwise_rotation = get_bits(gb, 16);
        get_ue_golomb(gb);  // display_orientation_repetition_period
        skip_bits1(gb);     // display_orientation_extension_flag
    }

    return 0;
}

int ff_h264_sei_decode(H264SEIContext *h, GetBitContext *gb,
                       const H264ParamSets *ps, void *logctx)
{
    while (get_bits_left(gb) > 16) {
        int size = 0;
        int type = 0;
        int ret  = 0;
        int last = 0;

        while (get_bits_left(gb) >= 8 &&
               (last = get_bits(gb, 8)) == 255) {
            type += 255;
        }
        type += last;

        last = 0;
        while (get_bits_left(gb) >= 8 &&
               (last = get_bits(gb, 8)) == 255) {
            size += 255;
        }
        size += last;

        if (size > get_bits_left(gb) / 8) {
            av_log(logctx, AV_LOG_ERROR, "SEI type %d truncated at %d\n",
                   type, get_bits_left(gb));
            return AVERROR_INVALIDDATA;
        }

        switch (type) {
        case SEI_TYPE_PIC_TIMING: // Picture timing SEI
            ret = decode_picture_timing(&h->picture_timing, gb, ps->sps, logctx);
            break;
        case SEI_TYPE_USER_DATA_REGISTERED:
            ret = decode_registered_user_data(h, gb, logctx, size);
            break;
        case SEI_TYPE_USER_DATA_UNREGISTERED:
            ret = decode_unregistered_user_data(&h->unregistered, gb, logctx, size);
            break;
        case SEI_TYPE_RECOVERY_POINT:
            ret = decode_recovery_point(&h->recovery_point, gb);
            break;
        case SEI_TYPE_BUFFERING_PERIOD:
            ret = decode_buffering_period(&h->buffering_period, gb, ps, logctx);
            break;
        case SEI_TYPE_FRAME_PACKING:
            ret = decode_frame_packing_arrangement(&h->frame_packing, gb);
            break;
        case SEI_TYPE_DISPLAY_ORIENTATION:
            ret = decode_display_orientation(&h->display_orientation, gb);
            break;
        default:
            av_log(logctx, AV_LOG_DEBUG, "unknown SEI type %d\n", type);
            skip_bits(gb, 8 * size);
        }
        if (ret < 0)
            return ret;

        // FIXME check bits here
        align_get_bits(gb);
    }

    return 0;
}
