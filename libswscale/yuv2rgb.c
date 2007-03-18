/*
 * yuv2rgb.c, Software YUV to RGB coverter
 *
 *  Copyright (C) 1999, Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *  All Rights Reserved.
 *
 *  Functions broken out from display_x11.c and several new modes
 *  added by Håkan Hjort <d95hjort@dtek.chalmers.se>
 *
 *  15 & 16 bpp support by Franck Sicard <Franck.Sicard@solsoft.fr>
 *
 *  This file is part of mpeg2dec, a free MPEG-2 video decoder
 *
 *  mpeg2dec is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  mpeg2dec is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with mpeg2dec; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * MMX/MMX2 Template stuff from Michael Niedermayer (michaelni@gmx.at) (needed for fast movntq support)
 * 1,4,8bpp support by Michael Niedermayer (michaelni@gmx.at)
 * context / deglobalize stuff by Michael Niedermayer
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>

#include "config.h"
#include "rgb2rgb.h"
#include "swscale.h"
#include "swscale_internal.h"

#ifdef HAVE_MLIB
#include "yuv2rgb_mlib.c"
#endif

#define DITHER1XBPP // only for mmx

const uint8_t  __attribute__((aligned(8))) dither_2x2_4[2][8]={
{  1,   3,   1,   3,   1,   3,   1,   3, },
{  2,   0,   2,   0,   2,   0,   2,   0, },
};

const uint8_t  __attribute__((aligned(8))) dither_2x2_8[2][8]={
{  6,   2,   6,   2,   6,   2,   6,   2, },
{  0,   4,   0,   4,   0,   4,   0,   4, },
};

const uint8_t  __attribute__((aligned(8))) dither_8x8_32[8][8]={
{ 17,   9,  23,  15,  16,   8,  22,  14, },
{  5,  29,   3,  27,   4,  28,   2,  26, },
{ 21,  13,  19,  11,  20,  12,  18,  10, },
{  0,  24,   6,  30,   1,  25,   7,  31, },
{ 16,   8,  22,  14,  17,   9,  23,  15, },
{  4,  28,   2,  26,   5,  29,   3,  27, },
{ 20,  12,  18,  10,  21,  13,  19,  11, },
{  1,  25,   7,  31,   0,  24,   6,  30, },
};

#if 0
const uint8_t  __attribute__((aligned(8))) dither_8x8_64[8][8]={
{  0,  48,  12,  60,   3,  51,  15,  63, },
{ 32,  16,  44,  28,  35,  19,  47,  31, },
{  8,  56,   4,  52,  11,  59,   7,  55, },
{ 40,  24,  36,  20,  43,  27,  39,  23, },
{  2,  50,  14,  62,   1,  49,  13,  61, },
{ 34,  18,  46,  30,  33,  17,  45,  29, },
{ 10,  58,   6,  54,   9,  57,   5,  53, },
{ 42,  26,  38,  22,  41,  25,  37,  21, },
};
#endif

const uint8_t  __attribute__((aligned(8))) dither_8x8_73[8][8]={
{  0,  55,  14,  68,   3,  58,  17,  72, },
{ 37,  18,  50,  32,  40,  22,  54,  35, },
{  9,  64,   5,  59,  13,  67,   8,  63, },
{ 46,  27,  41,  23,  49,  31,  44,  26, },
{  2,  57,  16,  71,   1,  56,  15,  70, },
{ 39,  21,  52,  34,  38,  19,  51,  33, },
{ 11,  66,   7,  62,  10,  65,   6,  60, },
{ 48,  30,  43,  25,  47,  29,  42,  24, },
};

#if 0
const uint8_t  __attribute__((aligned(8))) dither_8x8_128[8][8]={
{ 68,  36,  92,  60,  66,  34,  90,  58, },
{ 20, 116,  12, 108,  18, 114,  10, 106, },
{ 84,  52,  76,  44,  82,  50,  74,  42, },
{  0,  96,  24, 120,   6, 102,  30, 126, },
{ 64,  32,  88,  56,  70,  38,  94,  62, },
{ 16, 112,   8, 104,  22, 118,  14, 110, },
{ 80,  48,  72,  40,  86,  54,  78,  46, },
{  4, 100,  28, 124,   2,  98,  26, 122, },
};
#endif

#if 1
const uint8_t  __attribute__((aligned(8))) dither_8x8_220[8][8]={
{117,  62, 158, 103, 113,  58, 155, 100, },
{ 34, 199,  21, 186,  31, 196,  17, 182, },
{144,  89, 131,  76, 141,  86, 127,  72, },
{  0, 165,  41, 206,  10, 175,  52, 217, },
{110,  55, 151,  96, 120,  65, 162, 107, },
{ 28, 193,  14, 179,  38, 203,  24, 189, },
{138,  83, 124,  69, 148,  93, 134,  79, },
{  7, 172,  48, 213,   3, 168,  45, 210, },
};
#elif 1
// tries to correct a gamma of 1.5
const uint8_t  __attribute__((aligned(8))) dither_8x8_220[8][8]={
{  0, 143,  18, 200,   2, 156,  25, 215, },
{ 78,  28, 125,  64,  89,  36, 138,  74, },
{ 10, 180,   3, 161,  16, 195,   8, 175, },
{109,  51,  93,  38, 121,  60, 105,  47, },
{  1, 152,  23, 210,   0, 147,  20, 205, },
{ 85,  33, 134,  71,  81,  30, 130,  67, },
{ 14, 190,   6, 171,  12, 185,   5, 166, },
{117,  57, 101,  44, 113,  54,  97,  41, },
};
#elif 1
// tries to correct a gamma of 2.0
const uint8_t  __attribute__((aligned(8))) dither_8x8_220[8][8]={
{  0, 124,   8, 193,   0, 140,  12, 213, },
{ 55,  14, 104,  42,  66,  19, 119,  52, },
{  3, 168,   1, 145,   6, 187,   3, 162, },
{ 86,  31,  70,  21,  99,  39,  82,  28, },
{  0, 134,  11, 206,   0, 129,   9, 200, },
{ 62,  17, 114,  48,  58,  16, 109,  45, },
{  5, 181,   2, 157,   4, 175,   1, 151, },
{ 95,  36,  78,  26,  90,  34,  74,  24, },
};
#else
// tries to correct a gamma of 2.5
const uint8_t  __attribute__((aligned(8))) dither_8x8_220[8][8]={
{  0, 107,   3, 187,   0, 125,   6, 212, },
{ 39,   7,  86,  28,  49,  11, 102,  36, },
{  1, 158,   0, 131,   3, 180,   1, 151, },
{ 68,  19,  52,  12,  81,  25,  64,  17, },
{  0, 119,   5, 203,   0, 113,   4, 195, },
{ 45,   9,  96,  33,  42,   8,  91,  30, },
{  2, 172,   1, 144,   2, 165,   0, 137, },
{ 77,  23,  60,  15,  72,  21,  56,  14, },
};
#endif

#ifdef HAVE_MMX

/* hope these constant values are cache line aligned */
static uint64_t attribute_used __attribute__((aligned(8))) mmx_00ffw = 0x00ff00ff00ff00ffULL;
static uint64_t attribute_used __attribute__((aligned(8))) mmx_redmask = 0xf8f8f8f8f8f8f8f8ULL;
static uint64_t attribute_used __attribute__((aligned(8))) mmx_grnmask = 0xfcfcfcfcfcfcfcfcULL;

static uint64_t attribute_used __attribute__((aligned(8))) M24A=   0x00FF0000FF0000FFULL;
static uint64_t attribute_used __attribute__((aligned(8))) M24B=   0xFF0000FF0000FF00ULL;
static uint64_t attribute_used __attribute__((aligned(8))) M24C=   0x0000FF0000FF0000ULL;

// the volatile is required because gcc otherwise optimizes some writes away not knowing that these
// are read in the asm block
static volatile uint64_t attribute_used __attribute__((aligned(8))) b5Dither;
static volatile uint64_t attribute_used __attribute__((aligned(8))) g5Dither;
static volatile uint64_t attribute_used __attribute__((aligned(8))) g6Dither;
static volatile uint64_t attribute_used __attribute__((aligned(8))) r5Dither;

static uint64_t __attribute__((aligned(8))) dither4[2]={
	0x0103010301030103LL,
	0x0200020002000200LL,};

static uint64_t __attribute__((aligned(8))) dither8[2]={
	0x0602060206020602LL,
	0x0004000400040004LL,};

#undef HAVE_MMX

//MMX versions
#undef RENAME
#define HAVE_MMX
#undef HAVE_MMX2
#undef HAVE_3DNOW
#define RENAME(a) a ## _MMX
#include "yuv2rgb_template.c"

//MMX2 versions
#undef RENAME
#define HAVE_MMX
#define HAVE_MMX2
#undef HAVE_3DNOW
#define RENAME(a) a ## _MMX2
#include "yuv2rgb_template.c"

#endif /* defined(ARCH_X86) */

const int32_t Inverse_Table_6_9[8][4] = {
    {117504, 138453, 13954, 34903}, /* no sequence_display_extension */
    {117504, 138453, 13954, 34903}, /* ITU-R Rec. 709 (1990) */
    {104597, 132201, 25675, 53279}, /* unspecified */
    {104597, 132201, 25675, 53279}, /* reserved */
    {104448, 132798, 24759, 53109}, /* FCC */
    {104597, 132201, 25675, 53279}, /* ITU-R Rec. 624-4 System B, G */
    {104597, 132201, 25675, 53279}, /* SMPTE 170M */
    {117579, 136230, 16907, 35559}  /* SMPTE 240M (1987) */
};

#define RGB(i)					\
	U = pu[i];				\
	V = pv[i];				\
	r = (void *)c->table_rV[V];			\
	g = (void *)(c->table_gU[U] + c->table_gV[V]);		\
	b = (void *)c->table_bU[U];

#define DST1(i)					\
	Y = py_1[2*i];				\
	dst_1[2*i] = r[Y] + g[Y] + b[Y];	\
	Y = py_1[2*i+1];			\
	dst_1[2*i+1] = r[Y] + g[Y] + b[Y];

#define DST2(i)					\
	Y = py_2[2*i];				\
	dst_2[2*i] = r[Y] + g[Y] + b[Y];	\
	Y = py_2[2*i+1];			\
	dst_2[2*i+1] = r[Y] + g[Y] + b[Y];

#define DST1RGB(i)							\
	Y = py_1[2*i];							\
	dst_1[6*i] = r[Y]; dst_1[6*i+1] = g[Y]; dst_1[6*i+2] = b[Y];	\
	Y = py_1[2*i+1];						\
	dst_1[6*i+3] = r[Y]; dst_1[6*i+4] = g[Y]; dst_1[6*i+5] = b[Y];

#define DST2RGB(i)							\
	Y = py_2[2*i];							\
	dst_2[6*i] = r[Y]; dst_2[6*i+1] = g[Y]; dst_2[6*i+2] = b[Y];	\
	Y = py_2[2*i+1];						\
	dst_2[6*i+3] = r[Y]; dst_2[6*i+4] = g[Y]; dst_2[6*i+5] = b[Y];

#define DST1BGR(i)							\
	Y = py_1[2*i];							\
	dst_1[6*i] = b[Y]; dst_1[6*i+1] = g[Y]; dst_1[6*i+2] = r[Y];	\
	Y = py_1[2*i+1];						\
	dst_1[6*i+3] = b[Y]; dst_1[6*i+4] = g[Y]; dst_1[6*i+5] = r[Y];

#define DST2BGR(i)							\
	Y = py_2[2*i];							\
	dst_2[6*i] = b[Y]; dst_2[6*i+1] = g[Y]; dst_2[6*i+2] = r[Y];	\
	Y = py_2[2*i+1];						\
	dst_2[6*i+3] = b[Y]; dst_2[6*i+4] = g[Y]; dst_2[6*i+5] = r[Y];

#define PROLOG(func_name, dst_type) \
static int func_name(SwsContext *c, uint8_t* src[], int srcStride[], int srcSliceY, \
             int srcSliceH, uint8_t* dst[], int dstStride[]){\
    int y;\
\
    if(c->srcFormat == PIX_FMT_YUV422P){\
	srcStride[1] *= 2;\
	srcStride[2] *= 2;\
    }\
    for(y=0; y<srcSliceH; y+=2){\
	dst_type *dst_1= (dst_type*)(dst[0] + (y+srcSliceY  )*dstStride[0]);\
	dst_type *dst_2= (dst_type*)(dst[0] + (y+srcSliceY+1)*dstStride[0]);\
	dst_type attribute_unused *r, *b;\
	dst_type *g;\
	uint8_t *py_1= src[0] + y*srcStride[0];\
	uint8_t *py_2= py_1 + srcStride[0];\
	uint8_t *pu= src[1] + (y>>1)*srcStride[1];\
	uint8_t *pv= src[2] + (y>>1)*srcStride[2];\
	unsigned int h_size= c->dstW>>3;\
	while (h_size--) {\
	    int attribute_unused U, V;\
	    int Y;\

#define EPILOG(dst_delta)\
	    pu += 4;\
	    pv += 4;\
	    py_1 += 8;\
	    py_2 += 8;\
	    dst_1 += dst_delta;\
	    dst_2 += dst_delta;\
	}\
    }\
    return srcSliceH;\
}

PROLOG(yuv2rgb_c_32, uint32_t)
	RGB(0);
	DST1(0);
	DST2(0);

	RGB(1);
	DST2(1);
	DST1(1);

	RGB(2);
	DST1(2);
	DST2(2);

	RGB(3);
	DST2(3);
	DST1(3);
EPILOG(8)

PROLOG(yuv2rgb_c_24_rgb, uint8_t)
	RGB(0);
	DST1RGB(0);
	DST2RGB(0);

	RGB(1);
	DST2RGB(1);
	DST1RGB(1);

	RGB(2);
	DST1RGB(2);
	DST2RGB(2);

	RGB(3);
	DST2RGB(3);
	DST1RGB(3);
EPILOG(24)

// only trivial mods from yuv2rgb_c_24_rgb
PROLOG(yuv2rgb_c_24_bgr, uint8_t)
	RGB(0);
	DST1BGR(0);
	DST2BGR(0);

	RGB(1);
	DST2BGR(1);
	DST1BGR(1);

	RGB(2);
	DST1BGR(2);
	DST2BGR(2);

	RGB(3);
	DST2BGR(3);
	DST1BGR(3);
EPILOG(24)

// This is exactly the same code as yuv2rgb_c_32 except for the types of
// r, g, b, dst_1, dst_2
PROLOG(yuv2rgb_c_16, uint16_t)
	RGB(0);
	DST1(0);
	DST2(0);

	RGB(1);
	DST2(1);
	DST1(1);

	RGB(2);
	DST1(2);
	DST2(2);

	RGB(3);
	DST2(3);
	DST1(3);
EPILOG(8)

// This is exactly the same code as yuv2rgb_c_32 except for the types of
// r, g, b, dst_1, dst_2
PROLOG(yuv2rgb_c_8, uint8_t)
	RGB(0);
	DST1(0);
	DST2(0);

	RGB(1);
	DST2(1);
	DST1(1);

	RGB(2);
	DST1(2);
	DST2(2);

	RGB(3);
	DST2(3);
	DST1(3);
EPILOG(8)

// r, g, b, dst_1, dst_2
PROLOG(yuv2rgb_c_8_ordered_dither, uint8_t)
	const uint8_t *d32= dither_8x8_32[y&7];
	const uint8_t *d64= dither_8x8_73[y&7];
#define DST1bpp8(i,o)					\
	Y = py_1[2*i];				\
	dst_1[2*i] = r[Y+d32[0+o]] + g[Y+d32[0+o]] + b[Y+d64[0+o]];	\
	Y = py_1[2*i+1];			\
	dst_1[2*i+1] = r[Y+d32[1+o]] + g[Y+d32[1+o]] + b[Y+d64[1+o]];

#define DST2bpp8(i,o)					\
	Y = py_2[2*i];				\
	dst_2[2*i] =  r[Y+d32[8+o]] + g[Y+d32[8+o]] + b[Y+d64[8+o]];	\
	Y = py_2[2*i+1];			\
	dst_2[2*i+1] =  r[Y+d32[9+o]] + g[Y+d32[9+o]] + b[Y+d64[9+o]];


	RGB(0);
	DST1bpp8(0,0);
	DST2bpp8(0,0);

	RGB(1);
	DST2bpp8(1,2);
	DST1bpp8(1,2);

	RGB(2);
	DST1bpp8(2,4);
	DST2bpp8(2,4);

	RGB(3);
	DST2bpp8(3,6);
	DST1bpp8(3,6);
EPILOG(8)


// This is exactly the same code as yuv2rgb_c_32 except for the types of
// r, g, b, dst_1, dst_2
PROLOG(yuv2rgb_c_4, uint8_t)
        int acc;
#define DST1_4(i)					\
	Y = py_1[2*i];				\
	acc = r[Y] + g[Y] + b[Y];	\
	Y = py_1[2*i+1];			\
        acc |= (r[Y] + g[Y] + b[Y])<<4;\
	dst_1[i] = acc; 

#define DST2_4(i)					\
	Y = py_2[2*i];				\
	acc = r[Y] + g[Y] + b[Y];	\
	Y = py_2[2*i+1];			\
	acc |= (r[Y] + g[Y] + b[Y])<<4;\
	dst_2[i] = acc; 
	
        RGB(0);
	DST1_4(0);
	DST2_4(0);

	RGB(1);
	DST2_4(1);
	DST1_4(1);

	RGB(2);
	DST1_4(2);
	DST2_4(2);

	RGB(3);
	DST2_4(3);
	DST1_4(3);
EPILOG(4)

PROLOG(yuv2rgb_c_4_ordered_dither, uint8_t)
	const uint8_t *d64= dither_8x8_73[y&7];
	const uint8_t *d128=dither_8x8_220[y&7];
        int acc;

#define DST1bpp4(i,o)					\
	Y = py_1[2*i];				\
	acc = r[Y+d128[0+o]] + g[Y+d64[0+o]] + b[Y+d128[0+o]];	\
	Y = py_1[2*i+1];			\
	acc |= (r[Y+d128[1+o]] + g[Y+d64[1+o]] + b[Y+d128[1+o]])<<4;\
        dst_1[i]= acc;

#define DST2bpp4(i,o)					\
	Y = py_2[2*i];				\
	acc =  r[Y+d128[8+o]] + g[Y+d64[8+o]] + b[Y+d128[8+o]];	\
	Y = py_2[2*i+1];			\
	acc |=  (r[Y+d128[9+o]] + g[Y+d64[9+o]] + b[Y+d128[9+o]])<<4;\
        dst_2[i]= acc;


	RGB(0);
	DST1bpp4(0,0);
	DST2bpp4(0,0);

	RGB(1);
	DST2bpp4(1,2);
	DST1bpp4(1,2);

	RGB(2);
	DST1bpp4(2,4);
	DST2bpp4(2,4);

	RGB(3);
	DST2bpp4(3,6);
	DST1bpp4(3,6);
EPILOG(4)

// This is exactly the same code as yuv2rgb_c_32 except for the types of
// r, g, b, dst_1, dst_2
PROLOG(yuv2rgb_c_4b, uint8_t)
	RGB(0);
	DST1(0);
	DST2(0);

	RGB(1);
	DST2(1);
	DST1(1);

	RGB(2);
	DST1(2);
	DST2(2);

	RGB(3);
	DST2(3);
	DST1(3);
EPILOG(8)

PROLOG(yuv2rgb_c_4b_ordered_dither, uint8_t)
	const uint8_t *d64= dither_8x8_73[y&7];
	const uint8_t *d128=dither_8x8_220[y&7];

#define DST1bpp4b(i,o)					\
	Y = py_1[2*i];				\
	dst_1[2*i] = r[Y+d128[0+o]] + g[Y+d64[0+o]] + b[Y+d128[0+o]];	\
	Y = py_1[2*i+1];			\
	dst_1[2*i+1] = r[Y+d128[1+o]] + g[Y+d64[1+o]] + b[Y+d128[1+o]];

#define DST2bpp4b(i,o)					\
	Y = py_2[2*i];				\
	dst_2[2*i] =  r[Y+d128[8+o]] + g[Y+d64[8+o]] + b[Y+d128[8+o]];	\
	Y = py_2[2*i+1];			\
	dst_2[2*i+1] =  r[Y+d128[9+o]] + g[Y+d64[9+o]] + b[Y+d128[9+o]];


	RGB(0);
	DST1bpp4b(0,0);
	DST2bpp4b(0,0);

	RGB(1);
	DST2bpp4b(1,2);
	DST1bpp4b(1,2);

	RGB(2);
	DST1bpp4b(2,4);
	DST2bpp4b(2,4);

	RGB(3);
	DST2bpp4b(3,6);
	DST1bpp4b(3,6);
EPILOG(8)

PROLOG(yuv2rgb_c_1_ordered_dither, uint8_t)
	const uint8_t *d128=dither_8x8_220[y&7];
	char out_1=0, out_2=0;
	g= c->table_gU[128] + c->table_gV[128];

#define DST1bpp1(i,o)					\
	Y = py_1[2*i];				\
	out_1+= out_1 + g[Y+d128[0+o]];	\
	Y = py_1[2*i+1];			\
	out_1+= out_1 + g[Y+d128[1+o]];

#define DST2bpp1(i,o)					\
	Y = py_2[2*i];				\
	out_2+= out_2 + g[Y+d128[8+o]];	\
	Y = py_2[2*i+1];			\
	out_2+= out_2 + g[Y+d128[9+o]];

	DST1bpp1(0,0);
	DST2bpp1(0,0);

	DST2bpp1(1,2);
	DST1bpp1(1,2);

	DST1bpp1(2,4);
	DST2bpp1(2,4);

	DST2bpp1(3,6);
	DST1bpp1(3,6);
	
	dst_1[0]= out_1;
	dst_2[0]= out_2;
EPILOG(1)

SwsFunc yuv2rgb_get_func_ptr (SwsContext *c)
{
#if defined(HAVE_MMX2) || defined(HAVE_MMX)
    if(c->flags & SWS_CPU_CAPS_MMX2){
	switch(c->dstFormat){
	case PIX_FMT_RGB32: return yuv420_rgb32_MMX2;
	case PIX_FMT_BGR24: return yuv420_rgb24_MMX2;
	case PIX_FMT_BGR565: return yuv420_rgb16_MMX2;
	case PIX_FMT_BGR555: return yuv420_rgb15_MMX2;
	}
    }
    if(c->flags & SWS_CPU_CAPS_MMX){
	switch(c->dstFormat){
	case PIX_FMT_RGB32: return yuv420_rgb32_MMX;
	case PIX_FMT_BGR24: return yuv420_rgb24_MMX;
	case PIX_FMT_BGR565: return yuv420_rgb16_MMX;
	case PIX_FMT_BGR555: return yuv420_rgb15_MMX;
	}
    }
#endif
#ifdef HAVE_MLIB
    {
	SwsFunc t= yuv2rgb_init_mlib(c);
	if(t) return t;
    }
#endif
#ifdef HAVE_ALTIVEC
    if (c->flags & SWS_CPU_CAPS_ALTIVEC)
    {
	SwsFunc t = yuv2rgb_init_altivec(c);
	if(t) return t;
    }
#endif

    av_log(c, AV_LOG_WARNING, "No accelerated colorspace conversion found\n");

    switch(c->dstFormat){
    case PIX_FMT_BGR32:
    case PIX_FMT_RGB32: return yuv2rgb_c_32;
    case PIX_FMT_RGB24: return yuv2rgb_c_24_rgb;
    case PIX_FMT_BGR24: return yuv2rgb_c_24_bgr;
    case PIX_FMT_RGB565:
    case PIX_FMT_BGR565:
    case PIX_FMT_RGB555:
    case PIX_FMT_BGR555: return yuv2rgb_c_16;
    case PIX_FMT_RGB8:
    case PIX_FMT_BGR8:  return yuv2rgb_c_8_ordered_dither;
    case PIX_FMT_RGB4:
    case PIX_FMT_BGR4:  return yuv2rgb_c_4_ordered_dither;
    case PIX_FMT_RGB4_BYTE:
    case PIX_FMT_BGR4_BYTE:  return yuv2rgb_c_4b_ordered_dither;
    case PIX_FMT_MONOBLACK:  return yuv2rgb_c_1_ordered_dither;
    default:
    	assert(0);
    }
    return NULL;
}

static int div_round (int dividend, int divisor)
{
    if (dividend > 0)
	return (dividend + (divisor>>1)) / divisor;
    else
	return -((-dividend + (divisor>>1)) / divisor);
}

int yuv2rgb_c_init_tables (SwsContext *c, const int inv_table[4], int fullRange, int brightness, int contrast, int saturation)
{  
    const int isRgb = isBGR(c->dstFormat);
    const int bpp = fmt_depth(c->dstFormat);
    int i;
    uint8_t table_Y[1024];
    uint32_t *table_32 = 0;
    uint16_t *table_16 = 0;
    uint8_t *table_8 = 0;
    uint8_t *table_332 = 0;
    uint8_t *table_121 = 0;
    uint8_t *table_1 = 0;
    int entry_size = 0;
    void *table_r = 0, *table_g = 0, *table_b = 0;
    void *table_start;

    int64_t crv =  inv_table[0];
    int64_t cbu =  inv_table[1];
    int64_t cgu = -inv_table[2];
    int64_t cgv = -inv_table[3];
    int64_t cy  = 1<<16;
    int64_t oy  = 0;

//printf("%lld %lld %lld %lld %lld\n", cy, crv, cbu, cgu, cgv);
    if(!fullRange){
	cy= (cy*255) / 219;
	oy= 16<<16;
    }else{
        crv= (crv*224) / 255;
        cbu= (cbu*224) / 255;
        cgu= (cgu*224) / 255;
        cgv= (cgv*224) / 255;
    }
	
    cy = (cy *contrast             )>>16;
    crv= (crv*contrast * saturation)>>32;
    cbu= (cbu*contrast * saturation)>>32;
    cgu= (cgu*contrast * saturation)>>32;
    cgv= (cgv*contrast * saturation)>>32;
//printf("%lld %lld %lld %lld %lld\n", cy, crv, cbu, cgu, cgv);
    oy -= 256*brightness;

    for (i = 0; i < 1024; i++) {
	int j;

	j= (cy*(((i - 384)<<16) - oy) + (1<<31))>>32;
	j = (j < 0) ? 0 : ((j > 255) ? 255 : j);
	table_Y[i] = j;
    }

    switch (bpp) {
    case 32:
	table_start= table_32 = av_malloc ((197 + 2*682 + 256 + 132) * sizeof (uint32_t));

	entry_size = sizeof (uint32_t);
	table_r = table_32 + 197;
	table_b = table_32 + 197 + 685;
	table_g = table_32 + 197 + 2*682;

	for (i = -197; i < 256+197; i++)
	    ((uint32_t *)table_r)[i] = table_Y[i+384] << (isRgb ? 16 : 0);
	for (i = -132; i < 256+132; i++)
	    ((uint32_t *)table_g)[i] = table_Y[i+384] << 8;
	for (i = -232; i < 256+232; i++)
	    ((uint32_t *)table_b)[i] = table_Y[i+384] << (isRgb ? 0 : 16);
	break;

    case 24:
	table_start= table_8 = av_malloc ((256 + 2*232) * sizeof (uint8_t));

	entry_size = sizeof (uint8_t);
	table_r = table_g = table_b = table_8 + 232;

	for (i = -232; i < 256+232; i++)
	    ((uint8_t * )table_b)[i] = table_Y[i+384];
	break;

    case 15:
    case 16:
	table_start= table_16 = av_malloc ((197 + 2*682 + 256 + 132) * sizeof (uint16_t));

	entry_size = sizeof (uint16_t);
	table_r = table_16 + 197;
	table_b = table_16 + 197 + 685;
	table_g = table_16 + 197 + 2*682;

	for (i = -197; i < 256+197; i++) {
	    int j = table_Y[i+384] >> 3;

	    if (isRgb)
		j <<= ((bpp==16) ? 11 : 10);

	    ((uint16_t *)table_r)[i] = j;
	}
	for (i = -132; i < 256+132; i++) {
	    int j = table_Y[i+384] >> ((bpp==16) ? 2 : 3);

	    ((uint16_t *)table_g)[i] = j << 5;
	}
	for (i = -232; i < 256+232; i++) {
	    int j = table_Y[i+384] >> 3;

	    if (!isRgb)
		j <<= ((bpp==16) ? 11 : 10);

	    ((uint16_t *)table_b)[i] = j;
	}
	break;

    case 8:
	table_start= table_332 = av_malloc ((197 + 2*682 + 256 + 132) * sizeof (uint8_t));

	entry_size = sizeof (uint8_t);
	table_r = table_332 + 197;
	table_b = table_332 + 197 + 685;
	table_g = table_332 + 197 + 2*682;

	for (i = -197; i < 256+197; i++) {
	    int j = (table_Y[i+384 - 16] + 18)/36;

	    if (isRgb)
		j <<= 5;

	    ((uint8_t *)table_r)[i] = j;
	}
	for (i = -132; i < 256+132; i++) {
	    int j = (table_Y[i+384 - 16] + 18)/36;

	    if (!isRgb)
		j <<= 1;

	    ((uint8_t *)table_g)[i] = j << 2;
	}
	for (i = -232; i < 256+232; i++) {
	    int j = (table_Y[i+384 - 37] + 43)/85;

	    if (!isRgb)
		j <<= 6;

	    ((uint8_t *)table_b)[i] = j;
	}
	break;
    case 4:
    case 4|128:
	table_start= table_121 = av_malloc ((197 + 2*682 + 256 + 132) * sizeof (uint8_t));

	entry_size = sizeof (uint8_t);
	table_r = table_121 + 197;
	table_b = table_121 + 197 + 685;
	table_g = table_121 + 197 + 2*682;

	for (i = -197; i < 256+197; i++) {
	    int j = table_Y[i+384 - 110] >> 7;

	    if (isRgb)
		j <<= 3;

	    ((uint8_t *)table_r)[i] = j;
	}
	for (i = -132; i < 256+132; i++) {
	    int j = (table_Y[i+384 - 37]+ 43)/85;

	    ((uint8_t *)table_g)[i] = j << 1;
	}
	for (i = -232; i < 256+232; i++) {
	    int j =table_Y[i+384 - 110] >> 7;

	    if (!isRgb)
		j <<= 3;

	    ((uint8_t *)table_b)[i] = j;
	}
	break;

    case 1:
	table_start= table_1 = av_malloc (256*2 * sizeof (uint8_t));

	entry_size = sizeof (uint8_t);
	table_g = table_1;
	table_r = table_b = NULL;

	for (i = 0; i < 256+256; i++) {
	    int j = table_Y[i + 384 - 110]>>7;

	    ((uint8_t *)table_g)[i] = j;
	}
	break;

    default:
	table_start= NULL;
	av_log(c, AV_LOG_ERROR, "%ibpp not supported by yuv2rgb\n", bpp);
	//free mem?
	return -1;
    }

    for (i = 0; i < 256; i++) {
	c->table_rV[i] = (uint8_t *)table_r + entry_size * div_round (crv * (i-128), 76309);
	c->table_gU[i] = (uint8_t *)table_g + entry_size * div_round (cgu * (i-128), 76309);
	c->table_gV[i] = entry_size * div_round (cgv * (i-128), 76309);
	c->table_bU[i] = (uint8_t *)table_b + entry_size * div_round (cbu * (i-128), 76309);
    }

    av_free(c->yuvTable);
    c->yuvTable= table_start;
    return 0;
}
