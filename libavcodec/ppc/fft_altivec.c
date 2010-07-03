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

#if HAVE_GNU_AS
static av_always_inline void fft_dispatch(FFTContext *s, FFTComplex *z, int do_swizzle)
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
        ::"r"(ff_fft_dispatch_altivec[do_swizzle][s->nbits-2]), "i"(12*sizeof(void*)),
          "r"(zarg), "r"(cos_tabs),
          "v"(v14),"v"(v15),"v"(v16),"v"(v17),"v"(v18),"v"(v19),"v"(v20),"v"(v21),
          "v"(v22),"v"(v23),"v"(v24),"v"(v25),"v"(v26),"v"(v27),"v"(v28),"v"(v29)
        : "lr","ctr","r0","r4","r5","r6","r7","r8","r9","r10","r11",
          "v0","v1","v2","v3","v4","v5","v6","v7","v8","v9","v10","v11","v12","v13"
    );
}

static void ff_fft_calc_altivec(FFTContext *s, FFTComplex *z)
{
    fft_dispatch(s, z, 1);
}

static void ff_imdct_half_altivec(FFTContext *s, FFTSample *output, const FFTSample *input)
{
    int j, k;
    int n = 1 << s->mdct_bits;
    int n4 = n >> 2;
    int n8 = n >> 3;
    int n32 = n >> 5;
    const uint16_t *revtabj = s->revtab;
    const uint16_t *revtabk = s->revtab+n4;
    const vec_f *tcos = (const vec_f*)(s->tcos+n8);
    const vec_f *tsin = (const vec_f*)(s->tsin+n8);
    const vec_f *pin = (const vec_f*)(input+n4);
    vec_f *pout = (vec_f*)(output+n4);

    /* pre rotation */
    k = n32-1;
    do {
        vec_f cos,sin,cos0,sin0,cos1,sin1,re,im,r0,i0,r1,i1,a,b,c,d;
#define CMULA(p,o0,o1,o2,o3)\
        a = pin[ k*2+p];                       /* { z[k].re,    z[k].im,    z[k+1].re,  z[k+1].im  } */\
        b = pin[-k*2-p-1];                     /* { z[-k-2].re, z[-k-2].im, z[-k-1].re, z[-k-1].im } */\
        re = vec_perm(a, b, vcprm(0,2,s0,s2)); /* { z[k].re,    z[k+1].re,  z[-k-2].re, z[-k-1].re } */\
        im = vec_perm(a, b, vcprm(s3,s1,3,1)); /* { z[-k-1].im, z[-k-2].im, z[k+1].im,  z[k].im    } */\
        cos = vec_perm(cos0, cos1, vcprm(o0,o1,s##o2,s##o3)); /* { cos[k], cos[k+1], cos[-k-2], cos[-k-1] } */\
        sin = vec_perm(sin0, sin1, vcprm(o0,o1,s##o2,s##o3));\
        r##p = im*cos - re*sin;\
        i##p = re*cos + im*sin;
#define STORE2(v,dst)\
        j = dst;\
        vec_ste(v, 0, output+j*2);\
        vec_ste(v, 4, output+j*2);
#define STORE8(p)\
        a = vec_perm(r##p, i##p, vcprm(0,s0,0,s0));\
        b = vec_perm(r##p, i##p, vcprm(1,s1,1,s1));\
        c = vec_perm(r##p, i##p, vcprm(2,s2,2,s2));\
        d = vec_perm(r##p, i##p, vcprm(3,s3,3,s3));\
        STORE2(a, revtabk[ p*2-4]);\
        STORE2(b, revtabk[ p*2-3]);\
        STORE2(c, revtabj[-p*2+2]);\
        STORE2(d, revtabj[-p*2+3]);

        cos0 = tcos[k];
        sin0 = tsin[k];
        cos1 = tcos[-k-1];
        sin1 = tsin[-k-1];
        CMULA(0, 0,1,2,3);
        CMULA(1, 2,3,0,1);
        STORE8(0);
        STORE8(1);
        revtabj += 4;
        revtabk -= 4;
        k--;
    } while(k >= 0);

    fft_dispatch(s, (FFTComplex*)output, 0);

    /* post rotation + reordering */
    j = -n32;
    k = n32-1;
    do {
        vec_f cos,sin,re,im,a,b,c,d;
#define CMULB(d0,d1,o)\
        re = pout[o*2];\
        im = pout[o*2+1];\
        cos = tcos[o];\
        sin = tsin[o];\
        d0 = im*sin - re*cos;\
        d1 = re*sin + im*cos;

        CMULB(a,b,j);
        CMULB(c,d,k);
        pout[2*j]   = vec_perm(a, d, vcprm(0,s3,1,s2));
        pout[2*j+1] = vec_perm(a, d, vcprm(2,s1,3,s0));
        pout[2*k]   = vec_perm(c, b, vcprm(0,s3,1,s2));
        pout[2*k+1] = vec_perm(c, b, vcprm(2,s1,3,s0));
        j++;
        k--;
    } while(k >= 0);
}

static void ff_imdct_calc_altivec(FFTContext *s, FFTSample *output, const FFTSample *input)
{
    int k;
    int n = 1 << s->mdct_bits;
    int n4 = n >> 2;
    int n16 = n >> 4;
    vec_u32 sign = {1<<31,1<<31,1<<31,1<<31};
    vec_u32 *p0 = (vec_u32*)(output+n4);
    vec_u32 *p1 = (vec_u32*)(output+n4*3);

    ff_imdct_half_altivec(s, output+n4, input);

    for (k = 0; k < n16; k++) {
        vec_u32 a = p0[k] ^ sign;
        vec_u32 b = p1[-k-1];
        p0[-k-1] = vec_perm(a, a, vcprm(3,2,1,0));
        p1[k]    = vec_perm(b, b, vcprm(3,2,1,0));
    }
}
#endif /* HAVE_GNU_AS */

av_cold void ff_fft_init_altivec(FFTContext *s)
{
#if HAVE_GNU_AS
    s->fft_calc = ff_fft_calc_altivec;
    s->imdct_calc = ff_imdct_calc_altivec;
    s->imdct_half = ff_imdct_half_altivec;
#endif
}
