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
#include <sys/time.h>
#include <unistd.h>

#include "config.h"
#include "dsputil.h"
#include "libavutil/lfg.h"

#undef exit
#undef printf

#define WIDTH 64
#define HEIGHT 64

uint8_t img1[WIDTH * HEIGHT];
uint8_t img2[WIDTH * HEIGHT];

static void fill_random(uint8_t *tab, int size)
{
    int i;
    AVLFG prng;

    av_lfg_init(&prng, 1);
    for(i=0;i<size;i++) {
#if 1
        tab[i] = av_lfg_get(&prng) % 256;
#else
        tab[i] = i;
#endif
    }
}

static void help(void)
{
    printf("motion-test [-h]\n"
           "test motion implementations\n");
    exit(1);
}

static int64_t gettime(void)
{
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
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
                d1 = test_func(NULL, img1, ptr, WIDTH, 1);
                d2 = ref_func(NULL, img1, ptr, WIDTH, 1);
                if (d1 != d2) {
                    printf("error: mmx=%d c=%d\n", d1, d2);
                }
            }
        }
    }
    emms_c();

    /* speed test */
    ti = gettime();
    d1 = 0;
    for(it=0;it<NB_ITS;it++) {
        for(y=0;y<HEIGHT-17;y++) {
            for(x=0;x<WIDTH-17;x++) {
                ptr = img2 + y * WIDTH + x;
                d1 += test_func(NULL, img1, ptr, WIDTH, 1);
            }
        }
    }
    emms_c();
    dummy = d1; /* avoid optimization */
    ti = gettime() - ti;

    printf("  %0.0f kop/s\n",
           (double)NB_ITS * (WIDTH - 16) * (HEIGHT - 16) /
           (double)(ti / 1000.0));
}


int main(int argc, char **argv)
{
    AVCodecContext *ctx;
    int c;
    DSPContext cctx, mmxctx;
    int flags[2] = { AV_CPU_FLAG_MMX, AV_CPU_FLAG_MMX2 };
    int flags_size = HAVE_MMX2 ? 2 : 1;

    for(;;) {
        c = getopt(argc, argv, "h");
        if (c == -1)
            break;
        switch(c) {
        case 'h':
            help();
            break;
        }
    }

    printf("ffmpeg motion test\n");

    ctx = avcodec_alloc_context3(NULL);
    ctx->dsp_mask = AV_CPU_FLAG_FORCE;
    dsputil_init(&cctx, ctx);
    for (c = 0; c < flags_size; c++) {
        int x;
        ctx->dsp_mask = AV_CPU_FLAG_FORCE | flags[c];
        dsputil_init(&mmxctx, ctx);

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
