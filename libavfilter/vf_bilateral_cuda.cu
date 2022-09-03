/*
 * Copyright (c) 2022 Mohamed Khaled <Mohamed_Khaled_Kamal@outlook.com>
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

extern "C"
{

/**
 * @brief               calculated squared norm difference between two 3-dimension vecors ||first_vector-second_vector||^2
 *                      used float4 for better performance
 *
 * @param first_yuv     first color vector
 * @param second_yuv    second color vecotr
 * @return              answer of squared norm difference
 */
__device__ static inline float norm_squared(float4 first_yuv, float4 second_yuv)
{
    float x = first_yuv.x - second_yuv.x;
    float y = first_yuv.y - second_yuv.y;
    float z = first_yuv.z - second_yuv.z;
    return (x*x) + (y*y) + (z*z);
}

/**
 * @brief               calculate w as stated in bilateral filter research paper
 *
 * @param first_yuv     first color vector
 * @param second_yuv    second color vecotr
 * @return              the calculated w
 */
__device__ static inline float calculate_w(int x, int y, int r, int c,
                                           float4 pixel_value, float4 neighbor_value,
                                           float sigma_space, float sigma_color)
{
    float first_term, second_term;
    first_term = (((x - r) * (x - r)) + ((y - c) * (y - c))) / (2 * sigma_space * sigma_space);
    second_term = norm_squared(pixel_value, neighbor_value) / (2 * sigma_color * sigma_color);
    return __expf(-first_term - second_term);
}

/**
 * @brief apply the bilateral filter on the pixel sent
 *
 * @param src_tex_Y         Y channel of source image
 * @param src_tex           U channel of source image if yuv, or UV channels if format is nv12
 * @param src_tex_V         V channel of source image
 * @param dst_Y             Y channel of destination image
 * @param dst_U             U channel of destination image if format is in yuv
 * @param dst_V             V channel of destination image if format is in yuv
 * @param dst_UV            UV channels of destination image if format is in nv12
 * @param width             width of Y channel
 * @param height            height of Y channel
 * @param width_uv          width of UV channels
 * @param height_uv         height of UV channels
 * @param pitch             pitch of Y channel
 * @param pitch_uv          pitch of UV channels
 * @param x                 x coordinate of pixel to be filtered
 * @param y                 y coordinate of pixel to be filtered
 * @param sigma_space       sigma space parameter
 * @param sigma_color       sigma color parameter
 * @param window_size       window size parameter
 * @return void
 */
__device__ static inline void apply_biltaeral(
    cudaTextureObject_t src_tex_Y, cudaTextureObject_t src_tex, cudaTextureObject_t src_tex_V,
    uchar *dst_Y, uchar *dst_U, uchar *dst_V, uchar2 *dst_UV,
    int width, int height, int width_uv, int height_uv, int pitch, int pitch_uv,
    int x, int y,
    float sigma_space, float sigma_color, int window_size)
{
    int start_r = x - window_size / 2;
    int start_c = y - window_size / 2;
    float4 neighbor_pixel = make_float4(0.f, 0.f, 0.f, 0.f);
    float Wp = 0.f;
    float4 new_pixel_value = make_float4(0.f, 0.f, 0.f, 0.f);
    float w = 0.f;

    int channel_ratio = width / width_uv; // ratio between Y channel and UV channels
    float4 currrent_pixel;

    if (!src_tex_V) { // format is in nv12
        float2 temp_uv   = tex2D<float2>(src_tex, x/channel_ratio, y/channel_ratio) * 255.f;
        currrent_pixel.x = tex2D<float>(src_tex_Y, x, y) * 255.f;
        currrent_pixel.y = temp_uv.x;
        currrent_pixel.z = temp_uv.y;
        currrent_pixel.w = 0.f;
    } else { // format is fully planar
        currrent_pixel = make_float4(tex2D<float>(src_tex_Y, x, y) * 255.f,
                                     tex2D<float>(src_tex,   x/channel_ratio, y/channel_ratio) * 255.f,
                                     tex2D<float>(src_tex_V, x/channel_ratio, y/channel_ratio) * 255.f,
                                     0.f);
    }

    for (int i=0; i < window_size; i++)
    {
        for (int j=0; j < window_size; j++)
        {
            int r=start_r+i;
            int c=start_c+j;
            bool in_bounds=r>=0 && r<width && c>=0 && c<height;
            if (in_bounds)
            {
                if (!src_tex_V){
                    float2 temp_uv = tex2D<float2>(src_tex, r/channel_ratio, c/channel_ratio);
                    neighbor_pixel=make_float4(tex2D<float>(src_tex_Y, r, c) * 255.f,
                                               temp_uv.x * 255.f,
                                               temp_uv.y * 255.f, 0.f);
                } else {
                    neighbor_pixel=make_float4(tex2D<float>(src_tex_Y, r, c) * 255.f,
                                               tex2D<float>(src_tex, r/channel_ratio, c/channel_ratio) * 255.f,
                                               tex2D<float>(src_tex_V, r/channel_ratio, c/channel_ratio) * 255.f, 0.f);
                }
                w=calculate_w(x,y,r,c,currrent_pixel,neighbor_pixel,sigma_space,sigma_color);
                Wp+=w;
                new_pixel_value+= neighbor_pixel*w;
            }
        }
    }

    new_pixel_value    = new_pixel_value / Wp;
    dst_Y[y*pitch + x] = new_pixel_value.x;

    if (!src_tex_V) {
        dst_UV[(y/channel_ratio) * pitch_uv + (x/channel_ratio)] = make_uchar2(new_pixel_value.y, new_pixel_value.z);
    } else {
        dst_U[(y/channel_ratio) * pitch_uv + (x/channel_ratio)]  = new_pixel_value.y;
        dst_V[(y/channel_ratio) * pitch_uv + (x/channel_ratio)]  = new_pixel_value.z;
    }

    return;
}


__global__ void Process_uchar(cudaTextureObject_t src_tex_Y, cudaTextureObject_t src_tex_U, cudaTextureObject_t src_tex_V,
                              uchar *dst_Y, uchar *dst_U, uchar *dst_V,
                              int width, int height, int pitch,
                              int width_uv, int height_uv, int pitch_uv,
                              int window_size, float sigmaS, float sigmaR)
{

    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (y >= height || x >= width)
        return;

    apply_biltaeral(src_tex_Y, src_tex_U, src_tex_V,
                    dst_Y, dst_U, dst_V, (uchar2*)nullptr,
                    width, height, width_uv, height_uv, pitch, pitch_uv,
                    x, y,
                    sigmaS, sigmaR, window_size);
}


__global__ void Process_uchar2(cudaTextureObject_t src_tex_Y, cudaTextureObject_t src_tex_UV, cudaTextureObject_t unused1,
                               uchar *dst_Y, uchar2 *dst_UV, uchar *unused2,
                               int width, int height, int pitch,
                               int width_uv, int height_uv, int pitch_uv,
                               int window_size, float sigmaS, float sigmaR)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (y >= height || x >= width)
        return;

    apply_biltaeral(src_tex_Y, src_tex_UV, (cudaTextureObject_t)nullptr,
                    dst_Y, (uchar*)nullptr, (uchar*)nullptr, dst_UV,
                    width, height, width_uv, height_uv, pitch, pitch_uv,
                    x, y,
                    sigmaS, sigmaR, window_size);
}

}
