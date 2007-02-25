/*
 * Chinese AVS video (AVS1-P2, JiZhun profile) decoder.
 * Copyright (c) 2006  Stefan Gehrer <stefan.gehrer@gmx.de>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * @file cavs.c
 * Chinese AVS video (AVS1-P2, JiZhun profile) decoder
 * @author Stefan Gehrer <stefan.gehrer@gmx.de>
 */

#include "avcodec.h"
#include "bitstream.h"
#include "golomb.h"
#include "mpegvideo.h"
#include "cavsdata.h"

#ifdef CONFIG_CAVS_DECODER
typedef struct {
    MpegEncContext s;
    Picture picture; ///< currently decoded frame
    Picture DPB[2];  ///< reference frames
    int dist[2];     ///< temporal distances from current frame to ref frames
    int profile, level;
    int aspect_ratio;
    int mb_width, mb_height;
    int pic_type;
    int progressive;
    int pic_structure;
    int skip_mode_flag; ///< select between skip_count or one skip_flag per MB
    int loop_filter_disable;
    int alpha_offset, beta_offset;
    int ref_flag;
    int mbx, mby;      ///< macroblock coordinates
    int flags;         ///< availability flags of neighbouring macroblocks
    int stc;           ///< last start code
    uint8_t *cy, *cu, *cv; ///< current MB sample pointers
    int left_qp;
    uint8_t *top_qp;

    /** mv motion vector cache
       0:    D3  B2  B3  C2
       4:    A1  X0  X1   -
       8:    A3  X2  X3   -

       X are the vectors in the current macroblock (5,6,9,10)
       A is the macroblock to the left (4,8)
       B is the macroblock to the top (1,2)
       C is the macroblock to the top-right (3)
       D is the macroblock to the top-left (0)

       the same is repeated for backward motion vectors */
    vector_t mv[2*4*3];
    vector_t *top_mv[2];
    vector_t *col_mv;

    /** luma pred mode cache
       0:    --  B2  B3
       3:    A1  X0  X1
       6:    A3  X2  X3   */
    int pred_mode_Y[3*3];
    int *top_pred_Y;
    int l_stride, c_stride;
    int luma_scan[4];
    int qp;
    int qp_fixed;
    int cbp;
    ScanTable scantable;

    /** intra prediction is done with un-deblocked samples
     they are saved here before deblocking the MB  */
    uint8_t *top_border_y, *top_border_u, *top_border_v;
    uint8_t left_border_y[26], left_border_u[10], left_border_v[10];
    uint8_t intern_border_y[26];
    uint8_t topleft_border_y, topleft_border_u, topleft_border_v;

    void (*intra_pred_l[8])(uint8_t *d,uint8_t *top,uint8_t *left,int stride);
    void (*intra_pred_c[7])(uint8_t *d,uint8_t *top,uint8_t *left,int stride);
    uint8_t *col_type_base;
    uint8_t *col_type;

    /* scaling factors for MV prediction */
    int sym_factor;    ///< for scaling in symmetrical B block
    int direct_den[2]; ///< for scaling in direct B block
    int scale_den[2];  ///< for scaling neighbouring MVs

    int got_keyframe;
    DCTELEM *block;
} AVSContext;

/*****************************************************************************
 *
 * in-loop deblocking filter
 *
 ****************************************************************************/

static inline int get_bs(vector_t *mvP, vector_t *mvQ, int b) {
    if((mvP->ref == REF_INTRA) || (mvQ->ref == REF_INTRA))
        return 2;
    if( (abs(mvP->x - mvQ->x) >= 4) ||  (abs(mvP->y - mvQ->y) >= 4) )
        return 1;
    if(b){
        mvP += MV_BWD_OFFS;
        mvQ += MV_BWD_OFFS;
        if( (abs(mvP->x - mvQ->x) >= 4) ||  (abs(mvP->y - mvQ->y) >= 4) )
            return 1;
    }else{
        if(mvP->ref != mvQ->ref)
            return 1;
    }
    return 0;
}

#define SET_PARAMS                                            \
    alpha = alpha_tab[av_clip(qp_avg + h->alpha_offset,0,63)];   \
    beta  =  beta_tab[av_clip(qp_avg + h->beta_offset, 0,63)];   \
    tc    =    tc_tab[av_clip(qp_avg + h->alpha_offset,0,63)];

/**
 * in-loop deblocking filter for a single macroblock
 *
 * boundary strength (bs) mapping:
 *
 * --4---5--
 * 0   2   |
 * | 6 | 7 |
 * 1   3   |
 * ---------
 *
 */
static void filter_mb(AVSContext *h, enum mb_t mb_type) {
    DECLARE_ALIGNED_8(uint8_t, bs[8]);
    int qp_avg, alpha, beta, tc;
    int i;

    /* save un-deblocked lines */
    h->topleft_border_y = h->top_border_y[h->mbx*16+15];
    h->topleft_border_u = h->top_border_u[h->mbx*10+8];
    h->topleft_border_v = h->top_border_v[h->mbx*10+8];
    memcpy(&h->top_border_y[h->mbx*16], h->cy + 15* h->l_stride,16);
    memcpy(&h->top_border_u[h->mbx*10+1], h->cu +  7* h->c_stride,8);
    memcpy(&h->top_border_v[h->mbx*10+1], h->cv +  7* h->c_stride,8);
    for(i=0;i<8;i++) {
        h->left_border_y[i*2+1] = *(h->cy + 15 + (i*2+0)*h->l_stride);
        h->left_border_y[i*2+2] = *(h->cy + 15 + (i*2+1)*h->l_stride);
        h->left_border_u[i+1] = *(h->cu + 7 + i*h->c_stride);
        h->left_border_v[i+1] = *(h->cv + 7 + i*h->c_stride);
    }
    if(!h->loop_filter_disable) {
        /* determine bs */
        if(mb_type == I_8X8)
            *((uint64_t *)bs) = 0x0202020202020202ULL;
        else{
            *((uint64_t *)bs) = 0;
            if(partition_flags[mb_type] & SPLITV){
                bs[2] = get_bs(&h->mv[MV_FWD_X0], &h->mv[MV_FWD_X1], mb_type > P_8X8);
                bs[3] = get_bs(&h->mv[MV_FWD_X2], &h->mv[MV_FWD_X3], mb_type > P_8X8);
            }
            if(partition_flags[mb_type] & SPLITH){
                bs[6] = get_bs(&h->mv[MV_FWD_X0], &h->mv[MV_FWD_X2], mb_type > P_8X8);
                bs[7] = get_bs(&h->mv[MV_FWD_X1], &h->mv[MV_FWD_X3], mb_type > P_8X8);
            }
            bs[0] = get_bs(&h->mv[MV_FWD_A1], &h->mv[MV_FWD_X0], mb_type > P_8X8);
            bs[1] = get_bs(&h->mv[MV_FWD_A3], &h->mv[MV_FWD_X2], mb_type > P_8X8);
            bs[4] = get_bs(&h->mv[MV_FWD_B2], &h->mv[MV_FWD_X0], mb_type > P_8X8);
            bs[5] = get_bs(&h->mv[MV_FWD_B3], &h->mv[MV_FWD_X1], mb_type > P_8X8);
        }
        if( *((uint64_t *)bs) ) {
            if(h->flags & A_AVAIL) {
                qp_avg = (h->qp + h->left_qp + 1) >> 1;
                SET_PARAMS;
                h->s.dsp.cavs_filter_lv(h->cy,h->l_stride,alpha,beta,tc,bs[0],bs[1]);
                h->s.dsp.cavs_filter_cv(h->cu,h->c_stride,alpha,beta,tc,bs[0],bs[1]);
                h->s.dsp.cavs_filter_cv(h->cv,h->c_stride,alpha,beta,tc,bs[0],bs[1]);
            }
            qp_avg = h->qp;
            SET_PARAMS;
            h->s.dsp.cavs_filter_lv(h->cy + 8,h->l_stride,alpha,beta,tc,bs[2],bs[3]);
            h->s.dsp.cavs_filter_lh(h->cy + 8*h->l_stride,h->l_stride,alpha,beta,tc,
                           bs[6],bs[7]);

            if(h->flags & B_AVAIL) {
                qp_avg = (h->qp + h->top_qp[h->mbx] + 1) >> 1;
                SET_PARAMS;
                h->s.dsp.cavs_filter_lh(h->cy,h->l_stride,alpha,beta,tc,bs[4],bs[5]);
                h->s.dsp.cavs_filter_ch(h->cu,h->c_stride,alpha,beta,tc,bs[4],bs[5]);
                h->s.dsp.cavs_filter_ch(h->cv,h->c_stride,alpha,beta,tc,bs[4],bs[5]);
            }
        }
    }
    h->left_qp = h->qp;
    h->top_qp[h->mbx] = h->qp;
}

#undef SET_PARAMS

/*****************************************************************************
 *
 * spatial intra prediction
 *
 ****************************************************************************/

static inline void load_intra_pred_luma(AVSContext *h, uint8_t *top,
                                        uint8_t **left, int block) {
    int i;

    switch(block) {
    case 0:
        *left = h->left_border_y;
        h->left_border_y[0] = h->left_border_y[1];
        memset(&h->left_border_y[17],h->left_border_y[16],9);
        memcpy(&top[1],&h->top_border_y[h->mbx*16],16);
        top[17] = top[16];
        top[0] = top[1];
        if((h->flags & A_AVAIL) && (h->flags & B_AVAIL))
            h->left_border_y[0] = top[0] = h->topleft_border_y;
        break;
    case 1:
        *left = h->intern_border_y;
        for(i=0;i<8;i++)
            h->intern_border_y[i+1] = *(h->cy + 7 + i*h->l_stride);
        memset(&h->intern_border_y[9],h->intern_border_y[8],9);
        h->intern_border_y[0] = h->intern_border_y[1];
        memcpy(&top[1],&h->top_border_y[h->mbx*16+8],8);
        if(h->flags & C_AVAIL)
            memcpy(&top[9],&h->top_border_y[(h->mbx + 1)*16],8);
        else
            memset(&top[9],top[8],9);
        top[17] = top[16];
        top[0] = top[1];
        if(h->flags & B_AVAIL)
            h->intern_border_y[0] = top[0] = h->top_border_y[h->mbx*16+7];
        break;
    case 2:
        *left = &h->left_border_y[8];
        memcpy(&top[1],h->cy + 7*h->l_stride,16);
        top[17] = top[16];
        top[0] = top[1];
        if(h->flags & A_AVAIL)
            top[0] = h->left_border_y[8];
        break;
    case 3:
        *left = &h->intern_border_y[8];
        for(i=0;i<8;i++)
            h->intern_border_y[i+9] = *(h->cy + 7 + (i+8)*h->l_stride);
        memset(&h->intern_border_y[17],h->intern_border_y[16],9);
        memcpy(&top[0],h->cy + 7 + 7*h->l_stride,9);
        memset(&top[9],top[8],9);
        break;
    }
}

static void intra_pred_vert(uint8_t *d,uint8_t *top,uint8_t *left,int stride) {
    int y;
    uint64_t a = unaligned64(&top[1]);
    for(y=0;y<8;y++) {
        *((uint64_t *)(d+y*stride)) = a;
    }
}

static void intra_pred_horiz(uint8_t *d,uint8_t *top,uint8_t *left,int stride) {
    int y;
    uint64_t a;
    for(y=0;y<8;y++) {
        a = left[y+1] * 0x0101010101010101ULL;
        *((uint64_t *)(d+y*stride)) = a;
    }
}

static void intra_pred_dc_128(uint8_t *d,uint8_t *top,uint8_t *left,int stride) {
    int y;
    uint64_t a = 0x8080808080808080ULL;
    for(y=0;y<8;y++)
        *((uint64_t *)(d+y*stride)) = a;
}

static void intra_pred_plane(uint8_t *d,uint8_t *top,uint8_t *left,int stride) {
    int x,y,ia;
    int ih = 0;
    int iv = 0;
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;

    for(x=0; x<4; x++) {
        ih += (x+1)*(top[5+x]-top[3-x]);
        iv += (x+1)*(left[5+x]-left[3-x]);
    }
    ia = (top[8]+left[8])<<4;
    ih = (17*ih+16)>>5;
    iv = (17*iv+16)>>5;
    for(y=0; y<8; y++)
        for(x=0; x<8; x++)
            d[y*stride+x] = cm[(ia+(x-3)*ih+(y-3)*iv+16)>>5];
}

#define LOWPASS(ARRAY,INDEX)                                            \
    (( ARRAY[(INDEX)-1] + 2*ARRAY[(INDEX)] + ARRAY[(INDEX)+1] + 2) >> 2)

static void intra_pred_lp(uint8_t *d,uint8_t *top,uint8_t *left,int stride) {
    int x,y;
    for(y=0; y<8; y++)
        for(x=0; x<8; x++)
            d[y*stride+x] = (LOWPASS(top,x+1) + LOWPASS(left,y+1)) >> 1;
}

static void intra_pred_down_left(uint8_t *d,uint8_t *top,uint8_t *left,int stride) {
    int x,y;
    for(y=0; y<8; y++)
        for(x=0; x<8; x++)
            d[y*stride+x] = (LOWPASS(top,x+y+2) + LOWPASS(left,x+y+2)) >> 1;
}

static void intra_pred_down_right(uint8_t *d,uint8_t *top,uint8_t *left,int stride) {
    int x,y;
    for(y=0; y<8; y++)
        for(x=0; x<8; x++)
            if(x==y)
                d[y*stride+x] = (left[1]+2*top[0]+top[1]+2)>>2;
            else if(x>y)
                d[y*stride+x] = LOWPASS(top,x-y);
            else
                d[y*stride+x] = LOWPASS(left,y-x);
}

static void intra_pred_lp_left(uint8_t *d,uint8_t *top,uint8_t *left,int stride) {
    int x,y;
    for(y=0; y<8; y++)
        for(x=0; x<8; x++)
            d[y*stride+x] = LOWPASS(left,y+1);
}

static void intra_pred_lp_top(uint8_t *d,uint8_t *top,uint8_t *left,int stride) {
    int x,y;
    for(y=0; y<8; y++)
        for(x=0; x<8; x++)
            d[y*stride+x] = LOWPASS(top,x+1);
}

#undef LOWPASS

static inline void modify_pred(const int_fast8_t *mod_table, int *mode) {
    *mode = mod_table[*mode];
    if(*mode < 0) {
        av_log(NULL, AV_LOG_ERROR, "Illegal intra prediction mode\n");
        *mode = 0;
    }
}

/*****************************************************************************
 *
 * motion compensation
 *
 ****************************************************************************/

static inline void mc_dir_part(AVSContext *h,Picture *pic,int square,
                        int chroma_height,int delta,int list,uint8_t *dest_y,
                        uint8_t *dest_cb,uint8_t *dest_cr,int src_x_offset,
                        int src_y_offset,qpel_mc_func *qpix_op,
                        h264_chroma_mc_func chroma_op,vector_t *mv){
    MpegEncContext * const s = &h->s;
    const int mx= mv->x + src_x_offset*8;
    const int my= mv->y + src_y_offset*8;
    const int luma_xy= (mx&3) + ((my&3)<<2);
    uint8_t * src_y = pic->data[0] + (mx>>2) + (my>>2)*h->l_stride;
    uint8_t * src_cb= pic->data[1] + (mx>>3) + (my>>3)*h->c_stride;
    uint8_t * src_cr= pic->data[2] + (mx>>3) + (my>>3)*h->c_stride;
    int extra_width= 0; //(s->flags&CODEC_FLAG_EMU_EDGE) ? 0 : 16;
    int extra_height= extra_width;
    int emu=0;
    const int full_mx= mx>>2;
    const int full_my= my>>2;
    const int pic_width  = 16*h->mb_width;
    const int pic_height = 16*h->mb_height;

    if(!pic->data[0])
        return;
    if(mx&7) extra_width -= 3;
    if(my&7) extra_height -= 3;

    if(   full_mx < 0-extra_width
          || full_my < 0-extra_height
          || full_mx + 16/*FIXME*/ > pic_width + extra_width
          || full_my + 16/*FIXME*/ > pic_height + extra_height){
        ff_emulated_edge_mc(s->edge_emu_buffer, src_y - 2 - 2*h->l_stride, h->l_stride,
                            16+5, 16+5/*FIXME*/, full_mx-2, full_my-2, pic_width, pic_height);
        src_y= s->edge_emu_buffer + 2 + 2*h->l_stride;
        emu=1;
    }

    qpix_op[luma_xy](dest_y, src_y, h->l_stride); //FIXME try variable height perhaps?
    if(!square){
        qpix_op[luma_xy](dest_y + delta, src_y + delta, h->l_stride);
    }

    if(emu){
        ff_emulated_edge_mc(s->edge_emu_buffer, src_cb, h->c_stride,
                            9, 9/*FIXME*/, (mx>>3), (my>>3), pic_width>>1, pic_height>>1);
        src_cb= s->edge_emu_buffer;
    }
    chroma_op(dest_cb, src_cb, h->c_stride, chroma_height, mx&7, my&7);

    if(emu){
        ff_emulated_edge_mc(s->edge_emu_buffer, src_cr, h->c_stride,
                            9, 9/*FIXME*/, (mx>>3), (my>>3), pic_width>>1, pic_height>>1);
        src_cr= s->edge_emu_buffer;
    }
    chroma_op(dest_cr, src_cr, h->c_stride, chroma_height, mx&7, my&7);
}

static inline void mc_part_std(AVSContext *h,int square,int chroma_height,int delta,
                        uint8_t *dest_y,uint8_t *dest_cb,uint8_t *dest_cr,
                        int x_offset, int y_offset,qpel_mc_func *qpix_put,
                        h264_chroma_mc_func chroma_put,qpel_mc_func *qpix_avg,
                        h264_chroma_mc_func chroma_avg, vector_t *mv){
    qpel_mc_func *qpix_op=  qpix_put;
    h264_chroma_mc_func chroma_op= chroma_put;

    dest_y  += 2*x_offset + 2*y_offset*h->l_stride;
    dest_cb +=   x_offset +   y_offset*h->c_stride;
    dest_cr +=   x_offset +   y_offset*h->c_stride;
    x_offset += 8*h->mbx;
    y_offset += 8*h->mby;

    if(mv->ref >= 0){
        Picture *ref= &h->DPB[mv->ref];
        mc_dir_part(h, ref, square, chroma_height, delta, 0,
                    dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_op, chroma_op, mv);

        qpix_op=  qpix_avg;
        chroma_op= chroma_avg;
    }

    if((mv+MV_BWD_OFFS)->ref >= 0){
        Picture *ref= &h->DPB[0];
        mc_dir_part(h, ref, square, chroma_height, delta, 1,
                    dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_op, chroma_op, mv+MV_BWD_OFFS);
    }
}

static void inter_pred(AVSContext *h, enum mb_t mb_type) {
    if(partition_flags[mb_type] == 0){ // 16x16
        mc_part_std(h, 1, 8, 0, h->cy, h->cu, h->cv, 0, 0,
                h->s.dsp.put_cavs_qpel_pixels_tab[0],
                h->s.dsp.put_h264_chroma_pixels_tab[0],
                h->s.dsp.avg_cavs_qpel_pixels_tab[0],
                h->s.dsp.avg_h264_chroma_pixels_tab[0],&h->mv[MV_FWD_X0]);
    }else{
        mc_part_std(h, 1, 4, 0, h->cy, h->cu, h->cv, 0, 0,
                h->s.dsp.put_cavs_qpel_pixels_tab[1],
                h->s.dsp.put_h264_chroma_pixels_tab[1],
                h->s.dsp.avg_cavs_qpel_pixels_tab[1],
                h->s.dsp.avg_h264_chroma_pixels_tab[1],&h->mv[MV_FWD_X0]);
        mc_part_std(h, 1, 4, 0, h->cy, h->cu, h->cv, 4, 0,
                h->s.dsp.put_cavs_qpel_pixels_tab[1],
                h->s.dsp.put_h264_chroma_pixels_tab[1],
                h->s.dsp.avg_cavs_qpel_pixels_tab[1],
                h->s.dsp.avg_h264_chroma_pixels_tab[1],&h->mv[MV_FWD_X1]);
        mc_part_std(h, 1, 4, 0, h->cy, h->cu, h->cv, 0, 4,
                h->s.dsp.put_cavs_qpel_pixels_tab[1],
                h->s.dsp.put_h264_chroma_pixels_tab[1],
                h->s.dsp.avg_cavs_qpel_pixels_tab[1],
                h->s.dsp.avg_h264_chroma_pixels_tab[1],&h->mv[MV_FWD_X2]);
        mc_part_std(h, 1, 4, 0, h->cy, h->cu, h->cv, 4, 4,
                h->s.dsp.put_cavs_qpel_pixels_tab[1],
                h->s.dsp.put_h264_chroma_pixels_tab[1],
                h->s.dsp.avg_cavs_qpel_pixels_tab[1],
                h->s.dsp.avg_h264_chroma_pixels_tab[1],&h->mv[MV_FWD_X3]);
    }
    /* set intra prediction modes to default values */
    h->pred_mode_Y[3] =  h->pred_mode_Y[6] = INTRA_L_LP;
    h->top_pred_Y[h->mbx*2+0] = h->top_pred_Y[h->mbx*2+1] = INTRA_L_LP;
}

/*****************************************************************************
 *
 * motion vector prediction
 *
 ****************************************************************************/

static inline void set_mvs(vector_t *mv, enum block_t size) {
    switch(size) {
    case BLK_16X16:
        mv[MV_STRIDE  ] = mv[0];
        mv[MV_STRIDE+1] = mv[0];
    case BLK_16X8:
        mv[1] = mv[0];
        break;
    case BLK_8X16:
        mv[MV_STRIDE] = mv[0];
        break;
    }
}

static inline void store_mvs(AVSContext *h) {
    h->col_mv[(h->mby*h->mb_width + h->mbx)*4 + 0] = h->mv[MV_FWD_X0];
    h->col_mv[(h->mby*h->mb_width + h->mbx)*4 + 1] = h->mv[MV_FWD_X1];
    h->col_mv[(h->mby*h->mb_width + h->mbx)*4 + 2] = h->mv[MV_FWD_X2];
    h->col_mv[(h->mby*h->mb_width + h->mbx)*4 + 3] = h->mv[MV_FWD_X3];
}

static inline void scale_mv(AVSContext *h, int *d_x, int *d_y, vector_t *src, int distp) {
    int den = h->scale_den[src->ref];

    *d_x = (src->x*distp*den + 256 + (src->x>>31)) >> 9;
    *d_y = (src->y*distp*den + 256 + (src->y>>31)) >> 9;
}

static inline void mv_pred_median(AVSContext *h, vector_t *mvP, vector_t *mvA, vector_t *mvB, vector_t *mvC) {
    int ax, ay, bx, by, cx, cy;
    int len_ab, len_bc, len_ca, len_mid;

    /* scale candidates according to their temporal span */
    scale_mv(h, &ax, &ay, mvA, mvP->dist);
    scale_mv(h, &bx, &by, mvB, mvP->dist);
    scale_mv(h, &cx, &cy, mvC, mvP->dist);
    /* find the geometrical median of the three candidates */
    len_ab = abs(ax - bx) + abs(ay - by);
    len_bc = abs(bx - cx) + abs(by - cy);
    len_ca = abs(cx - ax) + abs(cy - ay);
    len_mid = mid_pred(len_ab, len_bc, len_ca);
    if(len_mid == len_ab) {
        mvP->x = cx;
        mvP->y = cy;
    } else if(len_mid == len_bc) {
        mvP->x = ax;
        mvP->y = ay;
    } else {
        mvP->x = bx;
        mvP->y = by;
    }
}

static inline void mv_pred_direct(AVSContext *h, vector_t *pmv_fw,
                                  vector_t *col_mv) {
    vector_t *pmv_bw = pmv_fw + MV_BWD_OFFS;
    int den = h->direct_den[col_mv->ref];
    int m = col_mv->x >> 31;

    pmv_fw->dist = h->dist[1];
    pmv_bw->dist = h->dist[0];
    pmv_fw->ref = 1;
    pmv_bw->ref = 0;
    /* scale the co-located motion vector according to its temporal span */
    pmv_fw->x = (((den+(den*col_mv->x*pmv_fw->dist^m)-m-1)>>14)^m)-m;
    pmv_bw->x = m-(((den+(den*col_mv->x*pmv_bw->dist^m)-m-1)>>14)^m);
    m = col_mv->y >> 31;
    pmv_fw->y = (((den+(den*col_mv->y*pmv_fw->dist^m)-m-1)>>14)^m)-m;
    pmv_bw->y = m-(((den+(den*col_mv->y*pmv_bw->dist^m)-m-1)>>14)^m);
}

static inline void mv_pred_sym(AVSContext *h, vector_t *src, enum block_t size) {
    vector_t *dst = src + MV_BWD_OFFS;

    /* backward mv is the scaled and negated forward mv */
    dst->x = -((src->x * h->sym_factor + 256) >> 9);
    dst->y = -((src->y * h->sym_factor + 256) >> 9);
    dst->ref = 0;
    dst->dist = h->dist[0];
    set_mvs(dst, size);
}

static void mv_pred(AVSContext *h, enum mv_loc_t nP, enum mv_loc_t nC,
                    enum mv_pred_t mode, enum block_t size, int ref) {
    vector_t *mvP = &h->mv[nP];
    vector_t *mvA = &h->mv[nP-1];
    vector_t *mvB = &h->mv[nP-4];
    vector_t *mvC = &h->mv[nC];
    const vector_t *mvP2 = NULL;

    mvP->ref = ref;
    mvP->dist = h->dist[mvP->ref];
    if(mvC->ref == NOT_AVAIL)
        mvC = &h->mv[nP-5]; // set to top-left (mvD)
    if((mode == MV_PRED_PSKIP) &&
       ((mvA->ref == NOT_AVAIL) || (mvB->ref == NOT_AVAIL) ||
           ((mvA->x | mvA->y | mvA->ref) == 0)  ||
           ((mvB->x | mvB->y | mvB->ref) == 0) )) {
        mvP2 = &un_mv;
    /* if there is only one suitable candidate, take it */
    } else if((mvA->ref >= 0) && (mvB->ref < 0) && (mvC->ref < 0)) {
        mvP2= mvA;
    } else if((mvA->ref < 0) && (mvB->ref >= 0) && (mvC->ref < 0)) {
        mvP2= mvB;
    } else if((mvA->ref < 0) && (mvB->ref < 0) && (mvC->ref >= 0)) {
        mvP2= mvC;
    } else if(mode == MV_PRED_LEFT     && mvA->ref == ref){
        mvP2= mvA;
    } else if(mode == MV_PRED_TOP      && mvB->ref == ref){
        mvP2= mvB;
    } else if(mode == MV_PRED_TOPRIGHT && mvC->ref == ref){
        mvP2= mvC;
    }
    if(mvP2){
        mvP->x = mvP2->x;
        mvP->y = mvP2->y;
    }else
        mv_pred_median(h, mvP, mvA, mvB, mvC);

    if(mode < MV_PRED_PSKIP) {
        mvP->x += get_se_golomb(&h->s.gb);
        mvP->y += get_se_golomb(&h->s.gb);
    }
    set_mvs(mvP,size);
}

/*****************************************************************************
 *
 * residual data decoding
 *
 ****************************************************************************/

/** kth-order exponential golomb code */
static inline int get_ue_code(GetBitContext *gb, int order) {
    if(order) {
        int ret = get_ue_golomb(gb) << order;
        return ret + get_bits(gb,order);
    }
    return get_ue_golomb(gb);
}

/**
 * decode coefficients from one 8x8 block, dequantize, inverse transform
 *  and add them to sample block
 * @param r pointer to 2D VLC table
 * @param esc_golomb_order escape codes are k-golomb with this order k
 * @param qp quantizer
 * @param dst location of sample block
 * @param stride line stride in frame buffer
 */
static int decode_residual_block(AVSContext *h, GetBitContext *gb,
                                 const residual_vlc_t *r, int esc_golomb_order,
                                 int qp, uint8_t *dst, int stride) {
    int i,pos = -1;
    int level_code, esc_code, level, run, mask;
    int level_buf[64];
    int run_buf[64];
    int dqm = dequant_mul[qp];
    int dqs = dequant_shift[qp];
    int dqa = 1 << (dqs - 1);
    const uint8_t *scantab = h->scantable.permutated;
    DCTELEM *block = h->block;

    for(i=0;i<65;i++) {
        level_code = get_ue_code(gb,r->golomb_order);
        if(level_code >= ESCAPE_CODE) {
            run = ((level_code - ESCAPE_CODE) >> 1) + 1;
            esc_code = get_ue_code(gb,esc_golomb_order);
            level = esc_code + (run > r->max_run ? 1 : r->level_add[run]);
            while(level > r->inc_limit)
                r++;
            mask = -(level_code & 1);
            level = (level^mask) - mask;
        } else {
            level = r->rltab[level_code][0];
            if(!level) //end of block signal
                break;
            run   = r->rltab[level_code][1];
            r += r->rltab[level_code][2];
        }
        level_buf[i] = level;
        run_buf[i] = run;
    }
    /* inverse scan and dequantization */
    while(--i >= 0){
        pos += run_buf[i];
        if(pos > 63) {
            av_log(h->s.avctx, AV_LOG_ERROR,
                   "position out of block bounds at pic %d MB(%d,%d)\n",
                   h->picture.poc, h->mbx, h->mby);
            return -1;
        }
        block[scantab[pos]] = (level_buf[i]*dqm + dqa) >> dqs;
    }
    h->s.dsp.cavs_idct8_add(dst,block,stride);
    return 0;
}


static inline void decode_residual_chroma(AVSContext *h) {
    if(h->cbp & (1<<4))
        decode_residual_block(h,&h->s.gb,chroma_2dvlc,0, chroma_qp[h->qp],
                              h->cu,h->c_stride);
    if(h->cbp & (1<<5))
        decode_residual_block(h,&h->s.gb,chroma_2dvlc,0, chroma_qp[h->qp],
                              h->cv,h->c_stride);
}

static inline int decode_residual_inter(AVSContext *h) {
    int block;

    /* get coded block pattern */
    int cbp= get_ue_golomb(&h->s.gb);
    if(cbp > 63){
        av_log(h->s.avctx, AV_LOG_ERROR, "illegal inter cbp\n");
        return -1;
    }
    h->cbp = cbp_tab[cbp][1];

    /* get quantizer */
    if(h->cbp && !h->qp_fixed)
        h->qp = (h->qp + get_se_golomb(&h->s.gb)) & 63;
    for(block=0;block<4;block++)
        if(h->cbp & (1<<block))
            decode_residual_block(h,&h->s.gb,inter_2dvlc,0,h->qp,
                                  h->cy + h->luma_scan[block], h->l_stride);
    decode_residual_chroma(h);

    return 0;
}

/*****************************************************************************
 *
 * macroblock level
 *
 ****************************************************************************/

/**
 * initialise predictors for motion vectors and intra prediction
 */
static inline void init_mb(AVSContext *h) {
    int i;

    /* copy predictors from top line (MB B and C) into cache */
    for(i=0;i<3;i++) {
        h->mv[MV_FWD_B2+i] = h->top_mv[0][h->mbx*2+i];
        h->mv[MV_BWD_B2+i] = h->top_mv[1][h->mbx*2+i];
    }
    h->pred_mode_Y[1] = h->top_pred_Y[h->mbx*2+0];
    h->pred_mode_Y[2] = h->top_pred_Y[h->mbx*2+1];
    /* clear top predictors if MB B is not available */
    if(!(h->flags & B_AVAIL)) {
        h->mv[MV_FWD_B2] = un_mv;
        h->mv[MV_FWD_B3] = un_mv;
        h->mv[MV_BWD_B2] = un_mv;
        h->mv[MV_BWD_B3] = un_mv;
        h->pred_mode_Y[1] = h->pred_mode_Y[2] = NOT_AVAIL;
        h->flags &= ~(C_AVAIL|D_AVAIL);
    } else if(h->mbx) {
        h->flags |= D_AVAIL;
    }
    if(h->mbx == h->mb_width-1) //MB C not available
        h->flags &= ~C_AVAIL;
    /* clear top-right predictors if MB C is not available */
    if(!(h->flags & C_AVAIL)) {
        h->mv[MV_FWD_C2] = un_mv;
        h->mv[MV_BWD_C2] = un_mv;
    }
    /* clear top-left predictors if MB D is not available */
    if(!(h->flags & D_AVAIL)) {
        h->mv[MV_FWD_D3] = un_mv;
        h->mv[MV_BWD_D3] = un_mv;
    }
    /* set pointer for co-located macroblock type */
    h->col_type = &h->col_type_base[h->mby*h->mb_width + h->mbx];
}

static inline void check_for_slice(AVSContext *h);

/**
 * save predictors for later macroblocks and increase
 * macroblock address
 * @returns 0 if end of frame is reached, 1 otherwise
 */
static inline int next_mb(AVSContext *h) {
    int i;

    h->flags |= A_AVAIL;
    h->cy += 16;
    h->cu += 8;
    h->cv += 8;
    /* copy mvs as predictors to the left */
    for(i=0;i<=20;i+=4)
        h->mv[i] = h->mv[i+2];
    /* copy bottom mvs from cache to top line */
    h->top_mv[0][h->mbx*2+0] = h->mv[MV_FWD_X2];
    h->top_mv[0][h->mbx*2+1] = h->mv[MV_FWD_X3];
    h->top_mv[1][h->mbx*2+0] = h->mv[MV_BWD_X2];
    h->top_mv[1][h->mbx*2+1] = h->mv[MV_BWD_X3];
    /* next MB address */
    h->mbx++;
    if(h->mbx == h->mb_width) { //new mb line
        h->flags = B_AVAIL|C_AVAIL;
        /* clear left pred_modes */
        h->pred_mode_Y[3] = h->pred_mode_Y[6] = NOT_AVAIL;
        /* clear left mv predictors */
        for(i=0;i<=20;i+=4)
            h->mv[i] = un_mv;
        h->mbx = 0;
        h->mby++;
        /* re-calculate sample pointers */
        h->cy = h->picture.data[0] + h->mby*16*h->l_stride;
        h->cu = h->picture.data[1] + h->mby*8*h->c_stride;
        h->cv = h->picture.data[2] + h->mby*8*h->c_stride;
        if(h->mby == h->mb_height) { //frame end
            return 0;
        } else {
            //check_for_slice(h);
        }
    }
    return 1;
}

static int decode_mb_i(AVSContext *h, int cbp_code) {
    GetBitContext *gb = &h->s.gb;
    int block, pred_mode_uv;
    uint8_t top[18];
    uint8_t *left = NULL;
    uint8_t *d;

    init_mb(h);

    /* get intra prediction modes from stream */
    for(block=0;block<4;block++) {
        int nA,nB,predpred;
        int pos = scan3x3[block];

        nA = h->pred_mode_Y[pos-1];
        nB = h->pred_mode_Y[pos-3];
        predpred = FFMIN(nA,nB);
        if(predpred == NOT_AVAIL) // if either is not available
            predpred = INTRA_L_LP;
        if(!get_bits1(gb)){
            int rem_mode= get_bits(gb, 2);
            predpred = rem_mode + (rem_mode >= predpred);
        }
        h->pred_mode_Y[pos] = predpred;
    }
    pred_mode_uv = get_ue_golomb(gb);
    if(pred_mode_uv > 6) {
        av_log(h->s.avctx, AV_LOG_ERROR, "illegal intra chroma pred mode\n");
        return -1;
    }

    /* save pred modes before they get modified */
    h->pred_mode_Y[3] =  h->pred_mode_Y[5];
    h->pred_mode_Y[6] =  h->pred_mode_Y[8];
    h->top_pred_Y[h->mbx*2+0] = h->pred_mode_Y[7];
    h->top_pred_Y[h->mbx*2+1] = h->pred_mode_Y[8];

    /* modify pred modes according to availability of neighbour samples */
    if(!(h->flags & A_AVAIL)) {
        modify_pred(left_modifier_l, &h->pred_mode_Y[4] );
        modify_pred(left_modifier_l, &h->pred_mode_Y[7] );
        modify_pred(left_modifier_c, &pred_mode_uv );
    }
    if(!(h->flags & B_AVAIL)) {
        modify_pred(top_modifier_l, &h->pred_mode_Y[4] );
        modify_pred(top_modifier_l, &h->pred_mode_Y[5] );
        modify_pred(top_modifier_c, &pred_mode_uv );
    }

    /* get coded block pattern */
    if(h->pic_type == FF_I_TYPE)
        cbp_code = get_ue_golomb(gb);
    if(cbp_code > 63){
        av_log(h->s.avctx, AV_LOG_ERROR, "illegal intra cbp\n");
        return -1;
    }
    h->cbp = cbp_tab[cbp_code][0];
    if(h->cbp && !h->qp_fixed)
        h->qp = (h->qp + get_se_golomb(gb)) & 63; //qp_delta

    /* luma intra prediction interleaved with residual decode/transform/add */
    for(block=0;block<4;block++) {
        d = h->cy + h->luma_scan[block];
        load_intra_pred_luma(h, top, &left, block);
        h->intra_pred_l[h->pred_mode_Y[scan3x3[block]]]
            (d, top, left, h->l_stride);
        if(h->cbp & (1<<block))
            decode_residual_block(h,gb,intra_2dvlc,1,h->qp,d,h->l_stride);
    }

    /* chroma intra prediction */
    /* extend borders by one pixel */
    h->left_border_u[9] = h->left_border_u[8];
    h->left_border_v[9] = h->left_border_v[8];
    h->top_border_u[h->mbx*10+9] = h->top_border_u[h->mbx*10+8];
    h->top_border_v[h->mbx*10+9] = h->top_border_v[h->mbx*10+8];
    if(h->mbx && h->mby) {
        h->top_border_u[h->mbx*10] = h->left_border_u[0] = h->topleft_border_u;
        h->top_border_v[h->mbx*10] = h->left_border_v[0] = h->topleft_border_v;
    } else {
        h->left_border_u[0] = h->left_border_u[1];
        h->left_border_v[0] = h->left_border_v[1];
        h->top_border_u[h->mbx*10] = h->top_border_u[h->mbx*10+1];
        h->top_border_v[h->mbx*10] = h->top_border_v[h->mbx*10+1];
    }
    h->intra_pred_c[pred_mode_uv](h->cu, &h->top_border_u[h->mbx*10],
                                  h->left_border_u, h->c_stride);
    h->intra_pred_c[pred_mode_uv](h->cv, &h->top_border_v[h->mbx*10],
                                  h->left_border_v, h->c_stride);

    decode_residual_chroma(h);
    filter_mb(h,I_8X8);

    /* mark motion vectors as intra */
    h->mv[MV_FWD_X0] = intra_mv;
    set_mvs(&h->mv[MV_FWD_X0], BLK_16X16);
    h->mv[MV_BWD_X0] = intra_mv;
    set_mvs(&h->mv[MV_BWD_X0], BLK_16X16);
    if(h->pic_type != FF_B_TYPE)
        *h->col_type = I_8X8;

    return 0;
}

static void decode_mb_p(AVSContext *h, enum mb_t mb_type) {
    GetBitContext *gb = &h->s.gb;
    int ref[4];

    init_mb(h);
    switch(mb_type) {
    case P_SKIP:
        mv_pred(h, MV_FWD_X0, MV_FWD_C2, MV_PRED_PSKIP, BLK_16X16, 0);
        break;
    case P_16X16:
        ref[0] = h->ref_flag ? 0 : get_bits1(gb);
        mv_pred(h, MV_FWD_X0, MV_FWD_C2, MV_PRED_MEDIAN,   BLK_16X16,ref[0]);
        break;
    case P_16X8:
        ref[0] = h->ref_flag ? 0 : get_bits1(gb);
        ref[2] = h->ref_flag ? 0 : get_bits1(gb);
        mv_pred(h, MV_FWD_X0, MV_FWD_C2, MV_PRED_TOP,      BLK_16X8, ref[0]);
        mv_pred(h, MV_FWD_X2, MV_FWD_A1, MV_PRED_LEFT,     BLK_16X8, ref[2]);
        break;
    case P_8X16:
        ref[0] = h->ref_flag ? 0 : get_bits1(gb);
        ref[1] = h->ref_flag ? 0 : get_bits1(gb);
        mv_pred(h, MV_FWD_X0, MV_FWD_B3, MV_PRED_LEFT,     BLK_8X16, ref[0]);
        mv_pred(h, MV_FWD_X1, MV_FWD_C2, MV_PRED_TOPRIGHT, BLK_8X16, ref[1]);
        break;
    case P_8X8:
        ref[0] = h->ref_flag ? 0 : get_bits1(gb);
        ref[1] = h->ref_flag ? 0 : get_bits1(gb);
        ref[2] = h->ref_flag ? 0 : get_bits1(gb);
        ref[3] = h->ref_flag ? 0 : get_bits1(gb);
        mv_pred(h, MV_FWD_X0, MV_FWD_B3, MV_PRED_MEDIAN,   BLK_8X8, ref[0]);
        mv_pred(h, MV_FWD_X1, MV_FWD_C2, MV_PRED_MEDIAN,   BLK_8X8, ref[1]);
        mv_pred(h, MV_FWD_X2, MV_FWD_X1, MV_PRED_MEDIAN,   BLK_8X8, ref[2]);
        mv_pred(h, MV_FWD_X3, MV_FWD_X0, MV_PRED_MEDIAN,   BLK_8X8, ref[3]);
    }
    inter_pred(h, mb_type);
    store_mvs(h);
    if(mb_type != P_SKIP)
        decode_residual_inter(h);
    filter_mb(h,mb_type);
    *h->col_type = mb_type;
}

static void decode_mb_b(AVSContext *h, enum mb_t mb_type) {
    int block;
    enum sub_mb_t sub_type[4];
    int flags;

    init_mb(h);

    /* reset all MVs */
    h->mv[MV_FWD_X0] = dir_mv;
    set_mvs(&h->mv[MV_FWD_X0], BLK_16X16);
    h->mv[MV_BWD_X0] = dir_mv;
    set_mvs(&h->mv[MV_BWD_X0], BLK_16X16);
    switch(mb_type) {
    case B_SKIP:
    case B_DIRECT:
        if(!(*h->col_type)) {
            /* intra MB at co-location, do in-plane prediction */
            mv_pred(h, MV_FWD_X0, MV_FWD_C2, MV_PRED_BSKIP, BLK_16X16, 1);
            mv_pred(h, MV_BWD_X0, MV_BWD_C2, MV_PRED_BSKIP, BLK_16X16, 0);
        } else
            /* direct prediction from co-located P MB, block-wise */
            for(block=0;block<4;block++)
                mv_pred_direct(h,&h->mv[mv_scan[block]],
                            &h->col_mv[(h->mby*h->mb_width+h->mbx)*4 + block]);
        break;
    case B_FWD_16X16:
        mv_pred(h, MV_FWD_X0, MV_FWD_C2, MV_PRED_MEDIAN, BLK_16X16, 1);
        break;
    case B_SYM_16X16:
        mv_pred(h, MV_FWD_X0, MV_FWD_C2, MV_PRED_MEDIAN, BLK_16X16, 1);
        mv_pred_sym(h, &h->mv[MV_FWD_X0], BLK_16X16);
        break;
    case B_BWD_16X16:
        mv_pred(h, MV_BWD_X0, MV_BWD_C2, MV_PRED_MEDIAN, BLK_16X16, 0);
        break;
    case B_8X8:
        for(block=0;block<4;block++)
            sub_type[block] = get_bits(&h->s.gb,2);
        for(block=0;block<4;block++) {
            switch(sub_type[block]) {
            case B_SUB_DIRECT:
                if(!(*h->col_type)) {
                    /* intra MB at co-location, do in-plane prediction */
                    mv_pred(h, mv_scan[block], mv_scan[block]-3,
                            MV_PRED_BSKIP, BLK_8X8, 1);
                    mv_pred(h, mv_scan[block]+MV_BWD_OFFS,
                            mv_scan[block]-3+MV_BWD_OFFS,
                            MV_PRED_BSKIP, BLK_8X8, 0);
                } else
                    mv_pred_direct(h,&h->mv[mv_scan[block]],
                                   &h->col_mv[(h->mby*h->mb_width + h->mbx)*4 + block]);
                break;
            case B_SUB_FWD:
                mv_pred(h, mv_scan[block], mv_scan[block]-3,
                        MV_PRED_MEDIAN, BLK_8X8, 1);
                break;
            case B_SUB_SYM:
                mv_pred(h, mv_scan[block], mv_scan[block]-3,
                        MV_PRED_MEDIAN, BLK_8X8, 1);
                mv_pred_sym(h, &h->mv[mv_scan[block]], BLK_8X8);
                break;
            }
        }
        for(block=0;block<4;block++) {
            if(sub_type[block] == B_SUB_BWD)
                mv_pred(h, mv_scan[block]+MV_BWD_OFFS,
                        mv_scan[block]+MV_BWD_OFFS-3,
                        MV_PRED_MEDIAN, BLK_8X8, 0);
        }
        break;
    default:
        assert((mb_type > B_SYM_16X16) && (mb_type < B_8X8));
        flags = partition_flags[mb_type];
        if(mb_type & 1) { /* 16x8 macroblock types */
            if(flags & FWD0)
                mv_pred(h, MV_FWD_X0, MV_FWD_C2, MV_PRED_TOP,  BLK_16X8, 1);
            if(flags & SYM0)
                mv_pred_sym(h, &h->mv[MV_FWD_X0], BLK_16X8);
            if(flags & FWD1)
                mv_pred(h, MV_FWD_X2, MV_FWD_A1, MV_PRED_LEFT, BLK_16X8, 1);
            if(flags & SYM1)
                mv_pred_sym(h, &h->mv[MV_FWD_X2], BLK_16X8);
            if(flags & BWD0)
                mv_pred(h, MV_BWD_X0, MV_BWD_C2, MV_PRED_TOP,  BLK_16X8, 0);
            if(flags & BWD1)
                mv_pred(h, MV_BWD_X2, MV_BWD_A1, MV_PRED_LEFT, BLK_16X8, 0);
        } else {          /* 8x16 macroblock types */
            if(flags & FWD0)
                mv_pred(h, MV_FWD_X0, MV_FWD_B3, MV_PRED_LEFT, BLK_8X16, 1);
            if(flags & SYM0)
                mv_pred_sym(h, &h->mv[MV_FWD_X0], BLK_8X16);
            if(flags & FWD1)
                mv_pred(h, MV_FWD_X1, MV_FWD_C2, MV_PRED_TOPRIGHT,BLK_8X16, 1);
            if(flags & SYM1)
                mv_pred_sym(h, &h->mv[MV_FWD_X1], BLK_8X16);
            if(flags & BWD0)
                mv_pred(h, MV_BWD_X0, MV_BWD_B3, MV_PRED_LEFT, BLK_8X16, 0);
            if(flags & BWD1)
                mv_pred(h, MV_BWD_X1, MV_BWD_C2, MV_PRED_TOPRIGHT,BLK_8X16, 0);
        }
    }
    inter_pred(h, mb_type);
    if(mb_type != B_SKIP)
        decode_residual_inter(h);
    filter_mb(h,mb_type);
}

/*****************************************************************************
 *
 * slice level
 *
 ****************************************************************************/

static inline int decode_slice_header(AVSContext *h, GetBitContext *gb) {
    if(h->stc > 0xAF)
        av_log(h->s.avctx, AV_LOG_ERROR, "unexpected start code 0x%02x\n", h->stc);
    h->mby = h->stc;
    if((h->mby == 0) && (!h->qp_fixed)){
        h->qp_fixed = get_bits1(gb);
        h->qp = get_bits(gb,6);
    }
    /* inter frame or second slice can have weighting params */
    if((h->pic_type != FF_I_TYPE) || (!h->pic_structure && h->mby >= h->mb_width/2))
        if(get_bits1(gb)) { //slice_weighting_flag
            av_log(h->s.avctx, AV_LOG_ERROR,
                   "weighted prediction not yet supported\n");
        }
    return 0;
}

static inline void check_for_slice(AVSContext *h) {
    GetBitContext *gb = &h->s.gb;
    int align;
    align = (-get_bits_count(gb)) & 7;
    if((show_bits_long(gb,24+align) & 0xFFFFFF) == 0x000001) {
        get_bits_long(gb,24+align);
        h->stc = get_bits(gb,8);
        decode_slice_header(h,gb);
    }
}

/*****************************************************************************
 *
 * frame level
 *
 ****************************************************************************/

static void init_pic(AVSContext *h) {
    int i;

    /* clear some predictors */
    for(i=0;i<=20;i+=4)
        h->mv[i] = un_mv;
    h->mv[MV_BWD_X0] = dir_mv;
    set_mvs(&h->mv[MV_BWD_X0], BLK_16X16);
    h->mv[MV_FWD_X0] = dir_mv;
    set_mvs(&h->mv[MV_FWD_X0], BLK_16X16);
    h->pred_mode_Y[3] = h->pred_mode_Y[6] = NOT_AVAIL;
    h->cy = h->picture.data[0];
    h->cu = h->picture.data[1];
    h->cv = h->picture.data[2];
    h->l_stride = h->picture.linesize[0];
    h->c_stride = h->picture.linesize[1];
    h->luma_scan[2] = 8*h->l_stride;
    h->luma_scan[3] = 8*h->l_stride+8;
    h->mbx = h->mby = 0;
    h->flags = 0;
}

static int decode_pic(AVSContext *h) {
    MpegEncContext *s = &h->s;
    int skip_count;
    enum mb_t mb_type;

    if (!s->context_initialized) {
        s->avctx->idct_algo = FF_IDCT_CAVS;
        if (MPV_common_init(s) < 0)
            return -1;
        ff_init_scantable(s->dsp.idct_permutation,&h->scantable,ff_zigzag_direct);
    }
    get_bits(&s->gb,16);//bbv_dwlay
    if(h->stc == PIC_PB_START_CODE) {
        h->pic_type = get_bits(&s->gb,2) + FF_I_TYPE;
        if(h->pic_type > FF_B_TYPE) {
            av_log(s->avctx, AV_LOG_ERROR, "illegal picture type\n");
            return -1;
        }
        /* make sure we have the reference frames we need */
        if(!h->DPB[0].data[0] ||
          (!h->DPB[1].data[0] && h->pic_type == FF_B_TYPE))
            return -1;
    } else {
        h->pic_type = FF_I_TYPE;
        if(get_bits1(&s->gb))
            get_bits(&s->gb,16);//time_code
    }
    /* release last B frame */
    if(h->picture.data[0])
        s->avctx->release_buffer(s->avctx, (AVFrame *)&h->picture);

    s->avctx->get_buffer(s->avctx, (AVFrame *)&h->picture);
    init_pic(h);
    h->picture.poc = get_bits(&s->gb,8)*2;

    /* get temporal distances and MV scaling factors */
    if(h->pic_type != FF_B_TYPE) {
        h->dist[0] = (h->picture.poc - h->DPB[0].poc  + 512) % 512;
    } else {
        h->dist[0] = (h->DPB[0].poc  - h->picture.poc + 512) % 512;
    }
    h->dist[1] = (h->picture.poc - h->DPB[1].poc  + 512) % 512;
    h->scale_den[0] = h->dist[0] ? 512/h->dist[0] : 0;
    h->scale_den[1] = h->dist[1] ? 512/h->dist[1] : 0;
    if(h->pic_type == FF_B_TYPE) {
        h->sym_factor = h->dist[0]*h->scale_den[1];
    } else {
        h->direct_den[0] = h->dist[0] ? 16384/h->dist[0] : 0;
        h->direct_den[1] = h->dist[1] ? 16384/h->dist[1] : 0;
    }

    if(s->low_delay)
        get_ue_golomb(&s->gb); //bbv_check_times
    h->progressive             = get_bits1(&s->gb);
    if(h->progressive)
        h->pic_structure = 1;
    else if(!(h->pic_structure = get_bits1(&s->gb) && (h->stc == PIC_PB_START_CODE)) )
        get_bits1(&s->gb);     //advanced_pred_mode_disable
    skip_bits1(&s->gb);        //top_field_first
    skip_bits1(&s->gb);        //repeat_first_field
    h->qp_fixed                = get_bits1(&s->gb);
    h->qp                      = get_bits(&s->gb,6);
    if(h->pic_type == FF_I_TYPE) {
        if(!h->progressive && !h->pic_structure)
            skip_bits1(&s->gb);//what is this?
        skip_bits(&s->gb,4);   //reserved bits
    } else {
        if(!(h->pic_type == FF_B_TYPE && h->pic_structure == 1))
            h->ref_flag        = get_bits1(&s->gb);
        skip_bits(&s->gb,4);   //reserved bits
        h->skip_mode_flag      = get_bits1(&s->gb);
    }
    h->loop_filter_disable     = get_bits1(&s->gb);
    if(!h->loop_filter_disable && get_bits1(&s->gb)) {
        h->alpha_offset        = get_se_golomb(&s->gb);
        h->beta_offset         = get_se_golomb(&s->gb);
    } else {
        h->alpha_offset = h->beta_offset  = 0;
    }
    check_for_slice(h);
    if(h->pic_type == FF_I_TYPE) {
        do {
            decode_mb_i(h, 0);
        } while(next_mb(h));
    } else if(h->pic_type == FF_P_TYPE) {
        do {
            if(h->skip_mode_flag) {
                skip_count = get_ue_golomb(&s->gb);
                while(skip_count--) {
                    decode_mb_p(h,P_SKIP);
                    if(!next_mb(h))
                        goto done;
                }
                mb_type = get_ue_golomb(&s->gb) + P_16X16;
            } else
                mb_type = get_ue_golomb(&s->gb) + P_SKIP;
            if(mb_type > P_8X8) {
                decode_mb_i(h, mb_type - P_8X8 - 1);
            } else
                decode_mb_p(h,mb_type);
        } while(next_mb(h));
    } else { /* FF_B_TYPE */
        do {
            if(h->skip_mode_flag) {
                skip_count = get_ue_golomb(&s->gb);
                while(skip_count--) {
                    decode_mb_b(h,B_SKIP);
                    if(!next_mb(h))
                        goto done;
                }
                mb_type = get_ue_golomb(&s->gb) + B_DIRECT;
            } else
                mb_type = get_ue_golomb(&s->gb) + B_SKIP;
            if(mb_type > B_8X8) {
                decode_mb_i(h, mb_type - B_8X8 - 1);
            } else
                decode_mb_b(h,mb_type);
        } while(next_mb(h));
    }
 done:
    if(h->pic_type != FF_B_TYPE) {
        if(h->DPB[1].data[0])
            s->avctx->release_buffer(s->avctx, (AVFrame *)&h->DPB[1]);
        memcpy(&h->DPB[1], &h->DPB[0], sizeof(Picture));
        memcpy(&h->DPB[0], &h->picture, sizeof(Picture));
        memset(&h->picture,0,sizeof(Picture));
    }
    return 0;
}

/*****************************************************************************
 *
 * headers and interface
 *
 ****************************************************************************/

/**
 * some predictions require data from the top-neighbouring macroblock.
 * this data has to be stored for one complete row of macroblocks
 * and this storage space is allocated here
 */
static void init_top_lines(AVSContext *h) {
    /* alloc top line of predictors */
    h->top_qp       = av_malloc( h->mb_width);
    h->top_mv[0]    = av_malloc((h->mb_width*2+1)*sizeof(vector_t));
    h->top_mv[1]    = av_malloc((h->mb_width*2+1)*sizeof(vector_t));
    h->top_pred_Y   = av_malloc( h->mb_width*2*sizeof(*h->top_pred_Y));
    h->top_border_y = av_malloc((h->mb_width+1)*16);
    h->top_border_u = av_malloc((h->mb_width)*10);
    h->top_border_v = av_malloc((h->mb_width)*10);

    /* alloc space for co-located MVs and types */
    h->col_mv       = av_malloc( h->mb_width*h->mb_height*4*sizeof(vector_t));
    h->col_type_base = av_malloc(h->mb_width*h->mb_height);
    h->block        = av_mallocz(64*sizeof(DCTELEM));
}

static int decode_seq_header(AVSContext *h) {
    MpegEncContext *s = &h->s;
    extern const AVRational ff_frame_rate_tab[];
    int frame_rate_code;

    h->profile =         get_bits(&s->gb,8);
    h->level =           get_bits(&s->gb,8);
    skip_bits1(&s->gb); //progressive sequence
    s->width =           get_bits(&s->gb,14);
    s->height =          get_bits(&s->gb,14);
    skip_bits(&s->gb,2); //chroma format
    skip_bits(&s->gb,3); //sample_precision
    h->aspect_ratio =    get_bits(&s->gb,4);
    frame_rate_code =    get_bits(&s->gb,4);
    skip_bits(&s->gb,18);//bit_rate_lower
    skip_bits1(&s->gb);  //marker_bit
    skip_bits(&s->gb,12);//bit_rate_upper
    s->low_delay =       get_bits1(&s->gb);
    h->mb_width  = (s->width  + 15) >> 4;
    h->mb_height = (s->height + 15) >> 4;
    h->s.avctx->time_base.den = ff_frame_rate_tab[frame_rate_code].num;
    h->s.avctx->time_base.num = ff_frame_rate_tab[frame_rate_code].den;
    h->s.avctx->width  = s->width;
    h->s.avctx->height = s->height;
    if(!h->top_qp)
        init_top_lines(h);
    return 0;
}

static void cavs_flush(AVCodecContext * avctx) {
    AVSContext *h = avctx->priv_data;
    h->got_keyframe = 0;
}

static int cavs_decode_frame(AVCodecContext * avctx,void *data, int *data_size,
                             uint8_t * buf, int buf_size) {
    AVSContext *h = avctx->priv_data;
    MpegEncContext *s = &h->s;
    int input_size;
    const uint8_t *buf_end;
    const uint8_t *buf_ptr;
    AVFrame *picture = data;
    uint32_t stc;

    s->avctx = avctx;

    if (buf_size == 0) {
        if(!s->low_delay && h->DPB[0].data[0]) {
            *data_size = sizeof(AVPicture);
            *picture = *(AVFrame *) &h->DPB[0];
        }
        return 0;
    }

    buf_ptr = buf;
    buf_end = buf + buf_size;
    for(;;) {
        buf_ptr = ff_find_start_code(buf_ptr,buf_end, &stc);
        if(stc & 0xFFFFFE00)
            return FFMAX(0, buf_ptr - buf - s->parse_context.last_index);
        input_size = (buf_end - buf_ptr)*8;
        switch(stc) {
        case SEQ_START_CODE:
            init_get_bits(&s->gb, buf_ptr, input_size);
            decode_seq_header(h);
            break;
        case PIC_I_START_CODE:
            if(!h->got_keyframe) {
                if(h->DPB[0].data[0])
                    avctx->release_buffer(avctx, (AVFrame *)&h->DPB[0]);
                if(h->DPB[1].data[0])
                    avctx->release_buffer(avctx, (AVFrame *)&h->DPB[1]);
                h->got_keyframe = 1;
            }
        case PIC_PB_START_CODE:
            *data_size = 0;
            if(!h->got_keyframe)
                break;
            init_get_bits(&s->gb, buf_ptr, input_size);
            h->stc = stc;
            if(decode_pic(h))
                break;
            *data_size = sizeof(AVPicture);
            if(h->pic_type != FF_B_TYPE) {
                if(h->DPB[1].data[0]) {
                    *picture = *(AVFrame *) &h->DPB[1];
                } else {
                    *data_size = 0;
                }
            } else
                *picture = *(AVFrame *) &h->picture;
            break;
        case EXT_START_CODE:
            //mpeg_decode_extension(avctx,buf_ptr, input_size);
            break;
        case USER_START_CODE:
            //mpeg_decode_user_data(avctx,buf_ptr, input_size);
            break;
        default:
            if (stc >= SLICE_MIN_START_CODE &&
                stc <= SLICE_MAX_START_CODE) {
                init_get_bits(&s->gb, buf_ptr, input_size);
                decode_slice_header(h, &s->gb);
            }
            break;
        }
    }
}

static int cavs_decode_init(AVCodecContext * avctx) {
    AVSContext *h = avctx->priv_data;
    MpegEncContext * const s = &h->s;

    MPV_decode_defaults(s);
    s->avctx = avctx;

    avctx->pix_fmt= PIX_FMT_YUV420P;

    h->luma_scan[0] = 0;
    h->luma_scan[1] = 8;
    h->intra_pred_l[      INTRA_L_VERT] = intra_pred_vert;
    h->intra_pred_l[     INTRA_L_HORIZ] = intra_pred_horiz;
    h->intra_pred_l[        INTRA_L_LP] = intra_pred_lp;
    h->intra_pred_l[ INTRA_L_DOWN_LEFT] = intra_pred_down_left;
    h->intra_pred_l[INTRA_L_DOWN_RIGHT] = intra_pred_down_right;
    h->intra_pred_l[   INTRA_L_LP_LEFT] = intra_pred_lp_left;
    h->intra_pred_l[    INTRA_L_LP_TOP] = intra_pred_lp_top;
    h->intra_pred_l[    INTRA_L_DC_128] = intra_pred_dc_128;
    h->intra_pred_c[        INTRA_C_LP] = intra_pred_lp;
    h->intra_pred_c[     INTRA_C_HORIZ] = intra_pred_horiz;
    h->intra_pred_c[      INTRA_C_VERT] = intra_pred_vert;
    h->intra_pred_c[     INTRA_C_PLANE] = intra_pred_plane;
    h->intra_pred_c[   INTRA_C_LP_LEFT] = intra_pred_lp_left;
    h->intra_pred_c[    INTRA_C_LP_TOP] = intra_pred_lp_top;
    h->intra_pred_c[    INTRA_C_DC_128] = intra_pred_dc_128;
    h->mv[ 7] = un_mv;
    h->mv[19] = un_mv;
    return 0;
}

static int cavs_decode_end(AVCodecContext * avctx) {
    AVSContext *h = avctx->priv_data;

    av_free(h->top_qp);
    av_free(h->top_mv[0]);
    av_free(h->top_mv[1]);
    av_free(h->top_pred_Y);
    av_free(h->top_border_y);
    av_free(h->top_border_u);
    av_free(h->top_border_v);
    av_free(h->col_mv);
    av_free(h->col_type_base);
    av_free(h->block);
    return 0;
}

AVCodec cavs_decoder = {
    "cavs",
    CODEC_TYPE_VIDEO,
    CODEC_ID_CAVS,
    sizeof(AVSContext),
    cavs_decode_init,
    NULL,
    cavs_decode_end,
    cavs_decode_frame,
    CODEC_CAP_DR1 | CODEC_CAP_DELAY,
    .flush= cavs_flush,
};
#endif /* CONFIG_CAVS_DECODER */

#ifdef CONFIG_CAVSVIDEO_PARSER
/**
 * finds the end of the current frame in the bitstream.
 * @return the position of the first byte of the next frame, or -1
 */
static int cavs_find_frame_end(ParseContext *pc, const uint8_t *buf,
                               int buf_size) {
    int pic_found, i;
    uint32_t state;

    pic_found= pc->frame_start_found;
    state= pc->state;

    i=0;
    if(!pic_found){
        for(i=0; i<buf_size; i++){
            state= (state<<8) | buf[i];
            if(state == PIC_I_START_CODE || state == PIC_PB_START_CODE){
                i++;
                pic_found=1;
                break;
            }
        }
    }

    if(pic_found){
        /* EOF considered as end of frame */
        if (buf_size == 0)
            return 0;
        for(; i<buf_size; i++){
            state= (state<<8) | buf[i];
            if((state&0xFFFFFF00) == 0x100){
                if(state < SLICE_MIN_START_CODE || state > SLICE_MAX_START_CODE){
                    pc->frame_start_found=0;
                    pc->state=-1;
                    return i-3;
                }
            }
        }
    }
    pc->frame_start_found= pic_found;
    pc->state= state;
    return END_NOT_FOUND;
}

static int cavsvideo_parse(AVCodecParserContext *s,
                           AVCodecContext *avctx,
                           uint8_t **poutbuf, int *poutbuf_size,
                           const uint8_t *buf, int buf_size)
{
    ParseContext *pc = s->priv_data;
    int next;

    if(s->flags & PARSER_FLAG_COMPLETE_FRAMES){
        next= buf_size;
    }else{
        next= cavs_find_frame_end(pc, buf, buf_size);

        if (ff_combine_frame(pc, next, (uint8_t **)&buf, &buf_size) < 0) {
            *poutbuf = NULL;
            *poutbuf_size = 0;
            return buf_size;
        }
    }
    *poutbuf = (uint8_t *)buf;
    *poutbuf_size = buf_size;
    return next;
}

AVCodecParser cavsvideo_parser = {
    { CODEC_ID_CAVS },
    sizeof(ParseContext1),
    NULL,
    cavsvideo_parse,
    ff_parse1_close,
    ff_mpeg4video_split,
};
#endif /* CONFIG_CAVSVIDEO_PARSER */
