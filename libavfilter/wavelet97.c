/*
 * Copyright (c) 2007 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2013 Clément Bœsch <u pkh me>
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

#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "wavelet97.h"

const double ff_wavelet97_coeff[2][5] = {
    {
         0.6029490182363579  * M_SQRT2,
         0.2668641184428723  * M_SQRT2,
        -0.07822326652898785 * M_SQRT2,
        -0.01686411844287495 * M_SQRT2,
         0.02674875741080976 * M_SQRT2,
    },{
         1.115087052456994   / M_SQRT2,
        -0.5912717631142470  / M_SQRT2,
        -0.05754352622849957 / M_SQRT2,
         0.09127176311424948 / M_SQRT2,
    }
};

static const double icoeff[2][5] = {
    {
         1.115087052456994   / M_SQRT2,
         0.5912717631142470  / M_SQRT2,
        -0.05754352622849957 / M_SQRT2,
        -0.09127176311424948 / M_SQRT2,
    },{
         0.6029490182363579  * M_SQRT2,
        -0.2668641184428723  * M_SQRT2,
        -0.07822326652898785 * M_SQRT2,
         0.01686411844287495 * M_SQRT2,
         0.02674875741080976 * M_SQRT2,
    }
};

static inline void decompose(float *dst_l, float *dst_h, const float *src,
                             const double coeff[2][5], int linesize, int w)
{
    int x, i;
    for (x = 0; x < w; x++) {
        double sum_l = src[x * linesize] * coeff[0][0];
        double sum_h = src[x * linesize] * coeff[1][0];
        for (i = 1; i <= 4; i++) {
            const double s = src[avpriv_mirror(x - i, w - 1) * linesize]
                           + src[avpriv_mirror(x + i, w - 1) * linesize];

            sum_l += coeff[0][i] * s;
            sum_h += coeff[1][i] * s;
        }
        dst_l[x * linesize] = sum_l;
        dst_h[x * linesize] = sum_h;
    }
}

static inline void compose(float *dst, const float *src_l, const float *src_h,
                           int linesize, int w)
{
    int x, i;
    for (x = 0; x < w; x++) {
        double sum_l = src_l[x * linesize] * icoeff[0][0];
        double sum_h = src_h[x * linesize] * icoeff[1][0];
        for (i = 1; i <= 4; i++) {
            const int x0 = avpriv_mirror(x - i, w - 1) * linesize;
            const int x1 = avpriv_mirror(x + i, w - 1) * linesize;

            sum_l += icoeff[0][i] * (src_l[x0] + src_l[x1]);
            sum_h += icoeff[1][i] * (src_h[x0] + src_h[x1]);
        }
        dst[x * linesize] = (sum_l + sum_h) * 0.5;
    }
}

static inline void decompose_decimated(float *dst, const float *src,
                                       const double coeff[2][5], int stride, int n)
{
    int x, i;
    for (x = 0; x < n; x++) {
        const double *c = coeff[x & 1];
        double sum = src[x * stride] * c[0];
        for (i = 1; i <= 4; i++)
            sum += c[i] * (src[avpriv_mirror(x - i, n - 1) * stride]
                         + src[avpriv_mirror(x + i, n - 1) * stride]);
        dst[x * stride] = sum;
    }
}

static inline void compose_decimated(float *dst, const float *src,
                                     int stride, int n)
{
    int x, i;
    for (x = 0; x < n; x++) {
        double sum = src[x * stride] * icoeff[x & 1][0];
        for (i = 1; i <= 4; i++)
            sum += icoeff[(x + i) & 1][i] * (src[avpriv_mirror(x - i, n - 1) * stride]
                                           + src[avpriv_mirror(x + i, n - 1) * stride]);
        dst[x * stride] = sum;
    }
}

void ff_wavelet97_decompose_lines(float *dst_l, float *dst_h, const float *src,
                                  const double coeff[2][5],
                                  int xlinesize, int ylinesize,
                                  int step, int w, int line0, int line1)
{
    int y, x;
    for (y = line0; y < line1; y++)
        for (x = 0; x < step; x++)
            decompose(dst_l + ylinesize*y + xlinesize*x,
                      dst_h + ylinesize*y + xlinesize*x,
                      src   + ylinesize*y + xlinesize*x,
                      coeff, step * xlinesize, (w - x + step - 1) / step);
}

void ff_wavelet97_compose_lines(float *dst, const float *src_l, const float *src_h,
                                int xlinesize, int ylinesize,
                                int step, int w, int line0, int line1)
{
    int y, x;
    for (y = line0; y < line1; y++)
        for (x = 0; x < step; x++)
            compose(dst   + ylinesize*y + xlinesize*x,
                    src_l + ylinesize*y + xlinesize*x,
                    src_h + ylinesize*y + xlinesize*x,
                    step * xlinesize, (w - x + step - 1) / step);
}

void ff_wavelet97_decompose_decimated_lines(float *dst, const float *src,
                                            const double coeff[2][5],
                                            int xstride, int ystride,
                                            int len, int line0, int line1)
{
    for (int y = line0; y < line1; y++)
        decompose_decimated(dst + ystride*y, src + ystride*y, coeff, xstride, len);
}

void ff_wavelet97_compose_decimated_lines(float *dst, const float *src,
                                          int xstride, int ystride,
                                          int len, int line0, int line1)
{
    for (int y = line0; y < line1; y++)
        compose_decimated(dst + ystride*y, src + ystride*y, xstride, len);
}

void ff_wavelet97_decompose(float *dst[4], const float *src, float *temp[2],
                            const double coeff[2][5],
                            int linesize, int step, int w, int h)
{
    ff_wavelet97_decompose_lines(temp[0], temp[1], src,     coeff, 1, linesize, step, w, 0, h);
    ff_wavelet97_decompose_lines( dst[0],  dst[1], temp[0], coeff, linesize, 1, step, h, 0, w);
    ff_wavelet97_decompose_lines( dst[2],  dst[3], temp[1], coeff, linesize, 1, step, h, 0, w);
}

void ff_wavelet97_compose(float *dst, float *const src[4], float *temp[2],
                          int linesize, int step, int w, int h)
{
    ff_wavelet97_compose_lines(temp[0],  src[0],  src[1], linesize, 1, step, h, 0, w);
    ff_wavelet97_compose_lines(temp[1],  src[2],  src[3], linesize, 1, step, h, 0, w);
    ff_wavelet97_compose_lines(dst,     temp[0], temp[1], 1, linesize, step, w, 0, h);
}
