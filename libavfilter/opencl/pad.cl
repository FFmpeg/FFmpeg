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

__kernel void pad (
    __read_only  image2d_t src,
    __write_only image2d_t dst,
    float4 color,
    int2 xy)
{
    int2 size_src = get_image_dim(src);
    int2 loc = (int2)(get_global_id(0), get_global_id(1));
    int2 src_pos = (int2)(get_global_id(0) - xy.x, get_global_id(1) - xy.y);
    float4 pixel = loc.x >= size_src.x + xy.x ||
                   loc.y >= size_src.y + xy.y ||
                   loc.x < xy.x ||
                   loc.y < xy.y ? color : read_imagef(src, sampler, src_pos);
    write_imagef(dst, loc, pixel);
}
