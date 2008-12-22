/*
 * Chinese AVS video (AVS1-P2, JiZhun profile) decoder.
 * Copyright (c) 2006  Stefan Gehrer <stefan.gehrer@gmx.de>
 *
 * MMX-optimized DSP functions, based on H.264 optimizations by
 * Michael Niedermayer and Loren Merritt
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

#include "libavutil/common.h"
#include "libavutil/x86_cpu.h"
#include "libavcodec/dsputil.h"
#include "dsputil_mmx.h"

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

static void cavs_idct8_add_mmx(uint8_t *dst, int16_t *block, int stride)
{
    int i;
    DECLARE_ALIGNED_8(int16_t, b2[64]);

    for(i=0; i<2; i++){
        DECLARE_ALIGNED_8(uint64_t, tmp);

        cavs_idct8_1d(block+4*i, ff_pw_4);

        __asm__ volatile(
            "psraw     $3, %%mm7  \n\t"
            "psraw     $3, %%mm6  \n\t"
            "psraw     $3, %%mm5  \n\t"
            "psraw     $3, %%mm4  \n\t"
            "psraw     $3, %%mm3  \n\t"
            "psraw     $3, %%mm2  \n\t"
            "psraw     $3, %%mm1  \n\t"
            "psraw     $3, %%mm0  \n\t"
            "movq   %%mm7,    %0   \n\t"
            TRANSPOSE4( %%mm0, %%mm2, %%mm4, %%mm6, %%mm7 )
            "movq   %%mm0,  8(%1)  \n\t"
            "movq   %%mm6, 24(%1)  \n\t"
            "movq   %%mm7, 40(%1)  \n\t"
            "movq   %%mm4, 56(%1)  \n\t"
            "movq    %0,    %%mm7  \n\t"
            TRANSPOSE4( %%mm7, %%mm5, %%mm3, %%mm1, %%mm0 )
            "movq   %%mm7,   (%1)  \n\t"
            "movq   %%mm1, 16(%1)  \n\t"
            "movq   %%mm0, 32(%1)  \n\t"
            "movq   %%mm3, 48(%1)  \n\t"
            : "=m"(tmp)
            : "r"(b2+32*i)
            : "memory"
        );
    }

    for(i=0; i<2; i++){
        cavs_idct8_1d(b2+4*i, ff_pw_64);

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

    add_pixels_clamped_mmx(b2, dst, stride);

    /* clear block */
    __asm__ volatile(
            "pxor %%mm7, %%mm7   \n\t"
            "movq %%mm7, (%0)    \n\t"
            "movq %%mm7, 8(%0)   \n\t"
            "movq %%mm7, 16(%0)  \n\t"
            "movq %%mm7, 24(%0)  \n\t"
            "movq %%mm7, 32(%0)  \n\t"
            "movq %%mm7, 40(%0)  \n\t"
            "movq %%mm7, 48(%0)  \n\t"
            "movq %%mm7, 56(%0)  \n\t"
            "movq %%mm7, 64(%0)  \n\t"
            "movq %%mm7, 72(%0)  \n\t"
            "movq %%mm7, 80(%0)  \n\t"
            "movq %%mm7, 88(%0)  \n\t"
            "movq %%mm7, 96(%0)  \n\t"
            "movq %%mm7, 104(%0) \n\t"
            "movq %%mm7, 112(%0) \n\t"
            "movq %%mm7, 120(%0) \n\t"
            :: "r" (block)
    );
}

/*****************************************************************************
 *
 * motion compensation
 *
 ****************************************************************************/

/* vertical filter [-1 -2 96 42 -7  0]  */
#define QPEL_CAVSV1(A,B,C,D,E,F,OP)      \
        "movd (%0), "#F"            \n\t"\
        "movq "#C", %%mm6           \n\t"\
        "pmullw %5, %%mm6           \n\t"\
        "movq "#D", %%mm7           \n\t"\
        "pmullw %6, %%mm7           \n\t"\
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
#define QPEL_CAVSV2(A,B,C,D,E,F,OP)      \
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
#define QPEL_CAVSV3(A,B,C,D,E,F,OP)      \
        "movd (%0), "#F"            \n\t"\
        "movq "#C", %%mm6           \n\t"\
        "pmullw %6, %%mm6           \n\t"\
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
        VOP(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, OP)\
        VOP(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, OP)\
        VOP(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, OP)\
        VOP(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, OP)\
        VOP(%%mm4, %%mm5, %%mm0, %%mm1, %%mm2, %%mm3, OP)\
        VOP(%%mm5, %%mm0, %%mm1, %%mm2, %%mm3, %%mm4, OP)\
        VOP(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, OP)\
        VOP(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, OP)\
        \
        : "+a"(src), "+c"(dst)\
        : "S"((x86_reg)srcStride), "r"((x86_reg)dstStride), "m"(ADD), "m"(MUL1), "m"(MUL2)\
        : "memory"\
     );\
     if(h==16){\
        __asm__ volatile(\
            VOP(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, OP)\
            VOP(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, OP)\
            VOP(%%mm4, %%mm5, %%mm0, %%mm1, %%mm2, %%mm3, OP)\
            VOP(%%mm5, %%mm0, %%mm1, %%mm2, %%mm3, %%mm4, OP)\
            VOP(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, OP)\
            VOP(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, OP)\
            VOP(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, OP)\
            VOP(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, OP)\
            \
           : "+a"(src), "+c"(dst)\
           : "S"((x86_reg)srcStride), "r"((x86_reg)dstStride), "m"(ADD),  "m"(MUL1), "m"(MUL2)\
           : "memory"\
        );\
     }\
     src += 4-(h+5)*srcStride;\
     dst += 4-h*dstStride;\
   }

#define QPEL_CAVS(OPNAME, OP, MMX)\
static void OPNAME ## cavs_qpel8_h_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
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
static inline void OPNAME ## cavs_qpel8or16_v1_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride, int h){\
  QPEL_CAVSVNUM(QPEL_CAVSV1,OP,ff_pw_64,ff_pw_96,ff_pw_42)      \
}\
\
static inline void OPNAME ## cavs_qpel8or16_v2_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride, int h){\
  QPEL_CAVSVNUM(QPEL_CAVSV2,OP,ff_pw_4,ff_pw_5,ff_pw_5)         \
}\
\
static inline void OPNAME ## cavs_qpel8or16_v3_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride, int h){\
  QPEL_CAVSVNUM(QPEL_CAVSV3,OP,ff_pw_64,ff_pw_96,ff_pw_42)      \
}\
\
static void OPNAME ## cavs_qpel8_v1_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    OPNAME ## cavs_qpel8or16_v1_ ## MMX(dst  , src  , dstStride, srcStride, 8);\
}\
static void OPNAME ## cavs_qpel16_v1_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    OPNAME ## cavs_qpel8or16_v1_ ## MMX(dst  , src  , dstStride, srcStride, 16);\
    OPNAME ## cavs_qpel8or16_v1_ ## MMX(dst+8, src+8, dstStride, srcStride, 16);\
}\
\
static void OPNAME ## cavs_qpel8_v2_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    OPNAME ## cavs_qpel8or16_v2_ ## MMX(dst  , src  , dstStride, srcStride, 8);\
}\
static void OPNAME ## cavs_qpel16_v2_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    OPNAME ## cavs_qpel8or16_v2_ ## MMX(dst  , src  , dstStride, srcStride, 16);\
    OPNAME ## cavs_qpel8or16_v2_ ## MMX(dst+8, src+8, dstStride, srcStride, 16);\
}\
\
static void OPNAME ## cavs_qpel8_v3_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    OPNAME ## cavs_qpel8or16_v3_ ## MMX(dst  , src  , dstStride, srcStride, 8);\
}\
static void OPNAME ## cavs_qpel16_v3_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    OPNAME ## cavs_qpel8or16_v3_ ## MMX(dst  , src  , dstStride, srcStride, 16);\
    OPNAME ## cavs_qpel8or16_v3_ ## MMX(dst+8, src+8, dstStride, srcStride, 16);\
}\
\
static void OPNAME ## cavs_qpel16_h_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    OPNAME ## cavs_qpel8_h_ ## MMX(dst  , src  , dstStride, srcStride);\
    OPNAME ## cavs_qpel8_h_ ## MMX(dst+8, src+8, dstStride, srcStride);\
    src += 8*srcStride;\
    dst += 8*dstStride;\
    OPNAME ## cavs_qpel8_h_ ## MMX(dst  , src  , dstStride, srcStride);\
    OPNAME ## cavs_qpel8_h_ ## MMX(dst+8, src+8, dstStride, srcStride);\
}\

#define CAVS_MC(OPNAME, SIZE, MMX) \
static void ff_ ## OPNAME ## cavs_qpel ## SIZE ## _mc20_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## cavs_qpel ## SIZE ## _h_ ## MMX(dst, src, stride, stride);\
}\
\
static void ff_ ## OPNAME ## cavs_qpel ## SIZE ## _mc01_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## cavs_qpel ## SIZE ## _v1_ ## MMX(dst, src, stride, stride);\
}\
\
static void ff_ ## OPNAME ## cavs_qpel ## SIZE ## _mc02_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## cavs_qpel ## SIZE ## _v2_ ## MMX(dst, src, stride, stride);\
}\
\
static void ff_ ## OPNAME ## cavs_qpel ## SIZE ## _mc03_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## cavs_qpel ## SIZE ## _v3_ ## MMX(dst, src, stride, stride);\
}\

#define PUT_OP(a,b,temp, size) "mov" #size " " #a ", " #b "    \n\t"
#define AVG_3DNOW_OP(a,b,temp, size) \
"mov" #size " " #b ", " #temp "   \n\t"\
"pavgusb " #temp ", " #a "        \n\t"\
"mov" #size " " #a ", " #b "      \n\t"
#define AVG_MMX2_OP(a,b,temp, size) \
"mov" #size " " #b ", " #temp "   \n\t"\
"pavgb " #temp ", " #a "          \n\t"\
"mov" #size " " #a ", " #b "      \n\t"

QPEL_CAVS(put_,       PUT_OP, 3dnow)
QPEL_CAVS(avg_, AVG_3DNOW_OP, 3dnow)
QPEL_CAVS(put_,       PUT_OP, mmx2)
QPEL_CAVS(avg_,  AVG_MMX2_OP, mmx2)

CAVS_MC(put_, 8, 3dnow)
CAVS_MC(put_, 16,3dnow)
CAVS_MC(avg_, 8, 3dnow)
CAVS_MC(avg_, 16,3dnow)
CAVS_MC(put_, 8, mmx2)
CAVS_MC(put_, 16,mmx2)
CAVS_MC(avg_, 8, mmx2)
CAVS_MC(avg_, 16,mmx2)

void ff_put_cavs_qpel8_mc00_mmx2(uint8_t *dst, uint8_t *src, int stride);
void ff_avg_cavs_qpel8_mc00_mmx2(uint8_t *dst, uint8_t *src, int stride);
void ff_put_cavs_qpel16_mc00_mmx2(uint8_t *dst, uint8_t *src, int stride);
void ff_avg_cavs_qpel16_mc00_mmx2(uint8_t *dst, uint8_t *src, int stride);

void ff_cavsdsp_init_mmx2(DSPContext* c, AVCodecContext *avctx) {
#define dspfunc(PFX, IDX, NUM) \
    c->PFX ## _pixels_tab[IDX][ 0] = ff_ ## PFX ## NUM ## _mc00_mmx2; \
    c->PFX ## _pixels_tab[IDX][ 2] = ff_ ## PFX ## NUM ## _mc20_mmx2; \
    c->PFX ## _pixels_tab[IDX][ 4] = ff_ ## PFX ## NUM ## _mc01_mmx2; \
    c->PFX ## _pixels_tab[IDX][ 8] = ff_ ## PFX ## NUM ## _mc02_mmx2; \
    c->PFX ## _pixels_tab[IDX][12] = ff_ ## PFX ## NUM ## _mc03_mmx2; \

    dspfunc(put_cavs_qpel, 0, 16);
    dspfunc(put_cavs_qpel, 1, 8);
    dspfunc(avg_cavs_qpel, 0, 16);
    dspfunc(avg_cavs_qpel, 1, 8);
#undef dspfunc
    c->cavs_idct8_add = cavs_idct8_add_mmx;
}

void ff_cavsdsp_init_3dnow(DSPContext* c, AVCodecContext *avctx) {
#define dspfunc(PFX, IDX, NUM) \
    c->PFX ## _pixels_tab[IDX][ 0] = ff_ ## PFX ## NUM ## _mc00_mmx2; \
    c->PFX ## _pixels_tab[IDX][ 2] = ff_ ## PFX ## NUM ## _mc20_3dnow; \
    c->PFX ## _pixels_tab[IDX][ 4] = ff_ ## PFX ## NUM ## _mc01_3dnow; \
    c->PFX ## _pixels_tab[IDX][ 8] = ff_ ## PFX ## NUM ## _mc02_3dnow; \
    c->PFX ## _pixels_tab[IDX][12] = ff_ ## PFX ## NUM ## _mc03_3dnow; \

    dspfunc(put_cavs_qpel, 0, 16);
    dspfunc(put_cavs_qpel, 1, 8);
    dspfunc(avg_cavs_qpel, 0, 16);
    dspfunc(avg_cavs_qpel, 1, 8);
#undef dspfunc
    c->cavs_idct8_add = cavs_idct8_add_mmx;
}
