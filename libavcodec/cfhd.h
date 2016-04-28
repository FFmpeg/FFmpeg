/*
 * Copyright (c) 2015 Kieran Kunhya
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

#ifndef AVCODEC_CFHD_H
#define AVCODEC_CFHD_H

#include <stdint.h>

#include "libavutil/avassert.h"

#include "avcodec.h"
#include "get_bits.h"

#define VLC_BITS 9
#define NB_VLC_TABLE_9 (71+3)
#define NB_VLC_TABLE_18 (263+1)

typedef struct CFHD_RL_VLC_ELEM {
    int16_t level;
    int8_t len;
    uint16_t run;
} CFHD_RL_VLC_ELEM;

#define DWT_LEVELS 3

typedef struct SubBand {
    int level;
    int orientation;
    int stride;
    int a_width;
    int width;
    int a_height;
    int height;
    int pshift;
    int quant;
    uint8_t *ibuf;
} SubBand;

typedef struct Plane {
    int width;
    int height;
    ptrdiff_t stride;

    int16_t *idwt_buf;
    int16_t *idwt_tmp;

    /* TODO: merge this into SubBand structure */
    int16_t *subband[10];
    int16_t *l_h[8];

    SubBand band[DWT_LEVELS][4];
} Plane;

typedef struct CFHDContext {
    AVCodecContext *avctx;

    CFHD_RL_VLC_ELEM table_9_rl_vlc[2088];
    VLC vlc_9;

    CFHD_RL_VLC_ELEM table_18_rl_vlc[4572];
    VLC vlc_18;

    GetBitContext gb;

    int chroma_x_shift;
    int chroma_y_shift;

    int coded_width;
    int coded_height;
    int coded_format;

    int a_width;
    int a_height;
    int a_format;

    int bpc;
    int channel_cnt;
    int subband_cnt;
    int channel_num;
    uint8_t lowpass_precision;
    uint16_t quantisation;
    int wavelet_depth;
    int pshift;

    int codebook;
    int subband_num;
    int level;
    int subband_num_actual;

    uint8_t prescale_shift[3];
    Plane plane[4];

} CFHDContext;

int ff_cfhd_init_vlcs(CFHDContext *s);

#endif /* AVCODEC_CFHD_H */
