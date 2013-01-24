/*
 * DSP utils
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * gmc & q-pel & 32/64 bit based MC by Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * DSP utils
 */

#include "bit_depth_template.c"

static inline void FUNC(copy_block2)(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride, int h)
{
    int i;
    for(i=0; i<h; i++)
    {
        AV_WN2P(dst   , AV_RN2P(src   ));
        dst+=dstStride;
        src+=srcStride;
    }
}

static inline void FUNC(copy_block4)(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride, int h)
{
    int i;
    for(i=0; i<h; i++)
    {
        AV_WN4P(dst   , AV_RN4P(src   ));
        dst+=dstStride;
        src+=srcStride;
    }
}

static inline void FUNC(copy_block8)(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride, int h)
{
    int i;
    for(i=0; i<h; i++)
    {
        AV_WN4P(dst                , AV_RN4P(src                ));
        AV_WN4P(dst+4*sizeof(pixel), AV_RN4P(src+4*sizeof(pixel)));
        dst+=dstStride;
        src+=srcStride;
    }
}

static inline void FUNC(copy_block16)(uint8_t *dst, const uint8_t *src, int dstStride, int srcStride, int h)
{
    int i;
    for(i=0; i<h; i++)
    {
        AV_WN4P(dst                 , AV_RN4P(src                 ));
        AV_WN4P(dst+ 4*sizeof(pixel), AV_RN4P(src+ 4*sizeof(pixel)));
        AV_WN4P(dst+ 8*sizeof(pixel), AV_RN4P(src+ 8*sizeof(pixel)));
        AV_WN4P(dst+12*sizeof(pixel), AV_RN4P(src+12*sizeof(pixel)));
        dst+=dstStride;
        src+=srcStride;
    }
}

/* draw the edges of width 'w' of an image of size width, height */
//FIXME check that this is ok for mpeg4 interlaced
static void FUNCC(draw_edges)(uint8_t *p_buf, int p_wrap, int width, int height, int w, int h, int sides)
{
    pixel *buf = (pixel*)p_buf;
    int wrap = p_wrap / sizeof(pixel);
    pixel *ptr, *last_line;
    int i;

    /* left and right */
    ptr = buf;
    for(i=0;i<height;i++) {
#if BIT_DEPTH > 8
        int j;
        for (j = 0; j < w; j++) {
            ptr[j-w] = ptr[0];
            ptr[j+width] = ptr[width-1];
        }
#else
        memset(ptr - w, ptr[0], w);
        memset(ptr + width, ptr[width-1], w);
#endif
        ptr += wrap;
    }

    /* top and bottom + corners */
    buf -= w;
    last_line = buf + (height - 1) * wrap;
    if (sides & EDGE_TOP)
        for(i = 0; i < h; i++)
            memcpy(buf - (i + 1) * wrap, buf, (width + w + w) * sizeof(pixel)); // top
    if (sides & EDGE_BOTTOM)
        for (i = 0; i < h; i++)
            memcpy(last_line + (i + 1) * wrap, last_line, (width + w + w) * sizeof(pixel)); // bottom
}

#define DCTELEM_FUNCS(dctcoef, suffix)                                  \
static void FUNCC(get_pixels ## suffix)(int16_t *av_restrict _block,    \
                                        const uint8_t *_pixels,         \
                                        int line_size)                  \
{                                                                       \
    const pixel *pixels = (const pixel *) _pixels;                      \
    dctcoef *av_restrict block = (dctcoef *) _block;                    \
    int i;                                                              \
                                                                        \
    /* read the pixels */                                               \
    for(i=0;i<8;i++) {                                                  \
        block[0] = pixels[0];                                           \
        block[1] = pixels[1];                                           \
        block[2] = pixels[2];                                           \
        block[3] = pixels[3];                                           \
        block[4] = pixels[4];                                           \
        block[5] = pixels[5];                                           \
        block[6] = pixels[6];                                           \
        block[7] = pixels[7];                                           \
        pixels += line_size / sizeof(pixel);                            \
        block += 8;                                                     \
    }                                                                   \
}                                                                       \
                                                                        \
static void FUNCC(add_pixels8 ## suffix)(uint8_t *av_restrict _pixels,  \
                                         int16_t *_block,               \
                                         int line_size)                 \
{                                                                       \
    int i;                                                              \
    pixel *av_restrict pixels = (pixel *av_restrict)_pixels;            \
    dctcoef *block = (dctcoef*)_block;                                  \
    line_size /= sizeof(pixel);                                         \
                                                                        \
    for(i=0;i<8;i++) {                                                  \
        pixels[0] += block[0];                                          \
        pixels[1] += block[1];                                          \
        pixels[2] += block[2];                                          \
        pixels[3] += block[3];                                          \
        pixels[4] += block[4];                                          \
        pixels[5] += block[5];                                          \
        pixels[6] += block[6];                                          \
        pixels[7] += block[7];                                          \
        pixels += line_size;                                            \
        block += 8;                                                     \
    }                                                                   \
}                                                                       \
                                                                        \
static void FUNCC(add_pixels4 ## suffix)(uint8_t *av_restrict _pixels,  \
                                         int16_t *_block,               \
                                         int line_size)                 \
{                                                                       \
    int i;                                                              \
    pixel *av_restrict pixels = (pixel *av_restrict)_pixels;            \
    dctcoef *block = (dctcoef*)_block;                                  \
    line_size /= sizeof(pixel);                                         \
                                                                        \
    for(i=0;i<4;i++) {                                                  \
        pixels[0] += block[0];                                          \
        pixels[1] += block[1];                                          \
        pixels[2] += block[2];                                          \
        pixels[3] += block[3];                                          \
        pixels += line_size;                                            \
        block += 4;                                                     \
    }                                                                   \
}                                                                       \
                                                                        \
static void FUNCC(clear_block ## suffix)(int16_t *block)                \
{                                                                       \
    memset(block, 0, sizeof(dctcoef)*64);                               \
}                                                                       \
                                                                        \
/**                                                                     \
 * memset(blocks, 0, sizeof(int16_t)*6*64)                              \
 */                                                                     \
static void FUNCC(clear_blocks ## suffix)(int16_t *blocks)              \
{                                                                       \
    memset(blocks, 0, sizeof(dctcoef)*6*64);                            \
}

DCTELEM_FUNCS(int16_t, _16)
#if BIT_DEPTH > 8
DCTELEM_FUNCS(dctcoef, _32)
#endif

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
static inline void FUNC(OPNAME ## _no_rnd_pixels16_l2)(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, \
                                                int src_stride1, int src_stride2, int h){\
    FUNC(OPNAME ## _no_rnd_pixels8_l2)(dst  , src1  , src2  , dst_stride, src_stride1, src_stride2, h);\
    FUNC(OPNAME ## _no_rnd_pixels8_l2)(dst+8*sizeof(pixel), src1+8*sizeof(pixel), src2+8*sizeof(pixel), dst_stride, src_stride1, src_stride2, h);\
}\
\
static inline void FUNCC(OPNAME ## _no_rnd_pixels8_x2)(uint8_t *block, const uint8_t *pixels, int line_size, int h){\
    FUNC(OPNAME ## _no_rnd_pixels8_l2)(block, pixels, pixels+sizeof(pixel), line_size, line_size, line_size, h);\
}\
\
static inline void FUNCC(OPNAME ## _pixels8_x2)(uint8_t *block, const uint8_t *pixels, int line_size, int h){\
    FUNC(OPNAME ## _pixels8_l2)(block, pixels, pixels+sizeof(pixel), line_size, line_size, line_size, h);\
}\
\
static inline void FUNCC(OPNAME ## _no_rnd_pixels8_y2)(uint8_t *block, const uint8_t *pixels, int line_size, int h){\
    FUNC(OPNAME ## _no_rnd_pixels8_l2)(block, pixels, pixels+line_size, line_size, line_size, line_size, h);\
}\
\
static inline void FUNCC(OPNAME ## _pixels8_y2)(uint8_t *block, const uint8_t *pixels, int line_size, int h){\
    FUNC(OPNAME ## _pixels8_l2)(block, pixels, pixels+line_size, line_size, line_size, line_size, h);\
}\
\
static inline void FUNC(OPNAME ## _pixels8_l4)(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, const uint8_t *src3, const uint8_t *src4,\
                 int dst_stride, int src_stride1, int src_stride2,int src_stride3,int src_stride4, int h){\
    /* FIXME HIGH BIT DEPTH */\
    int i;\
    for(i=0; i<h; i++){\
        uint32_t a, b, c, d, l0, l1, h0, h1;\
        a= AV_RN32(&src1[i*src_stride1]);\
        b= AV_RN32(&src2[i*src_stride2]);\
        c= AV_RN32(&src3[i*src_stride3]);\
        d= AV_RN32(&src4[i*src_stride4]);\
        l0=  (a&0x03030303UL)\
           + (b&0x03030303UL)\
           + 0x02020202UL;\
        h0= ((a&0xFCFCFCFCUL)>>2)\
          + ((b&0xFCFCFCFCUL)>>2);\
        l1=  (c&0x03030303UL)\
           + (d&0x03030303UL);\
        h1= ((c&0xFCFCFCFCUL)>>2)\
          + ((d&0xFCFCFCFCUL)>>2);\
        OP(*((uint32_t*)&dst[i*dst_stride]), h0+h1+(((l0+l1)>>2)&0x0F0F0F0FUL));\
        a= AV_RN32(&src1[i*src_stride1+4]);\
        b= AV_RN32(&src2[i*src_stride2+4]);\
        c= AV_RN32(&src3[i*src_stride3+4]);\
        d= AV_RN32(&src4[i*src_stride4+4]);\
        l0=  (a&0x03030303UL)\
           + (b&0x03030303UL)\
           + 0x02020202UL;\
        h0= ((a&0xFCFCFCFCUL)>>2)\
          + ((b&0xFCFCFCFCUL)>>2);\
        l1=  (c&0x03030303UL)\
           + (d&0x03030303UL);\
        h1= ((c&0xFCFCFCFCUL)>>2)\
          + ((d&0xFCFCFCFCUL)>>2);\
        OP(*((uint32_t*)&dst[i*dst_stride+4]), h0+h1+(((l0+l1)>>2)&0x0F0F0F0FUL));\
    }\
}\
\
static inline void FUNCC(OPNAME ## _pixels4_x2)(uint8_t *block, const uint8_t *pixels, int line_size, int h){\
    FUNC(OPNAME ## _pixels4_l2)(block, pixels, pixels+sizeof(pixel), line_size, line_size, line_size, h);\
}\
\
static inline void FUNCC(OPNAME ## _pixels4_y2)(uint8_t *block, const uint8_t *pixels, int line_size, int h){\
    FUNC(OPNAME ## _pixels4_l2)(block, pixels, pixels+line_size, line_size, line_size, line_size, h);\
}\
\
static inline void FUNCC(OPNAME ## _pixels2_x2)(uint8_t *block, const uint8_t *pixels, int line_size, int h){\
    FUNC(OPNAME ## _pixels2_l2)(block, pixels, pixels+sizeof(pixel), line_size, line_size, line_size, h);\
}\
\
static inline void FUNCC(OPNAME ## _pixels2_y2)(uint8_t *block, const uint8_t *pixels, int line_size, int h){\
    FUNC(OPNAME ## _pixels2_l2)(block, pixels, pixels+line_size, line_size, line_size, line_size, h);\
}\
\
static inline void FUNC(OPNAME ## _no_rnd_pixels8_l4)(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, const uint8_t *src3, const uint8_t *src4,\
                 int dst_stride, int src_stride1, int src_stride2,int src_stride3,int src_stride4, int h){\
    /* FIXME HIGH BIT DEPTH*/\
    int i;\
    for(i=0; i<h; i++){\
        uint32_t a, b, c, d, l0, l1, h0, h1;\
        a= AV_RN32(&src1[i*src_stride1]);\
        b= AV_RN32(&src2[i*src_stride2]);\
        c= AV_RN32(&src3[i*src_stride3]);\
        d= AV_RN32(&src4[i*src_stride4]);\
        l0=  (a&0x03030303UL)\
           + (b&0x03030303UL)\
           + 0x01010101UL;\
        h0= ((a&0xFCFCFCFCUL)>>2)\
          + ((b&0xFCFCFCFCUL)>>2);\
        l1=  (c&0x03030303UL)\
           + (d&0x03030303UL);\
        h1= ((c&0xFCFCFCFCUL)>>2)\
          + ((d&0xFCFCFCFCUL)>>2);\
        OP(*((uint32_t*)&dst[i*dst_stride]), h0+h1+(((l0+l1)>>2)&0x0F0F0F0FUL));\
        a= AV_RN32(&src1[i*src_stride1+4]);\
        b= AV_RN32(&src2[i*src_stride2+4]);\
        c= AV_RN32(&src3[i*src_stride3+4]);\
        d= AV_RN32(&src4[i*src_stride4+4]);\
        l0=  (a&0x03030303UL)\
           + (b&0x03030303UL)\
           + 0x01010101UL;\
        h0= ((a&0xFCFCFCFCUL)>>2)\
          + ((b&0xFCFCFCFCUL)>>2);\
        l1=  (c&0x03030303UL)\
           + (d&0x03030303UL);\
        h1= ((c&0xFCFCFCFCUL)>>2)\
          + ((d&0xFCFCFCFCUL)>>2);\
        OP(*((uint32_t*)&dst[i*dst_stride+4]), h0+h1+(((l0+l1)>>2)&0x0F0F0F0FUL));\
    }\
}\
static inline void FUNC(OPNAME ## _pixels16_l4)(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, const uint8_t *src3, const uint8_t *src4,\
                 int dst_stride, int src_stride1, int src_stride2,int src_stride3,int src_stride4, int h){\
    FUNC(OPNAME ## _pixels8_l4)(dst  , src1  , src2  , src3  , src4  , dst_stride, src_stride1, src_stride2, src_stride3, src_stride4, h);\
    FUNC(OPNAME ## _pixels8_l4)(dst+8*sizeof(pixel), src1+8*sizeof(pixel), src2+8*sizeof(pixel), src3+8*sizeof(pixel), src4+8*sizeof(pixel), dst_stride, src_stride1, src_stride2, src_stride3, src_stride4, h);\
}\
static inline void FUNC(OPNAME ## _no_rnd_pixels16_l4)(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, const uint8_t *src3, const uint8_t *src4,\
                 int dst_stride, int src_stride1, int src_stride2,int src_stride3,int src_stride4, int h){\
    FUNC(OPNAME ## _no_rnd_pixels8_l4)(dst  , src1  , src2  , src3  , src4  , dst_stride, src_stride1, src_stride2, src_stride3, src_stride4, h);\
    FUNC(OPNAME ## _no_rnd_pixels8_l4)(dst+8*sizeof(pixel), src1+8*sizeof(pixel), src2+8*sizeof(pixel), src3+8*sizeof(pixel), src4+8*sizeof(pixel), dst_stride, src_stride1, src_stride2, src_stride3, src_stride4, h);\
}\
\
static inline void FUNCC(OPNAME ## _pixels2_xy2)(uint8_t *p_block, const uint8_t *p_pixels, int line_size, int h)\
{\
        int i, a0, b0, a1, b1;\
        pixel *block = (pixel*)p_block;\
        const pixel *pixels = (const pixel*)p_pixels;\
        line_size >>= sizeof(pixel)-1;\
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
static inline void FUNCC(OPNAME ## _pixels4_xy2)(uint8_t *block, const uint8_t *pixels, int line_size, int h)\
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
static inline void FUNCC(OPNAME ## _pixels8_xy2)(uint8_t *block, const uint8_t *pixels, int line_size, int h)\
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
static inline void FUNCC(OPNAME ## _no_rnd_pixels8_xy2)(uint8_t *block, const uint8_t *pixels, int line_size, int h)\
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

#define H264_CHROMA_MC(OPNAME, OP)\
static void FUNCC(OPNAME ## h264_chroma_mc2)(uint8_t *p_dst/*align 8*/, uint8_t *p_src/*align 1*/, int stride, int h, int x, int y){\
    pixel *dst = (pixel*)p_dst;\
    pixel *src = (pixel*)p_src;\
    const int A=(8-x)*(8-y);\
    const int B=(  x)*(8-y);\
    const int C=(8-x)*(  y);\
    const int D=(  x)*(  y);\
    int i;\
    stride >>= sizeof(pixel)-1;\
    \
    av_assert2(x<8 && y<8 && x>=0 && y>=0);\
\
    if(D){\
        for(i=0; i<h; i++){\
            OP(dst[0], (A*src[0] + B*src[1] + C*src[stride+0] + D*src[stride+1]));\
            OP(dst[1], (A*src[1] + B*src[2] + C*src[stride+1] + D*src[stride+2]));\
            dst+= stride;\
            src+= stride;\
        }\
    }else{\
        const int E= B+C;\
        const int step= C ? stride : 1;\
        for(i=0; i<h; i++){\
            OP(dst[0], (A*src[0] + E*src[step+0]));\
            OP(dst[1], (A*src[1] + E*src[step+1]));\
            dst+= stride;\
            src+= stride;\
        }\
    }\
}\
\
static void FUNCC(OPNAME ## h264_chroma_mc4)(uint8_t *p_dst/*align 8*/, uint8_t *p_src/*align 1*/, int stride, int h, int x, int y){\
    pixel *dst = (pixel*)p_dst;\
    pixel *src = (pixel*)p_src;\
    const int A=(8-x)*(8-y);\
    const int B=(  x)*(8-y);\
    const int C=(8-x)*(  y);\
    const int D=(  x)*(  y);\
    int i;\
    stride >>= sizeof(pixel)-1;\
    \
    av_assert2(x<8 && y<8 && x>=0 && y>=0);\
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
    }else{\
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
    }\
}\
\
static void FUNCC(OPNAME ## h264_chroma_mc8)(uint8_t *p_dst/*align 8*/, uint8_t *p_src/*align 1*/, int stride, int h, int x, int y){\
    pixel *dst = (pixel*)p_dst;\
    pixel *src = (pixel*)p_src;\
    const int A=(8-x)*(8-y);\
    const int B=(  x)*(8-y);\
    const int C=(8-x)*(  y);\
    const int D=(  x)*(  y);\
    int i;\
    stride >>= sizeof(pixel)-1;\
    \
    av_assert2(x<8 && y<8 && x>=0 && y>=0);\
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
    }else{\
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
    }\
}

#define op_avg(a, b) a = (((a)+(((b) + 32)>>6)+1)>>1)
#define op_put(a, b) a = (((b) + 32)>>6)

H264_CHROMA_MC(put_       , op_put)
H264_CHROMA_MC(avg_       , op_avg)
#undef op_avg
#undef op_put

void FUNCC(ff_put_pixels8x8)(uint8_t *dst, uint8_t *src, int stride) {
    FUNCC(put_pixels8)(dst, src, stride, 8);
}
void FUNCC(ff_avg_pixels8x8)(uint8_t *dst, uint8_t *src, int stride) {
    FUNCC(avg_pixels8)(dst, src, stride, 8);
}
void FUNCC(ff_put_pixels16x16)(uint8_t *dst, uint8_t *src, int stride) {
    FUNCC(put_pixels16)(dst, src, stride, 16);
}
void FUNCC(ff_avg_pixels16x16)(uint8_t *dst, uint8_t *src, int stride) {
    FUNCC(avg_pixels16)(dst, src, stride, 16);
}

