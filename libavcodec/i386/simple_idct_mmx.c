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

#include <inttypes.h>
#include "../dsputil.h"

#define C0 23170 //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define C1 22725 //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define C2 21407 //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define C3 19266 //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define C4 16384 //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define C5 12873 //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define C6 8867 //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define C7 4520 //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5

#define ROW_SHIFT 11
#define COL_SHIFT 20 // 6

static uint64_t __attribute__((aligned(8))) wm1010= 0xFFFF0000FFFF0000ULL;
static uint64_t __attribute__((aligned(8))) d40000= 0x0000000000040000ULL;
static int16_t __attribute__((aligned(8))) temp[64];
static int16_t __attribute__((aligned(8))) coeffs[]= {
	1<<(ROW_SHIFT-1), 0, 1<<(ROW_SHIFT-1), 0,
//	1<<(COL_SHIFT-1), 0, 1<<(COL_SHIFT-1), 0,
//	0, 1<<(COL_SHIFT-1-16), 0, 1<<(COL_SHIFT-1-16),
	1<<(ROW_SHIFT-1), 1, 1<<(ROW_SHIFT-1), 0,
	// the 1 = ((1<<(COL_SHIFT-1))/C4)<<ROW_SHIFT :)
//	0, 0, 0, 0,
//	0, 0, 0, 0,

	 C4,  C2,  C4,  C2,
	 C4,  C6,  C4,  C6,
	 C1,  C3,  C1,  C3,
	 C5,  C7,  C5,  C7,

	 C4,  C6,  C4,  C6,
	-C4, -C2, -C4, -C2,
	 C3, -C7,  C3, -C7,
	-C1, -C5, -C1, -C5,

	 C4, -C6,  C4, -C6,
	-C4,  C2, -C4,  C2,
	 C5, -C1,  C5, -C1,
	 C7,  C3,  C7,  C3,

	 C4, -C2,  C4, -C2,
	 C4, -C6,  C4, -C6,
	 C7, -C5,  C7, -C5,
	 C3, -C1,  C3, -C1
	};
#if 0
static void inline idctCol (int16_t * col, int16_t *input)
{
#undef C0
#undef C1
#undef C2
#undef C3
#undef C4
#undef C5
#undef C6
#undef C7
	int a0, a1, a2, a3, b0, b1, b2, b3;
	const int C0 = 23170; //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
	const int C1 = 22725; //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
	const int C2 = 21407; //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
	const int C3 = 19266; //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
	const int C4 = 16384; //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
	const int C5 = 12873; //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
	const int C6 = 8867; //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
	const int C7 = 4520; //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
/*
	if( !(col[8*1] | col[8*2] |col[8*3] |col[8*4] |col[8*5] |col[8*6] | col[8*7])) {
		col[8*0] = col[8*1] = col[8*2] = col[8*3] = col[8*4] =
			col[8*5] = col[8*6] = col[8*7] = col[8*0]<<3;
		return;
	}*/

col[8*0] = input[8*0 + 0];
col[8*1] = input[8*2 + 0];
col[8*2] = input[8*0 + 1];
col[8*3] = input[8*2 + 1];
col[8*4] = input[8*4 + 0];
col[8*5] = input[8*6 + 0];
col[8*6] = input[8*4 + 1];
col[8*7] = input[8*6 + 1];

	a0 = C4*col[8*0] + C2*col[8*2] + C4*col[8*4] + C6*col[8*6] + (1<<(COL_SHIFT-1));
	a1 = C4*col[8*0] + C6*col[8*2] - C4*col[8*4] - C2*col[8*6] + (1<<(COL_SHIFT-1));
	a2 = C4*col[8*0] - C6*col[8*2] - C4*col[8*4] + C2*col[8*6] + (1<<(COL_SHIFT-1));
	a3 = C4*col[8*0] - C2*col[8*2] + C4*col[8*4] - C6*col[8*6] + (1<<(COL_SHIFT-1));

	b0 = C1*col[8*1] + C3*col[8*3] + C5*col[8*5] + C7*col[8*7];
	b1 = C3*col[8*1] - C7*col[8*3] - C1*col[8*5] - C5*col[8*7];
	b2 = C5*col[8*1] - C1*col[8*3] + C7*col[8*5] + C3*col[8*7];
	b3 = C7*col[8*1] - C5*col[8*3] + C3*col[8*5] - C1*col[8*7];

	col[8*0] = (a0 + b0) >> COL_SHIFT;
	col[8*1] = (a1 + b1) >> COL_SHIFT;
	col[8*2] = (a2 + b2) >> COL_SHIFT;
	col[8*3] = (a3 + b3) >> COL_SHIFT;
	col[8*4] = (a3 - b3) >> COL_SHIFT;
	col[8*5] = (a2 - b2) >> COL_SHIFT;
	col[8*6] = (a1 - b1) >> COL_SHIFT;
	col[8*7] = (a0 - b0) >> COL_SHIFT;
}

static void inline idctRow (int16_t * output, int16_t * input)
{
	int16_t row[8];

	int a0, a1, a2, a3, b0, b1, b2, b3;
	const int C0 = 23170; //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
	const int C1 = 22725; //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
	const int C2 = 21407; //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
	const int C3 = 19266; //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
	const int C4 = 16384; //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
	const int C5 = 12873; //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
	const int C6 = 8867; //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
	const int C7 = 4520; //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5

row[0] = input[0];
row[2] = input[1];
row[4] = input[4];
row[6] = input[5];
row[1] = input[8];
row[3] = input[9];
row[5] = input[12];
row[7] = input[13];

	if( !(row[1] | row[2] |row[3] |row[4] |row[5] |row[6] | row[7]) ) {
		row[0] = row[1] = row[2] = row[3] = row[4] =
			row[5] = row[6] = row[7] = row[0]<<3;
	output[0] = row[0];
	output[2] = row[1];
	output[4] = row[2];
	output[6] = row[3];
	output[8] = row[4];
	output[10] = row[5];
	output[12] = row[6];
	output[14] = row[7];
		return;
	}

	a0 = C4*row[0] + C2*row[2] + C4*row[4] + C6*row[6] + (1<<(ROW_SHIFT-1));
	a1 = C4*row[0] + C6*row[2] - C4*row[4] - C2*row[6] + (1<<(ROW_SHIFT-1));
	a2 = C4*row[0] - C6*row[2] - C4*row[4] + C2*row[6] + (1<<(ROW_SHIFT-1));
	a3 = C4*row[0] - C2*row[2] + C4*row[4] - C6*row[6] + (1<<(ROW_SHIFT-1));

	b0 = C1*row[1] + C3*row[3] + C5*row[5] + C7*row[7];
	b1 = C3*row[1] - C7*row[3] - C1*row[5] - C5*row[7];
	b2 = C5*row[1] - C1*row[3] + C7*row[5] + C3*row[7];
	b3 = C7*row[1] - C5*row[3] + C3*row[5] - C1*row[7];

	row[0] = (a0 + b0) >> ROW_SHIFT;
	row[1] = (a1 + b1) >> ROW_SHIFT;
	row[2] = (a2 + b2) >> ROW_SHIFT;
	row[3] = (a3 + b3) >> ROW_SHIFT;
	row[4] = (a3 - b3) >> ROW_SHIFT;
	row[5] = (a2 - b2) >> ROW_SHIFT;
	row[6] = (a1 - b1) >> ROW_SHIFT;
	row[7] = (a0 - b0) >> ROW_SHIFT;

	output[0] = row[0];
	output[2] = row[1];
	output[4] = row[2];
	output[6] = row[3];
	output[8] = row[4];
	output[10] = row[5];
	output[12] = row[6];
	output[14] = row[7];
}
#endif

static inline void idct(int16_t *block)
{
	int i;
//for(i=0; i<64; i++) temp[i]= block[ block_permute_op(i) ];
//for(i=0; i<64; i++) temp[block_permute_op(i)]= block[ i ];
//for(i=0; i<64; i++) block[i]= temp[i];
//block_permute(block);
/*
idctRow(temp, block);
idctRow(temp+16, block+16);
idctRow(temp+1, block+2);
idctRow(temp+17, block+18);
idctRow(temp+32, block+32);
idctRow(temp+48, block+48);
idctRow(temp+33, block+34);
idctRow(temp+49, block+50);
*/

	asm volatile(
//		"lea 64(%0), %%eax		\n\t"
//r0,r2,R0,R2	r4,r6,R4,R6	r1,r3,R1,R3	r5,r7,R5,R7
//src0		src4		src1		src5
//r0,R0,r7,R7	r1,R1,r6,R6	r2,R2,r5,R5	r3,R3,r4,R4
//dst0		dst1		dst2		dst3
#if 0 //Alternative, simpler variant
#define IDCT_CORE(src0, src4, src1, src5, dst, rounder, shift) \
	"movq " #src0 ", %%mm0			\n\t" /* R2	R0	r2	r0 */\
	"movq " #src4 ", %%mm1			\n\t" /* R6	R4	r6	r4 */\
	"movq " #src1 ", %%mm2			\n\t" /* R3	R1	r3	r1 */\
	"movq " #src5 ", %%mm3			\n\t" /* R7	R5	r7	r5 */\
	"movq 16(%2), %%mm4			\n\t" /* C2	C4	C2	C4 */\
	"pmaddwd %%mm0, %%mm4			\n\t" /* C2R2+C4R0	C2r2+C4r0 */\
	"movq 24(%2), %%mm5			\n\t" /* C6	C4	C6	C4 */\
	"pmaddwd %%mm1, %%mm5			\n\t" /* C6R6+C4R4	C6r6+C4r4 */\
	"movq 32(%2), %%mm6			\n\t" /* C3	C1	C3	C1 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* C3R3+C1R1	C3r3+C1r1 */\
	"movq 40(%2), %%mm7			\n\t" /* C7	C5	C7	C5 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* C7R7+C5R5	C7r7+C5r5 */\
	"paddd %%mm5, %%mm4			\n\t" /* A0		a0 */\
	#rounder ", %%mm4			\n\t"\
\
	"movq 48(%2), %%mm5			\n\t" /* C6	C4	C6	C4 */\
	"pmaddwd %%mm0, %%mm5			\n\t" /* C6R2+C4R0	C6r2+C4r0 */\
	"paddd %%mm7, %%mm6			\n\t" /* B0		b0 */\
	"paddd %%mm4, %%mm6			\n\t" /* A0+B0		a0+b0 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A0		2a0 */\
	"psubd %%mm6, %%mm4			\n\t" /* A0-B0		a0-b0 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
	WRITE0(%%mm6, %%mm4, dst) \
\
	"movq 56(%2), %%mm4			\n\t" /* -C2	-C4	-C2	-C4 */\
	"pmaddwd %%mm1, %%mm4			\n\t" /* -C2R6-C4R4	-C2r6-C4r4 */\
	"movq 64(%2), %%mm6			\n\t" /* -C7	C3	-C7	C3 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* -C7R3+C3R1	-C7r3+C3r1 */\
	"movq 72(%2), %%mm7			\n\t" /* -C5	-C1	-C5	-C1 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* -C5R7-C1R5	-C5r7-C1r5 */\
	"paddd %%mm5, %%mm4			\n\t" /* A1		a1 */\
	#rounder ", %%mm4			\n\t"\
\
	"movq 80(%2), %%mm5			\n\t" /* -C6	C4	-C6	C4 */\
	"pmaddwd %%mm0, %%mm5			\n\t" /* -C6R2+C4R0	-C6r2+C4r0 */\
	"paddd %%mm7, %%mm6			\n\t" /* B1		b1 */\
	"paddd %%mm4, %%mm6			\n\t" /* A1+B1		a1+b1 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A1		2a1 */\
	"psubd %%mm6, %%mm4			\n\t" /* A1-B1		a1-b1 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
	WRITE1(%%mm6, %%mm4, dst, %%mm7) \
\
	"movq 88(%2), %%mm4			\n\t" /* C2	-C4	C2	-C4 */\
	"pmaddwd %%mm1, %%mm4			\n\t" /* C2R6-C4R4	C2r6-C4r4 */\
	"movq 96(%2), %%mm6			\n\t" /* -C1	C5	-C1	C5 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* -C1R3+C5R1	-C1r3+C5r1 */\
	"movq 104(%2), %%mm7			\n\t" /* C3	C7	C3	C7 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* C3R7+C7R5	C3r7+C7r5 */\
	"paddd %%mm5, %%mm4			\n\t" /* A2		a2 */\
	#rounder ", %%mm4			\n\t"\
\
	"pmaddwd 112(%2), %%mm0			\n\t" /* -C2R2+C4R0	-C2r2+C4r0 */\
	"paddd %%mm7, %%mm6			\n\t" /* B1		b1 */\
	"paddd %%mm4, %%mm6			\n\t" /* A1+B1		a1+b1 */\
	"pmaddwd 120(%2), %%mm1			\n\t" /* -C6R6+C4R4	-C6r6+C4r4 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A1		2a1 */\
	"psubd %%mm6, %%mm4			\n\t" /* A1-B1		a1-b1 */\
	"pmaddwd 128(%2), %%mm2			\n\t" /* -C5R3+C7R1	-C5r3+C7r1 */\
	"pmaddwd 136(%2), %%mm3			\n\t" /* -C1R7+C3R5	-C1r7+C3r5 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
\
	"paddd %%mm1, %%mm0			\n\t" /* A3		a3 */\
	#rounder ", %%mm0			\n\t"\
	"paddd %%mm3, %%mm2			\n\t" /* B3		b3 */\
	"paddd %%mm0, %%mm2			\n\t" /* A3+B3		a3+b3 */\
	"paddd %%mm0, %%mm0			\n\t" /* 2A3		2a3 */\
	"psubd %%mm2, %%mm0			\n\t" /* A3-B3		a3-b3 */\
	"psrad $" #shift ", %%mm2		\n\t"\
	"psrad $" #shift ", %%mm0		\n\t"\
	WRITE2(%%mm6, %%mm4, %%mm2, %%mm0, dst)

#define DC_COND_IDCT_CORE(src0, src4, src1, src5, dst, rounder, shift) \
	"movq " #src0 ", %%mm0			\n\t" /* R2	R0	r2	r0 */\
	"movq " #src4 ", %%mm1			\n\t" /* R6	R4	r6	r4 */\
	"movq " #src1 ", %%mm2			\n\t" /* R3	R1	r3	r1 */\
	"movq " #src5 ", %%mm3			\n\t" /* R7	R5	r7	r5 */\
	"movq wm1010, %%mm4			\n\t"\
	"pand %%mm0, %%mm4			\n\t"\
	"por %%mm1, %%mm4			\n\t"\
	"por %%mm2, %%mm4			\n\t"\
	"por %%mm3, %%mm4			\n\t"\
	"packssdw %%mm4,%%mm4			\n\t"\
	"movd %%mm4, %%eax			\n\t"\
	"orl %%eax, %%eax			\n\t"\
	"jz 1f					\n\t"\
	"movq 16(%2), %%mm4			\n\t" /* C2	C4	C2	C4 */\
	"pmaddwd %%mm0, %%mm4			\n\t" /* C2R2+C4R0	C2r2+C4r0 */\
	"movq 24(%2), %%mm5			\n\t" /* C6	C4	C6	C4 */\
	"pmaddwd %%mm1, %%mm5			\n\t" /* C6R6+C4R4	C6r6+C4r4 */\
	"movq 32(%2), %%mm6			\n\t" /* C3	C1	C3	C1 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* C3R3+C1R1	C3r3+C1r1 */\
	"movq 40(%2), %%mm7			\n\t" /* C7	C5	C7	C5 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* C7R7+C5R5	C7r7+C5r5 */\
	"paddd %%mm5, %%mm4			\n\t" /* A0		a0 */\
	#rounder ", %%mm4			\n\t"\
\
	"movq 48(%2), %%mm5			\n\t" /* C6	C4	C6	C4 */\
	"pmaddwd %%mm0, %%mm5			\n\t" /* C6R2+C4R0	C6r2+C4r0 */\
	"paddd %%mm7, %%mm6			\n\t" /* B0		b0 */\
	"paddd %%mm4, %%mm6			\n\t" /* A0+B0		a0+b0 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A0		2a0 */\
	"psubd %%mm6, %%mm4			\n\t" /* A0-B0		a0-b0 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
	WRITE0(%%mm6, %%mm4, dst) \
\
	"movq 56(%2), %%mm4			\n\t" /* -C2	-C4	-C2	-C4 */\
	"pmaddwd %%mm1, %%mm4			\n\t" /* -C2R6-C4R4	-C2r6-C4r4 */\
	"movq 64(%2), %%mm6			\n\t" /* -C7	C3	-C7	C3 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* -C7R3+C3R1	-C7r3+C3r1 */\
	"movq 72(%2), %%mm7			\n\t" /* -C5	-C1	-C5	-C1 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* -C5R7-C1R5	-C5r7-C1r5 */\
	"paddd %%mm5, %%mm4			\n\t" /* A1		a1 */\
	#rounder ", %%mm4			\n\t"\
\
	"movq 80(%2), %%mm5			\n\t" /* -C6	C4	-C6	C4 */\
	"pmaddwd %%mm0, %%mm5			\n\t" /* -C6R2+C4R0	-C6r2+C4r0 */\
	"paddd %%mm7, %%mm6			\n\t" /* B1		b1 */\
	"paddd %%mm4, %%mm6			\n\t" /* A1+B1		a1+b1 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A1		2a1 */\
	"psubd %%mm6, %%mm4			\n\t" /* A1-B1		a1-b1 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
	WRITE1(%%mm6, %%mm4, dst, %%mm7) \
\
	"movq 88(%2), %%mm4			\n\t" /* C2	-C4	C2	-C4 */\
	"pmaddwd %%mm1, %%mm4			\n\t" /* C2R6-C4R4	C2r6-C4r4 */\
	"movq 96(%2), %%mm6			\n\t" /* -C1	C5	-C1	C5 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* -C1R3+C5R1	-C1r3+C5r1 */\
	"movq 104(%2), %%mm7			\n\t" /* C3	C7	C3	C7 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* C3R7+C7R5	C3r7+C7r5 */\
	"paddd %%mm5, %%mm4			\n\t" /* A2		a2 */\
	#rounder ", %%mm4			\n\t"\
\
	"pmaddwd 112(%2), %%mm0			\n\t" /* -C2R2+C4R0	-C2r2+C4r0 */\
	"paddd %%mm7, %%mm6			\n\t" /* B1		b1 */\
	"paddd %%mm4, %%mm6			\n\t" /* A1+B1		a1+b1 */\
	"pmaddwd 120(%2), %%mm1			\n\t" /* -C6R6+C4R4	-C6r6+C4r4 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A1		2a1 */\
	"psubd %%mm6, %%mm4			\n\t" /* A1-B1		a1-b1 */\
	"pmaddwd 128(%2), %%mm2			\n\t" /* -C5R3+C7R1	-C5r3+C7r1 */\
	"pmaddwd 136(%2), %%mm3			\n\t" /* -C1R7+C3R5	-C1r7+C3r5 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
\
	"paddd %%mm1, %%mm0			\n\t" /* A3		a3 */\
	#rounder ", %%mm0			\n\t"\
	"paddd %%mm3, %%mm2			\n\t" /* B3		b3 */\
	"paddd %%mm0, %%mm2			\n\t" /* A3+B3		a3+b3 */\
	"paddd %%mm0, %%mm0			\n\t" /* 2A3		2a3 */\
	"psubd %%mm2, %%mm0			\n\t" /* A3-B3		a3-b3 */\
	"psrad $" #shift ", %%mm2		\n\t"\
	"psrad $" #shift ", %%mm0		\n\t"\
	WRITE2(%%mm6, %%mm4, %%mm2, %%mm0, dst)\
	"jmp 2f					\n\t"\
	"1:					\n\t"\
	WRITE3(%%mm0, dst)\
	"2:					\n\t"\


#define WRITE0(s0, s7, dst)\
	"movq " #s0 ", " #dst "			\n\t" /* R0		r0 */\
	"movq " #s7 ", 24+" #dst "		\n\t" /* R7		r7 */

#define WRITE1(s1, s6, dst, tmp)\
	"movq " #dst ", " #tmp "		\n\t" /* R0		r0 */\
	"packssdw " #s1 ", " #tmp "		\n\t" /* R1	r1	R0	r0*/\
	"movq " #tmp ", " #dst "		\n\t"\
	"movq 24+" #dst ", " #tmp "		\n\t" /* R7		r7 */\
	"packssdw " #tmp ", " #s6 "		\n\t" /* R7	r7	R6	r6*/\
	"movq " #s6 ", 24+" #dst "		\n\t"

#define WRITE2(s2, s5, s3, s4, dst)\
	"packssdw " #s3 ", " #s2 "		\n\t" /* R3	r3	R2	r2*/\
	"packssdw " #s5 ", " #s4 "		\n\t" /* R5	r5	R4	r4*/\
	"movq " #s2 ", 8+" #dst "		\n\t"\
	"movq " #s4 ", 16+" #dst "		\n\t"

#define WRITE3(a, dst)\
	"pslld $16, " #a "			\n\t"\
	"psrad $13, " #a "			\n\t"\
	"packssdw " #a ", " #a "		\n\t"\
	"movq " #a ", " #dst "			\n\t"\
	"movq " #a ", 8+" #dst "		\n\t"\
	"movq " #a ", 16+" #dst "		\n\t"\
	"movq " #a ", 24+" #dst "		\n\t"\

//IDCT_CORE(          src0,   src4,   src1,   src5,    dst,   rounder, shift)
IDCT_CORE(            (%0),  8(%0), 16(%0), 24(%0),  0(%1),paddd 8(%2), 11)
/*
DC_COND_IDCT_CORE(  32(%0), 40(%0), 48(%0), 56(%0), 32(%1),paddd (%2), 11)
DC_COND_IDCT_CORE(  64(%0), 72(%0), 80(%0), 88(%0), 64(%1),paddd (%2), 11)
DC_COND_IDCT_CORE(  96(%0),104(%0),112(%0),120(%0), 96(%1),paddd (%2), 11)
*/
IDCT_CORE(  32(%0), 40(%0), 48(%0), 56(%0), 32(%1),paddd (%2), 11)
IDCT_CORE(  64(%0), 72(%0), 80(%0), 88(%0), 64(%1),paddd (%2), 11)
IDCT_CORE(  96(%0),104(%0),112(%0),120(%0), 96(%1),paddd (%2), 11)

#undef WRITE0
#undef WRITE1
#undef WRITE2

#define WRITE0(s0, s7, dst)\
	"packssdw " #s0 ", " #s0 "		\n\t" /* C0, c0, C0, c0 */\
	"packssdw " #s7 ", " #s7 "		\n\t" /* C7, c7, C7, c7 */\
	"movd " #s0 ", " #dst "			\n\t" /* C0, c0 */\
	"movd " #s7 ", 112+" #dst "		\n\t" /* C7, c7 */

#define WRITE1(s1, s6, dst, tmp)\
	"packssdw " #s1 ", " #s1 "		\n\t" /* C1, c1, C1, c1 */\
	"packssdw " #s6 ", " #s6 "		\n\t" /* C6, c6, C6, c6 */\
	"movd " #s1 ", 16+" #dst "		\n\t" /* C1, c1 */\
	"movd " #s6 ", 96+" #dst "		\n\t" /* C6, c6 */

#define WRITE2(s2, s5, s3, s4, dst)\
	"packssdw " #s2 ", " #s2 "		\n\t" /* C2, c2, C2, c2 */\
	"packssdw " #s3 ", " #s3 "		\n\t" /* C3, c3, C3, c3 */\
	"movd " #s2 ", 32+" #dst "		\n\t" /* C2, c2 */\
	"movd " #s3 ", 48+" #dst "		\n\t" /* C3, c3 */\
	"packssdw " #s4 ", " #s4 "		\n\t" /* C4, c4, C4, c4 */\
	"packssdw " #s5 ", " #s5 "		\n\t" /* C5, c5, C5, c5 */\
	"movd " #s4 ", 64+" #dst "		\n\t" /* C4, c4 */\
	"movd " #s5 ", 80+" #dst "		\n\t" /* C5, c5 */\

//IDCT_CORE(  src0,   src4,   src1,    src5,    dst, rounder, shift)
IDCT_CORE(    (%1), 64(%1), 32(%1),  96(%1),  0(%0),/nop, 20)
IDCT_CORE(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0),/nop, 20)
IDCT_CORE(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0),/nop, 20)
IDCT_CORE(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0),/nop, 20)

#else

#define IDCT_CORE(src0, src4, src1, src5, dst, rounder, shift) \
	"movq " #src0 ", %%mm0			\n\t" /* R2	R0	r2	r0 */\
	"movq " #src4 ", %%mm1			\n\t" /* R6	R4	r6	r4 */\
	"movq " #src1 ", %%mm2			\n\t" /* R3	R1	r3	r1 */\
	"movq " #src5 ", %%mm3			\n\t" /* R7	R5	r7	r5 */\
	"movq 16(%2), %%mm4			\n\t" /* C2	C4	C2	C4 */\
	"pmaddwd %%mm0, %%mm4			\n\t" /* C2R2+C4R0	C2r2+C4r0 */\
	"movq 24(%2), %%mm5			\n\t" /* C6	C4	C6	C4 */\
	"pmaddwd %%mm1, %%mm5			\n\t" /* C6R6+C4R4	C6r6+C4r4 */\
	"movq 32(%2), %%mm6			\n\t" /* C3	C1	C3	C1 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* C3R3+C1R1	C3r3+C1r1 */\
	"movq 40(%2), %%mm7			\n\t" /* C7	C5	C7	C5 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* C7R7+C5R5	C7r7+C5r5 */\
	"paddd %%mm5, %%mm4			\n\t" /* A0		a0 */\
	#rounder ", %%mm4			\n\t"\
\
	"movq 48(%2), %%mm5			\n\t" /* C6	C4	C6	C4 */\
	"pmaddwd %%mm0, %%mm5			\n\t" /* C6R2+C4R0	C6r2+C4r0 */\
	"paddd %%mm7, %%mm6			\n\t" /* B0		b0 */\
	"paddd %%mm4, %%mm6			\n\t" /* A0+B0		a0+b0 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A0		2a0 */\
	"psubd %%mm6, %%mm4			\n\t" /* A0-B0		a0-b0 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
	WRITE0(%%mm6, %%mm4, dst) \
\
	"movq 56(%2), %%mm4			\n\t" /* -C2	-C4	-C2	-C4 */\
	"pmaddwd %%mm1, %%mm4			\n\t" /* -C2R6-C4R4	-C2r6-C4r4 */\
	"movq 64(%2), %%mm6			\n\t" /* -C7	C3	-C7	C3 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* -C7R3+C3R1	-C7r3+C3r1 */\
	"movq 72(%2), %%mm7			\n\t" /* -C5	-C1	-C5	-C1 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* -C5R7-C1R5	-C5r7-C1r5 */\
	"paddd %%mm5, %%mm4			\n\t" /* A1		a1 */\
	#rounder ", %%mm4			\n\t"\
\
	"movq 80(%2), %%mm5			\n\t" /* -C6	C4	-C6	C4 */\
	"pmaddwd %%mm0, %%mm5			\n\t" /* -C6R2+C4R0	-C6r2+C4r0 */\
	"paddd %%mm7, %%mm6			\n\t" /* B1		b1 */\
	"paddd %%mm4, %%mm6			\n\t" /* A1+B1		a1+b1 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A1		2a1 */\
	"psubd %%mm6, %%mm4			\n\t" /* A1-B1		a1-b1 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
	WRITE1(%%mm6, %%mm4, dst, %%mm7) \
\
	"movq 88(%2), %%mm4			\n\t" /* C2	-C4	C2	-C4 */\
	"pmaddwd %%mm1, %%mm4			\n\t" /* C2R6-C4R4	C2r6-C4r4 */\
	"movq 96(%2), %%mm6			\n\t" /* -C1	C5	-C1	C5 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* -C1R3+C5R1	-C1r3+C5r1 */\
	"movq 104(%2), %%mm7			\n\t" /* C3	C7	C3	C7 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* C3R7+C7R5	C3r7+C7r5 */\
	"paddd %%mm5, %%mm4			\n\t" /* A2		a2 */\
	#rounder ", %%mm4			\n\t"\
\
	"pmaddwd 112(%2), %%mm0			\n\t" /* -C2R2+C4R0	-C2r2+C4r0 */\
	"paddd %%mm7, %%mm6			\n\t" /* B1		b1 */\
	"paddd %%mm4, %%mm6			\n\t" /* A1+B1		a1+b1 */\
	"pmaddwd 120(%2), %%mm1			\n\t" /* -C6R6+C4R4	-C6r6+C4r4 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A1		2a1 */\
	"psubd %%mm6, %%mm4			\n\t" /* A1-B1		a1-b1 */\
	"pmaddwd 128(%2), %%mm2			\n\t" /* -C5R3+C7R1	-C5r3+C7r1 */\
	"pmaddwd 136(%2), %%mm3			\n\t" /* -C1R7+C3R5	-C1r7+C3r5 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
\
	"paddd %%mm1, %%mm0			\n\t" /* A3		a3 */\
	#rounder ", %%mm0			\n\t"\
	"paddd %%mm3, %%mm2			\n\t" /* B3		b3 */\
	"paddd %%mm0, %%mm2			\n\t" /* A3+B3		a3+b3 */\
	"paddd %%mm0, %%mm0			\n\t" /* 2A3		2a3 */\
	"psubd %%mm2, %%mm0			\n\t" /* A3-B3		a3-b3 */\
	"psrad $" #shift ", %%mm2		\n\t"\
	"psrad $" #shift ", %%mm0		\n\t"\
	WRITE2(%%mm6, %%mm4, %%mm2, %%mm0, dst)

#define DC_COND_IDCT_CORE(src0, src4, src1, src5, dst, rounder, shift) \
	"movq " #src0 ", %%mm0			\n\t" /* R2	R0	r2	r0 */\
	"movq " #src4 ", %%mm1			\n\t" /* R6	R4	r6	r4 */\
	"movq " #src1 ", %%mm2			\n\t" /* R3	R1	r3	r1 */\
	"movq " #src5 ", %%mm3			\n\t" /* R7	R5	r7	r5 */\
	"movq wm1010, %%mm4			\n\t"\
	"pand %%mm0, %%mm4			\n\t"\
	"por %%mm1, %%mm4			\n\t"\
	"por %%mm2, %%mm4			\n\t"\
	"por %%mm3, %%mm4			\n\t"\
	"packssdw %%mm4,%%mm4			\n\t"\
	"movd %%mm4, %%eax			\n\t"\
	"orl %%eax, %%eax			\n\t"\
	"jz 1f					\n\t"\
	"movq 16(%2), %%mm4			\n\t" /* C2	C4	C2	C4 */\
	"pmaddwd %%mm0, %%mm4			\n\t" /* C2R2+C4R0	C2r2+C4r0 */\
	"movq 24(%2), %%mm5			\n\t" /* C6	C4	C6	C4 */\
	"pmaddwd %%mm1, %%mm5			\n\t" /* C6R6+C4R4	C6r6+C4r4 */\
	"movq 32(%2), %%mm6			\n\t" /* C3	C1	C3	C1 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* C3R3+C1R1	C3r3+C1r1 */\
	"movq 40(%2), %%mm7			\n\t" /* C7	C5	C7	C5 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* C7R7+C5R5	C7r7+C5r5 */\
	"paddd %%mm5, %%mm4			\n\t" /* A0		a0 */\
	#rounder ", %%mm4			\n\t"\
\
	"movq 48(%2), %%mm5			\n\t" /* C6	C4	C6	C4 */\
	"pmaddwd %%mm0, %%mm5			\n\t" /* C6R2+C4R0	C6r2+C4r0 */\
	"paddd %%mm7, %%mm6			\n\t" /* B0		b0 */\
	"paddd %%mm4, %%mm6			\n\t" /* A0+B0		a0+b0 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A0		2a0 */\
	"psubd %%mm6, %%mm4			\n\t" /* A0-B0		a0-b0 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
	WRITE0(%%mm6, %%mm4, dst) \
\
	"movq 56(%2), %%mm4			\n\t" /* -C2	-C4	-C2	-C4 */\
	"pmaddwd %%mm1, %%mm4			\n\t" /* -C2R6-C4R4	-C2r6-C4r4 */\
	"movq 64(%2), %%mm6			\n\t" /* -C7	C3	-C7	C3 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* -C7R3+C3R1	-C7r3+C3r1 */\
	"movq 72(%2), %%mm7			\n\t" /* -C5	-C1	-C5	-C1 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* -C5R7-C1R5	-C5r7-C1r5 */\
	"paddd %%mm5, %%mm4			\n\t" /* A1		a1 */\
	#rounder ", %%mm4			\n\t"\
\
	"movq 80(%2), %%mm5			\n\t" /* -C6	C4	-C6	C4 */\
	"pmaddwd %%mm0, %%mm5			\n\t" /* -C6R2+C4R0	-C6r2+C4r0 */\
	"paddd %%mm7, %%mm6			\n\t" /* B1		b1 */\
	"paddd %%mm4, %%mm6			\n\t" /* A1+B1		a1+b1 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A1		2a1 */\
	"psubd %%mm6, %%mm4			\n\t" /* A1-B1		a1-b1 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
	WRITE1(%%mm6, %%mm4, dst, %%mm7) \
\
	"movq 88(%2), %%mm4			\n\t" /* C2	-C4	C2	-C4 */\
	"pmaddwd %%mm1, %%mm4			\n\t" /* C2R6-C4R4	C2r6-C4r4 */\
	"movq 96(%2), %%mm6			\n\t" /* -C1	C5	-C1	C5 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* -C1R3+C5R1	-C1r3+C5r1 */\
	"movq 104(%2), %%mm7			\n\t" /* C3	C7	C3	C7 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* C3R7+C7R5	C3r7+C7r5 */\
	"paddd %%mm5, %%mm4			\n\t" /* A2		a2 */\
	#rounder ", %%mm4			\n\t"\
\
	"pmaddwd 112(%2), %%mm0			\n\t" /* -C2R2+C4R0	-C2r2+C4r0 */\
	"paddd %%mm7, %%mm6			\n\t" /* B1		b1 */\
	"paddd %%mm4, %%mm6			\n\t" /* A1+B1		a1+b1 */\
	"pmaddwd 120(%2), %%mm1			\n\t" /* -C6R6+C4R4	-C6r6+C4r4 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A1		2a1 */\
	"psubd %%mm6, %%mm4			\n\t" /* A1-B1		a1-b1 */\
	"pmaddwd 128(%2), %%mm2			\n\t" /* -C5R3+C7R1	-C5r3+C7r1 */\
	"pmaddwd 136(%2), %%mm3			\n\t" /* -C1R7+C3R5	-C1r7+C3r5 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
\
	"paddd %%mm1, %%mm0			\n\t" /* A3		a3 */\
	#rounder ", %%mm0			\n\t"\
	"paddd %%mm3, %%mm2			\n\t" /* B3		b3 */\
	"paddd %%mm0, %%mm2			\n\t" /* A3+B3		a3+b3 */\
	"paddd %%mm0, %%mm0			\n\t" /* 2A3		2a3 */\
	"psubd %%mm2, %%mm0			\n\t" /* A3-B3		a3-b3 */\
	"psrad $" #shift ", %%mm2		\n\t"\
	"psrad $" #shift ", %%mm0		\n\t"\
	WRITE2(%%mm6, %%mm4, %%mm2, %%mm0, dst)\
	"jmp 2f					\n\t"\
	"#.balign 16				\n\t"\
	"1:					\n\t"\
	WRITE3(%%mm0, dst)\
	"2:					\n\t"\

#define Z_COND_IDCT_CORE(src0, src4, src1, src5, dst, rounder, shift, bt) \
	"movq " #src0 ", %%mm0			\n\t" /* R2	R0	r2	r0 */\
	"movq " #src4 ", %%mm1			\n\t" /* R6	R4	r6	r4 */\
	"movq " #src1 ", %%mm2			\n\t" /* R3	R1	r3	r1 */\
	"movq " #src5 ", %%mm3			\n\t" /* R7	R5	r7	r5 */\
	"movq %%mm0, %%mm4			\n\t"\
	"por %%mm1, %%mm4			\n\t"\
	"por %%mm2, %%mm4			\n\t"\
	"por %%mm3, %%mm4			\n\t"\
	"packssdw %%mm4, %%mm4			\n\t"\
	"movd %%mm4, %%eax			\n\t"\
	"orl %%eax, %%eax			\n\t"\
	"jz " #bt "				\n\t"\
	"movq 16(%2), %%mm4			\n\t" /* C2	C4	C2	C4 */\
	"pmaddwd %%mm0, %%mm4			\n\t" /* C2R2+C4R0	C2r2+C4r0 */\
	"movq 24(%2), %%mm5			\n\t" /* C6	C4	C6	C4 */\
	"pmaddwd %%mm1, %%mm5			\n\t" /* C6R6+C4R4	C6r6+C4r4 */\
	"movq 32(%2), %%mm6			\n\t" /* C3	C1	C3	C1 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* C3R3+C1R1	C3r3+C1r1 */\
	"movq 40(%2), %%mm7			\n\t" /* C7	C5	C7	C5 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* C7R7+C5R5	C7r7+C5r5 */\
	"paddd %%mm5, %%mm4			\n\t" /* A0		a0 */\
	#rounder ", %%mm4			\n\t"\
\
	"movq 48(%2), %%mm5			\n\t" /* C6	C4	C6	C4 */\
	"pmaddwd %%mm0, %%mm5			\n\t" /* C6R2+C4R0	C6r2+C4r0 */\
	"paddd %%mm7, %%mm6			\n\t" /* B0		b0 */\
	"paddd %%mm4, %%mm6			\n\t" /* A0+B0		a0+b0 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A0		2a0 */\
	"psubd %%mm6, %%mm4			\n\t" /* A0-B0		a0-b0 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
	WRITE0(%%mm6, %%mm4, dst) \
\
	"movq 56(%2), %%mm4			\n\t" /* -C2	-C4	-C2	-C4 */\
	"pmaddwd %%mm1, %%mm4			\n\t" /* -C2R6-C4R4	-C2r6-C4r4 */\
	"movq 64(%2), %%mm6			\n\t" /* -C7	C3	-C7	C3 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* -C7R3+C3R1	-C7r3+C3r1 */\
	"movq 72(%2), %%mm7			\n\t" /* -C5	-C1	-C5	-C1 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* -C5R7-C1R5	-C5r7-C1r5 */\
	"paddd %%mm5, %%mm4			\n\t" /* A1		a1 */\
	#rounder ", %%mm4			\n\t"\
\
	"movq 80(%2), %%mm5			\n\t" /* -C6	C4	-C6	C4 */\
	"pmaddwd %%mm0, %%mm5			\n\t" /* -C6R2+C4R0	-C6r2+C4r0 */\
	"paddd %%mm7, %%mm6			\n\t" /* B1		b1 */\
	"paddd %%mm4, %%mm6			\n\t" /* A1+B1		a1+b1 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A1		2a1 */\
	"psubd %%mm6, %%mm4			\n\t" /* A1-B1		a1-b1 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
	WRITE1(%%mm6, %%mm4, dst, %%mm7) \
\
	"movq 88(%2), %%mm4			\n\t" /* C2	-C4	C2	-C4 */\
	"pmaddwd %%mm1, %%mm4			\n\t" /* C2R6-C4R4	C2r6-C4r4 */\
	"movq 96(%2), %%mm6			\n\t" /* -C1	C5	-C1	C5 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* -C1R3+C5R1	-C1r3+C5r1 */\
	"movq 104(%2), %%mm7			\n\t" /* C3	C7	C3	C7 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* C3R7+C7R5	C3r7+C7r5 */\
	"paddd %%mm5, %%mm4			\n\t" /* A2		a2 */\
	#rounder ", %%mm4			\n\t"\
\
	"pmaddwd 112(%2), %%mm0			\n\t" /* -C2R2+C4R0	-C2r2+C4r0 */\
	"paddd %%mm7, %%mm6			\n\t" /* B1		b1 */\
	"paddd %%mm4, %%mm6			\n\t" /* A1+B1		a1+b1 */\
	"pmaddwd 120(%2), %%mm1			\n\t" /* -C6R6+C4R4	-C6r6+C4r4 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A1		2a1 */\
	"psubd %%mm6, %%mm4			\n\t" /* A1-B1		a1-b1 */\
	"pmaddwd 128(%2), %%mm2			\n\t" /* -C5R3+C7R1	-C5r3+C7r1 */\
	"pmaddwd 136(%2), %%mm3			\n\t" /* -C1R7+C3R5	-C1r7+C3r5 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
\
	"paddd %%mm1, %%mm0			\n\t" /* A3		a3 */\
	#rounder ", %%mm0			\n\t"\
	"paddd %%mm3, %%mm2			\n\t" /* B3		b3 */\
	"paddd %%mm0, %%mm2			\n\t" /* A3+B3		a3+b3 */\
	"paddd %%mm0, %%mm0			\n\t" /* 2A3		2a3 */\
	"psubd %%mm2, %%mm0			\n\t" /* A3-B3		a3-b3 */\
	"psrad $" #shift ", %%mm2		\n\t"\
	"psrad $" #shift ", %%mm0		\n\t"\
	WRITE2(%%mm6, %%mm4, %%mm2, %%mm0, dst)\


#define WRITE0(s0, s7, dst)\
	"movq " #s0 ", " #dst "			\n\t" /* R0		r0 */\
	"movq " #s7 ", 24+" #dst "		\n\t" /* R7		r7 */

#define WRITE1(s1, s6, dst, tmp)\
	"movq " #dst ", " #tmp "		\n\t" /* R0		r0 */\
	"packssdw " #s1 ", " #tmp "		\n\t" /* R1	r1	R0	r0*/\
	"movq " #tmp ", " #dst "		\n\t"\
	"movq 24+" #dst ", " #tmp "		\n\t" /* R7		r7 */\
	"packssdw " #tmp ", " #s6 "		\n\t" /* R7	r7	R6	r6*/\
	"movq " #s6 ", 24+" #dst "		\n\t"

#define WRITE2(s2, s5, s3, s4, dst)\
	"packssdw " #s3 ", " #s2 "		\n\t" /* R3	r3	R2	r2*/\
	"packssdw " #s5 ", " #s4 "		\n\t" /* R5	r5	R4	r4*/\
	"movq " #s2 ", 8+" #dst "		\n\t"\
	"movq " #s4 ", 16+" #dst "		\n\t"

#define WRITE3(a, dst)\
	"pslld $16, " #a "			\n\t"\
	"paddd d40000, " #a "			\n\t"\
	"psrad $13, " #a "			\n\t"\
	"packssdw " #a ", " #a "		\n\t"\
	"movq " #a ", " #dst "			\n\t"\
	"movq " #a ", 8+" #dst "		\n\t"\
	"movq " #a ", 16+" #dst "		\n\t"\
	"movq " #a ", 24+" #dst "		\n\t"\

#define WRITE0b(s0, s7, dst)\
	"packssdw " #s0 ", " #s0 "		\n\t" /* C0, c0, C0, c0 */\
	"packssdw " #s7 ", " #s7 "		\n\t" /* C7, c7, C7, c7 */\
	"movd " #s0 ", " #dst "			\n\t" /* C0, c0 */\
	"movd " #s7 ", 112+" #dst "		\n\t" /* C7, c7 */

#define WRITE1b(s1, s6, dst, tmp)\
	"packssdw " #s1 ", " #s1 "		\n\t" /* C1, c1, C1, c1 */\
	"packssdw " #s6 ", " #s6 "		\n\t" /* C6, c6, C6, c6 */\
	"movd " #s1 ", 16+" #dst "		\n\t" /* C1, c1 */\
	"movd " #s6 ", 96+" #dst "		\n\t" /* C6, c6 */

#define WRITE2b(s2, s5, s3, s4, dst)\
	"packssdw " #s2 ", " #s2 "		\n\t" /* C2, c2, C2, c2 */\
	"packssdw " #s3 ", " #s3 "		\n\t" /* C3, c3, C3, c3 */\
	"movd " #s2 ", 32+" #dst "		\n\t" /* C2, c2 */\
	"movd " #s3 ", 48+" #dst "		\n\t" /* C3, c3 */\
	"packssdw " #s4 ", " #s4 "		\n\t" /* C4, c4, C4, c4 */\
	"packssdw " #s5 ", " #s5 "		\n\t" /* C5, c5, C5, c5 */\
	"movd " #s4 ", 64+" #dst "		\n\t" /* C4, c4 */\
	"movd " #s5 ", 80+" #dst "		\n\t" /* C5, c5 */\


//IDCT_CORE(         src0,   src4,   src1,   src5,    dst,   rounder, shift)
DC_COND_IDCT_CORE(  0(%0),  8(%0), 16(%0), 24(%0),  0(%1),paddd 8(%2), 11)
Z_COND_IDCT_CORE(  32(%0), 40(%0), 48(%0), 56(%0), 32(%1),paddd (%2), 11, 4f)
Z_COND_IDCT_CORE(  64(%0), 72(%0), 80(%0), 88(%0), 64(%1),paddd (%2), 11, 2f)
Z_COND_IDCT_CORE(  96(%0),104(%0),112(%0),120(%0), 96(%1),paddd (%2), 11, 1f)

#undef IDCT_CORE
#define IDCT_CORE(src0, src4, src1, src5, dst, rounder, shift) \
	"movq " #src0 ", %%mm0			\n\t" /* R2	R0	r2	r0 */\
	"movq " #src4 ", %%mm1			\n\t" /* R6	R4	r6	r4 */\
	"movq " #src1 ", %%mm2			\n\t" /* R3	R1	r3	r1 */\
	"movq " #src5 ", %%mm3			\n\t" /* R7	R5	r7	r5 */\
	"movq 16(%2), %%mm4			\n\t" /* C2	C4	C2	C4 */\
	"pmaddwd %%mm0, %%mm4			\n\t" /* C2R2+C4R0	C2r2+C4r0 */\
	"movq 24(%2), %%mm5			\n\t" /* C6	C4	C6	C4 */\
	"pmaddwd %%mm1, %%mm5			\n\t" /* C6R6+C4R4	C6r6+C4r4 */\
	"movq 32(%2), %%mm6			\n\t" /* C3	C1	C3	C1 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* C3R3+C1R1	C3r3+C1r1 */\
	"movq 40(%2), %%mm7			\n\t" /* C7	C5	C7	C5 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* C7R7+C5R5	C7r7+C5r5 */\
	"paddd %%mm5, %%mm4			\n\t" /* A0		a0 */\
\
	"movq 48(%2), %%mm5			\n\t" /* C6	C4	C6	C4 */\
	"pmaddwd %%mm0, %%mm5			\n\t" /* C6R2+C4R0	C6r2+C4r0 */\
	"paddd %%mm7, %%mm6			\n\t" /* B0		b0 */\
	"paddd %%mm4, %%mm6			\n\t" /* A0+B0		a0+b0 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A0		2a0 */\
	"psubd %%mm6, %%mm4			\n\t" /* A0-B0		a0-b0 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
	WRITE0b(%%mm6, %%mm4, dst) \
\
	"movq 56(%2), %%mm4			\n\t" /* -C2	-C4	-C2	-C4 */\
	"pmaddwd %%mm1, %%mm4			\n\t" /* -C2R6-C4R4	-C2r6-C4r4 */\
	"movq 64(%2), %%mm6			\n\t" /* -C7	C3	-C7	C3 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* -C7R3+C3R1	-C7r3+C3r1 */\
	"movq 72(%2), %%mm7			\n\t" /* -C5	-C1	-C5	-C1 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* -C5R7-C1R5	-C5r7-C1r5 */\
	"paddd %%mm5, %%mm4			\n\t" /* A1		a1 */\
\
	"movq 80(%2), %%mm5			\n\t" /* -C6	C4	-C6	C4 */\
	"pmaddwd %%mm0, %%mm5			\n\t" /* -C6R2+C4R0	-C6r2+C4r0 */\
	"paddd %%mm7, %%mm6			\n\t" /* B1		b1 */\
	"paddd %%mm4, %%mm6			\n\t" /* A1+B1		a1+b1 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A1		2a1 */\
	"psubd %%mm6, %%mm4			\n\t" /* A1-B1		a1-b1 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
	WRITE1b(%%mm6, %%mm4, dst, %%mm7) \
\
	"movq 88(%2), %%mm4			\n\t" /* C2	-C4	C2	-C4 */\
	"pmaddwd %%mm1, %%mm4			\n\t" /* C2R6-C4R4	C2r6-C4r4 */\
	"movq 96(%2), %%mm6			\n\t" /* -C1	C5	-C1	C5 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* -C1R3+C5R1	-C1r3+C5r1 */\
	"movq 104(%2), %%mm7			\n\t" /* C3	C7	C3	C7 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* C3R7+C7R5	C3r7+C7r5 */\
	"paddd %%mm5, %%mm4			\n\t" /* A2		a2 */\
\
	"pmaddwd 112(%2), %%mm0			\n\t" /* -C2R2+C4R0	-C2r2+C4r0 */\
	"paddd %%mm7, %%mm6			\n\t" /* B1		b1 */\
	"paddd %%mm4, %%mm6			\n\t" /* A1+B1		a1+b1 */\
	"pmaddwd 120(%2), %%mm1			\n\t" /* -C6R6+C4R4	-C6r6+C4r4 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A1		2a1 */\
	"psubd %%mm6, %%mm4			\n\t" /* A1-B1		a1-b1 */\
	"pmaddwd 128(%2), %%mm2			\n\t" /* -C5R3+C7R1	-C5r3+C7r1 */\
	"pmaddwd 136(%2), %%mm3			\n\t" /* -C1R7+C3R5	-C1r7+C3r5 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
\
	"paddd %%mm1, %%mm0			\n\t" /* A3		a3 */\
	"paddd %%mm3, %%mm2			\n\t" /* B3		b3 */\
	"paddd %%mm0, %%mm2			\n\t" /* A3+B3		a3+b3 */\
	"paddd %%mm0, %%mm0			\n\t" /* 2A3		2a3 */\
	"psubd %%mm2, %%mm0			\n\t" /* A3-B3		a3-b3 */\
	"psrad $" #shift ", %%mm2		\n\t"\
	"psrad $" #shift ", %%mm0		\n\t"\
	WRITE2b(%%mm6, %%mm4, %%mm2, %%mm0, dst)

//IDCT_CORE(  src0,   src4,   src1,    src5,    dst, rounder, shift)
IDCT_CORE(    (%1), 64(%1), 32(%1),  96(%1),  0(%0),/nop, 20)
IDCT_CORE(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0),/nop, 20)
IDCT_CORE(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0),/nop, 20)
IDCT_CORE(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0),/nop, 20)
	"jmp 9f					\n\t"

	"#.balign 16				\n\t"\
	"4:					\n\t"
Z_COND_IDCT_CORE(  64(%0), 72(%0), 80(%0), 88(%0), 64(%1),paddd (%2), 11, 6f)
Z_COND_IDCT_CORE(  96(%0),104(%0),112(%0),120(%0), 96(%1),paddd (%2), 11, 5f)

#undef IDCT_CORE
#define IDCT_CORE(src0, src4, src1, src5, dst, rounder, shift) \
	"movq " #src0 ", %%mm0			\n\t" /* R2	R0	r2	r0 */\
	"movq " #src4 ", %%mm1			\n\t" /* R6	R4	r6	r4 */\
	"movq " #src5 ", %%mm3			\n\t" /* R7	R5	r7	r5 */\
	"movq 16(%2), %%mm4			\n\t" /* C2	C4	C2	C4 */\
	"pmaddwd %%mm0, %%mm4			\n\t" /* C2R2+C4R0	C2r2+C4r0 */\
	"movq 24(%2), %%mm5			\n\t" /* C6	C4	C6	C4 */\
	"pmaddwd %%mm1, %%mm5			\n\t" /* C6R6+C4R4	C6r6+C4r4 */\
	"movq 40(%2), %%mm7			\n\t" /* C7	C5	C7	C5 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* C7R7+C5R5	C7r7+C5r5 */\
	"paddd %%mm5, %%mm4			\n\t" /* A0		a0 */\
\
	"movq 48(%2), %%mm5			\n\t" /* C6	C4	C6	C4 */\
	"pmaddwd %%mm0, %%mm5			\n\t" /* C6R2+C4R0	C6r2+C4r0 */\
	"paddd %%mm4, %%mm7			\n\t" /* A0+B0		a0+b0 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A0		2a0 */\
	"psubd %%mm7, %%mm4			\n\t" /* A0-B0		a0-b0 */\
	"psrad $" #shift ", %%mm7		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
	WRITE0b(%%mm7, %%mm4, dst) \
\
	"movq 56(%2), %%mm4			\n\t" /* -C2	-C4	-C2	-C4 */\
	"pmaddwd %%mm1, %%mm4			\n\t" /* -C2R6-C4R4	-C2r6-C4r4 */\
	"movq 72(%2), %%mm7			\n\t" /* -C5	-C1	-C5	-C1 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* -C5R7-C1R5	-C5r7-C1r5 */\
	"paddd %%mm5, %%mm4			\n\t" /* A1		a1 */\
\
	"movq 80(%2), %%mm5			\n\t" /* -C6	C4	-C6	C4 */\
	"pmaddwd %%mm0, %%mm5			\n\t" /* -C6R2+C4R0	-C6r2+C4r0 */\
	"paddd %%mm4, %%mm7			\n\t" /* A1+B1		a1+b1 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A1		2a1 */\
	"psubd %%mm7, %%mm4			\n\t" /* A1-B1		a1-b1 */\
	"psrad $" #shift ", %%mm7		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
	WRITE1b(%%mm7, %%mm4, dst, %%mm6) \
\
	"movq 88(%2), %%mm4			\n\t" /* C2	-C4	C2	-C4 */\
	"pmaddwd %%mm1, %%mm4			\n\t" /* C2R6-C4R4	C2r6-C4r4 */\
	"movq 104(%2), %%mm7			\n\t" /* C3	C7	C3	C7 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* C3R7+C7R5	C3r7+C7r5 */\
	"paddd %%mm5, %%mm4			\n\t" /* A2		a2 */\
\
	"pmaddwd 112(%2), %%mm0			\n\t" /* -C2R2+C4R0	-C2r2+C4r0 */\
	"paddd %%mm4, %%mm7			\n\t" /* A1+B1		a1+b1 */\
	"pmaddwd 120(%2), %%mm1			\n\t" /* -C6R6+C4R4	-C6r6+C4r4 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A1		2a1 */\
	"psubd %%mm7, %%mm4			\n\t" /* A1-B1		a1-b1 */\
	"pmaddwd 136(%2), %%mm3			\n\t" /* -C1R7+C3R5	-C1r7+C3r5 */\
	"psrad $" #shift ", %%mm7		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
\
	"paddd %%mm1, %%mm0			\n\t" /* A3		a3 */\
	"paddd %%mm0, %%mm3			\n\t" /* A3+B3		a3+b3 */\
	"paddd %%mm0, %%mm0			\n\t" /* 2A3		2a3 */\
	"psubd %%mm3, %%mm0			\n\t" /* A3-B3		a3-b3 */\
	"psrad $" #shift ", %%mm3		\n\t"\
	"psrad $" #shift ", %%mm0		\n\t"\
	WRITE2b(%%mm7, %%mm4, %%mm3, %%mm0, dst)

//IDCT_CORE(  src0,   src4,   src1,    src5,    dst, rounder, shift)
IDCT_CORE(    (%1), 64(%1), 32(%1),  96(%1),  0(%0),/nop, 20)
IDCT_CORE(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0),/nop, 20)
IDCT_CORE(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0),/nop, 20)
IDCT_CORE(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0),/nop, 20)
	"jmp 9f					\n\t"

	"#.balign 16				\n\t"\
	"6:					\n\t"
Z_COND_IDCT_CORE(  96(%0),104(%0),112(%0),120(%0), 96(%1),paddd (%2), 11, 7f)

#undef IDCT_CORE
#define IDCT_CORE(src0, src4, src1, src5, dst, rounder, shift) \
	"movq " #src0 ", %%mm0			\n\t" /* R2	R0	r2	r0 */\
	"movq " #src5 ", %%mm3			\n\t" /* R7	R5	r7	r5 */\
	"movq 16(%2), %%mm4			\n\t" /* C2	C4	C2	C4 */\
	"pmaddwd %%mm0, %%mm4			\n\t" /* C2R2+C4R0	C2r2+C4r0 */\
	"movq 40(%2), %%mm7			\n\t" /* C7	C5	C7	C5 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* C7R7+C5R5	C7r7+C5r5 */\
\
	"movq 48(%2), %%mm5			\n\t" /* C6	C4	C6	C4 */\
	"pmaddwd %%mm0, %%mm5			\n\t" /* C6R2+C4R0	C6r2+C4r0 */\
	"paddd %%mm4, %%mm7			\n\t" /* A0+B0		a0+b0 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A0		2a0 */\
	"psubd %%mm7, %%mm4			\n\t" /* A0-B0		a0-b0 */\
	"psrad $" #shift ", %%mm7		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
	WRITE0b(%%mm7, %%mm4, dst) \
\
	"movq 72(%2), %%mm7			\n\t" /* -C5	-C1	-C5	-C1 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* -C5R7-C1R5	-C5r7-C1r5 */\
\
	"movq 80(%2), %%mm4			\n\t" /* -C6	C4	-C6	C4 */\
	"pmaddwd %%mm0, %%mm4			\n\t" /* -C6R2+C4R0	-C6r2+C4r0 */\
	"paddd %%mm5, %%mm7			\n\t" /* A1+B1		a1+b1 */\
	"paddd %%mm5, %%mm5			\n\t" /* 2A1		2a1 */\
	"psubd %%mm7, %%mm5			\n\t" /* A1-B1		a1-b1 */\
	"psrad $" #shift ", %%mm7		\n\t"\
	"psrad $" #shift ", %%mm5		\n\t"\
	WRITE1b(%%mm7, %%mm5, dst, %%mm6) \
\
	"movq 104(%2), %%mm7			\n\t" /* C3	C7	C3	C7 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* C3R7+C7R5	C3r7+C7r5 */\
\
	"pmaddwd 112(%2), %%mm0			\n\t" /* -C2R2+C4R0	-C2r2+C4r0 */\
	"paddd %%mm4, %%mm7			\n\t" /* A1+B1		a1+b1 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A1		2a1 */\
	"psubd %%mm7, %%mm4			\n\t" /* A1-B1		a1-b1 */\
	"pmaddwd 136(%2), %%mm3			\n\t" /* -C1R7+C3R5	-C1r7+C3r5 */\
	"psrad $" #shift ", %%mm7		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
\
	"paddd %%mm0, %%mm3			\n\t" /* A3+B3		a3+b3 */\
	"paddd %%mm0, %%mm0			\n\t" /* 2A3		2a3 */\
	"psubd %%mm3, %%mm0			\n\t" /* A3-B3		a3-b3 */\
	"psrad $" #shift ", %%mm3		\n\t"\
	"psrad $" #shift ", %%mm0		\n\t"\
	WRITE2b(%%mm7, %%mm4, %%mm3, %%mm0, dst)

//IDCT_CORE(  src0,   src4,   src1,    src5,    dst, rounder, shift)
IDCT_CORE(    (%1), 64(%1), 32(%1),  96(%1),  0(%0),/nop, 20)
IDCT_CORE(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0),/nop, 20)
IDCT_CORE(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0),/nop, 20)
IDCT_CORE(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0),/nop, 20)
	"jmp 9f					\n\t"

	"#.balign 16				\n\t"\
	"2:					\n\t"
Z_COND_IDCT_CORE(  96(%0),104(%0),112(%0),120(%0), 96(%1),paddd (%2), 11, 3f)

#undef IDCT_CORE
#define IDCT_CORE(src0, src4, src1, src5, dst, rounder, shift) \
	"movq " #src0 ", %%mm0			\n\t" /* R2	R0	r2	r0 */\
	"movq " #src1 ", %%mm2			\n\t" /* R3	R1	r3	r1 */\
	"movq " #src5 ", %%mm3			\n\t" /* R7	R5	r7	r5 */\
	"movq 16(%2), %%mm4			\n\t" /* C2	C4	C2	C4 */\
	"pmaddwd %%mm0, %%mm4			\n\t" /* C2R2+C4R0	C2r2+C4r0 */\
	"movq 32(%2), %%mm6			\n\t" /* C3	C1	C3	C1 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* C3R3+C1R1	C3r3+C1r1 */\
	"movq 40(%2), %%mm7			\n\t" /* C7	C5	C7	C5 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* C7R7+C5R5	C7r7+C5r5 */\
\
	"movq 48(%2), %%mm5			\n\t" /* C6	C4	C6	C4 */\
	"pmaddwd %%mm0, %%mm5			\n\t" /* C6R2+C4R0	C6r2+C4r0 */\
	"paddd %%mm7, %%mm6			\n\t" /* B0		b0 */\
	"paddd %%mm4, %%mm6			\n\t" /* A0+B0		a0+b0 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A0		2a0 */\
	"psubd %%mm6, %%mm4			\n\t" /* A0-B0		a0-b0 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
	WRITE0b(%%mm6, %%mm4, dst) \
\
	"movq 64(%2), %%mm6			\n\t" /* -C7	C3	-C7	C3 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* -C7R3+C3R1	-C7r3+C3r1 */\
	"movq 72(%2), %%mm7			\n\t" /* -C5	-C1	-C5	-C1 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* -C5R7-C1R5	-C5r7-C1r5 */\
\
	"movq 80(%2), %%mm4			\n\t" /* -C6	C4	-C6	C4 */\
	"pmaddwd %%mm0, %%mm4			\n\t" /* -C6R2+C4R0	-C6r2+C4r0 */\
	"paddd %%mm7, %%mm6			\n\t" /* B1		b1 */\
	"paddd %%mm5, %%mm6			\n\t" /* A1+B1		a1+b1 */\
	"paddd %%mm5, %%mm5			\n\t" /* 2A1		2a1 */\
	"psubd %%mm6, %%mm5			\n\t" /* A1-B1		a1-b1 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm5		\n\t"\
	WRITE1b(%%mm6, %%mm5, dst, %%mm7) \
\
	"movq 96(%2), %%mm6			\n\t" /* -C1	C5	-C1	C5 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* -C1R3+C5R1	-C1r3+C5r1 */\
	"movq 104(%2), %%mm7			\n\t" /* C3	C7	C3	C7 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /* C3R7+C7R5	C3r7+C7r5 */\
\
	"pmaddwd 112(%2), %%mm0			\n\t" /* -C2R2+C4R0	-C2r2+C4r0 */\
	"paddd %%mm7, %%mm6			\n\t" /* B1		b1 */\
	"paddd %%mm4, %%mm6			\n\t" /* A1+B1		a1+b1 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A1		2a1 */\
	"psubd %%mm6, %%mm4			\n\t" /* A1-B1		a1-b1 */\
	"pmaddwd 128(%2), %%mm2			\n\t" /* -C5R3+C7R1	-C5r3+C7r1 */\
	"pmaddwd 136(%2), %%mm3			\n\t" /* -C1R7+C3R5	-C1r7+C3r5 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
\
	"paddd %%mm3, %%mm2			\n\t" /* B3		b3 */\
	"paddd %%mm0, %%mm2			\n\t" /* A3+B3		a3+b3 */\
	"paddd %%mm0, %%mm0			\n\t" /* 2A3		2a3 */\
	"psubd %%mm2, %%mm0			\n\t" /* A3-B3		a3-b3 */\
	"psrad $" #shift ", %%mm2		\n\t"\
	"psrad $" #shift ", %%mm0		\n\t"\
	WRITE2b(%%mm6, %%mm4, %%mm2, %%mm0, dst)

//IDCT_CORE(  src0,   src4,   src1,    src5,    dst, rounder, shift)
IDCT_CORE(    (%1), 64(%1), 32(%1),  96(%1),  0(%0),/nop, 20)
IDCT_CORE(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0),/nop, 20)
IDCT_CORE(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0),/nop, 20)
IDCT_CORE(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0),/nop, 20)
	"jmp 9f					\n\t"

	"#.balign 16				\n\t"\
	"3:					\n\t"
#undef IDCT_CORE
#define IDCT_CORE(src0, src4, src1, src5, dst, rounder, shift) \
	"movq " #src0 ", %%mm0			\n\t" /* R2	R0	r2	r0 */\
	"movq " #src1 ", %%mm2			\n\t" /* R3	R1	r3	r1 */\
	"movq 16(%2), %%mm4			\n\t" /* C2	C4	C2	C4 */\
	"pmaddwd %%mm0, %%mm4			\n\t" /* C2R2+C4R0	C2r2+C4r0 */\
	"movq 32(%2), %%mm6			\n\t" /* C3	C1	C3	C1 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* C3R3+C1R1	C3r3+C1r1 */\
\
	"movq 48(%2), %%mm5			\n\t" /* C6	C4	C6	C4 */\
	"pmaddwd %%mm0, %%mm5			\n\t" /* C6R2+C4R0	C6r2+C4r0 */\
	"paddd %%mm4, %%mm6			\n\t" /* A0+B0		a0+b0 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A0		2a0 */\
	"psubd %%mm6, %%mm4			\n\t" /* A0-B0		a0-b0 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
	WRITE0b(%%mm6, %%mm4, dst) \
\
	"movq 64(%2), %%mm6			\n\t" /* -C7	C3	-C7	C3 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* -C7R3+C3R1	-C7r3+C3r1 */\
\
	"movq 80(%2), %%mm4			\n\t" /* -C6	C4	-C6	C4 */\
	"pmaddwd %%mm0, %%mm4			\n\t" /* -C6R2+C4R0	-C6r2+C4r0 */\
	"paddd %%mm5, %%mm6			\n\t" /* A1+B1		a1+b1 */\
	"paddd %%mm5, %%mm5			\n\t" /* 2A1		2a1 */\
	"psubd %%mm6, %%mm5			\n\t" /* A1-B1		a1-b1 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm5		\n\t"\
	WRITE1b(%%mm6, %%mm5, dst, %%mm7) \
\
	"movq 96(%2), %%mm6			\n\t" /* -C1	C5	-C1	C5 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* -C1R3+C5R1	-C1r3+C5r1 */\
\
	"pmaddwd 112(%2), %%mm0			\n\t" /* -C2R2+C4R0	-C2r2+C4r0 */\
	"paddd %%mm4, %%mm6			\n\t" /* A1+B1		a1+b1 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A1		2a1 */\
	"psubd %%mm6, %%mm4			\n\t" /* A1-B1		a1-b1 */\
	"pmaddwd 128(%2), %%mm2			\n\t" /* -C5R3+C7R1	-C5r3+C7r1 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
\
	"paddd %%mm0, %%mm2			\n\t" /* A3+B3		a3+b3 */\
	"paddd %%mm0, %%mm0			\n\t" /* 2A3		2a3 */\
	"psubd %%mm2, %%mm0			\n\t" /* A3-B3		a3-b3 */\
	"psrad $" #shift ", %%mm2		\n\t"\
	"psrad $" #shift ", %%mm0		\n\t"\
	WRITE2b(%%mm6, %%mm4, %%mm2, %%mm0, dst)

//IDCT_CORE(  src0,   src4,   src1,    src5,    dst, rounder, shift)
IDCT_CORE(    (%1), 64(%1), 32(%1),  96(%1),  0(%0),/nop, 20)
IDCT_CORE(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0),/nop, 20)
IDCT_CORE(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0),/nop, 20)
IDCT_CORE(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0),/nop, 20)
	"jmp 9f					\n\t"

	"#.balign 16				\n\t"\
	"5:					\n\t"
#undef IDCT_CORE
#define IDCT_CORE(src0, src4, src1, src5, dst, rounder, shift) \
	"movq " #src0 ", %%mm0			\n\t" /* R2	R0	r2	r0 */\
	"movq 16(%2), %%mm4			\n\t" /* C2	C4	C2	C4 */\
	"movq %%mm4, %%mm6\n\t"\
	"pmaddwd %%mm0, %%mm4			\n\t" /* C2R2+C4R0	C2r2+C4r0 */\
	"movq " #src4 ", %%mm1			\n\t" /* R6	R4	r6	r4 */\
	"movq 24(%2), %%mm5			\n\t" /* C6	C4	C6	C4 */\
	"movq %%mm5, %%mm7\n\t"\
	"pmaddwd %%mm1, %%mm5			\n\t" /* C6R6+C4R4	C6r6+C4r4 */\
	"movq 8+" #src0 ", %%mm2		\n\t" /*2R2	R0	r2	r0 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /*2C2R2+C4R0	C2r2+C4r0 */\
	"movq 8+" #src4 ", %%mm3		\n\t" /*2R6	R4	r6	r4 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /*2C6R6+C4R4	C6r6+C4r4 */\
\
	"paddd %%mm5, %%mm4			\n\t" /* A0		a0 */\
	"movq 48(%2), %%mm5			\n\t" /* C6	C4	C6	C4 */\
	"psrad $" #shift ", %%mm4		\n\t"\
	"pmaddwd %%mm0, %%mm5			\n\t" /* C6R2+C4R0	C6r2+C4r0 */\
\
	"paddd %%mm7, %%mm6			\n\t" /*2A0		a0 */\
	"movq 56(%2), %%mm7			\n\t" /* -C2	-C4	-C2	-C4 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"pmaddwd %%mm1, %%mm7			\n\t" /* -C2R6-C4R4	-C2r6-C4r4 */\
\
	"packssdw %%mm6, %%mm4			\n\t" /* C0, c0, C0, c0 */\
	"movq 48(%2), %%mm6			\n\t" /* C6	C4	C6	C4 */\
	"movq %%mm4, " #dst "			\n\t" /* C0, c0 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /*2C6R2+C4R0	C6r2+C4r0 */\
\
	"movq %%mm4, 112+" #dst "		\n\t" /* C0, c0 */\
	"movq 56(%2), %%mm4			\n\t" /* -C2	-C4	-C2	-C4 */\
	"pmaddwd %%mm3, %%mm4			\n\t" /*2-C2R6-C4R4	-C2r6-C4r4 */\
\
	"paddd %%mm5, %%mm7			\n\t" /* A1		a1 */\
	"movq 80(%2), %%mm5			\n\t" /* -C6	C4	-C6	C4 */\
	"psrad $" #shift ", %%mm7		\n\t"\
	"pmaddwd %%mm0, %%mm5			\n\t" /* -C6R2+C4R0	-C6r2+C4r0 */\
\
	"paddd %%mm4, %%mm6			\n\t" /*2A1		a1 */\
	"pmaddwd 112(%2), %%mm0			\n\t" /* -C2R2+C4R0	-C2r2+C4r0 */\
\
	"psrad $" #shift ", %%mm6		\n\t"\
	"movq 88(%2), %%mm4			\n\t" /* C2	-C4	C2	-C4 */\
	"pmaddwd %%mm1, %%mm4			\n\t" /* C2R6-C4R4	C2r6-C4r4 */\
\
	"pmaddwd 120(%2), %%mm1			\n\t" /* -C6R6+C4R4	-C6r6+C4r4 */\
	"packssdw %%mm6, %%mm7			\n\t" /* C1, c1, C1, c1 */\
\
	"movq 80(%2), %%mm6			\n\t" /* -C6	C4	-C6	C4 */\
	"movq %%mm7, 16+" #dst "		\n\t" /* C1, c1 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /*2-C6R2+C4R0	-C6r2+C4r0 */\
\
	"movq %%mm7, 96+" #dst "		\n\t" /* C1, c1 */\
	"movq 88(%2), %%mm7			\n\t" /* C2	-C4	C2	-C4 */\
	"pmaddwd %%mm3, %%mm7			\n\t" /*2C2R6-C4R4	C2r6-C4r4 */\
\
	"pmaddwd 112(%2), %%mm2			\n\t" /*2-C2R2+C4R0	-C2r2+C4r0 */\
	"paddd %%mm5, %%mm4			\n\t" /* A2		a2 */\
\
	"pmaddwd 120(%2), %%mm3			\n\t" /*2-C6R6+C4R4	-C6r6+C4r4 */\
	"psrad $" #shift ", %%mm4		\n\t"\
\
	"paddd %%mm7, %%mm6			\n\t" /*2A2		a2 */\
	"paddd %%mm1, %%mm0			\n\t" /* A3		a3 */\
\
	"psrad $" #shift ", %%mm6		\n\t"\
\
	"packssdw %%mm6, %%mm4			\n\t" /* C2, c2, C2, c2 */\
	"movq %%mm4, 32+" #dst "		\n\t" /* C2, c2 */\
	"psrad $" #shift ", %%mm0		\n\t"\
	"paddd %%mm3, %%mm2			\n\t" /*2A3		a3 */\
\
	"movq %%mm4, 80+" #dst "		\n\t" /* C2, c2 */\
	"psrad $" #shift ", %%mm2		\n\t"\
\
	"packssdw %%mm2, %%mm0			\n\t" /* C3, c3, C3, c3 */\
	"movq %%mm0, 48+" #dst "		\n\t" /* C3, c3 */\
	"movq %%mm0, 64+" #dst "		\n\t" /* C3, c3 */\

//IDCT_CORE(  src0,   src4,   src1,    src5,    dst, rounder, shift)
IDCT_CORE(    0(%1), 64(%1), 32(%1),  96(%1),  0(%0),/nop, 20)
//IDCT_CORE(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0),/nop, 20)
IDCT_CORE(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0),/nop, 20)
//IDCT_CORE(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0),/nop, 20)
	"jmp 9f					\n\t"


	"#.balign 16				\n\t"\
	"1:					\n\t"
#undef IDCT_CORE
#define IDCT_CORE(src0, src4, src1, src5, dst, rounder, shift) \
	"movq " #src0 ", %%mm0			\n\t" /* R2	R0	r2	r0 */\
	"movq " #src4 ", %%mm1			\n\t" /* R6	R4	r6	r4 */\
	"movq " #src1 ", %%mm2			\n\t" /* R3	R1	r3	r1 */\
	"movq 16(%2), %%mm4			\n\t" /* C2	C4	C2	C4 */\
	"pmaddwd %%mm0, %%mm4			\n\t" /* C2R2+C4R0	C2r2+C4r0 */\
	"movq 24(%2), %%mm5			\n\t" /* C6	C4	C6	C4 */\
	"pmaddwd %%mm1, %%mm5			\n\t" /* C6R6+C4R4	C6r6+C4r4 */\
	"movq 32(%2), %%mm6			\n\t" /* C3	C1	C3	C1 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* C3R3+C1R1	C3r3+C1r1 */\
	"paddd %%mm5, %%mm4			\n\t" /* A0		a0 */\
\
	"movq 48(%2), %%mm5			\n\t" /* C6	C4	C6	C4 */\
	"pmaddwd %%mm0, %%mm5			\n\t" /* C6R2+C4R0	C6r2+C4r0 */\
	"paddd %%mm4, %%mm6			\n\t" /* A0+B0		a0+b0 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A0		2a0 */\
	"psubd %%mm6, %%mm4			\n\t" /* A0-B0		a0-b0 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
	WRITE0b(%%mm6, %%mm4, dst) \
\
	"movq 56(%2), %%mm4			\n\t" /* -C2	-C4	-C2	-C4 */\
	"pmaddwd %%mm1, %%mm4			\n\t" /* -C2R6-C4R4	-C2r6-C4r4 */\
	"movq 64(%2), %%mm6			\n\t" /* -C7	C3	-C7	C3 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* -C7R3+C3R1	-C7r3+C3r1 */\
	"paddd %%mm5, %%mm4			\n\t" /* A1		a1 */\
\
	"movq 80(%2), %%mm5			\n\t" /* -C6	C4	-C6	C4 */\
	"pmaddwd %%mm0, %%mm5			\n\t" /* -C6R2+C4R0	-C6r2+C4r0 */\
	"paddd %%mm4, %%mm6			\n\t" /* A1+B1		a1+b1 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A1		2a1 */\
	"psubd %%mm6, %%mm4			\n\t" /* A1-B1		a1-b1 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
	WRITE1b(%%mm6, %%mm4, dst, %%mm7) \
\
	"movq 88(%2), %%mm4			\n\t" /* C2	-C4	C2	-C4 */\
	"pmaddwd %%mm1, %%mm4			\n\t" /* C2R6-C4R4	C2r6-C4r4 */\
	"movq 96(%2), %%mm6			\n\t" /* -C1	C5	-C1	C5 */\
	"pmaddwd %%mm2, %%mm6			\n\t" /* -C1R3+C5R1	-C1r3+C5r1 */\
	"paddd %%mm5, %%mm4			\n\t" /* A2		a2 */\
\
	"pmaddwd 112(%2), %%mm0			\n\t" /* -C2R2+C4R0	-C2r2+C4r0 */\
	"paddd %%mm4, %%mm6			\n\t" /* A1+B1		a1+b1 */\
	"pmaddwd 120(%2), %%mm1			\n\t" /* -C6R6+C4R4	-C6r6+C4r4 */\
	"paddd %%mm4, %%mm4			\n\t" /* 2A1		2a1 */\
	"psubd %%mm6, %%mm4			\n\t" /* A1-B1		a1-b1 */\
	"pmaddwd 128(%2), %%mm2			\n\t" /* -C5R3+C7R1	-C5r3+C7r1 */\
	"psrad $" #shift ", %%mm6		\n\t"\
	"psrad $" #shift ", %%mm4		\n\t"\
\
	"paddd %%mm1, %%mm0			\n\t" /* A3		a3 */\
	"paddd %%mm0, %%mm2			\n\t" /* A3+B3		a3+b3 */\
	"paddd %%mm0, %%mm0			\n\t" /* 2A3		2a3 */\
	"psubd %%mm2, %%mm0			\n\t" /* A3-B3		a3-b3 */\
	"psrad $" #shift ", %%mm2		\n\t"\
	"psrad $" #shift ", %%mm0		\n\t"\
	WRITE2b(%%mm6, %%mm4, %%mm2, %%mm0, dst)

//IDCT_CORE(  src0,   src4,   src1,    src5,    dst, rounder, shift)
IDCT_CORE(    (%1), 64(%1), 32(%1),  96(%1),  0(%0),/nop, 20)
IDCT_CORE(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0),/nop, 20)
IDCT_CORE(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0),/nop, 20)
IDCT_CORE(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0),/nop, 20)
	"jmp 9f					\n\t"


	"#.balign 16				\n\t"
	"7:					\n\t"
#undef IDCT_CORE
#define IDCT_CORE(src0, src4, src1, src5, dst, rounder, shift) \
	"movq " #src0 ", %%mm0			\n\t" /* R2	R0	r2	r0 */\
	"movq 16(%2), %%mm2			\n\t" /* C2	C4	C2	C4 */\
	"movq 8+" #src0 ", %%mm1		\n\t" /* R2	R0	r2	r0 */\
	"pmaddwd %%mm0, %%mm2			\n\t" /* C2R2+C4R0	C2r2+C4r0 */\
	"movq 16(%2), %%mm3			\n\t" /* C2	C4	C2	C4 */\
	"pmaddwd %%mm1, %%mm3			\n\t" /* C2R2+C4R0	C2r2+C4r0 */\
\
	"movq 48(%2), %%mm4			\n\t" /* C6	C4	C6	C4 */\
	"pmaddwd %%mm0, %%mm4			\n\t" /* C6R2+C4R0	C6r2+C4r0 */\
	"movq 48(%2), %%mm5			\n\t" /* C6	C4	C6	C4 */\
	"pmaddwd %%mm1, %%mm5			\n\t" /* C6R2+C4R0	C6r2+C4r0 */\
	"movq 80(%2), %%mm6			\n\t" /* -C6	C4	-C6	C4 */\
	"pmaddwd %%mm0, %%mm6			\n\t" /* -C6R2+C4R0	-C6r2+C4r0 */\
	"movq 80(%2), %%mm7			\n\t" /* -C6	C4	-C6	C4 */\
	"pmaddwd %%mm1, %%mm7			\n\t" /* -C6R2+C4R0	-C6r2+C4r0 */\
	"pmaddwd 112(%2), %%mm0			\n\t" /* -C2R2+C4R0	-C2r2+C4r0 */\
	"psrad $" #shift ", %%mm2		\n\t"\
	"psrad $" #shift ", %%mm3		\n\t"\
	"pmaddwd 112(%2), %%mm1			\n\t" /* -C2R2+C4R0	-C2r2+C4r0 */\
	"packssdw %%mm3, %%mm2			\n\t" /* C0, c0, C0, c0 */\
	"movq %%mm2, " #dst "			\n\t" /* C0, c0 */\
	"psrad $" #shift ", %%mm4		\n\t"\
	"psrad $" #shift ", %%mm5		\n\t"\
	"movq %%mm2, 112+" #dst "		\n\t" /* C0, c0 */\
	"packssdw %%mm5, %%mm4			\n\t" /* C1, c1, C1, c1 */\
	"movq %%mm4, 16+" #dst "		\n\t" /* C0, c0 */\
	"psrad $" #shift ", %%mm7		\n\t"\
	"psrad $" #shift ", %%mm6		\n\t"\
	"movq %%mm4, 96+" #dst "		\n\t" /* C0, c0 */\
	"packssdw %%mm7, %%mm6			\n\t" /* C2, c2, C2, c2 */\
	"movq %%mm6, 32+" #dst "		\n\t" /* C0, c0 */\
	"psrad $" #shift ", %%mm0		\n\t"\
	"movq %%mm6, 80+" #dst "		\n\t" /* C0, c0 */\
	"psrad $" #shift ", %%mm1		\n\t"\
	"packssdw %%mm1, %%mm0			\n\t" /* C3, c3, C3, c3 */\
	"movq %%mm0, 48+" #dst "		\n\t" /* C0, c0 */\
	"movq %%mm0, 64+" #dst "		\n\t" /* C0, c0 */\

//IDCT_CORE(  src0,   src4,   src1,    src5,    dst, rounder, shift)
IDCT_CORE(   0(%1), 64(%1), 32(%1),  96(%1),  0(%0),/nop, 20)
//IDCT_CORE(   8(%1), 72(%1), 40(%1), 104(%1),  4(%0),/nop, 20)
IDCT_CORE(  16(%1), 80(%1), 48(%1), 112(%1),  8(%0),/nop, 20)
//IDCT_CORE(  24(%1), 88(%1), 56(%1), 120(%1), 12(%0),/nop, 20)


#endif

/*
Input
 00 20 02 22 40 60 42 62
 10 30 12 32 50 70 52 72
 01 21 03 23 41 61 43 63
 11 31 13 33 51 71 53 73
 04 24 06 26 44 64 46 66
 14 34 16 36 54 74 56 76
...
*/
/*
Temp
 00 02 10 12 20 22 30 32
 40 42 50 52 60 62 70 72
 01 03 11 13 21 23 31 33
 41 43 51 53 61 63 71 73
 04 06 14 16 24 26 34 36
 44 46 54 56 64 66 74 76
 05 07 15 17 25 27 35 37
 45 47 55 57 65 67 75 77
*/

/*
Output
 00 10 20 30 40 50 60 70
 01 11 21 31 41 51 61 71
...
*/

"9: \n\t"
		:: "r" (block), "r" (temp), "r" (coeffs)
		: "%eax"
	);
/*
idctCol(block, temp);
idctCol(block+1, temp+2);
idctCol(block+2, temp+4);
idctCol(block+3, temp+6);
idctCol(block+4, temp+8);
idctCol(block+5, temp+10);
idctCol(block+6, temp+12);
idctCol(block+7, temp+14);
*/
}

void simple_idct_mmx(int16_t *block)
{
	static int imax=0, imin=0;
	static int omax=0, omin=0;
	int i, j;
/*
	for(i=0; i<64; i++)
	{
		if(block[i] > imax)
		{
			imax= block[i];
			printf("Input-Max: %d\n", imax);
			printf("Input-Min: %d\n", imin);
			printf("Output-Max: %d\n", omax);
			printf("Output-Min: %d\n", omin);
		}
		if(block[i] < imin)
		{
			imin= block[i];
			printf("Input-Max: %d\n", imax);
			printf("Input-Min: %d\n", imin);
			printf("Output-Max: %d\n", omax);
			printf("Output-Min: %d\n", omin);
		}
	}*/
/*	static int stat[64];
	for(j=0; j<4; j++)
	{
		static int line[8]={0,2,1,3,4,6,5,7};
		for(i=0; i<16; i++)
		{
			if(block[j*16+i])
			{
				stat[j*16+1]++;
				break;
			}
		}
		for(i=0; i<16; i++)
		{
			if(block[j*16+i] && i!=0 && i!=2)
			{
				stat[j*16+2]++;
				break;
			}
		}
	}
	stat[0]++;*/
/*	for(i=1; i<8; i++)
	{
		if(block[i] != 0)
		{
			stat[1]++;
			break;
		}
	}
	for(i=32; i<64; i++)
	{
		if(block[i] != 0)
		{
			stat[2]++;
			break;
		}
	}
	stat[0]++;
*/
//	return;
	idct(block);
//	memset(block, 0, 128);
/*
	if(stat[0] > 100000)
		for(i=0; i<64; i++)
		{
			if((i&7) == 0) printf("\n");
			printf("%06d ", stat[i]);
		}
*/
/*
	for(i=0; i<4; i++) printf("%d", stat[1+i*16]);
	printf("  ");
	for(i=0; i<4; i++) printf("%d", stat[2+i*16]);
	printf("\n");
*/
//	printf("%d", stat[2]);

//	memset(stat, 0, 256);

/*
	for(i=0; i<64; i++)
	{
		if(block[i] > omax)
		{
			omax= block[i];
			printf("Input-Max: %d\n", imax);
			printf("Input-Min: %d\n", imin);
			printf("Output-Max: %d\n", omax);
			printf("Output-Min: %d\n", omin);
		}
		if(block[i] < omin)
		{
			omin= block[i];
			printf("Input-Max: %d\n", imax);
			printf("Input-Min: %d\n", imin);
			printf("Output-Max: %d\n", omax);
			printf("Output-Min: %d\n", omin);
		}
	}*/
}
