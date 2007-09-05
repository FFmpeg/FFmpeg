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
 * @file h264pred.h
 * H.264 / AVC / MPEG4 prediction functions.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#ifndef H264PRED_H
#define H264PRED_H

#include "common.h"

/**
 * Prediction types
 */
//@{
#define VERT_PRED             0
#define HOR_PRED              1
#define DC_PRED               2
#define DIAG_DOWN_LEFT_PRED   3
#define DIAG_DOWN_RIGHT_PRED  4
#define VERT_RIGHT_PRED       5
#define HOR_DOWN_PRED         6
#define VERT_LEFT_PRED        7
#define HOR_UP_PRED           8

#define LEFT_DC_PRED          9
#define TOP_DC_PRED           10
#define DC_128_PRED           11

#define DIAG_DOWN_LEFT_PRED_RV40_NODOWN   12
#define HOR_UP_PRED_RV40_NODOWN           13

#define DC_PRED8x8            0
#define HOR_PRED8x8           1
#define VERT_PRED8x8          2
#define PLANE_PRED8x8         3

#define LEFT_DC_PRED8x8       4
#define TOP_DC_PRED8x8        5
#define DC_128_PRED8x8        6
//@}

/**
 * Context for storing H.264 prediction functions
 */
typedef struct H264PredContext{
    void (*pred4x4  [9+3+2])(uint8_t *src, uint8_t *topright, int stride);//FIXME move to dsp?
    void (*pred8x8l [9+3])(uint8_t *src, int topleft, int topright, int stride);
    void (*pred8x8  [4+3])(uint8_t *src, int stride);
    void (*pred16x16[4+3])(uint8_t *src, int stride);
}H264PredContext;

void ff_h264_pred_init(H264PredContext *h, int codec_id);

#endif /* H264PRED_H */
