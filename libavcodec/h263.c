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
 * ac prediction encoding & b-frame support by Michael Niedermayer <michaelni@gmx.at>
 */
 
//#define DEBUG
#include "common.h"
#include "dsputil.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "h263data.h"
#include "mpeg4data.h"

//rounded divison & shift
#define RSHIFT(a,b) ((a) > 0 ? ((a) + (1<<((b)-1)))>>(b) : ((a) + (1<<((b)-1))-1)>>(b))

#define PRINT_MB_TYPE(a) {}
//#define PRINT_MB_TYPE(a) printf(a)

#define INTRA_MCBPC_VLC_BITS 6
#define INTER_MCBPC_VLC_BITS 6
#define CBPY_VLC_BITS 6
#define MV_VLC_BITS 9
#define DC_VLC_BITS 9
#define SPRITE_TRAJ_VLC_BITS 6
#define MB_TYPE_B_VLC_BITS 4
#define TEX_VLC_BITS 9

static void h263_encode_block(MpegEncContext * s, DCTELEM * block,
			      int n);
static void h263_encode_motion(MpegEncContext * s, int val, int fcode);
static void h263p_encode_umotion(MpegEncContext * s, int val);
static void mpeg4_encode_block(MpegEncContext * s, DCTELEM * block,
			       int n, int dc, UINT8 *scan_table, 
                               PutBitContext *dc_pb, PutBitContext *ac_pb);
static int h263_decode_motion(MpegEncContext * s, int pred, int fcode);
static int h263p_decode_umotion(MpegEncContext * s, int pred);
static int h263_decode_block(MpegEncContext * s, DCTELEM * block,
                             int n, int coded);
static inline int mpeg4_decode_dc(MpegEncContext * s, int n, int *dir_ptr);
static inline int mpeg4_decode_block(MpegEncContext * s, DCTELEM * block,
                              int n, int coded);
static int h263_pred_dc(MpegEncContext * s, int n, UINT16 **dc_val_ptr);
static void mpeg4_inv_pred_ac(MpegEncContext * s, INT16 *block, int n,
                              int dir);
static void mpeg4_decode_sprite_trajectory(MpegEncContext * s);

extern UINT32 inverse[256];

static UINT16 mv_penalty[MAX_FCODE+1][MAX_MV*2+1];
static UINT8 fcode_tab[MAX_MV*2+1];
static UINT8 umv_fcode_tab[MAX_MV*2+1];

static UINT16 uni_DCtab_lum  [512][2];
static UINT16 uni_DCtab_chrom[512][2];

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
        if (s->pict_type == I_TYPE)
            s->no_rounding = 0;
        else
            s->no_rounding ^= 1;
        put_bits(&s->pb,1,s->no_rounding); /* Rounding Type */
        put_bits(&s->pb,2,0); /* Reserved */
        put_bits(&s->pb,1,1); /* "1" to prevent start code emulation */
		
        /* This should be here if PLUSPTYPE */
        put_bits(&s->pb, 1, 0);	/* Continuous Presence Multipoint mode: off */
		
		if (format == 7) {
            /* Custom Picture Format (CPFMT) */
		
	    if (s->aspect_ratio_info)
            put_bits(&s->pb,4,s->aspect_ratio_info);
	    else
            put_bits(&s->pb,4,2); /* Aspect ratio: CIF 12:11 (4:3) picture */
            put_bits(&s->pb,9,(s->width >> 2) - 1);
            put_bits(&s->pb,1,1); /* "1" to prevent start code emulation */
            put_bits(&s->pb,9,(s->height >> 2));
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

int h263_encode_gob_header(MpegEncContext * s, int mb_line)
{
    int pdif=0;
    
    /* Check to see if we need to put a new GBSC */
    /* for RTP packetization                    */
    if (s->rtp_mode) {
        pdif = pbBufPtr(&s->pb) - s->ptr_lastgob;
        if (pdif >= s->rtp_payload_size) {
            /* Bad luck, packet must be cut before */
            align_put_bits(&s->pb);
            flush_put_bits(&s->pb);
            /* Call the RTP callback to send the last GOB */
            if (s->rtp_callback) {
                pdif = pbBufPtr(&s->pb) - s->ptr_lastgob;
                s->rtp_callback(s->ptr_lastgob, pdif, s->gob_number);
            }
            s->ptr_lastgob = pbBufPtr(&s->pb);
            put_bits(&s->pb, 17, 1); /* GBSC */
            s->gob_number = mb_line / s->gob_index;
            put_bits(&s->pb, 5, s->gob_number); /* GN */
            put_bits(&s->pb, 2, s->pict_type == I_TYPE); /* GFID */
            put_bits(&s->pb, 5, s->qscale); /* GQUANT */
            //fprintf(stderr,"\nGOB: %2d size: %d", s->gob_number - 1, pdif);
            return pdif;
       } else if (pdif + s->mb_line_avgsize >= s->rtp_payload_size) {
           /* Cut the packet before we can't */
           align_put_bits(&s->pb);
           flush_put_bits(&s->pb);
           /* Call the RTP callback to send the last GOB */
           if (s->rtp_callback) {
               pdif = pbBufPtr(&s->pb) - s->ptr_lastgob;
               s->rtp_callback(s->ptr_lastgob, pdif, s->gob_number);
           }
           s->ptr_lastgob = pbBufPtr(&s->pb);
           put_bits(&s->pb, 17, 1); /* GBSC */
           s->gob_number = mb_line / s->gob_index;
           put_bits(&s->pb, 5, s->gob_number); /* GN */
           put_bits(&s->pb, 2, s->pict_type == I_TYPE); /* GFID */
           put_bits(&s->pb, 5, s->qscale); /* GQUANT */
           //fprintf(stderr,"\nGOB: %2d size: %d", s->gob_number - 1, pdif);
           return pdif;
       }
   }
   return 0;
}

static inline int decide_ac_pred(MpegEncContext * s, DCTELEM block[6][64], int dir[6])
{
    int score0=0, score1=0;
    int i, n;

    for(n=0; n<6; n++){
        INT16 *ac_val, *ac_val1;

        ac_val = s->ac_val[0][0] + s->block_index[n] * 16;
        ac_val1= ac_val;
        if(dir[n]){
            ac_val-= s->block_wrap[n]*16;
            for(i=1; i<8; i++){
                const int level= block[n][block_permute_op(i   )];
                score0+= ABS(level);
                score1+= ABS(level - ac_val[i+8]);
                ac_val1[i  ]=    block[n][block_permute_op(i<<3)];
                ac_val1[i+8]= level;
            }
        }else{
            ac_val-= 16;
            for(i=1; i<8; i++){
                const int level= block[n][block_permute_op(i<<3)];
                score0+= ABS(level);
                score1+= ABS(level - ac_val[i]);
                ac_val1[i  ]= level;
                ac_val1[i+8]=    block[n][block_permute_op(i   )];
            }
        }
    }

    return score0 > score1 ? 1 : 0;    
}

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

            /* nothing to do if this MB was skiped in the next P Frame */
            if(s->mbskip_table[s->mb_y * s->mb_width + s->mb_x]){
                s->skip_count++;
                s->mv[0][0][0]= 
                s->mv[0][0][1]= 
                s->mv[1][0][0]= 
                s->mv[1][0][1]= 0;
                s->mv_dir= MV_DIR_FORWARD; //doesnt matter
                return;
            }

            if ((cbp | motion_x | motion_y | mb_type) ==0) {
                /* direct MB with MV={0,0} */
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
            
            if(cbp && mb_type)
                put_bits(&s->pb, 1, 0); /* no q-scale change */

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
                break;
            case 2: /* backward */
                h263_encode_motion(s, motion_x - s->last_mv[1][0][0], s->b_code);
                h263_encode_motion(s, motion_y - s->last_mv[1][0][1], s->b_code);
                s->last_mv[1][0][0]= motion_x;
                s->last_mv[1][0][1]= motion_y;
                break;
            case 3: /* forward */
                h263_encode_motion(s, motion_x - s->last_mv[0][0][0], s->f_code);
                h263_encode_motion(s, motion_y - s->last_mv[0][0][1], s->f_code);
                s->last_mv[0][0][0]= motion_x;
                s->last_mv[0][0][1]= motion_y;
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
                mpeg4_encode_block(s, block[i], i, 0, zigzag_direct, NULL, &s->pb);
            }

            if(interleaved_stats){
                bits= get_bit_count(&s->pb);
                s->p_tex_bits+= bits - s->last_bits;
                s->last_bits=bits;
            }
        }else{ /* s->pict_type==B_TYPE */
            if ((cbp | motion_x | motion_y) == 0 && s->mv_type==MV_TYPE_16X16) {
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
                    p_pic= s->new_picture[0] + offset;
                    
                    s->mb_skiped=1;
                    for(i=0; i<s->max_b_frames; i++){
                        uint8_t *b_pic;
                        int diff;

                        if(s->coded_order[i+1].pict_type!=B_TYPE) break;

                        b_pic= s->coded_order[i+1].picture[0] + offset;
                        diff= pix_abs16x16(p_pic, b_pic, s->linesize);
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
                put_bits(&s->pb,
                        inter_MCBPC_bits[cbpc],
                        inter_MCBPC_code[cbpc]);
                cbpy = cbp >> 2;
                cbpy ^= 0xf;
                put_bits(pb2, cbpy_tab[cbpy][1], cbpy_tab[cbpy][0]);
                    
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
                mpeg4_encode_block(s, block[i], i, 0, zigzag_direct, NULL, tex_pb);
            }

            if(interleaved_stats){
                bits= get_bit_count(&s->pb);
                s->p_tex_bits+= bits - s->last_bits;
                s->last_bits=bits;
            }
            s->p_count++;
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
                if (dir[i]==0) st = ff_alternate_vertical_scan; /* left */
                else           st = ff_alternate_horizontal_scan; /* top */

                for(last_index=63; last_index>=0; last_index--) //FIXME optimize
                    if(block[i][st[last_index]]) break;
                zigzag_last_index[i]= s->block_last_index[i];
                s->block_last_index[i]= last_index;
                scan_table[i]= st;
            }
        }else{
            for(i=0; i<6; i++)
                scan_table[i]= zigzag_direct;
        }

        /* compute cbp */
        cbp = 0;
        for (i = 0; i < 6; i++) {
            if (s->block_last_index[i] >= 1)
                cbp |= 1 << (5 - i);
        }

        cbpc = cbp & 3;
        if (s->pict_type == I_TYPE) {
            put_bits(&s->pb,
                intra_MCBPC_bits[cbpc],
                intra_MCBPC_code[cbpc]);
        } else {
            put_bits(&s->pb, 1, 0);	/* mb coded */
            put_bits(&s->pb,
                inter_MCBPC_bits[cbpc + 4],
                inter_MCBPC_code[cbpc + 4]);
        }
        put_bits(pb2, 1, s->ac_pred);
        cbpy = cbp >> 2;
        put_bits(pb2, cbpy_tab[cbpy][1], cbpy_tab[cbpy][0]);

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
                        block[i][block_permute_op(j   )]= ac_val[j+8];
                }else{
                    for(j=1; j<8; j++) 
                        block[i][block_permute_op(j<<3)]= ac_val[j  ];
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
           
    //printf("**mb x=%d y=%d\n", s->mb_x, s->mb_y);
    if (!s->mb_intra) {
        /* compute cbp */
        cbp = 0;
        for (i = 0; i < 6; i++) {
            if (s->block_last_index[i] >= 0)
                cbp |= 1 << (5 - i);
        }
        if ((cbp | motion_x | motion_y) == 0) {
            /* skip macroblock */
            put_bits(&s->pb, 1, 1);
            return;
        }
        put_bits(&s->pb, 1, 0);	/* mb coded */
        cbpc = cbp & 3;
        put_bits(&s->pb,
		    inter_MCBPC_bits[cbpc],
		    inter_MCBPC_code[cbpc]);
        cbpy = cbp >> 2;
        cbpy ^= 0xf;
        put_bits(&s->pb, cbpy_tab[cbpy][1], cbpy_tab[cbpy][0]);

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
            put_bits(&s->pb,
                intra_MCBPC_bits[cbpc],
                intra_MCBPC_code[cbpc]);
        } else {
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


void h263_pred_acdc(MpegEncContext * s, INT16 *block, int n)
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
                    block[block_permute_op(i*8)] += ac_val[i];
                }
                pred_dc = a;
            }
        } else {
            /* top prediction */
            if (c != 1024) {
                ac_val -= 16 * wrap;
                for(i=1;i<8;i++) {
                    block[block_permute_op(i)] += ac_val[i + 8];
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
        ac_val1[i] = block[block_permute_op(i * 8)];
    /* top copy */
    for(i=1;i<8;i++)
        ac_val1[8 + i] = block[block_permute_op(i)];
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
    if ((s->mb_y == 0 || s->first_slice_line) && block<3) {
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

static void h263_encode_motion(MpegEncContext * s, int val, int f_code)
{
    int range, l, m, bit_size, sign, code, bits;

    if (val == 0) {
        /* zero vector */
        code = 0;
        put_bits(&s->pb, mvtab[code][1], mvtab[code][0]);
    } else {
        bit_size = f_code - 1;
        range = 1 << bit_size;
        /* modulo encoding */
        l = range * 32;
        m = 2 * l;
        if (val < -l) {
            val += m;
        } else if (val >= l) {
            val -= m;
        }

        if (val >= 0) {
            sign = 0;
        } else {
            val = -val;
            sign = 1;
        }
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
        uni_DCtab_lum[level+256][0]= uni_code;
        uni_DCtab_lum[level+256][1]= uni_len;

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
        uni_DCtab_chrom[level+256][0]= uni_code;
        uni_DCtab_chrom[level+256][1]= uni_len;

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

        init_mv_penalty_and_fcode(s);
    }
    s->mv_penalty= mv_penalty; //FIXME exact table for msmpeg4 & h263p
    
    // use fcodes >1 only for mpeg4 & h263 & h263p FIXME
    switch(s->codec_id){
    case CODEC_ID_MPEG4:
        s->fcode_tab= fcode_tab;
        s->min_qcoeff= -2048;
        s->max_qcoeff=  2047;
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

    /* h263 type bias */
    //FIXME mpeg4 mpeg quantizer    
    s->intra_quant_bias=0;
    s->inter_quant_bias=-(1<<(QUANT_BIAS_SHIFT-2)); //(a - x/4)/x
}

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
        j = zigzag_direct[i];
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

/***************************************************/

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

    s->time= picture_number*(INT64)FRAME_RATE_BASE*s->time_increment_resolution/s->frame_rate;
    time_div= s->time/s->time_increment_resolution;
    time_mod= s->time%s->time_increment_resolution;

    if(s->pict_type==B_TYPE){
        s->bp_time= s->last_non_b_time - s->time;
    }else{
        s->last_time_base= s->time_base;
        s->time_base= time_div;
        s->pp_time= s->time - s->last_non_b_time;
        s->last_non_b_time= s->time;
    }
}

static void mpeg4_encode_vol_header(MpegEncContext * s)
{
    int vo_ver_id=1; //must be 2 if we want GMC or q-pel
    char buf[255];

    s->vo_type= s->has_b_frames ? CORE_VO_TYPE : SIMPLE_VO_TYPE;

    put_bits(&s->pb, 16, 0);
    put_bits(&s->pb, 16, 0x100);        /* video obj */
    put_bits(&s->pb, 16, 0);
    put_bits(&s->pb, 16, 0x120);        /* video obj layer */

    put_bits(&s->pb, 1, 0);		/* random access vol */
    put_bits(&s->pb, 8, s->vo_type);	/* video obj type indication */
    put_bits(&s->pb, 1, 1);		/* is obj layer id= yes */
      put_bits(&s->pb, 4, vo_ver_id);	/* is obj layer ver id */
      put_bits(&s->pb, 3, 1);		/* is obj layer priority */
    if(s->aspect_ratio_info) 
        put_bits(&s->pb, 4, s->aspect_ratio_info);/* aspect ratio info */
    else
        put_bits(&s->pb, 4, 1);		/* aspect ratio info= sqare pixel */

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
    put_bits(&s->pb, 1, 0);		/* interlace */
    put_bits(&s->pb, 1, 1);		/* obmc disable */
    if (vo_ver_id == 1) {
        put_bits(&s->pb, 1, s->vol_sprite_usage=0);		/* sprite enable */
    }else{ /* vo_ver_id == 2 */
        put_bits(&s->pb, 2, s->vol_sprite_usage=0);		/* sprite enable */
    }
    put_bits(&s->pb, 1, 0);		/* not 8 bit */
    put_bits(&s->pb, 1, 0);		/* quant type= h263 style*/
    if (vo_ver_id != 1)
        put_bits(&s->pb, 1, s->quarter_sample=0);
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
    put_bits(&s->pb, 16, 0);
    put_bits(&s->pb, 16, 0x1B2);	/* user_data */
    sprintf(buf, "FFmpeg%sb%s", FFMPEG_VERSION, LIBAVCODEC_BUILD_STR);
    put_string(&s->pb, buf);

    ff_mpeg4_stuffing(&s->pb);
}

/* write mpeg4 VOP header */
void mpeg4_encode_picture_header(MpegEncContext * s, int picture_number)
{
    int time_incr;
    int time_div, time_mod;
    
    if(s->pict_type==I_TYPE){
        s->no_rounding=0;
        if(picture_number==0 || !s->strict_std_compliance)
            mpeg4_encode_vol_header(s);
    }
    
//printf("num:%d rate:%d base:%d\n", s->picture_number, s->frame_rate, FRAME_RATE_BASE);
    
    put_bits(&s->pb, 16, 0);	        /* vop header */
    put_bits(&s->pb, 16, 0x1B6);	/* vop header */
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
        s->no_rounding ^= 1;
	put_bits(&s->pb, 1, s->no_rounding);	/* rounding type */
    }
    put_bits(&s->pb, 3, 0);	/* intra dc VLC threshold */
    //FIXME sprite stuff

    put_bits(&s->pb, 5, s->qscale);

    if (s->pict_type != I_TYPE)
	put_bits(&s->pb, 3, s->f_code);	/* fcode_for */
    if (s->pict_type == B_TYPE)
	put_bits(&s->pb, 3, s->b_code);	/* fcode_back */
    //    printf("****frame %d\n", picture_number);

     s->y_dc_scale_table= ff_mpeg4_y_dc_scale_table; //FIXME add short header support 
     s->c_dc_scale_table= ff_mpeg4_c_dc_scale_table;
}

static void h263_dc_scale(MpegEncContext * s)
{
    s->y_dc_scale= s->y_dc_scale_table[ s->qscale ];
    s->c_dc_scale= s->c_dc_scale_table[ s->qscale ];
}

inline int ff_mpeg4_pred_dc(MpegEncContext * s, int n, UINT16 **dc_val_ptr, int *dir_ptr)
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

void mpeg4_pred_ac(MpegEncContext * s, INT16 *block, int n,
                   int dir)
{
    int i;
    INT16 *ac_val, *ac_val1;

    /* find prediction */
    ac_val = s->ac_val[0][0] + s->block_index[n] * 16;
    ac_val1 = ac_val;
    if (s->ac_pred) {
        if (dir == 0) {
            /* left prediction */
            ac_val -= 16;
            for(i=1;i<8;i++) {
                block[block_permute_op(i*8)] += ac_val[i];
            }
        } else {
            /* top prediction */
            ac_val -= 16 * s->block_wrap[n];
            for(i=1;i<8;i++) {
                block[block_permute_op(i)] += ac_val[i + 8];
            }
        }
    }
    /* left copy */
    for(i=1;i<8;i++)
        ac_val1[i] = block[block_permute_op(i * 8)];
    /* top copy */
    for(i=1;i<8;i++)
        ac_val1[8 + i] = block[block_permute_op(i)];
}

static void mpeg4_inv_pred_ac(MpegEncContext * s, INT16 *block, int n,
                              int dir)
{
    int i;
    INT16 *ac_val;

    /* find prediction */
    ac_val = s->ac_val[0][0] + s->block_index[n] * 16;
 
    if (dir == 0) {
        /* left prediction */
        ac_val -= 16;
        for(i=1;i<8;i++) {
            block[block_permute_op(i*8)] -= ac_val[i];
        }
    } else {
        /* top prediction */
        ac_val -= 16 * s->block_wrap[n];
        for(i=1;i<8;i++) {
            block[block_permute_op(i)] -= ac_val[i + 8];
        }
    }
}

static inline void mpeg4_encode_dc(PutBitContext * s, int level, int n)
{
#if 1
//    if(level<-255 || level>255) printf("dc overflow\n");
    level+=256;
    if (n < 4) {
	/* luminance */
	put_bits(s, uni_DCtab_lum[level][1], uni_DCtab_lum[level][0]);
    } else {
	/* chrominance */
	put_bits(s, uni_DCtab_chrom[level][1], uni_DCtab_chrom[level][0]);
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

static void mpeg4_encode_block(MpegEncContext * s, DCTELEM * block, int n, int intra_dc, 
                               UINT8 *scan_table, PutBitContext *dc_pb, PutBitContext *ac_pb)
{
    int level, run, last, i, j, last_index, last_non_zero, sign, slevel;
    int code;
    const RLTable *rl;

    if (s->mb_intra) {
	/* mpeg4 based DC predictor */
	mpeg4_encode_dc(dc_pb, intra_dc, n);
	i = 1;
        rl = &rl_intra;
    } else {
	i = 0;
        rl = &rl_inter;
    }

    /* AC coefs */
    last_index = s->block_last_index[n];
    last_non_zero = i - 1;
    for (; i <= last_index; i++) {
	j = scan_table[i];
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
}



/***********************************************/
/* decoding */

static VLC intra_MCBPC_vlc;
static VLC inter_MCBPC_vlc;
static VLC cbpy_vlc;
static VLC mv_vlc;
static VLC dc_lum, dc_chrom;
static VLC sprite_trajectory;
static VLC mb_type_b_vlc;

void init_rl(RLTable *rl)
{
    INT8 max_level[MAX_RUN+1], max_run[MAX_LEVEL+1];
    UINT8 index_run[MAX_RUN+1];
    int last, run, level, start, end, i;

    /* compute max_level[], max_run[] and index_run[] */
    for(last=0;last<2;last++) {
        if (last == 0) {
            start = 0;
            end = rl->last;
        } else {
            start = rl->last;
            end = rl->n;
        }

        memset(max_level, 0, MAX_RUN + 1);
        memset(max_run, 0, MAX_LEVEL + 1);
        memset(index_run, rl->n, MAX_RUN + 1);
        for(i=start;i<end;i++) {
            run = rl->table_run[i];
            level = rl->table_level[i];
            if (index_run[run] == rl->n)
                index_run[run] = i;
            if (level > max_level[run])
                max_level[run] = level;
            if (run > max_run[level])
                max_run[level] = run;
        }
        rl->max_level[last] = av_malloc(MAX_RUN + 1);
        memcpy(rl->max_level[last], max_level, MAX_RUN + 1);
        rl->max_run[last] = av_malloc(MAX_LEVEL + 1);
        memcpy(rl->max_run[last], max_run, MAX_LEVEL + 1);
        rl->index_run[last] = av_malloc(MAX_RUN + 1);
        memcpy(rl->index_run[last], index_run, MAX_RUN + 1);
    }
}

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
                run= 65;
                level= MAX_LEVEL;
            }else if(len<0){ //more bits needed
                run= 0;
                level= code;
            }else{
                if(code==rl->n){ //esc
                    run= 65;
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

int h263_decode_gob_header(MpegEncContext *s)
{
    unsigned int val, gfid;
    
    /* Check for GOB Start Code */
    val = show_bits(&s->gb, 16);
    if (val == 0) {
        /* We have a GBSC probably with GSTUFF */
        skip_bits(&s->gb, 16); /* Drop the zeros */
        while (get_bits1(&s->gb) == 0); /* Seek the '1' bit */
#ifdef DEBUG
        fprintf(stderr,"\nGOB Start Code at MB %d\n", (s->mb_y * s->mb_width) + s->mb_x);
#endif
        s->gob_number = get_bits(&s->gb, 5); /* GN */
        gfid = get_bits(&s->gb, 2); /* GFID */
        s->qscale = get_bits(&s->gb, 5); /* GQUANT */
#ifdef DEBUG
        fprintf(stderr, "\nGN: %u GFID: %u Quant: %u\n", s->gob_number, gfid, s->qscale);
#endif
        return 1;
    }
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
        s->misc_bits+=17 + pb2_len;;
        s->mv_bits+= bits - s->last_bits;
        s->p_tex_bits+= tex_pb_len;
    }

    flush_put_bits(&s->pb2);
    flush_put_bits(&s->tex_pb);

    ff_copy_bits(&s->pb, s->pb2_buffer   , pb2_len);
    ff_copy_bits(&s->pb, s->tex_pb_buffer, tex_pb_len);
    s->last_bits= get_bit_count(&s->pb);
}

void ff_mpeg4_encode_video_packet_header(MpegEncContext *s)
{
    int mb_num_bits= av_log2(s->mb_num - 1) + 1;

    ff_mpeg4_stuffing(&s->pb);
    if(s->pict_type==I_TYPE)
        put_bits(&s->pb, 16, 0);
    else if(s->pict_type==B_TYPE)
        put_bits(&s->pb, MAX(MAX(s->f_code, s->b_code)+15, 17), 0);
    else /* S/P_TYPE */
        put_bits(&s->pb, s->f_code+15, 0);
    put_bits(&s->pb, 1, 1);
    
    put_bits(&s->pb, mb_num_bits, s->mb_x + s->mb_y*s->mb_width);
    put_bits(&s->pb, 5, s->qscale);
    put_bits(&s->pb, 1, 0); /* no HEC */
}

/**
 * decodes the next video packet and sets s->next_qscale 
 * returns mb_num of the next packet or <0 if something went wrong
 */
static int decode_video_packet_header(MpegEncContext *s, GetBitContext *gb)
{
    int bits;
    int mb_num_bits= av_log2(s->mb_num - 1) + 1;
    int header_extension=0, mb_num;
//printf("%X\n", show_bits(&gb, 24));
//printf("parse_video_packet_header\n");
//    if(show_aligned_bits(gb, 1, 16) != 0) return -1;
    
    /* is there enough space left for a video packet + header */
    if( get_bits_count(gb) > gb->size*8-20) return -1;

//printf("resync at %d %d\n", s->mb_x, s->mb_y);
//    skip_bits(gb, 1);
//    align_get_bits(gb);
    if(get_bits(gb, 16)!=0){
        printf("internal error while decoding video packet header\n");
    }

//printf("%X\n", show_bits(gb, 24));
    bits=0;
    while(!get_bits1(gb) && bits<30) bits++;
    if((s->pict_type == P_TYPE || s->pict_type == S_TYPE) && bits != s->f_code-1){
        printf("marker does not match f_code (is: %d should be: %d pos: %d end %d x: %d y: %d)\n", 
               bits+1, s->f_code, get_bits_count(gb), gb->size*8, s->mb_x, s->mb_y);
        return -1;
    }else if(s->pict_type == I_TYPE && bits != 0){
        printf("marker too long\n");
        return -1;
    }else if(s->pict_type == B_TYPE && bits != MAX(MAX(s->f_code, s->b_code)-1, 1)){
        printf("marker does not match f/b_code\n");
        return -1;
    }
//printf("%X\n", show_bits(gb, 24));

    if(s->shape != RECT_SHAPE){
        header_extension= get_bits1(gb);
        //FIXME more stuff here
    }

    mb_num= get_bits(gb, mb_num_bits);
    if(mb_num < s->mb_x + s->mb_y*s->mb_width || mb_num>=s->mb_num){
        fprintf(stderr, "illegal mb_num in video packet (%d %d) \n", mb_num, s->mb_x + s->mb_y*s->mb_width);
        return -1;
    }

    if(s->shape != BIN_ONLY_SHAPE){
        s->next_resync_qscale= get_bits(gb, 5);
        if(s->next_resync_qscale==0)
            s->next_resync_qscale= s->qscale;
        if(s->next_resync_qscale==0){
            fprintf(stderr, "qscale==0\n");
            return -1;
        }
    }

    if(s->shape == RECT_SHAPE){
        header_extension= get_bits1(gb);
    }
    if(header_extension){
        int time_increment;
        int time_incr=0;
        printf("header extension not supported\n");
        return -1;

        while (get_bits1(gb) != 0) 
            time_incr++;

        check_marker(gb, "before time_increment in video packed header");
        time_increment= get_bits(gb, s->time_increment_bits);
        if(s->pict_type!=B_TYPE){
            s->last_time_base= s->time_base;
            s->time_base+= time_incr;
            s->time= s->time_base*s->time_increment_resolution + time_increment;
            s->pp_time= s->time - s->last_non_b_time;
            s->last_non_b_time= s->time;
        }else{
            s->time= (s->last_time_base + time_incr)*s->time_increment_resolution + time_increment;
            s->bp_time= s->last_non_b_time - s->time;
        }
        check_marker(gb, "before vop_coding_type in video packed header");
        
        skip_bits(gb, 2); /* vop coding type */
        //FIXME not rect stuff here

        if(s->shape != BIN_ONLY_SHAPE){
            skip_bits(gb, 3); /* intra dc vlc threshold */

            if(s->pict_type == S_TYPE && s->vol_sprite_usage==GMC_SPRITE && s->num_sprite_warping_points){
                mpeg4_decode_sprite_trajectory(s);
            }

            //FIXME reduced res stuff here
            
            if (s->pict_type != I_TYPE) {
                s->f_code = get_bits(gb, 3);	/* fcode_for */
                if(s->f_code==0){
                    printf("Error, video packet header damaged or not MPEG4 header (f_code=0)\n");
                    return -1; // makes no sense to continue, as the MV decoding will break very quickly
                }
            }
            if (s->pict_type == B_TYPE) {
                s->b_code = get_bits(gb, 3);
            }       
        }
    }
    //FIXME new-pred stuff
    
//printf("parse ok %d %d %d %d\n", mb_num, s->mb_x + s->mb_y*s->mb_width, get_bits_count(gb), get_bits_count(&s->gb));

    return mb_num;
}

void ff_mpeg4_clean_buffers(MpegEncContext *s)
{
    int c_wrap, c_xy, l_wrap, l_xy;

    l_wrap= s->block_wrap[0];
    l_xy= s->mb_y*l_wrap*2 + s->mb_x*2;
    c_wrap= s->block_wrap[4];
    c_xy= s->mb_y*c_wrap + s->mb_x;

    /* clean DC */
    memsetw(s->dc_val[0] + l_xy, 1024, l_wrap*2+1);
    memsetw(s->dc_val[1] + c_xy, 1024, c_wrap+1);
    memsetw(s->dc_val[2] + c_xy, 1024, c_wrap+1);

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

/* searches for the next resync marker clears ac,dc,mc, and sets s->next_resync_gb, s->mb_num_left */
int ff_mpeg4_resync(MpegEncContext *s)
{
    GetBitContext gb;
    
    /* search & parse next resync marker */
    
    gb= s->next_resync_gb;
    align_get_bits(&gb);
//printf("mpeg4_resync %d next:%d \n", get_bits_count(&gb), get_bits_count(&s->next_resync_gb));
    for(;;) {
        int v= show_bits(&gb, 24);
        if( get_bits_count(&gb) >= gb.size*8-24 || v == 1 /* start-code */){
            s->mb_num_left= s->mb_num - s->mb_x - s->mb_y*s->mb_width;
//printf("mpeg4_resync end\n");
            s->gb= s->next_resync_gb; //continue at the next resync marker
            return -1;
        }else if(v>>8 == 0){
            int next;
            s->next_resync_pos= get_bits_count(&gb);
            
            next= decode_video_packet_header(s, &gb);
            if(next >= 0){
                s->mb_num_left= next - s->mb_x - s->mb_y*s->mb_width;
                break;
            }

            align_get_bits(&gb);
        }
        skip_bits(&gb, 8);
    }
    s->next_resync_gb=gb;
    
    return 0;
}

static inline void init_block_index(MpegEncContext *s)
{
    s->block_index[0]= s->block_wrap[0]*(s->mb_y*2 + 1) - 1 + s->mb_x*2;
    s->block_index[1]= s->block_wrap[0]*(s->mb_y*2 + 1)     + s->mb_x*2;
    s->block_index[2]= s->block_wrap[0]*(s->mb_y*2 + 2) - 1 + s->mb_x*2;
    s->block_index[3]= s->block_wrap[0]*(s->mb_y*2 + 2)     + s->mb_x*2;
    s->block_index[4]= s->block_wrap[4]*(s->mb_y + 1)                    + s->block_wrap[0]*(s->mb_height*2 + 2) + s->mb_x;
    s->block_index[5]= s->block_wrap[4]*(s->mb_y + 1 + s->mb_height + 2) + s->block_wrap[0]*(s->mb_height*2 + 2) + s->mb_x;
}

static inline void update_block_index(MpegEncContext *s)
{
    s->block_index[0]+=2;
    s->block_index[1]+=2;
    s->block_index[2]+=2;
    s->block_index[3]+=2;
    s->block_index[4]++;
    s->block_index[5]++;
}

/**
 * decodes the first & second partition
 * returns error type or 0 if no error
 */
int ff_mpeg4_decode_partitions(MpegEncContext *s)
{
    static const INT8 quant_tab[4] = { -1, -2, 1, 2 };
    int mb_num;
    
    /* decode first partition */
    mb_num=0;
    s->first_slice_line=1;
    s->mb_x= s->resync_mb_x;
    for(s->mb_y= s->resync_mb_y; mb_num < s->mb_num_left; s->mb_y++){
        init_block_index(s);
        for(; mb_num < s->mb_num_left && s->mb_x<s->mb_width; s->mb_x++){
            const int xy= s->mb_x + s->mb_y*s->mb_width;
            int cbpc;
            int dir=0;
            
            mb_num++;
            update_block_index(s);
            if(s->mb_x == s->resync_mb_x && s->mb_y == s->resync_mb_y+1)
                s->first_slice_line=0;
            
            if(s->mb_x==0) PRINT_MB_TYPE("\n");

            if(s->pict_type==I_TYPE){
                int i;

                PRINT_MB_TYPE("I");
                cbpc = get_vlc2(&s->gb, intra_MCBPC_vlc.table, INTRA_MCBPC_VLC_BITS, 1);
                if (cbpc < 0){
                    fprintf(stderr, "cbpc corrupted at %d %d\n", s->mb_x, s->mb_y);
                    return DECODING_DESYNC;
                }
                s->cbp_table[xy]= cbpc & 3;
                s->mb_type[xy]= MB_TYPE_INTRA;
                s->mb_intra = 1;

                if(cbpc & 4) {
                    s->qscale += quant_tab[get_bits(&s->gb, 2)];
                    if (s->qscale < 1)
                        s->qscale = 1;
                    else if (s->qscale > 31)
                        s->qscale = 31;
                    h263_dc_scale(s);
                }
                s->qscale_table[xy]= s->qscale;

                s->mbintra_table[xy]= 1;
                for(i=0; i<6; i++){
                    int dc_pred_dir;
                    int dc= mpeg4_decode_dc(s, i, &dc_pred_dir); 
                    if(dc < 0){
                        fprintf(stderr, "DC corrupted at %d %d\n", s->mb_x, s->mb_y);
                        return DECODING_DESYNC;
                    }
                    dir<<=1;
                    if(dc_pred_dir) dir|=1;
                }
                s->pred_dir_table[xy]= dir;
            }else{ /* P/S_TYPE */
                int mx, my, pred_x, pred_y;
                INT16 * const mot_val= s->motion_val[s->block_index[0]];
                const int stride= s->block_wrap[0]*2;

                if(get_bits1(&s->gb)){
                    /* skip mb */
                    s->mb_type[xy]= MB_TYPE_SKIPED;
                    if(s->pict_type==S_TYPE && s->vol_sprite_usage==GMC_SPRITE){
                        const int a= s->sprite_warping_accuracy;
                        PRINT_MB_TYPE("G");
                        if(s->divx_version==500 && s->divx_build==413){
                            mx = s->sprite_offset[0][0] / (1<<(a-s->quarter_sample));
                            my = s->sprite_offset[0][1] / (1<<(a-s->quarter_sample));
                        }else{
                            mx = RSHIFT(s->sprite_offset[0][0], a-s->quarter_sample);
                            my = RSHIFT(s->sprite_offset[0][1], a-s->quarter_sample);
                            s->mb_type[xy]= MB_TYPE_GMC | MB_TYPE_SKIPED;
                        }
                    }else{
                        PRINT_MB_TYPE("S");
                        mx = 0;
                        my = 0;
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
                    fprintf(stderr, "cbpc corrupted at %d %d\n", s->mb_x, s->mb_y);
                    return DECODING_DESYNC;
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
                        if(!s->mcsel)
                           mx = h263_decode_motion(s, pred_x, s->f_code);
                        else {
                            const int a= s->sprite_warping_accuracy;
                            if(s->divx_version==500 && s->divx_build==413){
                                mx = s->sprite_offset[0][0] / (1<<(a-s->quarter_sample));
                            }else{
                                mx = RSHIFT(s->sprite_offset[0][0], a-s->quarter_sample);
                            }
                        }
                        if (mx >= 0xffff)
                            return DECODING_DESYNC;
            
                        if(!s->mcsel)
                           my = h263_decode_motion(s, pred_y, s->f_code);
                        else{
                           const int a= s->sprite_warping_accuracy;
                            if(s->divx_version==500 && s->divx_build==413){
                                my = s->sprite_offset[0][1] / (1<<(a-s->quarter_sample));
                            }else{
                                my = RSHIFT(s->sprite_offset[0][1], a-s->quarter_sample);
                            }
                        }
                        if (my >= 0xffff)
                            return DECODING_DESYNC;
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
                                return DECODING_DESYNC;
                
                            my = h263_decode_motion(s, pred_y, s->f_code);
                            if (my >= 0xffff)
                                return DECODING_DESYNC;
                            mot_val[0] = mx;
                            mot_val[1] = my;
                        }
                    }
                }
            }
        }
        s->mb_x= 0;
    }

    if     (s->pict_type==I_TYPE && get_bits(&s->gb, 19)!=DC_MARKER    ) s->decoding_error= DECODING_DESYNC;
    else if(s->pict_type!=I_TYPE && get_bits(&s->gb, 17)!=MOTION_MARKER) s->decoding_error= DECODING_DESYNC;
    if(s->decoding_error== DECODING_DESYNC){
        fprintf(stderr, "marker missing after first partition at %d %d\n", s->mb_x, s->mb_y);
        return DECODING_DESYNC;
    }

    /* decode second partition */
    mb_num=0;
    s->mb_x= s->resync_mb_x;
    for(s->mb_y= s->resync_mb_y; mb_num < s->mb_num_left; s->mb_y++){
        init_block_index(s);
        for(; mb_num < s->mb_num_left && s->mb_x<s->mb_width; s->mb_x++){
            const int xy= s->mb_x + s->mb_y*s->mb_width;

            mb_num++;
            update_block_index(s);
            
            if(s->pict_type==I_TYPE){
                int ac_pred= get_bits1(&s->gb);
                int cbpy = get_vlc2(&s->gb, cbpy_vlc.table, CBPY_VLC_BITS, 1);
                if(cbpy<0){
                    fprintf(stderr, "cbpy corrupted at %d %d\n", s->mb_x, s->mb_y);
                    return DECODING_AC_LOST;
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
                        return DECODING_ACDC_LOST;
                    }
                    
                    if(s->cbp_table[xy] & 8) {
                        s->qscale += quant_tab[get_bits(&s->gb, 2)];
                        if (s->qscale < 1)
                            s->qscale = 1;
                        else if (s->qscale > 31)
                            s->qscale = 31;
                        h263_dc_scale(s);
                    }
                    s->qscale_table[xy]= s->qscale;

                    for(i=0; i<6; i++){
                        int dc_pred_dir;
                        int dc= mpeg4_decode_dc(s, i, &dc_pred_dir); 
                        if(dc < 0){
                            fprintf(stderr, "DC corrupted at %d %d\n", s->mb_x, s->mb_y);
                            return DECODING_ACDC_LOST;
                        }
                        dir<<=1;
                        if(dc_pred_dir) dir|=1;
                    }
                    s->cbp_table[xy]&= 3; //remove dquant
                    s->cbp_table[xy]|= cbpy<<2;
                    s->pred_dir_table[xy]= dir | (ac_pred<<7);
                }else if(s->mb_type[xy]&MB_TYPE_SKIPED){
                    s->qscale_table[xy]= s->qscale;
                    s->cbp_table[xy]= 0;
                }else{
                    int cbpy = get_vlc2(&s->gb, cbpy_vlc.table, CBPY_VLC_BITS, 1);

                    if(cbpy<0){
                        fprintf(stderr, "P cbpy corrupted at %d %d\n", s->mb_x, s->mb_y);
                        return DECODING_ACDC_LOST;
                    }
                    
                    if(s->cbp_table[xy] & 8) {
//fprintf(stderr, "dquant\n");
                        s->qscale += quant_tab[get_bits(&s->gb, 2)];
                        if (s->qscale < 1)
                            s->qscale = 1;
                        else if (s->qscale > 31)
                            s->qscale = 31;
                        h263_dc_scale(s);
                    }
                    s->qscale_table[xy]= s->qscale;

                    s->cbp_table[xy]&= 3; //remove dquant
                    s->cbp_table[xy]|= (cbpy^0xf)<<2;
                }
            }
        }
        s->mb_x= 0;
    }
    

    return 0;        
}

static int mpeg4_decode_partitioned_mb(MpegEncContext *s,
                   DCTELEM block[6][64])
{
    int cbp, mb_type;
    const int xy= s->mb_x + s->mb_y*s->mb_width;

    if(s->mb_x==s->resync_mb_x && s->mb_y==s->resync_mb_y){ //Note resync_mb_{x,y}==0 at the start
        int i;
        int block_index_backup[6];
        int qscale= s->qscale;
        
        for(i=0; i<6; i++) block_index_backup[i]= s->block_index[i];
        
        s->decoding_error= ff_mpeg4_decode_partitions(s);
        
        for(i=0; i<6; i++) s->block_index[i]= block_index_backup[i];
        s->first_slice_line=1;
        s->mb_x= s->resync_mb_x;
        s->mb_y= s->resync_mb_y;
        s->qscale= qscale;
        h263_dc_scale(s);

        if(s->decoding_error==DECODING_DESYNC) return -1;
    }
    
    mb_type= s->mb_type[xy];
    if(s->decoding_error)
        cbp=0;
    else 
        cbp = s->cbp_table[xy];

    if(s->decoding_error!=DECODING_ACDC_LOST && s->qscale_table[xy] != s->qscale){
        s->qscale= s->qscale_table[xy];
        h263_dc_scale(s);
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
            return 0;
        }else if(s->mb_intra && s->decoding_error!=DECODING_ACDC_LOST){
            s->ac_pred = s->pred_dir_table[xy]>>7;

            /* decode each block */
            for (i = 0; i < 6; i++) {
                int ret= mpeg4_decode_block(s, block[i], i, (cbp >> (5 - i)) & 1);
                if(ret==DECODING_AC_LOST){
                    fprintf(stderr, "texture corrupted at %d %d (trying to continue with mc/dc only)\n", s->mb_x, s->mb_y);
                    s->decoding_error=DECODING_AC_LOST;
                    cbp=0;
                }else if(ret==DECODING_ACDC_LOST){
                    fprintf(stderr, "dc corrupted at %d %d (trying to continue with mc only)\n", s->mb_x, s->mb_y);
                    s->decoding_error=DECODING_ACDC_LOST;
                    break;
                }
            }
        }else if(!s->mb_intra){
//            s->mcsel= 0; //FIXME do we need to init that
            
            s->mv_dir = MV_DIR_FORWARD;
            if (mb_type&MB_TYPE_INTER4V) {
                s->mv_type = MV_TYPE_8X8;
            } else {
                s->mv_type = MV_TYPE_16X16;
            }
            if(s->decoding_error==0 && cbp){
                /* decode each block */
                for (i = 0; i < 6; i++) {
                    int ret= mpeg4_decode_block(s, block[i], i, (cbp >> (5 - i)) & 1);
                    if(ret==DECODING_AC_LOST){
                        fprintf(stderr, "texture corrupted at %d %d (trying to continue with mc/dc only)\n", s->mb_x, s->mb_y);
                        s->decoding_error=DECODING_AC_LOST;
                        break;
                    }
                }
            }
        }
    } else { /* I-Frame */
        int i;
        s->mb_intra = 1;
        s->ac_pred = s->pred_dir_table[xy]>>7;
        
        /* decode each block */
        for (i = 0; i < 6; i++) {
            int ret= mpeg4_decode_block(s, block[i], i, (cbp >> (5 - i)) & 1);
            if(ret==DECODING_AC_LOST){
                fprintf(stderr, "texture corrupted at %d %d (trying to continue with dc only)\n", s->mb_x, s->mb_y);
                s->decoding_error=DECODING_AC_LOST;
                cbp=0;
            }else if(ret==DECODING_ACDC_LOST){
                fprintf(stderr, "dc corrupted at %d %d\n", s->mb_x, s->mb_y);
                return -1;
            }
        }
    }

    return 0;
}


int h263_decode_mb(MpegEncContext *s,
                   DCTELEM block[6][64])
{
    int cbpc, cbpy, i, cbp, pred_x, pred_y, mx, my, dquant;
    INT16 *mot_val;
    static INT8 quant_tab[4] = { -1, -2, 1, 2 };

    if(s->mb_x==0) PRINT_MB_TYPE("\n");

    if(s->resync_marker){
        if(s->resync_mb_x == s->mb_x && s->resync_mb_y+1 == s->mb_y){
            s->first_slice_line=0; 
        }
    }

    if(s->data_partitioning && s->pict_type!=B_TYPE)
        return mpeg4_decode_partitioned_mb(s, block);

    if (s->pict_type == P_TYPE || s->pict_type==S_TYPE) {
        if (get_bits1(&s->gb)) {
            /* skip mb */
            s->mb_intra = 0;
            for(i=0;i<6;i++)
                s->block_last_index[i] = -1;
            s->mv_dir = MV_DIR_FORWARD;
            s->mv_type = MV_TYPE_16X16;
            if(s->pict_type==S_TYPE && s->vol_sprite_usage==GMC_SPRITE){
                const int a= s->sprite_warping_accuracy;
//                int l = (1 << (s->f_code - 1)) * 32;
                PRINT_MB_TYPE("G");
                s->mcsel=1;
                if(s->divx_version==500 && s->divx_build==413){
                    s->mv[0][0][0] = s->sprite_offset[0][0] / (1<<(a-s->quarter_sample));
                    s->mv[0][0][1] = s->sprite_offset[0][1] / (1<<(a-s->quarter_sample));
                }else{
                    s->mv[0][0][0] = RSHIFT(s->sprite_offset[0][0], a-s->quarter_sample);
                    s->mv[0][0][1] = RSHIFT(s->sprite_offset[0][1], a-s->quarter_sample);
                }
/*                if (s->mv[0][0][0] < -l) s->mv[0][0][0]= -l;
                else if (s->mv[0][0][0] >= l) s->mv[0][0][0]= l-1;
                if (s->mv[0][0][1] < -l) s->mv[0][0][1]= -l;
                else if (s->mv[0][0][1] >= l) s->mv[0][0][1]= l-1;*/

                s->mb_skiped = 0;
            }else{
                PRINT_MB_TYPE("S");
                s->mcsel=0;
                s->mv[0][0][0] = 0;
                s->mv[0][0][1] = 0;
                s->mb_skiped = 1;
            }
            return 0;
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
            s->qscale += quant_tab[get_bits(&s->gb, 2)];
            if (s->qscale < 1)
                s->qscale = 1;
            else if (s->qscale > 31)
                s->qscale = 31;
            h263_dc_scale(s);
        }
        s->mv_dir = MV_DIR_FORWARD;
        if ((cbpc & 16) == 0) {
            PRINT_MB_TYPE("P");
            /* 16x16 motion prediction */
            s->mv_type = MV_TYPE_16X16;
            h263_pred_motion(s, 0, &pred_x, &pred_y);
            if (s->umvplus_dec)
               mx = h263p_decode_umotion(s, pred_x);
            else if(!s->mcsel)
               mx = h263_decode_motion(s, pred_x, s->f_code);
            else {
               const int a= s->sprite_warping_accuracy;
//        int l = (1 << (s->f_code - 1)) * 32;
                if(s->divx_version==500 && s->divx_build==413){
                    mx = s->sprite_offset[0][0] / (1<<(a-s->quarter_sample));
                }else{
                    mx = RSHIFT(s->sprite_offset[0][0], a-s->quarter_sample);
                }
//        if (mx < -l) mx= -l, printf("C");
//        else if (mx >= l) mx= l-1, printf("C");
            }
            if (mx >= 0xffff)
                return -1;
            
            if (s->umvplus_dec)
               my = h263p_decode_umotion(s, pred_y);
            else if(!s->mcsel)
               my = h263_decode_motion(s, pred_y, s->f_code);
            else{
               const int a= s->sprite_warping_accuracy;
//       int l = (1 << (s->f_code - 1)) * 32;
                if(s->divx_version==500 && s->divx_build==413){
                    my = s->sprite_offset[0][1] / (1<<(a-s->quarter_sample));
                }else{
                    my = RSHIFT(s->sprite_offset[0][1], a-s->quarter_sample);
                }
//       if (my < -l) my= -l, printf("C");
//       else if (my >= l) my= l-1, printf("C");
            }
            if (my >= 0xffff)
                return -1;
            s->mv[0][0][0] = mx;
            s->mv[0][0][1] = my;
            /*fprintf(stderr, "\n MB %d", (s->mb_y * s->mb_width) + s->mb_x);
            fprintf(stderr, "\n\tmvx: %d\t\tpredx: %d", mx, pred_x);
            fprintf(stderr, "\n\tmvy: %d\t\tpredy: %d", my, pred_y);*/
            if (s->umvplus_dec && (mx - pred_x) == 1 && (my - pred_y) == 1)
               skip_bits1(&s->gb); /* Bit stuffing to prevent PSC */
                           
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
        uint16_t time_pp;
        uint16_t time_pb;
        int xy;

        s->mb_intra = 0; //B-frames never contain intra blocks
        s->mcsel=0;      //     ...               true gmc blocks

        if(s->mb_x==0){
            s->last_mv[0][0][0]= 
            s->last_mv[0][0][1]= 
            s->last_mv[1][0][0]= 
            s->last_mv[1][0][1]= 0;
        }

        /* if we skipped it in the future P Frame than skip it now too */
        s->mb_skiped= s->mbskip_table[s->mb_y * s->mb_width + s->mb_x]; // Note, skiptab=0 if last was GMC

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
//FIXME is this correct?
/*            s->last_mv[0][0][0]=
            s->last_mv[0][0][1]=0;*/
            PRINT_MB_TYPE("s");
            return 0;
        }

        modb1= get_bits1(&s->gb);
        if(modb1==0){
            modb2= get_bits1(&s->gb);
            mb_type= get_vlc2(&s->gb, mb_type_b_vlc.table, MB_TYPE_B_VLC_BITS, 1);
            if(modb2==0) cbp= get_bits(&s->gb, 6);
            else cbp=0;
            if (mb_type && cbp) {
                if(get_bits1(&s->gb)){
                    s->qscale +=get_bits1(&s->gb)*4 - 2;
                    if (s->qscale < 1)
                        s->qscale = 1;
                    else if (s->qscale > 31)
                        s->qscale = 31;
                    h263_dc_scale(s);
                }
            }
        }else{
            mb_type=4; //like 0 but no vectors coded
            cbp=0;
        }
        s->mv_type = MV_TYPE_16X16; // we'll switch to 8x8 only if the last P frame had 8x8 for this MB and mb_type=0 here
        mx=my=0; //for case 4, we could put this to the mb_type=4 but than gcc compains about uninitalized mx/my
        switch(mb_type)
        {
        case 0: /* direct */
            mx = h263_decode_motion(s, 0, 1);
            my = h263_decode_motion(s, 0, 1);
        case 4: /* direct with mx=my=0 */
            s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD | MV_DIRECT;
            xy= s->block_index[0];
            time_pp= s->pp_time;
            time_pb= time_pp - s->bp_time;
//if(time_pp>3000 )printf("%d %d  ", time_pp, time_pb);
            //FIXME 4MV
            //FIXME avoid divides
            s->mv[0][0][0] = s->motion_val[xy][0]*time_pb/time_pp + mx;
            s->mv[0][0][1] = s->motion_val[xy][1]*time_pb/time_pp + my;
            s->mv[1][0][0] = mx ? s->mv[0][0][0] - s->motion_val[xy][0]
                                : s->motion_val[xy][0]*(time_pb - time_pp)/time_pp + mx;
            s->mv[1][0][1] = my ? s->mv[0][0][1] - s->motion_val[xy][1] 
                                : s->motion_val[xy][1]*(time_pb - time_pp)/time_pp + my;
/*            s->mv[0][0][0] = 
            s->mv[0][0][1] = 
            s->mv[1][0][0] = 
            s->mv[1][0][1] = 1000;*/
            PRINT_MB_TYPE("D");
            break;
        case 1: 
            s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD;
            mx = h263_decode_motion(s, s->last_mv[0][0][0], s->f_code);
            my = h263_decode_motion(s, s->last_mv[0][0][1], s->f_code);
            s->last_mv[0][0][0]= s->mv[0][0][0] = mx;
            s->last_mv[0][0][1]= s->mv[0][0][1] = my;

            mx = h263_decode_motion(s, s->last_mv[1][0][0], s->b_code);
            my = h263_decode_motion(s, s->last_mv[1][0][1], s->b_code);
            s->last_mv[1][0][0]= s->mv[1][0][0] = mx;
            s->last_mv[1][0][1]= s->mv[1][0][1] = my;
            PRINT_MB_TYPE("i");
            break;
        case 2: 
            s->mv_dir = MV_DIR_BACKWARD;
            mx = h263_decode_motion(s, s->last_mv[1][0][0], s->b_code);
            my = h263_decode_motion(s, s->last_mv[1][0][1], s->b_code);
            s->last_mv[1][0][0]= s->mv[1][0][0] = mx;
            s->last_mv[1][0][1]= s->mv[1][0][1] = my;
            PRINT_MB_TYPE("B");
            break;
        case 3:
            s->mv_dir = MV_DIR_FORWARD;
            mx = h263_decode_motion(s, s->last_mv[0][0][0], s->f_code);
            my = h263_decode_motion(s, s->last_mv[0][0][1], s->f_code);
            s->last_mv[0][0][0]= s->mv[0][0][0] = mx;
            s->last_mv[0][0][1]= s->mv[0][0][1] = my;
            PRINT_MB_TYPE("F");
            break;
        default: 
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
        PRINT_MB_TYPE("I");
        s->ac_pred = 0;
        if (s->h263_pred || s->h263_aic) {
            s->ac_pred = get_bits1(&s->gb);
            if (s->ac_pred && s->h263_aic)
                s->h263_aic_dir = get_bits1(&s->gb);
        }
        cbpy = get_vlc2(&s->gb, cbpy_vlc.table, CBPY_VLC_BITS, 1);
        if(cbpy<0) return -1;
        cbp = (cbpc & 3) | (cbpy << 2);
        if (dquant) {
            s->qscale += quant_tab[get_bits(&s->gb, 2)];
            if (s->qscale < 1)
                s->qscale = 1;
            else if (s->qscale > 31)
                s->qscale = 31;
            h263_dc_scale(s);
        }
    }

    /* decode each block */
    if (s->h263_pred) {
	for (i = 0; i < 6; i++) {
	    if (mpeg4_decode_block(s, block[i], i, (cbp >> (5 - i)) & 1) < 0)
                return -1;
	}
    } else {
	for (i = 0; i < 6; i++) {
	    if (h263_decode_block(s, block[i], i, (cbp >> (5 - i)) & 1) < 0)
                return -1;
	}
    }
    return 0;
}

static int h263_decode_motion(MpegEncContext * s, int pred, int f_code)
{
    int code, val, sign, shift, l, m;

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
        l = (1 << (f_code - 1)) * 32;
        m = 2 * l;
        if (val < -l) {
            val += m;
        } else if (val >= l) {
            val -= m;
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

    scan_table = zigzag_direct;
    if (s->h263_aic && s->mb_intra) {
        rl = &rl_intra_aic;
        i = 0;
        if (s->ac_pred) {
            if (s->h263_aic_dir) 
                scan_table = ff_alternate_vertical_scan; /* left */
            else
                scan_table = ff_alternate_horizontal_scan; /* top */
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
        if (code < 0)
            return -1;
        if (code == rl->n) {
            /* escape */
            last = get_bits1(&s->gb);
            run = get_bits(&s->gb, 6);
            level = (INT8)get_bits(&s->gb, 8);
            if (s->h263_rv10 && level == -128) {
                /* XXX: should patch encoder too */
                level = get_bits(&s->gb, 12);
                level = (level << 20) >> 20;
            }
        } else {
            run = rl->table_run[code];
            level = rl->table_level[code];
            last = code >= rl->last;
            if (get_bits1(&s->gb))
                level = -level;
        }
        i += run;
        if (i >= 64)
            return -1;
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
                fprintf(stderr, "dc marker bit missing\n");
                return -1;
            }
        }
    }

    pred = ff_mpeg4_pred_dc(s, n, &dc_val, dir_ptr);
    level += pred;
    if (level < 0)
        level = 0;
    if (n < 4) {
        *dc_val = level * s->y_dc_scale;
    } else {
        *dc_val = level * s->c_dc_scale;
    }
    return level;
}

/**
 * decode a block
 * returns 0 if everything went ok
 * returns DECODING_AC_LOST   if an error was detected during AC decoding
 * returns DECODING_ACDC_LOST if an error was detected during DC decoding
 */
static inline int mpeg4_decode_block(MpegEncContext * s, DCTELEM * block,
                              int n, int coded)
{
    int level, i, last, run;
    int dc_pred_dir;
    RLTable *rl;
    RL_VLC_ELEM *rl_vlc;
    const UINT8 *scan_table;
    int qmul, qadd;

    if (s->mb_intra) {
	/* DC coef */
        if(s->data_partitioning && s->pict_type!=B_TYPE){
            level = s->dc_val[0][ s->block_index[n] ];
            if(n<4) level= (level + (s->y_dc_scale>>1))/s->y_dc_scale; //FIXME optimizs
            else    level= (level + (s->c_dc_scale>>1))/s->c_dc_scale;
            dc_pred_dir= (s->pred_dir_table[s->mb_x + s->mb_y*s->mb_width]<<n)&32;
        }else{
            level = mpeg4_decode_dc(s, n, &dc_pred_dir);
            if (level < 0)
                return DECODING_ACDC_LOST;
        }
        block[0] = level;
        i = 0;
        if (!coded) 
            goto not_coded;
        rl = &rl_intra;
        rl_vlc = rl_intra.rl_vlc[0];
        if (s->ac_pred) {
            if (dc_pred_dir == 0) 
                scan_table = ff_alternate_vertical_scan; /* left */
            else
                scan_table = ff_alternate_horizontal_scan; /* top */
        } else {
            scan_table = zigzag_direct;
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
        rl_vlc = rl_inter.rl_vlc[s->qscale];
        scan_table = zigzag_direct;
        qmul = s->qscale << 1;
        qadd = (s->qscale - 1) | 1;
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
                        return DECODING_AC_LOST;
                    }; SKIP_CACHE(re, &s->gb, 1);
                    
                    level= SHOW_SBITS(re, &s->gb, 12); SKIP_CACHE(re, &s->gb, 12);
 
                    if(SHOW_UBITS(re, &s->gb, 1)==0){
                        fprintf(stderr, "2. marker bit missing in 3. esc\n");
                        return DECODING_AC_LOST;
                    }; LAST_SKIP_CACHE(re, &s->gb, 1);
                    
                    SKIP_COUNTER(re, &s->gb, 1+12+1);
                    
                    if(level>512 || level<-512){ //FIXME check that QP=1 is ok with this too
                        fprintf(stderr, "|level| overflow in 3. esc\n");
                        return DECODING_AC_LOST;
                    }
#if 1 
                    {
                        const int abs_level= ABS(level);
                        if(abs_level<=MAX_LEVEL && run<=MAX_RUN && s->error_resilience>=0){
                            const int run1= run - rl->max_run[last][abs_level] - 1;
                            if(abs_level <= rl->max_level[last][run]){
                                fprintf(stderr, "illegal 3. esc, vlc encoding possible\n");
                                return DECODING_AC_LOST;
                            }
                            if(abs_level <= rl->max_level[last][run]*2){
                                fprintf(stderr, "illegal 3. esc, esc 1 encoding possible\n");
                                return DECODING_AC_LOST;
                            }
                            if(run1 >= 0 && abs_level <= rl->max_level[last][run1]){
                                fprintf(stderr, "illegal 3. esc, esc 2 encoding possible\n");
                                return DECODING_AC_LOST;
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
                return DECODING_AC_LOST;
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
                if (s->aspect_ratio_info == EXTENDED_PAR) {
                    /* aspected dimensions */
                    skip_bits(&s->gb, 8); /* width */
                    skip_bits(&s->gb, 8); /* height */
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
    int w2, h2;
    int alpha=0, beta=0;
    int w= s->width;
    int h= s->height;
//printf("SP %d\n", s->sprite_warping_accuracy);
    for(i=0; i<s->num_sprite_warping_points; i++){
        int length;
        int x=0, y=0;

        length= get_vlc(&s->gb, &sprite_trajectory);
        if(length){
            x= get_bits(&s->gb, length);
//printf("lx %d %d\n", length, x);
            if ((x >> (length - 1)) == 0) /* if MSB not set it is negative*/
                x = - (x ^ ((1 << length) - 1));
        }
        if(!(s->divx_version==500 && s->divx_build==413)) skip_bits1(&s->gb); /* marker bit */
        
        length= get_vlc(&s->gb, &sprite_trajectory);
        if(length){
            y=get_bits(&s->gb, length);
//printf("ly %d %d\n", length, y);
            if ((y >> (length - 1)) == 0) /* if MSB not set it is negative*/
                y = - (y ^ ((1 << length) - 1));
        }
        skip_bits1(&s->gb); /* marker bit */
//printf("%d %d %d %d\n", x, y, i, s->sprite_warping_accuracy);
//if(i>0 && (x!=0 || y!=0)) printf("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n");
//x=y=0;
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
            s->sprite_delta[0][0][0]= a;
            s->sprite_delta[0][0][1]= 0;
            s->sprite_delta[0][1][0]= 0;
            s->sprite_delta[0][1][1]= a;
            s->sprite_delta[1][0][0]= a;
            s->sprite_delta[1][0][1]= 0;
            s->sprite_delta[1][1][0]= 0;
            s->sprite_delta[1][1][1]= a;
            s->sprite_shift[0][0]= 0;
            s->sprite_shift[0][1]= 0;
            s->sprite_shift[1][0]= 0;
            s->sprite_shift[1][1]= 0;
            break;
        case 1: //GMC only
            s->sprite_offset[0][0]= sprite_ref[0][0] - a*vop_ref[0][0];
            s->sprite_offset[0][1]= sprite_ref[0][1] - a*vop_ref[0][1];
            s->sprite_offset[1][0]= ((sprite_ref[0][0]>>1)|(sprite_ref[0][0]&1)) - a*(vop_ref[0][0]/2);
            s->sprite_offset[1][1]= ((sprite_ref[0][1]>>1)|(sprite_ref[0][1]&1)) - a*(vop_ref[0][1]/2);
            s->sprite_delta[0][0][0]= a;
            s->sprite_delta[0][0][1]= 0;
            s->sprite_delta[0][1][0]= 0;
            s->sprite_delta[0][1][1]= a;
            s->sprite_delta[1][0][0]= a;
            s->sprite_delta[1][0][1]= 0;
            s->sprite_delta[1][1][0]= 0;
            s->sprite_delta[1][1][1]= a;
            s->sprite_shift[0][0]= 0;
            s->sprite_shift[0][1]= 0;
            s->sprite_shift[1][0]= 0;
            s->sprite_shift[1][1]= 0;
            break;
        case 2:
        case 3: //FIXME
            s->sprite_offset[0][0]= (sprite_ref[0][0]<<(alpha+rho))
                                                  + ((-r*sprite_ref[0][0] + virtual_ref[0][0])*(-vop_ref[0][0])
                                                    +( r*sprite_ref[0][1] - virtual_ref[0][1])*(-vop_ref[0][1]));
            s->sprite_offset[0][1]= (sprite_ref[0][1]<<(alpha+rho))
                                                  + ((-r*sprite_ref[0][1] + virtual_ref[0][1])*(-vop_ref[0][0])
                                                    +(-r*sprite_ref[0][0] + virtual_ref[0][0])*(-vop_ref[0][1]));
            s->sprite_offset[1][0]= ((-r*sprite_ref[0][0] + virtual_ref[0][0])*(-2*vop_ref[0][0] + 1)
                                 +( r*sprite_ref[0][1] - virtual_ref[0][1])*(-2*vop_ref[0][1] + 1)
                                 +2*w2*r*sprite_ref[0][0] - 16*w2);
            s->sprite_offset[1][1]= ((-r*sprite_ref[0][1] + virtual_ref[0][1])*(-2*vop_ref[0][0] + 1) 
                                 +(-r*sprite_ref[0][0] + virtual_ref[0][0])*(-2*vop_ref[0][1] + 1)
                                 +2*w2*r*sprite_ref[0][1] - 16*w2);
            s->sprite_delta[0][0][0]=   (-r*sprite_ref[0][0] + virtual_ref[0][0]);
            s->sprite_delta[0][0][1]=   ( r*sprite_ref[0][1] - virtual_ref[0][1]);
            s->sprite_delta[0][1][0]=   (-r*sprite_ref[0][1] + virtual_ref[0][1]);
            s->sprite_delta[0][1][1]=   (-r*sprite_ref[0][0] + virtual_ref[0][0]);
            s->sprite_delta[1][0][0]= 4*(-r*sprite_ref[0][0] + virtual_ref[0][0]);
            s->sprite_delta[1][0][1]= 4*( r*sprite_ref[0][1] - virtual_ref[0][1]);
            s->sprite_delta[1][1][0]= 4*(-r*sprite_ref[0][1] + virtual_ref[0][1]);
            s->sprite_delta[1][1][1]= 4*(-r*sprite_ref[0][0] + virtual_ref[0][0]);
            s->sprite_shift[0][0]= alpha+rho;
            s->sprite_shift[0][1]= alpha+rho;
            s->sprite_shift[1][0]= alpha+rho+2;
            s->sprite_shift[1][1]= alpha+rho+2;
            break;
//        case 3:
            break;
    }
/*printf("%d %d\n", s->sprite_delta[0][0][0], a<<s->sprite_shift[0][0]);
printf("%d %d\n", s->sprite_delta[0][0][1], 0);
printf("%d %d\n", s->sprite_delta[0][1][0], 0);
printf("%d %d\n", s->sprite_delta[0][1][1], a<<s->sprite_shift[0][1]);
printf("%d %d\n", s->sprite_delta[1][0][0], a<<s->sprite_shift[1][0]);
printf("%d %d\n", s->sprite_delta[1][0][1], 0);
printf("%d %d\n", s->sprite_delta[1][1][0], 0);
printf("%d %d\n", s->sprite_delta[1][1][1], a<<s->sprite_shift[1][1]);*/
    /* try to simplify the situation */ 
    if(   s->sprite_delta[0][0][0] == a<<s->sprite_shift[0][0]
       && s->sprite_delta[0][0][1] == 0
       && s->sprite_delta[0][1][0] == 0
       && s->sprite_delta[0][1][1] == a<<s->sprite_shift[0][1]
       && s->sprite_delta[1][0][0] == a<<s->sprite_shift[1][0]
       && s->sprite_delta[1][0][1] == 0
       && s->sprite_delta[1][1][0] == 0
       && s->sprite_delta[1][1][1] == a<<s->sprite_shift[1][1])
    {
        s->sprite_offset[0][0]>>=s->sprite_shift[0][0];
        s->sprite_offset[0][1]>>=s->sprite_shift[0][1];
        s->sprite_offset[1][0]>>=s->sprite_shift[1][0];
        s->sprite_offset[1][1]>>=s->sprite_shift[1][1];
        s->sprite_delta[0][0][0]= a;
        s->sprite_delta[0][0][1]= 0;
        s->sprite_delta[0][1][0]= 0;
        s->sprite_delta[0][1][1]= a;
        s->sprite_delta[1][0][0]= a;
        s->sprite_delta[1][0][1]= 0;
        s->sprite_delta[1][1][0]= 0;
        s->sprite_delta[1][1][1]= a;
        s->sprite_shift[0][0]= 0;
        s->sprite_shift[0][1]= 0;
        s->sprite_shift[1][0]= 0;
        s->sprite_shift[1][1]= 0;
        s->real_sprite_warping_points=1;
    }
    else
        s->real_sprite_warping_points= s->num_sprite_warping_points;

//printf("%d %d %d %d\n", d[0][0], d[0][1], s->sprite_offset[0][0], s->sprite_offset[0][1]);
}

/* decode mpeg4 VOP header */
int mpeg4_decode_picture_header(MpegEncContext * s)
{
    int time_incr, startcode, state, v;
    int time_increment;

 redo:
    /* search next start code */
    align_get_bits(&s->gb);
    state = 0xff;
    for(;;) {
        v = get_bits(&s->gb, 8);
        if (state == 0x000001) {
            state = ((state << 8) | v) & 0xffffff;
            startcode = state;
            break;
        }
        state = ((state << 8) | v) & 0xffffff;
        if( get_bits_count(&s->gb) > s->gb.size*8-32){
            if(s->gb.size>50){
                printf("no VOP startcode found, frame size was=%d\n", s->gb.size);
                return -1;
            }else{
                printf("frame skip\n");
                return FRAME_SKIPED;
            }
        }
    }
//printf("startcode %X %d\n", startcode, get_bits_count(&s->gb));
    if (startcode == 0x120) { // Video Object Layer
        int width, height, vo_ver_id;

        /* vol header */
        skip_bits(&s->gb, 1); /* random access */
        s->vo_type= get_bits(&s->gb, 8);
        if (get_bits1(&s->gb) != 0) { /* is_ol_id */
            vo_ver_id = get_bits(&s->gb, 4); /* vo_ver_id */
            skip_bits(&s->gb, 3); /* vo_priority */
        } else {
            vo_ver_id = 1;
        }
//printf("vo type:%d\n",s->vo_type);
        s->aspect_ratio_info= get_bits(&s->gb, 4);
	if(s->aspect_ratio_info == EXTENDED_PAR){
            skip_bits(&s->gb, 8); //par_width
            skip_bits(&s->gb, 8); // par_height
        }

        if ((s->vol_control_parameters=get_bits1(&s->gb))) { /* vol control parameter */
            int chroma_format= get_bits(&s->gb, 2);
            if(chroma_format!=1){
                printf("illegal chroma format\n");
            }
            s->low_delay= get_bits1(&s->gb);
            if(get_bits1(&s->gb)){ /* vbv parameters */
                printf("vbv parameters not supported\n");
                return -1;
            }
        }else{
            s->low_delay=0;
        }

        s->shape = get_bits(&s->gb, 2); /* vol shape */
        if(s->shape != RECT_SHAPE) printf("only rectangular vol supported\n");
        if(s->shape == GRAY_SHAPE && vo_ver_id != 1){
            printf("Gray shape not supported\n");
            skip_bits(&s->gb, 4);  //video_object_layer_shape_extension
        }

        skip_bits1(&s->gb);   /* marker */
        
        s->time_increment_resolution = get_bits(&s->gb, 16);
        s->time_increment_bits = av_log2(s->time_increment_resolution - 1) + 1;
        if (s->time_increment_bits < 1)
            s->time_increment_bits = 1;
        skip_bits1(&s->gb);   /* marker */

        if (get_bits1(&s->gb) != 0) {   /* fixed_vop_rate  */
            skip_bits(&s->gb, s->time_increment_bits);
        }

        if (s->shape != BIN_ONLY_SHAPE) {
            if (s->shape == RECT_SHAPE) {
                skip_bits1(&s->gb);   /* marker */
                width = get_bits(&s->gb, 13);
                skip_bits1(&s->gb);   /* marker */
                height = get_bits(&s->gb, 13);
                skip_bits1(&s->gb);   /* marker */
                if(width && height){ /* they should be non zero but who knows ... */
                    s->width = width;
                    s->height = height;
//                    printf("width/height: %d %d\n", width, height);
                }
            }
            
            if(get_bits1(&s->gb)) printf("interlaced not supported\n");   /* interlaced */
            if(!get_bits1(&s->gb)) printf("OBMC not supported (very likely buggy encoder)\n");   /* OBMC Disable */
            if (vo_ver_id == 1) {
                s->vol_sprite_usage = get_bits1(&s->gb); /* vol_sprite_usage */
            } else {
                s->vol_sprite_usage = get_bits(&s->gb, 2); /* vol_sprite_usage */
            }
            if(s->vol_sprite_usage==STATIC_SPRITE) printf("Static Sprites not supported\n");
            if(s->vol_sprite_usage==STATIC_SPRITE || s->vol_sprite_usage==GMC_SPRITE){
                if(s->vol_sprite_usage==STATIC_SPRITE){
                    s->sprite_width = get_bits(&s->gb, 13);
                    skip_bits1(&s->gb); /* marker */
                    s->sprite_height= get_bits(&s->gb, 13);
                    skip_bits1(&s->gb); /* marker */
                    s->sprite_left  = get_bits(&s->gb, 13);
                    skip_bits1(&s->gb); /* marker */
                    s->sprite_top   = get_bits(&s->gb, 13);
                    skip_bits1(&s->gb); /* marker */
                }
                s->num_sprite_warping_points= get_bits(&s->gb, 6);
                s->sprite_warping_accuracy = get_bits(&s->gb, 2);
                s->sprite_brightness_change= get_bits1(&s->gb);
                if(s->vol_sprite_usage==STATIC_SPRITE)
                    s->low_latency_sprite= get_bits1(&s->gb);            
            }
            // FIXME sadct disable bit if verid!=1 && shape not rect
            
            if (get_bits1(&s->gb) == 1) {   /* not_8_bit */
                s->quant_precision = get_bits(&s->gb, 4); /* quant_precision */
                if(get_bits(&s->gb, 4)!=8) printf("N-bit not supported\n"); /* bits_per_pixel */
                if(s->quant_precision!=5) printf("quant precission %d\n", s->quant_precision);
            } else {
                s->quant_precision = 5;
            }
            
            // FIXME a bunch of grayscale shape things

            if(get_bits1(&s->gb)){ /* vol_quant_type */
                int i, j, v;
                /* load default matrixes */
                for(i=0; i<64; i++){
                    v= ff_mpeg4_default_intra_matrix[i];
                    s->intra_matrix[i]= v;
                    s->chroma_intra_matrix[i]= v;
                    
                    v= ff_mpeg4_default_non_intra_matrix[i];
                    s->inter_matrix[i]= v;
                    s->chroma_inter_matrix[i]= v;
                }

                /* load custom intra matrix */
                if(get_bits1(&s->gb)){
                    for(i=0; i<64; i++){
                        v= get_bits(&s->gb, 8);
                        if(v==0) break;

                        j= zigzag_direct[i];
                        s->intra_matrix[j]= v;
                        s->chroma_intra_matrix[j]= v;
                    }
                }

                /* load custom non intra matrix */
                if(get_bits1(&s->gb)){
                    for(i=0; i<64; i++){
                        v= get_bits(&s->gb, 8);
                        if(v==0) break;

                        j= zigzag_direct[i];
                        s->inter_matrix[j]= v;
                        s->chroma_inter_matrix[j]= v;
                    }

                    /* replicate last value */
                    for(; i<64; i++){
                        j= zigzag_direct[i];
                        s->inter_matrix[j]= v;
                        s->chroma_inter_matrix[j]= v;
                    }
                }

                s->dct_unquantize= s->dct_unquantize_mpeg2;

                // FIXME a bunch of grayscale shape things
            }else
                s->dct_unquantize= s->dct_unquantize_h263;

            if(vo_ver_id != 1)
                 s->quarter_sample= get_bits1(&s->gb);
            else s->quarter_sample=0;

            if(!get_bits1(&s->gb)) printf("Complexity estimation not supported\n");

            s->resync_marker= !get_bits1(&s->gb); /* resync_marker_disabled */

            s->data_partitioning= get_bits1(&s->gb);
            if(s->data_partitioning){
                s->rvlc= get_bits1(&s->gb);
                if(s->rvlc){
                    printf("reversible vlc not supported\n");
                }
            }
            
            if(vo_ver_id != 1) {
                s->new_pred= get_bits1(&s->gb);
                if(s->new_pred){
                    printf("new pred not supported\n");
                    skip_bits(&s->gb, 2); /* requested upstream message type */
                    skip_bits1(&s->gb); /* newpred segment type */
                }
                s->reduced_res_vop= get_bits1(&s->gb);
                if(s->reduced_res_vop) printf("reduced resolution VOP not supported\n");
            }
            else{
                s->new_pred=0;
                s->reduced_res_vop= 0;
            }

            s->scalability= get_bits1(&s->gb);
            if (s->scalability) {
                printf("scalability not supported\n");
            }
        }
//printf("end Data %X %d\n", show_bits(&s->gb, 32), get_bits_count(&s->gb)&0x7);
        goto redo;
    } else if (startcode == 0x1b2) { //userdata
        char buf[256];
        int i;
        int e;
        int ver, build;

//printf("user Data %X\n", show_bits(&s->gb, 32));
        buf[0]= show_bits(&s->gb, 8);
        for(i=1; i<256; i++){
            buf[i]= show_bits(&s->gb, 16)&0xFF;
            if(buf[i]==0) break;
            skip_bits(&s->gb, 8);
        }
        buf[255]=0;
        e=sscanf(buf, "DivX%dBuild%d", &ver, &build);
        if(e!=2)
            e=sscanf(buf, "DivX%db%d", &ver, &build);
        if(e==2){
            s->divx_version= ver;
            s->divx_build= build;
            if(s->picture_number==0){
                printf("This file was encoded with DivX%d Build%d\n", ver, build);
                if(ver==500 && build==413){
                    printf("WARNING: this version of DivX is not MPEG4 compatible, trying to workaround these bugs...\n");
#if 0
                }else{
                    printf("hmm, i havnt seen that version of divx yet, lets assume they fixed these bugs ...\n"
                           "using mpeg4 decoder, if it fails contact the developers (of ffmpeg)\n");
#endif
                }
            }
        }
//printf("User Data: %s\n", buf);
        goto redo;
    } else if (startcode != 0x1b6) { //VOP
        goto redo;
    }

    s->pict_type = get_bits(&s->gb, 2) + I_TYPE;	/* pict type: I = 0 , P = 1 */
//if(s->pict_type!=I_TYPE) return FRAME_SKIPED;
    if(s->pict_type==B_TYPE && s->low_delay && s->vol_control_parameters==0){
        printf("low_delay flag set, but shouldnt, clearing it\n");
        s->low_delay=0;
    }
// printf("pic: %d, qpel:%d\n", s->pict_type, s->quarter_sample); 
//printf("%d", s->pict_type);
    time_incr=0;
    while (get_bits1(&s->gb) != 0) 
        time_incr++;

    check_marker(&s->gb, "before time_increment");
    time_increment= get_bits(&s->gb, s->time_increment_bits);
//printf(" type:%d incr:%d increment:%d\n", s->pict_type, time_incr, time_increment);
    if(s->pict_type!=B_TYPE){
        s->last_time_base= s->time_base;
        s->time_base+= time_incr;
        s->time= s->time_base*s->time_increment_resolution + time_increment;
        s->pp_time= s->time - s->last_non_b_time;
        s->last_non_b_time= s->time;
    }else{
        s->time= (s->last_time_base + time_incr)*s->time_increment_resolution + time_increment;
        s->bp_time= s->last_non_b_time - s->time;
        if(s->pp_time <=s->bp_time){
//            printf("messed up order, seeking?, skiping current b frame\n");
            return FRAME_SKIPED;
        }
    }

    if(check_marker(&s->gb, "before vop_coded")==0 && s->picture_number==0){
        printf("hmm, seems the headers arnt complete, trying to guess time_increment_bits\n");
        for(s->time_increment_bits++ ;s->time_increment_bits<16; s->time_increment_bits++){
            if(get_bits1(&s->gb)) break;
        }
        printf("my guess is %d bits ;)\n",s->time_increment_bits);
    }
    /* vop coded */
    if (get_bits1(&s->gb) != 1)
        goto redo;
//printf("time %d %d %d || %d %d %d\n", s->time_increment_bits, s->time_increment, s->time_base,
//s->time, s->last_non_b_time[0], s->last_non_b_time[1]);  
    if (s->shape != BIN_ONLY_SHAPE && ( s->pict_type == P_TYPE
                          || (s->pict_type == S_TYPE && s->vol_sprite_usage==GMC_SPRITE))) {
        /* rounding type for motion estimation */
	s->no_rounding = get_bits1(&s->gb);
    } else {
	s->no_rounding = 0;
    }
//FIXME reduced res stuff

     if (s->shape != RECT_SHAPE) {
         if (s->vol_sprite_usage != 1 || s->pict_type != I_TYPE) {
             int width, height, hor_spat_ref, ver_spat_ref;
 
             width = get_bits(&s->gb, 13);
             skip_bits1(&s->gb);   /* marker */
             height = get_bits(&s->gb, 13);
             skip_bits1(&s->gb);   /* marker */
             hor_spat_ref = get_bits(&s->gb, 13); /* hor_spat_ref */
             skip_bits1(&s->gb);   /* marker */
             ver_spat_ref = get_bits(&s->gb, 13); /* ver_spat_ref */
         }
         skip_bits1(&s->gb); /* change_CR_disable */
 
         if (get_bits1(&s->gb) != 0) {
             skip_bits(&s->gb, 8); /* constant_alpha_value */
         }
     }
//FIXME complexity estimation stuff
     
     if (s->shape != BIN_ONLY_SHAPE) {
         int t;
         t=get_bits(&s->gb, 3); /* intra dc VLC threshold */
//printf("threshold %d\n", t);
         //FIXME interlaced specific bits
     }

     if(s->pict_type == S_TYPE && (s->vol_sprite_usage==STATIC_SPRITE || s->vol_sprite_usage==GMC_SPRITE)){
         if(s->num_sprite_warping_points){
             mpeg4_decode_sprite_trajectory(s);
         }
         if(s->sprite_brightness_change) printf("sprite_brightness_change not supported\n");
         if(s->vol_sprite_usage==STATIC_SPRITE) printf("static sprite not supported\n");
     }

     if (s->shape != BIN_ONLY_SHAPE) {
         /* note: we do not use quant_precision to avoid problem if no
            MPEG4 vol header as it is found on some old opendivx
            movies */
         s->qscale = get_bits(&s->gb, 5);
         if(s->qscale==0){
             printf("Error, header damaged or not MPEG4 header (qscale=0)\n");
             return -1; // makes no sense to continue, as there is nothing left from the image then
         }
  
         if (s->pict_type != I_TYPE) {
             s->f_code = get_bits(&s->gb, 3);	/* fcode_for */
             if(s->f_code==0){
                 printf("Error, header damaged or not MPEG4 header (f_code=0)\n");
                 return -1; // makes no sense to continue, as the MV decoding will break very quickly
             }
         }
         if (s->pict_type == B_TYPE) {
             s->b_code = get_bits(&s->gb, 3);
//printf("b-code %d\n", s->b_code);
         }
//printf("quant:%d fcode:%d bcode:%d type:%d\n", s->qscale, s->f_code, s->b_code, s->pict_type);
         if(!s->scalability){
             if (s->shape!=RECT_SHAPE && s->pict_type!=I_TYPE) {
                 skip_bits1(&s->gb); // vop shape coding type
             }
         }
     }
     /* detect buggy encoders which dont set the low_delay flag (divx4/xvid/opendivx)*/
     // note we cannot detect divx5 without b-frames easyly (allthough its buggy too)
     if(s->vo_type==0 && s->vol_control_parameters==0 && s->divx_version==0 && s->picture_number==0){
         printf("looks like this file was encoded with (divx4/(old)xvid/opendivx) -> forcing low_delay flag\n");
         s->low_delay=1;
     }

     s->picture_number++; // better than pic number==0 allways ;)
//printf("done\n");

     s->y_dc_scale_table= ff_mpeg4_y_dc_scale_table; //FIXME add short header support 
     s->c_dc_scale_table= ff_mpeg4_c_dc_scale_table;

     return 0;
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
    return 0;
}

