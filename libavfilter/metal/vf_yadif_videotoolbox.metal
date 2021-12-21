/*
 * Copyright (C) 2018 Philip Langdale <philipl@overt.org>
 *               2020 Aman Karmani <aman@tmm1.net>
 *               2020 Stefan Dyulgerov <stefan.dyulgerov@gmail.com>
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

#include <metal_stdlib>
#include <metal_integer>
#include <metal_texture>

using namespace metal;

/*
 * Version compat shims
 */

#if __METAL_VERSION__ < 210
#define max3(x, y, z) max(x, max(y, z))
#define min3(x, y, z) min(x, min(y, z))
#endif

/*
 * Parameters
 */

struct deintParams {
    uint channels;
    uint parity;
    uint tff;
    bool is_second_field;
    bool skip_spatial_check;
    int field_mode;
};

/*
 * Texture access helpers
 */

#define accesstype access::sample
constexpr sampler s(coord::pixel);

template <typename T>
T tex2D(texture2d<float, access::sample> tex, uint x, uint y)
{
    return tex.sample(s, float2(x, y)).x;
}

template <>
float2 tex2D<float2>(texture2d<float, access::sample> tex, uint x, uint y)
{
    return tex.sample(s, float2(x, y)).xy;
}

template <typename T>
T tex2D(texture2d<float, access::read> tex, uint x, uint y)
{
    return tex.read(uint2(x, y)).x;
}

template <>
float2 tex2D<float2>(texture2d<float, access::read> tex, uint x, uint y)
{
    return tex.read(uint2(x, y)).xy;
}

/*
 * YADIF helpers
 */

template<typename T>
T spatial_predictor(T a, T b, T c, T d, T e, T f, T g,
                    T h, T i, T j, T k, T l, T m, T n)
{
    T spatial_pred = (d + k)/2;
    T spatial_score = abs(c - j) + abs(d - k) + abs(e - l);

    T score = abs(b - k) + abs(c - l) + abs(d - m);
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

template<typename T>
T temporal_predictor(T A, T B, T C, T D, T E, T F,
                     T G, T H, T I, T J, T K, T L,
                     T spatial_pred, bool skip_check)
{
    T p0 = (C + H) / 2;
    T p1 = F;
    T p2 = (D + I) / 2;
    T p3 = G;
    T p4 = (E + J) / 2;

    T tdiff0 = abs(D - I);
    T tdiff1 = (abs(A - F) + abs(B - G)) / 2;
    T tdiff2 = (abs(K - F) + abs(G - L)) / 2;

    T diff = max3(tdiff0, tdiff1, tdiff2);

    if (!skip_check) {
        T maxi = max3(p2 - p3, p2 - p1, min(p0 - p1, p4 - p3));
        T mini = min3(p2 - p3, p2 - p1, max(p0 - p1, p4 - p3));
        diff = max3(diff, mini, -maxi);
    }

    return clamp(spatial_pred, p2 - diff, p2 + diff);
}

#define T float2
template <>
T spatial_predictor<T>(T a, T b, T c, T d, T e, T f, T g,
                       T h, T i, T j, T k, T l, T m, T n)
{
    return T(
        spatial_predictor(a.x, b.x, c.x, d.x, e.x, f.x, g.x,
                          h.x, i.x, j.x, k.x, l.x, m.x, n.x),
        spatial_predictor(a.y, b.y, c.y, d.y, e.y, f.y, g.y,
                          h.y, i.y, j.y, k.y, l.y, m.y, n.y)
    );
}

template <>
T temporal_predictor<T>(T A, T B, T C, T D, T E, T F,
                        T G, T H, T I, T J, T K, T L,
                        T spatial_pred, bool skip_check)
{
    return T(
        temporal_predictor(A.x, B.x, C.x, D.x, E.x, F.x,
                           G.x, H.x, I.x, J.x, K.x, L.x,
                           spatial_pred.x, skip_check),
        temporal_predictor(A.y, B.y, C.y, D.y, E.y, F.y,
                           G.y, H.y, I.y, J.y, K.y, L.y,
                           spatial_pred.y, skip_check)
    );
}
#undef T

/*
 * YADIF compute
 */

template <typename T>
T yadif_compute_spatial(
    texture2d<float, accesstype> cur,
    uint2 pos)
{
    // Calculate spatial prediction
    T a = tex2D<T>(cur, pos.x - 3, pos.y - 1);
    T b = tex2D<T>(cur, pos.x - 2, pos.y - 1);
    T c = tex2D<T>(cur, pos.x - 1, pos.y - 1);
    T d = tex2D<T>(cur, pos.x - 0, pos.y - 1);
    T e = tex2D<T>(cur, pos.x + 1, pos.y - 1);
    T f = tex2D<T>(cur, pos.x + 2, pos.y - 1);
    T g = tex2D<T>(cur, pos.x + 3, pos.y - 1);

    T h = tex2D<T>(cur, pos.x - 3, pos.y + 1);
    T i = tex2D<T>(cur, pos.x - 2, pos.y + 1);
    T j = tex2D<T>(cur, pos.x - 1, pos.y + 1);
    T k = tex2D<T>(cur, pos.x - 0, pos.y + 1);
    T l = tex2D<T>(cur, pos.x + 1, pos.y + 1);
    T m = tex2D<T>(cur, pos.x + 2, pos.y + 1);
    T n = tex2D<T>(cur, pos.x + 3, pos.y + 1);

    return spatial_predictor(a, b, c, d, e, f, g,
                             h, i, j, k, l, m, n);
}

template <typename T>
T yadif_compute_temporal(
    texture2d<float, accesstype> cur,
    texture2d<float, accesstype> prev2,
    texture2d<float, accesstype> prev1,
    texture2d<float, accesstype> next1,
    texture2d<float, accesstype> next2,
    T spatial_pred,
    bool skip_spatial_check,
    uint2 pos)
{
    // Calculate temporal prediction
    T A = tex2D<T>(prev2, pos.x, pos.y - 1);
    T B = tex2D<T>(prev2, pos.x, pos.y + 1);
    T C = tex2D<T>(prev1, pos.x, pos.y - 2);
    T D = tex2D<T>(prev1, pos.x, pos.y + 0);
    T E = tex2D<T>(prev1, pos.x, pos.y + 2);
    T F = tex2D<T>(cur,   pos.x, pos.y - 1);
    T G = tex2D<T>(cur,   pos.x, pos.y + 1);
    T H = tex2D<T>(next1, pos.x, pos.y - 2);
    T I = tex2D<T>(next1, pos.x, pos.y + 0);
    T J = tex2D<T>(next1, pos.x, pos.y + 2);
    T K = tex2D<T>(next2, pos.x, pos.y - 1);
    T L = tex2D<T>(next2, pos.x, pos.y + 1);

    return temporal_predictor(A, B, C, D, E, F, G, H, I, J, K, L,
                              spatial_pred, skip_spatial_check);
}

template <typename T>
T yadif(
    texture2d<float, access::write> dst,
    texture2d<float, accesstype> prev,
    texture2d<float, accesstype> cur,
    texture2d<float, accesstype> next,
    constant deintParams& params,
    uint2 pos)
{
    T spatial_pred = yadif_compute_spatial<T>(cur, pos);

    if (params.is_second_field) {
        return yadif_compute_temporal(cur, prev, cur, next, next, spatial_pred, params.skip_spatial_check, pos);
    } else {
        return yadif_compute_temporal(cur, prev, prev, cur, next, spatial_pred, params.skip_spatial_check, pos);
    }
}

/*
 * Kernel dispatch
 */

kernel void deint(
    texture2d<float, access::write> dst [[texture(0)]],
    texture2d<float, accesstype> prev [[texture(1)]],
    texture2d<float, accesstype> cur  [[texture(2)]],
    texture2d<float, accesstype> next [[texture(3)]],
    constant deintParams& params [[buffer(4)]],
    uint2 pos [[thread_position_in_grid]])
{
    if ((pos.x >= dst.get_width()) ||
        (pos.y >= dst.get_height())) {
        return;
    }

    // Don't modify the primary field
    if (pos.y % 2 == params.parity) {
        float4 in = cur.read(pos);
        dst.write(in, pos);
        return;
    }

    float2 pred;
    if (params.channels == 1)
        pred = float2(yadif<float>(dst, prev, cur, next, params, pos));
    else
        pred = yadif<float2>(dst, prev, cur, next, params, pos);
    dst.write(pred.xyyy, pos);
}
