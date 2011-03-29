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

#include "h264pred.h"

#define BIT_DEPTH 8
#include "h264pred_internal.h"
#undef BIT_DEPTH

#define BIT_DEPTH 9
#include "h264pred_internal.h"
#undef BIT_DEPTH

#define BIT_DEPTH 10
#include "h264pred_internal.h"
#undef BIT_DEPTH

/**
 * Set the intra prediction function pointers.
 */
void ff_h264_pred_init(H264PredContext *h, int codec_id, const int bit_depth){
//    MpegEncContext * const s = &h->s;

#undef FUNC
#undef FUNCC
#define FUNC(a, depth) a ## _ ## depth
#define FUNCC(a, depth) a ## _ ## depth ## _c

#define H264_PRED(depth) \
    if(codec_id != CODEC_ID_RV40){\
        if(codec_id == CODEC_ID_VP8) {\
            h->pred4x4[VERT_PRED       ]= FUNCC(pred4x4_vertical_vp8      , depth);\
            h->pred4x4[HOR_PRED        ]= FUNCC(pred4x4_horizontal_vp8    , depth);\
        } else {\
            h->pred4x4[VERT_PRED       ]= FUNCC(pred4x4_vertical          , depth);\
            h->pred4x4[HOR_PRED        ]= FUNCC(pred4x4_horizontal        , depth);\
        }\
        h->pred4x4[DC_PRED             ]= FUNCC(pred4x4_dc                , depth);\
        if(codec_id == CODEC_ID_SVQ3)\
            h->pred4x4[DIAG_DOWN_LEFT_PRED ]= FUNCC(pred4x4_down_left_svq3, depth);\
        else\
            h->pred4x4[DIAG_DOWN_LEFT_PRED ]= FUNCC(pred4x4_down_left     , depth);\
        h->pred4x4[DIAG_DOWN_RIGHT_PRED]= FUNCC(pred4x4_down_right        , depth);\
        h->pred4x4[VERT_RIGHT_PRED     ]= FUNCC(pred4x4_vertical_right    , depth);\
        h->pred4x4[HOR_DOWN_PRED       ]= FUNCC(pred4x4_horizontal_down   , depth);\
        if (codec_id == CODEC_ID_VP8) {\
            h->pred4x4[VERT_LEFT_PRED  ]= FUNCC(pred4x4_vertical_left_vp8 , depth);\
        } else\
            h->pred4x4[VERT_LEFT_PRED  ]= FUNCC(pred4x4_vertical_left     , depth);\
        h->pred4x4[HOR_UP_PRED         ]= FUNCC(pred4x4_horizontal_up     , depth);\
        if(codec_id != CODEC_ID_VP8) {\
            h->pred4x4[LEFT_DC_PRED    ]= FUNCC(pred4x4_left_dc           , depth);\
            h->pred4x4[TOP_DC_PRED     ]= FUNCC(pred4x4_top_dc            , depth);\
            h->pred4x4[DC_128_PRED     ]= FUNCC(pred4x4_128_dc            , depth);\
        } else {\
            h->pred4x4[TM_VP8_PRED     ]= FUNCC(pred4x4_tm_vp8            , depth);\
            h->pred4x4[DC_127_PRED     ]= FUNCC(pred4x4_127_dc            , depth);\
            h->pred4x4[DC_129_PRED     ]= FUNCC(pred4x4_129_dc            , depth);\
            h->pred4x4[VERT_VP8_PRED   ]= FUNCC(pred4x4_vertical          , depth);\
            h->pred4x4[HOR_VP8_PRED    ]= FUNCC(pred4x4_horizontal        , depth);\
        }\
    }else{\
        h->pred4x4[VERT_PRED           ]= FUNCC(pred4x4_vertical          , depth);\
        h->pred4x4[HOR_PRED            ]= FUNCC(pred4x4_horizontal        , depth);\
        h->pred4x4[DC_PRED             ]= FUNCC(pred4x4_dc                , depth);\
        h->pred4x4[DIAG_DOWN_LEFT_PRED ]= FUNCC(pred4x4_down_left_rv40    , depth);\
        h->pred4x4[DIAG_DOWN_RIGHT_PRED]= FUNCC(pred4x4_down_right        , depth);\
        h->pred4x4[VERT_RIGHT_PRED     ]= FUNCC(pred4x4_vertical_right    , depth);\
        h->pred4x4[HOR_DOWN_PRED       ]= FUNCC(pred4x4_horizontal_down   , depth);\
        h->pred4x4[VERT_LEFT_PRED      ]= FUNCC(pred4x4_vertical_left_rv40, depth);\
        h->pred4x4[HOR_UP_PRED         ]= FUNCC(pred4x4_horizontal_up_rv40, depth);\
        h->pred4x4[LEFT_DC_PRED        ]= FUNCC(pred4x4_left_dc           , depth);\
        h->pred4x4[TOP_DC_PRED         ]= FUNCC(pred4x4_top_dc            , depth);\
        h->pred4x4[DC_128_PRED         ]= FUNCC(pred4x4_128_dc            , depth);\
        h->pred4x4[DIAG_DOWN_LEFT_PRED_RV40_NODOWN]= FUNCC(pred4x4_down_left_rv40_nodown, depth);\
        h->pred4x4[HOR_UP_PRED_RV40_NODOWN]= FUNCC(pred4x4_horizontal_up_rv40_nodown    , depth);\
        h->pred4x4[VERT_LEFT_PRED_RV40_NODOWN]= FUNCC(pred4x4_vertical_left_rv40_nodown , depth);\
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
    h->pred8x8[VERT_PRED8x8   ]= FUNCC(pred8x8_vertical                   , depth);\
    h->pred8x8[HOR_PRED8x8    ]= FUNCC(pred8x8_horizontal                 , depth);\
    if (codec_id != CODEC_ID_VP8) {\
        h->pred8x8[PLANE_PRED8x8]= FUNCC(pred8x8_plane                    , depth);\
    } else\
        h->pred8x8[PLANE_PRED8x8]= FUNCC(pred8x8_tm_vp8                   , depth);\
    if(codec_id != CODEC_ID_RV40 && codec_id != CODEC_ID_VP8){\
        h->pred8x8[DC_PRED8x8     ]= FUNCC(pred8x8_dc                     , depth);\
        h->pred8x8[LEFT_DC_PRED8x8]= FUNCC(pred8x8_left_dc                , depth);\
        h->pred8x8[TOP_DC_PRED8x8 ]= FUNCC(pred8x8_top_dc                 , depth);\
        h->pred8x8[ALZHEIMER_DC_L0T_PRED8x8 ]= FUNC(pred8x8_mad_cow_dc_l0t, depth);\
        h->pred8x8[ALZHEIMER_DC_0LT_PRED8x8 ]= FUNC(pred8x8_mad_cow_dc_0lt, depth);\
        h->pred8x8[ALZHEIMER_DC_L00_PRED8x8 ]= FUNC(pred8x8_mad_cow_dc_l00, depth);\
        h->pred8x8[ALZHEIMER_DC_0L0_PRED8x8 ]= FUNC(pred8x8_mad_cow_dc_0l0, depth);\
    }else{\
        h->pred8x8[DC_PRED8x8     ]= FUNCC(pred8x8_dc_rv40                , depth);\
        h->pred8x8[LEFT_DC_PRED8x8]= FUNCC(pred8x8_left_dc_rv40           , depth);\
        h->pred8x8[TOP_DC_PRED8x8 ]= FUNCC(pred8x8_top_dc_rv40            , depth);\
        if (codec_id == CODEC_ID_VP8) {\
            h->pred8x8[DC_127_PRED8x8]= FUNCC(pred8x8_127_dc              , depth);\
            h->pred8x8[DC_129_PRED8x8]= FUNCC(pred8x8_129_dc              , depth);\
        }\
    }\
    h->pred8x8[DC_128_PRED8x8 ]= FUNCC(pred8x8_128_dc                     , depth);\
\
    h->pred16x16[DC_PRED8x8     ]= FUNCC(pred16x16_dc                     , depth);\
    h->pred16x16[VERT_PRED8x8   ]= FUNCC(pred16x16_vertical               , depth);\
    h->pred16x16[HOR_PRED8x8    ]= FUNCC(pred16x16_horizontal             , depth);\
    switch(codec_id){\
    case CODEC_ID_SVQ3:\
       h->pred16x16[PLANE_PRED8x8  ]= FUNCC(pred16x16_plane_svq3          , depth);\
       break;\
    case CODEC_ID_RV40:\
       h->pred16x16[PLANE_PRED8x8  ]= FUNCC(pred16x16_plane_rv40          , depth);\
       break;\
    case CODEC_ID_VP8:\
       h->pred16x16[PLANE_PRED8x8  ]= FUNCC(pred16x16_tm_vp8              , depth);\
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
    h->pred8x8_add  [VERT_PRED8x8]= FUNCC(pred8x8_vertical_add            , depth);\
    h->pred8x8_add  [ HOR_PRED8x8]= FUNCC(pred8x8_horizontal_add          , depth);\
    h->pred16x16_add[VERT_PRED8x8]= FUNCC(pred16x16_vertical_add          , depth);\
    h->pred16x16_add[ HOR_PRED8x8]= FUNCC(pred16x16_horizontal_add        , depth);\

    switch (bit_depth) {
        case 9:
            H264_PRED(9)
            break;
        case 10:
            H264_PRED(10)
            break;
        default:
            H264_PRED(8)
            break;
    }

    if (ARCH_ARM) ff_h264_pred_init_arm(h, codec_id, bit_depth);
    if (HAVE_MMX) ff_h264_pred_init_x86(h, codec_id, bit_depth);
}
