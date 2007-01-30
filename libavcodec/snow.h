/*
 * Copyright (C) 2004 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (C) 2006 Robert Edele <yartrebo@earthlink.net>
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

#ifndef _SNOW_H
#define _SNOW_H

#include "dsputil.h"

#define MID_STATE 128

#define MAX_DECOMPOSITIONS 8
#define MAX_PLANES 4
#define QSHIFT 5
#define QROOT (1<<QSHIFT)
#define LOSSLESS_QLOG -128
#define FRAC_BITS 8
#define MAX_REF_FRAMES 8

#define LOG2_OBMC_MAX 8
#define OBMC_MAX (1<<(LOG2_OBMC_MAX))

#define DWT_97 0
#define DWT_53 1
#define DWT_X  2

/** Used to minimize the amount of memory used in order to optimize cache performance. **/
struct slice_buffer_s {
    DWTELEM * * line; ///< For use by idwt and predict_slices.
    DWTELEM * * data_stack; ///< Used for internal purposes.
    int data_stack_top;
    int line_count;
    int line_width;
    int data_count;
    DWTELEM * base_buffer; ///< Buffer that this structure is caching.
};

#define liftS lift
#define lift5 lift
#if 1
#define W_AM 3
#define W_AO 0
#define W_AS 1

#undef liftS
#define W_BM 1
#define W_BO 8
#define W_BS 4

#define W_CM 1
#define W_CO 0
#define W_CS 0

#define W_DM 3
#define W_DO 4
#define W_DS 3
#elif 0
#define W_AM 55
#define W_AO 16
#define W_AS 5

#define W_BM 3
#define W_BO 32
#define W_BS 6

#define W_CM 127
#define W_CO 64
#define W_CS 7

#define W_DM 7
#define W_DO 8
#define W_DS 4
#elif 0
#define W_AM 97
#define W_AO 32
#define W_AS 6

#define W_BM 63
#define W_BO 512
#define W_BS 10

#define W_CM 13
#define W_CO 8
#define W_CS 4

#define W_DM 15
#define W_DO 16
#define W_DS 5

#else

#define W_AM 203
#define W_AO 64
#define W_AS 7

#define W_BM 217
#define W_BO 2048
#define W_BS 12

#define W_CM 113
#define W_CO 64
#define W_CS 7

#define W_DM 227
#define W_DO 128
#define W_DS 9
#endif

extern void ff_snow_vertical_compose97i(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, DWTELEM *b3, DWTELEM *b4, DWTELEM *b5, int width);
extern void ff_snow_horizontal_compose97i(DWTELEM *b, int width);
extern void ff_snow_inner_add_yblock(const uint8_t *obmc, const int obmc_stride, uint8_t * * block, int b_w, int b_h, int src_x, int src_y, int src_stride, slice_buffer * sb, int add, uint8_t * dst8);

#ifdef CONFIG_SNOW_ENCODER
int w53_32_c(void *v, uint8_t * pix1, uint8_t * pix2, int line_size, int h);
int w97_32_c(void *v, uint8_t * pix1, uint8_t * pix2, int line_size, int h);
#else
static int w53_32_c(void *v, uint8_t * pix1, uint8_t * pix2, int line_size, int h) {assert (0);}
static int w97_32_c(void *v, uint8_t * pix1, uint8_t * pix2, int line_size, int h) {assert (0);}
#endif

/* C bits used by mmx/sse2/altivec */

static av_always_inline void snow_interleave_line_header(int * i, int width, DWTELEM * low, DWTELEM * high){
    (*i) = (width) - 2;

    if (width & 1){
        low[(*i)+1] = low[((*i)+1)>>1];
        (*i)--;
    }
}

static av_always_inline void snow_interleave_line_footer(int * i, DWTELEM * low, DWTELEM * high){
    for (; (*i)>=0; (*i)-=2){
        low[(*i)+1] = high[(*i)>>1];
        low[*i] = low[(*i)>>1];
    }
}

static av_always_inline void snow_horizontal_compose_lift_lead_out(int i, DWTELEM * dst, DWTELEM * src, DWTELEM * ref, int width, int w, int lift_high, int mul, int add, int shift){
    for(; i<w; i++){
        dst[i] = src[i] - ((mul * (ref[i] + ref[i + 1]) + add) >> shift);
    }

    if((width^lift_high)&1){
        dst[w] = src[w] - ((mul * 2 * ref[w] + add) >> shift);
    }
}

static av_always_inline void snow_horizontal_compose_liftS_lead_out(int i, DWTELEM * dst, DWTELEM * src, DWTELEM * ref, int width, int w){
        for(; i<w; i++){
            dst[i] = src[i] - (((-(ref[i] + ref[(i+1)])+W_BO) - 4 * src[i]) >> W_BS);
        }

        if(width&1){
            dst[w] = src[w] - (((-2 * ref[w] + W_BO) - 4 * src[w]) >> W_BS);
        }
}

#endif
