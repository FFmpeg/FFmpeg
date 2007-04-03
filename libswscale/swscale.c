/*
 * Copyright (C) 2001-2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * the C code (not assembly, mmx, ...) of this file can be used
 * under the LGPL license too
 */

/*
  supported Input formats: YV12, I420/IYUV, YUY2, UYVY, BGR32, BGR24, BGR16, BGR15, RGB32, RGB24, Y8/Y800, YVU9/IF09, PAL8
  supported output formats: YV12, I420/IYUV, YUY2, UYVY, {BGR,RGB}{1,4,8,15,16,24,32}, Y8/Y800, YVU9/IF09
  {BGR,RGB}{1,4,8,15,16} support dithering
  
  unscaled special converters (YV12=I420=IYUV, Y800=Y8)
  YV12 -> {BGR,RGB}{1,4,8,15,16,24,32}
  x -> x
  YUV9 -> YV12
  YUV9/YV12 -> Y800
  Y800 -> YUV9/YV12
  BGR24 -> BGR32 & RGB24 -> RGB32
  BGR32 -> BGR24 & RGB32 -> RGB24
  BGR15 -> BGR16
*/

/* 
tested special converters (most are tested actually but i didnt write it down ...)
 YV12 -> BGR16
 YV12 -> YV12
 BGR15 -> BGR16
 BGR16 -> BGR16
 YVU9 -> YV12

untested special converters
  YV12/I420 -> BGR15/BGR24/BGR32 (its the yuv2rgb stuff, so it should be ok)
  YV12/I420 -> YV12/I420
  YUY2/BGR15/BGR24/BGR32/RGB24/RGB32 -> same format
  BGR24 -> BGR32 & RGB24 -> RGB32
  BGR32 -> BGR24 & RGB32 -> RGB24
  BGR24 -> YV12
*/

#include <inttypes.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include "config.h"
#include <assert.h>
#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif
#include "swscale.h"
#include "swscale_internal.h"
#include "x86_cpu.h"
#include "bswap.h"
#include "rgb2rgb.h"
#ifdef USE_FASTMEMCPY
#include "libvo/fastmemcpy.h"
#endif

#undef MOVNTQ
#undef PAVGB

//#undef HAVE_MMX2
//#define HAVE_3DNOW
//#undef HAVE_MMX
//#undef ARCH_X86
//#define WORDS_BIGENDIAN
#define DITHER1XBPP

#define FAST_BGR2YV12 // use 7 bit coeffs instead of 15bit

#define RET 0xC3 //near return opcode for X86

#ifdef MP_DEBUG
#define ASSERT(x) assert(x);
#else
#define ASSERT(x) ;
#endif

#ifdef M_PI
#define PI M_PI
#else
#define PI 3.14159265358979323846
#endif

#define isSupportedIn(x)  ((x)==PIX_FMT_YUV420P || (x)==PIX_FMT_YUYV422 || (x)==PIX_FMT_UYVY422\
			|| (x)==PIX_FMT_RGB32|| (x)==PIX_FMT_BGR24|| (x)==PIX_FMT_BGR565|| (x)==PIX_FMT_BGR555\
			|| (x)==PIX_FMT_BGR32|| (x)==PIX_FMT_RGB24|| (x)==PIX_FMT_RGB565|| (x)==PIX_FMT_RGB555\
			|| (x)==PIX_FMT_GRAY8 || (x)==PIX_FMT_YUV410P\
			|| (x)==PIX_FMT_GRAY16BE || (x)==PIX_FMT_GRAY16LE\
			|| (x)==PIX_FMT_YUV444P || (x)==PIX_FMT_YUV422P || (x)==PIX_FMT_YUV411P\
			|| (x)==PIX_FMT_PAL8 || (x)==PIX_FMT_BGR8 || (x)==PIX_FMT_RGB8\
                        || (x)==PIX_FMT_BGR4_BYTE  || (x)==PIX_FMT_RGB4_BYTE)
#define isSupportedOut(x) ((x)==PIX_FMT_YUV420P || (x)==PIX_FMT_YUYV422 || (x)==PIX_FMT_UYVY422\
			|| (x)==PIX_FMT_YUV444P || (x)==PIX_FMT_YUV422P || (x)==PIX_FMT_YUV411P\
			|| isRGB(x) || isBGR(x)\
			|| (x)==PIX_FMT_NV12 || (x)==PIX_FMT_NV21\
			|| (x)==PIX_FMT_GRAY16BE || (x)==PIX_FMT_GRAY16LE\
			|| (x)==PIX_FMT_GRAY8 || (x)==PIX_FMT_YUV410P)
#define isPacked(x)    ((x)==PIX_FMT_PAL8 || (x)==PIX_FMT_YUYV422 ||\
                        (x)==PIX_FMT_UYVY422 || isRGB(x) || isBGR(x))

#define RGB2YUV_SHIFT 16
#define BY ((int)( 0.098*(1<<RGB2YUV_SHIFT)+0.5))
#define BV ((int)(-0.071*(1<<RGB2YUV_SHIFT)+0.5))
#define BU ((int)( 0.439*(1<<RGB2YUV_SHIFT)+0.5))
#define GY ((int)( 0.504*(1<<RGB2YUV_SHIFT)+0.5))
#define GV ((int)(-0.368*(1<<RGB2YUV_SHIFT)+0.5))
#define GU ((int)(-0.291*(1<<RGB2YUV_SHIFT)+0.5))
#define RY ((int)( 0.257*(1<<RGB2YUV_SHIFT)+0.5))
#define RV ((int)( 0.439*(1<<RGB2YUV_SHIFT)+0.5))
#define RU ((int)(-0.148*(1<<RGB2YUV_SHIFT)+0.5))

extern const int32_t Inverse_Table_6_9[8][4];

/*
NOTES
Special versions: fast Y 1:1 scaling (no interpolation in y direction)

TODO
more intelligent missalignment avoidance for the horizontal scaler
write special vertical cubic upscale version
Optimize C code (yv12 / minmax)
add support for packed pixel yuv input & output
add support for Y8 output
optimize bgr24 & bgr32
add BGR4 output support
write special BGR->BGR scaler
*/

#if defined(ARCH_X86) && defined (CONFIG_GPL)
static uint64_t attribute_used __attribute__((aligned(8))) bF8=       0xF8F8F8F8F8F8F8F8LL;
static uint64_t attribute_used __attribute__((aligned(8))) bFC=       0xFCFCFCFCFCFCFCFCLL;
static uint64_t __attribute__((aligned(8))) w10=       0x0010001000100010LL;
static uint64_t attribute_used __attribute__((aligned(8))) w02=       0x0002000200020002LL;
static uint64_t attribute_used __attribute__((aligned(8))) bm00001111=0x00000000FFFFFFFFLL;
static uint64_t attribute_used __attribute__((aligned(8))) bm00000111=0x0000000000FFFFFFLL;
static uint64_t attribute_used __attribute__((aligned(8))) bm11111000=0xFFFFFFFFFF000000LL;
static uint64_t attribute_used __attribute__((aligned(8))) bm01010101=0x00FF00FF00FF00FFLL;

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

static uint64_t __attribute__((aligned(8))) b16Mask=   0x001F001F001F001FLL;
static uint64_t attribute_used __attribute__((aligned(8))) g16Mask=   0x07E007E007E007E0LL;
static uint64_t attribute_used __attribute__((aligned(8))) r16Mask=   0xF800F800F800F800LL;
static uint64_t __attribute__((aligned(8))) b15Mask=   0x001F001F001F001FLL;
static uint64_t attribute_used __attribute__((aligned(8))) g15Mask=   0x03E003E003E003E0LL;
static uint64_t attribute_used __attribute__((aligned(8))) r15Mask=   0x7C007C007C007C00LL;

static uint64_t attribute_used __attribute__((aligned(8))) M24A=   0x00FF0000FF0000FFLL;
static uint64_t attribute_used __attribute__((aligned(8))) M24B=   0xFF0000FF0000FF00LL;
static uint64_t attribute_used __attribute__((aligned(8))) M24C=   0x0000FF0000FF0000LL;

#ifdef FAST_BGR2YV12
static const uint64_t bgr2YCoeff  attribute_used __attribute__((aligned(8))) = 0x000000210041000DULL;
static const uint64_t bgr2UCoeff  attribute_used __attribute__((aligned(8))) = 0x0000FFEEFFDC0038ULL;
static const uint64_t bgr2VCoeff  attribute_used __attribute__((aligned(8))) = 0x00000038FFD2FFF8ULL;
#else
static const uint64_t bgr2YCoeff  attribute_used __attribute__((aligned(8))) = 0x000020E540830C8BULL;
static const uint64_t bgr2UCoeff  attribute_used __attribute__((aligned(8))) = 0x0000ED0FDAC23831ULL;
static const uint64_t bgr2VCoeff  attribute_used __attribute__((aligned(8))) = 0x00003831D0E6F6EAULL;
#endif /* FAST_BGR2YV12 */
static const uint64_t bgr2YOffset attribute_used __attribute__((aligned(8))) = 0x1010101010101010ULL;
static const uint64_t bgr2UVOffset attribute_used __attribute__((aligned(8)))= 0x8080808080808080ULL;
static const uint64_t w1111       attribute_used __attribute__((aligned(8))) = 0x0001000100010001ULL;
#endif /* defined(ARCH_X86) */

// clipping helper table for C implementations:
static unsigned char clip_table[768];

static SwsVector *sws_getConvVec(SwsVector *a, SwsVector *b);
		  
extern const uint8_t dither_2x2_4[2][8];
extern const uint8_t dither_2x2_8[2][8];
extern const uint8_t dither_8x8_32[8][8];
extern const uint8_t dither_8x8_73[8][8];
extern const uint8_t dither_8x8_220[8][8];

static const char * sws_context_to_name(void * ptr) {
    return "swscaler";
}

static AVClass sws_context_class = { "SWScaler", sws_context_to_name, NULL };

char *sws_format_name(enum PixelFormat format)
{
    switch (format) {
        case PIX_FMT_YUV420P:
            return "yuv420p";
        case PIX_FMT_YUYV422:
            return "yuyv422";
        case PIX_FMT_RGB24:
            return "rgb24";
        case PIX_FMT_BGR24:
            return "bgr24";
        case PIX_FMT_YUV422P:
            return "yuv422p";
        case PIX_FMT_YUV444P:
            return "yuv444p";
        case PIX_FMT_RGB32:
            return "rgb32";
        case PIX_FMT_YUV410P:
            return "yuv410p";
        case PIX_FMT_YUV411P:
            return "yuv411p";
        case PIX_FMT_RGB565:
            return "rgb565";
        case PIX_FMT_RGB555:
            return "rgb555";
        case PIX_FMT_GRAY16BE:
            return "gray16be";
        case PIX_FMT_GRAY16LE:
            return "gray16le";
        case PIX_FMT_GRAY8:
            return "gray8";
        case PIX_FMT_MONOWHITE:
            return "mono white";
        case PIX_FMT_MONOBLACK:
            return "mono black";
        case PIX_FMT_PAL8:
            return "Palette";
        case PIX_FMT_YUVJ420P:
            return "yuvj420p";
        case PIX_FMT_YUVJ422P:
            return "yuvj422p";
        case PIX_FMT_YUVJ444P:
            return "yuvj444p";
        case PIX_FMT_XVMC_MPEG2_MC:
            return "xvmc_mpeg2_mc";
        case PIX_FMT_XVMC_MPEG2_IDCT:
            return "xvmc_mpeg2_idct";
        case PIX_FMT_UYVY422:
            return "uyvy422";
        case PIX_FMT_UYYVYY411:
            return "uyyvyy411";
        case PIX_FMT_RGB32_1:
            return "rgb32x";
        case PIX_FMT_BGR32_1:
            return "bgr32x";
        case PIX_FMT_BGR32:
            return "bgr32";
        case PIX_FMT_BGR565:
            return "bgr565";
        case PIX_FMT_BGR555:
            return "bgr555";
        case PIX_FMT_BGR8:
            return "bgr8";
        case PIX_FMT_BGR4:
            return "bgr4";
        case PIX_FMT_BGR4_BYTE:
            return "bgr4 byte";
        case PIX_FMT_RGB8:
            return "rgb8";
        case PIX_FMT_RGB4:
            return "rgb4";
        case PIX_FMT_RGB4_BYTE:
            return "rgb4 byte";
        case PIX_FMT_NV12:
            return "nv12";
        case PIX_FMT_NV21:
            return "nv21";
        default:
            return "Unknown format";
    }
}

#if defined(ARCH_X86) && defined (CONFIG_GPL)
void in_asm_used_var_warning_killer()
{
 volatile int i= bF8+bFC+w10+
 bm00001111+bm00000111+bm11111000+b16Mask+g16Mask+r16Mask+b15Mask+g15Mask+r15Mask+
 M24A+M24B+M24C+w02 + b5Dither+g5Dither+r5Dither+g6Dither+dither4[0]+dither8[0]+bm01010101;
 if(i) i=0;
}
#endif

static inline void yuv2yuvXinC(int16_t *lumFilter, int16_t **lumSrc, int lumFilterSize,
				    int16_t *chrFilter, int16_t **chrSrc, int chrFilterSize,
				    uint8_t *dest, uint8_t *uDest, uint8_t *vDest, int dstW, int chrDstW)
{
	//FIXME Optimize (just quickly writen not opti..)
	int i;
	for(i=0; i<dstW; i++)
	{
		int val=1<<18;
		int j;
		for(j=0; j<lumFilterSize; j++)
			val += lumSrc[j][i] * lumFilter[j];

		dest[i]= av_clip_uint8(val>>19);
	}

	if(uDest != NULL)
		for(i=0; i<chrDstW; i++)
		{
			int u=1<<18;
			int v=1<<18;
			int j;
			for(j=0; j<chrFilterSize; j++)
			{
				u += chrSrc[j][i] * chrFilter[j];
				v += chrSrc[j][i + 2048] * chrFilter[j];
			}

			uDest[i]= av_clip_uint8(u>>19);
			vDest[i]= av_clip_uint8(v>>19);
		}
}

static inline void yuv2nv12XinC(int16_t *lumFilter, int16_t **lumSrc, int lumFilterSize,
				int16_t *chrFilter, int16_t **chrSrc, int chrFilterSize,
				uint8_t *dest, uint8_t *uDest, int dstW, int chrDstW, int dstFormat)
{
	//FIXME Optimize (just quickly writen not opti..)
	int i;
	for(i=0; i<dstW; i++)
	{
		int val=1<<18;
		int j;
		for(j=0; j<lumFilterSize; j++)
			val += lumSrc[j][i] * lumFilter[j];

		dest[i]= av_clip_uint8(val>>19);
	}

	if(uDest == NULL)
		return;

	if(dstFormat == PIX_FMT_NV12)
		for(i=0; i<chrDstW; i++)
		{
			int u=1<<18;
			int v=1<<18;
			int j;
			for(j=0; j<chrFilterSize; j++)
			{
				u += chrSrc[j][i] * chrFilter[j];
				v += chrSrc[j][i + 2048] * chrFilter[j];
			}

			uDest[2*i]= av_clip_uint8(u>>19);
			uDest[2*i+1]= av_clip_uint8(v>>19);
		}
	else
		for(i=0; i<chrDstW; i++)
		{
			int u=1<<18;
			int v=1<<18;
			int j;
			for(j=0; j<chrFilterSize; j++)
			{
				u += chrSrc[j][i] * chrFilter[j];
				v += chrSrc[j][i + 2048] * chrFilter[j];
			}

			uDest[2*i]= av_clip_uint8(v>>19);
			uDest[2*i+1]= av_clip_uint8(u>>19);
		}
}

#define YSCALE_YUV_2_PACKEDX_C(type) \
		for(i=0; i<(dstW>>1); i++){\
			int j;\
			int Y1=1<<18;\
			int Y2=1<<18;\
			int U=1<<18;\
			int V=1<<18;\
			type attribute_unused *r, *b, *g;\
			const int i2= 2*i;\
			\
			for(j=0; j<lumFilterSize; j++)\
			{\
				Y1 += lumSrc[j][i2] * lumFilter[j];\
				Y2 += lumSrc[j][i2+1] * lumFilter[j];\
			}\
			for(j=0; j<chrFilterSize; j++)\
			{\
				U += chrSrc[j][i] * chrFilter[j];\
				V += chrSrc[j][i+2048] * chrFilter[j];\
			}\
			Y1>>=19;\
			Y2>>=19;\
			U >>=19;\
			V >>=19;\
			if((Y1|Y2|U|V)&256)\
			{\
				if(Y1>255)   Y1=255;\
				else if(Y1<0)Y1=0;\
				if(Y2>255)   Y2=255;\
				else if(Y2<0)Y2=0;\
				if(U>255)    U=255;\
				else if(U<0) U=0;\
				if(V>255)    V=255;\
				else if(V<0) V=0;\
			}
                        
#define YSCALE_YUV_2_RGBX_C(type) \
			YSCALE_YUV_2_PACKEDX_C(type)\
			r = (type *)c->table_rV[V];\
			g = (type *)(c->table_gU[U] + c->table_gV[V]);\
			b = (type *)c->table_bU[U];\

#define YSCALE_YUV_2_PACKED2_C \
		for(i=0; i<(dstW>>1); i++){\
			const int i2= 2*i;\
			int Y1= (buf0[i2  ]*yalpha1+buf1[i2  ]*yalpha)>>19;\
			int Y2= (buf0[i2+1]*yalpha1+buf1[i2+1]*yalpha)>>19;\
			int U= (uvbuf0[i     ]*uvalpha1+uvbuf1[i     ]*uvalpha)>>19;\
			int V= (uvbuf0[i+2048]*uvalpha1+uvbuf1[i+2048]*uvalpha)>>19;\

#define YSCALE_YUV_2_RGB2_C(type) \
			YSCALE_YUV_2_PACKED2_C\
			type *r, *b, *g;\
			r = (type *)c->table_rV[V];\
			g = (type *)(c->table_gU[U] + c->table_gV[V]);\
			b = (type *)c->table_bU[U];\

#define YSCALE_YUV_2_PACKED1_C \
		for(i=0; i<(dstW>>1); i++){\
			const int i2= 2*i;\
			int Y1= buf0[i2  ]>>7;\
			int Y2= buf0[i2+1]>>7;\
			int U= (uvbuf1[i     ])>>7;\
			int V= (uvbuf1[i+2048])>>7;\

#define YSCALE_YUV_2_RGB1_C(type) \
			YSCALE_YUV_2_PACKED1_C\
			type *r, *b, *g;\
			r = (type *)c->table_rV[V];\
			g = (type *)(c->table_gU[U] + c->table_gV[V]);\
			b = (type *)c->table_bU[U];\

#define YSCALE_YUV_2_PACKED1B_C \
		for(i=0; i<(dstW>>1); i++){\
			const int i2= 2*i;\
			int Y1= buf0[i2  ]>>7;\
			int Y2= buf0[i2+1]>>7;\
			int U= (uvbuf0[i     ] + uvbuf1[i     ])>>8;\
			int V= (uvbuf0[i+2048] + uvbuf1[i+2048])>>8;\

#define YSCALE_YUV_2_RGB1B_C(type) \
			YSCALE_YUV_2_PACKED1B_C\
			type *r, *b, *g;\
			r = (type *)c->table_rV[V];\
			g = (type *)(c->table_gU[U] + c->table_gV[V]);\
			b = (type *)c->table_bU[U];\

#define YSCALE_YUV_2_ANYRGB_C(func, func2)\
	switch(c->dstFormat)\
	{\
	case PIX_FMT_RGB32:\
	case PIX_FMT_BGR32:\
		func(uint32_t)\
			((uint32_t*)dest)[i2+0]= r[Y1] + g[Y1] + b[Y1];\
			((uint32_t*)dest)[i2+1]= r[Y2] + g[Y2] + b[Y2];\
		}		\
		break;\
	case PIX_FMT_RGB24:\
		func(uint8_t)\
			((uint8_t*)dest)[0]= r[Y1];\
			((uint8_t*)dest)[1]= g[Y1];\
			((uint8_t*)dest)[2]= b[Y1];\
			((uint8_t*)dest)[3]= r[Y2];\
			((uint8_t*)dest)[4]= g[Y2];\
			((uint8_t*)dest)[5]= b[Y2];\
			dest+=6;\
		}\
		break;\
	case PIX_FMT_BGR24:\
		func(uint8_t)\
			((uint8_t*)dest)[0]= b[Y1];\
			((uint8_t*)dest)[1]= g[Y1];\
			((uint8_t*)dest)[2]= r[Y1];\
			((uint8_t*)dest)[3]= b[Y2];\
			((uint8_t*)dest)[4]= g[Y2];\
			((uint8_t*)dest)[5]= r[Y2];\
			dest+=6;\
		}\
		break;\
	case PIX_FMT_RGB565:\
	case PIX_FMT_BGR565:\
		{\
			const int dr1= dither_2x2_8[y&1    ][0];\
			const int dg1= dither_2x2_4[y&1    ][0];\
			const int db1= dither_2x2_8[(y&1)^1][0];\
			const int dr2= dither_2x2_8[y&1    ][1];\
			const int dg2= dither_2x2_4[y&1    ][1];\
			const int db2= dither_2x2_8[(y&1)^1][1];\
			func(uint16_t)\
				((uint16_t*)dest)[i2+0]= r[Y1+dr1] + g[Y1+dg1] + b[Y1+db1];\
				((uint16_t*)dest)[i2+1]= r[Y2+dr2] + g[Y2+dg2] + b[Y2+db2];\
			}\
		}\
		break;\
	case PIX_FMT_RGB555:\
	case PIX_FMT_BGR555:\
		{\
			const int dr1= dither_2x2_8[y&1    ][0];\
			const int dg1= dither_2x2_8[y&1    ][1];\
			const int db1= dither_2x2_8[(y&1)^1][0];\
			const int dr2= dither_2x2_8[y&1    ][1];\
			const int dg2= dither_2x2_8[y&1    ][0];\
			const int db2= dither_2x2_8[(y&1)^1][1];\
			func(uint16_t)\
				((uint16_t*)dest)[i2+0]= r[Y1+dr1] + g[Y1+dg1] + b[Y1+db1];\
				((uint16_t*)dest)[i2+1]= r[Y2+dr2] + g[Y2+dg2] + b[Y2+db2];\
			}\
		}\
		break;\
	case PIX_FMT_RGB8:\
	case PIX_FMT_BGR8:\
		{\
			const uint8_t * const d64= dither_8x8_73[y&7];\
			const uint8_t * const d32= dither_8x8_32[y&7];\
			func(uint8_t)\
				((uint8_t*)dest)[i2+0]= r[Y1+d32[(i2+0)&7]] + g[Y1+d32[(i2+0)&7]] + b[Y1+d64[(i2+0)&7]];\
				((uint8_t*)dest)[i2+1]= r[Y2+d32[(i2+1)&7]] + g[Y2+d32[(i2+1)&7]] + b[Y2+d64[(i2+1)&7]];\
			}\
		}\
		break;\
	case PIX_FMT_RGB4:\
	case PIX_FMT_BGR4:\
		{\
			const uint8_t * const d64= dither_8x8_73 [y&7];\
			const uint8_t * const d128=dither_8x8_220[y&7];\
			func(uint8_t)\
				((uint8_t*)dest)[i]= r[Y1+d128[(i2+0)&7]] + g[Y1+d64[(i2+0)&7]] + b[Y1+d128[(i2+0)&7]]\
				                 + ((r[Y2+d128[(i2+1)&7]] + g[Y2+d64[(i2+1)&7]] + b[Y2+d128[(i2+1)&7]])<<4);\
			}\
		}\
		break;\
	case PIX_FMT_RGB4_BYTE:\
	case PIX_FMT_BGR4_BYTE:\
		{\
			const uint8_t * const d64= dither_8x8_73 [y&7];\
			const uint8_t * const d128=dither_8x8_220[y&7];\
			func(uint8_t)\
				((uint8_t*)dest)[i2+0]= r[Y1+d128[(i2+0)&7]] + g[Y1+d64[(i2+0)&7]] + b[Y1+d128[(i2+0)&7]];\
				((uint8_t*)dest)[i2+1]= r[Y2+d128[(i2+1)&7]] + g[Y2+d64[(i2+1)&7]] + b[Y2+d128[(i2+1)&7]];\
			}\
		}\
		break;\
	case PIX_FMT_MONOBLACK:\
		{\
			const uint8_t * const d128=dither_8x8_220[y&7];\
			uint8_t *g= c->table_gU[128] + c->table_gV[128];\
			for(i=0; i<dstW-7; i+=8){\
				int acc;\
				acc =       g[((buf0[i  ]*yalpha1+buf1[i  ]*yalpha)>>19) + d128[0]];\
				acc+= acc + g[((buf0[i+1]*yalpha1+buf1[i+1]*yalpha)>>19) + d128[1]];\
				acc+= acc + g[((buf0[i+2]*yalpha1+buf1[i+2]*yalpha)>>19) + d128[2]];\
				acc+= acc + g[((buf0[i+3]*yalpha1+buf1[i+3]*yalpha)>>19) + d128[3]];\
				acc+= acc + g[((buf0[i+4]*yalpha1+buf1[i+4]*yalpha)>>19) + d128[4]];\
				acc+= acc + g[((buf0[i+5]*yalpha1+buf1[i+5]*yalpha)>>19) + d128[5]];\
				acc+= acc + g[((buf0[i+6]*yalpha1+buf1[i+6]*yalpha)>>19) + d128[6]];\
				acc+= acc + g[((buf0[i+7]*yalpha1+buf1[i+7]*yalpha)>>19) + d128[7]];\
				((uint8_t*)dest)[0]= acc;\
				dest++;\
			}\
\
/*\
((uint8_t*)dest)-= dstW>>4;\
{\
			int acc=0;\
			int left=0;\
			static int top[1024];\
			static int last_new[1024][1024];\
			static int last_in3[1024][1024];\
			static int drift[1024][1024];\
			int topLeft=0;\
			int shift=0;\
			int count=0;\
			const uint8_t * const d128=dither_8x8_220[y&7];\
			int error_new=0;\
			int error_in3=0;\
			int f=0;\
			\
			for(i=dstW>>1; i<dstW; i++){\
				int in= ((buf0[i  ]*yalpha1+buf1[i  ]*yalpha)>>19);\
				int in2 = (76309 * (in - 16) + 32768) >> 16;\
				int in3 = (in2 < 0) ? 0 : ((in2 > 255) ? 255 : in2);\
				int old= (left*7 + topLeft + top[i]*5 + top[i+1]*3)/20 + in3\
					+ (last_new[y][i] - in3)*f/256;\
				int new= old> 128 ? 255 : 0;\
\
				error_new+= FFABS(last_new[y][i] - new);\
				error_in3+= FFABS(last_in3[y][i] - in3);\
				f= error_new - error_in3*4;\
				if(f<0) f=0;\
				if(f>256) f=256;\
\
				topLeft= top[i];\
				left= top[i]= old - new;\
				last_new[y][i]= new;\
				last_in3[y][i]= in3;\
\
				acc+= acc + (new&1);\
				if((i&7)==6){\
					((uint8_t*)dest)[0]= acc;\
					((uint8_t*)dest)++;\
				}\
			}\
}\
*/\
		}\
		break;\
	case PIX_FMT_YUYV422:\
		func2\
			((uint8_t*)dest)[2*i2+0]= Y1;\
			((uint8_t*)dest)[2*i2+1]= U;\
			((uint8_t*)dest)[2*i2+2]= Y2;\
			((uint8_t*)dest)[2*i2+3]= V;\
		}		\
		break;\
	case PIX_FMT_UYVY422:\
		func2\
			((uint8_t*)dest)[2*i2+0]= U;\
			((uint8_t*)dest)[2*i2+1]= Y1;\
			((uint8_t*)dest)[2*i2+2]= V;\
			((uint8_t*)dest)[2*i2+3]= Y2;\
		}		\
		break;\
	}\


static inline void yuv2packedXinC(SwsContext *c, int16_t *lumFilter, int16_t **lumSrc, int lumFilterSize,
				    int16_t *chrFilter, int16_t **chrSrc, int chrFilterSize,
				    uint8_t *dest, int dstW, int y)
{
	int i;
	switch(c->dstFormat)
	{
	case PIX_FMT_BGR32:
	case PIX_FMT_RGB32:
		YSCALE_YUV_2_RGBX_C(uint32_t)
			((uint32_t*)dest)[i2+0]= r[Y1] + g[Y1] + b[Y1];
			((uint32_t*)dest)[i2+1]= r[Y2] + g[Y2] + b[Y2];
		}
		break;
	case PIX_FMT_RGB24:
		YSCALE_YUV_2_RGBX_C(uint8_t)
			((uint8_t*)dest)[0]= r[Y1];
			((uint8_t*)dest)[1]= g[Y1];
			((uint8_t*)dest)[2]= b[Y1];
			((uint8_t*)dest)[3]= r[Y2];
			((uint8_t*)dest)[4]= g[Y2];
			((uint8_t*)dest)[5]= b[Y2];
			dest+=6;
		}
		break;
	case PIX_FMT_BGR24:
		YSCALE_YUV_2_RGBX_C(uint8_t)
			((uint8_t*)dest)[0]= b[Y1];
			((uint8_t*)dest)[1]= g[Y1];
			((uint8_t*)dest)[2]= r[Y1];
			((uint8_t*)dest)[3]= b[Y2];
			((uint8_t*)dest)[4]= g[Y2];
			((uint8_t*)dest)[5]= r[Y2];
			dest+=6;
		}
		break;
	case PIX_FMT_RGB565:
	case PIX_FMT_BGR565:
		{
			const int dr1= dither_2x2_8[y&1    ][0];
			const int dg1= dither_2x2_4[y&1    ][0];
			const int db1= dither_2x2_8[(y&1)^1][0];
			const int dr2= dither_2x2_8[y&1    ][1];
			const int dg2= dither_2x2_4[y&1    ][1];
			const int db2= dither_2x2_8[(y&1)^1][1];
			YSCALE_YUV_2_RGBX_C(uint16_t)
				((uint16_t*)dest)[i2+0]= r[Y1+dr1] + g[Y1+dg1] + b[Y1+db1];
				((uint16_t*)dest)[i2+1]= r[Y2+dr2] + g[Y2+dg2] + b[Y2+db2];
			}
		}
		break;
	case PIX_FMT_RGB555:
	case PIX_FMT_BGR555:
		{
			const int dr1= dither_2x2_8[y&1    ][0];
			const int dg1= dither_2x2_8[y&1    ][1];
			const int db1= dither_2x2_8[(y&1)^1][0];
			const int dr2= dither_2x2_8[y&1    ][1];
			const int dg2= dither_2x2_8[y&1    ][0];
			const int db2= dither_2x2_8[(y&1)^1][1];
			YSCALE_YUV_2_RGBX_C(uint16_t)
				((uint16_t*)dest)[i2+0]= r[Y1+dr1] + g[Y1+dg1] + b[Y1+db1];
				((uint16_t*)dest)[i2+1]= r[Y2+dr2] + g[Y2+dg2] + b[Y2+db2];
			}
		}
		break;
	case PIX_FMT_RGB8:
	case PIX_FMT_BGR8:
		{
			const uint8_t * const d64= dither_8x8_73[y&7];
			const uint8_t * const d32= dither_8x8_32[y&7];
			YSCALE_YUV_2_RGBX_C(uint8_t)
				((uint8_t*)dest)[i2+0]= r[Y1+d32[(i2+0)&7]] + g[Y1+d32[(i2+0)&7]] + b[Y1+d64[(i2+0)&7]];
				((uint8_t*)dest)[i2+1]= r[Y2+d32[(i2+1)&7]] + g[Y2+d32[(i2+1)&7]] + b[Y2+d64[(i2+1)&7]];
			}
		}
		break;
	case PIX_FMT_RGB4:
	case PIX_FMT_BGR4:
		{
			const uint8_t * const d64= dither_8x8_73 [y&7];
			const uint8_t * const d128=dither_8x8_220[y&7];
			YSCALE_YUV_2_RGBX_C(uint8_t)
				((uint8_t*)dest)[i]= r[Y1+d128[(i2+0)&7]] + g[Y1+d64[(i2+0)&7]] + b[Y1+d128[(i2+0)&7]]
				                  +((r[Y2+d128[(i2+1)&7]] + g[Y2+d64[(i2+1)&7]] + b[Y2+d128[(i2+1)&7]])<<4);
			}
		}
		break;
	case PIX_FMT_RGB4_BYTE:
	case PIX_FMT_BGR4_BYTE:
		{
			const uint8_t * const d64= dither_8x8_73 [y&7];
			const uint8_t * const d128=dither_8x8_220[y&7];
			YSCALE_YUV_2_RGBX_C(uint8_t)
				((uint8_t*)dest)[i2+0]= r[Y1+d128[(i2+0)&7]] + g[Y1+d64[(i2+0)&7]] + b[Y1+d128[(i2+0)&7]];
				((uint8_t*)dest)[i2+1]= r[Y2+d128[(i2+1)&7]] + g[Y2+d64[(i2+1)&7]] + b[Y2+d128[(i2+1)&7]];
			}
		}
		break;
	case PIX_FMT_MONOBLACK:
		{
			const uint8_t * const d128=dither_8x8_220[y&7];
			uint8_t *g= c->table_gU[128] + c->table_gV[128];
			int acc=0;
			for(i=0; i<dstW-1; i+=2){
				int j;
				int Y1=1<<18;
				int Y2=1<<18;

				for(j=0; j<lumFilterSize; j++)
				{
					Y1 += lumSrc[j][i] * lumFilter[j];
					Y2 += lumSrc[j][i+1] * lumFilter[j];
				}
				Y1>>=19;
				Y2>>=19;
				if((Y1|Y2)&256)
				{
					if(Y1>255)   Y1=255;
					else if(Y1<0)Y1=0;
					if(Y2>255)   Y2=255;
					else if(Y2<0)Y2=0;
				}
				acc+= acc + g[Y1+d128[(i+0)&7]];
				acc+= acc + g[Y2+d128[(i+1)&7]];
				if((i&7)==6){
					((uint8_t*)dest)[0]= acc;
					dest++;
				}
			}
		}
		break;
	case PIX_FMT_YUYV422:
		YSCALE_YUV_2_PACKEDX_C(void)
			((uint8_t*)dest)[2*i2+0]= Y1;
			((uint8_t*)dest)[2*i2+1]= U;
			((uint8_t*)dest)[2*i2+2]= Y2;
			((uint8_t*)dest)[2*i2+3]= V;
		}
                break;
	case PIX_FMT_UYVY422:
		YSCALE_YUV_2_PACKEDX_C(void)
			((uint8_t*)dest)[2*i2+0]= U;
			((uint8_t*)dest)[2*i2+1]= Y1;
			((uint8_t*)dest)[2*i2+2]= V;
			((uint8_t*)dest)[2*i2+3]= Y2;
		}
                break;
	}
}


//Note: we have C, X86, MMX, MMX2, 3DNOW version therse no 3DNOW+MMX2 one
//Plain C versions
#if !defined (HAVE_MMX) || defined (RUNTIME_CPUDETECT) || !defined(CONFIG_GPL)
#define COMPILE_C
#endif

#ifdef ARCH_POWERPC
#if (defined (HAVE_ALTIVEC) || defined (RUNTIME_CPUDETECT)) && defined (CONFIG_GPL)
#define COMPILE_ALTIVEC
#endif //HAVE_ALTIVEC
#endif //ARCH_POWERPC

#if defined(ARCH_X86)

#if ((defined (HAVE_MMX) && !defined (HAVE_3DNOW) && !defined (HAVE_MMX2)) || defined (RUNTIME_CPUDETECT)) && defined (CONFIG_GPL)
#define COMPILE_MMX
#endif

#if (defined (HAVE_MMX2) || defined (RUNTIME_CPUDETECT)) && defined (CONFIG_GPL)
#define COMPILE_MMX2
#endif

#if ((defined (HAVE_3DNOW) && !defined (HAVE_MMX2)) || defined (RUNTIME_CPUDETECT)) && defined (CONFIG_GPL)
#define COMPILE_3DNOW
#endif
#endif //ARCH_X86 || ARCH_X86_64

#undef HAVE_MMX
#undef HAVE_MMX2
#undef HAVE_3DNOW

#ifdef COMPILE_C
#undef HAVE_MMX
#undef HAVE_MMX2
#undef HAVE_3DNOW
#undef HAVE_ALTIVEC
#define RENAME(a) a ## _C
#include "swscale_template.c"
#endif

#ifdef ARCH_POWERPC
#ifdef COMPILE_ALTIVEC
#undef RENAME
#define HAVE_ALTIVEC
#define RENAME(a) a ## _altivec
#include "swscale_template.c"
#endif
#endif //ARCH_POWERPC

#if defined(ARCH_X86)

//X86 versions
/*
#undef RENAME
#undef HAVE_MMX
#undef HAVE_MMX2
#undef HAVE_3DNOW
#define ARCH_X86
#define RENAME(a) a ## _X86
#include "swscale_template.c"
*/
//MMX versions
#ifdef COMPILE_MMX
#undef RENAME
#define HAVE_MMX
#undef HAVE_MMX2
#undef HAVE_3DNOW
#define RENAME(a) a ## _MMX
#include "swscale_template.c"
#endif

//MMX2 versions
#ifdef COMPILE_MMX2
#undef RENAME
#define HAVE_MMX
#define HAVE_MMX2
#undef HAVE_3DNOW
#define RENAME(a) a ## _MMX2
#include "swscale_template.c"
#endif

//3DNOW versions
#ifdef COMPILE_3DNOW
#undef RENAME
#define HAVE_MMX
#undef HAVE_MMX2
#define HAVE_3DNOW
#define RENAME(a) a ## _3DNow
#include "swscale_template.c"
#endif

#endif //ARCH_X86 || ARCH_X86_64

// minor note: the HAVE_xyz is messed up after that line so don't use it

static double getSplineCoeff(double a, double b, double c, double d, double dist)
{
//	printf("%f %f %f %f %f\n", a,b,c,d,dist);
	if(dist<=1.0) 	return ((d*dist + c)*dist + b)*dist +a;
	else		return getSplineCoeff(	0.0, 
						 b+ 2.0*c + 3.0*d,
						        c + 3.0*d,
						-b- 3.0*c - 6.0*d,
						dist-1.0);
}

static inline int initFilter(int16_t **outFilter, int16_t **filterPos, int *outFilterSize, int xInc,
			      int srcW, int dstW, int filterAlign, int one, int flags,
			      SwsVector *srcFilter, SwsVector *dstFilter, double param[2])
{
	int i;
	int filterSize;
	int filter2Size;
	int minFilterSize;
	double *filter=NULL;
	double *filter2=NULL;
#if defined(ARCH_X86)
	if(flags & SWS_CPU_CAPS_MMX)
		asm volatile("emms\n\t"::: "memory"); //FIXME this shouldnt be required but it IS (even for non mmx versions)
#endif

	// Note the +1 is for the MMXscaler which reads over the end
	*filterPos = av_malloc((dstW+1)*sizeof(int16_t));

	if(FFABS(xInc - 0x10000) <10) // unscaled
	{
		int i;
		filterSize= 1;
		filter= av_malloc(dstW*sizeof(double)*filterSize);
		for(i=0; i<dstW*filterSize; i++) filter[i]=0;

		for(i=0; i<dstW; i++)
		{
			filter[i*filterSize]=1;
			(*filterPos)[i]=i;
		}

	}
	else if(flags&SWS_POINT) // lame looking point sampling mode
	{
		int i;
		int xDstInSrc;
		filterSize= 1;
		filter= av_malloc(dstW*sizeof(double)*filterSize);
		
		xDstInSrc= xInc/2 - 0x8000;
		for(i=0; i<dstW; i++)
		{
			int xx= (xDstInSrc - ((filterSize-1)<<15) + (1<<15))>>16;

			(*filterPos)[i]= xx;
			filter[i]= 1.0;
			xDstInSrc+= xInc;
		}
	}
	else if((xInc <= (1<<16) && (flags&SWS_AREA)) || (flags&SWS_FAST_BILINEAR)) // bilinear upscale
	{
		int i;
		int xDstInSrc;
		if     (flags&SWS_BICUBIC) filterSize= 4;
		else if(flags&SWS_X      ) filterSize= 4;
		else			   filterSize= 2; // SWS_BILINEAR / SWS_AREA 
		filter= av_malloc(dstW*sizeof(double)*filterSize);

		xDstInSrc= xInc/2 - 0x8000;
		for(i=0; i<dstW; i++)
		{
			int xx= (xDstInSrc - ((filterSize-1)<<15) + (1<<15))>>16;
			int j;

			(*filterPos)[i]= xx;
				//Bilinear upscale / linear interpolate / Area averaging
				for(j=0; j<filterSize; j++)
				{
					double d= FFABS((xx<<16) - xDstInSrc)/(double)(1<<16);
					double coeff= 1.0 - d;
					if(coeff<0) coeff=0;
					filter[i*filterSize + j]= coeff;
					xx++;
				}
			xDstInSrc+= xInc;
		}
	}
	else
	{
		double xDstInSrc;
		double sizeFactor, filterSizeInSrc;
		const double xInc1= (double)xInc / (double)(1<<16);

		if     (flags&SWS_BICUBIC)	sizeFactor= 4.0;
		else if(flags&SWS_X)		sizeFactor= 8.0;
		else if(flags&SWS_AREA)		sizeFactor= 1.0; //downscale only, for upscale it is bilinear
		else if(flags&SWS_GAUSS)	sizeFactor= 8.0;   // infinite ;)
		else if(flags&SWS_LANCZOS)	sizeFactor= param[0] != SWS_PARAM_DEFAULT ? 2.0*param[0] : 6.0;
		else if(flags&SWS_SINC)		sizeFactor= 20.0; // infinite ;)
		else if(flags&SWS_SPLINE)	sizeFactor= 20.0;  // infinite ;)
		else if(flags&SWS_BILINEAR)	sizeFactor= 2.0;
		else {
			sizeFactor= 0.0; //GCC warning killer
			ASSERT(0)
		}
		
		if(xInc1 <= 1.0)	filterSizeInSrc= sizeFactor; // upscale
		else			filterSizeInSrc= sizeFactor*srcW / (double)dstW;

		filterSize= (int)ceil(1 + filterSizeInSrc); // will be reduced later if possible
		if(filterSize > srcW-2) filterSize=srcW-2;

		filter= av_malloc(dstW*sizeof(double)*filterSize);

		xDstInSrc= xInc1 / 2.0 - 0.5;
		for(i=0; i<dstW; i++)
		{
			int xx= (int)(xDstInSrc - (filterSize-1)*0.5 + 0.5);
			int j;
			(*filterPos)[i]= xx;
			for(j=0; j<filterSize; j++)
			{
				double d= FFABS(xx - xDstInSrc)/filterSizeInSrc*sizeFactor;
				double coeff;
				if(flags & SWS_BICUBIC)
				{
					double B= param[0] != SWS_PARAM_DEFAULT ? param[0] : 0.0;
					double C= param[1] != SWS_PARAM_DEFAULT ? param[1] : 0.6;

					if(d<1.0) 
						coeff = (12-9*B-6*C)*d*d*d + (-18+12*B+6*C)*d*d + 6-2*B;
					else if(d<2.0)
						coeff = (-B-6*C)*d*d*d + (6*B+30*C)*d*d + (-12*B-48*C)*d +8*B+24*C;
					else
						coeff=0.0;
				}
/*				else if(flags & SWS_X)
				{
					double p= param ? param*0.01 : 0.3;
					coeff = d ? sin(d*PI)/(d*PI) : 1.0;
					coeff*= pow(2.0, - p*d*d);
				}*/
				else if(flags & SWS_X)
				{
					double A= param[0] != SWS_PARAM_DEFAULT ? param[0] : 1.0;
					
					if(d<1.0)
						coeff = cos(d*PI);
					else
						coeff=-1.0;
					if(coeff<0.0) 	coeff= -pow(-coeff, A);
					else		coeff=  pow( coeff, A);
					coeff= coeff*0.5 + 0.5;
				}
				else if(flags & SWS_AREA)
				{
					double srcPixelSize= 1.0/xInc1;
					if(d + srcPixelSize/2 < 0.5) coeff= 1.0;
					else if(d - srcPixelSize/2 < 0.5) coeff= (0.5-d)/srcPixelSize + 0.5;
					else coeff=0.0;
				}
				else if(flags & SWS_GAUSS)
				{
					double p= param[0] != SWS_PARAM_DEFAULT ? param[0] : 3.0;
					coeff = pow(2.0, - p*d*d);
				}
				else if(flags & SWS_SINC)
				{
					coeff = d ? sin(d*PI)/(d*PI) : 1.0;
				}
				else if(flags & SWS_LANCZOS)
				{
					double p= param[0] != SWS_PARAM_DEFAULT ? param[0] : 3.0; 
					coeff = d ? sin(d*PI)*sin(d*PI/p)/(d*d*PI*PI/p) : 1.0;
					if(d>p) coeff=0;
				}
				else if(flags & SWS_BILINEAR)
				{
					coeff= 1.0 - d;
					if(coeff<0) coeff=0;
				}
				else if(flags & SWS_SPLINE)
				{
					double p=-2.196152422706632;
					coeff = getSplineCoeff(1.0, 0.0, p, -p-1.0, d);
				}
				else {
					coeff= 0.0; //GCC warning killer
					ASSERT(0)
				}

				filter[i*filterSize + j]= coeff;
				xx++;
			}
			xDstInSrc+= xInc1;
		}
	}

	/* apply src & dst Filter to filter -> filter2
	   av_free(filter);
	*/
	ASSERT(filterSize>0)
	filter2Size= filterSize;
	if(srcFilter) filter2Size+= srcFilter->length - 1;
	if(dstFilter) filter2Size+= dstFilter->length - 1;
	ASSERT(filter2Size>0)
	filter2= av_malloc(filter2Size*dstW*sizeof(double));

	for(i=0; i<dstW; i++)
	{
		int j;
		SwsVector scaleFilter;
		SwsVector *outVec;

		scaleFilter.coeff= filter + i*filterSize;
		scaleFilter.length= filterSize;

		if(srcFilter) outVec= sws_getConvVec(srcFilter, &scaleFilter);
		else	      outVec= &scaleFilter;

		ASSERT(outVec->length == filter2Size)
		//FIXME dstFilter

		for(j=0; j<outVec->length; j++)
		{
			filter2[i*filter2Size + j]= outVec->coeff[j];
		}

		(*filterPos)[i]+= (filterSize-1)/2 - (filter2Size-1)/2;

		if(outVec != &scaleFilter) sws_freeVec(outVec);
	}
	av_free(filter); filter=NULL;

	/* try to reduce the filter-size (step1 find size and shift left) */
	// Assume its near normalized (*0.5 or *2.0 is ok but * 0.001 is not)
	minFilterSize= 0;
	for(i=dstW-1; i>=0; i--)
	{
		int min= filter2Size;
		int j;
		double cutOff=0.0;

		/* get rid off near zero elements on the left by shifting left */
		for(j=0; j<filter2Size; j++)
		{
			int k;
			cutOff += FFABS(filter2[i*filter2Size]);

			if(cutOff > SWS_MAX_REDUCE_CUTOFF) break;

			/* preserve Monotonicity because the core can't handle the filter otherwise */
			if(i<dstW-1 && (*filterPos)[i] >= (*filterPos)[i+1]) break;

			// Move filter coeffs left
			for(k=1; k<filter2Size; k++)
				filter2[i*filter2Size + k - 1]= filter2[i*filter2Size + k];
			filter2[i*filter2Size + k - 1]= 0.0;
			(*filterPos)[i]++;
		}

		cutOff=0.0;
		/* count near zeros on the right */
		for(j=filter2Size-1; j>0; j--)
		{
			cutOff += FFABS(filter2[i*filter2Size + j]);

			if(cutOff > SWS_MAX_REDUCE_CUTOFF) break;
			min--;
		}

		if(min>minFilterSize) minFilterSize= min;
	}

        if (flags & SWS_CPU_CAPS_ALTIVEC) {
          // we can handle the special case 4,
          // so we don't want to go to the full 8
          if (minFilterSize < 5)
            filterAlign = 4;

          // we really don't want to waste our time
          // doing useless computation, so fall-back on
          // the scalar C code for very small filter.
          // vectorizing is worth it only if you have
          // decent-sized vector.
          if (minFilterSize < 3)
            filterAlign = 1;
        }

        if (flags & SWS_CPU_CAPS_MMX) {
                // special case for unscaled vertical filtering
                if(minFilterSize == 1 && filterAlign == 2)
                        filterAlign= 1;
        }

	ASSERT(minFilterSize > 0)
	filterSize= (minFilterSize +(filterAlign-1)) & (~(filterAlign-1));
	ASSERT(filterSize > 0)
	filter= av_malloc(filterSize*dstW*sizeof(double));
        if(filterSize >= MAX_FILTER_SIZE)
                return -1;
	*outFilterSize= filterSize;

	if(flags&SWS_PRINT_INFO)
		av_log(NULL, AV_LOG_VERBOSE, "SwScaler: reducing / aligning filtersize %d -> %d\n", filter2Size, filterSize);
	/* try to reduce the filter-size (step2 reduce it) */
	for(i=0; i<dstW; i++)
	{
		int j;

		for(j=0; j<filterSize; j++)
		{
			if(j>=filter2Size) filter[i*filterSize + j]= 0.0;
			else		   filter[i*filterSize + j]= filter2[i*filter2Size + j];
		}
	}
	av_free(filter2); filter2=NULL;
	

	//FIXME try to align filterpos if possible

	//fix borders
	for(i=0; i<dstW; i++)
	{
		int j;
		if((*filterPos)[i] < 0)
		{
			// Move filter coeffs left to compensate for filterPos
			for(j=1; j<filterSize; j++)
			{
				int left= FFMAX(j + (*filterPos)[i], 0);
				filter[i*filterSize + left] += filter[i*filterSize + j];
				filter[i*filterSize + j]=0;
			}
			(*filterPos)[i]= 0;
		}

		if((*filterPos)[i] + filterSize > srcW)
		{
			int shift= (*filterPos)[i] + filterSize - srcW;
			// Move filter coeffs right to compensate for filterPos
			for(j=filterSize-2; j>=0; j--)
			{
				int right= FFMIN(j + shift, filterSize-1);
				filter[i*filterSize +right] += filter[i*filterSize +j];
				filter[i*filterSize +j]=0;
			}
			(*filterPos)[i]= srcW - filterSize;
		}
	}

	// Note the +1 is for the MMXscaler which reads over the end
	/* align at 16 for AltiVec (needed by hScale_altivec_real) */
	*outFilter= av_mallocz(*outFilterSize*(dstW+1)*sizeof(int16_t));

	/* Normalize & Store in outFilter */
	for(i=0; i<dstW; i++)
	{
		int j;
		double error=0;
		double sum=0;
		double scale= one;

		for(j=0; j<filterSize; j++)
		{
			sum+= filter[i*filterSize + j];
		}
		scale/= sum;
		for(j=0; j<*outFilterSize; j++)
		{
			double v= filter[i*filterSize + j]*scale + error;
			int intV= floor(v + 0.5);
			(*outFilter)[i*(*outFilterSize) + j]= intV;
			error = v - intV;
		}
	}
	
	(*filterPos)[dstW]= (*filterPos)[dstW-1]; // the MMX scaler will read over the end
	for(i=0; i<*outFilterSize; i++)
	{
		int j= dstW*(*outFilterSize);
		(*outFilter)[j + i]= (*outFilter)[j + i - (*outFilterSize)];
	}

	av_free(filter);
        return 0;
}

#ifdef COMPILE_MMX2
static void initMMX2HScaler(int dstW, int xInc, uint8_t *funnyCode, int16_t *filter, int32_t *filterPos, int numSplits)
{
	uint8_t *fragmentA;
	long imm8OfPShufW1A;
	long imm8OfPShufW2A;
	long fragmentLengthA;
	uint8_t *fragmentB;
	long imm8OfPShufW1B;
	long imm8OfPShufW2B;
	long fragmentLengthB;
	int fragmentPos;

	int xpos, i;

	// create an optimized horizontal scaling routine

	//code fragment

	asm volatile(
		"jmp 9f				\n\t"
	// Begin
		"0:				\n\t"
		"movq (%%"REG_d", %%"REG_a"), %%mm3\n\t" 
		"movd (%%"REG_c", %%"REG_S"), %%mm0\n\t" 
		"movd 1(%%"REG_c", %%"REG_S"), %%mm1\n\t"
		"punpcklbw %%mm7, %%mm1		\n\t"
		"punpcklbw %%mm7, %%mm0		\n\t"
		"pshufw $0xFF, %%mm1, %%mm1	\n\t"
		"1:				\n\t"
		"pshufw $0xFF, %%mm0, %%mm0	\n\t"
		"2:				\n\t"
		"psubw %%mm1, %%mm0		\n\t"
		"movl 8(%%"REG_b", %%"REG_a"), %%esi\n\t"
		"pmullw %%mm3, %%mm0		\n\t"
		"psllw $7, %%mm1		\n\t"
		"paddw %%mm1, %%mm0		\n\t"

		"movq %%mm0, (%%"REG_D", %%"REG_a")\n\t"

		"add $8, %%"REG_a"		\n\t"
	// End
		"9:				\n\t"
//		"int $3\n\t"
		"lea 0b, %0			\n\t"
		"lea 1b, %1			\n\t"
		"lea 2b, %2			\n\t"
		"dec %1				\n\t"
		"dec %2				\n\t"
		"sub %0, %1			\n\t"
		"sub %0, %2			\n\t"
		"lea 9b, %3			\n\t"
		"sub %0, %3			\n\t"


		:"=r" (fragmentA), "=r" (imm8OfPShufW1A), "=r" (imm8OfPShufW2A),
		"=r" (fragmentLengthA)
	);

	asm volatile(
		"jmp 9f				\n\t"
	// Begin
		"0:				\n\t"
		"movq (%%"REG_d", %%"REG_a"), %%mm3\n\t" 
		"movd (%%"REG_c", %%"REG_S"), %%mm0\n\t" 
		"punpcklbw %%mm7, %%mm0		\n\t"
		"pshufw $0xFF, %%mm0, %%mm1	\n\t"
		"1:				\n\t"
		"pshufw $0xFF, %%mm0, %%mm0	\n\t"
		"2:				\n\t"
		"psubw %%mm1, %%mm0		\n\t"
		"movl 8(%%"REG_b", %%"REG_a"), %%esi\n\t"
		"pmullw %%mm3, %%mm0		\n\t"
		"psllw $7, %%mm1		\n\t"
		"paddw %%mm1, %%mm0		\n\t"

		"movq %%mm0, (%%"REG_D", %%"REG_a")\n\t"

		"add $8, %%"REG_a"		\n\t"
	// End
		"9:				\n\t"
//		"int $3\n\t"
		"lea 0b, %0			\n\t"
		"lea 1b, %1			\n\t"
		"lea 2b, %2			\n\t"
		"dec %1				\n\t"
		"dec %2				\n\t"
		"sub %0, %1			\n\t"
		"sub %0, %2			\n\t"
		"lea 9b, %3			\n\t"
		"sub %0, %3			\n\t"


		:"=r" (fragmentB), "=r" (imm8OfPShufW1B), "=r" (imm8OfPShufW2B),
		"=r" (fragmentLengthB)
	);

	xpos= 0; //lumXInc/2 - 0x8000; // difference between pixel centers
	fragmentPos=0;
	
	for(i=0; i<dstW/numSplits; i++)
	{
		int xx=xpos>>16;

		if((i&3) == 0)
		{
			int a=0;
			int b=((xpos+xInc)>>16) - xx;
			int c=((xpos+xInc*2)>>16) - xx;
			int d=((xpos+xInc*3)>>16) - xx;

			filter[i  ] = (( xpos         & 0xFFFF) ^ 0xFFFF)>>9;
			filter[i+1] = (((xpos+xInc  ) & 0xFFFF) ^ 0xFFFF)>>9;
			filter[i+2] = (((xpos+xInc*2) & 0xFFFF) ^ 0xFFFF)>>9;
			filter[i+3] = (((xpos+xInc*3) & 0xFFFF) ^ 0xFFFF)>>9;
			filterPos[i/2]= xx;

			if(d+1<4)
			{
				int maxShift= 3-(d+1);
				int shift=0;

				memcpy(funnyCode + fragmentPos, fragmentB, fragmentLengthB);

				funnyCode[fragmentPos + imm8OfPShufW1B]=
					(a+1) | ((b+1)<<2) | ((c+1)<<4) | ((d+1)<<6);
				funnyCode[fragmentPos + imm8OfPShufW2B]=
					a | (b<<2) | (c<<4) | (d<<6);

				if(i+3>=dstW) shift=maxShift; //avoid overread
				else if((filterPos[i/2]&3) <= maxShift) shift=filterPos[i/2]&3; //Align

				if(shift && i>=shift)
				{
					funnyCode[fragmentPos + imm8OfPShufW1B]+= 0x55*shift;
					funnyCode[fragmentPos + imm8OfPShufW2B]+= 0x55*shift;
					filterPos[i/2]-=shift;
				}

				fragmentPos+= fragmentLengthB;
			}
			else
			{
				int maxShift= 3-d;
				int shift=0;

				memcpy(funnyCode + fragmentPos, fragmentA, fragmentLengthA);

				funnyCode[fragmentPos + imm8OfPShufW1A]=
				funnyCode[fragmentPos + imm8OfPShufW2A]=
					a | (b<<2) | (c<<4) | (d<<6);

				if(i+4>=dstW) shift=maxShift; //avoid overread
				else if((filterPos[i/2]&3) <= maxShift) shift=filterPos[i/2]&3; //partial align

				if(shift && i>=shift)
				{
					funnyCode[fragmentPos + imm8OfPShufW1A]+= 0x55*shift;
					funnyCode[fragmentPos + imm8OfPShufW2A]+= 0x55*shift;
					filterPos[i/2]-=shift;
				}

				fragmentPos+= fragmentLengthA;
			}

			funnyCode[fragmentPos]= RET;
		}
		xpos+=xInc;
	}
	filterPos[i/2]= xpos>>16; // needed to jump to the next part
}
#endif /* COMPILE_MMX2 */

static void globalInit(void){
    // generating tables:
    int i;
    for(i=0; i<768; i++){
	int c= av_clip_uint8(i-256);
	clip_table[i]=c;
    }
}

static SwsFunc getSwsFunc(int flags){
    
#if defined(RUNTIME_CPUDETECT) && defined (CONFIG_GPL)
#if defined(ARCH_X86)
	// ordered per speed fasterst first
	if(flags & SWS_CPU_CAPS_MMX2)
		return swScale_MMX2;
	else if(flags & SWS_CPU_CAPS_3DNOW)
		return swScale_3DNow;
	else if(flags & SWS_CPU_CAPS_MMX)
		return swScale_MMX;
	else
		return swScale_C;

#else
#ifdef ARCH_POWERPC
	if(flags & SWS_CPU_CAPS_ALTIVEC)
	  return swScale_altivec;
	else
	  return swScale_C;
#endif
	return swScale_C;
#endif /* defined(ARCH_X86) */
#else //RUNTIME_CPUDETECT
#ifdef HAVE_MMX2
	return swScale_MMX2;
#elif defined (HAVE_3DNOW)
	return swScale_3DNow;
#elif defined (HAVE_MMX)
	return swScale_MMX;
#elif defined (HAVE_ALTIVEC)
	return swScale_altivec;
#else
	return swScale_C;
#endif
#endif //!RUNTIME_CPUDETECT
}

static int PlanarToNV12Wrapper(SwsContext *c, uint8_t* src[], int srcStride[], int srcSliceY,
             int srcSliceH, uint8_t* dstParam[], int dstStride[]){
	uint8_t *dst=dstParam[0] + dstStride[0]*srcSliceY;
	/* Copy Y plane */
	if(dstStride[0]==srcStride[0] && srcStride[0] > 0)
		memcpy(dst, src[0], srcSliceH*dstStride[0]);
	else
	{
		int i;
		uint8_t *srcPtr= src[0];
		uint8_t *dstPtr= dst;
		for(i=0; i<srcSliceH; i++)
		{
			memcpy(dstPtr, srcPtr, c->srcW);
			srcPtr+= srcStride[0];
			dstPtr+= dstStride[0];
		}
	}
	dst = dstParam[1] + dstStride[1]*srcSliceY/2;
	if (c->dstFormat == PIX_FMT_NV12)
		interleaveBytes( src[1],src[2],dst,c->srcW/2,srcSliceH/2,srcStride[1],srcStride[2],dstStride[0] );
	else
		interleaveBytes( src[2],src[1],dst,c->srcW/2,srcSliceH/2,srcStride[2],srcStride[1],dstStride[0] );

	return srcSliceH;
}

static int PlanarToYuy2Wrapper(SwsContext *c, uint8_t* src[], int srcStride[], int srcSliceY,
             int srcSliceH, uint8_t* dstParam[], int dstStride[]){
	uint8_t *dst=dstParam[0] + dstStride[0]*srcSliceY;

	yv12toyuy2( src[0],src[1],src[2],dst,c->srcW,srcSliceH,srcStride[0],srcStride[1],dstStride[0] );

	return srcSliceH;
}

static int PlanarToUyvyWrapper(SwsContext *c, uint8_t* src[], int srcStride[], int srcSliceY,
             int srcSliceH, uint8_t* dstParam[], int dstStride[]){
	uint8_t *dst=dstParam[0] + dstStride[0]*srcSliceY;

	yv12touyvy( src[0],src[1],src[2],dst,c->srcW,srcSliceH,srcStride[0],srcStride[1],dstStride[0] );

	return srcSliceH;
}

/* {RGB,BGR}{15,16,24,32} -> {RGB,BGR}{15,16,24,32} */
static int rgb2rgbWrapper(SwsContext *c, uint8_t* src[], int srcStride[], int srcSliceY,
			   int srcSliceH, uint8_t* dst[], int dstStride[]){
	const int srcFormat= c->srcFormat;
	const int dstFormat= c->dstFormat;
	const int srcBpp= (fmt_depth(srcFormat) + 7) >> 3;
	const int dstBpp= (fmt_depth(dstFormat) + 7) >> 3;
	const int srcId= fmt_depth(srcFormat) >> 2; /* 1:0, 4:1, 8:2, 15:3, 16:4, 24:6, 32:8 */
	const int dstId= fmt_depth(dstFormat) >> 2;
	void (*conv)(const uint8_t *src, uint8_t *dst, long src_size)=NULL;

	/* BGR -> BGR */
	if(   (isBGR(srcFormat) && isBGR(dstFormat))
	   || (isRGB(srcFormat) && isRGB(dstFormat))){
		switch(srcId | (dstId<<4)){
		case 0x34: conv= rgb16to15; break;
		case 0x36: conv= rgb24to15; break;
		case 0x38: conv= rgb32to15; break;
		case 0x43: conv= rgb15to16; break;
		case 0x46: conv= rgb24to16; break;
		case 0x48: conv= rgb32to16; break;
		case 0x63: conv= rgb15to24; break;
		case 0x64: conv= rgb16to24; break;
		case 0x68: conv= rgb32to24; break;
		case 0x83: conv= rgb15to32; break;
		case 0x84: conv= rgb16to32; break;
		case 0x86: conv= rgb24to32; break;
		default: av_log(c, AV_LOG_ERROR, "swScaler: internal error %s -> %s converter\n", 
				 sws_format_name(srcFormat), sws_format_name(dstFormat)); break;
		}
	}else if(   (isBGR(srcFormat) && isRGB(dstFormat))
		 || (isRGB(srcFormat) && isBGR(dstFormat))){
		switch(srcId | (dstId<<4)){
		case 0x33: conv= rgb15tobgr15; break;
		case 0x34: conv= rgb16tobgr15; break;
		case 0x36: conv= rgb24tobgr15; break;
		case 0x38: conv= rgb32tobgr15; break;
		case 0x43: conv= rgb15tobgr16; break;
		case 0x44: conv= rgb16tobgr16; break;
		case 0x46: conv= rgb24tobgr16; break;
		case 0x48: conv= rgb32tobgr16; break;
		case 0x63: conv= rgb15tobgr24; break;
		case 0x64: conv= rgb16tobgr24; break;
		case 0x66: conv= rgb24tobgr24; break;
		case 0x68: conv= rgb32tobgr24; break;
		case 0x83: conv= rgb15tobgr32; break;
		case 0x84: conv= rgb16tobgr32; break;
		case 0x86: conv= rgb24tobgr32; break;
		case 0x88: conv= rgb32tobgr32; break;
		default: av_log(c, AV_LOG_ERROR, "swScaler: internal error %s -> %s converter\n", 
				 sws_format_name(srcFormat), sws_format_name(dstFormat)); break;
		}
	}else{
		av_log(c, AV_LOG_ERROR, "swScaler: internal error %s -> %s converter\n", 
			 sws_format_name(srcFormat), sws_format_name(dstFormat));
	}

	if(dstStride[0]*srcBpp == srcStride[0]*dstBpp)
		conv(src[0], dst[0] + dstStride[0]*srcSliceY, srcSliceH*srcStride[0]);
	else
	{
		int i;
		uint8_t *srcPtr= src[0];
		uint8_t *dstPtr= dst[0] + dstStride[0]*srcSliceY;

		for(i=0; i<srcSliceH; i++)
		{
			conv(srcPtr, dstPtr, c->srcW*srcBpp);
			srcPtr+= srcStride[0];
			dstPtr+= dstStride[0];
		}
	}     
	return srcSliceH;
}

static int bgr24toyv12Wrapper(SwsContext *c, uint8_t* src[], int srcStride[], int srcSliceY,
             int srcSliceH, uint8_t* dst[], int dstStride[]){

	rgb24toyv12(
		src[0], 
		dst[0]+ srcSliceY    *dstStride[0], 
		dst[1]+(srcSliceY>>1)*dstStride[1], 
		dst[2]+(srcSliceY>>1)*dstStride[2],
		c->srcW, srcSliceH, 
		dstStride[0], dstStride[1], srcStride[0]);
	return srcSliceH;
}

static int yvu9toyv12Wrapper(SwsContext *c, uint8_t* src[], int srcStride[], int srcSliceY,
             int srcSliceH, uint8_t* dst[], int dstStride[]){
	int i;

	/* copy Y */
	if(srcStride[0]==dstStride[0] && srcStride[0] > 0) 
		memcpy(dst[0]+ srcSliceY*dstStride[0], src[0], srcStride[0]*srcSliceH);
	else{
		uint8_t *srcPtr= src[0];
		uint8_t *dstPtr= dst[0] + dstStride[0]*srcSliceY;

		for(i=0; i<srcSliceH; i++)
		{
			memcpy(dstPtr, srcPtr, c->srcW);
			srcPtr+= srcStride[0];
			dstPtr+= dstStride[0];
		}
	}

	if(c->dstFormat==PIX_FMT_YUV420P){
		planar2x(src[1], dst[1], c->chrSrcW, c->chrSrcH, srcStride[1], dstStride[1]);
		planar2x(src[2], dst[2], c->chrSrcW, c->chrSrcH, srcStride[2], dstStride[2]);
	}else{
		planar2x(src[1], dst[2], c->chrSrcW, c->chrSrcH, srcStride[1], dstStride[2]);
		planar2x(src[2], dst[1], c->chrSrcW, c->chrSrcH, srcStride[2], dstStride[1]);
	}
	return srcSliceH;
}

/* unscaled copy like stuff (assumes nearly identical formats) */
static int simpleCopy(SwsContext *c, uint8_t* src[], int srcStride[], int srcSliceY,
             int srcSliceH, uint8_t* dst[], int dstStride[]){

	if(isPacked(c->srcFormat))
	{
		if(dstStride[0]==srcStride[0] && srcStride[0] > 0)
			memcpy(dst[0] + dstStride[0]*srcSliceY, src[0], srcSliceH*dstStride[0]);
		else
		{
			int i;
			uint8_t *srcPtr= src[0];
			uint8_t *dstPtr= dst[0] + dstStride[0]*srcSliceY;
			int length=0;

			/* universal length finder */
			while(length+c->srcW <= FFABS(dstStride[0]) 
			   && length+c->srcW <= FFABS(srcStride[0])) length+= c->srcW;
			ASSERT(length!=0);

			for(i=0; i<srcSliceH; i++)
			{
				memcpy(dstPtr, srcPtr, length);
				srcPtr+= srcStride[0];
				dstPtr+= dstStride[0];
			}
		}
	}
	else 
	{ /* Planar YUV or gray */
		int plane;
		for(plane=0; plane<3; plane++)
		{
			int length= plane==0 ? c->srcW  : -((-c->srcW  )>>c->chrDstHSubSample);
			int y=      plane==0 ? srcSliceY: -((-srcSliceY)>>c->chrDstVSubSample);
			int height= plane==0 ? srcSliceH: -((-srcSliceH)>>c->chrDstVSubSample);

			if((isGray(c->srcFormat) || isGray(c->dstFormat)) && plane>0)
			{
				if(!isGray(c->dstFormat))
					memset(dst[plane], 128, dstStride[plane]*height);
			}
			else
			{
				if(dstStride[plane]==srcStride[plane] && srcStride[plane] > 0)
					memcpy(dst[plane] + dstStride[plane]*y, src[plane], height*dstStride[plane]);
				else
				{
					int i;
					uint8_t *srcPtr= src[plane];
					uint8_t *dstPtr= dst[plane] + dstStride[plane]*y;
					for(i=0; i<height; i++)
					{
						memcpy(dstPtr, srcPtr, length);
						srcPtr+= srcStride[plane];
						dstPtr+= dstStride[plane];
					}
				}
			}
		}
	}
	return srcSliceH;
}

static int gray16togray(SwsContext *c, uint8_t* src[], int srcStride[], int srcSliceY,
             int srcSliceH, uint8_t* dst[], int dstStride[]){

	int length= c->srcW;
	int y=      srcSliceY;
	int height= srcSliceH;
	int i, j;
	uint8_t *srcPtr= src[0];
	uint8_t *dstPtr= dst[0] + dstStride[0]*y;

	if(!isGray(c->dstFormat)){
		int height= -((-srcSliceH)>>c->chrDstVSubSample);
		memset(dst[1], 128, dstStride[1]*height);
		memset(dst[2], 128, dstStride[2]*height);
	}
	if(c->srcFormat == PIX_FMT_GRAY16LE) srcPtr++;
	for(i=0; i<height; i++)
	{
		for(j=0; j<length; j++) dstPtr[j] = srcPtr[j<<1];
		srcPtr+= srcStride[0];
		dstPtr+= dstStride[0];
	}
	return srcSliceH;
}

static int graytogray16(SwsContext *c, uint8_t* src[], int srcStride[], int srcSliceY,
             int srcSliceH, uint8_t* dst[], int dstStride[]){

	int length= c->srcW;
	int y=      srcSliceY;
	int height= srcSliceH;
	int i, j;
	uint8_t *srcPtr= src[0];
	uint8_t *dstPtr= dst[0] + dstStride[0]*y;
	for(i=0; i<height; i++)
	{
		for(j=0; j<length; j++)
		{
			dstPtr[j<<1] = srcPtr[j];
			dstPtr[(j<<1)+1] = srcPtr[j];
		}
		srcPtr+= srcStride[0];
		dstPtr+= dstStride[0];
	}
	return srcSliceH;
}

static int gray16swap(SwsContext *c, uint8_t* src[], int srcStride[], int srcSliceY,
             int srcSliceH, uint8_t* dst[], int dstStride[]){

	int length= c->srcW;
	int y=      srcSliceY;
	int height= srcSliceH;
	int i, j;
	uint16_t *srcPtr= src[0];
	uint16_t *dstPtr= dst[0] + dstStride[0]*y/2;
	for(i=0; i<height; i++)
	{
		for(j=0; j<length; j++) dstPtr[j] = bswap_16(srcPtr[j]);
		srcPtr+= srcStride[0]/2;
		dstPtr+= dstStride[0]/2;
	}
	return srcSliceH;
}


static void getSubSampleFactors(int *h, int *v, int format){
	switch(format){
	case PIX_FMT_UYVY422:
	case PIX_FMT_YUYV422:
		*h=1;
		*v=0;
		break;
	case PIX_FMT_YUV420P:
	case PIX_FMT_GRAY16BE:
	case PIX_FMT_GRAY16LE:
	case PIX_FMT_GRAY8: //FIXME remove after different subsamplings are fully implemented
	case PIX_FMT_NV12:
	case PIX_FMT_NV21:
		*h=1;
		*v=1;
		break;
	case PIX_FMT_YUV410P:
		*h=2;
		*v=2;
		break;
	case PIX_FMT_YUV444P:
		*h=0;
		*v=0;
		break;
	case PIX_FMT_YUV422P:
		*h=1;
		*v=0;
		break;
	case PIX_FMT_YUV411P:
		*h=2;
		*v=0;
		break;
	default:
		*h=0;
		*v=0;
		break;
	}
}

static uint16_t roundToInt16(int64_t f){
	int r= (f + (1<<15))>>16;
	     if(r<-0x7FFF) return 0x8000;
	else if(r> 0x7FFF) return 0x7FFF;
	else               return r;
}

/**
 * @param inv_table the yuv2rgb coeffs, normally Inverse_Table_6_9[x]
 * @param fullRange if 1 then the luma range is 0..255 if 0 its 16..235
 * @return -1 if not supported
 */
int sws_setColorspaceDetails(SwsContext *c, const int inv_table[4], int srcRange, const int table[4], int dstRange, int brightness, int contrast, int saturation){
	int64_t crv =  inv_table[0];
	int64_t cbu =  inv_table[1];
	int64_t cgu = -inv_table[2];
	int64_t cgv = -inv_table[3];
	int64_t cy  = 1<<16;
	int64_t oy  = 0;

	if(isYUV(c->dstFormat) || isGray(c->dstFormat)) return -1;
	memcpy(c->srcColorspaceTable, inv_table, sizeof(int)*4);
	memcpy(c->dstColorspaceTable,     table, sizeof(int)*4);

	c->brightness= brightness;
	c->contrast  = contrast;
	c->saturation= saturation;
	c->srcRange  = srcRange;
	c->dstRange  = dstRange;

	c->uOffset=   0x0400040004000400LL;
	c->vOffset=   0x0400040004000400LL;

	if(!srcRange){
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

	oy -= 256*brightness;

	c->yCoeff=    roundToInt16(cy *8192) * 0x0001000100010001ULL;
	c->vrCoeff=   roundToInt16(crv*8192) * 0x0001000100010001ULL;
	c->ubCoeff=   roundToInt16(cbu*8192) * 0x0001000100010001ULL;
	c->vgCoeff=   roundToInt16(cgv*8192) * 0x0001000100010001ULL;
	c->ugCoeff=   roundToInt16(cgu*8192) * 0x0001000100010001ULL;
	c->yOffset=   roundToInt16(oy *   8) * 0x0001000100010001ULL;

	yuv2rgb_c_init_tables(c, inv_table, srcRange, brightness, contrast, saturation);
	//FIXME factorize

#ifdef COMPILE_ALTIVEC
	if (c->flags & SWS_CPU_CAPS_ALTIVEC)
	    yuv2rgb_altivec_init_tables (c, inv_table, brightness, contrast, saturation);
#endif	
	return 0;
}

/**
 * @return -1 if not supported
 */
int sws_getColorspaceDetails(SwsContext *c, int **inv_table, int *srcRange, int **table, int *dstRange, int *brightness, int *contrast, int *saturation){
	if(isYUV(c->dstFormat) || isGray(c->dstFormat)) return -1;

	*inv_table = c->srcColorspaceTable;
	*table     = c->dstColorspaceTable;
	*srcRange  = c->srcRange;
	*dstRange  = c->dstRange;
	*brightness= c->brightness;
	*contrast  = c->contrast;
	*saturation= c->saturation;
	
	return 0;	
}

static int handle_jpeg(int *format)
{
	switch (*format) {
		case PIX_FMT_YUVJ420P:
			*format = PIX_FMT_YUV420P;
			return 1;
		case PIX_FMT_YUVJ422P:
			*format = PIX_FMT_YUV422P;
			return 1;
		case PIX_FMT_YUVJ444P:
			*format = PIX_FMT_YUV444P;
			return 1;
		default:
			return 0;
	}
}

SwsContext *sws_getContext(int srcW, int srcH, int srcFormat, int dstW, int dstH, int dstFormat, int flags,
                         SwsFilter *srcFilter, SwsFilter *dstFilter, double *param){

	SwsContext *c;
	int i;
	int usesVFilter, usesHFilter;
	int unscaled, needsDither;
	int srcRange, dstRange;
	SwsFilter dummyFilter= {NULL, NULL, NULL, NULL};
#if defined(ARCH_X86)
	if(flags & SWS_CPU_CAPS_MMX)
		asm volatile("emms\n\t"::: "memory");
#endif

#if !defined(RUNTIME_CPUDETECT) || !defined (CONFIG_GPL) //ensure that the flags match the compiled variant if cpudetect is off
	flags &= ~(SWS_CPU_CAPS_MMX|SWS_CPU_CAPS_MMX2|SWS_CPU_CAPS_3DNOW|SWS_CPU_CAPS_ALTIVEC);
#ifdef HAVE_MMX2
	flags |= SWS_CPU_CAPS_MMX|SWS_CPU_CAPS_MMX2;
#elif defined (HAVE_3DNOW)
	flags |= SWS_CPU_CAPS_MMX|SWS_CPU_CAPS_3DNOW;
#elif defined (HAVE_MMX)
	flags |= SWS_CPU_CAPS_MMX;
#elif defined (HAVE_ALTIVEC)
	flags |= SWS_CPU_CAPS_ALTIVEC;
#endif
#endif /* RUNTIME_CPUDETECT */
	if(clip_table[512] != 255) globalInit();
	if(rgb15to16 == NULL) sws_rgb2rgb_init(flags);

	unscaled = (srcW == dstW && srcH == dstH);
	needsDither= (isBGR(dstFormat) || isRGB(dstFormat)) 
		     && (fmt_depth(dstFormat))<24
		     && ((fmt_depth(dstFormat))<(fmt_depth(srcFormat)) || (!(isRGB(srcFormat) || isBGR(srcFormat))));

	srcRange = handle_jpeg(&srcFormat);
	dstRange = handle_jpeg(&dstFormat);

	if(!isSupportedIn(srcFormat)) 
	{
		av_log(NULL, AV_LOG_ERROR, "swScaler: %s is not supported as input format\n", sws_format_name(srcFormat));
		return NULL;
	}
	if(!isSupportedOut(dstFormat))
	{
		av_log(NULL, AV_LOG_ERROR, "swScaler: %s is not supported as output format\n", sws_format_name(dstFormat));
		return NULL;
	}

	/* sanity check */
	if(srcW<4 || srcH<1 || dstW<8 || dstH<1) //FIXME check if these are enough and try to lowwer them after fixing the relevant parts of the code
	{
		 av_log(NULL, AV_LOG_ERROR, "swScaler: %dx%d -> %dx%d is invalid scaling dimension\n", 
			srcW, srcH, dstW, dstH);
		return NULL;
	}

	if(!dstFilter) dstFilter= &dummyFilter;
	if(!srcFilter) srcFilter= &dummyFilter;

	c= av_mallocz(sizeof(SwsContext));

	c->av_class = &sws_context_class;
	c->srcW= srcW;
	c->srcH= srcH;
	c->dstW= dstW;
	c->dstH= dstH;
	c->lumXInc= ((srcW<<16) + (dstW>>1))/dstW;
	c->lumYInc= ((srcH<<16) + (dstH>>1))/dstH;
	c->flags= flags;
	c->dstFormat= dstFormat;
	c->srcFormat= srcFormat;
        c->vRounder= 4* 0x0001000100010001ULL;

	usesHFilter= usesVFilter= 0;
	if(dstFilter->lumV!=NULL && dstFilter->lumV->length>1) usesVFilter=1;
	if(dstFilter->lumH!=NULL && dstFilter->lumH->length>1) usesHFilter=1;
	if(dstFilter->chrV!=NULL && dstFilter->chrV->length>1) usesVFilter=1;
	if(dstFilter->chrH!=NULL && dstFilter->chrH->length>1) usesHFilter=1;
	if(srcFilter->lumV!=NULL && srcFilter->lumV->length>1) usesVFilter=1;
	if(srcFilter->lumH!=NULL && srcFilter->lumH->length>1) usesHFilter=1;
	if(srcFilter->chrV!=NULL && srcFilter->chrV->length>1) usesVFilter=1;
	if(srcFilter->chrH!=NULL && srcFilter->chrH->length>1) usesHFilter=1;

	getSubSampleFactors(&c->chrSrcHSubSample, &c->chrSrcVSubSample, srcFormat);
	getSubSampleFactors(&c->chrDstHSubSample, &c->chrDstVSubSample, dstFormat);

	// reuse chroma for 2 pixles rgb/bgr unless user wants full chroma interpolation
	if((isBGR(dstFormat) || isRGB(dstFormat)) && !(flags&SWS_FULL_CHR_H_INT)) c->chrDstHSubSample=1;

	// drop some chroma lines if the user wants it
	c->vChrDrop= (flags&SWS_SRC_V_CHR_DROP_MASK)>>SWS_SRC_V_CHR_DROP_SHIFT;
	c->chrSrcVSubSample+= c->vChrDrop;

	// drop every 2. pixel for chroma calculation unless user wants full chroma
	if((isBGR(srcFormat) || isRGB(srcFormat)) && !(flags&SWS_FULL_CHR_H_INP)) 
		c->chrSrcHSubSample=1;

	if(param){
		c->param[0] = param[0];
		c->param[1] = param[1];
	}else{
		c->param[0] =
		c->param[1] = SWS_PARAM_DEFAULT;
	}

	c->chrIntHSubSample= c->chrDstHSubSample;
	c->chrIntVSubSample= c->chrSrcVSubSample;

	// note the -((-x)>>y) is so that we allways round toward +inf
	c->chrSrcW= -((-srcW) >> c->chrSrcHSubSample);
	c->chrSrcH= -((-srcH) >> c->chrSrcVSubSample);
	c->chrDstW= -((-dstW) >> c->chrDstHSubSample);
	c->chrDstH= -((-dstH) >> c->chrDstVSubSample);

	sws_setColorspaceDetails(c, Inverse_Table_6_9[SWS_CS_DEFAULT], srcRange, Inverse_Table_6_9[SWS_CS_DEFAULT] /* FIXME*/, dstRange, 0, 1<<16, 1<<16); 

	/* unscaled special Cases */
	if(unscaled && !usesHFilter && !usesVFilter)
	{
		/* yv12_to_nv12 */
		if(srcFormat == PIX_FMT_YUV420P && (dstFormat == PIX_FMT_NV12 || dstFormat == PIX_FMT_NV21))
		{
			c->swScale= PlanarToNV12Wrapper;
		}
#ifdef CONFIG_GPL
		/* yuv2bgr */
		if((srcFormat==PIX_FMT_YUV420P || srcFormat==PIX_FMT_YUV422P) && (isBGR(dstFormat) || isRGB(dstFormat)))
		{
			c->swScale= yuv2rgb_get_func_ptr(c);
		}
#endif
		
		if( srcFormat==PIX_FMT_YUV410P && dstFormat==PIX_FMT_YUV420P )
		{
			c->swScale= yvu9toyv12Wrapper;
		}

		/* bgr24toYV12 */
		if(srcFormat==PIX_FMT_BGR24 && dstFormat==PIX_FMT_YUV420P)
			c->swScale= bgr24toyv12Wrapper;
		
		/* rgb/bgr -> rgb/bgr (no dither needed forms) */
		if(   (isBGR(srcFormat) || isRGB(srcFormat))
		   && (isBGR(dstFormat) || isRGB(dstFormat)) 
		   && srcFormat != PIX_FMT_BGR8 && dstFormat != PIX_FMT_BGR8
		   && srcFormat != PIX_FMT_RGB8 && dstFormat != PIX_FMT_RGB8
		   && srcFormat != PIX_FMT_BGR4 && dstFormat != PIX_FMT_BGR4
		   && srcFormat != PIX_FMT_RGB4 && dstFormat != PIX_FMT_RGB4
		   && srcFormat != PIX_FMT_BGR4_BYTE && dstFormat != PIX_FMT_BGR4_BYTE
		   && srcFormat != PIX_FMT_RGB4_BYTE && dstFormat != PIX_FMT_RGB4_BYTE
		   && srcFormat != PIX_FMT_MONOBLACK && dstFormat != PIX_FMT_MONOBLACK
		   && !needsDither)
			c->swScale= rgb2rgbWrapper;

		/* LQ converters if -sws 0 or -sws 4*/
		if(c->flags&(SWS_FAST_BILINEAR|SWS_POINT)){
			/* rgb/bgr -> rgb/bgr (dither needed forms) */
			if(  (isBGR(srcFormat) || isRGB(srcFormat))
			  && (isBGR(dstFormat) || isRGB(dstFormat)) 
			  && needsDither)
				c->swScale= rgb2rgbWrapper;

			/* yv12_to_yuy2 */
			if(srcFormat == PIX_FMT_YUV420P && 
			    (dstFormat == PIX_FMT_YUYV422 || dstFormat == PIX_FMT_UYVY422))
			{
				if (dstFormat == PIX_FMT_YUYV422)
				    c->swScale= PlanarToYuy2Wrapper;
				else
				    c->swScale= PlanarToUyvyWrapper;
			}
		}

#ifdef COMPILE_ALTIVEC
		if ((c->flags & SWS_CPU_CAPS_ALTIVEC) &&
		    ((srcFormat == PIX_FMT_YUV420P && 
		      (dstFormat == PIX_FMT_YUYV422 || dstFormat == PIX_FMT_UYVY422)))) {
		  // unscaled YV12 -> packed YUV, we want speed
		  if (dstFormat == PIX_FMT_YUYV422)
		    c->swScale= yv12toyuy2_unscaled_altivec;
		  else
		    c->swScale= yv12touyvy_unscaled_altivec;
		}
#endif

		/* simple copy */
		if(   srcFormat == dstFormat
		   || (isPlanarYUV(srcFormat) && isGray(dstFormat))
		   || (isPlanarYUV(dstFormat) && isGray(srcFormat))
		  )
		{
			c->swScale= simpleCopy;
		}

		/* gray16{le,be} conversions */
		if(isGray16(srcFormat) && (isPlanarYUV(dstFormat) || (dstFormat == PIX_FMT_GRAY8)))
		{
			c->swScale= gray16togray;
		}
		if((isPlanarYUV(srcFormat) || (srcFormat == PIX_FMT_GRAY8)) && isGray16(dstFormat))
		{
			c->swScale= graytogray16;
		}
		if(srcFormat != dstFormat && isGray16(srcFormat) && isGray16(dstFormat))
		{
			c->swScale= gray16swap;
		}		

		if(c->swScale){
			if(flags&SWS_PRINT_INFO)
				av_log(c, AV_LOG_INFO, "SwScaler: using unscaled %s -> %s special converter\n", 
					sws_format_name(srcFormat), sws_format_name(dstFormat));
			return c;
		}
	}

	if(flags & SWS_CPU_CAPS_MMX2)
	{
		c->canMMX2BeUsed= (dstW >=srcW && (dstW&31)==0 && (srcW&15)==0) ? 1 : 0;
		if(!c->canMMX2BeUsed && dstW >=srcW && (srcW&15)==0 && (flags&SWS_FAST_BILINEAR))
		{
			if(flags&SWS_PRINT_INFO)
				av_log(c, AV_LOG_INFO, "SwScaler: output Width is not a multiple of 32 -> no MMX2 scaler\n");
		}
		if(usesHFilter) c->canMMX2BeUsed=0;
	}
	else
		c->canMMX2BeUsed=0;

	c->chrXInc= ((c->chrSrcW<<16) + (c->chrDstW>>1))/c->chrDstW;
	c->chrYInc= ((c->chrSrcH<<16) + (c->chrDstH>>1))/c->chrDstH;

	// match pixel 0 of the src to pixel 0 of dst and match pixel n-2 of src to pixel n-2 of dst
	// but only for the FAST_BILINEAR mode otherwise do correct scaling
	// n-2 is the last chrominance sample available
	// this is not perfect, but noone shuld notice the difference, the more correct variant
	// would be like the vertical one, but that would require some special code for the
	// first and last pixel
	if(flags&SWS_FAST_BILINEAR)
	{
		if(c->canMMX2BeUsed)
		{
			c->lumXInc+= 20;
			c->chrXInc+= 20;
		}
		//we don't use the x86asm scaler if mmx is available
		else if(flags & SWS_CPU_CAPS_MMX)
		{
			c->lumXInc = ((srcW-2)<<16)/(dstW-2) - 20;
			c->chrXInc = ((c->chrSrcW-2)<<16)/(c->chrDstW-2) - 20;
		}
	}

	/* precalculate horizontal scaler filter coefficients */
	{
		const int filterAlign=
		  (flags & SWS_CPU_CAPS_MMX) ? 4 :
		  (flags & SWS_CPU_CAPS_ALTIVEC) ? 8 :
		  1;

		initFilter(&c->hLumFilter, &c->hLumFilterPos, &c->hLumFilterSize, c->lumXInc,
				 srcW      ,       dstW, filterAlign, 1<<14,
				 (flags&SWS_BICUBLIN) ? (flags|SWS_BICUBIC)  : flags,
				 srcFilter->lumH, dstFilter->lumH, c->param);
		initFilter(&c->hChrFilter, &c->hChrFilterPos, &c->hChrFilterSize, c->chrXInc,
				 c->chrSrcW, c->chrDstW, filterAlign, 1<<14,
				 (flags&SWS_BICUBLIN) ? (flags|SWS_BILINEAR) : flags,
				 srcFilter->chrH, dstFilter->chrH, c->param);

#define MAX_FUNNY_CODE_SIZE 10000
#if defined(COMPILE_MMX2)
// can't downscale !!!
		if(c->canMMX2BeUsed && (flags & SWS_FAST_BILINEAR))
		{
#ifdef MAP_ANONYMOUS
			c->funnyYCode = (uint8_t*)mmap(NULL, MAX_FUNNY_CODE_SIZE, PROT_EXEC | PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
			c->funnyUVCode = (uint8_t*)mmap(NULL, MAX_FUNNY_CODE_SIZE, PROT_EXEC | PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
#else
			c->funnyYCode = av_malloc(MAX_FUNNY_CODE_SIZE);
			c->funnyUVCode = av_malloc(MAX_FUNNY_CODE_SIZE);
#endif

			c->lumMmx2Filter   = av_malloc((dstW        /8+8)*sizeof(int16_t));
			c->chrMmx2Filter   = av_malloc((c->chrDstW  /4+8)*sizeof(int16_t));
			c->lumMmx2FilterPos= av_malloc((dstW      /2/8+8)*sizeof(int32_t));
			c->chrMmx2FilterPos= av_malloc((c->chrDstW/2/4+8)*sizeof(int32_t));

			initMMX2HScaler(      dstW, c->lumXInc, c->funnyYCode , c->lumMmx2Filter, c->lumMmx2FilterPos, 8);
			initMMX2HScaler(c->chrDstW, c->chrXInc, c->funnyUVCode, c->chrMmx2Filter, c->chrMmx2FilterPos, 4);
		}
#endif /* defined(COMPILE_MMX2) */
	} // Init Horizontal stuff



	/* precalculate vertical scaler filter coefficients */
	{
		const int filterAlign=
		  (flags & SWS_CPU_CAPS_MMX) && (flags & SWS_ACCURATE_RND) ? 2 :
		  (flags & SWS_CPU_CAPS_ALTIVEC) ? 8 :
		  1;

		initFilter(&c->vLumFilter, &c->vLumFilterPos, &c->vLumFilterSize, c->lumYInc,
				srcH      ,        dstH, filterAlign, (1<<12)-4,
				(flags&SWS_BICUBLIN) ? (flags|SWS_BICUBIC)  : flags,
				srcFilter->lumV, dstFilter->lumV, c->param);
		initFilter(&c->vChrFilter, &c->vChrFilterPos, &c->vChrFilterSize, c->chrYInc,
				c->chrSrcH, c->chrDstH, filterAlign, (1<<12)-4,
				(flags&SWS_BICUBLIN) ? (flags|SWS_BILINEAR) : flags,
				srcFilter->chrV, dstFilter->chrV, c->param);

#ifdef HAVE_ALTIVEC
		c->vYCoeffsBank = av_malloc(sizeof (vector signed short)*c->vLumFilterSize*c->dstH);
		c->vCCoeffsBank = av_malloc(sizeof (vector signed short)*c->vChrFilterSize*c->chrDstH);

		for (i=0;i<c->vLumFilterSize*c->dstH;i++) {
                  int j;
		  short *p = (short *)&c->vYCoeffsBank[i];
		  for (j=0;j<8;j++)
		    p[j] = c->vLumFilter[i];
		}

		for (i=0;i<c->vChrFilterSize*c->chrDstH;i++) {
                  int j;
		  short *p = (short *)&c->vCCoeffsBank[i];
		  for (j=0;j<8;j++)
		    p[j] = c->vChrFilter[i];
		}
#endif
	}

	// Calculate Buffer Sizes so that they won't run out while handling these damn slices
	c->vLumBufSize= c->vLumFilterSize;
	c->vChrBufSize= c->vChrFilterSize;
	for(i=0; i<dstH; i++)
	{
		int chrI= i*c->chrDstH / dstH;
		int nextSlice= FFMAX(c->vLumFilterPos[i   ] + c->vLumFilterSize - 1,
				 ((c->vChrFilterPos[chrI] + c->vChrFilterSize - 1)<<c->chrSrcVSubSample));

		nextSlice>>= c->chrSrcVSubSample;
		nextSlice<<= c->chrSrcVSubSample;
		if(c->vLumFilterPos[i   ] + c->vLumBufSize < nextSlice)
			c->vLumBufSize= nextSlice - c->vLumFilterPos[i   ];
		if(c->vChrFilterPos[chrI] + c->vChrBufSize < (nextSlice>>c->chrSrcVSubSample))
			c->vChrBufSize= (nextSlice>>c->chrSrcVSubSample) - c->vChrFilterPos[chrI];
	}

	// allocate pixbufs (we use dynamic allocation because otherwise we would need to
	c->lumPixBuf= av_malloc(c->vLumBufSize*2*sizeof(int16_t*));
	c->chrPixBuf= av_malloc(c->vChrBufSize*2*sizeof(int16_t*));
	//Note we need at least one pixel more at the end because of the mmx code (just in case someone wanna replace the 4000/8000)
	/* align at 16 bytes for AltiVec */
	for(i=0; i<c->vLumBufSize; i++)
		c->lumPixBuf[i]= c->lumPixBuf[i+c->vLumBufSize]= av_mallocz(4000);
	for(i=0; i<c->vChrBufSize; i++)
		c->chrPixBuf[i]= c->chrPixBuf[i+c->vChrBufSize]= av_malloc(8000);

	//try to avoid drawing green stuff between the right end and the stride end
	for(i=0; i<c->vChrBufSize; i++) memset(c->chrPixBuf[i], 64, 8000);

	ASSERT(c->chrDstH <= dstH)

	if(flags&SWS_PRINT_INFO)
	{
#ifdef DITHER1XBPP
		char *dither= " dithered";
#else
		char *dither= "";
#endif
		if(flags&SWS_FAST_BILINEAR)
			av_log(c, AV_LOG_INFO, "SwScaler: FAST_BILINEAR scaler, ");
		else if(flags&SWS_BILINEAR)
			av_log(c, AV_LOG_INFO, "SwScaler: BILINEAR scaler, ");
		else if(flags&SWS_BICUBIC)
			av_log(c, AV_LOG_INFO, "SwScaler: BICUBIC scaler, ");
		else if(flags&SWS_X)
			av_log(c, AV_LOG_INFO, "SwScaler: Experimental scaler, ");
		else if(flags&SWS_POINT)
			av_log(c, AV_LOG_INFO, "SwScaler: Nearest Neighbor / POINT scaler, ");
		else if(flags&SWS_AREA)
			av_log(c, AV_LOG_INFO, "SwScaler: Area Averageing scaler, ");
		else if(flags&SWS_BICUBLIN)
			av_log(c, AV_LOG_INFO, "SwScaler: luma BICUBIC / chroma BILINEAR scaler, ");
		else if(flags&SWS_GAUSS)
			av_log(c, AV_LOG_INFO, "SwScaler: Gaussian scaler, ");
		else if(flags&SWS_SINC)
			av_log(c, AV_LOG_INFO, "SwScaler: Sinc scaler, ");
		else if(flags&SWS_LANCZOS)
			av_log(c, AV_LOG_INFO, "SwScaler: Lanczos scaler, ");
		else if(flags&SWS_SPLINE)
			av_log(c, AV_LOG_INFO, "SwScaler: Bicubic spline scaler, ");
		else
			av_log(c, AV_LOG_INFO, "SwScaler: ehh flags invalid?! ");

		if(dstFormat==PIX_FMT_BGR555 || dstFormat==PIX_FMT_BGR565)
			av_log(c, AV_LOG_INFO, "from %s to%s %s ", 
				sws_format_name(srcFormat), dither, sws_format_name(dstFormat));
		else
			av_log(c, AV_LOG_INFO, "from %s to %s ", 
				sws_format_name(srcFormat), sws_format_name(dstFormat));

		if(flags & SWS_CPU_CAPS_MMX2)
			av_log(c, AV_LOG_INFO, "using MMX2\n");
		else if(flags & SWS_CPU_CAPS_3DNOW)
			av_log(c, AV_LOG_INFO, "using 3DNOW\n");
		else if(flags & SWS_CPU_CAPS_MMX)
			av_log(c, AV_LOG_INFO, "using MMX\n");
		else if(flags & SWS_CPU_CAPS_ALTIVEC)
			av_log(c, AV_LOG_INFO, "using AltiVec\n");
		else 
			av_log(c, AV_LOG_INFO, "using C\n");
	}

	if(flags & SWS_PRINT_INFO)
	{
		if(flags & SWS_CPU_CAPS_MMX)
		{
			if(c->canMMX2BeUsed && (flags&SWS_FAST_BILINEAR))
				av_log(c, AV_LOG_VERBOSE, "SwScaler: using FAST_BILINEAR MMX2 scaler for horizontal scaling\n");
			else
			{
				if(c->hLumFilterSize==4)
					av_log(c, AV_LOG_VERBOSE, "SwScaler: using 4-tap MMX scaler for horizontal luminance scaling\n");
				else if(c->hLumFilterSize==8)
					av_log(c, AV_LOG_VERBOSE, "SwScaler: using 8-tap MMX scaler for horizontal luminance scaling\n");
				else
					av_log(c, AV_LOG_VERBOSE, "SwScaler: using n-tap MMX scaler for horizontal luminance scaling\n");

				if(c->hChrFilterSize==4)
					av_log(c, AV_LOG_VERBOSE, "SwScaler: using 4-tap MMX scaler for horizontal chrominance scaling\n");
				else if(c->hChrFilterSize==8)
					av_log(c, AV_LOG_VERBOSE, "SwScaler: using 8-tap MMX scaler for horizontal chrominance scaling\n");
				else
					av_log(c, AV_LOG_VERBOSE, "SwScaler: using n-tap MMX scaler for horizontal chrominance scaling\n");
			}
		}
		else
		{
#if defined(ARCH_X86)
			av_log(c, AV_LOG_VERBOSE, "SwScaler: using X86-Asm scaler for horizontal scaling\n");
#else
			if(flags & SWS_FAST_BILINEAR)
				av_log(c, AV_LOG_VERBOSE, "SwScaler: using FAST_BILINEAR C scaler for horizontal scaling\n");
			else
				av_log(c, AV_LOG_VERBOSE, "SwScaler: using C scaler for horizontal scaling\n");
#endif
		}
		if(isPlanarYUV(dstFormat))
		{
			if(c->vLumFilterSize==1)
				av_log(c, AV_LOG_VERBOSE, "SwScaler: using 1-tap %s \"scaler\" for vertical scaling (YV12 like)\n", (flags & SWS_CPU_CAPS_MMX) ? "MMX" : "C");
			else
				av_log(c, AV_LOG_VERBOSE, "SwScaler: using n-tap %s scaler for vertical scaling (YV12 like)\n", (flags & SWS_CPU_CAPS_MMX) ? "MMX" : "C");
		}
		else
		{
			if(c->vLumFilterSize==1 && c->vChrFilterSize==2)
				av_log(c, AV_LOG_VERBOSE, "SwScaler: using 1-tap %s \"scaler\" for vertical luminance scaling (BGR)\n"
				       "SwScaler:       2-tap scaler for vertical chrominance scaling (BGR)\n",(flags & SWS_CPU_CAPS_MMX) ? "MMX" : "C");
			else if(c->vLumFilterSize==2 && c->vChrFilterSize==2)
				av_log(c, AV_LOG_VERBOSE, "SwScaler: using 2-tap linear %s scaler for vertical scaling (BGR)\n", (flags & SWS_CPU_CAPS_MMX) ? "MMX" : "C");
			else
				av_log(c, AV_LOG_VERBOSE, "SwScaler: using n-tap %s scaler for vertical scaling (BGR)\n", (flags & SWS_CPU_CAPS_MMX) ? "MMX" : "C");
		}

		if(dstFormat==PIX_FMT_BGR24)
			av_log(c, AV_LOG_VERBOSE, "SwScaler: using %s YV12->BGR24 Converter\n",
				(flags & SWS_CPU_CAPS_MMX2) ? "MMX2" : ((flags & SWS_CPU_CAPS_MMX) ? "MMX" : "C"));
		else if(dstFormat==PIX_FMT_RGB32)
			av_log(c, AV_LOG_VERBOSE, "SwScaler: using %s YV12->BGR32 Converter\n", (flags & SWS_CPU_CAPS_MMX) ? "MMX" : "C");
		else if(dstFormat==PIX_FMT_BGR565)
			av_log(c, AV_LOG_VERBOSE, "SwScaler: using %s YV12->BGR16 Converter\n", (flags & SWS_CPU_CAPS_MMX) ? "MMX" : "C");
		else if(dstFormat==PIX_FMT_BGR555)
			av_log(c, AV_LOG_VERBOSE, "SwScaler: using %s YV12->BGR15 Converter\n", (flags & SWS_CPU_CAPS_MMX) ? "MMX" : "C");

		av_log(c, AV_LOG_VERBOSE, "SwScaler: %dx%d -> %dx%d\n", srcW, srcH, dstW, dstH);
	}
	if(flags & SWS_PRINT_INFO)
	{
		av_log(c, AV_LOG_DEBUG, "SwScaler:Lum srcW=%d srcH=%d dstW=%d dstH=%d xInc=%d yInc=%d\n",
			c->srcW, c->srcH, c->dstW, c->dstH, c->lumXInc, c->lumYInc);
		av_log(c, AV_LOG_DEBUG, "SwScaler:Chr srcW=%d srcH=%d dstW=%d dstH=%d xInc=%d yInc=%d\n",
			c->chrSrcW, c->chrSrcH, c->chrDstW, c->chrDstH, c->chrXInc, c->chrYInc);
	}

	c->swScale= getSwsFunc(flags);
	return c;
}

/**
 * swscale warper, so we don't need to export the SwsContext.
 * assumes planar YUV to be in YUV order instead of YVU
 */
int sws_scale(SwsContext *c, uint8_t* src[], int srcStride[], int srcSliceY,
                           int srcSliceH, uint8_t* dst[], int dstStride[]){
	if (c->sliceDir == 0 && srcSliceY != 0 && srcSliceY + srcSliceH != c->srcH) {
	    av_log(c, AV_LOG_ERROR, "swScaler: slices start in the middle!\n");
	    return 0;
	}
	if (c->sliceDir == 0) {
	    if (srcSliceY == 0) c->sliceDir = 1; else c->sliceDir = -1;
	}

	// copy strides, so they can safely be modified
	if (c->sliceDir == 1) {
            uint8_t* src2[4]= {src[0], src[1], src[2]};
	    // slices go from top to bottom
	    int srcStride2[4]= {srcStride[0], srcStride[1], srcStride[2]};
	    int dstStride2[4]= {dstStride[0], dstStride[1], dstStride[2]};
	    return c->swScale(c, src2, srcStride2, srcSliceY, srcSliceH, dst, dstStride2);
	} else {
	    // slices go from bottom to top => we flip the image internally
	    uint8_t* src2[4]= {src[0] + (srcSliceH-1)*srcStride[0],
			       src[1] + ((srcSliceH>>c->chrSrcVSubSample)-1)*srcStride[1],
			       src[2] + ((srcSliceH>>c->chrSrcVSubSample)-1)*srcStride[2]
	    };
	    uint8_t* dst2[4]= {dst[0] + (c->dstH-1)*dstStride[0],
			       dst[1] + ((c->dstH>>c->chrDstVSubSample)-1)*dstStride[1],
			       dst[2] + ((c->dstH>>c->chrDstVSubSample)-1)*dstStride[2]};
	    int srcStride2[4]= {-srcStride[0], -srcStride[1], -srcStride[2]};
	    int dstStride2[4]= {-dstStride[0], -dstStride[1], -dstStride[2]};
	    
	    return c->swScale(c, src2, srcStride2, c->srcH-srcSliceY-srcSliceH, srcSliceH, dst2, dstStride2);
	}
}

/**
 * swscale warper, so we don't need to export the SwsContext
 */
int sws_scale_ordered(SwsContext *c, uint8_t* src[], int srcStride[], int srcSliceY,
                           int srcSliceH, uint8_t* dst[], int dstStride[]){
	return sws_scale(c, src, srcStride, srcSliceY, srcSliceH, dst, dstStride);
}

SwsFilter *sws_getDefaultFilter(float lumaGBlur, float chromaGBlur, 
				float lumaSharpen, float chromaSharpen,
				float chromaHShift, float chromaVShift,
				int verbose)
{
	SwsFilter *filter= av_malloc(sizeof(SwsFilter));

	if(lumaGBlur!=0.0){
		filter->lumH= sws_getGaussianVec(lumaGBlur, 3.0);
		filter->lumV= sws_getGaussianVec(lumaGBlur, 3.0);
	}else{
		filter->lumH= sws_getIdentityVec();
		filter->lumV= sws_getIdentityVec();
	}

	if(chromaGBlur!=0.0){
		filter->chrH= sws_getGaussianVec(chromaGBlur, 3.0);
		filter->chrV= sws_getGaussianVec(chromaGBlur, 3.0);
	}else{
		filter->chrH= sws_getIdentityVec();
		filter->chrV= sws_getIdentityVec();
	}

	if(chromaSharpen!=0.0){
		SwsVector *id= sws_getIdentityVec();
                sws_scaleVec(filter->chrH, -chromaSharpen);
                sws_scaleVec(filter->chrV, -chromaSharpen);
		sws_addVec(filter->chrH, id);
		sws_addVec(filter->chrV, id);
		sws_freeVec(id);
	}

	if(lumaSharpen!=0.0){
		SwsVector *id= sws_getIdentityVec();
                sws_scaleVec(filter->lumH, -lumaSharpen);
                sws_scaleVec(filter->lumV, -lumaSharpen);
		sws_addVec(filter->lumH, id);
		sws_addVec(filter->lumV, id);
		sws_freeVec(id);
	}

	if(chromaHShift != 0.0)
		sws_shiftVec(filter->chrH, (int)(chromaHShift+0.5));

	if(chromaVShift != 0.0)
		sws_shiftVec(filter->chrV, (int)(chromaVShift+0.5));

	sws_normalizeVec(filter->chrH, 1.0);
	sws_normalizeVec(filter->chrV, 1.0);
	sws_normalizeVec(filter->lumH, 1.0);
	sws_normalizeVec(filter->lumV, 1.0);

	if(verbose) sws_printVec(filter->chrH);
	if(verbose) sws_printVec(filter->lumH);

        return filter;
}

/**
 * returns a normalized gaussian curve used to filter stuff
 * quality=3 is high quality, lowwer is lowwer quality
 */
SwsVector *sws_getGaussianVec(double variance, double quality){
	const int length= (int)(variance*quality + 0.5) | 1;
	int i;
	double *coeff= av_malloc(length*sizeof(double));
	double middle= (length-1)*0.5;
	SwsVector *vec= av_malloc(sizeof(SwsVector));

	vec->coeff= coeff;
	vec->length= length;

	for(i=0; i<length; i++)
	{
		double dist= i-middle;
		coeff[i]= exp( -dist*dist/(2*variance*variance) ) / sqrt(2*variance*PI);
	}

	sws_normalizeVec(vec, 1.0);

	return vec;
}

SwsVector *sws_getConstVec(double c, int length){
	int i;
	double *coeff= av_malloc(length*sizeof(double));
	SwsVector *vec= av_malloc(sizeof(SwsVector));

	vec->coeff= coeff;
	vec->length= length;

	for(i=0; i<length; i++)
		coeff[i]= c;

	return vec;
}


SwsVector *sws_getIdentityVec(void){
        return sws_getConstVec(1.0, 1);
}

double sws_dcVec(SwsVector *a){
	int i;
        double sum=0;

	for(i=0; i<a->length; i++)
		sum+= a->coeff[i];

        return sum;
}

void sws_scaleVec(SwsVector *a, double scalar){
	int i;

	for(i=0; i<a->length; i++)
		a->coeff[i]*= scalar;
}

void sws_normalizeVec(SwsVector *a, double height){
        sws_scaleVec(a, height/sws_dcVec(a));
}

static SwsVector *sws_getConvVec(SwsVector *a, SwsVector *b){
	int length= a->length + b->length - 1;
	double *coeff= av_malloc(length*sizeof(double));
	int i, j;
	SwsVector *vec= av_malloc(sizeof(SwsVector));

	vec->coeff= coeff;
	vec->length= length;

	for(i=0; i<length; i++) coeff[i]= 0.0;

	for(i=0; i<a->length; i++)
	{
		for(j=0; j<b->length; j++)
		{
			coeff[i+j]+= a->coeff[i]*b->coeff[j];
		}
	}

	return vec;
}

static SwsVector *sws_sumVec(SwsVector *a, SwsVector *b){
	int length= FFMAX(a->length, b->length);
	double *coeff= av_malloc(length*sizeof(double));
	int i;
	SwsVector *vec= av_malloc(sizeof(SwsVector));

	vec->coeff= coeff;
	vec->length= length;

	for(i=0; i<length; i++) coeff[i]= 0.0;

	for(i=0; i<a->length; i++) coeff[i + (length-1)/2 - (a->length-1)/2]+= a->coeff[i];
	for(i=0; i<b->length; i++) coeff[i + (length-1)/2 - (b->length-1)/2]+= b->coeff[i];

	return vec;
}

static SwsVector *sws_diffVec(SwsVector *a, SwsVector *b){
	int length= FFMAX(a->length, b->length);
	double *coeff= av_malloc(length*sizeof(double));
	int i;
	SwsVector *vec= av_malloc(sizeof(SwsVector));

	vec->coeff= coeff;
	vec->length= length;

	for(i=0; i<length; i++) coeff[i]= 0.0;

	for(i=0; i<a->length; i++) coeff[i + (length-1)/2 - (a->length-1)/2]+= a->coeff[i];
	for(i=0; i<b->length; i++) coeff[i + (length-1)/2 - (b->length-1)/2]-= b->coeff[i];

	return vec;
}

/* shift left / or right if "shift" is negative */
static SwsVector *sws_getShiftedVec(SwsVector *a, int shift){
	int length= a->length + FFABS(shift)*2;
	double *coeff= av_malloc(length*sizeof(double));
	int i;
	SwsVector *vec= av_malloc(sizeof(SwsVector));

	vec->coeff= coeff;
	vec->length= length;

	for(i=0; i<length; i++) coeff[i]= 0.0;

	for(i=0; i<a->length; i++)
	{
		coeff[i + (length-1)/2 - (a->length-1)/2 - shift]= a->coeff[i];
	}

	return vec;
}

void sws_shiftVec(SwsVector *a, int shift){
	SwsVector *shifted= sws_getShiftedVec(a, shift);
	av_free(a->coeff);
	a->coeff= shifted->coeff;
	a->length= shifted->length;
	av_free(shifted);
}

void sws_addVec(SwsVector *a, SwsVector *b){
	SwsVector *sum= sws_sumVec(a, b);
	av_free(a->coeff);
	a->coeff= sum->coeff;
	a->length= sum->length;
	av_free(sum);
}

void sws_subVec(SwsVector *a, SwsVector *b){
	SwsVector *diff= sws_diffVec(a, b);
	av_free(a->coeff);
	a->coeff= diff->coeff;
	a->length= diff->length;
	av_free(diff);
}

void sws_convVec(SwsVector *a, SwsVector *b){
	SwsVector *conv= sws_getConvVec(a, b);
	av_free(a->coeff);  
	a->coeff= conv->coeff;
	a->length= conv->length;
	av_free(conv);
}

SwsVector *sws_cloneVec(SwsVector *a){
	double *coeff= av_malloc(a->length*sizeof(double));
	int i;
	SwsVector *vec= av_malloc(sizeof(SwsVector));

	vec->coeff= coeff;
	vec->length= a->length;

	for(i=0; i<a->length; i++) coeff[i]= a->coeff[i];

	return vec;
}

void sws_printVec(SwsVector *a){
	int i;
	double max=0;
	double min=0;
	double range;

	for(i=0; i<a->length; i++)
		if(a->coeff[i]>max) max= a->coeff[i];

	for(i=0; i<a->length; i++)
		if(a->coeff[i]<min) min= a->coeff[i];

	range= max - min;

	for(i=0; i<a->length; i++)
	{
		int x= (int)((a->coeff[i]-min)*60.0/range +0.5);
		av_log(NULL, AV_LOG_DEBUG, "%1.3f ", a->coeff[i]);
		for(;x>0; x--) av_log(NULL, AV_LOG_DEBUG, " ");
		av_log(NULL, AV_LOG_DEBUG, "|\n");
	}
}

void sws_freeVec(SwsVector *a){
	if(!a) return;
	av_free(a->coeff);
	a->coeff=NULL;
	a->length=0;
	av_free(a);
}

void sws_freeFilter(SwsFilter *filter){
	if(!filter) return;

	if(filter->lumH) sws_freeVec(filter->lumH);
	if(filter->lumV) sws_freeVec(filter->lumV);
	if(filter->chrH) sws_freeVec(filter->chrH);
	if(filter->chrV) sws_freeVec(filter->chrV);
	av_free(filter);
}


void sws_freeContext(SwsContext *c){
	int i;
	if(!c) return;

	if(c->lumPixBuf)
	{
		for(i=0; i<c->vLumBufSize; i++)
		{
			av_free(c->lumPixBuf[i]);
			c->lumPixBuf[i]=NULL;
		}
		av_free(c->lumPixBuf);
		c->lumPixBuf=NULL;
	}

	if(c->chrPixBuf)
	{
		for(i=0; i<c->vChrBufSize; i++)
		{
			av_free(c->chrPixBuf[i]);
			c->chrPixBuf[i]=NULL;
		}
		av_free(c->chrPixBuf);
		c->chrPixBuf=NULL;
	}

	av_free(c->vLumFilter);
	c->vLumFilter = NULL;
	av_free(c->vChrFilter);
	c->vChrFilter = NULL;
	av_free(c->hLumFilter);
	c->hLumFilter = NULL;
	av_free(c->hChrFilter);
	c->hChrFilter = NULL;
#ifdef HAVE_ALTIVEC
	av_free(c->vYCoeffsBank);
	c->vYCoeffsBank = NULL;
	av_free(c->vCCoeffsBank);
	c->vCCoeffsBank = NULL;
#endif

	av_free(c->vLumFilterPos);
	c->vLumFilterPos = NULL;
	av_free(c->vChrFilterPos);
	c->vChrFilterPos = NULL;
	av_free(c->hLumFilterPos);
	c->hLumFilterPos = NULL;
	av_free(c->hChrFilterPos);
	c->hChrFilterPos = NULL;

#if defined(ARCH_X86) && defined(CONFIG_GPL)
#ifdef MAP_ANONYMOUS
	if(c->funnyYCode) munmap(c->funnyYCode, MAX_FUNNY_CODE_SIZE);
	if(c->funnyUVCode) munmap(c->funnyUVCode, MAX_FUNNY_CODE_SIZE);
#else
	av_free(c->funnyYCode);
	av_free(c->funnyUVCode);
#endif
	c->funnyYCode=NULL;
	c->funnyUVCode=NULL;
#endif /* defined(ARCH_X86) */

	av_free(c->lumMmx2Filter);
	c->lumMmx2Filter=NULL;
	av_free(c->chrMmx2Filter);
	c->chrMmx2Filter=NULL;
	av_free(c->lumMmx2FilterPos);
	c->lumMmx2FilterPos=NULL;
	av_free(c->chrMmx2FilterPos);
	c->chrMmx2FilterPos=NULL;
	av_free(c->yuvTable);
	c->yuvTable=NULL;

	av_free(c);
}

/**
 * Checks if context is valid or reallocs a new one instead.
 * If context is NULL, just calls sws_getContext() to get a new one.
 * Otherwise, checks if the parameters are the same already saved in context.
 * If that is the case, returns the current context.
 * Otherwise, frees context and gets a new one.
 *
 * Be warned that srcFilter, dstFilter are not checked, they are
 * asumed to remain valid.
 */
struct SwsContext *sws_getCachedContext(struct SwsContext *context,
                        int srcW, int srcH, int srcFormat,
                        int dstW, int dstH, int dstFormat, int flags,
                        SwsFilter *srcFilter, SwsFilter *dstFilter, double *param)
{
    if (context != NULL) {
        if ((context->srcW != srcW) || (context->srcH != srcH) ||
            (context->srcFormat != srcFormat) ||
            (context->dstW != dstW) || (context->dstH != dstH) ||
            (context->dstFormat != dstFormat) || (context->flags != flags) ||
            (context->param != param))
        {
            sws_freeContext(context);
            context = NULL;
        }
    }
    if (context == NULL) {
        return sws_getContext(srcW, srcH, srcFormat,
                        dstW, dstH, dstFormat, flags,
                        srcFilter, dstFilter, param);
    }
    return context;
}

