/*
 * DSP utils
 * Copyright (c) 2000, 2001 Gerard Lantau.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * gmc & q-pel support by Michael Niedermayer <michaelni@gmx.at>
 */
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "avcodec.h"
#include "dsputil.h"
#include "simple_idct.h"

void (*ff_idct)(DCTELEM *block);
void (*get_pixels)(DCTELEM *block, const UINT8 *pixels, int line_size);
void (*put_pixels_clamped)(const DCTELEM *block, UINT8 *pixels, int line_size);
void (*add_pixels_clamped)(const DCTELEM *block, UINT8 *pixels, int line_size);
void (*gmc1)(UINT8 *dst, UINT8 *src, int srcStride, int h, int x16, int y16, int rounder);

op_pixels_abs_func pix_abs16x16;
op_pixels_abs_func pix_abs16x16_x2;
op_pixels_abs_func pix_abs16x16_y2;
op_pixels_abs_func pix_abs16x16_xy2;

UINT8 cropTbl[256 + 2 * MAX_NEG_CROP];
UINT32 squareTbl[512];

extern UINT16 default_intra_matrix[64];
extern UINT16 default_non_intra_matrix[64];

UINT8 zigzag_direct[64] = {
    0, 1, 8, 16, 9, 2, 3, 10,
    17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

/* not permutated inverse zigzag_direct + 1 for MMX quantizer */
UINT16 __align8 inv_zigzag_direct16[64];

/* not permutated zigzag_direct for MMX quantizer */
UINT8 zigzag_direct_noperm[64];

UINT8 ff_alternate_horizontal_scan[64] = {
    0,  1,  2,  3,  8,  9, 16, 17, 
    10, 11,  4,  5,  6,  7, 15, 14,
    13, 12, 19, 18, 24, 25, 32, 33, 
    26, 27, 20, 21, 22, 23, 28, 29,
    30, 31, 34, 35, 40, 41, 48, 49, 
    42, 43, 36, 37, 38, 39, 44, 45,
    46, 47, 50, 51, 56, 57, 58, 59, 
    52, 53, 54, 55, 60, 61, 62, 63,
};

UINT8 ff_alternate_vertical_scan[64] = {
    0,  8, 16, 24,  1,  9,  2, 10, 
    17, 25, 32, 40, 48, 56, 57, 49,
    41, 33, 26, 18,  3, 11,  4, 12, 
    19, 27, 34, 42, 50, 58, 35, 43,
    51, 59, 20, 28,  5, 13,  6, 14, 
    21, 29, 36, 44, 52, 60, 37, 45,
    53, 61, 22, 30,  7, 15, 23, 31, 
    38, 46, 54, 62, 39, 47, 55, 63,
};

/* Input permutation for the simple_idct_mmx */
static UINT8 simple_mmx_permutation[64]={
	0x00, 0x08, 0x04, 0x09, 0x01, 0x0C, 0x05, 0x0D, 
	0x10, 0x18, 0x14, 0x19, 0x11, 0x1C, 0x15, 0x1D, 
	0x20, 0x28, 0x24, 0x29, 0x21, 0x2C, 0x25, 0x2D, 
	0x12, 0x1A, 0x16, 0x1B, 0x13, 0x1E, 0x17, 0x1F, 
	0x02, 0x0A, 0x06, 0x0B, 0x03, 0x0E, 0x07, 0x0F, 
	0x30, 0x38, 0x34, 0x39, 0x31, 0x3C, 0x35, 0x3D, 
	0x22, 0x2A, 0x26, 0x2B, 0x23, 0x2E, 0x27, 0x2F, 
	0x32, 0x3A, 0x36, 0x3B, 0x33, 0x3E, 0x37, 0x3F,
};

/* a*inverse[b]>>32 == a/b for all 0<=a<=65536 && 2<=b<=255 */
UINT32 inverse[256]={
         0, 4294967295U,2147483648U,1431655766, 1073741824,  858993460,  715827883,  613566757, 
 536870912,  477218589,  429496730,  390451573,  357913942,  330382100,  306783379,  286331154, 
 268435456,  252645136,  238609295,  226050911,  214748365,  204522253,  195225787,  186737709, 
 178956971,  171798692,  165191050,  159072863,  153391690,  148102321,  143165577,  138547333, 
 134217728,  130150525,  126322568,  122713352,  119304648,  116080198,  113025456,  110127367, 
 107374183,  104755300,  102261127,   99882961,   97612894,   95443718,   93368855,   91382283, 
  89478486,   87652394,   85899346,   84215046,   82595525,   81037119,   79536432,   78090315, 
  76695845,   75350304,   74051161,   72796056,   71582789,   70409300,   69273667,   68174085, 
  67108864,   66076420,   65075263,   64103990,   63161284,   62245903,   61356676,   60492498, 
  59652324,   58835169,   58040099,   57266231,   56512728,   55778797,   55063684,   54366675, 
  53687092,   53024288,   52377650,   51746594,   51130564,   50529028,   49941481,   49367441, 
  48806447,   48258060,   47721859,   47197443,   46684428,   46182445,   45691142,   45210183, 
  44739243,   44278014,   43826197,   43383509,   42949673,   42524429,   42107523,   41698712, 
  41297763,   40904451,   40518560,   40139882,   39768216,   39403370,   39045158,   38693400, 
  38347923,   38008561,   37675152,   37347542,   37025581,   36709123,   36398028,   36092163, 
  35791395,   35495598,   35204650,   34918434,   34636834,   34359739,   34087043,   33818641, 
  33554432,   33294321,   33038210,   32786010,   32537632,   32292988,   32051995,   31814573, 
  31580642,   31350127,   31122952,   30899046,   30678338,   30460761,   30246249,   30034737, 
  29826162,   29620465,   29417585,   29217465,   29020050,   28825284,   28633116,   28443493, 
  28256364,   28071682,   27889399,   27709467,   27531842,   27356480,   27183338,   27012373, 
  26843546,   26676816,   26512144,   26349493,   26188825,   26030105,   25873297,   25718368, 
  25565282,   25414008,   25264514,   25116768,   24970741,   24826401,   24683721,   24542671, 
  24403224,   24265352,   24129030,   23994231,   23860930,   23729102,   23598722,   23469767, 
  23342214,   23216040,   23091223,   22967740,   22845571,   22724695,   22605092,   22486740, 
  22369622,   22253717,   22139007,   22025474,   21913099,   21801865,   21691755,   21582751, 
  21474837,   21367997,   21262215,   21157475,   21053762,   20951060,   20849356,   20748635, 
  20648882,   20550083,   20452226,   20355296,   20259280,   20164166,   20069941,   19976593, 
  19884108,   19792477,   19701685,   19611723,   19522579,   19434242,   19346700,   19259944, 
  19173962,   19088744,   19004281,   18920561,   18837576,   18755316,   18673771,   18592933, 
  18512791,   18433337,   18354562,   18276457,   18199014,   18122225,   18046082,   17970575, 
  17895698,   17821442,   17747799,   17674763,   17602325,   17530479,   17459217,   17388532, 
  17318417,   17248865,   17179870,   17111424,   17043522,   16976156,   16909321,   16843010,
};

/* used to skip zeros at the end */
UINT8 zigzag_end[64];

UINT8 permutation[64];
//UINT8 invPermutation[64];

static void build_zigzag_end()
{
    int lastIndex;
    int lastIndexAfterPerm=0;
    for(lastIndex=0; lastIndex<64; lastIndex++)
    {
        if(zigzag_direct[lastIndex] > lastIndexAfterPerm) 
            lastIndexAfterPerm= zigzag_direct[lastIndex];
        zigzag_end[lastIndex]= lastIndexAfterPerm + 1;
    }
}

void get_pixels_c(DCTELEM *block, const UINT8 *pixels, int line_size)
{
    DCTELEM *p;
    const UINT8 *pix;
    int i;

    /* read the pixels */
    p = block;
    pix = pixels;
    for(i=0;i<8;i++) {
        p[0] = pix[0];
        p[1] = pix[1];
        p[2] = pix[2];
        p[3] = pix[3];
        p[4] = pix[4];
        p[5] = pix[5];
        p[6] = pix[6];
        p[7] = pix[7];
        pix += line_size;
        p += 8;
    }
}

void put_pixels_clamped_c(const DCTELEM *block, UINT8 *pixels, int line_size)
{
    const DCTELEM *p;
    UINT8 *pix;
    int i;
    UINT8 *cm = cropTbl + MAX_NEG_CROP;
    
    /* read the pixels */
    p = block;
    pix = pixels;
    for(i=0;i<8;i++) {
        pix[0] = cm[p[0]];
        pix[1] = cm[p[1]];
        pix[2] = cm[p[2]];
        pix[3] = cm[p[3]];
        pix[4] = cm[p[4]];
        pix[5] = cm[p[5]];
        pix[6] = cm[p[6]];
        pix[7] = cm[p[7]];
        pix += line_size;
        p += 8;
    }
}

void add_pixels_clamped_c(const DCTELEM *block, UINT8 *pixels, int line_size)
{
    const DCTELEM *p;
    UINT8 *pix;
    int i;
    UINT8 *cm = cropTbl + MAX_NEG_CROP;
    
    /* read the pixels */
    p = block;
    pix = pixels;
    for(i=0;i<8;i++) {
        pix[0] = cm[pix[0] + p[0]];
        pix[1] = cm[pix[1] + p[1]];
        pix[2] = cm[pix[2] + p[2]];
        pix[3] = cm[pix[3] + p[3]];
        pix[4] = cm[pix[4] + p[4]];
        pix[5] = cm[pix[5] + p[5]];
        pix[6] = cm[pix[6] + p[6]];
        pix[7] = cm[pix[7] + p[7]];
        pix += line_size;
        p += 8;
    }
}

#define PIXOP(BTYPE, OPNAME, OP, INCR)                                                   \
                                                                                         \
static void OPNAME ## _pixels(BTYPE *block, const UINT8 *pixels, int line_size, int h)    \
{                                                                                        \
    BTYPE *p;                                                                            \
    const UINT8 *pix;                                                                    \
                                                                                         \
    p = block;                                                                           \
    pix = pixels;                                                                        \
    do {                                                                                 \
        OP(p[0], pix[0]);                                                                  \
        OP(p[1], pix[1]);                                                                  \
        OP(p[2], pix[2]);                                                                  \
        OP(p[3], pix[3]);                                                                  \
        OP(p[4], pix[4]);                                                                  \
        OP(p[5], pix[5]);                                                                  \
        OP(p[6], pix[6]);                                                                  \
        OP(p[7], pix[7]);                                                                  \
        pix += line_size;                                                                \
        p += INCR;                                                                       \
    } while (--h);;                                                                       \
}                                                                                        \
                                                                                         \
static void OPNAME ## _pixels_x2(BTYPE *block, const UINT8 *pixels, int line_size, int h)     \
{                                                                                        \
    BTYPE *p;                                                                          \
    const UINT8 *pix;                                                                    \
                                                                                         \
    p = block;                                                                           \
    pix = pixels;                                                                        \
    do {                                                                   \
        OP(p[0], avg2(pix[0], pix[1]));                                                    \
        OP(p[1], avg2(pix[1], pix[2]));                                                    \
        OP(p[2], avg2(pix[2], pix[3]));                                                    \
        OP(p[3], avg2(pix[3], pix[4]));                                                    \
        OP(p[4], avg2(pix[4], pix[5]));                                                    \
        OP(p[5], avg2(pix[5], pix[6]));                                                    \
        OP(p[6], avg2(pix[6], pix[7]));                                                    \
        OP(p[7], avg2(pix[7], pix[8]));                                                    \
        pix += line_size;                                                                \
        p += INCR;                                                                       \
    } while (--h);                                                                        \
}                                                                                        \
                                                                                         \
static void OPNAME ## _pixels_y2(BTYPE *block, const UINT8 *pixels, int line_size, int h)     \
{                                                                                        \
    BTYPE *p;                                                                          \
    const UINT8 *pix;                                                                    \
    const UINT8 *pix1;                                                                   \
                                                                                         \
    p = block;                                                                           \
    pix = pixels;                                                                        \
    pix1 = pixels + line_size;                                                           \
    do {                                                                                 \
        OP(p[0], avg2(pix[0], pix1[0]));                                                   \
        OP(p[1], avg2(pix[1], pix1[1]));                                                   \
        OP(p[2], avg2(pix[2], pix1[2]));                                                   \
        OP(p[3], avg2(pix[3], pix1[3]));                                                   \
        OP(p[4], avg2(pix[4], pix1[4]));                                                   \
        OP(p[5], avg2(pix[5], pix1[5]));                                                   \
        OP(p[6], avg2(pix[6], pix1[6]));                                                   \
        OP(p[7], avg2(pix[7], pix1[7]));                                                   \
        pix += line_size;                                                                \
        pix1 += line_size;                                                               \
        p += INCR;                                                                       \
    } while(--h);                                                                         \
}                                                                                        \
                                                                                         \
static void OPNAME ## _pixels_xy2(BTYPE *block, const UINT8 *pixels, int line_size, int h)    \
{                                                                                        \
    BTYPE *p;                                                                          \
    const UINT8 *pix;                                                                    \
    const UINT8 *pix1;                                                                   \
                                                                                         \
    p = block;                                                                           \
    pix = pixels;                                                                        \
    pix1 = pixels + line_size;                                                           \
    do {                                                                   \
        OP(p[0], avg4(pix[0], pix[1], pix1[0], pix1[1]));                                  \
        OP(p[1], avg4(pix[1], pix[2], pix1[1], pix1[2]));                                  \
        OP(p[2], avg4(pix[2], pix[3], pix1[2], pix1[3]));                                  \
        OP(p[3], avg4(pix[3], pix[4], pix1[3], pix1[4]));                                  \
        OP(p[4], avg4(pix[4], pix[5], pix1[4], pix1[5]));                                  \
        OP(p[5], avg4(pix[5], pix[6], pix1[5], pix1[6]));                                  \
        OP(p[6], avg4(pix[6], pix[7], pix1[6], pix1[7]));                                  \
        OP(p[7], avg4(pix[7], pix[8], pix1[7], pix1[8]));                                  \
        pix += line_size;                                                                \
        pix1 += line_size;                                                               \
        p += INCR;                                                                       \
    } while(--h);                                                                         \
}                                                                                        \
                                                                                         \
void (*OPNAME ## _pixels_tab[4])(BTYPE *block, const UINT8 *pixels, int line_size, int h) = { \
    OPNAME ## _pixels,                                                                   \
    OPNAME ## _pixels_x2,                                                                \
    OPNAME ## _pixels_y2,                                                                \
    OPNAME ## _pixels_xy2,                                                               \
};


/* rounding primitives */
#define avg2(a,b) ((a+b+1)>>1)
#define avg4(a,b,c,d) ((a+b+c+d+2)>>2)

#define op_put(a, b) a = b
#define op_avg(a, b) a = avg2(a, b)
#define op_sub(a, b) a -= b

PIXOP(UINT8, put, op_put, line_size)
PIXOP(UINT8, avg, op_avg, line_size)

PIXOP(DCTELEM, sub, op_sub, 8)

/* not rounding primitives */
#undef avg2
#undef avg4
#define avg2(a,b) ((a+b)>>1)
#define avg4(a,b,c,d) ((a+b+c+d+1)>>2)

PIXOP(UINT8, put_no_rnd, op_put, line_size)
PIXOP(UINT8, avg_no_rnd, op_avg, line_size)

/* motion estimation */

#undef avg2
#undef avg4
#define avg2(a,b) ((a+b+1)>>1)
#define avg4(a,b,c,d) ((a+b+c+d+2)>>2)

static void gmc1_c(UINT8 *dst, UINT8 *src, int srcStride, int h, int x16, int y16, int rounder)
{
    const int A=(16-x16)*(16-y16);
    const int B=(   x16)*(16-y16);
    const int C=(16-x16)*(   y16);
    const int D=(   x16)*(   y16);
    int i;
    rounder= 128 - rounder;

    for(i=0; i<h; i++)
    {
        dst[0]= (A*src[0] + B*src[1] + C*src[srcStride+0] + D*src[srcStride+1] + rounder)>>8;
        dst[1]= (A*src[1] + B*src[2] + C*src[srcStride+1] + D*src[srcStride+2] + rounder)>>8;
        dst[2]= (A*src[2] + B*src[3] + C*src[srcStride+2] + D*src[srcStride+3] + rounder)>>8;
        dst[3]= (A*src[3] + B*src[4] + C*src[srcStride+3] + D*src[srcStride+4] + rounder)>>8;
        dst[4]= (A*src[4] + B*src[5] + C*src[srcStride+4] + D*src[srcStride+5] + rounder)>>8;
        dst[5]= (A*src[5] + B*src[6] + C*src[srcStride+5] + D*src[srcStride+6] + rounder)>>8;
        dst[6]= (A*src[6] + B*src[7] + C*src[srcStride+6] + D*src[srcStride+7] + rounder)>>8;
        dst[7]= (A*src[7] + B*src[8] + C*src[srcStride+7] + D*src[srcStride+8] + rounder)>>8;
        dst+= srcStride;
        src+= srcStride;
    }
}

static void qpel_h_lowpass(UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int h, int r)
{
    UINT8 *cm = cropTbl + MAX_NEG_CROP;
    int i;
    for(i=0; i<h; i++)
    {
        dst[0]= cm[(((src[0]+src[1])*160 - (src[0]+src[2])*48 + (src[1]+src[3])*24 - (src[2]+src[4])*8 + r)>>8)];
        dst[1]= cm[(((src[1]+src[2])*160 - (src[0]+src[3])*48 + (src[0]+src[4])*24 - (src[1]+src[5])*8 + r)>>8)];
        dst[2]= cm[(((src[2]+src[3])*160 - (src[1]+src[4])*48 + (src[0]+src[5])*24 - (src[0]+src[6])*8 + r)>>8)];
        dst[3]= cm[(((src[3]+src[4])*160 - (src[2]+src[5])*48 + (src[1]+src[6])*24 - (src[0]+src[7])*8 + r)>>8)];
        dst[4]= cm[(((src[4]+src[5])*160 - (src[3]+src[6])*48 + (src[2]+src[7])*24 - (src[1]+src[8])*8 + r)>>8)];
        dst[5]= cm[(((src[5]+src[6])*160 - (src[4]+src[7])*48 + (src[3]+src[8])*24 - (src[2]+src[8])*8 + r)>>8)];
        dst[6]= cm[(((src[6]+src[7])*160 - (src[5]+src[8])*48 + (src[4]+src[8])*24 - (src[3]+src[7])*8 + r)>>8)];
        dst[7]= cm[(((src[7]+src[8])*160 - (src[6]+src[8])*48 + (src[5]+src[7])*24 - (src[4]+src[6])*8 + r)>>8)];
        dst+=dstStride;
        src+=srcStride;
    }
}

static void qpel_v_lowpass(UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int w, int r)
{
    UINT8 *cm = cropTbl + MAX_NEG_CROP;
    int i;
    for(i=0; i<w; i++)
    {
        const int src0= src[0*srcStride];
        const int src1= src[1*srcStride];
        const int src2= src[2*srcStride];
        const int src3= src[3*srcStride];
        const int src4= src[4*srcStride];
        const int src5= src[5*srcStride];
        const int src6= src[6*srcStride];
        const int src7= src[7*srcStride];
        const int src8= src[8*srcStride];
        dst[0*dstStride]= cm[(((src0+src1)*160 - (src0+src2)*48 + (src1+src3)*24 - (src2+src4)*8 + r)>>8)];
        dst[1*dstStride]= cm[(((src1+src2)*160 - (src0+src3)*48 + (src0+src4)*24 - (src1+src5)*8 + r)>>8)];
        dst[2*dstStride]= cm[(((src2+src3)*160 - (src1+src4)*48 + (src0+src5)*24 - (src0+src6)*8 + r)>>8)];
        dst[3*dstStride]= cm[(((src3+src4)*160 - (src2+src5)*48 + (src1+src6)*24 - (src0+src7)*8 + r)>>8)];
        dst[4*dstStride]= cm[(((src4+src5)*160 - (src3+src6)*48 + (src2+src7)*24 - (src1+src8)*8 + r)>>8)];
        dst[5*dstStride]= cm[(((src5+src6)*160 - (src4+src7)*48 + (src3+src8)*24 - (src2+src8)*8 + r)>>8)];
        dst[6*dstStride]= cm[(((src6+src7)*160 - (src5+src8)*48 + (src4+src8)*24 - (src3+src7)*8 + r)>>8)];
        dst[7*dstStride]= cm[(((src7+src8)*160 - (src6+src8)*48 + (src5+src7)*24 - (src4+src6)*8 + r)>>8)];
        dst++;
        src++;
    }
}

static inline void put_block(UINT8 *dst, UINT8 *src, int dstStride, int srcStride)
{
    int i;
    for(i=0; i<8; i++)
    {
        dst[0]= src[0];
        dst[1]= src[1];
        dst[2]= src[2];
        dst[3]= src[3];
        dst[4]= src[4];
        dst[5]= src[5];
        dst[6]= src[6];
        dst[7]= src[7];
        dst+=dstStride;
        src+=srcStride;
    }
}

static inline void avg2_block(UINT8 *dst, UINT8 *src1, UINT8 *src2, int dstStride, int srcStride, int r)
{
    int i;
    for(i=0; i<8; i++)
    {
        dst[0]= (src1[0] + src2[0] + r)>>1;
        dst[1]= (src1[1] + src2[1] + r)>>1;
        dst[2]= (src1[2] + src2[2] + r)>>1;
        dst[3]= (src1[3] + src2[3] + r)>>1;
        dst[4]= (src1[4] + src2[4] + r)>>1;
        dst[5]= (src1[5] + src2[5] + r)>>1;
        dst[6]= (src1[6] + src2[6] + r)>>1;
        dst[7]= (src1[7] + src2[7] + r)>>1;
        dst+=dstStride;
        src1+=srcStride;
        src2+=8;
    }
}

static inline void avg4_block(UINT8 *dst, UINT8 *src1, UINT8 *src2, UINT8 *src3, UINT8 *src4, int dstStride, int srcStride, int r)
{
    int i;
    for(i=0; i<8; i++)
    {
        dst[0]= (src1[0] + src2[0] + src3[0] + src4[0] + r)>>2;
        dst[1]= (src1[1] + src2[1] + src3[1] + src4[1] + r)>>2;
        dst[2]= (src1[2] + src2[2] + src3[2] + src4[2] + r)>>2;
        dst[3]= (src1[3] + src2[3] + src3[3] + src4[3] + r)>>2;
        dst[4]= (src1[4] + src2[4] + src3[4] + src4[4] + r)>>2;
        dst[5]= (src1[5] + src2[5] + src3[5] + src4[5] + r)>>2;
        dst[6]= (src1[6] + src2[6] + src3[6] + src4[6] + r)>>2;
        dst[7]= (src1[7] + src2[7] + src3[7] + src4[7] + r)>>2;
        dst+=dstStride;
        src1+=srcStride;
        src2+=8;
        src3+=8;
        src4+=8;
    }
}

#define QPEL_MC(r, name) \
static void qpel_mc00_c ## name (UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int mx, int my)\
{\
    put_block(dst, src, dstStride, srcStride);\
}\
\
static void qpel_mc10_c ## name (UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int mx, int my)\
{\
    UINT8 half[64];\
    qpel_h_lowpass(half, src, 8, srcStride, 8, 128-r);\
    avg2_block(dst, src, half, dstStride, srcStride, 1-r);\
}\
\
static void qpel_mc20_c ## name (UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int mx, int my)\
{\
    qpel_h_lowpass(dst, src, dstStride, srcStride, 8, 128-r);\
}\
\
static void qpel_mc30_c ## name (UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int mx, int my)\
{\
    UINT8 half[64];\
    qpel_h_lowpass(half, src, 8, srcStride, 8, 128-r);\
    avg2_block(dst, src+1, half, dstStride, srcStride, 1-r);\
}\
\
static void qpel_mc01_c ## name (UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int mx, int my)\
{\
    UINT8 half[64];\
    qpel_v_lowpass(half, src, 8, srcStride, 8, 128-r);\
    avg2_block(dst, src, half, dstStride, srcStride, 1-r);\
}\
\
static void qpel_mc02_c ## name (UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int mx, int my)\
{\
    qpel_v_lowpass(dst, src, dstStride, srcStride, 8, 128-r);\
}\
\
static void qpel_mc03_c ## name (UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int mx, int my)\
{\
    UINT8 half[64];\
    qpel_v_lowpass(half, src, 8, srcStride, 8, 128-r);\
    avg2_block(dst, src+srcStride, half, dstStride, srcStride, 1-r);\
}\
static void qpel_mc11_c ## name (UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int mx, int my)\
{\
    UINT8 halfH[72];\
    UINT8 halfV[64];\
    UINT8 halfHV[64];\
    qpel_h_lowpass(halfH, src, 8, srcStride, 9, 128-r);\
    qpel_v_lowpass(halfV, src, 8, srcStride, 8, 128-r);\
    qpel_v_lowpass(halfHV, halfH, 8, 8, 8, 128-r);\
    avg4_block(dst, src, halfH, halfV, halfHV, dstStride, srcStride, 2-r);\
}\
static void qpel_mc31_c ## name (UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int mx, int my)\
{\
    UINT8 halfH[72];\
    UINT8 halfV[64];\
    UINT8 halfHV[64];\
    qpel_h_lowpass(halfH, src, 8, srcStride, 9, 128-r);\
    qpel_v_lowpass(halfV, src+1, 8, srcStride, 8, 128-r);\
    qpel_v_lowpass(halfHV, halfH, 8, 8, 8, 128-r);\
    avg4_block(dst, src+1, halfH, halfV, halfHV, dstStride, srcStride, 2-r);\
}\
static void qpel_mc13_c ## name (UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int mx, int my)\
{\
    UINT8 halfH[72];\
    UINT8 halfV[64];\
    UINT8 halfHV[64];\
    qpel_h_lowpass(halfH, src, 8, srcStride, 9, 128-r);\
    qpel_v_lowpass(halfV, src, 8, srcStride, 8, 128-r);\
    qpel_v_lowpass(halfHV, halfH, 8, 8, 8, 128-r);\
    avg4_block(dst, src+srcStride, halfH+8, halfV, halfHV, dstStride, srcStride, 2-r);\
}\
static void qpel_mc33_c ## name (UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int mx, int my)\
{\
    UINT8 halfH[72];\
    UINT8 halfV[64];\
    UINT8 halfHV[64];\
    qpel_h_lowpass(halfH, src, 8, srcStride, 9, 128-r);\
    qpel_v_lowpass(halfV, src+1, 8, srcStride, 8, 128-r);\
    qpel_v_lowpass(halfHV, halfH, 8, 8, 8, 128-r);\
    avg4_block(dst, src+srcStride+1, halfH+8, halfV, halfHV, dstStride, srcStride, 2-r);\
}\
static void qpel_mc21_c ## name (UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int mx, int my)\
{\
    UINT8 halfH[72];\
    UINT8 halfHV[64];\
    qpel_h_lowpass(halfH, src, 8, srcStride, 9, 128-r);\
    qpel_v_lowpass(halfHV, halfH, 8, 8, 8, 128-r);\
    avg2_block(dst, halfH, halfHV, dstStride, 8, 1-r);\
}\
static void qpel_mc23_c ## name (UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int mx, int my)\
{\
    UINT8 halfH[72];\
    UINT8 halfHV[64];\
    qpel_h_lowpass(halfH, src, 8, srcStride, 9, 128-r);\
    qpel_v_lowpass(halfHV, halfH, 8, 8, 8, 128-r);\
    avg2_block(dst, halfH+8, halfHV, dstStride, 8, 1-r);\
}\
static void qpel_mc12_c ## name (UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int mx, int my)\
{\
    UINT8 halfH[72];\
    UINT8 halfV[64];\
    UINT8 halfHV[64];\
    qpel_h_lowpass(halfH, src, 8, srcStride, 9, 128-r);\
    qpel_v_lowpass(halfV, src, 8, srcStride, 8, 128-r);\
    qpel_v_lowpass(halfHV, halfH, 8, 8, 8, 128-r);\
    avg2_block(dst, halfV, halfHV, dstStride, 8, 1-r);\
}\
static void qpel_mc32_c ## name (UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int mx, int my)\
{\
    UINT8 halfH[72];\
    UINT8 halfV[64];\
    UINT8 halfHV[64];\
    qpel_h_lowpass(halfH, src, 8, srcStride, 9, 128-r);\
    qpel_v_lowpass(halfV, src+1, 8, srcStride, 8, 128-r);\
    qpel_v_lowpass(halfHV, halfH, 8, 8, 8, 128-r);\
    avg2_block(dst, halfV, halfHV, dstStride, 8, 1-r);\
}\
static void qpel_mc22_c ## name (UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int mx, int my)\
{\
    UINT8 halfH[72];\
    qpel_h_lowpass(halfH, src, 8, srcStride, 9, 128-r);\
    qpel_v_lowpass(dst, halfH, dstStride, 8, 8, 128-r);\
}\
qpel_mc_func qpel_mc ## name ## _tab[16]={ \
    qpel_mc00_c ## name,                                                                   \
    qpel_mc10_c ## name,                                                                   \
    qpel_mc20_c ## name,                                                                   \
    qpel_mc30_c ## name,                                                                   \
    qpel_mc01_c ## name,                                                                   \
    qpel_mc11_c ## name,                                                                   \
    qpel_mc21_c ## name,                                                                   \
    qpel_mc31_c ## name,                                                                   \
    qpel_mc02_c ## name,                                                                   \
    qpel_mc12_c ## name,                                                                   \
    qpel_mc22_c ## name,                                                                   \
    qpel_mc32_c ## name,                                                                   \
    qpel_mc03_c ## name,                                                                   \
    qpel_mc13_c ## name,                                                                   \
    qpel_mc23_c ## name,                                                                   \
    qpel_mc33_c ## name,                                                                   \
};

QPEL_MC(0, _rnd)
QPEL_MC(1, _no_rnd)

int pix_abs16x16_c(UINT8 *pix1, UINT8 *pix2, int line_size, int h)
{
    int s, i;

    s = 0;
    for(i=0;i<h;i++) {
        s += abs(pix1[0] - pix2[0]);
        s += abs(pix1[1] - pix2[1]);
        s += abs(pix1[2] - pix2[2]);
        s += abs(pix1[3] - pix2[3]);
        s += abs(pix1[4] - pix2[4]);
        s += abs(pix1[5] - pix2[5]);
        s += abs(pix1[6] - pix2[6]);
        s += abs(pix1[7] - pix2[7]);
        s += abs(pix1[8] - pix2[8]);
        s += abs(pix1[9] - pix2[9]);
        s += abs(pix1[10] - pix2[10]);
        s += abs(pix1[11] - pix2[11]);
        s += abs(pix1[12] - pix2[12]);
        s += abs(pix1[13] - pix2[13]);
        s += abs(pix1[14] - pix2[14]);
        s += abs(pix1[15] - pix2[15]);
        pix1 += line_size;
        pix2 += line_size;
    }
    return s;
}

int pix_abs16x16_x2_c(UINT8 *pix1, UINT8 *pix2, int line_size, int h)
{
    int s, i;

    s = 0;
    for(i=0;i<h;i++) {
        s += abs(pix1[0] - avg2(pix2[0], pix2[1]));
        s += abs(pix1[1] - avg2(pix2[1], pix2[2]));
        s += abs(pix1[2] - avg2(pix2[2], pix2[3]));
        s += abs(pix1[3] - avg2(pix2[3], pix2[4]));
        s += abs(pix1[4] - avg2(pix2[4], pix2[5]));
        s += abs(pix1[5] - avg2(pix2[5], pix2[6]));
        s += abs(pix1[6] - avg2(pix2[6], pix2[7]));
        s += abs(pix1[7] - avg2(pix2[7], pix2[8]));
        s += abs(pix1[8] - avg2(pix2[8], pix2[9]));
        s += abs(pix1[9] - avg2(pix2[9], pix2[10]));
        s += abs(pix1[10] - avg2(pix2[10], pix2[11]));
        s += abs(pix1[11] - avg2(pix2[11], pix2[12]));
        s += abs(pix1[12] - avg2(pix2[12], pix2[13]));
        s += abs(pix1[13] - avg2(pix2[13], pix2[14]));
        s += abs(pix1[14] - avg2(pix2[14], pix2[15]));
        s += abs(pix1[15] - avg2(pix2[15], pix2[16]));
        pix1 += line_size;
        pix2 += line_size;
    }
    return s;
}

int pix_abs16x16_y2_c(UINT8 *pix1, UINT8 *pix2, int line_size, int h)
{
    int s, i;
    UINT8 *pix3 = pix2 + line_size;

    s = 0;
    for(i=0;i<h;i++) {
        s += abs(pix1[0] - avg2(pix2[0], pix3[0]));
        s += abs(pix1[1] - avg2(pix2[1], pix3[1]));
        s += abs(pix1[2] - avg2(pix2[2], pix3[2]));
        s += abs(pix1[3] - avg2(pix2[3], pix3[3]));
        s += abs(pix1[4] - avg2(pix2[4], pix3[4]));
        s += abs(pix1[5] - avg2(pix2[5], pix3[5]));
        s += abs(pix1[6] - avg2(pix2[6], pix3[6]));
        s += abs(pix1[7] - avg2(pix2[7], pix3[7]));
        s += abs(pix1[8] - avg2(pix2[8], pix3[8]));
        s += abs(pix1[9] - avg2(pix2[9], pix3[9]));
        s += abs(pix1[10] - avg2(pix2[10], pix3[10]));
        s += abs(pix1[11] - avg2(pix2[11], pix3[11]));
        s += abs(pix1[12] - avg2(pix2[12], pix3[12]));
        s += abs(pix1[13] - avg2(pix2[13], pix3[13]));
        s += abs(pix1[14] - avg2(pix2[14], pix3[14]));
        s += abs(pix1[15] - avg2(pix2[15], pix3[15]));
        pix1 += line_size;
        pix2 += line_size;
        pix3 += line_size;
    }
    return s;
}

int pix_abs16x16_xy2_c(UINT8 *pix1, UINT8 *pix2, int line_size, int h)
{
    int s, i;
    UINT8 *pix3 = pix2 + line_size;

    s = 0;
    for(i=0;i<h;i++) {
        s += abs(pix1[0] - avg4(pix2[0], pix2[1], pix3[0], pix3[1]));
        s += abs(pix1[1] - avg4(pix2[1], pix2[2], pix3[1], pix3[2]));
        s += abs(pix1[2] - avg4(pix2[2], pix2[3], pix3[2], pix3[3]));
        s += abs(pix1[3] - avg4(pix2[3], pix2[4], pix3[3], pix3[4]));
        s += abs(pix1[4] - avg4(pix2[4], pix2[5], pix3[4], pix3[5]));
        s += abs(pix1[5] - avg4(pix2[5], pix2[6], pix3[5], pix3[6]));
        s += abs(pix1[6] - avg4(pix2[6], pix2[7], pix3[6], pix3[7]));
        s += abs(pix1[7] - avg4(pix2[7], pix2[8], pix3[7], pix3[8]));
        s += abs(pix1[8] - avg4(pix2[8], pix2[9], pix3[8], pix3[9]));
        s += abs(pix1[9] - avg4(pix2[9], pix2[10], pix3[9], pix3[10]));
        s += abs(pix1[10] - avg4(pix2[10], pix2[11], pix3[10], pix3[11]));
        s += abs(pix1[11] - avg4(pix2[11], pix2[12], pix3[11], pix3[12]));
        s += abs(pix1[12] - avg4(pix2[12], pix2[13], pix3[12], pix3[13]));
        s += abs(pix1[13] - avg4(pix2[13], pix2[14], pix3[13], pix3[14]));
        s += abs(pix1[14] - avg4(pix2[14], pix2[15], pix3[14], pix3[15]));
        s += abs(pix1[15] - avg4(pix2[15], pix2[16], pix3[15], pix3[16]));
        pix1 += line_size;
        pix2 += line_size;
        pix3 += line_size;
    }
    return s;
}

/* permute block according so that it corresponds to the MMX idct
   order */
#ifdef SIMPLE_IDCT
 /* general permutation, but perhaps slightly slower */
void block_permute(INT16 *block)
{
	int i;
	INT16 temp[64];

	for(i=0; i<64; i++) temp[ block_permute_op(i) ] = block[i];

	for(i=0; i<64; i++) block[i] = temp[i];
}
#else

void block_permute(INT16 *block)
{
    int tmp1, tmp2, tmp3, tmp4, tmp5, tmp6;
    int i;

    for(i=0;i<8;i++) {
        tmp1 = block[1];
        tmp2 = block[2];
        tmp3 = block[3];
        tmp4 = block[4];
        tmp5 = block[5];
        tmp6 = block[6];
        block[1] = tmp2;
        block[2] = tmp4;
        block[3] = tmp6;
        block[4] = tmp1;
        block[5] = tmp3;
        block[6] = tmp5;
        block += 8;
    }
}
#endif

void dsputil_init(void)
{
    int i, j;
    int use_permuted_idct;

    for(i=0;i<256;i++) cropTbl[i + MAX_NEG_CROP] = i;
    for(i=0;i<MAX_NEG_CROP;i++) {
        cropTbl[i] = 0;
        cropTbl[i + MAX_NEG_CROP + 256] = 255;
    }

    for(i=0;i<512;i++) {
        squareTbl[i] = (i - 256) * (i - 256);
    }

#ifdef SIMPLE_IDCT
    ff_idct = simple_idct;
#else
    ff_idct = j_rev_dct;
#endif
    get_pixels = get_pixels_c;
    put_pixels_clamped = put_pixels_clamped_c;
    add_pixels_clamped = add_pixels_clamped_c;
    gmc1= gmc1_c;

    pix_abs16x16 = pix_abs16x16_c;
    pix_abs16x16_x2 = pix_abs16x16_x2_c;
    pix_abs16x16_y2 = pix_abs16x16_y2_c;
    pix_abs16x16_xy2 = pix_abs16x16_xy2_c;
    av_fdct = jpeg_fdct_ifast;

    use_permuted_idct = 1;

#ifdef HAVE_MMX
    dsputil_init_mmx();
#endif
#ifdef ARCH_ARMV4L
    dsputil_init_armv4l();
#endif
#ifdef HAVE_MLIB
    dsputil_init_mlib();
    use_permuted_idct = 0;
#endif
#ifdef ARCH_ALPHA
    dsputil_init_alpha();
    use_permuted_idct = 0;
#endif

#ifdef SIMPLE_IDCT
    if(ff_idct == simple_idct) use_permuted_idct=0;
#endif

    if(use_permuted_idct)
#ifdef SIMPLE_IDCT
        for(i=0; i<64; i++) permutation[i]= simple_mmx_permutation[i];
#else
        for(i=0; i<64; i++) permutation[i]= (i & 0x38) | ((i & 6) >> 1) | ((i & 1) << 2);
#endif
    else
        for(i=0; i<64; i++) permutation[i]=i;

    for(i=0; i<64; i++) inv_zigzag_direct16[zigzag_direct[i]]= i+1;
    for(i=0; i<64; i++) zigzag_direct_noperm[i]= zigzag_direct[i];
    
    if (use_permuted_idct) {
        /* permute for IDCT */
        for(i=0;i<64;i++) {
            j = zigzag_direct[i];
            zigzag_direct[i] = block_permute_op(j);
            j = ff_alternate_horizontal_scan[i];
            ff_alternate_horizontal_scan[i] = block_permute_op(j);
            j = ff_alternate_vertical_scan[i];
            ff_alternate_vertical_scan[i] = block_permute_op(j);
        }
        block_permute(default_intra_matrix);
        block_permute(default_non_intra_matrix);
    }
    
    build_zigzag_end();
}

void get_psnr(UINT8 *orig_image[3], UINT8 *coded_image[3],
              int orig_linesize[3], int coded_linesize,
              AVCodecContext *avctx)
{
    int quad, diff, x, y;
    UINT8 *orig, *coded;
    UINT32 *sq = squareTbl + 256;
    
    quad = 0;
    diff = 0;
    
    /* Luminance */
    orig = orig_image[0];
    coded = coded_image[0];
    
    for (y=0;y<avctx->height;y++) {
        for (x=0;x<avctx->width;x++) {
            diff = *(orig + x) - *(coded + x);
            quad += sq[diff];
        }
        orig += orig_linesize[0];
        coded += coded_linesize;
    }
   
    avctx->psnr_y = (float) quad / (float) (avctx->width * avctx->height);
    
    if (avctx->psnr_y) {
        avctx->psnr_y = (float) (255 * 255) / avctx->psnr_y;
        avctx->psnr_y = 10 * (float) log10 (avctx->psnr_y); 
    } else
        avctx->psnr_y = 99.99;
}

