/*
 * Copyright (C) 2013 Wei Gao <weigao@multicorewareinc.com>
 * Copyright (C) 2013 Lenny Wang
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
inline unsigned char pixel(global const unsigned char *src, int x, int y,
                           int w, int h,int stride, unsigned char def)
{
    return (x < 0 || y < 0 || x >= w || y >= h) ? def : src[x + y * stride];
}

unsigned char interpolate_nearest(float x, float y, global const unsigned char *src,
                                  int width, int height, int stride, unsigned char def)
{
    return pixel(src, (int)(x + 0.5f), (int)(y + 0.5f), width, height, stride, def);
}

unsigned char interpolate_bilinear(float x, float y, global const unsigned char *src,
                                   int width, int height, int stride, unsigned char def)
{
    int x_c, x_f, y_c, y_f;
    int v1, v2, v3, v4;
    x_f = (int)x;
    y_f = (int)y;
    x_c = x_f + 1;
    y_c = y_f + 1;

    if (x_f < -1 || x_f > width || y_f < -1 || y_f > height) {
        return def;
    } else {
        v4 = pixel(src, x_f, y_f, width, height, stride, def);
        v2 = pixel(src, x_c, y_f, width, height, stride, def);
        v3 = pixel(src, x_f, y_c, width, height, stride, def);
        v1 = pixel(src, x_c, y_c, width, height, stride, def);
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
    x_f = (int)x;
    y_f = (int)y;
    x_c = x_f + 1;
    y_c = y_f + 1;

    if (x_f < - 1 || x_f > width || y_f < -1 || y_f > height)
        return def;
    else {
        v4 = pixel(src, x_f, y_f, width, height, stride, def);
        v2 = pixel(src, x_c, y_f, width, height, stride, def);
        v3 = pixel(src, x_f, y_c, width, height, stride, def);
        v1 = pixel(src, x_c, y_c, width, height, stride, def);

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

kernel void avfilter_transform_luma(global unsigned char *src,
                                    global unsigned char *dst,
                                    float4 matrix,
                                    int interpolate,
                                    int fill,
                                    int src_stride_lu,
                                    int dst_stride_lu,
                                    int height,
                                    int width)
{
    int x = get_global_id(0);
    int y = get_global_id(1);
    int idx_dst = y * dst_stride_lu + x;
    unsigned char def = 0;
    float x_s = x * matrix.x + y * matrix.y + matrix.z;
    float y_s = x * (-matrix.y) + y * matrix.x + matrix.w;

    if (x < width && y < height) {
        switch (fill) {
            case 0: //FILL_BLANK
                def = 0;
                break;
            case 1: //FILL_ORIGINAL
                def = src[y*src_stride_lu + x];
                break;
            case 2: //FILL_CLAMP
                y_s = clipf(y_s, 0, height - 1);
                x_s = clipf(x_s, 0, width - 1);
                def = src[(int)y_s * src_stride_lu + (int)x_s];
                break;
            case 3: //FILL_MIRROR
                y_s = mirror(y_s, height - 1);
                x_s = mirror(x_s, width - 1);
                def = src[(int)y_s * src_stride_lu + (int)x_s];
                break;
        }
        switch (interpolate) {
            case 0: //INTERPOLATE_NEAREST
                dst[idx_dst] = interpolate_nearest(x_s, y_s, src, width, height, src_stride_lu, def);
                break;
            case 1: //INTERPOLATE_BILINEAR
                dst[idx_dst] = interpolate_bilinear(x_s, y_s, src, width, height, src_stride_lu, def);
                break;
            case 2: //INTERPOLATE_BIQUADRATIC
                dst[idx_dst] = interpolate_biquadratic(x_s, y_s, src, width, height, src_stride_lu, def);
                break;
            default:
                return;
        }
    }
}

kernel void avfilter_transform_chroma(global unsigned char *src,
                                      global unsigned char *dst,
                                      float4 matrix,
                                      int interpolate,
                                      int fill,
                                      int src_stride_lu,
                                      int dst_stride_lu,
                                      int src_stride_ch,
                                      int dst_stride_ch,
                                      int height,
                                      int width,
                                      int ch,
                                      int cw)
{

    int x = get_global_id(0);
    int y = get_global_id(1);
    int pad_ch = get_global_size(1)>>1;
    global unsigned char *dst_u = dst + height * dst_stride_lu;
    global unsigned char *src_u = src + height * src_stride_lu;
    global unsigned char *dst_v = dst_u + ch * dst_stride_ch;
    global unsigned char *src_v = src_u + ch * src_stride_ch;
    src = y < pad_ch ? src_u : src_v;
    dst = y < pad_ch ? dst_u : dst_v;
    y = select(y - pad_ch, y, y < pad_ch);
    float x_s = x * matrix.x + y * matrix.y + matrix.z;
    float y_s = x * (-matrix.y) + y * matrix.x + matrix.w;
    int idx_dst = y * dst_stride_ch + x;
    unsigned char def;

    if (x < cw && y < ch) {
        switch (fill) {
            case 0: //FILL_BLANK
                def = 0;
                break;
            case 1: //FILL_ORIGINAL
                def = src[y*src_stride_ch + x];
                break;
            case 2: //FILL_CLAMP
                y_s = clipf(y_s, 0, ch - 1);
                x_s = clipf(x_s, 0, cw - 1);
                def = src[(int)y_s * src_stride_ch + (int)x_s];
                break;
            case 3: //FILL_MIRROR
                y_s = mirror(y_s, ch - 1);
                x_s = mirror(x_s, cw - 1);
                def = src[(int)y_s * src_stride_ch + (int)x_s];
                break;
        }
        switch (interpolate) {
            case 0: //INTERPOLATE_NEAREST
                dst[idx_dst] = interpolate_nearest(x_s, y_s, src, cw, ch, src_stride_ch, def);
                break;
            case 1: //INTERPOLATE_BILINEAR
                dst[idx_dst] = interpolate_bilinear(x_s, y_s, src, cw, ch, src_stride_ch, def);
                break;
            case 2: //INTERPOLATE_BIQUADRATIC
                dst[idx_dst] = interpolate_biquadratic(x_s, y_s, src, cw, ch, src_stride_ch, def);
                break;
            default:
                return;
        }
    }
}
);

#endif /* AVFILTER_DESHAKE_OPENCL_KERNEL_H */
