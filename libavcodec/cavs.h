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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef CAVS_H
#define CAVS_H

#include "dsputil.h"
#include "mpegvideo.h"

#define SLICE_MIN_START_CODE    0x00000101
#define SLICE_MAX_START_CODE    0x000001af
#define EXT_START_CODE          0x000001b5
#define USER_START_CODE         0x000001b2
#define CAVS_START_CODE         0x000001b0
#define PIC_I_START_CODE        0x000001b3
#define PIC_PB_START_CODE       0x000001b6

#define A_AVAIL                          1
#define B_AVAIL                          2
#define C_AVAIL                          4
#define D_AVAIL                          8
#define NOT_AVAIL                       -1
#define REF_INTRA                       -2
#define REF_DIR                         -3

#define ESCAPE_CODE                     59

#define FWD0                          0x01
#define FWD1                          0x02
#define BWD0                          0x04
#define BWD1                          0x08
#define SYM0                          0x10
#define SYM1                          0x20
#define SPLITH                        0x40
#define SPLITV                        0x80

#define MV_BWD_OFFS                     12
#define MV_STRIDE                        4

enum mb_t {
  I_8X8 = 0,
  P_SKIP,
  P_16X16,
  P_16X8,
  P_8X16,
  P_8X8,
  B_SKIP,
  B_DIRECT,
  B_FWD_16X16,
  B_BWD_16X16,
  B_SYM_16X16,
  B_8X8 = 29
};

enum sub_mb_t {
  B_SUB_DIRECT,
  B_SUB_FWD,
  B_SUB_BWD,
  B_SUB_SYM
};

enum intra_luma_t {
  INTRA_L_VERT,
  INTRA_L_HORIZ,
  INTRA_L_LP,
  INTRA_L_DOWN_LEFT,
  INTRA_L_DOWN_RIGHT,
  INTRA_L_LP_LEFT,
  INTRA_L_LP_TOP,
  INTRA_L_DC_128
};

enum intra_chroma_t {
  INTRA_C_LP,
  INTRA_C_HORIZ,
  INTRA_C_VERT,
  INTRA_C_PLANE,
  INTRA_C_LP_LEFT,
  INTRA_C_LP_TOP,
  INTRA_C_DC_128,
};

enum mv_pred_t {
  MV_PRED_MEDIAN,
  MV_PRED_LEFT,
  MV_PRED_TOP,
  MV_PRED_TOPRIGHT,
  MV_PRED_PSKIP,
  MV_PRED_BSKIP
};

enum block_t {
  BLK_16X16,
  BLK_16X8,
  BLK_8X16,
  BLK_8X8
};

enum mv_loc_t {
  MV_FWD_D3 = 0,
  MV_FWD_B2,
  MV_FWD_B3,
  MV_FWD_C2,
  MV_FWD_A1,
  MV_FWD_X0,
  MV_FWD_X1,
  MV_FWD_A3 = 8,
  MV_FWD_X2,
  MV_FWD_X3,
  MV_BWD_D3 = MV_BWD_OFFS,
  MV_BWD_B2,
  MV_BWD_B3,
  MV_BWD_C2,
  MV_BWD_A1,
  MV_BWD_X0,
  MV_BWD_X1,
  MV_BWD_A3 = MV_BWD_OFFS+8,
  MV_BWD_X2,
  MV_BWD_X3
};

DECLARE_ALIGNED_8(typedef, struct) {
    int16_t x;
    int16_t y;
    int16_t dist;
    int16_t ref;
} vector_t;

typedef struct residual_vlc_t {
  int8_t rltab[59][3];
  int8_t level_add[27];
  int8_t golomb_order;
  int inc_limit;
  int8_t max_run;
} residual_vlc_t;

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

extern const vector_t ff_cavs_un_mv;

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

static inline void modify_pred(const int_fast8_t *mod_table, int *mode) {
    *mode = mod_table[*mode];
    if(*mode < 0) {
        av_log(NULL, AV_LOG_ERROR, "Illegal intra prediction mode\n");
        *mode = 0;
    }
}

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
        h->mv[MV_FWD_B2] = ff_cavs_un_mv;
        h->mv[MV_FWD_B3] = ff_cavs_un_mv;
        h->mv[MV_BWD_B2] = ff_cavs_un_mv;
        h->mv[MV_BWD_B3] = ff_cavs_un_mv;
        h->pred_mode_Y[1] = h->pred_mode_Y[2] = NOT_AVAIL;
        h->flags &= ~(C_AVAIL|D_AVAIL);
    } else if(h->mbx) {
        h->flags |= D_AVAIL;
    }
    if(h->mbx == h->mb_width-1) //MB C not available
        h->flags &= ~C_AVAIL;
    /* clear top-right predictors if MB C is not available */
    if(!(h->flags & C_AVAIL)) {
        h->mv[MV_FWD_C2] = ff_cavs_un_mv;
        h->mv[MV_BWD_C2] = ff_cavs_un_mv;
    }
    /* clear top-left predictors if MB D is not available */
    if(!(h->flags & D_AVAIL)) {
        h->mv[MV_FWD_D3] = ff_cavs_un_mv;
        h->mv[MV_BWD_D3] = ff_cavs_un_mv;
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
            h->mv[i] = ff_cavs_un_mv;
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

#endif /* CAVS_H */
