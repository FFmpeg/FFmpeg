/*
 * Copyright (c) 2015 James Almer
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
#include "libavcodec/alacdsp.h"
#include "libavcodec/mathops.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/mem_internal.h"

#define BUF_SIZE 256
#define MAX_CHANNELS 2

#define randomize_buffers()                           \
    do {                                              \
        int i;                                        \
        for (i = 0; i < BUF_SIZE*MAX_CHANNELS; i++) { \
            int32_t r = sign_extend(rnd(), 24);       \
            ref_buf[i] = r;                           \
            new_buf[i] = r;                           \
        }                                             \
    } while (0)

static void check_decorrelate_stereo(void)
{
    LOCAL_ALIGNED_16(int32_t, ref_buf, [BUF_SIZE*MAX_CHANNELS]);
    LOCAL_ALIGNED_16(int32_t, new_buf, [BUF_SIZE*MAX_CHANNELS]);
    int32_t *ref[2] = { &ref_buf[BUF_SIZE*0], &ref_buf[BUF_SIZE*1] };
    int32_t *new[2] = { &new_buf[BUF_SIZE*0], &new_buf[BUF_SIZE*1] };
    ALACDSPContext c;

    ff_alacdsp_init(&c);
    if (check_func(c.decorrelate_stereo, "alac_decorrelate_stereo")) {
        int len    = (rnd() & 0xFF) + 1;
        int shift  =  rnd() & 0x1F;
        int weight =  rnd() & 0xFF;
        declare_func(void, int32_t *buf[2], int len, int shift, int weight);

        randomize_buffers();
        call_ref(ref, len, shift, weight);
        call_new(new, len, shift, weight);
        if (memcmp(ref[0], new[0], len * sizeof(int32_t)) ||
            memcmp(ref[1], new[1], len * sizeof(int32_t)))
            fail();
        bench_new(new, BUF_SIZE, shift, weight);
    }

    report("decorrelate_stereo");
}

#undef randomize_buffers
#define randomize_buffers()                           \
    do {                                              \
        int i, j;                                     \
        for (i = 0; i < BUF_SIZE; i++) {              \
            for (j = 0; j < ch; j++) {                \
                int32_t r = sign_extend(rnd(), 24);   \
                ref[j][i] = r;                        \
                new[j][i] = r;                        \
                r = rnd() & 0xFF;                     \
                ref_ebb[j][i] = r;                    \
                new_ebb[j][i] = r;                    \
            }                                         \
        }                                             \
    } while (0)

static void check_append_extra_bits(void)
{
    LOCAL_ALIGNED_16(int32_t, ref_buf, [BUF_SIZE*MAX_CHANNELS*2]);
    LOCAL_ALIGNED_16(int32_t, new_buf, [BUF_SIZE*MAX_CHANNELS*2]);
    int32_t *ref[2]     = { &ref_buf[BUF_SIZE*0], &ref_buf[BUF_SIZE*1] };
    int32_t *new[2]     = { &new_buf[BUF_SIZE*0], &new_buf[BUF_SIZE*1] };
    int32_t *ref_ebb[2] = { &ref_buf[BUF_SIZE*2], &ref_buf[BUF_SIZE*3] };
    int32_t *new_ebb[2] = { &new_buf[BUF_SIZE*2], &new_buf[BUF_SIZE*3] };
    ALACDSPContext c;
    static const char * const channels[2] = { "mono", "stereo" };
    int ch;

    ff_alacdsp_init(&c);
    for (ch = 1; ch <= 2; ch++) {
        if (check_func(c.append_extra_bits[ch-1], "alac_append_extra_bits_%s", channels[ch-1])) {
            int len    = (rnd() & 0xFF) + 1;
            declare_func(void, int32_t *buf[2], int32_t *ebb[2], int ebits, int ch, int len);

            randomize_buffers();
            call_ref(ref, ref_ebb, 8, ch, len);
            call_new(new, new_ebb, 8, ch, len);
            if (            memcmp(ref[0], new[0], len * sizeof(int32_t)) ||
                (ch == 2 && memcmp(ref[1], new[1], len * sizeof(int32_t))))
                fail();
            bench_new(new, new_ebb, 8, ch, BUF_SIZE);
        }
    }

    report("append_extra_bits");
}

void checkasm_check_alacdsp(void)
{
    check_decorrelate_stereo();
    check_append_extra_bits();
}
