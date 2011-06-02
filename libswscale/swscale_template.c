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

static inline void yuv2yuvX_c(SwsContext *c, const int16_t *lumFilter,
                              const int16_t **lumSrc, int lumFilterSize,
                              const int16_t *chrFilter, const int16_t **chrUSrc,
                              const int16_t **chrVSrc,
                              int chrFilterSize, const int16_t **alpSrc,
                              uint8_t *dest, uint8_t *uDest, uint8_t *vDest,
                              uint8_t *aDest, int dstW, int chrDstW, const uint8_t *lumDither, const uint8_t *chrDither)
{
    yuv2yuvXinC(lumFilter, lumSrc, lumFilterSize,
                chrFilter, chrUSrc, chrVSrc, chrFilterSize,
                alpSrc, dest, uDest, vDest, aDest, dstW, chrDstW, lumDither, chrDither);
}

static inline void yuv2nv12X_c(SwsContext *c, const int16_t *lumFilter,
                               const int16_t **lumSrc, int lumFilterSize,
                               const int16_t *chrFilter, const int16_t **chrUSrc,
                               const int16_t **chrVSrc,
                               int chrFilterSize, uint8_t *dest, uint8_t *uDest,
                               int dstW, int chrDstW, enum PixelFormat dstFormat, const uint8_t *dither, const uint8_t *chrDither)
{
    yuv2nv12XinC(lumFilter, lumSrc, lumFilterSize,
                 chrFilter, chrUSrc, chrVSrc, chrFilterSize,
                 dest, uDest, dstW, chrDstW, dstFormat, dither, chrDither);
}

static inline void yuv2yuv1_c(SwsContext *c, const int16_t *lumSrc,
                              const int16_t *chrUSrc, const int16_t *chrVSrc,
                              const int16_t *alpSrc,
                              uint8_t *dest, uint8_t *uDest, uint8_t *vDest,
                              uint8_t *aDest, int dstW, int chrDstW, const uint8_t *lumDither, const uint8_t *chrDither)
{
    int i;

    for (i=0; i<dstW; i++) {
        int val= (lumSrc[i]+lumDither[i&7])>>7;
        dest[i]= av_clip_uint8(val);
    }

    if (uDest)
        for (i=0; i<chrDstW; i++) {
            int u=(chrUSrc[i]+chrDither[i&7])>>7;
            int v=(chrVSrc[i]+chrDither[(i+3)&7])>>7;
            uDest[i]= av_clip_uint8(u);
            vDest[i]= av_clip_uint8(v);
        }

    if (CONFIG_SWSCALE_ALPHA && aDest)
        for (i=0; i<dstW; i++) {
            int val= (alpSrc[i]+lumDither[i&7])>>7;
            aDest[i]= av_clip_uint8(val);
        }
}


/**
 * vertical scale YV12 to RGB
 */
static inline void yuv2packedX_c(SwsContext *c, const int16_t *lumFilter,
                                 const int16_t **lumSrc, int lumFilterSize,
                                 const int16_t *chrFilter, const int16_t **chrUSrc,
                                 const int16_t **chrVSrc,
                                 int chrFilterSize, const int16_t **alpSrc,
                                 uint8_t *dest, int dstW, int dstY)
{
        yuv2packedXinC(c, lumFilter, lumSrc, lumFilterSize,
                       chrFilter, chrUSrc, chrVSrc, chrFilterSize,
                       alpSrc, dest, dstW, dstY);
}

/**
 * vertical bilinear scale YV12 to RGB
 */
static inline void yuv2packed2_c(SwsContext *c, const uint16_t *buf0,
                                 const uint16_t *buf1, const uint16_t *ubuf0,
                                 const uint16_t *ubuf1, const uint16_t *vbuf0,
                                 const uint16_t *vbuf1, const uint16_t *abuf0,
                                 const uint16_t *abuf1, uint8_t *dest, int dstW,
                                 int yalpha, int uvalpha, int y)
{
    int  yalpha1=4095- yalpha;
    int uvalpha1=4095-uvalpha;
    int i;

    YSCALE_YUV_2_ANYRGB_C(YSCALE_YUV_2_RGB2_C, YSCALE_YUV_2_PACKED2_C(void,0), YSCALE_YUV_2_GRAY16_2_C, YSCALE_YUV_2_MONO2_C)
}

/**
 * YV12 to RGB without scaling or interpolating
 */
static inline void yuv2packed1_c(SwsContext *c, const uint16_t *buf0,
                                 const uint16_t *ubuf0, const uint16_t *ubuf1,
                                 const uint16_t *vbuf0, const uint16_t *vbuf1,
                                 const uint16_t *abuf0, uint8_t *dest, int dstW,
                                 int uvalpha, enum PixelFormat dstFormat,
                                 int flags, int y)
{
    const int yalpha1=0;
    int i;

    const uint16_t *buf1= buf0; //FIXME needed for RGB1/BGR1
    const int yalpha= 4096; //FIXME ...

    if (uvalpha < 2048) {
        YSCALE_YUV_2_ANYRGB_C(YSCALE_YUV_2_RGB1_C, YSCALE_YUV_2_PACKED1_C(void,0), YSCALE_YUV_2_GRAY16_1_C, YSCALE_YUV_2_MONO2_C)
    } else {
        YSCALE_YUV_2_ANYRGB_C(YSCALE_YUV_2_RGB1B_C, YSCALE_YUV_2_PACKED1B_C(void,0), YSCALE_YUV_2_GRAY16_1_C, YSCALE_YUV_2_MONO2_C)
    }
}

//FIXME yuy2* can read up to 7 samples too much

static inline void yuy2ToY_c(uint8_t *dst, const uint8_t *src, int width,
                             uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++)
        dst[i]= src[2*i];
}

static inline void yuy2ToUV_c(uint8_t *dstU, uint8_t *dstV, const uint8_t *src1,
                              const uint8_t *src2, int width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        dstU[i]= src1[4*i + 1];
        dstV[i]= src1[4*i + 3];
    }
    assert(src1 == src2);
}

static inline void LEToUV_c(uint8_t *dstU, uint8_t *dstV, const uint8_t *src1,
                            const uint8_t *src2, int width, uint32_t *unused)
{
    int i;
    // FIXME I don't think this code is right for YUV444/422, since then h is not subsampled so
    // we need to skip each second pixel. Same for BEToUV.
    for (i=0; i<width; i++) {
        dstU[i]= src1[2*i + 1];
        dstV[i]= src2[2*i + 1];
    }
}

/* This is almost identical to the previous, end exists only because
 * yuy2ToY/UV)(dst, src+1, ...) would have 100% unaligned accesses. */
static inline void uyvyToY_c(uint8_t *dst, const uint8_t *src, int width,
                             uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++)
        dst[i]= src[2*i+1];
}

static inline void uyvyToUV_c(uint8_t *dstU, uint8_t *dstV, const uint8_t *src1,
                              const uint8_t *src2, int width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        dstU[i]= src1[4*i + 0];
        dstV[i]= src1[4*i + 2];
    }
    assert(src1 == src2);
}

static inline void BEToUV_c(uint8_t *dstU, uint8_t *dstV, const uint8_t *src1,
                            const uint8_t *src2, int width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        dstU[i]= src1[2*i];
        dstV[i]= src2[2*i];
    }
}

static inline void nvXXtoUV_c(uint8_t *dst1, uint8_t *dst2,
                              const uint8_t *src, int width)
{
    int i;
    for (i = 0; i < width; i++) {
        dst1[i] = src[2*i+0];
        dst2[i] = src[2*i+1];
    }
}

// FIXME Maybe dither instead.
#ifndef YUV_NBPS
#define YUV_NBPS(depth, endianness, rfunc) \
static inline void endianness ## depth ## ToUV_c(uint8_t *dstU, uint8_t *dstV, \
                                          const uint16_t *srcU, const uint16_t *srcV, \
                                          int width, uint32_t *unused) \
{ \
    int i; \
    for (i = 0; i < width; i++) { \
        dstU[i] = rfunc(&srcU[i])>>(depth-8); \
        dstV[i] = rfunc(&srcV[i])>>(depth-8); \
    } \
} \
\
static inline void endianness ## depth ## ToY_c(uint8_t *dstY, const uint16_t *srcY, int width, uint32_t *unused) \
{ \
    int i; \
    for (i = 0; i < width; i++) \
        dstY[i] = rfunc(&srcY[i])>>(depth-8); \
} \

YUV_NBPS( 9, LE, AV_RL16)
YUV_NBPS( 9, BE, AV_RB16)
YUV_NBPS(10, LE, AV_RL16)
YUV_NBPS(10, BE, AV_RB16)
#endif // YUV_NBPS

static inline void nv12ToUV_c(uint8_t *dstU, uint8_t *dstV,
                              const uint8_t *src1, const uint8_t *src2,
                              int width, uint32_t *unused)
{
    nvXXtoUV_c(dstU, dstV, src1, width);
}

static inline void nv21ToUV_c(uint8_t *dstU, uint8_t *dstV,
                              const uint8_t *src1, const uint8_t *src2,
                              int width, uint32_t *unused)
{
    nvXXtoUV_c(dstV, dstU, src1, width);
}

static inline void bgr24ToY_c(int16_t *dst, const uint8_t *src,
                              int width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        int b= src[i*3+0];
        int g= src[i*3+1];
        int r= src[i*3+2];

        dst[i]= ((RY*r + GY*g + BY*b + (32<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6));
    }
}

static inline void bgr24ToUV_c(int16_t *dstU, int16_t *dstV, const uint8_t *src1,
                               const uint8_t *src2, int width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        int b= src1[3*i + 0];
        int g= src1[3*i + 1];
        int r= src1[3*i + 2];

        dstU[i]= (RU*r + GU*g + BU*b + (256<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6);
        dstV[i]= (RV*r + GV*g + BV*b + (256<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6);
    }
    assert(src1 == src2);
}

static inline void bgr24ToUV_half_c(int16_t *dstU, int16_t *dstV, const uint8_t *src1,
                                    const uint8_t *src2, int width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        int b= src1[6*i + 0] + src1[6*i + 3];
        int g= src1[6*i + 1] + src1[6*i + 4];
        int r= src1[6*i + 2] + src1[6*i + 5];

        dstU[i]= (RU*r + GU*g + BU*b + (256<<RGB2YUV_SHIFT) + (1<<(RGB2YUV_SHIFT-6)))>>(RGB2YUV_SHIFT-5);
        dstV[i]= (RV*r + GV*g + BV*b + (256<<RGB2YUV_SHIFT) + (1<<(RGB2YUV_SHIFT-6)))>>(RGB2YUV_SHIFT-5);
    }
    assert(src1 == src2);
}

static inline void rgb24ToY_c(int16_t *dst, const uint8_t *src, int width,
                              uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++) {
        int r= src[i*3+0];
        int g= src[i*3+1];
        int b= src[i*3+2];

        dst[i]= ((RY*r + GY*g + BY*b + (32<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6));
    }
}

static inline void rgb24ToUV_c(int16_t *dstU, int16_t *dstV, const uint8_t *src1,
                               const uint8_t *src2, int width, uint32_t *unused)
{
    int i;
    assert(src1==src2);
    for (i=0; i<width; i++) {
        int r= src1[3*i + 0];
        int g= src1[3*i + 1];
        int b= src1[3*i + 2];

        dstU[i]= (RU*r + GU*g + BU*b + (256<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6);
        dstV[i]= (RV*r + GV*g + BV*b + (256<<(RGB2YUV_SHIFT-1)) + (1<<(RGB2YUV_SHIFT-7)))>>(RGB2YUV_SHIFT-6);
    }
}

static inline void rgb24ToUV_half_c(int16_t *dstU, int16_t *dstV, const uint8_t *src1,
                                    const uint8_t *src2, int width, uint32_t *unused)
{
    int i;
    assert(src1==src2);
    for (i=0; i<width; i++) {
        int r= src1[6*i + 0] + src1[6*i + 3];
        int g= src1[6*i + 1] + src1[6*i + 4];
        int b= src1[6*i + 2] + src1[6*i + 5];

        dstU[i]= (RU*r + GU*g + BU*b + (256<<RGB2YUV_SHIFT) + (1<<(RGB2YUV_SHIFT-6)))>>(RGB2YUV_SHIFT-5);
        dstV[i]= (RV*r + GV*g + BV*b + (256<<RGB2YUV_SHIFT) + (1<<(RGB2YUV_SHIFT-6)))>>(RGB2YUV_SHIFT-5);
    }
}


// bilinear / bicubic scaling
static inline void hScale_c(int16_t *dst, int dstW, const uint8_t *src,
                            int srcW, int xInc,
                            const int16_t *filter, const int16_t *filterPos,
                            int filterSize)
{
    int i;
    for (i=0; i<dstW; i++) {
        int j;
        int srcPos= filterPos[i];
        int val=0;
        for (j=0; j<filterSize; j++) {
            val += ((int)src[srcPos + j])*filter[filterSize*i + j];
        }
        //filter += hFilterSize;
        dst[i] = FFMIN(val>>7, (1<<15)-1); // the cubic equation does overflow ...
        //dst[i] = val>>7;
    }
}

static inline void hScale16_c(int16_t *dst, int dstW, const uint16_t *src, int srcW, int xInc,
                                    const int16_t *filter, const int16_t *filterPos, long filterSize, int shift)
{
    int i, j;

    for (i=0; i<dstW; i++) {
        int srcPos= filterPos[i];
        int val=0;
        for (j=0; j<filterSize; j++) {
            val += ((int)src[srcPos + j])*filter[filterSize*i + j];
        }
        dst[i] = FFMIN(val>>shift, (1<<15)-1); // the cubic equation does overflow ...
    }
}

static inline void hScale16X_c(int16_t *dst, int dstW, const uint16_t *src, int srcW, int xInc,
                                    const int16_t *filter, const int16_t *filterPos, long filterSize, int shift)
{
    int i, j;
    for (i=0; i<dstW; i++) {
        int srcPos= filterPos[i];
        int val=0;
        for (j=0; j<filterSize; j++) {
            val += ((int)av_bswap16(src[srcPos + j]))*filter[filterSize*i + j];
        }
        dst[i] = FFMIN(val>>shift, (1<<15)-1); // the cubic equation does overflow ...
    }
}

//FIXME all pal and rgb srcFormats could do this convertion as well
//FIXME all scalers more complex than bilinear could do half of this transform
static void chrRangeToJpeg_c(int16_t *dstU, int16_t *dstV, int width)
{
    int i;
    for (i = 0; i < width; i++) {
        dstU[i] = (FFMIN(dstU[i],30775)*4663 - 9289992)>>12; //-264
        dstV[i] = (FFMIN(dstV[i],30775)*4663 - 9289992)>>12; //-264
    }
}
static void chrRangeFromJpeg_c(int16_t *dstU, int16_t *dstV, int width)
{
    int i;
    for (i = 0; i < width; i++) {
        dstU[i] = (dstU[i]*1799 + 4081085)>>11; //1469
        dstV[i] = (dstV[i]*1799 + 4081085)>>11; //1469
    }
}
static void lumRangeToJpeg_c(int16_t *dst, int width)
{
    int i;
    for (i = 0; i < width; i++)
        dst[i] = (FFMIN(dst[i],30189)*19077 - 39057361)>>14;
}
static void lumRangeFromJpeg_c(int16_t *dst, int width)
{
    int i;
    for (i = 0; i < width; i++)
        dst[i] = (dst[i]*14071 + 33561947)>>14;
}

static inline void hyscale_fast_c(SwsContext *c, int16_t *dst, int dstWidth,
                                  const uint8_t *src, int srcW, int xInc)
{
    int i;
    unsigned int xpos=0;
    for (i=0;i<dstWidth;i++) {
        register unsigned int xx=xpos>>16;
        register unsigned int xalpha=(xpos&0xFFFF)>>9;
        dst[i]= (src[xx]<<7) + (src[xx+1] - src[xx])*xalpha;
        xpos+=xInc;
    }
    for (i=dstWidth-1; (i*xInc)>>16 >=srcW-1; i--)
        dst[i] = src[srcW-1]*128;
}

      // *** horizontal scale Y line to temp buffer
static inline void hyscale_c(SwsContext *c, uint16_t *dst, int dstWidth,
                             const uint8_t *src, int srcW, int xInc,
                             const int16_t *hLumFilter,
                             const int16_t *hLumFilterPos, int hLumFilterSize,
                             uint8_t *formatConvBuffer,
                             uint32_t *pal, int isAlpha)
{
    void (*toYV12)(uint8_t *, const uint8_t *, int, uint32_t *) = isAlpha ? c->alpToYV12 : c->lumToYV12;
    void (*convertRange)(int16_t *, int) = isAlpha ? NULL : c->lumConvertRange;

    src += isAlpha ? c->alpSrcOffset : c->lumSrcOffset;

    if (toYV12) {
        toYV12(formatConvBuffer, src, srcW, pal);
        src= formatConvBuffer;
    }

    if (c->hScale16) {
        int shift= isAnyRGB(c->srcFormat) || c->srcFormat==PIX_FMT_PAL8 ? 13 : av_pix_fmt_descriptors[c->srcFormat].comp[0].depth_minus1;
        c->hScale16(dst, dstWidth, (const uint16_t*)src, srcW, xInc, hLumFilter, hLumFilterPos, hLumFilterSize, shift);
    } else if (!c->hyscale_fast) {
        c->hScale(dst, dstWidth, src, srcW, xInc, hLumFilter, hLumFilterPos, hLumFilterSize);
    } else { // fast bilinear upscale / crap downscale
        c->hyscale_fast(c, dst, dstWidth, src, srcW, xInc);
    }

    if (convertRange)
        convertRange(dst, dstWidth);
}

static inline void hcscale_fast_c(SwsContext *c, int16_t *dst1, int16_t *dst2,
                                  int dstWidth, const uint8_t *src1,
                                  const uint8_t *src2, int srcW, int xInc)
{
    int i;
    unsigned int xpos=0;
    for (i=0;i<dstWidth;i++) {
        register unsigned int xx=xpos>>16;
        register unsigned int xalpha=(xpos&0xFFFF)>>9;
        dst1[i]=(src1[xx]*(xalpha^127)+src1[xx+1]*xalpha);
        dst2[i]=(src2[xx]*(xalpha^127)+src2[xx+1]*xalpha);
        xpos+=xInc;
    }
    for (i=dstWidth-1; (i*xInc)>>16 >=srcW-1; i--) {
        dst1[i] = src1[srcW-1]*128;
        dst2[i] = src2[srcW-1]*128;
    }
}

inline static void hcscale_c(SwsContext *c, uint16_t *dst1, uint16_t *dst2, int dstWidth,
                             const uint8_t *src1, const uint8_t *src2,
                             int srcW, int xInc, const int16_t *hChrFilter,
                             const int16_t *hChrFilterPos, int hChrFilterSize,
                             uint8_t *formatConvBuffer, uint32_t *pal)
{

    src1 += c->chrSrcOffset;
    src2 += c->chrSrcOffset;

    if (c->chrToYV12) {
        uint8_t *buf2 = formatConvBuffer + FFALIGN(srcW*2+78, 16);
        c->chrToYV12(formatConvBuffer, buf2, src1, src2, srcW, pal);
        src1= formatConvBuffer;
        src2= buf2;
    }

    if (c->hScale16) {
        int shift= isAnyRGB(c->srcFormat) || c->srcFormat==PIX_FMT_PAL8 ? 13 : av_pix_fmt_descriptors[c->srcFormat].comp[0].depth_minus1;
        c->hScale16(dst1, dstWidth, (const uint16_t*)src1, srcW, xInc, hChrFilter, hChrFilterPos, hChrFilterSize, shift);
        c->hScale16(dst2, dstWidth, (const uint16_t*)src2, srcW, xInc, hChrFilter, hChrFilterPos, hChrFilterSize, shift);
    } else if (!c->hcscale_fast) {
        c->hScale(dst1, dstWidth, src1, srcW, xInc, hChrFilter, hChrFilterPos, hChrFilterSize);
        c->hScale(dst2, dstWidth, src2, srcW, xInc, hChrFilter, hChrFilterPos, hChrFilterSize);
    } else { // fast bilinear upscale / crap downscale
        c->hcscale_fast(c, dst1, dst2, dstWidth, src1, src2, srcW, xInc);
    }

    if (c->chrConvertRange)
        c->chrConvertRange(dst1, dst2, dstWidth);
}

#define DEBUG_SWSCALE_BUFFERS 0
#define DEBUG_BUFFERS(...) if (DEBUG_SWSCALE_BUFFERS) av_log(c, AV_LOG_DEBUG, __VA_ARGS__)

#if HAVE_MMX
static void updateMMXDitherTables(SwsContext *c, int dstY, int lumBufIndex, int chrBufIndex,
                                  int lastInLumBuf, int lastInChrBuf);
#endif

static int swScale_c(SwsContext *c, const uint8_t* src[], int srcStride[],
                     int srcSliceY, int srcSliceH, uint8_t* dst[], int dstStride[])
{
    /* load a few things into local vars to make the code more readable? and faster */
    const int srcW= c->srcW;
    const int dstW= c->dstW;
    const int dstH= c->dstH;
    const int chrDstW= c->chrDstW;
    const int chrSrcW= c->chrSrcW;
    const int lumXInc= c->lumXInc;
    const int chrXInc= c->chrXInc;
    const enum PixelFormat dstFormat= c->dstFormat;
    const int flags= c->flags;
    int16_t *vLumFilterPos= c->vLumFilterPos;
    int16_t *vChrFilterPos= c->vChrFilterPos;
    int16_t *hLumFilterPos= c->hLumFilterPos;
    int16_t *hChrFilterPos= c->hChrFilterPos;
    int16_t *vLumFilter= c->vLumFilter;
    int16_t *vChrFilter= c->vChrFilter;
    int16_t *hLumFilter= c->hLumFilter;
    int16_t *hChrFilter= c->hChrFilter;
    int32_t *lumMmxFilter= c->lumMmxFilter;
    int32_t *chrMmxFilter= c->chrMmxFilter;
    int32_t av_unused *alpMmxFilter= c->alpMmxFilter;
    const int vLumFilterSize= c->vLumFilterSize;
    const int vChrFilterSize= c->vChrFilterSize;
    const int hLumFilterSize= c->hLumFilterSize;
    const int hChrFilterSize= c->hChrFilterSize;
    int16_t **lumPixBuf= c->lumPixBuf;
    int16_t **chrUPixBuf= c->chrUPixBuf;
    int16_t **chrVPixBuf= c->chrVPixBuf;
    int16_t **alpPixBuf= c->alpPixBuf;
    const int vLumBufSize= c->vLumBufSize;
    const int vChrBufSize= c->vChrBufSize;
    uint8_t *formatConvBuffer= c->formatConvBuffer;
    const int chrSrcSliceY= srcSliceY >> c->chrSrcVSubSample;
    const int chrSrcSliceH= -((-srcSliceH) >> c->chrSrcVSubSample);
    int lastDstY;
    uint32_t *pal=c->pal_yuv;

    /* vars which will change and which we need to store back in the context */
    int dstY= c->dstY;
    int lumBufIndex= c->lumBufIndex;
    int chrBufIndex= c->chrBufIndex;
    int lastInLumBuf= c->lastInLumBuf;
    int lastInChrBuf= c->lastInChrBuf;

    if (isPacked(c->srcFormat)) {
        src[0]=
        src[1]=
        src[2]=
        src[3]= src[0];
        srcStride[0]=
        srcStride[1]=
        srcStride[2]=
        srcStride[3]= srcStride[0];
    }
    srcStride[1]<<= c->vChrDrop;
    srcStride[2]<<= c->vChrDrop;

    DEBUG_BUFFERS("swScale() %p[%d] %p[%d] %p[%d] %p[%d] -> %p[%d] %p[%d] %p[%d] %p[%d]\n",
                  src[0], srcStride[0], src[1], srcStride[1], src[2], srcStride[2], src[3], srcStride[3],
                  dst[0], dstStride[0], dst[1], dstStride[1], dst[2], dstStride[2], dst[3], dstStride[3]);
    DEBUG_BUFFERS("srcSliceY: %d srcSliceH: %d dstY: %d dstH: %d\n",
                   srcSliceY,    srcSliceH,    dstY,    dstH);
    DEBUG_BUFFERS("vLumFilterSize: %d vLumBufSize: %d vChrFilterSize: %d vChrBufSize: %d\n",
                   vLumFilterSize,    vLumBufSize,    vChrFilterSize,    vChrBufSize);

    if (dstStride[0]%8 !=0 || dstStride[1]%8 !=0 || dstStride[2]%8 !=0 || dstStride[3]%8 != 0) {
        static int warnedAlready=0; //FIXME move this into the context perhaps
        if (flags & SWS_PRINT_INFO && !warnedAlready) {
            av_log(c, AV_LOG_WARNING, "Warning: dstStride is not aligned!\n"
                   "         ->cannot do aligned memory accesses anymore\n");
            warnedAlready=1;
        }
    }

    /* Note the user might start scaling the picture in the middle so this
       will not get executed. This is not really intended but works
       currently, so people might do it. */
    if (srcSliceY ==0) {
        lumBufIndex=-1;
        chrBufIndex=-1;
        dstY=0;
        lastInLumBuf= -1;
        lastInChrBuf= -1;
    }

    lastDstY= dstY;

    for (;dstY < dstH; dstY++) {
        unsigned char *dest =dst[0]+dstStride[0]*dstY;
        const int chrDstY= dstY>>c->chrDstVSubSample;
        unsigned char *uDest=dst[1]+dstStride[1]*chrDstY;
        unsigned char *vDest=dst[2]+dstStride[2]*chrDstY;
        unsigned char *aDest=(CONFIG_SWSCALE_ALPHA && alpPixBuf) ? dst[3]+dstStride[3]*dstY : NULL;
        const uint8_t *lumDither= isNBPS(c->srcFormat) || is16BPS(c->srcFormat) ? dithers[7][dstY   &7] : flat64;
        const uint8_t *chrDither= isNBPS(c->srcFormat) || is16BPS(c->srcFormat) ? dithers[7][chrDstY&7] : flat64;

        const int firstLumSrcY= vLumFilterPos[dstY]; //First line needed as input
        const int firstLumSrcY2= vLumFilterPos[FFMIN(dstY | ((1<<c->chrDstVSubSample) - 1), dstH-1)];
        const int firstChrSrcY= vChrFilterPos[chrDstY]; //First line needed as input
        int lastLumSrcY= firstLumSrcY + vLumFilterSize -1; // Last line needed as input
        int lastLumSrcY2=firstLumSrcY2+ vLumFilterSize -1; // Last line needed as input
        int lastChrSrcY= firstChrSrcY + vChrFilterSize -1; // Last line needed as input
        int enough_lines;

        //handle holes (FAST_BILINEAR & weird filters)
        if (firstLumSrcY > lastInLumBuf) lastInLumBuf= firstLumSrcY-1;
        if (firstChrSrcY > lastInChrBuf) lastInChrBuf= firstChrSrcY-1;
        assert(firstLumSrcY >= lastInLumBuf - vLumBufSize + 1);
        assert(firstChrSrcY >= lastInChrBuf - vChrBufSize + 1);

        DEBUG_BUFFERS("dstY: %d\n", dstY);
        DEBUG_BUFFERS("\tfirstLumSrcY: %d lastLumSrcY: %d lastInLumBuf: %d\n",
                         firstLumSrcY,    lastLumSrcY,    lastInLumBuf);
        DEBUG_BUFFERS("\tfirstChrSrcY: %d lastChrSrcY: %d lastInChrBuf: %d\n",
                         firstChrSrcY,    lastChrSrcY,    lastInChrBuf);

        // Do we have enough lines in this slice to output the dstY line
        enough_lines = lastLumSrcY2 < srcSliceY + srcSliceH && lastChrSrcY < -((-srcSliceY - srcSliceH)>>c->chrSrcVSubSample);

        if (!enough_lines) {
            lastLumSrcY = srcSliceY + srcSliceH - 1;
            lastChrSrcY = chrSrcSliceY + chrSrcSliceH - 1;
            DEBUG_BUFFERS("buffering slice: lastLumSrcY %d lastChrSrcY %d\n",
                                            lastLumSrcY, lastChrSrcY);
        }

        //Do horizontal scaling
        while(lastInLumBuf < lastLumSrcY) {
            const uint8_t *src1= src[0]+(lastInLumBuf + 1 - srcSliceY)*srcStride[0];
            const uint8_t *src2= src[3]+(lastInLumBuf + 1 - srcSliceY)*srcStride[3];
            lumBufIndex++;
            assert(lumBufIndex < 2*vLumBufSize);
            assert(lastInLumBuf + 1 - srcSliceY < srcSliceH);
            assert(lastInLumBuf + 1 - srcSliceY >= 0);
            hyscale_c(c, lumPixBuf[ lumBufIndex ], dstW, src1, srcW, lumXInc,
                      hLumFilter, hLumFilterPos, hLumFilterSize,
                      formatConvBuffer,
                      pal, 0);
            if (CONFIG_SWSCALE_ALPHA && alpPixBuf)
                hyscale_c(c, alpPixBuf[ lumBufIndex ], dstW, src2, srcW,
                          lumXInc, hLumFilter, hLumFilterPos, hLumFilterSize,
                          formatConvBuffer,
                          pal, 1);
            lastInLumBuf++;
            DEBUG_BUFFERS("\t\tlumBufIndex %d: lastInLumBuf: %d\n",
                               lumBufIndex,    lastInLumBuf);
        }
        while(lastInChrBuf < lastChrSrcY) {
            const uint8_t *src1= src[1]+(lastInChrBuf + 1 - chrSrcSliceY)*srcStride[1];
            const uint8_t *src2= src[2]+(lastInChrBuf + 1 - chrSrcSliceY)*srcStride[2];
            chrBufIndex++;
            assert(chrBufIndex < 2*vChrBufSize);
            assert(lastInChrBuf + 1 - chrSrcSliceY < (chrSrcSliceH));
            assert(lastInChrBuf + 1 - chrSrcSliceY >= 0);
            //FIXME replace parameters through context struct (some at least)

            if (c->needs_hcscale)
                hcscale_c(c, chrUPixBuf[chrBufIndex], chrVPixBuf[chrBufIndex],
                          chrDstW, src1, src2, chrSrcW, chrXInc,
                          hChrFilter, hChrFilterPos, hChrFilterSize,
                          formatConvBuffer, pal);
            lastInChrBuf++;
            DEBUG_BUFFERS("\t\tchrBufIndex %d: lastInChrBuf: %d\n",
                               chrBufIndex,    lastInChrBuf);
        }
        //wrap buf index around to stay inside the ring buffer
        if (lumBufIndex >= vLumBufSize) lumBufIndex-= vLumBufSize;
        if (chrBufIndex >= vChrBufSize) chrBufIndex-= vChrBufSize;
        if (!enough_lines)
            break; //we can't output a dstY line so let's try with the next slice

#if HAVE_MMX
        updateMMXDitherTables(c, dstY, lumBufIndex, chrBufIndex, lastInLumBuf, lastInChrBuf);
#endif
        if (dstY < dstH-2) {
            const int16_t **lumSrcPtr= (const int16_t **) lumPixBuf + lumBufIndex + firstLumSrcY - lastInLumBuf + vLumBufSize;
            const int16_t **chrUSrcPtr= (const int16_t **) chrUPixBuf + chrBufIndex + firstChrSrcY - lastInChrBuf + vChrBufSize;
            const int16_t **chrVSrcPtr= (const int16_t **) chrVPixBuf + chrBufIndex + firstChrSrcY - lastInChrBuf + vChrBufSize;
            const int16_t **alpSrcPtr= (CONFIG_SWSCALE_ALPHA && alpPixBuf) ? (const int16_t **) alpPixBuf + lumBufIndex + firstLumSrcY - lastInLumBuf + vLumBufSize : NULL;
            if (dstFormat == PIX_FMT_NV12 || dstFormat == PIX_FMT_NV21) {
                const int chrSkipMask= (1<<c->chrDstVSubSample)-1;
                if (dstY&chrSkipMask) uDest= NULL; //FIXME split functions in lumi / chromi
                c->yuv2nv12X(c,
                             vLumFilter+dstY*vLumFilterSize   , lumSrcPtr, vLumFilterSize,
                             vChrFilter+chrDstY*vChrFilterSize, chrUSrcPtr, chrVSrcPtr, vChrFilterSize,
                             dest, uDest, dstW, chrDstW, dstFormat, lumDither, chrDither);
            } else if (isPlanarYUV(dstFormat) || dstFormat==PIX_FMT_GRAY8) { //YV12 like
                const int chrSkipMask= (1<<c->chrDstVSubSample)-1;
                if ((dstY&chrSkipMask) || isGray(dstFormat)) uDest=vDest= NULL; //FIXME split functions in lumi / chromi
                if (is16BPS(dstFormat) || isNBPS(dstFormat)) {
                    yuv2yuvX16inC(vLumFilter+dstY*vLumFilterSize   , lumSrcPtr, vLumFilterSize,
                                  vChrFilter+chrDstY*vChrFilterSize, chrUSrcPtr,
                                  chrVSrcPtr, vChrFilterSize,
                                  alpSrcPtr, (uint16_t *) dest, (uint16_t *) uDest,
                                  (uint16_t *) vDest, (uint16_t *) aDest, dstW, chrDstW,
                                  dstFormat);
                } else if (vLumFilterSize == 1 && vChrFilterSize == 1) { // unscaled YV12
                    const int16_t *lumBuf = lumSrcPtr[0];
                    const int16_t *chrUBuf= chrUSrcPtr[0];
                    const int16_t *chrVBuf= chrVSrcPtr[0];
                    const int16_t *alpBuf= (CONFIG_SWSCALE_ALPHA && alpPixBuf) ? alpSrcPtr[0] : NULL;
                    c->yuv2yuv1(c, lumBuf, chrUBuf, chrVBuf, alpBuf, dest,
                                uDest, vDest, aDest, dstW, chrDstW, lumDither, chrDither);
                } else { //General YV12
                    c->yuv2yuvX(c,
                                vLumFilter+dstY*vLumFilterSize   , lumSrcPtr, vLumFilterSize,
                                vChrFilter+chrDstY*vChrFilterSize, chrUSrcPtr,
                                chrVSrcPtr, vChrFilterSize,
                                alpSrcPtr, dest, uDest, vDest, aDest, dstW, chrDstW, lumDither, chrDither);
                }
            } else {
                assert(lumSrcPtr  + vLumFilterSize - 1 < lumPixBuf  + vLumBufSize*2);
                assert(chrUSrcPtr + vChrFilterSize - 1 < chrUPixBuf + vChrBufSize*2);
                if (vLumFilterSize == 1 && vChrFilterSize == 2) { //unscaled RGB
                    int chrAlpha= vChrFilter[2*dstY+1];
                    if(flags & SWS_FULL_CHR_H_INT) {
                        yuv2rgbXinC_full(c, //FIXME write a packed1_full function
                                         vLumFilter+dstY*vLumFilterSize, lumSrcPtr, vLumFilterSize,
                                         vChrFilter+dstY*vChrFilterSize, chrUSrcPtr,
                                         chrVSrcPtr, vChrFilterSize,
                                         alpSrcPtr, dest, dstW, dstY);
                    } else {
                        c->yuv2packed1(c, *lumSrcPtr, *chrUSrcPtr, *(chrUSrcPtr+1),
                                       *chrVSrcPtr, *(chrVSrcPtr+1),
                                       alpPixBuf ? *alpSrcPtr : NULL,
                                       dest, dstW, chrAlpha, dstFormat, flags, dstY);
                    }
                } else if (vLumFilterSize == 2 && vChrFilterSize == 2) { //bilinear upscale RGB
                    int lumAlpha= vLumFilter[2*dstY+1];
                    int chrAlpha= vChrFilter[2*dstY+1];
                    lumMmxFilter[2]=
                    lumMmxFilter[3]= vLumFilter[2*dstY   ]*0x10001;
                    chrMmxFilter[2]=
                    chrMmxFilter[3]= vChrFilter[2*chrDstY]*0x10001;
                    if(flags & SWS_FULL_CHR_H_INT) {
                        yuv2rgbXinC_full(c, //FIXME write a packed2_full function
                                         vLumFilter+dstY*vLumFilterSize, lumSrcPtr, vLumFilterSize,
                                         vChrFilter+dstY*vChrFilterSize, chrUSrcPtr, chrVSrcPtr, vChrFilterSize,
                                         alpSrcPtr, dest, dstW, dstY);
                    } else {
                        c->yuv2packed2(c, *lumSrcPtr, *(lumSrcPtr+1), *chrUSrcPtr, *(chrUSrcPtr+1),
                                       *chrVSrcPtr, *(chrVSrcPtr+1),
                                       alpPixBuf ? *alpSrcPtr : NULL, alpPixBuf ? *(alpSrcPtr+1) : NULL,
                                       dest, dstW, lumAlpha, chrAlpha, dstY);
                    }
                } else { //general RGB
                    if(flags & SWS_FULL_CHR_H_INT) {
                        yuv2rgbXinC_full(c,
                                         vLumFilter+dstY*vLumFilterSize, lumSrcPtr, vLumFilterSize,
                                         vChrFilter+dstY*vChrFilterSize, chrUSrcPtr, chrVSrcPtr, vChrFilterSize,
                                         alpSrcPtr, dest, dstW, dstY);
                    } else {
                        c->yuv2packedX(c,
                                       vLumFilter+dstY*vLumFilterSize, lumSrcPtr, vLumFilterSize,
                                       vChrFilter+dstY*vChrFilterSize, chrUSrcPtr, chrVSrcPtr, vChrFilterSize,
                                       alpSrcPtr, dest, dstW, dstY);
                    }
                }
            }
        } else { // hmm looks like we can't use MMX here without overwriting this array's tail
            const int16_t **lumSrcPtr= (const int16_t **)lumPixBuf + lumBufIndex + firstLumSrcY - lastInLumBuf + vLumBufSize;
            const int16_t **chrUSrcPtr= (const int16_t **)chrUPixBuf + chrBufIndex + firstChrSrcY - lastInChrBuf + vChrBufSize;
            const int16_t **chrVSrcPtr= (const int16_t **)chrVPixBuf + chrBufIndex + firstChrSrcY - lastInChrBuf + vChrBufSize;
            const int16_t **alpSrcPtr= (CONFIG_SWSCALE_ALPHA && alpPixBuf) ? (const int16_t **)alpPixBuf + lumBufIndex + firstLumSrcY - lastInLumBuf + vLumBufSize : NULL;
            if (dstFormat == PIX_FMT_NV12 || dstFormat == PIX_FMT_NV21) {
                const int chrSkipMask= (1<<c->chrDstVSubSample)-1;
                if (dstY&chrSkipMask) uDest= NULL; //FIXME split functions in lumi / chromi
                yuv2nv12XinC(
                             vLumFilter+dstY*vLumFilterSize   , lumSrcPtr, vLumFilterSize,
                             vChrFilter+chrDstY*vChrFilterSize, chrUSrcPtr, chrVSrcPtr, vChrFilterSize,
                             dest, uDest, dstW, chrDstW, dstFormat, lumDither, chrDither);
            } else if (isPlanarYUV(dstFormat) || dstFormat==PIX_FMT_GRAY8) { //YV12
                const int chrSkipMask= (1<<c->chrDstVSubSample)-1;
                if ((dstY&chrSkipMask) || isGray(dstFormat)) uDest=vDest= NULL; //FIXME split functions in lumi / chromi
                if (is16BPS(dstFormat) || isNBPS(dstFormat)) {
                    yuv2yuvX16inC(
                                  vLumFilter+dstY*vLumFilterSize   , lumSrcPtr, vLumFilterSize,
                                  vChrFilter+chrDstY*vChrFilterSize, chrUSrcPtr, chrVSrcPtr, vChrFilterSize,
                                  alpSrcPtr, (uint16_t *) dest, (uint16_t *) uDest, (uint16_t *) vDest, (uint16_t *) aDest, dstW, chrDstW,
                                  dstFormat);
                } else {
                    yuv2yuvXinC(
                                vLumFilter+dstY*vLumFilterSize   , lumSrcPtr, vLumFilterSize,
                                vChrFilter+chrDstY*vChrFilterSize, chrUSrcPtr, chrVSrcPtr, vChrFilterSize,
                                alpSrcPtr, dest, uDest, vDest, aDest, dstW, chrDstW, lumDither, chrDither);
                }
            } else {
                assert(lumSrcPtr + vLumFilterSize - 1 < lumPixBuf + vLumBufSize*2);
                assert(chrUSrcPtr + vChrFilterSize - 1 < chrUPixBuf + vChrBufSize*2);
                if(flags & SWS_FULL_CHR_H_INT) {
                    yuv2rgbXinC_full(c,
                                     vLumFilter+dstY*vLumFilterSize, lumSrcPtr, vLumFilterSize,
                                     vChrFilter+dstY*vChrFilterSize, chrUSrcPtr, chrVSrcPtr, vChrFilterSize,
                                     alpSrcPtr, dest, dstW, dstY);
                } else {
                    yuv2packedXinC(c,
                                   vLumFilter+dstY*vLumFilterSize, lumSrcPtr, vLumFilterSize,
                                   vChrFilter+dstY*vChrFilterSize, chrUSrcPtr, chrVSrcPtr, vChrFilterSize,
                                   alpSrcPtr, dest, dstW, dstY);
                }
            }
        }
    }

    if ((dstFormat == PIX_FMT_YUVA420P) && !alpPixBuf)
        fillPlane(dst[3], dstStride[3], dstW, dstY-lastDstY, lastDstY, 255);

#if HAVE_MMX2
    if (av_get_cpu_flags() & AV_CPU_FLAG_MMX2)
        __asm__ volatile("sfence":::"memory");
#endif
    emms_c();

    /* store changed local vars back in the context */
    c->dstY= dstY;
    c->lumBufIndex= lumBufIndex;
    c->chrBufIndex= chrBufIndex;
    c->lastInLumBuf= lastInLumBuf;
    c->lastInChrBuf= lastInChrBuf;

    return dstY - lastDstY;
}

static void sws_init_swScale_c(SwsContext *c)
{
    enum PixelFormat srcFormat = c->srcFormat;

    c->yuv2nv12X    = yuv2nv12X_c;
    c->yuv2yuv1     = yuv2yuv1_c;
    c->yuv2yuvX     = yuv2yuvX_c;
    c->yuv2packed1  = yuv2packed1_c;
    c->yuv2packed2  = yuv2packed2_c;
    c->yuv2packedX  = yuv2packedX_c;

    c->hScale       = hScale_c;

    if (c->flags & SWS_FAST_BILINEAR)
    {
        c->hyscale_fast = hyscale_fast_c;
        c->hcscale_fast = hcscale_fast_c;
    }

    c->chrToYV12 = NULL;
    switch(srcFormat) {
        case PIX_FMT_YUYV422  : c->chrToYV12 = yuy2ToUV_c; break;
        case PIX_FMT_UYVY422  : c->chrToYV12 = uyvyToUV_c; break;
        case PIX_FMT_NV12     : c->chrToYV12 = nv12ToUV_c; break;
        case PIX_FMT_NV21     : c->chrToYV12 = nv21ToUV_c; break;
        case PIX_FMT_RGB8     :
        case PIX_FMT_BGR8     :
        case PIX_FMT_PAL8     :
        case PIX_FMT_BGR4_BYTE:
        case PIX_FMT_RGB4_BYTE: c->chrToYV12 = palToUV; break;
        case PIX_FMT_GRAY16BE :
        case PIX_FMT_YUV420P9BE:
        case PIX_FMT_YUV422P10BE:
        case PIX_FMT_YUV420P10BE:
        case PIX_FMT_YUV420P16BE:
        case PIX_FMT_YUV422P16BE:
        case PIX_FMT_YUV444P16BE: c->hScale16= HAVE_BIGENDIAN ? hScale16_c : hScale16X_c; break;
        case PIX_FMT_GRAY16LE :
        case PIX_FMT_YUV420P9LE:
        case PIX_FMT_YUV422P10LE:
        case PIX_FMT_YUV420P10LE:
        case PIX_FMT_YUV420P16LE:
        case PIX_FMT_YUV422P16LE:
        case PIX_FMT_YUV444P16LE: c->hScale16= HAVE_BIGENDIAN ? hScale16X_c : hScale16_c; break;
    }
    if (c->chrSrcHSubSample) {
        switch(srcFormat) {
        case PIX_FMT_RGB48BE: c->chrToYV12 = rgb48BEToUV_half; break;
        case PIX_FMT_RGB48LE: c->chrToYV12 = rgb48LEToUV_half; break;
        case PIX_FMT_BGR48BE: c->chrToYV12 = bgr48BEToUV_half; break;
        case PIX_FMT_BGR48LE: c->chrToYV12 = bgr48LEToUV_half; break;
        case PIX_FMT_RGB32  : c->chrToYV12 = bgr32ToUV_half;  break;
        case PIX_FMT_RGB32_1: c->chrToYV12 = bgr321ToUV_half; break;
        case PIX_FMT_BGR24  : c->chrToYV12 = bgr24ToUV_half_c; break;
        case PIX_FMT_BGR565 : c->chrToYV12 = bgr16ToUV_half; break;
        case PIX_FMT_BGR555 : c->chrToYV12 = bgr15ToUV_half; break;
        case PIX_FMT_BGR32  : c->chrToYV12 = rgb32ToUV_half;  break;
        case PIX_FMT_BGR32_1: c->chrToYV12 = rgb321ToUV_half; break;
        case PIX_FMT_RGB24  : c->chrToYV12 = rgb24ToUV_half_c; break;
        case PIX_FMT_RGB565 : c->chrToYV12 = rgb16ToUV_half; break;
        case PIX_FMT_RGB555 : c->chrToYV12 = rgb15ToUV_half; break;
        }
    } else {
        switch(srcFormat) {
        case PIX_FMT_RGB48BE: c->chrToYV12 = rgb48BEToUV; break;
        case PIX_FMT_RGB48LE: c->chrToYV12 = rgb48LEToUV; break;
        case PIX_FMT_BGR48BE: c->chrToYV12 = bgr48BEToUV; break;
        case PIX_FMT_BGR48LE: c->chrToYV12 = bgr48LEToUV; break;
        case PIX_FMT_RGB32  : c->chrToYV12 = bgr32ToUV;  break;
        case PIX_FMT_RGB32_1: c->chrToYV12 = bgr321ToUV; break;
        case PIX_FMT_BGR24  : c->chrToYV12 = bgr24ToUV_c; break;
        case PIX_FMT_BGR565 : c->chrToYV12 = bgr16ToUV; break;
        case PIX_FMT_BGR555 : c->chrToYV12 = bgr15ToUV; break;
        case PIX_FMT_BGR32  : c->chrToYV12 = rgb32ToUV;  break;
        case PIX_FMT_BGR32_1: c->chrToYV12 = rgb321ToUV; break;
        case PIX_FMT_RGB24  : c->chrToYV12 = rgb24ToUV_c; break;
        case PIX_FMT_RGB565 : c->chrToYV12 = rgb16ToUV; break;
        case PIX_FMT_RGB555 : c->chrToYV12 = rgb15ToUV; break;
        }
    }

    c->lumToYV12 = NULL;
    c->alpToYV12 = NULL;
    switch (srcFormat) {
    case PIX_FMT_YUYV422  :
    case PIX_FMT_GRAY8A   :
                            c->lumToYV12 = yuy2ToY_c; break;
    case PIX_FMT_UYVY422  :
                            c->lumToYV12 = uyvyToY_c; break;
    case PIX_FMT_BGR24    : c->lumToYV12 = bgr24ToY_c; break;
    case PIX_FMT_BGR565   : c->lumToYV12 = bgr16ToY; break;
    case PIX_FMT_BGR555   : c->lumToYV12 = bgr15ToY; break;
    case PIX_FMT_RGB24    : c->lumToYV12 = rgb24ToY_c; break;
    case PIX_FMT_RGB565   : c->lumToYV12 = rgb16ToY; break;
    case PIX_FMT_RGB555   : c->lumToYV12 = rgb15ToY; break;
    case PIX_FMT_RGB8     :
    case PIX_FMT_BGR8     :
    case PIX_FMT_PAL8     :
    case PIX_FMT_BGR4_BYTE:
    case PIX_FMT_RGB4_BYTE: c->lumToYV12 = palToY; break;
    case PIX_FMT_MONOBLACK: c->lumToYV12 = monoblack2Y; break;
    case PIX_FMT_MONOWHITE: c->lumToYV12 = monowhite2Y; break;
    case PIX_FMT_RGB32  : c->lumToYV12 = bgr32ToY;  break;
    case PIX_FMT_RGB32_1: c->lumToYV12 = bgr321ToY; break;
    case PIX_FMT_BGR32  : c->lumToYV12 = rgb32ToY;  break;
    case PIX_FMT_BGR32_1: c->lumToYV12 = rgb321ToY; break;
    case PIX_FMT_RGB48BE: c->lumToYV12 = rgb48BEToY; break;
    case PIX_FMT_RGB48LE: c->lumToYV12 = rgb48LEToY; break;
    case PIX_FMT_BGR48BE: c->lumToYV12 = bgr48BEToY; break;
    case PIX_FMT_BGR48LE: c->lumToYV12 = bgr48LEToY; break;
    }
    if (c->alpPixBuf) {
        switch (srcFormat) {
        case PIX_FMT_RGB32  :
        case PIX_FMT_RGB32_1:
        case PIX_FMT_BGR32  :
        case PIX_FMT_BGR32_1: c->alpToYV12 = abgrToA; break;
        case PIX_FMT_GRAY8A : c->alpToYV12 = yuy2ToY_c; break;
        case PIX_FMT_PAL8   : c->alpToYV12 = palToA; break;
        }
    }

    if(isAnyRGB(c->srcFormat) || c->srcFormat == PIX_FMT_PAL8)
        c->hScale16= hScale16_c;

    switch (srcFormat) {
    case PIX_FMT_GRAY8A :
        c->alpSrcOffset = 1;
        break;
    case PIX_FMT_RGB32  :
    case PIX_FMT_BGR32  :
        c->alpSrcOffset = 3;
        break;
    }

    if (c->srcRange != c->dstRange && !isAnyRGB(c->dstFormat)) {
        if (c->srcRange) {
            c->lumConvertRange = lumRangeFromJpeg_c;
            c->chrConvertRange = chrRangeFromJpeg_c;
        } else {
            c->lumConvertRange = lumRangeToJpeg_c;
            c->chrConvertRange = chrRangeToJpeg_c;
        }
    }

    if (!(isGray(srcFormat) || isGray(c->dstFormat) ||
          srcFormat == PIX_FMT_MONOBLACK || srcFormat == PIX_FMT_MONOWHITE))
        c->needs_hcscale = 1;
}
