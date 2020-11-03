/*
 * Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "cuda/vector_helpers.cuh"

template<typename T>
__device__ inline void Subsample_Nearest(cudaTextureObject_t tex,
                                         T *dst,
                                         int dst_width, int dst_height, int dst_pitch,
                                         int src_width, int src_height,
                                         int bit_depth)
{
    int xo = blockIdx.x * blockDim.x + threadIdx.x;
    int yo = blockIdx.y * blockDim.y + threadIdx.y;

    if (yo < dst_height && xo < dst_width)
    {
        float hscale = (float)src_width / (float)dst_width;
        float vscale = (float)src_height / (float)dst_height;
        float xi = (xo + 0.5f) * hscale;
        float yi = (yo + 0.5f) * vscale;

        dst[yo*dst_pitch+xo] = tex2D<T>(tex, xi, yi);
    }
}

template<typename T>
__device__ inline void Subsample_Bilinear(cudaTextureObject_t tex,
                                          T *dst,
                                          int dst_width, int dst_height, int dst_pitch,
                                          int src_width, int src_height,
                                          int bit_depth)
{
    int xo = blockIdx.x * blockDim.x + threadIdx.x;
    int yo = blockIdx.y * blockDim.y + threadIdx.y;

    if (yo < dst_height && xo < dst_width)
    {
        float hscale = (float)src_width / (float)dst_width;
        float vscale = (float)src_height / (float)dst_height;
        float xi = (xo + 0.5f) * hscale;
        float yi = (yo + 0.5f) * vscale;
        // 3-tap filter weights are {wh,1.0,wh} and {wv,1.0,wv}
        float wh = min(max(0.5f * (hscale - 1.0f), 0.0f), 1.0f);
        float wv = min(max(0.5f * (vscale - 1.0f), 0.0f), 1.0f);
        // Convert weights to two bilinear weights -> {wh,1.0,wh} -> {wh,0.5,0} + {0,0.5,wh}
        float dx = wh / (0.5f + wh);
        float dy = wv / (0.5f + wv);

        intT r = { 0 };
        vec_set_scalar(r, 2);
        r += tex2D<T>(tex, xi - dx, yi - dy);
        r += tex2D<T>(tex, xi + dx, yi - dy);
        r += tex2D<T>(tex, xi - dx, yi + dy);
        r += tex2D<T>(tex, xi + dx, yi + dy);
        vec_set(dst[yo*dst_pitch+xo], r >> 2);
    }
}

extern "C" {

#define NEAREST_KERNEL(T) \
    __global__ void Subsample_Nearest_ ## T(cudaTextureObject_t src_tex,                  \
                                            T *dst,                                       \
                                            int dst_width, int dst_height, int dst_pitch, \
                                            int src_width, int src_height,                \
                                            int bit_depth)                                \
    {                                                                                     \
        Subsample_Nearest<T>(src_tex, dst,                                                \
                              dst_width, dst_height, dst_pitch,                           \
                              src_width, src_height,                                      \
                              bit_depth);                                                 \
    }

NEAREST_KERNEL(uchar)
NEAREST_KERNEL(uchar2)
NEAREST_KERNEL(uchar4)

NEAREST_KERNEL(ushort)
NEAREST_KERNEL(ushort2)
NEAREST_KERNEL(ushort4)

#define BILINEAR_KERNEL(T) \
    __global__ void Subsample_Bilinear_ ## T(cudaTextureObject_t src_tex,                  \
                                             T *dst,                                       \
                                             int dst_width, int dst_height, int dst_pitch, \
                                             int src_width, int src_height,                \
                                             int bit_depth)                                \
    {                                                                                      \
        Subsample_Bilinear<T>(src_tex, dst,                                                \
                              dst_width, dst_height, dst_pitch,                            \
                              src_width, src_height,                                       \
                              bit_depth);                                                  \
    }

BILINEAR_KERNEL(uchar)
BILINEAR_KERNEL(uchar2)
BILINEAR_KERNEL(uchar4)

BILINEAR_KERNEL(ushort)
BILINEAR_KERNEL(ushort2)
BILINEAR_KERNEL(ushort4)

}
