/*
 * This file is part of FFmpeg.
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
#include "vf_scale_cuda.h"

typedef float4 (*coeffs_function_t)(float, float);

__device__ inline float4 lanczos_coeffs(float x, float param)
{
    const float pi = 3.141592654f;

    float4 res = make_float4(
        pi * (x + 1),
        pi * x,
        pi * (x - 1),
        pi * (x - 2));

    res.x = res.x == 0.0f ? 1.0f :
        __sinf(res.x) * __sinf(res.x / 2.0f) / (res.x * res.x / 2.0f);
    res.y = res.y == 0.0f ? 1.0f :
        __sinf(res.y) * __sinf(res.y / 2.0f) / (res.y * res.y / 2.0f);
    res.z = res.z == 0.0f ? 1.0f :
        __sinf(res.z) * __sinf(res.z / 2.0f) / (res.z * res.z / 2.0f);
    res.w = res.w == 0.0f ? 1.0f :
        __sinf(res.w) * __sinf(res.w / 2.0f) / (res.w * res.w / 2.0f);

    return res / (res.x + res.y + res.z + res.w);
}

__device__ inline float4 bicubic_coeffs(float x, float param)
{
    const float A = param == SCALE_CUDA_PARAM_DEFAULT ? 0.0f : -param;

    float4 res;
    res.x = ((A * (x + 1) - 5 * A) * (x + 1) + 8 * A) * (x + 1) - 4 * A;
    res.y = ((A + 2) * x - (A + 3)) * x * x + 1;
    res.z = ((A + 2) * (1 - x) - (A + 3)) * (1 - x) * (1 - x) + 1;
    res.w = 1.0f - res.x - res.y - res.z;

    return res;
}

template<typename V>
__device__ inline V apply_coeffs(float4 coeffs, V c0, V c1, V c2, V c3)
{
    V res = c0 * coeffs.x;
    res  += c1 * coeffs.y;
    res  += c2 * coeffs.z;
    res  += c3 * coeffs.w;

    return res;
}

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

template<typename T>
__device__ inline void Subsample_Bicubic(coeffs_function_t coeffs_function,
                                         cudaTextureObject_t tex,
                                         T *dst,
                                         int dst_width, int dst_height, int dst_pitch,
                                         int src_width, int src_height,
                                         int bit_depth, float param)
{
    int xo = blockIdx.x * blockDim.x + threadIdx.x;
    int yo = blockIdx.y * blockDim.y + threadIdx.y;

    if (yo < dst_height && xo < dst_width)
    {
        float hscale = (float)src_width / (float)dst_width;
        float vscale = (float)src_height / (float)dst_height;
        float xi = (xo + 0.5f) * hscale - 0.5f;
        float yi = (yo + 0.5f) * vscale - 0.5f;
        float px = floor(xi);
        float py = floor(yi);
        float fx = xi - px;
        float fy = yi - py;

        float factor = bit_depth > 8 ? 0xFFFF : 0xFF;

        float4 coeffsX = coeffs_function(fx, param);
        float4 coeffsY = coeffs_function(fy, param);

#define PIX(x, y) tex2D<floatT>(tex, (x), (y))

        dst[yo * dst_pitch + xo] = from_floatN<T, floatT>(
            apply_coeffs<floatT>(coeffsY,
                apply_coeffs<floatT>(coeffsX, PIX(px - 1, py - 1), PIX(px, py - 1), PIX(px + 1, py - 1), PIX(px + 2, py - 1)),
                apply_coeffs<floatT>(coeffsX, PIX(px - 1, py    ), PIX(px, py    ), PIX(px + 1, py    ), PIX(px + 2, py    )),
                apply_coeffs<floatT>(coeffsX, PIX(px - 1, py + 1), PIX(px, py + 1), PIX(px + 1, py + 1), PIX(px + 2, py + 1)),
                apply_coeffs<floatT>(coeffsX, PIX(px - 1, py + 2), PIX(px, py + 2), PIX(px + 1, py + 2), PIX(px + 2, py + 2))
            ) * factor
        );

#undef PIX
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

#define BICUBIC_KERNEL(T) \
    __global__ void Subsample_Bicubic_ ## T(cudaTextureObject_t src_tex,                  \
                                            T *dst,                                       \
                                            int dst_width, int dst_height, int dst_pitch, \
                                            int src_width, int src_height,                \
                                            int bit_depth, float param)                   \
    {                                                                                     \
        Subsample_Bicubic<T>(&bicubic_coeffs, src_tex, dst,                               \
                             dst_width, dst_height, dst_pitch,                            \
                             src_width, src_height,                                       \
                             bit_depth, param);                                           \
    }

BICUBIC_KERNEL(uchar)
BICUBIC_KERNEL(uchar2)
BICUBIC_KERNEL(uchar4)

BICUBIC_KERNEL(ushort)
BICUBIC_KERNEL(ushort2)
BICUBIC_KERNEL(ushort4)


#define LANCZOS_KERNEL(T) \
    __global__ void Subsample_Lanczos_ ## T(cudaTextureObject_t src_tex,                  \
                                            T *dst,                                       \
                                            int dst_width, int dst_height, int dst_pitch, \
                                            int src_width, int src_height,                \
                                            int bit_depth, float param)                   \
    {                                                                                     \
        Subsample_Bicubic<T>(&lanczos_coeffs, src_tex, dst,                               \
                             dst_width, dst_height, dst_pitch,                            \
                             src_width, src_height,                                       \
                             bit_depth, param);                                           \
    }

LANCZOS_KERNEL(uchar)
LANCZOS_KERNEL(uchar2)
LANCZOS_KERNEL(uchar4)

LANCZOS_KERNEL(ushort)
LANCZOS_KERNEL(ushort2)
LANCZOS_KERNEL(ushort4)

}
