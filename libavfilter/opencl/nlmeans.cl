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
                           CLK_ADDRESS_CLAMP_TO_EDGE   |
                           CLK_FILTER_NEAREST);

kernel void horiz_sum(__global uint4 *integral_img,
                      __read_only image2d_t src,
                      int width,
                      int height,
                      int4 dx,
                      int4 dy)
{

    int y = get_global_id(0);
    int work_size = get_global_size(0);

    uint4 sum = (uint4)(0);
    float4 s2;
    for (int i = 0; i < width; i++) {
        float s1 = read_imagef(src, sampler, (int2)(i, y)).x;
        s2.x = read_imagef(src, sampler, (int2)(i + dx.x, y + dy.x)).x;
        s2.y = read_imagef(src, sampler, (int2)(i + dx.y, y + dy.y)).x;
        s2.z = read_imagef(src, sampler, (int2)(i + dx.z, y + dy.z)).x;
        s2.w = read_imagef(src, sampler, (int2)(i + dx.w, y + dy.w)).x;
        sum += convert_uint4((s1 - s2) * (s1 - s2) * 255 * 255);
        integral_img[y * width + i] = sum;
    }
}

kernel void vert_sum(__global uint4 *integral_img,
                     __global int *overflow,
                     int width,
                     int height)
{
    int x = get_global_id(0);
    uint4 sum = 0;
    for (int i = 0; i < height; i++) {
        if (any((uint4)UINT_MAX - integral_img[i * width + x] < sum))
            atomic_inc(overflow);
        integral_img[i * width + x] += sum;
        sum = integral_img[i * width + x];
    }
}

kernel void weight_accum(global float *sum, global float *weight,
                         global uint4 *integral_img, __read_only image2d_t src,
                         int width, int height, int p, float h,
                         int4 dx, int4 dy)
{
    // w(x) = integral_img(x-p, y-p) +
    //        integral_img(x+p, y+p) -
    //        integral_img(x+p, y-p) -
    //        integral_img(x-p, y+p)
    // total_sum[x] += w(x, y) * src(x + dx, y + dy)
    // total_weight += w(x, y)

    int x = get_global_id(0);
    int y = get_global_id(1);
    int4 xoff = x + dx;
    int4 yoff = y + dy;
    uint4 a = 0, b = 0, c = 0, d = 0;
    uint4 src_pix = 0;

    // out-of-bounding-box?
    int oobb = (x - p) < 0 || (y - p) < 0 || (y + p) >= height || (x + p) >= width;

    src_pix.x = (int)(255 * read_imagef(src, sampler, (int2)(xoff.x, yoff.x)).x);
    src_pix.y = (int)(255 * read_imagef(src, sampler, (int2)(xoff.y, yoff.y)).x);
    src_pix.z = (int)(255 * read_imagef(src, sampler, (int2)(xoff.z, yoff.z)).x);
    src_pix.w = (int)(255 * read_imagef(src, sampler, (int2)(xoff.w, yoff.w)).x);
    if (!oobb) {
        a = integral_img[(y - p) * width + x - p];
        b = integral_img[(y + p) * width + x - p];
        c = integral_img[(y - p) * width + x + p];
        d = integral_img[(y + p) * width + x + p];
    }

    float4 patch_diff = convert_float4(d + a - c - b);
    float4 w = native_exp(-patch_diff / (h * h));
    float w_sum = w.x + w.y + w.z + w.w;
    weight[y * width + x] += w_sum;
    sum[y * width + x] += dot(w, convert_float4(src_pix));
}

kernel void average(__write_only image2d_t dst,
                    __read_only image2d_t src,
                    global float *sum, global float *weight) {
    int x = get_global_id(0);
    int y = get_global_id(1);
    int2 dim = get_image_dim(dst);

    float w = weight[y * dim.x + x];
    float s = sum[y * dim.x + x];
    float src_pix = read_imagef(src, sampler, (int2)(x, y)).x;
    float r = (s + src_pix * 255) / (1.0f + w) / 255.0f;
    if (x < dim.x && y < dim.y)
        write_imagef(dst, (int2)(x, y), (float4)(r, 0.0f, 0.0f, 1.0f));
}
