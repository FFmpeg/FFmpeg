/*
 * Copyright (c) 2024 Zhao Zhili
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

#include "libavcodec/wasm/hevc/idct.h"

#include <wasm_simd128.h>

#include "libavutil/mem_internal.h"

static const int8_t transform[] = {
    64, 83, 64, 36, 89, 75, 50, 18,
    90, 87, 80, 70, 57, 43, 25, 9,
    90, 90, 88, 85, 82, 78, 73, 67,
    61, 54, 46, 38, 31, 22, 13, 4,
};

static inline void transpose_4x8h(v128_t *src)
{
    v128_t t0 = wasm_i16x8_shuffle(src[0], src[1], 0, 8, 2, 10, 4, 12, 6, 14);
    v128_t t1 = wasm_i16x8_shuffle(src[0], src[1], 1, 9, 3, 11, 5, 13, 7, 15);
    v128_t t2 = wasm_i16x8_shuffle(src[2], src[3], 0, 8, 2, 10, 4, 12, 6, 14);
    v128_t t3 = wasm_i16x8_shuffle(src[2], src[3], 1, 9, 3, 11, 5, 13, 7, 15);

    src[0] = wasm_i32x4_shuffle(t0, t2, 0, 4, 2, 6);
    src[2] = wasm_i32x4_shuffle(t0, t2, 1, 5, 3, 7);
    src[1] = wasm_i32x4_shuffle(t1, t3, 0, 4, 2, 6);
    src[3] = wasm_i32x4_shuffle(t1, t3, 1, 5, 3, 7);
}

static inline void transpose_8x8h(v128_t *src)
{
    transpose_4x8h(src);
    transpose_4x8h(&src[4]);
}

static inline void tr_4x4(v128_t *src, v128_t *trans, int shift)
{
    v128_t tmp[4];
    v128_t add = wasm_i32x4_splat(1 << (shift - 1));
    v128_t e0 = wasm_i32x4_extmul_low_i16x8(src[0], trans[0]);
    v128_t e1 = wasm_i32x4_extmul_low_i16x8(src[0], trans[0]);
    v128_t o0 = wasm_i32x4_extmul_low_i16x8(src[1], trans[1]);
    v128_t o1 = wasm_i32x4_extmul_low_i16x8(src[1], trans[3]);

    tmp[0] = wasm_i32x4_extmul_low_i16x8(src[2], trans[0]);
    tmp[1] = wasm_i32x4_extmul_low_i16x8(src[2], trans[0]);
    tmp[2] = wasm_i32x4_extmul_low_i16x8(src[3], trans[3]);
    tmp[3] = wasm_i32x4_extmul_low_i16x8(src[3], trans[1]);
    e0 = wasm_i32x4_add(e0, tmp[0]);
    e1 = wasm_i32x4_sub(e1, tmp[1]);
    o0 = wasm_i32x4_add(o0, tmp[2]);
    o1 = wasm_i32x4_sub(o1, tmp[3]);

    tmp[0] = wasm_i32x4_add(e0, o0);
    tmp[1] = wasm_i32x4_sub(e0, o0);
    tmp[2] = wasm_i32x4_add(e1, o1);
    tmp[3] = wasm_i32x4_sub(e1, o1);

    tmp[0] = wasm_i32x4_add(tmp[0], add);
    tmp[1] = wasm_i32x4_add(tmp[1], add);
    tmp[2] = wasm_i32x4_add(tmp[2], add);
    tmp[3] = wasm_i32x4_add(tmp[3], add);
    tmp[0] = wasm_i32x4_shr(tmp[0], shift);
    tmp[1] = wasm_i32x4_shr(tmp[1], shift);
    tmp[2] = wasm_i32x4_shr(tmp[2], shift);
    tmp[3] = wasm_i32x4_shr(tmp[3], shift);

    src[0] = wasm_i16x8_narrow_i32x4(tmp[0], tmp[0]);
    src[3] = wasm_i16x8_narrow_i32x4(tmp[1], tmp[1]);
    src[1] = wasm_i16x8_narrow_i32x4(tmp[2], tmp[2]);
    src[2] = wasm_i16x8_narrow_i32x4(tmp[3], tmp[3]);
}

static void idct_4x4(int16_t *coeffs, int bit_depth)
{
    v128_t src[4];
    v128_t trans[4];

    src[0] = wasm_v128_load64_zero(&coeffs[0]);
    src[1] = wasm_v128_load64_zero(&coeffs[4]);
    src[2] = wasm_v128_load64_zero(&coeffs[8]);
    src[3] = wasm_v128_load64_zero(&coeffs[12]);

    trans[0] = wasm_i16x8_const_splat(transform[0]);
    trans[1] = wasm_i16x8_const_splat(transform[1]);
    trans[2] = wasm_i16x8_const_splat(transform[2]);
    trans[3] = wasm_i16x8_const_splat(transform[3]);

    tr_4x4(src, trans, 7);
    transpose_4x8h(src);

    tr_4x4(src, trans, 20 - bit_depth);
    transpose_4x8h(src);

    src[0] = wasm_i64x2_shuffle(src[0], src[1], 0, 2);
    src[2] = wasm_i64x2_shuffle(src[2], src[3], 0, 2);
    wasm_v128_store(&coeffs[0], src[0]);
    wasm_v128_store(&coeffs[8], src[2]);
}

void ff_hevc_idct_4x4_8_simd128(int16_t *coeffs, int col_limit)
{
    idct_4x4(coeffs, 8);
}

void ff_hevc_idct_4x4_10_simd128(int16_t *coeffs, int col_limit)
{
    idct_4x4(coeffs, 10);
}

static inline void shift_narrow_low(v128_t src, v128_t *dst, v128_t add, int shift)
{
    src = wasm_i32x4_add(src, add);
    src = wasm_i32x4_shr(src, shift);
    *dst = wasm_i64x2_shuffle(wasm_i16x8_narrow_i32x4(src, src), *dst, 0, 3);
}

static inline void shift_narrow_high(v128_t src, v128_t *dst, v128_t add, int shift)
{
    src = wasm_i32x4_add(src, add);
    src = wasm_i32x4_shr(src, shift);
    *dst = wasm_i64x2_shuffle(wasm_i16x8_narrow_i32x4(src, src), *dst, 2, 0);
}

#define tr_4x4_8(in0, in1, in2, in3, dst0, dst1, dst2, dst3, trans, half0, half1)   \
    do {                                                                            \
        v128_t e0, e1, o0, o1;                                                      \
        v128_t tmp[4];                                                              \
                                                                                    \
        e0 = wasm_i32x4_extmul_ ## half0 ## _i16x8(in0, trans[0]);                  \
        e1 = e0;                                                                    \
        o0 = wasm_i32x4_extmul_ ## half0 ## _i16x8(in1, trans[1]);                  \
        o1 = wasm_i32x4_extmul_ ## half0 ## _i16x8(in1, trans[3]);                  \
                                                                                    \
        tmp[0] = wasm_i32x4_extmul_ ## half1 ## _i16x8(in2, trans[0]);              \
        tmp[1] = wasm_i32x4_extmul_ ## half1 ## _i16x8(in2, trans[0]);              \
        tmp[2] = wasm_i32x4_extmul_ ## half1 ## _i16x8(in3, trans[3]);              \
        tmp[3] = wasm_i32x4_extmul_ ## half1 ## _i16x8(in3, trans[1]);              \
        e0 = wasm_i32x4_add(e0, tmp[0]);                                            \
        e1 = wasm_i32x4_sub(e1, tmp[1]);                                            \
        o0 = wasm_i32x4_add(o0, tmp[2]);                                            \
        o1 = wasm_i32x4_sub(o1, tmp[3]);                                            \
        dst0 = wasm_i32x4_add(e0, o0);                                              \
        dst1 = wasm_i32x4_add(e1, o1);                                              \
        dst2 = wasm_i32x4_sub(e1, o1);                                              \
        dst3 = wasm_i32x4_sub(e0, o0);                                              \
    } while (0)

#define tr_8x4(src0, src1, half0, half1, trans, shift)                                          \
    do {                                                                                        \
        v128_t v24, v25, v26, v27, v28, v29, v30, v31;                                          \
        v128_t add = wasm_i32x4_splat(1 << (shift - 1));                                        \
                                                                                                \
        tr_4x4_8(src0[0], src0[2], src1[0], src1[2], v24, v25, v26, v27, trans, half0, half1);  \
                                                                                                \
        v30 = wasm_i32x4_extmul_ ## half0 ## _i16x8(src0[1], trans[6]);                         \
        v28 = wasm_i32x4_extmul_ ## half0 ## _i16x8(src0[1], trans[4]);                         \
        v29 = wasm_i32x4_extmul_ ## half0 ## _i16x8(src0[1], trans[5]);                         \
        v30 = wasm_i32x4_sub(v30, wasm_i32x4_extmul_ ## half0 ## _i16x8(src0[3], trans[4]));    \
        v28 = wasm_i32x4_add(v28, wasm_i32x4_extmul_ ## half0 ## _i16x8(src0[3], trans[5]));    \
        v29 = wasm_i32x4_sub(v29, wasm_i32x4_extmul_ ## half0 ## _i16x8(src0[3], trans[7]));    \
                                                                                                \
        v30 = wasm_i32x4_add(v30, wasm_i32x4_extmul_ ## half1 ## _i16x8(src1[1], trans[7]));    \
        v28 = wasm_i32x4_add(v28, wasm_i32x4_extmul_ ## half1 ## _i16x8(src1[1], trans[6]));    \
        v29 = wasm_i32x4_sub(v29, wasm_i32x4_extmul_ ## half1 ## _i16x8(src1[1], trans[4]));    \
                                                                                                \
        v30 = wasm_i32x4_add(v30, wasm_i32x4_extmul_ ## half1 ## _i16x8(src1[3], trans[5]));    \
        v28 = wasm_i32x4_add(v28, wasm_i32x4_extmul_ ## half1 ## _i16x8(src1[3], trans[7]));    \
        v29 = wasm_i32x4_sub(v29, wasm_i32x4_extmul_ ## half1 ## _i16x8(src1[3], trans[6]));    \
                                                                                                \
        v31 = wasm_i32x4_add(v26, v30);                                                         \
        v26 = wasm_i32x4_sub(v26, v30);                                                         \
        shift_narrow_ ## half0 (v31, &src0[2], add, shift);                                     \
        v31 = wasm_i32x4_extmul_ ## half0 ## _i16x8(src0[1], trans[7]);                         \
        v31 = wasm_i32x4_sub(v31, wasm_i32x4_extmul_ ## half0 ## _i16x8(src0[3], trans[6]));    \
        v31 = wasm_i32x4_add(v31, wasm_i32x4_extmul_ ## half1 ## _i16x8(src1[1], trans[5]));    \
        v31 = wasm_i32x4_sub(v31, wasm_i32x4_extmul_ ## half1 ## _i16x8(src1[3], trans[4]));    \
        shift_narrow_ ## half1 (v26, &src1[1], add, shift);                                     \
        v26 = wasm_i32x4_add(v24, v28);                                                         \
        v24 = wasm_i32x4_sub(v24, v28);                                                         \
        v28 = wasm_i32x4_add(v25, v29);                                                         \
        v25 = wasm_i32x4_sub(v25, v29);                                                         \
        v30 = wasm_i32x4_add(v27, v31);                                                         \
        v27 = wasm_i32x4_sub(v27, v31);                                                         \
        shift_narrow_ ## half0 (v26, &src0[0], add, shift);                                     \
        shift_narrow_ ## half1 (v24, &src1[3], add, shift);                                     \
        shift_narrow_ ## half0 (v28, &src0[1], add, shift);                                     \
        shift_narrow_ ## half1 (v25, &src1[2], add, shift);                                     \
        shift_narrow_ ## half0 (v30, &src0[3], add, shift);                                     \
        shift_narrow_ ## half1 (v27, &src1[0], add, shift);                                     \
    } while (0)

static void idct_8x8(int16_t *coeffs, int bit_depth)
{
    v128_t src[8];
    v128_t trans[8];
    v128_t *src1;
    int shift1 = 7;
    int shift2 = 20 - bit_depth;

    src[0] = wasm_v128_load(coeffs + 0 * 8);
    src[1] = wasm_v128_load(coeffs + 1 * 8);
    src[2] = wasm_v128_load(coeffs + 2 * 8);
    src[3] = wasm_v128_load(coeffs + 3 * 8);
    src[4] = wasm_v128_load(coeffs + 4 * 8);
    src[5] = wasm_v128_load(coeffs + 5 * 8);
    src[6] = wasm_v128_load(coeffs + 6 * 8);
    src[7] = wasm_v128_load(coeffs + 7 * 8);

    trans[0] = wasm_i16x8_const_splat(transform[0]);
    trans[1] = wasm_i16x8_const_splat(transform[1]);
    trans[2] = wasm_i16x8_const_splat(transform[2]);
    trans[3] = wasm_i16x8_const_splat(transform[3]);
    trans[4] = wasm_i16x8_const_splat(transform[4]);
    trans[5] = wasm_i16x8_const_splat(transform[5]);
    trans[6] = wasm_i16x8_const_splat(transform[6]);
    trans[7] = wasm_i16x8_const_splat(transform[7]);

    src1 = &src[4];
    tr_8x4(src, src1, low, low, trans, shift1);
    tr_8x4(src, src1, high, high, trans, shift1);
    transpose_8x8h(src);
    tr_8x4(src, src, low, high, trans, shift2);
    tr_8x4(src1, src1, low, high, trans, shift2);
    transpose_8x8h(src);

    wasm_v128_store(&coeffs[0 * 8], src[0]);
    wasm_v128_store(&coeffs[1 * 8], src[1]);
    wasm_v128_store(&coeffs[2 * 8], src[2]);
    wasm_v128_store(&coeffs[3 * 8], src[3]);
    wasm_v128_store(&coeffs[4 * 8], src[4]);
    wasm_v128_store(&coeffs[5 * 8], src[5]);
    wasm_v128_store(&coeffs[6 * 8], src[6]);
    wasm_v128_store(&coeffs[7 * 8], src[7]);
}

void ff_hevc_idct_8x8_8_simd128(int16_t *coeffs, int col_limit)
{
    idct_8x8(coeffs, 8);
}

void ff_hevc_idct_8x8_10_simd128(int16_t *coeffs, int col_limit)
{
    idct_8x8(coeffs, 10);
}

#define load16(x1, x3, x2, in0, in1, in2, in3)  \
    in0 = wasm_v128_load64_zero(x1);            \
    in0 = wasm_v128_load64_lane(x3, in0, 1);    \
    x1 += x2;                                   \
    x3 += x2;                                   \
    in1 = wasm_v128_load64_zero(x1);            \
    in1 = wasm_v128_load64_lane(x3, in1, 1);    \
    x1 += x2;                                   \
    x3 += x2;                                   \
    in2 = wasm_v128_load64_zero(x1);            \
    in2 = wasm_v128_load64_lane(x3, in2, 1);    \
    x1 += x2;                                   \
    x3 += x2;                                   \
    in3 = wasm_v128_load64_zero(x1);            \
    in3 = wasm_v128_load64_lane(x3, in3, 1);    \
    x1 += x2;                                   \
    x3 += x2;                                   \

#define bufferfly(e, o, p, m)   \
    p = wasm_i32x4_add(e, o);   \
    m = wasm_i32x4_sub(e, o);   \

static void tr16_8x4(v128_t in0, v128_t in1, v128_t in2, v128_t in3,
                     const v128_t *trans, char *sp, int offset)
{
    v128_t v16, v17, v18, v19, v20, v21, v22, v23, v24, v25, v26, v27, v28, v29, v30, v31;

    tr_4x4_8(in0, in1, in2, in3, v24, v25, v26, v27, trans, low, low);

    v28 = wasm_i32x4_extmul_high_i16x8(in0, trans[4]);
    v29 = wasm_i32x4_extmul_high_i16x8(in0, trans[5]);
    v30 = wasm_i32x4_extmul_high_i16x8(in0, trans[6]);
    v31 = wasm_i32x4_extmul_high_i16x8(in0, trans[7]);
    v28 = wasm_i32x4_add(v28, wasm_i32x4_extmul_high_i16x8(in1, trans[5]));
    v29 = wasm_i32x4_sub(v29, wasm_i32x4_extmul_high_i16x8(in1, trans[7]));
    v30 = wasm_i32x4_sub(v30, wasm_i32x4_extmul_high_i16x8(in1, trans[4]));
    v31 = wasm_i32x4_sub(v31, wasm_i32x4_extmul_high_i16x8(in1, trans[6]));

    v28 = wasm_i32x4_add(v28, wasm_i32x4_extmul_high_i16x8(in2, trans[6]));
    v29 = wasm_i32x4_sub(v29, wasm_i32x4_extmul_high_i16x8(in2, trans[4]));
    v30 = wasm_i32x4_add(v30, wasm_i32x4_extmul_high_i16x8(in2, trans[7]));
    v31 = wasm_i32x4_add(v31, wasm_i32x4_extmul_high_i16x8(in2, trans[5]));

    v28 = wasm_i32x4_add(v28, wasm_i32x4_extmul_high_i16x8(in3, trans[7]));
    v29 = wasm_i32x4_sub(v29, wasm_i32x4_extmul_high_i16x8(in3, trans[6]));
    v30 = wasm_i32x4_add(v30, wasm_i32x4_extmul_high_i16x8(in3, trans[5]));
    v31 = wasm_i32x4_sub(v31, wasm_i32x4_extmul_high_i16x8(in3, trans[4]));

    bufferfly(v24, v28, v16, v23);
    bufferfly(v25, v29, v17, v22);
    bufferfly(v26, v30, v18, v21);
    bufferfly(v27, v31, v19, v20);

    sp += offset;
    wasm_v128_store(sp, v16); sp += 16;
    wasm_v128_store(sp, v17); sp += 16;
    wasm_v128_store(sp, v18); sp += 16;
    wasm_v128_store(sp, v19); sp += 16;
    wasm_v128_store(sp, v20); sp += 16;
    wasm_v128_store(sp, v21); sp += 16;
    wasm_v128_store(sp, v22); sp += 16;
    wasm_v128_store(sp, v23);
}

static void scale(v128_t *out0, v128_t *out1, v128_t *out2, v128_t *out3,
        v128_t in0, v128_t in1, v128_t in2, v128_t in3,
        v128_t in4, v128_t in5, v128_t in6, v128_t in7,
        int shift)
{
    v128_t add = wasm_i32x4_splat(1 << (shift - 1));

    in0 = wasm_i32x4_add(in0, add);
    in1 = wasm_i32x4_add(in1, add);
    in2 = wasm_i32x4_add(in2, add);
    in3 = wasm_i32x4_add(in3, add);
    in4 = wasm_i32x4_add(in4, add);
    in5 = wasm_i32x4_add(in5, add);
    in6 = wasm_i32x4_add(in6, add);
    in7 = wasm_i32x4_add(in7, add);

    in0 = wasm_i32x4_shr(in0, shift);
    in1 = wasm_i32x4_shr(in1, shift);
    in2 = wasm_i32x4_shr(in2, shift);
    in3 = wasm_i32x4_shr(in3, shift);
    in4 = wasm_i32x4_shr(in4, shift);
    in5 = wasm_i32x4_shr(in5, shift);
    in6 = wasm_i32x4_shr(in6, shift);
    in7 = wasm_i32x4_shr(in7, shift);

    *out0 = wasm_i16x8_narrow_i32x4(in0, in1);
    *out1 = wasm_i16x8_narrow_i32x4(in2, in3);
    *out2 = wasm_i16x8_narrow_i32x4(in4, in5);
    *out3 = wasm_i16x8_narrow_i32x4(in6, in7);
}

static void transpose16_4x4_2(v128_t *r0, v128_t *r1, v128_t *r2, v128_t *r3)
{
    v128_t t0, t1, t2, t3, t4, t5;

    t0 = wasm_i16x8_shuffle(*r0, *r1, 0, 8, 2, 10, 4, 12, 6, 14);
    t1 = wasm_i16x8_shuffle(*r0, *r1, 1, 9, 3, 11, 5, 13, 7, 15);
    t2 = wasm_i16x8_shuffle(*r2, *r3, 0, 8, 2, 10, 4, 12, 6, 14);
    t3 = wasm_i16x8_shuffle(*r2, *r3, 1, 9, 3, 11, 5, 13, 7, 15);
    t4 = wasm_i32x4_shuffle(t0, t2, 0, 4, 2, 6);
    t5 = wasm_i32x4_shuffle(t0, t2, 1, 5, 3, 7);
    t0 = wasm_i32x4_shuffle(t1, t3, 0, 4, 2, 6);
    t2 = wasm_i32x4_shuffle(t1, t3, 1, 5, 3, 7);
    *r0 = wasm_i64x2_shuffle(t4, *r0, 0, 3);
    *r2 = wasm_i64x2_shuffle(t5, *r2, 0, 3);
    *r1 = wasm_i64x2_shuffle(t0, *r1, 0, 3);
    *r3 = wasm_i64x2_shuffle(t2, *r3, 0, 3);

    t0 = wasm_i16x8_shuffle(*r3, *r2, 0, 8, 2, 10, 4, 12, 6, 14);
    t1 = wasm_i16x8_shuffle(*r3, *r2, 1, 9, 3, 11, 5, 13, 7, 15);
    t2 = wasm_i16x8_shuffle(*r1, *r0, 0, 8, 2, 10, 4, 12, 6, 14);
    t3 = wasm_i16x8_shuffle(*r1, *r0, 1, 9, 3, 11, 5, 13, 7, 15);
    t4 = wasm_i32x4_shuffle(t0, t2, 0, 4, 2, 6);
    t5 = wasm_i32x4_shuffle(t0, t2, 1, 5, 3, 7);
    t0 = wasm_i32x4_shuffle(t1, t3, 0, 4, 2, 6);
    t2 = wasm_i32x4_shuffle(t1, t3, 1, 5, 3, 7);
    *r3 = wasm_i64x2_shuffle(*r3, t4, 0, 3);
    *r1 = wasm_i64x2_shuffle(*r1, t5, 0, 3);
    *r2 = wasm_i64x2_shuffle(*r2, t0, 0, 3);
    *r0 = wasm_i64x2_shuffle(*r0, t2, 0, 3);
}

static void store16(v128_t in0, v128_t in1, v128_t in2, v128_t in3,
                    char *x1, char *x3, int x1_step, int x3_step)
{
    wasm_v128_store64_lane(x1, in0, 0);
    wasm_v128_store64_lane(x3, in0, 1);
    x1 += x1_step;
    x3 += x3_step;

    wasm_v128_store64_lane(x1, in1, 0);
    wasm_v128_store64_lane(x3, in1, 1);
    x1 += x1_step;
    x3 += x3_step;

    wasm_v128_store64_lane(x1, in2, 0);
    wasm_v128_store64_lane(x3, in2, 1);
    x1 += x1_step;
    x3 += x3_step;

    wasm_v128_store64_lane(x1, in3, 0);
    wasm_v128_store64_lane(x3, in3, 1);
}


static void store_to_stack(char *sp, int off1, int off2,
                           v128_t in0, v128_t in2, v128_t in4, v128_t in6,
                           v128_t in7, v128_t in5, v128_t in3, v128_t in1)
{
    char *x1 = sp + off1;
    char *x3 = sp + off2;

    wasm_v128_store(x1, in0);
    wasm_v128_store(x3, in1);
    x1 += 16;
    x3 -= 16;
    wasm_v128_store(x1, in2);
    wasm_v128_store(x3, in3);
    x1 += 16;
    x3 -= 16;
    wasm_v128_store(x1, in4);
    wasm_v128_store(x3, in5);
    x1 += 16;
    x3 -= 16;
    wasm_v128_store(x1, in6);
    wasm_v128_store(x3, in7);
}

#define sum_sub(out, in0, in1, operation, half) \
    out = wasm_i32x4_ ## operation (out, wasm_i32x4_extmul_ ## half ## _i16x8(in0, in1));

#define add_member(in, t0, t1, t2, t3, t4, t5, t6, t7, op0, op1, op2, op3, op4, op5, op6, op7, half) \
    do {                                \
        sum_sub(v21, in, t0, op0, half) \
        sum_sub(v22, in, t1, op1, half) \
        sum_sub(v23, in, t2, op2, half) \
        sum_sub(v24, in, t3, op3, half) \
        sum_sub(v25, in, t4, op4, half) \
        sum_sub(v26, in, t5, op5, half) \
        sum_sub(v27, in, t6, op6, half) \
        sum_sub(v28, in, t7, op7, half) \
    } while (0)

#define butterfly16(in0, in1, in2, in3, in4, in5, in6, in7) \
    do {                                                    \
        v20 = wasm_i32x4_add(in0, in1);                     \
        in0 = wasm_i32x4_sub(in0, in1);                     \
        in1 = wasm_i32x4_add(in2, in3);                     \
        in2 = wasm_i32x4_sub(in2, in3);                     \
        in3 = wasm_i32x4_add(in4, in5);                     \
        in4 = wasm_i32x4_sub(in4, in5);                     \
        in5 = wasm_i32x4_add(in6, in7);                     \
        in6 = wasm_i32x4_sub(in6, in7);                     \
    } while (0)

static void tr_16x4(char *src, char *buf, char *sp,
                    int shift, int offset, int step)
{
    char *x1, *x3, *x4;
    int x2;
    v128_t trans[8];
    v128_t v16, v17, v18, v19, v20, v21, v22, v23,
           v24, v25, v26, v27, v28, v29, v30, v31;

    trans[0] = wasm_i16x8_const_splat(transform[0]);
    trans[1] = wasm_i16x8_const_splat(transform[1]);
    trans[2] = wasm_i16x8_const_splat(transform[2]);
    trans[3] = wasm_i16x8_const_splat(transform[3]);
    trans[4] = wasm_i16x8_const_splat(transform[4]);
    trans[5] = wasm_i16x8_const_splat(transform[5]);
    trans[6] = wasm_i16x8_const_splat(transform[6]);
    trans[7] = wasm_i16x8_const_splat(transform[7]);

    x1 = src;
    x3 = src + step * 64;
    x2 = step * 128;
    load16(x1, x3, x2, v16, v17, v18, v19);
    tr16_8x4(v16, v17, v18, v19, trans, sp, offset);

    x1 = src + step * 32;
    x3 = src + step * 3 * 32;
    x2 = step * 128;
    load16(x1, x3, x2, v20, v17, v18, v19);

    trans[0] = wasm_i16x8_const_splat(transform[0 + 8]);
    trans[1] = wasm_i16x8_const_splat(transform[1 + 8]);
    trans[2] = wasm_i16x8_const_splat(transform[2 + 8]);
    trans[3] = wasm_i16x8_const_splat(transform[3 + 8]);
    trans[4] = wasm_i16x8_const_splat(transform[4 + 8]);
    trans[5] = wasm_i16x8_const_splat(transform[5 + 8]);
    trans[6] = wasm_i16x8_const_splat(transform[6 + 8]);
    trans[7] = wasm_i16x8_const_splat(transform[7 + 8]);

    v21 = wasm_i32x4_extmul_low_i16x8(v20, trans[0]);
    v22 = wasm_i32x4_extmul_low_i16x8(v20, trans[1]);
    v23 = wasm_i32x4_extmul_low_i16x8(v20, trans[2]);
    v24 = wasm_i32x4_extmul_low_i16x8(v20, trans[3]);
    v25 = wasm_i32x4_extmul_low_i16x8(v20, trans[4]);
    v26 = wasm_i32x4_extmul_low_i16x8(v20, trans[5]);
    v27 = wasm_i32x4_extmul_low_i16x8(v20, trans[6]);
    v28 = wasm_i32x4_extmul_low_i16x8(v20, trans[7]);

    add_member(v20, trans[1], trans[4], trans[7], trans[5],
        trans[2], trans[0], trans[3], trans[6],
        add, add, add, sub, sub, sub, sub, sub, high);
    add_member(v17, trans[2], trans[7], trans[3], trans[1],
        trans[6], trans[4], trans[0], trans[5],
        add, add, sub, sub, sub, add, add, add, low);
    add_member(v17, trans[3], trans[5], trans[1], trans[7],
        trans[0], trans[6], trans[2], trans[4],
        add, sub, sub, add, add, add, sub, sub, high);
    add_member(v18, trans[4], trans[2], trans[6], trans[0],
        trans[7], trans[1], trans[5], trans[3],
        add, sub, sub, add, sub, sub, add, add, low);
    add_member(v18, trans[5], trans[0], trans[4], trans[6],
        trans[1], trans[3], trans[7], trans[2],
        add, sub, add, add, sub, add, add, sub, high);
    add_member(v19, trans[6], trans[3], trans[0], trans[2],
        trans[5], trans[7], trans[4], trans[1],
        add, sub, add, sub, add, add, sub, add, low);
    add_member(v19, trans[7], trans[6], trans[5], trans[4],
        trans[3], trans[2], trans[1], trans[0],
        add, sub, add, sub, add, sub, add, sub, high);

    x4 = &sp[offset];
    v16 = wasm_v128_load(x4);
    x4 += 16;
    v17 = wasm_v128_load(x4);
    x4 += 16;
    v18 = wasm_v128_load(x4);
    x4 += 16;
    v19 = wasm_v128_load(x4);
    butterfly16(v16, v21, v17, v22, v18, v23, v19, v24);

    if (shift > 0) {
        scale(&v29, &v30, &v31, &v24,
              v20, v16, v21, v17, v22, v18, v23, v19,
              shift);
        transpose16_4x4_2(&v29, &v30, &v31, &v24);
        x1 = buf;
        x3 = &buf[24 + 3 * 32];
        store16(v29, v30, v31, v24, x1, x3, 32, -32);
    } else {
        store_to_stack(sp, offset, offset + 240,
                v20, v21, v22, v23, v19, v18, v17, v16);
    }

    x4 = &sp[offset + 64];
    v16 = wasm_v128_load(x4);
    x4 += 16;
    v17 = wasm_v128_load(x4);
    x4 += 16;
    v18 = wasm_v128_load(x4);
    x4 += 16;
    v19 = wasm_v128_load(x4);
    butterfly16(v16, v25, v17, v26, v18, v27, v19, v28);

    if (shift > 0) {
        scale(&v29, &v30, &v31, &v20,
              v20, v16, v25, v17, v26, v18, v27, v19,
              shift);
        transpose16_4x4_2(&v29, &v30, &v31, &v20);
        x1 = &buf[8];
        x3 = &buf[16 + 3 * 32];
        store16(v29, v30, v31, v20, x1, x3, 32, -32);
    } else {
        store_to_stack(sp, offset + 64, offset + 176,
                v20, v25, v26, v27, v19, v18, v17, v16);
    }
}

static void idct_16x16(char *coeffs, int bit_depth)
{
    DECLARE_ALIGNED(16, char, sp)[640];

    for (int i = 0; i < 4; i++) {
        char *x5 = &coeffs[8 * i];
        char *x6 = &sp[8 * i * 16];
        tr_16x4(x5, x6, sp, 7, 512, 1);
    }

    for (int i = 0; i < 4; i++) {
        char *x5 = &sp[8 * i];
        char *x6 = &coeffs[8 * i * 16];
        tr_16x4(x5, x6, sp, 20 - bit_depth, 512, 1);
    }
}

void ff_hevc_idct_16x16_8_simd128(int16_t *coeffs, int col_limit)
{
    idct_16x16((char *)coeffs, 8);
}

void ff_hevc_idct_16x16_10_simd128(int16_t *coeffs, int col_limit)
{
    idct_16x16((char *)coeffs, 10);
}

#define add_member32(in, t0, t1, t2, t3, op0, op1, op2, op3, half) \
    do { \
        sum_sub(v24, in, t0, op0, half) \
        sum_sub(v25, in, t1, op1, half) \
        sum_sub(v26, in, t2, op2, half) \
        sum_sub(v27, in, t3, op3, half) \
    } while (0)

#define butterfly32(in0, in1, in2, in3, out) \
    do {                                     \
        out = wasm_i32x4_add(in0, in1);      \
        in0 = wasm_i32x4_sub(in0, in1);      \
        in1 = wasm_i32x4_add(in2, in3);      \
        in2 = wasm_i32x4_sub(in2, in3);      \
    } while (0)

static void tr_32x4(char *x5, char *x11, char *sp, int shift)
{
    char *x1, *x3, *x4;
    // transform in v0 - v4
    v128_t v0[4];
    v128_t v1[4];
    v128_t v2[4];
    v128_t v3[4];
    v128_t v4, v5, v6, v7, v16, v17, v18, v19,
           v20, v21, v22, v23, v24, v25, v26, v27,
           v28, v29, v30, v31, v32, v33;

    tr_16x4(x5, x11, sp, 0, 2048, 4);

    // load32
    x1 = &x5[64];
    x3 = &x1[128];
    v4 = wasm_v128_load64_zero(x1);
    v4 = wasm_v128_load64_lane(x3, v4, 1);
    x1 += 256;
    x3 += 256;
    v5 = wasm_v128_load64_zero(x1);
    v5 = wasm_v128_load64_lane(x3, v5, 1);
    x1 += 256;
    x3 += 256;
    v6 = wasm_v128_load64_zero(x1);
    v6 = wasm_v128_load64_lane(x3, v6, 1);
    x1 += 256;
    x3 += 256;
    v7 = wasm_v128_load64_zero(x1);
    v7 = wasm_v128_load64_lane(x3, v7, 1);
    x1 += 256;
    x3 += 256;
    v16 = wasm_v128_load64_zero(x1);
    v16 = wasm_v128_load64_lane(x3, v16, 1);
    x1 += 256;
    x3 += 256;
    v17 = wasm_v128_load64_zero(x1);
    v17 = wasm_v128_load64_lane(x3, v17, 1);
    x1 += 256;
    x3 += 256;
    v18 = wasm_v128_load64_zero(x1);
    v18 = wasm_v128_load64_lane(x3, v18, 1);
    x1 += 256;
    x3 += 256;
    v19 = wasm_v128_load64_zero(x1);
    v19 = wasm_v128_load64_lane(x3, v19, 1);

    // load transform
    v0[0] = wasm_i16x8_const_splat(transform[16 + 0]);
    v0[1] = wasm_i16x8_const_splat(transform[16 + 1]);
    v0[2] = wasm_i16x8_const_splat(transform[16 + 2]);
    v0[3] = wasm_i16x8_const_splat(transform[16 + 3]);
    v1[0] = wasm_i16x8_const_splat(transform[16 + 4]);
    v1[1] = wasm_i16x8_const_splat(transform[16 + 5]);
    v1[2] = wasm_i16x8_const_splat(transform[16 + 6]);
    v1[3] = wasm_i16x8_const_splat(transform[16 + 7]);
    v2[0] = wasm_i16x8_const_splat(transform[16 + 8]);
    v2[1] = wasm_i16x8_const_splat(transform[16 + 9]);
    v2[2] = wasm_i16x8_const_splat(transform[16 + 10]);
    v2[3] = wasm_i16x8_const_splat(transform[16 + 11]);
    v3[0] = wasm_i16x8_const_splat(transform[16 + 12]);
    v3[1] = wasm_i16x8_const_splat(transform[16 + 13]);
    v3[2] = wasm_i16x8_const_splat(transform[16 + 14]);
    v3[3] = wasm_i16x8_const_splat(transform[16 + 15]);

    // tr_block1
    v24 = wasm_i32x4_extmul_low_i16x8(v4, v0[0]);
    v25 = wasm_i32x4_extmul_low_i16x8(v4, v0[1]);
    v26 = wasm_i32x4_extmul_low_i16x8(v4, v0[2]);
    v27 = wasm_i32x4_extmul_low_i16x8(v4, v0[3]);

    add_member32(v4, v0[1], v1[0], v1[3], v2[2], add, add, add, add, high);
    add_member32(v5, v0[2], v1[3], v3[0], v3[2], add, add, add, sub, low);
    add_member32(v5, v0[3], v2[2], v3[2], v1[3], add, add, sub, sub, high);
    add_member32(v6, v1[0], v3[1], v2[1], v0[0], add, add, sub, sub, low);
    add_member32(v6, v1[1], v3[3], v1[0], v1[2], add, sub, sub, sub, high);
    add_member32(v7, v1[2], v3[0], v0[0], v3[1], add, sub, sub, sub, low);
    add_member32(v7, v1[3], v2[1], v1[1], v2[3], add, sub, sub, add, high);
    add_member32(v16, v2[0], v1[2], v2[2], v1[0], add, sub, sub, add, low);
    add_member32(v16, v2[1], v0[3], v3[3], v0[2], add, sub, sub, add, high);
    add_member32(v17, v2[2], v0[1], v2[3], v2[1], add, sub, add, add, low);
    add_member32(v17, v2[3], v0[2], v1[2], v3[3], add, sub, add, sub, high);
    add_member32(v18, v3[0], v1[1], v0[1], v2[0], add, sub, add, sub, low);
    add_member32(v18, v3[1], v2[0], v0[3], v0[1], add, sub, add, sub, high);
    add_member32(v19, v3[2], v2[3], v2[0], v1[1], add, sub, add, sub, low);
    add_member32(v19, v3[3], v3[2], v3[1], v3[0], add, sub, add, sub, high);

    x4 = &sp[2048];
    // scale_store
    v28 = wasm_v128_load(x4);
    x4 += 16;
    v29 = wasm_v128_load(x4);
    x4 += 16;
    v30 = wasm_v128_load(x4);
    x4 += 16;
    v31 = wasm_v128_load(x4);
    x4 += 16;
    butterfly32(v28, v24, v29, v25, v32);
    butterfly32(v30, v26, v31, v27, v33);
    scale(&v20, &v21, &v22, &v23, v32, v28, v24, v29, v33, v30, v26, v31, shift);
    transpose16_4x4_2(&v20, &v21, &v22, &v23);
    x1 = x11;
    x3 = &x11[56 + 3 * 64];
    store16(v20, v21, v22, v23, x1, x3, 64, -64);

    // tr_block2
    v24 = wasm_i32x4_extmul_low_i16x8(v4, v1[0]);
    v25 = wasm_i32x4_extmul_low_i16x8(v4, v1[1]);
    v26 = wasm_i32x4_extmul_low_i16x8(v4, v1[2]);
    v27 = wasm_i32x4_extmul_low_i16x8(v4, v1[3]);

    add_member32(v4,  v3[1], v3[3], v3[0], v2[1], add, sub, sub, sub, high);
    add_member32(v5,  v2[1], v1[0], v0[0], v1[1], sub, sub, sub, sub, low);
    add_member32(v5,  v0[0], v1[2], v3[1], v2[3], sub, sub, sub, add, high);
    add_member32(v6,  v2[0], v3[2], v1[1], v0[3], sub, add, add, add, low);
    add_member32(v6,  v3[2], v0[3], v1[3], v3[1], add, add, add, sub, high);
    add_member32(v7,  v1[1], v1[3], v2[3], v0[0], add, add, sub, sub, low);
    add_member32(v7,  v0[3], v3[1], v0[1], v3[3], add, sub, sub, add, high);
    add_member32(v16, v3[0], v0[2], v3[2], v0[1], add, sub, sub, add, low);
    add_member32(v16, v2[2], v2[0], v1[0], v3[2], sub, sub, add, add, high);
    add_member32(v17, v0[1], v3[0], v2[0], v0[2], sub, add, add, sub, low);
    add_member32(v17, v1[3], v0[1], v2[2], v3[0], sub, add, sub, sub, high);
    add_member32(v18, v3[3], v2[1], v0[2], v1[0], add, add, sub, add, low);
    add_member32(v18, v1[2], v2[3], v3[3], v2[2], add, sub, sub, add, high);
    add_member32(v19, v0[2], v0[1], v0[3], v1[2], add, sub, add, sub, low);
    add_member32(v19, v2[3], v2[2], v2[1], v2[0], add, sub, add, sub, high);

    // scale_store
    v28 = wasm_v128_load(x4);
    x4 += 16;
    v29 = wasm_v128_load(x4);
    x4 += 16;
    v30 = wasm_v128_load(x4);
    x4 += 16;
    v31 = wasm_v128_load(x4);
    x4 += 16;
    butterfly32(v28, v24, v29, v25, v32);
    butterfly32(v30, v26, v31, v27, v33);
    scale(&v20, &v21, &v22, &v23, v32, v28, v24, v29, v33, v30, v26, v31, shift);
    transpose16_4x4_2(&v20, &v21, &v22, &v23);
    x1 = &x11[8];
    x3 = &x11[48 + 3 * 64];
    store16(v20, v21, v22, v23, x1, x3, 64, -64);

    // tr_block3
    v24 = wasm_i32x4_extmul_low_i16x8(v4, v2[0]);
    v25 = wasm_i32x4_extmul_low_i16x8(v4, v2[1]);
    v26 = wasm_i32x4_extmul_low_i16x8(v4, v2[2]);
    v27 = wasm_i32x4_extmul_low_i16x8(v4, v2[3]);
    add_member32(v4,  v1[2], v0[3], v0[0], v0[2], sub, sub, sub, sub, high);
    add_member32(v5,  v2[2], v3[3], v2[3], v1[2], sub, sub, add, add, low);
    add_member32(v5,  v1[0], v0[2], v2[1], v3[3], add, add, add, sub, high);
    add_member32(v6,  v3[0], v2[2], v0[1], v1[3], add, sub, sub, sub, low);
    add_member32(v6,  v0[2], v2[0], v3[0], v0[0], sub, sub, add, add, high);
    add_member32(v7,  v3[2], v1[0], v2[0], v2[2], sub, add, add, sub, low);
    add_member32(v7,  v0[0], v3[2], v0[2], v3[0], add, add, sub, sub, high);
    add_member32(v16, v3[3], v0[1], v3[1], v0[3], sub, sub, add, add, low);
    add_member32(v16, v0[1], v2[3], v1[3], v1[1], sub, add, add, sub, high);
    add_member32(v17, v3[1], v1[3], v0[3], v3[2], add, add, sub, add, low);
    add_member32(v17, v0[3], v1[1], v3[2], v2[0], add, sub, add, add, high);
    add_member32(v18, v2[3], v3[1], v1[2], v0[1], sub, sub, add, sub, low);
    add_member32(v18, v1[1], v0[0], v1[0], v2[1], sub, add, sub, add, high);
    add_member32(v19, v2[1], v3[0], v3[3], v3[1], add, sub, add, add, low);
    add_member32(v19, v1[3], v1[2], v1[1], v1[0], add, sub, add, sub, high);

    // scale_store
    v28 = wasm_v128_load(x4);
    x4 += 16;
    v29 = wasm_v128_load(x4);
    x4 += 16;
    v30 = wasm_v128_load(x4);
    x4 += 16;
    v31 = wasm_v128_load(x4);
    x4 += 16;
    butterfly32(v28, v24, v29, v25, v32);
    butterfly32(v30, v26, v31, v27, v33);
    scale(&v20, &v21, &v22, &v23, v32, v28, v24, v29, v33, v30, v26, v31, shift);
    transpose16_4x4_2(&v20, &v21, &v22, &v23);
    x1 = &x11[16];
    x3 = &x11[40 + 3 * 64];
    store16(v20, v21, v22, v23, x1, x3, 64, -64);

    // try_block4
    v24 = wasm_i32x4_extmul_low_i16x8(v4, v3[0]);
    v25 = wasm_i32x4_extmul_low_i16x8(v4, v3[1]);
    v26 = wasm_i32x4_extmul_low_i16x8(v4, v3[2]);
    v27 = wasm_i32x4_extmul_low_i16x8(v4, v3[3]);
    add_member32(v4,  v1[1], v2[0], v2[3], v3[2], sub, sub, sub, sub, high);
    add_member32(v5,  v0[0], v0[3], v2[0], v3[1], add, add, add, add, low);
    add_member32(v5,  v2[0], v0[0], v1[1], v3[0], sub, sub, sub, sub, high);
    add_member32(v6,  v3[3], v1[2], v0[2], v2[3], add, add, add, add, low);
    add_member32(v6,  v2[1], v2[3], v0[0], v2[2], add, sub, sub, sub, high);
    add_member32(v7,  v0[2], v3[3], v0[3], v2[1], sub, sub, add, add, low);
    add_member32(v7,  v1[0], v2[2], v1[2], v2[0], add, add, sub, sub, high);
    add_member32(v16, v2[3], v1[1], v2[1], v1[3], sub, sub, add, add, low);
    add_member32(v16, v3[1], v0[1], v3[0], v1[2], sub, add, sub, sub, high);
    add_member32(v17, v1[2], v1[0], v3[3], v1[1], add, sub, add, add, low);
    add_member32(v17, v0[1], v2[1], v3[1], v1[0], sub, add, add, sub, high);
    add_member32(v18, v1[3], v3[2], v2[2], v0[3], add, sub, sub, add, low);
    add_member32(v18, v3[2], v3[0], v1[3], v0[2], sub, sub, add, sub, high);
    add_member32(v19, v2[2], v1[3], v1[0], v0[1], sub, add, sub, add, low);
    add_member32(v19, v0[3], v0[2], v0[1], v0[0], add, sub, add, sub, high);

    // scale_store
    v28 = wasm_v128_load(x4);
    x4 += 16;
    v29 = wasm_v128_load(x4);
    x4 += 16;
    v30 = wasm_v128_load(x4);
    x4 += 16;
    v31 = wasm_v128_load(x4);
    butterfly32(v28, v24, v29, v25, v32);
    butterfly32(v30, v26, v31, v27, v33);
    scale(&v20, &v21, &v22, &v23, v32, v28, v24, v29, v33, v30, v26, v31, shift);
    transpose16_4x4_2(&v20, &v21, &v22, &v23);
    x1 = &x11[24];
    x3 = &x11[32 + 3 * 64];
    store16(v20, v21, v22, v23, x1, x3, 64, -64);
}

static void idct_32x32(char *coeffs, int bit_depth)
{
    DECLARE_ALIGNED(16, char, sp)[2432];
    char *x5, *x11;

    for (int i = 0; i < 8; i++) {
        x5 = &coeffs[8 * i];
        x11 = &sp[8 * i * 32];
        tr_32x4(x5, x11, sp, 7);
    }

    for (int i = 0; i < 8; i++) {
        x5 = &sp[8 * i];
        x11 = &coeffs[8 * i * 32];
        tr_32x4(x5, x11, sp, 20 - bit_depth);
    }
}

void ff_hevc_idct_32x32_8_simd128(int16_t *coeffs, int col_limit)
{
    idct_32x32((char *)coeffs, 8);
}

void ff_hevc_idct_32x32_10_simd128(int16_t *coeffs, int col_limit)
{
    idct_32x32((char *)coeffs, 10);
}
