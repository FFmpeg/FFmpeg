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

#include <string.h>

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
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

static void check_add_bytes(LLVidDSPContext c, int width)
{
    uint8_t *dst0 = av_mallocz(width);
    uint8_t *dst1 = av_mallocz(width);
    uint8_t *src0 = av_mallocz_array(width, sizeof(uint8_t));
    uint8_t *src1 = av_mallocz_array(width, sizeof(uint8_t));
    declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *dst, uint8_t *src, ptrdiff_t w);

    init_buffer(src0, src1, uint8_t, width);

    if (!dst0 || !dst1)
        fail();


    if (check_func(c.add_bytes, "add_bytes")) {
        call_ref(dst0, src0, width);
        call_new(dst1, src1, width);
        if (memcmp(dst0, dst1, width))
            fail();
        bench_new(dst1, src1, width);
    }

    av_free(src0);
    av_free(src1);
    av_free(dst0);
    av_free(dst1);
}

static void check_add_median_pred(LLVidDSPContext c, int width) {
    int A0, A1, B0, B1;
    uint8_t *dst0 = av_mallocz(width);
    uint8_t *dst1 = av_mallocz(width);
    uint8_t *src0 = av_mallocz_array(width, sizeof(uint8_t));
    uint8_t *src1 = av_mallocz_array(width, sizeof(uint8_t));
    uint8_t *diff0 = av_mallocz_array(width, sizeof(uint8_t));
    uint8_t *diff1 = av_mallocz_array(width, sizeof(uint8_t));
    declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *dst, const uint8_t *src1,
                      const uint8_t *diff, ptrdiff_t w,
                      int *left, int *left_top);

    init_buffer(src0, src1, uint8_t, width);
    init_buffer(diff0, diff1, uint8_t, width);

    A0 = rnd() & 0xFF;
    B0 = rnd() & 0xFF;
    A1 = A0;
    B1 = B0;


    if (check_func(c.add_median_pred, "add_median_pred")) {
        call_ref(dst0, src0, diff0, width, &A0, &B0);
        call_new(dst1, src1, diff1, width, &A1, &B1);
        if (memcmp(dst0, dst1, width) || (A0 != A1) || (B0 != B1))
            fail();
        bench_new(dst1, src1, diff1, width, &A1, &B1);
    }

    av_free(src0);
    av_free(src1);
    av_free(diff0);
    av_free(diff1);
    av_free(dst0);
    av_free(dst1);
}

static void check_add_left_pred(LLVidDSPContext c, int width, int acc, const char * report)
{
    int res0, res1;
    uint8_t *dst0 = av_mallocz(width);
    uint8_t *dst1 = av_mallocz(width);
    uint8_t *src0 = av_mallocz_array(width, sizeof(uint8_t));
    uint8_t *src1 = av_mallocz_array(width, sizeof(uint8_t));
    declare_func_emms(AV_CPU_FLAG_MMX, int, uint8_t *dst, uint8_t *src, ptrdiff_t w, int acc);

    init_buffer(src0, src1, uint8_t, width);

    if (!dst0 || !dst1)
        fail();

    if (check_func(c.add_left_pred, "%s", report)) {
        res0 = call_ref(dst0, src0, width, acc);
        res1 = call_new(dst1, src1, width, acc);
        if ((res0 & 0xFF) != (res1 & 0xFF)||\
            memcmp(dst0, dst1, width))
            fail();
        bench_new(dst1, src1, width, acc);
    }

    av_free(src0);
    av_free(src1);
    av_free(dst0);
    av_free(dst1);
}

static void check_add_left_pred_16(LLVidDSPContext c, unsigned mask, int width, unsigned acc, const char * report)
{
    int res0, res1;
    uint16_t *dst0 = av_mallocz_array(width, sizeof(uint16_t));
    uint16_t *dst1 = av_mallocz_array(width, sizeof(uint16_t));
    uint16_t *src0 = av_mallocz_array(width, sizeof(uint16_t));
    uint16_t *src1 = av_mallocz_array(width, sizeof(uint16_t));
    declare_func_emms(AV_CPU_FLAG_MMX, int, uint16_t *dst, uint16_t *src, unsigned mask, ptrdiff_t w, unsigned acc);

    init_buffer(src0, src1, uint16_t, width);

    if (!dst0 || !dst1)
        fail();

    if (check_func(c.add_left_pred_int16, "%s", report)) {
        res0 = call_ref(dst0, src0, mask, width, acc);
        res1 = call_new(dst1, src1, mask, width, acc);
        if ((res0 &0xFFFF) != (res1 &0xFFFF)||\
            memcmp(dst0, dst1, width))
            fail();
        bench_new(dst1, src1, mask, width, acc);
    }

    av_free(src0);
    av_free(src1);
    av_free(dst0);
    av_free(dst1);
}

static void check_add_gradient_pred(LLVidDSPContext c, int w) {
    int src_size, stride;
    uint8_t *src0, *src1;
    declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *src, const ptrdiff_t stride,
                      const ptrdiff_t width);

    stride = w + 32;
    src_size = (stride + 32) * 2; /* dsp need previous line, and ignore the start of the line */
    src0 = av_mallocz(src_size);
    src1 = av_mallocz(src_size);

    init_buffer(src0, src1, uint8_t, src_size);

    if (check_func(c.add_gradient_pred, "add_gradient_pred")) {
        call_ref(src0 + stride + 32, stride, w);
        call_new(src1 + stride + 32, stride, w);
        if (memcmp(src0, src1, stride)||/* previous line doesn't change */
            memcmp(src0+stride, src1 + stride, w + 32)) {
            fail();
        }
        bench_new(src1 + stride + 32, stride, w);
    }

    av_free(src0);
    av_free(src1);
}

void checkasm_check_llviddsp(void)
{
    LLVidDSPContext c;
    int width = 16 * av_clip(rnd(), 16, 128);
    int accRnd = rnd() & 0xFF;

    ff_llviddsp_init(&c);

    check_add_bytes(c, width);
    report("add_bytes");

    check_add_median_pred(c, width);
    report("add_median_pred");

    check_add_left_pred(c, width, 0, "add_left_pred_zero");
    report("add_left_pred_zero");

    check_add_left_pred(c, width, accRnd, "add_left_pred_rnd_acc");
    report("add_left_pred_rnd_acc");

    check_add_left_pred_16(c, 255, width, accRnd, "add_left_pred_int16");
    report("add_left_pred_int16");

    check_add_gradient_pred(c, width);
    report("add_gradient_pred");
}
