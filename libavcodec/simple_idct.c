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
#if 1
static void inline idctRow (int16_t * row)
{
	int a0, a1, a2, a3, b0, b1, b2, b3;
	const int C1 =W1;
	const int C2 =W2;
	const int C3 =W3;
	const int C4 =W4;
	const int C5 =W5;
	const int C6 =W6;
	const int C7 =W7;

	if( !(row[1] | row[2] |row[3] |row[4] |row[5] |row[6] | row[7])) {
		row[0] = row[1] = row[2] = row[3] = row[4] =
			row[5] = row[6] = row[7] = row[0]<<3;
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
}

static void inline idctCol (int16_t * col)
{
	int a0, a1, a2, a3, b0, b1, b2, b3;
	const int C1 =W1;
	const int C2 =W2;
	const int C3 =W3;
	const int C4 =W4;
	const int C5 =W5;
	const int C6 =W6;
	const int C7 =W7;
/*
	if( !(col[8*1] | col[8*2] |col[8*3] |col[8*4] |col[8*5] |col[8*6] | col[8*7])) {
		col[8*0] = col[8*1] = col[8*2] = col[8*3] = col[8*4] =
			col[8*5] = col[8*6] = col[8*7] = col[8*0]<<3;
		return;
	}*/
	col[0] += (1<<(COL_SHIFT-1))/W4;
	a0 = C4*col[8*0] + C2*col[8*2] + C4*col[8*4] + C6*col[8*6];
	a1 = C4*col[8*0] + C6*col[8*2] - C4*col[8*4] - C2*col[8*6];
	a2 = C4*col[8*0] - C6*col[8*2] - C4*col[8*4] + C2*col[8*6];
	a3 = C4*col[8*0] - C2*col[8*2] + C4*col[8*4] - C6*col[8*6];

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

void simple_idct (short *block)
{
	int i;
	for(i=0; i<8; i++)
		idctRow(block + 8*i);

	for(i=0; i<8; i++)
		idctCol(block + i);

}

#else

#define W1  22725  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W2  21407  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W3  19266  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W4  16384  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W5  12873  //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W6  8867   //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define W7  4520   //cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5
#define COL_SHIFT 31 // 6

static void inline idctRow (int32_t *out, int16_t * row)
{
	int a0, a1, a2, a3, b0, b1, b2, b3;
	const int C1 =W1;
	const int C2 =W2;
	const int C3 =W3;
	const int C4 =W4;
	const int C5 =W5;
	const int C6 =W6;
	const int C7 =W7;
/*
	if( !(row[1] | row[2] |row[3] |row[4] |row[5] |row[6] | row[7])) {
		row[0] = row[1] = row[2] = row[3] = row[4] =
			row[5] = row[6] = row[7] = row[0]<<14;
		return;
	}
*/
	a0 = C4*row[0] + C2*row[2] + C4*row[4] + C6*row[6];
	a1 = C4*row[0] + C6*row[2] - C4*row[4] - C2*row[6];
	a2 = C4*row[0] - C6*row[2] - C4*row[4] + C2*row[6];
	a3 = C4*row[0] - C2*row[2] + C4*row[4] - C6*row[6];

	b0 = C1*row[1] + C3*row[3] + C5*row[5] + C7*row[7];
	b1 = C3*row[1] - C7*row[3] - C1*row[5] - C5*row[7];
	b2 = C5*row[1] - C1*row[3] + C7*row[5] + C3*row[7];
	b3 = C7*row[1] - C5*row[3] + C3*row[5] - C1*row[7];

	out[0] = (a0 + b0);
	out[1] = (a1 + b1);
	out[2] = (a2 + b2);
	out[3] = (a3 + b3);
	out[4] = (a3 - b3);
	out[5] = (a2 - b2);
	out[6] = (a1 - b1);
	out[7] = (a0 - b0);
}

static void inline idctCol (int32_t *in, int16_t * col)
{
	int64_t a0, a1, a2, a3, b0, b1, b2, b3;
	const int64_t C1 =W1;
	const int64_t C2 =W2;
	const int64_t C3 =W3;
	const int64_t C4 =W4;
	const int64_t C5 =W5;
	const int64_t C6 =W6;
	const int64_t C7 =W7;
/*
	if( !(col[8*1] | col[8*2] |col[8*3] |col[8*4] |col[8*5] |col[8*6] | col[8*7])) {
		col[8*0] = col[8*1] = col[8*2] = col[8*3] = col[8*4] =
			col[8*5] = col[8*6] = col[8*7] = col[8*0]<<3;
		return;
	}*/
	in[0] += (1<<(COL_SHIFT-1))/W4;
	a0 = C4*in[8*0] + C2*in[8*2] + C4*in[8*4] + C6*in[8*6];
	a1 = C4*in[8*0] + C6*in[8*2] - C4*in[8*4] - C2*in[8*6];
	a2 = C4*in[8*0] - C6*in[8*2] - C4*in[8*4] + C2*in[8*6];
	a3 = C4*in[8*0] - C2*in[8*2] + C4*in[8*4] - C6*in[8*6];

	b0 = C1*in[8*1] + C3*in[8*3] + C5*in[8*5] + C7*in[8*7];
	b1 = C3*in[8*1] - C7*in[8*3] - C1*in[8*5] - C5*in[8*7];
	b2 = C5*in[8*1] - C1*in[8*3] + C7*in[8*5] + C3*in[8*7];
	b3 = C7*in[8*1] - C5*in[8*3] + C3*in[8*5] - C1*in[8*7];

	col[8*0] = (a0 + b0) >> COL_SHIFT;
	col[8*1] = (a1 + b1) >> COL_SHIFT;
	col[8*2] = (a2 + b2) >> COL_SHIFT;
	col[8*3] = (a3 + b3) >> COL_SHIFT;
	col[8*4] = (a3 - b3) >> COL_SHIFT;
	col[8*5] = (a2 - b2) >> COL_SHIFT;
	col[8*6] = (a1 - b1) >> COL_SHIFT;
	col[8*7] = (a0 - b0) >> COL_SHIFT;
}

void simple_idct (short *block)
{
	int i;
	int32_t temp[64];
	for(i=0; i<8; i++)
		idctRow(temp+8*i, block + 8*i);

	for(i=0; i<8; i++)
		idctCol(temp+i, block + i);

}

#endif
