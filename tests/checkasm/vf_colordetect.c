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

#include <string.h>
#include "checkasm.h"

#include "libavfilter/vf_colordetect.h"
#include "libavutil/mem_internal.h"

#define WIDTH  256
#define HEIGHT 16
#define STRIDE (WIDTH + 32)

static void check_range_detect(int depth)
{
    const int mpeg_min =  16 << (depth - 8);
    const int mpeg_max = 235 << (depth - 8);

    FFColorDetectDSPContext dsp = {0};
    ff_color_detect_dsp_init(&dsp, depth, AVCOL_RANGE_UNSPECIFIED);

    declare_func(int, const uint8_t *, ptrdiff_t, ptrdiff_t, ptrdiff_t, int, int);

    /* Initialize to 128, which should always return 0 */
    LOCAL_ALIGNED_32(uint8_t, in, [HEIGHT * STRIDE]);
    memset(in, 0x80, HEIGHT * STRIDE);

    /* Place an out-of-range value in a random position near the center */
    const int h2 = HEIGHT >> 1;
    int idx0 = ((rnd() % h2) + h2) * STRIDE + (rnd() % WIDTH);
    if (depth > 8) {
        idx0 &= ~1;
        in[idx0] = in[idx0 + 1] = 0;
    } else {
        in[idx0] = 0;
    }

    int w = WIDTH;
    if (depth > 8)
        w /= 2;

    if (check_func(dsp.detect_range, "detect_range_%d", depth)) {
        /* Test increasing height, to ensure we hit the placed 0 eventually */
        for (int h = 1; h <= HEIGHT; h++) {
            int res_ref = call_ref(in, STRIDE, w, h, mpeg_min, mpeg_max);
            int res_new = call_new(in, STRIDE, w, h, mpeg_min, mpeg_max);
            if (res_ref != res_new)
                fail();
        }

        /* Test performance of base case without any out-of-range values */
        memset(in, 0x80, HEIGHT * STRIDE);
        bench_new(in, STRIDE, w, HEIGHT, mpeg_min, mpeg_max);
    }
}

static void check_alpha_detect(int depth, enum AVColorRange range)
{
    const int mpeg_min =  16 << (depth - 8);
    const int mpeg_max = 235 << (depth - 8);
    const int p = (1 << depth) - 1;
    const int q = mpeg_max - mpeg_min;
    const int k = p * mpeg_min + q + (1 << (depth - 1));

    FFColorDetectDSPContext dsp = {0};
    ff_color_detect_dsp_init(&dsp, depth, range);

    declare_func(int, const uint8_t *, ptrdiff_t, const uint8_t *, ptrdiff_t,
                      ptrdiff_t, ptrdiff_t, int p, int q, int k);

    LOCAL_ALIGNED_32(uint8_t, luma,  [HEIGHT * STRIDE]);
    LOCAL_ALIGNED_32(uint8_t, alpha, [HEIGHT * STRIDE]);
    memset(luma,  0x80, HEIGHT * STRIDE);
    memset(alpha, 0xFF, HEIGHT * STRIDE);

    /* Try and force overflow */
    if (depth > 8 && range == AVCOL_RANGE_MPEG) {
        ((uint16_t *) luma)[0] = 235 << (depth - 8);
        ((uint16_t *) luma)[1] =  16 << (depth - 8);
    } else {
        luma[0] = 235;
        luma[1] = 16;
    }

    /* Place an out-of-range value in a random position near the center */
    const int h2 = HEIGHT >> 1;
    int idx0 = ((rnd() % h2) + h2) * STRIDE + (rnd() % WIDTH);
    if (depth > 8) {
        idx0 &= ~1;
        alpha[idx0] = alpha[idx0 + 1] = 0;
    } else {
        alpha[idx0] = 0;
    }

    int w = WIDTH;
    if (depth > 8)
        w /= 2;

    if (check_func(dsp.detect_alpha, "detect_alpha_%d_%s", depth, range == AVCOL_RANGE_JPEG ? "full" : "limited")) {
        /* Test increasing height, to ensure we hit the placed 0 eventually */
        for (int h = 1; h <= HEIGHT; h++) {
            int res_ref = call_ref(luma, STRIDE, alpha, STRIDE, w, h, p, q, k);
            int res_new = call_new(luma, STRIDE, alpha, STRIDE, w, h, p, q, k);
            if (res_ref != res_new)
                fail();
        }

        /* Test performance of base case without any out-of-range values */
        memset(alpha, 0xFF, HEIGHT * STRIDE);
        bench_new(luma, STRIDE, alpha, STRIDE, w, HEIGHT, p, q, k);
    }
}

void checkasm_check_colordetect(void)
{
    for (int depth = 8; depth <= 16; depth += 8) {
        check_range_detect(depth);
        report("detect_range_%d", depth);

        check_alpha_detect(depth, AVCOL_RANGE_JPEG);
        check_alpha_detect(depth, AVCOL_RANGE_MPEG);
        report("detect_alpha_%d", depth);
    }
}
