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
#include "libavcodec/mathops.h"

#include "checkasm.h"

#define randomize(buf, len) \
    do { \
        for (int i = 0; i < len; i++) \
            buf[i] = rnd(); \
    } while (0)

#define BUF_SIZE 1024

static void test_decorrelate_ls(TAKDSPContext *s) {
    declare_func(void, const int32_t *, int32_t *, int);

    if (check_func(s->decorrelate_ls, "decorrelate_ls")) {
        LOCAL_ALIGNED_32(int32_t, p1, [BUF_SIZE]);
        LOCAL_ALIGNED_32(int32_t, p2, [BUF_SIZE]);
        LOCAL_ALIGNED_32(int32_t, p2_2, [BUF_SIZE]);

        randomize(p1, BUF_SIZE);
        randomize(p2, BUF_SIZE);
        memcpy(p2_2, p2, BUF_SIZE * sizeof(*p2));

        call_ref(p1, p2, BUF_SIZE);
        call_new(p1, p2_2, BUF_SIZE);

        if (memcmp(p2, p2_2, BUF_SIZE * sizeof(*p2)) != 0) {
            fail();
        }

        bench_new(p1, p2, BUF_SIZE);
    }

    report("decorrelate_ls");
}

static void test_decorrelate_sr(TAKDSPContext *s) {
    declare_func(void, int32_t *, const int32_t *, int);

    if (check_func(s->decorrelate_sr, "decorrelate_sr")) {
        LOCAL_ALIGNED_32(int32_t, p1, [BUF_SIZE]);
        LOCAL_ALIGNED_32(int32_t, p1_2, [BUF_SIZE]);
        LOCAL_ALIGNED_32(int32_t, p2, [BUF_SIZE]);

        randomize(p1, BUF_SIZE);
        memcpy(p1_2, p1, BUF_SIZE * sizeof(*p1));
        randomize(p2, BUF_SIZE);

        call_ref(p1, p2, BUF_SIZE);
        call_new(p1_2, p2, BUF_SIZE);

        if (memcmp(p1, p1_2, BUF_SIZE * sizeof(*p1)) != 0) {
            fail();
        }

        bench_new(p1, p2, BUF_SIZE);
    }

    report("decorrelate_sr");
}

static void test_decorrelate_sm(TAKDSPContext *s) {
    declare_func(void, int32_t *, int32_t *, int);

    if (check_func(s->decorrelate_sm, "decorrelate_sm")) {
        LOCAL_ALIGNED_32(int32_t, p1, [BUF_SIZE]);
        LOCAL_ALIGNED_32(int32_t, p1_2, [BUF_SIZE]);
        LOCAL_ALIGNED_32(int32_t, p2, [BUF_SIZE]);
        LOCAL_ALIGNED_32(int32_t, p2_2, [BUF_SIZE]);

        randomize(p1, BUF_SIZE);
        memcpy(p1_2, p1, BUF_SIZE * sizeof(*p1));
        randomize(p2, BUF_SIZE);
        memcpy(p2_2, p2, BUF_SIZE * sizeof(*p2));

        call_ref(p1, p2, BUF_SIZE);
        call_new(p1_2, p2_2, BUF_SIZE);

        if (memcmp(p1, p1_2, BUF_SIZE * sizeof(*p1)) != 0 ||
            memcmp(p2, p2_2, BUF_SIZE * sizeof(*p2)) != 0) {
            fail();
        }

        bench_new(p1, p2, BUF_SIZE);
    }

    report("decorrelate_sm");
}

static void test_decorrelate_sf(TAKDSPContext *s) {
    declare_func(void, int32_t *, const int32_t *, int, int, int);

    if (check_func(s->decorrelate_sf, "decorrelate_sf")) {
        LOCAL_ALIGNED_32(int32_t, p1, [BUF_SIZE]);
        LOCAL_ALIGNED_32(int32_t, p1_2, [BUF_SIZE]);
        LOCAL_ALIGNED_32(int32_t, p2, [BUF_SIZE]);
        int dshift, dfactor;

        randomize(p1, BUF_SIZE);
        memcpy(p1_2, p1, BUF_SIZE * sizeof(*p1));
        randomize(p2, BUF_SIZE);
        dshift = (rnd() & 0xF) + 1;
        dfactor = sign_extend(rnd(), 10);

        call_ref(p1, p2, BUF_SIZE, dshift, dfactor);
        call_new(p1_2, p2, BUF_SIZE, dshift, dfactor);

        if (memcmp(p1, p1_2, BUF_SIZE * sizeof(*p1)) != 0) {
            fail();
        }

        bench_new(p1, p2, BUF_SIZE, dshift, dfactor);
    }

    report("decorrelate_sf");
}

void checkasm_check_takdsp(void)
{
    TAKDSPContext s = { 0 };
    ff_takdsp_init(&s);

    test_decorrelate_ls(&s);
    test_decorrelate_sr(&s);
    test_decorrelate_sm(&s);
    test_decorrelate_sf(&s);
}
