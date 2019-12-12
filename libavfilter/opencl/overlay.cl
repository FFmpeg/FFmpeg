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

__kernel void overlay_no_alpha(__write_only image2d_t dst,
                               __read_only  image2d_t main,
                               __read_only  image2d_t overlay,
                               int x_position,
                               int y_position)
{
    const sampler_t sampler = (CLK_NORMALIZED_COORDS_FALSE |
                               CLK_FILTER_NEAREST);

    int2 overlay_size = get_image_dim(overlay);
    int2 loc = (int2)(get_global_id(0), get_global_id(1));

    if (loc.x <  x_position ||
        loc.y <  y_position ||
        loc.x >= overlay_size.x + x_position ||
        loc.y >= overlay_size.y + y_position) {
        float4 val = read_imagef(main, sampler, loc);
        write_imagef(dst, loc, val);
    } else {
        int2 loc_overlay = (int2)(x_position, y_position);
        float4 val       = read_imagef(overlay, sampler, loc - loc_overlay);
        write_imagef(dst, loc, val);
    }
}

__kernel void overlay_internal_alpha(__write_only image2d_t dst,
                                     __read_only  image2d_t main,
                                     __read_only  image2d_t overlay,
                                     int x_position,
                                     int y_position)
{
    const sampler_t sampler = (CLK_NORMALIZED_COORDS_FALSE |
                               CLK_FILTER_NEAREST);

    int2 overlay_size = get_image_dim(overlay);
    int2 loc = (int2)(get_global_id(0), get_global_id(1));

    if (loc.x <  x_position ||
        loc.y <  y_position ||
        loc.x >= overlay_size.x + x_position ||
        loc.y >= overlay_size.y + y_position) {
        float4 val = read_imagef(main, sampler, loc);
        write_imagef(dst, loc, val);
    } else {
        int2 loc_overlay  = (int2)(x_position, y_position);
        float4 in_main    = read_imagef(main,    sampler, loc);
        float4 in_overlay = read_imagef(overlay, sampler, loc - loc_overlay);
        float4 val        = in_overlay * in_overlay.w + in_main * (1.0f - in_overlay.w);
        write_imagef(dst, loc, val);
    }
}

__kernel void overlay_external_alpha(__write_only image2d_t dst,
                                     __read_only  image2d_t main,
                                     __read_only  image2d_t overlay,
                                     __read_only  image2d_t alpha,
                                     int x_position,
                                     int y_position,
                                     int alpha_adj_x,
                                     int alpha_adj_y)
{
    const sampler_t sampler = (CLK_NORMALIZED_COORDS_FALSE |
                               CLK_FILTER_NEAREST);

    int2 overlay_size = get_image_dim(overlay);
    int2 loc = (int2)(get_global_id(0), get_global_id(1));

    if (loc.x <  x_position ||
        loc.y <  y_position ||
        loc.x >= overlay_size.x + x_position ||
        loc.y >= overlay_size.y + y_position) {
        float4 val = read_imagef(main, sampler, loc);
        write_imagef(dst, loc, val);
    } else {
        int2 loc_overlay  = (int2)(x_position, y_position);
        float4 in_main    = read_imagef(main,    sampler, loc);
        float4 in_overlay = read_imagef(overlay, sampler, loc - loc_overlay);

        int2 loc_alpha    = (int2)(loc.x * alpha_adj_x,
                                   loc.y * alpha_adj_y) - loc_overlay;
        float4 in_alpha   = read_imagef(alpha,   sampler, loc_alpha);

        float4 val = in_overlay * in_alpha.x + in_main * (1.0f - in_alpha.x);
        write_imagef(dst, loc, val);
    }
}
