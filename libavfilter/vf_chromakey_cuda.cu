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
 * @brief function contains the main logic of chroma keying, and changes the alpahc channel with the suitable value
 *
 * @param src_tex           texture U or texture UV , decided based on the passed is_uchar2 flag
 * @param src_tex_V         texture V , used only if is_uchar2 flag is false
 * @param dst_A             alpha channel destination
 * @param width_uv          width of uv channels
 * @param height_uv         height of uv channels
 * @param width             width of alpha channel
 * @param height            height of alpha channel
 * @param pitch             pitch of alpha channel
 * @param x                 current x coordinate of pixel
 * @param y                 current y coordinate of pixel
 * @param chromakey_uv      uv values for chroma keying
 * @param similarity        similarity of keying
 * @param blend             blend of keying
 */
__device__ static inline void change_alpha_channel(
    cudaTextureObject_t src_tex, cudaTextureObject_t src_tex_V, uchar *dst_A,
    int width_uv, int height_uv,
    int width, int height, int pitch,
    int x, int y,
    float2 chromakey_uv, float similarity, float blend)
{
    int window_size = 3;
    int start_r = x - window_size / 2;
    int start_c = y - window_size / 2;
    int resize_ratio = width / width_uv;
    int counter = 0;
    float diff = 0.0f;
    float du, dv;
    uchar alpha_value;

    // loop over the eight neighbourhood of the current pixel(x,y)
    for (uchar i = 0; i < window_size; i++)
    {
        for (uchar j = 0; j < window_size; j++)
        {
            float u_value, v_value;
            int r = start_r + i;
            int c = start_c + j;

            if (r < 0 || r >= width_uv || c < 0 || c >= height_uv)
                continue;

            if (!src_tex_V) {
                float2 temp_uv = tex2D<float2>(src_tex, r, c);
                u_value = temp_uv.x;
                v_value = temp_uv.y;
            } else {
                u_value = tex2D<float>(src_tex, r, c);
                v_value = tex2D<float>(src_tex_V, r, c);
            }

            du = (u_value * 255.0f) - chromakey_uv.x;
            dv = (v_value * 255.0f) - chromakey_uv.y;
            diff += sqrtf((du * du + dv * dv) / (255.0f * 255.0f * 2.f));

            counter++;
        }
    }

    if (counter > 0)
        diff = diff / counter;
    else
        diff /= 9.0f;

    if (blend>0.0001f)
        alpha_value = __saturatef((diff - similarity) / blend) * 255;
    else
        alpha_value = (diff < similarity) ? 0 : 255;

    //write the value in the alpha channel with regarding the ratio of (alpha_size : uv_size)
    for (uchar k = 0; k < resize_ratio; k++)
    {
        for (uchar l = 0; l < resize_ratio; l++)
        {
            int x_resize = x * resize_ratio + k;
            int y_resize = y * resize_ratio + l;
            int a_channel_resize = y_resize * pitch + x_resize;

            if (y_resize >= height || x_resize >= width)
                continue;

            dst_A[a_channel_resize] = alpha_value;
        }
    }
}

__global__ void Process_uchar(
    cudaTextureObject_t src_tex_Y, cudaTextureObject_t src_tex_U, cudaTextureObject_t src_tex_V,
    uchar *dst_Y, uchar *dst_U, uchar *dst_V, uchar *dst_A,
    int width, int height, int pitch,
    int width_uv, int height_uv, int pitch_uv,
    float u_key, float v_key, float similarity, float blend)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (y >= height || x >= width)
        return;

    dst_Y[y * pitch + x] = tex2D<float>(src_tex_Y, x, y)*255;

    if (y >= height_uv || x >= width_uv)
        return;

    int uv_index = y * pitch_uv + x;
    dst_U[uv_index] = tex2D<float>(src_tex_U, x, y) * 255;
    dst_V[uv_index] = tex2D<float>(src_tex_V, x, y) * 255;

    change_alpha_channel(src_tex_U, src_tex_V, dst_A,
                         width_uv, height_uv,
                         width, height, pitch,
                         x, y,
                         make_float2(u_key, v_key), similarity, blend);
}

__global__ void Process_uchar2(
    cudaTextureObject_t src_tex_Y, cudaTextureObject_t src_tex_UV, cudaTextureObject_t unused1,
    uchar *dst_Y, uchar *dst_U, uchar *dst_V, uchar *dst_A,
    int width, int height, int pitch,
    int width_uv, int height_uv,int pitch_uv,
    float u_key, float v_key, float similarity, float blend)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;  // x coordinate of current pixel
    int y = blockIdx.y * blockDim.y + threadIdx.y;  // y coordinate of current pixel

    if (y >= height || x >= width)
        return;

    dst_Y[y * pitch + x] = tex2D<float>(src_tex_Y, x, y) * 255;

    if (y >= height_uv || x >= width_uv)
        return;

    int uv_index = y * pitch_uv + x;
    float2 uv_temp = tex2D<float2>(src_tex_UV, x, y);
    dst_U[uv_index] = uv_temp.x * 255;
    dst_V[uv_index] = uv_temp.y * 255;

    change_alpha_channel(src_tex_UV, (cudaTextureObject_t)nullptr,
                         dst_A, width_uv, height_uv,
                         width, height, pitch,
                         x, y,
                         make_float2(u_key, v_key), similarity, blend);
}

}
