#ifndef AVCODEC_PPC_FFT_VSX_H
#define AVCODEC_PPC_FFT_VSX_H
/*
 * FFT  transform, optimized with VSX built-in functions
 * Copyright (c) 2014 Rong Yan  Copyright (c) 2009 Loren Merritt
 *
 * This algorithm (though not any of the implementation details) is
 * based on libdjbfft by D. J. Bernstein, and fft_altivec_s.S.
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


#include "config.h"
#include "libavutil/cpu.h"
#include "libavutil/ppc/util_altivec.h"
#include "libavcodec/fft.h"
#include "libavcodec/fft-internal.h"

#if HAVE_VSX

void ff_fft_calc_interleave_vsx(FFTContext *s, FFTComplex *z);
void ff_fft_calc_vsx(FFTContext *s, FFTComplex *z);


#define byte_2complex (2*sizeof(FFTComplex))
#define byte_4complex (4*sizeof(FFTComplex))
#define byte_6complex (6*sizeof(FFTComplex))
#define byte_8complex (8*sizeof(FFTComplex))
#define byte_10complex (10*sizeof(FFTComplex))
#define byte_12complex (12*sizeof(FFTComplex))
#define byte_14complex (14*sizeof(FFTComplex))

inline static void pass_vsx_interleave(FFTComplex *z, const FFTSample *wre, unsigned int n)
{
    int o1 = n<<1;
    int o2 = n<<2;
    int o3 = o1+o2;
    int i1, i2, i3;
    FFTSample* out = (FFTSample*)z;
    const FFTSample *wim = wre+o1;
    vec_f vz0, vzo1, vzo2, vzo3;
    vec_f x0, x1, x2, x3;
    vec_f x4, x5, x6, x7;
    vec_f x8, x9, x10, x11;
    vec_f x12, x13, x14, x15;
    vec_f x16, x17, x18, x19;
    vec_f x20, x21, x22, x23;
    vec_f vz0plus1, vzo1plus1, vzo2plus1, vzo3plus1;
    vec_f y0, y1, y2, y3;
    vec_f y4, y5, y8, y9;
    vec_f y10, y13, y14, y15;
    vec_f y16, y17, y18, y19;
    vec_f y20, y21, y22, y23;
    vec_f wr1, wi1, wr0, wi0;
    vec_f wr2, wi2, wr3, wi3;
    vec_f xmulwi0, xmulwi1, ymulwi2, ymulwi3;

    n = n-2;
    i1 = o1*sizeof(FFTComplex);
    i2 = o2*sizeof(FFTComplex);
    i3 = o3*sizeof(FFTComplex);
    vzo2 = vec_ld(i2, &(out[0]));  // zo2.r  zo2.i  z(o2+1).r  z(o2+1).i
    vzo2plus1 = vec_ld(i2+16, &(out[0]));
    vzo3 = vec_ld(i3, &(out[0]));  // zo3.r  zo3.i  z(o3+1).r  z(o3+1).i
    vzo3plus1 = vec_ld(i3+16, &(out[0]));
    vz0 = vec_ld(0, &(out[0]));    // z0.r  z0.i  z1.r  z1.i
    vz0plus1 = vec_ld(16, &(out[0]));
    vzo1 = vec_ld(i1, &(out[0]));  // zo1.r  zo1.i  z(o1+1).r  z(o1+1).i
    vzo1plus1 = vec_ld(i1+16, &(out[0]));

    x0 = vec_add(vzo2, vzo3);
    x1 = vec_sub(vzo2, vzo3);
    y0 = vec_add(vzo2plus1, vzo3plus1);
    y1 = vec_sub(vzo2plus1, vzo3plus1);

    wr1 = vec_splats(wre[1]);
    wi1 = vec_splats(wim[-1]);
    wi2 = vec_splats(wim[-2]);
    wi3 = vec_splats(wim[-3]);
    wr2 = vec_splats(wre[2]);
    wr3 = vec_splats(wre[3]);

    x2 = vec_perm(x0, x1, vcprm(2,s2,3,s3));
    x3 = vec_perm(x0, x1, vcprm(s3,3,s2,2));

    y4 = vec_perm(y0, y1, vcprm(s1,1,s0,0));
    y5 = vec_perm(y0, y1, vcprm(s3,3,s2,2));
    y2 = vec_perm(y0, y1, vcprm(0,s0,1,s1));
    y3 = vec_perm(y0, y1, vcprm(2,s2,3,s3));

    ymulwi2 = vec_mul(y4, wi2);
    ymulwi3 = vec_mul(y5, wi3);
    x4 = vec_mul(x2, wr1);
    x5 = vec_mul(x3, wi1);
    y8 = vec_madd(y2, wr2, ymulwi2);
    y9 = vec_msub(y2, wr2, ymulwi2);
    x6 = vec_add(x4, x5);
    x7 = vec_sub(x4, x5);
    y13 = vec_madd(y3, wr3, ymulwi3);
    y14 = vec_msub(y3, wr3, ymulwi3);

    x8 = vec_perm(x6, x7, vcprm(0,1,s2,s3));
    y10 = vec_perm(y8, y9, vcprm(0,1,s2,s3));
    y15 = vec_perm(y13, y14, vcprm(0,1,s2,s3));

    x9 = vec_perm(x0, x8, vcprm(0,1,s0,s2));
    x10 = vec_perm(x1, x8, vcprm(1,0,s3,s1));

    y16 = vec_perm(y10, y15, vcprm(0,2,s0,s2));
    y17 = vec_perm(y10, y15, vcprm(3,1,s3,s1));

    x11 = vec_add(vz0, x9);
    x12 = vec_sub(vz0, x9);
    x13 = vec_add(vzo1, x10);
    x14 = vec_sub(vzo1, x10);

    y18 = vec_add(vz0plus1, y16);
    y19 = vec_sub(vz0plus1, y16);
    y20 = vec_add(vzo1plus1, y17);
    y21 = vec_sub(vzo1plus1, y17);

    x15 = vec_perm(x13, x14, vcprm(0,s1,2,s3));
    x16 = vec_perm(x13, x14, vcprm(s0,1,s2,3));
    y22 = vec_perm(y20, y21, vcprm(0,s1,2,s3));
    y23 = vec_perm(y20, y21, vcprm(s0,1,s2,3));


    vec_st(x11, 0, &(out[0]));
    vec_st(y18, 16, &(out[0]));
    vec_st(x15, i1, &(out[0]));
    vec_st(y22, i1+16, &(out[0]));
    vec_st(x12, i2, &(out[0]));
    vec_st(y19, i2+16, &(out[0]));
    vec_st(x16, i3, &(out[0]));
    vec_st(y23, i3+16, &(out[0]));

    do {
        out += 8;
        wre += 4;
        wim -= 4;
        wr0 = vec_splats(wre[0]);
        wr1 = vec_splats(wre[1]);
        wi0 = vec_splats(wim[0]);
        wi1 = vec_splats(wim[-1]);

        wr2 = vec_splats(wre[2]);
        wr3 = vec_splats(wre[3]);
        wi2 = vec_splats(wim[-2]);
        wi3 = vec_splats(wim[-3]);

        vzo2 = vec_ld(i2, &(out[0]));  // zo2.r  zo2.i  z(o2+1).r  z(o2+1).i
        vzo2plus1 = vec_ld(i2+16, &(out[0]));
        vzo3 = vec_ld(i3, &(out[0]));  // zo3.r  zo3.i  z(o3+1).r  z(o3+1).i
        vzo3plus1 = vec_ld(i3+16, &(out[0]));
        vz0 = vec_ld(0, &(out[0]));    // z0.r  z0.i  z1.r  z1.i
        vz0plus1 = vec_ld(16, &(out[0]));
        vzo1 = vec_ld(i1, &(out[0])); // zo1.r  zo1.i  z(o1+1).r  z(o1+1).i
        vzo1plus1 = vec_ld(i1+16, &(out[0]));

        x0 = vec_add(vzo2, vzo3);
        x1 = vec_sub(vzo2, vzo3);

        y0 = vec_add(vzo2plus1, vzo3plus1);
        y1 = vec_sub(vzo2plus1, vzo3plus1);

        x4 = vec_perm(x0, x1, vcprm(s1,1,s0,0));
        x5 = vec_perm(x0, x1, vcprm(s3,3,s2,2));
        x2 = vec_perm(x0, x1, vcprm(0,s0,1,s1));
        x3 = vec_perm(x0, x1, vcprm(2,s2,3,s3));

        y2 = vec_perm(y0, y1, vcprm(0,s0,1,s1));
        y3 = vec_perm(y0, y1, vcprm(2,s2,3,s3));
        xmulwi0 = vec_mul(x4, wi0);
        xmulwi1 = vec_mul(x5, wi1);

        y4 = vec_perm(y0, y1, vcprm(s1,1,s0,0));
        y5 = vec_perm(y0, y1, vcprm(s3,3,s2,2));

        x8 = vec_madd(x2, wr0, xmulwi0);
        x9 = vec_msub(x2, wr0, xmulwi0);
        ymulwi2 = vec_mul(y4, wi2);
        ymulwi3 = vec_mul(y5, wi3);

        x13 = vec_madd(x3, wr1, xmulwi1);
        x14 = vec_msub(x3, wr1, xmulwi1);

        y8 = vec_madd(y2, wr2, ymulwi2);
        y9 = vec_msub(y2, wr2, ymulwi2);
        y13 = vec_madd(y3, wr3, ymulwi3);
        y14 = vec_msub(y3, wr3, ymulwi3);

        x10 = vec_perm(x8, x9, vcprm(0,1,s2,s3));
        x15 = vec_perm(x13, x14, vcprm(0,1,s2,s3));

        y10 = vec_perm(y8, y9, vcprm(0,1,s2,s3));
        y15 = vec_perm(y13, y14, vcprm(0,1,s2,s3));

        x16 = vec_perm(x10, x15, vcprm(0,2,s0,s2));
        x17 = vec_perm(x10, x15, vcprm(3,1,s3,s1));

        y16 = vec_perm(y10, y15, vcprm(0,2,s0,s2));
        y17 = vec_perm(y10, y15, vcprm(3,1,s3,s1));

        x18 = vec_add(vz0, x16);
        x19 = vec_sub(vz0, x16);
        x20 = vec_add(vzo1, x17);
        x21 = vec_sub(vzo1, x17);

        y18 = vec_add(vz0plus1, y16);
        y19 = vec_sub(vz0plus1, y16);
        y20 = vec_add(vzo1plus1, y17);
        y21 = vec_sub(vzo1plus1, y17);

        x22 = vec_perm(x20, x21, vcprm(0,s1,2,s3));
        x23 = vec_perm(x20, x21, vcprm(s0,1,s2,3));

        y22 = vec_perm(y20, y21, vcprm(0,s1,2,s3));
        y23 = vec_perm(y20, y21, vcprm(s0,1,s2,3));

        vec_st(x18, 0, &(out[0]));
        vec_st(y18, 16, &(out[0]));
        vec_st(x22, i1, &(out[0]));
        vec_st(y22, i1+16, &(out[0]));
        vec_st(x19, i2, &(out[0]));
        vec_st(y19, i2+16, &(out[0]));
        vec_st(x23, i3, &(out[0]));
        vec_st(y23, i3+16, &(out[0]));
    } while (n-=2);
}

inline static void fft2_vsx_interleave(FFTComplex *z)
{
    FFTSample r1, i1;

    r1 = z[0].re - z[1].re;
    z[0].re += z[1].re;
    z[1].re = r1;

    i1 = z[0].im - z[1].im;
    z[0].im += z[1].im;
    z[1].im = i1;
 }

inline static void fft4_vsx_interleave(FFTComplex *z)
{
    vec_f a, b, c, d;
    float* out=  (float*)z;
    a = vec_ld(0, &(out[0]));
    b = vec_ld(byte_2complex, &(out[0]));

    c = vec_perm(a, b, vcprm(0,1,s2,s1));
    d = vec_perm(a, b, vcprm(2,3,s0,s3));
    a = vec_add(c, d);
    b = vec_sub(c, d);

    c = vec_perm(a, b, vcprm(0,1,s0,s1));
    d = vec_perm(a, b, vcprm(2,3,s3,s2));

    a = vec_add(c, d);
    b = vec_sub(c, d);
    vec_st(a, 0, &(out[0]));
    vec_st(b, byte_2complex, &(out[0]));
}

inline static void fft8_vsx_interleave(FFTComplex *z)
{
    vec_f vz0, vz1, vz2, vz3;
    vec_f x0, x1, x2, x3;
    vec_f x4, x5, x6, x7;
    vec_f x8, x9, x10, x11;
    vec_f x12, x13, x14, x15;
    vec_f x16, x17, x18, x19;
    vec_f x20, x21, x22, x23;
    vec_f x24, x25, x26, x27;
    vec_f x28, x29, x30, x31;
    vec_f x32, x33, x34;

    float* out=  (float*)z;
    vec_f vc1 = {sqrthalf, sqrthalf, sqrthalf, sqrthalf};

    vz0 = vec_ld(0, &(out[0]));
    vz1 = vec_ld(byte_2complex, &(out[0]));
    vz2 = vec_ld(byte_4complex, &(out[0]));
    vz3 = vec_ld(byte_6complex, &(out[0]));

    x0 = vec_perm(vz0, vz1, vcprm(0,1,s2,s1));
    x1 = vec_perm(vz0, vz1, vcprm(2,3,s0,s3));
    x2 = vec_perm(vz2, vz3, vcprm(2,1,s0,s1));
    x3 = vec_perm(vz2, vz3, vcprm(0,3,s2,s3));

    x4 = vec_add(x0, x1);
    x5 = vec_sub(x0, x1);
    x6 = vec_add(x2, x3);
    x7 = vec_sub(x2, x3);

    x8 = vec_perm(x4, x5, vcprm(0,1,s0,s1));
    x9 = vec_perm(x4, x5, vcprm(2,3,s3,s2));
    x10 = vec_perm(x6, x7, vcprm(2,1,s2,s1));
    x11 = vec_perm(x6, x7, vcprm(0,3,s0,s3));

    x12 = vec_add(x8, x9);
    x13 = vec_sub(x8, x9);
    x14 = vec_add(x10, x11);
    x15 = vec_sub(x10, x11);
    x16 = vec_perm(x12, x13, vcprm(0,s0,1,s1));
    x17 = vec_perm(x14, x15, vcprm(0,s0,1,s1));
    x18 = vec_perm(x16, x17, vcprm(s0,s3,s2,s1));
    x19 = vec_add(x16, x18); // z0.r  z2.r  z0.i  z2.i
    x20 = vec_sub(x16, x18); // z4.r  z6.r  z4.i  z6.i

    x21 = vec_perm(x12, x13, vcprm(2,s2,3,s3));
    x22 = vec_perm(x14, x15, vcprm(2,3,s2,s3));
    x23 = vec_perm(x14, x15, vcprm(3,2,s3,s2));
    x24 = vec_add(x22, x23);
    x25 = vec_sub(x22, x23);
    x26 = vec_mul( vec_perm(x24, x25, vcprm(2,s2,0,s0)), vc1);

    x27 = vec_add(x21, x26); // z1.r  z7.r z1.i z3.i
    x28 = vec_sub(x21, x26); //z5.r  z3.r z5.i z7.i

    x29 = vec_perm(x19, x27, vcprm(0,2,s0,s2)); // z0.r  z0.i  z1.r  z1.i
    x30 = vec_perm(x19, x27, vcprm(1,3,s1,s3)); // z2.r  z2.i  z7.r  z3.i
    x31 = vec_perm(x20, x28, vcprm(0,2,s0,s2)); // z4.r  z4.i  z5.r  z5.i
    x32 = vec_perm(x20, x28, vcprm(1,3,s1,s3)); // z6.r  z6.i  z3.r  z7.i
    x33 = vec_perm(x30, x32, vcprm(0,1,s2,3));  // z2.r  z2.i  z3.r  z3.i
    x34 = vec_perm(x30, x32, vcprm(s0,s1,2,s3)); // z6.r  z6.i  z7.r  z7.i

    vec_st(x29, 0, &(out[0]));
    vec_st(x33, byte_2complex, &(out[0]));
    vec_st(x31, byte_4complex, &(out[0]));
    vec_st(x34, byte_6complex, &(out[0]));
}

inline static void fft16_vsx_interleave(FFTComplex *z)
{
    float* out=  (float*)z;
    vec_f vc0 = {sqrthalf, sqrthalf, sqrthalf, sqrthalf};
    vec_f vc1 = {ff_cos_16[1], ff_cos_16[1], ff_cos_16[1], ff_cos_16[1]};
    vec_f vc2 = {ff_cos_16[3], ff_cos_16[3], ff_cos_16[3], ff_cos_16[3]};
    vec_f vz0, vz1, vz2, vz3;
    vec_f vz4, vz5, vz6, vz7;
    vec_f x0, x1, x2, x3;
    vec_f x4, x5, x6, x7;
    vec_f x8, x9, x10, x11;
    vec_f x12, x13, x14, x15;
    vec_f x16, x17, x18, x19;
    vec_f x20, x21, x22, x23;
    vec_f x24, x25, x26, x27;
    vec_f x28, x29, x30, x31;
    vec_f x32, x33, x34, x35;
    vec_f x36, x37, x38, x39;
    vec_f x40, x41, x42, x43;
    vec_f x44, x45, x46, x47;
    vec_f x48, x49, x50, x51;
    vec_f x52, x53, x54, x55;
    vec_f x56, x57, x58, x59;
    vec_f x60, x61, x62, x63;
    vec_f x64, x65, x66, x67;
    vec_f x68, x69, x70, x71;
    vec_f x72, x73, x74, x75;
    vec_f x76, x77, x78, x79;
    vec_f x80, x81, x82, x83;
    vec_f x84, x85, x86;

    vz0 = vec_ld(0, &(out[0]));
    vz1 = vec_ld(byte_2complex, &(out[0]));
    vz2 = vec_ld(byte_4complex, &(out[0]));
    vz3 = vec_ld(byte_6complex, &(out[0]));
    vz4 = vec_ld(byte_8complex, &(out[0]));
    vz5 = vec_ld(byte_10complex, &(out[0]));
    vz6 = vec_ld(byte_12complex, &(out[0]));
    vz7 = vec_ld(byte_14complex, &(out[0]));

    x0 = vec_perm(vz0, vz1, vcprm(0,1,s2,s1));
    x1 = vec_perm(vz0, vz1, vcprm(2,3,s0,s3));
    x2 = vec_perm(vz2, vz3, vcprm(0,1,s0,s1));
    x3 = vec_perm(vz2, vz3, vcprm(2,3,s2,s3));

    x4 = vec_perm(vz4, vz5, vcprm(0,1,s2,s1));
    x5 = vec_perm(vz4, vz5, vcprm(2,3,s0,s3));
    x6 = vec_perm(vz6, vz7, vcprm(0,1,s2,s1));
    x7 = vec_perm(vz6, vz7, vcprm(2,3,s0,s3));

    x8 = vec_add(x0, x1);
    x9 = vec_sub(x0, x1);
    x10 = vec_add(x2, x3);
    x11 = vec_sub(x2, x3);

    x12 = vec_add(x4, x5);
    x13 = vec_sub(x4, x5);
    x14 = vec_add(x6, x7);
    x15 = vec_sub(x6, x7);

    x16 = vec_perm(x8, x9, vcprm(0,1,s0,s1));
    x17 = vec_perm(x8, x9, vcprm(2,3,s3,s2));
    x18 = vec_perm(x10, x11, vcprm(2,1,s1,s2));
    x19 = vec_perm(x10, x11, vcprm(0,3,s0,s3));
    x20 = vec_perm(x12, x14, vcprm(0,1,s0, s1));
    x21 = vec_perm(x12, x14, vcprm(2,3,s2,s3));
    x22 = vec_perm(x13, x15, vcprm(0,1,s0,s1));
    x23 = vec_perm(x13, x15, vcprm(3,2,s3,s2));

    x24 = vec_add(x16, x17);
    x25 = vec_sub(x16, x17);
    x26 = vec_add(x18, x19);
    x27 = vec_sub(x18, x19);
    x28 = vec_add(x20, x21);
    x29 = vec_sub(x20, x21);
    x30 = vec_add(x22, x23);
    x31 = vec_sub(x22, x23);

    x32 = vec_add(x24, x26);
    x33 = vec_sub(x24, x26);
    x34 = vec_perm(x32, x33, vcprm(0,1,s0,s1));

    x35 = vec_perm(x28, x29, vcprm(2,1,s1,s2));
    x36 = vec_perm(x28, x29, vcprm(0,3,s0,s3));
    x37 = vec_add(x35, x36);
    x38 = vec_sub(x35, x36);
    x39 = vec_perm(x37, x38, vcprm(0,1,s1,s0));

    x40 = vec_perm(x27, x38, vcprm(3,2,s2,s3));
    x41 = vec_perm(x26,  x37, vcprm(2,3,s3,s2));
    x42 = vec_add(x40, x41);
    x43 = vec_sub(x40, x41);
    x44 = vec_mul(x42, vc0);
    x45 = vec_mul(x43, vc0);

    x46 = vec_add(x34, x39);  // z0.r  z0.i  z4.r  z4.i
    x47 = vec_sub(x34, x39);  // z8.r  z8.i  z12.r  z12.i

    x48 = vec_perm(x30, x31, vcprm(2,1,s1,s2));
    x49 = vec_perm(x30, x31, vcprm(0,3,s3,s0));
    x50 = vec_add(x48, x49);
    x51 = vec_sub(x48, x49);
    x52 = vec_mul(x50, vc1);
    x53 = vec_mul(x50, vc2);
    x54 = vec_mul(x51, vc1);
    x55 = vec_mul(x51, vc2);

    x56 = vec_perm(x24, x25, vcprm(2,3,s2,s3));
    x57 = vec_perm(x44, x45, vcprm(0,1,s1,s0));
    x58 = vec_add(x56, x57);
    x59 = vec_sub(x56, x57);

    x60 = vec_perm(x54, x55, vcprm(1,0,3,2));
    x61 = vec_perm(x54, x55, vcprm(s1,s0,s3,s2));
    x62 = vec_add(x52, x61);
    x63 = vec_sub(x52, x61);
    x64 = vec_add(x60, x53);
    x65 = vec_sub(x60, x53);
    x66 = vec_perm(x62, x64, vcprm(0,1,s3,s2));
    x67 = vec_perm(x63, x65, vcprm(s0,s1,3,2));

    x68 = vec_add(x58, x66); // z1.r    z1.i  z3.r    z3.i
    x69 = vec_sub(x58, x66); // z9.r    z9.i  z11.r  z11.i
    x70 = vec_add(x59, x67); // z5.r    z5.i  z15.r  z15.i
    x71 = vec_sub(x59, x67); // z13.r  z13.i z7.r   z7.i

    x72 = vec_perm(x25, x27, vcprm(s1,s0,s2,s3));
    x73 = vec_add(x25, x72);
    x74 = vec_sub(x25, x72);
    x75 = vec_perm(x73, x74, vcprm(0,1,s0,s1));
    x76 = vec_perm(x44, x45, vcprm(3,2,s2,s3));
    x77 = vec_add(x75, x76); // z2.r   z2.i    z6.r    z6.i
    x78 = vec_sub(x75, x76); // z10.r  z10.i  z14.r  z14.i

    x79 = vec_perm(x46, x68, vcprm(0,1,s0,s1)); // z0.r  z0.i  z1.r  z1.i
    x80 = vec_perm(x77, x68, vcprm(0,1,s2,s3)); // z2.r  z2.i  z3.r  z3.i
    x81 = vec_perm(x46, x70, vcprm(2,3,s0,s1)); // z4.r  z4.i  z5.r  z5.i
    x82 = vec_perm(x71, x77, vcprm(s2,s3,2,3)); // z6.r  z6.i  z7.r  z7.i
    vec_st(x79, 0, &(out[0]));
    vec_st(x80, byte_2complex, &(out[0]));
    vec_st(x81, byte_4complex, &(out[0]));
    vec_st(x82, byte_6complex, &(out[0]));
    x83 = vec_perm(x47, x69, vcprm(0,1,s0,s1)); // z8.r  z8.i  z9.r  z9.i
    x84 = vec_perm(x78, x69, vcprm(0,1,s2,s3)); // z10.r  z10.i  z11.r  z11.i
    x85 = vec_perm(x47, x71, vcprm(2,3,s0,s1)); // z12.r  z12.i  z13.r  z13.i
    x86 = vec_perm(x70, x78, vcprm(s2,s3,2,3)); // z14.r  z14.i  z15.r  z15.i
    vec_st(x83, byte_8complex, &(out[0]));
    vec_st(x84, byte_10complex, &(out[0]));
    vec_st(x85, byte_12complex, &(out[0]));
    vec_st(x86, byte_14complex, &(out[0]));
}

inline static void fft4_vsx(FFTComplex *z)
{
    vec_f a, b, c, d;
    float* out=  (float*)z;
    a = vec_ld(0, &(out[0]));
    b = vec_ld(byte_2complex, &(out[0]));

    c = vec_perm(a, b, vcprm(0,1,s2,s1));
    d = vec_perm(a, b, vcprm(2,3,s0,s3));
    a = vec_add(c, d);
    b = vec_sub(c, d);

    c = vec_perm(a,b, vcprm(0,s0,1,s1));
    d = vec_perm(a, b, vcprm(2,s3,3,s2));

    a = vec_add(c, d);
    b = vec_sub(c, d);

    c = vec_perm(a, b, vcprm(0,1,s0,s1));
    d = vec_perm(a, b, vcprm(2,3,s2,s3));

    vec_st(c, 0, &(out[0]));
    vec_st(d, byte_2complex, &(out[0]));
    return;
}

inline static void fft8_vsx(FFTComplex *z)
{
    vec_f vz0, vz1, vz2, vz3;
    vec_f vz4, vz5, vz6, vz7, vz8;

    float* out=  (float*)z;
    vec_f vc0 = {0.0, 0.0, 0.0, 0.0};
    vec_f vc1 = {-sqrthalf, sqrthalf, sqrthalf, -sqrthalf};
    vec_f vc2 = {sqrthalf, sqrthalf, sqrthalf, sqrthalf};

    vz0 = vec_ld(0, &(out[0]));
    vz1 = vec_ld(byte_2complex, &(out[0]));
    vz2 = vec_ld(byte_4complex, &(out[0]));
    vz3 = vec_ld(byte_6complex, &(out[0]));

    vz6 = vec_perm(vz2, vz3, vcprm(0,s0,1,s1));
    vz7 = vec_perm(vz2, vz3, vcprm(2,s2,3,s3));
    vz4 = vec_perm(vz0, vz1, vcprm(0,1,s2,s1));
    vz5 = vec_perm(vz0, vz1, vcprm(2,3,s0,s3));

    vz2 = vec_add(vz6, vz7);
    vz3 = vec_sub(vz6, vz7);
    vz8 = vec_perm(vz3, vz3, vcprm(2,3,0,1));

    vz0 = vec_add(vz4, vz5);
    vz1 = vec_sub(vz4, vz5);

    vz3 = vec_madd(vz3, vc1, vc0);
    vz3 = vec_madd(vz8, vc2, vz3);

    vz4 = vec_perm(vz0, vz1, vcprm(0,s0,1,s1));
    vz5 = vec_perm(vz0, vz1, vcprm(2,s3,3,s2));
    vz6 = vec_perm(vz2, vz3, vcprm(1,2,s3,s0));
    vz7 = vec_perm(vz2, vz3, vcprm(0,3,s2,s1));

    vz0 = vec_add(vz4, vz5);
    vz1 = vec_sub(vz4, vz5);
    vz2 = vec_add(vz6, vz7);
    vz3 = vec_sub(vz6, vz7);

    vz4 = vec_perm(vz0, vz1, vcprm(0,1,s0,s1));
    vz5 = vec_perm(vz0, vz1, vcprm(2,3,s2,s3));
    vz6 = vec_perm(vz2, vz3, vcprm(0,2,s1,s3));
    vz7 = vec_perm(vz2, vz3, vcprm(1,3,s0,s2));


    vz2 = vec_sub(vz4, vz6);
    vz3 = vec_sub(vz5, vz7);

    vz0 = vec_add(vz4, vz6);
    vz1 = vec_add(vz5, vz7);

    vec_st(vz0, 0, &(out[0]));
    vec_st(vz1, byte_2complex, &(out[0]));
    vec_st(vz2, byte_4complex, &(out[0]));
    vec_st(vz3, byte_6complex, &(out[0]));
    return;
}

inline static void fft16_vsx(FFTComplex *z)
{
    float* out=  (float*)z;
    vec_f vc0 = {0.0, 0.0, 0.0, 0.0};
    vec_f vc1 = {-sqrthalf, sqrthalf, sqrthalf, -sqrthalf};
    vec_f vc2 = {sqrthalf, sqrthalf, sqrthalf, sqrthalf};
    vec_f vc3 = {1.0, 0.92387953, sqrthalf, 0.38268343};
    vec_f vc4 = {0.0, 0.38268343, sqrthalf, 0.92387953};
    vec_f vc5 = {-0.0, -0.38268343, -sqrthalf, -0.92387953};

    vec_f vz0, vz1, vz2, vz3;
    vec_f vz4, vz5, vz6, vz7;
    vec_f vz8, vz9, vz10, vz11;
    vec_f vz12, vz13;

    vz0 = vec_ld(byte_8complex, &(out[0]));
    vz1 = vec_ld(byte_10complex, &(out[0]));
    vz2 = vec_ld(byte_12complex, &(out[0]));
    vz3 = vec_ld(byte_14complex, &(out[0]));

    vz4 = vec_perm(vz0, vz1, vcprm(0,1,s2,s1));
    vz5 = vec_perm(vz0, vz1, vcprm(2,3,s0,s3));
    vz6 = vec_perm(vz2, vz3, vcprm(0,1,s2,s1));
    vz7 = vec_perm(vz2, vz3, vcprm(2,3,s0,s3));

    vz0 = vec_add(vz4, vz5);
    vz1= vec_sub(vz4, vz5);
    vz2 = vec_add(vz6, vz7);
    vz3 = vec_sub(vz6, vz7);

    vz4 = vec_perm(vz0, vz1, vcprm(0,s0,1,s1));
    vz5 = vec_perm(vz0, vz1, vcprm(2,s3,3,s2));
    vz6 = vec_perm(vz2, vz3, vcprm(0,s0,1,s1));
    vz7 = vec_perm(vz2, vz3, vcprm(2,s3,3,s2));

    vz0 = vec_add(vz4, vz5);
    vz1 = vec_sub(vz4, vz5);
    vz2 = vec_add(vz6, vz7);
    vz3 = vec_sub(vz6, vz7);

    vz4 = vec_perm(vz0, vz1, vcprm(0,1,s0,s1));
    vz5 = vec_perm(vz0, vz1, vcprm(2,3,s2,s3));

    vz6 = vec_perm(vz2, vz3, vcprm(0,1,s0,s1));
    vz7 = vec_perm(vz2, vz3, vcprm(2,3,s2,s3));

    vz0 = vec_ld(0, &(out[0]));
    vz1 = vec_ld(byte_2complex, &(out[0]));
    vz2 = vec_ld(byte_4complex, &(out[0]));
    vz3 = vec_ld(byte_6complex, &(out[0]));
    vz10 = vec_perm(vz2, vz3, vcprm(0,s0,1,s1));
    vz11 = vec_perm(vz2, vz3, vcprm(2,s2,3,s3));
    vz8 = vec_perm(vz0, vz1, vcprm(0,1,s2,s1));
    vz9 = vec_perm(vz0, vz1, vcprm(2,3,s0,s3));

    vz2 = vec_add(vz10, vz11);
    vz3 = vec_sub(vz10, vz11);
    vz12 = vec_perm(vz3, vz3, vcprm(2,3,0,1));
    vz0 = vec_add(vz8, vz9);
    vz1 = vec_sub(vz8, vz9);

    vz3 = vec_madd(vz3, vc1, vc0);
    vz3 = vec_madd(vz12, vc2, vz3);
    vz8 = vec_perm(vz0, vz1, vcprm(0,s0,1,s1));
    vz9 = vec_perm(vz0, vz1, vcprm(2,s3,3,s2));
    vz10 = vec_perm(vz2, vz3, vcprm(1,2,s3,s0));
    vz11 = vec_perm(vz2, vz3, vcprm(0,3,s2,s1));

    vz0 = vec_add(vz8, vz9);
    vz1 = vec_sub(vz8, vz9);
    vz2 = vec_add(vz10, vz11);
    vz3 = vec_sub(vz10, vz11);

    vz8 = vec_perm(vz0, vz1, vcprm(0,1,s0,s1));
    vz9 = vec_perm(vz0, vz1, vcprm(2,3,s2,s3));
    vz10 = vec_perm(vz2, vz3, vcprm(0,2,s1,s3));
    vz11 = vec_perm(vz2, vz3, vcprm(1,3,s0,s2));

    vz2 = vec_sub(vz8, vz10);
    vz3 = vec_sub(vz9, vz11);
    vz0 = vec_add(vz8, vz10);
    vz1 = vec_add(vz9, vz11);

    vz8 = vec_madd(vz4, vc3, vc0);
    vz9 = vec_madd(vz5, vc3, vc0);
    vz10 = vec_madd(vz6, vc3, vc0);
    vz11 = vec_madd(vz7, vc3, vc0);

    vz8 = vec_madd(vz5, vc4, vz8);
    vz9 = vec_madd(vz4, vc5, vz9);
    vz10 = vec_madd(vz7, vc5, vz10);
    vz11 = vec_madd(vz6, vc4, vz11);

    vz12 = vec_sub(vz10, vz8);
    vz10 = vec_add(vz10, vz8);

    vz13 = vec_sub(vz9, vz11);
    vz11 = vec_add(vz9, vz11);

    vz4 = vec_sub(vz0, vz10);
    vz0 = vec_add(vz0, vz10);

    vz7= vec_sub(vz3, vz12);
    vz3= vec_add(vz3, vz12);

    vz5 = vec_sub(vz1, vz11);
    vz1 = vec_add(vz1, vz11);

    vz6 = vec_sub(vz2, vz13);
    vz2 = vec_add(vz2, vz13);

    vec_st(vz0, 0, &(out[0]));
    vec_st(vz1, byte_2complex, &(out[0]));
    vec_st(vz2, byte_4complex, &(out[0]));
    vec_st(vz3, byte_6complex, &(out[0]));
    vec_st(vz4, byte_8complex, &(out[0]));
    vec_st(vz5, byte_10complex, &(out[0]));
    vec_st(vz6, byte_12complex, &(out[0]));
    vec_st(vz7, byte_14complex, &(out[0]));
    return;

}
inline static void pass_vsx(FFTComplex * z, const FFTSample * wre, unsigned int n)
{
    int o1 = n<<1;
    int o2 = n<<2;
    int o3 = o1+o2;
    int i1, i2, i3;
    FFTSample* out = (FFTSample*)z;
    const FFTSample *wim = wre+o1;
    vec_f v0, v1, v2, v3;
    vec_f v4, v5, v6, v7;
    vec_f v8, v9, v10, v11;
    vec_f v12, v13;

    n = n-2;
    i1 = o1*sizeof(FFTComplex);
    i2 = o2*sizeof(FFTComplex);
    i3 = o3*sizeof(FFTComplex);

    v8 = vec_ld(0, &(wre[0]));
    v10 = vec_ld(0, &(wim[0]));
    v9 = vec_ld(0, &(wim[-4]));
    v9 = vec_perm(v9, v10, vcprm(s0,3,2,1));

    v4 = vec_ld(i2, &(out[0]));
    v5 = vec_ld(i2+16, &(out[0]));
    v6 = vec_ld(i3, &(out[0]));
    v7 = vec_ld(i3+16, &(out[0]));
    v10 = vec_mul(v4, v8); // r2*wre
    v11 = vec_mul(v5, v8); // i2*wre
    v12 = vec_mul(v6, v8); // r3*wre
    v13 = vec_mul(v7, v8); // i3*wre

    v0 = vec_ld(0, &(out[0])); // r0
    v3 = vec_ld(i1+16, &(out[0])); // i1
    v10 = vec_madd(v5, v9, v10); // r2*wim
    v11 = vec_nmsub(v4, v9, v11); // i2*wim
    v12 = vec_nmsub(v7, v9, v12); // r3*wim
    v13 = vec_madd(v6, v9, v13); // i3*wim

    v1 = vec_ld(16, &(out[0])); // i0
    v2 = vec_ld(i1, &(out[0])); // r1
    v8 = vec_sub(v12, v10);
    v12 = vec_add(v12, v10);
    v9 = vec_sub(v11, v13);
    v13 = vec_add(v11, v13);
    v4 = vec_sub(v0, v12);
    v0 = vec_add(v0, v12);
    v7 = vec_sub(v3, v8);
    v3 = vec_add(v3, v8);

    vec_st(v0, 0, &(out[0])); // r0
    vec_st(v3, i1+16, &(out[0])); // i1
    vec_st(v4, i2, &(out[0])); // r2
    vec_st(v7, i3+16, &(out[0]));// i3

    v5 = vec_sub(v1, v13);
    v1 = vec_add(v1, v13);
    v6 = vec_sub(v2, v9);
    v2 = vec_add(v2, v9);

    vec_st(v1, 16, &(out[0])); // i0
    vec_st(v2, i1, &(out[0])); // r1
    vec_st(v5, i2+16, &(out[0])); // i2
    vec_st(v6, i3, &(out[0])); // r3

    do {
        out += 8;
        wre += 4;
        wim -= 4;

        v8 = vec_ld(0, &(wre[0]));
        v10 = vec_ld(0, &(wim[0]));
        v9 = vec_ld(0, &(wim[-4]));
        v9 = vec_perm(v9, v10, vcprm(s0,3,2,1));

        v4 = vec_ld(i2, &(out[0])); // r2
        v5 = vec_ld(i2+16, &(out[0])); // i2
        v6 = vec_ld(i3, &(out[0])); // r3
        v7 = vec_ld(i3+16, &(out[0]));// i3
        v10 = vec_mul(v4, v8); // r2*wre
        v11 = vec_mul(v5, v8); // i2*wre
        v12 = vec_mul(v6, v8); // r3*wre
        v13 = vec_mul(v7, v8); // i3*wre

        v0 = vec_ld(0, &(out[0])); // r0
        v3 = vec_ld(i1+16, &(out[0])); // i1
        v10 = vec_madd(v5, v9, v10); // r2*wim
        v11 = vec_nmsub(v4, v9, v11); // i2*wim
        v12 = vec_nmsub(v7, v9, v12); // r3*wim
        v13 = vec_madd(v6, v9, v13); // i3*wim

        v1 = vec_ld(16, &(out[0])); // i0
        v2 = vec_ld(i1, &(out[0])); // r1
        v8 = vec_sub(v12, v10);
        v12 = vec_add(v12, v10);
        v9 = vec_sub(v11, v13);
        v13 = vec_add(v11, v13);
        v4 = vec_sub(v0, v12);
        v0 = vec_add(v0, v12);
        v7 = vec_sub(v3, v8);
        v3 = vec_add(v3, v8);

        vec_st(v0, 0, &(out[0])); // r0
        vec_st(v3, i1+16, &(out[0])); // i1
        vec_st(v4, i2, &(out[0])); // r2
        vec_st(v7, i3+16, &(out[0])); // i3

        v5 = vec_sub(v1, v13);
        v1 = vec_add(v1, v13);
        v6 = vec_sub(v2, v9);
        v2 = vec_add(v2, v9);

        vec_st(v1, 16, &(out[0])); // i0
        vec_st(v2, i1, &(out[0])); // r1
        vec_st(v5, i2+16, &(out[0])); // i2
        vec_st(v6, i3, &(out[0])); // r3
    } while (n-=2);
}

#endif

#endif /* AVCODEC_PPC_FFT_VSX_H */
