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
kernel void transpose(__write_only image2d_t dst,
                      __read_only image2d_t src,
                      int dir) {
    const sampler_t sampler = (CLK_NORMALIZED_COORDS_FALSE |
                               CLK_ADDRESS_CLAMP_TO_EDGE   |
                               CLK_FILTER_NEAREST);

    int2 size = get_image_dim(dst);
    int x = get_global_id(0);
    int y = get_global_id(1);

    int xin = (dir & 2) ? (size.y - 1 - y) : y;
    int yin = (dir & 1) ? (size.x - 1 - x) : x;
    float4 data = read_imagef(src, sampler, (int2)(xin, yin));

    if (x < size.x && y < size.y)
        write_imagef(dst, (int2)(x, y), data);
}
