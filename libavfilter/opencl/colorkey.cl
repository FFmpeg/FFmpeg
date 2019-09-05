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

const sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE |
                          CLK_FILTER_NEAREST;

__kernel void colorkey_blend(
    __read_only  image2d_t src,
    __write_only image2d_t dst,
    float4 colorkey_rgba,
    float similarity,
    float blend
) {
    int2 loc = (int2)(get_global_id(0), get_global_id(1));
    float4 pixel = read_imagef(src, sampler, loc);
    float diff = distance(pixel.xyz, colorkey_rgba.xyz);

    pixel.s3 = clamp((diff - similarity) / blend, 0.0f, 1.0f);
    write_imagef(dst, loc, pixel);
}

__kernel void colorkey(
    __read_only  image2d_t src,
    __write_only image2d_t dst,
    float4 colorkey_rgba,
    float similarity
) {
    int2 loc = (int2)(get_global_id(0), get_global_id(1));
    float4 pixel = read_imagef(src, sampler, loc);
    float diff = distance(pixel.xyz, colorkey_rgba.xyz);

    pixel.s3 = (diff > similarity) ? 1.0f : 0.0f;
    write_imagef(dst, loc, pixel);
}
