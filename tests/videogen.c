/*
 * Generate a synthetic YUV video sequence suitable for codec testing.
 * NOTE: No floats are used to guarantee bitexact output.
 *
 * Copyright (c) 2002 Fabrice Bellard
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#define SCALEBITS 8
#define ONE_HALF  (1 << (SCALEBITS - 1))
#define FIX(x)    ((int) ((x) * (1L << SCALEBITS) + 0.5))

static void rgb24_to_yuv420p(uint8_t *lum, uint8_t *cb, uint8_t *cr,
                             uint8_t *src, int width, int height)
{
    int wrap, wrap3, x, y;
    int r, g, b, r1, g1, b1;
    uint8_t *p;

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

            cb[0]   = 128 + ((- FIX(0.16874) * r1 -
                                FIX(0.33126) * g1 +
                                FIX(0.50000) * b1 +
                              4 * ONE_HALF - 1)
                             >> (SCALEBITS + 2));
            cr[0]   = 128 + ((FIX(0.50000) * r1 -
                              FIX(0.41869) * g1 -
                              FIX(0.08131) * b1 +
                              4 * ONE_HALF - 1)
                             >> (SCALEBITS + 2));

            cb++;
            cr++;
            p   += -wrap3 + 2 * 3;
            lum += -wrap + 2;
        }
        p   += wrap3;
        lum += wrap;
    }
}

/* cif format */
#define DEFAULT_WIDTH   352
#define DEFAULT_HEIGHT  288
#define DEFAULT_NB_PICT 50 /* 2 seconds */

static void pgmyuv_save(const char *filename, int w, int h,
                        unsigned char *rgb_tab)
{
    FILE *f;
    int i, h2, w2;
    unsigned char *cb, *cr;
    unsigned char *lum_tab, *cb_tab, *cr_tab;

    lum_tab = malloc(w * h);
    cb_tab  = malloc((w * h) / 4);
    cr_tab  = malloc((w * h) / 4);

    rgb24_to_yuv420p(lum_tab, cb_tab, cr_tab, rgb_tab, w, h);

    f = fopen(filename, "wb");
    fprintf(f, "P5\n%d %d\n%d\n", w, (h * 3) / 2, 255);
    fwrite(lum_tab, 1, w * h, f);
    h2 = h / 2;
    w2 = w / 2;
    cb = cb_tab;
    cr = cr_tab;
    for (i = 0; i < h2; i++) {
        fwrite(cb, 1, w2, f);
        fwrite(cr, 1, w2, f);
        cb += w2;
        cr += w2;
    }
    fclose(f);

    free(lum_tab);
    free(cb_tab);
    free(cr_tab);
}

unsigned char *rgb_tab;
int width, height, wrap;

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

static unsigned int myrnd(unsigned int *seed_ptr, int n)
{
    unsigned int seed, val;

    seed = *seed_ptr;
    seed = (seed * 314159) + 1;
    if (n == 256) {
        val = seed >> 24;
    } else {
        val = seed % n;
    }
    *seed_ptr = seed;
    return val;
}

#define NOISE_X  10
#define NOISE_Y  30
#define NOISE_W  26

#define FRAC_BITS 8
#define FRAC_ONE (1 << FRAC_BITS)

/* cosine approximate with 1-x^2 */
static int int_cos(int a)
{
    int v, neg;
    a = a & (FRAC_ONE - 1);
    if (a >= (FRAC_ONE / 2))
        a = FRAC_ONE - a;
    neg = 0;
    if (a > (FRAC_ONE / 4)) {
        neg = -1;
        a   = (FRAC_ONE / 2) - a;
    }
    v = FRAC_ONE - ((a * a) >> 4);
    v = (v ^ neg) - neg;
    return v;
}

#define NB_OBJS  10

typedef struct VObj {
    int x, y, w, h;
    int r, g, b;
} VObj;

VObj objs[NB_OBJS];

unsigned int seed = 1;

static void gen_image(int num, int w, int h)
{
    int r, g, b, x, y, i, dx, dy, x1, y1;
    unsigned int seed1;

    if (num == 0) {
        for (i = 0; i < NB_OBJS; i++) {
            objs[i].x = myrnd(&seed, w);
            objs[i].y = myrnd(&seed, h);
            objs[i].w = myrnd(&seed, w / 4) + 10;
            objs[i].h = myrnd(&seed, h / 4) + 10;
            objs[i].r = myrnd(&seed, 256);
            objs[i].g = myrnd(&seed, 256);
            objs[i].b = myrnd(&seed, 256);
        }
    }

    /* first a moving background with gradients */
    /* test motion estimation */
    dx = int_cos(num * FRAC_ONE / 50) * 35;
    dy = int_cos(num * FRAC_ONE / 50 + FRAC_ONE / 10) * 30;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            x1 = (x << FRAC_BITS) + dx;
            y1 = (y << FRAC_BITS) + dy;
            r  =       ((y1  * 7) >> FRAC_BITS) & 0xff;
            g  = (((x1 + y1) * 9) >> FRAC_BITS) & 0xff;
            b  =  ((x1       * 5) >> FRAC_BITS) & 0xff;
            put_pixel(x, y, r, g, b);
        }
    }

    /* then some noise with very high intensity to test saturation */
    seed1 = num;
    for (y = 0; y < NOISE_W; y++) {
        for (x = 0; x < NOISE_W; x++) {
            r = myrnd(&seed1, 256);
            g = myrnd(&seed1, 256);
            b = myrnd(&seed1, 256);
            put_pixel(x + NOISE_X, y + NOISE_Y, r, g, b);
        }
    }

    /* then moving objects */
    for (i = 0; i < NB_OBJS; i++) {
        VObj *p = &objs[i];
        seed1 = i;
        for (y = 0; y < p->h; y++) {
            for (x = 0; x < p->w; x++) {
                r = p->r;
                g = p->g;
                b = p->b;
                /* add a per object noise */
                r += myrnd(&seed1, 50);
                g += myrnd(&seed1, 50);
                b += myrnd(&seed1, 50);
                put_pixel(x + p->x, y + p->y, r, g, b);
            }
        }
        p->x += myrnd(&seed, 21) - 10;
        p->y += myrnd(&seed, 21) - 10;
    }
}

int main(int argc, char **argv)
{
    int w, h, i;
    char buf[1024];

    if (argc != 2) {
        printf("usage: %s file\n"
               "generate a test video stream\n", argv[0]);
        exit(1);
    }

    w = DEFAULT_WIDTH;
    h = DEFAULT_HEIGHT;

    rgb_tab = malloc(w * h * 3);
    wrap    = w * 3;
    width   = w;
    height  = h;

    for (i = 0; i < DEFAULT_NB_PICT; i++) {
        snprintf(buf, sizeof(buf), "%s%02d.pgm", argv[1], i);
        gen_image(i, w, h);
        pgmyuv_save(buf, w, h, rgb_tab);
    }

    free(rgb_tab);
    return 0;
}
