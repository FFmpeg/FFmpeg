/*
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

const sampler_t sampler = (CLK_NORMALIZED_COORDS_FALSE |
                           CLK_FILTER_NEAREST);

__kernel void fade(__write_only image2d_t dst,
                   __read_only  image2d_t src1,
                   __read_only  image2d_t src2,
                   float progress)
{
    int2  p = (int2)(get_global_id(0), get_global_id(1));

    float4 val1 = read_imagef(src1, sampler, p);
    float4 val2 = read_imagef(src2, sampler, p);

    write_imagef(dst, p, mix(val2, val1, progress));
}

__kernel void wipeleft(__write_only image2d_t dst,
                       __read_only  image2d_t src1,
                       __read_only  image2d_t src2,
                       float progress)
{
    int   s = (int)(get_image_dim(src1).x * progress);
    int2  p = (int2)(get_global_id(0), get_global_id(1));

    float4 val1 = read_imagef(src1, sampler, p);
    float4 val2 = read_imagef(src2, sampler, p);

    write_imagef(dst, p, p.x > s ? val2 : val1);
}

__kernel void wiperight(__write_only image2d_t dst,
                        __read_only  image2d_t src1,
                        __read_only  image2d_t src2,
                        float progress)
{
    int   s = (int)(get_image_dim(src1).x * (1.f - progress));
    int2  p = (int2)(get_global_id(0), get_global_id(1));

    float4 val1 = read_imagef(src1, sampler, p);
    float4 val2 = read_imagef(src2, sampler, p);

    write_imagef(dst, p, p.x > s ? val1 : val2);
}

__kernel void wipeup(__write_only image2d_t dst,
                     __read_only  image2d_t src1,
                     __read_only  image2d_t src2,
                     float progress)
{
    int   s = (int)(get_image_dim(src1).y * progress);
    int2  p = (int2)(get_global_id(0), get_global_id(1));

    float4 val1 = read_imagef(src1, sampler, p);
    float4 val2 = read_imagef(src2, sampler, p);

    write_imagef(dst, p, p.y > s ? val2 : val1);
}

__kernel void wipedown(__write_only image2d_t dst,
                       __read_only  image2d_t src1,
                       __read_only  image2d_t src2,
                       float progress)
{
    int   s = (int)(get_image_dim(src1).y * (1.f - progress));
    int2  p = (int2)(get_global_id(0), get_global_id(1));

    float4 val1 = read_imagef(src1, sampler, p);
    float4 val2 = read_imagef(src2, sampler, p);

    write_imagef(dst, p, p.y > s ? val1 : val2);
}

void slide(__write_only image2d_t dst,
           __read_only  image2d_t src1,
           __read_only  image2d_t src2,
           float progress,
           int2 direction)
{
    int   w = get_image_dim(src1).x;
    int   h = get_image_dim(src1).y;
    int2 wh = (int2)(w, h);
    int2 uv = (int2)(get_global_id(0), get_global_id(1));
    int2 pi = (int2)(progress * w, progress * h);
    int2 p = uv + pi * direction;
    int2 f = p % wh;

    f = f + (int2)(w, h) * (int2)(f.x < 0, f.y < 0);
    float4 val1 = read_imagef(src1, sampler, f);
    float4 val2 = read_imagef(src2, sampler, f);
    write_imagef(dst, uv, mix(val1, val2, (p.y >= 0) * (h > p.y) * (p.x >= 0) * (w > p.x)));
}

__kernel void slidedown(__write_only image2d_t dst,
                        __read_only  image2d_t src1,
                        __read_only  image2d_t src2,
                        float progress)
{
    int2 direction = (int2)(0, 1);
    slide(dst, src1, src2, progress, direction);
}

__kernel void slideup(__write_only image2d_t dst,
                      __read_only  image2d_t src1,
                      __read_only  image2d_t src2,
                      float progress)
{
    int2 direction = (int2)(0, -1);
    slide(dst, src1, src2, progress, direction);
}

__kernel void slideleft(__write_only image2d_t dst,
                        __read_only  image2d_t src1,
                        __read_only  image2d_t src2,
                        float progress)
{
    int2 direction = (int2)(-1, 0);
    slide(dst, src1, src2, progress, direction);
}

__kernel void slideright(__write_only image2d_t dst,
                         __read_only  image2d_t src1,
                         __read_only  image2d_t src2,
                         float progress)
{
    int2 direction = (int2)(1, 0);
    slide(dst, src1, src2, progress, direction);
}
