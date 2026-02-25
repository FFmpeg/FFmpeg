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

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "checkasm.h"
#include "libavcodec/huffyuvencdsp.h"
#include "libavutil/cpu.h"
#include "libavutil/macros.h"
#include "libavutil/mem_internal.h"

enum {
    MAX_WIDTH = 4096,   ///< maximum test width, must be a power of two smaller than the maximum alignment
};

#define randomize_buffers(buf, size, mask) \
    do {                                   \
        for (size_t j = 0; j < size; ++j)  \
            buf[j] = rnd() & mask;         \
    } while (0)


static void check_sub_hfyu_median_pred_int16(const char *aligned, unsigned width)
{
    static const int bpps[] = { 9, 16, };
    HuffYUVEncDSPContext c;

    declare_func_emms(AV_CPU_FLAG_MMXEXT, void, uint16_t *dst, const uint16_t *src1,
                      const uint16_t *src2, unsigned mask, int w, int *left, int *left_top);

    for (size_t i = 0; i < FF_ARRAY_ELEMS(bpps); ++i) {
        const int bpp = bpps[i];

        ff_huffyuvencdsp_init(&c, bpp);

        if (check_func(c.sub_hfyu_median_pred_int16, "sub_hfyu_median_pred_int16_%dbpp%s", bpp, aligned)) {
            DECLARE_ALIGNED(32, uint16_t, dst0)[MAX_WIDTH];
            DECLARE_ALIGNED(32, uint16_t, dst1)[MAX_WIDTH];
            uint16_t src1[MAX_WIDTH];
            uint16_t src2[MAX_WIDTH];
            const unsigned mask = (1 << bpp) - 1;
            int l1 = rnd() & mask, lt1 = rnd() & mask, l2 = l1, lt2 = lt1;

            randomize_buffers(src1, width, mask);
            randomize_buffers(src2, width, mask);

            call_ref(dst0, src1, src2, mask, width, &l1, &lt1);
            call_new(dst1, src1, src2, mask, width, &l2, &lt2);
            if (l1 != l2 || lt1 != lt2 || memcmp(dst0, dst1, width * sizeof(dst0[0])))
                fail();
            bench_new(dst1, src1, src2, mask, width, &l2, &lt2);
        }
    }
}

void checkasm_check_huffyuvencdsp(void)
{
    static unsigned width = 0;

    if (!width) {
        width = rnd() % MAX_WIDTH;
        width = width ? width : 1;
    }

    const size_t align = av_cpu_max_align();

    check_sub_hfyu_median_pred_int16("_aligned", FFALIGN(width, align / sizeof(uint16_t)));
    report("sub_hfyu_median_pred_int16_aligned");

    check_sub_hfyu_median_pred_int16("", width);
    report("sub_hfyu_median_pred_int16");
}
