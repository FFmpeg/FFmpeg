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

#include "checkasm.h"

#include "libavfilter/mpdecimate.h"
#include "libavutil/mem_internal.h"

#define STRIDE 32

static void check_sad16(int depth)
{
    LOCAL_ALIGNED_32(uint16_t, src1, [STRIDE * 9]);
    LOCAL_ALIGNED_32(uint16_t, src2, [STRIDE * 9]);
    const int mask = (1 << depth) - 1;

    declare_func(int, const uint8_t *src1, ptrdiff_t stride1,
                      const uint8_t *src2, ptrdiff_t stride2);

    for (int i = 0; i < STRIDE * 9; i++) {
        src1[i] = rnd() & mask;
        src2[i] = rnd() & mask;
    }
    if (depth == 16) {
        /* largest possible SAD */
        for (int i = 0; i < STRIDE * 9; i++) {
            src1[i] = 0;
            src2[i] = 0xffff;
        }
    }

    if (check_func(ff_mpdecimate_get_sad16_fn(), "sad_8x8_16_%d", depth)) {
        /* blocks are not aligned in the filter */
        for (int off = 0; off < 2; off++) {
            const uint8_t *p1 = (const uint8_t *)(src1 + off * 3);
            const uint8_t *p2 = (const uint8_t *)(src2 + off * 5);
            int ref = call_ref(p1, STRIDE * 2, p2, STRIDE * 2);
            int new = call_new(p1, STRIDE * 2, p2, STRIDE * 2);
            if (ref != new) {
                fprintf(stderr, "sad_8x8_16_%d: %d != %d\n", depth, ref, new);
                fail();
            }
        }
        bench_new((const uint8_t *)src1, STRIDE * 2,
                  (const uint8_t *)src2, STRIDE * 2);
    }
}

void checkasm_check_mpdecimate(void)
{
    static const int depths[] = { 10, 12, 15, 16 };

    for (int i = 0; i < FF_ARRAY_ELEMS(depths); i++)
        check_sad16(depths[i]);
    report("sad_8x8_16");
}
