/*
 * Copyright (C) 2013 Wei Gao <weigao@multicorewareinc.com>
 * Copyright (C) 2013 Lenny Wang
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

#ifndef AVFILTER_UNSHARP_OPENCL_KERNEL_H
#define AVFILTER_UNSHARP_OPENCL_KERNEL_H

#include "libavutil/opencl.h"

const char *ff_kernel_unsharp_opencl = AV_OPENCL_KERNEL(
inline unsigned char clip_uint8(int a)
{
    if (a & (~0xFF))
        return (-a)>>31;
    else
        return a;
}

kernel void unsharp_luma(
                    global unsigned char *src,
                    global unsigned char *dst,
                    global int *mask_x,
                    global int *mask_y,
                    int amount,
                    int scalebits,
                    int halfscale,
                    int src_stride,
                    int dst_stride,
                    int width,
                    int height)
{
    int2 threadIdx, blockIdx, globalIdx;
    threadIdx.x = get_local_id(0);
    threadIdx.y = get_local_id(1);
    blockIdx.x = get_group_id(0);
    blockIdx.y = get_group_id(1);
    globalIdx.x = get_global_id(0);
    globalIdx.y = get_global_id(1);

    if (!amount) {
        if (globalIdx.x < width && globalIdx.y < height)
            dst[globalIdx.x + globalIdx.y*dst_stride] = src[globalIdx.x + globalIdx.y*src_stride];
        return;
    }

    local unsigned int l[32][32];
    local unsigned int lcx[LU_RADIUS_X];
    local unsigned int lcy[LU_RADIUS_Y];
    int indexIx, indexIy, i, j;

    //load up tile: actual workspace + halo of 8 points in x and y \n
    for(i = 0; i <= 1; i++) {
        indexIy = -8 + (blockIdx.y + i) * 16 + threadIdx.y;
        indexIy = indexIy < 0 ? 0 : indexIy;
        indexIy = indexIy >= height ? height - 1: indexIy;
        for(j = 0; j <= 1; j++) {
            indexIx = -8 + (blockIdx.x + j) * 16 + threadIdx.x;
            indexIx = indexIx < 0 ? 0 : indexIx;
            indexIx = indexIx >= width ? width - 1: indexIx;
            l[i*16 + threadIdx.y][j*16 + threadIdx.x] = src[indexIy*src_stride + indexIx];
        }
    }

    int indexL = threadIdx.y*16 + threadIdx.x;
    if (indexL < LU_RADIUS_X)
        lcx[indexL] = mask_x[indexL];
    if (indexL < LU_RADIUS_Y)
        lcy[indexL] = mask_y[indexL];
    barrier(CLK_LOCAL_MEM_FENCE);

    //needed for unsharp mask application in the end \n
    int orig_value = (int)l[threadIdx.y + 8][threadIdx.x + 8];

    int idx, idy, maskIndex;
    int temp[2] = {0};
    int steps_x = (LU_RADIUS_X-1)/2;
    int steps_y = (LU_RADIUS_Y-1)/2;

    // compute the actual workspace + left&right halos \n
      \n#pragma unroll\n
    for (j = 0; j <=1; j++) {
      //extra work to cover left and right halos \n
      idx = 16*j + threadIdx.x;
      \n#pragma unroll\n
        for (i = -steps_y; i <= steps_y; i++) {
          idy = 8 + i + threadIdx.y;
          maskIndex = (i + steps_y);
          temp[j] += (int)l[idy][idx] * lcy[maskIndex];
        }
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    //save results from the vertical filter in local memory \n
    idy = 8 + threadIdx.y;
      \n#pragma unroll\n
    for (j = 0; j <=1; j++) {
      idx = 16*j + threadIdx.x;
      l[idy][idx] = temp[j];
    }
    barrier(CLK_LOCAL_MEM_FENCE);

    //compute results with the horizontal filter \n
    int sum = 0;
    idy = 8 + threadIdx.y;
    \n#pragma unroll\n
      for (j = -steps_x; j <= steps_x; j++) {
        idx = 8 + j + threadIdx.x;
        maskIndex = j + steps_x;
        sum += (int)l[idy][idx] * lcx[maskIndex];
      }

    int res = orig_value + (((orig_value - (int)((sum + halfscale) >> scalebits)) * amount) >> 16);

    if (globalIdx.x < width && globalIdx.y < height)
        dst[globalIdx.x + globalIdx.y*dst_stride] = clip_uint8(res);
}

kernel void unsharp_chroma(
                    global unsigned char *src_y,
                    global unsigned char *dst_y,
                    global int *mask_x,
                    global int *mask_y,
                    int amount,
                    int scalebits,
                    int halfscale,
                    int src_stride_lu,
                    int src_stride_ch,
                    int dst_stride_lu,
                    int dst_stride_ch,
                    int width,
                    int height,
                    int cw,
                    int ch)
{
    global unsigned char *dst_u = dst_y + height * dst_stride_lu;
    global unsigned char *dst_v = dst_u + ch * dst_stride_ch;
    global unsigned char *src_u = src_y + height * src_stride_lu;
    global unsigned char *src_v = src_u + ch * src_stride_ch;
    int2 threadIdx, blockIdx, globalIdx;
    threadIdx.x = get_local_id(0);
    threadIdx.y = get_local_id(1);
    blockIdx.x = get_group_id(0);
    blockIdx.y = get_group_id(1);
    globalIdx.x = get_global_id(0);
    globalIdx.y = get_global_id(1);
    int padch = get_global_size(1)/2;
    global unsigned char *src = globalIdx.y>=padch ? src_v : src_u;
    global unsigned char *dst = globalIdx.y>=padch ? dst_v : dst_u;

    blockIdx.y = globalIdx.y>=padch ? blockIdx.y - get_num_groups(1)/2 : blockIdx.y;
    globalIdx.y = globalIdx.y>=padch ? globalIdx.y - padch : globalIdx.y;

    if (!amount) {
        if (globalIdx.x < cw && globalIdx.y < ch)
            dst[globalIdx.x + globalIdx.y*dst_stride_ch] = src[globalIdx.x + globalIdx.y*src_stride_ch];
        return;
    }

    local unsigned int l[32][32];
    local unsigned int lcx[CH_RADIUS_X];
    local unsigned int lcy[CH_RADIUS_Y];
    int indexIx, indexIy, i, j;
    for(i = 0; i <= 1; i++) {
        indexIy = -8 + (blockIdx.y + i) * 16 + threadIdx.y;
        indexIy = indexIy < 0 ? 0 : indexIy;
        indexIy = indexIy >= ch ? ch - 1: indexIy;
        for(j = 0; j <= 1; j++) {
            indexIx = -8 + (blockIdx.x + j) * 16 + threadIdx.x;
            indexIx = indexIx < 0 ? 0 : indexIx;
            indexIx = indexIx >= cw ? cw - 1: indexIx;
            l[i*16 + threadIdx.y][j*16 + threadIdx.x] = src[indexIy * src_stride_ch + indexIx];
        }
    }

    int indexL = threadIdx.y*16 + threadIdx.x;
    if (indexL < CH_RADIUS_X)
        lcx[indexL] = mask_x[indexL];
    if (indexL < CH_RADIUS_Y)
        lcy[indexL] = mask_y[indexL];
    barrier(CLK_LOCAL_MEM_FENCE);

    int orig_value = (int)l[threadIdx.y + 8][threadIdx.x + 8];

    int idx, idy, maskIndex;
    int steps_x = CH_RADIUS_X/2;
    int steps_y = CH_RADIUS_Y/2;
    int temp[2] = {0,0};

    \n#pragma unroll\n
      for (j = 0; j <= 1; j++) {
        idx = 16*j + threadIdx.x;
        \n#pragma unroll\n
          for (i = -steps_y; i <= steps_y; i++) {
            idy = 8 + i + threadIdx.y;
            maskIndex = i + steps_y;
            temp[j] += (int)l[idy][idx] * lcy[maskIndex];
          }
      }

    barrier(CLK_LOCAL_MEM_FENCE);
    idy = 8 + threadIdx.y;
    \n#pragma unroll\n
    for (j = 0; j <= 1; j++) {
      idx = 16*j + threadIdx.x;
      l[idy][idx] = temp[j];
    }
    barrier(CLK_LOCAL_MEM_FENCE);

    //compute results with the horizontal filter \n
    int sum = 0;
    idy = 8 + threadIdx.y;
    \n#pragma unroll\n
      for (j = -steps_x; j <= steps_x; j++) {
        idx = 8 + j + threadIdx.x;
        maskIndex = j + steps_x;
        sum += (int)l[idy][idx] * lcx[maskIndex];
      }

    int res = orig_value + (((orig_value - (int)((sum + halfscale) >> scalebits)) * amount) >> 16);

    if (globalIdx.x < cw && globalIdx.y < ch)
        dst[globalIdx.x + globalIdx.y*dst_stride_ch] = clip_uint8(res);
}

kernel void unsharp_default(global  unsigned char *src,
                    global  unsigned char *dst,
                    const global  unsigned int *mask_lu,
                    const global  unsigned int *mask_ch,
                    int amount_lu,
                    int amount_ch,
                    int step_x_lu,
                    int step_y_lu,
                    int step_x_ch,
                    int step_y_ch,
                    int scalebits_lu,
                    int scalebits_ch,
                    int halfscale_lu,
                    int halfscale_ch,
                    int src_stride_lu,
                    int src_stride_ch,
                    int dst_stride_lu,
                    int dst_stride_ch,
                    int height,
                    int width,
                    int ch,
                    int cw)
{
    global unsigned char *dst_y = dst;
    global unsigned char *dst_u = dst_y + height * dst_stride_lu;
    global unsigned char *dst_v = dst_u + ch * dst_stride_ch;

    global unsigned char *src_y = src;
    global unsigned char *src_u = src_y + height * src_stride_lu;
    global unsigned char *src_v = src_u + ch * src_stride_ch;

    global unsigned char *temp_dst;
    global unsigned char *temp_src;
    const global unsigned int *temp_mask;
    int global_id = get_global_id(0);
    int i, j, x, y, temp_src_stride, temp_dst_stride, temp_height, temp_width, temp_steps_x, temp_steps_y,
        temp_amount, temp_scalebits, temp_halfscale, sum, idx_x, idx_y, temp, res;
    if (global_id < width * height) {
        y = global_id / width;
        x = global_id % width;
        temp_dst = dst_y;
        temp_src = src_y;
        temp_src_stride = src_stride_lu;
        temp_dst_stride = dst_stride_lu;
        temp_height = height;
        temp_width = width;
        temp_steps_x = step_x_lu;
        temp_steps_y = step_y_lu;
        temp_mask = mask_lu;
        temp_amount = amount_lu;
        temp_scalebits = scalebits_lu;
        temp_halfscale = halfscale_lu;
    } else if ((global_id >= width * height) && (global_id < width * height + ch * cw)) {
        y = (global_id - width * height) / cw;
        x = (global_id - width * height) % cw;
        temp_dst = dst_u;
        temp_src = src_u;
        temp_src_stride = src_stride_ch;
        temp_dst_stride = dst_stride_ch;
        temp_height = ch;
        temp_width = cw;
        temp_steps_x = step_x_ch;
        temp_steps_y = step_y_ch;
        temp_mask = mask_ch;
        temp_amount = amount_ch;
        temp_scalebits = scalebits_ch;
        temp_halfscale = halfscale_ch;
    } else {
        y = (global_id - width * height - ch * cw) / cw;
        x = (global_id - width * height - ch * cw) % cw;
        temp_dst = dst_v;
        temp_src = src_v;
        temp_src_stride = src_stride_ch;
        temp_dst_stride = dst_stride_ch;
        temp_height = ch;
        temp_width = cw;
        temp_steps_x = step_x_ch;
        temp_steps_y = step_y_ch;
        temp_mask = mask_ch;
        temp_amount = amount_ch;
        temp_scalebits = scalebits_ch;
        temp_halfscale = halfscale_ch;
    }
    if (temp_amount) {
        sum = 0;
        for (j = 0; j <= 2 * temp_steps_y; j++) {
            idx_y = (y - temp_steps_y + j) <= 0 ? 0 : (y - temp_steps_y + j) >= temp_height ? temp_height-1 : y - temp_steps_y + j;
            for (i = 0; i <= 2 * temp_steps_x; i++) {
                idx_x = (x - temp_steps_x + i) <= 0 ? 0 : (x - temp_steps_x + i) >= temp_width ? temp_width-1 : x - temp_steps_x + i;
                sum += temp_mask[i + j * (2 * temp_steps_x + 1)] * temp_src[idx_x + idx_y * temp_src_stride];
            }
        }
        temp = (int)temp_src[x + y * temp_src_stride];
        res = temp + (((temp - (int)((sum + temp_halfscale) >> temp_scalebits)) * temp_amount) >> 16);
        temp_dst[x + y * temp_dst_stride] = clip_uint8(res);
    } else {
        temp_dst[x + y * temp_dst_stride] = temp_src[x + y * temp_src_stride];
    }
}
);

#endif /* AVFILTER_UNSHARP_OPENCL_KERNEL_H */
