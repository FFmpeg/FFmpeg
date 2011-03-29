/*
 * H.26L/H.264/AVC/JVT/14496-10/... encoder/decoder
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * H.264 / AVC / MPEG4 part10 prediction functions.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "h264pred.h"
#include "h264pred_template.c"

static void pred4x4_vertical_vp8_c(uint8_t *src, const uint8_t *topright, int stride){
    const int lt= src[-1-1*stride];
    LOAD_TOP_EDGE
    LOAD_TOP_RIGHT_EDGE
    uint32_t v = PACK_4U8((lt + 2*t0 + t1 + 2) >> 2,
                          (t0 + 2*t1 + t2 + 2) >> 2,
                          (t1 + 2*t2 + t3 + 2) >> 2,
                          (t2 + 2*t3 + t4 + 2) >> 2);

    AV_WN32A(src+0*stride, v);
    AV_WN32A(src+1*stride, v);
    AV_WN32A(src+2*stride, v);
    AV_WN32A(src+3*stride, v);
}

static void pred4x4_horizontal_vp8_c(uint8_t *src, const uint8_t *topright, int stride){
    const int lt= src[-1-1*stride];
    LOAD_LEFT_EDGE

    AV_WN32A(src+0*stride, ((lt + 2*l0 + l1 + 2) >> 2)*0x01010101);
    AV_WN32A(src+1*stride, ((l0 + 2*l1 + l2 + 2) >> 2)*0x01010101);
    AV_WN32A(src+2*stride, ((l1 + 2*l2 + l3 + 2) >> 2)*0x01010101);
    AV_WN32A(src+3*stride, ((l2 + 2*l3 + l3 + 2) >> 2)*0x01010101);
}

static void pred4x4_down_left_svq3_c(uint8_t *src, const uint8_t *topright, int stride){
    LOAD_TOP_EDGE
    LOAD_LEFT_EDGE
    const av_unused int unu0= t0;
    const av_unused int unu1= l0;

    src[0+0*stride]=(l1 + t1)>>1;
    src[1+0*stride]=
    src[0+1*stride]=(l2 + t2)>>1;
    src[2+0*stride]=
    src[1+1*stride]=
    src[0+2*stride]=
    src[3+0*stride]=
    src[2+1*stride]=
    src[1+2*stride]=
    src[0+3*stride]=
    src[3+1*stride]=
    src[2+2*stride]=
    src[1+3*stride]=
    src[3+2*stride]=
    src[2+3*stride]=
    src[3+3*stride]=(l3 + t3)>>1;
}

static void pred4x4_down_left_rv40_c(uint8_t *src, const uint8_t *topright, int stride){
    LOAD_TOP_EDGE
    LOAD_TOP_RIGHT_EDGE
    LOAD_LEFT_EDGE
    LOAD_DOWN_LEFT_EDGE

    src[0+0*stride]=(t0 + t2 + 2*t1 + 2 + l0 + l2 + 2*l1 + 2)>>3;
    src[1+0*stride]=
    src[0+1*stride]=(t1 + t3 + 2*t2 + 2 + l1 + l3 + 2*l2 + 2)>>3;
    src[2+0*stride]=
    src[1+1*stride]=
    src[0+2*stride]=(t2 + t4 + 2*t3 + 2 + l2 + l4 + 2*l3 + 2)>>3;
    src[3+0*stride]=
    src[2+1*stride]=
    src[1+2*stride]=
    src[0+3*stride]=(t3 + t5 + 2*t4 + 2 + l3 + l5 + 2*l4 + 2)>>3;
    src[3+1*stride]=
    src[2+2*stride]=
    src[1+3*stride]=(t4 + t6 + 2*t5 + 2 + l4 + l6 + 2*l5 + 2)>>3;
    src[3+2*stride]=
    src[2+3*stride]=(t5 + t7 + 2*t6 + 2 + l5 + l7 + 2*l6 + 2)>>3;
    src[3+3*stride]=(t6 + t7 + 1 + l6 + l7 + 1)>>2;
}

static void pred4x4_down_left_rv40_nodown_c(uint8_t *src, const uint8_t *topright, int stride){
    LOAD_TOP_EDGE
    LOAD_TOP_RIGHT_EDGE
    LOAD_LEFT_EDGE

    src[0+0*stride]=(t0 + t2 + 2*t1 + 2 + l0 + l2 + 2*l1 + 2)>>3;
    src[1+0*stride]=
    src[0+1*stride]=(t1 + t3 + 2*t2 + 2 + l1 + l3 + 2*l2 + 2)>>3;
    src[2+0*stride]=
    src[1+1*stride]=
    src[0+2*stride]=(t2 + t4 + 2*t3 + 2 + l2 + 3*l3 + 2)>>3;
    src[3+0*stride]=
    src[2+1*stride]=
    src[1+2*stride]=
    src[0+3*stride]=(t3 + t5 + 2*t4 + 2 + l3*4 + 2)>>3;
    src[3+1*stride]=
    src[2+2*stride]=
    src[1+3*stride]=(t4 + t6 + 2*t5 + 2 + l3*4 + 2)>>3;
    src[3+2*stride]=
    src[2+3*stride]=(t5 + t7 + 2*t6 + 2 + l3*4 + 2)>>3;
    src[3+3*stride]=(t6 + t7 + 1 + 2*l3 + 1)>>2;
}

static void pred4x4_vertical_left_rv40(uint8_t *src, const uint8_t *topright, int stride,
                                       const int l0, const int l1, const int l2, const int l3, const int l4){
    LOAD_TOP_EDGE
    LOAD_TOP_RIGHT_EDGE

    src[0+0*stride]=(2*t0 + 2*t1 + l1 + 2*l2 + l3 + 4)>>3;
    src[1+0*stride]=
    src[0+2*stride]=(t1 + t2 + 1)>>1;
    src[2+0*stride]=
    src[1+2*stride]=(t2 + t3 + 1)>>1;
    src[3+0*stride]=
    src[2+2*stride]=(t3 + t4+ 1)>>1;
    src[3+2*stride]=(t4 + t5+ 1)>>1;
    src[0+1*stride]=(t0 + 2*t1 + t2 + l2 + 2*l3 + l4 + 4)>>3;
    src[1+1*stride]=
    src[0+3*stride]=(t1 + 2*t2 + t3 + 2)>>2;
    src[2+1*stride]=
    src[1+3*stride]=(t2 + 2*t3 + t4 + 2)>>2;
    src[3+1*stride]=
    src[2+3*stride]=(t3 + 2*t4 + t5 + 2)>>2;
    src[3+3*stride]=(t4 + 2*t5 + t6 + 2)>>2;
}

static void pred4x4_vertical_left_rv40_c(uint8_t *src, const uint8_t *topright, int stride){
    LOAD_LEFT_EDGE
    LOAD_DOWN_LEFT_EDGE

    pred4x4_vertical_left_rv40(src, topright, stride, l0, l1, l2, l3, l4);
}

static void pred4x4_vertical_left_rv40_nodown_c(uint8_t *src, const uint8_t *topright, int stride){
    LOAD_LEFT_EDGE

    pred4x4_vertical_left_rv40(src, topright, stride, l0, l1, l2, l3, l3);
}

static void pred4x4_vertical_left_vp8_c(uint8_t *src, const uint8_t *topright, int stride){
    LOAD_TOP_EDGE
    LOAD_TOP_RIGHT_EDGE

    src[0+0*stride]=(t0 + t1 + 1)>>1;
    src[1+0*stride]=
    src[0+2*stride]=(t1 + t2 + 1)>>1;
    src[2+0*stride]=
    src[1+2*stride]=(t2 + t3 + 1)>>1;
    src[3+0*stride]=
    src[2+2*stride]=(t3 + t4 + 1)>>1;
    src[0+1*stride]=(t0 + 2*t1 + t2 + 2)>>2;
    src[1+1*stride]=
    src[0+3*stride]=(t1 + 2*t2 + t3 + 2)>>2;
    src[2+1*stride]=
    src[1+3*stride]=(t2 + 2*t3 + t4 + 2)>>2;
    src[3+1*stride]=
    src[2+3*stride]=(t3 + 2*t4 + t5 + 2)>>2;
    src[3+2*stride]=(t4 + 2*t5 + t6 + 2)>>2;
    src[3+3*stride]=(t5 + 2*t6 + t7 + 2)>>2;
}

static void pred4x4_horizontal_up_rv40_c(uint8_t *src, const uint8_t *topright, int stride){
    LOAD_LEFT_EDGE
    LOAD_DOWN_LEFT_EDGE
    LOAD_TOP_EDGE
    LOAD_TOP_RIGHT_EDGE

    src[0+0*stride]=(t1 + 2*t2 + t3 + 2*l0 + 2*l1 + 4)>>3;
    src[1+0*stride]=(t2 + 2*t3 + t4 + l0 + 2*l1 + l2 + 4)>>3;
    src[2+0*stride]=
    src[0+1*stride]=(t3 + 2*t4 + t5 + 2*l1 + 2*l2 + 4)>>3;
    src[3+0*stride]=
    src[1+1*stride]=(t4 + 2*t5 + t6 + l1 + 2*l2 + l3 + 4)>>3;
    src[2+1*stride]=
    src[0+2*stride]=(t5 + 2*t6 + t7 + 2*l2 + 2*l3 + 4)>>3;
    src[3+1*stride]=
    src[1+2*stride]=(t6 + 3*t7 + l2 + 3*l3 + 4)>>3;
    src[3+2*stride]=
    src[1+3*stride]=(l3 + 2*l4 + l5 + 2)>>2;
    src[0+3*stride]=
    src[2+2*stride]=(t6 + t7 + l3 + l4 + 2)>>2;
    src[2+3*stride]=(l4 + l5 + 1)>>1;
    src[3+3*stride]=(l4 + 2*l5 + l6 + 2)>>2;
}

static void pred4x4_horizontal_up_rv40_nodown_c(uint8_t *src, const uint8_t *topright, int stride){
    LOAD_LEFT_EDGE
    LOAD_TOP_EDGE
    LOAD_TOP_RIGHT_EDGE

    src[0+0*stride]=(t1 + 2*t2 + t3 + 2*l0 + 2*l1 + 4)>>3;
    src[1+0*stride]=(t2 + 2*t3 + t4 + l0 + 2*l1 + l2 + 4)>>3;
    src[2+0*stride]=
    src[0+1*stride]=(t3 + 2*t4 + t5 + 2*l1 + 2*l2 + 4)>>3;
    src[3+0*stride]=
    src[1+1*stride]=(t4 + 2*t5 + t6 + l1 + 2*l2 + l3 + 4)>>3;
    src[2+1*stride]=
    src[0+2*stride]=(t5 + 2*t6 + t7 + 2*l2 + 2*l3 + 4)>>3;
    src[3+1*stride]=
    src[1+2*stride]=(t6 + 3*t7 + l2 + 3*l3 + 4)>>3;
    src[3+2*stride]=
    src[1+3*stride]=l3;
    src[0+3*stride]=
    src[2+2*stride]=(t6 + t7 + 2*l3 + 2)>>2;
    src[2+3*stride]=
    src[3+3*stride]=l3;
}

static void pred4x4_tm_vp8_c(uint8_t *src, const uint8_t *topright, int stride){
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP - src[-1-stride];
    uint8_t *top = src-stride;
    int y;

    for (y = 0; y < 4; y++) {
        uint8_t *cm_in = cm + src[-1];
        src[0] = cm_in[top[0]];
        src[1] = cm_in[top[1]];
        src[2] = cm_in[top[2]];
        src[3] = cm_in[top[3]];
        src += stride;
    }
}

static void pred16x16_plane_svq3_c(uint8_t *src, int stride){
    pred16x16_plane_compat_c(src, stride, 1, 0);
}

static void pred16x16_plane_rv40_c(uint8_t *src, int stride){
    pred16x16_plane_compat_c(src, stride, 0, 1);
}

static void pred16x16_tm_vp8_c(uint8_t *src, int stride){
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP - src[-1-stride];
    uint8_t *top = src-stride;
    int y;

    for (y = 0; y < 16; y++) {
        uint8_t *cm_in = cm + src[-1];
        src[0]  = cm_in[top[0]];
        src[1]  = cm_in[top[1]];
        src[2]  = cm_in[top[2]];
        src[3]  = cm_in[top[3]];
        src[4]  = cm_in[top[4]];
        src[5]  = cm_in[top[5]];
        src[6]  = cm_in[top[6]];
        src[7]  = cm_in[top[7]];
        src[8]  = cm_in[top[8]];
        src[9]  = cm_in[top[9]];
        src[10] = cm_in[top[10]];
        src[11] = cm_in[top[11]];
        src[12] = cm_in[top[12]];
        src[13] = cm_in[top[13]];
        src[14] = cm_in[top[14]];
        src[15] = cm_in[top[15]];
        src += stride;
    }
}

static void pred8x8_left_dc_rv40_c(uint8_t *src, int stride){
    int i;
    int dc0;

    dc0=0;
    for(i=0;i<8; i++)
        dc0+= src[-1+i*stride];
    dc0= 0x01010101*((dc0 + 4)>>3);

    for(i=0; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]= dc0;
    }
}

static void pred8x8_top_dc_rv40_c(uint8_t *src, int stride){
    int i;
    int dc0;

    dc0=0;
    for(i=0;i<8; i++)
        dc0+= src[i-stride];
    dc0= 0x01010101*((dc0 + 4)>>3);

    for(i=0; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]= dc0;
    }
}

static void pred8x8_dc_rv40_c(uint8_t *src, int stride){
    int i;
    int dc0=0;

    for(i=0;i<4; i++){
        dc0+= src[-1+i*stride] + src[i-stride];
        dc0+= src[4+i-stride];
        dc0+= src[-1+(i+4)*stride];
    }
    dc0= 0x01010101*((dc0 + 8)>>4);

    for(i=0; i<4; i++){
        ((uint32_t*)(src+i*stride))[0]= dc0;
        ((uint32_t*)(src+i*stride))[1]= dc0;
    }
    for(i=4; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]= dc0;
        ((uint32_t*)(src+i*stride))[1]= dc0;
    }
}

static void pred8x8_tm_vp8_c(uint8_t *src, int stride){
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP - src[-1-stride];
    uint8_t *top = src-stride;
    int y;

    for (y = 0; y < 8; y++) {
        uint8_t *cm_in = cm + src[-1];
        src[0] = cm_in[top[0]];
        src[1] = cm_in[top[1]];
        src[2] = cm_in[top[2]];
        src[3] = cm_in[top[3]];
        src[4] = cm_in[top[4]];
        src[5] = cm_in[top[5]];
        src[6] = cm_in[top[6]];
        src[7] = cm_in[top[7]];
        src += stride;
    }
}

/**
 * Set the intra prediction function pointers.
 */
void ff_h264_pred_init(H264PredContext *h, int codec_id){
//    MpegEncContext * const s = &h->s;

    if(codec_id != CODEC_ID_RV40){
        if(codec_id == CODEC_ID_VP8) {
            h->pred4x4[VERT_PRED       ]= pred4x4_vertical_vp8_c;
            h->pred4x4[HOR_PRED        ]= pred4x4_horizontal_vp8_c;
        } else {
            h->pred4x4[VERT_PRED       ]= pred4x4_vertical_c;
            h->pred4x4[HOR_PRED        ]= pred4x4_horizontal_c;
        }
        h->pred4x4[DC_PRED             ]= pred4x4_dc_c;
        if(codec_id == CODEC_ID_SVQ3)
            h->pred4x4[DIAG_DOWN_LEFT_PRED ]= pred4x4_down_left_svq3_c;
        else
            h->pred4x4[DIAG_DOWN_LEFT_PRED ]= pred4x4_down_left_c;
        h->pred4x4[DIAG_DOWN_RIGHT_PRED]= pred4x4_down_right_c;
        h->pred4x4[VERT_RIGHT_PRED     ]= pred4x4_vertical_right_c;
        h->pred4x4[HOR_DOWN_PRED       ]= pred4x4_horizontal_down_c;
        if (codec_id == CODEC_ID_VP8) {
            h->pred4x4[VERT_LEFT_PRED  ]= pred4x4_vertical_left_vp8_c;
        } else
            h->pred4x4[VERT_LEFT_PRED  ]= pred4x4_vertical_left_c;
        h->pred4x4[HOR_UP_PRED         ]= pred4x4_horizontal_up_c;
        if(codec_id != CODEC_ID_VP8) {
            h->pred4x4[LEFT_DC_PRED    ]= pred4x4_left_dc_c;
            h->pred4x4[TOP_DC_PRED     ]= pred4x4_top_dc_c;
            h->pred4x4[DC_128_PRED     ]= pred4x4_128_dc_c;
        } else {
            h->pred4x4[TM_VP8_PRED     ]= pred4x4_tm_vp8_c;
            h->pred4x4[DC_127_PRED     ]= pred4x4_127_dc_c;
            h->pred4x4[DC_129_PRED     ]= pred4x4_129_dc_c;
            h->pred4x4[VERT_VP8_PRED   ]= pred4x4_vertical_c;
            h->pred4x4[HOR_VP8_PRED    ]= pred4x4_horizontal_c;
        }
    }else{
        h->pred4x4[VERT_PRED           ]= pred4x4_vertical_c;
        h->pred4x4[HOR_PRED            ]= pred4x4_horizontal_c;
        h->pred4x4[DC_PRED             ]= pred4x4_dc_c;
        h->pred4x4[DIAG_DOWN_LEFT_PRED ]= pred4x4_down_left_rv40_c;
        h->pred4x4[DIAG_DOWN_RIGHT_PRED]= pred4x4_down_right_c;
        h->pred4x4[VERT_RIGHT_PRED     ]= pred4x4_vertical_right_c;
        h->pred4x4[HOR_DOWN_PRED       ]= pred4x4_horizontal_down_c;
        h->pred4x4[VERT_LEFT_PRED      ]= pred4x4_vertical_left_rv40_c;
        h->pred4x4[HOR_UP_PRED         ]= pred4x4_horizontal_up_rv40_c;
        h->pred4x4[LEFT_DC_PRED        ]= pred4x4_left_dc_c;
        h->pred4x4[TOP_DC_PRED         ]= pred4x4_top_dc_c;
        h->pred4x4[DC_128_PRED         ]= pred4x4_128_dc_c;
        h->pred4x4[DIAG_DOWN_LEFT_PRED_RV40_NODOWN]= pred4x4_down_left_rv40_nodown_c;
        h->pred4x4[HOR_UP_PRED_RV40_NODOWN]= pred4x4_horizontal_up_rv40_nodown_c;
        h->pred4x4[VERT_LEFT_PRED_RV40_NODOWN]= pred4x4_vertical_left_rv40_nodown_c;
    }

    h->pred8x8l[VERT_PRED           ]= pred8x8l_vertical_c;
    h->pred8x8l[HOR_PRED            ]= pred8x8l_horizontal_c;
    h->pred8x8l[DC_PRED             ]= pred8x8l_dc_c;
    h->pred8x8l[DIAG_DOWN_LEFT_PRED ]= pred8x8l_down_left_c;
    h->pred8x8l[DIAG_DOWN_RIGHT_PRED]= pred8x8l_down_right_c;
    h->pred8x8l[VERT_RIGHT_PRED     ]= pred8x8l_vertical_right_c;
    h->pred8x8l[HOR_DOWN_PRED       ]= pred8x8l_horizontal_down_c;
    h->pred8x8l[VERT_LEFT_PRED      ]= pred8x8l_vertical_left_c;
    h->pred8x8l[HOR_UP_PRED         ]= pred8x8l_horizontal_up_c;
    h->pred8x8l[LEFT_DC_PRED        ]= pred8x8l_left_dc_c;
    h->pred8x8l[TOP_DC_PRED         ]= pred8x8l_top_dc_c;
    h->pred8x8l[DC_128_PRED         ]= pred8x8l_128_dc_c;

    h->pred8x8[VERT_PRED8x8   ]= pred8x8_vertical_c;
    h->pred8x8[HOR_PRED8x8    ]= pred8x8_horizontal_c;
    if (codec_id != CODEC_ID_VP8) {
        h->pred8x8[PLANE_PRED8x8]= pred8x8_plane_c;
    } else
        h->pred8x8[PLANE_PRED8x8]= pred8x8_tm_vp8_c;
    if(codec_id != CODEC_ID_RV40 && codec_id != CODEC_ID_VP8){
        h->pred8x8[DC_PRED8x8     ]= pred8x8_dc_c;
        h->pred8x8[LEFT_DC_PRED8x8]= pred8x8_left_dc_c;
        h->pred8x8[TOP_DC_PRED8x8 ]= pred8x8_top_dc_c;
        h->pred8x8[ALZHEIMER_DC_L0T_PRED8x8 ]= pred8x8_mad_cow_dc_l0t;
        h->pred8x8[ALZHEIMER_DC_0LT_PRED8x8 ]= pred8x8_mad_cow_dc_0lt;
        h->pred8x8[ALZHEIMER_DC_L00_PRED8x8 ]= pred8x8_mad_cow_dc_l00;
        h->pred8x8[ALZHEIMER_DC_0L0_PRED8x8 ]= pred8x8_mad_cow_dc_0l0;
    }else{
        h->pred8x8[DC_PRED8x8     ]= pred8x8_dc_rv40_c;
        h->pred8x8[LEFT_DC_PRED8x8]= pred8x8_left_dc_rv40_c;
        h->pred8x8[TOP_DC_PRED8x8 ]= pred8x8_top_dc_rv40_c;
        if (codec_id == CODEC_ID_VP8) {
            h->pred8x8[DC_127_PRED8x8]= pred8x8_127_dc_c;
            h->pred8x8[DC_129_PRED8x8]= pred8x8_129_dc_c;
        }
    }
    h->pred8x8[DC_128_PRED8x8 ]= pred8x8_128_dc_c;

    h->pred16x16[DC_PRED8x8     ]= pred16x16_dc_c;
    h->pred16x16[VERT_PRED8x8   ]= pred16x16_vertical_c;
    h->pred16x16[HOR_PRED8x8    ]= pred16x16_horizontal_c;
    switch(codec_id){
    case CODEC_ID_SVQ3:
       h->pred16x16[PLANE_PRED8x8  ]= pred16x16_plane_svq3_c;
       break;
    case CODEC_ID_RV40:
       h->pred16x16[PLANE_PRED8x8  ]= pred16x16_plane_rv40_c;
       break;
    case CODEC_ID_VP8:
       h->pred16x16[PLANE_PRED8x8  ]= pred16x16_tm_vp8_c;
       h->pred16x16[DC_127_PRED8x8]= pred16x16_127_dc_c;
       h->pred16x16[DC_129_PRED8x8]= pred16x16_129_dc_c;
       break;
    default:
       h->pred16x16[PLANE_PRED8x8  ]= pred16x16_plane_c;
       break;
    }
    h->pred16x16[LEFT_DC_PRED8x8]= pred16x16_left_dc_c;
    h->pred16x16[TOP_DC_PRED8x8 ]= pred16x16_top_dc_c;
    h->pred16x16[DC_128_PRED8x8 ]= pred16x16_128_dc_c;

    //special lossless h/v prediction for h264
    h->pred4x4_add  [VERT_PRED   ]= pred4x4_vertical_add_c;
    h->pred4x4_add  [ HOR_PRED   ]= pred4x4_horizontal_add_c;
    h->pred8x8l_add [VERT_PRED   ]= pred8x8l_vertical_add_c;
    h->pred8x8l_add [ HOR_PRED   ]= pred8x8l_horizontal_add_c;
    h->pred8x8_add  [VERT_PRED8x8]= pred8x8_vertical_add_c;
    h->pred8x8_add  [ HOR_PRED8x8]= pred8x8_horizontal_add_c;
    h->pred16x16_add[VERT_PRED8x8]= pred16x16_vertical_add_c;
    h->pred16x16_add[ HOR_PRED8x8]= pred16x16_horizontal_add_c;

    if (ARCH_ARM) ff_h264_pred_init_arm(h, codec_id);
    if (HAVE_MMX) ff_h264_pred_init_x86(h, codec_id);
}
