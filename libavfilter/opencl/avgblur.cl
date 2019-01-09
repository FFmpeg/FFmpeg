/*
 * Copyright (c) 2018 Dylan Fernando
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


__kernel void avgblur_horiz(__write_only image2d_t dst,
                            __read_only  image2d_t src,
                            int rad)
{
    const sampler_t sampler = (CLK_NORMALIZED_COORDS_FALSE |
                               CLK_FILTER_NEAREST);
    int2 loc = (int2)(get_global_id(0), get_global_id(1));
    int2 size = (int2)(get_global_size(0), get_global_size(1));

    int count = 0;
    float4 acc = (float4)(0,0,0,0);

    for (int xx = max(0, loc.x - rad); xx < min(loc.x + rad + 1, size.x); xx++) {
        count++;
        acc += read_imagef(src, sampler, (int2)(xx, loc.y));
    }

    write_imagef(dst, loc, acc / count);
}

__kernel void avgblur_vert(__write_only image2d_t dst,
                           __read_only  image2d_t src,
                           int radv)
{
    const sampler_t sampler = (CLK_NORMALIZED_COORDS_FALSE |
                               CLK_FILTER_NEAREST);
    int2 loc = (int2)(get_global_id(0), get_global_id(1));
    int2 size = (int2)(get_global_size(0), get_global_size(1));

    int count = 0;
    float4 acc = (float4)(0,0,0,0);

    for (int yy = max(0, loc.y - radv); yy < min(loc.y + radv + 1, size.y); yy++) {
        count++;
        acc += read_imagef(src, sampler, (int2)(loc.x, yy));
    }

    write_imagef(dst, loc, acc / count);
}
