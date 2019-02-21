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

extern "C" {

__global__ void Thumbnail_uchar(cudaTextureObject_t uchar_tex,
                                int *histogram, int src_width, int src_height)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (y < src_height && x < src_width)
    {
        unsigned char pixel = tex2D<unsigned char>(uchar_tex, x, y);
        atomicAdd(&histogram[pixel], 1);
    }
}

__global__ void Thumbnail_uchar2(cudaTextureObject_t uchar2_tex,
                                 int *histogram, int src_width, int src_height)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (y < src_height && x < src_width)
    {
        uchar2 pixel = tex2D<uchar2>(uchar2_tex, x, y);
        atomicAdd(&histogram[pixel.x], 1);
        atomicAdd(&histogram[256 + pixel.y], 1);
    }
}

__global__ void Thumbnail_ushort(cudaTextureObject_t ushort_tex,
                                 int *histogram, int src_width, int src_height)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (y < src_height && x < src_width)
    {
        unsigned short pixel = (tex2D<unsigned short>(ushort_tex, x, y) + 128) >> 8;
        atomicAdd(&histogram[pixel], 1);
    }
}

__global__ void Thumbnail_ushort2(cudaTextureObject_t ushort2_tex,
                                  int *histogram, int src_width, int src_height)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (y < src_height && x < src_width)
    {
        ushort2 pixel = tex2D<ushort2>(ushort2_tex, x, y);
        atomicAdd(&histogram[(pixel.x + 128) >> 8], 1);
        atomicAdd(&histogram[256 + (pixel.y + 128) >> 8], 1);
    }
}

}
