/*
 * Discrete wavelet transform
 * Copyright (c) 2007 Kamil Nowosad
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

#ifndef AVCODEC_DWT_H
#define AVCODEC_DWT_H

/**
 * Discrete wavelet transform
 * @file
 * @author Kamil Nowosad
 */

#include "avcodec.h"

#define FF_DWT_MAX_DECLVLS 32 ///< max number of decomposition levels

enum DWTType{
    FF_DWT97,
    FF_DWT53
};

typedef struct {
    ///line lengths {horizontal, vertical} in consecutive decomposition levels
    uint16_t linelen[FF_DWT_MAX_DECLVLS][2];
    uint8_t  mod[FF_DWT_MAX_DECLVLS][2]; ///< coordinates (x0, y0) of decomp. levels mod 2
    uint8_t  ndeclevels;                 ///< number of decomposition levels
    uint8_t  type;                       ///< 0 for 9/7; 1 for 5/3
    void     *linebuf;                   ///< buffer used by transform (int or float)
} DWTContext;

/**
 * initialize DWT
 * @param s DWT context
 * @param border coordinates of transformed region {{x0, x1}, {y0, y1}}
 * @param decomp_levels number of decomposition levels
 * @param type 0 for DWT 9/7; 1 for DWT 5/3
 */
int ff_j2k_dwt_init(DWTContext *s, uint16_t border[2][2], int decomp_levels, int type);

int ff_j2k_dwt_encode(DWTContext *s, int *t);
int ff_j2k_dwt_decode(DWTContext *s, int *t);

void ff_j2k_dwt_destroy(DWTContext *s);

#endif /* AVCODEC_DWT_H */
