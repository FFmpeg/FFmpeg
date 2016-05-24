/*
 * Copyright (C) 2001-2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <inttypes.h>
#include "config.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"
#include "libavutil/attributes.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/x86/cpu.h"
#include "libavutil/cpu.h"
#include "libavutil/pixdesc.h"

#if HAVE_INLINE_ASM

#define DITHER1XBPP

DECLARE_ASM_CONST(8, uint64_t, bF8)=       0xF8F8F8F8F8F8F8F8LL;
DECLARE_ASM_CONST(8, uint64_t, bFC)=       0xFCFCFCFCFCFCFCFCLL;
DECLARE_ASM_CONST(8, uint64_t, w10)=       0x0010001000100010LL;
DECLARE_ASM_CONST(8, uint64_t, w02)=       0x0002000200020002LL;

const DECLARE_ALIGNED(8, uint64_t, ff_dither4)[2] = {
    0x0103010301030103LL,
    0x0200020002000200LL,};

const DECLARE_ALIGNED(8, uint64_t, ff_dither8)[2] = {
    0x0602060206020602LL,
    0x0004000400040004LL,};

DECLARE_ASM_CONST(8, uint64_t, b16Mask)=   0x001F001F001F001FLL;
DECLARE_ASM_CONST(8, uint64_t, g16Mask)=   0x07E007E007E007E0LL;
DECLARE_ASM_CONST(8, uint64_t, r16Mask)=   0xF800F800F800F800LL;
DECLARE_ASM_CONST(8, uint64_t, b15Mask)=   0x001F001F001F001FLL;
DECLARE_ASM_CONST(8, uint64_t, g15Mask)=   0x03E003E003E003E0LL;
DECLARE_ASM_CONST(8, uint64_t, r15Mask)=   0x7C007C007C007C00LL;

DECLARE_ALIGNED(8, const uint64_t, ff_M24A)         = 0x00FF0000FF0000FFLL;
DECLARE_ALIGNED(8, const uint64_t, ff_M24B)         = 0xFF0000FF0000FF00LL;
DECLARE_ALIGNED(8, const uint64_t, ff_M24C)         = 0x0000FF0000FF0000LL;

#ifdef FAST_BGR2YV12
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2YCoeff)   = 0x000000210041000DULL;
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2UCoeff)   = 0x0000FFEEFFDC0038ULL;
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2VCoeff)   = 0x00000038FFD2FFF8ULL;
#else
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2YCoeff)   = 0x000020E540830C8BULL;
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2UCoeff)   = 0x0000ED0FDAC23831ULL;
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2VCoeff)   = 0x00003831D0E6F6EAULL;
#endif /* FAST_BGR2YV12 */
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2YOffset)  = 0x1010101010101010ULL;
DECLARE_ALIGNED(8, const uint64_t, ff_bgr2UVOffset) = 0x8080808080808080ULL;
DECLARE_ALIGNED(8, const uint64_t, ff_w1111)        = 0x0001000100010001ULL;

//MMX versions
#if HAVE_MMX_INLINE
#undef RENAME
#define COMPILE_TEMPLATE_MMXEXT 0
#define RENAME(a) a ## _mmx
#include "swscale_template.c"
#endif

// MMXEXT versions
#if HAVE_MMXEXT_INLINE
#undef RENAME
#undef COMPILE_TEMPLATE_MMXEXT
#define COMPILE_TEMPLATE_MMXEXT 1
#define RENAME(a) a ## _mmxext
#include "swscale_template.c"
#endif

void updateMMXDitherTables(SwsContext *c, int dstY, int lumBufIndex, int chrBufIndex,
                           int lastInLumBuf, int lastInChrBuf)
{
    const int dstH= c->dstH;
    const int flags= c->flags;
    int16_t **lumPixBuf= c->lumPixBuf;
    int16_t **chrUPixBuf= c->chrUPixBuf;
    int16_t **alpPixBuf= c->alpPixBuf;
    const int vLumBufSize= c->vLumBufSize;
    const int vChrBufSize= c->vChrBufSize;
    int32_t *vLumFilterPos= c->vLumFilterPos;
    int32_t *vChrFilterPos= c->vChrFilterPos;
    int16_t *vLumFilter= c->vLumFilter;
    int16_t *vChrFilter= c->vChrFilter;
    int32_t *lumMmxFilter= c->lumMmxFilter;
    int32_t *chrMmxFilter= c->chrMmxFilter;
    int32_t av_unused *alpMmxFilter= c->alpMmxFilter;
    const int vLumFilterSize= c->vLumFilterSize;
    const int vChrFilterSize= c->vChrFilterSize;
    const int chrDstY= dstY>>c->chrDstVSubSample;
    const int firstLumSrcY= vLumFilterPos[dstY]; //First line needed as input
    const int firstChrSrcY= vChrFilterPos[chrDstY]; //First line needed as input

    c->blueDither= ff_dither8[dstY&1];
    if (c->dstFormat == AV_PIX_FMT_RGB555 || c->dstFormat == AV_PIX_FMT_BGR555)
        c->greenDither= ff_dither8[dstY&1];
    else
        c->greenDither= ff_dither4[dstY&1];
    c->redDither= ff_dither8[(dstY+1)&1];
    if (dstY < dstH - 2) {
        const int16_t **lumSrcPtr= (const int16_t **) lumPixBuf + lumBufIndex + firstLumSrcY - lastInLumBuf + vLumBufSize;
        const int16_t **chrUSrcPtr= (const int16_t **) chrUPixBuf + chrBufIndex + firstChrSrcY - lastInChrBuf + vChrBufSize;
        const int16_t **alpSrcPtr= (CONFIG_SWSCALE_ALPHA && alpPixBuf) ? (const int16_t **) alpPixBuf + lumBufIndex + firstLumSrcY - lastInLumBuf + vLumBufSize : NULL;
        int i;

        if (firstLumSrcY < 0 || firstLumSrcY + vLumFilterSize > c->srcH) {
            const int16_t **tmpY = (const int16_t **) lumPixBuf + 2 * vLumBufSize;
            int neg = -firstLumSrcY, i, end = FFMIN(c->srcH - firstLumSrcY, vLumFilterSize);
            for (i = 0; i < neg;            i++)
                tmpY[i] = lumSrcPtr[neg];
            for (     ; i < end;            i++)
                tmpY[i] = lumSrcPtr[i];
            for (     ; i < vLumFilterSize; i++)
                tmpY[i] = tmpY[i-1];
            lumSrcPtr = tmpY;

            if (alpSrcPtr) {
                const int16_t **tmpA = (const int16_t **) alpPixBuf + 2 * vLumBufSize;
                for (i = 0; i < neg;            i++)
                    tmpA[i] = alpSrcPtr[neg];
                for (     ; i < end;            i++)
                    tmpA[i] = alpSrcPtr[i];
                for (     ; i < vLumFilterSize; i++)
                    tmpA[i] = tmpA[i - 1];
                alpSrcPtr = tmpA;
            }
        }
        if (firstChrSrcY < 0 || firstChrSrcY + vChrFilterSize > c->chrSrcH) {
            const int16_t **tmpU = (const int16_t **) chrUPixBuf + 2 * vChrBufSize;
            int neg = -firstChrSrcY, i, end = FFMIN(c->chrSrcH - firstChrSrcY, vChrFilterSize);
            for (i = 0; i < neg;            i++) {
                tmpU[i] = chrUSrcPtr[neg];
            }
            for (     ; i < end;            i++) {
                tmpU[i] = chrUSrcPtr[i];
            }
            for (     ; i < vChrFilterSize; i++) {
                tmpU[i] = tmpU[i - 1];
            }
            chrUSrcPtr = tmpU;
        }

        if (flags & SWS_ACCURATE_RND) {
            int s= APCK_SIZE / 8;
            for (i=0; i<vLumFilterSize; i+=2) {
                *(const void**)&lumMmxFilter[s*i              ]= lumSrcPtr[i  ];
                *(const void**)&lumMmxFilter[s*i+APCK_PTR2/4  ]= lumSrcPtr[i+(vLumFilterSize>1)];
                lumMmxFilter[s*i+APCK_COEF/4  ]=
                lumMmxFilter[s*i+APCK_COEF/4+1]= vLumFilter[dstY*vLumFilterSize + i    ]
                + (vLumFilterSize>1 ? vLumFilter[dstY*vLumFilterSize + i + 1]<<16 : 0);
                if (CONFIG_SWSCALE_ALPHA && alpPixBuf) {
                    *(const void**)&alpMmxFilter[s*i              ]= alpSrcPtr[i  ];
                    *(const void**)&alpMmxFilter[s*i+APCK_PTR2/4  ]= alpSrcPtr[i+(vLumFilterSize>1)];
                    alpMmxFilter[s*i+APCK_COEF/4  ]=
                    alpMmxFilter[s*i+APCK_COEF/4+1]= lumMmxFilter[s*i+APCK_COEF/4  ];
                }
            }
            for (i=0; i<vChrFilterSize; i+=2) {
                *(const void**)&chrMmxFilter[s*i              ]= chrUSrcPtr[i  ];
                *(const void**)&chrMmxFilter[s*i+APCK_PTR2/4  ]= chrUSrcPtr[i+(vChrFilterSize>1)];
                chrMmxFilter[s*i+APCK_COEF/4  ]=
                chrMmxFilter[s*i+APCK_COEF/4+1]= vChrFilter[chrDstY*vChrFilterSize + i    ]
                + (vChrFilterSize>1 ? vChrFilter[chrDstY*vChrFilterSize + i + 1]<<16 : 0);
            }
        } else {
            for (i=0; i<vLumFilterSize; i++) {
                *(const void**)&lumMmxFilter[4*i+0]= lumSrcPtr[i];
                lumMmxFilter[4*i+2]=
                lumMmxFilter[4*i+3]=
                ((uint16_t)vLumFilter[dstY*vLumFilterSize + i])*0x10001;
                if (CONFIG_SWSCALE_ALPHA && alpPixBuf) {
                    *(const void**)&alpMmxFilter[4*i+0]= alpSrcPtr[i];
                    alpMmxFilter[4*i+2]=
                    alpMmxFilter[4*i+3]= lumMmxFilter[4*i+2];
                }
            }
            for (i=0; i<vChrFilterSize; i++) {
                *(const void**)&chrMmxFilter[4*i+0]= chrUSrcPtr[i];
                chrMmxFilter[4*i+2]=
                chrMmxFilter[4*i+3]=
                ((uint16_t)vChrFilter[chrDstY*vChrFilterSize + i])*0x10001;
            }
        }
    }
}

#endif /* HAVE_INLINE_ASM */

#define SCALE_FUNC(filter_n, from_bpc, to_bpc, opt) \
void ff_hscale ## from_bpc ## to ## to_bpc ## _ ## filter_n ## _ ## opt( \
                                                SwsContext *c, int16_t *data, \
                                                int dstW, const uint8_t *src, \
                                                const int16_t *filter, \
                                                const int32_t *filterPos, int filterSize)

#define SCALE_FUNCS(filter_n, opt) \
    SCALE_FUNC(filter_n,  8, 15, opt); \
    SCALE_FUNC(filter_n,  9, 15, opt); \
    SCALE_FUNC(filter_n, 10, 15, opt); \
    SCALE_FUNC(filter_n, 16, 15, opt); \
    SCALE_FUNC(filter_n,  8, 19, opt); \
    SCALE_FUNC(filter_n,  9, 19, opt); \
    SCALE_FUNC(filter_n, 10, 19, opt); \
    SCALE_FUNC(filter_n, 16, 19, opt)

#define SCALE_FUNCS_MMX(opt) \
    SCALE_FUNCS(4, opt); \
    SCALE_FUNCS(8, opt); \
    SCALE_FUNCS(X, opt)

#define SCALE_FUNCS_SSE(opt) \
    SCALE_FUNCS(4, opt); \
    SCALE_FUNCS(8, opt); \
    SCALE_FUNCS(X4, opt); \
    SCALE_FUNCS(X8, opt)

#if ARCH_X86_32
SCALE_FUNCS_MMX(mmx);
#endif
SCALE_FUNCS_SSE(sse2);
SCALE_FUNCS_SSE(ssse3);
SCALE_FUNCS_SSE(sse4);

#define VSCALEX_FUNC(size, opt) \
void ff_yuv2planeX_ ## size ## _ ## opt(const int16_t *filter, int filterSize, \
                                        const int16_t **src, uint8_t *dest, int dstW, \
                                        const uint8_t *dither, int offset)
#define VSCALEX_FUNCS(opt) \
    VSCALEX_FUNC(8,  opt); \
    VSCALEX_FUNC(9,  opt); \
    VSCALEX_FUNC(10, opt)

#if ARCH_X86_32
VSCALEX_FUNCS(mmxext);
#endif
VSCALEX_FUNCS(sse2);
VSCALEX_FUNCS(sse4);
VSCALEX_FUNC(16, sse4);
VSCALEX_FUNCS(avx);

#define VSCALE_FUNC(size, opt) \
void ff_yuv2plane1_ ## size ## _ ## opt(const int16_t *src, uint8_t *dst, int dstW, \
                                        const uint8_t *dither, int offset)
#define VSCALE_FUNCS(opt1, opt2) \
    VSCALE_FUNC(8,  opt1); \
    VSCALE_FUNC(9,  opt2); \
    VSCALE_FUNC(10, opt2); \
    VSCALE_FUNC(16, opt1)

#if ARCH_X86_32
VSCALE_FUNCS(mmx, mmxext);
#endif
VSCALE_FUNCS(sse2, sse2);
VSCALE_FUNC(16, sse4);
VSCALE_FUNCS(avx, avx);

#define INPUT_Y_FUNC(fmt, opt) \
void ff_ ## fmt ## ToY_  ## opt(uint8_t *dst, const uint8_t *src, \
                                int w, uint32_t *unused)
#define INPUT_UV_FUNC(fmt, opt) \
void ff_ ## fmt ## ToUV_ ## opt(uint8_t *dstU, uint8_t *dstV, \
                                const uint8_t *src, const uint8_t *unused1, \
                                int w, uint32_t *unused2)
#define INPUT_FUNC(fmt, opt) \
    INPUT_Y_FUNC(fmt, opt); \
    INPUT_UV_FUNC(fmt, opt)
#define INPUT_FUNCS(opt) \
    INPUT_FUNC(uyvy, opt); \
    INPUT_FUNC(yuyv, opt); \
    INPUT_UV_FUNC(nv12, opt); \
    INPUT_UV_FUNC(nv21, opt); \
    INPUT_FUNC(rgba, opt); \
    INPUT_FUNC(bgra, opt); \
    INPUT_FUNC(argb, opt); \
    INPUT_FUNC(abgr, opt); \
    INPUT_FUNC(rgb24, opt); \
    INPUT_FUNC(bgr24, opt)

#if ARCH_X86_32
INPUT_FUNCS(mmx);
#endif
INPUT_FUNCS(sse2);
INPUT_FUNCS(ssse3);
INPUT_FUNCS(avx);

av_cold void ff_sws_init_swscale_x86(SwsContext *c)
{
    int cpu_flags = av_get_cpu_flags();

#if HAVE_MMX_INLINE
    if (INLINE_MMX(cpu_flags))
        sws_init_swscale_mmx(c);
#endif
#if HAVE_MMXEXT_INLINE
    if (INLINE_MMXEXT(cpu_flags))
        sws_init_swscale_mmxext(c);
#endif

#define ASSIGN_SCALE_FUNC2(hscalefn, filtersize, opt1, opt2) do { \
    if (c->srcBpc == 8) { \
        hscalefn = c->dstBpc <= 10 ? ff_hscale8to15_ ## filtersize ## _ ## opt2 : \
                                     ff_hscale8to19_ ## filtersize ## _ ## opt1; \
    } else if (c->srcBpc == 9) { \
        hscalefn = c->dstBpc <= 10 ? ff_hscale9to15_ ## filtersize ## _ ## opt2 : \
                                     ff_hscale9to19_ ## filtersize ## _ ## opt1; \
    } else if (c->srcBpc == 10) { \
        hscalefn = c->dstBpc <= 10 ? ff_hscale10to15_ ## filtersize ## _ ## opt2 : \
                                     ff_hscale10to19_ ## filtersize ## _ ## opt1; \
    } else /* c->srcBpc == 16 */ { \
        hscalefn = c->dstBpc <= 10 ? ff_hscale16to15_ ## filtersize ## _ ## opt2 : \
                                     ff_hscale16to19_ ## filtersize ## _ ## opt1; \
    } \
} while (0)
#define ASSIGN_MMX_SCALE_FUNC(hscalefn, filtersize, opt1, opt2) \
    switch (filtersize) { \
    case 4:  ASSIGN_SCALE_FUNC2(hscalefn, 4, opt1, opt2); break; \
    case 8:  ASSIGN_SCALE_FUNC2(hscalefn, 8, opt1, opt2); break; \
    default: ASSIGN_SCALE_FUNC2(hscalefn, X, opt1, opt2); break; \
    }
#define ASSIGN_VSCALEX_FUNC(vscalefn, opt, do_16_case, condition_8bit) \
switch(c->dstBpc){ \
    case 16:                          do_16_case;                          break; \
    case 10: if (!isBE(c->dstFormat)) vscalefn = ff_yuv2planeX_10_ ## opt; break; \
    case 9:  if (!isBE(c->dstFormat)) vscalefn = ff_yuv2planeX_9_  ## opt; break; \
    default: if (condition_8bit)      vscalefn = ff_yuv2planeX_8_  ## opt; break; \
    }
#define ASSIGN_VSCALE_FUNC(vscalefn, opt1, opt2, opt2chk) \
    switch(c->dstBpc){ \
    case 16: if (!isBE(c->dstFormat))            vscalefn = ff_yuv2plane1_16_ ## opt1; break; \
    case 10: if (!isBE(c->dstFormat) && opt2chk) vscalefn = ff_yuv2plane1_10_ ## opt2; break; \
    case 9:  if (!isBE(c->dstFormat) && opt2chk) vscalefn = ff_yuv2plane1_9_  ## opt2;  break; \
    default:                                     vscalefn = ff_yuv2plane1_8_  ## opt1;  break; \
    }
#define case_rgb(x, X, opt) \
        case AV_PIX_FMT_ ## X: \
            c->lumToYV12 = ff_ ## x ## ToY_ ## opt; \
            if (!c->chrSrcHSubSample) \
                c->chrToYV12 = ff_ ## x ## ToUV_ ## opt; \
            break
#if ARCH_X86_32
    if (EXTERNAL_MMX(cpu_flags)) {
        ASSIGN_MMX_SCALE_FUNC(c->hyScale, c->hLumFilterSize, mmx, mmx);
        ASSIGN_MMX_SCALE_FUNC(c->hcScale, c->hChrFilterSize, mmx, mmx);
        ASSIGN_VSCALE_FUNC(c->yuv2plane1, mmx, mmxext, cpu_flags & AV_CPU_FLAG_MMXEXT);

        switch (c->srcFormat) {
        case AV_PIX_FMT_YA8:
            c->lumToYV12 = ff_yuyvToY_mmx;
            if (c->alpPixBuf)
                c->alpToYV12 = ff_uyvyToY_mmx;
            break;
        case AV_PIX_FMT_YUYV422:
            c->lumToYV12 = ff_yuyvToY_mmx;
            c->chrToYV12 = ff_yuyvToUV_mmx;
            break;
        case AV_PIX_FMT_UYVY422:
            c->lumToYV12 = ff_uyvyToY_mmx;
            c->chrToYV12 = ff_uyvyToUV_mmx;
            break;
        case AV_PIX_FMT_NV12:
            c->chrToYV12 = ff_nv12ToUV_mmx;
            break;
        case AV_PIX_FMT_NV21:
            c->chrToYV12 = ff_nv21ToUV_mmx;
            break;
        case_rgb(rgb24, RGB24, mmx);
        case_rgb(bgr24, BGR24, mmx);
        case_rgb(bgra,  BGRA,  mmx);
        case_rgb(rgba,  RGBA,  mmx);
        case_rgb(abgr,  ABGR,  mmx);
        case_rgb(argb,  ARGB,  mmx);
        default:
            break;
        }
    }
    if (EXTERNAL_MMXEXT(cpu_flags)) {
        ASSIGN_VSCALEX_FUNC(c->yuv2planeX, mmxext, , 1);
    }
#endif /* ARCH_X86_32 */
#define ASSIGN_SSE_SCALE_FUNC(hscalefn, filtersize, opt1, opt2) \
    switch (filtersize) { \
    case 4:  ASSIGN_SCALE_FUNC2(hscalefn, 4, opt1, opt2); break; \
    case 8:  ASSIGN_SCALE_FUNC2(hscalefn, 8, opt1, opt2); break; \
    default: if (filtersize & 4) ASSIGN_SCALE_FUNC2(hscalefn, X4, opt1, opt2); \
             else                ASSIGN_SCALE_FUNC2(hscalefn, X8, opt1, opt2); \
             break; \
    }
    if (EXTERNAL_SSE2(cpu_flags)) {
        ASSIGN_SSE_SCALE_FUNC(c->hyScale, c->hLumFilterSize, sse2, sse2);
        ASSIGN_SSE_SCALE_FUNC(c->hcScale, c->hChrFilterSize, sse2, sse2);
        ASSIGN_VSCALEX_FUNC(c->yuv2planeX, sse2, ,
                            HAVE_ALIGNED_STACK || ARCH_X86_64);
        ASSIGN_VSCALE_FUNC(c->yuv2plane1, sse2, sse2, 1);

        switch (c->srcFormat) {
        case AV_PIX_FMT_YA8:
            c->lumToYV12 = ff_yuyvToY_sse2;
            if (c->alpPixBuf)
                c->alpToYV12 = ff_uyvyToY_sse2;
            break;
        case AV_PIX_FMT_YUYV422:
            c->lumToYV12 = ff_yuyvToY_sse2;
            c->chrToYV12 = ff_yuyvToUV_sse2;
            break;
        case AV_PIX_FMT_UYVY422:
            c->lumToYV12 = ff_uyvyToY_sse2;
            c->chrToYV12 = ff_uyvyToUV_sse2;
            break;
        case AV_PIX_FMT_NV12:
            c->chrToYV12 = ff_nv12ToUV_sse2;
            break;
        case AV_PIX_FMT_NV21:
            c->chrToYV12 = ff_nv21ToUV_sse2;
            break;
        case_rgb(rgb24, RGB24, sse2);
        case_rgb(bgr24, BGR24, sse2);
        case_rgb(bgra,  BGRA,  sse2);
        case_rgb(rgba,  RGBA,  sse2);
        case_rgb(abgr,  ABGR,  sse2);
        case_rgb(argb,  ARGB,  sse2);
        default:
            break;
        }
    }
    if (EXTERNAL_SSSE3(cpu_flags)) {
        ASSIGN_SSE_SCALE_FUNC(c->hyScale, c->hLumFilterSize, ssse3, ssse3);
        ASSIGN_SSE_SCALE_FUNC(c->hcScale, c->hChrFilterSize, ssse3, ssse3);
        switch (c->srcFormat) {
        case_rgb(rgb24, RGB24, ssse3);
        case_rgb(bgr24, BGR24, ssse3);
        default:
            break;
        }
    }
    if (EXTERNAL_SSE4(cpu_flags)) {
        /* Xto15 don't need special sse4 functions */
        ASSIGN_SSE_SCALE_FUNC(c->hyScale, c->hLumFilterSize, sse4, ssse3);
        ASSIGN_SSE_SCALE_FUNC(c->hcScale, c->hChrFilterSize, sse4, ssse3);
        ASSIGN_VSCALEX_FUNC(c->yuv2planeX, sse4,
                            if (!isBE(c->dstFormat)) c->yuv2planeX = ff_yuv2planeX_16_sse4,
                            HAVE_ALIGNED_STACK || ARCH_X86_64);
        if (c->dstBpc == 16 && !isBE(c->dstFormat))
            c->yuv2plane1 = ff_yuv2plane1_16_sse4;
    }

    if (EXTERNAL_AVX(cpu_flags)) {
        ASSIGN_VSCALEX_FUNC(c->yuv2planeX, avx, ,
                            HAVE_ALIGNED_STACK || ARCH_X86_64);
        ASSIGN_VSCALE_FUNC(c->yuv2plane1, avx, avx, 1);

        switch (c->srcFormat) {
        case AV_PIX_FMT_YUYV422:
            c->chrToYV12 = ff_yuyvToUV_avx;
            break;
        case AV_PIX_FMT_UYVY422:
            c->chrToYV12 = ff_uyvyToUV_avx;
            break;
        case AV_PIX_FMT_NV12:
            c->chrToYV12 = ff_nv12ToUV_avx;
            break;
        case AV_PIX_FMT_NV21:
            c->chrToYV12 = ff_nv21ToUV_avx;
            break;
        case_rgb(rgb24, RGB24, avx);
        case_rgb(bgr24, BGR24, avx);
        case_rgb(bgra,  BGRA,  avx);
        case_rgb(rgba,  RGBA,  avx);
        case_rgb(abgr,  ABGR,  avx);
        case_rgb(argb,  ARGB,  avx);
        default:
            break;
        }
    }
}
