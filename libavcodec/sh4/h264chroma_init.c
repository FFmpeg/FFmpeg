/*
 * aligned/packed access motion
 *
 * Copyright (c) 2001-2003 BERO <bero@geocities.co.jp>
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

#include <assert.h>
#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavcodec/h264chroma.h"

#define H264_CHROMA_MC(OPNAME, OP)\
static void OPNAME ## h264_chroma_mc2_sh4(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y){\
    const int A=(8-x)*(8-y);\
    const int B=(  x)*(8-y);\
    const int C=(8-x)*(  y);\
    const int D=(  x)*(  y);\
    \
    assert(x<8 && y<8 && x>=0 && y>=0);\
\
    do {\
        int t0,t1,t2,t3; \
        uint8_t *s0 = src; \
        uint8_t *s1 = src+stride; \
        t0 = *s0++; t2 = *s1++; \
        t1 = *s0++; t3 = *s1++; \
        OP(dst[0], (A*t0 + B*t1 + C*t2 + D*t3));\
        t0 = *s0++; t2 = *s1++; \
        OP(dst[1], (A*t1 + B*t0 + C*t3 + D*t2));\
        dst+= stride;\
        src+= stride;\
    }while(--h);\
}\
\
static void OPNAME ## h264_chroma_mc4_sh4(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y){\
    const int A=(8-x)*(8-y);\
    const int B=(  x)*(8-y);\
    const int C=(8-x)*(  y);\
    const int D=(  x)*(  y);\
    \
    assert(x<8 && y<8 && x>=0 && y>=0);\
\
    do {\
        int t0,t1,t2,t3; \
        uint8_t *s0 = src; \
        uint8_t *s1 = src+stride; \
        t0 = *s0++; t2 = *s1++; \
        t1 = *s0++; t3 = *s1++; \
        OP(dst[0], (A*t0 + B*t1 + C*t2 + D*t3));\
        t0 = *s0++; t2 = *s1++; \
        OP(dst[1], (A*t1 + B*t0 + C*t3 + D*t2));\
        t1 = *s0++; t3 = *s1++; \
        OP(dst[2], (A*t0 + B*t1 + C*t2 + D*t3));\
        t0 = *s0++; t2 = *s1++; \
        OP(dst[3], (A*t1 + B*t0 + C*t3 + D*t2));\
        dst+= stride;\
        src+= stride;\
    }while(--h);\
}\
\
static void OPNAME ## h264_chroma_mc8_sh4(uint8_t *dst/*align 8*/, uint8_t *src/*align 1*/, int stride, int h, int x, int y){\
    const int A=(8-x)*(8-y);\
    const int B=(  x)*(8-y);\
    const int C=(8-x)*(  y);\
    const int D=(  x)*(  y);\
    \
    assert(x<8 && y<8 && x>=0 && y>=0);\
\
    do {\
        int t0,t1,t2,t3; \
        uint8_t *s0 = src; \
        uint8_t *s1 = src+stride; \
        t0 = *s0++; t2 = *s1++; \
        t1 = *s0++; t3 = *s1++; \
        OP(dst[0], (A*t0 + B*t1 + C*t2 + D*t3));\
        t0 = *s0++; t2 = *s1++; \
        OP(dst[1], (A*t1 + B*t0 + C*t3 + D*t2));\
        t1 = *s0++; t3 = *s1++; \
        OP(dst[2], (A*t0 + B*t1 + C*t2 + D*t3));\
        t0 = *s0++; t2 = *s1++; \
        OP(dst[3], (A*t1 + B*t0 + C*t3 + D*t2));\
        t1 = *s0++; t3 = *s1++; \
        OP(dst[4], (A*t0 + B*t1 + C*t2 + D*t3));\
        t0 = *s0++; t2 = *s1++; \
        OP(dst[5], (A*t1 + B*t0 + C*t3 + D*t2));\
        t1 = *s0++; t3 = *s1++; \
        OP(dst[6], (A*t0 + B*t1 + C*t2 + D*t3));\
        t0 = *s0++; t2 = *s1++; \
        OP(dst[7], (A*t1 + B*t0 + C*t3 + D*t2));\
        dst+= stride;\
        src+= stride;\
    }while(--h);\
}

#define op_avg(a, b) a = (((a)+(((b) + 32)>>6)+1)>>1)
#define op_put(a, b) a = (((b) + 32)>>6)

H264_CHROMA_MC(put_       , op_put)
H264_CHROMA_MC(avg_       , op_avg)
#undef op_avg
#undef op_put

av_cold void ff_h264chroma_init_sh4(H264ChromaContext *c, int bit_depth)
{
    const int high_bit_depth = bit_depth > 8;

    if (!high_bit_depth) {
    c->put_h264_chroma_pixels_tab[0]= put_h264_chroma_mc8_sh4;
    c->put_h264_chroma_pixels_tab[1]= put_h264_chroma_mc4_sh4;
    c->put_h264_chroma_pixels_tab[2]= put_h264_chroma_mc2_sh4;
    c->avg_h264_chroma_pixels_tab[0]= avg_h264_chroma_mc8_sh4;
    c->avg_h264_chroma_pixels_tab[1]= avg_h264_chroma_mc4_sh4;
    c->avg_h264_chroma_pixels_tab[2]= avg_h264_chroma_mc2_sh4;
    }
}
