/*
 * Copyright (c) 2015 Anton Khirnov
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

#include "checkasm.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/hevcdsp.h"

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"

// max PU size + interpolation stencil
#define BUF_SIZE (FFALIGN(64 + 7, 16) * (64 + 7) * 2)

#define PIXEL_SIZE(depth) ((depth + 7) / 8)

#define randomize_buffers(buf, size, depth)     \
    do {                                        \
        uint32_t mask = pixel_mask[depth - 8];  \
        int i;                                  \
        for (i = 0; i < size; i += 4) {         \
            uint32_t r = rnd() & mask;          \
            AV_WN32A(buf + i, r);               \
        }                                       \
    } while (0)

static const uint32_t pixel_mask[3] = { 0xffffffff, 0x01ff01ff, 0x03ff03ff };

static const int pred_heights[][7] = {
    [2]  = {  8, 4, 2, 0 },
    [4]  = { 16, 8, 4, 2, 0 },
    [6]  = {  8, 0 },
    [8]  = { 32, 16, 8, 4, 2, 0 },
    [12] = { 16, 0 },
    [16] = { 64, 32, 16, 12, 8, 4, 0 },
    [24] = { 32, 0 },
    [32] = { 64, 32, 24, 16, 8, 0 },
    [48] = { 64, 0 },
    [64] = { 64, 48, 32, 16, 0 },
};

static const int pred_widths[] = { 4, 8, 12, 16, 24, 32, 48, 64 };

static const char *interp_names[2][2] = { { "pixels", "h" }, { "v", "hv" } };

#define UNWEIGHTED_PRED(dst0, dst1, src0, width, bit_depth)         \
do {                                                                \
    int i;                                                          \
    for (i = 0; i < FF_ARRAY_ELEMS(pred_heights[i]); i++) {         \
        int height = pred_heights[width][i];                        \
        if (!height)                                                \
            break;                                                  \
        call_ref(dst0, dststride, src0,       srcstride, height);   \
        call_new(dst1, dststride, src0,       srcstride, height);   \
        if (memcmp(dst0, dst1, dststride * height))                 \
            fail();                                                 \
        bench_new(dst1, dststride, src0,       srcstride, height);  \
    }                                                               \
} while (0)

#define UNWEIGHTED_PRED_AVG(dst0, dst1, src0, src1, width, bit_depth)   \
do {                                                                    \
    int i;                                                              \
    for (i = 0; i < FF_ARRAY_ELEMS(pred_heights[i]); i++) {             \
        int height = pred_heights[width][i];                            \
        if (!height)                                                    \
            break;                                                      \
        call_ref(dst0, dststride, src0, src1, srcstride, height);       \
        call_new(dst1, dststride, src0, src1, srcstride, height);       \
        if (memcmp(dst0, dst1, dststride * height))                     \
            fail();                                                     \
        bench_new(dst1, dststride, src0, src1, srcstride, height);      \
    }                                                                   \
} while (0)

static void check_unweighted_pred(HEVCDSPContext *h, uint8_t *dst0, uint8_t *dst1,
                                  int16_t *src0, int16_t *src1, int bit_depth)
{
    int i;

    randomize_buffers(src0, BUF_SIZE, 8);
    randomize_buffers(src1, BUF_SIZE, 8);

    memset(dst0, 0, BUF_SIZE * sizeof(*dst0));
    memset(dst1, 0, BUF_SIZE * sizeof(*dst1));

    for (i = 0; i < FF_ARRAY_ELEMS(pred_widths); i++) {
        const int width = pred_widths[i];
        const int srcstride = FFALIGN(width, 16) * sizeof(*src0);
        const int dststride = FFALIGN(width, 16) * PIXEL_SIZE(bit_depth);

        {
            declare_func(void, uint8_t *dst, ptrdiff_t dststride, int16_t *src, ptrdiff_t srcstride, int height);
            if (check_func(h->put_unweighted_pred[i], "put_unweighted_pred_%d_%d", width, bit_depth))
                UNWEIGHTED_PRED(dst0, dst1, src0, width, bit_depth);
            if (check_func(h->put_unweighted_pred_chroma[i], "put_unweighted_pred_%d_%d", width / 2, bit_depth))
                UNWEIGHTED_PRED(dst0, dst1, src0, width, bit_depth);
        }
        {
            declare_func(void, uint8_t *dst, ptrdiff_t dststride,
                         int16_t *src0, int16_t *src1, ptrdiff_t srcstride, int height);
            if (check_func(h->put_unweighted_pred_avg[i], "put_unweighted_pred_avg_%d_%d", width, bit_depth))
                UNWEIGHTED_PRED_AVG(dst0, dst1, src0, src1, width, bit_depth);
            if (check_func(h->put_unweighted_pred_avg_chroma[i], "put_unweighted_pred_avg_%d_%d", width / 2, bit_depth))
                UNWEIGHTED_PRED_AVG(dst0, dst1, src0, src1, width, bit_depth);
        }
    }
}

#define WEIGHTED_PRED(dst0, dst1, src0, width, bit_depth)                               \
do {                                                                                    \
    int i;                                                                              \
    for (i = 0; i < FF_ARRAY_ELEMS(pred_heights[i]); i++) {                             \
        int height = pred_heights[width][i];                                            \
        if (!height)                                                                    \
            break;                                                                      \
        call_ref(denom, weight0, offset0, dst0, dststride, src0, srcstride, height);    \
        call_new(denom, weight0, offset0, dst1, dststride, src0, srcstride, height);    \
        if (memcmp(dst0, dst1, dststride * height))                                     \
            fail();                                                                     \
        bench_new(denom,  weight0, offset0, dst1, dststride, src0, srcstride, height);  \
    }                                                                                   \
} while (0)

#define WEIGHTED_PRED_AVG(dst0, dst1, src0, src1, width, bit_depth)                                              \
do {                                                                                                            \
    int i;                                                                                                      \
    for (i = 0; i < FF_ARRAY_ELEMS(pred_heights[i]); i++) {                                                     \
        int height = pred_heights[width][i];                                                                    \
        if (!height)                                                                                            \
            break;                                                                                              \
        call_ref(denom, weight0, weight1, offset0, offset1, dst0, dststride, src0, src1, srcstride, height);    \
        call_new(denom, weight0, weight1, offset0, offset1, dst1, dststride, src0, src1, srcstride, height);    \
        if (memcmp(dst0, dst1, dststride * height))                                                             \
            fail();                                                                                             \
        bench_new(denom, weight0, weight1, offset0, offset1, dst1, dststride, src0, src1, srcstride, height);   \
    }                                                                                                           \
} while (0)

static void check_weighted_pred(HEVCDSPContext *h, uint8_t *dst0, uint8_t *dst1,
                                int16_t *src0, int16_t *src1, int bit_depth)
{
    uint8_t denom;
    int16_t weight0, weight1, offset0, offset1;
    int i;

    randomize_buffers(src0, BUF_SIZE, 8);
    randomize_buffers(src1, BUF_SIZE, 8);

    denom   = rnd() & 7;
    weight0 = denom + ((rnd() & 255) - 128);
    weight1 = denom + ((rnd() & 255) - 128);
    offset0 = (rnd() & 255) - 128;
    offset1 = (rnd() & 255) - 128;

    memset(dst0, 0, BUF_SIZE * sizeof(*dst0));
    memset(dst1, 0, BUF_SIZE * sizeof(*dst1));

    for (i = 0; i < FF_ARRAY_ELEMS(pred_widths); i++) {
        const int width = pred_widths[i];
        const int srcstride = FFALIGN(width, 16) * sizeof(*src0);
        const int dststride = FFALIGN(width, 16) * PIXEL_SIZE(bit_depth);

        {
            declare_func(void, uint8_t denom, int16_t weight, int16_t offset,
                         uint8_t *dst, ptrdiff_t dststride, int16_t *src, ptrdiff_t srcstride, int height);
            if (check_func(h->weighted_pred[i], "weighted_pred_%d_%d", width, bit_depth))
                WEIGHTED_PRED(dst0, dst1, src0, width, bit_depth);
            if (check_func(h->weighted_pred_chroma[i], "weighted_pred_%d_%d", width / 2, bit_depth))
                WEIGHTED_PRED(dst0, dst1, src0, width, bit_depth);
        }
        {
            declare_func(void, uint8_t denom, int16_t weight0, int16_t weight1, int16_t offset0, int16_t offset1,
                         uint8_t *dst, ptrdiff_t dststride, int16_t *src0, int16_t *src1, ptrdiff_t srcstride, int height);
            if (check_func(h->weighted_pred_avg[i], "weighted_pred_avg_%d_%d", width, bit_depth))
                WEIGHTED_PRED_AVG(dst0, dst1, src0, src1, width, bit_depth);
            if (check_func(h->weighted_pred_avg_chroma[i], "weighted_pred_avg_%d_%d", width / 2, bit_depth))
                WEIGHTED_PRED_AVG(dst0, dst1, src0, src1, width, bit_depth);
        }
    }
}

static void check_epel(HEVCDSPContext *h, int16_t *dst0, int16_t *dst1,
                       uint8_t *src, int16_t *mcbuffer, int bit_depth)
{
    int i, j, k, l, mx, my;

    declare_func(void, int16_t *dst, ptrdiff_t dststride, uint8_t *src, ptrdiff_t srcstride,
                 int height, int mx, int my, int16_t *mcbuffer);

    randomize_buffers(src, BUF_SIZE, bit_depth);

    memset(dst0, 0, BUF_SIZE * sizeof(*dst0));
    memset(dst1, 0, BUF_SIZE * sizeof(*dst1));

    for (i = 0; i < 2; i++) {
        for (j = 0; j < 2; j++) {
            for (k = 0; k < FF_ARRAY_ELEMS(h->put_hevc_epel[i][j]); k++) {
                int width = pred_widths[k] / 2;
                int dststride = FFALIGN(width, 16) * sizeof(*dst0);
                int srcstride = FFALIGN(width + 3, 8) * PIXEL_SIZE(bit_depth);

                if (!check_func(h->put_hevc_epel[i][j][k], "epel_%s_%d_%d", interp_names[i][j], width, bit_depth))
                    continue;

                for (l = 0; l < FF_ARRAY_ELEMS(pred_heights[0]); l++) {
                    int height = pred_heights[width][l];

                    if (!height)
                        continue;

                    for (my = i; my < (i ? 8 : 1); my++)
                        for (mx = j; mx < (j ? 8 : 1); mx++) {
                            call_ref(dst0, dststride, src + srcstride + PIXEL_SIZE(bit_depth), srcstride, height, mx, my, mcbuffer);
                            call_new(dst1, dststride, src + srcstride + PIXEL_SIZE(bit_depth), srcstride, height, mx, my, mcbuffer);

                            if (memcmp(dst0, dst1, dststride * height * sizeof(*dst0)))
                                fail();

                            bench_new(dst1, dststride, src + srcstride + PIXEL_SIZE(bit_depth), srcstride, height, mx, my, mcbuffer);
                        }
                }
            }
        }
    }
}

static void check_qpel(HEVCDSPContext *h, int16_t *dst0, int16_t *dst1,
                       uint8_t *src, int16_t *mcbuffer, int bit_depth)
{
    int i, j, k, l, mx, my;

    declare_func(void, int16_t *dst, ptrdiff_t dststride, uint8_t *src, ptrdiff_t srcstride,
                 int height, int mx, int my, int16_t *mcbuffer);

    randomize_buffers(src, BUF_SIZE, bit_depth);

    memset(dst0, 0, BUF_SIZE * sizeof(*dst0));
    memset(dst1, 0, BUF_SIZE * sizeof(*dst1));

    for (i = 0; i < 2; i++) {
        for (j = 0; j < 2; j++) {
            for (k = 0; k < FF_ARRAY_ELEMS(h->put_hevc_qpel[i][j]); k++) {
                int width = pred_widths[k];
                int dststride = FFALIGN(width, 16) * sizeof(*dst0);
                int srcstride = FFALIGN(width + 7, 8) * PIXEL_SIZE(bit_depth);

                if (!check_func(h->put_hevc_qpel[i][j][k], "qpel_%s_%d_%d", interp_names[i][j], width, bit_depth))
                    continue;

                for (l = 0; l < FF_ARRAY_ELEMS(pred_heights[0]); l++) {
                    int height = pred_heights[width][l];

                    if (!height)
                        continue;

                    for (my = i; my < (i ? 2 : 1); my++)
                        for (mx = j; mx < (j ? 2 : 1); mx++) {
                            call_ref(dst0, dststride, src + 3 * srcstride + 3 * PIXEL_SIZE(bit_depth), srcstride, height, mx, my, mcbuffer);
                            call_new(dst1, dststride, src + 3 * srcstride + 3 * PIXEL_SIZE(bit_depth), srcstride, height, mx, my, mcbuffer);

                            if (memcmp(dst0, dst1, dststride * height * sizeof(*dst0)))
                                fail();

                            bench_new(dst1, dststride, src + 3 * srcstride + 3 * PIXEL_SIZE(bit_depth), srcstride, height, mx, my, mcbuffer);
                        }
                }
            }
        }
    }
}

void checkasm_check_hevc_mc(void)
{
    DECLARE_ALIGNED(16, uint8_t,  buf8_0)[BUF_SIZE];
    DECLARE_ALIGNED(16, uint8_t,  buf8_1)[BUF_SIZE];

    DECLARE_ALIGNED(16, int16_t, buf16_0)[BUF_SIZE];
    DECLARE_ALIGNED(16, int16_t, buf16_1)[BUF_SIZE];

    DECLARE_ALIGNED(16, int16_t, mcbuffer)[BUF_SIZE];

    HEVCDSPContext h;
    int bit_depth;

    for (bit_depth = 8; bit_depth <= 10; bit_depth++) {
        ff_hevc_dsp_init(&h, bit_depth);
        check_qpel(&h, buf16_0, buf16_1, buf8_0, mcbuffer, bit_depth);
    }
    report("qpel");

    for (bit_depth = 8; bit_depth <= 10; bit_depth++) {
        ff_hevc_dsp_init(&h, bit_depth);
        check_epel(&h, buf16_0, buf16_1, buf8_0, mcbuffer, bit_depth);
    }
    report("epel");

    for (bit_depth = 8; bit_depth <= 10; bit_depth++) {
        ff_hevc_dsp_init(&h, bit_depth);
        check_unweighted_pred(&h, buf8_0, buf8_1, buf16_0, buf16_1, bit_depth);
    }
    report("unweighted_pred");

    for (bit_depth = 8; bit_depth <= 10; bit_depth++) {
        ff_hevc_dsp_init(&h, bit_depth);
        check_weighted_pred(&h, buf8_0, buf8_1, buf16_0, buf16_1, bit_depth);
    }
    report("weighted_pred");
}
