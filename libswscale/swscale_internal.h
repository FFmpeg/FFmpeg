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

#if ARCH_X86
#define VOFW 5120
#else
#define VOFW 2048 // faster on PPC and not tested on others
#endif

#define VOF  (VOFW*2)

#if HAVE_BIGENDIAN
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

typedef int (*SwsFunc)(struct SwsContext *context, const uint8_t* src[],
                       int srcStride[], int srcSliceY, int srcSliceH,
                       uint8_t* dst[], int dstStride[]);

/* This struct should be aligned on at least a 32-byte boundary. */
typedef struct SwsContext {
    /**
     * info on struct for av_log
     */
    const AVClass *av_class;

    /**
     * Note that src, dst, srcStride, dstStride will be copied in the
     * sws_scale() wrapper so they can be freely modified here.
     */
    SwsFunc swScale;
    int srcW;                     ///< Width  of source      luma/alpha planes.
    int srcH;                     ///< Height of source      luma/alpha planes.
    int dstH;                     ///< Height of destination luma/alpha planes.
    int chrSrcW;                  ///< Width  of source      chroma     planes.
    int chrSrcH;                  ///< Height of source      chroma     planes.
    int chrDstW;                  ///< Width  of destination chroma     planes.
    int chrDstH;                  ///< Height of destination chroma     planes.
    int lumXInc, chrXInc;
    int lumYInc, chrYInc;
    enum PixelFormat dstFormat;   ///< Destination pixel format.
    enum PixelFormat srcFormat;   ///< Source      pixel format.
    int dstFormatBpp;             ///< Number of bits per pixel of the destination pixel format.
    int srcFormatBpp;             ///< Number of bits per pixel of the source      pixel format.
    int chrSrcHSubSample;         ///< Binary logarithm of horizontal subsampling factor between luma/alpha and chroma planes in source      image.
    int chrSrcVSubSample;         ///< Binary logarithm of vertical   subsampling factor between luma/alpha and chroma planes in source      image.
    int chrDstHSubSample;         ///< Binary logarithm of horizontal subsampling factor between luma/alpha and chroma planes in destination image.
    int chrDstVSubSample;         ///< Binary logarithm of vertical   subsampling factor between luma/alpha and chroma planes in destination image.
    int vChrDrop;                 ///< Binary logarithm of extra vertical subsampling factor in source image chroma planes specified by user.
    int sliceDir;                 ///< Direction that slices are fed to the scaler (1 = top-to-bottom, -1 = bottom-to-top).
    double param[2];              ///< Input parameters for scaling algorithms that need them.

    uint32_t pal_yuv[256];
    uint32_t pal_rgb[256];

    /**
     * @name Scaled horizontal lines ring buffer.
     * The horizontal scaler keeps just enough scaled lines in a ring buffer
     * so they may be passed to the vertical scaler. The pointers to the
     * allocated buffers for each line are duplicated in sequence in the ring
     * buffer to simplify indexing and avoid wrapping around between lines
     * inside the vertical scaler code. The wrapping is done before the
     * vertical scaler is called.
     */
    //@{
    int16_t **lumPixBuf;          ///< Ring buffer for scaled horizontal luma   plane lines to be fed to the vertical scaler.
    int16_t **chrPixBuf;          ///< Ring buffer for scaled horizontal chroma plane lines to be fed to the vertical scaler.
    int16_t **alpPixBuf;          ///< Ring buffer for scaled horizontal alpha  plane lines to be fed to the vertical scaler.
    int       vLumBufSize;        ///< Number of vertical luma/alpha lines allocated in the ring buffer.
    int       vChrBufSize;        ///< Number of vertical chroma     lines allocated in the ring buffer.
    int       lastInLumBuf;       ///< Last scaled horizontal luma/alpha line from source in the ring buffer.
    int       lastInChrBuf;       ///< Last scaled horizontal chroma     line from source in the ring buffer.
    int       lumBufIndex;        ///< Index in ring buffer of the last scaled horizontal luma/alpha line from source.
    int       chrBufIndex;        ///< Index in ring buffer of the last scaled horizontal chroma     line from source.
    //@}

    uint8_t formatConvBuffer[VOF]; //FIXME dynamic allocation, but we have to change a lot of code for this to be useful

    /**
     * @name Horizontal and vertical filters.
     * To better understand the following fields, here is a pseudo-code of
     * their usage in filtering a horizontal line:
     * @code
     * for (i = 0; i < width; i++) {
     *     dst[i] = 0;
     *     for (j = 0; j < filterSize; j++)
     *         dst[i] += src[ filterPos[i] + j ] * filter[ filterSize * i + j ];
     *     dst[i] >>= FRAC_BITS; // The actual implementation is fixed-point.
     * }
     * @endcode
     */
    //@{
    int16_t *hLumFilter;          ///< Array of horizontal filter coefficients for luma/alpha planes.
    int16_t *hChrFilter;          ///< Array of horizontal filter coefficients for chroma     planes.
    int16_t *vLumFilter;          ///< Array of vertical   filter coefficients for luma/alpha planes.
    int16_t *vChrFilter;          ///< Array of vertical   filter coefficients for chroma     planes.
    int16_t *hLumFilterPos;       ///< Array of horizontal filter starting positions for each dst[i] for luma/alpha planes.
    int16_t *hChrFilterPos;       ///< Array of horizontal filter starting positions for each dst[i] for chroma     planes.
    int16_t *vLumFilterPos;       ///< Array of vertical   filter starting positions for each dst[i] for luma/alpha planes.
    int16_t *vChrFilterPos;       ///< Array of vertical   filter starting positions for each dst[i] for chroma     planes.
    int      hLumFilterSize;      ///< Horizontal filter size for luma/alpha pixels.
    int      hChrFilterSize;      ///< Horizontal filter size for chroma     pixels.
    int      vLumFilterSize;      ///< Vertical   filter size for luma/alpha pixels.
    int      vChrFilterSize;      ///< Vertical   filter size for chroma     pixels.
    //@}

    int lumMmx2FilterCodeSize;    ///< Runtime-generated MMX2 horizontal fast bilinear scaler code size for luma/alpha planes.
    int chrMmx2FilterCodeSize;    ///< Runtime-generated MMX2 horizontal fast bilinear scaler code size for chroma     planes.
    uint8_t *lumMmx2FilterCode;   ///< Runtime-generated MMX2 horizontal fast bilinear scaler code for luma/alpha planes.
    uint8_t *chrMmx2FilterCode;   ///< Runtime-generated MMX2 horizontal fast bilinear scaler code for chroma     planes.

    int canMMX2BeUsed;

    int dstY;                     ///< Last destination vertical line output from last slice.
    int flags;                    ///< Flags passed by the user to select scaler algorithm, optimizations, subsampling, etc...
    void * yuvTable;            // pointer to the yuv->rgb table start so it can be freed()
    uint8_t * table_rV[256];
    uint8_t * table_gU[256];
    int    table_gV[256];
    uint8_t * table_bU[256];

    //Colorspace stuff
    int contrast, brightness, saturation;    // for sws_getColorspaceDetails
    int srcColorspaceTable[4];
    int dstColorspaceTable[4];
    int srcRange;                 ///< 0 = MPG YUV range, 1 = JPG YUV range (source      image).
    int dstRange;                 ///< 0 = MPG YUV range, 1 = JPG YUV range (destination image).
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

    DECLARE_ALIGNED(8, uint64_t, redDither);
    DECLARE_ALIGNED(8, uint64_t, greenDither);
    DECLARE_ALIGNED(8, uint64_t, blueDither);

    DECLARE_ALIGNED(8, uint64_t, yCoeff);
    DECLARE_ALIGNED(8, uint64_t, vrCoeff);
    DECLARE_ALIGNED(8, uint64_t, ubCoeff);
    DECLARE_ALIGNED(8, uint64_t, vgCoeff);
    DECLARE_ALIGNED(8, uint64_t, ugCoeff);
    DECLARE_ALIGNED(8, uint64_t, yOffset);
    DECLARE_ALIGNED(8, uint64_t, uOffset);
    DECLARE_ALIGNED(8, uint64_t, vOffset);
    int32_t  lumMmxFilter[4*MAX_FILTER_SIZE];
    int32_t  chrMmxFilter[4*MAX_FILTER_SIZE];
    int dstW;                     ///< Width  of destination luma/alpha planes.
    DECLARE_ALIGNED(8, uint64_t, esp);
    DECLARE_ALIGNED(8, uint64_t, vRounder);
    DECLARE_ALIGNED(8, uint64_t, u_temp);
    DECLARE_ALIGNED(8, uint64_t, v_temp);
    DECLARE_ALIGNED(8, uint64_t, y_temp);
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
    DECLARE_ALIGNED(4, uint32_t, oy);
    DECLARE_ALIGNED(4, uint32_t, oc);
    DECLARE_ALIGNED(4, uint32_t, zero);
    DECLARE_ALIGNED(4, uint32_t, cy);
    DECLARE_ALIGNED(4, uint32_t, crv);
    DECLARE_ALIGNED(4, uint32_t, rmask);
    DECLARE_ALIGNED(4, uint32_t, cbu);
    DECLARE_ALIGNED(4, uint32_t, bmask);
    DECLARE_ALIGNED(4, uint32_t, cgu);
    DECLARE_ALIGNED(4, uint32_t, cgv);
    DECLARE_ALIGNED(4, uint32_t, gmask);
#endif

#if HAVE_VIS
    DECLARE_ALIGNED(8, uint64_t, sparc_coeffs)[10];
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

    void (*lumToYV12)(uint8_t *dst, const uint8_t *src,
                      long width, uint32_t *pal); ///< Unscaled conversion of luma plane to YV12 for horizontal scaler.
    void (*alpToYV12)(uint8_t *dst, const uint8_t *src,
                      long width, uint32_t *pal); ///< Unscaled conversion of alpha plane to YV12 for horizontal scaler.
    void (*chrToYV12)(uint8_t *dstU, uint8_t *dstV,
                      const uint8_t *src1, const uint8_t *src2,
                      long width, uint32_t *pal); ///< Unscaled conversion of chroma planes to YV12 for horizontal scaler.
    void (*hyscale_fast)(struct SwsContext *c,
                         int16_t *dst, long dstWidth,
                         const uint8_t *src, int srcW, int xInc);
    void (*hcscale_fast)(struct SwsContext *c,
                         int16_t *dst, long dstWidth,
                         const uint8_t *src1, const uint8_t *src2,
                         int srcW, int xInc);

    void (*hScale)(int16_t *dst, int dstW, const uint8_t *src, int srcW,
                   int xInc, const int16_t *filter, const int16_t *filterPos,
                   long filterSize);

    void (*lumConvertRange)(uint16_t *dst, int width); ///< Color range conversion function for luma plane if needed.
    void (*chrConvertRange)(uint16_t *dst, int width); ///< Color range conversion function for chroma planes if needed.

    int lumSrcOffset; ///< Offset given to luma src pointers passed to horizontal input functions.
    int chrSrcOffset; ///< Offset given to chroma src pointers passed to horizontal input functions.
    int alpSrcOffset; ///< Offset given to alpha src pointers passed to horizontal input functions.

    int needs_hcscale; ///< Set if there are chroma planes to be converted.

} SwsContext;
//FIXME check init (where 0)

SwsFunc ff_yuv2rgb_get_func_ptr(SwsContext *c);
int ff_yuv2rgb_c_init_tables(SwsContext *c, const int inv_table[4],
                             int fullRange, int brightness,
                             int contrast, int saturation);

void ff_yuv2rgb_init_tables_altivec(SwsContext *c, const int inv_table[4],
                                    int brightness, int contrast, int saturation);
SwsFunc ff_yuv2rgb_init_mmx(SwsContext *c);
SwsFunc ff_yuv2rgb_init_vis(SwsContext *c);
SwsFunc ff_yuv2rgb_init_mlib(SwsContext *c);
SwsFunc ff_yuv2rgb_init_altivec(SwsContext *c);
SwsFunc ff_yuv2rgb_get_func_ptr_bfin(SwsContext *c);
void ff_bfin_get_unscaled_swscale(SwsContext *c);
void ff_yuv2packedX_altivec(SwsContext *c,
                            const int16_t *lumFilter, int16_t **lumSrc, int lumFilterSize,
                            const int16_t *chrFilter, int16_t **chrSrc, int chrFilterSize,
                            uint8_t *dest, int dstW, int dstY);

const char *sws_format_name(enum PixelFormat format);

//FIXME replace this with something faster
#define is16BPS(x)      (           \
           (x)==PIX_FMT_GRAY16BE    \
        || (x)==PIX_FMT_GRAY16LE    \
        || (x)==PIX_FMT_RGB48BE     \
        || (x)==PIX_FMT_RGB48LE     \
        || (x)==PIX_FMT_YUV420P16LE   \
        || (x)==PIX_FMT_YUV422P16LE   \
        || (x)==PIX_FMT_YUV444P16LE   \
        || (x)==PIX_FMT_YUV420P16BE   \
        || (x)==PIX_FMT_YUV422P16BE   \
        || (x)==PIX_FMT_YUV444P16BE   \
    )
#define isBE(x) ((x)&1)
#define isPlanar8YUV(x) (           \
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
#define isPlanarYUV(x)  (           \
        isPlanar8YUV(x)             \
        || (x)==PIX_FMT_YUV420P16LE   \
        || (x)==PIX_FMT_YUV422P16LE   \
        || (x)==PIX_FMT_YUV444P16LE   \
        || (x)==PIX_FMT_YUV420P16BE   \
        || (x)==PIX_FMT_YUV422P16BE   \
        || (x)==PIX_FMT_YUV444P16BE   \
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
#define isRGBinInt(x)   (           \
           (x)==PIX_FMT_RGB48BE     \
        || (x)==PIX_FMT_RGB48LE     \
        || (x)==PIX_FMT_RGB32       \
        || (x)==PIX_FMT_RGB32_1     \
        || (x)==PIX_FMT_RGB24       \
        || (x)==PIX_FMT_RGB565BE    \
        || (x)==PIX_FMT_RGB565LE    \
        || (x)==PIX_FMT_RGB555BE    \
        || (x)==PIX_FMT_RGB555LE    \
        || (x)==PIX_FMT_RGB444BE    \
        || (x)==PIX_FMT_RGB444LE    \
        || (x)==PIX_FMT_RGB8        \
        || (x)==PIX_FMT_RGB4        \
        || (x)==PIX_FMT_RGB4_BYTE   \
        || (x)==PIX_FMT_MONOBLACK   \
        || (x)==PIX_FMT_MONOWHITE   \
    )
#define isBGRinInt(x)   (           \
           (x)==PIX_FMT_BGR32       \
        || (x)==PIX_FMT_BGR32_1     \
        || (x)==PIX_FMT_BGR24       \
        || (x)==PIX_FMT_BGR565BE    \
        || (x)==PIX_FMT_BGR565LE    \
        || (x)==PIX_FMT_BGR555BE    \
        || (x)==PIX_FMT_BGR555LE    \
        || (x)==PIX_FMT_BGR444BE    \
        || (x)==PIX_FMT_BGR444LE    \
        || (x)==PIX_FMT_BGR8        \
        || (x)==PIX_FMT_BGR4        \
        || (x)==PIX_FMT_BGR4_BYTE   \
        || (x)==PIX_FMT_MONOBLACK   \
        || (x)==PIX_FMT_MONOWHITE   \
    )
#define isRGBinBytes(x) (           \
           (x)==PIX_FMT_RGB48BE     \
        || (x)==PIX_FMT_RGB48LE     \
        || (x)==PIX_FMT_RGBA        \
        || (x)==PIX_FMT_ARGB        \
        || (x)==PIX_FMT_RGB24       \
    )
#define isBGRinBytes(x) (           \
           (x)==PIX_FMT_BGRA        \
        || (x)==PIX_FMT_ABGR        \
        || (x)==PIX_FMT_BGR24       \
    )
#define isAnyRGB(x)     (           \
            isRGBinInt(x)           \
        ||  isBGRinInt(x)           \
    )
#define isALPHA(x)      (           \
           (x)==PIX_FMT_BGR32       \
        || (x)==PIX_FMT_BGR32_1     \
        || (x)==PIX_FMT_RGB32       \
        || (x)==PIX_FMT_RGB32_1     \
        || (x)==PIX_FMT_YUVA420P    \
    )
#define usePal(x) (av_pix_fmt_descriptors[x].flags & PIX_FMT_PAL)

extern const uint64_t ff_dither4[2];
extern const uint64_t ff_dither8[2];

extern const AVClass sws_context_class;

/**
 * Sets c->swScale to an unscaled converter if one exists for the specific
 * source and destination formats, bit depths, flags, etc.
 */
void ff_get_unscaled_swscale(SwsContext *c);

/**
 * Returns the SWS_CPU_CAPS for the optimized code compiled into swscale.
 */
int ff_hardcodedcpuflags(void);

/**
 * Returns function pointer to fastest main scaler path function depending
 * on architecture and available optimizations.
 */
SwsFunc ff_getSwsFunc(SwsContext *c);

#endif /* SWSCALE_SWSCALE_INTERNAL_H */
