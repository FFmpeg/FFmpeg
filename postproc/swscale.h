/*
    Copyright (C) 2001-2002 Michael Niedermayer <michaelni@gmx.at>

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

/* this struct should be aligned on at least 32-byte boundary */
typedef struct SwsContext{
	int srcW, srcH, dstW, dstH;
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

// Contain simply the values from v(Lum|Chr)Filter just nicely packed for mmx
	int16_t  *lumMmxFilter;
	int16_t  *chrMmxFilter;
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
	void * yuvTable;
	void * table_rV[256];
	void * table_gU[256];
	int    table_gV[256];
	void * table_bU[256];

	void (*swScale)(struct SwsContext *context, uint8_t* src[], int srcStride[], int srcSliceY,
             int srcSliceH, uint8_t* dst[], int dstStride[]);
} SwsContext;
//FIXME check init (where 0)

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

