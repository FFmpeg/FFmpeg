/*
 * H263/MPEG4 backend for ffmpeg encoder and decoder
 * Copyright (c) 2000,2001 Fabrice Bellard.
 * H263+ support.
 * Copyright (c) 2001 Juan J. Sierralta P.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * ac prediction encoding, b-frame support, error resilience, optimizations,
 * qpel decoding, gmc decoding, interlaced decoding, 
 * by Michael Niedermayer <michaelni@gmx.at>
 */

/**
 * @file h263.c
 * @brief h263/mpeg4 codec
 *
 */
 
//#define DEBUG
#include "common.h"
#include "dsputil.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "h263data.h"
#include "mpeg4data.h"

//#undef NDEBUG
//#include <assert.h>

#if 1
#define PRINT_MB_TYPE(a) {}
#else
#define PRINT_MB_TYPE(a) printf(a)
#endif

#define INTRA_MCBPC_VLC_BITS 6
#define INTER_MCBPC_VLC_BITS 6
#define CBPY_VLC_BITS 6
#define MV_VLC_BITS 9
#define DC_VLC_BITS 9
#define SPRITE_TRAJ_VLC_BITS 6
#define MB_TYPE_B_VLC_BITS 4
#define TEX_VLC_BITS 9

#ifdef CONFIG_ENCODERS
static void h263_encode_block(MpegEncContext * s, DCTELEM * block,
			      int n);
static void h263_encode_motion(MpegEncContext * s, int val, int fcode);
static void h263p_encode_umotion(MpegEncContext * s, int val);
static inline void mpeg4_encode_block(MpegEncContext * s, DCTELEM * block,
			       int n, int dc, UINT8 *scan_table, 
                               PutBitContext *dc_pb, PutBitContext *ac_pb);
#endif

static int h263_decode_motion(MpegEncContext * s, int pred, int fcode);
static int h263p_decode_umotion(MpegEncContext * s, int pred);
static int h263_decode_block(MpegEncContext * s, DCTELEM * block,
                             int n, int coded);
static inline int mpeg4_decode_dc(MpegEncContext * s, int n, int *dir_ptr);
static inline int mpeg4_decode_block(MpegEncContext * s, DCTELEM * block,
                              int n, int coded, int intra);
static int h263_pred_dc(MpegEncContext * s, int n, UINT16 **dc_val_ptr);
static void mpeg4_inv_pred_ac(MpegEncContext * s, DCTELEM *block, int n,
                              int dir);
static void mpeg4_decode_sprite_trajectory(MpegEncContext * s);
static inline int ff_mpeg4_pred_dc(MpegEncContext * s, int n, UINT16 **dc_val_ptr, int *dir_ptr);

extern UINT32 inverse[256];

static UINT8 uni_DCtab_lum_len[512];
static UINT8 uni_DCtab_chrom_len[512];
static UINT16 uni_DCtab_lum_bits[512];
static UINT16 uni_DCtab_chrom_bits[512];

#ifdef CONFIG_ENCODERS
static UINT16 (*mv_penalty)[MAX_MV*2+1]= NULL;
static UINT8 fcode_tab[MAX_MV*2+1];
static UINT8 umv_fcode_tab[MAX_MV*2+1];

static uint32_t uni_mpeg4_intra_rl_bits[64*64*2*2];
static uint8_t  uni_mpeg4_intra_rl_len [64*64*2*2];
static uint32_t uni_mpeg4_inter_rl_bits[64*64*2*2];
static uint8_t  uni_mpeg4_inter_rl_len [64*64*2*2];
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

static void float_aspect_to_info(MpegEncContext * s, float aspect){
    int i;

    aspect*= s->height/(double)s->width;
//printf("%f\n", aspect);
    
    if(aspect==0) aspect= 1.0;

    ff_float2fraction(&s->aspected_width, &s->aspected_height, aspect, 255);

//printf("%d %d\n", s->aspected_width, s->aspected_height);
    for(i=1; i<6; i++){
        if(s->aspected_width == pixel_aspect[i][0] && s->aspected_height== pixel_aspect[i][1]){
            s->aspect_ratio_info=i;
            return;
        }
    }
    
    s->aspect_ratio_info= FF_ASPECT_EXTENDED;
}

void h263_encode_picture_header(MpegEncContext * s, int picture_number)
{
    int format;

    align_put_bits(&s->pb);

    /* Update the pointer to last GOB */
    s->ptr_lastgob = pbBufPtr(&s->pb);
    s->gob_number = 0;

    put_bits(&s->pb, 22, 0x20); /* PSC */
    put_bits(&s->pb, 8, (((INT64)s->picture_number * 30 * FRAME_RATE_BASE) / 
                         s->frame_rate) & 0xff);

    put_bits(&s->pb, 1, 1);	/* marker */
    put_bits(&s->pb, 1, 0);	/* h263 id */
    put_bits(&s->pb, 1, 0);	/* split screen off */
    put_bits(&s->pb, 1, 0);	/* camera  off */
    put_bits(&s->pb, 1, 0);	/* freeze picture release off */
    
    format = h263_get_picture_format(s->width, s->height);
    if (!s->h263_plus) {
        /* H.263v1 */
        put_bits(&s->pb, 3, format);
        put_bits(&s->pb, 1, (s->pict_type == P_TYPE));
        /* By now UMV IS DISABLED ON H.263v1, since the restrictions
        of H.263v1 UMV implies to check the predicted MV after
        calculation of the current MB to see if we're on the limits */
        put_bits(&s->pb, 1, 0);	/* unrestricted motion vector: off */
        put_bits(&s->pb, 1, 0);	/* SAC: off */
        put_bits(&s->pb, 1, 0);	/* advanced prediction mode: off */
        put_bits(&s->pb, 1, 0);	/* not PB frame */
        put_bits(&s->pb, 5, s->qscale);
        put_bits(&s->pb, 1, 0);	/* Continuous Presence Multipoint mode: off */
    } else {
        /* H.263v2 */
        /* H.263 Plus PTYPE */
        put_bits(&s->pb, 3, 7);
        put_bits(&s->pb,3,1); /* Update Full Extended PTYPE */
        if (format == 7)
            put_bits(&s->pb,3,6); /* Custom Source Format */
        else
            put_bits(&s->pb, 3, format);
            
        put_bits(&s->pb,1,0); /* Custom PCF: off */
        s->umvplus = (s->pict_type == P_TYPE) && s->unrestricted_mv;
        put_bits(&s->pb, 1, s->umvplus); /* Unrestricted Motion Vector */
        put_bits(&s->pb,1,0); /* SAC: off */
        put_bits(&s->pb,1,0); /* Advanced Prediction Mode: off */
        put_bits(&s->pb,1,s->h263_aic); /* Advanced Intra Coding */
        put_bits(&s->pb,1,0); /* Deblocking Filter: off */
        put_bits(&s->pb,1,0); /* Slice Structured: off */
        put_bits(&s->pb,1,0); /* Reference Picture Selection: off */
        put_bits(&s->pb,1,0); /* Independent Segment Decoding: off */
        put_bits(&s->pb,1,0); /* Alternative Inter VLC: off */
        put_bits(&s->pb,1,0); /* Modified Quantization: off */
        put_bits(&s->pb,1,1); /* "1" to prevent start code emulation */
        put_bits(&s->pb,3,0); /* Reserved */
		
        put_bits(&s->pb, 3, s->pict_type == P_TYPE);
		
        put_bits(&s->pb,1,0); /* Reference Picture Resampling: off */
        put_bits(&s->pb,1,0); /* Reduced-Resolution Update: off */
        put_bits(&s->pb,1,s->no_rounding); /* Rounding Type */
        put_bits(&s->pb,2,0); /* Reserved */
        put_bits(&s->pb,1,1); /* "1" to prevent start code emulation */
		
        /* This should be here if PLUSPTYPE */
        put_bits(&s->pb, 1, 0);	/* Continuous Presence Multipoint mode: off */
		
		if (format == 7) {
            /* Custom Picture Format (CPFMT) */
            float_aspect_to_info(s, s->avctx->aspect_ratio);

            put_bits(&s->pb,4,s->aspect_ratio_info);
            put_bits(&s->pb,9,(s->width >> 2) - 1);
            put_bits(&s->pb,1,1); /* "1" to prevent start code emulation */
            put_bits(&s->pb,9,(s->height >> 2));
	    if (s->aspect_ratio_info == FF_ASPECT_EXTENDED)
	    {
		put_bits(&s->pb, 8, s->aspected_width);
		put_bits(&s->pb, 8, s->aspected_height);
	    }
        }
        
        /* Unlimited Unrestricted Motion Vectors Indicator (UUI) */
        if (s->umvplus)
            put_bits(&s->pb,1,1); /* Limited according tables of Annex D */
        put_bits(&s->pb, 5, s->qscale);
    }

    put_bits(&s->pb, 1, 0);	/* no PEI */

    if(s->h263_aic){
         s->y_dc_scale_table= 
         s->c_dc_scale_table= h263_aic_dc_scale_table;
    }else{
        s->y_dc_scale_table=
        s->c_dc_scale_table= ff_mpeg1_dc_scale_table;
    }
}

/**
 * Encodes a group of blocks header.
 */
int h263_encode_gob_header(MpegEncContext * s, int mb_line)
{
           align_put_bits(&s->pb);
           flush_put_bits(&s->pb);
           /* Call the RTP callback to send the last GOB */
           if (s->rtp_callback) {
               int pdif = pbBufPtr(&s->pb) - s->ptr_lastgob;
               s->rtp_callback(s->ptr_lastgob, pdif, s->gob_number);
           }
           put_bits(&s->pb, 17, 1); /* GBSC */
           s->gob_number = mb_line / s->gob_index;
           put_bits(&s->pb, 5, s->gob_number); /* GN */
           put_bits(&s->pb, 2, s->pict_type == I_TYPE); /* GFID */
           put_bits(&s->pb, 5, s->qscale); /* GQUANT */
           //fprintf(stderr,"\nGOB: %2d size: %d", s->gob_number - 1, pdif);
    return 0;
}

static inline int decide_ac_pred(MpegEncContext * s, DCTELEM block[6][64], int dir[6])
{
    int score0=0, score1=0;
    int i, n;
    int8_t * const qscale_table= s->current_picture.qscale_table;

    for(n=0; n<6; n++){
        INT16 *ac_val, *ac_val1;

        ac_val = s->ac_val[0][0] + s->block_index[n] * 16;
        ac_val1= ac_val;
        if(dir[n]){
            const int xy= s->mb_x + s->mb_y*s->mb_width - s->mb_width;
            /* top prediction */
            ac_val-= s->block_wrap[n]*16;
            if(s->mb_y==0 || s->qscale == qscale_table[xy] || n==2 || n==3){
                /* same qscale */
                for(i=1; i<8; i++){
                    const int level= block[n][s->idct_permutation[i   ]];
                    score0+= ABS(level);
                    score1+= ABS(level - ac_val[i+8]);
                    ac_val1[i  ]=    block[n][s->idct_permutation[i<<3]];
                    ac_val1[i+8]= level;
                }
            }else{
                /* different qscale, we must rescale */
                for(i=1; i<8; i++){
                    const int level= block[n][s->idct_permutation[i   ]];
                    score0+= ABS(level);
                    score1+= ABS(level - ROUNDED_DIV(ac_val[i + 8]*qscale_table[xy], s->qscale));
                    ac_val1[i  ]=    block[n][s->idct_permutation[i<<3]];
                    ac_val1[i+8]= level;
                }
            }
        }else{
            const int xy= s->mb_x-1 + s->mb_y*s->mb_width;
            /* left prediction */
            ac_val-= 16;
            if(s->mb_x==0 || s->qscale == qscale_table[xy] || n==1 || n==3){
                /* same qscale */
                for(i=1; i<8; i++){
                    const int level= block[n][s->idct_permutation[i<<3]];
                    score0+= ABS(level);
                    score1+= ABS(level - ac_val[i]);
                    ac_val1[i  ]= level;
                    ac_val1[i+8]=    block[n][s->idct_permutation[i   ]];
                }
            }else{
                /* different qscale, we must rescale */
                for(i=1; i<8; i++){
                    const int level= block[n][s->idct_permutation[i<<3]];
                    score0+= ABS(level);
                    score1+= ABS(level - ROUNDED_DIV(ac_val[i]*qscale_table[xy], s->qscale));
                    ac_val1[i  ]= level;
                    ac_val1[i+8]=    block[n][s->idct_permutation[i   ]];
                }
            }
        }
    }

    return score0 > score1 ? 1 : 0;    
}

/**
 * modify qscale so that encoding is acually possible in h263 (limit difference to -2..2)
 */
void ff_clean_h263_qscales(MpegEncContext *s){
    int i;
    int8_t * const qscale_table= s->current_picture.qscale_table;
    
    for(i=1; i<s->mb_num; i++){
        if(qscale_table[i] - qscale_table[i-1] >2)
            qscale_table[i]= qscale_table[i-1]+2;
    }
    for(i=s->mb_num-2; i>=0; i--){
        if(qscale_table[i] - qscale_table[i+1] >2)
            qscale_table[i]= qscale_table[i+1]+2;
    }
}

/**
 * modify mb_type & qscale so that encoding is acually possible in mpeg4
 */
void ff_clean_mpeg4_qscales(MpegEncContext *s){
    int i;
    int8_t * const qscale_table= s->current_picture.qscale_table;

    ff_clean_h263_qscales(s);
    
    for(i=1; i<s->mb_num; i++){
        if(qscale_table[i] != qscale_table[i-1] && (s->mb_type[i]&MB_TYPE_INTER4V)){
            s->mb_type[i]&= ~MB_TYPE_INTER4V;
            s->mb_type[i]|= MB_TYPE_INTER;
        }
    }

    if(s->pict_type== B_TYPE){
        int odd=0;
        /* ok, come on, this isnt funny anymore, theres more code for handling this mpeg4 mess than
           for the actual adaptive quantization */
        
        for(i=0; i<s->mb_num; i++){
            odd += qscale_table[i]&1;
        }
        
        if(2*odd > s->mb_num) odd=1;
        else                  odd=0;
        
        for(i=0; i<s->mb_num; i++){
            if((qscale_table[i]&1) != odd)
                qscale_table[i]++;
            if(qscale_table[i] > 31)
                qscale_table[i]= 31;
        }            
    
        for(i=1; i<s->mb_num; i++){
            if(qscale_table[i] != qscale_table[i-1] && (s->mb_type[i]&MB_TYPE_DIRECT)){
                s->mb_type[i]&= ~MB_TYPE_DIRECT;
                s->mb_type[i]|= MB_TYPE_BIDIR;
            }
        }
    }
}

void ff_mpeg4_set_direct_mv(MpegEncContext *s, int mx, int my){
    const int mb_index= s->mb_x + s->mb_y*s->mb_width;
    int xy= s->block_index[0];
    uint16_t time_pp= s->pp_time;
    uint16_t time_pb= s->pb_time;
    int i;
        
    //FIXME avoid divides
    switch(s->co_located_type_table[mb_index]){
    case 0:
        s->mv_type= MV_TYPE_16X16;
        s->mv[0][0][0] = s->motion_val[xy][0]*time_pb/time_pp + mx;
        s->mv[0][0][1] = s->motion_val[xy][1]*time_pb/time_pp + my;
        s->mv[1][0][0] = mx ? s->mv[0][0][0] - s->motion_val[xy][0]
                            : s->motion_val[xy][0]*(time_pb - time_pp)/time_pp;
        s->mv[1][0][1] = my ? s->mv[0][0][1] - s->motion_val[xy][1] 
                            : s->motion_val[xy][1]*(time_pb - time_pp)/time_pp;
        break;
    case CO_LOCATED_TYPE_4MV:
        s->mv_type = MV_TYPE_8X8;
        for(i=0; i<4; i++){
            xy= s->block_index[i];
            s->mv[0][i][0] = s->motion_val[xy][0]*time_pb/time_pp + mx;
            s->mv[0][i][1] = s->motion_val[xy][1]*time_pb/time_pp + my;
            s->mv[1][i][0] = mx ? s->mv[0][i][0] - s->motion_val[xy][0]
                                : s->motion_val[xy][0]*(time_pb - time_pp)/time_pp;
            s->mv[1][i][1] = my ? s->mv[0][i][1] - s->motion_val[xy][1] 
                                : s->motion_val[xy][1]*(time_pb - time_pp)/time_pp;
        }
        break;
    case CO_LOCATED_TYPE_FIELDMV:
        s->mv_type = MV_TYPE_FIELD;
        for(i=0; i<2; i++){
            if(s->top_field_first){
                time_pp= s->pp_field_time - s->field_select_table[mb_index][i] + i;
                time_pb= s->pb_field_time - s->field_select_table[mb_index][i] + i;
            }else{
                time_pp= s->pp_field_time + s->field_select_table[mb_index][i] - i;
                time_pb= s->pb_field_time + s->field_select_table[mb_index][i] - i;
            }
            s->mv[0][i][0] = s->field_mv_table[mb_index][i][0]*time_pb/time_pp + mx;
            s->mv[0][i][1] = s->field_mv_table[mb_index][i][1]*time_pb/time_pp + my;
            s->mv[1][i][0] = mx ? s->mv[0][i][0] - s->field_mv_table[mb_index][i][0]
                                : s->field_mv_table[mb_index][i][0]*(time_pb - time_pp)/time_pp;
            s->mv[1][i][1] = my ? s->mv[0][i][1] - s->field_mv_table[mb_index][i][1] 
                                : s->field_mv_table[mb_index][i][1]*(time_pb - time_pp)/time_pp;
        }
        break;
    }
}

#ifdef CONFIG_ENCODERS
void mpeg4_encode_mb(MpegEncContext * s,
		    DCTELEM block[6][64],
		    int motion_x, int motion_y)
{
    int cbpc, cbpy, i, pred_x, pred_y;
    int bits;
    PutBitContext * const pb2    = s->data_partitioning                         ? &s->pb2    : &s->pb;
    PutBitContext * const tex_pb = s->data_partitioning && s->pict_type!=B_TYPE ? &s->tex_pb : &s->pb;
    PutBitContext * const dc_pb  = s->data_partitioning && s->pict_type!=I_TYPE ? &s->pb2    : &s->pb;
    const int interleaved_stats= (s->flags&CODEC_FLAG_PASS1) && !s->data_partitioning ? 1 : 0;
    const int dquant_code[5]= {1,0,9,2,3};
    
    //    printf("**mb x=%d y=%d\n", s->mb_x, s->mb_y);
    if (!s->mb_intra) {
        /* compute cbp */
        int cbp = 0;
        for (i = 0; i < 6; i++) {
            if (s->block_last_index[i] >= 0)
                cbp |= 1 << (5 - i);
        }

        if(s->pict_type==B_TYPE){
            static const int mb_type_table[8]= {-1, 2, 3, 1,-1,-1,-1, 0}; /* convert from mv_dir to type */
            int mb_type=  mb_type_table[s->mv_dir];
            
            if(s->mb_x==0){
                s->last_mv[0][0][0]= 
                s->last_mv[0][0][1]= 
                s->last_mv[1][0][0]= 
                s->last_mv[1][0][1]= 0;
            }
            
            assert(s->dquant>=-2 && s->dquant<=2);
            assert((s->dquant&1)==0);
            assert(mb_type>=0);

            /* nothing to do if this MB was skiped in the next P Frame */
            if(s->next_picture.mbskip_table[s->mb_y * s->mb_width + s->mb_x]){ //FIXME avoid DCT & ...
                s->skip_count++;
                s->mv[0][0][0]= 
                s->mv[0][0][1]= 
                s->mv[1][0][0]= 
                s->mv[1][0][1]= 0;
                s->mv_dir= MV_DIR_FORWARD; //doesnt matter
                s->qscale -= s->dquant;
//                s->mb_skiped=1;

                return;
            }
            
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
            
            put_bits(&s->pb, 1, 0);	/* mb coded modb1=0 */
            put_bits(&s->pb, 1, cbp ? 0 : 1); /* modb2 */ //FIXME merge
            put_bits(&s->pb, mb_type+1, 1); // this table is so simple that we dont need it :)
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
                if(mb_type) // not diect mode
                    put_bits(&s->pb, 1, 0); // no interlaced ME yet
            }

            if(interleaved_stats){
                bits= get_bit_count(&s->pb);
                s->misc_bits+= bits - s->last_bits;
                s->last_bits=bits;
            }

            switch(mb_type)
            {
            case 0: /* direct */
                h263_encode_motion(s, motion_x, 1);
                h263_encode_motion(s, motion_y, 1);                
                s->b_count++;
                s->f_count++;
                break;
            case 1: /* bidir */
                h263_encode_motion(s, s->mv[0][0][0] - s->last_mv[0][0][0], s->f_code);
                h263_encode_motion(s, s->mv[0][0][1] - s->last_mv[0][0][1], s->f_code);
                h263_encode_motion(s, s->mv[1][0][0] - s->last_mv[1][0][0], s->b_code);
                h263_encode_motion(s, s->mv[1][0][1] - s->last_mv[1][0][1], s->b_code);
                s->last_mv[0][0][0]= s->mv[0][0][0];
                s->last_mv[0][0][1]= s->mv[0][0][1];
                s->last_mv[1][0][0]= s->mv[1][0][0];
                s->last_mv[1][0][1]= s->mv[1][0][1];
                s->b_count++;
                s->f_count++;
                break;
            case 2: /* backward */
                h263_encode_motion(s, motion_x - s->last_mv[1][0][0], s->b_code);
                h263_encode_motion(s, motion_y - s->last_mv[1][0][1], s->b_code);
                s->last_mv[1][0][0]= motion_x;
                s->last_mv[1][0][1]= motion_y;
                s->b_count++;
                break;
            case 3: /* forward */
                h263_encode_motion(s, motion_x - s->last_mv[0][0][0], s->f_code);
                h263_encode_motion(s, motion_y - s->last_mv[0][0][1], s->f_code);
                s->last_mv[0][0][0]= motion_x;
                s->last_mv[0][0][1]= motion_y;
                s->f_count++;
                break;
            default:
                printf("unknown mb type\n");
                return;
            }

            if(interleaved_stats){
                bits= get_bit_count(&s->pb);
                s->mv_bits+= bits - s->last_bits;
                s->last_bits=bits;
            }

            /* encode each block */
            for (i = 0; i < 6; i++) {
                mpeg4_encode_block(s, block[i], i, 0, s->intra_scantable.permutated, NULL, &s->pb);
            }

            if(interleaved_stats){
                bits= get_bit_count(&s->pb);
                s->p_tex_bits+= bits - s->last_bits;
                s->last_bits=bits;
            }
        }else{ /* s->pict_type==B_TYPE */
            if ((cbp | motion_x | motion_y | s->dquant) == 0 && s->mv_type==MV_TYPE_16X16) {
                /* check if the B frames can skip it too, as we must skip it if we skip here 
                   why didnt they just compress the skip-mb bits instead of reusing them ?! */
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
                    
                    s->mb_skiped=1;
                    for(i=0; i<s->max_b_frames; i++){
                        uint8_t *b_pic;
                        int diff;
                        Picture *pic= s->reordered_input_picture[i+1];

                        if(pic==NULL || pic->pict_type!=B_TYPE) break;

                        b_pic= pic->data[0] + offset + 16; //FIXME +16
			diff= s->dsp.pix_abs16x16(p_pic, b_pic, s->linesize);
                        if(diff>s->qscale*70){ //FIXME check that 70 is optimal
                            s->mb_skiped=0;
                            break;
                        }
                    }
                }else
                    s->mb_skiped=1; 

                if(s->mb_skiped==1){
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

            put_bits(&s->pb, 1, 0);	/* mb coded */
            if(s->mv_type==MV_TYPE_16X16){
                cbpc = cbp & 3;
                if(s->dquant) cbpc+= 8;
                put_bits(&s->pb,
                        inter_MCBPC_bits[cbpc],
                        inter_MCBPC_code[cbpc]);

                cbpy = cbp >> 2;
                cbpy ^= 0xf;
                put_bits(pb2, cbpy_tab[cbpy][1], cbpy_tab[cbpy][0]);
                if(s->dquant)
                    put_bits(pb2, 2, dquant_code[s->dquant+2]);

                if(!s->progressive_sequence){
                    if(cbp)
                        put_bits(pb2, 1, s->interlaced_dct);
                    put_bits(pb2, 1, 0); // no interlaced ME yet
                }
                    
                if(interleaved_stats){
                    bits= get_bit_count(&s->pb);
                    s->misc_bits+= bits - s->last_bits;
                    s->last_bits=bits;
                }

                /* motion vectors: 16x16 mode */
                h263_pred_motion(s, 0, &pred_x, &pred_y);
            
                h263_encode_motion(s, motion_x - pred_x, s->f_code);
                h263_encode_motion(s, motion_y - pred_y, s->f_code);
            }else{
                cbpc = (cbp & 3)+16;
                put_bits(&s->pb,
                        inter_MCBPC_bits[cbpc],
                        inter_MCBPC_code[cbpc]);
                cbpy = cbp >> 2;
                cbpy ^= 0xf;
                put_bits(pb2, cbpy_tab[cbpy][1], cbpy_tab[cbpy][0]);

                if(!s->progressive_sequence){
                    if(cbp)
                        put_bits(pb2, 1, s->interlaced_dct);
                }
    
                if(interleaved_stats){
                    bits= get_bit_count(&s->pb);
                    s->misc_bits+= bits - s->last_bits;
                    s->last_bits=bits;
                }

                for(i=0; i<4; i++){
                    /* motion vectors: 8x8 mode*/
                    h263_pred_motion(s, i, &pred_x, &pred_y);

                    h263_encode_motion(s, s->motion_val[ s->block_index[i] ][0] - pred_x, s->f_code);
                    h263_encode_motion(s, s->motion_val[ s->block_index[i] ][1] - pred_y, s->f_code);
                }
            }

            if(interleaved_stats){ 
                bits= get_bit_count(&s->pb);
                s->mv_bits+= bits - s->last_bits;
                s->last_bits=bits;
            }

            /* encode each block */
            for (i = 0; i < 6; i++) {
                mpeg4_encode_block(s, block[i], i, 0, s->intra_scantable.permutated, NULL, tex_pb);
            }

            if(interleaved_stats){
                bits= get_bit_count(&s->pb);
                s->p_tex_bits+= bits - s->last_bits;
                s->last_bits=bits;
            }
            s->f_count++;
        }
    } else {
        int cbp;
        int dc_diff[6];   //dc values with the dc prediction subtracted 
        int dir[6];  //prediction direction
        int zigzag_last_index[6];
        UINT8 *scan_table[6];

        for(i=0; i<6; i++){
            const int level= block[i][0];
            UINT16 *dc_ptr;

            dc_diff[i]= level - ff_mpeg4_pred_dc(s, i, &dc_ptr, &dir[i]);
            if (i < 4) {
                *dc_ptr = level * s->y_dc_scale;
            } else {
                *dc_ptr = level * s->c_dc_scale;
            }
        }

        s->ac_pred= decide_ac_pred(s, block, dir);

        if(s->ac_pred){
            for(i=0; i<6; i++){
                UINT8 *st;
                int last_index;

                mpeg4_inv_pred_ac(s, block[i], i, dir[i]);
                if (dir[i]==0) st = s->intra_v_scantable.permutated; /* left */
                else           st = s->intra_h_scantable.permutated; /* top */

                for(last_index=63; last_index>=0; last_index--) //FIXME optimize
                    if(block[i][st[last_index]]) break;
                zigzag_last_index[i]= s->block_last_index[i];
                s->block_last_index[i]= last_index;
                scan_table[i]= st;
            }
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
        if (s->pict_type == I_TYPE) {
            if(s->dquant) cbpc+=4;
            put_bits(&s->pb,
                intra_MCBPC_bits[cbpc],
                intra_MCBPC_code[cbpc]);
        } else {
            if(s->dquant) cbpc+=8;
            put_bits(&s->pb, 1, 0);	/* mb coded */
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
            bits= get_bit_count(&s->pb);
            s->misc_bits+= bits - s->last_bits;
            s->last_bits=bits;
        }

        /* encode each block */
        for (i = 0; i < 6; i++) {
            mpeg4_encode_block(s, block[i], i, dc_diff[i], scan_table[i], dc_pb, tex_pb);
        }

        if(interleaved_stats){
            bits= get_bit_count(&s->pb);
            s->i_tex_bits+= bits - s->last_bits;
            s->last_bits=bits;
        }
        s->i_count++;

        /* restore ac coeffs & last_index stuff if we messed them up with the prediction */
        if(s->ac_pred){
            for(i=0; i<6; i++){
                int j;    
                INT16 *ac_val;

                ac_val = s->ac_val[0][0] + s->block_index[i] * 16;

                if(dir[i]){
                    for(j=1; j<8; j++) 
                        block[i][s->idct_permutation[j   ]]= ac_val[j+8];
                }else{
                    for(j=1; j<8; j++) 
                        block[i][s->idct_permutation[j<<3]]= ac_val[j  ];
                }
                s->block_last_index[i]= zigzag_last_index[i];
            }
        }
    }
}

void h263_encode_mb(MpegEncContext * s,
		    DCTELEM block[6][64],
		    int motion_x, int motion_y)
{
    int cbpc, cbpy, i, cbp, pred_x, pred_y;
    INT16 pred_dc;
    INT16 rec_intradc[6];
    UINT16 *dc_ptr[6];
    const int dquant_code[5]= {1,0,9,2,3};
           
    //printf("**mb x=%d y=%d\n", s->mb_x, s->mb_y);
    if (!s->mb_intra) {
        /* compute cbp */
        cbp = 0;
        for (i = 0; i < 6; i++) {
            if (s->block_last_index[i] >= 0)
                cbp |= 1 << (5 - i);
        }
        if ((cbp | motion_x | motion_y | s->dquant) == 0) {
            /* skip macroblock */
            put_bits(&s->pb, 1, 1);
            return;
        }
        put_bits(&s->pb, 1, 0);	/* mb coded */
        cbpc = cbp & 3;
        if(s->dquant) cbpc+= 8;
        put_bits(&s->pb,
		    inter_MCBPC_bits[cbpc],
		    inter_MCBPC_code[cbpc]);
        cbpy = cbp >> 2;
        cbpy ^= 0xf;
        put_bits(&s->pb, cbpy_tab[cbpy][1], cbpy_tab[cbpy][0]);
        if(s->dquant)
            put_bits(&s->pb, 2, dquant_code[s->dquant+2]);

        /* motion vectors: 16x16 mode only now */
        h263_pred_motion(s, 0, &pred_x, &pred_y);
      
        if (!s->umvplus) {  
            h263_encode_motion(s, motion_x - pred_x, s->f_code);
            h263_encode_motion(s, motion_y - pred_y, s->f_code);
        }
        else {
            h263p_encode_umotion(s, motion_x - pred_x);
            h263p_encode_umotion(s, motion_y - pred_y);
            if (((motion_x - pred_x) == 1) && ((motion_y - pred_y) == 1))
                /* To prevent Start Code emulation */
                put_bits(&s->pb,1,1);
        }
    } else {
        int li = s->h263_aic ? 0 : 1;
        
        cbp = 0;
        for(i=0; i<6; i++) {
            /* Predict DC */
            if (s->h263_aic && s->mb_intra) {
                INT16 level = block[i][0];
            
                pred_dc = h263_pred_dc(s, i, &dc_ptr[i]);
                level -= pred_dc;
                /* Quant */
                if (level < 0)
                    level = (level + (s->qscale >> 1))/(s->y_dc_scale);
                else
                    level = (level - (s->qscale >> 1))/(s->y_dc_scale);
                    
                /* AIC can change CBP */
                if (level == 0 && s->block_last_index[i] == 0)
                    s->block_last_index[i] = -1;
                else if (level < -127)
                    level = -127;
                else if (level > 127)
                    level = 127;
                
                block[i][0] = level;
                /* Reconstruction */ 
                rec_intradc[i] = (s->y_dc_scale*level) + pred_dc;
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
            }
            /* compute cbp */
            if (s->block_last_index[i] >= li)
                cbp |= 1 << (5 - i);
        }

        cbpc = cbp & 3;
        if (s->pict_type == I_TYPE) {
            if(s->dquant) cbpc+=4;
            put_bits(&s->pb,
                intra_MCBPC_bits[cbpc],
                intra_MCBPC_code[cbpc]);
        } else {
            if(s->dquant) cbpc+=8;
            put_bits(&s->pb, 1, 0);	/* mb coded */
            put_bits(&s->pb,
                inter_MCBPC_bits[cbpc + 4],
                inter_MCBPC_code[cbpc + 4]);
        }
        if (s->h263_aic) {
            /* XXX: currently, we do not try to use ac prediction */
            put_bits(&s->pb, 1, 0);	/* no AC prediction */
        }
        cbpy = cbp >> 2;
        put_bits(&s->pb, cbpy_tab[cbpy][1], cbpy_tab[cbpy][0]);
        if(s->dquant)
            put_bits(&s->pb, 2, dquant_code[s->dquant+2]);
    }

    for(i=0; i<6; i++) {
        /* encode each block */
        h263_encode_block(s, block[i], i);
    
        /* Update INTRADC for decoding */
        if (s->h263_aic && s->mb_intra) {
            block[i][0] = rec_intradc[i];
            
        }
    }
}
#endif

static int h263_pred_dc(MpegEncContext * s, int n, UINT16 **dc_val_ptr)
{
    int x, y, wrap, a, c, pred_dc, scale;
    INT16 *dc_val, *ac_val;

    /* find prediction */
    if (n < 4) {
        x = 2 * s->mb_x + 1 + (n & 1);
        y = 2 * s->mb_y + 1 + ((n & 2) >> 1);
        wrap = s->mb_width * 2 + 2;
        dc_val = s->dc_val[0];
        ac_val = s->ac_val[0][0];
        scale = s->y_dc_scale;
    } else {
        x = s->mb_x + 1;
        y = s->mb_y + 1;
        wrap = s->mb_width + 2;
        dc_val = s->dc_val[n - 4 + 1];
        ac_val = s->ac_val[n - 4 + 1][0];
        scale = s->c_dc_scale;
    }
    /* B C
     * A X 
     */
    a = dc_val[(x - 1) + (y) * wrap];
    c = dc_val[(x) + (y - 1) * wrap];
    
    /* No prediction outside GOB boundary */
    if (s->first_slice_line && ((n < 2) || (n > 3)))
        c = 1024;
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


void h263_pred_acdc(MpegEncContext * s, DCTELEM *block, int n)
{
    int x, y, wrap, a, c, pred_dc, scale, i;
    INT16 *dc_val, *ac_val, *ac_val1;

    /* find prediction */
    if (n < 4) {
        x = 2 * s->mb_x + 1 + (n & 1);
        y = 2 * s->mb_y + 1 + ((n & 2) >> 1);
        wrap = s->mb_width * 2 + 2;
        dc_val = s->dc_val[0];
        ac_val = s->ac_val[0][0];
        scale = s->y_dc_scale;
    } else {
        x = s->mb_x + 1;
        y = s->mb_y + 1;
        wrap = s->mb_width + 2;
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
    if (s->first_slice_line && ((n < 2) || (n > 3)))
        c = 1024;
    pred_dc = 1024;
    if (s->ac_pred) {
        if (s->h263_aic_dir) {
            /* left prediction */
            if (a != 1024) {
                ac_val -= 16;
                for(i=1;i<8;i++) {
                    block[s->idct_permutation[i<<3]] += ac_val[i];
                }
                pred_dc = a;
            }
        } else {
            /* top prediction */
            if (c != 1024) {
                ac_val -= 16 * wrap;
                for(i=1;i<8;i++) {
                    block[s->idct_permutation[i   ]] += ac_val[i + 8];
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
    else if (!(block[0] & 1))
        block[0]++;
    
    /* Update AC/DC tables */
    dc_val[(x) + (y) * wrap] = block[0];
    
    /* left copy */
    for(i=1;i<8;i++)
        ac_val1[i    ] = block[s->idct_permutation[i<<3]];
    /* top copy */
    for(i=1;i<8;i++)
        ac_val1[8 + i] = block[s->idct_permutation[i   ]];
}

INT16 *h263_pred_motion(MpegEncContext * s, int block, 
                        int *px, int *py)
{
    int xy, wrap;
    INT16 *A, *B, *C, *mot_val;
    static const int off[4]= {2, 1, 1, -1};

    wrap = s->block_wrap[0];
    xy = s->block_index[block];

    mot_val = s->motion_val[xy];

    A = s->motion_val[xy - 1];
    /* special case for first (slice) line */
    if (s->first_slice_line && block<3) {
        // we cant just change some MVs to simulate that as we need them for the B frames (and ME)
        // and if we ever support non rectangular objects than we need to do a few ifs here anyway :(
        if(block==0){ //most common case
            if(s->mb_x  == s->resync_mb_x){ //rare
                *px= *py = 0;
            }else if(s->mb_x + 1 == s->resync_mb_x){ //rare
                C = s->motion_val[xy + off[block] - wrap];
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
            if(s->mb_x + 1 == s->resync_mb_x){ //rare
                C = s->motion_val[xy + off[block] - wrap];
                *px = mid_pred(A[0], 0, C[0]);
                *py = mid_pred(A[1], 0, C[1]);
            }else{
                *px = A[0];
                *py = A[1];
            }
        }else{ /* block==2*/
            B = s->motion_val[xy - wrap];
            C = s->motion_val[xy + off[block] - wrap];
            if(s->mb_x == s->resync_mb_x) //rare
                A[0]=A[1]=0;
    
            *px = mid_pred(A[0], B[0], C[0]);
            *py = mid_pred(A[1], B[1], C[1]);
        }
    } else {
        B = s->motion_val[xy - wrap];
        C = s->motion_val[xy + off[block] - wrap];
        *px = mid_pred(A[0], B[0], C[0]);
        *py = mid_pred(A[1], B[1], C[1]);
    }
    return mot_val;
}

#ifdef CONFIG_ENCODERS
static void h263_encode_motion(MpegEncContext * s, int val, int f_code)
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
        l = range * 32;
#if 1
        val+= l;
        val&= 2*l-1;
        val-= l;
        sign = val>>31;
        val= (val^sign)-sign;
        sign&=1;
#else
        if (val < -l) {
            val += 2*l;
        } else if (val >= l) {
            val -= 2*l;
        }

        assert(val>=-l && val<l);

        if (val >= 0) {
            sign = 0;
        } else {
            val = -val;
            sign = 1;
        }
#endif
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
    
    if(mv_penalty==NULL)
        mv_penalty= av_mallocz( sizeof(UINT16)*(MAX_FCODE+1)*(2*MAX_MV+1) );
    
    for(f_code=1; f_code<=MAX_FCODE; f_code++){
        for(mv=-MAX_MV; mv<=MAX_MV; mv++){
            int len;

            if(mv==0) len= mvtab[0][1];
            else{
                int val, bit_size, range, code;

                bit_size = s->f_code - 1;
                range = 1 << bit_size;

                val=mv;
                if (val < 0) 
                    val = -val;
                val--;
                code = (val >> bit_size) + 1;
                if(code<33){
                    len= mvtab[code][1] + 1 + bit_size;
                }else{
                    len= mvtab[32][1] + 2 + bit_size;
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
#endif

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

#ifdef CONFIG_ENCODERS
static void init_uni_mpeg4_rl_tab(RLTable *rl, UINT32 *bits_tab, UINT8 *len_tab){
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

void h263_encode_init(MpegEncContext *s)
{
    static int done = 0;

    if (!done) {
        done = 1;

        init_uni_dc_tab();

        init_rl(&rl_inter);
        init_rl(&rl_intra);
        init_rl(&rl_intra_aic);
        
        init_uni_mpeg4_rl_tab(&rl_intra, uni_mpeg4_intra_rl_bits, uni_mpeg4_intra_rl_len);
        init_uni_mpeg4_rl_tab(&rl_inter, uni_mpeg4_inter_rl_bits, uni_mpeg4_inter_rl_len);

        init_mv_penalty_and_fcode(s);
    }
    s->me.mv_penalty= mv_penalty; //FIXME exact table for msmpeg4 & h263p
    
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
        break;
    case CODEC_ID_H263P:
        s->fcode_tab= umv_fcode_tab;
        s->min_qcoeff= -128;
        s->max_qcoeff=  127;
        break;
        //Note for mpeg4 & h263 the dc-scale table will be set per frame as needed later 
    default: //nothing needed default table allready set in mpegvideo.c
        s->min_qcoeff= -128;
        s->max_qcoeff=  127;
        s->y_dc_scale_table=
        s->c_dc_scale_table= ff_mpeg1_dc_scale_table;
    }

    if(s->mpeg_quant){
        s->intra_quant_bias= 3<<(QUANT_BIAS_SHIFT-3); //(a + x*3/8)/x
        s->inter_quant_bias= 0;
    }else{
        s->intra_quant_bias=0;
        s->inter_quant_bias=-(1<<(QUANT_BIAS_SHIFT-2)); //(a - x/4)/x
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
        else if (!level) {
            level = 1;
            block[0] = 1;
        }
	    if (level == 128)
	        put_bits(&s->pb, 8, 0xff);
	    else
	        put_bits(&s->pb, 8, level & 0xff);
	    i = 1;
    } else {
	    i = 0;
	    if (s->h263_aic && s->mb_intra)
	        rl = &rl_intra_aic;
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
                put_bits(&s->pb, 1, last);
                put_bits(&s->pb, 6, run);
                put_bits(&s->pb, 8, slevel & 0xff);
            } else {
                put_bits(&s->pb, 1, sign);
            }
	        last_non_zero = i;
	    }
    }
}
#endif

/***************************************************/
/**
 * add mpeg4 stuffing bits (01...1)
 */
void ff_mpeg4_stuffing(PutBitContext * pbc)
{
    int length;
    put_bits(pbc, 1, 0);
    length= (-get_bit_count(pbc))&7;
    if(length) put_bits(pbc, length, (1<<length)-1);
}

/* must be called before writing the header */
void ff_set_mpeg4_time(MpegEncContext * s, int picture_number){
    int time_div, time_mod;

    if(s->pict_type==I_TYPE){ //we will encode a vol header
        s->time_increment_resolution= s->frame_rate/ff_gcd(s->frame_rate, FRAME_RATE_BASE);
        if(s->time_increment_resolution>=256*256) s->time_increment_resolution= 256*128;

        s->time_increment_bits = av_log2(s->time_increment_resolution - 1) + 1;
    }
    
    if(s->current_picture.pts)
        s->time= (s->current_picture.pts*s->time_increment_resolution + 500*1000)/(1000*1000);
    else
        s->time= picture_number*(INT64)FRAME_RATE_BASE*s->time_increment_resolution/s->frame_rate;
    time_div= s->time/s->time_increment_resolution;
    time_mod= s->time%s->time_increment_resolution;

    if(s->pict_type==B_TYPE){
        s->pb_time= s->pp_time - (s->last_non_b_time - s->time);
    }else{
        s->last_time_base= s->time_base;
        s->time_base= time_div;
        s->pp_time= s->time - s->last_non_b_time;
        s->last_non_b_time= s->time;
    }
}

static void mpeg4_encode_gop_header(MpegEncContext * s){
    int hours, minutes, seconds;
    
    put_bits(&s->pb, 16, 0);
    put_bits(&s->pb, 16, GOP_STARTCODE);
    
    seconds= s->time/s->time_increment_resolution;
    minutes= seconds/60; seconds %= 60;
    hours= minutes/60; minutes %= 60;
    hours%=24;

    put_bits(&s->pb, 5, hours);
    put_bits(&s->pb, 6, minutes);
    put_bits(&s->pb, 1, 1);
    put_bits(&s->pb, 6, seconds);
    
    put_bits(&s->pb, 1, 0); //closed gov == NO
    put_bits(&s->pb, 1, 0); //broken link == NO

    ff_mpeg4_stuffing(&s->pb);
}

static void mpeg4_encode_visual_object_header(MpegEncContext * s){
    int profile_and_level_indication;
    int vo_ver_id;
    
    if(s->max_b_frames || s->quarter_sample){
        profile_and_level_indication= 0xF1; // adv simple level 1
        vo_ver_id= 5;
    }else{
        profile_and_level_indication= 0x01; // simple level 1
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
    char buf[255];

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

    put_bits(&s->pb, 1, 0);		/* random access vol */
    put_bits(&s->pb, 8, s->vo_type);	/* video obj type indication */
    put_bits(&s->pb, 1, 1);		/* is obj layer id= yes */
      put_bits(&s->pb, 4, vo_ver_id);	/* is obj layer ver id */
      put_bits(&s->pb, 3, 1);		/* is obj layer priority */
    
    float_aspect_to_info(s, s->avctx->aspect_ratio);

    put_bits(&s->pb, 4, s->aspect_ratio_info);/* aspect ratio info */
    if (s->aspect_ratio_info == FF_ASPECT_EXTENDED)
    {
	put_bits(&s->pb, 8, s->aspected_width);
	put_bits(&s->pb, 8, s->aspected_height);
    }

    if(s->low_delay){
        put_bits(&s->pb, 1, 1);		/* vol control parameters= yes */
        put_bits(&s->pb, 2, 1);		/* chroma format YUV 420/YV12 */
        put_bits(&s->pb, 1, s->low_delay);
        put_bits(&s->pb, 1, 0);		/* vbv parameters= no */
    }else{
        put_bits(&s->pb, 1, 0);		/* vol control parameters= no */
    }

    put_bits(&s->pb, 2, RECT_SHAPE);	/* vol shape= rectangle */
    put_bits(&s->pb, 1, 1);		/* marker bit */
    
    put_bits(&s->pb, 16, s->time_increment_resolution);
    if (s->time_increment_bits < 1)
        s->time_increment_bits = 1;
    put_bits(&s->pb, 1, 1);		/* marker bit */
    put_bits(&s->pb, 1, 0);		/* fixed vop rate=no */
    put_bits(&s->pb, 1, 1);		/* marker bit */
    put_bits(&s->pb, 13, s->width);	/* vol width */
    put_bits(&s->pb, 1, 1);		/* marker bit */
    put_bits(&s->pb, 13, s->height);	/* vol height */
    put_bits(&s->pb, 1, 1);		/* marker bit */
    put_bits(&s->pb, 1, s->progressive_sequence ? 0 : 1);
    put_bits(&s->pb, 1, 1);		/* obmc disable */
    if (vo_ver_id == 1) {
        put_bits(&s->pb, 1, s->vol_sprite_usage=0);		/* sprite enable */
    }else{
        put_bits(&s->pb, 2, s->vol_sprite_usage=0);		/* sprite enable */
    }
    
    s->quant_precision=5;
    put_bits(&s->pb, 1, 0);		/* not 8 bit == false */
    put_bits(&s->pb, 1, s->mpeg_quant);	/* quant type= (0=h263 style)*/
    if(s->mpeg_quant) put_bits(&s->pb, 2, 0); /* no custom matrixes */

    if (vo_ver_id != 1)
        put_bits(&s->pb, 1, s->quarter_sample);
    put_bits(&s->pb, 1, 1);		/* complexity estimation disable */
    s->resync_marker= s->rtp_mode;
    put_bits(&s->pb, 1, s->resync_marker ? 0 : 1);/* resync marker disable */
    put_bits(&s->pb, 1, s->data_partitioning ? 1 : 0);
    if(s->data_partitioning){
        put_bits(&s->pb, 1, 0);		/* no rvlc */
    }

    if (vo_ver_id != 1){
        put_bits(&s->pb, 1, 0);		/* newpred */
        put_bits(&s->pb, 1, 0);		/* reduced res vop */
    }
    put_bits(&s->pb, 1, 0);		/* scalability */
    
    ff_mpeg4_stuffing(&s->pb);

    /* user data */
    if(!ff_bit_exact){
        put_bits(&s->pb, 16, 0);
        put_bits(&s->pb, 16, 0x1B2);	/* user_data */
        sprintf(buf, "FFmpeg%sb%s", FFMPEG_VERSION, LIBAVCODEC_BUILD_STR);
        put_string(&s->pb, buf);
        ff_mpeg4_stuffing(&s->pb);
    }
}

/* write mpeg4 VOP header */
void mpeg4_encode_picture_header(MpegEncContext * s, int picture_number)
{
    int time_incr;
    int time_div, time_mod;
    
    if(s->pict_type==I_TYPE){
        if(!(s->flags&CODEC_FLAG_GLOBAL_HEADER)){
            mpeg4_encode_visual_object_header(s);
            mpeg4_encode_vol_header(s, 0, 0);
        }
        mpeg4_encode_gop_header(s);
    }
    
    s->partitioned_frame= s->data_partitioning && s->pict_type!=B_TYPE;

//printf("num:%d rate:%d base:%d\n", s->picture_number, s->frame_rate, FRAME_RATE_BASE);
    
    put_bits(&s->pb, 16, 0);	        /* vop header */
    put_bits(&s->pb, 16, VOP_STARTCODE);	/* vop header */
    put_bits(&s->pb, 2, s->pict_type - 1);	/* pict type: I = 0 , P = 1 */

    time_div= s->time/s->time_increment_resolution;
    time_mod= s->time%s->time_increment_resolution;
    time_incr= time_div - s->last_time_base;
    while(time_incr--)
        put_bits(&s->pb, 1, 1);
        
    put_bits(&s->pb, 1, 0);

    put_bits(&s->pb, 1, 1);	/* marker */
    put_bits(&s->pb, s->time_increment_bits, time_mod);	/* time increment */
    put_bits(&s->pb, 1, 1);	/* marker */
    put_bits(&s->pb, 1, 1);	/* vop coded */
    if (    s->pict_type == P_TYPE 
        || (s->pict_type == S_TYPE && s->vol_sprite_usage==GMC_SPRITE)) {
	put_bits(&s->pb, 1, s->no_rounding);	/* rounding type */
    }
    put_bits(&s->pb, 3, 0);	/* intra dc VLC threshold */
    if(!s->progressive_sequence){
         put_bits(&s->pb, 1, s->top_field_first);
         put_bits(&s->pb, 1, s->alternate_scan);
    }
    //FIXME sprite stuff

    put_bits(&s->pb, 5, s->qscale);

    if (s->pict_type != I_TYPE)
	put_bits(&s->pb, 3, s->f_code);	/* fcode_for */
    if (s->pict_type == B_TYPE)
	put_bits(&s->pb, 3, s->b_code);	/* fcode_back */
    //    printf("****frame %d\n", picture_number);

     s->y_dc_scale_table= ff_mpeg4_y_dc_scale_table; //FIXME add short header support 
     s->c_dc_scale_table= ff_mpeg4_c_dc_scale_table;
     s->h_edge_pos= s->width;
     s->v_edge_pos= s->height;
}

/**
 * change qscale by given dquant and update qscale dependant variables.
 */
static void change_qscale(MpegEncContext * s, int dquant)
{
    s->qscale += dquant;

    if (s->qscale < 1)
        s->qscale = 1;
    else if (s->qscale > 31)
        s->qscale = 31;

    s->y_dc_scale= s->y_dc_scale_table[ s->qscale ];
    s->c_dc_scale= s->c_dc_scale_table[ s->qscale ];
}

/**
 * predicts the dc.
 * @param n block index (0-3 are luma, 4-5 are chroma)
 * @param dc_val_ptr a pointer to the dc_val entry for the current MB will be stored here
 * @param dir_ptr pointer to an integer where the prediction direction will be stored
 * @return the quantized predicted dc
 */
static inline int ff_mpeg4_pred_dc(MpegEncContext * s, int n, UINT16 **dc_val_ptr, int *dir_ptr)
{
    int a, b, c, wrap, pred, scale;
    UINT16 *dc_val;
    int dummy;

    /* find prediction */
    if (n < 4) {
	scale = s->y_dc_scale;
    } else {
	scale = s->c_dc_scale;
    }
    wrap= s->block_wrap[n];
    dc_val = s->dc_val[0] + s->block_index[n];

    /* B C
     * A X 
     */
    a = dc_val[ - 1];
    b = dc_val[ - 1 - wrap];
    c = dc_val[ - wrap];

    /* outside slice handling (we cant do that by memset as we need the dc for error resilience) */
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
#ifdef ARCH_X86
	asm volatile (
		"xorl %%edx, %%edx	\n\t"
		"mul %%ecx		\n\t"
		: "=d" (pred), "=a"(dummy)
		: "a" (pred + (scale >> 1)), "c" (inverse[scale])
	);
#else
    pred = (pred + (scale >> 1)) / scale;
#endif

    /* prepare address for prediction update */
    *dc_val_ptr = &dc_val[0];

    return pred;
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
    INT16 *ac_val, *ac_val1;
    int8_t * const qscale_table= s->current_picture.qscale_table;

    /* find prediction */
    ac_val = s->ac_val[0][0] + s->block_index[n] * 16;
    ac_val1 = ac_val;
    if (s->ac_pred) {
        if (dir == 0) {
            const int xy= s->mb_x-1 + s->mb_y*s->mb_width;
            /* left prediction */
            ac_val -= 16;
            
            if(s->mb_x==0 || s->qscale == qscale_table[xy] || n==1 || n==3){
                /* same qscale */
                for(i=1;i<8;i++) {
                    block[s->idct_permutation[i<<3]] += ac_val[i];
                }
            }else{
                /* different qscale, we must rescale */
                for(i=1;i<8;i++) {
                    block[s->idct_permutation[i<<3]] += ROUNDED_DIV(ac_val[i]*qscale_table[xy], s->qscale);
                }
            }
        } else {
            const int xy= s->mb_x + s->mb_y*s->mb_width - s->mb_width;
            /* top prediction */
            ac_val -= 16 * s->block_wrap[n];

            if(s->mb_y==0 || s->qscale == qscale_table[xy] || n==2 || n==3){
                /* same qscale */
                for(i=1;i<8;i++) {
                    block[s->idct_permutation[i]] += ac_val[i + 8];
                }
            }else{
                /* different qscale, we must rescale */
                for(i=1;i<8;i++) {
                    block[s->idct_permutation[i]] += ROUNDED_DIV(ac_val[i + 8]*qscale_table[xy], s->qscale);
                }
            }
        }
    }
    /* left copy */
    for(i=1;i<8;i++)
        ac_val1[i    ] = block[s->idct_permutation[i<<3]];

    /* top copy */
    for(i=1;i<8;i++)
        ac_val1[8 + i] = block[s->idct_permutation[i   ]];

}

static void mpeg4_inv_pred_ac(MpegEncContext * s, DCTELEM *block, int n,
                              int dir)
{
    int i;
    INT16 *ac_val;
    int8_t * const qscale_table= s->current_picture.qscale_table;

    /* find prediction */
    ac_val = s->ac_val[0][0] + s->block_index[n] * 16;
 
    if (dir == 0) {
        const int xy= s->mb_x-1 + s->mb_y*s->mb_width;
        /* left prediction */
        ac_val -= 16;
        if(s->mb_x==0 || s->qscale == qscale_table[xy] || n==1 || n==3){
            /* same qscale */
            for(i=1;i<8;i++) {
                block[s->idct_permutation[i<<3]] -= ac_val[i];
            }
        }else{
            /* different qscale, we must rescale */
            for(i=1;i<8;i++) {
                block[s->idct_permutation[i<<3]] -= ROUNDED_DIV(ac_val[i]*qscale_table[xy], s->qscale);
            }
        }
    } else {
        const int xy= s->mb_x + s->mb_y*s->mb_width - s->mb_width;
        /* top prediction */
        ac_val -= 16 * s->block_wrap[n];
        if(s->mb_y==0 || s->qscale == qscale_table[xy] || n==2 || n==3){
            /* same qscale */
            for(i=1;i<8;i++) {
                block[s->idct_permutation[i]] -= ac_val[i + 8];
            }
        }else{
            /* different qscale, we must rescale */
            for(i=1;i<8;i++) {
                block[s->idct_permutation[i]] -= ROUNDED_DIV(ac_val[i + 8]*qscale_table[xy], s->qscale);
            }
        }
    }
}

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
#ifdef CONFIG_ENCODERS
/**
 * encodes a 8x8 block
 * @param n block index (0-3 are luma, 4-5 are chroma)
 */
static inline void mpeg4_encode_block(MpegEncContext * s, DCTELEM * block, int n, int intra_dc, 
                               UINT8 *scan_table, PutBitContext *dc_pb, PutBitContext *ac_pb)
{
    int i, last_non_zero;
#if 0 //variables for the outcommented version
    int code, sign, last;
#endif
    const RLTable *rl;
    UINT32 *bits_tab;
    UINT8 *len_tab;
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
                        put_bits(ac_pb, 12, slevel & 0xfff);
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

static inline int mpeg4_get_block_length(MpegEncContext * s, DCTELEM * block, int n, int intra_dc, 
                               UINT8 *scan_table)
{
    int i, last_non_zero;
    const RLTable *rl;
    UINT8 *len_tab;
    const int last_index = s->block_last_index[n];
    int len=0;

    if (s->mb_intra) { //Note gcc (3.2.1 at least) will optimize this away
	/* mpeg4 based DC predictor */
	//mpeg4_encode_dc(dc_pb, intra_dc, n); //FIXME
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

void init_vlc_rl(RLTable *rl)
{
    int i, q;
    
    init_vlc(&rl->vlc, 9, rl->n + 1, 
             &rl->table_vlc[0][1], 4, 2,
             &rl->table_vlc[0][0], 4, 2);

    
    for(q=0; q<32; q++){
        int qmul= q*2;
        int qadd= (q-1)|1;
        
        if(q==0){
            qmul=1;
            qadd=0;
        }
        
        rl->rl_vlc[q]= av_malloc(rl->vlc.table_size*sizeof(RL_VLC_ELEM));
        for(i=0; i<rl->vlc.table_size; i++){
            int code= rl->vlc.table[i][0];
            int len = rl->vlc.table[i][1];
            int level, run;
        
            if(len==0){ // illegal code
                run= 66;
                level= MAX_LEVEL;
            }else if(len<0){ //more bits needed
                run= 0;
                level= code;
            }else{
                if(code==rl->n){ //esc
                    run= 66;
                    level= 0;
                }else{
                    run=   rl->table_run  [code] + 1;
                    level= rl->table_level[code] * qmul + qadd;
                    if(code >= rl->last) run+=192;
                }
            }
            rl->rl_vlc[q][i].len= len;
            rl->rl_vlc[q][i].level= level;
            rl->rl_vlc[q][i].run= run;
        }
    }
}

/* init vlcs */

/* XXX: find a better solution to handle static init */
void h263_decode_init_vlc(MpegEncContext *s)
{
    static int done = 0;

    if (!done) {
        done = 1;

        init_vlc(&intra_MCBPC_vlc, INTRA_MCBPC_VLC_BITS, 8, 
                 intra_MCBPC_bits, 1, 1,
                 intra_MCBPC_code, 1, 1);
        init_vlc(&inter_MCBPC_vlc, INTER_MCBPC_VLC_BITS, 25, 
                 inter_MCBPC_bits, 1, 1,
                 inter_MCBPC_code, 1, 1);
        init_vlc(&cbpy_vlc, CBPY_VLC_BITS, 16,
                 &cbpy_tab[0][1], 2, 1,
                 &cbpy_tab[0][0], 2, 1);
        init_vlc(&mv_vlc, MV_VLC_BITS, 33,
                 &mvtab[0][1], 2, 1,
                 &mvtab[0][0], 2, 1);
        init_rl(&rl_inter);
        init_rl(&rl_intra);
        init_rl(&rl_intra_aic);
        init_vlc_rl(&rl_inter);
        init_vlc_rl(&rl_intra);
        init_vlc_rl(&rl_intra_aic);
        init_vlc(&dc_lum, DC_VLC_BITS, 10 /* 13 */,
                 &DCtab_lum[0][1], 2, 1,
                 &DCtab_lum[0][0], 2, 1);
        init_vlc(&dc_chrom, DC_VLC_BITS, 10 /* 13 */,
                 &DCtab_chrom[0][1], 2, 1,
                 &DCtab_chrom[0][0], 2, 1);
        init_vlc(&sprite_trajectory, SPRITE_TRAJ_VLC_BITS, 15,
                 &sprite_trajectory_tab[0][1], 4, 2,
                 &sprite_trajectory_tab[0][0], 4, 2);
        init_vlc(&mb_type_b_vlc, MB_TYPE_B_VLC_BITS, 4,
                 &mb_type_b_tab[0][1], 2, 1,
                 &mb_type_b_tab[0][0], 2, 1);
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

/**
 * decodes the group of blocks header.
 * @return <0 if an error occured
 */
static int h263_decode_gob_header(MpegEncContext *s)
{
    unsigned int val, gfid;
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

#ifdef DEBUG
    fprintf(stderr,"\nGOB Start Code at MB %d\n", (s->mb_y * s->mb_width) + s->mb_x);
#endif
    s->gob_number = get_bits(&s->gb, 5); /* GN */
    gfid = get_bits(&s->gb, 2); /* GFID */
    s->qscale = get_bits(&s->gb, 5); /* GQUANT */
    if(s->qscale==0) 
        return -1;
    s->mb_x= 0;
    s->mb_y= s->gob_index* s->gob_number;
#ifdef DEBUG
    fprintf(stderr, "\nGN: %u GFID: %u Quant: %u\n", s->gob_number, gfid, s->qscale);
#endif
    return 0;
}

static inline void memsetw(short *tab, int val, int n)
{
    int i;
    for(i=0;i<n;i++)
        tab[i] = val;
}

void ff_mpeg4_init_partitions(MpegEncContext *s)
{
    init_put_bits(&s->tex_pb, s->tex_pb_buffer, PB_BUFFER_SIZE, NULL, NULL);
    init_put_bits(&s->pb2   , s->pb2_buffer   , PB_BUFFER_SIZE, NULL, NULL);
}

void ff_mpeg4_merge_partitions(MpegEncContext *s)
{
    const int pb2_len   = get_bit_count(&s->pb2   );
    const int tex_pb_len= get_bit_count(&s->tex_pb);
    const int bits= get_bit_count(&s->pb);

    if(s->pict_type==I_TYPE){
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

    ff_copy_bits(&s->pb, s->pb2_buffer   , pb2_len);
    ff_copy_bits(&s->pb, s->tex_pb_buffer, tex_pb_len);
    s->last_bits= get_bit_count(&s->pb);
}

int ff_mpeg4_get_video_packet_prefix_length(MpegEncContext *s){
    switch(s->pict_type){
        case I_TYPE:
            return 16;
        case P_TYPE:
        case S_TYPE:
            return s->f_code+15;
        case B_TYPE:
            return FFMAX(FFMAX(s->f_code, s->b_code)+15, 17);
        default:
            return -1;
    }
}

void ff_mpeg4_encode_video_packet_header(MpegEncContext *s)
{
    int mb_num_bits= av_log2(s->mb_num - 1) + 1;

    ff_mpeg4_stuffing(&s->pb);
    put_bits(&s->pb, ff_mpeg4_get_video_packet_prefix_length(s), 0);
    put_bits(&s->pb, 1, 1);
    
    put_bits(&s->pb, mb_num_bits, s->mb_x + s->mb_y*s->mb_width);
    put_bits(&s->pb, s->quant_precision, s->qscale);
    put_bits(&s->pb, 1, 0); /* no HEC */
}

/**
 * check if the next stuff is a resync marker or the end.
 * @return 0 if not
 */
static inline int mpeg4_is_resync(MpegEncContext *s){
    const int bits_count= get_bits_count(&s->gb);
    
    if(s->workaround_bugs&FF_BUG_NO_PADDING){
        return 0;
    }

    if(bits_count + 8 >= s->gb.size_in_bits){
        int v= show_bits(&s->gb, 8);
        v|= 0x7F >> (7-(bits_count&7));
                
        if(v==0x7F)
            return 1;
    }else{
        if(show_bits(&s->gb, 16) == ff_mpeg4_resync_prefix[bits_count&7]){
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
        printf("marker does not match f_code\n");
        return -1;
    }
    
    if(s->shape != RECT_SHAPE){
        header_extension= get_bits1(&s->gb);
        //FIXME more stuff here
    }

    mb_num= get_bits(&s->gb, mb_num_bits);
    if(mb_num>=s->mb_num){
        fprintf(stderr, "illegal mb_num in video packet (%d %d) \n", mb_num, s->mb_num);
        return -1;
    }
    s->mb_x= mb_num % s->mb_width;
    s->mb_y= mb_num / s->mb_width;

    if(s->shape != BIN_ONLY_SHAPE){
        int qscale= get_bits(&s->gb, s->quant_precision); 
        if(qscale)
            s->qscale= qscale;
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
//FIXME dont just ignore everything
            if(s->pict_type == S_TYPE && s->vol_sprite_usage==GMC_SPRITE){
                mpeg4_decode_sprite_trajectory(s);
                fprintf(stderr, "untested\n");
            }

            //FIXME reduced res stuff here
            
            if (s->pict_type != I_TYPE) {
                int f_code = get_bits(&s->gb, 3);	/* fcode_for */
                if(f_code==0){
                    printf("Error, video packet header damaged (f_code=0)\n");
                }
            }
            if (s->pict_type == B_TYPE) {
                int b_code = get_bits(&s->gb, 3);
                if(b_code==0){
                    printf("Error, video packet header damaged (b_code=0)\n");
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

    l_wrap= s->block_wrap[0];
    l_xy= s->mb_y*l_wrap*2 + s->mb_x*2;
    c_wrap= s->block_wrap[4];
    c_xy= s->mb_y*c_wrap + s->mb_x;

#if 0
    /* clean DC */
    memsetw(s->dc_val[0] + l_xy, 1024, l_wrap*2+1);
    memsetw(s->dc_val[1] + c_xy, 1024, c_wrap+1);
    memsetw(s->dc_val[2] + c_xy, 1024, c_wrap+1);
#endif

    /* clean AC */
    memset(s->ac_val[0] + l_xy, 0, (l_wrap*2+1)*16*sizeof(INT16));
    memset(s->ac_val[1] + c_xy, 0, (c_wrap  +1)*16*sizeof(INT16));
    memset(s->ac_val[2] + c_xy, 0, (c_wrap  +1)*16*sizeof(INT16));

    /* clean MV */
    // we cant clear the MVs as they might be needed by a b frame
//    memset(s->motion_val + l_xy, 0, (l_wrap*2+1)*2*sizeof(INT16));
//    memset(s->motion_val, 0, 2*sizeof(INT16)*(2 + s->mb_width*2)*(2 + s->mb_height*2));
    s->last_mv[0][0][0]=
    s->last_mv[0][0][1]=
    s->last_mv[1][0][0]=
    s->last_mv[1][0][1]= 0;
}

/**
 * decodes the group of blocks / video packet header.
 * @return <0 if no resync found
 */
int ff_h263_resync(MpegEncContext *s){
    int left, ret;
    
    if(s->codec_id==CODEC_ID_MPEG4)
        skip_bits1(&s->gb);
    
    align_get_bits(&s->gb);

    if(show_bits(&s->gb, 16)==0){
        if(s->codec_id==CODEC_ID_MPEG4)
            ret= mpeg4_decode_video_packet_header(s);
        else
            ret= h263_decode_gob_header(s);
        if(ret>=0)
            return 0;
    }
    //ok, its not where its supposed to be ...
    s->gb= s->last_resync_gb;
    align_get_bits(&s->gb);
    left= s->gb.size_in_bits - get_bits_count(&s->gb);
    
    for(;left>16+1+5+5; left-=8){ 
        if(show_bits(&s->gb, 16)==0){
            GetBitContext bak= s->gb;

            if(s->codec_id==CODEC_ID_MPEG4)
                ret= mpeg4_decode_video_packet_header(s);
            else
                ret= h263_decode_gob_header(s);
            if(ret>=0)
                return 0;

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
 * @return number of MBs decoded or <0 if an error occured
 */
static int mpeg4_decode_partition_a(MpegEncContext *s){
    int mb_num;
    static const INT8 quant_tab[4] = { -1, -2, 1, 2 };
    
    /* decode first partition */
    mb_num=0;
    s->first_slice_line=1;
    for(; s->mb_y<s->mb_height; s->mb_y++){
        ff_init_block_index(s);
        for(; s->mb_x<s->mb_width; s->mb_x++){
            const int xy= s->mb_x + s->mb_y*s->mb_width;
            int cbpc;
            int dir=0;
            
            mb_num++;
            ff_update_block_index(s);
            if(s->mb_x == s->resync_mb_x && s->mb_y == s->resync_mb_y+1)
                s->first_slice_line=0;
            
            if(s->mb_x==0) PRINT_MB_TYPE("\n");

            if(s->pict_type==I_TYPE){
                int i;

                if(show_bits(&s->gb, 19)==DC_MARKER){
                    return mb_num-1;
                }

                PRINT_MB_TYPE("I");
                cbpc = get_vlc2(&s->gb, intra_MCBPC_vlc.table, INTRA_MCBPC_VLC_BITS, 1);
                if (cbpc < 0){

                    fprintf(stderr, "cbpc corrupted at %d %d\n", s->mb_x, s->mb_y);
                    return -1;
                }
                s->cbp_table[xy]= cbpc & 3;
                s->mb_type[xy]= MB_TYPE_INTRA;
                s->mb_intra = 1;

                if(cbpc & 4) {
                    change_qscale(s, quant_tab[get_bits(&s->gb, 2)]);
                }
                s->current_picture.qscale_table[xy]= s->qscale;

                s->mbintra_table[xy]= 1;
                for(i=0; i<6; i++){
                    int dc_pred_dir;
                    int dc= mpeg4_decode_dc(s, i, &dc_pred_dir); 
                    if(dc < 0){
                        fprintf(stderr, "DC corrupted at %d %d\n", s->mb_x, s->mb_y);
                        return -1;
                    }
                    dir<<=1;
                    if(dc_pred_dir) dir|=1;
                }
                s->pred_dir_table[xy]= dir;
                
                s->error_status_table[xy]= AC_ERROR;
            }else{ /* P/S_TYPE */
                int mx, my, pred_x, pred_y, bits;
                INT16 * const mot_val= s->motion_val[s->block_index[0]];
                const int stride= s->block_wrap[0]*2;

                bits= show_bits(&s->gb, 17);
                if(bits==MOTION_MARKER){
                    return mb_num-1;
                }
                skip_bits1(&s->gb);
                if(bits&0x10000){
                    /* skip mb */
                    s->mb_type[xy]= MB_TYPE_SKIPED;
                    if(s->pict_type==S_TYPE && s->vol_sprite_usage==GMC_SPRITE){
                        PRINT_MB_TYPE("G");
                        mx= get_amv(s, 0);
                        my= get_amv(s, 1);
                    }else{
                        PRINT_MB_TYPE("S");
                        mx=my=0;
                    }
                    mot_val[0       ]= mot_val[2       ]=
                    mot_val[0+stride]= mot_val[2+stride]= mx;
                    mot_val[1       ]= mot_val[3       ]=
                    mot_val[1+stride]= mot_val[3+stride]= my;

                    if(s->mbintra_table[xy])
                        ff_clean_intra_table_entries(s);

                    s->error_status_table[xy]= AC_ERROR;
                    continue;
                }
                cbpc = get_vlc2(&s->gb, inter_MCBPC_vlc.table, INTER_MCBPC_VLC_BITS, 2);
                if (cbpc < 0){
                    fprintf(stderr, "cbpc corrupted at %d %d\n", s->mb_x, s->mb_y);
                    return -1;
                }
                if (cbpc > 20)
                    cbpc+=3;
                else if (cbpc == 20)
                    fprintf(stderr, "Stuffing !");
                s->cbp_table[xy]= cbpc&(8+3); //8 is dquant
    
                s->mb_intra = ((cbpc & 4) != 0);
        
                if(s->mb_intra){
                    PRINT_MB_TYPE("I");
                    s->mbintra_table[xy]= 1;
                    s->mb_type[xy]= MB_TYPE_INTRA;
                    mot_val[0       ]= mot_val[2       ]= 
                    mot_val[0+stride]= mot_val[2+stride]= 0;
                    mot_val[1       ]= mot_val[3       ]=
                    mot_val[1+stride]= mot_val[3+stride]= 0;
                    s->error_status_table[xy]= DC_ERROR|AC_ERROR;
                }else{
                    if(s->mbintra_table[xy])
                        ff_clean_intra_table_entries(s);

                    if(s->pict_type==S_TYPE && s->vol_sprite_usage==GMC_SPRITE && (cbpc & 16) == 0)
                        s->mcsel= get_bits1(&s->gb);
                    else s->mcsel= 0;
        
                    if ((cbpc & 16) == 0) {
                        PRINT_MB_TYPE("P");
                        /* 16x16 motion prediction */
                        s->mb_type[xy]= MB_TYPE_INTER;

                        h263_pred_motion(s, 0, &pred_x, &pred_y);
                        if(!s->mcsel){
                            mx = h263_decode_motion(s, pred_x, s->f_code);
                            if (mx >= 0xffff)
                                return -1;

                            my = h263_decode_motion(s, pred_y, s->f_code);
                            if (my >= 0xffff)
                                return -1;
                        } else {
                            mx = get_amv(s, 0);
                            my = get_amv(s, 1);
                        }

                        mot_val[0       ]= mot_val[2       ] =
                        mot_val[0+stride]= mot_val[2+stride]= mx;
                        mot_val[1       ]= mot_val[3       ]=
                        mot_val[1+stride]= mot_val[3+stride]= my;
                    } else {
                        int i;
                        PRINT_MB_TYPE("4");
                        s->mb_type[xy]= MB_TYPE_INTER4V;
                        for(i=0;i<4;i++) {
                            INT16 *mot_val= h263_pred_motion(s, i, &pred_x, &pred_y);
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
                    s->error_status_table[xy]= AC_ERROR;
                }
            }
        }
        s->mb_x= 0;
    }

    return mb_num;
}

/**
 * decode second partition.
 * @return <0 if an error occured
 */
static int mpeg4_decode_partition_b(MpegEncContext *s, int mb_count){
    int mb_num=0;
    static const INT8 quant_tab[4] = { -1, -2, 1, 2 };

    s->mb_x= s->resync_mb_x;
    s->first_slice_line=1;
    for(s->mb_y= s->resync_mb_y; mb_num < mb_count; s->mb_y++){
        ff_init_block_index(s);
        for(; mb_num < mb_count && s->mb_x<s->mb_width; s->mb_x++){
            const int xy= s->mb_x + s->mb_y*s->mb_width;

            mb_num++;
            ff_update_block_index(s);
            if(s->mb_x == s->resync_mb_x && s->mb_y == s->resync_mb_y+1)
                s->first_slice_line=0;
            
            if(s->pict_type==I_TYPE){
                int ac_pred= get_bits1(&s->gb);
                int cbpy = get_vlc2(&s->gb, cbpy_vlc.table, CBPY_VLC_BITS, 1);
                if(cbpy<0){
                    fprintf(stderr, "cbpy corrupted at %d %d\n", s->mb_x, s->mb_y);
                    return -1;
                }
                
                s->cbp_table[xy]|= cbpy<<2;
                s->pred_dir_table[xy]|= ac_pred<<7;
            }else{ /* P || S_TYPE */
                if(s->mb_type[xy]&MB_TYPE_INTRA){          
                    int dir=0,i;
                    int ac_pred = get_bits1(&s->gb);
                    int cbpy = get_vlc2(&s->gb, cbpy_vlc.table, CBPY_VLC_BITS, 1);

                    if(cbpy<0){
                        fprintf(stderr, "I cbpy corrupted at %d %d\n", s->mb_x, s->mb_y);
                        return -1;
                    }
                    
                    if(s->cbp_table[xy] & 8) {
                        change_qscale(s, quant_tab[get_bits(&s->gb, 2)]);
                    }
                    s->current_picture.qscale_table[xy]= s->qscale;

                    for(i=0; i<6; i++){
                        int dc_pred_dir;
                        int dc= mpeg4_decode_dc(s, i, &dc_pred_dir); 
                        if(dc < 0){
                            fprintf(stderr, "DC corrupted at %d %d\n", s->mb_x, s->mb_y);
                            return -1;
                        }
                        dir<<=1;
                        if(dc_pred_dir) dir|=1;
                    }
                    s->cbp_table[xy]&= 3; //remove dquant
                    s->cbp_table[xy]|= cbpy<<2;
                    s->pred_dir_table[xy]= dir | (ac_pred<<7);
                    s->error_status_table[xy]&= ~DC_ERROR;
                }else if(s->mb_type[xy]&MB_TYPE_SKIPED){
                    s->current_picture.qscale_table[xy]= s->qscale;
                    s->cbp_table[xy]= 0;
                }else{
                    int cbpy = get_vlc2(&s->gb, cbpy_vlc.table, CBPY_VLC_BITS, 1);

                    if(cbpy<0){
                        fprintf(stderr, "P cbpy corrupted at %d %d\n", s->mb_x, s->mb_y);
                        return -1;
                    }
                    
                    if(s->cbp_table[xy] & 8) {
                        change_qscale(s, quant_tab[get_bits(&s->gb, 2)]);
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
    
    mb_num= mpeg4_decode_partition_a(s);    
    if(mb_num<0)
        return -1;
    
    if(s->resync_mb_x + s->resync_mb_y*s->mb_width + mb_num > s->mb_num){
        fprintf(stderr, "slice below monitor ...\n");
        return -1;
    }

    s->mb_num_left= mb_num;
        
    if(s->pict_type==I_TYPE){
        if(get_bits(&s->gb, 19)!=DC_MARKER){
            fprintf(stderr, "marker missing after first I partition at %d %d\n", s->mb_x, s->mb_y);
            return -1;
        }else
            s->error_status_table[s->mb_x + s->mb_y*s->mb_width-1]|= MV_END|DC_END;
    }else{
        if(get_bits(&s->gb, 17)!=MOTION_MARKER){
            fprintf(stderr, "marker missing after first P partition at %d %d\n", s->mb_x, s->mb_y);
            return -1;
        }else
            s->error_status_table[s->mb_x + s->mb_y*s->mb_width-1]|= MV_END;
    }
    
    if( mpeg4_decode_partition_b(s, mb_num) < 0){
        return -1;
    }
    
    s->error_status_table[s->mb_x + s->mb_y*s->mb_width-1]|= DC_END;

    return 0;        
}

/**
 * decode partition C of one MB.
 * @return <0 if an error occured
 */
static int mpeg4_decode_partitioned_mb(MpegEncContext *s, DCTELEM block[6][64])
{
    int cbp, mb_type;
    const int xy= s->mb_x + s->mb_y*s->mb_width;

    mb_type= s->mb_type[xy];
    cbp = s->cbp_table[xy];

    if(s->current_picture.qscale_table[xy] != s->qscale){
        s->qscale= s->current_picture.qscale_table[xy];
        s->y_dc_scale= s->y_dc_scale_table[ s->qscale ];
        s->c_dc_scale= s->c_dc_scale_table[ s->qscale ];
    }
    
    if (s->pict_type == P_TYPE || s->pict_type==S_TYPE) {
        int i;
        for(i=0; i<4; i++){
            s->mv[0][i][0] = s->motion_val[ s->block_index[i] ][0];
            s->mv[0][i][1] = s->motion_val[ s->block_index[i] ][1];
        }
        s->mb_intra = mb_type&MB_TYPE_INTRA;

        if (mb_type&MB_TYPE_SKIPED) {
            /* skip mb */
            for(i=0;i<6;i++)
                s->block_last_index[i] = -1;
            s->mv_dir = MV_DIR_FORWARD;
            s->mv_type = MV_TYPE_16X16;
            if(s->pict_type==S_TYPE && s->vol_sprite_usage==GMC_SPRITE){
                s->mcsel=1;
                s->mb_skiped = 0;
            }else{
                s->mcsel=0;
                s->mb_skiped = 1;
            }
        }else if(s->mb_intra){
            s->ac_pred = s->pred_dir_table[xy]>>7;

            /* decode each block */
            for (i = 0; i < 6; i++) {
                if(mpeg4_decode_block(s, block[i], i, cbp&32, 1) < 0){
                    fprintf(stderr, "texture corrupted at %d %d\n", s->mb_x, s->mb_y);
                    return -1;
                }
                cbp+=cbp;
            }
        }else if(!s->mb_intra){
//            s->mcsel= 0; //FIXME do we need to init that
            
            s->mv_dir = MV_DIR_FORWARD;
            if (mb_type&MB_TYPE_INTER4V) {
                s->mv_type = MV_TYPE_8X8;
            } else {
                s->mv_type = MV_TYPE_16X16;
            }
            /* decode each block */
            for (i = 0; i < 6; i++) {
                if(mpeg4_decode_block(s, block[i], i, cbp&32, 0) < 0){
                    fprintf(stderr, "texture corrupted at %d %d (trying to continue with mc/dc only)\n", s->mb_x, s->mb_y);
                    return -1;
                }
                cbp+=cbp;
            }
        }
    } else { /* I-Frame */
        int i;
        s->mb_intra = 1;
        s->ac_pred = s->pred_dir_table[xy]>>7;
        
        /* decode each block */
        for (i = 0; i < 6; i++) {
            if(mpeg4_decode_block(s, block[i], i, cbp&32, 1) < 0){
                fprintf(stderr, "texture corrupted at %d %d (trying to continue with dc only)\n", s->mb_x, s->mb_y);
                return -1;
            }
            cbp+=cbp;
        }
    }

    s->error_status_table[xy]&= ~AC_ERROR;

    /* per-MB end of slice check */

    if(--s->mb_num_left <= 0){
//printf("%06X %d\n", show_bits(&s->gb, 24), s->gb.size_in_bits - get_bits_count(&s->gb));
        if(mpeg4_is_resync(s))
            return SLICE_END;
        else
            return SLICE_NOEND;     
    }else{
        if(s->cbp_table[xy+1] && mpeg4_is_resync(s))
            return SLICE_END;
        else
            return SLICE_OK;
    }
}

int ff_h263_decode_mb(MpegEncContext *s,
                      DCTELEM block[6][64])
{
    int cbpc, cbpy, i, cbp, pred_x, pred_y, mx, my, dquant;
    INT16 *mot_val;
    static INT8 quant_tab[4] = { -1, -2, 1, 2 };

    s->error_status_table[s->mb_x + s->mb_y*s->mb_width]= 0;

    if(s->mb_x==0) PRINT_MB_TYPE("\n");

    if (s->pict_type == P_TYPE || s->pict_type==S_TYPE) {
        if (get_bits1(&s->gb)) {
            /* skip mb */
            s->mb_intra = 0;
            for(i=0;i<6;i++)
                s->block_last_index[i] = -1;
            s->mv_dir = MV_DIR_FORWARD;
            s->mv_type = MV_TYPE_16X16;
            if(s->pict_type==S_TYPE && s->vol_sprite_usage==GMC_SPRITE){
                PRINT_MB_TYPE("G");
                s->mcsel=1;
                s->mv[0][0][0]= get_amv(s, 0);
                s->mv[0][0][1]= get_amv(s, 1);

                s->mb_skiped = 0;
            }else{
                PRINT_MB_TYPE("S");
                s->mcsel=0;
                s->mv[0][0][0] = 0;
                s->mv[0][0][1] = 0;
                s->mb_skiped = 1;
            }
            goto end;
        }
        cbpc = get_vlc2(&s->gb, inter_MCBPC_vlc.table, INTER_MCBPC_VLC_BITS, 2);
        //fprintf(stderr, "\tCBPC: %d", cbpc);
        if (cbpc < 0)
            return -1;
        if (cbpc > 20)
            cbpc+=3;
        else if (cbpc == 20)
            fprintf(stderr, "Stuffing !");
        
        dquant = cbpc & 8;
        s->mb_intra = ((cbpc & 4) != 0);
        if (s->mb_intra) goto intra;
        
        if(s->pict_type==S_TYPE && s->vol_sprite_usage==GMC_SPRITE && (cbpc & 16) == 0)
            s->mcsel= get_bits1(&s->gb);
        else s->mcsel= 0;
        cbpy = get_vlc2(&s->gb, cbpy_vlc.table, CBPY_VLC_BITS, 1);
        cbp = (cbpc & 3) | ((cbpy ^ 0xf) << 2);
        if (dquant) {
            change_qscale(s, quant_tab[get_bits(&s->gb, 2)]);
        }
        if((!s->progressive_sequence) && (cbp || (s->workaround_bugs&FF_BUG_XVID_ILACE)))
            s->interlaced_dct= get_bits1(&s->gb);
        
        s->mv_dir = MV_DIR_FORWARD;
        if ((cbpc & 16) == 0) {
            if(s->mcsel){
                PRINT_MB_TYPE("G");
                /* 16x16 global motion prediction */
                s->mv_type = MV_TYPE_16X16;
                mx= get_amv(s, 0);
                my= get_amv(s, 1);
                s->mv[0][0][0] = mx;
                s->mv[0][0][1] = my;
            }else if((!s->progressive_sequence) && get_bits1(&s->gb)){
                PRINT_MB_TYPE("f");
                /* 16x8 field motion prediction */
                s->mv_type= MV_TYPE_FIELD;

                s->field_select[0][0]= get_bits1(&s->gb);
                s->field_select[0][1]= get_bits1(&s->gb);

                h263_pred_motion(s, 0, &pred_x, &pred_y);
                
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
                PRINT_MB_TYPE("P");
                /* 16x16 motion prediction */
                s->mv_type = MV_TYPE_16X16;
                h263_pred_motion(s, 0, &pred_x, &pred_y);
                if (s->umvplus_dec)
                   mx = h263p_decode_umotion(s, pred_x);
                else
                   mx = h263_decode_motion(s, pred_x, s->f_code);
            
                if (mx >= 0xffff)
                    return -1;
            
                if (s->umvplus_dec)
                   my = h263p_decode_umotion(s, pred_y);
                else
                   my = h263_decode_motion(s, pred_y, s->f_code);
            
                if (my >= 0xffff)
                    return -1;
                s->mv[0][0][0] = mx;
                s->mv[0][0][1] = my;

                if (s->umvplus_dec && (mx - pred_x) == 1 && (my - pred_y) == 1)
                   skip_bits1(&s->gb); /* Bit stuffing to prevent PSC */                   
            }
        } else {
            PRINT_MB_TYPE("4");
            s->mv_type = MV_TYPE_8X8;
            for(i=0;i<4;i++) {
                mot_val = h263_pred_motion(s, i, &pred_x, &pred_y);
                if (s->umvplus_dec)
                  mx = h263p_decode_umotion(s, pred_x);
                else
                  mx = h263_decode_motion(s, pred_x, s->f_code);
                if (mx >= 0xffff)
                    return -1;
                
                if (s->umvplus_dec)
                  my = h263p_decode_umotion(s, pred_y);
                else    
                  my = h263_decode_motion(s, pred_y, s->f_code);
                if (my >= 0xffff)
                    return -1;
                s->mv[0][i][0] = mx;
                s->mv[0][i][1] = my;
                if (s->umvplus_dec && (mx - pred_x) == 1 && (my - pred_y) == 1)
                  skip_bits1(&s->gb); /* Bit stuffing to prevent PSC */
                mot_val[0] = mx;
                mot_val[1] = my;
            }
        }
    } else if(s->pict_type==B_TYPE) {
        int modb1; // first bit of modb
        int modb2; // second bit of modb
        int mb_type;
        int xy;

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
        s->mb_skiped= s->next_picture.mbskip_table[s->mb_y * s->mb_width + s->mb_x]; // Note, skiptab=0 if last was GMC

        if(s->mb_skiped){
                /* skip mb */
            for(i=0;i<6;i++)
                s->block_last_index[i] = -1;

            s->mv_dir = MV_DIR_FORWARD;
            s->mv_type = MV_TYPE_16X16;
            s->mv[0][0][0] = 0;
            s->mv[0][0][1] = 0;
            s->mv[1][0][0] = 0;
            s->mv[1][0][1] = 0;
            PRINT_MB_TYPE("s");
            goto end;
        }

        modb1= get_bits1(&s->gb); 
        if(modb1){
            mb_type=4; //like MB_TYPE_B_DIRECT but no vectors coded
            cbp=0;
        }else{
            int field_mv;
        
            modb2= get_bits1(&s->gb);
            mb_type= get_vlc2(&s->gb, mb_type_b_vlc.table, MB_TYPE_B_VLC_BITS, 1);
            if(modb2) cbp= 0;
            else      cbp= get_bits(&s->gb, 6);

            if (mb_type!=MB_TYPE_B_DIRECT && cbp) {
                if(get_bits1(&s->gb)){
                    change_qscale(s, get_bits1(&s->gb)*4 - 2);
                }
            }
            field_mv=0;

            if(!s->progressive_sequence){
                if(cbp)
                    s->interlaced_dct= get_bits1(&s->gb);

                if(mb_type!=MB_TYPE_B_DIRECT && get_bits1(&s->gb)){
                    field_mv=1;

                    if(mb_type!=MB_TYPE_B_BACKW){
                        s->field_select[0][0]= get_bits1(&s->gb);
                        s->field_select[0][1]= get_bits1(&s->gb);
                    }
                    if(mb_type!=MB_TYPE_B_FORW){
                        s->field_select[1][0]= get_bits1(&s->gb);
                        s->field_select[1][1]= get_bits1(&s->gb);
                    }
                }
            }

            s->mv_dir = 0;
            if(mb_type!=MB_TYPE_B_DIRECT && !field_mv){
                s->mv_type= MV_TYPE_16X16;
                if(mb_type!=MB_TYPE_B_BACKW){
                    s->mv_dir = MV_DIR_FORWARD;

                    mx = h263_decode_motion(s, s->last_mv[0][0][0], s->f_code);
                    my = h263_decode_motion(s, s->last_mv[0][0][1], s->f_code);
                    s->last_mv[0][1][0]= s->last_mv[0][0][0]= s->mv[0][0][0] = mx;
                    s->last_mv[0][1][1]= s->last_mv[0][0][1]= s->mv[0][0][1] = my;
                }
    
                if(mb_type!=MB_TYPE_B_FORW){
                    s->mv_dir |= MV_DIR_BACKWARD;

                    mx = h263_decode_motion(s, s->last_mv[1][0][0], s->b_code);
                    my = h263_decode_motion(s, s->last_mv[1][0][1], s->b_code);
                    s->last_mv[1][1][0]= s->last_mv[1][0][0]= s->mv[1][0][0] = mx;
                    s->last_mv[1][1][1]= s->last_mv[1][0][1]= s->mv[1][0][1] = my;
                }
                if(mb_type!=MB_TYPE_B_DIRECT)
                    PRINT_MB_TYPE(mb_type==MB_TYPE_B_FORW ? "F" : (mb_type==MB_TYPE_B_BACKW ? "B" : "T"));
            }else if(mb_type!=MB_TYPE_B_DIRECT){
                s->mv_type= MV_TYPE_FIELD;

                if(mb_type!=MB_TYPE_B_BACKW){
                    s->mv_dir = MV_DIR_FORWARD;
                
                    for(i=0; i<2; i++){
                        mx = h263_decode_motion(s, s->last_mv[0][i][0]  , s->f_code);
                        my = h263_decode_motion(s, s->last_mv[0][i][1]/2, s->f_code);
                        s->last_mv[0][i][0]=  s->mv[0][i][0] = mx;
                        s->last_mv[0][i][1]= (s->mv[0][i][1] = my)*2;
                    }
                }
    
                if(mb_type!=MB_TYPE_B_FORW){
                    s->mv_dir |= MV_DIR_BACKWARD;

                    for(i=0; i<2; i++){
                        mx = h263_decode_motion(s, s->last_mv[1][i][0]  , s->b_code);
                        my = h263_decode_motion(s, s->last_mv[1][i][1]/2, s->b_code);
                        s->last_mv[1][i][0]=  s->mv[1][i][0] = mx;
                        s->last_mv[1][i][1]= (s->mv[1][i][1] = my)*2;
                    }
                }
                if(mb_type!=MB_TYPE_B_DIRECT)
                    PRINT_MB_TYPE(mb_type==MB_TYPE_B_FORW ? "f" : (mb_type==MB_TYPE_B_BACKW ? "b" : "t"));
            }
        }
          
        if(mb_type==4 || mb_type==MB_TYPE_B_DIRECT){
            if(mb_type==4)
                mx=my=0;
            else{
                mx = h263_decode_motion(s, 0, 1);
                my = h263_decode_motion(s, 0, 1);
            }
 
            s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD | MV_DIRECT;
            ff_mpeg4_set_direct_mv(s, mx, my);
        }
        
        if(mb_type<0 || mb_type>4){
            printf("illegal MB_type\n");
            return -1;
        }
    } else { /* I-Frame */
        cbpc = get_vlc2(&s->gb, intra_MCBPC_vlc.table, INTRA_MCBPC_VLC_BITS, 1);
        if (cbpc < 0)
            return -1;
        dquant = cbpc & 4;
        s->mb_intra = 1;
intra:
        s->ac_pred = 0;
        if (s->h263_pred || s->h263_aic) {
            s->ac_pred = get_bits1(&s->gb);
            if (s->ac_pred && s->h263_aic)
                s->h263_aic_dir = get_bits1(&s->gb);
        }
        PRINT_MB_TYPE(s->ac_pred ? "A" : "I");
        
        cbpy = get_vlc2(&s->gb, cbpy_vlc.table, CBPY_VLC_BITS, 1);
        if(cbpy<0) return -1;
        cbp = (cbpc & 3) | (cbpy << 2);
        if (dquant) {
            change_qscale(s, quant_tab[get_bits(&s->gb, 2)]);
        }
        
        if(!s->progressive_sequence)
            s->interlaced_dct= get_bits1(&s->gb);

        /* decode each block */
        if (s->h263_pred) {
            for (i = 0; i < 6; i++) {
                if (mpeg4_decode_block(s, block[i], i, cbp&32, 1) < 0)
                    return -1;
                cbp+=cbp;
            }
        } else {
            for (i = 0; i < 6; i++) {
                if (h263_decode_block(s, block[i], i, cbp&32) < 0)
                    return -1;
                cbp+=cbp;
            }
        }
        goto end;
    }

    /* decode each block */
    if (s->h263_pred) {
        for (i = 0; i < 6; i++) {
            if (mpeg4_decode_block(s, block[i], i, cbp&32, 0) < 0)
                return -1;
            cbp+=cbp;
        }
    } else {
        for (i = 0; i < 6; i++) {
            if (h263_decode_block(s, block[i], i, cbp&32) < 0)
                return -1;
            cbp+=cbp;
        }
    }
end:

        /* per-MB end of slice check */
    if(s->codec_id==CODEC_ID_MPEG4){
        if(mpeg4_is_resync(s)){
            if(s->pict_type==B_TYPE && s->next_picture.mbskip_table[s->mb_y * s->mb_width + s->mb_x+1])
                return SLICE_OK;
            return SLICE_END;
        }
    }else{
        int v= show_bits(&s->gb, 16);
    
        if(get_bits_count(&s->gb) + 16 > s->gb.size_in_bits){
            v>>= get_bits_count(&s->gb) + 16 - s->gb.size_in_bits;
        }

        if(v==0)
            return SLICE_END;
    }

    return SLICE_OK;     
}

static int h263_decode_motion(MpegEncContext * s, int pred, int f_code)
{
    int code, val, sign, shift, l;
    code = get_vlc2(&s->gb, mv_vlc.table, MV_VLC_BITS, 2);
    if (code < 0)
        return 0xffff;

    if (code == 0)
        return pred;

    sign = get_bits1(&s->gb);
    shift = f_code - 1;
    val = (code - 1) << shift;
    if (shift > 0)
        val |= get_bits(&s->gb, shift);
    val++;
    if (sign)
        val = -val;
    val += pred;

    /* modulo decoding */
    if (!s->h263_long_vectors) {
        l = 1 << (f_code + 4);
        if (val < -l) {
            val += l<<1;
        } else if (val >= l) {
            val -= l<<1;
        }
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
   fprintf(stderr,"H.263+ UMV Motion = %d\n", code);
#endif
   return code;   

}

static int h263_decode_block(MpegEncContext * s, DCTELEM * block,
                             int n, int coded)
{
    int code, level, i, j, last, run;
    RLTable *rl = &rl_inter;
    const UINT8 *scan_table;

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
        if (s->h263_rv10 && s->rv10_version == 3 && s->pict_type == I_TYPE) {
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

    for(;;) {
        code = get_vlc2(&s->gb, rl->vlc.table, TEX_VLC_BITS, 2);
        if (code < 0){
            fprintf(stderr, "illegal ac vlc code at %dx%d\n", s->mb_x, s->mb_y);
            return -1;
        }
        if (code == rl->n) {
            /* escape */
            last = get_bits1(&s->gb);
            run = get_bits(&s->gb, 6);
            level = (INT8)get_bits(&s->gb, 8);
            if (s->h263_rv10 && level == -128) {
                /* XXX: should patch encoder too */
                level = get_bits(&s->gb, 12);
		level= (level + ((-1)<<11)) ^ ((-1)<<11); //sign extension
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
            fprintf(stderr, "run overflow at %dx%d\n", s->mb_x, s->mb_y);
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
    int level, pred, code;
    UINT16 *dc_val;

    if (n < 4) 
        code = get_vlc2(&s->gb, dc_lum.table, DC_VLC_BITS, 1);
    else 
        code = get_vlc2(&s->gb, dc_chrom.table, DC_VLC_BITS, 1);
    if (code < 0 || code > 9 /* && s->nbit<9 */){
        fprintf(stderr, "illegal dc vlc\n");
        return -1;
    }
    if (code == 0) {
        level = 0;
    } else {
        level = get_bits(&s->gb, code);
        if ((level >> (code - 1)) == 0) /* if MSB not set it is negative*/
            level = - (level ^ ((1 << code) - 1));
        if (code > 8){
            if(get_bits1(&s->gb)==0){ /* marker */
                if(s->error_resilience>=2){
                    fprintf(stderr, "dc marker bit missing\n");
                    return -1;
                }
            }
        }
    }
    pred = ff_mpeg4_pred_dc(s, n, &dc_val, dir_ptr);
    level += pred;
    if (level < 0){
        if(s->error_resilience>=3){
            fprintf(stderr, "dc<0 at %dx%d\n", s->mb_x, s->mb_y);
            return -1;
        }
        level = 0;
    }
    if (n < 4) {
        *dc_val = level * s->y_dc_scale;
    } else {
        *dc_val = level * s->c_dc_scale;
    }
    if(s->error_resilience>=3){
        if(*dc_val > 2048 + s->y_dc_scale + s->c_dc_scale){
            fprintf(stderr, "dc overflow at %dx%d\n", s->mb_x, s->mb_y);
            return -1;
        }
    }
    return level;
}

/**
 * decodes a block.
 * @return <0 if an error occured
 */
static inline int mpeg4_decode_block(MpegEncContext * s, DCTELEM * block,
                              int n, int coded, int intra)
{
    int level, i, last, run;
    int dc_pred_dir;
    RLTable * rl;
    RL_VLC_ELEM * rl_vlc;
    const UINT8 * scan_table;
    int qmul, qadd;

    if(intra) {
	/* DC coef */
        if(s->partitioned_frame){
            level = s->dc_val[0][ s->block_index[n] ];
            if(n<4) level= (level + (s->y_dc_scale>>1))/s->y_dc_scale; //FIXME optimizs
            else    level= (level + (s->c_dc_scale>>1))/s->c_dc_scale;
            dc_pred_dir= (s->pred_dir_table[s->mb_x + s->mb_y*s->mb_width]<<n)&32;
        }else{
            level = mpeg4_decode_dc(s, n, &dc_pred_dir);
            if (level < 0)
                return -1;
        }
        block[0] = level;
        i = 0;
        if (!coded) 
            goto not_coded;
        rl = &rl_intra;
        rl_vlc = rl_intra.rl_vlc[0];
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
        rl = &rl_inter;
   
        scan_table = s->intra_scantable.permutated;

        if(s->mpeg_quant){
            qmul=1;
            qadd=0;
            rl_vlc = rl_inter.rl_vlc[0];        
        }else{
            qmul = s->qscale << 1;
            qadd = (s->qscale - 1) | 1;
            rl_vlc = rl_inter.rl_vlc[s->qscale];
        }
    }
  {
    OPEN_READER(re, &s->gb);
    for(;;) {
        UPDATE_CACHE(re, &s->gb);
        GET_RL_VLC(level, run, re, &s->gb, rl_vlc, TEX_VLC_BITS, 2);
        if (level==0) {
            int cache;
            cache= GET_CACHE(re, &s->gb);
            /* escape */
            if (cache&0x80000000) {
                if (cache&0x40000000) {
                    /* third escape */
                    SKIP_CACHE(re, &s->gb, 2);
                    last=  SHOW_UBITS(re, &s->gb, 1); SKIP_CACHE(re, &s->gb, 1);
                    run=   SHOW_UBITS(re, &s->gb, 6); LAST_SKIP_CACHE(re, &s->gb, 6);
                    SKIP_COUNTER(re, &s->gb, 2+1+6);
                    UPDATE_CACHE(re, &s->gb);

                    if(SHOW_UBITS(re, &s->gb, 1)==0){
                        fprintf(stderr, "1. marker bit missing in 3. esc\n");
                        return -1;
                    }; SKIP_CACHE(re, &s->gb, 1);
                    
                    level= SHOW_SBITS(re, &s->gb, 12); SKIP_CACHE(re, &s->gb, 12);
 
                    if(SHOW_UBITS(re, &s->gb, 1)==0){
                        fprintf(stderr, "2. marker bit missing in 3. esc\n");
                        return -1;
                    }; LAST_SKIP_CACHE(re, &s->gb, 1);
                    
                    SKIP_COUNTER(re, &s->gb, 1+12+1);
                    
                    if(level*s->qscale>1024 || level*s->qscale<-1024){
                        fprintf(stderr, "|level| overflow in 3. esc, qp=%d\n", s->qscale);
                        return -1;
                    }
#if 1 
                    {
                        const int abs_level= ABS(level);
                        if(abs_level<=MAX_LEVEL && run<=MAX_RUN && !(s->workaround_bugs&FF_BUG_AC_VLC)){
                            const int run1= run - rl->max_run[last][abs_level] - 1;
                            if(abs_level <= rl->max_level[last][run]){
                                fprintf(stderr, "illegal 3. esc, vlc encoding possible\n");
                                return -1;
                            }
                            if(s->error_resilience > FF_ER_COMPLIANT){
                                if(abs_level <= rl->max_level[last][run]*2){
                                    fprintf(stderr, "illegal 3. esc, esc 1 encoding possible\n");
                                    return -1;
                                }
                                if(run1 >= 0 && abs_level <= rl->max_level[last][run1]){
                                    fprintf(stderr, "illegal 3. esc, esc 2 encoding possible\n");
                                    return -1;
                                }
                            }
                        }
                    }
#endif
		    if (level>0) level= level * qmul + qadd;
                    else         level= level * qmul - qadd;

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
                    GET_RL_VLC(level, run, re, &s->gb, rl_vlc, TEX_VLC_BITS, 2);
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
                GET_RL_VLC(level, run, re, &s->gb, rl_vlc, TEX_VLC_BITS, 2);
                i+= run;
                level = level + rl->max_level[run>>7][(run-1)&63] * qmul;//FIXME opt indexing
                level = (level ^ SHOW_SBITS(re, &s->gb, 1)) - SHOW_SBITS(re, &s->gb, 1);
                LAST_SKIP_BITS(re, &s->gb, 1);
            }
        } else {
            i+= run;
            level = (level ^ SHOW_SBITS(re, &s->gb, 1)) - SHOW_SBITS(re, &s->gb, 1);
            LAST_SKIP_BITS(re, &s->gb, 1);
        }
        if (i > 62){
            i-= 192;
            if(i&(~63)){
                fprintf(stderr, "ac-tex damaged at %d %d\n", s->mb_x, s->mb_y);
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
    if (s->mb_intra) {
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
    int format, width, height;

    /* picture start code */
    if (get_bits(&s->gb, 22) != 0x20) {
        fprintf(stderr, "Bad picture start code\n");
        return -1;
    }
    /* temporal reference */
    s->picture_number = get_bits(&s->gb, 8); /* picture timestamp */

    /* PTYPE starts here */    
    if (get_bits1(&s->gb) != 1) {
        /* marker */
        fprintf(stderr, "Bad marker\n");
        return -1;
    }
    if (get_bits1(&s->gb) != 0) {
        fprintf(stderr, "Bad H263 id\n");
        return -1;	/* h263 id */
    }
    skip_bits1(&s->gb);	/* split screen off */
    skip_bits1(&s->gb);	/* camera  off */
    skip_bits1(&s->gb);	/* freeze picture release off */

    /* Reset GOB number */
    s->gob_number = 0;
        
    format = get_bits(&s->gb, 3);
    /*
        0    forbidden
        1    sub-QCIF
        10   QCIF
        7	extended PTYPE (PLUSPTYPE)
    */

    if (format != 7 && format != 6) {
        s->h263_plus = 0;
        /* H.263v1 */
        width = h263_format[format][0];
        height = h263_format[format][1];
        if (!width)
            return -1;
        
        s->width = width;
        s->height = height;
        s->pict_type = I_TYPE + get_bits1(&s->gb);

        s->unrestricted_mv = get_bits1(&s->gb); 
        s->h263_long_vectors = s->unrestricted_mv;

        if (get_bits1(&s->gb) != 0) {
            fprintf(stderr, "H263 SAC not supported\n");
            return -1;	/* SAC: off */
        }
        if (get_bits1(&s->gb) != 0) {
            s->mv_type = MV_TYPE_8X8; /* Advanced prediction mode */
        }   
        
        if (get_bits1(&s->gb) != 0) {
            fprintf(stderr, "H263 PB frame not supported\n");
            return -1;	/* not PB frame */
        }
        s->qscale = get_bits(&s->gb, 5);
        skip_bits1(&s->gb);	/* Continuous Presence Multipoint mode: off */
    } else {
        int ufep;
        
        /* H.263v2 */
        s->h263_plus = 1;
        ufep = get_bits(&s->gb, 3); /* Update Full Extended PTYPE */

        /* ufep other than 0 and 1 are reserved */        
        if (ufep == 1) {
            /* OPPTYPE */       
            format = get_bits(&s->gb, 3);
            dprintf("ufep=1, format: %d\n", format);
            skip_bits(&s->gb,1); /* Custom PCF */
            s->umvplus_dec = get_bits(&s->gb, 1); /* Unrestricted Motion Vector */
            skip_bits1(&s->gb); /* Syntax-based Arithmetic Coding (SAC) */
            if (get_bits1(&s->gb) != 0) {
                s->mv_type = MV_TYPE_8X8; /* Advanced prediction mode */
            }
            if (get_bits1(&s->gb) != 0) { /* Advanced Intra Coding (AIC) */
                s->h263_aic = 1;
            }
	    
            skip_bits(&s->gb, 7);
            /* these are the 7 bits: (in order of appearence  */
            /* Deblocking Filter */
            /* Slice Structured */
            /* Reference Picture Selection */
            /* Independent Segment Decoding */
            /* Alternative Inter VLC */
            /* Modified Quantization */
            /* Prevent start code emulation */

            skip_bits(&s->gb, 3); /* Reserved */
        } else if (ufep != 0) {
            fprintf(stderr, "Bad UFEP type (%d)\n", ufep);
            return -1;
        }
            
        /* MPPTYPE */
        s->pict_type = get_bits(&s->gb, 3) + I_TYPE;
        dprintf("pict_type: %d\n", s->pict_type);
        if (s->pict_type != I_TYPE &&
            s->pict_type != P_TYPE)
            return -1;
        skip_bits(&s->gb, 2);
        s->no_rounding = get_bits1(&s->gb);
        dprintf("RTYPE: %d\n", s->no_rounding);
        skip_bits(&s->gb, 4);
        
        /* Get the picture dimensions */
        if (ufep) {
            if (format == 6) {
                /* Custom Picture Format (CPFMT) */
                s->aspect_ratio_info = get_bits(&s->gb, 4);
                dprintf("aspect: %d\n", s->aspect_ratio_info);
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
                dprintf("\nH.263+ Custom picture: %dx%d\n",width,height);
                if (s->aspect_ratio_info == FF_ASPECT_EXTENDED) {
                    /* aspected dimensions */
		    s->aspected_width = get_bits(&s->gb, 8);
		    s->aspected_height = get_bits(&s->gb, 8);
                }else{
                    s->aspected_width = pixel_aspect[s->aspect_ratio_info][0];
                    s->aspected_height= pixel_aspect[s->aspect_ratio_info][1];
                }
            } else {
                width = h263_format[format][0];
                height = h263_format[format][1];
            }
            if ((width == 0) || (height == 0))
                return -1;
            s->width = width;
            s->height = height;
            if (s->umvplus_dec) {
                skip_bits1(&s->gb); /* Unlimited Unrestricted Motion Vectors Indicator (UUI) */
            }
        }
            
        s->qscale = get_bits(&s->gb, 5);
    }
    /* PEI */
    while (get_bits1(&s->gb) != 0) {
        skip_bits(&s->gb, 8);
    }
    s->f_code = 1;
    
    if(s->h263_aic){
         s->y_dc_scale_table= 
         s->c_dc_scale_table= h263_aic_dc_scale_table;
    }else{
        s->y_dc_scale_table=
        s->c_dc_scale_table= ff_mpeg1_dc_scale_table;
    }

    return 0;
}

static void mpeg4_decode_sprite_trajectory(MpegEncContext * s)
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

        length= get_vlc(&s->gb, &sprite_trajectory);
        if(length){
            x= get_bits(&s->gb, length);

            if ((x >> (length - 1)) == 0) /* if MSB not set it is negative*/
                x = - (x ^ ((1 << length) - 1));
        }
        if(!(s->divx_version==500 && s->divx_build==413)) skip_bits1(&s->gb); /* marker bit */
        
        length= get_vlc(&s->gb, &sprite_trajectory);
        if(length){
            y=get_bits(&s->gb, length);

            if ((y >> (length - 1)) == 0) /* if MSB not set it is negative*/
                y = - (y ^ ((1 << length) - 1));
        }
        skip_bits1(&s->gb); /* marker bit */
//printf("%d %d %d %d\n", x, y, i, s->sprite_warping_accuracy);
        d[i][0]= x;
        d[i][1]= y;
    }

    while((1<<alpha)<w) alpha++;
    while((1<<beta )<h) beta++; // there seems to be a typo in the mpeg4 std for the definition of w' and h'
    w2= 1<<alpha;
    h2= 1<<beta;

// Note, the 4th point isnt used for GMC
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
        s->aspected_width = get_bits(gb, 8); // par_width
        s->aspected_height = get_bits(gb, 8); // par_height
    }else{
        s->aspected_width = pixel_aspect[s->aspect_ratio_info][0];
        s->aspected_height= pixel_aspect[s->aspect_ratio_info][1];
    }

    if ((s->vol_control_parameters=get_bits1(gb))) { /* vol control parameter */
        int chroma_format= get_bits(gb, 2);
        if(chroma_format!=1){
            printf("illegal chroma format\n");
        }
        s->low_delay= get_bits1(gb);
        if(get_bits1(gb)){ /* vbv parameters */
            get_bits(gb, 15);	/* first_half_bitrate */
            skip_bits1(gb);	/* marker */
            get_bits(gb, 15);	/* latter_half_bitrate */
            skip_bits1(gb);	/* marker */
            get_bits(gb, 15);	/* first_half_vbv_buffer_size */
            skip_bits1(gb);	/* marker */
            get_bits(gb, 3);	/* latter_half_vbv_buffer_size */
            get_bits(gb, 11);	/* first_half_vbv_occupancy */
            skip_bits1(gb);	/* marker */
            get_bits(gb, 15);	/* latter_half_vbv_occupancy */
            skip_bits1(gb);	/* marker */               
        }
    }else{
        // set low delay flag only once so the smart? low delay detection wont be overriden
        if(s->picture_number==0)
            s->low_delay=0;
    }

    s->shape = get_bits(gb, 2); /* vol shape */
    if(s->shape != RECT_SHAPE) printf("only rectangular vol supported\n");
    if(s->shape == GRAY_SHAPE && vo_ver_id != 1){
        printf("Gray shape not supported\n");
        skip_bits(gb, 4);  //video_object_layer_shape_extension
    }

    skip_bits1(gb);   /* marker */
    
    s->time_increment_resolution = get_bits(gb, 16);
    
    s->time_increment_bits = av_log2(s->time_increment_resolution - 1) + 1;
    if (s->time_increment_bits < 1)
        s->time_increment_bits = 1;
    skip_bits1(gb);   /* marker */

    if (get_bits1(gb) != 0) {   /* fixed_vop_rate  */
        skip_bits(gb, s->time_increment_bits);
    }

    if (s->shape != BIN_ONLY_SHAPE) {
        if (s->shape == RECT_SHAPE) {
            skip_bits1(gb);   /* marker */
            width = get_bits(gb, 13);
            skip_bits1(gb);   /* marker */
            height = get_bits(gb, 13);
            skip_bits1(gb);   /* marker */
            if(width && height){ /* they should be non zero but who knows ... */
                s->width = width;
                s->height = height;
//                printf("width/height: %d %d\n", width, height);
            }
        }
        
        s->progressive_sequence= get_bits1(gb)^1;
        if(!get_bits1(gb)) printf("OBMC not supported (very likely buggy encoder)\n");   /* OBMC Disable */
        if (vo_ver_id == 1) {
            s->vol_sprite_usage = get_bits1(gb); /* vol_sprite_usage */
        } else {
            s->vol_sprite_usage = get_bits(gb, 2); /* vol_sprite_usage */
        }
        if(s->vol_sprite_usage==STATIC_SPRITE) printf("Static Sprites not supported\n");
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
            s->sprite_warping_accuracy = get_bits(gb, 2);
            s->sprite_brightness_change= get_bits1(gb);
            if(s->vol_sprite_usage==STATIC_SPRITE)
                s->low_latency_sprite= get_bits1(gb);            
        }
        // FIXME sadct disable bit if verid!=1 && shape not rect
        
        if (get_bits1(gb) == 1) {   /* not_8_bit */
            s->quant_precision = get_bits(gb, 4); /* quant_precision */
            if(get_bits(gb, 4)!=8) printf("N-bit not supported\n"); /* bits_per_pixel */
            if(s->quant_precision!=5) printf("quant precission %d\n", s->quant_precision);
        } else {
            s->quant_precision = 5;
        }
        
        // FIXME a bunch of grayscale shape things

        if((s->mpeg_quant=get_bits1(gb))){ /* vol_quant_type */
            int i, j, v;
            
            /* load default matrixes */
            for(i=0; i<64; i++){
                int j= s->idct_permutation[i];
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
                    v= get_bits(gb, 8);
                    if(v==0) break;
                    
                    last= v;
                    j= s->idct_permutation[ ff_zigzag_direct[i] ];
                    s->intra_matrix[j]= v;
                    s->chroma_intra_matrix[j]= v;
                }

                /* replicate last value */
                for(; i<64; i++){
                    j= s->idct_permutation[ ff_zigzag_direct[i] ];
                    s->intra_matrix[j]= v;
                    s->chroma_intra_matrix[j]= v;
                }
            }

            /* load custom non intra matrix */
            if(get_bits1(gb)){
                int last=0;
                for(i=0; i<64; i++){
                    v= get_bits(gb, 8);
                    if(v==0) break;

                    last= v;
                    j= s->idct_permutation[ ff_zigzag_direct[i] ];
                    s->inter_matrix[j]= v;
                    s->chroma_inter_matrix[j]= v;
                }

                /* replicate last value */
                for(; i<64; i++){
                    j= s->idct_permutation[ ff_zigzag_direct[i] ];
                    s->inter_matrix[j]= last;
                    s->chroma_inter_matrix[j]= last;
                }
            }

            // FIXME a bunch of grayscale shape things
        }

        if(vo_ver_id != 1)
             s->quarter_sample= get_bits1(gb);
        else s->quarter_sample=0;

        if(!get_bits1(gb)) printf("Complexity estimation not supported\n");

        s->resync_marker= !get_bits1(gb); /* resync_marker_disabled */

        s->data_partitioning= get_bits1(gb);
        if(s->data_partitioning){
            s->rvlc= get_bits1(gb);
            if(s->rvlc){
                printf("reversible vlc not supported\n");
            }
        }
        
        if(vo_ver_id != 1) {
            s->new_pred= get_bits1(gb);
            if(s->new_pred){
                printf("new pred not supported\n");
                skip_bits(gb, 2); /* requested upstream message type */
                skip_bits1(gb); /* newpred segment type */
            }
            s->reduced_res_vop= get_bits1(gb);
            if(s->reduced_res_vop) printf("reduced resolution VOP not supported\n");
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
                printf("scalability not supported\n");
            
            // bin shape stuff FIXME
        }
    }
    return 0;
}

/**
 * decodes the user data stuff in the header.
 * allso inits divx/xvid/lavc_version/build
 */
static int decode_user_data(MpegEncContext *s, GetBitContext *gb){
    char buf[256];
    int i;
    int e;
    int ver, build, ver2, ver3;

    buf[0]= show_bits(gb, 8);
    for(i=1; i<256; i++){
        buf[i]= show_bits(gb, 16)&0xFF;
        if(buf[i]==0) break;
        skip_bits(gb, 8);
    }
    buf[255]=0;
    
    /* divx detection */
    e=sscanf(buf, "DivX%dBuild%d", &ver, &build);
    if(e!=2)
        e=sscanf(buf, "DivX%db%d", &ver, &build);
    if(e==2){
        s->divx_version= ver;
        s->divx_build= build;
        if(s->picture_number==0){
            printf("This file was encoded with DivX%d Build%d\n", ver, build);
        }
    }
    
    /* ffmpeg detection */
    e=sscanf(buf, "FFmpeg%d.%d.%db%d", &ver, &ver2, &ver3, &build);
    if(e!=4)
        e=sscanf(buf, "FFmpeg v%d.%d.%d / libavcodec build: %d", &ver, &ver2, &ver3, &build); 
    if(e!=4){
        if(strcmp(buf, "ffmpeg")==0){
            s->ffmpeg_version= 0x000406;
            s->lavc_build= 4600;
        }
    }
    if(e==4){
        s->ffmpeg_version= ver*256*256 + ver2*256 + ver3;
        s->lavc_build= build;
        if(s->picture_number==0)
            printf("This file was encoded with libavcodec build %d\n", build);
    }
    
    /* xvid detection */
    e=sscanf(buf, "XviD%d", &build);
    if(e==1){
        s->xvid_build= build;
        if(s->picture_number==0)
            printf("This file was encoded with XviD build %d\n", build);
    }

//printf("User Data: %s\n", buf);
    return 0;
}

static int decode_vop_header(MpegEncContext *s, GetBitContext *gb){
    int time_incr, time_increment;

    s->pict_type = get_bits(gb, 2) + I_TYPE;	/* pict type: I = 0 , P = 1 */
    if(s->pict_type==B_TYPE && s->low_delay && s->vol_control_parameters==0){
        printf("low_delay flag set, but shouldnt, clearing it\n");
        s->low_delay=0;
    }
 
    s->partitioned_frame= s->data_partitioning && s->pict_type!=B_TYPE;
    if(s->partitioned_frame)
        s->decode_mb= mpeg4_decode_partitioned_mb;
    else
        s->decode_mb= ff_h263_decode_mb;

    if(s->time_increment_resolution==0){
        s->time_increment_resolution=1;
//        fprintf(stderr, "time_increment_resolution is illegal\n");
    }
    time_incr=0;
    while (get_bits1(gb) != 0) 
        time_incr++;

    check_marker(gb, "before time_increment");
    time_increment= get_bits(gb, s->time_increment_bits);
//printf(" type:%d modulo_time_base:%d increment:%d\n", s->pict_type, time_incr, time_increment);
    if(s->pict_type!=B_TYPE){
        s->last_time_base= s->time_base;
        s->time_base+= time_incr;
        s->time= s->time_base*s->time_increment_resolution + time_increment;
        if(s->workaround_bugs&FF_BUG_UMP4){
            if(s->time < s->last_non_b_time){
//                fprintf(stderr, "header is not mpeg4 compatible, broken encoder, trying to workaround\n");
                s->time_base++;
                s->time+= s->time_increment_resolution;
            }
        }
        s->pp_time= s->time - s->last_non_b_time;
        s->last_non_b_time= s->time;
    }else{
        s->time= (s->last_time_base + time_incr)*s->time_increment_resolution + time_increment;
        s->pb_time= s->pp_time - (s->last_non_b_time - s->time);
        if(s->pp_time <=s->pb_time || s->pp_time <= s->pp_time - s->pb_time || s->pp_time<=0){
//            printf("messed up order, seeking?, skiping current b frame\n");
            return FRAME_SKIPED;
        }
        
        if(s->t_frame==0) s->t_frame= s->time - s->last_time_base;
        if(s->t_frame==0) s->t_frame=1; // 1/0 protection
//printf("%Ld %Ld %d %d\n", s->last_non_b_time, s->time, s->pp_time, s->t_frame); fflush(stdout);
        s->pp_field_time= (  ROUNDED_DIV(s->last_non_b_time, s->t_frame) 
                           - ROUNDED_DIV(s->last_non_b_time - s->pp_time, s->t_frame))*2;
        s->pb_field_time= (  ROUNDED_DIV(s->time, s->t_frame) 
                           - ROUNDED_DIV(s->last_non_b_time - s->pp_time, s->t_frame))*2;
    }
    
    s->current_picture.pts= s->time*1000LL*1000LL / s->time_increment_resolution;
    if(s->avctx->debug&FF_DEBUG_PTS)
        printf("MPEG4 PTS: %f\n", s->current_picture.pts/(1000.0*1000.0));
    
    if(check_marker(gb, "before vop_coded")==0 && s->picture_number==0){
        printf("hmm, seems the headers arnt complete, trying to guess time_increment_bits\n");
        for(s->time_increment_bits++ ;s->time_increment_bits<16; s->time_increment_bits++){
            if(get_bits1(gb)) break;
        }
        printf("my guess is %d bits ;)\n",s->time_increment_bits);
    }
    /* vop coded */
    if (get_bits1(gb) != 1){
        printf("vop not coded\n");
        return FRAME_SKIPED;
    }
//printf("time %d %d %d || %Ld %Ld %Ld\n", s->time_increment_bits, s->time_increment_resolution, s->time_base,
//s->time, s->last_non_b_time, s->last_non_b_time - s->pp_time);  
    if (s->shape != BIN_ONLY_SHAPE && ( s->pict_type == P_TYPE
                          || (s->pict_type == S_TYPE && s->vol_sprite_usage==GMC_SPRITE))) {
        /* rounding type for motion estimation */
	s->no_rounding = get_bits1(gb);
    } else {
	s->no_rounding = 0;
    }
//FIXME reduced res stuff

     if (s->shape != RECT_SHAPE) {
         if (s->vol_sprite_usage != 1 || s->pict_type != I_TYPE) {
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
         int t;
         t=get_bits(gb, 3); /* intra dc VLC threshold */
//printf("threshold %d\n", t);
         if(!s->progressive_sequence){
             s->top_field_first= get_bits1(gb);
             s->alternate_scan= get_bits1(gb);
         }else
             s->alternate_scan= 0;
     }

     if(s->alternate_scan){
         ff_init_scantable(s, &s->inter_scantable  , ff_alternate_vertical_scan);
         ff_init_scantable(s, &s->intra_scantable  , ff_alternate_vertical_scan);
         ff_init_scantable(s, &s->intra_h_scantable, ff_alternate_vertical_scan);
         ff_init_scantable(s, &s->intra_v_scantable, ff_alternate_vertical_scan);
     } else{
         ff_init_scantable(s, &s->inter_scantable  , ff_zigzag_direct);
         ff_init_scantable(s, &s->intra_scantable  , ff_zigzag_direct);
         ff_init_scantable(s, &s->intra_h_scantable, ff_alternate_horizontal_scan);
         ff_init_scantable(s, &s->intra_v_scantable, ff_alternate_vertical_scan);
     }
 
     if(s->pict_type == S_TYPE && (s->vol_sprite_usage==STATIC_SPRITE || s->vol_sprite_usage==GMC_SPRITE)){
         mpeg4_decode_sprite_trajectory(s);
         if(s->sprite_brightness_change) printf("sprite_brightness_change not supported\n");
         if(s->vol_sprite_usage==STATIC_SPRITE) printf("static sprite not supported\n");
     }

     if (s->shape != BIN_ONLY_SHAPE) {
         s->qscale = get_bits(gb, s->quant_precision);
         if(s->qscale==0){
             printf("Error, header damaged or not MPEG4 header (qscale=0)\n");
             return -1; // makes no sense to continue, as there is nothing left from the image then
         }
  
         if (s->pict_type != I_TYPE) {
             s->f_code = get_bits(gb, 3);	/* fcode_for */
             if(s->f_code==0){
                 printf("Error, header damaged or not MPEG4 header (f_code=0)\n");
                 return -1; // makes no sense to continue, as the MV decoding will break very quickly
             }
         }else
             s->f_code=1;
     
         if (s->pict_type == B_TYPE) {
             s->b_code = get_bits(gb, 3);
         }else
             s->b_code=1;

         if(s->avctx->debug&FF_DEBUG_PICT_INFO){
             printf("qp:%d fc:%d,%d %s size:%d pro:%d alt:%d top:%d %spel part:%d resync:%d w:%d a:%d\n", 
                 s->qscale, s->f_code, s->b_code, 
                 s->pict_type == I_TYPE ? "I" : (s->pict_type == P_TYPE ? "P" : (s->pict_type == B_TYPE ? "B" : "S")), 
                 gb->size_in_bits,s->progressive_sequence, s->alternate_scan, s->top_field_first, 
                 s->quarter_sample ? "q" : "h", s->data_partitioning, s->resync_marker, s->num_sprite_warping_points,
                 s->sprite_warping_accuracy); 
         }

         if(!s->scalability){
             if (s->shape!=RECT_SHAPE && s->pict_type!=I_TYPE) {
                 skip_bits1(gb); // vop shape coding type
             }
         }else{
             if(s->enhancement_type){
                 int load_backward_shape= get_bits1(gb);
                 if(load_backward_shape){
                     printf("load backward shape isnt supported\n");
                 }
             }
             skip_bits(gb, 2); //ref_select_code
         }
     }
     /* detect buggy encoders which dont set the low_delay flag (divx4/xvid/opendivx)*/
     // note we cannot detect divx5 without b-frames easyly (allthough its buggy too)
     if(s->vo_type==0 && s->vol_control_parameters==0 && s->divx_version==0 && s->picture_number==0){
         printf("looks like this file was encoded with (divx4/(old)xvid/opendivx) -> forcing low_delay flag\n");
         s->low_delay=1;
     }

     s->picture_number++; // better than pic number==0 allways ;)

     s->y_dc_scale_table= ff_mpeg4_y_dc_scale_table; //FIXME add short header support 
     s->c_dc_scale_table= ff_mpeg4_c_dc_scale_table;

     if(s->divx_version==0 || s->divx_version < 500){
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
    startcode = 0xff;
    for(;;) {
        v = get_bits(gb, 8);
        startcode = ((startcode << 8) | v) & 0xffffffff;
        
        if(get_bits_count(gb) >= gb->size_in_bits){
            if(gb->size_in_bits==8 && s->divx_version){
                printf("frame skip %d\n", gb->size_in_bits);
                return FRAME_SKIPED; //divx bug
            }else
                return -1; //end of stream
        }

        if((startcode&0xFFFFFF00) != 0x100)
            continue; //no startcode
        
        if(s->avctx->debug&FF_DEBUG_STARTCODE){
            printf("startcode: %3X ", startcode);
            if     (startcode<=0x11F) printf("Video Object Start");
            else if(startcode<=0x12F) printf("Video Object Layer Start");
            else if(startcode<=0x13F) printf("Reserved");
            else if(startcode<=0x15F) printf("FGS bp start");
            else if(startcode<=0x1AF) printf("Reserved");
            else if(startcode==0x1B0) printf("Visual Object Seq Start");
            else if(startcode==0x1B1) printf("Visual Object Seq End");
            else if(startcode==0x1B2) printf("User Data");
            else if(startcode==0x1B3) printf("Group of VOP start");
            else if(startcode==0x1B4) printf("Video Session Error");
            else if(startcode==0x1B5) printf("Visual Object Start");
            else if(startcode==0x1B6) printf("Video Object Plane start");
            else if(startcode==0x1B7) printf("slice start");
            else if(startcode==0x1B8) printf("extension start");
            else if(startcode==0x1B9) printf("fgs start");
            else if(startcode==0x1BA) printf("FBA Object start");
            else if(startcode==0x1BB) printf("FBA Object Plane start");
            else if(startcode==0x1BC) printf("Mesh Object start");
            else if(startcode==0x1BD) printf("Mesh Object Plane start");
            else if(startcode==0x1BE) printf("Still Textutre Object start");
            else if(startcode==0x1BF) printf("Textutre Spatial Layer start");
            else if(startcode==0x1C0) printf("Textutre SNR Layer start");
            else if(startcode==0x1C1) printf("Textutre Tile start");
            else if(startcode==0x1C2) printf("Textutre Shape Layer start");
            else if(startcode==0x1C3) printf("stuffing start");
            else if(startcode<=0x1C5) printf("reserved");
            else if(startcode<=0x1FF) printf("System start");
            printf(" at %d\n", get_bits_count(gb));
        }

        switch(startcode){
        case 0x120:
            decode_vol_header(s, gb);
            break;
        case USER_DATA_STARTCODE:
            decode_user_data(s, gb);
            break;
        case GOP_STARTCODE:
            mpeg4_decode_gop_header(s, gb);
            break;
        case VOP_STARTCODE:
            return decode_vop_header(s, gb);
        default:
            break;
        }

        align_get_bits(gb);
        startcode = 0xff;
    }
}

/* don't understand why they choose a different header ! */
int intel_h263_decode_picture_header(MpegEncContext *s)
{
    int format;

    /* picture header */
    if (get_bits(&s->gb, 22) != 0x20) {
        fprintf(stderr, "Bad picture start code\n");
        return -1;
    }
    s->picture_number = get_bits(&s->gb, 8); /* picture timestamp */

    if (get_bits1(&s->gb) != 1) {
        fprintf(stderr, "Bad marker\n");
        return -1;	/* marker */
    }
    if (get_bits1(&s->gb) != 0) {
        fprintf(stderr, "Bad H263 id\n");
        return -1;	/* h263 id */
    }
    skip_bits1(&s->gb);	/* split screen off */
    skip_bits1(&s->gb);	/* camera  off */
    skip_bits1(&s->gb);	/* freeze picture release off */

    format = get_bits(&s->gb, 3);
    if (format != 7) {
        fprintf(stderr, "Intel H263 free format not supported\n");
        return -1;
    }
    s->h263_plus = 0;

    s->pict_type = I_TYPE + get_bits1(&s->gb);
    
    s->unrestricted_mv = get_bits1(&s->gb); 
    s->h263_long_vectors = s->unrestricted_mv;

    if (get_bits1(&s->gb) != 0) {
        fprintf(stderr, "SAC not supported\n");
        return -1;	/* SAC: off */
    }
    if (get_bits1(&s->gb) != 0) {
        fprintf(stderr, "Advanced Prediction Mode not supported\n");
        return -1;	/* advanced prediction mode: off */
    }
    if (get_bits1(&s->gb) != 0) {
        fprintf(stderr, "PB frame mode no supported\n");
        return -1;	/* PB frame mode */
    }

    /* skip unknown header garbage */
    skip_bits(&s->gb, 41);

    s->qscale = get_bits(&s->gb, 5);
    skip_bits1(&s->gb);	/* Continuous Presence Multipoint mode: off */

    /* PEI */
    while (get_bits1(&s->gb) != 0) {
        skip_bits(&s->gb, 8);
    }
    s->f_code = 1;

    s->y_dc_scale_table=
    s->c_dc_scale_table= ff_mpeg1_dc_scale_table;

    return 0;
}

