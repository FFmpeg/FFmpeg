/*
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

#ifndef AVCODEC_INTRAX8_H
#define AVCODEC_INTRAX8_H

#include "bitstream.h"
#include "mpegvideo.h"

typedef struct{
    VLC * j_ac_vlc[4];//they point to the static j_mb_vlc
    VLC * j_orient_vlc;
    VLC * j_dc_vlc[3];

    int use_quant_matrix;
//set by ff_intrax8_common_init
    uint8_t * prediction_table;//2*(mb_w*2)
    ScanTable scantable[3];
//set by the caller codec
    MpegEncContext * s;
    int quant;
    int dquant;
    int qsum;
//calculated per frame
    int quant_dc_chroma;
    int divide_quant_dc_luma;
    int divide_quant_dc_chroma;
//changed per block
    int edges;
    int flat_dc;
    int predicted_dc;
    int raw_orient;
    int chroma_orient;
    int orient;
    int est_run;
} IntraX8Context;

void ff_intrax8_common_init(IntraX8Context * w, MpegEncContext * const s);
void ff_intrax8_common_end(IntraX8Context * w);
int  ff_intrax8_decode_picture(IntraX8Context * w, int quant, int halfpq);

#endif /* AVCODEC_INTRAX8_H */
