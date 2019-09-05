/*
 * Copyright (c) 2002 The FFmpeg Project
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

#ifndef AVCODEC_WMV2_H
#define AVCODEC_WMV2_H

#include "avcodec.h"
#include "intrax8.h"
#include "mpegvideo.h"
#include "wmv2dsp.h"

#define SKIP_TYPE_NONE 0
#define SKIP_TYPE_MPEG 1
#define SKIP_TYPE_ROW  2
#define SKIP_TYPE_COL  3


typedef struct Wmv2Context {
    MpegEncContext s;
    IntraX8Context x8;
    WMV2DSPContext wdsp;
    int j_type_bit;
    int j_type;
    int abt_flag;
    int abt_type;
    int abt_type_table[6];
    int per_mb_abt;
    int per_block_abt;
    int mspel_bit;
    int cbp_table_index;
    int top_left_mv_flag;
    int per_mb_rl_bit;
    int skip_type;
    int hshift;

    ScanTable abt_scantable[2];
    DECLARE_ALIGNED(32, int16_t, abt_block2)[6][64];
} Wmv2Context;

void ff_wmv2_common_init(Wmv2Context *w);

int ff_wmv2_decode_mb(MpegEncContext *s, int16_t block[6][64]);
int ff_wmv2_encode_picture_header(MpegEncContext * s, int picture_number);
void ff_wmv2_encode_mb(MpegEncContext * s, int16_t block[6][64],
                       int motion_x, int motion_y);
int ff_wmv2_decode_picture_header(MpegEncContext * s);
int ff_wmv2_decode_secondary_picture_header(MpegEncContext * s);
void ff_wmv2_add_mb(MpegEncContext *s, int16_t block[6][64],
                    uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr);

void ff_mspel_motion(MpegEncContext *s,
                     uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                     uint8_t **ref_picture, op_pixels_func (*pix_op)[4],
                     int motion_x, int motion_y, int h);


static av_always_inline int wmv2_get_cbp_table_index(MpegEncContext *s, int cbp_index)
{
    static const uint8_t map[3][3] = {
        { 0, 2, 1 },
        { 1, 0, 2 },
        { 2, 1, 0 },
    };

    return map[(s->qscale > 10) + (s->qscale > 20)][cbp_index];
}

#endif /* AVCODEC_WMV2_H */
