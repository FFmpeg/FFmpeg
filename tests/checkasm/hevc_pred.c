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

#define randomize_ref_buffers()                    \
    do {                                           \
        uint32_t mask = pixel_mask[bit_depth - 8]; \
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

static void check_ref_filter_3tap(HEVCPredContext *h,
                                  uint8_t *top, uint8_t *left, int bit_depth)
{
    const char *const block_name[] = { "8x8", "16x16", "32x32" };
    const int block_size[] = { 8, 16, 32 };
    int i;

    /* 3-tap filter: out[i] = (in[i+1] + 2*in[i] + in[i-1] + 2) >> 2
     * Filters 2*size-1 samples (indices 0..2*size-2) plus corner [-1].
     * Output: filtered_left[-1..2*size-1] and filtered_top[-1..2*size-1] */
    declare_func(void, uint8_t *filtered_left, uint8_t *filtered_top,
                 const uint8_t *left, const uint8_t *top, int size);

    for (i = 0; i < 3; i++) {
        int size = block_size[i];
        int n = 2 * size;

        if (check_func(h->ref_filter_3tap[i],
                       "hevc_ref_filter_3tap_%s_%d",
                       block_name[i], bit_depth)) {
            /* Allocate output buffers with space for [-1] indexing.
             * Need n+1 elements: indices [-1..n-1] = n+1 pixels.
             * Use (n+1)*SIZEOF_PIXEL bytes starting at offset SIZEOF_PIXEL. */
            LOCAL_ALIGNED_32(uint8_t, fl_ref_buf, [PRED_SIZE + 16]);
            LOCAL_ALIGNED_32(uint8_t, fl_new_buf, [PRED_SIZE + 16]);
            LOCAL_ALIGNED_32(uint8_t, ft_ref_buf, [PRED_SIZE + 16]);
            LOCAL_ALIGNED_32(uint8_t, ft_new_buf, [PRED_SIZE + 16]);
            uint8_t *fl_ref = fl_ref_buf + 8;
            uint8_t *fl_new = fl_new_buf + 8;
            uint8_t *ft_ref = ft_ref_buf + 8;
            uint8_t *ft_new = ft_new_buf + 8;

            randomize_ref_buffers();
            /* Clear output buffers so comparison is clean */
            memset(fl_ref_buf, 0, PRED_SIZE + 16);
            memset(fl_new_buf, 0, PRED_SIZE + 16);
            memset(ft_ref_buf, 0, PRED_SIZE + 16);
            memset(ft_new_buf, 0, PRED_SIZE + 16);

            call_ref(fl_ref, ft_ref, left, top, size);
            call_new(fl_new, ft_new, left, top, size);

            /* Compare filtered_left[-1..2*size-1] and filtered_top[-1..2*size-1] */
            if (memcmp(fl_ref - SIZEOF_PIXEL, fl_new - SIZEOF_PIXEL,
                       (n + 1) * SIZEOF_PIXEL))
                fail();
            if (memcmp(ft_ref - SIZEOF_PIXEL, ft_new - SIZEOF_PIXEL,
                       (n + 1) * SIZEOF_PIXEL))
                fail();

            bench_new(fl_new, ft_new, left, top, size);
        }
    }
}

static void check_ref_filter_strong(HEVCPredContext *h,
                                    uint8_t *top, uint8_t *left,
                                    int bit_depth)
{
    /* Strong intra smoothing: only 32x32 luma.
     * Interpolates top into filtered_top[0..62], sets filtered_top[-1] and [63].
     * Modifies left[0..62] in-place. */
    declare_func(void, uint8_t *filtered_top, uint8_t *left,
                 const uint8_t *top);

    if (check_func(h->ref_filter_strong,
                   "hevc_ref_filter_strong_%d", bit_depth)) {
        LOCAL_ALIGNED_32(uint8_t, ft_ref_buf, [PRED_SIZE + 16]);
        LOCAL_ALIGNED_32(uint8_t, ft_new_buf, [PRED_SIZE + 16]);
        LOCAL_ALIGNED_32(uint8_t, left_ref_buf, [PRED_SIZE + 16]);
        LOCAL_ALIGNED_32(uint8_t, left_new_buf, [PRED_SIZE + 16]);
        uint8_t *ft_ref = ft_ref_buf + 8;
        uint8_t *ft_new = ft_new_buf + 8;
        uint8_t *left_ref = left_ref_buf + 8;
        uint8_t *left_new = left_new_buf + 8;

        randomize_ref_buffers();
        memset(ft_ref_buf, 0, PRED_SIZE + 16);
        memset(ft_new_buf, 0, PRED_SIZE + 16);

        /* Copy left so both ref and new start with the same input
         * (left is modified in-place) */
        memcpy(left_ref_buf, left - 8, PRED_SIZE + 16);
        memcpy(left_new_buf, left - 8, PRED_SIZE + 16);

        call_ref(ft_ref, left_ref, top);
        call_new(ft_new, left_new, top);

        /* Compare filtered_top[-1..63] = 65 pixels */
        if (memcmp(ft_ref - SIZEOF_PIXEL, ft_new - SIZEOF_PIXEL,
                   65 * SIZEOF_PIXEL))
            fail();

        /* Compare left[-1..63] = 65 pixels (left[-1] is unchanged,
         * left[0..62] are modified, left[63] is unchanged) */
        if (memcmp(left_ref - SIZEOF_PIXEL, left_new - SIZEOF_PIXEL,
                   65 * SIZEOF_PIXEL))
            fail();

        bench_new(ft_new, left_new, top);
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

    for (bit_depth = 8; bit_depth <= 10; bit_depth += 2) {
        HEVCPredContext h;

        ff_hevc_pred_init(&h, bit_depth);
        check_ref_filter_3tap(&h, top, left, bit_depth);
    }
    report("ref_filter_3tap");

    for (bit_depth = 8; bit_depth <= 10; bit_depth += 2) {
        HEVCPredContext h;

        ff_hevc_pred_init(&h, bit_depth);
        check_ref_filter_strong(&h, top, left, bit_depth);
    }
    report("ref_filter_strong");
}
