/*
    Copyright (C) 2001-2002 Michael Niedermayer (michaelni@gmx.at)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*
			C	MMX	MMX2	3DNow
isVertDC		Ec	Ec
isVertMinMaxOk		Ec	Ec
doVertLowPass		E		e	e
doVertDefFilter		Ec	Ec	e	e
isHorizDC		Ec	Ec
isHorizMinMaxOk		a	E
doHorizLowPass		E		e	e
doHorizDefFilter	Ec	Ec	e	e
deRing			E		e	e*
Vertical RKAlgo1	E		a	a
Horizontal RKAlgo1			a	a
Vertical X1#		a		E	E
Horizontal X1#		a		E	E
LinIpolDeinterlace	e		E	E*
CubicIpolDeinterlace	a		e	e*
LinBlendDeinterlace	e		E	E*
MedianDeinterlace#	 	Ec	Ec
TempDeNoiser#		E		e	e

* i dont have a 3dnow CPU -> its untested, but noone said it doesnt work so it seems to work
# more or less selfinvented filters so the exactness isnt too meaningfull
E = Exact implementation
e = allmost exact implementation (slightly different rounding,...)
a = alternative / approximate impl
c = checked against the other implementations (-vo md5)
*/

/*
TODO:
remove global/static vars
reduce the time wasted on the mem transfer
implement everything in C at least (done at the moment but ...)
unroll stuff if instructions depend too much on the prior one
we use 8x8 blocks for the horizontal filters, opendivx seems to use 8x4?
move YScale thing to the end instead of fixing QP
write a faster and higher quality deblocking filter :)
make the mainloop more flexible (variable number of blocks at once
	(the if/else stuff per block is slowing things down)
compare the quality & speed of all filters
split this huge file
border remover
optimize c versions
try to unroll inner for(x=0 ... loop to avoid these damn if(x ... checks
smart blur
commandline option for   the deblock / dering thresholds
put fastmemcpy back
dont use #ifdef ARCH_X86 for the asm stuff ... cross compilers? (note cpudetect uses ARCH_X86)
...
*/

//Changelog: use the CVS log

#include "../config.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
//#undef HAVE_MMX2
//#define HAVE_3DNOW
//#undef HAVE_MMX
//#undef ARCH_X86
//#define DEBUG_BRIGHTNESS
//#include "../libvo/fastmemcpy.h"
#include "postprocess.h"
#include "../cpudetect.h"
#include "../mangle.h"

#define MIN(a,b) ((a) > (b) ? (b) : (a))
#define MAX(a,b) ((a) < (b) ? (b) : (a))
#define ABS(a) ((a) > 0 ? (a) : (-(a)))
#define SIGN(a) ((a) > 0 ? 1 : -1)

#define GET_MODE_BUFFER_SIZE 500
#define OPTIONS_ARRAY_SIZE 10

#ifdef ARCH_X86
#define CAN_COMPILE_X86_ASM
#endif

#ifdef CAN_COMPILE_X86_ASM
static volatile uint64_t __attribute__((aligned(8))) packedYOffset=	0x0000000000000000LL;
static volatile uint64_t __attribute__((aligned(8))) packedYScale=	0x0100010001000100LL;
static uint64_t __attribute__((aligned(8))) w05=		0x0005000500050005LL;
static uint64_t __attribute__((aligned(8))) w20=		0x0020002000200020LL;
static uint64_t __attribute__((aligned(8))) w1400=		0x1400140014001400LL;
static uint64_t __attribute__((aligned(8))) bm00000001=		0x00000000000000FFLL;
static uint64_t __attribute__((aligned(8))) bm00010000=		0x000000FF00000000LL;
static uint64_t __attribute__((aligned(8))) bm00001000=		0x00000000FF000000LL;
static uint64_t __attribute__((aligned(8))) bm10000000=		0xFF00000000000000LL;
static uint64_t __attribute__((aligned(8))) bm10000001=		0xFF000000000000FFLL;
static uint64_t __attribute__((aligned(8))) bm11000011=		0xFFFF00000000FFFFLL;
static uint64_t __attribute__((aligned(8))) bm00000011=		0x000000000000FFFFLL;
static uint64_t __attribute__((aligned(8))) bm11111110=		0xFFFFFFFFFFFFFF00LL;
static uint64_t __attribute__((aligned(8))) bm11000000=		0xFFFF000000000000LL;
static uint64_t __attribute__((aligned(8))) bm00011000=		0x000000FFFF000000LL;
static uint64_t __attribute__((aligned(8))) bm00110011=		0x0000FFFF0000FFFFLL;
static uint64_t __attribute__((aligned(8))) bm11001100=		0xFFFF0000FFFF0000LL;
static uint64_t __attribute__((aligned(8))) b00= 		0x0000000000000000LL;
static uint64_t __attribute__((aligned(8))) b01= 		0x0101010101010101LL;
static uint64_t __attribute__((aligned(8))) b02= 		0x0202020202020202LL;
static uint64_t __attribute__((aligned(8))) b0F= 		0x0F0F0F0F0F0F0F0FLL;
static uint64_t __attribute__((aligned(8))) b04= 		0x0404040404040404LL;
static uint64_t __attribute__((aligned(8))) b08= 		0x0808080808080808LL;
static uint64_t __attribute__((aligned(8))) bFF= 		0xFFFFFFFFFFFFFFFFLL;
static uint64_t __attribute__((aligned(8))) b20= 		0x2020202020202020LL;
static uint64_t __attribute__((aligned(8))) b80= 		0x8080808080808080LL;
static uint64_t __attribute__((aligned(8))) mmxDCOffset= 	0x7E7E7E7E7E7E7E7ELL;
static uint64_t __attribute__((aligned(8))) mmxDCThreshold=	0x7C7C7C7C7C7C7C7CLL;
static uint64_t __attribute__((aligned(8))) b3F= 		0x3F3F3F3F3F3F3F3FLL;
static uint64_t __attribute__((aligned(8))) temp0=0;
static uint64_t __attribute__((aligned(8))) temp1=0;
static uint64_t __attribute__((aligned(8))) temp2=0;
static uint64_t __attribute__((aligned(8))) temp3=0;
static uint64_t __attribute__((aligned(8))) temp4=0;
static uint64_t __attribute__((aligned(8))) temp5=0;
static uint64_t __attribute__((aligned(8))) pQPb=0;
static uint64_t __attribute__((aligned(8))) pQPb2=0;
static uint8_t __attribute__((aligned(8))) tempBlocks[8*16*2]; //used for the horizontal code
static uint32_t __attribute__((aligned(4))) maxTmpNoise[4];
#else
static uint64_t packedYOffset=	0x0000000000000000LL;
static uint64_t packedYScale=	0x0100010001000100LL;
#endif

extern int divx_quality;
int newPPFlag=0; //is set if -npp is used
struct PPMode gPPMode[GET_PP_QUALITY_MAX+1];
static int firstTime = 0, firstTime2 = 0;

extern int verbose;

int hFlatnessThreshold= 56 - 16;
int vFlatnessThreshold= 56 - 16;
int deringThreshold= 20;

static int dcOffset;
static int dcThreshold;

//amount of "black" u r willing to loose to get a brightness corrected picture
double maxClippedThreshold= 0.01;

static struct PPFilter filters[]=
{
	{"hb", "hdeblock", 		1, 1, 3, H_DEBLOCK},
	{"vb", "vdeblock", 		1, 2, 4, V_DEBLOCK},
	{"hr", "rkhdeblock", 		1, 1, 3, H_RK1_FILTER},
	{"vr", "rkvdeblock", 		1, 2, 4, V_RK1_FILTER},
	{"h1", "x1hdeblock", 		1, 1, 3, H_X1_FILTER},
	{"v1", "x1vdeblock", 		1, 2, 4, V_X1_FILTER},
	{"dr", "dering", 		1, 5, 6, DERING},
	{"al", "autolevels", 		0, 1, 2, LEVEL_FIX},
	{"lb", "linblenddeint", 	1, 1, 4, LINEAR_BLEND_DEINT_FILTER},
	{"li", "linipoldeint", 		1, 1, 4, LINEAR_IPOL_DEINT_FILTER},
	{"ci", "cubicipoldeint",	1, 1, 4, CUBIC_IPOL_DEINT_FILTER},
	{"md", "mediandeint", 		1, 1, 4, MEDIAN_DEINT_FILTER},
	{"tn", "tmpnoise", 		1, 7, 8, TEMP_NOISE_FILTER},
	{"fq", "forcequant", 		1, 0, 0, FORCE_QUANT},
	{NULL, NULL,0,0,0,0} //End Marker
};

static char *replaceTable[]=
{
	"default", 	"hdeblock:a,vdeblock:a,dering:a,autolevels,tmpnoise:a:150:200:400",
	"de", 		"hdeblock:a,vdeblock:a,dering:a,autolevels,tmpnoise:a:150:200:400",
	"fast", 	"x1hdeblock:a,x1vdeblock:a,dering:a,autolevels,tmpnoise:a:150:200:400",
	"fa", 		"x1hdeblock:a,x1vdeblock:a,dering:a,autolevels,tmpnoise:a:150:200:400",
	NULL //End Marker
};

#ifdef CAN_COMPILE_X86_ASM
static inline void unusedVariableWarningFixer()
{
if(
 packedYOffset + packedYScale + w05 + w20 + w1400 + bm00000001 + bm00010000
 + bm00001000 + bm10000000 + bm10000001 + bm11000011 + bm00000011 + bm11111110
 + bm11000000 + bm00011000 + bm00110011 + bm11001100 + b00 + b01 + b02 + b0F
 + bFF + b20 + b04+ b08 + pQPb2 + b80 + mmxDCOffset + mmxDCThreshold + b3F + temp0 + temp1 + temp2 + temp3 + temp4
 + temp5 + pQPb== 0) b00=0;
}
#endif

#ifdef TIMING
static inline long long rdtsc()
{
	long long l;
	asm volatile(	"rdtsc\n\t"
		: "=A" (l)
	);
//	printf("%d\n", int(l/1000));
	return l;
}
#endif

#ifdef CAN_COMPILE_X86_ASM
static inline void prefetchnta(void *p)
{
	asm volatile(	"prefetchnta (%0)\n\t"
		: : "r" (p)
	);
}

static inline void prefetcht0(void *p)
{
	asm volatile(	"prefetcht0 (%0)\n\t"
		: : "r" (p)
	);
}

static inline void prefetcht1(void *p)
{
	asm volatile(	"prefetcht1 (%0)\n\t"
		: : "r" (p)
	);
}

static inline void prefetcht2(void *p)
{
	asm volatile(	"prefetcht2 (%0)\n\t"
		: : "r" (p)
	);
}
#endif

// The horizontal Functions exist only in C cuz the MMX code is faster with vertical filters and transposing

/**
 * Check if the given 8x8 Block is mostly "flat"
 */
static inline int isHorizDC(uint8_t src[], int stride)
{
	int numEq= 0;
	int y;
	for(y=0; y<BLOCK_SIZE; y++)
	{
		if(((src[0] - src[1] + dcOffset) & 0xFFFF) < dcThreshold) numEq++;
		if(((src[1] - src[2] + dcOffset) & 0xFFFF) < dcThreshold) numEq++;
		if(((src[2] - src[3] + dcOffset) & 0xFFFF) < dcThreshold) numEq++;
		if(((src[3] - src[4] + dcOffset) & 0xFFFF) < dcThreshold) numEq++;
		if(((src[4] - src[5] + dcOffset) & 0xFFFF) < dcThreshold) numEq++;
		if(((src[5] - src[6] + dcOffset) & 0xFFFF) < dcThreshold) numEq++;
		if(((src[6] - src[7] + dcOffset) & 0xFFFF) < dcThreshold) numEq++;
		src+= stride;
	}
	return numEq > hFlatnessThreshold;
}

static inline int isHorizMinMaxOk(uint8_t src[], int stride, int QP)
{
	if(abs(src[0] - src[7]) > 2*QP) return 0;

	return 1;
}

static inline void doHorizDefFilter(uint8_t dst[], int stride, int QP)
{
	int y;
	for(y=0; y<BLOCK_SIZE; y++)
	{
		const int middleEnergy= 5*(dst[4] - dst[5]) + 2*(dst[2] - dst[5]);

		if(ABS(middleEnergy) < 8*QP)
		{
			const int q=(dst[3] - dst[4])/2;
			const int leftEnergy=  5*(dst[2] - dst[1]) + 2*(dst[0] - dst[3]);
			const int rightEnergy= 5*(dst[6] - dst[5]) + 2*(dst[4] - dst[7]);

			int d= ABS(middleEnergy) - MIN( ABS(leftEnergy), ABS(rightEnergy) );
			d= MAX(d, 0);

			d= (5*d + 32) >> 6;
			d*= SIGN(-middleEnergy);

			if(q>0)
			{
				d= d<0 ? 0 : d;
				d= d>q ? q : d;
			}
			else
			{
				d= d>0 ? 0 : d;
				d= d<q ? q : d;
			}

        		dst[3]-= d;
	        	dst[4]+= d;
		}
		dst+= stride;
	}
}

/**
 * Do a horizontal low pass filter on the 10x8 block (dst points to middle 8x8 Block)
 * using the 9-Tap Filter (1,1,2,2,4,2,2,1,1)/16 (C version)
 */
static inline void doHorizLowPass(uint8_t dst[], int stride, int QP)
{

	int y;
	for(y=0; y<BLOCK_SIZE; y++)
	{
		const int first= ABS(dst[-1] - dst[0]) < QP ? dst[-1] : dst[0];
		const int last= ABS(dst[8] - dst[7]) < QP ? dst[8] : dst[7];

		int sums[9];
		sums[0] = first + dst[0];
		sums[1] = dst[0] + dst[1];
		sums[2] = dst[1] + dst[2];
		sums[3] = dst[2] + dst[3];
		sums[4] = dst[3] + dst[4];
		sums[5] = dst[4] + dst[5];
		sums[6] = dst[5] + dst[6];
		sums[7] = dst[6] + dst[7];
		sums[8] = dst[7] + last;

		dst[0]= ((sums[0]<<2) + ((first + sums[2])<<1) + sums[4] + 8)>>4;
		dst[1]= ((dst[1]<<2) + ((first + sums[0] + sums[3])<<1) + sums[5] + 8)>>4;
		dst[2]= ((dst[2]<<2) + ((first + sums[1] + sums[4])<<1) + sums[6] + 8)>>4;
		dst[3]= ((dst[3]<<2) + ((sums[2] + sums[5])<<1) + sums[0] + sums[7] + 8)>>4;
		dst[4]= ((dst[4]<<2) + ((sums[3] + sums[6])<<1) + sums[1] + sums[8] + 8)>>4;
		dst[5]= ((dst[5]<<2) + ((last + sums[7] + sums[4])<<1) + sums[2] + 8)>>4;
		dst[6]= (((last + dst[6])<<2) + ((dst[7] + sums[5])<<1) + sums[3] + 8)>>4;
		dst[7]= ((sums[8]<<2) + ((last + sums[6])<<1) + sums[4] + 8)>>4;

		dst+= stride;
	}
}

/**
 * Experimental Filter 1 (Horizontal)
 * will not damage linear gradients
 * Flat blocks should look like they where passed through the (1,1,2,2,4,2,2,1,1) 9-Tap filter
 * can only smooth blocks at the expected locations (it cant smooth them if they did move)
 * MMX2 version does correct clipping C version doesnt
 * not identical with the vertical one
 */
static inline void horizX1Filter(uint8_t *src, int stride, int QP)
{
	int y;
	static uint64_t *lut= NULL;
	if(lut==NULL)
	{
		int i;
		lut= (uint64_t*)memalign(8, 256*8);
		for(i=0; i<256; i++)
		{
			int v= i < 128 ? 2*i : 2*(i-256);
/*
//Simulate 112242211 9-Tap filter
			uint64_t a= (v/16) & 0xFF;
			uint64_t b= (v/8) & 0xFF;
			uint64_t c= (v/4) & 0xFF;
			uint64_t d= (3*v/8) & 0xFF;
*/
//Simulate piecewise linear interpolation
			uint64_t a= (v/16) & 0xFF;
			uint64_t b= (v*3/16) & 0xFF;
			uint64_t c= (v*5/16) & 0xFF;
			uint64_t d= (7*v/16) & 0xFF;
			uint64_t A= (0x100 - a)&0xFF;
			uint64_t B= (0x100 - b)&0xFF;
			uint64_t C= (0x100 - c)&0xFF;
			uint64_t D= (0x100 - c)&0xFF;

			lut[i]   = (a<<56) | (b<<48) | (c<<40) | (d<<32) |
				(D<<24) | (C<<16) | (B<<8) | (A);
			//lut[i] = (v<<32) | (v<<24);
		}
	}

	for(y=0; y<BLOCK_SIZE; y++)
	{
		int a= src[1] - src[2];
		int b= src[3] - src[4];
		int c= src[5] - src[6];

		int d= MAX(ABS(b) - (ABS(a) + ABS(c))/2, 0);

		if(d < QP)
		{
			int v = d * SIGN(-b);

			src[1] +=v/8;
			src[2] +=v/4;
			src[3] +=3*v/8;
			src[4] -=3*v/8;
			src[5] -=v/4;
			src[6] -=v/8;

		}
		src+=stride;
	}
}


//Note: we have C, MMX, MMX2, 3DNOW version there is no 3DNOW+MMX2 one
//Plain C versions
#if !defined (HAVE_MMX) || defined (RUNTIME_CPUDETECT)
#define COMPILE_C
#endif

#ifdef CAN_COMPILE_X86_ASM

#if (defined (HAVE_MMX) && !defined (HAVE_3DNOW) && !defined (HAVE_MMX2)) || defined (RUNTIME_CPUDETECT)
#define COMPILE_MMX
#endif

#if defined (HAVE_MMX2) || defined (RUNTIME_CPUDETECT)
#define COMPILE_MMX2
#endif

#if (defined (HAVE_3DNOW) && !defined (HAVE_MMX2)) || defined (RUNTIME_CPUDETECT)
#define COMPILE_3DNOW
#endif
#endif //CAN_COMPILE_X86_ASM

#undef HAVE_MMX
#undef HAVE_MMX2
#undef HAVE_3DNOW
#undef ARCH_X86

#ifdef COMPILE_C
#undef HAVE_MMX
#undef HAVE_MMX2
#undef HAVE_3DNOW
#undef ARCH_X86
#define RENAME(a) a ## _C
#include "postprocess_template.c"
#endif

//MMX versions
#ifdef COMPILE_MMX
#undef RENAME
#define HAVE_MMX
#undef HAVE_MMX2
#undef HAVE_3DNOW
#define ARCH_X86
#define RENAME(a) a ## _MMX
#include "postprocess_template.c"
#endif

//MMX2 versions
#ifdef COMPILE_MMX2
#undef RENAME
#define HAVE_MMX
#define HAVE_MMX2
#undef HAVE_3DNOW
#define ARCH_X86
#define RENAME(a) a ## _MMX2
#include "postprocess_template.c"
#endif

//3DNOW versions
#ifdef COMPILE_3DNOW
#undef RENAME
#define HAVE_MMX
#undef HAVE_MMX2
#define HAVE_3DNOW
#define ARCH_X86
#define RENAME(a) a ## _3DNow
#include "postprocess_template.c"
#endif

// minor note: the HAVE_xyz is messed up after that line so dont use it

static inline void postProcess(uint8_t src[], int srcStride, uint8_t dst[], int dstStride, int width, int height,
	QP_STORE_T QPs[], int QPStride, int isColor, struct PPMode *ppMode)
{
	// useing ifs here as they are faster than function pointers allthough the
	// difference wouldnt be messureable here but its much better because
	// someone might exchange the cpu whithout restarting mplayer ;)
#ifdef RUNTIME_CPUDETECT
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		postProcess_MMX2(src, srcStride, dst, dstStride, width, height, QPs, QPStride, isColor, ppMode);
	else if(gCpuCaps.has3DNow)
		postProcess_3DNow(src, srcStride, dst, dstStride, width, height, QPs, QPStride, isColor, ppMode);
	else if(gCpuCaps.hasMMX)
		postProcess_MMX(src, srcStride, dst, dstStride, width, height, QPs, QPStride, isColor, ppMode);
	else
		postProcess_C(src, srcStride, dst, dstStride, width, height, QPs, QPStride, isColor, ppMode);
#else
		postProcess_C(src, srcStride, dst, dstStride, width, height, QPs, QPStride, isColor, ppMode);
#endif
#else //RUNTIME_CPUDETECT
#ifdef HAVE_MMX2
		postProcess_MMX2(src, srcStride, dst, dstStride, width, height, QPs, QPStride, isColor, ppMode);
#elif defined (HAVE_3DNOW)
		postProcess_3DNow(src, srcStride, dst, dstStride, width, height, QPs, QPStride, isColor, ppMode);
#elif defined (HAVE_MMX)
		postProcess_MMX(src, srcStride, dst, dstStride, width, height, QPs, QPStride, isColor, ppMode);
#else
		postProcess_C(src, srcStride, dst, dstStride, width, height, QPs, QPStride, isColor, ppMode);
#endif
#endif //!RUNTIME_CPUDETECT
}

#ifdef HAVE_ODIVX_POSTPROCESS
#include "../opendivx/postprocess.h"
int use_old_pp=0;
#endif

//static void postProcess(uint8_t src[], int srcStride, uint8_t dst[], int dstStride, int width, int height,
//	QP_STORE_T QPs[], int QPStride, int isColor, struct PPMode *ppMode);

/* -pp Command line Help
NOTE/FIXME: put this at an appropriate place (--help, html docs, man mplayer)?
*/
char *help=
"-npp <filterName>[:<option>[:<option>...]][,[-]<filterName>[:<option>...]]...\n"
"long form example:\n"
"-npp vdeblock:autoq,hdeblock:autoq,linblenddeint	-npp default,-vdeblock\n"
"short form example:\n"
"-npp vb:a,hb:a,lb					-npp de,-vb\n"
"more examples:\n"
"-npp tn:64:128:256\n"
"Filters			Options\n"
"short	long name	short	long option	Description\n"
"*	*		a	autoq		cpu power dependant enabler\n"
"			c	chrom		chrominance filtring enabled\n"
"			y	nochrom		chrominance filtring disabled\n"
"hb	hdeblock	(2 Threshold)		horizontal deblocking filter\n"
"                        1. Threshold: default=1, higher -> more deblocking\n"
"                        2. Threshold: default=40, lower -> more deblocking\n"
"			the h & v deblocking filters share these\n"
"			so u cant set different thresholds for h / v\n"
"vb	vdeblock	(2 Threshold)		vertical deblocking filter\n"
"hr	rkhdeblock\n"
"vr	rkvdeblock\n"
"h1	x1hdeblock				Experimental h deblock filter 1\n"
"v1	x1vdeblock				Experimental v deblock filter 1\n"
"dr	dering					Deringing filter\n"
"al	autolevels				automatic brightness / contrast\n"
"			f	fullyrange	stretch luminance to (0..255)\n"
"lb	linblenddeint				linear blend deinterlacer\n"
"li	linipoldeint				linear interpolating deinterlace\n"
"ci	cubicipoldeint				cubic interpolating deinterlacer\n"
"md	mediandeint				median deinterlacer\n"
"de	default					hb:a,vb:a,dr:a,al\n"
"fa	fast					h1:a,v1:a,dr:a,al\n"
"tn	tmpnoise	(3 Thresholds)		Temporal Noise Reducer\n"
"			1. <= 2. <= 3.		larger -> stronger filtering\n"
"fq	forceQuant	<quantizer>		Force quantizer\n"
;

/**
 * returns a PPMode struct which will have a non 0 error variable if an error occured
 * name is the string after "-pp" on the command line
 * quality is a number from 0 to GET_PP_QUALITY_MAX
 */
struct PPMode getPPModeByNameAndQuality(char *name, int quality)
{
	char temp[GET_MODE_BUFFER_SIZE];
	char *p= temp;
	char *filterDelimiters= ",";
	char *optionDelimiters= ":";
	struct PPMode ppMode= {0,0,0,0,0,0,{150,200,400}};
	char *filterToken;

	strncpy(temp, name, GET_MODE_BUFFER_SIZE);

	if(verbose>1) printf("pp: %s\n", name);

	for(;;){
		char *filterName;
		int q= 1000000; //GET_PP_QUALITY_MAX;
		int chrom=-1;
		char *option;
		char *options[OPTIONS_ARRAY_SIZE];
		int i;
		int filterNameOk=0;
		int numOfUnknownOptions=0;
		int enable=1; //does the user want us to enabled or disabled the filter

		filterToken= strtok(p, filterDelimiters);
		if(filterToken == NULL) break;
		p+= strlen(filterToken) + 1; // p points to next filterToken
		filterName= strtok(filterToken, optionDelimiters);
		if(verbose>1) printf("pp: %s::%s\n", filterToken, filterName);

		if(*filterName == '-')
		{
			enable=0;
			filterName++;
		}

		for(;;){ //for all options
			option= strtok(NULL, optionDelimiters);
			if(option == NULL) break;

			if(verbose>1) printf("pp: option: %s\n", option);
			if(!strcmp("autoq", option) || !strcmp("a", option)) q= quality;
			else if(!strcmp("nochrom", option) || !strcmp("y", option)) chrom=0;
			else if(!strcmp("chrom", option) || !strcmp("c", option)) chrom=1;
			else
			{
				options[numOfUnknownOptions] = option;
				numOfUnknownOptions++;
			}
			if(numOfUnknownOptions >= OPTIONS_ARRAY_SIZE-1) break;
		}
		options[numOfUnknownOptions] = NULL;

		/* replace stuff from the replace Table */
		for(i=0; replaceTable[2*i]!=NULL; i++)
		{
			if(!strcmp(replaceTable[2*i], filterName))
			{
				int newlen= strlen(replaceTable[2*i + 1]);
				int plen;
				int spaceLeft;

				if(p==NULL) p= temp, *p=0; 	//last filter
				else p--, *p=',';		//not last filter

				plen= strlen(p);
				spaceLeft= p - temp + plen;
				if(spaceLeft + newlen  >= GET_MODE_BUFFER_SIZE)
				{
					ppMode.error++;
					break;
				}
				memmove(p + newlen, p, plen+1);
				memcpy(p, replaceTable[2*i + 1], newlen);
				filterNameOk=1;
			}
		}

		for(i=0; filters[i].shortName!=NULL; i++)
		{
//			printf("Compareing %s, %s, %s\n", filters[i].shortName,filters[i].longName, filterName);
			if(   !strcmp(filters[i].longName, filterName)
			   || !strcmp(filters[i].shortName, filterName))
			{
				ppMode.lumMode &= ~filters[i].mask;
				ppMode.chromMode &= ~filters[i].mask;

				filterNameOk=1;
				if(!enable) break; // user wants to disable it

				if(q >= filters[i].minLumQuality)
					ppMode.lumMode|= filters[i].mask;
				if(chrom==1 || (chrom==-1 && filters[i].chromDefault))
					if(q >= filters[i].minChromQuality)
						ppMode.chromMode|= filters[i].mask;

				if(filters[i].mask == LEVEL_FIX)
				{
					int o;
					ppMode.minAllowedY= 16;
					ppMode.maxAllowedY= 234;
					for(o=0; options[o]!=NULL; o++)
					{
						if(  !strcmp(options[o],"fullyrange")
						   ||!strcmp(options[o],"f"))
						{
							ppMode.minAllowedY= 0;
							ppMode.maxAllowedY= 255;
							numOfUnknownOptions--;
						}
					}
				}
				else if(filters[i].mask == TEMP_NOISE_FILTER)
				{
					int o;
					int numOfNoises=0;
					ppMode.maxTmpNoise[0]= 150;
					ppMode.maxTmpNoise[1]= 200;
					ppMode.maxTmpNoise[2]= 400;

					for(o=0; options[o]!=NULL; o++)
					{
						char *tail;
						ppMode.maxTmpNoise[numOfNoises]=
							strtol(options[o], &tail, 0);
						if(tail!=options[o])
						{
							numOfNoises++;
							numOfUnknownOptions--;
							if(numOfNoises >= 3) break;
						}
					}
				}
				else if(filters[i].mask == V_DEBLOCK || filters[i].mask == H_DEBLOCK)
				{
					int o;
					ppMode.maxDcDiff=1;
//					hFlatnessThreshold= 40;
//					vFlatnessThreshold= 40;

					for(o=0; options[o]!=NULL && o<2; o++)
					{
						char *tail;
						int val= strtol(options[o], &tail, 0);
						if(tail==options[o]) break;

						numOfUnknownOptions--;
						if(o==0) ppMode.maxDcDiff= val;
						else hFlatnessThreshold=
						     vFlatnessThreshold= val;
					}
				}
				else if(filters[i].mask == FORCE_QUANT)
				{
					int o;
					ppMode.forcedQuant= 15;

					for(o=0; options[o]!=NULL && o<1; o++)
					{
						char *tail;
						int val= strtol(options[o], &tail, 0);
						if(tail==options[o]) break;

						numOfUnknownOptions--;
						ppMode.forcedQuant= val;
					}
				}
			}
		}
		if(!filterNameOk) ppMode.error++;
		ppMode.error += numOfUnknownOptions;
	}

#ifdef HAVE_ODIVX_POSTPROCESS
	if(ppMode.lumMode & H_DEBLOCK) ppMode.oldMode |= PP_DEBLOCK_Y_H;
	if(ppMode.lumMode & V_DEBLOCK) ppMode.oldMode |= PP_DEBLOCK_Y_V;
	if(ppMode.chromMode & H_DEBLOCK) ppMode.oldMode |= PP_DEBLOCK_C_H;
	if(ppMode.chromMode & V_DEBLOCK) ppMode.oldMode |= PP_DEBLOCK_C_V;
	if(ppMode.lumMode & DERING) ppMode.oldMode |= PP_DERING_Y;
	if(ppMode.chromMode & DERING) ppMode.oldMode |= PP_DERING_C;
#endif

	if(verbose>1) printf("pp: lumMode=%X, chromMode=%X\n", ppMode.lumMode, ppMode.chromMode);
	return ppMode;
}

/**
 * Check and load the -npp part of the cmd line
 */
int readNPPOpt(void *conf, char *arg)
{
	int quality;
	
	if(!strcmp("help", arg))
	{
		printf("%s", help);
		exit(1);
	}
	
	for(quality=0; quality<GET_PP_QUALITY_MAX+1; quality++)
	{
		gPPMode[quality]= getPPModeByNameAndQuality(arg, quality);

		if(gPPMode[quality].error) return -1;
	}
	newPPFlag=1;

//divx_quality is passed to postprocess if autoq if off
	divx_quality= GET_PP_QUALITY_MAX;
	firstTime = firstTime2 = 1;
	return 1;
}

int readPPOpt(void *conf, char *arg)
{
  int val;

  if(arg == NULL)
    return -2; // ERR_MISSING_PARAM
  errno = 0;
  val = (int)strtol(arg,NULL,0);
  if(errno != 0)
    return -4;  // What about include cfgparser.h and use ERR_* defines */
  if(val < 0)
    return -3; // ERR_OUT_OF_RANGE

  divx_quality = val;
  firstTime = firstTime2 = 1;

  return 1;
}
  
void revertPPOpt(void *conf, char* opt) 
{
  newPPFlag=0;
  divx_quality=0;
}


/**
 * Obsolete, dont use it, use postprocess2() instead
 * this will check newPPFlag automatically and use postprocess2 if it is set
 * mode = quality if newPPFlag
 */
void  postprocess(unsigned char * src[], int src_stride,
                 unsigned char * dst[], int dst_stride,
                 int horizontal_size,   int vertical_size,
                 QP_STORE_T *QP_store,  int QP_stride,
					  int mode)
{
	struct PPMode ppMode;
	static QP_STORE_T zeroArray[2048/8];

	if(newPPFlag)
	{
		ppMode= gPPMode[mode];
//		printf("%d \n",QP_store[5]);
		postprocess2(src, src_stride, dst, dst_stride,
			horizontal_size, vertical_size, QP_store, QP_stride, &ppMode);

		return;
	}

	if(firstTime && verbose)
	{
		printf("using pp filters 0x%X\n", mode);
		firstTime=0;
	}

	if(QP_store==NULL)
	{
		QP_store= zeroArray;
		QP_stride= 0;
	}

	ppMode.lumMode= mode;
	mode= ((mode&0xFF)>>4) | (mode&0xFFFFFF00);
	ppMode.chromMode= mode;
	ppMode.maxTmpNoise[0]= 700;
	ppMode.maxTmpNoise[1]= 1500;
	ppMode.maxTmpNoise[2]= 3000;
	ppMode.maxAllowedY= 234;
	ppMode.minAllowedY= 16;
	ppMode.maxDcDiff= 1;

#ifdef HAVE_ODIVX_POSTPROCESS
// Note: I could make this shit outside of this file, but it would mean one
// more function call...
	if(use_old_pp){
	    odivx_postprocess(src,src_stride,dst,dst_stride,horizontal_size,vertical_size,QP_store,QP_stride,mode);
	    return;
	}
#endif

	postProcess(src[0], src_stride, dst[0], dst_stride,
		horizontal_size, vertical_size, QP_store, QP_stride, 0, &ppMode);

	horizontal_size >>= 1;
	vertical_size   >>= 1;
	src_stride      >>= 1;
	dst_stride      >>= 1;

	if(ppMode.chromMode)
	{
		postProcess(src[1], src_stride, dst[1], dst_stride,
			horizontal_size, vertical_size, QP_store, QP_stride, 1, &ppMode);
		postProcess(src[2], src_stride, dst[2], dst_stride,
			horizontal_size, vertical_size, QP_store, QP_stride, 2, &ppMode);
	}
	else if(src_stride == dst_stride)
	{
		memcpy(dst[1], src[1], src_stride*vertical_size);
		memcpy(dst[2], src[2], src_stride*vertical_size);
	}
	else
	{
		int y;
		for(y=0; y<vertical_size; y++)
		{
			memcpy(&(dst[1][y*dst_stride]), &(src[1][y*src_stride]), horizontal_size);
			memcpy(&(dst[2][y*dst_stride]), &(src[2][y*src_stride]), horizontal_size);
		}
	}

#if 0
		memset(dst[1], 128, dst_stride*vertical_size);
		memset(dst[2], 128, dst_stride*vertical_size);
#endif
}

void  postprocess2(unsigned char * src[], int src_stride,
                 unsigned char * dst[], int dst_stride,
                 int horizontal_size,   int vertical_size,
                 QP_STORE_T *QP_store,  int QP_stride,
		 struct PPMode *mode)
{

	QP_STORE_T quantArray[2048/8];
	
	if(QP_store==NULL || (mode->lumMode & FORCE_QUANT)) 
	{
		int i;
		QP_store= quantArray;
		QP_stride= 0;
		if(mode->lumMode & FORCE_QUANT)
			for(i=0; i<2048/8; i++) quantArray[i]= mode->forcedQuant;
		else
			for(i=0; i<2048/8; i++) quantArray[i]= 1;
	}

	if(firstTime2 && verbose)
	{
		printf("using npp filters 0x%X/0x%X\n", mode->lumMode, mode->chromMode);
		firstTime2=0;
	}

#ifdef HAVE_ODIVX_POSTPROCESS
// Note: I could make this shit outside of this file, but it would mean one
// more function call...
	if(use_old_pp){
	    odivx_postprocess(src,src_stride,dst,dst_stride,horizontal_size,vertical_size,QP_store,QP_stride,
	    mode->oldMode);
	    return;
	}
#endif

	postProcess(src[0], src_stride, dst[0], dst_stride,
		horizontal_size, vertical_size, QP_store, QP_stride, 0, mode);

	horizontal_size >>= 1;
	vertical_size   >>= 1;
	src_stride      >>= 1;
	dst_stride      >>= 1;

	if(mode->chromMode)
	{
		postProcess(src[1], src_stride, dst[1], dst_stride,
			horizontal_size, vertical_size, QP_store, QP_stride, 1, mode);
		postProcess(src[2], src_stride, dst[2], dst_stride,
			horizontal_size, vertical_size, QP_store, QP_stride, 2, mode);
	}
	else if(src_stride == dst_stride)
	{
		memcpy(dst[1], src[1], src_stride*vertical_size);
		memcpy(dst[2], src[2], src_stride*vertical_size);
	}
	else
	{
		int y;
		for(y=0; y<vertical_size; y++)
		{
			memcpy(&(dst[1][y*dst_stride]), &(src[1][y*src_stride]), horizontal_size);
			memcpy(&(dst[2][y*dst_stride]), &(src[2][y*src_stride]), horizontal_size);
		}
	}
}


/**
 * gets the mode flags for a given quality (larger values mean slower but better postprocessing)
 * with -npp it simply returns quality 
 * 0 <= quality <= 6
 */
int getPpModeForQuality(int quality){
	int modes[1+GET_PP_QUALITY_MAX]= {
		0,
#if 1
		// horizontal filters first
		LUM_H_DEBLOCK,
		LUM_H_DEBLOCK | LUM_V_DEBLOCK,
		LUM_H_DEBLOCK | LUM_V_DEBLOCK | CHROM_H_DEBLOCK,
		LUM_H_DEBLOCK | LUM_V_DEBLOCK | CHROM_H_DEBLOCK | CHROM_V_DEBLOCK,
		LUM_H_DEBLOCK | LUM_V_DEBLOCK | CHROM_H_DEBLOCK | CHROM_V_DEBLOCK | LUM_DERING,
		LUM_H_DEBLOCK | LUM_V_DEBLOCK | CHROM_H_DEBLOCK | CHROM_V_DEBLOCK | LUM_DERING | CHROM_DERING
#else
		// vertical filters first
		LUM_V_DEBLOCK,
		LUM_V_DEBLOCK | LUM_H_DEBLOCK,
		LUM_V_DEBLOCK | LUM_H_DEBLOCK | CHROM_V_DEBLOCK,
		LUM_V_DEBLOCK | LUM_H_DEBLOCK | CHROM_V_DEBLOCK | CHROM_H_DEBLOCK,
		LUM_V_DEBLOCK | LUM_H_DEBLOCK | CHROM_V_DEBLOCK | CHROM_H_DEBLOCK | LUM_DERING,
		LUM_V_DEBLOCK | LUM_H_DEBLOCK | CHROM_V_DEBLOCK | CHROM_H_DEBLOCK | LUM_DERING | CHROM_DERING
#endif
	};

#ifdef HAVE_ODIVX_POSTPROCESS
	int odivx_modes[1+GET_PP_QUALITY_MAX]= {
		0,
		PP_DEBLOCK_Y_H,
		PP_DEBLOCK_Y_H|PP_DEBLOCK_Y_V,
		PP_DEBLOCK_Y_H|PP_DEBLOCK_Y_V|PP_DEBLOCK_C_H,
		PP_DEBLOCK_Y_H|PP_DEBLOCK_Y_V|PP_DEBLOCK_C_H|PP_DEBLOCK_C_V,
		PP_DEBLOCK_Y_H|PP_DEBLOCK_Y_V|PP_DEBLOCK_C_H|PP_DEBLOCK_C_V|PP_DERING_Y,
		PP_DEBLOCK_Y_H|PP_DEBLOCK_Y_V|PP_DEBLOCK_C_H|PP_DEBLOCK_C_V|PP_DERING_Y|PP_DERING_C
	};
	if(use_old_pp) return odivx_modes[quality];
#endif
	if(newPPFlag)	return quality;
	else		return modes[quality];
}


