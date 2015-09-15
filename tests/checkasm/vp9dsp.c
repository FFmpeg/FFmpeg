/*
 * Copyright (c) 2015 Ronald S. Bultje <rsbultje@gmail.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Libav; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>

#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"

#include "libavcodec/vp9.h"

#include "checkasm.h"

static const uint32_t pixel_mask[3] = { 0xffffffff, 0x03ff03ff, 0x0fff0fff };

#define BIT_DEPTH 8
#define SIZEOF_PIXEL ((BIT_DEPTH + 7) / 8)
#define DST_BUF_SIZE (size * size * SIZEOF_PIXEL)
#define SRC_BUF_STRIDE 72
#define SRC_BUF_SIZE ((size + 7) * SRC_BUF_STRIDE * SIZEOF_PIXEL)
#define src (buf + 3 * SIZEOF_PIXEL * (SRC_BUF_STRIDE + 1))

#define randomize_buffers()                               \
    do {                                                  \
        uint32_t mask = pixel_mask[(BIT_DEPTH - 8) >> 1]; \
        int k;                                            \
        for (k = 0; k < SRC_BUF_SIZE; k += 4) {           \
            uint32_t r = rnd() & mask;                    \
            AV_WN32A(buf + k, r);                         \
        }                                                 \
        if (op == 1) {                                    \
            for (k = 0; k < DST_BUF_SIZE; k += 4) {       \
                uint32_t r = rnd() & mask;                \
                AV_WN32A(dst0 + k, r);                    \
                AV_WN32A(dst1 + k, r);                    \
            }                                             \
        }                                                 \
    } while (0)

static void check_mc(void)
{
    static const char *const filter_names[4] = {
        "8tap_smooth", "8tap_regular", "8tap_sharp", "bilin"
    };
    static const char *const subpel_names[2][2] = { { "", "h" }, { "v", "hv" } };
    static const char *const op_names[2] = { "put", "avg" };

    LOCAL_ALIGNED_32(uint8_t, buf,  [72 * 72 * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst0, [64 * 64 * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst1, [64 * 64 * 2]);
    char str[256];
    VP9DSPContext dsp;
    int op, hsize, filter, dx, dy;

    declare_func_emms(AV_CPU_FLAG_MMX | AV_CPU_FLAG_MMXEXT,
                      void, uint8_t *dst, const uint8_t *ref,
                      ptrdiff_t dst_stride, ptrdiff_t ref_stride,
                      int h, int mx, int my);

    for (op = 0; op < 2; op++) {
        ff_vp9dsp_init(&dsp);
        for (hsize = 0; hsize < 5; hsize++) {
            int size = 64 >> hsize;

            for (filter = 0; filter < 4; filter++) {
                for (dx = 0; dx < 2; dx++) {
                    for (dy = 0; dy < 2; dy++) {
                        if (dx || dy) {
                            snprintf(str, sizeof(str), "%s_%s_%d%s", op_names[op],
                                     filter_names[filter], size,
                                     subpel_names[dy][dx]);
                        } else {
                            snprintf(str, sizeof(str), "%s%d", op_names[op], size);
                        }
                        if (check_func(dsp.mc[hsize][filter][op][dx][dy],
                                       "vp9_%s", str)) {
                            int mx = dx ? 1 + (rnd() % 14) : 0;
                            int my = dy ? 1 + (rnd() % 14) : 0;
                            randomize_buffers();
                            call_ref(dst0, src,
                                     size * SIZEOF_PIXEL,
                                     SRC_BUF_STRIDE * SIZEOF_PIXEL,
                                     size, mx, my);
                            call_new(dst1, src,
                                     size * SIZEOF_PIXEL,
                                     SRC_BUF_STRIDE * SIZEOF_PIXEL,
                                     size, mx, my);
                            if (memcmp(dst0, dst1, DST_BUF_SIZE))
                                fail();

                            // SIMD implementations for each filter of subpel
                            // functions are identical
                            if (filter >= 1 && filter <= 2) continue;

                            bench_new(dst1, src, size * SIZEOF_PIXEL,
                                      SRC_BUF_STRIDE * SIZEOF_PIXEL,
                                      size, mx, my);
                        }
                    }
                }
            }
        }
    }
    report("mc");
}

void checkasm_check_vp9dsp(void)
{
    check_mc();
}
