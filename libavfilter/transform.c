/*
 * Copyright (C) 2010 Georg Martius <georg.martius@web.de>
 * Copyright (C) 2010 Daniel G. Taylor <dan@programmer-art.org>
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

/**
 * @file
 * transform input video
 */

#include "libavutil/common.h"
#include "libavutil/avassert.h"

#include "transform.h"

#define INTERPOLATE_METHOD(name) \
    static uint8_t name(float x, float y, const uint8_t *src, \
                        int width, int height, int stride, uint8_t def)

#define PIXEL(img, x, y, w, h, stride, def) \
    ((x) < 0 || (y) < 0) ? (def) : \
    (((x) >= (w) || (y) >= (h)) ? (def) : \
    img[(x) + (y) * (stride)])

/**
 * Nearest neighbor interpolation
 */
INTERPOLATE_METHOD(interpolate_nearest)
{
    return PIXEL(src, (int)(x + 0.5), (int)(y + 0.5), width, height, stride, def);
}

/**
 * Bilinear interpolation
 */
INTERPOLATE_METHOD(interpolate_bilinear)
{
    int x_c, x_f, y_c, y_f;
    int v1, v2, v3, v4;

    if (x < -1 || x > width || y < -1 || y > height) {
        return def;
    } else {
        x_f = (int)x;
        x_c = x_f + 1;

        y_f = (int)y;
        y_c = y_f + 1;

        v1 = PIXEL(src, x_c, y_c, width, height, stride, def);
        v2 = PIXEL(src, x_c, y_f, width, height, stride, def);
        v3 = PIXEL(src, x_f, y_c, width, height, stride, def);
        v4 = PIXEL(src, x_f, y_f, width, height, stride, def);

        return (v1*(x - x_f)*(y - y_f) + v2*((x - x_f)*(y_c - y)) +
                v3*(x_c - x)*(y - y_f) + v4*((x_c - x)*(y_c - y)));
    }
}

/**
 * Biquadratic interpolation
 */
INTERPOLATE_METHOD(interpolate_biquadratic)
{
    int     x_c, x_f, y_c, y_f;
    uint8_t v1,  v2,  v3,  v4;
    float   f1,  f2,  f3,  f4;

    if (x < - 1 || x > width || y < -1 || y > height)
        return def;
    else {
        x_f = (int)x;
        x_c = x_f + 1;
        y_f = (int)y;
        y_c = y_f + 1;

        v1 = PIXEL(src, x_c, y_c, width, height, stride, def);
        v2 = PIXEL(src, x_c, y_f, width, height, stride, def);
        v3 = PIXEL(src, x_f, y_c, width, height, stride, def);
        v4 = PIXEL(src, x_f, y_f, width, height, stride, def);

        f1 = 1 - sqrt((x_c - x) * (y_c - y));
        f2 = 1 - sqrt((x_c - x) * (y - y_f));
        f3 = 1 - sqrt((x - x_f) * (y_c - y));
        f4 = 1 - sqrt((x - x_f) * (y - y_f));
        return (v1 * f1 + v2 * f2 + v3 * f3 + v4 * f4) / (f1 + f2 + f3 + f4);
    }
}

void ff_get_matrix(
    float x_shift,
    float y_shift,
    float angle,
    float scale_x,
    float scale_y,
    float *matrix
) {
    matrix[0] = scale_x * cos(angle);
    matrix[1] = -sin(angle);
    matrix[2] = x_shift;
    matrix[3] = -matrix[1];
    matrix[4] = scale_y * cos(angle);
    matrix[5] = y_shift;
    matrix[6] = 0;
    matrix[7] = 0;
    matrix[8] = 1;
}

void avfilter_add_matrix(const float *m1, const float *m2, float *result)
{
    int i;
    for (i = 0; i < 9; i++)
        result[i] = m1[i] + m2[i];
}

void avfilter_sub_matrix(const float *m1, const float *m2, float *result)
{
    int i;
    for (i = 0; i < 9; i++)
        result[i] = m1[i] - m2[i];
}

void avfilter_mul_matrix(const float *m1, float scalar, float *result)
{
    int i;
    for (i = 0; i < 9; i++)
        result[i] = m1[i] * scalar;
}

int avfilter_transform(const uint8_t *src, uint8_t *dst,
                        int src_stride, int dst_stride,
                        int width, int height, const float *matrix,
                        enum InterpolateMethod interpolate,
                        enum FillMethod fill)
{
    int x, y;
    float x_s, y_s;
    uint8_t def = 0;
    uint8_t (*func)(float, float, const uint8_t *, int, int, int, uint8_t) = NULL;

    switch(interpolate) {
        case INTERPOLATE_NEAREST:
            func = interpolate_nearest;
            break;
        case INTERPOLATE_BILINEAR:
            func = interpolate_bilinear;
            break;
        case INTERPOLATE_BIQUADRATIC:
            func = interpolate_biquadratic;
            break;
        default:
            return AVERROR(EINVAL);
    }

    for (y = 0; y < height; y++) {
        for(x = 0; x < width; x++) {
            x_s = x * matrix[0] + y * matrix[1] + matrix[2];
            y_s = x * matrix[3] + y * matrix[4] + matrix[5];

            switch(fill) {
                case FILL_ORIGINAL:
                    def = src[y * src_stride + x];
                    break;
                case FILL_CLAMP:
                    y_s = av_clipf(y_s, 0, height - 1);
                    x_s = av_clipf(x_s, 0, width - 1);
                    def = src[(int)y_s * src_stride + (int)x_s];
                    break;
                case FILL_MIRROR:
                    x_s = avpriv_mirror(x_s,  width-1);
                    y_s = avpriv_mirror(y_s, height-1);

                    av_assert2(x_s >= 0 && y_s >= 0);
                    av_assert2(x_s < width && y_s < height);
                    def = src[(int)y_s * src_stride + (int)x_s];
            }

            dst[y * dst_stride + x] = func(x_s, y_s, src, width, height, src_stride, def);
        }
    }
    return 0;
}
