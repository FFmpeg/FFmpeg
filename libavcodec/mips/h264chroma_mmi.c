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
    const int A = (8 - x) * (8 - y);
    const int B = x * (8 - y);
    const int C = (8 - x) * y;
    const int D = x * y;
    const int E = B + C;
    double ftmp[10];
    uint64_t tmp[1];
    mips_reg addr[1];
    DECLARE_VAR_ALL64;

    if (D) {
        __asm__ volatile (
            "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
            "dli        %[tmp0],    0x06                                \n\t"
            "pshufh     %[A],       %[A],           %[ftmp0]            \n\t"
            "pshufh     %[B],       %[B],           %[ftmp0]            \n\t"
            "mtc1       %[tmp0],    %[ftmp9]                            \n\t"
            "pshufh     %[C],       %[C],           %[ftmp0]            \n\t"
            "pshufh     %[D],       %[D],           %[ftmp0]            \n\t"

            "1:                                                         \n\t"
            PTR_ADDU   "%[addr0],   %[src],         %[stride]           \n\t"
            MMI_ULDC1(%[ftmp1], %[src], 0x00)
            MMI_ULDC1(%[ftmp2], %[src], 0x01)
            MMI_ULDC1(%[ftmp3], %[addr0], 0x00)
            MMI_ULDC1(%[ftmp4], %[addr0], 0x01)

            "punpcklbh  %[ftmp5],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp6],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp7],   %[ftmp2],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp8],   %[ftmp2],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp5],   %[ftmp5],       %[A]                \n\t"
            "pmullh     %[ftmp7],   %[ftmp7],       %[B]                \n\t"
            "paddh      %[ftmp1],   %[ftmp5],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[A]                \n\t"
            "pmullh     %[ftmp8],   %[ftmp8],       %[B]                \n\t"
            "paddh      %[ftmp2],   %[ftmp6],       %[ftmp8]            \n\t"

            "punpcklbh  %[ftmp5],   %[ftmp3],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp6],   %[ftmp3],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp7],   %[ftmp4],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp8],   %[ftmp4],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp5],   %[ftmp5],       %[C]                \n\t"
            "pmullh     %[ftmp7],   %[ftmp7],       %[D]                \n\t"
            "paddh      %[ftmp3],   %[ftmp5],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[C]                \n\t"
            "pmullh     %[ftmp8],   %[ftmp8],       %[D]                \n\t"
            "paddh      %[ftmp4],   %[ftmp6],       %[ftmp8]            \n\t"

            "paddh      %[ftmp1],   %[ftmp1],       %[ftmp3]            \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ftmp4]            \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_32]         \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_32]         \n\t"
            "psrlh      %[ftmp1],   %[ftmp1],       %[ftmp9]            \n\t"
            "psrlh      %[ftmp2],   %[ftmp2],       %[ftmp9]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"
            "addi       %[h],       %[h],           -0x01               \n\t"
            MMI_SDC1(%[ftmp1], %[dst], 0x00)
            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            "bnez       %[h],       1b                                  \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
              [ftmp8]"=&f"(ftmp[8]),        [ftmp9]"=&f"(ftmp[9]),
              [tmp0]"=&r"(tmp[0]),
              RESTRICT_ASM_ALL64
              [addr0]"=&r"(addr[0]),
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride),[ff_pw_32]"f"(ff_pw_32),
              [A]"f"(A),                    [B]"f"(B),
              [C]"f"(C),                    [D]"f"(D)
            : "memory"
        );
    } else if (E) {
        const int step = C ? stride : 1;

        __asm__ volatile (
            "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
            "dli        %[tmp0],    0x06                                \n\t"
            "pshufh     %[A],       %[A],           %[ftmp0]            \n\t"
            "pshufh     %[E],       %[E],           %[ftmp0]            \n\t"
            "mtc1       %[tmp0],    %[ftmp7]                            \n\t"

            "1:                                                         \n\t"
            PTR_ADDU   "%[addr0],   %[src],         %[step]             \n\t"
            MMI_ULDC1(%[ftmp1], %[src], 0x00)
            MMI_ULDC1(%[ftmp2], %[addr0], 0x00)

            "punpcklbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp4],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp5],   %[ftmp2],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp6],   %[ftmp2],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp3],   %[ftmp3],       %[A]                \n\t"
            "pmullh     %[ftmp5],   %[ftmp5],       %[E]                \n\t"
            "paddh      %[ftmp1],   %[ftmp3],       %[ftmp5]            \n\t"
            "pmullh     %[ftmp4],   %[ftmp4],       %[A]                \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[E]                \n\t"
            "paddh      %[ftmp2],   %[ftmp4],       %[ftmp6]            \n\t"

            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_32]         \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_32]         \n\t"
            "psrlh      %[ftmp1],   %[ftmp1],       %[ftmp7]            \n\t"
            "psrlh      %[ftmp2],   %[ftmp2],       %[ftmp7]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"
            "addi       %[h],       %[h],           -0x01               \n\t"
            MMI_SDC1(%[ftmp1], %[dst], 0x00)
            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            "bnez       %[h],       1b                                  \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
              [tmp0]"=&r"(tmp[0]),
              RESTRICT_ASM_ALL64
              [addr0]"=&r"(addr[0]),
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride),[step]"r"((mips_reg)step),
              [ff_pw_32]"f"(ff_pw_32),
              [A]"f"(A),                    [E]"f"(E)
            : "memory"
        );
    } else {
        __asm__ volatile (
            "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
            "dli        %[tmp0],    0x06                                \n\t"
            "pshufh     %[A],       %[A],           %[ftmp0]            \n\t"
            "mtc1       %[tmp0],    %[ftmp4]                            \n\t"

            "1:                                                         \n\t"
            MMI_ULDC1(%[ftmp1], %[src], 0x00)
            "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp1],   %[ftmp2],       %[A]                \n\t"
            "pmullh     %[ftmp2],   %[ftmp3],       %[A]                \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_32]         \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_32]         \n\t"
            "psrlh      %[ftmp1],   %[ftmp1],       %[ftmp4]            \n\t"
            "psrlh      %[ftmp2],   %[ftmp2],       %[ftmp4]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            MMI_SDC1(%[ftmp1], %[dst], 0x00)

            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            MMI_ULDC1(%[ftmp1], %[src], 0x00)
            "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp1],   %[ftmp2],       %[A]                \n\t"
            "pmullh     %[ftmp2],   %[ftmp3],       %[A]                \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_32]         \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_32]         \n\t"
            "psrlh      %[ftmp1],   %[ftmp1],       %[ftmp4]            \n\t"
            "psrlh      %[ftmp2],   %[ftmp2],       %[ftmp4]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"
            "addi       %[h],       %[h],           -0x02               \n\t"
            MMI_SDC1(%[ftmp1], %[dst], 0x00)

            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            "bnez       %[h],       1b                                  \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),
              [tmp0]"=&r"(tmp[0]),
              RESTRICT_ASM_ALL64
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride),[ff_pw_32]"f"(ff_pw_32),
              [A]"f"(A)
            : "memory"
        );
    }
}

void ff_avg_h264_chroma_mc8_mmi(uint8_t *dst, uint8_t *src, ptrdiff_t stride,
        int h, int x, int y)
{
    const int A = (8 - x) * (8 - y);
    const int B = x * (8 - y);
    const int C = (8 - x) * y;
    const int D = x * y;
    const int E = B + C;
    double ftmp[10];
    uint64_t tmp[1];
    mips_reg addr[1];
    DECLARE_VAR_ALL64;

    if (D) {
        __asm__ volatile (
            "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
            "dli        %[tmp0],    0x06                                \n\t"
            "pshufh     %[A],       %[A],           %[ftmp0]            \n\t"
            "pshufh     %[B],       %[B],           %[ftmp0]            \n\t"
            "mtc1       %[tmp0],    %[ftmp9]                            \n\t"
            "pshufh     %[C],       %[C],           %[ftmp0]            \n\t"
            "pshufh     %[D],       %[D],           %[ftmp0]            \n\t"

            "1:                                                         \n\t"
            PTR_ADDU   "%[addr0],   %[src],         %[stride]           \n\t"
            MMI_ULDC1(%[ftmp1], %[src], 0x00)
            MMI_ULDC1(%[ftmp2], %[src], 0x01)
            MMI_ULDC1(%[ftmp3], %[addr0], 0x00)
            MMI_ULDC1(%[ftmp4], %[addr0], 0x01)

            "punpcklbh  %[ftmp5],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp6],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp7],   %[ftmp2],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp8],   %[ftmp2],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp5],   %[ftmp5],       %[A]                \n\t"
            "pmullh     %[ftmp7],   %[ftmp7],       %[B]                \n\t"
            "paddh      %[ftmp1],   %[ftmp5],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[A]                \n\t"
            "pmullh     %[ftmp8],   %[ftmp8],       %[B]                \n\t"
            "paddh      %[ftmp2],   %[ftmp6],       %[ftmp8]            \n\t"

            "punpcklbh  %[ftmp5],   %[ftmp3],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp6],   %[ftmp3],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp7],   %[ftmp4],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp8],   %[ftmp4],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp5],   %[ftmp5],       %[C]                \n\t"
            "pmullh     %[ftmp7],   %[ftmp7],       %[D]                \n\t"
            "paddh      %[ftmp3],   %[ftmp5],       %[ftmp7]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[C]                \n\t"
            "pmullh     %[ftmp8],   %[ftmp8],       %[D]                \n\t"
            "paddh      %[ftmp4],   %[ftmp6],       %[ftmp8]            \n\t"

            "paddh      %[ftmp1],   %[ftmp1],       %[ftmp3]            \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ftmp4]            \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_32]         \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_32]         \n\t"
            "psrlh      %[ftmp1],   %[ftmp1],       %[ftmp9]            \n\t"
            "psrlh      %[ftmp2],   %[ftmp2],       %[ftmp9]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"
            MMI_LDC1(%[ftmp2], %[dst], 0x00)
            "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"
            "addi       %[h],       %[h],           -0x01               \n\t"
            MMI_SDC1(%[ftmp1], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            "bnez       %[h],       1b                                  \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
              [ftmp8]"=&f"(ftmp[8]),        [ftmp9]"=&f"(ftmp[9]),
              [tmp0]"=&r"(tmp[0]),
              RESTRICT_ASM_ALL64
              [addr0]"=&r"(addr[0]),
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride),[ff_pw_32]"f"(ff_pw_32),
              [A]"f"(A),                    [B]"f"(B),
              [C]"f"(C),                    [D]"f"(D)
            : "memory"
        );
    } else if (E) {
        const int step = C ? stride : 1;

        __asm__ volatile (
            "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
            "dli        %[tmp0],    0x06                                \n\t"
            "pshufh     %[A],       %[A],           %[ftmp0]            \n\t"
            "pshufh     %[E],       %[E],           %[ftmp0]            \n\t"
            "mtc1       %[tmp0],    %[ftmp7]                            \n\t"

            "1:                                                         \n\t"
            PTR_ADDU   "%[addr0],   %[src],         %[step]             \n\t"
            MMI_ULDC1(%[ftmp1], %[src], 0x00)
            MMI_ULDC1(%[ftmp2], %[addr0], 0x00)

            "punpcklbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp4],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp5],   %[ftmp2],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp6],   %[ftmp2],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp3],   %[ftmp3],       %[A]                \n\t"
            "pmullh     %[ftmp5],   %[ftmp5],       %[E]                \n\t"
            "paddh      %[ftmp1],   %[ftmp3],       %[ftmp5]            \n\t"
            "pmullh     %[ftmp4],   %[ftmp4],       %[A]                \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[E]                \n\t"
            "paddh      %[ftmp2],   %[ftmp4],       %[ftmp6]            \n\t"

            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_32]         \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_32]         \n\t"
            "psrlh      %[ftmp1],   %[ftmp1],       %[ftmp7]            \n\t"
            "psrlh      %[ftmp2],   %[ftmp2],       %[ftmp7]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"
            MMI_LDC1(%[ftmp2], %[dst], 0x00)
            "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"
            "addi       %[h],       %[h],           -0x01               \n\t"
            MMI_SDC1(%[ftmp1], %[dst], 0x00)
            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            "bnez       %[h],       1b                                  \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
              [tmp0]"=&r"(tmp[0]),
              RESTRICT_ASM_ALL64
              [addr0]"=&r"(addr[0]),
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride),[step]"r"((mips_reg)step),
              [ff_pw_32]"f"(ff_pw_32),
              [A]"f"(A),                    [E]"f"(E)
            : "memory"
        );
    } else {
        __asm__ volatile (
            "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
            "dli        %[tmp0],    0x06                                \n\t"
            "pshufh     %[A],       %[A],           %[ftmp0]            \n\t"
            "mtc1       %[tmp0],    %[ftmp4]                            \n\t"

            "1:                                                         \n\t"
            MMI_ULDC1(%[ftmp1], %[src], 0x00)
            "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp1],   %[ftmp2],       %[A]                \n\t"
            "pmullh     %[ftmp2],   %[ftmp3],       %[A]                \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_32]         \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_32]         \n\t"
            "psrlh      %[ftmp1],   %[ftmp1],       %[ftmp4]            \n\t"
            "psrlh      %[ftmp2],   %[ftmp2],       %[ftmp4]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"
            MMI_LDC1(%[ftmp2], %[dst], 0x00)
            "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            MMI_SDC1(%[ftmp1], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"

            MMI_ULDC1(%[ftmp1], %[src], 0x00)
            "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp1],   %[ftmp2],       %[A]                \n\t"
            "pmullh     %[ftmp2],   %[ftmp3],       %[A]                \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_32]         \n\t"
            "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_32]         \n\t"
            "psrlh      %[ftmp1],   %[ftmp1],       %[ftmp4]            \n\t"
            "psrlh      %[ftmp2],   %[ftmp2],       %[ftmp4]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"
            MMI_LDC1(%[ftmp2], %[dst], 0x00)
            "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"
            "addi       %[h],       %[h],           -0x02               \n\t"
            MMI_SDC1(%[ftmp1], %[dst], 0x00)

            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            "bnez       %[h],       1b                                  \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),
              [tmp0]"=&r"(tmp[0]),
              RESTRICT_ASM_ALL64
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride),[ff_pw_32]"f"(ff_pw_32),
              [A]"f"(A)
            : "memory"
        );
    }
}

void ff_put_h264_chroma_mc4_mmi(uint8_t *dst, uint8_t *src, ptrdiff_t stride,
        int h, int x, int y)
{
    const int A = (8 - x) * (8 - y);
    const int B = x * (8 - y);
    const int C = (8 - x) *  y;
    const int D = x *  y;
    const int E = B + C;
    double ftmp[8];
    uint64_t tmp[1];
    mips_reg addr[1];
    DECLARE_VAR_LOW32;

    if (D) {
        __asm__ volatile (
            "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
            "dli        %[tmp0],    0x06                                \n\t"
            "pshufh     %[A],       %[A],           %[ftmp0]            \n\t"
            "pshufh     %[B],       %[B],           %[ftmp0]            \n\t"
            "mtc1       %[tmp0],    %[ftmp7]                            \n\t"
            "pshufh     %[C],       %[C],           %[ftmp0]            \n\t"
            "pshufh     %[D],       %[D],           %[ftmp0]            \n\t"

            "1:                                                         \n\t"
            PTR_ADDU   "%[addr0],   %[src],         %[stride]           \n\t"
            MMI_ULWC1(%[ftmp1], %[src], 0x00)
            MMI_ULWC1(%[ftmp2], %[src], 0x01)
            MMI_ULWC1(%[ftmp3], %[addr0], 0x00)
            MMI_ULWC1(%[ftmp4], %[addr0], 0x01)

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
            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            "bnez       %[h],       1b                                  \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
              [tmp0]"=&r"(tmp[0]),
              RESTRICT_ASM_LOW32
              [addr0]"=&r"(addr[0]),
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride),[ff_pw_32]"f"(ff_pw_32),
              [A]"f"(A),                    [B]"f"(B),
              [C]"f"(C),                    [D]"f"(D)
            : "memory"
        );
    } else if (E) {
        const int step = C ? stride : 1;

        __asm__ volatile (
            "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
            "dli        %[tmp0],    0x06                                \n\t"
            "pshufh     %[A],       %[A],           %[ftmp0]            \n\t"
            "pshufh     %[E],       %[E],           %[ftmp0]            \n\t"
            "mtc1       %[tmp0],    %[ftmp5]                            \n\t"

            "1:                                                         \n\t"
            PTR_ADDU   "%[addr0],   %[src],         %[step]             \n\t"
            MMI_ULWC1(%[ftmp1], %[src], 0x00)
            MMI_ULWC1(%[ftmp2], %[addr0], 0x00)

            "punpcklbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp4],   %[ftmp2],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp3],   %[ftmp3],       %[A]                \n\t"
            "pmullh     %[ftmp4],   %[ftmp4],       %[E]                \n\t"
            "paddh      %[ftmp1],   %[ftmp3],       %[ftmp4]            \n\t"

            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_32]         \n\t"
            "psrlh      %[ftmp1],   %[ftmp1],       %[ftmp5]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            "addi       %[h],       %[h],           -0x01               \n\t"
            MMI_SWC1(%[ftmp1], %[dst], 0x00)
            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            "bnez       %[h],       1b                                  \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [tmp0]"=&r"(tmp[0]),
              RESTRICT_ASM_LOW32
              [addr0]"=&r"(addr[0]),
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride),[step]"r"((mips_reg)step),
              [ff_pw_32]"f"(ff_pw_32),
              [A]"f"(A),                    [E]"f"(E)
            : "memory"
        );
    } else {
        __asm__ volatile (
            "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
            "dli        %[tmp0],    0x06                                \n\t"
            "pshufh     %[A],       %[A],           %[ftmp0]            \n\t"
            "mtc1       %[tmp0],    %[ftmp3]                            \n\t"

            "1:                                                         \n\t"
            MMI_ULWC1(%[ftmp1], %[src], 0x00)
            "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp1],   %[ftmp2],       %[A]                \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_32]         \n\t"
            "psrlh      %[ftmp1],   %[ftmp1],       %[ftmp3]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            MMI_SWC1(%[ftmp1], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"

            MMI_ULWC1(%[ftmp1], %[src], 0x00)
            "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp1],   %[ftmp2],       %[A]                \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_32]         \n\t"
            "psrlh      %[ftmp1],   %[ftmp1],       %[ftmp3]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            "addi       %[h],       %[h],           -0x02               \n\t"
            MMI_SWC1(%[ftmp1], %[dst], 0x00)

            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            "bnez       %[h],       1b                                  \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [tmp0]"=&r"(tmp[0]),
              RESTRICT_ASM_LOW32
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride),[ff_pw_32]"f"(ff_pw_32),
              [A]"f"(A)
            : "memory"
        );
    }
}

void ff_avg_h264_chroma_mc4_mmi(uint8_t *dst, uint8_t *src, ptrdiff_t stride,
        int h, int x, int y)
{
    const int A = (8 - x) *(8 - y);
    const int B = x * (8 - y);
    const int C = (8 - x) * y;
    const int D = x * y;
    const int E = B + C;
    double ftmp[8];
    uint64_t tmp[1];
    mips_reg addr[1];
    DECLARE_VAR_LOW32;

    if (D) {
        __asm__ volatile (
            "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
            "dli        %[tmp0],    0x06                                \n\t"
            "pshufh     %[A],       %[A],           %[ftmp0]            \n\t"
            "pshufh     %[B],       %[B],           %[ftmp0]            \n\t"
            "mtc1       %[tmp0],    %[ftmp7]                            \n\t"
            "pshufh     %[C],       %[C],           %[ftmp0]            \n\t"
            "pshufh     %[D],       %[D],           %[ftmp0]            \n\t"

            "1:                                                         \n\t"
            PTR_ADDU   "%[addr0],   %[src],         %[stride]           \n\t"
            MMI_ULWC1(%[ftmp1], %[src], 0x00)
            MMI_ULWC1(%[ftmp2], %[src], 0x01)
            MMI_ULWC1(%[ftmp3], %[addr0], 0x00)
            MMI_ULWC1(%[ftmp4], %[addr0], 0x01)

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
            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            "bnez       %[h],       1b                                  \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
              [tmp0]"=&r"(tmp[0]),
              RESTRICT_ASM_LOW32
              [addr0]"=&r"(addr[0]),
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride),[ff_pw_32]"f"(ff_pw_32),
              [A]"f"(A),                    [B]"f"(B),
              [C]"f"(C),                    [D]"f"(D)
            : "memory"
        );
    } else if (E) {
        const int step = C ? stride : 1;

        __asm__ volatile (
            "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
            "dli        %[tmp0],    0x06                                \n\t"
            "pshufh     %[A],       %[A],           %[ftmp0]            \n\t"
            "pshufh     %[E],       %[E],           %[ftmp0]            \n\t"
            "mtc1       %[tmp0],    %[ftmp5]                            \n\t"
            "1:                                                         \n\t"
            PTR_ADDU   "%[addr0],   %[src],         %[step]             \n\t"
            MMI_ULWC1(%[ftmp1], %[src], 0x00)
            MMI_ULWC1(%[ftmp2], %[addr0], 0x00)

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
            "addi       %[h],       %[h],           -0x01               \n\t"
            MMI_SWC1(%[ftmp1], %[dst], 0x00)
            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            "bnez       %[h],       1b                                  \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [tmp0]"=&r"(tmp[0]),
              RESTRICT_ASM_LOW32
              [addr0]"=&r"(addr[0]),
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride),[step]"r"((mips_reg)step),
              [ff_pw_32]"f"(ff_pw_32),
              [A]"f"(A),                    [E]"f"(E)
            : "memory"
        );
    } else {
        __asm__ volatile (
            "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
            "dli        %[tmp0],    0x06                                \n\t"
            "pshufh     %[A],       %[A],           %[ftmp0]            \n\t"
            "mtc1       %[tmp0],    %[ftmp3]                            \n\t"

            "1:                                                         \n\t"
            MMI_ULWC1(%[ftmp1], %[src], 0x00)
            "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp1],   %[ftmp2],       %[A]                \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_32]         \n\t"
            "psrlh      %[ftmp1],   %[ftmp1],       %[ftmp3]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            MMI_LWC1(%[ftmp2], %[dst], 0x00)
            "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"
            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            MMI_SWC1(%[ftmp1], %[dst], 0x00)
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"

            MMI_ULWC1(%[ftmp1], %[src], 0x00)
            "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp1],   %[ftmp2],       %[A]                \n\t"
            "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_32]         \n\t"
            "psrlh      %[ftmp1],   %[ftmp1],       %[ftmp3]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            MMI_LWC1(%[ftmp2], %[dst], 0x00)
            "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"
            "addi       %[h],       %[h],           -0x02               \n\t"
            MMI_SWC1(%[ftmp1], %[dst], 0x00)

            PTR_ADDU   "%[src],     %[src],         %[stride]           \n\t"
            PTR_ADDU   "%[dst],     %[dst],         %[stride]           \n\t"
            "bnez       %[h],       1b                                  \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [tmp0]"=&r"(tmp[0]),
              RESTRICT_ASM_LOW32
              [dst]"+&r"(dst),              [src]"+&r"(src),
              [h]"+&r"(h)
            : [stride]"r"((mips_reg)stride),[ff_pw_32]"f"(ff_pw_32),
              [A]"f"(A)
            : "memory"
        );
    }
}
