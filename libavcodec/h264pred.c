/*
 * H.26L/H.264/AVC/JVT/14496-10/... encoder/decoder
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
 * H.264 / AVC / MPEG4 part10 prediction functions.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "h264pred.h"

#define BIT_DEPTH 8
#include "h264pred_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 9
#include "h264pred_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 10
#include "h264pred_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 12
#include "h264pred_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 14
#include "h264pred_template.c"
#undef BIT_DEPTH

static void pred4x4_vertical_vp8_c(uint8_t *src, const uint8_t *topright,
                                   ptrdiff_t stride)
{
    const unsigned lt = src[-1-1*stride];
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

static void pred4x4_horizontal_vp8_c(uint8_t *src, const uint8_t *topright,
                                     ptrdiff_t stride)
{
    const unsigned lt = src[-1-1*stride];
    LOAD_LEFT_EDGE

    AV_WN32A(src+0*stride, ((lt + 2*l0 + l1 + 2) >> 2)*0x01010101);
    AV_WN32A(src+1*stride, ((l0 + 2*l1 + l2 + 2) >> 2)*0x01010101);
    AV_WN32A(src+2*stride, ((l1 + 2*l2 + l3 + 2) >> 2)*0x01010101);
    AV_WN32A(src+3*stride, ((l2 + 2*l3 + l3 + 2) >> 2)*0x01010101);
}

static void pred4x4_down_left_svq3_c(uint8_t *src, const uint8_t *topright,
                                     ptrdiff_t stride)
{
    LOAD_TOP_EDGE
    LOAD_LEFT_EDGE

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

static void pred4x4_down_left_rv40_c(uint8_t *src, const uint8_t *topright,
                                     ptrdiff_t stride)
{
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

static void pred4x4_down_left_rv40_nodown_c(uint8_t *src,
                                            const uint8_t *topright,
                                            ptrdiff_t stride)
{
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

static void pred4x4_vertical_left_rv40(uint8_t *src, const uint8_t *topright,
                                       ptrdiff_t stride,
                                       const int l0, const int l1, const int l2,
                                       const int l3, const int l4)
{
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

static void pred4x4_vertical_left_rv40_c(uint8_t *src, const uint8_t *topright,
                                         ptrdiff_t stride)
{
    LOAD_LEFT_EDGE
    LOAD_DOWN_LEFT_EDGE

    pred4x4_vertical_left_rv40(src, topright, stride, l0, l1, l2, l3, l4);
}

static void pred4x4_vertical_left_rv40_nodown_c(uint8_t *src,
                                                const uint8_t *topright,
                                                ptrdiff_t stride)
{
    LOAD_LEFT_EDGE

    pred4x4_vertical_left_rv40(src, topright, stride, l0, l1, l2, l3, l3);
}

static void pred4x4_vertical_left_vp8_c(uint8_t *src, const uint8_t *topright,
                                        ptrdiff_t stride)
{
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

static void pred4x4_horizontal_up_rv40_c(uint8_t *src, const uint8_t *topright,
                                         ptrdiff_t stride)
{
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

static void pred4x4_horizontal_up_rv40_nodown_c(uint8_t *src,
                                                const uint8_t *topright,
                                                ptrdiff_t stride)
{
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

static void pred4x4_tm_vp8_c(uint8_t *src, const uint8_t *topright,
                             ptrdiff_t stride)
{
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP - src[-1-stride];
    uint8_t *top = src-stride;
    int y;

    for (y = 0; y < 4; y++) {
        const uint8_t *cm_in = cm + src[-1];
        src[0] = cm_in[top[0]];
        src[1] = cm_in[top[1]];
        src[2] = cm_in[top[2]];
        src[3] = cm_in[top[3]];
        src += stride;
    }
}

static void pred16x16_plane_svq3_c(uint8_t *src, ptrdiff_t stride)
{
    pred16x16_plane_compat_8_c(src, stride, 1, 0);
}

static void pred16x16_plane_rv40_c(uint8_t *src, ptrdiff_t stride)
{
    pred16x16_plane_compat_8_c(src, stride, 0, 1);
}

static void pred16x16_tm_vp8_c(uint8_t *src, ptrdiff_t stride)
{
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP - src[-1-stride];
    uint8_t *top = src-stride;
    int y;

    for (y = 0; y < 16; y++) {
        const uint8_t *cm_in = cm + src[-1];
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

static void pred8x8_left_dc_rv40_c(uint8_t *src, ptrdiff_t stride)
{
    int i;
    unsigned dc0;

    dc0=0;
    for(i=0;i<8; i++)
        dc0+= src[-1+i*stride];
    dc0= 0x01010101*((dc0 + 4)>>3);

    for(i=0; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]= dc0;
    }
}

static void pred8x8_top_dc_rv40_c(uint8_t *src, ptrdiff_t stride)
{
    int i;
    unsigned dc0;

    dc0=0;
    for(i=0;i<8; i++)
        dc0+= src[i-stride];
    dc0= 0x01010101*((dc0 + 4)>>3);

    for(i=0; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]= dc0;
    }
}

static void pred8x8_dc_rv40_c(uint8_t *src, ptrdiff_t stride)
{
    int i;
    unsigned dc0 = 0;

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

static void pred8x8_tm_vp8_c(uint8_t *src, ptrdiff_t stride)
{
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP - src[-1-stride];
    uint8_t *top = src-stride;
    int y;

    for (y = 0; y < 8; y++) {
        const uint8_t *cm_in = cm + src[-1];
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
av_cold void ff_h264_pred_init(H264PredContext *h, int codec_id,
                               const int bit_depth,
                               int chroma_format_idc)
{
#undef FUNC
#undef FUNCC
#define FUNC(a, depth) a ## _ ## depth
#define FUNCC(a, depth) a ## _ ## depth ## _c
#define FUNCD(a) a ## _c

#define H264_PRED(depth) \
    if(codec_id != AV_CODEC_ID_RV40){\
        if (codec_id == AV_CODEC_ID_VP7 || codec_id == AV_CODEC_ID_VP8) {\
            h->pred4x4[VERT_PRED       ]= FUNCD(pred4x4_vertical_vp8);\
            h->pred4x4[HOR_PRED        ]= FUNCD(pred4x4_horizontal_vp8);\
        } else {\
            h->pred4x4[VERT_PRED       ]= FUNCC(pred4x4_vertical          , depth);\
            h->pred4x4[HOR_PRED        ]= FUNCC(pred4x4_horizontal        , depth);\
        }\
        h->pred4x4[DC_PRED             ]= FUNCC(pred4x4_dc                , depth);\
        if(codec_id == AV_CODEC_ID_SVQ3)\
            h->pred4x4[DIAG_DOWN_LEFT_PRED ]= FUNCD(pred4x4_down_left_svq3);\
        else\
            h->pred4x4[DIAG_DOWN_LEFT_PRED ]= FUNCC(pred4x4_down_left     , depth);\
        h->pred4x4[DIAG_DOWN_RIGHT_PRED]= FUNCC(pred4x4_down_right        , depth);\
        h->pred4x4[VERT_RIGHT_PRED     ]= FUNCC(pred4x4_vertical_right    , depth);\
        h->pred4x4[HOR_DOWN_PRED       ]= FUNCC(pred4x4_horizontal_down   , depth);\
        if (codec_id == AV_CODEC_ID_VP7 || codec_id == AV_CODEC_ID_VP8) {\
            h->pred4x4[VERT_LEFT_PRED  ]= FUNCD(pred4x4_vertical_left_vp8);\
        } else\
            h->pred4x4[VERT_LEFT_PRED  ]= FUNCC(pred4x4_vertical_left     , depth);\
        h->pred4x4[HOR_UP_PRED         ]= FUNCC(pred4x4_horizontal_up     , depth);\
        if (codec_id != AV_CODEC_ID_VP7 && codec_id != AV_CODEC_ID_VP8) {\
            h->pred4x4[LEFT_DC_PRED    ]= FUNCC(pred4x4_left_dc           , depth);\
            h->pred4x4[TOP_DC_PRED     ]= FUNCC(pred4x4_top_dc            , depth);\
        } else {\
            h->pred4x4[TM_VP8_PRED     ]= FUNCD(pred4x4_tm_vp8);\
            h->pred4x4[DC_127_PRED     ]= FUNCC(pred4x4_127_dc            , depth);\
            h->pred4x4[DC_129_PRED     ]= FUNCC(pred4x4_129_dc            , depth);\
            h->pred4x4[VERT_VP8_PRED   ]= FUNCC(pred4x4_vertical          , depth);\
            h->pred4x4[HOR_VP8_PRED    ]= FUNCC(pred4x4_horizontal        , depth);\
        }\
        if (codec_id != AV_CODEC_ID_VP8)\
            h->pred4x4[DC_128_PRED     ]= FUNCC(pred4x4_128_dc            , depth);\
    }else{\
        h->pred4x4[VERT_PRED           ]= FUNCC(pred4x4_vertical          , depth);\
        h->pred4x4[HOR_PRED            ]= FUNCC(pred4x4_horizontal        , depth);\
        h->pred4x4[DC_PRED             ]= FUNCC(pred4x4_dc                , depth);\
        h->pred4x4[DIAG_DOWN_LEFT_PRED ]= FUNCD(pred4x4_down_left_rv40);\
        h->pred4x4[DIAG_DOWN_RIGHT_PRED]= FUNCC(pred4x4_down_right        , depth);\
        h->pred4x4[VERT_RIGHT_PRED     ]= FUNCC(pred4x4_vertical_right    , depth);\
        h->pred4x4[HOR_DOWN_PRED       ]= FUNCC(pred4x4_horizontal_down   , depth);\
        h->pred4x4[VERT_LEFT_PRED      ]= FUNCD(pred4x4_vertical_left_rv40);\
        h->pred4x4[HOR_UP_PRED         ]= FUNCD(pred4x4_horizontal_up_rv40);\
        h->pred4x4[LEFT_DC_PRED        ]= FUNCC(pred4x4_left_dc           , depth);\
        h->pred4x4[TOP_DC_PRED         ]= FUNCC(pred4x4_top_dc            , depth);\
        h->pred4x4[DC_128_PRED         ]= FUNCC(pred4x4_128_dc            , depth);\
        h->pred4x4[DIAG_DOWN_LEFT_PRED_RV40_NODOWN]= FUNCD(pred4x4_down_left_rv40_nodown);\
        h->pred4x4[HOR_UP_PRED_RV40_NODOWN]= FUNCD(pred4x4_horizontal_up_rv40_nodown);\
        h->pred4x4[VERT_LEFT_PRED_RV40_NODOWN]= FUNCD(pred4x4_vertical_left_rv40_nodown);\
    }\
\
    h->pred8x8l[VERT_PRED           ]= FUNCC(pred8x8l_vertical            , depth);\
    h->pred8x8l[HOR_PRED            ]= FUNCC(pred8x8l_horizontal          , depth);\
    h->pred8x8l[DC_PRED             ]= FUNCC(pred8x8l_dc                  , depth);\
    h->pred8x8l[DIAG_DOWN_LEFT_PRED ]= FUNCC(pred8x8l_down_left           , depth);\
    h->pred8x8l[DIAG_DOWN_RIGHT_PRED]= FUNCC(pred8x8l_down_right          , depth);\
    h->pred8x8l[VERT_RIGHT_PRED     ]= FUNCC(pred8x8l_vertical_right      , depth);\
    h->pred8x8l[HOR_DOWN_PRED       ]= FUNCC(pred8x8l_horizontal_down     , depth);\
    h->pred8x8l[VERT_LEFT_PRED      ]= FUNCC(pred8x8l_vertical_left       , depth);\
    h->pred8x8l[HOR_UP_PRED         ]= FUNCC(pred8x8l_horizontal_up       , depth);\
    h->pred8x8l[LEFT_DC_PRED        ]= FUNCC(pred8x8l_left_dc             , depth);\
    h->pred8x8l[TOP_DC_PRED         ]= FUNCC(pred8x8l_top_dc              , depth);\
    h->pred8x8l[DC_128_PRED         ]= FUNCC(pred8x8l_128_dc              , depth);\
\
    if (chroma_format_idc <= 1) {\
        h->pred8x8[VERT_PRED8x8   ]= FUNCC(pred8x8_vertical               , depth);\
        h->pred8x8[HOR_PRED8x8    ]= FUNCC(pred8x8_horizontal             , depth);\
    } else {\
        h->pred8x8[VERT_PRED8x8   ]= FUNCC(pred8x16_vertical              , depth);\
        h->pred8x8[HOR_PRED8x8    ]= FUNCC(pred8x16_horizontal            , depth);\
    }\
    if (codec_id != AV_CODEC_ID_VP7 && codec_id != AV_CODEC_ID_VP8) {\
        if (chroma_format_idc <= 1) {\
            h->pred8x8[PLANE_PRED8x8]= FUNCC(pred8x8_plane                , depth);\
        } else {\
            h->pred8x8[PLANE_PRED8x8]= FUNCC(pred8x16_plane               , depth);\
        }\
    } else\
        h->pred8x8[PLANE_PRED8x8]= FUNCD(pred8x8_tm_vp8);\
    if (codec_id != AV_CODEC_ID_RV40 && codec_id != AV_CODEC_ID_VP7 && \
        codec_id != AV_CODEC_ID_VP8) {\
        if (chroma_format_idc <= 1) {\
            h->pred8x8[DC_PRED8x8     ]= FUNCC(pred8x8_dc                     , depth);\
            h->pred8x8[LEFT_DC_PRED8x8]= FUNCC(pred8x8_left_dc                , depth);\
            h->pred8x8[TOP_DC_PRED8x8 ]= FUNCC(pred8x8_top_dc                 , depth);\
            h->pred8x8[ALZHEIMER_DC_L0T_PRED8x8 ]= FUNC(pred8x8_mad_cow_dc_l0t, depth);\
            h->pred8x8[ALZHEIMER_DC_0LT_PRED8x8 ]= FUNC(pred8x8_mad_cow_dc_0lt, depth);\
            h->pred8x8[ALZHEIMER_DC_L00_PRED8x8 ]= FUNC(pred8x8_mad_cow_dc_l00, depth);\
            h->pred8x8[ALZHEIMER_DC_0L0_PRED8x8 ]= FUNC(pred8x8_mad_cow_dc_0l0, depth);\
        } else {\
            h->pred8x8[DC_PRED8x8     ]= FUNCC(pred8x16_dc                    , depth);\
            h->pred8x8[LEFT_DC_PRED8x8]= FUNCC(pred8x16_left_dc               , depth);\
            h->pred8x8[TOP_DC_PRED8x8 ]= FUNCC(pred8x16_top_dc                , depth);\
            h->pred8x8[ALZHEIMER_DC_L0T_PRED8x8 ]= FUNC(pred8x16_mad_cow_dc_l0t, depth);\
            h->pred8x8[ALZHEIMER_DC_0LT_PRED8x8 ]= FUNC(pred8x16_mad_cow_dc_0lt, depth);\
            h->pred8x8[ALZHEIMER_DC_L00_PRED8x8 ]= FUNC(pred8x16_mad_cow_dc_l00, depth);\
            h->pred8x8[ALZHEIMER_DC_0L0_PRED8x8 ]= FUNC(pred8x16_mad_cow_dc_0l0, depth);\
        }\
    }else{\
        h->pred8x8[DC_PRED8x8     ]= FUNCD(pred8x8_dc_rv40);\
        h->pred8x8[LEFT_DC_PRED8x8]= FUNCD(pred8x8_left_dc_rv40);\
        h->pred8x8[TOP_DC_PRED8x8 ]= FUNCD(pred8x8_top_dc_rv40);\
        if (codec_id == AV_CODEC_ID_VP7 || codec_id == AV_CODEC_ID_VP8) {\
            h->pred8x8[DC_127_PRED8x8]= FUNCC(pred8x8_127_dc              , depth);\
            h->pred8x8[DC_129_PRED8x8]= FUNCC(pred8x8_129_dc              , depth);\
        }\
    }\
    if (chroma_format_idc <= 1) {\
        h->pred8x8[DC_128_PRED8x8 ]= FUNCC(pred8x8_128_dc                 , depth);\
    } else {\
        h->pred8x8[DC_128_PRED8x8 ]= FUNCC(pred8x16_128_dc                , depth);\
    }\
\
    h->pred16x16[DC_PRED8x8     ]= FUNCC(pred16x16_dc                     , depth);\
    h->pred16x16[VERT_PRED8x8   ]= FUNCC(pred16x16_vertical               , depth);\
    h->pred16x16[HOR_PRED8x8    ]= FUNCC(pred16x16_horizontal             , depth);\
    switch(codec_id){\
    case AV_CODEC_ID_SVQ3:\
       h->pred16x16[PLANE_PRED8x8  ]= FUNCD(pred16x16_plane_svq3);\
       break;\
    case AV_CODEC_ID_RV40:\
       h->pred16x16[PLANE_PRED8x8  ]= FUNCD(pred16x16_plane_rv40);\
       break;\
    case AV_CODEC_ID_VP7:\
    case AV_CODEC_ID_VP8:\
       h->pred16x16[PLANE_PRED8x8  ]= FUNCD(pred16x16_tm_vp8);\
       h->pred16x16[DC_127_PRED8x8]= FUNCC(pred16x16_127_dc               , depth);\
       h->pred16x16[DC_129_PRED8x8]= FUNCC(pred16x16_129_dc               , depth);\
       break;\
    default:\
       h->pred16x16[PLANE_PRED8x8  ]= FUNCC(pred16x16_plane               , depth);\
       break;\
    }\
    h->pred16x16[LEFT_DC_PRED8x8]= FUNCC(pred16x16_left_dc                , depth);\
    h->pred16x16[TOP_DC_PRED8x8 ]= FUNCC(pred16x16_top_dc                 , depth);\
    h->pred16x16[DC_128_PRED8x8 ]= FUNCC(pred16x16_128_dc                 , depth);\
\
    /* special lossless h/v prediction for h264 */ \
    h->pred4x4_add  [VERT_PRED   ]= FUNCC(pred4x4_vertical_add            , depth);\
    h->pred4x4_add  [ HOR_PRED   ]= FUNCC(pred4x4_horizontal_add          , depth);\
    h->pred8x8l_add [VERT_PRED   ]= FUNCC(pred8x8l_vertical_add           , depth);\
    h->pred8x8l_add [ HOR_PRED   ]= FUNCC(pred8x8l_horizontal_add         , depth);\
    h->pred8x8l_filter_add [VERT_PRED   ]= FUNCC(pred8x8l_vertical_filter_add           , depth);\
    h->pred8x8l_filter_add [ HOR_PRED   ]= FUNCC(pred8x8l_horizontal_filter_add         , depth);\
    if (chroma_format_idc <= 1) {\
    h->pred8x8_add  [VERT_PRED8x8]= FUNCC(pred8x8_vertical_add            , depth);\
    h->pred8x8_add  [ HOR_PRED8x8]= FUNCC(pred8x8_horizontal_add          , depth);\
    } else {\
        h->pred8x8_add  [VERT_PRED8x8]= FUNCC(pred8x16_vertical_add            , depth);\
        h->pred8x8_add  [ HOR_PRED8x8]= FUNCC(pred8x16_horizontal_add          , depth);\
    }\
    h->pred16x16_add[VERT_PRED8x8]= FUNCC(pred16x16_vertical_add          , depth);\
    h->pred16x16_add[ HOR_PRED8x8]= FUNCC(pred16x16_horizontal_add        , depth);\

    switch (bit_depth) {
        case 9:
            H264_PRED(9)
            break;
        case 10:
            H264_PRED(10)
            break;
        case 12:
            H264_PRED(12)
            break;
        case 14:
            H264_PRED(14)
            break;
        default:
            av_assert0(bit_depth<=8);
            H264_PRED(8)
            break;
    }

    if (ARCH_ARM) ff_h264_pred_init_arm(h, codec_id, bit_depth, chroma_format_idc);
    if (ARCH_X86) ff_h264_pred_init_x86(h, codec_id, bit_depth, chroma_format_idc);
}
