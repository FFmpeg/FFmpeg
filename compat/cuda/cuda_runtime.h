/*
 * Minimum CUDA compatibility definitions header
 *
 * Copyright (c) 2019 Rodger Combs
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

#ifndef COMPAT_CUDA_CUDA_RUNTIME_H
#define COMPAT_CUDA_CUDA_RUNTIME_H

// Common macros
#define __global__ __attribute__((global))
#define __device__ __attribute__((device))
#define __device_builtin__ __attribute__((device_builtin))
#define __align__(N) __attribute__((aligned(N)))
#define __inline__ __inline__ __attribute__((always_inline))

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define abs(x) ((x) < 0 ? -(x) : (x))

#define atomicAdd(a, b) (__atomic_fetch_add(a, b, __ATOMIC_SEQ_CST))

// Basic typedefs
typedef __device_builtin__ unsigned long long cudaTextureObject_t;

typedef struct __device_builtin__ __align__(2) uchar2
{
    unsigned char x, y;
} uchar2;

typedef struct __device_builtin__ __align__(4) ushort2
{
    unsigned short x, y;
} ushort2;

typedef struct __device_builtin__ uint3
{
    unsigned int x, y, z;
} uint3;

typedef struct uint3 dim3;

typedef struct __device_builtin__ __align__(8) int2
{
    int x, y;
} int2;

typedef struct __device_builtin__ __align__(4) uchar4
{
    unsigned char x, y, z, w;
} uchar4;

typedef struct __device_builtin__ __align__(8) ushort4
{
    unsigned char x, y, z, w;
} ushort4;

typedef struct __device_builtin__ __align__(16) int4
{
    int x, y, z, w;
} int4;

// Accessors for special registers
#define GETCOMP(reg, comp) \
    asm("mov.u32 %0, %%" #reg "." #comp ";" : "=r"(tmp)); \
    ret.comp = tmp;

#define GET(name, reg) static inline __device__ uint3 name() {\
    uint3 ret; \
    unsigned tmp; \
    GETCOMP(reg, x) \
    GETCOMP(reg, y) \
    GETCOMP(reg, z) \
    return ret; \
}

GET(getBlockIdx, ctaid)
GET(getBlockDim, ntid)
GET(getThreadIdx, tid)

// Instead of externs for these registers, we turn access to them into calls into trivial ASM
#define blockIdx (getBlockIdx())
#define blockDim (getBlockDim())
#define threadIdx (getThreadIdx())

// Basic initializers (simple macros rather than inline functions)
#define make_uchar2(a, b) ((uchar2){.x = a, .y = b})
#define make_ushort2(a, b) ((ushort2){.x = a, .y = b})
#define make_uchar4(a, b, c, d) ((uchar4){.x = a, .y = b, .z = c, .w = d})
#define make_ushort4(a, b, c, d) ((ushort4){.x = a, .y = b, .z = c, .w = d})

// Conversions from the tex instruction's 4-register output to various types
#define TEX2D(type, ret) static inline __device__ void conv(type* out, unsigned a, unsigned b, unsigned c, unsigned d) {*out = (ret);}

TEX2D(unsigned char, a & 0xFF)
TEX2D(unsigned short, a & 0xFFFF)
TEX2D(uchar2, make_uchar2(a & 0xFF, b & 0xFF))
TEX2D(ushort2, make_ushort2(a & 0xFFFF, b & 0xFFFF))
TEX2D(uchar4, make_uchar4(a & 0xFF, b & 0xFF, c & 0xFF, d & 0xFF))
TEX2D(ushort4, make_ushort4(a & 0xFFFF, b & 0xFFFF, c & 0xFFFF, d & 0xFFFF))

// Template calling tex instruction and converting the output to the selected type
template <class T>
static inline __device__ T tex2D(cudaTextureObject_t texObject, float x, float y)
{
  T ret;
  unsigned ret1, ret2, ret3, ret4;
  asm("tex.2d.v4.u32.f32 {%0, %1, %2, %3}, [%4, {%5, %6}];" :
      "=r"(ret1), "=r"(ret2), "=r"(ret3), "=r"(ret4) :
      "l"(texObject), "f"(x), "f"(y));
  conv(&ret, ret1, ret2, ret3, ret4);
  return ret;
}

#endif /* COMPAT_CUDA_CUDA_RUNTIME_H */
