/*
 * This file is part of the Independent JPEG Group's software.
 *
 * The authors make NO WARRANTY or representation, either express or implied,
 * with respect to this software, its quality, accuracy, merchantability, or
 * fitness for a particular purpose.  This software is provided "AS IS", and
 * you, its user, assume the entire risk as to its quality and accuracy.
 *
 * This software is copyright (C) 1991-1996, Thomas G. Lane.
 * All Rights Reserved except as specified below.
 *
 * Permission is hereby granted to use, copy, modify, and distribute this
 * software (or portions thereof) for any purpose, without fee, subject to
 * these conditions:
 * (1) If any part of the source code for this software is distributed, then
 * this README file must be included, with this copyright and no-warranty
 * notice unaltered; and any additions, deletions, or changes to the original
 * files must be clearly indicated in accompanying documentation.
 * (2) If only executable code is distributed, then the accompanying
 * documentation must state that "this software is based in part on the work
 * of the Independent JPEG Group".
 * (3) Permission for use of this software is granted only if the user accepts
 * full responsibility for any undesirable consequences; the authors accept
 * NO LIABILITY for damages of any kind.
 *
 * These conditions apply to any software derived from or based on the IJG
 * code, not just to the unmodified library.  If you use our work, you ought
 * to acknowledge us.
 *
 * Permission is NOT granted for the use of any IJG author's name or company
 * name in advertising or publicity relating to this software or products
 * derived from it.  This software may be referred to only as "the Independent
 * JPEG Group's software".
 *
 * We specifically permit and encourage the use of this software as the basis
 * of commercial products, provided that all warranty or liability claims are
 * assumed by the product vendor.
 *
 * This file contains a slow-but-accurate integer implementation of the
 * forward DCT (Discrete Cosine Transform).
 *
 * A 2-D DCT can be done by 1-D DCT on each row followed by 1-D DCT
 * on each column.  Direct algorithms are also available, but they are
 * much more complex and seem not to be any faster when reduced to code.
 *
 * This implementation is based on an algorithm described in
 *   C. Loeffler, A. Ligtenberg and G. Moschytz, "Practical Fast 1-D DCT
 *   Algorithms with 11 Multiplications", Proc. Int'l. Conf. on Acoustics,
 *   Speech, and Signal Processing 1989 (ICASSP '89), pp. 988-991.
 * The primary algorithm described there uses 11 multiplies and 29 adds.
 * We use their alternate method with 12 multiplies and 32 adds.
 * The advantage of this method is that no data path contains more than one
 * multiplication; this allows a very simple and accurate implementation in
 * scaled fixed-point arithmetic, with a minimal number of shifts.
 */

/**
 * @file
 * Independent JPEG Group's slow & accurate dct.
 */

#include "libavutil/common.h"
#include "dct.h"

#include "bit_depth_template.c"

#define DCTSIZE 8
#define BITS_IN_JSAMPLE BIT_DEPTH
#define GLOBAL(x) x
#define RIGHT_SHIFT(x, n) ((x) >> (n))
#define MULTIPLY16C16(var,const) ((var)*(const))

#if 1 //def USE_ACCURATE_ROUNDING
#define DESCALE(x,n)  RIGHT_SHIFT((x) + (1 << ((n) - 1)), n)
#else
#define DESCALE(x,n)  RIGHT_SHIFT(x, n)
#endif


/*
 * This module is specialized to the case DCTSIZE = 8.
 */

#if DCTSIZE != 8
#error  "Sorry, this code only copes with 8x8 DCTs."
#endif


/*
 * The poop on this scaling stuff is as follows:
 *
 * Each 1-D DCT step produces outputs which are a factor of sqrt(N)
 * larger than the true DCT outputs.  The final outputs are therefore
 * a factor of N larger than desired; since N=8 this can be cured by
 * a simple right shift at the end of the algorithm.  The advantage of
 * this arrangement is that we save two multiplications per 1-D DCT,
 * because the y0 and y4 outputs need not be divided by sqrt(N).
 * In the IJG code, this factor of 8 is removed by the quantization step
 * (in jcdctmgr.c), NOT in this module.
 *
 * We have to do addition and subtraction of the integer inputs, which
 * is no problem, and multiplication by fractional constants, which is
 * a problem to do in integer arithmetic.  We multiply all the constants
 * by CONST_SCALE and convert them to integer constants (thus retaining
 * CONST_BITS bits of precision in the constants).  After doing a
 * multiplication we have to divide the product by CONST_SCALE, with proper
 * rounding, to produce the correct output.  This division can be done
 * cheaply as a right shift of CONST_BITS bits.  We postpone shifting
 * as long as possible so that partial sums can be added together with
 * full fractional precision.
 *
 * The outputs of the first pass are scaled up by PASS1_BITS bits so that
 * they are represented to better-than-integral precision.  These outputs
 * require BITS_IN_JSAMPLE + PASS1_BITS + 3 bits; this fits in a 16-bit word
 * with the recommended scaling.  (For 12-bit sample data, the intermediate
 * array is int32_t anyway.)
 *
 * To avoid overflow of the 32-bit intermediate results in pass 2, we must
 * have BITS_IN_JSAMPLE + CONST_BITS + PASS1_BITS <= 26.  Error analysis
 * shows that the values given below are the most effective.
 */

#undef CONST_BITS
#undef PASS1_BITS
#undef OUT_SHIFT

#if BITS_IN_JSAMPLE == 8
#define CONST_BITS  13
#define PASS1_BITS  4   /* set this to 2 if 16x16 multiplies are faster */
#define OUT_SHIFT   PASS1_BITS
#else
#define CONST_BITS  13
#define PASS1_BITS  1   /* lose a little precision to avoid overflow */
#define OUT_SHIFT   (PASS1_BITS + 1)
#endif

/* Some C compilers fail to reduce "FIX(constant)" at compile time, thus
 * causing a lot of useless floating-point operations at run time.
 * To get around this we use the following pre-calculated constants.
 * If you change CONST_BITS you may want to add appropriate values.
 * (With a reasonable C compiler, you can just rely on the FIX() macro...)
 */

#if CONST_BITS == 13
#define FIX_0_298631336  ((int32_t)  2446)      /* FIX(0.298631336) */
#define FIX_0_390180644  ((int32_t)  3196)      /* FIX(0.390180644) */
#define FIX_0_541196100  ((int32_t)  4433)      /* FIX(0.541196100) */
#define FIX_0_765366865  ((int32_t)  6270)      /* FIX(0.765366865) */
#define FIX_0_899976223  ((int32_t)  7373)      /* FIX(0.899976223) */
#define FIX_1_175875602  ((int32_t)  9633)      /* FIX(1.175875602) */
#define FIX_1_501321110  ((int32_t)  12299)     /* FIX(1.501321110) */
#define FIX_1_847759065  ((int32_t)  15137)     /* FIX(1.847759065) */
#define FIX_1_961570560  ((int32_t)  16069)     /* FIX(1.961570560) */
#define FIX_2_053119869  ((int32_t)  16819)     /* FIX(2.053119869) */
#define FIX_2_562915447  ((int32_t)  20995)     /* FIX(2.562915447) */
#define FIX_3_072711026  ((int32_t)  25172)     /* FIX(3.072711026) */
#else
#define FIX_0_298631336  FIX(0.298631336)
#define FIX_0_390180644  FIX(0.390180644)
#define FIX_0_541196100  FIX(0.541196100)
#define FIX_0_765366865  FIX(0.765366865)
#define FIX_0_899976223  FIX(0.899976223)
#define FIX_1_175875602  FIX(1.175875602)
#define FIX_1_501321110  FIX(1.501321110)
#define FIX_1_847759065  FIX(1.847759065)
#define FIX_1_961570560  FIX(1.961570560)
#define FIX_2_053119869  FIX(2.053119869)
#define FIX_2_562915447  FIX(2.562915447)
#define FIX_3_072711026  FIX(3.072711026)
#endif


/* Multiply an int32_t variable by an int32_t constant to yield an int32_t result.
 * For 8-bit samples with the recommended scaling, all the variable
 * and constant values involved are no more than 16 bits wide, so a
 * 16x16->32 bit multiply can be used instead of a full 32x32 multiply.
 * For 12-bit samples, a full 32-bit multiplication will be needed.
 */

#if BITS_IN_JSAMPLE == 8 && CONST_BITS<=13 && PASS1_BITS<=2
#define MULTIPLY(var,const)  MULTIPLY16C16(var,const)
#else
#define MULTIPLY(var,const)  ((var) * (const))
#endif


static av_always_inline void FUNC(row_fdct)(int16_t *data)
{
  int tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
  int tmp10, tmp11, tmp12, tmp13;
  int z1, z2, z3, z4, z5;
  int16_t *dataptr;
  int ctr;

  /* Pass 1: process rows. */
  /* Note results are scaled up by sqrt(8) compared to a true DCT; */
  /* furthermore, we scale the results by 2**PASS1_BITS. */

  dataptr = data;
  for (ctr = DCTSIZE-1; ctr >= 0; ctr--) {
    tmp0 = dataptr[0] + dataptr[7];
    tmp7 = dataptr[0] - dataptr[7];
    tmp1 = dataptr[1] + dataptr[6];
    tmp6 = dataptr[1] - dataptr[6];
    tmp2 = dataptr[2] + dataptr[5];
    tmp5 = dataptr[2] - dataptr[5];
    tmp3 = dataptr[3] + dataptr[4];
    tmp4 = dataptr[3] - dataptr[4];

    /* Even part per LL&M figure 1 --- note that published figure is faulty;
     * rotator "sqrt(2)*c1" should be "sqrt(2)*c6".
     */

    tmp10 = tmp0 + tmp3;
    tmp13 = tmp0 - tmp3;
    tmp11 = tmp1 + tmp2;
    tmp12 = tmp1 - tmp2;

    dataptr[0] = (int16_t) ((tmp10 + tmp11) << PASS1_BITS);
    dataptr[4] = (int16_t) ((tmp10 - tmp11) << PASS1_BITS);

    z1 = MULTIPLY(tmp12 + tmp13, FIX_0_541196100);
    dataptr[2] = (int16_t) DESCALE(z1 + MULTIPLY(tmp13, FIX_0_765366865),
                                   CONST_BITS-PASS1_BITS);
    dataptr[6] = (int16_t) DESCALE(z1 + MULTIPLY(tmp12, - FIX_1_847759065),
                                   CONST_BITS-PASS1_BITS);

    /* Odd part per figure 8 --- note paper omits factor of sqrt(2).
     * cK represents cos(K*pi/16).
     * i0..i3 in the paper are tmp4..tmp7 here.
     */

    z1 = tmp4 + tmp7;
    z2 = tmp5 + tmp6;
    z3 = tmp4 + tmp6;
    z4 = tmp5 + tmp7;
    z5 = MULTIPLY(z3 + z4, FIX_1_175875602); /* sqrt(2) * c3 */

    tmp4 = MULTIPLY(tmp4, FIX_0_298631336); /* sqrt(2) * (-c1+c3+c5-c7) */
    tmp5 = MULTIPLY(tmp5, FIX_2_053119869); /* sqrt(2) * ( c1+c3-c5+c7) */
    tmp6 = MULTIPLY(tmp6, FIX_3_072711026); /* sqrt(2) * ( c1+c3+c5-c7) */
    tmp7 = MULTIPLY(tmp7, FIX_1_501321110); /* sqrt(2) * ( c1+c3-c5-c7) */
    z1 = MULTIPLY(z1, - FIX_0_899976223); /* sqrt(2) * (c7-c3) */
    z2 = MULTIPLY(z2, - FIX_2_562915447); /* sqrt(2) * (-c1-c3) */
    z3 = MULTIPLY(z3, - FIX_1_961570560); /* sqrt(2) * (-c3-c5) */
    z4 = MULTIPLY(z4, - FIX_0_390180644); /* sqrt(2) * (c5-c3) */

    z3 += z5;
    z4 += z5;

    dataptr[7] = (int16_t) DESCALE(tmp4 + z1 + z3, CONST_BITS-PASS1_BITS);
    dataptr[5] = (int16_t) DESCALE(tmp5 + z2 + z4, CONST_BITS-PASS1_BITS);
    dataptr[3] = (int16_t) DESCALE(tmp6 + z2 + z3, CONST_BITS-PASS1_BITS);
    dataptr[1] = (int16_t) DESCALE(tmp7 + z1 + z4, CONST_BITS-PASS1_BITS);

    dataptr += DCTSIZE;         /* advance pointer to next row */
  }
}

/*
 * Perform the forward DCT on one block of samples.
 */

GLOBAL(void)
FUNC(ff_jpeg_fdct_islow)(int16_t *data)
{
  int tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
  int tmp10, tmp11, tmp12, tmp13;
  int z1, z2, z3, z4, z5;
  int16_t *dataptr;
  int ctr;

  FUNC(row_fdct)(data);

  /* Pass 2: process columns.
   * We remove the PASS1_BITS scaling, but leave the results scaled up
   * by an overall factor of 8.
   */

  dataptr = data;
  for (ctr = DCTSIZE-1; ctr >= 0; ctr--) {
    tmp0 = dataptr[DCTSIZE*0] + dataptr[DCTSIZE*7];
    tmp7 = dataptr[DCTSIZE*0] - dataptr[DCTSIZE*7];
    tmp1 = dataptr[DCTSIZE*1] + dataptr[DCTSIZE*6];
    tmp6 = dataptr[DCTSIZE*1] - dataptr[DCTSIZE*6];
    tmp2 = dataptr[DCTSIZE*2] + dataptr[DCTSIZE*5];
    tmp5 = dataptr[DCTSIZE*2] - dataptr[DCTSIZE*5];
    tmp3 = dataptr[DCTSIZE*3] + dataptr[DCTSIZE*4];
    tmp4 = dataptr[DCTSIZE*3] - dataptr[DCTSIZE*4];

    /* Even part per LL&M figure 1 --- note that published figure is faulty;
     * rotator "sqrt(2)*c1" should be "sqrt(2)*c6".
     */

    tmp10 = tmp0 + tmp3;
    tmp13 = tmp0 - tmp3;
    tmp11 = tmp1 + tmp2;
    tmp12 = tmp1 - tmp2;

    dataptr[DCTSIZE*0] = DESCALE(tmp10 + tmp11, OUT_SHIFT);
    dataptr[DCTSIZE*4] = DESCALE(tmp10 - tmp11, OUT_SHIFT);

    z1 = MULTIPLY(tmp12 + tmp13, FIX_0_541196100);
    dataptr[DCTSIZE*2] = DESCALE(z1 + MULTIPLY(tmp13, FIX_0_765366865),
                                 CONST_BITS + OUT_SHIFT);
    dataptr[DCTSIZE*6] = DESCALE(z1 + MULTIPLY(tmp12, - FIX_1_847759065),
                                 CONST_BITS + OUT_SHIFT);

    /* Odd part per figure 8 --- note paper omits factor of sqrt(2).
     * cK represents cos(K*pi/16).
     * i0..i3 in the paper are tmp4..tmp7 here.
     */

    z1 = tmp4 + tmp7;
    z2 = tmp5 + tmp6;
    z3 = tmp4 + tmp6;
    z4 = tmp5 + tmp7;
    z5 = MULTIPLY(z3 + z4, FIX_1_175875602); /* sqrt(2) * c3 */

    tmp4 = MULTIPLY(tmp4, FIX_0_298631336); /* sqrt(2) * (-c1+c3+c5-c7) */
    tmp5 = MULTIPLY(tmp5, FIX_2_053119869); /* sqrt(2) * ( c1+c3-c5+c7) */
    tmp6 = MULTIPLY(tmp6, FIX_3_072711026); /* sqrt(2) * ( c1+c3+c5-c7) */
    tmp7 = MULTIPLY(tmp7, FIX_1_501321110); /* sqrt(2) * ( c1+c3-c5-c7) */
    z1 = MULTIPLY(z1, - FIX_0_899976223); /* sqrt(2) * (c7-c3) */
    z2 = MULTIPLY(z2, - FIX_2_562915447); /* sqrt(2) * (-c1-c3) */
    z3 = MULTIPLY(z3, - FIX_1_961570560); /* sqrt(2) * (-c3-c5) */
    z4 = MULTIPLY(z4, - FIX_0_390180644); /* sqrt(2) * (c5-c3) */

    z3 += z5;
    z4 += z5;

    dataptr[DCTSIZE*7] = DESCALE(tmp4 + z1 + z3, CONST_BITS + OUT_SHIFT);
    dataptr[DCTSIZE*5] = DESCALE(tmp5 + z2 + z4, CONST_BITS + OUT_SHIFT);
    dataptr[DCTSIZE*3] = DESCALE(tmp6 + z2 + z3, CONST_BITS + OUT_SHIFT);
    dataptr[DCTSIZE*1] = DESCALE(tmp7 + z1 + z4, CONST_BITS + OUT_SHIFT);

    dataptr++;                  /* advance pointer to next column */
  }
}

/*
 * The secret of DCT2-4-8 is really simple -- you do the usual 1-DCT
 * on the rows and then, instead of doing even and odd, part on the columns
 * you do even part two times.
 */
GLOBAL(void)
FUNC(ff_fdct248_islow)(int16_t *data)
{
  int tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
  int tmp10, tmp11, tmp12, tmp13;
  int z1;
  int16_t *dataptr;
  int ctr;

  FUNC(row_fdct)(data);

  /* Pass 2: process columns.
   * We remove the PASS1_BITS scaling, but leave the results scaled up
   * by an overall factor of 8.
   */

  dataptr = data;
  for (ctr = DCTSIZE-1; ctr >= 0; ctr--) {
     tmp0 = dataptr[DCTSIZE*0] + dataptr[DCTSIZE*1];
     tmp1 = dataptr[DCTSIZE*2] + dataptr[DCTSIZE*3];
     tmp2 = dataptr[DCTSIZE*4] + dataptr[DCTSIZE*5];
     tmp3 = dataptr[DCTSIZE*6] + dataptr[DCTSIZE*7];
     tmp4 = dataptr[DCTSIZE*0] - dataptr[DCTSIZE*1];
     tmp5 = dataptr[DCTSIZE*2] - dataptr[DCTSIZE*3];
     tmp6 = dataptr[DCTSIZE*4] - dataptr[DCTSIZE*5];
     tmp7 = dataptr[DCTSIZE*6] - dataptr[DCTSIZE*7];

     tmp10 = tmp0 + tmp3;
     tmp11 = tmp1 + tmp2;
     tmp12 = tmp1 - tmp2;
     tmp13 = tmp0 - tmp3;

     dataptr[DCTSIZE*0] = DESCALE(tmp10 + tmp11, OUT_SHIFT);
     dataptr[DCTSIZE*4] = DESCALE(tmp10 - tmp11, OUT_SHIFT);

     z1 = MULTIPLY(tmp12 + tmp13, FIX_0_541196100);
     dataptr[DCTSIZE*2] = DESCALE(z1 + MULTIPLY(tmp13, FIX_0_765366865),
                                  CONST_BITS+OUT_SHIFT);
     dataptr[DCTSIZE*6] = DESCALE(z1 + MULTIPLY(tmp12, - FIX_1_847759065),
                                  CONST_BITS+OUT_SHIFT);

     tmp10 = tmp4 + tmp7;
     tmp11 = tmp5 + tmp6;
     tmp12 = tmp5 - tmp6;
     tmp13 = tmp4 - tmp7;

     dataptr[DCTSIZE*1] = DESCALE(tmp10 + tmp11, OUT_SHIFT);
     dataptr[DCTSIZE*5] = DESCALE(tmp10 - tmp11, OUT_SHIFT);

     z1 = MULTIPLY(tmp12 + tmp13, FIX_0_541196100);
     dataptr[DCTSIZE*3] = DESCALE(z1 + MULTIPLY(tmp13, FIX_0_765366865),
                                  CONST_BITS + OUT_SHIFT);
     dataptr[DCTSIZE*7] = DESCALE(z1 + MULTIPLY(tmp12, - FIX_1_847759065),
                                  CONST_BITS + OUT_SHIFT);

     dataptr++;                 /* advance pointer to next column */
  }
}
