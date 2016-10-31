/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"
#include "libavutil/attributes.h"
#include "huffyuvencdsp.h"
#include "mathops.h"

// 0x7f7f7f7f or 0x7f7f7f7f7f7f7f7f or whatever, depending on the cpu's native arithmetic size
#define pb_7f (~0UL / 255 * 0x7f)
#define pb_80 (~0UL / 255 * 0x80)

static void diff_bytes_c(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, intptr_t w)
{
    long i;

#if !HAVE_FAST_UNALIGNED
    if (((long)src1 | (long)src2) & (sizeof(long) - 1)) {
        for (i = 0; i + 7 < w; i += 8) {
            dst[i + 0] = src1[i + 0] - src2[i + 0];
            dst[i + 1] = src1[i + 1] - src2[i + 1];
            dst[i + 2] = src1[i + 2] - src2[i + 2];
            dst[i + 3] = src1[i + 3] - src2[i + 3];
            dst[i + 4] = src1[i + 4] - src2[i + 4];
            dst[i + 5] = src1[i + 5] - src2[i + 5];
            dst[i + 6] = src1[i + 6] - src2[i + 6];
            dst[i + 7] = src1[i + 7] - src2[i + 7];
        }
    } else
#endif
    for (i = 0; i <= w - (int) sizeof(long); i += sizeof(long)) {
        long a = *(long *) (src1 + i);
        long b = *(long *) (src2 + i);
        *(long *) (dst + i) = ((a | pb_80) - (b & pb_7f)) ^
                              ((a ^ b ^ pb_80) & pb_80);
    }
    for (; i < w; i++)
        dst[i + 0] = src1[i + 0] - src2[i + 0];
}

static void sub_hfyu_median_pred_c(uint8_t *dst, const uint8_t *src1,
                                   const uint8_t *src2, intptr_t w,
                                   int *left, int *left_top)
{
    int i;
    uint8_t l, lt;

    l  = *left;
    lt = *left_top;

    for (i = 0; i < w; i++) {
        const int pred = mid_pred(l, src1[i], (l + src1[i] - lt) & 0xFF);
        lt     = src1[i];
        l      = src2[i];
        dst[i] = l - pred;
    }

    *left     = l;
    *left_top = lt;
}

av_cold void ff_huffyuvencdsp_init(HuffYUVEncDSPContext *c)
{
    c->diff_bytes           = diff_bytes_c;
    c->sub_hfyu_median_pred = sub_hfyu_median_pred_c;

    if (ARCH_X86)
        ff_huffyuvencdsp_init_x86(c);
}
