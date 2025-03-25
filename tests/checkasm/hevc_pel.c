/*
 * Copyright (c) 2015 Henrik Gramner
 * Copyright (c) 2021 Josh Dekker
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
#include "libavcodec/hevc/dsp.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"

static const uint32_t pixel_mask[] = { 0xffffffff, 0x01ff01ff, 0x03ff03ff, 0x07ff07ff, 0x0fff0fff };
static const uint32_t pixel_mask16[] = { 0x00ff00ff, 0x01ff01ff, 0x03ff03ff, 0x07ff07ff, 0x0fff0fff };
static const int sizes[] = { -1, 4, 6, 8, 12, 16, 24, 32, 48, 64 };
static const int weights[] = { 0, 128, 255, -1 };
static const int denoms[] = {0, 7, 12, -1 };
static const int offsets[] = {0, 255, -1 };

#define SIZEOF_PIXEL ((bit_depth + 7) / 8)
#define BUF_SIZE (2 * MAX_PB_SIZE * (2 * 4 + MAX_PB_SIZE))

#define randomize_buffers()                          \
    do {                                             \
        uint32_t mask = pixel_mask[bit_depth - 8];   \
        int k;                                       \
        for (k = 0; k < BUF_SIZE + SRC_EXTRA; k += 4) { \
            uint32_t r = rnd() & mask;               \
            AV_WN32A(buf0 + k, r);                   \
            AV_WN32A(buf1 + k, r);                   \
        }                                            \
    } while (0)

#define randomize_buffers_dst()                      \
    do {                                             \
        int k;                                       \
        for (k = 0; k < BUF_SIZE; k += 4) {          \
            uint32_t r = rnd();                      \
            AV_WN32A(dst0 + k, r);                   \
            AV_WN32A(dst1 + k, r);                   \
        }                                            \
    } while (0)

#define randomize_buffers_ref()                      \
    randomize_buffers();                             \
    do {                                             \
        uint32_t mask = pixel_mask16[bit_depth - 8]; \
        int k;                                       \
        for (k = 0; k < BUF_SIZE; k += 2) {          \
            uint32_t r = rnd() & mask;               \
            AV_WN32A(ref0 + k, r);                   \
            AV_WN32A(ref1 + k, r);                   \
        }                                            \
    } while (0)

#define src0 (buf0 + 2 * 4 * MAX_PB_SIZE) /* hevc qpel functions read data from negative src pointer offsets */
#define src1 (buf1 + 2 * 4 * MAX_PB_SIZE)

/* FIXME: Does the need for SRC_EXTRA for these tests indicate a bug? */
#define SRC_EXTRA 8

static void checkasm_check_hevc_qpel(void)
{
    LOCAL_ALIGNED_32(uint8_t, buf0, [BUF_SIZE + SRC_EXTRA]);
    LOCAL_ALIGNED_32(uint8_t, buf1, [BUF_SIZE + SRC_EXTRA]);
    LOCAL_ALIGNED_32(uint8_t, dst0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, dst1, [BUF_SIZE]);

    HEVCDSPContext h;
    int size, bit_depth, i, j;
    declare_func(void, int16_t *dst, const uint8_t *src, ptrdiff_t srcstride,
                 int height, intptr_t mx, intptr_t my, int width);

    for (bit_depth = 8; bit_depth <= 12; bit_depth++) {
        ff_hevc_dsp_init(&h, bit_depth);

        for (i = 0; i < 2; i++) {
            for (j = 0; j < 2; j++) {
                for (size = 1; size < 10; size++) {
                    const char *type;
                    switch ((j << 1) | i) {
                    case 0: type = "pel_pixels"; break; // 0 0
                    case 1: type = "qpel_h"; break; // 0 1
                    case 2: type = "qpel_v"; break; // 1 0
                    case 3: type = "qpel_hv"; break; // 1 1
                    }

                    if (check_func(h.put_hevc_qpel[size][j][i],
                                   "put_hevc_%s%d_%d", type, sizes[size], bit_depth)) {
                        int16_t *dstw0 = (int16_t *) dst0, *dstw1 = (int16_t *) dst1;
                        randomize_buffers();
                        randomize_buffers_dst();
                        call_ref(dstw0, src0, sizes[size] * SIZEOF_PIXEL, sizes[size], i, j, sizes[size]);
                        call_new(dstw1, src1, sizes[size] * SIZEOF_PIXEL, sizes[size], i, j, sizes[size]);
                        checkasm_check(int16_t, dstw0, MAX_PB_SIZE * sizeof(int16_t),
                                                dstw1, MAX_PB_SIZE * sizeof(int16_t),
                                                size[sizes], size[sizes], "dst");
                        bench_new(dstw1, src1, sizes[size] * SIZEOF_PIXEL, sizes[size], i, j, sizes[size]);
                    }
                }
            }
        }
    }
    report("qpel");
}

static void checkasm_check_hevc_qpel_uni(void)
{
    LOCAL_ALIGNED_32(uint8_t, buf0, [BUF_SIZE + SRC_EXTRA]);
    LOCAL_ALIGNED_32(uint8_t, buf1, [BUF_SIZE + SRC_EXTRA]);
    PIXEL_RECT(dst0, 64, 64);
    PIXEL_RECT(dst1, 64, 64);

    HEVCDSPContext h;
    int size, bit_depth, i, j;
    declare_func(void, uint8_t *dst, ptrdiff_t dststride, const uint8_t *src, ptrdiff_t srcstride,
                 int height, intptr_t mx, intptr_t my, int width);

    for (bit_depth = 8; bit_depth <= 12; bit_depth++) {
        ff_hevc_dsp_init(&h, bit_depth);

        for (i = 0; i < 2; i++) {
            for (j = 0; j < 2; j++) {
                for (size = 1; size < 10; size++) {
                    const char *type;
                    switch ((j << 1) | i) {
                    case 0: type = "pel_uni_pixels"; break; // 0 0
                    case 1: type = "qpel_uni_h"; break; // 0 1
                    case 2: type = "qpel_uni_v"; break; // 1 0
                    case 3: type = "qpel_uni_hv"; break; // 1 1
                    }

                    if (check_func(h.put_hevc_qpel_uni[size][j][i],
                                   "put_hevc_%s%d_%d", type, sizes[size], bit_depth)) {
                        randomize_buffers();
                        CLEAR_PIXEL_RECT(dst0);
                        CLEAR_PIXEL_RECT(dst1);
                        call_ref(dst0, dst0_stride,
                                 src0, sizes[size] * SIZEOF_PIXEL,
                                 sizes[size], i, j, sizes[size]);
                        call_new(dst1, dst1_stride,
                                 src1, sizes[size] * SIZEOF_PIXEL,
                                 sizes[size], i, j, sizes[size]);
                        checkasm_check_pixel_padded(dst0, dst0_stride,
                                                    dst1, dst1_stride,
                                                    size[sizes], size[sizes], "dst");
                        bench_new(dst1, dst1_stride,
                                  src1, sizes[size] * SIZEOF_PIXEL,
                                  sizes[size], i, j, sizes[size]);
                    }
                }
            }
        }
    }
    report("qpel_uni");
}

static void checkasm_check_hevc_qpel_uni_w(void)
{
    LOCAL_ALIGNED_32(uint8_t, buf0, [BUF_SIZE + SRC_EXTRA]);
    LOCAL_ALIGNED_32(uint8_t, buf1, [BUF_SIZE + SRC_EXTRA]);
    PIXEL_RECT(dst0, 64, 64);
    PIXEL_RECT(dst1, 64, 64);

    HEVCDSPContext h;
    int size, bit_depth, i, j;
    const int *denom, *wx, *ox;
    declare_func(void, uint8_t *dst, ptrdiff_t dststride, const uint8_t *src, ptrdiff_t srcstride,
                 int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width);

    for (bit_depth = 8; bit_depth <= 12; bit_depth++) {
        ff_hevc_dsp_init(&h, bit_depth);

        for (i = 0; i < 2; i++) {
            for (j = 0; j < 2; j++) {
                for (size = 1; size < 10; size++) {
                    const char *type;
                    switch ((j << 1) | i) {
                    case 0: type = "pel_uni_w_pixels"; break; // 0 0
                    case 1: type = "qpel_uni_w_h"; break; // 0 1
                    case 2: type = "qpel_uni_w_v"; break; // 1 0
                    case 3: type = "qpel_uni_w_hv"; break; // 1 1
                    }

                    if (check_func(h.put_hevc_qpel_uni_w[size][j][i],
                                   "put_hevc_%s%d_%d", type, sizes[size], bit_depth)) {
                        for (denom = denoms; *denom >= 0; denom++) {
                            for (wx = weights; *wx >= 0; wx++) {
                                for (ox = offsets; *ox >= 0; ox++) {
                                    randomize_buffers();
                                    CLEAR_PIXEL_RECT(dst0);
                                    CLEAR_PIXEL_RECT(dst1);
                                    call_ref(dst0, dst0_stride,
                                             src0, sizes[size] * SIZEOF_PIXEL,
                                             sizes[size], *denom, *wx, *ox, i, j, sizes[size]);
                                    call_new(dst1, dst1_stride,
                                             src1, sizes[size] * SIZEOF_PIXEL,
                                             sizes[size], *denom, *wx, *ox, i, j, sizes[size]);
                                    checkasm_check_pixel_padded(dst0, dst0_stride,
                                                                dst1, dst1_stride,
                                                                size[sizes], size[sizes], "dst");
                                    bench_new(dst1, dst1_stride,
                                              src1, sizes[size] * SIZEOF_PIXEL,
                                              sizes[size], *denom, *wx, *ox, i, j, sizes[size]);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    report("qpel_uni_w");
}

static void checkasm_check_hevc_qpel_bi(void)
{
    LOCAL_ALIGNED_32(uint8_t, buf0, [BUF_SIZE + SRC_EXTRA]);
    LOCAL_ALIGNED_32(uint8_t, buf1, [BUF_SIZE + SRC_EXTRA]);
    PIXEL_RECT(dst0, 64, 64);
    PIXEL_RECT(dst1, 64, 64);
    LOCAL_ALIGNED_32(int16_t, ref0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(int16_t, ref1, [BUF_SIZE]);

    HEVCDSPContext h;
    int size, bit_depth, i, j;
    declare_func(void, uint8_t *dst, ptrdiff_t dststride, const uint8_t *src, ptrdiff_t srcstride,
                 const int16_t *src2,
                 int height, intptr_t mx, intptr_t my, int width);

    for (bit_depth = 8; bit_depth <= 12; bit_depth++) {
        ff_hevc_dsp_init(&h, bit_depth);

        for (i = 0; i < 2; i++) {
            for (j = 0; j < 2; j++) {
                for (size = 1; size < 10; size++) {
                    const char *type;
                    switch ((j << 1) | i) {
                    case 0: type = "pel_bi_pixels"; break; // 0 0
                    case 1: type = "qpel_bi_h"; break; // 0 1
                    case 2: type = "qpel_bi_v"; break; // 1 0
                    case 3: type = "qpel_bi_hv"; break; // 1 1
                    }

                    if (check_func(h.put_hevc_qpel_bi[size][j][i],
                                   "put_hevc_%s%d_%d", type, sizes[size], bit_depth)) {
                        randomize_buffers_ref();
                        CLEAR_PIXEL_RECT(dst0);
                        CLEAR_PIXEL_RECT(dst1);
                        call_ref(dst0, dst0_stride,
                                 src0, sizes[size] * SIZEOF_PIXEL,
                                 ref0, sizes[size], i, j, sizes[size]);
                        call_new(dst1, dst1_stride,
                                 src1, sizes[size] * SIZEOF_PIXEL,
                                 ref1, sizes[size], i, j, sizes[size]);
                        checkasm_check_pixel_padded(dst0, dst0_stride,
                                                    dst1, dst1_stride,
                                                    size[sizes], size[sizes], "dst");
                        bench_new(dst1, dst1_stride,
                                  src1, sizes[size] * SIZEOF_PIXEL,
                                  ref1, sizes[size], i, j, sizes[size]);
                    }
                }
            }
        }
    }
    report("qpel_bi");
}

static void checkasm_check_hevc_qpel_bi_w(void)
{
    LOCAL_ALIGNED_32(uint8_t, buf0, [BUF_SIZE + SRC_EXTRA]);
    LOCAL_ALIGNED_32(uint8_t, buf1, [BUF_SIZE + SRC_EXTRA]);
    PIXEL_RECT(dst0, 64, 64);
    PIXEL_RECT(dst1, 64, 64);
    LOCAL_ALIGNED_32(int16_t, ref0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(int16_t, ref1, [BUF_SIZE]);

    HEVCDSPContext h;
    int size, bit_depth, i, j;
    const int *denom, *wx, *ox;
    declare_func(void, uint8_t *dst, ptrdiff_t dststride, const uint8_t *src, ptrdiff_t srcstride,
                 const int16_t *src2,
                 int height, int denom, int wx0, int wx1,
                 int ox0, int ox1, intptr_t mx, intptr_t my, int width);

    for (bit_depth = 8; bit_depth <= 12; bit_depth++) {
        ff_hevc_dsp_init(&h, bit_depth);

        for (i = 0; i < 2; i++) {
            for (j = 0; j < 2; j++) {
                for (size = 1; size < 10; size++) {
                    const char *type;
                    switch ((j << 1) | i) {
                    case 0: type = "pel_bi_w_pixels"; break; // 0 0
                    case 1: type = "qpel_bi_w_h"; break; // 0 1
                    case 2: type = "qpel_bi_w_v"; break; // 1 0
                    case 3: type = "qpel_bi_w_hv"; break; // 1 1
                    }

                    if (check_func(h.put_hevc_qpel_bi_w[size][j][i],
                                   "put_hevc_%s%d_%d", type, sizes[size], bit_depth)) {
                        for (denom = denoms; *denom >= 0; denom++) {
                            for (wx = weights; *wx >= 0; wx++) {
                                for (ox = offsets; *ox >= 0; ox++) {
                                    randomize_buffers_ref();
                                    CLEAR_PIXEL_RECT(dst0);
                                    CLEAR_PIXEL_RECT(dst1);
                                    call_ref(dst0, dst0_stride,
                                             src0, sizes[size] * SIZEOF_PIXEL,
                                             ref0, sizes[size], *denom, *wx, *wx, *ox, *ox, i, j, sizes[size]);
                                    call_new(dst1, dst1_stride,
                                             src1, sizes[size] * SIZEOF_PIXEL,
                                             ref1, sizes[size], *denom, *wx, *wx, *ox, *ox, i, j, sizes[size]);
                                    checkasm_check_pixel_padded(dst0, dst0_stride,
                                                                dst1, dst1_stride,
                                                                size[sizes], size[sizes], "dst");
                                    bench_new(dst1, dst1_stride,
                                              src1, sizes[size] * SIZEOF_PIXEL,
                                              ref1, sizes[size], *denom, *wx, *wx, *ox, *ox, i, j, sizes[size]);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    report("qpel_bi_w");
}

#undef SRC_EXTRA
#define SRC_EXTRA 0

static void checkasm_check_hevc_epel(void)
{
    LOCAL_ALIGNED_32(uint8_t, buf0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, buf1, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, dst0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, dst1, [BUF_SIZE]);

    HEVCDSPContext h;
    int size, bit_depth, i, j;
    declare_func(void, int16_t *dst, const uint8_t *src, ptrdiff_t srcstride,
                 int height, intptr_t mx, intptr_t my, int width);

    for (bit_depth = 8; bit_depth <= 12; bit_depth++) {
        ff_hevc_dsp_init(&h, bit_depth);

        for (i = 0; i < 2; i++) {
            for (j = 0; j < 2; j++) {
                for (size = 1; size < 10; size++) {
                    const char *type;
                    switch ((j << 1) | i) {
                    case 0: type = "pel_pixels"; break; // 0 0
                    case 1: type = "epel_h"; break; // 0 1
                    case 2: type = "epel_v"; break; // 1 0
                    case 3: type = "epel_hv"; break; // 1 1
                    }

                    if (check_func(h.put_hevc_epel[size][j][i],
                                   "put_hevc_%s%d_%d", type, sizes[size], bit_depth)) {
                        int16_t *dstw0 = (int16_t *) dst0, *dstw1 = (int16_t *) dst1;
                        randomize_buffers();
                        randomize_buffers_dst();
                        call_ref(dstw0, src0, sizes[size] * SIZEOF_PIXEL, sizes[size], i, j, sizes[size]);
                        call_new(dstw1, src1, sizes[size] * SIZEOF_PIXEL, sizes[size], i, j, sizes[size]);
                        checkasm_check(int16_t, dstw0, MAX_PB_SIZE * sizeof(int16_t),
                                                dstw1, MAX_PB_SIZE * sizeof(int16_t),
                                                size[sizes], size[sizes], "dst");
                        bench_new(dstw1, src1, sizes[size] * SIZEOF_PIXEL, sizes[size], i, j, sizes[size]);
                    }
                }
            }
        }
    }
    report("epel");
}

static void checkasm_check_hevc_epel_uni(void)
{
    LOCAL_ALIGNED_32(uint8_t, buf0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, buf1, [BUF_SIZE]);
    PIXEL_RECT(dst0, 64, 64);
    PIXEL_RECT(dst1, 64, 64);

    HEVCDSPContext h;
    int size, bit_depth, i, j;
    declare_func(void, uint8_t *dst, ptrdiff_t dststride, const uint8_t *src, ptrdiff_t srcstride,
                 int height, intptr_t mx, intptr_t my, int width);

    for (bit_depth = 8; bit_depth <= 12; bit_depth++) {
        ff_hevc_dsp_init(&h, bit_depth);

        for (i = 0; i < 2; i++) {
            for (j = 0; j < 2; j++) {
                for (size = 1; size < 10; size++) {
                    const char *type;
                    switch ((j << 1) | i) {
                    case 0: type = "pel_uni_pixels"; break; // 0 0
                    case 1: type = "epel_uni_h"; break; // 0 1
                    case 2: type = "epel_uni_v"; break; // 1 0
                    case 3: type = "epel_uni_hv"; break; // 1 1
                    }

                    if (check_func(h.put_hevc_epel_uni[size][j][i],
                                   "put_hevc_%s%d_%d", type, sizes[size], bit_depth)) {
                        randomize_buffers();
                        CLEAR_PIXEL_RECT(dst0);
                        CLEAR_PIXEL_RECT(dst1);
                        call_ref(dst0, dst0_stride,
                                 src0, sizes[size] * SIZEOF_PIXEL,
                                 sizes[size], i, j, sizes[size]);
                        call_new(dst1, dst1_stride,
                                 src1, sizes[size] * SIZEOF_PIXEL,
                                 sizes[size], i, j, sizes[size]);
                        checkasm_check_pixel_padded(dst0, dst0_stride,
                                                    dst1, dst1_stride,
                                                    size[sizes], size[sizes], "dst");
                        bench_new(dst1, dst1_stride,
                                  src1, sizes[size] * SIZEOF_PIXEL,
                                  sizes[size], i, j, sizes[size]);
                    }
                }
            }
        }
    }
    report("epel_uni");
}

static void checkasm_check_hevc_epel_uni_w(void)
{
    LOCAL_ALIGNED_32(uint8_t, buf0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, buf1, [BUF_SIZE]);
    PIXEL_RECT(dst0, 64, 64);
    PIXEL_RECT(dst1, 64, 64);

    HEVCDSPContext h;
    int size, bit_depth, i, j;
    const int *denom, *wx, *ox;
    declare_func(void, uint8_t *dst, ptrdiff_t dststride, const uint8_t *src, ptrdiff_t srcstride,
                 int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width);

    for (bit_depth = 8; bit_depth <= 12; bit_depth++) {
        ff_hevc_dsp_init(&h, bit_depth);

        for (i = 0; i < 2; i++) {
            for (j = 0; j < 2; j++) {
                for (size = 1; size < 10; size++) {
                    const char *type;
                    switch ((j << 1) | i) {
                    case 0: type = "pel_uni_w_pixels"; break; // 0 0
                    case 1: type = "epel_uni_w_h"; break; // 0 1
                    case 2: type = "epel_uni_w_v"; break; // 1 0
                    case 3: type = "epel_uni_w_hv"; break; // 1 1
                    }

                    if (check_func(h.put_hevc_epel_uni_w[size][j][i],
                                   "put_hevc_%s%d_%d", type, sizes[size], bit_depth)) {
                        for (denom = denoms; *denom >= 0; denom++) {
                            for (wx = weights; *wx >= 0; wx++) {
                                for (ox = offsets; *ox >= 0; ox++) {
                                    randomize_buffers();
                                    CLEAR_PIXEL_RECT(dst0);
                                    CLEAR_PIXEL_RECT(dst1);
                                    call_ref(dst0, dst0_stride,
                                             src0, sizes[size] * SIZEOF_PIXEL,
                                             sizes[size], *denom, *wx, *ox, i, j, sizes[size]);
                                    call_new(dst1, dst1_stride,
                                             src1, sizes[size] * SIZEOF_PIXEL,
                                             sizes[size], *denom, *wx, *ox, i, j, sizes[size]);
                                    checkasm_check_pixel_padded(dst0, dst0_stride,
                                                                dst1, dst1_stride,
                                                                size[sizes], size[sizes], "dst");
                                    bench_new(dst1, dst1_stride,
                                              src1, sizes[size] * SIZEOF_PIXEL,
                                              sizes[size], *denom, *wx, *ox, i, j, sizes[size]);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    report("epel_uni_w");
}

static void checkasm_check_hevc_epel_bi(void)
{
    LOCAL_ALIGNED_32(uint8_t, buf0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, buf1, [BUF_SIZE]);
    PIXEL_RECT(dst0, 64, 64);
    PIXEL_RECT(dst1, 64, 64);
    LOCAL_ALIGNED_32(int16_t, ref0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(int16_t, ref1, [BUF_SIZE]);

    HEVCDSPContext h;
    int size, bit_depth, i, j;
    declare_func(void, uint8_t *dst, ptrdiff_t dststride, const uint8_t *src, ptrdiff_t srcstride,
                 const int16_t *src2,
                 int height, intptr_t mx, intptr_t my, int width);

    for (bit_depth = 8; bit_depth <= 12; bit_depth++) {
        ff_hevc_dsp_init(&h, bit_depth);

        for (i = 0; i < 2; i++) {
            for (j = 0; j < 2; j++) {
                for (size = 1; size < 10; size++) {
                    const char *type;
                    switch ((j << 1) | i) {
                    case 0: type = "pel_bi_pixels"; break; // 0 0
                    case 1: type = "epel_bi_h"; break; // 0 1
                    case 2: type = "epel_bi_v"; break; // 1 0
                    case 3: type = "epel_bi_hv"; break; // 1 1
                    }

                    if (check_func(h.put_hevc_epel_bi[size][j][i],
                                   "put_hevc_%s%d_%d", type, sizes[size], bit_depth)) {
                        randomize_buffers_ref();
                        CLEAR_PIXEL_RECT(dst0);
                        CLEAR_PIXEL_RECT(dst1);
                        call_ref(dst0, dst0_stride,
                                 src0, sizes[size] * SIZEOF_PIXEL,
                                 ref0, sizes[size], i, j, sizes[size]);
                        call_new(dst1, dst1_stride,
                                 src1, sizes[size] * SIZEOF_PIXEL,
                                 ref1, sizes[size], i, j, sizes[size]);
                        checkasm_check_pixel_padded(dst0, dst0_stride,
                                                    dst1, dst1_stride,
                                                    size[sizes], size[sizes], "dst");
                        bench_new(dst1, dst1_stride,
                                  src1, sizes[size] * SIZEOF_PIXEL,
                                  ref1, sizes[size], i, j, sizes[size]);
                    }
                }
            }
        }
    }
    report("epel_bi");
}

static void checkasm_check_hevc_epel_bi_w(void)
{
    LOCAL_ALIGNED_32(uint8_t, buf0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, buf1, [BUF_SIZE]);
    PIXEL_RECT(dst0, 64, 64);
    PIXEL_RECT(dst1, 64, 64);
    LOCAL_ALIGNED_32(int16_t, ref0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(int16_t, ref1, [BUF_SIZE]);

    HEVCDSPContext h;
    int size, bit_depth, i, j;
    const int *denom, *wx, *ox;
    declare_func(void, uint8_t *dst, ptrdiff_t dststride, const uint8_t *src, ptrdiff_t srcstride,
                 const int16_t *src2,
                 int height, int denom, int wx0, int wx1,
                 int ox0, int ox1, intptr_t mx, intptr_t my, int width);

    for (bit_depth = 8; bit_depth <= 12; bit_depth++) {
        ff_hevc_dsp_init(&h, bit_depth);

        for (i = 0; i < 2; i++) {
            for (j = 0; j < 2; j++) {
                for (size = 1; size < 10; size++) {
                    const char *type;
                    switch ((j << 1) | i) {
                    case 0: type = "pel_bi_w_pixels"; break; // 0 0
                    case 1: type = "epel_bi_w_h"; break; // 0 1
                    case 2: type = "epel_bi_w_v"; break; // 1 0
                    case 3: type = "epel_bi_w_hv"; break; // 1 1
                    }

                    if (check_func(h.put_hevc_epel_bi_w[size][j][i],
                                   "put_hevc_%s%d_%d", type, sizes[size], bit_depth)) {
                        for (denom = denoms; *denom >= 0; denom++) {
                            for (wx = weights; *wx >= 0; wx++) {
                                for (ox = offsets; *ox >= 0; ox++) {
                                    randomize_buffers_ref();
                                    CLEAR_PIXEL_RECT(dst0);
                                    CLEAR_PIXEL_RECT(dst1);
                                    call_ref(dst0, dst0_stride,
                                             src0, sizes[size] * SIZEOF_PIXEL,
                                             ref0, sizes[size], *denom, *wx, *wx, *ox, *ox, i, j, sizes[size]);
                                    call_new(dst1, dst1_stride,
                                             src1, sizes[size] * SIZEOF_PIXEL,
                                             ref1, sizes[size], *denom, *wx, *wx, *ox, *ox, i, j, sizes[size]);
                                    checkasm_check_pixel_padded(dst0, dst0_stride,
                                                                dst1, dst1_stride,
                                                                size[sizes], size[sizes], "dst");
                                    bench_new(dst1, dst1_stride,
                                              src1, sizes[size] * SIZEOF_PIXEL,
                                              ref1, sizes[size], *denom, *wx, *wx, *ox, *ox, i, j, sizes[size]);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    report("epel_bi_w");
}

void checkasm_check_hevc_pel(void)
{
    checkasm_check_hevc_qpel();
    checkasm_check_hevc_qpel_uni();
    checkasm_check_hevc_qpel_uni_w();
    checkasm_check_hevc_qpel_bi();
    checkasm_check_hevc_qpel_bi_w();
    checkasm_check_hevc_epel();
    checkasm_check_hevc_epel_uni();
    checkasm_check_hevc_epel_uni_w();
    checkasm_check_hevc_epel_bi();
    checkasm_check_hevc_epel_bi_w();
}
