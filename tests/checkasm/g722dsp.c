/*
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
#include "libavcodec/g722.h"
#include "libavcodec/g722dsp.h"
#include "libavcodec/mathops.h"

#define randomize_buffers()                             \
    do {                                                \
        int i;                                          \
        for (i = 0; i < PREV_SAMPLES_BUF_SIZE; i++) {   \
            src0[i] = src1[i] = sign_extend(rnd(), 16); \
        }                                               \
    } while (0)

static void check_qmf(void) {
    int16_t src0[PREV_SAMPLES_BUF_SIZE];
    int16_t src1[PREV_SAMPLES_BUF_SIZE];
    const int16_t *tmp0 = src0;
    const int16_t *tmp1 = src1;
    int dst0[2], dst1[2];
    int i;

    declare_func(void, const int16_t *prev_samples, int xout[2]);

    randomize_buffers();
    for (i = 0; i < PREV_SAMPLES_BUF_SIZE - 24; i++) {
        call_ref(tmp0++, dst0);
        call_new(tmp1++, dst1);
        if (memcmp(dst0, dst1, sizeof(dst0)))
            fail();
    }
    bench_new(src1, dst1);
}

void checkasm_check_g722dsp(void)
{
    G722DSPContext h;

    ff_g722dsp_init(&h);

    if (check_func(h.apply_qmf, "g722_apply_qmf"))
        check_qmf();

    report("apply_qmf");
}
