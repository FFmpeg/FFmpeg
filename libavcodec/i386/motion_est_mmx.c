/*
 * MMX optimized motion estimation
 * Copyright (c) 2001 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * mostly by Michael Niedermayer <michaelni@gmx.at>
 */
#include "../dsputil.h"

static const __attribute__ ((aligned(8))) UINT64 round_tab[3]={
0x0000000000000000,
0x0001000100010001,
0x0002000200020002,
};

static __attribute__ ((aligned(8))) uint64_t bone= 0x0101010101010101LL;

static inline void sad8_mmx(UINT8 *blk1, UINT8 *blk2, int stride, int h)
{
    int len= -(stride<<h);
    asm volatile(
        ".balign 16			\n\t"
        "1:				\n\t"
        "movq (%1, %%eax), %%mm0	\n\t"
        "movq (%2, %%eax), %%mm2	\n\t"
        "movq (%2, %%eax), %%mm4	\n\t"
        "addl %3, %%eax			\n\t"
        "psubusb %%mm0, %%mm2		\n\t"
        "psubusb %%mm4, %%mm0		\n\t"
        "movq (%1, %%eax), %%mm1	\n\t"
        "movq (%2, %%eax), %%mm3	\n\t"
        "movq (%2, %%eax), %%mm5	\n\t"
        "psubusb %%mm1, %%mm3		\n\t"
        "psubusb %%mm5, %%mm1		\n\t"
        "por %%mm2, %%mm0		\n\t"
        "por %%mm1, %%mm3		\n\t"
        "movq %%mm0, %%mm1		\n\t"
        "movq %%mm3, %%mm2		\n\t"
        "punpcklbw %%mm7, %%mm0		\n\t"
        "punpckhbw %%mm7, %%mm1		\n\t"
        "punpcklbw %%mm7, %%mm3		\n\t"
        "punpckhbw %%mm7, %%mm2		\n\t"
        "paddw %%mm1, %%mm0		\n\t"
        "paddw %%mm3, %%mm2		\n\t"
        "paddw %%mm2, %%mm0		\n\t"
        "paddw %%mm0, %%mm6		\n\t"
        "addl %3, %%eax			\n\t"
        " js 1b				\n\t"
        : "+a" (len)
        : "r" (blk1 - len), "r" (blk2 - len), "r" (stride)
    );
}

static inline void sad8_mmx2(UINT8 *blk1, UINT8 *blk2, int stride, int h)
{
    int len= -(stride<<h);
    asm volatile(
        ".balign 16			\n\t"
        "1:				\n\t"
        "movq (%1, %%eax), %%mm0	\n\t"
        "movq (%2, %%eax), %%mm2	\n\t"
        "psadbw %%mm2, %%mm0		\n\t"
        "addl %3, %%eax			\n\t"
        "movq (%1, %%eax), %%mm1	\n\t"
        "movq (%2, %%eax), %%mm3	\n\t"
        "psadbw %%mm1, %%mm3		\n\t"
        "paddw %%mm3, %%mm0		\n\t"
        "paddw %%mm0, %%mm6		\n\t"
        "addl %3, %%eax			\n\t"
        " js 1b				\n\t"
        : "+a" (len)
        : "r" (blk1 - len), "r" (blk2 - len), "r" (stride)
    );
}

static inline void sad8_2_mmx2(UINT8 *blk1a, UINT8 *blk1b, UINT8 *blk2, int stride, int h)
{
    int len= -(stride<<h);
    asm volatile(
        ".balign 16			\n\t"
        "1:				\n\t"
        "movq (%1, %%eax), %%mm0	\n\t"
        "movq (%2, %%eax), %%mm2	\n\t"
        "pavgb %%mm2, %%mm0		\n\t"
        "movq (%3, %%eax), %%mm2	\n\t"
        "psadbw %%mm2, %%mm0		\n\t"
        "addl %4, %%eax			\n\t"
        "movq (%1, %%eax), %%mm1	\n\t"
        "movq (%2, %%eax), %%mm3	\n\t"
        "pavgb %%mm1, %%mm3		\n\t"
        "movq (%3, %%eax), %%mm1	\n\t"
        "psadbw %%mm1, %%mm3		\n\t"
        "paddw %%mm3, %%mm0		\n\t"
        "paddw %%mm0, %%mm6		\n\t"
        "addl %4, %%eax			\n\t"
        " js 1b				\n\t"
        : "+a" (len)
        : "r" (blk1a - len), "r" (blk1b -len), "r" (blk2 - len), "r" (stride)
    );
}

static inline void sad8_4_mmx2(UINT8 *blk1, UINT8 *blk2, int stride, int h)
{ //FIXME reuse src
    int len= -(stride<<h);
    asm volatile(
        ".balign 16			\n\t"
        "movq "MANGLE(bone)", %%mm5	\n\t"
        "1:				\n\t" 
        "movq (%1, %%eax), %%mm0	\n\t"
        "movq (%2, %%eax), %%mm2	\n\t"
        "movq 1(%1, %%eax), %%mm1	\n\t"
        "movq 1(%2, %%eax), %%mm3	\n\t"
        "pavgb %%mm2, %%mm0		\n\t"
        "pavgb %%mm1, %%mm3		\n\t"
        "psubusb %%mm5, %%mm3		\n\t"
        "pavgb %%mm3, %%mm0		\n\t"
        "movq (%3, %%eax), %%mm2	\n\t"
        "psadbw %%mm2, %%mm0		\n\t"
        "addl %4, %%eax			\n\t"
        "movq (%1, %%eax), %%mm1	\n\t"
        "movq (%2, %%eax), %%mm3	\n\t"
        "movq 1(%1, %%eax), %%mm2	\n\t"
        "movq 1(%2, %%eax), %%mm4	\n\t"
        "pavgb %%mm3, %%mm1		\n\t"
        "pavgb %%mm4, %%mm2		\n\t"
        "psubusb %%mm5, %%mm2		\n\t"
        "pavgb %%mm1, %%mm2		\n\t"
        "movq (%3, %%eax), %%mm1	\n\t"
        "psadbw %%mm1, %%mm2		\n\t"
        "paddw %%mm2, %%mm0		\n\t"
        "paddw %%mm0, %%mm6		\n\t"
        "addl %4, %%eax			\n\t"
        " js 1b				\n\t"
        : "+a" (len)
        : "r" (blk1 - len), "r" (blk1 - len + stride), "r" (blk2 - len), "r" (stride)
    );
}

static inline void sad8_2_mmx(UINT8 *blk1a, UINT8 *blk1b, UINT8 *blk2, int stride, int h)
{
    int len= -(stride<<h);
    asm volatile(
        ".balign 16			\n\t"
        "1:				\n\t"
        "movq (%1, %%eax), %%mm0	\n\t"
        "movq (%2, %%eax), %%mm1	\n\t"
        "movq (%1, %%eax), %%mm2	\n\t"
        "movq (%2, %%eax), %%mm3	\n\t"
        "punpcklbw %%mm7, %%mm0		\n\t"
        "punpcklbw %%mm7, %%mm1		\n\t"
        "punpckhbw %%mm7, %%mm2		\n\t"
        "punpckhbw %%mm7, %%mm3		\n\t"
        "paddw %%mm0, %%mm1		\n\t"
        "paddw %%mm2, %%mm3		\n\t"
        "movq (%3, %%eax), %%mm4	\n\t" 
        "movq (%3, %%eax), %%mm2	\n\t"
        "paddw %%mm5, %%mm1		\n\t"
        "paddw %%mm5, %%mm3		\n\t"
        "psrlw $1, %%mm1		\n\t"
        "psrlw $1, %%mm3		\n\t"
        "packuswb %%mm3, %%mm1		\n\t"
        "psubusb %%mm1, %%mm4		\n\t"
        "psubusb %%mm2, %%mm1		\n\t"
        "por %%mm4, %%mm1		\n\t"
        "movq %%mm1, %%mm0		\n\t"
        "punpcklbw %%mm7, %%mm0		\n\t"
        "punpckhbw %%mm7, %%mm1		\n\t"
        "paddw %%mm1, %%mm0		\n\t"
        "paddw %%mm0, %%mm6		\n\t"
        "addl %4, %%eax			\n\t"
        " js 1b				\n\t"
        : "+a" (len)
        : "r" (blk1a - len), "r" (blk1b -len), "r" (blk2 - len), "r" (stride)
    );
}

static inline void sad8_4_mmx(UINT8 *blk1, UINT8 *blk2, int stride, int h)
{
    int len= -(stride<<h);
    asm volatile(
        ".balign 16			\n\t"
        "1:				\n\t"
        "movq (%1, %%eax), %%mm0	\n\t"
        "movq (%2, %%eax), %%mm1	\n\t"
        "movq %%mm0, %%mm4		\n\t"
        "movq %%mm1, %%mm2		\n\t"
        "punpcklbw %%mm7, %%mm0		\n\t"
        "punpcklbw %%mm7, %%mm1		\n\t"
        "punpckhbw %%mm7, %%mm4		\n\t"
        "punpckhbw %%mm7, %%mm2		\n\t"
        "paddw %%mm1, %%mm0		\n\t"
        "paddw %%mm2, %%mm4		\n\t"
        "movq 1(%1, %%eax), %%mm2	\n\t"
        "movq 1(%2, %%eax), %%mm3	\n\t"
        "movq %%mm2, %%mm1		\n\t"
        "punpcklbw %%mm7, %%mm2		\n\t"
        "punpckhbw %%mm7, %%mm1		\n\t"
        "paddw %%mm0, %%mm2		\n\t"
        "paddw %%mm4, %%mm1		\n\t"
        "movq %%mm3, %%mm4		\n\t"
        "punpcklbw %%mm7, %%mm3		\n\t"
        "punpckhbw %%mm7, %%mm4		\n\t"
        "paddw %%mm3, %%mm2		\n\t"
        "paddw %%mm4, %%mm1		\n\t"
        "movq (%3, %%eax), %%mm3	\n\t" 
        "movq (%3, %%eax), %%mm4	\n\t" 
        "paddw %%mm5, %%mm2		\n\t"
        "paddw %%mm5, %%mm1		\n\t"
        "psrlw $2, %%mm2		\n\t"
        "psrlw $2, %%mm1		\n\t"
        "packuswb %%mm1, %%mm2		\n\t"
        "psubusb %%mm2, %%mm3		\n\t"
        "psubusb %%mm4, %%mm2		\n\t"
        "por %%mm3, %%mm2		\n\t"
        "movq %%mm2, %%mm0		\n\t"
        "punpcklbw %%mm7, %%mm0		\n\t"
        "punpckhbw %%mm7, %%mm2		\n\t"
        "paddw %%mm2, %%mm0		\n\t"
        "paddw %%mm0, %%mm6		\n\t"
        "addl %4, %%eax			\n\t"
        " js 1b				\n\t"
        : "+a" (len)
        : "r" (blk1 - len), "r" (blk1 -len + stride), "r" (blk2 - len), "r" (stride)
    );
}

static inline int sum_mmx()
{
    int ret;
    asm volatile(
        "movq %%mm6, %%mm0		\n\t"
        "psrlq $32, %%mm6		\n\t"
        "paddw %%mm0, %%mm6		\n\t"
        "movq %%mm6, %%mm0		\n\t"
        "psrlq $16, %%mm6		\n\t"
        "paddw %%mm0, %%mm6		\n\t"
        "movd %%mm6, %0			\n\t"
        : "=r" (ret)
    );
    return ret&0xFFFF;
}

static inline int sum_mmx2()
{
    int ret;
    asm volatile(
        "movd %%mm6, %0			\n\t"
        : "=r" (ret)
    );
    return ret;
}


#define PIX_SAD(suf)\
int pix_abs8x8_ ## suf(UINT8 *blk2, UINT8 *blk1, int stride)\
{\
    asm volatile("pxor %%mm7, %%mm7		\n\t"\
                 "pxor %%mm6, %%mm6		\n\t":);\
\
    sad8_ ## suf(blk1, blk2, stride, 3);\
\
    return sum_ ## suf();\
}\
\
int pix_abs8x8_x2_ ## suf(UINT8 *blk2, UINT8 *blk1, int stride)\
{\
    asm volatile("pxor %%mm7, %%mm7		\n\t"\
                 "pxor %%mm6, %%mm6		\n\t"\
                 "movq %0, %%mm5		\n\t"\
                 :: "m"(round_tab[1]) \
                 );\
\
    sad8_2_ ## suf(blk1, blk1+1, blk2, stride, 3);\
\
    return sum_ ## suf();\
}\
\
int pix_abs8x8_y2_ ## suf(UINT8 *blk2, UINT8 *blk1, int stride)\
{\
    asm volatile("pxor %%mm7, %%mm7		\n\t"\
                 "pxor %%mm6, %%mm6		\n\t"\
                 "movq %0, %%mm5		\n\t"\
                 :: "m"(round_tab[1]) \
                 );\
\
    sad8_2_ ## suf(blk1, blk1+stride, blk2, stride, 3);\
\
    return sum_ ## suf();\
}\
\
int pix_abs8x8_xy2_ ## suf(UINT8 *blk2, UINT8 *blk1, int stride)\
{\
    asm volatile("pxor %%mm7, %%mm7		\n\t"\
                 "pxor %%mm6, %%mm6		\n\t"\
                 "movq %0, %%mm5		\n\t"\
                 :: "m"(round_tab[2]) \
                 );\
\
    sad8_4_ ## suf(blk1, blk2, stride, 3);\
\
    return sum_ ## suf();\
}\
\
int pix_abs16x16_ ## suf(UINT8 *blk2, UINT8 *blk1, int stride)\
{\
    asm volatile("pxor %%mm7, %%mm7		\n\t"\
                 "pxor %%mm6, %%mm6		\n\t":);\
\
    sad8_ ## suf(blk1  , blk2  , stride, 4);\
    sad8_ ## suf(blk1+8, blk2+8, stride, 4);\
\
    return sum_ ## suf();\
}\
int pix_abs16x16_x2_ ## suf(UINT8 *blk2, UINT8 *blk1, int stride)\
{\
    asm volatile("pxor %%mm7, %%mm7		\n\t"\
                 "pxor %%mm6, %%mm6		\n\t"\
                 "movq %0, %%mm5		\n\t"\
                 :: "m"(round_tab[1]) \
                 );\
\
    sad8_2_ ## suf(blk1  , blk1+1, blk2  , stride, 4);\
    sad8_2_ ## suf(blk1+8, blk1+9, blk2+8, stride, 4);\
\
    return sum_ ## suf();\
}\
int pix_abs16x16_y2_ ## suf(UINT8 *blk2, UINT8 *blk1, int stride)\
{\
    asm volatile("pxor %%mm7, %%mm7		\n\t"\
                 "pxor %%mm6, %%mm6		\n\t"\
                 "movq %0, %%mm5		\n\t"\
                 :: "m"(round_tab[1]) \
                 );\
\
    sad8_2_ ## suf(blk1  , blk1+stride,  blk2  , stride, 4);\
    sad8_2_ ## suf(blk1+8, blk1+stride+8,blk2+8, stride, 4);\
\
    return sum_ ## suf();\
}\
int pix_abs16x16_xy2_ ## suf(UINT8 *blk2, UINT8 *blk1, int stride)\
{\
    asm volatile("pxor %%mm7, %%mm7		\n\t"\
                 "pxor %%mm6, %%mm6		\n\t"\
                 "movq %0, %%mm5		\n\t"\
                 :: "m"(round_tab[2]) \
                 );\
\
    sad8_4_ ## suf(blk1  , blk2  , stride, 4);\
    sad8_4_ ## suf(blk1+8, blk2+8, stride, 4);\
\
    return sum_ ## suf();\
}\

PIX_SAD(mmx)
PIX_SAD(mmx2)
