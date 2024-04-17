/*
 * Armv8 Neon optimizations for libjpeg-turbo
 *
 * Copyright (C) 2009-2011, Nokia Corporation and/or its subsidiary(-ies).
 *                          All Rights Reserved.
 * Author:  Siarhei Siamashka <siarhei.siamashka@nokia.com>
 * Copyright (C) 2013-2014, Linaro Limited.  All Rights Reserved.
 * Author:  Ragesh Radhakrishnan <ragesh.r@linaro.org>
 * Copyright (C) 2014-2016, 2020, D. R. Commander.  All Rights Reserved.
 * Copyright (C) 2015-2016, 2018, Matthieu Darbois.  All Rights Reserved.
 * Copyright (C) 2016, Siarhei Siamashka.  All Rights Reserved.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#include "libavutil/aarch64/asm.S"
#include "neon.S"

// #define EIGHT_BIT_SAMPLES

/* Constants for jsimd_fdct_islow_neon() */

#define F_0_298   2446  /* FIX(0.298631336) */
#define F_0_390   3196  /* FIX(0.390180644) */
#define F_0_541   4433  /* FIX(0.541196100) */
#define F_0_765   6270  /* FIX(0.765366865) */
#define F_0_899   7373  /* FIX(0.899976223) */
#define F_1_175   9633  /* FIX(1.175875602) */
#define F_1_501  12299  /* FIX(1.501321110) */
#define F_1_847  15137  /* FIX(1.847759065) */
#define F_1_961  16069  /* FIX(1.961570560) */
#define F_2_053  16819  /* FIX(2.053119869) */
#define F_2_562  20995  /* FIX(2.562915447) */
#define F_3_072  25172  /* FIX(3.072711026) */

const jsimd_fdct_islow_neon_consts, align=4
        .short F_0_298
        .short -F_0_390
        .short F_0_541
        .short F_0_765
        .short - F_0_899
        .short F_1_175
        .short F_1_501
        .short - F_1_847
        .short - F_1_961
        .short F_2_053
        .short - F_2_562
        .short F_3_072
        .short 0          /* padding */
        .short 0
        .short 0
        .short 0
endconst

#undef F_0_298
#undef F_0_390
#undef F_0_541
#undef F_0_765
#undef F_0_899
#undef F_1_175
#undef F_1_501
#undef F_1_847
#undef F_1_961
#undef F_2_053
#undef F_2_562
#undef F_3_072

/*****************************************************************************/

/*
 * jsimd_fdct_islow_neon
 *
 * This file contains a slower but more accurate integer implementation of the
 * forward DCT (Discrete Cosine Transform). The following code is based
 * directly on the IJG''s original jfdctint.c; see the jfdctint.c for
 * more details.
 */

#define CONST_BITS  13
#ifdef EIGHT_BIT_SAMPLES
#define PASS1_BITS  2
#else
#define PASS1_BITS  1   /* lose a little precision to avoid overflow */
#endif

#define DESCALE_P1  (CONST_BITS - PASS1_BITS)
#define DESCALE_P2  (CONST_BITS + PASS1_BITS)

#define XFIX_P_0_298  v0.h[0]
#define XFIX_N_0_390  v0.h[1]
#define XFIX_P_0_541  v0.h[2]
#define XFIX_P_0_765  v0.h[3]
#define XFIX_N_0_899  v0.h[4]
#define XFIX_P_1_175  v0.h[5]
#define XFIX_P_1_501  v0.h[6]
#define XFIX_N_1_847  v0.h[7]
#define XFIX_N_1_961  v1.h[0]
#define XFIX_P_2_053  v1.h[1]
#define XFIX_N_2_562  v1.h[2]
#define XFIX_P_3_072  v1.h[3]

function ff_fdct_neon, export=1

        DATA            .req x0
        TMP             .req x9

        /* Load constants */
        movrel          TMP, jsimd_fdct_islow_neon_consts
        ld1             {v0.8h, v1.8h}, [TMP]

        /* Load all DATA into Neon registers with the following allocation:
         *       0 1 2 3 | 4 5 6 7
         *      ---------+--------
         *   0 | d16     | d17    | v16.8h
         *   1 | d18     | d19    | v17.8h
         *   2 | d20     | d21    | v18.8h
         *   3 | d22     | d23    | v19.8h
         *   4 | d24     | d25    | v20.8h
         *   5 | d26     | d27    | v21.8h
         *   6 | d28     | d29    | v22.8h
         *   7 | d30     | d31    | v23.8h
         */

        ld1             {v16.8h, v17.8h, v18.8h, v19.8h}, [DATA], 64
        ld1             {v20.8h, v21.8h, v22.8h, v23.8h}, [DATA]
        sub             DATA, DATA, #64

        /* Transpose */
        transpose_8x8H  v16, v17, v18, v19, v20, v21, v22, v23, v31, v2

        /* 1-D FDCT */
        add             v24.8h, v16.8h, v23.8h  /* tmp0 = dataptr[0] + dataptr[7]; */
        sub             v31.8h, v16.8h, v23.8h  /* tmp7 = dataptr[0] - dataptr[7]; */
        add             v25.8h, v17.8h, v22.8h  /* tmp1 = dataptr[1] + dataptr[6]; */
        sub             v30.8h, v17.8h, v22.8h  /* tmp6 = dataptr[1] - dataptr[6]; */
        add             v26.8h, v18.8h, v21.8h  /* tmp2 = dataptr[2] + dataptr[5]; */
        sub             v29.8h, v18.8h, v21.8h  /* tmp5 = dataptr[2] - dataptr[5]; */
        add             v27.8h, v19.8h, v20.8h  /* tmp3 = dataptr[3] + dataptr[4]; */
        sub             v28.8h, v19.8h, v20.8h  /* tmp4 = dataptr[3] - dataptr[4]; */

        /* Even part */
        add             v4.8h, v24.8h, v27.8h   /* tmp10 = tmp0 + tmp3; */
        sub             v5.8h, v24.8h, v27.8h   /* tmp13 = tmp0 - tmp3; */
        add             v6.8h, v25.8h, v26.8h   /* tmp11 = tmp1 + tmp2; */
        sub             v7.8h, v25.8h, v26.8h   /* tmp12 = tmp1 - tmp2; */

        add             v16.8h, v4.8h, v6.8h   /* tmp10 + tmp11 */
        sub             v20.8h, v4.8h, v6.8h   /* tmp10 - tmp11 */

        add             v18.8h, v7.8h, v5.8h   /* tmp12 + tmp13 */

        shl             v16.8h, v16.8h, #PASS1_BITS  /* dataptr[0] = (DCTELEM)LEFT_SHIFT(tmp10 + tmp11, PASS1_BITS); */
        shl             v20.8h, v20.8h, #PASS1_BITS  /* dataptr[4] = (DCTELEM)LEFT_SHIFT(tmp10 - tmp11, PASS1_BITS); */

        smull2          v24.4s, v18.8h, XFIX_P_0_541  /* z1 hi = MULTIPLY(tmp12 + tmp13, XFIX_P_0_541); */
        smull           v18.4s, v18.4h, XFIX_P_0_541  /* z1 lo = MULTIPLY(tmp12 + tmp13, XFIX_P_0_541); */
        mov             v22.16b, v18.16b
        mov             v25.16b, v24.16b

        smlal           v18.4s, v5.4h, XFIX_P_0_765   /* lo z1 + MULTIPLY(tmp13, XFIX_P_0_765) */
        smlal2          v24.4s, v5.8h, XFIX_P_0_765   /* hi z1 + MULTIPLY(tmp13, XFIX_P_0_765) */
        smlal           v22.4s, v7.4h, XFIX_N_1_847   /* lo z1 + MULTIPLY(tmp12, XFIX_N_1_847) */
        smlal2          v25.4s, v7.8h, XFIX_N_1_847   /* hi z1 + MULTIPLY(tmp12, XFIX_N_1_847) */

        rshrn           v18.4h, v18.4s, #DESCALE_P1
        rshrn           v22.4h, v22.4s, #DESCALE_P1
        rshrn2          v18.8h, v24.4s, #DESCALE_P1  /* dataptr[2] = (DCTELEM)DESCALE(z1 + MULTIPLY(tmp13, XFIX_P_0_765), CONST_BITS-PASS1_BITS); */
        rshrn2          v22.8h, v25.4s, #DESCALE_P1  /* dataptr[6] = (DCTELEM)DESCALE(z1 + MULTIPLY(tmp12, XFIX_N_1_847), CONST_BITS-PASS1_BITS); */

        /* Odd part */
        add             v2.8h, v28.8h, v31.8h        /* z1 = tmp4 + tmp7; */
        add             v3.8h, v29.8h, v30.8h        /* z2 = tmp5 + tmp6; */
        add             v6.8h, v28.8h, v30.8h        /* z3 = tmp4 + tmp6; */
        add             v7.8h, v29.8h, v31.8h        /* z4 = tmp5 + tmp7; */
        smull           v4.4s, v6.4h, XFIX_P_1_175   /* z5 lo = z3 lo * XFIX_P_1_175 */
        smull2          v5.4s, v6.8h, XFIX_P_1_175
        smlal           v4.4s, v7.4h, XFIX_P_1_175   /* z5 = MULTIPLY(z3 + z4, FIX_1_175875602); */
        smlal2          v5.4s, v7.8h, XFIX_P_1_175

        smull2          v24.4s, v28.8h, XFIX_P_0_298
        smull2          v25.4s, v29.8h, XFIX_P_2_053
        smull2          v26.4s, v30.8h, XFIX_P_3_072
        smull2          v27.4s, v31.8h, XFIX_P_1_501
        smull           v23.4s, v28.4h, XFIX_P_0_298  /* tmp4 = MULTIPLY(tmp4, FIX_0_298631336); */
        smull           v21.4s, v29.4h, XFIX_P_2_053  /* tmp5 = MULTIPLY(tmp5, FIX_2_053119869); */
        smull           v19.4s, v30.4h, XFIX_P_3_072  /* tmp6 = MULTIPLY(tmp6, FIX_3_072711026); */
        smull           v17.4s, v31.4h, XFIX_P_1_501  /* tmp7 = MULTIPLY(tmp7, FIX_1_501321110); */

        smull2          v28.4s, v2.8h, XFIX_N_0_899
        smull2          v29.4s, v3.8h, XFIX_N_2_562
        smull2          v30.4s, v6.8h, XFIX_N_1_961
        smull2          v31.4s, v7.8h, XFIX_N_0_390
        smull           v2.4s, v2.4h, XFIX_N_0_899    /* z1 = MULTIPLY(z1, -FIX_0_899976223); */
        smull           v3.4s, v3.4h, XFIX_N_2_562    /* z2 = MULTIPLY(z2, -FIX_2_562915447); */
        smull           v6.4s, v6.4h, XFIX_N_1_961    /* z3 = MULTIPLY(z3, -FIX_1_961570560); */
        smull           v7.4s, v7.4h, XFIX_N_0_390    /* z4 = MULTIPLY(z4, -FIX_0_390180644); */

        add             v6.4s, v6.4s, v4.4s    /* z3 += z5 */
        add             v30.4s, v30.4s, v5.4s
        add             v7.4s, v7.4s, v4.4s    /* z4 += z5 */
        add             v31.4s, v31.4s, v5.4s

        add             v23.4s, v23.4s, v2.4s   /* tmp4 += z1 */
        add             v24.4s, v24.4s, v28.4s
        add             v21.4s, v21.4s, v3.4s   /* tmp5 += z2 */
        add             v25.4s, v25.4s, v29.4s
        add             v19.4s, v19.4s, v6.4s   /* tmp6 += z3 */
        add             v26.4s, v26.4s, v30.4s
        add             v17.4s, v17.4s, v7.4s   /* tmp7 += z4 */
        add             v27.4s, v27.4s, v31.4s

        add             v23.4s, v23.4s, v6.4s   /* tmp4 += z3 */
        add             v24.4s, v24.4s, v30.4s
        add             v21.4s, v21.4s, v7.4s   /* tmp5 += z4 */
        add             v25.4s, v25.4s, v31.4s
        add             v19.4s, v19.4s, v3.4s   /* tmp6 += z2 */
        add             v26.4s, v26.4s, v29.4s
        add             v17.4s, v17.4s, v2.4s   /* tmp7 += z1 */
        add             v27.4s, v27.4s, v28.4s

        rshrn           v23.4h, v23.4s, #DESCALE_P1
        rshrn           v21.4h, v21.4s, #DESCALE_P1
        rshrn           v19.4h, v19.4s, #DESCALE_P1
        rshrn           v17.4h, v17.4s, #DESCALE_P1
        rshrn2          v23.8h, v24.4s, #DESCALE_P1  /* dataptr[7] = (DCTELEM)DESCALE(tmp4 + z1 + z3, CONST_BITS-PASS1_BITS); */
        rshrn2          v21.8h, v25.4s, #DESCALE_P1  /* dataptr[5] = (DCTELEM)DESCALE(tmp5 + z2 + z4, CONST_BITS-PASS1_BITS); */
        rshrn2          v19.8h, v26.4s, #DESCALE_P1  /* dataptr[3] = (DCTELEM)DESCALE(tmp6 + z2 + z3, CONST_BITS-PASS1_BITS); */
        rshrn2          v17.8h, v27.4s, #DESCALE_P1  /* dataptr[1] = (DCTELEM)DESCALE(tmp7 + z1 + z4, CONST_BITS-PASS1_BITS); */

        /* Transpose */
        transpose_8x8H  v16, v17, v18, v19, v20, v21, v22, v23, v31, v2

        /* 1-D FDCT */
        add             v24.8h, v16.8h, v23.8h  /* tmp0 = dataptr[0] + dataptr[7]; */
        sub             v31.8h, v16.8h, v23.8h  /* tmp7 = dataptr[0] - dataptr[7]; */
        add             v25.8h, v17.8h, v22.8h  /* tmp1 = dataptr[1] + dataptr[6]; */
        sub             v30.8h, v17.8h, v22.8h  /* tmp6 = dataptr[1] - dataptr[6]; */
        add             v26.8h, v18.8h, v21.8h  /* tmp2 = dataptr[2] + dataptr[5]; */
        sub             v29.8h, v18.8h, v21.8h  /* tmp5 = dataptr[2] - dataptr[5]; */
        add             v27.8h, v19.8h, v20.8h  /* tmp3 = dataptr[3] + dataptr[4]; */
        sub             v28.8h, v19.8h, v20.8h  /* tmp4 = dataptr[3] - dataptr[4]; */

        /* Even part */
        add             v4.8h, v24.8h, v27.8h   /* tmp10 = tmp0 + tmp3; */
        sub             v5.8h, v24.8h, v27.8h   /* tmp13 = tmp0 - tmp3; */
        add             v6.8h, v25.8h, v26.8h   /* tmp11 = tmp1 + tmp2; */
        sub             v7.8h, v25.8h, v26.8h   /* tmp12 = tmp1 - tmp2; */

        add             v16.8h, v4.8h, v6.8h   /* tmp10 + tmp11 */
        sub             v20.8h, v4.8h, v6.8h   /* tmp10 - tmp11 */

        add             v18.8h, v7.8h, v5.8h   /* tmp12 + tmp13 */

        srshr           v16.8h, v16.8h, #PASS1_BITS  /* dataptr[0] = (DCTELEM)DESCALE(tmp10 + tmp11, PASS1_BITS); */
        srshr           v20.8h, v20.8h, #PASS1_BITS  /* dataptr[4] = (DCTELEM)DESCALE(tmp10 - tmp11, PASS1_BITS); */

        smull2          v24.4s, v18.8h, XFIX_P_0_541  /* z1 hi = MULTIPLY(tmp12 + tmp13, XFIX_P_0_541); */
        smull           v18.4s, v18.4h, XFIX_P_0_541  /* z1 lo = MULTIPLY(tmp12 + tmp13, XFIX_P_0_541); */
        mov             v22.16b, v18.16b
        mov             v25.16b, v24.16b

        smlal           v18.4s, v5.4h, XFIX_P_0_765   /* lo z1 + MULTIPLY(tmp13, XFIX_P_0_765) */
        smlal2          v24.4s, v5.8h, XFIX_P_0_765   /* hi z1 + MULTIPLY(tmp13, XFIX_P_0_765) */
        smlal           v22.4s, v7.4h, XFIX_N_1_847   /* lo z1 + MULTIPLY(tmp12, XFIX_N_1_847) */
        smlal2          v25.4s, v7.8h, XFIX_N_1_847   /* hi z1 + MULTIPLY(tmp12, XFIX_N_1_847) */

        rshrn           v18.4h, v18.4s, #DESCALE_P2
        rshrn           v22.4h, v22.4s, #DESCALE_P2
        rshrn2          v18.8h, v24.4s, #DESCALE_P2  /* dataptr[2] = (DCTELEM)DESCALE(z1 + MULTIPLY(tmp13, XFIX_P_0_765), CONST_BITS+PASS1_BITS); */
        rshrn2          v22.8h, v25.4s, #DESCALE_P2  /* dataptr[6] = (DCTELEM)DESCALE(z1 + MULTIPLY(tmp12, XFIX_N_1_847), CONST_BITS+PASS1_BITS); */

        /* Odd part */
        add             v2.8h, v28.8h, v31.8h   /* z1 = tmp4 + tmp7; */
        add             v3.8h, v29.8h, v30.8h   /* z2 = tmp5 + tmp6; */
        add             v6.8h, v28.8h, v30.8h   /* z3 = tmp4 + tmp6; */
        add             v7.8h, v29.8h, v31.8h   /* z4 = tmp5 + tmp7; */

        smull           v4.4s, v6.4h, XFIX_P_1_175   /* z5 lo = z3 lo * XFIX_P_1_175 */
        smull2          v5.4s, v6.8h, XFIX_P_1_175
        smlal           v4.4s, v7.4h, XFIX_P_1_175   /* z5 = MULTIPLY(z3 + z4, FIX_1_175875602); */
        smlal2          v5.4s, v7.8h, XFIX_P_1_175

        smull2          v24.4s, v28.8h, XFIX_P_0_298
        smull2          v25.4s, v29.8h, XFIX_P_2_053
        smull2          v26.4s, v30.8h, XFIX_P_3_072
        smull2          v27.4s, v31.8h, XFIX_P_1_501
        smull           v23.4s, v28.4h, XFIX_P_0_298  /* tmp4 = MULTIPLY(tmp4, FIX_0_298631336); */
        smull           v21.4s, v29.4h, XFIX_P_2_053  /* tmp5 = MULTIPLY(tmp5, FIX_2_053119869); */
        smull           v19.4s, v30.4h, XFIX_P_3_072  /* tmp6 = MULTIPLY(tmp6, FIX_3_072711026); */
        smull           v17.4s, v31.4h, XFIX_P_1_501  /* tmp7 = MULTIPLY(tmp7, FIX_1_501321110); */

        smull2          v28.4s, v2.8h, XFIX_N_0_899
        smull2          v29.4s, v3.8h, XFIX_N_2_562
        smull2          v30.4s, v6.8h, XFIX_N_1_961
        smull2          v31.4s, v7.8h, XFIX_N_0_390
        smull           v2.4s, v2.4h, XFIX_N_0_899    /* z1 = MULTIPLY(z1, -FIX_0_899976223); */
        smull           v3.4s, v3.4h, XFIX_N_2_562    /* z2 = MULTIPLY(z2, -FIX_2_562915447); */
        smull           v6.4s, v6.4h, XFIX_N_1_961    /* z3 = MULTIPLY(z3, -FIX_1_961570560); */
        smull           v7.4s, v7.4h, XFIX_N_0_390    /* z4 = MULTIPLY(z4, -FIX_0_390180644); */

        add             v6.4s, v6.4s, v4.4s    /* z3 += z5 */
        add             v30.4s, v30.4s, v5.4s
        add             v7.4s, v7.4s, v4.4s    /* z4 += z5 */
        add             v31.4s, v31.4s, v5.4s

        add             v23.4s, v23.4s, v2.4s   /* tmp4 += z1 */
        add             v24.4s, v24.4s, v28.4s
        add             v21.4s, v21.4s, v3.4s   /* tmp5 += z2 */
        add             v25.4s, v25.4s, v29.4s
        add             v19.4s, v19.4s, v6.4s   /* tmp6 += z3 */
        add             v26.4s, v26.4s, v30.4s
        add             v17.4s, v17.4s, v7.4s   /* tmp7 += z4 */
        add             v27.4s, v27.4s, v31.4s

        add             v23.4s, v23.4s, v6.4s   /* tmp4 += z3 */
        add             v24.4s, v24.4s, v30.4s
        add             v21.4s, v21.4s, v7.4s   /* tmp5 += z4 */
        add             v25.4s, v25.4s, v31.4s
        add             v19.4s, v19.4s, v3.4s   /* tmp6 += z2 */
        add             v26.4s, v26.4s, v29.4s
        add             v17.4s, v17.4s, v2.4s   /* tmp7 += z1 */
        add             v27.4s, v27.4s, v28.4s

        rshrn           v23.4h, v23.4s, #DESCALE_P2
        rshrn           v21.4h, v21.4s, #DESCALE_P2
        rshrn           v19.4h, v19.4s, #DESCALE_P2
        rshrn           v17.4h, v17.4s, #DESCALE_P2
        rshrn2          v23.8h, v24.4s, #DESCALE_P2  /* dataptr[7] = (DCTELEM)DESCALE(tmp4 + z1 + z3, CONST_BITS+PASS1_BITS); */
        rshrn2          v21.8h, v25.4s, #DESCALE_P2  /* dataptr[5] = (DCTELEM)DESCALE(tmp5 + z2 + z4, CONST_BITS+PASS1_BITS); */
        rshrn2          v19.8h, v26.4s, #DESCALE_P2  /* dataptr[3] = (DCTELEM)DESCALE(tmp6 + z2 + z3, CONST_BITS+PASS1_BITS); */
        rshrn2          v17.8h, v27.4s, #DESCALE_P2  /* dataptr[1] = (DCTELEM)DESCALE(tmp7 + z1 + z4, CONST_BITS+PASS1_BITS); */

        /* Store results */
        st1             {v16.8h, v17.8h, v18.8h, v19.8h}, [DATA], 64
        st1             {v20.8h, v21.8h, v22.8h, v23.8h}, [DATA]

        ret

        .unreq          DATA
        .unreq          TMP
endfunc

#undef XFIX_P_0_298
#undef XFIX_N_0_390
#undef XFIX_P_0_541
#undef XFIX_P_0_765
#undef XFIX_N_0_899
#undef XFIX_P_1_175
#undef XFIX_P_1_501
#undef XFIX_N_1_847
#undef XFIX_N_1_961
#undef XFIX_P_2_053
#undef XFIX_N_2_562
#undef XFIX_P_3_072
