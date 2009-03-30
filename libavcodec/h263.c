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
#include "h263data.h"
#include "mpeg4data.h"
#include "mathops.h"
#include "unary.h"

//#undef NDEBUG
//#include <assert.h>

#define INTRA_MCBPC_VLC_BITS 6
#define INTER_MCBPC_VLC_BITS 7
#define CBPY_VLC_BITS 6
#define MV_VLC_BITS 9
#define DC_VLC_BITS 9
#define SPRITE_TRAJ_VLC_BITS 6
#define MB_TYPE_B_VLC_BITS 4
#define TEX_VLC_BITS 9
#define H263_MBTYPE_B_VLC_BITS 6
#define CBPC_B_VLC_BITS 3

static void h263_encode_block(MpegEncContext * s, DCTELEM * block,
                              int n);
static void h263p_encode_umotion(MpegEncContext * s, int val);
static inline void mpeg4_encode_block(MpegEncContext * s, DCTELEM * block,
                               int n, int dc, uint8_t *scan_table,
                               PutBitContext *dc_pb, PutBitContext *ac_pb);
static int mpeg4_get_block_length(MpegEncContext * s, DCTELEM * block, int n, int intra_dc,
                                  uint8_t *scan_table);

static int h263_decode_motion(MpegEncContext * s, int pred, int fcode);
static int h263p_decode_umotion(MpegEncContext * s, int pred);
static int h263_decode_block(MpegEncContext * s, DCTELEM * block,
                             int n, int coded);
static inline int mpeg4_decode_dc(MpegEncContext * s, int n, int *dir_ptr);
static inline int mpeg4_decode_block(MpegEncContext * s, DCTELEM * block,
                              int n, int coded, int intra, int rvlc);

static int h263_pred_dc(MpegEncContext * s, int n, int16_t **dc_val_ptr);
static void mpeg4_encode_visual_object_header(MpegEncContext * s);
static void mpeg4_encode_vol_header(MpegEncContext * s, int vo_number, int vol_number);

static void mpeg4_decode_sprite_trajectory(MpegEncContext * s, GetBitContext *gb);
static inline int ff_mpeg4_pred_dc(MpegEncContext * s, int n, int level, int *dir_ptr, int encoding);

#if CONFIG_ENCODERS
static uint8_t uni_DCtab_lum_len[512];
static uint8_t uni_DCtab_chrom_len[512];
static uint16_t uni_DCtab_lum_bits[512];
static uint16_t uni_DCtab_chrom_bits[512];

static uint8_t mv_penalty[MAX_FCODE+1][MAX_MV*2+1];
static uint8_t fcode_tab[MAX_MV*2+1];
static uint8_t umv_fcode_tab[MAX_MV*2+1];

static uint32_t uni_mpeg4_intra_rl_bits[64*64*2*2];
static uint8_t  uni_mpeg4_intra_rl_len [64*64*2*2];
static uint32_t uni_mpeg4_inter_rl_bits[64*64*2*2];
static uint8_t  uni_mpeg4_inter_rl_len [64*64*2*2];
static uint8_t  uni_h263_intra_aic_rl_len [64*64*2*2];
static uint8_t  uni_h263_inter_rl_len [64*64*2*2];
//#define UNI_MPEG4_ENC_INDEX(last,run,level) ((last)*128 + (run)*256 + (level))
//#define UNI_MPEG4_ENC_INDEX(last,run,level) ((last)*128*64 + (run) + (level)*64)
#define UNI_MPEG4_ENC_INDEX(last,run,level) ((last)*128*64 + (run)*128 + (level))

/* mpeg4
inter
max level: 24/6
max run: 53/63

intra
max level: 53/16
max run: 29/41
*/
#endif

static uint8_t static_rl_table_store[5][2][2*MAX_RUN + MAX_LEVEL + 3];

#if 0 //3IV1 is quite rare and it slows things down a tiny bit
#define IS_3IV1 s->codec_tag == AV_RL32("3IV1")
#else
#define IS_3IV1 0
#endif

int h263_get_picture_format(int width, int height)
{
    int format;

    if (width == 128 && height == 96)
        format = 1;
    else if (width == 176 && height == 144)
        format = 2;
    else if (width == 352 && height == 288)
        format = 3;
    else if (width == 704 && height == 576)
        format = 4;
    else if (width == 1408 && height == 1152)
        format = 5;
    else
        format = 7;
    return format;
}

static void show_pict_info(MpegEncContext *s){
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

#if CONFIG_ENCODERS

static void aspect_to_info(MpegEncContext * s, AVRational aspect){
    int i;

    if(aspect.num==0) aspect= (AVRational){1,1};

    for(i=1; i<6; i++){
        if(av_cmp_q(pixel_aspect[i], aspect) == 0){
            s->aspect_ratio_info=i;
            return;
        }
    }

    s->aspect_ratio_info= FF_ASPECT_EXTENDED;
}

void ff_flv_encode_picture_header(MpegEncContext * s, int picture_number)
{
      int format;

      align_put_bits(&s->pb);

      put_bits(&s->pb, 17, 1);
      put_bits(&s->pb, 5, (s->h263_flv-1)); /* 0: h263 escape codes 1: 11-bit escape codes */
      put_bits(&s->pb, 8, (((int64_t)s->picture_number * 30 * s->avctx->time_base.num) / //FIXME use timestamp
                           s->avctx->time_base.den) & 0xff); /* TemporalReference */
      if (s->width == 352 && s->height == 288)
        format = 2;
      else if (s->width == 176 && s->height == 144)
        format = 3;
      else if (s->width == 128 && s->height == 96)
        format = 4;
      else if (s->width == 320 && s->height == 240)
        format = 5;
      else if (s->width == 160 && s->height == 120)
        format = 6;
      else if (s->width <= 255 && s->height <= 255)
        format = 0; /* use 1 byte width & height */
      else
        format = 1; /* use 2 bytes width & height */
      put_bits(&s->pb, 3, format); /* PictureSize */
      if (format == 0) {
        put_bits(&s->pb, 8, s->width);
        put_bits(&s->pb, 8, s->height);
      } else if (format == 1) {
        put_bits(&s->pb, 16, s->width);
        put_bits(&s->pb, 16, s->height);
      }
      put_bits(&s->pb, 2, s->pict_type == FF_P_TYPE); /* PictureType */
      put_bits(&s->pb, 1, 1); /* DeblockingFlag: on */
      put_bits(&s->pb, 5, s->qscale); /* Quantizer */
      put_bits(&s->pb, 1, 0); /* ExtraInformation */

      if(s->h263_aic){
        s->y_dc_scale_table=
          s->c_dc_scale_table= ff_aic_dc_scale_table;
      }else{
        s->y_dc_scale_table=
          s->c_dc_scale_table= ff_mpeg1_dc_scale_table;
      }
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
    s->ptr_lastgob = pbBufPtr(&s->pb);
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
            aspect_to_info(s, s->avctx->sample_aspect_ratio);

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

static inline int get_block_rate(MpegEncContext * s, DCTELEM block[64], int block_last_index, uint8_t scantable[64]){
    int last=0;
    int j;
    int rate=0;

    for(j=1; j<=block_last_index; j++){
        const int index= scantable[j];
        int level= block[index];
        if(level){
            level+= 64;
            if((level&(~127)) == 0){
                if(j<block_last_index) rate+= s->intra_ac_vlc_length     [UNI_AC_ENC_INDEX(j-last-1, level)];
                else                   rate+= s->intra_ac_vlc_last_length[UNI_AC_ENC_INDEX(j-last-1, level)];
            }else
                rate += s->ac_esc_length;
            level-= 64;

            last= j;
        }
    }

    return rate;
}

static inline int decide_ac_pred(MpegEncContext * s, DCTELEM block[6][64], int dir[6], uint8_t *st[6], int zigzag_last_index[6])
{
    int score= 0;
    int i, n;
    int8_t * const qscale_table= s->current_picture.qscale_table;

    memcpy(zigzag_last_index, s->block_last_index, sizeof(int)*6);

    for(n=0; n<6; n++){
        int16_t *ac_val, *ac_val1;

        score -= get_block_rate(s, block[n], s->block_last_index[n], s->intra_scantable.permutated);

        ac_val = s->ac_val[0][0] + s->block_index[n] * 16;
        ac_val1= ac_val;
        if(dir[n]){
            const int xy= s->mb_x + s->mb_y*s->mb_stride - s->mb_stride;
            /* top prediction */
            ac_val-= s->block_wrap[n]*16;
            if(s->mb_y==0 || s->qscale == qscale_table[xy] || n==2 || n==3){
                /* same qscale */
                for(i=1; i<8; i++){
                    const int level= block[n][s->dsp.idct_permutation[i   ]];
                    block[n][s->dsp.idct_permutation[i   ]] = level - ac_val[i+8];
                    ac_val1[i  ]=    block[n][s->dsp.idct_permutation[i<<3]];
                    ac_val1[i+8]= level;
                }
            }else{
                /* different qscale, we must rescale */
                for(i=1; i<8; i++){
                    const int level= block[n][s->dsp.idct_permutation[i   ]];
                    block[n][s->dsp.idct_permutation[i   ]] = level - ROUNDED_DIV(ac_val[i + 8]*qscale_table[xy], s->qscale);
                    ac_val1[i  ]=    block[n][s->dsp.idct_permutation[i<<3]];
                    ac_val1[i+8]= level;
                }
            }
            st[n]= s->intra_h_scantable.permutated;
        }else{
            const int xy= s->mb_x-1 + s->mb_y*s->mb_stride;
            /* left prediction */
            ac_val-= 16;
            if(s->mb_x==0 || s->qscale == qscale_table[xy] || n==1 || n==3){
                /* same qscale */
                for(i=1; i<8; i++){
                    const int level= block[n][s->dsp.idct_permutation[i<<3]];
                    block[n][s->dsp.idct_permutation[i<<3]]= level - ac_val[i];
                    ac_val1[i  ]= level;
                    ac_val1[i+8]=    block[n][s->dsp.idct_permutation[i   ]];
                }
            }else{
                /* different qscale, we must rescale */
                for(i=1; i<8; i++){
                    const int level= block[n][s->dsp.idct_permutation[i<<3]];
                    block[n][s->dsp.idct_permutation[i<<3]]= level - ROUNDED_DIV(ac_val[i]*qscale_table[xy], s->qscale);
                    ac_val1[i  ]= level;
                    ac_val1[i+8]=    block[n][s->dsp.idct_permutation[i   ]];
                }
            }
            st[n]= s->intra_v_scantable.permutated;
        }

        for(i=63; i>0; i--) //FIXME optimize
            if(block[n][ st[n][i] ]) break;
        s->block_last_index[n]= i;

        score += get_block_rate(s, block[n], s->block_last_index[n], st[n]);
    }

    return score < 0;
}

static inline void restore_ac_coeffs(MpegEncContext * s, DCTELEM block[6][64], int dir[6], uint8_t *st[6], int zigzag_last_index[6])
{
    int i, n;
    memcpy(s->block_last_index, zigzag_last_index, sizeof(int)*6);

    for(n=0; n<6; n++){
        int16_t *ac_val = s->ac_val[0][0] + s->block_index[n] * 16;

        st[n]= s->intra_scantable.permutated;
        if(dir[n]){
            /* top prediction */
            for(i=1; i<8; i++){
                block[n][s->dsp.idct_permutation[i   ]] = ac_val[i+8];
            }
        }else{
            /* left prediction */
            for(i=1; i<8; i++){
                block[n][s->dsp.idct_permutation[i<<3]]= ac_val[i  ];
            }
        }
    }
}

/**
 * init s->current_picture.qscale_table from s->lambda_table
 */
static void ff_init_qscale_tab(MpegEncContext *s){
    int8_t * const qscale_table= s->current_picture.qscale_table;
    int i;

    for(i=0; i<s->mb_num; i++){
        unsigned int lam= s->lambda_table[ s->mb_index2xy[i] ];
        int qp= (lam*139 + FF_LAMBDA_SCALE*64) >> (FF_LAMBDA_SHIFT + 7);
        qscale_table[ s->mb_index2xy[i] ]= av_clip(qp, s->avctx->qmin, s->avctx->qmax);
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

/**
 * modify mb_type & qscale so that encoding is acually possible in mpeg4
 */
void ff_clean_mpeg4_qscales(MpegEncContext *s){
    int i;
    int8_t * const qscale_table= s->current_picture.qscale_table;

    ff_clean_h263_qscales(s);

    if(s->pict_type== FF_B_TYPE){
        int odd=0;
        /* ok, come on, this isn't funny anymore, there's more code for handling this mpeg4 mess than for the actual adaptive quantization */

        for(i=0; i<s->mb_num; i++){
            int mb_xy= s->mb_index2xy[i];
            odd += qscale_table[mb_xy]&1;
        }

        if(2*odd > s->mb_num) odd=1;
        else                  odd=0;

        for(i=0; i<s->mb_num; i++){
            int mb_xy= s->mb_index2xy[i];
            if((qscale_table[mb_xy]&1) != odd)
                qscale_table[mb_xy]++;
            if(qscale_table[mb_xy] > 31)
                qscale_table[mb_xy]= 31;
        }

        for(i=1; i<s->mb_num; i++){
            int mb_xy= s->mb_index2xy[i];
            if(qscale_table[mb_xy] != qscale_table[s->mb_index2xy[i-1]] && (s->mb_type[mb_xy]&CANDIDATE_MB_TYPE_DIRECT)){
                s->mb_type[mb_xy]|= CANDIDATE_MB_TYPE_BIDIR;
            }
        }
    }
}

#endif //CONFIG_ENCODERS

#define tab_size ((signed)FF_ARRAY_ELEMS(s->direct_scale_mv[0]))
#define tab_bias (tab_size/2)

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
    uint16_t time_pp= s->pp_time;
    uint16_t time_pb= s->pb_time;
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

static inline int h263_get_motion_length(MpegEncContext * s, int val, int f_code){
    int l, bit_size, code;

    if (val == 0) {
        return mvtab[0][1];
    } else {
        bit_size = f_code - 1;
        /* modulo encoding */
        l= INT_BIT - 6 - bit_size;
        val = (val<<l)>>l;
        val--;
        code = (val >> bit_size) + 1;

        return mvtab[code][1] + 1 + bit_size;
    }
}

static inline void ff_h263_encode_motion_vector(MpegEncContext * s, int x, int y, int f_code){
    if(s->flags2 & CODEC_FLAG2_NO_OUTPUT){
        skip_put_bits(&s->pb,
            h263_get_motion_length(s, x, f_code)
           +h263_get_motion_length(s, y, f_code));
    }else{
        ff_h263_encode_motion(s, x, f_code);
        ff_h263_encode_motion(s, y, f_code);
    }
}

static inline int get_p_cbp(MpegEncContext * s,
                      DCTELEM block[6][64],
                      int motion_x, int motion_y){
    int cbp, i;

    if(s->flags & CODEC_FLAG_CBP_RD){
        int best_cbpy_score= INT_MAX;
        int best_cbpc_score= INT_MAX;
        int cbpc = (-1), cbpy= (-1);
        const int offset= (s->mv_type==MV_TYPE_16X16 ? 0 : 16) + (s->dquant ? 8 : 0);
        const int lambda= s->lambda2 >> (FF_LAMBDA_SHIFT - 6);

        for(i=0; i<4; i++){
            int score= inter_MCBPC_bits[i + offset] * lambda;
            if(i&1) score += s->coded_score[5];
            if(i&2) score += s->coded_score[4];

            if(score < best_cbpc_score){
                best_cbpc_score= score;
                cbpc= i;
            }
        }

        for(i=0; i<16; i++){
            int score= cbpy_tab[i ^ 0xF][1] * lambda;
            if(i&1) score += s->coded_score[3];
            if(i&2) score += s->coded_score[2];
            if(i&4) score += s->coded_score[1];
            if(i&8) score += s->coded_score[0];

            if(score < best_cbpy_score){
                best_cbpy_score= score;
                cbpy= i;
            }
        }
        cbp= cbpc + 4*cbpy;
        if ((motion_x | motion_y | s->dquant) == 0 && s->mv_type==MV_TYPE_16X16){
            if(best_cbpy_score + best_cbpc_score + 2*lambda >= 0)
                cbp= 0;
        }

        for (i = 0; i < 6; i++) {
            if (s->block_last_index[i] >= 0 && ((cbp >> (5 - i))&1)==0 ){
                s->block_last_index[i]= -1;
                s->dsp.clear_block(s->block[i]);
            }
        }
    }else{
        cbp= 0;
        for (i = 0; i < 6; i++) {
            if (s->block_last_index[i] >= 0)
                cbp |= 1 << (5 - i);
        }
    }
    return cbp;
}

static inline int get_b_cbp(MpegEncContext * s, DCTELEM block[6][64],
                            int motion_x, int motion_y, int mb_type){
    int cbp=0, i;

    if(s->flags & CODEC_FLAG_CBP_RD){
        int score=0;
        const int lambda= s->lambda2 >> (FF_LAMBDA_SHIFT - 6);

        for(i=0; i<6; i++){
            if(s->coded_score[i] < 0){
                score += s->coded_score[i];
                cbp |= 1 << (5 - i);
            }
        }

        if(cbp){
            int zero_score= -6;
            if ((motion_x | motion_y | s->dquant | mb_type) == 0){
                zero_score-= 4; //2*MV + mb_type + cbp bit
            }

            zero_score*= lambda;
            if(zero_score <= score){
                cbp=0;
            }
        }

        for (i = 0; i < 6; i++) {
            if (s->block_last_index[i] >= 0 && ((cbp >> (5 - i))&1)==0 ){
                s->block_last_index[i]= -1;
                s->dsp.clear_block(s->block[i]);
            }
        }
    }else{
        for (i = 0; i < 6; i++) {
            if (s->block_last_index[i] >= 0)
                cbp |= 1 << (5 - i);
        }
    }
    return cbp;
}

static inline void mpeg4_encode_blocks(MpegEncContext * s, DCTELEM block[6][64], int intra_dc[6],
                               uint8_t **scan_table, PutBitContext *dc_pb, PutBitContext *ac_pb){
    int i;

    if(scan_table){
        if(s->flags2 & CODEC_FLAG2_NO_OUTPUT){
            for (i = 0; i < 6; i++) {
                skip_put_bits(&s->pb, mpeg4_get_block_length(s, block[i], i, intra_dc[i], scan_table[i]));
            }
        }else{
            /* encode each block */
            for (i = 0; i < 6; i++) {
                mpeg4_encode_block(s, block[i], i, intra_dc[i], scan_table[i], dc_pb, ac_pb);
            }
        }
    }else{
        if(s->flags2 & CODEC_FLAG2_NO_OUTPUT){
            for (i = 0; i < 6; i++) {
                skip_put_bits(&s->pb, mpeg4_get_block_length(s, block[i], i, 0, s->intra_scantable.permutated));
            }
        }else{
            /* encode each block */
            for (i = 0; i < 6; i++) {
                mpeg4_encode_block(s, block[i], i, 0, s->intra_scantable.permutated, dc_pb, ac_pb);
            }
        }
    }
}

static const int dquant_code[5]= {1,0,9,2,3};

void mpeg4_encode_mb(MpegEncContext * s,
                    DCTELEM block[6][64],
                    int motion_x, int motion_y)
{
    int cbpc, cbpy, pred_x, pred_y;
    PutBitContext * const pb2    = s->data_partitioning                         ? &s->pb2    : &s->pb;
    PutBitContext * const tex_pb = s->data_partitioning && s->pict_type!=FF_B_TYPE ? &s->tex_pb : &s->pb;
    PutBitContext * const dc_pb  = s->data_partitioning && s->pict_type!=FF_I_TYPE ? &s->pb2    : &s->pb;
    const int interleaved_stats= (s->flags&CODEC_FLAG_PASS1) && !s->data_partitioning ? 1 : 0;

    //    printf("**mb x=%d y=%d\n", s->mb_x, s->mb_y);
    if (!s->mb_intra) {
        int i, cbp;

        if(s->pict_type==FF_B_TYPE){
            static const int mb_type_table[8]= {-1, 3, 2, 1,-1,-1,-1, 0}; /* convert from mv_dir to type */
            int mb_type=  mb_type_table[s->mv_dir];

            if(s->mb_x==0){
                for(i=0; i<2; i++){
                    s->last_mv[i][0][0]=
                    s->last_mv[i][0][1]=
                    s->last_mv[i][1][0]=
                    s->last_mv[i][1][1]= 0;
                }
            }

            assert(s->dquant>=-2 && s->dquant<=2);
            assert((s->dquant&1)==0);
            assert(mb_type>=0);

            /* nothing to do if this MB was skipped in the next P Frame */
            if(s->next_picture.mbskip_table[s->mb_y * s->mb_stride + s->mb_x]){ //FIXME avoid DCT & ...
                s->skip_count++;
                s->mv[0][0][0]=
                s->mv[0][0][1]=
                s->mv[1][0][0]=
                s->mv[1][0][1]= 0;
                s->mv_dir= MV_DIR_FORWARD; //doesn't matter
                s->qscale -= s->dquant;
//                s->mb_skipped=1;

                return;
            }

            cbp= get_b_cbp(s, block, motion_x, motion_y, mb_type);

            if ((cbp | motion_x | motion_y | mb_type) ==0) {
                /* direct MB with MV={0,0} */
                assert(s->dquant==0);

                put_bits(&s->pb, 1, 1); /* mb not coded modb1=1 */

                if(interleaved_stats){
                    s->misc_bits++;
                    s->last_bits++;
                }
                s->skip_count++;
                return;
            }

            put_bits(&s->pb, 1, 0);     /* mb coded modb1=0 */
            put_bits(&s->pb, 1, cbp ? 0 : 1); /* modb2 */ //FIXME merge
            put_bits(&s->pb, mb_type+1, 1); // this table is so simple that we don't need it :)
            if(cbp) put_bits(&s->pb, 6, cbp);

            if(cbp && mb_type){
                if(s->dquant)
                    put_bits(&s->pb, 2, (s->dquant>>2)+3);
                else
                    put_bits(&s->pb, 1, 0);
            }else
                s->qscale -= s->dquant;

            if(!s->progressive_sequence){
                if(cbp)
                    put_bits(&s->pb, 1, s->interlaced_dct);
                if(mb_type) // not direct mode
                    put_bits(&s->pb, 1, s->mv_type == MV_TYPE_FIELD);
            }

            if(interleaved_stats){
                s->misc_bits+= get_bits_diff(s);
            }

            if(mb_type == 0){
                assert(s->mv_dir & MV_DIRECT);
                ff_h263_encode_motion_vector(s, motion_x, motion_y, 1);
                s->b_count++;
                s->f_count++;
            }else{
                assert(mb_type > 0 && mb_type < 4);
                if(s->mv_type != MV_TYPE_FIELD){
                    if(s->mv_dir & MV_DIR_FORWARD){
                        ff_h263_encode_motion_vector(s, s->mv[0][0][0] - s->last_mv[0][0][0],
                                                        s->mv[0][0][1] - s->last_mv[0][0][1], s->f_code);
                        s->last_mv[0][0][0]= s->last_mv[0][1][0]= s->mv[0][0][0];
                        s->last_mv[0][0][1]= s->last_mv[0][1][1]= s->mv[0][0][1];
                        s->f_count++;
                    }
                    if(s->mv_dir & MV_DIR_BACKWARD){
                        ff_h263_encode_motion_vector(s, s->mv[1][0][0] - s->last_mv[1][0][0],
                                                        s->mv[1][0][1] - s->last_mv[1][0][1], s->b_code);
                        s->last_mv[1][0][0]= s->last_mv[1][1][0]= s->mv[1][0][0];
                        s->last_mv[1][0][1]= s->last_mv[1][1][1]= s->mv[1][0][1];
                        s->b_count++;
                    }
                }else{
                    if(s->mv_dir & MV_DIR_FORWARD){
                        put_bits(&s->pb, 1, s->field_select[0][0]);
                        put_bits(&s->pb, 1, s->field_select[0][1]);
                    }
                    if(s->mv_dir & MV_DIR_BACKWARD){
                        put_bits(&s->pb, 1, s->field_select[1][0]);
                        put_bits(&s->pb, 1, s->field_select[1][1]);
                    }
                    if(s->mv_dir & MV_DIR_FORWARD){
                        for(i=0; i<2; i++){
                            ff_h263_encode_motion_vector(s, s->mv[0][i][0] - s->last_mv[0][i][0]  ,
                                                            s->mv[0][i][1] - s->last_mv[0][i][1]/2, s->f_code);
                            s->last_mv[0][i][0]= s->mv[0][i][0];
                            s->last_mv[0][i][1]= s->mv[0][i][1]*2;
                        }
                        s->f_count++;
                    }
                    if(s->mv_dir & MV_DIR_BACKWARD){
                        for(i=0; i<2; i++){
                            ff_h263_encode_motion_vector(s, s->mv[1][i][0] - s->last_mv[1][i][0]  ,
                                                            s->mv[1][i][1] - s->last_mv[1][i][1]/2, s->b_code);
                            s->last_mv[1][i][0]= s->mv[1][i][0];
                            s->last_mv[1][i][1]= s->mv[1][i][1]*2;
                        }
                        s->b_count++;
                    }
                }
            }

            if(interleaved_stats){
                s->mv_bits+= get_bits_diff(s);
            }

            mpeg4_encode_blocks(s, block, NULL, NULL, NULL, &s->pb);

            if(interleaved_stats){
                s->p_tex_bits+= get_bits_diff(s);
            }

        }else{ /* s->pict_type==FF_B_TYPE */
            cbp= get_p_cbp(s, block, motion_x, motion_y);

            if ((cbp | motion_x | motion_y | s->dquant) == 0 && s->mv_type==MV_TYPE_16X16) {
                /* check if the B frames can skip it too, as we must skip it if we skip here
                   why didn't they just compress the skip-mb bits instead of reusing them ?! */
                if(s->max_b_frames>0){
                    int i;
                    int x,y, offset;
                    uint8_t *p_pic;

                    x= s->mb_x*16;
                    y= s->mb_y*16;
                    if(x+16 > s->width)  x= s->width-16;
                    if(y+16 > s->height) y= s->height-16;

                    offset= x + y*s->linesize;
                    p_pic= s->new_picture.data[0] + offset;

                    s->mb_skipped=1;
                    for(i=0; i<s->max_b_frames; i++){
                        uint8_t *b_pic;
                        int diff;
                        Picture *pic= s->reordered_input_picture[i+1];

                        if(pic==NULL || pic->pict_type!=FF_B_TYPE) break;

                        b_pic= pic->data[0] + offset;
                        if(pic->type != FF_BUFFER_TYPE_SHARED)
                            b_pic+= INPLACE_OFFSET;
                        diff= s->dsp.sad[0](NULL, p_pic, b_pic, s->linesize, 16);
                        if(diff>s->qscale*70){ //FIXME check that 70 is optimal
                            s->mb_skipped=0;
                            break;
                        }
                    }
                }else
                    s->mb_skipped=1;

                if(s->mb_skipped==1){
                    /* skip macroblock */
                    put_bits(&s->pb, 1, 1);

                    if(interleaved_stats){
                        s->misc_bits++;
                        s->last_bits++;
                    }
                    s->skip_count++;

                    return;
                }
            }

            put_bits(&s->pb, 1, 0);     /* mb coded */
            cbpc = cbp & 3;
            cbpy = cbp >> 2;
            cbpy ^= 0xf;
            if(s->mv_type==MV_TYPE_16X16){
                if(s->dquant) cbpc+= 8;
                put_bits(&s->pb,
                        inter_MCBPC_bits[cbpc],
                        inter_MCBPC_code[cbpc]);

                put_bits(pb2, cbpy_tab[cbpy][1], cbpy_tab[cbpy][0]);
                if(s->dquant)
                    put_bits(pb2, 2, dquant_code[s->dquant+2]);

                if(!s->progressive_sequence){
                    if(cbp)
                        put_bits(pb2, 1, s->interlaced_dct);
                    put_bits(pb2, 1, 0);
                }

                if(interleaved_stats){
                    s->misc_bits+= get_bits_diff(s);
                }

                /* motion vectors: 16x16 mode */
                h263_pred_motion(s, 0, 0, &pred_x, &pred_y);

                ff_h263_encode_motion_vector(s, motion_x - pred_x,
                                                motion_y - pred_y, s->f_code);
            }else if(s->mv_type==MV_TYPE_FIELD){
                if(s->dquant) cbpc+= 8;
                put_bits(&s->pb,
                        inter_MCBPC_bits[cbpc],
                        inter_MCBPC_code[cbpc]);

                put_bits(pb2, cbpy_tab[cbpy][1], cbpy_tab[cbpy][0]);
                if(s->dquant)
                    put_bits(pb2, 2, dquant_code[s->dquant+2]);

                assert(!s->progressive_sequence);
                if(cbp)
                    put_bits(pb2, 1, s->interlaced_dct);
                put_bits(pb2, 1, 1);

                if(interleaved_stats){
                    s->misc_bits+= get_bits_diff(s);
                }

                /* motion vectors: 16x8 interlaced mode */
                h263_pred_motion(s, 0, 0, &pred_x, &pred_y);
                pred_y /=2;

                put_bits(&s->pb, 1, s->field_select[0][0]);
                put_bits(&s->pb, 1, s->field_select[0][1]);

                ff_h263_encode_motion_vector(s, s->mv[0][0][0] - pred_x,
                                                s->mv[0][0][1] - pred_y, s->f_code);
                ff_h263_encode_motion_vector(s, s->mv[0][1][0] - pred_x,
                                                s->mv[0][1][1] - pred_y, s->f_code);
            }else{
                assert(s->mv_type==MV_TYPE_8X8);
                put_bits(&s->pb,
                        inter_MCBPC_bits[cbpc+16],
                        inter_MCBPC_code[cbpc+16]);
                put_bits(pb2, cbpy_tab[cbpy][1], cbpy_tab[cbpy][0]);

                if(!s->progressive_sequence){
                    if(cbp)
                        put_bits(pb2, 1, s->interlaced_dct);
                }

                if(interleaved_stats){
                    s->misc_bits+= get_bits_diff(s);
                }

                for(i=0; i<4; i++){
                    /* motion vectors: 8x8 mode*/
                    h263_pred_motion(s, i, 0, &pred_x, &pred_y);

                    ff_h263_encode_motion_vector(s, s->current_picture.motion_val[0][ s->block_index[i] ][0] - pred_x,
                                                    s->current_picture.motion_val[0][ s->block_index[i] ][1] - pred_y, s->f_code);
                }
            }

            if(interleaved_stats){
                s->mv_bits+= get_bits_diff(s);
            }

            mpeg4_encode_blocks(s, block, NULL, NULL, NULL, tex_pb);

            if(interleaved_stats){
                s->p_tex_bits+= get_bits_diff(s);
            }
            s->f_count++;
        }
    } else {
        int cbp;
        int dc_diff[6];   //dc values with the dc prediction subtracted
        int dir[6];  //prediction direction
        int zigzag_last_index[6];
        uint8_t *scan_table[6];
        int i;

        for(i=0; i<6; i++){
            dc_diff[i]= ff_mpeg4_pred_dc(s, i, block[i][0], &dir[i], 1);
        }

        if(s->flags & CODEC_FLAG_AC_PRED){
            s->ac_pred= decide_ac_pred(s, block, dir, scan_table, zigzag_last_index);
            if(!s->ac_pred)
                restore_ac_coeffs(s, block, dir, scan_table, zigzag_last_index);
        }else{
            for(i=0; i<6; i++)
                scan_table[i]= s->intra_scantable.permutated;
        }

        /* compute cbp */
        cbp = 0;
        for (i = 0; i < 6; i++) {
            if (s->block_last_index[i] >= 1)
                cbp |= 1 << (5 - i);
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
        put_bits(pb2, 1, s->ac_pred);
        cbpy = cbp >> 2;
        put_bits(pb2, cbpy_tab[cbpy][1], cbpy_tab[cbpy][0]);
        if(s->dquant)
            put_bits(dc_pb, 2, dquant_code[s->dquant+2]);

        if(!s->progressive_sequence){
            put_bits(dc_pb, 1, s->interlaced_dct);
        }

        if(interleaved_stats){
            s->misc_bits+= get_bits_diff(s);
        }

        mpeg4_encode_blocks(s, block, dc_diff, scan_table, dc_pb, tex_pb);

        if(interleaved_stats){
            s->i_tex_bits+= get_bits_diff(s);
        }
        s->i_count++;

        /* restore ac coeffs & last_index stuff if we messed them up with the prediction */
        if(s->ac_pred)
            restore_ac_coeffs(s, block, dir, scan_table, zigzag_last_index);
    }
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

    //printf("**mb x=%d y=%d\n", s->mb_x, s->mb_y);
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

#if CONFIG_ENCODERS
static int h263_pred_dc(MpegEncContext * s, int n, int16_t **dc_val_ptr)
{
    int x, y, wrap, a, c, pred_dc, scale;
    int16_t *dc_val;

    /* find prediction */
    if (n < 4) {
        x = 2 * s->mb_x + (n & 1);
        y = 2 * s->mb_y + ((n & 2) >> 1);
        wrap = s->b8_stride;
        dc_val = s->dc_val[0];
        scale = s->y_dc_scale;
    } else {
        x = s->mb_x;
        y = s->mb_y;
        wrap = s->mb_stride;
        dc_val = s->dc_val[n - 4 + 1];
        scale = s->c_dc_scale;
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
    pred_dc = 1024;
    /* just DC prediction */
    if (a != 1024 && c != 1024)
        pred_dc = (a + c) >> 1;
    else if (a != 1024)
        pred_dc = a;
    else
        pred_dc = c;

    /* we assume pred is positive */
    //pred_dc = (pred_dc + (scale >> 1)) / scale;
    *dc_val_ptr = &dc_val[x + y * wrap];
    return pred_dc;
}
#endif /* CONFIG_ENCODERS */

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
        //printf("\nVal = %d\tCode = %d", sval, code);
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
                int val, bit_size, range, code;

                bit_size = f_code - 1;
                range = 1 << bit_size;

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

static void init_uni_dc_tab(void)
{
    int level, uni_code, uni_len;

    for(level=-256; level<256; level++){
        int size, v, l;
        /* find number of bits */
        size = 0;
        v = abs(level);
        while (v) {
            v >>= 1;
            size++;
        }

        if (level < 0)
            l= (-level) ^ ((1 << size) - 1);
        else
            l= level;

        /* luminance */
        uni_code= DCtab_lum[size][0];
        uni_len = DCtab_lum[size][1];

        if (size > 0) {
            uni_code<<=size; uni_code|=l;
            uni_len+=size;
            if (size > 8){
                uni_code<<=1; uni_code|=1;
                uni_len++;
            }
        }
        uni_DCtab_lum_bits[level+256]= uni_code;
        uni_DCtab_lum_len [level+256]= uni_len;

        /* chrominance */
        uni_code= DCtab_chrom[size][0];
        uni_len = DCtab_chrom[size][1];

        if (size > 0) {
            uni_code<<=size; uni_code|=l;
            uni_len+=size;
            if (size > 8){
                uni_code<<=1; uni_code|=1;
                uni_len++;
            }
        }
        uni_DCtab_chrom_bits[level+256]= uni_code;
        uni_DCtab_chrom_len [level+256]= uni_len;

    }
}

static void init_uni_mpeg4_rl_tab(RLTable *rl, uint32_t *bits_tab, uint8_t *len_tab){
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
                int level1, run1;

                len_tab[index]= 100;

                /* ESC0 */
                code= get_rl_index(rl, last, run, level);
                bits= rl->table_vlc[code][0];
                len=  rl->table_vlc[code][1];
                bits=bits*2+sign; len++;

                if(code!=rl->n && len < len_tab[index]){
                    bits_tab[index]= bits;
                    len_tab [index]= len;
                }
#if 1
                /* ESC1 */
                bits= rl->table_vlc[rl->n][0];
                len=  rl->table_vlc[rl->n][1];
                bits=bits*2;    len++; //esc1
                level1= level - rl->max_level[last][run];
                if(level1>0){
                    code= get_rl_index(rl, last, run, level1);
                    bits<<= rl->table_vlc[code][1];
                    len  += rl->table_vlc[code][1];
                    bits += rl->table_vlc[code][0];
                    bits=bits*2+sign; len++;

                    if(code!=rl->n && len < len_tab[index]){
                        bits_tab[index]= bits;
                        len_tab [index]= len;
                    }
                }
#endif
#if 1
                /* ESC2 */
                bits= rl->table_vlc[rl->n][0];
                len=  rl->table_vlc[rl->n][1];
                bits=bits*4+2;    len+=2; //esc2
                run1 = run - rl->max_run[last][level] - 1;
                if(run1>=0){
                    code= get_rl_index(rl, last, run1, level);
                    bits<<= rl->table_vlc[code][1];
                    len  += rl->table_vlc[code][1];
                    bits += rl->table_vlc[code][0];
                    bits=bits*2+sign; len++;

                    if(code!=rl->n && len < len_tab[index]){
                        bits_tab[index]= bits;
                        len_tab [index]= len;
                    }
                }
#endif
                /* ESC3 */
                bits= rl->table_vlc[rl->n][0];
                len = rl->table_vlc[rl->n][1];
                bits=bits*4+3;    len+=2; //esc3
                bits=bits*2+last; len++;
                bits=bits*64+run; len+=6;
                bits=bits*2+1;    len++;  //marker
                bits=bits*4096+(slevel&0xfff); len+=12;
                bits=bits*2+1;    len++;  //marker

                if(len < len_tab[index]){
                    bits_tab[index]= bits;
                    len_tab [index]= len;
                }
            }
        }
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

        init_uni_dc_tab();

        init_rl(&rl_inter, static_rl_table_store[0]);
        init_rl(&rl_intra, static_rl_table_store[1]);
        init_rl(&rl_intra_aic, static_rl_table_store[2]);

        init_uni_mpeg4_rl_tab(&rl_intra, uni_mpeg4_intra_rl_bits, uni_mpeg4_intra_rl_len);
        init_uni_mpeg4_rl_tab(&rl_inter, uni_mpeg4_inter_rl_bits, uni_mpeg4_inter_rl_len);

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
        s->min_qcoeff= -2048;
        s->max_qcoeff=  2047;
        s->intra_ac_vlc_length     = uni_mpeg4_intra_rl_len;
        s->intra_ac_vlc_last_length= uni_mpeg4_intra_rl_len + 128*64;
        s->inter_ac_vlc_length     = uni_mpeg4_inter_rl_len;
        s->inter_ac_vlc_last_length= uni_mpeg4_inter_rl_len + 128*64;
        s->luma_dc_vlc_length= uni_DCtab_lum_len;
        s->chroma_dc_vlc_length= uni_DCtab_chrom_len;
        s->ac_esc_length= 7+2+1+6+1+12+1;
        s->y_dc_scale_table= ff_mpeg4_y_dc_scale_table;
        s->c_dc_scale_table= ff_mpeg4_c_dc_scale_table;

        if(s->flags & CODEC_FLAG_GLOBAL_HEADER){

            s->avctx->extradata= av_malloc(1024);
            init_put_bits(&s->pb, s->avctx->extradata, 1024);

            if(!(s->workaround_bugs & FF_BUG_MS))
                mpeg4_encode_visual_object_header(s);
            mpeg4_encode_vol_header(s, 0, 0);

//            ff_mpeg4_stuffing(&s->pb); ?
            flush_put_bits(&s->pb);
            s->avctx->extradata_size= (put_bits_count(&s->pb)+7)>>3;
        }

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
              if(s->h263_flv <= 1){
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
                if(level < 64) { // 7-bit level
                        put_bits(&s->pb, 1, 0);
                        put_bits(&s->pb, 1, last);
                        put_bits(&s->pb, 6, run);

                        put_sbits(&s->pb, 7, slevel);
                    } else {
                        /* 11-bit level */
                        put_bits(&s->pb, 1, 1);
                        put_bits(&s->pb, 1, last);
                        put_bits(&s->pb, 6, run);

                        put_sbits(&s->pb, 11, slevel);
                    }
              }
            } else {
                put_bits(&s->pb, 1, sign);
            }
            last_non_zero = i;
        }
    }
}

/***************************************************/
/**
 * add mpeg4 stuffing bits (01...1)
 */
void ff_mpeg4_stuffing(PutBitContext * pbc)
{
    int length;
    put_bits(pbc, 1, 0);
    length= (-put_bits_count(pbc))&7;
    if(length) put_bits(pbc, length, (1<<length)-1);
}

/* must be called before writing the header */
void ff_set_mpeg4_time(MpegEncContext * s){
    if(s->pict_type==FF_B_TYPE){
        ff_mpeg4_init_direct_mv(s);
    }else{
        s->last_time_base= s->time_base;
        s->time_base= s->time/s->avctx->time_base.den;
    }
}

static void mpeg4_encode_gop_header(MpegEncContext * s){
    int hours, minutes, seconds;
    int64_t time;

    put_bits(&s->pb, 16, 0);
    put_bits(&s->pb, 16, GOP_STARTCODE);

    time= s->current_picture_ptr->pts;
    if(s->reordered_input_picture[1])
        time= FFMIN(time, s->reordered_input_picture[1]->pts);
    time= time*s->avctx->time_base.num;

    seconds= time/s->avctx->time_base.den;
    minutes= seconds/60; seconds %= 60;
    hours= minutes/60; minutes %= 60;
    hours%=24;

    put_bits(&s->pb, 5, hours);
    put_bits(&s->pb, 6, minutes);
    put_bits(&s->pb, 1, 1);
    put_bits(&s->pb, 6, seconds);

    put_bits(&s->pb, 1, !!(s->flags&CODEC_FLAG_CLOSED_GOP));
    put_bits(&s->pb, 1, 0); //broken link == NO

    s->last_time_base= time / s->avctx->time_base.den;

    ff_mpeg4_stuffing(&s->pb);
}

static void mpeg4_encode_visual_object_header(MpegEncContext * s){
    int profile_and_level_indication;
    int vo_ver_id;

    if(s->avctx->profile != FF_PROFILE_UNKNOWN){
        profile_and_level_indication = s->avctx->profile << 4;
    }else if(s->max_b_frames || s->quarter_sample){
        profile_and_level_indication= 0xF0; // adv simple
    }else{
        profile_and_level_indication= 0x00; // simple
    }

    if(s->avctx->level != FF_LEVEL_UNKNOWN){
        profile_and_level_indication |= s->avctx->level;
    }else{
        profile_and_level_indication |= 1; //level 1
    }

    if(profile_and_level_indication>>4 == 0xF){
        vo_ver_id= 5;
    }else{
        vo_ver_id= 1;
    }

    //FIXME levels

    put_bits(&s->pb, 16, 0);
    put_bits(&s->pb, 16, VOS_STARTCODE);

    put_bits(&s->pb, 8, profile_and_level_indication);

    put_bits(&s->pb, 16, 0);
    put_bits(&s->pb, 16, VISUAL_OBJ_STARTCODE);

    put_bits(&s->pb, 1, 1);
        put_bits(&s->pb, 4, vo_ver_id);
        put_bits(&s->pb, 3, 1); //priority

    put_bits(&s->pb, 4, 1); //visual obj type== video obj

    put_bits(&s->pb, 1, 0); //video signal type == no clue //FIXME

    ff_mpeg4_stuffing(&s->pb);
}

static void mpeg4_encode_vol_header(MpegEncContext * s, int vo_number, int vol_number)
{
    int vo_ver_id;

    if (!CONFIG_MPEG4_ENCODER)  return;

    if(s->max_b_frames || s->quarter_sample){
        vo_ver_id= 5;
        s->vo_type= ADV_SIMPLE_VO_TYPE;
    }else{
        vo_ver_id= 1;
        s->vo_type= SIMPLE_VO_TYPE;
    }

    put_bits(&s->pb, 16, 0);
    put_bits(&s->pb, 16, 0x100 + vo_number);        /* video obj */
    put_bits(&s->pb, 16, 0);
    put_bits(&s->pb, 16, 0x120 + vol_number);       /* video obj layer */

    put_bits(&s->pb, 1, 0);             /* random access vol */
    put_bits(&s->pb, 8, s->vo_type);    /* video obj type indication */
    if(s->workaround_bugs & FF_BUG_MS) {
        put_bits(&s->pb, 1, 0);         /* is obj layer id= no */
    } else {
        put_bits(&s->pb, 1, 1);         /* is obj layer id= yes */
        put_bits(&s->pb, 4, vo_ver_id); /* is obj layer ver id */
        put_bits(&s->pb, 3, 1);         /* is obj layer priority */
    }

    aspect_to_info(s, s->avctx->sample_aspect_ratio);

    put_bits(&s->pb, 4, s->aspect_ratio_info);/* aspect ratio info */
    if (s->aspect_ratio_info == FF_ASPECT_EXTENDED){
        put_bits(&s->pb, 8, s->avctx->sample_aspect_ratio.num);
        put_bits(&s->pb, 8, s->avctx->sample_aspect_ratio.den);
    }

    if(s->workaround_bugs & FF_BUG_MS) { //
        put_bits(&s->pb, 1, 0);         /* vol control parameters= no @@@ */
    } else {
        put_bits(&s->pb, 1, 1);         /* vol control parameters= yes */
        put_bits(&s->pb, 2, 1);         /* chroma format YUV 420/YV12 */
        put_bits(&s->pb, 1, s->low_delay);
        put_bits(&s->pb, 1, 0);         /* vbv parameters= no */
    }

    put_bits(&s->pb, 2, RECT_SHAPE);    /* vol shape= rectangle */
    put_bits(&s->pb, 1, 1);             /* marker bit */

    put_bits(&s->pb, 16, s->avctx->time_base.den);
    if (s->time_increment_bits < 1)
        s->time_increment_bits = 1;
    put_bits(&s->pb, 1, 1);             /* marker bit */
    put_bits(&s->pb, 1, 0);             /* fixed vop rate=no */
    put_bits(&s->pb, 1, 1);             /* marker bit */
    put_bits(&s->pb, 13, s->width);     /* vol width */
    put_bits(&s->pb, 1, 1);             /* marker bit */
    put_bits(&s->pb, 13, s->height);    /* vol height */
    put_bits(&s->pb, 1, 1);             /* marker bit */
    put_bits(&s->pb, 1, s->progressive_sequence ? 0 : 1);
    put_bits(&s->pb, 1, 1);             /* obmc disable */
    if (vo_ver_id == 1) {
        put_bits(&s->pb, 1, s->vol_sprite_usage);       /* sprite enable */
    }else{
        put_bits(&s->pb, 2, s->vol_sprite_usage);       /* sprite enable */
    }

    put_bits(&s->pb, 1, 0);             /* not 8 bit == false */
    put_bits(&s->pb, 1, s->mpeg_quant); /* quant type= (0=h263 style)*/

    if(s->mpeg_quant){
        ff_write_quant_matrix(&s->pb, s->avctx->intra_matrix);
        ff_write_quant_matrix(&s->pb, s->avctx->inter_matrix);
    }

    if (vo_ver_id != 1)
        put_bits(&s->pb, 1, s->quarter_sample);
    put_bits(&s->pb, 1, 1);             /* complexity estimation disable */
    s->resync_marker= s->rtp_mode;
    put_bits(&s->pb, 1, s->resync_marker ? 0 : 1);/* resync marker disable */
    put_bits(&s->pb, 1, s->data_partitioning ? 1 : 0);
    if(s->data_partitioning){
        put_bits(&s->pb, 1, 0);         /* no rvlc */
    }

    if (vo_ver_id != 1){
        put_bits(&s->pb, 1, 0);         /* newpred */
        put_bits(&s->pb, 1, 0);         /* reduced res vop */
    }
    put_bits(&s->pb, 1, 0);             /* scalability */

    ff_mpeg4_stuffing(&s->pb);

    /* user data */
    if(!(s->flags & CODEC_FLAG_BITEXACT)){
        put_bits(&s->pb, 16, 0);
        put_bits(&s->pb, 16, 0x1B2);    /* user_data */
        ff_put_string(&s->pb, LIBAVCODEC_IDENT, 0);
    }
}

/* write mpeg4 VOP header */
void mpeg4_encode_picture_header(MpegEncContext * s, int picture_number)
{
    int time_incr;
    int time_div, time_mod;

    if(s->pict_type==FF_I_TYPE){
        if(!(s->flags&CODEC_FLAG_GLOBAL_HEADER)){
            if(s->strict_std_compliance < FF_COMPLIANCE_VERY_STRICT) //HACK, the reference sw is buggy
                mpeg4_encode_visual_object_header(s);
            if(s->strict_std_compliance < FF_COMPLIANCE_VERY_STRICT || picture_number==0) //HACK, the reference sw is buggy
                mpeg4_encode_vol_header(s, 0, 0);
        }
        if(!(s->workaround_bugs & FF_BUG_MS))
            mpeg4_encode_gop_header(s);
    }

    s->partitioned_frame= s->data_partitioning && s->pict_type!=FF_B_TYPE;

//printf("num:%d rate:%d base:%d\n", s->picture_number, s->time_base.den, FRAME_RATE_BASE);

    put_bits(&s->pb, 16, 0);                /* vop header */
    put_bits(&s->pb, 16, VOP_STARTCODE);    /* vop header */
    put_bits(&s->pb, 2, s->pict_type - 1);  /* pict type: I = 0 , P = 1 */

    assert(s->time>=0);
    time_div= s->time/s->avctx->time_base.den;
    time_mod= s->time%s->avctx->time_base.den;
    time_incr= time_div - s->last_time_base;
    assert(time_incr >= 0);
    while(time_incr--)
        put_bits(&s->pb, 1, 1);

    put_bits(&s->pb, 1, 0);

    put_bits(&s->pb, 1, 1);                             /* marker */
    put_bits(&s->pb, s->time_increment_bits, time_mod); /* time increment */
    put_bits(&s->pb, 1, 1);                             /* marker */
    put_bits(&s->pb, 1, 1);                             /* vop coded */
    if (    s->pict_type == FF_P_TYPE
        || (s->pict_type == FF_S_TYPE && s->vol_sprite_usage==GMC_SPRITE)) {
        put_bits(&s->pb, 1, s->no_rounding);    /* rounding type */
    }
    put_bits(&s->pb, 3, 0);     /* intra dc VLC threshold */
    if(!s->progressive_sequence){
         put_bits(&s->pb, 1, s->current_picture_ptr->top_field_first);
         put_bits(&s->pb, 1, s->alternate_scan);
    }
    //FIXME sprite stuff

    put_bits(&s->pb, 5, s->qscale);

    if (s->pict_type != FF_I_TYPE)
        put_bits(&s->pb, 3, s->f_code); /* fcode_for */
    if (s->pict_type == FF_B_TYPE)
        put_bits(&s->pb, 3, s->b_code); /* fcode_back */
    //    printf("****frame %d\n", picture_number);
}

#endif //CONFIG_ENCODERS

/**
 * predicts the dc.
 * encoding quantized level -> quantized diff
 * decoding quantized diff -> quantized level
 * @param n block index (0-3 are luma, 4-5 are chroma)
 * @param dir_ptr pointer to an integer where the prediction direction will be stored
 */
static inline int ff_mpeg4_pred_dc(MpegEncContext * s, int n, int level, int *dir_ptr, int encoding)
{
    int a, b, c, wrap, pred, scale, ret;
    int16_t *dc_val;

    /* find prediction */
    if (n < 4) {
        scale = s->y_dc_scale;
    } else {
        scale = s->c_dc_scale;
    }
    if(IS_3IV1)
        scale= 8;

    wrap= s->block_wrap[n];
    dc_val = s->dc_val[0] + s->block_index[n];

    /* B C
     * A X
     */
    a = dc_val[ - 1];
    b = dc_val[ - 1 - wrap];
    c = dc_val[ - wrap];

    /* outside slice handling (we can't do that by memset as we need the dc for error resilience) */
    if(s->first_slice_line && n!=3){
        if(n!=2) b=c= 1024;
        if(n!=1 && s->mb_x == s->resync_mb_x) b=a= 1024;
    }
    if(s->mb_x == s->resync_mb_x && s->mb_y == s->resync_mb_y+1){
        if(n==0 || n==4 || n==5)
            b=1024;
    }

    if (abs(a - b) < abs(b - c)) {
        pred = c;
        *dir_ptr = 1; /* top */
    } else {
        pred = a;
        *dir_ptr = 0; /* left */
    }
    /* we assume pred is positive */
    pred = FASTDIV((pred + (scale >> 1)), scale);

    if(encoding){
        ret = level - pred;
    }else{
        level += pred;
        ret= level;
        if(s->error_recognition>=3){
            if(level<0){
                av_log(s->avctx, AV_LOG_ERROR, "dc<0 at %dx%d\n", s->mb_x, s->mb_y);
                return -1;
            }
            if(level*scale > 2048 + scale){
                av_log(s->avctx, AV_LOG_ERROR, "dc overflow at %dx%d\n", s->mb_x, s->mb_y);
                return -1;
            }
        }
    }
    level *=scale;
    if(level&(~2047)){
        if(level<0)
            level=0;
        else if(!(s->workaround_bugs&FF_BUG_DC_CLIP))
            level=2047;
    }
    dc_val[0]= level;

    return ret;
}

/**
 * predicts the ac.
 * @param n block index (0-3 are luma, 4-5 are chroma)
 * @param dir the ac prediction direction
 */
void mpeg4_pred_ac(MpegEncContext * s, DCTELEM *block, int n,
                   int dir)
{
    int i;
    int16_t *ac_val, *ac_val1;
    int8_t * const qscale_table= s->current_picture.qscale_table;

    /* find prediction */
    ac_val = s->ac_val[0][0] + s->block_index[n] * 16;
    ac_val1 = ac_val;
    if (s->ac_pred) {
        if (dir == 0) {
            const int xy= s->mb_x-1 + s->mb_y*s->mb_stride;
            /* left prediction */
            ac_val -= 16;

            if(s->mb_x==0 || s->qscale == qscale_table[xy] || n==1 || n==3){
                /* same qscale */
                for(i=1;i<8;i++) {
                    block[s->dsp.idct_permutation[i<<3]] += ac_val[i];
                }
            }else{
                /* different qscale, we must rescale */
                for(i=1;i<8;i++) {
                    block[s->dsp.idct_permutation[i<<3]] += ROUNDED_DIV(ac_val[i]*qscale_table[xy], s->qscale);
                }
            }
        } else {
            const int xy= s->mb_x + s->mb_y*s->mb_stride - s->mb_stride;
            /* top prediction */
            ac_val -= 16 * s->block_wrap[n];

            if(s->mb_y==0 || s->qscale == qscale_table[xy] || n==2 || n==3){
                /* same qscale */
                for(i=1;i<8;i++) {
                    block[s->dsp.idct_permutation[i]] += ac_val[i + 8];
                }
            }else{
                /* different qscale, we must rescale */
                for(i=1;i<8;i++) {
                    block[s->dsp.idct_permutation[i]] += ROUNDED_DIV(ac_val[i + 8]*qscale_table[xy], s->qscale);
                }
            }
        }
    }
    /* left copy */
    for(i=1;i<8;i++)
        ac_val1[i    ] = block[s->dsp.idct_permutation[i<<3]];

    /* top copy */
    for(i=1;i<8;i++)
        ac_val1[8 + i] = block[s->dsp.idct_permutation[i   ]];

}

#if CONFIG_ENCODERS

/**
 * encodes the dc value.
 * @param n block index (0-3 are luma, 4-5 are chroma)
 */
static inline void mpeg4_encode_dc(PutBitContext * s, int level, int n)
{
#if 1
//    if(level<-255 || level>255) printf("dc overflow\n");
    level+=256;
    if (n < 4) {
        /* luminance */
        put_bits(s, uni_DCtab_lum_len[level], uni_DCtab_lum_bits[level]);
    } else {
        /* chrominance */
        put_bits(s, uni_DCtab_chrom_len[level], uni_DCtab_chrom_bits[level]);
    }
#else
    int size, v;
    /* find number of bits */
    size = 0;
    v = abs(level);
    while (v) {
        v >>= 1;
        size++;
    }

    if (n < 4) {
        /* luminance */
        put_bits(&s->pb, DCtab_lum[size][1], DCtab_lum[size][0]);
    } else {
        /* chrominance */
        put_bits(&s->pb, DCtab_chrom[size][1], DCtab_chrom[size][0]);
    }

    /* encode remaining bits */
    if (size > 0) {
        if (level < 0)
            level = (-level) ^ ((1 << size) - 1);
        put_bits(&s->pb, size, level);
        if (size > 8)
            put_bits(&s->pb, 1, 1);
    }
#endif
}

static inline int mpeg4_get_dc_length(int level, int n){
    if (n < 4) {
        return uni_DCtab_lum_len[level + 256];
    } else {
        return uni_DCtab_chrom_len[level + 256];
    }
}

/**
 * encodes a 8x8 block
 * @param n block index (0-3 are luma, 4-5 are chroma)
 */
static inline void mpeg4_encode_block(MpegEncContext * s, DCTELEM * block, int n, int intra_dc,
                               uint8_t *scan_table, PutBitContext *dc_pb, PutBitContext *ac_pb)
{
    int i, last_non_zero;
#if 0 //variables for the outcommented version
    int code, sign, last;
#endif
    const RLTable *rl;
    uint32_t *bits_tab;
    uint8_t *len_tab;
    const int last_index = s->block_last_index[n];

    if (s->mb_intra) { //Note gcc (3.2.1 at least) will optimize this away
        /* mpeg4 based DC predictor */
        mpeg4_encode_dc(dc_pb, intra_dc, n);
        if(last_index<1) return;
        i = 1;
        rl = &rl_intra;
        bits_tab= uni_mpeg4_intra_rl_bits;
        len_tab = uni_mpeg4_intra_rl_len;
    } else {
        if(last_index<0) return;
        i = 0;
        rl = &rl_inter;
        bits_tab= uni_mpeg4_inter_rl_bits;
        len_tab = uni_mpeg4_inter_rl_len;
    }

    /* AC coefs */
    last_non_zero = i - 1;
#if 1
    for (; i < last_index; i++) {
        int level = block[ scan_table[i] ];
        if (level) {
            int run = i - last_non_zero - 1;
            level+=64;
            if((level&(~127)) == 0){
                const int index= UNI_MPEG4_ENC_INDEX(0, run, level);
                put_bits(ac_pb, len_tab[index], bits_tab[index]);
            }else{ //ESC3
                put_bits(ac_pb, 7+2+1+6+1+12+1, (3<<23)+(3<<21)+(0<<20)+(run<<14)+(1<<13)+(((level-64)&0xfff)<<1)+1);
            }
            last_non_zero = i;
        }
    }
    /*if(i<=last_index)*/{
        int level = block[ scan_table[i] ];
        int run = i - last_non_zero - 1;
        level+=64;
        if((level&(~127)) == 0){
            const int index= UNI_MPEG4_ENC_INDEX(1, run, level);
            put_bits(ac_pb, len_tab[index], bits_tab[index]);
        }else{ //ESC3
            put_bits(ac_pb, 7+2+1+6+1+12+1, (3<<23)+(3<<21)+(1<<20)+(run<<14)+(1<<13)+(((level-64)&0xfff)<<1)+1);
        }
    }
#else
    for (; i <= last_index; i++) {
        const int slevel = block[ scan_table[i] ];
        if (slevel) {
            int level;
            int run = i - last_non_zero - 1;
            last = (i == last_index);
            sign = 0;
            level = slevel;
            if (level < 0) {
                sign = 1;
                level = -level;
            }
            code = get_rl_index(rl, last, run, level);
            put_bits(ac_pb, rl->table_vlc[code][1], rl->table_vlc[code][0]);
            if (code == rl->n) {
                int level1, run1;
                level1 = level - rl->max_level[last][run];
                if (level1 < 1)
                    goto esc2;
                code = get_rl_index(rl, last, run, level1);
                if (code == rl->n) {
                esc2:
                    put_bits(ac_pb, 1, 1);
                    if (level > MAX_LEVEL)
                        goto esc3;
                    run1 = run - rl->max_run[last][level] - 1;
                    if (run1 < 0)
                        goto esc3;
                    code = get_rl_index(rl, last, run1, level);
                    if (code == rl->n) {
                    esc3:
                        /* third escape */
                        put_bits(ac_pb, 1, 1);
                        put_bits(ac_pb, 1, last);
                        put_bits(ac_pb, 6, run);
                        put_bits(ac_pb, 1, 1);
                        put_sbits(ac_pb, 12, slevel);
                        put_bits(ac_pb, 1, 1);
                    } else {
                        /* second escape */
                        put_bits(ac_pb, 1, 0);
                        put_bits(ac_pb, rl->table_vlc[code][1], rl->table_vlc[code][0]);
                        put_bits(ac_pb, 1, sign);
                    }
                } else {
                    /* first escape */
                    put_bits(ac_pb, 1, 0);
                    put_bits(ac_pb, rl->table_vlc[code][1], rl->table_vlc[code][0]);
                    put_bits(ac_pb, 1, sign);
                }
            } else {
                put_bits(ac_pb, 1, sign);
            }
            last_non_zero = i;
        }
    }
#endif
}

static int mpeg4_get_block_length(MpegEncContext * s, DCTELEM * block, int n, int intra_dc,
                               uint8_t *scan_table)
{
    int i, last_non_zero;
    const RLTable *rl;
    uint8_t *len_tab;
    const int last_index = s->block_last_index[n];
    int len=0;

    if (s->mb_intra) { //Note gcc (3.2.1 at least) will optimize this away
        /* mpeg4 based DC predictor */
        len += mpeg4_get_dc_length(intra_dc, n);
        if(last_index<1) return len;
        i = 1;
        rl = &rl_intra;
        len_tab = uni_mpeg4_intra_rl_len;
    } else {
        if(last_index<0) return 0;
        i = 0;
        rl = &rl_inter;
        len_tab = uni_mpeg4_inter_rl_len;
    }

    /* AC coefs */
    last_non_zero = i - 1;
    for (; i < last_index; i++) {
        int level = block[ scan_table[i] ];
        if (level) {
            int run = i - last_non_zero - 1;
            level+=64;
            if((level&(~127)) == 0){
                const int index= UNI_MPEG4_ENC_INDEX(0, run, level);
                len += len_tab[index];
            }else{ //ESC3
                len += 7+2+1+6+1+12+1;
            }
            last_non_zero = i;
        }
    }
    /*if(i<=last_index)*/{
        int level = block[ scan_table[i] ];
        int run = i - last_non_zero - 1;
        level+=64;
        if((level&(~127)) == 0){
            const int index= UNI_MPEG4_ENC_INDEX(1, run, level);
            len += len_tab[index];
        }else{ //ESC3
            len += 7+2+1+6+1+12+1;
        }
    }

    return len;
}

#endif


/***********************************************/
/* decoding */

static VLC intra_MCBPC_vlc;
static VLC inter_MCBPC_vlc;
static VLC cbpy_vlc;
static VLC mv_vlc;
static VLC dc_lum, dc_chrom;
static VLC sprite_trajectory;
static VLC mb_type_b_vlc;
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
        init_rl(&rl_intra, static_rl_table_store[1]);
        init_rl(&rvlc_rl_inter, static_rl_table_store[3]);
        init_rl(&rvlc_rl_intra, static_rl_table_store[4]);
        init_rl(&rl_intra_aic, static_rl_table_store[2]);
        INIT_VLC_RL(rl_inter, 554);
        INIT_VLC_RL(rl_intra, 554);
        INIT_VLC_RL(rvlc_rl_inter, 1072);
        INIT_VLC_RL(rvlc_rl_intra, 1072);
        INIT_VLC_RL(rl_intra_aic, 554);
        INIT_VLC_STATIC(&dc_lum, DC_VLC_BITS, 10 /* 13 */,
                 &DCtab_lum[0][1], 2, 1,
                 &DCtab_lum[0][0], 2, 1, 512);
        INIT_VLC_STATIC(&dc_chrom, DC_VLC_BITS, 10 /* 13 */,
                 &DCtab_chrom[0][1], 2, 1,
                 &DCtab_chrom[0][0], 2, 1, 512);
        INIT_VLC_STATIC(&sprite_trajectory, SPRITE_TRAJ_VLC_BITS, 15,
                 &sprite_trajectory_tab[0][1], 4, 2,
                 &sprite_trajectory_tab[0][0], 4, 2, 128);
        INIT_VLC_STATIC(&mb_type_b_vlc, MB_TYPE_B_VLC_BITS, 4,
                 &mb_type_b_tab[0][1], 2, 1,
                 &mb_type_b_tab[0][0], 2, 1, 16);
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
    left= s->gb.size_in_bits - get_bits_count(&s->gb);
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

#if CONFIG_ENCODERS

void ff_mpeg4_init_partitions(MpegEncContext *s)
{
    uint8_t *start= pbBufPtr(&s->pb);
    uint8_t *end= s->pb.buf_end;
    int size= end - start;
    int pb_size = (((intptr_t)start + size/3)&(~3)) - (intptr_t)start;
    int tex_size= (size - 2*pb_size)&(~3);

    set_put_bits_buffer_size(&s->pb, pb_size);
    init_put_bits(&s->tex_pb, start + pb_size           , tex_size);
    init_put_bits(&s->pb2   , start + pb_size + tex_size, pb_size);
}

void ff_mpeg4_merge_partitions(MpegEncContext *s)
{
    const int pb2_len   = put_bits_count(&s->pb2   );
    const int tex_pb_len= put_bits_count(&s->tex_pb);
    const int bits= put_bits_count(&s->pb);

    if(s->pict_type==FF_I_TYPE){
        put_bits(&s->pb, 19, DC_MARKER);
        s->misc_bits+=19 + pb2_len + bits - s->last_bits;
        s->i_tex_bits+= tex_pb_len;
    }else{
        put_bits(&s->pb, 17, MOTION_MARKER);
        s->misc_bits+=17 + pb2_len;
        s->mv_bits+= bits - s->last_bits;
        s->p_tex_bits+= tex_pb_len;
    }

    flush_put_bits(&s->pb2);
    flush_put_bits(&s->tex_pb);

    set_put_bits_buffer_size(&s->pb, s->pb2.buf_end - s->pb.buf);
    ff_copy_bits(&s->pb, s->pb2.buf   , pb2_len);
    ff_copy_bits(&s->pb, s->tex_pb.buf, tex_pb_len);
    s->last_bits= put_bits_count(&s->pb);
}

#endif //CONFIG_ENCODERS

int ff_mpeg4_get_video_packet_prefix_length(MpegEncContext *s){
    switch(s->pict_type){
        case FF_I_TYPE:
            return 16;
        case FF_P_TYPE:
        case FF_S_TYPE:
            return s->f_code+15;
        case FF_B_TYPE:
            return FFMAX3(s->f_code, s->b_code, 2) + 15;
        default:
            return -1;
    }
}

#if CONFIG_ENCODERS

void ff_mpeg4_encode_video_packet_header(MpegEncContext *s)
{
    int mb_num_bits= av_log2(s->mb_num - 1) + 1;

    put_bits(&s->pb, ff_mpeg4_get_video_packet_prefix_length(s), 0);
    put_bits(&s->pb, 1, 1);

    put_bits(&s->pb, mb_num_bits, s->mb_x + s->mb_y*s->mb_width);
    put_bits(&s->pb, s->quant_precision, s->qscale);
    put_bits(&s->pb, 1, 0); /* no HEC */
}

#endif //CONFIG_ENCODERS

/**
 * check if the next stuff is a resync marker or the end.
 * @return 0 if not
 */
static inline int mpeg4_is_resync(MpegEncContext *s){
    int bits_count= get_bits_count(&s->gb);
    int v= show_bits(&s->gb, 16);

    if(s->workaround_bugs&FF_BUG_NO_PADDING){
        return 0;
    }

    while(v<=0xFF){
        if(s->pict_type==FF_B_TYPE || (v>>(8-s->pict_type)!=1) || s->partitioned_frame)
            break;
        skip_bits(&s->gb, 8+s->pict_type);
        bits_count+= 8+s->pict_type;
        v= show_bits(&s->gb, 16);
    }

    if(bits_count + 8 >= s->gb.size_in_bits){
        v>>=8;
        v|= 0x7F >> (7-(bits_count&7));

        if(v==0x7F)
            return 1;
    }else{
        if(v == ff_mpeg4_resync_prefix[bits_count&7]){
            int len;
            GetBitContext gb= s->gb;

            skip_bits(&s->gb, 1);
            align_get_bits(&s->gb);

            for(len=0; len<32; len++){
                if(get_bits1(&s->gb)) break;
            }

            s->gb= gb;

            if(len>=ff_mpeg4_get_video_packet_prefix_length(s))
                return 1;
        }
    }
    return 0;
}

/**
 * decodes the next video packet.
 * @return <0 if something went wrong
 */
static int mpeg4_decode_video_packet_header(MpegEncContext *s)
{
    int mb_num_bits= av_log2(s->mb_num - 1) + 1;
    int header_extension=0, mb_num, len;

    /* is there enough space left for a video packet + header */
    if( get_bits_count(&s->gb) > s->gb.size_in_bits-20) return -1;

    for(len=0; len<32; len++){
        if(get_bits1(&s->gb)) break;
    }

    if(len!=ff_mpeg4_get_video_packet_prefix_length(s)){
        av_log(s->avctx, AV_LOG_ERROR, "marker does not match f_code\n");
        return -1;
    }

    if(s->shape != RECT_SHAPE){
        header_extension= get_bits1(&s->gb);
        //FIXME more stuff here
    }

    mb_num= get_bits(&s->gb, mb_num_bits);
    if(mb_num>=s->mb_num){
        av_log(s->avctx, AV_LOG_ERROR, "illegal mb_num in video packet (%d %d) \n", mb_num, s->mb_num);
        return -1;
    }
    if(s->pict_type == FF_B_TYPE){
        while(s->next_picture.mbskip_table[ s->mb_index2xy[ mb_num ] ]) mb_num++;
        if(mb_num >= s->mb_num) return -1; // slice contains just skipped MBs which where already decoded
    }

    s->mb_x= mb_num % s->mb_width;
    s->mb_y= mb_num / s->mb_width;

    if(s->shape != BIN_ONLY_SHAPE){
        int qscale= get_bits(&s->gb, s->quant_precision);
        if(qscale)
            s->chroma_qscale=s->qscale= qscale;
    }

    if(s->shape == RECT_SHAPE){
        header_extension= get_bits1(&s->gb);
    }
    if(header_extension){
        int time_increment;
        int time_incr=0;

        while (get_bits1(&s->gb) != 0)
            time_incr++;

        check_marker(&s->gb, "before time_increment in video packed header");
        time_increment= get_bits(&s->gb, s->time_increment_bits);
        check_marker(&s->gb, "before vop_coding_type in video packed header");

        skip_bits(&s->gb, 2); /* vop coding type */
        //FIXME not rect stuff here

        if(s->shape != BIN_ONLY_SHAPE){
            skip_bits(&s->gb, 3); /* intra dc vlc threshold */
//FIXME don't just ignore everything
            if(s->pict_type == FF_S_TYPE && s->vol_sprite_usage==GMC_SPRITE){
                mpeg4_decode_sprite_trajectory(s, &s->gb);
                av_log(s->avctx, AV_LOG_ERROR, "untested\n");
            }

            //FIXME reduced res stuff here

            if (s->pict_type != FF_I_TYPE) {
                int f_code = get_bits(&s->gb, 3);       /* fcode_for */
                if(f_code==0){
                    av_log(s->avctx, AV_LOG_ERROR, "Error, video packet header damaged (f_code=0)\n");
                }
            }
            if (s->pict_type == FF_B_TYPE) {
                int b_code = get_bits(&s->gb, 3);
                if(b_code==0){
                    av_log(s->avctx, AV_LOG_ERROR, "Error, video packet header damaged (b_code=0)\n");
                }
            }
        }
    }
    //FIXME new-pred stuff

//printf("parse ok %d %d %d %d\n", mb_num, s->mb_x + s->mb_y*s->mb_width, get_bits_count(gb), get_bits_count(&s->gb));

    return 0;
}

void ff_mpeg4_clean_buffers(MpegEncContext *s)
{
    int c_wrap, c_xy, l_wrap, l_xy;

    l_wrap= s->b8_stride;
    l_xy= (2*s->mb_y-1)*l_wrap + s->mb_x*2 - 1;
    c_wrap= s->mb_stride;
    c_xy= (s->mb_y-1)*c_wrap + s->mb_x - 1;

#if 0
    /* clean DC */
    memsetw(s->dc_val[0] + l_xy, 1024, l_wrap*2+1);
    memsetw(s->dc_val[1] + c_xy, 1024, c_wrap+1);
    memsetw(s->dc_val[2] + c_xy, 1024, c_wrap+1);
#endif

    /* clean AC */
    memset(s->ac_val[0] + l_xy, 0, (l_wrap*2+1)*16*sizeof(int16_t));
    memset(s->ac_val[1] + c_xy, 0, (c_wrap  +1)*16*sizeof(int16_t));
    memset(s->ac_val[2] + c_xy, 0, (c_wrap  +1)*16*sizeof(int16_t));

    /* clean MV */
    // we can't clear the MVs as they might be needed by a b frame
//    memset(s->motion_val + l_xy, 0, (l_wrap*2+1)*2*sizeof(int16_t));
//    memset(s->motion_val, 0, 2*sizeof(int16_t)*(2 + s->mb_width*2)*(2 + s->mb_height*2));
    s->last_mv[0][0][0]=
    s->last_mv[0][0][1]=
    s->last_mv[1][0][0]=
    s->last_mv[1][0][1]= 0;
}

/**
 * finds the next resync_marker
 * @param p pointer to buffer to scan
 * @param end pointer to the end of the buffer
 * @return pointer to the next resync_marker, or \p end if none was found
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
        if(s->codec_id==CODEC_ID_MPEG4)
            ret= mpeg4_decode_video_packet_header(s);
        else
            ret= h263_decode_gob_header(s);
        if(ret>=0)
            return pos;
    }
    //OK, it's not where it is supposed to be ...
    s->gb= s->last_resync_gb;
    align_get_bits(&s->gb);
    left= s->gb.size_in_bits - get_bits_count(&s->gb);

    for(;left>16+1+5+5; left-=8){
        if(show_bits(&s->gb, 16)==0){
            GetBitContext bak= s->gb;

            pos= get_bits_count(&s->gb);
            if(s->codec_id==CODEC_ID_MPEG4)
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

/**
 * gets the average motion vector for a GMC MB.
 * @param n either 0 for the x component or 1 for y
 * @returns the average MV for a GMC MB
 */
static inline int get_amv(MpegEncContext *s, int n){
    int x, y, mb_v, sum, dx, dy, shift;
    int len = 1 << (s->f_code + 4);
    const int a= s->sprite_warping_accuracy;

    if(s->workaround_bugs & FF_BUG_AMV)
        len >>= s->quarter_sample;

    if(s->real_sprite_warping_points==1){
        if(s->divx_version==500 && s->divx_build==413)
            sum= s->sprite_offset[0][n] / (1<<(a - s->quarter_sample));
        else
            sum= RSHIFT(s->sprite_offset[0][n]<<s->quarter_sample, a);
    }else{
        dx= s->sprite_delta[n][0];
        dy= s->sprite_delta[n][1];
        shift= s->sprite_shift[0];
        if(n) dy -= 1<<(shift + a + 1);
        else  dx -= 1<<(shift + a + 1);
        mb_v= s->sprite_offset[0][n] + dx*s->mb_x*16 + dy*s->mb_y*16;

        sum=0;
        for(y=0; y<16; y++){
            int v;

            v= mb_v + dy*y;
            //XXX FIXME optimize
            for(x=0; x<16; x++){
                sum+= v>>shift;
                v+= dx;
            }
        }
        sum= RSHIFT(sum, a+8-s->quarter_sample);
    }

    if      (sum < -len) sum= -len;
    else if (sum >= len) sum= len-1;

    return sum;
}

/**
 * decodes first partition.
 * @return number of MBs decoded or <0 if an error occurred
 */
static int mpeg4_decode_partition_a(MpegEncContext *s){
    int mb_num;
    static const int8_t quant_tab[4] = { -1, -2, 1, 2 };

    /* decode first partition */
    mb_num=0;
    s->first_slice_line=1;
    for(; s->mb_y<s->mb_height; s->mb_y++){
        ff_init_block_index(s);
        for(; s->mb_x<s->mb_width; s->mb_x++){
            const int xy= s->mb_x + s->mb_y*s->mb_stride;
            int cbpc;
            int dir=0;

            mb_num++;
            ff_update_block_index(s);
            if(s->mb_x == s->resync_mb_x && s->mb_y == s->resync_mb_y+1)
                s->first_slice_line=0;

            if(s->pict_type==FF_I_TYPE){
                int i;

                do{
                    if(show_bits_long(&s->gb, 19)==DC_MARKER){
                        return mb_num-1;
                    }

                    cbpc = get_vlc2(&s->gb, intra_MCBPC_vlc.table, INTRA_MCBPC_VLC_BITS, 2);
                    if (cbpc < 0){
                        av_log(s->avctx, AV_LOG_ERROR, "cbpc corrupted at %d %d\n", s->mb_x, s->mb_y);
                        return -1;
                    }
                }while(cbpc == 8);

                s->cbp_table[xy]= cbpc & 3;
                s->current_picture.mb_type[xy]= MB_TYPE_INTRA;
                s->mb_intra = 1;

                if(cbpc & 4) {
                    ff_set_qscale(s, s->qscale + quant_tab[get_bits(&s->gb, 2)]);
                }
                s->current_picture.qscale_table[xy]= s->qscale;

                s->mbintra_table[xy]= 1;
                for(i=0; i<6; i++){
                    int dc_pred_dir;
                    int dc= mpeg4_decode_dc(s, i, &dc_pred_dir);
                    if(dc < 0){
                        av_log(s->avctx, AV_LOG_ERROR, "DC corrupted at %d %d\n", s->mb_x, s->mb_y);
                        return -1;
                    }
                    dir<<=1;
                    if(dc_pred_dir) dir|=1;
                }
                s->pred_dir_table[xy]= dir;
            }else{ /* P/S_TYPE */
                int mx, my, pred_x, pred_y, bits;
                int16_t * const mot_val= s->current_picture.motion_val[0][s->block_index[0]];
                const int stride= s->b8_stride*2;

try_again:
                bits= show_bits(&s->gb, 17);
                if(bits==MOTION_MARKER){
                    return mb_num-1;
                }
                skip_bits1(&s->gb);
                if(bits&0x10000){
                    /* skip mb */
                    if(s->pict_type==FF_S_TYPE && s->vol_sprite_usage==GMC_SPRITE){
                        s->current_picture.mb_type[xy]= MB_TYPE_SKIP | MB_TYPE_16x16 | MB_TYPE_GMC | MB_TYPE_L0;
                        mx= get_amv(s, 0);
                        my= get_amv(s, 1);
                    }else{
                        s->current_picture.mb_type[xy]= MB_TYPE_SKIP | MB_TYPE_16x16 | MB_TYPE_L0;
                        mx=my=0;
                    }
                    mot_val[0       ]= mot_val[2       ]=
                    mot_val[0+stride]= mot_val[2+stride]= mx;
                    mot_val[1       ]= mot_val[3       ]=
                    mot_val[1+stride]= mot_val[3+stride]= my;

                    if(s->mbintra_table[xy])
                        ff_clean_intra_table_entries(s);
                    continue;
                }

                cbpc = get_vlc2(&s->gb, inter_MCBPC_vlc.table, INTER_MCBPC_VLC_BITS, 2);
                if (cbpc < 0){
                    av_log(s->avctx, AV_LOG_ERROR, "cbpc corrupted at %d %d\n", s->mb_x, s->mb_y);
                    return -1;
                }
                if(cbpc == 20)
                    goto try_again;

                s->cbp_table[xy]= cbpc&(8+3); //8 is dquant

                s->mb_intra = ((cbpc & 4) != 0);

                if(s->mb_intra){
                    s->current_picture.mb_type[xy]= MB_TYPE_INTRA;
                    s->mbintra_table[xy]= 1;
                    mot_val[0       ]= mot_val[2       ]=
                    mot_val[0+stride]= mot_val[2+stride]= 0;
                    mot_val[1       ]= mot_val[3       ]=
                    mot_val[1+stride]= mot_val[3+stride]= 0;
                }else{
                    if(s->mbintra_table[xy])
                        ff_clean_intra_table_entries(s);

                    if(s->pict_type==FF_S_TYPE && s->vol_sprite_usage==GMC_SPRITE && (cbpc & 16) == 0)
                        s->mcsel= get_bits1(&s->gb);
                    else s->mcsel= 0;

                    if ((cbpc & 16) == 0) {
                        /* 16x16 motion prediction */

                        h263_pred_motion(s, 0, 0, &pred_x, &pred_y);
                        if(!s->mcsel){
                            mx = h263_decode_motion(s, pred_x, s->f_code);
                            if (mx >= 0xffff)
                                return -1;

                            my = h263_decode_motion(s, pred_y, s->f_code);
                            if (my >= 0xffff)
                                return -1;
                            s->current_picture.mb_type[xy]= MB_TYPE_16x16 | MB_TYPE_L0;
                        } else {
                            mx = get_amv(s, 0);
                            my = get_amv(s, 1);
                            s->current_picture.mb_type[xy]= MB_TYPE_16x16 | MB_TYPE_GMC | MB_TYPE_L0;
                        }

                        mot_val[0       ]= mot_val[2       ] =
                        mot_val[0+stride]= mot_val[2+stride]= mx;
                        mot_val[1       ]= mot_val[3       ]=
                        mot_val[1+stride]= mot_val[3+stride]= my;
                    } else {
                        int i;
                        s->current_picture.mb_type[xy]= MB_TYPE_8x8 | MB_TYPE_L0;
                        for(i=0;i<4;i++) {
                            int16_t *mot_val= h263_pred_motion(s, i, 0, &pred_x, &pred_y);
                            mx = h263_decode_motion(s, pred_x, s->f_code);
                            if (mx >= 0xffff)
                                return -1;

                            my = h263_decode_motion(s, pred_y, s->f_code);
                            if (my >= 0xffff)
                                return -1;
                            mot_val[0] = mx;
                            mot_val[1] = my;
                        }
                    }
                }
            }
        }
        s->mb_x= 0;
    }

    return mb_num;
}

/**
 * decode second partition.
 * @return <0 if an error occurred
 */
static int mpeg4_decode_partition_b(MpegEncContext *s, int mb_count){
    int mb_num=0;
    static const int8_t quant_tab[4] = { -1, -2, 1, 2 };

    s->mb_x= s->resync_mb_x;
    s->first_slice_line=1;
    for(s->mb_y= s->resync_mb_y; mb_num < mb_count; s->mb_y++){
        ff_init_block_index(s);
        for(; mb_num < mb_count && s->mb_x<s->mb_width; s->mb_x++){
            const int xy= s->mb_x + s->mb_y*s->mb_stride;

            mb_num++;
            ff_update_block_index(s);
            if(s->mb_x == s->resync_mb_x && s->mb_y == s->resync_mb_y+1)
                s->first_slice_line=0;

            if(s->pict_type==FF_I_TYPE){
                int ac_pred= get_bits1(&s->gb);
                int cbpy = get_vlc2(&s->gb, cbpy_vlc.table, CBPY_VLC_BITS, 1);
                if(cbpy<0){
                    av_log(s->avctx, AV_LOG_ERROR, "cbpy corrupted at %d %d\n", s->mb_x, s->mb_y);
                    return -1;
                }

                s->cbp_table[xy]|= cbpy<<2;
                s->current_picture.mb_type[xy] |= ac_pred*MB_TYPE_ACPRED;
            }else{ /* P || S_TYPE */
                if(IS_INTRA(s->current_picture.mb_type[xy])){
                    int dir=0,i;
                    int ac_pred = get_bits1(&s->gb);
                    int cbpy = get_vlc2(&s->gb, cbpy_vlc.table, CBPY_VLC_BITS, 1);

                    if(cbpy<0){
                        av_log(s->avctx, AV_LOG_ERROR, "I cbpy corrupted at %d %d\n", s->mb_x, s->mb_y);
                        return -1;
                    }

                    if(s->cbp_table[xy] & 8) {
                        ff_set_qscale(s, s->qscale + quant_tab[get_bits(&s->gb, 2)]);
                    }
                    s->current_picture.qscale_table[xy]= s->qscale;

                    for(i=0; i<6; i++){
                        int dc_pred_dir;
                        int dc= mpeg4_decode_dc(s, i, &dc_pred_dir);
                        if(dc < 0){
                            av_log(s->avctx, AV_LOG_ERROR, "DC corrupted at %d %d\n", s->mb_x, s->mb_y);
                            return -1;
                        }
                        dir<<=1;
                        if(dc_pred_dir) dir|=1;
                    }
                    s->cbp_table[xy]&= 3; //remove dquant
                    s->cbp_table[xy]|= cbpy<<2;
                    s->current_picture.mb_type[xy] |= ac_pred*MB_TYPE_ACPRED;
                    s->pred_dir_table[xy]= dir;
                }else if(IS_SKIP(s->current_picture.mb_type[xy])){
                    s->current_picture.qscale_table[xy]= s->qscale;
                    s->cbp_table[xy]= 0;
                }else{
                    int cbpy = get_vlc2(&s->gb, cbpy_vlc.table, CBPY_VLC_BITS, 1);

                    if(cbpy<0){
                        av_log(s->avctx, AV_LOG_ERROR, "P cbpy corrupted at %d %d\n", s->mb_x, s->mb_y);
                        return -1;
                    }

                    if(s->cbp_table[xy] & 8) {
                        ff_set_qscale(s, s->qscale + quant_tab[get_bits(&s->gb, 2)]);
                    }
                    s->current_picture.qscale_table[xy]= s->qscale;

                    s->cbp_table[xy]&= 3; //remove dquant
                    s->cbp_table[xy]|= (cbpy^0xf)<<2;
                }
            }
        }
        if(mb_num >= mb_count) return 0;
        s->mb_x= 0;
    }
    return 0;
}

/**
 * decodes the first & second partition
 * @return <0 if error (and sets error type in the error_status_table)
 */
int ff_mpeg4_decode_partitions(MpegEncContext *s)
{
    int mb_num;
    const int part_a_error= s->pict_type==FF_I_TYPE ? (DC_ERROR|MV_ERROR) : MV_ERROR;
    const int part_a_end  = s->pict_type==FF_I_TYPE ? (DC_END  |MV_END)   : MV_END;

    mb_num= mpeg4_decode_partition_a(s);
    if(mb_num<0){
        ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, part_a_error);
        return -1;
    }

    if(s->resync_mb_x + s->resync_mb_y*s->mb_width + mb_num > s->mb_num){
        av_log(s->avctx, AV_LOG_ERROR, "slice below monitor ...\n");
        ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, part_a_error);
        return -1;
    }

    s->mb_num_left= mb_num;

    if(s->pict_type==FF_I_TYPE){
        while(show_bits(&s->gb, 9) == 1)
            skip_bits(&s->gb, 9);
        if(get_bits_long(&s->gb, 19)!=DC_MARKER){
            av_log(s->avctx, AV_LOG_ERROR, "marker missing after first I partition at %d %d\n", s->mb_x, s->mb_y);
            return -1;
        }
    }else{
        while(show_bits(&s->gb, 10) == 1)
            skip_bits(&s->gb, 10);
        if(get_bits(&s->gb, 17)!=MOTION_MARKER){
            av_log(s->avctx, AV_LOG_ERROR, "marker missing after first P partition at %d %d\n", s->mb_x, s->mb_y);
            return -1;
        }
    }
    ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x-1, s->mb_y, part_a_end);

    if( mpeg4_decode_partition_b(s, mb_num) < 0){
        if(s->pict_type==FF_P_TYPE)
            ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, DC_ERROR);
        return -1;
    }else{
        if(s->pict_type==FF_P_TYPE)
            ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x-1, s->mb_y, DC_END);
    }

    return 0;
}

/**
 * decode partition C of one MB.
 * @return <0 if an error occurred
 */
static int mpeg4_decode_partitioned_mb(MpegEncContext *s, DCTELEM block[6][64])
{
    int cbp, mb_type;
    const int xy= s->mb_x + s->mb_y*s->mb_stride;

    mb_type= s->current_picture.mb_type[xy];
    cbp = s->cbp_table[xy];

    s->use_intra_dc_vlc= s->qscale < s->intra_dc_threshold;

    if(s->current_picture.qscale_table[xy] != s->qscale){
        ff_set_qscale(s, s->current_picture.qscale_table[xy] );
    }

    if (s->pict_type == FF_P_TYPE || s->pict_type==FF_S_TYPE) {
        int i;
        for(i=0; i<4; i++){
            s->mv[0][i][0] = s->current_picture.motion_val[0][ s->block_index[i] ][0];
            s->mv[0][i][1] = s->current_picture.motion_val[0][ s->block_index[i] ][1];
        }
        s->mb_intra = IS_INTRA(mb_type);

        if (IS_SKIP(mb_type)) {
            /* skip mb */
            for(i=0;i<6;i++)
                s->block_last_index[i] = -1;
            s->mv_dir = MV_DIR_FORWARD;
            s->mv_type = MV_TYPE_16X16;
            if(s->pict_type==FF_S_TYPE && s->vol_sprite_usage==GMC_SPRITE){
                s->mcsel=1;
                s->mb_skipped = 0;
            }else{
                s->mcsel=0;
                s->mb_skipped = 1;
            }
        }else if(s->mb_intra){
            s->ac_pred = IS_ACPRED(s->current_picture.mb_type[xy]);
        }else if(!s->mb_intra){
//            s->mcsel= 0; //FIXME do we need to init that

            s->mv_dir = MV_DIR_FORWARD;
            if (IS_8X8(mb_type)) {
                s->mv_type = MV_TYPE_8X8;
            } else {
                s->mv_type = MV_TYPE_16X16;
            }
        }
    } else { /* I-Frame */
        s->mb_intra = 1;
        s->ac_pred = IS_ACPRED(s->current_picture.mb_type[xy]);
    }

    if (!IS_SKIP(mb_type)) {
        int i;
        s->dsp.clear_blocks(s->block[0]);
        /* decode each block */
        for (i = 0; i < 6; i++) {
            if(mpeg4_decode_block(s, block[i], i, cbp&32, s->mb_intra, s->rvlc) < 0){
                av_log(s->avctx, AV_LOG_ERROR, "texture corrupted at %d %d %d\n", s->mb_x, s->mb_y, s->mb_intra);
                return -1;
            }
            cbp+=cbp;
        }
    }

    /* per-MB end of slice check */

    if(--s->mb_num_left <= 0){
//printf("%06X %d\n", show_bits(&s->gb, 24), s->gb.size_in_bits - get_bits_count(&s->gb));
        if(mpeg4_is_resync(s))
            return SLICE_END;
        else
            return SLICE_NOEND;
    }else{
        if(mpeg4_is_resync(s)){
            const int delta= s->mb_x + 1 == s->mb_width ? 2 : 1;
            if(s->cbp_table[xy+delta])
                return SLICE_END;
        }
        return SLICE_OK;
    }
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
            //fprintf(stderr, "\tCBPC: %d", cbpc);
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

int ff_mpeg4_decode_mb(MpegEncContext *s,
                      DCTELEM block[6][64])
{
    int cbpc, cbpy, i, cbp, pred_x, pred_y, mx, my, dquant;
    int16_t *mot_val;
    static int8_t quant_tab[4] = { -1, -2, 1, 2 };
    const int xy= s->mb_x + s->mb_y * s->mb_stride;

    assert(s->h263_pred);

    if (s->pict_type == FF_P_TYPE || s->pict_type==FF_S_TYPE) {
        do{
            if (get_bits1(&s->gb)) {
                /* skip mb */
                s->mb_intra = 0;
                for(i=0;i<6;i++)
                    s->block_last_index[i] = -1;
                s->mv_dir = MV_DIR_FORWARD;
                s->mv_type = MV_TYPE_16X16;
                if(s->pict_type==FF_S_TYPE && s->vol_sprite_usage==GMC_SPRITE){
                    s->current_picture.mb_type[xy]= MB_TYPE_SKIP | MB_TYPE_GMC | MB_TYPE_16x16 | MB_TYPE_L0;
                    s->mcsel=1;
                    s->mv[0][0][0]= get_amv(s, 0);
                    s->mv[0][0][1]= get_amv(s, 1);

                    s->mb_skipped = 0;
                }else{
                    s->current_picture.mb_type[xy]= MB_TYPE_SKIP | MB_TYPE_16x16 | MB_TYPE_L0;
                    s->mcsel=0;
                    s->mv[0][0][0] = 0;
                    s->mv[0][0][1] = 0;
                    s->mb_skipped = 1;
                }
                goto end;
            }
            cbpc = get_vlc2(&s->gb, inter_MCBPC_vlc.table, INTER_MCBPC_VLC_BITS, 2);
            //fprintf(stderr, "\tCBPC: %d", cbpc);
            if (cbpc < 0){
                av_log(s->avctx, AV_LOG_ERROR, "cbpc damaged at %d %d\n", s->mb_x, s->mb_y);
                return -1;
            }
        }while(cbpc == 20);

        s->dsp.clear_blocks(s->block[0]);
        dquant = cbpc & 8;
        s->mb_intra = ((cbpc & 4) != 0);
        if (s->mb_intra) goto intra;

        if(s->pict_type==FF_S_TYPE && s->vol_sprite_usage==GMC_SPRITE && (cbpc & 16) == 0)
            s->mcsel= get_bits1(&s->gb);
        else s->mcsel= 0;
        cbpy = get_vlc2(&s->gb, cbpy_vlc.table, CBPY_VLC_BITS, 1) ^ 0x0F;

        cbp = (cbpc & 3) | (cbpy << 2);
        if (dquant) {
            ff_set_qscale(s, s->qscale + quant_tab[get_bits(&s->gb, 2)]);
        }
        if((!s->progressive_sequence) && (cbp || (s->workaround_bugs&FF_BUG_XVID_ILACE)))
            s->interlaced_dct= get_bits1(&s->gb);

        s->mv_dir = MV_DIR_FORWARD;
        if ((cbpc & 16) == 0) {
            if(s->mcsel){
                s->current_picture.mb_type[xy]= MB_TYPE_GMC | MB_TYPE_16x16 | MB_TYPE_L0;
                /* 16x16 global motion prediction */
                s->mv_type = MV_TYPE_16X16;
                mx= get_amv(s, 0);
                my= get_amv(s, 1);
                s->mv[0][0][0] = mx;
                s->mv[0][0][1] = my;
            }else if((!s->progressive_sequence) && get_bits1(&s->gb)){
                s->current_picture.mb_type[xy]= MB_TYPE_16x8 | MB_TYPE_L0 | MB_TYPE_INTERLACED;
                /* 16x8 field motion prediction */
                s->mv_type= MV_TYPE_FIELD;

                s->field_select[0][0]= get_bits1(&s->gb);
                s->field_select[0][1]= get_bits1(&s->gb);

                h263_pred_motion(s, 0, 0, &pred_x, &pred_y);

                for(i=0; i<2; i++){
                    mx = h263_decode_motion(s, pred_x, s->f_code);
                    if (mx >= 0xffff)
                        return -1;

                    my = h263_decode_motion(s, pred_y/2, s->f_code);
                    if (my >= 0xffff)
                        return -1;

                    s->mv[0][i][0] = mx;
                    s->mv[0][i][1] = my;
                }
            }else{
                s->current_picture.mb_type[xy]= MB_TYPE_16x16 | MB_TYPE_L0;
                /* 16x16 motion prediction */
                s->mv_type = MV_TYPE_16X16;
                h263_pred_motion(s, 0, 0, &pred_x, &pred_y);
                mx = h263_decode_motion(s, pred_x, s->f_code);

                if (mx >= 0xffff)
                    return -1;

                my = h263_decode_motion(s, pred_y, s->f_code);

                if (my >= 0xffff)
                    return -1;
                s->mv[0][0][0] = mx;
                s->mv[0][0][1] = my;
            }
        } else {
            s->current_picture.mb_type[xy]= MB_TYPE_8x8 | MB_TYPE_L0;
            s->mv_type = MV_TYPE_8X8;
            for(i=0;i<4;i++) {
                mot_val = h263_pred_motion(s, i, 0, &pred_x, &pred_y);
                mx = h263_decode_motion(s, pred_x, s->f_code);
                if (mx >= 0xffff)
                    return -1;

                my = h263_decode_motion(s, pred_y, s->f_code);
                if (my >= 0xffff)
                    return -1;
                s->mv[0][i][0] = mx;
                s->mv[0][i][1] = my;
                mot_val[0] = mx;
                mot_val[1] = my;
            }
        }
    } else if(s->pict_type==FF_B_TYPE) {
        int modb1; // first bit of modb
        int modb2; // second bit of modb
        int mb_type;

        s->mb_intra = 0; //B-frames never contain intra blocks
        s->mcsel=0;      //     ...               true gmc blocks

        if(s->mb_x==0){
            for(i=0; i<2; i++){
                s->last_mv[i][0][0]=
                s->last_mv[i][0][1]=
                s->last_mv[i][1][0]=
                s->last_mv[i][1][1]= 0;
            }
        }

        /* if we skipped it in the future P Frame than skip it now too */
        s->mb_skipped= s->next_picture.mbskip_table[s->mb_y * s->mb_stride + s->mb_x]; // Note, skiptab=0 if last was GMC

        if(s->mb_skipped){
                /* skip mb */
            for(i=0;i<6;i++)
                s->block_last_index[i] = -1;

            s->mv_dir = MV_DIR_FORWARD;
            s->mv_type = MV_TYPE_16X16;
            s->mv[0][0][0] = 0;
            s->mv[0][0][1] = 0;
            s->mv[1][0][0] = 0;
            s->mv[1][0][1] = 0;
            s->current_picture.mb_type[xy]= MB_TYPE_SKIP | MB_TYPE_16x16 | MB_TYPE_L0;
            goto end;
        }

        modb1= get_bits1(&s->gb);
        if(modb1){
            mb_type= MB_TYPE_DIRECT2 | MB_TYPE_SKIP | MB_TYPE_L0L1; //like MB_TYPE_B_DIRECT but no vectors coded
            cbp=0;
        }else{
            modb2= get_bits1(&s->gb);
            mb_type= get_vlc2(&s->gb, mb_type_b_vlc.table, MB_TYPE_B_VLC_BITS, 1);
            if(mb_type<0){
                av_log(s->avctx, AV_LOG_ERROR, "illegal MB_type\n");
                return -1;
            }
            mb_type= mb_type_b_map[ mb_type ];
            if(modb2) cbp= 0;
            else{
                s->dsp.clear_blocks(s->block[0]);
                cbp= get_bits(&s->gb, 6);
            }

            if ((!IS_DIRECT(mb_type)) && cbp) {
                if(get_bits1(&s->gb)){
                    ff_set_qscale(s, s->qscale + get_bits1(&s->gb)*4 - 2);
                }
            }

            if(!s->progressive_sequence){
                if(cbp)
                    s->interlaced_dct= get_bits1(&s->gb);

                if(!IS_DIRECT(mb_type) && get_bits1(&s->gb)){
                    mb_type |= MB_TYPE_16x8 | MB_TYPE_INTERLACED;
                    mb_type &= ~MB_TYPE_16x16;

                    if(USES_LIST(mb_type, 0)){
                        s->field_select[0][0]= get_bits1(&s->gb);
                        s->field_select[0][1]= get_bits1(&s->gb);
                    }
                    if(USES_LIST(mb_type, 1)){
                        s->field_select[1][0]= get_bits1(&s->gb);
                        s->field_select[1][1]= get_bits1(&s->gb);
                    }
                }
            }

            s->mv_dir = 0;
            if((mb_type & (MB_TYPE_DIRECT2|MB_TYPE_INTERLACED)) == 0){
                s->mv_type= MV_TYPE_16X16;

                if(USES_LIST(mb_type, 0)){
                    s->mv_dir = MV_DIR_FORWARD;

                    mx = h263_decode_motion(s, s->last_mv[0][0][0], s->f_code);
                    my = h263_decode_motion(s, s->last_mv[0][0][1], s->f_code);
                    s->last_mv[0][1][0]= s->last_mv[0][0][0]= s->mv[0][0][0] = mx;
                    s->last_mv[0][1][1]= s->last_mv[0][0][1]= s->mv[0][0][1] = my;
                }

                if(USES_LIST(mb_type, 1)){
                    s->mv_dir |= MV_DIR_BACKWARD;

                    mx = h263_decode_motion(s, s->last_mv[1][0][0], s->b_code);
                    my = h263_decode_motion(s, s->last_mv[1][0][1], s->b_code);
                    s->last_mv[1][1][0]= s->last_mv[1][0][0]= s->mv[1][0][0] = mx;
                    s->last_mv[1][1][1]= s->last_mv[1][0][1]= s->mv[1][0][1] = my;
                }
            }else if(!IS_DIRECT(mb_type)){
                s->mv_type= MV_TYPE_FIELD;

                if(USES_LIST(mb_type, 0)){
                    s->mv_dir = MV_DIR_FORWARD;

                    for(i=0; i<2; i++){
                        mx = h263_decode_motion(s, s->last_mv[0][i][0]  , s->f_code);
                        my = h263_decode_motion(s, s->last_mv[0][i][1]/2, s->f_code);
                        s->last_mv[0][i][0]=  s->mv[0][i][0] = mx;
                        s->last_mv[0][i][1]= (s->mv[0][i][1] = my)*2;
                    }
                }

                if(USES_LIST(mb_type, 1)){
                    s->mv_dir |= MV_DIR_BACKWARD;

                    for(i=0; i<2; i++){
                        mx = h263_decode_motion(s, s->last_mv[1][i][0]  , s->b_code);
                        my = h263_decode_motion(s, s->last_mv[1][i][1]/2, s->b_code);
                        s->last_mv[1][i][0]=  s->mv[1][i][0] = mx;
                        s->last_mv[1][i][1]= (s->mv[1][i][1] = my)*2;
                    }
                }
            }
        }

        if(IS_DIRECT(mb_type)){
            if(IS_SKIP(mb_type))
                mx=my=0;
            else{
                mx = h263_decode_motion(s, 0, 1);
                my = h263_decode_motion(s, 0, 1);
            }

            s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD | MV_DIRECT;
            mb_type |= ff_mpeg4_set_direct_mv(s, mx, my);
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

        dquant = cbpc & 4;
        s->mb_intra = 1;
intra:
        s->ac_pred = get_bits1(&s->gb);
        if(s->ac_pred)
            s->current_picture.mb_type[xy]= MB_TYPE_INTRA | MB_TYPE_ACPRED;
        else
            s->current_picture.mb_type[xy]= MB_TYPE_INTRA;

        cbpy = get_vlc2(&s->gb, cbpy_vlc.table, CBPY_VLC_BITS, 1);
        if(cbpy<0){
            av_log(s->avctx, AV_LOG_ERROR, "I cbpy damaged at %d %d\n", s->mb_x, s->mb_y);
            return -1;
        }
        cbp = (cbpc & 3) | (cbpy << 2);

        s->use_intra_dc_vlc= s->qscale < s->intra_dc_threshold;

        if (dquant) {
            ff_set_qscale(s, s->qscale + quant_tab[get_bits(&s->gb, 2)]);
        }

        if(!s->progressive_sequence)
            s->interlaced_dct= get_bits1(&s->gb);

        s->dsp.clear_blocks(s->block[0]);
        /* decode each block */
        for (i = 0; i < 6; i++) {
            if (mpeg4_decode_block(s, block[i], i, cbp&32, 1, 0) < 0)
                return -1;
            cbp+=cbp;
        }
        goto end;
    }

    /* decode each block */
    for (i = 0; i < 6; i++) {
        if (mpeg4_decode_block(s, block[i], i, cbp&32, 0, 0) < 0)
            return -1;
        cbp+=cbp;
    }
end:

        /* per-MB end of slice check */
    if(s->codec_id==CODEC_ID_MPEG4){
        if(mpeg4_is_resync(s)){
            const int delta= s->mb_x + 1 == s->mb_width ? 2 : 1;
            if(s->pict_type==FF_B_TYPE && s->next_picture.mbskip_table[xy + delta])
                return SLICE_OK;
            return SLICE_END;
        }
    }

    return SLICE_OK;
}

static int h263_decode_motion(MpegEncContext * s, int pred, int f_code)
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
#ifdef DEBUG
   av_log( s->avctx, AV_LOG_DEBUG,"H.263+ UMV Motion = %d\n", code);
#endif
   return code;

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
            if (s->h263_flv > 1) {
                int is11 = get_bits1(&s->gb);
                last = get_bits1(&s->gb);
                run = get_bits(&s->gb, 6);
                if(is11){
                    level = get_sbits(&s->gb, 11);
                } else {
                    level = get_sbits(&s->gb, 7);
                }
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

/**
 * decodes the dc value.
 * @param n block index (0-3 are luma, 4-5 are chroma)
 * @param dir_ptr the prediction direction will be stored here
 * @return the quantized dc
 */
static inline int mpeg4_decode_dc(MpegEncContext * s, int n, int *dir_ptr)
{
    int level, code;

    if (n < 4)
        code = get_vlc2(&s->gb, dc_lum.table, DC_VLC_BITS, 1);
    else
        code = get_vlc2(&s->gb, dc_chrom.table, DC_VLC_BITS, 1);
    if (code < 0 || code > 9 /* && s->nbit<9 */){
        av_log(s->avctx, AV_LOG_ERROR, "illegal dc vlc\n");
        return -1;
    }
    if (code == 0) {
        level = 0;
    } else {
        if(IS_3IV1){
            if(code==1)
                level= 2*get_bits1(&s->gb)-1;
            else{
                if(get_bits1(&s->gb))
                    level = get_bits(&s->gb, code-1) + (1<<(code-1));
                else
                    level = -get_bits(&s->gb, code-1) - (1<<(code-1));
            }
        }else{
            level = get_xbits(&s->gb, code);
        }

        if (code > 8){
            if(get_bits1(&s->gb)==0){ /* marker */
                if(s->error_recognition>=2){
                    av_log(s->avctx, AV_LOG_ERROR, "dc marker bit missing\n");
                    return -1;
                }
            }
        }
    }

    return ff_mpeg4_pred_dc(s, n, level, dir_ptr, 0);
}

/**
 * decodes a block.
 * @return <0 if an error occurred
 */
static inline int mpeg4_decode_block(MpegEncContext * s, DCTELEM * block,
                              int n, int coded, int intra, int rvlc)
{
    int level, i, last, run;
    int dc_pred_dir;
    RLTable * rl;
    RL_VLC_ELEM * rl_vlc;
    const uint8_t * scan_table;
    int qmul, qadd;

    //Note intra & rvlc should be optimized away if this is inlined

    if(intra) {
      if(s->use_intra_dc_vlc){
        /* DC coef */
        if(s->partitioned_frame){
            level = s->dc_val[0][ s->block_index[n] ];
            if(n<4) level= FASTDIV((level + (s->y_dc_scale>>1)), s->y_dc_scale);
            else    level= FASTDIV((level + (s->c_dc_scale>>1)), s->c_dc_scale);
            dc_pred_dir= (s->pred_dir_table[s->mb_x + s->mb_y*s->mb_stride]<<n)&32;
        }else{
            level = mpeg4_decode_dc(s, n, &dc_pred_dir);
            if (level < 0)
                return -1;
        }
        block[0] = level;
        i = 0;
      }else{
            i = -1;
            ff_mpeg4_pred_dc(s, n, 0, &dc_pred_dir, 0);
      }
      if (!coded)
          goto not_coded;

      if(rvlc){
          rl = &rvlc_rl_intra;
          rl_vlc = rvlc_rl_intra.rl_vlc[0];
      }else{
          rl = &rl_intra;
          rl_vlc = rl_intra.rl_vlc[0];
      }
      if (s->ac_pred) {
          if (dc_pred_dir == 0)
              scan_table = s->intra_v_scantable.permutated; /* left */
          else
              scan_table = s->intra_h_scantable.permutated; /* top */
      } else {
            scan_table = s->intra_scantable.permutated;
      }
      qmul=1;
      qadd=0;
    } else {
        i = -1;
        if (!coded) {
            s->block_last_index[n] = i;
            return 0;
        }
        if(rvlc) rl = &rvlc_rl_inter;
        else     rl = &rl_inter;

        scan_table = s->intra_scantable.permutated;

        if(s->mpeg_quant){
            qmul=1;
            qadd=0;
            if(rvlc){
                rl_vlc = rvlc_rl_inter.rl_vlc[0];
            }else{
                rl_vlc = rl_inter.rl_vlc[0];
            }
        }else{
            qmul = s->qscale << 1;
            qadd = (s->qscale - 1) | 1;
            if(rvlc){
                rl_vlc = rvlc_rl_inter.rl_vlc[s->qscale];
            }else{
                rl_vlc = rl_inter.rl_vlc[s->qscale];
            }
        }
    }
  {
    OPEN_READER(re, &s->gb);
    for(;;) {
        UPDATE_CACHE(re, &s->gb);
        GET_RL_VLC(level, run, re, &s->gb, rl_vlc, TEX_VLC_BITS, 2, 0);
        if (level==0) {
          /* escape */
          if(rvlc){
                if(SHOW_UBITS(re, &s->gb, 1)==0){
                    av_log(s->avctx, AV_LOG_ERROR, "1. marker bit missing in rvlc esc\n");
                    return -1;
                }; SKIP_CACHE(re, &s->gb, 1);

                last=  SHOW_UBITS(re, &s->gb, 1); SKIP_CACHE(re, &s->gb, 1);
                run=   SHOW_UBITS(re, &s->gb, 6); LAST_SKIP_CACHE(re, &s->gb, 6);
                SKIP_COUNTER(re, &s->gb, 1+1+6);
                UPDATE_CACHE(re, &s->gb);

                if(SHOW_UBITS(re, &s->gb, 1)==0){
                    av_log(s->avctx, AV_LOG_ERROR, "2. marker bit missing in rvlc esc\n");
                    return -1;
                }; SKIP_CACHE(re, &s->gb, 1);

                level= SHOW_UBITS(re, &s->gb, 11); SKIP_CACHE(re, &s->gb, 11);

                if(SHOW_UBITS(re, &s->gb, 5)!=0x10){
                    av_log(s->avctx, AV_LOG_ERROR, "reverse esc missing\n");
                    return -1;
                }; SKIP_CACHE(re, &s->gb, 5);

                level=  level * qmul + qadd;
                level = (level ^ SHOW_SBITS(re, &s->gb, 1)) - SHOW_SBITS(re, &s->gb, 1); LAST_SKIP_CACHE(re, &s->gb, 1);
                SKIP_COUNTER(re, &s->gb, 1+11+5+1);

                i+= run + 1;
                if(last) i+=192;
          }else{
            int cache;
            cache= GET_CACHE(re, &s->gb);

            if(IS_3IV1)
                cache ^= 0xC0000000;

            if (cache&0x80000000) {
                if (cache&0x40000000) {
                    /* third escape */
                    SKIP_CACHE(re, &s->gb, 2);
                    last=  SHOW_UBITS(re, &s->gb, 1); SKIP_CACHE(re, &s->gb, 1);
                    run=   SHOW_UBITS(re, &s->gb, 6); LAST_SKIP_CACHE(re, &s->gb, 6);
                    SKIP_COUNTER(re, &s->gb, 2+1+6);
                    UPDATE_CACHE(re, &s->gb);

                    if(IS_3IV1){
                        level= SHOW_SBITS(re, &s->gb, 12); LAST_SKIP_BITS(re, &s->gb, 12);
                    }else{
                        if(SHOW_UBITS(re, &s->gb, 1)==0){
                            av_log(s->avctx, AV_LOG_ERROR, "1. marker bit missing in 3. esc\n");
                            return -1;
                        }; SKIP_CACHE(re, &s->gb, 1);

                        level= SHOW_SBITS(re, &s->gb, 12); SKIP_CACHE(re, &s->gb, 12);

                        if(SHOW_UBITS(re, &s->gb, 1)==0){
                            av_log(s->avctx, AV_LOG_ERROR, "2. marker bit missing in 3. esc\n");
                            return -1;
                        }; LAST_SKIP_CACHE(re, &s->gb, 1);

                        SKIP_COUNTER(re, &s->gb, 1+12+1);
                    }

#if 0
                    if(s->error_recognition >= FF_ER_COMPLIANT){
                        const int abs_level= FFABS(level);
                        if(abs_level<=MAX_LEVEL && run<=MAX_RUN){
                            const int run1= run - rl->max_run[last][abs_level] - 1;
                            if(abs_level <= rl->max_level[last][run]){
                                av_log(s->avctx, AV_LOG_ERROR, "illegal 3. esc, vlc encoding possible\n");
                                return -1;
                            }
                            if(s->error_recognition > FF_ER_COMPLIANT){
                                if(abs_level <= rl->max_level[last][run]*2){
                                    av_log(s->avctx, AV_LOG_ERROR, "illegal 3. esc, esc 1 encoding possible\n");
                                    return -1;
                                }
                                if(run1 >= 0 && abs_level <= rl->max_level[last][run1]){
                                    av_log(s->avctx, AV_LOG_ERROR, "illegal 3. esc, esc 2 encoding possible\n");
                                    return -1;
                                }
                            }
                        }
                    }
#endif
                    if (level>0) level= level * qmul + qadd;
                    else         level= level * qmul - qadd;

                    if((unsigned)(level + 2048) > 4095){
                        if(s->error_recognition > FF_ER_COMPLIANT){
                            if(level > 2560 || level<-2560){
                                av_log(s->avctx, AV_LOG_ERROR, "|level| overflow in 3. esc, qp=%d\n", s->qscale);
                                return -1;
                            }
                        }
                        level= level<0 ? -2048 : 2047;
                    }

                    i+= run + 1;
                    if(last) i+=192;
                } else {
                    /* second escape */
#if MIN_CACHE_BITS < 20
                    LAST_SKIP_BITS(re, &s->gb, 2);
                    UPDATE_CACHE(re, &s->gb);
#else
                    SKIP_BITS(re, &s->gb, 2);
#endif
                    GET_RL_VLC(level, run, re, &s->gb, rl_vlc, TEX_VLC_BITS, 2, 1);
                    i+= run + rl->max_run[run>>7][level/qmul] +1; //FIXME opt indexing
                    level = (level ^ SHOW_SBITS(re, &s->gb, 1)) - SHOW_SBITS(re, &s->gb, 1);
                    LAST_SKIP_BITS(re, &s->gb, 1);
                }
            } else {
                /* first escape */
#if MIN_CACHE_BITS < 19
                LAST_SKIP_BITS(re, &s->gb, 1);
                UPDATE_CACHE(re, &s->gb);
#else
                SKIP_BITS(re, &s->gb, 1);
#endif
                GET_RL_VLC(level, run, re, &s->gb, rl_vlc, TEX_VLC_BITS, 2, 1);
                i+= run;
                level = level + rl->max_level[run>>7][(run-1)&63] * qmul;//FIXME opt indexing
                level = (level ^ SHOW_SBITS(re, &s->gb, 1)) - SHOW_SBITS(re, &s->gb, 1);
                LAST_SKIP_BITS(re, &s->gb, 1);
            }
          }
        } else {
            i+= run;
            level = (level ^ SHOW_SBITS(re, &s->gb, 1)) - SHOW_SBITS(re, &s->gb, 1);
            LAST_SKIP_BITS(re, &s->gb, 1);
        }
        if (i > 62){
            i-= 192;
            if(i&(~63)){
                av_log(s->avctx, AV_LOG_ERROR, "ac-tex damaged at %d %d\n", s->mb_x, s->mb_y);
                return -1;
            }

            block[scan_table[i]] = level;
            break;
        }

        block[scan_table[i]] = level;
    }
    CLOSE_READER(re, &s->gb);
  }
 not_coded:
    if (intra) {
        if(!s->use_intra_dc_vlc){
            block[0] = ff_mpeg4_pred_dc(s, n, block[0], &dc_pred_dir, 0);

            i -= i>>31; //if(i == -1) i=0;
        }

        mpeg4_pred_ac(s, block, n, dc_pred_dir);
        if (s->ac_pred) {
            i = 63; /* XXX: not optimal */
        }
    }
    s->block_last_index[n] = i;
    return 0;
}

/* most is hardcoded. should extend to handle all h263 streams */
int h263_decode_picture_header(MpegEncContext *s)
{
    int format, width, height, i;
    uint32_t startcode;

    align_get_bits(&s->gb);

    startcode= get_bits(&s->gb, 22-8);

    for(i= s->gb.size_in_bits - get_bits_count(&s->gb); i>24; i-=8) {
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
                    s->avctx->sample_aspect_ratio= pixel_aspect[s->aspect_ratio_info];
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
//                av_log(s->avctx, AV_LOG_DEBUG, "%d/%d\n", s->avctx->time_base.den, s->avctx->time_base.num);
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

     if(s->avctx->debug&FF_DEBUG_PICT_INFO){
        show_pict_info(s);
     }
#if 1
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
#endif

    return 0;
}

static void mpeg4_decode_sprite_trajectory(MpegEncContext * s, GetBitContext *gb)
{
    int i;
    int a= 2<<s->sprite_warping_accuracy;
    int rho= 3-s->sprite_warping_accuracy;
    int r=16/a;
    const int vop_ref[4][2]= {{0,0}, {s->width,0}, {0, s->height}, {s->width, s->height}}; // only true for rectangle shapes
    int d[4][2]={{0,0}, {0,0}, {0,0}, {0,0}};
    int sprite_ref[4][2];
    int virtual_ref[2][2];
    int w2, h2, w3, h3;
    int alpha=0, beta=0;
    int w= s->width;
    int h= s->height;
    int min_ab;

    for(i=0; i<s->num_sprite_warping_points; i++){
        int length;
        int x=0, y=0;

        length= get_vlc2(gb, sprite_trajectory.table, SPRITE_TRAJ_VLC_BITS, 3);
        if(length){
            x= get_xbits(gb, length);
        }
        if(!(s->divx_version==500 && s->divx_build==413)) skip_bits1(gb); /* marker bit */

        length= get_vlc2(gb, sprite_trajectory.table, SPRITE_TRAJ_VLC_BITS, 3);
        if(length){
            y=get_xbits(gb, length);
        }
        skip_bits1(gb); /* marker bit */
//printf("%d %d %d %d\n", x, y, i, s->sprite_warping_accuracy);
        s->sprite_traj[i][0]= d[i][0]= x;
        s->sprite_traj[i][1]= d[i][1]= y;
    }
    for(; i<4; i++)
        s->sprite_traj[i][0]= s->sprite_traj[i][1]= 0;

    while((1<<alpha)<w) alpha++;
    while((1<<beta )<h) beta++; // there seems to be a typo in the mpeg4 std for the definition of w' and h'
    w2= 1<<alpha;
    h2= 1<<beta;

// Note, the 4th point isn't used for GMC
    if(s->divx_version==500 && s->divx_build==413){
        sprite_ref[0][0]= a*vop_ref[0][0] + d[0][0];
        sprite_ref[0][1]= a*vop_ref[0][1] + d[0][1];
        sprite_ref[1][0]= a*vop_ref[1][0] + d[0][0] + d[1][0];
        sprite_ref[1][1]= a*vop_ref[1][1] + d[0][1] + d[1][1];
        sprite_ref[2][0]= a*vop_ref[2][0] + d[0][0] + d[2][0];
        sprite_ref[2][1]= a*vop_ref[2][1] + d[0][1] + d[2][1];
    } else {
        sprite_ref[0][0]= (a>>1)*(2*vop_ref[0][0] + d[0][0]);
        sprite_ref[0][1]= (a>>1)*(2*vop_ref[0][1] + d[0][1]);
        sprite_ref[1][0]= (a>>1)*(2*vop_ref[1][0] + d[0][0] + d[1][0]);
        sprite_ref[1][1]= (a>>1)*(2*vop_ref[1][1] + d[0][1] + d[1][1]);
        sprite_ref[2][0]= (a>>1)*(2*vop_ref[2][0] + d[0][0] + d[2][0]);
        sprite_ref[2][1]= (a>>1)*(2*vop_ref[2][1] + d[0][1] + d[2][1]);
    }
/*    sprite_ref[3][0]= (a>>1)*(2*vop_ref[3][0] + d[0][0] + d[1][0] + d[2][0] + d[3][0]);
    sprite_ref[3][1]= (a>>1)*(2*vop_ref[3][1] + d[0][1] + d[1][1] + d[2][1] + d[3][1]); */

// this is mostly identical to the mpeg4 std (and is totally unreadable because of that ...)
// perhaps it should be reordered to be more readable ...
// the idea behind this virtual_ref mess is to be able to use shifts later per pixel instead of divides
// so the distance between points is converted from w&h based to w2&h2 based which are of the 2^x form
    virtual_ref[0][0]= 16*(vop_ref[0][0] + w2)
        + ROUNDED_DIV(((w - w2)*(r*sprite_ref[0][0] - 16*vop_ref[0][0]) + w2*(r*sprite_ref[1][0] - 16*vop_ref[1][0])),w);
    virtual_ref[0][1]= 16*vop_ref[0][1]
        + ROUNDED_DIV(((w - w2)*(r*sprite_ref[0][1] - 16*vop_ref[0][1]) + w2*(r*sprite_ref[1][1] - 16*vop_ref[1][1])),w);
    virtual_ref[1][0]= 16*vop_ref[0][0]
        + ROUNDED_DIV(((h - h2)*(r*sprite_ref[0][0] - 16*vop_ref[0][0]) + h2*(r*sprite_ref[2][0] - 16*vop_ref[2][0])),h);
    virtual_ref[1][1]= 16*(vop_ref[0][1] + h2)
        + ROUNDED_DIV(((h - h2)*(r*sprite_ref[0][1] - 16*vop_ref[0][1]) + h2*(r*sprite_ref[2][1] - 16*vop_ref[2][1])),h);

    switch(s->num_sprite_warping_points)
    {
        case 0:
            s->sprite_offset[0][0]= 0;
            s->sprite_offset[0][1]= 0;
            s->sprite_offset[1][0]= 0;
            s->sprite_offset[1][1]= 0;
            s->sprite_delta[0][0]= a;
            s->sprite_delta[0][1]= 0;
            s->sprite_delta[1][0]= 0;
            s->sprite_delta[1][1]= a;
            s->sprite_shift[0]= 0;
            s->sprite_shift[1]= 0;
            break;
        case 1: //GMC only
            s->sprite_offset[0][0]= sprite_ref[0][0] - a*vop_ref[0][0];
            s->sprite_offset[0][1]= sprite_ref[0][1] - a*vop_ref[0][1];
            s->sprite_offset[1][0]= ((sprite_ref[0][0]>>1)|(sprite_ref[0][0]&1)) - a*(vop_ref[0][0]/2);
            s->sprite_offset[1][1]= ((sprite_ref[0][1]>>1)|(sprite_ref[0][1]&1)) - a*(vop_ref[0][1]/2);
            s->sprite_delta[0][0]= a;
            s->sprite_delta[0][1]= 0;
            s->sprite_delta[1][0]= 0;
            s->sprite_delta[1][1]= a;
            s->sprite_shift[0]= 0;
            s->sprite_shift[1]= 0;
            break;
        case 2:
            s->sprite_offset[0][0]= (sprite_ref[0][0]<<(alpha+rho))
                                                  + (-r*sprite_ref[0][0] + virtual_ref[0][0])*(-vop_ref[0][0])
                                                  + ( r*sprite_ref[0][1] - virtual_ref[0][1])*(-vop_ref[0][1])
                                                  + (1<<(alpha+rho-1));
            s->sprite_offset[0][1]= (sprite_ref[0][1]<<(alpha+rho))
                                                  + (-r*sprite_ref[0][1] + virtual_ref[0][1])*(-vop_ref[0][0])
                                                  + (-r*sprite_ref[0][0] + virtual_ref[0][0])*(-vop_ref[0][1])
                                                  + (1<<(alpha+rho-1));
            s->sprite_offset[1][0]= ( (-r*sprite_ref[0][0] + virtual_ref[0][0])*(-2*vop_ref[0][0] + 1)
                                     +( r*sprite_ref[0][1] - virtual_ref[0][1])*(-2*vop_ref[0][1] + 1)
                                     +2*w2*r*sprite_ref[0][0]
                                     - 16*w2
                                     + (1<<(alpha+rho+1)));
            s->sprite_offset[1][1]= ( (-r*sprite_ref[0][1] + virtual_ref[0][1])*(-2*vop_ref[0][0] + 1)
                                     +(-r*sprite_ref[0][0] + virtual_ref[0][0])*(-2*vop_ref[0][1] + 1)
                                     +2*w2*r*sprite_ref[0][1]
                                     - 16*w2
                                     + (1<<(alpha+rho+1)));
            s->sprite_delta[0][0]=   (-r*sprite_ref[0][0] + virtual_ref[0][0]);
            s->sprite_delta[0][1]=   (+r*sprite_ref[0][1] - virtual_ref[0][1]);
            s->sprite_delta[1][0]=   (-r*sprite_ref[0][1] + virtual_ref[0][1]);
            s->sprite_delta[1][1]=   (-r*sprite_ref[0][0] + virtual_ref[0][0]);

            s->sprite_shift[0]= alpha+rho;
            s->sprite_shift[1]= alpha+rho+2;
            break;
        case 3:
            min_ab= FFMIN(alpha, beta);
            w3= w2>>min_ab;
            h3= h2>>min_ab;
            s->sprite_offset[0][0]=  (sprite_ref[0][0]<<(alpha+beta+rho-min_ab))
                                   + (-r*sprite_ref[0][0] + virtual_ref[0][0])*h3*(-vop_ref[0][0])
                                   + (-r*sprite_ref[0][0] + virtual_ref[1][0])*w3*(-vop_ref[0][1])
                                   + (1<<(alpha+beta+rho-min_ab-1));
            s->sprite_offset[0][1]=  (sprite_ref[0][1]<<(alpha+beta+rho-min_ab))
                                   + (-r*sprite_ref[0][1] + virtual_ref[0][1])*h3*(-vop_ref[0][0])
                                   + (-r*sprite_ref[0][1] + virtual_ref[1][1])*w3*(-vop_ref[0][1])
                                   + (1<<(alpha+beta+rho-min_ab-1));
            s->sprite_offset[1][0]=  (-r*sprite_ref[0][0] + virtual_ref[0][0])*h3*(-2*vop_ref[0][0] + 1)
                                   + (-r*sprite_ref[0][0] + virtual_ref[1][0])*w3*(-2*vop_ref[0][1] + 1)
                                   + 2*w2*h3*r*sprite_ref[0][0]
                                   - 16*w2*h3
                                   + (1<<(alpha+beta+rho-min_ab+1));
            s->sprite_offset[1][1]=  (-r*sprite_ref[0][1] + virtual_ref[0][1])*h3*(-2*vop_ref[0][0] + 1)
                                   + (-r*sprite_ref[0][1] + virtual_ref[1][1])*w3*(-2*vop_ref[0][1] + 1)
                                   + 2*w2*h3*r*sprite_ref[0][1]
                                   - 16*w2*h3
                                   + (1<<(alpha+beta+rho-min_ab+1));
            s->sprite_delta[0][0]=   (-r*sprite_ref[0][0] + virtual_ref[0][0])*h3;
            s->sprite_delta[0][1]=   (-r*sprite_ref[0][0] + virtual_ref[1][0])*w3;
            s->sprite_delta[1][0]=   (-r*sprite_ref[0][1] + virtual_ref[0][1])*h3;
            s->sprite_delta[1][1]=   (-r*sprite_ref[0][1] + virtual_ref[1][1])*w3;

            s->sprite_shift[0]= alpha + beta + rho - min_ab;
            s->sprite_shift[1]= alpha + beta + rho - min_ab + 2;
            break;
    }
    /* try to simplify the situation */
    if(   s->sprite_delta[0][0] == a<<s->sprite_shift[0]
       && s->sprite_delta[0][1] == 0
       && s->sprite_delta[1][0] == 0
       && s->sprite_delta[1][1] == a<<s->sprite_shift[0])
    {
        s->sprite_offset[0][0]>>=s->sprite_shift[0];
        s->sprite_offset[0][1]>>=s->sprite_shift[0];
        s->sprite_offset[1][0]>>=s->sprite_shift[1];
        s->sprite_offset[1][1]>>=s->sprite_shift[1];
        s->sprite_delta[0][0]= a;
        s->sprite_delta[0][1]= 0;
        s->sprite_delta[1][0]= 0;
        s->sprite_delta[1][1]= a;
        s->sprite_shift[0]= 0;
        s->sprite_shift[1]= 0;
        s->real_sprite_warping_points=1;
    }
    else{
        int shift_y= 16 - s->sprite_shift[0];
        int shift_c= 16 - s->sprite_shift[1];
//printf("shifts %d %d\n", shift_y, shift_c);
        for(i=0; i<2; i++){
            s->sprite_offset[0][i]<<= shift_y;
            s->sprite_offset[1][i]<<= shift_c;
            s->sprite_delta[0][i]<<= shift_y;
            s->sprite_delta[1][i]<<= shift_y;
            s->sprite_shift[i]= 16;
        }
        s->real_sprite_warping_points= s->num_sprite_warping_points;
    }
#if 0
printf("vop:%d:%d %d:%d %d:%d, sprite:%d:%d %d:%d %d:%d, virtual: %d:%d %d:%d\n",
    vop_ref[0][0], vop_ref[0][1],
    vop_ref[1][0], vop_ref[1][1],
    vop_ref[2][0], vop_ref[2][1],
    sprite_ref[0][0], sprite_ref[0][1],
    sprite_ref[1][0], sprite_ref[1][1],
    sprite_ref[2][0], sprite_ref[2][1],
    virtual_ref[0][0], virtual_ref[0][1],
    virtual_ref[1][0], virtual_ref[1][1]
    );

printf("offset: %d:%d , delta: %d %d %d %d, shift %d\n",
    s->sprite_offset[0][0], s->sprite_offset[0][1],
    s->sprite_delta[0][0], s->sprite_delta[0][1],
    s->sprite_delta[1][0], s->sprite_delta[1][1],
    s->sprite_shift[0]
    );
#endif
}

static int mpeg4_decode_gop_header(MpegEncContext * s, GetBitContext *gb){
    int hours, minutes, seconds;

    hours= get_bits(gb, 5);
    minutes= get_bits(gb, 6);
    skip_bits1(gb);
    seconds= get_bits(gb, 6);

    s->time_base= seconds + 60*(minutes + 60*hours);

    skip_bits1(gb);
    skip_bits1(gb);

    return 0;
}

static int decode_vol_header(MpegEncContext *s, GetBitContext *gb){
    int width, height, vo_ver_id;

    /* vol header */
    skip_bits(gb, 1); /* random access */
    s->vo_type= get_bits(gb, 8);
    if (get_bits1(gb) != 0) { /* is_ol_id */
        vo_ver_id = get_bits(gb, 4); /* vo_ver_id */
        skip_bits(gb, 3); /* vo_priority */
    } else {
        vo_ver_id = 1;
    }
//printf("vo type:%d\n",s->vo_type);
    s->aspect_ratio_info= get_bits(gb, 4);
    if(s->aspect_ratio_info == FF_ASPECT_EXTENDED){
        s->avctx->sample_aspect_ratio.num= get_bits(gb, 8); // par_width
        s->avctx->sample_aspect_ratio.den= get_bits(gb, 8); // par_height
    }else{
        s->avctx->sample_aspect_ratio= pixel_aspect[s->aspect_ratio_info];
    }

    if ((s->vol_control_parameters=get_bits1(gb))) { /* vol control parameter */
        int chroma_format= get_bits(gb, 2);
        if(chroma_format!=CHROMA_420){
            av_log(s->avctx, AV_LOG_ERROR, "illegal chroma format\n");
        }
        s->low_delay= get_bits1(gb);
        if(get_bits1(gb)){ /* vbv parameters */
            get_bits(gb, 15);   /* first_half_bitrate */
            skip_bits1(gb);     /* marker */
            get_bits(gb, 15);   /* latter_half_bitrate */
            skip_bits1(gb);     /* marker */
            get_bits(gb, 15);   /* first_half_vbv_buffer_size */
            skip_bits1(gb);     /* marker */
            get_bits(gb, 3);    /* latter_half_vbv_buffer_size */
            get_bits(gb, 11);   /* first_half_vbv_occupancy */
            skip_bits1(gb);     /* marker */
            get_bits(gb, 15);   /* latter_half_vbv_occupancy */
            skip_bits1(gb);     /* marker */
        }
    }else{
        // set low delay flag only once the smartest? low delay detection won't be overriden
        if(s->picture_number==0)
            s->low_delay=0;
    }

    s->shape = get_bits(gb, 2); /* vol shape */
    if(s->shape != RECT_SHAPE) av_log(s->avctx, AV_LOG_ERROR, "only rectangular vol supported\n");
    if(s->shape == GRAY_SHAPE && vo_ver_id != 1){
        av_log(s->avctx, AV_LOG_ERROR, "Gray shape not supported\n");
        skip_bits(gb, 4);  //video_object_layer_shape_extension
    }

    check_marker(gb, "before time_increment_resolution");

    s->avctx->time_base.den = get_bits(gb, 16);
    if(!s->avctx->time_base.den){
        av_log(s->avctx, AV_LOG_ERROR, "time_base.den==0\n");
        return -1;
    }

    s->time_increment_bits = av_log2(s->avctx->time_base.den - 1) + 1;
    if (s->time_increment_bits < 1)
        s->time_increment_bits = 1;

    check_marker(gb, "before fixed_vop_rate");

    if (get_bits1(gb) != 0) {   /* fixed_vop_rate  */
        s->avctx->time_base.num = get_bits(gb, s->time_increment_bits);
    }else
        s->avctx->time_base.num = 1;

    s->t_frame=0;

    if (s->shape != BIN_ONLY_SHAPE) {
        if (s->shape == RECT_SHAPE) {
            skip_bits1(gb);   /* marker */
            width = get_bits(gb, 13);
            skip_bits1(gb);   /* marker */
            height = get_bits(gb, 13);
            skip_bits1(gb);   /* marker */
            if(width && height && !(s->width && s->codec_tag == AV_RL32("MP4S"))){ /* they should be non zero but who knows ... */
                s->width = width;
                s->height = height;
//                printf("width/height: %d %d\n", width, height);
            }
        }

        s->progressive_sequence=
        s->progressive_frame= get_bits1(gb)^1;
        s->interlaced_dct=0;
        if(!get_bits1(gb) && (s->avctx->debug & FF_DEBUG_PICT_INFO))
            av_log(s->avctx, AV_LOG_INFO, "MPEG4 OBMC not supported (very likely buggy encoder)\n");   /* OBMC Disable */
        if (vo_ver_id == 1) {
            s->vol_sprite_usage = get_bits1(gb); /* vol_sprite_usage */
        } else {
            s->vol_sprite_usage = get_bits(gb, 2); /* vol_sprite_usage */
        }
        if(s->vol_sprite_usage==STATIC_SPRITE) av_log(s->avctx, AV_LOG_ERROR, "Static Sprites not supported\n");
        if(s->vol_sprite_usage==STATIC_SPRITE || s->vol_sprite_usage==GMC_SPRITE){
            if(s->vol_sprite_usage==STATIC_SPRITE){
                s->sprite_width = get_bits(gb, 13);
                skip_bits1(gb); /* marker */
                s->sprite_height= get_bits(gb, 13);
                skip_bits1(gb); /* marker */
                s->sprite_left  = get_bits(gb, 13);
                skip_bits1(gb); /* marker */
                s->sprite_top   = get_bits(gb, 13);
                skip_bits1(gb); /* marker */
            }
            s->num_sprite_warping_points= get_bits(gb, 6);
            if(s->num_sprite_warping_points > 3){
                av_log(s->avctx, AV_LOG_ERROR, "%d sprite_warping_points\n", s->num_sprite_warping_points);
                s->num_sprite_warping_points= 0;
                return -1;
            }
            s->sprite_warping_accuracy = get_bits(gb, 2);
            s->sprite_brightness_change= get_bits1(gb);
            if(s->vol_sprite_usage==STATIC_SPRITE)
                s->low_latency_sprite= get_bits1(gb);
        }
        // FIXME sadct disable bit if verid!=1 && shape not rect

        if (get_bits1(gb) == 1) {   /* not_8_bit */
            s->quant_precision = get_bits(gb, 4); /* quant_precision */
            if(get_bits(gb, 4)!=8) av_log(s->avctx, AV_LOG_ERROR, "N-bit not supported\n"); /* bits_per_pixel */
            if(s->quant_precision!=5) av_log(s->avctx, AV_LOG_ERROR, "quant precision %d\n", s->quant_precision);
        } else {
            s->quant_precision = 5;
        }

        // FIXME a bunch of grayscale shape things

        if((s->mpeg_quant=get_bits1(gb))){ /* vol_quant_type */
            int i, v;

            /* load default matrixes */
            for(i=0; i<64; i++){
                int j= s->dsp.idct_permutation[i];
                v= ff_mpeg4_default_intra_matrix[i];
                s->intra_matrix[j]= v;
                s->chroma_intra_matrix[j]= v;

                v= ff_mpeg4_default_non_intra_matrix[i];
                s->inter_matrix[j]= v;
                s->chroma_inter_matrix[j]= v;
            }

            /* load custom intra matrix */
            if(get_bits1(gb)){
                int last=0;
                for(i=0; i<64; i++){
                    int j;
                    v= get_bits(gb, 8);
                    if(v==0) break;

                    last= v;
                    j= s->dsp.idct_permutation[ ff_zigzag_direct[i] ];
                    s->intra_matrix[j]= v;
                    s->chroma_intra_matrix[j]= v;
                }

                /* replicate last value */
                for(; i<64; i++){
                    int j= s->dsp.idct_permutation[ ff_zigzag_direct[i] ];
                    s->intra_matrix[j]= last;
                    s->chroma_intra_matrix[j]= last;
                }
            }

            /* load custom non intra matrix */
            if(get_bits1(gb)){
                int last=0;
                for(i=0; i<64; i++){
                    int j;
                    v= get_bits(gb, 8);
                    if(v==0) break;

                    last= v;
                    j= s->dsp.idct_permutation[ ff_zigzag_direct[i] ];
                    s->inter_matrix[j]= v;
                    s->chroma_inter_matrix[j]= v;
                }

                /* replicate last value */
                for(; i<64; i++){
                    int j= s->dsp.idct_permutation[ ff_zigzag_direct[i] ];
                    s->inter_matrix[j]= last;
                    s->chroma_inter_matrix[j]= last;
                }
            }

            // FIXME a bunch of grayscale shape things
        }

        if(vo_ver_id != 1)
             s->quarter_sample= get_bits1(gb);
        else s->quarter_sample=0;

        if(!get_bits1(gb)){
            int pos= get_bits_count(gb);
            int estimation_method= get_bits(gb, 2);
            if(estimation_method<2){
                if(!get_bits1(gb)){
                    s->cplx_estimation_trash_i += 8*get_bits1(gb); //opaque
                    s->cplx_estimation_trash_i += 8*get_bits1(gb); //transparent
                    s->cplx_estimation_trash_i += 8*get_bits1(gb); //intra_cae
                    s->cplx_estimation_trash_i += 8*get_bits1(gb); //inter_cae
                    s->cplx_estimation_trash_i += 8*get_bits1(gb); //no_update
                    s->cplx_estimation_trash_i += 8*get_bits1(gb); //upampling
                }
                if(!get_bits1(gb)){
                    s->cplx_estimation_trash_i += 8*get_bits1(gb); //intra_blocks
                    s->cplx_estimation_trash_p += 8*get_bits1(gb); //inter_blocks
                    s->cplx_estimation_trash_p += 8*get_bits1(gb); //inter4v_blocks
                    s->cplx_estimation_trash_i += 8*get_bits1(gb); //not coded blocks
                }
                if(!check_marker(gb, "in complexity estimation part 1")){
                    skip_bits_long(gb, pos - get_bits_count(gb));
                    goto no_cplx_est;
                }
                if(!get_bits1(gb)){
                    s->cplx_estimation_trash_i += 8*get_bits1(gb); //dct_coeffs
                    s->cplx_estimation_trash_i += 8*get_bits1(gb); //dct_lines
                    s->cplx_estimation_trash_i += 8*get_bits1(gb); //vlc_syms
                    s->cplx_estimation_trash_i += 4*get_bits1(gb); //vlc_bits
                }
                if(!get_bits1(gb)){
                    s->cplx_estimation_trash_p += 8*get_bits1(gb); //apm
                    s->cplx_estimation_trash_p += 8*get_bits1(gb); //npm
                    s->cplx_estimation_trash_b += 8*get_bits1(gb); //interpolate_mc_q
                    s->cplx_estimation_trash_p += 8*get_bits1(gb); //forwback_mc_q
                    s->cplx_estimation_trash_p += 8*get_bits1(gb); //halfpel2
                    s->cplx_estimation_trash_p += 8*get_bits1(gb); //halfpel4
                }
                if(!check_marker(gb, "in complexity estimation part 2")){
                    skip_bits_long(gb, pos - get_bits_count(gb));
                    goto no_cplx_est;
                }
                if(estimation_method==1){
                    s->cplx_estimation_trash_i += 8*get_bits1(gb); //sadct
                    s->cplx_estimation_trash_p += 8*get_bits1(gb); //qpel
                }
            }else
                av_log(s->avctx, AV_LOG_ERROR, "Invalid Complexity estimation method %d\n", estimation_method);
        }else{
no_cplx_est:
            s->cplx_estimation_trash_i=
            s->cplx_estimation_trash_p=
            s->cplx_estimation_trash_b= 0;
        }

        s->resync_marker= !get_bits1(gb); /* resync_marker_disabled */

        s->data_partitioning= get_bits1(gb);
        if(s->data_partitioning){
            s->rvlc= get_bits1(gb);
        }

        if(vo_ver_id != 1) {
            s->new_pred= get_bits1(gb);
            if(s->new_pred){
                av_log(s->avctx, AV_LOG_ERROR, "new pred not supported\n");
                skip_bits(gb, 2); /* requested upstream message type */
                skip_bits1(gb); /* newpred segment type */
            }
            s->reduced_res_vop= get_bits1(gb);
            if(s->reduced_res_vop) av_log(s->avctx, AV_LOG_ERROR, "reduced resolution VOP not supported\n");
        }
        else{
            s->new_pred=0;
            s->reduced_res_vop= 0;
        }

        s->scalability= get_bits1(gb);

        if (s->scalability) {
            GetBitContext bak= *gb;
            int ref_layer_id;
            int ref_layer_sampling_dir;
            int h_sampling_factor_n;
            int h_sampling_factor_m;
            int v_sampling_factor_n;
            int v_sampling_factor_m;

            s->hierachy_type= get_bits1(gb);
            ref_layer_id= get_bits(gb, 4);
            ref_layer_sampling_dir= get_bits1(gb);
            h_sampling_factor_n= get_bits(gb, 5);
            h_sampling_factor_m= get_bits(gb, 5);
            v_sampling_factor_n= get_bits(gb, 5);
            v_sampling_factor_m= get_bits(gb, 5);
            s->enhancement_type= get_bits1(gb);

            if(   h_sampling_factor_n==0 || h_sampling_factor_m==0
               || v_sampling_factor_n==0 || v_sampling_factor_m==0){

//                fprintf(stderr, "illegal scalability header (VERY broken encoder), trying to workaround\n");
                s->scalability=0;

                *gb= bak;
            }else
                av_log(s->avctx, AV_LOG_ERROR, "scalability not supported\n");

            // bin shape stuff FIXME
        }
    }
    return 0;
}

/**
 * decodes the user data stuff in the header.
 * Also initializes divx/xvid/lavc_version/build.
 */
static int decode_user_data(MpegEncContext *s, GetBitContext *gb){
    char buf[256];
    int i;
    int e;
    int ver = 0, build = 0, ver2 = 0, ver3 = 0;
    char last;

    for(i=0; i<255 && get_bits_count(gb) < gb->size_in_bits; i++){
        if(show_bits(gb, 23) == 0) break;
        buf[i]= get_bits(gb, 8);
    }
    buf[i]=0;

    /* divx detection */
    e=sscanf(buf, "DivX%dBuild%d%c", &ver, &build, &last);
    if(e<2)
        e=sscanf(buf, "DivX%db%d%c", &ver, &build, &last);
    if(e>=2){
        s->divx_version= ver;
        s->divx_build= build;
        s->divx_packed= e==3 && last=='p';
        if(s->divx_packed && !s->showed_packed_warning) {
            av_log(s->avctx, AV_LOG_WARNING, "Invalid and inefficient vfw-avi packed B frames detected\n");
            s->showed_packed_warning=1;
        }
    }

    /* ffmpeg detection */
    e=sscanf(buf, "FFmpe%*[^b]b%d", &build)+3;
    if(e!=4)
        e=sscanf(buf, "FFmpeg v%d.%d.%d / libavcodec build: %d", &ver, &ver2, &ver3, &build);
    if(e!=4){
        e=sscanf(buf, "Lavc%d.%d.%d", &ver, &ver2, &ver3)+1;
        if (e>1)
            build= (ver<<16) + (ver2<<8) + ver3;
    }
    if(e!=4){
        if(strcmp(buf, "ffmpeg")==0){
            s->lavc_build= 4600;
        }
    }
    if(e==4){
        s->lavc_build= build;
    }

    /* Xvid detection */
    e=sscanf(buf, "XviD%d", &build);
    if(e==1){
        s->xvid_build= build;
    }

//printf("User Data: %s\n", buf);
    return 0;
}

static int decode_vop_header(MpegEncContext *s, GetBitContext *gb){
    int time_incr, time_increment;

    s->pict_type = get_bits(gb, 2) + FF_I_TYPE;        /* pict type: I = 0 , P = 1 */
    if(s->pict_type==FF_B_TYPE && s->low_delay && s->vol_control_parameters==0 && !(s->flags & CODEC_FLAG_LOW_DELAY)){
        av_log(s->avctx, AV_LOG_ERROR, "low_delay flag incorrectly, clearing it\n");
        s->low_delay=0;
    }

    s->partitioned_frame= s->data_partitioning && s->pict_type!=FF_B_TYPE;
    if(s->partitioned_frame)
        s->decode_mb= mpeg4_decode_partitioned_mb;
    else
        s->decode_mb= ff_mpeg4_decode_mb;

    time_incr=0;
    while (get_bits1(gb) != 0)
        time_incr++;

    check_marker(gb, "before time_increment");

    if(s->time_increment_bits==0 || !(show_bits(gb, s->time_increment_bits+1)&1)){
        av_log(s->avctx, AV_LOG_ERROR, "hmm, seems the headers are not complete, trying to guess time_increment_bits\n");

        for(s->time_increment_bits=1 ;s->time_increment_bits<16; s->time_increment_bits++){
            if(show_bits(gb, s->time_increment_bits+1)&1) break;
        }

        av_log(s->avctx, AV_LOG_ERROR, "my guess is %d bits ;)\n",s->time_increment_bits);
    }

    if(IS_3IV1) time_increment= get_bits1(gb); //FIXME investigate further
    else time_increment= get_bits(gb, s->time_increment_bits);

//    printf("%d %X\n", s->time_increment_bits, time_increment);
//av_log(s->avctx, AV_LOG_DEBUG, " type:%d modulo_time_base:%d increment:%d t_frame %d\n", s->pict_type, time_incr, time_increment, s->t_frame);
    if(s->pict_type!=FF_B_TYPE){
        s->last_time_base= s->time_base;
        s->time_base+= time_incr;
        s->time= s->time_base*s->avctx->time_base.den + time_increment;
        if(s->workaround_bugs&FF_BUG_UMP4){
            if(s->time < s->last_non_b_time){
//                fprintf(stderr, "header is not mpeg4 compatible, broken encoder, trying to workaround\n");
                s->time_base++;
                s->time+= s->avctx->time_base.den;
            }
        }
        s->pp_time= s->time - s->last_non_b_time;
        s->last_non_b_time= s->time;
    }else{
        s->time= (s->last_time_base + time_incr)*s->avctx->time_base.den + time_increment;
        s->pb_time= s->pp_time - (s->last_non_b_time - s->time);
        if(s->pp_time <=s->pb_time || s->pp_time <= s->pp_time - s->pb_time || s->pp_time<=0){
//            printf("messed up order, maybe after seeking? skipping current b frame\n");
            return FRAME_SKIPPED;
        }
        ff_mpeg4_init_direct_mv(s);

        if(s->t_frame==0) s->t_frame= s->pb_time;
        if(s->t_frame==0) s->t_frame=1; // 1/0 protection
        s->pp_field_time= (  ROUNDED_DIV(s->last_non_b_time, s->t_frame)
                           - ROUNDED_DIV(s->last_non_b_time - s->pp_time, s->t_frame))*2;
        s->pb_field_time= (  ROUNDED_DIV(s->time, s->t_frame)
                           - ROUNDED_DIV(s->last_non_b_time - s->pp_time, s->t_frame))*2;
        if(!s->progressive_sequence){
            if(s->pp_field_time <= s->pb_field_time || s->pb_field_time <= 1)
                return FRAME_SKIPPED;
        }
    }
//av_log(s->avctx, AV_LOG_DEBUG, "last nonb %"PRId64" last_base %d time %"PRId64" pp %d pb %d t %d ppf %d pbf %d\n", s->last_non_b_time, s->last_time_base, s->time, s->pp_time, s->pb_time, s->t_frame, s->pp_field_time, s->pb_field_time);

    if(s->avctx->time_base.num)
        s->current_picture_ptr->pts= (s->time + s->avctx->time_base.num/2) / s->avctx->time_base.num;
    else
        s->current_picture_ptr->pts= AV_NOPTS_VALUE;
    if(s->avctx->debug&FF_DEBUG_PTS)
        av_log(s->avctx, AV_LOG_DEBUG, "MPEG4 PTS: %"PRId64"\n", s->current_picture_ptr->pts);

    check_marker(gb, "before vop_coded");

    /* vop coded */
    if (get_bits1(gb) != 1){
        if(s->avctx->debug&FF_DEBUG_PICT_INFO)
            av_log(s->avctx, AV_LOG_ERROR, "vop not coded\n");
        return FRAME_SKIPPED;
    }
//printf("time %d %d %d || %"PRId64" %"PRId64" %"PRId64"\n", s->time_increment_bits, s->avctx->time_base.den, s->time_base,
//s->time, s->last_non_b_time, s->last_non_b_time - s->pp_time);
    if (s->shape != BIN_ONLY_SHAPE && ( s->pict_type == FF_P_TYPE
                          || (s->pict_type == FF_S_TYPE && s->vol_sprite_usage==GMC_SPRITE))) {
        /* rounding type for motion estimation */
        s->no_rounding = get_bits1(gb);
    } else {
        s->no_rounding = 0;
    }
//FIXME reduced res stuff

     if (s->shape != RECT_SHAPE) {
         if (s->vol_sprite_usage != 1 || s->pict_type != FF_I_TYPE) {
             int width, height, hor_spat_ref, ver_spat_ref;

             width = get_bits(gb, 13);
             skip_bits1(gb);   /* marker */
             height = get_bits(gb, 13);
             skip_bits1(gb);   /* marker */
             hor_spat_ref = get_bits(gb, 13); /* hor_spat_ref */
             skip_bits1(gb);   /* marker */
             ver_spat_ref = get_bits(gb, 13); /* ver_spat_ref */
         }
         skip_bits1(gb); /* change_CR_disable */

         if (get_bits1(gb) != 0) {
             skip_bits(gb, 8); /* constant_alpha_value */
         }
     }
//FIXME complexity estimation stuff

     if (s->shape != BIN_ONLY_SHAPE) {
         skip_bits_long(gb, s->cplx_estimation_trash_i);
         if(s->pict_type != FF_I_TYPE)
            skip_bits_long(gb, s->cplx_estimation_trash_p);
         if(s->pict_type == FF_B_TYPE)
            skip_bits_long(gb, s->cplx_estimation_trash_b);

         s->intra_dc_threshold= mpeg4_dc_threshold[ get_bits(gb, 3) ];
         if(!s->progressive_sequence){
             s->top_field_first= get_bits1(gb);
             s->alternate_scan= get_bits1(gb);
         }else
             s->alternate_scan= 0;
     }

     if(s->alternate_scan){
         ff_init_scantable(s->dsp.idct_permutation, &s->inter_scantable  , ff_alternate_vertical_scan);
         ff_init_scantable(s->dsp.idct_permutation, &s->intra_scantable  , ff_alternate_vertical_scan);
         ff_init_scantable(s->dsp.idct_permutation, &s->intra_h_scantable, ff_alternate_vertical_scan);
         ff_init_scantable(s->dsp.idct_permutation, &s->intra_v_scantable, ff_alternate_vertical_scan);
     } else{
         ff_init_scantable(s->dsp.idct_permutation, &s->inter_scantable  , ff_zigzag_direct);
         ff_init_scantable(s->dsp.idct_permutation, &s->intra_scantable  , ff_zigzag_direct);
         ff_init_scantable(s->dsp.idct_permutation, &s->intra_h_scantable, ff_alternate_horizontal_scan);
         ff_init_scantable(s->dsp.idct_permutation, &s->intra_v_scantable, ff_alternate_vertical_scan);
     }

     if(s->pict_type == FF_S_TYPE && (s->vol_sprite_usage==STATIC_SPRITE || s->vol_sprite_usage==GMC_SPRITE)){
         mpeg4_decode_sprite_trajectory(s, gb);
         if(s->sprite_brightness_change) av_log(s->avctx, AV_LOG_ERROR, "sprite_brightness_change not supported\n");
         if(s->vol_sprite_usage==STATIC_SPRITE) av_log(s->avctx, AV_LOG_ERROR, "static sprite not supported\n");
     }

     if (s->shape != BIN_ONLY_SHAPE) {
         s->chroma_qscale= s->qscale = get_bits(gb, s->quant_precision);
         if(s->qscale==0){
             av_log(s->avctx, AV_LOG_ERROR, "Error, header damaged or not MPEG4 header (qscale=0)\n");
             return -1; // makes no sense to continue, as there is nothing left from the image then
         }

         if (s->pict_type != FF_I_TYPE) {
             s->f_code = get_bits(gb, 3);       /* fcode_for */
             if(s->f_code==0){
                 av_log(s->avctx, AV_LOG_ERROR, "Error, header damaged or not MPEG4 header (f_code=0)\n");
                 return -1; // makes no sense to continue, as the MV decoding will break very quickly
             }
         }else
             s->f_code=1;

         if (s->pict_type == FF_B_TYPE) {
             s->b_code = get_bits(gb, 3);
         }else
             s->b_code=1;

         if(s->avctx->debug&FF_DEBUG_PICT_INFO){
             av_log(s->avctx, AV_LOG_DEBUG, "qp:%d fc:%d,%d %s size:%d pro:%d alt:%d top:%d %spel part:%d resync:%d w:%d a:%d rnd:%d vot:%d%s dc:%d ce:%d/%d/%d\n",
                 s->qscale, s->f_code, s->b_code,
                 s->pict_type == FF_I_TYPE ? "I" : (s->pict_type == FF_P_TYPE ? "P" : (s->pict_type == FF_B_TYPE ? "B" : "S")),
                 gb->size_in_bits,s->progressive_sequence, s->alternate_scan, s->top_field_first,
                 s->quarter_sample ? "q" : "h", s->data_partitioning, s->resync_marker, s->num_sprite_warping_points,
                 s->sprite_warping_accuracy, 1-s->no_rounding, s->vo_type, s->vol_control_parameters ? " VOLC" : " ", s->intra_dc_threshold, s->cplx_estimation_trash_i, s->cplx_estimation_trash_p, s->cplx_estimation_trash_b);
         }

         if(!s->scalability){
             if (s->shape!=RECT_SHAPE && s->pict_type!=FF_I_TYPE) {
                 skip_bits1(gb); // vop shape coding type
             }
         }else{
             if(s->enhancement_type){
                 int load_backward_shape= get_bits1(gb);
                 if(load_backward_shape){
                     av_log(s->avctx, AV_LOG_ERROR, "load backward shape isn't supported\n");
                 }
             }
             skip_bits(gb, 2); //ref_select_code
         }
     }
     /* detect buggy encoders which don't set the low_delay flag (divx4/xvid/opendivx)*/
     // note we cannot detect divx5 without b-frames easily (although it's buggy too)
     if(s->vo_type==0 && s->vol_control_parameters==0 && s->divx_version==0 && s->picture_number==0){
         av_log(s->avctx, AV_LOG_ERROR, "looks like this file was encoded with (divx4/(old)xvid/opendivx) -> forcing low_delay flag\n");
         s->low_delay=1;
     }

     s->picture_number++; // better than pic number==0 always ;)

     s->y_dc_scale_table= ff_mpeg4_y_dc_scale_table; //FIXME add short header support
     s->c_dc_scale_table= ff_mpeg4_c_dc_scale_table;

     if(s->workaround_bugs&FF_BUG_EDGE){
         s->h_edge_pos= s->width;
         s->v_edge_pos= s->height;
     }
     return 0;
}

/**
 * decode mpeg4 headers
 * @return <0 if no VOP found (or a damaged one)
 *         FRAME_SKIPPED if a not coded VOP is found
 *         0 if a VOP is found
 */
int ff_mpeg4_decode_picture_header(MpegEncContext * s, GetBitContext *gb)
{
    int startcode, v;

    /* search next start code */
    align_get_bits(gb);

    if(s->codec_tag == AV_RL32("WV1F") && show_bits(gb, 24) == 0x575630){
        skip_bits(gb, 24);
        if(get_bits(gb, 8) == 0xF0)
            goto end;
    }

    startcode = 0xff;
    for(;;) {
        if(get_bits_count(gb) >= gb->size_in_bits){
            if(gb->size_in_bits==8 && (s->divx_version || s->xvid_build)){
                av_log(s->avctx, AV_LOG_ERROR, "frame skip %d\n", gb->size_in_bits);
                return FRAME_SKIPPED; //divx bug
            }else
                return -1; //end of stream
        }

        /* use the bits after the test */
        v = get_bits(gb, 8);
        startcode = ((startcode << 8) | v) & 0xffffffff;

        if((startcode&0xFFFFFF00) != 0x100)
            continue; //no startcode

        if(s->avctx->debug&FF_DEBUG_STARTCODE){
            av_log(s->avctx, AV_LOG_DEBUG, "startcode: %3X ", startcode);
            if     (startcode<=0x11F) av_log(s->avctx, AV_LOG_DEBUG, "Video Object Start");
            else if(startcode<=0x12F) av_log(s->avctx, AV_LOG_DEBUG, "Video Object Layer Start");
            else if(startcode<=0x13F) av_log(s->avctx, AV_LOG_DEBUG, "Reserved");
            else if(startcode<=0x15F) av_log(s->avctx, AV_LOG_DEBUG, "FGS bp start");
            else if(startcode<=0x1AF) av_log(s->avctx, AV_LOG_DEBUG, "Reserved");
            else if(startcode==0x1B0) av_log(s->avctx, AV_LOG_DEBUG, "Visual Object Seq Start");
            else if(startcode==0x1B1) av_log(s->avctx, AV_LOG_DEBUG, "Visual Object Seq End");
            else if(startcode==0x1B2) av_log(s->avctx, AV_LOG_DEBUG, "User Data");
            else if(startcode==0x1B3) av_log(s->avctx, AV_LOG_DEBUG, "Group of VOP start");
            else if(startcode==0x1B4) av_log(s->avctx, AV_LOG_DEBUG, "Video Session Error");
            else if(startcode==0x1B5) av_log(s->avctx, AV_LOG_DEBUG, "Visual Object Start");
            else if(startcode==0x1B6) av_log(s->avctx, AV_LOG_DEBUG, "Video Object Plane start");
            else if(startcode==0x1B7) av_log(s->avctx, AV_LOG_DEBUG, "slice start");
            else if(startcode==0x1B8) av_log(s->avctx, AV_LOG_DEBUG, "extension start");
            else if(startcode==0x1B9) av_log(s->avctx, AV_LOG_DEBUG, "fgs start");
            else if(startcode==0x1BA) av_log(s->avctx, AV_LOG_DEBUG, "FBA Object start");
            else if(startcode==0x1BB) av_log(s->avctx, AV_LOG_DEBUG, "FBA Object Plane start");
            else if(startcode==0x1BC) av_log(s->avctx, AV_LOG_DEBUG, "Mesh Object start");
            else if(startcode==0x1BD) av_log(s->avctx, AV_LOG_DEBUG, "Mesh Object Plane start");
            else if(startcode==0x1BE) av_log(s->avctx, AV_LOG_DEBUG, "Still Texture Object start");
            else if(startcode==0x1BF) av_log(s->avctx, AV_LOG_DEBUG, "Texture Spatial Layer start");
            else if(startcode==0x1C0) av_log(s->avctx, AV_LOG_DEBUG, "Texture SNR Layer start");
            else if(startcode==0x1C1) av_log(s->avctx, AV_LOG_DEBUG, "Texture Tile start");
            else if(startcode==0x1C2) av_log(s->avctx, AV_LOG_DEBUG, "Texture Shape Layer start");
            else if(startcode==0x1C3) av_log(s->avctx, AV_LOG_DEBUG, "stuffing start");
            else if(startcode<=0x1C5) av_log(s->avctx, AV_LOG_DEBUG, "reserved");
            else if(startcode<=0x1FF) av_log(s->avctx, AV_LOG_DEBUG, "System start");
            av_log(s->avctx, AV_LOG_DEBUG, " at %d\n", get_bits_count(gb));
        }

        if(startcode >= 0x120 && startcode <= 0x12F){
            if(decode_vol_header(s, gb) < 0)
                return -1;
        }
        else if(startcode == USER_DATA_STARTCODE){
            decode_user_data(s, gb);
        }
        else if(startcode == GOP_STARTCODE){
            mpeg4_decode_gop_header(s, gb);
        }
        else if(startcode == VOP_STARTCODE){
            break;
        }

        align_get_bits(gb);
        startcode = 0xff;
    }
end:
    if(s->flags& CODEC_FLAG_LOW_DELAY)
        s->low_delay=1;
    s->avctx->has_b_frames= !s->low_delay;
    return decode_vop_header(s, gb);
}

/* don't understand why they choose a different header ! */
int intel_h263_decode_picture_header(MpegEncContext *s)
{
    int format;

    /* picture header */
    if (get_bits_long(&s->gb, 22) != 0x20) {
        av_log(s->avctx, AV_LOG_ERROR, "Bad picture start code\n");
        return -1;
    }
    s->picture_number = get_bits(&s->gb, 8); /* picture timestamp */

    if (get_bits1(&s->gb) != 1) {
        av_log(s->avctx, AV_LOG_ERROR, "Bad marker\n");
        return -1;      /* marker */
    }
    if (get_bits1(&s->gb) != 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Bad H263 id\n");
        return -1;      /* h263 id */
    }
    skip_bits1(&s->gb);         /* split screen off */
    skip_bits1(&s->gb);         /* camera  off */
    skip_bits1(&s->gb);         /* freeze picture release off */

    format = get_bits(&s->gb, 3);
    if (format != 7) {
        av_log(s->avctx, AV_LOG_ERROR, "Intel H263 free format not supported\n");
        return -1;
    }
    s->h263_plus = 0;

    s->pict_type = FF_I_TYPE + get_bits1(&s->gb);

    s->unrestricted_mv = get_bits1(&s->gb);
    s->h263_long_vectors = s->unrestricted_mv;

    if (get_bits1(&s->gb) != 0) {
        av_log(s->avctx, AV_LOG_ERROR, "SAC not supported\n");
        return -1;      /* SAC: off */
    }
    s->obmc= get_bits1(&s->gb);
    s->pb_frame = get_bits1(&s->gb);

    if(format == 7){
        format = get_bits(&s->gb, 3);
        if(format == 0 || format == 7){
            av_log(s->avctx, AV_LOG_ERROR, "Wrong Intel H263 format\n");
            return -1;
        }
        if(get_bits(&s->gb, 2))
            av_log(s->avctx, AV_LOG_ERROR, "Bad value for reserved field\n");
        s->loop_filter = get_bits1(&s->gb);
        if(get_bits1(&s->gb))
            av_log(s->avctx, AV_LOG_ERROR, "Bad value for reserved field\n");
        if(get_bits1(&s->gb))
            s->pb_frame = 2;
        if(get_bits(&s->gb, 5))
            av_log(s->avctx, AV_LOG_ERROR, "Bad value for reserved field\n");
        if(get_bits(&s->gb, 5) != 1)
            av_log(s->avctx, AV_LOG_ERROR, "Invalid marker\n");
    }
    if(format == 6){
        int ar = get_bits(&s->gb, 4);
        skip_bits(&s->gb, 9); // display width
        skip_bits1(&s->gb);
        skip_bits(&s->gb, 9); // display height
        if(ar == 15){
            skip_bits(&s->gb, 8); // aspect ratio - width
            skip_bits(&s->gb, 8); // aspect ratio - height
        }
    }

    s->chroma_qscale= s->qscale = get_bits(&s->gb, 5);
    skip_bits1(&s->gb); /* Continuous Presence Multipoint mode: off */

    if(s->pb_frame){
        skip_bits(&s->gb, 3); //temporal reference for B-frame
        skip_bits(&s->gb, 2); //dbquant
    }

    /* PEI */
    while (get_bits1(&s->gb) != 0) {
        skip_bits(&s->gb, 8);
    }
    s->f_code = 1;

    s->y_dc_scale_table=
    s->c_dc_scale_table= ff_mpeg1_dc_scale_table;

    if(s->avctx->debug&FF_DEBUG_PICT_INFO)
        show_pict_info(s);

    return 0;
}

int flv_h263_decode_picture_header(MpegEncContext *s)
{
    int format, width, height;

    /* picture header */
    if (get_bits_long(&s->gb, 17) != 1) {
        av_log(s->avctx, AV_LOG_ERROR, "Bad picture start code\n");
        return -1;
    }
    format = get_bits(&s->gb, 5);
    if (format != 0 && format != 1) {
        av_log(s->avctx, AV_LOG_ERROR, "Bad picture format\n");
        return -1;
    }
    s->h263_flv = format+1;
    s->picture_number = get_bits(&s->gb, 8); /* picture timestamp */
    format = get_bits(&s->gb, 3);
    switch (format) {
    case 0:
        width = get_bits(&s->gb, 8);
        height = get_bits(&s->gb, 8);
        break;
    case 1:
        width = get_bits(&s->gb, 16);
        height = get_bits(&s->gb, 16);
        break;
    case 2:
        width = 352;
        height = 288;
        break;
    case 3:
        width = 176;
        height = 144;
        break;
    case 4:
        width = 128;
        height = 96;
        break;
    case 5:
        width = 320;
        height = 240;
        break;
    case 6:
        width = 160;
        height = 120;
        break;
    default:
        width = height = 0;
        break;
    }
    if(avcodec_check_dimensions(s->avctx, width, height))
        return -1;
    s->width = width;
    s->height = height;

    s->pict_type = FF_I_TYPE + get_bits(&s->gb, 2);
    s->dropable= s->pict_type > FF_P_TYPE;
    if (s->dropable)
        s->pict_type = FF_P_TYPE;

    skip_bits1(&s->gb); /* deblocking flag */
    s->chroma_qscale= s->qscale = get_bits(&s->gb, 5);

    s->h263_plus = 0;

    s->unrestricted_mv = 1;
    s->h263_long_vectors = 0;

    /* PEI */
    while (get_bits1(&s->gb) != 0) {
        skip_bits(&s->gb, 8);
    }
    s->f_code = 1;

    if(s->avctx->debug & FF_DEBUG_PICT_INFO){
        av_log(s->avctx, AV_LOG_DEBUG, "%c esc_type:%d, qp:%d num:%d\n",
               s->dropable ? 'D' : av_get_pict_type_char(s->pict_type), s->h263_flv-1, s->qscale, s->picture_number);
    }

    s->y_dc_scale_table=
    s->c_dc_scale_table= ff_mpeg1_dc_scale_table;

    return 0;
}
