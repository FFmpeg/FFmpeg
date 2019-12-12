/*
 * Copyright (c) 2018 Danil Iashchenko
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


__kernel void erosion_global(__write_only image2d_t dst,
                             __read_only  image2d_t src,
                             float threshold,
                             __constant int *coord)
{
    const sampler_t sampler = (CLK_NORMALIZED_COORDS_FALSE |
                               CLK_ADDRESS_CLAMP_TO_EDGE   |
                               CLK_FILTER_NEAREST);

    int2 loc = (int2)(get_global_id(0), get_global_id(1));

    float4 px = read_imagef(src, sampler, loc);
    float limit = px.x - threshold;
    if (limit < 0) {
        limit = 0;
    }

    for (int i = -1; i <= 1; i++) {
        for (int j = -1; j <= 1; j++) {
            if (coord[(j + 1) * 3 + (i + 1)] == 1) {
                float4 cur = read_imagef(src, sampler, loc + (int2)(i, j));
                if (cur.x < px.x) {
                    px = cur;
                }
            }
        }
    }
    if (limit > px.x) {
        px = (float4)(limit);
    }
    write_imagef(dst, loc, px);
}


__kernel void dilation_global(__write_only image2d_t dst,
                              __read_only  image2d_t src,
                              float threshold,
                              __constant int *coord)
{
    const sampler_t sampler = (CLK_NORMALIZED_COORDS_FALSE |
                               CLK_ADDRESS_CLAMP_TO_EDGE   |
                               CLK_FILTER_NEAREST);

    int2 loc = (int2)(get_global_id(0), get_global_id(1));

    float4 px = read_imagef(src, sampler, loc);
    float limit = px.x + threshold;
    if (limit > 1) {
        limit = 1;
    }

    for (int i = -1; i <= 1; i++) {
        for (int j = -1; j <= 1; j++) {
            if (coord[(j + 1) * 3 + (i + 1)] == 1) {
                float4 cur = read_imagef(src, sampler, loc + (int2)(i, j));
                if (cur.x > px.x) {
                    px = cur;
                }
            }
        }
    }
    if (limit < px.x) {
        px = (float4)(limit);
    }
    write_imagef(dst, loc, px);
}
