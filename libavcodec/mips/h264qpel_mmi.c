/*
 * Loongson SIMD optimized h264qpel
 *
 * Copyright (c) 2015 Loongson Technology Corporation Limited
 * Copyright (c) 2015 Zhou Xiaoyong <zhouxiaoyong@loongson.cn>
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

#include "h264dsp_mips.h"
#include "libavcodec/bit_depth_template.c"

static inline void copy_block4_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride, int h)
{
    __asm__ volatile (
        "1:                                     \r\n"
        "gslwlc1 $f2, 3(%[src])                 \r\n"
        "gslwrc1 $f2, 0(%[src])                 \r\n"
        "gsswlc1 $f2, 3(%[dst])                 \r\n"
        "gsswrc1 $f2, 0(%[dst])                 \r\n"
        "dadd %[src], %[src], %[srcStride]      \r\n"
        "dadd %[dst], %[dst], %[dstStride]      \r\n"
        "daddi %[h], %[h], -1                   \r\n"
        "bnez %[h], 1b                          \r\n"
        : [dst]"+&r"(dst),[src]"+&r"(src)
        : [dstStride]"r"(dstStride),[srcStride]"r"(srcStride),[h]"r"(h)
        : "$f2"
    );
}

static inline void copy_block8_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride, int h)
{
    __asm__ volatile (
        "1:                                     \r\n"
        "gsldlc1 $f2, 7(%[src])                 \r\n"
        "gsldrc1 $f2, 0(%[src])                 \r\n"
        "gssdlc1 $f2, 7(%[dst])                 \r\n"
        "gssdrc1 $f2, 0(%[dst])                 \r\n"
        "dadd %[src], %[src], %[srcStride]      \r\n"
        "dadd %[dst], %[dst], %[dstStride]      \r\n"
        "daddi %[h], %[h], -1                   \r\n"
        "bnez %[h], 1b                          \r\n"
        : [dst]"+&r"(dst),[src]"+&r"(src)
        : [dstStride]"r"(dstStride),[srcStride]"r"(srcStride),[h]"r"(h)
        : "$f2"
    );
}

static inline void copy_block16_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride, int h)
{
    __asm__ volatile (
        "1:                                     \r\n"
        "gsldlc1 $f2, 7(%[src])                 \r\n"
        "gsldrc1 $f2, 0(%[src])                 \r\n"
        "gsldlc1 $f4, 15(%[src])                \r\n"
        "gsldrc1 $f4, 8(%[src])                 \r\n"
        "gssdlc1 $f2, 7(%[dst])                 \r\n"
        "gssdrc1 $f2, 0(%[dst])                 \r\n"
        "gssdlc1 $f4, 15(%[dst])                \r\n"
        "gssdrc1 $f4, 8(%[dst])                 \r\n"
        "dadd %[src], %[src], %[srcStride]      \r\n"
        "dadd %[dst], %[dst], %[dstStride]      \r\n"
        "daddi %[h], %[h], -1                   \r\n"
        "bnez %[h], 1b                          \r\n"
        : [dst]"+&r"(dst),[src]"+&r"(src)
        : [dstStride]"r"(dstStride),[srcStride]"r"(srcStride),[h]"r"(h)
        : "$f2","$f4"
    );
}

#define op_put(a, b) a = b
#define op_avg(a, b) a = rnd_avg_pixel4(a, b)
static inline void put_pixels4_mmi(uint8_t *block, const uint8_t *pixels,
        ptrdiff_t line_size, int h)
{
    __asm__ volatile (
        "1:                                     \r\n"
        "gslwlc1 $f2, 3(%[pixels])              \r\n"
        "gslwrc1 $f2, 0(%[pixels])              \r\n"
        "gsswlc1 $f2, 3(%[block])               \r\n"
        "gsswrc1 $f2, 0(%[block])               \r\n"
        "dadd %[pixels], %[pixels], %[line_size]\r\n"
        "dadd %[block], %[block], %[line_size]  \r\n"
        "daddi %[h], %[h], -1                   \r\n"
        "bnez %[h], 1b                          \r\n"
        : [block]"+&r"(block),[pixels]"+&r"(pixels)
        : [line_size]"r"(line_size),[h]"r"(h)
        : "$f2"
    );
}

static inline void put_pixels8_mmi(uint8_t *block, const uint8_t *pixels,
        ptrdiff_t line_size, int h)
{
    __asm__ volatile (
        "1:                                     \r\n"
        "gsldlc1 $f2, 7(%[pixels])              \r\n"
        "gsldrc1 $f2, 0(%[pixels])              \r\n"
        "gssdlc1 $f2, 7(%[block])               \r\n"
        "gssdrc1 $f2, 0(%[block])               \r\n"
        "dadd %[pixels], %[pixels], %[line_size]\r\n"
        "dadd %[block], %[block], %[line_size]  \r\n"
        "daddi %[h], %[h], -1                   \r\n"
        "bnez %[h], 1b                          \r\n"
        : [block]"+&r"(block),[pixels]"+&r"(pixels)
        : [line_size]"r"(line_size),[h]"r"(h)
        : "$f2"
    );
}

static inline void put_pixels16_mmi(uint8_t *block, const uint8_t *pixels,
        ptrdiff_t line_size, int h)
{
    __asm__ volatile (
        "1:                                     \r\n"
        "gsldlc1 $f2, 7(%[pixels])              \r\n"
        "gsldrc1 $f2, 0(%[pixels])              \r\n"
        "gsldlc1 $f4, 15(%[pixels])             \r\n"
        "gsldrc1 $f4, 8(%[pixels])              \r\n"
        "gssdlc1 $f2, 7(%[block])               \r\n"
        "gssdrc1 $f2, 0(%[block])               \r\n"
        "gssdlc1 $f4, 15(%[block])              \r\n"
        "gssdrc1 $f4, 8(%[block])               \r\n"
        "dadd %[pixels], %[pixels], %[line_size]\r\n"
        "dadd %[block], %[block], %[line_size]  \r\n"
        "daddi %[h], %[h], -1                   \r\n"
        "bnez %[h], 1b                          \r\n"
        : [block]"+&r"(block),[pixels]"+&r"(pixels)
        : [line_size]"r"(line_size),[h]"r"(h)
        : "$f2","$f4"
    );
}

static inline void avg_pixels4_mmi(uint8_t *block, const uint8_t *pixels,
        ptrdiff_t line_size, int h)
{
    __asm__ volatile (
        "1:                                     \r\n"
        "gslwlc1 $f2, 3(%[pixels])              \r\n"
        "gslwrc1 $f2, 0(%[pixels])              \r\n"
        "gslwlc1 $f4, 3(%[block])               \r\n"
        "gslwrc1 $f4, 0(%[block])               \r\n"
        "pavgb $f2, $f2, $f4                    \r\n"
        "gsswlc1 $f2, 3(%[block])               \r\n"
        "gsswrc1 $f2, 0(%[block])               \r\n"
        "dadd %[pixels], %[pixels], %[line_size]\r\n"
        "dadd %[block], %[block], %[line_size]  \r\n"
        "daddi %[h], %[h], -1                   \r\n"
        "bnez %[h], 1b                          \r\n"
        : [block]"+&r"(block),[pixels]"+&r"(pixels)
        : [line_size]"r"(line_size),[h]"r"(h)
        : "$f2","$f4"
    );
}

static inline void avg_pixels8_mmi(uint8_t *block, const uint8_t *pixels,
        ptrdiff_t line_size, int h)
{
    __asm__ volatile (
        "1:                                     \r\n"
        "gsldlc1 $f2, 7(%[block])               \r\n"
        "gsldrc1 $f2, 0(%[block])               \r\n"
        "gsldlc1 $f4, 7(%[pixels])              \r\n"
        "gsldrc1 $f4, 0(%[pixels])              \r\n"
        "pavgb $f2, $f2, $f4                    \r\n"
        "gssdlc1 $f2, 7(%[block])               \r\n"
        "gssdrc1 $f2, 0(%[block])               \r\n"
        "dadd %[pixels], %[pixels], %[line_size]\r\n"
        "dadd %[block], %[block], %[line_size]  \r\n"
        "daddi %[h], %[h], -1                   \r\n"
        "bnez %[h], 1b                          \r\n"
        : [block]"+&r"(block),[pixels]"+&r"(pixels)
        : [line_size]"r"(line_size),[h]"r"(h)
        : "$f2","$f4"
    );
}

static inline void avg_pixels16_mmi(uint8_t *block, const uint8_t *pixels,
        ptrdiff_t line_size, int h)
{
    __asm__ volatile (
        "1:                                     \r\n"
        "gsldlc1 $f2, 7(%[block])               \r\n"
        "gsldrc1 $f2, 0(%[block])               \r\n"
        "gsldlc1 $f4, 15(%[block])              \r\n"
        "gsldrc1 $f4, 8(%[block])               \r\n"
        "gsldlc1 $f6, 7(%[pixels])              \r\n"
        "gsldrc1 $f6, 0(%[pixels])              \r\n"
        "gsldlc1 $f8, 15(%[pixels])             \r\n"
        "gsldrc1 $f8, 8(%[pixels])              \r\n"
        "pavgb $f2, $f2, $f6                    \r\n"
        "pavgb $f4, $f4, $f8                    \r\n"
        "gssdlc1 $f2, 7(%[block])               \r\n"
        "gssdrc1 $f2, 0(%[block])               \r\n"
        "gssdlc1 $f4, 15(%[block])              \r\n"
        "gssdrc1 $f4, 8(%[block])               \r\n"
        "dadd %[pixels], %[pixels], %[line_size]\r\n"
        "dadd %[block], %[block], %[line_size]  \r\n"
        "daddi %[h], %[h], -1                   \r\n"
        "bnez %[h], 1b                          \r\n"
        : [block]"+&r"(block),[pixels]"+&r"(pixels)
        : [line_size]"r"(line_size),[h]"r"(h)
        : "$f2","$f4","$f6","$f8"
    );
}

static inline void put_pixels4_l2_mmi(uint8_t *dst, const uint8_t *src1,
        const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2,
        int h)
{
    int i;
    for (i = 0; i < h; i++) {
        pixel4 a, b;
        a = AV_RN4P(&src1[i * src_stride1]);
        b = AV_RN4P(&src2[i * src_stride2]);
        op_put(*((pixel4 *) &dst[i * dst_stride]), rnd_avg_pixel4(a, b));
    }
}

static inline void put_pixels8_l2_mmi(uint8_t *dst, const uint8_t *src1,
        const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2,
        int h)
{
    int i;
    for (i = 0; i < h; i++) {
        pixel4 a, b;
        a = AV_RN4P(&src1[i * src_stride1]);
        b = AV_RN4P(&src2[i * src_stride2]);
        op_put(*((pixel4 *) &dst[i * dst_stride]), rnd_avg_pixel4(a, b));
        a = AV_RN4P(&src1[i * src_stride1 + 4]);
        b = AV_RN4P(&src2[i * src_stride2 + 4]);
        op_put(*((pixel4 *) &dst[i * dst_stride + 4]), rnd_avg_pixel4(a, b));
    }
}

static inline void put_pixels16_l2_mmi(uint8_t *dst, const uint8_t *src1,
        const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2,
        int h)
{
    int i;
    for (i = 0; i < h; i++) {
        pixel4 a, b;
        a = AV_RN4P(&src1[i * src_stride1]);
        b = AV_RN4P(&src2[i * src_stride2]);
        op_put(*((pixel4 *) &dst[i * dst_stride]), rnd_avg_pixel4(a, b));
        a = AV_RN4P(&src1[i * src_stride1 + 4]);
        b = AV_RN4P(&src2[i * src_stride2 + 4]);
        op_put(*((pixel4 *) &dst[i * dst_stride + 4]), rnd_avg_pixel4(a, b));
        a = AV_RN4P(&src1[i * src_stride1 + 8]);
        b = AV_RN4P(&src2[i * src_stride2 + 8]);
        op_put(*((pixel4 *) &dst[i * dst_stride + 8]), rnd_avg_pixel4(a, b));
        a = AV_RN4P(&src1[i * src_stride1 + 12]);
        b = AV_RN4P(&src2[i * src_stride2 + 12]);
        op_put(*((pixel4 *) &dst[i * dst_stride + 12]), rnd_avg_pixel4(a, b));
    }
}

static inline void avg_pixels4_l2_mmi(uint8_t *dst, const uint8_t *src1,
        const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2,
        int h)
{
    int i;
    for (i = 0; i < h; i++) {
        pixel4 a, b;
        a = AV_RN4P(&src1[i * src_stride1]);
        b = AV_RN4P(&src2[i * src_stride2]);
        op_avg(*((pixel4 *) &dst[i * dst_stride]), rnd_avg_pixel4(a, b));
    }
}

static inline void avg_pixels8_l2_mmi(uint8_t *dst, const uint8_t *src1,
        const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2,
        int h)
{
    int i;
    for (i = 0; i < h; i++) {
        pixel4 a, b;
        a = AV_RN4P(&src1[i * src_stride1]);
        b = AV_RN4P(&src2[i * src_stride2]);
        op_avg(*((pixel4 *) &dst[i * dst_stride]), rnd_avg_pixel4(a, b));
        a = AV_RN4P(&src1[i * src_stride1 + 4]);
        b = AV_RN4P(&src2[i * src_stride2 + 4]);
        op_avg(*((pixel4 *) &dst[i * dst_stride + 4]), rnd_avg_pixel4(a, b));
    }
}

static inline void avg_pixels16_l2_mmi(uint8_t *dst, const uint8_t *src1,
        const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2,
        int h)
{
    int i;
    for (i = 0; i < h; i++) {
        pixel4 a, b;
        a = AV_RN4P(&src1[i * src_stride1]);
        b = AV_RN4P(&src2[i * src_stride2]);
        op_avg(*((pixel4 *) &dst[i * dst_stride]), rnd_avg_pixel4(a, b));
        a = AV_RN4P(&src1[i * src_stride1 + 4]);
        b = AV_RN4P(&src2[i * src_stride2 + 4]);
        op_avg(*((pixel4 *) &dst[i * dst_stride + 4]), rnd_avg_pixel4(a, b));
        a = AV_RN4P(&src1[i * src_stride1 + 8]);
        b = AV_RN4P(&src2[i * src_stride2 + 8]);
        op_avg(*((pixel4 *) &dst[i * dst_stride + 8]), rnd_avg_pixel4(a, b));
        a = AV_RN4P(&src1[i * src_stride1 + 12]);
        b = AV_RN4P(&src2[i * src_stride2 + 12]);
        op_avg(*((pixel4 *) &dst[i * dst_stride + 12]), rnd_avg_pixel4(a, b));

    }
}
#undef op_put
#undef op_avg

#define op2_avg(a, b)  a = (((a)+CLIP(((b) + 512)>>10)+1)>>1)
#define op2_put(a, b)  a = CLIP(((b) + 512)>>10)
static void put_h264_qpel4_h_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    __asm__ volatile (
        "xor $f0, $f0, $f0                      \r\n"
        "dli $8, 4                              \r\n"
        "1:                                     \r\n"
        "gslwlc1 $f2, 1(%[src])                 \r\n"
        "gslwrc1 $f2, -2(%[src])                \r\n"
        "gslwlc1 $f4, 2(%[src])                 \r\n"
        "gslwrc1 $f4, -1(%[src])                \r\n"
        "gslwlc1 $f6, 3(%[src])                 \r\n"
        "gslwrc1 $f6, 0(%[src])                 \r\n"
        "gslwlc1 $f8, 4(%[src])                 \r\n"
        "gslwrc1 $f8, 1(%[src])                 \r\n"
        "gslwlc1 $f10, 5(%[src])                \r\n"
        "gslwrc1 $f10, 2(%[src])                \r\n"
        "gslwlc1 $f12, 6(%[src])                \r\n"
        "gslwrc1 $f12, 3(%[src])                \r\n"
        "punpcklbh $f2, $f2, $f0                \r\n"
        "punpcklbh $f4, $f4, $f0                \r\n"
        "punpcklbh $f6, $f6, $f0                \r\n"
        "punpcklbh $f8, $f8, $f0                \r\n"
        "punpcklbh $f10, $f10, $f0              \r\n"
        "punpcklbh $f12, $f12, $f0              \r\n"
        "paddsh $f14, $f6, $f8                  \r\n"
        "paddsh $f16, $f4, $f10                 \r\n"
        "paddsh $f18, $f2, $f12                 \r\n"
        "pmullh $f14, $f14, %[ff_pw_20]         \r\n"
        "pmullh $f16, $f16, %[ff_pw_5]          \r\n"
        "psubsh $f14, $f14, $f16                \r\n"
        "paddsh $f18, $f14, $f18                \r\n"
        "paddsh $f18, $f18, %[ff_pw_16]         \r\n"
        "psrah $f18, $f18, %[ff_pw_5]           \r\n"
        "packushb $f18, $f18, $f0               \r\n"
        "gsswlc1 $f18, 3(%[dst])                \r\n"
        "gsswrc1 $f18, 0(%[dst])                \r\n"
        "dadd %[dst], %[dst], %[dstStride]      \r\n"
        "dadd %[src], %[src], %[srcStride]      \r\n"
        "daddi $8, $8, -1                       \r\n"
        "bnez $8, 1b                            \r\n"
        : [dst]"+&r"(dst),[src]"+&r"(src)
        : [dstStride]"r"(dstStride),[srcStride]"r"(srcStride),
          [ff_pw_20]"f"(ff_pw_20),[ff_pw_5]"f"(ff_pw_5),[ff_pw_16]"f"(ff_pw_16)
        : "$8","$f0","$f2","$f4","$f6","$f8","$f10","$f12","$f14","$f16",
          "$f18"
    );
}

static void put_h264_qpel8_h_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    __asm__ volatile (
        "xor $f0, $f0, $f0                      \r\n"
        "dli $8, 8                              \r\n"
        "1:                                     \r\n"
        "gsldlc1 $f2, 5(%[src])                 \r\n"
        "gsldrc1 $f2, -2(%[src])                \r\n"
        "gsldlc1 $f4, 6(%[src])                 \r\n"
        "gsldrc1 $f4, -1(%[src])                \r\n"
        "gsldlc1 $f6, 7(%[src])                 \r\n"
        "gsldrc1 $f6, 0(%[src])                 \r\n"
        "gsldlc1 $f8, 8(%[src])                 \r\n"
        "gsldrc1 $f8, 1(%[src])                 \r\n"
        "gsldlc1 $f10, 9(%[src])                \r\n"
        "gsldrc1 $f10, 2(%[src])                \r\n"
        "gsldlc1 $f12, 10(%[src])               \r\n"
        "gsldrc1 $f12, 3(%[src])                \r\n"
        "punpcklbh $f14, $f6, $f0               \r\n"
        "punpckhbh $f16, $f6, $f0               \r\n"
        "punpcklbh $f18, $f8, $f0               \r\n"
        "punpckhbh $f20, $f8, $f0               \r\n"
        "paddsh $f6, $f14, $f18                 \r\n"
        "paddsh $f8, $f16, $f20                 \r\n"
        "pmullh $f6, $f6, %[ff_pw_20]           \r\n"
        "pmullh $f8, $f8, %[ff_pw_20]           \r\n"
        "punpcklbh $f14, $f4, $f0               \r\n"
        "punpckhbh $f16, $f4, $f0               \r\n"
        "punpcklbh $f18, $f10, $f0              \r\n"
        "punpckhbh $f20, $f10, $f0              \r\n"
        "paddsh $f4, $f14, $f18                 \r\n"
        "paddsh $f10, $f16, $f20                \r\n"
        "pmullh $f4, $f4, %[ff_pw_5]            \r\n"
        "pmullh $f10, $f10, %[ff_pw_5]          \r\n"
        "punpcklbh $f14, $f2, $f0               \r\n"
        "punpckhbh $f16, $f2, $f0               \r\n"
        "punpcklbh $f18, $f12, $f0              \r\n"
        "punpckhbh $f20, $f12, $f0              \r\n"
        "paddsh $f2, $f14, $f18                 \r\n"
        "paddsh $f12, $f16, $f20                \r\n"
        "psubsh $f6, $f6, $f4                   \r\n"
        "psubsh $f8, $f8, $f10                  \r\n"
        "paddsh $f6, $f6, $f2                   \r\n"
        "paddsh $f8, $f8, $f12                  \r\n"
        "paddsh $f6, $f6, %[ff_pw_16]           \r\n"
        "paddsh $f8, $f8, %[ff_pw_16]           \r\n"
        "psrah $f6, $f6, %[ff_pw_5]             \r\n"
        "psrah $f8, $f8, %[ff_pw_5]             \r\n"
        "packushb $f18, $f6, $f8                \r\n"
        "sdc1 $f18, 0(%[dst])                   \r\n"
        "dadd %[dst], %[dst], %[dstStride]      \r\n"
        "dadd %[src], %[src], %[srcStride]      \r\n"
        "daddi $8, $8, -1                       \r\n"
        "bnez $8, 1b                            \r\n"
        : [dst]"+&r"(dst),[src]"+&r"(src)
        : [dstStride]"r"(dstStride),[srcStride]"r"(srcStride),
          [ff_pw_20]"f"(ff_pw_20),[ff_pw_5]"f"(ff_pw_5),[ff_pw_16]"f"(ff_pw_16)
        : "$8","$f0","$f2","$f4","$f6","$f8","$f10","$f12","$f14","$f16",
          "$f18","$f20"
    );
}

static void put_h264_qpel16_h_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    put_h264_qpel8_h_lowpass_mmi(dst, src, dstStride, srcStride);
    put_h264_qpel8_h_lowpass_mmi(dst+8, src+8, dstStride, srcStride);
    src += 8*srcStride;
    dst += 8*dstStride;
    put_h264_qpel8_h_lowpass_mmi(dst, src, dstStride, srcStride);
    put_h264_qpel8_h_lowpass_mmi(dst+8, src+8, dstStride, srcStride);
}

static void avg_h264_qpel4_h_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    __asm__ volatile (
        "xor $f0, $f0, $f0                      \r\n"
        "dli $8, 4                              \r\n"
        "1:                                     \r\n"
        "gslwlc1 $f2, 1(%[src])                 \r\n"
        "gslwrc1 $f2, -2(%[src])                \r\n"
        "gslwlc1 $f4, 2(%[src])                 \r\n"
        "gslwrc1 $f4, -1(%[src])                \r\n"
        "gslwlc1 $f6, 3(%[src])                 \r\n"
        "gslwrc1 $f6, 0(%[src])                 \r\n"
        "gslwlc1 $f8, 4(%[src])                 \r\n"
        "gslwrc1 $f8, 1(%[src])                 \r\n"
        "gslwlc1 $f10, 5(%[src])                \r\n"
        "gslwrc1 $f10, 2(%[src])                \r\n"
        "gslwlc1 $f12, 6(%[src])                \r\n"
        "gslwrc1 $f12, 3(%[src])                \r\n"
        "punpcklbh $f2, $f2, $f0                \r\n"
        "punpcklbh $f4, $f4, $f0                \r\n"
        "punpcklbh $f6, $f6, $f0                \r\n"
        "punpcklbh $f8, $f8, $f0                \r\n"
        "punpcklbh $f10, $f10, $f0              \r\n"
        "punpcklbh $f12, $f12, $f0              \r\n"
        "paddsh $f14, $f6, $f8                  \r\n"
        "paddsh $f16, $f4, $f10                 \r\n"
        "paddsh $f18, $f2, $f12                 \r\n"
        "pmullh $f14, $f14, %[ff_pw_20]         \r\n"
        "pmullh $f16, $f16, %[ff_pw_5]          \r\n"
        "psubsh $f14, $f14, $f16                \r\n"
        "paddsh $f18, $f14, $f18                \r\n"
        "paddsh $f18, $f18, %[ff_pw_16]         \r\n"
        "psrah $f18, $f18, %[ff_pw_5]           \r\n"
        "packushb $f18, $f18, $f0               \r\n"
        "lwc1 $f20, 0(%[dst])                   \r\n"
        "pavgb $f18, $f18, $f20                 \r\n"
        "gsswlc1 $f18, 3(%[dst])                \r\n"
        "gsswrc1 $f18, 0(%[dst])                \r\n"
        "dadd %[dst], %[dst], %[dstStride]      \r\n"
        "dadd %[src], %[src], %[srcStride]      \r\n"
        "daddi $8, $8, -1                       \r\n"
        "bnez $8, 1b                            \r\n"
        : [dst]"+&r"(dst),[src]"+&r"(src)
        : [dstStride]"r"(dstStride),[srcStride]"r"(srcStride),
          [ff_pw_20]"f"(ff_pw_20),[ff_pw_5]"f"(ff_pw_5),[ff_pw_16]"f"(ff_pw_16)
        : "$8","$f0","$f2","$f4","$f6","$f8","$f10","$f12","$f14","$f16",
          "$f18","$f20"
    );
}

static void avg_h264_qpel8_h_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    __asm__ volatile (
        "xor $f0, $f0, $f0                      \r\n"
        "dli $8, 8                              \r\n"
        "1:                                     \r\n"
        "gsldlc1 $f2, 5(%[src])                 \r\n"
        "gsldrc1 $f2, -2(%[src])                \r\n"
        "gsldlc1 $f4, 6(%[src])                 \r\n"
        "gsldrc1 $f4, -1(%[src])                \r\n"
        "gsldlc1 $f6, 7(%[src])                 \r\n"
        "gsldrc1 $f6, 0(%[src])                 \r\n"
        "gsldlc1 $f8, 8(%[src])                 \r\n"
        "gsldrc1 $f8, 1(%[src])                 \r\n"
        "gsldlc1 $f10, 9(%[src])                \r\n"
        "gsldrc1 $f10, 2(%[src])                \r\n"
        "gsldlc1 $f12, 10(%[src])               \r\n"
        "gsldrc1 $f12, 3(%[src])                \r\n"
        "punpcklbh $f14, $f6, $f0               \r\n"
        "punpckhbh $f16, $f6, $f0               \r\n"
        "punpcklbh $f18, $f8, $f0               \r\n"
        "punpckhbh $f20, $f8, $f0               \r\n"
        "paddsh $f6, $f14, $f18                 \r\n"
        "paddsh $f8, $f16, $f20                 \r\n"
        "pmullh $f6, $f6, %[ff_pw_20]           \r\n"
        "pmullh $f8, $f8, %[ff_pw_20]           \r\n"
        "punpcklbh $f14, $f4, $f0               \r\n"
        "punpckhbh $f16, $f4, $f0               \r\n"
        "punpcklbh $f18, $f10, $f0              \r\n"
        "punpckhbh $f20, $f10, $f0              \r\n"
        "paddsh $f4, $f14, $f18                 \r\n"
        "paddsh $f10, $f16, $f20                \r\n"
        "pmullh $f4, $f4, %[ff_pw_5]            \r\n"
        "pmullh $f10, $f10, %[ff_pw_5]          \r\n"
        "punpcklbh $f14, $f2, $f0               \r\n"
        "punpckhbh $f16, $f2, $f0               \r\n"
        "punpcklbh $f18, $f12, $f0              \r\n"
        "punpckhbh $f20, $f12, $f0              \r\n"
        "paddsh $f2, $f14, $f18                 \r\n"
        "paddsh $f12, $f16, $f20                \r\n"
        "psubsh $f6, $f6, $f4                   \r\n"
        "psubsh $f8, $f8, $f10                  \r\n"
        "paddsh $f6, $f6, $f2                   \r\n"
        "paddsh $f8, $f8, $f12                  \r\n"
        "paddsh $f6, $f6, %[ff_pw_16]           \r\n"
        "paddsh $f8, $f8, %[ff_pw_16]           \r\n"
        "psrah $f6, $f6, %[ff_pw_5]             \r\n"
        "psrah $f8, $f8, %[ff_pw_5]             \r\n"
        "packushb $f18, $f6, $f8                \r\n"
        "ldc1 $f20, 0(%[dst])                   \r\n"
        "pavgb $f18, $f18, $f20                 \r\n"
        "sdc1 $f18, 0(%[dst])                   \r\n"
        "dadd %[dst], %[dst], %[dstStride]      \r\n"
        "dadd %[src], %[src], %[srcStride]      \r\n"
        "daddi $8, $8, -1                       \r\n"
        "bnez $8, 1b                            \r\n"
        : [dst]"+&r"(dst),[src]"+&r"(src)
        : [dstStride]"r"(dstStride),[srcStride]"r"(srcStride),
          [ff_pw_20]"f"(ff_pw_20),[ff_pw_5]"f"(ff_pw_5),[ff_pw_16]"f"(ff_pw_16)
        : "$8","$f0","$f2","$f4","$f6","$f8","$f10","$f12","$f14","$f16",
          "$f18","$f20"
    );
}

static void avg_h264_qpel16_h_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    avg_h264_qpel8_h_lowpass_mmi(dst, src, dstStride, srcStride);
    avg_h264_qpel8_h_lowpass_mmi(dst+8, src+8, dstStride, srcStride);
    src += 8*srcStride;
    dst += 8*dstStride;
    avg_h264_qpel8_h_lowpass_mmi(dst, src, dstStride, srcStride);
    avg_h264_qpel8_h_lowpass_mmi(dst+8, src+8, dstStride, srcStride);
}

static void put_h264_qpel4_v_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    __asm__ volatile (
        "xor $f0, $f0, $f0                      \r\n"
        "gslwlc1 $f2, 3(%[srcB])                \r\n"
        "gslwrc1 $f2, 0(%[srcB])                \r\n"
        "gslwlc1 $f4, 3(%[srcA])                \r\n"
        "gslwrc1 $f4, 0(%[srcA])                \r\n"
        "gslwlc1 $f6, 3(%[src0])                \r\n"
        "gslwrc1 $f6, 0(%[src0])                \r\n"
        "gslwlc1 $f8, 3(%[src1])                \r\n"
        "gslwrc1 $f8, 0(%[src1])                \r\n"
        "gslwlc1 $f10, 3(%[src2])               \r\n"
        "gslwrc1 $f10, 0(%[src2])               \r\n"
        "gslwlc1 $f12, 3(%[src3])               \r\n"
        "gslwrc1 $f12, 0(%[src3])               \r\n"
        "gslwlc1 $f14, 3(%[src4])               \r\n"
        "gslwrc1 $f14, 0(%[src4])               \r\n"
        "gslwlc1 $f16, 3(%[src5])               \r\n"
        "gslwrc1 $f16, 0(%[src5])               \r\n"
        "gslwlc1 $f18, 3(%[src6])               \r\n"
        "gslwrc1 $f18, 0(%[src6])               \r\n"
        "punpcklbh $f2, $f2, $f0                \r\n"
        "punpcklbh $f4, $f4, $f0                \r\n"
        "punpcklbh $f6, $f6, $f0                \r\n"
        "punpcklbh $f8, $f8, $f0                \r\n"
        "punpcklbh $f10, $f10, $f0              \r\n"
        "punpcklbh $f12, $f12, $f0              \r\n"
        "punpcklbh $f14, $f14, $f0              \r\n"
        "punpcklbh $f16, $f16, $f0              \r\n"
        "punpcklbh $f18, $f18, $f0              \r\n"
        "paddsh $f20, $f6, $f8                  \r\n"
        "pmullh $f20, $f20, %[ff_pw_20]         \r\n"
        "paddsh $f22, $f4, $f10                 \r\n"
        "pmullh $f22, $f22, %[ff_pw_5]          \r\n"
        "psubsh $f24, $f20, $f22                \r\n"
        "paddsh $f24, $f24, $f2                 \r\n"
        "paddsh $f24, $f24, $f12                \r\n"
        "paddsh $f20, $f8, $f10                 \r\n"
        "pmullh $f20, $f20, %[ff_pw_20]         \r\n"
        "paddsh $f22, $f6, $f12                 \r\n"
        "pmullh $f22, $f22, %[ff_pw_5]          \r\n"
        "psubsh $f26, $f20, $f22                \r\n"
        "paddsh $f26, $f26, $f4                 \r\n"
        "paddsh $f26, $f26, $f14                \r\n"
        "paddsh $f20, $f10, $f12                \r\n"
        "pmullh $f20, $f20, %[ff_pw_20]         \r\n"
        "paddsh $f22, $f8, $f14                 \r\n"
        "pmullh $f22, $f22, %[ff_pw_5]          \r\n"
        "psubsh $f28, $f20, $f22                \r\n"
        "paddsh $f28, $f28, $f6                 \r\n"
        "paddsh $f28, $f28, $f16                \r\n"
        "paddsh $f20, $f12, $f14                \r\n"
        "pmullh $f20, $f20, %[ff_pw_20]         \r\n"
        "paddsh $f22, $f10, $f16                \r\n"
        "pmullh $f22, $f22, %[ff_pw_5]          \r\n"
        "psubsh $f30, $f20, $f22                \r\n"
        "paddsh $f30, $f30, $f8                 \r\n"
        "paddsh $f30, $f30, $f18                \r\n"
        "paddsh $f24, $f24, %[ff_pw_16]         \r\n"
        "paddsh $f26, $f26, %[ff_pw_16]         \r\n"
        "paddsh $f28, $f28, %[ff_pw_16]         \r\n"
        "paddsh $f30, $f30, %[ff_pw_16]         \r\n"
        "psrah $f24, $f24, %[ff_pw_5]           \r\n"
        "psrah $f26, $f26, %[ff_pw_5]           \r\n"
        "psrah $f28, $f28, %[ff_pw_5]           \r\n"
        "psrah $f30, $f30, %[ff_pw_5]           \r\n"
        "packushb $f24, $f24, $f0               \r\n"
        "packushb $f26, $f26, $f0               \r\n"
        "packushb $f28, $f28, $f0               \r\n"
        "packushb $f30, $f30, $f0               \r\n"
        "swc1 $f24, 0(%[dst0])                  \r\n"
        "swc1 $f26, 0(%[dst1])                  \r\n"
        "swc1 $f28, 0(%[dst2])                  \r\n"
        "swc1 $f30, 0(%[dst3])                  \r\n"
        ::[dst0]"r"(dst),               [dst1]"r"(dst+dstStride),
          [dst2]"r"(dst+2*dstStride),   [dst3]"r"(dst+3*dstStride),
          [srcB]"r"(src-2*srcStride),   [srcA]"r"(src-srcStride),
          [src0]"r"(src),               [src1]"r"(src+srcStride),
          [src2]"r"(src+2*srcStride),   [src3]"r"(src+3*srcStride),
          [src4]"r"(src+4*srcStride),   [src5]"r"(src+5*srcStride),
          [src6]"r"(src+6*srcStride),   [ff_pw_20]"f"(ff_pw_20),
          [ff_pw_5]"f"(ff_pw_5),        [ff_pw_16]"f"(ff_pw_16)
        : "$f0","$f2","$f4","$f6","$f8","$f10","$f12","$f14","$f16","$f18",
          "$f20","$f22","$f24","$f26","$f28","$f30"
    );
}

static void put_h264_qpel8_v_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    __asm__ volatile (
        "xor $f0, $f0, $f0                      \r\n"
        "gsldlc1 $f2, 7(%[srcB])                \r\n"
        "gsldrc1 $f2, 0(%[srcB])                \r\n"
        "gsldlc1 $f4, 7(%[srcA])                \r\n"
        "gsldrc1 $f4, 0(%[srcA])                \r\n"
        "gsldlc1 $f6, 7(%[src0])                \r\n"
        "gsldrc1 $f6, 0(%[src0])                \r\n"
        "gsldlc1 $f8, 7(%[src1])                \r\n"
        "gsldrc1 $f8, 0(%[src1])                \r\n"
        "gsldlc1 $f10, 7(%[src2])               \r\n"
        "gsldrc1 $f10, 0(%[src2])               \r\n"
        "gsldlc1 $f12, 7(%[src3])               \r\n"
        "gsldrc1 $f12, 0(%[src3])               \r\n"
        "gsldlc1 $f14, 7(%[src4])               \r\n"
        "gsldrc1 $f14, 0(%[src4])               \r\n"
        "gsldlc1 $f16, 7(%[src5])               \r\n"
        "gsldrc1 $f16, 0(%[src5])               \r\n"
        "gsldlc1 $f18, 7(%[src6])               \r\n"
        "gsldrc1 $f18, 0(%[src6])               \r\n"
        "gsldlc1 $f20, 7(%[src7])               \r\n"
        "gsldrc1 $f20, 0(%[src7])               \r\n"
        "gsldlc1 $f22, 7(%[src8])               \r\n"
        "gsldrc1 $f22, 0(%[src8])               \r\n"
        "gsldlc1 $f24, 7(%[src9])               \r\n"
        "gsldrc1 $f24, 0(%[src9])               \r\n"
        "gsldlc1 $f26, 7(%[src10])              \r\n"
        "gsldrc1 $f26, 0(%[src10])              \r\n"
        "punpcklbh $f1, $f2, $f0                \r\n"
        "punpckhbh $f2, $f2, $f0                \r\n"
        "punpcklbh $f3, $f4, $f0                \r\n"
        "punpckhbh $f4, $f4, $f0                \r\n"
        "punpcklbh $f5, $f6, $f0                \r\n"
        "punpckhbh $f6, $f6, $f0                \r\n"
        "punpcklbh $f7, $f8, $f0                \r\n"
        "punpckhbh $f8, $f8, $f0                \r\n"
        "punpcklbh $f9, $f10, $f0               \r\n"
        "punpckhbh $f10, $f10, $f0              \r\n"
        "punpcklbh $f11, $f12, $f0              \r\n"
        "punpckhbh $f12, $f12, $f0              \r\n"
        "punpcklbh $f13, $f14, $f0              \r\n"
        "punpckhbh $f14, $f14, $f0              \r\n"
        "punpcklbh $f15, $f16, $f0              \r\n"
        "punpckhbh $f16, $f16, $f0              \r\n"
        "punpcklbh $f17, $f18, $f0              \r\n"
        "punpckhbh $f18, $f18, $f0              \r\n"
        "punpcklbh $f19, $f20, $f0              \r\n"
        "punpckhbh $f20, $f20, $f0              \r\n"
        "punpcklbh $f21, $f22, $f0              \r\n"
        "punpckhbh $f22, $f22, $f0              \r\n"
        "punpcklbh $f23, $f24, $f0              \r\n"
        "punpckhbh $f24, $f24, $f0              \r\n"
        "punpcklbh $f25, $f26, $f0              \r\n"
        "punpckhbh $f26, $f26, $f0              \r\n"
        "paddsh $f27, $f5, $f7                  \r\n"
        "pmullh $f27, $f27, %[ff_pw_4]          \r\n"
        "paddsh $f28, $f6, $f8                  \r\n"//src0+src1
        "pmullh $f28, $f28, %[ff_pw_4]          \r\n"
        "psubsh $f27, $f27, $f3                 \r\n"
        "psubsh $f28, $f28, $f4                 \r\n"
        "psubsh $f27, $f27, $f9                 \r\n"
        "psubsh $f28, $f28, $f10                \r\n"
        "pmullh $f27, $f27, %[ff_pw_5]          \r\n"
        "pmullh $f28, $f28, %[ff_pw_5]          \r\n"
        "paddsh $f27, $f27, $f1                 \r\n"
        "paddsh $f28, $f28, $f2                 \r\n"
        "paddsh $f27, $f27, $f11                \r\n"
        "paddsh $f28, $f28, $f12                \r\n"
        "paddsh $f27, $f27, %[ff_pw_16]         \r\n"
        "paddsh $f28, $f28, %[ff_pw_16]         \r\n"
        "psrah $f27, $f27, %[ff_pw_5]           \r\n"
        "psrah $f28, $f28, %[ff_pw_5]           \r\n"
        "packushb $f27, $f27, $f0               \r\n"
        "packushb $f28, $f28, $f0               \r\n"
        "punpcklwd $f2, $f27, $f28              \r\n"
        "sdc1 $f2, 0(%[dst0])                   \r\n"
        "paddsh $f27, $f7, $f9                  \r\n"
        "pmullh $f27, $f27, %[ff_pw_4]          \r\n"
        "paddsh $f28, $f8, $f10                 \r\n"//src1+src2
        "pmullh $f28, $f28, %[ff_pw_4]          \r\n"
        "psubsh $f27, $f27, $f5                 \r\n"
        "psubsh $f28, $f28, $f6                 \r\n"
        "psubsh $f27, $f27, $f11                \r\n"
        "psubsh $f28, $f28, $f12                \r\n"
        "pmullh $f27, $f27, %[ff_pw_5]          \r\n"
        "pmullh $f28, $f28, %[ff_pw_5]          \r\n"
        "paddsh $f27, $f27, $f3                 \r\n"
        "paddsh $f28, $f28, $f4                 \r\n"
        "paddsh $f27, $f27, $f13                \r\n"
        "paddsh $f28, $f28, $f14                \r\n"
        "paddsh $f27, $f27, %[ff_pw_16]         \r\n"
        "paddsh $f28, $f28, %[ff_pw_16]         \r\n"
        "psrah $f27, $f27, %[ff_pw_5]           \r\n"
        "psrah $f28, $f28, %[ff_pw_5]           \r\n"
        "packushb $f27, $f27, $f0               \r\n"
        "packushb $f28, $f28, $f0               \r\n"
        "punpcklwd $f4, $f27, $f28              \r\n"
        "sdc1 $f4, 0(%[dst1])                   \r\n"
        "paddsh $f27, $f9, $f11                 \r\n"
        "pmullh $f27, $f27, %[ff_pw_4]          \r\n"
        "paddsh $f28, $f10, $f12                \r\n"//src2+src3
        "pmullh $f28, $f28, %[ff_pw_4]          \r\n"
        "psubsh $f27, $f27, $f7                 \r\n"
        "psubsh $f28, $f28, $f8                 \r\n"
        "psubsh $f27, $f27, $f13                \r\n"
        "psubsh $f28, $f28, $f14                \r\n"
        "pmullh $f27, $f27, %[ff_pw_5]          \r\n"
        "pmullh $f28, $f28, %[ff_pw_5]          \r\n"
        "paddsh $f27, $f27, $f5                 \r\n"
        "paddsh $f28, $f28, $f6                 \r\n"
        "paddsh $f27, $f27, $f15                \r\n"
        "paddsh $f28, $f28, $f16                \r\n"
        "paddsh $f27, $f27, %[ff_pw_16]         \r\n"
        "paddsh $f28, $f28, %[ff_pw_16]         \r\n"
        "psrah $f27, $f27, %[ff_pw_5]           \r\n"
        "psrah $f28, $f28, %[ff_pw_5]           \r\n"
        "packushb $f27, $f27, $f0               \r\n"
        "packushb $f28, $f28, $f0               \r\n"
        "punpcklwd $f6, $f27, $f28              \r\n"
        "sdc1 $f6, 0(%[dst2])                   \r\n"
        "paddsh $f27, $f11, $f13                \r\n"
        "pmullh $f27, $f27, %[ff_pw_4]          \r\n"
        "paddsh $f28, $f12, $f14                \r\n"//src3+src4
        "pmullh $f28, $f28, %[ff_pw_4]          \r\n"
        "psubsh $f27, $f27, $f9                 \r\n"
        "psubsh $f28, $f28, $f10                \r\n"
        "psubsh $f27, $f27, $f15                \r\n"
        "psubsh $f28, $f28, $f16                \r\n"
        "pmullh $f27, $f27, %[ff_pw_5]          \r\n"
        "pmullh $f28, $f28, %[ff_pw_5]          \r\n"
        "paddsh $f27, $f27, $f7                 \r\n"
        "paddsh $f28, $f28, $f8                 \r\n"
        "paddsh $f27, $f27, $f17                \r\n"
        "paddsh $f28, $f28, $f18                \r\n"
        "paddsh $f27, $f27, %[ff_pw_16]         \r\n"
        "paddsh $f28, $f28, %[ff_pw_16]         \r\n"
        "psrah $f27, $f27, %[ff_pw_5]           \r\n"
        "psrah $f28, $f28, %[ff_pw_5]           \r\n"
        "packushb $f27, $f27, $f0               \r\n"
        "packushb $f28, $f28, $f0               \r\n"
        "punpcklwd $f8, $f27, $f28              \r\n"
        "sdc1 $f8, 0(%[dst3])                   \r\n"
        "paddsh $f27, $f13, $f15                \r\n"
        "pmullh $f27, $f27, %[ff_pw_4]          \r\n"
        "paddsh $f28, $f14, $f16                \r\n"//src4+src5
        "pmullh $f28, $f28, %[ff_pw_4]          \r\n"
        "psubsh $f27, $f27, $f11                \r\n"
        "psubsh $f28, $f28, $f12                \r\n"
        "psubsh $f27, $f27, $f17                \r\n"
        "psubsh $f28, $f28, $f18                \r\n"
        "pmullh $f27, $f27, %[ff_pw_5]          \r\n"
        "pmullh $f28, $f28, %[ff_pw_5]          \r\n"
        "paddsh $f27, $f27, $f9                 \r\n"
        "paddsh $f28, $f28, $f10                \r\n"
        "paddsh $f27, $f27, $f19                \r\n"
        "paddsh $f28, $f28, $f20                \r\n"
        "paddsh $f27, $f27, %[ff_pw_16]         \r\n"
        "paddsh $f28, $f28, %[ff_pw_16]         \r\n"
        "psrah $f27, $f27, %[ff_pw_5]           \r\n"
        "psrah $f28, $f28, %[ff_pw_5]           \r\n"
        "packushb $f27, $f27, $f0               \r\n"
        "packushb $f28, $f28, $f0               \r\n"
        "punpcklwd $f10, $f27, $f28             \r\n"
        "sdc1 $f10, 0(%[dst4])                  \r\n"

        "paddsh $f27, $f15, $f17                \r\n"
        "pmullh $f27, $f27, %[ff_pw_4]          \r\n"
        "paddsh $f28, $f16, $f18                \r\n"//src5+src6
        "pmullh $f28, $f28, %[ff_pw_4]          \r\n"
        "psubsh $f27, $f27, $f13                \r\n"
        "psubsh $f28, $f28, $f14                \r\n"
        "psubsh $f27, $f27, $f19                \r\n"
        "psubsh $f28, $f28, $f20                \r\n"
        "pmullh $f27, $f27, %[ff_pw_5]          \r\n"
        "pmullh $f28, $f28, %[ff_pw_5]          \r\n"
        "paddsh $f27, $f27, $f11                \r\n"
        "paddsh $f28, $f28, $f12                \r\n"
        "paddsh $f27, $f27, $f21                \r\n"
        "paddsh $f28, $f28, $f22                \r\n"
        "paddsh $f27, $f27, %[ff_pw_16]         \r\n"
        "paddsh $f28, $f28, %[ff_pw_16]         \r\n"
        "psrah $f27, $f27, %[ff_pw_5]           \r\n"
        "psrah $f28, $f28, %[ff_pw_5]           \r\n"
        "packushb $f27, $f27, $f0               \r\n"
        "packushb $f28, $f28, $f0               \r\n"
        "punpcklwd $f12, $f27, $f28             \r\n"
        "sdc1 $f12, 0(%[dst5])                  \r\n"
        "paddsh $f27, $f17, $f19                \r\n"
        "pmullh $f27, $f27, %[ff_pw_4]          \r\n"
        "paddsh $f28, $f18, $f20                \r\n"//src6+src7
        "pmullh $f28, $f28, %[ff_pw_4]          \r\n"
        "psubsh $f27, $f27, $f15                \r\n"
        "psubsh $f28, $f28, $f16                \r\n"
        "psubsh $f27, $f27, $f21                \r\n"
        "psubsh $f28, $f28, $f22                \r\n"
        "pmullh $f27, $f27, %[ff_pw_5]          \r\n"
        "pmullh $f28, $f28, %[ff_pw_5]          \r\n"
        "paddsh $f27, $f27, $f13                \r\n"
        "paddsh $f28, $f28, $f14                \r\n"
        "paddsh $f27, $f27, $f23                \r\n"
        "paddsh $f28, $f28, $f24                \r\n"
        "paddsh $f27, $f27, %[ff_pw_16]         \r\n"
        "paddsh $f28, $f28, %[ff_pw_16]         \r\n"
        "psrah $f27, $f27, %[ff_pw_5]           \r\n"
        "psrah $f28, $f28, %[ff_pw_5]           \r\n"
        "packushb $f27, $f27, $f0               \r\n"
        "packushb $f28, $f28, $f0               \r\n"
        "punpcklwd $f14, $f27, $f28             \r\n"
        "sdc1 $f14, 0(%[dst6])                  \r\n"
        "paddsh $f27, $f19, $f21                \r\n"
        "pmullh $f27, $f27, %[ff_pw_4]          \r\n"
        "paddsh $f28, $f20, $f22                \r\n"//src7+src8
        "pmullh $f28, $f28, %[ff_pw_4]          \r\n"
        "psubsh $f27, $f27, $f17                \r\n"
        "psubsh $f28, $f28, $f18                \r\n"
        "psubsh $f27, $f27, $f23                \r\n"
        "psubsh $f28, $f28, $f24                \r\n"
        "pmullh $f27, $f27, %[ff_pw_5]          \r\n"
        "pmullh $f28, $f28, %[ff_pw_5]          \r\n"
        "paddsh $f27, $f27, $f15                \r\n"
        "paddsh $f28, $f28, $f16                \r\n"
        "paddsh $f27, $f27, $f25                \r\n"
        "paddsh $f28, $f28, $f26                \r\n"
        "paddsh $f27, $f27, %[ff_pw_16]         \r\n"
        "paddsh $f28, $f28, %[ff_pw_16]         \r\n"
        "psrah $f27, $f27, %[ff_pw_5]           \r\n"
        "psrah $f28, $f28, %[ff_pw_5]           \r\n"
        "packushb $f27, $f27, $f0               \r\n"
        "packushb $f28, $f28, $f0               \r\n"
        "punpcklwd $f16, $f27, $f28             \r\n"
        "sdc1 $f16, 0(%[dst7])                  \r\n"
        ::[dst0]"r"(dst),               [dst1]"r"(dst+dstStride),
          [dst2]"r"(dst+2*dstStride),   [dst3]"r"(dst+3*dstStride),
          [dst4]"r"(dst+4*dstStride),   [dst5]"r"(dst+5*dstStride),
          [dst6]"r"(dst+6*dstStride),   [dst7]"r"(dst+7*dstStride),
          [srcB]"r"(src-2*srcStride),   [srcA]"r"(src-srcStride),
          [src0]"r"(src),               [src1]"r"(src+srcStride),
          [src2]"r"(src+2*srcStride),   [src3]"r"(src+3*srcStride),
          [src4]"r"(src+4*srcStride),   [src5]"r"(src+5*srcStride),
          [src6]"r"(src+6*srcStride),   [src7]"r"(src+7*srcStride),
          [src8]"r"(src+8*srcStride),   [src9]"r"(src+9*srcStride),
          [src10]"r"(src+10*srcStride), [ff_pw_4]"f"(ff_pw_4),
          [ff_pw_5]"f"(ff_pw_5),        [ff_pw_16]"f"(ff_pw_16)
        : "$f0","$f1","$f2","$f3","$f4","$f5","$f6","$f7","$f8","$f9","$f10",
          "$f11","$f12","$f13","$f14","$f15","$f16","$f17","$f18","$f19",
          "$f20","$f21","$f22","$f23","$f24","$f25","$f26","$f27","$f28"
    );
}

static void put_h264_qpel16_v_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    put_h264_qpel8_v_lowpass_mmi(dst, src, dstStride, srcStride);
    put_h264_qpel8_v_lowpass_mmi(dst+8, src+8, dstStride, srcStride);
    src += 8*srcStride;
    dst += 8*dstStride;
    put_h264_qpel8_v_lowpass_mmi(dst, src, dstStride, srcStride);
    put_h264_qpel8_v_lowpass_mmi(dst+8, src+8, dstStride, srcStride);
}

static void avg_h264_qpel4_v_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    __asm__ volatile (
        "xor $f0, $f0, $f0                      \r\n"
        "gslwlc1 $f2, 3(%[srcB])                \r\n"
        "gslwrc1 $f2, 0(%[srcB])                \r\n"
        "gslwlc1 $f4, 3(%[srcA])                \r\n"
        "gslwrc1 $f4, 0(%[srcA])                \r\n"
        "gslwlc1 $f6, 3(%[src0])                \r\n"
        "gslwrc1 $f6, 0(%[src0])                \r\n"
        "gslwlc1 $f8, 3(%[src1])                \r\n"
        "gslwrc1 $f8, 0(%[src1])                \r\n"
        "gslwlc1 $f10, 3(%[src2])               \r\n"
        "gslwrc1 $f10, 0(%[src2])               \r\n"
        "gslwlc1 $f12, 3(%[src3])               \r\n"
        "gslwrc1 $f12, 0(%[src3])               \r\n"
        "gslwlc1 $f14, 3(%[src4])               \r\n"
        "gslwrc1 $f14, 0(%[src4])               \r\n"
        "gslwlc1 $f16, 3(%[src5])               \r\n"
        "gslwrc1 $f16, 0(%[src5])               \r\n"
        "gslwlc1 $f18, 3(%[src6])               \r\n"
        "gslwrc1 $f18, 0(%[src6])               \r\n"
        "punpcklbh $f2, $f2, $f0                \r\n"
        "punpcklbh $f4, $f4, $f0                \r\n"
        "punpcklbh $f6, $f6, $f0                \r\n"
        "punpcklbh $f8, $f8, $f0                \r\n"
        "punpcklbh $f10, $f10, $f0              \r\n"
        "punpcklbh $f12, $f12, $f0              \r\n"
        "punpcklbh $f14, $f14, $f0              \r\n"
        "punpcklbh $f16, $f16, $f0              \r\n"
        "punpcklbh $f18, $f18, $f0              \r\n"
        "paddsh $f20, $f6, $f8                  \r\n"
        "pmullh $f20, $f20, %[ff_pw_20]         \r\n"
        "paddsh $f22, $f4, $f10                 \r\n"
        "pmullh $f22, $f22, %[ff_pw_5]          \r\n"
        "psubsh $f24, $f20, $f22                \r\n"
        "paddsh $f24, $f24, $f2                 \r\n"
        "paddsh $f24, $f24, $f12                \r\n"
        "paddsh $f20, $f8, $f10                 \r\n"
        "pmullh $f20, $f20, %[ff_pw_20]         \r\n"
        "paddsh $f22, $f6, $f12                 \r\n"
        "pmullh $f22, $f22, %[ff_pw_5]          \r\n"
        "psubsh $f26, $f20, $f22                \r\n"
        "paddsh $f26, $f26, $f4                 \r\n"
        "paddsh $f26, $f26, $f14                \r\n"
        "paddsh $f20, $f10, $f12                \r\n"
        "pmullh $f20, $f20, %[ff_pw_20]         \r\n"
        "paddsh $f22, $f8, $f14                 \r\n"
        "pmullh $f22, $f22, %[ff_pw_5]          \r\n"
        "psubsh $f28, $f20, $f22                \r\n"
        "paddsh $f28, $f28, $f6                 \r\n"
        "paddsh $f28, $f28, $f16                \r\n"
        "paddsh $f20, $f12, $f14                \r\n"
        "pmullh $f20, $f20, %[ff_pw_20]         \r\n"
        "paddsh $f22, $f10, $f16                \r\n"
        "pmullh $f22, $f22, %[ff_pw_5]          \r\n"
        "psubsh $f30, $f20, $f22                \r\n"
        "paddsh $f30, $f30, $f8                 \r\n"
        "paddsh $f30, $f30, $f18                \r\n"
        "paddsh $f24, $f24, %[ff_pw_16]         \r\n"
        "paddsh $f26, $f26, %[ff_pw_16]         \r\n"
        "paddsh $f28, $f28, %[ff_pw_16]         \r\n"
        "paddsh $f30, $f30, %[ff_pw_16]         \r\n"
        "psrah $f24, $f24, %[ff_pw_5]           \r\n"
        "psrah $f26, $f26, %[ff_pw_5]           \r\n"
        "psrah $f28, $f28, %[ff_pw_5]           \r\n"
        "psrah $f30, $f30, %[ff_pw_5]           \r\n"
        "packushb $f24, $f24, $f0               \r\n"
        "packushb $f26, $f26, $f0               \r\n"
        "packushb $f28, $f28, $f0               \r\n"
        "packushb $f30, $f30, $f0               \r\n"
        "lwc1 $f2, 0(%[dst0])                   \r\n"
        "lwc1 $f4, 0(%[dst1])                   \r\n"
        "lwc1 $f6, 0(%[dst2])                   \r\n"
        "lwc1 $f8, 0(%[dst3])                   \r\n"
        "pavgb $f24, $f2, $f24                  \r\n"
        "pavgb $f26, $f4, $f26                  \r\n"
        "pavgb $f28, $f6, $f28                  \r\n"
        "pavgb $f30, $f8, $f30                  \r\n"
        "swc1 $f24, 0(%[dst0])                  \r\n"
        "swc1 $f26, 0(%[dst1])                  \r\n"
        "swc1 $f28, 0(%[dst2])                  \r\n"
        "swc1 $f30, 0(%[dst3])                  \r\n"
        ::[dst0]"r"(dst),               [dst1]"r"(dst+dstStride),
          [dst2]"r"(dst+2*dstStride),   [dst3]"r"(dst+3*dstStride),
          [srcB]"r"(src-2*srcStride),   [srcA]"r"(src-srcStride),
          [src0]"r"(src),               [src1]"r"(src+srcStride),
          [src2]"r"(src+2*srcStride),   [src3]"r"(src+3*srcStride),
          [src4]"r"(src+4*srcStride),   [src5]"r"(src+5*srcStride),
          [src6]"r"(src+6*srcStride),   [ff_pw_20]"f"(ff_pw_20),
          [ff_pw_5]"f"(ff_pw_5),        [ff_pw_16]"f"(ff_pw_16)
        : "$f0","$f2","$f4","$f6","$f8","$f10","$f12","$f14","$f16","$f18",
          "$f20","$f22","$f24","$f26","$f28","$f30"
    );
}

static void avg_h264_qpel8_v_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    __asm__ volatile (
        "xor $f0, $f0, $f0                      \r\n"
        "gsldlc1 $f2, 7(%[srcB])                \r\n"
        "gsldrc1 $f2, 0(%[srcB])                \r\n"
        "gsldlc1 $f4, 7(%[srcA])                \r\n"
        "gsldrc1 $f4, 0(%[srcA])                \r\n"
        "gsldlc1 $f6, 7(%[src0])                \r\n"
        "gsldrc1 $f6, 0(%[src0])                \r\n"
        "gsldlc1 $f8, 7(%[src1])                \r\n"
        "gsldrc1 $f8, 0(%[src1])                \r\n"
        "gsldlc1 $f10, 7(%[src2])               \r\n"
        "gsldrc1 $f10, 0(%[src2])               \r\n"
        "gsldlc1 $f12, 7(%[src3])               \r\n"
        "gsldrc1 $f12, 0(%[src3])               \r\n"
        "gsldlc1 $f14, 7(%[src4])               \r\n"
        "gsldrc1 $f14, 0(%[src4])               \r\n"
        "gsldlc1 $f16, 7(%[src5])               \r\n"
        "gsldrc1 $f16, 0(%[src5])               \r\n"
        "gsldlc1 $f18, 7(%[src6])               \r\n"
        "gsldrc1 $f18, 0(%[src6])               \r\n"
        "gsldlc1 $f20, 7(%[src7])               \r\n"
        "gsldrc1 $f20, 0(%[src7])               \r\n"
        "gsldlc1 $f22, 7(%[src8])               \r\n"
        "gsldrc1 $f22, 0(%[src8])               \r\n"
        "gsldlc1 $f24, 7(%[src9])               \r\n"
        "gsldrc1 $f24, 0(%[src9])               \r\n"
        "gsldlc1 $f26, 7(%[src10])              \r\n"
        "gsldrc1 $f26, 0(%[src10])              \r\n"
        "punpcklbh $f1, $f2, $f0                \r\n"
        "punpckhbh $f2, $f2, $f0                \r\n"
        "punpcklbh $f3, $f4, $f0                \r\n"
        "punpckhbh $f4, $f4, $f0                \r\n"
        "punpcklbh $f5, $f6, $f0                \r\n"
        "punpckhbh $f6, $f6, $f0                \r\n"
        "punpcklbh $f7, $f8, $f0                \r\n"
        "punpckhbh $f8, $f8, $f0                \r\n"
        "punpcklbh $f9, $f10, $f0               \r\n"
        "punpckhbh $f10, $f10, $f0              \r\n"
        "punpcklbh $f11, $f12, $f0              \r\n"
        "punpckhbh $f12, $f12, $f0              \r\n"
        "punpcklbh $f13, $f14, $f0              \r\n"
        "punpckhbh $f14, $f14, $f0              \r\n"
        "punpcklbh $f15, $f16, $f0              \r\n"
        "punpckhbh $f16, $f16, $f0              \r\n"
        "punpcklbh $f17, $f18, $f0              \r\n"
        "punpckhbh $f18, $f18, $f0              \r\n"
        "punpcklbh $f19, $f20, $f0              \r\n"
        "punpckhbh $f20, $f20, $f0              \r\n"
        "punpcklbh $f21, $f22, $f0              \r\n"
        "punpckhbh $f22, $f22, $f0              \r\n"
        "punpcklbh $f23, $f24, $f0              \r\n"
        "punpckhbh $f24, $f24, $f0              \r\n"
        "punpcklbh $f25, $f26, $f0              \r\n"
        "punpckhbh $f26, $f26, $f0              \r\n"
        "paddsh $f27, $f5, $f7                  \r\n"
        "pmullh $f27, $f27, %[ff_pw_4]          \r\n"
        "paddsh $f28, $f6, $f8                  \r\n"//src0+src1
        "pmullh $f28, $f28, %[ff_pw_4]          \r\n"
        "psubsh $f27, $f27, $f3                 \r\n"
        "psubsh $f28, $f28, $f4                 \r\n"
        "psubsh $f27, $f27, $f9                 \r\n"
        "psubsh $f28, $f28, $f10                \r\n"
        "pmullh $f27, $f27, %[ff_pw_5]          \r\n"
        "pmullh $f28, $f28, %[ff_pw_5]          \r\n"
        "paddsh $f27, $f27, $f1                 \r\n"
        "paddsh $f28, $f28, $f2                 \r\n"
        "paddsh $f27, $f27, $f11                \r\n"
        "paddsh $f28, $f28, $f12                \r\n"
        "paddsh $f27, $f27, %[ff_pw_16]         \r\n"
        "paddsh $f28, $f28, %[ff_pw_16]         \r\n"
        "psrah $f27, $f27, %[ff_pw_5]           \r\n"
        "psrah $f28, $f28, %[ff_pw_5]           \r\n"
        "packushb $f27, $f27, $f0               \r\n"
        "packushb $f28, $f28, $f0               \r\n"
        "punpcklwd $f2, $f27, $f28              \r\n"
        "ldc1 $f28, 0(%[dst0])                  \r\n"
        "pavgb $f2, $f2, $f28                   \r\n"
        "sdc1 $f2, 0(%[dst0])                   \r\n"
        "paddsh $f27, $f7, $f9                  \r\n"
        "pmullh $f27, $f27, %[ff_pw_4]          \r\n"
        "paddsh $f28, $f8, $f10                 \r\n"//src1+src2
        "pmullh $f28, $f28, %[ff_pw_4]          \r\n"
        "psubsh $f27, $f27, $f5                 \r\n"
        "psubsh $f28, $f28, $f6                 \r\n"
        "psubsh $f27, $f27, $f11                \r\n"
        "psubsh $f28, $f28, $f12                \r\n"
        "pmullh $f27, $f27, %[ff_pw_5]          \r\n"
        "pmullh $f28, $f28, %[ff_pw_5]          \r\n"
        "paddsh $f27, $f27, $f3                 \r\n"
        "paddsh $f28, $f28, $f4                 \r\n"
        "paddsh $f27, $f27, $f13                \r\n"
        "paddsh $f28, $f28, $f14                \r\n"
        "paddsh $f27, $f27, %[ff_pw_16]         \r\n"
        "paddsh $f28, $f28, %[ff_pw_16]         \r\n"
        "psrah $f27, $f27, %[ff_pw_5]           \r\n"
        "psrah $f28, $f28, %[ff_pw_5]           \r\n"
        "packushb $f27, $f27, $f0               \r\n"
        "packushb $f28, $f28, $f0               \r\n"
        "punpcklwd $f4, $f27, $f28              \r\n"
        "ldc1 $f28, 0(%[dst1])                  \r\n"
        "pavgb $f4, $f4, $f28                   \r\n"
        "sdc1 $f4, 0(%[dst1])                   \r\n"
        "paddsh $f27, $f9, $f11                 \r\n"
        "pmullh $f27, $f27, %[ff_pw_4]          \r\n"
        "paddsh $f28, $f10, $f12                \r\n"//src2+src3
        "pmullh $f28, $f28, %[ff_pw_4]          \r\n"
        "psubsh $f27, $f27, $f7                 \r\n"
        "psubsh $f28, $f28, $f8                 \r\n"
        "psubsh $f27, $f27, $f13                \r\n"
        "psubsh $f28, $f28, $f14                \r\n"
        "pmullh $f27, $f27, %[ff_pw_5]          \r\n"
        "pmullh $f28, $f28, %[ff_pw_5]          \r\n"
        "paddsh $f27, $f27, $f5                 \r\n"
        "paddsh $f28, $f28, $f6                 \r\n"
        "paddsh $f27, $f27, $f15                \r\n"
        "paddsh $f28, $f28, $f16                \r\n"
        "paddsh $f27, $f27, %[ff_pw_16]         \r\n"
        "paddsh $f28, $f28, %[ff_pw_16]         \r\n"
        "psrah $f27, $f27, %[ff_pw_5]           \r\n"
        "psrah $f28, $f28, %[ff_pw_5]           \r\n"
        "packushb $f27, $f27, $f0               \r\n"
        "packushb $f28, $f28, $f0               \r\n"
        "punpcklwd $f6, $f27, $f28              \r\n"
        "ldc1 $f28, 0(%[dst2])                  \r\n"
        "pavgb $f6, $f6, $f28                   \r\n"
        "sdc1 $f6, 0(%[dst2])                   \r\n"
        "paddsh $f27, $f11, $f13                \r\n"
        "pmullh $f27, $f27, %[ff_pw_4]          \r\n"
        "paddsh $f28, $f12, $f14                \r\n"//src3+src4
        "pmullh $f28, $f28, %[ff_pw_4]          \r\n"
        "psubsh $f27, $f27, $f9                 \r\n"
        "psubsh $f28, $f28, $f10                \r\n"
        "psubsh $f27, $f27, $f15                \r\n"
        "psubsh $f28, $f28, $f16                \r\n"
        "pmullh $f27, $f27, %[ff_pw_5]          \r\n"
        "pmullh $f28, $f28, %[ff_pw_5]          \r\n"
        "paddsh $f27, $f27, $f7                 \r\n"
        "paddsh $f28, $f28, $f8                 \r\n"
        "paddsh $f27, $f27, $f17                \r\n"
        "paddsh $f28, $f28, $f18                \r\n"
        "paddsh $f27, $f27, %[ff_pw_16]         \r\n"
        "paddsh $f28, $f28, %[ff_pw_16]         \r\n"
        "psrah $f27, $f27, %[ff_pw_5]           \r\n"
        "psrah $f28, $f28, %[ff_pw_5]           \r\n"
        "packushb $f27, $f27, $f0               \r\n"
        "packushb $f28, $f28, $f0               \r\n"
        "punpcklwd $f8, $f27, $f28              \r\n"
        "ldc1 $f28, 0(%[dst3])                  \r\n"
        "pavgb $f8, $f8, $f28                   \r\n"
        "sdc1 $f8, 0(%[dst3])                   \r\n"
        "paddsh $f27, $f13, $f15                \r\n"
        "pmullh $f27, $f27, %[ff_pw_4]          \r\n"
        "paddsh $f28, $f14, $f16                \r\n"//src4+src5
        "pmullh $f28, $f28, %[ff_pw_4]          \r\n"
        "psubsh $f27, $f27, $f11                \r\n"
        "psubsh $f28, $f28, $f12                \r\n"
        "psubsh $f27, $f27, $f17                \r\n"
        "psubsh $f28, $f28, $f18                \r\n"
        "pmullh $f27, $f27, %[ff_pw_5]          \r\n"
        "pmullh $f28, $f28, %[ff_pw_5]          \r\n"
        "paddsh $f27, $f27, $f9                 \r\n"
        "paddsh $f28, $f28, $f10                \r\n"
        "paddsh $f27, $f27, $f19                \r\n"
        "paddsh $f28, $f28, $f20                \r\n"
        "paddsh $f27, $f27, %[ff_pw_16]         \r\n"
        "paddsh $f28, $f28, %[ff_pw_16]         \r\n"
        "psrah $f27, $f27, %[ff_pw_5]           \r\n"
        "psrah $f28, $f28, %[ff_pw_5]           \r\n"
        "packushb $f27, $f27, $f0               \r\n"
        "packushb $f28, $f28, $f0               \r\n"
        "punpcklwd $f10, $f27, $f28             \r\n"
        "ldc1 $f28, 0(%[dst4])                  \r\n"
        "pavgb $f10, $f10, $f28                 \r\n"
        "sdc1 $f10, 0(%[dst4])                  \r\n"
        "paddsh $f27, $f15, $f17                \r\n"
        "pmullh $f27, $f27, %[ff_pw_4]          \r\n"
        "paddsh $f28, $f16, $f18                \r\n"//src5+src6
        "pmullh $f28, $f28, %[ff_pw_4]          \r\n"
        "psubsh $f27, $f27, $f13                \r\n"
        "psubsh $f28, $f28, $f14                \r\n"
        "psubsh $f27, $f27, $f19                \r\n"
        "psubsh $f28, $f28, $f20                \r\n"
        "pmullh $f27, $f27, %[ff_pw_5]          \r\n"
        "pmullh $f28, $f28, %[ff_pw_5]          \r\n"
        "paddsh $f27, $f27, $f11                \r\n"
        "paddsh $f28, $f28, $f12                \r\n"
        "paddsh $f27, $f27, $f21                \r\n"
        "paddsh $f28, $f28, $f22                \r\n"
        "paddsh $f27, $f27, %[ff_pw_16]         \r\n"
        "paddsh $f28, $f28, %[ff_pw_16]         \r\n"
        "psrah $f27, $f27, %[ff_pw_5]           \r\n"
        "psrah $f28, $f28, %[ff_pw_5]           \r\n"
        "packushb $f27, $f27, $f0               \r\n"
        "packushb $f28, $f28, $f0               \r\n"
        "punpcklwd $f12, $f27, $f28             \r\n"
        "ldc1 $f28, 0(%[dst5])                  \r\n"
        "pavgb $f12, $f12, $f28                 \r\n"
        "sdc1 $f12, 0(%[dst5])                  \r\n"
        "paddsh $f27, $f17, $f19                \r\n"
        "pmullh $f27, $f27, %[ff_pw_4]          \r\n"
        "paddsh $f28, $f18, $f20                \r\n"//src6+src7
        "pmullh $f28, $f28, %[ff_pw_4]          \r\n"
        "psubsh $f27, $f27, $f15                \r\n"
        "psubsh $f28, $f28, $f16                \r\n"
        "psubsh $f27, $f27, $f21                \r\n"
        "psubsh $f28, $f28, $f22                \r\n"
        "pmullh $f27, $f27, %[ff_pw_5]          \r\n"
        "pmullh $f28, $f28, %[ff_pw_5]          \r\n"
        "paddsh $f27, $f27, $f13                \r\n"
        "paddsh $f28, $f28, $f14                \r\n"
        "paddsh $f27, $f27, $f23                \r\n"
        "paddsh $f28, $f28, $f24                \r\n"
        "paddsh $f27, $f27, %[ff_pw_16]         \r\n"
        "paddsh $f28, $f28, %[ff_pw_16]         \r\n"
        "psrah $f27, $f27, %[ff_pw_5]           \r\n"
        "psrah $f28, $f28, %[ff_pw_5]           \r\n"
        "packushb $f27, $f27, $f0               \r\n"
        "packushb $f28, $f28, $f0               \r\n"
        "punpcklwd $f14, $f27, $f28             \r\n"
        "ldc1 $f28, 0(%[dst6])                  \r\n"
        "pavgb $f14, $f14, $f28                 \r\n"
        "sdc1 $f14, 0(%[dst6])                  \r\n"
        "paddsh $f27, $f19, $f21                \r\n"
        "pmullh $f27, $f27, %[ff_pw_4]          \r\n"
        "paddsh $f28, $f20, $f22                \r\n"//src7+src8
        "pmullh $f28, $f28, %[ff_pw_4]          \r\n"
        "psubsh $f27, $f27, $f17                \r\n"
        "psubsh $f28, $f28, $f18                \r\n"
        "psubsh $f27, $f27, $f23                \r\n"
        "psubsh $f28, $f28, $f24                \r\n"
        "pmullh $f27, $f27, %[ff_pw_5]          \r\n"
        "pmullh $f28, $f28, %[ff_pw_5]          \r\n"
        "paddsh $f27, $f27, $f15                \r\n"
        "paddsh $f28, $f28, $f16                \r\n"
        "paddsh $f27, $f27, $f25                \r\n"
        "paddsh $f28, $f28, $f26                \r\n"
        "paddsh $f27, $f27, %[ff_pw_16]         \r\n"
        "paddsh $f28, $f28, %[ff_pw_16]         \r\n"
        "psrah $f27, $f27, %[ff_pw_5]           \r\n"
        "psrah $f28, $f28, %[ff_pw_5]           \r\n"
        "packushb $f27, $f27, $f0               \r\n"
        "packushb $f28, $f28, $f0               \r\n"
        "punpcklwd $f16, $f27, $f28             \r\n"
        "ldc1 $f28, 0(%[dst7])                  \r\n"
        "pavgb $f16, $f16, $f28                 \r\n"
        "sdc1 $f16, 0(%[dst7])                  \r\n"
        ::[dst0]"r"(dst),               [dst1]"r"(dst+dstStride),
          [dst2]"r"(dst+2*dstStride),   [dst3]"r"(dst+3*dstStride),
          [dst4]"r"(dst+4*dstStride),   [dst5]"r"(dst+5*dstStride),
          [dst6]"r"(dst+6*dstStride),   [dst7]"r"(dst+7*dstStride),
          [srcB]"r"(src-2*srcStride),   [srcA]"r"(src-srcStride),
          [src0]"r"(src),               [src1]"r"(src+srcStride),
          [src2]"r"(src+2*srcStride),   [src3]"r"(src+3*srcStride),
          [src4]"r"(src+4*srcStride),   [src5]"r"(src+5*srcStride),
          [src6]"r"(src+6*srcStride),   [src7]"r"(src+7*srcStride),
          [src8]"r"(src+8*srcStride),   [src9]"r"(src+9*srcStride),
          [src10]"r"(src+10*srcStride), [ff_pw_4]"f"(ff_pw_4),
          [ff_pw_5]"f"(ff_pw_5),        [ff_pw_16]"f"(ff_pw_16)
        : "$f0","$f1","$f2","$f3","$f4","$f5","$f6","$f7","$f8","$f9","$f10",
          "$f11","$f12","$f13","$f14","$f15","$f16","$f17","$f18","$f19",
          "$f20","$f21","$f22","$f23","$f24","$f25","$f26","$f27","$f28"
    );
}

static void avg_h264_qpel16_v_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    avg_h264_qpel8_v_lowpass_mmi(dst, src, dstStride, srcStride);
    avg_h264_qpel8_v_lowpass_mmi(dst+8, src+8, dstStride, srcStride);
    src += 8*srcStride;
    dst += 8*dstStride;
    avg_h264_qpel8_v_lowpass_mmi(dst, src, dstStride, srcStride);
    avg_h264_qpel8_v_lowpass_mmi(dst+8, src+8, dstStride, srcStride);
}

static void put_h264_qpel4_hv_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    int i;
    int16_t _tmp[36];
    int16_t *tmp = _tmp;
    src -= 2*srcStride;
    __asm__ volatile (
        "xor $f0, $f0, $f0                      \r\n"
        "dli $8, 9                              \r\n"
        "1:                                     \r\n"
        "gslwlc1 $f2, 1(%[src])                 \r\n"
        "gslwrc1 $f2, -2(%[src])                \r\n"
        "gslwlc1 $f4, 2(%[src])                 \r\n"
        "gslwrc1 $f4, -1(%[src])                \r\n"
        "gslwlc1 $f6, 3(%[src])                 \r\n"
        "gslwrc1 $f6, 0(%[src])                 \r\n"
        "gslwlc1 $f8, 4(%[src])                 \r\n"
        "gslwrc1 $f8, 1(%[src])                 \r\n"
        "gslwlc1 $f10, 5(%[src])                \r\n"
        "gslwrc1 $f10, 2(%[src])                \r\n"
        "gslwlc1 $f12, 6(%[src])                \r\n"
        "gslwrc1 $f12, 3(%[src])                \r\n"
        "punpcklbh $f2, $f2, $f0                \r\n"
        "punpcklbh $f4, $f4, $f0                \r\n"
        "punpcklbh $f6, $f6, $f0                \r\n"
        "punpcklbh $f8, $f8, $f0                \r\n"
        "punpcklbh $f10, $f10, $f0              \r\n"
        "punpcklbh $f12, $f12, $f0              \r\n"
        "paddsh $f14, $f6, $f8                  \r\n"
        "paddsh $f16, $f4, $f10                 \r\n"
        "paddsh $f18, $f2, $f12                 \r\n"
        "pmullh $f14, $f14, %[ff_pw_20]         \r\n"
        "pmullh $f16, $f16, %[ff_pw_5]          \r\n"
        "psubsh $f14, $f14, $f16                \r\n"
        "paddsh $f18, $f14, $f18                \r\n"
        "sdc1 $f18, 0(%[tmp])                   \r\n"
        "dadd %[tmp], %[tmp], %[tmpStride]      \r\n"
        "dadd %[src], %[src], %[srcStride]      \r\n"
        "daddi $8, $8, -1                       \r\n"
        "bnez $8, 1b                            \r\n"
        : [tmp]"+&r"(tmp),[src]"+&r"(src)
        : [tmpStride]"r"(8),[srcStride]"r"(srcStride),
          [ff_pw_20]"f"(ff_pw_20),[ff_pw_5]"f"(ff_pw_5)
        : "$8","$f0","$f2","$f4","$f6","$f8","$f10","$f12","$f14","$f16","$f18"
    );

    tmp -= 28;

    for(i=0; i<4; i++) {
        const int16_t tmpB= tmp[-8];
        const int16_t tmpA= tmp[-4];
        const int16_t tmp0= tmp[ 0];
        const int16_t tmp1= tmp[ 4];
        const int16_t tmp2= tmp[ 8];
        const int16_t tmp3= tmp[12];
        const int16_t tmp4= tmp[16];
        const int16_t tmp5= tmp[20];
        const int16_t tmp6= tmp[24];
        op2_put(dst[0*dstStride], (tmp0+tmp1)*20 - (tmpA+tmp2)*5 + (tmpB+tmp3));
        op2_put(dst[1*dstStride], (tmp1+tmp2)*20 - (tmp0+tmp3)*5 + (tmpA+tmp4));
        op2_put(dst[2*dstStride], (tmp2+tmp3)*20 - (tmp1+tmp4)*5 + (tmp0+tmp5));
        op2_put(dst[3*dstStride], (tmp3+tmp4)*20 - (tmp2+tmp5)*5 + (tmp1+tmp6));
        dst++;
        tmp++;
    }
}

static void put_h264_qpel8_hv_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    int16_t _tmp[104];
    int16_t *tmp = _tmp;
    int i;
    src -= 2*srcStride;

    __asm__ volatile (
        "xor $f0, $f0, $f0                      \r\n"
        "dli $8, 13                             \r\n"
        "1:                                     \r\n"
        "gsldlc1 $f2, 5(%[src])                 \r\n"
        "gsldrc1 $f2, -2(%[src])                \r\n"
        "gsldlc1 $f4, 6(%[src])                 \r\n"
        "gsldrc1 $f4, -1(%[src])                \r\n"
        "gsldlc1 $f6, 7(%[src])                 \r\n"
        "gsldrc1 $f6, 0(%[src])                 \r\n"
        "gsldlc1 $f8, 8(%[src])                 \r\n"
        "gsldrc1 $f8, 1(%[src])                 \r\n"
        "gsldlc1 $f10, 9(%[src])                \r\n"
        "gsldrc1 $f10, 2(%[src])                \r\n"
        "gsldlc1 $f12, 10(%[src])               \r\n"
        "gsldrc1 $f12, 3(%[src])                \r\n"
        "punpcklbh $f1, $f2, $f0                \r\n"
        "punpcklbh $f3, $f4, $f0                \r\n"
        "punpcklbh $f5, $f6, $f0                \r\n"
        "punpcklbh $f7, $f8, $f0                \r\n"
        "punpcklbh $f9, $f10, $f0               \r\n"
        "punpcklbh $f11, $f12, $f0              \r\n"
        "punpckhbh $f2, $f2, $f0                \r\n"
        "punpckhbh $f4, $f4, $f0                \r\n"
        "punpckhbh $f6, $f6, $f0                \r\n"
        "punpckhbh $f8, $f8, $f0                \r\n"
        "punpckhbh $f10, $f10, $f0              \r\n"
        "punpckhbh $f12, $f12, $f0              \r\n"
        "paddsh $f13, $f5, $f7                  \r\n"
        "paddsh $f15, $f3, $f9                 \r\n"
        "paddsh $f17, $f1, $f11                 \r\n"
        "pmullh $f13, $f13, %[ff_pw_20]         \r\n"
        "pmullh $f15, $f15, %[ff_pw_5]          \r\n"
        "psubsh $f13, $f13, $f15                \r\n"
        "paddsh $f17, $f13, $f17                \r\n"
        "paddsh $f14, $f6, $f8                  \r\n"
        "paddsh $f16, $f4, $f10                 \r\n"
        "paddsh $f18, $f2, $f12                 \r\n"
        "pmullh $f14, $f14, %[ff_pw_20]         \r\n"
        "pmullh $f16, $f16, %[ff_pw_5]          \r\n"
        "psubsh $f14, $f14, $f16                \r\n"
        "paddsh $f18, $f14, $f18                \r\n"
        "sdc1 $f17, 0(%[tmp])                   \r\n"
        "sdc1 $f18, 8(%[tmp])                   \r\n"
        "dadd %[tmp], %[tmp], %[tmpStride]      \r\n"
        "dadd %[src], %[src], %[srcStride]      \r\n"
        "daddi $8, $8, -1                       \r\n"
        "bnez $8, 1b                            \r\n"
        : [tmp]"+&r"(tmp),[src]"+&r"(src)
        : [tmpStride]"r"(16),[srcStride]"r"(srcStride),
          [ff_pw_20]"f"(ff_pw_20),[ff_pw_5]"f"(ff_pw_5)
        : "$8","$f0","$f1","$f2","$f3","$f4","$f5","$f6","$f7","$f8","$f9",
          "$f10","$f11","$f12","$f13","$f14","$f15","$f16","$f17","$f18"
    );

    tmp -= 88;

    for(i=0; i<8; i++) {
        const int tmpB= tmp[-16];
        const int tmpA= tmp[ -8];
        const int tmp0= tmp[  0];
        const int tmp1= tmp[  8];
        const int tmp2= tmp[ 16];
        const int tmp3= tmp[ 24];
        const int tmp4= tmp[ 32];
        const int tmp5= tmp[ 40];
        const int tmp6= tmp[ 48];
        const int tmp7= tmp[ 56];
        const int tmp8= tmp[ 64];
        const int tmp9= tmp[ 72];
        const int tmp10=tmp[ 80];
        op2_put(dst[0*dstStride], (tmp0+tmp1)*20 - (tmpA+tmp2)*5 + (tmpB+tmp3));
        op2_put(dst[1*dstStride], (tmp1+tmp2)*20 - (tmp0+tmp3)*5 + (tmpA+tmp4));
        op2_put(dst[2*dstStride], (tmp2+tmp3)*20 - (tmp1+tmp4)*5 + (tmp0+tmp5));
        op2_put(dst[3*dstStride], (tmp3+tmp4)*20 - (tmp2+tmp5)*5 + (tmp1+tmp6));
        op2_put(dst[4*dstStride], (tmp4+tmp5)*20 - (tmp3+tmp6)*5 + (tmp2+tmp7));
        op2_put(dst[5*dstStride], (tmp5+tmp6)*20 - (tmp4+tmp7)*5 + (tmp3+tmp8));
        op2_put(dst[6*dstStride], (tmp6+tmp7)*20 - (tmp5+tmp8)*5 + (tmp4+tmp9));
        op2_put(dst[7*dstStride], (tmp7+tmp8)*20 - (tmp6+tmp9)*5 + (tmp5+tmp10));
        dst++;
        tmp++;
    }
}

static void put_h264_qpel16_hv_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    put_h264_qpel8_hv_lowpass_mmi(dst, src, dstStride, srcStride);
    put_h264_qpel8_hv_lowpass_mmi(dst+8, src+8, dstStride, srcStride);
    src += 8*srcStride;
    dst += 8*dstStride;
    put_h264_qpel8_hv_lowpass_mmi(dst, src, dstStride, srcStride);
    put_h264_qpel8_hv_lowpass_mmi(dst+8, src+8, dstStride, srcStride);
}

static void avg_h264_qpel4_hv_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    int i;
    int16_t _tmp[36];
    int16_t *tmp = _tmp;
    src -= 2*srcStride;

    __asm__ volatile (
        "xor $f0, $f0, $f0                      \r\n"
        "dli $8, 9                              \r\n"
        "1:                                     \r\n"
        "gslwlc1 $f2, 1(%[src])                 \r\n"
        "gslwrc1 $f2, -2(%[src])                \r\n"
        "gslwlc1 $f4, 2(%[src])                 \r\n"
        "gslwrc1 $f4, -1(%[src])                \r\n"
        "gslwlc1 $f6, 3(%[src])                 \r\n"
        "gslwrc1 $f6, 0(%[src])                 \r\n"
        "gslwlc1 $f8, 4(%[src])                 \r\n"
        "gslwrc1 $f8, 1(%[src])                 \r\n"
        "gslwlc1 $f10, 5(%[src])                \r\n"
        "gslwrc1 $f10, 2(%[src])                \r\n"
        "gslwlc1 $f12, 6(%[src])                \r\n"
        "gslwrc1 $f12, 3(%[src])                \r\n"
        "punpcklbh $f2, $f2, $f0                \r\n"
        "punpcklbh $f4, $f4, $f0                \r\n"
        "punpcklbh $f6, $f6, $f0                \r\n"
        "punpcklbh $f8, $f8, $f0                \r\n"
        "punpcklbh $f10, $f10, $f0              \r\n"
        "punpcklbh $f12, $f12, $f0              \r\n"
        "paddsh $f14, $f6, $f8                  \r\n"
        "paddsh $f16, $f4, $f10                 \r\n"
        "paddsh $f18, $f2, $f12                 \r\n"
        "pmullh $f14, $f14, %[ff_pw_20]         \r\n"
        "pmullh $f16, $f16, %[ff_pw_5]          \r\n"
        "psubsh $f14, $f14, $f16                \r\n"
        "paddsh $f18, $f14, $f18                \r\n"
        "sdc1 $f18, 0(%[tmp])                   \r\n"
        "dadd %[tmp], %[tmp], %[tmpStride]      \r\n"
        "dadd %[src], %[src], %[srcStride]      \r\n"
        "daddi $8, $8, -1                       \r\n"
        "bnez $8, 1b                            \r\n"
        : [tmp]"+&r"(tmp),[src]"+&r"(src)
        : [tmpStride]"r"(8),[srcStride]"r"(srcStride),
          [ff_pw_20]"f"(ff_pw_20),[ff_pw_5]"f"(ff_pw_5)
        : "$8","$f0","$f2","$f4","$f6","$f8","$f10","$f12","$f14","$f16","$f18"
    );

    tmp -= 28;

    for(i=0; i<4; i++)
    {
        const int16_t tmpB= tmp[-8];
        const int16_t tmpA= tmp[-4];
        const int16_t tmp0= tmp[ 0];
        const int16_t tmp1= tmp[ 4];
        const int16_t tmp2= tmp[ 8];
        const int16_t tmp3= tmp[12];
        const int16_t tmp4= tmp[16];
        const int16_t tmp5= tmp[20];
        const int16_t tmp6= tmp[24];
        op2_avg(dst[0*dstStride], (tmp0+tmp1)*20 - (tmpA+tmp2)*5 + (tmpB+tmp3));
        op2_avg(dst[1*dstStride], (tmp1+tmp2)*20 - (tmp0+tmp3)*5 + (tmpA+tmp4));
        op2_avg(dst[2*dstStride], (tmp2+tmp3)*20 - (tmp1+tmp4)*5 + (tmp0+tmp5));
        op2_avg(dst[3*dstStride], (tmp3+tmp4)*20 - (tmp2+tmp5)*5 + (tmp1+tmp6));
        dst++;
        tmp++;
    }
}

static void avg_h264_qpel8_hv_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride)
{
    int16_t _tmp[104];
    int16_t *tmp = _tmp;
    int i;
    src -= 2*srcStride;

    __asm__ volatile (
        "xor $f0, $f0, $f0                      \r\n"
        "dli $8, 13                             \r\n"
        "1:                                     \r\n"
        "gsldlc1 $f2, 5(%[src])                 \r\n"
        "gsldrc1 $f2, -2(%[src])                \r\n"
        "gsldlc1 $f4, 6(%[src])                 \r\n"
        "gsldrc1 $f4, -1(%[src])                \r\n"
        "gsldlc1 $f6, 7(%[src])                 \r\n"
        "gsldrc1 $f6, 0(%[src])                 \r\n"
        "gsldlc1 $f8, 8(%[src])                 \r\n"
        "gsldrc1 $f8, 1(%[src])                 \r\n"
        "gsldlc1 $f10, 9(%[src])                \r\n"
        "gsldrc1 $f10, 2(%[src])                \r\n"
        "gsldlc1 $f12, 10(%[src])               \r\n"
        "gsldrc1 $f12, 3(%[src])                \r\n"
        "punpcklbh $f1, $f2, $f0                \r\n"
        "punpcklbh $f3, $f4, $f0                \r\n"
        "punpcklbh $f5, $f6, $f0                \r\n"
        "punpcklbh $f7, $f8, $f0                \r\n"
        "punpcklbh $f9, $f10, $f0               \r\n"
        "punpcklbh $f11, $f12, $f0              \r\n"
        "punpckhbh $f2, $f2, $f0                \r\n"
        "punpckhbh $f4, $f4, $f0                \r\n"
        "punpckhbh $f6, $f6, $f0                \r\n"
        "punpckhbh $f8, $f8, $f0                \r\n"
        "punpckhbh $f10, $f10, $f0              \r\n"
        "punpckhbh $f12, $f12, $f0              \r\n"
        "paddsh $f13, $f5, $f7                  \r\n"
        "paddsh $f15, $f3, $f9                 \r\n"
        "paddsh $f17, $f1, $f11                 \r\n"
        "pmullh $f13, $f13, %[ff_pw_20]         \r\n"
        "pmullh $f15, $f15, %[ff_pw_5]          \r\n"
        "psubsh $f13, $f13, $f15                \r\n"
        "paddsh $f17, $f13, $f17                \r\n"
        "paddsh $f14, $f6, $f8                  \r\n"
        "paddsh $f16, $f4, $f10                 \r\n"
        "paddsh $f18, $f2, $f12                 \r\n"
        "pmullh $f14, $f14, %[ff_pw_20]         \r\n"
        "pmullh $f16, $f16, %[ff_pw_5]          \r\n"
        "psubsh $f14, $f14, $f16                \r\n"
        "paddsh $f18, $f14, $f18                \r\n"

        "sdc1 $f17, 0(%[tmp])                   \r\n"
        "sdc1 $f18, 8(%[tmp])                   \r\n"
        "dadd %[tmp], %[tmp], %[tmpStride]      \r\n"
        "dadd %[src], %[src], %[srcStride]      \r\n"
        "daddi $8, $8, -1                       \r\n"
        "bnez $8, 1b                            \r\n"
        : [tmp]"+&r"(tmp),[src]"+&r"(src)
        : [tmpStride]"r"(16),[srcStride]"r"(srcStride),
          [ff_pw_20]"f"(ff_pw_20),[ff_pw_5]"f"(ff_pw_5)
        : "$8","$f0","$f1","$f2","$f3","$f4","$f5","$f6","$f7","$f8","$f9",
          "$f10","$f11","$f12","$f13","$f14","$f15","$f16","$f17","$f18"
    );

    tmp -= 88;

    for(i=0; i<8; i++) {
        const int tmpB= tmp[-16];
        const int tmpA= tmp[ -8];
        const int tmp0= tmp[  0];
        const int tmp1= tmp[  8];
        const int tmp2= tmp[ 16];
        const int tmp3= tmp[ 24];
        const int tmp4= tmp[ 32];
        const int tmp5= tmp[ 40];
        const int tmp6= tmp[ 48];
        const int tmp7= tmp[ 56];
        const int tmp8= tmp[ 64];
        const int tmp9= tmp[ 72];
        const int tmp10=tmp[ 80];
        op2_avg(dst[0*dstStride], (tmp0+tmp1)*20 - (tmpA+tmp2)*5 + (tmpB+tmp3));
        op2_avg(dst[1*dstStride], (tmp1+tmp2)*20 - (tmp0+tmp3)*5 + (tmpA+tmp4));
        op2_avg(dst[2*dstStride], (tmp2+tmp3)*20 - (tmp1+tmp4)*5 + (tmp0+tmp5));
        op2_avg(dst[3*dstStride], (tmp3+tmp4)*20 - (tmp2+tmp5)*5 + (tmp1+tmp6));
        op2_avg(dst[4*dstStride], (tmp4+tmp5)*20 - (tmp3+tmp6)*5 + (tmp2+tmp7));
        op2_avg(dst[5*dstStride], (tmp5+tmp6)*20 - (tmp4+tmp7)*5 + (tmp3+tmp8));
        op2_avg(dst[6*dstStride], (tmp6+tmp7)*20 - (tmp5+tmp8)*5 + (tmp4+tmp9));
        op2_avg(dst[7*dstStride], (tmp7+tmp8)*20 - (tmp6+tmp9)*5 + (tmp5+tmp10));
        dst++;
        tmp++;
    }
}

static void avg_h264_qpel16_hv_lowpass_mmi(uint8_t *dst, const uint8_t *src,
        int dstStride, int srcStride){
    avg_h264_qpel8_hv_lowpass_mmi(dst, src, dstStride, srcStride);
    avg_h264_qpel8_hv_lowpass_mmi(dst+8, src+8, dstStride, srcStride);
    src += 8*srcStride;
    dst += 8*dstStride;
    avg_h264_qpel8_hv_lowpass_mmi(dst, src, dstStride, srcStride);
    avg_h264_qpel8_hv_lowpass_mmi(dst+8, src+8, dstStride, srcStride);
}

//DEF_H264_MC_MMI(put_, 4)
void ff_put_h264_qpel4_mc00_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    put_pixels4_mmi(dst, src, stride, 4);
}

void ff_put_h264_qpel4_mc10_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[16];
    put_h264_qpel4_h_lowpass_mmi(half, src, 4, stride);
    put_pixels4_l2_mmi(dst, src, half, stride, stride, 4, 4);
}

void ff_put_h264_qpel4_mc20_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    put_h264_qpel4_h_lowpass_mmi(dst, src, stride, stride);
}

void ff_put_h264_qpel4_mc30_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[16];
    put_h264_qpel4_h_lowpass_mmi(half, src, 4, stride);
    put_pixels4_l2_mmi(dst, src+1, half, stride, stride, 4, 4);
}

void ff_put_h264_qpel4_mc01_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t half[16];
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(half, full_mid, 4, 4);
    put_pixels4_l2_mmi(dst, full_mid, half, stride, 4, 4, 4);
}

void ff_put_h264_qpel4_mc02_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(dst, full_mid, stride, 4);
}

void ff_put_h264_qpel4_mc03_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t half[16];
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(half, full_mid, 4, 4);
    put_pixels4_l2_mmi(dst, full_mid+4, half, stride, 4, 4, 4);
}

void ff_put_h264_qpel4_mc11_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfH[16];
    uint8_t halfV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src, 4, stride);
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    put_pixels4_l2_mmi(dst, halfH, halfV, stride, 4, 4, 4);
}

void ff_put_h264_qpel4_mc31_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfH[16];
    uint8_t halfV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src, 4, stride);
    copy_block4_mmi(full, src - stride*2 + 1, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    put_pixels4_l2_mmi(dst, halfH, halfV, stride, 4, 4, 4);
}

void ff_put_h264_qpel4_mc13_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfH[16];
    uint8_t halfV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src + stride, 4, stride);
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    put_pixels4_l2_mmi(dst, halfH, halfV, stride, 4, 4, 4);
}

void ff_put_h264_qpel4_mc33_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfH[16];
    uint8_t halfV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src + stride, 4, stride);
    copy_block4_mmi(full, src - stride*2 + 1, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    put_pixels4_l2_mmi(dst, halfH, halfV, stride, 4, 4, 4);
}

void ff_put_h264_qpel4_mc22_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    put_h264_qpel4_hv_lowpass_mmi(dst, src, stride, stride);
}

void ff_put_h264_qpel4_mc21_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t halfH[16];
    uint8_t halfHV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src, 4, stride);
    put_h264_qpel4_hv_lowpass_mmi(halfHV, src, 4, stride);
    put_pixels4_l2_mmi(dst, halfH, halfHV, stride, 4, 4, 4);
}

void ff_put_h264_qpel4_mc23_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t halfH[16];
    uint8_t halfHV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src + stride, 4, stride);
    put_h264_qpel4_hv_lowpass_mmi(halfHV, src, 4, stride);
    put_pixels4_l2_mmi(dst, halfH, halfHV, stride, 4, 4, 4);
}

void ff_put_h264_qpel4_mc12_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfV[16];
    uint8_t halfHV[16];
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    put_h264_qpel4_hv_lowpass_mmi(halfHV, src, 4, stride);
    put_pixels4_l2_mmi(dst, halfV, halfHV, stride, 4, 4, 4);
}

void ff_put_h264_qpel4_mc32_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfV[16];
    uint8_t halfHV[16];
    copy_block4_mmi(full, src - stride*2 + 1, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    put_h264_qpel4_hv_lowpass_mmi(halfHV, src, 4, stride);
    put_pixels4_l2_mmi(dst, halfV, halfHV, stride, 4, 4, 4);
}

//DEF_H264_MC_MMI(avg_, 4)
void ff_avg_h264_qpel4_mc00_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    avg_pixels4_mmi(dst, src, stride, 4);
}

void ff_avg_h264_qpel4_mc10_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[16];
    put_h264_qpel4_h_lowpass_mmi(half, src, 4, stride);
    avg_pixels4_l2_mmi(dst, src, half, stride, stride, 4, 4);
}

void ff_avg_h264_qpel4_mc20_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    avg_h264_qpel4_h_lowpass_mmi(dst, src, stride, stride);
}

void ff_avg_h264_qpel4_mc30_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[16];
    put_h264_qpel4_h_lowpass_mmi(half, src, 4, stride);
    avg_pixels4_l2_mmi(dst, src+1, half, stride, stride, 4, 4);
}

void ff_avg_h264_qpel4_mc01_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t half[16];
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(half, full_mid, 4, 4);
    avg_pixels4_l2_mmi(dst, full_mid, half, stride, 4, 4, 4);
}

void ff_avg_h264_qpel4_mc02_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    avg_h264_qpel4_v_lowpass_mmi(dst, full_mid, stride, 4);
}

void ff_avg_h264_qpel4_mc03_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t half[16];
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(half, full_mid, 4, 4);
    avg_pixels4_l2_mmi(dst, full_mid+4, half, stride, 4, 4, 4);
}

void ff_avg_h264_qpel4_mc11_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfH[16];
    uint8_t halfV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src, 4, stride);
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    avg_pixels4_l2_mmi(dst, halfH, halfV, stride, 4, 4, 4);
}

void ff_avg_h264_qpel4_mc31_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfH[16];
    uint8_t halfV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src, 4, stride);
    copy_block4_mmi(full, src - stride*2 + 1, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    avg_pixels4_l2_mmi(dst, halfH, halfV, stride, 4, 4, 4);
}

void ff_avg_h264_qpel4_mc13_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfH[16];
    uint8_t halfV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src + stride, 4, stride);
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    avg_pixels4_l2_mmi(dst, halfH, halfV, stride, 4, 4, 4);
}

void ff_avg_h264_qpel4_mc33_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfH[16];
    uint8_t halfV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src + stride, 4, stride);
    copy_block4_mmi(full, src - stride*2 + 1, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    avg_pixels4_l2_mmi(dst, halfH, halfV, stride, 4, 4, 4);
}

void ff_avg_h264_qpel4_mc22_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    avg_h264_qpel4_hv_lowpass_mmi(dst, src, stride, stride);
}

void ff_avg_h264_qpel4_mc21_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t halfH[16];
    uint8_t halfHV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src, 4, stride);
    put_h264_qpel4_hv_lowpass_mmi(halfHV, src, 4, stride);
    avg_pixels4_l2_mmi(dst, halfH, halfHV, stride, 4, 4, 4);
}

void ff_avg_h264_qpel4_mc23_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t halfH[16];
    uint8_t halfHV[16];
    put_h264_qpel4_h_lowpass_mmi(halfH, src + stride, 4, stride);
    put_h264_qpel4_hv_lowpass_mmi(halfHV, src, 4, stride);
    avg_pixels4_l2_mmi(dst, halfH, halfHV, stride, 4, 4, 4);
}

void ff_avg_h264_qpel4_mc12_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfV[16];
    uint8_t halfHV[16];
    copy_block4_mmi(full, src - stride*2, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    put_h264_qpel4_hv_lowpass_mmi(halfHV, src, 4, stride);
    avg_pixels4_l2_mmi(dst, halfV, halfHV, stride, 4, 4, 4);
}

void ff_avg_h264_qpel4_mc32_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[36];
    uint8_t * const full_mid= full + 8;
    uint8_t halfV[16];
    uint8_t halfHV[16];
    copy_block4_mmi(full, src - stride*2 + 1, 4,  stride, 9);
    put_h264_qpel4_v_lowpass_mmi(halfV, full_mid, 4, 4);
    put_h264_qpel4_hv_lowpass_mmi(halfHV, src, 4, stride);
    avg_pixels4_l2_mmi(dst, halfV, halfHV, stride, 4, 4, 4);
}

//DEF_H264_MC_MMI(put_, 8)
void ff_put_h264_qpel8_mc00_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    put_pixels8_mmi(dst, src, stride, 8);
}

void ff_put_h264_qpel8_mc10_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[64];
    put_h264_qpel8_h_lowpass_mmi(half, src, 8, stride);
    put_pixels8_l2_mmi(dst, src, half, stride, stride, 8, 8);
}

void ff_put_h264_qpel8_mc20_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    put_h264_qpel8_h_lowpass_mmi(dst, src, stride, stride);
}

void ff_put_h264_qpel8_mc30_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[64];
    put_h264_qpel8_h_lowpass_mmi(half, src, 8, stride);
    put_pixels8_l2_mmi(dst, src+1, half, stride, stride, 8, 8);
}

void ff_put_h264_qpel8_mc01_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t half[64];
    copy_block8_mmi(full, src - stride*2, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(half, full_mid, 8, 8);
    put_pixels8_l2_mmi(dst, full_mid, half, stride, 8, 8, 8);
}

void ff_put_h264_qpel8_mc02_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    copy_block8_mmi(full, src - stride*2, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(dst, full_mid, stride, 8);
}

void ff_put_h264_qpel8_mc03_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t half[64];
    copy_block8_mmi(full, src - stride*2, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(half, full_mid, 8, 8);
    put_pixels8_l2_mmi(dst, full_mid+8, half, stride, 8, 8, 8);
}

void ff_put_h264_qpel8_mc11_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t halfH[64];
    uint8_t halfV[64];
    put_h264_qpel8_h_lowpass_mmi(halfH, src, 8, stride);
    copy_block8_mmi(full, src - stride*2, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(halfV, full_mid, 8, 8);
    put_pixels8_l2_mmi(dst, halfH, halfV, stride, 8, 8, 8);
}

void ff_put_h264_qpel8_mc31_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t halfH[64];
    uint8_t halfV[64];
    put_h264_qpel8_h_lowpass_mmi(halfH, src, 8, stride);
    copy_block8_mmi(full, src - stride*2 + 1, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(halfV, full_mid, 8, 8);
    put_pixels8_l2_mmi(dst, halfH, halfV, stride, 8, 8, 8);
}

void ff_put_h264_qpel8_mc13_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t halfH[64];
    uint8_t halfV[64];
    put_h264_qpel8_h_lowpass_mmi(halfH, src + stride, 8, stride);
    copy_block8_mmi(full, src - stride*2, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(halfV, full_mid, 8, 8);
    put_pixels8_l2_mmi(dst, halfH, halfV, stride, 8, 8, 8);
}

void ff_put_h264_qpel8_mc33_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t halfH[64];
    uint8_t halfV[64];
    put_h264_qpel8_h_lowpass_mmi(halfH, src + stride, 8, stride);
    copy_block8_mmi(full, src - stride*2 + 1, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(halfV, full_mid, 8, 8);
    put_pixels8_l2_mmi(dst, halfH, halfV, stride, 8, 8, 8);
}

void ff_put_h264_qpel8_mc22_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    put_h264_qpel8_hv_lowpass_mmi(dst, src, stride, stride);
}

void ff_put_h264_qpel8_mc21_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t halfH[64];
    uint8_t halfHV[64];
    put_h264_qpel8_h_lowpass_mmi(halfH, src, 8, stride);
    put_h264_qpel8_hv_lowpass_mmi(halfHV, src, 8, stride);
    put_pixels8_l2_mmi(dst, halfH, halfHV, stride, 8, 8, 8);
}

void ff_put_h264_qpel8_mc23_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t halfH[64];
    uint8_t halfHV[64];
    put_h264_qpel8_h_lowpass_mmi(halfH, src + stride, 8, stride);
    put_h264_qpel8_hv_lowpass_mmi(halfHV, src, 8, stride);
    put_pixels8_l2_mmi(dst, halfH, halfHV, stride, 8, 8, 8);
}

void ff_put_h264_qpel8_mc12_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t halfV[64];
    uint8_t halfHV[64];
    copy_block8_mmi(full, src - stride*2, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(halfV, full_mid, 8, 8);
    put_h264_qpel8_hv_lowpass_mmi(halfHV, src, 8, stride);
    put_pixels8_l2_mmi(dst, halfV, halfHV, stride, 8, 8, 8);
}

void ff_put_h264_qpel8_mc32_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t halfV[64];
    uint8_t halfHV[64];
    copy_block8_mmi(full, src - stride*2 + 1, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(halfV, full_mid, 8, 8);
    put_h264_qpel8_hv_lowpass_mmi(halfHV, src, 8, stride);
    put_pixels8_l2_mmi(dst, halfV, halfHV, stride, 8, 8, 8);
}

//DEF_H264_MC_MMI(avg_, 8)
void ff_avg_h264_qpel8_mc00_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    avg_pixels8_mmi(dst, src, stride, 8);
}

void ff_avg_h264_qpel8_mc10_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[64];
    put_h264_qpel8_h_lowpass_mmi(half, src, 8, stride);
    avg_pixels8_l2_mmi(dst, src, half, stride, stride, 8, 8);
}

void ff_avg_h264_qpel8_mc20_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    avg_h264_qpel8_h_lowpass_mmi(dst, src, stride, stride);
}

void ff_avg_h264_qpel8_mc30_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[64];
    put_h264_qpel8_h_lowpass_mmi(half, src, 8, stride);
    avg_pixels8_l2_mmi(dst, src+1, half, stride, stride, 8, 8);
}

void ff_avg_h264_qpel8_mc01_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t half[64];
    copy_block8_mmi(full, src - stride*2, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(half, full_mid, 8, 8);
    avg_pixels8_l2_mmi(dst, full_mid, half, stride, 8, 8, 8);
}

void ff_avg_h264_qpel8_mc02_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    copy_block8_mmi(full, src - stride*2, 8,  stride, 13);
    avg_h264_qpel8_v_lowpass_mmi(dst, full_mid, stride, 8);
}

void ff_avg_h264_qpel8_mc03_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t half[64];
    copy_block8_mmi(full, src - stride*2, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(half, full_mid, 8, 8);
    avg_pixels8_l2_mmi(dst, full_mid+8, half, stride, 8, 8, 8);
}

void ff_avg_h264_qpel8_mc11_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t halfH[64];
    uint8_t halfV[64];
    put_h264_qpel8_h_lowpass_mmi(halfH, src, 8, stride);
    copy_block8_mmi(full, src - stride*2, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(halfV, full_mid, 8, 8);
    avg_pixels8_l2_mmi(dst, halfH, halfV, stride, 8, 8, 8);
}

void ff_avg_h264_qpel8_mc31_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t halfH[64];
    uint8_t halfV[64];
    put_h264_qpel8_h_lowpass_mmi(halfH, src, 8, stride);
    copy_block8_mmi(full, src - stride*2 + 1, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(halfV, full_mid, 8, 8);
    avg_pixels8_l2_mmi(dst, halfH, halfV, stride, 8, 8, 8);
}

void ff_avg_h264_qpel8_mc13_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t halfH[64];
    uint8_t halfV[64];
    put_h264_qpel8_h_lowpass_mmi(halfH, src + stride, 8, stride);
    copy_block8_mmi(full, src - stride*2, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(halfV, full_mid, 8, 8);
    avg_pixels8_l2_mmi(dst, halfH, halfV, stride, 8, 8, 8);
}

void ff_avg_h264_qpel8_mc33_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t halfH[64];
    uint8_t halfV[64];
    put_h264_qpel8_h_lowpass_mmi(halfH, src + stride, 8, stride);
    copy_block8_mmi(full, src - stride*2 + 1, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(halfV, full_mid, 8, 8);
    avg_pixels8_l2_mmi(dst, halfH, halfV, stride, 8, 8, 8);
}

void ff_avg_h264_qpel8_mc22_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    avg_h264_qpel8_hv_lowpass_mmi(dst, src, stride, stride);
}

void ff_avg_h264_qpel8_mc21_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t halfH[64];
    uint8_t halfHV[64];
    put_h264_qpel8_h_lowpass_mmi(halfH, src, 8, stride);
    put_h264_qpel8_hv_lowpass_mmi(halfHV, src, 8, stride);
    avg_pixels8_l2_mmi(dst, halfH, halfHV, stride, 8, 8, 8);
}

void ff_avg_h264_qpel8_mc23_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t halfH[64];
    uint8_t halfHV[64];
    put_h264_qpel8_h_lowpass_mmi(halfH, src + stride, 8, stride);
    put_h264_qpel8_hv_lowpass_mmi(halfHV, src, 8, stride);
    avg_pixels8_l2_mmi(dst, halfH, halfHV, stride, 8, 8, 8);
}

void ff_avg_h264_qpel8_mc12_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t halfV[64];
    uint8_t halfHV[64];
    copy_block8_mmi(full, src - stride*2, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(halfV, full_mid, 8, 8);
    put_h264_qpel8_hv_lowpass_mmi(halfHV, src, 8, stride);
    avg_pixels8_l2_mmi(dst, halfV, halfHV, stride, 8, 8, 8);
}

void ff_avg_h264_qpel8_mc32_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[104];
    uint8_t * const full_mid= full + 16;
    uint8_t halfV[64];
    uint8_t halfHV[64];
    copy_block8_mmi(full, src - stride*2 + 1, 8,  stride, 13);
    put_h264_qpel8_v_lowpass_mmi(halfV, full_mid, 8, 8);
    put_h264_qpel8_hv_lowpass_mmi(halfHV, src, 8, stride);
    avg_pixels8_l2_mmi(dst, halfV, halfHV, stride, 8, 8, 8);
}

//DEF_H264_MC_MMI(put_, 16)
void ff_put_h264_qpel16_mc00_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    put_pixels16_mmi(dst, src, stride, 16);
}

void ff_put_h264_qpel16_mc10_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[256];
    put_h264_qpel16_h_lowpass_mmi(half, src, 16, stride);
    put_pixels16_l2_mmi(dst, src, half, stride, stride, 16, 16);
}

void ff_put_h264_qpel16_mc20_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    put_h264_qpel16_h_lowpass_mmi(dst, src, stride, stride);
}

void ff_put_h264_qpel16_mc30_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[256];
    put_h264_qpel16_h_lowpass_mmi(half, src, 16, stride);
    put_pixels16_l2_mmi(dst, src+1, half, stride, stride, 16, 16);
}

void ff_put_h264_qpel16_mc01_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t half[256];
    copy_block16_mmi(full, src - stride*2, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(half, full_mid, 16, 16);
    put_pixels16_l2_mmi(dst, full_mid, half, stride, 16, 16, 16);
}

void ff_put_h264_qpel16_mc02_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    copy_block16_mmi(full, src - stride*2, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(dst, full_mid, stride, 16);
}

void ff_put_h264_qpel16_mc03_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t half[256];
    copy_block16_mmi(full, src - stride*2, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(half, full_mid, 16, 16);
    put_pixels16_l2_mmi(dst, full_mid+16, half, stride, 16, 16, 16);
}

void ff_put_h264_qpel16_mc11_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t halfH[256];
    uint8_t halfV[256];
    put_h264_qpel16_h_lowpass_mmi(halfH, src, 16, stride);
    copy_block16_mmi(full, src - stride*2, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(halfV, full_mid, 16, 16);
    put_pixels16_l2_mmi(dst, halfH, halfV, stride, 16, 16, 16);
}

void ff_put_h264_qpel16_mc31_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t halfH[256];
    uint8_t halfV[256];
    put_h264_qpel16_h_lowpass_mmi(halfH, src, 16, stride);
    copy_block16_mmi(full, src - stride*2 + 1, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(halfV, full_mid, 16, 16);
    put_pixels16_l2_mmi(dst, halfH, halfV, stride, 16, 16, 16);
}

void ff_put_h264_qpel16_mc13_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t halfH[256];
    uint8_t halfV[256];
    put_h264_qpel16_h_lowpass_mmi(halfH, src + stride, 16, stride);
    copy_block16_mmi(full, src - stride*2, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(halfV, full_mid, 16, 16);
    put_pixels16_l2_mmi(dst, halfH, halfV, stride, 16, 16, 16);
}

void ff_put_h264_qpel16_mc33_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t halfH[256];
    uint8_t halfV[256];
    put_h264_qpel16_h_lowpass_mmi(halfH, src + stride, 16, stride);
    copy_block16_mmi(full, src - stride*2 + 1, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(halfV, full_mid, 16, 16);
    put_pixels16_l2_mmi(dst, halfH, halfV, stride, 16, 16, 16);
}

void ff_put_h264_qpel16_mc22_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    put_h264_qpel16_hv_lowpass_mmi(dst, src, stride, stride);
}

void ff_put_h264_qpel16_mc21_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t halfH[256];
    uint8_t halfHV[256];
    put_h264_qpel16_h_lowpass_mmi(halfH, src, 16, stride);
    put_h264_qpel16_hv_lowpass_mmi(halfHV, src, 16, stride);
    put_pixels16_l2_mmi(dst, halfH, halfHV, stride, 16, 16, 16);
}

void ff_put_h264_qpel16_mc23_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t halfH[256];
    uint8_t halfHV[256];
    put_h264_qpel16_h_lowpass_mmi(halfH, src + stride, 16, stride);
    put_h264_qpel16_hv_lowpass_mmi(halfHV, src, 16, stride);
    put_pixels16_l2_mmi(dst, halfH, halfHV, stride, 16, 16, 16);
}

void ff_put_h264_qpel16_mc12_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t halfV[256];
    uint8_t halfHV[256];
    copy_block16_mmi(full, src - stride*2, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(halfV, full_mid, 16, 16);
    put_h264_qpel16_hv_lowpass_mmi(halfHV, src, 16, stride);
    put_pixels16_l2_mmi(dst, halfV, halfHV, stride, 16, 16, 16);
}

void ff_put_h264_qpel16_mc32_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t halfV[256];
    uint8_t halfHV[256];
    copy_block16_mmi(full, src - stride*2 + 1, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(halfV, full_mid, 16, 16);
    put_h264_qpel16_hv_lowpass_mmi(halfHV, src, 16, stride);
    put_pixels16_l2_mmi(dst, halfV, halfHV, stride, 16, 16, 16);
}

//DEF_H264_MC_MMI(avg_, 16)
void ff_avg_h264_qpel16_mc00_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    avg_pixels16_mmi(dst, src, stride, 16);
}

void ff_avg_h264_qpel16_mc10_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[256];
    put_h264_qpel16_h_lowpass_mmi(half, src, 16, stride);
    avg_pixels16_l2_mmi(dst, src, half, stride, stride, 16, 16);
}

void ff_avg_h264_qpel16_mc20_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    avg_h264_qpel16_h_lowpass_mmi(dst, src, stride, stride);
}

void ff_avg_h264_qpel16_mc30_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t half[256];
    put_h264_qpel16_h_lowpass_mmi(half, src, 16, stride);
    avg_pixels16_l2_mmi(dst, src+1, half, stride, stride, 16, 16);
}

void ff_avg_h264_qpel16_mc01_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t half[256];
    copy_block16_mmi(full, src - stride*2, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(half, full_mid, 16, 16);
    avg_pixels16_l2_mmi(dst, full_mid, half, stride, 16, 16, 16);
}

void ff_avg_h264_qpel16_mc02_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    copy_block16_mmi(full, src - stride*2, 16,  stride, 21);
    avg_h264_qpel16_v_lowpass_mmi(dst, full_mid, stride, 16);
}

void ff_avg_h264_qpel16_mc03_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t half[256];
    copy_block16_mmi(full, src - stride*2, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(half, full_mid, 16, 16);
    avg_pixels16_l2_mmi(dst, full_mid+16, half, stride, 16, 16, 16);
}

void ff_avg_h264_qpel16_mc11_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t halfH[256];
    uint8_t halfV[256];
    put_h264_qpel16_h_lowpass_mmi(halfH, src, 16, stride);
    copy_block16_mmi(full, src - stride*2, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(halfV, full_mid, 16, 16);
    avg_pixels16_l2_mmi(dst, halfH, halfV, stride, 16, 16, 16);
}

void ff_avg_h264_qpel16_mc31_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t halfH[256];
    uint8_t halfV[256];
    put_h264_qpel16_h_lowpass_mmi(halfH, src, 16, stride);
    copy_block16_mmi(full, src - stride*2 + 1, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(halfV, full_mid, 16, 16);
    avg_pixels16_l2_mmi(dst, halfH, halfV, stride, 16, 16, 16);
}

void ff_avg_h264_qpel16_mc13_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t halfH[256];
    uint8_t halfV[256];
    put_h264_qpel16_h_lowpass_mmi(halfH, src + stride, 16, stride);
    copy_block16_mmi(full, src - stride*2, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(halfV, full_mid, 16, 16);
    avg_pixels16_l2_mmi(dst, halfH, halfV, stride, 16, 16, 16);
}

void ff_avg_h264_qpel16_mc33_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t halfH[256];
    uint8_t halfV[256];
    put_h264_qpel16_h_lowpass_mmi(halfH, src + stride, 16, stride);
    copy_block16_mmi(full, src - stride*2 + 1, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(halfV, full_mid, 16, 16);
    avg_pixels16_l2_mmi(dst, halfH, halfV, stride, 16, 16, 16);
}

void ff_avg_h264_qpel16_mc22_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    avg_h264_qpel16_hv_lowpass_mmi(dst, src, stride, stride);
}

void ff_avg_h264_qpel16_mc21_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t halfH[256];
    uint8_t halfHV[256];
    put_h264_qpel16_h_lowpass_mmi(halfH, src, 16, stride);
    put_h264_qpel16_hv_lowpass_mmi(halfHV, src, 16, stride);
    avg_pixels16_l2_mmi(dst, halfH, halfHV, stride, 16, 16, 16);
}

void ff_avg_h264_qpel16_mc23_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t halfH[256];
    uint8_t halfHV[256];
    put_h264_qpel16_h_lowpass_mmi(halfH, src + stride, 16, stride);
    put_h264_qpel16_hv_lowpass_mmi(halfHV, src, 16, stride);
    avg_pixels16_l2_mmi(dst, halfH, halfHV, stride, 16, 16, 16);
}

void ff_avg_h264_qpel16_mc12_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t halfV[256];
    uint8_t halfHV[256];
    copy_block16_mmi(full, src - stride*2, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(halfV, full_mid, 16, 16);
    put_h264_qpel16_hv_lowpass_mmi(halfHV, src, 16, stride);
    avg_pixels16_l2_mmi(dst, halfV, halfHV, stride, 16, 16, 16);
}

void ff_avg_h264_qpel16_mc32_mmi(uint8_t *dst, const uint8_t *src,
        ptrdiff_t stride)
{
    uint8_t full[336];
    uint8_t * const full_mid= full + 32;
    uint8_t halfV[256];
    uint8_t halfHV[256];
    copy_block16_mmi(full, src - stride*2 + 1, 16,  stride, 21);
    put_h264_qpel16_v_lowpass_mmi(halfV, full_mid, 16, 16);
    put_h264_qpel16_hv_lowpass_mmi(halfHV, src, 16, stride);
    avg_pixels16_l2_mmi(dst, halfV, halfHV, stride, 16, 16, 16);
}

#undef op2_avg
#undef op2_put
