/*
 * copyright (c) Sebastien Bechet <s.bechet@av7.net>
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCALEBITS 8
#define ONE_HALF  (1 << (SCALEBITS - 1))
#define FIX(x)    ((int) ((x) * (1L << SCALEBITS) + 0.5))

#define err_if(expr) do {                                              \
    if (expr) {                                                        \
        fprintf(stderr, "%s\n", strerror(errno));                      \
        exit(1);                                                       \
    }                                                                  \
} while (0)

static void rgb24_to_yuv420p(unsigned char *lum, unsigned char *cb,
                             unsigned char *cr, unsigned char *src,
                             int width, int height)
{
    int wrap, wrap3, x, y;
    int r, g, b, r1, g1, b1;
    unsigned char *p;

    wrap  = width;
    wrap3 = width * 3;
    p     = src;
    for (y = 0; y < height; y += 2) {
        for (x = 0; x < width; x += 2) {
            r       = p[0];
            g       = p[1];
            b       = p[2];
            r1      = r;
            g1      = g;
            b1      = b;
            lum[0]  = (FIX(0.29900) * r + FIX(0.58700) * g +
                       FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            r       = p[3];
            g       = p[4];
            b       = p[5];
            r1     += r;
            g1     += g;
            b1     += b;
            lum[1]  = (FIX(0.29900) * r + FIX(0.58700) * g +
                       FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            p      += wrap3;
            lum    += wrap;

            r       = p[0];
            g       = p[1];
            b       = p[2];
            r1     += r;
            g1     += g;
            b1     += b;
            lum[0]  = (FIX(0.29900) * r + FIX(0.58700) * g +
                       FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            r       = p[3];
            g       = p[4];
            b       = p[5];
            r1     += r;
            g1     += g;
            b1     += b;
            lum[1]  = (FIX(0.29900) * r + FIX(0.58700) * g +
                       FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;

            cb[0]   = ((- FIX(0.16874) * r1 - FIX(0.33126) * g1 +
                       FIX(0.50000) * b1 + 4 * ONE_HALF - 1) >> (SCALEBITS + 2)) + 128;
            cr[0]   = ((FIX(0.50000) * r1 - FIX(0.41869) * g1 -
                       FIX(0.08131) * b1 + 4 * ONE_HALF - 1) >> (SCALEBITS + 2)) + 128;

            cb++;
            cr++;
            p   += -wrap3 + 2 * 3;
            lum += -wrap  + 2;
        }
        p   += wrap3;
        lum += wrap;
    }
}

/* cif format */
#define DEFAULT_WIDTH   352
#define DEFAULT_HEIGHT  288
#define DEFAULT_NB_PICT  50

static void pgmyuv_save(const char *filename, int w, int h,
                        unsigned char *rgb_tab)
{
    FILE *f;
    int i, h2, w2;
    unsigned char *cb, *cr;
    unsigned char *lum_tab, *cb_tab, *cr_tab;

    lum_tab = malloc(w * h);
    cb_tab  = malloc(w * h / 4);
    cr_tab  = malloc(w * h / 4);

    rgb24_to_yuv420p(lum_tab, cb_tab, cr_tab, rgb_tab, w, h);

    f = fopen(filename, "wb");
    fprintf(f, "P5\n%d %d\n%d\n", w, h * 3 / 2, 255);
    err_if(fwrite(lum_tab, 1, w * h, f) != w * h);
    h2 = h / 2;
    w2 = w / 2;
    cb = cb_tab;
    cr = cr_tab;
    for (i = 0; i < h2; i++) {
        err_if(fwrite(cb, 1, w2, f) != w2);
        err_if(fwrite(cr, 1, w2, f) != w2);
        cb += w2;
        cr += w2;
    }
    fclose(f);

    free(lum_tab);
    free(cb_tab);
    free(cr_tab);
}

static unsigned char *rgb_tab;
static int width, height, wrap;

static void put_pixel(int x, int y, int r, int g, int b)
{
    unsigned char *p;

    if (x < 0 || x >= width ||
        y < 0 || y >= height)
        return;

    p    = rgb_tab + y * wrap + x * 3;
    p[0] = r;
    p[1] = g;
    p[2] = b;
}
