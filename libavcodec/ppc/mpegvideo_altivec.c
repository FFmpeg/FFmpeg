/*
 * Copyright (c) 2002 Dieter Shirley
 *
 * dct_unquantize_h263_altivec:
 * Copyright (c) 2003 Romain Dolbeau <romain@dolbeau.org>
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

#include <stdlib.h>
#include <stdio.h>
#include "libavutil/cpu.h"
#include "libavcodec/dsputil.h"
#include "libavcodec/mpegvideo.h"

#include "util_altivec.h"
#include "types_altivec.h"
#include "dsputil_altivec.h"

// Swaps two variables (used for altivec registers)
#define SWAP(a,b) \
do { \
    __typeof__(a) swap_temp=a; \
    a=b; \
    b=swap_temp; \
} while (0)

// transposes a matrix consisting of four vectors with four elements each
#define TRANSPOSE4(a,b,c,d) \
do { \
    __typeof__(a) _trans_ach = vec_mergeh(a, c); \
    __typeof__(a) _trans_acl = vec_mergel(a, c); \
    __typeof__(a) _trans_bdh = vec_mergeh(b, d); \
    __typeof__(a) _trans_bdl = vec_mergel(b, d); \
                                                 \
    a = vec_mergeh(_trans_ach, _trans_bdh);      \
    b = vec_mergel(_trans_ach, _trans_bdh);      \
    c = vec_mergeh(_trans_acl, _trans_bdl);      \
    d = vec_mergel(_trans_acl, _trans_bdl);      \
} while (0)


// Loads a four-byte value (int or float) from the target address
// into every element in the target vector.  Only works if the
// target address is four-byte aligned (which should be always).
#define LOAD4(vec, address) \
{ \
    __typeof__(vec)* _load_addr = (__typeof__(vec)*)(address);  \
    vector unsigned char _perm_vec = vec_lvsl(0,(address));     \
    vec = vec_ld(0, _load_addr);                                \
    vec = vec_perm(vec, vec, _perm_vec);                        \
    vec = vec_splat(vec, 0);                                    \
}


#define FOUROF(a) {a,a,a,a}

static int dct_quantize_altivec(MpegEncContext* s,
                         DCTELEM* data, int n,
                         int qscale, int* overflow)
{
    int lastNonZero;
    vector float row0, row1, row2, row3, row4, row5, row6, row7;
    vector float alt0, alt1, alt2, alt3, alt4, alt5, alt6, alt7;
    const vector float zero = (const vector float)FOUROF(0.);
    // used after quantize step
    int oldBaseValue = 0;

    // Load the data into the row/alt vectors
    {
        vector signed short data0, data1, data2, data3, data4, data5, data6, data7;

        data0 = vec_ld(0, data);
        data1 = vec_ld(16, data);
        data2 = vec_ld(32, data);
        data3 = vec_ld(48, data);
        data4 = vec_ld(64, data);
        data5 = vec_ld(80, data);
        data6 = vec_ld(96, data);
        data7 = vec_ld(112, data);

        // Transpose the data before we start
        TRANSPOSE8(data0, data1, data2, data3, data4, data5, data6, data7);

        // load the data into floating point vectors.  We load
        // the high half of each row into the main row vectors
        // and the low half into the alt vectors.
        row0 = vec_ctf(vec_unpackh(data0), 0);
        alt0 = vec_ctf(vec_unpackl(data0), 0);
        row1 = vec_ctf(vec_unpackh(data1), 0);
        alt1 = vec_ctf(vec_unpackl(data1), 0);
        row2 = vec_ctf(vec_unpackh(data2), 0);
        alt2 = vec_ctf(vec_unpackl(data2), 0);
        row3 = vec_ctf(vec_unpackh(data3), 0);
        alt3 = vec_ctf(vec_unpackl(data3), 0);
        row4 = vec_ctf(vec_unpackh(data4), 0);
        alt4 = vec_ctf(vec_unpackl(data4), 0);
        row5 = vec_ctf(vec_unpackh(data5), 0);
        alt5 = vec_ctf(vec_unpackl(data5), 0);
        row6 = vec_ctf(vec_unpackh(data6), 0);
        alt6 = vec_ctf(vec_unpackl(data6), 0);
        row7 = vec_ctf(vec_unpackh(data7), 0);
        alt7 = vec_ctf(vec_unpackl(data7), 0);
    }

    // The following block could exist as a separate an altivec dct
                // function.  However, if we put it inline, the DCT data can remain
                // in the vector local variables, as floats, which we'll use during the
                // quantize step...
    {
        const vector float vec_0_298631336 = (vector float)FOUROF(0.298631336f);
        const vector float vec_0_390180644 = (vector float)FOUROF(-0.390180644f);
        const vector float vec_0_541196100 = (vector float)FOUROF(0.541196100f);
        const vector float vec_0_765366865 = (vector float)FOUROF(0.765366865f);
        const vector float vec_0_899976223 = (vector float)FOUROF(-0.899976223f);
        const vector float vec_1_175875602 = (vector float)FOUROF(1.175875602f);
        const vector float vec_1_501321110 = (vector float)FOUROF(1.501321110f);
        const vector float vec_1_847759065 = (vector float)FOUROF(-1.847759065f);
        const vector float vec_1_961570560 = (vector float)FOUROF(-1.961570560f);
        const vector float vec_2_053119869 = (vector float)FOUROF(2.053119869f);
        const vector float vec_2_562915447 = (vector float)FOUROF(-2.562915447f);
        const vector float vec_3_072711026 = (vector float)FOUROF(3.072711026f);


        int whichPass, whichHalf;

        for(whichPass = 1; whichPass<=2; whichPass++) {
            for(whichHalf = 1; whichHalf<=2; whichHalf++) {
                vector float tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
                vector float tmp10, tmp11, tmp12, tmp13;
                vector float z1, z2, z3, z4, z5;

                tmp0 = vec_add(row0, row7); // tmp0 = dataptr[0] + dataptr[7];
                tmp7 = vec_sub(row0, row7); // tmp7 = dataptr[0] - dataptr[7];
                tmp3 = vec_add(row3, row4); // tmp3 = dataptr[3] + dataptr[4];
                tmp4 = vec_sub(row3, row4); // tmp4 = dataptr[3] - dataptr[4];
                tmp1 = vec_add(row1, row6); // tmp1 = dataptr[1] + dataptr[6];
                tmp6 = vec_sub(row1, row6); // tmp6 = dataptr[1] - dataptr[6];
                tmp2 = vec_add(row2, row5); // tmp2 = dataptr[2] + dataptr[5];
                tmp5 = vec_sub(row2, row5); // tmp5 = dataptr[2] - dataptr[5];

                tmp10 = vec_add(tmp0, tmp3); // tmp10 = tmp0 + tmp3;
                tmp13 = vec_sub(tmp0, tmp3); // tmp13 = tmp0 - tmp3;
                tmp11 = vec_add(tmp1, tmp2); // tmp11 = tmp1 + tmp2;
                tmp12 = vec_sub(tmp1, tmp2); // tmp12 = tmp1 - tmp2;


                // dataptr[0] = (DCTELEM) ((tmp10 + tmp11) << PASS1_BITS);
                row0 = vec_add(tmp10, tmp11);

                // dataptr[4] = (DCTELEM) ((tmp10 - tmp11) << PASS1_BITS);
                row4 = vec_sub(tmp10, tmp11);


                // z1 = MULTIPLY(tmp12 + tmp13, FIX_0_541196100);
                z1 = vec_madd(vec_add(tmp12, tmp13), vec_0_541196100, (vector float)zero);

                // dataptr[2] = (DCTELEM) DESCALE(z1 + MULTIPLY(tmp13, FIX_0_765366865),
                //                                CONST_BITS-PASS1_BITS);
                row2 = vec_madd(tmp13, vec_0_765366865, z1);

                // dataptr[6] = (DCTELEM) DESCALE(z1 + MULTIPLY(tmp12, - FIX_1_847759065),
                //                                CONST_BITS-PASS1_BITS);
                row6 = vec_madd(tmp12, vec_1_847759065, z1);

                z1 = vec_add(tmp4, tmp7); // z1 = tmp4 + tmp7;
                z2 = vec_add(tmp5, tmp6); // z2 = tmp5 + tmp6;
                z3 = vec_add(tmp4, tmp6); // z3 = tmp4 + tmp6;
                z4 = vec_add(tmp5, tmp7); // z4 = tmp5 + tmp7;

                // z5 = MULTIPLY(z3 + z4, FIX_1_175875602); /* sqrt(2) * c3 */
                z5 = vec_madd(vec_add(z3, z4), vec_1_175875602, (vector float)zero);

                // z3 = MULTIPLY(z3, - FIX_1_961570560); /* sqrt(2) * (-c3-c5) */
                z3 = vec_madd(z3, vec_1_961570560, z5);

                // z4 = MULTIPLY(z4, - FIX_0_390180644); /* sqrt(2) * (c5-c3) */
                z4 = vec_madd(z4, vec_0_390180644, z5);

                // The following adds are rolled into the multiplies above
                // z3 = vec_add(z3, z5);  // z3 += z5;
                // z4 = vec_add(z4, z5);  // z4 += z5;

                // z2 = MULTIPLY(z2, - FIX_2_562915447); /* sqrt(2) * (-c1-c3) */
                // Wow!  It's actually more efficient to roll this multiply
                // into the adds below, even thought the multiply gets done twice!
                // z2 = vec_madd(z2, vec_2_562915447, (vector float)zero);

                // z1 = MULTIPLY(z1, - FIX_0_899976223); /* sqrt(2) * (c7-c3) */
                // Same with this one...
                // z1 = vec_madd(z1, vec_0_899976223, (vector float)zero);

                // tmp4 = MULTIPLY(tmp4, FIX_0_298631336); /* sqrt(2) * (-c1+c3+c5-c7) */
                // dataptr[7] = (DCTELEM) DESCALE(tmp4 + z1 + z3, CONST_BITS-PASS1_BITS);
                row7 = vec_madd(tmp4, vec_0_298631336, vec_madd(z1, vec_0_899976223, z3));

                // tmp5 = MULTIPLY(tmp5, FIX_2_053119869); /* sqrt(2) * ( c1+c3-c5+c7) */
                // dataptr[5] = (DCTELEM) DESCALE(tmp5 + z2 + z4, CONST_BITS-PASS1_BITS);
                row5 = vec_madd(tmp5, vec_2_053119869, vec_madd(z2, vec_2_562915447, z4));

                // tmp6 = MULTIPLY(tmp6, FIX_3_072711026); /* sqrt(2) * ( c1+c3+c5-c7) */
                // dataptr[3] = (DCTELEM) DESCALE(tmp6 + z2 + z3, CONST_BITS-PASS1_BITS);
                row3 = vec_madd(tmp6, vec_3_072711026, vec_madd(z2, vec_2_562915447, z3));

                // tmp7 = MULTIPLY(tmp7, FIX_1_501321110); /* sqrt(2) * ( c1+c3-c5-c7) */
                // dataptr[1] = (DCTELEM) DESCALE(tmp7 + z1 + z4, CONST_BITS-PASS1_BITS);
                row1 = vec_madd(z1, vec_0_899976223, vec_madd(tmp7, vec_1_501321110, z4));

                // Swap the row values with the alts.  If this is the first half,
                // this sets up the low values to be acted on in the second half.
                // If this is the second half, it puts the high values back in
                // the row values where they are expected to be when we're done.
                SWAP(row0, alt0);
                SWAP(row1, alt1);
                SWAP(row2, alt2);
                SWAP(row3, alt3);
                SWAP(row4, alt4);
                SWAP(row5, alt5);
                SWAP(row6, alt6);
                SWAP(row7, alt7);
            }

            if (whichPass == 1) {
                // transpose the data for the second pass

                // First, block transpose the upper right with lower left.
                SWAP(row4, alt0);
                SWAP(row5, alt1);
                SWAP(row6, alt2);
                SWAP(row7, alt3);

                // Now, transpose each block of four
                TRANSPOSE4(row0, row1, row2, row3);
                TRANSPOSE4(row4, row5, row6, row7);
                TRANSPOSE4(alt0, alt1, alt2, alt3);
                TRANSPOSE4(alt4, alt5, alt6, alt7);
            }
        }
    }

    // perform the quantize step, using the floating point data
    // still in the row/alt registers
    {
        const int* biasAddr;
        const vector signed int* qmat;
        vector float bias, negBias;

        if (s->mb_intra) {
            vector signed int baseVector;

            // We must cache element 0 in the intra case
            // (it needs special handling).
            baseVector = vec_cts(vec_splat(row0, 0), 0);
            vec_ste(baseVector, 0, &oldBaseValue);

            if(n<4){
                qmat = (vector signed int*)s->q_intra_matrix[qscale];
                biasAddr = &(s->intra_quant_bias);
            }else{
                qmat = (vector signed int*)s->q_chroma_intra_matrix[qscale];
                biasAddr = &(s->intra_quant_bias);
            }
        } else {
            qmat = (vector signed int*)s->q_inter_matrix[qscale];
            biasAddr = &(s->inter_quant_bias);
        }

        // Load the bias vector (We add 0.5 to the bias so that we're
                                // rounding when we convert to int, instead of flooring.)
        {
            vector signed int biasInt;
            const vector float negOneFloat = (vector float)FOUROF(-1.0f);
            LOAD4(biasInt, biasAddr);
            bias = vec_ctf(biasInt, QUANT_BIAS_SHIFT);
            negBias = vec_madd(bias, negOneFloat, zero);
        }

        {
            vector float q0, q1, q2, q3, q4, q5, q6, q7;

            q0 = vec_ctf(qmat[0], QMAT_SHIFT);
            q1 = vec_ctf(qmat[2], QMAT_SHIFT);
            q2 = vec_ctf(qmat[4], QMAT_SHIFT);
            q3 = vec_ctf(qmat[6], QMAT_SHIFT);
            q4 = vec_ctf(qmat[8], QMAT_SHIFT);
            q5 = vec_ctf(qmat[10], QMAT_SHIFT);
            q6 = vec_ctf(qmat[12], QMAT_SHIFT);
            q7 = vec_ctf(qmat[14], QMAT_SHIFT);

            row0 = vec_sel(vec_madd(row0, q0, negBias), vec_madd(row0, q0, bias),
                    vec_cmpgt(row0, zero));
            row1 = vec_sel(vec_madd(row1, q1, negBias), vec_madd(row1, q1, bias),
                    vec_cmpgt(row1, zero));
            row2 = vec_sel(vec_madd(row2, q2, negBias), vec_madd(row2, q2, bias),
                    vec_cmpgt(row2, zero));
            row3 = vec_sel(vec_madd(row3, q3, negBias), vec_madd(row3, q3, bias),
                    vec_cmpgt(row3, zero));
            row4 = vec_sel(vec_madd(row4, q4, negBias), vec_madd(row4, q4, bias),
                    vec_cmpgt(row4, zero));
            row5 = vec_sel(vec_madd(row5, q5, negBias), vec_madd(row5, q5, bias),
                    vec_cmpgt(row5, zero));
            row6 = vec_sel(vec_madd(row6, q6, negBias), vec_madd(row6, q6, bias),
                    vec_cmpgt(row6, zero));
            row7 = vec_sel(vec_madd(row7, q7, negBias), vec_madd(row7, q7, bias),
                    vec_cmpgt(row7, zero));

            q0 = vec_ctf(qmat[1], QMAT_SHIFT);
            q1 = vec_ctf(qmat[3], QMAT_SHIFT);
            q2 = vec_ctf(qmat[5], QMAT_SHIFT);
            q3 = vec_ctf(qmat[7], QMAT_SHIFT);
            q4 = vec_ctf(qmat[9], QMAT_SHIFT);
            q5 = vec_ctf(qmat[11], QMAT_SHIFT);
            q6 = vec_ctf(qmat[13], QMAT_SHIFT);
            q7 = vec_ctf(qmat[15], QMAT_SHIFT);

            alt0 = vec_sel(vec_madd(alt0, q0, negBias), vec_madd(alt0, q0, bias),
                    vec_cmpgt(alt0, zero));
            alt1 = vec_sel(vec_madd(alt1, q1, negBias), vec_madd(alt1, q1, bias),
                    vec_cmpgt(alt1, zero));
            alt2 = vec_sel(vec_madd(alt2, q2, negBias), vec_madd(alt2, q2, bias),
                    vec_cmpgt(alt2, zero));
            alt3 = vec_sel(vec_madd(alt3, q3, negBias), vec_madd(alt3, q3, bias),
                    vec_cmpgt(alt3, zero));
            alt4 = vec_sel(vec_madd(alt4, q4, negBias), vec_madd(alt4, q4, bias),
                    vec_cmpgt(alt4, zero));
            alt5 = vec_sel(vec_madd(alt5, q5, negBias), vec_madd(alt5, q5, bias),
                    vec_cmpgt(alt5, zero));
            alt6 = vec_sel(vec_madd(alt6, q6, negBias), vec_madd(alt6, q6, bias),
                    vec_cmpgt(alt6, zero));
            alt7 = vec_sel(vec_madd(alt7, q7, negBias), vec_madd(alt7, q7, bias),
                    vec_cmpgt(alt7, zero));
        }


    }

    // Store the data back into the original block
    {
        vector signed short data0, data1, data2, data3, data4, data5, data6, data7;

        data0 = vec_pack(vec_cts(row0, 0), vec_cts(alt0, 0));
        data1 = vec_pack(vec_cts(row1, 0), vec_cts(alt1, 0));
        data2 = vec_pack(vec_cts(row2, 0), vec_cts(alt2, 0));
        data3 = vec_pack(vec_cts(row3, 0), vec_cts(alt3, 0));
        data4 = vec_pack(vec_cts(row4, 0), vec_cts(alt4, 0));
        data5 = vec_pack(vec_cts(row5, 0), vec_cts(alt5, 0));
        data6 = vec_pack(vec_cts(row6, 0), vec_cts(alt6, 0));
        data7 = vec_pack(vec_cts(row7, 0), vec_cts(alt7, 0));

        {
            // Clamp for overflow
            vector signed int max_q_int, min_q_int;
            vector signed short max_q, min_q;

            LOAD4(max_q_int, &(s->max_qcoeff));
            LOAD4(min_q_int, &(s->min_qcoeff));

            max_q = vec_pack(max_q_int, max_q_int);
            min_q = vec_pack(min_q_int, min_q_int);

            data0 = vec_max(vec_min(data0, max_q), min_q);
            data1 = vec_max(vec_min(data1, max_q), min_q);
            data2 = vec_max(vec_min(data2, max_q), min_q);
            data4 = vec_max(vec_min(data4, max_q), min_q);
            data5 = vec_max(vec_min(data5, max_q), min_q);
            data6 = vec_max(vec_min(data6, max_q), min_q);
            data7 = vec_max(vec_min(data7, max_q), min_q);
        }

        {
        vector bool char zero_01, zero_23, zero_45, zero_67;
        vector signed char scanIndexes_01, scanIndexes_23, scanIndexes_45, scanIndexes_67;
        vector signed char negOne = vec_splat_s8(-1);
        vector signed char* scanPtr =
                (vector signed char*)(s->intra_scantable.inverse);
        signed char lastNonZeroChar;

        // Determine the largest non-zero index.
        zero_01 = vec_pack(vec_cmpeq(data0, (vector signed short)zero),
                vec_cmpeq(data1, (vector signed short)zero));
        zero_23 = vec_pack(vec_cmpeq(data2, (vector signed short)zero),
                vec_cmpeq(data3, (vector signed short)zero));
        zero_45 = vec_pack(vec_cmpeq(data4, (vector signed short)zero),
                vec_cmpeq(data5, (vector signed short)zero));
        zero_67 = vec_pack(vec_cmpeq(data6, (vector signed short)zero),
                vec_cmpeq(data7, (vector signed short)zero));

        // 64 biggest values
        scanIndexes_01 = vec_sel(scanPtr[0], negOne, zero_01);
        scanIndexes_23 = vec_sel(scanPtr[1], negOne, zero_23);
        scanIndexes_45 = vec_sel(scanPtr[2], negOne, zero_45);
        scanIndexes_67 = vec_sel(scanPtr[3], negOne, zero_67);

        // 32 largest values
        scanIndexes_01 = vec_max(scanIndexes_01, scanIndexes_23);
        scanIndexes_45 = vec_max(scanIndexes_45, scanIndexes_67);

        // 16 largest values
        scanIndexes_01 = vec_max(scanIndexes_01, scanIndexes_45);

        // 8 largest values
        scanIndexes_01 = vec_max(vec_mergeh(scanIndexes_01, negOne),
                vec_mergel(scanIndexes_01, negOne));

        // 4 largest values
        scanIndexes_01 = vec_max(vec_mergeh(scanIndexes_01, negOne),
                vec_mergel(scanIndexes_01, negOne));

        // 2 largest values
        scanIndexes_01 = vec_max(vec_mergeh(scanIndexes_01, negOne),
                vec_mergel(scanIndexes_01, negOne));

        // largest value
        scanIndexes_01 = vec_max(vec_mergeh(scanIndexes_01, negOne),
                vec_mergel(scanIndexes_01, negOne));

        scanIndexes_01 = vec_splat(scanIndexes_01, 0);


        vec_ste(scanIndexes_01, 0, &lastNonZeroChar);

        lastNonZero = lastNonZeroChar;

        // While the data is still in vectors we check for the transpose IDCT permute
        // and handle it using the vector unit if we can.  This is the permute used
        // by the altivec idct, so it is common when using the altivec dct.

        if ((lastNonZero > 0) && (s->dsp.idct_permutation_type == FF_TRANSPOSE_IDCT_PERM)) {
            TRANSPOSE8(data0, data1, data2, data3, data4, data5, data6, data7);
        }

        vec_st(data0, 0, data);
        vec_st(data1, 16, data);
        vec_st(data2, 32, data);
        vec_st(data3, 48, data);
        vec_st(data4, 64, data);
        vec_st(data5, 80, data);
        vec_st(data6, 96, data);
        vec_st(data7, 112, data);
        }
    }

    // special handling of block[0]
    if (s->mb_intra) {
        if (!s->h263_aic) {
            if (n < 4)
                oldBaseValue /= s->y_dc_scale;
            else
                oldBaseValue /= s->c_dc_scale;
        }

        // Divide by 8, rounding the result
        data[0] = (oldBaseValue + 4) >> 3;
    }

    // We handled the transpose permutation above and we don't
    // need to permute the "no" permutation case.
    if ((lastNonZero > 0) &&
        (s->dsp.idct_permutation_type != FF_TRANSPOSE_IDCT_PERM) &&
        (s->dsp.idct_permutation_type != FF_NO_IDCT_PERM)) {
        ff_block_permute(data, s->dsp.idct_permutation,
                s->intra_scantable.scantable, lastNonZero);
    }

    return lastNonZero;
}

/* AltiVec version of dct_unquantize_h263
   this code assumes `block' is 16 bytes-aligned */
static void dct_unquantize_h263_altivec(MpegEncContext *s,
                                 DCTELEM *block, int n, int qscale)
{
    int i, level, qmul, qadd;
    int nCoeffs;

    assert(s->block_last_index[n]>=0);

    qadd = (qscale - 1) | 1;
    qmul = qscale << 1;

    if (s->mb_intra) {
        if (!s->h263_aic) {
            if (n < 4)
                block[0] = block[0] * s->y_dc_scale;
            else
                block[0] = block[0] * s->c_dc_scale;
        }else
            qadd = 0;
        i = 1;
        nCoeffs= 63; //does not always use zigzag table
    } else {
        i = 0;
        nCoeffs= s->intra_scantable.raster_end[ s->block_last_index[n] ];
    }

    {
        register const vector signed short vczero = (const vector signed short)vec_splat_s16(0);
        DECLARE_ALIGNED(16, short, qmul8) = qmul;
        DECLARE_ALIGNED(16, short, qadd8) = qadd;
        register vector signed short blockv, qmulv, qaddv, nqaddv, temp1;
        register vector bool short blockv_null, blockv_neg;
        register short backup_0 = block[0];
        register int j = 0;

        qmulv = vec_splat((vec_s16)vec_lde(0, &qmul8), 0);
        qaddv = vec_splat((vec_s16)vec_lde(0, &qadd8), 0);
        nqaddv = vec_sub(vczero, qaddv);

        // vectorize all the 16 bytes-aligned blocks
        // of 8 elements
        for(; (j + 7) <= nCoeffs ; j+=8) {
            blockv = vec_ld(j << 1, block);
            blockv_neg = vec_cmplt(blockv, vczero);
            blockv_null = vec_cmpeq(blockv, vczero);
            // choose between +qadd or -qadd as the third operand
            temp1 = vec_sel(qaddv, nqaddv, blockv_neg);
            // multiply & add (block{i,i+7} * qmul [+-] qadd)
            temp1 = vec_mladd(blockv, qmulv, temp1);
            // put 0 where block[{i,i+7} used to have 0
            blockv = vec_sel(temp1, blockv, blockv_null);
            vec_st(blockv, j << 1, block);
        }

        // if nCoeffs isn't a multiple of 8, finish the job
        // using good old scalar units.
        // (we could do it using a truncated vector,
        // but I'm not sure it's worth the hassle)
        for(; j <= nCoeffs ; j++) {
            level = block[j];
            if (level) {
                if (level < 0) {
                    level = level * qmul - qadd;
                } else {
                    level = level * qmul + qadd;
                }
                block[j] = level;
            }
        }

        if (i == 1) {
            // cheat. this avoid special-casing the first iteration
            block[0] = backup_0;
        }
    }
}


void MPV_common_init_altivec(MpegEncContext *s)
{
    if (!(av_get_cpu_flags() & AV_CPU_FLAG_ALTIVEC)) return;

    // Test to make sure that the dct required alignments are met.
    if ((((long)(s->q_intra_matrix) & 0x0f) != 0) ||
        (((long)(s->q_inter_matrix) & 0x0f) != 0)) {
        av_log(s->avctx, AV_LOG_INFO, "Internal Error: q-matrix blocks must be 16-byte aligned "
                "to use AltiVec DCT. Reverting to non-AltiVec version.\n");
        return;
    }

    if (((long)(s->intra_scantable.inverse) & 0x0f) != 0) {
        av_log(s->avctx, AV_LOG_INFO, "Internal Error: scan table blocks must be 16-byte aligned "
                "to use AltiVec DCT. Reverting to non-AltiVec version.\n");
        return;
    }


    if ((s->avctx->dct_algo == FF_DCT_AUTO) ||
            (s->avctx->dct_algo == FF_DCT_ALTIVEC)) {
        s->dct_unquantize_h263_intra = dct_unquantize_h263_altivec;
        s->dct_unquantize_h263_inter = dct_unquantize_h263_altivec;
    }
}
