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

#undef REAL_MOVNTQ
#undef MOVNTQ
#undef PREFETCH

#if COMPILE_TEMPLATE_MMX2
#define PREFETCH "prefetchnta"
#else
#define PREFETCH  " # nop"
#endif

#if COMPILE_TEMPLATE_MMX2
#define REAL_MOVNTQ(a,b) "movntq " #a ", " #b " \n\t"
#else
#define REAL_MOVNTQ(a,b) "movq " #a ", " #b " \n\t"
#endif
#define MOVNTQ(a,b)  REAL_MOVNTQ(a,b)

#define YSCALEYUV2YV12X(offset, dest, end, pos) \
    __asm__ volatile(\
        "movq                  "DITHER16"+0(%0), %%mm3      \n\t"\
        "movq                  "DITHER16"+8(%0), %%mm4      \n\t"\
        "lea                     " offset "(%0), %%"REG_d"  \n\t"\
        "mov                        (%%"REG_d"), %%"REG_S"  \n\t"\
        ".p2align                             4             \n\t" /* FIXME Unroll? */\
        "1:                                                 \n\t"\
        "movq                      8(%%"REG_d"), %%mm0      \n\t" /* filterCoeff */\
        "movq                (%%"REG_S", %3, 2), %%mm2      \n\t" /* srcData */\
        "movq               8(%%"REG_S", %3, 2), %%mm5      \n\t" /* srcData */\
        "add                                $16, %%"REG_d"  \n\t"\
        "mov                        (%%"REG_d"), %%"REG_S"  \n\t"\
        "test                         %%"REG_S", %%"REG_S"  \n\t"\
        "pmulhw                           %%mm0, %%mm2      \n\t"\
        "pmulhw                           %%mm0, %%mm5      \n\t"\
        "paddw                            %%mm2, %%mm3      \n\t"\
        "paddw                            %%mm5, %%mm4      \n\t"\
        " jnz                                1b             \n\t"\
        "psraw                               $3, %%mm3      \n\t"\
        "psraw                               $3, %%mm4      \n\t"\
        "packuswb                         %%mm4, %%mm3      \n\t"\
        MOVNTQ(%%mm3, (%1, %3))\
        "add                                 $8, %3         \n\t"\
        "cmp                                 %2, %3         \n\t"\
        "movq                  "DITHER16"+0(%0), %%mm3      \n\t"\
        "movq                  "DITHER16"+8(%0), %%mm4      \n\t"\
        "lea                     " offset "(%0), %%"REG_d"  \n\t"\
        "mov                        (%%"REG_d"), %%"REG_S"  \n\t"\
        "jb                                  1b             \n\t"\
        :: "r" (&c->redDither),\
           "r" (dest), "g" ((x86_reg)(end)), "r"((x86_reg)(pos))\
        : "%"REG_d, "%"REG_S\
    );

static void RENAME(yuv2yuvX)(SwsContext *c, const int16_t *lumFilter,
                             const int16_t **lumSrc, int lumFilterSize,
                             const int16_t *chrFilter, const int16_t **chrUSrc,
                             const int16_t **chrVSrc,
                             int chrFilterSize, const int16_t **alpSrc,
                             uint8_t *dest, uint8_t *uDest, uint8_t *vDest,
                             uint8_t *aDest, int dstW, int chrDstW,
                             const uint8_t *lumDither, const uint8_t *chrDither)
{
    int i;
    if (uDest) {
        x86_reg uv_off = c->uv_off;
        for(i=0; i<8; i++) c->dither16[i] = chrDither[i]>>4;
        YSCALEYUV2YV12X(CHR_MMX_FILTER_OFFSET, uDest, chrDstW, 0)
        for(i=0; i<8; i++) c->dither16[i] = chrDither[(i+3)&7]>>4;
        YSCALEYUV2YV12X(CHR_MMX_FILTER_OFFSET, vDest - uv_off, chrDstW + uv_off, uv_off)
    }
    for(i=0; i<8; i++) c->dither16[i] = lumDither[i]>>4;
    if (CONFIG_SWSCALE_ALPHA && aDest) {
        YSCALEYUV2YV12X(ALP_MMX_FILTER_OFFSET, aDest, dstW, 0)
    }

    YSCALEYUV2YV12X(LUM_MMX_FILTER_OFFSET, dest, dstW, 0)
}

#define YSCALEYUV2YV12X_ACCURATE(offset, dest, end, pos) \
    __asm__ volatile(\
        "lea                     " offset "(%0), %%"REG_d"  \n\t"\
        "movq                  "DITHER32"+0(%0), %%mm4      \n\t"\
        "movq                  "DITHER32"+8(%0), %%mm5      \n\t"\
        "movq                 "DITHER32"+16(%0), %%mm6      \n\t"\
        "movq                 "DITHER32"+24(%0), %%mm7      \n\t"\
        "pxor                             %%mm4, %%mm4      \n\t"\
        "pxor                             %%mm5, %%mm5      \n\t"\
        "pxor                             %%mm6, %%mm6      \n\t"\
        "pxor                             %%mm7, %%mm7      \n\t"\
        "mov                        (%%"REG_d"), %%"REG_S"  \n\t"\
        ".p2align                             4             \n\t"\
        "1:                                                 \n\t"\
        "movq                (%%"REG_S", %3, 2), %%mm0      \n\t" /* srcData */\
        "movq               8(%%"REG_S", %3, 2), %%mm2      \n\t" /* srcData */\
        "mov        "STR(APCK_PTR2)"(%%"REG_d"), %%"REG_S"  \n\t"\
        "movq                (%%"REG_S", %3, 2), %%mm1      \n\t" /* srcData */\
        "movq                             %%mm0, %%mm3      \n\t"\
        "punpcklwd                        %%mm1, %%mm0      \n\t"\
        "punpckhwd                        %%mm1, %%mm3      \n\t"\
        "movq       "STR(APCK_COEF)"(%%"REG_d"), %%mm1      \n\t" /* filterCoeff */\
        "pmaddwd                          %%mm1, %%mm0      \n\t"\
        "pmaddwd                          %%mm1, %%mm3      \n\t"\
        "paddd                            %%mm0, %%mm4      \n\t"\
        "paddd                            %%mm3, %%mm5      \n\t"\
        "movq               8(%%"REG_S", %3, 2), %%mm3      \n\t" /* srcData */\
        "mov        "STR(APCK_SIZE)"(%%"REG_d"), %%"REG_S"  \n\t"\
        "add                  $"STR(APCK_SIZE)", %%"REG_d"  \n\t"\
        "test                         %%"REG_S", %%"REG_S"  \n\t"\
        "movq                             %%mm2, %%mm0      \n\t"\
        "punpcklwd                        %%mm3, %%mm2      \n\t"\
        "punpckhwd                        %%mm3, %%mm0      \n\t"\
        "pmaddwd                          %%mm1, %%mm2      \n\t"\
        "pmaddwd                          %%mm1, %%mm0      \n\t"\
        "paddd                            %%mm2, %%mm6      \n\t"\
        "paddd                            %%mm0, %%mm7      \n\t"\
        " jnz                                1b             \n\t"\
        "psrad                              $19, %%mm4      \n\t"\
        "psrad                              $19, %%mm5      \n\t"\
        "psrad                              $19, %%mm6      \n\t"\
        "psrad                              $19, %%mm7      \n\t"\
        "packssdw                         %%mm5, %%mm4      \n\t"\
        "packssdw                         %%mm7, %%mm6      \n\t"\
        "packuswb                         %%mm6, %%mm4      \n\t"\
        MOVNTQ(%%mm4, (%1, %3))\
        "add                                 $8, %3         \n\t"\
        "cmp                                 %2, %3         \n\t"\
        "lea                     " offset "(%0), %%"REG_d"  \n\t"\
        "movq                  "DITHER32"+0(%0), %%mm4      \n\t"\
        "movq                  "DITHER32"+8(%0), %%mm5      \n\t"\
        "movq                 "DITHER32"+16(%0), %%mm6      \n\t"\
        "movq                 "DITHER32"+24(%0), %%mm7      \n\t"\
        "mov                        (%%"REG_d"), %%"REG_S"  \n\t"\
        "jb                                  1b             \n\t"\
        :: "r" (&c->redDither),\
        "r" (dest), "g" ((x86_reg)(end)), "r"((x86_reg)(pos))\
        : "%"REG_a, "%"REG_d, "%"REG_S\
    );

static void RENAME(yuv2yuvX_ar)(SwsContext *c, const int16_t *lumFilter,
                                const int16_t **lumSrc, int lumFilterSize,
                                const int16_t *chrFilter, const int16_t **chrUSrc,
                                const int16_t **chrVSrc,
                                int chrFilterSize, const int16_t **alpSrc,
                                uint8_t *dest, uint8_t *uDest, uint8_t *vDest,
                                uint8_t *aDest, int dstW, int chrDstW,
                                const uint8_t *lumDither, const uint8_t *chrDither)
{
    int i;
    if (uDest) {
        x86_reg uv_off = c->uv_off;
        for(i=0; i<8; i++) c->dither32[i] = chrDither[i]<<12;
        YSCALEYUV2YV12X_ACCURATE(CHR_MMX_FILTER_OFFSET, uDest, chrDstW, 0)
        for(i=0; i<8; i++) c->dither32[i] = chrDither[(i+3)&7]<<12;
        YSCALEYUV2YV12X_ACCURATE(CHR_MMX_FILTER_OFFSET, vDest - uv_off, chrDstW + uv_off, uv_off)
    }
    for(i=0; i<8; i++) c->dither32[i] = lumDither[i]<<12;
    if (CONFIG_SWSCALE_ALPHA && aDest) {
        YSCALEYUV2YV12X_ACCURATE(ALP_MMX_FILTER_OFFSET, aDest, dstW, 0)
    }

    YSCALEYUV2YV12X_ACCURATE(LUM_MMX_FILTER_OFFSET, dest, dstW, 0)
}

static void RENAME(yuv2yuv1)(SwsContext *c, const int16_t *lumSrc,
                             const int16_t *chrUSrc, const int16_t *chrVSrc,
                             const int16_t *alpSrc,
                             uint8_t *dest, uint8_t *uDest, uint8_t *vDest,
                             uint8_t *aDest, int dstW, int chrDstW,
                             const uint8_t *lumDither, const uint8_t *chrDither)
{
    int p= 4;
    const int16_t *src[4]= { alpSrc + dstW, lumSrc + dstW, chrUSrc + chrDstW, chrVSrc + chrDstW };
    uint8_t *dst[4]= { aDest, dest, uDest, vDest };
    x86_reg counter[4]= { dstW, dstW, chrDstW, chrDstW };

    while (p--) {
        if (dst[p]) {
            __asm__ volatile(
                "mov %2, %%"REG_a"                    \n\t"
                ".p2align               4             \n\t" /* FIXME Unroll? */
                "1:                                   \n\t"
                "movq  (%0, %%"REG_a", 2), %%mm0      \n\t"
                "movq 8(%0, %%"REG_a", 2), %%mm1      \n\t"
                "psraw                 $7, %%mm0      \n\t"
                "psraw                 $7, %%mm1      \n\t"
                "packuswb           %%mm1, %%mm0      \n\t"
                MOVNTQ(%%mm0, (%1, %%REGa))
                "add                   $8, %%"REG_a"  \n\t"
                "jnc                   1b             \n\t"
                :: "r" (src[p]), "r" (dst[p] + counter[p]),
                   "g" (-counter[p])
                : "%"REG_a
            );
        }
    }
}

static void RENAME(yuv2yuv1_ar)(SwsContext *c, const int16_t *lumSrc,
                                const int16_t *chrUSrc, const int16_t *chrVSrc,
                                const int16_t *alpSrc,
                                uint8_t *dest, uint8_t *uDest, uint8_t *vDest,
                                uint8_t *aDest, int dstW, int chrDstW,
                                const uint8_t *lumDither, const uint8_t *chrDither)
{
    int p= 4;
    const int16_t *src[4]= { alpSrc + dstW, lumSrc + dstW, chrUSrc + chrDstW, chrVSrc + chrDstW };
    uint8_t *dst[4]= { aDest, dest, uDest, vDest };
    x86_reg counter[4]= { dstW, dstW, chrDstW, chrDstW };

    while (p--) {
        if (dst[p]) {
            int i;
            for(i=0; i<8; i++) c->dither16[i] = i<2 ? lumDither[i] : chrDither[i];
            __asm__ volatile(
                "mov %2, %%"REG_a"                    \n\t"
                "movq               0(%3), %%mm6      \n\t"
                "movq               8(%3), %%mm7      \n\t"
                ".p2align                4            \n\t" /* FIXME Unroll? */
                "1:                                   \n\t"
                "movq  (%0, %%"REG_a", 2), %%mm0      \n\t"
                "movq 8(%0, %%"REG_a", 2), %%mm1      \n\t"
                "paddsw             %%mm6, %%mm0      \n\t"
                "paddsw             %%mm7, %%mm1      \n\t"
                "psraw                 $7, %%mm0      \n\t"
                "psraw                 $7, %%mm1      \n\t"
                "packuswb           %%mm1, %%mm0      \n\t"
                MOVNTQ(%%mm0, (%1, %%REGa))
                "add                   $8, %%"REG_a"  \n\t"
                "jnc                   1b             \n\t"
                :: "r" (src[p]), "r" (dst[p] + counter[p]),
                   "g" (-counter[p]), "r"(c->dither16)
                : "%"REG_a
            );
        }
    }
}

#define YSCALEYUV2PACKEDX_UV \
    __asm__ volatile(\
        "xor                   %%"REG_a", %%"REG_a"     \n\t"\
        ".p2align                      4                \n\t"\
        "nop                                            \n\t"\
        "1:                                             \n\t"\
        "lea "CHR_MMX_FILTER_OFFSET"(%0), %%"REG_d"     \n\t"\
        "mov                 (%%"REG_d"), %%"REG_S"     \n\t"\
        "movq      "VROUNDER_OFFSET"(%0), %%mm3         \n\t"\
        "movq                      %%mm3, %%mm4         \n\t"\
        ".p2align                      4                \n\t"\
        "2:                                             \n\t"\
        "movq               8(%%"REG_d"), %%mm0         \n\t" /* filterCoeff */\
        "movq     (%%"REG_S", %%"REG_a"), %%mm2         \n\t" /* UsrcData */\
        "add                          %6, %%"REG_S"     \n\t" \
        "movq     (%%"REG_S", %%"REG_a"), %%mm5         \n\t" /* VsrcData */\
        "add                         $16, %%"REG_d"     \n\t"\
        "mov                 (%%"REG_d"), %%"REG_S"     \n\t"\
        "pmulhw                    %%mm0, %%mm2         \n\t"\
        "pmulhw                    %%mm0, %%mm5         \n\t"\
        "paddw                     %%mm2, %%mm3         \n\t"\
        "paddw                     %%mm5, %%mm4         \n\t"\
        "test                  %%"REG_S", %%"REG_S"     \n\t"\
        " jnz                         2b                \n\t"\

#define YSCALEYUV2PACKEDX_YA(offset,coeff,src1,src2,dst1,dst2) \
    "lea                "offset"(%0), %%"REG_d"     \n\t"\
    "mov                 (%%"REG_d"), %%"REG_S"     \n\t"\
    "movq      "VROUNDER_OFFSET"(%0), "#dst1"       \n\t"\
    "movq                    "#dst1", "#dst2"       \n\t"\
    ".p2align                      4                \n\t"\
    "2:                                             \n\t"\
    "movq               8(%%"REG_d"), "#coeff"      \n\t" /* filterCoeff */\
    "movq  (%%"REG_S", %%"REG_a", 2), "#src1"       \n\t" /* Y1srcData */\
    "movq 8(%%"REG_S", %%"REG_a", 2), "#src2"       \n\t" /* Y2srcData */\
    "add                         $16, %%"REG_d"            \n\t"\
    "mov                 (%%"REG_d"), %%"REG_S"     \n\t"\
    "pmulhw                 "#coeff", "#src1"       \n\t"\
    "pmulhw                 "#coeff", "#src2"       \n\t"\
    "paddw                   "#src1", "#dst1"       \n\t"\
    "paddw                   "#src2", "#dst2"       \n\t"\
    "test                  %%"REG_S", %%"REG_S"     \n\t"\
    " jnz                         2b                \n\t"\

#define YSCALEYUV2PACKEDX \
    YSCALEYUV2PACKEDX_UV \
    YSCALEYUV2PACKEDX_YA(LUM_MMX_FILTER_OFFSET,%%mm0,%%mm2,%%mm5,%%mm1,%%mm7) \

#define YSCALEYUV2PACKEDX_END                     \
        :: "r" (&c->redDither),                   \
            "m" (dummy), "m" (dummy), "m" (dummy),\
            "r" (dest), "m" (dstW_reg), "m"(uv_off) \
        : "%"REG_a, "%"REG_d, "%"REG_S            \
    );

#define YSCALEYUV2PACKEDX_ACCURATE_UV \
    __asm__ volatile(\
        "xor %%"REG_a", %%"REG_a"                       \n\t"\
        ".p2align                      4                \n\t"\
        "nop                                            \n\t"\
        "1:                                             \n\t"\
        "lea "CHR_MMX_FILTER_OFFSET"(%0), %%"REG_d"     \n\t"\
        "mov                 (%%"REG_d"), %%"REG_S"     \n\t"\
        "pxor                      %%mm4, %%mm4         \n\t"\
        "pxor                      %%mm5, %%mm5         \n\t"\
        "pxor                      %%mm6, %%mm6         \n\t"\
        "pxor                      %%mm7, %%mm7         \n\t"\
        ".p2align                      4                \n\t"\
        "2:                                             \n\t"\
        "movq     (%%"REG_S", %%"REG_a"), %%mm0         \n\t" /* UsrcData */\
        "add                          %6, %%"REG_S"      \n\t" \
        "movq     (%%"REG_S", %%"REG_a"), %%mm2         \n\t" /* VsrcData */\
        "mov "STR(APCK_PTR2)"(%%"REG_d"), %%"REG_S"     \n\t"\
        "movq     (%%"REG_S", %%"REG_a"), %%mm1         \n\t" /* UsrcData */\
        "movq                      %%mm0, %%mm3         \n\t"\
        "punpcklwd                 %%mm1, %%mm0         \n\t"\
        "punpckhwd                 %%mm1, %%mm3         \n\t"\
        "movq "STR(APCK_COEF)"(%%"REG_d"),%%mm1         \n\t" /* filterCoeff */\
        "pmaddwd                   %%mm1, %%mm0         \n\t"\
        "pmaddwd                   %%mm1, %%mm3         \n\t"\
        "paddd                     %%mm0, %%mm4         \n\t"\
        "paddd                     %%mm3, %%mm5         \n\t"\
        "add                          %6, %%"REG_S"      \n\t" \
        "movq     (%%"REG_S", %%"REG_a"), %%mm3         \n\t" /* VsrcData */\
        "mov "STR(APCK_SIZE)"(%%"REG_d"), %%"REG_S"     \n\t"\
        "add           $"STR(APCK_SIZE)", %%"REG_d"     \n\t"\
        "test                  %%"REG_S", %%"REG_S"     \n\t"\
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
    "lea                "offset"(%0), %%"REG_d"     \n\t"\
    "mov                 (%%"REG_d"), %%"REG_S"     \n\t"\
    "pxor                      %%mm1, %%mm1         \n\t"\
    "pxor                      %%mm5, %%mm5         \n\t"\
    "pxor                      %%mm7, %%mm7         \n\t"\
    "pxor                      %%mm6, %%mm6         \n\t"\
    ".p2align                      4                \n\t"\
    "2:                                             \n\t"\
    "movq  (%%"REG_S", %%"REG_a", 2), %%mm0         \n\t" /* Y1srcData */\
    "movq 8(%%"REG_S", %%"REG_a", 2), %%mm2         \n\t" /* Y2srcData */\
    "mov "STR(APCK_PTR2)"(%%"REG_d"), %%"REG_S"     \n\t"\
    "movq  (%%"REG_S", %%"REG_a", 2), %%mm4         \n\t" /* Y1srcData */\
    "movq                      %%mm0, %%mm3         \n\t"\
    "punpcklwd                 %%mm4, %%mm0         \n\t"\
    "punpckhwd                 %%mm4, %%mm3         \n\t"\
    "movq "STR(APCK_COEF)"(%%"REG_d"), %%mm4         \n\t" /* filterCoeff */\
    "pmaddwd                   %%mm4, %%mm0         \n\t"\
    "pmaddwd                   %%mm4, %%mm3         \n\t"\
    "paddd                     %%mm0, %%mm1         \n\t"\
    "paddd                     %%mm3, %%mm5         \n\t"\
    "movq 8(%%"REG_S", %%"REG_a", 2), %%mm3         \n\t" /* Y2srcData */\
    "mov "STR(APCK_SIZE)"(%%"REG_d"), %%"REG_S"     \n\t"\
    "add           $"STR(APCK_SIZE)", %%"REG_d"     \n\t"\
    "test                  %%"REG_S", %%"REG_S"     \n\t"\
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
    "cmp "#dstw", "#index"      \n\t"\
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
    x86_reg uv_off = c->uv_off << 1;

    if (CONFIG_SWSCALE_ALPHA && c->alpPixBuf) {
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
        WRITEBGR32(%4, %5, %%REGa, %%mm3, %%mm4, %%mm5, %%mm1, %%mm0, %%mm7, %%mm2, %%mm6)
        YSCALEYUV2PACKEDX_END
    } else {
        YSCALEYUV2PACKEDX_ACCURATE
        YSCALEYUV2RGBX
        "pcmpeqd %%mm7, %%mm7 \n\t"
        WRITEBGR32(%4, %5, %%REGa, %%mm2, %%mm4, %%mm5, %%mm7, %%mm0, %%mm1, %%mm3, %%mm6)
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
    x86_reg uv_off = c->uv_off << 1;

    if (CONFIG_SWSCALE_ALPHA && c->alpPixBuf) {
        YSCALEYUV2PACKEDX
        YSCALEYUV2RGBX
        YSCALEYUV2PACKEDX_YA(ALP_MMX_FILTER_OFFSET, %%mm0, %%mm3, %%mm6, %%mm1, %%mm7)
        "psraw                        $3, %%mm1         \n\t"
        "psraw                        $3, %%mm7         \n\t"
        "packuswb                  %%mm7, %%mm1         \n\t"
        WRITEBGR32(%4, %5, %%REGa, %%mm2, %%mm4, %%mm5, %%mm1, %%mm0, %%mm7, %%mm3, %%mm6)
        YSCALEYUV2PACKEDX_END
    } else {
        YSCALEYUV2PACKEDX
        YSCALEYUV2RGBX
        "pcmpeqd %%mm7, %%mm7 \n\t"
        WRITEBGR32(%4, %5, %%REGa, %%mm2, %%mm4, %%mm5, %%mm7, %%mm0, %%mm1, %%mm3, %%mm6)
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
    "cmp        "#dstw", "#index"   \n\t"\
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
    x86_reg uv_off = c->uv_off << 1;

    YSCALEYUV2PACKEDX_ACCURATE
    YSCALEYUV2RGBX
    "pxor %%mm7, %%mm7 \n\t"
    /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
    "paddusb "BLUE_DITHER"(%0), %%mm2\n\t"
    "paddusb "GREEN_DITHER"(%0), %%mm4\n\t"
    "paddusb "RED_DITHER"(%0), %%mm5\n\t"
#endif
    WRITERGB16(%4, %5, %%REGa)
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
    x86_reg uv_off = c->uv_off << 1;

    YSCALEYUV2PACKEDX
    YSCALEYUV2RGBX
    "pxor %%mm7, %%mm7 \n\t"
    /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
    "paddusb "BLUE_DITHER"(%0), %%mm2  \n\t"
    "paddusb "GREEN_DITHER"(%0), %%mm4  \n\t"
    "paddusb "RED_DITHER"(%0), %%mm5  \n\t"
#endif
    WRITERGB16(%4, %5, %%REGa)
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
    "cmp        "#dstw", "#index"   \n\t"\
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
    x86_reg uv_off = c->uv_off << 1;

    YSCALEYUV2PACKEDX_ACCURATE
    YSCALEYUV2RGBX
    "pxor %%mm7, %%mm7 \n\t"
    /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
    "paddusb "BLUE_DITHER"(%0), %%mm2\n\t"
    "paddusb "GREEN_DITHER"(%0), %%mm4\n\t"
    "paddusb "RED_DITHER"(%0), %%mm5\n\t"
#endif
    WRITERGB15(%4, %5, %%REGa)
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
    x86_reg uv_off = c->uv_off << 1;

    YSCALEYUV2PACKEDX
    YSCALEYUV2RGBX
    "pxor %%mm7, %%mm7 \n\t"
    /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
    "paddusb "BLUE_DITHER"(%0), %%mm2  \n\t"
    "paddusb "GREEN_DITHER"(%0), %%mm4  \n\t"
    "paddusb "RED_DITHER"(%0), %%mm5  \n\t"
#endif
    WRITERGB15(%4, %5, %%REGa)
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
    "cmp     "#dstw", "#index"  \n\t"\
    " jb          1b            \n\t"

#define WRITEBGR24MMX2(dst, dstw, index) \
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
    "cmp  "#dstw", "#index"     \n\t"\
    " jb       1b               \n\t"

#if COMPILE_TEMPLATE_MMX2
#undef WRITEBGR24
#define WRITEBGR24(dst, dstw, index)  WRITEBGR24MMX2(dst, dstw, index)
#else
#undef WRITEBGR24
#define WRITEBGR24(dst, dstw, index)  WRITEBGR24MMX(dst, dstw, index)
#endif

static void RENAME(yuv2bgr24_X_ar)(SwsContext *c, const int16_t *lumFilter,
                                   const int16_t **lumSrc, int lumFilterSize,
                                   const int16_t *chrFilter, const int16_t **chrUSrc,
                                   const int16_t **chrVSrc,
                                   int chrFilterSize, const int16_t **alpSrc,
                                   uint8_t *dest, int dstW, int dstY)
{
    x86_reg dummy=0;
    x86_reg dstW_reg = dstW;
    x86_reg uv_off = c->uv_off << 1;

    YSCALEYUV2PACKEDX_ACCURATE
    YSCALEYUV2RGBX
    "pxor %%mm7, %%mm7 \n\t"
    "lea (%%"REG_a", %%"REG_a", 2), %%"REG_c"\n\t" //FIXME optimize
    "add %4, %%"REG_c"                        \n\t"
    WRITEBGR24(%%REGc, %5, %%REGa)
    :: "r" (&c->redDither),
       "m" (dummy), "m" (dummy), "m" (dummy),
       "r" (dest), "m" (dstW_reg), "m"(uv_off)
    : "%"REG_a, "%"REG_c, "%"REG_d, "%"REG_S
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
    x86_reg uv_off = c->uv_off << 1;

    YSCALEYUV2PACKEDX
    YSCALEYUV2RGBX
    "pxor                    %%mm7, %%mm7       \n\t"
    "lea (%%"REG_a", %%"REG_a", 2), %%"REG_c"   \n\t" //FIXME optimize
    "add                        %4, %%"REG_c"   \n\t"
    WRITEBGR24(%%REGc, %5, %%REGa)
    :: "r" (&c->redDither),
       "m" (dummy), "m" (dummy), "m" (dummy),
       "r" (dest),  "m" (dstW_reg), "m"(uv_off)
    : "%"REG_a, "%"REG_c, "%"REG_d, "%"REG_S
    );
}

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
    "cmp     "#dstw", "#index"  \n\t"\
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
    x86_reg uv_off = c->uv_off << 1;

    YSCALEYUV2PACKEDX_ACCURATE
    /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
    "psraw $3, %%mm3    \n\t"
    "psraw $3, %%mm4    \n\t"
    "psraw $3, %%mm1    \n\t"
    "psraw $3, %%mm7    \n\t"
    WRITEYUY2(%4, %5, %%REGa)
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
    x86_reg uv_off = c->uv_off << 1;

    YSCALEYUV2PACKEDX
    /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
    "psraw $3, %%mm3    \n\t"
    "psraw $3, %%mm4    \n\t"
    "psraw $3, %%mm1    \n\t"
    "psraw $3, %%mm7    \n\t"
    WRITEYUY2(%4, %5, %%REGa)
    YSCALEYUV2PACKEDX_END
}

#define REAL_YSCALEYUV2RGB_UV(index, c) \
    "xor            "#index", "#index"  \n\t"\
    ".p2align              4            \n\t"\
    "1:                                 \n\t"\
    "movq     (%2, "#index"), %%mm2     \n\t" /* uvbuf0[eax]*/\
    "movq     (%3, "#index"), %%mm3     \n\t" /* uvbuf1[eax]*/\
    "add           "UV_OFFx2"("#c"), "#index"  \n\t" \
    "movq     (%2, "#index"), %%mm5     \n\t" /* uvbuf0[eax+2048]*/\
    "movq     (%3, "#index"), %%mm4     \n\t" /* uvbuf1[eax+2048]*/\
    "sub           "UV_OFFx2"("#c"), "#index"  \n\t" \
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
static void RENAME(yuv2rgb32_2)(SwsContext *c, const uint16_t *buf0,
                                const uint16_t *buf1, const uint16_t *ubuf0,
                                const uint16_t *ubuf1, const uint16_t *vbuf0,
                                const uint16_t *vbuf1, const uint16_t *abuf0,
                                const uint16_t *abuf1, uint8_t *dest,
                                int dstW, int yalpha, int uvalpha, int y)
{
    if (CONFIG_SWSCALE_ALPHA && c->alpPixBuf) {
#if ARCH_X86_64
        __asm__ volatile(
            YSCALEYUV2RGB(%%r8, %5)
            YSCALEYUV2RGB_YA(%%r8, %5, %6, %7)
            "psraw                  $3, %%mm1       \n\t" /* abuf0[eax] - abuf1[eax] >>7*/
            "psraw                  $3, %%mm7       \n\t" /* abuf0[eax] - abuf1[eax] >>7*/
            "packuswb            %%mm7, %%mm1       \n\t"
            WRITEBGR32(%4, 8280(%5), %%r8, %%mm2, %%mm4, %%mm5, %%mm1, %%mm0, %%mm7, %%mm3, %%mm6)
            :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "r" (dest),
               "a" (&c->redDither),
               "r" (abuf0), "r" (abuf1)
            : "%r8"
        );
#else
        c->u_temp=(intptr_t)abuf0;
        c->v_temp=(intptr_t)abuf1;
        __asm__ volatile(
            "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
            "mov        %4, %%"REG_b"               \n\t"
            "push %%"REG_BP"                        \n\t"
            YSCALEYUV2RGB(%%REGBP, %5)
            "push                   %0              \n\t"
            "push                   %1              \n\t"
            "mov          "U_TEMP"(%5), %0          \n\t"
            "mov          "V_TEMP"(%5), %1          \n\t"
            YSCALEYUV2RGB_YA(%%REGBP, %5, %0, %1)
            "psraw                  $3, %%mm1       \n\t" /* abuf0[eax] - abuf1[eax] >>7*/
            "psraw                  $3, %%mm7       \n\t" /* abuf0[eax] - abuf1[eax] >>7*/
            "packuswb            %%mm7, %%mm1       \n\t"
            "pop                    %1              \n\t"
            "pop                    %0              \n\t"
            WRITEBGR32(%%REGb, 8280(%5), %%REGBP, %%mm2, %%mm4, %%mm5, %%mm1, %%mm0, %%mm7, %%mm3, %%mm6)
            "pop %%"REG_BP"                         \n\t"
            "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"
            :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
               "a" (&c->redDither)
        );
#endif
    } else {
        __asm__ volatile(
            "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
            "mov        %4, %%"REG_b"               \n\t"
            "push %%"REG_BP"                        \n\t"
            YSCALEYUV2RGB(%%REGBP, %5)
            "pcmpeqd %%mm7, %%mm7                   \n\t"
            WRITEBGR32(%%REGb, 8280(%5), %%REGBP, %%mm2, %%mm4, %%mm5, %%mm7, %%mm0, %%mm1, %%mm3, %%mm6)
            "pop %%"REG_BP"                         \n\t"
            "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"
            :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
               "a" (&c->redDither)
        );
    }
}

static void RENAME(yuv2bgr24_2)(SwsContext *c, const uint16_t *buf0,
                                const uint16_t *buf1, const uint16_t *ubuf0,
                                const uint16_t *ubuf1, const uint16_t *vbuf0,
                                const uint16_t *vbuf1, const uint16_t *abuf0,
                                const uint16_t *abuf1, uint8_t *dest,
                                int dstW, int yalpha, int uvalpha, int y)
{
    //Note 8280 == DSTW_OFFSET but the preprocessor can't handle that there :(
    __asm__ volatile(
        "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
        "mov        %4, %%"REG_b"               \n\t"
        "push %%"REG_BP"                        \n\t"
        YSCALEYUV2RGB(%%REGBP, %5)
        "pxor    %%mm7, %%mm7                   \n\t"
        WRITEBGR24(%%REGb, 8280(%5), %%REGBP)
        "pop %%"REG_BP"                         \n\t"
        "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"
        :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
           "a" (&c->redDither)
    );
}

static void RENAME(yuv2rgb555_2)(SwsContext *c, const uint16_t *buf0,
                                 const uint16_t *buf1, const uint16_t *ubuf0,
                                 const uint16_t *ubuf1, const uint16_t *vbuf0,
                                 const uint16_t *vbuf1, const uint16_t *abuf0,
                                 const uint16_t *abuf1, uint8_t *dest,
                                 int dstW, int yalpha, int uvalpha, int y)
{
    //Note 8280 == DSTW_OFFSET but the preprocessor can't handle that there :(
    __asm__ volatile(
        "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
        "mov        %4, %%"REG_b"               \n\t"
        "push %%"REG_BP"                        \n\t"
        YSCALEYUV2RGB(%%REGBP, %5)
        "pxor    %%mm7, %%mm7                   \n\t"
        /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
        "paddusb "BLUE_DITHER"(%5), %%mm2      \n\t"
        "paddusb "GREEN_DITHER"(%5), %%mm4      \n\t"
        "paddusb "RED_DITHER"(%5), %%mm5      \n\t"
#endif
        WRITERGB15(%%REGb, 8280(%5), %%REGBP)
        "pop %%"REG_BP"                         \n\t"
        "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"
        :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
           "a" (&c->redDither)
    );
}

static void RENAME(yuv2rgb565_2)(SwsContext *c, const uint16_t *buf0,
                                 const uint16_t *buf1, const uint16_t *ubuf0,
                                 const uint16_t *ubuf1, const uint16_t *vbuf0,
                                 const uint16_t *vbuf1, const uint16_t *abuf0,
                                 const uint16_t *abuf1, uint8_t *dest,
                                 int dstW, int yalpha, int uvalpha, int y)
{
    //Note 8280 == DSTW_OFFSET but the preprocessor can't handle that there :(
    __asm__ volatile(
        "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
        "mov        %4, %%"REG_b"               \n\t"
        "push %%"REG_BP"                        \n\t"
        YSCALEYUV2RGB(%%REGBP, %5)
        "pxor    %%mm7, %%mm7                   \n\t"
        /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
        "paddusb "BLUE_DITHER"(%5), %%mm2      \n\t"
        "paddusb "GREEN_DITHER"(%5), %%mm4      \n\t"
        "paddusb "RED_DITHER"(%5), %%mm5      \n\t"
#endif
        WRITERGB16(%%REGb, 8280(%5), %%REGBP)
        "pop %%"REG_BP"                         \n\t"
        "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"
        :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
           "a" (&c->redDither)
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
    "add           "UV_OFFx2"("#c"), "#index"  \n\t" \
    "movq     (%2, "#index"), %%mm5     \n\t" /* uvbuf0[eax+2048]*/\
    "movq     (%3, "#index"), %%mm4     \n\t" /* uvbuf1[eax+2048]*/\
    "sub           "UV_OFFx2"("#c"), "#index"  \n\t" \
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

static void RENAME(yuv2yuyv422_2)(SwsContext *c, const uint16_t *buf0,
                                  const uint16_t *buf1, const uint16_t *ubuf0,
                                  const uint16_t *ubuf1, const uint16_t *vbuf0,
                                  const uint16_t *vbuf1, const uint16_t *abuf0,
                                  const uint16_t *abuf1, uint8_t *dest,
                                  int dstW, int yalpha, int uvalpha, int y)
{
    //Note 8280 == DSTW_OFFSET but the preprocessor can't handle that there :(
    __asm__ volatile(
        "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
        "mov %4, %%"REG_b"                        \n\t"
        "push %%"REG_BP"                        \n\t"
        YSCALEYUV2PACKED(%%REGBP, %5)
        WRITEYUY2(%%REGb, 8280(%5), %%REGBP)
        "pop %%"REG_BP"                         \n\t"
        "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"
        :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
           "a" (&c->redDither)
    );
}

#define REAL_YSCALEYUV2RGB1(index, c) \
    "xor            "#index", "#index"  \n\t"\
    ".p2align              4            \n\t"\
    "1:                                 \n\t"\
    "movq     (%2, "#index"), %%mm3     \n\t" /* uvbuf0[eax]*/\
    "add           "UV_OFFx2"("#c"), "#index"  \n\t" \
    "movq     (%2, "#index"), %%mm4     \n\t" /* uvbuf0[eax+2048]*/\
    "sub           "UV_OFFx2"("#c"), "#index"  \n\t" \
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
    "add           "UV_OFFx2"("#c"), "#index"  \n\t" \
    "movq     (%2, "#index"), %%mm5     \n\t" /* uvbuf0[eax+2048]*/\
    "movq     (%3, "#index"), %%mm4     \n\t" /* uvbuf1[eax+2048]*/\
    "sub           "UV_OFFx2"("#c"), "#index"  \n\t" \
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
static void RENAME(yuv2rgb32_1)(SwsContext *c, const uint16_t *buf0,
                                const uint16_t *ubuf0, const uint16_t *ubuf1,
                                const uint16_t *vbuf0, const uint16_t *vbuf1,
                                const uint16_t *abuf0, uint8_t *dest,
                                int dstW, int uvalpha, enum PixelFormat dstFormat,
                                int flags, int y)
{
    const uint16_t *buf1= buf0; //FIXME needed for RGB1/BGR1

    if (uvalpha < 2048) { // note this is not correct (shifts chrominance by 0.5 pixels) but it is a bit faster
        if (CONFIG_SWSCALE_ALPHA && c->alpPixBuf) {
            __asm__ volatile(
                "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
                "mov        %4, %%"REG_b"               \n\t"
                "push %%"REG_BP"                        \n\t"
                YSCALEYUV2RGB1(%%REGBP, %5)
                YSCALEYUV2RGB1_ALPHA(%%REGBP)
                WRITEBGR32(%%REGb, 8280(%5), %%REGBP, %%mm2, %%mm4, %%mm5, %%mm7, %%mm0, %%mm1, %%mm3, %%mm6)
                "pop %%"REG_BP"                         \n\t"
                "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"
                :: "c" (buf0), "d" (abuf0), "S" (ubuf0), "D" (ubuf1), "m" (dest),
                   "a" (&c->redDither)
            );
        } else {
            __asm__ volatile(
                "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
                "mov        %4, %%"REG_b"               \n\t"
                "push %%"REG_BP"                        \n\t"
                YSCALEYUV2RGB1(%%REGBP, %5)
                "pcmpeqd %%mm7, %%mm7                   \n\t"
                WRITEBGR32(%%REGb, 8280(%5), %%REGBP, %%mm2, %%mm4, %%mm5, %%mm7, %%mm0, %%mm1, %%mm3, %%mm6)
                "pop %%"REG_BP"                         \n\t"
                "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"
                :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
                   "a" (&c->redDither)
            );
        }
    } else {
        if (CONFIG_SWSCALE_ALPHA && c->alpPixBuf) {
            __asm__ volatile(
                "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
                "mov        %4, %%"REG_b"               \n\t"
                "push %%"REG_BP"                        \n\t"
                YSCALEYUV2RGB1b(%%REGBP, %5)
                YSCALEYUV2RGB1_ALPHA(%%REGBP)
                WRITEBGR32(%%REGb, 8280(%5), %%REGBP, %%mm2, %%mm4, %%mm5, %%mm7, %%mm0, %%mm1, %%mm3, %%mm6)
                "pop %%"REG_BP"                         \n\t"
                "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"
                :: "c" (buf0), "d" (abuf0), "S" (ubuf0), "D" (ubuf1), "m" (dest),
                   "a" (&c->redDither)
            );
        } else {
            __asm__ volatile(
                "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
                "mov        %4, %%"REG_b"               \n\t"
                "push %%"REG_BP"                        \n\t"
                YSCALEYUV2RGB1b(%%REGBP, %5)
                "pcmpeqd %%mm7, %%mm7                   \n\t"
                WRITEBGR32(%%REGb, 8280(%5), %%REGBP, %%mm2, %%mm4, %%mm5, %%mm7, %%mm0, %%mm1, %%mm3, %%mm6)
                "pop %%"REG_BP"                         \n\t"
                "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"
                :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
                   "a" (&c->redDither)
            );
        }
    }
}

static void RENAME(yuv2bgr24_1)(SwsContext *c, const uint16_t *buf0,
                                const uint16_t *ubuf0, const uint16_t *ubuf1,
                                const uint16_t *vbuf0, const uint16_t *vbuf1,
                                const uint16_t *abuf0, uint8_t *dest,
                                int dstW, int uvalpha, enum PixelFormat dstFormat,
                                int flags, int y)
{
    const uint16_t *buf1= buf0; //FIXME needed for RGB1/BGR1

    if (uvalpha < 2048) { // note this is not correct (shifts chrominance by 0.5 pixels) but it is a bit faster
        __asm__ volatile(
            "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
            "mov        %4, %%"REG_b"               \n\t"
            "push %%"REG_BP"                        \n\t"
            YSCALEYUV2RGB1(%%REGBP, %5)
            "pxor    %%mm7, %%mm7                   \n\t"
            WRITEBGR24(%%REGb, 8280(%5), %%REGBP)
            "pop %%"REG_BP"                         \n\t"
            "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"
            :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
               "a" (&c->redDither)
        );
    } else {
        __asm__ volatile(
            "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
            "mov        %4, %%"REG_b"               \n\t"
            "push %%"REG_BP"                        \n\t"
            YSCALEYUV2RGB1b(%%REGBP, %5)
            "pxor    %%mm7, %%mm7                   \n\t"
            WRITEBGR24(%%REGb, 8280(%5), %%REGBP)
            "pop %%"REG_BP"                         \n\t"
            "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"
            :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
               "a" (&c->redDither)
        );
    }
}

static void RENAME(yuv2rgb555_1)(SwsContext *c, const uint16_t *buf0,
                                 const uint16_t *ubuf0, const uint16_t *ubuf1,
                                 const uint16_t *vbuf0, const uint16_t *vbuf1,
                                 const uint16_t *abuf0, uint8_t *dest,
                                 int dstW, int uvalpha, enum PixelFormat dstFormat,
                                 int flags, int y)
{
    const uint16_t *buf1= buf0; //FIXME needed for RGB1/BGR1

    if (uvalpha < 2048) { // note this is not correct (shifts chrominance by 0.5 pixels) but it is a bit faster
        __asm__ volatile(
            "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
            "mov        %4, %%"REG_b"               \n\t"
            "push %%"REG_BP"                        \n\t"
            YSCALEYUV2RGB1(%%REGBP, %5)
            "pxor    %%mm7, %%mm7                   \n\t"
            /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
            "paddusb "BLUE_DITHER"(%5), %%mm2      \n\t"
            "paddusb "GREEN_DITHER"(%5), %%mm4      \n\t"
            "paddusb "RED_DITHER"(%5), %%mm5      \n\t"
#endif
            WRITERGB15(%%REGb, 8280(%5), %%REGBP)
            "pop %%"REG_BP"                         \n\t"
            "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"
            :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
               "a" (&c->redDither)
        );
    } else {
        __asm__ volatile(
            "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
            "mov        %4, %%"REG_b"               \n\t"
            "push %%"REG_BP"                        \n\t"
            YSCALEYUV2RGB1b(%%REGBP, %5)
            "pxor    %%mm7, %%mm7                   \n\t"
            /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
            "paddusb "BLUE_DITHER"(%5), %%mm2      \n\t"
            "paddusb "GREEN_DITHER"(%5), %%mm4      \n\t"
            "paddusb "RED_DITHER"(%5), %%mm5      \n\t"
#endif
            WRITERGB15(%%REGb, 8280(%5), %%REGBP)
            "pop %%"REG_BP"                         \n\t"
            "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"
            :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
               "a" (&c->redDither)
        );
    }
}

static void RENAME(yuv2rgb565_1)(SwsContext *c, const uint16_t *buf0,
                                 const uint16_t *ubuf0, const uint16_t *ubuf1,
                                 const uint16_t *vbuf0, const uint16_t *vbuf1,
                                 const uint16_t *abuf0, uint8_t *dest,
                                 int dstW, int uvalpha, enum PixelFormat dstFormat,
                                 int flags, int y)
{
    const uint16_t *buf1= buf0; //FIXME needed for RGB1/BGR1

    if (uvalpha < 2048) { // note this is not correct (shifts chrominance by 0.5 pixels) but it is a bit faster
        __asm__ volatile(
            "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
            "mov        %4, %%"REG_b"               \n\t"
            "push %%"REG_BP"                        \n\t"
            YSCALEYUV2RGB1(%%REGBP, %5)
            "pxor    %%mm7, %%mm7                   \n\t"
            /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
            "paddusb "BLUE_DITHER"(%5), %%mm2      \n\t"
            "paddusb "GREEN_DITHER"(%5), %%mm4      \n\t"
            "paddusb "RED_DITHER"(%5), %%mm5      \n\t"
#endif
            WRITERGB16(%%REGb, 8280(%5), %%REGBP)
            "pop %%"REG_BP"                         \n\t"
            "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"
            :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
               "a" (&c->redDither)
        );
    } else {
        __asm__ volatile(
            "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
            "mov        %4, %%"REG_b"               \n\t"
            "push %%"REG_BP"                        \n\t"
            YSCALEYUV2RGB1b(%%REGBP, %5)
            "pxor    %%mm7, %%mm7                   \n\t"
            /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
            "paddusb "BLUE_DITHER"(%5), %%mm2      \n\t"
            "paddusb "GREEN_DITHER"(%5), %%mm4      \n\t"
            "paddusb "RED_DITHER"(%5), %%mm5      \n\t"
#endif
            WRITERGB16(%%REGb, 8280(%5), %%REGBP)
            "pop %%"REG_BP"                         \n\t"
            "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"
            :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
               "a" (&c->redDither)
        );
    }
}

#define REAL_YSCALEYUV2PACKED1(index, c) \
    "xor            "#index", "#index"  \n\t"\
    ".p2align              4            \n\t"\
    "1:                                 \n\t"\
    "movq     (%2, "#index"), %%mm3     \n\t" /* uvbuf0[eax]*/\
    "add           "UV_OFFx2"("#c"), "#index"  \n\t" \
    "movq     (%2, "#index"), %%mm4     \n\t" /* uvbuf0[eax+2048]*/\
    "sub           "UV_OFFx2"("#c"), "#index"  \n\t" \
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
    "add           "UV_OFFx2"("#c"), "#index"  \n\t" \
    "movq     (%2, "#index"), %%mm5     \n\t" /* uvbuf0[eax+2048]*/\
    "movq     (%3, "#index"), %%mm4     \n\t" /* uvbuf1[eax+2048]*/\
    "sub           "UV_OFFx2"("#c"), "#index"  \n\t" \
    "paddw             %%mm2, %%mm3     \n\t" /* uvbuf0[eax] + uvbuf1[eax]*/\
    "paddw             %%mm5, %%mm4     \n\t" /* uvbuf0[eax+2048] + uvbuf1[eax+2048]*/\
    "psrlw                $8, %%mm3     \n\t" \
    "psrlw                $8, %%mm4     \n\t" \
    "movq  (%0, "#index", 2), %%mm1     \n\t" /*buf0[eax]*/\
    "movq 8(%0, "#index", 2), %%mm7     \n\t" /*buf0[eax]*/\
    "psraw                $7, %%mm1     \n\t" \
    "psraw                $7, %%mm7     \n\t"
#define YSCALEYUV2PACKED1b(index, c)  REAL_YSCALEYUV2PACKED1b(index, c)

static void RENAME(yuv2yuyv422_1)(SwsContext *c, const uint16_t *buf0,
                                  const uint16_t *ubuf0, const uint16_t *ubuf1,
                                  const uint16_t *vbuf0, const uint16_t *vbuf1,
                                  const uint16_t *abuf0, uint8_t *dest,
                                  int dstW, int uvalpha, enum PixelFormat dstFormat,
                                  int flags, int y)
{
    const uint16_t *buf1= buf0; //FIXME needed for RGB1/BGR1

    if (uvalpha < 2048) { // note this is not correct (shifts chrominance by 0.5 pixels) but it is a bit faster
        __asm__ volatile(
            "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
            "mov        %4, %%"REG_b"               \n\t"
            "push %%"REG_BP"                        \n\t"
            YSCALEYUV2PACKED1(%%REGBP, %5)
            WRITEYUY2(%%REGb, 8280(%5), %%REGBP)
            "pop %%"REG_BP"                         \n\t"
            "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"
            :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
               "a" (&c->redDither)
        );
    } else {
        __asm__ volatile(
            "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
            "mov        %4, %%"REG_b"               \n\t"
            "push %%"REG_BP"                        \n\t"
            YSCALEYUV2PACKED1b(%%REGBP, %5)
            WRITEYUY2(%%REGb, 8280(%5), %%REGBP)
            "pop %%"REG_BP"                         \n\t"
            "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"
            :: "c" (buf0), "d" (buf1), "S" (ubuf0), "D" (ubuf1), "m" (dest),
               "a" (&c->redDither)
        );
    }
}

#if !COMPILE_TEMPLATE_MMX2
//FIXME yuy2* can read up to 7 samples too much

static void RENAME(yuy2ToY)(uint8_t *dst, const uint8_t *src,
                            int width, uint32_t *unused)
{
    __asm__ volatile(
        "movq "MANGLE(bm01010101)", %%mm2           \n\t"
        "mov                    %0, %%"REG_a"       \n\t"
        "1:                                         \n\t"
        "movq    (%1, %%"REG_a",2), %%mm0           \n\t"
        "movq   8(%1, %%"REG_a",2), %%mm1           \n\t"
        "pand                %%mm2, %%mm0           \n\t"
        "pand                %%mm2, %%mm1           \n\t"
        "packuswb            %%mm1, %%mm0           \n\t"
        "movq                %%mm0, (%2, %%"REG_a") \n\t"
        "add                    $8, %%"REG_a"       \n\t"
        " js                    1b                  \n\t"
        : : "g" ((x86_reg)-width), "r" (src+width*2), "r" (dst+width)
        : "%"REG_a
    );
}

static void RENAME(yuy2ToUV)(uint8_t *dstU, uint8_t *dstV,
                             const uint8_t *src1, const uint8_t *src2,
                             int width, uint32_t *unused)
{
    __asm__ volatile(
        "movq "MANGLE(bm01010101)", %%mm4           \n\t"
        "mov                    %0, %%"REG_a"       \n\t"
        "1:                                         \n\t"
        "movq    (%1, %%"REG_a",4), %%mm0           \n\t"
        "movq   8(%1, %%"REG_a",4), %%mm1           \n\t"
        "psrlw                  $8, %%mm0           \n\t"
        "psrlw                  $8, %%mm1           \n\t"
        "packuswb            %%mm1, %%mm0           \n\t"
        "movq                %%mm0, %%mm1           \n\t"
        "psrlw                  $8, %%mm0           \n\t"
        "pand                %%mm4, %%mm1           \n\t"
        "packuswb            %%mm0, %%mm0           \n\t"
        "packuswb            %%mm1, %%mm1           \n\t"
        "movd                %%mm0, (%3, %%"REG_a") \n\t"
        "movd                %%mm1, (%2, %%"REG_a") \n\t"
        "add                    $4, %%"REG_a"       \n\t"
        " js                    1b                  \n\t"
        : : "g" ((x86_reg)-width), "r" (src1+width*4), "r" (dstU+width), "r" (dstV+width)
        : "%"REG_a
    );
    assert(src1 == src2);
}

static void RENAME(LEToUV)(uint8_t *dstU, uint8_t *dstV,
                           const uint8_t *src1, const uint8_t *src2,
                           int width, uint32_t *unused)
{
    __asm__ volatile(
        "mov                    %0, %%"REG_a"       \n\t"
        "1:                                         \n\t"
        "movq    (%1, %%"REG_a",2), %%mm0           \n\t"
        "movq   8(%1, %%"REG_a",2), %%mm1           \n\t"
        "movq    (%2, %%"REG_a",2), %%mm2           \n\t"
        "movq   8(%2, %%"REG_a",2), %%mm3           \n\t"
        "psrlw                  $8, %%mm0           \n\t"
        "psrlw                  $8, %%mm1           \n\t"
        "psrlw                  $8, %%mm2           \n\t"
        "psrlw                  $8, %%mm3           \n\t"
        "packuswb            %%mm1, %%mm0           \n\t"
        "packuswb            %%mm3, %%mm2           \n\t"
        "movq                %%mm0, (%3, %%"REG_a") \n\t"
        "movq                %%mm2, (%4, %%"REG_a") \n\t"
        "add                    $8, %%"REG_a"       \n\t"
        " js                    1b                  \n\t"
        : : "g" ((x86_reg)-width), "r" (src1+width*2), "r" (src2+width*2), "r" (dstU+width), "r" (dstV+width)
        : "%"REG_a
    );
}

/* This is almost identical to the previous, end exists only because
 * yuy2ToY/UV)(dst, src+1, ...) would have 100% unaligned accesses. */
static void RENAME(uyvyToY)(uint8_t *dst, const uint8_t *src,
                            int width, uint32_t *unused)
{
    __asm__ volatile(
        "mov                  %0, %%"REG_a"         \n\t"
        "1:                                         \n\t"
        "movq  (%1, %%"REG_a",2), %%mm0             \n\t"
        "movq 8(%1, %%"REG_a",2), %%mm1             \n\t"
        "psrlw                $8, %%mm0             \n\t"
        "psrlw                $8, %%mm1             \n\t"
        "packuswb          %%mm1, %%mm0             \n\t"
        "movq              %%mm0, (%2, %%"REG_a")   \n\t"
        "add                  $8, %%"REG_a"         \n\t"
        " js                  1b                    \n\t"
        : : "g" ((x86_reg)-width), "r" (src+width*2), "r" (dst+width)
        : "%"REG_a
    );
}

static void RENAME(uyvyToUV)(uint8_t *dstU, uint8_t *dstV,
                             const uint8_t *src1, const uint8_t *src2,
                             int width, uint32_t *unused)
{
    __asm__ volatile(
        "movq "MANGLE(bm01010101)", %%mm4           \n\t"
        "mov                    %0, %%"REG_a"       \n\t"
        "1:                                         \n\t"
        "movq    (%1, %%"REG_a",4), %%mm0           \n\t"
        "movq   8(%1, %%"REG_a",4), %%mm1           \n\t"
        "pand                %%mm4, %%mm0           \n\t"
        "pand                %%mm4, %%mm1           \n\t"
        "packuswb            %%mm1, %%mm0           \n\t"
        "movq                %%mm0, %%mm1           \n\t"
        "psrlw                  $8, %%mm0           \n\t"
        "pand                %%mm4, %%mm1           \n\t"
        "packuswb            %%mm0, %%mm0           \n\t"
        "packuswb            %%mm1, %%mm1           \n\t"
        "movd                %%mm0, (%3, %%"REG_a") \n\t"
        "movd                %%mm1, (%2, %%"REG_a") \n\t"
        "add                    $4, %%"REG_a"       \n\t"
        " js                    1b                  \n\t"
        : : "g" ((x86_reg)-width), "r" (src1+width*4), "r" (dstU+width), "r" (dstV+width)
        : "%"REG_a
    );
    assert(src1 == src2);
}

static void RENAME(BEToUV)(uint8_t *dstU, uint8_t *dstV,
                           const uint8_t *src1, const uint8_t *src2,
                           int width, uint32_t *unused)
{
    __asm__ volatile(
        "movq "MANGLE(bm01010101)", %%mm4           \n\t"
        "mov                    %0, %%"REG_a"       \n\t"
        "1:                                         \n\t"
        "movq    (%1, %%"REG_a",2), %%mm0           \n\t"
        "movq   8(%1, %%"REG_a",2), %%mm1           \n\t"
        "movq    (%2, %%"REG_a",2), %%mm2           \n\t"
        "movq   8(%2, %%"REG_a",2), %%mm3           \n\t"
        "pand                %%mm4, %%mm0           \n\t"
        "pand                %%mm4, %%mm1           \n\t"
        "pand                %%mm4, %%mm2           \n\t"
        "pand                %%mm4, %%mm3           \n\t"
        "packuswb            %%mm1, %%mm0           \n\t"
        "packuswb            %%mm3, %%mm2           \n\t"
        "movq                %%mm0, (%3, %%"REG_a") \n\t"
        "movq                %%mm2, (%4, %%"REG_a") \n\t"
        "add                    $8, %%"REG_a"       \n\t"
        " js                    1b                  \n\t"
        : : "g" ((x86_reg)-width), "r" (src1+width*2), "r" (src2+width*2), "r" (dstU+width), "r" (dstV+width)
        : "%"REG_a
    );
}

static av_always_inline void RENAME(nvXXtoUV)(uint8_t *dst1, uint8_t *dst2,
                                              const uint8_t *src, int width)
{
    __asm__ volatile(
        "movq "MANGLE(bm01010101)", %%mm4           \n\t"
        "mov                    %0, %%"REG_a"       \n\t"
        "1:                                         \n\t"
        "movq    (%1, %%"REG_a",2), %%mm0           \n\t"
        "movq   8(%1, %%"REG_a",2), %%mm1           \n\t"
        "movq                %%mm0, %%mm2           \n\t"
        "movq                %%mm1, %%mm3           \n\t"
        "pand                %%mm4, %%mm0           \n\t"
        "pand                %%mm4, %%mm1           \n\t"
        "psrlw                  $8, %%mm2           \n\t"
        "psrlw                  $8, %%mm3           \n\t"
        "packuswb            %%mm1, %%mm0           \n\t"
        "packuswb            %%mm3, %%mm2           \n\t"
        "movq                %%mm0, (%2, %%"REG_a") \n\t"
        "movq                %%mm2, (%3, %%"REG_a") \n\t"
        "add                    $8, %%"REG_a"       \n\t"
        " js                    1b                  \n\t"
        : : "g" ((x86_reg)-width), "r" (src+width*2), "r" (dst1+width), "r" (dst2+width)
        : "%"REG_a
    );
}

static void RENAME(nv12ToUV)(uint8_t *dstU, uint8_t *dstV,
                             const uint8_t *src1, const uint8_t *src2,
                             int width, uint32_t *unused)
{
    RENAME(nvXXtoUV)(dstU, dstV, src1, width);
}

static void RENAME(nv21ToUV)(uint8_t *dstU, uint8_t *dstV,
                             const uint8_t *src1, const uint8_t *src2,
                             int width, uint32_t *unused)
{
    RENAME(nvXXtoUV)(dstV, dstU, src1, width);
}
#endif /* !COMPILE_TEMPLATE_MMX2 */

static av_always_inline void RENAME(bgr24ToY_mmx)(int16_t *dst, const uint8_t *src,
                                                  int width, enum PixelFormat srcFormat)
{

    if(srcFormat == PIX_FMT_BGR24) {
        __asm__ volatile(
            "movq  "MANGLE(ff_bgr24toY1Coeff)", %%mm5       \n\t"
            "movq  "MANGLE(ff_bgr24toY2Coeff)", %%mm6       \n\t"
            :
        );
    } else {
        __asm__ volatile(
            "movq  "MANGLE(ff_rgb24toY1Coeff)", %%mm5       \n\t"
            "movq  "MANGLE(ff_rgb24toY2Coeff)", %%mm6       \n\t"
            :
        );
    }

    __asm__ volatile(
        "movq  "MANGLE(ff_bgr24toYOffset)", %%mm4   \n\t"
        "mov                        %2, %%"REG_a"   \n\t"
        "pxor                    %%mm7, %%mm7       \n\t"
        "1:                                         \n\t"
        PREFETCH"               64(%0)              \n\t"
        "movd                     (%0), %%mm0       \n\t"
        "movd                    2(%0), %%mm1       \n\t"
        "movd                    6(%0), %%mm2       \n\t"
        "movd                    8(%0), %%mm3       \n\t"
        "add                       $12, %0          \n\t"
        "punpcklbw               %%mm7, %%mm0       \n\t"
        "punpcklbw               %%mm7, %%mm1       \n\t"
        "punpcklbw               %%mm7, %%mm2       \n\t"
        "punpcklbw               %%mm7, %%mm3       \n\t"
        "pmaddwd                 %%mm5, %%mm0       \n\t"
        "pmaddwd                 %%mm6, %%mm1       \n\t"
        "pmaddwd                 %%mm5, %%mm2       \n\t"
        "pmaddwd                 %%mm6, %%mm3       \n\t"
        "paddd                   %%mm1, %%mm0       \n\t"
        "paddd                   %%mm3, %%mm2       \n\t"
        "paddd                   %%mm4, %%mm0       \n\t"
        "paddd                   %%mm4, %%mm2       \n\t"
        "psrad                     $9, %%mm0       \n\t"
        "psrad                     $9, %%mm2       \n\t"
        "packssdw                %%mm2, %%mm0       \n\t"
        "movq                %%mm0, (%1, %%"REG_a") \n\t"
        "add                        $8, %%"REG_a"   \n\t"
        " js                        1b              \n\t"
    : "+r" (src)
    : "r" (dst+width), "g" ((x86_reg)-2*width)
    : "%"REG_a
    );
}

static void RENAME(bgr24ToY)(int16_t *dst, const uint8_t *src,
                             int width, uint32_t *unused)
{
    RENAME(bgr24ToY_mmx)(dst, src, width, PIX_FMT_BGR24);
}

static void RENAME(rgb24ToY)(int16_t *dst, const uint8_t *src,
                             int width, uint32_t *unused)
{
    RENAME(bgr24ToY_mmx)(dst, src, width, PIX_FMT_RGB24);
}

static av_always_inline void RENAME(bgr24ToUV_mmx)(int16_t *dstU, int16_t *dstV,
                                                   const uint8_t *src, int width,
                                                   enum PixelFormat srcFormat)
{
    __asm__ volatile(
        "movq                    24(%4), %%mm6       \n\t"
        "mov                        %3, %%"REG_a"   \n\t"
        "pxor                    %%mm7, %%mm7       \n\t"
        "1:                                         \n\t"
        PREFETCH"               64(%0)              \n\t"
        "movd                     (%0), %%mm0       \n\t"
        "movd                    2(%0), %%mm1       \n\t"
        "punpcklbw               %%mm7, %%mm0       \n\t"
        "punpcklbw               %%mm7, %%mm1       \n\t"
        "movq                    %%mm0, %%mm2       \n\t"
        "movq                    %%mm1, %%mm3       \n\t"
        "pmaddwd                  (%4), %%mm0       \n\t"
        "pmaddwd                 8(%4), %%mm1       \n\t"
        "pmaddwd                16(%4), %%mm2       \n\t"
        "pmaddwd                 %%mm6, %%mm3       \n\t"
        "paddd                   %%mm1, %%mm0       \n\t"
        "paddd                   %%mm3, %%mm2       \n\t"

        "movd                    6(%0), %%mm1       \n\t"
        "movd                    8(%0), %%mm3       \n\t"
        "add                       $12, %0          \n\t"
        "punpcklbw               %%mm7, %%mm1       \n\t"
        "punpcklbw               %%mm7, %%mm3       \n\t"
        "movq                    %%mm1, %%mm4       \n\t"
        "movq                    %%mm3, %%mm5       \n\t"
        "pmaddwd                  (%4), %%mm1       \n\t"
        "pmaddwd                 8(%4), %%mm3       \n\t"
        "pmaddwd                16(%4), %%mm4       \n\t"
        "pmaddwd                 %%mm6, %%mm5       \n\t"
        "paddd                   %%mm3, %%mm1       \n\t"
        "paddd                   %%mm5, %%mm4       \n\t"

        "movq "MANGLE(ff_bgr24toUVOffset)", %%mm3       \n\t"
        "paddd                   %%mm3, %%mm0       \n\t"
        "paddd                   %%mm3, %%mm2       \n\t"
        "paddd                   %%mm3, %%mm1       \n\t"
        "paddd                   %%mm3, %%mm4       \n\t"
        "psrad                     $9, %%mm0       \n\t"
        "psrad                     $9, %%mm2       \n\t"
        "psrad                     $9, %%mm1       \n\t"
        "psrad                     $9, %%mm4       \n\t"
        "packssdw                %%mm1, %%mm0       \n\t"
        "packssdw                %%mm4, %%mm2       \n\t"
        "movq                %%mm0, (%1, %%"REG_a") \n\t"
        "movq                %%mm2, (%2, %%"REG_a") \n\t"
        "add                        $8, %%"REG_a"   \n\t"
        " js                        1b              \n\t"
    : "+r" (src)
    : "r" (dstU+width), "r" (dstV+width), "g" ((x86_reg)-2*width), "r"(ff_bgr24toUV[srcFormat == PIX_FMT_RGB24])
    : "%"REG_a
    );
}

static void RENAME(bgr24ToUV)(int16_t *dstU, int16_t *dstV,
                              const uint8_t *src1, const uint8_t *src2,
                              int width, uint32_t *unused)
{
    RENAME(bgr24ToUV_mmx)(dstU, dstV, src1, width, PIX_FMT_BGR24);
    assert(src1 == src2);
}

static void RENAME(rgb24ToUV)(int16_t *dstU, int16_t *dstV,
                              const uint8_t *src1, const uint8_t *src2,
                              int width, uint32_t *unused)
{
    assert(src1==src2);
    RENAME(bgr24ToUV_mmx)(dstU, dstV, src1, width, PIX_FMT_RGB24);
}

#if !COMPILE_TEMPLATE_MMX2
// bilinear / bicubic scaling
static void RENAME(hScale)(int16_t *dst, int dstW,
                           const uint8_t *src, int srcW,
                           int xInc, const int16_t *filter,
                           const int16_t *filterPos, int filterSize)
{
    assert(filterSize % 4 == 0 && filterSize>0);
    if (filterSize==4) { // Always true for upscaling, sometimes for down, too.
        x86_reg counter= -2*dstW;
        filter-= counter*2;
        filterPos-= counter/2;
        dst-= counter/2;
        __asm__ volatile(
#if defined(PIC)
            "push            %%"REG_b"              \n\t"
#endif
            "pxor                %%mm7, %%mm7       \n\t"
            "push           %%"REG_BP"              \n\t" // we use 7 regs here ...
            "mov             %%"REG_a", %%"REG_BP"  \n\t"
            ".p2align                4              \n\t"
            "1:                                     \n\t"
            "movzwl   (%2, %%"REG_BP"), %%eax       \n\t"
            "movzwl  2(%2, %%"REG_BP"), %%ebx       \n\t"
            "movq  (%1, %%"REG_BP", 4), %%mm1       \n\t"
            "movq 8(%1, %%"REG_BP", 4), %%mm3       \n\t"
            "movd      (%3, %%"REG_a"), %%mm0       \n\t"
            "movd      (%3, %%"REG_b"), %%mm2       \n\t"
            "punpcklbw           %%mm7, %%mm0       \n\t"
            "punpcklbw           %%mm7, %%mm2       \n\t"
            "pmaddwd             %%mm1, %%mm0       \n\t"
            "pmaddwd             %%mm2, %%mm3       \n\t"
            "movq                %%mm0, %%mm4       \n\t"
            "punpckldq           %%mm3, %%mm0       \n\t"
            "punpckhdq           %%mm3, %%mm4       \n\t"
            "paddd               %%mm4, %%mm0       \n\t"
            "psrad                  $7, %%mm0       \n\t"
            "packssdw            %%mm0, %%mm0       \n\t"
            "movd                %%mm0, (%4, %%"REG_BP")    \n\t"
            "add                    $4, %%"REG_BP"  \n\t"
            " jnc                   1b              \n\t"

            "pop            %%"REG_BP"              \n\t"
#if defined(PIC)
            "pop             %%"REG_b"              \n\t"
#endif
            : "+a" (counter)
            : "c" (filter), "d" (filterPos), "S" (src), "D" (dst)
#if !defined(PIC)
            : "%"REG_b
#endif
        );
    } else if (filterSize==8) {
        x86_reg counter= -2*dstW;
        filter-= counter*4;
        filterPos-= counter/2;
        dst-= counter/2;
        __asm__ volatile(
#if defined(PIC)
            "push             %%"REG_b"             \n\t"
#endif
            "pxor                 %%mm7, %%mm7      \n\t"
            "push            %%"REG_BP"             \n\t" // we use 7 regs here ...
            "mov              %%"REG_a", %%"REG_BP" \n\t"
            ".p2align                 4             \n\t"
            "1:                                     \n\t"
            "movzwl    (%2, %%"REG_BP"), %%eax      \n\t"
            "movzwl   2(%2, %%"REG_BP"), %%ebx      \n\t"
            "movq   (%1, %%"REG_BP", 8), %%mm1      \n\t"
            "movq 16(%1, %%"REG_BP", 8), %%mm3      \n\t"
            "movd       (%3, %%"REG_a"), %%mm0      \n\t"
            "movd       (%3, %%"REG_b"), %%mm2      \n\t"
            "punpcklbw            %%mm7, %%mm0      \n\t"
            "punpcklbw            %%mm7, %%mm2      \n\t"
            "pmaddwd              %%mm1, %%mm0      \n\t"
            "pmaddwd              %%mm2, %%mm3      \n\t"

            "movq  8(%1, %%"REG_BP", 8), %%mm1      \n\t"
            "movq 24(%1, %%"REG_BP", 8), %%mm5      \n\t"
            "movd      4(%3, %%"REG_a"), %%mm4      \n\t"
            "movd      4(%3, %%"REG_b"), %%mm2      \n\t"
            "punpcklbw            %%mm7, %%mm4      \n\t"
            "punpcklbw            %%mm7, %%mm2      \n\t"
            "pmaddwd              %%mm1, %%mm4      \n\t"
            "pmaddwd              %%mm2, %%mm5      \n\t"
            "paddd                %%mm4, %%mm0      \n\t"
            "paddd                %%mm5, %%mm3      \n\t"
            "movq                 %%mm0, %%mm4      \n\t"
            "punpckldq            %%mm3, %%mm0      \n\t"
            "punpckhdq            %%mm3, %%mm4      \n\t"
            "paddd                %%mm4, %%mm0      \n\t"
            "psrad                   $7, %%mm0      \n\t"
            "packssdw             %%mm0, %%mm0      \n\t"
            "movd                 %%mm0, (%4, %%"REG_BP")   \n\t"
            "add                     $4, %%"REG_BP" \n\t"
            " jnc                    1b             \n\t"

            "pop             %%"REG_BP"             \n\t"
#if defined(PIC)
            "pop              %%"REG_b"             \n\t"
#endif
            : "+a" (counter)
            : "c" (filter), "d" (filterPos), "S" (src), "D" (dst)
#if !defined(PIC)
            : "%"REG_b
#endif
        );
    } else {
        const uint8_t *offset = src+filterSize;
        x86_reg counter= -2*dstW;
        //filter-= counter*filterSize/2;
        filterPos-= counter/2;
        dst-= counter/2;
        __asm__ volatile(
            "pxor                  %%mm7, %%mm7     \n\t"
            ".p2align                  4            \n\t"
            "1:                                     \n\t"
            "mov                      %2, %%"REG_c" \n\t"
            "movzwl      (%%"REG_c", %0), %%eax     \n\t"
            "movzwl     2(%%"REG_c", %0), %%edx     \n\t"
            "mov                      %5, %%"REG_c" \n\t"
            "pxor                  %%mm4, %%mm4     \n\t"
            "pxor                  %%mm5, %%mm5     \n\t"
            "2:                                     \n\t"
            "movq                   (%1), %%mm1     \n\t"
            "movq               (%1, %6), %%mm3     \n\t"
            "movd (%%"REG_c", %%"REG_a"), %%mm0     \n\t"
            "movd (%%"REG_c", %%"REG_d"), %%mm2     \n\t"
            "punpcklbw             %%mm7, %%mm0     \n\t"
            "punpcklbw             %%mm7, %%mm2     \n\t"
            "pmaddwd               %%mm1, %%mm0     \n\t"
            "pmaddwd               %%mm2, %%mm3     \n\t"
            "paddd                 %%mm3, %%mm5     \n\t"
            "paddd                 %%mm0, %%mm4     \n\t"
            "add                      $8, %1        \n\t"
            "add                      $4, %%"REG_c" \n\t"
            "cmp                      %4, %%"REG_c" \n\t"
            " jb                      2b            \n\t"
            "add                      %6, %1        \n\t"
            "movq                  %%mm4, %%mm0     \n\t"
            "punpckldq             %%mm5, %%mm4     \n\t"
            "punpckhdq             %%mm5, %%mm0     \n\t"
            "paddd                 %%mm0, %%mm4     \n\t"
            "psrad                    $7, %%mm4     \n\t"
            "packssdw              %%mm4, %%mm4     \n\t"
            "mov                      %3, %%"REG_a" \n\t"
            "movd                  %%mm4, (%%"REG_a", %0)   \n\t"
            "add                      $4, %0        \n\t"
            " jnc                     1b            \n\t"

            : "+r" (counter), "+r" (filter)
            : "m" (filterPos), "m" (dst), "m"(offset),
            "m" (src), "r" ((x86_reg)filterSize*2)
            : "%"REG_a, "%"REG_c, "%"REG_d
        );
    }
}
#endif /* !COMPILE_TEMPLATE_MMX2 */

static inline void RENAME(hScale16)(int16_t *dst, int dstW, const uint16_t *src, int srcW, int xInc,
                                    const int16_t *filter, const int16_t *filterPos, long filterSize, int shift)
{
    int i, j;

    assert(filterSize % 4 == 0 && filterSize>0);
    if (filterSize==4 && shift<15) { // Always true for upscaling, sometimes for down, too.
        x86_reg counter= -2*dstW;
        filter-= counter*2;
        filterPos-= counter/2;
        dst-= counter/2;
        __asm__ volatile(
            "movd                   %5, %%mm7       \n\t"
#if defined(PIC)
            "push            %%"REG_b"              \n\t"
#endif
            "push           %%"REG_BP"              \n\t" // we use 7 regs here ...
            "mov             %%"REG_a", %%"REG_BP"  \n\t"
            ".p2align                4              \n\t"
            "1:                                     \n\t"
            "movzwl   (%2, %%"REG_BP"), %%eax       \n\t"
            "movzwl  2(%2, %%"REG_BP"), %%ebx       \n\t"
            "movq  (%1, %%"REG_BP", 4), %%mm1       \n\t"
            "movq 8(%1, %%"REG_BP", 4), %%mm3       \n\t"
            "movq      (%3, %%"REG_a", 2), %%mm0    \n\t"
            "movq      (%3, %%"REG_b", 2), %%mm2    \n\t"
            "pmaddwd             %%mm1, %%mm0       \n\t"
            "pmaddwd             %%mm2, %%mm3       \n\t"
            "movq                %%mm0, %%mm4       \n\t"
            "punpckldq           %%mm3, %%mm0       \n\t"
            "punpckhdq           %%mm3, %%mm4       \n\t"
            "paddd               %%mm4, %%mm0       \n\t"
            "psrad               %%mm7, %%mm0       \n\t"
            "packssdw            %%mm0, %%mm0       \n\t"
            "movd                %%mm0, (%4, %%"REG_BP")    \n\t"
            "add                    $4, %%"REG_BP"  \n\t"
            " jnc                   1b              \n\t"

            "pop            %%"REG_BP"              \n\t"
#if defined(PIC)
            "pop             %%"REG_b"              \n\t"
#endif
            : "+a" (counter)
            : "c" (filter), "d" (filterPos), "S" (src), "D" (dst), "m"(shift)
#if !defined(PIC)
            : "%"REG_b
#endif
        );
    } else if (filterSize==8 && shift<15) {
        x86_reg counter= -2*dstW;
        filter-= counter*4;
        filterPos-= counter/2;
        dst-= counter/2;
        __asm__ volatile(
            "movd                   %5, %%mm7       \n\t"
#if defined(PIC)
            "push            %%"REG_b"              \n\t"
#endif
            "push            %%"REG_BP"             \n\t" // we use 7 regs here ...
            "mov              %%"REG_a", %%"REG_BP" \n\t"
            ".p2align                 4             \n\t"
            "1:                                     \n\t"
            "movzwl    (%2, %%"REG_BP"), %%eax      \n\t"
            "movzwl   2(%2, %%"REG_BP"), %%ebx      \n\t"
            "movq   (%1, %%"REG_BP", 8), %%mm1      \n\t"
            "movq 16(%1, %%"REG_BP", 8), %%mm3      \n\t"
            "movq       (%3, %%"REG_a", 2), %%mm0   \n\t"
            "movq       (%3, %%"REG_b", 2), %%mm2   \n\t"
            "pmaddwd              %%mm1, %%mm0      \n\t"
            "pmaddwd              %%mm2, %%mm3      \n\t"

            "movq  8(%1, %%"REG_BP", 8), %%mm1      \n\t"
            "movq 24(%1, %%"REG_BP", 8), %%mm5      \n\t"
            "movq      8(%3, %%"REG_a", 2), %%mm4   \n\t"
            "movq      8(%3, %%"REG_b", 2), %%mm2   \n\t"
            "pmaddwd              %%mm1, %%mm4      \n\t"
            "pmaddwd              %%mm2, %%mm5      \n\t"
            "paddd                %%mm4, %%mm0      \n\t"
            "paddd                %%mm5, %%mm3      \n\t"
            "movq                 %%mm0, %%mm4      \n\t"
            "punpckldq            %%mm3, %%mm0      \n\t"
            "punpckhdq            %%mm3, %%mm4      \n\t"
            "paddd                %%mm4, %%mm0      \n\t"
            "psrad                %%mm7, %%mm0      \n\t"
            "packssdw             %%mm0, %%mm0      \n\t"
            "movd                 %%mm0, (%4, %%"REG_BP")   \n\t"
            "add                     $4, %%"REG_BP" \n\t"
            " jnc                    1b             \n\t"

            "pop             %%"REG_BP"             \n\t"
#if defined(PIC)
            "pop             %%"REG_b"              \n\t"
#endif
            : "+a" (counter)
            : "c" (filter), "d" (filterPos), "S" (src), "D" (dst), "m"(shift)
#if !defined(PIC)
            : "%"REG_b
#endif
        );
    } else if (shift<15){
        const uint16_t *offset = src+filterSize;
        x86_reg counter= -2*dstW;
        //filter-= counter*filterSize/2;
        filterPos-= counter/2;
        dst-= counter/2;
        __asm__ volatile(
            "movd                   %7, %%mm7       \n\t"
            ".p2align                  4            \n\t"
            "1:                                     \n\t"
            "mov                      %2, %%"REG_c" \n\t"
            "movzwl      (%%"REG_c", %0), %%eax     \n\t"
            "movzwl     2(%%"REG_c", %0), %%edx     \n\t"
            "mov                      %5, %%"REG_c" \n\t"
            "pxor                  %%mm4, %%mm4     \n\t"
            "pxor                  %%mm5, %%mm5     \n\t"
            "2:                                     \n\t"
            "movq                   (%1), %%mm1     \n\t"
            "movq               (%1, %6), %%mm3     \n\t"
            "movq (%%"REG_c", %%"REG_a", 2), %%mm0     \n\t"
            "movq (%%"REG_c", %%"REG_d", 2), %%mm2     \n\t"
            "pmaddwd               %%mm1, %%mm0     \n\t"
            "pmaddwd               %%mm2, %%mm3     \n\t"
            "paddd                 %%mm3, %%mm5     \n\t"
            "paddd                 %%mm0, %%mm4     \n\t"
            "add                      $8, %1        \n\t"
            "add                      $8, %%"REG_c" \n\t"
            "cmp                      %4, %%"REG_c" \n\t"
            " jb                      2b            \n\t"
            "add                      %6, %1        \n\t"
            "movq                  %%mm4, %%mm0     \n\t"
            "punpckldq             %%mm5, %%mm4     \n\t"
            "punpckhdq             %%mm5, %%mm0     \n\t"
            "paddd                 %%mm0, %%mm4     \n\t"
            "psrad                 %%mm7, %%mm4     \n\t"
            "packssdw              %%mm4, %%mm4     \n\t"
            "mov                      %3, %%"REG_a" \n\t"
            "movd                  %%mm4, (%%"REG_a", %0)   \n\t"
            "add                      $4, %0        \n\t"
            " jnc                     1b            \n\t"

            : "+r" (counter), "+r" (filter)
            : "m" (filterPos), "m" (dst), "m"(offset),
            "m" (src), "r" ((x86_reg)filterSize*2), "m"(shift)
            : "%"REG_a, "%"REG_c, "%"REG_d
        );
    } else
    for (i=0; i<dstW; i++) {
        int srcPos= filterPos[i];
        int val=0;
        for (j=0; j<filterSize; j++) {
            val += ((int)src[srcPos + j])*filter[filterSize*i + j];
        }
        dst[i] = FFMIN(val>>shift, (1<<15)-1); // the cubic equation does overflow ...
    }
}


#if COMPILE_TEMPLATE_MMX2
static void RENAME(hyscale_fast)(SwsContext *c, int16_t *dst,
                                 int dstWidth, const uint8_t *src,
                                 int srcW, int xInc)
{
    int16_t *filterPos = c->hLumFilterPos;
    int16_t *filter    = c->hLumFilter;
    void    *mmx2FilterCode= c->lumMmx2FilterCode;
    int i;
#if defined(PIC)
    DECLARE_ALIGNED(8, uint64_t, ebxsave);
#endif

    __asm__ volatile(
#if defined(PIC)
        "mov               %%"REG_b", %5        \n\t"
#endif
        "pxor                  %%mm7, %%mm7     \n\t"
        "mov                      %0, %%"REG_c" \n\t"
        "mov                      %1, %%"REG_D" \n\t"
        "mov                      %2, %%"REG_d" \n\t"
        "mov                      %3, %%"REG_b" \n\t"
        "xor               %%"REG_a", %%"REG_a" \n\t" // i
        PREFETCH"        (%%"REG_c")            \n\t"
        PREFETCH"      32(%%"REG_c")            \n\t"
        PREFETCH"      64(%%"REG_c")            \n\t"

#if ARCH_X86_64
#define CALL_MMX2_FILTER_CODE \
        "movl            (%%"REG_b"), %%esi     \n\t"\
        "call                    *%4            \n\t"\
        "movl (%%"REG_b", %%"REG_a"), %%esi     \n\t"\
        "add               %%"REG_S", %%"REG_c" \n\t"\
        "add               %%"REG_a", %%"REG_D" \n\t"\
        "xor               %%"REG_a", %%"REG_a" \n\t"\

#else
#define CALL_MMX2_FILTER_CODE \
        "movl (%%"REG_b"), %%esi        \n\t"\
        "call         *%4                       \n\t"\
        "addl (%%"REG_b", %%"REG_a"), %%"REG_c" \n\t"\
        "add               %%"REG_a", %%"REG_D" \n\t"\
        "xor               %%"REG_a", %%"REG_a" \n\t"\

#endif /* ARCH_X86_64 */

        CALL_MMX2_FILTER_CODE
        CALL_MMX2_FILTER_CODE
        CALL_MMX2_FILTER_CODE
        CALL_MMX2_FILTER_CODE
        CALL_MMX2_FILTER_CODE
        CALL_MMX2_FILTER_CODE
        CALL_MMX2_FILTER_CODE
        CALL_MMX2_FILTER_CODE

#if defined(PIC)
        "mov                      %5, %%"REG_b" \n\t"
#endif
        :: "m" (src), "m" (dst), "m" (filter), "m" (filterPos),
           "m" (mmx2FilterCode)
#if defined(PIC)
          ,"m" (ebxsave)
#endif
        : "%"REG_a, "%"REG_c, "%"REG_d, "%"REG_S, "%"REG_D
#if !defined(PIC)
         ,"%"REG_b
#endif
    );

    for (i=dstWidth-1; (i*xInc)>>16 >=srcW-1; i--)
        dst[i] = src[srcW-1]*128;
}

static void RENAME(hcscale_fast)(SwsContext *c, int16_t *dst1, int16_t *dst2,
                                 int dstWidth, const uint8_t *src1,
                                 const uint8_t *src2, int srcW, int xInc)
{
    int16_t *filterPos = c->hChrFilterPos;
    int16_t *filter    = c->hChrFilter;
    void    *mmx2FilterCode= c->chrMmx2FilterCode;
    int i;
#if defined(PIC)
    DECLARE_ALIGNED(8, uint64_t, ebxsave);
#endif

    __asm__ volatile(
#if defined(PIC)
        "mov          %%"REG_b", %7         \n\t"
#endif
        "pxor             %%mm7, %%mm7      \n\t"
        "mov                 %0, %%"REG_c"  \n\t"
        "mov                 %1, %%"REG_D"  \n\t"
        "mov                 %2, %%"REG_d"  \n\t"
        "mov                 %3, %%"REG_b"  \n\t"
        "xor          %%"REG_a", %%"REG_a"  \n\t" // i
        PREFETCH"   (%%"REG_c")             \n\t"
        PREFETCH" 32(%%"REG_c")             \n\t"
        PREFETCH" 64(%%"REG_c")             \n\t"

        CALL_MMX2_FILTER_CODE
        CALL_MMX2_FILTER_CODE
        CALL_MMX2_FILTER_CODE
        CALL_MMX2_FILTER_CODE
        "xor          %%"REG_a", %%"REG_a"  \n\t" // i
        "mov                 %5, %%"REG_c"  \n\t" // src
        "mov                 %6, %%"REG_D"  \n\t" // buf2
        PREFETCH"   (%%"REG_c")             \n\t"
        PREFETCH" 32(%%"REG_c")             \n\t"
        PREFETCH" 64(%%"REG_c")             \n\t"

        CALL_MMX2_FILTER_CODE
        CALL_MMX2_FILTER_CODE
        CALL_MMX2_FILTER_CODE
        CALL_MMX2_FILTER_CODE

#if defined(PIC)
        "mov %7, %%"REG_b"    \n\t"
#endif
        :: "m" (src1), "m" (dst1), "m" (filter), "m" (filterPos),
           "m" (mmx2FilterCode), "m" (src2), "m"(dst2)
#if defined(PIC)
          ,"m" (ebxsave)
#endif
        : "%"REG_a, "%"REG_c, "%"REG_d, "%"REG_S, "%"REG_D
#if !defined(PIC)
         ,"%"REG_b
#endif
    );

    for (i=dstWidth-1; (i*xInc)>>16 >=srcW-1; i--) {
        dst1[i] = src1[srcW-1]*128;
        dst2[i] = src2[srcW-1]*128;
    }
}
#endif /* COMPILE_TEMPLATE_MMX2 */

static av_cold void RENAME(sws_init_swScale)(SwsContext *c)
{
    enum PixelFormat srcFormat = c->srcFormat,
                     dstFormat = c->dstFormat;

    if (!is16BPS(dstFormat) && !is9_OR_10BPS(dstFormat) && dstFormat != PIX_FMT_NV12
        && dstFormat != PIX_FMT_NV21 && !(c->flags & SWS_BITEXACT)) {
            if (c->flags & SWS_ACCURATE_RND) {
                c->yuv2yuv1 = RENAME(yuv2yuv1_ar    );
                c->yuv2yuvX = RENAME(yuv2yuvX_ar    );
                if (!(c->flags & SWS_FULL_CHR_H_INT)) {
                    switch (c->dstFormat) {
                    case PIX_FMT_RGB32:   c->yuv2packedX = RENAME(yuv2rgb32_X_ar);   break;
                    case PIX_FMT_BGR24:   c->yuv2packedX = RENAME(yuv2bgr24_X_ar);   break;
                    case PIX_FMT_RGB555:  c->yuv2packedX = RENAME(yuv2rgb555_X_ar);  break;
                    case PIX_FMT_RGB565:  c->yuv2packedX = RENAME(yuv2rgb565_X_ar);  break;
                    case PIX_FMT_YUYV422: c->yuv2packedX = RENAME(yuv2yuyv422_X_ar); break;
                    default: break;
                    }
                }
            } else {
                int should_dither= isNBPS(c->srcFormat) || is16BPS(c->srcFormat);
                c->yuv2yuv1 = should_dither ? RENAME(yuv2yuv1_ar    ) : RENAME(yuv2yuv1    );
                c->yuv2yuvX = RENAME(yuv2yuvX    );
                if (!(c->flags & SWS_FULL_CHR_H_INT)) {
                    switch (c->dstFormat) {
                    case PIX_FMT_RGB32:   c->yuv2packedX = RENAME(yuv2rgb32_X);   break;
                    case PIX_FMT_BGR24:   c->yuv2packedX = RENAME(yuv2bgr24_X);   break;
                    case PIX_FMT_RGB555:  c->yuv2packedX = RENAME(yuv2rgb555_X);  break;
                    case PIX_FMT_RGB565:  c->yuv2packedX = RENAME(yuv2rgb565_X);  break;
                    case PIX_FMT_YUYV422: c->yuv2packedX = RENAME(yuv2yuyv422_X); break;
                    default: break;
                    }
                }
            }
        if (!(c->flags & SWS_FULL_CHR_H_INT)) {
            switch (c->dstFormat) {
            case PIX_FMT_RGB32:
                c->yuv2packed1 = RENAME(yuv2rgb32_1);
                c->yuv2packed2 = RENAME(yuv2rgb32_2);
                break;
            case PIX_FMT_BGR24:
                c->yuv2packed1 = RENAME(yuv2bgr24_1);
                c->yuv2packed2 = RENAME(yuv2bgr24_2);
                break;
            case PIX_FMT_RGB555:
                c->yuv2packed1 = RENAME(yuv2rgb555_1);
                c->yuv2packed2 = RENAME(yuv2rgb555_2);
                break;
            case PIX_FMT_RGB565:
                c->yuv2packed1 = RENAME(yuv2rgb565_1);
                c->yuv2packed2 = RENAME(yuv2rgb565_2);
                break;
            case PIX_FMT_YUYV422:
                c->yuv2packed1 = RENAME(yuv2yuyv422_1);
                c->yuv2packed2 = RENAME(yuv2yuyv422_2);
                break;
            default:
                break;
            }
        }
    }

#if !COMPILE_TEMPLATE_MMX2
    c->hScale       = RENAME(hScale      );
#endif /* !COMPILE_TEMPLATE_MMX2 */

    // Use the new MMX scaler if the MMX2 one can't be used (it is faster than the x86 ASM one).
#if COMPILE_TEMPLATE_MMX2
    if (c->flags & SWS_FAST_BILINEAR && c->canMMX2BeUsed)
    {
        c->hyscale_fast = RENAME(hyscale_fast);
        c->hcscale_fast = RENAME(hcscale_fast);
    } else {
#endif /* COMPILE_TEMPLATE_MMX2 */
        c->hyscale_fast = NULL;
        c->hcscale_fast = NULL;
#if COMPILE_TEMPLATE_MMX2
    }
#endif /* COMPILE_TEMPLATE_MMX2 */

#if !COMPILE_TEMPLATE_MMX2
    switch(srcFormat) {
        case PIX_FMT_YUYV422  : c->chrToYV12 = RENAME(yuy2ToUV); break;
        case PIX_FMT_UYVY422  : c->chrToYV12 = RENAME(uyvyToUV); break;
        case PIX_FMT_NV12     : c->chrToYV12 = RENAME(nv12ToUV); break;
        case PIX_FMT_NV21     : c->chrToYV12 = RENAME(nv21ToUV); break;
        case PIX_FMT_GRAY16LE :
        case PIX_FMT_YUV420P9LE:
        case PIX_FMT_YUV422P10LE:
        case PIX_FMT_YUV420P10LE:
        case PIX_FMT_YUV420P16LE:
        case PIX_FMT_YUV422P16LE:
        case PIX_FMT_YUV444P16LE: c->hScale16= RENAME(hScale16); break;
    }
#endif /* !COMPILE_TEMPLATE_MMX2 */
    if (!c->chrSrcHSubSample) {
        switch(srcFormat) {
        case PIX_FMT_BGR24  : c->chrToYV12 = RENAME(bgr24ToUV); break;
        case PIX_FMT_RGB24  : c->chrToYV12 = RENAME(rgb24ToUV); break;
        default: break;
        }
    }

    switch (srcFormat) {
#if !COMPILE_TEMPLATE_MMX2
    case PIX_FMT_YUYV422  :
    case PIX_FMT_Y400A    :
                            c->lumToYV12 = RENAME(yuy2ToY); break;
    case PIX_FMT_UYVY422  :
                            c->lumToYV12 = RENAME(uyvyToY); break;
#endif /* !COMPILE_TEMPLATE_MMX2 */
    case PIX_FMT_BGR24    : c->lumToYV12 = RENAME(bgr24ToY); break;
    case PIX_FMT_RGB24    : c->lumToYV12 = RENAME(rgb24ToY); break;
    default: break;
    }
#if !COMPILE_TEMPLATE_MMX2
    if (c->alpPixBuf) {
        switch (srcFormat) {
        case PIX_FMT_Y400A  : c->alpToYV12 = RENAME(yuy2ToY); break;
        default: break;
        }
    }
#endif /* !COMPILE_TEMPLATE_MMX2 */
    if(isAnyRGB(c->srcFormat))
        c->hScale16= RENAME(hScale16);
}
