/*
 * RV30/40 decoder motion compensation functions
 * Copyright (c) 2008 Konstantin Shishkov
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
 * RV30/40 decoder motion compensation functions
 */

#ifndef AVCODEC_RV34DSP_H
#define AVCODEC_RV34DSP_H

#include "dsputil.h"

typedef void (*rv40_weight_func)(uint8_t *dst/*align width (8 or 16)*/,
                                 uint8_t *src1/*align width (8 or 16)*/,
                                 uint8_t *src2/*align width (8 or 16)*/,
                                 int w1, int w2, int stride);

typedef void (*rv34_inv_transform_func)(DCTELEM *block);

typedef struct RV34DSPContext {
    qpel_mc_func put_pixels_tab[4][16];
    qpel_mc_func avg_pixels_tab[4][16];
    h264_chroma_mc_func put_chroma_pixels_tab[3];
    h264_chroma_mc_func avg_chroma_pixels_tab[3];
    rv40_weight_func rv40_weight_pixels_tab[2];
    rv34_inv_transform_func rv34_inv_transform_tab[2];
} RV34DSPContext;

void ff_rv30dsp_init(RV34DSPContext *c, DSPContext* dsp);
void ff_rv34dsp_init(RV34DSPContext *c, DSPContext* dsp);
void ff_rv40dsp_init(RV34DSPContext *c, DSPContext* dsp);

void ff_rv40dsp_init_x86(RV34DSPContext *c, DSPContext *dsp);

#endif /* AVCODEC_RV34DSP_H */
