/*
 * FFT/IFFT transforms
 * AltiVec-enabled
 * Copyright (c) 2009 Loren Merritt
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
#include "libavcodec/fft.h"
#include "util_altivec.h"
#include "types_altivec.h"
#include "regs.h"

/**
 * Do a complex FFT with the parameters defined in ff_fft_init(). The
 * input data must be permuted before with s->revtab table. No
 * 1.0/sqrt(n) normalization is done.
 * AltiVec-enabled
 * This code assumes that the 'z' pointer is 16 bytes-aligned
 * It also assumes all FFTComplex are 8 bytes-aligned pair of float
 */

// Pointers to functions. Not using function pointer syntax, because
// that involves an extra level of indirection on some PPC ABIs.
extern void *ff_fft_dispatch_altivec[2][15];

// Convert from simd order to C order.
static void swizzle(vec_f *z, int n)
{
    int i;
    n >>= 1;
    for (i = 0; i < n; i += 2) {
        vec_f re = z[i];
        vec_f im = z[i+1];
        z[i]   = vec_mergeh(re, im);
        z[i+1] = vec_mergel(re, im);
    }
}

static void ff_fft_calc_altivec(FFTContext *s, FFTComplex *z)
{
    register vec_f  v14 __asm__("v14") = {0,0,0,0};
    register vec_f  v15 __asm__("v15") = *(const vec_f*)ff_cos_16;
    register vec_f  v16 __asm__("v16") = {0, 0.38268343, M_SQRT1_2, 0.92387953};
    register vec_f  v17 __asm__("v17") = {-M_SQRT1_2, M_SQRT1_2, M_SQRT1_2,-M_SQRT1_2};
    register vec_f  v18 __asm__("v18") = { M_SQRT1_2, M_SQRT1_2, M_SQRT1_2, M_SQRT1_2};
    register vec_u8 v19 __asm__("v19") = vcprm(s0,3,2,1);
    register vec_u8 v20 __asm__("v20") = vcprm(0,1,s2,s1);
    register vec_u8 v21 __asm__("v21") = vcprm(2,3,s0,s3);
    register vec_u8 v22 __asm__("v22") = vcprm(2,s3,3,s2);
    register vec_u8 v23 __asm__("v23") = vcprm(0,1,s0,s1);
    register vec_u8 v24 __asm__("v24") = vcprm(2,3,s2,s3);
    register vec_u8 v25 __asm__("v25") = vcprm(2,3,0,1);
    register vec_u8 v26 __asm__("v26") = vcprm(1,2,s3,s0);
    register vec_u8 v27 __asm__("v27") = vcprm(0,3,s2,s1);
    register vec_u8 v28 __asm__("v28") = vcprm(0,2,s1,s3);
    register vec_u8 v29 __asm__("v29") = vcprm(1,3,s0,s2);
    register FFTSample *const*cos_tabs __asm__("r12") = ff_cos_tabs;
    register FFTComplex *zarg __asm__("r3") = z;
    __asm__(
        "mtctr %0               \n"
        "li   "r(9)", 16        \n"
        "subi "r(1)","r(1) ",%1 \n"
        "bctrl                  \n"
        "addi "r(1)","r(1) ",%1 \n"
        ::"r"(ff_fft_dispatch_altivec[1][s->nbits-2]), "i"(12*sizeof(void*)),
          "r"(zarg), "r"(cos_tabs),
          "v"(v14),"v"(v15),"v"(v16),"v"(v17),"v"(v18),"v"(v19),"v"(v20),"v"(v21),
          "v"(v22),"v"(v23),"v"(v24),"v"(v25),"v"(v26),"v"(v27),"v"(v28),"v"(v29)
        : "lr","ctr","r0","r4","r5","r6","r7","r8","r9","r10","r11",
          "v0","v1","v2","v3","v4","v5","v6","v7","v8","v9","v10","v11","v12","v13"
    );
    if (s->nbits <= 4)
        swizzle((vec_f*)z, 1<<s->nbits);
}

av_cold void ff_fft_init_altivec(FFTContext *s)
{
    if (HAVE_GNU_AS)
        s->fft_calc = ff_fft_calc_altivec;
}
