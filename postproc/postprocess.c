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
reduce the time wasted on the mem transfer
unroll stuff if instructions depend too much on the prior one
move YScale thing to the end instead of fixing QP
write a faster and higher quality deblocking filter :)
make the mainloop more flexible (variable number of blocks at once
	(the if/else stuff per block is slowing things down)
compare the quality & speed of all filters
split this huge file
optimize c versions
try to unroll inner for(x=0 ... loop to avoid these damn if(x ... checks
...
*/

//Changelog: use the CVS log

#include "../config.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
//#undef HAVE_MMX2
//#define HAVE_3DNOW
//#undef HAVE_MMX
//#undef ARCH_X86
//#define DEBUG_BRIGHTNESS
#include "../libvo/fastmemcpy.h"
#include "postprocess.h"
#include "../cpudetect.h"
#include "../mangle.h"

#define MIN(a,b) ((a) > (b) ? (b) : (a))
#define MAX(a,b) ((a) < (b) ? (b) : (a))
#define ABS(a) ((a) > 0 ? (a) : (-(a)))
#define SIGN(a) ((a) > 0 ? 1 : -1)

#define GET_MODE_BUFFER_SIZE 500
#define OPTIONS_ARRAY_SIZE 10
#define BLOCK_SIZE 8
#define TEMP_STRIDE 8
//#define NUM_BLOCKS_AT_ONCE 16 //not used yet

#ifdef ARCH_X86
static uint64_t __attribute__((aligned(8))) w05=		0x0005000500050005LL;
static uint64_t __attribute__((aligned(8))) w20=		0x0020002000200020LL;
static uint64_t __attribute__((aligned(8))) b00= 		0x0000000000000000LL;
static uint64_t __attribute__((aligned(8))) b01= 		0x0101010101010101LL;
static uint64_t __attribute__((aligned(8))) b02= 		0x0202020202020202LL;
static uint64_t __attribute__((aligned(8))) b08= 		0x0808080808080808LL;
static uint64_t __attribute__((aligned(8))) b80= 		0x8080808080808080LL;
#endif

static int verbose= 0;

static const int deringThreshold= 20;

struct PPFilter{
	char *shortName;
	char *longName;
	int chromDefault; 	// is chrominance filtering on by default if this filter is manually activated
	int minLumQuality; 	// minimum quality to turn luminance filtering on
	int minChromQuality;	// minimum quality to turn chrominance filtering on
	int mask; 		// Bitmask to turn this filter on
};

typedef struct PPContext{
	uint8_t *tempBlocks; //used for the horizontal code

	/* we need 64bit here otherwise we´ll going to have a problem
	   after watching a black picture for 5 hours*/
	uint64_t *yHistogram;

	uint64_t __attribute__((aligned(8))) packedYOffset;
	uint64_t __attribute__((aligned(8))) packedYScale;

	/* Temporal noise reducing buffers */
	uint8_t *tempBlured[3];
	int32_t *tempBluredPast[3];

	/* Temporary buffers for handling the last row(s) */
	uint8_t *tempDst;
	uint8_t *tempSrc;

	/* Temporary buffers for handling the last block */
	uint8_t *tempDstBlock;
	uint8_t *tempSrcBlock;
	uint8_t *deintTemp;

	uint64_t __attribute__((aligned(8))) pQPb;
	uint64_t __attribute__((aligned(8))) pQPb2;

	uint64_t __attribute__((aligned(8))) mmxDcOffset[32];
	uint64_t __attribute__((aligned(8))) mmxDcThreshold[32];
	
	QP_STORE_T *nonBQPTable;
	
	int QP;
	int nonBQP;

	int frameNum;

	PPMode ppMode;
} PPContext;

static struct PPFilter filters[]=
{
	{"hb", "hdeblock", 		1, 1, 3, H_DEBLOCK},
	{"vb", "vdeblock", 		1, 2, 4, V_DEBLOCK},
/*	{"hr", "rkhdeblock", 		1, 1, 3, H_RK1_FILTER},
	{"vr", "rkvdeblock", 		1, 2, 4, V_RK1_FILTER},*/
	{"h1", "x1hdeblock", 		1, 1, 3, H_X1_FILTER},
	{"v1", "x1vdeblock", 		1, 2, 4, V_X1_FILTER},
	{"dr", "dering", 		1, 5, 6, DERING},
	{"al", "autolevels", 		0, 1, 2, LEVEL_FIX},
	{"lb", "linblenddeint", 	1, 1, 4, LINEAR_BLEND_DEINT_FILTER},
	{"li", "linipoldeint", 		1, 1, 4, LINEAR_IPOL_DEINT_FILTER},
	{"ci", "cubicipoldeint",	1, 1, 4, CUBIC_IPOL_DEINT_FILTER},
	{"md", "mediandeint", 		1, 1, 4, MEDIAN_DEINT_FILTER},
	{"fd", "ffmpegdeint", 		1, 1, 4, FFMPEG_DEINT_FILTER},
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

#ifdef ARCH_X86
static inline void unusedVariableWarningFixer()
{
	if(w05 + w20 + b00 + b01 + b02 + b08 + b80 == 0) b00=0;
}
#endif

static inline long long rdtsc()
{
	long long l;
	asm volatile(	"rdtsc\n\t"
		: "=A" (l)
	);
//	printf("%d\n", int(l/1000));
	return l;
}

#ifdef ARCH_X86
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
static inline int isHorizDC(uint8_t src[], int stride, PPContext *c)
{
	int numEq= 0;
	int y;
	const int dcOffset= ((c->QP*c->ppMode.baseDcDiff)>>8) + 1;
	const int dcThreshold= dcOffset*2 + 1;
	for(y=0; y<BLOCK_SIZE; y++)
	{
		if(((unsigned)(src[0] - src[1] + dcOffset)) < dcThreshold) numEq++;
		if(((unsigned)(src[1] - src[2] + dcOffset)) < dcThreshold) numEq++;
		if(((unsigned)(src[2] - src[3] + dcOffset)) < dcThreshold) numEq++;
		if(((unsigned)(src[3] - src[4] + dcOffset)) < dcThreshold) numEq++;
		if(((unsigned)(src[4] - src[5] + dcOffset)) < dcThreshold) numEq++;
		if(((unsigned)(src[5] - src[6] + dcOffset)) < dcThreshold) numEq++;
		if(((unsigned)(src[6] - src[7] + dcOffset)) < dcThreshold) numEq++;
		src+= stride;
	}
	return numEq > c->ppMode.flatnessThreshold;
}

/**
 * Check if the middle 8x8 Block in the given 8x16 block is flat
 */
static inline int isVertDC_C(uint8_t src[], int stride, PPContext *c){
	int numEq= 0;
	int y;
	const int dcOffset= ((c->QP*c->ppMode.baseDcDiff)>>8) + 1;
	const int dcThreshold= dcOffset*2 + 1;
	src+= stride*4; // src points to begin of the 8x8 Block
	for(y=0; y<BLOCK_SIZE-1; y++)
	{
		if(((unsigned)(src[0] - src[0+stride] + dcOffset)) < dcThreshold) numEq++;
		if(((unsigned)(src[1] - src[1+stride] + dcOffset)) < dcThreshold) numEq++;
		if(((unsigned)(src[2] - src[2+stride] + dcOffset)) < dcThreshold) numEq++;
		if(((unsigned)(src[3] - src[3+stride] + dcOffset)) < dcThreshold) numEq++;
		if(((unsigned)(src[4] - src[4+stride] + dcOffset)) < dcThreshold) numEq++;
		if(((unsigned)(src[5] - src[5+stride] + dcOffset)) < dcThreshold) numEq++;
		if(((unsigned)(src[6] - src[6+stride] + dcOffset)) < dcThreshold) numEq++;
		if(((unsigned)(src[7] - src[7+stride] + dcOffset)) < dcThreshold) numEq++;
		src+= stride;
	}
	return numEq > c->ppMode.flatnessThreshold;
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

#ifdef ARCH_X86

#if (defined (HAVE_MMX) && !defined (HAVE_3DNOW) && !defined (HAVE_MMX2)) || defined (RUNTIME_CPUDETECT)
#define COMPILE_MMX
#endif

#if defined (HAVE_MMX2) || defined (RUNTIME_CPUDETECT)
#define COMPILE_MMX2
#endif

#if (defined (HAVE_3DNOW) && !defined (HAVE_MMX2)) || defined (RUNTIME_CPUDETECT)
#define COMPILE_3DNOW
#endif
#endif //ARCH_X86

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
	QP_STORE_T QPs[], int QPStride, int isColor, PPMode *ppMode, void *vc)
{
	PPContext *c= (PPContext *)vc;
	c->ppMode= *ppMode; //FIXME

	// useing ifs here as they are faster than function pointers allthough the
	// difference wouldnt be messureable here but its much better because
	// someone might exchange the cpu whithout restarting mplayer ;)
#ifdef RUNTIME_CPUDETECT
#ifdef ARCH_X86
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		postProcess_MMX2(src, srcStride, dst, dstStride, width, height, QPs, QPStride, isColor, c);
	else if(gCpuCaps.has3DNow)
		postProcess_3DNow(src, srcStride, dst, dstStride, width, height, QPs, QPStride, isColor, c);
	else if(gCpuCaps.hasMMX)
		postProcess_MMX(src, srcStride, dst, dstStride, width, height, QPs, QPStride, isColor, c);
	else
		postProcess_C(src, srcStride, dst, dstStride, width, height, QPs, QPStride, isColor, c);
#else
		postProcess_C(src, srcStride, dst, dstStride, width, height, QPs, QPStride, isColor, c);
#endif
#else //RUNTIME_CPUDETECT
#ifdef HAVE_MMX2
		postProcess_MMX2(src, srcStride, dst, dstStride, width, height, QPs, QPStride, isColor, c);
#elif defined (HAVE_3DNOW)
		postProcess_3DNow(src, srcStride, dst, dstStride, width, height, QPs, QPStride, isColor, c);
#elif defined (HAVE_MMX)
		postProcess_MMX(src, srcStride, dst, dstStride, width, height, QPs, QPStride, isColor, c);
#else
		postProcess_C(src, srcStride, dst, dstStride, width, height, QPs, QPStride, isColor, c);
#endif
#endif //!RUNTIME_CPUDETECT
}

//static void postProcess(uint8_t src[], int srcStride, uint8_t dst[], int dstStride, int width, int height,
//	QP_STORE_T QPs[], int QPStride, int isColor, struct PPMode *ppMode);

/* -pp Command line Help
*/
char *postproc_help=
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
"h1	x1hdeblock				Experimental h deblock filter 1\n"
"v1	x1vdeblock				Experimental v deblock filter 1\n"
"dr	dering					Deringing filter\n"
"al	autolevels				automatic brightness / contrast\n"
"			f	fullyrange	stretch luminance to (0..255)\n"
"lb	linblenddeint				linear blend deinterlacer\n"
"li	linipoldeint				linear interpolating deinterlace\n"
"ci	cubicipoldeint				cubic interpolating deinterlacer\n"
"md	mediandeint				median deinterlacer\n"
"fd	ffmpegdeint				ffmpeg deinterlacer\n"
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
struct PPMode pp_get_mode_by_name_and_quality(char *name, int quality)
{
	char temp[GET_MODE_BUFFER_SIZE];
	char *p= temp;
	char *filterDelimiters= ",/";
	char *optionDelimiters= ":";
	struct PPMode ppMode;
	char *filterToken;

	ppMode.lumMode= 0;
	ppMode.chromMode= 0;
	ppMode.maxTmpNoise[0]= 700;
	ppMode.maxTmpNoise[1]= 1500;
	ppMode.maxTmpNoise[2]= 3000;
	ppMode.maxAllowedY= 234;
	ppMode.minAllowedY= 16;
	ppMode.baseDcDiff= 256/4;
	ppMode.flatnessThreshold=40;
	ppMode.flatnessThreshold= 56-16;
	ppMode.maxClippedThreshold= 0.01;
	ppMode.error=0;

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

					for(o=0; options[o]!=NULL && o<2; o++)
					{
						char *tail;
						int val= strtol(options[o], &tail, 0);
						if(tail==options[o]) break;

						numOfUnknownOptions--;
						if(o==0) ppMode.baseDcDiff= val;
						else ppMode.flatnessThreshold= val;
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

	if(verbose>1) printf("pp: lumMode=%X, chromMode=%X\n", ppMode.lumMode, ppMode.chromMode);
	return ppMode;
}

void *pp_get_context(int width, int height){
	PPContext *c= memalign(32, sizeof(PPContext));
	int i;
	int mbWidth = (width+15)>>4;
	int mbHeight= (height+15)>>4;

	c->tempBlocks= (uint8_t*)memalign(8, 2*16*8);
	c->yHistogram= (uint64_t*)memalign(8, 256*sizeof(uint64_t));
	for(i=0; i<256; i++)
		c->yHistogram[i]= width*height/64*15/256;

	for(i=0; i<3; i++)
	{
		//Note:the +17*1024 is just there so i dont have to worry about r/w over te end
		c->tempBlured[i]= (uint8_t*)memalign(8, ((width+7)&(~7))*2*((height+7)&(~7)) + 17*1024); //FIXME dstStride instead of width
		c->tempBluredPast[i]= (uint32_t*)memalign(8, 256*((height+7)&(~7))/2 + 17*1024);

		memset(c->tempBlured[i], 0, ((width+7)&(~7))*2*((height+7)&(~7)) + 17*1024);
		memset(c->tempBluredPast[i], 0, 256*((height+7)&(~7))/2 + 17*1024);
	}
	
	c->tempDst= (uint8_t*)memalign(8, 1024*24);
	c->tempSrc= (uint8_t*)memalign(8, 1024*24);
	c->tempDstBlock= (uint8_t*)memalign(8, 1024*24);
	c->tempSrcBlock= (uint8_t*)memalign(8, 1024*24);
	c->deintTemp= (uint8_t*)memalign(8, width+16);
	c->nonBQPTable= (QP_STORE_T*)memalign(8, mbWidth*mbHeight*sizeof(QP_STORE_T));
	memset(c->nonBQPTable, 0, mbWidth*mbHeight*sizeof(QP_STORE_T));

	c->frameNum=-1;

	return c;
}

void pp_free_context(void *vc){
	PPContext *c = (PPContext*)vc;
	int i;
	
	for(i=0; i<3; i++) free(c->tempBlured[i]);
	for(i=0; i<3; i++) free(c->tempBluredPast[i]);
	
	free(c->tempBlocks);
	free(c->yHistogram);
	free(c->tempDst);
	free(c->tempSrc);
	free(c->tempDstBlock);
	free(c->tempSrcBlock);
	free(c->deintTemp);
	free(c->nonBQPTable);
	
	free(c);
}

void  pp_postprocess(uint8_t * src[3], int srcStride[3],
                 uint8_t * dst[3], int dstStride[3],
                 int width, int height,
                 QP_STORE_T *QP_store,  int QPStride,
		 PPMode *mode,  void *vc, int pict_type)
{
	int mbWidth = (width+15)>>4;
	int mbHeight= (height+15)>>4;
	QP_STORE_T quantArray[2048/8];
	PPContext *c = (PPContext*)vc;

	if(QP_store==NULL || (mode->lumMode & FORCE_QUANT)) 
	{
		int i;
		QP_store= quantArray;
		QPStride= 0;
		if(mode->lumMode & FORCE_QUANT)
			for(i=0; i<2048/8; i++) quantArray[i]= mode->forcedQuant;
		else
			for(i=0; i<2048/8; i++) quantArray[i]= 1;
	}
if(0){
int x,y;
for(y=0; y<mbHeight; y++){
	for(x=0; x<mbWidth; x++){
		printf("%2d ", QP_store[x + y*QPStride]);
	}
	printf("\n");
}
	printf("\n");
}
//printf("pict_type:%d\n", pict_type);

	if(pict_type!=3)
	{
		int x,y;
		for(y=0; y<mbHeight; y++){
			for(x=0; x<mbWidth; x++){
				int qscale= QP_store[x + y*QPStride];
				if(qscale&~31)
				    qscale=31;
				c->nonBQPTable[y*mbWidth + x]= qscale;
			}
		}
	}

	if(verbose>2)
	{
		printf("using npp filters 0x%X/0x%X\n", mode->lumMode, mode->chromMode);
	}

	postProcess(src[0], srcStride[0], dst[0], dstStride[0],
		width, height, QP_store, QPStride, 0, mode, c);

	width  = (width +1)>>1;
	height = (height+1)>>1;

	if(mode->chromMode)
	{
		postProcess(src[1], srcStride[1], dst[1], dstStride[1],
			width, height, QP_store, QPStride, 1, mode, c);
		postProcess(src[2], srcStride[2], dst[2], dstStride[2],
			width, height, QP_store, QPStride, 2, mode, c);
	}
	else if(srcStride[1] == dstStride[1] && srcStride[2] == dstStride[2])
	{
		memcpy(dst[1], src[1], srcStride[1]*height);
		memcpy(dst[2], src[2], srcStride[2]*height);
	}
	else
	{
		int y;
		for(y=0; y<height; y++)
		{
			memcpy(&(dst[1][y*dstStride[1]]), &(src[1][y*srcStride[1]]), width);
			memcpy(&(dst[2][y*dstStride[2]]), &(src[2][y*srcStride[2]]), width);
		}
	}
}

