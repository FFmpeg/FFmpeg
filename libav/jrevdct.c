/*
 * jrevdct.c
 *
 * Copyright (C) 1991, 1992, Thomas G. Lane.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
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

typedef int INT32;

/* Definition of Contant integer scale factor. */
#define CONST_BITS 13

/* Misc DCT definitions */
#define DCTSIZE		8	/* The basic DCT block is 8x8 samples */
#define DCTSIZE2	64	/* DCTSIZE squared; # of elements in a block */

#define GLOBAL			/* a function referenced thru EXTERNs */

typedef int DCTELEM;
typedef DCTELEM DCTBLOCK[DCTSIZE2];

void j_rev_dct (DCTELEM *data);


#define GLOBAL			/* a function referenced thru EXTERNs */
#define ORIG_DCT	1

/* We assume that right shift corresponds to signed division by 2 with
 * rounding towards minus infinity.  This is correct for typical "arithmetic
 * shift" instructions that shift in copies of the sign bit.  But some
 * C compilers implement >> with an unsigned shift.  For these machines you
 * must define RIGHT_SHIFT_IS_UNSIGNED.
 * RIGHT_SHIFT provides a proper signed right shift of an INT32 quantity.
 * It is only applied with constant shift counts.  SHIFT_TEMPS must be
 * included in the variables of any routine using RIGHT_SHIFT.
 */

#ifdef RIGHT_SHIFT_IS_UNSIGNED
#define SHIFT_TEMPS	INT32 shift_temp;
#define RIGHT_SHIFT(x,shft)  \
	((shift_temp = (x)) < 0 ? \
	 (shift_temp >> (shft)) | ((~((INT32) 0)) << (32-(shft))) : \
	 (shift_temp >> (shft)))
#else
#define SHIFT_TEMPS
#define RIGHT_SHIFT(x,shft)	((x) >> (shft))
#endif

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
 * intermediate INT32 array would be needed.)
 *
 * To avoid overflow of the 32-bit intermediate results in pass 2, we must
 * have BITS_IN_JSAMPLE + CONST_BITS + PASS1_BITS <= 26.  Error analysis
 * shows that the values given below are the most effective.
 */

#ifdef EIGHT_BIT_SAMPLES
#define PASS1_BITS  2
#else
#define PASS1_BITS  1		/* lose a little precision to avoid overflow */
#endif

#define ONE	((INT32) 1)

#define CONST_SCALE (ONE << CONST_BITS)

/* Convert a positive real constant to an integer scaled by CONST_SCALE.
 * IMPORTANT: if your compiler doesn't do this arithmetic at compile time,
 * you will pay a significant penalty in run time.  In that case, figure
 * the correct integer constant values and insert them by hand.
 */

#define FIX(x)	((INT32) ((x) * CONST_SCALE + 0.5))

/* Descale and correctly round an INT32 value that's scaled by N bits.
 * We assume RIGHT_SHIFT rounds towards minus infinity, so adding
 * the fudge factor is correct for either sign of X.
 */

#define DESCALE(x,n)  RIGHT_SHIFT((x) + (ONE << ((n)-1)), n)
#define SCALE(x,n)	((INT32)(x) << n)

/* Multiply an INT32 variable by an INT32 constant to yield an INT32 result.
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
#ifdef SHORTxSHORT_32		/* may work if 'int' is 32 bits */
#define MULTIPLY(var,const)  (((INT16) (var)) * ((INT16) (const)))
#endif
#ifdef SHORTxLCONST_32		/* known to work with Microsoft C 6.0 */
#define MULTIPLY(var,const)  (((INT16) (var)) * ((INT32) (const)))
#endif
#endif

#if 0
/* force a multiplication for x86 where a multiply is fast). We
   force the non constant operand to be in a register because
   otherwise it may be a 16 bit memory reference, which is not allowed
   by imull */
#define MULTIPLY(a,b) \
({\
    int res;\
    asm("imull %2,%1,%0" : "=r" (res) : "r" ((int)(a)), "i" (b));\
    res;\
})
#endif

#ifndef MULTIPLY		/* default definition */
#define MULTIPLY(var,const)  ((var) * (const))
#endif


#ifndef ORIG_DCT

#undef SSMUL
#define SSMUL(var1,var2)  ((INT16)(var1) * (INT32)(INT16)(var2))

/* Precomputed idct value arrays. */

STATIC DCTELEM PreIDCT[64][64];

/* Pre compute singleton coefficient IDCT values. */
void init_pre_idct() {
    int i;

    for (i = 0; i < 64; i++) {
	memset ((char *) PreIDCT[i], 0, 64 * sizeof(DCTELEM));
	PreIDCT[i][i] = 2048;
	j_rev_dct (PreIDCT[i]);
    }
}

/*
 * Perform the inverse DCT on one block of coefficients.
 */

void j_rev_dct_sparse (data, pos)
	DCTBLOCK data;
	int pos;
{
    register DCTELEM *dataptr;
    short int val;
    DCTELEM *ndataptr;
    int coeff, rr;

    /* If DC Coefficient. */

    if (pos == 0) {
	register INT32 *dp;
	register INT32 v;

	dp = (INT32*)data;
	v = *data;
	/* Compute 32 bit value to assign.
	 *  This speeds things up a bit */
	if (v < 0)
	    val = (short)((v - 3) >> 3);
	else
	    val = (short)((v + 4) >> 3);
	v = val | ((INT32)val << 16);
	dp[0] = v;      dp[1] = v;      dp[2] = v;      dp[3] = v;
	dp[4] = v;      dp[5] = v;      dp[6] = v;      dp[7] = v;
	dp[8] = v;      dp[9] = v;      dp[10] = v;     dp[11] = v;
	dp[12] = v;     dp[13] = v;     dp[14] = v;     dp[15] = v;
	dp[16] = v;     dp[17] = v;     dp[18] = v;     dp[19] = v;
	dp[20] = v;     dp[21] = v;     dp[22] = v;     dp[23] = v;
	dp[24] = v;     dp[25] = v;     dp[26] = v;     dp[27] = v;
	dp[28] = v;     dp[29] = v;     dp[30] = v;     dp[31] = v;
	return;
    }

    /* Some other coefficient. */
    dataptr = (DCTELEM *)data;
    coeff = dataptr[pos];
    ndataptr = PreIDCT[pos];

    for (rr = 0; rr < 4; rr++) {
	dataptr[0]  = (DCTELEM)(SSMUL (ndataptr[0] , coeff) >> (CONST_BITS-2));
	dataptr[1]  = (DCTELEM)(SSMUL (ndataptr[1] , coeff) >> (CONST_BITS-2));
	dataptr[2]  = (DCTELEM)(SSMUL (ndataptr[2] , coeff) >> (CONST_BITS-2));
	dataptr[3]  = (DCTELEM)(SSMUL (ndataptr[3] , coeff) >> (CONST_BITS-2));
	dataptr[4]  = (DCTELEM)(SSMUL (ndataptr[4] , coeff) >> (CONST_BITS-2));
	dataptr[5]  = (DCTELEM)(SSMUL (ndataptr[5] , coeff) >> (CONST_BITS-2));
	dataptr[6]  = (DCTELEM)(SSMUL (ndataptr[6] , coeff) >> (CONST_BITS-2));
	dataptr[7]  = (DCTELEM)(SSMUL (ndataptr[7] , coeff) >> (CONST_BITS-2));
	dataptr[8]  = (DCTELEM)(SSMUL (ndataptr[8] , coeff) >> (CONST_BITS-2));
	dataptr[9]  = (DCTELEM)(SSMUL (ndataptr[9] , coeff) >> (CONST_BITS-2));
	dataptr[10] = (DCTELEM)(SSMUL (ndataptr[10], coeff) >> (CONST_BITS-2));
	dataptr[11] = (DCTELEM)(SSMUL (ndataptr[11], coeff) >> (CONST_BITS-2));
	dataptr[12] = (DCTELEM)(SSMUL (ndataptr[12], coeff) >> (CONST_BITS-2));
	dataptr[13] = (DCTELEM)(SSMUL (ndataptr[13], coeff) >> (CONST_BITS-2));
	dataptr[14] = (DCTELEM)(SSMUL (ndataptr[14], coeff) >> (CONST_BITS-2));
	dataptr[15] = (DCTELEM)(SSMUL (ndataptr[15], coeff) >> (CONST_BITS-2));
	dataptr += 16;
	ndataptr += 16;
    }
}


void j_rev_dct (data)
	DCTBLOCK data;
{
    INT32 tmp0, tmp1, tmp2, tmp3;
    INT32 tmp10, tmp11, tmp12, tmp13;
    INT32 z1, z2, z3, z4, z5;
    int d0, d1, d2, d3, d4, d5, d6, d7;
    register DCTELEM *dataptr;
    int rowctr;
    SHIFT_TEMPS;

    /* Pass 1: process rows. */
    /* Note results are scaled up by sqrt(8) compared to a true IDCT; */
    /* furthermore, we scale the results by 2**PASS1_BITS. */

    dataptr = data;

    for (rowctr = DCTSIZE - 1; rowctr >= 0; rowctr--) {
	/* Due to quantization, we will usually find that many of the input
	 * coefficients are zero, especially the AC terms.  We can exploit this
	 * by short-circuiting the IDCT calculation for any row in which all
	 * the AC terms are zero.  In that case each output is equal to the
	 * DC coefficient (with scale factor as needed).
	 * With typical images and quantization tables, half or more of the
	 * row DCT calculations can be simplified this way.
	 */

	register INT32 *idataptr = (INT32*)dataptr;
	d0 = dataptr[0];
	d1 = dataptr[1];
	if ((d1 == 0) && (idataptr[1] | idataptr[2] | idataptr[3]) == 0) {
	    /* AC terms all zero */
	    if (d0) {
		/* Compute a 32 bit value to assign. */
		DCTELEM dcval = (DCTELEM) (d0 << PASS1_BITS);
		register INT32 v = (dcval & 0xffff) |
				   (((INT32)dcval << 16) & 0xffff0000L);

		idataptr[0] = v;
		idataptr[1] = v;
		idataptr[2] = v;
		idataptr[3] = v;
	    }

	    dataptr += DCTSIZE;	/* advance pointer to next row */
	    continue;
	}
	d2 = dataptr[2];
	d3 = dataptr[3];
	d4 = dataptr[4];
	d5 = dataptr[5];
	d6 = dataptr[6];
	d7 = dataptr[7];

	/* Even part: reverse the even part of the forward DCT. */
	/* The rotator is sqrt(2)*c(-6). */
	if (d6) {
	    if (d4) {
		if (d2) {
		    if (d0) {
			/* d0 != 0, d2 != 0, d4 != 0, d6 != 0 */
			z1 = MULTIPLY(d2 + d6, FIX(0.541196100));
			tmp2 = z1 + MULTIPLY(d6, - FIX(1.847759065));
			tmp3 = z1 + MULTIPLY(d2, FIX(0.765366865));

			tmp0 = SCALE (d0 + d4, CONST_BITS);
			tmp1 = SCALE (d0 - d4, CONST_BITS);

			tmp10 = tmp0 + tmp3;
			tmp13 = tmp0 - tmp3;
			tmp11 = tmp1 + tmp2;
			tmp12 = tmp1 - tmp2;
		    } else {
			/* d0 == 0, d2 != 0, d4 != 0, d6 != 0 */
			z1 = MULTIPLY(d2 + d6, FIX(0.541196100));
			tmp2 = z1 + MULTIPLY(d6, - FIX(1.847759065));
			tmp3 = z1 + MULTIPLY(d2, FIX(0.765366865));

			tmp0 = SCALE (d4, CONST_BITS);

			tmp10 = tmp0 + tmp3;
			tmp13 = tmp0 - tmp3;
			tmp11 = tmp2 - tmp0;
			tmp12 = -(tmp0 + tmp2);
		    }
		} else {
		    if (d0) {
			/* d0 != 0, d2 == 0, d4 != 0, d6 != 0 */
			tmp2 = MULTIPLY(d6, - FIX(1.306562965));
			tmp3 = MULTIPLY(d6, FIX(0.541196100));

			tmp0 = SCALE (d0 + d4, CONST_BITS);
			tmp1 = SCALE (d0 - d4, CONST_BITS);

			tmp10 = tmp0 + tmp3;
			tmp13 = tmp0 - tmp3;
			tmp11 = tmp1 + tmp2;
			tmp12 = tmp1 - tmp2;
		    } else {
			/* d0 == 0, d2 == 0, d4 != 0, d6 != 0 */
			tmp2 = MULTIPLY(d6, -FIX(1.306562965));
			tmp3 = MULTIPLY(d6, FIX(0.541196100));

			tmp0 = SCALE (d4, CONST_BITS);

			tmp10 = tmp0 + tmp3;
			tmp13 = tmp0 - tmp3;
			tmp11 = tmp2 - tmp0;
			tmp12 = -(tmp0 + tmp2);
		    }
		}
	    } else {
		if (d2) {
		    if (d0) {
			/* d0 != 0, d2 != 0, d4 == 0, d6 != 0 */
			z1 = MULTIPLY(d2 + d6, FIX(0.541196100));
			tmp2 = z1 + MULTIPLY(d6, - FIX(1.847759065));
			tmp3 = z1 + MULTIPLY(d2, FIX(0.765366865));

			tmp0 = SCALE (d0, CONST_BITS);

			tmp10 = tmp0 + tmp3;
			tmp13 = tmp0 - tmp3;
			tmp11 = tmp0 + tmp2;
			tmp12 = tmp0 - tmp2;
		    } else {
			/* d0 == 0, d2 != 0, d4 == 0, d6 != 0 */
			z1 = MULTIPLY(d2 + d6, FIX(0.541196100));
			tmp2 = z1 + MULTIPLY(d6, - FIX(1.847759065));
			tmp3 = z1 + MULTIPLY(d2, FIX(0.765366865));

			tmp10 = tmp3;
			tmp13 = -tmp3;
			tmp11 = tmp2;
			tmp12 = -tmp2;
		    }
		} else {
		    if (d0) {
			/* d0 != 0, d2 == 0, d4 == 0, d6 != 0 */
			tmp2 = MULTIPLY(d6, - FIX(1.306562965));
			tmp3 = MULTIPLY(d6, FIX(0.541196100));

			tmp0 = SCALE (d0, CONST_BITS);

			tmp10 = tmp0 + tmp3;
			tmp13 = tmp0 - tmp3;
			tmp11 = tmp0 + tmp2;
			tmp12 = tmp0 - tmp2;
		    } else {
			/* d0 == 0, d2 == 0, d4 == 0, d6 != 0 */
			tmp2 = MULTIPLY(d6, - FIX(1.306562965));
			tmp3 = MULTIPLY(d6, FIX(0.541196100));

			tmp10 = tmp3;
			tmp13 = -tmp3;
			tmp11 = tmp2;
			tmp12 = -tmp2;
		    }
		}
	    }
	} else {
	    if (d4) {
		if (d2) {
		    if (d0) {
			/* d0 != 0, d2 != 0, d4 != 0, d6 == 0 */
			tmp2 = MULTIPLY(d2, FIX(0.541196100));
			tmp3 = MULTIPLY(d2, FIX(1.306562965));

			tmp0 = SCALE (d0 + d4, CONST_BITS);
			tmp1 = SCALE (d0 - d4, CONST_BITS);

			tmp10 = tmp0 + tmp3;
			tmp13 = tmp0 - tmp3;
			tmp11 = tmp1 + tmp2;
			tmp12 = tmp1 - tmp2;
		    } else {
			/* d0 == 0, d2 != 0, d4 != 0, d6 == 0 */
			tmp2 = MULTIPLY(d2, FIX(0.541196100));
			tmp3 = MULTIPLY(d2, FIX(1.306562965));

			tmp0 = SCALE (d4, CONST_BITS);

			tmp10 = tmp0 + tmp3;
			tmp13 = tmp0 - tmp3;
			tmp11 = tmp2 - tmp0;
			tmp12 = -(tmp0 + tmp2);
		    }
		} else {
		    if (d0) {
			/* d0 != 0, d2 == 0, d4 != 0, d6 == 0 */
			tmp10 = tmp13 = SCALE (d0 + d4, CONST_BITS);
			tmp11 = tmp12 = SCALE (d0 - d4, CONST_BITS);
		    } else {
			/* d0 == 0, d2 == 0, d4 != 0, d6 == 0 */
			tmp10 = tmp13 = SCALE (d4, CONST_BITS);
			tmp11 = tmp12 = -tmp10;
		    }
		}
	    } else {
		if (d2) {
		    if (d0) {
			/* d0 != 0, d2 != 0, d4 == 0, d6 == 0 */
			tmp2 = MULTIPLY(d2, FIX(0.541196100));
			tmp3 = MULTIPLY(d2, FIX(1.306562965));

			tmp0 = SCALE (d0, CONST_BITS);

			tmp10 = tmp0 + tmp3;
			tmp13 = tmp0 - tmp3;
			tmp11 = tmp0 + tmp2;
			tmp12 = tmp0 - tmp2;
		    } else {
			/* d0 == 0, d2 != 0, d4 == 0, d6 == 0 */
			tmp2 = MULTIPLY(d2, FIX(0.541196100));
			tmp3 = MULTIPLY(d2, FIX(1.306562965));

			tmp10 = tmp3;
			tmp13 = -tmp3;
			tmp11 = tmp2;
			tmp12 = -tmp2;
		    }
		} else {
		    if (d0) {
			/* d0 != 0, d2 == 0, d4 == 0, d6 == 0 */
			tmp10 = tmp13 = tmp11 = tmp12 = SCALE (d0, CONST_BITS);
		    } else {
			/* d0 == 0, d2 == 0, d4 == 0, d6 == 0 */
			tmp10 = tmp13 = tmp11 = tmp12 = 0;
		    }
		}
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
			z5 = MULTIPLY(z3 + z4, FIX(1.175875602));

			tmp0 = MULTIPLY(d7, FIX(0.298631336));
			tmp1 = MULTIPLY(d5, FIX(2.053119869));
			tmp2 = MULTIPLY(d3, FIX(3.072711026));
			tmp3 = MULTIPLY(d1, FIX(1.501321110));
			z1 = MULTIPLY(z1, - FIX(0.899976223));
			z2 = MULTIPLY(z2, - FIX(2.562915447));
			z3 = MULTIPLY(z3, - FIX(1.961570560));
			z4 = MULTIPLY(z4, - FIX(0.390180644));

			z3 += z5;
			z4 += z5;

			tmp0 += z1 + z3;
			tmp1 += z2 + z4;
			tmp2 += z2 + z3;
			tmp3 += z1 + z4;
		    } else {
			/* d1 == 0, d3 != 0, d5 != 0, d7 != 0 */
			z1 = d7;
			z2 = d5 + d3;
			z3 = d7 + d3;
			z5 = MULTIPLY(z3 + d5, FIX(1.175875602));

			tmp0 = MULTIPLY(d7, FIX(0.298631336));
			tmp1 = MULTIPLY(d5, FIX(2.053119869));
			tmp2 = MULTIPLY(d3, FIX(3.072711026));
			z1 = MULTIPLY(d7, - FIX(0.899976223));
			z2 = MULTIPLY(z2, - FIX(2.562915447));
			z3 = MULTIPLY(z3, - FIX(1.961570560));
			z4 = MULTIPLY(d5, - FIX(0.390180644));

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
			z2 = d5;
			z3 = d7;
			z4 = d5 + d1;
			z5 = MULTIPLY(z3 + z4, FIX(1.175875602));

			tmp0 = MULTIPLY(d7, FIX(0.298631336));
			tmp1 = MULTIPLY(d5, FIX(2.053119869));
			tmp3 = MULTIPLY(d1, FIX(1.501321110));
			z1 = MULTIPLY(z1, - FIX(0.899976223));
			z2 = MULTIPLY(d5, - FIX(2.562915447));
			z3 = MULTIPLY(d7, - FIX(1.961570560));
			z4 = MULTIPLY(z4, - FIX(0.390180644));

			z3 += z5;
			z4 += z5;

			tmp0 += z1 + z3;
			tmp1 += z2 + z4;
			tmp2 = z2 + z3;
			tmp3 += z1 + z4;
		    } else {
			/* d1 == 0, d3 == 0, d5 != 0, d7 != 0 */
			tmp0 = MULTIPLY(d7, - FIX(0.601344887));
			z1 = MULTIPLY(d7, - FIX(0.899976223));
			z3 = MULTIPLY(d7, - FIX(1.961570560));
			tmp1 = MULTIPLY(d5, - FIX(0.509795578));
			z2 = MULTIPLY(d5, - FIX(2.562915447));
			z4 = MULTIPLY(d5, - FIX(0.390180644));
			z5 = MULTIPLY(d5 + d7, FIX(1.175875602));

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
			z5 = MULTIPLY(z3 + d1, FIX(1.175875602));

			tmp0 = MULTIPLY(d7, FIX(0.298631336));
			tmp2 = MULTIPLY(d3, FIX(3.072711026));
			tmp3 = MULTIPLY(d1, FIX(1.501321110));
			z1 = MULTIPLY(z1, - FIX(0.899976223));
			z2 = MULTIPLY(d3, - FIX(2.562915447));
			z3 = MULTIPLY(z3, - FIX(1.961570560));
			z4 = MULTIPLY(d1, - FIX(0.390180644));

			z3 += z5;
			z4 += z5;

			tmp0 += z1 + z3;
			tmp1 = z2 + z4;
			tmp2 += z2 + z3;
			tmp3 += z1 + z4;
		    } else {
			/* d1 == 0, d3 != 0, d5 == 0, d7 != 0 */
			z3 = d7 + d3;

			tmp0 = MULTIPLY(d7, - FIX(0.601344887));
			z1 = MULTIPLY(d7, - FIX(0.899976223));
			tmp2 = MULTIPLY(d3, FIX(0.509795579));
			z2 = MULTIPLY(d3, - FIX(2.562915447));
			z5 = MULTIPLY(z3, FIX(1.175875602));
			z3 = MULTIPLY(z3, - FIX(0.785694958));

			tmp0 += z3;
			tmp1 = z2 + z5;
			tmp2 += z3;
			tmp3 = z1 + z5;
		    }
		} else {
		    if (d1) {
			/* d1 != 0, d3 == 0, d5 == 0, d7 != 0 */
			z1 = d7 + d1;
			z5 = MULTIPLY(z1, FIX(1.175875602));

			z1 = MULTIPLY(z1, FIX(0.275899379));
			z3 = MULTIPLY(d7, - FIX(1.961570560));
			tmp0 = MULTIPLY(d7, - FIX(1.662939224));
			z4 = MULTIPLY(d1, - FIX(0.390180644));
			tmp3 = MULTIPLY(d1, FIX(1.111140466));

			tmp0 += z1;
			tmp1 = z4 + z5;
			tmp2 = z3 + z5;
			tmp3 += z1;
		    } else {
			/* d1 == 0, d3 == 0, d5 == 0, d7 != 0 */
			tmp0 = MULTIPLY(d7, - FIX(1.387039845));
			tmp1 = MULTIPLY(d7, FIX(1.175875602));
			tmp2 = MULTIPLY(d7, - FIX(0.785694958));
			tmp3 = MULTIPLY(d7, FIX(0.275899379));
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
			z5 = MULTIPLY(d3 + z4, FIX(1.175875602));

			tmp1 = MULTIPLY(d5, FIX(2.053119869));
			tmp2 = MULTIPLY(d3, FIX(3.072711026));
			tmp3 = MULTIPLY(d1, FIX(1.501321110));
			z1 = MULTIPLY(d1, - FIX(0.899976223));
			z2 = MULTIPLY(z2, - FIX(2.562915447));
			z3 = MULTIPLY(d3, - FIX(1.961570560));
			z4 = MULTIPLY(z4, - FIX(0.390180644));

			z3 += z5;
			z4 += z5;

			tmp0 = z1 + z3;
			tmp1 += z2 + z4;
			tmp2 += z2 + z3;
			tmp3 += z1 + z4;
		    } else {
			/* d1 == 0, d3 != 0, d5 != 0, d7 == 0 */
			z2 = d5 + d3;

			z5 = MULTIPLY(z2, FIX(1.175875602));
			tmp1 = MULTIPLY(d5, FIX(1.662939225));
			z4 = MULTIPLY(d5, - FIX(0.390180644));
			z2 = MULTIPLY(z2, - FIX(1.387039845));
			tmp2 = MULTIPLY(d3, FIX(1.111140466));
			z3 = MULTIPLY(d3, - FIX(1.961570560));

			tmp0 = z3 + z5;
			tmp1 += z2;
			tmp2 += z2;
			tmp3 = z4 + z5;
		    }
		} else {
		    if (d1) {
			/* d1 != 0, d3 == 0, d5 != 0, d7 == 0 */
			z4 = d5 + d1;

			z5 = MULTIPLY(z4, FIX(1.175875602));
			z1 = MULTIPLY(d1, - FIX(0.899976223));
			tmp3 = MULTIPLY(d1, FIX(0.601344887));
			tmp1 = MULTIPLY(d5, - FIX(0.509795578));
			z2 = MULTIPLY(d5, - FIX(2.562915447));
			z4 = MULTIPLY(z4, FIX(0.785694958));

			tmp0 = z1 + z5;
			tmp1 += z4;
			tmp2 = z2 + z5;
			tmp3 += z4;
		    } else {
			/* d1 == 0, d3 == 0, d5 != 0, d7 == 0 */
			tmp0 = MULTIPLY(d5, FIX(1.175875602));
			tmp1 = MULTIPLY(d5, FIX(0.275899380));
			tmp2 = MULTIPLY(d5, - FIX(1.387039845));
			tmp3 = MULTIPLY(d5, FIX(0.785694958));
		    }
		}
	    } else {
		if (d3) {
		    if (d1) {
			/* d1 != 0, d3 != 0, d5 == 0, d7 == 0 */
			z5 = d1 + d3;
			tmp3 = MULTIPLY(d1, FIX(0.211164243));
			tmp2 = MULTIPLY(d3, - FIX(1.451774981));
			z1 = MULTIPLY(d1, FIX(1.061594337));
			z2 = MULTIPLY(d3, - FIX(2.172734803));
			z4 = MULTIPLY(z5, FIX(0.785694958));
			z5 = MULTIPLY(z5, FIX(1.175875602));

			tmp0 = z1 - z4;
			tmp1 = z2 + z4;
			tmp2 += z5;
			tmp3 += z5;
		    } else {
			/* d1 == 0, d3 != 0, d5 == 0, d7 == 0 */
			tmp0 = MULTIPLY(d3, - FIX(0.785694958));
			tmp1 = MULTIPLY(d3, - FIX(1.387039845));
			tmp2 = MULTIPLY(d3, - FIX(0.275899379));
			tmp3 = MULTIPLY(d3, FIX(1.175875602));
		    }
		} else {
		    if (d1) {
			/* d1 != 0, d3 == 0, d5 == 0, d7 == 0 */
			tmp0 = MULTIPLY(d1, FIX(0.275899379));
			tmp1 = MULTIPLY(d1, FIX(0.785694958));
			tmp2 = MULTIPLY(d1, FIX(1.175875602));
			tmp3 = MULTIPLY(d1, FIX(1.387039845));
		    } else {
			/* d1 == 0, d3 == 0, d5 == 0, d7 == 0 */
			tmp0 = tmp1 = tmp2 = tmp3 = 0;
		    }
		}
	    }
	}

	/* Final output stage: inputs are tmp10..tmp13, tmp0..tmp3 */

	dataptr[0] = (DCTELEM) DESCALE(tmp10 + tmp3, CONST_BITS-PASS1_BITS);
	dataptr[7] = (DCTELEM) DESCALE(tmp10 - tmp3, CONST_BITS-PASS1_BITS);
	dataptr[1] = (DCTELEM) DESCALE(tmp11 + tmp2, CONST_BITS-PASS1_BITS);
	dataptr[6] = (DCTELEM) DESCALE(tmp11 - tmp2, CONST_BITS-PASS1_BITS);
	dataptr[2] = (DCTELEM) DESCALE(tmp12 + tmp1, CONST_BITS-PASS1_BITS);
	dataptr[5] = (DCTELEM) DESCALE(tmp12 - tmp1, CONST_BITS-PASS1_BITS);
	dataptr[3] = (DCTELEM) DESCALE(tmp13 + tmp0, CONST_BITS-PASS1_BITS);
	dataptr[4] = (DCTELEM) DESCALE(tmp13 - tmp0, CONST_BITS-PASS1_BITS);

	dataptr += DCTSIZE;		/* advance pointer to next row */
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
	    if (d4) {
		if (d2) {
		    if (d0) {
			/* d0 != 0, d2 != 0, d4 != 0, d6 != 0 */
			z1 = MULTIPLY(d2 + d6, FIX(0.541196100));
			tmp2 = z1 + MULTIPLY(d6, - FIX(1.847759065));
			tmp3 = z1 + MULTIPLY(d2, FIX(0.765366865));

			tmp0 = SCALE (d0 + d4, CONST_BITS);
			tmp1 = SCALE (d0 - d4, CONST_BITS);

			tmp10 = tmp0 + tmp3;
			tmp13 = tmp0 - tmp3;
			tmp11 = tmp1 + tmp2;
			tmp12 = tmp1 - tmp2;
		    } else {
			/* d0 == 0, d2 != 0, d4 != 0, d6 != 0 */
			z1 = MULTIPLY(d2 + d6, FIX(0.541196100));
			tmp2 = z1 + MULTIPLY(d6, - FIX(1.847759065));
			tmp3 = z1 + MULTIPLY(d2, FIX(0.765366865));

			tmp0 = SCALE (d4, CONST_BITS);

			tmp10 = tmp0 + tmp3;
			tmp13 = tmp0 - tmp3;
			tmp11 = tmp2 - tmp0;
			tmp12 = -(tmp0 + tmp2);
		    }
		} else {
		    if (d0) {
			/* d0 != 0, d2 == 0, d4 != 0, d6 != 0 */
			tmp2 = MULTIPLY(d6, - FIX(1.306562965));
			tmp3 = MULTIPLY(d6, FIX(0.541196100));

			tmp0 = SCALE (d0 + d4, CONST_BITS);
			tmp1 = SCALE (d0 - d4, CONST_BITS);

			tmp10 = tmp0 + tmp3;
			tmp13 = tmp0 - tmp3;
			tmp11 = tmp1 + tmp2;
			tmp12 = tmp1 - tmp2;
		    } else {
			/* d0 == 0, d2 == 0, d4 != 0, d6 != 0 */
			tmp2 = MULTIPLY(d6, -FIX(1.306562965));
			tmp3 = MULTIPLY(d6, FIX(0.541196100));

			tmp0 = SCALE (d4, CONST_BITS);

			tmp10 = tmp0 + tmp3;
			tmp13 = tmp0 - tmp3;
			tmp11 = tmp2 - tmp0;
			tmp12 = -(tmp0 + tmp2);
		    }
		}
	    } else {
		if (d2) {
		    if (d0) {
			/* d0 != 0, d2 != 0, d4 == 0, d6 != 0 */
			z1 = MULTIPLY(d2 + d6, FIX(0.541196100));
			tmp2 = z1 + MULTIPLY(d6, - FIX(1.847759065));
			tmp3 = z1 + MULTIPLY(d2, FIX(0.765366865));

			tmp0 = SCALE (d0, CONST_BITS);

			tmp10 = tmp0 + tmp3;
			tmp13 = tmp0 - tmp3;
			tmp11 = tmp0 + tmp2;
			tmp12 = tmp0 - tmp2;
		    } else {
			/* d0 == 0, d2 != 0, d4 == 0, d6 != 0 */
			z1 = MULTIPLY(d2 + d6, FIX(0.541196100));
			tmp2 = z1 + MULTIPLY(d6, - FIX(1.847759065));
			tmp3 = z1 + MULTIPLY(d2, FIX(0.765366865));

			tmp10 = tmp3;
			tmp13 = -tmp3;
			tmp11 = tmp2;
			tmp12 = -tmp2;
		    }
		} else {
		    if (d0) {
			/* d0 != 0, d2 == 0, d4 == 0, d6 != 0 */
			tmp2 = MULTIPLY(d6, - FIX(1.306562965));
			tmp3 = MULTIPLY(d6, FIX(0.541196100));

			tmp0 = SCALE (d0, CONST_BITS);

			tmp10 = tmp0 + tmp3;
			tmp13 = tmp0 - tmp3;
			tmp11 = tmp0 + tmp2;
			tmp12 = tmp0 - tmp2;
		    } else {
			/* d0 == 0, d2 == 0, d4 == 0, d6 != 0 */
			tmp2 = MULTIPLY(d6, - FIX(1.306562965));
			tmp3 = MULTIPLY(d6, FIX(0.541196100));

			tmp10 = tmp3;
			tmp13 = -tmp3;
			tmp11 = tmp2;
			tmp12 = -tmp2;
		    }
		}
	    }
	} else {
	    if (d4) {
		if (d2) {
		    if (d0) {
			/* d0 != 0, d2 != 0, d4 != 0, d6 == 0 */
			tmp2 = MULTIPLY(d2, FIX(0.541196100));
			tmp3 = MULTIPLY(d2, FIX(1.306562965));

			tmp0 = SCALE (d0 + d4, CONST_BITS);
			tmp1 = SCALE (d0 - d4, CONST_BITS);

			tmp10 = tmp0 + tmp3;
			tmp13 = tmp0 - tmp3;
			tmp11 = tmp1 + tmp2;
			tmp12 = tmp1 - tmp2;
		    } else {
			/* d0 == 0, d2 != 0, d4 != 0, d6 == 0 */
			tmp2 = MULTIPLY(d2, FIX(0.541196100));
			tmp3 = MULTIPLY(d2, FIX(1.306562965));

			tmp0 = SCALE (d4, CONST_BITS);

			tmp10 = tmp0 + tmp3;
			tmp13 = tmp0 - tmp3;
			tmp11 = tmp2 - tmp0;
			tmp12 = -(tmp0 + tmp2);
		    }
		} else {
		    if (d0) {
			/* d0 != 0, d2 == 0, d4 != 0, d6 == 0 */
			tmp10 = tmp13 = SCALE (d0 + d4, CONST_BITS);
			tmp11 = tmp12 = SCALE (d0 - d4, CONST_BITS);
		    } else {
			/* d0 == 0, d2 == 0, d4 != 0, d6 == 0 */
			tmp10 = tmp13 = SCALE (d4, CONST_BITS);
			tmp11 = tmp12 = -tmp10;
		    }
		}
	    } else {
		if (d2) {
		    if (d0) {
			/* d0 != 0, d2 != 0, d4 == 0, d6 == 0 */
			tmp2 = MULTIPLY(d2, FIX(0.541196100));
			tmp3 = MULTIPLY(d2, FIX(1.306562965));

			tmp0 = SCALE (d0, CONST_BITS);

			tmp10 = tmp0 + tmp3;
			tmp13 = tmp0 - tmp3;
			tmp11 = tmp0 + tmp2;
			tmp12 = tmp0 - tmp2;
		    } else {
			/* d0 == 0, d2 != 0, d4 == 0, d6 == 0 */
			tmp2 = MULTIPLY(d2, FIX(0.541196100));
			tmp3 = MULTIPLY(d2, FIX(1.306562965));

			tmp10 = tmp3;
			tmp13 = -tmp3;
			tmp11 = tmp2;
			tmp12 = -tmp2;
		    }
		} else {
		    if (d0) {
			/* d0 != 0, d2 == 0, d4 == 0, d6 == 0 */
			tmp10 = tmp13 = tmp11 = tmp12 = SCALE (d0, CONST_BITS);
		    } else {
			/* d0 == 0, d2 == 0, d4 == 0, d6 == 0 */
			tmp10 = tmp13 = tmp11 = tmp12 = 0;
		    }
		}
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
			z5 = MULTIPLY(z3 + z4, FIX(1.175875602));

			tmp0 = MULTIPLY(d7, FIX(0.298631336));
			tmp1 = MULTIPLY(d5, FIX(2.053119869));
			tmp2 = MULTIPLY(d3, FIX(3.072711026));
			tmp3 = MULTIPLY(d1, FIX(1.501321110));
			z1 = MULTIPLY(z1, - FIX(0.899976223));
			z2 = MULTIPLY(z2, - FIX(2.562915447));
			z3 = MULTIPLY(z3, - FIX(1.961570560));
			z4 = MULTIPLY(z4, - FIX(0.390180644));

			z3 += z5;
			z4 += z5;

			tmp0 += z1 + z3;
			tmp1 += z2 + z4;
			tmp2 += z2 + z3;
			tmp3 += z1 + z4;
		    } else {
			/* d1 == 0, d3 != 0, d5 != 0, d7 != 0 */
			z1 = d7;
			z2 = d5 + d3;
			z3 = d7 + d3;
			z5 = MULTIPLY(z3 + d5, FIX(1.175875602));

			tmp0 = MULTIPLY(d7, FIX(0.298631336));
			tmp1 = MULTIPLY(d5, FIX(2.053119869));
			tmp2 = MULTIPLY(d3, FIX(3.072711026));
			z1 = MULTIPLY(d7, - FIX(0.899976223));
			z2 = MULTIPLY(z2, - FIX(2.562915447));
			z3 = MULTIPLY(z3, - FIX(1.961570560));
			z4 = MULTIPLY(d5, - FIX(0.390180644));

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
			z2 = d5;
			z3 = d7;
			z4 = d5 + d1;
			z5 = MULTIPLY(z3 + z4, FIX(1.175875602));

			tmp0 = MULTIPLY(d7, FIX(0.298631336));
			tmp1 = MULTIPLY(d5, FIX(2.053119869));
			tmp3 = MULTIPLY(d1, FIX(1.501321110));
			z1 = MULTIPLY(z1, - FIX(0.899976223));
			z2 = MULTIPLY(d5, - FIX(2.562915447));
			z3 = MULTIPLY(d7, - FIX(1.961570560));
			z4 = MULTIPLY(z4, - FIX(0.390180644));

			z3 += z5;
			z4 += z5;

			tmp0 += z1 + z3;
			tmp1 += z2 + z4;
			tmp2 = z2 + z3;
			tmp3 += z1 + z4;
		    } else {
			/* d1 == 0, d3 == 0, d5 != 0, d7 != 0 */
			tmp0 = MULTIPLY(d7, - FIX(0.601344887));
			z1 = MULTIPLY(d7, - FIX(0.899976223));
			z3 = MULTIPLY(d7, - FIX(1.961570560));
			tmp1 = MULTIPLY(d5, - FIX(0.509795578));
			z2 = MULTIPLY(d5, - FIX(2.562915447));
			z4 = MULTIPLY(d5, - FIX(0.390180644));
			z5 = MULTIPLY(d5 + d7, FIX(1.175875602));

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
			z5 = MULTIPLY(z3 + d1, FIX(1.175875602));

			tmp0 = MULTIPLY(d7, FIX(0.298631336));
			tmp2 = MULTIPLY(d3, FIX(3.072711026));
			tmp3 = MULTIPLY(d1, FIX(1.501321110));
			z1 = MULTIPLY(z1, - FIX(0.899976223));
			z2 = MULTIPLY(d3, - FIX(2.562915447));
			z3 = MULTIPLY(z3, - FIX(1.961570560));
			z4 = MULTIPLY(d1, - FIX(0.390180644));

			z3 += z5;
			z4 += z5;

			tmp0 += z1 + z3;
			tmp1 = z2 + z4;
			tmp2 += z2 + z3;
			tmp3 += z1 + z4;
		    } else {
			/* d1 == 0, d3 != 0, d5 == 0, d7 != 0 */
			z3 = d7 + d3;

			tmp0 = MULTIPLY(d7, - FIX(0.601344887));
			z1 = MULTIPLY(d7, - FIX(0.899976223));
			tmp2 = MULTIPLY(d3, FIX(0.509795579));
			z2 = MULTIPLY(d3, - FIX(2.562915447));
			z5 = MULTIPLY(z3, FIX(1.175875602));
			z3 = MULTIPLY(z3, - FIX(0.785694958));

			tmp0 += z3;
			tmp1 = z2 + z5;
			tmp2 += z3;
			tmp3 = z1 + z5;
		    }
		} else {
		    if (d1) {
			/* d1 != 0, d3 == 0, d5 == 0, d7 != 0 */
			z1 = d7 + d1;
			z5 = MULTIPLY(z1, FIX(1.175875602));

			z1 = MULTIPLY(z1, FIX(0.275899379));
			z3 = MULTIPLY(d7, - FIX(1.961570560));
			tmp0 = MULTIPLY(d7, - FIX(1.662939224));
			z4 = MULTIPLY(d1, - FIX(0.390180644));
			tmp3 = MULTIPLY(d1, FIX(1.111140466));

			tmp0 += z1;
			tmp1 = z4 + z5;
			tmp2 = z3 + z5;
			tmp3 += z1;
		    } else {
			/* d1 == 0, d3 == 0, d5 == 0, d7 != 0 */
			tmp0 = MULTIPLY(d7, - FIX(1.387039845));
			tmp1 = MULTIPLY(d7, FIX(1.175875602));
			tmp2 = MULTIPLY(d7, - FIX(0.785694958));
			tmp3 = MULTIPLY(d7, FIX(0.275899379));
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
			z5 = MULTIPLY(d3 + z4, FIX(1.175875602));

			tmp1 = MULTIPLY(d5, FIX(2.053119869));
			tmp2 = MULTIPLY(d3, FIX(3.072711026));
			tmp3 = MULTIPLY(d1, FIX(1.501321110));
			z1 = MULTIPLY(d1, - FIX(0.899976223));
			z2 = MULTIPLY(z2, - FIX(2.562915447));
			z3 = MULTIPLY(d3, - FIX(1.961570560));
			z4 = MULTIPLY(z4, - FIX(0.390180644));

			z3 += z5;
			z4 += z5;

			tmp0 = z1 + z3;
			tmp1 += z2 + z4;
			tmp2 += z2 + z3;
			tmp3 += z1 + z4;
		    } else {
			/* d1 == 0, d3 != 0, d5 != 0, d7 == 0 */
			z2 = d5 + d3;

			z5 = MULTIPLY(z2, FIX(1.175875602));
			tmp1 = MULTIPLY(d5, FIX(1.662939225));
			z4 = MULTIPLY(d5, - FIX(0.390180644));
			z2 = MULTIPLY(z2, - FIX(1.387039845));
			tmp2 = MULTIPLY(d3, FIX(1.111140466));
			z3 = MULTIPLY(d3, - FIX(1.961570560));

			tmp0 = z3 + z5;
			tmp1 += z2;
			tmp2 += z2;
			tmp3 = z4 + z5;
		    }
		} else {
		    if (d1) {
			/* d1 != 0, d3 == 0, d5 != 0, d7 == 0 */
			z4 = d5 + d1;

			z5 = MULTIPLY(z4, FIX(1.175875602));
			z1 = MULTIPLY(d1, - FIX(0.899976223));
			tmp3 = MULTIPLY(d1, FIX(0.601344887));
			tmp1 = MULTIPLY(d5, - FIX(0.509795578));
			z2 = MULTIPLY(d5, - FIX(2.562915447));
			z4 = MULTIPLY(z4, FIX(0.785694958));

			tmp0 = z1 + z5;
			tmp1 += z4;
			tmp2 = z2 + z5;
			tmp3 += z4;
		    } else {
			/* d1 == 0, d3 == 0, d5 != 0, d7 == 0 */
			tmp0 = MULTIPLY(d5, FIX(1.175875602));
			tmp1 = MULTIPLY(d5, FIX(0.275899380));
			tmp2 = MULTIPLY(d5, - FIX(1.387039845));
			tmp3 = MULTIPLY(d5, FIX(0.785694958));
		    }
		}
	    } else {
		if (d3) {
		    if (d1) {
			/* d1 != 0, d3 != 0, d5 == 0, d7 == 0 */
			z5 = d1 + d3;
			tmp3 = MULTIPLY(d1, FIX(0.211164243));
			tmp2 = MULTIPLY(d3, - FIX(1.451774981));
			z1 = MULTIPLY(d1, FIX(1.061594337));
			z2 = MULTIPLY(d3, - FIX(2.172734803));
			z4 = MULTIPLY(z5, FIX(0.785694958));
			z5 = MULTIPLY(z5, FIX(1.175875602));

			tmp0 = z1 - z4;
			tmp1 = z2 + z4;
			tmp2 += z5;
			tmp3 += z5;
		    } else {
			/* d1 == 0, d3 != 0, d5 == 0, d7 == 0 */
			tmp0 = MULTIPLY(d3, - FIX(0.785694958));
			tmp1 = MULTIPLY(d3, - FIX(1.387039845));
			tmp2 = MULTIPLY(d3, - FIX(0.275899379));
			tmp3 = MULTIPLY(d3, FIX(1.175875602));
		    }
		} else {
		    if (d1) {
			/* d1 != 0, d3 == 0, d5 == 0, d7 == 0 */
			tmp0 = MULTIPLY(d1, FIX(0.275899379));
			tmp1 = MULTIPLY(d1, FIX(0.785694958));
			tmp2 = MULTIPLY(d1, FIX(1.175875602));
			tmp3 = MULTIPLY(d1, FIX(1.387039845));
		    } else {
			/* d1 == 0, d3 == 0, d5 == 0, d7 == 0 */
			tmp0 = tmp1 = tmp2 = tmp3 = 0;
		    }
		}
	    }
	}

	/* Final output stage: inputs are tmp10..tmp13, tmp0..tmp3 */

	dataptr[DCTSIZE*0] = (DCTELEM) DESCALE(tmp10 + tmp3,
	    CONST_BITS+PASS1_BITS+3);
	dataptr[DCTSIZE*7] = (DCTELEM) DESCALE(tmp10 - tmp3,
	    CONST_BITS+PASS1_BITS+3);
	dataptr[DCTSIZE*1] = (DCTELEM) DESCALE(tmp11 + tmp2,
	    CONST_BITS+PASS1_BITS+3);
	dataptr[DCTSIZE*6] = (DCTELEM) DESCALE(tmp11 - tmp2,
	    CONST_BITS+PASS1_BITS+3);
	dataptr[DCTSIZE*2] = (DCTELEM) DESCALE(tmp12 + tmp1,
	    CONST_BITS+PASS1_BITS+3);
	dataptr[DCTSIZE*5] = (DCTELEM) DESCALE(tmp12 - tmp1,
	    CONST_BITS+PASS1_BITS+3);
	dataptr[DCTSIZE*3] = (DCTELEM) DESCALE(tmp13 + tmp0,
	    CONST_BITS+PASS1_BITS+3);
	dataptr[DCTSIZE*4] = (DCTELEM) DESCALE(tmp13 - tmp0,
	    CONST_BITS+PASS1_BITS+3);

	dataptr++;			/* advance pointer to next column */
    }
}

#else

/*---- debugging/tracing macros ----*/

#if _MSC_VER
#pragma optimize("",on)
#if _MSC_VER > 700
/*#pragma optimize("l",off)*/
#endif
#endif

#define idct_single_pos0()
#define idct_zero_col_stat()
#define idct_zero_row_stat()
#define idct_nonzero_col_stat()
#define idct_nonzero_row_stat()
#define DUMP_COEFS(p)
#define TRACE(args)
#define FAST_DCTPTRS	1

#if 0	/* to count cases */
void idct_single_pos0 (void) { static int count; count++; }
void idct_zero_col_stat (void) { static int count; count++; }
void idct_zero_row_stat (void) { static int count; count++; }
void idct_nonzero_col_stat (void) { static int count; count++; }
void idct_nonzero_row_stat (void) { static int count; count++; }
#undef idct_single_pos0
#undef idct_zero_col_stat
#undef idct_zero_row_stat
#undef idct_nonzero_col_stat
#undef idct_nonzero_row_stat
#endif

void init_pre_idct (void) { }

void j_rev_dct_sparse (DCTBLOCK data, int pos)
{
    /* If just DC Coefficient. */

    if (pos == 0) {
	register DCTELEM *dp, *dq;
	DCTELEM dcval;

	idct_single_pos0();

	dp = data;
	dcval = dp[0];
	if (dcval < 0)
	    dcval = (short)((dcval - 3) >> 3);
	else
	    dcval = (short)((dcval + 4) >> 3);

	if (dcval) {
	    for (dq = dp + 64; dp < dq; dp += 8) {
		dp[3] = dp[2] = dp[1] = dp[0] = dcval;
		dp[7] = dp[6] = dp[5] = dp[4] = dcval;
	    }
	}
	return;
    }

    /* Some other coeff */
    j_rev_dct (data);
}

#ifndef OPTIMIZE_ASM
void j_rev_dct (DCTBLOCK data)
{
    INT32 tmp0, tmp1, tmp2, tmp3;
    INT32 tmp10, tmp11, tmp12, tmp13;
    INT32 z1, z2, z3, z4, z5;
    register DCTELEM *dp;
    int rowctr;
    SHIFT_TEMPS;

    /* Pass 1: process rows. */
    /* Note results are scaled up by sqrt(8) compared to a true IDCT; */
    /* furthermore, we scale the results by 2**PASS1_BITS. */

    DUMP_COEFS(data);

    dp = data;
    for (rowctr = DCTSIZE-1; rowctr >= 0; rowctr--, dp += DCTSIZE) {
	/* Due to quantization, we will usually find that many of the input
	 * coefficients are zero, especially the AC terms.  We can exploit this
	 * by short-circuiting the IDCT calculation for any row in which all
	 * the AC terms are zero.  In that case each output is equal to the
	 * DC coefficient (with scale factor as needed).
	 * With typical images and quantization tables, half or more of the
	 * row DCT calculations can be simplified this way.
	 */

#if FAST_DCTPTRS
#define d0	dp[0]
#define d1	dp[1]
#define d2	dp[2]
#define d3	dp[3]
#define d4	dp[4]
#define d5	dp[5]
#define d6	dp[6]
#define d7	dp[7]
#else
	int d0 = dp[0];
	int d1 = dp[1];
	int d2 = dp[2];
	int d3 = dp[3];
	int d4 = dp[4];
	int d5 = dp[5];
	int d6 = dp[6];
	int d7 = dp[7];
#endif

#ifndef NO_ZERO_ROW_TEST
	if ((d1 | d2 | d3 | d4 | d5 | d6 | d7) == 0) {
	    /* AC terms all zero */
	    DCTELEM dcval = (DCTELEM) (d0 << PASS1_BITS);

	    if (d0) {
		dp[0] = dcval;
		dp[1] = dcval;
		dp[2] = dcval;
		dp[3] = dcval;
		dp[4] = dcval;
		dp[5] = dcval;
		dp[6] = dcval;
		dp[7] = dcval;
	    }
	    idct_zero_row_stat();
	    continue;
	}
#endif

	idct_nonzero_row_stat();

	/* Even part: reverse the even part of the forward DCT. */
	/* The rotator is sqrt(2)*c(-6). */

	z1 = MULTIPLY(d2 + d6, FIX(0.541196100));
	tmp2 = z1 + MULTIPLY(d6, - FIX(1.847759065));
	tmp3 = z1 + MULTIPLY(d2, FIX(0.765366865));

	tmp0 = SCALE (d0 + d4, CONST_BITS);
	tmp1 = SCALE (d0 - d4, CONST_BITS);

	tmp10 = tmp0 + tmp3;
	tmp13 = tmp0 - tmp3;
	tmp11 = tmp1 + tmp2;
	tmp12 = tmp1 - tmp2;

	/* Odd part per figure 8; the matrix is unitary and hence its
	 * transpose is its inverse.  i0..i3 are y7,y5,y3,y1 respectively.
	 */

	z1 = d7 + d1;
	z2 = d5 + d3;
	z3 = d7 + d3;
	z4 = d5 + d1;
	z5 = MULTIPLY(z3 + z4, FIX(1.175875602)); /* sqrt(2) * c3 */

	tmp0 = MULTIPLY(d7, FIX(0.298631336)); /* sqrt(2) * (-c1+c3+c5-c7) */
	tmp1 = MULTIPLY(d5, FIX(2.053119869)); /* sqrt(2) * ( c1+c3-c5+c7) */
	tmp2 = MULTIPLY(d3, FIX(3.072711026)); /* sqrt(2) * ( c1+c3+c5-c7) */
	tmp3 = MULTIPLY(d1, FIX(1.501321110)); /* sqrt(2) * ( c1+c3-c5-c7) */
	z1 = MULTIPLY(z1, - FIX(0.899976223)); /* sqrt(2) * (c7-c3) */
	z2 = MULTIPLY(z2, - FIX(2.562915447)); /* sqrt(2) * (-c1-c3) */
	z3 = MULTIPLY(z3, - FIX(1.961570560)); /* sqrt(2) * (-c3-c5) */
	z4 = MULTIPLY(z4, - FIX(0.390180644)); /* sqrt(2) * (c5-c3) */

	z3 += z5;
	z4 += z5;

	tmp0 += z1 + z3;
	tmp1 += z2 + z4;
	tmp2 += z2 + z3;
	tmp3 += z1 + z4;

	/* Final output stage: inputs are tmp10..tmp13, tmp0..tmp3 */

	dp[0] = (DCTELEM) DESCALE(tmp10 + tmp3, CONST_BITS-PASS1_BITS);
	dp[7] = (DCTELEM) DESCALE(tmp10 - tmp3, CONST_BITS-PASS1_BITS);
	dp[1] = (DCTELEM) DESCALE(tmp11 + tmp2, CONST_BITS-PASS1_BITS);
	dp[6] = (DCTELEM) DESCALE(tmp11 - tmp2, CONST_BITS-PASS1_BITS);
	dp[2] = (DCTELEM) DESCALE(tmp12 + tmp1, CONST_BITS-PASS1_BITS);
	dp[5] = (DCTELEM) DESCALE(tmp12 - tmp1, CONST_BITS-PASS1_BITS);
	dp[3] = (DCTELEM) DESCALE(tmp13 + tmp0, CONST_BITS-PASS1_BITS);
	dp[4] = (DCTELEM) DESCALE(tmp13 - tmp0, CONST_BITS-PASS1_BITS);
    }
#if FAST_DCTPTRS
#undef d0
#undef d1
#undef d2
#undef d3
#undef d4
#undef d5
#undef d6
#undef d7
#endif

    /* Pass 2: process columns. */
    /* Note that we must descale the results by a factor of 8 == 2**3, */
    /* and also undo the PASS1_BITS scaling. */

    dp = data;
    for (rowctr = DCTSIZE-1; rowctr >= 0; rowctr--, dp++) {
	/* Columns of zeroes can be exploited in the same way as we did with rows.
	 * However, the row calculation has created many nonzero AC terms, so the
	 * simplification applies less often (typically 5% to 10% of the time).
	 * On machines with very fast multiplication, it's possible that the
	 * test takes more time than it's worth.  In that case this section
	 * may be commented out.
	 */

#if FAST_DCTPTRS
#define d0	dp[DCTSIZE*0]
#define d1	dp[DCTSIZE*1]
#define d2	dp[DCTSIZE*2]
#define d3	dp[DCTSIZE*3]
#define d4	dp[DCTSIZE*4]
#define d5	dp[DCTSIZE*5]
#define d6	dp[DCTSIZE*6]
#define d7	dp[DCTSIZE*7]
#else
	int d0 = dp[DCTSIZE*0];
	int d1 = dp[DCTSIZE*1];
	int d2 = dp[DCTSIZE*2];
	int d3 = dp[DCTSIZE*3];
	int d4 = dp[DCTSIZE*4];
	int d5 = dp[DCTSIZE*5];
	int d6 = dp[DCTSIZE*6];
	int d7 = dp[DCTSIZE*7];
#endif

#ifndef NO_ZERO_COLUMN_TEST
	if ((d1 | d2 | d3 | d4 | d5 | d6 | d7) == 0) {
	    /* AC terms all zero */
	    DCTELEM dcval = (DCTELEM) DESCALE((INT32) d0, PASS1_BITS+3);

	    if (d0) {
		dp[DCTSIZE*0] = dcval;
		dp[DCTSIZE*1] = dcval;
		dp[DCTSIZE*2] = dcval;
		dp[DCTSIZE*3] = dcval;
		dp[DCTSIZE*4] = dcval;
		dp[DCTSIZE*5] = dcval;
		dp[DCTSIZE*6] = dcval;
		dp[DCTSIZE*7] = dcval;
	    }
	    idct_zero_col_stat();
	    continue;
	}
#endif

	idct_nonzero_col_stat();

	/* Even part: reverse the even part of the forward DCT. */
	/* The rotator is sqrt(2)*c(-6). */

	z1 = MULTIPLY(d2 + d6, FIX(0.541196100));
	tmp2 = z1 + MULTIPLY(d6, - FIX(1.847759065));
	tmp3 = z1 + MULTIPLY(d2, FIX(0.765366865));

	tmp0 = SCALE (d0 + d4, CONST_BITS);
	tmp1 = SCALE (d0 - d4, CONST_BITS);

	tmp10 = tmp0 + tmp3;
	tmp13 = tmp0 - tmp3;
	tmp11 = tmp1 + tmp2;
	tmp12 = tmp1 - tmp2;

	/* Odd part per figure 8; the matrix is unitary and hence its
	 * transpose is its inverse.  i0..i3 are y7,y5,y3,y1 respectively.
	 */

	z1 = d7 + d1;
	z2 = d5 + d3;
	z3 = d7 + d3;
	z4 = d5 + d1;
	z5 = MULTIPLY(z3 + z4, FIX(1.175875602)); /* sqrt(2) * c3 */

	tmp0 = MULTIPLY(d7, FIX(0.298631336)); /* sqrt(2) * (-c1+c3+c5-c7) */
	tmp1 = MULTIPLY(d5, FIX(2.053119869)); /* sqrt(2) * ( c1+c3-c5+c7) */
	tmp2 = MULTIPLY(d3, FIX(3.072711026)); /* sqrt(2) * ( c1+c3+c5-c7) */
	tmp3 = MULTIPLY(d1, FIX(1.501321110)); /* sqrt(2) * ( c1+c3-c5-c7) */
	z1 = MULTIPLY(z1, - FIX(0.899976223)); /* sqrt(2) * (c7-c3) */
	z2 = MULTIPLY(z2, - FIX(2.562915447)); /* sqrt(2) * (-c1-c3) */
	z3 = MULTIPLY(z3, - FIX(1.961570560)); /* sqrt(2) * (-c3-c5) */
	z4 = MULTIPLY(z4, - FIX(0.390180644)); /* sqrt(2) * (c5-c3) */

	z3 += z5;
	z4 += z5;

	tmp0 += z1 + z3;
	tmp1 += z2 + z4;
	tmp2 += z2 + z3;
	tmp3 += z1 + z4;

	/* Final output stage: inputs are tmp10..tmp13, tmp0..tmp3 */

	dp[DCTSIZE*0] = (DCTELEM)DESCALE(tmp10 + tmp3, CONST_BITS+PASS1_BITS+3);
	dp[DCTSIZE*7] = (DCTELEM)DESCALE(tmp10 - tmp3, CONST_BITS+PASS1_BITS+3);
	dp[DCTSIZE*1] = (DCTELEM)DESCALE(tmp11 + tmp2, CONST_BITS+PASS1_BITS+3);
	dp[DCTSIZE*6] = (DCTELEM)DESCALE(tmp11 - tmp2, CONST_BITS+PASS1_BITS+3);
	dp[DCTSIZE*2] = (DCTELEM)DESCALE(tmp12 + tmp1, CONST_BITS+PASS1_BITS+3);
	dp[DCTSIZE*5] = (DCTELEM)DESCALE(tmp12 - tmp1, CONST_BITS+PASS1_BITS+3);
	dp[DCTSIZE*3] = (DCTELEM)DESCALE(tmp13 + tmp0, CONST_BITS+PASS1_BITS+3);
	dp[DCTSIZE*4] = (DCTELEM)DESCALE(tmp13 - tmp0, CONST_BITS+PASS1_BITS+3);
    }
#if FAST_DCTPTRS
#undef d0
#undef d1
#undef d2
#undef d3
#undef d4
#undef d5
#undef d6
#undef d7
#endif
}
#endif	/* optimize.asm */

#endif
