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
#include "libavutil/mem_internal.h"

#include "libavcodec/lossless_videoencdsp.h"

#include "checkasm.h"

#define randomize_buffers(buf, size)              \
    do {                                          \
        for (size_t j = 0; j < size & ~3; j += 4) \
            AV_WN32(buf + j, rnd());              \
        for (size_t j = 0; j < size; ++j)         \
            buf[j] = rnd();                       \
    } while (0)

static const struct {uint8_t w, h, s;} planes[] = {
    {16,16,16}, {21,23,25}, {32,17,48}, {15,128,16}, {128,127,128}
};

#define MAX_STRIDE 128
#define MAX_HEIGHT 127

static void check_diff_bytes(LLVidEncDSPContext *c)
{
    int i;
    LOCAL_ALIGNED_32(uint8_t, dst0, [MAX_STRIDE]);
    LOCAL_ALIGNED_32(uint8_t, dst1, [MAX_STRIDE]);
    LOCAL_ALIGNED_32(uint8_t, src0, [MAX_STRIDE]);
    LOCAL_ALIGNED_32(uint8_t, src1, [MAX_STRIDE]);
    LOCAL_ALIGNED_32(uint8_t, src2, [MAX_STRIDE]);
    LOCAL_ALIGNED_32(uint8_t, src3, [MAX_STRIDE]);

    declare_func(void, uint8_t *dst, const uint8_t *src1,
                 const uint8_t *src2, intptr_t w);

    memset(dst0, 0, MAX_STRIDE);
    memset(dst1, 0, MAX_STRIDE);
    randomize_buffers(src0, MAX_STRIDE);
    memcpy(src1, src0, MAX_STRIDE);
    randomize_buffers(src2, MAX_STRIDE);
    memcpy(src3, src2, MAX_STRIDE);

    if (check_func(c->diff_bytes, "diff_bytes")) {
        for (i = 0; i < 5; i ++) {
            call_ref(dst0, src0, src2, planes[i].w);
            call_new(dst1, src1, src3, planes[i].w);
            if (memcmp(dst0, dst1, planes[i].w))
                fail();
        }
        bench_new(dst1, src0, src2, planes[4].w);
    }
}

static void check_sub_median_pred(LLVidEncDSPContext *c)
{
    enum {
        BUF_SIZE = MAX_STRIDE + 15 /* to test misalignment */
    };
    uint8_t dst_ref[BUF_SIZE], dst_new[BUF_SIZE];
    uint8_t src1[BUF_SIZE], src2[BUF_SIZE];

    declare_func(void, uint8_t *dst, const uint8_t *src1,
                 const uint8_t *src2, intptr_t w,
                 int *left, int *left_top);

    if (check_func(c->sub_median_pred, "sub_median_pred")) {
        size_t width  = 1 + rnd() % MAX_STRIDE;
        size_t offset = rnd() & 0xF;
        int left_ref = rnd() & 0xFF, top_ref = rnd() & 0xFF;
        int left_new = left_ref, top_new = top_ref;

        memset(dst_ref, 0, sizeof(dst_ref));
        memset(dst_new, 0, sizeof(dst_new));

        randomize_buffers(src1, sizeof(src1));
        randomize_buffers(src2, sizeof(src2));

        call_ref(dst_ref + offset, src1 + offset, src2 + offset, width, &left_ref, &top_ref);
        call_new(dst_new + offset, src1 + offset, src2 + offset, width, &left_new, &top_new);
        if (left_new != left_ref || top_ref != top_new ||
            memcmp(dst_ref, dst_new, width + offset))
            fail();
        bench_new(dst_new, src1, src2, MAX_STRIDE, &left_new, &top_new);
    }
}

static void check_sub_left_pred(LLVidEncDSPContext *c)
{
    LOCAL_ALIGNED_32(uint8_t, dst0, [MAX_STRIDE * MAX_HEIGHT]);
    LOCAL_ALIGNED_32(uint8_t, dst1, [MAX_STRIDE * MAX_HEIGHT]);
    LOCAL_ALIGNED_32(uint8_t, src, [MAX_STRIDE * MAX_HEIGHT]);

    declare_func(void, uint8_t *dst, const uint8_t *src,
                 ptrdiff_t stride, ptrdiff_t width, int height);

    if (check_func(c->sub_left_predict, "sub_left_predict")) {
        randomize_buffers(src, MAX_STRIDE * MAX_HEIGHT);

        for (size_t i = 0; i < FF_ARRAY_ELEMS(planes); i ++) {
            memset(dst0, 0, MAX_STRIDE * MAX_HEIGHT);
            memset(dst1, 0, MAX_STRIDE * MAX_HEIGHT);
            call_ref(dst0, src, planes[i].s, planes[i].w, planes[i].h);
            call_new(dst1, src, planes[i].s, planes[i].w, planes[i].h);
            if (memcmp(dst0, dst1, planes[i].w * planes[i].h))
                fail();
        }
        bench_new(dst1, src, planes[4].s, planes[4].w, planes[4].h);
    }
}

void checkasm_check_llvidencdsp(void)
{
    LLVidEncDSPContext c;
    ff_llvidencdsp_init(&c);

    check_diff_bytes(&c);
    report("diff_bytes");

    check_sub_median_pred(&c);
    report("sub_median_pred");

    check_sub_left_pred(&c);
    report("sub_left_predict");
}
