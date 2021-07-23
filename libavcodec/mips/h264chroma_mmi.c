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
#include "constants.h"
#include "libavutil/mips/mmiutils.h"

void ff_put_h264_chroma_mc8_mmi(uint8_t *dst, uint8_t *src, ptrdiff_t stride,
        int h, int x, int y)
{
    double ftmp[12];
    union mmi_intfloat64 A, B, C, D, E;
    DECLARE_VAR_ALL64;

    A.i = 64;

    if (!(x || y)) {
        /* x=0, y=0, A.i=64 */
        __asm__ volatile (
            "1:                                                        \n\t"
            MMI_ULDC1(%[ftmp0], %[src], 0x00)
            PTR_ADDU   "%[src],     %[src],         %[stride]          \n\t"
            MMI_ULDC1(%[ftmp1], %[src], 0x00)
            PTR_ADDU   "%[src],     %[src],         %[stride]          \n\t"
            MMI_ULDC1(%[ftmp2], %[src], 0x00)
            PTR_ADDU   "%[src],     %[src],         %[stride]          \n\t"
            MMI_ULDC1(%[ftmp3], %[src], 0x00)
            PTR_ADDU   "%[src],     %[src],         %[stride]          \n\t"

            "addi       %[h],       %[h],           -0x04              \n\t"

            MMI_SDC1(%[ftmp0], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]          \n\t"
            MMI_SDC1(%[ftmp1], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]          \n\t"
            MMI_SDC1(%[ftmp2], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]          \n\t"
            MMI_SDC1(%[ftmp3], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]          \n\t"
            "bnez       %[h],       1b                                 \n\t"
            : RESTRICT_ASM_ALL64
              [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride)
            : "memory"
        );
    } else if (x && y) {
        /* x!=0, y!=0 */
        D.i = x * y;
        B.i = (x << 3) - D.i;
        C.i = (y << 3) - D.i;
        A.i = 64 - D.i - B.i - C.i;

        __asm__ volatile (
            "pxor       %[ftmp0],   %[ftmp0],       %[ftmp0]           \n\t"
            "pshufh     %[A],       %[A],           %[ftmp0]           \n\t"
            "pshufh     %[B],       %[B],           %[ftmp0]           \n\t"
            "mtc1       %[tmp0],    %[ftmp9]                           \n\t"
            "pshufh     %[C],       %[C],           %[ftmp0]           \n\t"
            "pshufh     %[D],       %[D],           %[ftmp0]           \n\t"

            "1:                                                        \n\t"
            MMI_ULDC1(%[ftmp1], %[src], 0x00)
            MMI_ULDC1(%[ftmp2], %[src], 0x01)
            PTR_ADDU   "%[src],     %[src],         %[stride]          \n\t"
            MMI_ULDC1(%[ftmp3], %[src], 0x00)
            MMI_ULDC1(%[ftmp4], %[src], 0x01)
            PTR_ADDU   "%[src],     %[src],         %[stride]          \n\t"
            MMI_ULDC1(%[ftmp10], %[src], 0x00)
            MMI_ULDC1(%[ftmp11], %[src], 0x01)
            "addi       %[h],       %[h],           -0x02              \n\t"

            "punpcklbh  %[ftmp5],   %[ftmp1],       %[ftmp0]           \n\t"
            "punpckhbh  %[ftmp6],   %[ftmp1],       %[ftmp0]           \n\t"
            "punpcklbh  %[ftmp7],   %[ftmp2],       %[ftmp0]           \n\t"
            "punpckhbh  %[ftmp8],   %[ftmp2],       %[ftmp0]           \n\t"
            "pmullh     %[ftmp5],   %[ftmp5],       %[A]               \n\t"
            "pmullh     %[ftmp7],   %[ftmp7],       %[B]               \n\t"
            "paddh      %[ftmp1],   %[ftmp5],       %[ftmp7]           \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[A]               \n\t"
            "pmullh     %[ftmp8],   %[ftmp8],       %[B]               \n\t"
            "paddh      %[ftmp2],   %[ftmp6],       %[ftmp8]           \n\t"
            "punpcklbh  %[ftmp5],   %[ftmp3],       %[ftmp0]           \n\t"
            "punpckhbh  %[ftmp6],   %[ftmp3],       %[ftmp0]           \n\t"
            "punpcklbh  %[ftmp7],   %[ftmp4],       %[ftmp0]           \n\t"
            "punpckhbh  %[ftmp8],   %[ftmp4],       %[ftmp0]           \n\t"
            "pmullh     %[ftmp5],   %[ftmp5],       %[C]               \n\t"
            "pmullh     %[ftmp7],   %[ftmp7],       %[D]               \n\t"
            "paddh      %[ftmp5],   %[ftmp5],       %[ftmp7]           \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[C]               \n\t"
            "pmullh     %[ftmp8],   %[ftmp8],       %[D]               \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp8]           \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ftmp5]           \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ftmp6]           \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_32]        \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_32]        \n\t"
            "psrlh      %[ftmp1],   %[ftmp1],       %[ftmp9]           \n\t"
            "psrlh      %[ftmp2],   %[ftmp2],       %[ftmp9]           \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp2]           \n\t"

            "punpcklbh  %[ftmp5],   %[ftmp3],       %[ftmp0]           \n\t"
            "punpckhbh  %[ftmp6],   %[ftmp3],       %[ftmp0]           \n\t"
            "punpcklbh  %[ftmp7],   %[ftmp4],       %[ftmp0]           \n\t"
            "punpckhbh  %[ftmp8],   %[ftmp4],       %[ftmp0]           \n\t"
            "pmullh     %[ftmp5],   %[ftmp5],       %[A]               \n\t"
            "pmullh     %[ftmp7],   %[ftmp7],       %[B]               \n\t"
            "paddh      %[ftmp3],   %[ftmp5],       %[ftmp7]           \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[A]               \n\t"
            "pmullh     %[ftmp8],   %[ftmp8],       %[B]               \n\t"
            "paddh      %[ftmp4],   %[ftmp6],       %[ftmp8]           \n\t"
            "punpcklbh  %[ftmp5],   %[ftmp10],      %[ftmp0]           \n\t"
            "punpckhbh  %[ftmp6],   %[ftmp10],      %[ftmp0]           \n\t"
            "punpcklbh  %[ftmp7],   %[ftmp11],      %[ftmp0]           \n\t"
            "punpckhbh  %[ftmp8],   %[ftmp11],      %[ftmp0]           \n\t"
            "pmullh     %[ftmp5],   %[ftmp5],       %[C]               \n\t"
            "pmullh     %[ftmp7],   %[ftmp7],       %[D]               \n\t"
            "paddh      %[ftmp5],   %[ftmp5],       %[ftmp7]           \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[C]               \n\t"
            "pmullh     %[ftmp8],   %[ftmp8],       %[D]               \n\t"
            "paddh      %[ftmp6],   %[ftmp6],       %[ftmp8]           \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ftmp5]           \n\t"
            "paddh      %[ftmp4],   %[ftmp4],       %[ftmp6]           \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ff_pw_32]        \n\t"
            "paddh      %[ftmp4],   %[ftmp4],       %[ff_pw_32]        \n\t"
            "psrlh      %[ftmp3],   %[ftmp3],       %[ftmp9]           \n\t"
            "psrlh      %[ftmp4],   %[ftmp4],       %[ftmp9]           \n\t"
            "packushb   %[ftmp3],   %[ftmp3],       %[ftmp4]           \n\t"

            MMI_SDC1(%[ftmp1], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]          \n\t"
            MMI_SDC1(%[ftmp3], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]          \n\t"
            "bnez       %[h],       1b                                 \n\t"
            : RESTRICT_ASM_ALL64
              [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
              [ftmp8]"=&f"(ftmp[8]),        [ftmp9]"=&f"(ftmp[9]),
              [ftmp10]"=&f"(ftmp[10]),      [ftmp11]"=&f"(ftmp[11]),
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride),[ff_pw_32]"f"(ff_pw_32.f),
              [A]"f"(A.f),                  [B]"f"(B.f),
              [C]"f"(C.f),                  [D]"f"(D.f),
              [tmp0]"r"(0x06)
            : "memory"
        );
    } else if (x) {
        /* x!=0, y==0 */
        E.i = x << 3;
        A.i = 64 - E.i;

        __asm__ volatile (
            "pxor       %[ftmp0],   %[ftmp0],       %[ftmp0]           \n\t"
            "pshufh     %[A],       %[A],           %[ftmp0]           \n\t"
            "pshufh     %[E],       %[E],           %[ftmp0]           \n\t"
            "mtc1       %[tmp0],    %[ftmp7]                           \n\t"

            "1:                                                        \n\t"
            MMI_ULDC1(%[ftmp1], %[src], 0x00)
            MMI_ULDC1(%[ftmp2], %[src], 0x01)
            "addi       %[h],       %[h],           -0x01              \n\t"
            PTR_ADDU   "%[src],     %[src],         %[stride]          \n\t"

            "punpcklbh  %[ftmp3],   %[ftmp1],       %[ftmp0]           \n\t"
            "punpckhbh  %[ftmp4],   %[ftmp1],       %[ftmp0]           \n\t"
            "punpcklbh  %[ftmp5],   %[ftmp2],       %[ftmp0]           \n\t"
            "punpckhbh  %[ftmp6],   %[ftmp2],       %[ftmp0]           \n\t"
            "pmullh     %[ftmp3],   %[ftmp3],       %[A]               \n\t"
            "pmullh     %[ftmp5],   %[ftmp5],       %[E]               \n\t"
            "paddh      %[ftmp1],   %[ftmp3],       %[ftmp5]           \n\t"
            "pmullh     %[ftmp4],   %[ftmp4],       %[A]               \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[E]               \n\t"
            "paddh      %[ftmp2],   %[ftmp4],       %[ftmp6]           \n\t"

            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_32]        \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_32]        \n\t"
            "psrlh      %[ftmp1],   %[ftmp1],       %[ftmp7]           \n\t"
            "psrlh      %[ftmp2],   %[ftmp2],       %[ftmp7]           \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp2]           \n\t"
            MMI_SDC1(%[ftmp1], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]          \n\t"
            "bnez       %[h],       1b                                 \n\t"
            : RESTRICT_ASM_ALL64
              [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride),
              [ff_pw_32]"f"(ff_pw_32.f),    [tmp0]"r"(0x06),
              [A]"f"(A.f),                  [E]"f"(E.f)
            : "memory"
        );
    } else {
        /* x==0, y!=0 */
        E.i = y << 3;
        A.i = 64 - E.i;

        __asm__ volatile (
            "pxor       %[ftmp0],   %[ftmp0],       %[ftmp0]           \n\t"
            "pshufh     %[A],       %[A],           %[ftmp0]           \n\t"
            "pshufh     %[E],       %[E],           %[ftmp0]           \n\t"
            "mtc1       %[tmp0],    %[ftmp7]                           \n\t"

            "1:                                                        \n\t"
            MMI_ULDC1(%[ftmp1], %[src], 0x00)
            PTR_ADDU   "%[src],     %[src],         %[stride]          \n\t"
            MMI_ULDC1(%[ftmp2], %[src], 0x00)
            PTR_ADDU   "%[src],     %[src],         %[stride]          \n\t"
            MMI_ULDC1(%[ftmp8], %[src], 0x00)
            "addi       %[h],       %[h],           -0x02              \n\t"

            "punpcklbh  %[ftmp3],   %[ftmp1],       %[ftmp0]           \n\t"
            "punpckhbh  %[ftmp4],   %[ftmp1],       %[ftmp0]           \n\t"
            "punpcklbh  %[ftmp5],   %[ftmp2],       %[ftmp0]           \n\t"
            "punpckhbh  %[ftmp6],   %[ftmp2],       %[ftmp0]           \n\t"
            "pmullh     %[ftmp3],   %[ftmp3],       %[A]               \n\t"
            "pmullh     %[ftmp5],   %[ftmp5],       %[E]               \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ftmp5]           \n\t"
            "pmullh     %[ftmp4],   %[ftmp4],       %[A]               \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[E]               \n\t"
            "paddh      %[ftmp4],   %[ftmp4],       %[ftmp6]           \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ff_pw_32]        \n\t"
            "paddh      %[ftmp4],   %[ftmp4],       %[ff_pw_32]        \n\t"
            "psrlh      %[ftmp3],   %[ftmp3],       %[ftmp7]           \n\t"
            "psrlh      %[ftmp4],   %[ftmp4],       %[ftmp7]           \n\t"
            "packushb   %[ftmp1],   %[ftmp3],       %[ftmp4]           \n\t"

            "punpcklbh  %[ftmp3],   %[ftmp2],       %[ftmp0]           \n\t"
            "punpckhbh  %[ftmp4],   %[ftmp2],       %[ftmp0]           \n\t"
            "punpcklbh  %[ftmp5],   %[ftmp8],       %[ftmp0]           \n\t"
            "punpckhbh  %[ftmp6],   %[ftmp8],       %[ftmp0]           \n\t"
            "pmullh     %[ftmp3],   %[ftmp3],       %[A]               \n\t"
            "pmullh     %[ftmp5],   %[ftmp5],       %[E]               \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ftmp5]           \n\t"
            "pmullh     %[ftmp4],   %[ftmp4],       %[A]               \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[E]               \n\t"
            "paddh      %[ftmp4],   %[ftmp4],       %[ftmp6]           \n\t"
            "paddh      %[ftmp3],   %[ftmp3],       %[ff_pw_32]        \n\t"
            "paddh      %[ftmp4],   %[ftmp4],       %[ff_pw_32]        \n\t"
            "psrlh      %[ftmp3],   %[ftmp3],       %[ftmp7]           \n\t"
            "psrlh      %[ftmp4],   %[ftmp4],       %[ftmp7]           \n\t"
            "packushb   %[ftmp2],   %[ftmp3],       %[ftmp4]           \n\t"

            MMI_SDC1(%[ftmp1], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]          \n\t"
            MMI_SDC1(%[ftmp2], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]          \n\t"
            "bnez       %[h],       1b                                 \n\t"
            : RESTRICT_ASM_ALL64
              [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
              [ftmp8]"=&f"(ftmp[8]),
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride),
              [ff_pw_32]"f"(ff_pw_32.f),    [A]"f"(A.f),
              [E]"f"(E.f),                  [tmp0]"r"(0x06)
            : "memory"
        );
    }
}

void ff_avg_h264_chroma_mc8_mmi(uint8_t *dst, uint8_t *src, ptrdiff_t stride,
        int h, int x, int y)
{
    double ftmp[10];
    union mmi_intfloat64 A, B, C, D, E;
    DECLARE_VAR_ALL64;

    A.i = 64;

    if(!(x || y)){
        /* x=0, y=0, A.i=64 */
        __asm__ volatile (
            "1:                                                         \n\t"
            MMI_ULDC1(%[ftmp0], %[src], 0x00)
            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            MMI_ULDC1(%[ftmp1], %[src], 0x00)
            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            MMI_LDC1(%[ftmp2], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            MMI_LDC1(%[ftmp3], %[dst], 0x00)
            PTR_SUBU   "%[dst],     %[dst],         %[stride]           \n\t"
            "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp2]            \n\t"
            "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp3]            \n\t"
            MMI_SDC1(%[ftmp0], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            MMI_SDC1(%[ftmp1], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            "addi       %[h],       %[h],           -0x02               \n\t"
            "bnez       %[h],       1b                                  \n\t"
            : RESTRICT_ASM_ALL64
              [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride)
            : "memory"
        );
    } else if (x && y) {
        /* x!=0, y!=0 */
        D.i = x * y;
        B.i = (x << 3) - D.i;
        C.i = (y << 3) - D.i;
        A.i = 64 - D.i - B.i - C.i;
        __asm__ volatile (
            "pxor       %[ftmp0],   %[ftmp0],       %[ftmp0]       \n\t"
            "pshufh     %[A],       %[A],           %[ftmp0]       \n\t"
            "pshufh     %[B],       %[B],           %[ftmp0]       \n\t"
            "mtc1       %[tmp0],    %[ftmp9]                       \n\t"
            "pshufh     %[C],       %[C],           %[ftmp0]       \n\t"
            "pshufh     %[D],       %[D],           %[ftmp0]       \n\t"

            "1:                                                    \n\t"
            MMI_ULDC1(%[ftmp1], %[src], 0x00)
            MMI_ULDC1(%[ftmp2], %[src], 0x01)
            PTR_ADDU   "%[src],     %[src],         %[stride]      \n\t"
            MMI_ULDC1(%[ftmp3], %[src], 0x00)
            MMI_ULDC1(%[ftmp4], %[src], 0x01)
            "addi       %[h],       %[h],           -0x01          \n\t"

            "punpcklbh  %[ftmp5],   %[ftmp1],       %[ftmp0]       \n\t"
            "punpckhbh  %[ftmp6],   %[ftmp1],       %[ftmp0]       \n\t"
            "punpcklbh  %[ftmp7],   %[ftmp2],       %[ftmp0]       \n\t"
            "punpckhbh  %[ftmp8],   %[ftmp2],       %[ftmp0]       \n\t"
            "pmullh     %[ftmp5],   %[ftmp5],       %[A]           \n\t"
            "pmullh     %[ftmp7],   %[ftmp7],       %[B]           \n\t"
            "paddh      %[ftmp1],   %[ftmp5],       %[ftmp7]       \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[A]           \n\t"
            "pmullh     %[ftmp8],   %[ftmp8],       %[B]           \n\t"
            "paddh      %[ftmp2],   %[ftmp6],       %[ftmp8]       \n\t"

            "punpcklbh  %[ftmp5],   %[ftmp3],       %[ftmp0]       \n\t"
            "punpckhbh  %[ftmp6],   %[ftmp3],       %[ftmp0]       \n\t"
            "punpcklbh  %[ftmp7],   %[ftmp4],       %[ftmp0]       \n\t"
            "punpckhbh  %[ftmp8],   %[ftmp4],       %[ftmp0]       \n\t"
            "pmullh     %[ftmp5],   %[ftmp5],       %[C]           \n\t"
            "pmullh     %[ftmp7],   %[ftmp7],       %[D]           \n\t"
            "paddh      %[ftmp3],   %[ftmp5],       %[ftmp7]       \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[C]           \n\t"
            "pmullh     %[ftmp8],   %[ftmp8],       %[D]           \n\t"
            "paddh      %[ftmp4],   %[ftmp6],       %[ftmp8]       \n\t"

            "paddh      %[ftmp1],   %[ftmp1],       %[ftmp3]       \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ftmp4]       \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_32]    \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_32]    \n\t"
            "psrlh      %[ftmp1],   %[ftmp1],       %[ftmp9]       \n\t"
            "psrlh      %[ftmp2],   %[ftmp2],       %[ftmp9]       \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp2]       \n\t"
            MMI_LDC1(%[ftmp2], %[dst], 0x00)
            "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp2]       \n\t"
            MMI_SDC1(%[ftmp1], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]      \n\t"
            "bnez       %[h],       1b                             \n\t"
            : RESTRICT_ASM_ALL64
              [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
              [ftmp8]"=&f"(ftmp[8]),        [ftmp9]"=&f"(ftmp[9]),
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride),[ff_pw_32]"f"(ff_pw_32.f),
              [A]"f"(A.f),                  [B]"f"(B.f),
              [C]"f"(C.f),                  [D]"f"(D.f),
              [tmp0]"r"(0x06)
            : "memory"
        );
    } else if (x) {
        /* x!=0, y==0 */
        E.i = x << 3;
        A.i = 64 - E.i;
        __asm__ volatile (
            "pxor       %[ftmp0],   %[ftmp0],       %[ftmp0]       \n\t"
            "pshufh     %[A],       %[A],           %[ftmp0]       \n\t"
            "pshufh     %[E],       %[E],           %[ftmp0]       \n\t"
            "mtc1       %[tmp0],    %[ftmp7]                       \n\t"

            "1:                                                    \n\t"
            MMI_ULDC1(%[ftmp1], %[src], 0x00)
            MMI_ULDC1(%[ftmp2], %[src], 0x01)
            PTR_ADDU   "%[src],     %[src],         %[stride]      \n\t"
            "addi       %[h],       %[h],           -0x01          \n\t"

            "punpcklbh  %[ftmp3],   %[ftmp1],       %[ftmp0]       \n\t"
            "punpckhbh  %[ftmp4],   %[ftmp1],       %[ftmp0]       \n\t"
            "punpcklbh  %[ftmp5],   %[ftmp2],       %[ftmp0]       \n\t"
            "punpckhbh  %[ftmp6],   %[ftmp2],       %[ftmp0]       \n\t"
            "pmullh     %[ftmp3],   %[ftmp3],       %[A]           \n\t"
            "pmullh     %[ftmp5],   %[ftmp5],       %[E]           \n\t"
            "paddh      %[ftmp1],   %[ftmp3],       %[ftmp5]       \n\t"
            "pmullh     %[ftmp4],   %[ftmp4],       %[A]           \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[E]           \n\t"
            "paddh      %[ftmp2],   %[ftmp4],       %[ftmp6]       \n\t"

            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_32]    \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_32]    \n\t"
            "psrlh      %[ftmp1],   %[ftmp1],       %[ftmp7]       \n\t"
            "psrlh      %[ftmp2],   %[ftmp2],       %[ftmp7]       \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp2]       \n\t"
            MMI_LDC1(%[ftmp2], %[dst], 0x00)
            "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp2]       \n\t"
            MMI_SDC1(%[ftmp1], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]      \n\t"
            "bnez       %[h],       1b                             \n\t"
            : RESTRICT_ASM_ALL64
              [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride),
              [ff_pw_32]"f"(ff_pw_32.f),    [tmp0]"r"(0x06),
              [A]"f"(A.f),                  [E]"f"(E.f)
            : "memory"
        );
    } else {
        /* x==0, y!=0 */
        E.i = y << 3;
        A.i = 64 - E.i;
        __asm__ volatile (
            "pxor       %[ftmp0],   %[ftmp0],       %[ftmp0]       \n\t"
            "pshufh     %[A],       %[A],           %[ftmp0]       \n\t"
            "pshufh     %[E],       %[E],           %[ftmp0]       \n\t"
            "mtc1       %[tmp0],    %[ftmp7]                       \n\t"

            "1:                                                    \n\t"
            MMI_ULDC1(%[ftmp1], %[src], 0x00)
            PTR_ADDU   "%[src],     %[src],         %[stride]      \n\t"
            MMI_ULDC1(%[ftmp2], %[src], 0x00)
            "addi       %[h],       %[h],           -0x01          \n\t"

            "punpcklbh  %[ftmp3],   %[ftmp1],       %[ftmp0]       \n\t"
            "punpckhbh  %[ftmp4],   %[ftmp1],       %[ftmp0]       \n\t"
            "punpcklbh  %[ftmp5],   %[ftmp2],       %[ftmp0]       \n\t"
            "punpckhbh  %[ftmp6],   %[ftmp2],       %[ftmp0]       \n\t"
            "pmullh     %[ftmp3],   %[ftmp3],       %[A]           \n\t"
            "pmullh     %[ftmp5],   %[ftmp5],       %[E]           \n\t"
            "paddh      %[ftmp1],   %[ftmp3],       %[ftmp5]       \n\t"
            "pmullh     %[ftmp4],   %[ftmp4],       %[A]           \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[E]           \n\t"
            "paddh      %[ftmp2],   %[ftmp4],       %[ftmp6]       \n\t"

            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_32]  \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_32]  \n\t"
            "psrlh      %[ftmp1],   %[ftmp1],       %[ftmp7]       \n\t"
            "psrlh      %[ftmp2],   %[ftmp2],       %[ftmp7]       \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp2]       \n\t"
            MMI_LDC1(%[ftmp2], %[dst], 0x00)
            "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp2]       \n\t"
            MMI_SDC1(%[ftmp1], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]      \n\t"
            "bnez       %[h],       1b                             \n\t"
            : RESTRICT_ASM_ALL64
              [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride),
              [ff_pw_32]"f"(ff_pw_32.f),    [tmp0]"r"(0x06),
              [A]"f"(A.f),                  [E]"f"(E.f)
            : "memory"
        );
    }
}

void ff_put_h264_chroma_mc4_mmi(uint8_t *dst, uint8_t *src, ptrdiff_t stride,
        int h, int x, int y)
{
    double ftmp[8];
    mips_reg addr[1];
    union mmi_intfloat64 A, B, C, D, E;
    DECLARE_VAR_LOW32;
    A.i = (8 - x) * (8 - y);
    B.i = x * (8 - y);
    C.i = (8 - x) * y;
    D.i = x * y;
    E.i = B.i + C.i;

    if (D.i) {
        __asm__ volatile (
            "pxor       %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
            "pshufh     %[A],       %[A],           %[ftmp0]            \n\t"
            "pshufh     %[B],       %[B],           %[ftmp0]            \n\t"
            "mtc1       %[tmp0],    %[ftmp7]                            \n\t"
            "pshufh     %[C],       %[C],           %[ftmp0]            \n\t"
            "pshufh     %[D],       %[D],           %[ftmp0]            \n\t"

            "1:                                                         \n\t"
            MMI_ULWC1(%[ftmp1], %[src], 0x00)
            MMI_ULWC1(%[ftmp2], %[src], 0x01)
            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            MMI_ULWC1(%[ftmp3], %[src], 0x00)
            MMI_ULWC1(%[ftmp4], %[src], 0x01)

            "punpcklbh  %[ftmp5],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp6],   %[ftmp2],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp5],   %[ftmp5],       %[A]                \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[B]                \n\t"
            "paddh      %[ftmp1],   %[ftmp5],       %[ftmp6]            \n\t"
            "punpcklbh  %[ftmp5],   %[ftmp3],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp6],   %[ftmp4],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp5],   %[ftmp5],       %[C]                \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[D]                \n\t"
            "paddh      %[ftmp2],   %[ftmp5],       %[ftmp6]            \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_32]         \n\t"
            "psrlh      %[ftmp1],   %[ftmp1],       %[ftmp7]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"

            "addi       %[h],       %[h],           -0x01               \n\t"
            MMI_SWC1(%[ftmp1], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            "bnez       %[h],       1b                                  \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
              RESTRICT_ASM_LOW32
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride),[ff_pw_32]"f"(ff_pw_32.f),
              [A]"f"(A.f),                  [B]"f"(B.f),
              [C]"f"(C.f),                  [D]"f"(D.f),
              [tmp0]"r"(0x06)
            : "memory"
        );
    } else if (E.i) {
        const int step = C.i ? stride : 1;
        __asm__ volatile (
            "pxor       %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
            "pshufh     %[A],       %[A],           %[ftmp0]            \n\t"
            "pshufh     %[E],       %[E],           %[ftmp0]            \n\t"
            "mtc1       %[tmp0],    %[ftmp5]                            \n\t"

            "1:                                                         \n\t"
            MMI_ULWC1(%[ftmp1], %[src], 0x00)
            PTR_ADDU   "%[addr0],   %[src],         %[step]             \n\t"
            MMI_ULWC1(%[ftmp2], %[addr0], 0x00)
            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            "addi       %[h],       %[h],           -0x01               \n\t"
            "punpcklbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp4],   %[ftmp2],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp3],   %[ftmp3],       %[A]                \n\t"
            "pmullh     %[ftmp4],   %[ftmp4],       %[E]                \n\t"
            "paddh      %[ftmp1],   %[ftmp3],       %[ftmp4]            \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_32]         \n\t"
            "psrlh      %[ftmp1],   %[ftmp1],       %[ftmp5]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            MMI_SWC1(%[ftmp1], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            "bnez       %[h],       1b                                  \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              RESTRICT_ASM_LOW32
              [addr0]"=&r"(addr[0]),
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride),[step]"r"((mips_reg)step),
              [ff_pw_32]"f"(ff_pw_32.f),    [tmp0]"r"(0x06),
              [A]"f"(A.f),                  [E]"f"(E.f)
            : "memory"
        );
    } else {
        __asm__ volatile (
            "1:                                                         \n\t"
            MMI_ULWC1(%[ftmp0], %[src], 0x00)
            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            MMI_ULWC1(%[ftmp1], %[src], 0x00)
            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            "addi       %[h],       %[h],           -0x02               \n\t"
            MMI_SWC1(%[ftmp0], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            MMI_SWC1(%[ftmp1], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            "bnez       %[h],       1b                                  \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [dst]"+&r"(dst),              [src]"+&r"(src),
              RESTRICT_ASM_LOW32
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride)
            : "memory"
        );
    }
}

void ff_avg_h264_chroma_mc4_mmi(uint8_t *dst, uint8_t *src, ptrdiff_t stride,
        int h, int x, int y)
{
    double ftmp[8];
    mips_reg addr[1];
    union mmi_intfloat64 A, B, C, D, E;
    DECLARE_VAR_LOW32;
    A.i = (8 - x) *(8 - y);
    B.i = x * (8 - y);
    C.i = (8 - x) * y;
    D.i = x * y;
    E.i = B.i + C.i;

    if (D.i) {
        __asm__ volatile (
            "pxor       %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
            "pshufh     %[A],       %[A],           %[ftmp0]            \n\t"
            "pshufh     %[B],       %[B],           %[ftmp0]            \n\t"
            "mtc1       %[tmp0],    %[ftmp7]                            \n\t"
            "pshufh     %[C],       %[C],           %[ftmp0]            \n\t"
            "pshufh     %[D],       %[D],           %[ftmp0]            \n\t"

            "1:                                                         \n\t"
            MMI_ULWC1(%[ftmp1], %[src], 0x00)
            MMI_ULWC1(%[ftmp2], %[src], 0x01)
            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            MMI_ULWC1(%[ftmp3], %[src], 0x00)
            MMI_ULWC1(%[ftmp4], %[src], 0x01)

            "punpcklbh  %[ftmp5],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp6],   %[ftmp2],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp5],   %[ftmp5],       %[A]                \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[B]                \n\t"
            "paddh      %[ftmp1],   %[ftmp5],       %[ftmp6]            \n\t"
            "punpcklbh  %[ftmp5],   %[ftmp3],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp6],   %[ftmp4],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp5],   %[ftmp5],       %[C]                \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[D]                \n\t"
            "paddh      %[ftmp2],   %[ftmp5],       %[ftmp6]            \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_32]         \n\t"
            "psrlh      %[ftmp1],   %[ftmp1],       %[ftmp7]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            MMI_LWC1(%[ftmp2], %[dst], 0x00)
            "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"

            "addi       %[h],       %[h],           -0x01               \n\t"
            MMI_SWC1(%[ftmp1], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            "bnez       %[h],       1b                                  \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
              RESTRICT_ASM_LOW32
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride),[ff_pw_32]"f"(ff_pw_32.f),
              [A]"f"(A.f),                  [B]"f"(B.f),
              [C]"f"(C.f),                  [D]"f"(D.f),
              [tmp0]"r"(0x06)
            : "memory"
        );
    } else if (E.i) {
        const int step = C.i ? stride : 1;
        __asm__ volatile (
            "pxor       %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
            "pshufh     %[A],       %[A],           %[ftmp0]            \n\t"
            "pshufh     %[E],       %[E],           %[ftmp0]            \n\t"
            "mtc1       %[tmp0],    %[ftmp5]                            \n\t"

            "1:                                                         \n\t"
            MMI_ULWC1(%[ftmp1], %[src], 0x00)
            PTR_ADDU   "%[addr0],   %[src],         %[step]             \n\t"
            MMI_ULWC1(%[ftmp2], %[addr0], 0x00)
            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            "addi       %[h],       %[h],           -0x01               \n\t"
            "punpcklbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp4],   %[ftmp2],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp3],   %[ftmp3],       %[A]                \n\t"
            "pmullh     %[ftmp4],   %[ftmp4],       %[E]                \n\t"
            "paddh      %[ftmp1],   %[ftmp3],       %[ftmp4]            \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_32]         \n\t"
            "psrlh      %[ftmp1],   %[ftmp1],       %[ftmp5]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            MMI_LWC1(%[ftmp2], %[dst], 0x00)
            "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"
            MMI_SWC1(%[ftmp1], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            "bnez       %[h],       1b                                  \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              RESTRICT_ASM_LOW32
              [addr0]"=&r"(addr[0]),
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride),[step]"r"((mips_reg)step),
              [ff_pw_32]"f"(ff_pw_32.f),    [tmp0]"r"(0x06),
              [A]"f"(A.f),                  [E]"f"(E.f)
            : "memory"
        );
    } else {
        __asm__ volatile (
            "1:                                                         \n\t"
            MMI_ULWC1(%[ftmp0], %[src], 0x00)
            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            MMI_ULWC1(%[ftmp1], %[src], 0x00)
            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            "addi       %[h],       %[h],           -0x02               \n\t"
            MMI_LWC1(%[ftmp2], %[dst], 0x00)
            "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp2]            \n\t"
            MMI_SWC1(%[ftmp0], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            MMI_LWC1(%[ftmp3], %[dst], 0x00)
            "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp3]            \n\t"
            MMI_SWC1(%[ftmp1], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            "bnez       %[h],       1b                                  \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [dst]"+&r"(dst),              [src]"+&r"(src),
              RESTRICT_ASM_LOW32
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride)
            : "memory"
        );
    }
}
