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

#ifndef AVFILTER_CUDA_VECTORHELPERS_H
#define AVFILTER_CUDA_VECTORHELPERS_H

typedef unsigned char uchar;
typedef unsigned short ushort;

template<typename T> struct vector_helper { };
template<> struct vector_helper<uchar>   { typedef float  ftype; typedef int  itype; };
template<> struct vector_helper<uchar2>  { typedef float2 ftype; typedef int2 itype; };
template<> struct vector_helper<uchar4>  { typedef float4 ftype; typedef int4 itype; };
template<> struct vector_helper<ushort>  { typedef float  ftype; typedef int  itype; };
template<> struct vector_helper<ushort2> { typedef float2 ftype; typedef int2 itype; };
template<> struct vector_helper<ushort4> { typedef float4 ftype; typedef int4 itype; };
template<> struct vector_helper<int>     { typedef float  ftype; typedef int  itype; };
template<> struct vector_helper<int2>    { typedef float2 ftype; typedef int2 itype; };
template<> struct vector_helper<int4>    { typedef float4 ftype; typedef int4 itype; };

#define floatT typename vector_helper<T>::ftype
#define intT typename vector_helper<T>::itype

template<typename T, typename V> inline __device__ V to_floatN(const T &a) { return (V)a; }
template<typename T, typename V> inline __device__ T from_floatN(const V &a) { return (T)a; }

#define OPERATORS2(T) \
    template<typename V> inline __device__ T operator+(const T &a, const V &b) { return make_ ## T (a.x + b.x, a.y + b.y); } \
    template<typename V> inline __device__ T operator-(const T &a, const V &b) { return make_ ## T (a.x - b.x, a.y - b.y); } \
    template<typename V> inline __device__ T operator*(const T &a, V b) { return make_ ## T (a.x * b, a.y * b); } \
    template<typename V> inline __device__ T operator/(const T &a, V b) { return make_ ## T (a.x / b, a.y / b); } \
    template<typename V> inline __device__ T operator>>(const T &a, V b) { return make_ ## T (a.x >> b, a.y >> b); } \
    template<typename V> inline __device__ T operator<<(const T &a, V b) { return make_ ## T (a.x << b, a.y << b); } \
    template<typename V> inline __device__ T &operator+=(T &a, const V &b) { a.x += b.x; a.y += b.y; return a; } \
    template<typename V> inline __device__ void vec_set(T &a, const V &b) { a.x = b.x; a.y = b.y; } \
    template<typename V> inline __device__ void vec_set_scalar(T &a, V b) { a.x = b; a.y = b; } \
    template<> inline __device__ float2 to_floatN<T, float2>(const T &a) { return make_float2(a.x, a.y); } \
    template<> inline __device__ T from_floatN<T, float2>(const float2 &a) { return make_ ## T(a.x, a.y); }
#define OPERATORS4(T) \
    template<typename V> inline __device__ T operator+(const T &a, const V &b) { return make_ ## T (a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); } \
    template<typename V> inline __device__ T operator-(const T &a, const V &b) { return make_ ## T (a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w); } \
    template<typename V> inline __device__ T operator*(const T &a, V b) { return make_ ## T (a.x * b, a.y * b, a.z * b, a.w * b); } \
    template<typename V> inline __device__ T operator/(const T &a, V b) { return make_ ## T (a.x / b, a.y / b, a.z / b, a.w / b); } \
    template<typename V> inline __device__ T operator>>(const T &a, V b) { return make_ ## T (a.x >> b, a.y >> b, a.z >> b, a.w >> b); } \
    template<typename V> inline __device__ T operator<<(const T &a, V b) { return make_ ## T (a.x << b, a.y << b, a.z << b, a.w << b); } \
    template<typename V> inline __device__ T &operator+=(T &a, const V &b) { a.x += b.x; a.y += b.y; a.z += b.z; a.w += b.w; return a; } \
    template<typename V> inline __device__ void vec_set(T &a, const V &b) { a.x = b.x; a.y = b.y; a.z = b.z; a.w = b.w; } \
    template<typename V> inline __device__ void vec_set_scalar(T &a, V b) { a.x = b; a.y = b; a.z = b; a.w = b; } \
    template<> inline __device__ float4 to_floatN<T, float4>(const T &a) { return make_float4(a.x, a.y, a.z, a.w); } \
    template<> inline __device__ T from_floatN<T, float4>(const float4 &a) { return make_ ## T(a.x, a.y, a.z, a.w); }

OPERATORS2(int2)
OPERATORS2(uchar2)
OPERATORS2(ushort2)
OPERATORS2(float2)
OPERATORS4(int4)
OPERATORS4(uchar4)
OPERATORS4(ushort4)
OPERATORS4(float4)

template<typename V> inline __device__ void vec_set(int &a, V b) { a = b; }
template<typename V> inline __device__ void vec_set(float &a, V b) { a = b; }
template<typename V> inline __device__ void vec_set(uchar &a, V b) { a = b; }
template<typename V> inline __device__ void vec_set(ushort &a, V b) { a = b; }
template<typename V> inline __device__ void vec_set_scalar(int &a, V b) { a = b; }
template<typename V> inline __device__ void vec_set_scalar(float &a, V b) { a = b; }
template<typename V> inline __device__ void vec_set_scalar(uchar &a, V b) { a = b; }
template<typename V> inline __device__ void vec_set_scalar(ushort &a, V b) { a = b; }

template<typename T>
inline __device__ T lerp_scalar(T v0, T v1, float t) {
    return t*v1 + (1.0f - t)*v0;
}

template<>
inline __device__ float2 lerp_scalar<float2>(float2 v0, float2 v1, float t) {
    return make_float2(
        lerp_scalar(v0.x, v1.x, t),
        lerp_scalar(v0.y, v1.y, t)
    );
}

template<>
inline __device__ float4 lerp_scalar<float4>(float4 v0, float4 v1, float t) {
    return make_float4(
        lerp_scalar(v0.x, v1.x, t),
        lerp_scalar(v0.y, v1.y, t),
        lerp_scalar(v0.z, v1.z, t),
        lerp_scalar(v0.w, v1.w, t)
    );
}

#endif
