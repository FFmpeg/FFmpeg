/*
 * DSP utils
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
static void FUNCC(draw_edges)(uint8_t *_buf, int _wrap, int width, int height, int w, int h, int sides)
{
    pixel *buf = (pixel*)_buf;
    int wrap = _wrap / sizeof(pixel);
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

/**
 * Copy a rectangular area of samples to a temporary buffer and replicate the border samples.
 * @param buf destination buffer
 * @param src source buffer
 * @param linesize number of bytes between 2 vertically adjacent samples in both the source and destination buffers
 * @param block_w width of block
 * @param block_h height of block
 * @param src_x x coordinate of the top left sample of the block in the source buffer
 * @param src_y y coordinate of the top left sample of the block in the source buffer
 * @param w width of the source buffer
 * @param h height of the source buffer
 */
void FUNC(ff_emulated_edge_mc)(uint8_t *buf, const uint8_t *src, int linesize, int block_w, int block_h,
                                    int src_x, int src_y, int w, int h){
    int x, y;
    int start_y, start_x, end_y, end_x;

    if(src_y>= h){
        src+= (h-1-src_y)*linesize;
        src_y=h-1;
    }else if(src_y<=-block_h){
        src+= (1-block_h-src_y)*linesize;
        src_y=1-block_h;
    }
    if(src_x>= w){
        src+= (w-1-src_x)*sizeof(pixel);
        src_x=w-1;
    }else if(src_x<=-block_w){
        src+= (1-block_w-src_x)*sizeof(pixel);
        src_x=1-block_w;
    }

    start_y= FFMAX(0, -src_y);
    start_x= FFMAX(0, -src_x);
    end_y= FFMIN(block_h, h-src_y);
    end_x= FFMIN(block_w, w-src_x);
    assert(start_y < end_y && block_h);
    assert(start_x < end_x && block_w);

    w    = end_x - start_x;
    src += start_y*linesize + start_x*sizeof(pixel);
    buf += start_x*sizeof(pixel);

    //top
    for(y=0; y<start_y; y++){
        memcpy(buf, src, w*sizeof(pixel));
        buf += linesize;
    }

    // copy existing part
    for(; y<end_y; y++){
        memcpy(buf, src, w*sizeof(pixel));
        src += linesize;
        buf += linesize;
    }

    //bottom
    src -= linesize;
    for(; y<block_h; y++){
        memcpy(buf, src, w*sizeof(pixel));
        buf += linesize;
    }

    buf -= block_h * linesize + start_x*sizeof(pixel);
    while (block_h--){
        pixel *bufp = (pixel*)buf;
       //left
        for(x=0; x<start_x; x++){
            bufp[x] = bufp[start_x];
        }

       //right
        for(x=end_x; x<block_w; x++){
            bufp[x] = bufp[end_x - 1];
        }
        buf += linesize;
    }
}

#define DCTELEM_FUNCS(dctcoef, suffix)                                  \
static void FUNCC(get_pixels ## suffix)(DCTELEM *restrict _block,       \
                                        const uint8_t *_pixels,         \
                                        int line_size)                  \
{                                                                       \
    const pixel *pixels = (const pixel *) _pixels;                      \
    dctcoef *restrict block = (dctcoef *) _block;                       \
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
static void FUNCC(add_pixels8 ## suffix)(uint8_t *restrict _pixels,     \
                                         DCTELEM *_block,               \
                                         int line_size)                 \
{                                                                       \
    int i;                                                              \
    pixel *restrict pixels = (pixel *restrict)_pixels;                  \
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
static void FUNCC(add_pixels4 ## suffix)(uint8_t *restrict _pixels,     \
                                         DCTELEM *_block,               \
                                         int line_size)                 \
{                                                                       \
    int i;                                                              \
    pixel *restrict pixels = (pixel *restrict)_pixels;                  \
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
static void FUNCC(clear_block ## suffix)(DCTELEM *block)                \
{                                                                       \
    memset(block, 0, sizeof(dctcoef)*64);                               \
}                                                                       \
                                                                        \
/**                                                                     \
 * memset(blocks, 0, sizeof(DCTELEM)*6*64)                              \
 */                                                                     \
static void FUNCC(clear_blocks ## suffix)(DCTELEM *blocks)              \
{                                                                       \
    memset(blocks, 0, sizeof(dctcoef)*6*64);                            \
}

DCTELEM_FUNCS(DCTELEM, _16)
#if BIT_DEPTH > 8
DCTELEM_FUNCS(dctcoef, _32)
#endif

#define PIXOP2(OPNAME, OP) \
static void FUNCC(OPNAME ## _pixels2)(uint8_t *block, const uint8_t *pixels, int line_size, int h){\
    int i;\
    for(i=0; i<h; i++){\
        OP(*((pixel2*)(block  )), AV_RN2P(pixels  ));\
        pixels+=line_size;\
        block +=line_size;\
    }\
}\
static void FUNCC(OPNAME ## _pixels4)(uint8_t *block, const uint8_t *pixels, int line_size, int h){\
    int i;\
    for(i=0; i<h; i++){\
        OP(*((pixel4*)(block  )), AV_RN4P(pixels  ));\
        pixels+=line_size;\
        block +=line_size;\
    }\
}\
static void FUNCC(OPNAME ## _pixels8)(uint8_t *block, const uint8_t *pixels, int line_size, int h){\
    int i;\
    for(i=0; i<h; i++){\
        OP(*((pixel4*)(block                )), AV_RN4P(pixels                ));\
        OP(*((pixel4*)(block+4*sizeof(pixel))), AV_RN4P(pixels+4*sizeof(pixel)));\
        pixels+=line_size;\
        block +=line_size;\
    }\
}\
static inline void FUNCC(OPNAME ## _no_rnd_pixels8)(uint8_t *block, const uint8_t *pixels, int line_size, int h){\
    FUNCC(OPNAME ## _pixels8)(block, pixels, line_size, h);\
}\
\
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
static inline void FUNC(OPNAME ## _pixels8_l2)(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, \
                                                int src_stride1, int src_stride2, int h){\
    int i;\
    for(i=0; i<h; i++){\
        pixel4 a,b;\
        a= AV_RN4P(&src1[i*src_stride1  ]);\
        b= AV_RN4P(&src2[i*src_stride2  ]);\
        OP(*((pixel4*)&dst[i*dst_stride  ]), rnd_avg_pixel4(a, b));\
        a= AV_RN4P(&src1[i*src_stride1+4*sizeof(pixel)]);\
        b= AV_RN4P(&src2[i*src_stride2+4*sizeof(pixel)]);\
        OP(*((pixel4*)&dst[i*dst_stride+4*sizeof(pixel)]), rnd_avg_pixel4(a, b));\
    }\
}\
\
static inline void FUNC(OPNAME ## _pixels4_l2)(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, \
                                                int src_stride1, int src_stride2, int h){\
    int i;\
    for(i=0; i<h; i++){\
        pixel4 a,b;\
        a= AV_RN4P(&src1[i*src_stride1  ]);\
        b= AV_RN4P(&src2[i*src_stride2  ]);\
        OP(*((pixel4*)&dst[i*dst_stride  ]), rnd_avg_pixel4(a, b));\
    }\
}\
\
static inline void FUNC(OPNAME ## _pixels2_l2)(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, \
                                                int src_stride1, int src_stride2, int h){\
    int i;\
    for(i=0; i<h; i++){\
        pixel4 a,b;\
        a= AV_RN2P(&src1[i*src_stride1  ]);\
        b= AV_RN2P(&src2[i*src_stride2  ]);\
        OP(*((pixel2*)&dst[i*dst_stride  ]), rnd_avg_pixel4(a, b));\
    }\
}\
\
static inline void FUNC(OPNAME ## _pixels16_l2)(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, \
                                                int src_stride1, int src_stride2, int h){\
    FUNC(OPNAME ## _pixels8_l2)(dst  , src1  , src2  , dst_stride, src_stride1, src_stride2, h);\
    FUNC(OPNAME ## _pixels8_l2)(dst+8*sizeof(pixel), src1+8*sizeof(pixel), src2+8*sizeof(pixel), dst_stride, src_stride1, src_stride2, h);\
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
static inline void FUNCC(OPNAME ## _pixels2_xy2)(uint8_t *_block, const uint8_t *_pixels, int line_size, int h)\
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
CALL_2X_PIXELS(FUNCC(OPNAME ## _pixels16)    , FUNCC(OPNAME ## _pixels8)    , 8*sizeof(pixel))\
CALL_2X_PIXELS(FUNCC(OPNAME ## _pixels16_x2) , FUNCC(OPNAME ## _pixels8_x2) , 8*sizeof(pixel))\
CALL_2X_PIXELS(FUNCC(OPNAME ## _pixels16_y2) , FUNCC(OPNAME ## _pixels8_y2) , 8*sizeof(pixel))\
CALL_2X_PIXELS(FUNCC(OPNAME ## _pixels16_xy2), FUNCC(OPNAME ## _pixels8_xy2), 8*sizeof(pixel))\
av_unused CALL_2X_PIXELS(FUNCC(OPNAME ## _no_rnd_pixels16)    , FUNCC(OPNAME ## _pixels8) , 8*sizeof(pixel))\
CALL_2X_PIXELS(FUNCC(OPNAME ## _no_rnd_pixels16_x2) , FUNCC(OPNAME ## _no_rnd_pixels8_x2) , 8*sizeof(pixel))\
CALL_2X_PIXELS(FUNCC(OPNAME ## _no_rnd_pixels16_y2) , FUNCC(OPNAME ## _no_rnd_pixels8_y2) , 8*sizeof(pixel))\
CALL_2X_PIXELS(FUNCC(OPNAME ## _no_rnd_pixels16_xy2), FUNCC(OPNAME ## _no_rnd_pixels8_xy2), 8*sizeof(pixel))\

#define op_avg(a, b) a = rnd_avg_pixel4(a, b)
#define op_put(a, b) a = b

PIXOP2(avg, op_avg)
PIXOP2(put, op_put)
#undef op_avg
#undef op_put

#define put_no_rnd_pixels8_c  put_pixels8_c
#define put_no_rnd_pixels16_c put_pixels16_c

static void FUNCC(put_no_rnd_pixels16_l2)(uint8_t *dst, const uint8_t *a, const uint8_t *b, int stride, int h){
    FUNC(put_no_rnd_pixels16_l2)(dst, a, b, stride, stride, stride, h);
}

static void FUNCC(put_no_rnd_pixels8_l2)(uint8_t *dst, const uint8_t *a, const uint8_t *b, int stride, int h){
    FUNC(put_no_rnd_pixels8_l2)(dst, a, b, stride, stride, stride, h);
}

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

#define H264_LOWPASS(OPNAME, OP, OP2) \
static av_unused void FUNC(OPNAME ## h264_qpel2_h_lowpass)(uint8_t *_dst, uint8_t *_src, int dstStride, int srcStride){\
    const int h=2;\
    INIT_CLIP\
    int i;\
    pixel *dst = (pixel*)_dst;\
    pixel *src = (pixel*)_src;\
    dstStride /= sizeof(pixel);\
    srcStride /= sizeof(pixel);\
    for(i=0; i<h; i++)\
    {\
        OP(dst[0], (src[0]+src[1])*20 - (src[-1]+src[2])*5 + (src[-2]+src[3]));\
        OP(dst[1], (src[1]+src[2])*20 - (src[0 ]+src[3])*5 + (src[-1]+src[4]));\
        dst+=dstStride;\
        src+=srcStride;\
    }\
}\
\
static av_unused void FUNC(OPNAME ## h264_qpel2_v_lowpass)(uint8_t *_dst, uint8_t *_src, int dstStride, int srcStride){\
    const int w=2;\
    INIT_CLIP\
    int i;\
    pixel *dst = (pixel*)_dst;\
    pixel *src = (pixel*)_src;\
    dstStride /= sizeof(pixel);\
    srcStride /= sizeof(pixel);\
    for(i=0; i<w; i++)\
    {\
        const int srcB= src[-2*srcStride];\
        const int srcA= src[-1*srcStride];\
        const int src0= src[0 *srcStride];\
        const int src1= src[1 *srcStride];\
        const int src2= src[2 *srcStride];\
        const int src3= src[3 *srcStride];\
        const int src4= src[4 *srcStride];\
        OP(dst[0*dstStride], (src0+src1)*20 - (srcA+src2)*5 + (srcB+src3));\
        OP(dst[1*dstStride], (src1+src2)*20 - (src0+src3)*5 + (srcA+src4));\
        dst++;\
        src++;\
    }\
}\
\
static av_unused void FUNC(OPNAME ## h264_qpel2_hv_lowpass)(uint8_t *_dst, int16_t *tmp, uint8_t *_src, int dstStride, int tmpStride, int srcStride){\
    const int h=2;\
    const int w=2;\
    const int pad = (BIT_DEPTH > 9) ? (-10 * ((1<<BIT_DEPTH)-1)) : 0;\
    INIT_CLIP\
    int i;\
    pixel *dst = (pixel*)_dst;\
    pixel *src = (pixel*)_src;\
    dstStride /= sizeof(pixel);\
    srcStride /= sizeof(pixel);\
    src -= 2*srcStride;\
    for(i=0; i<h+5; i++)\
    {\
        tmp[0]= (src[0]+src[1])*20 - (src[-1]+src[2])*5 + (src[-2]+src[3]) + pad;\
        tmp[1]= (src[1]+src[2])*20 - (src[0 ]+src[3])*5 + (src[-1]+src[4]) + pad;\
        tmp+=tmpStride;\
        src+=srcStride;\
    }\
    tmp -= tmpStride*(h+5-2);\
    for(i=0; i<w; i++)\
    {\
        const int tmpB= tmp[-2*tmpStride] - pad;\
        const int tmpA= tmp[-1*tmpStride] - pad;\
        const int tmp0= tmp[0 *tmpStride] - pad;\
        const int tmp1= tmp[1 *tmpStride] - pad;\
        const int tmp2= tmp[2 *tmpStride] - pad;\
        const int tmp3= tmp[3 *tmpStride] - pad;\
        const int tmp4= tmp[4 *tmpStride] - pad;\
        OP2(dst[0*dstStride], (tmp0+tmp1)*20 - (tmpA+tmp2)*5 + (tmpB+tmp3));\
        OP2(dst[1*dstStride], (tmp1+tmp2)*20 - (tmp0+tmp3)*5 + (tmpA+tmp4));\
        dst++;\
        tmp++;\
    }\
}\
static void FUNC(OPNAME ## h264_qpel4_h_lowpass)(uint8_t *_dst, uint8_t *_src, int dstStride, int srcStride){\
    const int h=4;\
    INIT_CLIP\
    int i;\
    pixel *dst = (pixel*)_dst;\
    pixel *src = (pixel*)_src;\
    dstStride /= sizeof(pixel);\
    srcStride /= sizeof(pixel);\
    for(i=0; i<h; i++)\
    {\
        OP(dst[0], (src[0]+src[1])*20 - (src[-1]+src[2])*5 + (src[-2]+src[3]));\
        OP(dst[1], (src[1]+src[2])*20 - (src[0 ]+src[3])*5 + (src[-1]+src[4]));\
        OP(dst[2], (src[2]+src[3])*20 - (src[1 ]+src[4])*5 + (src[0 ]+src[5]));\
        OP(dst[3], (src[3]+src[4])*20 - (src[2 ]+src[5])*5 + (src[1 ]+src[6]));\
        dst+=dstStride;\
        src+=srcStride;\
    }\
}\
\
static void FUNC(OPNAME ## h264_qpel4_v_lowpass)(uint8_t *_dst, uint8_t *_src, int dstStride, int srcStride){\
    const int w=4;\
    INIT_CLIP\
    int i;\
    pixel *dst = (pixel*)_dst;\
    pixel *src = (pixel*)_src;\
    dstStride /= sizeof(pixel);\
    srcStride /= sizeof(pixel);\
    for(i=0; i<w; i++)\
    {\
        const int srcB= src[-2*srcStride];\
        const int srcA= src[-1*srcStride];\
        const int src0= src[0 *srcStride];\
        const int src1= src[1 *srcStride];\
        const int src2= src[2 *srcStride];\
        const int src3= src[3 *srcStride];\
        const int src4= src[4 *srcStride];\
        const int src5= src[5 *srcStride];\
        const int src6= src[6 *srcStride];\
        OP(dst[0*dstStride], (src0+src1)*20 - (srcA+src2)*5 + (srcB+src3));\
        OP(dst[1*dstStride], (src1+src2)*20 - (src0+src3)*5 + (srcA+src4));\
        OP(dst[2*dstStride], (src2+src3)*20 - (src1+src4)*5 + (src0+src5));\
        OP(dst[3*dstStride], (src3+src4)*20 - (src2+src5)*5 + (src1+src6));\
        dst++;\
        src++;\
    }\
}\
\
static void FUNC(OPNAME ## h264_qpel4_hv_lowpass)(uint8_t *_dst, int16_t *tmp, uint8_t *_src, int dstStride, int tmpStride, int srcStride){\
    const int h=4;\
    const int w=4;\
    const int pad = (BIT_DEPTH > 9) ? (-10 * ((1<<BIT_DEPTH)-1)) : 0;\
    INIT_CLIP\
    int i;\
    pixel *dst = (pixel*)_dst;\
    pixel *src = (pixel*)_src;\
    dstStride /= sizeof(pixel);\
    srcStride /= sizeof(pixel);\
    src -= 2*srcStride;\
    for(i=0; i<h+5; i++)\
    {\
        tmp[0]= (src[0]+src[1])*20 - (src[-1]+src[2])*5 + (src[-2]+src[3]) + pad;\
        tmp[1]= (src[1]+src[2])*20 - (src[0 ]+src[3])*5 + (src[-1]+src[4]) + pad;\
        tmp[2]= (src[2]+src[3])*20 - (src[1 ]+src[4])*5 + (src[0 ]+src[5]) + pad;\
        tmp[3]= (src[3]+src[4])*20 - (src[2 ]+src[5])*5 + (src[1 ]+src[6]) + pad;\
        tmp+=tmpStride;\
        src+=srcStride;\
    }\
    tmp -= tmpStride*(h+5-2);\
    for(i=0; i<w; i++)\
    {\
        const int tmpB= tmp[-2*tmpStride] - pad;\
        const int tmpA= tmp[-1*tmpStride] - pad;\
        const int tmp0= tmp[0 *tmpStride] - pad;\
        const int tmp1= tmp[1 *tmpStride] - pad;\
        const int tmp2= tmp[2 *tmpStride] - pad;\
        const int tmp3= tmp[3 *tmpStride] - pad;\
        const int tmp4= tmp[4 *tmpStride] - pad;\
        const int tmp5= tmp[5 *tmpStride] - pad;\
        const int tmp6= tmp[6 *tmpStride] - pad;\
        OP2(dst[0*dstStride], (tmp0+tmp1)*20 - (tmpA+tmp2)*5 + (tmpB+tmp3));\
        OP2(dst[1*dstStride], (tmp1+tmp2)*20 - (tmp0+tmp3)*5 + (tmpA+tmp4));\
        OP2(dst[2*dstStride], (tmp2+tmp3)*20 - (tmp1+tmp4)*5 + (tmp0+tmp5));\
        OP2(dst[3*dstStride], (tmp3+tmp4)*20 - (tmp2+tmp5)*5 + (tmp1+tmp6));\
        dst++;\
        tmp++;\
    }\
}\
\
static void FUNC(OPNAME ## h264_qpel8_h_lowpass)(uint8_t *_dst, uint8_t *_src, int dstStride, int srcStride){\
    const int h=8;\
    INIT_CLIP\
    int i;\
    pixel *dst = (pixel*)_dst;\
    pixel *src = (pixel*)_src;\
    dstStride /= sizeof(pixel);\
    srcStride /= sizeof(pixel);\
    for(i=0; i<h; i++)\
    {\
        OP(dst[0], (src[0]+src[1])*20 - (src[-1]+src[2])*5 + (src[-2]+src[3 ]));\
        OP(dst[1], (src[1]+src[2])*20 - (src[0 ]+src[3])*5 + (src[-1]+src[4 ]));\
        OP(dst[2], (src[2]+src[3])*20 - (src[1 ]+src[4])*5 + (src[0 ]+src[5 ]));\
        OP(dst[3], (src[3]+src[4])*20 - (src[2 ]+src[5])*5 + (src[1 ]+src[6 ]));\
        OP(dst[4], (src[4]+src[5])*20 - (src[3 ]+src[6])*5 + (src[2 ]+src[7 ]));\
        OP(dst[5], (src[5]+src[6])*20 - (src[4 ]+src[7])*5 + (src[3 ]+src[8 ]));\
        OP(dst[6], (src[6]+src[7])*20 - (src[5 ]+src[8])*5 + (src[4 ]+src[9 ]));\
        OP(dst[7], (src[7]+src[8])*20 - (src[6 ]+src[9])*5 + (src[5 ]+src[10]));\
        dst+=dstStride;\
        src+=srcStride;\
    }\
}\
\
static void FUNC(OPNAME ## h264_qpel8_v_lowpass)(uint8_t *_dst, uint8_t *_src, int dstStride, int srcStride){\
    const int w=8;\
    INIT_CLIP\
    int i;\
    pixel *dst = (pixel*)_dst;\
    pixel *src = (pixel*)_src;\
    dstStride /= sizeof(pixel);\
    srcStride /= sizeof(pixel);\
    for(i=0; i<w; i++)\
    {\
        const int srcB= src[-2*srcStride];\
        const int srcA= src[-1*srcStride];\
        const int src0= src[0 *srcStride];\
        const int src1= src[1 *srcStride];\
        const int src2= src[2 *srcStride];\
        const int src3= src[3 *srcStride];\
        const int src4= src[4 *srcStride];\
        const int src5= src[5 *srcStride];\
        const int src6= src[6 *srcStride];\
        const int src7= src[7 *srcStride];\
        const int src8= src[8 *srcStride];\
        const int src9= src[9 *srcStride];\
        const int src10=src[10*srcStride];\
        OP(dst[0*dstStride], (src0+src1)*20 - (srcA+src2)*5 + (srcB+src3));\
        OP(dst[1*dstStride], (src1+src2)*20 - (src0+src3)*5 + (srcA+src4));\
        OP(dst[2*dstStride], (src2+src3)*20 - (src1+src4)*5 + (src0+src5));\
        OP(dst[3*dstStride], (src3+src4)*20 - (src2+src5)*5 + (src1+src6));\
        OP(dst[4*dstStride], (src4+src5)*20 - (src3+src6)*5 + (src2+src7));\
        OP(dst[5*dstStride], (src5+src6)*20 - (src4+src7)*5 + (src3+src8));\
        OP(dst[6*dstStride], (src6+src7)*20 - (src5+src8)*5 + (src4+src9));\
        OP(dst[7*dstStride], (src7+src8)*20 - (src6+src9)*5 + (src5+src10));\
        dst++;\
        src++;\
    }\
}\
\
static void FUNC(OPNAME ## h264_qpel8_hv_lowpass)(uint8_t *_dst, int16_t *tmp, uint8_t *_src, int dstStride, int tmpStride, int srcStride){\
    const int h=8;\
    const int w=8;\
    const int pad = (BIT_DEPTH > 9) ? (-10 * ((1<<BIT_DEPTH)-1)) : 0;\
    INIT_CLIP\
    int i;\
    pixel *dst = (pixel*)_dst;\
    pixel *src = (pixel*)_src;\
    dstStride /= sizeof(pixel);\
    srcStride /= sizeof(pixel);\
    src -= 2*srcStride;\
    for(i=0; i<h+5; i++)\
    {\
        tmp[0]= (src[0]+src[1])*20 - (src[-1]+src[2])*5 + (src[-2]+src[3 ]) + pad;\
        tmp[1]= (src[1]+src[2])*20 - (src[0 ]+src[3])*5 + (src[-1]+src[4 ]) + pad;\
        tmp[2]= (src[2]+src[3])*20 - (src[1 ]+src[4])*5 + (src[0 ]+src[5 ]) + pad;\
        tmp[3]= (src[3]+src[4])*20 - (src[2 ]+src[5])*5 + (src[1 ]+src[6 ]) + pad;\
        tmp[4]= (src[4]+src[5])*20 - (src[3 ]+src[6])*5 + (src[2 ]+src[7 ]) + pad;\
        tmp[5]= (src[5]+src[6])*20 - (src[4 ]+src[7])*5 + (src[3 ]+src[8 ]) + pad;\
        tmp[6]= (src[6]+src[7])*20 - (src[5 ]+src[8])*5 + (src[4 ]+src[9 ]) + pad;\
        tmp[7]= (src[7]+src[8])*20 - (src[6 ]+src[9])*5 + (src[5 ]+src[10]) + pad;\
        tmp+=tmpStride;\
        src+=srcStride;\
    }\
    tmp -= tmpStride*(h+5-2);\
    for(i=0; i<w; i++)\
    {\
        const int tmpB= tmp[-2*tmpStride] - pad;\
        const int tmpA= tmp[-1*tmpStride] - pad;\
        const int tmp0= tmp[0 *tmpStride] - pad;\
        const int tmp1= tmp[1 *tmpStride] - pad;\
        const int tmp2= tmp[2 *tmpStride] - pad;\
        const int tmp3= tmp[3 *tmpStride] - pad;\
        const int tmp4= tmp[4 *tmpStride] - pad;\
        const int tmp5= tmp[5 *tmpStride] - pad;\
        const int tmp6= tmp[6 *tmpStride] - pad;\
        const int tmp7= tmp[7 *tmpStride] - pad;\
        const int tmp8= tmp[8 *tmpStride] - pad;\
        const int tmp9= tmp[9 *tmpStride] - pad;\
        const int tmp10=tmp[10*tmpStride] - pad;\
        OP2(dst[0*dstStride], (tmp0+tmp1)*20 - (tmpA+tmp2)*5 + (tmpB+tmp3));\
        OP2(dst[1*dstStride], (tmp1+tmp2)*20 - (tmp0+tmp3)*5 + (tmpA+tmp4));\
        OP2(dst[2*dstStride], (tmp2+tmp3)*20 - (tmp1+tmp4)*5 + (tmp0+tmp5));\
        OP2(dst[3*dstStride], (tmp3+tmp4)*20 - (tmp2+tmp5)*5 + (tmp1+tmp6));\
        OP2(dst[4*dstStride], (tmp4+tmp5)*20 - (tmp3+tmp6)*5 + (tmp2+tmp7));\
        OP2(dst[5*dstStride], (tmp5+tmp6)*20 - (tmp4+tmp7)*5 + (tmp3+tmp8));\
        OP2(dst[6*dstStride], (tmp6+tmp7)*20 - (tmp5+tmp8)*5 + (tmp4+tmp9));\
        OP2(dst[7*dstStride], (tmp7+tmp8)*20 - (tmp6+tmp9)*5 + (tmp5+tmp10));\
        dst++;\
        tmp++;\
    }\
}\
\
static void FUNC(OPNAME ## h264_qpel16_v_lowpass)(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    FUNC(OPNAME ## h264_qpel8_v_lowpass)(dst                , src                , dstStride, srcStride);\
    FUNC(OPNAME ## h264_qpel8_v_lowpass)(dst+8*sizeof(pixel), src+8*sizeof(pixel), dstStride, srcStride);\
    src += 8*srcStride;\
    dst += 8*dstStride;\
    FUNC(OPNAME ## h264_qpel8_v_lowpass)(dst                , src                , dstStride, srcStride);\
    FUNC(OPNAME ## h264_qpel8_v_lowpass)(dst+8*sizeof(pixel), src+8*sizeof(pixel), dstStride, srcStride);\
}\
\
static void FUNC(OPNAME ## h264_qpel16_h_lowpass)(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    FUNC(OPNAME ## h264_qpel8_h_lowpass)(dst                , src                , dstStride, srcStride);\
    FUNC(OPNAME ## h264_qpel8_h_lowpass)(dst+8*sizeof(pixel), src+8*sizeof(pixel), dstStride, srcStride);\
    src += 8*srcStride;\
    dst += 8*dstStride;\
    FUNC(OPNAME ## h264_qpel8_h_lowpass)(dst                , src                , dstStride, srcStride);\
    FUNC(OPNAME ## h264_qpel8_h_lowpass)(dst+8*sizeof(pixel), src+8*sizeof(pixel), dstStride, srcStride);\
}\
\
static void FUNC(OPNAME ## h264_qpel16_hv_lowpass)(uint8_t *dst, int16_t *tmp, uint8_t *src, int dstStride, int tmpStride, int srcStride){\
    FUNC(OPNAME ## h264_qpel8_hv_lowpass)(dst                , tmp  , src                , dstStride, tmpStride, srcStride);\
    FUNC(OPNAME ## h264_qpel8_hv_lowpass)(dst+8*sizeof(pixel), tmp+8, src+8*sizeof(pixel), dstStride, tmpStride, srcStride);\
    src += 8*srcStride;\
    dst += 8*dstStride;\
    FUNC(OPNAME ## h264_qpel8_hv_lowpass)(dst                , tmp  , src                , dstStride, tmpStride, srcStride);\
    FUNC(OPNAME ## h264_qpel8_hv_lowpass)(dst+8*sizeof(pixel), tmp+8, src+8*sizeof(pixel), dstStride, tmpStride, srcStride);\
}\

#define H264_MC(OPNAME, SIZE) \
static av_unused void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc00)(uint8_t *dst, uint8_t *src, int stride){\
    FUNCC(OPNAME ## pixels ## SIZE)(dst, src, stride, SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc10)(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t half[SIZE*SIZE*sizeof(pixel)];\
    FUNC(put_h264_qpel ## SIZE ## _h_lowpass)(half, src, SIZE*sizeof(pixel), stride);\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, src, half, stride, stride, SIZE*sizeof(pixel), SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc20)(uint8_t *dst, uint8_t *src, int stride){\
    FUNC(OPNAME ## h264_qpel ## SIZE ## _h_lowpass)(dst, src, stride, stride);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc30)(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t half[SIZE*SIZE*sizeof(pixel)];\
    FUNC(put_h264_qpel ## SIZE ## _h_lowpass)(half, src, SIZE*sizeof(pixel), stride);\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, src+sizeof(pixel), half, stride, stride, SIZE*sizeof(pixel), SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc01)(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t * const full_mid= full + SIZE*2*sizeof(pixel);\
    uint8_t half[SIZE*SIZE*sizeof(pixel)];\
    FUNC(copy_block ## SIZE )(full, src - stride*2, SIZE*sizeof(pixel),  stride, SIZE + 5);\
    FUNC(put_h264_qpel ## SIZE ## _v_lowpass)(half, full_mid, SIZE*sizeof(pixel), SIZE*sizeof(pixel));\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, full_mid, half, stride, SIZE*sizeof(pixel), SIZE*sizeof(pixel), SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc02)(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t * const full_mid= full + SIZE*2*sizeof(pixel);\
    FUNC(copy_block ## SIZE )(full, src - stride*2, SIZE*sizeof(pixel),  stride, SIZE + 5);\
    FUNC(OPNAME ## h264_qpel ## SIZE ## _v_lowpass)(dst, full_mid, stride, SIZE*sizeof(pixel));\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc03)(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t * const full_mid= full + SIZE*2*sizeof(pixel);\
    uint8_t half[SIZE*SIZE*sizeof(pixel)];\
    FUNC(copy_block ## SIZE )(full, src - stride*2, SIZE*sizeof(pixel),  stride, SIZE + 5);\
    FUNC(put_h264_qpel ## SIZE ## _v_lowpass)(half, full_mid, SIZE*sizeof(pixel), SIZE*sizeof(pixel));\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, full_mid+SIZE*sizeof(pixel), half, stride, SIZE*sizeof(pixel), SIZE*sizeof(pixel), SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc11)(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t * const full_mid= full + SIZE*2*sizeof(pixel);\
    uint8_t halfH[SIZE*SIZE*sizeof(pixel)];\
    uint8_t halfV[SIZE*SIZE*sizeof(pixel)];\
    FUNC(put_h264_qpel ## SIZE ## _h_lowpass)(halfH, src, SIZE*sizeof(pixel), stride);\
    FUNC(copy_block ## SIZE )(full, src - stride*2, SIZE*sizeof(pixel),  stride, SIZE + 5);\
    FUNC(put_h264_qpel ## SIZE ## _v_lowpass)(halfV, full_mid, SIZE*sizeof(pixel), SIZE*sizeof(pixel));\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, halfH, halfV, stride, SIZE*sizeof(pixel), SIZE*sizeof(pixel), SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc31)(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t * const full_mid= full + SIZE*2*sizeof(pixel);\
    uint8_t halfH[SIZE*SIZE*sizeof(pixel)];\
    uint8_t halfV[SIZE*SIZE*sizeof(pixel)];\
    FUNC(put_h264_qpel ## SIZE ## _h_lowpass)(halfH, src, SIZE*sizeof(pixel), stride);\
    FUNC(copy_block ## SIZE )(full, src - stride*2 + sizeof(pixel), SIZE*sizeof(pixel),  stride, SIZE + 5);\
    FUNC(put_h264_qpel ## SIZE ## _v_lowpass)(halfV, full_mid, SIZE*sizeof(pixel), SIZE*sizeof(pixel));\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, halfH, halfV, stride, SIZE*sizeof(pixel), SIZE*sizeof(pixel), SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc13)(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t * const full_mid= full + SIZE*2*sizeof(pixel);\
    uint8_t halfH[SIZE*SIZE*sizeof(pixel)];\
    uint8_t halfV[SIZE*SIZE*sizeof(pixel)];\
    FUNC(put_h264_qpel ## SIZE ## _h_lowpass)(halfH, src + stride, SIZE*sizeof(pixel), stride);\
    FUNC(copy_block ## SIZE )(full, src - stride*2, SIZE*sizeof(pixel),  stride, SIZE + 5);\
    FUNC(put_h264_qpel ## SIZE ## _v_lowpass)(halfV, full_mid, SIZE*sizeof(pixel), SIZE*sizeof(pixel));\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, halfH, halfV, stride, SIZE*sizeof(pixel), SIZE*sizeof(pixel), SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc33)(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t * const full_mid= full + SIZE*2*sizeof(pixel);\
    uint8_t halfH[SIZE*SIZE*sizeof(pixel)];\
    uint8_t halfV[SIZE*SIZE*sizeof(pixel)];\
    FUNC(put_h264_qpel ## SIZE ## _h_lowpass)(halfH, src + stride, SIZE*sizeof(pixel), stride);\
    FUNC(copy_block ## SIZE )(full, src - stride*2 + sizeof(pixel), SIZE*sizeof(pixel),  stride, SIZE + 5);\
    FUNC(put_h264_qpel ## SIZE ## _v_lowpass)(halfV, full_mid, SIZE*sizeof(pixel), SIZE*sizeof(pixel));\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, halfH, halfV, stride, SIZE*sizeof(pixel), SIZE*sizeof(pixel), SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc22)(uint8_t *dst, uint8_t *src, int stride){\
    int16_t tmp[SIZE*(SIZE+5)*sizeof(pixel)];\
    FUNC(OPNAME ## h264_qpel ## SIZE ## _hv_lowpass)(dst, tmp, src, stride, SIZE*sizeof(pixel), stride);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc21)(uint8_t *dst, uint8_t *src, int stride){\
    int16_t tmp[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t halfH[SIZE*SIZE*sizeof(pixel)];\
    uint8_t halfHV[SIZE*SIZE*sizeof(pixel)];\
    FUNC(put_h264_qpel ## SIZE ## _h_lowpass)(halfH, src, SIZE*sizeof(pixel), stride);\
    FUNC(put_h264_qpel ## SIZE ## _hv_lowpass)(halfHV, tmp, src, SIZE*sizeof(pixel), SIZE*sizeof(pixel), stride);\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, halfH, halfHV, stride, SIZE*sizeof(pixel), SIZE*sizeof(pixel), SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc23)(uint8_t *dst, uint8_t *src, int stride){\
    int16_t tmp[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t halfH[SIZE*SIZE*sizeof(pixel)];\
    uint8_t halfHV[SIZE*SIZE*sizeof(pixel)];\
    FUNC(put_h264_qpel ## SIZE ## _h_lowpass)(halfH, src + stride, SIZE*sizeof(pixel), stride);\
    FUNC(put_h264_qpel ## SIZE ## _hv_lowpass)(halfHV, tmp, src, SIZE*sizeof(pixel), SIZE*sizeof(pixel), stride);\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, halfH, halfHV, stride, SIZE*sizeof(pixel), SIZE*sizeof(pixel), SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc12)(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t * const full_mid= full + SIZE*2*sizeof(pixel);\
    int16_t tmp[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t halfV[SIZE*SIZE*sizeof(pixel)];\
    uint8_t halfHV[SIZE*SIZE*sizeof(pixel)];\
    FUNC(copy_block ## SIZE )(full, src - stride*2, SIZE*sizeof(pixel),  stride, SIZE + 5);\
    FUNC(put_h264_qpel ## SIZE ## _v_lowpass)(halfV, full_mid, SIZE*sizeof(pixel), SIZE*sizeof(pixel));\
    FUNC(put_h264_qpel ## SIZE ## _hv_lowpass)(halfHV, tmp, src, SIZE*sizeof(pixel), SIZE*sizeof(pixel), stride);\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, halfV, halfHV, stride, SIZE*sizeof(pixel), SIZE*sizeof(pixel), SIZE);\
}\
\
static void FUNCC(OPNAME ## h264_qpel ## SIZE ## _mc32)(uint8_t *dst, uint8_t *src, int stride){\
    uint8_t full[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t * const full_mid= full + SIZE*2*sizeof(pixel);\
    int16_t tmp[SIZE*(SIZE+5)*sizeof(pixel)];\
    uint8_t halfV[SIZE*SIZE*sizeof(pixel)];\
    uint8_t halfHV[SIZE*SIZE*sizeof(pixel)];\
    FUNC(copy_block ## SIZE )(full, src - stride*2 + sizeof(pixel), SIZE*sizeof(pixel),  stride, SIZE + 5);\
    FUNC(put_h264_qpel ## SIZE ## _v_lowpass)(halfV, full_mid, SIZE*sizeof(pixel), SIZE*sizeof(pixel));\
    FUNC(put_h264_qpel ## SIZE ## _hv_lowpass)(halfHV, tmp, src, SIZE*sizeof(pixel), SIZE*sizeof(pixel), stride);\
    FUNC(OPNAME ## pixels ## SIZE ## _l2)(dst, halfV, halfHV, stride, SIZE*sizeof(pixel), SIZE*sizeof(pixel), SIZE);\
}\

#define op_avg(a, b)  a = (((a)+CLIP(((b) + 16)>>5)+1)>>1)
//#define op_avg2(a, b) a = (((a)*w1+cm[((b) + 16)>>5]*w2 + o + 64)>>7)
#define op_put(a, b)  a = CLIP(((b) + 16)>>5)
#define op2_avg(a, b)  a = (((a)+CLIP(((b) + 512)>>10)+1)>>1)
#define op2_put(a, b)  a = CLIP(((b) + 512)>>10)

H264_LOWPASS(put_       , op_put, op2_put)
H264_LOWPASS(avg_       , op_avg, op2_avg)
H264_MC(put_, 2)
H264_MC(put_, 4)
H264_MC(put_, 8)
H264_MC(put_, 16)
H264_MC(avg_, 4)
H264_MC(avg_, 8)
H264_MC(avg_, 16)

#undef op_avg
#undef op_put
#undef op2_avg
#undef op2_put

#if BIT_DEPTH == 8
#   define put_h264_qpel8_mc00_8_c  ff_put_pixels8x8_8_c
#   define avg_h264_qpel8_mc00_8_c  ff_avg_pixels8x8_8_c
#   define put_h264_qpel16_mc00_8_c ff_put_pixels16x16_8_c
#   define avg_h264_qpel16_mc00_8_c ff_avg_pixels16x16_8_c
#elif BIT_DEPTH == 9
#   define put_h264_qpel8_mc00_9_c  ff_put_pixels8x8_9_c
#   define avg_h264_qpel8_mc00_9_c  ff_avg_pixels8x8_9_c
#   define put_h264_qpel16_mc00_9_c ff_put_pixels16x16_9_c
#   define avg_h264_qpel16_mc00_9_c ff_avg_pixels16x16_9_c
#elif BIT_DEPTH == 10
#   define put_h264_qpel8_mc00_10_c  ff_put_pixels8x8_10_c
#   define avg_h264_qpel8_mc00_10_c  ff_avg_pixels8x8_10_c
#   define put_h264_qpel16_mc00_10_c ff_put_pixels16x16_10_c
#   define avg_h264_qpel16_mc00_10_c ff_avg_pixels16x16_10_c
#endif

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
