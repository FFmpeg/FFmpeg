/*
 * Copyright (C) 2019 Philip Langdale <philipl@overt.org>
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

__device__ static const int coef_lf[2] = { 4309, 213 };
__device__ static const int coef_hf[3] = { 5570, 3801, 1016 };
__device__ static const int coef_sp[2] = { 5077, 981 };

template<typename T>
__inline__ __device__ T max3(T a, T b, T c)
{
    T x = max(a, b);
    return max(x, c);
}

template<typename T>
__inline__ __device__ T min3(T a, T b, T c)
{
    T x = min(a, b);
    return min(x, c);
}

template<typename T>
__inline__ __device__ T clip(T a, T min, T max)
{
    if (a < min) {
        return min;
    } else if (a > max) {
        return max;
    } else {
        return a;
    }
}

template<typename T>
__inline__ __device__ T filter_intra(T cur_prefs3, T cur_prefs,
                                     T cur_mrefs, T cur_mrefs3,
                                     int clip_max)
{
    int final = (coef_sp[0] * (cur_mrefs + cur_prefs) -
                 coef_sp[1] * (cur_mrefs3 + cur_prefs3)) >> 13;
    return clip(final, 0, clip_max);
}

template<typename T>
__inline__ __device__ T filter(T cur_prefs3, T cur_prefs, T cur_mrefs, T cur_mrefs3,
                               T prev2_prefs4, T prev2_prefs2, T prev2_0, T prev2_mrefs2, T prev2_mrefs4,
                               T prev_prefs, T prev_mrefs, T next_prefs, T next_mrefs,
                               T next2_prefs4, T next2_prefs2, T next2_0, T next2_mrefs2, T next2_mrefs4,
                               int clip_max)
{
    T final;

    int c = cur_mrefs;
    int d = (prev2_0 + next2_0) >> 1;
    int e = cur_prefs;

    int temporal_diff0 = abs(prev2_0 - next2_0);
    int temporal_diff1 = (abs(prev_mrefs - c) + abs(prev_prefs - e)) >> 1;
    int temporal_diff2 = (abs(next_mrefs - c) + abs(next_prefs - e)) >> 1;
    int diff = max3(temporal_diff0 >> 1, temporal_diff1, temporal_diff2);

    if (!diff) {
        final = d;
    } else {
        int b = ((prev2_mrefs2 + next2_mrefs2) >> 1) - c;
        int f = ((prev2_prefs2 + next2_prefs2) >> 1) - e;
        int dc = d - c;
        int de = d - e;
        int mmax = max3(de, dc, min(b, f));
        int mmin = min3(de, dc, max(b, f));
        diff = max3(diff, mmin, -mmax);

        int interpol;
        if (abs(c - e) > temporal_diff0) {
            interpol = (((coef_hf[0] * (prev2_0 + next2_0)
                - coef_hf[1] * (prev2_mrefs2 + next2_mrefs2 + prev2_prefs2 + next2_prefs2)
                + coef_hf[2] * (prev2_mrefs4 + next2_mrefs4 + prev2_prefs4 + next2_mrefs4)) >> 2)
                + coef_lf[0] * (c + e) - coef_lf[1] * (cur_mrefs3 + cur_prefs3)) >> 13;
        } else {
            interpol = (coef_sp[0] * (c + e) - coef_sp[1] * (cur_mrefs3 + cur_prefs3)) >> 13;
        }

        if (interpol > d + diff) {
            interpol = d + diff;
        } else if (interpol < d - diff) {
            interpol = d - diff;
        }
        final = clip(interpol, 0, clip_max);
    }

    return final;
}

template<typename T>
__inline__ __device__ void bwdif_single(T *dst,
                                        cudaTextureObject_t prev,
                                        cudaTextureObject_t cur,
                                        cudaTextureObject_t next,
                                        int dst_width, int dst_height, int dst_pitch,
                                        int src_width, int src_height,
                                        int parity, int tff,
                                        int is_field_end, int clip_max)
{
    // Identify location
    int xo = blockIdx.x * blockDim.x + threadIdx.x;
    int yo = blockIdx.y * blockDim.y + threadIdx.y;

    if (xo >= dst_width || yo >= dst_height) {
        return;
    }

    // Don't modify the primary field
    if (yo % 2 == parity) {
      dst[yo*dst_pitch+xo] = tex2D<T>(cur, xo, yo);
      return;
    }

    T cur_prefs3 = tex2D<T>(cur, xo, yo + 3);
    T cur_prefs = tex2D<T>(cur, xo, yo + 1);
    T cur_mrefs = tex2D<T>(cur, xo, yo - 1);
    T cur_mrefs3 = tex2D<T>(cur, xo, yo - 3);

    if (is_field_end) {
        dst[yo*dst_pitch+xo] =
            filter_intra(cur_prefs3, cur_prefs, cur_mrefs, cur_mrefs3, clip_max);
        return;
    }

    // Calculate temporal prediction
    int is_second_field = !(parity ^ tff);

    cudaTextureObject_t prev2 = prev;
    cudaTextureObject_t prev1 = is_second_field ? cur : prev;
    cudaTextureObject_t next1 = is_second_field ? next : cur;
    cudaTextureObject_t next2 = next;

    T prev2_prefs4 = tex2D<T>(prev2, xo,  yo + 4);
    T prev2_prefs2 = tex2D<T>(prev2, xo,  yo + 2);
    T prev2_0 = tex2D<T>(prev2, xo,  yo + 0);
    T prev2_mrefs2 = tex2D<T>(prev2, xo,  yo - 2);
    T prev2_mrefs4 = tex2D<T>(prev2, xo,  yo - 4);
    T prev_prefs = tex2D<T>(prev1, xo,  yo + 1);
    T prev_mrefs = tex2D<T>(prev1, xo,  yo - 1);
    T next_prefs = tex2D<T>(next1, xo,  yo + 1);
    T next_mrefs = tex2D<T>(next1, xo,  yo - 1);
    T next2_prefs4 = tex2D<T>(next2, xo,  yo + 4);
    T next2_prefs2 = tex2D<T>(next2, xo,  yo + 2);
    T next2_0 = tex2D<T>(next2, xo,  yo + 0);
    T next2_mrefs2 = tex2D<T>(next2, xo,  yo - 2);
    T next2_mrefs4 = tex2D<T>(next2, xo,  yo - 4);

    dst[yo*dst_pitch+xo] = filter(cur_prefs3, cur_prefs, cur_mrefs, cur_mrefs3,
                                  prev2_prefs4, prev2_prefs2, prev2_0, prev2_mrefs2, prev2_mrefs4,
                                  prev_prefs, prev_mrefs, next_prefs, next_mrefs,
                                  next2_prefs4, next2_prefs2, next2_0, next2_mrefs2, next2_mrefs4,
                                  clip_max);
}

template <typename T>
__inline__ __device__ void bwdif_double(T *dst,
                                        cudaTextureObject_t prev,
                                        cudaTextureObject_t cur,
                                        cudaTextureObject_t next,
                                        int dst_width, int dst_height, int dst_pitch,
                                        int src_width, int src_height,
                                        int parity, int tff,
                                        int is_field_end, int clip_max)
{
    int xo = blockIdx.x * blockDim.x + threadIdx.x;
    int yo = blockIdx.y * blockDim.y + threadIdx.y;

    if (xo >= dst_width || yo >= dst_height) {
        return;
    }

    if (yo % 2 == parity) {
      // Don't modify the primary field
      dst[yo*dst_pitch+xo] = tex2D<T>(cur, xo, yo);
      return;
    }

    T cur_prefs3 = tex2D<T>(cur, xo, yo + 3);
    T cur_prefs = tex2D<T>(cur, xo, yo + 1);
    T cur_mrefs = tex2D<T>(cur, xo, yo - 1);
    T cur_mrefs3 = tex2D<T>(cur, xo, yo - 3);

    if (is_field_end) {
        T final;
        final.x = filter_intra(cur_prefs3.x, cur_prefs.x, cur_mrefs.x, cur_mrefs3.x,
                               clip_max);
        final.y = filter_intra(cur_prefs3.y, cur_prefs.y, cur_mrefs.y, cur_mrefs3.y,
                               clip_max);
        dst[yo*dst_pitch+xo] = final;
        return;
    }

    int is_second_field = !(parity ^ tff);

    cudaTextureObject_t prev2 = prev;
    cudaTextureObject_t prev1 = is_second_field ? cur : prev;
    cudaTextureObject_t next1 = is_second_field ? next : cur;
    cudaTextureObject_t next2 = next;

    T prev2_prefs4 = tex2D<T>(prev2, xo,  yo + 4);
    T prev2_prefs2 = tex2D<T>(prev2, xo,  yo + 2);
    T prev2_0 = tex2D<T>(prev2, xo,  yo + 0);
    T prev2_mrefs2 = tex2D<T>(prev2, xo,  yo - 2);
    T prev2_mrefs4 = tex2D<T>(prev2, xo,  yo - 4);
    T prev_prefs = tex2D<T>(prev1, xo,  yo + 1);
    T prev_mrefs = tex2D<T>(prev1, xo,  yo - 1);
    T next_prefs = tex2D<T>(next1, xo,  yo + 1);
    T next_mrefs = tex2D<T>(next1, xo,  yo - 1);
    T next2_prefs4 = tex2D<T>(next2, xo,  yo + 4);
    T next2_prefs2 = tex2D<T>(next2, xo,  yo + 2);
    T next2_0 = tex2D<T>(next2, xo,  yo + 0);
    T next2_mrefs2 = tex2D<T>(next2, xo,  yo - 2);
    T next2_mrefs4 = tex2D<T>(next2, xo,  yo - 4);

    T final;
    final.x = filter(cur_prefs3.x, cur_prefs.x, cur_mrefs.x, cur_mrefs3.x,
                     prev2_prefs4.x, prev2_prefs2.x, prev2_0.x, prev2_mrefs2.x, prev2_mrefs4.x,
                     prev_prefs.x, prev_mrefs.x, next_prefs.x, next_mrefs.x,
                     next2_prefs4.x, next2_prefs2.x, next2_0.x, next2_mrefs2.x, next2_mrefs4.x,
                     clip_max);
    final.y = filter(cur_prefs3.y, cur_prefs.y, cur_mrefs.y, cur_mrefs3.y,
                     prev2_prefs4.y, prev2_prefs2.y, prev2_0.y, prev2_mrefs2.y, prev2_mrefs4.y,
                     prev_prefs.y, prev_mrefs.y, next_prefs.y, next_mrefs.y,
                     next2_prefs4.y, next2_prefs2.y, next2_0.y, next2_mrefs2.y, next2_mrefs4.y,
                     clip_max);

    dst[yo*dst_pitch+xo] = final;
}

extern "C" {

__global__ void bwdif_uchar(unsigned char *dst,
                            cudaTextureObject_t prev,
                            cudaTextureObject_t cur,
                            cudaTextureObject_t next,
                            int dst_width, int dst_height, int dst_pitch,
                            int src_width, int src_height,
                            int parity, int tff, int is_field_end, int clip_max)
{
    bwdif_single(dst, prev, cur, next,
                 dst_width, dst_height, dst_pitch,
                 src_width, src_height,
                 parity, tff, is_field_end, clip_max);
}

__global__ void bwdif_ushort(unsigned short *dst,
                            cudaTextureObject_t prev,
                            cudaTextureObject_t cur,
                            cudaTextureObject_t next,
                            int dst_width, int dst_height, int dst_pitch,
                            int src_width, int src_height,
                            int parity, int tff, int is_field_end, int clip_max)
{
    bwdif_single(dst, prev, cur, next,
                 dst_width, dst_height, dst_pitch,
                 src_width, src_height,
                 parity, tff, is_field_end, clip_max);
}

__global__ void bwdif_uchar2(uchar2 *dst,
                            cudaTextureObject_t prev,
                            cudaTextureObject_t cur,
                            cudaTextureObject_t next,
                            int dst_width, int dst_height, int dst_pitch,
                            int src_width, int src_height,
                            int parity, int tff, int is_field_end, int clip_max)
{
    bwdif_double(dst, prev, cur, next,
                 dst_width, dst_height, dst_pitch,
                 src_width, src_height,
                 parity, tff, is_field_end, clip_max);
}

__global__ void bwdif_ushort2(ushort2 *dst,
                            cudaTextureObject_t prev,
                            cudaTextureObject_t cur,
                            cudaTextureObject_t next,
                            int dst_width, int dst_height, int dst_pitch,
                            int src_width, int src_height,
                            int parity, int tff, int is_field_end, int clip_max)
{
    bwdif_double(dst, prev, cur, next,
                 dst_width, dst_height, dst_pitch,
                 src_width, src_height,
                 parity, tff, is_field_end, clip_max);
}

} /* extern "C" */
