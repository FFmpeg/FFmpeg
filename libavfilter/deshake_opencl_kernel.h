/*
 * Copyright (C) 2013 Wei Gao <weigao@multicorewareinc.com>
 *
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

#ifndef AVFILTER_DESHAKE_OPENCL_KERNEL_H
#define AVFILTER_DESHAKE_OPENCL_KERNEL_H

#include "libavutil/opencl.h"

const char *ff_kernel_deshake_opencl = AV_OPENCL_KERNEL(

inline unsigned char pixel(global const unsigned char *src, float x, float y,
                           int w, int h,int stride, unsigned char def)
{
    return (x < 0 || y < 0 || x >= w || y >= h) ? def : src[(int)x + (int)y * stride];
}
unsigned char interpolate_nearest(float x, float y, global const unsigned char *src,
                                  int width, int height, int stride, unsigned char def)
{
    return pixel(src, (int)(x + 0.5), (int)(y + 0.5), width, height, stride, def);
}

unsigned char interpolate_bilinear(float x, float y, global const unsigned char *src,
                                   int width, int height, int stride, unsigned char def)
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

        v1 = pixel(src, x_c, y_c, width, height, stride, def);
        v2 = pixel(src, x_c, y_f, width, height, stride, def);
        v3 = pixel(src, x_f, y_c, width, height, stride, def);
        v4 = pixel(src, x_f, y_f, width, height, stride, def);

        return (v1*(x - x_f)*(y - y_f) + v2*((x - x_f)*(y_c - y)) +
                v3*(x_c - x)*(y - y_f) + v4*((x_c - x)*(y_c - y)));
    }
}

unsigned char interpolate_biquadratic(float x, float y, global const unsigned char *src,
                                      int width, int height, int stride, unsigned char def)
{
    int     x_c, x_f, y_c, y_f;
    unsigned char v1,  v2,  v3,  v4;
    float   f1,  f2,  f3,  f4;

    if (x < - 1 || x > width || y < -1 || y > height)
        return def;
    else {
        x_f = (int)x;
        x_c = x_f + 1;
        y_f = (int)y;
        y_c = y_f + 1;

        v1 = pixel(src, x_c, y_c, width, height, stride, def);
        v2 = pixel(src, x_c, y_f, width, height, stride, def);
        v3 = pixel(src, x_f, y_c, width, height, stride, def);
        v4 = pixel(src, x_f, y_f, width, height, stride, def);

        f1 = 1 - sqrt((x_c - x) * (y_c - y));
        f2 = 1 - sqrt((x_c - x) * (y - y_f));
        f3 = 1 - sqrt((x - x_f) * (y_c - y));
        f4 = 1 - sqrt((x - x_f) * (y - y_f));
        return (v1 * f1 + v2 * f2 + v3 * f3 + v4 * f4) / (f1 + f2 + f3 + f4);
    }
}

inline const float clipf(float a, float amin, float amax)
{
    if      (a < amin) return amin;
    else if (a > amax) return amax;
    else               return a;
}

inline int mirror(int v, int m)
{
    while ((unsigned)v > (unsigned)m) {
        v = -v;
        if (v < 0)
            v += 2 * m;
    }
    return v;
}

kernel void avfilter_transform(global  unsigned char *src,
                               global  unsigned char *dst,
                               global          float *matrix,
                               global          float *matrix2,
                                                 int interpolate,
                                                 int fillmethod,
                                                 int src_stride_lu,
                                                 int dst_stride_lu,
                                                 int src_stride_ch,
                                                 int dst_stride_ch,
                                                 int height,
                                                 int width,
                                                 int ch,
                                                 int cw)
{
     int global_id = get_global_id(0);

     global unsigned char *dst_y = dst;
     global unsigned char *dst_u = dst_y + height * dst_stride_lu;
     global unsigned char *dst_v = dst_u + ch * dst_stride_ch;

     global unsigned char *src_y = src;
     global unsigned char *src_u = src_y + height * src_stride_lu;
     global unsigned char *src_v = src_u + ch * src_stride_ch;

     global unsigned char *tempdst;
     global unsigned char *tempsrc;

     int x;
     int y;
     float x_s;
     float y_s;
     int tempsrc_stride;
     int tempdst_stride;
     int temp_height;
     int temp_width;
     int curpos;
     unsigned char def = 0;
     if (global_id < width*height) {
        y = global_id/width;
        x = global_id%width;
        x_s = x * matrix[0] + y * matrix[1] + matrix[2];
        y_s = x * matrix[3] + y * matrix[4] + matrix[5];
        tempdst = dst_y;
        tempsrc = src_y;
        tempsrc_stride = src_stride_lu;
        tempdst_stride = dst_stride_lu;
        temp_height = height;
        temp_width = width;
     } else if ((global_id >= width*height)&&(global_id < width*height + ch*cw)) {
        y = (global_id - width*height)/cw;
        x = (global_id - width*height)%cw;
        x_s = x * matrix2[0] + y * matrix2[1] + matrix2[2];
        y_s = x * matrix2[3] + y * matrix2[4] + matrix2[5];
        tempdst = dst_u;
        tempsrc = src_u;
        tempsrc_stride = src_stride_ch;
        tempdst_stride = dst_stride_ch;
        temp_height = ch;
        temp_width = cw;
     } else {
        y = (global_id - width*height - ch*cw)/cw;
        x = (global_id - width*height - ch*cw)%cw;
        x_s = x * matrix2[0] + y * matrix2[1] + matrix2[2];
        y_s = x * matrix2[3] + y * matrix2[4] + matrix2[5];
        tempdst = dst_v;
        tempsrc = src_v;
        tempsrc_stride = src_stride_ch;
        tempdst_stride = dst_stride_ch;
        temp_height = ch;
        temp_width = cw;
     }
     curpos = y * tempdst_stride + x;
     switch (fillmethod) {
        case 0: //FILL_BLANK
            def = 0;
            break;
        case 1: //FILL_ORIGINAL
            def = tempsrc[y*tempsrc_stride+x];
            break;
        case 2: //FILL_CLAMP
            y_s = clipf(y_s, 0, temp_height - 1);
            x_s = clipf(x_s, 0, temp_width - 1);
            def = tempsrc[(int)y_s * tempsrc_stride + (int)x_s];
            break;
        case 3: //FILL_MIRROR
            y_s = mirror(y_s,temp_height - 1);
            x_s = mirror(x_s,temp_width - 1);
            def = tempsrc[(int)y_s * tempsrc_stride + (int)x_s];
            break;
    }
    switch (interpolate) {
        case 0: //INTERPOLATE_NEAREST
            tempdst[curpos] = interpolate_nearest(x_s, y_s, tempsrc, temp_width, temp_height, tempsrc_stride, def);
            break;
        case 1: //INTERPOLATE_BILINEAR
            tempdst[curpos] = interpolate_bilinear(x_s, y_s, tempsrc, temp_width, temp_height, tempsrc_stride, def);
            break;
        case 2: //INTERPOLATE_BIQUADRATIC
            tempdst[curpos] = interpolate_biquadratic(x_s, y_s, tempsrc, temp_width, temp_height, tempsrc_stride, def);
            break;
        default:
            return;
    }
}
);

#endif /* AVFILTER_DESHAKE_OPENCL_KERNEL_H */
