/*
 * Copyright (c) 2016 Ronald S. Bultje <rsbultje@gmail.com>
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

#include "colorspace.h"


void invert_matrix3x3(const double in[3][3], double out[3][3])
{
    double m00 = in[0][0], m01 = in[0][1], m02 = in[0][2],
           m10 = in[1][0], m11 = in[1][1], m12 = in[1][2],
           m20 = in[2][0], m21 = in[2][1], m22 = in[2][2];
    int i, j;
    double det;

    out[0][0] =  (m11 * m22 - m21 * m12);
    out[0][1] = -(m01 * m22 - m21 * m02);
    out[0][2] =  (m01 * m12 - m11 * m02);
    out[1][0] = -(m10 * m22 - m20 * m12);
    out[1][1] =  (m00 * m22 - m20 * m02);
    out[1][2] = -(m00 * m12 - m10 * m02);
    out[2][0] =  (m10 * m21 - m20 * m11);
    out[2][1] = -(m00 * m21 - m20 * m01);
    out[2][2] =  (m00 * m11 - m10 * m01);

    det = m00 * out[0][0] + m10 * out[0][1] + m20 * out[0][2];
    det = 1.0 / det;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++)
            out[i][j] *= det;
    }
}

void mul3x3(double dst[3][3], const double src1[3][3], const double src2[3][3])
{
    int m, n;

    for (m = 0; m < 3; m++)
        for (n = 0; n < 3; n++)
            dst[m][n] = src2[m][0] * src1[0][n] +
                        src2[m][1] * src1[1][n] +
                        src2[m][2] * src1[2][n];
}
/*
 * see e.g. http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
 */
void fill_rgb2xyz_table(const struct PrimaryCoefficients *coeffs,
                        const struct WhitepointCoefficients *wp,
                        double rgb2xyz[3][3])
{
    double i[3][3], sr, sg, sb, zw;

    rgb2xyz[0][0] = coeffs->xr / coeffs->yr;
    rgb2xyz[0][1] = coeffs->xg / coeffs->yg;
    rgb2xyz[0][2] = coeffs->xb / coeffs->yb;
    rgb2xyz[1][0] = rgb2xyz[1][1] = rgb2xyz[1][2] = 1.0;
    rgb2xyz[2][0] = (1.0 - coeffs->xr - coeffs->yr) / coeffs->yr;
    rgb2xyz[2][1] = (1.0 - coeffs->xg - coeffs->yg) / coeffs->yg;
    rgb2xyz[2][2] = (1.0 - coeffs->xb - coeffs->yb) / coeffs->yb;
    invert_matrix3x3(rgb2xyz, i);
    zw = 1.0 - wp->xw - wp->yw;
    sr = i[0][0] * wp->xw + i[0][1] * wp->yw + i[0][2] * zw;
    sg = i[1][0] * wp->xw + i[1][1] * wp->yw + i[1][2] * zw;
    sb = i[2][0] * wp->xw + i[2][1] * wp->yw + i[2][2] * zw;
    rgb2xyz[0][0] *= sr;
    rgb2xyz[0][1] *= sg;
    rgb2xyz[0][2] *= sb;
    rgb2xyz[1][0] *= sr;
    rgb2xyz[1][1] *= sg;
    rgb2xyz[1][2] *= sb;
    rgb2xyz[2][0] *= sr;
    rgb2xyz[2][1] *= sg;
    rgb2xyz[2][2] *= sb;
}
