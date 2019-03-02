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

#include <stdint.h>

#include "libavutil/x86/asm.h"
#include "libswscale/swscale_internal.h"

#undef REAL_MOVNTQ
#undef MOVNTQ
#undef MOVNTQ2
#undef PREFETCH


#if COMPILE_TEMPLATE_MMXEXT
#define REAL_MOVNTQ(a,b) "movntq " #a ", " #b " \n\t"
#define MOVNTQ2 "movntq "
#else
#define REAL_MOVNTQ(a,b) "movq " #a ", " #b " \n\t"
#define MOVNTQ2 "movq "
#endif
#define MOVNTQ(a,b)  REAL_MOVNTQ(a,b)

#if !COMPILE_TEMPLATE_MMXEXT
static av_always_inline void
dither_8to16(const uint8_t *srcDither, int rot)
{
    if (rot) {
        __asm__ volatile("pxor      %%mm0, %%mm0\n\t"
                         "movq       (%0), %%mm3\n\t"
                         "movq      %%mm3, %%mm4\n\t"
                         "psrlq       $24, %%mm3\n\t"
                         "psllq       $40, %%mm4\n\t"
                         "por       %%mm4, %%mm3\n\t"
                         "movq      %%mm3, %%mm4\n\t"
                         "punpcklbw %%mm0, %%mm3\n\t"
                         "punpckhbw %%mm0, %%mm4\n\t"
                         :: "r"(srcDither)
                         );
    } else {
        __asm__ volatile("pxor      %%mm0, %%mm0\n\t"
                         "movq       (%0), %%mm3\n\t"
                         "movq      %%mm3, %%mm4\n\t"
                         "punpcklbw %%mm0, %%mm3\n\t"
                         "punpckhbw %%mm0, %%mm4\n\t"
                         :: "r"(srcDither)
                         );
    }
}
#endif

static void RENAME(yuv2yuvX)(const int16_t *filter, int filterSize,
                           const int16_t **src, uint8_t *dest, int dstW,
                           const uint8_t *dither, int offset)
{
    dither_8to16(dither, offset);
    filterSize--;
    __asm__ volatile(
        "movd %0, %%mm1\n\t"
        "punpcklwd %%mm1, %%mm1\n\t"
        "punpckldq %%mm1, %%mm1\n\t"
        "psllw        $3, %%mm1\n\t"
        "paddw     %%mm1, %%mm3\n\t"
        "paddw     %%mm1, %%mm4\n\t"
        "psraw        $4, %%mm3\n\t"
        "psraw        $4, %%mm4\n\t"
        ::"m"(filterSize)
     );

    __asm__ volatile(\
        "movq    %%mm3, %%mm6\n\t"
        "movq    %%mm4, %%mm7\n\t"
        "movl %3, %%ecx\n\t"
        "mov                                 %0, %%"FF_REG_d"       \n\t"\
        "mov                        (%%"FF_REG_d"), %%"FF_REG_S"    \n\t"\
        ".p2align                             4                     \n\t" /* FIXME Unroll? */\
        "1:                                                         \n\t"\
        "movq                      8(%%"FF_REG_d"), %%mm0           \n\t" /* filterCoeff */\
        "movq                (%%"FF_REG_S", %%"FF_REG_c", 2), %%mm2 \n\t" /* srcData */\
        "movq               8(%%"FF_REG_S", %%"FF_REG_c", 2), %%mm5 \n\t" /* srcData */\
        "add                                $16, %%"FF_REG_d"       \n\t"\
        "mov                        (%%"FF_REG_d"), %%"FF_REG_S"    \n\t"\
        "test                         %%"FF_REG_S", %%"FF_REG_S"    \n\t"\
        "pmulhw                           %%mm0, %%mm2      \n\t"\
        "pmulhw                           %%mm0, %%mm5      \n\t"\
        "paddw                            %%mm2, %%mm3      \n\t"\
        "paddw                            %%mm5, %%mm4      \n\t"\
        " jnz                                1b             \n\t"\
        "psraw                               $3, %%mm3      \n\t"\
        "psraw                               $3, %%mm4      \n\t"\
        "packuswb                         %%mm4, %%mm3      \n\t"
        MOVNTQ2 "                         %%mm3, (%1, %%"FF_REG_c")\n\t"
        "add                          $8, %%"FF_REG_c"      \n\t"\
        "cmp                          %2, %%"FF_REG_c"      \n\t"\
        "movq    %%mm6, %%mm3\n\t"
        "movq    %%mm7, %%mm4\n\t"
        "mov                                 %0, %%"FF_REG_d"     \n\t"\
        "mov                        (%%"FF_REG_d"), %%"FF_REG_S"  \n\t"\
        "jb                                  1b                   \n\t"\
        :: "g" (filter),
           "r" (dest-offset), "g" ((x86_reg)(dstW+offset)), "m" (offset)
        : "%"FF_REG_d, "%"FF_REG_S, "%"FF_REG_c
    );
}

#define YSCALEYUV2PACKEDX_UV \
    __asm__ volatile(\
        "xor                %%"FF_REG_a", %%"FF_REG_a"  \n\t"\
        ".p2align                      4                \n\t"\
        "nop                                            \n\t"\
        "1:                                             \n\t"\
        "lea "CHR_MMX_FILTER_OFFSET"(%0), %%"FF_REG_d"  \n\t"\
        "mov              (%%"FF_REG_d"), %%"FF_REG_S"  \n\t"\
        "movq      "VROUNDER_OFFSET"(%0), %%mm3         \n\t"\
        "movq                      %%mm3, %%mm4         \n\t"\
        ".p2align                      4                \n\t"\
        "2:                                             \n\t"\
        "movq            8(%%"FF_REG_d"), %%mm0         \n\t" /* filterCoeff */\
        "movq  (%%"FF_REG_S", %%"FF_REG_a"), %%mm2      \n\t" /* UsrcData */\
        "add                          %6, %%"FF_REG_S"  \n\t" \
        "movq  (%%"FF_REG_S", %%"FF_REG_a"), %%mm5      \n\t" /* VsrcData */\
        "add                         $16, %%"FF_REG_d"  \n\t"\
        "mov              (%%"FF_REG_d"), %%"FF_REG_S"  \n\t"\
        "pmulhw                    %%mm0, %%mm2         \n\t"\
        "pmulhw                    %%mm0, %%mm5         \n\t"\
        "paddw                     %%mm2, %%mm3         \n\t"\
        "paddw                     %%mm5, %%mm4         \n\t"\
        "test               %%"FF_REG_S", %%"FF_REG_S"  \n\t"\
        " jnz                         2b                \n\t"\

#define YSCALEYUV2PACKEDX_YA(offset,coeff,src1,src2,dst1,dst2) \
    "lea                "offset"(%0), %%"FF_REG_d"  \n\t"\
    "mov              (%%"FF_REG_d"), %%"FF_REG_S"  \n\t"\
    "movq      "VROUNDER_OFFSET"(%0), "#dst1"       \n\t"\
    "movq                    "#dst1", "#dst2"       \n\t"\
    ".p2align                      4                \n\t"\
    "2:                                             \n\t"\
    "movq            8(%%"FF_REG_d"), "#coeff"      \n\t" /* filterCoeff */\
    "movq  (%%"FF_REG_S", %%"FF_REG_a", 2), "#src1" \n\t" /* Y1srcData */\
    "movq 8(%%"FF_REG_S", %%"FF_REG_a", 2), "#src2" \n\t" /* Y2srcData */\
    "add                         $16, %%"FF_REG_d"  \n\t"\
    "mov              (%%"FF_REG_d"), %%"FF_REG_S"  \n\t"\
    "pmulhw                 "#coeff", "#src1"       \n\t"\
    "pmulhw                 "#coeff", "#src2"       \n\t"\
    "paddw                   "#src1", "#dst1"       \n\t"\
    "paddw                   "#src2", "#dst2"       \n\t"\
    "test               %%"FF_REG_S", %%"FF_REG_S"  \n\t"\
    " jnz                         2b                \n\t"\

#define YSCALEYUV2PACKEDX \
    YSCALEYUV2PACKEDX_UV \
    YSCALEYUV2PACKEDX_YA(LUM_MMX_FILTER_OFFSET,%%mm0,%%mm2,%%mm5,%%mm1,%%mm7) \

#define YSCALEYUV2PACKEDX_END                     \
        :: "r" (&c->redDither),                   \
            "m" (dummy), "m" (dummy), "m" (dummy),\
            "r" (dest), "m" (dstW_reg), "m"(uv_off) \
            NAMED_CONSTRAINTS_ADD(bF8,bFC) \
        : "%"FF_REG_a, "%"FF_REG_d, "%"FF_REG_S            \
    );

#define YSCALEYUV2PACKEDX_ACCURATE_UV \
    __asm__ volatile(\
        "xor %%"FF_REG_a", %%"FF_REG_a"                 \n\t"\
        ".p2align                      4                \n\t"\
        "nop                                            \n\t"\
        "1:                                             \n\t"\
        "lea "CHR_MMX_FILTER_OFFSET"(%0), %%"FF_REG_d"  \n\t"\
        "mov              (%%"FF_REG_d"), %%"FF_REG_S"  \n\t"\
        "pxor                      %%mm4, %%mm4         \n\t"\
        "pxor                      %%mm5, %%mm5         \n\t"\
        "pxor                      %%mm6, %%mm6         \n\t"\
        "pxor                      %%mm7, %%mm7         \n\t"\
        ".p2align                      4                \n\t"\
        "2:                                             \n\t"\
        "movq  (%%"FF_REG_S", %%"FF_REG_a"), %%mm0      \n\t" /* UsrcData */\
        "add                          %6, %%"FF_REG_S"  \n\t" \
        "movq  (%%"FF_REG_S", %%"FF_REG_a"), %%mm2      \n\t" /* VsrcData */\
        "mov "STR(APCK_PTR2)"(%%"FF_REG_d"), %%"FF_REG_S" \n\t"\
        "movq  (%%"FF_REG_S", %%"FF_REG_a"), %%mm1      \n\t" /* UsrcData */\
        "movq                      %%mm0, %%mm3         \n\t"\
        "punpcklwd                 %%mm1, %%mm0         \n\t"\
        "punpckhwd                 %%mm1, %%mm3         \n\t"\
        "movq "STR(APCK_COEF)"(%%"FF_REG_d"),%%mm1      \n\t" /* filterCoeff */\
        "pmaddwd                   %%mm1, %%mm0         \n\t"\
        "pmaddwd                   %%mm1, %%mm3         \n\t"\
        "paddd                     %%mm0, %%mm4         \n\t"\
        "paddd                     %%mm3, %%mm5         \n\t"\
        "add                          %6, %%"FF_REG_S"  \n\t" \
        "movq  (%%"FF_REG_S", %%"FF_REG_a"), %%mm3      \n\t" /* VsrcData */\
        "mov "STR(APCK_SIZE)"(%%"FF_REG_d"), %%"FF_REG_S" \n\t"\
        "add           $"STR(APCK_SIZE)", %%"FF_REG_d"  \n\t"\
        "test               %%"FF_REG_S", %%"FF_REG_S"  \n\t"\
        "movq                      %%mm2, %%mm0         \n\t"\
        "punpcklwd                 %%mm3, %%mm2         \n\t"\
        "punpckhwd                 %%mm3, %%mm0         \n\t"\
        "pmaddwd                   %%mm1, %%mm2         \n\t"\
        "pmaddwd                   %%mm1, %%mm0         \n\t"\
        "paddd                     %%mm2, %%mm6         \n\t"\
        "paddd                     %%mm0, %%mm7         \n\t"\
        " jnz                         2b                \n\t"\
        "psrad                       $16, %%mm4         \n\t"\
        "psrad                       $16, %%mm5         \n\t"\
        "psrad                       $16, %%mm6         \n\t"\
        "psrad                       $16, %%mm7         \n\t"\
        "movq      "VROUNDER_OFFSET"(%0), %%mm0         \n\t"\
        "packssdw                  %%mm5, %%mm4         \n\t"\
        "packssdw                  %%mm7, %%mm6         \n\t"\
        "paddw                     %%mm0, %%mm4         \n\t"\
        "paddw                     %%mm0, %%mm6         \n\t"\
        "movq                      %%mm4, "U_TEMP"(%0)  \n\t"\
        "movq                      %%mm6, "V_TEMP"(%0)  \n\t"\

#define YSCALEYUV2PACKEDX_ACCURATE_YA(offset) \
    "lea                "offset"(%0), %%"FF_REG_d"      \n\t"\
    "mov                 (%%"FF_REG_d"), %%"FF_REG_S"   \n\t"\
    "pxor                      %%mm1, %%mm1         \n\t"\
    "pxor                      %%mm5, %%mm5         \n\t"\
    "pxor                      %%mm7, %%mm7         \n\t"\
    "pxor                      %%mm6, %%mm6         \n\t"\
    ".p2align                      4                \n\t"\
    "2:                                             \n\t"\
    "movq  (%%"FF_REG_S", %%"FF_REG_a", 2), %%mm0       \n\t" /* Y1srcData */\
    "movq 8(%%"FF_REG_S", %%"FF_REG_a", 2), %%mm2       \n\t" /* Y2srcData */\
    "mov "STR(APCK_PTR2)"(%%"FF_REG_d"), %%"FF_REG_S"   \n\t"\
    "movq  (%%"FF_REG_S", %%"FF_REG_a", 2), %%mm4       \n\t" /* Y1srcData */\
    "movq                      %%mm0, %%mm3         \n\t"\
    "punpcklwd                 %%mm4, %%mm0         \n\t"\
    "punpckhwd                 %%mm4, %%mm3         \n\t"\
    "movq "STR(APCK_COEF)"(%%"FF_REG_d"), %%mm4     \n\t" /* filterCoeff */\
    "pmaddwd                   %%mm4, %%mm0         \n\t"\
    "pmaddwd                   %%mm4, %%mm3         \n\t"\
    "paddd                     %%mm0, %%mm1         \n\t"\
    "paddd                     %%mm3, %%mm5         \n\t"\
    "movq 8(%%"FF_REG_S", %%"FF_REG_a", 2), %%mm3   \n\t" /* Y2srcData */\
    "mov "STR(APCK_SIZE)"(%%"FF_REG_d"), %%"FF_REG_S" \n\t"\
    "add           $"STR(APCK_SIZE)", %%"FF_REG_d"  \n\t"\
    "test               %%"FF_REG_S", %%"FF_REG_S"  \n\t"\
    "movq                      %%mm2, %%mm0         \n\t"\
    "punpcklwd                 %%mm3, %%mm2         \n\t"\
    "punpckhwd                 %%mm3, %%mm0         \n\t"\
    "pmaddwd                   %%mm4, %%mm2         \n\t"\
    "pmaddwd                   %%mm4, %%mm0         \n\t"\
    "paddd                     %%mm2, %%mm7         \n\t"\
    "paddd                     %%mm0, %%mm6         \n\t"\
    " jnz                         2b                \n\t"\
    "psrad                       $16, %%mm1         \n\t"\
    "psrad                       $16, %%mm5         \n\t"\
    "psrad                       $16, %%mm7         \n\t"\
    "psrad                       $16, %%mm6         \n\t"\
    "movq      "VROUNDER_OFFSET"(%0), %%mm0         \n\t"\
    "packssdw                  %%mm5, %%mm1         \n\t"\
    "packssdw                  %%mm6, %%mm7         \n\t"\
    "paddw                     %%mm0, %%mm1         \n\t"\
    "paddw                     %%mm0, %%mm7         \n\t"\
    "movq               "U_TEMP"(%0), %%mm3         \n\t"\
    "movq               "V_TEMP"(%0), %%mm4         \n\t"\

#define YSCALEYUV2PACKEDX_ACCURATE \
    YSCALEYUV2PACKEDX_ACCURATE_UV \
    YSCALEYUV2PACKEDX_ACCURATE_YA(LUM_MMX_FILTER_OFFSET)

#define YSCALEYUV2RGBX \
    "psubw  "U_OFFSET"(%0), %%mm3       \n\t" /* (U-128)8*/\
    "psubw  "V_OFFSET"(%0), %%mm4       \n\t" /* (V-128)8*/\
    "movq            %%mm3, %%mm2       \n\t" /* (U-128)8*/\
    "movq            %%mm4, %%mm5       \n\t" /* (V-128)8*/\
    "pmulhw "UG_COEFF"(%0), %%mm3       \n\t"\
    "pmulhw "VG_COEFF"(%0), %%mm4       \n\t"\
    /* mm2=(U-128)8, mm3=ug, mm4=vg mm5=(V-128)8 */\
    "pmulhw "UB_COEFF"(%0), %%mm2       \n\t"\
    "pmulhw "VR_COEFF"(%0), %%mm5       \n\t"\
    "psubw  "Y_OFFSET"(%0), %%mm1       \n\t" /* 8(Y-16)*/\
    "psubw  "Y_OFFSET"(%0), %%mm7       \n\t" /* 8(Y-16)*/\
    "pmulhw  "Y_COEFF"(%0), %%mm1       \n\t"\
    "pmulhw  "Y_COEFF"(%0), %%mm7       \n\t"\
    /* mm1= Y1, mm2=ub, mm3=ug, mm4=vg mm5=vr, mm7=Y2 */\
    "paddw           %%mm3, %%mm4       \n\t"\
    "movq            %%mm2, %%mm0       \n\t"\
    "movq            %%mm5, %%mm6       \n\t"\
    "movq            %%mm4, %%mm3       \n\t"\
    "punpcklwd       %%mm2, %%mm2       \n\t"\
    "punpcklwd       %%mm5, %%mm5       \n\t"\
    "punpcklwd       %%mm4, %%mm4       \n\t"\
    "paddw           %%mm1, %%mm2       \n\t"\
    "paddw           %%mm1, %%mm5       \n\t"\
    "paddw           %%mm1, %%mm4       \n\t"\
    "punpckhwd       %%mm0, %%mm0       \n\t"\
    "punpckhwd       %%mm6, %%mm6       \n\t"\
    "punpckhwd       %%mm3, %%mm3       \n\t"\
    "paddw           %%mm7, %%mm0       \n\t"\
    "paddw           %%mm7, %%mm6       \n\t"\
    "paddw           %%mm7, %%mm3       \n\t"\
    /* mm0=B1, mm2=B2, mm3=G2, mm4=G1, mm5=R1, mm6=R2 */\
    "packuswb        %%mm0, %%mm2       \n\t"\
    "packuswb        %%mm6, %%mm5       \n\t"\
    "packuswb        %%mm3, %%mm4       \n\t"\

#define REAL_WRITEBGR32(dst, dstw, index, b, g, r, a, q0, q2, q3, t) \
    "movq       "#b", "#q2"     \n\t" /* B */\
    "movq       "#r", "#t"      \n\t" /* R */\
    "punpcklbw  "#g", "#b"      \n\t" /* GBGBGBGB 0 */\
    "punpcklbw  "#a", "#r"      \n\t" /* ARARARAR 0 */\
    "punpckhbw  "#g", "#q2"     \n\t" /* GBGBGBGB 2 */\
    "punpckhbw  "#a", "#t"      \n\t" /* ARARARAR 2 */\
    "movq       "#b", "#q0"     \n\t" /* GBGBGBGB 0 */\
    "movq      "#q2", "#q3"     \n\t" /* GBGBGBGB 2 */\
    "punpcklwd  "#r", "#q0"     \n\t" /* ARGBARGB 0 */\
    "punpckhwd  "#r", "#b"      \n\t" /* ARGBARGB 1 */\
    "punpcklwd  "#t", "#q2"     \n\t" /* ARGBARGB 2 */\
    "punpckhwd  "#t", "#q3"     \n\t" /* ARGBARGB 3 */\
\
    MOVNTQ(   q0,   (dst, index, 4))\
    MOVNTQ(    b,  8(dst, index, 4))\
    MOVNTQ(   q2, 16(dst, index, 4))\
    MOVNTQ(   q3, 24(dst, index, 4))\
\
    "add      $8, "#index"      \n\t"\
    "cmp  "dstw", "#index"      \n\t"\
    " jb      1b                \n\t"
#define WRITEBGR32(dst, dstw, index, b, g, r, a, q0, q2, q3, t)  REAL_WRITEBGR32(dst, dstw, index, b, g, r, a, q0, q2, q3, t)

static void RENAME(yuv2rgb32_X_ar)(SwsContext *c, const int16_t *lumFilter,
                                   const int16_t **lumSrc, int lumFilterSize,
                                   const int16_t *chrFilter, const int16_t **chrUSrc,
                                   const int16_t **chrVSrc,
                                   int chrFilterSize, const int16_t **alpSrc,
                                   uint8_t *dest, int dstW, int dstY)
{
    x86_reg dummy=0;
    x86_reg dstW_reg = dstW;
    x86_reg uv_off = c->uv_offx2;

    if (CONFIG_SWSCALE_ALPHA && c->needAlpha) {
        YSCALEYUV2PACKEDX_ACCURATE
        YSCALEYUV2RGBX
        "movq                      %%mm2, "U_TEMP"(%0)  \n\t"
        "movq                      %%mm4, "V_TEMP"(%0)  \n\t"
        "movq                      %%mm5, "Y_TEMP"(%0)  \n\t"
        YSCALEYUV2PACKEDX_ACCURATE_YA(ALP_MMX_FILTER_OFFSET)
        "movq               "Y_TEMP"(%0), %%mm5         \n\t"
        "psraw                        $3, %%mm1         \n\t"
        "psraw                        $3, %%mm7         \n\t"
        "packuswb                  %%mm7, %%mm1         \n\t"
        WRITEBGR32(%4, "%5", %%FF_REGa, %%mm3, %%mm4, %%mm5, %%mm1, %%mm0, %%mm7, %%mm2, %%mm6)
        YSCALEYUV2PACKEDX_END
    } else {
        YSCALEYUV2PACKEDX_ACCURATE
        YSCALEYUV2RGBX
        "pcmpeqd %%mm7, %%mm7 \n\t"
        WRITEBGR32(%4, "%5", %%FF_REGa, %%mm2, %%mm4, %%mm5, %%mm7, %%mm0, %%mm1, %%mm3, %%mm6)
        YSCALEYUV2PACKEDX_END
    }
}

static void RENAME(yuv2rgb32_X)(SwsContext *c, const int16_t *lumFilter,
                                const int16_t **lumSrc, int lumFilterSize,
                                const int16_t *chrFilter, const int16_t **chrUSrc,
                                const int16_t **chrVSrc,
                                int chrFilterSize, const int16_t **alpSrc,
                                uint8_t *dest, int dstW, int dstY)
{
    x86_reg dummy=0;
    x86_reg dstW_reg = dstW;
    x86_reg uv_off = c->uv_offx2;

    if (CONFIG_SWSCALE_ALPHA && c->needAlpha) {
        YSCALEYUV2PACKEDX
        YSCALEYUV2RGBX
        YSCALEYUV2PACKEDX_YA(ALP_MMX_FILTER_OFFSET, %%mm0, %%mm3, %%mm6, %%mm1, %%mm7)
        "psraw                        $3, %%mm1         \n\t"
        "psraw                        $3, %%mm7         \n\t"
        "packuswb                  %%mm7, %%mm1         \n\t"
        WRITEBGR32(%4, "%5", %%FF_REGa, %%mm2, %%mm4, %%mm5, %%mm1, %%mm0, %%mm7, %%mm3, %%mm6)
        YSCALEYUV2PACKEDX_END
    } else {
        YSCALEYUV2PACKEDX
        YSCALEYUV2RGBX
        "pcmpeqd %%mm7, %%mm7 \n\t"
        WRITEBGR32(%4, "%5", %%FF_REGa, %%mm2, %%mm4, %%mm5, %%mm7, %%mm0, %%mm1, %%mm3, %%mm6)
        YSCALEYUV2PACKEDX_END
    }
}

static void RENAME(yuv2bgr32_X)(SwsContext *c, const int16_t *lumFilter,
                                const int16_t **lumSrc, int lumFilterSize,
                                const int16_t *chrFilter, const int16_t **chrUSrc,
                                const int16_t **chrVSrc,
                                int chrFilterSize, const int16_t **alpSrc,
                                uint8_t *dest, int dstW, int dstY)
{
    x86_reg dummy=0;
    x86_reg dstW_reg = dstW;
    x86_reg uv_off = c->uv_offx2;

    if (CONFIG_SWSCALE_ALPHA && c->needAlpha) {
        YSCALEYUV2PACKEDX
        YSCALEYUV2RGBX
        YSCALEYUV2PACKEDX_YA(ALP_MMX_FILTER_OFFSET, %%mm0, %%mm3, %%mm6, %%mm1, %%mm7)
        "psraw                        $3, %%mm1         \n\t"
        "psraw                        $3, %%mm7         \n\t"
        "packuswb                  %%mm7, %%mm1         \n\t"
        WRITEBGR32(%4, "%5", %%FF_REGa, %%mm5, %%mm4, %%mm2, %%mm1, %%mm0, %%mm7, %%mm3, %%mm6)
        YSCALEYUV2PACKEDX_END
    } else {
        YSCALEYUV2PACKEDX
        YSCALEYUV2RGBX
        "pcmpeqd %%mm7, %%mm7 \n\t"
        WRITEBGR32(%4, "%5", %%FF_REGa, %%mm5, %%mm4, %%mm2, %%mm7, %%mm0, %%mm1, %%mm3, %%mm6)
        YSCALEYUV2PACKEDX_END
    }
}

#define REAL_WRITERGB16(dst, dstw, index) \
    "pand "MANGLE(bF8)", %%mm2  \n\t" /* B */\
    "pand "MANGLE(bFC)", %%mm4  \n\t" /* G */\
    "pand "MANGLE(bF8)", %%mm5  \n\t" /* R */\
    "psrlq           $3, %%mm2  \n\t"\
\
    "movq         %%mm2, %%mm1  \n\t"\
    "movq         %%mm4, %%mm3  \n\t"\
\
    "punpcklbw    %%mm7, %%mm3  \n\t"\
    "punpcklbw    %%mm5, %%mm2  \n\t"\
    "punpckhbw    %%mm7, %%mm4  \n\t"\
    "punpckhbw    %%mm5, %%mm1  \n\t"\
\
    "psllq           $3, %%mm3  \n\t"\
    "psllq           $3, %%mm4  \n\t"\
\
    "por          %%mm3, %%mm2  \n\t"\
    "por          %%mm4, %%mm1  \n\t"\
\
    MOVNTQ(%%mm2,  (dst, index, 2))\
    MOVNTQ(%%mm1, 8(dst, index, 2))\
\
    "add             $8, "#index"   \n\t"\
    "cmp         "dstw", "#index"   \n\t"\
    " jb             1b             \n\t"
#define WRITERGB16(dst, dstw, index)  REAL_WRITERGB16(dst, dstw, index)

static void RENAME(yuv2rgb565_X_ar)(SwsContext *c, const int16_t *lumFilter,
                                    const int16_t **lumSrc, int lumFilterSize,
                                    const int16_t *chrFilter, const int16_t **chrUSrc,
                                    const int16_t **chrVSrc,
                                    int chrFilterSize, const int16_t **alpSrc,
                                    uint8_t *dest, int dstW, int dstY)
{
    x86_reg dummy=0;
    x86_reg dstW_reg = dstW;
    x86_reg uv_off = c->uv_offx2;

    YSCALEYUV2PACKEDX_ACCURATE
    YSCALEYUV2RGBX
    "pxor %%mm7, %%mm7 \n\t"
    /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
    "paddusb "BLUE_DITHER"(%0), %%mm2\n\t"
    "paddusb "GREEN_DITHER"(%0), %%mm4\n\t"
    "paddusb "RED_DITHER"(%0), %%mm5\n\t"
#endif
    WRITERGB16(%4, "%5", %%FF_REGa)
    YSCALEYUV2PACKEDX_END
}

static void RENAME(yuv2rgb565_X)(SwsContext *c, const int16_t *lumFilter,
                                 const int16_t **lumSrc, int lumFilterSize,
                                 const int16_t *chrFilter, const int16_t **chrUSrc,
                                 const int16_t **chrVSrc,
                                 int chrFilterSize, const int16_t **alpSrc,
                                 uint8_t *dest, int dstW, int dstY)
{
    x86_reg dummy=0;
    x86_reg dstW_reg = dstW;
    x86_reg uv_off = c->uv_offx2;

    YSCALEYUV2PACKEDX
    YSCALEYUV2RGBX
    "pxor %%mm7, %%mm7 \n\t"
    /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
    "paddusb "BLUE_DITHER"(%0), %%mm2  \n\t"
    "paddusb "GREEN_DITHER"(%0), %%mm4  \n\t"
    "paddusb "RED_DITHER"(%0), %%mm5  \n\t"
#endif
    WRITERGB16(%4, "%5", %%FF_REGa)
    YSCALEYUV2PACKEDX_END
}

#define REAL_WRITERGB15(dst, dstw, index) \
    "pand "MANGLE(bF8)", %%mm2  \n\t" /* B */\
    "pand "MANGLE(bF8)", %%mm4  \n\t" /* G */\
    "pand "MANGLE(bF8)", %%mm5  \n\t" /* R */\
    "psrlq           $3, %%mm2  \n\t"\
    "psrlq           $1, %%mm5  \n\t"\
\
    "movq         %%mm2, %%mm1  \n\t"\
    "movq         %%mm4, %%mm3  \n\t"\
\
    "punpcklbw    %%mm7, %%mm3  \n\t"\
    "punpcklbw    %%mm5, %%mm2  \n\t"\
    "punpckhbw    %%mm7, %%mm4  \n\t"\
    "punpckhbw    %%mm5, %%mm1  \n\t"\
\
    "psllq           $2, %%mm3  \n\t"\
    "psllq           $2, %%mm4  \n\t"\
\
    "por          %%mm3, %%mm2  \n\t"\
    "por          %%mm4, %%mm1  \n\t"\
\
    MOVNTQ(%%mm2,  (dst, index, 2))\
    MOVNTQ(%%mm1, 8(dst, index, 2))\
\
    "add             $8, "#index"   \n\t"\
    "cmp         "dstw", "#index"   \n\t"\
    " jb             1b             \n\t"
#define WRITERGB15(dst, dstw, index)  REAL_WRITERGB15(dst, dstw, index)

static void RENAME(yuv2rgb555_X_ar)(SwsContext *c, const int16_t *lumFilter,
                                    const int16_t **lumSrc, int lumFilterSize,
                                    const int16_t *chrFilter, const int16_t **chrUSrc,
                                    const int16_t **chrVSrc,
                                    int chrFilterSize, const int16_t **alpSrc,
                                    uint8_t *dest, int dstW, int dstY)
{
    x86_reg dummy=0;
    x86_reg dstW_reg = dstW;
    x86_reg uv_off = c->uv_offx2;

    YSCALEYUV2PACKEDX_ACCURATE
    YSCALEYUV2RGBX
    "pxor %%mm7, %%mm7 \n\t"
    /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
    "paddusb "BLUE_DITHER"(%0), %%mm2\n\t"
    "paddusb "GREEN_DITHER"(%0), %%mm4\n\t"
    "paddusb "RED_DITHER"(%0), %%mm5\n\t"
#endif
    WRITERGB15(%4, "%5", %%FF_REGa)
    YSCALEYUV2PACKEDX_END
}

static void RENAME(yuv2rgb555_X)(SwsContext *c, const int16_t *lumFilter,
                                 const int16_t **lumSrc, int lumFilterSize,
                                 const int16_t *chrFilter, const int16_t **chrUSrc,
                                 const int16_t **chrVSrc,
                                 int chrFilterSize, const int16_t **alpSrc,
                                 uint8_t *dest, int dstW, int dstY)
{
    x86_reg dummy=0;
    x86_reg dstW_reg = dstW;
    x86_reg uv_off = c->uv_offx2;

    YSCALEYUV2PACKEDX
    YSCALEYUV2RGBX
    "pxor %%mm7, %%mm7 \n\t"
    /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
    "paddusb "BLUE_DITHER"(%0), %%mm2  \n\t"
    "paddusb "GREEN_DITHER"(%0), %%mm4  \n\t"
    "paddusb "RED_DITHER"(%0), %%mm5  \n\t"
#endif
    WRITERGB15(%4, "%5", %%FF_REGa)
    YSCALEYUV2PACKEDX_END
}

#define WRITEBGR24MMX(dst, dstw, index) \
    /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */\
    "movq      %%mm2, %%mm1     \n\t" /* B */\
    "movq      %%mm5, %%mm6     \n\t" /* R */\
    "punpcklbw %%mm4, %%mm2     \n\t" /* GBGBGBGB 0 */\
    "punpcklbw %%mm7, %%mm5     \n\t" /* 0R0R0R0R 0 */\
    "punpckhbw %%mm4, %%mm1     \n\t" /* GBGBGBGB 2 */\
    "punpckhbw %%mm7, %%mm6     \n\t" /* 0R0R0R0R 2 */\
    "movq      %%mm2, %%mm0     \n\t" /* GBGBGBGB 0 */\
    "movq      %%mm1, %%mm3     \n\t" /* GBGBGBGB 2 */\
    "punpcklwd %%mm5, %%mm0     \n\t" /* 0RGB0RGB 0 */\
    "punpckhwd %%mm5, %%mm2     \n\t" /* 0RGB0RGB 1 */\
    "punpcklwd %%mm6, %%mm1     \n\t" /* 0RGB0RGB 2 */\
    "punpckhwd %%mm6, %%mm3     \n\t" /* 0RGB0RGB 3 */\
\
    "movq      %%mm0, %%mm4     \n\t" /* 0RGB0RGB 0 */\
    "movq      %%mm2, %%mm6     \n\t" /* 0RGB0RGB 1 */\
    "movq      %%mm1, %%mm5     \n\t" /* 0RGB0RGB 2 */\
    "movq      %%mm3, %%mm7     \n\t" /* 0RGB0RGB 3 */\
\
    "psllq       $40, %%mm0     \n\t" /* RGB00000 0 */\
    "psllq       $40, %%mm2     \n\t" /* RGB00000 1 */\
    "psllq       $40, %%mm1     \n\t" /* RGB00000 2 */\
    "psllq       $40, %%mm3     \n\t" /* RGB00000 3 */\
\
    "punpckhdq %%mm4, %%mm0     \n\t" /* 0RGBRGB0 0 */\
    "punpckhdq %%mm6, %%mm2     \n\t" /* 0RGBRGB0 1 */\
    "punpckhdq %%mm5, %%mm1     \n\t" /* 0RGBRGB0 2 */\
    "punpckhdq %%mm7, %%mm3     \n\t" /* 0RGBRGB0 3 */\
\
    "psrlq        $8, %%mm0     \n\t" /* 00RGBRGB 0 */\
    "movq      %%mm2, %%mm6     \n\t" /* 0RGBRGB0 1 */\
    "psllq       $40, %%mm2     \n\t" /* GB000000 1 */\
    "por       %%mm2, %%mm0     \n\t" /* GBRGBRGB 0 */\
    MOVNTQ(%%mm0, (dst))\
\
    "psrlq       $24, %%mm6     \n\t" /* 0000RGBR 1 */\
    "movq      %%mm1, %%mm5     \n\t" /* 0RGBRGB0 2 */\
    "psllq       $24, %%mm1     \n\t" /* BRGB0000 2 */\
    "por       %%mm1, %%mm6     \n\t" /* BRGBRGBR 1 */\
    MOVNTQ(%%mm6, 8(dst))\
\
    "psrlq       $40, %%mm5     \n\t" /* 000000RG 2 */\
    "psllq        $8, %%mm3     \n\t" /* RGBRGB00 3 */\
    "por       %%mm3, %%mm5     \n\t" /* RGBRGBRG 2 */\
    MOVNTQ(%%mm5, 16(dst))\
\
    "add         $24, "#dst"    \n\t"\
\
    "add          $8, "#index"  \n\t"\
    "cmp      "dstw", "#index"  \n\t"\
    " jb          1b            \n\t"

#define WRITEBGR24MMXEXT(dst, dstw, index) \
    /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */\
    "movq "MANGLE(ff_M24A)", %%mm0 \n\t"\
    "movq "MANGLE(ff_M24C)", %%mm7 \n\t"\
    "pshufw $0x50, %%mm2, %%mm1 \n\t" /* B3 B2 B3 B2  B1 B0 B1 B0 */\
    "pshufw $0x50, %%mm4, %%mm3 \n\t" /* G3 G2 G3 G2  G1 G0 G1 G0 */\
    "pshufw $0x00, %%mm5, %%mm6 \n\t" /* R1 R0 R1 R0  R1 R0 R1 R0 */\
\
    "pand   %%mm0, %%mm1        \n\t" /*    B2        B1       B0 */\
    "pand   %%mm0, %%mm3        \n\t" /*    G2        G1       G0 */\
    "pand   %%mm7, %%mm6        \n\t" /*       R1        R0       */\
\
    "psllq     $8, %%mm3        \n\t" /* G2        G1       G0    */\
    "por    %%mm1, %%mm6        \n\t"\
    "por    %%mm3, %%mm6        \n\t"\
    MOVNTQ(%%mm6, (dst))\
\
    "psrlq     $8, %%mm4        \n\t" /* 00 G7 G6 G5  G4 G3 G2 G1 */\
    "pshufw $0xA5, %%mm2, %%mm1 \n\t" /* B5 B4 B5 B4  B3 B2 B3 B2 */\
    "pshufw $0x55, %%mm4, %%mm3 \n\t" /* G4 G3 G4 G3  G4 G3 G4 G3 */\
    "pshufw $0xA5, %%mm5, %%mm6 \n\t" /* R5 R4 R5 R4  R3 R2 R3 R2 */\
\
    "pand "MANGLE(ff_M24B)", %%mm1 \n\t" /* B5       B4        B3    */\
    "pand   %%mm7, %%mm3        \n\t" /*       G4        G3       */\
    "pand   %%mm0, %%mm6        \n\t" /*    R4        R3       R2 */\
\
    "por    %%mm1, %%mm3        \n\t" /* B5    G4 B4     G3 B3    */\
    "por    %%mm3, %%mm6        \n\t"\
    MOVNTQ(%%mm6, 8(dst))\
\
    "pshufw $0xFF, %%mm2, %%mm1 \n\t" /* B7 B6 B7 B6  B7 B6 B6 B7 */\
    "pshufw $0xFA, %%mm4, %%mm3 \n\t" /* 00 G7 00 G7  G6 G5 G6 G5 */\
    "pshufw $0xFA, %%mm5, %%mm6 \n\t" /* R7 R6 R7 R6  R5 R4 R5 R4 */\
\
    "pand   %%mm7, %%mm1        \n\t" /*       B7        B6       */\
    "pand   %%mm0, %%mm3        \n\t" /*    G7        G6       G5 */\
    "pand "MANGLE(ff_M24B)", %%mm6 \n\t" /* R7       R6        R5    */\
\
    "por    %%mm1, %%mm3        \n\t"\
    "por    %%mm3, %%mm6        \n\t"\
    MOVNTQ(%%mm6, 16(dst))\
\
    "add      $24, "#dst"       \n\t"\
\
    "add       $8, "#index"     \n\t"\
    "cmp   "dstw", "#index"     \n\t"\
    " jb       1b               \n\t"

#if COMPILE_TEMPLATE_MMXEXT
#undef WRITEBGR24
#define WRITEBGR24(dst, dstw, index)  WRITEBGR24MMXEXT(dst, dstw, index)
#else
#undef WRITEBGR24
#define WRITEBGR24(dst, dstw, index)  WRITEBGR24MMX(dst, dstw, index)
#endif

#if HAVE_6REGS
static void RENAME(yuv2bgr24_X_ar)(SwsContext *c, const int16_t *lumFilter,
                                   const int16_t **lumSrc, int lumFilterSize,
                                   const int16_t *chrFilter, const int16_t **chrUSrc,
                                   const int16_t **chrVSrc,
                                   int chrFilterSize, const int16_t **alpSrc,
                                   uint8_t *dest, int dstW, int dstY)
{
    x86_reg dummy=0;
    x86_reg dstW_reg = dstW;
    x86_reg uv_off = c->uv_offx2;

    YSCALEYUV2PACKEDX_ACCURATE
    YSCALEYUV2RGBX
    "pxor %%mm7, %%mm7 \n\t"
    "lea (%%"FF_REG_a", %%"FF_REG_a", 2), %%"FF_REG_c"\n\t" //FIXME optimize
    "add %4, %%"FF_REG_c"                        \n\t"
    WRITEBGR24(%%FF_REGc, "%5", %%FF_REGa)
    :: "r" (&c->redDither),
       "m" (dummy), "m" (dummy), "m" (dummy),
       "r" (dest), "m" (dstW_reg), "m"(uv_off)
       NAMED_CONSTRAINTS_ADD(ff_M24A,ff_M24C,ff_M24B)
    : "%"FF_REG_a, "%"FF_REG_c, "%"FF_REG_d, "%"FF_REG_S
    );
}

static void RENAME(yuv2bgr24_X)(SwsContext *c, const int16_t *lumFilter,
                                const int16_t **lumSrc, int lumFilterSize,
                                const int16_t *chrFilter, const int16_t **chrUSrc,
                                const int16_t **chrVSrc,
                                int chrFilterSize, const int16_t **alpSrc,
                                uint8_t *dest, int dstW, int dstY)
{
    x86_reg dummy=0;
    x86_reg dstW_reg = dstW;
    x86_reg uv_off = c->uv_offx2;

    YSCALEYUV2PACKEDX
    YSCALEYUV2RGBX
    "pxor                    %%mm7, %%mm7              \n\t"
    "lea (%%"FF_REG_a", %%"FF_REG_a", 2), %%"FF_REG_c" \n\t" //FIXME optimize
    "add                        %4, %%"FF_REG_c"       \n\t"
    WRITEBGR24(%%FF_REGc, "%5", %%FF_REGa)
    :: "r" (&c->redDither),
       "m" (dummy), "m" (dummy), "m" (dummy),
       "r" (dest),  "m" (dstW_reg), "m"(uv_off)
       NAMED_CONSTRAINTS_ADD(ff_M24A,ff_M24C,ff_M24B)
    : "%"FF_REG_a, "%"FF_REG_c, "%"FF_REG_d, "%"FF_REG_S
    );
}
#endif /* HAVE_6REGS */

#define REAL_WRITEYUY2(dst, dstw, index) \
    "packuswb  %%mm3, %%mm3     \n\t"\
    "packuswb  %%mm4, %%mm4     \n\t"\
    "packuswb  %%mm7, %%mm1     \n\t"\
    "punpcklbw %%mm4, %%mm3     \n\t"\
    "movq      %%mm1, %%mm7     \n\t"\
    "punpcklbw %%mm3, %%mm1     \n\t"\
    "punpckhbw %%mm3, %%mm7     \n\t"\
\
    MOVNTQ(%%mm1, (dst, index, 2))\
    MOVNTQ(%%mm7, 8(dst, index, 2))\
\
    "add          $8, "#index"  \n\t"\
    "cmp      "dstw", "#index"  \n\t"\
    " jb          1b            \n\t"
#define WRITEYUY2(dst, dstw, index)  REAL_WRITEYUY2(dst, dstw, index)

static void RENAME(yuv2yuyv422_X_ar)(SwsContext *c, const int16_t *lumFilter,
                                     const int16_t **lumSrc, int lumFilterSize,
                                     const int16_t *chrFilter, const int16_t **chrUSrc,
                                     const int16_t **chrVSrc,
                                     int chrFilterSize, const int16_t **alpSrc,
                                     uint8_t *dest, int dstW, int dstY)
{
    x86_reg dummy=0;
    x86_reg dstW_reg = dstW;
    x86_reg uv_off = c->uv_offx2;

    YSCALEYUV2PACKEDX_ACCURATE
    /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
    "psraw $3, %%mm3    \n\t"
    "psraw $3, %%mm4    \n\t"
    "psraw $3, %%mm1    \n\t"
    "psraw $3, %%mm7    \n\t"
    WRITEYUY2(%4, "%5", %%FF_REGa)
    YSCALEYUV2PACKEDX_END
}

static void RENAME(yuv2yuyv422_X)(SwsContext *c, const int16_t *lumFilter,
                                  const int16_t **lumSrc, int lumFilterSize,
                                  const int16_t *chrFilter, const int16_t **chrUSrc,
                                  const int16_t **chrVSrc,
                                  int chrFilterSize, const int16_t **alpSrc,
                                  uint8_t *dest, int dstW, int dstY)
{
    x86_reg dummy=0;
    x86_reg dstW_reg = dstW;
    x86_reg uv_off = c->uv_offx2;

    YSCALEYUV2PACKEDX
    /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
    "psraw $3, %%mm3    \n\t"
    "psraw $3, %%mm4    \n\t"
    "psraw $3, %%mm1    \n\t"
    "psraw $3, %%mm7    \n\t"
    WRITEYUY2(%4, "%5", %%FF_REGa)
    YSCALEYUV2PACKEDX_END
}

#define REAL_YSCALEYUV2RGB_UV(index, c) \
    "xor            "#index", "#index"  \n\t"\
    ".p2align              4            \n\t"\
    "1:                                 \n\t"\
    "movq     (%2, "#index"), %%mm2     \n\t" /* uvbuf0[eax]*/\
    "movq     (%3, "#index"), %%mm3     \n\t" /* uvbuf1[eax]*/\
    "add "UV_OFF_BYTE"("#c"), "#index"  \n\t" \
    "movq     (%2, "#index"), %%mm5     \n\t" /* uvbuf0[eax+2048]*/\
    "movq     (%3, "#index"), %%mm4     \n\t" /* uvbuf1[eax+2048]*/\
    "sub "UV_OFF_BYTE"("#c"), "#index"  \n\t" \
    "psubw             %%mm3, %%mm2     \n\t" /* uvbuf0[eax] - uvbuf1[eax]*/\
    "psubw             %%mm4, %%mm5     \n\t" /* uvbuf0[eax+2048] - uvbuf1[eax+2048]*/\
    "movq "CHR_MMX_FILTER_OFFSET"+8("#c"), %%mm0    \n\t"\
    "pmulhw            %%mm0, %%mm2     \n\t" /* (uvbuf0[eax] - uvbuf1[eax])uvalpha1>>16*/\
    "pmulhw            %%mm0, %%mm5     \n\t" /* (uvbuf0[eax+2048] - uvbuf1[eax+2048])uvalpha1>>16*/\
    "psraw                $4, %%mm3     \n\t" /* uvbuf0[eax] - uvbuf1[eax] >>4*/\
    "psraw                $4, %%mm4     \n\t" /* uvbuf0[eax+2048] - uvbuf1[eax+2048] >>4*/\
    "paddw             %%mm2, %%mm3     \n\t" /* uvbuf0[eax]uvalpha1 - uvbuf1[eax](1-uvalpha1)*/\
    "paddw             %%mm5, %%mm4     \n\t" /* uvbuf0[eax+2048]uvalpha1 - uvbuf1[eax+2048](1-uvalpha1)*/\
    "psubw  "U_OFFSET"("#c"), %%mm3     \n\t" /* (U-128)8*/\
    "psubw  "V_OFFSET"("#c"), %%mm4     \n\t" /* (V-128)8*/\
    "movq              %%mm3, %%mm2     \n\t" /* (U-128)8*/\
    "movq              %%mm4, %%mm5     \n\t" /* (V-128)8*/\
    "pmulhw "UG_COEFF"("#c"), %%mm3     \n\t"\
    "pmulhw "VG_COEFF"("#c"), %%mm4     \n\t"\
    /* mm2=(U-128)8, mm3=ug, mm4=vg mm5=(V-128)8 */\

#define REAL_YSCALEYUV2RGB_YA(index, c, b1, b2) \
    "movq  ("#b1", "#index", 2), %%mm0     \n\t" /*buf0[eax]*/\
    "movq  ("#b2", "#index", 2), %%mm1     \n\t" /*buf1[eax]*/\
    "movq 8("#b1", "#index", 2), %%mm6     \n\t" /*buf0[eax]*/\
    "movq 8("#b2", "#index", 2), %%mm7     \n\t" /*buf1[eax]*/\
    "psubw             %%mm1, %%mm0     \n\t" /* buf0[eax] - buf1[eax]*/\
    "psubw             %%mm7, %%mm6     \n\t" /* buf0[eax] - buf1[eax]*/\
    "pmulhw "LUM_MMX_FILTER_OFFSET"+8("#c"), %%mm0  \n\t" /* (buf0[eax] - buf1[eax])yalpha1>>16*/\
    "pmulhw "LUM_MMX_FILTER_OFFSET"+8("#c"), %%mm6  \n\t" /* (buf0[eax] - buf1[eax])yalpha1>>16*/\
    "psraw                $4, %%mm1     \n\t" /* buf0[eax] - buf1[eax] >>4*/\
    "psraw                $4, %%mm7     \n\t" /* buf0[eax] - buf1[eax] >>4*/\
    "paddw             %%mm0, %%mm1     \n\t" /* buf0[eax]yalpha1 + buf1[eax](1-yalpha1) >>16*/\
    "paddw             %%mm6, %%mm7     \n\t" /* buf0[eax]yalpha1 + buf1[eax](1-yalpha1) >>16*/\

#define REAL_YSCALEYUV2RGB_COEFF(c) \
    "pmulhw "UB_COEFF"("#c"), %%mm2     \n\t"\
    "pmulhw "VR_COEFF"("#c"), %%mm5     \n\t"\
    "psubw  "Y_OFFSET"("#c"), %%mm1     \n\t" /* 8(Y-16)*/\
    "psubw  "Y_OFFSET"("#c"), %%mm7     \n\t" /* 8(Y-16)*/\
    "pmulhw  "Y_COEFF"("#c"), %%mm1     \n\t"\
    "pmulhw  "Y_COEFF"("#c"), %%mm7     \n\t"\
    /* mm1= Y1, mm2=ub, mm3=ug, mm4=vg mm5=vr, mm7=Y2 */\
    "paddw             %%mm3, %%mm4     \n\t"\
    "movq              %%mm2, %%mm0     \n\t"\
    "movq              %%mm5, %%mm6     \n\t"\
    "movq              %%mm4, %%mm3     \n\t"\
    "punpcklwd         %%mm2, %%mm2     \n\t"\
    "punpcklwd         %%mm5, %%mm5     \n\t"\
    "punpcklwd         %%mm4, %%mm4     \n\t"\
    "paddw             %%mm1, %%mm2     \n\t"\
    "paddw             %%mm1, %%mm5     \n\t"\
    "paddw             %%mm1, %%mm4     \n\t"\
    "punpckhwd         %%mm0, %%mm0     \n\t"\
    "punpckhwd         %%mm6, %%mm6     \n\t"\
    "punpckhwd         %%mm3, %%mm3     \n\t"\
    "paddw             %%mm7, %%mm0     \n\t"\
    "paddw             %%mm7, %%mm6     \n\t"\
    "paddw             %%mm7, %%mm3     \n\t"\
    /* mm0=B1, mm2=B2, mm3=G2, mm4=G1, mm5=R1, mm6=R2 */\
    "packuswb          %%mm0, %%mm2     \n\t"\
    "packuswb          %%mm6, %%mm5     \n\t"\
    "packuswb          %%mm3, %%mm4     \n\t"\

#define YSCALEYUV2RGB_YA(index, c, b1, b2) REAL_YSCALEYUV2RGB_YA(index, c, b1, b2)

#define YSCALEYUV2RGB(index, c) \
    REAL_YSCALEYUV2RGB_UV(index, c) \
    REAL_YSCALEYUV2RGB_YA(index, c, %0, %1) \
    REAL_YSCALEYUV2RGB_COEFF(c)

/**
 * vertical bilinear scale YV12 to RGB
 */
static void RENAME(yuv2rgb32_2)(SwsContext *c, const int16_t *buf[2],
                                const int16_t *ubuf[2], const int16_t *vbuf[2],
                                const int16_t *abuf[2], uint8_t *dest,
                                int dstW, int yalpha, int uvalpha, int y)
{
    const int16_t *buf0  = buf[0],  *buf1  = buf[1],
                  *ubuf0 = ubuf[0], *ubuf1 = ubuf[1];

    if (CONFIG_SWSCALE_ALPHA && c->needAlpha) {
        const int16_t *abuf0 = abuf[0], *abuf1 = abuf[1];
#if ARCH_X86_64
        __asm__ volatile(
            YSCALEYUV2RGB(%%r8, %5)
            YSCALEYUV2RGB_YA(%%r8, %5, %6, %7)
            "psraw                  $3, %%mm1       \n\t" /* abuf0[eax] - abuf1[eax] >>7*/
            "psraw                  $3, %%mm7       \n\t" /* abuf0[eax] - abuf1[eax] >>7*/
            "packuswb            %%mm7, %%mm1       \n\t"
            WRITEBGR32(%4, DSTW_OFFSET"(%5)", %%r8, %%mm2, %%mm4, %%mm5, %%mm1, %%mm0, %%mm7, %%mm3, %%mm6)
            :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "r" (dest),
               "a" (&c->redDither),
               "r" (abuf0), "r" (abuf1)
            : "%r8"
        );
#else
        c->u_temp=(intptr_t)abuf0;
        c->v_temp=(intptr_t)abuf1;
        __asm__ volatile(
            "mov %%"FF_REG_b", "ESP_OFFSET"(%5)     \n\t"
            "mov        %4, %%"FF_REG_b"            \n\t"
            "push %%"FF_REG_BP"                     \n\t"
            YSCALEYUV2RGB(%%FF_REGBP, %5)
            "push                   %0              \n\t"
            "push                   %1              \n\t"
            "mov          "U_TEMP"(%5), %0          \n\t"
            "mov          "V_TEMP"(%5), %1          \n\t"
            YSCALEYUV2RGB_YA(%%FF_REGBP, %5, %0, %1)
            "psraw                  $3, %%mm1       \n\t" /* abuf0[eax] - abuf1[eax] >>7*/
            "psraw                  $3, %%mm7       \n\t" /* abuf0[eax] - abuf1[eax] >>7*/
            "packuswb            %%mm7, %%mm1       \n\t"
            "pop                    %1              \n\t"
            "pop                    %0              \n\t"
            WRITEBGR32(%%FF_REGb, DSTW_OFFSET"(%5)", %%FF_REGBP, %%mm2, %%mm4, %%mm5, %%mm1, %%mm0, %%mm7, %%mm3, %%mm6)
            "pop %%"FF_REG_BP"                      \n\t"
            "mov "ESP_OFFSET"(%5), %%"FF_REG_b"     \n\t"
            :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
               "a" (&c->redDither)
        );
#endif
    } else {
        __asm__ volatile(
            "mov %%"FF_REG_b", "ESP_OFFSET"(%5)     \n\t"
            "mov        %4, %%"FF_REG_b"            \n\t"
            "push %%"FF_REG_BP"                     \n\t"
            YSCALEYUV2RGB(%%FF_REGBP, %5)
            "pcmpeqd %%mm7, %%mm7                   \n\t"
            WRITEBGR32(%%FF_REGb, DSTW_OFFSET"(%5)", %%FF_REGBP, %%mm2, %%mm4, %%mm5, %%mm7, %%mm0, %%mm1, %%mm3, %%mm6)
            "pop %%"FF_REG_BP"                      \n\t"
            "mov "ESP_OFFSET"(%5), %%"FF_REG_b"     \n\t"
            :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
               "a" (&c->redDither)
        );
    }
}

static void RENAME(yuv2bgr24_2)(SwsContext *c, const int16_t *buf[2],
                                const int16_t *ubuf[2], const int16_t *vbuf[2],
                                const int16_t *abuf[2], uint8_t *dest,
                                int dstW, int yalpha, int uvalpha, int y)
{
    const int16_t *buf0  = buf[0],  *buf1  = buf[1],
                  *ubuf0 = ubuf[0], *ubuf1 = ubuf[1];

    __asm__ volatile(
        "mov %%"FF_REG_b", "ESP_OFFSET"(%5)     \n\t"
        "mov           %4, %%"FF_REG_b"         \n\t"
        "push %%"FF_REG_BP"                     \n\t"
        YSCALEYUV2RGB(%%FF_REGBP, %5)
        "pxor    %%mm7, %%mm7                   \n\t"
        WRITEBGR24(%%FF_REGb, DSTW_OFFSET"(%5)", %%FF_REGBP)
        "pop %%"FF_REG_BP"                      \n\t"
        "mov "ESP_OFFSET"(%5), %%"FF_REG_b"     \n\t"
        :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
           "a" (&c->redDither)
           NAMED_CONSTRAINTS_ADD(ff_M24A,ff_M24C,ff_M24B)
    );
}

static void RENAME(yuv2rgb555_2)(SwsContext *c, const int16_t *buf[2],
                                 const int16_t *ubuf[2], const int16_t *vbuf[2],
                                 const int16_t *abuf[2], uint8_t *dest,
                                 int dstW, int yalpha, int uvalpha, int y)
{
    const int16_t *buf0  = buf[0],  *buf1  = buf[1],
                  *ubuf0 = ubuf[0], *ubuf1 = ubuf[1];

    __asm__ volatile(
        "mov %%"FF_REG_b", "ESP_OFFSET"(%5)     \n\t"
        "mov        %4, %%"FF_REG_b"            \n\t"
        "push %%"FF_REG_BP"                     \n\t"
        YSCALEYUV2RGB(%%FF_REGBP, %5)
        "pxor    %%mm7, %%mm7                   \n\t"
        /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
        "paddusb "BLUE_DITHER"(%5), %%mm2       \n\t"
        "paddusb "GREEN_DITHER"(%5), %%mm4      \n\t"
        "paddusb "RED_DITHER"(%5), %%mm5        \n\t"
#endif
        WRITERGB15(%%FF_REGb, DSTW_OFFSET"(%5)", %%FF_REGBP)
        "pop %%"FF_REG_BP"                      \n\t"
        "mov "ESP_OFFSET"(%5), %%"FF_REG_b"     \n\t"
        :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
           "a" (&c->redDither)
           NAMED_CONSTRAINTS_ADD(bF8)
    );
}

static void RENAME(yuv2rgb565_2)(SwsContext *c, const int16_t *buf[2],
                                 const int16_t *ubuf[2], const int16_t *vbuf[2],
                                 const int16_t *abuf[2], uint8_t *dest,
                                 int dstW, int yalpha, int uvalpha, int y)
{
    const int16_t *buf0  = buf[0],  *buf1  = buf[1],
                  *ubuf0 = ubuf[0], *ubuf1 = ubuf[1];

    __asm__ volatile(
        "mov %%"FF_REG_b", "ESP_OFFSET"(%5)     \n\t"
        "mov           %4, %%"FF_REG_b"         \n\t"
        "push %%"FF_REG_BP"                     \n\t"
        YSCALEYUV2RGB(%%FF_REGBP, %5)
        "pxor    %%mm7, %%mm7                   \n\t"
        /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
        "paddusb "BLUE_DITHER"(%5), %%mm2       \n\t"
        "paddusb "GREEN_DITHER"(%5), %%mm4      \n\t"
        "paddusb "RED_DITHER"(%5), %%mm5        \n\t"
#endif
        WRITERGB16(%%FF_REGb, DSTW_OFFSET"(%5)", %%FF_REGBP)
        "pop %%"FF_REG_BP"                      \n\t"
        "mov "ESP_OFFSET"(%5), %%"FF_REG_b"     \n\t"
        :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
           "a" (&c->redDither)
           NAMED_CONSTRAINTS_ADD(bF8,bFC)
    );
}

#define REAL_YSCALEYUV2PACKED(index, c) \
    "movq "CHR_MMX_FILTER_OFFSET"+8("#c"), %%mm0              \n\t"\
    "movq "LUM_MMX_FILTER_OFFSET"+8("#c"), %%mm1              \n\t"\
    "psraw                $3, %%mm0                           \n\t"\
    "psraw                $3, %%mm1                           \n\t"\
    "movq              %%mm0, "CHR_MMX_FILTER_OFFSET"+8("#c") \n\t"\
    "movq              %%mm1, "LUM_MMX_FILTER_OFFSET"+8("#c") \n\t"\
    "xor            "#index", "#index"                        \n\t"\
    ".p2align              4            \n\t"\
    "1:                                 \n\t"\
    "movq     (%2, "#index"), %%mm2     \n\t" /* uvbuf0[eax]*/\
    "movq     (%3, "#index"), %%mm3     \n\t" /* uvbuf1[eax]*/\
    "add "UV_OFF_BYTE"("#c"), "#index"  \n\t" \
    "movq     (%2, "#index"), %%mm5     \n\t" /* uvbuf0[eax+2048]*/\
    "movq     (%3, "#index"), %%mm4     \n\t" /* uvbuf1[eax+2048]*/\
    "sub "UV_OFF_BYTE"("#c"), "#index"  \n\t" \
    "psubw             %%mm3, %%mm2     \n\t" /* uvbuf0[eax] - uvbuf1[eax]*/\
    "psubw             %%mm4, %%mm5     \n\t" /* uvbuf0[eax+2048] - uvbuf1[eax+2048]*/\
    "movq "CHR_MMX_FILTER_OFFSET"+8("#c"), %%mm0    \n\t"\
    "pmulhw            %%mm0, %%mm2     \n\t" /* (uvbuf0[eax] - uvbuf1[eax])uvalpha1>>16*/\
    "pmulhw            %%mm0, %%mm5     \n\t" /* (uvbuf0[eax+2048] - uvbuf1[eax+2048])uvalpha1>>16*/\
    "psraw                $7, %%mm3     \n\t" /* uvbuf0[eax] - uvbuf1[eax] >>4*/\
    "psraw                $7, %%mm4     \n\t" /* uvbuf0[eax+2048] - uvbuf1[eax+2048] >>4*/\
    "paddw             %%mm2, %%mm3     \n\t" /* uvbuf0[eax]uvalpha1 - uvbuf1[eax](1-uvalpha1)*/\
    "paddw             %%mm5, %%mm4     \n\t" /* uvbuf0[eax+2048]uvalpha1 - uvbuf1[eax+2048](1-uvalpha1)*/\
    "movq  (%0, "#index", 2), %%mm0     \n\t" /*buf0[eax]*/\
    "movq  (%1, "#index", 2), %%mm1     \n\t" /*buf1[eax]*/\
    "movq 8(%0, "#index", 2), %%mm6     \n\t" /*buf0[eax]*/\
    "movq 8(%1, "#index", 2), %%mm7     \n\t" /*buf1[eax]*/\
    "psubw             %%mm1, %%mm0     \n\t" /* buf0[eax] - buf1[eax]*/\
    "psubw             %%mm7, %%mm6     \n\t" /* buf0[eax] - buf1[eax]*/\
    "pmulhw "LUM_MMX_FILTER_OFFSET"+8("#c"), %%mm0  \n\t" /* (buf0[eax] - buf1[eax])yalpha1>>16*/\
    "pmulhw "LUM_MMX_FILTER_OFFSET"+8("#c"), %%mm6  \n\t" /* (buf0[eax] - buf1[eax])yalpha1>>16*/\
    "psraw                $7, %%mm1     \n\t" /* buf0[eax] - buf1[eax] >>4*/\
    "psraw                $7, %%mm7     \n\t" /* buf0[eax] - buf1[eax] >>4*/\
    "paddw             %%mm0, %%mm1     \n\t" /* buf0[eax]yalpha1 + buf1[eax](1-yalpha1) >>16*/\
    "paddw             %%mm6, %%mm7     \n\t" /* buf0[eax]yalpha1 + buf1[eax](1-yalpha1) >>16*/\

#define YSCALEYUV2PACKED(index, c)  REAL_YSCALEYUV2PACKED(index, c)

static void RENAME(yuv2yuyv422_2)(SwsContext *c, const int16_t *buf[2],
                                  const int16_t *ubuf[2], const int16_t *vbuf[2],
                                  const int16_t *abuf[2], uint8_t *dest,
                                  int dstW, int yalpha, int uvalpha, int y)
{
    const int16_t *buf0  = buf[0],  *buf1  = buf[1],
                  *ubuf0 = ubuf[0], *ubuf1 = ubuf[1];

    __asm__ volatile(
        "mov %%"FF_REG_b", "ESP_OFFSET"(%5)     \n\t"
        "mov           %4, %%"FF_REG_b"         \n\t"
        "push %%"FF_REG_BP"                     \n\t"
        YSCALEYUV2PACKED(%%FF_REGBP, %5)
        WRITEYUY2(%%FF_REGb, DSTW_OFFSET"(%5)", %%FF_REGBP)
        "pop %%"FF_REG_BP"                      \n\t"
        "mov "ESP_OFFSET"(%5), %%"FF_REG_b"     \n\t"
        :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
           "a" (&c->redDither)
    );
}

#define REAL_YSCALEYUV2RGB1(index, c) \
    "xor            "#index", "#index"  \n\t"\
    ".p2align              4            \n\t"\
    "1:                                 \n\t"\
    "movq     (%2, "#index"), %%mm3     \n\t" /* uvbuf0[eax]*/\
    "add "UV_OFF_BYTE"("#c"), "#index"  \n\t" \
    "movq     (%2, "#index"), %%mm4     \n\t" /* uvbuf0[eax+2048]*/\
    "sub "UV_OFF_BYTE"("#c"), "#index"  \n\t" \
    "psraw                $4, %%mm3     \n\t" /* uvbuf0[eax] - uvbuf1[eax] >>4*/\
    "psraw                $4, %%mm4     \n\t" /* uvbuf0[eax+2048] - uvbuf1[eax+2048] >>4*/\
    "psubw  "U_OFFSET"("#c"), %%mm3     \n\t" /* (U-128)8*/\
    "psubw  "V_OFFSET"("#c"), %%mm4     \n\t" /* (V-128)8*/\
    "movq              %%mm3, %%mm2     \n\t" /* (U-128)8*/\
    "movq              %%mm4, %%mm5     \n\t" /* (V-128)8*/\
    "pmulhw "UG_COEFF"("#c"), %%mm3     \n\t"\
    "pmulhw "VG_COEFF"("#c"), %%mm4     \n\t"\
    /* mm2=(U-128)8, mm3=ug, mm4=vg mm5=(V-128)8 */\
    "movq  (%0, "#index", 2), %%mm1     \n\t" /*buf0[eax]*/\
    "movq 8(%0, "#index", 2), %%mm7     \n\t" /*buf0[eax]*/\
    "psraw                $4, %%mm1     \n\t" /* buf0[eax] - buf1[eax] >>4*/\
    "psraw                $4, %%mm7     \n\t" /* buf0[eax] - buf1[eax] >>4*/\
    "pmulhw "UB_COEFF"("#c"), %%mm2     \n\t"\
    "pmulhw "VR_COEFF"("#c"), %%mm5     \n\t"\
    "psubw  "Y_OFFSET"("#c"), %%mm1     \n\t" /* 8(Y-16)*/\
    "psubw  "Y_OFFSET"("#c"), %%mm7     \n\t" /* 8(Y-16)*/\
    "pmulhw  "Y_COEFF"("#c"), %%mm1     \n\t"\
    "pmulhw  "Y_COEFF"("#c"), %%mm7     \n\t"\
    /* mm1= Y1, mm2=ub, mm3=ug, mm4=vg mm5=vr, mm7=Y2 */\
    "paddw             %%mm3, %%mm4     \n\t"\
    "movq              %%mm2, %%mm0     \n\t"\
    "movq              %%mm5, %%mm6     \n\t"\
    "movq              %%mm4, %%mm3     \n\t"\
    "punpcklwd         %%mm2, %%mm2     \n\t"\
    "punpcklwd         %%mm5, %%mm5     \n\t"\
    "punpcklwd         %%mm4, %%mm4     \n\t"\
    "paddw             %%mm1, %%mm2     \n\t"\
    "paddw             %%mm1, %%mm5     \n\t"\
    "paddw             %%mm1, %%mm4     \n\t"\
    "punpckhwd         %%mm0, %%mm0     \n\t"\
    "punpckhwd         %%mm6, %%mm6     \n\t"\
    "punpckhwd         %%mm3, %%mm3     \n\t"\
    "paddw             %%mm7, %%mm0     \n\t"\
    "paddw             %%mm7, %%mm6     \n\t"\
    "paddw             %%mm7, %%mm3     \n\t"\
    /* mm0=B1, mm2=B2, mm3=G2, mm4=G1, mm5=R1, mm6=R2 */\
    "packuswb          %%mm0, %%mm2     \n\t"\
    "packuswb          %%mm6, %%mm5     \n\t"\
    "packuswb          %%mm3, %%mm4     \n\t"\

#define YSCALEYUV2RGB1(index, c)  REAL_YSCALEYUV2RGB1(index, c)

// do vertical chrominance interpolation
#define REAL_YSCALEYUV2RGB1b(index, c) \
    "xor            "#index", "#index"  \n\t"\
    ".p2align              4            \n\t"\
    "1:                                 \n\t"\
    "movq     (%2, "#index"), %%mm2     \n\t" /* uvbuf0[eax]*/\
    "movq     (%3, "#index"), %%mm3     \n\t" /* uvbuf1[eax]*/\
    "add "UV_OFF_BYTE"("#c"), "#index"  \n\t" \
    "movq     (%2, "#index"), %%mm5     \n\t" /* uvbuf0[eax+2048]*/\
    "movq     (%3, "#index"), %%mm4     \n\t" /* uvbuf1[eax+2048]*/\
    "sub "UV_OFF_BYTE"("#c"), "#index"  \n\t" \
    "paddw             %%mm2, %%mm3     \n\t" /* uvbuf0[eax] + uvbuf1[eax]*/\
    "paddw             %%mm5, %%mm4     \n\t" /* uvbuf0[eax+2048] + uvbuf1[eax+2048]*/\
    "psrlw                $5, %%mm3     \n\t" /*FIXME might overflow*/\
    "psrlw                $5, %%mm4     \n\t" /*FIXME might overflow*/\
    "psubw  "U_OFFSET"("#c"), %%mm3     \n\t" /* (U-128)8*/\
    "psubw  "V_OFFSET"("#c"), %%mm4     \n\t" /* (V-128)8*/\
    "movq              %%mm3, %%mm2     \n\t" /* (U-128)8*/\
    "movq              %%mm4, %%mm5     \n\t" /* (V-128)8*/\
    "pmulhw "UG_COEFF"("#c"), %%mm3     \n\t"\
    "pmulhw "VG_COEFF"("#c"), %%mm4     \n\t"\
    /* mm2=(U-128)8, mm3=ug, mm4=vg mm5=(V-128)8 */\
    "movq  (%0, "#index", 2), %%mm1     \n\t" /*buf0[eax]*/\
    "movq 8(%0, "#index", 2), %%mm7     \n\t" /*buf0[eax]*/\
    "psraw                $4, %%mm1     \n\t" /* buf0[eax] - buf1[eax] >>4*/\
    "psraw                $4, %%mm7     \n\t" /* buf0[eax] - buf1[eax] >>4*/\
    "pmulhw "UB_COEFF"("#c"), %%mm2     \n\t"\
    "pmulhw "VR_COEFF"("#c"), %%mm5     \n\t"\
    "psubw  "Y_OFFSET"("#c"), %%mm1     \n\t" /* 8(Y-16)*/\
    "psubw  "Y_OFFSET"("#c"), %%mm7     \n\t" /* 8(Y-16)*/\
    "pmulhw  "Y_COEFF"("#c"), %%mm1     \n\t"\
    "pmulhw  "Y_COEFF"("#c"), %%mm7     \n\t"\
    /* mm1= Y1, mm2=ub, mm3=ug, mm4=vg mm5=vr, mm7=Y2 */\
    "paddw             %%mm3, %%mm4     \n\t"\
    "movq              %%mm2, %%mm0     \n\t"\
    "movq              %%mm5, %%mm6     \n\t"\
    "movq              %%mm4, %%mm3     \n\t"\
    "punpcklwd         %%mm2, %%mm2     \n\t"\
    "punpcklwd         %%mm5, %%mm5     \n\t"\
    "punpcklwd         %%mm4, %%mm4     \n\t"\
    "paddw             %%mm1, %%mm2     \n\t"\
    "paddw             %%mm1, %%mm5     \n\t"\
    "paddw             %%mm1, %%mm4     \n\t"\
    "punpckhwd         %%mm0, %%mm0     \n\t"\
    "punpckhwd         %%mm6, %%mm6     \n\t"\
    "punpckhwd         %%mm3, %%mm3     \n\t"\
    "paddw             %%mm7, %%mm0     \n\t"\
    "paddw             %%mm7, %%mm6     \n\t"\
    "paddw             %%mm7, %%mm3     \n\t"\
    /* mm0=B1, mm2=B2, mm3=G2, mm4=G1, mm5=R1, mm6=R2 */\
    "packuswb          %%mm0, %%mm2     \n\t"\
    "packuswb          %%mm6, %%mm5     \n\t"\
    "packuswb          %%mm3, %%mm4     \n\t"\

#define YSCALEYUV2RGB1b(index, c)  REAL_YSCALEYUV2RGB1b(index, c)

#define REAL_YSCALEYUV2RGB1_ALPHA(index) \
    "movq  (%1, "#index", 2), %%mm7     \n\t" /* abuf0[index  ]     */\
    "movq 8(%1, "#index", 2), %%mm1     \n\t" /* abuf0[index+4]     */\
    "psraw                $7, %%mm7     \n\t" /* abuf0[index  ] >>7 */\
    "psraw                $7, %%mm1     \n\t" /* abuf0[index+4] >>7 */\
    "packuswb          %%mm1, %%mm7     \n\t"
#define YSCALEYUV2RGB1_ALPHA(index) REAL_YSCALEYUV2RGB1_ALPHA(index)

/**
 * YV12 to RGB without scaling or interpolating
 */
static void RENAME(yuv2rgb32_1)(SwsContext *c, const int16_t *buf0,
                                const int16_t *ubuf[2], const int16_t *vbuf[2],
                                const int16_t *abuf0, uint8_t *dest,
                                int dstW, int uvalpha, int y)
{
    const int16_t *ubuf0 = ubuf[0];
    const int16_t *buf1= buf0; //FIXME needed for RGB1/BGR1

    if (uvalpha < 2048) { // note this is not correct (shifts chrominance by 0.5 pixels) but it is a bit faster
        const int16_t *ubuf1 = ubuf[0];
        if (CONFIG_SWSCALE_ALPHA && c->needAlpha) {
            __asm__ volatile(
                "mov %%"FF_REG_b", "ESP_OFFSET"(%5)     \n\t"
                "mov           %4, %%"FF_REG_b"         \n\t"
                "push %%"FF_REG_BP"                     \n\t"
                YSCALEYUV2RGB1(%%FF_REGBP, %5)
                YSCALEYUV2RGB1_ALPHA(%%FF_REGBP)
                WRITEBGR32(%%FF_REGb, DSTW_OFFSET"(%5)", %%FF_REGBP, %%mm2, %%mm4, %%mm5, %%mm7, %%mm0, %%mm1, %%mm3, %%mm6)
                "pop %%"FF_REG_BP"                      \n\t"
                "mov "ESP_OFFSET"(%5), %%"FF_REG_b"     \n\t"
                :: "c" (buf0), "d" (abuf0), "S" (ubuf0), "D" (ubuf1), "m" (dest),
                   "a" (&c->redDither)
            );
        } else {
            __asm__ volatile(
                "mov %%"FF_REG_b", "ESP_OFFSET"(%5)     \n\t"
                "mov           %4, %%"FF_REG_b"         \n\t"
                "push %%"FF_REG_BP"                     \n\t"
                YSCALEYUV2RGB1(%%FF_REGBP, %5)
                "pcmpeqd %%mm7, %%mm7                   \n\t"
                WRITEBGR32(%%FF_REGb, DSTW_OFFSET"(%5)", %%FF_REGBP, %%mm2, %%mm4, %%mm5, %%mm7, %%mm0, %%mm1, %%mm3, %%mm6)
                "pop %%"FF_REG_BP"                      \n\t"
                "mov "ESP_OFFSET"(%5), %%"FF_REG_b"     \n\t"
                :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
                   "a" (&c->redDither)
            );
        }
    } else {
        const int16_t *ubuf1 = ubuf[1];
        if (CONFIG_SWSCALE_ALPHA && c->needAlpha) {
            __asm__ volatile(
                "mov %%"FF_REG_b", "ESP_OFFSET"(%5)     \n\t"
                "mov           %4, %%"FF_REG_b"         \n\t"
                "push %%"FF_REG_BP"                     \n\t"
                YSCALEYUV2RGB1b(%%FF_REGBP, %5)
                YSCALEYUV2RGB1_ALPHA(%%FF_REGBP)
                WRITEBGR32(%%FF_REGb, DSTW_OFFSET"(%5)", %%FF_REGBP, %%mm2, %%mm4, %%mm5, %%mm7, %%mm0, %%mm1, %%mm3, %%mm6)
                "pop %%"FF_REG_BP"                      \n\t"
                "mov "ESP_OFFSET"(%5), %%"FF_REG_b"     \n\t"
                :: "c" (buf0), "d" (abuf0), "S" (ubuf0), "D" (ubuf1), "m" (dest),
                   "a" (&c->redDither)
            );
        } else {
            __asm__ volatile(
                "mov %%"FF_REG_b", "ESP_OFFSET"(%5)     \n\t"
                "mov           %4, %%"FF_REG_b"         \n\t"
                "push %%"FF_REG_BP"                     \n\t"
                YSCALEYUV2RGB1b(%%FF_REGBP, %5)
                "pcmpeqd %%mm7, %%mm7                   \n\t"
                WRITEBGR32(%%FF_REGb, DSTW_OFFSET"(%5)", %%FF_REGBP, %%mm2, %%mm4, %%mm5, %%mm7, %%mm0, %%mm1, %%mm3, %%mm6)
                "pop %%"FF_REG_BP"                      \n\t"
                "mov "ESP_OFFSET"(%5), %%"FF_REG_b"     \n\t"
                :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
                   "a" (&c->redDither)
            );
        }
    }
}

static void RENAME(yuv2bgr24_1)(SwsContext *c, const int16_t *buf0,
                                const int16_t *ubuf[2], const int16_t *vbuf[2],
                                const int16_t *abuf0, uint8_t *dest,
                                int dstW, int uvalpha, int y)
{
    const int16_t *ubuf0 = ubuf[0];
    const int16_t *buf1= buf0; //FIXME needed for RGB1/BGR1

    if (uvalpha < 2048) { // note this is not correct (shifts chrominance by 0.5 pixels) but it is a bit faster
        const int16_t *ubuf1 = ubuf[0];
        __asm__ volatile(
            "mov %%"FF_REG_b", "ESP_OFFSET"(%5)     \n\t"
            "mov           %4, %%"FF_REG_b"         \n\t"
            "push %%"FF_REG_BP"                     \n\t"
            YSCALEYUV2RGB1(%%FF_REGBP, %5)
            "pxor    %%mm7, %%mm7                   \n\t"
            WRITEBGR24(%%FF_REGb, DSTW_OFFSET"(%5)", %%FF_REGBP)
            "pop %%"FF_REG_BP"                      \n\t"
            "mov "ESP_OFFSET"(%5), %%"FF_REG_b"     \n\t"
            :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
               "a" (&c->redDither)
               NAMED_CONSTRAINTS_ADD(ff_M24A,ff_M24C,ff_M24B)
        );
    } else {
        const int16_t *ubuf1 = ubuf[1];
        __asm__ volatile(
            "mov %%"FF_REG_b", "ESP_OFFSET"(%5)     \n\t"
            "mov           %4, %%"FF_REG_b"         \n\t"
            "push %%"FF_REG_BP"                     \n\t"
            YSCALEYUV2RGB1b(%%FF_REGBP, %5)
            "pxor    %%mm7, %%mm7                   \n\t"
            WRITEBGR24(%%FF_REGb, DSTW_OFFSET"(%5)", %%FF_REGBP)
            "pop %%"FF_REG_BP"                      \n\t"
            "mov "ESP_OFFSET"(%5), %%"FF_REG_b"     \n\t"
            :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
               "a" (&c->redDither)
               NAMED_CONSTRAINTS_ADD(ff_M24A,ff_M24C,ff_M24B)
        );
    }
}

static void RENAME(yuv2rgb555_1)(SwsContext *c, const int16_t *buf0,
                                 const int16_t *ubuf[2], const int16_t *vbuf[2],
                                 const int16_t *abuf0, uint8_t *dest,
                                 int dstW, int uvalpha, int y)
{
    const int16_t *ubuf0 = ubuf[0];
    const int16_t *buf1= buf0; //FIXME needed for RGB1/BGR1

    if (uvalpha < 2048) { // note this is not correct (shifts chrominance by 0.5 pixels) but it is a bit faster
        const int16_t *ubuf1 = ubuf[0];
        __asm__ volatile(
            "mov %%"FF_REG_b", "ESP_OFFSET"(%5)     \n\t"
            "mov           %4, %%"FF_REG_b"         \n\t"
            "push %%"FF_REG_BP"                     \n\t"
            YSCALEYUV2RGB1(%%FF_REGBP, %5)
            "pxor    %%mm7, %%mm7                   \n\t"
            /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
            "paddusb "BLUE_DITHER"(%5), %%mm2       \n\t"
            "paddusb "GREEN_DITHER"(%5), %%mm4      \n\t"
            "paddusb "RED_DITHER"(%5), %%mm5        \n\t"
#endif
            WRITERGB15(%%FF_REGb, DSTW_OFFSET"(%5)", %%FF_REGBP)
            "pop %%"FF_REG_BP"                      \n\t"
            "mov "ESP_OFFSET"(%5), %%"FF_REG_b"     \n\t"
            :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
               "a" (&c->redDither)
               NAMED_CONSTRAINTS_ADD(bF8)
        );
    } else {
        const int16_t *ubuf1 = ubuf[1];
        __asm__ volatile(
            "mov %%"FF_REG_b", "ESP_OFFSET"(%5)     \n\t"
            "mov           %4, %%"FF_REG_b"         \n\t"
            "push %%"FF_REG_BP"                     \n\t"
            YSCALEYUV2RGB1b(%%FF_REGBP, %5)
            "pxor    %%mm7, %%mm7                   \n\t"
            /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
            "paddusb "BLUE_DITHER"(%5), %%mm2       \n\t"
            "paddusb "GREEN_DITHER"(%5), %%mm4      \n\t"
            "paddusb "RED_DITHER"(%5), %%mm5        \n\t"
#endif
            WRITERGB15(%%FF_REGb, DSTW_OFFSET"(%5)", %%FF_REGBP)
            "pop %%"FF_REG_BP"                      \n\t"
            "mov "ESP_OFFSET"(%5), %%"FF_REG_b"     \n\t"
            :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
               "a" (&c->redDither)
               NAMED_CONSTRAINTS_ADD(bF8)
        );
    }
}

static void RENAME(yuv2rgb565_1)(SwsContext *c, const int16_t *buf0,
                                 const int16_t *ubuf[2], const int16_t *vbuf[2],
                                 const int16_t *abuf0, uint8_t *dest,
                                 int dstW, int uvalpha, int y)
{
    const int16_t *ubuf0 = ubuf[0];
    const int16_t *buf1= buf0; //FIXME needed for RGB1/BGR1

    if (uvalpha < 2048) { // note this is not correct (shifts chrominance by 0.5 pixels) but it is a bit faster
        const int16_t *ubuf1 = ubuf[0];
        __asm__ volatile(
            "mov %%"FF_REG_b", "ESP_OFFSET"(%5)     \n\t"
            "mov           %4, %%"FF_REG_b"         \n\t"
            "push %%"FF_REG_BP"                     \n\t"
            YSCALEYUV2RGB1(%%FF_REGBP, %5)
            "pxor    %%mm7, %%mm7                   \n\t"
            /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
            "paddusb "BLUE_DITHER"(%5), %%mm2       \n\t"
            "paddusb "GREEN_DITHER"(%5), %%mm4      \n\t"
            "paddusb "RED_DITHER"(%5), %%mm5        \n\t"
#endif
            WRITERGB16(%%FF_REGb, DSTW_OFFSET"(%5)", %%FF_REGBP)
            "pop %%"FF_REG_BP"                      \n\t"
            "mov "ESP_OFFSET"(%5), %%"FF_REG_b"     \n\t"
            :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
               "a" (&c->redDither)
               NAMED_CONSTRAINTS_ADD(bF8,bFC)
        );
    } else {
        const int16_t *ubuf1 = ubuf[1];
        __asm__ volatile(
            "mov %%"FF_REG_b", "ESP_OFFSET"(%5)     \n\t"
            "mov           %4, %%"FF_REG_b"         \n\t"
            "push %%"FF_REG_BP"                     \n\t"
            YSCALEYUV2RGB1b(%%FF_REGBP, %5)
            "pxor    %%mm7, %%mm7                   \n\t"
            /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
            "paddusb "BLUE_DITHER"(%5), %%mm2       \n\t"
            "paddusb "GREEN_DITHER"(%5), %%mm4      \n\t"
            "paddusb "RED_DITHER"(%5), %%mm5        \n\t"
#endif
            WRITERGB16(%%FF_REGb, DSTW_OFFSET"(%5)", %%FF_REGBP)
            "pop %%"FF_REG_BP"                      \n\t"
            "mov "ESP_OFFSET"(%5), %%"FF_REG_b"     \n\t"
            :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
               "a" (&c->redDither)
               NAMED_CONSTRAINTS_ADD(bF8,bFC)
        );
    }
}

#define REAL_YSCALEYUV2PACKED1(index, c) \
    "xor            "#index", "#index"  \n\t"\
    ".p2align              4            \n\t"\
    "1:                                 \n\t"\
    "movq     (%2, "#index"), %%mm3     \n\t" /* uvbuf0[eax]*/\
    "add "UV_OFF_BYTE"("#c"), "#index"  \n\t" \
    "movq     (%2, "#index"), %%mm4     \n\t" /* uvbuf0[eax+2048]*/\
    "sub "UV_OFF_BYTE"("#c"), "#index"  \n\t" \
    "psraw                $7, %%mm3     \n\t" \
    "psraw                $7, %%mm4     \n\t" \
    "movq  (%0, "#index", 2), %%mm1     \n\t" /*buf0[eax]*/\
    "movq 8(%0, "#index", 2), %%mm7     \n\t" /*buf0[eax]*/\
    "psraw                $7, %%mm1     \n\t" \
    "psraw                $7, %%mm7     \n\t" \

#define YSCALEYUV2PACKED1(index, c)  REAL_YSCALEYUV2PACKED1(index, c)

#define REAL_YSCALEYUV2PACKED1b(index, c) \
    "xor "#index", "#index"             \n\t"\
    ".p2align              4            \n\t"\
    "1:                                 \n\t"\
    "movq     (%2, "#index"), %%mm2     \n\t" /* uvbuf0[eax]*/\
    "movq     (%3, "#index"), %%mm3     \n\t" /* uvbuf1[eax]*/\
    "add "UV_OFF_BYTE"("#c"), "#index"  \n\t" \
    "movq     (%2, "#index"), %%mm5     \n\t" /* uvbuf0[eax+2048]*/\
    "movq     (%3, "#index"), %%mm4     \n\t" /* uvbuf1[eax+2048]*/\
    "sub "UV_OFF_BYTE"("#c"), "#index"  \n\t" \
    "paddw             %%mm2, %%mm3     \n\t" /* uvbuf0[eax] + uvbuf1[eax]*/\
    "paddw             %%mm5, %%mm4     \n\t" /* uvbuf0[eax+2048] + uvbuf1[eax+2048]*/\
    "psrlw                $8, %%mm3     \n\t" \
    "psrlw                $8, %%mm4     \n\t" \
    "movq  (%0, "#index", 2), %%mm1     \n\t" /*buf0[eax]*/\
    "movq 8(%0, "#index", 2), %%mm7     \n\t" /*buf0[eax]*/\
    "psraw                $7, %%mm1     \n\t" \
    "psraw                $7, %%mm7     \n\t"
#define YSCALEYUV2PACKED1b(index, c)  REAL_YSCALEYUV2PACKED1b(index, c)

static void RENAME(yuv2yuyv422_1)(SwsContext *c, const int16_t *buf0,
                                  const int16_t *ubuf[2], const int16_t *vbuf[2],
                                  const int16_t *abuf0, uint8_t *dest,
                                  int dstW, int uvalpha, int y)
{
    const int16_t *ubuf0 = ubuf[0];
    const int16_t *buf1= buf0; //FIXME needed for RGB1/BGR1

    if (uvalpha < 2048) { // note this is not correct (shifts chrominance by 0.5 pixels) but it is a bit faster
        const int16_t *ubuf1 = ubuf[0];
        __asm__ volatile(
            "mov %%"FF_REG_b", "ESP_OFFSET"(%5)     \n\t"
            "mov           %4, %%"FF_REG_b"         \n\t"
            "push %%"FF_REG_BP"                     \n\t"
            YSCALEYUV2PACKED1(%%FF_REGBP, %5)
            WRITEYUY2(%%FF_REGb, DSTW_OFFSET"(%5)", %%FF_REGBP)
            "pop %%"FF_REG_BP"                      \n\t"
            "mov "ESP_OFFSET"(%5), %%"FF_REG_b"     \n\t"
            :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
               "a" (&c->redDither)
        );
    } else {
        const int16_t *ubuf1 = ubuf[1];
        __asm__ volatile(
            "mov %%"FF_REG_b", "ESP_OFFSET"(%5)     \n\t"
            "mov           %4, %%"FF_REG_b"         \n\t"
            "push %%"FF_REG_BP"                     \n\t"
            YSCALEYUV2PACKED1b(%%FF_REGBP, %5)
            WRITEYUY2(%%FF_REGb, DSTW_OFFSET"(%5)", %%FF_REGBP)
            "pop %%"FF_REG_BP"                      \n\t"
            "mov "ESP_OFFSET"(%5), %%"FF_REG_b"     \n\t"
            :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
               "a" (&c->redDither)
        );
    }
}
static av_cold void RENAME(sws_init_swscale)(SwsContext *c)
{
    enum AVPixelFormat dstFormat = c->dstFormat;

    c->use_mmx_vfilter= 0;
    if (!is16BPS(dstFormat) && !isNBPS(dstFormat) && dstFormat != AV_PIX_FMT_NV12
        && dstFormat != AV_PIX_FMT_NV21 && dstFormat != AV_PIX_FMT_GRAYF32BE && dstFormat != AV_PIX_FMT_GRAYF32LE
        && !(c->flags & SWS_BITEXACT)) {
            if (c->flags & SWS_ACCURATE_RND) {
                if (!(c->flags & SWS_FULL_CHR_H_INT)) {
                    switch (c->dstFormat) {
                    case AV_PIX_FMT_RGB32:   c->yuv2packedX = RENAME(yuv2rgb32_X_ar);   break;
#if HAVE_6REGS
                    case AV_PIX_FMT_BGR24:   c->yuv2packedX = RENAME(yuv2bgr24_X_ar);   break;
#endif
                    case AV_PIX_FMT_RGB555:  c->yuv2packedX = RENAME(yuv2rgb555_X_ar);  break;
                    case AV_PIX_FMT_RGB565:  c->yuv2packedX = RENAME(yuv2rgb565_X_ar);  break;
                    case AV_PIX_FMT_YUYV422: c->yuv2packedX = RENAME(yuv2yuyv422_X_ar); break;
                    default: break;
                    }
                }
            } else {
                c->use_mmx_vfilter= 1;
                c->yuv2planeX = RENAME(yuv2yuvX    );
                if (!(c->flags & SWS_FULL_CHR_H_INT)) {
                    switch (c->dstFormat) {
                    case AV_PIX_FMT_RGB32:   c->yuv2packedX = RENAME(yuv2rgb32_X);   break;
                    case AV_PIX_FMT_BGR32:   c->yuv2packedX = RENAME(yuv2bgr32_X);   break;
#if HAVE_6REGS
                    case AV_PIX_FMT_BGR24:   c->yuv2packedX = RENAME(yuv2bgr24_X);   break;
#endif
                    case AV_PIX_FMT_RGB555:  c->yuv2packedX = RENAME(yuv2rgb555_X);  break;
                    case AV_PIX_FMT_RGB565:  c->yuv2packedX = RENAME(yuv2rgb565_X);  break;
                    case AV_PIX_FMT_YUYV422: c->yuv2packedX = RENAME(yuv2yuyv422_X); break;
                    default: break;
                    }
                }
            }
        if (!(c->flags & SWS_FULL_CHR_H_INT)) {
            switch (c->dstFormat) {
            case AV_PIX_FMT_RGB32:
                c->yuv2packed1 = RENAME(yuv2rgb32_1);
                c->yuv2packed2 = RENAME(yuv2rgb32_2);
                break;
            case AV_PIX_FMT_BGR24:
                c->yuv2packed1 = RENAME(yuv2bgr24_1);
                c->yuv2packed2 = RENAME(yuv2bgr24_2);
                break;
            case AV_PIX_FMT_RGB555:
                c->yuv2packed1 = RENAME(yuv2rgb555_1);
                c->yuv2packed2 = RENAME(yuv2rgb555_2);
                break;
            case AV_PIX_FMT_RGB565:
                c->yuv2packed1 = RENAME(yuv2rgb565_1);
                c->yuv2packed2 = RENAME(yuv2rgb565_2);
                break;
            case AV_PIX_FMT_YUYV422:
                c->yuv2packed1 = RENAME(yuv2yuyv422_1);
                c->yuv2packed2 = RENAME(yuv2yuyv422_2);
                break;
            default:
                break;
            }
        }
    }

    if (c->srcBpc == 8 && c->dstBpc <= 14) {
    // Use the new MMX scaler if the MMXEXT one can't be used (it is faster than the x86 ASM one).
#if COMPILE_TEMPLATE_MMXEXT
    if (c->flags & SWS_FAST_BILINEAR && c->canMMXEXTBeUsed) {
        c->hyscale_fast = ff_hyscale_fast_mmxext;
        c->hcscale_fast = ff_hcscale_fast_mmxext;
    } else {
#endif /* COMPILE_TEMPLATE_MMXEXT */
        c->hyscale_fast = NULL;
        c->hcscale_fast = NULL;
#if COMPILE_TEMPLATE_MMXEXT
    }
#endif /* COMPILE_TEMPLATE_MMXEXT */
    }
}
