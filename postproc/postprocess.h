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

// POSTPROCESS_H is defined by opendivx's postprocess.h
#ifndef NEWPOSTPROCESS_H
#define NEWPOSTPROCESS_H

#define BLOCK_SIZE 8
#define TEMP_STRIDE 8
//#define NUM_BLOCKS_AT_ONCE 16 //not used yet

#define V_DEBLOCK	0x01
#define H_DEBLOCK	0x02
#define DERING		0x04
#define LEVEL_FIX	0x08 /* Brightness & Contrast */

#define LUM_V_DEBLOCK	V_DEBLOCK		//   1
#define LUM_H_DEBLOCK	H_DEBLOCK		//   2
#define CHROM_V_DEBLOCK	(V_DEBLOCK<<4)		//  16
#define CHROM_H_DEBLOCK	(H_DEBLOCK<<4)		//  32
#define LUM_DERING	DERING			//   4 (not implemented yet)
#define CHROM_DERING	(DERING<<4)		//  64 (not implemented yet)
#define LUM_LEVEL_FIX	LEVEL_FIX		//   8
#define CHROM_LEVEL_FIX	(LEVEL_FIX<<4)		// 128 (not implemented yet)

// Experimental vertical filters
#define V_RK1_FILTER	0x0100			// 256
#define V_X1_FILTER	0x0200			// 512

// Experimental horizontal filters
#define H_RK1_FILTER	0x1000			// 4096 (not implemented yet)
#define H_X1_FILTER	0x2000			// 8192

//Deinterlacing Filters
#define DEINTERLACE_FILTER_MASK		0xF0000
#define	LINEAR_IPOL_DEINT_FILTER	0x10000	// 65536
#define	LINEAR_BLEND_DEINT_FILTER	0x20000	// 131072
#define	CUBIC_BLEND_DEINT_FILTER	0x30000	// 196608 (not implemented yet)
#define	CUBIC_IPOL_DEINT_FILTER		0x40000	// 262144 (not implemented yet)
#define	MEDIAN_DEINT_FILTER		0x80000	// 524288 


#define GET_PP_QUALITY_MAX 6

//#define TIMEING
//#define MORE_TIMEING

#define QP_STORE_T int

void postprocess(unsigned char * src[], int src_stride,
                 unsigned char * dst[], int dst_stride,
                 int horizontal_size,   int vertical_size,
                 QP_STORE_T *QP_store,  int QP_stride, int mode);

int getPpModeForQuality(int quality);

#endif
