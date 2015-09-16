/*
 * Copyright (c) 2015 Ronald S. Bultje <rsbultje@gmail.com>
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
#include "libavcodec/vp9dsp.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"

static const uint32_t pixel_mask[3] = { 0xffffffff, 0x03ff03ff, 0x0fff0fff };
#define SIZEOF_PIXEL ((bit_depth + 7) / 8)

#define randomize_buffers()                                        \
    do {                                                           \
        uint32_t mask = pixel_mask[(bit_depth - 8) >> 1];          \
        int k;                                                     \
        for (k = -4;  k < SIZEOF_PIXEL * FFMAX(8, size); k += 4) { \
            uint32_t r = rnd() & mask;                             \
            AV_WN32A(a + k, r);                                    \
        }                                                          \
        for (k = 0; k < size * SIZEOF_PIXEL; k += 4) {             \
            uint32_t r = rnd() & mask;                             \
            AV_WN32A(l + k, r);                                    \
        }                                                          \
    } while (0)

static void check_ipred(void)
{
    LOCAL_ALIGNED_32(uint8_t, a_buf, [64 * 2]);
    uint8_t *a = &a_buf[32 * 2];
    LOCAL_ALIGNED_32(uint8_t, l, [32 * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst0, [32 * 32 * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst1, [32 * 32 * 2]);
    VP9DSPContext dsp;
    int tx, mode, bit_depth;
    declare_func(void, uint8_t *dst, ptrdiff_t stride,
                 const uint8_t *left, const uint8_t *top);
    static const char *const mode_names[N_INTRA_PRED_MODES] = {
        [VERT_PRED] = "vert",
        [HOR_PRED] = "hor",
        [DC_PRED] = "dc",
        [DIAG_DOWN_LEFT_PRED] = "diag_downleft",
        [DIAG_DOWN_RIGHT_PRED] = "diag_downright",
        [VERT_RIGHT_PRED] = "vert_right",
        [HOR_DOWN_PRED] = "hor_down",
        [VERT_LEFT_PRED] = "vert_left",
        [HOR_UP_PRED] = "hor_up",
        [TM_VP8_PRED] = "tm",
        [LEFT_DC_PRED] = "dc_left",
        [TOP_DC_PRED] = "dc_top",
        [DC_128_PRED] = "dc_128",
        [DC_127_PRED] = "dc_127",
        [DC_129_PRED] = "dc_129",
    };

    for (bit_depth = 8; bit_depth <= 12; bit_depth += 2) {
        ff_vp9dsp_init(&dsp, bit_depth, 0);
        for (tx = 0; tx < 4; tx++) {
            int size = 4 << tx;

            for (mode = 0; mode < N_INTRA_PRED_MODES; mode++) {
                if (check_func(dsp.intra_pred[tx][mode], "vp9_%s_%dx%d_%dbpp",
                               mode_names[mode], size, size, bit_depth)) {
                    randomize_buffers();
                    call_ref(dst0, size * SIZEOF_PIXEL, l, a);
                    call_new(dst1, size * SIZEOF_PIXEL, l, a);
                    if (memcmp(dst0, dst1, size * size * SIZEOF_PIXEL))
                        fail();
                    bench_new(dst1, size * SIZEOF_PIXEL,l, a);
                }
            }
        }
    }
    report("ipred");
}

#undef randomize_buffers

#define DST_BUF_SIZE (size * size * SIZEOF_PIXEL)
#define SRC_BUF_STRIDE 72
#define SRC_BUF_SIZE ((size + 7) * SRC_BUF_STRIDE * SIZEOF_PIXEL)
#define src (buf + 3 * SIZEOF_PIXEL * (SRC_BUF_STRIDE + 1))

#define randomize_buffers()                               \
    do {                                                  \
        uint32_t mask = pixel_mask[(bit_depth - 8) >> 1]; \
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
    LOCAL_ALIGNED_32(uint8_t, buf, [72 * 72 * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst0, [64 * 64 * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst1, [64 * 64 * 2]);
    VP9DSPContext dsp;
    int op, hsize, bit_depth, filter, dx, dy;
    declare_func(void, uint8_t *dst, ptrdiff_t dst_stride,
                 const uint8_t *ref, ptrdiff_t ref_stride,
                 int h, int mx, int my);
    static const char *const filter_names[4] = {
        "8tap_smooth", "8tap_regular", "8tap_sharp", "bilin"
    };
    static const char *const subpel_names[2][2] = { { "", "h" }, { "v", "hv" } };
    static const char *const op_names[2] = { "put", "avg" };
    char str[256];

    for (op = 0; op < 2; op++) {
        for (bit_depth = 8; bit_depth <= 12; bit_depth += 2) {
            ff_vp9dsp_init(&dsp, bit_depth, 0);
            for (hsize = 0; hsize < 5; hsize++) {
                int size = 64 >> hsize;

                for (filter = 0; filter < 4; filter++) {
                    for (dx = 0; dx < 2; dx++) {
                        for (dy = 0; dy < 2; dy++) {
                            if (dx || dy) {
                                snprintf(str, sizeof(str),
                                         "%s_%s_%d%s", op_names[op],
                                         filter_names[filter], size,
                                         subpel_names[dy][dx]);
                            } else {
                                snprintf(str, sizeof(str),
                                         "%s%d", op_names[op], size);
                            }
                            if (check_func(dsp.mc[hsize][filter][op][dx][dy],
                                           "vp9_%s_%dbpp", str, bit_depth)) {
                                int mx = dx ? 1 + (rnd() % 14) : 0;
                                int my = dy ? 1 + (rnd() % 14) : 0;
                                randomize_buffers();
                                call_ref(dst0, size * SIZEOF_PIXEL,
                                         src, SRC_BUF_STRIDE * SIZEOF_PIXEL,
                                         size, mx, my);
                                call_new(dst1, size * SIZEOF_PIXEL,
                                         src, SRC_BUF_STRIDE * SIZEOF_PIXEL,
                                         size, mx, my);
                                if (memcmp(dst0, dst1, DST_BUF_SIZE))
                                    fail();

                                // simd implementations for each filter of subpel
                                // functions are identical
                                if (filter >= 1 && filter <= 2) continue;
                                // 10/12 bpp for bilin are identical
                                if (bit_depth == 12 && filter == 3) continue;

                                bench_new(dst1, size * SIZEOF_PIXEL,
                                          src, SRC_BUF_STRIDE * SIZEOF_PIXEL,
                                          size, mx, my);
                            }
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
    check_ipred();
    check_mc();
}
