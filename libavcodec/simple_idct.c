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
  based upon some outcommented c code from mpeg2dec (idct_mmx.c written by Aaron Holtzman <aholtzma@ess.engr.uvic.ca>)
*/

#include <inttypes.h>

#include "simple_idct.h"
#include "../config.h"

#if 0
#define W1 2841 /* 2048*sqrt (2)*cos (1*pi/16) */
#define W2 2676 /* 2048*sqrt (2)*cos (2*pi/16) */
#define W3 2408 /* 2048*sqrt (2)*cos (3*pi/16) */
#define W4 2048 /* 2048*sqrt (2)*cos (4*pi/16) */
#define W5 1609 /* 2048*sqrt (2)*cos (5*pi/16) */
#define W6 1108 /* 2048*sqrt (2)*cos (6*pi/16) */
#define W7 565  /* 2048*sqrt (2)*cos (7*pi/16) */
#define ROW_SHIFT 8
#define COL_SHIFT 17
#else
#define W1  22725  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W2  21407  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W3  19266  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W4  16384  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W5  12873  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W6  8867   //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W7  4520   //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define ROW_SHIFT 11
#define COL_SHIFT 20 // 6
#endif

/* 8x8 Matrix used to do a trivial (slow) 8 point IDCT */
static int coeff[64]={
	W4, W4, W4, W4, W4, W4, W4, W4,
	W1, W3, W5, W7,-W7,-W5,-W3,-W1,
	W2, W6,-W6,-W2,-W2,-W6, W6, W2,
	W3,-W7,-W1,-W5, W5, W1, W7,-W3,
	W4,-W4,-W4, W4, W4,-W4,-W4, W4,
	W5,-W1, W7, W3,-W3,-W7, W1,-W5,
	W6,-W2, W2,-W6,-W6, W2,-W2, W6,
	W7,-W5, W3,-W1, W1,-W3, W5,-W7
};

static int inline idctRowCondZ (int16_t * row)
{
	int a0, a1, a2, a3, b0, b1, b2, b3;

	if( !( ((uint32_t*)row)[0]|((uint32_t*)row)[1] |((uint32_t*)row)[2] |((uint32_t*)row)[3])) {
/*		row[0] = row[1] = row[2] = row[3] = row[4] =
			row[5] = row[6] = row[7] = 0;*/
		return 0;
	}

	if(!( ((uint32_t*)row)[2] |((uint32_t*)row)[3] )){
		a0 = W4*row[0] + W2*row[2] + (1<<(ROW_SHIFT-1));
		a1 = W4*row[0] + W6*row[2] + (1<<(ROW_SHIFT-1));
		a2 = W4*row[0] - W6*row[2] + (1<<(ROW_SHIFT-1));
		a3 = W4*row[0] - W2*row[2] + (1<<(ROW_SHIFT-1));

		b0 = W1*row[1] + W3*row[3];
		b1 = W3*row[1] - W7*row[3];
		b2 = W5*row[1] - W1*row[3];
		b3 = W7*row[1] - W5*row[3];
	}else{
		a0 = W4*row[0] + W2*row[2] + W4*row[4] + W6*row[6] + (1<<(ROW_SHIFT-1));
		a1 = W4*row[0] + W6*row[2] - W4*row[4] - W2*row[6] + (1<<(ROW_SHIFT-1));
		a2 = W4*row[0] - W6*row[2] - W4*row[4] + W2*row[6] + (1<<(ROW_SHIFT-1));
		a3 = W4*row[0] - W2*row[2] + W4*row[4] - W6*row[6] + (1<<(ROW_SHIFT-1));

		b0 = W1*row[1] + W3*row[3] + W5*row[5] + W7*row[7];
		b1 = W3*row[1] - W7*row[3] - W1*row[5] - W5*row[7];
		b2 = W5*row[1] - W1*row[3] + W7*row[5] + W3*row[7];
		b3 = W7*row[1] - W5*row[3] + W3*row[5] - W1*row[7];
	}

	row[0] = (a0 + b0) >> ROW_SHIFT;
	row[1] = (a1 + b1) >> ROW_SHIFT;
	row[2] = (a2 + b2) >> ROW_SHIFT;
	row[3] = (a3 + b3) >> ROW_SHIFT;
	row[4] = (a3 - b3) >> ROW_SHIFT;
	row[5] = (a2 - b2) >> ROW_SHIFT;
	row[6] = (a1 - b1) >> ROW_SHIFT;
	row[7] = (a0 - b0) >> ROW_SHIFT;
	
	return 1;
}

#ifdef ARCH_ALPHA
/* 0: all entries 0, 1: only first entry nonzero, 2: otherwise  */
static int inline idctRowCondDC(int16_t *row)
{
	int_fast32_t a0, a1, a2, a3, b0, b1, b2, b3;
	uint64_t *lrow = (uint64_t *) row;

	if (lrow[1] == 0) {
		if (lrow[0] == 0)
			return 0;
		if ((lrow[0] & ~0xffffULL) == 0) {
			uint64_t v;

			a0 = W4 * row[0];
			a0 += 1 << (ROW_SHIFT - 1);
			a0 >>= ROW_SHIFT;
			v = (uint16_t) a0;
			v += v << 16;
			v += v << 32;
			lrow[0] = v;
			lrow[1] = v;

			return 1;
		}
	}

	a0 = W4 * row[0];
	a1 = W4 * row[0];
	a2 = W4 * row[0];
	a3 = W4 * row[0];

	if (row[2]) {
		a0 += W2 * row[2];
		a1 += W6 * row[2];
		a2 -= W6 * row[2];
		a3 -= W2 * row[2];
	}

	if (row[4]) {
		a0 += W4 * row[4];
		a1 -= W4 * row[4];
		a2 -= W4 * row[4];
		a3 += W4 * row[4];
	}

	if (row[6]) {
		a0 += W6 * row[6];
		a1 -= W2 * row[6];
		a2 += W2 * row[6];
		a3 -= W6 * row[6];
	}

	a0 += 1 << (ROW_SHIFT - 1);
	a1 += 1 << (ROW_SHIFT - 1);
	a2 += 1 << (ROW_SHIFT - 1);
	a3 += 1 << (ROW_SHIFT - 1);

	if (row[1]) {
		b0 = W1 * row[1];
		b1 = W3 * row[1];
		b2 = W5 * row[1];
		b3 = W7 * row[1];
	} else {
		b0 = 0;
		b1 = 0;
		b2 = 0;
		b3 = 0;
	}

	if (row[3]) {
		b0 += W3 * row[3];
		b1 -= W7 * row[3];
		b2 -= W1 * row[3];
		b3 -= W5 * row[3];
	}

	if (row[5]) {
		b0 += W5 * row[5];
		b1 -= W1 * row[5];
		b2 += W7 * row[5];
		b3 += W3 * row[5];
	}

	if (row[7]) {
		b0 += W7 * row[7];
		b1 -= W5 * row[7];
		b2 += W3 * row[7];
		b3 -= W1 * row[7];
	}

	row[0] = (a0 + b0) >> ROW_SHIFT;
	row[1] = (a1 + b1) >> ROW_SHIFT;
	row[2] = (a2 + b2) >> ROW_SHIFT;
	row[3] = (a3 + b3) >> ROW_SHIFT;
	row[4] = (a3 - b3) >> ROW_SHIFT;
	row[5] = (a2 - b2) >> ROW_SHIFT;
	row[6] = (a1 - b1) >> ROW_SHIFT;
	row[7] = (a0 - b0) >> ROW_SHIFT;

	return 2;
}
#else  /* not ARCH_ALPHA */
static int inline idctRowCondDC (int16_t * row)
{
	int a0, a1, a2, a3, b0, b1, b2, b3;

	if( !( ((uint32_t*)row)[1] |((uint32_t*)row)[2] |((uint32_t*)row)[3]| row[1])) {
//		row[0] = row[1] = row[2] = row[3] = row[4] = row[5] = row[6] = row[7] = row[0]<<3;
		uint16_t temp= row[0]<<3;
		((uint32_t*)row)[0]=((uint32_t*)row)[1]=
		((uint32_t*)row)[2]=((uint32_t*)row)[3]= temp + (temp<<16);
		return 0;
	}

	if(!( ((uint32_t*)row)[2] |((uint32_t*)row)[3] )){
		a0 = W4*row[0] + W2*row[2] + (1<<(ROW_SHIFT-1));
		a1 = W4*row[0] + W6*row[2] + (1<<(ROW_SHIFT-1));
		a2 = W4*row[0] - W6*row[2] + (1<<(ROW_SHIFT-1));
		a3 = W4*row[0] - W2*row[2] + (1<<(ROW_SHIFT-1));

		b0 = W1*row[1] + W3*row[3];
		b1 = W3*row[1] - W7*row[3];
		b2 = W5*row[1] - W1*row[3];
		b3 = W7*row[1] - W5*row[3];
	}else{
		a0 = W4*row[0] + W2*row[2] + W4*row[4] + W6*row[6] + (1<<(ROW_SHIFT-1));
		a1 = W4*row[0] + W6*row[2] - W4*row[4] - W2*row[6] + (1<<(ROW_SHIFT-1));
		a2 = W4*row[0] - W6*row[2] - W4*row[4] + W2*row[6] + (1<<(ROW_SHIFT-1));
		a3 = W4*row[0] - W2*row[2] + W4*row[4] - W6*row[6] + (1<<(ROW_SHIFT-1));

		b0 = W1*row[1] + W3*row[3] + W5*row[5] + W7*row[7];
		b1 = W3*row[1] - W7*row[3] - W1*row[5] - W5*row[7];
		b2 = W5*row[1] - W1*row[3] + W7*row[5] + W3*row[7];
		b3 = W7*row[1] - W5*row[3] + W3*row[5] - W1*row[7];
	}

	row[0] = (a0 + b0) >> ROW_SHIFT;
	row[7] = (a0 - b0) >> ROW_SHIFT;
	row[1] = (a1 + b1) >> ROW_SHIFT;
	row[6] = (a1 - b1) >> ROW_SHIFT;
	row[2] = (a2 + b2) >> ROW_SHIFT;
	row[5] = (a2 - b2) >> ROW_SHIFT;
	row[3] = (a3 + b3) >> ROW_SHIFT;
	row[4] = (a3 - b3) >> ROW_SHIFT;
	
	return 1;
}
#endif /* not ARCH_ALPHA */

static void inline idctCol (int16_t * col)
{

/*
	if( !(col[8*1] | col[8*2] |col[8*3] |col[8*4] |col[8*5] |col[8*6] | col[8*7])) {
		col[8*0] = col[8*1] = col[8*2] = col[8*3] = col[8*4] =
			col[8*5] = col[8*6] = col[8*7] = col[8*0]<<3;
		return;
	}*/

	int a0, a1, a2, a3, b0, b1, b2, b3;
	col[0] += (1<<(COL_SHIFT-1))/W4;
	a0 = W4*col[8*0] + W2*col[8*2] + W4*col[8*4] + W6*col[8*6];
	a1 = W4*col[8*0] + W6*col[8*2] - W4*col[8*4] - W2*col[8*6];
	a2 = W4*col[8*0] - W6*col[8*2] - W4*col[8*4] + W2*col[8*6];
	a3 = W4*col[8*0] - W2*col[8*2] + W4*col[8*4] - W6*col[8*6];

	b0 = W1*col[8*1] + W3*col[8*3] + W5*col[8*5] + W7*col[8*7];
	b1 = W3*col[8*1] - W7*col[8*3] - W1*col[8*5] - W5*col[8*7];
	b2 = W5*col[8*1] - W1*col[8*3] + W7*col[8*5] + W3*col[8*7];
	b3 = W7*col[8*1] - W5*col[8*3] + W3*col[8*5] - W1*col[8*7];

	col[8*0] = (a0 + b0) >> COL_SHIFT;
	col[8*7] = (a0 - b0) >> COL_SHIFT;
	col[8*1] = (a1 + b1) >> COL_SHIFT;
	col[8*6] = (a1 - b1) >> COL_SHIFT;
	col[8*2] = (a2 + b2) >> COL_SHIFT;
	col[8*5] = (a2 - b2) >> COL_SHIFT;
	col[8*3] = (a3 + b3) >> COL_SHIFT;
	col[8*4] = (a3 - b3) >> COL_SHIFT;
}

static void inline idctSparseCol (int16_t * col)
{
	int a0, a1, a2, a3, b0, b1, b2, b3;
	col[0] += (1<<(COL_SHIFT-1))/W4;
	a0 = W4*col[8*0];
	a1 = W4*col[8*0];
	a2 = W4*col[8*0];
	a3 = W4*col[8*0];

	if(col[8*2]){
		a0 +=  + W2*col[8*2];
		a1 +=  + W6*col[8*2];
		a2 +=  - W6*col[8*2];
		a3 +=  - W2*col[8*2];
	}

	if(col[8*4]){
		a0 += + W4*col[8*4];
		a1 += - W4*col[8*4];
		a2 += - W4*col[8*4];
		a3 += + W4*col[8*4];
	}

	if(col[8*6]){
		a0 += + W6*col[8*6];
		a1 += - W2*col[8*6];
		a2 += + W2*col[8*6];
		a3 += - W6*col[8*6];
	}

	if(col[8*1]){
		b0 = W1*col[8*1];
		b1 = W3*col[8*1];
		b2 = W5*col[8*1];
		b3 = W7*col[8*1];
	}else{
		b0 = 
		b1 = 
		b2 = 
		b3 = 0;
	}

	if(col[8*3]){
		b0 += + W3*col[8*3];
		b1 += - W7*col[8*3];
		b2 += - W1*col[8*3];
		b3 += - W5*col[8*3];
	}

	if(col[8*5]){
		b0 += + W5*col[8*5];
		b1 += - W1*col[8*5];
		b2 += + W7*col[8*5];
		b3 += + W3*col[8*5];
	}

	if(col[8*7]){
		b0 += + W7*col[8*7];
		b1 += - W5*col[8*7];
		b2 += + W3*col[8*7];
		b3 += - W1*col[8*7];
	}

#ifndef ARCH_ALPHA
	if(!(b0|b1|b2|b3)){
		col[8*0] = (a0) >> COL_SHIFT;
		col[8*7] = (a0) >> COL_SHIFT;
		col[8*1] = (a1) >> COL_SHIFT;
		col[8*6] = (a1) >> COL_SHIFT;
		col[8*2] = (a2) >> COL_SHIFT;
		col[8*5] = (a2) >> COL_SHIFT;
		col[8*3] = (a3) >> COL_SHIFT;
		col[8*4] = (a3) >> COL_SHIFT;
	}else{
#endif
		col[8*0] = (a0 + b0) >> COL_SHIFT;
		col[8*7] = (a0 - b0) >> COL_SHIFT;
		col[8*1] = (a1 + b1) >> COL_SHIFT;
		col[8*6] = (a1 - b1) >> COL_SHIFT;
		col[8*2] = (a2 + b2) >> COL_SHIFT;
		col[8*5] = (a2 - b2) >> COL_SHIFT;
		col[8*3] = (a3 + b3) >> COL_SHIFT;
		col[8*4] = (a3 - b3) >> COL_SHIFT;
#ifndef ARCH_ALPHA
	}
#endif
}

static void inline idctSparse2Col (int16_t * col)
{
	int a0, a1, a2, a3, b0, b1, b2, b3;
	col[0] += (1<<(COL_SHIFT-1))/W4;
	a0 = W4*col[8*0];
	a1 = W4*col[8*0];
	a2 = W4*col[8*0];
	a3 = W4*col[8*0];

	if(col[8*2]){
		a0 +=  + W2*col[8*2];
		a1 +=  + W6*col[8*2];
		a2 +=  - W6*col[8*2];
		a3 +=  - W2*col[8*2];
	}

	if(col[8*4]){
		a0 += + W4*col[8*4];
		a1 += - W4*col[8*4];
		a2 += - W4*col[8*4];
		a3 += + W4*col[8*4];
	}

	if(col[8*6]){
		a0 += + W6*col[8*6];
		a1 += - W2*col[8*6];
		a2 += + W2*col[8*6];
		a3 += - W6*col[8*6];
	}

	if(col[8*1] || 1){
		b0 = W1*col[8*1];
		b1 = W3*col[8*1];
		b2 = W5*col[8*1];
		b3 = W7*col[8*1];
	}else{
		b0 = 
		b1 = 
		b2 = 
		b3 = 0;
	}

	if(col[8*3]){
		b0 += + W3*col[8*3];
		b1 += - W7*col[8*3];
		b2 += - W1*col[8*3];
		b3 += - W5*col[8*3];
	}

	if(col[8*5]){
		b0 += + W5*col[8*5];
		b1 += - W1*col[8*5];
		b2 += + W7*col[8*5];
		b3 += + W3*col[8*5];
	}

	if(col[8*7]){
		b0 += + W7*col[8*7];
		b1 += - W5*col[8*7];
		b2 += + W3*col[8*7];
		b3 += - W1*col[8*7];
	}

	col[8*0] = (a0 + b0) >> COL_SHIFT;
	col[8*7] = (a0 - b0) >> COL_SHIFT;
	col[8*1] = (a1 + b1) >> COL_SHIFT;
	col[8*6] = (a1 - b1) >> COL_SHIFT;
	col[8*2] = (a2 + b2) >> COL_SHIFT;
	col[8*5] = (a2 - b2) >> COL_SHIFT;
	col[8*3] = (a3 + b3) >> COL_SHIFT;
	col[8*4] = (a3 - b3) >> COL_SHIFT;
}

#ifdef ARCH_ALPHA
/* If all rows but the first one are zero after row transformation,
   all rows will be identical after column transformation.  */
static inline void idctCol2(int16_t *col)
{
	int i;
	uint64_t l, r;
	uint64_t *lcol = (uint64_t *) col;

	for (i = 0; i < 8; ++i) {
		int a0 = col[0] + (1 << (COL_SHIFT - 1)) / W4;

		a0 *= W4;
		col[0] = a0 >> COL_SHIFT;
		++col;
	}

	l = lcol[0];
	r = lcol[1];
	lcol[ 2] = l; lcol[ 3] = r;
	lcol[ 4] = l; lcol[ 5] = r;
	lcol[ 6] = l; lcol[ 7] = r;
	lcol[ 8] = l; lcol[ 9] = r;
	lcol[10] = l; lcol[11] = r;
	lcol[12] = l; lcol[13] = r;
	lcol[14] = l; lcol[15] = r;
}
#endif

void simple_idct (short *block)
{

	int i;
	
#if 0
	int nonZero[8];
	int buffer[64];
	int nNonZero=0;
	
	idctRowCondDC(block);
	
	for(i=1; i<8; i++)
	{
		nonZero[nNonZero]=i;
		nNonZero+= idctRowCondZ(block + i*8);
	}
	
	if(nNonZero==0)
	{
		for(i=0; i<8; i++)
		{
			block[i   ]=
			block[i+8 ]=
			block[i+16]=
			block[i+24]=
			block[i+32]=
			block[i+40]=
			block[i+48]=
			block[i+56]= (W4*block[i] + (1<<(COL_SHIFT-1))) >> COL_SHIFT;
		}	
	}
	else if(nNonZero==1)
	{
		int index= nonZero[0]*8;
		for(i=0; i<8; i++)
		{
			int bias= W4*block[i] + (1<<(COL_SHIFT-1));
			int c= block[i + index];
			block[i   ]= (c*coeff[index  ] + bias) >> COL_SHIFT;
			block[i+8 ]= (c*coeff[index+1] + bias) >> COL_SHIFT;
			block[i+16]= (c*coeff[index+2] + bias) >> COL_SHIFT;
			block[i+24]= (c*coeff[index+3] + bias) >> COL_SHIFT;
			block[i+32]= (c*coeff[index+4] + bias) >> COL_SHIFT;
			block[i+40]= (c*coeff[index+5] + bias) >> COL_SHIFT;
			block[i+48]= (c*coeff[index+6] + bias) >> COL_SHIFT;
			block[i+56]= (c*coeff[index+7] + bias) >> COL_SHIFT;
		}	
	}
/*	else if(nNonZero==2)
	{
		int index1= nonZero[0]*8;
		int index2= nonZero[1]*8;
		for(i=0; i<8; i++)
		{
			int bias= W4*block[i] + (1<<(COL_SHIFT-1));
			int c1= block[i + index1];
			int c2= block[i + index2];
			block[i   ]= (c1*coeff[index1  ] + c2*coeff[index2  ] + bias) >> COL_SHIFT;
			block[i+8 ]= (c1*coeff[index1+1] + c2*coeff[index2+1] + bias) >> COL_SHIFT;
			block[i+16]= (c1*coeff[index1+2] + c2*coeff[index2+2] + bias) >> COL_SHIFT;
			block[i+24]= (c1*coeff[index1+3] + c2*coeff[index2+3] + bias) >> COL_SHIFT;
			block[i+32]= (c1*coeff[index1+4] + c2*coeff[index2+4] + bias) >> COL_SHIFT;
			block[i+40]= (c1*coeff[index1+5] + c2*coeff[index2+5] + bias) >> COL_SHIFT;
			block[i+48]= (c1*coeff[index1+6] + c2*coeff[index2+6] + bias) >> COL_SHIFT;
			block[i+56]= (c1*coeff[index1+7] + c2*coeff[index2+7] + bias) >> COL_SHIFT;
		}	
	}*/
	else
	{
		for(i=0; i<8; i++)
			idctSparse2Col(block + i);
	}
#elif defined(ARCH_ALPHA)
        int rowsZero = 1;       /* all rows except row 0 zero */
        int rowsConstant = 1;	/* all rows consist of a constant value */

	for (i = 0; i < 8; i++) {
		int sparseness = idctRowCondDC(block + 8 * i);

		if (i > 0 && sparseness > 0)
                        rowsZero = 0;
                if (sparseness == 2)
                        rowsConstant = 0;
	}

        if (rowsZero) {
                idctCol2(block);
        } else if (rowsConstant) {
		uint64_t *lblock = (uint64_t *) block;

		idctSparseCol(block);
		for (i = 0; i < 8; i++) {
			uint64_t v = (uint16_t) block[i * 8];

			v += v << 16;
			v += v << 32;
			lblock[0] = v;
			lblock[1] = v;
			lblock += 2;
		}
	} else {
		for (i = 0; i < 8; i++)
			idctSparseCol(block + i);
	}
#else
	for(i=0; i<8; i++)
		idctRowCondDC(block + i*8);
	
	for(i=0; i<8; i++)
		idctSparseCol(block + i);
#endif
}
