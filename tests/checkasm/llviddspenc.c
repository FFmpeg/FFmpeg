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

#define randomize_buffers(buf, size)      \
    do {                                  \
        int j;                            \
        for (j = 0; j < size; j+=4)       \
            AV_WN32(buf + j, rnd());      \
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

static void check_sub_left_pred(LLVidEncDSPContext *c)
{
    int i;
    LOCAL_ALIGNED_32(uint8_t, dst0, [MAX_STRIDE * MAX_HEIGHT]);
    LOCAL_ALIGNED_32(uint8_t, dst1, [MAX_STRIDE * MAX_HEIGHT]);
    LOCAL_ALIGNED_32(uint8_t, src0, [MAX_STRIDE * MAX_HEIGHT]);
    LOCAL_ALIGNED_32(uint8_t, src1, [MAX_STRIDE * MAX_HEIGHT]);

    declare_func(void, uint8_t *dst, const uint8_t *src,
                 ptrdiff_t stride, ptrdiff_t width, int height);

    memset(dst0, 0, MAX_STRIDE * MAX_HEIGHT);
    memset(dst1, 0, MAX_STRIDE * MAX_HEIGHT);
    randomize_buffers(src0, MAX_STRIDE * MAX_HEIGHT);
    memcpy(src1, src0, MAX_STRIDE * MAX_HEIGHT);

    if (check_func(c->sub_left_predict, "sub_left_predict")) {
        for (i = 0; i < 5; i ++) {
            call_ref(dst0, src0, planes[i].s, planes[i].w, planes[i].h);
            call_new(dst1, src1, planes[i].s, planes[i].w, planes[i].h);
            if (memcmp(dst0, dst1, planes[i].w * planes[i].h))
                fail();
            break;
        }
        bench_new(dst1, src0, planes[4].s, planes[4].w, planes[4].h);
    }
}

void checkasm_check_llviddspenc(void)
{
    LLVidEncDSPContext c;
    ff_llvidencdsp_init(&c);

    check_diff_bytes(&c);
    report("diff_bytes");

    check_sub_left_pred(&c);
    report("sub_left_predict");
}
