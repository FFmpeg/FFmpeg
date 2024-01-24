/*
 * Texture block compression
 * Copyright (C) 2015 Vittorio Giovara <vittorio.giovara@gmail.com>
 * Based on public domain code by Fabian Giesen, Sean Barrett and Yann Collet.
 *
 * This file is part of FFmpeg
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stddef.h>
#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"

#include "texturedsp.h"

static const uint8_t expand5[32] = {
      0,   8,  16,  24,  33,  41,  49,  57,  66,  74,  82,  90,
     99, 107, 115, 123, 132, 140, 148, 156, 165, 173, 181, 189,
    198, 206, 214, 222, 231, 239, 247, 255,
};

static const uint8_t expand6[64] = {
      0,   4,   8,  12,  16,  20,  24,  28,  32,  36,  40,  44,
     48,  52,  56,  60,  65,  69,  73,  77,  81,  85,  89,  93,
     97, 101, 105, 109, 113, 117, 121, 125, 130, 134, 138, 142,
    146, 150, 154, 158, 162, 166, 170, 174, 178, 182, 186, 190,
    195, 199, 203, 207, 211, 215, 219, 223, 227, 231, 235, 239,
    243, 247, 251, 255,
};

static const uint8_t match5[256][2] = {
    {  0,  0 }, {  0,  0 }, {  0,  1 }, {  0,  1 }, {  1,  0 }, {  1,  0 },
    {  1,  0 }, {  1,  1 }, {  1,  1 }, {  2,  0 }, {  2,  0 }, {  0,  4 },
    {  2,  1 }, {  2,  1 }, {  2,  1 }, {  3,  0 }, {  3,  0 }, {  3,  0 },
    {  3,  1 }, {  1,  5 }, {  3,  2 }, {  3,  2 }, {  4,  0 }, {  4,  0 },
    {  4,  1 }, {  4,  1 }, {  4,  2 }, {  4,  2 }, {  4,  2 }, {  3,  5 },
    {  5,  1 }, {  5,  1 }, {  5,  2 }, {  4,  4 }, {  5,  3 }, {  5,  3 },
    {  5,  3 }, {  6,  2 }, {  6,  2 }, {  6,  2 }, {  6,  3 }, {  5,  5 },
    {  6,  4 }, {  6,  4 }, {  4,  8 }, {  7,  3 }, {  7,  3 }, {  7,  3 },
    {  7,  4 }, {  7,  4 }, {  7,  4 }, {  7,  5 }, {  5,  9 }, {  7,  6 },
    {  7,  6 }, {  8,  4 }, {  8,  4 }, {  8,  5 }, {  8,  5 }, {  8,  6 },
    {  8,  6 }, {  8,  6 }, {  7,  9 }, {  9,  5 }, {  9,  5 }, {  9,  6 },
    {  8,  8 }, {  9,  7 }, {  9,  7 }, {  9,  7 }, { 10,  6 }, { 10,  6 },
    { 10,  6 }, { 10,  7 }, {  9,  9 }, { 10,  8 }, { 10,  8 }, {  8, 12 },
    { 11,  7 }, { 11,  7 }, { 11,  7 }, { 11,  8 }, { 11,  8 }, { 11,  8 },
    { 11,  9 }, {  9, 13 }, { 11, 10 }, { 11, 10 }, { 12,  8 }, { 12,  8 },
    { 12,  9 }, { 12,  9 }, { 12, 10 }, { 12, 10 }, { 12, 10 }, { 11, 13 },
    { 13,  9 }, { 13,  9 }, { 13, 10 }, { 12, 12 }, { 13, 11 }, { 13, 11 },
    { 13, 11 }, { 14, 10 }, { 14, 10 }, { 14, 10 }, { 14, 11 }, { 13, 13 },
    { 14, 12 }, { 14, 12 }, { 12, 16 }, { 15, 11 }, { 15, 11 }, { 15, 11 },
    { 15, 12 }, { 15, 12 }, { 15, 12 }, { 15, 13 }, { 13, 17 }, { 15, 14 },
    { 15, 14 }, { 16, 12 }, { 16, 12 }, { 16, 13 }, { 16, 13 }, { 16, 14 },
    { 16, 14 }, { 16, 14 }, { 15, 17 }, { 17, 13 }, { 17, 13 }, { 17, 14 },
    { 16, 16 }, { 17, 15 }, { 17, 15 }, { 17, 15 }, { 18, 14 }, { 18, 14 },
    { 18, 14 }, { 18, 15 }, { 17, 17 }, { 18, 16 }, { 18, 16 }, { 16, 20 },
    { 19, 15 }, { 19, 15 }, { 19, 15 }, { 19, 16 }, { 19, 16 }, { 19, 16 },
    { 19, 17 }, { 17, 21 }, { 19, 18 }, { 19, 18 }, { 20, 16 }, { 20, 16 },
    { 20, 17 }, { 20, 17 }, { 20, 18 }, { 20, 18 }, { 20, 18 }, { 19, 21 },
    { 21, 17 }, { 21, 17 }, { 21, 18 }, { 20, 20 }, { 21, 19 }, { 21, 19 },
    { 21, 19 }, { 22, 18 }, { 22, 18 }, { 22, 18 }, { 22, 19 }, { 21, 21 },
    { 22, 20 }, { 22, 20 }, { 20, 24 }, { 23, 19 }, { 23, 19 }, { 23, 19 },
    { 23, 20 }, { 23, 20 }, { 23, 20 }, { 23, 21 }, { 21, 25 }, { 23, 22 },
    { 23, 22 }, { 24, 20 }, { 24, 20 }, { 24, 21 }, { 24, 21 }, { 24, 22 },
    { 24, 22 }, { 24, 22 }, { 23, 25 }, { 25, 21 }, { 25, 21 }, { 25, 22 },
    { 24, 24 }, { 25, 23 }, { 25, 23 }, { 25, 23 }, { 26, 22 }, { 26, 22 },
    { 26, 22 }, { 26, 23 }, { 25, 25 }, { 26, 24 }, { 26, 24 }, { 24, 28 },
    { 27, 23 }, { 27, 23 }, { 27, 23 }, { 27, 24 }, { 27, 24 }, { 27, 24 },
    { 27, 25 }, { 25, 29 }, { 27, 26 }, { 27, 26 }, { 28, 24 }, { 28, 24 },
    { 28, 25 }, { 28, 25 }, { 28, 26 }, { 28, 26 }, { 28, 26 }, { 27, 29 },
    { 29, 25 }, { 29, 25 }, { 29, 26 }, { 28, 28 }, { 29, 27 }, { 29, 27 },
    { 29, 27 }, { 30, 26 }, { 30, 26 }, { 30, 26 }, { 30, 27 }, { 29, 29 },
    { 30, 28 }, { 30, 28 }, { 30, 28 }, { 31, 27 }, { 31, 27 }, { 31, 27 },
    { 31, 28 }, { 31, 28 }, { 31, 28 }, { 31, 29 }, { 31, 29 }, { 31, 30 },
    { 31, 30 }, { 31, 30 }, { 31, 31 }, { 31, 31 },
};

static const uint8_t match6[256][2] = {
    {  0,  0 }, {  0,  1 }, {  1,  0 }, {  1,  0 }, {  1,  1 }, {  2,  0 },
    {  2,  1 }, {  3,  0 }, {  3,  0 }, {  3,  1 }, {  4,  0 }, {  4,  0 },
    {  4,  1 }, {  5,  0 }, {  5,  1 }, {  6,  0 }, {  6,  0 }, {  6,  1 },
    {  7,  0 }, {  7,  0 }, {  7,  1 }, {  8,  0 }, {  8,  1 }, {  8,  1 },
    {  8,  2 }, {  9,  1 }, {  9,  2 }, {  9,  2 }, {  9,  3 }, { 10,  2 },
    { 10,  3 }, { 10,  3 }, { 10,  4 }, { 11,  3 }, { 11,  4 }, { 11,  4 },
    { 11,  5 }, { 12,  4 }, { 12,  5 }, { 12,  5 }, { 12,  6 }, { 13,  5 },
    { 13,  6 }, {  8, 16 }, { 13,  7 }, { 14,  6 }, { 14,  7 }, {  9, 17 },
    { 14,  8 }, { 15,  7 }, { 15,  8 }, { 11, 16 }, { 15,  9 }, { 15, 10 },
    { 16,  8 }, { 16,  9 }, { 16, 10 }, { 15, 13 }, { 17,  9 }, { 17, 10 },
    { 17, 11 }, { 15, 16 }, { 18, 10 }, { 18, 11 }, { 18, 12 }, { 16, 16 },
    { 19, 11 }, { 19, 12 }, { 19, 13 }, { 17, 17 }, { 20, 12 }, { 20, 13 },
    { 20, 14 }, { 19, 16 }, { 21, 13 }, { 21, 14 }, { 21, 15 }, { 20, 17 },
    { 22, 14 }, { 22, 15 }, { 25, 10 }, { 22, 16 }, { 23, 15 }, { 23, 16 },
    { 26, 11 }, { 23, 17 }, { 24, 16 }, { 24, 17 }, { 27, 12 }, { 24, 18 },
    { 25, 17 }, { 25, 18 }, { 28, 13 }, { 25, 19 }, { 26, 18 }, { 26, 19 },
    { 29, 14 }, { 26, 20 }, { 27, 19 }, { 27, 20 }, { 30, 15 }, { 27, 21 },
    { 28, 20 }, { 28, 21 }, { 28, 21 }, { 28, 22 }, { 29, 21 }, { 29, 22 },
    { 24, 32 }, { 29, 23 }, { 30, 22 }, { 30, 23 }, { 25, 33 }, { 30, 24 },
    { 31, 23 }, { 31, 24 }, { 27, 32 }, { 31, 25 }, { 31, 26 }, { 32, 24 },
    { 32, 25 }, { 32, 26 }, { 31, 29 }, { 33, 25 }, { 33, 26 }, { 33, 27 },
    { 31, 32 }, { 34, 26 }, { 34, 27 }, { 34, 28 }, { 32, 32 }, { 35, 27 },
    { 35, 28 }, { 35, 29 }, { 33, 33 }, { 36, 28 }, { 36, 29 }, { 36, 30 },
    { 35, 32 }, { 37, 29 }, { 37, 30 }, { 37, 31 }, { 36, 33 }, { 38, 30 },
    { 38, 31 }, { 41, 26 }, { 38, 32 }, { 39, 31 }, { 39, 32 }, { 42, 27 },
    { 39, 33 }, { 40, 32 }, { 40, 33 }, { 43, 28 }, { 40, 34 }, { 41, 33 },
    { 41, 34 }, { 44, 29 }, { 41, 35 }, { 42, 34 }, { 42, 35 }, { 45, 30 },
    { 42, 36 }, { 43, 35 }, { 43, 36 }, { 46, 31 }, { 43, 37 }, { 44, 36 },
    { 44, 37 }, { 44, 37 }, { 44, 38 }, { 45, 37 }, { 45, 38 }, { 40, 48 },
    { 45, 39 }, { 46, 38 }, { 46, 39 }, { 41, 49 }, { 46, 40 }, { 47, 39 },
    { 47, 40 }, { 43, 48 }, { 47, 41 }, { 47, 42 }, { 48, 40 }, { 48, 41 },
    { 48, 42 }, { 47, 45 }, { 49, 41 }, { 49, 42 }, { 49, 43 }, { 47, 48 },
    { 50, 42 }, { 50, 43 }, { 50, 44 }, { 48, 48 }, { 51, 43 }, { 51, 44 },
    { 51, 45 }, { 49, 49 }, { 52, 44 }, { 52, 45 }, { 52, 46 }, { 51, 48 },
    { 53, 45 }, { 53, 46 }, { 53, 47 }, { 52, 49 }, { 54, 46 }, { 54, 47 },
    { 57, 42 }, { 54, 48 }, { 55, 47 }, { 55, 48 }, { 58, 43 }, { 55, 49 },
    { 56, 48 }, { 56, 49 }, { 59, 44 }, { 56, 50 }, { 57, 49 }, { 57, 50 },
    { 60, 45 }, { 57, 51 }, { 58, 50 }, { 58, 51 }, { 61, 46 }, { 58, 52 },
    { 59, 51 }, { 59, 52 }, { 62, 47 }, { 59, 53 }, { 60, 52 }, { 60, 53 },
    { 60, 53 }, { 60, 54 }, { 61, 53 }, { 61, 54 }, { 61, 54 }, { 61, 55 },
    { 62, 54 }, { 62, 55 }, { 62, 55 }, { 62, 56 }, { 63, 55 }, { 63, 56 },
    { 63, 56 }, { 63, 57 }, { 63, 58 }, { 63, 59 }, { 63, 59 }, { 63, 60 },
    { 63, 61 }, { 63, 62 }, { 63, 62 }, { 63, 63 },
};

/* Multiplication over 8 bit emulation */
#define mul8(a, b) (((a) * (b) + 128 + (((a) * (b) + 128) >> 8)) >> 8)

/* Conversion from rgb24 to rgb565 */
#define rgb2rgb565(r, g, b) \
    ((mul8(r, 31) << 11) | (mul8(g, 63) << 5) | (mul8(b, 31) << 0))

/* Linear interpolation at 1/3 point between a and b */
#define lerp13(a, b) ((2 * (a) + (b)) / 3)

/* Linear interpolation on an RGB pixel */
static inline void lerp13rgb(uint8_t *out, uint8_t *p1, uint8_t *p2)
{
    out[0] = lerp13(p1[0], p2[0]);
    out[1] = lerp13(p1[1], p2[1]);
    out[2] = lerp13(p1[2], p2[2]);
}

/* Conversion from rgb565 to rgb24 */
static inline void rgb5652rgb(uint8_t *out, uint16_t v)
{
    int rv = (v & 0xf800) >> 11;
    int gv = (v & 0x07e0) >> 5;
    int bv = (v & 0x001f) >> 0;

    out[0] = expand5[rv];
    out[1] = expand6[gv];
    out[2] = expand5[bv];
    out[3] = 0;
}

/* Color matching function */
static unsigned int match_colors(const uint8_t *block, ptrdiff_t stride,
                                 uint16_t c0, uint16_t c1)
{
    uint32_t mask = 0;
    int dirr, dirg, dirb;
    int dots[16];
    int stops[4];
    int x, y, k = 0;
    int c0_point, half_point, c3_point;
    uint8_t color[16];
    static const uint32_t indexMap[8] = {
        0U << 30, 2U << 30, 0U << 30, 2U << 30,
        3U << 30, 3U << 30, 1U << 30, 1U << 30,
    };

    /* Fill color and compute direction for each component */
    rgb5652rgb(color + 0, c0);
    rgb5652rgb(color + 4, c1);
    lerp13rgb(color + 8, color + 0, color + 4);
    lerp13rgb(color + 12, color + 4, color + 0);

    dirr = color[0 * 4 + 0] - color[1 * 4 + 0];
    dirg = color[0 * 4 + 1] - color[1 * 4 + 1];
    dirb = color[0 * 4 + 2] - color[1 * 4 + 2];

    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++)
            dots[k++] = block[0 + x * 4 + y * stride] * dirr +
                        block[1 + x * 4 + y * stride] * dirg +
                        block[2 + x * 4 + y * stride] * dirb;

        stops[y] = color[0 + y * 4] * dirr +
                   color[1 + y * 4] * dirg +
                   color[2 + y * 4] * dirb;
    }

    /* Think of the colors as arranged on a line; project point onto that line,
     * then choose next color out of available ones. we compute the crossover
     * points for 'best color in top half'/'best in bottom half' and then
     * the same inside that subinterval.
     *
     * Relying on this 1d approximation isn't always optimal in terms of
     * Euclidean distance, but it's very close and a lot faster.
     *
     * http://cbloomrants.blogspot.com/2008/12/12-08-08-dxtc-summary.html */
    c0_point   = (stops[1] + stops[3]) >> 1;
    half_point = (stops[3] + stops[2]) >> 1;
    c3_point   = (stops[2] + stops[0]) >> 1;

    for (x = 0; x < 16; x++) {
        int dot  = dots[x];
        int bits = (dot < half_point ? 4 : 0) |
                   (dot < c0_point   ? 2 : 0) |
                   (dot < c3_point   ? 1 : 0);

        mask >>= 2;
        mask  |= indexMap[bits];
    }

    return mask;
}

/* Color optimization function */
static void optimize_colors(const uint8_t *block, ptrdiff_t stride,
                            uint16_t *pmax16, uint16_t *pmin16)
{
    const uint8_t *minp;
    const uint8_t *maxp;
    const int iter_power = 4;
    double magn;
    int v_r, v_g, v_b;
    float covf[6], vfr, vfg, vfb;
    int mind, maxd;
    int cov[6] = { 0 };
    int mu[3], min[3], max[3];
    int ch, iter, x, y;

    /* Determine color distribution */
    for (ch = 0; ch < 3; ch++) {
        const uint8_t *bp = &block[ch];
        int muv, minv, maxv;

        muv = minv = maxv = bp[0];
        for (y = 0; y < 4; y++) {
            for (x = 0; x < 4; x++) {
                muv += bp[x * 4 + y * stride];
                if (bp[x * 4 + y * stride] < minv)
                    minv = bp[x * 4 + y * stride];
                else if (bp[x * 4 + y * stride] > maxv)
                    maxv = bp[x * 4 + y * stride];
            }
        }

        mu[ch]  = (muv + 8) >> 4;
        min[ch] = minv;
        max[ch] = maxv;
    }

    /* Determine covariance matrix */
    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            int r = block[x * 4 + stride * y + 0] - mu[0];
            int g = block[x * 4 + stride * y + 1] - mu[1];
            int b = block[x * 4 + stride * y + 2] - mu[2];

            cov[0] += r * r;
            cov[1] += r * g;
            cov[2] += r * b;
            cov[3] += g * g;
            cov[4] += g * b;
            cov[5] += b * b;
        }
    }

    /* Convert covariance matrix to float, find principal axis via power iter */
    for (x = 0; x < 6; x++)
        covf[x] = cov[x] / 255.0f;

    vfr = (float) (max[0] - min[0]);
    vfg = (float) (max[1] - min[1]);
    vfb = (float) (max[2] - min[2]);

    for (iter = 0; iter < iter_power; iter++) {
        float r = vfr * covf[0] + vfg * covf[1] + vfb * covf[2];
        float g = vfr * covf[1] + vfg * covf[3] + vfb * covf[4];
        float b = vfr * covf[2] + vfg * covf[4] + vfb * covf[5];

        vfr = r;
        vfg = g;
        vfb = b;
    }

    magn = fabs(vfr);
    if (fabs(vfg) > magn)
        magn = fabs(vfg);
    if (fabs(vfb) > magn)
        magn = fabs(vfb);

    /* if magnitude is too small, default to luminance */
    if (magn < 4.0f) {
        /* JPEG YCbCr luma coefs, scaled by 1000 */
        v_r = 299;
        v_g = 587;
        v_b = 114;
    } else {
        magn = 512.0 / magn;
        v_r  = (int) (vfr * magn);
        v_g  = (int) (vfg * magn);
        v_b  = (int) (vfb * magn);
    }

    /* Pick colors at extreme points */
    mind = maxd = block[0] * v_r + block[1] * v_g + block[2] * v_b;
    minp = maxp = block;
    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            int dot = block[x * 4 + y * stride + 0] * v_r +
                      block[x * 4 + y * stride + 1] * v_g +
                      block[x * 4 + y * stride + 2] * v_b;

            if (dot < mind) {
                mind = dot;
                minp = block + x * 4 + y * stride;
            } else if (dot > maxd) {
                maxd = dot;
                maxp = block + x * 4 + y * stride;
            }
        }
    }

    *pmax16 = rgb2rgb565(maxp[0], maxp[1], maxp[2]);
    *pmin16 = rgb2rgb565(minp[0], minp[1], minp[2]);
}

/* Try to optimize colors to suit block contents better, by solving
 * a least squares system via normal equations + Cramer's rule. */
static int refine_colors(const uint8_t *block, ptrdiff_t stride,
                         uint16_t *pmax16, uint16_t *pmin16, uint32_t mask)
{
    uint32_t cm = mask;
    uint16_t oldMin = *pmin16;
    uint16_t oldMax = *pmax16;
    uint16_t min16, max16;
    int x, y;

    /* Additional magic to save a lot of multiplies in the accumulating loop.
     * The tables contain precomputed products of weights for least squares
     * system, accumulated inside one 32-bit register */
    static const int w1tab[4] = { 3, 0, 2, 1 };
    static const int prods[4] = { 0x090000, 0x000900, 0x040102, 0x010402 };

    /* Check if all pixels have the same index */
    if ((mask ^ (mask << 2)) < 4) {
        /* If so, linear system would be singular; solve using optimal
         * single-color match on average color. */
        int r = 8, g = 8, b = 8;
        for (y = 0; y < 4; y++) {
            for (x = 0; x < 4; x++) {
                r += block[0 + x * 4 + y * stride];
                g += block[1 + x * 4 + y * stride];
                b += block[2 + x * 4 + y * stride];
            }
        }

        r >>= 4;
        g >>= 4;
        b >>= 4;

        max16 = (match5[r][0] << 11) | (match6[g][0] << 5) | match5[b][0];
        min16 = (match5[r][1] << 11) | (match6[g][1] << 5) | match5[b][1];
    } else {
        float fr, fg, fb;
        int at1_r = 0, at1_g = 0, at1_b = 0;
        int at2_r = 0, at2_g = 0, at2_b = 0;
        int akku = 0;
        int xx, xy, yy;

        for (y = 0; y < 4; y++) {
            for (x = 0; x < 4; x++) {
                int step = cm & 3;
                int w1 = w1tab[step];
                int r = block[0 + x * 4 + y * stride];
                int g = block[1 + x * 4 + y * stride];
                int b = block[2 + x * 4 + y * stride];

                akku  += prods[step];
                at1_r += w1 * r;
                at1_g += w1 * g;
                at1_b += w1 * b;
                at2_r += r;
                at2_g += g;
                at2_b += b;

                cm >>= 2;
            }
        }

        at2_r = 3 * at2_r - at1_r;
        at2_g = 3 * at2_g - at1_g;
        at2_b = 3 * at2_b - at1_b;

        /* Extract solutions and decide solvability */
        xx =  akku >> 16;
        yy = (akku >>  8) & 0xFF;
        xy = (akku >>  0) & 0xFF;

        fr = 3.0f * 31.0f / 255.0f / (xx * yy - xy * xy);
        fg = fr * 63.0f / 31.0f;
        fb = fr;

        /* Solve */
        max16  = av_clip_uintp2((at1_r * yy - at2_r * xy) * fr + 0.5f, 5) << 11;
        max16 |= av_clip_uintp2((at1_g * yy - at2_g * xy) * fg + 0.5f, 6) <<  5;
        max16 |= av_clip_uintp2((at1_b * yy - at2_b * xy) * fb + 0.5f, 5) <<  0;

        min16  = av_clip_uintp2((at2_r * xx - at1_r * xy) * fr + 0.5f, 5) << 11;
        min16 |= av_clip_uintp2((at2_g * xx - at1_g * xy) * fg + 0.5f, 6) <<  5;
        min16 |= av_clip_uintp2((at2_b * xx - at1_b * xy) * fb + 0.5f, 5) <<  0;
    }

    *pmin16 = min16;
    *pmax16 = max16;
    return oldMin != min16 || oldMax != max16;
}

/* Check if input block is a constant color */
static int constant_color(const uint8_t *block, ptrdiff_t stride)
{
    int x, y;
    uint32_t first = AV_RL32(block);

    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            if (first != AV_RL32(block + x * 4 + y * stride))
                return 0;
    return 1;
}

/* Main color compression function */
static void compress_color(uint8_t *dst, ptrdiff_t stride, const uint8_t *block)
{
    uint32_t mask;
    uint16_t max16, min16;
    int constant = constant_color(block, stride);

    /* Constant color will load values from tables */
    if (constant) {
        int r = block[0];
        int g = block[1];
        int b = block[2];
        mask  = 0xAAAAAAAA;
        max16 = (match5[r][0] << 11) | (match6[g][0] << 5) | match5[b][0];
        min16 = (match5[r][1] << 11) | (match6[g][1] << 5) | match5[b][1];
    } else {
        int refine;

        /* Otherwise find pca and map along principal axis */
        optimize_colors(block, stride, &max16, &min16);
        if (max16 != min16)
            mask = match_colors(block, stride, max16, min16);
        else
            mask = 0;

        /* One pass refinement */
        refine  = refine_colors(block, stride, &max16, &min16, mask);
        if (refine) {
            if (max16 != min16)
                mask = match_colors(block, stride, max16, min16);
            else
                mask = 0;
        }
    }

    /* Finally write the color block */
    if (max16 < min16) {
        FFSWAP(uint16_t, min16, max16);
        mask ^= 0x55555555;
    }

    AV_WL16(dst + 0, max16);
    AV_WL16(dst + 2, min16);
    AV_WL32(dst + 4, mask);
}

/* Alpha compression function */
static void compress_alpha(uint8_t *dst, ptrdiff_t stride, const uint8_t *block)
{
    int x, y;
    int dist, bias, dist4, dist2;
    int mn, mx;
    int bits = 0;
    int mask = 0;

    memset(dst, 0, 8);

    /* Find min/max color */
    mn = mx = block[3];
    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            int val = block[3 + x * 4 + y * stride];
            if (val < mn)
                mn = val;
            else if (val > mx)
                mx = val;
        }
    }

    /* Encode them */
    dst[0] = (uint8_t) mx;
    dst[1] = (uint8_t) mn;
    dst += 2;

    /* Mono-alpha shortcut */
    if (mn == mx)
        return;

    /* Determine bias and emit color indices.
     * Given the choice of mx/mn, these indices are optimal:
     * fgiesen.wordpress.com/2009/12/15/dxt5-alpha-block-index-determination */
    dist = mx - mn;

    dist4 = dist * 4;
    dist2 = dist * 2;
    if (dist < 8)
        bias = dist - 1 - mn * 7;
    else
        bias = dist / 2 + 2 - mn * 7;

    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            int alp = block[3 + x * 4 + y * stride] * 7 + bias;
            int ind, tmp;

            /* This is a "linear scale" lerp factor between 0 (val=min)
             * and 7 (val=max) to select index. */
            tmp  = (alp >= dist4) ? -1 : 0;
            ind  = tmp & 4;
            alp -= dist4 & tmp;
            tmp  = (alp >= dist2) ? -1 : 0;
            ind += tmp & 2;
            alp -= dist2 & tmp;
            ind += (alp >= dist);

            /* Turn linear scale into DXT index (0/1 are extreme points) */
            ind  = -ind & 7;
            ind ^= (2 > ind);

            /* Write index */
            mask |= ind << bits;
            bits += 3;
            if (bits >= 8) {
                *dst++ = mask;
                mask >>= 8;
                bits  -= 8;
            }
        }
    }
}

/**
 * Convert a RGBA buffer to unscaled YCoCg.
 * Scale is usually introduced to avoid banding over a certain range of colors,
 * but this version of the algorithm does not introduce it as much as other
 * implementations, allowing for a simpler and faster conversion.
 */
static void rgba2ycocg(uint8_t *dst, const uint8_t *pixel)
{
    int r =  pixel[0];
    int g = (pixel[1] + 1) >> 1;
    int b =  pixel[2];
    int t = (2 + r + b) >> 2;

    dst[0] = av_clip_uint8(128 + ((r - b + 1) >> 1));   /* Co */
    dst[1] = av_clip_uint8(128 + g - t);                /* Cg */
    dst[2] = 0;
    dst[3] = av_clip_uint8(g + t);                      /* Y */
}

/**
 * Compress one block of RGBA pixels in a DXT1 texture and store the
 * resulting bytes in 'dst'. Alpha is not preserved.
 *
 * @param dst    output buffer.
 * @param stride scanline in bytes.
 * @param block  block to compress.
 * @return how much texture data has been written.
 */
static int dxt1_block(uint8_t *dst, ptrdiff_t stride, const uint8_t *block)
{
    compress_color(dst, stride, block);

    return 8;
}

/**
 * Compress one block of RGBA pixels in a DXT5 texture and store the
 * resulting bytes in 'dst'. Alpha is preserved.
 *
 * @param dst    output buffer.
 * @param stride scanline in bytes.
 * @param block  block to compress.
 * @return how much texture data has been written.
 */
static int dxt5_block(uint8_t *dst, ptrdiff_t stride, const uint8_t *block)
{
    compress_alpha(dst, stride, block);
    compress_color(dst + 8, stride, block);

    return 16;
}

/**
 * Compress one block of RGBA pixels in a DXT5-YCoCg texture and store the
 * resulting bytes in 'dst'. Alpha is not preserved.
 *
 * @param dst    output buffer.
 * @param stride scanline in bytes.
 * @param block  block to compress.
 * @return how much texture data has been written.
 */
static int dxt5ys_block(uint8_t *dst, ptrdiff_t stride, const uint8_t *block)
{
    int x, y;
    uint8_t reorder[64];

    /* Reorder the components and then run a normal DXT5 compression. */
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            rgba2ycocg(reorder + x * 4 + y * 16, block + x * 4 + y * stride);

    compress_alpha(dst + 0, 16, reorder);
    compress_color(dst + 8, 16, reorder);

    return 16;
}

av_cold void ff_texturedspenc_init(TextureDSPEncContext *c)
{
    c->dxt1_block         = dxt1_block;
    c->dxt5_block         = dxt5_block;
    c->dxt5ys_block       = dxt5ys_block;
}

#define TEXTUREDSP_FUNC_NAME ff_texturedsp_exec_compress_threads
#define TEXTUREDSP_TEX_FUNC(a, b, c) tex_funct(c, b, a)
#include "texturedsp_template.c"
