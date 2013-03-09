/*
 * Half-pel DSP functions.
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * gmc & q-pel & 32/64 bit based MC by Michael Niedermayer <michaelni@gmx.at>
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
 * Half-pel DSP functions.
 */

#include "bit_depth_template.c"

#include "hpel_template.c"

#define PIXOP2(OPNAME, OP) \
static inline void FUNC(OPNAME ## _no_rnd_pixels8_l2)(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, \
                                                int src_stride1, int src_stride2, int h){\
    int i;\
    for(i=0; i<h; i++){\
        pixel4 a,b;\
        a= AV_RN4P(&src1[i*src_stride1  ]);\
        b= AV_RN4P(&src2[i*src_stride2  ]);\
        OP(*((pixel4*)&dst[i*dst_stride  ]), no_rnd_avg_pixel4(a, b));\
        a= AV_RN4P(&src1[i*src_stride1+4*sizeof(pixel)]);\
        b= AV_RN4P(&src2[i*src_stride2+4*sizeof(pixel)]);\
        OP(*((pixel4*)&dst[i*dst_stride+4*sizeof(pixel)]), no_rnd_avg_pixel4(a, b));\
    }\
}\
\
static inline void FUNCC(OPNAME ## _no_rnd_pixels8_x2)(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h){\
    FUNC(OPNAME ## _no_rnd_pixels8_l2)(block, pixels, pixels+sizeof(pixel), line_size, line_size, line_size, h);\
}\
\
static inline void FUNCC(OPNAME ## _pixels8_x2)(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h){\
    FUNC(OPNAME ## _pixels8_l2)(block, pixels, pixels+sizeof(pixel), line_size, line_size, line_size, h);\
}\
\
static inline void FUNCC(OPNAME ## _no_rnd_pixels8_y2)(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h){\
    FUNC(OPNAME ## _no_rnd_pixels8_l2)(block, pixels, pixels+line_size, line_size, line_size, line_size, h);\
}\
\
static inline void FUNCC(OPNAME ## _pixels8_y2)(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h){\
    FUNC(OPNAME ## _pixels8_l2)(block, pixels, pixels+line_size, line_size, line_size, line_size, h);\
}\
\
static inline void FUNCC(OPNAME ## _pixels4_x2)(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h){\
    FUNC(OPNAME ## _pixels4_l2)(block, pixels, pixels+sizeof(pixel), line_size, line_size, line_size, h);\
}\
\
static inline void FUNCC(OPNAME ## _pixels4_y2)(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h){\
    FUNC(OPNAME ## _pixels4_l2)(block, pixels, pixels+line_size, line_size, line_size, line_size, h);\
}\
\
static inline void FUNCC(OPNAME ## _pixels2_x2)(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h){\
    FUNC(OPNAME ## _pixels2_l2)(block, pixels, pixels+sizeof(pixel), line_size, line_size, line_size, h);\
}\
\
static inline void FUNCC(OPNAME ## _pixels2_y2)(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h){\
    FUNC(OPNAME ## _pixels2_l2)(block, pixels, pixels+line_size, line_size, line_size, line_size, h);\
}\
\
static inline void FUNCC(OPNAME ## _pixels2_xy2)(uint8_t *_block, const uint8_t *_pixels, ptrdiff_t line_size, int h)\
{\
        int i, a0, b0, a1, b1;\
        pixel *block = (pixel*)_block;\
        const pixel *pixels = (const pixel*)_pixels;\
        line_size /= sizeof(pixel);\
        a0= pixels[0];\
        b0= pixels[1] + 2;\
        a0 += b0;\
        b0 += pixels[2];\
\
        pixels+=line_size;\
        for(i=0; i<h; i+=2){\
            a1= pixels[0];\
            b1= pixels[1];\
            a1 += b1;\
            b1 += pixels[2];\
\
            block[0]= (a1+a0)>>2; /* FIXME non put */\
            block[1]= (b1+b0)>>2;\
\
            pixels+=line_size;\
            block +=line_size;\
\
            a0= pixels[0];\
            b0= pixels[1] + 2;\
            a0 += b0;\
            b0 += pixels[2];\
\
            block[0]= (a1+a0)>>2;\
            block[1]= (b1+b0)>>2;\
            pixels+=line_size;\
            block +=line_size;\
        }\
}\
\
static inline void FUNCC(OPNAME ## _pixels4_xy2)(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)\
{\
        /* FIXME HIGH BIT DEPTH */\
        int i;\
        const uint32_t a= AV_RN32(pixels  );\
        const uint32_t b= AV_RN32(pixels+1);\
        uint32_t l0=  (a&0x03030303UL)\
                    + (b&0x03030303UL)\
                    + 0x02020202UL;\
        uint32_t h0= ((a&0xFCFCFCFCUL)>>2)\
                   + ((b&0xFCFCFCFCUL)>>2);\
        uint32_t l1,h1;\
\
        pixels+=line_size;\
        for(i=0; i<h; i+=2){\
            uint32_t a= AV_RN32(pixels  );\
            uint32_t b= AV_RN32(pixels+1);\
            l1=  (a&0x03030303UL)\
               + (b&0x03030303UL);\
            h1= ((a&0xFCFCFCFCUL)>>2)\
              + ((b&0xFCFCFCFCUL)>>2);\
            OP(*((uint32_t*)block), h0+h1+(((l0+l1)>>2)&0x0F0F0F0FUL));\
            pixels+=line_size;\
            block +=line_size;\
            a= AV_RN32(pixels  );\
            b= AV_RN32(pixels+1);\
            l0=  (a&0x03030303UL)\
               + (b&0x03030303UL)\
               + 0x02020202UL;\
            h0= ((a&0xFCFCFCFCUL)>>2)\
              + ((b&0xFCFCFCFCUL)>>2);\
            OP(*((uint32_t*)block), h0+h1+(((l0+l1)>>2)&0x0F0F0F0FUL));\
            pixels+=line_size;\
            block +=line_size;\
        }\
}\
\
static inline void FUNCC(OPNAME ## _pixels8_xy2)(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)\
{\
    /* FIXME HIGH BIT DEPTH */\
    int j;\
    for(j=0; j<2; j++){\
        int i;\
        const uint32_t a= AV_RN32(pixels  );\
        const uint32_t b= AV_RN32(pixels+1);\
        uint32_t l0=  (a&0x03030303UL)\
                    + (b&0x03030303UL)\
                    + 0x02020202UL;\
        uint32_t h0= ((a&0xFCFCFCFCUL)>>2)\
                   + ((b&0xFCFCFCFCUL)>>2);\
        uint32_t l1,h1;\
\
        pixels+=line_size;\
        for(i=0; i<h; i+=2){\
            uint32_t a= AV_RN32(pixels  );\
            uint32_t b= AV_RN32(pixels+1);\
            l1=  (a&0x03030303UL)\
               + (b&0x03030303UL);\
            h1= ((a&0xFCFCFCFCUL)>>2)\
              + ((b&0xFCFCFCFCUL)>>2);\
            OP(*((uint32_t*)block), h0+h1+(((l0+l1)>>2)&0x0F0F0F0FUL));\
            pixels+=line_size;\
            block +=line_size;\
            a= AV_RN32(pixels  );\
            b= AV_RN32(pixels+1);\
            l0=  (a&0x03030303UL)\
               + (b&0x03030303UL)\
               + 0x02020202UL;\
            h0= ((a&0xFCFCFCFCUL)>>2)\
              + ((b&0xFCFCFCFCUL)>>2);\
            OP(*((uint32_t*)block), h0+h1+(((l0+l1)>>2)&0x0F0F0F0FUL));\
            pixels+=line_size;\
            block +=line_size;\
        }\
        pixels+=4-line_size*(h+1);\
        block +=4-line_size*h;\
    }\
}\
\
static inline void FUNCC(OPNAME ## _no_rnd_pixels8_xy2)(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)\
{\
    /* FIXME HIGH BIT DEPTH */\
    int j;\
    for(j=0; j<2; j++){\
        int i;\
        const uint32_t a= AV_RN32(pixels  );\
        const uint32_t b= AV_RN32(pixels+1);\
        uint32_t l0=  (a&0x03030303UL)\
                    + (b&0x03030303UL)\
                    + 0x01010101UL;\
        uint32_t h0= ((a&0xFCFCFCFCUL)>>2)\
                   + ((b&0xFCFCFCFCUL)>>2);\
        uint32_t l1,h1;\
\
        pixels+=line_size;\
        for(i=0; i<h; i+=2){\
            uint32_t a= AV_RN32(pixels  );\
            uint32_t b= AV_RN32(pixels+1);\
            l1=  (a&0x03030303UL)\
               + (b&0x03030303UL);\
            h1= ((a&0xFCFCFCFCUL)>>2)\
              + ((b&0xFCFCFCFCUL)>>2);\
            OP(*((uint32_t*)block), h0+h1+(((l0+l1)>>2)&0x0F0F0F0FUL));\
            pixels+=line_size;\
            block +=line_size;\
            a= AV_RN32(pixels  );\
            b= AV_RN32(pixels+1);\
            l0=  (a&0x03030303UL)\
               + (b&0x03030303UL)\
               + 0x01010101UL;\
            h0= ((a&0xFCFCFCFCUL)>>2)\
              + ((b&0xFCFCFCFCUL)>>2);\
            OP(*((uint32_t*)block), h0+h1+(((l0+l1)>>2)&0x0F0F0F0FUL));\
            pixels+=line_size;\
            block +=line_size;\
        }\
        pixels+=4-line_size*(h+1);\
        block +=4-line_size*h;\
    }\
}\
\
CALL_2X_PIXELS(FUNCC(OPNAME ## _pixels16_x2) , FUNCC(OPNAME ## _pixels8_x2) , 8*sizeof(pixel))\
CALL_2X_PIXELS(FUNCC(OPNAME ## _pixels16_y2) , FUNCC(OPNAME ## _pixels8_y2) , 8*sizeof(pixel))\
CALL_2X_PIXELS(FUNCC(OPNAME ## _pixels16_xy2), FUNCC(OPNAME ## _pixels8_xy2), 8*sizeof(pixel))\
av_unused CALL_2X_PIXELS(FUNCC(OPNAME ## _no_rnd_pixels16)    , FUNCC(OPNAME ## _pixels8) , 8*sizeof(pixel))\
CALL_2X_PIXELS(FUNCC(OPNAME ## _no_rnd_pixels16_x2) , FUNCC(OPNAME ## _no_rnd_pixels8_x2) , 8*sizeof(pixel))\
CALL_2X_PIXELS(FUNCC(OPNAME ## _no_rnd_pixels16_y2) , FUNCC(OPNAME ## _no_rnd_pixels8_y2) , 8*sizeof(pixel))\
CALL_2X_PIXELS(FUNCC(OPNAME ## _no_rnd_pixels16_xy2), FUNCC(OPNAME ## _no_rnd_pixels8_xy2), 8*sizeof(pixel))\

#define op_avg(a, b) a = rnd_avg_pixel4(a, b)
#define op_put(a, b) a = b
#if BIT_DEPTH == 8
#define put_no_rnd_pixels8_8_c put_pixels8_8_c
PIXOP2(avg, op_avg)
PIXOP2(put, op_put)
#endif
#undef op_avg
#undef op_put
