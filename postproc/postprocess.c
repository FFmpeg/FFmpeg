/*
    Copyright (C) 2001 Michael Niedermayer (michaelni@gmx.at)

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
verify that everything workes as it should (how?)
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
//#define DEBUG_BRIGHTNESS
#include "postprocess.h"

#define MIN(a,b) ((a) > (b) ? (b) : (a))
#define MAX(a,b) ((a) < (b) ? (b) : (a))
#define ABS(a) ((a) > 0 ? (a) : (-(a)))
#define SIGN(a) ((a) > 0 ? 1 : -1)

#ifdef HAVE_MMX2
#define PAVGB(a,b) "pavgb " #a ", " #b " \n\t"
#elif defined (HAVE_3DNOW)
#define PAVGB(a,b) "pavgusb " #a ", " #b " \n\t"
#endif

#ifdef HAVE_MMX2
#define PMINUB(a,b,t) "pminub " #a ", " #b " \n\t"
#elif defined (HAVE_MMX)
#define PMINUB(b,a,t) \
	"movq " #a ", " #t " \n\t"\
	"psubusb " #b ", " #t " \n\t"\
	"psubb " #t ", " #a " \n\t"
#endif

#ifdef HAVE_MMX2
#define PMAXUB(a,b) "pmaxub " #a ", " #b " \n\t"
#elif defined (HAVE_MMX)
#define PMAXUB(a,b) \
	"psubusb " #a ", " #b " \n\t"\
	"paddb " #a ", " #b " \n\t"
#endif


#define GET_MODE_BUFFER_SIZE 500
#define OPTIONS_ARRAY_SIZE 10

#ifdef HAVE_MMX
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
static uint64_t __attribute__((aligned(8))) b7E= 		0x7E7E7E7E7E7E7E7ELL;
static uint64_t __attribute__((aligned(8))) b7C= 		0x7C7C7C7C7C7C7C7CLL;
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
static uint8_t tempBlocks[8*16*2]; //used for the horizontal code
#endif

int hFlatnessThreshold= 56 - 16;
int vFlatnessThreshold= 56 - 16;

//amount of "black" u r willing to loose to get a brightness corrected picture
double maxClippedThreshold= 0.01;

int maxAllowedY=234;
int minAllowedY=16;

static struct PPFilter filters[]=
{
	{"hb", "hdeblock", 		1, 1, 3, H_DEBLOCK},
	{"vb", "vdeblock", 		1, 2, 4, V_DEBLOCK},
	{"vr", "rkvdeblock", 		1, 2, 4, H_RK1_FILTER},
	{"h1", "x1hdeblock", 		1, 1, 3, H_X1_FILTER},
	{"v1", "x1vdeblock", 		1, 2, 4, V_X1_FILTER},
	{"dr", "dering", 		1, 5, 6, DERING},
	{"al", "autolevels", 		0, 1, 2, LEVEL_FIX},
	{"lb", "linblenddeint", 	0, 1, 6, LINEAR_BLEND_DEINT_FILTER},
	{"li", "linipoldeint", 		0, 1, 6, LINEAR_IPOL_DEINT_FILTER},
	{"ci", "cubicipoldeint",	0, 1, 6, CUBIC_IPOL_DEINT_FILTER},
	{"md", "mediandeint", 		0, 1, 6, MEDIAN_DEINT_FILTER},
	{"tn", "tmpnoise", 		1, 7, 8, TEMP_NOISE_FILTER},
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

#ifdef HAVE_MMX
static inline void unusedVariableWarningFixer()
{
if(
 packedYOffset + packedYScale + w05 + w20 + w1400 + bm00000001 + bm00010000
 + bm00001000 + bm10000000 + bm10000001 + bm11000011 + bm00000011 + bm11111110
 + bm11000000 + bm00011000 + bm00110011 + bm11001100 + b00 + b01 + b02 + b0F
 + bFF + b20 + b04+ b08 + pQPb2 + b80 + b7E + b7C + b3F + temp0 + temp1 + temp2 + temp3 + temp4
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

#ifdef HAVE_MMX2
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

//FIXME? |255-0| = 1 (shouldnt be a problem ...)
/**
 * Check if the middle 8x8 Block in the given 8x16 block is flat
 */
static inline int isVertDC(uint8_t src[], int stride){
	int numEq= 0;
#ifndef HAVE_MMX
	int y;
#endif
	src+= stride*4; // src points to begin of the 8x8 Block
#ifdef HAVE_MMX
asm volatile(
		"leal (%1, %2), %%eax				\n\t"
		"leal (%%eax, %2, 4), %%ebx			\n\t"
//	0	1	2	3	4	5	6	7	8	9
//	%1	eax	eax+%2	eax+2%2	%1+4%2	ebx	ebx+%2	ebx+2%2	%1+8%2	ebx+4%2
		"movq b7E, %%mm7					\n\t" // mm7 = 0x7F
		"movq b7C, %%mm6					\n\t" // mm6 = 0x7D
		"movq (%1), %%mm0				\n\t"
		"movq (%%eax), %%mm1				\n\t"
		"psubb %%mm1, %%mm0				\n\t" // mm0 = differnece
		"paddb %%mm7, %%mm0				\n\t"
		"pcmpgtb %%mm6, %%mm0				\n\t"

		"movq (%%eax,%2), %%mm2				\n\t"
		"psubb %%mm2, %%mm1				\n\t"
		"paddb %%mm7, %%mm1				\n\t"
		"pcmpgtb %%mm6, %%mm1				\n\t"
		"paddb %%mm1, %%mm0				\n\t"

		"movq (%%eax, %2, 2), %%mm1			\n\t"
		"psubb %%mm1, %%mm2				\n\t"
		"paddb %%mm7, %%mm2				\n\t"
		"pcmpgtb %%mm6, %%mm2				\n\t"
		"paddb %%mm2, %%mm0				\n\t"

		"movq (%1, %2, 4), %%mm2			\n\t"
		"psubb %%mm2, %%mm1				\n\t"
		"paddb %%mm7, %%mm1				\n\t"
		"pcmpgtb %%mm6, %%mm1				\n\t"
		"paddb %%mm1, %%mm0				\n\t"

		"movq (%%ebx), %%mm1				\n\t"
		"psubb %%mm1, %%mm2				\n\t"
		"paddb %%mm7, %%mm2				\n\t"
		"pcmpgtb %%mm6, %%mm2				\n\t"
		"paddb %%mm2, %%mm0				\n\t"

		"movq (%%ebx, %2), %%mm2			\n\t"
		"psubb %%mm2, %%mm1				\n\t"
		"paddb %%mm7, %%mm1				\n\t"
		"pcmpgtb %%mm6, %%mm1				\n\t"
		"paddb %%mm1, %%mm0				\n\t"

		"movq (%%ebx, %2, 2), %%mm1			\n\t"
		"psubb %%mm1, %%mm2				\n\t"
		"paddb %%mm7, %%mm2				\n\t"
		"pcmpgtb %%mm6, %%mm2				\n\t"
		"paddb %%mm2, %%mm0				\n\t"

		"						\n\t"
		"movq %%mm0, %%mm1				\n\t"
		"psrlw $8, %%mm0				\n\t"
		"paddb %%mm1, %%mm0				\n\t"
#ifdef HAVE_MMX2
		"pshufw $0xF9, %%mm0, %%mm1			\n\t"
		"paddb %%mm1, %%mm0				\n\t"
		"pshufw $0xFE, %%mm0, %%mm1			\n\t"
#else
		"movq %%mm0, %%mm1				\n\t"
		"psrlq $16, %%mm0				\n\t"
		"paddb %%mm1, %%mm0				\n\t"
		"movq %%mm0, %%mm1				\n\t"
		"psrlq $32, %%mm0				\n\t"
#endif
		"paddb %%mm1, %%mm0				\n\t"
		"movd %%mm0, %0					\n\t"
		: "=r" (numEq)
		: "r" (src), "r" (stride)
		: "%eax", "%ebx"
		);

	numEq= (256 - numEq) &0xFF;

#else
	for(y=0; y<BLOCK_SIZE-1; y++)
	{
		if(((src[0] - src[0+stride] + 1)&0xFFFF) < 3) numEq++;
		if(((src[1] - src[1+stride] + 1)&0xFFFF) < 3) numEq++;
		if(((src[2] - src[2+stride] + 1)&0xFFFF) < 3) numEq++;
		if(((src[3] - src[3+stride] + 1)&0xFFFF) < 3) numEq++;
		if(((src[4] - src[4+stride] + 1)&0xFFFF) < 3) numEq++;
		if(((src[5] - src[5+stride] + 1)&0xFFFF) < 3) numEq++;
		if(((src[6] - src[6+stride] + 1)&0xFFFF) < 3) numEq++;
		if(((src[7] - src[7+stride] + 1)&0xFFFF) < 3) numEq++;
		src+= stride;
	}
#endif
/*	if(abs(numEq - asmEq) > 0)
	{
		printf("\nasm:%d  c:%d\n", asmEq, numEq);
		for(int y=0; y<8; y++)
		{
			for(int x=0; x<8; x++)
			{
				printf("%d ", temp[x + y*stride]);
			}
			printf("\n");
		}
	}
*/
//	for(int i=0; i<numEq/8; i++) src[i]=255;
	return (numEq > vFlatnessThreshold) ? 1 : 0;
}

static inline int isVertMinMaxOk(uint8_t src[], int stride, int QP)
{
#ifdef HAVE_MMX
	int isOk;
	src+= stride*3;
	asm volatile(
//		"int $3 \n\t"
		"movq (%1, %2), %%mm0				\n\t"
		"movq (%1, %2, 8), %%mm1			\n\t"
		"movq %%mm0, %%mm2				\n\t"
		"psubusb %%mm1, %%mm0				\n\t"
		"psubusb %%mm2, %%mm1				\n\t"
		"por %%mm1, %%mm0				\n\t" // ABS Diff

		"movq pQPb, %%mm7				\n\t" // QP,..., QP
		"paddusb %%mm7, %%mm7				\n\t" // 2QP ... 2QP
		"psubusb %%mm7, %%mm0				\n\t" // Diff <= 2QP -> 0
		"pcmpeqd b00, %%mm0				\n\t"
		"psrlq $16, %%mm0				\n\t"
		"pcmpeqd bFF, %%mm0				\n\t"
//		"movd %%mm0, (%1, %2, 4)\n\t"
		"movd %%mm0, %0					\n\t"
		: "=r" (isOk)
		: "r" (src), "r" (stride)
		);
	return isOk;
#else

	int isOk2= 1;
	int x;
	src+= stride*3;
	for(x=0; x<BLOCK_SIZE; x++)
	{
		if(abs((int)src[x + stride] - (int)src[x + (stride<<3)]) > 2*QP) isOk2=0;
	}
/*	if(isOk && !isOk2 || !isOk && isOk2)
	{
		printf("\nasm:%d  c:%d QP:%d\n", isOk, isOk2, QP);
		for(int y=0; y<9; y++)
		{
			for(int x=0; x<8; x++)
			{
				printf("%d ", src[x + y*stride]);
			}
			printf("\n");
		}
	} */

	return isOk2;
#endif

}

/**
 * Do a vertical low pass filter on the 8x16 block (only write to the 8x8 block in the middle)
 * using the 9-Tap Filter (1,1,2,2,4,2,2,1,1)/16
 */
static inline void doVertLowPass(uint8_t *src, int stride, int QP)
{
#if defined (HAVE_MMX2) || defined (HAVE_3DNOW)
	src+= stride*3;
	asm volatile(	//"movv %0 %1 %2\n\t"
		"movq pQPb, %%mm0				\n\t"  // QP,..., QP

		"movq (%0), %%mm6				\n\t"
		"movq (%0, %1), %%mm5				\n\t"
		"movq %%mm5, %%mm1				\n\t"
		"movq %%mm6, %%mm2				\n\t"
		"psubusb %%mm6, %%mm5				\n\t"
		"psubusb %%mm1, %%mm2				\n\t"
		"por %%mm5, %%mm2				\n\t" // ABS Diff of lines
		"psubusb %%mm0, %%mm2				\n\t" // diff <= QP -> 0
		"pcmpeqb b00, %%mm2				\n\t" // diff <= QP -> FF

		"pand %%mm2, %%mm6				\n\t"
		"pandn %%mm1, %%mm2				\n\t"
		"por %%mm2, %%mm6				\n\t"// First Line to Filter

		"movq (%0, %1, 8), %%mm5			\n\t"
		"leal (%0, %1, 4), %%eax			\n\t"
		"leal (%0, %1, 8), %%ebx			\n\t"
		"subl %1, %%ebx					\n\t"
		"addl %1, %0					\n\t" // %0 points to line 1 not 0
		"movq (%0, %1, 8), %%mm7			\n\t"
		"movq %%mm5, %%mm1				\n\t"
		"movq %%mm7, %%mm2				\n\t"
		"psubusb %%mm7, %%mm5				\n\t"
		"psubusb %%mm1, %%mm2				\n\t"
		"por %%mm5, %%mm2				\n\t" // ABS Diff of lines
		"psubusb %%mm0, %%mm2				\n\t" // diff <= QP -> 0
		"pcmpeqb b00, %%mm2				\n\t" // diff <= QP -> FF

		"pand %%mm2, %%mm7				\n\t"
		"pandn %%mm1, %%mm2				\n\t"
		"por %%mm2, %%mm7				\n\t" // First Line to Filter


		// 	1	2	3	4	5	6	7	8
		//	%0	%0+%1	%0+2%1	eax	%0+4%1	eax+2%1	ebx	eax+4%1
		// 6 4 2 2 1 1
		// 6 4 4 2
		// 6 8 2

		"movq (%0, %1), %%mm0				\n\t" //  1
		"movq %%mm0, %%mm1				\n\t" //  1
		PAVGB(%%mm6, %%mm0)				      //1 1	/2
		PAVGB(%%mm6, %%mm0)				      //3 1	/4

		"movq (%0, %1, 4), %%mm2			\n\t" //     1
		"movq %%mm2, %%mm5				\n\t" //     1
		PAVGB((%%eax), %%mm2)				      //    11	/2
		PAVGB((%0, %1, 2), %%mm2)			      //   211	/4
		"movq %%mm2, %%mm3				\n\t" //   211	/4
		"movq (%0), %%mm4				\n\t" // 1
		PAVGB(%%mm4, %%mm3)				      // 4 211	/8
		PAVGB(%%mm0, %%mm3)				      //642211	/16
		"movq %%mm3, (%0)				\n\t" // X
		// mm1=2 mm2=3(211) mm4=1 mm5=5 mm6=0 mm7=9
		"movq %%mm1, %%mm0				\n\t" //  1
		PAVGB(%%mm6, %%mm0)				      //1 1	/2
		"movq %%mm4, %%mm3				\n\t" // 1
		PAVGB((%0,%1,2), %%mm3)				      // 1 1	/2
		PAVGB((%%eax,%1,2), %%mm5)			      //     11	/2
		PAVGB((%%eax), %%mm5)				      //    211 /4
		PAVGB(%%mm5, %%mm3)				      // 2 2211 /8
		PAVGB(%%mm0, %%mm3)				      //4242211 /16
		"movq %%mm3, (%0,%1)				\n\t" //  X
		// mm1=2 mm2=3(211) mm4=1 mm5=4(211) mm6=0 mm7=9
		PAVGB(%%mm4, %%mm6)				      //11	/2
		"movq (%%ebx), %%mm0				\n\t" //       1
		PAVGB((%%eax, %1, 2), %%mm0)			      //      11/2
		"movq %%mm0, %%mm3				\n\t" //      11/2
		PAVGB(%%mm1, %%mm0)				      //  2   11/4
		PAVGB(%%mm6, %%mm0)				      //222   11/8
		PAVGB(%%mm2, %%mm0)				      //22242211/16
		"movq (%0, %1, 2), %%mm2			\n\t" //   1
		"movq %%mm0, (%0, %1, 2)			\n\t" //   X
		// mm1=2 mm2=3 mm3=6(11) mm4=1 mm5=4(211) mm6=0(11) mm7=9
		"movq (%%eax, %1, 4), %%mm0			\n\t" //        1
		PAVGB((%%ebx), %%mm0)				      //       11	/2
		PAVGB(%%mm0, %%mm6)				      //11     11	/4
		PAVGB(%%mm1, %%mm4)				      // 11		/2
		PAVGB(%%mm2, %%mm1)				      //  11		/2
		PAVGB(%%mm1, %%mm6)				      //1122   11	/8
		PAVGB(%%mm5, %%mm6)				      //112242211	/16
		"movq (%%eax), %%mm5				\n\t" //    1
		"movq %%mm6, (%%eax)				\n\t" //    X
		// mm0=7(11) mm1=2(11) mm2=3 mm3=6(11) mm4=1(11) mm5=4 mm7=9
		"movq (%%eax, %1, 4), %%mm6			\n\t" //        1
		PAVGB(%%mm7, %%mm6)				      //        11	/2
		PAVGB(%%mm4, %%mm6)				      // 11     11	/4
		PAVGB(%%mm3, %%mm6)				      // 11   2211	/8
		PAVGB(%%mm5, %%mm2)				      //   11		/2
		"movq (%0, %1, 4), %%mm4			\n\t" //     1
		PAVGB(%%mm4, %%mm2)				      //   112		/4
		PAVGB(%%mm2, %%mm6)				      // 112242211	/16
		"movq %%mm6, (%0, %1, 4)			\n\t" //     X
		// mm0=7(11) mm1=2(11) mm2=3(112) mm3=6(11) mm4=5 mm5=4 mm7=9
		PAVGB(%%mm7, %%mm1)				      //  11     2	/4
		PAVGB(%%mm4, %%mm5)				      //    11		/2
		PAVGB(%%mm5, %%mm0)				      //    11 11	/4
		"movq (%%eax, %1, 2), %%mm6			\n\t" //      1
		PAVGB(%%mm6, %%mm1)				      //  11  4  2	/8
		PAVGB(%%mm0, %%mm1)				      //  11224222	/16
		"movq %%mm1, (%%eax, %1, 2)			\n\t" //      X
		// mm2=3(112) mm3=6(11) mm4=5 mm5=4(11) mm6=6 mm7=9
		PAVGB((%%ebx), %%mm2)				      //   112 4	/8
		"movq (%%eax, %1, 4), %%mm0			\n\t" //        1
		PAVGB(%%mm0, %%mm6)				      //      1 1	/2
		PAVGB(%%mm7, %%mm6)				      //      1 12	/4
		PAVGB(%%mm2, %%mm6)				      //   1122424	/4
		"movq %%mm6, (%%ebx)				\n\t" //       X
		// mm0=8 mm3=6(11) mm4=5 mm5=4(11) mm7=9
		PAVGB(%%mm7, %%mm5)				      //    11   2	/4
		PAVGB(%%mm7, %%mm5)				      //    11   6	/8

		PAVGB(%%mm3, %%mm0)				      //      112	/4
		PAVGB(%%mm0, %%mm5)				      //    112246	/16
		"movq %%mm5, (%%eax, %1, 4)			\n\t" //        X
		"subl %1, %0					\n\t"

		:
		: "r" (src), "r" (stride)
		: "%eax", "%ebx"
	);
#else
	const int l1= stride;
	const int l2= stride + l1;
	const int l3= stride + l2;
	const int l4= stride + l3;
	const int l5= stride + l4;
	const int l6= stride + l5;
	const int l7= stride + l6;
	const int l8= stride + l7;
	const int l9= stride + l8;
	int x;
	src+= stride*3;
	for(x=0; x<BLOCK_SIZE; x++)
	{
		const int first= ABS(src[0] - src[l1]) < QP ? src[0] : src[l1];
		const int last= ABS(src[l8] - src[l9]) < QP ? src[l9] : src[l8];

		int sums[9];
		sums[0] = first + src[l1];
		sums[1] = src[l1] + src[l2];
		sums[2] = src[l2] + src[l3];
		sums[3] = src[l3] + src[l4];
		sums[4] = src[l4] + src[l5];
		sums[5] = src[l5] + src[l6];
		sums[6] = src[l6] + src[l7];
		sums[7] = src[l7] + src[l8];
		sums[8] = src[l8] + last;

		src[l1]= ((sums[0]<<2) + ((first + sums[2])<<1) + sums[4] + 8)>>4;
		src[l2]= ((src[l2]<<2) + ((first + sums[0] + sums[3])<<1) + sums[5] + 8)>>4;
		src[l3]= ((src[l3]<<2) + ((first + sums[1] + sums[4])<<1) + sums[6] + 8)>>4;
		src[l4]= ((src[l4]<<2) + ((sums[2] + sums[5])<<1) + sums[0] + sums[7] + 8)>>4;
		src[l5]= ((src[l5]<<2) + ((sums[3] + sums[6])<<1) + sums[1] + sums[8] + 8)>>4;
		src[l6]= ((src[l6]<<2) + ((last + sums[7] + sums[4])<<1) + sums[2] + 8)>>4;
		src[l7]= (((last + src[l7])<<2) + ((src[l8] + sums[5])<<1) + sums[3] + 8)>>4;
		src[l8]= ((sums[8]<<2) + ((last + sums[6])<<1) + sums[4] + 8)>>4;

		src++;
	}

#endif
}

/**
 * Experimental implementation of the filter (Algorithm 1) described in a paper from Ramkishor & Karandikar
 * values are correctly clipped (MMX2)
 * values are wraparound (C)
 * conclusion: its fast, but introduces ugly horizontal patterns if there is a continious gradient
	0 8 16 24
	x = 8
	x/2 = 4
	x/8 = 1
	1 12 12 23
 */
static inline void vertRK1Filter(uint8_t *src, int stride, int QP)
{
#if defined (HAVE_MMX2) || defined (HAVE_3DNOW)
	src+= stride*3;
// FIXME rounding
	asm volatile(
		"pxor %%mm7, %%mm7				\n\t" // 0
		"movq b80, %%mm6				\n\t" // MIN_SIGNED_BYTE
		"leal (%0, %1), %%eax				\n\t"
		"leal (%%eax, %1, 4), %%ebx			\n\t"
//	0	1	2	3	4	5	6	7	8	9
//	%0	eax	eax+%1	eax+2%1	%0+4%1	ebx	ebx+%1	ebx+2%1	%0+8%1	ebx+4%1
		"movq pQPb, %%mm0				\n\t" // QP,..., QP
		"movq %%mm0, %%mm1				\n\t" // QP,..., QP
		"paddusb b02, %%mm0				\n\t"
		"psrlw $2, %%mm0				\n\t"
		"pand b3F, %%mm0				\n\t" // QP/4,..., QP/4
		"paddusb %%mm1, %%mm0				\n\t" // QP*1.25 ...
		"movq (%0, %1, 4), %%mm2			\n\t" // line 4
		"movq (%%ebx), %%mm3				\n\t" // line 5
		"movq %%mm2, %%mm4				\n\t" // line 4
		"pcmpeqb %%mm5, %%mm5				\n\t" // -1
		"pxor %%mm2, %%mm5				\n\t" // -line 4 - 1
		PAVGB(%%mm3, %%mm5)
		"paddb %%mm6, %%mm5				\n\t" // (l5-l4)/2
		"psubusb %%mm3, %%mm4				\n\t"
		"psubusb %%mm2, %%mm3				\n\t"
		"por %%mm3, %%mm4				\n\t" // |l4 - l5|
		"psubusb %%mm0, %%mm4				\n\t"
		"pcmpeqb %%mm7, %%mm4				\n\t"
		"pand %%mm4, %%mm5				\n\t" // d/2

//		"paddb %%mm6, %%mm2				\n\t" // line 4 + 0x80
		"paddb %%mm5, %%mm2				\n\t"
//		"psubb %%mm6, %%mm2				\n\t"
		"movq %%mm2, (%0,%1, 4)				\n\t"

		"movq (%%ebx), %%mm2				\n\t"
//		"paddb %%mm6, %%mm2				\n\t" // line 5 + 0x80
		"psubb %%mm5, %%mm2				\n\t"
//		"psubb %%mm6, %%mm2				\n\t"
		"movq %%mm2, (%%ebx)				\n\t"

		"paddb %%mm6, %%mm5				\n\t"
		"psrlw $2, %%mm5				\n\t"
		"pand b3F, %%mm5				\n\t"
		"psubb b20, %%mm5				\n\t" // (l5-l4)/8

		"movq (%%eax, %1, 2), %%mm2			\n\t"
		"paddb %%mm6, %%mm2				\n\t" // line 3 + 0x80
		"paddsb %%mm5, %%mm2				\n\t"
		"psubb %%mm6, %%mm2				\n\t"
		"movq %%mm2, (%%eax, %1, 2)			\n\t"

		"movq (%%ebx, %1), %%mm2			\n\t"
		"paddb %%mm6, %%mm2				\n\t" // line 6 + 0x80
		"psubsb %%mm5, %%mm2				\n\t"
		"psubb %%mm6, %%mm2				\n\t"
		"movq %%mm2, (%%ebx, %1)			\n\t"

		:
		: "r" (src), "r" (stride)
		: "%eax", "%ebx"
	);
#else
 	const int l1= stride;
	const int l2= stride + l1;
	const int l3= stride + l2;
	const int l4= stride + l3;
	const int l5= stride + l4;
	const int l6= stride + l5;
//	const int l7= stride + l6;
//	const int l8= stride + l7;
//	const int l9= stride + l8;
	int x;
	const int QP15= QP + (QP>>2);
	src+= stride*3;
	for(x=0; x<BLOCK_SIZE; x++)
	{
		const int v = (src[x+l5] - src[x+l4]);
		if(ABS(v) < QP15)
		{
			src[x+l3] +=v>>3;
			src[x+l4] +=v>>1;
			src[x+l5] -=v>>1;
			src[x+l6] -=v>>3;

		}
	}

#endif
}

/**
 * Experimental Filter 1
 * will not damage linear gradients
 * Flat blocks should look like they where passed through the (1,1,2,2,4,2,2,1,1) 9-Tap filter
 * can only smooth blocks at the expected locations (it cant smooth them if they did move)
 * MMX2 version does correct clipping C version doesnt
 */
static inline void vertX1Filter(uint8_t *src, int stride, int QP)
{
#if defined (HAVE_MMX2) || defined (HAVE_3DNOW)
	src+= stride*3;

	asm volatile(
		"pxor %%mm7, %%mm7				\n\t" // 0
//		"movq b80, %%mm6				\n\t" // MIN_SIGNED_BYTE
		"leal (%0, %1), %%eax				\n\t"
		"leal (%%eax, %1, 4), %%ebx			\n\t"
//	0	1	2	3	4	5	6	7	8	9
//	%0	eax	eax+%1	eax+2%1	%0+4%1	ebx	ebx+%1	ebx+2%1	%0+8%1	ebx+4%1
		"movq (%%eax, %1, 2), %%mm0			\n\t" // line 3
		"movq (%0, %1, 4), %%mm1			\n\t" // line 4
		"movq %%mm1, %%mm2				\n\t" // line 4
		"psubusb %%mm0, %%mm1				\n\t"
		"psubusb %%mm2, %%mm0				\n\t"
		"por %%mm1, %%mm0				\n\t" // |l2 - l3|
		"movq (%%ebx), %%mm3				\n\t" // line 5
		"movq (%%ebx, %1), %%mm4				\n\t" // line 6
		"movq %%mm3, %%mm5				\n\t" // line 5
		"psubusb %%mm4, %%mm3				\n\t"
		"psubusb %%mm5, %%mm4				\n\t"
		"por %%mm4, %%mm3				\n\t" // |l5 - l6|
		PAVGB(%%mm3, %%mm0)				      // (|l2 - l3| + |l5 - l6|)/2
		"movq %%mm2, %%mm1				\n\t" // line 4
		"psubusb %%mm5, %%mm2				\n\t"
		"movq %%mm2, %%mm4				\n\t"
		"pcmpeqb %%mm7, %%mm2				\n\t" // (l4 - l5) <= 0 ? -1 : 0
		"psubusb %%mm1, %%mm5				\n\t"
		"por %%mm5, %%mm4				\n\t" // |l4 - l5|
		"psubusb %%mm0, %%mm4		\n\t" //d = MAX(0, |l4-l5| - (|l2-l3| + |l5-l6|)/2)
		"movq %%mm4, %%mm3				\n\t" // d
		"psubusb pQPb, %%mm4				\n\t"
		"pcmpeqb %%mm7, %%mm4				\n\t" // d <= QP ? -1 : 0
		"psubusb b01, %%mm3				\n\t"
		"pand %%mm4, %%mm3				\n\t" // d <= QP ? d : 0

		PAVGB(%%mm7, %%mm3)				      // d/2
		"movq %%mm3, %%mm1				\n\t" // d/2
		PAVGB(%%mm7, %%mm3)				      // d/4
		PAVGB(%%mm1, %%mm3)				      // 3*d/8

		"movq (%0, %1, 4), %%mm0			\n\t" // line 4
		"pxor %%mm2, %%mm0				\n\t" //(l4 - l5) <= 0 ? -l4-1 : l4
		"psubusb %%mm3, %%mm0				\n\t"
		"pxor %%mm2, %%mm0				\n\t"
		"movq %%mm0, (%0, %1, 4)			\n\t" // line 4

		"movq (%%ebx), %%mm0				\n\t" // line 5
		"pxor %%mm2, %%mm0				\n\t" //(l4 - l5) <= 0 ? -l5-1 : l5
		"paddusb %%mm3, %%mm0				\n\t"
		"pxor %%mm2, %%mm0				\n\t"
		"movq %%mm0, (%%ebx)				\n\t" // line 5

		PAVGB(%%mm7, %%mm1)				      // d/4

		"movq (%%eax, %1, 2), %%mm0			\n\t" // line 3
		"pxor %%mm2, %%mm0				\n\t" //(l4 - l5) <= 0 ? -l4-1 : l4
		"psubusb %%mm1, %%mm0				\n\t"
		"pxor %%mm2, %%mm0				\n\t"
		"movq %%mm0, (%%eax, %1, 2)			\n\t" // line 3

		"movq (%%ebx, %1), %%mm0			\n\t" // line 6
		"pxor %%mm2, %%mm0				\n\t" //(l4 - l5) <= 0 ? -l5-1 : l5
		"paddusb %%mm1, %%mm0				\n\t"
		"pxor %%mm2, %%mm0				\n\t"
		"movq %%mm0, (%%ebx, %1)			\n\t" // line 6

		PAVGB(%%mm7, %%mm1)				      // d/8

		"movq (%%eax, %1), %%mm0			\n\t" // line 2
		"pxor %%mm2, %%mm0				\n\t" //(l4 - l5) <= 0 ? -l2-1 : l2
		"psubusb %%mm1, %%mm0				\n\t"
		"pxor %%mm2, %%mm0				\n\t"
		"movq %%mm0, (%%eax, %1)			\n\t" // line 2

		"movq (%%ebx, %1, 2), %%mm0			\n\t" // line 7
		"pxor %%mm2, %%mm0				\n\t" //(l4 - l5) <= 0 ? -l7-1 : l7
		"paddusb %%mm1, %%mm0				\n\t"
		"pxor %%mm2, %%mm0				\n\t"
		"movq %%mm0, (%%ebx, %1, 2)			\n\t" // line 7

		:
		: "r" (src), "r" (stride)
		: "%eax", "%ebx"
	);
#else

 	const int l1= stride;
	const int l2= stride + l1;
	const int l3= stride + l2;
	const int l4= stride + l3;
	const int l5= stride + l4;
	const int l6= stride + l5;
	const int l7= stride + l6;
//	const int l8= stride + l7;
//	const int l9= stride + l8;
	int x;

	src+= stride*3;
	for(x=0; x<BLOCK_SIZE; x++)
	{
		int a= src[l3] - src[l4];
		int b= src[l4] - src[l5];
		int c= src[l5] - src[l6];

		int d= ABS(b) - ((ABS(a) + ABS(c))>>1);
		d= MAX(d, 0);

		if(d < QP)
		{
			int v = d * SIGN(-b);

			src[l2] +=v>>3;
			src[l3] +=v>>2;
			src[l4] +=(3*v)>>3;
			src[l5] -=(3*v)>>3;
			src[l6] -=v>>2;
			src[l7] -=v>>3;

		}
		src++;
	}
	/*
 	const int l1= stride;
	const int l2= stride + l1;
	const int l3= stride + l2;
	const int l4= stride + l3;
	const int l5= stride + l4;
	const int l6= stride + l5;
	const int l7= stride + l6;
	const int l8= stride + l7;
	const int l9= stride + l8;
	for(int x=0; x<BLOCK_SIZE; x++)
	{
		int v2= src[l2];
		int v3= src[l3];
		int v4= src[l4];
		int v5= src[l5];
		int v6= src[l6];
		int v7= src[l7];

		if(ABS(v4-v5)<QP &&  ABS(v4-v5) - (ABS(v3-v4) + ABS(v5-v6))>0 )
		{
			src[l3] = (6*v2 + 4*v3 + 3*v4 + 2*v5 + v6         )/16;
			src[l4] = (3*v2 + 3*v3 + 4*v4 + 3*v5 + 2*v6 + v7  )/16;
			src[l5] = (1*v2 + 2*v3 + 3*v4 + 4*v5 + 3*v6 + 3*v7)/16;
			src[l6] = (       1*v3 + 2*v4 + 3*v5 + 4*v6 + 6*v7)/16;
		}
		src++;
	}
*/
#endif
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

#if 0
	asm volatile(
		"pxor %%mm7, %%mm7				\n\t" // 0
//		"movq b80, %%mm6				\n\t" // MIN_SIGNED_BYTE
		"leal (%0, %1), %%eax				\n\t"
		"leal (%%eax, %1, 4), %%ebx			\n\t"

		"movq b80, %%mm6				\n\t"
		"movd pQPb, %%mm5				\n\t" // QP
		"movq %%mm5, %%mm4				\n\t"
		"paddusb %%mm5, %%mm5				\n\t" // 2QP
		"paddusb %%mm5, %%mm4				\n\t" // 3QP
		"pxor %%mm5, %%mm5				\n\t" // 0
		"psubb %%mm4, %%mm5				\n\t" // -3QP
		"por bm11111110, %%mm5				\n\t" // ...,FF,FF,-3QP
		"psllq $24, %%mm5				\n\t"

//	0	1	2	3	4	5	6	7	8	9
//	%0	eax	eax+%1	eax+2%1	%0+4%1	ebx	ebx+%1	ebx+2%1	%0+8%1	ebx+4%1

#define HX1old(a) \
		"movd " #a ", %%mm0				\n\t"\
		"movd 4" #a ", %%mm1				\n\t"\
		"punpckldq %%mm1, %%mm0				\n\t"\
		"movq %%mm0, %%mm1				\n\t"\
		"movq %%mm0, %%mm2				\n\t"\
		"psrlq $8, %%mm1				\n\t"\
		"psubusb %%mm1, %%mm2				\n\t"\
		"psubusb %%mm0, %%mm1				\n\t"\
		"por %%mm2, %%mm1				\n\t" /* p´x = |px - p(x+1)| */\
		"pcmpeqb %%mm7, %%mm2				\n\t" /* p´x = sgn[px - p(x+1)] */\
		"pshufw $0x00, %%mm1, %%mm3			\n\t" /* p´5 = |p1 - p2| */\
		PAVGB(%%mm1, %%mm3)				      /* p´5 = (|p2-p1| + |p6-p5|)/2 */\
		"psrlq $16, %%mm3				\n\t" /* p´3 = (|p2-p1| + |p6-p5|)/2 */\
		"psubusb %%mm3, %%mm1			\n\t" /* |p3-p4|-(|p2-p1| + |p6-p5|)/2 */\
		"paddb %%mm5, %%mm1				\n\t"\
		"psubusb %%mm5, %%mm1				\n\t"\
		PAVGB(%%mm7, %%mm1)\
		"pxor %%mm2, %%mm1				\n\t"\
		"psubb %%mm2, %%mm1				\n\t"\
		"psrlq $24, %%mm1				\n\t"\
		"movd %%mm1, %%ecx				\n\t"\
		"paddb %%mm6, %%mm0				\n\t"\
		"paddsb (%3, %%ecx, 8), %%mm0			\n\t"\
		"paddb %%mm6, %%mm0				\n\t"\
		"movq %%mm0, " #a "				\n\t"\

/*
HX1old((%0))
HX1old((%%eax))
HX1old((%%eax, %1))
HX1old((%%eax, %1, 2))
HX1old((%0, %1, 4))
HX1old((%%ebx))
HX1old((%%ebx, %1))
HX1old((%%ebx, %1, 2))
*/

//FIXME add some comments, its unreadable ...
#define HX1b(a, c, b, d) \
		"movd " #a ", %%mm0				\n\t"\
		"movd 4" #a ", %%mm1				\n\t"\
		"punpckldq %%mm1, %%mm0				\n\t"\
		"movd " #b ", %%mm4				\n\t"\
		"movq %%mm0, %%mm1				\n\t"\
		"movq %%mm0, %%mm2				\n\t"\
		"psrlq $8, %%mm1				\n\t"\
		"movd 4" #b ", %%mm3				\n\t"\
		"psubusb %%mm1, %%mm2				\n\t"\
		"psubusb %%mm0, %%mm1				\n\t"\
		"por %%mm2, %%mm1				\n\t" /* p´x = |px - p(x+1)| */\
		"pcmpeqb %%mm7, %%mm2				\n\t" /* p´x = sgn[px - p(x+1)] */\
		"punpckldq %%mm3, %%mm4				\n\t"\
		"movq %%mm1, %%mm3				\n\t"\
		"psllq $32, %%mm3				\n\t" /* p´5 = |p1 - p2| */\
		PAVGB(%%mm1, %%mm3)				      /* p´5 = (|p2-p1| + |p6-p5|)/2 */\
		"paddb %%mm6, %%mm0				\n\t"\
		"psrlq $16, %%mm3				\n\t" /* p´3 = (|p2-p1| + |p6-p5|)/2 */\
		"psubusb %%mm3, %%mm1			\n\t" /* |p3-p4|-(|p2-p1| + |p6-p5|)/2 */\
		"movq %%mm4, %%mm3				\n\t"\
		"paddb %%mm5, %%mm1				\n\t"\
		"psubusb %%mm5, %%mm1				\n\t"\
		"psrlq $8, %%mm3				\n\t"\
		PAVGB(%%mm7, %%mm1)\
		"pxor %%mm2, %%mm1				\n\t"\
		"psubb %%mm2, %%mm1				\n\t"\
		"movq %%mm4, %%mm2				\n\t"\
		"psrlq $24, %%mm1				\n\t"\
		"psubusb %%mm3, %%mm2				\n\t"\
		"movd %%mm1, %%ecx				\n\t"\
		"psubusb %%mm4, %%mm3				\n\t"\
		"paddsb (%2, %%ecx, 8), %%mm0			\n\t"\
		"por %%mm2, %%mm3				\n\t" /* p´x = |px - p(x+1)| */\
		"paddb %%mm6, %%mm0				\n\t"\
		"pcmpeqb %%mm7, %%mm2				\n\t" /* p´x = sgn[px - p(x+1)] */\
		"movq %%mm3, %%mm1				\n\t"\
		"psllq $32, %%mm1				\n\t" /* p´5 = |p1 - p2| */\
		"movq %%mm0, " #a "				\n\t"\
		PAVGB(%%mm3, %%mm1)				      /* p´5 = (|p2-p1| + |p6-p5|)/2 */\
		"paddb %%mm6, %%mm4				\n\t"\
		"psrlq $16, %%mm1				\n\t" /* p´3 = (|p2-p1| + |p6-p5|)/2 */\
		"psubusb %%mm1, %%mm3			\n\t" /* |p3-p4|-(|p2-p1| + |p6-p5|)/2 */\
		"paddb %%mm5, %%mm3				\n\t"\
		"psubusb %%mm5, %%mm3				\n\t"\
		PAVGB(%%mm7, %%mm3)\
		"pxor %%mm2, %%mm3				\n\t"\
		"psubb %%mm2, %%mm3				\n\t"\
		"psrlq $24, %%mm3				\n\t"\
		"movd " #c ", %%mm0				\n\t"\
		"movd 4" #c ", %%mm1				\n\t"\
		"punpckldq %%mm1, %%mm0				\n\t"\
		"paddb %%mm6, %%mm0				\n\t"\
		"paddsb (%2, %%ecx, 8), %%mm0			\n\t"\
		"paddb %%mm6, %%mm0				\n\t"\
		"movq %%mm0, " #c "				\n\t"\
		"movd %%mm3, %%ecx				\n\t"\
		"movd " #d ", %%mm0				\n\t"\
		"paddsb (%2, %%ecx, 8), %%mm4			\n\t"\
		"movd 4" #d ", %%mm1				\n\t"\
		"paddb %%mm6, %%mm4				\n\t"\
		"punpckldq %%mm1, %%mm0				\n\t"\
		"movq %%mm4, " #b "				\n\t"\
		"paddb %%mm6, %%mm0				\n\t"\
		"paddsb (%2, %%ecx, 8), %%mm0			\n\t"\
		"paddb %%mm6, %%mm0				\n\t"\
		"movq %%mm0, " #d "				\n\t"\

HX1b((%0),(%%eax),(%%eax, %1),(%%eax, %1, 2))
HX1b((%0, %1, 4),(%%ebx),(%%ebx, %1),(%%ebx, %1, 2))


		:
		: "r" (src), "r" (stride), "r" (lut)
		: "%eax", "%ebx", "%ecx"
	);
#else

//FIXME (has little in common with the mmx2 version)
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
#endif
}


static inline void doVertDefFilter(uint8_t src[], int stride, int QP)
{
#if defined (HAVE_MMX2) || defined (HAVE_3DNOW)
/*
	uint8_t tmp[16];
	const int l1= stride;
	const int l2= stride + l1;
	const int l3= stride + l2;
	const int l4= (int)tmp - (int)src - stride*3;
	const int l5= (int)tmp - (int)src - stride*3 + 8;
	const int l6= stride*3 + l3;
	const int l7= stride + l6;
	const int l8= stride + l7;

	memcpy(tmp, src+stride*7, 8);
	memcpy(tmp+8, src+stride*8, 8);
*/
	src+= stride*4;
	asm volatile(

#if 0 //sligtly more accurate and slightly slower
		"pxor %%mm7, %%mm7				\n\t" // 0
		"leal (%0, %1), %%eax				\n\t"
		"leal (%%eax, %1, 4), %%ebx			\n\t"
//	0	1	2	3	4	5	6	7
//	%0	%0+%1	%0+2%1	eax+2%1	%0+4%1	eax+4%1	ebx+%1	ebx+2%1
//	%0	eax	eax+%1	eax+2%1	%0+4%1	ebx	ebx+%1	ebx+2%1


		"movq (%0, %1, 2), %%mm0			\n\t" // l2
		"movq (%0), %%mm1				\n\t" // l0
		"movq %%mm0, %%mm2				\n\t" // l2
		PAVGB(%%mm7, %%mm0)				      // ~l2/2
		PAVGB(%%mm1, %%mm0)				      // ~(l2 + 2l0)/4
		PAVGB(%%mm2, %%mm0)				      // ~(5l2 + 2l0)/8

		"movq (%%eax), %%mm1				\n\t" // l1
		"movq (%%eax, %1, 2), %%mm3			\n\t" // l3
		"movq %%mm1, %%mm4				\n\t" // l1
		PAVGB(%%mm7, %%mm1)				      // ~l1/2
		PAVGB(%%mm3, %%mm1)				      // ~(l1 + 2l3)/4
		PAVGB(%%mm4, %%mm1)				      // ~(5l1 + 2l3)/8

		"movq %%mm0, %%mm4				\n\t" // ~(5l2 + 2l0)/8
		"psubusb %%mm1, %%mm0				\n\t"
		"psubusb %%mm4, %%mm1				\n\t"
		"por %%mm0, %%mm1				\n\t" // ~|2l0 - 5l1 + 5l2 - 2l3|/8
// mm1= |lenergy|, mm2= l2, mm3= l3, mm7=0

		"movq (%0, %1, 4), %%mm0			\n\t" // l4
		"movq %%mm0, %%mm4				\n\t" // l4
		PAVGB(%%mm7, %%mm0)				      // ~l4/2
		PAVGB(%%mm2, %%mm0)				      // ~(l4 + 2l2)/4
		PAVGB(%%mm4, %%mm0)				      // ~(5l4 + 2l2)/8

		"movq (%%ebx), %%mm2				\n\t" // l5
		"movq %%mm3, %%mm5				\n\t" // l3
		PAVGB(%%mm7, %%mm3)				      // ~l3/2
		PAVGB(%%mm2, %%mm3)				      // ~(l3 + 2l5)/4
		PAVGB(%%mm5, %%mm3)				      // ~(5l3 + 2l5)/8

		"movq %%mm0, %%mm6				\n\t" // ~(5l4 + 2l2)/8
		"psubusb %%mm3, %%mm0				\n\t"
		"psubusb %%mm6, %%mm3				\n\t"
		"por %%mm0, %%mm3				\n\t" // ~|2l2 - 5l3 + 5l4 - 2l5|/8
		"pcmpeqb %%mm7, %%mm0				\n\t" // SIGN(2l2 - 5l3 + 5l4 - 2l5)
// mm0= SIGN(menergy), mm1= |lenergy|, mm2= l5, mm3= |menergy|, mm4=l4, mm5= l3, mm7=0

		"movq (%%ebx, %1), %%mm6			\n\t" // l6
		"movq %%mm6, %%mm5				\n\t" // l6
		PAVGB(%%mm7, %%mm6)				      // ~l6/2
		PAVGB(%%mm4, %%mm6)				      // ~(l6 + 2l4)/4
		PAVGB(%%mm5, %%mm6)				      // ~(5l6 + 2l4)/8

		"movq (%%ebx, %1, 2), %%mm5			\n\t" // l7
		"movq %%mm2, %%mm4				\n\t" // l5
		PAVGB(%%mm7, %%mm2)				      // ~l5/2
		PAVGB(%%mm5, %%mm2)				      // ~(l5 + 2l7)/4
		PAVGB(%%mm4, %%mm2)				      // ~(5l5 + 2l7)/8

		"movq %%mm6, %%mm4				\n\t" // ~(5l6 + 2l4)/8
		"psubusb %%mm2, %%mm6				\n\t"
		"psubusb %%mm4, %%mm2				\n\t"
		"por %%mm6, %%mm2				\n\t" // ~|2l4 - 5l5 + 5l6 - 2l7|/8
// mm0= SIGN(menergy), mm1= |lenergy|/8, mm2= |renergy|/8, mm3= |menergy|/8, mm7=0


		PMINUB(%%mm2, %%mm1, %%mm4)			      // MIN(|lenergy|,|renergy|)/8
		"movq pQPb, %%mm4				\n\t" // QP //FIXME QP+1 ?
		"paddusb b01, %%mm4				\n\t"
		"pcmpgtb %%mm3, %%mm4				\n\t" // |menergy|/8 < QP
		"psubusb %%mm1, %%mm3				\n\t" // d=|menergy|/8-MIN(|lenergy|,|renergy|)/8
		"pand %%mm4, %%mm3				\n\t"

		"movq %%mm3, %%mm1				\n\t"
//		"psubusb b01, %%mm3				\n\t"
		PAVGB(%%mm7, %%mm3)
		PAVGB(%%mm7, %%mm3)
		"paddusb %%mm1, %%mm3				\n\t"
//		"paddusb b01, %%mm3				\n\t"

		"movq (%%eax, %1, 2), %%mm6			\n\t" //l3
		"movq (%0, %1, 4), %%mm5			\n\t" //l4
		"movq (%0, %1, 4), %%mm4			\n\t" //l4
		"psubusb %%mm6, %%mm5				\n\t"
		"psubusb %%mm4, %%mm6				\n\t"
		"por %%mm6, %%mm5				\n\t" // |l3-l4|
		"pcmpeqb %%mm7, %%mm6				\n\t" // SIGN(l3-l4)
		"pxor %%mm6, %%mm0				\n\t"
		"pand %%mm0, %%mm3				\n\t"
		PMINUB(%%mm5, %%mm3, %%mm0)

		"psubusb b01, %%mm3				\n\t"
		PAVGB(%%mm7, %%mm3)

		"movq (%%eax, %1, 2), %%mm0			\n\t"
		"movq (%0, %1, 4), %%mm2			\n\t"
		"pxor %%mm6, %%mm0				\n\t"
		"pxor %%mm6, %%mm2				\n\t"
		"psubb %%mm3, %%mm0				\n\t"
		"paddb %%mm3, %%mm2				\n\t"
		"pxor %%mm6, %%mm0				\n\t"
		"pxor %%mm6, %%mm2				\n\t"
		"movq %%mm0, (%%eax, %1, 2)			\n\t"
		"movq %%mm2, (%0, %1, 4)			\n\t"
#endif

		"leal (%0, %1), %%eax				\n\t"
		"pcmpeqb %%mm6, %%mm6				\n\t" // -1
//	0	1	2	3	4	5	6	7
//	%0	%0+%1	%0+2%1	eax+2%1	%0+4%1	eax+4%1	ebx+%1	ebx+2%1
//	%0	eax	eax+%1	eax+2%1	%0+4%1	ebx	ebx+%1	ebx+2%1


		"movq (%%eax, %1, 2), %%mm1			\n\t" // l3
		"movq (%0, %1, 4), %%mm0			\n\t" // l4
		"pxor %%mm6, %%mm1				\n\t" // -l3-1
		PAVGB(%%mm1, %%mm0)				      // -q+128 = (l4-l3+256)/2
// mm1=-l3-1, mm0=128-q

		"movq (%%eax, %1, 4), %%mm2			\n\t" // l5
		"movq (%%eax, %1), %%mm3			\n\t" // l2
		"pxor %%mm6, %%mm2				\n\t" // -l5-1
		"movq %%mm2, %%mm5				\n\t" // -l5-1
		"movq b80, %%mm4				\n\t" // 128
		"leal (%%eax, %1, 4), %%ebx			\n\t"
		PAVGB(%%mm3, %%mm2)				      // (l2-l5+256)/2
		PAVGB(%%mm0, %%mm4)				      // ~(l4-l3)/4 + 128
		PAVGB(%%mm2, %%mm4)				      // ~(l2-l5)/4 +(l4-l3)/8 + 128
		PAVGB(%%mm0, %%mm4)				      // ~(l2-l5)/8 +5(l4-l3)/16 + 128
// mm1=-l3-1, mm0=128-q, mm3=l2, mm4=menergy/16 + 128, mm5= -l5-1

		"movq (%%eax), %%mm2				\n\t" // l1
		"pxor %%mm6, %%mm2				\n\t" // -l1-1
		PAVGB(%%mm3, %%mm2)				      // (l2-l1+256)/2
		PAVGB((%0), %%mm1)				      // (l0-l3+256)/2
		"movq b80, %%mm3				\n\t" // 128
		PAVGB(%%mm2, %%mm3)				      // ~(l2-l1)/4 + 128
		PAVGB(%%mm1, %%mm3)				      // ~(l0-l3)/4 +(l2-l1)/8 + 128
		PAVGB(%%mm2, %%mm3)				      // ~(l0-l3)/8 +5(l2-l1)/16 + 128
// mm0=128-q, mm3=lenergy/16 + 128, mm4= menergy/16 + 128, mm5= -l5-1

		PAVGB((%%ebx, %1), %%mm5)			      // (l6-l5+256)/2
		"movq (%%ebx, %1, 2), %%mm1			\n\t" // l7
		"pxor %%mm6, %%mm1				\n\t" // -l7-1
		PAVGB((%0, %1, 4), %%mm1)			      // (l4-l7+256)/2
		"movq b80, %%mm2				\n\t" // 128
		PAVGB(%%mm5, %%mm2)				      // ~(l6-l5)/4 + 128
		PAVGB(%%mm1, %%mm2)				      // ~(l4-l7)/4 +(l6-l5)/8 + 128
		PAVGB(%%mm5, %%mm2)				      // ~(l4-l7)/8 +5(l6-l5)/16 + 128
// mm0=128-q, mm2=renergy/16 + 128, mm3=lenergy/16 + 128, mm4= menergy/16 + 128

		"movq b00, %%mm1				\n\t" // 0
		"movq b00, %%mm5				\n\t" // 0
		"psubb %%mm2, %%mm1				\n\t" // 128 - renergy/16
		"psubb %%mm3, %%mm5				\n\t" // 128 - lenergy/16
		PMAXUB(%%mm1, %%mm2)				      // 128 + |renergy/16|
 		PMAXUB(%%mm5, %%mm3)				      // 128 + |lenergy/16|
		PMINUB(%%mm2, %%mm3, %%mm1)			      // 128 + MIN(|lenergy|,|renergy|)/16

// mm0=128-q, mm3=128 + MIN(|lenergy|,|renergy|)/16, mm4= menergy/16 + 128

		"movq b00, %%mm7				\n\t" // 0
		"movq pQPb, %%mm2				\n\t" // QP
		PAVGB(%%mm6, %%mm2)				      // 128 + QP/2
		"psubb %%mm6, %%mm2				\n\t"

		"movq %%mm4, %%mm1				\n\t"
		"pcmpgtb %%mm7, %%mm1				\n\t" // SIGN(menergy)
		"pxor %%mm1, %%mm4				\n\t"
		"psubb %%mm1, %%mm4				\n\t" // 128 + |menergy|/16
		"pcmpgtb %%mm4, %%mm2				\n\t" // |menergy|/16 < QP/2
		"psubusb %%mm3, %%mm4				\n\t" //d=|menergy|/16 - MIN(|lenergy|,|renergy|)/16
// mm0=128-q, mm1= SIGN(menergy), mm2= |menergy|/16 < QP/2, mm4= d/16

		"movq %%mm4, %%mm3				\n\t" // d
		"psubusb b01, %%mm4				\n\t"
		PAVGB(%%mm7, %%mm4)				      // d/32
		PAVGB(%%mm7, %%mm4)				      // (d + 32)/64
		"paddb %%mm3, %%mm4				\n\t" // 5d/64
		"pand %%mm2, %%mm4				\n\t"

		"movq b80, %%mm5				\n\t" // 128
		"psubb %%mm0, %%mm5				\n\t" // q
		"paddsb %%mm6, %%mm5				\n\t" // fix bad rounding
		"pcmpgtb %%mm5, %%mm7				\n\t" // SIGN(q)
		"pxor %%mm7, %%mm5				\n\t"

		PMINUB(%%mm5, %%mm4, %%mm3)			      // MIN(|q|, 5d/64)
		"pxor %%mm1, %%mm7				\n\t" // SIGN(d*q)

		"pand %%mm7, %%mm4				\n\t"
		"movq (%%eax, %1, 2), %%mm0			\n\t"
		"movq (%0, %1, 4), %%mm2			\n\t"
		"pxor %%mm1, %%mm0				\n\t"
		"pxor %%mm1, %%mm2				\n\t"
		"paddb %%mm4, %%mm0				\n\t"
		"psubb %%mm4, %%mm2				\n\t"
		"pxor %%mm1, %%mm0				\n\t"
		"pxor %%mm1, %%mm2				\n\t"
		"movq %%mm0, (%%eax, %1, 2)			\n\t"
		"movq %%mm2, (%0, %1, 4)			\n\t"

		:
		: "r" (src), "r" (stride)
		: "%eax", "%ebx"
	);

/*
	{
	int x;
	src-= stride;
	for(x=0; x<BLOCK_SIZE; x++)
	{
		const int middleEnergy= 5*(src[l5] - src[l4]) + 2*(src[l3] - src[l6]);
		if(ABS(middleEnergy)< 8*QP)
		{
			const int q=(src[l4] - src[l5])/2;
			const int leftEnergy=  5*(src[l3] - src[l2]) + 2*(src[l1] - src[l4]);
			const int rightEnergy= 5*(src[l7] - src[l6]) + 2*(src[l5] - src[l8]);

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

        		src[l4]-= d;
	        	src[l5]+= d;
		}
		src++;
	}
src-=8;
	for(x=0; x<8; x++)
	{
		int y;
		for(y=4; y<6; y++)
		{
			int d= src[x+y*stride] - tmp[x+(y-4)*8];
			int ad= ABS(d);
			static int max=0;
			static int sum=0;
			static int num=0;
			static int bias=0;

			if(max<ad) max=ad;
			sum+= ad>3 ? 1 : 0;
			if(ad>3)
			{
				src[0] = src[7] = src[stride*7] = src[(stride+1)*7]=255;
			}
			if(y==4) bias+=d;
			num++;
			if(num%1000000 == 0)
			{
				printf(" %d %d %d %d\n", num, sum, max, bias);
			}
		}
	}
}
*/
#elif defined (HAVE_MMX)
	src+= stride*4;

	asm volatile(
		"pxor %%mm7, %%mm7				\n\t"
		"leal (%0, %1), %%eax				\n\t"
		"leal (%%eax, %1, 4), %%ebx			\n\t"
//	0	1	2	3	4	5	6	7
//	%0	%0+%1	%0+2%1	eax+2%1	%0+4%1	eax+4%1	ebx+%1	ebx+2%1
//	%0	eax	eax+%1	eax+2%1	%0+4%1	ebx	ebx+%1	ebx+2%1

		"movq (%0), %%mm0				\n\t"
		"movq %%mm0, %%mm1				\n\t"
		"punpcklbw %%mm7, %%mm0				\n\t" // low part of line 0
		"punpckhbw %%mm7, %%mm1				\n\t" // high part of line 0

		"movq (%%eax), %%mm2				\n\t"
		"movq %%mm2, %%mm3				\n\t"
		"punpcklbw %%mm7, %%mm2				\n\t" // low part of line 1
		"punpckhbw %%mm7, %%mm3				\n\t" // high part of line 1

		"movq (%%eax, %1), %%mm4			\n\t"
		"movq %%mm4, %%mm5				\n\t"
		"punpcklbw %%mm7, %%mm4				\n\t" // low part of line 2
		"punpckhbw %%mm7, %%mm5				\n\t" // high part of line 2

		"paddw %%mm0, %%mm0				\n\t" // 2L0
		"paddw %%mm1, %%mm1				\n\t" // 2H0
		"psubw %%mm4, %%mm2				\n\t" // L1 - L2
		"psubw %%mm5, %%mm3				\n\t" // H1 - H2
		"psubw %%mm2, %%mm0				\n\t" // 2L0 - L1 + L2
		"psubw %%mm3, %%mm1				\n\t" // 2H0 - H1 + H2

		"psllw $2, %%mm2				\n\t" // 4L1 - 4L2
		"psllw $2, %%mm3				\n\t" // 4H1 - 4H2
		"psubw %%mm2, %%mm0				\n\t" // 2L0 - 5L1 + 5L2
		"psubw %%mm3, %%mm1				\n\t" // 2H0 - 5H1 + 5H2

		"movq (%%eax, %1, 2), %%mm2			\n\t"
		"movq %%mm2, %%mm3				\n\t"
		"punpcklbw %%mm7, %%mm2				\n\t" // L3
		"punpckhbw %%mm7, %%mm3				\n\t" // H3

		"psubw %%mm2, %%mm0				\n\t" // 2L0 - 5L1 + 5L2 - L3
		"psubw %%mm3, %%mm1				\n\t" // 2H0 - 5H1 + 5H2 - H3
		"psubw %%mm2, %%mm0				\n\t" // 2L0 - 5L1 + 5L2 - 2L3
		"psubw %%mm3, %%mm1				\n\t" // 2H0 - 5H1 + 5H2 - 2H3
		"movq %%mm0, temp0				\n\t" // 2L0 - 5L1 + 5L2 - 2L3
		"movq %%mm1, temp1				\n\t" // 2H0 - 5H1 + 5H2 - 2H3

		"movq (%0, %1, 4), %%mm0			\n\t"
		"movq %%mm0, %%mm1				\n\t"
		"punpcklbw %%mm7, %%mm0				\n\t" // L4
		"punpckhbw %%mm7, %%mm1				\n\t" // H4

		"psubw %%mm0, %%mm2				\n\t" // L3 - L4
		"psubw %%mm1, %%mm3				\n\t" // H3 - H4
		"movq %%mm2, temp2				\n\t" // L3 - L4
		"movq %%mm3, temp3				\n\t" // H3 - H4
		"paddw %%mm4, %%mm4				\n\t" // 2L2
		"paddw %%mm5, %%mm5				\n\t" // 2H2
		"psubw %%mm2, %%mm4				\n\t" // 2L2 - L3 + L4
		"psubw %%mm3, %%mm5				\n\t" // 2H2 - H3 + H4

		"psllw $2, %%mm2				\n\t" // 4L3 - 4L4
		"psllw $2, %%mm3				\n\t" // 4H3 - 4H4
		"psubw %%mm2, %%mm4				\n\t" // 2L2 - 5L3 + 5L4
		"psubw %%mm3, %%mm5				\n\t" // 2H2 - 5H3 + 5H4
//50 opcodes so far
		"movq (%%ebx), %%mm2				\n\t"
		"movq %%mm2, %%mm3				\n\t"
		"punpcklbw %%mm7, %%mm2				\n\t" // L5
		"punpckhbw %%mm7, %%mm3				\n\t" // H5
		"psubw %%mm2, %%mm4				\n\t" // 2L2 - 5L3 + 5L4 - L5
		"psubw %%mm3, %%mm5				\n\t" // 2H2 - 5H3 + 5H4 - H5
		"psubw %%mm2, %%mm4				\n\t" // 2L2 - 5L3 + 5L4 - 2L5
		"psubw %%mm3, %%mm5				\n\t" // 2H2 - 5H3 + 5H4 - 2H5

		"movq (%%ebx, %1), %%mm6			\n\t"
		"punpcklbw %%mm7, %%mm6				\n\t" // L6
		"psubw %%mm6, %%mm2				\n\t" // L5 - L6
		"movq (%%ebx, %1), %%mm6			\n\t"
		"punpckhbw %%mm7, %%mm6				\n\t" // H6
		"psubw %%mm6, %%mm3				\n\t" // H5 - H6

		"paddw %%mm0, %%mm0				\n\t" // 2L4
		"paddw %%mm1, %%mm1				\n\t" // 2H4
		"psubw %%mm2, %%mm0				\n\t" // 2L4 - L5 + L6
		"psubw %%mm3, %%mm1				\n\t" // 2H4 - H5 + H6

		"psllw $2, %%mm2				\n\t" // 4L5 - 4L6
		"psllw $2, %%mm3				\n\t" // 4H5 - 4H6
		"psubw %%mm2, %%mm0				\n\t" // 2L4 - 5L5 + 5L6
		"psubw %%mm3, %%mm1				\n\t" // 2H4 - 5H5 + 5H6

		"movq (%%ebx, %1, 2), %%mm2			\n\t"
		"movq %%mm2, %%mm3				\n\t"
		"punpcklbw %%mm7, %%mm2				\n\t" // L7
		"punpckhbw %%mm7, %%mm3				\n\t" // H7

		"paddw %%mm2, %%mm2				\n\t" // 2L7
		"paddw %%mm3, %%mm3				\n\t" // 2H7
		"psubw %%mm2, %%mm0				\n\t" // 2L4 - 5L5 + 5L6 - 2L7
		"psubw %%mm3, %%mm1				\n\t" // 2H4 - 5H5 + 5H6 - 2H7

		"movq temp0, %%mm2				\n\t" // 2L0 - 5L1 + 5L2 - 2L3
		"movq temp1, %%mm3				\n\t" // 2H0 - 5H1 + 5H2 - 2H3

#ifdef HAVE_MMX2
		"movq %%mm7, %%mm6				\n\t" // 0
		"psubw %%mm0, %%mm6				\n\t"
		"pmaxsw %%mm6, %%mm0				\n\t" // |2L4 - 5L5 + 5L6 - 2L7|
		"movq %%mm7, %%mm6				\n\t" // 0
		"psubw %%mm1, %%mm6				\n\t"
		"pmaxsw %%mm6, %%mm1				\n\t" // |2H4 - 5H5 + 5H6 - 2H7|
		"movq %%mm7, %%mm6				\n\t" // 0
		"psubw %%mm2, %%mm6				\n\t"
		"pmaxsw %%mm6, %%mm2				\n\t" // |2L0 - 5L1 + 5L2 - 2L3|
		"movq %%mm7, %%mm6				\n\t" // 0
		"psubw %%mm3, %%mm6				\n\t"
		"pmaxsw %%mm6, %%mm3				\n\t" // |2H0 - 5H1 + 5H2 - 2H3|
#else
		"movq %%mm7, %%mm6				\n\t" // 0
		"pcmpgtw %%mm0, %%mm6				\n\t"
		"pxor %%mm6, %%mm0				\n\t"
		"psubw %%mm6, %%mm0				\n\t" // |2L4 - 5L5 + 5L6 - 2L7|
		"movq %%mm7, %%mm6				\n\t" // 0
		"pcmpgtw %%mm1, %%mm6				\n\t"
		"pxor %%mm6, %%mm1				\n\t"
		"psubw %%mm6, %%mm1				\n\t" // |2H4 - 5H5 + 5H6 - 2H7|
		"movq %%mm7, %%mm6				\n\t" // 0
		"pcmpgtw %%mm2, %%mm6				\n\t"
		"pxor %%mm6, %%mm2				\n\t"
		"psubw %%mm6, %%mm2				\n\t" // |2L0 - 5L1 + 5L2 - 2L3|
		"movq %%mm7, %%mm6				\n\t" // 0
		"pcmpgtw %%mm3, %%mm6				\n\t"
		"pxor %%mm6, %%mm3				\n\t"
		"psubw %%mm6, %%mm3				\n\t" // |2H0 - 5H1 + 5H2 - 2H3|
#endif

#ifdef HAVE_MMX2
		"pminsw %%mm2, %%mm0				\n\t"
		"pminsw %%mm3, %%mm1				\n\t"
#else
		"movq %%mm0, %%mm6				\n\t"
		"psubusw %%mm2, %%mm6				\n\t"
		"psubw %%mm6, %%mm0				\n\t"
		"movq %%mm1, %%mm6				\n\t"
		"psubusw %%mm3, %%mm6				\n\t"
		"psubw %%mm6, %%mm1				\n\t"
#endif

		"movq %%mm7, %%mm6				\n\t" // 0
		"pcmpgtw %%mm4, %%mm6				\n\t" // sign(2L2 - 5L3 + 5L4 - 2L5)
		"pxor %%mm6, %%mm4				\n\t"
		"psubw %%mm6, %%mm4				\n\t" // |2L2 - 5L3 + 5L4 - 2L5|
		"pcmpgtw %%mm5, %%mm7				\n\t" // sign(2H2 - 5H3 + 5H4 - 2H5)
		"pxor %%mm7, %%mm5				\n\t"
		"psubw %%mm7, %%mm5				\n\t" // |2H2 - 5H3 + 5H4 - 2H5|
// 100 opcodes
		"movd %2, %%mm2					\n\t" // QP
		"punpcklwd %%mm2, %%mm2				\n\t"
		"punpcklwd %%mm2, %%mm2				\n\t"
		"psllw $3, %%mm2				\n\t" // 8QP
		"movq %%mm2, %%mm3				\n\t" // 8QP
		"pcmpgtw %%mm4, %%mm2				\n\t"
		"pcmpgtw %%mm5, %%mm3				\n\t"
		"pand %%mm2, %%mm4				\n\t"
		"pand %%mm3, %%mm5				\n\t"


		"psubusw %%mm0, %%mm4				\n\t" // hd
		"psubusw %%mm1, %%mm5				\n\t" // ld


		"movq w05, %%mm2				\n\t" // 5
		"pmullw %%mm2, %%mm4				\n\t"
		"pmullw %%mm2, %%mm5				\n\t"
		"movq w20, %%mm2				\n\t" // 32
		"paddw %%mm2, %%mm4				\n\t"
		"paddw %%mm2, %%mm5				\n\t"
		"psrlw $6, %%mm4				\n\t"
		"psrlw $6, %%mm5				\n\t"

/*
		"movq w06, %%mm2				\n\t" // 6
		"paddw %%mm2, %%mm4				\n\t"
		"paddw %%mm2, %%mm5				\n\t"
		"movq w1400, %%mm2				\n\t" // 1400h = 5120 = 5/64*2^16
//FIXME if *5/64 is supposed to be /13 then we should use 5041 instead of 5120
		"pmulhw %%mm2, %%mm4				\n\t" // hd/13
		"pmulhw %%mm2, %%mm5				\n\t" // ld/13
*/

		"movq temp2, %%mm0				\n\t" // L3 - L4
		"movq temp3, %%mm1				\n\t" // H3 - H4

		"pxor %%mm2, %%mm2				\n\t"
		"pxor %%mm3, %%mm3				\n\t"

		"pcmpgtw %%mm0, %%mm2				\n\t" // sign (L3-L4)
		"pcmpgtw %%mm1, %%mm3				\n\t" // sign (H3-H4)
		"pxor %%mm2, %%mm0				\n\t"
		"pxor %%mm3, %%mm1				\n\t"
		"psubw %%mm2, %%mm0				\n\t" // |L3-L4|
		"psubw %%mm3, %%mm1				\n\t" // |H3-H4|
		"psrlw $1, %%mm0				\n\t" // |L3 - L4|/2
		"psrlw $1, %%mm1				\n\t" // |H3 - H4|/2

		"pxor %%mm6, %%mm2				\n\t"
		"pxor %%mm7, %%mm3				\n\t"
		"pand %%mm2, %%mm4				\n\t"
		"pand %%mm3, %%mm5				\n\t"

#ifdef HAVE_MMX2
		"pminsw %%mm0, %%mm4				\n\t"
		"pminsw %%mm1, %%mm5				\n\t"
#else
		"movq %%mm4, %%mm2				\n\t"
		"psubusw %%mm0, %%mm2				\n\t"
		"psubw %%mm2, %%mm4				\n\t"
		"movq %%mm5, %%mm2				\n\t"
		"psubusw %%mm1, %%mm2				\n\t"
		"psubw %%mm2, %%mm5				\n\t"
#endif
		"pxor %%mm6, %%mm4				\n\t"
		"pxor %%mm7, %%mm5				\n\t"
		"psubw %%mm6, %%mm4				\n\t"
		"psubw %%mm7, %%mm5				\n\t"
		"packsswb %%mm5, %%mm4				\n\t"
		"movq (%%eax, %1, 2), %%mm0			\n\t"
		"paddb   %%mm4, %%mm0				\n\t"
		"movq %%mm0, (%%eax, %1, 2) 			\n\t"
		"movq (%0, %1, 4), %%mm0			\n\t"
		"psubb %%mm4, %%mm0				\n\t"
		"movq %%mm0, (%0, %1, 4) 			\n\t"

		:
		: "r" (src), "r" (stride), "r" (QP)
		: "%eax", "%ebx"
	);
#else
	const int l1= stride;
	const int l2= stride + l1;
	const int l3= stride + l2;
	const int l4= stride + l3;
	const int l5= stride + l4;
	const int l6= stride + l5;
	const int l7= stride + l6;
	const int l8= stride + l7;
//	const int l9= stride + l8;
	int x;
	src+= stride*3;
	for(x=0; x<BLOCK_SIZE; x++)
	{
		const int middleEnergy= 5*(src[l5] - src[l4]) + 2*(src[l3] - src[l6]);
		if(ABS(middleEnergy) < 8*QP)
		{
			const int q=(src[l4] - src[l5])/2;
			const int leftEnergy=  5*(src[l3] - src[l2]) + 2*(src[l1] - src[l4]);
			const int rightEnergy= 5*(src[l7] - src[l6]) + 2*(src[l5] - src[l8]);

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

        		src[l4]-= d;
	        	src[l5]+= d;
		}
		src++;
	}
#endif
}

//FIXME?  |255-0| = 1
/**
 * Check if the given 8x8 Block is mostly "flat"
 */
static inline int isHorizDC(uint8_t src[], int stride)
{
//	src++;
	int numEq= 0;
#if 0
asm volatile (
//		"int $3 \n\t"
		"leal (%1, %2), %%ecx				\n\t"
		"leal (%%ecx, %2, 4), %%ebx			\n\t"
//	0	1	2	3	4	5	6	7	8	9
//	%1	ecx	ecx+%2	ecx+2%2	%1+4%2	ebx	ebx+%2	ebx+2%2	%1+8%2	ebx+4%2
		"movq b7E, %%mm7				\n\t" // mm7 = 0x7F
		"movq b7C, %%mm6				\n\t" // mm6 = 0x7D
		"pxor %%mm0, %%mm0				\n\t"
		"movl %1, %%eax					\n\t"
		"andl $0x1F, %%eax				\n\t"
		"cmpl $24, %%eax				\n\t"
		"leal tempBlock, %%eax				\n\t"
		"jb 1f						\n\t"

#define HDC_CHECK_AND_CPY(src, dst) \
		"movd " #src ", %%mm2				\n\t"\
		"punpckldq 4" #src ", %%mm2				\n\t" /* (%1) */\
		"movq %%mm2, %%mm1				\n\t"\
		"psrlq $8, %%mm2				\n\t"\
		"psubb %%mm1, %%mm2				\n\t"\
		"paddb %%mm7, %%mm2				\n\t"\
		"pcmpgtb %%mm6, %%mm2				\n\t"\
		"paddb %%mm2, %%mm0				\n\t"\
		"movq %%mm1," #dst "(%%eax)			\n\t"

		HDC_CHECK_AND_CPY((%1),0)
		HDC_CHECK_AND_CPY((%%ecx),8)
		HDC_CHECK_AND_CPY((%%ecx, %2),16)
		HDC_CHECK_AND_CPY((%%ecx, %2, 2),24)
		HDC_CHECK_AND_CPY((%1, %2, 4),32)
		HDC_CHECK_AND_CPY((%%ebx),40)
		HDC_CHECK_AND_CPY((%%ebx, %2),48)
		HDC_CHECK_AND_CPY((%%ebx, %2, 2),56)
		"jmp 2f						\n\t"
		"1:						\n\t"
// src does not cross a 32 byte cache line so dont waste time with alignment
#define HDC_CHECK_AND_CPY2(src, dst) \
		"movq " #src ", %%mm2				\n\t"\
		"movq " #src ", %%mm1				\n\t"\
		"psrlq $8, %%mm2				\n\t"\
		"psubb %%mm1, %%mm2				\n\t"\
		"paddb %%mm7, %%mm2				\n\t"\
		"pcmpgtb %%mm6, %%mm2				\n\t"\
		"paddb %%mm2, %%mm0				\n\t"\
		"movq %%mm1," #dst "(%%eax)			\n\t"

		HDC_CHECK_AND_CPY2((%1),0)
		HDC_CHECK_AND_CPY2((%%ecx),8)
		HDC_CHECK_AND_CPY2((%%ecx, %2),16)
		HDC_CHECK_AND_CPY2((%%ecx, %2, 2),24)
		HDC_CHECK_AND_CPY2((%1, %2, 4),32)
		HDC_CHECK_AND_CPY2((%%ebx),40)
		HDC_CHECK_AND_CPY2((%%ebx, %2),48)
		HDC_CHECK_AND_CPY2((%%ebx, %2, 2),56)
		"2:						\n\t"
		"psllq $8, %%mm0				\n\t" // remove dummy value
		"movq %%mm0, %%mm1				\n\t"
		"psrlw $8, %%mm0				\n\t"
		"paddb %%mm1, %%mm0				\n\t"
		"movq %%mm0, %%mm1				\n\t"
		"psrlq $16, %%mm0				\n\t"
		"paddb %%mm1, %%mm0				\n\t"
		"movq %%mm0, %%mm1				\n\t"
		"psrlq $32, %%mm0				\n\t"
		"paddb %%mm1, %%mm0				\n\t"
		"movd %%mm0, %0					\n\t"
		: "=r" (numEq)
		: "r" (src), "r" (stride)
		: "%eax", "%ebx", "%ecx"
		);
//	printf("%d\n", numEq);
	numEq= (256 - numEq) &0xFF;
#else
	int y;
	for(y=0; y<BLOCK_SIZE; y++)
	{
		if(((src[0] - src[1] + 1) & 0xFFFF) < 3) numEq++;
		if(((src[1] - src[2] + 1) & 0xFFFF) < 3) numEq++;
		if(((src[2] - src[3] + 1) & 0xFFFF) < 3) numEq++;
		if(((src[3] - src[4] + 1) & 0xFFFF) < 3) numEq++;
		if(((src[4] - src[5] + 1) & 0xFFFF) < 3) numEq++;
		if(((src[5] - src[6] + 1) & 0xFFFF) < 3) numEq++;
		if(((src[6] - src[7] + 1) & 0xFFFF) < 3) numEq++;
		src+= stride;
	}
#endif
/*	if(abs(numEq - asmEq) > 0)
	{
//		printf("\nasm:%d  c:%d\n", asmEq, numEq);
		for(int y=0; y<8; y++)
		{
			for(int x=0; x<8; x++)
			{
				printf("%d ", src[x + y*stride]);
			}
			printf("\n");
		}
	}
*/
//	printf("%d\n", numEq);
	return numEq > hFlatnessThreshold;
}

static inline int isHorizMinMaxOk(uint8_t src[], int stride, int QP)
{
	if(abs(src[0] - src[7]) > 2*QP) return 0;

	return 1;
}

static inline void doHorizDefFilter(uint8_t dst[], int stride, int QP)
{
#if 0
	asm volatile(
		"leal (%0, %1), %%ecx				\n\t"
		"leal (%%ecx, %1, 4), %%ebx			\n\t"
//	0	1	2	3	4	5	6	7	8	9
//	%0	ecx	ecx+%1	ecx+2%1	%0+4%1	ebx	ebx+%1	ebx+2%1	%0+8%1	ebx+4%1
		"pxor %%mm7, %%mm7				\n\t"
		"movq bm00001000, %%mm6				\n\t"
		"movd %2, %%mm5					\n\t" // QP
		"movq %%mm5, %%mm4				\n\t"
		"paddusb %%mm5, %%mm5				\n\t" // 2QP
		"paddusb %%mm5, %%mm4				\n\t" // 3QP
		"psllq $24, %%mm4				\n\t"
		"pxor %%mm5, %%mm5				\n\t" // 0
		"psubb %%mm4, %%mm5				\n\t" // -QP
		"leal tempBlock, %%eax				\n\t"

//FIXME? "unroll by 2" and mix
#ifdef HAVE_MMX2
#define HDF(src, dst)	\
		"movq " #src "(%%eax), %%mm0			\n\t"\
		"movq " #src "(%%eax), %%mm1			\n\t"\
		"movq " #src "(%%eax), %%mm2			\n\t"\
		"psrlq $8, %%mm1				\n\t"\
		"psubusb %%mm1, %%mm2				\n\t"\
		"psubusb %%mm0, %%mm1				\n\t"\
		"por %%mm2, %%mm1				\n\t" /* p´x = |px - p(x+1)| */\
		"pcmpeqb %%mm7, %%mm2				\n\t" /* p´x = sgn[px - p(x+1)] */\
		"pshufw $0x00, %%mm1, %%mm3			\n\t" /* p´5 = |p1 - p2| */\
		"pminub %%mm1, %%mm3				\n\t" /* p´5 = min(|p2-p1|, |p6-p5|)*/\
		"psrlq $16, %%mm3				\n\t" /* p´3 = min(|p2-p1|, |p6-p5|)*/\
		"psubusb %%mm3, %%mm1			\n\t" /* |p3-p4|-min(|p1-p2|,|p5-p6|) */\
		"paddb %%mm5, %%mm1				\n\t"\
		"psubusb %%mm5, %%mm1				\n\t"\
		"psrlw $2, %%mm1				\n\t"\
		"pxor %%mm2, %%mm1				\n\t"\
		"psubb %%mm2, %%mm1				\n\t"\
		"pand %%mm6, %%mm1				\n\t"\
		"psubb %%mm1, %%mm0				\n\t"\
		"psllq $8, %%mm1				\n\t"\
		"paddb %%mm1, %%mm0				\n\t"\
		"movd %%mm0, " #dst"				\n\t"\
		"psrlq $32, %%mm0				\n\t"\
		"movd %%mm0, 4" #dst"				\n\t"
#else
#define HDF(src, dst)\
		"movq " #src "(%%eax), %%mm0			\n\t"\
		"movq %%mm0, %%mm1				\n\t"\
		"movq %%mm0, %%mm2				\n\t"\
		"psrlq $8, %%mm1				\n\t"\
		"psubusb %%mm1, %%mm2				\n\t"\
		"psubusb %%mm0, %%mm1				\n\t"\
		"por %%mm2, %%mm1				\n\t" /* p´x = |px - p(x+1)| */\
		"pcmpeqb %%mm7, %%mm2				\n\t" /* p´x = sgn[px - p(x+1)] */\
		"movq %%mm1, %%mm3				\n\t"\
		"psllq $32, %%mm3				\n\t"\
		"movq %%mm3, %%mm4				\n\t"\
		"psubusb %%mm1, %%mm4				\n\t"\
		"psubb %%mm4, %%mm3				\n\t"\
		"psrlq $16, %%mm3				\n\t" /* p´3 = min(|p2-p1|, |p6-p5|)*/\
		"psubusb %%mm3, %%mm1			\n\t" /* |p3-p4|-min(|p1-p2|,|p5,ü6|) */\
		"paddb %%mm5, %%mm1				\n\t"\
		"psubusb %%mm5, %%mm1				\n\t"\
		"psrlw $2, %%mm1				\n\t"\
		"pxor %%mm2, %%mm1				\n\t"\
		"psubb %%mm2, %%mm1				\n\t"\
		"pand %%mm6, %%mm1				\n\t"\
		"psubb %%mm1, %%mm0				\n\t"\
		"psllq $8, %%mm1				\n\t"\
		"paddb %%mm1, %%mm0				\n\t"\
		"movd %%mm0, " #dst "				\n\t"\
		"psrlq $32, %%mm0				\n\t"\
		"movd %%mm0, 4" #dst "				\n\t"
#endif
		HDF(0,(%0))
		HDF(8,(%%ecx))
		HDF(16,(%%ecx, %1))
		HDF(24,(%%ecx, %1, 2))
		HDF(32,(%0, %1, 4))
		HDF(40,(%%ebx))
		HDF(48,(%%ebx, %1))
		HDF(56,(%%ebx, %1, 2))
		:
		: "r" (dst), "r" (stride), "r" (QP)
		: "%eax", "%ebx", "%ecx"
	);
#else
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
#endif
}

/**
 * Do a horizontal low pass filter on the 10x8 block (dst points to middle 8x8 Block)
 * using the 9-Tap Filter (1,1,2,2,4,2,2,1,1)/16 (C version)
 * using the 7-Tap Filter   (2,2,2,4,2,2,2)/16 (MMX2/3DNOW version)
 */
static inline void doHorizLowPass(uint8_t dst[], int stride, int QP)
{

#if 0
	asm volatile(
		"leal (%0, %1), %%ecx				\n\t"
		"leal (%%ecx, %1, 4), %%ebx			\n\t"
//	0	1	2	3	4	5	6	7	8	9
//	%0	ecx	ecx+%1	ecx+2%1	%0+4%1	ebx	ebx+%1	ebx+2%1	%0+8%1	ebx+4%1
		"pxor %%mm7, %%mm7					\n\t"
		"leal tempBlock, %%eax					\n\t"
/*
#define HLP1	"movq (%0), %%mm0					\n\t"\
		"movq %%mm0, %%mm1					\n\t"\
		"psllq $8, %%mm0					\n\t"\
		PAVGB(%%mm1, %%mm0)\
		"psrlw $8, %%mm0					\n\t"\
		"pxor %%mm1, %%mm1					\n\t"\
		"packuswb %%mm1, %%mm0					\n\t"\
		"movq %%mm0, %%mm1					\n\t"\
		"movq %%mm0, %%mm2					\n\t"\
		"psllq $32, %%mm0					\n\t"\
		"paddb %%mm0, %%mm1					\n\t"\
		"psllq $16, %%mm2					\n\t"\
		PAVGB(%%mm2, %%mm0)\
		"movq %%mm0, %%mm3					\n\t"\
		"pand bm11001100, %%mm0					\n\t"\
		"paddusb %%mm0, %%mm3					\n\t"\
		"psrlq $8, %%mm3					\n\t"\
		PAVGB(%%mm1, %%mm4)\
		PAVGB(%%mm3, %%mm2)\
		"psrlq $16, %%mm2					\n\t"\
		"punpcklbw %%mm2, %%mm2					\n\t"\
		"movq %%mm2, (%0)					\n\t"\

#define HLP2	"movq (%0), %%mm0					\n\t"\
		"movq %%mm0, %%mm1					\n\t"\
		"psllq $8, %%mm0					\n\t"\
		PAVGB(%%mm1, %%mm0)\
		"psrlw $8, %%mm0					\n\t"\
		"pxor %%mm1, %%mm1					\n\t"\
		"packuswb %%mm1, %%mm0					\n\t"\
		"movq %%mm0, %%mm2					\n\t"\
		"psllq $32, %%mm0					\n\t"\
		"psllq $16, %%mm2					\n\t"\
		PAVGB(%%mm2, %%mm0)\
		"movq %%mm0, %%mm3					\n\t"\
		"pand bm11001100, %%mm0					\n\t"\
		"paddusb %%mm0, %%mm3					\n\t"\
		"psrlq $8, %%mm3					\n\t"\
		PAVGB(%%mm3, %%mm2)\
		"psrlq $16, %%mm2					\n\t"\
		"punpcklbw %%mm2, %%mm2					\n\t"\
		"movq %%mm2, (%0)					\n\t"\
*/
// approximately a 7-Tap Filter with Vector (1,2,3,4,3,2,1)/16
/*
Implemented	Exact 7-Tap
 9421		A321
 36421		64321
 334321		=
 1234321	=
  1234321	=
   123433	=
    12463	  12346
     1249	   123A

*/

#ifdef HAVE_MMX2
#define HLP3(i)	"movq " #i "(%%eax), %%mm0				\n\t"\
		"movq %%mm0, %%mm1					\n\t"\
		"movq %%mm0, %%mm2					\n\t"\
		"movq %%mm0, %%mm3					\n\t"\
		"movq %%mm0, %%mm4					\n\t"\
		"psllq $8, %%mm1					\n\t"\
		"psrlq $8, %%mm2					\n\t"\
		"pand bm00000001, %%mm3					\n\t"\
		"pand bm10000000, %%mm4					\n\t"\
		"por %%mm3, %%mm1					\n\t"\
		"por %%mm4, %%mm2					\n\t"\
		PAVGB(%%mm2, %%mm1)\
		PAVGB(%%mm1, %%mm0)\
\
		"pshufw $0xF9, %%mm0, %%mm3				\n\t"\
		"pshufw $0x90, %%mm0, %%mm4				\n\t"\
		PAVGB(%%mm3, %%mm4)\
		PAVGB(%%mm4, %%mm0)\
		"movd %%mm0, (%0)					\n\t"\
		"psrlq $32, %%mm0					\n\t"\
		"movd %%mm0, 4(%0)					\n\t"
#else
#define HLP3(i)	"movq " #i "(%%eax), %%mm0				\n\t"\
		"movq %%mm0, %%mm1					\n\t"\
		"movq %%mm0, %%mm2					\n\t"\
		"movd -4(%0), %%mm3					\n\t" /*0001000*/\
		"movd 8(%0), %%mm4					\n\t" /*0001000*/\
		"psllq $8, %%mm1					\n\t"\
		"psrlq $8, %%mm2					\n\t"\
		"psrlq $24, %%mm3					\n\t"\
		"psllq $56, %%mm4					\n\t"\
		"por %%mm3, %%mm1					\n\t"\
		"por %%mm4, %%mm2					\n\t"\
		PAVGB(%%mm2, %%mm1)\
		PAVGB(%%mm1, %%mm0)\
\
		"movq %%mm0, %%mm3					\n\t"\
		"movq %%mm0, %%mm4					\n\t"\
		"movq %%mm0, %%mm5					\n\t"\
		"psrlq $16, %%mm3					\n\t"\
		"psllq $16, %%mm4					\n\t"\
		"pand bm11000000, %%mm5					\n\t"\
		"por %%mm5, %%mm3					\n\t"\
		"movq %%mm0, %%mm5					\n\t"\
		"pand bm00000011, %%mm5					\n\t"\
		"por %%mm5, %%mm4					\n\t"\
		PAVGB(%%mm3, %%mm4)\
		PAVGB(%%mm4, %%mm0)\
		"movd %%mm0, (%0)					\n\t"\
		"psrlq $32, %%mm0					\n\t"\
		"movd %%mm0, 4(%0)					\n\t"
#endif

/* uses the 7-Tap Filter: 1112111 */
#define NEW_HLP(src, dst)\
		"movq " #src "(%%eax), %%mm1				\n\t"\
		"movq " #src "(%%eax), %%mm2				\n\t"\
		"psllq $8, %%mm1					\n\t"\
		"psrlq $8, %%mm2					\n\t"\
		"movd -4" #dst ", %%mm3					\n\t" /*0001000*/\
		"movd 8" #dst ", %%mm4					\n\t" /*0001000*/\
		"psrlq $24, %%mm3					\n\t"\
		"psllq $56, %%mm4					\n\t"\
		"por %%mm3, %%mm1					\n\t"\
		"por %%mm4, %%mm2					\n\t"\
		"movq %%mm1, %%mm5					\n\t"\
		PAVGB(%%mm2, %%mm1)\
		"movq " #src "(%%eax), %%mm0				\n\t"\
		PAVGB(%%mm1, %%mm0)\
		"psllq $8, %%mm5					\n\t"\
		"psrlq $8, %%mm2					\n\t"\
		"por %%mm3, %%mm5					\n\t"\
		"por %%mm4, %%mm2					\n\t"\
		"movq %%mm5, %%mm1					\n\t"\
		PAVGB(%%mm2, %%mm5)\
		"psllq $8, %%mm1					\n\t"\
		"psrlq $8, %%mm2					\n\t"\
		"por %%mm3, %%mm1					\n\t"\
		"por %%mm4, %%mm2					\n\t"\
		PAVGB(%%mm2, %%mm1)\
		PAVGB(%%mm1, %%mm5)\
		PAVGB(%%mm5, %%mm0)\
		"movd %%mm0, " #dst "					\n\t"\
		"psrlq $32, %%mm0					\n\t"\
		"movd %%mm0, 4" #dst "					\n\t"

/* uses the 9-Tap Filter: 112242211 */
#define NEW_HLP2(i)\
		"movq " #i "(%%eax), %%mm0				\n\t" /*0001000*/\
		"movq %%mm0, %%mm1					\n\t" /*0001000*/\
		"movq %%mm0, %%mm2					\n\t" /*0001000*/\
		"movd -4(%0), %%mm3					\n\t" /*0001000*/\
		"movd 8(%0), %%mm4					\n\t" /*0001000*/\
		"psllq $8, %%mm1					\n\t"\
		"psrlq $8, %%mm2					\n\t"\
		"psrlq $24, %%mm3					\n\t"\
		"psllq $56, %%mm4					\n\t"\
		"por %%mm3, %%mm1					\n\t" /*0010000*/\
		"por %%mm4, %%mm2					\n\t" /*0000100*/\
		"movq %%mm1, %%mm5					\n\t" /*0010000*/\
		PAVGB(%%mm2, %%mm1)					      /*0010100*/\
		PAVGB(%%mm1, %%mm0)					      /*0012100*/\
		"psllq $8, %%mm5					\n\t"\
		"psrlq $8, %%mm2					\n\t"\
		"por %%mm3, %%mm5					\n\t" /*0100000*/\
		"por %%mm4, %%mm2					\n\t" /*0000010*/\
		"movq %%mm5, %%mm1					\n\t" /*0100000*/\
		PAVGB(%%mm2, %%mm5)					      /*0100010*/\
		"psllq $8, %%mm1					\n\t"\
		"psrlq $8, %%mm2					\n\t"\
		"por %%mm3, %%mm1					\n\t" /*1000000*/\
		"por %%mm4, %%mm2					\n\t" /*0000001*/\
		"movq %%mm1, %%mm6					\n\t" /*1000000*/\
		PAVGB(%%mm2, %%mm1)					      /*1000001*/\
		"psllq $8, %%mm6					\n\t"\
		"psrlq $8, %%mm2					\n\t"\
		"por %%mm3, %%mm6					\n\t"/*100000000*/\
		"por %%mm4, %%mm2					\n\t"/*000000001*/\
		PAVGB(%%mm2, %%mm6)					     /*100000001*/\
		PAVGB(%%mm6, %%mm1)					     /*110000011*/\
		PAVGB(%%mm1, %%mm5)					     /*112000211*/\
		PAVGB(%%mm5, %%mm0)					     /*112242211*/\
		"movd %%mm0, (%0)					\n\t"\
		"psrlq $32, %%mm0					\n\t"\
		"movd %%mm0, 4(%0)					\n\t"

#define HLP(src, dst) NEW_HLP(src, dst)

		HLP(0, (%0))
		HLP(8, (%%ecx))
		HLP(16, (%%ecx, %1))
		HLP(24, (%%ecx, %1, 2))
		HLP(32, (%0, %1, 4))
		HLP(40, (%%ebx))
		HLP(48, (%%ebx, %1))
		HLP(56, (%%ebx, %1, 2))

		:
		: "r" (dst), "r" (stride)
		: "%eax", "%ebx", "%ecx"
	);

#else
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
#endif
}

static inline void dering(uint8_t src[], int stride, int QP)
{
#if defined (HAVE_MMX2) || defined (HAVE_3DNOW)
	asm volatile(
		"movq pQPb, %%mm0				\n\t"
		"paddusb %%mm0, %%mm0				\n\t"
		"movq %%mm0, pQPb2				\n\t"

		"leal (%0, %1), %%eax				\n\t"
		"leal (%%eax, %1, 4), %%ebx			\n\t"
//	0	1	2	3	4	5	6	7	8	9
//	%0	eax	eax+%1	eax+2%1	%0+4%1	ebx	ebx+%1	ebx+2%1	%0+8%1	ebx+4%1

		"pcmpeqb %%mm6, %%mm6				\n\t"
		"pxor %%mm7, %%mm7				\n\t"
#ifdef HAVE_MMX2
#define FIND_MIN_MAX(addr)\
		"movq " #addr ", %%mm0				\n\t"\
		"pminub %%mm0, %%mm6				\n\t"\
		"pmaxub %%mm0, %%mm7				\n\t"
#else
#define FIND_MIN_MAX(addr)\
		"movq " #addr ", %%mm0				\n\t"\
		"movq %%mm6, %%mm1				\n\t"\
		"psubusb %%mm0, %%mm7				\n\t"\
		"paddb %%mm0, %%mm7				\n\t"\
		"psubusb %%mm0, %%mm1				\n\t"\
		"psubb %%mm1, %%mm6				\n\t"
#endif

FIND_MIN_MAX((%%eax))
FIND_MIN_MAX((%%eax, %1))
FIND_MIN_MAX((%%eax, %1, 2))
FIND_MIN_MAX((%0, %1, 4))
FIND_MIN_MAX((%%ebx))
FIND_MIN_MAX((%%ebx, %1))
FIND_MIN_MAX((%%ebx, %1, 2))
FIND_MIN_MAX((%0, %1, 8))

		"movq %%mm6, %%mm4				\n\t"
		"psrlq $8, %%mm6				\n\t"
#ifdef HAVE_MMX2
		"pminub %%mm4, %%mm6				\n\t" // min of pixels
		"pshufw $0xF9, %%mm6, %%mm4			\n\t"
		"pminub %%mm4, %%mm6				\n\t" // min of pixels
		"pshufw $0xFE, %%mm6, %%mm4			\n\t"
		"pminub %%mm4, %%mm6				\n\t"
#else
		"movq %%mm6, %%mm1				\n\t"
		"psubusb %%mm4, %%mm1				\n\t"
		"psubb %%mm1, %%mm6				\n\t"
		"movq %%mm6, %%mm4				\n\t"
		"psrlq $16, %%mm6				\n\t"
		"movq %%mm6, %%mm1				\n\t"
		"psubusb %%mm4, %%mm1				\n\t"
		"psubb %%mm1, %%mm6				\n\t"
		"movq %%mm6, %%mm4				\n\t"
		"psrlq $32, %%mm6				\n\t"
		"movq %%mm6, %%mm1				\n\t"
		"psubusb %%mm4, %%mm1				\n\t"
		"psubb %%mm1, %%mm6				\n\t"
#endif


		"movq %%mm7, %%mm4				\n\t"
		"psrlq $8, %%mm7				\n\t"
#ifdef HAVE_MMX2
		"pmaxub %%mm4, %%mm7				\n\t" // max of pixels
		"pshufw $0xF9, %%mm7, %%mm4			\n\t"
		"pmaxub %%mm4, %%mm7				\n\t"
		"pshufw $0xFE, %%mm7, %%mm4			\n\t"
		"pmaxub %%mm4, %%mm7				\n\t"
#else
		"psubusb %%mm4, %%mm7				\n\t"
		"paddb %%mm4, %%mm7				\n\t"
		"movq %%mm7, %%mm4				\n\t"
		"psrlq $16, %%mm7				\n\t"
		"psubusb %%mm4, %%mm7				\n\t"
		"paddb %%mm4, %%mm7				\n\t"
		"movq %%mm7, %%mm4				\n\t"
		"psrlq $32, %%mm7				\n\t"
		"psubusb %%mm4, %%mm7				\n\t"
		"paddb %%mm4, %%mm7				\n\t"
#endif
		PAVGB(%%mm6, %%mm7)				      // a=(max + min)/2
		"punpcklbw %%mm7, %%mm7				\n\t"
		"punpcklbw %%mm7, %%mm7				\n\t"
		"punpcklbw %%mm7, %%mm7				\n\t"
		"movq %%mm7, temp0				\n\t"

		"movq (%0), %%mm0				\n\t" // L10
		"movq %%mm0, %%mm1				\n\t" // L10
		"movq %%mm0, %%mm2				\n\t" // L10
		"psllq $8, %%mm1				\n\t"
		"psrlq $8, %%mm2				\n\t"
		"movd -4(%0), %%mm3				\n\t"
		"movd 8(%0), %%mm4				\n\t"
		"psrlq $24, %%mm3				\n\t"
		"psllq $56, %%mm4				\n\t"
		"por %%mm3, %%mm1				\n\t" // L00
		"por %%mm4, %%mm2				\n\t" // L20
		"movq %%mm1, %%mm3				\n\t" // L00
		PAVGB(%%mm2, %%mm1)				      // (L20 + L00)/2
		PAVGB(%%mm0, %%mm1)				      // (L20 + L00 + 2L10)/4
		"psubusb %%mm7, %%mm0				\n\t"
		"psubusb %%mm7, %%mm2				\n\t"
		"psubusb %%mm7, %%mm3				\n\t"
		"pcmpeqb b00, %%mm0				\n\t" // L10 > a ? 0 : -1
		"pcmpeqb b00, %%mm2				\n\t" // L20 > a ? 0 : -1
		"pcmpeqb b00, %%mm3				\n\t" // L00 > a ? 0 : -1
		"paddb %%mm2, %%mm0				\n\t"
		"paddb %%mm3, %%mm0				\n\t"

		"movq (%%eax), %%mm2				\n\t" // L11
		"movq %%mm2, %%mm3				\n\t" // L11
		"movq %%mm2, %%mm4				\n\t" // L11
		"psllq $8, %%mm3				\n\t"
		"psrlq $8, %%mm4				\n\t"
		"movd -4(%%eax), %%mm5				\n\t"
		"movd 8(%%eax), %%mm6				\n\t"
		"psrlq $24, %%mm5				\n\t"
		"psllq $56, %%mm6				\n\t"
		"por %%mm5, %%mm3				\n\t" // L01
		"por %%mm6, %%mm4				\n\t" // L21
		"movq %%mm3, %%mm5				\n\t" // L01
		PAVGB(%%mm4, %%mm3)				      // (L21 + L01)/2
		PAVGB(%%mm2, %%mm3)				      // (L21 + L01 + 2L11)/4
		"psubusb %%mm7, %%mm2				\n\t"
		"psubusb %%mm7, %%mm4				\n\t"
		"psubusb %%mm7, %%mm5				\n\t"
		"pcmpeqb b00, %%mm2				\n\t" // L11 > a ? 0 : -1
		"pcmpeqb b00, %%mm4				\n\t" // L21 > a ? 0 : -1
		"pcmpeqb b00, %%mm5				\n\t" // L01 > a ? 0 : -1
		"paddb %%mm4, %%mm2				\n\t"
		"paddb %%mm5, %%mm2				\n\t"
// 0, 2, 3, 1
#define DERING_CORE(dst,src,ppsx,psx,sx,pplx,plx,lx,t0,t1) \
		"movq " #src ", " #sx "				\n\t" /* src[0] */\
		"movq " #sx ", " #lx "				\n\t" /* src[0] */\
		"movq " #sx ", " #t0 "				\n\t" /* src[0] */\
		"psllq $8, " #lx "				\n\t"\
		"psrlq $8, " #t0 "				\n\t"\
		"movd -4" #src ", " #t1 "			\n\t"\
		"psrlq $24, " #t1 "				\n\t"\
		"por " #t1 ", " #lx "				\n\t" /* src[-1] */\
		"movd 8" #src ", " #t1 "			\n\t"\
		"psllq $56, " #t1 "				\n\t"\
		"por " #t1 ", " #t0 "				\n\t" /* src[+1] */\
		"movq " #lx ", " #t1 "				\n\t" /* src[-1] */\
		PAVGB(t0, lx)				              /* (src[-1] + src[+1])/2 */\
		PAVGB(sx, lx)				      /* (src[-1] + 2src[0] + src[+1])/4 */\
		PAVGB(lx, pplx)					     \
		"movq " #lx ", temp1				\n\t"\
		"movq temp0, " #lx "				\n\t"\
		"psubusb " #lx ", " #t1 "			\n\t"\
		"psubusb " #lx ", " #t0 "			\n\t"\
		"psubusb " #lx ", " #sx "			\n\t"\
		"movq b00, " #lx "				\n\t"\
		"pcmpeqb " #lx ", " #t1 "			\n\t" /* src[-1] > a ? 0 : -1*/\
		"pcmpeqb " #lx ", " #t0 "			\n\t" /* src[+1] > a ? 0 : -1*/\
		"pcmpeqb " #lx ", " #sx "			\n\t" /* src[0]  > a ? 0 : -1*/\
		"paddb " #t1 ", " #t0 "				\n\t"\
		"paddb " #t0 ", " #sx "				\n\t"\
\
		PAVGB(plx, pplx)				      /* filtered */\
		"movq " #dst ", " #t0 "				\n\t" /* dst */\
		"movq " #t0 ", " #t1 "				\n\t" /* dst */\
		"psubusb pQPb2, " #t0 "				\n\t"\
		"paddusb pQPb2, " #t1 "				\n\t"\
		PMAXUB(t0, pplx)\
		PMINUB(t1, pplx, t0)\
		"paddb " #sx ", " #ppsx "			\n\t"\
		"paddb " #psx ", " #ppsx "			\n\t"\
	"#paddb b02, " #ppsx "				\n\t"\
		"pand b08, " #ppsx "				\n\t"\
		"pcmpeqb " #lx ", " #ppsx "			\n\t"\
		"pand " #ppsx ", " #pplx "			\n\t"\
		"pandn " #dst ", " #ppsx "			\n\t"\
		"por " #pplx ", " #ppsx "			\n\t"\
		"movq " #ppsx ", " #dst "			\n\t"\
		"movq temp1, " #lx "				\n\t"

/*
0000000
1111111

1111110
1111101
1111100
1111011
1111010
1111001

1111000
1110111

*/
//DERING_CORE(dst,src                  ,ppsx ,psx  ,sx   ,pplx ,plx  ,lx   ,t0   ,t1)
DERING_CORE((%%eax),(%%eax, %1)        ,%%mm0,%%mm2,%%mm4,%%mm1,%%mm3,%%mm5,%%mm6,%%mm7)
DERING_CORE((%%eax, %1),(%%eax, %1, 2) ,%%mm2,%%mm4,%%mm0,%%mm3,%%mm5,%%mm1,%%mm6,%%mm7)
DERING_CORE((%%eax, %1, 2),(%0, %1, 4) ,%%mm4,%%mm0,%%mm2,%%mm5,%%mm1,%%mm3,%%mm6,%%mm7)
DERING_CORE((%0, %1, 4),(%%ebx)        ,%%mm0,%%mm2,%%mm4,%%mm1,%%mm3,%%mm5,%%mm6,%%mm7)
DERING_CORE((%%ebx),(%%ebx, %1)        ,%%mm2,%%mm4,%%mm0,%%mm3,%%mm5,%%mm1,%%mm6,%%mm7)
DERING_CORE((%%ebx, %1), (%%ebx, %1, 2),%%mm4,%%mm0,%%mm2,%%mm5,%%mm1,%%mm3,%%mm6,%%mm7)
DERING_CORE((%%ebx, %1, 2),(%0, %1, 8) ,%%mm0,%%mm2,%%mm4,%%mm1,%%mm3,%%mm5,%%mm6,%%mm7)
DERING_CORE((%0, %1, 8),(%%ebx, %1, 4) ,%%mm2,%%mm4,%%mm0,%%mm3,%%mm5,%%mm1,%%mm6,%%mm7)


		: : "r" (src), "r" (stride), "r" (QP)
		: "%eax", "%ebx"
	);
#else
	int y;
	int min=255;
	int max=0;
	int avg;
	uint8_t *p;
	int s[10];

	for(y=1; y<9; y++)
	{
		int x;
		p= src + stride*y;
		for(x=1; x<9; x++)
		{
			p++;
			if(*p > max) max= *p;
			if(*p < min) min= *p;
		}
	}
	avg= (min + max + 1)/2;

	for(y=0; y<10; y++)
	{
		int x;
		int t = 0;
		p= src + stride*y;
		for(x=0; x<10; x++)
		{
			if(*p > avg) t |= (1<<x);
			p++;
		}
		t |= (~t)<<16;
		t &= (t<<1) & (t>>1);
		s[y] = t;
	}

	for(y=1; y<9; y++)
	{
		int x;
		int t = s[y-1] & s[y] & s[y+1];
		t|= t>>16;

		p= src + stride*y;
		for(x=1; x<9; x++)
		{
			p++;
			if(t & (1<<x))
			{
				int f= (*(p-stride-1)) + 2*(*(p-stride)) + (*(p-stride+1))
				      +2*(*(p     -1)) + 4*(*p         ) + 2*(*(p     +1))
				      +(*(p+stride-1)) + 2*(*(p+stride)) + (*(p+stride+1));
				f= (f + 8)>>4;

				if     (*p + 2*QP < f) *p= *p + 2*QP;
				else if(*p - 2*QP > f) *p= *p - 2*QP;
				else *p=f;
			}
		}
	}

#endif
}

/**
 * Deinterlaces the given block
 * will be called for every 8x8 block and can read & write from line 4-15
 * lines 0-3 have been passed through the deblock / dering filters allready, but can be read too
 * lines 4-12 will be read into the deblocking filter and should be deinterlaced
 */
static inline void deInterlaceInterpolateLinear(uint8_t src[], int stride)
{
#if defined (HAVE_MMX2) || defined (HAVE_3DNOW)
	src+= 4*stride;
	asm volatile(
		"leal (%0, %1), %%eax				\n\t"
		"leal (%%eax, %1, 4), %%ebx			\n\t"
//	0	1	2	3	4	5	6	7	8	9
//	%0	eax	eax+%1	eax+2%1	%0+4%1	ebx	ebx+%1	ebx+2%1	%0+8%1	ebx+4%1

		"movq (%0), %%mm0				\n\t"
		"movq (%%eax, %1), %%mm1			\n\t"
		PAVGB(%%mm1, %%mm0)
		"movq %%mm0, (%%eax)				\n\t"
		"movq (%0, %1, 4), %%mm0			\n\t"
		PAVGB(%%mm0, %%mm1)
		"movq %%mm1, (%%eax, %1, 2)			\n\t"
		"movq (%%ebx, %1), %%mm1			\n\t"
		PAVGB(%%mm1, %%mm0)
		"movq %%mm0, (%%ebx)				\n\t"
		"movq (%0, %1, 8), %%mm0			\n\t"
		PAVGB(%%mm0, %%mm1)
		"movq %%mm1, (%%ebx, %1, 2)			\n\t"

		: : "r" (src), "r" (stride)
		: "%eax", "%ebx"
	);
#else
	int x;
	src+= 4*stride;
	for(x=0; x<8; x++)
	{
		src[stride]   = (src[0]        + src[stride*2])>>1;
		src[stride*3] = (src[stride*2] + src[stride*4])>>1;
		src[stride*5] = (src[stride*4] + src[stride*6])>>1;
		src[stride*7] = (src[stride*6] + src[stride*8])>>1;
		src++;
	}
#endif
}

/**
 * Deinterlaces the given block
 * will be called for every 8x8 block and can read & write from line 4-15
 * lines 0-3 have been passed through the deblock / dering filters allready, but can be read too
 * lines 4-12 will be read into the deblocking filter and should be deinterlaced
 * this filter will read lines 3-15 and write 7-13
 * no cliping in C version
 */
static inline void deInterlaceInterpolateCubic(uint8_t src[], int stride)
{
#if defined (HAVE_MMX2) || defined (HAVE_3DNOW)
	src+= stride*3;
	asm volatile(
		"leal (%0, %1), %%eax				\n\t"
		"leal (%%eax, %1, 4), %%ebx			\n\t"
		"leal (%%ebx, %1, 4), %%ecx			\n\t"
		"addl %1, %%ecx					\n\t"
		"pxor %%mm7, %%mm7				\n\t"
//	0	1	2	3	4	5	6	7	8	9	10
//	%0	eax	eax+%1	eax+2%1	%0+4%1	ebx	ebx+%1	ebx+2%1	%0+8%1	ebx+4%1 ecx

#define DEINT_CUBIC(a,b,c,d,e)\
		"movq " #a ", %%mm0				\n\t"\
		"movq " #b ", %%mm1				\n\t"\
		"movq " #d ", %%mm2				\n\t"\
		"movq " #e ", %%mm3				\n\t"\
		PAVGB(%%mm2, %%mm1)					/* (b+d) /2 */\
		PAVGB(%%mm3, %%mm0)					/* a(a+e) /2 */\
		"movq %%mm0, %%mm2				\n\t"\
		"punpcklbw %%mm7, %%mm0				\n\t"\
		"punpckhbw %%mm7, %%mm2				\n\t"\
		"movq %%mm1, %%mm3				\n\t"\
		"punpcklbw %%mm7, %%mm1				\n\t"\
		"punpckhbw %%mm7, %%mm3				\n\t"\
		"psubw %%mm1, %%mm0				\n\t"	/* L(a+e - (b+d))/2 */\
		"psubw %%mm3, %%mm2				\n\t"	/* H(a+e - (b+d))/2 */\
		"psraw $3, %%mm0				\n\t"	/* L(a+e - (b+d))/16 */\
		"psraw $3, %%mm2				\n\t"	/* H(a+e - (b+d))/16 */\
		"psubw %%mm0, %%mm1				\n\t"	/* L(9b + 9d - a - e)/16 */\
		"psubw %%mm2, %%mm3				\n\t"	/* H(9b + 9d - a - e)/16 */\
		"packuswb %%mm3, %%mm1				\n\t"\
		"movq %%mm1, " #c "				\n\t"

DEINT_CUBIC((%0), (%%eax, %1), (%%eax, %1, 2), (%0, %1, 4), (%%ebx, %1))
DEINT_CUBIC((%%eax, %1), (%0, %1, 4), (%%ebx), (%%ebx, %1), (%0, %1, 8))
DEINT_CUBIC((%0, %1, 4), (%%ebx, %1), (%%ebx, %1, 2), (%0, %1, 8), (%%ecx))
DEINT_CUBIC((%%ebx, %1), (%0, %1, 8), (%%ebx, %1, 4), (%%ecx), (%%ecx, %1, 2))

		: : "r" (src), "r" (stride)
		: "%eax", "%ebx", "ecx"
	);
#else
	int x;
	src+= stride*3;
	for(x=0; x<8; x++)
	{
		src[stride*3] = (-src[0]        + 9*src[stride*2] + 9*src[stride*4] - src[stride*6])>>4;
		src[stride*5] = (-src[stride*2] + 9*src[stride*4] + 9*src[stride*6] - src[stride*8])>>4;
		src[stride*7] = (-src[stride*4] + 9*src[stride*6] + 9*src[stride*8] - src[stride*10])>>4;
		src[stride*9] = (-src[stride*6] + 9*src[stride*8] + 9*src[stride*10] - src[stride*12])>>4;
		src++;
	}
#endif
}

/**
 * Deinterlaces the given block
 * will be called for every 8x8 block and can read & write from line 4-15
 * lines 0-3 have been passed through the deblock / dering filters allready, but can be read too
 * lines 4-12 will be read into the deblocking filter and should be deinterlaced
 * will shift the image up by 1 line (FIXME if this is a problem)
 * this filter will read lines 4-13 and write 4-11
 */
static inline void deInterlaceBlendLinear(uint8_t src[], int stride)
{
#if defined (HAVE_MMX2) || defined (HAVE_3DNOW)
	src+= 4*stride;
	asm volatile(
		"leal (%0, %1), %%eax				\n\t"
		"leal (%%eax, %1, 4), %%ebx			\n\t"
//	0	1	2	3	4	5	6	7	8	9
//	%0	eax	eax+%1	eax+2%1	%0+4%1	ebx	ebx+%1	ebx+2%1	%0+8%1	ebx+4%1

		"movq (%0), %%mm0				\n\t" // L0
		"movq (%%eax, %1), %%mm1			\n\t" // L2
		PAVGB(%%mm1, %%mm0)				      // L0+L2
		"movq (%%eax), %%mm2				\n\t" // L1
		PAVGB(%%mm2, %%mm0)
		"movq %%mm0, (%0)				\n\t"
		"movq (%%eax, %1, 2), %%mm0			\n\t" // L3
		PAVGB(%%mm0, %%mm2)				      // L1+L3
		PAVGB(%%mm1, %%mm2)				      // 2L2 + L1 + L3
		"movq %%mm2, (%%eax)				\n\t"
		"movq (%0, %1, 4), %%mm2			\n\t" // L4
		PAVGB(%%mm2, %%mm1)				      // L2+L4
		PAVGB(%%mm0, %%mm1)				      // 2L3 + L2 + L4
		"movq %%mm1, (%%eax, %1)			\n\t"
		"movq (%%ebx), %%mm1				\n\t" // L5
		PAVGB(%%mm1, %%mm0)				      // L3+L5
		PAVGB(%%mm2, %%mm0)				      // 2L4 + L3 + L5
		"movq %%mm0, (%%eax, %1, 2)			\n\t"
		"movq (%%ebx, %1), %%mm0			\n\t" // L6
		PAVGB(%%mm0, %%mm2)				      // L4+L6
		PAVGB(%%mm1, %%mm2)				      // 2L5 + L4 + L6
		"movq %%mm2, (%0, %1, 4)			\n\t"
		"movq (%%ebx, %1, 2), %%mm2			\n\t" // L7
		PAVGB(%%mm2, %%mm1)				      // L5+L7
		PAVGB(%%mm0, %%mm1)				      // 2L6 + L5 + L7
		"movq %%mm1, (%%ebx)				\n\t"
		"movq (%0, %1, 8), %%mm1			\n\t" // L8
		PAVGB(%%mm1, %%mm0)				      // L6+L8
		PAVGB(%%mm2, %%mm0)				      // 2L7 + L6 + L8
		"movq %%mm0, (%%ebx, %1)			\n\t"
		"movq (%%ebx, %1, 4), %%mm0			\n\t" // L9
		PAVGB(%%mm0, %%mm2)				      // L7+L9
		PAVGB(%%mm1, %%mm2)				      // 2L8 + L7 + L9
		"movq %%mm2, (%%ebx, %1, 2)			\n\t"


		: : "r" (src), "r" (stride)
		: "%eax", "%ebx"
	);
#else
	int x;
	src+= 4*stride;
	for(x=0; x<8; x++)
	{
		src[0       ] = (src[0       ] + 2*src[stride  ] + src[stride*2])>>2;
		src[stride  ] = (src[stride  ] + 2*src[stride*2] + src[stride*3])>>2;
		src[stride*2] = (src[stride*2] + 2*src[stride*3] + src[stride*4])>>2;
		src[stride*3] = (src[stride*3] + 2*src[stride*4] + src[stride*5])>>2;
		src[stride*4] = (src[stride*4] + 2*src[stride*5] + src[stride*6])>>2;
		src[stride*5] = (src[stride*5] + 2*src[stride*6] + src[stride*7])>>2;
		src[stride*6] = (src[stride*6] + 2*src[stride*7] + src[stride*8])>>2;
		src[stride*7] = (src[stride*7] + 2*src[stride*8] + src[stride*9])>>2;
		src++;
	}
#endif
}

/**
 * Deinterlaces the given block
 * will be called for every 8x8 block and can read & write from line 4-15,
 * lines 0-3 have been passed through the deblock / dering filters allready, but can be read too
 * lines 4-12 will be read into the deblocking filter and should be deinterlaced
 */
static inline void deInterlaceMedian(uint8_t src[], int stride)
{
#ifdef HAVE_MMX
	src+= 4*stride;
#ifdef HAVE_MMX2
	asm volatile(
		"leal (%0, %1), %%eax				\n\t"
		"leal (%%eax, %1, 4), %%ebx			\n\t"
//	0	1	2	3	4	5	6	7	8	9
//	%0	eax	eax+%1	eax+2%1	%0+4%1	ebx	ebx+%1	ebx+2%1	%0+8%1	ebx+4%1

		"movq (%0), %%mm0				\n\t" //
		"movq (%%eax, %1), %%mm2			\n\t" //
		"movq (%%eax), %%mm1				\n\t" //
		"movq %%mm0, %%mm3				\n\t"
		"pmaxub %%mm1, %%mm0				\n\t" //
		"pminub %%mm3, %%mm1				\n\t" //
		"pmaxub %%mm2, %%mm1				\n\t" //
		"pminub %%mm1, %%mm0				\n\t"
		"movq %%mm0, (%%eax)				\n\t"

		"movq (%0, %1, 4), %%mm0			\n\t" //
		"movq (%%eax, %1, 2), %%mm1			\n\t" //
		"movq %%mm2, %%mm3				\n\t"
		"pmaxub %%mm1, %%mm2				\n\t" //
		"pminub %%mm3, %%mm1				\n\t" //
		"pmaxub %%mm0, %%mm1				\n\t" //
		"pminub %%mm1, %%mm2				\n\t"
		"movq %%mm2, (%%eax, %1, 2)			\n\t"

		"movq (%%ebx), %%mm2				\n\t" //
		"movq (%%ebx, %1), %%mm1			\n\t" //
		"movq %%mm2, %%mm3				\n\t"
		"pmaxub %%mm0, %%mm2				\n\t" //
		"pminub %%mm3, %%mm0				\n\t" //
		"pmaxub %%mm1, %%mm0				\n\t" //
		"pminub %%mm0, %%mm2				\n\t"
		"movq %%mm2, (%%ebx)				\n\t"

		"movq (%%ebx, %1, 2), %%mm2			\n\t" //
		"movq (%0, %1, 8), %%mm0			\n\t" //
		"movq %%mm2, %%mm3				\n\t"
		"pmaxub %%mm0, %%mm2				\n\t" //
		"pminub %%mm3, %%mm0				\n\t" //
		"pmaxub %%mm1, %%mm0				\n\t" //
		"pminub %%mm0, %%mm2				\n\t"
		"movq %%mm2, (%%ebx, %1, 2)			\n\t"


		: : "r" (src), "r" (stride)
		: "%eax", "%ebx"
	);

#else // MMX without MMX2
	asm volatile(
		"leal (%0, %1), %%eax				\n\t"
		"leal (%%eax, %1, 4), %%ebx			\n\t"
//	0	1	2	3	4	5	6	7	8	9
//	%0	eax	eax+%1	eax+2%1	%0+4%1	ebx	ebx+%1	ebx+2%1	%0+8%1	ebx+4%1
		"pxor %%mm7, %%mm7				\n\t"

#define MEDIAN(a,b,c)\
		"movq " #a ", %%mm0				\n\t"\
		"movq " #b ", %%mm2				\n\t"\
		"movq " #c ", %%mm1				\n\t"\
		"movq %%mm0, %%mm3				\n\t"\
		"movq %%mm1, %%mm4				\n\t"\
		"movq %%mm2, %%mm5				\n\t"\
		"psubusb %%mm1, %%mm3				\n\t"\
		"psubusb %%mm2, %%mm4				\n\t"\
		"psubusb %%mm0, %%mm5				\n\t"\
		"pcmpeqb %%mm7, %%mm3				\n\t"\
		"pcmpeqb %%mm7, %%mm4				\n\t"\
		"pcmpeqb %%mm7, %%mm5				\n\t"\
		"movq %%mm3, %%mm6				\n\t"\
		"pxor %%mm4, %%mm3				\n\t"\
		"pxor %%mm5, %%mm4				\n\t"\
		"pxor %%mm6, %%mm5				\n\t"\
		"por %%mm3, %%mm1				\n\t"\
		"por %%mm4, %%mm2				\n\t"\
		"por %%mm5, %%mm0				\n\t"\
		"pand %%mm2, %%mm0				\n\t"\
		"pand %%mm1, %%mm0				\n\t"\
		"movq %%mm0, " #b "				\n\t"

MEDIAN((%0), (%%eax), (%%eax, %1))
MEDIAN((%%eax, %1), (%%eax, %1, 2), (%0, %1, 4))
MEDIAN((%0, %1, 4), (%%ebx), (%%ebx, %1))
MEDIAN((%%ebx, %1), (%%ebx, %1, 2), (%0, %1, 8))

		: : "r" (src), "r" (stride)
		: "%eax", "%ebx"
	);
#endif // MMX
#else
	//FIXME
	int x;
	src+= 4*stride;
	for(x=0; x<8; x++)
	{
		src[0       ] = (src[0       ] + 2*src[stride  ] + src[stride*2])>>2;
		src[stride  ] = (src[stride  ] + 2*src[stride*2] + src[stride*3])>>2;
		src[stride*2] = (src[stride*2] + 2*src[stride*3] + src[stride*4])>>2;
		src[stride*3] = (src[stride*3] + 2*src[stride*4] + src[stride*5])>>2;
		src[stride*4] = (src[stride*4] + 2*src[stride*5] + src[stride*6])>>2;
		src[stride*5] = (src[stride*5] + 2*src[stride*6] + src[stride*7])>>2;
		src[stride*6] = (src[stride*6] + 2*src[stride*7] + src[stride*8])>>2;
		src[stride*7] = (src[stride*7] + 2*src[stride*8] + src[stride*9])>>2;
		src++;
	}
#endif
}

#ifdef HAVE_MMX
/**
 * transposes and shift the given 8x8 Block into dst1 and dst2
 */
static inline void transpose1(uint8_t *dst1, uint8_t *dst2, uint8_t *src, int srcStride)
{
	asm(
		"leal (%0, %1), %%eax				\n\t"
		"leal (%%eax, %1, 4), %%ebx			\n\t"
//	0	1	2	3	4	5	6	7	8	9
//	%0	eax	eax+%1	eax+2%1	%0+4%1	ebx	ebx+%1	ebx+2%1	%0+8%1	ebx+4%1
		"movq (%0), %%mm0		\n\t" // 12345678
		"movq (%%eax), %%mm1		\n\t" // abcdefgh
		"movq %%mm0, %%mm2		\n\t" // 12345678
		"punpcklbw %%mm1, %%mm0		\n\t" // 1a2b3c4d
		"punpckhbw %%mm1, %%mm2		\n\t" // 5e6f7g8h

		"movq (%%eax, %1), %%mm1	\n\t"
		"movq (%%eax, %1, 2), %%mm3	\n\t"
		"movq %%mm1, %%mm4		\n\t"
		"punpcklbw %%mm3, %%mm1		\n\t"
		"punpckhbw %%mm3, %%mm4		\n\t"

		"movq %%mm0, %%mm3		\n\t"
		"punpcklwd %%mm1, %%mm0		\n\t"
		"punpckhwd %%mm1, %%mm3		\n\t"
		"movq %%mm2, %%mm1		\n\t"
		"punpcklwd %%mm4, %%mm2		\n\t"
		"punpckhwd %%mm4, %%mm1		\n\t"

		"movd %%mm0, 128(%2)		\n\t"
		"psrlq $32, %%mm0		\n\t"
		"movd %%mm0, 144(%2)		\n\t"
		"movd %%mm3, 160(%2)		\n\t"
		"psrlq $32, %%mm3		\n\t"
		"movd %%mm3, 176(%2)		\n\t"
		"movd %%mm3, 48(%3)		\n\t"
		"movd %%mm2, 192(%2)		\n\t"
		"movd %%mm2, 64(%3)		\n\t"
		"psrlq $32, %%mm2		\n\t"
		"movd %%mm2, 80(%3)		\n\t"
		"movd %%mm1, 96(%3)		\n\t"
		"psrlq $32, %%mm1		\n\t"
		"movd %%mm1, 112(%3)		\n\t"

		"movq (%0, %1, 4), %%mm0	\n\t" // 12345678
		"movq (%%ebx), %%mm1		\n\t" // abcdefgh
		"movq %%mm0, %%mm2		\n\t" // 12345678
		"punpcklbw %%mm1, %%mm0		\n\t" // 1a2b3c4d
		"punpckhbw %%mm1, %%mm2		\n\t" // 5e6f7g8h

		"movq (%%ebx, %1), %%mm1	\n\t"
		"movq (%%ebx, %1, 2), %%mm3	\n\t"
		"movq %%mm1, %%mm4		\n\t"
		"punpcklbw %%mm3, %%mm1		\n\t"
		"punpckhbw %%mm3, %%mm4		\n\t"

		"movq %%mm0, %%mm3		\n\t"
		"punpcklwd %%mm1, %%mm0		\n\t"
		"punpckhwd %%mm1, %%mm3		\n\t"
		"movq %%mm2, %%mm1		\n\t"
		"punpcklwd %%mm4, %%mm2		\n\t"
		"punpckhwd %%mm4, %%mm1		\n\t"

		"movd %%mm0, 132(%2)		\n\t"
		"psrlq $32, %%mm0		\n\t"
		"movd %%mm0, 148(%2)		\n\t"
		"movd %%mm3, 164(%2)		\n\t"
		"psrlq $32, %%mm3		\n\t"
		"movd %%mm3, 180(%2)		\n\t"
		"movd %%mm3, 52(%3)		\n\t"
		"movd %%mm2, 196(%2)		\n\t"
		"movd %%mm2, 68(%3)		\n\t"
		"psrlq $32, %%mm2		\n\t"
		"movd %%mm2, 84(%3)		\n\t"
		"movd %%mm1, 100(%3)		\n\t"
		"psrlq $32, %%mm1		\n\t"
		"movd %%mm1, 116(%3)		\n\t"


	:: "r" (src), "r" (srcStride), "r" (dst1), "r" (dst2)
	: "%eax", "%ebx"
	);
}

/**
 * transposes the given 8x8 block
 */
static inline void transpose2(uint8_t *dst, int dstStride, uint8_t *src)
{
	asm(
		"leal (%0, %1), %%eax				\n\t"
		"leal (%%eax, %1, 4), %%ebx			\n\t"
//	0	1	2	3	4	5	6	7	8	9
//	%0	eax	eax+%1	eax+2%1	%0+4%1	ebx	ebx+%1	ebx+2%1	%0+8%1	ebx+4%1
		"movq (%2), %%mm0		\n\t" // 12345678
		"movq 16(%2), %%mm1		\n\t" // abcdefgh
		"movq %%mm0, %%mm2		\n\t" // 12345678
		"punpcklbw %%mm1, %%mm0		\n\t" // 1a2b3c4d
		"punpckhbw %%mm1, %%mm2		\n\t" // 5e6f7g8h

		"movq 32(%2), %%mm1		\n\t"
		"movq 48(%2), %%mm3		\n\t"
		"movq %%mm1, %%mm4		\n\t"
		"punpcklbw %%mm3, %%mm1		\n\t"
		"punpckhbw %%mm3, %%mm4		\n\t"

		"movq %%mm0, %%mm3		\n\t"
		"punpcklwd %%mm1, %%mm0		\n\t"
		"punpckhwd %%mm1, %%mm3		\n\t"
		"movq %%mm2, %%mm1		\n\t"
		"punpcklwd %%mm4, %%mm2		\n\t"
		"punpckhwd %%mm4, %%mm1		\n\t"

		"movd %%mm0, (%0)		\n\t"
		"psrlq $32, %%mm0		\n\t"
		"movd %%mm0, (%%eax)		\n\t"
		"movd %%mm3, (%%eax, %1)	\n\t"
		"psrlq $32, %%mm3		\n\t"
		"movd %%mm3, (%%eax, %1, 2)	\n\t"
		"movd %%mm2, (%0, %1, 4)	\n\t"
		"psrlq $32, %%mm2		\n\t"
		"movd %%mm2, (%%ebx)		\n\t"
		"movd %%mm1, (%%ebx, %1)	\n\t"
		"psrlq $32, %%mm1		\n\t"
		"movd %%mm1, (%%ebx, %1, 2)	\n\t"


		"movq 64(%2), %%mm0		\n\t" // 12345678
		"movq 80(%2), %%mm1		\n\t" // abcdefgh
		"movq %%mm0, %%mm2		\n\t" // 12345678
		"punpcklbw %%mm1, %%mm0		\n\t" // 1a2b3c4d
		"punpckhbw %%mm1, %%mm2		\n\t" // 5e6f7g8h

		"movq 96(%2), %%mm1		\n\t"
		"movq 112(%2), %%mm3		\n\t"
		"movq %%mm1, %%mm4		\n\t"
		"punpcklbw %%mm3, %%mm1		\n\t"
		"punpckhbw %%mm3, %%mm4		\n\t"

		"movq %%mm0, %%mm3		\n\t"
		"punpcklwd %%mm1, %%mm0		\n\t"
		"punpckhwd %%mm1, %%mm3		\n\t"
		"movq %%mm2, %%mm1		\n\t"
		"punpcklwd %%mm4, %%mm2		\n\t"
		"punpckhwd %%mm4, %%mm1		\n\t"

		"movd %%mm0, 4(%0)		\n\t"
		"psrlq $32, %%mm0		\n\t"
		"movd %%mm0, 4(%%eax)		\n\t"
		"movd %%mm3, 4(%%eax, %1)	\n\t"
		"psrlq $32, %%mm3		\n\t"
		"movd %%mm3, 4(%%eax, %1, 2)	\n\t"
		"movd %%mm2, 4(%0, %1, 4)	\n\t"
		"psrlq $32, %%mm2		\n\t"
		"movd %%mm2, 4(%%ebx)		\n\t"
		"movd %%mm1, 4(%%ebx, %1)	\n\t"
		"psrlq $32, %%mm1		\n\t"
		"movd %%mm1, 4(%%ebx, %1, 2)	\n\t"

	:: "r" (dst), "r" (dstStride), "r" (src)
	: "%eax", "%ebx"
	);
}
#endif
//static int test=0;

static void inline tempNoiseReducer(uint8_t *src, int stride,
				    uint8_t *tempBlured, uint32_t *tempBluredPast, int *maxNoise)
{
#define FAST_L2_DIFF
//#define L1_DIFF //u should change the thresholds too if u try that one
#if defined (HAVE_MMX2) || defined (HAVE_3DNOW)
	asm volatile(
		"leal (%2, %2, 2), %%eax			\n\t" // 3*stride
		"leal (%2, %2, 4), %%ebx			\n\t" // 5*stride
		"leal (%%ebx, %2, 2), %%ecx			\n\t" // 7*stride
//	0	1	2	3	4	5	6	7	8	9
//	%x	%x+%2	%x+2%2	%x+eax	%x+4%2	%x+ebx	%x+2eax	%x+ecx	%x+8%2
//FIXME reorder?
#ifdef L1_DIFF //needs mmx2
		"movq (%0), %%mm0				\n\t" // L0
		"psadbw (%1), %%mm0				\n\t" // |L0-R0|
		"movq (%0, %2), %%mm1				\n\t" // L1
		"psadbw (%1, %2), %%mm1				\n\t" // |L1-R1|
		"movq (%0, %2, 2), %%mm2			\n\t" // L2
		"psadbw (%1, %2, 2), %%mm2			\n\t" // |L2-R2|
		"movq (%0, %%eax), %%mm3			\n\t" // L3
		"psadbw (%1, %%eax), %%mm3			\n\t" // |L3-R3|

		"movq (%0, %2, 4), %%mm4			\n\t" // L4
		"paddw %%mm1, %%mm0				\n\t"
		"psadbw (%1, %2, 4), %%mm4			\n\t" // |L4-R4|
		"movq (%0, %%ebx), %%mm5			\n\t" // L5
		"paddw %%mm2, %%mm0				\n\t"
		"psadbw (%1, %%ebx), %%mm5			\n\t" // |L5-R5|
		"movq (%0, %%eax, 2), %%mm6			\n\t" // L6
		"paddw %%mm3, %%mm0				\n\t"
		"psadbw (%1, %%eax, 2), %%mm6			\n\t" // |L6-R6|
		"movq (%0, %%ecx), %%mm7			\n\t" // L7
		"paddw %%mm4, %%mm0				\n\t"
		"psadbw (%1, %%ecx), %%mm7			\n\t" // |L7-R7|
		"paddw %%mm5, %%mm6				\n\t"
		"paddw %%mm7, %%mm6				\n\t"
		"paddw %%mm6, %%mm0				\n\t"
#elif defined (FAST_L2_DIFF)
		"pcmpeqb %%mm7, %%mm7				\n\t"
		"movq b80, %%mm6				\n\t"
		"pxor %%mm0, %%mm0				\n\t"
#define L2_DIFF_CORE(a, b)\
		"movq " #a ", %%mm5				\n\t"\
		"movq " #b ", %%mm2				\n\t"\
		"pxor %%mm7, %%mm2				\n\t"\
		PAVGB(%%mm2, %%mm5)\
		"paddb %%mm6, %%mm5				\n\t"\
		"movq %%mm5, %%mm2				\n\t"\
		"psllw $8, %%mm5				\n\t"\
		"pmaddwd %%mm5, %%mm5				\n\t"\
		"pmaddwd %%mm2, %%mm2				\n\t"\
		"paddd %%mm2, %%mm5				\n\t"\
		"psrld $14, %%mm5				\n\t"\
		"paddd %%mm5, %%mm0				\n\t"

L2_DIFF_CORE((%0), (%1))
L2_DIFF_CORE((%0, %2), (%1, %2))
L2_DIFF_CORE((%0, %2, 2), (%1, %2, 2))
L2_DIFF_CORE((%0, %%eax), (%1, %%eax))
L2_DIFF_CORE((%0, %2, 4), (%1, %2, 4))
L2_DIFF_CORE((%0, %%ebx), (%1, %%ebx))
L2_DIFF_CORE((%0, %%eax,2), (%1, %%eax,2))
L2_DIFF_CORE((%0, %%ecx), (%1, %%ecx))

#else
		"pxor %%mm7, %%mm7				\n\t"
		"pxor %%mm0, %%mm0				\n\t"
#define L2_DIFF_CORE(a, b)\
		"movq " #a ", %%mm5				\n\t"\
		"movq " #b ", %%mm2				\n\t"\
		"movq %%mm5, %%mm1				\n\t"\
		"movq %%mm2, %%mm3				\n\t"\
		"punpcklbw %%mm7, %%mm5				\n\t"\
		"punpckhbw %%mm7, %%mm1				\n\t"\
		"punpcklbw %%mm7, %%mm2				\n\t"\
		"punpckhbw %%mm7, %%mm3				\n\t"\
		"psubw %%mm2, %%mm5				\n\t"\
		"psubw %%mm3, %%mm1				\n\t"\
		"pmaddwd %%mm5, %%mm5				\n\t"\
		"pmaddwd %%mm1, %%mm1				\n\t"\
		"paddd %%mm1, %%mm5				\n\t"\
		"paddd %%mm5, %%mm0				\n\t"

L2_DIFF_CORE((%0), (%1))
L2_DIFF_CORE((%0, %2), (%1, %2))
L2_DIFF_CORE((%0, %2, 2), (%1, %2, 2))
L2_DIFF_CORE((%0, %%eax), (%1, %%eax))
L2_DIFF_CORE((%0, %2, 4), (%1, %2, 4))
L2_DIFF_CORE((%0, %%ebx), (%1, %%ebx))
L2_DIFF_CORE((%0, %%eax,2), (%1, %%eax,2))
L2_DIFF_CORE((%0, %%ecx), (%1, %%ecx))

#endif

		"movq %%mm0, %%mm4				\n\t"
		"psrlq $32, %%mm0				\n\t"
		"paddd %%mm0, %%mm4				\n\t"
		"movd %%mm4, %%ecx				\n\t"
		"shll $2, %%ecx					\n\t"
		"movl %3, %%ebx					\n\t"
		"addl -4(%%ebx), %%ecx				\n\t"
		"addl 4(%%ebx), %%ecx				\n\t"
		"addl -1024(%%ebx), %%ecx			\n\t"
		"addl $4, %%ecx					\n\t"
		"addl 1024(%%ebx), %%ecx			\n\t"
		"shrl $3, %%ecx					\n\t"
		"movl %%ecx, (%%ebx)				\n\t"
		"leal (%%eax, %2, 2), %%ebx			\n\t" // 5*stride

//		"movl %3, %%ecx				\n\t"
//		"movl %%ecx, test				\n\t"
//		"jmp 4f \n\t"
		"cmpl 4+maxTmpNoise, %%ecx			\n\t"
		" jb 2f						\n\t"
		"cmpl 8+maxTmpNoise, %%ecx			\n\t"
		" jb 1f						\n\t"

		"leal (%%ebx, %2, 2), %%ecx			\n\t" // 7*stride
		"movq (%0), %%mm0				\n\t" // L0
		"movq (%0, %2), %%mm1				\n\t" // L1
		"movq (%0, %2, 2), %%mm2			\n\t" // L2
		"movq (%0, %%eax), %%mm3			\n\t" // L3
		"movq (%0, %2, 4), %%mm4			\n\t" // L4
		"movq (%0, %%ebx), %%mm5			\n\t" // L5
		"movq (%0, %%eax, 2), %%mm6			\n\t" // L6
		"movq (%0, %%ecx), %%mm7			\n\t" // L7
		"movq %%mm0, (%1)				\n\t" // L0
		"movq %%mm1, (%1, %2)				\n\t" // L1
		"movq %%mm2, (%1, %2, 2)			\n\t" // L2
		"movq %%mm3, (%1, %%eax)			\n\t" // L3
		"movq %%mm4, (%1, %2, 4)			\n\t" // L4
		"movq %%mm5, (%1, %%ebx)			\n\t" // L5
		"movq %%mm6, (%1, %%eax, 2)			\n\t" // L6
		"movq %%mm7, (%1, %%ecx)			\n\t" // L7
		"jmp 4f						\n\t"

		"1:						\n\t"
		"leal (%%ebx, %2, 2), %%ecx			\n\t" // 7*stride
		"movq (%0), %%mm0				\n\t" // L0
		"pavgb (%1), %%mm0				\n\t" // L0
		"movq (%0, %2), %%mm1				\n\t" // L1
		"pavgb (%1, %2), %%mm1				\n\t" // L1
		"movq (%0, %2, 2), %%mm2			\n\t" // L2
		"pavgb (%1, %2, 2), %%mm2			\n\t" // L2
		"movq (%0, %%eax), %%mm3			\n\t" // L3
		"pavgb (%1, %%eax), %%mm3			\n\t" // L3
		"movq (%0, %2, 4), %%mm4			\n\t" // L4
		"pavgb (%1, %2, 4), %%mm4			\n\t" // L4
		"movq (%0, %%ebx), %%mm5			\n\t" // L5
		"pavgb (%1, %%ebx), %%mm5			\n\t" // L5
		"movq (%0, %%eax, 2), %%mm6			\n\t" // L6
		"pavgb (%1, %%eax, 2), %%mm6			\n\t" // L6
		"movq (%0, %%ecx), %%mm7			\n\t" // L7
		"pavgb (%1, %%ecx), %%mm7			\n\t" // L7
		"movq %%mm0, (%1)				\n\t" // R0
		"movq %%mm1, (%1, %2)				\n\t" // R1
		"movq %%mm2, (%1, %2, 2)			\n\t" // R2
		"movq %%mm3, (%1, %%eax)			\n\t" // R3
		"movq %%mm4, (%1, %2, 4)			\n\t" // R4
		"movq %%mm5, (%1, %%ebx)			\n\t" // R5
		"movq %%mm6, (%1, %%eax, 2)			\n\t" // R6
		"movq %%mm7, (%1, %%ecx)			\n\t" // R7
		"movq %%mm0, (%0)				\n\t" // L0
		"movq %%mm1, (%0, %2)				\n\t" // L1
		"movq %%mm2, (%0, %2, 2)			\n\t" // L2
		"movq %%mm3, (%0, %%eax)			\n\t" // L3
		"movq %%mm4, (%0, %2, 4)			\n\t" // L4
		"movq %%mm5, (%0, %%ebx)			\n\t" // L5
		"movq %%mm6, (%0, %%eax, 2)			\n\t" // L6
		"movq %%mm7, (%0, %%ecx)			\n\t" // L7
		"jmp 4f						\n\t"

		"2:						\n\t"
		"cmpl maxTmpNoise, %%ecx			\n\t"
		" jb 3f						\n\t"

		"leal (%%ebx, %2, 2), %%ecx			\n\t" // 7*stride
		"movq (%0), %%mm0				\n\t" // L0
		"movq (%0, %2), %%mm1				\n\t" // L1
		"movq (%0, %2, 2), %%mm2			\n\t" // L2
		"movq (%0, %%eax), %%mm3			\n\t" // L3
		"movq (%1), %%mm4				\n\t" // R0
		"movq (%1, %2), %%mm5				\n\t" // R1
		"movq (%1, %2, 2), %%mm6			\n\t" // R2
		"movq (%1, %%eax), %%mm7			\n\t" // R3
		PAVGB(%%mm4, %%mm0)
		PAVGB(%%mm5, %%mm1)
		PAVGB(%%mm6, %%mm2)
		PAVGB(%%mm7, %%mm3)
		PAVGB(%%mm4, %%mm0)
		PAVGB(%%mm5, %%mm1)
		PAVGB(%%mm6, %%mm2)
		PAVGB(%%mm7, %%mm3)
		"movq %%mm0, (%1)				\n\t" // R0
		"movq %%mm1, (%1, %2)				\n\t" // R1
		"movq %%mm2, (%1, %2, 2)			\n\t" // R2
		"movq %%mm3, (%1, %%eax)			\n\t" // R3
		"movq %%mm0, (%0)				\n\t" // L0
		"movq %%mm1, (%0, %2)				\n\t" // L1
		"movq %%mm2, (%0, %2, 2)			\n\t" // L2
		"movq %%mm3, (%0, %%eax)			\n\t" // L3

		"movq (%0, %2, 4), %%mm0			\n\t" // L4
		"movq (%0, %%ebx), %%mm1			\n\t" // L5
		"movq (%0, %%eax, 2), %%mm2			\n\t" // L6
		"movq (%0, %%ecx), %%mm3			\n\t" // L7
		"movq (%1, %2, 4), %%mm4			\n\t" // R4
		"movq (%1, %%ebx), %%mm5			\n\t" // R5
		"movq (%1, %%eax, 2), %%mm6			\n\t" // R6
		"movq (%1, %%ecx), %%mm7			\n\t" // R7
		PAVGB(%%mm4, %%mm0)
		PAVGB(%%mm5, %%mm1)
		PAVGB(%%mm6, %%mm2)
		PAVGB(%%mm7, %%mm3)
		PAVGB(%%mm4, %%mm0)
		PAVGB(%%mm5, %%mm1)
		PAVGB(%%mm6, %%mm2)
		PAVGB(%%mm7, %%mm3)
		"movq %%mm0, (%1, %2, 4)			\n\t" // R4
		"movq %%mm1, (%1, %%ebx)			\n\t" // R5
		"movq %%mm2, (%1, %%eax, 2)			\n\t" // R6
		"movq %%mm3, (%1, %%ecx)			\n\t" // R7
		"movq %%mm0, (%0, %2, 4)			\n\t" // L4
		"movq %%mm1, (%0, %%ebx)			\n\t" // L5
		"movq %%mm2, (%0, %%eax, 2)			\n\t" // L6
		"movq %%mm3, (%0, %%ecx)			\n\t" // L7
		"jmp 4f						\n\t"

		"3:						\n\t"
		"leal (%%ebx, %2, 2), %%ecx			\n\t" // 7*stride
		"movq (%0), %%mm0				\n\t" // L0
		"movq (%0, %2), %%mm1				\n\t" // L1
		"movq (%0, %2, 2), %%mm2			\n\t" // L2
		"movq (%0, %%eax), %%mm3			\n\t" // L3
		"movq (%1), %%mm4				\n\t" // R0
		"movq (%1, %2), %%mm5				\n\t" // R1
		"movq (%1, %2, 2), %%mm6			\n\t" // R2
		"movq (%1, %%eax), %%mm7			\n\t" // R3
		PAVGB(%%mm4, %%mm0)
		PAVGB(%%mm5, %%mm1)
		PAVGB(%%mm6, %%mm2)
		PAVGB(%%mm7, %%mm3)
		PAVGB(%%mm4, %%mm0)
		PAVGB(%%mm5, %%mm1)
		PAVGB(%%mm6, %%mm2)
		PAVGB(%%mm7, %%mm3)
		PAVGB(%%mm4, %%mm0)
		PAVGB(%%mm5, %%mm1)
		PAVGB(%%mm6, %%mm2)
		PAVGB(%%mm7, %%mm3)
		"movq %%mm0, (%1)				\n\t" // R0
		"movq %%mm1, (%1, %2)				\n\t" // R1
		"movq %%mm2, (%1, %2, 2)			\n\t" // R2
		"movq %%mm3, (%1, %%eax)			\n\t" // R3
		"movq %%mm0, (%0)				\n\t" // L0
		"movq %%mm1, (%0, %2)				\n\t" // L1
		"movq %%mm2, (%0, %2, 2)			\n\t" // L2
		"movq %%mm3, (%0, %%eax)			\n\t" // L3

		"movq (%0, %2, 4), %%mm0			\n\t" // L4
		"movq (%0, %%ebx), %%mm1			\n\t" // L5
		"movq (%0, %%eax, 2), %%mm2			\n\t" // L6
		"movq (%0, %%ecx), %%mm3			\n\t" // L7
		"movq (%1, %2, 4), %%mm4			\n\t" // R4
		"movq (%1, %%ebx), %%mm5			\n\t" // R5
		"movq (%1, %%eax, 2), %%mm6			\n\t" // R6
		"movq (%1, %%ecx), %%mm7			\n\t" // R7
		PAVGB(%%mm4, %%mm0)
		PAVGB(%%mm5, %%mm1)
		PAVGB(%%mm6, %%mm2)
		PAVGB(%%mm7, %%mm3)
		PAVGB(%%mm4, %%mm0)
		PAVGB(%%mm5, %%mm1)
		PAVGB(%%mm6, %%mm2)
		PAVGB(%%mm7, %%mm3)
		PAVGB(%%mm4, %%mm0)
		PAVGB(%%mm5, %%mm1)
		PAVGB(%%mm6, %%mm2)
		PAVGB(%%mm7, %%mm3)
		"movq %%mm0, (%1, %2, 4)			\n\t" // R4
		"movq %%mm1, (%1, %%ebx)			\n\t" // R5
		"movq %%mm2, (%1, %%eax, 2)			\n\t" // R6
		"movq %%mm3, (%1, %%ecx)			\n\t" // R7
		"movq %%mm0, (%0, %2, 4)			\n\t" // L4
		"movq %%mm1, (%0, %%ebx)			\n\t" // L5
		"movq %%mm2, (%0, %%eax, 2)			\n\t" // L6
		"movq %%mm3, (%0, %%ecx)			\n\t" // L7

		"4:						\n\t"

		:: "r" (src), "r" (tempBlured), "r"(stride), "m" (tempBluredPast)
		: "%eax", "%ebx", "%ecx", "memory"
		);
//printf("%d\n", test);
#else
	int y;
	int d=0;
	int sysd=0;
	int i;

	for(y=0; y<8; y++)
	{
		int x;
		for(x=0; x<8; x++)
		{
			int ref= tempBlured[ x + y*stride ];
			int cur= src[ x + y*stride ];
			int d1=ref - cur;
//			if(x==0 || x==7) d1+= d1>>1;
//			if(y==0 || y==7) d1+= d1>>1;
//			d+= ABS(d1);
			d+= d1*d1;
			sysd+= d1;
		}
	}
	i=d;
	d= 	(
		4*d
		+(*(tempBluredPast-256))
		+(*(tempBluredPast-1))+ (*(tempBluredPast+1))
		+(*(tempBluredPast+256))
		+4)>>3;
	*tempBluredPast=i;
//	((*tempBluredPast)*3 + d + 2)>>2;

//printf("%d %d %d\n", maxNoise[0], maxNoise[1], maxNoise[2]);
/*
Switch between
 1  0  0  0  0  0  0  (0)
64 32 16  8  4  2  1  (1)
64 48 36 27 20 15 11 (33) (approx)
64 56 49 43 37 33 29 (200) (approx)
*/
	if(d > maxNoise[1])
	{
		if(d < maxNoise[2])
		{
			for(y=0; y<8; y++)
			{
				int x;
				for(x=0; x<8; x++)
				{
					int ref= tempBlured[ x + y*stride ];
					int cur= src[ x + y*stride ];
					tempBlured[ x + y*stride ]=
					src[ x + y*stride ]=
						(ref + cur + 1)>>1;
				}
			}
		}
		else
		{
			for(y=0; y<8; y++)
			{
				int x;
				for(x=0; x<8; x++)
				{
					tempBlured[ x + y*stride ]= src[ x + y*stride ];
				}
			}
		}
	}
	else
	{
		if(d < maxNoise[0])
		{
			for(y=0; y<8; y++)
			{
				int x;
				for(x=0; x<8; x++)
				{
					int ref= tempBlured[ x + y*stride ];
					int cur= src[ x + y*stride ];
					tempBlured[ x + y*stride ]=
					src[ x + y*stride ]=
						(ref*7 + cur + 4)>>3;
				}
			}
		}
		else
		{
			for(y=0; y<8; y++)
			{
				int x;
				for(x=0; x<8; x++)
				{
					int ref= tempBlured[ x + y*stride ];
					int cur= src[ x + y*stride ];
					tempBlured[ x + y*stride ]=
					src[ x + y*stride ]=
						(ref*3 + cur + 2)>>2;
				}
			}
		}
	}
#endif
}

#ifdef HAVE_ODIVX_POSTPROCESS
#include "../opendivx/postprocess.h"
int use_old_pp=0;
#endif

static void postProcess(uint8_t src[], int srcStride, uint8_t dst[], int dstStride, int width, int height,
	QP_STORE_T QPs[], int QPStride, int isColor, struct PPMode *ppMode);

/* -pp Command line Help
NOTE/FIXME: put this at an appropriate place (--help, html docs, man mplayer)?

-pp <filterName>[:<option>[:<option>...]][,[-]<filterName>[:<option>...]]...

long form example:
-pp vdeblock:autoq,hdeblock:autoq,linblenddeint		-pp default,-vdeblock
short form example:
-pp vb:a,hb:a,lb					-pp de,-vb
more examples:
-pp tn:64:128:256

Filters			Options
short	long name	short	long option	Description
*	*		a	autoq		cpu power dependant enabler
			c	chrom		chrominance filtring enabled
			y	nochrom		chrominance filtring disabled
hb	hdeblock				horizontal deblocking filter
vb	vdeblock				vertical deblocking filter
vr	rkvdeblock
h1	x1hdeblock				Experimental horizontal deblock filter 1
v1	x1vdeblock				Experimental vertical deblock filter 1
dr	dering					not implemented yet
al	autolevels				automatic brightness / contrast fixer
			f	fullyrange	stretch luminance range to (0..255)
lb	linblenddeint				linear blend deinterlacer
li	linipoldeint				linear interpolating deinterlacer
ci	cubicipoldeint				cubic interpolating deinterlacer
md	mediandeint				median deinterlacer
de	default					hdeblock:a,vdeblock:a,dering:a,autolevels
fa	fast					x1hdeblock:a,x1vdeblock:a,dering:a,autolevels
tn	tmpnoise	(3 Thresholds)		Temporal Noise Reducer
*/

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

	printf("%s\n", name);

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
		printf("%s::%s\n", filterToken, filterName);

		if(*filterName == '-')
		{
			enable=0;
			filterName++;
		}

		for(;;){ //for all options
			option= strtok(NULL, optionDelimiters);
			if(option == NULL) break;

			printf("%s\n", option);
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
				spaceLeft= (int)p - (int)temp + plen;
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
						if(  !strcmp(options[o],"fullyrange")
						   ||!strcmp(options[o],"f"))
						{
							ppMode.minAllowedY= 0;
							ppMode.maxAllowedY= 255;
							numOfUnknownOptions--;
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

	return ppMode;
}

/**
 * Obsolete, dont use it, use postprocess2() instead
 */
void  postprocess(unsigned char * src[], int src_stride,
                 unsigned char * dst[], int dst_stride,
                 int horizontal_size,   int vertical_size,
                 QP_STORE_T *QP_store,  int QP_stride,
					  int mode)
{
	struct PPMode ppMode;
	static QP_STORE_T zeroArray[2048/8];
/*
	static int qual=0;

	ppMode= getPPModeByNameAndQuality("fast,default,-hdeblock,-vdeblock,tmpnoise:150:200:300", qual);
	printf("OK\n");
	qual++;
	qual%=7;
	printf("\n%X %X %X %X :%d: %d %d %d\n", ppMode.lumMode, ppMode.chromMode, ppMode.oldMode, ppMode.error,
		qual, ppMode.maxTmpNoise[0], ppMode.maxTmpNoise[1], ppMode.maxTmpNoise[2]);
	postprocess2(src, src_stride, dst, dst_stride,
                 horizontal_size, vertical_size, QP_store, QP_stride, &ppMode);

	return;
*/
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
//	mode&= ~(LINEAR_IPOL_DEINT_FILTER | LINEAR_BLEND_DEINT_FILTER |
//		 MEDIAN_DEINT_FILTER | CUBIC_IPOL_DEINT_FILTER);

	if(1)
	{
		postProcess(src[1], src_stride, dst[1], dst_stride,
			horizontal_size, vertical_size, QP_store, QP_stride, 1, &ppMode);
		postProcess(src[2], src_stride, dst[2], dst_stride,
			horizontal_size, vertical_size, QP_store, QP_stride, 2, &ppMode);
	}
	else
	{
		memset(dst[1], 128, dst_stride*vertical_size);
		memset(dst[2], 128, dst_stride*vertical_size);
//		memcpy(dst[1], src[1], src_stride*horizontal_size);
//		memcpy(dst[2], src[2], src_stride*horizontal_size);
	}
}

void  postprocess2(unsigned char * src[], int src_stride,
                 unsigned char * dst[], int dst_stride,
                 int horizontal_size,   int vertical_size,
                 QP_STORE_T *QP_store,  int QP_stride,
		 struct PPMode *mode)
{

	static QP_STORE_T zeroArray[2048/8];
	if(QP_store==NULL)
	{
		QP_store= zeroArray;
		QP_stride= 0;
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

	postProcess(src[1], src_stride, dst[1], dst_stride,
		horizontal_size, vertical_size, QP_store, QP_stride, 1, mode);
	postProcess(src[2], src_stride, dst[2], dst_stride,
		horizontal_size, vertical_size, QP_store, QP_stride, 2, mode);
}


/**
 * gets the mode flags for a given quality (larger values mean slower but better postprocessing)
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
	return modes[quality];
}

/**
 * Copies a block from src to dst and fixes the blacklevel
 * numLines must be a multiple of 4
 * levelFix == 0 -> dont touch the brighness & contrast
 */
static inline void blockCopy(uint8_t dst[], int dstStride, uint8_t src[], int srcStride,
	int numLines, int levelFix)
{
#ifndef HAVE_MMX
	int i;
#endif
	if(levelFix)
	{
#ifdef HAVE_MMX
					asm volatile(
						"leal (%2,%2), %%eax	\n\t"
						"leal (%3,%3), %%ebx	\n\t"
						"movq packedYOffset, %%mm2	\n\t"
						"movq packedYScale, %%mm3	\n\t"
						"pxor %%mm4, %%mm4	\n\t"

#define SCALED_CPY					\
						"movq (%0), %%mm0	\n\t"\
						"movq (%0), %%mm5	\n\t"\
						"punpcklbw %%mm4, %%mm0 \n\t"\
						"punpckhbw %%mm4, %%mm5 \n\t"\
						"psubw %%mm2, %%mm0	\n\t"\
						"psubw %%mm2, %%mm5	\n\t"\
						"movq (%0,%2), %%mm1	\n\t"\
						"psllw $6, %%mm0	\n\t"\
						"psllw $6, %%mm5	\n\t"\
						"pmulhw %%mm3, %%mm0	\n\t"\
						"movq (%0,%2), %%mm6	\n\t"\
						"pmulhw %%mm3, %%mm5	\n\t"\
						"punpcklbw %%mm4, %%mm1 \n\t"\
						"punpckhbw %%mm4, %%mm6 \n\t"\
						"psubw %%mm2, %%mm1	\n\t"\
						"psubw %%mm2, %%mm6	\n\t"\
						"psllw $6, %%mm1	\n\t"\
						"psllw $6, %%mm6	\n\t"\
						"pmulhw %%mm3, %%mm1	\n\t"\
						"pmulhw %%mm3, %%mm6	\n\t"\
						"addl %%eax, %0		\n\t"\
						"packuswb %%mm5, %%mm0	\n\t"\
						"packuswb %%mm6, %%mm1	\n\t"\
						"movq %%mm0, (%1)	\n\t"\
						"movq %%mm1, (%1, %3)	\n\t"\

SCALED_CPY
						"addl %%ebx, %1		\n\t"
SCALED_CPY
						"addl %%ebx, %1		\n\t"
SCALED_CPY
						"addl %%ebx, %1		\n\t"
SCALED_CPY

						: "+r"(src),
						"+r"(dst)
						:"r" (srcStride),
						"r" (dstStride)
						: "%eax", "%ebx"
					);
#else
				for(i=0; i<numLines; i++)
					memcpy(	&(dst[dstStride*i]),
						&(src[srcStride*i]), BLOCK_SIZE);
#endif
	}
	else
	{
#ifdef HAVE_MMX
					asm volatile(
						"movl %4, %%eax \n\t"
						"movl %%eax, temp0\n\t"
						"pushl %0 \n\t"
						"pushl %1 \n\t"
						"leal (%2,%2), %%eax	\n\t"
						"leal (%3,%3), %%ebx	\n\t"
						"movq packedYOffset, %%mm2	\n\t"
						"movq packedYScale, %%mm3	\n\t"

#define SIMPLE_CPY					\
						"movq (%0), %%mm0	\n\t"\
						"movq (%0,%2), %%mm1	\n\t"\
						"movq %%mm0, (%1)	\n\t"\
						"movq %%mm1, (%1, %3)	\n\t"\

						"1:			\n\t"
SIMPLE_CPY
						"addl %%eax, %0		\n\t"
						"addl %%ebx, %1		\n\t"
SIMPLE_CPY
						"addl %%eax, %0		\n\t"
						"addl %%ebx, %1		\n\t"
						"decl temp0		\n\t"
						"jnz 1b			\n\t"

						"popl %1 \n\t"
						"popl %0 \n\t"
						: : "r" (src),
						"r" (dst),
						"r" (srcStride),
						"r" (dstStride),
						"m" (numLines>>2)
						: "%eax", "%ebx"
					);
#else
				for(i=0; i<numLines; i++)
					memcpy(	&(dst[dstStride*i]),
						&(src[srcStride*i]), BLOCK_SIZE);
#endif
	}
}


/**
 * Filters array of bytes (Y or U or V values)
 */
static void postProcess(uint8_t src[], int srcStride, uint8_t dst[], int dstStride, int width, int height,
	QP_STORE_T QPs[], int QPStride, int isColor, struct PPMode *ppMode)
{
	int x,y;
	const int mode= isColor ? ppMode->chromMode : ppMode->lumMode;

	/* we need 64bit here otherwise we´ll going to have a problem
	   after watching a black picture for 5 hours*/
	static uint64_t *yHistogram= NULL;
	int black=0, white=255; // blackest black and whitest white in the picture
	int QPCorrecture= 256;

	/* Temporary buffers for handling the last row(s) */
	static uint8_t *tempDst= NULL;
	static uint8_t *tempSrc= NULL;

	/* Temporary buffers for handling the last block */
	static uint8_t *tempDstBlock= NULL;
	static uint8_t *tempSrcBlock= NULL;

	/* Temporal noise reducing buffers */
	static uint8_t *tempBlured[3]= {NULL,NULL,NULL};
	static uint32_t *tempBluredPast[3]= {NULL,NULL,NULL};

#ifdef PP_FUNNY_STRIDE
	uint8_t *dstBlockPtrBackup;
	uint8_t *srcBlockPtrBackup;
#endif

#ifdef MORE_TIMING
	long long T0, T1, diffTime=0;
#endif
#ifdef TIMING
	long long memcpyTime=0, vertTime=0, horizTime=0, sumTime;
	sumTime= rdtsc();
#endif
//mode= 0x7F;
#ifdef HAVE_MMX
	maxTmpNoise[0]= ppMode->maxTmpNoise[0];
	maxTmpNoise[1]= ppMode->maxTmpNoise[1];
	maxTmpNoise[2]= ppMode->maxTmpNoise[2];
#endif

	if(tempDst==NULL)
	{
		tempDst= (uint8_t*)memalign(8, 1024*24);
		tempSrc= (uint8_t*)memalign(8, 1024*24);
		tempDstBlock= (uint8_t*)memalign(8, 1024*24);
		tempSrcBlock= (uint8_t*)memalign(8, 1024*24);
	}

	if(tempBlured[isColor]==NULL && (mode & TEMP_NOISE_FILTER))
	{
//		printf("%d %d %d\n", isColor, dstStride, height);
		//FIXME works only as long as the size doesnt increase
		//Note:the +17*1024 is just there so i dont have to worry about r/w over te end
		tempBlured[isColor]= (uint8_t*)memalign(8, dstStride*((height+7)&(~7)) + 17*1024);
		tempBluredPast[isColor]= (uint32_t*)memalign(8, 256*((height+7)&(~7))/2 + 17*1024);

		memset(tempBlured[isColor], 0, dstStride*((height+7)&(~7)) + 17*1024);
		memset(tempBluredPast[isColor], 0, 256*((height+7)&(~7))/2 + 17*1024);
	}

	if(!yHistogram)
	{
		int i;
		yHistogram= (uint64_t*)malloc(8*256);
		for(i=0; i<256; i++) yHistogram[i]= width*height/64*15/256;

		if(mode & FULL_Y_RANGE)
		{
			maxAllowedY=255;
			minAllowedY=0;
		}
	}

	if(!isColor)
	{
		uint64_t sum= 0;
		int i;
		static int framenum= -1;
		uint64_t maxClipped;
		uint64_t clipped;
		double scale;

		framenum++;
		if(framenum == 1) yHistogram[0]= width*height/64*15/256;

		for(i=0; i<256; i++)
		{
			sum+= yHistogram[i];
//			printf("%d ", yHistogram[i]);
		}
//		printf("\n\n");

		/* we allways get a completly black picture first */
		maxClipped= (uint64_t)(sum * maxClippedThreshold);

		clipped= sum;
		for(black=255; black>0; black--)
		{
			if(clipped < maxClipped) break;
			clipped-= yHistogram[black];
		}

		clipped= sum;
		for(white=0; white<256; white++)
		{
			if(clipped < maxClipped) break;
			clipped-= yHistogram[white];
		}

		packedYOffset= (black - minAllowedY) & 0xFFFF;
		packedYOffset|= packedYOffset<<32;
		packedYOffset|= packedYOffset<<16;

		scale= (double)(maxAllowedY - minAllowedY) / (double)(white-black);

		packedYScale= (uint16_t)(scale*1024.0 + 0.5);
		packedYScale|= packedYScale<<32;
		packedYScale|= packedYScale<<16;
	}
	else
	{
		packedYScale= 0x0100010001000100LL;
		packedYOffset= 0;
	}

	if(mode & LEVEL_FIX)	QPCorrecture= packedYScale &0xFFFF;
	else			QPCorrecture= 256;

	/* copy & deinterlace first row of blocks */
	y=-BLOCK_SIZE;
	{
		//1% speedup if these are here instead of the inner loop
		uint8_t *srcBlock= &(src[y*srcStride]);
		uint8_t *dstBlock= &(dst[y*dstStride]);

		dstBlock= tempDst + dstStride;

		// From this point on it is guranteed that we can read and write 16 lines downward
		// finish 1 block before the next otherwise we´ll might have a problem
		// with the L1 Cache of the P4 ... or only a few blocks at a time or soemthing
		for(x=0; x<width; x+=BLOCK_SIZE)
		{

#ifdef HAVE_MMX2
/*
			prefetchnta(srcBlock + (((x>>3)&3) + 5)*srcStride + 32);
			prefetchnta(srcBlock + (((x>>3)&3) + 9)*srcStride + 32);
			prefetcht0(dstBlock + (((x>>3)&3) + 5)*dstStride + 32);
			prefetcht0(dstBlock + (((x>>3)&3) + 9)*dstStride + 32);
*/
/*
			prefetchnta(srcBlock + (((x>>2)&6) + 5)*srcStride + 32);
			prefetchnta(srcBlock + (((x>>2)&6) + 6)*srcStride + 32);
			prefetcht0(dstBlock + (((x>>2)&6) + 5)*dstStride + 32);
			prefetcht0(dstBlock + (((x>>2)&6) + 6)*dstStride + 32);
*/

			asm(
				"movl %4, %%eax			\n\t"
				"shrl $2, %%eax			\n\t"
				"andl $6, %%eax			\n\t"
				"addl $8, %%eax			\n\t"
				"movl %%eax, %%ebx		\n\t"
				"imul %1, %%eax			\n\t"
				"imul %3, %%ebx			\n\t"
				"prefetchnta 32(%%eax, %0)	\n\t"
				"prefetcht0 32(%%ebx, %2)	\n\t"
				"addl %1, %%eax			\n\t"
				"addl %3, %%ebx			\n\t"
				"prefetchnta 32(%%eax, %0)	\n\t"
				"prefetcht0 32(%%ebx, %2)	\n\t"
			:: "r" (srcBlock), "r" (srcStride), "r" (dstBlock), "r" (dstStride),
			"m" (x)
			: "%eax", "%ebx"
			);

#elif defined(HAVE_3DNOW)
//FIXME check if this is faster on an 3dnow chip or if its faster without the prefetch or ...
/*			prefetch(srcBlock + (((x>>3)&3) + 5)*srcStride + 32);
			prefetch(srcBlock + (((x>>3)&3) + 9)*srcStride + 32);
			prefetchw(dstBlock + (((x>>3)&3) + 5)*dstStride + 32);
			prefetchw(dstBlock + (((x>>3)&3) + 9)*dstStride + 32);
*/
#endif

			blockCopy(dstBlock + dstStride*8, dstStride,
				srcBlock + srcStride*8, srcStride, 8, mode & LEVEL_FIX);

			if(mode & LINEAR_IPOL_DEINT_FILTER)
				deInterlaceInterpolateLinear(dstBlock, dstStride);
			else if(mode & LINEAR_BLEND_DEINT_FILTER)
				deInterlaceBlendLinear(dstBlock, dstStride);
			else if(mode & MEDIAN_DEINT_FILTER)
				deInterlaceMedian(dstBlock, dstStride);
			else if(mode & CUBIC_IPOL_DEINT_FILTER)
				deInterlaceInterpolateCubic(dstBlock, dstStride);
/*			else if(mode & CUBIC_BLEND_DEINT_FILTER)
				deInterlaceBlendCubic(dstBlock, dstStride);
*/
			dstBlock+=8;
			srcBlock+=8;
		}
		memcpy(&(dst[y*dstStride]) + 8*dstStride, tempDst + 9*dstStride, 8*dstStride );
	}

	for(y=0; y<height; y+=BLOCK_SIZE)
	{
		//1% speedup if these are here instead of the inner loop
		uint8_t *srcBlock= &(src[y*srcStride]);
		uint8_t *dstBlock= &(dst[y*dstStride]);
#ifdef ARCH_X86
		int *QPptr= isColor ? &QPs[(y>>3)*QPStride] :&QPs[(y>>4)*QPStride];
		int QPDelta= isColor ? 1<<(32-3) : 1<<(32-4);
		int QPFrac= QPDelta;
		uint8_t *tempBlock1= tempBlocks;
		uint8_t *tempBlock2= tempBlocks + 8;
#endif
		int QP=0;
		/* can we mess with a 8x16 block from srcBlock/dstBlock downwards and 1 line upwards
		   if not than use a temporary buffer */
		if(y+15 >= height)
		{
			int i;
			/* copy from line 8 to 15 of src, these will be copied with
			   blockcopy to dst later */
			memcpy(tempSrc + srcStride*8, srcBlock + srcStride*8,
				srcStride*MAX(height-y-8, 0) );

			/* duplicate last line of src to fill the void upto line 15 */
			for(i=MAX(height-y, 8); i<=15; i++)
				memcpy(tempSrc + srcStride*i, src + srcStride*(height-1), srcStride);

			/* copy up to 9 lines of dst (line -1 to 7)*/
			memcpy(tempDst, dstBlock - dstStride, dstStride*MIN(height-y+1, 9) );

			/* duplicate last line of dst to fill the void upto line 8 */
			for(i=height-y+1; i<=8; i++)
				memcpy(tempDst + dstStride*i, dst + dstStride*(height-1), dstStride);

			dstBlock= tempDst + dstStride;
			srcBlock= tempSrc;
		}

		// From this point on it is guranteed that we can read and write 16 lines downward
		// finish 1 block before the next otherwise we´ll might have a problem
		// with the L1 Cache of the P4 ... or only a few blocks at a time or soemthing
		for(x=0; x<width; x+=BLOCK_SIZE)
		{
			const int stride= dstStride;
			uint8_t *tmpXchg;
#ifdef ARCH_X86
			QP= *QPptr;
			asm volatile(
				"addl %2, %1		\n\t"
				"sbbl %%eax, %%eax	\n\t"
				"shll $2, %%eax		\n\t"
				"subl %%eax, %0		\n\t"
				: "+r" (QPptr), "+m" (QPFrac)
				: "r" (QPDelta)
				: "%eax"
			);
#else
			QP= isColor ?
                                QPs[(y>>3)*QPStride + (x>>3)]:
                                QPs[(y>>4)*QPStride + (x>>4)];
#endif
			if(!isColor)
			{
				QP= (QP* QPCorrecture)>>8;
				yHistogram[ srcBlock[srcStride*12 + 4] ]++;
			}
#ifdef HAVE_MMX
			asm volatile(
				"movd %0, %%mm7					\n\t"
				"packuswb %%mm7, %%mm7				\n\t" // 0, 0, 0, QP, 0, 0, 0, QP
				"packuswb %%mm7, %%mm7				\n\t" // 0,QP, 0, QP, 0,QP, 0, QP
				"packuswb %%mm7, %%mm7				\n\t" // QP,..., QP
				"movq %%mm7, pQPb				\n\t"
				: : "r" (QP)
			);
#endif

#ifdef MORE_TIMING
			T0= rdtsc();
#endif

#ifdef HAVE_MMX2
/*
			prefetchnta(srcBlock + (((x>>3)&3) + 5)*srcStride + 32);
			prefetchnta(srcBlock + (((x>>3)&3) + 9)*srcStride + 32);
			prefetcht0(dstBlock + (((x>>3)&3) + 5)*dstStride + 32);
			prefetcht0(dstBlock + (((x>>3)&3) + 9)*dstStride + 32);
*/
/*
			prefetchnta(srcBlock + (((x>>2)&6) + 5)*srcStride + 32);
			prefetchnta(srcBlock + (((x>>2)&6) + 6)*srcStride + 32);
			prefetcht0(dstBlock + (((x>>2)&6) + 5)*dstStride + 32);
			prefetcht0(dstBlock + (((x>>2)&6) + 6)*dstStride + 32);
*/

			asm(
				"movl %4, %%eax			\n\t"
				"shrl $2, %%eax			\n\t"
				"andl $6, %%eax			\n\t"
				"addl $8, %%eax			\n\t"
				"movl %%eax, %%ebx		\n\t"
				"imul %1, %%eax			\n\t"
				"imul %3, %%ebx			\n\t"
				"prefetchnta 32(%%eax, %0)	\n\t"
				"prefetcht0 32(%%ebx, %2)	\n\t"
				"addl %1, %%eax			\n\t"
				"addl %3, %%ebx			\n\t"
				"prefetchnta 32(%%eax, %0)	\n\t"
				"prefetcht0 32(%%ebx, %2)	\n\t"
			:: "r" (srcBlock), "r" (srcStride), "r" (dstBlock), "r" (dstStride),
			"m" (x)
			: "%eax", "%ebx"
			);

#elif defined(HAVE_3DNOW)
//FIXME check if this is faster on an 3dnow chip or if its faster without the prefetch or ...
/*			prefetch(srcBlock + (((x>>3)&3) + 5)*srcStride + 32);
			prefetch(srcBlock + (((x>>3)&3) + 9)*srcStride + 32);
			prefetchw(dstBlock + (((x>>3)&3) + 5)*dstStride + 32);
			prefetchw(dstBlock + (((x>>3)&3) + 9)*dstStride + 32);
*/
#endif

#ifdef PP_FUNNY_STRIDE
			//can we mess with a 8x16 block, if not use a temp buffer, yes again
			if(x+7 >= width)
			{
				int i;
				dstBlockPtrBackup= dstBlock;
				srcBlockPtrBackup= srcBlock;

				for(i=0;i<BLOCK_SIZE*2; i++)
				{
					memcpy(tempSrcBlock+i*srcStride, srcBlock+i*srcStride, width-x);
					memcpy(tempDstBlock+i*dstStride, dstBlock+i*dstStride, width-x);
				}

				dstBlock= tempDstBlock;
				srcBlock= tempSrcBlock;
			}
#endif

			blockCopy(dstBlock + dstStride*8, dstStride,
				srcBlock + srcStride*8, srcStride, 8, mode & LEVEL_FIX);

			if(mode & LINEAR_IPOL_DEINT_FILTER)
				deInterlaceInterpolateLinear(dstBlock, dstStride);
			else if(mode & LINEAR_BLEND_DEINT_FILTER)
				deInterlaceBlendLinear(dstBlock, dstStride);
			else if(mode & MEDIAN_DEINT_FILTER)
				deInterlaceMedian(dstBlock, dstStride);
			else if(mode & CUBIC_IPOL_DEINT_FILTER)
				deInterlaceInterpolateCubic(dstBlock, dstStride);
/*			else if(mode & CUBIC_BLEND_DEINT_FILTER)
				deInterlaceBlendCubic(dstBlock, dstStride);
*/

			/* only deblock if we have 2 blocks */
			if(y + 8 < height)
			{
#ifdef MORE_TIMING
				T1= rdtsc();
				memcpyTime+= T1-T0;
				T0=T1;
#endif
				if(mode & V_RK1_FILTER)
					vertRK1Filter(dstBlock, stride, QP);
				else if(mode & V_X1_FILTER)
					vertX1Filter(dstBlock, stride, QP);
				else if(mode & V_DEBLOCK)
				{
					if( isVertDC(dstBlock, stride))
					{
						if(isVertMinMaxOk(dstBlock, stride, QP))
							doVertLowPass(dstBlock, stride, QP);
					}
					else
						doVertDefFilter(dstBlock, stride, QP);
				}
#ifdef MORE_TIMING
				T1= rdtsc();
				vertTime+= T1-T0;
				T0=T1;
#endif
			}

#ifdef HAVE_MMX
			transpose1(tempBlock1, tempBlock2, dstBlock, dstStride);
#endif
			/* check if we have a previous block to deblock it with dstBlock */
			if(x - 8 >= 0)
			{
#ifdef MORE_TIMING
				T0= rdtsc();
#endif
#ifdef HAVE_MMX
				if(mode & H_RK1_FILTER)
					vertRK1Filter(tempBlock1, 16, QP);
				else if(mode & H_X1_FILTER)
					vertX1Filter(tempBlock1, 16, QP);
				else if(mode & H_DEBLOCK)
				{
					if( isVertDC(tempBlock1, 16))
					{
						if(isVertMinMaxOk(tempBlock1, 16, QP))
							doVertLowPass(tempBlock1, 16, QP);
					}
					else
						doVertDefFilter(tempBlock1, 16, QP);
				}

				transpose2(dstBlock-4, dstStride, tempBlock1 + 4*16);

#else
				if(mode & H_X1_FILTER)
					horizX1Filter(dstBlock-4, stride, QP);
				else if(mode & H_DEBLOCK)
				{
					if( isHorizDC(dstBlock-4, stride))
					{
						if(isHorizMinMaxOk(dstBlock-4, stride, QP))
							doHorizLowPass(dstBlock-4, stride, QP);
					}
					else
						doHorizDefFilter(dstBlock-4, stride, QP);
				}
#endif
#ifdef MORE_TIMING
				T1= rdtsc();
				horizTime+= T1-T0;
				T0=T1;
#endif
				if(mode & DERING)
				{
				//FIXME filter first line
					if(y>0) dering(dstBlock - stride - 8, stride, QP);
				}

				if(mode & TEMP_NOISE_FILTER)
				{
					tempNoiseReducer(dstBlock-8, stride,
						tempBlured[isColor] + y*dstStride + x,
						tempBluredPast[isColor] + (y>>3)*256 + (x>>3),
						ppMode->maxTmpNoise);
				}
			}

#ifdef PP_FUNNY_STRIDE
			/* did we use a tmp-block buffer */
			if(x+7 >= width)
			{
				int i;
				dstBlock= dstBlockPtrBackup;
				srcBlock= srcBlockPtrBackup;

				for(i=0;i<BLOCK_SIZE*2; i++)
				{
					memcpy(dstBlock+i*dstStride, tempDstBlock+i*dstStride, width-x);
				}
			}
#endif

			dstBlock+=8;
			srcBlock+=8;

#ifdef HAVE_MMX
			tmpXchg= tempBlock1;
			tempBlock1= tempBlock2;
			tempBlock2 = tmpXchg;
#endif
		}

		if(mode & DERING)
		{
				if(y > 0) dering(dstBlock - dstStride - 8, dstStride, QP);
		}

		if((mode & TEMP_NOISE_FILTER))
		{
			tempNoiseReducer(dstBlock-8, dstStride,
				tempBlured[isColor] + y*dstStride + x,
				tempBluredPast[isColor] + (y>>3)*256 + (x>>3),
				ppMode->maxTmpNoise);
		}

		/* did we use a tmp buffer for the last lines*/
		if(y+15 >= height)
		{
			uint8_t *dstBlock= &(dst[y*dstStride]);
			memcpy(dstBlock, tempDst + dstStride, dstStride*(height-y) );
		}
/*
		for(x=0; x<width; x+=32)
		{
			int i;
			i+=	+ dstBlock[x + 7*dstStride] + dstBlock[x + 8*dstStride]
				+ dstBlock[x + 9*dstStride] + dstBlock[x +10*dstStride]
				+ dstBlock[x +11*dstStride] + dstBlock[x +12*dstStride]
				+ dstBlock[x +13*dstStride] + dstBlock[x +14*dstStride]
				+ dstBlock[x +15*dstStride];
		}
*/	}
#ifdef HAVE_3DNOW
	asm volatile("femms");
#elif defined (HAVE_MMX)
	asm volatile("emms");
#endif

#ifdef TIMING
	// FIXME diff is mostly the time spent for rdtsc (should subtract that but ...)
	sumTime= rdtsc() - sumTime;
	if(!isColor)
		printf("cpy:%4dk, vert:%4dk, horiz:%4dk, sum:%4dk, diff:%4dk, color: %d/%d    \r",
			(int)(memcpyTime/1000), (int)(vertTime/1000), (int)(horizTime/1000),
			(int)(sumTime/1000), (int)((sumTime-memcpyTime-vertTime-horizTime)/1000)
			, black, white);
#endif
#ifdef DEBUG_BRIGHTNESS
	if(!isColor)
	{
		int max=1;
		int i;
		for(i=0; i<256; i++)
			if(yHistogram[i] > max) max=yHistogram[i];

		for(i=1; i<256; i++)
		{
			int x;
			int start=yHistogram[i-1]/(max/256+1);
			int end=yHistogram[i]/(max/256+1);
			int inc= end > start ? 1 : -1;
			for(x=start; x!=end+inc; x+=inc)
				dst[ i*dstStride + x]+=128;
		}

		for(i=0; i<100; i+=2)
		{
			dst[ (white)*dstStride + i]+=128;
			dst[ (black)*dstStride + i]+=128;
		}

	}
#endif

}
