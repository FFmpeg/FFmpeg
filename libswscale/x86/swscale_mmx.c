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

#include <inttypes.h>
#include "config.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/x86_cpu.h"
#include "libavutil/cpu.h"
#include "libavutil/pixdesc.h"

DECLARE_ASM_CONST(8, uint64_t, bF8)=       0xF8F8F8F8F8F8F8F8LL;
DECLARE_ASM_CONST(8, uint64_t, bFC)=       0xFCFCFCFCFCFCFCFCLL;
DECLARE_ASM_CONST(8, uint64_t, w10)=       0x0010001000100010LL;
DECLARE_ASM_CONST(8, uint64_t, w02)=       0x0002000200020002LL;
DECLARE_ASM_CONST(8, uint64_t, bm00001111)=0x00000000FFFFFFFFLL;
DECLARE_ASM_CONST(8, uint64_t, bm00000111)=0x0000000000FFFFFFLL;
DECLARE_ASM_CONST(8, uint64_t, bm11111000)=0xFFFFFFFFFF000000LL;
DECLARE_ASM_CONST(8, uint64_t, bm01010101)=0x00FF00FF00FF00FFLL;

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

DECLARE_ASM_CONST(8, uint64_t, ff_bgr24toY1Coeff) = 0x0C88000040870C88ULL;
DECLARE_ASM_CONST(8, uint64_t, ff_bgr24toY2Coeff) = 0x20DE4087000020DEULL;
DECLARE_ASM_CONST(8, uint64_t, ff_rgb24toY1Coeff) = 0x20DE0000408720DEULL;
DECLARE_ASM_CONST(8, uint64_t, ff_rgb24toY2Coeff) = 0x0C88408700000C88ULL;
DECLARE_ASM_CONST(8, uint64_t, ff_bgr24toYOffset) = 0x0008010000080100ULL;

DECLARE_ASM_CONST(8, uint64_t, ff_bgr24toUV)[2][4] = {
    {0x38380000DAC83838ULL, 0xECFFDAC80000ECFFULL, 0xF6E40000D0E3F6E4ULL, 0x3838D0E300003838ULL},
    {0xECFF0000DAC8ECFFULL, 0x3838DAC800003838ULL, 0x38380000D0E33838ULL, 0xF6E4D0E30000F6E4ULL},
};

DECLARE_ASM_CONST(8, uint64_t, ff_bgr24toUVOffset)= 0x0040010000400100ULL;

//MMX versions
#if HAVE_MMX
#undef RENAME
#define COMPILE_TEMPLATE_MMX2 0
#define RENAME(a) a ## _MMX
#include "swscale_template.c"
#endif

//MMX2 versions
#if HAVE_MMX2
#undef RENAME
#undef COMPILE_TEMPLATE_MMX2
#define COMPILE_TEMPLATE_MMX2 1
#define RENAME(a) a ## _MMX2
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
    int16_t *vLumFilterPos= c->vLumFilterPos;
    int16_t *vChrFilterPos= c->vChrFilterPos;
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
    if (c->dstFormat == PIX_FMT_RGB555 || c->dstFormat == PIX_FMT_BGR555)
        c->greenDither= ff_dither8[dstY&1];
    else
        c->greenDither= ff_dither4[dstY&1];
    c->redDither= ff_dither8[(dstY+1)&1];
    if (dstY < dstH - 2) {
        const int16_t **lumSrcPtr= (const int16_t **) lumPixBuf + lumBufIndex + firstLumSrcY - lastInLumBuf + vLumBufSize;
        const int16_t **chrUSrcPtr= (const int16_t **) chrUPixBuf + chrBufIndex + firstChrSrcY - lastInChrBuf + vChrBufSize;
        const int16_t **alpSrcPtr= (CONFIG_SWSCALE_ALPHA && alpPixBuf) ? (const int16_t **) alpPixBuf + lumBufIndex + firstLumSrcY - lastInLumBuf + vLumBufSize : NULL;
        int i;
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

#if HAVE_MMX2
static void yuv2yuvX_sse3(const int16_t *filter, int filterSize,
                           const int16_t **src, uint8_t *dest, int dstW,
                           const uint8_t *dither, int offset)
{
    if(((int)dest) & 15){
        return yuv2yuvX_MMX2(filter, filterSize, src, dest, dstW, dither, offset);
    }
    if (offset) {
        __asm__ volatile("movq       (%0), %%xmm3\n\t"
                         "movdqa    %%xmm3, %%xmm4\n\t"
                         "psrlq       $24, %%xmm3\n\t"
                         "psllq       $40, %%xmm4\n\t"
                         "por       %%xmm4, %%xmm3\n\t"
                         :: "r"(dither)
                         );
    } else {
        __asm__ volatile("movq       (%0), %%xmm3\n\t"
                         :: "r"(dither)
                         );
    }
    __asm__ volatile(
        "pxor      %%xmm0, %%xmm0\n\t"
        "punpcklbw %%xmm0, %%xmm3\n\t"
        "psraw        $4, %%xmm3\n\t"
        "movdqa    %%xmm3, %%xmm4\n\t"
        "movdqa    %%xmm3, %%xmm7\n\t"
        "movl %3, %%ecx\n\t"
        "mov                                 %0, %%"REG_d"  \n\t"\
        "mov                        (%%"REG_d"), %%"REG_S"  \n\t"\
        ".p2align                             4             \n\t" /* FIXME Unroll? */\
        "1:                                                 \n\t"\
        "movddup                  8(%%"REG_d"), %%xmm0      \n\t" /* filterCoeff */\
        "movdqa              (%%"REG_S", %%"REG_c", 2), %%xmm2      \n\t" /* srcData */\
        "movdqa            16(%%"REG_S", %%"REG_c", 2), %%xmm5      \n\t" /* srcData */\
        "add                                $16, %%"REG_d"  \n\t"\
        "mov                        (%%"REG_d"), %%"REG_S"  \n\t"\
        "test                         %%"REG_S", %%"REG_S"  \n\t"\
        "pmulhw                           %%xmm0, %%xmm2      \n\t"\
        "pmulhw                           %%xmm0, %%xmm5      \n\t"\
        "paddw                            %%xmm2, %%xmm3      \n\t"\
        "paddw                            %%xmm5, %%xmm4      \n\t"\
        " jnz                                1b             \n\t"\
        "psraw                               $3, %%xmm3      \n\t"\
        "psraw                               $3, %%xmm4      \n\t"\
        "packuswb                         %%xmm4, %%xmm3      \n\t"
        "movntdq                          %%xmm3, (%1, %%"REG_c")\n\t"
        "add                         $16, %%"REG_c"         \n\t"\
        "cmp                          %2, %%"REG_c"         \n\t"\
        "movdqa    %%xmm7, %%xmm3\n\t"
        "movdqa    %%xmm7, %%xmm4\n\t"
        "mov                                 %0, %%"REG_d"  \n\t"\
        "mov                        (%%"REG_d"), %%"REG_S"  \n\t"\
        "jb                                  1b             \n\t"\
        :: "g" (filter),
           "r" (dest-offset), "g" ((x86_reg)(dstW+offset)), "m" (offset)
        : "%"REG_d, "%"REG_S, "%"REG_c
    );
}
#endif

#define SCALE_FUNC(filter_n, from_bpc, to_bpc, opt) \
extern void ff_hscale ## from_bpc ## to ## to_bpc ## _ ## filter_n ## _ ## opt( \
                                                SwsContext *c, int16_t *data, \
                                                int dstW, const uint8_t *src, \
                                                const int16_t *filter, \
                                                const int16_t *filterPos, int filterSize);

#define SCALE_FUNCS(filter_n, opt) \
    SCALE_FUNC(filter_n,  8, 15, opt); \
    SCALE_FUNC(filter_n,  9, 15, opt); \
    SCALE_FUNC(filter_n, 10, 15, opt); \
    SCALE_FUNC(filter_n, 14, 15, opt); \
    SCALE_FUNC(filter_n, 16, 15, opt); \
    SCALE_FUNC(filter_n,  8, 19, opt); \
    SCALE_FUNC(filter_n,  9, 19, opt); \
    SCALE_FUNC(filter_n, 10, 19, opt); \
    SCALE_FUNC(filter_n, 14, 19, opt); \
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
extern void ff_yuv2planeX_ ## size ## _ ## opt(const int16_t *filter, int filterSize, \
                                               const int16_t **src, uint8_t *dest, int dstW, \
                                               const uint8_t *dither, int offset)
#define VSCALEX_FUNCS(opt1, opt2) \
    VSCALEX_FUNC(8,  opt1); \
    VSCALEX_FUNC(9,  opt2); \
    VSCALEX_FUNC(10, opt2)

#if ARCH_X86_32
VSCALEX_FUNCS(mmx,  mmx2);
#endif
VSCALEX_FUNCS(sse2, sse2);
VSCALEX_FUNCS(sse4, sse4);
VSCALEX_FUNC(16, sse4);
VSCALEX_FUNCS(avx,  avx);

#define VSCALE_FUNC(size, opt) \
extern void ff_yuv2plane1_ ## size ## _ ## opt(const int16_t *src, uint8_t *dst, int dstW, \
                                               const uint8_t *dither, int offset)
#define VSCALE_FUNCS(opt1, opt2) \
    VSCALE_FUNC(8,  opt1); \
    VSCALE_FUNC(9,  opt2); \
    VSCALE_FUNC(10, opt2); \
    VSCALE_FUNC(16, opt1)

#if ARCH_X86_32
VSCALE_FUNCS(mmx, mmx2);
#endif
VSCALE_FUNCS(sse2, sse2);
VSCALE_FUNC(16, sse4);
VSCALE_FUNCS(avx, avx);

void ff_sws_init_swScale_mmx(SwsContext *c)
{
    int cpu_flags = av_get_cpu_flags();

    if (cpu_flags & AV_CPU_FLAG_MMX)
        sws_init_swScale_MMX(c);
#if HAVE_MMX2
    if (cpu_flags & AV_CPU_FLAG_MMX2)
        sws_init_swScale_MMX2(c);
    if (cpu_flags & AV_CPU_FLAG_SSE3){
        if(c->use_mmx_vfilter && !(c->flags & SWS_ACCURATE_RND))
            c->yuv2planeX = yuv2yuvX_sse3;
    }
#endif

#if HAVE_YASM
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
    } else if (c->srcBpc == 14 || ((c->srcFormat==PIX_FMT_PAL8||isAnyRGB(c->srcFormat)) && av_pix_fmt_descriptors[c->srcFormat].comp[0].depth_minus1<15)) { \
        hscalefn = c->dstBpc <= 10 ? ff_hscale14to15_ ## filtersize ## _ ## opt2 : \
                                     ff_hscale14to19_ ## filtersize ## _ ## opt1; \
    } else { /* c->srcBpc == 16 */ \
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
#define ASSIGN_VSCALEX_FUNC(vscalefn, opt1, opt2, opt2chk, do_16_case) \
switch(c->dstBpc){ \
    case 16:                                     /*do_16_case;*/                           break; \
    case 10: if (!isBE(c->dstFormat) && opt2chk) /*vscalefn = ff_yuv2planeX_10_ ## opt2;*/ break; \
    case 9:  if (!isBE(c->dstFormat) && opt2chk) /*vscalefn = ff_yuv2planeX_9_  ## opt2;*/ break; \
    default:                                     /*vscalefn = ff_yuv2planeX_8_  ## opt1;*/ break; \
    }
#define ASSIGN_VSCALE_FUNC(vscalefn, opt1, opt2, opt2chk) \
    switch(c->dstBpc){ \
    case 16: if (!isBE(c->dstFormat))            vscalefn = ff_yuv2plane1_16_ ## opt1; break; \
    case 10: if (!isBE(c->dstFormat) && opt2chk) vscalefn = ff_yuv2plane1_10_ ## opt2; break; \
    case 9:  if (!isBE(c->dstFormat) && opt2chk) vscalefn = ff_yuv2plane1_9_  ## opt2;  break; \
    default:                                     vscalefn = ff_yuv2plane1_8_  ## opt1;  break; \
    }
#if ARCH_X86_32
    if (cpu_flags & AV_CPU_FLAG_MMX) {
        ASSIGN_MMX_SCALE_FUNC(c->hyScale, c->hLumFilterSize, mmx, mmx);
        ASSIGN_MMX_SCALE_FUNC(c->hcScale, c->hChrFilterSize, mmx, mmx);
        ASSIGN_VSCALEX_FUNC(c->yuv2planeX, mmx, mmx2, cpu_flags & AV_CPU_FLAG_MMX2,);
        ASSIGN_VSCALE_FUNC(c->yuv2plane1, mmx, mmx2, cpu_flags & AV_CPU_FLAG_MMX2);
    }
#endif
#define ASSIGN_SSE_SCALE_FUNC(hscalefn, filtersize, opt1, opt2) \
    switch (filtersize) { \
    case 4:  ASSIGN_SCALE_FUNC2(hscalefn, 4, opt1, opt2); break; \
    case 8:  ASSIGN_SCALE_FUNC2(hscalefn, 8, opt1, opt2); break; \
    default: if (filtersize & 4) ASSIGN_SCALE_FUNC2(hscalefn, X4, opt1, opt2); \
             else                ASSIGN_SCALE_FUNC2(hscalefn, X8, opt1, opt2); \
             break; \
    }
    if (cpu_flags & AV_CPU_FLAG_SSE2) {
        ASSIGN_SSE_SCALE_FUNC(c->hyScale, c->hLumFilterSize, sse2, sse2);
        ASSIGN_SSE_SCALE_FUNC(c->hcScale, c->hChrFilterSize, sse2, sse2);
        ASSIGN_VSCALEX_FUNC(c->yuv2planeX, sse2, sse2, 1,);
        ASSIGN_VSCALE_FUNC(c->yuv2plane1, sse2, sse2, 1);
    }
    if (cpu_flags & AV_CPU_FLAG_SSSE3) {
        ASSIGN_SSE_SCALE_FUNC(c->hyScale, c->hLumFilterSize, ssse3, ssse3);
        ASSIGN_SSE_SCALE_FUNC(c->hcScale, c->hChrFilterSize, ssse3, ssse3);
    }
    if (cpu_flags & AV_CPU_FLAG_SSE4) {
        /* Xto15 don't need special sse4 functions */
        ASSIGN_SSE_SCALE_FUNC(c->hyScale, c->hLumFilterSize, sse4, ssse3);
        ASSIGN_SSE_SCALE_FUNC(c->hcScale, c->hChrFilterSize, sse4, ssse3);
        ASSIGN_VSCALEX_FUNC(c->yuv2planeX, sse4, sse4, 1,
                            if (!isBE(c->dstFormat)) c->yuv2planeX = ff_yuv2planeX_16_sse4);
        if (c->dstBpc == 16 && !isBE(c->dstFormat))
            c->yuv2plane1 = ff_yuv2plane1_16_sse4;
    }

    if (cpu_flags & AV_CPU_FLAG_AVX) {
        ASSIGN_VSCALEX_FUNC(c->yuv2planeX, avx, avx, 1,);
        ASSIGN_VSCALE_FUNC(c->yuv2plane1, avx, avx, 1);
    }
#endif
}
