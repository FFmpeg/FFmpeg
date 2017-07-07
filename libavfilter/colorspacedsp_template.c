/*
 * Copyright (c) 2016 Ronald S. Bultje <rsbultje@gmail.com>
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

#include "libavutil/avassert.h"

#undef avg
#undef ss

#if SS_W == 0
#define ss 444
#define avg(a,b,c,d) (a)
#elif SS_H == 0
#define ss 422
#define avg(a,b,c,d) (((a) + (b) + 1) >> 1)
#else
#define ss 420
#define avg(a,b,c,d) (((a) + (b) + (c) + (d) + 2) >> 2)
#endif

#undef fn
#undef fn2
#undef fn3
#define fn3(a,b,c) a##_##c##p##b##_c
#define fn2(a,b,c) fn3(a,b,c)
#define fn(a) fn2(a, BIT_DEPTH, ss)

#undef pixel
#undef av_clip_pixel
#if BIT_DEPTH == 8
#define pixel uint8_t
#define av_clip_pixel(x) av_clip_uint8(x)
#else
#define pixel uint16_t
#define av_clip_pixel(x) av_clip_uintp2(x, BIT_DEPTH)
#endif

static void fn(yuv2rgb)(int16_t *rgb[3], ptrdiff_t rgb_stride,
                        uint8_t *_yuv[3], const ptrdiff_t yuv_stride[3],
                        int w, int h, const int16_t yuv2rgb_coeffs[3][3][8],
                        const int16_t yuv_offset[8])
{
    pixel **yuv = (pixel **) _yuv;
    const pixel *yuv0 = yuv[0], *yuv1 = yuv[1], *yuv2 = yuv[2];
    int16_t *rgb0 = rgb[0], *rgb1 = rgb[1], *rgb2 = rgb[2];
    int y, x;
    int cy = yuv2rgb_coeffs[0][0][0];
    int crv = yuv2rgb_coeffs[0][2][0];
    int cgu = yuv2rgb_coeffs[1][1][0];
    int cgv = yuv2rgb_coeffs[1][2][0];
    int cbu = yuv2rgb_coeffs[2][1][0];
    const int sh = BIT_DEPTH - 1, rnd = 1 << (sh - 1);
    const int uv_offset = 128 << (BIT_DEPTH - 8);

    av_assert2(yuv2rgb_coeffs[0][1][0] == 0);
    av_assert2(yuv2rgb_coeffs[2][2][0] == 0);
    av_assert2(yuv2rgb_coeffs[1][0][0] == cy && yuv2rgb_coeffs[2][0][0] == cy);

    w = AV_CEIL_RSHIFT(w, SS_W);
    h = AV_CEIL_RSHIFT(h, SS_H);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int y00 = yuv0[x << SS_W] - yuv_offset[0];
#if SS_W == 1
            int y01 = yuv0[2 * x + 1] - yuv_offset[0];
#if SS_H == 1
            int y10 = yuv0[yuv_stride[0] / sizeof(pixel) + 2 * x] - yuv_offset[0];
            int y11 = yuv0[yuv_stride[0] / sizeof(pixel) + 2 * x + 1] - yuv_offset[0];
#endif
#endif
            int u = yuv1[x] - uv_offset, v = yuv2[x] - uv_offset;

            rgb0[x << SS_W]              = av_clip_int16((y00 * cy + crv * v + rnd) >> sh);
#if SS_W == 1
            rgb0[2 * x + 1]              = av_clip_int16((y01 * cy + crv * v + rnd) >> sh);
#if SS_H == 1
            rgb0[2 * x + rgb_stride]     = av_clip_int16((y10 * cy + crv * v + rnd) >> sh);
            rgb0[2 * x + rgb_stride + 1] = av_clip_int16((y11 * cy + crv * v + rnd) >> sh);
#endif
#endif

            rgb1[x << SS_W]              = av_clip_int16((y00 * cy + cgu * u +
                                                          cgv * v + rnd) >> sh);
#if SS_W == 1
            rgb1[2 * x + 1]              = av_clip_int16((y01 * cy + cgu * u +
                                                          cgv * v + rnd) >> sh);
#if SS_H == 1
            rgb1[2 * x + rgb_stride]     = av_clip_int16((y10 * cy + cgu * u +
                                                          cgv * v + rnd) >> sh);
            rgb1[2 * x + rgb_stride + 1] = av_clip_int16((y11 * cy + cgu * u +
                                                          cgv * v + rnd) >> sh);
#endif
#endif

            rgb2[x << SS_W]              = av_clip_int16((y00 * cy + cbu * u + rnd) >> sh);
#if SS_W == 1
            rgb2[2 * x + 1]              = av_clip_int16((y01 * cy + cbu * u + rnd) >> sh);
#if SS_H == 1
            rgb2[2 * x + rgb_stride]     = av_clip_int16((y10 * cy + cbu * u + rnd) >> sh);
            rgb2[2 * x + rgb_stride + 1] = av_clip_int16((y11 * cy + cbu * u + rnd) >> sh);
#endif
#endif
        }

        yuv0 += (yuv_stride[0] * (1 << SS_H)) / sizeof(pixel);
        yuv1 += yuv_stride[1] / sizeof(pixel);
        yuv2 += yuv_stride[2] / sizeof(pixel);
        rgb0 += rgb_stride * (1 << SS_H);
        rgb1 += rgb_stride * (1 << SS_H);
        rgb2 += rgb_stride * (1 << SS_H);
    }
}

static void fn(rgb2yuv)(uint8_t *_yuv[3], const ptrdiff_t yuv_stride[3],
                        int16_t *rgb[3], ptrdiff_t s,
                        int w, int h, const int16_t rgb2yuv_coeffs[3][3][8],
                        const int16_t yuv_offset[8])
{
    pixel **yuv = (pixel **) _yuv;
    pixel *yuv0 = yuv[0], *yuv1 = yuv[1], *yuv2 = yuv[2];
    const int16_t *rgb0 = rgb[0], *rgb1 = rgb[1], *rgb2 = rgb[2];
    int y, x;
    const int sh = 29 - BIT_DEPTH;
    const int rnd = 1 << (sh - 1);
    int cry = rgb2yuv_coeffs[0][0][0];
    int cgy = rgb2yuv_coeffs[0][1][0];
    int cby = rgb2yuv_coeffs[0][2][0];
    int cru = rgb2yuv_coeffs[1][0][0];
    int cgu = rgb2yuv_coeffs[1][1][0];
    int cburv = rgb2yuv_coeffs[1][2][0];
    int cgv = rgb2yuv_coeffs[2][1][0];
    int cbv = rgb2yuv_coeffs[2][2][0];
    ptrdiff_t s0 = yuv_stride[0] / sizeof(pixel);
    const int uv_offset = 128 << (BIT_DEPTH - 8);

    av_assert2(rgb2yuv_coeffs[1][2][0] == rgb2yuv_coeffs[2][0][0]);
    w = AV_CEIL_RSHIFT(w, SS_W);
    h = AV_CEIL_RSHIFT(h, SS_H);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int r00 = rgb0[x << SS_W], g00 = rgb1[x << SS_W], b00 = rgb2[x << SS_W];
#if SS_W == 1
            int r01 = rgb0[x * 2 + 1], g01 = rgb1[x * 2 + 1], b01 = rgb2[x * 2 + 1];
#if SS_H == 1
            int r10 = rgb0[x * 2 + 0 + s], g10 = rgb1[x * 2 + 0 + s], b10 = rgb2[x * 2 + 0 + s];
            int r11 = rgb0[x * 2 + 1 + s], g11 = rgb1[x * 2 + 1 + s], b11 = rgb2[x * 2 + 1 + s];
#endif
#endif

            yuv0[x << SS_W]      = av_clip_pixel(yuv_offset[0] +
                                                 ((r00 * cry + g00 * cgy +
                                                   b00 * cby + rnd) >> sh));
#if SS_W == 1
            yuv0[x * 2 + 1]      = av_clip_pixel(yuv_offset[0] +
                                                 ((r01 * cry + g01 * cgy +
                                                   b01 * cby + rnd) >> sh));
#if SS_H == 1
            yuv0[x * 2 + 0 + s0] = av_clip_pixel(yuv_offset[0] +
                                                 ((r10 * cry + g10 * cgy +
                                                   b10 * cby + rnd) >> sh));
            yuv0[x * 2 + 1 + s0] = av_clip_pixel(yuv_offset[0] +
                                                 ((r11 * cry + g11 * cgy +
                                                   b11 * cby + rnd) >> sh));
#endif
#endif

            yuv1[x]      = av_clip_pixel(uv_offset +
                                         ((avg(r00, r01, r10, r11) * cru +
                                           avg(g00, g01, g10, g11) * cgu +
                                           avg(b00, b01, b10, b11) * cburv + rnd) >> sh));
            yuv2[x]      = av_clip_pixel(uv_offset +
                                         ((avg(r00, r01, r10, r11) * cburv +
                                           avg(g00, g01, g10, g11) * cgv +
                                           avg(b00, b01, b10, b11) * cbv + rnd) >> sh));
        }

        yuv0 += s0 * (1 << SS_H);
        yuv1 += yuv_stride[1] / sizeof(pixel);
        yuv2 += yuv_stride[2] / sizeof(pixel);
        rgb0 += s * (1 << SS_H);
        rgb1 += s * (1 << SS_H);
        rgb2 += s * (1 << SS_H);
    }
}

/* floyd-steinberg dithering - for any mid-top pixel A in a 3x2 block of pixels:
 *    1 A 2
 *    3 4 5
 * the rounding error is distributed over the neighbouring pixels:
 *    2: 7/16th, 3: 3/16th, 4: 5/16th and 5: 1/16th
 */
static void fn(rgb2yuv_fsb)(uint8_t *_yuv[3], const ptrdiff_t yuv_stride[3],
                            int16_t *rgb[3], ptrdiff_t s,
                            int w, int h, const int16_t rgb2yuv_coeffs[3][3][8],
                            const int16_t yuv_offset[8],
                            int *rnd_scratch[3][2])
{
    pixel **yuv = (pixel **) _yuv;
    pixel *yuv0 = yuv[0], *yuv1 = yuv[1], *yuv2 = yuv[2];
    const int16_t *rgb0 = rgb[0], *rgb1 = rgb[1], *rgb2 = rgb[2];
    int y, x;
    const int sh = 29 - BIT_DEPTH;
    const int rnd = 1 << (sh - 1);
    int cry = rgb2yuv_coeffs[0][0][0];
    int cgy = rgb2yuv_coeffs[0][1][0];
    int cby = rgb2yuv_coeffs[0][2][0];
    int cru = rgb2yuv_coeffs[1][0][0];
    int cgu = rgb2yuv_coeffs[1][1][0];
    int cburv = rgb2yuv_coeffs[1][2][0];
    int cgv = rgb2yuv_coeffs[2][1][0];
    int cbv = rgb2yuv_coeffs[2][2][0];
    ptrdiff_t s0 = yuv_stride[0] / sizeof(pixel);
    const int uv_offset = 128 << (BIT_DEPTH - 8);
    unsigned mask = (1 << sh) - 1;

    for (x = 0; x < w; x++) {
        rnd_scratch[0][0][x] =
        rnd_scratch[0][1][x] = rnd;
    }
    av_assert2(rgb2yuv_coeffs[1][2][0] == rgb2yuv_coeffs[2][0][0]);
    w = AV_CEIL_RSHIFT(w, SS_W);
    h = AV_CEIL_RSHIFT(h, SS_H);
    for (x = 0; x < w; x++) {
        rnd_scratch[1][0][x] =
        rnd_scratch[1][1][x] =
        rnd_scratch[2][0][x] =
        rnd_scratch[2][1][x] = rnd;
    }
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int r00 = rgb0[x << SS_W], g00 = rgb1[x << SS_W], b00 = rgb2[x << SS_W];
            int y00;
#if SS_W == 1
            int r01 = rgb0[x * 2 + 1], g01 = rgb1[x * 2 + 1], b01 = rgb2[x * 2 + 1];
            int y01;
#if SS_H == 1
            int r10 = rgb0[x * 2 + 0 + s], g10 = rgb1[x * 2 + 0 + s], b10 = rgb2[x * 2 + 0 + s];
            int r11 = rgb0[x * 2 + 1 + s], g11 = rgb1[x * 2 + 1 + s], b11 = rgb2[x * 2 + 1 + s];
            int y10, y11;
#endif
#endif
            int u, v, diff;

            y00 = r00 * cry + g00 * cgy + b00 * cby + rnd_scratch[0][y & !SS_H][x << SS_W];
            diff = (y00 & mask) - rnd;
            yuv0[x << SS_W]      = av_clip_pixel(yuv_offset[0] + (y00 >> sh));
            rnd_scratch[0][ (y & !SS_H)][(x << SS_W) + 1] += (diff * 7 + 8) >> 4;
            rnd_scratch[0][!(y & !SS_H)][(x << SS_W) - 1] += (diff * 3 + 8) >> 4;
            rnd_scratch[0][!(y & !SS_H)][(x << SS_W) + 0] += (diff * 5 + 8) >> 4;
            rnd_scratch[0][!(y & !SS_H)][(x << SS_W) + 1] += (diff * 1 + 8) >> 4;
            rnd_scratch[0][ (y & !SS_H)][(x << SS_W) + 0]  = rnd;
#if SS_W == 1
            y01 = r01 * cry + g01 * cgy + b01 * cby + rnd_scratch[0][y & !SS_H][x * 2 + 1];
            diff = (y01 & mask) - rnd;
            yuv0[x * 2 + 1]      = av_clip_pixel(yuv_offset[0] + (y01 >> sh));
            rnd_scratch[0][ (y & !SS_H)][x * 2 + 2] += (diff * 7 + 8) >> 4;
            rnd_scratch[0][!(y & !SS_H)][x * 2 + 0] += (diff * 3 + 8) >> 4;
            rnd_scratch[0][!(y & !SS_H)][x * 2 + 1] += (diff * 5 + 8) >> 4;
            rnd_scratch[0][!(y & !SS_H)][x * 2 + 2] += (diff * 1 + 8) >> 4;
            rnd_scratch[0][ (y & !SS_H)][x * 2 + 1]  = rnd;
#if SS_H == 1
            y10 = r10 * cry + g10 * cgy + b10 * cby + rnd_scratch[0][1][x * 2 + 0];
            diff = (y10 & mask) - rnd;
            yuv0[x * 2 + 0 + s0] = av_clip_pixel(yuv_offset[0] + (y10 >> sh));
            rnd_scratch[0][1][x * 2 + 1] += (diff * 7 + 8) >> 4;
            rnd_scratch[0][0][x * 2 - 1] += (diff * 3 + 8) >> 4;
            rnd_scratch[0][0][x * 2 + 0] += (diff * 5 + 8) >> 4;
            rnd_scratch[0][0][x * 2 + 1] += (diff * 1 + 8) >> 4;
            rnd_scratch[0][1][x * 2 + 0]  = rnd;

            y11 = r11 * cry + g11 * cgy + b11 * cby + rnd_scratch[0][1][x * 2 + 1];
            diff = (y11 & mask) - rnd;
            yuv0[x * 2 + 1 + s0] = av_clip_pixel(yuv_offset[0] + (y11 >> sh));
            rnd_scratch[0][1][x * 2 + 2] += (diff * 7 + 8) >> 4;
            rnd_scratch[0][0][x * 2 + 0] += (diff * 3 + 8) >> 4;
            rnd_scratch[0][0][x * 2 + 1] += (diff * 5 + 8) >> 4;
            rnd_scratch[0][0][x * 2 + 2] += (diff * 1 + 8) >> 4;
            rnd_scratch[0][1][x * 2 + 1]  = rnd;
#endif
#endif

            u = avg(r00, r01, r10, r11) * cru +
                avg(g00, g01, g10, g11) * cgu +
                avg(b00, b01, b10, b11) * cburv + rnd_scratch[1][y & 1][x];
            diff = (u & mask) - rnd;
            yuv1[x] = av_clip_pixel(uv_offset + (u >> sh));
            rnd_scratch[1][ (y & 1)][x + 1] += (diff * 7 + 8) >> 4;
            rnd_scratch[1][!(y & 1)][x - 1] += (diff * 3 + 8) >> 4;
            rnd_scratch[1][!(y & 1)][x + 0] += (diff * 5 + 8) >> 4;
            rnd_scratch[1][!(y & 1)][x + 1] += (diff * 1 + 8) >> 4;
            rnd_scratch[1][ (y & 1)][x + 0]  = rnd;

            v = avg(r00, r01, r10, r11) * cburv +
                avg(g00, g01, g10, g11) * cgv +
                avg(b00, b01, b10, b11) * cbv + rnd_scratch[2][y & 1][x];
            diff = (v & mask) - rnd;
            yuv2[x] = av_clip_pixel(uv_offset + (v >> sh));
            rnd_scratch[2][ (y & 1)][x + 1] += (diff * 7 + 8) >> 4;
            rnd_scratch[2][!(y & 1)][x - 1] += (diff * 3 + 8) >> 4;
            rnd_scratch[2][!(y & 1)][x + 0] += (diff * 5 + 8) >> 4;
            rnd_scratch[2][!(y & 1)][x + 1] += (diff * 1 + 8) >> 4;
            rnd_scratch[2][ (y & 1)][x + 0]  = rnd;
        }

        yuv0 += s0 * (1 << SS_H);
        yuv1 += yuv_stride[1] / sizeof(pixel);
        yuv2 += yuv_stride[2] / sizeof(pixel);
        rgb0 += s * (1 << SS_H);
        rgb1 += s * (1 << SS_H);
        rgb2 += s * (1 << SS_H);
    }
}

#undef IN_BIT_DEPTH
#undef OUT_BIT_DEPTH
#define OUT_BIT_DEPTH BIT_DEPTH
#define IN_BIT_DEPTH 8
#include "colorspacedsp_yuv2yuv_template.c"

#undef IN_BIT_DEPTH
#define IN_BIT_DEPTH 10
#include "colorspacedsp_yuv2yuv_template.c"

#undef IN_BIT_DEPTH
#define IN_BIT_DEPTH 12
#include "colorspacedsp_yuv2yuv_template.c"
