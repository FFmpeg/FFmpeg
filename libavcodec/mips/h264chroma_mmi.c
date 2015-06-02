/*
 * Loongson SIMD optimized h264chroma
 *
 * Copyright (c) 2015 Loongson Technology Corporation Limited
 * Copyright (c) 2015 Zhou Xiaoyong <zhouxiaoyong@loongson.cn>
 *                    Zhang Shuangshuang <zhangshuangshuang@ict.ac.cn>
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

#include "h264chroma_mips.h"

void ff_put_h264_chroma_mc8_mmi(uint8_t *dst, uint8_t *src, int stride,
        int h, int x, int y)
{
    const int A = (8 - x) * (8 - y);
    const int B = x * (8 - y);
    const int C = (8 - x) * y;
    const int D = x * y;
    const int E = B + C;
    int i;

    av_assert2(x<8 && y<8 && x>=0 && y>=0);

    if (D) {
        for (i=0; i<h; i++) {
            __asm__ volatile (
                "ldl $2, %2                 \r\n"
                "ldr $2, %1                 \r\n"
                "ldl $3, %4                 \r\n"
                "ldr $3, %3                 \r\n"
                "ldl $4, %6                 \r\n"
                "ldr $4, %5                 \r\n"
                "ldl $5, %8                 \r\n"
                "ldr $5, %7                 \r\n"
                "daddiu $6, $0, 32          \r\n"
                "mtc1 %9, $f6               \r\n"
                "mtc1 %10, $f8              \r\n"
                "mtc1 %11, $f10             \r\n"
                "mtc1 %12, $f12             \r\n"
                "mtc1 $0, $f20              \r\n"
                "mtc1 $2, $f2               \r\n"
                "mtc1 $3, $f4               \r\n"
                "mtc1 $4, $f16              \r\n"
                "mtc1 $5, $f18              \r\n"
                "mtc1 $6, $f14              \r\n"
                "punpcklbh $f2, $f2, $f20   \r\n"
                "punpcklbh $f4, $f4, $f20   \r\n"
                "pshufh $f6, $f6, $f20      \r\n"
                "pshufh $f8, $f8, $f20      \r\n"
                "pshufh $f10, $f10, $f20    \r\n"
                "pshufh $f12, $f12, $f20    \r\n"
                "pshufh $f14, $f14, $f20    \r\n"
                "punpcklbh $f16, $f16, $f20 \r\n"
                "punpcklbh $f18, $f18, $f20 \r\n"
                "daddiu $6, $0, 6           \r\n"
                "mtc1 $6, $f22              \r\n"
                "dsrl32 $2, $2, 0           \r\n"
                "dsrl32 $3, $3, 0           \r\n"
                "dsrl32 $4, $4, 0           \r\n"
                "dsrl32 $5, $5, 0           \r\n"
                "pmullh $f2, $f2, $f6       \r\n"
                "pmullh $f4, $f4, $f8       \r\n"
                "pmullh $f16, $f10, $f16    \r\n"
                "pmullh $f18, $f12, $f18    \r\n"
                "paddh $f2, $f2, $f14       \r\n"
                "paddh $f4, $f4, $f16       \r\n"
                "paddh $f2, $f2, $f18       \r\n"
                "paddh $f2, $f2, $f4        \r\n"
                "psrah $f24, $f2, $f22      \r\n"
                "mtc1 $2, $f2               \r\n"
                "mtc1 $3, $f4               \r\n"
                "mtc1 $4, $f16              \r\n"
                "mtc1 $5, $f18              \r\n"
                "punpcklbh $f2, $f2, $f20   \r\n"
                "punpcklbh $f4, $f4, $f20   \r\n"
                "punpcklbh $f16, $f16, $f20 \r\n"
                "punpcklbh $f18, $f18, $f20 \r\n"
                "pmullh $f2, $f2, $f6       \r\n"
                "pmullh $f4, $f4, $f8       \r\n"
                "pmullh $f16, $f10, $f16    \r\n"
                "pmullh $f18, $f12, $f18    \r\n"
                "paddh $f2, $f2, $f14       \r\n"
                "paddh $f4, $f4, $f16       \r\n"
                "paddh $f2, $f2, $f18       \r\n"
                "paddh $f2, $f2, $f4        \r\n"
                "psrah $f2, $f2, $f22       \r\n"
                "packushb $f2, $f24, $f2    \r\n"
                "sdc1 $f2, %0               \r\n"
                : "=m"(*dst)
                : "m"(*src),"m"(*(src+7)),"m"(*(src+1)),"m"(*(src+8)),
                  "m"(*(src+stride)),"m"(*(src+stride+7)),
                  "m"(*(src+stride+1)),"m"(*(src+stride+8)),
                  "r"(A),"r"(B),"r"(C),"r"(D)
                : "$2","$3","$4","$5","$6"
            );

            dst += stride;
            src += stride;
        }
    } else if (E) {
        const int step = C ? stride : 1;

        for (i=0; i<h; i++) {
            __asm__ volatile (
                "daddiu $6, $0, 32          \r\n"
                "ldl $2, %2                 \r\n"
                "ldr $2, %1                 \r\n"
                "ldl $3, %4                 \r\n"
                "ldr $3, %3                 \r\n"
                "mtc1 $6, $f14              \r\n"
                "mtc1 %5, $f6               \r\n"
                "mtc1 %6, $f8               \r\n"
                "mtc1 $0, $f20              \r\n"
                "mtc1 $2, $f2               \r\n"
                "mtc1 $3, $f4               \r\n"
                "daddiu $6, $0, 6           \r\n"
                "punpcklbh $f2, $f2, $f20   \r\n"
                "punpcklbh $f4, $f4, $f20   \r\n"
                "pshufh $f6, $f6, $f20      \r\n"
                "pshufh $f8, $f8, $f20      \r\n"
                "pshufh $f14, $f14, $f20    \r\n"
                "mtc1 $6, $f22              \r\n"
                "dsrl32 $2, $2, 0           \r\n"
                "dsrl32 $3, $3, 0           \r\n"
                "pmullh $f2, $f2, $f6       \r\n"
                "pmullh $f4, $f4, $f8       \r\n"
                "paddh $f2, $f2, $f14       \r\n"
                "paddh $f2, $f2, $f4        \r\n"
                "psrah $f24, $f2, $f22      \r\n"
                "mtc1 $2, $f2               \r\n"
                "mtc1 $3, $f4               \r\n"
                "punpcklbh $f2, $f2, $f20   \r\n"
                "punpcklbh $f4, $f4, $f20   \r\n"
                "pmullh $f2, $f2, $f6       \r\n"
                "pmullh $f4, $f4, $f8       \r\n"
                "paddh $f2, $f2, $f14       \r\n"
                "paddh $f2, $f2, $f4        \r\n"
                "psrah $f2, $f2, $f22       \r\n"
                "packushb $f2, $f24, $f2    \r\n"
                "sdc1 $f2, %0               \r\n"
                : "=m"(*dst)
                : "m"(*(src)),"m"(*(src+7)),
                  "m"(*(src+step)),"m"(*(src+step+7)),
                  "r"(A),"r"(E)
                : "$2","$3","$4","$5","$6"
            );

            dst += stride;
            src += stride;
        }
    } else {
        for (i = 0; i < h; i++) {
            __asm__ volatile (
                "daddiu $6, $0, 32          \r\n"
                "ldl $2, %2                 \r\n"
                "ldr $2, %1                 \r\n"
                "mtc1 $6, $f14              \r\n"
                "mtc1 %3, $f6               \r\n"
                "mtc1 $0, $f20              \r\n"
                "mtc1 $2, $f2               \r\n"
                "daddiu $6, $0, 6           \r\n"
                "punpcklbh $f2, $f2, $f20   \r\n"
                "pshufh $f6, $f6, $f20      \r\n"
                "pshufh $f14, $f14, $f20    \r\n"
                "mtc1 $6, $f22              \r\n"
                "dsrl32 $2, $2, 0           \r\n"
                "pmullh $f2, $f2, $f6       \r\n"
                "paddh $f2, $f2, $f14       \r\n"
                "psrah $f24, $f2, $f22      \r\n"
                "mtc1 $2, $f2               \r\n"
                "punpcklbh $f2, $f2, $f20   \r\n"
                "pmullh $f2, $f2, $f6       \r\n"
                "paddh $f2, $f2, $f14       \r\n"
                "psrah $f2, $f2, $f22       \r\n"
                "packushb $f2, $f24, $f2    \r\n"
                "sdc1 $f2, %0               \r\n"
                :"=m"(*dst)
                :"m"(*src),"m"(*(src+7)),"r"(A)
                :"$2"
            );

            dst += stride;
            src += stride;
        }
    }
}

void ff_avg_h264_chroma_mc8_mmi(uint8_t *dst, uint8_t *src, int stride,
        int h, int x, int y)
{
    const int A = (8 - x) * (8 - y);
    const int B = x * (8 - y);
    const int C = (8 - x) * y;
    const int D = x * y;
    const int E = B + C;
    int i;

    av_assert2(x<8 && y<8 && x>=0 && y>=0);

    if (D) {
        for (i=0; i<h; i++) {
            __asm__ volatile (
                "ldl $2, %2                 \r\n"
                "ldr $2, %1                 \r\n"
                "ldl $3, %4                 \r\n"
                "ldr $3, %3                 \r\n"
                "ldl $4, %6                 \r\n"
                "ldr $4, %5                 \r\n"
                "ldl $5, %8                 \r\n"
                "ldr $5, %7                 \r\n"
                "daddiu $6, $0, 32          \r\n"
                "mtc1 %9, $f6               \r\n"
                "mtc1 %10, $f8              \r\n"
                "mtc1 %11, $f10             \r\n"
                "mtc1 %12, $f12             \r\n"
                "mtc1 $0, $f20              \r\n"
                "mtc1 $2, $f2               \r\n"
                "mtc1 $3, $f4               \r\n"
                "mtc1 $4, $f16              \r\n"
                "mtc1 $5, $f18              \r\n"
                "mtc1 $6, $f14              \r\n"
                "punpcklbh $f2, $f2, $f20   \r\n"
                "punpcklbh $f4, $f4, $f20   \r\n"
                "pshufh $f6, $f6, $f20      \r\n"
                "pshufh $f8, $f8, $f20      \r\n"
                "pshufh $f10, $f10, $f20    \r\n"
                "pshufh $f12, $f12, $f20    \r\n"
                "pshufh $f14, $f14, $f20    \r\n"
                "punpcklbh $f16, $f16, $f20 \r\n"
                "punpcklbh $f18, $f18, $f20 \r\n"
                "daddiu $6, $0, 6           \r\n"
                "mtc1 $6, $f22              \r\n"
                "dsrl32 $2, $2, 0           \r\n"
                "dsrl32 $3, $3, 0           \r\n"
                "dsrl32 $4, $4, 0           \r\n"
                "dsrl32 $5, $5, 0           \r\n"
                "pmullh $f2, $f2, $f6       \r\n"
                "pmullh $f4, $f4, $f8       \r\n"
                "pmullh $f16, $f10, $f16    \r\n"
                "pmullh $f18, $f12, $f18    \r\n"
                "paddh $f2, $f2, $f14       \r\n"
                "paddh $f4, $f4, $f16       \r\n"
                "paddh $f2, $f2, $f18       \r\n"
                "paddh $f2, $f2, $f4        \r\n"
                "psrah $f24, $f2, $f22      \r\n"
                "mtc1 $2, $f2               \r\n"
                "mtc1 $3, $f4               \r\n"
                "mtc1 $4, $f16              \r\n"
                "mtc1 $5, $f18              \r\n"
                "punpcklbh $f2, $f2, $f20   \r\n"
                "punpcklbh $f4, $f4, $f20   \r\n"
                "punpcklbh $f16, $f16, $f20 \r\n"
                "punpcklbh $f18, $f18, $f20 \r\n"
                "pmullh $f2, $f2, $f6       \r\n"
                "pmullh $f4, $f4, $f8       \r\n"
                "pmullh $f16, $f10, $f16    \r\n"
                "pmullh $f18, $f12, $f18    \r\n"
                "paddh $f2, $f2, $f14       \r\n"
                "paddh $f4, $f4, $f16       \r\n"
                "paddh $f2, $f2, $f18       \r\n"
                "paddh $f2, $f2, $f4        \r\n"
                "psrah $f2, $f2, $f22       \r\n"
                "packushb $f2, $f24, $f2    \r\n"
                "ldc1 $f4, %0               \r\n"
                "pavgb $f2, $f2, $f4        \r\n"
                "sdc1 $f2, %0               \r\n"
                : "=m"(*dst)
                : "m"(*(src)),"m"(*(src+7)),"m"(*(src+1)),"m"(*(src+8)),
                  "m"(*(src+stride)),"m"(*(src+stride+7)),
                  "m"(*(src+stride+1)),"m"(*(src+stride+8)),
                  "r"(A),"r"(B),"r"(C),"r"(D)
                : "$2","$3","$4","$5","$6"
            );

            dst += stride;
            src += stride;
        }
    } else {
        const int step = C ? stride : 1;

        for (i=0; i<h; i++) {
            __asm__ volatile (
                "daddiu $6, $0, 32          \r\n"
                "ldl $2, %2                 \r\n"
                "ldr $2, %1                 \r\n"
                "ldl $3, %4                 \r\n"
                "ldr $3, %3                 \r\n"
                "mtc1 $6, $f14              \r\n"
                "mtc1 %5, $f6               \r\n"
                "mtc1 %6, $f8               \r\n"
                "mtc1 $0, $f20              \r\n"
                "mtc1 $2, $f2               \r\n"
                "mtc1 $3, $f4               \r\n"
                "daddiu $6, $0, 6           \r\n"
                "punpcklbh $f2, $f2, $f20   \r\n"
                "punpcklbh $f4, $f4, $f20   \r\n"
                "pshufh $f6, $f6, $f20      \r\n"
                "pshufh $f8, $f8, $f20      \r\n"
                "pshufh $f14, $f14, $f20    \r\n"
                "mtc1 $6, $f22              \r\n"
                "dsrl32 $2, $2, 0           \r\n"
                "dsrl32 $3, $3, 0           \r\n"
                "pmullh $f2, $f2, $f6       \r\n"
                "pmullh $f4, $f4, $f8       \r\n"
                "paddh $f2, $f2, $f14       \r\n"
                "paddh $f2, $f2, $f4        \r\n"
                "psrah $f24, $f2, $f22      \r\n"
                "mtc1 $2, $f2               \r\n"
                "mtc1 $3, $f4               \r\n"
                "punpcklbh $f2, $f2, $f20   \r\n"
                "punpcklbh $f4, $f4, $f20   \r\n"
                "pmullh $f2, $f2, $f6       \r\n"
                "pmullh $f4, $f4, $f8       \r\n"
                "paddh $f2, $f2, $f14       \r\n"
                "paddh $f2, $f2, $f4        \r\n"
                "psrah $f2, $f2, $f22       \r\n"
                "packushb $f2, $f24, $f2    \r\n"
                "ldc1 $f4, %0               \r\n"
                "pavgb $f2, $f2, $f4        \r\n"
                "sdc1 $f2, %0               \r\n"
                : "=m"(*dst)
                : "m"(*(src)),"m"(*(src+7)),
                  "m"(*(src+step)),"m"(*(src+step+7)),"r"(A),"r"(E)
                : "$2","$3","$4","$5","$6"
            );

            dst += stride;
            src += stride;
        }
    }
}

void ff_put_h264_chroma_mc4_mmi(uint8_t *dst, uint8_t *src, int stride,
        int h, int x, int y)
{
    const int A = (8 - x) * (8 - y);
    const int B = x * (8 - y);
    const int C = (8 - x) *  y;
    const int D = x *  y;
    const int E = B + C;
    int i;

    av_assert2(x<8 && y<8 && x>=0 && y>=0);

    if (D) {
        for (i=0; i<h; i++) {
            __asm__ volatile (
                "ldl $2, %2                 \r\n"
                "ldr $2, %1                 \r\n"
                "ldl $3, %4                 \r\n"
                "ldr $3, %3                 \r\n"
                "ldl $4, %6                 \r\n"
                "ldr $4, %5                 \r\n"
                "ldl $5, %8                 \r\n"
                "ldr $5, %7                 \r\n"
                "daddiu $6, $0, 32          \r\n"
                "mtc1 %9, $f6               \r\n"
                "mtc1 %10, $f8              \r\n"
                "mtc1 %11, $f10             \r\n"
                "mtc1 %12, $f12             \r\n"
                "mtc1 $0, $f20              \r\n"
                "mtc1 $2, $f2               \r\n"
                "mtc1 $3, $f4               \r\n"
                "mtc1 $4, $f16              \r\n"
                "mtc1 $5, $f18              \r\n"
                "mtc1 $6, $f14              \r\n"
                "punpcklbh $f2, $f2, $f20   \r\n"
                "punpcklbh $f4, $f4, $f20   \r\n"
                "pshufh $f6, $f6, $f20      \r\n"
                "pshufh $f8, $f8, $f20      \r\n"
                "pshufh $f10, $f10, $f20    \r\n"
                "pshufh $f12, $f12, $f20    \r\n"
                "pshufh $f14, $f14, $f20    \r\n"
                "punpcklbh $f16, $f16, $f20 \r\n"
                "punpcklbh $f18, $f18, $f20 \r\n"
                "daddiu $6, $0, 6           \r\n"
                "mtc1 $6, $f22              \r\n"
                "pmullh $f2, $f2, $f6       \r\n"
                "pmullh $f4, $f4, $f8       \r\n"
                "pmullh $f16, $f10, $f16    \r\n"
                "pmullh $f18, $f12, $f18    \r\n"
                "paddh $f2, $f2, $f14       \r\n"
                "paddh $f4, $f4, $f16       \r\n"
                "paddh $f2, $f2, $f18       \r\n"
                "paddh $f2, $f2, $f4        \r\n"
                "psrah $f2, $f2, $f22       \r\n"
                "packushb $f2, $f2, $f2     \r\n"
                "swc1 $f2, %0               \r\n"
                : "=m"(*dst)
                : "m"(*(src)),"m"(*(src+7)),"m"(*(src+1)),"m"(*(src+8)),
                  "m"(*(src+stride)),"m"(*(src+stride+7)),
                  "m"(*(src+stride+1)),"m"(*(src+stride+8)),
                  "r"(A),"r"(B),"r"(C),"r"(D)
                : "$2","$3","$4","$5","$6"
            );

            dst += stride;
            src += stride;
        }
    } else if (E) {
        const int step = C ? stride : 1;

        for (i=0; i<h; i++) {
            __asm__ volatile (
                "ldl $2, %2                 \r\n"
                "ldr $2, %1                 \r\n"
                "ldl $3, %4                 \r\n"
                "ldr $3, %3                 \r\n"
                "daddiu $4, $0, 32          \r\n"
                "mtc1 %5, $f6               \r\n"
                "mtc1 %6, $f8               \r\n"
                "mtc1 $0, $f20              \r\n"
                "mtc1 $2, $f2               \r\n"
                "mtc1 $3, $f4               \r\n"
                "mtc1 $4, $f10              \r\n"
                "punpcklbh $f2, $f2, $f20   \r\n"
                "punpcklbh $f4, $f4, $f20   \r\n"
                "pshufh $f6, $f6, $f20      \r\n"
                "pshufh $f8, $f8, $f20      \r\n"
                "pshufh $f10, $f10, $f20    \r\n"
                "daddiu $4, $0, 6           \r\n"
                "mtc1 $4, $f22              \r\n"
                "pmullh $f2, $f2, $f6       \r\n"
                "pmullh $f4, $f4, $f8       \r\n"
                "paddh $f2, $f2, $f10       \r\n"
                "paddh $f2, $f2, $f4        \r\n"
                "psrah $f2, $f2, $f22       \r\n"
                "packushb $f2, $f2, $f20    \r\n"
                "swc1 $f2, %0               \r\n"
                : "=m"(*dst)
                : "m"(*(src)),"m"(*(src+7)),"m"(*(src+step)),
                  "m"(*(src+step+7)),"r"(A),"r"(E)
                : "$2","$3","$4","$5","$6"
            );

            dst += stride;
            src += stride;
        }
    } else {
        for (i=0; i<h; i++) {
            __asm__ volatile (
                "lwl $2, %2                 \r\n"
                "lwr $2, %1                 \r\n"
                "sw $2, %0                  \r\n"
                : "=m"(*dst)
                : "m"(*src),"m"(*(src+3))
                : "$2"
            );

            dst += stride;
            src += stride;
        }
    }
}

void ff_avg_h264_chroma_mc4_mmi(uint8_t *dst, uint8_t *src, int stride,
        int h, int x, int y)
{
    const int A = (8 - x) *(8 - y);
    const int B = x * (8 - y);
    const int C = (8 - x) * y;
    const int D = x * y;
    int i;

    av_assert2(x<8 && y<8 && x>=0 && y>=0);

    if (D) {
        for (i=0; i<h; i++) {
            __asm__ volatile (
                "ldl $2, %2                 \r\n"
                "ldr $2, %1                 \r\n"
                "ldl $3, %4                 \r\n"
                "ldr $3, %3                 \r\n"
                "ldl $4, %6                 \r\n"
                "ldr $4, %5                 \r\n"
                "ldl $5, %8                 \r\n"
                "ldr $5, %7                 \r\n"
                "daddiu $6, $0, 32          \r\n"
                "mtc1 %9, $f6               \r\n"
                "mtc1 %10, $f8              \r\n"
                "mtc1 %11, $f10             \r\n"
                "mtc1 %12, $f12             \r\n"
                "mtc1 $0, $f20              \r\n"
                "mtc1 $2, $f2               \r\n"
                "mtc1 $3, $f4               \r\n"
                "mtc1 $4, $f16              \r\n"
                "mtc1 $5, $f18              \r\n"
                "mtc1 $6, $f14              \r\n"
                "punpcklbh $f2, $f2, $f20   \r\n"
                "punpcklbh $f4, $f4, $f20   \r\n"
                "pshufh $f6, $f6, $f20      \r\n"
                "pshufh $f8, $f8, $f20      \r\n"
                "pshufh $f10, $f10, $f20    \r\n"
                "pshufh $f12, $f12, $f20    \r\n"
                "pshufh $f14, $f14, $f20    \r\n"
                "punpcklbh $f16, $f16, $f20 \r\n"
                "punpcklbh $f18, $f18, $f20 \r\n"
                "daddiu $6, $0, 6           \r\n"
                "mtc1 $6, $f22              \r\n"
                "pmullh $f2, $f2, $f6       \r\n"
                "pmullh $f4, $f4, $f8       \r\n"
                "pmullh $f16, $f10, $f16    \r\n"
                "pmullh $f18, $f12, $f18    \r\n"
                "paddh $f2, $f2, $f14       \r\n"
                "paddh $f4, $f4, $f16       \r\n"
                "paddh $f2, $f2, $f18       \r\n"
                "paddh $f2, $f2, $f4        \r\n"
                "psrah $f2, $f2, $f22       \r\n"
                "packushb $f2, $f2, $f2     \r\n"
                "lwc1 $f4, %0               \r\n"
                "pavgb $f2, $f2, $f4        \r\n"
                "swc1 $f2, %0               \r\n"
                : "=m"(*dst)
                : "m"(*(src)),"m"(*(src+7)),"m"(*(src+1)),"m"(*(src+8)),
                  "m"(*(src+stride)),"m"(*(src+stride+7)),
                  "m"(*(src+stride+1)),"m"(*(src+stride+8)),
                  "r"(A),"r"(B),"r"(C),"r"(D)
                : "$2","$3","$4","$5","$6"
            );

            dst += stride;
            src += stride;
        }
    } else {
        const int E = B + C;
        const int step = C ? stride : 1;

        for (i=0; i<h; i++) {
            __asm__ volatile (
                "ldl $2, %2                 \r\n"
                "ldr $2, %1                 \r\n"
                "ldl $3, %4                 \r\n"
                "ldr $3, %3                 \r\n"
                "daddiu $4, $0, 32          \r\n"
                "mtc1 %5, $f6               \r\n"
                "mtc1 %6, $f8               \r\n"
                "mtc1 $0, $f20              \r\n"
                "mtc1 $2, $f2               \r\n"
                "mtc1 $3, $f4               \r\n"
                "mtc1 $4, $f10              \r\n"
                "punpcklbh $f2, $f2, $f20   \r\n"
                "punpcklbh $f4, $f4, $f20   \r\n"
                "pshufh $f6, $f6, $f20      \r\n"
                "pshufh $f8, $f8, $f20      \r\n"
                "pshufh $f10, $f10, $f20    \r\n"
                "daddiu $4, $0, 6           \r\n"
                "mtc1 $4, $f22              \r\n"
                "pmullh $f2, $f2, $f6       \r\n"
                "pmullh $f4, $f4, $f8       \r\n"
                "paddh $f2, $f2, $f10       \r\n"
                "paddh $f2, $f2, $f4        \r\n"
                "psrah $f2, $f2, $f22       \r\n"
                "packushb $f2, $f2, $f20    \r\n"
                "lwc1 $f4, %0               \r\n"
                "pavgb $f2, $f2, $f4        \r\n"
                "swc1 $f2, %0               \r\n"
                : "=m"(*dst)
                : "m"(*(src)),"m"(*(src+7)),"m"(*(src+step)),
                  "m"(*(src+step+7)),"r"(A),"r"(E)
                : "$2","$3","$4","$5","$6"
            );

            dst += stride;
            src += stride;
        }
    }
}
