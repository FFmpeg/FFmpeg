/*
 * Copyright (C) 2001-2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef SWSCALE_INTERNAL_H
#define SWSCALE_INTERNAL_H

#ifdef HAVE_ALTIVEC_H
#include <altivec.h>
#endif

#include "avutil.h"

#ifdef CONFIG_DARWIN
#define AVV(x...) (x)
#else
#define AVV(x...) {x}
#endif

#define MAX_FILTER_SIZE 256

typedef int (*SwsFunc)(struct SwsContext *context, uint8_t* src[], int srcStride[], int srcSliceY,
             int srcSliceH, uint8_t* dst[], int dstStride[]);

/* this struct should be aligned on at least 32-byte boundary */
typedef struct SwsContext{
        /**
         * info on struct for av_log
         */
        AVClass *av_class;

	/**
	 *
	 * Note the src,dst,srcStride,dstStride will be copied, in the sws_scale() warper so they can freely be modified here
	 */
	SwsFunc swScale;
	int srcW, srcH, dstH;
	int chrSrcW, chrSrcH, chrDstW, chrDstH;
	int lumXInc, chrXInc;
	int lumYInc, chrYInc;
	int dstFormat, srcFormat;               ///< format 4:2:0 type is allways YV12
	int origDstFormat, origSrcFormat;       ///< format
	int chrSrcHSubSample, chrSrcVSubSample;
	int chrIntHSubSample, chrIntVSubSample;
	int chrDstHSubSample, chrDstVSubSample;
	int vChrDrop;
        int sliceDir;
	double param[2];

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

	uint8_t formatConvBuffer[4000]; //FIXME dynamic alloc, but we have to change a lot of code for this to be useful

	int hLumFilterSize;
	int hChrFilterSize;
	int vLumFilterSize;
	int vChrFilterSize;
	int vLumBufSize;
	int vChrBufSize;

	uint8_t *funnyYCode;
	uint8_t *funnyUVCode;
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
	uint8_t * table_rV[256];
	uint8_t * table_gU[256];
	int    table_gV[256];
	uint8_t * table_bU[256];

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
#define DSTW_OFFSET  "11*8+4*4*256*2" //do not change, its hardcoded in the asm
#define ESP_OFFSET  "11*8+4*4*256*2+8"
#define VROUNDER_OFFSET "11*8+4*4*256*2+16"
#define U_TEMP       "11*8+4*4*256*2+24"
#define V_TEMP       "11*8+4*4*256*2+32"

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
	uint64_t esp __attribute__((aligned(8)));
	uint64_t vRounder     __attribute__((aligned(8)));
	uint64_t u_temp       __attribute__((aligned(8)));
	uint64_t v_temp       __attribute__((aligned(8)));

#ifdef HAVE_ALTIVEC

  vector signed short   CY;
  vector signed short   CRV;
  vector signed short   CBU;
  vector signed short   CGU;
  vector signed short   CGV;
  vector signed short   OY;
  vector unsigned short CSHIFT;
  vector signed short *vYCoeffsBank, *vCCoeffsBank;

#endif

} SwsContext;
//FIXME check init (where 0)

SwsFunc yuv2rgb_get_func_ptr (SwsContext *c);
int yuv2rgb_c_init_tables (SwsContext *c, const int inv_table[4], int fullRange, int brightness, int contrast, int saturation);

char *sws_format_name(int format);

//FIXME replace this with something faster
#define isPlanarYUV(x) ((x)==PIX_FMT_YUV410P || (x)==PIX_FMT_YUV420P	\
			|| (x)==PIX_FMT_YUV411P || (x)==PIX_FMT_YUV422P	\
			|| (x)==PIX_FMT_YUV444P || (x)==PIX_FMT_NV12	\
			|| (x)==PIX_FMT_NV21)
#define isYUV(x)       ((x)==PIX_FMT_UYVY422 || (x)==PIX_FMT_YUYV422 || isPlanarYUV(x))
#define isGray(x)      ((x)==PIX_FMT_GRAY8 || (x)==PIX_FMT_GRAY16BE || (x)==PIX_FMT_GRAY16LE)
#define isGray16(x)    ((x)==PIX_FMT_GRAY16BE || (x)==PIX_FMT_GRAY16LE)
#define isRGB(x)       ((x)==PIX_FMT_BGR32 || (x)==PIX_FMT_RGB24	\
			|| (x)==PIX_FMT_RGB565 || (x)==PIX_FMT_RGB555	\
			|| (x)==PIX_FMT_RGB8 || (x)==PIX_FMT_RGB4 || (x)==PIX_FMT_RGB4_BYTE	\
			|| (x)==PIX_FMT_MONOBLACK)
#define isBGR(x)       ((x)==PIX_FMT_RGB32 || (x)==PIX_FMT_BGR24	\
			|| (x)==PIX_FMT_BGR565 || (x)==PIX_FMT_BGR555	\
			|| (x)==PIX_FMT_BGR8 || (x)==PIX_FMT_BGR4 || (x)==PIX_FMT_BGR4_BYTE	\
			|| (x)==PIX_FMT_MONOBLACK)

static inline int fmt_depth(int fmt)
{
    switch(fmt) {
        case PIX_FMT_BGRA:
        case PIX_FMT_ABGR:
        case PIX_FMT_RGBA:
        case PIX_FMT_ARGB:
            return 32;
        case PIX_FMT_BGR24:
        case PIX_FMT_RGB24:
            return 24;
        case PIX_FMT_BGR565:
        case PIX_FMT_RGB565:
        case PIX_FMT_GRAY16BE:
        case PIX_FMT_GRAY16LE:
            return 16;
        case PIX_FMT_BGR555:
        case PIX_FMT_RGB555:
            return 15;
        case PIX_FMT_BGR8:
        case PIX_FMT_RGB8:
            return 8;
        case PIX_FMT_BGR4:
        case PIX_FMT_RGB4:
        case PIX_FMT_BGR4_BYTE:
        case PIX_FMT_RGB4_BYTE:
            return 4;
        case PIX_FMT_MONOBLACK:
            return 1;
        default:
            return 0;
    }
}

#endif
