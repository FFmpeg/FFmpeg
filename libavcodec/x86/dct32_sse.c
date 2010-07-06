/*
 * 32 point SSE-optimized DCT transform
 * Copyright (c) 2010 Vitor Sessak
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

#include "libavutil/x86_cpu.h"
#include "libavutil/mem.h"
#include "libavcodec/dsputil.h"
#include "fft.h"

DECLARE_ALIGNED(16, static const float, b1)[] = {
     0.500603,  0.505471,  0.515447,  0.531043,
     0.553104,  0.582935,  0.622504,  0.674808,
    -1.169440, -0.972568, -0.839350, -0.744536,
   -10.190008, -3.407609, -2.057781, -1.484165,
     0.502419,  0.522499,  0.566944,  0.646822,
     0.788155,  1.060678,  1.722447,  5.101149,
     0.509796,  0.601345,  0.899976,  2.562916,
     1.000000,  1.000000,  1.306563,  0.541196,
     1.000000,  0.707107,  1.000000, -0.707107
};

DECLARE_ALIGNED(16, static const int32_t, smask)[4] = {
    0, 0, 0x80000000, 0x80000000
};

/* butterfly operator */
#define BUTTERFLY(a,b,c,tmp)                            \
    "movaps  %%" #a    ", %%" #tmp  "             \n\t" \
    "subps   %%" #b    ", %%" #a    "             \n\t" \
    "addps   %%" #tmp  ", %%" #b    "             \n\t" \
    "mulps     " #c    ", %%" #a    "             \n\t"

///* Same as BUTTERFLY when vectors a and b overlap */
#define BUTTERFLY0(val, mask, cos, tmp, shuf)                            \
    "movaps  %%" #val  ", %%" #tmp  "             \n\t"                  \
    "shufps    " #shuf ", %%" #val  ",%%" #val "  \n\t"                  \
    "xorps   %%" #mask ", %%" #tmp  "             \n\t" /* flip signs */ \
    "addps   %%" #tmp  ", %%" #val  "             \n\t"                  \
    "mulps   %%" #cos  ", %%" #val  "             \n\t"

#define BUTTERFLY2(val, mask, cos, tmp) BUTTERFLY0(val, mask, cos, tmp, $0x1b)
#define BUTTERFLY3(val, mask, cos, tmp) BUTTERFLY0(val, mask, cos, tmp, $0xb1)

void ff_dct32_float_sse(FFTSample *out, const FFTSample *in)
{
    int32_t tmp1 = 0;
    __asm__ volatile(
        /* pass 1 */

        "movaps    (%4), %%xmm0           \n\t"
        "movaps 112(%4), %%xmm1           \n\t"
        "shufps   $0x1b, %%xmm1, %%xmm1   \n\t"
        BUTTERFLY(xmm0, xmm1, (%2), xmm3)

        "movaps  64(%4), %%xmm7           \n\t"
        "movaps  48(%4), %%xmm4           \n\t"
        "shufps   $0x1b, %%xmm4, %%xmm4   \n\t"
        BUTTERFLY(xmm7, xmm4, 48(%2), xmm3)


        /* pass 2 */
        "movaps  64(%2), %%xmm2           \n\t"
        BUTTERFLY(xmm1, xmm4, %%xmm2, xmm3)
        "movaps  %%xmm1, 48(%1)           \n\t"
        "movaps  %%xmm4, (%1)             \n\t"

        /* pass 1 */
        "movaps  16(%4), %%xmm1           \n\t"
        "movaps  96(%4), %%xmm6           \n\t"
        "shufps   $0x1b, %%xmm6, %%xmm6   \n\t"
        BUTTERFLY(xmm1, xmm6, 16(%2), xmm3)

        "movaps  80(%4), %%xmm4           \n\t"
        "movaps  32(%4), %%xmm5           \n\t"
        "shufps   $0x1b, %%xmm5, %%xmm5   \n\t"
        BUTTERFLY(xmm4, xmm5, 32(%2), xmm3)

        /* pass 2 */
        BUTTERFLY(xmm0, xmm7, %%xmm2, xmm3)

        "movaps  80(%2), %%xmm2           \n\t"
        BUTTERFLY(xmm6, xmm5, %%xmm2, xmm3)

        BUTTERFLY(xmm1, xmm4, %%xmm2, xmm3)

        /* pass 3 */
        "movaps  96(%2), %%xmm2           \n\t"
        "shufps   $0x1b, %%xmm1, %%xmm1   \n\t"
        BUTTERFLY(xmm0, xmm1, %%xmm2, xmm3)
        "movaps  %%xmm0, 112(%1)          \n\t"
        "movaps  %%xmm1,  96(%1)          \n\t"

        "movaps   0(%1), %%xmm0           \n\t"
        "shufps   $0x1b, %%xmm5, %%xmm5   \n\t"
        BUTTERFLY(xmm0, xmm5, %%xmm2, xmm3)

        "movaps  48(%1), %%xmm1           \n\t"
        "shufps   $0x1b, %%xmm6, %%xmm6   \n\t"
        BUTTERFLY(xmm1, xmm6, %%xmm2, xmm3)
        "movaps  %%xmm1,  48(%1)          \n\t"

        "shufps   $0x1b, %%xmm4, %%xmm4   \n\t"
        BUTTERFLY(xmm7, xmm4, %%xmm2, xmm3)

        /* pass 4 */
        "movaps    (%3), %%xmm3           \n\t"
        "movaps 112(%2), %%xmm2           \n\t"

        BUTTERFLY2(xmm5, xmm3, xmm2, xmm1)

        BUTTERFLY2(xmm0, xmm3, xmm2, xmm1)
        "movaps  %%xmm0, 16(%1)           \n\t"

        BUTTERFLY2(xmm6, xmm3, xmm2, xmm1)
        "movaps  %%xmm6, 32(%1)           \n\t"

        "movaps  48(%1), %%xmm0           \n\t"
        BUTTERFLY2(xmm0, xmm3, xmm2, xmm1)
        "movaps  %%xmm0, 48(%1)           \n\t"

        BUTTERFLY2(xmm4, xmm3, xmm2, xmm1)

        BUTTERFLY2(xmm7, xmm3, xmm2, xmm1)

        "movaps  96(%1), %%xmm6           \n\t"
        BUTTERFLY2(xmm6, xmm3, xmm2, xmm1)

        "movaps 112(%1), %%xmm0           \n\t"
        BUTTERFLY2(xmm0, xmm3, xmm2, xmm1)

        /* pass 5 */
        "movaps 128(%2), %%xmm2           \n\t"
        "shufps   $0xCC, %%xmm3,%%xmm3    \n\t"

        BUTTERFLY3(xmm5, xmm3, xmm2, xmm1)
        "movaps  %%xmm5, (%1)             \n\t"

        "movaps  16(%1), %%xmm1           \n\t"
        BUTTERFLY3(xmm1, xmm3, xmm2, xmm5)
        "movaps  %%xmm1, 16(%1)           \n\t"

        BUTTERFLY3(xmm4, xmm3, xmm2, xmm5)
        "movaps  %%xmm4, 64(%1)           \n\t"

        BUTTERFLY3(xmm7, xmm3, xmm2, xmm5)
        "movaps  %%xmm7, 80(%1)           \n\t"

        "movaps  32(%1), %%xmm5           \n\t"
        BUTTERFLY3(xmm5, xmm3, xmm2, xmm7)
        "movaps  %%xmm5, 32(%1)           \n\t"

        "movaps  48(%1), %%xmm4           \n\t"
        BUTTERFLY3(xmm4, xmm3, xmm2, xmm7)
        "movaps  %%xmm4, 48(%1)           \n\t"

        BUTTERFLY3(xmm6, xmm3, xmm2, xmm7)
        "movaps  %%xmm6, 96(%1)           \n\t"

        BUTTERFLY3(xmm0, xmm3, xmm2, xmm7)
        "movaps  %%xmm0, 112(%1)          \n\t"


        /* pass 6, no SIMD... */
        "movss    56(%1),  %%xmm3           \n\t"
        "movl      4(%1),      %0           \n\t"
        "addss    60(%1),  %%xmm3           \n\t"
        "movss    72(%1),  %%xmm7           \n\t"
        "addss    %%xmm3,  %%xmm4           \n\t"
        "movss    52(%1),  %%xmm2           \n\t"
        "addss    %%xmm3,  %%xmm2           \n\t"
        "movss    24(%1),  %%xmm3           \n\t"
        "addss    28(%1),  %%xmm3           \n\t"
        "addss    76(%1),  %%xmm7           \n\t"
        "addss    %%xmm3,  %%xmm1           \n\t"
        "addss    %%xmm4,  %%xmm5           \n\t"
        "movss    %%xmm1,  16(%1)           \n\t"
        "movss    20(%1),  %%xmm1           \n\t"
        "addss    %%xmm3,  %%xmm1           \n\t"
        "movss    40(%1),  %%xmm3           \n\t"
        "movss    %%xmm1,  48(%1)           \n\t"
        "addss    44(%1),  %%xmm3           \n\t"
        "movss    20(%1),  %%xmm1           \n\t"
        "addss    %%xmm3,  %%xmm4           \n\t"
        "addss    %%xmm2,  %%xmm3           \n\t"
        "addss    28(%1),  %%xmm1           \n\t"
        "movss    %%xmm3,  40(%1)           \n\t"
        "addss    36(%1),  %%xmm2           \n\t"
        "movss     8(%1),  %%xmm3           \n\t"
        "movss    %%xmm2,  56(%1)           \n\t"
        "addss    12(%1),  %%xmm3           \n\t"
        "movss    %%xmm5,   8(%1)           \n\t"
        "movss    %%xmm3,  32(%1)           \n\t"
        "movss    52(%1),  %%xmm2           \n\t"
        "movss    80(%1),  %%xmm3           \n\t"
        "movss   120(%1),  %%xmm5           \n\t"
        "movss    %%xmm1,  80(%1)           \n\t"
        "movss    %%xmm4,  24(%1)           \n\t"
        "addss   124(%1),  %%xmm5           \n\t"
        "movss    64(%1),  %%xmm1           \n\t"
        "addss    60(%1),  %%xmm2           \n\t"
        "addss    %%xmm5,  %%xmm0           \n\t"
        "addss   116(%1),  %%xmm5           \n\t"
        "movl         %0,  64(%1)           \n\t"
        "addss    %%xmm0,  %%xmm6           \n\t"
        "addss    %%xmm6,  %%xmm1           \n\t"
        "movl     12(%1),      %0           \n\t"
        "movss    %%xmm1,   4(%1)           \n\t"
        "movss    88(%1),  %%xmm1           \n\t"
        "movl         %0,  96(%1)           \n\t"
        "addss    92(%1),  %%xmm1           \n\t"
        "movss   104(%1),  %%xmm4           \n\t"
        "movl     28(%1),      %0           \n\t"
        "addss   108(%1),  %%xmm4           \n\t"
        "addss    %%xmm4,  %%xmm0           \n\t"
        "addss    %%xmm1,  %%xmm3           \n\t"
        "addss    84(%1),  %%xmm1           \n\t"
        "addss    %%xmm5,  %%xmm4           \n\t"
        "addss    %%xmm3,  %%xmm6           \n\t"
        "addss    %%xmm0,  %%xmm3           \n\t"
        "addss    %%xmm7,  %%xmm0           \n\t"
        "addss   100(%1),  %%xmm5           \n\t"
        "addss    %%xmm4,  %%xmm7           \n\t"
        "movl         %0, 112(%1)           \n\t"
        "movss    %%xmm0,  28(%1)           \n\t"
        "movss    36(%1),  %%xmm0           \n\t"
        "movss    %%xmm7,  36(%1)           \n\t"
        "addss    %%xmm1,  %%xmm4           \n\t"
        "movss   116(%1),  %%xmm7           \n\t"
        "addss    %%xmm2,  %%xmm0           \n\t"
        "addss   124(%1),  %%xmm7           \n\t"
        "movss    %%xmm0,  72(%1)           \n\t"
        "movss    44(%1),  %%xmm0           \n\t"
        "movss    %%xmm6,  12(%1)           \n\t"
        "movss    %%xmm3,  20(%1)           \n\t"
        "addss    %%xmm0,  %%xmm2           \n\t"
        "movss    %%xmm4,  44(%1)           \n\t"
        "movss    %%xmm2,  88(%1)           \n\t"
        "addss    60(%1),  %%xmm0           \n\t"
        "movl     60(%1),      %0           \n\t"
        "movl         %0, 120(%1)           \n\t"
        "movss    %%xmm0, 104(%1)           \n\t"
        "addss    %%xmm5,  %%xmm1           \n\t"
        "addss    68(%1),  %%xmm5           \n\t"
        "movss    %%xmm1,  52(%1)           \n\t"
        "movss    %%xmm5,  60(%1)           \n\t"
        "movss    68(%1),  %%xmm1           \n\t"
        "movss   100(%1),  %%xmm5           \n\t"
        "addss    %%xmm7,  %%xmm5           \n\t"
        "addss   108(%1),  %%xmm7           \n\t"
        "addss    %%xmm5,  %%xmm1           \n\t"
        "movss    84(%1),  %%xmm2           \n\t"
        "addss    92(%1),  %%xmm2           \n\t"
        "addss    %%xmm2,  %%xmm5           \n\t"
        "movss    %%xmm1,  68(%1)           \n\t"
        "addss    %%xmm7,  %%xmm2           \n\t"
        "movss    76(%1),  %%xmm1           \n\t"
        "movss    %%xmm2,  84(%1)           \n\t"
        "movss    %%xmm5,  76(%1)           \n\t"
        "movss   108(%1),  %%xmm2           \n\t"
        "addss    %%xmm1,  %%xmm7           \n\t"
        "addss   124(%1),  %%xmm2           \n\t"
        "addss    %%xmm2,  %%xmm1           \n\t"
        "addss    92(%1),  %%xmm2           \n\t"
        "movss    %%xmm1, 100(%1)           \n\t"
        "movss    %%xmm2, 108(%1)           \n\t"
        "movss    92(%1),  %%xmm2           \n\t"
        "movss    %%xmm7,  92(%1)           \n\t"
        "addss   124(%1),  %%xmm2           \n\t"
        "movss    %%xmm2, 116(%1)           \n\t"
        :"+&r"(tmp1)
        :"r"(out), "r"(b1), "r"(smask), "r"(in)
        :"memory"
        );
}

