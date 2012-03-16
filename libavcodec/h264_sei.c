/*
 * H.26L/H.264/AVC/JVT/14496-10/... sei decoding
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * H.264 / AVC / MPEG4 part10 sei decoding.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "internal.h"
#include "avcodec.h"
#include "h264.h"
#include "golomb.h"

//#undef NDEBUG
#include <assert.h>

static const uint8_t sei_num_clock_ts_table[9]={
    1,  1,  1,  2,  2,  3,  3,  2,  3
};

void ff_h264_reset_sei(H264Context *h) {
    h->sei_recovery_frame_cnt       = -1;
    h->sei_dpb_output_delay         =  0;
    h->sei_cpb_removal_delay        = -1;
    h->sei_buffering_period_present =  0;
}

static int decode_picture_timing(H264Context *h){
    MpegEncContext * const s = &h->s;
    if(h->sps.nal_hrd_parameters_present_flag || h->sps.vcl_hrd_parameters_present_flag){
        h->sei_cpb_removal_delay = get_bits(&s->gb, h->sps.cpb_removal_delay_length);
        h->sei_dpb_output_delay = get_bits(&s->gb, h->sps.dpb_output_delay_length);
    }
    if(h->sps.pic_struct_present_flag){
        unsigned int i, num_clock_ts;
        h->sei_pic_struct = get_bits(&s->gb, 4);
        h->sei_ct_type    = 0;

        if (h->sei_pic_struct > SEI_PIC_STRUCT_FRAME_TRIPLING)
            return -1;

        num_clock_ts = sei_num_clock_ts_table[h->sei_pic_struct];

        for (i = 0 ; i < num_clock_ts ; i++){
            if(get_bits(&s->gb, 1)){                  /* clock_timestamp_flag */
                unsigned int full_timestamp_flag;
                h->sei_ct_type |= 1<<get_bits(&s->gb, 2);
                skip_bits(&s->gb, 1);                 /* nuit_field_based_flag */
                skip_bits(&s->gb, 5);                 /* counting_type */
                full_timestamp_flag = get_bits(&s->gb, 1);
                skip_bits(&s->gb, 1);                 /* discontinuity_flag */
                skip_bits(&s->gb, 1);                 /* cnt_dropped_flag */
                skip_bits(&s->gb, 8);                 /* n_frames */
                if(full_timestamp_flag){
                    skip_bits(&s->gb, 6);             /* seconds_value 0..59 */
                    skip_bits(&s->gb, 6);             /* minutes_value 0..59 */
                    skip_bits(&s->gb, 5);             /* hours_value 0..23 */
                }else{
                    if(get_bits(&s->gb, 1)){          /* seconds_flag */
                        skip_bits(&s->gb, 6);         /* seconds_value range 0..59 */
                        if(get_bits(&s->gb, 1)){      /* minutes_flag */
                            skip_bits(&s->gb, 6);     /* minutes_value 0..59 */
                            if(get_bits(&s->gb, 1))   /* hours_flag */
                                skip_bits(&s->gb, 5); /* hours_value 0..23 */
                        }
                    }
                }
                if(h->sps.time_offset_length > 0)
                    skip_bits(&s->gb, h->sps.time_offset_length); /* time_offset */
            }
        }

        if(s->avctx->debug & FF_DEBUG_PICT_INFO)
            av_log(s->avctx, AV_LOG_DEBUG, "ct_type:%X pic_struct:%d\n", h->sei_ct_type, h->sei_pic_struct);
    }
    return 0;
}

static int decode_unregistered_user_data(H264Context *h, int size){
    MpegEncContext * const s = &h->s;
    uint8_t user_data[16+256];
    int e, build, i;

    if(size<16)
        return -1;

    for(i=0; i<sizeof(user_data)-1 && i<size; i++){
        user_data[i]= get_bits(&s->gb, 8);
    }

    user_data[i]= 0;
    e= sscanf(user_data+16, "x264 - core %d"/*%s - H.264/MPEG-4 AVC codec - Copyleft 2005 - http://www.videolan.org/x264.html*/, &build);
    if(e==1 && build>0)
        h->x264_build= build;

    if(s->avctx->debug & FF_DEBUG_BUGS)
        av_log(s->avctx, AV_LOG_DEBUG, "user data:\"%s\"\n", user_data+16);

    for(; i<size; i++)
        skip_bits(&s->gb, 8);

    return 0;
}

static int decode_recovery_point(H264Context *h){
    MpegEncContext * const s = &h->s;

    h->sei_recovery_frame_cnt = get_ue_golomb(&s->gb);
    skip_bits(&s->gb, 4);       /* 1b exact_match_flag, 1b broken_link_flag, 2b changing_slice_group_idc */

    return 0;
}

static int decode_buffering_period(H264Context *h){
    MpegEncContext * const s = &h->s;
    unsigned int sps_id;
    int sched_sel_idx;
    SPS *sps;

    sps_id = get_ue_golomb_31(&s->gb);
    if(sps_id > 31 || !h->sps_buffers[sps_id]) {
        av_log(h->s.avctx, AV_LOG_ERROR, "non-existing SPS %d referenced in buffering period\n", sps_id);
        return -1;
    }
    sps = h->sps_buffers[sps_id];

    // NOTE: This is really so duplicated in the standard... See H.264, D.1.1
    if (sps->nal_hrd_parameters_present_flag) {
        for (sched_sel_idx = 0; sched_sel_idx < sps->cpb_cnt; sched_sel_idx++) {
            h->initial_cpb_removal_delay[sched_sel_idx] = get_bits(&s->gb, sps->initial_cpb_removal_delay_length);
            skip_bits(&s->gb, sps->initial_cpb_removal_delay_length); // initial_cpb_removal_delay_offset
        }
    }
    if (sps->vcl_hrd_parameters_present_flag) {
        for (sched_sel_idx = 0; sched_sel_idx < sps->cpb_cnt; sched_sel_idx++) {
            h->initial_cpb_removal_delay[sched_sel_idx] = get_bits(&s->gb, sps->initial_cpb_removal_delay_length);
            skip_bits(&s->gb, sps->initial_cpb_removal_delay_length); // initial_cpb_removal_delay_offset
        }
    }

    h->sei_buffering_period_present = 1;
    return 0;
}

int ff_h264_decode_sei(H264Context *h){
    MpegEncContext * const s = &h->s;

    while (get_bits_left(&s->gb) > 16) {
        int size, type;

        type=0;
        do{
            if (get_bits_left(&s->gb) < 8)
                return -1;
            type+= show_bits(&s->gb, 8);
        }while(get_bits(&s->gb, 8) == 255);

        size=0;
        do{
            if (get_bits_left(&s->gb) < 8)
                return -1;
            size+= show_bits(&s->gb, 8);
        }while(get_bits(&s->gb, 8) == 255);

        if(s->avctx->debug&FF_DEBUG_STARTCODE)
            av_log(h->s.avctx, AV_LOG_DEBUG, "SEI %d len:%d\n", type, size);

        switch(type){
        case SEI_TYPE_PIC_TIMING: // Picture timing SEI
            if(decode_picture_timing(h) < 0)
                return -1;
            break;
        case SEI_TYPE_USER_DATA_UNREGISTERED:
            if(decode_unregistered_user_data(h, size) < 0)
                return -1;
            break;
        case SEI_TYPE_RECOVERY_POINT:
            if(decode_recovery_point(h) < 0)
                return -1;
            break;
        case SEI_BUFFERING_PERIOD:
            if(decode_buffering_period(h) < 0)
                return -1;
            break;
        default:
            skip_bits(&s->gb, 8*size);
        }

        //FIXME check bits here
        align_get_bits(&s->gb);
    }

    return 0;
}
