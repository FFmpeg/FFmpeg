/*
 * Copyright (c) 2026 Jia Yuan <yuan.jia@sanechips.com.cn>
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

/*
 * This file is derived from libavcodec/aarch64/fdctdsp_neon.S,
 * which is itself based on libjpeg-turbo. The original license
 * notice is preserved below.
 *
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

#include "libavutil/riscv/asm.S"
#include "rvv.S"

/* Constants for FDCT */
#define CONST_BITS  13
#ifndef EIGHT_BIT_SAMPLES
#  define PASS1_BITS  1
#else
#  define PASS1_BITS  2
#endif
#define DESCALE_P1  (CONST_BITS - PASS1_BITS)   /* 12 or 11 */
#define DESCALE_P2  (CONST_BITS + PASS1_BITS)   /* 14 or 15 */

/* FIX(x) = (int)( (x) * (1 << CONST_BITS) + 0.5 ) */
#define F_0_298   2446
#define F_0_390   3196
#define F_0_541   4433
#define F_0_765   6270
#define F_0_899   7373
#define F_1_175   9633
#define F_1_501  12299
#define F_1_847  15137
#define F_1_961  16069
#define F_2_053  16819
#define F_2_562  20995
#define F_3_072  25172

/* ------------------------------------------------------- */
/* One-dimensional FDCT (8 elements in, 8 elements out)    */
/*   pass: 0 = first pass (shift DESCALE_P1, shl out0/out4)*/
/*         1 = second pass (shift DESCALE_P2, srshr out0/4)*/
/* ------------------------------------------------------- */
.macro fdct_1d pass, r0, r1, r2, r3, r4, r5, r6, r7
    /* Input  : r0..r7 (int16, m1)                         */
    /* During 32-bit arithmetic we switch to e32, m2       */
    /* Output : v16..v23 (int16, m1)                       */

    /* ---- Butterfly sums/differences (16-bit) ---- */
    vadd.vv   v24, \r0, \r7    /* tmp0 = S0 + S7 */
    vsub.vv   v31, \r0, \r7    /* tmp7 = S0 - S7 */
    vadd.vv   v25, \r1, \r6    /* tmp1 = S1 + S6 */
    vsub.vv   v30, \r1, \r6    /* tmp6 = S1 - S6 */
    vadd.vv   v26, \r2, \r5    /* tmp2 = S2 + S5 */
    vsub.vv   v29, \r2, \r5    /* tmp5 = S2 - S5 */
    vadd.vv   v27, \r3, \r4    /* tmp3 = S3 + S4 */
    vsub.vv   v28, \r3, \r4    /* tmp4 = S3 - S4 */

    /* ---- Even part ---- */
    vadd.vv   v4,  v24, v27    /* tmp10 = tmp0 + tmp3 */
    vsub.vv   v5,  v24, v27    /* tmp13 = tmp0 - tmp3 */
    vadd.vv   v6,  v25, v26    /* tmp11 = tmp1 + tmp2 */
    vsub.vv   v7,  v25, v26    /* tmp12 = tmp1 - tmp2 */

    vadd.vv   v16, v4, v6      /* out0 = tmp10 + tmp11 */
    vsub.vv   v20, v4, v6      /* out4 = tmp10 - tmp11 */
    vadd.vv   v12, v7, v5      /* (tmp12 + tmp13) for z1 */

    /* For the first pass: left shift out0 and out4 */
.if \pass == 0
    vsll.vi   v16, v16, PASS1_BITS
    vsll.vi   v20, v20, PASS1_BITS
.else
    /* Second pass: rounding right shift out0 and out4 */
    vsetivli  zero, 8, e32, m2, ta, ma
    vsext.vf2 v8, v16
    vsext.vf2 v10, v20
    vsetivli  zero, 8, e16, m1, ta, ma
    vnclip.wi v16, v8, PASS1_BITS
    vnclip.wi v20, v10, PASS1_BITS
.endif

    li        t2, F_0_541
    li        t3, F_0_765
    li        t4, -(F_1_847)

    /* z1 = (tmp12+tmp13) * F_0_541 */
    vwmul.vx  v18, v12, t2
    vsetivli  zero, 8, e32, m2, ta, ma
    vmv.v.v   v22, v18         /* copy for out6 */

    /* out2 = z1 + tmp13 * F_0_765 */
    vsetivli  zero, 8, e16, m1, ta, ma
    vwmacc.vx v18, t3, v5

    /* out6 = z1 + tmp12 * (-F_1_847) */
    vwmacc.vx v22, t4, v7

    /* Narrow & shift (DESCALE_P1 or DESCALE_P2) */
    vsetivli  zero, 8, e16, m1, ta, ma
.if \pass == 0
    vnclip.wi v18, v18, DESCALE_P1
    vnclip.wi v22, v22, DESCALE_P1
.else
    vnclip.wi v18, v18, DESCALE_P2
    vnclip.wi v22, v22, DESCALE_P2
.endif

    /* ---- Odd part  ---- */
    vadd.vv   v2, v28, v31      /* z1 */
    vadd.vv   v3, v29, v30      /* z2 */
    vadd.vv   v6, v28, v30      /* z3 */
    vadd.vv   v7, v29, v31      /* z4 */

    /* z5 = (z3 + z4) * F_1_175 */
    li        t2, F_1_175
    vwmul.vx  v4, v6, t2   /* z5 = z3 * F_1_175 */
    vwmacc.vx v4, t2, v7   /* z5 += z4 * F_1_175 */

    /* tmp4 = S4 * F_0_298, tmp5 = S5 * F_2_053,
       tmp6 = S6 * F_3_072, tmp7 = S7 * F_1_501 */
    li        t3, F_0_298
    li        t4, F_2_053
    li        t5, F_3_072
    li        t6, F_1_501
    vwmul.vx  v8, v28, t3
    vwmul.vx  v10, v29, t4
    vwmul.vx  v12, v30, t5
    vwmul.vx  v14, v31, t6

    /* z1..z4 with negative coefficients */
    li        t1, -(F_0_899)
    li        t2, -(F_2_562)
    li        t3, -(F_1_961)
    li        t4, -(F_0_390)
    vwmul.vx  v24, v2, t1
    vwmul.vx  v26, v3, t2
    vwmul.vx  v28, v6, t3
    vwmul.vx  v30, v7, t4

    vsetivli  zero, 8, e32, m2, ta, ma
    /* z3 += z5, z4 += z5 */
    vadd.vv   v28, v28, v4
    vadd.vv   v30, v30, v4

    /* tmp4 += z1 ; tmp5 += z2 ; tmp6 += z3 ; tmp7 += z4 */
    vadd.vv    v8,  v8,  v24          /* tmp4 += z1 */
    vadd.vv    v10, v10, v26          /* tmp5 += z2 */
    vadd.vv    v12, v12, v28          /* tmp6 += z3 */
    vadd.vv    v14, v14, v30          /* tmp7 += z4 */

    /* tmp4 += z3 ; tmp5 += z4 ; tmp6 += z2 ; tmp7 += z1 */
    vadd.vv    v8,  v8,  v28          /* tmp4 += z3 */
    vadd.vv    v10, v10, v30          /* tmp5 += z4 */
    vadd.vv    v12, v12, v26          /* tmp6 += z2 */
    vadd.vv    v14, v14, v24          /* tmp7 += z1 */

    /* ---- Restore 16-bit mode ---- */
    vsetivli   zero, 8, e16, m1, ta, ma
.if \pass == 0
    vnclip.wi v23, v8, DESCALE_P1
    vnclip.wi v21, v10, DESCALE_P1
    vnclip.wi v19, v12, DESCALE_P1
    vnclip.wi v17, v14, DESCALE_P1
.else
    vnclip.wi v23, v8, DESCALE_P2
    vnclip.wi v21, v10, DESCALE_P2
    vnclip.wi v19, v12, DESCALE_P2
    vnclip.wi v17, v14, DESCALE_P2
.endif

.endm

/* ------------------------------------------------------- */
/* 8x8 FDCT entry point                                    */
/*   void ff_fdct_rvv(int16_t *data)                       */
/* ------------------------------------------------------- */
func ff_fdct_rvv, zve64x
    lpad 0

    li        t1, 0x55
    vsetivli  zero, 8, e16, m1, ta, ma
    vmv.v.x   v0, t1
    csrwi     vxrm, 0

    /* Load 8x8 block (rows) */
    mv        t0, a0
    vle16.v   v16, (t0)
    addi      t0, t0, 16
    vle16.v   v17, (t0)
    addi      t0, t0, 16
    vle16.v   v18, (t0)
    addi      t0, t0, 16
    vle16.v   v19, (t0)
    addi      t0, t0, 16
    vle16.v   v20, (t0)
    addi      t0, t0, 16
    vle16.v   v21, (t0)
    addi      t0, t0, 16
    vle16.v   v22, (t0)
    addi      t0, t0, 16
    vle16.v   v23, (t0)

    /* First transpose: rows -> columns */
    TRANSPOSE_8x8 16, v8, v9, v10, v11, v12, v13, v14, v15, v16, v17, v18, v19, v20, v21, v22, v23

    /* First 1-D FDCT (columns) */
    vsetivli   zero, 8, e16, m1, ta, ma
    fdct_1d 0, v8, v9, v10, v11, v12, v13, v14, v15

    /* Transpose again: columns -> rows */
    TRANSPOSE_8x8 16, v8, v9, v10, v11, v12, v13, v14, v15, v16, v17, v18, v19, v20, v21, v22, v23

    /* Second 1-D FDCT (rows) */
    vsetivli   zero, 8, e16, m1, ta, ma
    fdct_1d 1, v8, v9, v10, v11, v12, v13, v14, v15

    /* Store the final 8x8 block back to memory */
    mv        t0, a0
    vse16.v   v16, (t0)
    addi      t0, t0, 16
    vse16.v   v17, (t0)
    addi      t0, t0, 16
    vse16.v   v18, (t0)
    addi      t0, t0, 16
    vse16.v   v19, (t0)
    addi      t0, t0, 16
    vse16.v   v20, (t0)
    addi      t0, t0, 16
    vse16.v   v21, (t0)
    addi      t0, t0, 16
    vse16.v   v22, (t0)
    addi      t0, t0, 16
    vse16.v   v23, (t0)

    ret
endfunc
