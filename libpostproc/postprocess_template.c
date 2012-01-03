/*
 * Copyright (C) 2001-2002 Michael Niedermayer (michaelni@gmx.at)
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * mmx/mmx2/3dnow postprocess code.
 */

#include "libavutil/x86_cpu.h"

#define ALIGN_MASK "$-8"

#undef REAL_PAVGB
#undef PAVGB
#undef PMINUB
#undef PMAXUB

#if   HAVE_MMX2
#define REAL_PAVGB(a,b) "pavgb " #a ", " #b " \n\t"
#elif HAVE_AMD3DNOW
#define REAL_PAVGB(a,b) "pavgusb " #a ", " #b " \n\t"
#endif
#define PAVGB(a,b)  REAL_PAVGB(a,b)

#if   HAVE_MMX2
#define PMINUB(a,b,t) "pminub " #a ", " #b " \n\t"
#elif HAVE_MMX
#define PMINUB(b,a,t) \
    "movq " #a ", " #t " \n\t"\
    "psubusb " #b ", " #t " \n\t"\
    "psubb " #t ", " #a " \n\t"
#endif

#if   HAVE_MMX2
#define PMAXUB(a,b) "pmaxub " #a ", " #b " \n\t"
#elif HAVE_MMX
#define PMAXUB(a,b) \
    "psubusb " #a ", " #b " \n\t"\
    "paddb " #a ", " #b " \n\t"
#endif

//FIXME? |255-0| = 1 (should not be a problem ...)
#if HAVE_MMX
/**
 * Check if the middle 8x8 Block in the given 8x16 block is flat
 */
static inline int RENAME(vertClassify)(uint8_t src[], int stride, PPContext *c){
    int numEq= 0, dcOk;
    src+= stride*4; // src points to begin of the 8x8 Block
    __asm__ volatile(
        "movq %0, %%mm7                         \n\t"
        "movq %1, %%mm6                         \n\t"
        : : "m" (c->mmxDcOffset[c->nonBQP]),  "m" (c->mmxDcThreshold[c->nonBQP])
        );

    __asm__ volatile(
        "lea (%2, %3), %%"REG_a"                \n\t"
//      0       1       2       3       4       5       6       7       8       9
//      %1      eax     eax+%2  eax+2%2 %1+4%2  ecx     ecx+%2  ecx+2%2 %1+8%2  ecx+4%2

        "movq (%2), %%mm0                       \n\t"
        "movq (%%"REG_a"), %%mm1                \n\t"
        "movq %%mm0, %%mm3                      \n\t"
        "movq %%mm0, %%mm4                      \n\t"
        PMAXUB(%%mm1, %%mm4)
        PMINUB(%%mm1, %%mm3, %%mm5)
        "psubb %%mm1, %%mm0                     \n\t" // mm0 = differnece
        "paddb %%mm7, %%mm0                     \n\t"
        "pcmpgtb %%mm6, %%mm0                   \n\t"

        "movq (%%"REG_a",%3), %%mm2             \n\t"
        PMAXUB(%%mm2, %%mm4)
        PMINUB(%%mm2, %%mm3, %%mm5)
        "psubb %%mm2, %%mm1                     \n\t"
        "paddb %%mm7, %%mm1                     \n\t"
        "pcmpgtb %%mm6, %%mm1                   \n\t"
        "paddb %%mm1, %%mm0                     \n\t"

        "movq (%%"REG_a", %3, 2), %%mm1         \n\t"
        PMAXUB(%%mm1, %%mm4)
        PMINUB(%%mm1, %%mm3, %%mm5)
        "psubb %%mm1, %%mm2                     \n\t"
        "paddb %%mm7, %%mm2                     \n\t"
        "pcmpgtb %%mm6, %%mm2                   \n\t"
        "paddb %%mm2, %%mm0                     \n\t"

        "lea (%%"REG_a", %3, 4), %%"REG_a"      \n\t"

        "movq (%2, %3, 4), %%mm2                \n\t"
        PMAXUB(%%mm2, %%mm4)
        PMINUB(%%mm2, %%mm3, %%mm5)
        "psubb %%mm2, %%mm1                     \n\t"
        "paddb %%mm7, %%mm1                     \n\t"
        "pcmpgtb %%mm6, %%mm1                   \n\t"
        "paddb %%mm1, %%mm0                     \n\t"

        "movq (%%"REG_a"), %%mm1                \n\t"
        PMAXUB(%%mm1, %%mm4)
        PMINUB(%%mm1, %%mm3, %%mm5)
        "psubb %%mm1, %%mm2                     \n\t"
        "paddb %%mm7, %%mm2                     \n\t"
        "pcmpgtb %%mm6, %%mm2                   \n\t"
        "paddb %%mm2, %%mm0                     \n\t"

        "movq (%%"REG_a", %3), %%mm2            \n\t"
        PMAXUB(%%mm2, %%mm4)
        PMINUB(%%mm2, %%mm3, %%mm5)
        "psubb %%mm2, %%mm1                     \n\t"
        "paddb %%mm7, %%mm1                     \n\t"
        "pcmpgtb %%mm6, %%mm1                   \n\t"
        "paddb %%mm1, %%mm0                     \n\t"

        "movq (%%"REG_a", %3, 2), %%mm1         \n\t"
        PMAXUB(%%mm1, %%mm4)
        PMINUB(%%mm1, %%mm3, %%mm5)
        "psubb %%mm1, %%mm2                     \n\t"
        "paddb %%mm7, %%mm2                     \n\t"
        "pcmpgtb %%mm6, %%mm2                   \n\t"
        "paddb %%mm2, %%mm0                     \n\t"
        "psubusb %%mm3, %%mm4                   \n\t"

        "                                       \n\t"
#if HAVE_MMX2
        "pxor %%mm7, %%mm7                      \n\t"
        "psadbw %%mm7, %%mm0                    \n\t"
#else
        "movq %%mm0, %%mm1                      \n\t"
        "psrlw $8, %%mm0                        \n\t"
        "paddb %%mm1, %%mm0                     \n\t"
        "movq %%mm0, %%mm1                      \n\t"
        "psrlq $16, %%mm0                       \n\t"
        "paddb %%mm1, %%mm0                     \n\t"
        "movq %%mm0, %%mm1                      \n\t"
        "psrlq $32, %%mm0                       \n\t"
        "paddb %%mm1, %%mm0                     \n\t"
#endif
        "movq %4, %%mm7                         \n\t" // QP,..., QP
        "paddusb %%mm7, %%mm7                   \n\t" // 2QP ... 2QP
        "psubusb %%mm7, %%mm4                   \n\t" // Diff <= 2QP -> 0
        "packssdw %%mm4, %%mm4                  \n\t"
        "movd %%mm0, %0                         \n\t"
        "movd %%mm4, %1                         \n\t"

        : "=r" (numEq), "=r" (dcOk)
        : "r" (src), "r" ((x86_reg)stride), "m" (c->pQPb)
        : "%"REG_a
        );

    numEq= (-numEq) &0xFF;
    if(numEq > c->ppMode.flatnessThreshold){
        if(dcOk) return 0;
        else     return 1;
    }else{
        return 2;
    }
}
#endif //HAVE_MMX

/**
 * Do a vertical low pass filter on the 8x16 block (only write to the 8x8 block in the middle)
 * using the 9-Tap Filter (1,1,2,2,4,2,2,1,1)/16
 */
#if !HAVE_ALTIVEC
static inline void RENAME(doVertLowPass)(uint8_t *src, int stride, PPContext *c)
{
#if HAVE_MMX2 || HAVE_AMD3DNOW
    src+= stride*3;
    __asm__ volatile(        //"movv %0 %1 %2\n\t"
        "movq %2, %%mm0                         \n\t"  // QP,..., QP
        "pxor %%mm4, %%mm4                      \n\t"

        "movq (%0), %%mm6                       \n\t"
        "movq (%0, %1), %%mm5                   \n\t"
        "movq %%mm5, %%mm1                      \n\t"
        "movq %%mm6, %%mm2                      \n\t"
        "psubusb %%mm6, %%mm5                   \n\t"
        "psubusb %%mm1, %%mm2                   \n\t"
        "por %%mm5, %%mm2                       \n\t" // ABS Diff of lines
        "psubusb %%mm0, %%mm2                   \n\t" // diff <= QP -> 0
        "pcmpeqb %%mm4, %%mm2                   \n\t" // diff <= QP -> FF

        "pand %%mm2, %%mm6                      \n\t"
        "pandn %%mm1, %%mm2                     \n\t"
        "por %%mm2, %%mm6                       \n\t"// First Line to Filter

        "movq (%0, %1, 8), %%mm5                \n\t"
        "lea (%0, %1, 4), %%"REG_a"             \n\t"
        "lea (%0, %1, 8), %%"REG_c"             \n\t"
        "sub %1, %%"REG_c"                      \n\t"
        "add %1, %0                             \n\t" // %0 points to line 1 not 0
        "movq (%0, %1, 8), %%mm7                \n\t"
        "movq %%mm5, %%mm1                      \n\t"
        "movq %%mm7, %%mm2                      \n\t"
        "psubusb %%mm7, %%mm5                   \n\t"
        "psubusb %%mm1, %%mm2                   \n\t"
        "por %%mm5, %%mm2                       \n\t" // ABS Diff of lines
        "psubusb %%mm0, %%mm2                   \n\t" // diff <= QP -> 0
        "pcmpeqb %%mm4, %%mm2                   \n\t" // diff <= QP -> FF

        "pand %%mm2, %%mm7                      \n\t"
        "pandn %%mm1, %%mm2                     \n\t"
        "por %%mm2, %%mm7                       \n\t" // First Line to Filter


        //      1       2       3       4       5       6       7       8
        //      %0      %0+%1   %0+2%1  eax     %0+4%1  eax+2%1 ecx     eax+4%1
        // 6 4 2 2 1 1
        // 6 4 4 2
        // 6 8 2

        "movq (%0, %1), %%mm0                   \n\t" //  1
        "movq %%mm0, %%mm1                      \n\t" //  1
        PAVGB(%%mm6, %%mm0)                           //1 1        /2
        PAVGB(%%mm6, %%mm0)                           //3 1        /4

        "movq (%0, %1, 4), %%mm2                \n\t" //     1
        "movq %%mm2, %%mm5                      \n\t" //     1
        PAVGB((%%REGa), %%mm2)                        //    11        /2
        PAVGB((%0, %1, 2), %%mm2)                     //   211        /4
        "movq %%mm2, %%mm3                      \n\t" //   211        /4
        "movq (%0), %%mm4                       \n\t" // 1
        PAVGB(%%mm4, %%mm3)                           // 4 211        /8
        PAVGB(%%mm0, %%mm3)                           //642211        /16
        "movq %%mm3, (%0)                       \n\t" // X
        // mm1=2 mm2=3(211) mm4=1 mm5=5 mm6=0 mm7=9
        "movq %%mm1, %%mm0                      \n\t" //  1
        PAVGB(%%mm6, %%mm0)                           //1 1        /2
        "movq %%mm4, %%mm3                      \n\t" // 1
        PAVGB((%0,%1,2), %%mm3)                       // 1 1        /2
        PAVGB((%%REGa,%1,2), %%mm5)                   //     11        /2
        PAVGB((%%REGa), %%mm5)                        //    211 /4
        PAVGB(%%mm5, %%mm3)                           // 2 2211 /8
        PAVGB(%%mm0, %%mm3)                           //4242211 /16
        "movq %%mm3, (%0,%1)                    \n\t" //  X
        // mm1=2 mm2=3(211) mm4=1 mm5=4(211) mm6=0 mm7=9
        PAVGB(%%mm4, %%mm6)                                   //11        /2
        "movq (%%"REG_c"), %%mm0                \n\t" //       1
        PAVGB((%%REGa, %1, 2), %%mm0)                 //      11/2
        "movq %%mm0, %%mm3                      \n\t" //      11/2
        PAVGB(%%mm1, %%mm0)                           //  2   11/4
        PAVGB(%%mm6, %%mm0)                           //222   11/8
        PAVGB(%%mm2, %%mm0)                           //22242211/16
        "movq (%0, %1, 2), %%mm2                \n\t" //   1
        "movq %%mm0, (%0, %1, 2)                \n\t" //   X
        // mm1=2 mm2=3 mm3=6(11) mm4=1 mm5=4(211) mm6=0(11) mm7=9
        "movq (%%"REG_a", %1, 4), %%mm0         \n\t" //        1
        PAVGB((%%REGc), %%mm0)                        //       11        /2
        PAVGB(%%mm0, %%mm6)                           //11     11        /4
        PAVGB(%%mm1, %%mm4)                           // 11                /2
        PAVGB(%%mm2, %%mm1)                           //  11                /2
        PAVGB(%%mm1, %%mm6)                           //1122   11        /8
        PAVGB(%%mm5, %%mm6)                           //112242211        /16
        "movq (%%"REG_a"), %%mm5                \n\t" //    1
        "movq %%mm6, (%%"REG_a")                \n\t" //    X
        // mm0=7(11) mm1=2(11) mm2=3 mm3=6(11) mm4=1(11) mm5=4 mm7=9
        "movq (%%"REG_a", %1, 4), %%mm6         \n\t" //        1
        PAVGB(%%mm7, %%mm6)                           //        11        /2
        PAVGB(%%mm4, %%mm6)                           // 11     11        /4
        PAVGB(%%mm3, %%mm6)                           // 11   2211        /8
        PAVGB(%%mm5, %%mm2)                           //   11                /2
        "movq (%0, %1, 4), %%mm4                \n\t" //     1
        PAVGB(%%mm4, %%mm2)                           //   112                /4
        PAVGB(%%mm2, %%mm6)                           // 112242211        /16
        "movq %%mm6, (%0, %1, 4)                \n\t" //     X
        // mm0=7(11) mm1=2(11) mm2=3(112) mm3=6(11) mm4=5 mm5=4 mm7=9
        PAVGB(%%mm7, %%mm1)                           //  11     2        /4
        PAVGB(%%mm4, %%mm5)                           //    11                /2
        PAVGB(%%mm5, %%mm0)                           //    11 11        /4
        "movq (%%"REG_a", %1, 2), %%mm6         \n\t" //      1
        PAVGB(%%mm6, %%mm1)                           //  11  4  2        /8
        PAVGB(%%mm0, %%mm1)                           //  11224222        /16
        "movq %%mm1, (%%"REG_a", %1, 2)         \n\t" //      X
        // mm2=3(112) mm3=6(11) mm4=5 mm5=4(11) mm6=6 mm7=9
        PAVGB((%%REGc), %%mm2)                        //   112 4        /8
        "movq (%%"REG_a", %1, 4), %%mm0         \n\t" //        1
        PAVGB(%%mm0, %%mm6)                           //      1 1        /2
        PAVGB(%%mm7, %%mm6)                           //      1 12        /4
        PAVGB(%%mm2, %%mm6)                           //   1122424        /4
        "movq %%mm6, (%%"REG_c")                \n\t" //       X
        // mm0=8 mm3=6(11) mm4=5 mm5=4(11) mm7=9
        PAVGB(%%mm7, %%mm5)                           //    11   2        /4
        PAVGB(%%mm7, %%mm5)                           //    11   6        /8

        PAVGB(%%mm3, %%mm0)                           //      112        /4
        PAVGB(%%mm0, %%mm5)                           //    112246        /16
        "movq %%mm5, (%%"REG_a", %1, 4)         \n\t" //        X
        "sub %1, %0                             \n\t"

        :
        : "r" (src), "r" ((x86_reg)stride), "m" (c->pQPb)
        : "%"REG_a, "%"REG_c
    );
#else //HAVE_MMX2 || HAVE_AMD3DNOW
    const int l1= stride;
    const int l2= stride + l1;
    const int l3= stride + l2;
    const int l4= stride + l3;
    const int l5= stride + l4;
    const int l6= stride + l5;
    const int l7= stride + l6;
    const int l8= stride + l7;
    const int l9= stride + l8;
    int x;
    src+= stride*3;
    for(x=0; x<BLOCK_SIZE; x++){
        const int first= FFABS(src[0] - src[l1]) < c->QP ? src[0] : src[l1];
        const int last= FFABS(src[l8] - src[l9]) < c->QP ? src[l9] : src[l8];

        int sums[10];
        sums[0] = 4*first + src[l1] + src[l2] + src[l3] + 4;
        sums[1] = sums[0] - first  + src[l4];
        sums[2] = sums[1] - first  + src[l5];
        sums[3] = sums[2] - first  + src[l6];
        sums[4] = sums[3] - first  + src[l7];
        sums[5] = sums[4] - src[l1] + src[l8];
        sums[6] = sums[5] - src[l2] + last;
        sums[7] = sums[6] - src[l3] + last;
        sums[8] = sums[7] - src[l4] + last;
        sums[9] = sums[8] - src[l5] + last;

        src[l1]= (sums[0] + sums[2] + 2*src[l1])>>4;
        src[l2]= (sums[1] + sums[3] + 2*src[l2])>>4;
        src[l3]= (sums[2] + sums[4] + 2*src[l3])>>4;
        src[l4]= (sums[3] + sums[5] + 2*src[l4])>>4;
        src[l5]= (sums[4] + sums[6] + 2*src[l5])>>4;
        src[l6]= (sums[5] + sums[7] + 2*src[l6])>>4;
        src[l7]= (sums[6] + sums[8] + 2*src[l7])>>4;
        src[l8]= (sums[7] + sums[9] + 2*src[l8])>>4;

        src++;
    }
#endif //HAVE_MMX2 || HAVE_AMD3DNOW
}
#endif //HAVE_ALTIVEC

/**
 * Experimental Filter 1
 * will not damage linear gradients
 * Flat blocks should look like they were passed through the (1,1,2,2,4,2,2,1,1) 9-Tap filter
 * can only smooth blocks at the expected locations (it cannot smooth them if they did move)
 * MMX2 version does correct clipping C version does not
 */
static inline void RENAME(vertX1Filter)(uint8_t *src, int stride, PPContext *co)
{
#if HAVE_MMX2 || HAVE_AMD3DNOW
    src+= stride*3;

    __asm__ volatile(
        "pxor %%mm7, %%mm7                      \n\t" // 0
        "lea (%0, %1), %%"REG_a"                \n\t"
        "lea (%%"REG_a", %1, 4), %%"REG_c"      \n\t"
//      0       1       2       3       4       5       6       7       8       9
//      %0      eax     eax+%1  eax+2%1 %0+4%1  ecx     ecx+%1  ecx+2%1 %0+8%1  ecx+4%1
        "movq (%%"REG_a", %1, 2), %%mm0         \n\t" // line 3
        "movq (%0, %1, 4), %%mm1                \n\t" // line 4
        "movq %%mm1, %%mm2                      \n\t" // line 4
        "psubusb %%mm0, %%mm1                   \n\t"
        "psubusb %%mm2, %%mm0                   \n\t"
        "por %%mm1, %%mm0                       \n\t" // |l2 - l3|
        "movq (%%"REG_c"), %%mm3                \n\t" // line 5
        "movq (%%"REG_c", %1), %%mm4            \n\t" // line 6
        "movq %%mm3, %%mm5                      \n\t" // line 5
        "psubusb %%mm4, %%mm3                   \n\t"
        "psubusb %%mm5, %%mm4                   \n\t"
        "por %%mm4, %%mm3                       \n\t" // |l5 - l6|
        PAVGB(%%mm3, %%mm0)                           // (|l2 - l3| + |l5 - l6|)/2
        "movq %%mm2, %%mm1                      \n\t" // line 4
        "psubusb %%mm5, %%mm2                   \n\t"
        "movq %%mm2, %%mm4                      \n\t"
        "pcmpeqb %%mm7, %%mm2                   \n\t" // (l4 - l5) <= 0 ? -1 : 0
        "psubusb %%mm1, %%mm5                   \n\t"
        "por %%mm5, %%mm4                       \n\t" // |l4 - l5|
        "psubusb %%mm0, %%mm4                   \n\t" //d = MAX(0, |l4-l5| - (|l2-l3| + |l5-l6|)/2)
        "movq %%mm4, %%mm3                      \n\t" // d
        "movq %2, %%mm0                         \n\t"
        "paddusb %%mm0, %%mm0                   \n\t"
        "psubusb %%mm0, %%mm4                   \n\t"
        "pcmpeqb %%mm7, %%mm4                   \n\t" // d <= QP ? -1 : 0
        "psubusb "MANGLE(b01)", %%mm3           \n\t"
        "pand %%mm4, %%mm3                      \n\t" // d <= QP ? d : 0

        PAVGB(%%mm7, %%mm3)                           // d/2
        "movq %%mm3, %%mm1                      \n\t" // d/2
        PAVGB(%%mm7, %%mm3)                           // d/4
        PAVGB(%%mm1, %%mm3)                           // 3*d/8

        "movq (%0, %1, 4), %%mm0                \n\t" // line 4
        "pxor %%mm2, %%mm0                      \n\t" //(l4 - l5) <= 0 ? -l4-1 : l4
        "psubusb %%mm3, %%mm0                   \n\t"
        "pxor %%mm2, %%mm0                      \n\t"
        "movq %%mm0, (%0, %1, 4)                \n\t" // line 4

        "movq (%%"REG_c"), %%mm0                \n\t" // line 5
        "pxor %%mm2, %%mm0                      \n\t" //(l4 - l5) <= 0 ? -l5-1 : l5
        "paddusb %%mm3, %%mm0                   \n\t"
        "pxor %%mm2, %%mm0                      \n\t"
        "movq %%mm0, (%%"REG_c")                \n\t" // line 5

        PAVGB(%%mm7, %%mm1)                           // d/4

        "movq (%%"REG_a", %1, 2), %%mm0         \n\t" // line 3
        "pxor %%mm2, %%mm0                      \n\t" //(l4 - l5) <= 0 ? -l4-1 : l4
        "psubusb %%mm1, %%mm0                   \n\t"
        "pxor %%mm2, %%mm0                      \n\t"
        "movq %%mm0, (%%"REG_a", %1, 2)         \n\t" // line 3

        "movq (%%"REG_c", %1), %%mm0            \n\t" // line 6
        "pxor %%mm2, %%mm0                      \n\t" //(l4 - l5) <= 0 ? -l5-1 : l5
        "paddusb %%mm1, %%mm0                   \n\t"
        "pxor %%mm2, %%mm0                      \n\t"
        "movq %%mm0, (%%"REG_c", %1)            \n\t" // line 6

        PAVGB(%%mm7, %%mm1)                           // d/8

        "movq (%%"REG_a", %1), %%mm0            \n\t" // line 2
        "pxor %%mm2, %%mm0                      \n\t" //(l4 - l5) <= 0 ? -l2-1 : l2
        "psubusb %%mm1, %%mm0                   \n\t"
        "pxor %%mm2, %%mm0                      \n\t"
        "movq %%mm0, (%%"REG_a", %1)            \n\t" // line 2

        "movq (%%"REG_c", %1, 2), %%mm0         \n\t" // line 7
        "pxor %%mm2, %%mm0                      \n\t" //(l4 - l5) <= 0 ? -l7-1 : l7
        "paddusb %%mm1, %%mm0                   \n\t"
        "pxor %%mm2, %%mm0                      \n\t"
        "movq %%mm0, (%%"REG_c", %1, 2)         \n\t" // line 7

        :
        : "r" (src), "r" ((x86_reg)stride), "m" (co->pQPb)
        : "%"REG_a, "%"REG_c
    );
#else //HAVE_MMX2 || HAVE_AMD3DNOW

    const int l1= stride;
    const int l2= stride + l1;
    const int l3= stride + l2;
    const int l4= stride + l3;
    const int l5= stride + l4;
    const int l6= stride + l5;
    const int l7= stride + l6;
//    const int l8= stride + l7;
//    const int l9= stride + l8;
    int x;

    src+= stride*3;
    for(x=0; x<BLOCK_SIZE; x++){
        int a= src[l3] - src[l4];
        int b= src[l4] - src[l5];
        int c= src[l5] - src[l6];

        int d= FFABS(b) - ((FFABS(a) + FFABS(c))>>1);
        d= FFMAX(d, 0);

        if(d < co->QP*2){
            int v = d * FFSIGN(-b);

            src[l2] +=v>>3;
            src[l3] +=v>>2;
            src[l4] +=(3*v)>>3;
            src[l5] -=(3*v)>>3;
            src[l6] -=v>>2;
            src[l7] -=v>>3;
        }
        src++;
    }
#endif //HAVE_MMX2 || HAVE_AMD3DNOW
}

#if !HAVE_ALTIVEC
static inline void RENAME(doVertDefFilter)(uint8_t src[], int stride, PPContext *c)
{
#if HAVE_MMX2 || HAVE_AMD3DNOW
/*
    uint8_t tmp[16];
    const int l1= stride;
    const int l2= stride + l1;
    const int l3= stride + l2;
    const int l4= (int)tmp - (int)src - stride*3;
    const int l5= (int)tmp - (int)src - stride*3 + 8;
    const int l6= stride*3 + l3;
    const int l7= stride + l6;
    const int l8= stride + l7;

    memcpy(tmp, src+stride*7, 8);
    memcpy(tmp+8, src+stride*8, 8);
*/
    src+= stride*4;
    __asm__ volatile(

#if 0 //slightly more accurate and slightly slower
        "pxor %%mm7, %%mm7                      \n\t" // 0
        "lea (%0, %1), %%"REG_a"                \n\t"
        "lea (%%"REG_a", %1, 4), %%"REG_c"      \n\t"
//      0       1       2       3       4       5       6       7
//      %0      %0+%1   %0+2%1  eax+2%1 %0+4%1  eax+4%1 ecx+%1  ecx+2%1
//      %0      eax     eax+%1  eax+2%1 %0+4%1  ecx     ecx+%1  ecx+2%1


        "movq (%0, %1, 2), %%mm0                \n\t" // l2
        "movq (%0), %%mm1                       \n\t" // l0
        "movq %%mm0, %%mm2                      \n\t" // l2
        PAVGB(%%mm7, %%mm0)                           // ~l2/2
        PAVGB(%%mm1, %%mm0)                           // ~(l2 + 2l0)/4
        PAVGB(%%mm2, %%mm0)                           // ~(5l2 + 2l0)/8

        "movq (%%"REG_a"), %%mm1                \n\t" // l1
        "movq (%%"REG_a", %1, 2), %%mm3         \n\t" // l3
        "movq %%mm1, %%mm4                      \n\t" // l1
        PAVGB(%%mm7, %%mm1)                           // ~l1/2
        PAVGB(%%mm3, %%mm1)                           // ~(l1 + 2l3)/4
        PAVGB(%%mm4, %%mm1)                           // ~(5l1 + 2l3)/8

        "movq %%mm0, %%mm4                      \n\t" // ~(5l2 + 2l0)/8
        "psubusb %%mm1, %%mm0                   \n\t"
        "psubusb %%mm4, %%mm1                   \n\t"
        "por %%mm0, %%mm1                       \n\t" // ~|2l0 - 5l1 + 5l2 - 2l3|/8
// mm1= |lenergy|, mm2= l2, mm3= l3, mm7=0

        "movq (%0, %1, 4), %%mm0                \n\t" // l4
        "movq %%mm0, %%mm4                      \n\t" // l4
        PAVGB(%%mm7, %%mm0)                           // ~l4/2
        PAVGB(%%mm2, %%mm0)                           // ~(l4 + 2l2)/4
        PAVGB(%%mm4, %%mm0)                           // ~(5l4 + 2l2)/8

        "movq (%%"REG_c"), %%mm2                \n\t" // l5
        "movq %%mm3, %%mm5                      \n\t" // l3
        PAVGB(%%mm7, %%mm3)                           // ~l3/2
        PAVGB(%%mm2, %%mm3)                           // ~(l3 + 2l5)/4
        PAVGB(%%mm5, %%mm3)                           // ~(5l3 + 2l5)/8

        "movq %%mm0, %%mm6                      \n\t" // ~(5l4 + 2l2)/8
        "psubusb %%mm3, %%mm0                   \n\t"
        "psubusb %%mm6, %%mm3                   \n\t"
        "por %%mm0, %%mm3                       \n\t" // ~|2l2 - 5l3 + 5l4 - 2l5|/8
        "pcmpeqb %%mm7, %%mm0                   \n\t" // SIGN(2l2 - 5l3 + 5l4 - 2l5)
// mm0= SIGN(menergy), mm1= |lenergy|, mm2= l5, mm3= |menergy|, mm4=l4, mm5= l3, mm7=0

        "movq (%%"REG_c", %1), %%mm6            \n\t" // l6
        "movq %%mm6, %%mm5                      \n\t" // l6
        PAVGB(%%mm7, %%mm6)                           // ~l6/2
        PAVGB(%%mm4, %%mm6)                           // ~(l6 + 2l4)/4
        PAVGB(%%mm5, %%mm6)                           // ~(5l6 + 2l4)/8

        "movq (%%"REG_c", %1, 2), %%mm5         \n\t" // l7
        "movq %%mm2, %%mm4                      \n\t" // l5
        PAVGB(%%mm7, %%mm2)                           // ~l5/2
        PAVGB(%%mm5, %%mm2)                           // ~(l5 + 2l7)/4
        PAVGB(%%mm4, %%mm2)                           // ~(5l5 + 2l7)/8

        "movq %%mm6, %%mm4                      \n\t" // ~(5l6 + 2l4)/8
        "psubusb %%mm2, %%mm6                   \n\t"
        "psubusb %%mm4, %%mm2                   \n\t"
        "por %%mm6, %%mm2                       \n\t" // ~|2l4 - 5l5 + 5l6 - 2l7|/8
// mm0= SIGN(menergy), mm1= |lenergy|/8, mm2= |renergy|/8, mm3= |menergy|/8, mm7=0


        PMINUB(%%mm2, %%mm1, %%mm4)                   // MIN(|lenergy|,|renergy|)/8
        "movq %2, %%mm4                         \n\t" // QP //FIXME QP+1 ?
        "paddusb "MANGLE(b01)", %%mm4           \n\t"
        "pcmpgtb %%mm3, %%mm4                   \n\t" // |menergy|/8 < QP
        "psubusb %%mm1, %%mm3                   \n\t" // d=|menergy|/8-MIN(|lenergy|,|renergy|)/8
        "pand %%mm4, %%mm3                      \n\t"

        "movq %%mm3, %%mm1                      \n\t"
//        "psubusb "MANGLE(b01)", %%mm3           \n\t"
        PAVGB(%%mm7, %%mm3)
        PAVGB(%%mm7, %%mm3)
        "paddusb %%mm1, %%mm3                   \n\t"
//        "paddusb "MANGLE(b01)", %%mm3           \n\t"

        "movq (%%"REG_a", %1, 2), %%mm6         \n\t" //l3
        "movq (%0, %1, 4), %%mm5                \n\t" //l4
        "movq (%0, %1, 4), %%mm4                \n\t" //l4
        "psubusb %%mm6, %%mm5                   \n\t"
        "psubusb %%mm4, %%mm6                   \n\t"
        "por %%mm6, %%mm5                       \n\t" // |l3-l4|
        "pcmpeqb %%mm7, %%mm6                   \n\t" // SIGN(l3-l4)
        "pxor %%mm6, %%mm0                      \n\t"
        "pand %%mm0, %%mm3                      \n\t"
        PMINUB(%%mm5, %%mm3, %%mm0)

        "psubusb "MANGLE(b01)", %%mm3           \n\t"
        PAVGB(%%mm7, %%mm3)

        "movq (%%"REG_a", %1, 2), %%mm0         \n\t"
        "movq (%0, %1, 4), %%mm2                \n\t"
        "pxor %%mm6, %%mm0                      \n\t"
        "pxor %%mm6, %%mm2                      \n\t"
        "psubb %%mm3, %%mm0                     \n\t"
        "paddb %%mm3, %%mm2                     \n\t"
        "pxor %%mm6, %%mm0                      \n\t"
        "pxor %%mm6, %%mm2                      \n\t"
        "movq %%mm0, (%%"REG_a", %1, 2)         \n\t"
        "movq %%mm2, (%0, %1, 4)                \n\t"
#endif //0

        "lea (%0, %1), %%"REG_a"                \n\t"
        "pcmpeqb %%mm6, %%mm6                   \n\t" // -1
//      0       1       2       3       4       5       6       7
//      %0      %0+%1   %0+2%1  eax+2%1 %0+4%1  eax+4%1 ecx+%1  ecx+2%1
//      %0      eax     eax+%1  eax+2%1 %0+4%1  ecx     ecx+%1  ecx+2%1


        "movq (%%"REG_a", %1, 2), %%mm1         \n\t" // l3
        "movq (%0, %1, 4), %%mm0                \n\t" // l4
        "pxor %%mm6, %%mm1                      \n\t" // -l3-1
        PAVGB(%%mm1, %%mm0)                           // -q+128 = (l4-l3+256)/2
// mm1=-l3-1, mm0=128-q

        "movq (%%"REG_a", %1, 4), %%mm2         \n\t" // l5
        "movq (%%"REG_a", %1), %%mm3            \n\t" // l2
        "pxor %%mm6, %%mm2                      \n\t" // -l5-1
        "movq %%mm2, %%mm5                      \n\t" // -l5-1
        "movq "MANGLE(b80)", %%mm4              \n\t" // 128
        "lea (%%"REG_a", %1, 4), %%"REG_c"      \n\t"
        PAVGB(%%mm3, %%mm2)                           // (l2-l5+256)/2
        PAVGB(%%mm0, %%mm4)                           // ~(l4-l3)/4 + 128
        PAVGB(%%mm2, %%mm4)                           // ~(l2-l5)/4 +(l4-l3)/8 + 128
        PAVGB(%%mm0, %%mm4)                           // ~(l2-l5)/8 +5(l4-l3)/16 + 128
// mm1=-l3-1, mm0=128-q, mm3=l2, mm4=menergy/16 + 128, mm5= -l5-1

        "movq (%%"REG_a"), %%mm2                \n\t" // l1
        "pxor %%mm6, %%mm2                      \n\t" // -l1-1
        PAVGB(%%mm3, %%mm2)                           // (l2-l1+256)/2
        PAVGB((%0), %%mm1)                            // (l0-l3+256)/2
        "movq "MANGLE(b80)", %%mm3              \n\t" // 128
        PAVGB(%%mm2, %%mm3)                           // ~(l2-l1)/4 + 128
        PAVGB(%%mm1, %%mm3)                           // ~(l0-l3)/4 +(l2-l1)/8 + 128
        PAVGB(%%mm2, %%mm3)                           // ~(l0-l3)/8 +5(l2-l1)/16 + 128
// mm0=128-q, mm3=lenergy/16 + 128, mm4= menergy/16 + 128, mm5= -l5-1

        PAVGB((%%REGc, %1), %%mm5)                    // (l6-l5+256)/2
        "movq (%%"REG_c", %1, 2), %%mm1         \n\t" // l7
        "pxor %%mm6, %%mm1                      \n\t" // -l7-1
        PAVGB((%0, %1, 4), %%mm1)                     // (l4-l7+256)/2
        "movq "MANGLE(b80)", %%mm2              \n\t" // 128
        PAVGB(%%mm5, %%mm2)                           // ~(l6-l5)/4 + 128
        PAVGB(%%mm1, %%mm2)                           // ~(l4-l7)/4 +(l6-l5)/8 + 128
        PAVGB(%%mm5, %%mm2)                           // ~(l4-l7)/8 +5(l6-l5)/16 + 128
// mm0=128-q, mm2=renergy/16 + 128, mm3=lenergy/16 + 128, mm4= menergy/16 + 128

        "movq "MANGLE(b00)", %%mm1              \n\t" // 0
        "movq "MANGLE(b00)", %%mm5              \n\t" // 0
        "psubb %%mm2, %%mm1                     \n\t" // 128 - renergy/16
        "psubb %%mm3, %%mm5                     \n\t" // 128 - lenergy/16
        PMAXUB(%%mm1, %%mm2)                          // 128 + |renergy/16|
        PMAXUB(%%mm5, %%mm3)                          // 128 + |lenergy/16|
        PMINUB(%%mm2, %%mm3, %%mm1)                   // 128 + MIN(|lenergy|,|renergy|)/16

// mm0=128-q, mm3=128 + MIN(|lenergy|,|renergy|)/16, mm4= menergy/16 + 128

        "movq "MANGLE(b00)", %%mm7              \n\t" // 0
        "movq %2, %%mm2                         \n\t" // QP
        PAVGB(%%mm6, %%mm2)                           // 128 + QP/2
        "psubb %%mm6, %%mm2                     \n\t"

        "movq %%mm4, %%mm1                      \n\t"
        "pcmpgtb %%mm7, %%mm1                   \n\t" // SIGN(menergy)
        "pxor %%mm1, %%mm4                      \n\t"
        "psubb %%mm1, %%mm4                     \n\t" // 128 + |menergy|/16
        "pcmpgtb %%mm4, %%mm2                   \n\t" // |menergy|/16 < QP/2
        "psubusb %%mm3, %%mm4                   \n\t" //d=|menergy|/16 - MIN(|lenergy|,|renergy|)/16
// mm0=128-q, mm1= SIGN(menergy), mm2= |menergy|/16 < QP/2, mm4= d/16

        "movq %%mm4, %%mm3                      \n\t" // d
        "psubusb "MANGLE(b01)", %%mm4           \n\t"
        PAVGB(%%mm7, %%mm4)                           // d/32
        PAVGB(%%mm7, %%mm4)                           // (d + 32)/64
        "paddb %%mm3, %%mm4                     \n\t" // 5d/64
        "pand %%mm2, %%mm4                      \n\t"

        "movq "MANGLE(b80)", %%mm5              \n\t" // 128
        "psubb %%mm0, %%mm5                     \n\t" // q
        "paddsb %%mm6, %%mm5                    \n\t" // fix bad rounding
        "pcmpgtb %%mm5, %%mm7                   \n\t" // SIGN(q)
        "pxor %%mm7, %%mm5                      \n\t"

        PMINUB(%%mm5, %%mm4, %%mm3)                   // MIN(|q|, 5d/64)
        "pxor %%mm1, %%mm7                      \n\t" // SIGN(d*q)

        "pand %%mm7, %%mm4                      \n\t"
        "movq (%%"REG_a", %1, 2), %%mm0         \n\t"
        "movq (%0, %1, 4), %%mm2                \n\t"
        "pxor %%mm1, %%mm0                      \n\t"
        "pxor %%mm1, %%mm2                      \n\t"
        "paddb %%mm4, %%mm0                     \n\t"
        "psubb %%mm4, %%mm2                     \n\t"
        "pxor %%mm1, %%mm0                      \n\t"
        "pxor %%mm1, %%mm2                      \n\t"
        "movq %%mm0, (%%"REG_a", %1, 2)         \n\t"
        "movq %%mm2, (%0, %1, 4)                \n\t"

        :
        : "r" (src), "r" ((x86_reg)stride), "m" (c->pQPb)
        : "%"REG_a, "%"REG_c
    );

/*
    {
    int x;
    src-= stride;
    for(x=0; x<BLOCK_SIZE; x++){
        const int middleEnergy= 5*(src[l5] - src[l4]) + 2*(src[l3] - src[l6]);
        if(FFABS(middleEnergy)< 8*QP){
            const int q=(src[l4] - src[l5])/2;
            const int leftEnergy=  5*(src[l3] - src[l2]) + 2*(src[l1] - src[l4]);
            const int rightEnergy= 5*(src[l7] - src[l6]) + 2*(src[l5] - src[l8]);

            int d= FFABS(middleEnergy) - FFMIN( FFABS(leftEnergy), FFABS(rightEnergy) );
            d= FFMAX(d, 0);

            d= (5*d + 32) >> 6;
            d*= FFSIGN(-middleEnergy);

            if(q>0){
                d= d<0 ? 0 : d;
                d= d>q ? q : d;
            }else{
                d= d>0 ? 0 : d;
                d= d<q ? q : d;
            }

            src[l4]-= d;
            src[l5]+= d;
        }
        src++;
    }
    src-=8;
    for(x=0; x<8; x++){
        int y;
        for(y=4; y<6; y++){
            int d= src[x+y*stride] - tmp[x+(y-4)*8];
            int ad= FFABS(d);
            static int max=0;
            static int sum=0;
            static int num=0;
            static int bias=0;

            if(max<ad) max=ad;
            sum+= ad>3 ? 1 : 0;
            if(ad>3){
                src[0] = src[7] = src[stride*7] = src[(stride+1)*7]=255;
            }
            if(y==4) bias+=d;
            num++;
            if(num%1000000 == 0){
                av_log(c, AV_LOG_INFO, " %d %d %d %d\n", num, sum, max, bias);
            }
        }
    }
}
*/
#elif HAVE_MMX
    src+= stride*4;
    __asm__ volatile(
        "pxor %%mm7, %%mm7                      \n\t"
        "lea -40(%%"REG_SP"), %%"REG_c"         \n\t" // make space for 4 8-byte vars
        "and "ALIGN_MASK", %%"REG_c"            \n\t" // align
//      0       1       2       3       4       5       6       7
//      %0      %0+%1   %0+2%1  eax+2%1 %0+4%1  eax+4%1 edx+%1  edx+2%1
//      %0      eax     eax+%1  eax+2%1 %0+4%1  edx     edx+%1  edx+2%1

        "movq (%0), %%mm0                       \n\t"
        "movq %%mm0, %%mm1                      \n\t"
        "punpcklbw %%mm7, %%mm0                 \n\t" // low part of line 0
        "punpckhbw %%mm7, %%mm1                 \n\t" // high part of line 0

        "movq (%0, %1), %%mm2                   \n\t"
        "lea (%0, %1, 2), %%"REG_a"             \n\t"
        "movq %%mm2, %%mm3                      \n\t"
        "punpcklbw %%mm7, %%mm2                 \n\t" // low part of line 1
        "punpckhbw %%mm7, %%mm3                 \n\t" // high part of line 1

        "movq (%%"REG_a"), %%mm4                \n\t"
        "movq %%mm4, %%mm5                      \n\t"
        "punpcklbw %%mm7, %%mm4                 \n\t" // low part of line 2
        "punpckhbw %%mm7, %%mm5                 \n\t" // high part of line 2

        "paddw %%mm0, %%mm0                     \n\t" // 2L0
        "paddw %%mm1, %%mm1                     \n\t" // 2H0
        "psubw %%mm4, %%mm2                     \n\t" // L1 - L2
        "psubw %%mm5, %%mm3                     \n\t" // H1 - H2
        "psubw %%mm2, %%mm0                     \n\t" // 2L0 - L1 + L2
        "psubw %%mm3, %%mm1                     \n\t" // 2H0 - H1 + H2

        "psllw $2, %%mm2                        \n\t" // 4L1 - 4L2
        "psllw $2, %%mm3                        \n\t" // 4H1 - 4H2
        "psubw %%mm2, %%mm0                     \n\t" // 2L0 - 5L1 + 5L2
        "psubw %%mm3, %%mm1                     \n\t" // 2H0 - 5H1 + 5H2

        "movq (%%"REG_a", %1), %%mm2            \n\t"
        "movq %%mm2, %%mm3                      \n\t"
        "punpcklbw %%mm7, %%mm2                 \n\t" // L3
        "punpckhbw %%mm7, %%mm3                 \n\t" // H3

        "psubw %%mm2, %%mm0                     \n\t" // 2L0 - 5L1 + 5L2 - L3
        "psubw %%mm3, %%mm1                     \n\t" // 2H0 - 5H1 + 5H2 - H3
        "psubw %%mm2, %%mm0                     \n\t" // 2L0 - 5L1 + 5L2 - 2L3
        "psubw %%mm3, %%mm1                     \n\t" // 2H0 - 5H1 + 5H2 - 2H3
        "movq %%mm0, (%%"REG_c")                \n\t" // 2L0 - 5L1 + 5L2 - 2L3
        "movq %%mm1, 8(%%"REG_c")               \n\t" // 2H0 - 5H1 + 5H2 - 2H3

        "movq (%%"REG_a", %1, 2), %%mm0         \n\t"
        "movq %%mm0, %%mm1                      \n\t"
        "punpcklbw %%mm7, %%mm0                 \n\t" // L4
        "punpckhbw %%mm7, %%mm1                 \n\t" // H4

        "psubw %%mm0, %%mm2                     \n\t" // L3 - L4
        "psubw %%mm1, %%mm3                     \n\t" // H3 - H4
        "movq %%mm2, 16(%%"REG_c")              \n\t" // L3 - L4
        "movq %%mm3, 24(%%"REG_c")              \n\t" // H3 - H4
        "paddw %%mm4, %%mm4                     \n\t" // 2L2
        "paddw %%mm5, %%mm5                     \n\t" // 2H2
        "psubw %%mm2, %%mm4                     \n\t" // 2L2 - L3 + L4
        "psubw %%mm3, %%mm5                     \n\t" // 2H2 - H3 + H4

        "lea (%%"REG_a", %1), %0                \n\t"
        "psllw $2, %%mm2                        \n\t" // 4L3 - 4L4
        "psllw $2, %%mm3                        \n\t" // 4H3 - 4H4
        "psubw %%mm2, %%mm4                     \n\t" // 2L2 - 5L3 + 5L4
        "psubw %%mm3, %%mm5                     \n\t" // 2H2 - 5H3 + 5H4
//50 opcodes so far
        "movq (%0, %1, 2), %%mm2                \n\t"
        "movq %%mm2, %%mm3                      \n\t"
        "punpcklbw %%mm7, %%mm2                 \n\t" // L5
        "punpckhbw %%mm7, %%mm3                 \n\t" // H5
        "psubw %%mm2, %%mm4                     \n\t" // 2L2 - 5L3 + 5L4 - L5
        "psubw %%mm3, %%mm5                     \n\t" // 2H2 - 5H3 + 5H4 - H5
        "psubw %%mm2, %%mm4                     \n\t" // 2L2 - 5L3 + 5L4 - 2L5
        "psubw %%mm3, %%mm5                     \n\t" // 2H2 - 5H3 + 5H4 - 2H5

        "movq (%%"REG_a", %1, 4), %%mm6         \n\t"
        "punpcklbw %%mm7, %%mm6                 \n\t" // L6
        "psubw %%mm6, %%mm2                     \n\t" // L5 - L6
        "movq (%%"REG_a", %1, 4), %%mm6         \n\t"
        "punpckhbw %%mm7, %%mm6                 \n\t" // H6
        "psubw %%mm6, %%mm3                     \n\t" // H5 - H6

        "paddw %%mm0, %%mm0                     \n\t" // 2L4
        "paddw %%mm1, %%mm1                     \n\t" // 2H4
        "psubw %%mm2, %%mm0                     \n\t" // 2L4 - L5 + L6
        "psubw %%mm3, %%mm1                     \n\t" // 2H4 - H5 + H6

        "psllw $2, %%mm2                        \n\t" // 4L5 - 4L6
        "psllw $2, %%mm3                        \n\t" // 4H5 - 4H6
        "psubw %%mm2, %%mm0                     \n\t" // 2L4 - 5L5 + 5L6
        "psubw %%mm3, %%mm1                     \n\t" // 2H4 - 5H5 + 5H6

        "movq (%0, %1, 4), %%mm2                \n\t"
        "movq %%mm2, %%mm3                      \n\t"
        "punpcklbw %%mm7, %%mm2                 \n\t" // L7
        "punpckhbw %%mm7, %%mm3                 \n\t" // H7

        "paddw %%mm2, %%mm2                     \n\t" // 2L7
        "paddw %%mm3, %%mm3                     \n\t" // 2H7
        "psubw %%mm2, %%mm0                     \n\t" // 2L4 - 5L5 + 5L6 - 2L7
        "psubw %%mm3, %%mm1                     \n\t" // 2H4 - 5H5 + 5H6 - 2H7

        "movq (%%"REG_c"), %%mm2                \n\t" // 2L0 - 5L1 + 5L2 - 2L3
        "movq 8(%%"REG_c"), %%mm3               \n\t" // 2H0 - 5H1 + 5H2 - 2H3

#if HAVE_MMX2
        "movq %%mm7, %%mm6                      \n\t" // 0
        "psubw %%mm0, %%mm6                     \n\t"
        "pmaxsw %%mm6, %%mm0                    \n\t" // |2L4 - 5L5 + 5L6 - 2L7|
        "movq %%mm7, %%mm6                      \n\t" // 0
        "psubw %%mm1, %%mm6                     \n\t"
        "pmaxsw %%mm6, %%mm1                    \n\t" // |2H4 - 5H5 + 5H6 - 2H7|
        "movq %%mm7, %%mm6                      \n\t" // 0
        "psubw %%mm2, %%mm6                     \n\t"
        "pmaxsw %%mm6, %%mm2                    \n\t" // |2L0 - 5L1 + 5L2 - 2L3|
        "movq %%mm7, %%mm6                      \n\t" // 0
        "psubw %%mm3, %%mm6                     \n\t"
        "pmaxsw %%mm6, %%mm3                    \n\t" // |2H0 - 5H1 + 5H2 - 2H3|
#else
        "movq %%mm7, %%mm6                      \n\t" // 0
        "pcmpgtw %%mm0, %%mm6                   \n\t"
        "pxor %%mm6, %%mm0                      \n\t"
        "psubw %%mm6, %%mm0                     \n\t" // |2L4 - 5L5 + 5L6 - 2L7|
        "movq %%mm7, %%mm6                      \n\t" // 0
        "pcmpgtw %%mm1, %%mm6                   \n\t"
        "pxor %%mm6, %%mm1                      \n\t"
        "psubw %%mm6, %%mm1                     \n\t" // |2H4 - 5H5 + 5H6 - 2H7|
        "movq %%mm7, %%mm6                      \n\t" // 0
        "pcmpgtw %%mm2, %%mm6                   \n\t"
        "pxor %%mm6, %%mm2                      \n\t"
        "psubw %%mm6, %%mm2                     \n\t" // |2L0 - 5L1 + 5L2 - 2L3|
        "movq %%mm7, %%mm6                      \n\t" // 0
        "pcmpgtw %%mm3, %%mm6                   \n\t"
        "pxor %%mm6, %%mm3                      \n\t"
        "psubw %%mm6, %%mm3                     \n\t" // |2H0 - 5H1 + 5H2 - 2H3|
#endif

#if HAVE_MMX2
        "pminsw %%mm2, %%mm0                    \n\t"
        "pminsw %%mm3, %%mm1                    \n\t"
#else
        "movq %%mm0, %%mm6                      \n\t"
        "psubusw %%mm2, %%mm6                   \n\t"
        "psubw %%mm6, %%mm0                     \n\t"
        "movq %%mm1, %%mm6                      \n\t"
        "psubusw %%mm3, %%mm6                   \n\t"
        "psubw %%mm6, %%mm1                     \n\t"
#endif

        "movd %2, %%mm2                         \n\t" // QP
        "punpcklbw %%mm7, %%mm2                 \n\t"

        "movq %%mm7, %%mm6                      \n\t" // 0
        "pcmpgtw %%mm4, %%mm6                   \n\t" // sign(2L2 - 5L3 + 5L4 - 2L5)
        "pxor %%mm6, %%mm4                      \n\t"
        "psubw %%mm6, %%mm4                     \n\t" // |2L2 - 5L3 + 5L4 - 2L5|
        "pcmpgtw %%mm5, %%mm7                   \n\t" // sign(2H2 - 5H3 + 5H4 - 2H5)
        "pxor %%mm7, %%mm5                      \n\t"
        "psubw %%mm7, %%mm5                     \n\t" // |2H2 - 5H3 + 5H4 - 2H5|
// 100 opcodes
        "psllw $3, %%mm2                        \n\t" // 8QP
        "movq %%mm2, %%mm3                      \n\t" // 8QP
        "pcmpgtw %%mm4, %%mm2                   \n\t"
        "pcmpgtw %%mm5, %%mm3                   \n\t"
        "pand %%mm2, %%mm4                      \n\t"
        "pand %%mm3, %%mm5                      \n\t"


        "psubusw %%mm0, %%mm4                   \n\t" // hd
        "psubusw %%mm1, %%mm5                   \n\t" // ld


        "movq "MANGLE(w05)", %%mm2              \n\t" // 5
        "pmullw %%mm2, %%mm4                    \n\t"
        "pmullw %%mm2, %%mm5                    \n\t"
        "movq "MANGLE(w20)", %%mm2              \n\t" // 32
        "paddw %%mm2, %%mm4                     \n\t"
        "paddw %%mm2, %%mm5                     \n\t"
        "psrlw $6, %%mm4                        \n\t"
        "psrlw $6, %%mm5                        \n\t"

        "movq 16(%%"REG_c"), %%mm0              \n\t" // L3 - L4
        "movq 24(%%"REG_c"), %%mm1              \n\t" // H3 - H4

        "pxor %%mm2, %%mm2                      \n\t"
        "pxor %%mm3, %%mm3                      \n\t"

        "pcmpgtw %%mm0, %%mm2                   \n\t" // sign (L3-L4)
        "pcmpgtw %%mm1, %%mm3                   \n\t" // sign (H3-H4)
        "pxor %%mm2, %%mm0                      \n\t"
        "pxor %%mm3, %%mm1                      \n\t"
        "psubw %%mm2, %%mm0                     \n\t" // |L3-L4|
        "psubw %%mm3, %%mm1                     \n\t" // |H3-H4|
        "psrlw $1, %%mm0                        \n\t" // |L3 - L4|/2
        "psrlw $1, %%mm1                        \n\t" // |H3 - H4|/2

        "pxor %%mm6, %%mm2                      \n\t"
        "pxor %%mm7, %%mm3                      \n\t"
        "pand %%mm2, %%mm4                      \n\t"
        "pand %%mm3, %%mm5                      \n\t"

#if HAVE_MMX2
        "pminsw %%mm0, %%mm4                    \n\t"
        "pminsw %%mm1, %%mm5                    \n\t"
#else
        "movq %%mm4, %%mm2                      \n\t"
        "psubusw %%mm0, %%mm2                   \n\t"
        "psubw %%mm2, %%mm4                     \n\t"
        "movq %%mm5, %%mm2                      \n\t"
        "psubusw %%mm1, %%mm2                   \n\t"
        "psubw %%mm2, %%mm5                     \n\t"
#endif
        "pxor %%mm6, %%mm4                      \n\t"
        "pxor %%mm7, %%mm5                      \n\t"
        "psubw %%mm6, %%mm4                     \n\t"
        "psubw %%mm7, %%mm5                     \n\t"
        "packsswb %%mm5, %%mm4                  \n\t"
        "movq (%0), %%mm0                       \n\t"
        "paddb   %%mm4, %%mm0                   \n\t"
        "movq %%mm0, (%0)                       \n\t"
        "movq (%0, %1), %%mm0                   \n\t"
        "psubb %%mm4, %%mm0                     \n\t"
        "movq %%mm0, (%0, %1)                   \n\t"

        : "+r" (src)
        : "r" ((x86_reg)stride), "m" (c->pQPb)
        : "%"REG_a, "%"REG_c
    );
#else //HAVE_MMX2 || HAVE_AMD3DNOW
    const int l1= stride;
    const int l2= stride + l1;
    const int l3= stride + l2;
    const int l4= stride + l3;
    const int l5= stride + l4;
    const int l6= stride + l5;
    const int l7= stride + l6;
    const int l8= stride + l7;
//    const int l9= stride + l8;
    int x;
    src+= stride*3;
    for(x=0; x<BLOCK_SIZE; x++){
        const int middleEnergy= 5*(src[l5] - src[l4]) + 2*(src[l3] - src[l6]);
        if(FFABS(middleEnergy) < 8*c->QP){
            const int q=(src[l4] - src[l5])/2;
            const int leftEnergy=  5*(src[l3] - src[l2]) + 2*(src[l1] - src[l4]);
            const int rightEnergy= 5*(src[l7] - src[l6]) + 2*(src[l5] - src[l8]);

            int d= FFABS(middleEnergy) - FFMIN( FFABS(leftEnergy), FFABS(rightEnergy) );
            d= FFMAX(d, 0);

            d= (5*d + 32) >> 6;
            d*= FFSIGN(-middleEnergy);

            if(q>0){
                d= d<0 ? 0 : d;
                d= d>q ? q : d;
            }else{
                d= d>0 ? 0 : d;
                d= d<q ? q : d;
            }

            src[l4]-= d;
            src[l5]+= d;
        }
        src++;
    }
#endif //HAVE_MMX2 || HAVE_AMD3DNOW
}
#endif //HAVE_ALTIVEC

#if !HAVE_ALTIVEC
static inline void RENAME(dering)(uint8_t src[], int stride, PPContext *c)
{
#if HAVE_MMX2 || HAVE_AMD3DNOW
    __asm__ volatile(
        "pxor %%mm6, %%mm6                      \n\t"
        "pcmpeqb %%mm7, %%mm7                   \n\t"
        "movq %2, %%mm0                         \n\t"
        "punpcklbw %%mm6, %%mm0                 \n\t"
        "psrlw $1, %%mm0                        \n\t"
        "psubw %%mm7, %%mm0                     \n\t"
        "packuswb %%mm0, %%mm0                  \n\t"
        "movq %%mm0, %3                         \n\t"

        "lea (%0, %1), %%"REG_a"                \n\t"
        "lea (%%"REG_a", %1, 4), %%"REG_d"      \n\t"

//        0        1        2        3        4        5        6        7        8        9
//        %0        eax        eax+%1        eax+2%1        %0+4%1        edx        edx+%1        edx+2%1        %0+8%1        edx+4%1

#undef REAL_FIND_MIN_MAX
#undef FIND_MIN_MAX
#if HAVE_MMX2
#define REAL_FIND_MIN_MAX(addr)\
        "movq " #addr ", %%mm0                  \n\t"\
        "pminub %%mm0, %%mm7                    \n\t"\
        "pmaxub %%mm0, %%mm6                    \n\t"
#else
#define REAL_FIND_MIN_MAX(addr)\
        "movq " #addr ", %%mm0                  \n\t"\
        "movq %%mm7, %%mm1                      \n\t"\
        "psubusb %%mm0, %%mm6                   \n\t"\
        "paddb %%mm0, %%mm6                     \n\t"\
        "psubusb %%mm0, %%mm1                   \n\t"\
        "psubb %%mm1, %%mm7                     \n\t"
#endif
#define FIND_MIN_MAX(addr)  REAL_FIND_MIN_MAX(addr)

FIND_MIN_MAX((%%REGa))
FIND_MIN_MAX((%%REGa, %1))
FIND_MIN_MAX((%%REGa, %1, 2))
FIND_MIN_MAX((%0, %1, 4))
FIND_MIN_MAX((%%REGd))
FIND_MIN_MAX((%%REGd, %1))
FIND_MIN_MAX((%%REGd, %1, 2))
FIND_MIN_MAX((%0, %1, 8))

        "movq %%mm7, %%mm4                      \n\t"
        "psrlq $8, %%mm7                        \n\t"
#if HAVE_MMX2
        "pminub %%mm4, %%mm7                    \n\t" // min of pixels
        "pshufw $0xF9, %%mm7, %%mm4             \n\t"
        "pminub %%mm4, %%mm7                    \n\t" // min of pixels
        "pshufw $0xFE, %%mm7, %%mm4             \n\t"
        "pminub %%mm4, %%mm7                    \n\t"
#else
        "movq %%mm7, %%mm1                      \n\t"
        "psubusb %%mm4, %%mm1                   \n\t"
        "psubb %%mm1, %%mm7                     \n\t"
        "movq %%mm7, %%mm4                      \n\t"
        "psrlq $16, %%mm7                       \n\t"
        "movq %%mm7, %%mm1                      \n\t"
        "psubusb %%mm4, %%mm1                   \n\t"
        "psubb %%mm1, %%mm7                     \n\t"
        "movq %%mm7, %%mm4                      \n\t"
        "psrlq $32, %%mm7                       \n\t"
        "movq %%mm7, %%mm1                      \n\t"
        "psubusb %%mm4, %%mm1                   \n\t"
        "psubb %%mm1, %%mm7                     \n\t"
#endif


        "movq %%mm6, %%mm4                      \n\t"
        "psrlq $8, %%mm6                        \n\t"
#if HAVE_MMX2
        "pmaxub %%mm4, %%mm6                    \n\t" // max of pixels
        "pshufw $0xF9, %%mm6, %%mm4             \n\t"
        "pmaxub %%mm4, %%mm6                    \n\t"
        "pshufw $0xFE, %%mm6, %%mm4             \n\t"
        "pmaxub %%mm4, %%mm6                    \n\t"
#else
        "psubusb %%mm4, %%mm6                   \n\t"
        "paddb %%mm4, %%mm6                     \n\t"
        "movq %%mm6, %%mm4                      \n\t"
        "psrlq $16, %%mm6                       \n\t"
        "psubusb %%mm4, %%mm6                   \n\t"
        "paddb %%mm4, %%mm6                     \n\t"
        "movq %%mm6, %%mm4                      \n\t"
        "psrlq $32, %%mm6                       \n\t"
        "psubusb %%mm4, %%mm6                   \n\t"
        "paddb %%mm4, %%mm6                     \n\t"
#endif
        "movq %%mm6, %%mm0                      \n\t" // max
        "psubb %%mm7, %%mm6                     \n\t" // max - min
        "movd %%mm6, %%ecx                      \n\t"
        "cmpb "MANGLE(deringThreshold)", %%cl   \n\t"
        " jb 1f                                 \n\t"
        "lea -24(%%"REG_SP"), %%"REG_c"         \n\t"
        "and "ALIGN_MASK", %%"REG_c"            \n\t"
        PAVGB(%%mm0, %%mm7)                           // a=(max + min)/2
        "punpcklbw %%mm7, %%mm7                 \n\t"
        "punpcklbw %%mm7, %%mm7                 \n\t"
        "punpcklbw %%mm7, %%mm7                 \n\t"
        "movq %%mm7, (%%"REG_c")                \n\t"

        "movq (%0), %%mm0                       \n\t" // L10
        "movq %%mm0, %%mm1                      \n\t" // L10
        "movq %%mm0, %%mm2                      \n\t" // L10
        "psllq $8, %%mm1                        \n\t"
        "psrlq $8, %%mm2                        \n\t"
        "movd -4(%0), %%mm3                     \n\t"
        "movd 8(%0), %%mm4                      \n\t"
        "psrlq $24, %%mm3                       \n\t"
        "psllq $56, %%mm4                       \n\t"
        "por %%mm3, %%mm1                       \n\t" // L00
        "por %%mm4, %%mm2                       \n\t" // L20
        "movq %%mm1, %%mm3                      \n\t" // L00
        PAVGB(%%mm2, %%mm1)                           // (L20 + L00)/2
        PAVGB(%%mm0, %%mm1)                           // (L20 + L00 + 2L10)/4
        "psubusb %%mm7, %%mm0                   \n\t"
        "psubusb %%mm7, %%mm2                   \n\t"
        "psubusb %%mm7, %%mm3                   \n\t"
        "pcmpeqb "MANGLE(b00)", %%mm0           \n\t" // L10 > a ? 0 : -1
        "pcmpeqb "MANGLE(b00)", %%mm2           \n\t" // L20 > a ? 0 : -1
        "pcmpeqb "MANGLE(b00)", %%mm3           \n\t" // L00 > a ? 0 : -1
        "paddb %%mm2, %%mm0                     \n\t"
        "paddb %%mm3, %%mm0                     \n\t"

        "movq (%%"REG_a"), %%mm2                \n\t" // L11
        "movq %%mm2, %%mm3                      \n\t" // L11
        "movq %%mm2, %%mm4                      \n\t" // L11
        "psllq $8, %%mm3                        \n\t"
        "psrlq $8, %%mm4                        \n\t"
        "movd -4(%%"REG_a"), %%mm5              \n\t"
        "movd 8(%%"REG_a"), %%mm6               \n\t"
        "psrlq $24, %%mm5                       \n\t"
        "psllq $56, %%mm6                       \n\t"
        "por %%mm5, %%mm3                       \n\t" // L01
        "por %%mm6, %%mm4                       \n\t" // L21
        "movq %%mm3, %%mm5                      \n\t" // L01
        PAVGB(%%mm4, %%mm3)                           // (L21 + L01)/2
        PAVGB(%%mm2, %%mm3)                           // (L21 + L01 + 2L11)/4
        "psubusb %%mm7, %%mm2                   \n\t"
        "psubusb %%mm7, %%mm4                   \n\t"
        "psubusb %%mm7, %%mm5                   \n\t"
        "pcmpeqb "MANGLE(b00)", %%mm2           \n\t" // L11 > a ? 0 : -1
        "pcmpeqb "MANGLE(b00)", %%mm4           \n\t" // L21 > a ? 0 : -1
        "pcmpeqb "MANGLE(b00)", %%mm5           \n\t" // L01 > a ? 0 : -1
        "paddb %%mm4, %%mm2                     \n\t"
        "paddb %%mm5, %%mm2                     \n\t"
// 0, 2, 3, 1
#define REAL_DERING_CORE(dst,src,ppsx,psx,sx,pplx,plx,lx,t0,t1) \
        "movq " #src ", " #sx "                 \n\t" /* src[0] */\
        "movq " #sx ", " #lx "                  \n\t" /* src[0] */\
        "movq " #sx ", " #t0 "                  \n\t" /* src[0] */\
        "psllq $8, " #lx "                      \n\t"\
        "psrlq $8, " #t0 "                      \n\t"\
        "movd -4" #src ", " #t1 "               \n\t"\
        "psrlq $24, " #t1 "                     \n\t"\
        "por " #t1 ", " #lx "                   \n\t" /* src[-1] */\
        "movd 8" #src ", " #t1 "                \n\t"\
        "psllq $56, " #t1 "                     \n\t"\
        "por " #t1 ", " #t0 "                   \n\t" /* src[+1] */\
        "movq " #lx ", " #t1 "                  \n\t" /* src[-1] */\
        PAVGB(t0, lx)                                 /* (src[-1] + src[+1])/2 */\
        PAVGB(sx, lx)                                 /* (src[-1] + 2src[0] + src[+1])/4 */\
        PAVGB(lx, pplx)                                     \
        "movq " #lx ", 8(%%"REG_c")             \n\t"\
        "movq (%%"REG_c"), " #lx "              \n\t"\
        "psubusb " #lx ", " #t1 "               \n\t"\
        "psubusb " #lx ", " #t0 "               \n\t"\
        "psubusb " #lx ", " #sx "               \n\t"\
        "movq "MANGLE(b00)", " #lx "            \n\t"\
        "pcmpeqb " #lx ", " #t1 "               \n\t" /* src[-1] > a ? 0 : -1*/\
        "pcmpeqb " #lx ", " #t0 "               \n\t" /* src[+1] > a ? 0 : -1*/\
        "pcmpeqb " #lx ", " #sx "               \n\t" /* src[0]  > a ? 0 : -1*/\
        "paddb " #t1 ", " #t0 "                 \n\t"\
        "paddb " #t0 ", " #sx "                 \n\t"\
\
        PAVGB(plx, pplx)                              /* filtered */\
        "movq " #dst ", " #t0 "                 \n\t" /* dst */\
        "movq " #t0 ", " #t1 "                  \n\t" /* dst */\
        "psubusb %3, " #t0 "                    \n\t"\
        "paddusb %3, " #t1 "                    \n\t"\
        PMAXUB(t0, pplx)\
        PMINUB(t1, pplx, t0)\
        "paddb " #sx ", " #ppsx "               \n\t"\
        "paddb " #psx ", " #ppsx "              \n\t"\
        "#paddb "MANGLE(b02)", " #ppsx "        \n\t"\
        "pand "MANGLE(b08)", " #ppsx "          \n\t"\
        "pcmpeqb " #lx ", " #ppsx "             \n\t"\
        "pand " #ppsx ", " #pplx "              \n\t"\
        "pandn " #dst ", " #ppsx "              \n\t"\
        "por " #pplx ", " #ppsx "               \n\t"\
        "movq " #ppsx ", " #dst "               \n\t"\
        "movq 8(%%"REG_c"), " #lx "             \n\t"

#define DERING_CORE(dst,src,ppsx,psx,sx,pplx,plx,lx,t0,t1) \
   REAL_DERING_CORE(dst,src,ppsx,psx,sx,pplx,plx,lx,t0,t1)
/*
0000000
1111111

1111110
1111101
1111100
1111011
1111010
1111001

1111000
1110111

*/
//DERING_CORE(dst          ,src            ,ppsx ,psx  ,sx   ,pplx ,plx  ,lx   ,t0   ,t1)
DERING_CORE((%%REGa)       ,(%%REGa, %1)   ,%%mm0,%%mm2,%%mm4,%%mm1,%%mm3,%%mm5,%%mm6,%%mm7)
DERING_CORE((%%REGa, %1)   ,(%%REGa, %1, 2),%%mm2,%%mm4,%%mm0,%%mm3,%%mm5,%%mm1,%%mm6,%%mm7)
DERING_CORE((%%REGa, %1, 2),(%0, %1, 4)    ,%%mm4,%%mm0,%%mm2,%%mm5,%%mm1,%%mm3,%%mm6,%%mm7)
DERING_CORE((%0, %1, 4)    ,(%%REGd)       ,%%mm0,%%mm2,%%mm4,%%mm1,%%mm3,%%mm5,%%mm6,%%mm7)
DERING_CORE((%%REGd)       ,(%%REGd, %1)   ,%%mm2,%%mm4,%%mm0,%%mm3,%%mm5,%%mm1,%%mm6,%%mm7)
DERING_CORE((%%REGd, %1)   ,(%%REGd, %1, 2),%%mm4,%%mm0,%%mm2,%%mm5,%%mm1,%%mm3,%%mm6,%%mm7)
DERING_CORE((%%REGd, %1, 2),(%0, %1, 8)    ,%%mm0,%%mm2,%%mm4,%%mm1,%%mm3,%%mm5,%%mm6,%%mm7)
DERING_CORE((%0, %1, 8)    ,(%%REGd, %1, 4),%%mm2,%%mm4,%%mm0,%%mm3,%%mm5,%%mm1,%%mm6,%%mm7)

        "1:                        \n\t"
        : : "r" (src), "r" ((x86_reg)stride), "m" (c->pQPb), "m"(c->pQPb2)
        : "%"REG_a, "%"REG_d, "%"REG_c
    );
#else //HAVE_MMX2 || HAVE_AMD3DNOW
    int y;
    int min=255;
    int max=0;
    int avg;
    uint8_t *p;
    int s[10];
    const int QP2= c->QP/2 + 1;

    for(y=1; y<9; y++){
        int x;
        p= src + stride*y;
        for(x=1; x<9; x++){
            p++;
            if(*p > max) max= *p;
            if(*p < min) min= *p;
        }
    }
    avg= (min + max + 1)>>1;

    if(max - min <deringThreshold) return;

    for(y=0; y<10; y++){
        int t = 0;

        if(src[stride*y + 0] > avg) t+= 1;
        if(src[stride*y + 1] > avg) t+= 2;
        if(src[stride*y + 2] > avg) t+= 4;
        if(src[stride*y + 3] > avg) t+= 8;
        if(src[stride*y + 4] > avg) t+= 16;
        if(src[stride*y + 5] > avg) t+= 32;
        if(src[stride*y + 6] > avg) t+= 64;
        if(src[stride*y + 7] > avg) t+= 128;
        if(src[stride*y + 8] > avg) t+= 256;
        if(src[stride*y + 9] > avg) t+= 512;

        t |= (~t)<<16;
        t &= (t<<1) & (t>>1);
        s[y] = t;
    }

    for(y=1; y<9; y++){
        int t = s[y-1] & s[y] & s[y+1];
        t|= t>>16;
        s[y-1]= t;
    }

    for(y=1; y<9; y++){
        int x;
        int t = s[y-1];

        p= src + stride*y;
        for(x=1; x<9; x++){
            p++;
            if(t & (1<<x)){
                int f= (*(p-stride-1)) + 2*(*(p-stride)) + (*(p-stride+1))
                      +2*(*(p     -1)) + 4*(*p         ) + 2*(*(p     +1))
                      +(*(p+stride-1)) + 2*(*(p+stride)) + (*(p+stride+1));
                f= (f + 8)>>4;

#ifdef DEBUG_DERING_THRESHOLD
                    __asm__ volatile("emms\n\t":);
                    {
                    static long long numPixels=0;
                    if(x!=1 && x!=8 && y!=1 && y!=8) numPixels++;
//                    if((max-min)<20 || (max-min)*QP<200)
//                    if((max-min)*QP < 500)
//                    if(max-min<QP/2)
                    if(max-min < 20){
                        static int numSkipped=0;
                        static int errorSum=0;
                        static int worstQP=0;
                        static int worstRange=0;
                        static int worstDiff=0;
                        int diff= (f - *p);
                        int absDiff= FFABS(diff);
                        int error= diff*diff;

                        if(x==1 || x==8 || y==1 || y==8) continue;

                        numSkipped++;
                        if(absDiff > worstDiff){
                            worstDiff= absDiff;
                            worstQP= QP;
                            worstRange= max-min;
                        }
                        errorSum+= error;

                        if(1024LL*1024LL*1024LL % numSkipped == 0){
                            av_log(c, AV_LOG_INFO, "sum:%1.3f, skip:%d, wQP:%d, "
                                   "wRange:%d, wDiff:%d, relSkip:%1.3f\n",
                                   (float)errorSum/numSkipped, numSkipped, worstQP, worstRange,
                                   worstDiff, (float)numSkipped/numPixels);
                        }
                    }
                    }
#endif
                    if     (*p + QP2 < f) *p= *p + QP2;
                    else if(*p - QP2 > f) *p= *p - QP2;
                    else *p=f;
            }
        }
    }
#ifdef DEBUG_DERING_THRESHOLD
    if(max-min < 20){
        for(y=1; y<9; y++){
            int x;
            int t = 0;
            p= src + stride*y;
            for(x=1; x<9; x++){
                p++;
                *p = FFMIN(*p + 20, 255);
            }
        }
//        src[0] = src[7]=src[stride*7]=src[stride*7 + 7]=255;
    }
#endif
#endif //HAVE_MMX2 || HAVE_AMD3DNOW
}
#endif //HAVE_ALTIVEC

/**
 * Deinterlace the given block by linearly interpolating every second line.
 * will be called for every 8x8 block and can read & write from line 4-15
 * lines 0-3 have been passed through the deblock / dering filters already, but can be read, too.
 * lines 4-12 will be read into the deblocking filter and should be deinterlaced
 */
static inline void RENAME(deInterlaceInterpolateLinear)(uint8_t src[], int stride)
{
#if HAVE_MMX2 || HAVE_AMD3DNOW
    src+= 4*stride;
    __asm__ volatile(
        "lea (%0, %1), %%"REG_a"                \n\t"
        "lea (%%"REG_a", %1, 4), %%"REG_c"      \n\t"
//      0       1       2       3       4       5       6       7       8       9
//      %0      eax     eax+%1  eax+2%1 %0+4%1  ecx     ecx+%1  ecx+2%1 %0+8%1  ecx+4%1

        "movq (%0), %%mm0                       \n\t"
        "movq (%%"REG_a", %1), %%mm1            \n\t"
        PAVGB(%%mm1, %%mm0)
        "movq %%mm0, (%%"REG_a")                \n\t"
        "movq (%0, %1, 4), %%mm0                \n\t"
        PAVGB(%%mm0, %%mm1)
        "movq %%mm1, (%%"REG_a", %1, 2)         \n\t"
        "movq (%%"REG_c", %1), %%mm1            \n\t"
        PAVGB(%%mm1, %%mm0)
        "movq %%mm0, (%%"REG_c")                \n\t"
        "movq (%0, %1, 8), %%mm0                \n\t"
        PAVGB(%%mm0, %%mm1)
        "movq %%mm1, (%%"REG_c", %1, 2)         \n\t"

        : : "r" (src), "r" ((x86_reg)stride)
        : "%"REG_a, "%"REG_c
    );
#else
    int a, b, x;
    src+= 4*stride;

    for(x=0; x<2; x++){
        a= *(uint32_t*)&src[stride*0];
        b= *(uint32_t*)&src[stride*2];
        *(uint32_t*)&src[stride*1]= (a|b) - (((a^b)&0xFEFEFEFEUL)>>1);
        a= *(uint32_t*)&src[stride*4];
        *(uint32_t*)&src[stride*3]= (a|b) - (((a^b)&0xFEFEFEFEUL)>>1);
        b= *(uint32_t*)&src[stride*6];
        *(uint32_t*)&src[stride*5]= (a|b) - (((a^b)&0xFEFEFEFEUL)>>1);
        a= *(uint32_t*)&src[stride*8];
        *(uint32_t*)&src[stride*7]= (a|b) - (((a^b)&0xFEFEFEFEUL)>>1);
        src += 4;
    }
#endif
}

/**
 * Deinterlace the given block by cubic interpolating every second line.
 * will be called for every 8x8 block and can read & write from line 4-15
 * lines 0-3 have been passed through the deblock / dering filters already, but can be read, too.
 * lines 4-12 will be read into the deblocking filter and should be deinterlaced
 * this filter will read lines 3-15 and write 7-13
 */
static inline void RENAME(deInterlaceInterpolateCubic)(uint8_t src[], int stride)
{
#if HAVE_MMX2 || HAVE_AMD3DNOW
    src+= stride*3;
    __asm__ volatile(
        "lea (%0, %1), %%"REG_a"                \n\t"
        "lea (%%"REG_a", %1, 4), %%"REG_d"      \n\t"
        "lea (%%"REG_d", %1, 4), %%"REG_c"      \n\t"
        "add %1, %%"REG_c"                      \n\t"
        "pxor %%mm7, %%mm7                      \n\t"
//      0       1       2       3       4       5       6       7       8       9       10
//      %0      eax     eax+%1  eax+2%1 %0+4%1  edx     edx+%1  edx+2%1 %0+8%1  edx+4%1 ecx

#define REAL_DEINT_CUBIC(a,b,c,d,e)\
        "movq " #a ", %%mm0                     \n\t"\
        "movq " #b ", %%mm1                     \n\t"\
        "movq " #d ", %%mm2                     \n\t"\
        "movq " #e ", %%mm3                     \n\t"\
        PAVGB(%%mm2, %%mm1)                             /* (b+d) /2 */\
        PAVGB(%%mm3, %%mm0)                             /* a(a+e) /2 */\
        "movq %%mm0, %%mm2                      \n\t"\
        "punpcklbw %%mm7, %%mm0                 \n\t"\
        "punpckhbw %%mm7, %%mm2                 \n\t"\
        "movq %%mm1, %%mm3                      \n\t"\
        "punpcklbw %%mm7, %%mm1                 \n\t"\
        "punpckhbw %%mm7, %%mm3                 \n\t"\
        "psubw %%mm1, %%mm0                     \n\t"   /* L(a+e - (b+d))/2 */\
        "psubw %%mm3, %%mm2                     \n\t"   /* H(a+e - (b+d))/2 */\
        "psraw $3, %%mm0                        \n\t"   /* L(a+e - (b+d))/16 */\
        "psraw $3, %%mm2                        \n\t"   /* H(a+e - (b+d))/16 */\
        "psubw %%mm0, %%mm1                     \n\t"   /* L(9b + 9d - a - e)/16 */\
        "psubw %%mm2, %%mm3                     \n\t"   /* H(9b + 9d - a - e)/16 */\
        "packuswb %%mm3, %%mm1                  \n\t"\
        "movq %%mm1, " #c "                     \n\t"
#define DEINT_CUBIC(a,b,c,d,e)  REAL_DEINT_CUBIC(a,b,c,d,e)

DEINT_CUBIC((%0)        , (%%REGa, %1), (%%REGa, %1, 2), (%0, %1, 4) , (%%REGd, %1))
DEINT_CUBIC((%%REGa, %1), (%0, %1, 4) , (%%REGd)       , (%%REGd, %1), (%0, %1, 8))
DEINT_CUBIC((%0, %1, 4) , (%%REGd, %1), (%%REGd, %1, 2), (%0, %1, 8) , (%%REGc))
DEINT_CUBIC((%%REGd, %1), (%0, %1, 8) , (%%REGd, %1, 4), (%%REGc)    , (%%REGc, %1, 2))

        : : "r" (src), "r" ((x86_reg)stride)
        : "%"REG_a, "%"REG_d, "%"REG_c
    );
#else //HAVE_MMX2 || HAVE_AMD3DNOW
    int x;
    src+= stride*3;
    for(x=0; x<8; x++){
        src[stride*3] = CLIP((-src[0]        + 9*src[stride*2] + 9*src[stride*4] - src[stride*6])>>4);
        src[stride*5] = CLIP((-src[stride*2] + 9*src[stride*4] + 9*src[stride*6] - src[stride*8])>>4);
        src[stride*7] = CLIP((-src[stride*4] + 9*src[stride*6] + 9*src[stride*8] - src[stride*10])>>4);
        src[stride*9] = CLIP((-src[stride*6] + 9*src[stride*8] + 9*src[stride*10] - src[stride*12])>>4);
        src++;
    }
#endif //HAVE_MMX2 || HAVE_AMD3DNOW
}

/**
 * Deinterlace the given block by filtering every second line with a (-1 4 2 4 -1) filter.
 * will be called for every 8x8 block and can read & write from line 4-15
 * lines 0-3 have been passed through the deblock / dering filters already, but can be read, too.
 * lines 4-12 will be read into the deblocking filter and should be deinterlaced
 * this filter will read lines 4-13 and write 5-11
 */
static inline void RENAME(deInterlaceFF)(uint8_t src[], int stride, uint8_t *tmp)
{
#if HAVE_MMX2 || HAVE_AMD3DNOW
    src+= stride*4;
    __asm__ volatile(
        "lea (%0, %1), %%"REG_a"                \n\t"
        "lea (%%"REG_a", %1, 4), %%"REG_d"      \n\t"
        "pxor %%mm7, %%mm7                      \n\t"
        "movq (%2), %%mm0                       \n\t"
//      0       1       2       3       4       5       6       7       8       9       10
//      %0      eax     eax+%1  eax+2%1 %0+4%1  edx     edx+%1  edx+2%1 %0+8%1  edx+4%1 ecx

#define REAL_DEINT_FF(a,b,c,d)\
        "movq " #a ", %%mm1                     \n\t"\
        "movq " #b ", %%mm2                     \n\t"\
        "movq " #c ", %%mm3                     \n\t"\
        "movq " #d ", %%mm4                     \n\t"\
        PAVGB(%%mm3, %%mm1)                          \
        PAVGB(%%mm4, %%mm0)                          \
        "movq %%mm0, %%mm3                      \n\t"\
        "punpcklbw %%mm7, %%mm0                 \n\t"\
        "punpckhbw %%mm7, %%mm3                 \n\t"\
        "movq %%mm1, %%mm4                      \n\t"\
        "punpcklbw %%mm7, %%mm1                 \n\t"\
        "punpckhbw %%mm7, %%mm4                 \n\t"\
        "psllw $2, %%mm1                        \n\t"\
        "psllw $2, %%mm4                        \n\t"\
        "psubw %%mm0, %%mm1                     \n\t"\
        "psubw %%mm3, %%mm4                     \n\t"\
        "movq %%mm2, %%mm5                      \n\t"\
        "movq %%mm2, %%mm0                      \n\t"\
        "punpcklbw %%mm7, %%mm2                 \n\t"\
        "punpckhbw %%mm7, %%mm5                 \n\t"\
        "paddw %%mm2, %%mm1                     \n\t"\
        "paddw %%mm5, %%mm4                     \n\t"\
        "psraw $2, %%mm1                        \n\t"\
        "psraw $2, %%mm4                        \n\t"\
        "packuswb %%mm4, %%mm1                  \n\t"\
        "movq %%mm1, " #b "                     \n\t"\

#define DEINT_FF(a,b,c,d)  REAL_DEINT_FF(a,b,c,d)

DEINT_FF((%0)        , (%%REGa)       , (%%REGa, %1), (%%REGa, %1, 2))
DEINT_FF((%%REGa, %1), (%%REGa, %1, 2), (%0, %1, 4) , (%%REGd)       )
DEINT_FF((%0, %1, 4) , (%%REGd)       , (%%REGd, %1), (%%REGd, %1, 2))
DEINT_FF((%%REGd, %1), (%%REGd, %1, 2), (%0, %1, 8) , (%%REGd, %1, 4))

        "movq %%mm0, (%2)                       \n\t"
        : : "r" (src), "r" ((x86_reg)stride), "r"(tmp)
        : "%"REG_a, "%"REG_d
    );
#else //HAVE_MMX2 || HAVE_AMD3DNOW
    int x;
    src+= stride*4;
    for(x=0; x<8; x++){
        int t1= tmp[x];
        int t2= src[stride*1];

        src[stride*1]= CLIP((-t1 + 4*src[stride*0] + 2*t2 + 4*src[stride*2] - src[stride*3] + 4)>>3);
        t1= src[stride*4];
        src[stride*3]= CLIP((-t2 + 4*src[stride*2] + 2*t1 + 4*src[stride*4] - src[stride*5] + 4)>>3);
        t2= src[stride*6];
        src[stride*5]= CLIP((-t1 + 4*src[stride*4] + 2*t2 + 4*src[stride*6] - src[stride*7] + 4)>>3);
        t1= src[stride*8];
        src[stride*7]= CLIP((-t2 + 4*src[stride*6] + 2*t1 + 4*src[stride*8] - src[stride*9] + 4)>>3);
        tmp[x]= t1;

        src++;
    }
#endif //HAVE_MMX2 || HAVE_AMD3DNOW
}

/**
 * Deinterlace the given block by filtering every line with a (-1 2 6 2 -1) filter.
 * will be called for every 8x8 block and can read & write from line 4-15
 * lines 0-3 have been passed through the deblock / dering filters already, but can be read, too.
 * lines 4-12 will be read into the deblocking filter and should be deinterlaced
 * this filter will read lines 4-13 and write 4-11
 */
static inline void RENAME(deInterlaceL5)(uint8_t src[], int stride, uint8_t *tmp, uint8_t *tmp2)
{
#if HAVE_MMX2 || HAVE_AMD3DNOW
    src+= stride*4;
    __asm__ volatile(
        "lea (%0, %1), %%"REG_a"                \n\t"
        "lea (%%"REG_a", %1, 4), %%"REG_d"      \n\t"
        "pxor %%mm7, %%mm7                      \n\t"
        "movq (%2), %%mm0                       \n\t"
        "movq (%3), %%mm1                       \n\t"
//      0       1       2       3       4       5       6       7       8       9       10
//      %0      eax     eax+%1  eax+2%1 %0+4%1  edx     edx+%1  edx+2%1 %0+8%1  edx+4%1 ecx

#define REAL_DEINT_L5(t1,t2,a,b,c)\
        "movq " #a ", %%mm2                     \n\t"\
        "movq " #b ", %%mm3                     \n\t"\
        "movq " #c ", %%mm4                     \n\t"\
        PAVGB(t2, %%mm3)                             \
        PAVGB(t1, %%mm4)                             \
        "movq %%mm2, %%mm5                      \n\t"\
        "movq %%mm2, " #t1 "                    \n\t"\
        "punpcklbw %%mm7, %%mm2                 \n\t"\
        "punpckhbw %%mm7, %%mm5                 \n\t"\
        "movq %%mm2, %%mm6                      \n\t"\
        "paddw %%mm2, %%mm2                     \n\t"\
        "paddw %%mm6, %%mm2                     \n\t"\
        "movq %%mm5, %%mm6                      \n\t"\
        "paddw %%mm5, %%mm5                     \n\t"\
        "paddw %%mm6, %%mm5                     \n\t"\
        "movq %%mm3, %%mm6                      \n\t"\
        "punpcklbw %%mm7, %%mm3                 \n\t"\
        "punpckhbw %%mm7, %%mm6                 \n\t"\
        "paddw %%mm3, %%mm3                     \n\t"\
        "paddw %%mm6, %%mm6                     \n\t"\
        "paddw %%mm3, %%mm2                     \n\t"\
        "paddw %%mm6, %%mm5                     \n\t"\
        "movq %%mm4, %%mm6                      \n\t"\
        "punpcklbw %%mm7, %%mm4                 \n\t"\
        "punpckhbw %%mm7, %%mm6                 \n\t"\
        "psubw %%mm4, %%mm2                     \n\t"\
        "psubw %%mm6, %%mm5                     \n\t"\
        "psraw $2, %%mm2                        \n\t"\
        "psraw $2, %%mm5                        \n\t"\
        "packuswb %%mm5, %%mm2                  \n\t"\
        "movq %%mm2, " #a "                     \n\t"\

#define DEINT_L5(t1,t2,a,b,c)  REAL_DEINT_L5(t1,t2,a,b,c)

DEINT_L5(%%mm0, %%mm1, (%0)           , (%%REGa)       , (%%REGa, %1)   )
DEINT_L5(%%mm1, %%mm0, (%%REGa)       , (%%REGa, %1)   , (%%REGa, %1, 2))
DEINT_L5(%%mm0, %%mm1, (%%REGa, %1)   , (%%REGa, %1, 2), (%0, %1, 4)   )
DEINT_L5(%%mm1, %%mm0, (%%REGa, %1, 2), (%0, %1, 4)    , (%%REGd)       )
DEINT_L5(%%mm0, %%mm1, (%0, %1, 4)    , (%%REGd)       , (%%REGd, %1)   )
DEINT_L5(%%mm1, %%mm0, (%%REGd)       , (%%REGd, %1)   , (%%REGd, %1, 2))
DEINT_L5(%%mm0, %%mm1, (%%REGd, %1)   , (%%REGd, %1, 2), (%0, %1, 8)   )
DEINT_L5(%%mm1, %%mm0, (%%REGd, %1, 2), (%0, %1, 8)    , (%%REGd, %1, 4))

        "movq %%mm0, (%2)                       \n\t"
        "movq %%mm1, (%3)                       \n\t"
        : : "r" (src), "r" ((x86_reg)stride), "r"(tmp), "r"(tmp2)
        : "%"REG_a, "%"REG_d
    );
#else //HAVE_MMX2 || HAVE_AMD3DNOW
    int x;
    src+= stride*4;
    for(x=0; x<8; x++){
        int t1= tmp[x];
        int t2= tmp2[x];
        int t3= src[0];

        src[stride*0]= CLIP((-(t1 + src[stride*2]) + 2*(t2 + src[stride*1]) + 6*t3 + 4)>>3);
        t1= src[stride*1];
        src[stride*1]= CLIP((-(t2 + src[stride*3]) + 2*(t3 + src[stride*2]) + 6*t1 + 4)>>3);
        t2= src[stride*2];
        src[stride*2]= CLIP((-(t3 + src[stride*4]) + 2*(t1 + src[stride*3]) + 6*t2 + 4)>>3);
        t3= src[stride*3];
        src[stride*3]= CLIP((-(t1 + src[stride*5]) + 2*(t2 + src[stride*4]) + 6*t3 + 4)>>3);
        t1= src[stride*4];
        src[stride*4]= CLIP((-(t2 + src[stride*6]) + 2*(t3 + src[stride*5]) + 6*t1 + 4)>>3);
        t2= src[stride*5];
        src[stride*5]= CLIP((-(t3 + src[stride*7]) + 2*(t1 + src[stride*6]) + 6*t2 + 4)>>3);
        t3= src[stride*6];
        src[stride*6]= CLIP((-(t1 + src[stride*8]) + 2*(t2 + src[stride*7]) + 6*t3 + 4)>>3);
        t1= src[stride*7];
        src[stride*7]= CLIP((-(t2 + src[stride*9]) + 2*(t3 + src[stride*8]) + 6*t1 + 4)>>3);

        tmp[x]= t3;
        tmp2[x]= t1;

        src++;
    }
#endif //HAVE_MMX2 || HAVE_AMD3DNOW
}

/**
 * Deinterlace the given block by filtering all lines with a (1 2 1) filter.
 * will be called for every 8x8 block and can read & write from line 4-15
 * lines 0-3 have been passed through the deblock / dering filters already, but can be read, too.
 * lines 4-12 will be read into the deblocking filter and should be deinterlaced
 * this filter will read lines 4-13 and write 4-11
 */
static inline void RENAME(deInterlaceBlendLinear)(uint8_t src[], int stride, uint8_t *tmp)
{
#if HAVE_MMX2 || HAVE_AMD3DNOW
    src+= 4*stride;
    __asm__ volatile(
        "lea (%0, %1), %%"REG_a"                \n\t"
        "lea (%%"REG_a", %1, 4), %%"REG_d"      \n\t"
//      0       1       2       3       4       5       6       7       8       9
//      %0      eax     eax+%1  eax+2%1 %0+4%1  edx     edx+%1  edx+2%1 %0+8%1  edx+4%1

        "movq (%2), %%mm0                       \n\t" // L0
        "movq (%%"REG_a"), %%mm1                \n\t" // L2
        PAVGB(%%mm1, %%mm0)                           // L0+L2
        "movq (%0), %%mm2                       \n\t" // L1
        PAVGB(%%mm2, %%mm0)
        "movq %%mm0, (%0)                       \n\t"
        "movq (%%"REG_a", %1), %%mm0            \n\t" // L3
        PAVGB(%%mm0, %%mm2)                           // L1+L3
        PAVGB(%%mm1, %%mm2)                           // 2L2 + L1 + L3
        "movq %%mm2, (%%"REG_a")                \n\t"
        "movq (%%"REG_a", %1, 2), %%mm2         \n\t" // L4
        PAVGB(%%mm2, %%mm1)                           // L2+L4
        PAVGB(%%mm0, %%mm1)                           // 2L3 + L2 + L4
        "movq %%mm1, (%%"REG_a", %1)            \n\t"
        "movq (%0, %1, 4), %%mm1                \n\t" // L5
        PAVGB(%%mm1, %%mm0)                           // L3+L5
        PAVGB(%%mm2, %%mm0)                           // 2L4 + L3 + L5
        "movq %%mm0, (%%"REG_a", %1, 2)         \n\t"
        "movq (%%"REG_d"), %%mm0                \n\t" // L6
        PAVGB(%%mm0, %%mm2)                           // L4+L6
        PAVGB(%%mm1, %%mm2)                           // 2L5 + L4 + L6
        "movq %%mm2, (%0, %1, 4)                \n\t"
        "movq (%%"REG_d", %1), %%mm2            \n\t" // L7
        PAVGB(%%mm2, %%mm1)                           // L5+L7
        PAVGB(%%mm0, %%mm1)                           // 2L6 + L5 + L7
        "movq %%mm1, (%%"REG_d")                \n\t"
        "movq (%%"REG_d", %1, 2), %%mm1         \n\t" // L8
        PAVGB(%%mm1, %%mm0)                           // L6+L8
        PAVGB(%%mm2, %%mm0)                           // 2L7 + L6 + L8
        "movq %%mm0, (%%"REG_d", %1)            \n\t"
        "movq (%0, %1, 8), %%mm0                \n\t" // L9
        PAVGB(%%mm0, %%mm2)                           // L7+L9
        PAVGB(%%mm1, %%mm2)                           // 2L8 + L7 + L9
        "movq %%mm2, (%%"REG_d", %1, 2)         \n\t"
        "movq %%mm1, (%2)                       \n\t"

        : : "r" (src), "r" ((x86_reg)stride), "r" (tmp)
        : "%"REG_a, "%"REG_d
    );
#else //HAVE_MMX2 || HAVE_AMD3DNOW
    int a, b, c, x;
    src+= 4*stride;

    for(x=0; x<2; x++){
        a= *(uint32_t*)&tmp[stride*0];
        b= *(uint32_t*)&src[stride*0];
        c= *(uint32_t*)&src[stride*1];
        a= (a&c) + (((a^c)&0xFEFEFEFEUL)>>1);
        *(uint32_t*)&src[stride*0]= (a|b) - (((a^b)&0xFEFEFEFEUL)>>1);

        a= *(uint32_t*)&src[stride*2];
        b= (a&b) + (((a^b)&0xFEFEFEFEUL)>>1);
        *(uint32_t*)&src[stride*1]= (c|b) - (((c^b)&0xFEFEFEFEUL)>>1);

        b= *(uint32_t*)&src[stride*3];
        c= (b&c) + (((b^c)&0xFEFEFEFEUL)>>1);
        *(uint32_t*)&src[stride*2]= (c|a) - (((c^a)&0xFEFEFEFEUL)>>1);

        c= *(uint32_t*)&src[stride*4];
        a= (a&c) + (((a^c)&0xFEFEFEFEUL)>>1);
        *(uint32_t*)&src[stride*3]= (a|b) - (((a^b)&0xFEFEFEFEUL)>>1);

        a= *(uint32_t*)&src[stride*5];
        b= (a&b) + (((a^b)&0xFEFEFEFEUL)>>1);
        *(uint32_t*)&src[stride*4]= (c|b) - (((c^b)&0xFEFEFEFEUL)>>1);

        b= *(uint32_t*)&src[stride*6];
        c= (b&c) + (((b^c)&0xFEFEFEFEUL)>>1);
        *(uint32_t*)&src[stride*5]= (c|a) - (((c^a)&0xFEFEFEFEUL)>>1);

        c= *(uint32_t*)&src[stride*7];
        a= (a&c) + (((a^c)&0xFEFEFEFEUL)>>1);
        *(uint32_t*)&src[stride*6]= (a|b) - (((a^b)&0xFEFEFEFEUL)>>1);

        a= *(uint32_t*)&src[stride*8];
        b= (a&b) + (((a^b)&0xFEFEFEFEUL)>>1);
        *(uint32_t*)&src[stride*7]= (c|b) - (((c^b)&0xFEFEFEFEUL)>>1);

        *(uint32_t*)&tmp[stride*0]= c;
        src += 4;
        tmp += 4;
    }
#endif //HAVE_MMX2 || HAVE_AMD3DNOW
}

/**
 * Deinterlace the given block by applying a median filter to every second line.
 * will be called for every 8x8 block and can read & write from line 4-15,
 * lines 0-3 have been passed through the deblock / dering filters already, but can be read, too.
 * lines 4-12 will be read into the deblocking filter and should be deinterlaced
 */
static inline void RENAME(deInterlaceMedian)(uint8_t src[], int stride)
{
#if HAVE_MMX
    src+= 4*stride;
#if HAVE_MMX2
    __asm__ volatile(
        "lea (%0, %1), %%"REG_a"                \n\t"
        "lea (%%"REG_a", %1, 4), %%"REG_d"      \n\t"
//      0       1       2       3       4       5       6       7       8       9
//      %0      eax     eax+%1  eax+2%1 %0+4%1  edx     edx+%1  edx+2%1 %0+8%1  edx+4%1

        "movq (%0), %%mm0                       \n\t" //
        "movq (%%"REG_a", %1), %%mm2            \n\t" //
        "movq (%%"REG_a"), %%mm1                \n\t" //
        "movq %%mm0, %%mm3                      \n\t"
        "pmaxub %%mm1, %%mm0                    \n\t" //
        "pminub %%mm3, %%mm1                    \n\t" //
        "pmaxub %%mm2, %%mm1                    \n\t" //
        "pminub %%mm1, %%mm0                    \n\t"
        "movq %%mm0, (%%"REG_a")                \n\t"

        "movq (%0, %1, 4), %%mm0                \n\t" //
        "movq (%%"REG_a", %1, 2), %%mm1         \n\t" //
        "movq %%mm2, %%mm3                      \n\t"
        "pmaxub %%mm1, %%mm2                    \n\t" //
        "pminub %%mm3, %%mm1                    \n\t" //
        "pmaxub %%mm0, %%mm1                    \n\t" //
        "pminub %%mm1, %%mm2                    \n\t"
        "movq %%mm2, (%%"REG_a", %1, 2)         \n\t"

        "movq (%%"REG_d"), %%mm2                \n\t" //
        "movq (%%"REG_d", %1), %%mm1            \n\t" //
        "movq %%mm2, %%mm3                      \n\t"
        "pmaxub %%mm0, %%mm2                    \n\t" //
        "pminub %%mm3, %%mm0                    \n\t" //
        "pmaxub %%mm1, %%mm0                    \n\t" //
        "pminub %%mm0, %%mm2                    \n\t"
        "movq %%mm2, (%%"REG_d")                \n\t"

        "movq (%%"REG_d", %1, 2), %%mm2         \n\t" //
        "movq (%0, %1, 8), %%mm0                \n\t" //
        "movq %%mm2, %%mm3                      \n\t"
        "pmaxub %%mm0, %%mm2                    \n\t" //
        "pminub %%mm3, %%mm0                    \n\t" //
        "pmaxub %%mm1, %%mm0                    \n\t" //
        "pminub %%mm0, %%mm2                    \n\t"
        "movq %%mm2, (%%"REG_d", %1, 2)         \n\t"


        : : "r" (src), "r" ((x86_reg)stride)
        : "%"REG_a, "%"REG_d
    );

#else // MMX without MMX2
    __asm__ volatile(
        "lea (%0, %1), %%"REG_a"                \n\t"
        "lea (%%"REG_a", %1, 4), %%"REG_d"      \n\t"
//      0       1       2       3       4       5       6       7       8       9
//      %0      eax     eax+%1  eax+2%1 %0+4%1  edx     edx+%1  edx+2%1 %0+8%1  edx+4%1
        "pxor %%mm7, %%mm7                      \n\t"

#define REAL_MEDIAN(a,b,c)\
        "movq " #a ", %%mm0                     \n\t"\
        "movq " #b ", %%mm2                     \n\t"\
        "movq " #c ", %%mm1                     \n\t"\
        "movq %%mm0, %%mm3                      \n\t"\
        "movq %%mm1, %%mm4                      \n\t"\
        "movq %%mm2, %%mm5                      \n\t"\
        "psubusb %%mm1, %%mm3                   \n\t"\
        "psubusb %%mm2, %%mm4                   \n\t"\
        "psubusb %%mm0, %%mm5                   \n\t"\
        "pcmpeqb %%mm7, %%mm3                   \n\t"\
        "pcmpeqb %%mm7, %%mm4                   \n\t"\
        "pcmpeqb %%mm7, %%mm5                   \n\t"\
        "movq %%mm3, %%mm6                      \n\t"\
        "pxor %%mm4, %%mm3                      \n\t"\
        "pxor %%mm5, %%mm4                      \n\t"\
        "pxor %%mm6, %%mm5                      \n\t"\
        "por %%mm3, %%mm1                       \n\t"\
        "por %%mm4, %%mm2                       \n\t"\
        "por %%mm5, %%mm0                       \n\t"\
        "pand %%mm2, %%mm0                      \n\t"\
        "pand %%mm1, %%mm0                      \n\t"\
        "movq %%mm0, " #b "                     \n\t"
#define MEDIAN(a,b,c)  REAL_MEDIAN(a,b,c)

MEDIAN((%0)        , (%%REGa)       , (%%REGa, %1))
MEDIAN((%%REGa, %1), (%%REGa, %1, 2), (%0, %1, 4))
MEDIAN((%0, %1, 4) , (%%REGd)       , (%%REGd, %1))
MEDIAN((%%REGd, %1), (%%REGd, %1, 2), (%0, %1, 8))

        : : "r" (src), "r" ((x86_reg)stride)
        : "%"REG_a, "%"REG_d
    );
#endif //HAVE_MMX2
#else //HAVE_MMX
    int x, y;
    src+= 4*stride;
    // FIXME - there should be a way to do a few columns in parallel like w/mmx
    for(x=0; x<8; x++){
        uint8_t *colsrc = src;
        for (y=0; y<4; y++){
            int a, b, c, d, e, f;
            a = colsrc[0       ];
            b = colsrc[stride  ];
            c = colsrc[stride*2];
            d = (a-b)>>31;
            e = (b-c)>>31;
            f = (c-a)>>31;
            colsrc[stride  ] = (a|(d^f)) & (b|(d^e)) & (c|(e^f));
            colsrc += stride*2;
        }
        src++;
    }
#endif //HAVE_MMX
}

#if HAVE_MMX
/**
 * Transpose and shift the given 8x8 Block into dst1 and dst2.
 */
static inline void RENAME(transpose1)(uint8_t *dst1, uint8_t *dst2, uint8_t *src, int srcStride)
{
    __asm__(
        "lea (%0, %1), %%"REG_a"                \n\t"
//      0       1       2       3       4       5       6       7       8       9
//      %0      eax     eax+%1  eax+2%1 %0+4%1  edx     edx+%1  edx+2%1 %0+8%1  edx+4%1
        "movq (%0), %%mm0                       \n\t" // 12345678
        "movq (%%"REG_a"), %%mm1                \n\t" // abcdefgh
        "movq %%mm0, %%mm2                      \n\t" // 12345678
        "punpcklbw %%mm1, %%mm0                 \n\t" // 1a2b3c4d
        "punpckhbw %%mm1, %%mm2                 \n\t" // 5e6f7g8h

        "movq (%%"REG_a", %1), %%mm1            \n\t"
        "movq (%%"REG_a", %1, 2), %%mm3         \n\t"
        "movq %%mm1, %%mm4                      \n\t"
        "punpcklbw %%mm3, %%mm1                 \n\t"
        "punpckhbw %%mm3, %%mm4                 \n\t"

        "movq %%mm0, %%mm3                      \n\t"
        "punpcklwd %%mm1, %%mm0                 \n\t"
        "punpckhwd %%mm1, %%mm3                 \n\t"
        "movq %%mm2, %%mm1                      \n\t"
        "punpcklwd %%mm4, %%mm2                 \n\t"
        "punpckhwd %%mm4, %%mm1                 \n\t"

        "movd %%mm0, 128(%2)                    \n\t"
        "psrlq $32, %%mm0                       \n\t"
        "movd %%mm0, 144(%2)                    \n\t"
        "movd %%mm3, 160(%2)                    \n\t"
        "psrlq $32, %%mm3                       \n\t"
        "movd %%mm3, 176(%2)                    \n\t"
        "movd %%mm3, 48(%3)                     \n\t"
        "movd %%mm2, 192(%2)                    \n\t"
        "movd %%mm2, 64(%3)                     \n\t"
        "psrlq $32, %%mm2                       \n\t"
        "movd %%mm2, 80(%3)                     \n\t"
        "movd %%mm1, 96(%3)                     \n\t"
        "psrlq $32, %%mm1                       \n\t"
        "movd %%mm1, 112(%3)                    \n\t"

        "lea (%%"REG_a", %1, 4), %%"REG_a"      \n\t"

        "movq (%0, %1, 4), %%mm0                \n\t" // 12345678
        "movq (%%"REG_a"), %%mm1                \n\t" // abcdefgh
        "movq %%mm0, %%mm2                      \n\t" // 12345678
        "punpcklbw %%mm1, %%mm0                 \n\t" // 1a2b3c4d
        "punpckhbw %%mm1, %%mm2                 \n\t" // 5e6f7g8h

        "movq (%%"REG_a", %1), %%mm1            \n\t"
        "movq (%%"REG_a", %1, 2), %%mm3         \n\t"
        "movq %%mm1, %%mm4                      \n\t"
        "punpcklbw %%mm3, %%mm1                 \n\t"
        "punpckhbw %%mm3, %%mm4                 \n\t"

        "movq %%mm0, %%mm3                      \n\t"
        "punpcklwd %%mm1, %%mm0                 \n\t"
        "punpckhwd %%mm1, %%mm3                 \n\t"
        "movq %%mm2, %%mm1                      \n\t"
        "punpcklwd %%mm4, %%mm2                 \n\t"
        "punpckhwd %%mm4, %%mm1                 \n\t"

        "movd %%mm0, 132(%2)                    \n\t"
        "psrlq $32, %%mm0                       \n\t"
        "movd %%mm0, 148(%2)                    \n\t"
        "movd %%mm3, 164(%2)                    \n\t"
        "psrlq $32, %%mm3                       \n\t"
        "movd %%mm3, 180(%2)                    \n\t"
        "movd %%mm3, 52(%3)                     \n\t"
        "movd %%mm2, 196(%2)                    \n\t"
        "movd %%mm2, 68(%3)                     \n\t"
        "psrlq $32, %%mm2                       \n\t"
        "movd %%mm2, 84(%3)                     \n\t"
        "movd %%mm1, 100(%3)                    \n\t"
        "psrlq $32, %%mm1                       \n\t"
        "movd %%mm1, 116(%3)                    \n\t"


        :: "r" (src), "r" ((x86_reg)srcStride), "r" (dst1), "r" (dst2)
        : "%"REG_a
    );
}

/**
 * Transpose the given 8x8 block.
 */
static inline void RENAME(transpose2)(uint8_t *dst, int dstStride, uint8_t *src)
{
    __asm__(
        "lea (%0, %1), %%"REG_a"                \n\t"
        "lea (%%"REG_a",%1,4), %%"REG_d"        \n\t"
//      0       1       2       3       4       5       6       7       8       9
//      %0      eax     eax+%1  eax+2%1 %0+4%1  edx     edx+%1  edx+2%1 %0+8%1  edx+4%1
        "movq (%2), %%mm0                       \n\t" // 12345678
        "movq 16(%2), %%mm1                     \n\t" // abcdefgh
        "movq %%mm0, %%mm2                      \n\t" // 12345678
        "punpcklbw %%mm1, %%mm0                 \n\t" // 1a2b3c4d
        "punpckhbw %%mm1, %%mm2                 \n\t" // 5e6f7g8h

        "movq 32(%2), %%mm1                     \n\t"
        "movq 48(%2), %%mm3                     \n\t"
        "movq %%mm1, %%mm4                      \n\t"
        "punpcklbw %%mm3, %%mm1                 \n\t"
        "punpckhbw %%mm3, %%mm4                 \n\t"

        "movq %%mm0, %%mm3                      \n\t"
        "punpcklwd %%mm1, %%mm0                 \n\t"
        "punpckhwd %%mm1, %%mm3                 \n\t"
        "movq %%mm2, %%mm1                      \n\t"
        "punpcklwd %%mm4, %%mm2                 \n\t"
        "punpckhwd %%mm4, %%mm1                 \n\t"

        "movd %%mm0, (%0)                       \n\t"
        "psrlq $32, %%mm0                       \n\t"
        "movd %%mm0, (%%"REG_a")                \n\t"
        "movd %%mm3, (%%"REG_a", %1)            \n\t"
        "psrlq $32, %%mm3                       \n\t"
        "movd %%mm3, (%%"REG_a", %1, 2)         \n\t"
        "movd %%mm2, (%0, %1, 4)                \n\t"
        "psrlq $32, %%mm2                       \n\t"
        "movd %%mm2, (%%"REG_d")                \n\t"
        "movd %%mm1, (%%"REG_d", %1)            \n\t"
        "psrlq $32, %%mm1                       \n\t"
        "movd %%mm1, (%%"REG_d", %1, 2)         \n\t"


        "movq 64(%2), %%mm0                     \n\t" // 12345678
        "movq 80(%2), %%mm1                     \n\t" // abcdefgh
        "movq %%mm0, %%mm2                      \n\t" // 12345678
        "punpcklbw %%mm1, %%mm0                 \n\t" // 1a2b3c4d
        "punpckhbw %%mm1, %%mm2                 \n\t" // 5e6f7g8h

        "movq 96(%2), %%mm1                     \n\t"
        "movq 112(%2), %%mm3                    \n\t"
        "movq %%mm1, %%mm4                      \n\t"
        "punpcklbw %%mm3, %%mm1                 \n\t"
        "punpckhbw %%mm3, %%mm4                 \n\t"

        "movq %%mm0, %%mm3                      \n\t"
        "punpcklwd %%mm1, %%mm0                 \n\t"
        "punpckhwd %%mm1, %%mm3                 \n\t"
        "movq %%mm2, %%mm1                      \n\t"
        "punpcklwd %%mm4, %%mm2                 \n\t"
        "punpckhwd %%mm4, %%mm1                 \n\t"

        "movd %%mm0, 4(%0)                      \n\t"
        "psrlq $32, %%mm0                       \n\t"
        "movd %%mm0, 4(%%"REG_a")               \n\t"
        "movd %%mm3, 4(%%"REG_a", %1)           \n\t"
        "psrlq $32, %%mm3                       \n\t"
        "movd %%mm3, 4(%%"REG_a", %1, 2)        \n\t"
        "movd %%mm2, 4(%0, %1, 4)               \n\t"
        "psrlq $32, %%mm2                       \n\t"
        "movd %%mm2, 4(%%"REG_d")               \n\t"
        "movd %%mm1, 4(%%"REG_d", %1)           \n\t"
        "psrlq $32, %%mm1                       \n\t"
        "movd %%mm1, 4(%%"REG_d", %1, 2)        \n\t"

        :: "r" (dst), "r" ((x86_reg)dstStride), "r" (src)
        : "%"REG_a, "%"REG_d
    );
}
#endif //HAVE_MMX
//static long test=0;

#if !HAVE_ALTIVEC
static inline void RENAME(tempNoiseReducer)(uint8_t *src, int stride,
                                    uint8_t *tempBlurred, uint32_t *tempBlurredPast, int *maxNoise)
{
    // to save a register (FIXME do this outside of the loops)
    tempBlurredPast[127]= maxNoise[0];
    tempBlurredPast[128]= maxNoise[1];
    tempBlurredPast[129]= maxNoise[2];

#define FAST_L2_DIFF
//#define L1_DIFF //u should change the thresholds too if u try that one
#if HAVE_MMX2 || HAVE_AMD3DNOW
    __asm__ volatile(
        "lea (%2, %2, 2), %%"REG_a"             \n\t" // 3*stride
        "lea (%2, %2, 4), %%"REG_d"             \n\t" // 5*stride
        "lea (%%"REG_d", %2, 2), %%"REG_c"      \n\t" // 7*stride
//      0       1       2       3       4       5       6       7       8       9
//      %x      %x+%2   %x+2%2  %x+eax  %x+4%2  %x+edx  %x+2eax %x+ecx  %x+8%2
//FIXME reorder?
#ifdef L1_DIFF //needs mmx2
        "movq (%0), %%mm0                       \n\t" // L0
        "psadbw (%1), %%mm0                     \n\t" // |L0-R0|
        "movq (%0, %2), %%mm1                   \n\t" // L1
        "psadbw (%1, %2), %%mm1                 \n\t" // |L1-R1|
        "movq (%0, %2, 2), %%mm2                \n\t" // L2
        "psadbw (%1, %2, 2), %%mm2              \n\t" // |L2-R2|
        "movq (%0, %%"REG_a"), %%mm3            \n\t" // L3
        "psadbw (%1, %%"REG_a"), %%mm3          \n\t" // |L3-R3|

        "movq (%0, %2, 4), %%mm4                \n\t" // L4
        "paddw %%mm1, %%mm0                     \n\t"
        "psadbw (%1, %2, 4), %%mm4              \n\t" // |L4-R4|
        "movq (%0, %%"REG_d"), %%mm5            \n\t" // L5
        "paddw %%mm2, %%mm0                     \n\t"
        "psadbw (%1, %%"REG_d"), %%mm5          \n\t" // |L5-R5|
        "movq (%0, %%"REG_a", 2), %%mm6         \n\t" // L6
        "paddw %%mm3, %%mm0                     \n\t"
        "psadbw (%1, %%"REG_a", 2), %%mm6       \n\t" // |L6-R6|
        "movq (%0, %%"REG_c"), %%mm7            \n\t" // L7
        "paddw %%mm4, %%mm0                     \n\t"
        "psadbw (%1, %%"REG_c"), %%mm7          \n\t" // |L7-R7|
        "paddw %%mm5, %%mm6                     \n\t"
        "paddw %%mm7, %%mm6                     \n\t"
        "paddw %%mm6, %%mm0                     \n\t"
#else //L1_DIFF
#if defined (FAST_L2_DIFF)
        "pcmpeqb %%mm7, %%mm7                   \n\t"
        "movq "MANGLE(b80)", %%mm6              \n\t"
        "pxor %%mm0, %%mm0                      \n\t"
#define REAL_L2_DIFF_CORE(a, b)\
        "movq " #a ", %%mm5                     \n\t"\
        "movq " #b ", %%mm2                     \n\t"\
        "pxor %%mm7, %%mm2                      \n\t"\
        PAVGB(%%mm2, %%mm5)\
        "paddb %%mm6, %%mm5                     \n\t"\
        "movq %%mm5, %%mm2                      \n\t"\
        "psllw $8, %%mm5                        \n\t"\
        "pmaddwd %%mm5, %%mm5                   \n\t"\
        "pmaddwd %%mm2, %%mm2                   \n\t"\
        "paddd %%mm2, %%mm5                     \n\t"\
        "psrld $14, %%mm5                       \n\t"\
        "paddd %%mm5, %%mm0                     \n\t"

#else //defined (FAST_L2_DIFF)
        "pxor %%mm7, %%mm7                      \n\t"
        "pxor %%mm0, %%mm0                      \n\t"
#define REAL_L2_DIFF_CORE(a, b)\
        "movq " #a ", %%mm5                     \n\t"\
        "movq " #b ", %%mm2                     \n\t"\
        "movq %%mm5, %%mm1                      \n\t"\
        "movq %%mm2, %%mm3                      \n\t"\
        "punpcklbw %%mm7, %%mm5                 \n\t"\
        "punpckhbw %%mm7, %%mm1                 \n\t"\
        "punpcklbw %%mm7, %%mm2                 \n\t"\
        "punpckhbw %%mm7, %%mm3                 \n\t"\
        "psubw %%mm2, %%mm5                     \n\t"\
        "psubw %%mm3, %%mm1                     \n\t"\
        "pmaddwd %%mm5, %%mm5                   \n\t"\
        "pmaddwd %%mm1, %%mm1                   \n\t"\
        "paddd %%mm1, %%mm5                     \n\t"\
        "paddd %%mm5, %%mm0                     \n\t"

#endif //defined (FAST_L2_DIFF)

#define L2_DIFF_CORE(a, b)  REAL_L2_DIFF_CORE(a, b)

L2_DIFF_CORE((%0)          , (%1))
L2_DIFF_CORE((%0, %2)      , (%1, %2))
L2_DIFF_CORE((%0, %2, 2)   , (%1, %2, 2))
L2_DIFF_CORE((%0, %%REGa)  , (%1, %%REGa))
L2_DIFF_CORE((%0, %2, 4)   , (%1, %2, 4))
L2_DIFF_CORE((%0, %%REGd)  , (%1, %%REGd))
L2_DIFF_CORE((%0, %%REGa,2), (%1, %%REGa,2))
L2_DIFF_CORE((%0, %%REGc)  , (%1, %%REGc))

#endif //L1_DIFF

        "movq %%mm0, %%mm4                      \n\t"
        "psrlq $32, %%mm0                       \n\t"
        "paddd %%mm0, %%mm4                     \n\t"
        "movd %%mm4, %%ecx                      \n\t"
        "shll $2, %%ecx                         \n\t"
        "mov %3, %%"REG_d"                      \n\t"
        "addl -4(%%"REG_d"), %%ecx              \n\t"
        "addl 4(%%"REG_d"), %%ecx               \n\t"
        "addl -1024(%%"REG_d"), %%ecx           \n\t"
        "addl $4, %%ecx                         \n\t"
        "addl 1024(%%"REG_d"), %%ecx            \n\t"
        "shrl $3, %%ecx                         \n\t"
        "movl %%ecx, (%%"REG_d")                \n\t"

//        "mov %3, %%"REG_c"                      \n\t"
//        "mov %%"REG_c", test                    \n\t"
//        "jmp 4f                                 \n\t"
        "cmpl 512(%%"REG_d"), %%ecx             \n\t"
        " jb 2f                                 \n\t"
        "cmpl 516(%%"REG_d"), %%ecx             \n\t"
        " jb 1f                                 \n\t"

        "lea (%%"REG_a", %2, 2), %%"REG_d"      \n\t" // 5*stride
        "lea (%%"REG_d", %2, 2), %%"REG_c"      \n\t" // 7*stride
        "movq (%0), %%mm0                       \n\t" // L0
        "movq (%0, %2), %%mm1                   \n\t" // L1
        "movq (%0, %2, 2), %%mm2                \n\t" // L2
        "movq (%0, %%"REG_a"), %%mm3            \n\t" // L3
        "movq (%0, %2, 4), %%mm4                \n\t" // L4
        "movq (%0, %%"REG_d"), %%mm5            \n\t" // L5
        "movq (%0, %%"REG_a", 2), %%mm6         \n\t" // L6
        "movq (%0, %%"REG_c"), %%mm7            \n\t" // L7
        "movq %%mm0, (%1)                       \n\t" // L0
        "movq %%mm1, (%1, %2)                   \n\t" // L1
        "movq %%mm2, (%1, %2, 2)                \n\t" // L2
        "movq %%mm3, (%1, %%"REG_a")            \n\t" // L3
        "movq %%mm4, (%1, %2, 4)                \n\t" // L4
        "movq %%mm5, (%1, %%"REG_d")            \n\t" // L5
        "movq %%mm6, (%1, %%"REG_a", 2)         \n\t" // L6
        "movq %%mm7, (%1, %%"REG_c")            \n\t" // L7
        "jmp 4f                                 \n\t"

        "1:                                     \n\t"
        "lea (%%"REG_a", %2, 2), %%"REG_d"      \n\t" // 5*stride
        "lea (%%"REG_d", %2, 2), %%"REG_c"      \n\t" // 7*stride
        "movq (%0), %%mm0                       \n\t" // L0
        PAVGB((%1), %%mm0)                            // L0
        "movq (%0, %2), %%mm1                   \n\t" // L1
        PAVGB((%1, %2), %%mm1)                        // L1
        "movq (%0, %2, 2), %%mm2                \n\t" // L2
        PAVGB((%1, %2, 2), %%mm2)                     // L2
        "movq (%0, %%"REG_a"), %%mm3            \n\t" // L3
        PAVGB((%1, %%REGa), %%mm3)                    // L3
        "movq (%0, %2, 4), %%mm4                \n\t" // L4
        PAVGB((%1, %2, 4), %%mm4)                     // L4
        "movq (%0, %%"REG_d"), %%mm5            \n\t" // L5
        PAVGB((%1, %%REGd), %%mm5)                    // L5
        "movq (%0, %%"REG_a", 2), %%mm6         \n\t" // L6
        PAVGB((%1, %%REGa, 2), %%mm6)                 // L6
        "movq (%0, %%"REG_c"), %%mm7            \n\t" // L7
        PAVGB((%1, %%REGc), %%mm7)                    // L7
        "movq %%mm0, (%1)                       \n\t" // R0
        "movq %%mm1, (%1, %2)                   \n\t" // R1
        "movq %%mm2, (%1, %2, 2)                \n\t" // R2
        "movq %%mm3, (%1, %%"REG_a")            \n\t" // R3
        "movq %%mm4, (%1, %2, 4)                \n\t" // R4
        "movq %%mm5, (%1, %%"REG_d")            \n\t" // R5
        "movq %%mm6, (%1, %%"REG_a", 2)         \n\t" // R6
        "movq %%mm7, (%1, %%"REG_c")            \n\t" // R7
        "movq %%mm0, (%0)                       \n\t" // L0
        "movq %%mm1, (%0, %2)                   \n\t" // L1
        "movq %%mm2, (%0, %2, 2)                \n\t" // L2
        "movq %%mm3, (%0, %%"REG_a")            \n\t" // L3
        "movq %%mm4, (%0, %2, 4)                \n\t" // L4
        "movq %%mm5, (%0, %%"REG_d")            \n\t" // L5
        "movq %%mm6, (%0, %%"REG_a", 2)         \n\t" // L6
        "movq %%mm7, (%0, %%"REG_c")            \n\t" // L7
        "jmp 4f                                 \n\t"

        "2:                                     \n\t"
        "cmpl 508(%%"REG_d"), %%ecx             \n\t"
        " jb 3f                                 \n\t"

        "lea (%%"REG_a", %2, 2), %%"REG_d"      \n\t" // 5*stride
        "lea (%%"REG_d", %2, 2), %%"REG_c"      \n\t" // 7*stride
        "movq (%0), %%mm0                       \n\t" // L0
        "movq (%0, %2), %%mm1                   \n\t" // L1
        "movq (%0, %2, 2), %%mm2                \n\t" // L2
        "movq (%0, %%"REG_a"), %%mm3            \n\t" // L3
        "movq (%1), %%mm4                       \n\t" // R0
        "movq (%1, %2), %%mm5                   \n\t" // R1
        "movq (%1, %2, 2), %%mm6                \n\t" // R2
        "movq (%1, %%"REG_a"), %%mm7            \n\t" // R3
        PAVGB(%%mm4, %%mm0)
        PAVGB(%%mm5, %%mm1)
        PAVGB(%%mm6, %%mm2)
        PAVGB(%%mm7, %%mm3)
        PAVGB(%%mm4, %%mm0)
        PAVGB(%%mm5, %%mm1)
        PAVGB(%%mm6, %%mm2)
        PAVGB(%%mm7, %%mm3)
        "movq %%mm0, (%1)                       \n\t" // R0
        "movq %%mm1, (%1, %2)                   \n\t" // R1
        "movq %%mm2, (%1, %2, 2)                \n\t" // R2
        "movq %%mm3, (%1, %%"REG_a")            \n\t" // R3
        "movq %%mm0, (%0)                       \n\t" // L0
        "movq %%mm1, (%0, %2)                   \n\t" // L1
        "movq %%mm2, (%0, %2, 2)                \n\t" // L2
        "movq %%mm3, (%0, %%"REG_a")            \n\t" // L3

        "movq (%0, %2, 4), %%mm0                \n\t" // L4
        "movq (%0, %%"REG_d"), %%mm1            \n\t" // L5
        "movq (%0, %%"REG_a", 2), %%mm2         \n\t" // L6
        "movq (%0, %%"REG_c"), %%mm3            \n\t" // L7
        "movq (%1, %2, 4), %%mm4                \n\t" // R4
        "movq (%1, %%"REG_d"), %%mm5            \n\t" // R5
        "movq (%1, %%"REG_a", 2), %%mm6         \n\t" // R6
        "movq (%1, %%"REG_c"), %%mm7            \n\t" // R7
        PAVGB(%%mm4, %%mm0)
        PAVGB(%%mm5, %%mm1)
        PAVGB(%%mm6, %%mm2)
        PAVGB(%%mm7, %%mm3)
        PAVGB(%%mm4, %%mm0)
        PAVGB(%%mm5, %%mm1)
        PAVGB(%%mm6, %%mm2)
        PAVGB(%%mm7, %%mm3)
        "movq %%mm0, (%1, %2, 4)                \n\t" // R4
        "movq %%mm1, (%1, %%"REG_d")            \n\t" // R5
        "movq %%mm2, (%1, %%"REG_a", 2)         \n\t" // R6
        "movq %%mm3, (%1, %%"REG_c")            \n\t" // R7
        "movq %%mm0, (%0, %2, 4)                \n\t" // L4
        "movq %%mm1, (%0, %%"REG_d")            \n\t" // L5
        "movq %%mm2, (%0, %%"REG_a", 2)         \n\t" // L6
        "movq %%mm3, (%0, %%"REG_c")            \n\t" // L7
        "jmp 4f                                 \n\t"

        "3:                                     \n\t"
        "lea (%%"REG_a", %2, 2), %%"REG_d"      \n\t" // 5*stride
        "lea (%%"REG_d", %2, 2), %%"REG_c"      \n\t" // 7*stride
        "movq (%0), %%mm0                       \n\t" // L0
        "movq (%0, %2), %%mm1                   \n\t" // L1
        "movq (%0, %2, 2), %%mm2                \n\t" // L2
        "movq (%0, %%"REG_a"), %%mm3            \n\t" // L3
        "movq (%1), %%mm4                       \n\t" // R0
        "movq (%1, %2), %%mm5                   \n\t" // R1
        "movq (%1, %2, 2), %%mm6                \n\t" // R2
        "movq (%1, %%"REG_a"), %%mm7            \n\t" // R3
        PAVGB(%%mm4, %%mm0)
        PAVGB(%%mm5, %%mm1)
        PAVGB(%%mm6, %%mm2)
        PAVGB(%%mm7, %%mm3)
        PAVGB(%%mm4, %%mm0)
        PAVGB(%%mm5, %%mm1)
        PAVGB(%%mm6, %%mm2)
        PAVGB(%%mm7, %%mm3)
        PAVGB(%%mm4, %%mm0)
        PAVGB(%%mm5, %%mm1)
        PAVGB(%%mm6, %%mm2)
        PAVGB(%%mm7, %%mm3)
        "movq %%mm0, (%1)                       \n\t" // R0
        "movq %%mm1, (%1, %2)                   \n\t" // R1
        "movq %%mm2, (%1, %2, 2)                \n\t" // R2
        "movq %%mm3, (%1, %%"REG_a")            \n\t" // R3
        "movq %%mm0, (%0)                       \n\t" // L0
        "movq %%mm1, (%0, %2)                   \n\t" // L1
        "movq %%mm2, (%0, %2, 2)                \n\t" // L2
        "movq %%mm3, (%0, %%"REG_a")            \n\t" // L3

        "movq (%0, %2, 4), %%mm0                \n\t" // L4
        "movq (%0, %%"REG_d"), %%mm1            \n\t" // L5
        "movq (%0, %%"REG_a", 2), %%mm2         \n\t" // L6
        "movq (%0, %%"REG_c"), %%mm3            \n\t" // L7
        "movq (%1, %2, 4), %%mm4                \n\t" // R4
        "movq (%1, %%"REG_d"), %%mm5            \n\t" // R5
        "movq (%1, %%"REG_a", 2), %%mm6         \n\t" // R6
        "movq (%1, %%"REG_c"), %%mm7            \n\t" // R7
        PAVGB(%%mm4, %%mm0)
        PAVGB(%%mm5, %%mm1)
        PAVGB(%%mm6, %%mm2)
        PAVGB(%%mm7, %%mm3)
        PAVGB(%%mm4, %%mm0)
        PAVGB(%%mm5, %%mm1)
        PAVGB(%%mm6, %%mm2)
        PAVGB(%%mm7, %%mm3)
        PAVGB(%%mm4, %%mm0)
        PAVGB(%%mm5, %%mm1)
        PAVGB(%%mm6, %%mm2)
        PAVGB(%%mm7, %%mm3)
        "movq %%mm0, (%1, %2, 4)                \n\t" // R4
        "movq %%mm1, (%1, %%"REG_d")            \n\t" // R5
        "movq %%mm2, (%1, %%"REG_a", 2)         \n\t" // R6
        "movq %%mm3, (%1, %%"REG_c")            \n\t" // R7
        "movq %%mm0, (%0, %2, 4)                \n\t" // L4
        "movq %%mm1, (%0, %%"REG_d")            \n\t" // L5
        "movq %%mm2, (%0, %%"REG_a", 2)         \n\t" // L6
        "movq %%mm3, (%0, %%"REG_c")            \n\t" // L7

        "4:                                     \n\t"

        :: "r" (src), "r" (tempBlurred), "r"((x86_reg)stride), "m" (tempBlurredPast)
        : "%"REG_a, "%"REG_d, "%"REG_c, "memory"
    );
#else //HAVE_MMX2 || HAVE_AMD3DNOW
{
    int y;
    int d=0;
//    int sysd=0;
    int i;

    for(y=0; y<8; y++){
        int x;
        for(x=0; x<8; x++){
            int ref= tempBlurred[ x + y*stride ];
            int cur= src[ x + y*stride ];
            int d1=ref - cur;
//            if(x==0 || x==7) d1+= d1>>1;
//            if(y==0 || y==7) d1+= d1>>1;
//            d+= FFABS(d1);
            d+= d1*d1;
//            sysd+= d1;
        }
    }
    i=d;
    d=  (
        4*d
        +(*(tempBlurredPast-256))
        +(*(tempBlurredPast-1))+ (*(tempBlurredPast+1))
        +(*(tempBlurredPast+256))
        +4)>>3;
    *tempBlurredPast=i;
//    ((*tempBlurredPast)*3 + d + 2)>>2;

/*
Switch between
 1  0  0  0  0  0  0  (0)
64 32 16  8  4  2  1  (1)
64 48 36 27 20 15 11 (33) (approx)
64 56 49 43 37 33 29 (200) (approx)
*/
    if(d > maxNoise[1]){
        if(d < maxNoise[2]){
            for(y=0; y<8; y++){
                int x;
                for(x=0; x<8; x++){
                    int ref= tempBlurred[ x + y*stride ];
                    int cur= src[ x + y*stride ];
                    tempBlurred[ x + y*stride ]=
                    src[ x + y*stride ]=
                        (ref + cur + 1)>>1;
                }
            }
        }else{
            for(y=0; y<8; y++){
                int x;
                for(x=0; x<8; x++){
                    tempBlurred[ x + y*stride ]= src[ x + y*stride ];
                }
            }
        }
    }else{
        if(d < maxNoise[0]){
            for(y=0; y<8; y++){
                int x;
                for(x=0; x<8; x++){
                    int ref= tempBlurred[ x + y*stride ];
                    int cur= src[ x + y*stride ];
                    tempBlurred[ x + y*stride ]=
                    src[ x + y*stride ]=
                        (ref*7 + cur + 4)>>3;
                }
            }
        }else{
            for(y=0; y<8; y++){
                int x;
                for(x=0; x<8; x++){
                    int ref= tempBlurred[ x + y*stride ];
                    int cur= src[ x + y*stride ];
                    tempBlurred[ x + y*stride ]=
                    src[ x + y*stride ]=
                        (ref*3 + cur + 2)>>2;
                }
            }
        }
    }
}
#endif //HAVE_MMX2 || HAVE_AMD3DNOW
}
#endif //HAVE_ALTIVEC

#if HAVE_MMX
/**
 * accurate deblock filter
 */
static av_always_inline void RENAME(do_a_deblock)(uint8_t *src, int step, int stride, PPContext *c){
    int64_t dc_mask, eq_mask, both_masks;
    int64_t sums[10*8*2];
    src+= step*3; // src points to begin of the 8x8 Block
    //{ START_TIMER
    __asm__ volatile(
        "movq %0, %%mm7                         \n\t"
        "movq %1, %%mm6                         \n\t"
        : : "m" (c->mmxDcOffset[c->nonBQP]),  "m" (c->mmxDcThreshold[c->nonBQP])
        );

    __asm__ volatile(
        "lea (%2, %3), %%"REG_a"                \n\t"
//      0       1       2       3       4       5       6       7       8       9
//      %1      eax     eax+%2  eax+2%2 %1+4%2  ecx     ecx+%2  ecx+2%2 %1+8%2  ecx+4%2

        "movq (%2), %%mm0                       \n\t"
        "movq (%%"REG_a"), %%mm1                \n\t"
        "movq %%mm1, %%mm3                      \n\t"
        "movq %%mm1, %%mm4                      \n\t"
        "psubb %%mm1, %%mm0                     \n\t" // mm0 = differnece
        "paddb %%mm7, %%mm0                     \n\t"
        "pcmpgtb %%mm6, %%mm0                   \n\t"

        "movq (%%"REG_a",%3), %%mm2             \n\t"
        PMAXUB(%%mm2, %%mm4)
        PMINUB(%%mm2, %%mm3, %%mm5)
        "psubb %%mm2, %%mm1                     \n\t"
        "paddb %%mm7, %%mm1                     \n\t"
        "pcmpgtb %%mm6, %%mm1                   \n\t"
        "paddb %%mm1, %%mm0                     \n\t"

        "movq (%%"REG_a", %3, 2), %%mm1         \n\t"
        PMAXUB(%%mm1, %%mm4)
        PMINUB(%%mm1, %%mm3, %%mm5)
        "psubb %%mm1, %%mm2                     \n\t"
        "paddb %%mm7, %%mm2                     \n\t"
        "pcmpgtb %%mm6, %%mm2                   \n\t"
        "paddb %%mm2, %%mm0                     \n\t"

        "lea (%%"REG_a", %3, 4), %%"REG_a"      \n\t"

        "movq (%2, %3, 4), %%mm2                \n\t"
        PMAXUB(%%mm2, %%mm4)
        PMINUB(%%mm2, %%mm3, %%mm5)
        "psubb %%mm2, %%mm1                     \n\t"
        "paddb %%mm7, %%mm1                     \n\t"
        "pcmpgtb %%mm6, %%mm1                   \n\t"
        "paddb %%mm1, %%mm0                     \n\t"

        "movq (%%"REG_a"), %%mm1                \n\t"
        PMAXUB(%%mm1, %%mm4)
        PMINUB(%%mm1, %%mm3, %%mm5)
        "psubb %%mm1, %%mm2                     \n\t"
        "paddb %%mm7, %%mm2                     \n\t"
        "pcmpgtb %%mm6, %%mm2                   \n\t"
        "paddb %%mm2, %%mm0                     \n\t"

        "movq (%%"REG_a", %3), %%mm2            \n\t"
        PMAXUB(%%mm2, %%mm4)
        PMINUB(%%mm2, %%mm3, %%mm5)
        "psubb %%mm2, %%mm1                     \n\t"
        "paddb %%mm7, %%mm1                     \n\t"
        "pcmpgtb %%mm6, %%mm1                   \n\t"
        "paddb %%mm1, %%mm0                     \n\t"

        "movq (%%"REG_a", %3, 2), %%mm1         \n\t"
        PMAXUB(%%mm1, %%mm4)
        PMINUB(%%mm1, %%mm3, %%mm5)
        "psubb %%mm1, %%mm2                     \n\t"
        "paddb %%mm7, %%mm2                     \n\t"
        "pcmpgtb %%mm6, %%mm2                   \n\t"
        "paddb %%mm2, %%mm0                     \n\t"

        "movq (%2, %3, 8), %%mm2                \n\t"
        PMAXUB(%%mm2, %%mm4)
        PMINUB(%%mm2, %%mm3, %%mm5)
        "psubb %%mm2, %%mm1                     \n\t"
        "paddb %%mm7, %%mm1                     \n\t"
        "pcmpgtb %%mm6, %%mm1                   \n\t"
        "paddb %%mm1, %%mm0                     \n\t"

        "movq (%%"REG_a", %3, 4), %%mm1         \n\t"
        "psubb %%mm1, %%mm2                     \n\t"
        "paddb %%mm7, %%mm2                     \n\t"
        "pcmpgtb %%mm6, %%mm2                   \n\t"
        "paddb %%mm2, %%mm0                     \n\t"
        "psubusb %%mm3, %%mm4                   \n\t"

        "pxor %%mm6, %%mm6                      \n\t"
        "movq %4, %%mm7                         \n\t" // QP,..., QP
        "paddusb %%mm7, %%mm7                   \n\t" // 2QP ... 2QP
        "psubusb %%mm4, %%mm7                   \n\t" // Diff >=2QP -> 0
        "pcmpeqb %%mm6, %%mm7                   \n\t" // Diff < 2QP -> 0
        "pcmpeqb %%mm6, %%mm7                   \n\t" // Diff < 2QP -> 0
        "movq %%mm7, %1                         \n\t"

        "movq %5, %%mm7                         \n\t"
        "punpcklbw %%mm7, %%mm7                 \n\t"
        "punpcklbw %%mm7, %%mm7                 \n\t"
        "punpcklbw %%mm7, %%mm7                 \n\t"
        "psubb %%mm0, %%mm6                     \n\t"
        "pcmpgtb %%mm7, %%mm6                   \n\t"
        "movq %%mm6, %0                         \n\t"

        : "=m" (eq_mask), "=m" (dc_mask)
        : "r" (src), "r" ((x86_reg)step), "m" (c->pQPb), "m"(c->ppMode.flatnessThreshold)
        : "%"REG_a
    );

    both_masks = dc_mask & eq_mask;

    if(both_masks){
        x86_reg offset= -8*step;
        int64_t *temp_sums= sums;

        __asm__ volatile(
            "movq %2, %%mm0                         \n\t"  // QP,..., QP
            "pxor %%mm4, %%mm4                      \n\t"

            "movq (%0), %%mm6                       \n\t"
            "movq (%0, %1), %%mm5                   \n\t"
            "movq %%mm5, %%mm1                      \n\t"
            "movq %%mm6, %%mm2                      \n\t"
            "psubusb %%mm6, %%mm5                   \n\t"
            "psubusb %%mm1, %%mm2                   \n\t"
            "por %%mm5, %%mm2                       \n\t" // ABS Diff of lines
            "psubusb %%mm2, %%mm0                   \n\t" // diff >= QP -> 0
            "pcmpeqb %%mm4, %%mm0                   \n\t" // diff >= QP -> FF

            "pxor %%mm6, %%mm1                      \n\t"
            "pand %%mm0, %%mm1                      \n\t"
            "pxor %%mm1, %%mm6                      \n\t"
            // 0:QP  6:First

            "movq (%0, %1, 8), %%mm5                \n\t"
            "add %1, %0                             \n\t" // %0 points to line 1 not 0
            "movq (%0, %1, 8), %%mm7                \n\t"
            "movq %%mm5, %%mm1                      \n\t"
            "movq %%mm7, %%mm2                      \n\t"
            "psubusb %%mm7, %%mm5                   \n\t"
            "psubusb %%mm1, %%mm2                   \n\t"
            "por %%mm5, %%mm2                       \n\t" // ABS Diff of lines
            "movq %2, %%mm0                         \n\t"  // QP,..., QP
            "psubusb %%mm2, %%mm0                   \n\t" // diff >= QP -> 0
            "pcmpeqb %%mm4, %%mm0                   \n\t" // diff >= QP -> FF

            "pxor %%mm7, %%mm1                      \n\t"
            "pand %%mm0, %%mm1                      \n\t"
            "pxor %%mm1, %%mm7                      \n\t"

            "movq %%mm6, %%mm5                      \n\t"
            "punpckhbw %%mm4, %%mm6                 \n\t"
            "punpcklbw %%mm4, %%mm5                 \n\t"
            // 4:0 5/6:First 7:Last

            "movq %%mm5, %%mm0                      \n\t"
            "movq %%mm6, %%mm1                      \n\t"
            "psllw $2, %%mm0                        \n\t"
            "psllw $2, %%mm1                        \n\t"
            "paddw "MANGLE(w04)", %%mm0             \n\t"
            "paddw "MANGLE(w04)", %%mm1             \n\t"

#define NEXT\
            "movq (%0), %%mm2                       \n\t"\
            "movq (%0), %%mm3                       \n\t"\
            "add %1, %0                             \n\t"\
            "punpcklbw %%mm4, %%mm2                 \n\t"\
            "punpckhbw %%mm4, %%mm3                 \n\t"\
            "paddw %%mm2, %%mm0                     \n\t"\
            "paddw %%mm3, %%mm1                     \n\t"

#define PREV\
            "movq (%0), %%mm2                       \n\t"\
            "movq (%0), %%mm3                       \n\t"\
            "add %1, %0                             \n\t"\
            "punpcklbw %%mm4, %%mm2                 \n\t"\
            "punpckhbw %%mm4, %%mm3                 \n\t"\
            "psubw %%mm2, %%mm0                     \n\t"\
            "psubw %%mm3, %%mm1                     \n\t"


            NEXT //0
            NEXT //1
            NEXT //2
            "movq %%mm0, (%3)                       \n\t"
            "movq %%mm1, 8(%3)                      \n\t"

            NEXT //3
            "psubw %%mm5, %%mm0                     \n\t"
            "psubw %%mm6, %%mm1                     \n\t"
            "movq %%mm0, 16(%3)                     \n\t"
            "movq %%mm1, 24(%3)                     \n\t"

            NEXT //4
            "psubw %%mm5, %%mm0                     \n\t"
            "psubw %%mm6, %%mm1                     \n\t"
            "movq %%mm0, 32(%3)                     \n\t"
            "movq %%mm1, 40(%3)                     \n\t"

            NEXT //5
            "psubw %%mm5, %%mm0                     \n\t"
            "psubw %%mm6, %%mm1                     \n\t"
            "movq %%mm0, 48(%3)                     \n\t"
            "movq %%mm1, 56(%3)                     \n\t"

            NEXT //6
            "psubw %%mm5, %%mm0                     \n\t"
            "psubw %%mm6, %%mm1                     \n\t"
            "movq %%mm0, 64(%3)                     \n\t"
            "movq %%mm1, 72(%3)                     \n\t"

            "movq %%mm7, %%mm6                      \n\t"
            "punpckhbw %%mm4, %%mm7                 \n\t"
            "punpcklbw %%mm4, %%mm6                 \n\t"

            NEXT //7
            "mov %4, %0                             \n\t"
            "add %1, %0                             \n\t"
            PREV //0
            "movq %%mm0, 80(%3)                     \n\t"
            "movq %%mm1, 88(%3)                     \n\t"

            PREV //1
            "paddw %%mm6, %%mm0                     \n\t"
            "paddw %%mm7, %%mm1                     \n\t"
            "movq %%mm0, 96(%3)                     \n\t"
            "movq %%mm1, 104(%3)                    \n\t"

            PREV //2
            "paddw %%mm6, %%mm0                     \n\t"
            "paddw %%mm7, %%mm1                     \n\t"
            "movq %%mm0, 112(%3)                    \n\t"
            "movq %%mm1, 120(%3)                    \n\t"

            PREV //3
            "paddw %%mm6, %%mm0                     \n\t"
            "paddw %%mm7, %%mm1                     \n\t"
            "movq %%mm0, 128(%3)                    \n\t"
            "movq %%mm1, 136(%3)                    \n\t"

            PREV //4
            "paddw %%mm6, %%mm0                     \n\t"
            "paddw %%mm7, %%mm1                     \n\t"
            "movq %%mm0, 144(%3)                    \n\t"
            "movq %%mm1, 152(%3)                    \n\t"

            "mov %4, %0                             \n\t" //FIXME

            : "+&r"(src)
            : "r" ((x86_reg)step), "m" (c->pQPb), "r"(sums), "g"(src)
        );

        src+= step; // src points to begin of the 8x8 Block

        __asm__ volatile(
            "movq %4, %%mm6                         \n\t"
            "pcmpeqb %%mm5, %%mm5                   \n\t"
            "pxor %%mm6, %%mm5                      \n\t"
            "pxor %%mm7, %%mm7                      \n\t"

            "1:                                     \n\t"
            "movq (%1), %%mm0                       \n\t"
            "movq 8(%1), %%mm1                      \n\t"
            "paddw 32(%1), %%mm0                    \n\t"
            "paddw 40(%1), %%mm1                    \n\t"
            "movq (%0, %3), %%mm2                   \n\t"
            "movq %%mm2, %%mm3                      \n\t"
            "movq %%mm2, %%mm4                      \n\t"
            "punpcklbw %%mm7, %%mm2                 \n\t"
            "punpckhbw %%mm7, %%mm3                 \n\t"
            "paddw %%mm2, %%mm0                     \n\t"
            "paddw %%mm3, %%mm1                     \n\t"
            "paddw %%mm2, %%mm0                     \n\t"
            "paddw %%mm3, %%mm1                     \n\t"
            "psrlw $4, %%mm0                        \n\t"
            "psrlw $4, %%mm1                        \n\t"
            "packuswb %%mm1, %%mm0                  \n\t"
            "pand %%mm6, %%mm0                      \n\t"
            "pand %%mm5, %%mm4                      \n\t"
            "por %%mm4, %%mm0                       \n\t"
            "movq %%mm0, (%0, %3)                   \n\t"
            "add $16, %1                            \n\t"
            "add %2, %0                             \n\t"
            " js 1b                                 \n\t"

            : "+r"(offset), "+r"(temp_sums)
            : "r" ((x86_reg)step), "r"(src - offset), "m"(both_masks)
        );
    }else
        src+= step; // src points to begin of the 8x8 Block

    if(eq_mask != -1LL){
        uint8_t *temp_src= src;
        __asm__ volatile(
            "pxor %%mm7, %%mm7                      \n\t"
            "lea -40(%%"REG_SP"), %%"REG_c"         \n\t" // make space for 4 8-byte vars
            "and "ALIGN_MASK", %%"REG_c"            \n\t" // align
//      0       1       2       3       4       5       6       7       8       9
//      %0      eax     eax+%1  eax+2%1 %0+4%1  ecx     ecx+%1  ecx+2%1 %1+8%1  ecx+4%1

            "movq (%0), %%mm0                       \n\t"
            "movq %%mm0, %%mm1                      \n\t"
            "punpcklbw %%mm7, %%mm0                 \n\t" // low part of line 0
            "punpckhbw %%mm7, %%mm1                 \n\t" // high part of line 0

            "movq (%0, %1), %%mm2                   \n\t"
            "lea (%0, %1, 2), %%"REG_a"             \n\t"
            "movq %%mm2, %%mm3                      \n\t"
            "punpcklbw %%mm7, %%mm2                 \n\t" // low part of line 1
            "punpckhbw %%mm7, %%mm3                 \n\t" // high part of line 1

            "movq (%%"REG_a"), %%mm4                \n\t"
            "movq %%mm4, %%mm5                      \n\t"
            "punpcklbw %%mm7, %%mm4                 \n\t" // low part of line 2
            "punpckhbw %%mm7, %%mm5                 \n\t" // high part of line 2

            "paddw %%mm0, %%mm0                     \n\t" // 2L0
            "paddw %%mm1, %%mm1                     \n\t" // 2H0
            "psubw %%mm4, %%mm2                     \n\t" // L1 - L2
            "psubw %%mm5, %%mm3                     \n\t" // H1 - H2
            "psubw %%mm2, %%mm0                     \n\t" // 2L0 - L1 + L2
            "psubw %%mm3, %%mm1                     \n\t" // 2H0 - H1 + H2

            "psllw $2, %%mm2                        \n\t" // 4L1 - 4L2
            "psllw $2, %%mm3                        \n\t" // 4H1 - 4H2
            "psubw %%mm2, %%mm0                     \n\t" // 2L0 - 5L1 + 5L2
            "psubw %%mm3, %%mm1                     \n\t" // 2H0 - 5H1 + 5H2

            "movq (%%"REG_a", %1), %%mm2            \n\t"
            "movq %%mm2, %%mm3                      \n\t"
            "punpcklbw %%mm7, %%mm2                 \n\t" // L3
            "punpckhbw %%mm7, %%mm3                 \n\t" // H3

            "psubw %%mm2, %%mm0                     \n\t" // 2L0 - 5L1 + 5L2 - L3
            "psubw %%mm3, %%mm1                     \n\t" // 2H0 - 5H1 + 5H2 - H3
            "psubw %%mm2, %%mm0                     \n\t" // 2L0 - 5L1 + 5L2 - 2L3
            "psubw %%mm3, %%mm1                     \n\t" // 2H0 - 5H1 + 5H2 - 2H3
            "movq %%mm0, (%%"REG_c")                \n\t" // 2L0 - 5L1 + 5L2 - 2L3
            "movq %%mm1, 8(%%"REG_c")               \n\t" // 2H0 - 5H1 + 5H2 - 2H3

            "movq (%%"REG_a", %1, 2), %%mm0         \n\t"
            "movq %%mm0, %%mm1                      \n\t"
            "punpcklbw %%mm7, %%mm0                 \n\t" // L4
            "punpckhbw %%mm7, %%mm1                 \n\t" // H4

            "psubw %%mm0, %%mm2                     \n\t" // L3 - L4
            "psubw %%mm1, %%mm3                     \n\t" // H3 - H4
            "movq %%mm2, 16(%%"REG_c")              \n\t" // L3 - L4
            "movq %%mm3, 24(%%"REG_c")              \n\t" // H3 - H4
            "paddw %%mm4, %%mm4                     \n\t" // 2L2
            "paddw %%mm5, %%mm5                     \n\t" // 2H2
            "psubw %%mm2, %%mm4                     \n\t" // 2L2 - L3 + L4
            "psubw %%mm3, %%mm5                     \n\t" // 2H2 - H3 + H4

            "lea (%%"REG_a", %1), %0                \n\t"
            "psllw $2, %%mm2                        \n\t" // 4L3 - 4L4
            "psllw $2, %%mm3                        \n\t" // 4H3 - 4H4
            "psubw %%mm2, %%mm4                     \n\t" // 2L2 - 5L3 + 5L4
            "psubw %%mm3, %%mm5                     \n\t" // 2H2 - 5H3 + 5H4
//50 opcodes so far
            "movq (%0, %1, 2), %%mm2                \n\t"
            "movq %%mm2, %%mm3                      \n\t"
            "punpcklbw %%mm7, %%mm2                 \n\t" // L5
            "punpckhbw %%mm7, %%mm3                 \n\t" // H5
            "psubw %%mm2, %%mm4                     \n\t" // 2L2 - 5L3 + 5L4 - L5
            "psubw %%mm3, %%mm5                     \n\t" // 2H2 - 5H3 + 5H4 - H5
            "psubw %%mm2, %%mm4                     \n\t" // 2L2 - 5L3 + 5L4 - 2L5
            "psubw %%mm3, %%mm5                     \n\t" // 2H2 - 5H3 + 5H4 - 2H5

            "movq (%%"REG_a", %1, 4), %%mm6         \n\t"
            "punpcklbw %%mm7, %%mm6                 \n\t" // L6
            "psubw %%mm6, %%mm2                     \n\t" // L5 - L6
            "movq (%%"REG_a", %1, 4), %%mm6         \n\t"
            "punpckhbw %%mm7, %%mm6                 \n\t" // H6
            "psubw %%mm6, %%mm3                     \n\t" // H5 - H6

            "paddw %%mm0, %%mm0                     \n\t" // 2L4
            "paddw %%mm1, %%mm1                     \n\t" // 2H4
            "psubw %%mm2, %%mm0                     \n\t" // 2L4 - L5 + L6
            "psubw %%mm3, %%mm1                     \n\t" // 2H4 - H5 + H6

            "psllw $2, %%mm2                        \n\t" // 4L5 - 4L6
            "psllw $2, %%mm3                        \n\t" // 4H5 - 4H6
            "psubw %%mm2, %%mm0                     \n\t" // 2L4 - 5L5 + 5L6
            "psubw %%mm3, %%mm1                     \n\t" // 2H4 - 5H5 + 5H6

            "movq (%0, %1, 4), %%mm2                \n\t"
            "movq %%mm2, %%mm3                      \n\t"
            "punpcklbw %%mm7, %%mm2                 \n\t" // L7
            "punpckhbw %%mm7, %%mm3                 \n\t" // H7

            "paddw %%mm2, %%mm2                     \n\t" // 2L7
            "paddw %%mm3, %%mm3                     \n\t" // 2H7
            "psubw %%mm2, %%mm0                     \n\t" // 2L4 - 5L5 + 5L6 - 2L7
            "psubw %%mm3, %%mm1                     \n\t" // 2H4 - 5H5 + 5H6 - 2H7

            "movq (%%"REG_c"), %%mm2                \n\t" // 2L0 - 5L1 + 5L2 - 2L3
            "movq 8(%%"REG_c"), %%mm3               \n\t" // 2H0 - 5H1 + 5H2 - 2H3

#if HAVE_MMX2
            "movq %%mm7, %%mm6                      \n\t" // 0
            "psubw %%mm0, %%mm6                     \n\t"
            "pmaxsw %%mm6, %%mm0                    \n\t" // |2L4 - 5L5 + 5L6 - 2L7|
            "movq %%mm7, %%mm6                      \n\t" // 0
            "psubw %%mm1, %%mm6                     \n\t"
            "pmaxsw %%mm6, %%mm1                    \n\t" // |2H4 - 5H5 + 5H6 - 2H7|
            "movq %%mm7, %%mm6                      \n\t" // 0
            "psubw %%mm2, %%mm6                     \n\t"
            "pmaxsw %%mm6, %%mm2                    \n\t" // |2L0 - 5L1 + 5L2 - 2L3|
            "movq %%mm7, %%mm6                      \n\t" // 0
            "psubw %%mm3, %%mm6                     \n\t"
            "pmaxsw %%mm6, %%mm3                    \n\t" // |2H0 - 5H1 + 5H2 - 2H3|
#else
            "movq %%mm7, %%mm6                      \n\t" // 0
            "pcmpgtw %%mm0, %%mm6                   \n\t"
            "pxor %%mm6, %%mm0                      \n\t"
            "psubw %%mm6, %%mm0                     \n\t" // |2L4 - 5L5 + 5L6 - 2L7|
            "movq %%mm7, %%mm6                      \n\t" // 0
            "pcmpgtw %%mm1, %%mm6                   \n\t"
            "pxor %%mm6, %%mm1                      \n\t"
            "psubw %%mm6, %%mm1                     \n\t" // |2H4 - 5H5 + 5H6 - 2H7|
            "movq %%mm7, %%mm6                      \n\t" // 0
            "pcmpgtw %%mm2, %%mm6                   \n\t"
            "pxor %%mm6, %%mm2                      \n\t"
            "psubw %%mm6, %%mm2                     \n\t" // |2L0 - 5L1 + 5L2 - 2L3|
            "movq %%mm7, %%mm6                      \n\t" // 0
            "pcmpgtw %%mm3, %%mm6                   \n\t"
            "pxor %%mm6, %%mm3                      \n\t"
            "psubw %%mm6, %%mm3                     \n\t" // |2H0 - 5H1 + 5H2 - 2H3|
#endif

#if HAVE_MMX2
            "pminsw %%mm2, %%mm0                    \n\t"
            "pminsw %%mm3, %%mm1                    \n\t"
#else
            "movq %%mm0, %%mm6                      \n\t"
            "psubusw %%mm2, %%mm6                   \n\t"
            "psubw %%mm6, %%mm0                     \n\t"
            "movq %%mm1, %%mm6                      \n\t"
            "psubusw %%mm3, %%mm6                   \n\t"
            "psubw %%mm6, %%mm1                     \n\t"
#endif

            "movd %2, %%mm2                         \n\t" // QP
            "punpcklbw %%mm7, %%mm2                 \n\t"

            "movq %%mm7, %%mm6                      \n\t" // 0
            "pcmpgtw %%mm4, %%mm6                   \n\t" // sign(2L2 - 5L3 + 5L4 - 2L5)
            "pxor %%mm6, %%mm4                      \n\t"
            "psubw %%mm6, %%mm4                     \n\t" // |2L2 - 5L3 + 5L4 - 2L5|
            "pcmpgtw %%mm5, %%mm7                   \n\t" // sign(2H2 - 5H3 + 5H4 - 2H5)
            "pxor %%mm7, %%mm5                      \n\t"
            "psubw %%mm7, %%mm5                     \n\t" // |2H2 - 5H3 + 5H4 - 2H5|
// 100 opcodes
            "psllw $3, %%mm2                        \n\t" // 8QP
            "movq %%mm2, %%mm3                      \n\t" // 8QP
            "pcmpgtw %%mm4, %%mm2                   \n\t"
            "pcmpgtw %%mm5, %%mm3                   \n\t"
            "pand %%mm2, %%mm4                      \n\t"
            "pand %%mm3, %%mm5                      \n\t"


            "psubusw %%mm0, %%mm4                   \n\t" // hd
            "psubusw %%mm1, %%mm5                   \n\t" // ld


            "movq "MANGLE(w05)", %%mm2              \n\t" // 5
            "pmullw %%mm2, %%mm4                    \n\t"
            "pmullw %%mm2, %%mm5                    \n\t"
            "movq "MANGLE(w20)", %%mm2              \n\t" // 32
            "paddw %%mm2, %%mm4                     \n\t"
            "paddw %%mm2, %%mm5                     \n\t"
            "psrlw $6, %%mm4                        \n\t"
            "psrlw $6, %%mm5                        \n\t"

            "movq 16(%%"REG_c"), %%mm0              \n\t" // L3 - L4
            "movq 24(%%"REG_c"), %%mm1              \n\t" // H3 - H4

            "pxor %%mm2, %%mm2                      \n\t"
            "pxor %%mm3, %%mm3                      \n\t"

            "pcmpgtw %%mm0, %%mm2                   \n\t" // sign (L3-L4)
            "pcmpgtw %%mm1, %%mm3                   \n\t" // sign (H3-H4)
            "pxor %%mm2, %%mm0                      \n\t"
            "pxor %%mm3, %%mm1                      \n\t"
            "psubw %%mm2, %%mm0                     \n\t" // |L3-L4|
            "psubw %%mm3, %%mm1                     \n\t" // |H3-H4|
            "psrlw $1, %%mm0                        \n\t" // |L3 - L4|/2
            "psrlw $1, %%mm1                        \n\t" // |H3 - H4|/2

            "pxor %%mm6, %%mm2                      \n\t"
            "pxor %%mm7, %%mm3                      \n\t"
            "pand %%mm2, %%mm4                      \n\t"
            "pand %%mm3, %%mm5                      \n\t"

#if HAVE_MMX2
            "pminsw %%mm0, %%mm4                    \n\t"
            "pminsw %%mm1, %%mm5                    \n\t"
#else
            "movq %%mm4, %%mm2                      \n\t"
            "psubusw %%mm0, %%mm2                   \n\t"
            "psubw %%mm2, %%mm4                     \n\t"
            "movq %%mm5, %%mm2                      \n\t"
            "psubusw %%mm1, %%mm2                   \n\t"
            "psubw %%mm2, %%mm5                     \n\t"
#endif
            "pxor %%mm6, %%mm4                      \n\t"
            "pxor %%mm7, %%mm5                      \n\t"
            "psubw %%mm6, %%mm4                     \n\t"
            "psubw %%mm7, %%mm5                     \n\t"
            "packsswb %%mm5, %%mm4                  \n\t"
            "movq %3, %%mm1                         \n\t"
            "pandn %%mm4, %%mm1                     \n\t"
            "movq (%0), %%mm0                       \n\t"
            "paddb   %%mm1, %%mm0                   \n\t"
            "movq %%mm0, (%0)                       \n\t"
            "movq (%0, %1), %%mm0                   \n\t"
            "psubb %%mm1, %%mm0                     \n\t"
            "movq %%mm0, (%0, %1)                   \n\t"

            : "+r" (temp_src)
            : "r" ((x86_reg)step), "m" (c->pQPb), "m"(eq_mask)
            : "%"REG_a, "%"REG_c
        );
    }
/*if(step==16){
    STOP_TIMER("step16")
}else{
    STOP_TIMER("stepX")
}
    } */
}
#endif //HAVE_MMX

static void RENAME(postProcess)(const uint8_t src[], int srcStride, uint8_t dst[], int dstStride, int width, int height,
                                const QP_STORE_T QPs[], int QPStride, int isColor, PPContext *c);

/**
 * Copy a block from src to dst and fixes the blacklevel.
 * levelFix == 0 -> do not touch the brighness & contrast
 */
#undef REAL_SCALED_CPY
#undef SCALED_CPY

static inline void RENAME(blockCopy)(uint8_t dst[], int dstStride, const uint8_t src[], int srcStride,
                                     int levelFix, int64_t *packedOffsetAndScale)
{
#if !HAVE_MMX
    int i;
#endif
    if(levelFix){
#if HAVE_MMX
    __asm__ volatile(
        "movq (%%"REG_a"), %%mm2        \n\t" // packedYOffset
        "movq 8(%%"REG_a"), %%mm3       \n\t" // packedYScale
        "lea (%2,%4), %%"REG_a"         \n\t"
        "lea (%3,%5), %%"REG_d"         \n\t"
        "pxor %%mm4, %%mm4              \n\t"
#if HAVE_MMX2
#define REAL_SCALED_CPY(src1, src2, dst1, dst2)                                                \
        "movq " #src1 ", %%mm0          \n\t"\
        "movq " #src1 ", %%mm5          \n\t"\
        "movq " #src2 ", %%mm1          \n\t"\
        "movq " #src2 ", %%mm6          \n\t"\
        "punpcklbw %%mm0, %%mm0         \n\t"\
        "punpckhbw %%mm5, %%mm5         \n\t"\
        "punpcklbw %%mm1, %%mm1         \n\t"\
        "punpckhbw %%mm6, %%mm6         \n\t"\
        "pmulhuw %%mm3, %%mm0           \n\t"\
        "pmulhuw %%mm3, %%mm5           \n\t"\
        "pmulhuw %%mm3, %%mm1           \n\t"\
        "pmulhuw %%mm3, %%mm6           \n\t"\
        "psubw %%mm2, %%mm0             \n\t"\
        "psubw %%mm2, %%mm5             \n\t"\
        "psubw %%mm2, %%mm1             \n\t"\
        "psubw %%mm2, %%mm6             \n\t"\
        "packuswb %%mm5, %%mm0          \n\t"\
        "packuswb %%mm6, %%mm1          \n\t"\
        "movq %%mm0, " #dst1 "          \n\t"\
        "movq %%mm1, " #dst2 "          \n\t"\

#else //HAVE_MMX2
#define REAL_SCALED_CPY(src1, src2, dst1, dst2)                                        \
        "movq " #src1 ", %%mm0          \n\t"\
        "movq " #src1 ", %%mm5          \n\t"\
        "punpcklbw %%mm4, %%mm0         \n\t"\
        "punpckhbw %%mm4, %%mm5         \n\t"\
        "psubw %%mm2, %%mm0             \n\t"\
        "psubw %%mm2, %%mm5             \n\t"\
        "movq " #src2 ", %%mm1          \n\t"\
        "psllw $6, %%mm0                \n\t"\
        "psllw $6, %%mm5                \n\t"\
        "pmulhw %%mm3, %%mm0            \n\t"\
        "movq " #src2 ", %%mm6          \n\t"\
        "pmulhw %%mm3, %%mm5            \n\t"\
        "punpcklbw %%mm4, %%mm1         \n\t"\
        "punpckhbw %%mm4, %%mm6         \n\t"\
        "psubw %%mm2, %%mm1             \n\t"\
        "psubw %%mm2, %%mm6             \n\t"\
        "psllw $6, %%mm1                \n\t"\
        "psllw $6, %%mm6                \n\t"\
        "pmulhw %%mm3, %%mm1            \n\t"\
        "pmulhw %%mm3, %%mm6            \n\t"\
        "packuswb %%mm5, %%mm0          \n\t"\
        "packuswb %%mm6, %%mm1          \n\t"\
        "movq %%mm0, " #dst1 "          \n\t"\
        "movq %%mm1, " #dst2 "          \n\t"\

#endif //HAVE_MMX2
#define SCALED_CPY(src1, src2, dst1, dst2)\
   REAL_SCALED_CPY(src1, src2, dst1, dst2)

SCALED_CPY((%2)       , (%2, %4)      , (%3)       , (%3, %5))
SCALED_CPY((%2, %4, 2), (%%REGa, %4, 2), (%3, %5, 2), (%%REGd, %5, 2))
SCALED_CPY((%2, %4, 4), (%%REGa, %4, 4), (%3, %5, 4), (%%REGd, %5, 4))
        "lea (%%"REG_a",%4,4), %%"REG_a"        \n\t"
        "lea (%%"REG_d",%5,4), %%"REG_d"        \n\t"
SCALED_CPY((%%REGa, %4), (%%REGa, %4, 2), (%%REGd, %5), (%%REGd, %5, 2))


        : "=&a" (packedOffsetAndScale)
        : "0" (packedOffsetAndScale),
        "r"(src),
        "r"(dst),
        "r" ((x86_reg)srcStride),
        "r" ((x86_reg)dstStride)
        : "%"REG_d
    );
#else //HAVE_MMX
    for(i=0; i<8; i++)
        memcpy( &(dst[dstStride*i]),
                &(src[srcStride*i]), BLOCK_SIZE);
#endif //HAVE_MMX
    }else{
#if HAVE_MMX
    __asm__ volatile(
        "lea (%0,%2), %%"REG_a"                 \n\t"
        "lea (%1,%3), %%"REG_d"                 \n\t"

#define REAL_SIMPLE_CPY(src1, src2, dst1, dst2)                              \
        "movq " #src1 ", %%mm0          \n\t"\
        "movq " #src2 ", %%mm1          \n\t"\
        "movq %%mm0, " #dst1 "          \n\t"\
        "movq %%mm1, " #dst2 "          \n\t"\

#define SIMPLE_CPY(src1, src2, dst1, dst2)\
   REAL_SIMPLE_CPY(src1, src2, dst1, dst2)

SIMPLE_CPY((%0)       , (%0, %2)       , (%1)       , (%1, %3))
SIMPLE_CPY((%0, %2, 2), (%%REGa, %2, 2), (%1, %3, 2), (%%REGd, %3, 2))
SIMPLE_CPY((%0, %2, 4), (%%REGa, %2, 4), (%1, %3, 4), (%%REGd, %3, 4))
        "lea (%%"REG_a",%2,4), %%"REG_a"        \n\t"
        "lea (%%"REG_d",%3,4), %%"REG_d"        \n\t"
SIMPLE_CPY((%%REGa, %2), (%%REGa, %2, 2), (%%REGd, %3), (%%REGd, %3, 2))

        : : "r" (src),
        "r" (dst),
        "r" ((x86_reg)srcStride),
        "r" ((x86_reg)dstStride)
        : "%"REG_a, "%"REG_d
    );
#else //HAVE_MMX
    for(i=0; i<8; i++)
        memcpy( &(dst[dstStride*i]),
                &(src[srcStride*i]), BLOCK_SIZE);
#endif //HAVE_MMX
    }
}

/**
 * Duplicate the given 8 src pixels ? times upward
 */
static inline void RENAME(duplicate)(uint8_t src[], int stride)
{
#if HAVE_MMX
    __asm__ volatile(
        "movq (%0), %%mm0               \n\t"
        "add %1, %0                     \n\t"
        "movq %%mm0, (%0)               \n\t"
        "movq %%mm0, (%0, %1)           \n\t"
        "movq %%mm0, (%0, %1, 2)        \n\t"
        : "+r" (src)
        : "r" ((x86_reg)-stride)
    );
#else
    int i;
    uint8_t *p=src;
    for(i=0; i<3; i++){
        p-= stride;
        memcpy(p, src, 8);
    }
#endif
}

/**
 * Filter array of bytes (Y or U or V values)
 */
static void RENAME(postProcess)(const uint8_t src[], int srcStride, uint8_t dst[], int dstStride, int width, int height,
                                const QP_STORE_T QPs[], int QPStride, int isColor, PPContext *c2)
{
    DECLARE_ALIGNED(8, PPContext, c)= *c2; //copy to stack for faster access
    int x,y;
#ifdef COMPILE_TIME_MODE
    const int mode= COMPILE_TIME_MODE;
#else
    const int mode= isColor ? c.ppMode.chromMode : c.ppMode.lumMode;
#endif
    int black=0, white=255; // blackest black and whitest white in the picture
    int QPCorrecture= 256*256;

    int copyAhead;
#if HAVE_MMX
    int i;
#endif

    const int qpHShift= isColor ? 4-c.hChromaSubSample : 4;
    const int qpVShift= isColor ? 4-c.vChromaSubSample : 4;

    //FIXME remove
    uint64_t * const yHistogram= c.yHistogram;
    uint8_t * const tempSrc= srcStride > 0 ? c.tempSrc : c.tempSrc - 23*srcStride;
    uint8_t * const tempDst= dstStride > 0 ? c.tempDst : c.tempDst - 23*dstStride;
    //const int mbWidth= isColor ? (width+7)>>3 : (width+15)>>4;

#if HAVE_MMX
    for(i=0; i<57; i++){
        int offset= ((i*c.ppMode.baseDcDiff)>>8) + 1;
        int threshold= offset*2 + 1;
        c.mmxDcOffset[i]= 0x7F - offset;
        c.mmxDcThreshold[i]= 0x7F - threshold;
        c.mmxDcOffset[i]*= 0x0101010101010101LL;
        c.mmxDcThreshold[i]*= 0x0101010101010101LL;
    }
#endif

    if(mode & CUBIC_IPOL_DEINT_FILTER) copyAhead=16;
    else if(   (mode & LINEAR_BLEND_DEINT_FILTER)
            || (mode & FFMPEG_DEINT_FILTER)
            || (mode & LOWPASS5_DEINT_FILTER)) copyAhead=14;
    else if(   (mode & V_DEBLOCK)
            || (mode & LINEAR_IPOL_DEINT_FILTER)
            || (mode & MEDIAN_DEINT_FILTER)
            || (mode & V_A_DEBLOCK)) copyAhead=13;
    else if(mode & V_X1_FILTER) copyAhead=11;
//    else if(mode & V_RK1_FILTER) copyAhead=10;
    else if(mode & DERING) copyAhead=9;
    else copyAhead=8;

    copyAhead-= 8;

    if(!isColor){
        uint64_t sum= 0;
        int i;
        uint64_t maxClipped;
        uint64_t clipped;
        double scale;

        c.frameNum++;
        // first frame is fscked so we ignore it
        if(c.frameNum == 1) yHistogram[0]= width*height/64*15/256;

        for(i=0; i<256; i++){
            sum+= yHistogram[i];
        }

        /* We always get a completely black picture first. */
        maxClipped= (uint64_t)(sum * c.ppMode.maxClippedThreshold);

        clipped= sum;
        for(black=255; black>0; black--){
            if(clipped < maxClipped) break;
            clipped-= yHistogram[black];
        }

        clipped= sum;
        for(white=0; white<256; white++){
            if(clipped < maxClipped) break;
            clipped-= yHistogram[white];
        }

        scale= (double)(c.ppMode.maxAllowedY - c.ppMode.minAllowedY) / (double)(white-black);

#if HAVE_MMX2
        c.packedYScale= (uint16_t)(scale*256.0 + 0.5);
        c.packedYOffset= (((black*c.packedYScale)>>8) - c.ppMode.minAllowedY) & 0xFFFF;
#else
        c.packedYScale= (uint16_t)(scale*1024.0 + 0.5);
        c.packedYOffset= (black - c.ppMode.minAllowedY) & 0xFFFF;
#endif

        c.packedYOffset|= c.packedYOffset<<32;
        c.packedYOffset|= c.packedYOffset<<16;

        c.packedYScale|= c.packedYScale<<32;
        c.packedYScale|= c.packedYScale<<16;

        if(mode & LEVEL_FIX)        QPCorrecture= (int)(scale*256*256 + 0.5);
        else                        QPCorrecture= 256*256;
    }else{
        c.packedYScale= 0x0100010001000100LL;
        c.packedYOffset= 0;
        QPCorrecture= 256*256;
    }

    /* copy & deinterlace first row of blocks */
    y=-BLOCK_SIZE;
    {
        const uint8_t *srcBlock= &(src[y*srcStride]);
        uint8_t *dstBlock= tempDst + dstStride;

        // From this point on it is guaranteed that we can read and write 16 lines downward
        // finish 1 block before the next otherwise we might have a problem
        // with the L1 Cache of the P4 ... or only a few blocks at a time or soemthing
        for(x=0; x<width; x+=BLOCK_SIZE){

#if HAVE_MMX2
/*
            prefetchnta(srcBlock + (((x>>2)&6) + 5)*srcStride + 32);
            prefetchnta(srcBlock + (((x>>2)&6) + 6)*srcStride + 32);
            prefetcht0(dstBlock + (((x>>2)&6) + 5)*dstStride + 32);
            prefetcht0(dstBlock + (((x>>2)&6) + 6)*dstStride + 32);
*/

            __asm__(
                "mov %4, %%"REG_a"              \n\t"
                "shr $2, %%"REG_a"              \n\t"
                "and $6, %%"REG_a"              \n\t"
                "add %5, %%"REG_a"              \n\t"
                "mov %%"REG_a", %%"REG_d"       \n\t"
                "imul %1, %%"REG_a"             \n\t"
                "imul %3, %%"REG_d"             \n\t"
                "prefetchnta 32(%%"REG_a", %0)  \n\t"
                "prefetcht0 32(%%"REG_d", %2)   \n\t"
                "add %1, %%"REG_a"              \n\t"
                "add %3, %%"REG_d"              \n\t"
                "prefetchnta 32(%%"REG_a", %0)  \n\t"
                "prefetcht0 32(%%"REG_d", %2)   \n\t"
                :: "r" (srcBlock), "r" ((x86_reg)srcStride), "r" (dstBlock), "r" ((x86_reg)dstStride),
                "g" ((x86_reg)x), "g" ((x86_reg)copyAhead)
                : "%"REG_a, "%"REG_d
            );

#elif HAVE_AMD3DNOW
//FIXME check if this is faster on an 3dnow chip or if it is faster without the prefetch or ...
/*          prefetch(srcBlock + (((x>>3)&3) + 5)*srcStride + 32);
            prefetch(srcBlock + (((x>>3)&3) + 9)*srcStride + 32);
            prefetchw(dstBlock + (((x>>3)&3) + 5)*dstStride + 32);
            prefetchw(dstBlock + (((x>>3)&3) + 9)*dstStride + 32);
*/
#endif

            RENAME(blockCopy)(dstBlock + dstStride*8, dstStride,
                              srcBlock + srcStride*8, srcStride, mode & LEVEL_FIX, &c.packedYOffset);

            RENAME(duplicate)(dstBlock + dstStride*8, dstStride);

            if(mode & LINEAR_IPOL_DEINT_FILTER)
                RENAME(deInterlaceInterpolateLinear)(dstBlock, dstStride);
            else if(mode & LINEAR_BLEND_DEINT_FILTER)
                RENAME(deInterlaceBlendLinear)(dstBlock, dstStride, c.deintTemp + x);
            else if(mode & MEDIAN_DEINT_FILTER)
                RENAME(deInterlaceMedian)(dstBlock, dstStride);
            else if(mode & CUBIC_IPOL_DEINT_FILTER)
                RENAME(deInterlaceInterpolateCubic)(dstBlock, dstStride);
            else if(mode & FFMPEG_DEINT_FILTER)
                RENAME(deInterlaceFF)(dstBlock, dstStride, c.deintTemp + x);
            else if(mode & LOWPASS5_DEINT_FILTER)
                RENAME(deInterlaceL5)(dstBlock, dstStride, c.deintTemp + x, c.deintTemp + width + x);
/*          else if(mode & CUBIC_BLEND_DEINT_FILTER)
                RENAME(deInterlaceBlendCubic)(dstBlock, dstStride);
*/
            dstBlock+=8;
            srcBlock+=8;
        }
        if(width==FFABS(dstStride))
            linecpy(dst, tempDst + 9*dstStride, copyAhead, dstStride);
        else{
            int i;
            for(i=0; i<copyAhead; i++){
                memcpy(dst + i*dstStride, tempDst + (9+i)*dstStride, width);
            }
        }
    }

    for(y=0; y<height; y+=BLOCK_SIZE){
        //1% speedup if these are here instead of the inner loop
        const uint8_t *srcBlock= &(src[y*srcStride]);
        uint8_t *dstBlock= &(dst[y*dstStride]);
#if HAVE_MMX
        uint8_t *tempBlock1= c.tempBlocks;
        uint8_t *tempBlock2= c.tempBlocks + 8;
#endif
        const int8_t *QPptr= &QPs[(y>>qpVShift)*QPStride];
        int8_t *nonBQPptr= &c.nonBQPTable[(y>>qpVShift)*FFABS(QPStride)];
        int QP=0;
        /* can we mess with a 8x16 block from srcBlock/dstBlock downwards and 1 line upwards
           if not than use a temporary buffer */
        if(y+15 >= height){
            int i;
            /* copy from line (copyAhead) to (copyAhead+7) of src, these will be copied with
               blockcopy to dst later */
            linecpy(tempSrc + srcStride*copyAhead, srcBlock + srcStride*copyAhead,
                    FFMAX(height-y-copyAhead, 0), srcStride);

            /* duplicate last line of src to fill the void up to line (copyAhead+7) */
            for(i=FFMAX(height-y, 8); i<copyAhead+8; i++)
                    memcpy(tempSrc + srcStride*i, src + srcStride*(height-1), FFABS(srcStride));

            /* copy up to (copyAhead+1) lines of dst (line -1 to (copyAhead-1))*/
            linecpy(tempDst, dstBlock - dstStride, FFMIN(height-y+1, copyAhead+1), dstStride);

            /* duplicate last line of dst to fill the void up to line (copyAhead) */
            for(i=height-y+1; i<=copyAhead; i++)
                    memcpy(tempDst + dstStride*i, dst + dstStride*(height-1), FFABS(dstStride));

            dstBlock= tempDst + dstStride;
            srcBlock= tempSrc;
        }

        // From this point on it is guaranteed that we can read and write 16 lines downward
        // finish 1 block before the next otherwise we might have a problem
        // with the L1 Cache of the P4 ... or only a few blocks at a time or soemthing
        for(x=0; x<width; x+=BLOCK_SIZE){
            const int stride= dstStride;
#if HAVE_MMX
            uint8_t *tmpXchg;
#endif
            if(isColor){
                QP= QPptr[x>>qpHShift];
                c.nonBQP= nonBQPptr[x>>qpHShift];
            }else{
                QP= QPptr[x>>4];
                QP= (QP* QPCorrecture + 256*128)>>16;
                c.nonBQP= nonBQPptr[x>>4];
                c.nonBQP= (c.nonBQP* QPCorrecture + 256*128)>>16;
                yHistogram[ srcBlock[srcStride*12 + 4] ]++;
            }
            c.QP= QP;
#if HAVE_MMX
            __asm__ volatile(
                "movd %1, %%mm7         \n\t"
                "packuswb %%mm7, %%mm7  \n\t" // 0, 0, 0, QP, 0, 0, 0, QP
                "packuswb %%mm7, %%mm7  \n\t" // 0,QP, 0, QP, 0,QP, 0, QP
                "packuswb %%mm7, %%mm7  \n\t" // QP,..., QP
                "movq %%mm7, %0         \n\t"
                : "=m" (c.pQPb)
                : "r" (QP)
            );
#endif


#if HAVE_MMX2
/*
            prefetchnta(srcBlock + (((x>>2)&6) + 5)*srcStride + 32);
            prefetchnta(srcBlock + (((x>>2)&6) + 6)*srcStride + 32);
            prefetcht0(dstBlock + (((x>>2)&6) + 5)*dstStride + 32);
            prefetcht0(dstBlock + (((x>>2)&6) + 6)*dstStride + 32);
*/

            __asm__(
                "mov %4, %%"REG_a"              \n\t"
                "shr $2, %%"REG_a"              \n\t"
                "and $6, %%"REG_a"              \n\t"
                "add %5, %%"REG_a"              \n\t"
                "mov %%"REG_a", %%"REG_d"       \n\t"
                "imul %1, %%"REG_a"             \n\t"
                "imul %3, %%"REG_d"             \n\t"
                "prefetchnta 32(%%"REG_a", %0)  \n\t"
                "prefetcht0 32(%%"REG_d", %2)   \n\t"
                "add %1, %%"REG_a"              \n\t"
                "add %3, %%"REG_d"              \n\t"
                "prefetchnta 32(%%"REG_a", %0)  \n\t"
                "prefetcht0 32(%%"REG_d", %2)   \n\t"
                :: "r" (srcBlock), "r" ((x86_reg)srcStride), "r" (dstBlock), "r" ((x86_reg)dstStride),
                "g" ((x86_reg)x), "g" ((x86_reg)copyAhead)
                : "%"REG_a, "%"REG_d
            );

#elif HAVE_AMD3DNOW
//FIXME check if this is faster on an 3dnow chip or if it is faster without the prefetch or ...
/*          prefetch(srcBlock + (((x>>3)&3) + 5)*srcStride + 32);
            prefetch(srcBlock + (((x>>3)&3) + 9)*srcStride + 32);
            prefetchw(dstBlock + (((x>>3)&3) + 5)*dstStride + 32);
            prefetchw(dstBlock + (((x>>3)&3) + 9)*dstStride + 32);
*/
#endif

            RENAME(blockCopy)(dstBlock + dstStride*copyAhead, dstStride,
                              srcBlock + srcStride*copyAhead, srcStride, mode & LEVEL_FIX, &c.packedYOffset);

            if(mode & LINEAR_IPOL_DEINT_FILTER)
                RENAME(deInterlaceInterpolateLinear)(dstBlock, dstStride);
            else if(mode & LINEAR_BLEND_DEINT_FILTER)
                RENAME(deInterlaceBlendLinear)(dstBlock, dstStride, c.deintTemp + x);
            else if(mode & MEDIAN_DEINT_FILTER)
                RENAME(deInterlaceMedian)(dstBlock, dstStride);
            else if(mode & CUBIC_IPOL_DEINT_FILTER)
                RENAME(deInterlaceInterpolateCubic)(dstBlock, dstStride);
            else if(mode & FFMPEG_DEINT_FILTER)
                RENAME(deInterlaceFF)(dstBlock, dstStride, c.deintTemp + x);
            else if(mode & LOWPASS5_DEINT_FILTER)
                RENAME(deInterlaceL5)(dstBlock, dstStride, c.deintTemp + x, c.deintTemp + width + x);
/*          else if(mode & CUBIC_BLEND_DEINT_FILTER)
                RENAME(deInterlaceBlendCubic)(dstBlock, dstStride);
*/

            /* only deblock if we have 2 blocks */
            if(y + 8 < height){
                if(mode & V_X1_FILTER)
                    RENAME(vertX1Filter)(dstBlock, stride, &c);
                else if(mode & V_DEBLOCK){
                    const int t= RENAME(vertClassify)(dstBlock, stride, &c);

                    if(t==1)
                        RENAME(doVertLowPass)(dstBlock, stride, &c);
                    else if(t==2)
                        RENAME(doVertDefFilter)(dstBlock, stride, &c);
                }else if(mode & V_A_DEBLOCK){
                    RENAME(do_a_deblock)(dstBlock, stride, 1, &c);
                }
            }

#if HAVE_MMX
            RENAME(transpose1)(tempBlock1, tempBlock2, dstBlock, dstStride);
#endif
            /* check if we have a previous block to deblock it with dstBlock */
            if(x - 8 >= 0){
#if HAVE_MMX
                if(mode & H_X1_FILTER)
                        RENAME(vertX1Filter)(tempBlock1, 16, &c);
                else if(mode & H_DEBLOCK){
//START_TIMER
                    const int t= RENAME(vertClassify)(tempBlock1, 16, &c);
//STOP_TIMER("dc & minmax")
                    if(t==1)
                        RENAME(doVertLowPass)(tempBlock1, 16, &c);
                    else if(t==2)
                        RENAME(doVertDefFilter)(tempBlock1, 16, &c);
                }else if(mode & H_A_DEBLOCK){
                        RENAME(do_a_deblock)(tempBlock1, 16, 1, &c);
                }

                RENAME(transpose2)(dstBlock-4, dstStride, tempBlock1 + 4*16);

#else
                if(mode & H_X1_FILTER)
                    horizX1Filter(dstBlock-4, stride, QP);
                else if(mode & H_DEBLOCK){
#if HAVE_ALTIVEC
                    DECLARE_ALIGNED(16, unsigned char, tempBlock)[272];
                    int t;
                    transpose_16x8_char_toPackedAlign_altivec(tempBlock, dstBlock - (4 + 1), stride);

                    t = vertClassify_altivec(tempBlock-48, 16, &c);
                    if(t==1) {
                        doVertLowPass_altivec(tempBlock-48, 16, &c);
                        transpose_8x16_char_fromPackedAlign_altivec(dstBlock - (4 + 1), tempBlock, stride);
                    }
                    else if(t==2) {
                        doVertDefFilter_altivec(tempBlock-48, 16, &c);
                        transpose_8x16_char_fromPackedAlign_altivec(dstBlock - (4 + 1), tempBlock, stride);
                    }
#else
                    const int t= RENAME(horizClassify)(dstBlock-4, stride, &c);

                    if(t==1)
                        RENAME(doHorizLowPass)(dstBlock-4, stride, &c);
                    else if(t==2)
                        RENAME(doHorizDefFilter)(dstBlock-4, stride, &c);
#endif
                }else if(mode & H_A_DEBLOCK){
                    RENAME(do_a_deblock)(dstBlock-8, 1, stride, &c);
                }
#endif //HAVE_MMX
                if(mode & DERING){
                //FIXME filter first line
                    if(y>0) RENAME(dering)(dstBlock - stride - 8, stride, &c);
                }

                if(mode & TEMP_NOISE_FILTER)
                {
                    RENAME(tempNoiseReducer)(dstBlock-8, stride,
                            c.tempBlurred[isColor] + y*dstStride + x,
                            c.tempBlurredPast[isColor] + (y>>3)*256 + (x>>3),
                            c.ppMode.maxTmpNoise);
                }
            }

            dstBlock+=8;
            srcBlock+=8;

#if HAVE_MMX
            tmpXchg= tempBlock1;
            tempBlock1= tempBlock2;
            tempBlock2 = tmpXchg;
#endif
        }

        if(mode & DERING){
            if(y > 0) RENAME(dering)(dstBlock - dstStride - 8, dstStride, &c);
        }

        if((mode & TEMP_NOISE_FILTER)){
            RENAME(tempNoiseReducer)(dstBlock-8, dstStride,
                    c.tempBlurred[isColor] + y*dstStride + x,
                    c.tempBlurredPast[isColor] + (y>>3)*256 + (x>>3),
                    c.ppMode.maxTmpNoise);
        }

        /* did we use a tmp buffer for the last lines*/
        if(y+15 >= height){
            uint8_t *dstBlock= &(dst[y*dstStride]);
            if(width==FFABS(dstStride))
                linecpy(dstBlock, tempDst + dstStride, height-y, dstStride);
            else{
                int i;
                for(i=0; i<height-y; i++){
                    memcpy(dstBlock + i*dstStride, tempDst + (i+1)*dstStride, width);
                }
            }
        }
/*
        for(x=0; x<width; x+=32){
            volatile int i;
            i+= + dstBlock[x + 7*dstStride] + dstBlock[x + 8*dstStride]
                + dstBlock[x + 9*dstStride] + dstBlock[x +10*dstStride]
                + dstBlock[x +11*dstStride] + dstBlock[x +12*dstStride];
                + dstBlock[x +13*dstStride]
                + dstBlock[x +14*dstStride] + dstBlock[x +15*dstStride];
        }*/
    }
#if   HAVE_AMD3DNOW
    __asm__ volatile("femms");
#elif HAVE_MMX
    __asm__ volatile("emms");
#endif

#ifdef DEBUG_BRIGHTNESS
    if(!isColor){
        int max=1;
        int i;
        for(i=0; i<256; i++)
            if(yHistogram[i] > max) max=yHistogram[i];

        for(i=1; i<256; i++){
            int x;
            int start=yHistogram[i-1]/(max/256+1);
            int end=yHistogram[i]/(max/256+1);
            int inc= end > start ? 1 : -1;
            for(x=start; x!=end+inc; x+=inc)
                dst[ i*dstStride + x]+=128;
        }

        for(i=0; i<100; i+=2){
            dst[ (white)*dstStride + i]+=128;
            dst[ (black)*dstStride + i]+=128;
        }
    }
#endif

    *c2= c; //copy local context back

}
