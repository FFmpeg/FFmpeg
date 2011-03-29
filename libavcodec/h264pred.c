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
#include "h264pred_internal.h"

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
