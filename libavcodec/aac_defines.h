/*
 * AAC defines
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

#ifndef AVCODEC_AAC_DEFINES_H
#define AVCODEC_AAC_DEFINES_H

#ifndef USE_FIXED
#define USE_FIXED 0
#endif

#if USE_FIXED

#include "libavutil/softfloat.h"

#define FFT_FLOAT    0
#define FFT_FIXED_32 1

#define AAC_RENAME(x)       x ## _fixed
#define AAC_RENAME_32(x)    x ## _fixed_32
typedef int                 INTFLOAT;
typedef int64_t             INT64FLOAT;
typedef int16_t             SHORTFLOAT;
typedef SoftFloat           AAC_FLOAT;
typedef int                 AAC_SIGNE;
#define FIXR(a)             ((int)((a) * 1 + 0.5))
#define FIXR10(a)           ((int)((a) * 1024.0 + 0.5))
#define Q23(a)              (int)((a) * 8388608.0 + 0.5)
#define Q30(x)              (int)((x)*1073741824.0 + 0.5)
#define Q31(x)              (int)((x)*2147483648.0 + 0.5)
#define RANGE15(x)          x
#define GET_GAIN(x, y)      (-(y) * (1 << (x))) + 1024
#define AAC_MUL16(x, y)     (int)(((int64_t)(x) * (y) + 0x8000) >> 16)
#define AAC_MUL26(x, y)     (int)(((int64_t)(x) * (y) + 0x2000000) >> 26)
#define AAC_MUL30(x, y)     (int)(((int64_t)(x) * (y) + 0x20000000) >> 30)
#define AAC_MUL31(x, y)     (int)(((int64_t)(x) * (y) + 0x40000000) >> 31)
#define AAC_MADD28(x, y, a, b) (int)((((int64_t)(x) * (y)) + \
                                      ((int64_t)(a) * (b)) + \
                                        0x8000000) >> 28)
#define AAC_MADD30(x, y, a, b) (int)((((int64_t)(x) * (y)) + \
                                      ((int64_t)(a) * (b)) + \
                                        0x20000000) >> 30)
#define AAC_MADD30_V8(x, y, a, b, c, d, e, f) (int)((((int64_t)(x) * (y)) + \
                                                     ((int64_t)(a) * (b)) + \
                                                     ((int64_t)(c) * (d)) + \
                                                     ((int64_t)(e) * (f)) + \
                                                       0x20000000) >> 30)
#define AAC_MSUB30(x, y, a, b) (int)((((int64_t)(x) * (y)) - \
                                      ((int64_t)(a) * (b)) + \
                                        0x20000000) >> 30)
#define AAC_MSUB30_V8(x, y, a, b, c, d, e, f) (int)((((int64_t)(x) * (y)) + \
                                                     ((int64_t)(a) * (b)) - \
                                                     ((int64_t)(c) * (d)) - \
                                                     ((int64_t)(e) * (f)) + \
                                                       0x20000000) >> 30)
#define AAC_MSUB31_V3(x, y, z)    (int)((((int64_t)(x) * (z)) - \
                                      ((int64_t)(y) * (z)) + \
                                        0x40000000) >> 31)
#define AAC_HALF_SUM(x, y)  (((x) >> 1) + ((y) >> 1))
#define AAC_SRA_R(x, y)     (int)(((x) + (1 << ((y) - 1))) >> (y))

#else

#define FFT_FLOAT    1
#define FFT_FIXED_32 0

#define AAC_RENAME(x)       x
#define AAC_RENAME_32(x)    x
typedef float               INTFLOAT;
typedef float               INT64FLOAT;
typedef float               SHORTFLOAT;
typedef float               AAC_FLOAT;
typedef unsigned            AAC_SIGNE;
#define FIXR(x)             ((float)(x))
#define FIXR10(x)           ((float)(x))
#define Q23(x)              ((float)(x))
#define Q30(x)              ((float)(x))
#define Q31(x)              ((float)(x))
#define RANGE15(x)          (32768.0 * (x))
#define GET_GAIN(x, y)      powf((x), -(y))
#define AAC_MUL16(x, y)     ((x) * (y))
#define AAC_MUL26(x, y)     ((x) * (y))
#define AAC_MUL30(x, y)     ((x) * (y))
#define AAC_MUL31(x, y)     ((x) * (y))
#define AAC_MADD28(x, y, a, b) ((x) * (y) + (a) * (b))
#define AAC_MADD30(x, y, a, b) ((x) * (y) + (a) * (b))
#define AAC_MADD30_V8(x, y, a, b, c, d, e, f) ((x) * (y) + (a) * (b) + \
                                               (c) * (d) + (e) * (f))
#define AAC_MSUB30(x, y, a, b) ((x) * (y) - (a) * (b))
#define AAC_MSUB30_V8(x, y, a, b, c, d, e, f) ((x) * (y) + (a) * (b) - \
                                               (c) * (d) - (e) * (f))
#define AAC_MSUB31_V3(x, y, z)    ((x) - (y)) * (z)
#define AAC_HALF_SUM(x, y)  ((x) + (y)) * 0.5f
#define AAC_SRA_R(x, y)     (x)

#endif /* USE_FIXED */

#endif /* AVCODEC_AAC_DEFINES_H */
