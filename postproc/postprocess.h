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

// POSTPROCESS_H is defined by opendivx's postprocess.h
#ifndef NEWPOSTPROCESS_H
#define NEWPOSTPROCESS_H

#define V_DEBLOCK	0x01
#define H_DEBLOCK	0x02
#define DERING		0x04
#define LEVEL_FIX	0x08 /* Brightness & Contrast */

#define LUM_V_DEBLOCK	V_DEBLOCK		//   1
#define LUM_H_DEBLOCK	H_DEBLOCK		//   2
#define CHROM_V_DEBLOCK	(V_DEBLOCK<<4)		//  16
#define CHROM_H_DEBLOCK	(H_DEBLOCK<<4)		//  32
#define LUM_DERING	DERING			//   4
#define CHROM_DERING	(DERING<<4)		//  64
#define LUM_LEVEL_FIX	LEVEL_FIX		//   8
#define CHROM_LEVEL_FIX	(LEVEL_FIX<<4)		// 128 (not implemented yet)

// Experimental vertical filters
#define V_X1_FILTER	0x0200			// 512

// Experimental horizontal filters
#define H_X1_FILTER	0x2000			// 8192

// select between full y range (255-0) or standart one (234-16)
#define FULL_Y_RANGE	0x8000			// 32768

//Deinterlacing Filters
#define	LINEAR_IPOL_DEINT_FILTER	0x10000	// 65536
#define	LINEAR_BLEND_DEINT_FILTER	0x20000	// 131072
#define	CUBIC_BLEND_DEINT_FILTER	0x8000	// (not implemented yet)
#define	CUBIC_IPOL_DEINT_FILTER		0x40000	// 262144
#define	MEDIAN_DEINT_FILTER		0x80000	// 524288
#define	FFMPEG_DEINT_FILTER		0x400000

#define TEMP_NOISE_FILTER		0x100000
#define FORCE_QUANT			0x200000


#define GET_PP_QUALITY_MAX 6

//use if u want a faster postprocessing code
//cant differentiate between chroma & luma filters (both on or both off)
//obviosly the -pp option at the commandline has no effect except turning the here selected
//filters on
//#define COMPILE_TIME_MODE 0x77

#define QP_STORE_T int

char *postproc_help;

typedef struct PPMode{
	int lumMode; //acivates filters for luminance
	int chromMode; //acivates filters for chrominance
	int error; // non zero on error

	int minAllowedY; // for brigtness correction
	int maxAllowedY; // for brihtness correction

	int maxTmpNoise[3]; // for Temporal Noise Reducing filter (Maximal sum of abs differences)
	
	int maxDcDiff; // max abs diff between pixels to be considered flat
	int flatnessThreshold;

	int forcedQuant; // quantizer if FORCE_QUANT is used
} PPMode;

void  postprocess(uint8_t * src[3], int srcStride[3],
                 uint8_t * dst[3], int dstStride[3],
                 int horizontalSize, int verticalSize,
                 QP_STORE_T *QP_store,  int QP_stride,
		 PPMode *mode, void *ppContext);

// name is the stuff after "-pp" on the command line
PPMode getPPModeByNameAndQuality(char *name, int quality);

void *getPPContext(int width, int height);
void freePPContext(void *ppContext);

int readPPOpt(void *conf, char *arg);

#endif
