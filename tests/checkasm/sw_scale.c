/*
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

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"

#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"

#include "checkasm.h"

#define randomize_buffers(buf, size)      \
    do {                                  \
        int j;                            \
        for (j = 0; j < size; j+=4)       \
            AV_WN32(buf + j, rnd());      \
    } while (0)

#define SRC_PIXELS 128

static void check_hscale(void)
{
#define MAX_FILTER_WIDTH 40
#define FILTER_SIZES 5
    static const int filter_sizes[FILTER_SIZES] = { 4, 8, 16, 32, 40 };

#define HSCALE_PAIRS 2
    static const int hscale_pairs[HSCALE_PAIRS][2] = {
        { 8, 14 },
        { 8, 18 },
    };

    int i, j, fsi, hpi, width;
    struct SwsContext *ctx;

    // padded
    LOCAL_ALIGNED_32(uint8_t, src, [FFALIGN(SRC_PIXELS + MAX_FILTER_WIDTH - 1, 4)]);
    LOCAL_ALIGNED_32(uint32_t, dst0, [SRC_PIXELS]);
    LOCAL_ALIGNED_32(uint32_t, dst1, [SRC_PIXELS]);

    // padded
    LOCAL_ALIGNED_32(int16_t, filter, [SRC_PIXELS * MAX_FILTER_WIDTH + MAX_FILTER_WIDTH]);
    LOCAL_ALIGNED_32(int32_t, filterPos, [SRC_PIXELS]);

    // The dst parameter here is either int16_t or int32_t but we use void* to
    // just cover both cases.
    declare_func_emms(AV_CPU_FLAG_MMX, void, void *c, void *dst, int dstW,
                      const uint8_t *src, const int16_t *filter,
                      const int32_t *filterPos, int filterSize);

    ctx = sws_alloc_context();
    if (sws_init_context(ctx, NULL, NULL) < 0)
        fail();

    randomize_buffers(src, SRC_PIXELS + MAX_FILTER_WIDTH - 1);

    for (hpi = 0; hpi < HSCALE_PAIRS; hpi++) {
        for (fsi = 0; fsi < FILTER_SIZES; fsi++) {
            width = filter_sizes[fsi];

            ctx->srcBpc = hscale_pairs[hpi][0];
            ctx->dstBpc = hscale_pairs[hpi][1];
            ctx->hLumFilterSize = ctx->hChrFilterSize = width;

            for (i = 0; i < SRC_PIXELS; i++) {
                filterPos[i] = i;

                // These filter cofficients are chosen to try break two corner
                // cases, namely:
                //
                // - Negative filter coefficients. The filters output signed
                //   values, and it should be possible to end up with negative
                //   output values.
                //
                // - Positive clipping. The hscale filter function has clipping
                //   at (1<<15) - 1
                //
                // The coefficients sum to the 1.0 point for the hscale
                // functions (1 << 14).

                for (j = 0; j < width; j++) {
                    filter[i * width + j] = -((1 << 14) / (width - 1));
                }
                filter[i * width + (rnd() % width)] = ((1 << 15) - 1);
            }

            for (i = 0; i < MAX_FILTER_WIDTH; i++) {
                // These values should be unused in SIMD implementations but
                // may still be read, random coefficients here should help show
                // issues where they are used in error.

                filter[SRC_PIXELS * width + i] = rnd();
            }
            ff_getSwsFunc(ctx);

            if (check_func(ctx->hcScale, "hscale_%d_to_%d_width%d", ctx->srcBpc, ctx->dstBpc + 1, width)) {
                memset(dst0, 0, SRC_PIXELS * sizeof(dst0[0]));
                memset(dst1, 0, SRC_PIXELS * sizeof(dst1[0]));

                call_ref(NULL, dst0, SRC_PIXELS, src, filter, filterPos, width);
                call_new(NULL, dst1, SRC_PIXELS, src, filter, filterPos, width);
                if (memcmp(dst0, dst1, SRC_PIXELS * sizeof(dst0[0])))
                    fail();
                bench_new(NULL, dst0, SRC_PIXELS, src, filter, filterPos, width);
            }
        }
    }
    sws_freeContext(ctx);
}

void checkasm_check_sw_scale(void)
{
    check_hscale();
    report("hscale");
}
