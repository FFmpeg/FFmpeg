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

const sampler_t linear_sampler = (CLK_NORMALIZED_COORDS_FALSE |
                                  CLK_FILTER_LINEAR);

const sampler_t nearest_sampler = (CLK_NORMALIZED_COORDS_FALSE |
                                   CLK_FILTER_NEAREST);

__kernel void remap_near(__write_only image2d_t dst,
                         __read_only  image2d_t src,
                         __read_only  image2d_t xmapi,
                         __read_only  image2d_t ymapi,
                         float4 fill_color)
{
    int2 p = (int2)(get_global_id(0), get_global_id(1));
    int2 dimi = get_image_dim(src);
    float2 dimf = (float2)(dimi.x, dimi.y);
    float4 val;
    int2 mi;
    float m;

    float4 xmap = read_imagef(xmapi, nearest_sampler, p);
    float4 ymap = read_imagef(ymapi, nearest_sampler, p);
    float2 pos  = (float2)(xmap.x, ymap.x);
    pos.xy = pos.xy * 65535.f;

    mi = ((pos >= (float2)(0.f, 0.f)) * (pos < dimf) * (p <= dimi));
    m = mi.x && mi.y;
    val = mix(fill_color, read_imagef(src, nearest_sampler, pos), m);

    write_imagef(dst, p, val);
}

__kernel void remap_linear(__write_only image2d_t dst,
                           __read_only  image2d_t src,
                           __read_only  image2d_t xmapi,
                           __read_only  image2d_t ymapi,
                           float4 fill_color)
{
    int2 p = (int2)(get_global_id(0), get_global_id(1));
    int2 dimi = get_image_dim(src);
    float2 dimf = (float2)(dimi.x, dimi.y);
    float4 val;
    int2 mi;
    float m;

    float4 xmap = read_imagef(xmapi, nearest_sampler, p);
    float4 ymap = read_imagef(ymapi, nearest_sampler, p);
    float2 pos  = (float2)(xmap.x, ymap.x);
    pos.xy = pos.xy * 65535.f;

    mi = ((pos >= (float2)(0.f, 0.f)) * (pos < dimf) * (p <= dimi));
    m = mi.x && mi.y;
    val = mix(fill_color, read_imagef(src, linear_sampler, pos), m);

    write_imagef(dst, p, val);
}
