/*
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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

#include <assert.h>

#include "bit_depth_template.c"

#define H264_CHROMA_MC(OPNAME, OP)\
static void FUNCC(OPNAME ## h264_chroma_mc2)(uint8_t *_dst/*align 8*/, uint8_t *_src/*align 1*/, int stride, int h, int x, int y){\
    pixel *dst = (pixel*)_dst;\
    pixel *src = (pixel*)_src;\
    const int A=(8-x)*(8-y);\
    const int B=(  x)*(8-y);\
    const int C=(8-x)*(  y);\
    const int D=(  x)*(  y);\
    int i;\
    stride /= sizeof(pixel);\
    \
    assert(x<8 && y<8 && x>=0 && y>=0);\
\
    if(D){\
        for(i=0; i<h; i++){\
            OP(dst[0], (A*src[0] + B*src[1] + C*src[stride+0] + D*src[stride+1]));\
            OP(dst[1], (A*src[1] + B*src[2] + C*src[stride+1] + D*src[stride+2]));\
            dst+= stride;\
            src+= stride;\
        }\
    } else if (B + C) {\
        const int E= B+C;\
        const int step= C ? stride : 1;\
        for(i=0; i<h; i++){\
            OP(dst[0], (A*src[0] + E*src[step+0]));\
            OP(dst[1], (A*src[1] + E*src[step+1]));\
            dst+= stride;\
            src+= stride;\
        }\
    } else {\
        for ( i = 0; i < h; i++){\
            OP(dst[0], A * src[0]);\
            OP(dst[1], A * src[1]);\
            dst += stride;\
            src += stride;\
        }\
    }\
}\
\
static void FUNCC(OPNAME ## h264_chroma_mc4)(uint8_t *_dst/*align 8*/, uint8_t *_src/*align 1*/, int stride, int h, int x, int y){\
    pixel *dst = (pixel*)_dst;\
    pixel *src = (pixel*)_src;\
    const int A=(8-x)*(8-y);\
    const int B=(  x)*(8-y);\
    const int C=(8-x)*(  y);\
    const int D=(  x)*(  y);\
    int i;\
    stride /= sizeof(pixel);\
    \
    assert(x<8 && y<8 && x>=0 && y>=0);\
\
    if(D){\
        for(i=0; i<h; i++){\
            OP(dst[0], (A*src[0] + B*src[1] + C*src[stride+0] + D*src[stride+1]));\
            OP(dst[1], (A*src[1] + B*src[2] + C*src[stride+1] + D*src[stride+2]));\
            OP(dst[2], (A*src[2] + B*src[3] + C*src[stride+2] + D*src[stride+3]));\
            OP(dst[3], (A*src[3] + B*src[4] + C*src[stride+3] + D*src[stride+4]));\
            dst+= stride;\
            src+= stride;\
        }\
    } else if (B + C) {\
        const int E= B+C;\
        const int step= C ? stride : 1;\
        for(i=0; i<h; i++){\
            OP(dst[0], (A*src[0] + E*src[step+0]));\
            OP(dst[1], (A*src[1] + E*src[step+1]));\
            OP(dst[2], (A*src[2] + E*src[step+2]));\
            OP(dst[3], (A*src[3] + E*src[step+3]));\
            dst+= stride;\
            src+= stride;\
        }\
    } else {\
        for ( i = 0; i < h; i++){\
            OP(dst[0], A * src[0]);\
            OP(dst[1], A * src[1]);\
            OP(dst[2], A * src[2]);\
            OP(dst[3], A * src[3]);\
            dst += stride;\
            src += stride;\
        }\
    }\
}\
\
static void FUNCC(OPNAME ## h264_chroma_mc8)(uint8_t *_dst/*align 8*/, uint8_t *_src/*align 1*/, int stride, int h, int x, int y){\
    pixel *dst = (pixel*)_dst;\
    pixel *src = (pixel*)_src;\
    const int A=(8-x)*(8-y);\
    const int B=(  x)*(8-y);\
    const int C=(8-x)*(  y);\
    const int D=(  x)*(  y);\
    int i;\
    stride /= sizeof(pixel);\
    \
    assert(x<8 && y<8 && x>=0 && y>=0);\
\
    if(D){\
        for(i=0; i<h; i++){\
            OP(dst[0], (A*src[0] + B*src[1] + C*src[stride+0] + D*src[stride+1]));\
            OP(dst[1], (A*src[1] + B*src[2] + C*src[stride+1] + D*src[stride+2]));\
            OP(dst[2], (A*src[2] + B*src[3] + C*src[stride+2] + D*src[stride+3]));\
            OP(dst[3], (A*src[3] + B*src[4] + C*src[stride+3] + D*src[stride+4]));\
            OP(dst[4], (A*src[4] + B*src[5] + C*src[stride+4] + D*src[stride+5]));\
            OP(dst[5], (A*src[5] + B*src[6] + C*src[stride+5] + D*src[stride+6]));\
            OP(dst[6], (A*src[6] + B*src[7] + C*src[stride+6] + D*src[stride+7]));\
            OP(dst[7], (A*src[7] + B*src[8] + C*src[stride+7] + D*src[stride+8]));\
            dst+= stride;\
            src+= stride;\
        }\
    } else if (B + C) {\
        const int E= B+C;\
        const int step= C ? stride : 1;\
        for(i=0; i<h; i++){\
            OP(dst[0], (A*src[0] + E*src[step+0]));\
            OP(dst[1], (A*src[1] + E*src[step+1]));\
            OP(dst[2], (A*src[2] + E*src[step+2]));\
            OP(dst[3], (A*src[3] + E*src[step+3]));\
            OP(dst[4], (A*src[4] + E*src[step+4]));\
            OP(dst[5], (A*src[5] + E*src[step+5]));\
            OP(dst[6], (A*src[6] + E*src[step+6]));\
            OP(dst[7], (A*src[7] + E*src[step+7]));\
            dst+= stride;\
            src+= stride;\
        }\
    } else {\
        for ( i = 0; i < h; i++){\
            OP(dst[0], A * src[0]);\
            OP(dst[1], A * src[1]);\
            OP(dst[2], A * src[2]);\
            OP(dst[3], A * src[3]);\
            OP(dst[4], A * src[4]);\
            OP(dst[5], A * src[5]);\
            OP(dst[6], A * src[6]);\
            OP(dst[7], A * src[7]);\
            dst += stride;\
            src += stride;\
        }\
    }\
}

#define op_avg(a, b) a = (((a)+(((b) + 32)>>6)+1)>>1)
#define op_put(a, b) a = (((b) + 32)>>6)

H264_CHROMA_MC(put_       , op_put)
H264_CHROMA_MC(avg_       , op_avg)
#undef op_avg
#undef op_put
