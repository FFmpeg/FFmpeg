/*
 * Copyright (C) 2001-2011 Michael Niedermayer <michaelni@gmx.at>
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

#include "version.h"

#include "libavutil/avassert.h"
#include "libavutil/avutil.h"
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/pixfmt.h"
#include "libavutil/pixdesc.h"

#define STR(s) AV_TOSTRING(s) // AV_STRINGIFY is too long

#define YUVRGB_TABLE_HEADROOM 512
#define YUVRGB_TABLE_LUMA_HEADROOM 512

#define MAX_FILTER_SIZE SWS_MAX_FILTER_SIZE

#define DITHER1XBPP

#if HAVE_BIGENDIAN
#define ALT32_CORR (-1)
#else
#define ALT32_CORR   1
#endif

#if ARCH_X86_64
#   define APCK_PTR2  8
#   define APCK_COEF 16
#   define APCK_SIZE 24
#else
#   define APCK_PTR2  4
#   define APCK_COEF  8
#   define APCK_SIZE 16
#endif

#define RETCODE_USE_CASCADE -12345

struct SwsContext;

typedef enum SwsDither {
    SWS_DITHER_NONE = 0,
    SWS_DITHER_AUTO,
    SWS_DITHER_BAYER,
    SWS_DITHER_ED,
    SWS_DITHER_A_DITHER,
    SWS_DITHER_X_DITHER,
    NB_SWS_DITHER,
} SwsDither;

typedef enum SwsAlphaBlend {
    SWS_ALPHA_BLEND_NONE  = 0,
    SWS_ALPHA_BLEND_UNIFORM,
    SWS_ALPHA_BLEND_CHECKERBOARD,
    SWS_ALPHA_BLEND_NB,
} SwsAlphaBlend;

typedef int (*SwsFunc)(struct SwsContext *context, const uint8_t *src[],
                       int srcStride[], int srcSliceY, int srcSliceH,
                       uint8_t *dst[], int dstStride[]);

/**
 * Write one line of horizontally scaled data to planar output
 * without any additional vertical scaling (or point-scaling).
 *
 * @param src     scaled source data, 15 bits for 8-10-bit output,
 *                19 bits for 16-bit output (in int32_t)
 * @param dest    pointer to the output plane. For >8-bit
 *                output, this is in uint16_t
 * @param dstW    width of destination in pixels
 * @param dither  ordered dither array of type int16_t and size 8
 * @param offset  Dither offset
 */
typedef void (*yuv2planar1_fn)(const int16_t *src, uint8_t *dest, int dstW,
                               const uint8_t *dither, int offset);

/**
 * Write one line of horizontally scaled data to planar output
 * with multi-point vertical scaling between input pixels.
 *
 * @param filter        vertical luma/alpha scaling coefficients, 12 bits [0,4096]
 * @param src           scaled luma (Y) or alpha (A) source data, 15 bits for
 *                      8-10-bit output, 19 bits for 16-bit output (in int32_t)
 * @param filterSize    number of vertical input lines to scale
 * @param dest          pointer to output plane. For >8-bit
 *                      output, this is in uint16_t
 * @param dstW          width of destination pixels
 * @param offset        Dither offset
 */
typedef void (*yuv2planarX_fn)(const int16_t *filter, int filterSize,
                               const int16_t **src, uint8_t *dest, int dstW,
                               const uint8_t *dither, int offset);

/**
 * Write one line of horizontally scaled chroma to interleaved output
 * with multi-point vertical scaling between input pixels.
 *
 * @param c             SWS scaling context
 * @param chrFilter     vertical chroma scaling coefficients, 12 bits [0,4096]
 * @param chrUSrc       scaled chroma (U) source data, 15 bits for 8-10-bit
 *                      output, 19 bits for 16-bit output (in int32_t)
 * @param chrVSrc       scaled chroma (V) source data, 15 bits for 8-10-bit
 *                      output, 19 bits for 16-bit output (in int32_t)
 * @param chrFilterSize number of vertical chroma input lines to scale
 * @param dest          pointer to the output plane. For >8-bit
 *                      output, this is in uint16_t
 * @param dstW          width of chroma planes
 */
typedef void (*yuv2interleavedX_fn)(struct SwsContext *c,
                                    const int16_t *chrFilter,
                                    int chrFilterSize,
                                    const int16_t **chrUSrc,
                                    const int16_t **chrVSrc,
                                    uint8_t *dest, int dstW);

/**
 * Write one line of horizontally scaled Y/U/V/A to packed-pixel YUV/RGB
 * output without any additional vertical scaling (or point-scaling). Note
 * that this function may do chroma scaling, see the "uvalpha" argument.
 *
 * @param c       SWS scaling context
 * @param lumSrc  scaled luma (Y) source data, 15 bits for 8-10-bit output,
 *                19 bits for 16-bit output (in int32_t)
 * @param chrUSrc scaled chroma (U) source data, 15 bits for 8-10-bit output,
 *                19 bits for 16-bit output (in int32_t)
 * @param chrVSrc scaled chroma (V) source data, 15 bits for 8-10-bit output,
 *                19 bits for 16-bit output (in int32_t)
 * @param alpSrc  scaled alpha (A) source data, 15 bits for 8-10-bit output,
 *                19 bits for 16-bit output (in int32_t)
 * @param dest    pointer to the output plane. For 16-bit output, this is
 *                uint16_t
 * @param dstW    width of lumSrc and alpSrc in pixels, number of pixels
 *                to write into dest[]
 * @param uvalpha chroma scaling coefficient for the second line of chroma
 *                pixels, either 2048 or 0. If 0, one chroma input is used
 *                for 2 output pixels (or if the SWS_FLAG_FULL_CHR_INT flag
 *                is set, it generates 1 output pixel). If 2048, two chroma
 *                input pixels should be averaged for 2 output pixels (this
 *                only happens if SWS_FLAG_FULL_CHR_INT is not set)
 * @param y       vertical line number for this output. This does not need
 *                to be used to calculate the offset in the destination,
 *                but can be used to generate comfort noise using dithering
 *                for some output formats.
 */
typedef void (*yuv2packed1_fn)(struct SwsContext *c, const int16_t *lumSrc,
                               const int16_t *chrUSrc[2],
                               const int16_t *chrVSrc[2],
                               const int16_t *alpSrc, uint8_t *dest,
                               int dstW, int uvalpha, int y);
/**
 * Write one line of horizontally scaled Y/U/V/A to packed-pixel YUV/RGB
 * output by doing bilinear scaling between two input lines.
 *
 * @param c       SWS scaling context
 * @param lumSrc  scaled luma (Y) source data, 15 bits for 8-10-bit output,
 *                19 bits for 16-bit output (in int32_t)
 * @param chrUSrc scaled chroma (U) source data, 15 bits for 8-10-bit output,
 *                19 bits for 16-bit output (in int32_t)
 * @param chrVSrc scaled chroma (V) source data, 15 bits for 8-10-bit output,
 *                19 bits for 16-bit output (in int32_t)
 * @param alpSrc  scaled alpha (A) source data, 15 bits for 8-10-bit output,
 *                19 bits for 16-bit output (in int32_t)
 * @param dest    pointer to the output plane. For 16-bit output, this is
 *                uint16_t
 * @param dstW    width of lumSrc and alpSrc in pixels, number of pixels
 *                to write into dest[]
 * @param yalpha  luma/alpha scaling coefficients for the second input line.
 *                The first line's coefficients can be calculated by using
 *                4096 - yalpha
 * @param uvalpha chroma scaling coefficient for the second input line. The
 *                first line's coefficients can be calculated by using
 *                4096 - uvalpha
 * @param y       vertical line number for this output. This does not need
 *                to be used to calculate the offset in the destination,
 *                but can be used to generate comfort noise using dithering
 *                for some output formats.
 */
typedef void (*yuv2packed2_fn)(struct SwsContext *c, const int16_t *lumSrc[2],
                               const int16_t *chrUSrc[2],
                               const int16_t *chrVSrc[2],
                               const int16_t *alpSrc[2],
                               uint8_t *dest,
                               int dstW, int yalpha, int uvalpha, int y);
/**
 * Write one line of horizontally scaled Y/U/V/A to packed-pixel YUV/RGB
 * output by doing multi-point vertical scaling between input pixels.
 *
 * @param c             SWS scaling context
 * @param lumFilter     vertical luma/alpha scaling coefficients, 12 bits [0,4096]
 * @param lumSrc        scaled luma (Y) source data, 15 bits for 8-10-bit output,
 *                      19 bits for 16-bit output (in int32_t)
 * @param lumFilterSize number of vertical luma/alpha input lines to scale
 * @param chrFilter     vertical chroma scaling coefficients, 12 bits [0,4096]
 * @param chrUSrc       scaled chroma (U) source data, 15 bits for 8-10-bit output,
 *                      19 bits for 16-bit output (in int32_t)
 * @param chrVSrc       scaled chroma (V) source data, 15 bits for 8-10-bit output,
 *                      19 bits for 16-bit output (in int32_t)
 * @param chrFilterSize number of vertical chroma input lines to scale
 * @param alpSrc        scaled alpha (A) source data, 15 bits for 8-10-bit output,
 *                      19 bits for 16-bit output (in int32_t)
 * @param dest          pointer to the output plane. For 16-bit output, this is
 *                      uint16_t
 * @param dstW          width of lumSrc and alpSrc in pixels, number of pixels
 *                      to write into dest[]
 * @param y             vertical line number for this output. This does not need
 *                      to be used to calculate the offset in the destination,
 *                      but can be used to generate comfort noise using dithering
 *                      or some output formats.
 */
typedef void (*yuv2packedX_fn)(struct SwsContext *c, const int16_t *lumFilter,
                               const int16_t **lumSrc, int lumFilterSize,
                               const int16_t *chrFilter,
                               const int16_t **chrUSrc,
                               const int16_t **chrVSrc, int chrFilterSize,
                               const int16_t **alpSrc, uint8_t *dest,
                               int dstW, int y);

/**
 * Write one line of horizontally scaled Y/U/V/A to YUV/RGB
 * output by doing multi-point vertical scaling between input pixels.
 *
 * @param c             SWS scaling context
 * @param lumFilter     vertical luma/alpha scaling coefficients, 12 bits [0,4096]
 * @param lumSrc        scaled luma (Y) source data, 15 bits for 8-10-bit output,
 *                      19 bits for 16-bit output (in int32_t)
 * @param lumFilterSize number of vertical luma/alpha input lines to scale
 * @param chrFilter     vertical chroma scaling coefficients, 12 bits [0,4096]
 * @param chrUSrc       scaled chroma (U) source data, 15 bits for 8-10-bit output,
 *                      19 bits for 16-bit output (in int32_t)
 * @param chrVSrc       scaled chroma (V) source data, 15 bits for 8-10-bit output,
 *                      19 bits for 16-bit output (in int32_t)
 * @param chrFilterSize number of vertical chroma input lines to scale
 * @param alpSrc        scaled alpha (A) source data, 15 bits for 8-10-bit output,
 *                      19 bits for 16-bit output (in int32_t)
 * @param dest          pointer to the output planes. For 16-bit output, this is
 *                      uint16_t
 * @param dstW          width of lumSrc and alpSrc in pixels, number of pixels
 *                      to write into dest[]
 * @param y             vertical line number for this output. This does not need
 *                      to be used to calculate the offset in the destination,
 *                      but can be used to generate comfort noise using dithering
 *                      or some output formats.
 */
typedef void (*yuv2anyX_fn)(struct SwsContext *c, const int16_t *lumFilter,
                            const int16_t **lumSrc, int lumFilterSize,
                            const int16_t *chrFilter,
                            const int16_t **chrUSrc,
                            const int16_t **chrVSrc, int chrFilterSize,
                            const int16_t **alpSrc, uint8_t **dest,
                            int dstW, int y);

struct SwsSlice;
struct SwsFilterDescriptor;

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
    SwsFunc swscale;
    int srcW;                     ///< Width  of source      luma/alpha planes.
    int srcH;                     ///< Height of source      luma/alpha planes.
    int dstH;                     ///< Height of destination luma/alpha planes.
    int chrSrcW;                  ///< Width  of source      chroma     planes.
    int chrSrcH;                  ///< Height of source      chroma     planes.
    int chrDstW;                  ///< Width  of destination chroma     planes.
    int chrDstH;                  ///< Height of destination chroma     planes.
    int lumXInc, chrXInc;
    int lumYInc, chrYInc;
    enum AVPixelFormat dstFormat; ///< Destination pixel format.
    enum AVPixelFormat srcFormat; ///< Source      pixel format.
    int dstFormatBpp;             ///< Number of bits per pixel of the destination pixel format.
    int srcFormatBpp;             ///< Number of bits per pixel of the source      pixel format.
    int dstBpc, srcBpc;
    int chrSrcHSubSample;         ///< Binary logarithm of horizontal subsampling factor between luma/alpha and chroma planes in source      image.
    int chrSrcVSubSample;         ///< Binary logarithm of vertical   subsampling factor between luma/alpha and chroma planes in source      image.
    int chrDstHSubSample;         ///< Binary logarithm of horizontal subsampling factor between luma/alpha and chroma planes in destination image.
    int chrDstVSubSample;         ///< Binary logarithm of vertical   subsampling factor between luma/alpha and chroma planes in destination image.
    int vChrDrop;                 ///< Binary logarithm of extra vertical subsampling factor in source image chroma planes specified by user.
    int sliceDir;                 ///< Direction that slices are fed to the scaler (1 = top-to-bottom, -1 = bottom-to-top).
    double param[2];              ///< Input parameters for scaling algorithms that need them.

    /* The cascaded_* fields allow spliting a scaler task into multiple
     * sequential steps, this is for example used to limit the maximum
     * downscaling factor that needs to be supported in one scaler.
     */
    struct SwsContext *cascaded_context[3];
    int cascaded_tmpStride[4];
    uint8_t *cascaded_tmp[4];
    int cascaded1_tmpStride[4];
    uint8_t *cascaded1_tmp[4];
    int cascaded_mainindex;

    double gamma_value;
    int gamma_flag;
    int is_internal_gamma;
    uint16_t *gamma;
    uint16_t *inv_gamma;

    int numDesc;
    int descIndex[2];
    int numSlice;
    struct SwsSlice *slice;
    struct SwsFilterDescriptor *desc;

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
    int lastInLumBuf;             ///< Last scaled horizontal luma/alpha line from source in the ring buffer.
    int lastInChrBuf;             ///< Last scaled horizontal chroma     line from source in the ring buffer.
    int lumBufIndex;              ///< Index in ring buffer of the last scaled horizontal luma/alpha line from source.
    int chrBufIndex;              ///< Index in ring buffer of the last scaled horizontal chroma     line from source.
    //@}

    uint8_t *formatConvBuffer;
    int needAlpha;

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
    int32_t *hLumFilterPos;       ///< Array of horizontal filter starting positions for each dst[i] for luma/alpha planes.
    int32_t *hChrFilterPos;       ///< Array of horizontal filter starting positions for each dst[i] for chroma     planes.
    int32_t *vLumFilterPos;       ///< Array of vertical   filter starting positions for each dst[i] for luma/alpha planes.
    int32_t *vChrFilterPos;       ///< Array of vertical   filter starting positions for each dst[i] for chroma     planes.
    int hLumFilterSize;           ///< Horizontal filter size for luma/alpha pixels.
    int hChrFilterSize;           ///< Horizontal filter size for chroma     pixels.
    int vLumFilterSize;           ///< Vertical   filter size for luma/alpha pixels.
    int vChrFilterSize;           ///< Vertical   filter size for chroma     pixels.
    //@}

    int lumMmxextFilterCodeSize;  ///< Runtime-generated MMXEXT horizontal fast bilinear scaler code size for luma/alpha planes.
    int chrMmxextFilterCodeSize;  ///< Runtime-generated MMXEXT horizontal fast bilinear scaler code size for chroma planes.
    uint8_t *lumMmxextFilterCode; ///< Runtime-generated MMXEXT horizontal fast bilinear scaler code for luma/alpha planes.
    uint8_t *chrMmxextFilterCode; ///< Runtime-generated MMXEXT horizontal fast bilinear scaler code for chroma planes.

    int canMMXEXTBeUsed;
    int warned_unuseable_bilinear;

    int dstY;                     ///< Last destination vertical line output from last slice.
    int flags;                    ///< Flags passed by the user to select scaler algorithm, optimizations, subsampling, etc...
    void *yuvTable;             // pointer to the yuv->rgb table start so it can be freed()
    // alignment ensures the offset can be added in a single
    // instruction on e.g. ARM
    DECLARE_ALIGNED(16, int, table_gV)[256 + 2*YUVRGB_TABLE_HEADROOM];
    uint8_t *table_rV[256 + 2*YUVRGB_TABLE_HEADROOM];
    uint8_t *table_gU[256 + 2*YUVRGB_TABLE_HEADROOM];
    uint8_t *table_bU[256 + 2*YUVRGB_TABLE_HEADROOM];
    DECLARE_ALIGNED(16, int32_t, input_rgb2yuv_table)[16+40*4]; // This table can contain both C and SIMD formatted values, the C vales are always at the XY_IDX points
#define RY_IDX 0
#define GY_IDX 1
#define BY_IDX 2
#define RU_IDX 3
#define GU_IDX 4
#define BU_IDX 5
#define RV_IDX 6
#define GV_IDX 7
#define BV_IDX 8
#define RGB2YUV_SHIFT 15

    int *dither_error[4];

    //Colorspace stuff
    int contrast, brightness, saturation;    // for sws_getColorspaceDetails
    int srcColorspaceTable[4];
    int dstColorspaceTable[4];
    int srcRange;                 ///< 0 = MPG YUV range, 1 = JPG YUV range (source      image).
    int dstRange;                 ///< 0 = MPG YUV range, 1 = JPG YUV range (destination image).
    int src0Alpha;
    int dst0Alpha;
    int srcXYZ;
    int dstXYZ;
    int src_h_chr_pos;
    int dst_h_chr_pos;
    int src_v_chr_pos;
    int dst_v_chr_pos;
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
#define CHR_MMX_FILTER_OFFSET "11*8+4*4*"AV_STRINGIFY(MAX_FILTER_SIZE)
#define DSTW_OFFSET           "11*8+4*4*"AV_STRINGIFY(MAX_FILTER_SIZE)"*2"
#define ESP_OFFSET            "11*8+4*4*"AV_STRINGIFY(MAX_FILTER_SIZE)"*2+8"
#define VROUNDER_OFFSET       "11*8+4*4*"AV_STRINGIFY(MAX_FILTER_SIZE)"*2+16"
#define U_TEMP                "11*8+4*4*"AV_STRINGIFY(MAX_FILTER_SIZE)"*2+24"
#define V_TEMP                "11*8+4*4*"AV_STRINGIFY(MAX_FILTER_SIZE)"*2+32"
#define Y_TEMP                "11*8+4*4*"AV_STRINGIFY(MAX_FILTER_SIZE)"*2+40"
#define ALP_MMX_FILTER_OFFSET "11*8+4*4*"AV_STRINGIFY(MAX_FILTER_SIZE)"*2+48"
#define UV_OFF_PX             "11*8+4*4*"AV_STRINGIFY(MAX_FILTER_SIZE)"*3+48"
#define UV_OFF_BYTE           "11*8+4*4*"AV_STRINGIFY(MAX_FILTER_SIZE)"*3+56"
#define DITHER16              "11*8+4*4*"AV_STRINGIFY(MAX_FILTER_SIZE)"*3+64"
#define DITHER32              "11*8+4*4*"AV_STRINGIFY(MAX_FILTER_SIZE)"*3+80"
#define DITHER32_INT          (11*8+4*4*MAX_FILTER_SIZE*3+80) // value equal to above, used for checking that the struct hasn't been changed by mistake

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
    int32_t lumMmxFilter[4 * MAX_FILTER_SIZE];
    int32_t chrMmxFilter[4 * MAX_FILTER_SIZE];
    int dstW;                     ///< Width  of destination luma/alpha planes.
    DECLARE_ALIGNED(8, uint64_t, esp);
    DECLARE_ALIGNED(8, uint64_t, vRounder);
    DECLARE_ALIGNED(8, uint64_t, u_temp);
    DECLARE_ALIGNED(8, uint64_t, v_temp);
    DECLARE_ALIGNED(8, uint64_t, y_temp);
    int32_t alpMmxFilter[4 * MAX_FILTER_SIZE];
    // alignment of these values is not necessary, but merely here
    // to maintain the same offset across x8632 and x86-64. Once we
    // use proper offset macros in the asm, they can be removed.
    DECLARE_ALIGNED(8, ptrdiff_t, uv_off); ///< offset (in pixels) between u and v planes
    DECLARE_ALIGNED(8, ptrdiff_t, uv_offx2); ///< offset (in bytes) between u and v planes
    DECLARE_ALIGNED(8, uint16_t, dither16)[8];
    DECLARE_ALIGNED(8, uint32_t, dither32)[8];

    const uint8_t *chrDither8, *lumDither8;

#if HAVE_ALTIVEC
    vector signed short   CY;
    vector signed short   CRV;
    vector signed short   CBU;
    vector signed short   CGU;
    vector signed short   CGV;
    vector signed short   OY;
    vector unsigned short CSHIFT;
    vector signed short  *vYCoeffsBank, *vCCoeffsBank;
#endif

    int use_mmx_vfilter;

/* pre defined color-spaces gamma */
#define XYZ_GAMMA (2.6f)
#define RGB_GAMMA (2.2f)
    int16_t *xyzgamma;
    int16_t *rgbgamma;
    int16_t *xyzgammainv;
    int16_t *rgbgammainv;
    int16_t xyz2rgb_matrix[3][4];
    int16_t rgb2xyz_matrix[3][4];

    /* function pointers for swscale() */
    yuv2planar1_fn yuv2plane1;
    yuv2planarX_fn yuv2planeX;
    yuv2interleavedX_fn yuv2nv12cX;
    yuv2packed1_fn yuv2packed1;
    yuv2packed2_fn yuv2packed2;
    yuv2packedX_fn yuv2packedX;
    yuv2anyX_fn yuv2anyX;

    /// Unscaled conversion of luma plane to YV12 for horizontal scaler.
    void (*lumToYV12)(uint8_t *dst, const uint8_t *src, const uint8_t *src2, const uint8_t *src3,
                      int width, uint32_t *pal);
    /// Unscaled conversion of alpha plane to YV12 for horizontal scaler.
    void (*alpToYV12)(uint8_t *dst, const uint8_t *src, const uint8_t *src2, const uint8_t *src3,
                      int width, uint32_t *pal);
    /// Unscaled conversion of chroma planes to YV12 for horizontal scaler.
    void (*chrToYV12)(uint8_t *dstU, uint8_t *dstV,
                      const uint8_t *src1, const uint8_t *src2, const uint8_t *src3,
                      int width, uint32_t *pal);

    /**
     * Functions to read planar input, such as planar RGB, and convert
     * internally to Y/UV/A.
     */
    /** @{ */
    void (*readLumPlanar)(uint8_t *dst, const uint8_t *src[4], int width, int32_t *rgb2yuv);
    void (*readChrPlanar)(uint8_t *dstU, uint8_t *dstV, const uint8_t *src[4],
                          int width, int32_t *rgb2yuv);
    void (*readAlpPlanar)(uint8_t *dst, const uint8_t *src[4], int width, int32_t *rgb2yuv);
    /** @} */

    /**
     * Scale one horizontal line of input data using a bilinear filter
     * to produce one line of output data. Compared to SwsContext->hScale(),
     * please take note of the following caveats when using these:
     * - Scaling is done using only 7 bits instead of 14-bit coefficients.
     * - You can use no more than 5 input pixels to produce 4 output
     *   pixels. Therefore, this filter should not be used for downscaling
     *   by more than ~20% in width (because that equals more than 5/4th
     *   downscaling and thus more than 5 pixels input per 4 pixels output).
     * - In general, bilinear filters create artifacts during downscaling
     *   (even when <20%), because one output pixel will span more than one
     *   input pixel, and thus some pixels will need edges of both neighbor
     *   pixels to interpolate the output pixel. Since you can use at most
     *   two input pixels per output pixel in bilinear scaling, this is
     *   impossible and thus downscaling by any size will create artifacts.
     * To enable this type of scaling, set SWS_FLAG_FAST_BILINEAR
     * in SwsContext->flags.
     */
    /** @{ */
    void (*hyscale_fast)(struct SwsContext *c,
                         int16_t *dst, int dstWidth,
                         const uint8_t *src, int srcW, int xInc);
    void (*hcscale_fast)(struct SwsContext *c,
                         int16_t *dst1, int16_t *dst2, int dstWidth,
                         const uint8_t *src1, const uint8_t *src2,
                         int srcW, int xInc);
    /** @} */

    /**
     * Scale one horizontal line of input data using a filter over the input
     * lines, to produce one (differently sized) line of output data.
     *
     * @param dst        pointer to destination buffer for horizontally scaled
     *                   data. If the number of bits per component of one
     *                   destination pixel (SwsContext->dstBpc) is <= 10, data
     *                   will be 15 bpc in 16 bits (int16_t) width. Else (i.e.
     *                   SwsContext->dstBpc == 16), data will be 19bpc in
     *                   32 bits (int32_t) width.
     * @param dstW       width of destination image
     * @param src        pointer to source data to be scaled. If the number of
     *                   bits per component of a source pixel (SwsContext->srcBpc)
     *                   is 8, this is 8bpc in 8 bits (uint8_t) width. Else
     *                   (i.e. SwsContext->dstBpc > 8), this is native depth
     *                   in 16 bits (uint16_t) width. In other words, for 9-bit
     *                   YUV input, this is 9bpc, for 10-bit YUV input, this is
     *                   10bpc, and for 16-bit RGB or YUV, this is 16bpc.
     * @param filter     filter coefficients to be used per output pixel for
     *                   scaling. This contains 14bpp filtering coefficients.
     *                   Guaranteed to contain dstW * filterSize entries.
     * @param filterPos  position of the first input pixel to be used for
     *                   each output pixel during scaling. Guaranteed to
     *                   contain dstW entries.
     * @param filterSize the number of input coefficients to be used (and
     *                   thus the number of input pixels to be used) for
     *                   creating a single output pixel. Is aligned to 4
     *                   (and input coefficients thus padded with zeroes)
     *                   to simplify creating SIMD code.
     */
    /** @{ */
    void (*hyScale)(struct SwsContext *c, int16_t *dst, int dstW,
                    const uint8_t *src, const int16_t *filter,
                    const int32_t *filterPos, int filterSize);
    void (*hcScale)(struct SwsContext *c, int16_t *dst, int dstW,
                    const uint8_t *src, const int16_t *filter,
                    const int32_t *filterPos, int filterSize);
    /** @} */

    /// Color range conversion function for luma plane if needed.
    void (*lumConvertRange)(int16_t *dst, int width);
    /// Color range conversion function for chroma planes if needed.
    void (*chrConvertRange)(int16_t *dst1, int16_t *dst2, int width);

    int needs_hcscale; ///< Set if there are chroma planes to be converted.

    SwsDither dither;

    SwsAlphaBlend alphablend;
} SwsContext;
//FIXME check init (where 0)

SwsFunc ff_yuv2rgb_get_func_ptr(SwsContext *c);
int ff_yuv2rgb_c_init_tables(SwsContext *c, const int inv_table[4],
                             int fullRange, int brightness,
                             int contrast, int saturation);
void ff_yuv2rgb_init_tables_ppc(SwsContext *c, const int inv_table[4],
                                int brightness, int contrast, int saturation);

void ff_updateMMXDitherTables(SwsContext *c, int dstY, int lumBufIndex, int chrBufIndex,
                           int lastInLumBuf, int lastInChrBuf);

av_cold void ff_sws_init_range_convert(SwsContext *c);

SwsFunc ff_yuv2rgb_init_x86(SwsContext *c);
SwsFunc ff_yuv2rgb_init_ppc(SwsContext *c);

static av_always_inline int is16BPS(enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    av_assert0(desc);
    return desc->comp[0].depth == 16;
}

static av_always_inline int is9_OR_10BPS(enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    av_assert0(desc);
    return desc->comp[0].depth >= 9 && desc->comp[0].depth <= 14;
}

#define isNBPS(x) is9_OR_10BPS(x)

static av_always_inline int isBE(enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    av_assert0(desc);
    return desc->flags & AV_PIX_FMT_FLAG_BE;
}

static av_always_inline int isYUV(enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    av_assert0(desc);
    return !(desc->flags & AV_PIX_FMT_FLAG_RGB) && desc->nb_components >= 2;
}

static av_always_inline int isPlanarYUV(enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    av_assert0(desc);
    return ((desc->flags & AV_PIX_FMT_FLAG_PLANAR) && isYUV(pix_fmt));
}

static av_always_inline int isRGB(enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    av_assert0(desc);
    return (desc->flags & AV_PIX_FMT_FLAG_RGB);
}

#if 0 // FIXME
#define isGray(x) \
    (!(av_pix_fmt_desc_get(x)->flags & AV_PIX_FMT_FLAG_PAL) && \
     av_pix_fmt_desc_get(x)->nb_components <= 2)
#else
#define isGray(x)                      \
    ((x) == AV_PIX_FMT_GRAY8       ||  \
     (x) == AV_PIX_FMT_YA8         ||  \
     (x) == AV_PIX_FMT_GRAY16BE    ||  \
     (x) == AV_PIX_FMT_GRAY16LE    ||  \
     (x) == AV_PIX_FMT_YA16BE      ||  \
     (x) == AV_PIX_FMT_YA16LE)
#endif

#define isRGBinInt(x) \
    (           \
     (x) == AV_PIX_FMT_RGB48BE     ||  \
     (x) == AV_PIX_FMT_RGB48LE     ||  \
     (x) == AV_PIX_FMT_RGB32       ||  \
     (x) == AV_PIX_FMT_RGB32_1     ||  \
     (x) == AV_PIX_FMT_RGB24       ||  \
     (x) == AV_PIX_FMT_RGB565BE    ||  \
     (x) == AV_PIX_FMT_RGB565LE    ||  \
     (x) == AV_PIX_FMT_RGB555BE    ||  \
     (x) == AV_PIX_FMT_RGB555LE    ||  \
     (x) == AV_PIX_FMT_RGB444BE    ||  \
     (x) == AV_PIX_FMT_RGB444LE    ||  \
     (x) == AV_PIX_FMT_RGB8        ||  \
     (x) == AV_PIX_FMT_RGB4        ||  \
     (x) == AV_PIX_FMT_RGB4_BYTE   ||  \
     (x) == AV_PIX_FMT_RGBA64BE    ||  \
     (x) == AV_PIX_FMT_RGBA64LE    ||  \
     (x) == AV_PIX_FMT_MONOBLACK   ||  \
     (x) == AV_PIX_FMT_MONOWHITE   \
    )
#define isBGRinInt(x) \
    (           \
     (x) == AV_PIX_FMT_BGR48BE     ||  \
     (x) == AV_PIX_FMT_BGR48LE     ||  \
     (x) == AV_PIX_FMT_BGR32       ||  \
     (x) == AV_PIX_FMT_BGR32_1     ||  \
     (x) == AV_PIX_FMT_BGR24       ||  \
     (x) == AV_PIX_FMT_BGR565BE    ||  \
     (x) == AV_PIX_FMT_BGR565LE    ||  \
     (x) == AV_PIX_FMT_BGR555BE    ||  \
     (x) == AV_PIX_FMT_BGR555LE    ||  \
     (x) == AV_PIX_FMT_BGR444BE    ||  \
     (x) == AV_PIX_FMT_BGR444LE    ||  \
     (x) == AV_PIX_FMT_BGR8        ||  \
     (x) == AV_PIX_FMT_BGR4        ||  \
     (x) == AV_PIX_FMT_BGR4_BYTE   ||  \
     (x) == AV_PIX_FMT_BGRA64BE    ||  \
     (x) == AV_PIX_FMT_BGRA64LE    ||  \
     (x) == AV_PIX_FMT_MONOBLACK   ||  \
     (x) == AV_PIX_FMT_MONOWHITE   \
    )

#define isRGBinBytes(x) (           \
           (x) == AV_PIX_FMT_RGB48BE     \
        || (x) == AV_PIX_FMT_RGB48LE     \
        || (x) == AV_PIX_FMT_RGBA64BE    \
        || (x) == AV_PIX_FMT_RGBA64LE    \
        || (x) == AV_PIX_FMT_RGBA        \
        || (x) == AV_PIX_FMT_ARGB        \
        || (x) == AV_PIX_FMT_RGB24       \
    )
#define isBGRinBytes(x) (           \
           (x) == AV_PIX_FMT_BGR48BE     \
        || (x) == AV_PIX_FMT_BGR48LE     \
        || (x) == AV_PIX_FMT_BGRA64BE    \
        || (x) == AV_PIX_FMT_BGRA64LE    \
        || (x) == AV_PIX_FMT_BGRA        \
        || (x) == AV_PIX_FMT_ABGR        \
        || (x) == AV_PIX_FMT_BGR24       \
    )

#define isBayer(x) ( \
           (x)==AV_PIX_FMT_BAYER_BGGR8    \
        || (x)==AV_PIX_FMT_BAYER_BGGR16LE \
        || (x)==AV_PIX_FMT_BAYER_BGGR16BE \
        || (x)==AV_PIX_FMT_BAYER_RGGB8    \
        || (x)==AV_PIX_FMT_BAYER_RGGB16LE \
        || (x)==AV_PIX_FMT_BAYER_RGGB16BE \
        || (x)==AV_PIX_FMT_BAYER_GBRG8    \
        || (x)==AV_PIX_FMT_BAYER_GBRG16LE \
        || (x)==AV_PIX_FMT_BAYER_GBRG16BE \
        || (x)==AV_PIX_FMT_BAYER_GRBG8    \
        || (x)==AV_PIX_FMT_BAYER_GRBG16LE \
        || (x)==AV_PIX_FMT_BAYER_GRBG16BE \
    )

#define isAnyRGB(x) \
    (           \
          isBayer(x)          ||    \
          isRGBinInt(x)       ||    \
          isBGRinInt(x)       ||    \
          isRGB(x)      \
    )

static av_always_inline int isALPHA(enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    av_assert0(desc);
    if (pix_fmt == AV_PIX_FMT_PAL8)
        return 1;
    return desc->flags & AV_PIX_FMT_FLAG_ALPHA;
}

#if 1
#define isPacked(x)         (       \
           (x)==AV_PIX_FMT_PAL8        \
        || (x)==AV_PIX_FMT_YUYV422     \
        || (x)==AV_PIX_FMT_YVYU422     \
        || (x)==AV_PIX_FMT_UYVY422     \
        || (x)==AV_PIX_FMT_YA8       \
        || (x)==AV_PIX_FMT_YA16LE      \
        || (x)==AV_PIX_FMT_YA16BE      \
        || (x)==AV_PIX_FMT_AYUV64LE    \
        || (x)==AV_PIX_FMT_AYUV64BE    \
        ||  isRGBinInt(x)           \
        ||  isBGRinInt(x)           \
    )
#else
static av_always_inline int isPacked(enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    av_assert0(desc);
    return ((desc->nb_components >= 2 && !(desc->flags & AV_PIX_FMT_FLAG_PLANAR)) ||
            pix_fmt == AV_PIX_FMT_PAL8);
}

#endif
static av_always_inline int isPlanar(enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    av_assert0(desc);
    return (desc->nb_components >= 2 && (desc->flags & AV_PIX_FMT_FLAG_PLANAR));
}

static av_always_inline int isPackedRGB(enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    av_assert0(desc);
    return ((desc->flags & (AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_RGB)) == AV_PIX_FMT_FLAG_RGB);
}

static av_always_inline int isPlanarRGB(enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    av_assert0(desc);
    return ((desc->flags & (AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_RGB)) ==
            (AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_RGB));
}

static av_always_inline int usePal(enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    av_assert0(desc);
    return (desc->flags & AV_PIX_FMT_FLAG_PAL) || (desc->flags & AV_PIX_FMT_FLAG_PSEUDOPAL);
}

extern const uint64_t ff_dither4[2];
extern const uint64_t ff_dither8[2];

extern const uint8_t ff_dither_2x2_4[3][8];
extern const uint8_t ff_dither_2x2_8[3][8];
extern const uint8_t ff_dither_4x4_16[5][8];
extern const uint8_t ff_dither_8x8_32[9][8];
extern const uint8_t ff_dither_8x8_73[9][8];
extern const uint8_t ff_dither_8x8_128[9][8];
extern const uint8_t ff_dither_8x8_220[9][8];

extern const int32_t ff_yuv2rgb_coeffs[11][4];

extern const AVClass ff_sws_context_class;

/**
 * Set c->swscale to an unscaled converter if one exists for the specific
 * source and destination formats, bit depths, flags, etc.
 */
void ff_get_unscaled_swscale(SwsContext *c);
void ff_get_unscaled_swscale_ppc(SwsContext *c);
void ff_get_unscaled_swscale_arm(SwsContext *c);
void ff_get_unscaled_swscale_aarch64(SwsContext *c);

/**
 * Return function pointer to fastest main scaler path function depending
 * on architecture and available optimizations.
 */
SwsFunc ff_getSwsFunc(SwsContext *c);

void ff_sws_init_input_funcs(SwsContext *c);
void ff_sws_init_output_funcs(SwsContext *c,
                              yuv2planar1_fn *yuv2plane1,
                              yuv2planarX_fn *yuv2planeX,
                              yuv2interleavedX_fn *yuv2nv12cX,
                              yuv2packed1_fn *yuv2packed1,
                              yuv2packed2_fn *yuv2packed2,
                              yuv2packedX_fn *yuv2packedX,
                              yuv2anyX_fn *yuv2anyX);
void ff_sws_init_swscale_ppc(SwsContext *c);
void ff_sws_init_swscale_x86(SwsContext *c);
void ff_sws_init_swscale_aarch64(SwsContext *c);
void ff_sws_init_swscale_arm(SwsContext *c);

void ff_hyscale_fast_c(SwsContext *c, int16_t *dst, int dstWidth,
                       const uint8_t *src, int srcW, int xInc);
void ff_hcscale_fast_c(SwsContext *c, int16_t *dst1, int16_t *dst2,
                       int dstWidth, const uint8_t *src1,
                       const uint8_t *src2, int srcW, int xInc);
int ff_init_hscaler_mmxext(int dstW, int xInc, uint8_t *filterCode,
                           int16_t *filter, int32_t *filterPos,
                           int numSplits);
void ff_hyscale_fast_mmxext(SwsContext *c, int16_t *dst,
                            int dstWidth, const uint8_t *src,
                            int srcW, int xInc);
void ff_hcscale_fast_mmxext(SwsContext *c, int16_t *dst1, int16_t *dst2,
                            int dstWidth, const uint8_t *src1,
                            const uint8_t *src2, int srcW, int xInc);

/**
 * Allocate and return an SwsContext.
 * This is like sws_getContext() but does not perform the init step, allowing
 * the user to set additional AVOptions.
 *
 * @see sws_getContext()
 */
struct SwsContext *sws_alloc_set_opts(int srcW, int srcH, enum AVPixelFormat srcFormat,
                                      int dstW, int dstH, enum AVPixelFormat dstFormat,
                                      int flags, const double *param);

int ff_sws_alphablendaway(SwsContext *c, const uint8_t *src[],
                          int srcStride[], int srcSliceY, int srcSliceH,
                          uint8_t *dst[], int dstStride[]);

static inline void fillPlane16(uint8_t *plane, int stride, int width, int height, int y,
                               int alpha, int bits, const int big_endian)
{
    int i, j;
    uint8_t *ptr = plane + stride * y;
    int v = alpha ? 0xFFFF>>(16-bits) : (1<<(bits-1));
    for (i = 0; i < height; i++) {
#define FILL(wfunc) \
        for (j = 0; j < width; j++) {\
            wfunc(ptr+2*j, v);\
        }
        if (big_endian) {
            FILL(AV_WB16);
        } else {
            FILL(AV_WL16);
        }
        ptr += stride;
    }
}

#define MAX_SLICE_PLANES 4

/// Slice plane
typedef struct SwsPlane
{
    int available_lines;    ///< max number of lines that can be hold by this plane
    int sliceY;             ///< index of first line
    int sliceH;             ///< number of lines
    uint8_t **line;         ///< line buffer
    uint8_t **tmp;          ///< Tmp line buffer used by mmx code
} SwsPlane;

/**
 * Struct which defines a slice of an image to be scaled or an output for
 * a scaled slice.
 * A slice can also be used as intermediate ring buffer for scaling steps.
 */
typedef struct SwsSlice
{
    int width;              ///< Slice line width
    int h_chr_sub_sample;   ///< horizontal chroma subsampling factor
    int v_chr_sub_sample;   ///< vertical chroma subsampling factor
    int is_ring;            ///< flag to identify if this slice is a ring buffer
    int should_free_lines;  ///< flag to identify if there are dynamic allocated lines
    enum AVPixelFormat fmt; ///< planes pixel format
    SwsPlane plane[MAX_SLICE_PLANES];   ///< color planes
} SwsSlice;

/**
 * Struct which holds all necessary data for processing a slice.
 * A processing step can be a color conversion or horizontal/vertical scaling.
 */
typedef struct SwsFilterDescriptor
{
    SwsSlice *src;  ///< Source slice
    SwsSlice *dst;  ///< Output slice

    int alpha;      ///< Flag for processing alpha channel
    void *instance; ///< Filter instance data

    /// Function for processing input slice sliceH lines starting from line sliceY
    int (*process)(SwsContext *c, struct SwsFilterDescriptor *desc, int sliceY, int sliceH);
} SwsFilterDescriptor;

// warp input lines in the form (src + width*i + j) to slice format (line[i][j])
// relative=true means first line src[x][0] otherwise first line is src[x][lum/crh Y]
int ff_init_slice_from_src(SwsSlice * s, uint8_t *src[4], int stride[4], int srcW, int lumY, int lumH, int chrY, int chrH, int relative);

// Initialize scaler filter descriptor chain
int ff_init_filters(SwsContext *c);

// Free all filter data
int ff_free_filters(SwsContext *c);

/*
 function for applying ring buffer logic into slice s
 It checks if the slice can hold more @lum lines, if yes
 do nothing otherwise remove @lum least used lines.
 It applies the same procedure for @chr lines.
*/
int ff_rotate_slice(SwsSlice *s, int lum, int chr);

/// initializes gamma conversion descriptor
int ff_init_gamma_convert(SwsFilterDescriptor *desc, SwsSlice * src, uint16_t *table);

/// initializes lum pixel format conversion descriptor
int ff_init_desc_fmt_convert(SwsFilterDescriptor *desc, SwsSlice * src, SwsSlice *dst, uint32_t *pal);

/// initializes lum horizontal scaling descriptor
int ff_init_desc_hscale(SwsFilterDescriptor *desc, SwsSlice *src, SwsSlice *dst, uint16_t *filter, int * filter_pos, int filter_size, int xInc);

/// initializes chr pixel format conversion descriptor
int ff_init_desc_cfmt_convert(SwsFilterDescriptor *desc, SwsSlice * src, SwsSlice *dst, uint32_t *pal);

/// initializes chr horizontal scaling descriptor
int ff_init_desc_chscale(SwsFilterDescriptor *desc, SwsSlice *src, SwsSlice *dst, uint16_t *filter, int * filter_pos, int filter_size, int xInc);

int ff_init_desc_no_chr(SwsFilterDescriptor *desc, SwsSlice * src, SwsSlice *dst);

/// initializes vertical scaling descriptors
int ff_init_vscale(SwsContext *c, SwsFilterDescriptor *desc, SwsSlice *src, SwsSlice *dst);

/// setup vertical scaler functions
void ff_init_vscale_pfn(SwsContext *c, yuv2planar1_fn yuv2plane1, yuv2planarX_fn yuv2planeX,
    yuv2interleavedX_fn yuv2nv12cX, yuv2packed1_fn yuv2packed1, yuv2packed2_fn yuv2packed2,
    yuv2packedX_fn yuv2packedX, yuv2anyX_fn yuv2anyX, int use_mmx);

//number of extra lines to process
#define MAX_LINES_AHEAD 4

#endif /* SWSCALE_SWSCALE_INTERNAL_H */
