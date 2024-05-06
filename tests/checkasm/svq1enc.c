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

#include "libavutil/mem_internal.h"

#include "libavcodec/svq1encdsp.h"

#include "checkasm.h"

#define BUF_SIZE 1024
#define MIN_VAL (-255 - 5 * 127)
#define MAX_VAL ( 255 + 5 * 128)

#define randomize(buf, len) \
    do { \
        for (int i = 0; i < len; i++) \
            buf[i] = ((rnd() % (MAX_VAL - MIN_VAL + 1)) + MIN_VAL); \
    } while (0)

static void test_ssd_int8_vs_int16(SVQ1EncDSPContext *s) {
    declare_func(int, const int8_t *pix1, const int16_t *pix2, intptr_t size);

    int r1, r2;

    if (check_func(s->ssd_int8_vs_int16, "ssd_int8_vs_int16")) {
        LOCAL_ALIGNED_4(int8_t, p1, [BUF_SIZE]);
        LOCAL_ALIGNED_16(int16_t, p2, [BUF_SIZE]);

        randomize(p1, BUF_SIZE);
        randomize(p2, BUF_SIZE);

        r1 = call_ref(p1, p2, BUF_SIZE);
        r2 = call_new(p1, p2, BUF_SIZE);

        if (r1 != r2) {
            fail();
        }

        bench_new(p1, p2, BUF_SIZE);
    }

    report("ssd_int8_vs_int16");

}

void checkasm_check_svq1enc(void)
{
    SVQ1EncDSPContext s = { 0 };
    ff_svq1enc_init(&s);

    test_ssd_int8_vs_int16(&s);
}
