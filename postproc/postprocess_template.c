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

#undef PAVGB
#undef PMINUB
#undef PMAXUB

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


//FIXME? |255-0| = 1 (shouldnt be a problem ...)
/**
 * Check if the middle 8x8 Block in the given 8x16 block is flat
 */
static inline int RENAME(isVertDC)(uint8_t src[], int stride){
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
		"movq "MANGLE(mmxDCOffset)", %%mm7		\n\t" // mm7 = 0x7F
		"movq "MANGLE(mmxDCThreshold)", %%mm6		\n\t" // mm6 = 0x7D
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
#ifdef HAVE_MMX2
		"pxor %%mm7, %%mm7				\n\t"
		"psadbw %%mm7, %%mm0				\n\t"
#else
		"movq %%mm0, %%mm1				\n\t"
		"psrlw $8, %%mm0				\n\t"
		"paddb %%mm1, %%mm0				\n\t"
		"movq %%mm0, %%mm1				\n\t"
		"psrlq $16, %%mm0				\n\t"
		"paddb %%mm1, %%mm0				\n\t"
		"movq %%mm0, %%mm1				\n\t"
		"psrlq $32, %%mm0				\n\t"
		"paddb %%mm1, %%mm0				\n\t"
#endif
		"movd %%mm0, %0					\n\t"
		: "=r" (numEq)
		: "r" (src), "r" (stride)
		: "%ebx"
		);
	numEq= (-numEq) &0xFF;

#else
	for(y=0; y<BLOCK_SIZE-1; y++)
	{
		if(((src[0] - src[0+stride] + dcOffset)&0xFFFF) < dcThreshold) numEq++;
		if(((src[1] - src[1+stride] + dcOffset)&0xFFFF) < dcThreshold) numEq++;
		if(((src[2] - src[2+stride] + dcOffset)&0xFFFF) < dcThreshold) numEq++;
		if(((src[3] - src[3+stride] + dcOffset)&0xFFFF) < dcThreshold) numEq++;
		if(((src[4] - src[4+stride] + dcOffset)&0xFFFF) < dcThreshold) numEq++;
		if(((src[5] - src[5+stride] + dcOffset)&0xFFFF) < dcThreshold) numEq++;
		if(((src[6] - src[6+stride] + dcOffset)&0xFFFF) < dcThreshold) numEq++;
		if(((src[7] - src[7+stride] + dcOffset)&0xFFFF) < dcThreshold) numEq++;
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

static inline int RENAME(isVertMinMaxOk)(uint8_t src[], int stride, int QP)
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

		"movq "MANGLE(pQPb)", %%mm7			\n\t" // QP,..., QP
		"paddusb %%mm7, %%mm7				\n\t" // 2QP ... 2QP
		"psubusb %%mm7, %%mm0				\n\t" // Diff <= 2QP -> 0
		"pcmpeqd "MANGLE(b00)", %%mm0			\n\t"
		"psrlq $16, %%mm0				\n\t"
		"pcmpeqd "MANGLE(bFF)", %%mm0			\n\t"
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
static inline void RENAME(doVertLowPass)(uint8_t *src, int stride, int QP)
{
#if defined (HAVE_MMX2) || defined (HAVE_3DNOW)
	src+= stride*3;
	asm volatile(	//"movv %0 %1 %2\n\t"
		"movq "MANGLE(pQPb)", %%mm0			\n\t"  // QP,..., QP

		"movq (%0), %%mm6				\n\t"
		"movq (%0, %1), %%mm5				\n\t"
		"movq %%mm5, %%mm1				\n\t"
		"movq %%mm6, %%mm2				\n\t"
		"psubusb %%mm6, %%mm5				\n\t"
		"psubusb %%mm1, %%mm2				\n\t"
		"por %%mm5, %%mm2				\n\t" // ABS Diff of lines
		"psubusb %%mm0, %%mm2				\n\t" // diff <= QP -> 0
		"pcmpeqb "MANGLE(b00)", %%mm2			\n\t" // diff <= QP -> FF

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
		"pcmpeqb "MANGLE(b00)", %%mm2			\n\t" // diff <= QP -> FF

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
static inline void RENAME(vertRK1Filter)(uint8_t *src, int stride, int QP)
{
#if defined (HAVE_MMX2) || defined (HAVE_3DNOW)
	src+= stride*3;
// FIXME rounding
	asm volatile(
		"pxor %%mm7, %%mm7				\n\t" // 0
		"movq "MANGLE(b80)", %%mm6			\n\t" // MIN_SIGNED_BYTE
		"leal (%0, %1), %%eax				\n\t"
		"leal (%%eax, %1, 4), %%ebx			\n\t"
//	0	1	2	3	4	5	6	7	8	9
//	%0	eax	eax+%1	eax+2%1	%0+4%1	ebx	ebx+%1	ebx+2%1	%0+8%1	ebx+4%1
		"movq "MANGLE(pQPb)", %%mm0			\n\t" // QP,..., QP
		"movq %%mm0, %%mm1				\n\t" // QP,..., QP
		"paddusb "MANGLE(b02)", %%mm0			\n\t"
		"psrlw $2, %%mm0				\n\t"
		"pand "MANGLE(b3F)", %%mm0			\n\t" // QP/4,..., QP/4
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
		"pand "MANGLE(b3F)", %%mm5			\n\t"
		"psubb "MANGLE(b20)", %%mm5			\n\t" // (l5-l4)/8

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
static inline void RENAME(vertX1Filter)(uint8_t *src, int stride, int QP)
{
#if defined (HAVE_MMX2) || defined (HAVE_3DNOW)
	src+= stride*3;

	asm volatile(
		"pxor %%mm7, %%mm7				\n\t" // 0
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
		"movq (%%ebx, %1), %%mm4			\n\t" // line 6
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
		"movq "MANGLE(pQPb)", %%mm0			\n\t"
                "paddusb %%mm0, %%mm0				\n\t"
		"psubusb %%mm0, %%mm4				\n\t"
		"pcmpeqb %%mm7, %%mm4				\n\t" // d <= QP ? -1 : 0
		"psubusb "MANGLE(b01)", %%mm3			\n\t"
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

		if(d < QP*2)
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

static inline void RENAME(doVertDefFilter)(uint8_t src[], int stride, int QP)
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
		"movq "MANGLE(pQPb)", %%mm4			\n\t" // QP //FIXME QP+1 ?
		"paddusb "MANGLE(b01)", %%mm4			\n\t"
		"pcmpgtb %%mm3, %%mm4				\n\t" // |menergy|/8 < QP
		"psubusb %%mm1, %%mm3				\n\t" // d=|menergy|/8-MIN(|lenergy|,|renergy|)/8
		"pand %%mm4, %%mm3				\n\t"

		"movq %%mm3, %%mm1				\n\t"
//		"psubusb "MANGLE(b01)", %%mm3			\n\t"
		PAVGB(%%mm7, %%mm3)
		PAVGB(%%mm7, %%mm3)
		"paddusb %%mm1, %%mm3				\n\t"
//		"paddusb "MANGLE(b01)", %%mm3			\n\t"

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

		"psubusb "MANGLE(b01)", %%mm3			\n\t"
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
		"movq "MANGLE(b80)", %%mm4			\n\t" // 128
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
		"movq "MANGLE(b80)", %%mm3			\n\t" // 128
		PAVGB(%%mm2, %%mm3)				      // ~(l2-l1)/4 + 128
		PAVGB(%%mm1, %%mm3)				      // ~(l0-l3)/4 +(l2-l1)/8 + 128
		PAVGB(%%mm2, %%mm3)				      // ~(l0-l3)/8 +5(l2-l1)/16 + 128
// mm0=128-q, mm3=lenergy/16 + 128, mm4= menergy/16 + 128, mm5= -l5-1

		PAVGB((%%ebx, %1), %%mm5)			      // (l6-l5+256)/2
		"movq (%%ebx, %1, 2), %%mm1			\n\t" // l7
		"pxor %%mm6, %%mm1				\n\t" // -l7-1
		PAVGB((%0, %1, 4), %%mm1)			      // (l4-l7+256)/2
		"movq "MANGLE(b80)", %%mm2			\n\t" // 128
		PAVGB(%%mm5, %%mm2)				      // ~(l6-l5)/4 + 128
		PAVGB(%%mm1, %%mm2)				      // ~(l4-l7)/4 +(l6-l5)/8 + 128
		PAVGB(%%mm5, %%mm2)				      // ~(l4-l7)/8 +5(l6-l5)/16 + 128
// mm0=128-q, mm2=renergy/16 + 128, mm3=lenergy/16 + 128, mm4= menergy/16 + 128

		"movq "MANGLE(b00)", %%mm1			\n\t" // 0
		"movq "MANGLE(b00)", %%mm5			\n\t" // 0
		"psubb %%mm2, %%mm1				\n\t" // 128 - renergy/16
		"psubb %%mm3, %%mm5				\n\t" // 128 - lenergy/16
		PMAXUB(%%mm1, %%mm2)				      // 128 + |renergy/16|
 		PMAXUB(%%mm5, %%mm3)				      // 128 + |lenergy/16|
		PMINUB(%%mm2, %%mm3, %%mm1)			      // 128 + MIN(|lenergy|,|renergy|)/16

// mm0=128-q, mm3=128 + MIN(|lenergy|,|renergy|)/16, mm4= menergy/16 + 128

		"movq "MANGLE(b00)", %%mm7			\n\t" // 0
		"movq "MANGLE(pQPb)", %%mm2			\n\t" // QP
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
		"psubusb "MANGLE(b01)", %%mm4			\n\t"
		PAVGB(%%mm7, %%mm4)				      // d/32
		PAVGB(%%mm7, %%mm4)				      // (d + 32)/64
		"paddb %%mm3, %%mm4				\n\t" // 5d/64
		"pand %%mm2, %%mm4				\n\t"

		"movq "MANGLE(b80)", %%mm5			\n\t" // 128
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
		"movq %%mm0, "MANGLE(temp0)"			\n\t" // 2L0 - 5L1 + 5L2 - 2L3
		"movq %%mm1, "MANGLE(temp1)"			\n\t" // 2H0 - 5H1 + 5H2 - 2H3

		"movq (%0, %1, 4), %%mm0			\n\t"
		"movq %%mm0, %%mm1				\n\t"
		"punpcklbw %%mm7, %%mm0				\n\t" // L4
		"punpckhbw %%mm7, %%mm1				\n\t" // H4

		"psubw %%mm0, %%mm2				\n\t" // L3 - L4
		"psubw %%mm1, %%mm3				\n\t" // H3 - H4
		"movq %%mm2, "MANGLE(temp2)"			\n\t" // L3 - L4
		"movq %%mm3, "MANGLE(temp3)"			\n\t" // H3 - H4
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

		"movq "MANGLE(temp0)", %%mm2			\n\t" // 2L0 - 5L1 + 5L2 - 2L3
		"movq "MANGLE(temp1)", %%mm3			\n\t" // 2H0 - 5H1 + 5H2 - 2H3

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


		"movq "MANGLE(w05)", %%mm2			\n\t" // 5
		"pmullw %%mm2, %%mm4				\n\t"
		"pmullw %%mm2, %%mm5				\n\t"
		"movq "MANGLE(w20)", %%mm2			\n\t" // 32
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

		"movq "MANGLE(temp2)", %%mm0			\n\t" // L3 - L4
		"movq "MANGLE(temp3)", %%mm1			\n\t" // H3 - H4

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

static inline void RENAME(dering)(uint8_t src[], int stride, int QP)
{
#if defined (HAVE_MMX2) || defined (HAVE_3DNOW)
	asm volatile(
		"movq "MANGLE(pQPb)", %%mm0			\n\t"
		"paddusb %%mm0, %%mm0				\n\t"
		"movq %%mm0, "MANGLE(pQPb2)"			\n\t"

		"leal (%0, %1), %%eax				\n\t"
		"leal (%%eax, %1, 4), %%ebx			\n\t"
//	0	1	2	3	4	5	6	7	8	9
//	%0	eax	eax+%1	eax+2%1	%0+4%1	ebx	ebx+%1	ebx+2%1	%0+8%1	ebx+4%1

		"pcmpeqb %%mm7, %%mm7				\n\t"
		"pxor %%mm6, %%mm6				\n\t"
#undef FIND_MIN_MAX
#ifdef HAVE_MMX2
#define FIND_MIN_MAX(addr)\
		"movq " #addr ", %%mm0				\n\t"\
		"pminub %%mm0, %%mm7				\n\t"\
		"pmaxub %%mm0, %%mm6				\n\t"
#else
#define FIND_MIN_MAX(addr)\
		"movq " #addr ", %%mm0				\n\t"\
		"movq %%mm7, %%mm1				\n\t"\
		"psubusb %%mm0, %%mm6				\n\t"\
		"paddb %%mm0, %%mm6				\n\t"\
		"psubusb %%mm0, %%mm1				\n\t"\
		"psubb %%mm1, %%mm7				\n\t"
#endif

FIND_MIN_MAX((%%eax))
FIND_MIN_MAX((%%eax, %1))
FIND_MIN_MAX((%%eax, %1, 2))
FIND_MIN_MAX((%0, %1, 4))
FIND_MIN_MAX((%%ebx))
FIND_MIN_MAX((%%ebx, %1))
FIND_MIN_MAX((%%ebx, %1, 2))
FIND_MIN_MAX((%0, %1, 8))

		"movq %%mm7, %%mm4				\n\t"
		"psrlq $8, %%mm7				\n\t"
#ifdef HAVE_MMX2
		"pminub %%mm4, %%mm7				\n\t" // min of pixels
		"pshufw $0xF9, %%mm7, %%mm4			\n\t"
		"pminub %%mm4, %%mm7				\n\t" // min of pixels
		"pshufw $0xFE, %%mm7, %%mm4			\n\t"
		"pminub %%mm4, %%mm7				\n\t"
#else
		"movq %%mm7, %%mm1				\n\t"
		"psubusb %%mm4, %%mm1				\n\t"
		"psubb %%mm1, %%mm7				\n\t"
		"movq %%mm7, %%mm4				\n\t"
		"psrlq $16, %%mm7				\n\t"
		"movq %%mm7, %%mm1				\n\t"
		"psubusb %%mm4, %%mm1				\n\t"
		"psubb %%mm1, %%mm7				\n\t"
		"movq %%mm7, %%mm4				\n\t"
		"psrlq $32, %%mm7				\n\t"
		"movq %%mm7, %%mm1				\n\t"
		"psubusb %%mm4, %%mm1				\n\t"
		"psubb %%mm1, %%mm7				\n\t"
#endif


		"movq %%mm6, %%mm4				\n\t"
		"psrlq $8, %%mm6				\n\t"
#ifdef HAVE_MMX2
		"pmaxub %%mm4, %%mm6				\n\t" // max of pixels
		"pshufw $0xF9, %%mm6, %%mm4			\n\t"
		"pmaxub %%mm4, %%mm6				\n\t"
		"pshufw $0xFE, %%mm6, %%mm4			\n\t"
		"pmaxub %%mm4, %%mm6				\n\t"
#else
		"psubusb %%mm4, %%mm6				\n\t"
		"paddb %%mm4, %%mm6				\n\t"
		"movq %%mm6, %%mm4				\n\t"
		"psrlq $16, %%mm6				\n\t"
		"psubusb %%mm4, %%mm6				\n\t"
		"paddb %%mm4, %%mm6				\n\t"
		"movq %%mm6, %%mm4				\n\t"
		"psrlq $32, %%mm6				\n\t"
		"psubusb %%mm4, %%mm6				\n\t"
		"paddb %%mm4, %%mm6				\n\t"
#endif
		"movq %%mm6, %%mm0				\n\t" // max
		"psubb %%mm7, %%mm6				\n\t" // max - min
		"movd %%mm6, %%ecx				\n\t"
		"cmpb "MANGLE(deringThreshold)", %%cl		\n\t"
		" jb 1f						\n\t"
		PAVGB(%%mm0, %%mm7)				      // a=(max + min)/2
		"punpcklbw %%mm7, %%mm7				\n\t"
		"punpcklbw %%mm7, %%mm7				\n\t"
		"punpcklbw %%mm7, %%mm7				\n\t"
		"movq %%mm7, "MANGLE(temp0)"			\n\t"

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
		"pcmpeqb "MANGLE(b00)", %%mm0			\n\t" // L10 > a ? 0 : -1
		"pcmpeqb "MANGLE(b00)", %%mm2			\n\t" // L20 > a ? 0 : -1
		"pcmpeqb "MANGLE(b00)", %%mm3			\n\t" // L00 > a ? 0 : -1
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
		"pcmpeqb "MANGLE(b00)", %%mm2			\n\t" // L11 > a ? 0 : -1
		"pcmpeqb "MANGLE(b00)", %%mm4			\n\t" // L21 > a ? 0 : -1
		"pcmpeqb "MANGLE(b00)", %%mm5			\n\t" // L01 > a ? 0 : -1
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
		"movq " #lx ", "MANGLE(temp1)"			\n\t"\
		"movq "MANGLE(temp0)", " #lx "			\n\t"\
		"psubusb " #lx ", " #t1 "			\n\t"\
		"psubusb " #lx ", " #t0 "			\n\t"\
		"psubusb " #lx ", " #sx "			\n\t"\
		"movq "MANGLE(b00)", " #lx "			\n\t"\
		"pcmpeqb " #lx ", " #t1 "			\n\t" /* src[-1] > a ? 0 : -1*/\
		"pcmpeqb " #lx ", " #t0 "			\n\t" /* src[+1] > a ? 0 : -1*/\
		"pcmpeqb " #lx ", " #sx "			\n\t" /* src[0]  > a ? 0 : -1*/\
		"paddb " #t1 ", " #t0 "				\n\t"\
		"paddb " #t0 ", " #sx "				\n\t"\
\
		PAVGB(plx, pplx)				      /* filtered */\
		"movq " #dst ", " #t0 "				\n\t" /* dst */\
		"movq " #t0 ", " #t1 "				\n\t" /* dst */\
		"psubusb "MANGLE(pQPb2)", " #t0 "		\n\t"\
		"paddusb "MANGLE(pQPb2)", " #t1 "		\n\t"\
		PMAXUB(t0, pplx)\
		PMINUB(t1, pplx, t0)\
		"paddb " #sx ", " #ppsx "			\n\t"\
		"paddb " #psx ", " #ppsx "			\n\t"\
		"#paddb "MANGLE(b02)", " #ppsx "		\n\t"\
		"pand "MANGLE(b08)", " #ppsx "			\n\t"\
		"pcmpeqb " #lx ", " #ppsx "			\n\t"\
		"pand " #ppsx ", " #pplx "			\n\t"\
		"pandn " #dst ", " #ppsx "			\n\t"\
		"por " #pplx ", " #ppsx "			\n\t"\
		"movq " #ppsx ", " #dst "			\n\t"\
		"movq "MANGLE(temp1)", " #lx "			\n\t"

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

		"1:			\n\t"
		: : "r" (src), "r" (stride), "r" (QP)
		: "%eax", "%ebx", "%ecx"
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

	if(max - min <deringThreshold) return;

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

#ifdef DEBUG_DERING_THRESHOLD
				asm volatile("emms\n\t":);
				{
				static long long numPixels=0;
				if(x!=1 && x!=8 && y!=1 && y!=8) numPixels++;
//				if((max-min)<20 || (max-min)*QP<200)
//				if((max-min)*QP < 500)
//				if(max-min<QP/2)
				if(max-min < 20)
				{
					static int numSkiped=0;
					static int errorSum=0;
					static int worstQP=0;
					static int worstRange=0;
					static int worstDiff=0;
					int diff= (f - *p);
					int absDiff= ABS(diff);
					int error= diff*diff;

					if(x==1 || x==8 || y==1 || y==8) continue;

					numSkiped++;
					if(absDiff > worstDiff)
					{
						worstDiff= absDiff;
						worstQP= QP;
						worstRange= max-min;
					}
					errorSum+= error;

					if(1024LL*1024LL*1024LL % numSkiped == 0)
					{
						printf( "sum:%1.3f, skip:%d, wQP:%d, "
							"wRange:%d, wDiff:%d, relSkip:%1.3f\n",
							(float)errorSum/numSkiped, numSkiped, worstQP, worstRange,
							worstDiff, (float)numSkiped/numPixels);
					}
				}
				}
#endif
				if     (*p + 2*QP < f) *p= *p + 2*QP;
				else if(*p - 2*QP > f) *p= *p - 2*QP;
				else *p=f;
			}
		}
	}
#ifdef DEBUG_DERING_THRESHOLD
	if(max-min < 20)
	{
		for(y=1; y<9; y++)
		{
			int x;
			int t = 0;
			p= src + stride*y;
			for(x=1; x<9; x++)
			{
				p++;
				*p = MIN(*p + 20, 255);
			}
		}
//		src[0] = src[7]=src[stride*7]=src[stride*7 + 7]=255;
	}
#endif
#endif
}

/**
 * Deinterlaces the given block
 * will be called for every 8x8 block and can read & write from line 4-15
 * lines 0-3 have been passed through the deblock / dering filters allready, but can be read too
 * lines 4-12 will be read into the deblocking filter and should be deinterlaced
 */
static inline void RENAME(deInterlaceInterpolateLinear)(uint8_t src[], int stride)
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
static inline void RENAME(deInterlaceInterpolateCubic)(uint8_t src[], int stride)
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
static inline void RENAME(deInterlaceBlendLinear)(uint8_t src[], int stride)
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
static inline void RENAME(deInterlaceMedian)(uint8_t src[], int stride)
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
static inline void RENAME(transpose1)(uint8_t *dst1, uint8_t *dst2, uint8_t *src, int srcStride)
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
static inline void RENAME(transpose2)(uint8_t *dst, int dstStride, uint8_t *src)
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

static void inline RENAME(tempNoiseReducer)(uint8_t *src, int stride,
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
		"movq "MANGLE(b80)", %%mm6			\n\t"
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

//		"movl %3, %%ecx					\n\t"
//		"movl %%ecx, test				\n\t"
//		"jmp 4f \n\t"
		"cmpl 4+"MANGLE(maxTmpNoise)", %%ecx		\n\t"
		" jb 2f						\n\t"
		"cmpl 8+"MANGLE(maxTmpNoise)", %%ecx		\n\t"
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
		PAVGB((%1), %%mm0)				      // L0
		"movq (%0, %2), %%mm1				\n\t" // L1
		PAVGB((%1, %2), %%mm1)				      // L1
		"movq (%0, %2, 2), %%mm2			\n\t" // L2
		PAVGB((%1, %2, 2), %%mm2)			      // L2
		"movq (%0, %%eax), %%mm3			\n\t" // L3
		PAVGB((%1, %%eax), %%mm3)			      // L3
		"movq (%0, %2, 4), %%mm4			\n\t" // L4
		PAVGB((%1, %2, 4), %%mm4)			      // L4
		"movq (%0, %%ebx), %%mm5			\n\t" // L5
		PAVGB((%1, %%ebx), %%mm5)			      // L5
		"movq (%0, %%eax, 2), %%mm6			\n\t" // L6
		PAVGB((%1, %%eax, 2), %%mm6)			      // L6
		"movq (%0, %%ecx), %%mm7			\n\t" // L7
		PAVGB((%1, %%ecx), %%mm7)			      // L7
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
		"cmpl "MANGLE(maxTmpNoise)", %%ecx		\n\t"
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

static void RENAME(postProcess)(uint8_t src[], int srcStride, uint8_t dst[], int dstStride, int width, int height,
	QP_STORE_T QPs[], int QPStride, int isColor, struct PPMode *ppMode);

/**
 * Copies a block from src to dst and fixes the blacklevel
 * levelFix == 0 -> dont touch the brighness & contrast
 */
static inline void RENAME(blockCopy)(uint8_t dst[], int dstStride, uint8_t src[], int srcStride,
	int levelFix)
{
#ifndef HAVE_MMX
	int i;
#endif
	if(levelFix)
	{
#ifdef HAVE_MMX
					asm volatile(
						"leal (%0,%2), %%eax	\n\t"
						"leal (%1,%3), %%ebx	\n\t"
						"movq "MANGLE(packedYOffset)", %%mm2\n\t"
						"movq "MANGLE(packedYScale)", %%mm3\n\t"
						"pxor %%mm4, %%mm4	\n\t"
#ifdef HAVE_MMX2
#define SCALED_CPY(src1, src2, dst1, dst2)					\
						"movq " #src1 ", %%mm0	\n\t"\
						"movq " #src1 ", %%mm5	\n\t"\
						"movq " #src2 ", %%mm1	\n\t"\
						"movq " #src2 ", %%mm6	\n\t"\
						"punpcklbw %%mm0, %%mm0 \n\t"\
						"punpckhbw %%mm5, %%mm5 \n\t"\
						"punpcklbw %%mm1, %%mm1 \n\t"\
						"punpckhbw %%mm6, %%mm6 \n\t"\
						"pmulhuw %%mm3, %%mm0	\n\t"\
						"pmulhuw %%mm3, %%mm5	\n\t"\
						"pmulhuw %%mm3, %%mm1	\n\t"\
						"pmulhuw %%mm3, %%mm6	\n\t"\
						"psubw %%mm2, %%mm0	\n\t"\
						"psubw %%mm2, %%mm5	\n\t"\
						"psubw %%mm2, %%mm1	\n\t"\
						"psubw %%mm2, %%mm6	\n\t"\
						"packuswb %%mm5, %%mm0	\n\t"\
						"packuswb %%mm6, %%mm1	\n\t"\
						"movq %%mm0, " #dst1 "	\n\t"\
						"movq %%mm1, " #dst2 "	\n\t"\

#else //HAVE_MMX2
#define SCALED_CPY(src1, src2, dst1, dst2)					\
						"movq " #src1 ", %%mm0	\n\t"\
						"movq " #src1 ", %%mm5	\n\t"\
						"punpcklbw %%mm4, %%mm0 \n\t"\
						"punpckhbw %%mm4, %%mm5 \n\t"\
						"psubw %%mm2, %%mm0	\n\t"\
						"psubw %%mm2, %%mm5	\n\t"\
						"movq " #src2 ", %%mm1	\n\t"\
						"psllw $6, %%mm0	\n\t"\
						"psllw $6, %%mm5	\n\t"\
						"pmulhw %%mm3, %%mm0	\n\t"\
						"movq " #src2 ", %%mm6	\n\t"\
						"pmulhw %%mm3, %%mm5	\n\t"\
						"punpcklbw %%mm4, %%mm1 \n\t"\
						"punpckhbw %%mm4, %%mm6 \n\t"\
						"psubw %%mm2, %%mm1	\n\t"\
						"psubw %%mm2, %%mm6	\n\t"\
						"psllw $6, %%mm1	\n\t"\
						"psllw $6, %%mm6	\n\t"\
						"pmulhw %%mm3, %%mm1	\n\t"\
						"pmulhw %%mm3, %%mm6	\n\t"\
						"packuswb %%mm5, %%mm0	\n\t"\
						"packuswb %%mm6, %%mm1	\n\t"\
						"movq %%mm0, " #dst1 "	\n\t"\
						"movq %%mm1, " #dst2 "	\n\t"\

#endif //!HAVE_MMX2

SCALED_CPY((%0)       , (%0, %2)      , (%1)       , (%1, %3))
SCALED_CPY((%0, %2, 2), (%%eax, %2, 2), (%1, %3, 2), (%%ebx, %3, 2))
SCALED_CPY((%0, %2, 4), (%%eax, %2, 4), (%1, %3, 4), (%%ebx, %3, 4))
						"leal (%%eax,%2,4), %%eax	\n\t"
						"leal (%%ebx,%3,4), %%ebx	\n\t"
SCALED_CPY((%%eax, %2), (%%eax, %2, 2), (%%ebx, %3), (%%ebx, %3, 2))


						: : "r"(src),
						"r"(dst),
						"r" (srcStride),
						"r" (dstStride)
						: "%eax", "%ebx"
					);
#else
				for(i=0; i<8; i++)
					memcpy(	&(dst[dstStride*i]),
						&(src[srcStride*i]), BLOCK_SIZE);
#endif
	}
	else
	{
#ifdef HAVE_MMX
					asm volatile(
						"leal (%0,%2), %%eax	\n\t"
						"leal (%1,%3), %%ebx	\n\t"

#define SIMPLE_CPY(src1, src2, dst1, dst2)				\
						"movq " #src1 ", %%mm0	\n\t"\
						"movq " #src2 ", %%mm1	\n\t"\
						"movq %%mm0, " #dst1 "	\n\t"\
						"movq %%mm1, " #dst2 "	\n\t"\

SIMPLE_CPY((%0)       , (%0, %2)      , (%1)       , (%1, %3))
SIMPLE_CPY((%0, %2, 2), (%%eax, %2, 2), (%1, %3, 2), (%%ebx, %3, 2))
SIMPLE_CPY((%0, %2, 4), (%%eax, %2, 4), (%1, %3, 4), (%%ebx, %3, 4))
						"leal (%%eax,%2,4), %%eax	\n\t"
						"leal (%%ebx,%3,4), %%ebx	\n\t"
SIMPLE_CPY((%%eax, %2), (%%eax, %2, 2), (%%ebx, %3), (%%ebx, %3, 2))

						: : "r" (src),
						"r" (dst),
						"r" (srcStride),
						"r" (dstStride)
						: "%eax", "%ebx"
					);
#else
				for(i=0; i<8; i++)
					memcpy(	&(dst[dstStride*i]),
						&(src[srcStride*i]), BLOCK_SIZE);
#endif
	}
}

/**
 * Duplicates the given 8 src pixels ? times upward
 */
static inline void RENAME(duplicate)(uint8_t src[], int stride)
{
#ifdef HAVE_MMX
	asm volatile(
		"movq (%0), %%mm0		\n\t"
		"addl %1, %0			\n\t"
		"movq %%mm0, (%0)		\n\t"
		"movq %%mm0, (%0, %1)		\n\t"
		"movq %%mm0, (%0, %1, 2)	\n\t"
		: "+r" (src)
		: "r" (-stride)
	);
#else
	int i;
	uint8_t *p=src;
	for(i=0; i<3; i++)
	{
		p-= stride;
		memcpy(p, src, 8);
	}
#endif
}

/**
 * Filters array of bytes (Y or U or V values)
 */
static void RENAME(postProcess)(uint8_t src[], int srcStride, uint8_t dst[], int dstStride, int width, int height,
	QP_STORE_T QPs[], int QPStride, int isColor, struct PPMode *ppMode)
{
	int x,y;
#ifdef COMPILE_TIME_MODE
	const int mode= COMPILE_TIME_MODE;
#else
	const int mode= isColor ? ppMode->chromMode : ppMode->lumMode;
#endif
	/* we need 64bit here otherwise we´ll going to have a problem
	   after watching a black picture for 5 hours*/
	static uint64_t *yHistogram= NULL;
	int black=0, white=255; // blackest black and whitest white in the picture
	int QPCorrecture= 256*256;

	/* Temporary buffers for handling the last row(s) */
	static uint8_t *tempDst= NULL;
	static uint8_t *tempSrc= NULL;

	/* Temporary buffers for handling the last block */
	static uint8_t *tempDstBlock= NULL;
	static uint8_t *tempSrcBlock= NULL;

	/* Temporal noise reducing buffers */
	static uint8_t *tempBlured[3]= {NULL,NULL,NULL};
	static uint32_t *tempBluredPast[3]= {NULL,NULL,NULL};

	int copyAhead;

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
	dcOffset= ppMode->maxDcDiff;
	dcThreshold= ppMode->maxDcDiff*2 + 1;

#ifdef HAVE_MMX
	maxTmpNoise[0]= ppMode->maxTmpNoise[0];
	maxTmpNoise[1]= ppMode->maxTmpNoise[1];
	maxTmpNoise[2]= ppMode->maxTmpNoise[2];
	
	mmxDCOffset= 0x7F - dcOffset;
	mmxDCThreshold= 0x7F - dcThreshold;

	mmxDCOffset*= 0x0101010101010101LL;
	mmxDCThreshold*= 0x0101010101010101LL;
#endif

	if(mode & CUBIC_IPOL_DEINT_FILTER) copyAhead=16;
	else if(mode & LINEAR_BLEND_DEINT_FILTER) copyAhead=14;
	else if(   (mode & V_DEBLOCK)
		|| (mode & LINEAR_IPOL_DEINT_FILTER)
		|| (mode & MEDIAN_DEINT_FILTER)) copyAhead=13;
	else if(mode & V_X1_FILTER) copyAhead=11;
	else if(mode & V_RK1_FILTER) copyAhead=10;
	else if(mode & DERING) copyAhead=9;
	else copyAhead=8;

	copyAhead-= 8;

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
			ppMode->maxAllowedY=255;
			ppMode->minAllowedY=0;
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

		scale= (double)(ppMode->maxAllowedY - ppMode->minAllowedY) / (double)(white-black);

#ifdef HAVE_MMX2
		packedYScale= (uint16_t)(scale*256.0 + 0.5);
		packedYOffset= (((black*packedYScale)>>8) - ppMode->minAllowedY) & 0xFFFF;
#else
		packedYScale= (uint16_t)(scale*1024.0 + 0.5);
		packedYOffset= (black - ppMode->minAllowedY) & 0xFFFF;
#endif

		packedYOffset|= packedYOffset<<32;
		packedYOffset|= packedYOffset<<16;

		packedYScale|= packedYScale<<32;
		packedYScale|= packedYScale<<16;
		
		if(mode & LEVEL_FIX)	QPCorrecture= (int)(scale*256*256 + 0.5);
		else			QPCorrecture= 256*256;
	}
	else
	{
		packedYScale= 0x0100010001000100LL;
		packedYOffset= 0;
		QPCorrecture= 256*256;
	}

	/* copy & deinterlace first row of blocks */
	y=-BLOCK_SIZE;
	{
		uint8_t *srcBlock= &(src[y*srcStride]);
		uint8_t *dstBlock= tempDst + dstStride;

		// From this point on it is guranteed that we can read and write 16 lines downward
		// finish 1 block before the next otherwise we´ll might have a problem
		// with the L1 Cache of the P4 ... or only a few blocks at a time or soemthing
		for(x=0; x<width; x+=BLOCK_SIZE)
		{

#ifdef HAVE_MMX2
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
				"addl %5, %%eax			\n\t"
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
			"m" (x), "m" (copyAhead)
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

			RENAME(blockCopy)(dstBlock + dstStride*8, dstStride,
				srcBlock + srcStride*8, srcStride, mode & LEVEL_FIX);

			RENAME(duplicate)(dstBlock + dstStride*8, dstStride);

			if(mode & LINEAR_IPOL_DEINT_FILTER)
				RENAME(deInterlaceInterpolateLinear)(dstBlock, dstStride);
			else if(mode & LINEAR_BLEND_DEINT_FILTER)
				RENAME(deInterlaceBlendLinear)(dstBlock, dstStride);
			else if(mode & MEDIAN_DEINT_FILTER)
				RENAME(deInterlaceMedian)(dstBlock, dstStride);
			else if(mode & CUBIC_IPOL_DEINT_FILTER)
				RENAME(deInterlaceInterpolateCubic)(dstBlock, dstStride);
/*			else if(mode & CUBIC_BLEND_DEINT_FILTER)
				RENAME(deInterlaceBlendCubic)(dstBlock, dstStride);
*/
			dstBlock+=8;
			srcBlock+=8;
		}
		memcpy(dst, tempDst + 9*dstStride, copyAhead*dstStride );
	}

	for(y=0; y<height; y+=BLOCK_SIZE)
	{
		//1% speedup if these are here instead of the inner loop
		uint8_t *srcBlock= &(src[y*srcStride]);
		uint8_t *dstBlock= &(dst[y*dstStride]);
#ifdef HAVE_MMX
		uint8_t *tempBlock1= tempBlocks;
		uint8_t *tempBlock2= tempBlocks + 8;
#endif
#ifdef ARCH_X86
		int *QPptr= isColor ? &QPs[(y>>3)*QPStride] :&QPs[(y>>4)*QPStride];
		int QPDelta= isColor ? (-1) : 1<<31;
		int QPFrac= 1<<30;
#endif
		int QP=0;
		/* can we mess with a 8x16 block from srcBlock/dstBlock downwards and 1 line upwards
		   if not than use a temporary buffer */
		if(y+15 >= height)
		{
			int i;
			/* copy from line (copyAhead) to (copyAhead+7) of src, these will be copied with
			   blockcopy to dst later */
			memcpy(tempSrc + srcStride*copyAhead, srcBlock + srcStride*copyAhead,
				srcStride*MAX(height-y-copyAhead, 0) );

			/* duplicate last line of src to fill the void upto line (copyAhead+7) */
			for(i=MAX(height-y, 8); i<copyAhead+8; i++)
				memcpy(tempSrc + srcStride*i, src + srcStride*(height-1), srcStride);

			/* copy up to (copyAhead+1) lines of dst (line -1 to (copyAhead-1))*/
			memcpy(tempDst, dstBlock - dstStride, dstStride*MIN(height-y+1, copyAhead+1) );

			/* duplicate last line of dst to fill the void upto line (copyAhead) */
			for(i=height-y+1; i<=copyAhead; i++)
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
#ifdef HAVE_MMX
			uint8_t *tmpXchg;
#endif
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
				QP= (QP* QPCorrecture + 256*128)>>16;
				yHistogram[ srcBlock[srcStride*12 + 4] ]++;
			}
#ifdef HAVE_MMX
			asm volatile(
				"movd %0, %%mm7					\n\t"
				"packuswb %%mm7, %%mm7				\n\t" // 0, 0, 0, QP, 0, 0, 0, QP
				"packuswb %%mm7, %%mm7				\n\t" // 0,QP, 0, QP, 0,QP, 0, QP
				"packuswb %%mm7, %%mm7				\n\t" // QP,..., QP
				"movq %%mm7, "MANGLE(pQPb)"			\n\t"
				: : "r" (QP)
			);
#endif

#ifdef MORE_TIMING
			T0= rdtsc();
#endif

#ifdef HAVE_MMX2
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
				"addl %5, %%eax			\n\t"
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
			"m" (x), "m" (copyAhead)
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

			RENAME(blockCopy)(dstBlock + dstStride*copyAhead, dstStride,
				srcBlock + srcStride*copyAhead, srcStride, mode & LEVEL_FIX);

			if(mode & LINEAR_IPOL_DEINT_FILTER)
				RENAME(deInterlaceInterpolateLinear)(dstBlock, dstStride);
			else if(mode & LINEAR_BLEND_DEINT_FILTER)
				RENAME(deInterlaceBlendLinear)(dstBlock, dstStride);
			else if(mode & MEDIAN_DEINT_FILTER)
				RENAME(deInterlaceMedian)(dstBlock, dstStride);
			else if(mode & CUBIC_IPOL_DEINT_FILTER)
				RENAME(deInterlaceInterpolateCubic)(dstBlock, dstStride);
/*			else if(mode & CUBIC_BLEND_DEINT_FILTER)
				RENAME(deInterlaceBlendCubic)(dstBlock, dstStride);
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
					RENAME(vertRK1Filter)(dstBlock, stride, QP);
				else if(mode & V_X1_FILTER)
					RENAME(vertX1Filter)(dstBlock, stride, QP);
				else if(mode & V_DEBLOCK)
				{
					if( RENAME(isVertDC)(dstBlock, stride))
					{
						if(RENAME(isVertMinMaxOk)(dstBlock, stride, QP))
							RENAME(doVertLowPass)(dstBlock, stride, QP);
					}
					else
						RENAME(doVertDefFilter)(dstBlock, stride, QP);
				}
#ifdef MORE_TIMING
				T1= rdtsc();
				vertTime+= T1-T0;
				T0=T1;
#endif
			}

#ifdef HAVE_MMX
			RENAME(transpose1)(tempBlock1, tempBlock2, dstBlock, dstStride);
#endif
			/* check if we have a previous block to deblock it with dstBlock */
			if(x - 8 >= 0)
			{
#ifdef MORE_TIMING
				T0= rdtsc();
#endif
#ifdef HAVE_MMX
				if(mode & H_RK1_FILTER)
					RENAME(vertRK1Filter)(tempBlock1, 16, QP);
				else if(mode & H_X1_FILTER)
					RENAME(vertX1Filter)(tempBlock1, 16, QP);
				else if(mode & H_DEBLOCK)
				{
					if( RENAME(isVertDC)(tempBlock1, 16) )
					{
						if(RENAME(isVertMinMaxOk)(tempBlock1, 16, QP))
							RENAME(doVertLowPass)(tempBlock1, 16, QP);
					}
					else
						RENAME(doVertDefFilter)(tempBlock1, 16, QP);
				}

				RENAME(transpose2)(dstBlock-4, dstStride, tempBlock1 + 4*16);

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
					if(y>0) RENAME(dering)(dstBlock - stride - 8, stride, QP);
				}

				if(mode & TEMP_NOISE_FILTER)
				{
					RENAME(tempNoiseReducer)(dstBlock-8, stride,
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
				if(y > 0) RENAME(dering)(dstBlock - dstStride - 8, dstStride, QP);
		}

		if((mode & TEMP_NOISE_FILTER))
		{
			RENAME(tempNoiseReducer)(dstBlock-8, dstStride,
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
			volatile int i;
			i+=	+ dstBlock[x + 7*dstStride] + dstBlock[x + 8*dstStride]
				+ dstBlock[x + 9*dstStride] + dstBlock[x +10*dstStride]
				+ dstBlock[x +11*dstStride] + dstBlock[x +12*dstStride];
//				+ dstBlock[x +13*dstStride]
//				+ dstBlock[x +14*dstStride] + dstBlock[x +15*dstStride];
		}*/
	}
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
