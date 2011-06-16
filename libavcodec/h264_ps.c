/*
 * H.26L/H.264/AVC/JVT/14496-10/... parameter set decoding
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
 * H.264 / AVC / MPEG4 part10 parameter set decoding.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "libavutil/imgutils.h"
#include "internal.h"
#include "dsputil.h"
#include "avcodec.h"
#include "h264.h"
#include "h264data.h" //FIXME FIXME FIXME (just for zigzag_scan)
#include "golomb.h"


//#undef NDEBUG
#include <assert.h>

static const AVRational pixel_aspect[17]={
 {0, 1},
 {1, 1},
 {12, 11},
 {10, 11},
 {16, 11},
 {40, 33},
 {24, 11},
 {20, 11},
 {32, 11},
 {80, 33},
 {18, 11},
 {15, 11},
 {64, 33},
 {160,99},
 {4, 3},
 {3, 2},
 {2, 1},
};

#define QP(qP,depth) ( (qP)+6*((depth)-8) )

#define CHROMA_QP_TABLE_END(d) \
     QP(0,d),  QP(1,d),  QP(2,d),  QP(3,d),  QP(4,d),  QP(5,d),\
     QP(6,d),  QP(7,d),  QP(8,d),  QP(9,d), QP(10,d), QP(11,d),\
    QP(12,d), QP(13,d), QP(14,d), QP(15,d), QP(16,d), QP(17,d),\
    QP(18,d), QP(19,d), QP(20,d), QP(21,d), QP(22,d), QP(23,d),\
    QP(24,d), QP(25,d), QP(26,d), QP(27,d), QP(28,d), QP(29,d),\
    QP(29,d), QP(30,d), QP(31,d), QP(32,d), QP(32,d), QP(33,d),\
    QP(34,d), QP(34,d), QP(35,d), QP(35,d), QP(36,d), QP(36,d),\
    QP(37,d), QP(37,d), QP(37,d), QP(38,d), QP(38,d), QP(38,d),\
    QP(39,d), QP(39,d), QP(39,d), QP(39,d)

const uint8_t ff_h264_chroma_qp[3][QP_MAX_NUM+1] = {
    {
        CHROMA_QP_TABLE_END(8)
    },
    {
        0, 1, 2, 3, 4, 5,
        CHROMA_QP_TABLE_END(9)
    },
    {
        0, 1, 2, 3,  4,  5,
        6, 7, 8, 9, 10, 11,
        CHROMA_QP_TABLE_END(10)
    },
};

static const uint8_t default_scaling4[2][16]={
{   6,13,20,28,
   13,20,28,32,
   20,28,32,37,
   28,32,37,42
},{
   10,14,20,24,
   14,20,24,27,
   20,24,27,30,
   24,27,30,34
}};

static const uint8_t default_scaling8[2][64]={
{   6,10,13,16,18,23,25,27,
   10,11,16,18,23,25,27,29,
   13,16,18,23,25,27,29,31,
   16,18,23,25,27,29,31,33,
   18,23,25,27,29,31,33,36,
   23,25,27,29,31,33,36,38,
   25,27,29,31,33,36,38,40,
   27,29,31,33,36,38,40,42
},{
    9,13,15,17,19,21,22,24,
   13,13,17,19,21,22,24,25,
   15,17,19,21,22,24,25,27,
   17,19,21,22,24,25,27,28,
   19,21,22,24,25,27,28,30,
   21,22,24,25,27,28,30,32,
   22,24,25,27,28,30,32,33,
   24,25,27,28,30,32,33,35
}};

static inline int decode_hrd_parameters(H264Context *h, SPS *sps){
    MpegEncContext * const s = &h->s;
    int cpb_count, i;
    cpb_count = get_ue_golomb_31(&s->gb) + 1;

    if(cpb_count > 32U){
        av_log(h->s.avctx, AV_LOG_ERROR, "cpb_count %d invalid\n", cpb_count);
        return -1;
    }

    get_bits(&s->gb, 4); /* bit_rate_scale */
    get_bits(&s->gb, 4); /* cpb_size_scale */
    for(i=0; i<cpb_count; i++){
        get_ue_golomb(&s->gb); /* bit_rate_value_minus1 */
        get_ue_golomb(&s->gb); /* cpb_size_value_minus1 */
        get_bits1(&s->gb);     /* cbr_flag */
    }
    sps->initial_cpb_removal_delay_length = get_bits(&s->gb, 5) + 1;
    sps->cpb_removal_delay_length = get_bits(&s->gb, 5) + 1;
    sps->dpb_output_delay_length = get_bits(&s->gb, 5) + 1;
    sps->time_offset_length = get_bits(&s->gb, 5);
    sps->cpb_cnt = cpb_count;
    return 0;
}

static inline int decode_vui_parameters(H264Context *h, SPS *sps){
    MpegEncContext * const s = &h->s;
    int aspect_ratio_info_present_flag;
    unsigned int aspect_ratio_idc;

    aspect_ratio_info_present_flag= get_bits1(&s->gb);

    if( aspect_ratio_info_present_flag ) {
        aspect_ratio_idc= get_bits(&s->gb, 8);
        if( aspect_ratio_idc == EXTENDED_SAR ) {
            sps->sar.num= get_bits(&s->gb, 16);
            sps->sar.den= get_bits(&s->gb, 16);
        }else if(aspect_ratio_idc < FF_ARRAY_ELEMS(pixel_aspect)){
            sps->sar=  pixel_aspect[aspect_ratio_idc];
        }else{
            av_log(h->s.avctx, AV_LOG_ERROR, "illegal aspect ratio\n");
            return -1;
        }
    }else{
        sps->sar.num=
        sps->sar.den= 0;
    }
//            s->avctx->aspect_ratio= sar_width*s->width / (float)(s->height*sar_height);

    if(get_bits1(&s->gb)){      /* overscan_info_present_flag */
        get_bits1(&s->gb);      /* overscan_appropriate_flag */
    }

    sps->video_signal_type_present_flag = get_bits1(&s->gb);
    if(sps->video_signal_type_present_flag){
        get_bits(&s->gb, 3);    /* video_format */
        sps->full_range = get_bits1(&s->gb); /* video_full_range_flag */

        sps->colour_description_present_flag = get_bits1(&s->gb);
        if(sps->colour_description_present_flag){
            sps->color_primaries = get_bits(&s->gb, 8); /* colour_primaries */
            sps->color_trc       = get_bits(&s->gb, 8); /* transfer_characteristics */
            sps->colorspace      = get_bits(&s->gb, 8); /* matrix_coefficients */
            if (sps->color_primaries >= AVCOL_PRI_NB)
                sps->color_primaries  = AVCOL_PRI_UNSPECIFIED;
            if (sps->color_trc >= AVCOL_TRC_NB)
                sps->color_trc  = AVCOL_TRC_UNSPECIFIED;
            if (sps->colorspace >= AVCOL_SPC_NB)
                sps->colorspace  = AVCOL_SPC_UNSPECIFIED;
        }
    }

    if(get_bits1(&s->gb)){      /* chroma_location_info_present_flag */
        s->avctx->chroma_sample_location = get_ue_golomb(&s->gb)+1;  /* chroma_sample_location_type_top_field */
        get_ue_golomb(&s->gb);  /* chroma_sample_location_type_bottom_field */
    }

    sps->timing_info_present_flag = get_bits1(&s->gb);
    if(sps->timing_info_present_flag){
        sps->num_units_in_tick = get_bits_long(&s->gb, 32);
        sps->time_scale = get_bits_long(&s->gb, 32);
        if(!sps->num_units_in_tick || !sps->time_scale){
            av_log(h->s.avctx, AV_LOG_ERROR, "time_scale/num_units_in_tick invalid or unsupported (%d/%d)\n", sps->time_scale, sps->num_units_in_tick);
            return -1;
        }
        sps->fixed_frame_rate_flag = get_bits1(&s->gb);
    }

    sps->nal_hrd_parameters_present_flag = get_bits1(&s->gb);
    if(sps->nal_hrd_parameters_present_flag)
        if(decode_hrd_parameters(h, sps) < 0)
            return -1;
    sps->vcl_hrd_parameters_present_flag = get_bits1(&s->gb);
    if(sps->vcl_hrd_parameters_present_flag)
        if(decode_hrd_parameters(h, sps) < 0)
            return -1;
    if(sps->nal_hrd_parameters_present_flag || sps->vcl_hrd_parameters_present_flag)
        get_bits1(&s->gb);     /* low_delay_hrd_flag */
    sps->pic_struct_present_flag = get_bits1(&s->gb);

    sps->bitstream_restriction_flag = get_bits1(&s->gb);
    if(sps->bitstream_restriction_flag){
        get_bits1(&s->gb);     /* motion_vectors_over_pic_boundaries_flag */
        get_ue_golomb(&s->gb); /* max_bytes_per_pic_denom */
        get_ue_golomb(&s->gb); /* max_bits_per_mb_denom */
        get_ue_golomb(&s->gb); /* log2_max_mv_length_horizontal */
        get_ue_golomb(&s->gb); /* log2_max_mv_length_vertical */
        sps->num_reorder_frames= get_ue_golomb(&s->gb);
        get_ue_golomb(&s->gb); /*max_dec_frame_buffering*/

        if(s->gb.size_in_bits < get_bits_count(&s->gb)){
            av_log(h->s.avctx, AV_LOG_ERROR, "Overread VUI by %d bits\n", get_bits_count(&s->gb) - s->gb.size_in_bits);
            sps->num_reorder_frames=0;
            sps->bitstream_restriction_flag= 0;
        }

        if(sps->num_reorder_frames > 16U /*max_dec_frame_buffering || max_dec_frame_buffering > 16*/){
            av_log(h->s.avctx, AV_LOG_ERROR, "illegal num_reorder_frames %d\n", sps->num_reorder_frames);
            return -1;
        }
    }

    return 0;
}

static void decode_scaling_list(H264Context *h, uint8_t *factors, int size,
                                const uint8_t *jvt_list, const uint8_t *fallback_list){
    MpegEncContext * const s = &h->s;
    int i, last = 8, next = 8;
    const uint8_t *scan = size == 16 ? zigzag_scan : ff_zigzag_direct;
    if(!get_bits1(&s->gb)) /* matrix not written, we use the predicted one */
        memcpy(factors, fallback_list, size*sizeof(uint8_t));
    else
    for(i=0;i<size;i++){
        if(next)
            next = (last + get_se_golomb(&s->gb)) & 0xff;
        if(!i && !next){ /* matrix not written, we use the preset one */
            memcpy(factors, jvt_list, size*sizeof(uint8_t));
            break;
        }
        last = factors[scan[i]] = next ? next : last;
    }
}

static void decode_scaling_matrices(H264Context *h, SPS *sps, PPS *pps, int is_sps,
                                   uint8_t (*scaling_matrix4)[16], uint8_t (*scaling_matrix8)[64]){
    MpegEncContext * const s = &h->s;
    int fallback_sps = !is_sps && sps->scaling_matrix_present;
    const uint8_t *fallback[4] = {
        fallback_sps ? sps->scaling_matrix4[0] : default_scaling4[0],
        fallback_sps ? sps->scaling_matrix4[3] : default_scaling4[1],
        fallback_sps ? sps->scaling_matrix8[0] : default_scaling8[0],
        fallback_sps ? sps->scaling_matrix8[3] : default_scaling8[1]
    };
    if(get_bits1(&s->gb)){
        sps->scaling_matrix_present |= is_sps;
        decode_scaling_list(h,scaling_matrix4[0],16,default_scaling4[0],fallback[0]); // Intra, Y
        decode_scaling_list(h,scaling_matrix4[1],16,default_scaling4[0],scaling_matrix4[0]); // Intra, Cr
        decode_scaling_list(h,scaling_matrix4[2],16,default_scaling4[0],scaling_matrix4[1]); // Intra, Cb
        decode_scaling_list(h,scaling_matrix4[3],16,default_scaling4[1],fallback[1]); // Inter, Y
        decode_scaling_list(h,scaling_matrix4[4],16,default_scaling4[1],scaling_matrix4[3]); // Inter, Cr
        decode_scaling_list(h,scaling_matrix4[5],16,default_scaling4[1],scaling_matrix4[4]); // Inter, Cb
        if(is_sps || pps->transform_8x8_mode){
            decode_scaling_list(h,scaling_matrix8[0],64,default_scaling8[0],fallback[2]);  // Intra, Y
            if(h->sps.chroma_format_idc == 3){
                decode_scaling_list(h,scaling_matrix8[1],64,default_scaling8[0],scaling_matrix8[0]);  // Intra, Cr
                decode_scaling_list(h,scaling_matrix8[2],64,default_scaling8[0],scaling_matrix8[1]);  // Intra, Cb
            }
            decode_scaling_list(h,scaling_matrix8[3],64,default_scaling8[1],fallback[3]);  // Inter, Y
            if(h->sps.chroma_format_idc == 3){
                decode_scaling_list(h,scaling_matrix8[4],64,default_scaling8[1],scaling_matrix8[3]);  // Inter, Cr
                decode_scaling_list(h,scaling_matrix8[5],64,default_scaling8[1],scaling_matrix8[4]);  // Inter, Cb
            }
        }
    }
}

int ff_h264_decode_seq_parameter_set(H264Context *h){
    MpegEncContext * const s = &h->s;
    int profile_idc, level_idc, constraint_set_flags = 0;
    unsigned int sps_id;
    int i;
    SPS *sps;

    profile_idc= get_bits(&s->gb, 8);
    constraint_set_flags |= get_bits1(&s->gb) << 0;   //constraint_set0_flag
    constraint_set_flags |= get_bits1(&s->gb) << 1;   //constraint_set1_flag
    constraint_set_flags |= get_bits1(&s->gb) << 2;   //constraint_set2_flag
    constraint_set_flags |= get_bits1(&s->gb) << 3;   //constraint_set3_flag
    get_bits(&s->gb, 4); // reserved
    level_idc= get_bits(&s->gb, 8);
    sps_id= get_ue_golomb_31(&s->gb);

    if(sps_id >= MAX_SPS_COUNT) {
        av_log(h->s.avctx, AV_LOG_ERROR, "sps_id (%d) out of range\n", sps_id);
        return -1;
    }
    sps= av_mallocz(sizeof(SPS));
    if(sps == NULL)
        return -1;

    sps->time_offset_length = 24;
    sps->profile_idc= profile_idc;
    sps->constraint_set_flags = constraint_set_flags;
    sps->level_idc= level_idc;

    memset(sps->scaling_matrix4, 16, sizeof(sps->scaling_matrix4));
    memset(sps->scaling_matrix8, 16, sizeof(sps->scaling_matrix8));
    sps->scaling_matrix_present = 0;

    if(sps->profile_idc >= 100){ //high profile
        sps->chroma_format_idc= get_ue_golomb_31(&s->gb);
        if(sps->chroma_format_idc == 3)
            sps->residual_color_transform_flag = get_bits1(&s->gb);
        sps->bit_depth_luma   = get_ue_golomb(&s->gb) + 8;
        sps->bit_depth_chroma = get_ue_golomb(&s->gb) + 8;
        sps->transform_bypass = get_bits1(&s->gb);
        decode_scaling_matrices(h, sps, NULL, 1, sps->scaling_matrix4, sps->scaling_matrix8);
    }else{
        sps->chroma_format_idc= 1;
        sps->bit_depth_luma   = 8;
        sps->bit_depth_chroma = 8;
    }

    sps->log2_max_frame_num= get_ue_golomb(&s->gb) + 4;
    sps->poc_type= get_ue_golomb_31(&s->gb);

    if(sps->poc_type == 0){ //FIXME #define
        sps->log2_max_poc_lsb= get_ue_golomb(&s->gb) + 4;
    } else if(sps->poc_type == 1){//FIXME #define
        sps->delta_pic_order_always_zero_flag= get_bits1(&s->gb);
        sps->offset_for_non_ref_pic= get_se_golomb(&s->gb);
        sps->offset_for_top_to_bottom_field= get_se_golomb(&s->gb);
        sps->poc_cycle_length                = get_ue_golomb(&s->gb);

        if((unsigned)sps->poc_cycle_length >= FF_ARRAY_ELEMS(sps->offset_for_ref_frame)){
            av_log(h->s.avctx, AV_LOG_ERROR, "poc_cycle_length overflow %u\n", sps->poc_cycle_length);
            goto fail;
        }

        for(i=0; i<sps->poc_cycle_length; i++)
            sps->offset_for_ref_frame[i]= get_se_golomb(&s->gb);
    }else if(sps->poc_type != 2){
        av_log(h->s.avctx, AV_LOG_ERROR, "illegal POC type %d\n", sps->poc_type);
        goto fail;
    }

    sps->ref_frame_count= get_ue_golomb_31(&s->gb);
    if(sps->ref_frame_count > MAX_PICTURE_COUNT-2 || sps->ref_frame_count >= 32U){
        av_log(h->s.avctx, AV_LOG_ERROR, "too many reference frames\n");
        goto fail;
    }
    sps->gaps_in_frame_num_allowed_flag= get_bits1(&s->gb);
    sps->mb_width = get_ue_golomb(&s->gb) + 1;
    sps->mb_height= get_ue_golomb(&s->gb) + 1;
    if((unsigned)sps->mb_width >= INT_MAX/16 || (unsigned)sps->mb_height >= INT_MAX/16 ||
       av_image_check_size(16*sps->mb_width, 16*sps->mb_height, 0, h->s.avctx)){
        av_log(h->s.avctx, AV_LOG_ERROR, "mb_width/height overflow\n");
        goto fail;
    }

    sps->frame_mbs_only_flag= get_bits1(&s->gb);
    if(!sps->frame_mbs_only_flag)
        sps->mb_aff= get_bits1(&s->gb);
    else
        sps->mb_aff= 0;

    sps->direct_8x8_inference_flag= get_bits1(&s->gb);
    if(!sps->frame_mbs_only_flag && !sps->direct_8x8_inference_flag){
        av_log(h->s.avctx, AV_LOG_ERROR, "This stream was generated by a broken encoder, invalid 8x8 inference\n");
        goto fail;
    }

#ifndef ALLOW_INTERLACE
    if(sps->mb_aff)
        av_log(h->s.avctx, AV_LOG_ERROR, "MBAFF support not included; enable it at compile-time.\n");
#endif
    sps->crop= get_bits1(&s->gb);
    if(sps->crop){
        sps->crop_left  = get_ue_golomb(&s->gb);
        sps->crop_right = get_ue_golomb(&s->gb);
        sps->crop_top   = get_ue_golomb(&s->gb);
        sps->crop_bottom= get_ue_golomb(&s->gb);
        if(sps->crop_left || sps->crop_top){
            av_log(h->s.avctx, AV_LOG_ERROR, "insane cropping not completely supported, this could look slightly wrong ...\n");
        }
        if(sps->crop_right >= (8<<CHROMA444) || sps->crop_bottom >= (8<<CHROMA444)){
            av_log(h->s.avctx, AV_LOG_ERROR, "brainfart cropping not supported, this could look slightly wrong ...\n");
        }
    }else{
        sps->crop_left  =
        sps->crop_right =
        sps->crop_top   =
        sps->crop_bottom= 0;
    }

    sps->vui_parameters_present_flag= get_bits1(&s->gb);
    if( sps->vui_parameters_present_flag )
        if (decode_vui_parameters(h, sps) < 0)
            goto fail;

    if(!sps->sar.den)
        sps->sar.den= 1;

    if(s->avctx->debug&FF_DEBUG_PICT_INFO){
        av_log(h->s.avctx, AV_LOG_DEBUG, "sps:%u profile:%d/%d poc:%d ref:%d %dx%d %s %s crop:%d/%d/%d/%d %s %s %d/%d b%d\n",
               sps_id, sps->profile_idc, sps->level_idc,
               sps->poc_type,
               sps->ref_frame_count,
               sps->mb_width, sps->mb_height,
               sps->frame_mbs_only_flag ? "FRM" : (sps->mb_aff ? "MB-AFF" : "PIC-AFF"),
               sps->direct_8x8_inference_flag ? "8B8" : "",
               sps->crop_left, sps->crop_right,
               sps->crop_top, sps->crop_bottom,
               sps->vui_parameters_present_flag ? "VUI" : "",
               ((const char*[]){"Gray","420","422","444"})[sps->chroma_format_idc],
               sps->timing_info_present_flag ? sps->num_units_in_tick : 0,
               sps->timing_info_present_flag ? sps->time_scale : 0,
               sps->bit_depth_luma
               );
    }

    av_free(h->sps_buffers[sps_id]);
    h->sps_buffers[sps_id]= sps;
    h->sps = *sps;
    return 0;
fail:
    av_free(sps);
    return -1;
}

static void
build_qp_table(PPS *pps, int t, int index, const int depth)
{
    int i;
    const int max_qp = 51 + 6*(depth-8);
    for(i = 0; i < max_qp+1; i++)
        pps->chroma_qp_table[t][i] = ff_h264_chroma_qp[depth-8][av_clip(i + index, 0, max_qp)];
}

int ff_h264_decode_picture_parameter_set(H264Context *h, int bit_length){
    MpegEncContext * const s = &h->s;
    unsigned int pps_id= get_ue_golomb(&s->gb);
    PPS *pps;
    const int qp_bd_offset = 6*(h->sps.bit_depth_luma-8);

    if(pps_id >= MAX_PPS_COUNT) {
        av_log(h->s.avctx, AV_LOG_ERROR, "pps_id (%d) out of range\n", pps_id);
        return -1;
    }

    pps= av_mallocz(sizeof(PPS));
    if(pps == NULL)
        return -1;
    pps->sps_id= get_ue_golomb_31(&s->gb);
    if((unsigned)pps->sps_id>=MAX_SPS_COUNT || h->sps_buffers[pps->sps_id] == NULL){
        av_log(h->s.avctx, AV_LOG_ERROR, "sps_id out of range\n");
        goto fail;
    }

    pps->cabac= get_bits1(&s->gb);
    pps->pic_order_present= get_bits1(&s->gb);
    pps->slice_group_count= get_ue_golomb(&s->gb) + 1;
    if(pps->slice_group_count > 1 ){
        pps->mb_slice_group_map_type= get_ue_golomb(&s->gb);
        av_log(h->s.avctx, AV_LOG_ERROR, "FMO not supported\n");
        switch(pps->mb_slice_group_map_type){
        case 0:
#if 0
|   for( i = 0; i <= num_slice_groups_minus1; i++ ) |   |        |
|    run_length[ i ]                                |1  |ue(v)   |
#endif
            break;
        case 2:
#if 0
|   for( i = 0; i < num_slice_groups_minus1; i++ )  |   |        |
|{                                                  |   |        |
|    top_left_mb[ i ]                               |1  |ue(v)   |
|    bottom_right_mb[ i ]                           |1  |ue(v)   |
|   }                                               |   |        |
#endif
            break;
        case 3:
        case 4:
        case 5:
#if 0
|   slice_group_change_direction_flag               |1  |u(1)    |
|   slice_group_change_rate_minus1                  |1  |ue(v)   |
#endif
            break;
        case 6:
#if 0
|   slice_group_id_cnt_minus1                       |1  |ue(v)   |
|   for( i = 0; i <= slice_group_id_cnt_minus1; i++ |   |        |
|)                                                  |   |        |
|    slice_group_id[ i ]                            |1  |u(v)    |
#endif
            break;
        }
    }
    pps->ref_count[0]= get_ue_golomb(&s->gb) + 1;
    pps->ref_count[1]= get_ue_golomb(&s->gb) + 1;
    if(pps->ref_count[0]-1 > 32-1 || pps->ref_count[1]-1 > 32-1){
        av_log(h->s.avctx, AV_LOG_ERROR, "reference overflow (pps)\n");
        goto fail;
    }

    pps->weighted_pred= get_bits1(&s->gb);
    pps->weighted_bipred_idc= get_bits(&s->gb, 2);
    pps->init_qp= get_se_golomb(&s->gb) + 26 + qp_bd_offset;
    pps->init_qs= get_se_golomb(&s->gb) + 26 + qp_bd_offset;
    pps->chroma_qp_index_offset[0]= get_se_golomb(&s->gb);
    pps->deblocking_filter_parameters_present= get_bits1(&s->gb);
    pps->constrained_intra_pred= get_bits1(&s->gb);
    pps->redundant_pic_cnt_present = get_bits1(&s->gb);

    pps->transform_8x8_mode= 0;
    h->dequant_coeff_pps= -1; //contents of sps/pps can change even if id doesn't, so reinit
    memcpy(pps->scaling_matrix4, h->sps_buffers[pps->sps_id]->scaling_matrix4, sizeof(pps->scaling_matrix4));
    memcpy(pps->scaling_matrix8, h->sps_buffers[pps->sps_id]->scaling_matrix8, sizeof(pps->scaling_matrix8));

    if(get_bits_count(&s->gb) < bit_length){
        pps->transform_8x8_mode= get_bits1(&s->gb);
        decode_scaling_matrices(h, h->sps_buffers[pps->sps_id], pps, 0, pps->scaling_matrix4, pps->scaling_matrix8);
        pps->chroma_qp_index_offset[1]= get_se_golomb(&s->gb); //second_chroma_qp_index_offset
    } else {
        pps->chroma_qp_index_offset[1]= pps->chroma_qp_index_offset[0];
    }

    build_qp_table(pps, 0, pps->chroma_qp_index_offset[0], h->sps.bit_depth_luma);
    build_qp_table(pps, 1, pps->chroma_qp_index_offset[1], h->sps.bit_depth_luma);
    if(pps->chroma_qp_index_offset[0] != pps->chroma_qp_index_offset[1])
        pps->chroma_qp_diff= 1;

    if(s->avctx->debug&FF_DEBUG_PICT_INFO){
        av_log(h->s.avctx, AV_LOG_DEBUG, "pps:%u sps:%u %s slice_groups:%d ref:%d/%d %s qp:%d/%d/%d/%d %s %s %s %s\n",
               pps_id, pps->sps_id,
               pps->cabac ? "CABAC" : "CAVLC",
               pps->slice_group_count,
               pps->ref_count[0], pps->ref_count[1],
               pps->weighted_pred ? "weighted" : "",
               pps->init_qp, pps->init_qs, pps->chroma_qp_index_offset[0], pps->chroma_qp_index_offset[1],
               pps->deblocking_filter_parameters_present ? "LPAR" : "",
               pps->constrained_intra_pred ? "CONSTR" : "",
               pps->redundant_pic_cnt_present ? "REDU" : "",
               pps->transform_8x8_mode ? "8x8DCT" : ""
               );
    }

    av_free(h->pps_buffers[pps_id]);
    h->pps_buffers[pps_id]= pps;
    return 0;
fail:
    av_free(pps);
    return -1;
}
