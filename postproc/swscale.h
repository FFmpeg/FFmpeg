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

#ifndef SWSCALE_H
#define SWSCALE_H

#include "swscale_internal.h" //FIXME HACK REMOVE

/* values for the flags, the stuff on the command line is different */
#define SWS_FAST_BILINEAR 1
#define SWS_BILINEAR 2
#define SWS_BICUBIC  4
#define SWS_X        8
#define SWS_POINT    0x10
#define SWS_AREA     0x20
#define SWS_BICUBLIN 0x40
#define SWS_GAUSS    0x80
#define SWS_SINC     0x100
#define SWS_LANCZOS  0x200
#define SWS_SPLINE   0x400

#define SWS_SRC_V_CHR_DROP_MASK		0x30000
#define SWS_SRC_V_CHR_DROP_SHIFT	16

#define SWS_PARAM_MASK			0x3FC0000
#define SWS_PARAM_SHIFT			18

#define SWS_PRINT_INFO		0x1000

//the following 3 flags are not completly implemented
//internal chrominace subsamling info
#define SWS_FULL_CHR_H_INT	0x2000
//input subsampling info
#define SWS_FULL_CHR_H_INP	0x4000
#define SWS_DIRECT_BGR		0x8000

#define SWS_MAX_REDUCE_CUTOFF 0.002

#define SWS_CS_ITU709		1
#define SWS_CS_FCC 		4
#define SWS_CS_ITU601		5
#define SWS_CS_ITU624		5
#define SWS_CS_SMPTE170M 	5
#define SWS_CS_SMPTE240M 	7
#define SWS_CS_DEFAULT 		5



// when used for filters they must have an odd number of elements
// coeffs cannot be shared between vectors
typedef struct {
	double *coeff;
	int length;
} SwsVector;

// vectors can be shared
typedef struct {
	SwsVector *lumH;
	SwsVector *lumV;
	SwsVector *chrH;
	SwsVector *chrV;
} SwsFilter;


// *** bilinear scaling and yuv->rgb & yuv->yuv conversion of yv12 slices:
// *** Note: it's called multiple times while decoding a frame, first time y==0
// dstbpp == 12 -> yv12 output
// will use sws_flags
void SwScale_YV12slice(unsigned char* src[],int srcStride[], int srcSliceY,
			     int srcSliceH, uint8_t* dst[], int dstStride, int dstbpp,
			     int srcW, int srcH, int dstW, int dstH);

// Obsolete, will be removed soon
void SwScale_Init();



void freeSwsContext(SwsContext *swsContext);

SwsContext *getSwsContextFromCmdLine(int srcW, int srcH, int srcFormat, int dstW, int dstH, int dstFormat);
SwsContext *getSwsContext(int srcW, int srcH, int srcFormat, int dstW, int dstH, int dstFormat, int flags,
			 SwsFilter *srcFilter, SwsFilter *dstFilter);
void swsGetFlagsAndFilterFromCmdLine(int *flags, SwsFilter **srcFilterParam, SwsFilter **dstFilterParam);

int sws_setColorspaceDetails(SwsContext *c, const int inv_table[4], int srcRange, const int table[4], int dstRange, int brightness, int contrast, int saturation);
int sws_getColorspaceDetails(SwsContext *c, int **inv_table, int *srcRange, int **table, int *dstRange, int *brightness, int *contrast, int *saturation);

SwsVector *getGaussianVec(double variance, double quality);
SwsVector *getConstVec(double c, int length);
SwsVector *getIdentityVec(void);
void scaleVec(SwsVector *a, double scalar);
void normalizeVec(SwsVector *a, double height);
void convVec(SwsVector *a, SwsVector *b);
void addVec(SwsVector *a, SwsVector *b);
void subVec(SwsVector *a, SwsVector *b);
void shiftVec(SwsVector *a, int shift);
SwsVector *cloneVec(SwsVector *a);

void printVec(SwsVector *a);
void freeVec(SwsVector *a);

#endif
