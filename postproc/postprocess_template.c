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
			C	MMX	MMX2	3DNow*
isVertDC		Ec	Ec
isVertMinMaxOk		Ec	Ec
doVertLowPass		E		e	e*
doVertDefFilter		Ec	Ec	Ec
isHorizDC		Ec	Ec
isHorizMinMaxOk		a
doHorizLowPass		E		a	a*
doHorizDefFilter	E	ac	ac
deRing

* i dont have a 3dnow CPU -> its untested
E = Exact implementation
e = allmost exact implementation
a = alternative / approximate impl
c = checked against the other implementations (-vo md5)
*/

/*
TODO:
verify that everything workes as it should
reduce the time wasted on the mem transfer
implement dering
implement everything in C at least (done at the moment but ...)
figure range of QP out (assuming <256 for now)
unroll stuff if instructions depend too much on the prior one
we use 8x8 blocks for the horizontal filters, opendivx seems to use 8x4?
move YScale thing to the end instead of fixing QP
write a faster and higher quality deblocking filter :)
...

Notes:

*/

/*
Changelog:
0.1.2
	fixed a bug in the horizontal default filter
	3dnow version of the Horizontal & Vertical Lowpass filters
	mmx version of the Horizontal Default filter
	mmx2 & C versions of a simple filter described in a paper from ramkishor & karandikar
	added mode flags & quality2mode function
0.1.1
*/


#include <inttypes.h>
#include <stdio.h>
#include "../config.h"
//#undef HAVE_MMX2
//#define HAVE_3DNOW
//#undef HAVE_MMX
#include "postprocess.h"


static uint64_t packedYOffset=	0x0000000000000000LL;
static uint64_t packedYScale=	0x0100010001000100LL;
static uint64_t w05=		0x0005000500050005LL;
static uint64_t w20=		0x0020002000200020LL;
static uint64_t w1400=		0x1400140014001400LL;
static uint64_t bm00000001=	0x00000000000000FFLL;
static uint64_t bm00010000=	0x000000FF00000000LL;
static uint64_t bm00001000=	0x00000000FF000000LL;
static uint64_t bm10000000=	0xFF00000000000000LL;
static uint64_t bm10000001=	0xFF000000000000FFLL;
static uint64_t bm11000011=	0xFFFF00000000FFFFLL;
static uint64_t bm00000011=	0x000000000000FFFFLL;
static uint64_t bm11000000=	0xFFFF000000000000LL;
static uint64_t bm00011000=	0x000000FFFF000000LL;
static uint64_t bm00110011=	0x0000FFFF0000FFFFLL;
static uint64_t bm11001100=	0xFFFF0000FFFF0000LL;
static uint64_t b00= 		0x0000000000000000LL;
static uint64_t b02= 		0x0202020202020202LL;
static uint64_t b0F= 		0x0F0F0F0F0F0F0F0FLL;
static uint64_t bFF= 		0xFFFFFFFFFFFFFFFFLL;
static uint64_t b20= 		0x2020202020202020LL;
static uint64_t b80= 		0x8080808080808080LL;
static uint64_t b7E= 		0x7E7E7E7E7E7E7E7ELL;
static uint64_t b7C= 		0x7C7C7C7C7C7C7C7CLL;
static uint64_t b3F= 		0x3F3F3F3F3F3F3F3FLL;
static uint64_t temp0=0;
static uint64_t temp1=0;
static uint64_t temp2=0;
static uint64_t temp3=0;
static uint64_t temp4=0;
static uint64_t temp5=0;
static uint64_t pQPb=0;
static uint8_t tempBlock[16*16];

int hFlatnessThreshold= 56 - 16;
int vFlatnessThreshold= 56 - 16;

//amount of "black" u r willing to loose to get a brightness corrected picture
double maxClippedThreshold= 0.01;

int maxAllowedY=255;
//FIXME can never make a movie´s black brighter (anyone needs that?)
int minAllowedY=0;


static inline long long rdtsc()
{
	long long l;
	asm volatile(	"rdtsc\n\t"
		: "=A" (l)
	);
//	printf("%d\n", int(l/1000));
	return l;
}

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

//FIXME? |255-0| = 1 (shouldnt be a problem ...)
/**
 * Check if the middle 8x8 Block in the given 8x10 block is flat
 */
static inline bool isVertDC(uint8_t src[], int stride){
//	return true;
	int numEq= 0;
	src+= stride; // src points to begin of the 8x8 Block
#ifdef HAVE_MMX
	asm volatile(
//		"int $3 \n\t"
		"pushl %1\n\t"
		"movq b7E, %%mm7					\n\t" // mm7 = 0x7F
		"movq b7C, %%mm6					\n\t" // mm6 = 0x7D
		"movq (%1), %%mm0				\n\t"
		"addl %2, %1					\n\t"
		"movq (%1), %%mm1				\n\t"
		"psubb %%mm1, %%mm0				\n\t" // mm0 = differnece
		"paddb %%mm7, %%mm0				\n\t"
		"pcmpgtb %%mm6, %%mm0				\n\t"

		"addl %2, %1					\n\t"
		"movq (%1), %%mm2				\n\t"
		"psubb %%mm2, %%mm1				\n\t"
		"paddb %%mm7, %%mm1				\n\t"
		"pcmpgtb %%mm6, %%mm1				\n\t"
		"paddb %%mm1, %%mm0				\n\t"

		"addl %2, %1					\n\t"
		"movq (%1), %%mm1				\n\t"
		"psubb %%mm1, %%mm2				\n\t"
		"paddb %%mm7, %%mm2				\n\t"
		"pcmpgtb %%mm6, %%mm2				\n\t"
		"paddb %%mm2, %%mm0				\n\t"

		"addl %2, %1					\n\t"
		"movq (%1), %%mm2				\n\t"
		"psubb %%mm2, %%mm1				\n\t"
		"paddb %%mm7, %%mm1				\n\t"
		"pcmpgtb %%mm6, %%mm1				\n\t"
		"paddb %%mm1, %%mm0				\n\t"

		"addl %2, %1					\n\t"
		"movq (%1), %%mm1				\n\t"
		"psubb %%mm1, %%mm2				\n\t"
		"paddb %%mm7, %%mm2				\n\t"
		"pcmpgtb %%mm6, %%mm2				\n\t"
		"paddb %%mm2, %%mm0				\n\t"

		"addl %2, %1					\n\t"
		"movq (%1), %%mm2				\n\t"
		"psubb %%mm2, %%mm1				\n\t"
		"paddb %%mm7, %%mm1				\n\t"
		"pcmpgtb %%mm6, %%mm1				\n\t"
		"paddb %%mm1, %%mm0				\n\t"

		"addl %2, %1					\n\t"
		"movq (%1), %%mm1				\n\t"
		"psubb %%mm1, %%mm2				\n\t"
		"paddb %%mm7, %%mm2				\n\t"
		"pcmpgtb %%mm6, %%mm2				\n\t"
		"paddb %%mm2, %%mm0				\n\t"

		"						\n\t"
		"movq %%mm0, %%mm1				\n\t"
		"psrlw $8, %%mm0				\n\t"
		"paddb %%mm1, %%mm0				\n\t"
		"movq %%mm0, %%mm1				\n\t"
		"psrlq $16, %%mm0				\n\t"
		"paddb %%mm1, %%mm0				\n\t"
		"movq %%mm0, %%mm1				\n\t"
		"psrlq $32, %%mm0				\n\t"
		"paddb %%mm1, %%mm0				\n\t"
		"popl %1\n\t"
		"movd %%mm0, %0					\n\t"
		: "=r" (numEq)
		: "r" (src), "r" (stride)
		);
//	printf("%d\n", numEq);
	numEq= (256 - (numEq & 0xFF)) &0xFF;

//	int asmEq= numEq;
//	numEq=0;
//	uint8_t *temp= src;

#else
	for(int y=0; y<BLOCK_SIZE-1; y++)
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
	return numEq > vFlatnessThreshold;
}

static inline bool isVertMinMaxOk(uint8_t src[], int stride, int QP)
{
#ifdef HAVE_MMX
	int isOk;
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

	int isOk2= true;
	for(int x=0; x<BLOCK_SIZE; x++)
	{
		if(abs((int)src[x + stride] - (int)src[x + (stride<<3)]) > 2*QP) isOk2=false;
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
 * Do a vertical low pass filter on the 8x10 block (only write to the 8x8 block in the middle)
 * useing the 9-Tap Filter (1,1,2,2,4,2,2,1,1)/16
 */
static inline void doVertLowPass(uint8_t *src, int stride, int QP)
{
//	QP= 64;

#if defined (HAVE_MMX2) || defined (HAVE_3DNOW)
//#ifdef HAVE_MMX2
	asm volatile(	//"movv %0 %1 %2\n\t"
		"pushl %0 \n\t"
		"movq pQPb, %%mm0				\n\t"  // QP,..., QP
//		"movq bFF  , %%mm0				\n\t"  // QP,..., QP

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
/*
		"movq %%mm6, %%mm2				\n\t" //1
		"movq %%mm6, %%mm3				\n\t" //1
		"paddusb b02, %%mm3 				\n\t"
		"psrlw $2, %%mm3				\n\t" //1	/4
		"pand b3F, %%mm3				\n\t"
		"psubb %%mm3, %%mm2				\n\t"
		"movq (%0, %1), %%mm0				\n\t" //  1
		"movq %%mm0, %%mm1				\n\t" //  1
		"paddusb b02, %%mm0				\n\t"
		"psrlw $2, %%mm0				\n\t" //  1	/4
		"pand b3F, %%mm0				\n\t"
		"paddusb %%mm2, %%mm0				\n\t" //3 1	/4
*/
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
//		"pxor %%mm1, %%mm1 \n\t"
		"movq %%mm1, (%%eax, %1, 2)			\n\t" //      X
		// mm2=3(112) mm3=6(11) mm4=5 mm5=4(11) mm6=6 mm7=9
		PAVGB((%%ebx), %%mm2)				      //   112 4	/8
		"movq (%%eax, %1, 4), %%mm0			\n\t" //        1
		PAVGB(%%mm0, %%mm6)				      //      1 1	/2
		PAVGB(%%mm7, %%mm6)				      //      1 12	/4
		PAVGB(%%mm2, %%mm6)				      //   1122424	/4
//		"pxor %%mm6, %%mm6 \n\t"
		"movq %%mm6, (%%ebx)				\n\t" //       X
		// mm0=8 mm3=6(11) mm4=5 mm5=4(11) mm7=9
		PAVGB(%%mm7, %%mm5)				      //    11   2	/4
		PAVGB(%%mm7, %%mm5)				      //    11   6	/8

		PAVGB(%%mm3, %%mm0)				      //      112	/4
		PAVGB(%%mm0, %%mm5)				      //    112246	/16
//		"pxor %%mm5, %%mm5 \n\t"
//		"movq pQPb, %%mm5 \n\t"
		"movq %%mm5, (%%eax, %1, 4)			\n\t" //        X
		"popl %0\n\t"

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

	for(int x=0; x<BLOCK_SIZE; x++)
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
		src[l2]= ((src[l2]<<2) + (first + sums[0] + sums[3]<<1) + sums[5] + 8)>>4;
		src[l3]= ((src[l3]<<2) + (first + sums[1] + sums[4]<<1) + sums[6] + 8)>>4;
		src[l4]= ((src[l4]<<2) + (sums[2] + sums[5]<<1) + sums[0] + sums[7] + 8)>>4;
		src[l5]= ((src[l5]<<2) + (sums[3] + sums[6]<<1) + sums[1] + sums[8] + 8)>>4;
		src[l6]= ((src[l6]<<2) + (last + sums[7] + sums[4]<<1) + sums[2] + 8)>>4;
		src[l7]= ((last + src[l7]<<2) + (src[l8] + sums[5]<<1) + sums[3] + 8)>>4;
		src[l8]= ((sums[8]<<2) + (last + sums[6]<<1) + sums[4] + 8)>>4;

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
static inline void vertRKFilter(uint8_t *src, int stride, int QP)
{
#ifdef HAVE_MMX2
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
		"pavgb %%mm3, %%mm5				\n\t"
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
	const int l7= stride + l6;
	const int l8= stride + l7;
	const int l9= stride + l8;
	for(int x=0; x<BLOCK_SIZE; x++)
	{
		if(ABS(src[l4]-src[l5]) < QP + QP/4)
		{
			int x = src[l5] - src[l4];

			src[l3] +=x/8;
			src[l4] +=x/2;
			src[l5] -=x/2;
			src[l6] -=x/8;
		}
		src++;
	}

#endif
}

/**
 * Experimental Filter 1
 */
static inline void vertX1Filter(uint8_t *src, int stride, int QP)
{
#ifdef HAVE_MMX2X
// FIXME
	asm volatile(

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

#endif
}


static inline void doVertDefFilter(uint8_t src[], int stride, int QP)
{
#ifdef HAVE_MMX
	src+= stride;
	//FIXME try pmul for *5 stuff
//	src[0]=0;
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
//FIXME pxor, psubw, pmax for abs
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
//"pcmpeqb %%mm2, %%mm2\n\t"
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

		// FIXME rounding error
		"psraw $1, %%mm0				\n\t" // (L3 - L4)/2
		"psraw $1, %%mm1				\n\t" // (H3 - H4)/2
		"pcmpgtw %%mm0, %%mm2				\n\t" // sign (L3-L4)
		"pcmpgtw %%mm1, %%mm3				\n\t" // sign (H3-H4)
		"pxor %%mm2, %%mm0				\n\t"
		"pxor %%mm3, %%mm1				\n\t"
		"psubw %%mm2, %%mm0				\n\t" // |L3-L4|
		"psubw %%mm3, %%mm1				\n\t" // |H3-H4|
//		"psrlw $1, %%mm0				\n\t" // |L3 - L4|/2
//		"psrlw $1, %%mm1				\n\t" // |H3 - H4|/2

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
//		"pxor %%mm0, %%mm0 \n\t"
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

	for(int x=0; x<BLOCK_SIZE; x++)
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
 * Check if the given 8x8 Block is mostly "flat" and copy the unaliged data into tempBlock.
 */
static inline bool isHorizDCAndCopy2Temp(uint8_t src[], int stride)
{
//	src++;
	int numEq= 0;
#ifdef HAVE_MMX
asm volatile (
//		"int $3 \n\t"
		"pushl %1\n\t"
		"movq b7E, %%mm7				\n\t" // mm7 = 0x7F
		"movq b7C, %%mm6				\n\t" // mm6 = 0x7D
		"leal tempBlock, %%eax				\n\t"
		"pxor %%mm0, %%mm0				\n\t"

#define HDC_CHECK_AND_CPY(i) \
		"movq -4(%1), %%mm2				\n\t"\
		"psrlq $32, %%mm2				\n\t"\
		"punpckldq 4(%1), %%mm2				\n\t" /* (%1) */\
		"movq %%mm2, %%mm1				\n\t"\
		"psrlq $8, %%mm2				\n\t"\
		"psubb %%mm1, %%mm2				\n\t"\
		"paddb %%mm7, %%mm2				\n\t"\
		"pcmpgtb %%mm6, %%mm2				\n\t"\
		"paddb %%mm2, %%mm0				\n\t"\
		"movq %%mm1," #i "(%%eax)			\n\t"

		HDC_CHECK_AND_CPY(0)
		"addl %2, %1					\n\t"
		HDC_CHECK_AND_CPY(8)
		"addl %2, %1					\n\t"
		HDC_CHECK_AND_CPY(16)
		"addl %2, %1					\n\t"
		HDC_CHECK_AND_CPY(24)
		"addl %2, %1					\n\t"
		HDC_CHECK_AND_CPY(32)
		"addl %2, %1					\n\t"
		HDC_CHECK_AND_CPY(40)
		"addl %2, %1					\n\t"
		HDC_CHECK_AND_CPY(48)
		"addl %2, %1					\n\t"
		HDC_CHECK_AND_CPY(56)

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
		"popl %1\n\t"
		"movd %%mm0, %0					\n\t"
		: "=r" (numEq)
		: "r" (src), "r" (stride)
		: "%eax"
		);
//	printf("%d\n", numEq);
	numEq= (256 - (numEq & 0xFF)) &0xFF;
#else
	for(int y=0; y<BLOCK_SIZE; y++)
	{
		if(((src[0] - src[1] + 1) & 0xFFFF) < 3) numEq++;
		if(((src[1] - src[2] + 1) & 0xFFFF) < 3) numEq++;
		if(((src[2] - src[3] + 1) & 0xFFFF) < 3) numEq++;
		if(((src[3] - src[4] + 1) & 0xFFFF) < 3) numEq++;
		if(((src[4] - src[5] + 1) & 0xFFFF) < 3) numEq++;
		if(((src[5] - src[6] + 1) & 0xFFFF) < 3) numEq++;
		if(((src[6] - src[7] + 1) & 0xFFFF) < 3) numEq++;
		tempBlock[0 + y*TEMP_STRIDE] = src[0];
		tempBlock[1 + y*TEMP_STRIDE] = src[1];
		tempBlock[2 + y*TEMP_STRIDE] = src[2];
		tempBlock[3 + y*TEMP_STRIDE] = src[3];
		tempBlock[4 + y*TEMP_STRIDE] = src[4];
		tempBlock[5 + y*TEMP_STRIDE] = src[5];
		tempBlock[6 + y*TEMP_STRIDE] = src[6];
		tempBlock[7 + y*TEMP_STRIDE] = src[7];
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

static inline bool isHorizMinMaxOk(uint8_t src[], int stride, int QP)
{
#ifdef MMX_FIXME
FIXME
	int isOk;
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
	if(abs(src[0] - src[7]) > 2*QP) return false;

	return true;
#endif
}

static inline void doHorizDefFilterAndCopyBack(uint8_t dst[], int stride, int QP)
{
#ifdef HAVE_MMX
	asm volatile(
		"pushl %0					\n\t"
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
#define HDF(i)	\
		"movq " #i "(%%eax), %%mm0			\n\t"\
		"movq %%mm0, %%mm1				\n\t"\
		"movq %%mm0, %%mm2				\n\t"\
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
		"movd %%mm0, (%0)				\n\t"\
		"psrlq $32, %%mm0				\n\t"\
		"movd %%mm0, 4(%0)				\n\t"
#else
#define HDF(i)\
		"movq " #i "(%%eax), %%mm0			\n\t"\
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
		"movd %%mm0, (%0)				\n\t"\
		"psrlq $32, %%mm0				\n\t"\
		"movd %%mm0, 4(%0)				\n\t"
#endif
		HDF(0)
		"addl %1, %0					\n\t"
		HDF(8)
		"addl %1, %0					\n\t"
		HDF(16)
		"addl %1, %0					\n\t"
		HDF(24)
		"addl %1, %0					\n\t"
		HDF(32)
		"addl %1, %0					\n\t"
		HDF(40)
		"addl %1, %0					\n\t"
		HDF(48)
		"addl %1, %0					\n\t"
		HDF(56)
		"popl %0					\n\t"
		:
		: "r" (dst), "r" (stride), "r" (QP)
		: "%eax"
	);
#else
	uint8_t *src= tempBlock;

	for(int y=0; y<BLOCK_SIZE; y++)
	{
		dst[0] = src[0];
		dst[1] = src[1];
		dst[2] = src[2];
		dst[3] = src[3];
		dst[4] = src[4];
		dst[5] = src[5];
		dst[6] = src[6];
		dst[7] = src[7];

		const int middleEnergy= 5*(src[4] - src[5]) + 2*(src[2] - src[5]);
		if(ABS(middleEnergy) < 8*QP)
		{
			const int q=(src[3] - src[4])/2;
			const int leftEnergy=  5*(src[2] - src[1]) + 2*(src[0] - src[3]);
			const int rightEnergy= 5*(src[6] - src[5]) + 2*(src[4] - src[7]);

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
		src+= TEMP_STRIDE;
	}
#endif
}

/**
 * Do a horizontal low pass filter on the 8x8 block
 * useing the 9-Tap Filter (1,1,2,2,4,2,2,1,1)/16 (C version)
 * useing approximately the 7-Tap Filter (1,2,3,4,3,2,1)/16 (MMX2/3DNOW version)
 */
static inline void doHorizLowPassAndCopyBack(uint8_t dst[], int stride, int QP)
{
//return;
#if defined (HAVE_MMX2) || defined (HAVE_3DNOW)
	asm volatile(	//"movv %0 %1 %2\n\t"
		"pushl %0\n\t"
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
 31
 121
  121
   121
    121
     121
      121
       13
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

#define HLP(i) HLP3(i)

		HLP(0)
		"addl %1, %0						\n\t"
		HLP(8)
		"addl %1, %0						\n\t"
		HLP(16)
		"addl %1, %0						\n\t"
		HLP(24)
		"addl %1, %0						\n\t"
		HLP(32)
		"addl %1, %0						\n\t"
		HLP(40)
		"addl %1, %0						\n\t"
		HLP(48)
		"addl %1, %0						\n\t"
		HLP(56)

		"popl %0\n\t"
		:
		: "r" (dst), "r" (stride)
		: "%eax", "%ebx"
	);

#else
	uint8_t *temp= tempBlock;
	for(int y=0; y<BLOCK_SIZE; y++)
	{
		const int first= ABS(dst[-1] - dst[0]) < QP ? dst[-1] : dst[0];
		const int last= ABS(dst[8] - dst[7]) < QP ? dst[8] : dst[7];

		int sums[9];
		sums[0] = first + temp[0];
		sums[1] = temp[0] + temp[1];
		sums[2] = temp[1] + temp[2];
		sums[3] = temp[2] + temp[3];
		sums[4] = temp[3] + temp[4];
		sums[5] = temp[4] + temp[5];
		sums[6] = temp[5] + temp[6];
		sums[7] = temp[6] + temp[7];
		sums[8] = temp[7] + last;

		dst[0]= ((sums[0]<<2) + ((first + sums[2])<<1) + sums[4] + 8)>>4;
		dst[1]= ((dst[1]<<2) + (first + sums[0] + sums[3]<<1) + sums[5] + 8)>>4;
		dst[2]= ((dst[2]<<2) + (first + sums[1] + sums[4]<<1) + sums[6] + 8)>>4;
		dst[3]= ((dst[3]<<2) + (sums[2] + sums[5]<<1) + sums[0] + sums[7] + 8)>>4;
		dst[4]= ((dst[4]<<2) + (sums[3] + sums[6]<<1) + sums[1] + sums[8] + 8)>>4;
		dst[5]= ((dst[5]<<2) + (last + sums[7] + sums[4]<<1) + sums[2] + 8)>>4;
		dst[6]= ((last + dst[6]<<2) + (dst[7] + sums[5]<<1) + sums[3] + 8)>>4;
		dst[7]= ((sums[8]<<2) + (last + sums[6]<<1) + sums[4] + 8)>>4;

		dst+= stride;
		temp+= TEMP_STRIDE;
	}
#endif
}


static inline void dering(uint8_t src[], int stride, int QP)
{
//FIXME

#ifdef HAVE_MMX2X
	asm volatile(
		"leal (%0, %1), %%eax				\n\t"
		"leal (%%eax, %1, 4), %%ebx			\n\t"
//	0	1	2	3	4	5	6	7	8	9
//	%0	eax	eax+%1	eax+2%1	%0+4%1	ebx	ebx+%1	ebx+2%1	%0+8%1	ebx+4%1

		"pcmpeq %%mm6, %%mm6				\n\t"
		"pxor %%mm7, %%mm7				\n\t"

#define FIND_MIN_MAX(addr)\
		"movq (" #addr "), %%mm0,			\n\t"\
		"pminub %%mm0, %%mm6				\n\t"\
		"pmaxub %%mm0, %%mm7				\n\t"

FIND_MIN_MAX(%0)
FIND_MIN_MAX(%%eax)
FIND_MIN_MAX(%%eax, %1)
FIND_MIN_MAX(%%eax, %1, 2)
FIND_MIN_MAX(%0, %1, 4)
FIND_MIN_MAX(%%ebx)
FIND_MIN_MAX(%%ebx, %1)
FIND_MIN_MAX(%%ebx, %1, 2)
FIND_MIN_MAX(%0, %1, 8)
FIND_MIN_MAX(%%ebx, %1, 2)

		"movq %%mm6, %%mm4				\n\t"
		"psrlq $32, %%mm6				\n\t"
		"pminub %%mm4, %%mm6				\n\t"
		"movq %%mm6, %%mm4				\n\t"
		"psrlq $16, %%mm6				\n\t"
		"pminub %%mm4, %%mm6				\n\t"
		"movq %%mm6, %%mm4				\n\t"
		"psrlq $8, %%mm6				\n\t"
		"pminub %%mm4, %%mm6				\n\t" // min of pixels

		"movq %%mm7, %%mm4				\n\t"
		"psrlq $32, %%mm7				\n\t"
		"pmaxub %%mm4, %%mm7				\n\t"
		"movq %%mm7, %%mm4				\n\t"
		"psrlq $16, %%mm7				\n\t"
		"pmaxub %%mm4, %%mm7				\n\t"
		"movq %%mm7, %%mm4				\n\t"
		"psrlq $8, %%mm7				\n\t"
		"pmaxub %%mm4, %%mm7				\n\t" // max of pixels
		PAVGB(%%mm6, %%mm7)				      // (max + min)/2


		: : "r" (src), "r" (stride), "r" (QP)
		: "%eax", "%ebx"
	);
#else

//FIXME
#endif
}




/**
 * ...
 * the mode value is interpreted as a quality value if its negative, its range is then (-1 ... -63)
 * -63 is best quality -1 is worst
 */
extern "C"{
void  postprocess(unsigned char * src[], int src_stride,
                 unsigned char * dst[], int dst_stride,
                 int horizontal_size,   int vertical_size,
                 QP_STORE_T *QP_store,  int QP_stride,
					  int mode)
{

	if(mode<0) mode= getModeForQuality(-mode);

/*
	long long T= rdtsc();
	for(int y=vertical_size-1; y>=0 ; y--)
		memcpy(dst[0] + y*src_stride, src[0] + y*src_stride,src_stride);
//	memcpy(dst[0], src[0],src_stride*vertical_size);
	printf("%4dk\r", (rdtsc()-T)/1000);

	return;
*/
/*
	long long T= rdtsc();
	while( (rdtsc() - T)/1000 < 4000);

	return;
*/
	postProcess(src[0], src_stride, dst[0], dst_stride,
		horizontal_size, vertical_size, QP_store, QP_stride, false, mode);

	horizontal_size >>= 1;
	vertical_size   >>= 1;
	src_stride      >>= 1;
	dst_stride      >>= 1;

	if(1)
	{
		postProcess(src[1], src_stride, dst[1], dst_stride,
			horizontal_size, vertical_size, QP_store, QP_stride, true, mode >>4);
		postProcess(src[2], src_stride, dst[2], dst_stride,
			horizontal_size, vertical_size, QP_store, QP_stride, true, mode >>4);
	}
	else
	{
		memcpy(dst[1], src[1], src_stride*horizontal_size);
		memcpy(dst[2], src[2], src_stride*horizontal_size);
	}
}
/**
 * gets the mode flags for a given quality (larger values mean slower but better postprocessing)
 * 0 <= quality < 64
 */
int getModeForQuality(int quality){
	int modes[6]= {
		LUM_V_DEBLOCK,
		LUM_V_DEBLOCK | LUM_H_DEBLOCK,
		LUM_V_DEBLOCK | LUM_H_DEBLOCK | CHROM_V_DEBLOCK,
		LUM_V_DEBLOCK | LUM_H_DEBLOCK | CHROM_V_DEBLOCK | CHROM_H_DEBLOCK,
		LUM_V_DEBLOCK | LUM_H_DEBLOCK | CHROM_V_DEBLOCK | CHROM_H_DEBLOCK | LUM_DERING,
		LUM_V_DEBLOCK | LUM_H_DEBLOCK | CHROM_V_DEBLOCK | CHROM_H_DEBLOCK | LUM_DERING | CHROM_DERING
		};

	return modes[ (quality*6) >>6 ];
}

} // extern "C"

/**
 * Copies a block from src to dst and fixes the blacklevel
 */
static inline void blockCopy(uint8_t dst[], int dstStride, uint8_t src[], int srcStride)
{
#ifdef HAVE_MMX
					asm volatile(
						"pushl %0 \n\t"
						"pushl %1 \n\t"
						"leal (%2,%2), %%eax	\n\t"
						"leal (%3,%3), %%ebx	\n\t"
						"movq packedYOffset, %%mm2	\n\t"
						"movq packedYScale, %%mm3	\n\t"

#define SIMPLE_CPY					\
						"movq (%0), %%mm0	\n\t"\
						"movq (%0,%2), %%mm1	\n\t"\
						"psubusb %%mm2, %%mm0	\n\t"\
						"psubusb %%mm2, %%mm1	\n\t"\
						"movq %%mm0, (%1)	\n\t"\
						"movq %%mm1, (%1, %3)	\n\t"\

#define SCALED_CPY					\
						"movq (%0), %%mm0	\n\t"\
						"movq (%0,%2), %%mm1	\n\t"\
						"psubusb %%mm2, %%mm0	\n\t"\
						"psubusb %%mm2, %%mm1	\n\t"\
						"pxor %%mm4, %%mm4	\n\t"\
						"pxor %%mm5, %%mm5	\n\t"\
						"punpcklbw %%mm0, %%mm4 \n\t"\
						"punpckhbw %%mm0, %%mm5 \n\t"\
						"pmulhuw %%mm3, %%mm4	\n\t"\
						"pmulhuw %%mm3, %%mm5	\n\t"\
						"packuswb %%mm5, %%mm4	\n\t"\
						"movq %%mm4, (%1)	\n\t"\
						"pxor %%mm4, %%mm4	\n\t"\
						"pxor %%mm5, %%mm5	\n\t"\
						"punpcklbw %%mm1, %%mm4 \n\t"\
						"punpckhbw %%mm1, %%mm5 \n\t"\
						"pmulhuw %%mm3, %%mm4	\n\t"\
						"pmulhuw %%mm3, %%mm5	\n\t"\
						"packuswb %%mm5, %%mm4	\n\t"\
						"movq %%mm4, (%1, %3)	\n\t"\


#define CPY SCALED_CPY
//#define CPY SIMPLE_CPY
//				"prefetchnta 8(%0)\n\t"
CPY
						"addl %%eax, %0		\n\t"
						"addl %%ebx, %1		\n\t"
CPY
						"addl %%eax, %0		\n\t"
						"addl %%ebx, %1		\n\t"
CPY
						"addl %%eax, %0		\n\t"
						"addl %%ebx, %1		\n\t"
CPY
						"popl %1 \n\t"
						"popl %0 \n\t"
						: : "r" (src),
						"r" (dst),
						"r" (srcStride),
						"r" (dstStride)
						: "%eax", "%ebx"
					);
#else
				for(int i=0; i<BLOCK_SIZE; i++) // last 10x8 Block is copied allready so +2
					memcpy(	&(dst[dstStride*i]),
						&(src[srcStride*i]), BLOCK_SIZE);
#endif
}


/**
 * Filters array of bytes (Y or U or V values)
 */
void postProcess(uint8_t src[], int srcStride, uint8_t dst[], int dstStride, int width, int height,
	QP_STORE_T QPs[], int QPStride, bool isColor, int mode)
{

#ifdef TIMEING
	long long T0, T1, memcpyTime=0, vertTime=0, horizTime=0, sumTime, diffTime=0;
	sumTime= rdtsc();
#endif

	/* we need 64bit here otherwise we´ll going to have a problem
	   after watching a black picture for 5 hours*/
	static uint64_t *yHistogram= NULL;
	if(!yHistogram)
	{
		yHistogram= new uint64_t[256];
		for(int i=0; i<256; i++) yHistogram[i]= width*height/64/256;
	}

	int black=0, white=255; // blackest black and whitest white in the picture
	if(!isColor)
	{
		uint64_t sum= 0;
		for(int i=0; i<256; i++)
			sum+= yHistogram[i];

		uint64_t maxClipped= (uint64_t)(sum * maxClippedThreshold);

		uint64_t clipped= sum;
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

		// we cant handle negative correctures
		packedYOffset= MAX(black - minAllowedY, 0);
		packedYOffset|= packedYOffset<<32;
		packedYOffset|= packedYOffset<<16;
		packedYOffset|= packedYOffset<<8;

		double scale= (double)(maxAllowedY - minAllowedY) / (double)(white-black);

		packedYScale= uint16_t(scale*256.0 + 0.5);
		packedYScale|= packedYScale<<32;
		packedYScale|= packedYScale<<16;
	}
	else
	{
		packedYScale= 0x0100010001000100LL;
		packedYOffset= 0;
	}

	for(int x=0; x<width; x+=BLOCK_SIZE)
		blockCopy(dst + x, dstStride, src + x, srcStride);

	for(int y=0; y<height; y+=BLOCK_SIZE)
	{
		//1% speedup if these are here instead of the inner loop
		uint8_t *srcBlock= &(src[y*srcStride]);
		uint8_t *dstBlock= &(dst[y*dstStride]);
		uint8_t *vertSrcBlock= &(srcBlock[srcStride*3]); // Blocks are 10x8 -> *3 to start
		uint8_t *vertBlock= &(dstBlock[dstStride*3]);

		// finish 1 block before the next otherwise we´ll might have a problem
		// with the L1 Cache of the P4 ... or only a few blocks at a time or soemthing
		for(int x=0; x<width; x+=BLOCK_SIZE)
		{
			int QP= isColor ?
				QPs[(y>>3)*QPStride + (x>>3)]:
				(QPs[(y>>4)*QPStride + (x>>4)] * (packedYScale &0xFFFF))>>8;
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


			const int stride= dstStride;
			if(y + 12 < height)
			{
#ifdef MORE_TIMEING
				T0= rdtsc();
#endif

#ifdef HAVE_MMX2
				prefetchnta(vertSrcBlock + (((x>>3)&3) + 2)*srcStride + 32);
				prefetchnta(vertSrcBlock + (((x>>3)&3) + 6)*srcStride + 32);
				prefetcht0(vertBlock + (((x>>3)&3) + 2)*dstStride + 32);
				prefetcht0(vertBlock + (((x>>3)&3) + 6)*dstStride + 32);
#elif defined(HAVE_3DNOW)
//FIXME check if this is faster on an 3dnow chip or if its faster without the prefetch or ...
/*				prefetch(vertSrcBlock + (((x>>3)&3) + 2)*srcStride + 32);
				prefetch(vertSrcBlock + (((x>>3)&3) + 6)*srcStride + 32);
				prefetchw(vertBlock + (((x>>3)&3) + 2)*dstStride + 32);
				prefetchw(vertBlock + (((x>>3)&3) + 6)*dstStride + 32);
*/
#endif
				if(!isColor) yHistogram[ srcBlock[0] ]++;

				blockCopy(vertBlock + dstStride*2, dstStride,
					vertSrcBlock + srcStride*2, srcStride);


#ifdef MORE_TIMEING
				T1= rdtsc();
				memcpyTime+= T1-T0;
				T0=T1;
#endif
				if(mode & V_DEBLOCK)
				{
					if(mode & RK_FILTER)
						vertRKFilter(vertBlock, stride, QP);
					else if(0)
						vertX1Filter(vertBlock, stride, QP);
					else
					{
						if( isVertDC(vertBlock, stride))
						{
							if(isVertMinMaxOk(vertBlock, stride, QP))
								doVertLowPass(vertBlock, stride, QP);
						}
						else
							doVertDefFilter(vertBlock, stride, QP);
					}
				}
#ifdef MORE_TIMEING
				T1= rdtsc();
				vertTime+= T1-T0;
				T0=T1;
#endif
			}
			else
			{
				for(int i=2; i<BLOCK_SIZE/2+1; i++) // last 10x8 Block is copied allready so +2
					memcpy(	&(vertBlock[dstStride*i]),
						&(vertSrcBlock[srcStride*i]), BLOCK_SIZE);

			}

			if(x - 8 >= 0 && x<width)
			{
#ifdef MORE_TIMEING
				T0= rdtsc();
#endif
				if(mode & H_DEBLOCK)
				{
					if( isHorizDCAndCopy2Temp(dstBlock-4, stride))
					{
						if(isHorizMinMaxOk(tempBlock, TEMP_STRIDE, QP))
							doHorizLowPassAndCopyBack(dstBlock-4, stride, QP);
					}
					else
						doHorizDefFilterAndCopyBack(dstBlock-4, stride, QP);
				}
#ifdef MORE_TIMEING
				T1= rdtsc();
				horizTime+= T1-T0;
				T0=T1;
#endif
				dering(dstBlock - 9 - stride, stride, QP);
			}
			else if(y!=0)
				dering(dstBlock - stride*9 + width-9, stride, QP);
			//FIXME dering filter will not be applied to last block (bottom right)


			dstBlock+=8;
			srcBlock+=8;
			vertBlock+=8;
			vertSrcBlock+=8;
		}
	}
#ifdef HAVE_3DNOW
	asm volatile("femms");
#elif defined (HAVE_MMX)
	asm volatile("emms");
#endif

#ifdef TIMEING
	// FIXME diff is mostly the time spent for rdtsc (should subtract that but ...)
	sumTime= rdtsc() - sumTime;
	if(!isColor)
		printf("cpy:%4dk, vert:%4dk, horiz:%4dk, sum:%4dk, diff:%4dk, color: %d/%d    \r",
			int(memcpyTime/1000), int(vertTime/1000), int(horizTime/1000),
			int(sumTime/1000), int((sumTime-memcpyTime-vertTime-horizTime)/1000)
			, black, white);
#endif
}


