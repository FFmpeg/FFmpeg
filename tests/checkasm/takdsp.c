/*
 * Copyright (c) 2023 Institue of Software Chinese Academy of Sciences (ISCAS).
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

#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"

#include "libavcodec/takdsp.h"

#include "checkasm.h"

#define randomize(buf, len) \
    do { \
        for (int i = 0; i < len; i++) \
            buf[i] = rnd(); \
    } while (0)

static void test_decorrelate_ls(TAKDSPContext *s) {
#define BUF_SIZE 1024
    declare_func(void, int32_t *, int32_t *, int);

    if (check_func(s->decorrelate_ls, "decorrelate_ls")) {
        LOCAL_ALIGNED_32(int32_t, p1, [BUF_SIZE]);
        LOCAL_ALIGNED_32(int32_t, p2, [BUF_SIZE]);
        LOCAL_ALIGNED_32(int32_t, p2_2, [BUF_SIZE]);

        randomize(p1, BUF_SIZE);
        randomize(p2, BUF_SIZE);
        memcpy(p2_2, p2, BUF_SIZE);

        call_ref(p1, p2, BUF_SIZE);
        call_new(p1, p2_2, BUF_SIZE);

        if (memcmp(p2, p2_2, BUF_SIZE) != 0){
            fail();
        }

        bench_new(p1, p2, BUF_SIZE);
    }

    report("decorrelate_ls");
}

static void test_decorrelate_sr(TAKDSPContext *s) {
#define BUF_SIZE 1024
    declare_func(void, int32_t *, int32_t *, int);

    if (check_func(s->decorrelate_sr, "decorrelate_sr")) {
        LOCAL_ALIGNED_32(int32_t, p1, [BUF_SIZE]);
        LOCAL_ALIGNED_32(int32_t, p2, [BUF_SIZE]);
        LOCAL_ALIGNED_32(int32_t, p2_2, [BUF_SIZE]);

        randomize(p1, BUF_SIZE);
        randomize(p2, BUF_SIZE);
        memcpy(p2_2, p2, BUF_SIZE);

        call_ref(p1, p2, BUF_SIZE);
        call_new(p1, p2_2, BUF_SIZE);

        if (memcmp(p2, p2_2, BUF_SIZE) != 0){
            fail();
        }

        bench_new(p1, p2, BUF_SIZE);
    }

    report("decorrelate_sr");
}

void checkasm_check_takdsp(void)
{
    TAKDSPContext s = { 0 };
    ff_takdsp_init(&s);

    test_decorrelate_ls(&s);
    test_decorrelate_sr(&s);
}
