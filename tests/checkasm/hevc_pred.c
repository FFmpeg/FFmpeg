/*
 * Copyright (c) 2026 Jun Zhao <barryjzhao@tencent.com>
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
#include "libavcodec/hevc/pred.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

static const uint32_t pixel_mask[3] = { 0xffffffff, 0x01ff01ff, 0x03ff03ff };

#define SIZEOF_PIXEL ((bit_depth + 7) / 8)
#define BUF_SIZE (2 * 64 * 64)  /* Enough for 32x32 with stride=64 */
#define PRED_SIZE 128           /* Increased to 4 * MAX_TB_SIZE to accommodate C code reads */

#define randomize_buffers()                        \
    do {                                           \
        uint32_t mask = pixel_mask[bit_depth - 8]; \
        for (int i = 0; i < BUF_SIZE; i += 4) {    \
            uint32_t r = rnd() & mask;             \
            AV_WN32A(buf0 + i, r);                 \
            AV_WN32A(buf1 + i, r);                 \
        }                                          \
        /* Start from -4 so that AV_WN32A writes  \
         * top[-4..-1] and left[-4..-1], ensuring  \
         * top[-1] and left[-1] contain known data \
         * since angular pred references them      \
         * (e.g. mode 10/26 edge filtering,        \
         * mode 18 diagonal, V/H neg extension). */\
        for (int i = -4; i < PRED_SIZE; i += 4) {  \
            uint32_t r = rnd() & mask;             \
            AV_WN32A(top + i, r);                  \
            AV_WN32A(left + i, r);                 \
        }                                          \
    } while (0)

static void check_pred_dc(HEVCPredContext *h,
                          uint8_t *buf0, uint8_t *buf1,
                          uint8_t *top, uint8_t *left, int bit_depth)
{
    const char *const block_name[] = { "4x4", "8x8", "16x16", "32x32" };
    const int block_size[] = { 4, 8, 16, 32 };
    int log2_size;

    declare_func(void, uint8_t *src, const uint8_t *top,
                 const uint8_t *left, ptrdiff_t stride,
                 int log2_size, int c_idx);

    /* Test all 4 sizes: 4x4, 8x8, 16x16, 32x32 */
    for (log2_size = 2; log2_size <= 5; log2_size++) {
        int size = block_size[log2_size - 2];
        ptrdiff_t stride = 64 * SIZEOF_PIXEL;

        if (check_func(h->pred_dc, "hevc_pred_dc_%s_%d",
                       block_name[log2_size - 2], bit_depth)) {
            /* Test with c_idx=0 (luma, with edge smoothing for size < 32) */
            randomize_buffers();
            call_ref(buf0, top, left, stride, log2_size, 0);
            call_new(buf1, top, left, stride, log2_size, 0);
            if (memcmp(buf0, buf1, size * stride))
                fail();

            /* Test with c_idx=1 (chroma, no edge smoothing) */
            randomize_buffers();
            call_ref(buf0, top, left, stride, log2_size, 1);
            call_new(buf1, top, left, stride, log2_size, 1);
            if (memcmp(buf0, buf1, size * stride))
                fail();

            bench_new(buf1, top, left, stride, log2_size, 0);
        }
    }
}

static void check_pred_planar(HEVCPredContext *h,
                              uint8_t *buf0, uint8_t *buf1,
                              uint8_t *top, uint8_t *left, int bit_depth)
{
    const char *const block_name[] = { "4x4", "8x8", "16x16", "32x32" };
    const int block_size[] = { 4, 8, 16, 32 };
    int i;

    declare_func(void, uint8_t *src, const uint8_t *top,
                 const uint8_t *left, ptrdiff_t stride);

    /* Test all 4 sizes: 4x4, 8x8, 16x16, 32x32 */
    for (i = 0; i < 4; i++) {
        int size = block_size[i];
        ptrdiff_t stride = 64 * SIZEOF_PIXEL;

        if (check_func(h->pred_planar[i], "hevc_pred_planar_%s_%d",
                       block_name[i], bit_depth)) {
            randomize_buffers();
            call_ref(buf0, top, left, stride);
            call_new(buf1, top, left, stride);
            if (memcmp(buf0, buf1, size * stride))
                fail();

            bench_new(buf1, top, left, stride);
        }
    }
}

/*
 * Angular prediction modes are divided into categories:
 *
 * Mode 10: Horizontal pure copy (H pure)
 * Mode 26: Vertical pure copy (V pure)
 * Modes 2-9: Horizontal positive angle (H pos) - uses left reference
 * Modes 11-17: Horizontal negative angle (H neg) - needs reference extension
 * Modes 18-25: Vertical negative angle (V neg) - needs reference extension
 * Modes 27-34: Vertical positive angle (V pos) - uses top reference
 *
 * Each category has 4 NEON functions for 4x4, 8x8, 16x16, 32x32 sizes.
 */
static void check_pred_angular(HEVCPredContext *h,
                               uint8_t *buf0, uint8_t *buf1,
                               uint8_t *top, uint8_t *left, int bit_depth)
{
    const char *const block_name[] = { "4x4", "8x8", "16x16", "32x32" };
    const int block_size[] = { 4, 8, 16, 32 };
    int i, mode;

    declare_func(void, uint8_t *src, const uint8_t *top,
                 const uint8_t *left, ptrdiff_t stride, int c_idx, int mode);

    /* Test all 4 sizes */
    for (i = 0; i < 4; i++) {
        int size = block_size[i];
        ptrdiff_t stride = 64 * SIZEOF_PIXEL;

        /* Test all 33 angular modes (2-34) */
        for (mode = 2; mode <= 34; mode++) {
            const char *mode_category;

            /* Determine mode category for descriptive test name */
            if (mode == 10)
                mode_category = "Hpure";
            else if (mode == 26)
                mode_category = "Vpure";
            else if (mode >= 2 && mode <= 9)
                mode_category = "Hpos";
            else if (mode >= 11 && mode <= 17)
                mode_category = "Hneg";
            else if (mode >= 18 && mode <= 25)
                mode_category = "Vneg";
            else /* mode >= 27 && mode <= 34 */
                mode_category = "Vpos";

            if (check_func(h->pred_angular[i],
                           "hevc_pred_angular_%s_%s_mode%d_%d",
                           block_name[i], mode_category, mode, bit_depth)) {
                /* Test with c_idx=0 (luma) */
                randomize_buffers();
                call_ref(buf0, top, left, stride, 0, mode);
                call_new(buf1, top, left, stride, 0, mode);
                if (memcmp(buf0, buf1, size * stride))
                    fail();

                /* Test with c_idx=1 (chroma) for modes 10/26 to cover
                 * the edge filtering skip path */
                if (mode == 10 || mode == 26) {
                    randomize_buffers();
                    call_ref(buf0, top, left, stride, 1, mode);
                    call_new(buf1, top, left, stride, 1, mode);
                    if (memcmp(buf0, buf1, size * stride))
                        fail();
                }

                bench_new(buf1, top, left, stride, 0, mode);
            }
        }
    }
}

void checkasm_check_hevc_pred(void)
{
    LOCAL_ALIGNED_32(uint8_t, buf0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, buf1, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, top_buf, [PRED_SIZE + 16]);
    LOCAL_ALIGNED_32(uint8_t, left_buf, [PRED_SIZE + 16]);
    /* Add offset of 8 bytes to allow negative indexing (top[-1], left[-1]) */
    uint8_t *top = top_buf + 8;
    uint8_t *left = left_buf + 8;
    int bit_depth;

    for (bit_depth = 8; bit_depth <= 10; bit_depth += 2) {
        HEVCPredContext h;

        ff_hevc_pred_init(&h, bit_depth);
        check_pred_dc(&h, buf0, buf1, top, left, bit_depth);
    }
    report("pred_dc");

    for (bit_depth = 8; bit_depth <= 10; bit_depth += 2) {
        HEVCPredContext h;

        ff_hevc_pred_init(&h, bit_depth);
        check_pred_planar(&h, buf0, buf1, top, left, bit_depth);
    }
    report("pred_planar");

    for (bit_depth = 8; bit_depth <= 10; bit_depth += 2) {
        HEVCPredContext h;

        ff_hevc_pred_init(&h, bit_depth);
        check_pred_angular(&h, buf0, buf1, top, left, bit_depth);
    }
    report("pred_angular");
}
