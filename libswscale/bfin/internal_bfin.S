/*
 * Copyright (C) 2007 Marc Hoffman <marc.hoffman@analog.com>
 *                    April 20, 2007
 *
 * Blackfin video color space converter operations
 * convert I420 YV12 to RGB in various formats
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


/*
YUV420 to RGB565 conversion. This routine takes a YUV 420 planar macroblock
and converts it to RGB565. R:5 bits, G:6 bits, B:5 bits.. packed into shorts.


The following calculation is used for the conversion:

  r = clipz((y-oy)*cy  + crv*(v-128))
  g = clipz((y-oy)*cy  + cgv*(v-128) + cgu*(u-128))
  b = clipz((y-oy)*cy  + cbu*(u-128))

y,u,v are prescaled by a factor of 4 i.e. left-shifted to gain precision.


New factorization to eliminate the truncation error which was
occurring due to the byteop3p.


1) Use the bytop16m to subtract quad bytes we use this in U8 this
 then so the offsets need to be renormalized to 8bits.

2) Scale operands up by a factor of 4 not 8 because Blackfin
   multiplies include a shift.

3) Compute into the accumulators cy*yx0, cy*yx1.

4) Compute each of the linear equations:
     r = clipz((y - oy) * cy  + crv * (v - 128))

     g = clipz((y - oy) * cy  + cgv * (v - 128) + cgu * (u - 128))

     b = clipz((y - oy) * cy  + cbu * (u - 128))

   Reuse of the accumulators requires that we actually multiply
   twice once with addition and the second time with a subtraction.

   Because of this we need to compute the equations in the order R B
   then G saving the writes for B in the case of 24/32 bit color
   formats.

   API: yuv2rgb_kind (uint8_t *Y, uint8_t *U, uint8_t *V, int *out,
                      int dW, uint32_t *coeffs);

       A          B
       ---        ---
       i2 = cb    i3 = cr
       i1 = coeff i0 = y

Where coeffs have the following layout in memory.

uint32_t oy,oc,zero,cy,crv,rmask,cbu,bmask,cgu,cgv;

coeffs is a pointer to oy.

The {rgb} masks are only utilized by the 565 packing algorithm. Note the data
replication is used to simplify the internal algorithms for the dual Mac
architecture of BlackFin.

All routines are exported with _ff_bfin_ as a symbol prefix.

Rough performance gain compared against -O3:

2779809/1484290 187.28%

which translates to ~33c/pel to ~57c/pel for the reference vs 17.5
c/pel for the optimized implementations. Not sure why there is such a
huge variation on the reference codes on Blackfin I guess it must have
to do with the memory system.
*/

#define mL3 .text
#if defined(__FDPIC__) && CONFIG_SRAM
#define mL1 .l1.text
#else
#define mL1 mL3
#endif
#define MEM mL1

#define DEFUN(fname,where,interface) \
        .section where;              \
        .global _ff_bfin_ ## fname;  \
        .type _ff_bfin_ ## fname, STT_FUNC; \
        .align 8;                    \
        _ff_bfin_ ## fname

#define DEFUN_END(fname) \
        .size _ff_bfin_ ## fname, . - _ff_bfin_ ## fname


.text

#define COEFF_LEN        11*4
#define COEFF_REL_CY_OFF 4*4

#define ARG_OUT   20
#define ARG_W     24
#define ARG_COEFF 28

DEFUN(yuv2rgb565_line,MEM,
   (uint8_t *Y, uint8_t *U, uint8_t *V, int *out, int dW, uint32_t *coeffs)):
        link 0;
        [--sp] = (r7:4);
        p1 = [fp+ARG_OUT];
        r3 = [fp+ARG_W];

        i0 = r0;
        i2 = r1;
        i3 = r2;

        r0 = [fp+ARG_COEFF];
        i1 = r0;
        b1 = i1;
        l1 = COEFF_LEN;
        m0 = COEFF_REL_CY_OFF;
        p0 = r3;

        r0   = [i0++];         // 2Y
        r1.l = w[i2++];        // 2u
        r1.h = w[i3++];        // 2v
        p0 = p0>>2;

        lsetup (.L0565, .L1565) lc0 = p0;

        /*
           uint32_t oy,oc,zero,cy,crv,rmask,cbu,bmask,cgu,cgv
           r0 -- used to load 4ys
           r1 -- used to load 2us,2vs
           r4 -- y3,y2
           r5 -- y1,y0
           r6 -- u1,u0
           r7 -- v1,v0
        */
                                                              r2=[i1++]; // oy
.L0565:
        /*
        rrrrrrrr gggggggg bbbbbbbb
         5432109876543210
                    bbbbb >>3
              gggggggg    <<3
         rrrrrrrr         <<8
         rrrrrggggggbbbbb
        */
        (r4,r5) = byteop16m (r1:0, r3:2)                   || r3=[i1++]; // oc
        (r7,r6) = byteop16m (r1:0, r3:2) (r);
        r5 = r5 << 2 (v);                                                // y1,y0
        r4 = r4 << 2 (v);                                                // y3,y2
        r6 = r6 << 2 (v)                                   || r0=[i1++]; // u1,u0, r0=zero
        r7 = r7 << 2 (v)                                   || r1=[i1++]; // v1,v0  r1=cy
        /* Y' = y*cy */
        a1 = r1.h*r5.h, a0 = r1.l*r5.l                     || r1=[i1++]; // crv

        /* R = Y+ crv*(Cr-128) */
        r2.h = (a1 += r1.h*r7.l), r2.l = (a0 += r1.l*r7.l);
                a1 -= r1.h*r7.l,          a0 -= r1.l*r7.l  || r5=[i1++]; // rmask
        r2 = byteop3p(r3:2, r1:0)(LO)                      || r1=[i1++]; // cbu
        r2 = r2 >> 3 (v);
        r3 = r2 & r5;

        /* B = Y+ cbu*(Cb-128) */
        r2.h = (a1 += r1.h*r6.l), r2.l = (a0 += r1.l*r6.l);
                a1 -= r1.h*r6.l,          a0 -= r1.l*r6.l  || r5=[i1++]; // bmask
        r2 = byteop3p(r3:2, r1:0)(LO)                      || r1=[i1++]; // cgu
        r2 = r2 << 8 (v);
        r2 = r2 & r5;
        r3 = r3 | r2;

        /* G = Y+ cgu*(Cb-128)+cgv*(Cr-128) */
                a1 += r1.h*r6.l,          a0 += r1.l*r6.l  || r1=[i1++]; // cgv
        r2.h = (a1 += r1.h*r7.l), r2.l = (a0 += r1.l*r7.l);
        r2 = byteop3p(r3:2, r1:0)(LO)                      || r5=[i1++m0]; // gmask
        r2 = r2 << 3 (v);
        r2 = r2 & r5;
        r3 = r3 | r2;
        [p1++]=r3                                          || r1=[i1++]; // cy

        /* Y' = y*cy */

        a1 = r1.h*r4.h, a0 = r1.l*r4.l                     || r1=[i1++]; // crv

        /* R = Y+ crv*(Cr-128) */
        r2.h = (a1 += r1.h*r7.h), r2.l = (a0 += r1.l*r7.h);
                a1 -= r1.h*r7.h,          a0 -= r1.l*r7.h  || r5=[i1++]; // rmask
        r2 = byteop3p(r3:2, r1:0)(LO)                      || r1=[i1++]; // cbu
        r2 = r2 >> 3 (v);
        r3 = r2 & r5;

        /* B = Y+ cbu*(Cb-128) */
        r2.h = (a1 += r1.h*r6.h), r2.l = (a0 += r1.l*r6.h);
                a1 -= r1.h*r6.h,          a0 -= r1.l*r6.h  || r5=[i1++]; // bmask
        r2 = byteop3p(r3:2, r1:0)(LO)                      || r1=[i1++]; // cgu
        r2 = r2 << 8 (v);
        r2 = r2 & r5;
        r3 = r3 | r2;

        /* G = Y+ cgu*(Cb-128)+cgv*(Cr-128) */
                a1 += r1.h*r6.h,          a0 += r1.l*r6.h  || r1=[i1++]; // cgv
        r2.h = (a1 += r1.h*r7.h), r2.l = (a0 += r1.l*r7.h) || r5=[i1++]; // gmask
        r2 = byteop3p(r3:2, r1:0)(LO)                      || r0   =  [i0++];        // 2Y
        r2 = r2 << 3 (v)                                   || r1.l = w[i2++];        // 2u
        r2 = r2 & r5;
        r3 = r3 | r2;
        [p1++]=r3                                          || r1.h = w[i3++];        // 2v
.L1565:                                                       r2=[i1++]; // oy

        l1 = 0;

        (r7:4) = [sp++];
        unlink;
        rts;
DEFUN_END(yuv2rgb565_line)

DEFUN(yuv2rgb555_line,MEM,
   (uint8_t *Y, uint8_t *U, uint8_t *V, int *out, int dW, uint32_t *coeffs)):
        link 0;
        [--sp] = (r7:4);
        p1 = [fp+ARG_OUT];
        r3 = [fp+ARG_W];

        i0 = r0;
        i2 = r1;
        i3 = r2;

        r0 = [fp+ARG_COEFF];
        i1 = r0;
        b1 = i1;
        l1 = COEFF_LEN;
        m0 = COEFF_REL_CY_OFF;
        p0 = r3;

        r0   = [i0++];         // 2Y
        r1.l = w[i2++];        // 2u
        r1.h = w[i3++];        // 2v
        p0 = p0>>2;

        lsetup (.L0555, .L1555) lc0 = p0;

        /*
           uint32_t oy,oc,zero,cy,crv,rmask,cbu,bmask,cgu,cgv
           r0 -- used to load 4ys
           r1 -- used to load 2us,2vs
           r4 -- y3,y2
           r5 -- y1,y0
           r6 -- u1,u0
           r7 -- v1,v0
        */
                                                              r2=[i1++]; // oy
.L0555:
        /*
        rrrrrrrr gggggggg bbbbbbbb
         5432109876543210
                    bbbbb >>3
               gggggggg   <<2
          rrrrrrrr        <<7
         xrrrrrgggggbbbbb
        */

        (r4,r5) = byteop16m (r1:0, r3:2)                   || r3=[i1++]; // oc
        (r7,r6) = byteop16m (r1:0, r3:2) (r);
        r5 = r5 << 2 (v);                                                // y1,y0
        r4 = r4 << 2 (v);                                                // y3,y2
        r6 = r6 << 2 (v)                                   || r0=[i1++]; // u1,u0, r0=zero
        r7 = r7 << 2 (v)                                   || r1=[i1++]; // v1,v0  r1=cy
        /* Y' = y*cy */
        a1 = r1.h*r5.h, a0 = r1.l*r5.l                     || r1=[i1++]; // crv

        /* R = Y+ crv*(Cr-128) */
        r2.h = (a1 += r1.h*r7.l), r2.l = (a0 += r1.l*r7.l);
                a1 -= r1.h*r7.l,          a0 -= r1.l*r7.l  || r5=[i1++]; // rmask
        r2 = byteop3p(r3:2, r1:0)(LO)                      || r1=[i1++]; // cbu
        r2 = r2 >> 3 (v);
        r3 = r2 & r5;

        /* B = Y+ cbu*(Cb-128) */
        r2.h = (a1 += r1.h*r6.l), r2.l = (a0 += r1.l*r6.l);
                a1 -= r1.h*r6.l,          a0 -= r1.l*r6.l  || r5=[i1++]; // bmask
        r2 = byteop3p(r3:2, r1:0)(LO)                      || r1=[i1++]; // cgu
        r2 = r2 << 7 (v);
        r2 = r2 & r5;
        r3 = r3 | r2;

        /* G = Y+ cgu*(Cb-128)+cgv*(Cr-128) */
                a1 += r1.h*r6.l,          a0 += r1.l*r6.l  || r1=[i1++]; // cgv
        r2.h = (a1 += r1.h*r7.l), r2.l = (a0 += r1.l*r7.l);
        r2 = byteop3p(r3:2, r1:0)(LO)                      || r5=[i1++m0]; // gmask
        r2 = r2 << 2 (v);
        r2 = r2 & r5;
        r3 = r3 | r2;
        [p1++]=r3                                          || r1=[i1++]; // cy

        /* Y' = y*cy */

        a1 = r1.h*r4.h, a0 = r1.l*r4.l                     || r1=[i1++]; // crv

        /* R = Y+ crv*(Cr-128) */
        r2.h = (a1 += r1.h*r7.h), r2.l = (a0 += r1.l*r7.h);
                a1 -= r1.h*r7.h,          a0 -= r1.l*r7.h  || r5=[i1++]; // rmask
        r2 = byteop3p(r3:2, r1:0)(LO)                      || r1=[i1++]; // cbu
        r2 = r2 >> 3 (v);
        r3 = r2 & r5;

        /* B = Y+ cbu*(Cb-128) */
        r2.h = (a1 += r1.h*r6.h), r2.l = (a0 += r1.l*r6.h);
                a1 -= r1.h*r6.h,          a0 -= r1.l*r6.h  || r5=[i1++]; // bmask
        r2 = byteop3p(r3:2, r1:0)(LO)                      || r1=[i1++]; // cgu
        r2 = r2 << 7 (v);
        r2 = r2 & r5;
        r3 = r3 | r2;

        /* G = Y+ cgu*(Cb-128)+cgv*(Cr-128) */
                a1 += r1.h*r6.h,          a0 += r1.l*r6.h  || r1=[i1++]; // cgv
        r2.h = (a1 += r1.h*r7.h), r2.l = (a0 += r1.l*r7.h) || r5=[i1++]; // gmask
        r2 = byteop3p(r3:2, r1:0)(LO)                      || r0=[i0++];     // 4Y
        r2 = r2 << 2 (v)                                   || r1.l=w[i2++];  // 2u
        r2 = r2 & r5;
        r3 = r3 | r2;
        [p1++]=r3                                          || r1.h=w[i3++]; // 2v

.L1555:                                                       r2=[i1++]; // oy

        l1 = 0;

        (r7:4) = [sp++];
        unlink;
        rts;
DEFUN_END(yuv2rgb555_line)

DEFUN(yuv2rgb24_line,MEM,
   (uint8_t *Y, uint8_t *U, uint8_t *V, int *out, int dW, uint32_t *coeffs)):
        link 0;
        [--sp] = (r7:4);
        p1 = [fp+ARG_OUT];
        r3 = [fp+ARG_W];
        p2 = p1;
        p2 += 3;

        i0 = r0;
        i2 = r1;
        i3 = r2;

        r0 = [fp+ARG_COEFF]; // coeff buffer
        i1 = r0;
        b1 = i1;
        l1 = COEFF_LEN;
        m0 = COEFF_REL_CY_OFF;
        p0 = r3;

        r0   = [i0++];         // 2Y
        r1.l = w[i2++];        // 2u
        r1.h = w[i3++];        // 2v
        p0 = p0>>2;

        lsetup (.L0888, .L1888) lc0 = p0;

        /*
           uint32_t oy,oc,zero,cy,crv,rmask,cbu,bmask,cgu,cgv
           r0 -- used to load 4ys
           r1 -- used to load 2us,2vs
           r4 -- y3,y2
           r5 -- y1,y0
           r6 -- u1,u0
           r7 -- v1,v0
        */
                                                              r2=[i1++]; // oy
.L0888:
        (r4,r5) = byteop16m (r1:0, r3:2)                   || r3=[i1++]; // oc
        (r7,r6) = byteop16m (r1:0, r3:2) (r);
        r5 = r5 << 2 (v);               // y1,y0
        r4 = r4 << 2 (v);               // y3,y2
        r6 = r6 << 2 (v) || r0=[i1++];  // u1,u0, r0=zero
        r7 = r7 << 2 (v) || r1=[i1++];  // v1,v0  r1=cy

        /* Y' = y*cy */
        a1 = r1.h*r5.h, a0 = r1.l*r5.l                     || r1=[i1++]; // crv

        /* R = Y+ crv*(Cr-128) */
        r2.h = (a1 += r1.h*r7.l), r2.l = (a0 += r1.l*r7.l);
                a1 -= r1.h*r7.l,          a0 -= r1.l*r7.l  || r5=[i1++]; // rmask
        r2 = byteop3p(r3:2, r1:0)(LO)                      || r1=[i1++]; // cbu
        r2=r2>>16 || B[p1++]=r2;
                     B[p2++]=r2;

        /* B = Y+ cbu*(Cb-128) */
        r2.h = (a1 += r1.h*r6.l), r2.l = (a0 += r1.l*r6.l);
                a1 -= r1.h*r6.l,          a0 -= r1.l*r6.l  || r5=[i1++]; // bmask
        r3 = byteop3p(r3:2, r1:0)(LO)                      || r1=[i1++]; // cgu

        /* G = Y+ cgu*(Cb-128)+cgv*(Cr-128) */
                a1 += r1.h*r6.l,          a0 += r1.l*r6.l  || r1=[i1++]; // cgv
        r2.h = (a1 += r1.h*r7.l), r2.l = (a0 += r1.l*r7.l);
        r2 = byteop3p(r3:2, r1:0)(LO)                      || r5=[i1++m0]; // gmask, oy,cy,zero

        r2=r2>>16 || B[p1++]=r2;
                     B[p2++]=r2;

        r3=r3>>16 || B[p1++]=r3;
                     B[p2++]=r3                            || r1=[i1++]; // cy

        p1+=3;
        p2+=3;
        /* Y' = y*cy */
        a1 = r1.h*r4.h, a0 = r1.l*r4.l                     || r1=[i1++]; // crv

        /* R = Y+ crv*(Cr-128) */
        r2.h = (a1 += r1.h*r7.h), r2.l = (a0 += r1.l*r7.h);
                a1 -= r1.h*r7.h,          a0 -= r1.l*r7.h  || r5=[i1++]; // rmask
        r2 = byteop3p(r3:2, r1:0)(LO)                      || r1=[i1++]; // cbu
        r2=r2>>16 || B[p1++]=r2;
        B[p2++]=r2;

        /* B = Y+ cbu*(Cb-128) */
        r2.h = (a1 += r1.h*r6.h), r2.l = (a0 += r1.l*r6.h);
                a1 -= r1.h*r6.h,          a0 -= r1.l*r6.h  || r5=[i1++]; // bmask
        r3 = byteop3p(r3:2, r1:0)(LO)                      || r1=[i1++]; // cgu

        /* G = Y+ cgu*(Cb-128)+cgv*(Cr-128) */
                a1 += r1.h*r6.h,          a0 += r1.l*r6.h  || r1=[i1++]; // cgv
        r2.h = (a1 += r1.h*r7.h), r2.l = (a0 += r1.l*r7.h);
        r2 = byteop3p(r3:2, r1:0)(LO)                      || r5=[i1++]; // gmask
        r2=r2>>16 || B[p1++]=r2 || r0 = [i0++];    // 4y
                     B[p2++]=r2 || r1.l = w[i2++]; // 2u
        r3=r3>>16 || B[p1++]=r3 || r1.h = w[i3++]; // 2v
                     B[p2++]=r3 || r2=[i1++];      // oy

        p1+=3;
.L1888: p2+=3;

        l1 = 0;

        (r7:4) = [sp++];
        unlink;
        rts;
DEFUN_END(yuv2rgb24_line)



#define ARG_vdst        20
#define ARG_width       24
#define ARG_height      28
#define ARG_lumStride   32
#define ARG_chromStride 36
#define ARG_srcStride   40

DEFUN(uyvytoyv12, mL3,  (const uint8_t *src, uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
                         int width, int height,
                         int lumStride, int chromStride, int srcStride)):
        link 0;
        [--sp] = (r7:4,p5:4);

        p0 = r1;       // Y top even

        i2 = r2; // *u
        r2 = [fp + ARG_vdst];
        i3 = r2; // *v

        r1 = [fp + ARG_srcStride];
        r2 = r0 + r1;
        i0 = r0;  // uyvy_T even
        i1 = r2;  // uyvy_B odd

        p2 = [fp + ARG_lumStride];
        p1 = p0 + p2;  // Y bot odd

        p5 = [fp + ARG_width];
        p4 = [fp + ARG_height];
        r0 = p5;
        p4 = p4 >> 1;
        p5 = p5 >> 2;

        r2 = r0 << 1;
        r1 = r1 << 1;
        r1 = r1 - r2;  // srcStride + (srcStride - 2*width)
        r1 += -8;  // i0,i1 is pre read need to correct
        m0 = r1;

        r2 = [fp + ARG_chromStride];
        r0 = r0 >> 1;
        r2 = r2 - r0;
        m1 = r2;

        /*   I0,I1 - src input line pointers
         *   p0,p1 - luma output line pointers
         *   I2    - dstU
         *   I3    - dstV
         */

        lsetup (0f, 1f) lc1 = p4;   // H/2
0:        r0 = [i0++] || r2 = [i1++];
          r1 = [i0++] || r3 = [i1++];
          r4 = byteop1p(r1:0, r3:2);
          r5 = byteop1p(r1:0, r3:2) (r);
          lsetup (2f, 3f) lc0 = p5; // W/4
2:          r0 = r0 >> 8(v);
            r1 = r1 >> 8(v);
            r2 = r2 >> 8(v);
            r3 = r3 >> 8(v);
            r0 = bytepack(r0, r1);
            r2 = bytepack(r2, r3)         ||  [p0++] = r0;    // yyyy
            r6 = pack(r5.l, r4.l)         ||  [p1++] = r2;    // yyyy
            r7 = pack(r5.h, r4.h)         ||  r0 = [i0++] || r2 = [i1++];
            r6 = bytepack(r6, r7)         ||  r1 = [i0++] || r3 = [i1++];
            r4 = byteop1p(r1:0, r3:2)     ||  w[i2++] = r6.l; // uu
3:          r5 = byteop1p(r1:0, r3:2) (r) ||  w[i3++] = r6.h; // vv

          i0 += m0;
          i1 += m0;
          i2 += m1;
          i3 += m1;
          p0 = p0 + p2;
1:        p1 = p1 + p2;

        (r7:4,p5:4) = [sp++];
        unlink;
        rts;
DEFUN_END(uyvytoyv12)

DEFUN(yuyvtoyv12, mL3,  (const uint8_t *src, uint8_t *ydst, uint8_t *udst, uint8_t *vdst,
                         int width, int height,
                         int lumStride, int chromStride, int srcStride)):
        link 0;
        [--sp] = (r7:4,p5:4);

        p0 = r1;       // Y top even

        i2 = r2; // *u
        r2 = [fp + ARG_vdst];
        i3 = r2; // *v

        r1 = [fp + ARG_srcStride];
        r2 = r0 + r1;

        i0 = r0;  // uyvy_T even
        i1 = r2;  // uyvy_B odd

        p2 = [fp + ARG_lumStride];
        p1 = p0 + p2;  // Y bot odd

        p5 = [fp + ARG_width];
        p4 = [fp + ARG_height];
        r0 = p5;
        p4 = p4 >> 1;
        p5 = p5 >> 2;

        r2 = r0 << 1;
        r1 = r1 << 1;
        r1 = r1 - r2;  // srcStride + (srcStride - 2*width)
        r1 += -8;  // i0,i1 is pre read need to correct
        m0 = r1;

        r2 = [fp + ARG_chromStride];
        r0 = r0 >> 1;
        r2 = r2 - r0;
        m1 = r2;

        /*   I0,I1 - src input line pointers
         *   p0,p1 - luma output line pointers
         *   I2    - dstU
         *   I3    - dstV
         */

        lsetup (0f, 1f) lc1 = p4;   // H/2
0:        r0 = [i0++] || r2 = [i1++];
          r1 = [i0++] || r3 = [i1++];
          r4 = bytepack(r0, r1);
          r5 = bytepack(r2, r3);
          lsetup (2f, 3f) lc0 = p5; // W/4
2:          r0 = r0 >> 8(v) || [p0++] = r4;  // yyyy-even
            r1 = r1 >> 8(v) || [p1++] = r5;  // yyyy-odd
            r2 = r2 >> 8(v);
            r3 = r3 >> 8(v);
            r4 = byteop1p(r1:0, r3:2);
            r5 = byteop1p(r1:0, r3:2) (r);
            r6 = pack(r5.l, r4.l);
            r7 = pack(r5.h, r4.h)         ||  r0 = [i0++] || r2 = [i1++];
            r6 = bytepack(r6, r7)         ||  r1 = [i0++] || r3 = [i1++];
            r4 = bytepack(r0, r1)         ||  w[i2++] = r6.l; // uu
3:          r5 = bytepack(r2, r3)         ||  w[i3++] = r6.h; // vv

          i0 += m0;
          i1 += m0;
          i2 += m1;
          i3 += m1;
          p0 = p0 + p2;
1:        p1 = p1 + p2;

        (r7:4,p5:4) = [sp++];
        unlink;
        rts;
DEFUN_END(yuyvtoyv12)
