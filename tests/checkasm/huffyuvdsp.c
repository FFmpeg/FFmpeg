/*
 * Copyright (c) 2016 Alexandra Hájková
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

#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"

#include "libavcodec/huffyuvdsp.h"

#include "checkasm.h"

enum {
    MAX_WIDTH = 16*128,   ///< arbitrary limit used for the tests
};

#define randomize_buffers(buf, size)     \
    do {                                 \
        int j;                           \
        for (j = 0; j < size; j++)       \
            buf[j] = rnd() & 0xFFFF;       \
    } while (0)

#define randomize_buffer_mask(buf, width, mask)        \
    do {                                               \
        unsigned mask2 = mask | (mask << 16);          \
        for (size_t i = 0; i < (width & ~1); i += 2)   \
            AV_WN32A((buf) + i, rnd() & mask2);        \
        if (width & 1)                                 \
            buf[width - 1] = rnd() & mask;             \
    } while (0)

static void check_add_int16(HuffYUVDSPContext *c, unsigned mask, int width, const char * name)
{
    if (!check_func(c->add_int16, "%s", name))
        return;

    uint16_t *src0 = av_mallocz(width * sizeof(uint16_t));
    uint16_t *src1 = av_mallocz(width * sizeof(uint16_t));
    uint16_t *dst0 = av_mallocz(width * sizeof(uint16_t));
    uint16_t *dst1 = av_mallocz(width * sizeof(uint16_t));

    declare_func(void, uint16_t *dst, const uint16_t *src, unsigned mask, int w);

    if (!src0 || !src1 || !dst0 || !dst1)
        fail();

    randomize_buffers(src0, width);
    memcpy(src1, src0, width * sizeof(uint16_t));

    call_ref(dst0, src0, mask, width);
    call_new(dst1, src1, mask, width);
    if (memcmp(dst0, dst1, width * sizeof(uint16_t)))
        fail();
    bench_new(dst1, src1, mask, width);

    av_free(src0);
    av_free(src1);
    av_free(dst0);
    av_free(dst1);
}

static void check_add_hfyu_median_pred_int16(const HuffYUVDSPContext *c, unsigned mask, int width)
{
    declare_func(void, uint16_t *dst, const uint16_t *top,
                       const uint16_t *diff, unsigned mask,
                       int w, int *left, int *left_top);

    if (!check_func(c->add_hfyu_median_pred_int16, "add_hfyu_median_pred_int16"))
        return;

    DECLARE_ALIGNED(16, uint16_t, top)[MAX_WIDTH];
    DECLARE_ALIGNED(16, uint16_t, diff)[MAX_WIDTH];
    DECLARE_ALIGNED(16, uint16_t, dst_new)[MAX_WIDTH];
    DECLARE_ALIGNED(16, uint16_t, dst_ref)[MAX_WIDTH];
    int left_new = rnd() & mask, left_ref = left_new;
    int lt_new   = rnd() & mask, lt_ref   = lt_new;

    randomize_buffer_mask(top,  MAX_WIDTH, mask);
    randomize_buffer_mask(diff, MAX_WIDTH, mask);

    call_ref(dst_ref, top, diff, mask, width, &left_ref, &lt_ref);
    call_new(dst_new, top, diff, mask, width, &left_new, &lt_new);

    if (left_ref != left_new || lt_ref != lt_new ||
        memcmp(dst_ref, dst_new, width * sizeof(dst_ref[0])))
        fail();

    bench_new(dst_new, top, diff, mask, width, &left_new, &lt_new);
}

static void check_add_hfyu_left_pred_bgr32(HuffYUVDSPContext *c)
{
#define BUF_SIZE 1080
    uint8_t src[4 * BUF_SIZE], dst0[4 * BUF_SIZE], dst1[4 * BUF_SIZE];
    uint8_t left[4], left0[4], left1[4];

    declare_func(void, uint8_t *d, const uint8_t *s, intptr_t w, uint8_t *l);

    if (!check_func(c->add_hfyu_left_pred_bgr32, "add_hfyu_left_pred_bgr32"))
        return;

    randomize_buffers(src, sizeof (src));
    randomize_buffers(left, sizeof (left));
    memcpy(left0, left, sizeof (left));
    memcpy(left1, left, sizeof (left));

    call_ref(dst0, src, BUF_SIZE, left0);
    call_new(dst1, src, BUF_SIZE, left1);

    if (memcmp(dst0, dst1, sizeof (dst0)) != 0 ||
        memcmp(left0, left1, sizeof (left0)) != 0) {
        fail();
    }

    bench_new(dst1, src, BUF_SIZE, left);
}

void checkasm_check_huffyuvdsp(void)
{
    HuffYUVDSPContext c;

    ff_huffyuvdsp_init(&c);

    unsigned bps  = 9 + rnd() % 8;
    unsigned mask = (1 << bps) - 1;
    int width = 1 + rnd() % MAX_WIDTH;

    /*! test width not multiple of mmsize */
    check_add_int16(&c, mask, width, "add_int16_rnd_width");
    report("add_int16_rnd_width");

    /*! test always with the same size (for perf test) */
    check_add_int16(&c, mask, MAX_WIDTH, "add_int16_128");
    report("add_int16_128");

    check_add_hfyu_median_pred_int16(&c, mask, width);
    report("add_hfyu_median_pred_int16");

    check_add_hfyu_left_pred_bgr32(&c);
    report("add_hfyu_left_pred_bgr32");
}
