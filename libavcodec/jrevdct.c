/*
 * This file is part of the Independent JPEG Group's software.
 *
 * The authors make NO WARRANTY or representation, either express or implied,
 * with respect to this software, its quality, accuracy, merchantability, or
 * fitness for a particular purpose.  This software is provided "AS IS", and
 * you, its user, assume the entire risk as to its quality and accuracy.
 *
 * This software is copyright (C) 1991, 1992, Thomas G. Lane.
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
 * This file contains the basic inverse-DCT transformation subroutine.
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
 *
 * I've made lots of modifications to attempt to take advantage of the
 * sparse nature of the DCT matrices we're getting.  Although the logic
 * is cumbersome, it's straightforward and the resulting code is much
 * faster.
 *
 * A better way to do this would be to pass in the DCT block as a sparse
 * matrix, perhaps with the difference cases encoded.
 */

/**
 * @file
 * Independent JPEG Group's LLM idct.
 */

#include "libavutil/common.h"
#include "dct.h"

#define EIGHT_BIT_SAMPLES

#define DCTSIZE 8
#define DCTSIZE2 64

#define GLOBAL

#define RIGHT_SHIFT(x, n) ((x) >> (n))

typedef int16_t DCTBLOCK[DCTSIZE2];

#define CONST_BITS 13

/*
 * This routine is specialized to the case DCTSIZE = 8.
 */

#if DCTSIZE != 8
  Sorry, this code only copes with 8x8 DCTs. /* deliberate syntax err */
#endif


/*
 * A 2-D IDCT can be done by 1-D IDCT on each row followed by 1-D IDCT
 * on each column.  Direct algorithms are also available, but they are
 * much more complex and seem not to be any faster when reduced to code.
 *
 * The poop on this scaling stuff is as follows:
 *
 * Each 1-D IDCT step produces outputs which are a factor of sqrt(N)
 * larger than the true IDCT outputs.  The final outputs are therefore
 * a factor of N larger than desired; since N=8 this can be cured by
 * a simple right shift at the end of the algorithm.  The advantage of
 * this arrangement is that we save two multiplications per 1-D IDCT,
 * because the y0 and y4 inputs need not be divided by sqrt(N).
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
 * with the recommended scaling.  (To scale up 12-bit sample data further, an
 * intermediate int32 array would be needed.)
 *
 * To avoid overflow of the 32-bit intermediate results in pass 2, we must
 * have BITS_IN_JSAMPLE + CONST_BITS + PASS1_BITS <= 26.  Error analysis
 * shows that the values given below are the most effective.
 */

#ifdef EIGHT_BIT_SAMPLES
#define PASS1_BITS  2
#else
#define PASS1_BITS  1   /* lose a little precision to avoid overflow */
#endif

#define ONE         ((int32_t) 1)

#define CONST_SCALE (ONE << CONST_BITS)

/* Convert a positive real constant to an integer scaled by CONST_SCALE.
 * IMPORTANT: if your compiler doesn't do this arithmetic at compile time,
 * you will pay a significant penalty in run time.  In that case, figure
 * the correct integer constant values and insert them by hand.
 */

/* Actually FIX is no longer used, we precomputed them all */
#define FIX(x)  ((int32_t) ((x) * CONST_SCALE + 0.5))

/* Descale and correctly round an int32_t value that's scaled by N bits.
 * We assume RIGHT_SHIFT rounds towards minus infinity, so adding
 * the fudge factor is correct for either sign of X.
 */

#define DESCALE(x,n)  RIGHT_SHIFT((x) + (ONE << ((n)-1)), n)

/* Multiply an int32_t variable by an int32_t constant to yield an int32_t result.
 * For 8-bit samples with the recommended scaling, all the variable
 * and constant values involved are no more than 16 bits wide, so a
 * 16x16->32 bit multiply can be used instead of a full 32x32 multiply;
 * this provides a useful speedup on many machines.
 * There is no way to specify a 16x16->32 multiply in portable C, but
 * some C compilers will do the right thing if you provide the correct
 * combination of casts.
 * NB: for 12-bit samples, a full 32-bit multiplication will be needed.
 */

#ifdef EIGHT_BIT_SAMPLES
#ifdef SHORTxSHORT_32           /* may work if 'int' is 32 bits */
#define MULTIPLY(var,const)  (((int16_t) (var)) * ((int16_t) (const)))
#endif
#ifdef SHORTxLCONST_32          /* known to work with Microsoft C 6.0 */
#define MULTIPLY(var,const)  (((int16_t) (var)) * ((int32_t) (const)))
#endif
#endif

#ifndef MULTIPLY                /* default definition */
#define MULTIPLY(var,const)  ((var) * (const))
#endif


/*
  Unlike our decoder where we approximate the FIXes, we need to use exact
ones here or successive P-frames will drift too much with Reference frame coding
*/
#define FIX_0_211164243 1730
#define FIX_0_275899380 2260
#define FIX_0_298631336 2446
#define FIX_0_390180644 3196
#define FIX_0_509795579 4176
#define FIX_0_541196100 4433
#define FIX_0_601344887 4926
#define FIX_0_765366865 6270
#define FIX_0_785694958 6436
#define FIX_0_899976223 7373
#define FIX_1_061594337 8697
#define FIX_1_111140466 9102
#define FIX_1_175875602 9633
#define FIX_1_306562965 10703
#define FIX_1_387039845 11363
#define FIX_1_451774981 11893
#define FIX_1_501321110 12299
#define FIX_1_662939225 13623
#define FIX_1_847759065 15137
#define FIX_1_961570560 16069
#define FIX_2_053119869 16819
#define FIX_2_172734803 17799
#define FIX_2_562915447 20995
#define FIX_3_072711026 25172

/*
 * Perform the inverse DCT on one block of coefficients.
 */

void ff_j_rev_dct(DCTBLOCK data)
{
  int32_t tmp0, tmp1, tmp2, tmp3;
  int32_t tmp10, tmp11, tmp12, tmp13;
  int32_t z1, z2, z3, z4, z5;
  int32_t d0, d1, d2, d3, d4, d5, d6, d7;
  register int16_t *dataptr;
  int rowctr;

  /* Pass 1: process rows. */
  /* Note results are scaled up by sqrt(8) compared to a true IDCT; */
  /* furthermore, we scale the results by 2**PASS1_BITS. */

  dataptr = data;

  for (rowctr = DCTSIZE-1; rowctr >= 0; rowctr--) {
    /* Due to quantization, we will usually find that many of the input
     * coefficients are zero, especially the AC terms.  We can exploit this
     * by short-circuiting the IDCT calculation for any row in which all
     * the AC terms are zero.  In that case each output is equal to the
     * DC coefficient (with scale factor as needed).
     * With typical images and quantization tables, half or more of the
     * row DCT calculations can be simplified this way.
     */

    register int *idataptr = (int*)dataptr;

    /* WARNING: we do the same permutation as MMX idct to simplify the
       video core */
    d0 = dataptr[0];
    d2 = dataptr[1];
    d4 = dataptr[2];
    d6 = dataptr[3];
    d1 = dataptr[4];
    d3 = dataptr[5];
    d5 = dataptr[6];
    d7 = dataptr[7];

    if ((d1 | d2 | d3 | d4 | d5 | d6 | d7) == 0) {
      /* AC terms all zero */
      if (d0) {
          /* Compute a 32 bit value to assign. */
          int16_t dcval = (int16_t) (d0 * (1 << PASS1_BITS));
          register int v = (dcval & 0xffff) | ((dcval * (1 << 16)) & 0xffff0000);

          idataptr[0] = v;
          idataptr[1] = v;
          idataptr[2] = v;
          idataptr[3] = v;
      }

      dataptr += DCTSIZE;       /* advance pointer to next row */
      continue;
    }

    /* Even part: reverse the even part of the forward DCT. */
    /* The rotator is sqrt(2)*c(-6). */
{
    if (d6) {
            if (d2) {
                    /* d0 != 0, d2 != 0, d4 != 0, d6 != 0 */
                    z1 = MULTIPLY(d2 + d6, FIX_0_541196100);
                    tmp2 = z1 + MULTIPLY(-d6, FIX_1_847759065);
                    tmp3 = z1 + MULTIPLY(d2, FIX_0_765366865);

                    tmp0 = (d0 + d4) * CONST_SCALE;
                    tmp1 = (d0 - d4) * CONST_SCALE;

                    tmp10 = tmp0 + tmp3;
                    tmp13 = tmp0 - tmp3;
                    tmp11 = tmp1 + tmp2;
                    tmp12 = tmp1 - tmp2;
            } else {
                    /* d0 != 0, d2 == 0, d4 != 0, d6 != 0 */
                    tmp2 = MULTIPLY(-d6, FIX_1_306562965);
                    tmp3 = MULTIPLY(d6, FIX_0_541196100);

                    tmp0 = (d0 + d4) * CONST_SCALE;
                    tmp1 = (d0 - d4) * CONST_SCALE;

                    tmp10 = tmp0 + tmp3;
                    tmp13 = tmp0 - tmp3;
                    tmp11 = tmp1 + tmp2;
                    tmp12 = tmp1 - tmp2;
            }
    } else {
            if (d2) {
                    /* d0 != 0, d2 != 0, d4 != 0, d6 == 0 */
                    tmp2 = MULTIPLY(d2, FIX_0_541196100);
                    tmp3 = MULTIPLY(d2, FIX_1_306562965);

                    tmp0 = (d0 + d4) * CONST_SCALE;
                    tmp1 = (d0 - d4) * CONST_SCALE;

                    tmp10 = tmp0 + tmp3;
                    tmp13 = tmp0 - tmp3;
                    tmp11 = tmp1 + tmp2;
                    tmp12 = tmp1 - tmp2;
            } else {
                    /* d0 != 0, d2 == 0, d4 != 0, d6 == 0 */
                    tmp10 = tmp13 = (d0 + d4) * CONST_SCALE;
                    tmp11 = tmp12 = (d0 - d4) * CONST_SCALE;
            }
      }

    /* Odd part per figure 8; the matrix is unitary and hence its
     * transpose is its inverse.  i0..i3 are y7,y5,y3,y1 respectively.
     */

    if (d7) {
        if (d5) {
            if (d3) {
                if (d1) {
                    /* d1 != 0, d3 != 0, d5 != 0, d7 != 0 */
                    z1 = d7 + d1;
                    z2 = d5 + d3;
                    z3 = d7 + d3;
                    z4 = d5 + d1;
                    z5 = MULTIPLY(z3 + z4, FIX_1_175875602);

                    tmp0 = MULTIPLY(d7, FIX_0_298631336);
                    tmp1 = MULTIPLY(d5, FIX_2_053119869);
                    tmp2 = MULTIPLY(d3, FIX_3_072711026);
                    tmp3 = MULTIPLY(d1, FIX_1_501321110);
                    z1 = MULTIPLY(-z1, FIX_0_899976223);
                    z2 = MULTIPLY(-z2, FIX_2_562915447);
                    z3 = MULTIPLY(-z3, FIX_1_961570560);
                    z4 = MULTIPLY(-z4, FIX_0_390180644);

                    z3 += z5;
                    z4 += z5;

                    tmp0 += z1 + z3;
                    tmp1 += z2 + z4;
                    tmp2 += z2 + z3;
                    tmp3 += z1 + z4;
                } else {
                    /* d1 == 0, d3 != 0, d5 != 0, d7 != 0 */
                    z2 = d5 + d3;
                    z3 = d7 + d3;
                    z5 = MULTIPLY(z3 + d5, FIX_1_175875602);

                    tmp0 = MULTIPLY(d7, FIX_0_298631336);
                    tmp1 = MULTIPLY(d5, FIX_2_053119869);
                    tmp2 = MULTIPLY(d3, FIX_3_072711026);
                    z1 = MULTIPLY(-d7, FIX_0_899976223);
                    z2 = MULTIPLY(-z2, FIX_2_562915447);
                    z3 = MULTIPLY(-z3, FIX_1_961570560);
                    z4 = MULTIPLY(-d5, FIX_0_390180644);

                    z3 += z5;
                    z4 += z5;

                    tmp0 += z1 + z3;
                    tmp1 += z2 + z4;
                    tmp2 += z2 + z3;
                    tmp3 = z1 + z4;
                }
            } else {
                if (d1) {
                    /* d1 != 0, d3 == 0, d5 != 0, d7 != 0 */
                    z1 = d7 + d1;
                    z4 = d5 + d1;
                    z5 = MULTIPLY(d7 + z4, FIX_1_175875602);

                    tmp0 = MULTIPLY(d7, FIX_0_298631336);
                    tmp1 = MULTIPLY(d5, FIX_2_053119869);
                    tmp3 = MULTIPLY(d1, FIX_1_501321110);
                    z1 = MULTIPLY(-z1, FIX_0_899976223);
                    z2 = MULTIPLY(-d5, FIX_2_562915447);
                    z3 = MULTIPLY(-d7, FIX_1_961570560);
                    z4 = MULTIPLY(-z4, FIX_0_390180644);

                    z3 += z5;
                    z4 += z5;

                    tmp0 += z1 + z3;
                    tmp1 += z2 + z4;
                    tmp2 = z2 + z3;
                    tmp3 += z1 + z4;
                } else {
                    /* d1 == 0, d3 == 0, d5 != 0, d7 != 0 */
                    tmp0 = MULTIPLY(-d7, FIX_0_601344887);
                    z1 = MULTIPLY(-d7, FIX_0_899976223);
                    z3 = MULTIPLY(-d7, FIX_1_961570560);
                    tmp1 = MULTIPLY(-d5, FIX_0_509795579);
                    z2 = MULTIPLY(-d5, FIX_2_562915447);
                    z4 = MULTIPLY(-d5, FIX_0_390180644);
                    z5 = MULTIPLY(d5 + d7, FIX_1_175875602);

                    z3 += z5;
                    z4 += z5;

                    tmp0 += z3;
                    tmp1 += z4;
                    tmp2 = z2 + z3;
                    tmp3 = z1 + z4;
                }
            }
        } else {
            if (d3) {
                if (d1) {
                    /* d1 != 0, d3 != 0, d5 == 0, d7 != 0 */
                    z1 = d7 + d1;
                    z3 = d7 + d3;
                    z5 = MULTIPLY(z3 + d1, FIX_1_175875602);

                    tmp0 = MULTIPLY(d7, FIX_0_298631336);
                    tmp2 = MULTIPLY(d3, FIX_3_072711026);
                    tmp3 = MULTIPLY(d1, FIX_1_501321110);
                    z1 = MULTIPLY(-z1, FIX_0_899976223);
                    z2 = MULTIPLY(-d3, FIX_2_562915447);
                    z3 = MULTIPLY(-z3, FIX_1_961570560);
                    z4 = MULTIPLY(-d1, FIX_0_390180644);

                    z3 += z5;
                    z4 += z5;

                    tmp0 += z1 + z3;
                    tmp1 = z2 + z4;
                    tmp2 += z2 + z3;
                    tmp3 += z1 + z4;
                } else {
                    /* d1 == 0, d3 != 0, d5 == 0, d7 != 0 */
                    z3 = d7 + d3;

                    tmp0 = MULTIPLY(-d7, FIX_0_601344887);
                    z1 = MULTIPLY(-d7, FIX_0_899976223);
                    tmp2 = MULTIPLY(d3, FIX_0_509795579);
                    z2 = MULTIPLY(-d3, FIX_2_562915447);
                    z5 = MULTIPLY(z3, FIX_1_175875602);
                    z3 = MULTIPLY(-z3, FIX_0_785694958);

                    tmp0 += z3;
                    tmp1 = z2 + z5;
                    tmp2 += z3;
                    tmp3 = z1 + z5;
                }
            } else {
                if (d1) {
                    /* d1 != 0, d3 == 0, d5 == 0, d7 != 0 */
                    z1 = d7 + d1;
                    z5 = MULTIPLY(z1, FIX_1_175875602);

                    z1 = MULTIPLY(z1, FIX_0_275899380);
                    z3 = MULTIPLY(-d7, FIX_1_961570560);
                    tmp0 = MULTIPLY(-d7, FIX_1_662939225);
                    z4 = MULTIPLY(-d1, FIX_0_390180644);
                    tmp3 = MULTIPLY(d1, FIX_1_111140466);

                    tmp0 += z1;
                    tmp1 = z4 + z5;
                    tmp2 = z3 + z5;
                    tmp3 += z1;
                } else {
                    /* d1 == 0, d3 == 0, d5 == 0, d7 != 0 */
                    tmp0 = MULTIPLY(-d7, FIX_1_387039845);
                    tmp1 = MULTIPLY(d7, FIX_1_175875602);
                    tmp2 = MULTIPLY(-d7, FIX_0_785694958);
                    tmp3 = MULTIPLY(d7, FIX_0_275899380);
                }
            }
        }
    } else {
        if (d5) {
            if (d3) {
                if (d1) {
                    /* d1 != 0, d3 != 0, d5 != 0, d7 == 0 */
                    z2 = d5 + d3;
                    z4 = d5 + d1;
                    z5 = MULTIPLY(d3 + z4, FIX_1_175875602);

                    tmp1 = MULTIPLY(d5, FIX_2_053119869);
                    tmp2 = MULTIPLY(d3, FIX_3_072711026);
                    tmp3 = MULTIPLY(d1, FIX_1_501321110);
                    z1 = MULTIPLY(-d1, FIX_0_899976223);
                    z2 = MULTIPLY(-z2, FIX_2_562915447);
                    z3 = MULTIPLY(-d3, FIX_1_961570560);
                    z4 = MULTIPLY(-z4, FIX_0_390180644);

                    z3 += z5;
                    z4 += z5;

                    tmp0 = z1 + z3;
                    tmp1 += z2 + z4;
                    tmp2 += z2 + z3;
                    tmp3 += z1 + z4;
                } else {
                    /* d1 == 0, d3 != 0, d5 != 0, d7 == 0 */
                    z2 = d5 + d3;

                    z5 = MULTIPLY(z2, FIX_1_175875602);
                    tmp1 = MULTIPLY(d5, FIX_1_662939225);
                    z4 = MULTIPLY(-d5, FIX_0_390180644);
                    z2 = MULTIPLY(-z2, FIX_1_387039845);
                    tmp2 = MULTIPLY(d3, FIX_1_111140466);
                    z3 = MULTIPLY(-d3, FIX_1_961570560);

                    tmp0 = z3 + z5;
                    tmp1 += z2;
                    tmp2 += z2;
                    tmp3 = z4 + z5;
                }
            } else {
                if (d1) {
                    /* d1 != 0, d3 == 0, d5 != 0, d7 == 0 */
                    z4 = d5 + d1;

                    z5 = MULTIPLY(z4, FIX_1_175875602);
                    z1 = MULTIPLY(-d1, FIX_0_899976223);
                    tmp3 = MULTIPLY(d1, FIX_0_601344887);
                    tmp1 = MULTIPLY(-d5, FIX_0_509795579);
                    z2 = MULTIPLY(-d5, FIX_2_562915447);
                    z4 = MULTIPLY(z4, FIX_0_785694958);

                    tmp0 = z1 + z5;
                    tmp1 += z4;
                    tmp2 = z2 + z5;
                    tmp3 += z4;
                } else {
                    /* d1 == 0, d3 == 0, d5 != 0, d7 == 0 */
                    tmp0 = MULTIPLY(d5, FIX_1_175875602);
                    tmp1 = MULTIPLY(d5, FIX_0_275899380);
                    tmp2 = MULTIPLY(-d5, FIX_1_387039845);
                    tmp3 = MULTIPLY(d5, FIX_0_785694958);
                }
            }
        } else {
            if (d3) {
                if (d1) {
                    /* d1 != 0, d3 != 0, d5 == 0, d7 == 0 */
                    z5 = d1 + d3;
                    tmp3 = MULTIPLY(d1, FIX_0_211164243);
                    tmp2 = MULTIPLY(-d3, FIX_1_451774981);
                    z1 = MULTIPLY(d1, FIX_1_061594337);
                    z2 = MULTIPLY(-d3, FIX_2_172734803);
                    z4 = MULTIPLY(z5, FIX_0_785694958);
                    z5 = MULTIPLY(z5, FIX_1_175875602);

                    tmp0 = z1 - z4;
                    tmp1 = z2 + z4;
                    tmp2 += z5;
                    tmp3 += z5;
                } else {
                    /* d1 == 0, d3 != 0, d5 == 0, d7 == 0 */
                    tmp0 = MULTIPLY(-d3, FIX_0_785694958);
                    tmp1 = MULTIPLY(-d3, FIX_1_387039845);
                    tmp2 = MULTIPLY(-d3, FIX_0_275899380);
                    tmp3 = MULTIPLY(d3, FIX_1_175875602);
                }
            } else {
                if (d1) {
                    /* d1 != 0, d3 == 0, d5 == 0, d7 == 0 */
                    tmp0 = MULTIPLY(d1, FIX_0_275899380);
                    tmp1 = MULTIPLY(d1, FIX_0_785694958);
                    tmp2 = MULTIPLY(d1, FIX_1_175875602);
                    tmp3 = MULTIPLY(d1, FIX_1_387039845);
                } else {
                    /* d1 == 0, d3 == 0, d5 == 0, d7 == 0 */
                    tmp0 = tmp1 = tmp2 = tmp3 = 0;
                }
            }
        }
    }
}
    /* Final output stage: inputs are tmp10..tmp13, tmp0..tmp3 */

    dataptr[0] = (int16_t) DESCALE(tmp10 + tmp3, CONST_BITS-PASS1_BITS);
    dataptr[7] = (int16_t) DESCALE(tmp10 - tmp3, CONST_BITS-PASS1_BITS);
    dataptr[1] = (int16_t) DESCALE(tmp11 + tmp2, CONST_BITS-PASS1_BITS);
    dataptr[6] = (int16_t) DESCALE(tmp11 - tmp2, CONST_BITS-PASS1_BITS);
    dataptr[2] = (int16_t) DESCALE(tmp12 + tmp1, CONST_BITS-PASS1_BITS);
    dataptr[5] = (int16_t) DESCALE(tmp12 - tmp1, CONST_BITS-PASS1_BITS);
    dataptr[3] = (int16_t) DESCALE(tmp13 + tmp0, CONST_BITS-PASS1_BITS);
    dataptr[4] = (int16_t) DESCALE(tmp13 - tmp0, CONST_BITS-PASS1_BITS);

    dataptr += DCTSIZE;         /* advance pointer to next row */
  }

  /* Pass 2: process columns. */
  /* Note that we must descale the results by a factor of 8 == 2**3, */
  /* and also undo the PASS1_BITS scaling. */

  dataptr = data;
  for (rowctr = DCTSIZE-1; rowctr >= 0; rowctr--) {
    /* Columns of zeroes can be exploited in the same way as we did with rows.
     * However, the row calculation has created many nonzero AC terms, so the
     * simplification applies less often (typically 5% to 10% of the time).
     * On machines with very fast multiplication, it's possible that the
     * test takes more time than it's worth.  In that case this section
     * may be commented out.
     */

    d0 = dataptr[DCTSIZE*0];
    d1 = dataptr[DCTSIZE*1];
    d2 = dataptr[DCTSIZE*2];
    d3 = dataptr[DCTSIZE*3];
    d4 = dataptr[DCTSIZE*4];
    d5 = dataptr[DCTSIZE*5];
    d6 = dataptr[DCTSIZE*6];
    d7 = dataptr[DCTSIZE*7];

    /* Even part: reverse the even part of the forward DCT. */
    /* The rotator is sqrt(2)*c(-6). */
    if (d6) {
            if (d2) {
                    /* d0 != 0, d2 != 0, d4 != 0, d6 != 0 */
                    z1 = MULTIPLY(d2 + d6, FIX_0_541196100);
                    tmp2 = z1 + MULTIPLY(-d6, FIX_1_847759065);
                    tmp3 = z1 + MULTIPLY(d2, FIX_0_765366865);

                    tmp0 = (d0 + d4) * CONST_SCALE;
                    tmp1 = (d0 - d4) * CONST_SCALE;

                    tmp10 = tmp0 + tmp3;
                    tmp13 = tmp0 - tmp3;
                    tmp11 = tmp1 + tmp2;
                    tmp12 = tmp1 - tmp2;
            } else {
                    /* d0 != 0, d2 == 0, d4 != 0, d6 != 0 */
                    tmp2 = MULTIPLY(-d6, FIX_1_306562965);
                    tmp3 = MULTIPLY(d6, FIX_0_541196100);

                    tmp0 = (d0 + d4) * CONST_SCALE;
                    tmp1 = (d0 - d4) * CONST_SCALE;

                    tmp10 = tmp0 + tmp3;
                    tmp13 = tmp0 - tmp3;
                    tmp11 = tmp1 + tmp2;
                    tmp12 = tmp1 - tmp2;
            }
    } else {
            if (d2) {
                    /* d0 != 0, d2 != 0, d4 != 0, d6 == 0 */
                    tmp2 = MULTIPLY(d2, FIX_0_541196100);
                    tmp3 = MULTIPLY(d2, FIX_1_306562965);

                    tmp0 = (d0 + d4) * CONST_SCALE;
                    tmp1 = (d0 - d4) * CONST_SCALE;

                    tmp10 = tmp0 + tmp3;
                    tmp13 = tmp0 - tmp3;
                    tmp11 = tmp1 + tmp2;
                    tmp12 = tmp1 - tmp2;
            } else {
                    /* d0 != 0, d2 == 0, d4 != 0, d6 == 0 */
                    tmp10 = tmp13 = (d0 + d4) * CONST_SCALE;
                    tmp11 = tmp12 = (d0 - d4) * CONST_SCALE;
            }
    }

    /* Odd part per figure 8; the matrix is unitary and hence its
     * transpose is its inverse.  i0..i3 are y7,y5,y3,y1 respectively.
     */
    if (d7) {
        if (d5) {
            if (d3) {
                if (d1) {
                    /* d1 != 0, d3 != 0, d5 != 0, d7 != 0 */
                    z1 = d7 + d1;
                    z2 = d5 + d3;
                    z3 = d7 + d3;
                    z4 = d5 + d1;
                    z5 = MULTIPLY(z3 + z4, FIX_1_175875602);

                    tmp0 = MULTIPLY(d7, FIX_0_298631336);
                    tmp1 = MULTIPLY(d5, FIX_2_053119869);
                    tmp2 = MULTIPLY(d3, FIX_3_072711026);
                    tmp3 = MULTIPLY(d1, FIX_1_501321110);
                    z1 = MULTIPLY(-z1, FIX_0_899976223);
                    z2 = MULTIPLY(-z2, FIX_2_562915447);
                    z3 = MULTIPLY(-z3, FIX_1_961570560);
                    z4 = MULTIPLY(-z4, FIX_0_390180644);

                    z3 += z5;
                    z4 += z5;

                    tmp0 += z1 + z3;
                    tmp1 += z2 + z4;
                    tmp2 += z2 + z3;
                    tmp3 += z1 + z4;
                } else {
                    /* d1 == 0, d3 != 0, d5 != 0, d7 != 0 */
                    z2 = d5 + d3;
                    z3 = d7 + d3;
                    z5 = MULTIPLY(z3 + d5, FIX_1_175875602);

                    tmp0 = MULTIPLY(d7, FIX_0_298631336);
                    tmp1 = MULTIPLY(d5, FIX_2_053119869);
                    tmp2 = MULTIPLY(d3, FIX_3_072711026);
                    z1 = MULTIPLY(-d7, FIX_0_899976223);
                    z2 = MULTIPLY(-z2, FIX_2_562915447);
                    z3 = MULTIPLY(-z3, FIX_1_961570560);
                    z4 = MULTIPLY(-d5, FIX_0_390180644);

                    z3 += z5;
                    z4 += z5;

                    tmp0 += z1 + z3;
                    tmp1 += z2 + z4;
                    tmp2 += z2 + z3;
                    tmp3 = z1 + z4;
                }
            } else {
                if (d1) {
                    /* d1 != 0, d3 == 0, d5 != 0, d7 != 0 */
                    z1 = d7 + d1;
                    z3 = d7;
                    z4 = d5 + d1;
                    z5 = MULTIPLY(z3 + z4, FIX_1_175875602);

                    tmp0 = MULTIPLY(d7, FIX_0_298631336);
                    tmp1 = MULTIPLY(d5, FIX_2_053119869);
                    tmp3 = MULTIPLY(d1, FIX_1_501321110);
                    z1 = MULTIPLY(-z1, FIX_0_899976223);
                    z2 = MULTIPLY(-d5, FIX_2_562915447);
                    z3 = MULTIPLY(-d7, FIX_1_961570560);
                    z4 = MULTIPLY(-z4, FIX_0_390180644);

                    z3 += z5;
                    z4 += z5;

                    tmp0 += z1 + z3;
                    tmp1 += z2 + z4;
                    tmp2 = z2 + z3;
                    tmp3 += z1 + z4;
                } else {
                    /* d1 == 0, d3 == 0, d5 != 0, d7 != 0 */
                    tmp0 = MULTIPLY(-d7, FIX_0_601344887);
                    z1 = MULTIPLY(-d7, FIX_0_899976223);
                    z3 = MULTIPLY(-d7, FIX_1_961570560);
                    tmp1 = MULTIPLY(-d5, FIX_0_509795579);
                    z2 = MULTIPLY(-d5, FIX_2_562915447);
                    z4 = MULTIPLY(-d5, FIX_0_390180644);
                    z5 = MULTIPLY(d5 + d7, FIX_1_175875602);

                    z3 += z5;
                    z4 += z5;

                    tmp0 += z3;
                    tmp1 += z4;
                    tmp2 = z2 + z3;
                    tmp3 = z1 + z4;
                }
            }
        } else {
            if (d3) {
                if (d1) {
                    /* d1 != 0, d3 != 0, d5 == 0, d7 != 0 */
                    z1 = d7 + d1;
                    z3 = d7 + d3;
                    z5 = MULTIPLY(z3 + d1, FIX_1_175875602);

                    tmp0 = MULTIPLY(d7, FIX_0_298631336);
                    tmp2 = MULTIPLY(d3, FIX_3_072711026);
                    tmp3 = MULTIPLY(d1, FIX_1_501321110);
                    z1 = MULTIPLY(-z1, FIX_0_899976223);
                    z2 = MULTIPLY(-d3, FIX_2_562915447);
                    z3 = MULTIPLY(-z3, FIX_1_961570560);
                    z4 = MULTIPLY(-d1, FIX_0_390180644);

                    z3 += z5;
                    z4 += z5;

                    tmp0 += z1 + z3;
                    tmp1 = z2 + z4;
                    tmp2 += z2 + z3;
                    tmp3 += z1 + z4;
                } else {
                    /* d1 == 0, d3 != 0, d5 == 0, d7 != 0 */
                    z3 = d7 + d3;

                    tmp0 = MULTIPLY(-d7, FIX_0_601344887);
                    z1 = MULTIPLY(-d7, FIX_0_899976223);
                    tmp2 = MULTIPLY(d3, FIX_0_509795579);
                    z2 = MULTIPLY(-d3, FIX_2_562915447);
                    z5 = MULTIPLY(z3, FIX_1_175875602);
                    z3 = MULTIPLY(-z3, FIX_0_785694958);

                    tmp0 += z3;
                    tmp1 = z2 + z5;
                    tmp2 += z3;
                    tmp3 = z1 + z5;
                }
            } else {
                if (d1) {
                    /* d1 != 0, d3 == 0, d5 == 0, d7 != 0 */
                    z1 = d7 + d1;
                    z5 = MULTIPLY(z1, FIX_1_175875602);

                    z1 = MULTIPLY(z1, FIX_0_275899380);
                    z3 = MULTIPLY(-d7, FIX_1_961570560);
                    tmp0 = MULTIPLY(-d7, FIX_1_662939225);
                    z4 = MULTIPLY(-d1, FIX_0_390180644);
                    tmp3 = MULTIPLY(d1, FIX_1_111140466);

                    tmp0 += z1;
                    tmp1 = z4 + z5;
                    tmp2 = z3 + z5;
                    tmp3 += z1;
                } else {
                    /* d1 == 0, d3 == 0, d5 == 0, d7 != 0 */
                    tmp0 = MULTIPLY(-d7, FIX_1_387039845);
                    tmp1 = MULTIPLY(d7, FIX_1_175875602);
                    tmp2 = MULTIPLY(-d7, FIX_0_785694958);
                    tmp3 = MULTIPLY(d7, FIX_0_275899380);
                }
            }
        }
    } else {
        if (d5) {
            if (d3) {
                if (d1) {
                    /* d1 != 0, d3 != 0, d5 != 0, d7 == 0 */
                    z2 = d5 + d3;
                    z4 = d5 + d1;
                    z5 = MULTIPLY(d3 + z4, FIX_1_175875602);

                    tmp1 = MULTIPLY(d5, FIX_2_053119869);
                    tmp2 = MULTIPLY(d3, FIX_3_072711026);
                    tmp3 = MULTIPLY(d1, FIX_1_501321110);
                    z1 = MULTIPLY(-d1, FIX_0_899976223);
                    z2 = MULTIPLY(-z2, FIX_2_562915447);
                    z3 = MULTIPLY(-d3, FIX_1_961570560);
                    z4 = MULTIPLY(-z4, FIX_0_390180644);

                    z3 += z5;
                    z4 += z5;

                    tmp0 = z1 + z3;
                    tmp1 += z2 + z4;
                    tmp2 += z2 + z3;
                    tmp3 += z1 + z4;
                } else {
                    /* d1 == 0, d3 != 0, d5 != 0, d7 == 0 */
                    z2 = d5 + d3;

                    z5 = MULTIPLY(z2, FIX_1_175875602);
                    tmp1 = MULTIPLY(d5, FIX_1_662939225);
                    z4 = MULTIPLY(-d5, FIX_0_390180644);
                    z2 = MULTIPLY(-z2, FIX_1_387039845);
                    tmp2 = MULTIPLY(d3, FIX_1_111140466);
                    z3 = MULTIPLY(-d3, FIX_1_961570560);

                    tmp0 = z3 + z5;
                    tmp1 += z2;
                    tmp2 += z2;
                    tmp3 = z4 + z5;
                }
            } else {
                if (d1) {
                    /* d1 != 0, d3 == 0, d5 != 0, d7 == 0 */
                    z4 = d5 + d1;

                    z5 = MULTIPLY(z4, FIX_1_175875602);
                    z1 = MULTIPLY(-d1, FIX_0_899976223);
                    tmp3 = MULTIPLY(d1, FIX_0_601344887);
                    tmp1 = MULTIPLY(-d5, FIX_0_509795579);
                    z2 = MULTIPLY(-d5, FIX_2_562915447);
                    z4 = MULTIPLY(z4, FIX_0_785694958);

                    tmp0 = z1 + z5;
                    tmp1 += z4;
                    tmp2 = z2 + z5;
                    tmp3 += z4;
                } else {
                    /* d1 == 0, d3 == 0, d5 != 0, d7 == 0 */
                    tmp0 = MULTIPLY(d5, FIX_1_175875602);
                    tmp1 = MULTIPLY(d5, FIX_0_275899380);
                    tmp2 = MULTIPLY(-d5, FIX_1_387039845);
                    tmp3 = MULTIPLY(d5, FIX_0_785694958);
                }
            }
        } else {
            if (d3) {
                if (d1) {
                    /* d1 != 0, d3 != 0, d5 == 0, d7 == 0 */
                    z5 = d1 + d3;
                    tmp3 = MULTIPLY(d1, FIX_0_211164243);
                    tmp2 = MULTIPLY(-d3, FIX_1_451774981);
                    z1 = MULTIPLY(d1, FIX_1_061594337);
                    z2 = MULTIPLY(-d3, FIX_2_172734803);
                    z4 = MULTIPLY(z5, FIX_0_785694958);
                    z5 = MULTIPLY(z5, FIX_1_175875602);

                    tmp0 = z1 - z4;
                    tmp1 = z2 + z4;
                    tmp2 += z5;
                    tmp3 += z5;
                } else {
                    /* d1 == 0, d3 != 0, d5 == 0, d7 == 0 */
                    tmp0 = MULTIPLY(-d3, FIX_0_785694958);
                    tmp1 = MULTIPLY(-d3, FIX_1_387039845);
                    tmp2 = MULTIPLY(-d3, FIX_0_275899380);
                    tmp3 = MULTIPLY(d3, FIX_1_175875602);
                }
            } else {
                if (d1) {
                    /* d1 != 0, d3 == 0, d5 == 0, d7 == 0 */
                    tmp0 = MULTIPLY(d1, FIX_0_275899380);
                    tmp1 = MULTIPLY(d1, FIX_0_785694958);
                    tmp2 = MULTIPLY(d1, FIX_1_175875602);
                    tmp3 = MULTIPLY(d1, FIX_1_387039845);
                } else {
                    /* d1 == 0, d3 == 0, d5 == 0, d7 == 0 */
                    tmp0 = tmp1 = tmp2 = tmp3 = 0;
                }
            }
        }
    }

    /* Final output stage: inputs are tmp10..tmp13, tmp0..tmp3 */

    dataptr[DCTSIZE*0] = (int16_t) DESCALE(tmp10 + tmp3,
                                           CONST_BITS+PASS1_BITS+3);
    dataptr[DCTSIZE*7] = (int16_t) DESCALE(tmp10 - tmp3,
                                           CONST_BITS+PASS1_BITS+3);
    dataptr[DCTSIZE*1] = (int16_t) DESCALE(tmp11 + tmp2,
                                           CONST_BITS+PASS1_BITS+3);
    dataptr[DCTSIZE*6] = (int16_t) DESCALE(tmp11 - tmp2,
                                           CONST_BITS+PASS1_BITS+3);
    dataptr[DCTSIZE*2] = (int16_t) DESCALE(tmp12 + tmp1,
                                           CONST_BITS+PASS1_BITS+3);
    dataptr[DCTSIZE*5] = (int16_t) DESCALE(tmp12 - tmp1,
                                           CONST_BITS+PASS1_BITS+3);
    dataptr[DCTSIZE*3] = (int16_t) DESCALE(tmp13 + tmp0,
                                           CONST_BITS+PASS1_BITS+3);
    dataptr[DCTSIZE*4] = (int16_t) DESCALE(tmp13 - tmp0,
                                           CONST_BITS+PASS1_BITS+3);

    dataptr++;                  /* advance pointer to next column */
  }
}

#undef DCTSIZE
#define DCTSIZE 4
#define DCTSTRIDE 8

void ff_j_rev_dct4(DCTBLOCK data)
{
  int32_t tmp0, tmp1, tmp2, tmp3;
  int32_t tmp10, tmp11, tmp12, tmp13;
  int32_t z1;
  int32_t d0, d2, d4, d6;
  register int16_t *dataptr;
  int rowctr;

  /* Pass 1: process rows. */
  /* Note results are scaled up by sqrt(8) compared to a true IDCT; */
  /* furthermore, we scale the results by 2**PASS1_BITS. */

  data[0] += 4;

  dataptr = data;

  for (rowctr = DCTSIZE-1; rowctr >= 0; rowctr--) {
    /* Due to quantization, we will usually find that many of the input
     * coefficients are zero, especially the AC terms.  We can exploit this
     * by short-circuiting the IDCT calculation for any row in which all
     * the AC terms are zero.  In that case each output is equal to the
     * DC coefficient (with scale factor as needed).
     * With typical images and quantization tables, half or more of the
     * row DCT calculations can be simplified this way.
     */

    register int *idataptr = (int*)dataptr;

    d0 = dataptr[0];
    d2 = dataptr[1];
    d4 = dataptr[2];
    d6 = dataptr[3];

    if ((d2 | d4 | d6) == 0) {
      /* AC terms all zero */
      if (d0) {
          /* Compute a 32 bit value to assign. */
          int16_t dcval = (int16_t) (d0 << PASS1_BITS);
          register int v = (dcval & 0xffff) | ((dcval << 16) & 0xffff0000);

          idataptr[0] = v;
          idataptr[1] = v;
      }

      dataptr += DCTSTRIDE;     /* advance pointer to next row */
      continue;
    }

    /* Even part: reverse the even part of the forward DCT. */
    /* The rotator is sqrt(2)*c(-6). */
    if (d6) {
            if (d2) {
                    /* d0 != 0, d2 != 0, d4 != 0, d6 != 0 */
                    z1 = MULTIPLY(d2 + d6, FIX_0_541196100);
                    tmp2 = z1 + MULTIPLY(-d6, FIX_1_847759065);
                    tmp3 = z1 + MULTIPLY(d2, FIX_0_765366865);

                    tmp0 = (d0 + d4) << CONST_BITS;
                    tmp1 = (d0 - d4) << CONST_BITS;

                    tmp10 = tmp0 + tmp3;
                    tmp13 = tmp0 - tmp3;
                    tmp11 = tmp1 + tmp2;
                    tmp12 = tmp1 - tmp2;
            } else {
                    /* d0 != 0, d2 == 0, d4 != 0, d6 != 0 */
                    tmp2 = MULTIPLY(-d6, FIX_1_306562965);
                    tmp3 = MULTIPLY(d6, FIX_0_541196100);

                    tmp0 = (d0 + d4) << CONST_BITS;
                    tmp1 = (d0 - d4) << CONST_BITS;

                    tmp10 = tmp0 + tmp3;
                    tmp13 = tmp0 - tmp3;
                    tmp11 = tmp1 + tmp2;
                    tmp12 = tmp1 - tmp2;
            }
    } else {
            if (d2) {
                    /* d0 != 0, d2 != 0, d4 != 0, d6 == 0 */
                    tmp2 = MULTIPLY(d2, FIX_0_541196100);
                    tmp3 = MULTIPLY(d2, FIX_1_306562965);

                    tmp0 = (d0 + d4) << CONST_BITS;
                    tmp1 = (d0 - d4) << CONST_BITS;

                    tmp10 = tmp0 + tmp3;
                    tmp13 = tmp0 - tmp3;
                    tmp11 = tmp1 + tmp2;
                    tmp12 = tmp1 - tmp2;
            } else {
                    /* d0 != 0, d2 == 0, d4 != 0, d6 == 0 */
                    tmp10 = tmp13 = (d0 + d4) << CONST_BITS;
                    tmp11 = tmp12 = (d0 - d4) << CONST_BITS;
            }
      }

    /* Final output stage: inputs are tmp10..tmp13, tmp0..tmp3 */

    dataptr[0] = (int16_t) DESCALE(tmp10, CONST_BITS-PASS1_BITS);
    dataptr[1] = (int16_t) DESCALE(tmp11, CONST_BITS-PASS1_BITS);
    dataptr[2] = (int16_t) DESCALE(tmp12, CONST_BITS-PASS1_BITS);
    dataptr[3] = (int16_t) DESCALE(tmp13, CONST_BITS-PASS1_BITS);

    dataptr += DCTSTRIDE;       /* advance pointer to next row */
  }

  /* Pass 2: process columns. */
  /* Note that we must descale the results by a factor of 8 == 2**3, */
  /* and also undo the PASS1_BITS scaling. */

  dataptr = data;
  for (rowctr = DCTSIZE-1; rowctr >= 0; rowctr--) {
    /* Columns of zeroes can be exploited in the same way as we did with rows.
     * However, the row calculation has created many nonzero AC terms, so the
     * simplification applies less often (typically 5% to 10% of the time).
     * On machines with very fast multiplication, it's possible that the
     * test takes more time than it's worth.  In that case this section
     * may be commented out.
     */

    d0 = dataptr[DCTSTRIDE*0];
    d2 = dataptr[DCTSTRIDE*1];
    d4 = dataptr[DCTSTRIDE*2];
    d6 = dataptr[DCTSTRIDE*3];

    /* Even part: reverse the even part of the forward DCT. */
    /* The rotator is sqrt(2)*c(-6). */
    if (d6) {
            if (d2) {
                    /* d0 != 0, d2 != 0, d4 != 0, d6 != 0 */
                    z1 = MULTIPLY(d2 + d6, FIX_0_541196100);
                    tmp2 = z1 + MULTIPLY(-d6, FIX_1_847759065);
                    tmp3 = z1 + MULTIPLY(d2, FIX_0_765366865);

                    tmp0 = (d0 + d4) << CONST_BITS;
                    tmp1 = (d0 - d4) << CONST_BITS;

                    tmp10 = tmp0 + tmp3;
                    tmp13 = tmp0 - tmp3;
                    tmp11 = tmp1 + tmp2;
                    tmp12 = tmp1 - tmp2;
            } else {
                    /* d0 != 0, d2 == 0, d4 != 0, d6 != 0 */
                    tmp2 = MULTIPLY(-d6, FIX_1_306562965);
                    tmp3 = MULTIPLY(d6, FIX_0_541196100);

                    tmp0 = (d0 + d4) << CONST_BITS;
                    tmp1 = (d0 - d4) << CONST_BITS;

                    tmp10 = tmp0 + tmp3;
                    tmp13 = tmp0 - tmp3;
                    tmp11 = tmp1 + tmp2;
                    tmp12 = tmp1 - tmp2;
            }
    } else {
            if (d2) {
                    /* d0 != 0, d2 != 0, d4 != 0, d6 == 0 */
                    tmp2 = MULTIPLY(d2, FIX_0_541196100);
                    tmp3 = MULTIPLY(d2, FIX_1_306562965);

                    tmp0 = (d0 + d4) << CONST_BITS;
                    tmp1 = (d0 - d4) << CONST_BITS;

                    tmp10 = tmp0 + tmp3;
                    tmp13 = tmp0 - tmp3;
                    tmp11 = tmp1 + tmp2;
                    tmp12 = tmp1 - tmp2;
            } else {
                    /* d0 != 0, d2 == 0, d4 != 0, d6 == 0 */
                    tmp10 = tmp13 = (d0 + d4) << CONST_BITS;
                    tmp11 = tmp12 = (d0 - d4) << CONST_BITS;
            }
    }

    /* Final output stage: inputs are tmp10..tmp13, tmp0..tmp3 */

    dataptr[DCTSTRIDE*0] = tmp10 >> (CONST_BITS+PASS1_BITS+3);
    dataptr[DCTSTRIDE*1] = tmp11 >> (CONST_BITS+PASS1_BITS+3);
    dataptr[DCTSTRIDE*2] = tmp12 >> (CONST_BITS+PASS1_BITS+3);
    dataptr[DCTSTRIDE*3] = tmp13 >> (CONST_BITS+PASS1_BITS+3);

    dataptr++;                  /* advance pointer to next column */
  }
}

void ff_j_rev_dct2(DCTBLOCK data){
  int d00, d01, d10, d11;

  data[0] += 4;
  d00 = data[0+0*DCTSTRIDE] + data[1+0*DCTSTRIDE];
  d01 = data[0+0*DCTSTRIDE] - data[1+0*DCTSTRIDE];
  d10 = data[0+1*DCTSTRIDE] + data[1+1*DCTSTRIDE];
  d11 = data[0+1*DCTSTRIDE] - data[1+1*DCTSTRIDE];

  data[0+0*DCTSTRIDE]= (d00 + d10)>>3;
  data[1+0*DCTSTRIDE]= (d01 + d11)>>3;
  data[0+1*DCTSTRIDE]= (d00 - d10)>>3;
  data[1+1*DCTSTRIDE]= (d01 - d11)>>3;
}

void ff_j_rev_dct1(DCTBLOCK data){
  data[0] = (data[0] + 4)>>3;
}

#undef FIX
#undef CONST_BITS
