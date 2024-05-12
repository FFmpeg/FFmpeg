/*
 * (c) 2001 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * motion test.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "libavcodec/me_cmp.h"
#include "libavutil/cpu.h"
#include "libavutil/emms.h"
#include "libavutil/internal.h"
#include "libavutil/lfg.h"
#include "libavutil/mem.h"
#include "libavutil/time.h"

#undef printf

#define WIDTH 64
#define HEIGHT 64

static uint8_t img1[WIDTH * HEIGHT];
static uint8_t img2[WIDTH * HEIGHT];

static void fill_random(uint8_t *tab, int size)
{
    int i;
    AVLFG prng;

    av_lfg_init(&prng, 1);
    for(i=0;i<size;i++) {
        tab[i] = av_lfg_get(&prng) % 256;
    }
}

static void help(void)
{
    printf("motion-test [-h]\n"
           "test motion implementations\n");
}

#define NB_ITS 500

int dummy;

static void test_motion(const char *name,
                 me_cmp_func test_func, me_cmp_func ref_func)
{
    int x, y, d1, d2, it;
    uint8_t *ptr;
    int64_t ti;
    printf("testing '%s'\n", name);

    /* test correctness */
    for(it=0;it<20;it++) {

        fill_random(img1, WIDTH * HEIGHT);
        fill_random(img2, WIDTH * HEIGHT);

        for(y=0;y<HEIGHT-17;y++) {
            for(x=0;x<WIDTH-17;x++) {
                ptr = img2 + y * WIDTH + x;
                d1 = test_func(NULL, img1, ptr, WIDTH, 8);
                d2 = ref_func(NULL, img1, ptr, WIDTH, 8);
                if (d1 != d2) {
                    printf("error: mmx=%d c=%d\n", d1, d2);
                }
            }
        }
    }
    emms_c();

    /* speed test */
    ti = av_gettime_relative();
    d1 = 0;
    for(it=0;it<NB_ITS;it++) {
        for(y=0;y<HEIGHT-17;y++) {
            for(x=0;x<WIDTH-17;x++) {
                ptr = img2 + y * WIDTH + x;
                d1 += test_func(NULL, img1, ptr, WIDTH, 8);
            }
        }
    }
    emms_c();
    dummy = d1; /* avoid optimization */
    ti = av_gettime_relative() - ti;

    printf("  %0.0f kop/s\n",
           (double)NB_ITS * (WIDTH - 16) * (HEIGHT - 16) /
           (double)(ti / 1000.0));
}


int main(int argc, char **argv)
{
    AVCodecContext *ctx;
    int c;
    MECmpContext cctx, mmxctx;
    int flags[2] = { AV_CPU_FLAG_MMX, AV_CPU_FLAG_MMXEXT };
    int flags_size = HAVE_MMXEXT ? 2 : 1;

    if (argc > 1) {
        help();
        return 1;
    }

    printf("ffmpeg motion test\n");

    ctx = avcodec_alloc_context3(NULL);
    ctx->flags |= AV_CODEC_FLAG_BITEXACT;
    av_force_cpu_flags(0);
    ff_me_cmp_init(&cctx, ctx);
    for (c = 0; c < flags_size; c++) {
        int x;
        av_force_cpu_flags(flags[c]);
        ff_me_cmp_init(&mmxctx, ctx);

        for (x = 0; x < 2; x++) {
            printf("%s for %dx%d pixels\n", c ? "mmx2" : "mmx",
                   x ? 8 : 16, x ? 8 : 16);
            test_motion("mmx",     mmxctx.pix_abs[x][0], cctx.pix_abs[x][0]);
            test_motion("mmx_x2",  mmxctx.pix_abs[x][1], cctx.pix_abs[x][1]);
            test_motion("mmx_y2",  mmxctx.pix_abs[x][2], cctx.pix_abs[x][2]);
            test_motion("mmx_xy2", mmxctx.pix_abs[x][3], cctx.pix_abs[x][3]);
        }
    }
    av_free(ctx);

    return 0;
}
