/*
 * Copyright (c) 2016 Alexandra Hájková
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

#include <stddef.h>
#include <string.h>

#include "libavutil/cpu.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"

#include "libavcodec/lossless_videodsp.h"

#include "checkasm.h"

#define randomize_buffers(buf, size)     \
    do {                                 \
        int j;                           \
        uint8_t *tmp_buf = (uint8_t *)buf;\
        for (j = 0; j < size; j++)       \
            tmp_buf[j] = rnd() & 0xFF;       \
    } while (0)

#define init_buffer(a0, a1, type, width)\
    if (!a0 || !a1)\
        fail();\
    randomize_buffers(a0, width * sizeof(type));\
    memcpy(a1, a0, width*sizeof(type));\

static void check_add_bytes(LLVidDSPContext *c, int width, size_t aligned_width)
{
    uint8_t *dst0 = av_mallocz(aligned_width);
    uint8_t *dst1 = av_mallocz(aligned_width);
    uint8_t *src0 = av_malloc(aligned_width);
    uint8_t *src1 = av_malloc(aligned_width);
    declare_func(void, uint8_t *dst, uint8_t *src, ptrdiff_t w);

    init_buffer(src0, src1, uint8_t, width);

    if (!dst0 || !dst1)
        fail();


    call_ref(dst0, src0, width);
    call_new(dst1, src1, width);
    if (memcmp(dst0, dst1, width))
        fail();
    bench_new(dst1, src1, width);

    av_free(src0);
    av_free(src1);
    av_free(dst0);
    av_free(dst1);
}

static void check_add_median_pred(LLVidDSPContext *c, int width, size_t aligned_width)
{
    int a0, a1, b0, b1;
    uint8_t *dst0  = av_mallocz(aligned_width);
    uint8_t *dst1  = av_mallocz(aligned_width);
    uint8_t *src0  = av_malloc(aligned_width);
    uint8_t *src1  = av_malloc(aligned_width);
    uint8_t *diff0 = av_malloc(aligned_width);
    uint8_t *diff1 = av_malloc(aligned_width);
    declare_func(void, uint8_t *dst, const uint8_t *src1,
                 const uint8_t *diff, ptrdiff_t w,
                 int *left, int *left_top);

    init_buffer(src0, src1, uint8_t, width);
    init_buffer(diff0, diff1, uint8_t, width);

    a0 = rnd() & 0xFF;
    b0 = rnd() & 0xFF;
    a1 = a0;
    b1 = b0;


    call_ref(dst0, src0, diff0, width, &a0, &b0);
    call_new(dst1, src1, diff1, width, &a1, &b1);
    if (memcmp(dst0, dst1, width) || (a0 != a1) || (b0 != b1))
        fail();
    bench_new(dst1, src1, diff1, width, &a1, &b1);

    av_free(src0);
    av_free(src1);
    av_free(diff0);
    av_free(diff1);
    av_free(dst0);
    av_free(dst1);
}

static void check_add_left_pred(LLVidDSPContext *c, int width, size_t aligned_width, int acc)
{
    int res0, res1;
    declare_func(int, uint8_t *dst, const uint8_t *src, ptrdiff_t w, int acc);
    uint8_t *dst0 = av_mallocz(aligned_width);
    uint8_t *dst1 = av_mallocz(aligned_width);
    uint8_t *src0 = av_malloc(aligned_width);
    uint8_t *src1 = av_malloc(aligned_width);

    init_buffer(src0, src1, uint8_t, width);

    if (!dst0 || !dst1)
        fail();

    res0 = call_ref(dst0, src0, width, acc);
    res1 = call_new(dst1, src1, width, acc);
    if ((res0 & 0xFF) != (res1 & 0xFF) || memcmp(dst0, dst1, width))
        fail();
    bench_new(dst1, src1, width, acc);

    av_free(src0);
    av_free(src1);
    av_free(dst0);
    av_free(dst1);
}

static void check_add_left_pred_16(LLVidDSPContext *c, unsigned mask, int width,
                                   size_t align, unsigned acc)
{
    int res0, res1;
    uint16_t *dst0, *dst1, *src0, *src1;
    size_t aligned_width = FFALIGN(width * sizeof(*dst0), align);
    declare_func(int, uint16_t *dst, const uint16_t *src, unsigned mask, ptrdiff_t w, unsigned acc);

    dst0 = av_mallocz(aligned_width);
    dst1 = av_mallocz(aligned_width);
    src0 = av_malloc(aligned_width);
    src1 = av_malloc(aligned_width);

    init_buffer(src0, src1, uint16_t, width);

    if (!dst0 || !dst1)
        fail();

    res0 = call_ref(dst0, src0, mask, width, acc);
    res1 = call_new(dst1, src1, mask, width, acc);
    if ((res0 & 0xFFFF) != (res1 & 0xFFFF) || memcmp(dst0, dst1, width * sizeof(*dst0)))
        fail();
    bench_new(dst1, src1, mask, width, acc);

    av_free(src0);
    av_free(src1);
    av_free(dst0);
    av_free(dst1);
}

static void check_add_gradient_pred(LLVidDSPContext *c, int w, size_t align)
{
    int src_size, stride;
    uint8_t *src0, *src1;
    declare_func(void, uint8_t *src, const ptrdiff_t stride,
                 const ptrdiff_t width);

    stride = FFALIGN(w + 32, align);
    src_size = (stride + 32) * 2; /* dsp need previous line, and ignore the start of the line */
    src0 = av_mallocz(src_size);
    src1 = av_mallocz(src_size);

    init_buffer(src0, src1, uint8_t, src_size);

    call_ref(src0 + stride + 32, stride, w);
    call_new(src1 + stride + 32, stride, w);
    if (memcmp(src0, src1, stride)||/* previous line doesn't change */
        memcmp(src0+stride, src1 + stride, w + 32)) {
        fail();
    }
    bench_new(src1 + stride + 32, stride, w);

    av_free(src0);
    av_free(src1);
}

void checkasm_check_llviddsp(void)
{
    LLVidDSPContext c;
    int accRnd = rnd() & 0xFF;

    size_t align = av_cpu_max_align();
    int width  = 1 + rnd() % 16*128;
    size_t aligned_width = FFALIGN(width, align);

    ff_llviddsp_init(&c);

    if (check_func(c.add_bytes, "add_bytes"))
        check_add_bytes(&c, width, aligned_width);
    report("add_bytes");

    if (check_func(c.add_median_pred, "add_median_pred"))
        check_add_median_pred(&c, width, aligned_width);
    report("add_median_pred");

    if (check_func(c.add_left_pred, "add_left_pred_zero"))
        check_add_left_pred(&c, width, aligned_width, 0);
    report("add_left_pred_zero");

    if (check_func(c.add_left_pred, "add_left_pred_rnd_acc"))
        check_add_left_pred(&c, width, aligned_width, accRnd);
    report("add_left_pred_rnd_acc");

    if (check_func(c.add_left_pred_int16, "add_left_pred_int16"))
        check_add_left_pred_16(&c, 255, width, align, accRnd);
    report("add_left_pred_int16");

    if (check_func(c.add_gradient_pred, "add_gradient_pred"))
        check_add_gradient_pred(&c, width, align);
    report("add_gradient_pred");
}
