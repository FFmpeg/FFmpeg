/*
 * Copyright (c) 2022 Ben Avison
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>

#include "checkasm.h"

#include "libavcodec/vc1dsp.h"

#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#define VC1DSP_TEST(func) { #func, offsetof(VC1DSPContext, func) },
#define VC1DSP_SIZED_TEST(func, width, height) { #func, offsetof(VC1DSPContext, func), width, height },

typedef struct {
    const char *name;
    size_t offset;
    int width;
    int height;
} test;

typedef struct matrix {
    size_t width;
    size_t height;
    float d[];
} matrix;

static const matrix T8 = { 8, 8, {
        12,  12,  12,  12,  12,  12,  12,  12,
        16,  15,   9,   4,  -4,  -9, -15, -16,
        16,   6,  -6, -16, -16,  -6,   6,  16,
        15,  -4, -16,  -9,   9,  16,   4, -15,
        12, -12, -12,  12,  12, -12, -12,  12,
         9, -16,   4,  15, -15,  -4,  16,  -9,
         6, -16,  16,  -6,  -6,  16, -16,   6,
         4,  -9,  15, -16,  16, -15,   9,  -4
} };

static const matrix T4 = { 4, 4, {
        17,  17,  17,  17,
        22,  10, -10, -22,
        17, -17, -17,  17,
        10, -22,  22, -10
} };

static const matrix T8t = { 8, 8, {
        12,  16,  16,  15,  12,   9,   6,   4,
        12,  15,   6,  -4, -12, -16, -16,  -9,
        12,   9,  -6, -16, -12,   4,  16,  15,
        12,   4, -16,  -9,  12,  15,  -6, -16,
        12,  -4, -16,   9,  12, -15,  -6,  16,
        12,  -9,  -6,  16, -12,  -4,  16, -15,
        12, -15,   6,   4, -12,  16, -16,   9,
        12, -16,  16, -15,  12,  -9,   6,  -4
} };

static const matrix T4t = { 4, 4, {
        17,  22,  17,  10,
        17,  10, -17, -22,
        17, -10, -17,  22,
        17, -22,  17, -10
} };

static matrix *new_matrix(size_t width, size_t height)
{
    matrix *out = av_mallocz(sizeof (matrix) + height * width * sizeof (float));
    if (out == NULL) {
        fprintf(stderr, "Memory allocation failure\n");
        exit(EXIT_FAILURE);
    }
    out->width = width;
    out->height = height;
    return out;
}

static matrix *multiply(const matrix *a, const matrix *b)
{
    matrix *out;
    if (a->width != b->height) {
        fprintf(stderr, "Incompatible multiplication\n");
        exit(EXIT_FAILURE);
    }
    out = new_matrix(b->width, a->height);
    for (int j = 0; j < out->height; ++j)
        for (int i = 0; i < out->width; ++i) {
            float sum = 0;
            for (int k = 0; k < a->width; ++k)
                sum += a->d[j * a->width + k] * b->d[k * b->width + i];
            out->d[j * out->width + i] = sum;
        }
    return out;
}

static void normalise(matrix *a)
{
    for (int j = 0; j < a->height; ++j)
        for (int i = 0; i < a->width; ++i) {
            float *p = a->d + j * a->width + i;
            *p *= 64;
            if (a->height == 4)
                *p /= (const unsigned[]) { 289, 292, 289, 292 } [j];
            else
                *p /= (const unsigned[]) { 288, 289, 292, 289, 288, 289, 292, 289 } [j];
            if (a->width == 4)
                *p /= (const unsigned[]) { 289, 292, 289, 292 } [i];
            else
                *p /= (const unsigned[]) { 288, 289, 292, 289, 288, 289, 292, 289 } [i];
        }
}

static void divide_and_round_nearest(matrix *a, float by)
{
    for (int j = 0; j < a->height; ++j)
        for (int i = 0; i < a->width; ++i) {
            float *p = a->d + j * a->width + i;
            *p = rintf(*p / by);
        }
}

static void tweak(matrix *a)
{
    for (int j = 4; j < a->height; ++j)
        for (int i = 0; i < a->width; ++i) {
            float *p = a->d + j * a->width + i;
            *p += 1;
        }
}

/* The VC-1 spec places restrictions on the values permitted at three
 * different stages:
 * - D: the input coefficients in frequency domain
 * - E: the intermediate coefficients, inverse-transformed only horizontally
 * - R: the fully inverse-transformed coefficients
 *
 * To fully cater for the ranges specified requires various intermediate
 * values to be held to 17-bit precision; yet these conditions do not appear
 * to be utilised in real-world streams. At least some assembly
 * implementations have chosen to restrict these values to 16-bit precision,
 * to accelerate the decoding of real-world streams at the cost of strict
 * adherence to the spec. To avoid our test marking these as failures,
 * reduce our random inputs.
 */
#define ATTENUATION 4

static matrix *generate_inverse_quantized_transform_coefficients(size_t width, size_t height)
{
    matrix *raw, *tmp, *D, *E, *R;
    raw = new_matrix(width, height);
    for (int i = 0; i < width * height; ++i)
        raw->d[i] = (int) (rnd() % (1024/ATTENUATION)) - 512/ATTENUATION;
    tmp = multiply(height == 8 ? &T8 : &T4, raw);
    D = multiply(tmp, width == 8 ? &T8t : &T4t);
    normalise(D);
    divide_and_round_nearest(D, 1);
    for (int i = 0; i < width * height; ++i) {
        if (D->d[i] < -2048/ATTENUATION || D->d[i] > 2048/ATTENUATION-1) {
            /* Rare, so simply try again */
            av_free(raw);
            av_free(tmp);
            av_free(D);
            return generate_inverse_quantized_transform_coefficients(width, height);
        }
    }
    E = multiply(D, width == 8 ? &T8 : &T4);
    divide_and_round_nearest(E, 8);
    for (int i = 0; i < width * height; ++i)
        if (E->d[i] < -4096/ATTENUATION || E->d[i] > 4096/ATTENUATION-1) {
            /* Rare, so simply try again */
            av_free(raw);
            av_free(tmp);
            av_free(D);
            av_free(E);
            return generate_inverse_quantized_transform_coefficients(width, height);
        }
    R = multiply(height == 8 ? &T8t : &T4t, E);
    tweak(R);
    divide_and_round_nearest(R, 128);
    for (int i = 0; i < width * height; ++i)
        if (R->d[i] < -512/ATTENUATION || R->d[i] > 512/ATTENUATION-1) {
            /* Rare, so simply try again */
            av_free(raw);
            av_free(tmp);
            av_free(D);
            av_free(E);
            av_free(R);
            return generate_inverse_quantized_transform_coefficients(width, height);
        }
    av_free(raw);
    av_free(tmp);
    av_free(E);
    av_free(R);
    return D;
}

#define RANDOMIZE_BUFFER16(name, size)        \
    do {                                      \
        int i;                                \
        for (i = 0; i < size; ++i) {          \
            uint16_t r = rnd();               \
            AV_WN16A(name##0 + i, r);         \
            AV_WN16A(name##1 + i, r);         \
        }                                     \
    } while (0)

#define RANDOMIZE_BUFFER8(name, size)         \
    do {                                      \
        int i;                                \
        for (i = 0; i < size; ++i) {          \
            uint8_t r = rnd();                \
            name##0[i] = r;                   \
            name##1[i] = r;                   \
        }                                     \
    } while (0)

#define RANDOMIZE_BUFFER8_MID_WEIGHTED(name, size)  \
    do {                                            \
        uint8_t *p##0 = name##0, *p##1 = name##1;   \
        int i = (size);                             \
        while (i-- > 0) {                           \
            int x = 0x80 | (rnd() & 0x7F);          \
            x >>= rnd() % 9;                        \
            if (rnd() & 1)                          \
                x = -x;                             \
            *p##1++ = *p##0++ = 0x80 + x;           \
        }                                           \
    } while (0)

static void check_inv_trans_inplace(void)
{
    /* Inverse transform input coefficients are stored in a 16-bit buffer
     * with row stride of 8 coefficients irrespective of transform size.
     * vc1_inv_trans_8x8 differs from the others in two ways: coefficients
     * are stored in column-major order, and the outputs are written back
     * to the input buffer, so we oversize it slightly to catch overruns. */
    LOCAL_ALIGNED_16(int16_t, inv_trans_in0, [10 * 8]);
    LOCAL_ALIGNED_16(int16_t, inv_trans_in1, [10 * 8]);

    VC1DSPContext h;

    ff_vc1dsp_init(&h);

    if (check_func(h.vc1_inv_trans_8x8, "vc1dsp.vc1_inv_trans_8x8")) {
        matrix *coeffs;
        declare_func(void, int16_t *);
        RANDOMIZE_BUFFER16(inv_trans_in, 10 * 8);
        coeffs = generate_inverse_quantized_transform_coefficients(8, 8);
        for (int j = 0; j < 8; ++j)
            for (int i = 0; i < 8; ++i) {
                int idx = 8 + i * 8 + j;
                inv_trans_in1[idx] = inv_trans_in0[idx] = coeffs->d[j * 8 + i];
            }
        call_ref(inv_trans_in0 + 8);
        call_new(inv_trans_in1 + 8);
        if (memcmp(inv_trans_in0,  inv_trans_in1,  10 * 8 * sizeof (int16_t)))
            fail();
        bench_new(inv_trans_in1 + 8);
        av_free(coeffs);
    }
}

static void check_inv_trans_adding(void)
{
    /* Inverse transform input coefficients are stored in a 16-bit buffer
     * with row stride of 8 coefficients irrespective of transform size. */
    LOCAL_ALIGNED_16(int16_t, inv_trans_in0, [8 * 8]);
    LOCAL_ALIGNED_16(int16_t, inv_trans_in1, [8 * 8]);

    /* For all but vc1_inv_trans_8x8, the inverse transform is narrowed and
     * added with saturation to an array of unsigned 8-bit values. Oversize
     * this by 8 samples left and right and one row above and below. */
    LOCAL_ALIGNED_8(uint8_t, inv_trans_out0, [10 * 24]);
    LOCAL_ALIGNED_8(uint8_t, inv_trans_out1, [10 * 24]);

    VC1DSPContext h;

    const test tests[] = {
        VC1DSP_SIZED_TEST(vc1_inv_trans_8x4, 8, 4)
        VC1DSP_SIZED_TEST(vc1_inv_trans_4x8, 4, 8)
        VC1DSP_SIZED_TEST(vc1_inv_trans_4x4, 4, 4)
        VC1DSP_SIZED_TEST(vc1_inv_trans_8x8_dc, 8, 8)
        VC1DSP_SIZED_TEST(vc1_inv_trans_8x4_dc, 8, 4)
        VC1DSP_SIZED_TEST(vc1_inv_trans_4x8_dc, 4, 8)
        VC1DSP_SIZED_TEST(vc1_inv_trans_4x4_dc, 4, 4)
    };

    ff_vc1dsp_init(&h);

    for (size_t t = 0; t < FF_ARRAY_ELEMS(tests); ++t) {
        void (*func)(uint8_t *, ptrdiff_t, int16_t *) = *(void **)((intptr_t) &h + tests[t].offset);
        if (check_func(func, "vc1dsp.%s", tests[t].name)) {
            matrix *coeffs;
            declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *, ptrdiff_t, int16_t *);
            RANDOMIZE_BUFFER16(inv_trans_in, 8 * 8);
            RANDOMIZE_BUFFER8(inv_trans_out, 10 * 24);
            coeffs = generate_inverse_quantized_transform_coefficients(tests[t].width, tests[t].height);
            for (int j = 0; j < tests[t].height; ++j)
                for (int i = 0; i < tests[t].width; ++i) {
                    int idx = j * 8 + i;
                    inv_trans_in1[idx] = inv_trans_in0[idx] = coeffs->d[j * tests[t].width + i];
                }
            call_ref(inv_trans_out0 + 24 + 8, 24, inv_trans_in0);
            call_new(inv_trans_out1 + 24 + 8, 24, inv_trans_in1);
            if (memcmp(inv_trans_out0, inv_trans_out1, 10 * 24))
                fail();
            bench_new(inv_trans_out1 + 24 + 8, 24, inv_trans_in1 + 8);
            av_free(coeffs);
        }
    }
}

static void check_loop_filter(void)
{
    /* Deblocking filter buffers are big enough to hold a 16x16 block,
     * plus 16 columns left and 4 rows above to hold filter inputs
     * (depending on whether v or h neighbouring block edge, oversized
     * horizontally to maintain 16-byte alignment) plus 16 columns and
     * 4 rows below to catch write overflows */
    LOCAL_ALIGNED_16(uint8_t, filter_buf0, [24 * 48]);
    LOCAL_ALIGNED_16(uint8_t, filter_buf1, [24 * 48]);

    VC1DSPContext h;

    const test tests[] = {
        VC1DSP_TEST(vc1_v_loop_filter4)
        VC1DSP_TEST(vc1_h_loop_filter4)
        VC1DSP_TEST(vc1_v_loop_filter8)
        VC1DSP_TEST(vc1_h_loop_filter8)
        VC1DSP_TEST(vc1_v_loop_filter16)
        VC1DSP_TEST(vc1_h_loop_filter16)
    };

    ff_vc1dsp_init(&h);

    for (size_t t = 0; t < FF_ARRAY_ELEMS(tests); ++t) {
        void (*func)(uint8_t *, ptrdiff_t, int) = *(void **)((intptr_t) &h + tests[t].offset);
        declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *, ptrdiff_t, int);
        if (check_func(func, "vc1dsp.%s", tests[t].name)) {
            for (int count = 1000; count > 0; --count) {
                int pq = rnd() % 31 + 1;
                RANDOMIZE_BUFFER8_MID_WEIGHTED(filter_buf, 24 * 48);
                call_ref(filter_buf0 + 4 * 48 + 16, 48, pq);
                call_new(filter_buf1 + 4 * 48 + 16, 48, pq);
                if (memcmp(filter_buf0, filter_buf1, 24 * 48))
                    fail();
            }
        }
        for (int j = 0; j < 24; ++j)
            for (int i = 0; i < 48; ++i)
                filter_buf1[j * 48 + i] = 0x60 + 0x40 * (i >= 16 && j >= 4);
        if (check_func(func, "vc1dsp.%s_bestcase", tests[t].name))
            bench_new(filter_buf1 + 4 * 48 + 16, 48, 1);
        if (check_func(func, "vc1dsp.%s_worstcase", tests[t].name))
            bench_new(filter_buf1 + 4 * 48 + 16, 48, 31);
    }
}

#define TEST_UNESCAPE                                                                               \
    do {                                                                                            \
        for (int count = 100; count > 0; --count) {                                                 \
            escaped_offset = rnd() & 7;                                                             \
            unescaped_offset = rnd() & 7;                                                           \
            escaped_len = (1u << (rnd() % 8) + 3) - (rnd() & 7);                                    \
            RANDOMIZE_BUFFER8(unescaped, UNESCAPE_BUF_SIZE);                                        \
            len0 = call_ref(escaped0 + escaped_offset, escaped_len, unescaped0 + unescaped_offset); \
            len1 = call_new(escaped1 + escaped_offset, escaped_len, unescaped1 + unescaped_offset); \
            if (len0 != len1 || memcmp(unescaped0, unescaped1, UNESCAPE_BUF_SIZE))                  \
                fail();                                                                             \
        }                                                                                           \
    } while (0)

static void check_unescape(void)
{
    /* This appears to be a typical length of buffer in use */
#define LOG2_UNESCAPE_BUF_SIZE 17
#define UNESCAPE_BUF_SIZE (1u<<LOG2_UNESCAPE_BUF_SIZE)
    LOCAL_ALIGNED_8(uint8_t, escaped0, [UNESCAPE_BUF_SIZE]);
    LOCAL_ALIGNED_8(uint8_t, escaped1, [UNESCAPE_BUF_SIZE]);
    LOCAL_ALIGNED_8(uint8_t, unescaped0, [UNESCAPE_BUF_SIZE]);
    LOCAL_ALIGNED_8(uint8_t, unescaped1, [UNESCAPE_BUF_SIZE]);

    VC1DSPContext h;

    ff_vc1dsp_init(&h);

    if (check_func(h.vc1_unescape_buffer, "vc1dsp.vc1_unescape_buffer")) {
        int len0, len1, escaped_offset, unescaped_offset, escaped_len;
        declare_func(int, const uint8_t *, int, uint8_t *);

        /* Test data which consists of escapes sequences packed as tightly as possible */
        for (int x = 0; x < UNESCAPE_BUF_SIZE; ++x)
            escaped1[x] = escaped0[x] = 3 * (x % 3 == 0);
        TEST_UNESCAPE;

        /* Test random data */
        RANDOMIZE_BUFFER8(escaped, UNESCAPE_BUF_SIZE);
        TEST_UNESCAPE;

        /* Test data with escape sequences at random intervals */
        for (int x = 0; x <= UNESCAPE_BUF_SIZE - 4;) {
            int gap, gap_msb;
            escaped1[x+0] = escaped0[x+0] = 0;
            escaped1[x+1] = escaped0[x+1] = 0;
            escaped1[x+2] = escaped0[x+2] = 3;
            escaped1[x+3] = escaped0[x+3] = rnd() & 3;
            gap_msb = 2u << (rnd() % 8);
            gap = (rnd() &~ -gap_msb) | gap_msb;
            x += gap;
        }
        TEST_UNESCAPE;

        /* Test data which is known to contain no escape sequences */
        memset(escaped0, 0xFF, UNESCAPE_BUF_SIZE);
        memset(escaped1, 0xFF, UNESCAPE_BUF_SIZE);
        TEST_UNESCAPE;

        /* Benchmark the no-escape-sequences case */
        bench_new(escaped1, UNESCAPE_BUF_SIZE, unescaped1);
    }
}

void checkasm_check_vc1dsp(void)
{
    check_inv_trans_inplace();
    check_inv_trans_adding();
    report("inv_trans");

    check_loop_filter();
    report("loop_filter");

    check_unescape();
    report("unescape_buffer");
}
