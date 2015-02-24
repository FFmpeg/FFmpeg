/*
 * Canopus HQX decoder
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

#ifndef AVCODEC_HQX_H
#define AVCODEC_HQX_H

#include <stdint.h>
#include "libavutil/mem.h"
#include "get_bits.h"

enum HQXACMode {
    HQX_AC_Q0 = 0,
    HQX_AC_Q8,
    HQX_AC_Q16,
    HQX_AC_Q32,
    HQX_AC_Q64,
    HQX_AC_Q128,
    NUM_HQX_AC
};

typedef struct HQXLUT {
    int16_t lev;
    uint8_t run;
    int8_t  bits;
} HQXLUT;

typedef struct HQXAC {
    int lut_bits, extra_bits;
    const HQXLUT *lut;
} HQXAC;

typedef struct HQXContext {
    int format, dcb, width, height;
    int interlaced;

    DECLARE_ALIGNED(16, int16_t, block)[16][64];

    VLC cbp_vlc;
    VLC dc_vlc[3];
} HQXContext;

#define HQX_DC_VLC_BITS 9

extern const HQXAC ff_hqx_ac[NUM_HQX_AC];

int ff_hqx_init_vlcs(HQXContext *ctx);

#endif /* AVCODEC_HQX_H */
