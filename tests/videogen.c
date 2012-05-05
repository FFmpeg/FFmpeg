/*
 * Generate a synthetic YUV video sequence suitable for codec testing.
 * NOTE: No floats are used to guarantee bitexact output.
 *
 * Copyright (c) 2002 Fabrice Bellard
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

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include "utils.c"

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

static VObj objs[NB_OBJS];

static unsigned int seed = 1;

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
