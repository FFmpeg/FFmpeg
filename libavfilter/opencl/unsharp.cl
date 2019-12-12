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

__kernel void unsharp_global(__write_only image2d_t dst,
                             __read_only  image2d_t src,
                             int size_x,
                             int size_y,
                             float amount,
                             __constant float *coef_matrix)
{
    const sampler_t sampler = (CLK_NORMALIZED_COORDS_FALSE |
                               CLK_FILTER_NEAREST);
    int2 loc    = (int2)(get_global_id(0), get_global_id(1));
    int2 centre = (int2)(size_x / 2, size_y / 2);

    float4 val = read_imagef(src, sampler, loc);
    float4 sum = 0.0f;
    int x, y;

    for (y = 0; y < size_y; y++) {
        for (x = 0; x < size_x; x++) {
            int2 pos = loc + (int2)(x, y) - centre;
            sum += coef_matrix[y * size_x + x] *
                read_imagef(src, sampler, pos);
        }
    }

    write_imagef(dst, loc, val + (val - sum) * amount);
}

__kernel void unsharp_local(__write_only image2d_t dst,
                            __read_only  image2d_t src,
                            int size_x,
                            int size_y,
                            float amount,
                            __constant float *coef_x,
                            __constant float *coef_y)
{
    const sampler_t sampler = (CLK_NORMALIZED_COORDS_FALSE |
                               CLK_ADDRESS_CLAMP_TO_EDGE |
                               CLK_FILTER_NEAREST);
    int2 block = (int2)(get_group_id(0), get_group_id(1)) * 16;
    int2 pos   = (int2)(get_local_id(0), get_local_id(1));

    __local float4 tmp[32][32];

    int rad_x = size_x / 2;
    int rad_y = size_y / 2;
    int x, y;

    for (y = 0; y <= 1; y++) {
        for (x = 0; x <= 1; x++) {
            tmp[pos.y + 16 * y][pos.x + 16 * x] =
                read_imagef(src, sampler, block + pos + (int2)(16 * x - 8, 16 * y - 8));
        }
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    float4 val = tmp[pos.y + 8][pos.x + 8];

    float4 horiz[2];
    for (y = 0; y <= 1; y++) {
        horiz[y] = 0.0f;
        for (x = 0; x < size_x; x++)
            horiz[y] += coef_x[x] * tmp[pos.y + y * 16][pos.x + 8 + x - rad_x];
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    for (y = 0; y <= 1; y++) {
        tmp[pos.y + y * 16][pos.x + 8] = horiz[y];
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    float4 sum = 0.0f;
    for (y = 0; y < size_y; y++)
        sum += coef_y[y] * tmp[pos.y + 8 + y - rad_y][pos.x + 8];

    if (block.x + pos.x < get_image_width(dst) &&
        block.y + pos.y < get_image_height(dst))
        write_imagef(dst, block + pos, val + (val - sum) * amount);
}
