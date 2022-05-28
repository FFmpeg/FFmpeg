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

#include "libavutil/frame.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/pixdesc.h"

#include "colorspace.h"


void ff_matrix_invert_3x3(const double in[3][3], double out[3][3])
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

void ff_matrix_mul_3x3(double dst[3][3],
               const double src1[3][3], const double src2[3][3])
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
void ff_fill_rgb2xyz_table(const AVPrimaryCoefficients *coeffs,
                           const AVWhitepointCoefficients *wp,
                           double rgb2xyz[3][3])
{
    double i[3][3], sr, sg, sb, zw;
    double xr = av_q2d(coeffs->r.x), yr = av_q2d(coeffs->r.y);
    double xg = av_q2d(coeffs->g.x), yg = av_q2d(coeffs->g.y);
    double xb = av_q2d(coeffs->b.x), yb = av_q2d(coeffs->b.y);
    double xw = av_q2d(wp->x), yw = av_q2d(wp->y);

    rgb2xyz[0][0] = xr / yr;
    rgb2xyz[0][1] = xg / yg;
    rgb2xyz[0][2] = xb / yb;
    rgb2xyz[1][0] = rgb2xyz[1][1] = rgb2xyz[1][2] = 1.0;
    rgb2xyz[2][0] = (1.0 - xr - yr) / yr;
    rgb2xyz[2][1] = (1.0 - xg - yg) / yg;
    rgb2xyz[2][2] = (1.0 - xb - yb) / yb;
    ff_matrix_invert_3x3(rgb2xyz, i);
    zw = 1.0 - xw - yw;
    sr = i[0][0] * xw + i[0][1] * yw + i[0][2] * zw;
    sg = i[1][0] * xw + i[1][1] * yw + i[1][2] * zw;
    sb = i[2][0] * xw + i[2][1] * yw + i[2][2] * zw;
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
static const double ycgco_matrix[3][3] =
{
    {  0.25, 0.5,  0.25 },
    { -0.25, 0.5, -0.25 },
    {  0.5,  0,   -0.5  },
};

static const double gbr_matrix[3][3] =
{
    { 0,    1,   0   },
    { 0,   -0.5, 0.5 },
    { 0.5, -0.5, 0   },
};

void ff_fill_rgb2yuv_table(const AVLumaCoefficients *coeffs,
                           double rgb2yuv[3][3])
{
    double bscale, rscale;
    double cr = av_q2d(coeffs->cr), cg = av_q2d(coeffs->cg), cb = av_q2d(coeffs->cb);

    // special ycgco matrix
    if (cr == 0.25 && cg == 0.5 && cb == 0.25) {
        memcpy(rgb2yuv, ycgco_matrix, sizeof(double) * 9);
        return;
    } else if (cr == 1 && cg == 1 && cb == 1) {
        memcpy(rgb2yuv, gbr_matrix, sizeof(double) * 9);
        return;
    }

    rgb2yuv[0][0] = cr;
    rgb2yuv[0][1] = cg;
    rgb2yuv[0][2] = cb;
    bscale = 0.5 / (cb - 1.0);
    rscale = 0.5 / (cr - 1.0);
    rgb2yuv[1][0] = bscale * cr;
    rgb2yuv[1][1] = bscale * cg;
    rgb2yuv[1][2] = 0.5;
    rgb2yuv[2][0] = 0.5;
    rgb2yuv[2][1] = rscale * cg;
    rgb2yuv[2][2] = rscale * cb;
}

double ff_determine_signal_peak(AVFrame *in)
{
    AVFrameSideData *sd = av_frame_get_side_data(in, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
    double peak = 0;

    if (sd) {
        AVContentLightMetadata *clm = (AVContentLightMetadata *)sd->data;
        peak = clm->MaxCLL / REFERENCE_WHITE;
    }

    sd = av_frame_get_side_data(in, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
    if (!peak && sd) {
        AVMasteringDisplayMetadata *metadata = (AVMasteringDisplayMetadata *)sd->data;
        if (metadata->has_luminance)
            peak = av_q2d(metadata->max_luminance) / REFERENCE_WHITE;
    }

    // For untagged source, use peak of 10000 if SMPTE ST.2084
    // otherwise assume HLG with reference display peak 1000.
    if (!peak)
        peak = in->color_trc == AVCOL_TRC_SMPTE2084 ? 100.0f : 10.0f;

    return peak;
}

void ff_update_hdr_metadata(AVFrame *in, double peak)
{
    AVFrameSideData *sd = av_frame_get_side_data(in, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);

    if (sd) {
        AVContentLightMetadata *clm = (AVContentLightMetadata *)sd->data;
        clm->MaxCLL = (unsigned)(peak * REFERENCE_WHITE);
    }

    sd = av_frame_get_side_data(in, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
    if (sd) {
        AVMasteringDisplayMetadata *metadata = (AVMasteringDisplayMetadata *)sd->data;
        if (metadata->has_luminance)
            metadata->max_luminance = av_d2q(peak * REFERENCE_WHITE, 10000);
    }
}
