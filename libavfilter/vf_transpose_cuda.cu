/*
 * Copyright (C) 2026 NyanMisaka
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

#include "cuda/vector_helpers.cuh"

__inline__ __device__ void map_input_ncoords(float *xi, float *yi,
                                             int xo, int yo,
                                             int dst_width, int dst_height,
                                             int dir)
{
    int flip_wh = dir < 4;
    *xi = flip_wh ? ((dir &  2) ? (dst_height - 1 - yo) : yo)
                  : ((dir == 6) ? xo : (dst_width  - 1 - xo));
    *yi = flip_wh ? ((dir &  1) ? (dst_width  - 1 - xo) : xo)
                  : ((dir == 5) ? yo : (dst_height - 1 - yo));

    *xi = (*xi + 0.5f) / (flip_wh ? dst_height : dst_width);
    *yi = (*yi + 0.5f) / (flip_wh ? dst_width : dst_height);
}

template<typename T, int DST1, int FACTOR>
__inline__ __device__ void Transpose_Cuda(
    T *dst0, T *dst1, int dst_width, int dst_height, int dst_pitch,
    cudaTextureObject_t src0_tex, cudaTextureObject_t src1_tex, int dir)
{
    int xo = blockIdx.x * blockDim.x + threadIdx.x;
    int yo = blockIdx.y * blockDim.y + threadIdx.y;
    if (xo >= dst_width || yo >= dst_height)
        return;

    float xi, yi;
    map_input_ncoords(&xi, &yi, xo, yo,
                      dst_width, dst_height, dir);

    dst0[yo*dst_pitch+xo] = from_floatN<T, floatT>(
        saturate_rintf<floatT>(
            tex2D<floatT>(src0_tex, xi, yi), FACTOR
        )
    );
    if (DST1 && dst1 && src1_tex) {
        dst1[yo*dst_pitch+xo] = from_floatN<T, floatT>(
            saturate_rintf<floatT>(
                tex2D<floatT>(src1_tex, xi, yi), FACTOR
            )
        );
    }
}

extern "C" {

#define TRANSPOSE_KERNEL(NAME, TYPE, DST1, FACTOR) \
__global__ void Transpose_Cuda_ ## NAME(                                  \
    TYPE *dst0, TYPE *dst1, int dst_width, int dst_height, int dst_pitch, \
    cudaTextureObject_t src0_tex, cudaTextureObject_t src1_tex, int dir)  \
{                                                                         \
    Transpose_Cuda<TYPE, DST1, FACTOR>(                                   \
        dst0, dst1, dst_width, dst_height, dst_pitch,                     \
        src0_tex, src1_tex, dir                                           \
    );                                                                    \
}

TRANSPOSE_KERNEL(uchar, uchar, 1, 0xFF)
TRANSPOSE_KERNEL(ushort, ushort, 1, 0xFFFF)
TRANSPOSE_KERNEL(uchar2, uchar2, 0, 0xFF)
TRANSPOSE_KERNEL(ushort2, ushort2, 0, 0xFFFF)
TRANSPOSE_KERNEL(uchar4, uchar4, 0, 0xFF)

} /* extern "C" */
