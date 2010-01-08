/*
 * H263/MPEG4 backend for ffmpeg encoder and decoder
 * Copyright (c) 2000,2001 Fabrice Bellard
 * H263+ support.
 * Copyright (c) 2001 Juan J. Sierralta P
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * ac prediction encoding, B-frame support, error resilience, optimizations,
 * qpel decoding, gmc decoding, interlaced decoding
 * by Michael Niedermayer <michaelni@gmx.at>
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
 * @file libavcodec/h263.c
 * h263/mpeg4 codec.
 */

//#define DEBUG
#include <limits.h>

#include "dsputil.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "h263.h"
#include "h263data.h"
#include "mathops.h"
#include "unary.h"
#include "flv.h"
#include "mpeg4video.h"

//#undef NDEBUG
//#include <assert.h>

// The defines below define the number of bits that are read at once for
// reading vlc values. Changing these may improve speed and data cache needs
// be aware though that decreasing them may need the number of stages that is
// passed to get_vlc* to be increased.
#define MV_VLC_BITS 9
#define H263_MBTYPE_B_VLC_BITS 6
#define CBPC_B_VLC_BITS 3


#if CONFIG_ENCODERS
/**
 * Table of number of bits a motion vector component needs.
 */
static uint8_t mv_penalty[MAX_FCODE+1][MAX_MV*2+1];

/**
 * Minimal fcode that a motion vector component would need.
 */
static uint8_t fcode_tab[MAX_MV*2+1];

/**
 * Minimal fcode that a motion vector component would need in umv.
 * All entries in this table are 1.
 */
static uint8_t umv_fcode_tab[MAX_MV*2+1];

//unified encoding tables for run length encoding of coefficients
//unified in the sense that the specification specifies the encoding in several steps.
static uint8_t  uni_h263_intra_aic_rl_len [64*64*2*2];
static uint8_t  uni_h263_inter_rl_len [64*64*2*2];
//#define UNI_MPEG4_ENC_INDEX(last,run,level) ((last)*128 + (run)*256 + (level))
//#define UNI_MPEG4_ENC_INDEX(last,run,level) ((last)*128*64 + (run) + (level)*64)
#define UNI_MPEG4_ENC_INDEX(last,run,level) ((last)*128*64 + (run)*128 + (level))

#endif

static uint8_t static_rl_table_store[2][2][2*MAX_RUN + MAX_LEVEL + 3];


int h263_get_picture_format(int width, int height)
{
    if (width == 128 && height == 96)
        return 1;
    else if (width == 176 && height == 144)
        return 2;
    else if (width == 352 && height == 288)
        return 3;
    else if (width == 704 && height == 576)
        return 4;
    else if (width == 1408 && height == 1152)
        return 5;
    else
        return 7;
}

void ff_h263_show_pict_info(MpegEncContext *s){
    if(s->avctx->debug&FF_DEBUG_PICT_INFO){
    av_log(s->avctx, AV_LOG_DEBUG, "qp:%d %c size:%d rnd:%d%s%s%s%s%s%s%s%s%s %d/%d\n",
         s->qscale, av_get_pict_type_char(s->pict_type),
         s->gb.size_in_bits, 1-s->no_rounding,
         s->obmc ? " AP" : "",
         s->umvplus ? " UMV" : "",
         s->h263_long_vectors ? " LONG" : "",
         s->h263_plus ? " +" : "",
         s->h263_aic ? " AIC" : "",
         s->alt_inter_vlc ? " AIV" : "",
         s->modified_quant ? " MQ" : "",
         s->loop_filter ? " LOOP" : "",
         s->h263_slice_structured ? " SS" : "",
         s->avctx->time_base.den, s->avctx->time_base.num
    );
    }
}

#if CONFIG_ENCODERS

/**
 * Returns the 4 bit value that specifies the given aspect ratio.
 * This may be one of the standard aspect ratios or it specifies
 * that the aspect will be stored explicitly later.
 */
av_const int ff_h263_aspect_to_info(AVRational aspect){
    int i;

    if(aspect.num==0) aspect= (AVRational){1,1};

    for(i=1; i<6; i++){
        if(av_cmp_q(ff_h263_pixel_aspect[i], aspect) == 0){
            return i;
        }
    }

    return FF_ASPECT_EXTENDED;
}

void h263_encode_picture_header(MpegEncContext * s, int picture_number)
{
    int format, coded_frame_rate, coded_frame_rate_base, i, temp_ref;
    int best_clock_code=1;
    int best_divisor=60;
    int best_error= INT_MAX;

    if(s->h263_plus){
        for(i=0; i<2; i++){
            int div, error;
            div= (s->avctx->time_base.num*1800000LL + 500LL*s->avctx->time_base.den) / ((1000LL+i)*s->avctx->time_base.den);
            div= av_clip(div, 1, 127);
            error= FFABS(s->avctx->time_base.num*1800000LL - (1000LL+i)*s->avctx->time_base.den*div);
            if(error < best_error){
                best_error= error;
                best_divisor= div;
                best_clock_code= i;
            }
        }
    }
    s->custom_pcf= best_clock_code!=1 || best_divisor!=60;
    coded_frame_rate= 1800000;
    coded_frame_rate_base= (1000+best_clock_code)*best_divisor;

    align_put_bits(&s->pb);

    /* Update the pointer to last GOB */
    s->ptr_lastgob = put_bits_ptr(&s->pb);
    put_bits(&s->pb, 22, 0x20); /* PSC */
    temp_ref= s->picture_number * (int64_t)coded_frame_rate * s->avctx->time_base.num / //FIXME use timestamp
                         (coded_frame_rate_base * (int64_t)s->avctx->time_base.den);
    put_sbits(&s->pb, 8, temp_ref); /* TemporalReference */

    put_bits(&s->pb, 1, 1);     /* marker */
    put_bits(&s->pb, 1, 0);     /* h263 id */
    put_bits(&s->pb, 1, 0);     /* split screen off */
    put_bits(&s->pb, 1, 0);     /* camera  off */
    put_bits(&s->pb, 1, 0);     /* freeze picture release off */

    format = h263_get_picture_format(s->width, s->height);
    if (!s->h263_plus) {
        /* H.263v1 */
        put_bits(&s->pb, 3, format);
        put_bits(&s->pb, 1, (s->pict_type == FF_P_TYPE));
        /* By now UMV IS DISABLED ON H.263v1, since the restrictions
        of H.263v1 UMV implies to check the predicted MV after
        calculation of the current MB to see if we're on the limits */
        put_bits(&s->pb, 1, 0);         /* Unrestricted Motion Vector: off */
        put_bits(&s->pb, 1, 0);         /* SAC: off */
        put_bits(&s->pb, 1, s->obmc);   /* Advanced Prediction */
        put_bits(&s->pb, 1, 0);         /* only I/P frames, no PB frame */
        put_bits(&s->pb, 5, s->qscale);
        put_bits(&s->pb, 1, 0);         /* Continuous Presence Multipoint mode: off */
    } else {
        int ufep=1;
        /* H.263v2 */
        /* H.263 Plus PTYPE */

        put_bits(&s->pb, 3, 7);
        put_bits(&s->pb,3,ufep); /* Update Full Extended PTYPE */
        if (format == 7)
            put_bits(&s->pb,3,6); /* Custom Source Format */
        else
            put_bits(&s->pb, 3, format);

        put_bits(&s->pb,1, s->custom_pcf);
        put_bits(&s->pb,1, s->umvplus); /* Unrestricted Motion Vector */
        put_bits(&s->pb,1,0); /* SAC: off */
        put_bits(&s->pb,1,s->obmc); /* Advanced Prediction Mode */
        put_bits(&s->pb,1,s->h263_aic); /* Advanced Intra Coding */
        put_bits(&s->pb,1,s->loop_filter); /* Deblocking Filter */
        put_bits(&s->pb,1,s->h263_slice_structured); /* Slice Structured */
        put_bits(&s->pb,1,0); /* Reference Picture Selection: off */
        put_bits(&s->pb,1,0); /* Independent Segment Decoding: off */
        put_bits(&s->pb,1,s->alt_inter_vlc); /* Alternative Inter VLC */
        put_bits(&s->pb,1,s->modified_quant); /* Modified Quantization: */
        put_bits(&s->pb,1,1); /* "1" to prevent start code emulation */
        put_bits(&s->pb,3,0); /* Reserved */

        put_bits(&s->pb, 3, s->pict_type == FF_P_TYPE);

        put_bits(&s->pb,1,0); /* Reference Picture Resampling: off */
        put_bits(&s->pb,1,0); /* Reduced-Resolution Update: off */
        put_bits(&s->pb,1,s->no_rounding); /* Rounding Type */
        put_bits(&s->pb,2,0); /* Reserved */
        put_bits(&s->pb,1,1); /* "1" to prevent start code emulation */

        /* This should be here if PLUSPTYPE */
        put_bits(&s->pb, 1, 0); /* Continuous Presence Multipoint mode: off */

                if (format == 7) {
            /* Custom Picture Format (CPFMT) */
            s->aspect_ratio_info= ff_h263_aspect_to_info(s->avctx->sample_aspect_ratio);

            put_bits(&s->pb,4,s->aspect_ratio_info);
            put_bits(&s->pb,9,(s->width >> 2) - 1);
            put_bits(&s->pb,1,1); /* "1" to prevent start code emulation */
            put_bits(&s->pb,9,(s->height >> 2));
            if (s->aspect_ratio_info == FF_ASPECT_EXTENDED){
                put_bits(&s->pb, 8, s->avctx->sample_aspect_ratio.num);
                put_bits(&s->pb, 8, s->avctx->sample_aspect_ratio.den);
            }
        }
        if(s->custom_pcf){
            if(ufep){
                put_bits(&s->pb, 1, best_clock_code);
                put_bits(&s->pb, 7, best_divisor);
            }
            put_sbits(&s->pb, 2, temp_ref>>8);
        }

        /* Unlimited Unrestricted Motion Vectors Indicator (UUI) */
        if (s->umvplus)
//            put_bits(&s->pb,1,1); /* Limited according tables of Annex D */
//FIXME check actual requested range
            put_bits(&s->pb,2,1); /* unlimited */
        if(s->h263_slice_structured)
            put_bits(&s->pb,2,0); /* no weird submodes */

        put_bits(&s->pb, 5, s->qscale);
    }

    put_bits(&s->pb, 1, 0);     /* no PEI */

    if(s->h263_slice_structured){
        put_bits(&s->pb, 1, 1);

        assert(s->mb_x == 0 && s->mb_y == 0);
        ff_h263_encode_mba(s);

        put_bits(&s->pb, 1, 1);
    }

    if(s->h263_aic){
         s->y_dc_scale_table=
         s->c_dc_scale_table= ff_aic_dc_scale_table;
    }else{
        s->y_dc_scale_table=
        s->c_dc_scale_table= ff_mpeg1_dc_scale_table;
    }
}

/**
 * Encodes a group of blocks header.
 */
void h263_encode_gob_header(MpegEncContext * s, int mb_line)
{
    put_bits(&s->pb, 17, 1); /* GBSC */

    if(s->h263_slice_structured){
        put_bits(&s->pb, 1, 1);

        ff_h263_encode_mba(s);

        if(s->mb_num > 1583)
            put_bits(&s->pb, 1, 1);
        put_bits(&s->pb, 5, s->qscale); /* GQUANT */
        put_bits(&s->pb, 1, 1);
        put_bits(&s->pb, 2, s->pict_type == FF_I_TYPE); /* GFID */
    }else{
        int gob_number= mb_line / s->gob_index;

        put_bits(&s->pb, 5, gob_number); /* GN */
        put_bits(&s->pb, 2, s->pict_type == FF_I_TYPE); /* GFID */
        put_bits(&s->pb, 5, s->qscale); /* GQUANT */
    }
}

/**
 * modify qscale so that encoding is acually possible in h263 (limit difference to -2..2)
 */
void ff_clean_h263_qscales(MpegEncContext *s){
    int i;
    int8_t * const qscale_table= s->current_picture.qscale_table;

    ff_init_qscale_tab(s);

    for(i=1; i<s->mb_num; i++){
        if(qscale_table[ s->mb_index2xy[i] ] - qscale_table[ s->mb_index2xy[i-1] ] >2)
            qscale_table[ s->mb_index2xy[i] ]= qscale_table[ s->mb_index2xy[i-1] ]+2;
    }
    for(i=s->mb_num-2; i>=0; i--){
        if(qscale_table[ s->mb_index2xy[i] ] - qscale_table[ s->mb_index2xy[i+1] ] >2)
            qscale_table[ s->mb_index2xy[i] ]= qscale_table[ s->mb_index2xy[i+1] ]+2;
    }

    if(s->codec_id != CODEC_ID_H263P){
        for(i=1; i<s->mb_num; i++){
            int mb_xy= s->mb_index2xy[i];

            if(qscale_table[mb_xy] != qscale_table[s->mb_index2xy[i-1]] && (s->mb_type[mb_xy]&CANDIDATE_MB_TYPE_INTER4V)){
                s->mb_type[mb_xy]|= CANDIDATE_MB_TYPE_INTER;
            }
        }
    }
}

#endif //CONFIG_ENCODERS

#define tab_size ((signed)FF_ARRAY_ELEMS(s->direct_scale_mv[0]))
#define tab_bias (tab_size/2)

//used by mpeg4 and rv10 decoder
void ff_mpeg4_init_direct_mv(MpegEncContext *s){
    int i;
    for(i=0; i<tab_size; i++){
        s->direct_scale_mv[0][i] = (i-tab_bias)*s->pb_time/s->pp_time;
        s->direct_scale_mv[1][i] = (i-tab_bias)*(s->pb_time-s->pp_time)/s->pp_time;
    }
}

static inline void ff_mpeg4_set_one_direct_mv(MpegEncContext *s, int mx, int my, int i){
    int xy= s->block_index[i];
    uint16_t time_pp= s->pp_time;
    uint16_t time_pb= s->pb_time;
    int p_mx, p_my;

    p_mx= s->next_picture.motion_val[0][xy][0];
    if((unsigned)(p_mx + tab_bias) < tab_size){
        s->mv[0][i][0] = s->direct_scale_mv[0][p_mx + tab_bias] + mx;
        s->mv[1][i][0] = mx ? s->mv[0][i][0] - p_mx
                            : s->direct_scale_mv[1][p_mx + tab_bias];
    }else{
        s->mv[0][i][0] = p_mx*time_pb/time_pp + mx;
        s->mv[1][i][0] = mx ? s->mv[0][i][0] - p_mx
                            : p_mx*(time_pb - time_pp)/time_pp;
    }
    p_my= s->next_picture.motion_val[0][xy][1];
    if((unsigned)(p_my + tab_bias) < tab_size){
        s->mv[0][i][1] = s->direct_scale_mv[0][p_my + tab_bias] + my;
        s->mv[1][i][1] = my ? s->mv[0][i][1] - p_my
                            : s->direct_scale_mv[1][p_my + tab_bias];
    }else{
        s->mv[0][i][1] = p_my*time_pb/time_pp + my;
        s->mv[1][i][1] = my ? s->mv[0][i][1] - p_my
                            : p_my*(time_pb - time_pp)/time_pp;
    }
}

#undef tab_size
#undef tab_bias

/**
 *
 * @return the mb_type
 */
int ff_mpeg4_set_direct_mv(MpegEncContext *s, int mx, int my){
    const int mb_index= s->mb_x + s->mb_y*s->mb_stride;
    const int colocated_mb_type= s->next_picture.mb_type[mb_index];
    uint16_t time_pp;
    uint16_t time_pb;
    int i;

    //FIXME avoid divides
    // try special case with shifts for 1 and 3 B-frames?

    if(IS_8X8(colocated_mb_type)){
        s->mv_type = MV_TYPE_8X8;
        for(i=0; i<4; i++){
            ff_mpeg4_set_one_direct_mv(s, mx, my, i);
        }
        return MB_TYPE_DIRECT2 | MB_TYPE_8x8 | MB_TYPE_L0L1;
    } else if(IS_INTERLACED(colocated_mb_type)){
        s->mv_type = MV_TYPE_FIELD;
        for(i=0; i<2; i++){
            int field_select= s->next_picture.ref_index[0][s->block_index[2*i]];
            s->field_select[0][i]= field_select;
            s->field_select[1][i]= i;
            if(s->top_field_first){
                time_pp= s->pp_field_time - field_select + i;
                time_pb= s->pb_field_time - field_select + i;
            }else{
                time_pp= s->pp_field_time + field_select - i;
                time_pb= s->pb_field_time + field_select - i;
            }
            s->mv[0][i][0] = s->p_field_mv_table[i][0][mb_index][0]*time_pb/time_pp + mx;
            s->mv[0][i][1] = s->p_field_mv_table[i][0][mb_index][1]*time_pb/time_pp + my;
            s->mv[1][i][0] = mx ? s->mv[0][i][0] - s->p_field_mv_table[i][0][mb_index][0]
                                : s->p_field_mv_table[i][0][mb_index][0]*(time_pb - time_pp)/time_pp;
            s->mv[1][i][1] = my ? s->mv[0][i][1] - s->p_field_mv_table[i][0][mb_index][1]
                                : s->p_field_mv_table[i][0][mb_index][1]*(time_pb - time_pp)/time_pp;
        }
        return MB_TYPE_DIRECT2 | MB_TYPE_16x8 | MB_TYPE_L0L1 | MB_TYPE_INTERLACED;
    }else{
        ff_mpeg4_set_one_direct_mv(s, mx, my, 0);
        s->mv[0][1][0] = s->mv[0][2][0] = s->mv[0][3][0] = s->mv[0][0][0];
        s->mv[0][1][1] = s->mv[0][2][1] = s->mv[0][3][1] = s->mv[0][0][1];
        s->mv[1][1][0] = s->mv[1][2][0] = s->mv[1][3][0] = s->mv[1][0][0];
        s->mv[1][1][1] = s->mv[1][2][1] = s->mv[1][3][1] = s->mv[1][0][1];
        if((s->avctx->workaround_bugs & FF_BUG_DIRECT_BLOCKSIZE) || !s->quarter_sample)
            s->mv_type= MV_TYPE_16X16;
        else
            s->mv_type= MV_TYPE_8X8;
        return MB_TYPE_DIRECT2 | MB_TYPE_16x16 | MB_TYPE_L0L1; //Note see prev line
    }
}

void ff_h263_update_motion_val(MpegEncContext * s){
    const int mb_xy = s->mb_y * s->mb_stride + s->mb_x;
               //FIXME a lot of that is only needed for !low_delay
    const int wrap = s->b8_stride;
    const int xy = s->block_index[0];

    s->current_picture.mbskip_table[mb_xy]= s->mb_skipped;

    if(s->mv_type != MV_TYPE_8X8){
        int motion_x, motion_y;
        if (s->mb_intra) {
            motion_x = 0;
            motion_y = 0;
        } else if (s->mv_type == MV_TYPE_16X16) {
            motion_x = s->mv[0][0][0];
            motion_y = s->mv[0][0][1];
        } else /*if (s->mv_type == MV_TYPE_FIELD)*/ {
            int i;
            motion_x = s->mv[0][0][0] + s->mv[0][1][0];
            motion_y = s->mv[0][0][1] + s->mv[0][1][1];
            motion_x = (motion_x>>1) | (motion_x&1);
            for(i=0; i<2; i++){
                s->p_field_mv_table[i][0][mb_xy][0]= s->mv[0][i][0];
                s->p_field_mv_table[i][0][mb_xy][1]= s->mv[0][i][1];
            }
            s->current_picture.ref_index[0][xy           ]=
            s->current_picture.ref_index[0][xy        + 1]= s->field_select[0][0];
            s->current_picture.ref_index[0][xy + wrap    ]=
            s->current_picture.ref_index[0][xy + wrap + 1]= s->field_select[0][1];
        }

        /* no update if 8X8 because it has been done during parsing */
        s->current_picture.motion_val[0][xy][0] = motion_x;
        s->current_picture.motion_val[0][xy][1] = motion_y;
        s->current_picture.motion_val[0][xy + 1][0] = motion_x;
        s->current_picture.motion_val[0][xy + 1][1] = motion_y;
        s->current_picture.motion_val[0][xy + wrap][0] = motion_x;
        s->current_picture.motion_val[0][xy + wrap][1] = motion_y;
        s->current_picture.motion_val[0][xy + 1 + wrap][0] = motion_x;
        s->current_picture.motion_val[0][xy + 1 + wrap][1] = motion_y;
    }

    if(s->encoding){ //FIXME encoding MUST be cleaned up
        if (s->mv_type == MV_TYPE_8X8)
            s->current_picture.mb_type[mb_xy]= MB_TYPE_L0 | MB_TYPE_8x8;
        else if(s->mb_intra)
            s->current_picture.mb_type[mb_xy]= MB_TYPE_INTRA;
        else
            s->current_picture.mb_type[mb_xy]= MB_TYPE_L0 | MB_TYPE_16x16;
    }
}

#if CONFIG_ENCODERS

static const int dquant_code[5]= {1,0,9,2,3};

/**
 * encodes a 8x8 block.
 * @param block the 8x8 block
 * @param n block index (0-3 are luma, 4-5 are chroma)
 */
static void h263_encode_block(MpegEncContext * s, DCTELEM * block, int n)
{
    int level, run, last, i, j, last_index, last_non_zero, sign, slevel, code;
    RLTable *rl;

    rl = &rl_inter;
    if (s->mb_intra && !s->h263_aic) {
        /* DC coef */
        level = block[0];
        /* 255 cannot be represented, so we clamp */
        if (level > 254) {
            level = 254;
            block[0] = 254;
        }
        /* 0 cannot be represented also */
        else if (level < 1) {
            level = 1;
            block[0] = 1;
        }
        if (level == 128) //FIXME check rv10
            put_bits(&s->pb, 8, 0xff);
        else
            put_bits(&s->pb, 8, level);
        i = 1;
    } else {
        i = 0;
        if (s->h263_aic && s->mb_intra)
            rl = &rl_intra_aic;

        if(s->alt_inter_vlc && !s->mb_intra){
            int aic_vlc_bits=0;
            int inter_vlc_bits=0;
            int wrong_pos=-1;
            int aic_code;

            last_index = s->block_last_index[n];
            last_non_zero = i - 1;
            for (; i <= last_index; i++) {
                j = s->intra_scantable.permutated[i];
                level = block[j];
                if (level) {
                    run = i - last_non_zero - 1;
                    last = (i == last_index);

                    if(level<0) level= -level;

                    code = get_rl_index(rl, last, run, level);
                    aic_code = get_rl_index(&rl_intra_aic, last, run, level);
                    inter_vlc_bits += rl->table_vlc[code][1]+1;
                    aic_vlc_bits   += rl_intra_aic.table_vlc[aic_code][1]+1;

                    if (code == rl->n) {
                        inter_vlc_bits += 1+6+8-1;
                    }
                    if (aic_code == rl_intra_aic.n) {
                        aic_vlc_bits += 1+6+8-1;
                        wrong_pos += run + 1;
                    }else
                        wrong_pos += wrong_run[aic_code];
                    last_non_zero = i;
                }
            }
            i = 0;
            if(aic_vlc_bits < inter_vlc_bits && wrong_pos > 63)
                rl = &rl_intra_aic;
        }
    }

    /* AC coefs */
    last_index = s->block_last_index[n];
    last_non_zero = i - 1;
    for (; i <= last_index; i++) {
        j = s->intra_scantable.permutated[i];
        level = block[j];
        if (level) {
            run = i - last_non_zero - 1;
            last = (i == last_index);
            sign = 0;
            slevel = level;
            if (level < 0) {
                sign = 1;
                level = -level;
            }
            code = get_rl_index(rl, last, run, level);
            put_bits(&s->pb, rl->table_vlc[code][1], rl->table_vlc[code][0]);
            if (code == rl->n) {
              if(!CONFIG_FLV_ENCODER || s->h263_flv <= 1){
                put_bits(&s->pb, 1, last);
                put_bits(&s->pb, 6, run);

                assert(slevel != 0);

                if(level < 128)
                    put_sbits(&s->pb, 8, slevel);
                else{
                    put_bits(&s->pb, 8, 128);
                    put_sbits(&s->pb, 5, slevel);
                    put_sbits(&s->pb, 6, slevel>>5);
                }
              }else{
                    ff_flv2_encode_ac_esc(&s->pb, slevel, level, run, last);
              }
            } else {
                put_bits(&s->pb, 1, sign);
            }
            last_non_zero = i;
        }
    }
}

/* Encode MV differences on H.263+ with Unrestricted MV mode */
static void h263p_encode_umotion(MpegEncContext * s, int val)
{
    short sval = 0;
    short i = 0;
    short n_bits = 0;
    short temp_val;
    int code = 0;
    int tcode;

    if ( val == 0)
        put_bits(&s->pb, 1, 1);
    else if (val == 1)
        put_bits(&s->pb, 3, 0);
    else if (val == -1)
        put_bits(&s->pb, 3, 2);
    else {

        sval = ((val < 0) ? (short)(-val):(short)val);
        temp_val = sval;

        while (temp_val != 0) {
            temp_val = temp_val >> 1;
            n_bits++;
        }

        i = n_bits - 1;
        while (i > 0) {
            tcode = (sval & (1 << (i-1))) >> (i-1);
            tcode = (tcode << 1) | 1;
            code = (code << 2) | tcode;
            i--;
        }
        code = ((code << 1) | (val < 0)) << 1;
        put_bits(&s->pb, (2*n_bits)+1, code);
    }
}

static int h263_pred_dc(MpegEncContext * s, int n, int16_t **dc_val_ptr)
{
    int x, y, wrap, a, c, pred_dc;
    int16_t *dc_val;

    /* find prediction */
    if (n < 4) {
        x = 2 * s->mb_x + (n & 1);
        y = 2 * s->mb_y + ((n & 2) >> 1);
        wrap = s->b8_stride;
        dc_val = s->dc_val[0];
    } else {
        x = s->mb_x;
        y = s->mb_y;
        wrap = s->mb_stride;
        dc_val = s->dc_val[n - 4 + 1];
    }
    /* B C
     * A X
     */
    a = dc_val[(x - 1) + (y) * wrap];
    c = dc_val[(x) + (y - 1) * wrap];

    /* No prediction outside GOB boundary */
    if(s->first_slice_line && n!=3){
        if(n!=2) c= 1024;
        if(n!=1 && s->mb_x == s->resync_mb_x) a= 1024;
    }
    /* just DC prediction */
    if (a != 1024 && c != 1024)
        pred_dc = (a + c) >> 1;
    else if (a != 1024)
        pred_dc = a;
    else
        pred_dc = c;

    /* we assume pred is positive */
    *dc_val_ptr = &dc_val[x + y * wrap];
    return pred_dc;
}

void h263_encode_mb(MpegEncContext * s,
                    DCTELEM block[6][64],
                    int motion_x, int motion_y)
{
    int cbpc, cbpy, i, cbp, pred_x, pred_y;
    int16_t pred_dc;
    int16_t rec_intradc[6];
    int16_t *dc_ptr[6];
    const int interleaved_stats= (s->flags&CODEC_FLAG_PASS1);

    if (!s->mb_intra) {
        /* compute cbp */
        cbp= get_p_cbp(s, block, motion_x, motion_y);

        if ((cbp | motion_x | motion_y | s->dquant | (s->mv_type - MV_TYPE_16X16)) == 0) {
            /* skip macroblock */
            put_bits(&s->pb, 1, 1);
            if(interleaved_stats){
                s->misc_bits++;
                s->last_bits++;
            }
            s->skip_count++;

            return;
        }
        put_bits(&s->pb, 1, 0);         /* mb coded */

        cbpc = cbp & 3;
        cbpy = cbp >> 2;
        if(s->alt_inter_vlc==0 || cbpc!=3)
            cbpy ^= 0xF;
        if(s->dquant) cbpc+= 8;
        if(s->mv_type==MV_TYPE_16X16){
            put_bits(&s->pb,
                    inter_MCBPC_bits[cbpc],
                    inter_MCBPC_code[cbpc]);

            put_bits(&s->pb, cbpy_tab[cbpy][1], cbpy_tab[cbpy][0]);
            if(s->dquant)
                put_bits(&s->pb, 2, dquant_code[s->dquant+2]);

            if(interleaved_stats){
                s->misc_bits+= get_bits_diff(s);
            }

            /* motion vectors: 16x16 mode */
            h263_pred_motion(s, 0, 0, &pred_x, &pred_y);

            if (!s->umvplus) {
                ff_h263_encode_motion_vector(s, motion_x - pred_x,
                                                motion_y - pred_y, 1);
            }
            else {
                h263p_encode_umotion(s, motion_x - pred_x);
                h263p_encode_umotion(s, motion_y - pred_y);
                if (((motion_x - pred_x) == 1) && ((motion_y - pred_y) == 1))
                    /* To prevent Start Code emulation */
                    put_bits(&s->pb,1,1);
            }
        }else{
            put_bits(&s->pb,
                    inter_MCBPC_bits[cbpc+16],
                    inter_MCBPC_code[cbpc+16]);
            put_bits(&s->pb, cbpy_tab[cbpy][1], cbpy_tab[cbpy][0]);
            if(s->dquant)
                put_bits(&s->pb, 2, dquant_code[s->dquant+2]);

            if(interleaved_stats){
                s->misc_bits+= get_bits_diff(s);
            }

            for(i=0; i<4; i++){
                /* motion vectors: 8x8 mode*/
                h263_pred_motion(s, i, 0, &pred_x, &pred_y);

                motion_x= s->current_picture.motion_val[0][ s->block_index[i] ][0];
                motion_y= s->current_picture.motion_val[0][ s->block_index[i] ][1];
                if (!s->umvplus) {
                    ff_h263_encode_motion_vector(s, motion_x - pred_x,
                                                    motion_y - pred_y, 1);
                }
                else {
                    h263p_encode_umotion(s, motion_x - pred_x);
                    h263p_encode_umotion(s, motion_y - pred_y);
                    if (((motion_x - pred_x) == 1) && ((motion_y - pred_y) == 1))
                        /* To prevent Start Code emulation */
                        put_bits(&s->pb,1,1);
                }
            }
        }

        if(interleaved_stats){
            s->mv_bits+= get_bits_diff(s);
        }
    } else {
        assert(s->mb_intra);

        cbp = 0;
        if (s->h263_aic) {
            /* Predict DC */
            for(i=0; i<6; i++) {
                int16_t level = block[i][0];
                int scale;

                if(i<4) scale= s->y_dc_scale;
                else    scale= s->c_dc_scale;

                pred_dc = h263_pred_dc(s, i, &dc_ptr[i]);
                level -= pred_dc;
                /* Quant */
                if (level >= 0)
                    level = (level + (scale>>1))/scale;
                else
                    level = (level - (scale>>1))/scale;

                /* AIC can change CBP */
                if (level == 0 && s->block_last_index[i] == 0)
                    s->block_last_index[i] = -1;

                if(!s->modified_quant){
                    if (level < -127)
                        level = -127;
                    else if (level > 127)
                        level = 127;
                }

                block[i][0] = level;
                /* Reconstruction */
                rec_intradc[i] = scale*level + pred_dc;
                /* Oddify */
                rec_intradc[i] |= 1;
                //if ((rec_intradc[i] % 2) == 0)
                //    rec_intradc[i]++;
                /* Clipping */
                if (rec_intradc[i] < 0)
                    rec_intradc[i] = 0;
                else if (rec_intradc[i] > 2047)
                    rec_intradc[i] = 2047;

                /* Update AC/DC tables */
                *dc_ptr[i] = rec_intradc[i];
                if (s->block_last_index[i] >= 0)
                    cbp |= 1 << (5 - i);
            }
        }else{
            for(i=0; i<6; i++) {
                /* compute cbp */
                if (s->block_last_index[i] >= 1)
                    cbp |= 1 << (5 - i);
            }
        }

        cbpc = cbp & 3;
        if (s->pict_type == FF_I_TYPE) {
            if(s->dquant) cbpc+=4;
            put_bits(&s->pb,
                intra_MCBPC_bits[cbpc],
                intra_MCBPC_code[cbpc]);
        } else {
            if(s->dquant) cbpc+=8;
            put_bits(&s->pb, 1, 0);     /* mb coded */
            put_bits(&s->pb,
                inter_MCBPC_bits[cbpc + 4],
                inter_MCBPC_code[cbpc + 4]);
        }
        if (s->h263_aic) {
            /* XXX: currently, we do not try to use ac prediction */
            put_bits(&s->pb, 1, 0);     /* no AC prediction */
        }
        cbpy = cbp >> 2;
        put_bits(&s->pb, cbpy_tab[cbpy][1], cbpy_tab[cbpy][0]);
        if(s->dquant)
            put_bits(&s->pb, 2, dquant_code[s->dquant+2]);

        if(interleaved_stats){
            s->misc_bits+= get_bits_diff(s);
        }
    }

    for(i=0; i<6; i++) {
        /* encode each block */
        h263_encode_block(s, block[i], i);

        /* Update INTRADC for decoding */
        if (s->h263_aic && s->mb_intra) {
            block[i][0] = rec_intradc[i];

        }
    }

    if(interleaved_stats){
        if (!s->mb_intra) {
            s->p_tex_bits+= get_bits_diff(s);
            s->f_count++;
        }else{
            s->i_tex_bits+= get_bits_diff(s);
            s->i_count++;
        }
    }
}
#endif

void ff_h263_loop_filter(MpegEncContext * s){
    int qp_c;
    const int linesize  = s->linesize;
    const int uvlinesize= s->uvlinesize;
    const int xy = s->mb_y * s->mb_stride + s->mb_x;
    uint8_t *dest_y = s->dest[0];
    uint8_t *dest_cb= s->dest[1];
    uint8_t *dest_cr= s->dest[2];

//    if(s->pict_type==FF_B_TYPE && !s->readable) return;

    /*
       Diag Top
       Left Center
    */
    if(!IS_SKIP(s->current_picture.mb_type[xy])){
        qp_c= s->qscale;
        s->dsp.h263_v_loop_filter(dest_y+8*linesize  , linesize, qp_c);
        s->dsp.h263_v_loop_filter(dest_y+8*linesize+8, linesize, qp_c);
    }else
        qp_c= 0;

    if(s->mb_y){
        int qp_dt, qp_tt, qp_tc;

        if(IS_SKIP(s->current_picture.mb_type[xy-s->mb_stride]))
            qp_tt=0;
        else
            qp_tt= s->current_picture.qscale_table[xy-s->mb_stride];

        if(qp_c)
            qp_tc= qp_c;
        else
            qp_tc= qp_tt;

        if(qp_tc){
            const int chroma_qp= s->chroma_qscale_table[qp_tc];
            s->dsp.h263_v_loop_filter(dest_y  ,   linesize, qp_tc);
            s->dsp.h263_v_loop_filter(dest_y+8,   linesize, qp_tc);

            s->dsp.h263_v_loop_filter(dest_cb , uvlinesize, chroma_qp);
            s->dsp.h263_v_loop_filter(dest_cr , uvlinesize, chroma_qp);
        }

        if(qp_tt)
            s->dsp.h263_h_loop_filter(dest_y-8*linesize+8  ,   linesize, qp_tt);

        if(s->mb_x){
            if(qp_tt || IS_SKIP(s->current_picture.mb_type[xy-1-s->mb_stride]))
                qp_dt= qp_tt;
            else
                qp_dt= s->current_picture.qscale_table[xy-1-s->mb_stride];

            if(qp_dt){
                const int chroma_qp= s->chroma_qscale_table[qp_dt];
                s->dsp.h263_h_loop_filter(dest_y -8*linesize  ,   linesize, qp_dt);
                s->dsp.h263_h_loop_filter(dest_cb-8*uvlinesize, uvlinesize, chroma_qp);
                s->dsp.h263_h_loop_filter(dest_cr-8*uvlinesize, uvlinesize, chroma_qp);
            }
        }
    }

    if(qp_c){
        s->dsp.h263_h_loop_filter(dest_y +8,   linesize, qp_c);
        if(s->mb_y + 1 == s->mb_height)
            s->dsp.h263_h_loop_filter(dest_y+8*linesize+8,   linesize, qp_c);
    }

    if(s->mb_x){
        int qp_lc;
        if(qp_c || IS_SKIP(s->current_picture.mb_type[xy-1]))
            qp_lc= qp_c;
        else
            qp_lc= s->current_picture.qscale_table[xy-1];

        if(qp_lc){
            s->dsp.h263_h_loop_filter(dest_y,   linesize, qp_lc);
            if(s->mb_y + 1 == s->mb_height){
                const int chroma_qp= s->chroma_qscale_table[qp_lc];
                s->dsp.h263_h_loop_filter(dest_y +8*  linesize,   linesize, qp_lc);
                s->dsp.h263_h_loop_filter(dest_cb             , uvlinesize, chroma_qp);
                s->dsp.h263_h_loop_filter(dest_cr             , uvlinesize, chroma_qp);
            }
        }
    }
}

static void h263_pred_acdc(MpegEncContext * s, DCTELEM *block, int n)
{
    int x, y, wrap, a, c, pred_dc, scale, i;
    int16_t *dc_val, *ac_val, *ac_val1;

    /* find prediction */
    if (n < 4) {
        x = 2 * s->mb_x + (n & 1);
        y = 2 * s->mb_y + (n>> 1);
        wrap = s->b8_stride;
        dc_val = s->dc_val[0];
        ac_val = s->ac_val[0][0];
        scale = s->y_dc_scale;
    } else {
        x = s->mb_x;
        y = s->mb_y;
        wrap = s->mb_stride;
        dc_val = s->dc_val[n - 4 + 1];
        ac_val = s->ac_val[n - 4 + 1][0];
        scale = s->c_dc_scale;
    }

    ac_val += ((y) * wrap + (x)) * 16;
    ac_val1 = ac_val;

    /* B C
     * A X
     */
    a = dc_val[(x - 1) + (y) * wrap];
    c = dc_val[(x) + (y - 1) * wrap];

    /* No prediction outside GOB boundary */
    if(s->first_slice_line && n!=3){
        if(n!=2) c= 1024;
        if(n!=1 && s->mb_x == s->resync_mb_x) a= 1024;
    }

    if (s->ac_pred) {
        pred_dc = 1024;
        if (s->h263_aic_dir) {
            /* left prediction */
            if (a != 1024) {
                ac_val -= 16;
                for(i=1;i<8;i++) {
                    block[s->dsp.idct_permutation[i<<3]] += ac_val[i];
                }
                pred_dc = a;
            }
        } else {
            /* top prediction */
            if (c != 1024) {
                ac_val -= 16 * wrap;
                for(i=1;i<8;i++) {
                    block[s->dsp.idct_permutation[i   ]] += ac_val[i + 8];
                }
                pred_dc = c;
            }
        }
    } else {
        /* just DC prediction */
        if (a != 1024 && c != 1024)
            pred_dc = (a + c) >> 1;
        else if (a != 1024)
            pred_dc = a;
        else
            pred_dc = c;
    }

    /* we assume pred is positive */
    block[0]=block[0]*scale + pred_dc;

    if (block[0] < 0)
        block[0] = 0;
    else
        block[0] |= 1;

    /* Update AC/DC tables */
    dc_val[(x) + (y) * wrap] = block[0];

    /* left copy */
    for(i=1;i<8;i++)
        ac_val1[i    ] = block[s->dsp.idct_permutation[i<<3]];
    /* top copy */
    for(i=1;i<8;i++)
        ac_val1[8 + i] = block[s->dsp.idct_permutation[i   ]];
}

int16_t *h263_pred_motion(MpegEncContext * s, int block, int dir,
                        int *px, int *py)
{
    int wrap;
    int16_t *A, *B, *C, (*mot_val)[2];
    static const int off[4]= {2, 1, 1, -1};

    wrap = s->b8_stride;
    mot_val = s->current_picture.motion_val[dir] + s->block_index[block];

    A = mot_val[ - 1];
    /* special case for first (slice) line */
    if (s->first_slice_line && block<3) {
        // we can't just change some MVs to simulate that as we need them for the B frames (and ME)
        // and if we ever support non rectangular objects than we need to do a few ifs here anyway :(
        if(block==0){ //most common case
            if(s->mb_x  == s->resync_mb_x){ //rare
                *px= *py = 0;
            }else if(s->mb_x + 1 == s->resync_mb_x && s->h263_pred){ //rare
                C = mot_val[off[block] - wrap];
                if(s->mb_x==0){
                    *px = C[0];
                    *py = C[1];
                }else{
                    *px = mid_pred(A[0], 0, C[0]);
                    *py = mid_pred(A[1], 0, C[1]);
                }
            }else{
                *px = A[0];
                *py = A[1];
            }
        }else if(block==1){
            if(s->mb_x + 1 == s->resync_mb_x && s->h263_pred){ //rare
                C = mot_val[off[block] - wrap];
                *px = mid_pred(A[0], 0, C[0]);
                *py = mid_pred(A[1], 0, C[1]);
            }else{
                *px = A[0];
                *py = A[1];
            }
        }else{ /* block==2*/
            B = mot_val[ - wrap];
            C = mot_val[off[block] - wrap];
            if(s->mb_x == s->resync_mb_x) //rare
                A[0]=A[1]=0;

            *px = mid_pred(A[0], B[0], C[0]);
            *py = mid_pred(A[1], B[1], C[1]);
        }
    } else {
        B = mot_val[ - wrap];
        C = mot_val[off[block] - wrap];
        *px = mid_pred(A[0], B[0], C[0]);
        *py = mid_pred(A[1], B[1], C[1]);
    }
    return *mot_val;
}

#if CONFIG_ENCODERS

/***************************************************/
void ff_h263_encode_motion(MpegEncContext * s, int val, int f_code)
{
    int range, l, bit_size, sign, code, bits;

    if (val == 0) {
        /* zero vector */
        code = 0;
        put_bits(&s->pb, mvtab[code][1], mvtab[code][0]);
    } else {
        bit_size = f_code - 1;
        range = 1 << bit_size;
        /* modulo encoding */
        l= INT_BIT - 6 - bit_size;
        val = (val<<l)>>l;
        sign = val>>31;
        val= (val^sign)-sign;
        sign&=1;

        val--;
        code = (val >> bit_size) + 1;
        bits = val & (range - 1);

        put_bits(&s->pb, mvtab[code][1] + 1, (mvtab[code][0] << 1) | sign);
        if (bit_size > 0) {
            put_bits(&s->pb, bit_size, bits);
        }
    }
}

static void init_mv_penalty_and_fcode(MpegEncContext *s)
{
    int f_code;
    int mv;

    for(f_code=1; f_code<=MAX_FCODE; f_code++){
        for(mv=-MAX_MV; mv<=MAX_MV; mv++){
            int len;

            if(mv==0) len= mvtab[0][1];
            else{
                int val, bit_size, code;

                bit_size = f_code - 1;

                val=mv;
                if (val < 0)
                    val = -val;
                val--;
                code = (val >> bit_size) + 1;
                if(code<33){
                    len= mvtab[code][1] + 1 + bit_size;
                }else{
                    len= mvtab[32][1] + av_log2(code>>5) + 2 + bit_size;
                }
            }

            mv_penalty[f_code][mv+MAX_MV]= len;
        }
    }

    for(f_code=MAX_FCODE; f_code>0; f_code--){
        for(mv=-(16<<f_code); mv<(16<<f_code); mv++){
            fcode_tab[mv+MAX_MV]= f_code;
        }
    }

    for(mv=0; mv<MAX_MV*2+1; mv++){
        umv_fcode_tab[mv]= 1;
    }
}

static void init_uni_h263_rl_tab(RLTable *rl, uint32_t *bits_tab, uint8_t *len_tab){
    int slevel, run, last;

    assert(MAX_LEVEL >= 64);
    assert(MAX_RUN   >= 63);

    for(slevel=-64; slevel<64; slevel++){
        if(slevel==0) continue;
        for(run=0; run<64; run++){
            for(last=0; last<=1; last++){
                const int index= UNI_MPEG4_ENC_INDEX(last, run, slevel+64);
                int level= slevel < 0 ? -slevel : slevel;
                int sign= slevel < 0 ? 1 : 0;
                int bits, len, code;

                len_tab[index]= 100;

                /* ESC0 */
                code= get_rl_index(rl, last, run, level);
                bits= rl->table_vlc[code][0];
                len=  rl->table_vlc[code][1];
                bits=bits*2+sign; len++;

                if(code!=rl->n && len < len_tab[index]){
                    if(bits_tab) bits_tab[index]= bits;
                    len_tab [index]= len;
                }
                /* ESC */
                bits= rl->table_vlc[rl->n][0];
                len = rl->table_vlc[rl->n][1];
                bits=bits*2+last; len++;
                bits=bits*64+run; len+=6;
                bits=bits*256+(level&0xff); len+=8;

                if(len < len_tab[index]){
                    if(bits_tab) bits_tab[index]= bits;
                    len_tab [index]= len;
                }
            }
        }
    }
}

void h263_encode_init(MpegEncContext *s)
{
    static int done = 0;

    if (!done) {
        done = 1;

        init_rl(&rl_inter, static_rl_table_store[0]);
        init_rl(&rl_intra_aic, static_rl_table_store[1]);

        init_uni_h263_rl_tab(&rl_intra_aic, NULL, uni_h263_intra_aic_rl_len);
        init_uni_h263_rl_tab(&rl_inter    , NULL, uni_h263_inter_rl_len);

        init_mv_penalty_and_fcode(s);
    }
    s->me.mv_penalty= mv_penalty; //FIXME exact table for msmpeg4 & h263p

    s->intra_ac_vlc_length     =s->inter_ac_vlc_length     = uni_h263_inter_rl_len;
    s->intra_ac_vlc_last_length=s->inter_ac_vlc_last_length= uni_h263_inter_rl_len + 128*64;
    if(s->h263_aic){
        s->intra_ac_vlc_length     = uni_h263_intra_aic_rl_len;
        s->intra_ac_vlc_last_length= uni_h263_intra_aic_rl_len + 128*64;
    }
    s->ac_esc_length= 7+1+6+8;

    // use fcodes >1 only for mpeg4 & h263 & h263p FIXME
    switch(s->codec_id){
    case CODEC_ID_MPEG4:
        s->fcode_tab= fcode_tab;
        break;
    case CODEC_ID_H263P:
        if(s->umvplus)
            s->fcode_tab= umv_fcode_tab;
        if(s->modified_quant){
            s->min_qcoeff= -2047;
            s->max_qcoeff=  2047;
        }else{
            s->min_qcoeff= -127;
            s->max_qcoeff=  127;
        }
        break;
        //Note for mpeg4 & h263 the dc-scale table will be set per frame as needed later
    case CODEC_ID_FLV1:
        if (s->h263_flv > 1) {
            s->min_qcoeff= -1023;
            s->max_qcoeff=  1023;
        } else {
            s->min_qcoeff= -127;
            s->max_qcoeff=  127;
        }
        s->y_dc_scale_table=
        s->c_dc_scale_table= ff_mpeg1_dc_scale_table;
        break;
    default: //nothing needed - default table already set in mpegvideo.c
        s->min_qcoeff= -127;
        s->max_qcoeff=  127;
        s->y_dc_scale_table=
        s->c_dc_scale_table= ff_mpeg1_dc_scale_table;
    }
}
#endif //CONFIG_ENCODERS

/***********************************************/
/* decoding */

VLC intra_MCBPC_vlc;
VLC inter_MCBPC_vlc;
VLC cbpy_vlc;
static VLC mv_vlc;
static VLC h263_mbtype_b_vlc;
static VLC cbpc_b_vlc;

/* init vlcs */

/* XXX: find a better solution to handle static init */
void h263_decode_init_vlc(MpegEncContext *s)
{
    static int done = 0;

    if (!done) {
        done = 1;

        INIT_VLC_STATIC(&intra_MCBPC_vlc, INTRA_MCBPC_VLC_BITS, 9,
                 intra_MCBPC_bits, 1, 1,
                 intra_MCBPC_code, 1, 1, 72);
        INIT_VLC_STATIC(&inter_MCBPC_vlc, INTER_MCBPC_VLC_BITS, 28,
                 inter_MCBPC_bits, 1, 1,
                 inter_MCBPC_code, 1, 1, 198);
        INIT_VLC_STATIC(&cbpy_vlc, CBPY_VLC_BITS, 16,
                 &cbpy_tab[0][1], 2, 1,
                 &cbpy_tab[0][0], 2, 1, 64);
        INIT_VLC_STATIC(&mv_vlc, MV_VLC_BITS, 33,
                 &mvtab[0][1], 2, 1,
                 &mvtab[0][0], 2, 1, 538);
        init_rl(&rl_inter, static_rl_table_store[0]);
        init_rl(&rl_intra_aic, static_rl_table_store[1]);
        INIT_VLC_RL(rl_inter, 554);
        INIT_VLC_RL(rl_intra_aic, 554);
        INIT_VLC_STATIC(&h263_mbtype_b_vlc, H263_MBTYPE_B_VLC_BITS, 15,
                 &h263_mbtype_b_tab[0][1], 2, 1,
                 &h263_mbtype_b_tab[0][0], 2, 1, 80);
        INIT_VLC_STATIC(&cbpc_b_vlc, CBPC_B_VLC_BITS, 4,
                 &cbpc_b_tab[0][1], 2, 1,
                 &cbpc_b_tab[0][0], 2, 1, 8);
    }
}

/**
 * Get the GOB height based on picture height.
 */
int ff_h263_get_gob_height(MpegEncContext *s){
    if (s->height <= 400)
        return 1;
    else if (s->height <= 800)
        return  2;
    else
        return 4;
}

int ff_h263_decode_mba(MpegEncContext *s)
{
    int i, mb_pos;

    for(i=0; i<6; i++){
        if(s->mb_num-1 <= ff_mba_max[i]) break;
    }
    mb_pos= get_bits(&s->gb, ff_mba_length[i]);
    s->mb_x= mb_pos % s->mb_width;
    s->mb_y= mb_pos / s->mb_width;

    return mb_pos;
}

void ff_h263_encode_mba(MpegEncContext *s)
{
    int i, mb_pos;

    for(i=0; i<6; i++){
        if(s->mb_num-1 <= ff_mba_max[i]) break;
    }
    mb_pos= s->mb_x + s->mb_width*s->mb_y;
    put_bits(&s->pb, ff_mba_length[i], mb_pos);
}

/**
 * decodes the group of blocks header or slice header.
 * @return <0 if an error occurred
 */
static int h263_decode_gob_header(MpegEncContext *s)
{
    unsigned int val, gfid, gob_number;
    int left;

    /* Check for GOB Start Code */
    val = show_bits(&s->gb, 16);
    if(val)
        return -1;

        /* We have a GBSC probably with GSTUFF */
    skip_bits(&s->gb, 16); /* Drop the zeros */
    left= get_bits_left(&s->gb);
    //MN: we must check the bits left or we might end in a infinite loop (or segfault)
    for(;left>13; left--){
        if(get_bits1(&s->gb)) break; /* Seek the '1' bit */
    }
    if(left<=13)
        return -1;

    if(s->h263_slice_structured){
        if(get_bits1(&s->gb)==0)
            return -1;

        ff_h263_decode_mba(s);

        if(s->mb_num > 1583)
            if(get_bits1(&s->gb)==0)
                return -1;

        s->qscale = get_bits(&s->gb, 5); /* SQUANT */
        if(get_bits1(&s->gb)==0)
            return -1;
        gfid = get_bits(&s->gb, 2); /* GFID */
    }else{
        gob_number = get_bits(&s->gb, 5); /* GN */
        s->mb_x= 0;
        s->mb_y= s->gob_index* gob_number;
        gfid = get_bits(&s->gb, 2); /* GFID */
        s->qscale = get_bits(&s->gb, 5); /* GQUANT */
    }

    if(s->mb_y >= s->mb_height)
        return -1;

    if(s->qscale==0)
        return -1;

    return 0;
}

static inline void memsetw(short *tab, int val, int n)
{
    int i;
    for(i=0;i<n;i++)
        tab[i] = val;
}

/**
 * finds the next resync_marker
 * @param p pointer to buffer to scan
 * @param end pointer to the end of the buffer
 * @return pointer to the next resync_marker, or end if none was found
 */
const uint8_t *ff_h263_find_resync_marker(const uint8_t *restrict p, const uint8_t * restrict end)
{
    assert(p < end);

    end-=2;
    p++;
    for(;p<end; p+=2){
        if(!*p){
            if     (!p[-1] && p[1]) return p - 1;
            else if(!p[ 1] && p[2]) return p;
        }
    }
    return end+2;
}

/**
 * decodes the group of blocks / video packet header.
 * @return bit position of the resync_marker, or <0 if none was found
 */
int ff_h263_resync(MpegEncContext *s){
    int left, pos, ret;

    if(s->codec_id==CODEC_ID_MPEG4){
        skip_bits1(&s->gb);
        align_get_bits(&s->gb);
    }

    if(show_bits(&s->gb, 16)==0){
        pos= get_bits_count(&s->gb);
        if(CONFIG_MPEG4_DECODER && s->codec_id==CODEC_ID_MPEG4)
            ret= mpeg4_decode_video_packet_header(s);
        else
            ret= h263_decode_gob_header(s);
        if(ret>=0)
            return pos;
    }
    //OK, it's not where it is supposed to be ...
    s->gb= s->last_resync_gb;
    align_get_bits(&s->gb);
    left= get_bits_left(&s->gb);

    for(;left>16+1+5+5; left-=8){
        if(show_bits(&s->gb, 16)==0){
            GetBitContext bak= s->gb;

            pos= get_bits_count(&s->gb);
            if(CONFIG_MPEG4_DECODER && s->codec_id==CODEC_ID_MPEG4)
                ret= mpeg4_decode_video_packet_header(s);
            else
                ret= h263_decode_gob_header(s);
            if(ret>=0)
                return pos;

            s->gb= bak;
        }
        skip_bits(&s->gb, 8);
    }

    return -1;
}

int h263_decode_motion(MpegEncContext * s, int pred, int f_code)
{
    int code, val, sign, shift, l;
    code = get_vlc2(&s->gb, mv_vlc.table, MV_VLC_BITS, 2);

    if (code == 0)
        return pred;
    if (code < 0)
        return 0xffff;

    sign = get_bits1(&s->gb);
    shift = f_code - 1;
    val = code;
    if (shift) {
        val = (val - 1) << shift;
        val |= get_bits(&s->gb, shift);
        val++;
    }
    if (sign)
        val = -val;
    val += pred;

    /* modulo decoding */
    if (!s->h263_long_vectors) {
        l = INT_BIT - 5 - f_code;
        val = (val<<l)>>l;
    } else {
        /* horrible h263 long vector mode */
        if (pred < -31 && val < -63)
            val += 64;
        if (pred > 32 && val > 63)
            val -= 64;

    }
    return val;
}


/* Decodes RVLC of H.263+ UMV */
static int h263p_decode_umotion(MpegEncContext * s, int pred)
{
   int code = 0, sign;

   if (get_bits1(&s->gb)) /* Motion difference = 0 */
      return pred;

   code = 2 + get_bits1(&s->gb);

   while (get_bits1(&s->gb))
   {
      code <<= 1;
      code += get_bits1(&s->gb);
   }
   sign = code & 1;
   code >>= 1;

   code = (sign) ? (pred - code) : (pred + code);
   dprintf(s->avctx,"H.263+ UMV Motion = %d\n", code);
   return code;

}

/**
 * read the next MVs for OBMC. yes this is a ugly hack, feel free to send a patch :)
 */
static void preview_obmc(MpegEncContext *s){
    GetBitContext gb= s->gb;

    int cbpc, i, pred_x, pred_y, mx, my;
    int16_t *mot_val;
    const int xy= s->mb_x + 1 + s->mb_y * s->mb_stride;
    const int stride= s->b8_stride*2;

    for(i=0; i<4; i++)
        s->block_index[i]+= 2;
    for(i=4; i<6; i++)
        s->block_index[i]+= 1;
    s->mb_x++;

    assert(s->pict_type == FF_P_TYPE);

    do{
        if (get_bits1(&s->gb)) {
            /* skip mb */
            mot_val = s->current_picture.motion_val[0][ s->block_index[0] ];
            mot_val[0       ]= mot_val[2       ]=
            mot_val[0+stride]= mot_val[2+stride]= 0;
            mot_val[1       ]= mot_val[3       ]=
            mot_val[1+stride]= mot_val[3+stride]= 0;

            s->current_picture.mb_type[xy]= MB_TYPE_SKIP | MB_TYPE_16x16 | MB_TYPE_L0;
            goto end;
        }
        cbpc = get_vlc2(&s->gb, inter_MCBPC_vlc.table, INTER_MCBPC_VLC_BITS, 2);
    }while(cbpc == 20);

    if(cbpc & 4){
        s->current_picture.mb_type[xy]= MB_TYPE_INTRA;
    }else{
        get_vlc2(&s->gb, cbpy_vlc.table, CBPY_VLC_BITS, 1);
        if (cbpc & 8) {
            if(s->modified_quant){
                if(get_bits1(&s->gb)) skip_bits(&s->gb, 1);
                else                  skip_bits(&s->gb, 5);
            }else
                skip_bits(&s->gb, 2);
        }

        if ((cbpc & 16) == 0) {
                s->current_picture.mb_type[xy]= MB_TYPE_16x16 | MB_TYPE_L0;
                /* 16x16 motion prediction */
                mot_val= h263_pred_motion(s, 0, 0, &pred_x, &pred_y);
                if (s->umvplus)
                   mx = h263p_decode_umotion(s, pred_x);
                else
                   mx = h263_decode_motion(s, pred_x, 1);

                if (s->umvplus)
                   my = h263p_decode_umotion(s, pred_y);
                else
                   my = h263_decode_motion(s, pred_y, 1);

                mot_val[0       ]= mot_val[2       ]=
                mot_val[0+stride]= mot_val[2+stride]= mx;
                mot_val[1       ]= mot_val[3       ]=
                mot_val[1+stride]= mot_val[3+stride]= my;
        } else {
            s->current_picture.mb_type[xy]= MB_TYPE_8x8 | MB_TYPE_L0;
            for(i=0;i<4;i++) {
                mot_val = h263_pred_motion(s, i, 0, &pred_x, &pred_y);
                if (s->umvplus)
                  mx = h263p_decode_umotion(s, pred_x);
                else
                  mx = h263_decode_motion(s, pred_x, 1);

                if (s->umvplus)
                  my = h263p_decode_umotion(s, pred_y);
                else
                  my = h263_decode_motion(s, pred_y, 1);
                if (s->umvplus && (mx - pred_x) == 1 && (my - pred_y) == 1)
                  skip_bits1(&s->gb); /* Bit stuffing to prevent PSC */
                mot_val[0] = mx;
                mot_val[1] = my;
            }
        }
    }
end:

    for(i=0; i<4; i++)
        s->block_index[i]-= 2;
    for(i=4; i<6; i++)
        s->block_index[i]-= 1;
    s->mb_x--;

    s->gb= gb;
}

static void h263_decode_dquant(MpegEncContext *s){
    static const int8_t quant_tab[4] = { -1, -2, 1, 2 };

    if(s->modified_quant){
        if(get_bits1(&s->gb))
            s->qscale= modified_quant_tab[get_bits1(&s->gb)][ s->qscale ];
        else
            s->qscale= get_bits(&s->gb, 5);
    }else
        s->qscale += quant_tab[get_bits(&s->gb, 2)];
    ff_set_qscale(s, s->qscale);
}

static int h263_decode_block(MpegEncContext * s, DCTELEM * block,
                             int n, int coded)
{
    int code, level, i, j, last, run;
    RLTable *rl = &rl_inter;
    const uint8_t *scan_table;
    GetBitContext gb= s->gb;

    scan_table = s->intra_scantable.permutated;
    if (s->h263_aic && s->mb_intra) {
        rl = &rl_intra_aic;
        i = 0;
        if (s->ac_pred) {
            if (s->h263_aic_dir)
                scan_table = s->intra_v_scantable.permutated; /* left */
            else
                scan_table = s->intra_h_scantable.permutated; /* top */
        }
    } else if (s->mb_intra) {
        /* DC coef */
        if(s->codec_id == CODEC_ID_RV10){
#if CONFIG_RV10_DECODER
          if (s->rv10_version == 3 && s->pict_type == FF_I_TYPE) {
            int component, diff;
            component = (n <= 3 ? 0 : n - 4 + 1);
            level = s->last_dc[component];
            if (s->rv10_first_dc_coded[component]) {
                diff = rv_decode_dc(s, n);
                if (diff == 0xffff)
                    return -1;
                level += diff;
                level = level & 0xff; /* handle wrap round */
                s->last_dc[component] = level;
            } else {
                s->rv10_first_dc_coded[component] = 1;
            }
          } else {
                level = get_bits(&s->gb, 8);
                if (level == 255)
                    level = 128;
          }
#endif
        }else{
            level = get_bits(&s->gb, 8);
            if((level&0x7F) == 0){
                av_log(s->avctx, AV_LOG_ERROR, "illegal dc %d at %d %d\n", level, s->mb_x, s->mb_y);
                if(s->error_recognition >= FF_ER_COMPLIANT)
                    return -1;
            }
            if (level == 255)
                level = 128;
        }
        block[0] = level;
        i = 1;
    } else {
        i = 0;
    }
    if (!coded) {
        if (s->mb_intra && s->h263_aic)
            goto not_coded;
        s->block_last_index[n] = i - 1;
        return 0;
    }
retry:
    for(;;) {
        code = get_vlc2(&s->gb, rl->vlc.table, TEX_VLC_BITS, 2);
        if (code < 0){
            av_log(s->avctx, AV_LOG_ERROR, "illegal ac vlc code at %dx%d\n", s->mb_x, s->mb_y);
            return -1;
        }
        if (code == rl->n) {
            /* escape */
            if (CONFIG_FLV_DECODER && s->h263_flv > 1) {
                ff_flv2_decode_ac_esc(&s->gb, &level, &run, &last);
            } else {
                last = get_bits1(&s->gb);
                run = get_bits(&s->gb, 6);
                level = (int8_t)get_bits(&s->gb, 8);
                if(level == -128){
                    if (s->codec_id == CODEC_ID_RV10) {
                        /* XXX: should patch encoder too */
                        level = get_sbits(&s->gb, 12);
                    }else{
                        level = get_bits(&s->gb, 5);
                        level |= get_sbits(&s->gb, 6)<<5;
                    }
                }
            }
        } else {
            run = rl->table_run[code];
            level = rl->table_level[code];
            last = code >= rl->last;
            if (get_bits1(&s->gb))
                level = -level;
        }
        i += run;
        if (i >= 64){
            if(s->alt_inter_vlc && rl == &rl_inter && !s->mb_intra){
                //Looks like a hack but no, it's the way it is supposed to work ...
                rl = &rl_intra_aic;
                i = 0;
                s->gb= gb;
                s->dsp.clear_block(block);
                goto retry;
            }
            av_log(s->avctx, AV_LOG_ERROR, "run overflow at %dx%d i:%d\n", s->mb_x, s->mb_y, s->mb_intra);
            return -1;
        }
        j = scan_table[i];
        block[j] = level;
        if (last)
            break;
        i++;
    }
not_coded:
    if (s->mb_intra && s->h263_aic) {
        h263_pred_acdc(s, block, n);
        i = 63;
    }
    s->block_last_index[n] = i;
    return 0;
}

static int h263_skip_b_part(MpegEncContext *s, int cbp)
{
    DECLARE_ALIGNED(16, DCTELEM, dblock[64]);
    int i, mbi;

    /* we have to set s->mb_intra to zero to decode B-part of PB-frame correctly
     * but real value should be restored in order to be used later (in OBMC condition)
     */
    mbi = s->mb_intra;
    s->mb_intra = 0;
    for (i = 0; i < 6; i++) {
        if (h263_decode_block(s, dblock, i, cbp&32) < 0)
            return -1;
        cbp+=cbp;
    }
    s->mb_intra = mbi;
    return 0;
}

static int h263_get_modb(GetBitContext *gb, int pb_frame, int *cbpb)
{
    int c, mv = 1;

    if (pb_frame < 3) { // h.263 Annex G and i263 PB-frame
        c = get_bits1(gb);
        if (pb_frame == 2 && c)
            mv = !get_bits1(gb);
    } else { // h.263 Annex M improved PB-frame
        mv = get_unary(gb, 0, 4) + 1;
        c = mv & 1;
        mv = !!(mv & 2);
    }
    if(c)
        *cbpb = get_bits(gb, 6);
    return mv;
}

int ff_h263_decode_mb(MpegEncContext *s,
                      DCTELEM block[6][64])
{
    int cbpc, cbpy, i, cbp, pred_x, pred_y, mx, my, dquant;
    int16_t *mot_val;
    const int xy= s->mb_x + s->mb_y * s->mb_stride;
    int cbpb = 0, pb_mv_count = 0;

    assert(!s->h263_pred);

    if (s->pict_type == FF_P_TYPE) {
        do{
            if (get_bits1(&s->gb)) {
                /* skip mb */
                s->mb_intra = 0;
                for(i=0;i<6;i++)
                    s->block_last_index[i] = -1;
                s->mv_dir = MV_DIR_FORWARD;
                s->mv_type = MV_TYPE_16X16;
                s->current_picture.mb_type[xy]= MB_TYPE_SKIP | MB_TYPE_16x16 | MB_TYPE_L0;
                s->mv[0][0][0] = 0;
                s->mv[0][0][1] = 0;
                s->mb_skipped = !(s->obmc | s->loop_filter);
                goto end;
            }
            cbpc = get_vlc2(&s->gb, inter_MCBPC_vlc.table, INTER_MCBPC_VLC_BITS, 2);
            if (cbpc < 0){
                av_log(s->avctx, AV_LOG_ERROR, "cbpc damaged at %d %d\n", s->mb_x, s->mb_y);
                return -1;
            }
        }while(cbpc == 20);

        s->dsp.clear_blocks(s->block[0]);

        dquant = cbpc & 8;
        s->mb_intra = ((cbpc & 4) != 0);
        if (s->mb_intra) goto intra;

        if(s->pb_frame && get_bits1(&s->gb))
            pb_mv_count = h263_get_modb(&s->gb, s->pb_frame, &cbpb);
        cbpy = get_vlc2(&s->gb, cbpy_vlc.table, CBPY_VLC_BITS, 1);

        if(s->alt_inter_vlc==0 || (cbpc & 3)!=3)
            cbpy ^= 0xF;

        cbp = (cbpc & 3) | (cbpy << 2);
        if (dquant) {
            h263_decode_dquant(s);
        }

        s->mv_dir = MV_DIR_FORWARD;
        if ((cbpc & 16) == 0) {
            s->current_picture.mb_type[xy]= MB_TYPE_16x16 | MB_TYPE_L0;
            /* 16x16 motion prediction */
            s->mv_type = MV_TYPE_16X16;
            h263_pred_motion(s, 0, 0, &pred_x, &pred_y);
            if (s->umvplus)
               mx = h263p_decode_umotion(s, pred_x);
            else
               mx = h263_decode_motion(s, pred_x, 1);

            if (mx >= 0xffff)
                return -1;

            if (s->umvplus)
               my = h263p_decode_umotion(s, pred_y);
            else
               my = h263_decode_motion(s, pred_y, 1);

            if (my >= 0xffff)
                return -1;
            s->mv[0][0][0] = mx;
            s->mv[0][0][1] = my;

            if (s->umvplus && (mx - pred_x) == 1 && (my - pred_y) == 1)
               skip_bits1(&s->gb); /* Bit stuffing to prevent PSC */
        } else {
            s->current_picture.mb_type[xy]= MB_TYPE_8x8 | MB_TYPE_L0;
            s->mv_type = MV_TYPE_8X8;
            for(i=0;i<4;i++) {
                mot_val = h263_pred_motion(s, i, 0, &pred_x, &pred_y);
                if (s->umvplus)
                  mx = h263p_decode_umotion(s, pred_x);
                else
                  mx = h263_decode_motion(s, pred_x, 1);
                if (mx >= 0xffff)
                    return -1;

                if (s->umvplus)
                  my = h263p_decode_umotion(s, pred_y);
                else
                  my = h263_decode_motion(s, pred_y, 1);
                if (my >= 0xffff)
                    return -1;
                s->mv[0][i][0] = mx;
                s->mv[0][i][1] = my;
                if (s->umvplus && (mx - pred_x) == 1 && (my - pred_y) == 1)
                  skip_bits1(&s->gb); /* Bit stuffing to prevent PSC */
                mot_val[0] = mx;
                mot_val[1] = my;
            }
        }
    } else if(s->pict_type==FF_B_TYPE) {
        int mb_type;
        const int stride= s->b8_stride;
        int16_t *mot_val0 = s->current_picture.motion_val[0][ 2*(s->mb_x + s->mb_y*stride) ];
        int16_t *mot_val1 = s->current_picture.motion_val[1][ 2*(s->mb_x + s->mb_y*stride) ];
//        const int mv_xy= s->mb_x + 1 + s->mb_y * s->mb_stride;

        //FIXME ugly
        mot_val0[0       ]= mot_val0[2       ]= mot_val0[0+2*stride]= mot_val0[2+2*stride]=
        mot_val0[1       ]= mot_val0[3       ]= mot_val0[1+2*stride]= mot_val0[3+2*stride]=
        mot_val1[0       ]= mot_val1[2       ]= mot_val1[0+2*stride]= mot_val1[2+2*stride]=
        mot_val1[1       ]= mot_val1[3       ]= mot_val1[1+2*stride]= mot_val1[3+2*stride]= 0;

        do{
            mb_type= get_vlc2(&s->gb, h263_mbtype_b_vlc.table, H263_MBTYPE_B_VLC_BITS, 2);
            if (mb_type < 0){
                av_log(s->avctx, AV_LOG_ERROR, "b mb_type damaged at %d %d\n", s->mb_x, s->mb_y);
                return -1;
            }

            mb_type= h263_mb_type_b_map[ mb_type ];
        }while(!mb_type);

        s->mb_intra = IS_INTRA(mb_type);
        if(HAS_CBP(mb_type)){
            s->dsp.clear_blocks(s->block[0]);
            cbpc = get_vlc2(&s->gb, cbpc_b_vlc.table, CBPC_B_VLC_BITS, 1);
            if(s->mb_intra){
                dquant = IS_QUANT(mb_type);
                goto intra;
            }

            cbpy = get_vlc2(&s->gb, cbpy_vlc.table, CBPY_VLC_BITS, 1);

            if (cbpy < 0){
                av_log(s->avctx, AV_LOG_ERROR, "b cbpy damaged at %d %d\n", s->mb_x, s->mb_y);
                return -1;
            }

            if(s->alt_inter_vlc==0 || (cbpc & 3)!=3)
                cbpy ^= 0xF;

            cbp = (cbpc & 3) | (cbpy << 2);
        }else
            cbp=0;

        assert(!s->mb_intra);

        if(IS_QUANT(mb_type)){
            h263_decode_dquant(s);
        }

        if(IS_DIRECT(mb_type)){
            s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD | MV_DIRECT;
            mb_type |= ff_mpeg4_set_direct_mv(s, 0, 0);
        }else{
            s->mv_dir = 0;
            s->mv_type= MV_TYPE_16X16;
//FIXME UMV

            if(USES_LIST(mb_type, 0)){
                int16_t *mot_val= h263_pred_motion(s, 0, 0, &mx, &my);
                s->mv_dir = MV_DIR_FORWARD;

                mx = h263_decode_motion(s, mx, 1);
                my = h263_decode_motion(s, my, 1);

                s->mv[0][0][0] = mx;
                s->mv[0][0][1] = my;
                mot_val[0       ]= mot_val[2       ]= mot_val[0+2*stride]= mot_val[2+2*stride]= mx;
                mot_val[1       ]= mot_val[3       ]= mot_val[1+2*stride]= mot_val[3+2*stride]= my;
            }

            if(USES_LIST(mb_type, 1)){
                int16_t *mot_val= h263_pred_motion(s, 0, 1, &mx, &my);
                s->mv_dir |= MV_DIR_BACKWARD;

                mx = h263_decode_motion(s, mx, 1);
                my = h263_decode_motion(s, my, 1);

                s->mv[1][0][0] = mx;
                s->mv[1][0][1] = my;
                mot_val[0       ]= mot_val[2       ]= mot_val[0+2*stride]= mot_val[2+2*stride]= mx;
                mot_val[1       ]= mot_val[3       ]= mot_val[1+2*stride]= mot_val[3+2*stride]= my;
            }
        }

        s->current_picture.mb_type[xy]= mb_type;
    } else { /* I-Frame */
        do{
            cbpc = get_vlc2(&s->gb, intra_MCBPC_vlc.table, INTRA_MCBPC_VLC_BITS, 2);
            if (cbpc < 0){
                av_log(s->avctx, AV_LOG_ERROR, "I cbpc damaged at %d %d\n", s->mb_x, s->mb_y);
                return -1;
            }
        }while(cbpc == 8);

        s->dsp.clear_blocks(s->block[0]);

        dquant = cbpc & 4;
        s->mb_intra = 1;
intra:
        s->current_picture.mb_type[xy]= MB_TYPE_INTRA;
        if (s->h263_aic) {
            s->ac_pred = get_bits1(&s->gb);
            if(s->ac_pred){
                s->current_picture.mb_type[xy]= MB_TYPE_INTRA | MB_TYPE_ACPRED;

                s->h263_aic_dir = get_bits1(&s->gb);
            }
        }else
            s->ac_pred = 0;

        if(s->pb_frame && get_bits1(&s->gb))
            pb_mv_count = h263_get_modb(&s->gb, s->pb_frame, &cbpb);
        cbpy = get_vlc2(&s->gb, cbpy_vlc.table, CBPY_VLC_BITS, 1);
        if(cbpy<0){
            av_log(s->avctx, AV_LOG_ERROR, "I cbpy damaged at %d %d\n", s->mb_x, s->mb_y);
            return -1;
        }
        cbp = (cbpc & 3) | (cbpy << 2);
        if (dquant) {
            h263_decode_dquant(s);
        }

        pb_mv_count += !!s->pb_frame;
    }

    while(pb_mv_count--){
        h263_decode_motion(s, 0, 1);
        h263_decode_motion(s, 0, 1);
    }

    /* decode each block */
    for (i = 0; i < 6; i++) {
        if (h263_decode_block(s, block[i], i, cbp&32) < 0)
            return -1;
        cbp+=cbp;
    }

    if(s->pb_frame && h263_skip_b_part(s, cbpb) < 0)
        return -1;
    if(s->obmc && !s->mb_intra){
        if(s->pict_type == FF_P_TYPE && s->mb_x+1<s->mb_width && s->mb_num_left != 1)
            preview_obmc(s);
    }
end:

        /* per-MB end of slice check */
    {
        int v= show_bits(&s->gb, 16);

        if(get_bits_count(&s->gb) + 16 > s->gb.size_in_bits){
            v>>= get_bits_count(&s->gb) + 16 - s->gb.size_in_bits;
        }

        if(v==0)
            return SLICE_END;
    }

    return SLICE_OK;
}

/* most is hardcoded. should extend to handle all h263 streams */
int h263_decode_picture_header(MpegEncContext *s)
{
    int format, width, height, i;
    uint32_t startcode;

    align_get_bits(&s->gb);

    startcode= get_bits(&s->gb, 22-8);

    for(i= get_bits_left(&s->gb); i>24; i-=8) {
        startcode = ((startcode << 8) | get_bits(&s->gb, 8)) & 0x003FFFFF;

        if(startcode == 0x20)
            break;
    }

    if (startcode != 0x20) {
        av_log(s->avctx, AV_LOG_ERROR, "Bad picture start code\n");
        return -1;
    }
    /* temporal reference */
    i = get_bits(&s->gb, 8); /* picture timestamp */
    if( (s->picture_number&~0xFF)+i < s->picture_number)
        i+= 256;
    s->current_picture_ptr->pts=
    s->picture_number= (s->picture_number&~0xFF) + i;

    /* PTYPE starts here */
    if (get_bits1(&s->gb) != 1) {
        /* marker */
        av_log(s->avctx, AV_LOG_ERROR, "Bad marker\n");
        return -1;
    }
    if (get_bits1(&s->gb) != 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Bad H263 id\n");
        return -1;      /* h263 id */
    }
    skip_bits1(&s->gb);         /* split screen off */
    skip_bits1(&s->gb);         /* camera  off */
    skip_bits1(&s->gb);         /* freeze picture release off */

    format = get_bits(&s->gb, 3);
    /*
        0    forbidden
        1    sub-QCIF
        10   QCIF
        7       extended PTYPE (PLUSPTYPE)
    */

    if (format != 7 && format != 6) {
        s->h263_plus = 0;
        /* H.263v1 */
        width = h263_format[format][0];
        height = h263_format[format][1];
        if (!width)
            return -1;

        s->pict_type = FF_I_TYPE + get_bits1(&s->gb);

        s->h263_long_vectors = get_bits1(&s->gb);

        if (get_bits1(&s->gb) != 0) {
            av_log(s->avctx, AV_LOG_ERROR, "H263 SAC not supported\n");
            return -1; /* SAC: off */
        }
        s->obmc= get_bits1(&s->gb); /* Advanced prediction mode */
        s->unrestricted_mv = s->h263_long_vectors || s->obmc;

        s->pb_frame = get_bits1(&s->gb);
        s->chroma_qscale= s->qscale = get_bits(&s->gb, 5);
        skip_bits1(&s->gb); /* Continuous Presence Multipoint mode: off */

        s->width = width;
        s->height = height;
        s->avctx->sample_aspect_ratio= (AVRational){12,11};
        s->avctx->time_base= (AVRational){1001, 30000};
    } else {
        int ufep;

        /* H.263v2 */
        s->h263_plus = 1;
        ufep = get_bits(&s->gb, 3); /* Update Full Extended PTYPE */

        /* ufep other than 0 and 1 are reserved */
        if (ufep == 1) {
            /* OPPTYPE */
            format = get_bits(&s->gb, 3);
            dprintf(s->avctx, "ufep=1, format: %d\n", format);
            s->custom_pcf= get_bits1(&s->gb);
            s->umvplus = get_bits1(&s->gb); /* Unrestricted Motion Vector */
            if (get_bits1(&s->gb) != 0) {
                av_log(s->avctx, AV_LOG_ERROR, "Syntax-based Arithmetic Coding (SAC) not supported\n");
            }
            s->obmc= get_bits1(&s->gb); /* Advanced prediction mode */
            s->h263_aic = get_bits1(&s->gb); /* Advanced Intra Coding (AIC) */
            s->loop_filter= get_bits1(&s->gb);
            s->unrestricted_mv = s->umvplus || s->obmc || s->loop_filter;

            s->h263_slice_structured= get_bits1(&s->gb);
            if (get_bits1(&s->gb) != 0) {
                av_log(s->avctx, AV_LOG_ERROR, "Reference Picture Selection not supported\n");
            }
            if (get_bits1(&s->gb) != 0) {
                av_log(s->avctx, AV_LOG_ERROR, "Independent Segment Decoding not supported\n");
            }
            s->alt_inter_vlc= get_bits1(&s->gb);
            s->modified_quant= get_bits1(&s->gb);
            if(s->modified_quant)
                s->chroma_qscale_table= ff_h263_chroma_qscale_table;

            skip_bits(&s->gb, 1); /* Prevent start code emulation */

            skip_bits(&s->gb, 3); /* Reserved */
        } else if (ufep != 0) {
            av_log(s->avctx, AV_LOG_ERROR, "Bad UFEP type (%d)\n", ufep);
            return -1;
        }

        /* MPPTYPE */
        s->pict_type = get_bits(&s->gb, 3);
        switch(s->pict_type){
        case 0: s->pict_type= FF_I_TYPE;break;
        case 1: s->pict_type= FF_P_TYPE;break;
        case 2: s->pict_type= FF_P_TYPE;s->pb_frame = 3;break;
        case 3: s->pict_type= FF_B_TYPE;break;
        case 7: s->pict_type= FF_I_TYPE;break; //ZYGO
        default:
            return -1;
        }
        skip_bits(&s->gb, 2);
        s->no_rounding = get_bits1(&s->gb);
        skip_bits(&s->gb, 4);

        /* Get the picture dimensions */
        if (ufep) {
            if (format == 6) {
                /* Custom Picture Format (CPFMT) */
                s->aspect_ratio_info = get_bits(&s->gb, 4);
                dprintf(s->avctx, "aspect: %d\n", s->aspect_ratio_info);
                /* aspect ratios:
                0 - forbidden
                1 - 1:1
                2 - 12:11 (CIF 4:3)
                3 - 10:11 (525-type 4:3)
                4 - 16:11 (CIF 16:9)
                5 - 40:33 (525-type 16:9)
                6-14 - reserved
                */
                width = (get_bits(&s->gb, 9) + 1) * 4;
                skip_bits1(&s->gb);
                height = get_bits(&s->gb, 9) * 4;
                dprintf(s->avctx, "\nH.263+ Custom picture: %dx%d\n",width,height);
                if (s->aspect_ratio_info == FF_ASPECT_EXTENDED) {
                    /* aspected dimensions */
                    s->avctx->sample_aspect_ratio.num= get_bits(&s->gb, 8);
                    s->avctx->sample_aspect_ratio.den= get_bits(&s->gb, 8);
                }else{
                    s->avctx->sample_aspect_ratio= ff_h263_pixel_aspect[s->aspect_ratio_info];
                }
            } else {
                width = h263_format[format][0];
                height = h263_format[format][1];
                s->avctx->sample_aspect_ratio= (AVRational){12,11};
            }
            if ((width == 0) || (height == 0))
                return -1;
            s->width = width;
            s->height = height;

            if(s->custom_pcf){
                int gcd;
                s->avctx->time_base.den= 1800000;
                s->avctx->time_base.num= 1000 + get_bits1(&s->gb);
                s->avctx->time_base.num*= get_bits(&s->gb, 7);
                if(s->avctx->time_base.num == 0){
                    av_log(s, AV_LOG_ERROR, "zero framerate\n");
                    return -1;
                }
                gcd= av_gcd(s->avctx->time_base.den, s->avctx->time_base.num);
                s->avctx->time_base.den /= gcd;
                s->avctx->time_base.num /= gcd;
            }else{
                s->avctx->time_base= (AVRational){1001, 30000};
            }
        }

        if(s->custom_pcf){
            skip_bits(&s->gb, 2); //extended Temporal reference
        }

        if (ufep) {
            if (s->umvplus) {
                if(get_bits1(&s->gb)==0) /* Unlimited Unrestricted Motion Vectors Indicator (UUI) */
                    skip_bits1(&s->gb);
            }
            if(s->h263_slice_structured){
                if (get_bits1(&s->gb) != 0) {
                    av_log(s->avctx, AV_LOG_ERROR, "rectangular slices not supported\n");
                }
                if (get_bits1(&s->gb) != 0) {
                    av_log(s->avctx, AV_LOG_ERROR, "unordered slices not supported\n");
                }
            }
        }

        s->qscale = get_bits(&s->gb, 5);
    }

    s->mb_width = (s->width  + 15) / 16;
    s->mb_height = (s->height  + 15) / 16;
    s->mb_num = s->mb_width * s->mb_height;

    if (s->pb_frame) {
        skip_bits(&s->gb, 3); /* Temporal reference for B-pictures */
        if (s->custom_pcf)
            skip_bits(&s->gb, 2); //extended Temporal reference
        skip_bits(&s->gb, 2); /* Quantization information for B-pictures */
    }

    /* PEI */
    while (get_bits1(&s->gb) != 0) {
        skip_bits(&s->gb, 8);
    }

    if(s->h263_slice_structured){
        if (get_bits1(&s->gb) != 1) {
            av_log(s->avctx, AV_LOG_ERROR, "SEPB1 marker missing\n");
            return -1;
        }

        ff_h263_decode_mba(s);

        if (get_bits1(&s->gb) != 1) {
            av_log(s->avctx, AV_LOG_ERROR, "SEPB2 marker missing\n");
            return -1;
        }
    }
    s->f_code = 1;

    if(s->h263_aic){
         s->y_dc_scale_table=
         s->c_dc_scale_table= ff_aic_dc_scale_table;
    }else{
        s->y_dc_scale_table=
        s->c_dc_scale_table= ff_mpeg1_dc_scale_table;
    }

        ff_h263_show_pict_info(s);
    if (s->pict_type == FF_I_TYPE && s->codec_tag == AV_RL32("ZYGO")){
        int i,j;
        for(i=0; i<85; i++) av_log(s->avctx, AV_LOG_DEBUG, "%d", get_bits1(&s->gb));
        av_log(s->avctx, AV_LOG_DEBUG, "\n");
        for(i=0; i<13; i++){
            for(j=0; j<3; j++){
                int v= get_bits(&s->gb, 8);
                v |= get_sbits(&s->gb, 8)<<8;
                av_log(s->avctx, AV_LOG_DEBUG, " %5d", v);
            }
            av_log(s->avctx, AV_LOG_DEBUG, "\n");
        }
        for(i=0; i<50; i++) av_log(s->avctx, AV_LOG_DEBUG, "%d", get_bits1(&s->gb));
    }

    return 0;
}


