/*
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/intreadwrite.h"

#include "pixels.h"

#include "bit_depth_template.c"

#define DEF_HPEL(OPNAME, OP)                                            \
static inline void FUNC(OPNAME ## _pixels8_l2)(uint8_t *dst,            \
                                               const uint8_t *src1,     \
                                               const uint8_t *src2,     \
                                               int dst_stride,          \
                                               int src_stride1,         \
                                               int src_stride2,         \
                                               int h)                   \
{                                                                       \
    int i;                                                              \
    for (i = 0; i < h; i++) {                                           \
        pixel4 a, b;                                                    \
        a = AV_RN4P(&src1[i * src_stride1]);                            \
        b = AV_RN4P(&src2[i * src_stride2]);                            \
        OP(*((pixel4 *) &dst[i * dst_stride]), rnd_avg_pixel4(a, b));   \
        a = AV_RN4P(&src1[i * src_stride1 + 4 * sizeof(pixel)]);        \
        b = AV_RN4P(&src2[i * src_stride2 + 4 * sizeof(pixel)]);        \
        OP(*((pixel4 *) &dst[i * dst_stride + 4 * sizeof(pixel)]),      \
           rnd_avg_pixel4(a, b));                                       \
    }                                                                   \
}                                                                       \
                                                                        \
static inline void FUNC(OPNAME ## _pixels4_l2)(uint8_t *dst,            \
                                               const uint8_t *src1,     \
                                               const uint8_t *src2,     \
                                               int dst_stride,          \
                                               int src_stride1,         \
                                               int src_stride2,         \
                                               int h)                   \
{                                                                       \
    int i;                                                              \
    for (i = 0; i < h; i++) {                                           \
        pixel4 a, b;                                                    \
        a = AV_RN4P(&src1[i * src_stride1]);                            \
        b = AV_RN4P(&src2[i * src_stride2]);                            \
        OP(*((pixel4 *) &dst[i * dst_stride]), rnd_avg_pixel4(a, b));   \
    }                                                                   \
}                                                                       \
                                                                        \
static inline void FUNC(OPNAME ## _pixels2_l2)(uint8_t *dst,            \
                                               const uint8_t *src1,     \
                                               const uint8_t *src2,     \
                                               int dst_stride,          \
                                               int src_stride1,         \
                                               int src_stride2,         \
                                               int h)                   \
{                                                                       \
    int i;                                                              \
    for (i = 0; i < h; i++) {                                           \
        pixel4 a, b;                                                    \
        a = AV_RN2P(&src1[i * src_stride1]);                            \
        b = AV_RN2P(&src2[i * src_stride2]);                            \
        OP(*((pixel2 *) &dst[i * dst_stride]), rnd_avg_pixel4(a, b));   \
    }                                                                   \
}                                                                       \
                                                                        \
static inline void FUNC(OPNAME ## _pixels16_l2)(uint8_t *dst,           \
                                                const uint8_t *src1,    \
                                                const uint8_t *src2,    \
                                                int dst_stride,         \
                                                int src_stride1,        \
                                                int src_stride2,        \
                                                int h)                  \
{                                                                       \
    FUNC(OPNAME ## _pixels8_l2)(dst, src1, src2, dst_stride,            \
                                src_stride1, src_stride2, h);           \
    FUNC(OPNAME ## _pixels8_l2)(dst  + 8 * sizeof(pixel),               \
                                src1 + 8 * sizeof(pixel),               \
                                src2 + 8 * sizeof(pixel),               \
                                dst_stride, src_stride1,                \
                                src_stride2, h);                        \
}                                                                       \

#define op_avg(a, b) a = rnd_avg_pixel4(a, b)
#define op_put(a, b) a = b
DEF_HPEL(avg, op_avg)
DEF_HPEL(put, op_put)
#undef op_avg
#undef op_put
