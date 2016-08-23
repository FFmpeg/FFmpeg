/*
 * Chinese AVS video (AVS1-P2, JiZhun profile) decoder.
 * Copyright (c) 2006  Stefan Gehrer <stefan.gehrer@gmx.de>
 *
 * MMX-optimized DSP functions, based on H.264 optimizations by
 * Michael Niedermayer and Loren Merritt
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

#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/cavsdsp.h"
#include "libavcodec/idctdsp.h"
#include "constants.h"
#include "fpel.h"
#include "idctdsp.h"
#include "config.h"

#if HAVE_MMX_INLINE

/* in/out: mma=mma+mmb, mmb=mmb-mma */
#define SUMSUB_BA( a, b ) \
    "paddw "#b", "#a" \n\t"\
    "paddw "#b", "#b" \n\t"\
    "psubw "#a", "#b" \n\t"

/*****************************************************************************
 *
 * inverse transform
 *
 ****************************************************************************/

static inline void cavs_idct8_1d(int16_t *block, uint64_t bias)
{
    __asm__ volatile(
        "movq 112(%0), %%mm4  \n\t" /* mm4 = src7 */
        "movq  16(%0), %%mm5  \n\t" /* mm5 = src1 */
        "movq  80(%0), %%mm2  \n\t" /* mm2 = src5 */
        "movq  48(%0), %%mm7  \n\t" /* mm7 = src3 */
        "movq   %%mm4, %%mm0  \n\t"
        "movq   %%mm5, %%mm3  \n\t"
        "movq   %%mm2, %%mm6  \n\t"
        "movq   %%mm7, %%mm1  \n\t"

        "paddw  %%mm4, %%mm4  \n\t" /* mm4 = 2*src7 */
        "paddw  %%mm3, %%mm3  \n\t" /* mm3 = 2*src1 */
        "paddw  %%mm6, %%mm6  \n\t" /* mm6 = 2*src5 */
        "paddw  %%mm1, %%mm1  \n\t" /* mm1 = 2*src3 */
        "paddw  %%mm4, %%mm0  \n\t" /* mm0 = 3*src7 */
        "paddw  %%mm3, %%mm5  \n\t" /* mm5 = 3*src1 */
        "paddw  %%mm6, %%mm2  \n\t" /* mm2 = 3*src5 */
        "paddw  %%mm1, %%mm7  \n\t" /* mm7 = 3*src3 */
        "psubw  %%mm4, %%mm5  \n\t" /* mm5 = 3*src1 - 2*src7 = a0 */
        "paddw  %%mm6, %%mm7  \n\t" /* mm7 = 3*src3 + 2*src5 = a1 */
        "psubw  %%mm2, %%mm1  \n\t" /* mm1 = 2*src3 - 3*src5 = a2 */
        "paddw  %%mm0, %%mm3  \n\t" /* mm3 = 2*src1 + 3*src7 = a3 */

        "movq   %%mm5, %%mm4  \n\t"
        "movq   %%mm7, %%mm6  \n\t"
        "movq   %%mm3, %%mm0  \n\t"
        "movq   %%mm1, %%mm2  \n\t"
        SUMSUB_BA( %%mm7, %%mm5 )   /* mm7 = a0 + a1  mm5 = a0 - a1 */
        "paddw  %%mm3, %%mm7  \n\t" /* mm7 = a0 + a1 + a3 */
        "paddw  %%mm1, %%mm5  \n\t" /* mm5 = a0 - a1 + a2 */
        "paddw  %%mm7, %%mm7  \n\t"
        "paddw  %%mm5, %%mm5  \n\t"
        "paddw  %%mm6, %%mm7  \n\t" /* mm7 = b4 */
        "paddw  %%mm4, %%mm5  \n\t" /* mm5 = b5 */

        SUMSUB_BA( %%mm1, %%mm3 )   /* mm1 = a3 + a2  mm3 = a3 - a2 */
        "psubw  %%mm1, %%mm4  \n\t" /* mm4 = a0 - a2 - a3 */
        "movq   %%mm4, %%mm1  \n\t" /* mm1 = a0 - a2 - a3 */
        "psubw  %%mm6, %%mm3  \n\t" /* mm3 = a3 - a2 - a1 */
        "paddw  %%mm1, %%mm1  \n\t"
        "paddw  %%mm3, %%mm3  \n\t"
        "psubw  %%mm2, %%mm1  \n\t" /* mm1 = b7 */
        "paddw  %%mm0, %%mm3  \n\t" /* mm3 = b6 */

        "movq  32(%0), %%mm2  \n\t" /* mm2 = src2 */
        "movq  96(%0), %%mm6  \n\t" /* mm6 = src6 */
        "movq   %%mm2, %%mm4  \n\t"
        "movq   %%mm6, %%mm0  \n\t"
        "psllw  $2,    %%mm4  \n\t" /* mm4 = 4*src2 */
        "psllw  $2,    %%mm6  \n\t" /* mm6 = 4*src6 */
        "paddw  %%mm4, %%mm2  \n\t" /* mm2 = 5*src2 */
        "paddw  %%mm6, %%mm0  \n\t" /* mm0 = 5*src6 */
        "paddw  %%mm2, %%mm2  \n\t"
        "paddw  %%mm0, %%mm0  \n\t"
        "psubw  %%mm0, %%mm4  \n\t" /* mm4 = 4*src2 - 10*src6 = a7 */
        "paddw  %%mm2, %%mm6  \n\t" /* mm6 = 4*src6 + 10*src2 = a6 */

        "movq    (%0), %%mm2  \n\t" /* mm2 = src0 */
        "movq  64(%0), %%mm0  \n\t" /* mm0 = src4 */
        SUMSUB_BA( %%mm0, %%mm2 )   /* mm0 = src0+src4  mm2 = src0-src4 */
        "psllw  $3,    %%mm0  \n\t"
        "psllw  $3,    %%mm2  \n\t"
        "paddw  %1,    %%mm0  \n\t" /* add rounding bias */
        "paddw  %1,    %%mm2  \n\t" /* add rounding bias */

        SUMSUB_BA( %%mm6, %%mm0 )   /* mm6 = a4 + a6  mm0 = a4 - a6 */
        SUMSUB_BA( %%mm4, %%mm2 )   /* mm4 = a5 + a7  mm2 = a5 - a7 */
        SUMSUB_BA( %%mm7, %%mm6 )   /* mm7 = dst0  mm6 = dst7 */
        SUMSUB_BA( %%mm5, %%mm4 )   /* mm5 = dst1  mm4 = dst6 */
        SUMSUB_BA( %%mm3, %%mm2 )   /* mm3 = dst2  mm2 = dst5 */
        SUMSUB_BA( %%mm1, %%mm0 )   /* mm1 = dst3  mm0 = dst4 */
        :: "r"(block), "m"(bias)
    );
}

#define SBUTTERFLY(a,b,t,n,m)\
    "mov" #m " " #a ", " #t "         \n\t" /* abcd */\
    "punpckl" #n " " #b ", " #a "     \n\t" /* aebf */\
    "punpckh" #n " " #b ", " #t "     \n\t" /* cgdh */\

#define TRANSPOSE4(a,b,c,d,t)\
    SBUTTERFLY(a,b,t,wd,q) /* a=aebf t=cgdh */\
    SBUTTERFLY(c,d,b,wd,q) /* c=imjn b=kolp */\
    SBUTTERFLY(a,c,d,dq,q) /* a=aeim d=bfjn */\
    SBUTTERFLY(t,b,c,dq,q) /* t=cgko c=dhlp */

static void cavs_idct8_add_mmx(uint8_t *dst, int16_t *block, ptrdiff_t stride)
{
    int i;
    DECLARE_ALIGNED(8, int16_t, b2)[64];

    for(i=0; i<2; i++){
        cavs_idct8_1d(block + 4 * i, ff_pw_4.a);

        __asm__ volatile(
            "psraw     $3, %%mm7  \n\t"
            "psraw     $3, %%mm6  \n\t"
            "psraw     $3, %%mm5  \n\t"
            "psraw     $3, %%mm4  \n\t"
            "psraw     $3, %%mm3  \n\t"
            "psraw     $3, %%mm2  \n\t"
            "psraw     $3, %%mm1  \n\t"
            "psraw     $3, %%mm0  \n\t"
            "movq   %%mm7,  (%0)  \n\t"
            TRANSPOSE4( %%mm0, %%mm2, %%mm4, %%mm6, %%mm7 )
            "movq   %%mm0,  8(%0)  \n\t"
            "movq   %%mm6, 24(%0)  \n\t"
            "movq   %%mm7, 40(%0)  \n\t"
            "movq   %%mm4, 56(%0)  \n\t"
            "movq    (%0),  %%mm7  \n\t"
            TRANSPOSE4( %%mm7, %%mm5, %%mm3, %%mm1, %%mm0 )
            "movq   %%mm7,   (%0)  \n\t"
            "movq   %%mm1, 16(%0)  \n\t"
            "movq   %%mm0, 32(%0)  \n\t"
            "movq   %%mm3, 48(%0)  \n\t"
            :
            : "r"(b2 + 32 * i)
            : "memory"
        );
    }

    for(i=0; i<2; i++){
        cavs_idct8_1d(b2+4*i, ff_pw_64.a);

        __asm__ volatile(
            "psraw     $7, %%mm7  \n\t"
            "psraw     $7, %%mm6  \n\t"
            "psraw     $7, %%mm5  \n\t"
            "psraw     $7, %%mm4  \n\t"
            "psraw     $7, %%mm3  \n\t"
            "psraw     $7, %%mm2  \n\t"
            "psraw     $7, %%mm1  \n\t"
            "psraw     $7, %%mm0  \n\t"
            "movq   %%mm7,    (%0)  \n\t"
            "movq   %%mm5,  16(%0)  \n\t"
            "movq   %%mm3,  32(%0)  \n\t"
            "movq   %%mm1,  48(%0)  \n\t"
            "movq   %%mm0,  64(%0)  \n\t"
            "movq   %%mm2,  80(%0)  \n\t"
            "movq   %%mm4,  96(%0)  \n\t"
            "movq   %%mm6, 112(%0)  \n\t"
            :: "r"(b2+4*i)
            : "memory"
        );
    }

    ff_add_pixels_clamped_mmx(b2, dst, stride);
}

#endif /* HAVE_MMX_INLINE */

#if (HAVE_MMXEXT_INLINE || HAVE_AMD3DNOW_INLINE)

/*****************************************************************************
 *
 * motion compensation
 *
 ****************************************************************************/

/* vertical filter [-1 -2 96 42 -7  0]  */
#define QPEL_CAVSV1(A,B,C,D,E,F,OP,MUL2) \
        "movd (%0), "#F"            \n\t"\
        "movq "#C", %%mm6           \n\t"\
        "pmullw %5, %%mm6           \n\t"\
        "movq "#D", %%mm7           \n\t"\
        "pmullw "MANGLE(MUL2)", %%mm7\n\t"\
        "psllw $3, "#E"             \n\t"\
        "psubw "#E", %%mm6          \n\t"\
        "psraw $3, "#E"             \n\t"\
        "paddw %%mm7, %%mm6         \n\t"\
        "paddw "#E", %%mm6          \n\t"\
        "paddw "#B", "#B"           \n\t"\
        "pxor %%mm7, %%mm7          \n\t"\
        "add %2, %0                 \n\t"\
        "punpcklbw %%mm7, "#F"      \n\t"\
        "psubw "#B", %%mm6          \n\t"\
        "psraw $1, "#B"             \n\t"\
        "psubw "#A", %%mm6          \n\t"\
        "paddw %4, %%mm6            \n\t"\
        "psraw $7, %%mm6            \n\t"\
        "packuswb %%mm6, %%mm6      \n\t"\
        OP(%%mm6, (%1), A, d)            \
        "add %3, %1                 \n\t"

/* vertical filter [ 0 -1  5  5 -1  0]  */
#define QPEL_CAVSV2(A,B,C,D,E,F,OP,MUL2) \
        "movd (%0), "#F"            \n\t"\
        "movq "#C", %%mm6           \n\t"\
        "paddw "#D", %%mm6          \n\t"\
        "pmullw %5, %%mm6           \n\t"\
        "add %2, %0                 \n\t"\
        "punpcklbw %%mm7, "#F"      \n\t"\
        "psubw "#B", %%mm6          \n\t"\
        "psubw "#E", %%mm6          \n\t"\
        "paddw %4, %%mm6            \n\t"\
        "psraw $3, %%mm6            \n\t"\
        "packuswb %%mm6, %%mm6      \n\t"\
        OP(%%mm6, (%1), A, d)            \
        "add %3, %1                 \n\t"

/* vertical filter [ 0 -7 42 96 -2 -1]  */
#define QPEL_CAVSV3(A,B,C,D,E,F,OP,MUL2) \
        "movd (%0), "#F"            \n\t"\
        "movq "#C", %%mm6           \n\t"\
        "pmullw "MANGLE(MUL2)", %%mm6\n\t"\
        "movq "#D", %%mm7           \n\t"\
        "pmullw %5, %%mm7           \n\t"\
        "psllw $3, "#B"             \n\t"\
        "psubw "#B", %%mm6          \n\t"\
        "psraw $3, "#B"             \n\t"\
        "paddw %%mm7, %%mm6         \n\t"\
        "paddw "#B", %%mm6          \n\t"\
        "paddw "#E", "#E"           \n\t"\
        "pxor %%mm7, %%mm7          \n\t"\
        "add %2, %0                 \n\t"\
        "punpcklbw %%mm7, "#F"      \n\t"\
        "psubw "#E", %%mm6          \n\t"\
        "psraw $1, "#E"             \n\t"\
        "psubw "#F", %%mm6          \n\t"\
        "paddw %4, %%mm6            \n\t"\
        "psraw $7, %%mm6            \n\t"\
        "packuswb %%mm6, %%mm6      \n\t"\
        OP(%%mm6, (%1), A, d)            \
        "add %3, %1                 \n\t"


#define QPEL_CAVSVNUM(VOP,OP,ADD,MUL1,MUL2)\
    int w= 2;\
    src -= 2*srcStride;\
    \
    while(w--){\
      __asm__ volatile(\
        "pxor %%mm7, %%mm7          \n\t"\
        "movd (%0), %%mm0           \n\t"\
        "add %2, %0                 \n\t"\
        "movd (%0), %%mm1           \n\t"\
        "add %2, %0                 \n\t"\
        "movd (%0), %%mm2           \n\t"\
        "add %2, %0                 \n\t"\
        "movd (%0), %%mm3           \n\t"\
        "add %2, %0                 \n\t"\
        "movd (%0), %%mm4           \n\t"\
        "add %2, %0                 \n\t"\
        "punpcklbw %%mm7, %%mm0     \n\t"\
        "punpcklbw %%mm7, %%mm1     \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpcklbw %%mm7, %%mm3     \n\t"\
        "punpcklbw %%mm7, %%mm4     \n\t"\
        VOP(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, OP, MUL2)\
        VOP(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, OP, MUL2)\
        VOP(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, OP, MUL2)\
        VOP(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, OP, MUL2)\
        VOP(%%mm4, %%mm5, %%mm0, %%mm1, %%mm2, %%mm3, OP, MUL2)\
        VOP(%%mm5, %%mm0, %%mm1, %%mm2, %%mm3, %%mm4, OP, MUL2)\
        VOP(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, OP, MUL2)\
        VOP(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, OP, MUL2)\
        \
        : "+a"(src), "+c"(dst)\
        : "S"((x86_reg)srcStride), "r"((x86_reg)dstStride), "m"(ADD), "m"(MUL1)\
        : "memory"\
     );\
     if(h==16){\
        __asm__ volatile(\
            VOP(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, OP, MUL2)\
            VOP(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, OP, MUL2)\
            VOP(%%mm4, %%mm5, %%mm0, %%mm1, %%mm2, %%mm3, OP, MUL2)\
            VOP(%%mm5, %%mm0, %%mm1, %%mm2, %%mm3, %%mm4, OP, MUL2)\
            VOP(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, OP, MUL2)\
            VOP(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, OP, MUL2)\
            VOP(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, OP, MUL2)\
            VOP(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, OP, MUL2)\
            \
           : "+a"(src), "+c"(dst)\
           : "S"((x86_reg)srcStride), "r"((x86_reg)dstStride), "m"(ADD),  "m"(MUL1)\
           : "memory"\
        );\
     }\
     src += 4-(h+5)*srcStride;\
     dst += 4-h*dstStride;\
   }

#define QPEL_CAVS(OPNAME, OP, MMX)\
static void OPNAME ## cavs_qpel8_h_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{\
    int h=8;\
    __asm__ volatile(\
        "pxor %%mm7, %%mm7          \n\t"\
        "movq %5, %%mm6             \n\t"\
        "1:                         \n\t"\
        "movq    (%0), %%mm0        \n\t"\
        "movq   1(%0), %%mm2        \n\t"\
        "movq %%mm0, %%mm1          \n\t"\
        "movq %%mm2, %%mm3          \n\t"\
        "punpcklbw %%mm7, %%mm0     \n\t"\
        "punpckhbw %%mm7, %%mm1     \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpckhbw %%mm7, %%mm3     \n\t"\
        "paddw %%mm2, %%mm0         \n\t"\
        "paddw %%mm3, %%mm1         \n\t"\
        "pmullw %%mm6, %%mm0        \n\t"\
        "pmullw %%mm6, %%mm1        \n\t"\
        "movq   -1(%0), %%mm2       \n\t"\
        "movq    2(%0), %%mm4       \n\t"\
        "movq %%mm2, %%mm3          \n\t"\
        "movq %%mm4, %%mm5          \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpckhbw %%mm7, %%mm3     \n\t"\
        "punpcklbw %%mm7, %%mm4     \n\t"\
        "punpckhbw %%mm7, %%mm5     \n\t"\
        "paddw %%mm4, %%mm2         \n\t"\
        "paddw %%mm3, %%mm5         \n\t"\
        "psubw %%mm2, %%mm0         \n\t"\
        "psubw %%mm5, %%mm1         \n\t"\
        "movq %6, %%mm5             \n\t"\
        "paddw %%mm5, %%mm0         \n\t"\
        "paddw %%mm5, %%mm1         \n\t"\
        "psraw $3, %%mm0            \n\t"\
        "psraw $3, %%mm1            \n\t"\
        "packuswb %%mm1, %%mm0      \n\t"\
        OP(%%mm0, (%1),%%mm5, q)         \
        "add %3, %0                 \n\t"\
        "add %4, %1                 \n\t"\
        "decl %2                    \n\t"\
        " jnz 1b                    \n\t"\
        : "+a"(src), "+c"(dst), "+m"(h)\
        : "d"((x86_reg)srcStride), "S"((x86_reg)dstStride), "m"(ff_pw_5), "m"(ff_pw_4)\
        : "memory"\
    );\
}\
\
static inline void OPNAME ## cavs_qpel8or16_v1_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride, int h)\
{                                                                       \
  QPEL_CAVSVNUM(QPEL_CAVSV1,OP,ff_pw_64,ff_pw_96,ff_pw_42)      \
}\
\
static inline void OPNAME ## cavs_qpel8or16_v2_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride, int h)\
{                                                                       \
  QPEL_CAVSVNUM(QPEL_CAVSV2,OP,ff_pw_4,ff_pw_5,ff_pw_5)         \
}\
\
static inline void OPNAME ## cavs_qpel8or16_v3_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride, int h)\
{                                                                       \
  QPEL_CAVSVNUM(QPEL_CAVSV3,OP,ff_pw_64,ff_pw_96,ff_pw_42)      \
}\
\
static void OPNAME ## cavs_qpel8_v1_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{                                                                       \
    OPNAME ## cavs_qpel8or16_v1_ ## MMX(dst  , src  , dstStride, srcStride, 8);\
}\
static void OPNAME ## cavs_qpel16_v1_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{                                                                       \
    OPNAME ## cavs_qpel8or16_v1_ ## MMX(dst  , src  , dstStride, srcStride, 16);\
    OPNAME ## cavs_qpel8or16_v1_ ## MMX(dst+8, src+8, dstStride, srcStride, 16);\
}\
\
static void OPNAME ## cavs_qpel8_v2_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{                                                                       \
    OPNAME ## cavs_qpel8or16_v2_ ## MMX(dst  , src  , dstStride, srcStride, 8);\
}\
static void OPNAME ## cavs_qpel16_v2_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{                                                                       \
    OPNAME ## cavs_qpel8or16_v2_ ## MMX(dst  , src  , dstStride, srcStride, 16);\
    OPNAME ## cavs_qpel8or16_v2_ ## MMX(dst+8, src+8, dstStride, srcStride, 16);\
}\
\
static void OPNAME ## cavs_qpel8_v3_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{                                                                       \
    OPNAME ## cavs_qpel8or16_v3_ ## MMX(dst  , src  , dstStride, srcStride, 8);\
}\
static void OPNAME ## cavs_qpel16_v3_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{                                                                       \
    OPNAME ## cavs_qpel8or16_v3_ ## MMX(dst  , src  , dstStride, srcStride, 16);\
    OPNAME ## cavs_qpel8or16_v3_ ## MMX(dst+8, src+8, dstStride, srcStride, 16);\
}\
\
static void OPNAME ## cavs_qpel16_h_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t dstStride, ptrdiff_t srcStride)\
{                                                                       \
    OPNAME ## cavs_qpel8_h_ ## MMX(dst  , src  , dstStride, srcStride);\
    OPNAME ## cavs_qpel8_h_ ## MMX(dst+8, src+8, dstStride, srcStride);\
    src += 8*srcStride;\
    dst += 8*dstStride;\
    OPNAME ## cavs_qpel8_h_ ## MMX(dst  , src  , dstStride, srcStride);\
    OPNAME ## cavs_qpel8_h_ ## MMX(dst+8, src+8, dstStride, srcStride);\
}\

#define CAVS_MC(OPNAME, SIZE, MMX) \
static void OPNAME ## cavs_qpel ## SIZE ## _mc20_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    OPNAME ## cavs_qpel ## SIZE ## _h_ ## MMX(dst, src, stride, stride);\
}\
\
static void OPNAME ## cavs_qpel ## SIZE ## _mc01_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    OPNAME ## cavs_qpel ## SIZE ## _v1_ ## MMX(dst, src, stride, stride);\
}\
\
static void OPNAME ## cavs_qpel ## SIZE ## _mc02_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    OPNAME ## cavs_qpel ## SIZE ## _v2_ ## MMX(dst, src, stride, stride);\
}\
\
static void OPNAME ## cavs_qpel ## SIZE ## _mc03_ ## MMX(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)\
{\
    OPNAME ## cavs_qpel ## SIZE ## _v3_ ## MMX(dst, src, stride, stride);\
}\

#define PUT_OP(a,b,temp, size) "mov" #size " " #a ", " #b "    \n\t"
#define AVG_3DNOW_OP(a,b,temp, size) \
"mov" #size " " #b ", " #temp "   \n\t"\
"pavgusb " #temp ", " #a "        \n\t"\
"mov" #size " " #a ", " #b "      \n\t"
#define AVG_MMXEXT_OP(a, b, temp, size) \
"mov" #size " " #b ", " #temp "   \n\t"\
"pavgb " #temp ", " #a "          \n\t"\
"mov" #size " " #a ", " #b "      \n\t"

#endif /* (HAVE_MMXEXT_INLINE || HAVE_AMD3DNOW_INLINE) */

#if HAVE_MMX_INLINE
static void put_cavs_qpel8_mc00_mmx(uint8_t *dst, const uint8_t *src,
                                    ptrdiff_t stride)
{
    ff_put_pixels8_mmx(dst, src, stride, 8);
}

static void avg_cavs_qpel8_mc00_mmx(uint8_t *dst, const uint8_t *src,
                                    ptrdiff_t stride)
{
    ff_avg_pixels8_mmx(dst, src, stride, 8);
}

static void put_cavs_qpel16_mc00_mmx(uint8_t *dst, const uint8_t *src,
                                     ptrdiff_t stride)
{
    ff_put_pixels16_mmx(dst, src, stride, 16);
}

static void avg_cavs_qpel16_mc00_mmx(uint8_t *dst, const uint8_t *src,
                                     ptrdiff_t stride)
{
    ff_avg_pixels16_mmx(dst, src, stride, 16);
}

static av_cold void cavsdsp_init_mmx(CAVSDSPContext *c,
                                     AVCodecContext *avctx)
{
    c->put_cavs_qpel_pixels_tab[0][0] = put_cavs_qpel16_mc00_mmx;
    c->put_cavs_qpel_pixels_tab[1][0] = put_cavs_qpel8_mc00_mmx;
    c->avg_cavs_qpel_pixels_tab[0][0] = avg_cavs_qpel16_mc00_mmx;
    c->avg_cavs_qpel_pixels_tab[1][0] = avg_cavs_qpel8_mc00_mmx;

    c->cavs_idct8_add = cavs_idct8_add_mmx;
    c->idct_perm      = FF_IDCT_PERM_TRANSPOSE;
}
#endif /* HAVE_MMX_INLINE */

#define DSPFUNC(PFX, IDX, NUM, EXT)                                                       \
    c->PFX ## _cavs_qpel_pixels_tab[IDX][ 2] = PFX ## _cavs_qpel ## NUM ## _mc20_ ## EXT; \
    c->PFX ## _cavs_qpel_pixels_tab[IDX][ 4] = PFX ## _cavs_qpel ## NUM ## _mc01_ ## EXT; \
    c->PFX ## _cavs_qpel_pixels_tab[IDX][ 8] = PFX ## _cavs_qpel ## NUM ## _mc02_ ## EXT; \
    c->PFX ## _cavs_qpel_pixels_tab[IDX][12] = PFX ## _cavs_qpel ## NUM ## _mc03_ ## EXT; \

#if HAVE_MMXEXT_INLINE
QPEL_CAVS(put_,        PUT_OP, mmxext)
QPEL_CAVS(avg_, AVG_MMXEXT_OP, mmxext)

CAVS_MC(put_,  8, mmxext)
CAVS_MC(put_, 16, mmxext)
CAVS_MC(avg_,  8, mmxext)
CAVS_MC(avg_, 16, mmxext)

static av_cold void cavsdsp_init_mmxext(CAVSDSPContext *c,
                                        AVCodecContext *avctx)
{
    DSPFUNC(put, 0, 16, mmxext);
    DSPFUNC(put, 1,  8, mmxext);
    DSPFUNC(avg, 0, 16, mmxext);
    DSPFUNC(avg, 1,  8, mmxext);
}
#endif /* HAVE_MMXEXT_INLINE */

#if HAVE_AMD3DNOW_INLINE
QPEL_CAVS(put_,       PUT_OP, 3dnow)
QPEL_CAVS(avg_, AVG_3DNOW_OP, 3dnow)

CAVS_MC(put_, 8, 3dnow)
CAVS_MC(put_, 16,3dnow)
CAVS_MC(avg_, 8, 3dnow)
CAVS_MC(avg_, 16,3dnow)

static av_cold void cavsdsp_init_3dnow(CAVSDSPContext *c,
                                       AVCodecContext *avctx)
{
    DSPFUNC(put, 0, 16, 3dnow);
    DSPFUNC(put, 1,  8, 3dnow);
    DSPFUNC(avg, 0, 16, 3dnow);
    DSPFUNC(avg, 1,  8, 3dnow);
}
#endif /* HAVE_AMD3DNOW_INLINE */

av_cold void ff_cavsdsp_init_x86(CAVSDSPContext *c, AVCodecContext *avctx)
{
#if HAVE_MMX_INLINE
    int cpu_flags = av_get_cpu_flags();

    if (INLINE_MMX(cpu_flags))
        cavsdsp_init_mmx(c, avctx);
#endif /* HAVE_MMX_INLINE */
#if HAVE_AMD3DNOW_INLINE
    if (INLINE_AMD3DNOW(cpu_flags))
        cavsdsp_init_3dnow(c, avctx);
#endif /* HAVE_AMD3DNOW_INLINE */
#if HAVE_MMXEXT_INLINE
    if (INLINE_MMXEXT(cpu_flags))
        cavsdsp_init_mmxext(c, avctx);
#endif /* HAVE_MMXEXT_INLINE */
}
