/*
 * H.26L/H.264/AVC/JVT/14496-10/... motion vector predicion
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
 * H.264 / AVC / MPEG4 part10 motion vector predicion.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#ifndef AVCODEC_H264_MVPRED_H
#define AVCODEC_H264_MVPRED_H

#include "internal.h"
#include "avcodec.h"
#include "h264.h"

//#undef NDEBUG
#include <assert.h>

static av_always_inline int fetch_diagonal_mv(H264Context *h, const int16_t **C, int i, int list, int part_width){
    const int topright_ref= h->ref_cache[list][ i - 8 + part_width ];
    MpegEncContext *s = &h->s;

    /* there is no consistent mapping of mvs to neighboring locations that will
     * make mbaff happy, so we can't move all this logic to fill_caches */
    if(FRAME_MBAFF){

#define SET_DIAG_MV(MV_OP, REF_OP, XY, Y4)\
                const int xy = XY, y4 = Y4;\
                const int mb_type = mb_types[xy+(y4>>2)*s->mb_stride];\
                if(!USES_LIST(mb_type,list))\
                    return LIST_NOT_USED;\
                mv = s->current_picture_ptr->motion_val[list][h->mb2b_xy[xy]+3 + y4*h->b_stride];\
                h->mv_cache[list][scan8[0]-2][0] = mv[0];\
                h->mv_cache[list][scan8[0]-2][1] = mv[1] MV_OP;\
                return s->current_picture_ptr->ref_index[list][4*xy+1 + (y4&~1)] REF_OP;

        if(topright_ref == PART_NOT_AVAILABLE
           && i >= scan8[0]+8 && (i&7)==4
           && h->ref_cache[list][scan8[0]-1] != PART_NOT_AVAILABLE){
            const uint32_t *mb_types = s->current_picture_ptr->mb_type;
            const int16_t *mv;
            AV_ZERO32(h->mv_cache[list][scan8[0]-2]);
            *C = h->mv_cache[list][scan8[0]-2];

            if(!MB_FIELD
               && IS_INTERLACED(h->left_type[0])){
                SET_DIAG_MV(*2, >>1, h->left_mb_xy[0]+s->mb_stride, (s->mb_y&1)*2+(i>>5));
            }
            if(MB_FIELD
               && !IS_INTERLACED(h->left_type[0])){
                // left shift will turn LIST_NOT_USED into PART_NOT_AVAILABLE, but that's OK.
                SET_DIAG_MV(/2, <<1, h->left_mb_xy[i>=36], ((i>>2))&3);
            }
        }
#undef SET_DIAG_MV
    }

    if(topright_ref != PART_NOT_AVAILABLE){
        *C= h->mv_cache[list][ i - 8 + part_width ];
        return topright_ref;
    }else{
        tprintf(s->avctx, "topright MV not available\n");

        *C= h->mv_cache[list][ i - 8 - 1 ];
        return h->ref_cache[list][ i - 8 - 1 ];
    }
}

/**
 * gets the predicted MV.
 * @param n the block index
 * @param part_width the width of the partition (4, 8,16) -> (1, 2, 4)
 * @param mx the x component of the predicted motion vector
 * @param my the y component of the predicted motion vector
 */
static av_always_inline void pred_motion(H264Context * const h, int n, int part_width, int list, int ref, int * const mx, int * const my){
    const int index8= scan8[n];
    const int top_ref=      h->ref_cache[list][ index8 - 8 ];
    const int left_ref=     h->ref_cache[list][ index8 - 1 ];
    const int16_t * const A= h->mv_cache[list][ index8 - 1 ];
    const int16_t * const B= h->mv_cache[list][ index8 - 8 ];
    const int16_t * C;
    int diagonal_ref, match_count;

    assert(part_width==1 || part_width==2 || part_width==4);

/* mv_cache
  B . . A T T T T
  U . . L . . , .
  U . . L . . . .
  U . . L . . , .
  . . . L . . . .
*/

    diagonal_ref= fetch_diagonal_mv(h, &C, index8, list, part_width);
    match_count= (diagonal_ref==ref) + (top_ref==ref) + (left_ref==ref);
    tprintf(h->s.avctx, "pred_motion match_count=%d\n", match_count);
    if(match_count > 1){ //most common
        *mx= mid_pred(A[0], B[0], C[0]);
        *my= mid_pred(A[1], B[1], C[1]);
    }else if(match_count==1){
        if(left_ref==ref){
            *mx= A[0];
            *my= A[1];
        }else if(top_ref==ref){
            *mx= B[0];
            *my= B[1];
        }else{
            *mx= C[0];
            *my= C[1];
        }
    }else{
        if(top_ref == PART_NOT_AVAILABLE && diagonal_ref == PART_NOT_AVAILABLE && left_ref != PART_NOT_AVAILABLE){
            *mx= A[0];
            *my= A[1];
        }else{
            *mx= mid_pred(A[0], B[0], C[0]);
            *my= mid_pred(A[1], B[1], C[1]);
        }
    }

    tprintf(h->s.avctx, "pred_motion (%2d %2d %2d) (%2d %2d %2d) (%2d %2d %2d) -> (%2d %2d %2d) at %2d %2d %d list %d\n", top_ref, B[0], B[1],                    diagonal_ref, C[0], C[1], left_ref, A[0], A[1], ref, *mx, *my, h->s.mb_x, h->s.mb_y, n, list);
}

/**
 * gets the directionally predicted 16x8 MV.
 * @param n the block index
 * @param mx the x component of the predicted motion vector
 * @param my the y component of the predicted motion vector
 */
static av_always_inline void pred_16x8_motion(H264Context * const h, int n, int list, int ref, int * const mx, int * const my){
    if(n==0){
        const int top_ref=      h->ref_cache[list][ scan8[0] - 8 ];
        const int16_t * const B= h->mv_cache[list][ scan8[0] - 8 ];

        tprintf(h->s.avctx, "pred_16x8: (%2d %2d %2d) at %2d %2d %d list %d\n", top_ref, B[0], B[1], h->s.mb_x, h->s.mb_y, n, list);

        if(top_ref == ref){
            *mx= B[0];
            *my= B[1];
            return;
        }
    }else{
        const int left_ref=     h->ref_cache[list][ scan8[8] - 1 ];
        const int16_t * const A= h->mv_cache[list][ scan8[8] - 1 ];

        tprintf(h->s.avctx, "pred_16x8: (%2d %2d %2d) at %2d %2d %d list %d\n", left_ref, A[0], A[1], h->s.mb_x, h->s.mb_y, n, list);

        if(left_ref == ref){
            *mx= A[0];
            *my= A[1];
            return;
        }
    }

    //RARE
    pred_motion(h, n, 4, list, ref, mx, my);
}

/**
 * gets the directionally predicted 8x16 MV.
 * @param n the block index
 * @param mx the x component of the predicted motion vector
 * @param my the y component of the predicted motion vector
 */
static av_always_inline void pred_8x16_motion(H264Context * const h, int n, int list, int ref, int * const mx, int * const my){
    if(n==0){
        const int left_ref=      h->ref_cache[list][ scan8[0] - 1 ];
        const int16_t * const A=  h->mv_cache[list][ scan8[0] - 1 ];

        tprintf(h->s.avctx, "pred_8x16: (%2d %2d %2d) at %2d %2d %d list %d\n", left_ref, A[0], A[1], h->s.mb_x, h->s.mb_y, n, list);

        if(left_ref == ref){
            *mx= A[0];
            *my= A[1];
            return;
        }
    }else{
        const int16_t * C;
        int diagonal_ref;

        diagonal_ref= fetch_diagonal_mv(h, &C, scan8[4], list, 2);

        tprintf(h->s.avctx, "pred_8x16: (%2d %2d %2d) at %2d %2d %d list %d\n", diagonal_ref, C[0], C[1], h->s.mb_x, h->s.mb_y, n, list);

        if(diagonal_ref == ref){
            *mx= C[0];
            *my= C[1];
            return;
        }
    }

    //RARE
    pred_motion(h, n, 2, list, ref, mx, my);
}

#define FIX_MV_MBAFF(type, refn, mvn, idx)\
    if(FRAME_MBAFF){\
        if(MB_FIELD){\
            if(!IS_INTERLACED(type)){\
                refn <<= 1;\
                AV_COPY32(mvbuf[idx], mvn);\
                mvbuf[idx][1] /= 2;\
                mvn = mvbuf[idx];\
            }\
        }else{\
            if(IS_INTERLACED(type)){\
                refn >>= 1;\
                AV_COPY32(mvbuf[idx], mvn);\
                mvbuf[idx][1] <<= 1;\
                mvn = mvbuf[idx];\
            }\
        }\
    }

static av_always_inline void pred_pskip_motion(H264Context * const h){
    DECLARE_ALIGNED(4, static const int16_t, zeromv)[2] = {0};
    DECLARE_ALIGNED(4, int16_t, mvbuf)[3][2];
    MpegEncContext * const s = &h->s;
    int8_t *ref = s->current_picture.ref_index[0];
    int16_t (*mv)[2] = s->current_picture.motion_val[0];
    int top_ref, left_ref, diagonal_ref, match_count, mx, my;
    const int16_t *A, *B, *C;
    int b_stride = h->b_stride;

    fill_rectangle(&h->ref_cache[0][scan8[0]], 4, 4, 8, 0, 1);

    /* To avoid doing an entire fill_decode_caches, we inline the relevant parts here.
     * FIXME: this is a partial duplicate of the logic in fill_decode_caches, but it's
     * faster this way.  Is there a way to avoid this duplication?
     */
    if(USES_LIST(h->left_type[LTOP], 0)){
        left_ref = ref[4*h->left_mb_xy[LTOP] + 1 + (h->left_block[0]&~1)];
        A = mv[h->mb2b_xy[h->left_mb_xy[LTOP]] + 3 + b_stride*h->left_block[0]];
        FIX_MV_MBAFF(h->left_type[LTOP], left_ref, A, 0);
        if(!(left_ref | AV_RN32A(A))){
            goto zeromv;
        }
    }else if(h->left_type[LTOP]){
        left_ref = LIST_NOT_USED;
        A = zeromv;
    }else{
        goto zeromv;
    }

    if(USES_LIST(h->top_type, 0)){
        top_ref = ref[4*h->top_mb_xy + 2];
        B = mv[h->mb2b_xy[h->top_mb_xy] + 3*b_stride];
        FIX_MV_MBAFF(h->top_type, top_ref, B, 1);
        if(!(top_ref | AV_RN32A(B))){
            goto zeromv;
        }
    }else if(h->top_type){
        top_ref = LIST_NOT_USED;
        B = zeromv;
    }else{
        goto zeromv;
    }

    tprintf(h->s.avctx, "pred_pskip: (%d) (%d) at %2d %2d\n", top_ref, left_ref, h->s.mb_x, h->s.mb_y);

    if(USES_LIST(h->topright_type, 0)){
        diagonal_ref = ref[4*h->topright_mb_xy + 2];
        C = mv[h->mb2b_xy[h->topright_mb_xy] + 3*b_stride];
        FIX_MV_MBAFF(h->topright_type, diagonal_ref, C, 2);
    }else if(h->topright_type){
        diagonal_ref = LIST_NOT_USED;
        C = zeromv;
    }else{
        if(USES_LIST(h->topleft_type, 0)){
            diagonal_ref = ref[4*h->topleft_mb_xy + 1 + (h->topleft_partition & 2)];
            C = mv[h->mb2b_xy[h->topleft_mb_xy] + 3 + b_stride + (h->topleft_partition & 2*b_stride)];
            FIX_MV_MBAFF(h->topleft_type, diagonal_ref, C, 2);
        }else if(h->topleft_type){
            diagonal_ref = LIST_NOT_USED;
            C = zeromv;
        }else{
            diagonal_ref = PART_NOT_AVAILABLE;
            C = zeromv;
        }
    }

    match_count= !diagonal_ref + !top_ref + !left_ref;
    tprintf(h->s.avctx, "pred_pskip_motion match_count=%d\n", match_count);
    if(match_count > 1){
        mx = mid_pred(A[0], B[0], C[0]);
        my = mid_pred(A[1], B[1], C[1]);
    }else if(match_count==1){
        if(!left_ref){
            mx = A[0];
            my = A[1];
        }else if(!top_ref){
            mx = B[0];
            my = B[1];
        }else{
            mx = C[0];
            my = C[1];
        }
    }else{
        mx = mid_pred(A[0], B[0], C[0]);
        my = mid_pred(A[1], B[1], C[1]);
    }

    fill_rectangle( h->mv_cache[0][scan8[0]], 4, 4, 8, pack16to32(mx,my), 4);
    return;
zeromv:
    fill_rectangle( h->mv_cache[0][scan8[0]], 4, 4, 8, 0, 4);
    return;
}

#endif /* AVCODEC_H264_MVPRED_H */
