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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef SWSCALE_SWSCALE_INTERNAL_H
#define SWSCALE_SWSCALE_INTERNAL_H

#include "config.h"

#if HAVE_ALTIVEC_H
#include <altivec.h>
#endif

#include "libavutil/avutil.h"

#define STR(s)         AV_TOSTRING(s) //AV_STRINGIFY is too long

#define MAX_FILTER_SIZE 256

#define VOFW 2048
#define VOF  (VOFW*2)

#ifdef WORDS_BIGENDIAN
#define ALT32_CORR (-1)
#else
#define ALT32_CORR   1
#endif

#if ARCH_X86_64
#   define APCK_PTR2 8
#   define APCK_COEF 16
#   define APCK_SIZE 24
#else
#   define APCK_PTR2 4
#   define APCK_COEF 8
#   define APCK_SIZE 16
#endif

struct SwsContext;

typedef int (*SwsFunc)(struct SwsContext *context, uint8_t* src[], int srcStride[], int srcSliceY,
             int srcSliceH, uint8_t* dst[], int dstStride[]);

/* This struct should be aligned on at least a 32-byte boundary. */
typedef struct SwsContext{
    /**
     * info on struct for av_log
     */
    const AVClass *av_class;

    /**
     * Note that src, dst, srcStride, dstStride will be copied in the
     * sws_scale() wrapper so they can be freely modified here.
     */
    SwsFunc swScale;
    int srcW, srcH, dstH;
    int chrSrcW, chrSrcH, chrDstW, chrDstH;
    int lumXInc, chrXInc;
    int lumYInc, chrYInc;
    enum PixelFormat dstFormat, srcFormat;  ///< format 4:2:0 type is always YV12
    int origDstFormat, origSrcFormat;       ///< format
    int chrSrcHSubSample, chrSrcVSubSample;
    int chrIntHSubSample, chrIntVSubSample;
    int chrDstHSubSample, chrDstVSubSample;
    int vChrDrop;
    int sliceDir;
    double param[2];

    uint32_t pal_yuv[256];
    uint32_t pal_rgb[256];

    int16_t **lumPixBuf;
    int16_t **chrPixBuf;
    int16_t **alpPixBuf;
    int16_t *hLumFilter;
    int16_t *hLumFilterPos;
    int16_t *hChrFilter;
    int16_t *hChrFilterPos;
    int16_t *vLumFilter;
    int16_t *vLumFilterPos;
    int16_t *vChrFilter;
    int16_t *vChrFilterPos;

    uint8_t formatConvBuffer[VOF]; //FIXME dynamic allocation, but we have to change a lot of code for this to be useful

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
    void * yuvTable;            // pointer to the yuv->rgb table start so it can be freed()
    uint8_t * table_rV[256];
    uint8_t * table_gU[256];
    int    table_gV[256];
    uint8_t * table_bU[256];

    //Colorspace stuff
    int contrast, brightness, saturation;    // for sws_getColorspaceDetails
    int srcColorspaceTable[4];
    int dstColorspaceTable[4];
    int srcRange, dstRange;
    int yuv2rgb_y_offset;
    int yuv2rgb_y_coeff;
    int yuv2rgb_v2r_coeff;
    int yuv2rgb_v2g_coeff;
    int yuv2rgb_u2g_coeff;
    int yuv2rgb_u2b_coeff;

#define RED_DITHER            "0*8"
#define GREEN_DITHER          "1*8"
#define BLUE_DITHER           "2*8"
#define Y_COEFF               "3*8"
#define VR_COEFF              "4*8"
#define UB_COEFF              "5*8"
#define VG_COEFF              "6*8"
#define UG_COEFF              "7*8"
#define Y_OFFSET              "8*8"
#define U_OFFSET              "9*8"
#define V_OFFSET              "10*8"
#define LUM_MMX_FILTER_OFFSET "11*8"
#define CHR_MMX_FILTER_OFFSET "11*8+4*4*256"
#define DSTW_OFFSET           "11*8+4*4*256*2" //do not change, it is hardcoded in the ASM
#define ESP_OFFSET            "11*8+4*4*256*2+8"
#define VROUNDER_OFFSET       "11*8+4*4*256*2+16"
#define U_TEMP                "11*8+4*4*256*2+24"
#define V_TEMP                "11*8+4*4*256*2+32"
#define Y_TEMP                "11*8+4*4*256*2+40"
#define ALP_MMX_FILTER_OFFSET "11*8+4*4*256*2+48"

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
    uint64_t esp          __attribute__((aligned(8)));
    uint64_t vRounder     __attribute__((aligned(8)));
    uint64_t u_temp       __attribute__((aligned(8)));
    uint64_t v_temp       __attribute__((aligned(8)));
    uint64_t y_temp       __attribute__((aligned(8)));
    int32_t  alpMmxFilter[4*MAX_FILTER_SIZE];

#if HAVE_ALTIVEC

  vector signed short   CY;
  vector signed short   CRV;
  vector signed short   CBU;
  vector signed short   CGU;
  vector signed short   CGV;
  vector signed short   OY;
  vector unsigned short CSHIFT;
  vector signed short   *vYCoeffsBank, *vCCoeffsBank;

#endif


#if ARCH_BFIN
    uint32_t oy           __attribute__((aligned(4)));
    uint32_t oc           __attribute__((aligned(4)));
    uint32_t zero         __attribute__((aligned(4)));
    uint32_t cy           __attribute__((aligned(4)));
    uint32_t crv          __attribute__((aligned(4)));
    uint32_t rmask        __attribute__((aligned(4)));
    uint32_t cbu          __attribute__((aligned(4)));
    uint32_t bmask        __attribute__((aligned(4)));
    uint32_t cgu          __attribute__((aligned(4)));
    uint32_t cgv          __attribute__((aligned(4)));
    uint32_t gmask        __attribute__((aligned(4)));
#endif

#if HAVE_VIS
    uint64_t sparc_coeffs[10] __attribute__((aligned(8)));
#endif

    /* function pointers for swScale() */
    void (*yuv2nv12X  )(struct SwsContext *c,
                        const int16_t *lumFilter, const int16_t **lumSrc, int lumFilterSize,
                        const int16_t *chrFilter, const int16_t **chrSrc, int chrFilterSize,
                        uint8_t *dest, uint8_t *uDest,
                        int dstW, int chrDstW, int dstFormat);
    void (*yuv2yuv1   )(struct SwsContext *c,
                        const int16_t *lumSrc, const int16_t *chrSrc, const int16_t *alpSrc,
                        uint8_t *dest,
                        uint8_t *uDest, uint8_t *vDest, uint8_t *aDest,
                        long dstW, long chrDstW);
    void (*yuv2yuvX   )(struct SwsContext *c,
                        const int16_t *lumFilter, const int16_t **lumSrc, int lumFilterSize,
                        const int16_t *chrFilter, const int16_t **chrSrc, int chrFilterSize,
                        const int16_t **alpSrc,
                        uint8_t *dest,
                        uint8_t *uDest, uint8_t *vDest, uint8_t *aDest,
                        long dstW, long chrDstW);
    void (*yuv2packed1)(struct SwsContext *c,
                        const uint16_t *buf0,
                        const uint16_t *uvbuf0, const uint16_t *uvbuf1,
                        const uint16_t *abuf0,
                        uint8_t *dest,
                        int dstW, int uvalpha, int dstFormat, int flags, int y);
    void (*yuv2packed2)(struct SwsContext *c,
                        const uint16_t *buf0, const uint16_t *buf1,
                        const uint16_t *uvbuf0, const uint16_t *uvbuf1,
                        const uint16_t *abuf0, const uint16_t *abuf1,
                        uint8_t *dest,
                        int dstW, int yalpha, int uvalpha, int y);
    void (*yuv2packedX)(struct SwsContext *c,
                        const int16_t *lumFilter, const int16_t **lumSrc, int lumFilterSize,
                        const int16_t *chrFilter, const int16_t **chrSrc, int chrFilterSize,
                        const int16_t **alpSrc, uint8_t *dest,
                        long dstW, long dstY);

    void (*hyscale_internal)(uint8_t *dst, const uint8_t *src,
                             long width, uint32_t *pal);
    void (*hcscale_internal)(uint8_t *dstU, uint8_t *dstV,
                             const uint8_t *src1, const uint8_t *src2,
                             long width, uint32_t *pal);
    void (*hyscale_fast)(struct SwsContext *c,
                         int16_t *dst, int dstWidth,
                         const uint8_t *src, int srcW, int xInc);
    void (*hcscale_fast)(struct SwsContext *c,
                         int16_t *dst, int dstWidth,
                         const uint8_t *src1, const uint8_t *src2, int srcW, int xInc);

    void (*hScale)(int16_t *dst, int dstW, const uint8_t *src, int srcW,
                   int xInc, const int16_t *filter, const int16_t *filterPos, long filterSize);

} SwsContext;
//FIXME check init (where 0)

SwsFunc ff_yuv2rgb_get_func_ptr(SwsContext *c);
int ff_yuv2rgb_c_init_tables(SwsContext *c, const int inv_table[4], int fullRange, int brightness, int contrast, int saturation);

void ff_yuv2rgb_init_tables_altivec(SwsContext *c, const int inv_table[4], int brightness, int contrast, int saturation);
SwsFunc ff_yuv2rgb_init_mmx(SwsContext *c);
SwsFunc ff_yuv2rgb_init_vis(SwsContext *c);
SwsFunc ff_yuv2rgb_init_mlib(SwsContext *c);
SwsFunc ff_yuv2rgb_init_altivec(SwsContext *c);
SwsFunc ff_yuv2rgb_get_func_ptr_bfin(SwsContext *c);
void ff_bfin_get_unscaled_swscale(SwsContext *c);
void ff_yuv2packedX_altivec(SwsContext *c,
                          int16_t *lumFilter, int16_t **lumSrc, int lumFilterSize,
                          int16_t *chrFilter, int16_t **chrSrc, int chrFilterSize,
                          uint8_t *dest, int dstW, int dstY);

const char *sws_format_name(int format);

//FIXME replace this with something faster
#define isPlanarYUV(x)  (           \
           (x)==PIX_FMT_YUV410P     \
        || (x)==PIX_FMT_YUV420P     \
        || (x)==PIX_FMT_YUVA420P    \
        || (x)==PIX_FMT_YUV411P     \
        || (x)==PIX_FMT_YUV422P     \
        || (x)==PIX_FMT_YUV444P     \
        || (x)==PIX_FMT_YUV440P     \
        || (x)==PIX_FMT_NV12        \
        || (x)==PIX_FMT_NV21        \
    )
#define isYUV(x)        (           \
           (x)==PIX_FMT_UYVY422     \
        || (x)==PIX_FMT_YUYV422     \
        || isPlanarYUV(x)           \
    )
#define isGray(x)       (           \
           (x)==PIX_FMT_GRAY8       \
        || (x)==PIX_FMT_GRAY16BE    \
        || (x)==PIX_FMT_GRAY16LE    \
    )
#define isGray16(x)     (           \
           (x)==PIX_FMT_GRAY16BE    \
        || (x)==PIX_FMT_GRAY16LE    \
    )
#define isRGB(x)        (           \
           (x)==PIX_FMT_RGB32       \
        || (x)==PIX_FMT_RGB32_1     \
        || (x)==PIX_FMT_RGB24       \
        || (x)==PIX_FMT_RGB565      \
        || (x)==PIX_FMT_RGB555      \
        || (x)==PIX_FMT_RGB8        \
        || (x)==PIX_FMT_RGB4        \
        || (x)==PIX_FMT_RGB4_BYTE   \
        || (x)==PIX_FMT_MONOBLACK   \
        || (x)==PIX_FMT_MONOWHITE   \
    )
#define isBGR(x)        (           \
           (x)==PIX_FMT_BGR32       \
        || (x)==PIX_FMT_BGR32_1     \
        || (x)==PIX_FMT_BGR24       \
        || (x)==PIX_FMT_BGR565      \
        || (x)==PIX_FMT_BGR555      \
        || (x)==PIX_FMT_BGR8        \
        || (x)==PIX_FMT_BGR4        \
        || (x)==PIX_FMT_BGR4_BYTE   \
        || (x)==PIX_FMT_MONOBLACK   \
        || (x)==PIX_FMT_MONOWHITE   \
    )
#define isALPHA(x)      (           \
           (x)==PIX_FMT_BGR32       \
        || (x)==PIX_FMT_BGR32_1     \
        || (x)==PIX_FMT_RGB32       \
        || (x)==PIX_FMT_RGB32_1     \
        || (x)==PIX_FMT_YUVA420P    \
    )

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
        case PIX_FMT_MONOWHITE:
            return 1;
        default:
            return 0;
    }
}

extern const uint64_t ff_dither4[2];
extern const uint64_t ff_dither8[2];

extern const AVClass sws_context_class;

#endif /* SWSCALE_SWSCALE_INTERNAL_H */
