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
#include "libavutil/intreadwrite.h"
#include "lossless_videoencdsp.h"
#include "mathops.h"

#if HAVE_FAST_64BIT
typedef uint64_t uint_native;
#define READ   AV_RN64
#define READA  AV_RN64A
#define WRITEA AV_WN64A
#else
typedef uint32_t uint_native;
#define READ   AV_RN32
#define READA  AV_RN32A
#define WRITEA AV_WN32A
#endif
// 0x7f7f7f7f or 0x7f7f7f7f7f7f7f7f or whatever, depending on the cpu's native arithmetic size
#define pb_7f (~(uint_native)0 / 255 * 0x7f)
#define pb_80 (~(uint_native)0 / 255 * 0x80)

static void diff_bytes_c(uint8_t *dst, const uint8_t *src1, const uint8_t *src2, intptr_t w)
{
    long i;

#if !HAVE_FAST_UNALIGNED
    if (((uintptr_t)src1 | (uintptr_t)src2) & (sizeof(uint_native) - 1)) {
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
    for (i = 0; i <= w - (int) sizeof(uint_native); i += sizeof(uint_native)) {
        uint_native a = READA(src1 + i);
        uint_native b = READ(src2 + i);
        WRITEA(dst + i, ((a | pb_80) - (b & pb_7f)) ^ ((a ^ b ^ pb_80) & pb_80));
    }
    for (; i < w; i++)
        dst[i + 0] = src1[i + 0] - src2[i + 0];
}

static void sub_median_pred_c(uint8_t *dst, const uint8_t *src1,
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

static void sub_left_predict_c(uint8_t *dst, const uint8_t *src,
                               ptrdiff_t stride, ptrdiff_t width, int height)
{
    int i, j;
    uint8_t prev = 0x80; /* Set the initial value */
    for (j = 0; j < height; j++) {
        for (i = 0; i < width; i++) {
            *dst++ = src[i] - prev;
            prev   = src[i];
        }
        src += stride;
    }
}

av_cold void ff_llvidencdsp_init(LLVidEncDSPContext *c)
{
    c->diff_bytes      = diff_bytes_c;
    c->sub_median_pred = sub_median_pred_c;
    c->sub_left_predict = sub_left_predict_c;

#if ARCH_RISCV
    ff_llvidencdsp_init_riscv(c);
#elif ARCH_X86
    ff_llvidencdsp_init_x86(c);
#endif
}
