/*
 * Generate a synthetic YUV video sequence suitable for codec testing.
 *
 * copyright (c) Sebastien Bechet <s.bechet@av7.net>
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
#include <stdio.h>
#include <inttypes.h>

#include "utils.c"

#define FIXP (1 << 16)
#define MY_PI 205887 // (M_PI * FIX)

static int64_t int_pow(int64_t a, int p)
{
    int64_t v = FIXP;

    for (; p; p--) {
        v *= a;
        v /= FIXP;
    }

    return v;
}

static int64_t int_sin(int64_t a)
{
    if (a < 0)
        a = MY_PI - a;  // 0..inf
    a %= 2 * MY_PI;     // 0..2PI

    if (a >= MY_PI * 3 / 2)
        a -= 2 * MY_PI; // -PI / 2 .. 3PI / 2
    if (a >= MY_PI / 2)
        a = MY_PI - a;  // -PI / 2 ..  PI / 2

    return a - int_pow(a, 3) / 6 + int_pow(a, 5) / 120 - int_pow(a, 7) / 5040;
}

static unsigned char tab_r[256 * 256];
static unsigned char tab_g[256 * 256];
static unsigned char tab_b[256 * 256];

static int h_cos[360];
static int h_sin[360];

static int ipol(uint8_t *src, int x, int y)
{
    int int_x  = x >> 16;
    int int_y  = y >> 16;
    int frac_x = x & 0xFFFF;
    int frac_y = y & 0xFFFF;
    int s00    = src[( int_x      & 255) + 256 * ( int_y      & 255)];
    int s01    = src[((int_x + 1) & 255) + 256 * ( int_y      & 255)];
    int s10    = src[( int_x      & 255) + 256 * ((int_y + 1) & 255)];
    int s11    = src[((int_x + 1) & 255) + 256 * ((int_y + 1) & 255)];
    int s0     = (((1 << 16) - frac_x) * s00 + frac_x * s01) >> 8;
    int s1     = (((1 << 16) - frac_x) * s10 + frac_x * s11) >> 8;

    return (((1 << 16) - frac_y) * s0 + frac_y * s1) >> 24;
}

static void gen_image(int num, int w, int h)
{
    const int c = h_cos[num % 360];
    const int s = h_sin[num % 360];

    const int xi = -(w / 2) * c;
    const int yi =  (w / 2) * s;

    const int xj = -(h / 2) * s;
    const int yj = -(h / 2) * c;
    int i, j;

    int x, y;
    int xprime = xj;
    int yprime = yj;

    for (j = 0; j < h; j++) {
        x       = xprime + xi + FIXP * w / 2;
        xprime += s;

        y       = yprime + yi + FIXP * h / 2;
        yprime += c;

        for (i = 0; i < w; i++) {
            x += c;
            y -= s;
            put_pixel(i, j,
                      ipol(tab_r, x, y),
                      ipol(tab_g, x, y),
                      ipol(tab_b, x, y));
        }
    }
}

#define W 256
#define H 256

static int init_demo(const char *filename)
{
    int i, j;
    int h;
    int radian;
    char line[3 * W];

    FILE *input_file;

    input_file = fopen(filename, "rb");
    if (!input_file) {
        perror(filename);
        return 1;
    }

    if (fread(line, 1, 15, input_file) != 15)
        return 1;
    for (i = 0; i < H; i++) {
        if (fread(line, 1, 3 * W, input_file) != 3 * W)
            return 1;
        for (j = 0; j < W; j++) {
            tab_r[W * i + j] = line[3 * j    ];
            tab_g[W * i + j] = line[3 * j + 1];
            tab_b[W * i + j] = line[3 * j + 2];
        }
    }
    fclose(input_file);

    /* tables sin/cos */
    for (i = 0; i < 360; i++) {
        radian   = 2 * i * MY_PI / 360;
        h        = 2 * FIXP + int_sin(radian);
        h_cos[i] = h * int_sin(radian + MY_PI / 2) / 2 / FIXP;
        h_sin[i] = h * int_sin(radian)             / 2 / FIXP;
    }

    return 0;
}

int main(int argc, char **argv)
{
    int w, h, i;
    char buf[1024];
    int isdir = 0;

    if (argc != 3) {
        printf("usage: %s image.pnm file|dir\n"
               "generate a test video stream\n", argv[0]);
        return 1;
    }

    if (!freopen(argv[2], "wb", stdout))
        isdir = 1;

    w = DEFAULT_WIDTH;
    h = DEFAULT_HEIGHT;

    rgb_tab = malloc(w * h * 3);
    wrap    = w * 3;
    width   = w;
    height  = h;

    if (init_demo(argv[1]))
        return 1;

    for (i = 0; i < DEFAULT_NB_PICT; i++) {
        gen_image(i, w, h);
        if (isdir) {
            snprintf(buf, sizeof(buf), "%s%02d.pgm", argv[2], i);
            pgmyuv_save(buf, w, h, rgb_tab);
        } else {
            pgmyuv_save(NULL, w, h, rgb_tab);
        }
    }

    free(rgb_tab);
    return 0;
}
