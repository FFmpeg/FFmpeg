/*
 * DSP utils
 * Copyright (c) 2000, 2001 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * gmc & q-pel & 32/64 bit based MC by Michael Niedermayer <michaelni@gmx.at>
 */
#include "avcodec.h"
#include "dsputil.h"

int ff_bit_exact=0;

UINT8 cropTbl[256 + 2 * MAX_NEG_CROP];
UINT32 squareTbl[512];

const UINT8 ff_zigzag_direct[64] = {
    0,   1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

/* not permutated inverse zigzag_direct + 1 for MMX quantizer */
UINT16 __align8 inv_zigzag_direct16[64];

const UINT8 ff_alternate_horizontal_scan[64] = {
    0,  1,   2,  3,  8,  9, 16, 17, 
    10, 11,  4,  5,  6,  7, 15, 14,
    13, 12, 19, 18, 24, 25, 32, 33, 
    26, 27, 20, 21, 22, 23, 28, 29,
    30, 31, 34, 35, 40, 41, 48, 49, 
    42, 43, 36, 37, 38, 39, 44, 45,
    46, 47, 50, 51, 56, 57, 58, 59, 
    52, 53, 54, 55, 60, 61, 62, 63,
};

const UINT8 ff_alternate_vertical_scan[64] = {
    0,  8,  16, 24,  1,  9,  2, 10, 
    17, 25, 32, 40, 48, 56, 57, 49,
    41, 33, 26, 18,  3, 11,  4, 12, 
    19, 27, 34, 42, 50, 58, 35, 43,
    51, 59, 20, 28,  5, 13,  6, 14, 
    21, 29, 36, 44, 52, 60, 37, 45,
    53, 61, 22, 30,  7, 15, 23, 31, 
    38, 46, 54, 62, 39, 47, 55, 63,
};

/* a*inverse[b]>>32 == a/b for all 0<=a<=65536 && 2<=b<=255 */
const UINT32 inverse[256]={
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

static int pix_sum_c(UINT8 * pix, int line_size)
{
    int s, i, j;

    s = 0;
    for (i = 0; i < 16; i++) {
	for (j = 0; j < 16; j += 8) {
	    s += pix[0];
	    s += pix[1];
	    s += pix[2];
	    s += pix[3];
	    s += pix[4];
	    s += pix[5];
	    s += pix[6];
	    s += pix[7];
	    pix += 8;
	}
	pix += line_size - 16;
    }
    return s;
}

static int pix_norm1_c(UINT8 * pix, int line_size)
{
    int s, i, j;
    UINT32 *sq = squareTbl + 256;

    s = 0;
    for (i = 0; i < 16; i++) {
	for (j = 0; j < 16; j += 8) {
	    s += sq[pix[0]];
	    s += sq[pix[1]];
	    s += sq[pix[2]];
	    s += sq[pix[3]];
	    s += sq[pix[4]];
	    s += sq[pix[5]];
	    s += sq[pix[6]];
	    s += sq[pix[7]];
	    pix += 8;
	}
	pix += line_size - 16;
    }
    return s;
}


static int pix_norm_c(UINT8 * pix1, UINT8 * pix2, int line_size)
{
    int s, i, j;
    UINT32 *sq = squareTbl + 256;

    s = 0;
    for (i = 0; i < 16; i++) {
        for (j = 0; j < 16; j += 8) {
            s += sq[pix1[0] - pix2[0]];
            s += sq[pix1[1] - pix2[1]];
            s += sq[pix1[2] - pix2[2]];
            s += sq[pix1[3] - pix2[3]];
            s += sq[pix1[4] - pix2[4]];
            s += sq[pix1[5] - pix2[5]];
            s += sq[pix1[6] - pix2[6]];
            s += sq[pix1[7] - pix2[7]];
            pix1 += 8;
            pix2 += 8;
        }
        pix1 += line_size - 16;
        pix2 += line_size - 16;
    }
    return s;
}

static void get_pixels_c(DCTELEM *restrict block, const UINT8 *pixels, int line_size)
{
    int i;

    /* read the pixels */
    for(i=0;i<8;i++) {
        block[0] = pixels[0];
        block[1] = pixels[1];
        block[2] = pixels[2];
        block[3] = pixels[3];
        block[4] = pixels[4];
        block[5] = pixels[5];
        block[6] = pixels[6];
        block[7] = pixels[7];
        pixels += line_size;
        block += 8;
    }
}

static void diff_pixels_c(DCTELEM *restrict block, const UINT8 *s1,
			  const UINT8 *s2, int stride){
    int i;

    /* read the pixels */
    for(i=0;i<8;i++) {
        block[0] = s1[0] - s2[0];
        block[1] = s1[1] - s2[1];
        block[2] = s1[2] - s2[2];
        block[3] = s1[3] - s2[3];
        block[4] = s1[4] - s2[4];
        block[5] = s1[5] - s2[5];
        block[6] = s1[6] - s2[6];
        block[7] = s1[7] - s2[7];
        s1 += stride;
        s2 += stride;
        block += 8;
    }
}


static void put_pixels_clamped_c(const DCTELEM *block, UINT8 *restrict pixels,
				 int line_size)
{
    int i;
    UINT8 *cm = cropTbl + MAX_NEG_CROP;
    
    /* read the pixels */
    for(i=0;i<8;i++) {
        pixels[0] = cm[block[0]];
        pixels[1] = cm[block[1]];
        pixels[2] = cm[block[2]];
        pixels[3] = cm[block[3]];
        pixels[4] = cm[block[4]];
        pixels[5] = cm[block[5]];
        pixels[6] = cm[block[6]];
        pixels[7] = cm[block[7]];

        pixels += line_size;
        block += 8;
    }
}

static void add_pixels_clamped_c(const DCTELEM *block, UINT8 *restrict pixels,
                          int line_size)
{
    int i;
    UINT8 *cm = cropTbl + MAX_NEG_CROP;
    
    /* read the pixels */
    for(i=0;i<8;i++) {
        pixels[0] = cm[pixels[0] + block[0]];
        pixels[1] = cm[pixels[1] + block[1]];
        pixels[2] = cm[pixels[2] + block[2]];
        pixels[3] = cm[pixels[3] + block[3]];
        pixels[4] = cm[pixels[4] + block[4]];
        pixels[5] = cm[pixels[5] + block[5]];
        pixels[6] = cm[pixels[6] + block[6]];
        pixels[7] = cm[pixels[7] + block[7]];
        pixels += line_size;
        block += 8;
    }
}
#if 0

#define PIXOP2(OPNAME, OP) \
static void OPNAME ## _pixels(uint8_t *block, const uint8_t *pixels, int line_size, int h)\
{\
    int i;\
    for(i=0; i<h; i++){\
        OP(*((uint64_t*)block), LD64(pixels));\
        pixels+=line_size;\
        block +=line_size;\
    }\
}\
\
static void OPNAME ## _no_rnd_pixels_x2_c(uint8_t *block, const uint8_t *pixels, int line_size, int h)\
{\
    int i;\
    for(i=0; i<h; i++){\
        const uint64_t a= LD64(pixels  );\
        const uint64_t b= LD64(pixels+1);\
        OP(*((uint64_t*)block), (a&b) + (((a^b)&0xFEFEFEFEFEFEFEFEULL)>>1));\
        pixels+=line_size;\
        block +=line_size;\
    }\
}\
\
static void OPNAME ## _pixels_x2_c(uint8_t *block, const uint8_t *pixels, int line_size, int h)\
{\
    int i;\
    for(i=0; i<h; i++){\
        const uint64_t a= LD64(pixels  );\
        const uint64_t b= LD64(pixels+1);\
        OP(*((uint64_t*)block), (a|b) - (((a^b)&0xFEFEFEFEFEFEFEFEULL)>>1));\
        pixels+=line_size;\
        block +=line_size;\
    }\
}\
\
static void OPNAME ## _no_rnd_pixels_y2_c(uint8_t *block, const uint8_t *pixels, int line_size, int h)\
{\
    int i;\
    for(i=0; i<h; i++){\
        const uint64_t a= LD64(pixels          );\
        const uint64_t b= LD64(pixels+line_size);\
        OP(*((uint64_t*)block), (a&b) + (((a^b)&0xFEFEFEFEFEFEFEFEULL)>>1));\
        pixels+=line_size;\
        block +=line_size;\
    }\
}\
\
static void OPNAME ## _pixels_y2_c(uint8_t *block, const uint8_t *pixels, int line_size, int h)\
{\
    int i;\
    for(i=0; i<h; i++){\
        const uint64_t a= LD64(pixels          );\
        const uint64_t b= LD64(pixels+line_size);\
        OP(*((uint64_t*)block), (a|b) - (((a^b)&0xFEFEFEFEFEFEFEFEULL)>>1));\
        pixels+=line_size;\
        block +=line_size;\
    }\
}\
\
static void OPNAME ## _pixels_xy2_c(uint8_t *block, const uint8_t *pixels, int line_size, int h)\
{\
        int i;\
        const uint64_t a= LD64(pixels  );\
        const uint64_t b= LD64(pixels+1);\
        uint64_t l0=  (a&0x0303030303030303ULL)\
                    + (b&0x0303030303030303ULL)\
                    + 0x0202020202020202ULL;\
        uint64_t h0= ((a&0xFCFCFCFCFCFCFCFCULL)>>2)\
                   + ((b&0xFCFCFCFCFCFCFCFCULL)>>2);\
        uint64_t l1,h1;\
\
        pixels+=line_size;\
        for(i=0; i<h; i+=2){\
            uint64_t a= LD64(pixels  );\
            uint64_t b= LD64(pixels+1);\
            l1=  (a&0x0303030303030303ULL)\
               + (b&0x0303030303030303ULL);\
            h1= ((a&0xFCFCFCFCFCFCFCFCULL)>>2)\
              + ((b&0xFCFCFCFCFCFCFCFCULL)>>2);\
            OP(*((uint64_t*)block), h0+h1+(((l0+l1)>>2)&0x0F0F0F0F0F0F0F0FULL));\
            pixels+=line_size;\
            block +=line_size;\
            a= LD64(pixels  );\
            b= LD64(pixels+1);\
            l0=  (a&0x0303030303030303ULL)\
               + (b&0x0303030303030303ULL)\
               + 0x0202020202020202ULL;\
            h0= ((a&0xFCFCFCFCFCFCFCFCULL)>>2)\
              + ((b&0xFCFCFCFCFCFCFCFCULL)>>2);\
            OP(*((uint64_t*)block), h0+h1+(((l0+l1)>>2)&0x0F0F0F0F0F0F0F0FULL));\
            pixels+=line_size;\
            block +=line_size;\
        }\
}\
\
static void OPNAME ## _no_rnd_pixels_xy2_c(uint8_t *block, const uint8_t *pixels, int line_size, int h)\
{\
        int i;\
        const uint64_t a= LD64(pixels  );\
        const uint64_t b= LD64(pixels+1);\
        uint64_t l0=  (a&0x0303030303030303ULL)\
                    + (b&0x0303030303030303ULL)\
                    + 0x0101010101010101ULL;\
        uint64_t h0= ((a&0xFCFCFCFCFCFCFCFCULL)>>2)\
                   + ((b&0xFCFCFCFCFCFCFCFCULL)>>2);\
        uint64_t l1,h1;\
\
        pixels+=line_size;\
        for(i=0; i<h; i+=2){\
            uint64_t a= LD64(pixels  );\
            uint64_t b= LD64(pixels+1);\
            l1=  (a&0x0303030303030303ULL)\
               + (b&0x0303030303030303ULL);\
            h1= ((a&0xFCFCFCFCFCFCFCFCULL)>>2)\
              + ((b&0xFCFCFCFCFCFCFCFCULL)>>2);\
            OP(*((uint64_t*)block), h0+h1+(((l0+l1)>>2)&0x0F0F0F0F0F0F0F0FULL));\
            pixels+=line_size;\
            block +=line_size;\
            a= LD64(pixels  );\
            b= LD64(pixels+1);\
            l0=  (a&0x0303030303030303ULL)\
               + (b&0x0303030303030303ULL)\
               + 0x0101010101010101ULL;\
            h0= ((a&0xFCFCFCFCFCFCFCFCULL)>>2)\
              + ((b&0xFCFCFCFCFCFCFCFCULL)>>2);\
            OP(*((uint64_t*)block), h0+h1+(((l0+l1)>>2)&0x0F0F0F0F0F0F0F0FULL));\
            pixels+=line_size;\
            block +=line_size;\
        }\
}\
\
CALL_2X_PIXELS(OPNAME ## _pixels16_c    , OPNAME ## _pixels_c    , 8)\
CALL_2X_PIXELS(OPNAME ## _pixels16_x2_c , OPNAME ## _pixels_x2_c , 8)\
CALL_2X_PIXELS(OPNAME ## _pixels16_y2_c , OPNAME ## _pixels_y2_c , 8)\
CALL_2X_PIXELS(OPNAME ## _pixels16_xy2_c, OPNAME ## _pixels_xy2_c, 8)\
CALL_2X_PIXELS(OPNAME ## _no_rnd_pixels16_x2_c , OPNAME ## _no_rnd_pixels_x2_c , 8)\
CALL_2X_PIXELS(OPNAME ## _no_rnd_pixels16_y2_c , OPNAME ## _no_rnd_pixels_y2_c , 8)\
CALL_2X_PIXELS(OPNAME ## _no_rnd_pixels16_xy2_c, OPNAME ## _no_rnd_pixels_xy2_c, 8)

#define op_avg(a, b) a = ( ((a)|(b)) - ((((a)^(b))&0xFEFEFEFEFEFEFEFEULL)>>1) )
#else // 64 bit variant

#define PIXOP2(OPNAME, OP) \
static void OPNAME ## _pixels8_c(uint8_t *block, const uint8_t *pixels, int line_size, int h){\
    int i;\
    for(i=0; i<h; i++){\
        OP(*((uint32_t*)(block  )), LD32(pixels  ));\
        OP(*((uint32_t*)(block+4)), LD32(pixels+4));\
        pixels+=line_size;\
        block +=line_size;\
    }\
}\
static inline void OPNAME ## _no_rnd_pixels8_c(uint8_t *block, const uint8_t *pixels, int line_size, int h){\
    OPNAME ## _pixels8_c(block, pixels, line_size, h);\
}\
\
static inline void OPNAME ## _no_rnd_pixels8_l2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, \
                                                int src_stride1, int src_stride2, int h){\
    int i;\
    for(i=0; i<h; i++){\
        uint32_t a,b;\
        a= LD32(&src1[i*src_stride1  ]);\
        b= LD32(&src2[i*src_stride2  ]);\
        OP(*((uint32_t*)&dst[i*dst_stride  ]), (a&b) + (((a^b)&0xFEFEFEFEUL)>>1));\
        a= LD32(&src1[i*src_stride1+4]);\
        b= LD32(&src2[i*src_stride2+4]);\
        OP(*((uint32_t*)&dst[i*dst_stride+4]), (a&b) + (((a^b)&0xFEFEFEFEUL)>>1));\
    }\
}\
\
static inline void OPNAME ## _pixels8_l2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, \
                                                int src_stride1, int src_stride2, int h){\
    int i;\
    for(i=0; i<h; i++){\
        uint32_t a,b;\
        a= LD32(&src1[i*src_stride1  ]);\
        b= LD32(&src2[i*src_stride2  ]);\
        OP(*((uint32_t*)&dst[i*dst_stride  ]), (a|b) - (((a^b)&0xFEFEFEFEUL)>>1));\
        a= LD32(&src1[i*src_stride1+4]);\
        b= LD32(&src2[i*src_stride2+4]);\
        OP(*((uint32_t*)&dst[i*dst_stride+4]), (a|b) - (((a^b)&0xFEFEFEFEUL)>>1));\
    }\
}\
\
static inline void OPNAME ## _pixels16_l2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, \
                                                int src_stride1, int src_stride2, int h){\
    OPNAME ## _pixels8_l2(dst  , src1  , src2  , dst_stride, src_stride1, src_stride2, h);\
    OPNAME ## _pixels8_l2(dst+8, src1+8, src2+8, dst_stride, src_stride1, src_stride2, h);\
}\
\
static inline void OPNAME ## _no_rnd_pixels16_l2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, int dst_stride, \
                                                int src_stride1, int src_stride2, int h){\
    OPNAME ## _no_rnd_pixels8_l2(dst  , src1  , src2  , dst_stride, src_stride1, src_stride2, h);\
    OPNAME ## _no_rnd_pixels8_l2(dst+8, src1+8, src2+8, dst_stride, src_stride1, src_stride2, h);\
}\
\
static inline void OPNAME ## _no_rnd_pixels8_x2_c(uint8_t *block, const uint8_t *pixels, int line_size, int h){\
    OPNAME ## _no_rnd_pixels8_l2(block, pixels, pixels+1, line_size, line_size, line_size, h);\
}\
\
static inline void OPNAME ## _pixels8_x2_c(uint8_t *block, const uint8_t *pixels, int line_size, int h){\
    OPNAME ## _pixels8_l2(block, pixels, pixels+1, line_size, line_size, line_size, h);\
}\
\
static inline void OPNAME ## _no_rnd_pixels8_y2_c(uint8_t *block, const uint8_t *pixels, int line_size, int h){\
    OPNAME ## _no_rnd_pixels8_l2(block, pixels, pixels+line_size, line_size, line_size, line_size, h);\
}\
\
static inline void OPNAME ## _pixels8_y2_c(uint8_t *block, const uint8_t *pixels, int line_size, int h){\
    OPNAME ## _pixels8_l2(block, pixels, pixels+line_size, line_size, line_size, line_size, h);\
}\
\
static inline void OPNAME ## _pixels8_l4(uint8_t *dst, const uint8_t *src1, uint8_t *src2, uint8_t *src3, uint8_t *src4,\
                 int dst_stride, int src_stride1, int src_stride2,int src_stride3,int src_stride4, int h){\
    int i;\
    for(i=0; i<h; i++){\
        uint32_t a, b, c, d, l0, l1, h0, h1;\
        a= LD32(&src1[i*src_stride1]);\
        b= LD32(&src2[i*src_stride2]);\
        c= LD32(&src3[i*src_stride3]);\
        d= LD32(&src4[i*src_stride4]);\
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
        a= LD32(&src1[i*src_stride1+4]);\
        b= LD32(&src2[i*src_stride2+4]);\
        c= LD32(&src3[i*src_stride3+4]);\
        d= LD32(&src4[i*src_stride4+4]);\
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
static inline void OPNAME ## _no_rnd_pixels8_l4(uint8_t *dst, const uint8_t *src1, uint8_t *src2, uint8_t *src3, uint8_t *src4,\
                 int dst_stride, int src_stride1, int src_stride2,int src_stride3,int src_stride4, int h){\
    int i;\
    for(i=0; i<h; i++){\
        uint32_t a, b, c, d, l0, l1, h0, h1;\
        a= LD32(&src1[i*src_stride1]);\
        b= LD32(&src2[i*src_stride2]);\
        c= LD32(&src3[i*src_stride3]);\
        d= LD32(&src4[i*src_stride4]);\
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
        a= LD32(&src1[i*src_stride1+4]);\
        b= LD32(&src2[i*src_stride2+4]);\
        c= LD32(&src3[i*src_stride3+4]);\
        d= LD32(&src4[i*src_stride4+4]);\
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
static inline void OPNAME ## _pixels16_l4(uint8_t *dst, const uint8_t *src1, uint8_t *src2, uint8_t *src3, uint8_t *src4,\
                 int dst_stride, int src_stride1, int src_stride2,int src_stride3,int src_stride4, int h){\
    OPNAME ## _pixels8_l4(dst  , src1  , src2  , src3  , src4  , dst_stride, src_stride1, src_stride2, src_stride3, src_stride4, h);\
    OPNAME ## _pixels8_l4(dst+8, src1+8, src2+8, src3+8, src4+8, dst_stride, src_stride1, src_stride2, src_stride3, src_stride4, h);\
}\
static inline void OPNAME ## _no_rnd_pixels16_l4(uint8_t *dst, const uint8_t *src1, uint8_t *src2, uint8_t *src3, uint8_t *src4,\
                 int dst_stride, int src_stride1, int src_stride2,int src_stride3,int src_stride4, int h){\
    OPNAME ## _no_rnd_pixels8_l4(dst  , src1  , src2  , src3  , src4  , dst_stride, src_stride1, src_stride2, src_stride3, src_stride4, h);\
    OPNAME ## _no_rnd_pixels8_l4(dst+8, src1+8, src2+8, src3+8, src4+8, dst_stride, src_stride1, src_stride2, src_stride3, src_stride4, h);\
}\
\
static inline void OPNAME ## _pixels8_xy2_c(uint8_t *block, const uint8_t *pixels, int line_size, int h)\
{\
    int j;\
    for(j=0; j<2; j++){\
        int i;\
        const uint32_t a= LD32(pixels  );\
        const uint32_t b= LD32(pixels+1);\
        uint32_t l0=  (a&0x03030303UL)\
                    + (b&0x03030303UL)\
                    + 0x02020202UL;\
        uint32_t h0= ((a&0xFCFCFCFCUL)>>2)\
                   + ((b&0xFCFCFCFCUL)>>2);\
        uint32_t l1,h1;\
\
        pixels+=line_size;\
        for(i=0; i<h; i+=2){\
            uint32_t a= LD32(pixels  );\
            uint32_t b= LD32(pixels+1);\
            l1=  (a&0x03030303UL)\
               + (b&0x03030303UL);\
            h1= ((a&0xFCFCFCFCUL)>>2)\
              + ((b&0xFCFCFCFCUL)>>2);\
            OP(*((uint32_t*)block), h0+h1+(((l0+l1)>>2)&0x0F0F0F0FUL));\
            pixels+=line_size;\
            block +=line_size;\
            a= LD32(pixels  );\
            b= LD32(pixels+1);\
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
static inline void OPNAME ## _no_rnd_pixels8_xy2_c(uint8_t *block, const uint8_t *pixels, int line_size, int h)\
{\
    int j;\
    for(j=0; j<2; j++){\
        int i;\
        const uint32_t a= LD32(pixels  );\
        const uint32_t b= LD32(pixels+1);\
        uint32_t l0=  (a&0x03030303UL)\
                    + (b&0x03030303UL)\
                    + 0x01010101UL;\
        uint32_t h0= ((a&0xFCFCFCFCUL)>>2)\
                   + ((b&0xFCFCFCFCUL)>>2);\
        uint32_t l1,h1;\
\
        pixels+=line_size;\
        for(i=0; i<h; i+=2){\
            uint32_t a= LD32(pixels  );\
            uint32_t b= LD32(pixels+1);\
            l1=  (a&0x03030303UL)\
               + (b&0x03030303UL);\
            h1= ((a&0xFCFCFCFCUL)>>2)\
              + ((b&0xFCFCFCFCUL)>>2);\
            OP(*((uint32_t*)block), h0+h1+(((l0+l1)>>2)&0x0F0F0F0FUL));\
            pixels+=line_size;\
            block +=line_size;\
            a= LD32(pixels  );\
            b= LD32(pixels+1);\
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
CALL_2X_PIXELS(OPNAME ## _pixels16_c  , OPNAME ## _pixels8_c  , 8)\
CALL_2X_PIXELS(OPNAME ## _pixels16_x2_c , OPNAME ## _pixels8_x2_c , 8)\
CALL_2X_PIXELS(OPNAME ## _pixels16_y2_c , OPNAME ## _pixels8_y2_c , 8)\
CALL_2X_PIXELS(OPNAME ## _pixels16_xy2_c, OPNAME ## _pixels8_xy2_c, 8)\
CALL_2X_PIXELS(OPNAME ## _no_rnd_pixels16_c  , OPNAME ## _pixels8_c         , 8)\
CALL_2X_PIXELS(OPNAME ## _no_rnd_pixels16_x2_c , OPNAME ## _no_rnd_pixels8_x2_c , 8)\
CALL_2X_PIXELS(OPNAME ## _no_rnd_pixels16_y2_c , OPNAME ## _no_rnd_pixels8_y2_c , 8)\
CALL_2X_PIXELS(OPNAME ## _no_rnd_pixels16_xy2_c, OPNAME ## _no_rnd_pixels8_xy2_c, 8)\

#define op_avg(a, b) a = ( ((a)|(b)) - ((((a)^(b))&0xFEFEFEFEUL)>>1) )
#endif
#define op_put(a, b) a = b

PIXOP2(avg, op_avg)
PIXOP2(put, op_put)
#undef op_avg
#undef op_put

#define avg2(a,b) ((a+b+1)>>1)
#define avg4(a,b,c,d) ((a+b+c+d+2)>>2)


static void gmc1_c(UINT8 *dst, UINT8 *src, int stride, int h, int x16, int y16, int rounder)
{
    const int A=(16-x16)*(16-y16);
    const int B=(   x16)*(16-y16);
    const int C=(16-x16)*(   y16);
    const int D=(   x16)*(   y16);
    int i;

    for(i=0; i<h; i++)
    {
        dst[0]= (A*src[0] + B*src[1] + C*src[stride+0] + D*src[stride+1] + rounder)>>8;
        dst[1]= (A*src[1] + B*src[2] + C*src[stride+1] + D*src[stride+2] + rounder)>>8;
        dst[2]= (A*src[2] + B*src[3] + C*src[stride+2] + D*src[stride+3] + rounder)>>8;
        dst[3]= (A*src[3] + B*src[4] + C*src[stride+3] + D*src[stride+4] + rounder)>>8;
        dst[4]= (A*src[4] + B*src[5] + C*src[stride+4] + D*src[stride+5] + rounder)>>8;
        dst[5]= (A*src[5] + B*src[6] + C*src[stride+5] + D*src[stride+6] + rounder)>>8;
        dst[6]= (A*src[6] + B*src[7] + C*src[stride+6] + D*src[stride+7] + rounder)>>8;
        dst[7]= (A*src[7] + B*src[8] + C*src[stride+7] + D*src[stride+8] + rounder)>>8;
        dst+= stride;
        src+= stride;
    }
}

static void gmc_c(UINT8 *dst, UINT8 *src, int stride, int h, int ox, int oy, 
                  int dxx, int dxy, int dyx, int dyy, int shift, int r, int width, int height)
{
    int y, vx, vy;
    const int s= 1<<shift;
    
    width--;
    height--;

    for(y=0; y<h; y++){
        int x;

        vx= ox;
        vy= oy;
        for(x=0; x<8; x++){ //XXX FIXME optimize
            int src_x, src_y, frac_x, frac_y, index;

            src_x= vx>>16;
            src_y= vy>>16;
            frac_x= src_x&(s-1);
            frac_y= src_y&(s-1);
            src_x>>=shift;
            src_y>>=shift;
  
            if((unsigned)src_x < width){
                if((unsigned)src_y < height){
                    index= src_x + src_y*stride;
                    dst[y*stride + x]= (  (  src[index         ]*(s-frac_x)
                                           + src[index       +1]*   frac_x )*(s-frac_y)
                                        + (  src[index+stride  ]*(s-frac_x)
                                           + src[index+stride+1]*   frac_x )*   frac_y
                                        + r)>>(shift*2);
                }else{
                    index= src_x + clip(src_y, 0, height)*stride;                    
                    dst[y*stride + x]= ( (  src[index         ]*(s-frac_x) 
                                          + src[index       +1]*   frac_x )*s
                                        + r)>>(shift*2);
                }
            }else{
                if((unsigned)src_y < height){
                    index= clip(src_x, 0, width) + src_y*stride;                    
                    dst[y*stride + x]= (  (  src[index         ]*(s-frac_y) 
                                           + src[index+stride  ]*   frac_y )*s
                                        + r)>>(shift*2);
                }else{
                    index= clip(src_x, 0, width) + clip(src_y, 0, height)*stride;                    
                    dst[y*stride + x]=    src[index         ];
                }
            }
            
            vx+= dxx;
            vy+= dyx;
        }
        ox += dxy;
        oy += dyy;
    }
}

static inline void copy_block17(UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int h)
{
    int i;
    for(i=0; i<h; i++)
    {
        ST32(dst   , LD32(src   ));
        ST32(dst+4 , LD32(src+4 ));
        ST32(dst+8 , LD32(src+8 ));
        ST32(dst+12, LD32(src+12));
        dst[16]= src[16];
        dst+=dstStride;
        src+=srcStride;
    }
}

static inline void copy_block9(UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int h)
{
    int i;
    for(i=0; i<h; i++)
    {
        ST32(dst   , LD32(src   ));
        ST32(dst+4 , LD32(src+4 ));
        dst[8]= src[8];
        dst+=dstStride;
        src+=srcStride;
    }
}

#define QPEL_MC(r, OPNAME, RND, OP) \
static void OPNAME ## mpeg4_qpel8_h_lowpass(UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int h){\
    UINT8 *cm = cropTbl + MAX_NEG_CROP;\
    int i;\
    for(i=0; i<h; i++)\
    {\
        OP(dst[0], (src[0]+src[1])*20 - (src[0]+src[2])*6 + (src[1]+src[3])*3 - (src[2]+src[4]));\
        OP(dst[1], (src[1]+src[2])*20 - (src[0]+src[3])*6 + (src[0]+src[4])*3 - (src[1]+src[5]));\
        OP(dst[2], (src[2]+src[3])*20 - (src[1]+src[4])*6 + (src[0]+src[5])*3 - (src[0]+src[6]));\
        OP(dst[3], (src[3]+src[4])*20 - (src[2]+src[5])*6 + (src[1]+src[6])*3 - (src[0]+src[7]));\
        OP(dst[4], (src[4]+src[5])*20 - (src[3]+src[6])*6 + (src[2]+src[7])*3 - (src[1]+src[8]));\
        OP(dst[5], (src[5]+src[6])*20 - (src[4]+src[7])*6 + (src[3]+src[8])*3 - (src[2]+src[8]));\
        OP(dst[6], (src[6]+src[7])*20 - (src[5]+src[8])*6 + (src[4]+src[8])*3 - (src[3]+src[7]));\
        OP(dst[7], (src[7]+src[8])*20 - (src[6]+src[8])*6 + (src[5]+src[7])*3 - (src[4]+src[6]));\
        dst+=dstStride;\
        src+=srcStride;\
    }\
}\
\
static void OPNAME ## mpeg4_qpel8_v_lowpass(UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int w){\
    UINT8 *cm = cropTbl + MAX_NEG_CROP;\
    int i;\
    for(i=0; i<w; i++)\
    {\
        const int src0= src[0*srcStride];\
        const int src1= src[1*srcStride];\
        const int src2= src[2*srcStride];\
        const int src3= src[3*srcStride];\
        const int src4= src[4*srcStride];\
        const int src5= src[5*srcStride];\
        const int src6= src[6*srcStride];\
        const int src7= src[7*srcStride];\
        const int src8= src[8*srcStride];\
        OP(dst[0*dstStride], (src0+src1)*20 - (src0+src2)*6 + (src1+src3)*3 - (src2+src4));\
        OP(dst[1*dstStride], (src1+src2)*20 - (src0+src3)*6 + (src0+src4)*3 - (src1+src5));\
        OP(dst[2*dstStride], (src2+src3)*20 - (src1+src4)*6 + (src0+src5)*3 - (src0+src6));\
        OP(dst[3*dstStride], (src3+src4)*20 - (src2+src5)*6 + (src1+src6)*3 - (src0+src7));\
        OP(dst[4*dstStride], (src4+src5)*20 - (src3+src6)*6 + (src2+src7)*3 - (src1+src8));\
        OP(dst[5*dstStride], (src5+src6)*20 - (src4+src7)*6 + (src3+src8)*3 - (src2+src8));\
        OP(dst[6*dstStride], (src6+src7)*20 - (src5+src8)*6 + (src4+src8)*3 - (src3+src7));\
        OP(dst[7*dstStride], (src7+src8)*20 - (src6+src8)*6 + (src5+src7)*3 - (src4+src6));\
        dst++;\
        src++;\
    }\
}\
\
static void OPNAME ## mpeg4_qpel16_h_lowpass(UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int h){\
    UINT8 *cm = cropTbl + MAX_NEG_CROP;\
    int i;\
    for(i=0; i<h; i++)\
    {\
        OP(dst[ 0], (src[ 0]+src[ 1])*20 - (src[ 0]+src[ 2])*6 + (src[ 1]+src[ 3])*3 - (src[ 2]+src[ 4]));\
        OP(dst[ 1], (src[ 1]+src[ 2])*20 - (src[ 0]+src[ 3])*6 + (src[ 0]+src[ 4])*3 - (src[ 1]+src[ 5]));\
        OP(dst[ 2], (src[ 2]+src[ 3])*20 - (src[ 1]+src[ 4])*6 + (src[ 0]+src[ 5])*3 - (src[ 0]+src[ 6]));\
        OP(dst[ 3], (src[ 3]+src[ 4])*20 - (src[ 2]+src[ 5])*6 + (src[ 1]+src[ 6])*3 - (src[ 0]+src[ 7]));\
        OP(dst[ 4], (src[ 4]+src[ 5])*20 - (src[ 3]+src[ 6])*6 + (src[ 2]+src[ 7])*3 - (src[ 1]+src[ 8]));\
        OP(dst[ 5], (src[ 5]+src[ 6])*20 - (src[ 4]+src[ 7])*6 + (src[ 3]+src[ 8])*3 - (src[ 2]+src[ 9]));\
        OP(dst[ 6], (src[ 6]+src[ 7])*20 - (src[ 5]+src[ 8])*6 + (src[ 4]+src[ 9])*3 - (src[ 3]+src[10]));\
        OP(dst[ 7], (src[ 7]+src[ 8])*20 - (src[ 6]+src[ 9])*6 + (src[ 5]+src[10])*3 - (src[ 4]+src[11]));\
        OP(dst[ 8], (src[ 8]+src[ 9])*20 - (src[ 7]+src[10])*6 + (src[ 6]+src[11])*3 - (src[ 5]+src[12]));\
        OP(dst[ 9], (src[ 9]+src[10])*20 - (src[ 8]+src[11])*6 + (src[ 7]+src[12])*3 - (src[ 6]+src[13]));\
        OP(dst[10], (src[10]+src[11])*20 - (src[ 9]+src[12])*6 + (src[ 8]+src[13])*3 - (src[ 7]+src[14]));\
        OP(dst[11], (src[11]+src[12])*20 - (src[10]+src[13])*6 + (src[ 9]+src[14])*3 - (src[ 8]+src[15]));\
        OP(dst[12], (src[12]+src[13])*20 - (src[11]+src[14])*6 + (src[10]+src[15])*3 - (src[ 9]+src[16]));\
        OP(dst[13], (src[13]+src[14])*20 - (src[12]+src[15])*6 + (src[11]+src[16])*3 - (src[10]+src[16]));\
        OP(dst[14], (src[14]+src[15])*20 - (src[13]+src[16])*6 + (src[12]+src[16])*3 - (src[11]+src[15]));\
        OP(dst[15], (src[15]+src[16])*20 - (src[14]+src[16])*6 + (src[13]+src[15])*3 - (src[12]+src[14]));\
        dst+=dstStride;\
        src+=srcStride;\
    }\
}\
\
static void OPNAME ## mpeg4_qpel16_v_lowpass(UINT8 *dst, UINT8 *src, int dstStride, int srcStride, int w){\
    UINT8 *cm = cropTbl + MAX_NEG_CROP;\
    int i;\
    for(i=0; i<w; i++)\
    {\
        const int src0= src[0*srcStride];\
        const int src1= src[1*srcStride];\
        const int src2= src[2*srcStride];\
        const int src3= src[3*srcStride];\
        const int src4= src[4*srcStride];\
        const int src5= src[5*srcStride];\
        const int src6= src[6*srcStride];\
        const int src7= src[7*srcStride];\
        const int src8= src[8*srcStride];\
        const int src9= src[9*srcStride];\
        const int src10= src[10*srcStride];\
        const int src11= src[11*srcStride];\
        const int src12= src[12*srcStride];\
        const int src13= src[13*srcStride];\
        const int src14= src[14*srcStride];\
        const int src15= src[15*srcStride];\
        const int src16= src[16*srcStride];\
        OP(dst[ 0*dstStride], (src0 +src1 )*20 - (src0 +src2 )*6 + (src1 +src3 )*3 - (src2 +src4 ));\
        OP(dst[ 1*dstStride], (src1 +src2 )*20 - (src0 +src3 )*6 + (src0 +src4 )*3 - (src1 +src5 ));\
        OP(dst[ 2*dstStride], (src2 +src3 )*20 - (src1 +src4 )*6 + (src0 +src5 )*3 - (src0 +src6 ));\
        OP(dst[ 3*dstStride], (src3 +src4 )*20 - (src2 +src5 )*6 + (src1 +src6 )*3 - (src0 +src7 ));\
        OP(dst[ 4*dstStride], (src4 +src5 )*20 - (src3 +src6 )*6 + (src2 +src7 )*3 - (src1 +src8 ));\
        OP(dst[ 5*dstStride], (src5 +src6 )*20 - (src4 +src7 )*6 + (src3 +src8 )*3 - (src2 +src9 ));\
        OP(dst[ 6*dstStride], (src6 +src7 )*20 - (src5 +src8 )*6 + (src4 +src9 )*3 - (src3 +src10));\
        OP(dst[ 7*dstStride], (src7 +src8 )*20 - (src6 +src9 )*6 + (src5 +src10)*3 - (src4 +src11));\
        OP(dst[ 8*dstStride], (src8 +src9 )*20 - (src7 +src10)*6 + (src6 +src11)*3 - (src5 +src12));\
        OP(dst[ 9*dstStride], (src9 +src10)*20 - (src8 +src11)*6 + (src7 +src12)*3 - (src6 +src13));\
        OP(dst[10*dstStride], (src10+src11)*20 - (src9 +src12)*6 + (src8 +src13)*3 - (src7 +src14));\
        OP(dst[11*dstStride], (src11+src12)*20 - (src10+src13)*6 + (src9 +src14)*3 - (src8 +src15));\
        OP(dst[12*dstStride], (src12+src13)*20 - (src11+src14)*6 + (src10+src15)*3 - (src9 +src16));\
        OP(dst[13*dstStride], (src13+src14)*20 - (src12+src15)*6 + (src11+src16)*3 - (src10+src16));\
        OP(dst[14*dstStride], (src14+src15)*20 - (src13+src16)*6 + (src12+src16)*3 - (src11+src15));\
        OP(dst[15*dstStride], (src15+src16)*20 - (src14+src16)*6 + (src13+src15)*3 - (src12+src14));\
        dst++;\
        src++;\
    }\
}\
\
static void OPNAME ## qpel8_mc00_c (UINT8 *dst, UINT8 *src, int stride){\
    OPNAME ## pixels8_c(dst, src, stride, 8);\
}\
\
static void OPNAME ## qpel8_mc10_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 half[64];\
    put ## RND ## mpeg4_qpel8_h_lowpass(half, src, 8, stride, 8);\
    OPNAME ## pixels8_l2(dst, src, half, stride, stride, 8, 8);\
}\
\
static void OPNAME ## qpel8_mc20_c(UINT8 *dst, UINT8 *src, int stride){\
    OPNAME ## mpeg4_qpel8_h_lowpass(dst, src, stride, stride, 8);\
}\
\
static void OPNAME ## qpel8_mc30_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 half[64];\
    put ## RND ## mpeg4_qpel8_h_lowpass(half, src, 8, stride, 8);\
    OPNAME ## pixels8_l2(dst, src+1, half, stride, stride, 8, 8);\
}\
\
static void OPNAME ## qpel8_mc01_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 full[16*9];\
    UINT8 half[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(half, full, 8, 16, 8);\
    OPNAME ## pixels8_l2(dst, full, half, stride, 16, 8, 8);\
}\
\
static void OPNAME ## qpel8_mc02_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 full[16*9];\
    copy_block9(full, src, 16, stride, 9);\
    OPNAME ## mpeg4_qpel8_v_lowpass(dst, full, stride, 16, 8);\
}\
\
static void OPNAME ## qpel8_mc03_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 full[16*9];\
    UINT8 half[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(half, full, 8, 16, 8);\
    OPNAME ## pixels8_l2(dst, full+16, half, stride, 16, 8, 8);\
}\
static void OPNAME ## qpel8_mc11_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 full[16*9];\
    UINT8 halfH[72];\
    UINT8 halfV[64];\
    UINT8 halfHV[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full, 8, 16, 8);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8, 8);\
    OPNAME ## pixels8_l4(dst, full, halfH, halfV, halfHV, stride, 16, 8, 8, 8, 8);\
}\
static void OPNAME ## qpel8_mc31_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 full[16*9];\
    UINT8 halfH[72];\
    UINT8 halfV[64];\
    UINT8 halfHV[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full+1, 8, 16, 8);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8, 8);\
    OPNAME ## pixels8_l4(dst, full+1, halfH, halfV, halfHV, stride, 16, 8, 8, 8, 8);\
}\
static void OPNAME ## qpel8_mc13_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 full[16*9];\
    UINT8 halfH[72];\
    UINT8 halfV[64];\
    UINT8 halfHV[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full, 8, 16, 8);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8, 8);\
    OPNAME ## pixels8_l4(dst, full+16, halfH+8, halfV, halfHV, stride, 16, 8, 8, 8, 8);\
}\
static void OPNAME ## qpel8_mc33_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 full[16*9];\
    UINT8 halfH[72];\
    UINT8 halfV[64];\
    UINT8 halfHV[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full  , 8, 16, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full+1, 8, 16, 8);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8, 8);\
    OPNAME ## pixels8_l4(dst, full+17, halfH+8, halfV, halfHV, stride, 16, 8, 8, 8, 8);\
}\
static void OPNAME ## qpel8_mc21_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 halfH[72];\
    UINT8 halfHV[64];\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, src, 8, stride, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8, 8);\
    OPNAME ## pixels8_l2(dst, halfH, halfHV, stride, 8, 8, 8);\
}\
static void OPNAME ## qpel8_mc23_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 halfH[72];\
    UINT8 halfHV[64];\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, src, 8, stride, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8, 8);\
    OPNAME ## pixels8_l2(dst, halfH+8, halfHV, stride, 8, 8, 8);\
}\
static void OPNAME ## qpel8_mc12_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 full[16*9];\
    UINT8 halfH[72];\
    UINT8 halfV[64];\
    UINT8 halfHV[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full, 8, 16, 8);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8, 8);\
    OPNAME ## pixels8_l2(dst, halfV, halfHV, stride, 8, 8, 8);\
}\
static void OPNAME ## qpel8_mc32_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 full[16*9];\
    UINT8 halfH[72];\
    UINT8 halfV[64];\
    UINT8 halfHV[64];\
    copy_block9(full, src, 16, stride, 9);\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, full, 8, 16, 9);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfV, full+1, 8, 16, 8);\
    put ## RND ## mpeg4_qpel8_v_lowpass(halfHV, halfH, 8, 8, 8);\
    OPNAME ## pixels8_l2(dst, halfV, halfHV, stride, 8, 8, 8);\
}\
static void OPNAME ## qpel8_mc22_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 halfH[72];\
    put ## RND ## mpeg4_qpel8_h_lowpass(halfH, src, 8, stride, 9);\
    OPNAME ## mpeg4_qpel8_v_lowpass(dst, halfH, stride, 8, 8);\
}\
static void OPNAME ## qpel16_mc00_c (UINT8 *dst, UINT8 *src, int stride){\
    OPNAME ## pixels16_c(dst, src, stride, 16);\
}\
\
static void OPNAME ## qpel16_mc10_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 half[256];\
    put ## RND ## mpeg4_qpel16_h_lowpass(half, src, 16, stride, 16);\
    OPNAME ## pixels16_l2(dst, src, half, stride, stride, 16, 16);\
}\
\
static void OPNAME ## qpel16_mc20_c(UINT8 *dst, UINT8 *src, int stride){\
    OPNAME ## mpeg4_qpel16_h_lowpass(dst, src, stride, stride, 16);\
}\
\
static void OPNAME ## qpel16_mc30_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 half[256];\
    put ## RND ## mpeg4_qpel16_h_lowpass(half, src, 16, stride, 16);\
    OPNAME ## pixels16_l2(dst, src+1, half, stride, stride, 16, 16);\
}\
\
static void OPNAME ## qpel16_mc01_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 full[24*17];\
    UINT8 half[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(half, full, 16, 24, 16);\
    OPNAME ## pixels16_l2(dst, full, half, stride, 24, 16, 16);\
}\
\
static void OPNAME ## qpel16_mc02_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 full[24*17];\
    copy_block17(full, src, 24, stride, 17);\
    OPNAME ## mpeg4_qpel16_v_lowpass(dst, full, stride, 24, 16);\
}\
\
static void OPNAME ## qpel16_mc03_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 full[24*17];\
    UINT8 half[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(half, full, 16, 24, 16);\
    OPNAME ## pixels16_l2(dst, full+24, half, stride, 24, 16, 16);\
}\
static void OPNAME ## qpel16_mc11_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 full[24*17];\
    UINT8 halfH[272];\
    UINT8 halfV[256];\
    UINT8 halfHV[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full, 16, 24, 16);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16, 16);\
    OPNAME ## pixels16_l4(dst, full, halfH, halfV, halfHV, stride, 24, 16, 16, 16, 16);\
}\
static void OPNAME ## qpel16_mc31_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 full[24*17];\
    UINT8 halfH[272];\
    UINT8 halfV[256];\
    UINT8 halfHV[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full+1, 16, 24, 16);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16, 16);\
    OPNAME ## pixels16_l4(dst, full+1, halfH, halfV, halfHV, stride, 24, 16, 16, 16, 16);\
}\
static void OPNAME ## qpel16_mc13_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 full[24*17];\
    UINT8 halfH[272];\
    UINT8 halfV[256];\
    UINT8 halfHV[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full, 16, 24, 16);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16, 16);\
    OPNAME ## pixels16_l4(dst, full+24, halfH+16, halfV, halfHV, stride, 24, 16, 16, 16, 16);\
}\
static void OPNAME ## qpel16_mc33_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 full[24*17];\
    UINT8 halfH[272];\
    UINT8 halfV[256];\
    UINT8 halfHV[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full  , 16, 24, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full+1, 16, 24, 16);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16, 16);\
    OPNAME ## pixels16_l4(dst, full+25, halfH+16, halfV, halfHV, stride, 24, 16, 16, 16, 16);\
}\
static void OPNAME ## qpel16_mc21_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 halfH[272];\
    UINT8 halfHV[256];\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, src, 16, stride, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16, 16);\
    OPNAME ## pixels16_l2(dst, halfH, halfHV, stride, 16, 16, 16);\
}\
static void OPNAME ## qpel16_mc23_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 halfH[272];\
    UINT8 halfHV[256];\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, src, 16, stride, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16, 16);\
    OPNAME ## pixels16_l2(dst, halfH+16, halfHV, stride, 16, 16, 16);\
}\
static void OPNAME ## qpel16_mc12_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 full[24*17];\
    UINT8 halfH[272];\
    UINT8 halfV[256];\
    UINT8 halfHV[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full, 16, 24, 16);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16, 16);\
    OPNAME ## pixels16_l2(dst, halfV, halfHV, stride, 16, 16, 16);\
}\
static void OPNAME ## qpel16_mc32_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 full[24*17];\
    UINT8 halfH[272];\
    UINT8 halfV[256];\
    UINT8 halfHV[256];\
    copy_block17(full, src, 24, stride, 17);\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, full, 16, 24, 17);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfV, full+1, 16, 24, 16);\
    put ## RND ## mpeg4_qpel16_v_lowpass(halfHV, halfH, 16, 16, 16);\
    OPNAME ## pixels16_l2(dst, halfV, halfHV, stride, 16, 16, 16);\
}\
static void OPNAME ## qpel16_mc22_c(UINT8 *dst, UINT8 *src, int stride){\
    UINT8 halfH[272];\
    put ## RND ## mpeg4_qpel16_h_lowpass(halfH, src, 16, stride, 17);\
    OPNAME ## mpeg4_qpel16_v_lowpass(dst, halfH, stride, 16, 16);\
}

#define op_avg(a, b) a = (((a)+cm[((b) + 16)>>5]+1)>>1)
#define op_avg_no_rnd(a, b) a = (((a)+cm[((b) + 15)>>5])>>1)
#define op_put(a, b) a = cm[((b) + 16)>>5]
#define op_put_no_rnd(a, b) a = cm[((b) + 15)>>5]

QPEL_MC(0, put_       , _       , op_put)
QPEL_MC(1, put_no_rnd_, _no_rnd_, op_put_no_rnd)
QPEL_MC(0, avg_       , _       , op_avg)
//QPEL_MC(1, avg_no_rnd , _       , op_avg)
#undef op_avg
#undef op_avg_no_rnd
#undef op_put
#undef op_put_no_rnd

static int pix_abs16x16_c(UINT8 *pix1, UINT8 *pix2, int line_size)
{
    int s, i;

    s = 0;
    for(i=0;i<16;i++) {
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

static int pix_abs16x16_x2_c(UINT8 *pix1, UINT8 *pix2, int line_size)
{
    int s, i;

    s = 0;
    for(i=0;i<16;i++) {
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

static int pix_abs16x16_y2_c(UINT8 *pix1, UINT8 *pix2, int line_size)
{
    int s, i;
    UINT8 *pix3 = pix2 + line_size;

    s = 0;
    for(i=0;i<16;i++) {
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

static int pix_abs16x16_xy2_c(UINT8 *pix1, UINT8 *pix2, int line_size)
{
    int s, i;
    UINT8 *pix3 = pix2 + line_size;

    s = 0;
    for(i=0;i<16;i++) {
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

static int pix_abs8x8_c(UINT8 *pix1, UINT8 *pix2, int line_size)
{
    int s, i;

    s = 0;
    for(i=0;i<8;i++) {
        s += abs(pix1[0] - pix2[0]);
        s += abs(pix1[1] - pix2[1]);
        s += abs(pix1[2] - pix2[2]);
        s += abs(pix1[3] - pix2[3]);
        s += abs(pix1[4] - pix2[4]);
        s += abs(pix1[5] - pix2[5]);
        s += abs(pix1[6] - pix2[6]);
        s += abs(pix1[7] - pix2[7]);
        pix1 += line_size;
        pix2 += line_size;
    }
    return s;
}

static int pix_abs8x8_x2_c(UINT8 *pix1, UINT8 *pix2, int line_size)
{
    int s, i;

    s = 0;
    for(i=0;i<8;i++) {
        s += abs(pix1[0] - avg2(pix2[0], pix2[1]));
        s += abs(pix1[1] - avg2(pix2[1], pix2[2]));
        s += abs(pix1[2] - avg2(pix2[2], pix2[3]));
        s += abs(pix1[3] - avg2(pix2[3], pix2[4]));
        s += abs(pix1[4] - avg2(pix2[4], pix2[5]));
        s += abs(pix1[5] - avg2(pix2[5], pix2[6]));
        s += abs(pix1[6] - avg2(pix2[6], pix2[7]));
        s += abs(pix1[7] - avg2(pix2[7], pix2[8]));
        pix1 += line_size;
        pix2 += line_size;
    }
    return s;
}

static int pix_abs8x8_y2_c(UINT8 *pix1, UINT8 *pix2, int line_size)
{
    int s, i;
    UINT8 *pix3 = pix2 + line_size;

    s = 0;
    for(i=0;i<8;i++) {
        s += abs(pix1[0] - avg2(pix2[0], pix3[0]));
        s += abs(pix1[1] - avg2(pix2[1], pix3[1]));
        s += abs(pix1[2] - avg2(pix2[2], pix3[2]));
        s += abs(pix1[3] - avg2(pix2[3], pix3[3]));
        s += abs(pix1[4] - avg2(pix2[4], pix3[4]));
        s += abs(pix1[5] - avg2(pix2[5], pix3[5]));
        s += abs(pix1[6] - avg2(pix2[6], pix3[6]));
        s += abs(pix1[7] - avg2(pix2[7], pix3[7]));
        pix1 += line_size;
        pix2 += line_size;
        pix3 += line_size;
    }
    return s;
}

static int pix_abs8x8_xy2_c(UINT8 *pix1, UINT8 *pix2, int line_size)
{
    int s, i;
    UINT8 *pix3 = pix2 + line_size;

    s = 0;
    for(i=0;i<8;i++) {
        s += abs(pix1[0] - avg4(pix2[0], pix2[1], pix3[0], pix3[1]));
        s += abs(pix1[1] - avg4(pix2[1], pix2[2], pix3[1], pix3[2]));
        s += abs(pix1[2] - avg4(pix2[2], pix2[3], pix3[2], pix3[3]));
        s += abs(pix1[3] - avg4(pix2[3], pix2[4], pix3[3], pix3[4]));
        s += abs(pix1[4] - avg4(pix2[4], pix2[5], pix3[4], pix3[5]));
        s += abs(pix1[5] - avg4(pix2[5], pix2[6], pix3[5], pix3[6]));
        s += abs(pix1[6] - avg4(pix2[6], pix2[7], pix3[6], pix3[7]));
        s += abs(pix1[7] - avg4(pix2[7], pix2[8], pix3[7], pix3[8]));
        pix1 += line_size;
        pix2 += line_size;
        pix3 += line_size;
    }
    return s;
}

void ff_block_permute(INT16 *block, UINT8 *permutation, const UINT8 *scantable, int last)
{
    int i;
    INT16 temp[64];
    
    if(last<=0) return;
    //if(permutation[1]==1) return; //FIXME its ok but not clean and might fail for some perms

    for(i=0; i<=last; i++){
        const int j= scantable[i];
        temp[j]= block[j];
        block[j]=0;
    }
    
    for(i=0; i<=last; i++){
        const int j= scantable[i];
        const int perm_j= permutation[j];
        block[perm_j]= temp[j];
    }
}

static void clear_blocks_c(DCTELEM *blocks)
{
    memset(blocks, 0, sizeof(DCTELEM)*6*64);
}

static void add_bytes_c(uint8_t *dst, uint8_t *src, int w){
    int i;
    for(i=0; i+7<w; i++){
        dst[i+0] += src[i+0];
        dst[i+1] += src[i+1];
        dst[i+2] += src[i+2];
        dst[i+3] += src[i+3];
        dst[i+4] += src[i+4];
        dst[i+5] += src[i+5];
        dst[i+6] += src[i+6];
        dst[i+7] += src[i+7];
    }
    for(; i<w; i++)
        dst[i+0] += src[i+0];
}

static void diff_bytes_c(uint8_t *dst, uint8_t *src1, uint8_t *src2, int w){
    int i;
    for(i=0; i+7<w; i++){
        dst[i+0] = src1[i+0]-src2[i+0];
        dst[i+1] = src1[i+1]-src2[i+1];
        dst[i+2] = src1[i+2]-src2[i+2];
        dst[i+3] = src1[i+3]-src2[i+3];
        dst[i+4] = src1[i+4]-src2[i+4];
        dst[i+5] = src1[i+5]-src2[i+5];
        dst[i+6] = src1[i+6]-src2[i+6];
        dst[i+7] = src1[i+7]-src2[i+7];
    }
    for(; i<w; i++)
        dst[i+0] = src1[i+0]-src2[i+0];
}

void dsputil_init(DSPContext* c, unsigned mask)
{
    static int init_done = 0;
    int i;

    if (!init_done) {
	for(i=0;i<256;i++) cropTbl[i + MAX_NEG_CROP] = i;
	for(i=0;i<MAX_NEG_CROP;i++) {
	    cropTbl[i] = 0;
	    cropTbl[i + MAX_NEG_CROP + 256] = 255;
	}

	for(i=0;i<512;i++) {
	    squareTbl[i] = (i - 256) * (i - 256);
	}

	for(i=0; i<64; i++) inv_zigzag_direct16[ff_zigzag_direct[i]]= i+1;

	init_done = 1;
    }

    c->get_pixels = get_pixels_c;
    c->diff_pixels = diff_pixels_c;
    c->put_pixels_clamped = put_pixels_clamped_c;
    c->add_pixels_clamped = add_pixels_clamped_c;
    c->gmc1 = gmc1_c;
    c->gmc = gmc_c;
    c->clear_blocks = clear_blocks_c;
    c->pix_sum = pix_sum_c;
    c->pix_norm1 = pix_norm1_c;
    c->pix_norm = pix_norm_c;

    /* TODO [0] 16  [1] 8 */
    c->pix_abs16x16     = pix_abs16x16_c;
    c->pix_abs16x16_x2  = pix_abs16x16_x2_c;
    c->pix_abs16x16_y2  = pix_abs16x16_y2_c;
    c->pix_abs16x16_xy2 = pix_abs16x16_xy2_c;
    c->pix_abs8x8     = pix_abs8x8_c;
    c->pix_abs8x8_x2  = pix_abs8x8_x2_c;
    c->pix_abs8x8_y2  = pix_abs8x8_y2_c;
    c->pix_abs8x8_xy2 = pix_abs8x8_xy2_c;

#define dspfunc(PFX, IDX, NUM) \
    c->PFX ## _pixels_tab[IDX][0] = PFX ## _pixels ## NUM ## _c;     \
    c->PFX ## _pixels_tab[IDX][1] = PFX ## _pixels ## NUM ## _x2_c;  \
    c->PFX ## _pixels_tab[IDX][2] = PFX ## _pixels ## NUM ## _y2_c;  \
    c->PFX ## _pixels_tab[IDX][3] = PFX ## _pixels ## NUM ## _xy2_c

    dspfunc(put, 0, 16);
    dspfunc(put_no_rnd, 0, 16);
    dspfunc(put, 1, 8);
    dspfunc(put_no_rnd, 1, 8);

    dspfunc(avg, 0, 16);
    dspfunc(avg_no_rnd, 0, 16);
    dspfunc(avg, 1, 8);
    dspfunc(avg_no_rnd, 1, 8);
#undef dspfunc

#define dspfunc(PFX, IDX, NUM) \
    c->PFX ## _pixels_tab[IDX][ 0] = PFX ## NUM ## _mc00_c; \
    c->PFX ## _pixels_tab[IDX][ 1] = PFX ## NUM ## _mc10_c; \
    c->PFX ## _pixels_tab[IDX][ 2] = PFX ## NUM ## _mc20_c; \
    c->PFX ## _pixels_tab[IDX][ 3] = PFX ## NUM ## _mc30_c; \
    c->PFX ## _pixels_tab[IDX][ 4] = PFX ## NUM ## _mc01_c; \
    c->PFX ## _pixels_tab[IDX][ 5] = PFX ## NUM ## _mc11_c; \
    c->PFX ## _pixels_tab[IDX][ 6] = PFX ## NUM ## _mc21_c; \
    c->PFX ## _pixels_tab[IDX][ 7] = PFX ## NUM ## _mc31_c; \
    c->PFX ## _pixels_tab[IDX][ 8] = PFX ## NUM ## _mc02_c; \
    c->PFX ## _pixels_tab[IDX][ 9] = PFX ## NUM ## _mc12_c; \
    c->PFX ## _pixels_tab[IDX][10] = PFX ## NUM ## _mc22_c; \
    c->PFX ## _pixels_tab[IDX][11] = PFX ## NUM ## _mc32_c; \
    c->PFX ## _pixels_tab[IDX][12] = PFX ## NUM ## _mc03_c; \
    c->PFX ## _pixels_tab[IDX][13] = PFX ## NUM ## _mc13_c; \
    c->PFX ## _pixels_tab[IDX][14] = PFX ## NUM ## _mc23_c; \
    c->PFX ## _pixels_tab[IDX][15] = PFX ## NUM ## _mc33_c

    dspfunc(put_qpel, 0, 16);
    dspfunc(put_no_rnd_qpel, 0, 16);

    dspfunc(avg_qpel, 0, 16);
    /* dspfunc(avg_no_rnd_qpel, 0, 16); */

    dspfunc(put_qpel, 1, 8);
    dspfunc(put_no_rnd_qpel, 1, 8);

    dspfunc(avg_qpel, 1, 8);
    /* dspfunc(avg_no_rnd_qpel, 1, 8); */
#undef dspfunc

    c->add_bytes= add_bytes_c;
    c->diff_bytes= diff_bytes_c;

#ifdef HAVE_MMX
    dsputil_init_mmx(c, mask);
    if (ff_bit_exact)
    {
        /* FIXME - AVCodec context should have flag for bitexact match */
	/* fprintf(stderr, "\n\n\nff_bit_exact %d\n\n\n\n", ff_bit_exact); */
	dsputil_set_bit_exact_mmx(c, mask);
    }
#endif
#ifdef ARCH_ARMV4L
    dsputil_init_armv4l(c, mask);
#endif
#ifdef HAVE_MLIB
    dsputil_init_mlib(c, mask);
#endif
#ifdef ARCH_ALPHA
    dsputil_init_alpha(c, mask);
#endif
#ifdef ARCH_POWERPC
    dsputil_init_ppc(c, mask);
#endif
#ifdef HAVE_MMI
    dsputil_init_mmi(c, mask);
#endif

}

/* remove any non bit exact operation (testing purpose) */
void avcodec_set_bit_exact(void)
{
    ff_bit_exact=1;
#ifdef HAVE_MMX
// FIXME - better set_bit_exact
//    dsputil_set_bit_exact_mmx();
#endif
}
