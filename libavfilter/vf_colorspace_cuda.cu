/*
 * Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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
#define MPEG_LUMA_MIN   (16)
#define MPEG_CHROMA_MIN (16)
#define MPEG_LUMA_MAX   (235)
#define MPEG_CHROMA_MAX (240)

#define JPEG_LUMA_MIN   (0)
#define JPEG_CHROMA_MIN (1)
#define JPEG_LUMA_MAX   (255)
#define JPEG_CHROMA_MAX (255)

__device__ int mpeg_min[] = {MPEG_LUMA_MIN, MPEG_CHROMA_MIN};
__device__ int mpeg_max[] = {MPEG_LUMA_MAX, MPEG_CHROMA_MAX};

__device__ int jpeg_min[] = {JPEG_LUMA_MIN, JPEG_CHROMA_MIN};
__device__ int jpeg_max[] = {JPEG_LUMA_MAX, JPEG_CHROMA_MAX};

__device__ int clamp(int val, int min, int max)
{
    if (val < min)
        return min;
    else if (val > max)
        return max;
    else
        return val;
}

__global__ void to_jpeg_cuda(const unsigned char* src, unsigned char* dst,
                             int pitch, int comp_id)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    int src_, dst_;

    // 8 bit -> 15 bit for better precision
    src_ = static_cast<int>(src[x + y * pitch]) << 7;

    // Conversion
    dst_ = comp_id ? (min(src_, 30775) * 4663 - 9289992) >> 12    // chroma
                   : (min(src_, 30189) * 19077 - 39057361) >> 14; // luma

    // Dither replacement
    dst_ = dst_ + 64;

    // Back to 8 bit
    dst_ = clamp(dst_ >> 7, jpeg_min[comp_id], jpeg_max[comp_id]);
    dst[x + y * pitch] = static_cast<unsigned char>(dst_);
}

__global__ void to_mpeg_cuda(const unsigned char* src, unsigned char* dst,
                             int pitch, int comp_id)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    int src_, dst_;

    // 8 bit -> 15 bit for better precision
    src_ = static_cast<int>(src[x + y * pitch]) << 7;

    // Conversion
    dst_ = comp_id ? (src_ * 1799 + 4081085) >> 11    // chroma
                   : (src_ * 14071 + 33561947) >> 14; // luma

    // Dither replacement
    dst_ = dst_ + 64;

    // Back to 8 bit
    dst_ = clamp(dst_ >> 7, mpeg_min[comp_id], mpeg_max[comp_id]);
    dst[x + y * pitch] = static_cast<unsigned char>(dst_);
}

}
