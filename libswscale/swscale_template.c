/*
 * Copyright (C) 2001-2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * The C code (not assembly, MMX, ...) of this file can be used
 * under the LGPL license.
 */

#undef REAL_MOVNTQ
#undef MOVNTQ
#undef PAVGB
#undef PREFETCH
#undef PREFETCHW
#undef EMMS
#undef SFENCE

#if HAVE_AMD3DNOW
/* On K6 femms is faster than emms. On K7 femms is directly mapped to emms. */
#define EMMS     "femms"
#else
#define EMMS     "emms"
#endif

#if HAVE_AMD3DNOW
#define PREFETCH  "prefetch"
#define PREFETCHW "prefetchw"
#elif HAVE_MMX2
#define PREFETCH "prefetchnta"
#define PREFETCHW "prefetcht0"
#else
#define PREFETCH  " # nop"
#define PREFETCHW " # nop"
#endif

#if HAVE_MMX2
#define SFENCE "sfence"
#else
#define SFENCE " # nop"
#endif

#if HAVE_MMX2
#define PAVGB(a,b) "pavgb " #a ", " #b " \n\t"
#elif HAVE_AMD3DNOW
#define PAVGB(a,b) "pavgusb " #a ", " #b " \n\t"
#endif

#if HAVE_MMX2
#define REAL_MOVNTQ(a,b) "movntq " #a ", " #b " \n\t"
#else
#define REAL_MOVNTQ(a,b) "movq " #a ", " #b " \n\t"
#endif
#define MOVNTQ(a,b)  REAL_MOVNTQ(a,b)

#if HAVE_ALTIVEC
#include "swscale_altivec_template.c"
#endif

#define YSCALEYUV2YV12X(x, offset, dest, width) \
    __asm__ volatile(\
    "xor                          %%"REG_a", %%"REG_a"  \n\t"\
    "movq             "VROUNDER_OFFSET"(%0), %%mm3      \n\t"\
    "movq                             %%mm3, %%mm4      \n\t"\
    "lea                     " offset "(%0), %%"REG_d"  \n\t"\
    "mov                        (%%"REG_d"), %%"REG_S"  \n\t"\
    ASMALIGN(4) /* FIXME Unroll? */\
    "1:                                                 \n\t"\
    "movq                      8(%%"REG_d"), %%mm0      \n\t" /* filterCoeff */\
    "movq   "  x "(%%"REG_S", %%"REG_a", 2), %%mm2      \n\t" /* srcData */\
    "movq 8+"  x "(%%"REG_S", %%"REG_a", 2), %%mm5      \n\t" /* srcData */\
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
    MOVNTQ(%%mm3, (%1, %%REGa))\
    "add                                 $8, %%"REG_a"  \n\t"\
    "cmp                                 %2, %%"REG_a"  \n\t"\
    "movq             "VROUNDER_OFFSET"(%0), %%mm3      \n\t"\
    "movq                             %%mm3, %%mm4      \n\t"\
    "lea                     " offset "(%0), %%"REG_d"  \n\t"\
    "mov                        (%%"REG_d"), %%"REG_S"  \n\t"\
    "jb                                  1b             \n\t"\
    :: "r" (&c->redDither),\
    "r" (dest), "g" (width)\
    : "%"REG_a, "%"REG_d, "%"REG_S\
    );

#define YSCALEYUV2YV12X_ACCURATE(x, offset, dest, width) \
    __asm__ volatile(\
    "lea                     " offset "(%0), %%"REG_d"  \n\t"\
    "xor                          %%"REG_a", %%"REG_a"  \n\t"\
    "pxor                             %%mm4, %%mm4      \n\t"\
    "pxor                             %%mm5, %%mm5      \n\t"\
    "pxor                             %%mm6, %%mm6      \n\t"\
    "pxor                             %%mm7, %%mm7      \n\t"\
    "mov                        (%%"REG_d"), %%"REG_S"  \n\t"\
    ASMALIGN(4) \
    "1:                                                 \n\t"\
    "movq   "  x "(%%"REG_S", %%"REG_a", 2), %%mm0      \n\t" /* srcData */\
    "movq 8+"  x "(%%"REG_S", %%"REG_a", 2), %%mm2      \n\t" /* srcData */\
    "mov        "STR(APCK_PTR2)"(%%"REG_d"), %%"REG_S"  \n\t"\
    "movq   "  x "(%%"REG_S", %%"REG_a", 2), %%mm1      \n\t" /* srcData */\
    "movq                             %%mm0, %%mm3      \n\t"\
    "punpcklwd                        %%mm1, %%mm0      \n\t"\
    "punpckhwd                        %%mm1, %%mm3      \n\t"\
    "movq       "STR(APCK_COEF)"(%%"REG_d"), %%mm1      \n\t" /* filterCoeff */\
    "pmaddwd                          %%mm1, %%mm0      \n\t"\
    "pmaddwd                          %%mm1, %%mm3      \n\t"\
    "paddd                            %%mm0, %%mm4      \n\t"\
    "paddd                            %%mm3, %%mm5      \n\t"\
    "movq 8+"  x "(%%"REG_S", %%"REG_a", 2), %%mm3      \n\t" /* srcData */\
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
    "psrad                              $16, %%mm4      \n\t"\
    "psrad                              $16, %%mm5      \n\t"\
    "psrad                              $16, %%mm6      \n\t"\
    "psrad                              $16, %%mm7      \n\t"\
    "movq             "VROUNDER_OFFSET"(%0), %%mm0      \n\t"\
    "packssdw                         %%mm5, %%mm4      \n\t"\
    "packssdw                         %%mm7, %%mm6      \n\t"\
    "paddw                            %%mm0, %%mm4      \n\t"\
    "paddw                            %%mm0, %%mm6      \n\t"\
    "psraw                               $3, %%mm4      \n\t"\
    "psraw                               $3, %%mm6      \n\t"\
    "packuswb                         %%mm6, %%mm4      \n\t"\
    MOVNTQ(%%mm4, (%1, %%REGa))\
    "add                                 $8, %%"REG_a"  \n\t"\
    "cmp                                 %2, %%"REG_a"  \n\t"\
    "lea                     " offset "(%0), %%"REG_d"  \n\t"\
    "pxor                             %%mm4, %%mm4      \n\t"\
    "pxor                             %%mm5, %%mm5      \n\t"\
    "pxor                             %%mm6, %%mm6      \n\t"\
    "pxor                             %%mm7, %%mm7      \n\t"\
    "mov                        (%%"REG_d"), %%"REG_S"  \n\t"\
    "jb                                  1b             \n\t"\
    :: "r" (&c->redDither),\
    "r" (dest), "g" (width)\
    : "%"REG_a, "%"REG_d, "%"REG_S\
    );

#define YSCALEYUV2YV121 \
    "mov %2, %%"REG_a"                    \n\t"\
    ASMALIGN(4) /* FIXME Unroll? */\
    "1:                                   \n\t"\
    "movq  (%0, %%"REG_a", 2), %%mm0      \n\t"\
    "movq 8(%0, %%"REG_a", 2), %%mm1      \n\t"\
    "psraw                 $7, %%mm0      \n\t"\
    "psraw                 $7, %%mm1      \n\t"\
    "packuswb           %%mm1, %%mm0      \n\t"\
    MOVNTQ(%%mm0, (%1, %%REGa))\
    "add                   $8, %%"REG_a"  \n\t"\
    "jnc                   1b             \n\t"

#define YSCALEYUV2YV121_ACCURATE \
    "mov %2, %%"REG_a"                    \n\t"\
    "pcmpeqw %%mm7, %%mm7                 \n\t"\
    "psrlw                 $15, %%mm7     \n\t"\
    "psllw                  $6, %%mm7     \n\t"\
    ASMALIGN(4) /* FIXME Unroll? */\
    "1:                                   \n\t"\
    "movq  (%0, %%"REG_a", 2), %%mm0      \n\t"\
    "movq 8(%0, %%"REG_a", 2), %%mm1      \n\t"\
    "paddsw             %%mm7, %%mm0      \n\t"\
    "paddsw             %%mm7, %%mm1      \n\t"\
    "psraw                 $7, %%mm0      \n\t"\
    "psraw                 $7, %%mm1      \n\t"\
    "packuswb           %%mm1, %%mm0      \n\t"\
    MOVNTQ(%%mm0, (%1, %%REGa))\
    "add                   $8, %%"REG_a"  \n\t"\
    "jnc                   1b             \n\t"

/*
    :: "m" (-lumFilterSize), "m" (-chrFilterSize),
       "m" (lumMmxFilter+lumFilterSize*4), "m" (chrMmxFilter+chrFilterSize*4),
       "r" (dest), "m" (dstW),
       "m" (lumSrc+lumFilterSize), "m" (chrSrc+chrFilterSize)
    : "%eax", "%ebx", "%ecx", "%edx", "%esi"
*/
#define YSCALEYUV2PACKEDX_UV \
    __asm__ volatile(\
    "xor                   %%"REG_a", %%"REG_a"     \n\t"\
    ASMALIGN(4)\
    "nop                                            \n\t"\
    "1:                                             \n\t"\
    "lea "CHR_MMX_FILTER_OFFSET"(%0), %%"REG_d"     \n\t"\
    "mov                 (%%"REG_d"), %%"REG_S"     \n\t"\
    "movq      "VROUNDER_OFFSET"(%0), %%mm3         \n\t"\
    "movq                      %%mm3, %%mm4         \n\t"\
    ASMALIGN(4)\
    "2:                                             \n\t"\
    "movq               8(%%"REG_d"), %%mm0         \n\t" /* filterCoeff */\
    "movq     (%%"REG_S", %%"REG_a"), %%mm2         \n\t" /* UsrcData */\
    "movq "AV_STRINGIFY(VOF)"(%%"REG_S", %%"REG_a"), %%mm5         \n\t" /* VsrcData */\
    "add                         $16, %%"REG_d"     \n\t"\
    "mov                 (%%"REG_d"), %%"REG_S"     \n\t"\
    "pmulhw                    %%mm0, %%mm2         \n\t"\
    "pmulhw                    %%mm0, %%mm5         \n\t"\
    "paddw                     %%mm2, %%mm3         \n\t"\
    "paddw                     %%mm5, %%mm4         \n\t"\
    "test                  %%"REG_S", %%"REG_S"     \n\t"\
    " jnz                         2b                \n\t"\

#define YSCALEYUV2PACKEDX_YA(offset) \
    "lea                "offset"(%0), %%"REG_d"     \n\t"\
    "mov                 (%%"REG_d"), %%"REG_S"     \n\t"\
    "movq      "VROUNDER_OFFSET"(%0), %%mm1         \n\t"\
    "movq                      %%mm1, %%mm7         \n\t"\
    ASMALIGN(4)\
    "2:                                             \n\t"\
    "movq               8(%%"REG_d"), %%mm0         \n\t" /* filterCoeff */\
    "movq  (%%"REG_S", %%"REG_a", 2), %%mm2         \n\t" /* Y1srcData */\
    "movq 8(%%"REG_S", %%"REG_a", 2), %%mm5         \n\t" /* Y2srcData */\
    "add                         $16, %%"REG_d"            \n\t"\
    "mov                 (%%"REG_d"), %%"REG_S"     \n\t"\
    "pmulhw                    %%mm0, %%mm2         \n\t"\
    "pmulhw                    %%mm0, %%mm5         \n\t"\
    "paddw                     %%mm2, %%mm1         \n\t"\
    "paddw                     %%mm5, %%mm7         \n\t"\
    "test                  %%"REG_S", %%"REG_S"     \n\t"\
    " jnz                         2b                \n\t"\

#define YSCALEYUV2PACKEDX \
    YSCALEYUV2PACKEDX_UV \
    YSCALEYUV2PACKEDX_YA(LUM_MMX_FILTER_OFFSET) \

#define YSCALEYUV2PACKEDX_END                 \
    :: "r" (&c->redDither),                   \
        "m" (dummy), "m" (dummy), "m" (dummy),\
        "r" (dest), "m" (dstW)                \
    : "%"REG_a, "%"REG_d, "%"REG_S            \
    );

#define YSCALEYUV2PACKEDX_ACCURATE_UV \
    __asm__ volatile(\
    "xor %%"REG_a", %%"REG_a"                       \n\t"\
    ASMALIGN(4)\
    "nop                                            \n\t"\
    "1:                                             \n\t"\
    "lea "CHR_MMX_FILTER_OFFSET"(%0), %%"REG_d"     \n\t"\
    "mov                 (%%"REG_d"), %%"REG_S"     \n\t"\
    "pxor                      %%mm4, %%mm4         \n\t"\
    "pxor                      %%mm5, %%mm5         \n\t"\
    "pxor                      %%mm6, %%mm6         \n\t"\
    "pxor                      %%mm7, %%mm7         \n\t"\
    ASMALIGN(4)\
    "2:                                             \n\t"\
    "movq     (%%"REG_S", %%"REG_a"), %%mm0         \n\t" /* UsrcData */\
    "movq "AV_STRINGIFY(VOF)"(%%"REG_S", %%"REG_a"), %%mm2         \n\t" /* VsrcData */\
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
    "movq "AV_STRINGIFY(VOF)"(%%"REG_S", %%"REG_a"), %%mm3         \n\t" /* VsrcData */\
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
    ASMALIGN(4)\
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

#define REAL_YSCALEYUV2PACKED(index, c) \
    "movq "CHR_MMX_FILTER_OFFSET"+8("#c"), %%mm0              \n\t"\
    "movq "LUM_MMX_FILTER_OFFSET"+8("#c"), %%mm1              \n\t"\
    "psraw                $3, %%mm0                           \n\t"\
    "psraw                $3, %%mm1                           \n\t"\
    "movq              %%mm0, "CHR_MMX_FILTER_OFFSET"+8("#c") \n\t"\
    "movq              %%mm1, "LUM_MMX_FILTER_OFFSET"+8("#c") \n\t"\
    "xor            "#index", "#index"                        \n\t"\
    ASMALIGN(4)\
    "1:                                 \n\t"\
    "movq     (%2, "#index"), %%mm2     \n\t" /* uvbuf0[eax]*/\
    "movq     (%3, "#index"), %%mm3     \n\t" /* uvbuf1[eax]*/\
    "movq "AV_STRINGIFY(VOF)"(%2, "#index"), %%mm5     \n\t" /* uvbuf0[eax+2048]*/\
    "movq "AV_STRINGIFY(VOF)"(%3, "#index"), %%mm4     \n\t" /* uvbuf1[eax+2048]*/\
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

#define REAL_YSCALEYUV2RGB_UV(index, c) \
    "xor            "#index", "#index"  \n\t"\
    ASMALIGN(4)\
    "1:                                 \n\t"\
    "movq     (%2, "#index"), %%mm2     \n\t" /* uvbuf0[eax]*/\
    "movq     (%3, "#index"), %%mm3     \n\t" /* uvbuf1[eax]*/\
    "movq "AV_STRINGIFY(VOF)"(%2, "#index"), %%mm5     \n\t" /* uvbuf0[eax+2048]*/\
    "movq "AV_STRINGIFY(VOF)"(%3, "#index"), %%mm4     \n\t" /* uvbuf1[eax+2048]*/\
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

#define REAL_YSCALEYUV2RGB_YA(index, c) \
    "movq  (%0, "#index", 2), %%mm0     \n\t" /*buf0[eax]*/\
    "movq  (%1, "#index", 2), %%mm1     \n\t" /*buf1[eax]*/\
    "movq 8(%0, "#index", 2), %%mm6     \n\t" /*buf0[eax]*/\
    "movq 8(%1, "#index", 2), %%mm7     \n\t" /*buf1[eax]*/\
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

#define YSCALEYUV2RGB_YA(index, c) REAL_YSCALEYUV2RGB_YA(index, c)

#define YSCALEYUV2RGB(index, c) \
    REAL_YSCALEYUV2RGB_UV(index, c) \
    REAL_YSCALEYUV2RGB_YA(index, c) \
    REAL_YSCALEYUV2RGB_COEFF(c)

#define REAL_YSCALEYUV2PACKED1(index, c) \
    "xor            "#index", "#index"  \n\t"\
    ASMALIGN(4)\
    "1:                                 \n\t"\
    "movq     (%2, "#index"), %%mm3     \n\t" /* uvbuf0[eax]*/\
    "movq "AV_STRINGIFY(VOF)"(%2, "#index"), %%mm4     \n\t" /* uvbuf0[eax+2048]*/\
    "psraw                $7, %%mm3     \n\t" \
    "psraw                $7, %%mm4     \n\t" \
    "movq  (%0, "#index", 2), %%mm1     \n\t" /*buf0[eax]*/\
    "movq 8(%0, "#index", 2), %%mm7     \n\t" /*buf0[eax]*/\
    "psraw                $7, %%mm1     \n\t" \
    "psraw                $7, %%mm7     \n\t" \

#define YSCALEYUV2PACKED1(index, c)  REAL_YSCALEYUV2PACKED1(index, c)

#define REAL_YSCALEYUV2RGB1(index, c) \
    "xor            "#index", "#index"  \n\t"\
    ASMALIGN(4)\
    "1:                                 \n\t"\
    "movq     (%2, "#index"), %%mm3     \n\t" /* uvbuf0[eax]*/\
    "movq "AV_STRINGIFY(VOF)"(%2, "#index"), %%mm4     \n\t" /* uvbuf0[eax+2048]*/\
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

#define REAL_YSCALEYUV2PACKED1b(index, c) \
    "xor "#index", "#index"             \n\t"\
    ASMALIGN(4)\
    "1:                                 \n\t"\
    "movq     (%2, "#index"), %%mm2     \n\t" /* uvbuf0[eax]*/\
    "movq     (%3, "#index"), %%mm3     \n\t" /* uvbuf1[eax]*/\
    "movq "AV_STRINGIFY(VOF)"(%2, "#index"), %%mm5     \n\t" /* uvbuf0[eax+2048]*/\
    "movq "AV_STRINGIFY(VOF)"(%3, "#index"), %%mm4     \n\t" /* uvbuf1[eax+2048]*/\
    "paddw             %%mm2, %%mm3     \n\t" /* uvbuf0[eax] + uvbuf1[eax]*/\
    "paddw             %%mm5, %%mm4     \n\t" /* uvbuf0[eax+2048] + uvbuf1[eax+2048]*/\
    "psrlw                $8, %%mm3     \n\t" \
    "psrlw                $8, %%mm4     \n\t" \
    "movq  (%0, "#index", 2), %%mm1     \n\t" /*buf0[eax]*/\
    "movq 8(%0, "#index", 2), %%mm7     \n\t" /*buf0[eax]*/\
    "psraw                $7, %%mm1     \n\t" \
    "psraw                $7, %%mm7     \n\t"
#define YSCALEYUV2PACKED1b(index, c)  REAL_YSCALEYUV2PACKED1b(index, c)

// do vertical chrominance interpolation
#define REAL_YSCALEYUV2RGB1b(index, c) \
    "xor            "#index", "#index"  \n\t"\
    ASMALIGN(4)\
    "1:                                 \n\t"\
    "movq     (%2, "#index"), %%mm2     \n\t" /* uvbuf0[eax]*/\
    "movq     (%3, "#index"), %%mm3     \n\t" /* uvbuf1[eax]*/\
    "movq "AV_STRINGIFY(VOF)"(%2, "#index"), %%mm5     \n\t" /* uvbuf0[eax+2048]*/\
    "movq "AV_STRINGIFY(VOF)"(%3, "#index"), %%mm4     \n\t" /* uvbuf1[eax+2048]*/\
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

#define WRITEBGR24OLD(dst, dstw, index) \
    /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */\
    "movq      %%mm2, %%mm1             \n\t" /* B */\
    "movq      %%mm5, %%mm6             \n\t" /* R */\
    "punpcklbw %%mm4, %%mm2             \n\t" /* GBGBGBGB 0 */\
    "punpcklbw %%mm7, %%mm5             \n\t" /* 0R0R0R0R 0 */\
    "punpckhbw %%mm4, %%mm1             \n\t" /* GBGBGBGB 2 */\
    "punpckhbw %%mm7, %%mm6             \n\t" /* 0R0R0R0R 2 */\
    "movq      %%mm2, %%mm0             \n\t" /* GBGBGBGB 0 */\
    "movq      %%mm1, %%mm3             \n\t" /* GBGBGBGB 2 */\
    "punpcklwd %%mm5, %%mm0             \n\t" /* 0RGB0RGB 0 */\
    "punpckhwd %%mm5, %%mm2             \n\t" /* 0RGB0RGB 1 */\
    "punpcklwd %%mm6, %%mm1             \n\t" /* 0RGB0RGB 2 */\
    "punpckhwd %%mm6, %%mm3             \n\t" /* 0RGB0RGB 3 */\
\
    "movq      %%mm0, %%mm4             \n\t" /* 0RGB0RGB 0 */\
    "psrlq        $8, %%mm0             \n\t" /* 00RGB0RG 0 */\
    "pand "MANGLE(bm00000111)", %%mm4   \n\t" /* 00000RGB 0 */\
    "pand "MANGLE(bm11111000)", %%mm0   \n\t" /* 00RGB000 0.5 */\
    "por       %%mm4, %%mm0             \n\t" /* 00RGBRGB 0 */\
    "movq      %%mm2, %%mm4             \n\t" /* 0RGB0RGB 1 */\
    "psllq       $48, %%mm2             \n\t" /* GB000000 1 */\
    "por       %%mm2, %%mm0             \n\t" /* GBRGBRGB 0 */\
\
    "movq      %%mm4, %%mm2             \n\t" /* 0RGB0RGB 1 */\
    "psrld       $16, %%mm4             \n\t" /* 000R000R 1 */\
    "psrlq       $24, %%mm2             \n\t" /* 0000RGB0 1.5 */\
    "por       %%mm4, %%mm2             \n\t" /* 000RRGBR 1 */\
    "pand "MANGLE(bm00001111)", %%mm2   \n\t" /* 0000RGBR 1 */\
    "movq      %%mm1, %%mm4             \n\t" /* 0RGB0RGB 2 */\
    "psrlq        $8, %%mm1             \n\t" /* 00RGB0RG 2 */\
    "pand "MANGLE(bm00000111)", %%mm4   \n\t" /* 00000RGB 2 */\
    "pand "MANGLE(bm11111000)", %%mm1   \n\t" /* 00RGB000 2.5 */\
    "por       %%mm4, %%mm1             \n\t" /* 00RGBRGB 2 */\
    "movq      %%mm1, %%mm4             \n\t" /* 00RGBRGB 2 */\
    "psllq       $32, %%mm1             \n\t" /* BRGB0000 2 */\
    "por       %%mm1, %%mm2             \n\t" /* BRGBRGBR 1 */\
\
    "psrlq       $32, %%mm4             \n\t" /* 000000RG 2.5 */\
    "movq      %%mm3, %%mm5             \n\t" /* 0RGB0RGB 3 */\
    "psrlq        $8, %%mm3             \n\t" /* 00RGB0RG 3 */\
    "pand "MANGLE(bm00000111)", %%mm5   \n\t" /* 00000RGB 3 */\
    "pand "MANGLE(bm11111000)", %%mm3   \n\t" /* 00RGB000 3.5 */\
    "por       %%mm5, %%mm3             \n\t" /* 00RGBRGB 3 */\
    "psllq       $16, %%mm3             \n\t" /* RGBRGB00 3 */\
    "por       %%mm4, %%mm3             \n\t" /* RGBRGBRG 2.5 */\
\
    MOVNTQ(%%mm0,   (dst))\
    MOVNTQ(%%mm2,  8(dst))\
    MOVNTQ(%%mm3, 16(dst))\
    "add         $24, "#dst"            \n\t"\
\
    "add          $8, "#index"          \n\t"\
    "cmp     "#dstw", "#index"          \n\t"\
    " jb          1b                    \n\t"

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

#if HAVE_MMX2
#undef WRITEBGR24
#define WRITEBGR24(dst, dstw, index)  WRITEBGR24MMX2(dst, dstw, index)
#else
#undef WRITEBGR24
#define WRITEBGR24(dst, dstw, index)  WRITEBGR24MMX(dst, dstw, index)
#endif

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


static inline void RENAME(yuv2yuvX)(SwsContext *c, int16_t *lumFilter, int16_t **lumSrc, int lumFilterSize,
                                    int16_t *chrFilter, int16_t **chrSrc, int chrFilterSize,
                                    uint8_t *dest, uint8_t *uDest, uint8_t *vDest, long dstW, long chrDstW)
{
#if HAVE_MMX
    if(!(c->flags & SWS_BITEXACT)){
        if (c->flags & SWS_ACCURATE_RND){
            if (uDest){
                YSCALEYUV2YV12X_ACCURATE(   "0", CHR_MMX_FILTER_OFFSET, uDest, chrDstW)
                YSCALEYUV2YV12X_ACCURATE(AV_STRINGIFY(VOF), CHR_MMX_FILTER_OFFSET, vDest, chrDstW)
            }

            YSCALEYUV2YV12X_ACCURATE("0", LUM_MMX_FILTER_OFFSET, dest, dstW)
        }else{
            if (uDest){
                YSCALEYUV2YV12X(   "0", CHR_MMX_FILTER_OFFSET, uDest, chrDstW)
                YSCALEYUV2YV12X(AV_STRINGIFY(VOF), CHR_MMX_FILTER_OFFSET, vDest, chrDstW)
            }

            YSCALEYUV2YV12X("0", LUM_MMX_FILTER_OFFSET, dest, dstW)
        }
        return;
    }
#endif
#if HAVE_ALTIVEC
yuv2yuvX_altivec_real(lumFilter, lumSrc, lumFilterSize,
                      chrFilter, chrSrc, chrFilterSize,
                      dest, uDest, vDest, dstW, chrDstW);
#else //HAVE_ALTIVEC
yuv2yuvXinC(lumFilter, lumSrc, lumFilterSize,
            chrFilter, chrSrc, chrFilterSize,
            dest, uDest, vDest, dstW, chrDstW);
#endif //!HAVE_ALTIVEC
}

static inline void RENAME(yuv2nv12X)(SwsContext *c, int16_t *lumFilter, int16_t **lumSrc, int lumFilterSize,
                                     int16_t *chrFilter, int16_t **chrSrc, int chrFilterSize,
                                     uint8_t *dest, uint8_t *uDest, int dstW, int chrDstW, int dstFormat)
{
yuv2nv12XinC(lumFilter, lumSrc, lumFilterSize,
             chrFilter, chrSrc, chrFilterSize,
             dest, uDest, dstW, chrDstW, dstFormat);
}

static inline void RENAME(yuv2yuv1)(SwsContext *c, int16_t *lumSrc, int16_t *chrSrc,
                                    uint8_t *dest, uint8_t *uDest, uint8_t *vDest, long dstW, long chrDstW)
{
    int i;
#if HAVE_MMX
    if(!(c->flags & SWS_BITEXACT)){
        long p= uDest ? 3 : 1;
        uint8_t *src[3]= {lumSrc + dstW, chrSrc + chrDstW, chrSrc + VOFW + chrDstW};
        uint8_t *dst[3]= {dest, uDest, vDest};
        long counter[3] = {dstW, chrDstW, chrDstW};

        if (c->flags & SWS_ACCURATE_RND){
            while(p--){
                __asm__ volatile(
                    YSCALEYUV2YV121_ACCURATE
                    :: "r" (src[p]), "r" (dst[p] + counter[p]),
                    "g" (-counter[p])
                    : "%"REG_a
                );
            }
        }else{
            while(p--){
                __asm__ volatile(
                    YSCALEYUV2YV121
                    :: "r" (src[p]), "r" (dst[p] + counter[p]),
                    "g" (-counter[p])
                    : "%"REG_a
                );
            }
        }
        return;
    }
#endif
    for (i=0; i<dstW; i++)
    {
        int val= (lumSrc[i]+64)>>7;

        if (val&256){
            if (val<0) val=0;
            else       val=255;
        }

        dest[i]= val;
    }

    if (uDest)
        for (i=0; i<chrDstW; i++)
        {
            int u=(chrSrc[i       ]+64)>>7;
            int v=(chrSrc[i + VOFW]+64)>>7;

            if ((u|v)&256){
                if (u<0)        u=0;
                else if (u>255) u=255;
                if (v<0)        v=0;
                else if (v>255) v=255;
            }

            uDest[i]= u;
            vDest[i]= v;
        }
}


/**
 * vertical scale YV12 to RGB
 */
static inline void RENAME(yuv2packedX)(SwsContext *c, int16_t *lumFilter, int16_t **lumSrc, int lumFilterSize,
                                       int16_t *chrFilter, int16_t **chrSrc, int chrFilterSize,
                                       uint8_t *dest, long dstW, long dstY)
{
#if HAVE_MMX
    long dummy=0;
    if(!(c->flags & SWS_BITEXACT)){
        if (c->flags & SWS_ACCURATE_RND){
            switch(c->dstFormat){
            case PIX_FMT_RGB32:
                YSCALEYUV2PACKEDX_ACCURATE
                YSCALEYUV2RGBX
                "pcmpeqd %%mm7, %%mm7 \n\t"
                WRITEBGR32(%4, %5, %%REGa, %%mm2, %%mm4, %%mm5, %%mm7, %%mm0, %%mm1, %%mm3, %%mm6)

                YSCALEYUV2PACKEDX_END
                return;
            case PIX_FMT_BGR24:
                YSCALEYUV2PACKEDX_ACCURATE
                YSCALEYUV2RGBX
                "pxor %%mm7, %%mm7 \n\t"
                "lea (%%"REG_a", %%"REG_a", 2), %%"REG_c"\n\t" //FIXME optimize
                "add %4, %%"REG_c"                        \n\t"
                WRITEBGR24(%%REGc, %5, %%REGa)


                :: "r" (&c->redDither),
                "m" (dummy), "m" (dummy), "m" (dummy),
                "r" (dest), "m" (dstW)
                : "%"REG_a, "%"REG_c, "%"REG_d, "%"REG_S
                );
                return;
            case PIX_FMT_RGB555:
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
                return;
            case PIX_FMT_RGB565:
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
                return;
            case PIX_FMT_YUYV422:
                YSCALEYUV2PACKEDX_ACCURATE
                /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */

                "psraw $3, %%mm3    \n\t"
                "psraw $3, %%mm4    \n\t"
                "psraw $3, %%mm1    \n\t"
                "psraw $3, %%mm7    \n\t"
                WRITEYUY2(%4, %5, %%REGa)
                YSCALEYUV2PACKEDX_END
                return;
            }
        }else{
            switch(c->dstFormat)
            {
            case PIX_FMT_RGB32:
                YSCALEYUV2PACKEDX
                YSCALEYUV2RGBX
                "pcmpeqd %%mm7, %%mm7 \n\t"
                WRITEBGR32(%4, %5, %%REGa, %%mm2, %%mm4, %%mm5, %%mm7, %%mm0, %%mm1, %%mm3, %%mm6)
                YSCALEYUV2PACKEDX_END
                return;
            case PIX_FMT_BGR24:
                YSCALEYUV2PACKEDX
                YSCALEYUV2RGBX
                "pxor                    %%mm7, %%mm7       \n\t"
                "lea (%%"REG_a", %%"REG_a", 2), %%"REG_c"   \n\t" //FIXME optimize
                "add                        %4, %%"REG_c"   \n\t"
                WRITEBGR24(%%REGc, %5, %%REGa)

                :: "r" (&c->redDither),
                "m" (dummy), "m" (dummy), "m" (dummy),
                "r" (dest),  "m" (dstW)
                : "%"REG_a, "%"REG_c, "%"REG_d, "%"REG_S
                );
                return;
            case PIX_FMT_RGB555:
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
                return;
            case PIX_FMT_RGB565:
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
                return;
            case PIX_FMT_YUYV422:
                YSCALEYUV2PACKEDX
                /* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */

                "psraw $3, %%mm3    \n\t"
                "psraw $3, %%mm4    \n\t"
                "psraw $3, %%mm1    \n\t"
                "psraw $3, %%mm7    \n\t"
                WRITEYUY2(%4, %5, %%REGa)
                YSCALEYUV2PACKEDX_END
                return;
            }
        }
    }
#endif /* HAVE_MMX */
#if HAVE_ALTIVEC
    /* The following list of supported dstFormat values should
       match what's found in the body of altivec_yuv2packedX() */
    if (!(c->flags & SWS_BITEXACT) &&
       (c->dstFormat==PIX_FMT_ABGR  || c->dstFormat==PIX_FMT_BGRA  ||
        c->dstFormat==PIX_FMT_BGR24 || c->dstFormat==PIX_FMT_RGB24 ||
        c->dstFormat==PIX_FMT_RGBA  || c->dstFormat==PIX_FMT_ARGB))
            altivec_yuv2packedX (c, lumFilter, lumSrc, lumFilterSize,
                                 chrFilter, chrSrc, chrFilterSize,
                                 dest, dstW, dstY);
    else
#endif
        yuv2packedXinC(c, lumFilter, lumSrc, lumFilterSize,
                       chrFilter, chrSrc, chrFilterSize,
                       dest, dstW, dstY);
}

/**
 * vertical bilinear scale YV12 to RGB
 */
static inline void RENAME(yuv2packed2)(SwsContext *c, uint16_t *buf0, uint16_t *buf1, uint16_t *uvbuf0, uint16_t *uvbuf1,
                          uint8_t *dest, int dstW, int yalpha, int uvalpha, int y)
{
    int  yalpha1=4095- yalpha;
    int uvalpha1=4095-uvalpha;
    int i;

#if HAVE_MMX
    if(!(c->flags & SWS_BITEXACT)){
        switch(c->dstFormat)
        {
            //Note 8280 == DSTW_OFFSET but the preprocessor can't handle that there :(
            case PIX_FMT_RGB32:
                __asm__ volatile(
                "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
                "mov        %4, %%"REG_b"               \n\t"
                "push %%"REG_BP"                        \n\t"
                YSCALEYUV2RGB(%%REGBP, %5)
                "pcmpeqd %%mm7, %%mm7                   \n\t"
                WRITEBGR32(%%REGb, 8280(%5), %%REGBP, %%mm2, %%mm4, %%mm5, %%mm7, %%mm0, %%mm1, %%mm3, %%mm6)
                "pop %%"REG_BP"                         \n\t"
                "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"

                :: "c" (buf0), "d" (buf1), "S" (uvbuf0), "D" (uvbuf1), "m" (dest),
                "a" (&c->redDither)
                );
                return;
            case PIX_FMT_BGR24:
                __asm__ volatile(
                "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
                "mov        %4, %%"REG_b"               \n\t"
                "push %%"REG_BP"                        \n\t"
                YSCALEYUV2RGB(%%REGBP, %5)
                "pxor    %%mm7, %%mm7                   \n\t"
                WRITEBGR24(%%REGb, 8280(%5), %%REGBP)
                "pop %%"REG_BP"                         \n\t"
                "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"
                :: "c" (buf0), "d" (buf1), "S" (uvbuf0), "D" (uvbuf1), "m" (dest),
                "a" (&c->redDither)
                );
                return;
            case PIX_FMT_RGB555:
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

                :: "c" (buf0), "d" (buf1), "S" (uvbuf0), "D" (uvbuf1), "m" (dest),
                "a" (&c->redDither)
                );
                return;
            case PIX_FMT_RGB565:
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
                :: "c" (buf0), "d" (buf1), "S" (uvbuf0), "D" (uvbuf1), "m" (dest),
                "a" (&c->redDither)
                );
                return;
            case PIX_FMT_YUYV422:
                __asm__ volatile(
                "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
                "mov %4, %%"REG_b"                        \n\t"
                "push %%"REG_BP"                        \n\t"
                YSCALEYUV2PACKED(%%REGBP, %5)
                WRITEYUY2(%%REGb, 8280(%5), %%REGBP)
                "pop %%"REG_BP"                         \n\t"
                "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"
                :: "c" (buf0), "d" (buf1), "S" (uvbuf0), "D" (uvbuf1), "m" (dest),
                "a" (&c->redDither)
                );
                return;
            default: break;
        }
    }
#endif //HAVE_MMX
YSCALE_YUV_2_ANYRGB_C(YSCALE_YUV_2_RGB2_C, YSCALE_YUV_2_PACKED2_C, YSCALE_YUV_2_GRAY16_2_C, YSCALE_YUV_2_MONO2_C)
}

/**
 * YV12 to RGB without scaling or interpolating
 */
static inline void RENAME(yuv2packed1)(SwsContext *c, uint16_t *buf0, uint16_t *uvbuf0, uint16_t *uvbuf1,
                          uint8_t *dest, int dstW, int uvalpha, int dstFormat, int flags, int y)
{
    const int yalpha1=0;
    int i;

    uint16_t *buf1= buf0; //FIXME needed for RGB1/BGR1
    const int yalpha= 4096; //FIXME ...

    if (flags&SWS_FULL_CHR_H_INT)
    {
        RENAME(yuv2packed2)(c, buf0, buf0, uvbuf0, uvbuf1, dest, dstW, 0, uvalpha, y);
        return;
    }

#if HAVE_MMX
    if(!(flags & SWS_BITEXACT)){
        if (uvalpha < 2048) // note this is not correct (shifts chrominance by 0.5 pixels) but it is a bit faster
        {
            switch(dstFormat)
            {
            case PIX_FMT_RGB32:
                __asm__ volatile(
                "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
                "mov        %4, %%"REG_b"               \n\t"
                "push %%"REG_BP"                        \n\t"
                YSCALEYUV2RGB1(%%REGBP, %5)
                "pcmpeqd %%mm7, %%mm7                   \n\t"
                WRITEBGR32(%%REGb, 8280(%5), %%REGBP, %%mm2, %%mm4, %%mm5, %%mm7, %%mm0, %%mm1, %%mm3, %%mm6)
                "pop %%"REG_BP"                         \n\t"
                "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"

                :: "c" (buf0), "d" (buf1), "S" (uvbuf0), "D" (uvbuf1), "m" (dest),
                "a" (&c->redDither)
                );
                return;
            case PIX_FMT_BGR24:
                __asm__ volatile(
                "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
                "mov        %4, %%"REG_b"               \n\t"
                "push %%"REG_BP"                        \n\t"
                YSCALEYUV2RGB1(%%REGBP, %5)
                "pxor    %%mm7, %%mm7                   \n\t"
                WRITEBGR24(%%REGb, 8280(%5), %%REGBP)
                "pop %%"REG_BP"                         \n\t"
                "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"

                :: "c" (buf0), "d" (buf1), "S" (uvbuf0), "D" (uvbuf1), "m" (dest),
                "a" (&c->redDither)
                );
                return;
            case PIX_FMT_RGB555:
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

                :: "c" (buf0), "d" (buf1), "S" (uvbuf0), "D" (uvbuf1), "m" (dest),
                "a" (&c->redDither)
                );
                return;
            case PIX_FMT_RGB565:
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

                :: "c" (buf0), "d" (buf1), "S" (uvbuf0), "D" (uvbuf1), "m" (dest),
                "a" (&c->redDither)
                );
                return;
            case PIX_FMT_YUYV422:
                __asm__ volatile(
                "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
                "mov        %4, %%"REG_b"               \n\t"
                "push %%"REG_BP"                        \n\t"
                YSCALEYUV2PACKED1(%%REGBP, %5)
                WRITEYUY2(%%REGb, 8280(%5), %%REGBP)
                "pop %%"REG_BP"                         \n\t"
                "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"

                :: "c" (buf0), "d" (buf1), "S" (uvbuf0), "D" (uvbuf1), "m" (dest),
                "a" (&c->redDither)
                );
                return;
            }
        }
        else
        {
            switch(dstFormat)
            {
            case PIX_FMT_RGB32:
                __asm__ volatile(
                "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
                "mov        %4, %%"REG_b"               \n\t"
                "push %%"REG_BP"                        \n\t"
                YSCALEYUV2RGB1b(%%REGBP, %5)
                "pcmpeqd %%mm7, %%mm7                   \n\t"
                WRITEBGR32(%%REGb, 8280(%5), %%REGBP, %%mm2, %%mm4, %%mm5, %%mm7, %%mm0, %%mm1, %%mm3, %%mm6)
                "pop %%"REG_BP"                         \n\t"
                "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"

                :: "c" (buf0), "d" (buf1), "S" (uvbuf0), "D" (uvbuf1), "m" (dest),
                "a" (&c->redDither)
                );
                return;
            case PIX_FMT_BGR24:
                __asm__ volatile(
                "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
                "mov        %4, %%"REG_b"               \n\t"
                "push %%"REG_BP"                        \n\t"
                YSCALEYUV2RGB1b(%%REGBP, %5)
                "pxor    %%mm7, %%mm7                   \n\t"
                WRITEBGR24(%%REGb, 8280(%5), %%REGBP)
                "pop %%"REG_BP"                         \n\t"
                "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"

                :: "c" (buf0), "d" (buf1), "S" (uvbuf0), "D" (uvbuf1), "m" (dest),
                "a" (&c->redDither)
                );
                return;
            case PIX_FMT_RGB555:
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

                :: "c" (buf0), "d" (buf1), "S" (uvbuf0), "D" (uvbuf1), "m" (dest),
                "a" (&c->redDither)
                );
                return;
            case PIX_FMT_RGB565:
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

                :: "c" (buf0), "d" (buf1), "S" (uvbuf0), "D" (uvbuf1), "m" (dest),
                "a" (&c->redDither)
                );
                return;
            case PIX_FMT_YUYV422:
                __asm__ volatile(
                "mov %%"REG_b", "ESP_OFFSET"(%5)        \n\t"
                "mov        %4, %%"REG_b"               \n\t"
                "push %%"REG_BP"                        \n\t"
                YSCALEYUV2PACKED1b(%%REGBP, %5)
                WRITEYUY2(%%REGb, 8280(%5), %%REGBP)
                "pop %%"REG_BP"                         \n\t"
                "mov "ESP_OFFSET"(%5), %%"REG_b"        \n\t"

                :: "c" (buf0), "d" (buf1), "S" (uvbuf0), "D" (uvbuf1), "m" (dest),
                "a" (&c->redDither)
                );
                return;
            }
        }
    }
#endif /* HAVE_MMX */
    if (uvalpha < 2048)
    {
        YSCALE_YUV_2_ANYRGB_C(YSCALE_YUV_2_RGB1_C, YSCALE_YUV_2_PACKED1_C, YSCALE_YUV_2_GRAY16_1_C, YSCALE_YUV_2_MONO2_C)
    }else{
        YSCALE_YUV_2_ANYRGB_C(YSCALE_YUV_2_RGB1B_C, YSCALE_YUV_2_PACKED1B_C, YSCALE_YUV_2_GRAY16_1_C, YSCALE_YUV_2_MONO2_C)
    }
}

//FIXME yuy2* can read up to 7 samples too much

static inline void RENAME(yuy2ToY)(uint8_t *dst, uint8_t *src, long width, uint32_t *unused)
{
#if HAVE_MMX
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
    : : "g" (-width), "r" (src+width*2), "r" (dst+width)
    : "%"REG_a
    );
#else
    int i;
    for (i=0; i<width; i++)
        dst[i]= src[2*i];
#endif
}

static inline void RENAME(yuy2ToUV)(uint8_t *dstU, uint8_t *dstV, uint8_t *src1, uint8_t *src2, long width, uint32_t *unused)
{
#if HAVE_MMX
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
    : : "g" (-width), "r" (src1+width*4), "r" (dstU+width), "r" (dstV+width)
    : "%"REG_a
    );
#else
    int i;
    for (i=0; i<width; i++)
    {
        dstU[i]= src1[4*i + 1];
        dstV[i]= src1[4*i + 3];
    }
#endif
    assert(src1 == src2);
}

/* This is almost identical to the previous, end exists only because
 * yuy2ToY/UV)(dst, src+1, ...) would have 100% unaligned accesses. */
static inline void RENAME(uyvyToY)(uint8_t *dst, uint8_t *src, long width, uint32_t *unused)
{
#if HAVE_MMX
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
    : : "g" (-width), "r" (src+width*2), "r" (dst+width)
    : "%"REG_a
    );
#else
    int i;
    for (i=0; i<width; i++)
        dst[i]= src[2*i+1];
#endif
}

static inline void RENAME(uyvyToUV)(uint8_t *dstU, uint8_t *dstV, uint8_t *src1, uint8_t *src2, long width, uint32_t *unused)
{
#if HAVE_MMX
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
    : : "g" (-width), "r" (src1+width*4), "r" (dstU+width), "r" (dstV+width)
    : "%"REG_a
    );
#else
    int i;
    for (i=0; i<width; i++)
    {
        dstU[i]= src1[4*i + 0];
        dstV[i]= src1[4*i + 2];
    }
#endif
    assert(src1 == src2);
}

#define BGR2Y(type, name, shr, shg, shb, maskr, maskg, maskb, RY, GY, BY, S)\
static inline void RENAME(name)(uint8_t *dst, uint8_t *src, long width, uint32_t *unused)\
{\
    int i;\
    for (i=0; i<width; i++)\
    {\
        int b= (((type*)src)[i]>>shb)&maskb;\
        int g= (((type*)src)[i]>>shg)&maskg;\
        int r= (((type*)src)[i]>>shr)&maskr;\
\
        dst[i]= (((RY)*r + (GY)*g + (BY)*b + (33<<((S)-1)))>>(S));\
    }\
}

BGR2Y(uint32_t, bgr32ToY,16, 0, 0, 0x00FF, 0xFF00, 0x00FF, RY<< 8, GY   , BY<< 8, RGB2YUV_SHIFT+8)
BGR2Y(uint32_t, rgb32ToY, 0, 0,16, 0x00FF, 0xFF00, 0x00FF, RY<< 8, GY   , BY<< 8, RGB2YUV_SHIFT+8)
BGR2Y(uint16_t, bgr16ToY, 0, 0, 0, 0x001F, 0x07E0, 0xF800, RY<<11, GY<<5, BY    , RGB2YUV_SHIFT+8)
BGR2Y(uint16_t, bgr15ToY, 0, 0, 0, 0x001F, 0x03E0, 0x7C00, RY<<10, GY<<5, BY    , RGB2YUV_SHIFT+7)
BGR2Y(uint16_t, rgb16ToY, 0, 0, 0, 0xF800, 0x07E0, 0x001F, RY    , GY<<5, BY<<11, RGB2YUV_SHIFT+8)
BGR2Y(uint16_t, rgb15ToY, 0, 0, 0, 0x7C00, 0x03E0, 0x001F, RY    , GY<<5, BY<<10, RGB2YUV_SHIFT+7)

#define BGR2UV(type, name, shr, shg, shb, maska, maskr, maskg, maskb, RU, GU, BU, RV, GV, BV, S)\
static inline void RENAME(name)(uint8_t *dstU, uint8_t *dstV, uint8_t *src, uint8_t *dummy, long width, uint32_t *unused)\
{\
    int i;\
    for (i=0; i<width; i++)\
    {\
        int b= (((type*)src)[i]&maskb)>>shb;\
        int g= (((type*)src)[i]&maskg)>>shg;\
        int r= (((type*)src)[i]&maskr)>>shr;\
\
        dstU[i]= ((RU)*r + (GU)*g + (BU)*b + (257<<((S)-1)))>>(S);\
        dstV[i]= ((RV)*r + (GV)*g + (BV)*b + (257<<((S)-1)))>>(S);\
    }\
}\
static inline void RENAME(name ## _half)(uint8_t *dstU, uint8_t *dstV, uint8_t *src, uint8_t *dummy, long width, uint32_t *unused)\
{\
    int i;\
    for (i=0; i<width; i++)\
    {\
        int pix0= ((type*)src)[2*i+0];\
        int pix1= ((type*)src)[2*i+1];\
        int g= (pix0&(maskg|maska))+(pix1&(maskg|maska));\
        int b= ((pix0+pix1-g)&(maskb|(2*maskb)))>>shb;\
        int r= ((pix0+pix1-g)&(maskr|(2*maskr)))>>shr;\
        g&= maskg|(2*maskg);\
\
        g>>=shg;\
\
        dstU[i]= ((RU)*r + (GU)*g + (BU)*b + (257<<(S)))>>((S)+1);\
        dstV[i]= ((RV)*r + (GV)*g + (BV)*b + (257<<(S)))>>((S)+1);\
    }\
}

BGR2UV(uint32_t, bgr32ToUV,16, 0, 0, 0xFF000000, 0xFF0000, 0xFF00,   0x00FF, RU<< 8, GU   , BU<< 8, RV<< 8, GV   , BV<< 8, RGB2YUV_SHIFT+8)
BGR2UV(uint32_t, rgb32ToUV, 0, 0,16, 0xFF000000,   0x00FF, 0xFF00, 0xFF0000, RU<< 8, GU   , BU<< 8, RV<< 8, GV   , BV<< 8, RGB2YUV_SHIFT+8)
BGR2UV(uint16_t, bgr16ToUV, 0, 0, 0,          0,   0x001F, 0x07E0,   0xF800, RU<<11, GU<<5, BU    , RV<<11, GV<<5, BV    , RGB2YUV_SHIFT+8)
BGR2UV(uint16_t, bgr15ToUV, 0, 0, 0,          0,   0x001F, 0x03E0,   0x7C00, RU<<10, GU<<5, BU    , RV<<10, GV<<5, BV    , RGB2YUV_SHIFT+7)
BGR2UV(uint16_t, rgb16ToUV, 0, 0, 0,          0,   0xF800, 0x07E0,   0x001F, RU    , GU<<5, BU<<11, RV    , GV<<5, BV<<11, RGB2YUV_SHIFT+8)
BGR2UV(uint16_t, rgb15ToUV, 0, 0, 0,          0,   0x7C00, 0x03E0,   0x001F, RU    , GU<<5, BU<<10, RV    , GV<<5, BV<<10, RGB2YUV_SHIFT+7)

#if HAVE_MMX
static inline void RENAME(bgr24ToY_mmx)(uint8_t *dst, uint8_t *src, long width, int srcFormat)
{

    if(srcFormat == PIX_FMT_BGR24){
        __asm__ volatile(
            "movq  "MANGLE(ff_bgr24toY1Coeff)", %%mm5       \n\t"
            "movq  "MANGLE(ff_bgr24toY2Coeff)", %%mm6       \n\t"
            :
        );
    }else{
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
        "psrad                     $15, %%mm0       \n\t"
        "psrad                     $15, %%mm2       \n\t"
        "packssdw                %%mm2, %%mm0       \n\t"
        "packuswb                %%mm0, %%mm0       \n\t"
        "movd                %%mm0, (%1, %%"REG_a") \n\t"
        "add                        $4, %%"REG_a"   \n\t"
        " js                        1b              \n\t"
    : "+r" (src)
    : "r" (dst+width), "g" (-width)
    : "%"REG_a
    );
}

static inline void RENAME(bgr24ToUV_mmx)(uint8_t *dstU, uint8_t *dstV, uint8_t *src, long width, int srcFormat)
{
    __asm__ volatile(
        "movq                    24+%4, %%mm6       \n\t"
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
        "pmaddwd                    %4, %%mm0       \n\t"
        "pmaddwd                  8+%4, %%mm1       \n\t"
        "pmaddwd                 16+%4, %%mm2       \n\t"
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
        "pmaddwd                    %4, %%mm1       \n\t"
        "pmaddwd                  8+%4, %%mm3       \n\t"
        "pmaddwd                 16+%4, %%mm4       \n\t"
        "pmaddwd                 %%mm6, %%mm5       \n\t"
        "paddd                   %%mm3, %%mm1       \n\t"
        "paddd                   %%mm5, %%mm4       \n\t"

        "movq "MANGLE(ff_bgr24toUVOffset)", %%mm3       \n\t"
        "paddd                   %%mm3, %%mm0       \n\t"
        "paddd                   %%mm3, %%mm2       \n\t"
        "paddd                   %%mm3, %%mm1       \n\t"
        "paddd                   %%mm3, %%mm4       \n\t"
        "psrad                     $15, %%mm0       \n\t"
        "psrad                     $15, %%mm2       \n\t"
        "psrad                     $15, %%mm1       \n\t"
        "psrad                     $15, %%mm4       \n\t"
        "packssdw                %%mm1, %%mm0       \n\t"
        "packssdw                %%mm4, %%mm2       \n\t"
        "packuswb                %%mm0, %%mm0       \n\t"
        "packuswb                %%mm2, %%mm2       \n\t"
        "movd                %%mm0, (%1, %%"REG_a") \n\t"
        "movd                %%mm2, (%2, %%"REG_a") \n\t"
        "add                        $4, %%"REG_a"   \n\t"
        " js                        1b              \n\t"
    : "+r" (src)
    : "r" (dstU+width), "r" (dstV+width), "g" (-width), "m"(ff_bgr24toUV[srcFormat == PIX_FMT_RGB24][0])
    : "%"REG_a
    );
}
#endif

static inline void RENAME(bgr24ToY)(uint8_t *dst, uint8_t *src, long width, uint32_t *unused)
{
#if HAVE_MMX
    RENAME(bgr24ToY_mmx)(dst, src, width, PIX_FMT_BGR24);
#else
    int i;
    for (i=0; i<width; i++)
    {
        int b= src[i*3+0];
        int g= src[i*3+1];
        int r= src[i*3+2];

        dst[i]= ((RY*r + GY*g + BY*b + (33<<(RGB2YUV_SHIFT-1)))>>RGB2YUV_SHIFT);
    }
#endif /* HAVE_MMX */
}

static inline void RENAME(bgr24ToUV)(uint8_t *dstU, uint8_t *dstV, uint8_t *src1, uint8_t *src2, long width, uint32_t *unused)
{
#if HAVE_MMX
    RENAME(bgr24ToUV_mmx)(dstU, dstV, src1, width, PIX_FMT_BGR24);
#else
    int i;
    for (i=0; i<width; i++)
    {
        int b= src1[3*i + 0];
        int g= src1[3*i + 1];
        int r= src1[3*i + 2];

        dstU[i]= (RU*r + GU*g + BU*b + (257<<(RGB2YUV_SHIFT-1)))>>RGB2YUV_SHIFT;
        dstV[i]= (RV*r + GV*g + BV*b + (257<<(RGB2YUV_SHIFT-1)))>>RGB2YUV_SHIFT;
    }
#endif /* HAVE_MMX */
    assert(src1 == src2);
}

static inline void RENAME(bgr24ToUV_half)(uint8_t *dstU, uint8_t *dstV, uint8_t *src1, uint8_t *src2, long width, uint32_t *unused)
{
    int i;
    for (i=0; i<width; i++)
    {
        int b= src1[6*i + 0] + src1[6*i + 3];
        int g= src1[6*i + 1] + src1[6*i + 4];
        int r= src1[6*i + 2] + src1[6*i + 5];

        dstU[i]= (RU*r + GU*g + BU*b + (257<<RGB2YUV_SHIFT))>>(RGB2YUV_SHIFT+1);
        dstV[i]= (RV*r + GV*g + BV*b + (257<<RGB2YUV_SHIFT))>>(RGB2YUV_SHIFT+1);
    }
    assert(src1 == src2);
}

static inline void RENAME(rgb24ToY)(uint8_t *dst, uint8_t *src, long width, uint32_t *unused)
{
#if HAVE_MMX
    RENAME(bgr24ToY_mmx)(dst, src, width, PIX_FMT_RGB24);
#else
    int i;
    for (i=0; i<width; i++)
    {
        int r= src[i*3+0];
        int g= src[i*3+1];
        int b= src[i*3+2];

        dst[i]= ((RY*r + GY*g + BY*b + (33<<(RGB2YUV_SHIFT-1)))>>RGB2YUV_SHIFT);
    }
#endif
}

static inline void RENAME(rgb24ToUV)(uint8_t *dstU, uint8_t *dstV, uint8_t *src1, uint8_t *src2, long width, uint32_t *unused)
{
#if HAVE_MMX
    assert(src1==src2);
    RENAME(bgr24ToUV_mmx)(dstU, dstV, src1, width, PIX_FMT_RGB24);
#else
    int i;
    assert(src1==src2);
    for (i=0; i<width; i++)
    {
        int r= src1[3*i + 0];
        int g= src1[3*i + 1];
        int b= src1[3*i + 2];

        dstU[i]= (RU*r + GU*g + BU*b + (257<<(RGB2YUV_SHIFT-1)))>>RGB2YUV_SHIFT;
        dstV[i]= (RV*r + GV*g + BV*b + (257<<(RGB2YUV_SHIFT-1)))>>RGB2YUV_SHIFT;
    }
#endif
}

static inline void RENAME(rgb24ToUV_half)(uint8_t *dstU, uint8_t *dstV, uint8_t *src1, uint8_t *src2, long width, uint32_t *unused)
{
    int i;
    assert(src1==src2);
    for (i=0; i<width; i++)
    {
        int r= src1[6*i + 0] + src1[6*i + 3];
        int g= src1[6*i + 1] + src1[6*i + 4];
        int b= src1[6*i + 2] + src1[6*i + 5];

        dstU[i]= (RU*r + GU*g + BU*b + (257<<RGB2YUV_SHIFT))>>(RGB2YUV_SHIFT+1);
        dstV[i]= (RV*r + GV*g + BV*b + (257<<RGB2YUV_SHIFT))>>(RGB2YUV_SHIFT+1);
    }
}


static inline void RENAME(palToY)(uint8_t *dst, uint8_t *src, long width, uint32_t *pal)
{
    int i;
    for (i=0; i<width; i++)
    {
        int d= src[i];

        dst[i]= pal[d] & 0xFF;
    }
}

static inline void RENAME(palToUV)(uint8_t *dstU, uint8_t *dstV, uint8_t *src1, uint8_t *src2, long width, uint32_t *pal)
{
    int i;
    assert(src1 == src2);
    for (i=0; i<width; i++)
    {
        int p= pal[src1[i]];

        dstU[i]= p>>8;
        dstV[i]= p>>16;
    }
}

static inline void RENAME(monowhite2Y)(uint8_t *dst, uint8_t *src, long width, uint32_t *unused)
{
    int i, j;
    for (i=0; i<width/8; i++){
        int d= ~src[i];
        for(j=0; j<8; j++)
            dst[8*i+j]= ((d>>(7-j))&1)*255;
    }
}

static inline void RENAME(monoblack2Y)(uint8_t *dst, uint8_t *src, long width, uint32_t *unused)
{
    int i, j;
    for (i=0; i<width/8; i++){
        int d= src[i];
        for(j=0; j<8; j++)
            dst[8*i+j]= ((d>>(7-j))&1)*255;
    }
}

// bilinear / bicubic scaling
static inline void RENAME(hScale)(int16_t *dst, int dstW, uint8_t *src, int srcW, int xInc,
                                  int16_t *filter, int16_t *filterPos, long filterSize)
{
#if HAVE_MMX
    assert(filterSize % 4 == 0 && filterSize>0);
    if (filterSize==4) // Always true for upscaling, sometimes for down, too.
    {
        long counter= -2*dstW;
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
        ASMALIGN(4)
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
    }
    else if (filterSize==8)
    {
        long counter= -2*dstW;
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
        ASMALIGN(4)
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
    }
    else
    {
        uint8_t *offset = src+filterSize;
        long counter= -2*dstW;
        //filter-= counter*filterSize/2;
        filterPos-= counter/2;
        dst-= counter/2;
        __asm__ volatile(
        "pxor                  %%mm7, %%mm7     \n\t"
        ASMALIGN(4)
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
          "m" (src), "r" (filterSize*2)
        : "%"REG_a, "%"REG_c, "%"REG_d
        );
    }
#else
#if HAVE_ALTIVEC
    hScale_altivec_real(dst, dstW, src, srcW, xInc, filter, filterPos, filterSize);
#else
    int i;
    for (i=0; i<dstW; i++)
    {
        int j;
        int srcPos= filterPos[i];
        int val=0;
        //printf("filterPos: %d\n", filterPos[i]);
        for (j=0; j<filterSize; j++)
        {
            //printf("filter: %d, src: %d\n", filter[i], src[srcPos + j]);
            val += ((int)src[srcPos + j])*filter[filterSize*i + j];
        }
        //filter += hFilterSize;
        dst[i] = FFMIN(val>>7, (1<<15)-1); // the cubic equation does overflow ...
        //dst[i] = val>>7;
    }
#endif /* HAVE_ALTIVEC */
#endif /* HAVE_MMX */
}
      // *** horizontal scale Y line to temp buffer
static inline void RENAME(hyscale)(SwsContext *c, uint16_t *dst, long dstWidth, uint8_t *src, int srcW, int xInc,
                                   int flags, int canMMX2BeUsed, int16_t *hLumFilter,
                                   int16_t *hLumFilterPos, int hLumFilterSize, void *funnyYCode,
                                   int srcFormat, uint8_t *formatConvBuffer, int16_t *mmx2Filter,
                                   int32_t *mmx2FilterPos, uint32_t *pal)
{
    if (srcFormat==PIX_FMT_YUYV422 || srcFormat==PIX_FMT_GRAY16BE)
    {
        RENAME(yuy2ToY)(formatConvBuffer, src, srcW, pal);
        src= formatConvBuffer;
    }
    else if (srcFormat==PIX_FMT_UYVY422 || srcFormat==PIX_FMT_GRAY16LE)
    {
        RENAME(uyvyToY)(formatConvBuffer, src, srcW, pal);
        src= formatConvBuffer;
    }
    else if (srcFormat==PIX_FMT_RGB32)
    {
        RENAME(bgr32ToY)(formatConvBuffer, src, srcW, pal);
        src= formatConvBuffer;
    }
    else if (srcFormat==PIX_FMT_RGB32_1)
    {
        RENAME(bgr32ToY)(formatConvBuffer, src+ALT32_CORR, srcW, pal);
        src= formatConvBuffer;
    }
    else if (srcFormat==PIX_FMT_BGR24)
    {
        RENAME(bgr24ToY)(formatConvBuffer, src, srcW, pal);
        src= formatConvBuffer;
    }
    else if (srcFormat==PIX_FMT_BGR565)
    {
        RENAME(bgr16ToY)(formatConvBuffer, src, srcW, pal);
        src= formatConvBuffer;
    }
    else if (srcFormat==PIX_FMT_BGR555)
    {
        RENAME(bgr15ToY)(formatConvBuffer, src, srcW, pal);
        src= formatConvBuffer;
    }
    else if (srcFormat==PIX_FMT_BGR32)
    {
        RENAME(rgb32ToY)(formatConvBuffer, src, srcW, pal);
        src= formatConvBuffer;
    }
    else if (srcFormat==PIX_FMT_BGR32_1)
    {
        RENAME(rgb32ToY)(formatConvBuffer, src+ALT32_CORR, srcW, pal);
        src= formatConvBuffer;
    }
    else if (srcFormat==PIX_FMT_RGB24)
    {
        RENAME(rgb24ToY)(formatConvBuffer, src, srcW, pal);
        src= formatConvBuffer;
    }
    else if (srcFormat==PIX_FMT_RGB565)
    {
        RENAME(rgb16ToY)(formatConvBuffer, src, srcW, pal);
        src= formatConvBuffer;
    }
    else if (srcFormat==PIX_FMT_RGB555)
    {
        RENAME(rgb15ToY)(formatConvBuffer, src, srcW, pal);
        src= formatConvBuffer;
    }
    else if (srcFormat==PIX_FMT_RGB8 || srcFormat==PIX_FMT_BGR8 || srcFormat==PIX_FMT_PAL8 || srcFormat==PIX_FMT_BGR4_BYTE  || srcFormat==PIX_FMT_RGB4_BYTE)
    {
        RENAME(palToY)(formatConvBuffer, src, srcW, pal);
        src= formatConvBuffer;
    }
    else if (srcFormat==PIX_FMT_MONOBLACK)
    {
        RENAME(monoblack2Y)(formatConvBuffer, src, srcW, pal);
        src= formatConvBuffer;
    }
    else if (srcFormat==PIX_FMT_MONOWHITE)
    {
        RENAME(monowhite2Y)(formatConvBuffer, src, srcW, pal);
        src= formatConvBuffer;
    }

#if HAVE_MMX
    // Use the new MMX scaler if the MMX2 one can't be used (it is faster than the x86 ASM one).
    if (!(flags&SWS_FAST_BILINEAR) || (!canMMX2BeUsed))
#else
    if (!(flags&SWS_FAST_BILINEAR))
#endif
    {
        RENAME(hScale)(dst, dstWidth, src, srcW, xInc, hLumFilter, hLumFilterPos, hLumFilterSize);
    }
    else // fast bilinear upscale / crap downscale
    {
#if ARCH_X86 && CONFIG_GPL
#if HAVE_MMX2
        int i;
#if defined(PIC)
        uint64_t ebxsave __attribute__((aligned(8)));
#endif
        if (canMMX2BeUsed)
        {
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

#define FUNNY_Y_CODE \
            "movl            (%%"REG_b"), %%esi     \n\t"\
            "call                    *%4            \n\t"\
            "movl (%%"REG_b", %%"REG_a"), %%esi     \n\t"\
            "add               %%"REG_S", %%"REG_c" \n\t"\
            "add               %%"REG_a", %%"REG_D" \n\t"\
            "xor               %%"REG_a", %%"REG_a" \n\t"\

#else

#define FUNNY_Y_CODE \
            "movl (%%"REG_b"), %%esi        \n\t"\
            "call         *%4                       \n\t"\
            "addl (%%"REG_b", %%"REG_a"), %%"REG_c" \n\t"\
            "add               %%"REG_a", %%"REG_D" \n\t"\
            "xor               %%"REG_a", %%"REG_a" \n\t"\

#endif /* ARCH_X86_64 */

FUNNY_Y_CODE
FUNNY_Y_CODE
FUNNY_Y_CODE
FUNNY_Y_CODE
FUNNY_Y_CODE
FUNNY_Y_CODE
FUNNY_Y_CODE
FUNNY_Y_CODE

#if defined(PIC)
            "mov                      %5, %%"REG_b" \n\t"
#endif
            :: "m" (src), "m" (dst), "m" (mmx2Filter), "m" (mmx2FilterPos),
            "m" (funnyYCode)
#if defined(PIC)
            ,"m" (ebxsave)
#endif
            : "%"REG_a, "%"REG_c, "%"REG_d, "%"REG_S, "%"REG_D
#if !defined(PIC)
            ,"%"REG_b
#endif
            );
            for (i=dstWidth-1; (i*xInc)>>16 >=srcW-1; i--) dst[i] = src[srcW-1]*128;
        }
        else
        {
#endif /* HAVE_MMX2 */
        long xInc_shr16 = xInc >> 16;
        uint16_t xInc_mask = xInc & 0xffff;
        //NO MMX just normal asm ...
        __asm__ volatile(
        "xor %%"REG_a", %%"REG_a"            \n\t" // i
        "xor %%"REG_d", %%"REG_d"            \n\t" // xx
        "xorl    %%ecx, %%ecx                \n\t" // 2*xalpha
        ASMALIGN(4)
        "1:                                  \n\t"
        "movzbl    (%0, %%"REG_d"), %%edi    \n\t" //src[xx]
        "movzbl   1(%0, %%"REG_d"), %%esi    \n\t" //src[xx+1]
        "subl    %%edi, %%esi                \n\t" //src[xx+1] - src[xx]
        "imull   %%ecx, %%esi                \n\t" //(src[xx+1] - src[xx])*2*xalpha
        "shll      $16, %%edi                \n\t"
        "addl    %%edi, %%esi                \n\t" //src[xx+1]*2*xalpha + src[xx]*(1-2*xalpha)
        "mov        %1, %%"REG_D"            \n\t"
        "shrl       $9, %%esi                \n\t"
        "movw     %%si, (%%"REG_D", %%"REG_a", 2)   \n\t"
        "addw       %4, %%cx                 \n\t" //2*xalpha += xInc&0xFF
        "adc        %3, %%"REG_d"            \n\t" //xx+= xInc>>8 + carry

        "movzbl    (%0, %%"REG_d"), %%edi    \n\t" //src[xx]
        "movzbl   1(%0, %%"REG_d"), %%esi    \n\t" //src[xx+1]
        "subl    %%edi, %%esi                \n\t" //src[xx+1] - src[xx]
        "imull   %%ecx, %%esi                \n\t" //(src[xx+1] - src[xx])*2*xalpha
        "shll      $16, %%edi                \n\t"
        "addl    %%edi, %%esi                \n\t" //src[xx+1]*2*xalpha + src[xx]*(1-2*xalpha)
        "mov        %1, %%"REG_D"            \n\t"
        "shrl       $9, %%esi                \n\t"
        "movw     %%si, 2(%%"REG_D", %%"REG_a", 2)  \n\t"
        "addw       %4, %%cx                 \n\t" //2*xalpha += xInc&0xFF
        "adc        %3, %%"REG_d"            \n\t" //xx+= xInc>>8 + carry


        "add        $2, %%"REG_a"            \n\t"
        "cmp        %2, %%"REG_a"            \n\t"
        " jb        1b                       \n\t"


        :: "r" (src), "m" (dst), "m" (dstWidth), "m" (xInc_shr16), "m" (xInc_mask)
        : "%"REG_a, "%"REG_d, "%ecx", "%"REG_D, "%esi"
        );
#if HAVE_MMX2
        } //if MMX2 can't be used
#endif
#else
        int i;
        unsigned int xpos=0;
        for (i=0;i<dstWidth;i++)
        {
            register unsigned int xx=xpos>>16;
            register unsigned int xalpha=(xpos&0xFFFF)>>9;
            dst[i]= (src[xx]<<7) + (src[xx+1] - src[xx])*xalpha;
            xpos+=xInc;
        }
#endif /* ARCH_X86 */
    }

    if(c->srcRange != c->dstRange && !(isRGB(c->dstFormat) || isBGR(c->dstFormat))){
        int i;
        //FIXME all pal and rgb srcFormats could do this convertion as well
        //FIXME all scalers more complex than bilinear could do half of this transform
        if(c->srcRange){
            for (i=0; i<dstWidth; i++)
                dst[i]= (dst[i]*14071 + 33561947)>>14;
        }else{
            for (i=0; i<dstWidth; i++)
                dst[i]= (FFMIN(dst[i],30189)*19077 - 39057361)>>14;
        }
    }
}

inline static void RENAME(hcscale)(SwsContext *c, uint16_t *dst, long dstWidth, uint8_t *src1, uint8_t *src2,
                                   int srcW, int xInc, int flags, int canMMX2BeUsed, int16_t *hChrFilter,
                                   int16_t *hChrFilterPos, int hChrFilterSize, void *funnyUVCode,
                                   int srcFormat, uint8_t *formatConvBuffer, int16_t *mmx2Filter,
                                   int32_t *mmx2FilterPos, uint32_t *pal)
{
    if (srcFormat==PIX_FMT_YUYV422)
    {
        RENAME(yuy2ToUV)(formatConvBuffer, formatConvBuffer+VOFW, src1, src2, srcW, pal);
        src1= formatConvBuffer;
        src2= formatConvBuffer+VOFW;
    }
    else if (srcFormat==PIX_FMT_UYVY422)
    {
        RENAME(uyvyToUV)(formatConvBuffer, formatConvBuffer+VOFW, src1, src2, srcW, pal);
        src1= formatConvBuffer;
        src2= formatConvBuffer+VOFW;
    }
    else if (srcFormat==PIX_FMT_RGB32)
    {
        if(c->chrSrcHSubSample)
            RENAME(bgr32ToUV_half)(formatConvBuffer, formatConvBuffer+VOFW, src1, src2, srcW, pal);
        else
            RENAME(bgr32ToUV)(formatConvBuffer, formatConvBuffer+VOFW, src1, src2, srcW, pal);
        src1= formatConvBuffer;
        src2= formatConvBuffer+VOFW;
    }
    else if (srcFormat==PIX_FMT_RGB32_1)
    {
        if(c->chrSrcHSubSample)
            RENAME(bgr32ToUV_half)(formatConvBuffer, formatConvBuffer+VOFW, src1+ALT32_CORR, src2+ALT32_CORR, srcW, pal);
        else
            RENAME(bgr32ToUV)(formatConvBuffer, formatConvBuffer+VOFW, src1+ALT32_CORR, src2+ALT32_CORR, srcW, pal);
        src1= formatConvBuffer;
        src2= formatConvBuffer+VOFW;
    }
    else if (srcFormat==PIX_FMT_BGR24)
    {
        if(c->chrSrcHSubSample)
            RENAME(bgr24ToUV_half)(formatConvBuffer, formatConvBuffer+VOFW, src1, src2, srcW, pal);
        else
            RENAME(bgr24ToUV)(formatConvBuffer, formatConvBuffer+VOFW, src1, src2, srcW, pal);
        src1= formatConvBuffer;
        src2= formatConvBuffer+VOFW;
    }
    else if (srcFormat==PIX_FMT_BGR565)
    {
        if(c->chrSrcHSubSample)
            RENAME(bgr16ToUV_half)(formatConvBuffer, formatConvBuffer+VOFW, src1, src2, srcW, pal);
        else
            RENAME(bgr16ToUV)(formatConvBuffer, formatConvBuffer+VOFW, src1, src2, srcW, pal);
        src1= formatConvBuffer;
        src2= formatConvBuffer+VOFW;
    }
    else if (srcFormat==PIX_FMT_BGR555)
    {
        if(c->chrSrcHSubSample)
            RENAME(bgr15ToUV_half)(formatConvBuffer, formatConvBuffer+VOFW, src1, src2, srcW, pal);
        else
            RENAME(bgr15ToUV)(formatConvBuffer, formatConvBuffer+VOFW, src1, src2, srcW, pal);
        src1= formatConvBuffer;
        src2= formatConvBuffer+VOFW;
    }
    else if (srcFormat==PIX_FMT_BGR32)
    {
        if(c->chrSrcHSubSample)
            RENAME(rgb32ToUV_half)(formatConvBuffer, formatConvBuffer+VOFW, src1, src2, srcW, pal);
        else
            RENAME(rgb32ToUV)(formatConvBuffer, formatConvBuffer+VOFW, src1, src2, srcW, pal);
        src1= formatConvBuffer;
        src2= formatConvBuffer+VOFW;
    }
    else if (srcFormat==PIX_FMT_BGR32_1)
    {
        if(c->chrSrcHSubSample)
            RENAME(rgb32ToUV_half)(formatConvBuffer, formatConvBuffer+VOFW, src1+ALT32_CORR, src2+ALT32_CORR, srcW, pal);
        else
            RENAME(rgb32ToUV)(formatConvBuffer, formatConvBuffer+VOFW, src1+ALT32_CORR, src2+ALT32_CORR, srcW, pal);
        src1= formatConvBuffer;
        src2= formatConvBuffer+VOFW;
    }
    else if (srcFormat==PIX_FMT_RGB24)
    {
        if(c->chrSrcHSubSample)
            RENAME(rgb24ToUV_half)(formatConvBuffer, formatConvBuffer+VOFW, src1, src2, srcW, pal);
        else
            RENAME(rgb24ToUV)(formatConvBuffer, formatConvBuffer+VOFW, src1, src2, srcW, pal);
        src1= formatConvBuffer;
        src2= formatConvBuffer+VOFW;
    }
    else if (srcFormat==PIX_FMT_RGB565)
    {
        if(c->chrSrcHSubSample)
            RENAME(rgb16ToUV_half)(formatConvBuffer, formatConvBuffer+VOFW, src1, src2, srcW, pal);
        else
            RENAME(rgb16ToUV)(formatConvBuffer, formatConvBuffer+VOFW, src1, src2, srcW, pal);
        src1= formatConvBuffer;
        src2= formatConvBuffer+VOFW;
    }
    else if (srcFormat==PIX_FMT_RGB555)
    {
        if(c->chrSrcHSubSample)
            RENAME(rgb15ToUV_half)(formatConvBuffer, formatConvBuffer+VOFW, src1, src2, srcW, pal);
        else
            RENAME(rgb15ToUV)(formatConvBuffer, formatConvBuffer+VOFW, src1, src2, srcW, pal);
        src1= formatConvBuffer;
        src2= formatConvBuffer+VOFW;
    }
    else if (isGray(srcFormat) || srcFormat==PIX_FMT_MONOBLACK || srcFormat==PIX_FMT_MONOWHITE)
    {
        return;
    }
    else if (srcFormat==PIX_FMT_RGB8 || srcFormat==PIX_FMT_BGR8 || srcFormat==PIX_FMT_PAL8 || srcFormat==PIX_FMT_BGR4_BYTE  || srcFormat==PIX_FMT_RGB4_BYTE)
    {
        RENAME(palToUV)(formatConvBuffer, formatConvBuffer+VOFW, src1, src2, srcW, pal);
        src1= formatConvBuffer;
        src2= formatConvBuffer+VOFW;
    }

#if HAVE_MMX
    // Use the new MMX scaler if the MMX2 one can't be used (it is faster than the x86 ASM one).
    if (!(flags&SWS_FAST_BILINEAR) || (!canMMX2BeUsed))
#else
    if (!(flags&SWS_FAST_BILINEAR))
#endif
    {
        RENAME(hScale)(dst     , dstWidth, src1, srcW, xInc, hChrFilter, hChrFilterPos, hChrFilterSize);
        RENAME(hScale)(dst+VOFW, dstWidth, src2, srcW, xInc, hChrFilter, hChrFilterPos, hChrFilterSize);
    }
    else // fast bilinear upscale / crap downscale
    {
#if ARCH_X86 && CONFIG_GPL
#if HAVE_MMX2
        int i;
#if defined(PIC)
        uint64_t ebxsave __attribute__((aligned(8)));
#endif
        if (canMMX2BeUsed)
        {
            __asm__ volatile(
#if defined(PIC)
            "mov          %%"REG_b", %6         \n\t"
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

#if ARCH_X86_64

#define FUNNY_UV_CODE \
            "movl       (%%"REG_b"), %%esi      \n\t"\
            "call               *%4             \n\t"\
            "movl (%%"REG_b", %%"REG_a"), %%esi \n\t"\
            "add          %%"REG_S", %%"REG_c"  \n\t"\
            "add          %%"REG_a", %%"REG_D"  \n\t"\
            "xor          %%"REG_a", %%"REG_a"  \n\t"\

#else

#define FUNNY_UV_CODE \
            "movl       (%%"REG_b"), %%esi      \n\t"\
            "call               *%4             \n\t"\
            "addl (%%"REG_b", %%"REG_a"), %%"REG_c" \n\t"\
            "add          %%"REG_a", %%"REG_D"  \n\t"\
            "xor          %%"REG_a", %%"REG_a"  \n\t"\

#endif /* ARCH_X86_64 */

FUNNY_UV_CODE
FUNNY_UV_CODE
FUNNY_UV_CODE
FUNNY_UV_CODE
            "xor          %%"REG_a", %%"REG_a"  \n\t" // i
            "mov                 %5, %%"REG_c"  \n\t" // src
            "mov                 %1, %%"REG_D"  \n\t" // buf1
            "add              $"AV_STRINGIFY(VOF)", %%"REG_D"  \n\t"
            PREFETCH"   (%%"REG_c")             \n\t"
            PREFETCH" 32(%%"REG_c")             \n\t"
            PREFETCH" 64(%%"REG_c")             \n\t"

FUNNY_UV_CODE
FUNNY_UV_CODE
FUNNY_UV_CODE
FUNNY_UV_CODE

#if defined(PIC)
            "mov %6, %%"REG_b"    \n\t"
#endif
            :: "m" (src1), "m" (dst), "m" (mmx2Filter), "m" (mmx2FilterPos),
            "m" (funnyUVCode), "m" (src2)
#if defined(PIC)
            ,"m" (ebxsave)
#endif
            : "%"REG_a, "%"REG_c, "%"REG_d, "%"REG_S, "%"REG_D
#if !defined(PIC)
             ,"%"REG_b
#endif
            );
            for (i=dstWidth-1; (i*xInc)>>16 >=srcW-1; i--)
            {
                //printf("%d %d %d\n", dstWidth, i, srcW);
                dst[i] = src1[srcW-1]*128;
                dst[i+VOFW] = src2[srcW-1]*128;
            }
        }
        else
        {
#endif /* HAVE_MMX2 */
            long xInc_shr16 = (long) (xInc >> 16);
            uint16_t xInc_mask = xInc & 0xffff;
            __asm__ volatile(
            "xor %%"REG_a", %%"REG_a"               \n\t" // i
            "xor %%"REG_d", %%"REG_d"               \n\t" // xx
            "xorl    %%ecx, %%ecx                   \n\t" // 2*xalpha
            ASMALIGN(4)
            "1:                                     \n\t"
            "mov        %0, %%"REG_S"               \n\t"
            "movzbl  (%%"REG_S", %%"REG_d"), %%edi  \n\t" //src[xx]
            "movzbl 1(%%"REG_S", %%"REG_d"), %%esi  \n\t" //src[xx+1]
            "subl    %%edi, %%esi                   \n\t" //src[xx+1] - src[xx]
            "imull   %%ecx, %%esi                   \n\t" //(src[xx+1] - src[xx])*2*xalpha
            "shll      $16, %%edi                   \n\t"
            "addl    %%edi, %%esi                   \n\t" //src[xx+1]*2*xalpha + src[xx]*(1-2*xalpha)
            "mov        %1, %%"REG_D"               \n\t"
            "shrl       $9, %%esi                   \n\t"
            "movw     %%si, (%%"REG_D", %%"REG_a", 2)   \n\t"

            "movzbl    (%5, %%"REG_d"), %%edi       \n\t" //src[xx]
            "movzbl   1(%5, %%"REG_d"), %%esi       \n\t" //src[xx+1]
            "subl    %%edi, %%esi                   \n\t" //src[xx+1] - src[xx]
            "imull   %%ecx, %%esi                   \n\t" //(src[xx+1] - src[xx])*2*xalpha
            "shll      $16, %%edi                   \n\t"
            "addl    %%edi, %%esi                   \n\t" //src[xx+1]*2*xalpha + src[xx]*(1-2*xalpha)
            "mov        %1, %%"REG_D"               \n\t"
            "shrl       $9, %%esi                   \n\t"
            "movw     %%si, "AV_STRINGIFY(VOF)"(%%"REG_D", %%"REG_a", 2)   \n\t"

            "addw       %4, %%cx                    \n\t" //2*xalpha += xInc&0xFF
            "adc        %3, %%"REG_d"               \n\t" //xx+= xInc>>8 + carry
            "add        $1, %%"REG_a"               \n\t"
            "cmp        %2, %%"REG_a"               \n\t"
            " jb        1b                          \n\t"

/* GCC 3.3 makes MPlayer crash on IA-32 machines when using "g" operand here,
   which is needed to support GCC 4.0. */
#if ARCH_X86_64 && ((__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4))
            :: "m" (src1), "m" (dst), "g" ((long)dstWidth), "m" (xInc_shr16), "m" (xInc_mask),
#else
            :: "m" (src1), "m" (dst), "m" ((long)dstWidth), "m" (xInc_shr16), "m" (xInc_mask),
#endif
            "r" (src2)
            : "%"REG_a, "%"REG_d, "%ecx", "%"REG_D, "%esi"
            );
#if HAVE_MMX2
        } //if MMX2 can't be used
#endif
#else
        int i;
        unsigned int xpos=0;
        for (i=0;i<dstWidth;i++)
        {
            register unsigned int xx=xpos>>16;
            register unsigned int xalpha=(xpos&0xFFFF)>>9;
            dst[i]=(src1[xx]*(xalpha^127)+src1[xx+1]*xalpha);
            dst[i+VOFW]=(src2[xx]*(xalpha^127)+src2[xx+1]*xalpha);
            /* slower
            dst[i]= (src1[xx]<<7) + (src1[xx+1] - src1[xx])*xalpha;
            dst[i+VOFW]=(src2[xx]<<7) + (src2[xx+1] - src2[xx])*xalpha;
            */
            xpos+=xInc;
        }
#endif /* ARCH_X86 */
    }
    if(c->srcRange != c->dstRange && !(isRGB(c->dstFormat) || isBGR(c->dstFormat))){
        int i;
        //FIXME all pal and rgb srcFormats could do this convertion as well
        //FIXME all scalers more complex than bilinear could do half of this transform
        if(c->srcRange){
            for (i=0; i<dstWidth; i++){
                dst[i     ]= (dst[i     ]*1799 + 4081085)>>11; //1469
                dst[i+VOFW]= (dst[i+VOFW]*1799 + 4081085)>>11; //1469
            }
        }else{
            for (i=0; i<dstWidth; i++){
                dst[i     ]= (FFMIN(dst[i     ],30775)*4663 - 9289992)>>12; //-264
                dst[i+VOFW]= (FFMIN(dst[i+VOFW],30775)*4663 - 9289992)>>12; //-264
            }
        }
    }
}

static int RENAME(swScale)(SwsContext *c, uint8_t* src[], int srcStride[], int srcSliceY,
                           int srcSliceH, uint8_t* dst[], int dstStride[]){

    /* load a few things into local vars to make the code more readable? and faster */
    const int srcW= c->srcW;
    const int dstW= c->dstW;
    const int dstH= c->dstH;
    const int chrDstW= c->chrDstW;
    const int chrSrcW= c->chrSrcW;
    const int lumXInc= c->lumXInc;
    const int chrXInc= c->chrXInc;
    const int dstFormat= c->dstFormat;
    const int srcFormat= c->srcFormat;
    const int flags= c->flags;
    const int canMMX2BeUsed= c->canMMX2BeUsed;
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
    const int vLumFilterSize= c->vLumFilterSize;
    const int vChrFilterSize= c->vChrFilterSize;
    const int hLumFilterSize= c->hLumFilterSize;
    const int hChrFilterSize= c->hChrFilterSize;
    int16_t **lumPixBuf= c->lumPixBuf;
    int16_t **chrPixBuf= c->chrPixBuf;
    const int vLumBufSize= c->vLumBufSize;
    const int vChrBufSize= c->vChrBufSize;
    uint8_t *funnyYCode= c->funnyYCode;
    uint8_t *funnyUVCode= c->funnyUVCode;
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

    if (isPacked(c->srcFormat)){
        src[0]=
        src[1]=
        src[2]= src[0];
        srcStride[0]=
        srcStride[1]=
        srcStride[2]= srcStride[0];
    }
    srcStride[1]<<= c->vChrDrop;
    srcStride[2]<<= c->vChrDrop;

    //printf("swscale %X %X %X -> %X %X %X\n", (int)src[0], (int)src[1], (int)src[2],
    //       (int)dst[0], (int)dst[1], (int)dst[2]);

#if 0 //self test FIXME move to a vfilter or something
    {
    static volatile int i=0;
    i++;
    if (srcFormat==PIX_FMT_YUV420P && i==1 && srcSliceH>= c->srcH)
        selfTest(src, srcStride, c->srcW, c->srcH);
    i--;
    }
#endif

    //printf("sws Strides:%d %d %d -> %d %d %d\n", srcStride[0],srcStride[1],srcStride[2],
    //dstStride[0],dstStride[1],dstStride[2]);

    if (dstStride[0]%8 !=0 || dstStride[1]%8 !=0 || dstStride[2]%8 !=0)
    {
        static int warnedAlready=0; //FIXME move this into the context perhaps
        if (flags & SWS_PRINT_INFO && !warnedAlready)
        {
            av_log(c, AV_LOG_WARNING, "Warning: dstStride is not aligned!\n"
                   "         ->cannot do aligned memory accesses anymore\n");
            warnedAlready=1;
        }
    }

    /* Note the user might start scaling the picture in the middle so this
       will not get executed. This is not really intended but works
       currently, so people might do it. */
    if (srcSliceY ==0){
        lumBufIndex=0;
        chrBufIndex=0;
        dstY=0;
        lastInLumBuf= -1;
        lastInChrBuf= -1;
    }

    lastDstY= dstY;

    for (;dstY < dstH; dstY++){
        unsigned char *dest =dst[0]+dstStride[0]*dstY;
        const int chrDstY= dstY>>c->chrDstVSubSample;
        unsigned char *uDest=dst[1]+dstStride[1]*chrDstY;
        unsigned char *vDest=dst[2]+dstStride[2]*chrDstY;

        const int firstLumSrcY= vLumFilterPos[dstY]; //First line needed as input
        const int firstChrSrcY= vChrFilterPos[chrDstY]; //First line needed as input
        const int lastLumSrcY= firstLumSrcY + vLumFilterSize -1; // Last line needed as input
        const int lastChrSrcY= firstChrSrcY + vChrFilterSize -1; // Last line needed as input

        //printf("dstY:%d dstH:%d firstLumSrcY:%d lastInLumBuf:%d vLumBufSize: %d vChrBufSize: %d slice: %d %d vLumFilterSize: %d firstChrSrcY: %d vChrFilterSize: %d c->chrSrcVSubSample: %d\n",
        // dstY, dstH, firstLumSrcY, lastInLumBuf, vLumBufSize, vChrBufSize, srcSliceY, srcSliceH, vLumFilterSize, firstChrSrcY, vChrFilterSize,  c->chrSrcVSubSample);
        //handle holes (FAST_BILINEAR & weird filters)
        if (firstLumSrcY > lastInLumBuf) lastInLumBuf= firstLumSrcY-1;
        if (firstChrSrcY > lastInChrBuf) lastInChrBuf= firstChrSrcY-1;
        //printf("%d %d %d\n", firstChrSrcY, lastInChrBuf, vChrBufSize);
        assert(firstLumSrcY >= lastInLumBuf - vLumBufSize + 1);
        assert(firstChrSrcY >= lastInChrBuf - vChrBufSize + 1);

        // Do we have enough lines in this slice to output the dstY line
        if (lastLumSrcY < srcSliceY + srcSliceH && lastChrSrcY < -((-srcSliceY - srcSliceH)>>c->chrSrcVSubSample))
        {
            //Do horizontal scaling
            while(lastInLumBuf < lastLumSrcY)
            {
                uint8_t *s= src[0]+(lastInLumBuf + 1 - srcSliceY)*srcStride[0];
                lumBufIndex++;
                //printf("%d %d %d %d\n", lumBufIndex, vLumBufSize, lastInLumBuf,  lastLumSrcY);
                assert(lumBufIndex < 2*vLumBufSize);
                assert(lastInLumBuf + 1 - srcSliceY < srcSliceH);
                assert(lastInLumBuf + 1 - srcSliceY >= 0);
                //printf("%d %d\n", lumBufIndex, vLumBufSize);
                RENAME(hyscale)(c, lumPixBuf[ lumBufIndex ], dstW, s, srcW, lumXInc,
                                flags, canMMX2BeUsed, hLumFilter, hLumFilterPos, hLumFilterSize,
                                funnyYCode, c->srcFormat, formatConvBuffer,
                                c->lumMmx2Filter, c->lumMmx2FilterPos, pal);
                lastInLumBuf++;
            }
            while(lastInChrBuf < lastChrSrcY)
            {
                uint8_t *src1= src[1]+(lastInChrBuf + 1 - chrSrcSliceY)*srcStride[1];
                uint8_t *src2= src[2]+(lastInChrBuf + 1 - chrSrcSliceY)*srcStride[2];
                chrBufIndex++;
                assert(chrBufIndex < 2*vChrBufSize);
                assert(lastInChrBuf + 1 - chrSrcSliceY < (chrSrcSliceH));
                assert(lastInChrBuf + 1 - chrSrcSliceY >= 0);
                //FIXME replace parameters through context struct (some at least)

                if (!(isGray(srcFormat) || isGray(dstFormat)))
                    RENAME(hcscale)(c, chrPixBuf[ chrBufIndex ], chrDstW, src1, src2, chrSrcW, chrXInc,
                                    flags, canMMX2BeUsed, hChrFilter, hChrFilterPos, hChrFilterSize,
                                    funnyUVCode, c->srcFormat, formatConvBuffer,
                                    c->chrMmx2Filter, c->chrMmx2FilterPos, pal);
                lastInChrBuf++;
            }
            //wrap buf index around to stay inside the ring buffer
            if (lumBufIndex >= vLumBufSize) lumBufIndex-= vLumBufSize;
            if (chrBufIndex >= vChrBufSize) chrBufIndex-= vChrBufSize;
        }
        else // not enough lines left in this slice -> load the rest in the buffer
        {
            /* printf("%d %d Last:%d %d LastInBuf:%d %d Index:%d %d Y:%d FSize: %d %d BSize: %d %d\n",
            firstChrSrcY,firstLumSrcY,lastChrSrcY,lastLumSrcY,
            lastInChrBuf,lastInLumBuf,chrBufIndex,lumBufIndex,dstY,vChrFilterSize,vLumFilterSize,
            vChrBufSize, vLumBufSize);*/

            //Do horizontal scaling
            while(lastInLumBuf+1 < srcSliceY + srcSliceH)
            {
                uint8_t *s= src[0]+(lastInLumBuf + 1 - srcSliceY)*srcStride[0];
                lumBufIndex++;
                assert(lumBufIndex < 2*vLumBufSize);
                assert(lastInLumBuf + 1 - srcSliceY < srcSliceH);
                assert(lastInLumBuf + 1 - srcSliceY >= 0);
                RENAME(hyscale)(c, lumPixBuf[ lumBufIndex ], dstW, s, srcW, lumXInc,
                                flags, canMMX2BeUsed, hLumFilter, hLumFilterPos, hLumFilterSize,
                                funnyYCode, c->srcFormat, formatConvBuffer,
                                c->lumMmx2Filter, c->lumMmx2FilterPos, pal);
                lastInLumBuf++;
            }
            while(lastInChrBuf+1 < (chrSrcSliceY + chrSrcSliceH))
            {
                uint8_t *src1= src[1]+(lastInChrBuf + 1 - chrSrcSliceY)*srcStride[1];
                uint8_t *src2= src[2]+(lastInChrBuf + 1 - chrSrcSliceY)*srcStride[2];
                chrBufIndex++;
                assert(chrBufIndex < 2*vChrBufSize);
                assert(lastInChrBuf + 1 - chrSrcSliceY < chrSrcSliceH);
                assert(lastInChrBuf + 1 - chrSrcSliceY >= 0);

                if (!(isGray(srcFormat) || isGray(dstFormat)))
                    RENAME(hcscale)(c, chrPixBuf[ chrBufIndex ], chrDstW, src1, src2, chrSrcW, chrXInc,
                            flags, canMMX2BeUsed, hChrFilter, hChrFilterPos, hChrFilterSize,
                            funnyUVCode, c->srcFormat, formatConvBuffer,
                            c->chrMmx2Filter, c->chrMmx2FilterPos, pal);
                lastInChrBuf++;
            }
            //wrap buf index around to stay inside the ring buffer
            if (lumBufIndex >= vLumBufSize) lumBufIndex-= vLumBufSize;
            if (chrBufIndex >= vChrBufSize) chrBufIndex-= vChrBufSize;
            break; //we can't output a dstY line so let's try with the next slice
        }

#if HAVE_MMX
        c->blueDither= ff_dither8[dstY&1];
        if (c->dstFormat == PIX_FMT_RGB555 || c->dstFormat == PIX_FMT_BGR555)
            c->greenDither= ff_dither8[dstY&1];
        else
            c->greenDither= ff_dither4[dstY&1];
        c->redDither= ff_dither8[(dstY+1)&1];
#endif
        if (dstY < dstH-2)
        {
            int16_t **lumSrcPtr= lumPixBuf + lumBufIndex + firstLumSrcY - lastInLumBuf + vLumBufSize;
            int16_t **chrSrcPtr= chrPixBuf + chrBufIndex + firstChrSrcY - lastInChrBuf + vChrBufSize;
#if HAVE_MMX
            int i;
        if (flags & SWS_ACCURATE_RND){
            int s= APCK_SIZE / 8;
            for (i=0; i<vLumFilterSize; i+=2){
                *(void**)&lumMmxFilter[s*i              ]= lumSrcPtr[i  ];
                *(void**)&lumMmxFilter[s*i+APCK_PTR2/4  ]= lumSrcPtr[i+(vLumFilterSize>1)];
                          lumMmxFilter[s*i+APCK_COEF/4  ]=
                          lumMmxFilter[s*i+APCK_COEF/4+1]= vLumFilter[dstY*vLumFilterSize + i    ]
                    + (vLumFilterSize>1 ? vLumFilter[dstY*vLumFilterSize + i + 1]<<16 : 0);
            }
            for (i=0; i<vChrFilterSize; i+=2){
                *(void**)&chrMmxFilter[s*i              ]= chrSrcPtr[i  ];
                *(void**)&chrMmxFilter[s*i+APCK_PTR2/4  ]= chrSrcPtr[i+(vChrFilterSize>1)];
                          chrMmxFilter[s*i+APCK_COEF/4  ]=
                          chrMmxFilter[s*i+APCK_COEF/4+1]= vChrFilter[chrDstY*vChrFilterSize + i    ]
                    + (vChrFilterSize>1 ? vChrFilter[chrDstY*vChrFilterSize + i + 1]<<16 : 0);
            }
        }else{
            for (i=0; i<vLumFilterSize; i++)
            {
                lumMmxFilter[4*i+0]= (int32_t)lumSrcPtr[i];
                lumMmxFilter[4*i+1]= (uint64_t)lumSrcPtr[i] >> 32;
                lumMmxFilter[4*i+2]=
                lumMmxFilter[4*i+3]=
                    ((uint16_t)vLumFilter[dstY*vLumFilterSize + i])*0x10001;
            }
            for (i=0; i<vChrFilterSize; i++)
            {
                chrMmxFilter[4*i+0]= (int32_t)chrSrcPtr[i];
                chrMmxFilter[4*i+1]= (uint64_t)chrSrcPtr[i] >> 32;
                chrMmxFilter[4*i+2]=
                chrMmxFilter[4*i+3]=
                    ((uint16_t)vChrFilter[chrDstY*vChrFilterSize + i])*0x10001;
            }
        }
#endif
            if (dstFormat == PIX_FMT_NV12 || dstFormat == PIX_FMT_NV21){
                const int chrSkipMask= (1<<c->chrDstVSubSample)-1;
                if (dstY&chrSkipMask) uDest= NULL; //FIXME split functions in lumi / chromi
                RENAME(yuv2nv12X)(c,
                    vLumFilter+dstY*vLumFilterSize   , lumSrcPtr, vLumFilterSize,
                    vChrFilter+chrDstY*vChrFilterSize, chrSrcPtr, vChrFilterSize,
                    dest, uDest, dstW, chrDstW, dstFormat);
            }
            else if (isPlanarYUV(dstFormat) || dstFormat==PIX_FMT_GRAY8) //YV12 like
            {
                const int chrSkipMask= (1<<c->chrDstVSubSample)-1;
                if ((dstY&chrSkipMask) || isGray(dstFormat)) uDest=vDest= NULL; //FIXME split functions in lumi / chromi
                if (vLumFilterSize == 1 && vChrFilterSize == 1) // unscaled YV12
                {
                    int16_t *lumBuf = lumPixBuf[0];
                    int16_t *chrBuf= chrPixBuf[0];
                    RENAME(yuv2yuv1)(c, lumBuf, chrBuf, dest, uDest, vDest, dstW, chrDstW);
                }
                else //General YV12
                {
                    RENAME(yuv2yuvX)(c,
                        vLumFilter+dstY*vLumFilterSize   , lumSrcPtr, vLumFilterSize,
                        vChrFilter+chrDstY*vChrFilterSize, chrSrcPtr, vChrFilterSize,
                        dest, uDest, vDest, dstW, chrDstW);
                }
            }
            else
            {
                assert(lumSrcPtr + vLumFilterSize - 1 < lumPixBuf + vLumBufSize*2);
                assert(chrSrcPtr + vChrFilterSize - 1 < chrPixBuf + vChrBufSize*2);
                if (vLumFilterSize == 1 && vChrFilterSize == 2) //unscaled RGB
                {
                    int chrAlpha= vChrFilter[2*dstY+1];
                    if(flags & SWS_FULL_CHR_H_INT){
                        yuv2rgbXinC_full(c, //FIXME write a packed1_full function
                            vLumFilter+dstY*vLumFilterSize, lumSrcPtr, vLumFilterSize,
                            vChrFilter+dstY*vChrFilterSize, chrSrcPtr, vChrFilterSize,
                            dest, dstW, dstY);
                    }else{
                        RENAME(yuv2packed1)(c, *lumSrcPtr, *chrSrcPtr, *(chrSrcPtr+1),
                            dest, dstW, chrAlpha, dstFormat, flags, dstY);
                    }
                }
                else if (vLumFilterSize == 2 && vChrFilterSize == 2) //bilinear upscale RGB
                {
                    int lumAlpha= vLumFilter[2*dstY+1];
                    int chrAlpha= vChrFilter[2*dstY+1];
                    lumMmxFilter[2]=
                    lumMmxFilter[3]= vLumFilter[2*dstY   ]*0x10001;
                    chrMmxFilter[2]=
                    chrMmxFilter[3]= vChrFilter[2*chrDstY]*0x10001;
                    if(flags & SWS_FULL_CHR_H_INT){
                        yuv2rgbXinC_full(c, //FIXME write a packed2_full function
                            vLumFilter+dstY*vLumFilterSize, lumSrcPtr, vLumFilterSize,
                            vChrFilter+dstY*vChrFilterSize, chrSrcPtr, vChrFilterSize,
                            dest, dstW, dstY);
                    }else{
                        RENAME(yuv2packed2)(c, *lumSrcPtr, *(lumSrcPtr+1), *chrSrcPtr, *(chrSrcPtr+1),
                            dest, dstW, lumAlpha, chrAlpha, dstY);
                    }
                }
                else //general RGB
                {
                    if(flags & SWS_FULL_CHR_H_INT){
                        yuv2rgbXinC_full(c,
                            vLumFilter+dstY*vLumFilterSize, lumSrcPtr, vLumFilterSize,
                            vChrFilter+dstY*vChrFilterSize, chrSrcPtr, vChrFilterSize,
                            dest, dstW, dstY);
                    }else{
                        RENAME(yuv2packedX)(c,
                            vLumFilter+dstY*vLumFilterSize, lumSrcPtr, vLumFilterSize,
                            vChrFilter+dstY*vChrFilterSize, chrSrcPtr, vChrFilterSize,
                            dest, dstW, dstY);
                    }
                }
            }
        }
        else // hmm looks like we can't use MMX here without overwriting this array's tail
        {
            int16_t **lumSrcPtr= lumPixBuf + lumBufIndex + firstLumSrcY - lastInLumBuf + vLumBufSize;
            int16_t **chrSrcPtr= chrPixBuf + chrBufIndex + firstChrSrcY - lastInChrBuf + vChrBufSize;
            if (dstFormat == PIX_FMT_NV12 || dstFormat == PIX_FMT_NV21){
                const int chrSkipMask= (1<<c->chrDstVSubSample)-1;
                if (dstY&chrSkipMask) uDest= NULL; //FIXME split functions in lumi / chromi
                yuv2nv12XinC(
                    vLumFilter+dstY*vLumFilterSize   , lumSrcPtr, vLumFilterSize,
                    vChrFilter+chrDstY*vChrFilterSize, chrSrcPtr, vChrFilterSize,
                    dest, uDest, dstW, chrDstW, dstFormat);
            }
            else if (isPlanarYUV(dstFormat) || dstFormat==PIX_FMT_GRAY8) //YV12
            {
                const int chrSkipMask= (1<<c->chrDstVSubSample)-1;
                if ((dstY&chrSkipMask) || isGray(dstFormat)) uDest=vDest= NULL; //FIXME split functions in lumi / chromi
                yuv2yuvXinC(
                    vLumFilter+dstY*vLumFilterSize   , lumSrcPtr, vLumFilterSize,
                    vChrFilter+chrDstY*vChrFilterSize, chrSrcPtr, vChrFilterSize,
                    dest, uDest, vDest, dstW, chrDstW);
            }
            else
            {
                assert(lumSrcPtr + vLumFilterSize - 1 < lumPixBuf + vLumBufSize*2);
                assert(chrSrcPtr + vChrFilterSize - 1 < chrPixBuf + vChrBufSize*2);
                if(flags & SWS_FULL_CHR_H_INT){
                    yuv2rgbXinC_full(c,
                        vLumFilter+dstY*vLumFilterSize, lumSrcPtr, vLumFilterSize,
                        vChrFilter+dstY*vChrFilterSize, chrSrcPtr, vChrFilterSize,
                        dest, dstW, dstY);
                }else{
                    yuv2packedXinC(c,
                        vLumFilter+dstY*vLumFilterSize, lumSrcPtr, vLumFilterSize,
                        vChrFilter+dstY*vChrFilterSize, chrSrcPtr, vChrFilterSize,
                        dest, dstW, dstY);
                }
            }
        }
    }

#if HAVE_MMX
    __asm__ volatile(SFENCE:::"memory");
    __asm__ volatile(EMMS:::"memory");
#endif
    /* store changed local vars back in the context */
    c->dstY= dstY;
    c->lumBufIndex= lumBufIndex;
    c->chrBufIndex= chrBufIndex;
    c->lastInLumBuf= lastInLumBuf;
    c->lastInChrBuf= lastInChrBuf;

    return dstY - lastDstY;
}
