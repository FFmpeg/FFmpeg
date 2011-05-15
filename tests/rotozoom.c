/*
 * Generates a synthetic YUV video sequence suitable for codec testing.
 *
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

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

#define FIXP (1 << 16)
#define MY_PI 205887 //(M_PI * FIX)

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
    if (a >= MY_PI /2)
        a = MY_PI - a;  // -PI / 2 ..  PI / 2

    return a - int_pow(a, 3) / 6 + int_pow(a, 5) / 120 - int_pow(a, 7) / 5040;
}

#define SCALEBITS 8
#define ONE_HALF  (1 << (SCALEBITS - 1))
#define FIX(x)    ((int) ((x) * (1L << SCALEBITS) + 0.5))

static void rgb24_to_yuv420p(unsigned char *lum, unsigned char *cb,
                             unsigned char *cr, unsigned char *src,
                             int width, int height)
{
    int wrap, wrap3, x, y;
    int r, g, b, r1, g1, b1;
    unsigned char *p;

    wrap  = width;
    wrap3 = width * 3;
    p = src;
    for (y = 0; y < height; y += 2) {
        for (x = 0; x < width; x += 2) {
            r = p[0];
            g = p[1];
            b = p[2];
            r1 = r;
            g1 = g;
            b1 = b;
            lum[0] = (FIX(0.29900) * r + FIX(0.58700) * g +
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            r = p[3];
            g = p[4];
            b = p[5];
            r1 += r;
            g1 += g;
            b1 += b;
            lum[1] = (FIX(0.29900) * r + FIX(0.58700) * g +
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            p   += wrap3;
            lum += wrap;

            r = p[0];
            g = p[1];
            b = p[2];
            r1 += r;
            g1 += g;
            b1 += b;
            lum[0] = (FIX(0.29900) * r + FIX(0.58700) * g +
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;
            r = p[3];
            g = p[4];
            b = p[5];
            r1 += r;
            g1 += g;
            b1 += b;
            lum[1] = (FIX(0.29900) * r + FIX(0.58700) * g +
                      FIX(0.11400) * b + ONE_HALF) >> SCALEBITS;

            cb[0] = ((- FIX(0.16874) * r1 - FIX(0.33126) * g1 +
                      FIX(0.50000) * b1 + 4 * ONE_HALF - 1) >> (SCALEBITS + 2)) + 128;
            cr[0] = ((FIX(0.50000) * r1 - FIX(0.41869) * g1 -
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

    p = rgb_tab + y * wrap + x * 3;
    p[0] = r;
    p[1] = g;
    p[2] = b;
}

unsigned char tab_r[256 * 256];
unsigned char tab_g[256 * 256];
unsigned char tab_b[256 * 256];

int h_cos [360];
int h_sin [360];

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
    const int c = h_cos [num % 360];
    const int s = h_sin [num % 360];

    const int xi = -(w / 2) * c;
    const int yi =  (w / 2) * s;

    const int xj = -(h / 2) * s;
    const int yj = -(h / 2) * c;
    int i, j;

    int x, y;
    int xprime = xj;
    int yprime = yj;

    for (j = 0; j < h; j++) {
        x = xprime + xi + FIXP * w / 2;
        xprime += s;

        y = yprime + yi + FIXP * h / 2;
        yprime += c;

        for (i = 0; i < w; i++ ) {
            x += c;
            y -= s;
            put_pixel(i, j, ipol(tab_r, x, y), ipol(tab_g, x, y), ipol(tab_b, x, y));
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
        radian = 2 * i * MY_PI / 360;
        h      = 2 * FIXP + int_sin (radian);
        h_cos[i] = h * int_sin(radian + MY_PI / 2) / 2 / FIXP;
        h_sin[i] = h * int_sin(radian)             / 2 / FIXP;
    }

  return 0;
}

int main(int argc, char **argv)
{
    int w, h, i;
    char buf[1024];

    if (argc != 3) {
        printf("usage: %s directory/ image.pnm\n"
               "generate a test video stream\n", argv[0]);
        return 1;
    }

    w = DEFAULT_WIDTH;
    h = DEFAULT_HEIGHT;

    rgb_tab = malloc(w * h * 3);
    wrap    = w * 3;
    width   = w;
    height  = h;

    if (init_demo(argv[2]))
        return 1;

    for (i = 0; i < DEFAULT_NB_PICT; i++) {
        snprintf(buf, sizeof(buf), "%s%02d.pgm", argv[1], i);
        gen_image(i, w, h);
        pgmyuv_save(buf, w, h, rgb_tab);
    }

    free(rgb_tab);
    return 0;
}
