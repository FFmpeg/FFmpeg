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
#include "libavutil/mem_internal.h"

#include "libavcodec/me_cmp.h"

#include "checkasm.h"

static void fill_random(uint8_t *tab, int size)
{
    int i;
    for (i = 0; i < size; i++) {
        tab[i] = rnd() % 256;
    }
}

static void test_motion(const char *name, me_cmp_func test_func)
{
    /* test configurarion */
#define ITERATIONS 16
#define WIDTH 64
#define HEIGHT 64

    /* motion estimation can look up to 17 bytes ahead */
    static const int look_ahead = 17;

    int i, x, y, h, d1, d2;
    uint8_t *ptr;

    LOCAL_ALIGNED_16(uint8_t, img1, [WIDTH * HEIGHT]);
    LOCAL_ALIGNED_16(uint8_t, img2, [WIDTH * HEIGHT]);

    declare_func_emms(AV_CPU_FLAG_MMX, int, struct MpegEncContext *c,
                      uint8_t *blk1 /* align width (8 or 16) */,
                      uint8_t *blk2 /* align 1 */, ptrdiff_t stride,
                      int h);

    if (test_func == NULL) {
        return;
    }

    /* test correctness */
    fill_random(img1, WIDTH * HEIGHT);
    fill_random(img2, WIDTH * HEIGHT);

    if (check_func(test_func, "%s", name)) {
        for (i = 0; i < ITERATIONS; i++) {
            x = rnd() % (WIDTH - look_ahead);
            y = rnd() % (HEIGHT - look_ahead);
            // Pick a random h between 4 and 16; pick an even value.
            h = 4 + ((rnd() % (16 + 1 - 4)) & ~1);

            ptr = img2 + y * WIDTH + x;
            d2 = call_ref(NULL, img1, ptr, WIDTH, h);
            d1 = call_new(NULL, img1, ptr, WIDTH, h);

            if (d1 != d2) {
                fail();
                printf("func: %s, x=%d y=%d h=%d, error: asm=%d c=%d\n", name, x, y, h, d1, d2);
                break;
            }
        }
        // Test with a fixed offset, for benchmark stability
        ptr = img2 + 3 * WIDTH + 3;
        bench_new(NULL, img1, ptr, WIDTH, 8);
    }
}

#define ME_CMP_1D_ARRAYS(XX)                                                   \
    XX(sad)                                                                    \
    XX(sse)                                                                    \
    XX(hadamard8_diff)                                                         \
    XX(vsad)                                                                   \
    XX(vsse)                                                                   \
    XX(nsse)                                                                   \
    XX(me_pre_cmp)                                                             \
    XX(me_cmp)                                                                 \
    XX(me_sub_cmp)                                                             \
    XX(mb_cmp)                                                                 \
    XX(ildct_cmp)                                                              \
    XX(frame_skip_cmp)                                                         \
    XX(median_sad)

// tests for functions not yet implemented
#if 0
    XX(dct_sad)                                                                \
    XX(quant_psnr)                                                             \
    XX(bit)                                                                    \
    XX(rd)                                                                     \
    XX(w53)                                                                    \
    XX(w97)                                                                    \
    XX(dct_max)                                                                \
    XX(dct264_sad)                                                             \

#endif

static void check_motion(void)
{
    char buf[64];
    AVCodecContext *av_ctx;
    MECmpContext me_ctx;

    memset(&me_ctx, 0, sizeof(me_ctx));

    /* allocate AVCodecContext */
    av_ctx = avcodec_alloc_context3(NULL);
    av_ctx->flags |= AV_CODEC_FLAG_BITEXACT;

    ff_me_cmp_init(&me_ctx, av_ctx);

    for (int i = 0; i < FF_ARRAY_ELEMS(me_ctx.pix_abs); i++) {
        for (int j = 0; j < FF_ARRAY_ELEMS(me_ctx.pix_abs[0]); j++) {
            snprintf(buf, sizeof(buf), "pix_abs_%d_%d", i, j);
            test_motion(buf, me_ctx.pix_abs[i][j]);
        }
    }

#define XX(me_cmp_array)                                                        \
    for (int i = 0; i < FF_ARRAY_ELEMS(me_ctx.me_cmp_array); i++) {             \
        snprintf(buf, sizeof(buf), #me_cmp_array "_%d", i);                     \
        test_motion(buf, me_ctx.me_cmp_array[i]);                               \
    }
    ME_CMP_1D_ARRAYS(XX)
#undef XX

    avcodec_free_context(&av_ctx);
}

void checkasm_check_motion(void)
{
    check_motion();
    report("motion");
}
