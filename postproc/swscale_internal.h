/*
    Copyright (C) 2001-2003 Michael Niedermayer <michaelni@gmx.at>

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

#ifndef SWSCALE_INTERNAL_H
#define SWSCALE_INTERNAL_H

#define MAX_FILTER_SIZE 256

typedef int (*SwsFunc)(struct SwsContext *context, uint8_t* src[], int srcStride[], int srcSliceY,
             int srcSliceH, uint8_t* dst[], int dstStride[]);

/* this struct should be aligned on at least 32-byte boundary */
typedef struct SwsContext{
	SwsFunc swScale;
	int srcW, srcH, dstH;
	int chrSrcW, chrSrcH, chrDstW, chrDstH;
	int lumXInc, chrXInc;
	int lumYInc, chrYInc;
	int dstFormat, srcFormat;
	int chrSrcHSubSample, chrSrcVSubSample;
	int chrIntHSubSample, chrIntVSubSample;
	int chrDstHSubSample, chrDstVSubSample;
	int vChrDrop;

	int16_t **lumPixBuf;
	int16_t **chrPixBuf;
	int16_t *hLumFilter;
	int16_t *hLumFilterPos;
	int16_t *hChrFilter;
	int16_t *hChrFilterPos;
	int16_t *vLumFilter;
	int16_t *vLumFilterPos;
	int16_t *vChrFilter;
	int16_t *vChrFilterPos;

	uint8_t formatConvBuffer[4000]; //FIXME dynamic alloc, but we have to change alot of code for this to be usefull

	int hLumFilterSize;
	int hChrFilterSize;
	int vLumFilterSize;
	int vChrFilterSize;
	int vLumBufSize;
	int vChrBufSize;

	uint8_t __attribute__((aligned(32))) funnyYCode[10000];
	uint8_t __attribute__((aligned(32))) funnyUVCode[10000];
	int32_t *lumMmx2FilterPos;
	int32_t *chrMmx2FilterPos;
	int16_t *lumMmx2Filter;
	int16_t *chrMmx2Filter;

	int canMMX2BeUsed;

	int lastInLumBuf;
	int lastInChrBuf;
	int lumBufIndex;
	int chrBufIndex;
	int dstY;
	int flags;
	void * yuvTable;			// pointer to the yuv->rgb table start so it can be freed()
	void * table_rV[256];
	void * table_gU[256];
	int    table_gV[256];
	void * table_bU[256];

	//Colorspace stuff
	int contrast, brightness, saturation;	// for sws_getColorspaceDetails
	int srcColorspaceTable[4];
	int dstColorspaceTable[4];
	int srcRange, dstRange;

#define RED_DITHER   "0*8"
#define GREEN_DITHER "1*8"
#define BLUE_DITHER  "2*8"
#define Y_COEFF      "3*8"
#define VR_COEFF     "4*8"
#define UB_COEFF     "5*8"
#define VG_COEFF     "6*8"
#define UG_COEFF     "7*8"
#define Y_OFFSET     "8*8"
#define U_OFFSET     "9*8"
#define V_OFFSET     "10*8"
#define LUM_MMX_FILTER_OFFSET "11*8"
#define CHR_MMX_FILTER_OFFSET "11*8+4*4*256"
#define DSTW_OFFSET  "11*8+4*4*256*2"
#define ESP_OFFSET  "11*8+4*4*256*2+4"
                  
	uint64_t redDither   __attribute__((aligned(8)));
	uint64_t greenDither __attribute__((aligned(8)));
	uint64_t blueDither  __attribute__((aligned(8)));

	uint64_t yCoeff      __attribute__((aligned(8)));
	uint64_t vrCoeff     __attribute__((aligned(8)));
	uint64_t ubCoeff     __attribute__((aligned(8)));
	uint64_t vgCoeff     __attribute__((aligned(8)));
	uint64_t ugCoeff     __attribute__((aligned(8)));
	uint64_t yOffset     __attribute__((aligned(8)));
	uint64_t uOffset     __attribute__((aligned(8)));
	uint64_t vOffset     __attribute__((aligned(8)));
	int32_t  lumMmxFilter[4*MAX_FILTER_SIZE];
	int32_t  chrMmxFilter[4*MAX_FILTER_SIZE];
	int dstW;
	int esp;
} SwsContext;
//FIXME check init (where 0)

inline void sws_orderYUV(int format, uint8_t * sortedP[], int sortedStride[], uint8_t * p[], int stride[]);
SwsFunc yuv2rgb_get_func_ptr (SwsContext *c);
int yuv2rgb_c_init_tables (SwsContext *c, const int inv_table[4], int fullRange, int brightness, int contrast, int saturation);

#endif
