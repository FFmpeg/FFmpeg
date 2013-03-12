/*
 * H263/MPEG4 backend for encoder and decoder
 * Copyright (c) 2000,2001 Fabrice Bellard
 * H263+ support.
 * Copyright (c) 2001 Juan J. Sierralta P
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
 * h263/mpeg4 codec.
 */

//#define DEBUG
#include <limits.h>

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

uint8_t ff_h263_static_rl_table_store[2][2][2*MAX_RUN + MAX_LEVEL + 3];


void ff_h263_update_motion_val(MpegEncContext * s){
    const int mb_xy = s->mb_y * s->mb_stride + s->mb_x;
               //FIXME a lot of that is only needed for !low_delay
    const int wrap = s->b8_stride;
    const int xy = s->block_index[0];

    s->current_picture.mbskip_table[mb_xy] = s->mb_skipped;

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
            s->current_picture.ref_index[0][4*mb_xy    ] =
            s->current_picture.ref_index[0][4*mb_xy + 1] = s->field_select[0][0];
            s->current_picture.ref_index[0][4*mb_xy + 2] =
            s->current_picture.ref_index[0][4*mb_xy + 3] = s->field_select[0][1];
        }

        /* no update if 8X8 because it has been done during parsing */
        s->current_picture.motion_val[0][xy][0]            = motion_x;
        s->current_picture.motion_val[0][xy][1]            = motion_y;
        s->current_picture.motion_val[0][xy + 1][0]        = motion_x;
        s->current_picture.motion_val[0][xy + 1][1]        = motion_y;
        s->current_picture.motion_val[0][xy + wrap][0]     = motion_x;
        s->current_picture.motion_val[0][xy + wrap][1]     = motion_y;
        s->current_picture.motion_val[0][xy + 1 + wrap][0] = motion_x;
        s->current_picture.motion_val[0][xy + 1 + wrap][1] = motion_y;
    }

    if(s->encoding){ //FIXME encoding MUST be cleaned up
        if (s->mv_type == MV_TYPE_8X8)
            s->current_picture.mb_type[mb_xy] = MB_TYPE_L0 | MB_TYPE_8x8;
        else if(s->mb_intra)
            s->current_picture.mb_type[mb_xy] = MB_TYPE_INTRA;
        else
            s->current_picture.mb_type[mb_xy] = MB_TYPE_L0 | MB_TYPE_16x16;
    }
}

int ff_h263_pred_dc(MpegEncContext * s, int n, int16_t **dc_val_ptr)
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

void ff_h263_loop_filter(MpegEncContext * s){
    int qp_c;
    const int linesize  = s->linesize;
    const int uvlinesize= s->uvlinesize;
    const int xy = s->mb_y * s->mb_stride + s->mb_x;
    uint8_t *dest_y = s->dest[0];
    uint8_t *dest_cb= s->dest[1];
    uint8_t *dest_cr= s->dest[2];

//    if(s->pict_type==AV_PICTURE_TYPE_B && !s->readable) return;

    /*
       Diag Top
       Left Center
    */
    if (!IS_SKIP(s->current_picture.mb_type[xy])) {
        qp_c= s->qscale;
        s->dsp.h263_v_loop_filter(dest_y+8*linesize  , linesize, qp_c);
        s->dsp.h263_v_loop_filter(dest_y+8*linesize+8, linesize, qp_c);
    }else
        qp_c= 0;

    if(s->mb_y){
        int qp_dt, qp_tt, qp_tc;

        if (IS_SKIP(s->current_picture.mb_type[xy - s->mb_stride]))
            qp_tt=0;
        else
            qp_tt = s->current_picture.qscale_table[xy - s->mb_stride];

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
            if (qp_tt || IS_SKIP(s->current_picture.mb_type[xy - 1 - s->mb_stride]))
                qp_dt= qp_tt;
            else
                qp_dt = s->current_picture.qscale_table[xy - 1 - s->mb_stride];

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
        if (qp_c || IS_SKIP(s->current_picture.mb_type[xy - 1]))
            qp_lc= qp_c;
        else
            qp_lc = s->current_picture.qscale_table[xy - 1];

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

void ff_h263_pred_acdc(MpegEncContext * s, int16_t *block, int n)
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

int16_t *ff_h263_pred_motion(MpegEncContext * s, int block, int dir,
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
