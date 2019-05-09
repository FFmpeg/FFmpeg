/*
 * Copyright (C) 2018 Philip Langdale <philipl@overt.org>
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

template<typename T>
__inline__ __device__ T spatial_predictor(T a, T b, T c, T d, T e, T f, T g,
                                          T h, T i, T j, T k, T l, T m, T n)
{
    int spatial_pred = (d + k)/2;
    int spatial_score = abs(c - j) + abs(d - k) + abs(e - l);

    int score = abs(b - k) + abs(c - l) + abs(d - m);
    if (score < spatial_score) {
        spatial_pred = (c + l)/2;
        spatial_score = score;
        score = abs(a - l) + abs(b - m) + abs(c - n);
        if (score < spatial_score) {
          spatial_pred = (b + m)/2;
          spatial_score = score;
        }
    }
    score = abs(d - i) + abs(e - j) + abs(f - k);
    if (score < spatial_score) {
        spatial_pred = (e + j)/2;
        spatial_score = score;
        score = abs(e - h) + abs(f - i) + abs(g - j);
        if (score < spatial_score) {
          spatial_pred = (f + i)/2;
          spatial_score = score;
        }
    }
    return spatial_pred;
}

__inline__ __device__ int max3(int a, int b, int c)
{
    int x = max(a, b);
    return max(x, c);
}

__inline__ __device__ int min3(int a, int b, int c)
{
    int x = min(a, b);
    return min(x, c);
}

template<typename T>
__inline__ __device__ T temporal_predictor(T A, T B, T C, T D, T E, T F,
                                           T G, T H, T I, T J, T K, T L,
                                           T spatial_pred, bool skip_check)
{
    int p0 = (C + H) / 2;
    int p1 = F;
    int p2 = (D + I) / 2;
    int p3 = G;
    int p4 = (E + J) / 2;

    int tdiff0 = abs(D - I);
    int tdiff1 = (abs(A - F) + abs(B - G)) / 2;
    int tdiff2 = (abs(K - F) + abs(G - L)) / 2;

    int diff = max3(tdiff0, tdiff1, tdiff2);

    if (!skip_check) {
      int maxi = max3(p2 - p3, p2 - p1, min(p0 - p1, p4 - p3));
      int mini = min3(p2 - p3, p2 - p1, max(p0 - p1, p4 - p3));
      diff = max3(diff, mini, -maxi);
    }

    if (spatial_pred > p2 + diff) {
      spatial_pred = p2 + diff;
    }
    if (spatial_pred < p2 - diff) {
      spatial_pred = p2 - diff;
    }

    return spatial_pred;
}

template<typename T>
__inline__ __device__ void yadif_single(T *dst,
                                        cudaTextureObject_t prev,
                                        cudaTextureObject_t cur,
                                        cudaTextureObject_t next,
                                        int dst_width, int dst_height, int dst_pitch,
                                        int src_width, int src_height,
                                        int parity, int tff, bool skip_spatial_check)
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

    // Calculate spatial prediction
    T a = tex2D<T>(cur, xo - 3, yo - 1);
    T b = tex2D<T>(cur, xo - 2, yo - 1);
    T c = tex2D<T>(cur, xo - 1, yo - 1);
    T d = tex2D<T>(cur, xo - 0, yo - 1);
    T e = tex2D<T>(cur, xo + 1, yo - 1);
    T f = tex2D<T>(cur, xo + 2, yo - 1);
    T g = tex2D<T>(cur, xo + 3, yo - 1);

    T h = tex2D<T>(cur, xo - 3, yo + 1);
    T i = tex2D<T>(cur, xo - 2, yo + 1);
    T j = tex2D<T>(cur, xo - 1, yo + 1);
    T k = tex2D<T>(cur, xo - 0, yo + 1);
    T l = tex2D<T>(cur, xo + 1, yo + 1);
    T m = tex2D<T>(cur, xo + 2, yo + 1);
    T n = tex2D<T>(cur, xo + 3, yo + 1);

    T spatial_pred =
        spatial_predictor(a, b, c, d, e, f, g, h, i, j, k, l, m, n);

    // Calculate temporal prediction
    int is_second_field = !(parity ^ tff);

    cudaTextureObject_t prev2 = prev;
    cudaTextureObject_t prev1 = is_second_field ? cur : prev;
    cudaTextureObject_t next1 = is_second_field ? next : cur;
    cudaTextureObject_t next2 = next;

    T A = tex2D<T>(prev2, xo,  yo - 1);
    T B = tex2D<T>(prev2, xo,  yo + 1);
    T C = tex2D<T>(prev1, xo,  yo - 2);
    T D = tex2D<T>(prev1, xo,  yo + 0);
    T E = tex2D<T>(prev1, xo,  yo + 2);
    T F = tex2D<T>(cur,   xo,  yo - 1);
    T G = tex2D<T>(cur,   xo,  yo + 1);
    T H = tex2D<T>(next1, xo,  yo - 2);
    T I = tex2D<T>(next1, xo,  yo + 0);
    T J = tex2D<T>(next1, xo,  yo + 2);
    T K = tex2D<T>(next2, xo,  yo - 1);
    T L = tex2D<T>(next2, xo,  yo + 1);

    spatial_pred = temporal_predictor(A, B, C, D, E, F, G, H, I, J, K, L,
                                      spatial_pred, skip_spatial_check);

    dst[yo*dst_pitch+xo] = spatial_pred;
}

template <typename T>
__inline__ __device__ void yadif_double(T *dst,
                                        cudaTextureObject_t prev,
                                        cudaTextureObject_t cur,
                                        cudaTextureObject_t next,
                                        int dst_width, int dst_height, int dst_pitch,
                                        int src_width, int src_height,
                                        int parity, int tff, bool skip_spatial_check)
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

    T a = tex2D<T>(cur, xo - 3, yo - 1);
    T b = tex2D<T>(cur, xo - 2, yo - 1);
    T c = tex2D<T>(cur, xo - 1, yo - 1);
    T d = tex2D<T>(cur, xo - 0, yo - 1);
    T e = tex2D<T>(cur, xo + 1, yo - 1);
    T f = tex2D<T>(cur, xo + 2, yo - 1);
    T g = tex2D<T>(cur, xo + 3, yo - 1);

    T h = tex2D<T>(cur, xo - 3, yo + 1);
    T i = tex2D<T>(cur, xo - 2, yo + 1);
    T j = tex2D<T>(cur, xo - 1, yo + 1);
    T k = tex2D<T>(cur, xo - 0, yo + 1);
    T l = tex2D<T>(cur, xo + 1, yo + 1);
    T m = tex2D<T>(cur, xo + 2, yo + 1);
    T n = tex2D<T>(cur, xo + 3, yo + 1);

    T spatial_pred;
    spatial_pred.x =
        spatial_predictor(a.x, b.x, c.x, d.x, e.x, f.x, g.x, h.x, i.x, j.x, k.x, l.x, m.x, n.x);
    spatial_pred.y =
        spatial_predictor(a.y, b.y, c.y, d.y, e.y, f.y, g.y, h.y, i.y, j.y, k.y, l.y, m.y, n.y);

    // Calculate temporal prediction
    int is_second_field = !(parity ^ tff);

    cudaTextureObject_t prev2 = prev;
    cudaTextureObject_t prev1 = is_second_field ? cur : prev;
    cudaTextureObject_t next1 = is_second_field ? next : cur;
    cudaTextureObject_t next2 = next;

    T A = tex2D<T>(prev2, xo,  yo - 1);
    T B = tex2D<T>(prev2, xo,  yo + 1);
    T C = tex2D<T>(prev1, xo,  yo - 2);
    T D = tex2D<T>(prev1, xo,  yo + 0);
    T E = tex2D<T>(prev1, xo,  yo + 2);
    T F = tex2D<T>(cur,   xo,  yo - 1);
    T G = tex2D<T>(cur,   xo,  yo + 1);
    T H = tex2D<T>(next1, xo,  yo - 2);
    T I = tex2D<T>(next1, xo,  yo + 0);
    T J = tex2D<T>(next1, xo,  yo + 2);
    T K = tex2D<T>(next2, xo,  yo - 1);
    T L = tex2D<T>(next2, xo,  yo + 1);

    spatial_pred.x =
        temporal_predictor(A.x, B.x, C.x, D.x, E.x, F.x, G.x, H.x, I.x, J.x, K.x, L.x,
                           spatial_pred.x, skip_spatial_check);
    spatial_pred.y =
        temporal_predictor(A.y, B.y, C.y, D.y, E.y, F.y, G.y, H.y, I.y, J.y, K.y, L.y,
                           spatial_pred.y, skip_spatial_check);

    dst[yo*dst_pitch+xo] = spatial_pred;
}

extern "C" {

__global__ void yadif_uchar(unsigned char *dst,
                            cudaTextureObject_t prev,
                            cudaTextureObject_t cur,
                            cudaTextureObject_t next,
                            int dst_width, int dst_height, int dst_pitch,
                            int src_width, int src_height,
                            int parity, int tff, bool skip_spatial_check)
{
    yadif_single(dst, prev, cur, next,
                 dst_width, dst_height, dst_pitch,
                 src_width, src_height,
                 parity, tff, skip_spatial_check);
}

__global__ void yadif_ushort(unsigned short *dst,
                            cudaTextureObject_t prev,
                            cudaTextureObject_t cur,
                            cudaTextureObject_t next,
                            int dst_width, int dst_height, int dst_pitch,
                            int src_width, int src_height,
                            int parity, int tff, bool skip_spatial_check)
{
    yadif_single(dst, prev, cur, next,
                 dst_width, dst_height, dst_pitch,
                 src_width, src_height,
                 parity, tff, skip_spatial_check);
}

__global__ void yadif_uchar2(uchar2 *dst,
                            cudaTextureObject_t prev,
                            cudaTextureObject_t cur,
                            cudaTextureObject_t next,
                            int dst_width, int dst_height, int dst_pitch,
                            int src_width, int src_height,
                            int parity, int tff, bool skip_spatial_check)
{
    yadif_double(dst, prev, cur, next,
                 dst_width, dst_height, dst_pitch,
                 src_width, src_height,
                 parity, tff, skip_spatial_check);
}

__global__ void yadif_ushort2(ushort2 *dst,
                            cudaTextureObject_t prev,
                            cudaTextureObject_t cur,
                            cudaTextureObject_t next,
                            int dst_width, int dst_height, int dst_pitch,
                            int src_width, int src_height,
                            int parity, int tff, bool skip_spatial_check)
{
    yadif_double(dst, prev, cur, next,
                 dst_width, dst_height, dst_pitch,
                 src_width, src_height,
                 parity, tff, skip_spatial_check);
}

} /* extern "C" */
