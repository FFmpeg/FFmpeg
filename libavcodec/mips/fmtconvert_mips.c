/*
 * Format Conversion Utils for MIPS
 *
 * Copyright (c) 2012
 *      MIPS Technologies, Inc., California.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the MIPS Technologies, Inc., nor the names of is
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE MIPS TECHNOLOGIES, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE MIPS TECHNOLOGIES, INC. BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Author:  Zoran Lukic (zoranl@mips.com)
 * Author:  Nedeljko Babic (nbabic@mips.com)
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
#include "config.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/fmtconvert.h"

#if HAVE_MIPSDSPR1
static void float_to_int16_mips(int16_t *dst, const float *src, long len)
{
    const float *src_end = src + len;
    int ret0, ret1, ret2, ret3, ret4, ret5, ret6, ret7;
    float src0, src1, src2, src3, src4, src5, src6, src7;

    /*
     * loop is 8 times unrolled in assembler in order to achieve better performance
     */
    __asm__ volatile(
        "beq        %[len],  $zero,   fti16_end%=   \n\t"
        "fti16_lp%=:                                \n\t"
        "lwc1       %[src0], 0(%[src])              \n\t"
        "lwc1       %[src1], 4(%[src])              \n\t"
        "lwc1       %[src2], 8(%[src])              \n\t"
        "lwc1       %[src3], 12(%[src])             \n\t"
        "cvt.w.s    %[src0], %[src0]                \n\t"
        "cvt.w.s    %[src1], %[src1]                \n\t"
        "cvt.w.s    %[src2], %[src2]                \n\t"
        "cvt.w.s    %[src3], %[src3]                \n\t"
        "mfc1       %[ret0], %[src0]                \n\t"
        "mfc1       %[ret1], %[src1]                \n\t"
        "mfc1       %[ret2], %[src2]                \n\t"
        "mfc1       %[ret3], %[src3]                \n\t"
        "lwc1       %[src4], 16(%[src])             \n\t"
        "lwc1       %[src5], 20(%[src])             \n\t"
        "lwc1       %[src6], 24(%[src])             \n\t"
        "lwc1       %[src7], 28(%[src])             \n\t"
        "cvt.w.s    %[src4], %[src4]                \n\t"
        "cvt.w.s    %[src5], %[src5]                \n\t"
        "cvt.w.s    %[src6], %[src6]                \n\t"
        "cvt.w.s    %[src7], %[src7]                \n\t"
        "addiu      %[src],  32                     \n\t"
        "shll_s.w   %[ret0], %[ret0], 16            \n\t"
        "shll_s.w   %[ret1], %[ret1], 16            \n\t"
        "shll_s.w   %[ret2], %[ret2], 16            \n\t"
        "shll_s.w   %[ret3], %[ret3], 16            \n\t"
        "srl        %[ret0], %[ret0], 16            \n\t"
        "srl        %[ret1], %[ret1], 16            \n\t"
        "srl        %[ret2], %[ret2], 16            \n\t"
        "srl        %[ret3], %[ret3], 16            \n\t"
        "sh         %[ret0], 0(%[dst])              \n\t"
        "sh         %[ret1], 2(%[dst])              \n\t"
        "sh         %[ret2], 4(%[dst])              \n\t"
        "sh         %[ret3], 6(%[dst])              \n\t"
        "mfc1       %[ret4], %[src4]                \n\t"
        "mfc1       %[ret5], %[src5]                \n\t"
        "mfc1       %[ret6], %[src6]                \n\t"
        "mfc1       %[ret7], %[src7]                \n\t"
        "shll_s.w   %[ret4], %[ret4], 16            \n\t"
        "shll_s.w   %[ret5], %[ret5], 16            \n\t"
        "shll_s.w   %[ret6], %[ret6], 16            \n\t"
        "shll_s.w   %[ret7], %[ret7], 16            \n\t"
        "srl        %[ret4], %[ret4], 16            \n\t"
        "srl        %[ret5], %[ret5], 16            \n\t"
        "srl        %[ret6], %[ret6], 16            \n\t"
        "srl        %[ret7], %[ret7], 16            \n\t"
        "sh         %[ret4], 8(%[dst])              \n\t"
        "sh         %[ret5], 10(%[dst])             \n\t"
        "sh         %[ret6], 12(%[dst])             \n\t"
        "sh         %[ret7], 14(%[dst])             \n\t"
        "addiu      %[dst],  16                     \n\t"
        "bne        %[src],  %[src_end], fti16_lp%= \n\t"
        "fti16_end%=:                               \n\t"
        : [ret0]"=&r"(ret0), [ret1]"=&r"(ret1), [ret2]"=&r"(ret2), [ret3]"=&r"(ret3),
          [ret4]"=&r"(ret4), [ret5]"=&r"(ret5), [ret6]"=&r"(ret6), [ret7]"=&r"(ret7),
          [src0]"=&f"(src0), [src1]"=&f"(src1), [src2]"=&f"(src2), [src3]"=&f"(src3),
          [src4]"=&f"(src4), [src5]"=&f"(src5), [src6]"=&f"(src6), [src7]"=&f"(src7),
          [src]"+r"(src), [dst]"+r"(dst)
        : [src_end]"r"(src_end), [len]"r"(len)
        : "memory"
    );
}

static void float_to_int16_interleave_mips(int16_t *dst, const float **src, long len,
        int channels)
{
    int   c, ch2 = channels <<1;
    int ret0, ret1, ret2, ret3, ret4, ret5, ret6, ret7;
    float src0, src1, src2, src3, src4, src5, src6, src7;
    int16_t *dst_ptr0, *dst_ptr1, *dst_ptr2, *dst_ptr3;
    int16_t *dst_ptr4, *dst_ptr5, *dst_ptr6, *dst_ptr7;
    const float *src_ptr, *src_ptr2, *src_end;

    if (channels == 2) {
        src_ptr = &src[0][0];
        src_ptr2 = &src[1][0];
        src_end = src_ptr + len;

        __asm__ volatile (
            "fti16i2_lp%=:                                   \n\t"
            "lwc1       %[src0],    0(%[src_ptr])            \n\t"
            "lwc1       %[src1],    0(%[src_ptr2])           \n\t"
            "addiu      %[src_ptr], 4                        \n\t"
            "cvt.w.s    $f9,        %[src0]                  \n\t"
            "cvt.w.s    $f10,       %[src1]                  \n\t"
            "mfc1       %[ret0],    $f9                      \n\t"
            "mfc1       %[ret1],    $f10                     \n\t"
            "shll_s.w   %[ret0],    %[ret0], 16              \n\t"
            "shll_s.w   %[ret1],    %[ret1], 16              \n\t"
            "addiu      %[src_ptr2], 4                       \n\t"
            "srl        %[ret0],    %[ret0], 16              \n\t"
            "srl        %[ret1],    %[ret1], 16              \n\t"
            "sh         %[ret0],    0(%[dst])                \n\t"
            "sh         %[ret1],    2(%[dst])                \n\t"
            "addiu      %[dst],     4                        \n\t"
            "bne        %[src_ptr], %[src_end], fti16i2_lp%= \n\t"
            : [ret0]"=&r"(ret0), [ret1]"=&r"(ret1),
              [src0]"=&f"(src0), [src1]"=&f"(src1),
              [src_ptr]"+r"(src_ptr), [src_ptr2]"+r"(src_ptr2),
              [dst]"+r"(dst)
            : [src_end]"r"(src_end)
            : "memory"
        );
    } else {
        for (c = 0; c < channels; c++) {
            src_ptr  = &src[c][0];
            dst_ptr0 = &dst[c];
            src_end = src_ptr + len;
            /*
             * loop is 8 times unrolled in assembler in order to achieve better performance
             */
            __asm__ volatile(
                "fti16i_lp%=:                                     \n\t"
                "lwc1       %[src0], 0(%[src_ptr])                \n\t"
                "lwc1       %[src1], 4(%[src_ptr])                \n\t"
                "lwc1       %[src2], 8(%[src_ptr])                \n\t"
                "lwc1       %[src3], 12(%[src_ptr])               \n\t"
                "cvt.w.s    %[src0], %[src0]                      \n\t"
                "cvt.w.s    %[src1], %[src1]                      \n\t"
                "cvt.w.s    %[src2], %[src2]                      \n\t"
                "cvt.w.s    %[src3], %[src3]                      \n\t"
                "mfc1       %[ret0], %[src0]                      \n\t"
                "mfc1       %[ret1], %[src1]                      \n\t"
                "mfc1       %[ret2], %[src2]                      \n\t"
                "mfc1       %[ret3], %[src3]                      \n\t"
                "lwc1       %[src4], 16(%[src_ptr])               \n\t"
                "lwc1       %[src5], 20(%[src_ptr])               \n\t"
                "lwc1       %[src6], 24(%[src_ptr])               \n\t"
                "lwc1       %[src7], 28(%[src_ptr])               \n\t"
                "addu       %[dst_ptr1], %[dst_ptr0], %[ch2]      \n\t"
                "addu       %[dst_ptr2], %[dst_ptr1], %[ch2]      \n\t"
                "addu       %[dst_ptr3], %[dst_ptr2], %[ch2]      \n\t"
                "addu       %[dst_ptr4], %[dst_ptr3], %[ch2]      \n\t"
                "addu       %[dst_ptr5], %[dst_ptr4], %[ch2]      \n\t"
                "addu       %[dst_ptr6], %[dst_ptr5], %[ch2]      \n\t"
                "addu       %[dst_ptr7], %[dst_ptr6], %[ch2]      \n\t"
                "addiu      %[src_ptr],  32                       \n\t"
                "cvt.w.s    %[src4], %[src4]                      \n\t"
                "cvt.w.s    %[src5], %[src5]                      \n\t"
                "cvt.w.s    %[src6], %[src6]                      \n\t"
                "cvt.w.s    %[src7], %[src7]                      \n\t"
                "shll_s.w   %[ret0], %[ret0], 16                  \n\t"
                "shll_s.w   %[ret1], %[ret1], 16                  \n\t"
                "shll_s.w   %[ret2], %[ret2], 16                  \n\t"
                "shll_s.w   %[ret3], %[ret3], 16                  \n\t"
                "srl        %[ret0], %[ret0], 16                  \n\t"
                "srl        %[ret1], %[ret1], 16                  \n\t"
                "srl        %[ret2], %[ret2], 16                  \n\t"
                "srl        %[ret3], %[ret3], 16                  \n\t"
                "sh         %[ret0], 0(%[dst_ptr0])               \n\t"
                "sh         %[ret1], 0(%[dst_ptr1])               \n\t"
                "sh         %[ret2], 0(%[dst_ptr2])               \n\t"
                "sh         %[ret3], 0(%[dst_ptr3])               \n\t"
                "mfc1       %[ret4], %[src4]                      \n\t"
                "mfc1       %[ret5], %[src5]                      \n\t"
                "mfc1       %[ret6], %[src6]                      \n\t"
                "mfc1       %[ret7], %[src7]                      \n\t"
                "shll_s.w   %[ret4], %[ret4], 16                  \n\t"
                "shll_s.w   %[ret5], %[ret5], 16                  \n\t"
                "shll_s.w   %[ret6], %[ret6], 16                  \n\t"
                "shll_s.w   %[ret7], %[ret7], 16                  \n\t"
                "srl        %[ret4], %[ret4], 16                  \n\t"
                "srl        %[ret5], %[ret5], 16                  \n\t"
                "srl        %[ret6], %[ret6], 16                  \n\t"
                "srl        %[ret7], %[ret7], 16                  \n\t"
                "sh         %[ret4], 0(%[dst_ptr4])               \n\t"
                "sh         %[ret5], 0(%[dst_ptr5])               \n\t"
                "sh         %[ret6], 0(%[dst_ptr6])               \n\t"
                "sh         %[ret7], 0(%[dst_ptr7])               \n\t"
                "addu       %[dst_ptr0], %[dst_ptr7], %[ch2]      \n\t"
                "bne        %[src_ptr],  %[src_end],  fti16i_lp%= \n\t"
                : [ret0]"=&r"(ret0), [ret1]"=&r"(ret1), [ret2]"=&r"(ret2), [ret3]"=&r"(ret3),
                  [ret4]"=&r"(ret4), [ret5]"=&r"(ret5), [ret6]"=&r"(ret6), [ret7]"=&r"(ret7),
                  [src0]"=&f"(src0), [src1]"=&f"(src1), [src2]"=&f"(src2), [src3]"=&f"(src3),
                  [src4]"=&f"(src4), [src5]"=&f"(src5), [src6]"=&f"(src6), [src7]"=&f"(src7),
                  [dst_ptr1]"=&r"(dst_ptr1), [dst_ptr2]"=&r"(dst_ptr2), [dst_ptr3]"=&r"(dst_ptr3),
                  [dst_ptr4]"=&r"(dst_ptr4), [dst_ptr5]"=&r"(dst_ptr5), [dst_ptr6]"=&r"(dst_ptr6),
                  [dst_ptr7]"=&r"(dst_ptr7), [dst_ptr0]"+r"(dst_ptr0), [src_ptr]"+r"(src_ptr)
                : [ch2]"r"(ch2), [src_end]"r"(src_end)
                : "memory"
            );
        }
    }
}
#endif /* HAVE_MIPSDSPR1 */

static void int32_to_float_fmul_scalar_mips(float *dst, const int *src,
        float mul, int len)
{
    /*
     * variables used in inline assembler
     */
    float temp1, temp3, temp5, temp7, temp9, temp11, temp13, temp15;

    int rpom1, rpom2, rpom11, rpom21, rpom12, rpom22, rpom13, rpom23;
    const int *src_end = src + len;
    /*
     * loop is 8 times unrolled in assembler in order to achieve better performance
     */
    __asm__ volatile (
        "i32tf_lp%=:                                    \n\t"
        "lw       %[rpom11],     0(%[src])              \n\t"
        "lw       %[rpom21],     4(%[src])              \n\t"
        "lw       %[rpom1],      8(%[src])              \n\t"
        "lw       %[rpom2],      12(%[src])             \n\t"
        "mtc1     %[rpom11],     %[temp1]               \n\t"
        "mtc1     %[rpom21],     %[temp3]               \n\t"
        "mtc1     %[rpom1],      %[temp5]               \n\t"
        "mtc1     %[rpom2],      %[temp7]               \n\t"

        "lw       %[rpom13],     16(%[src])             \n\t"
        "lw       %[rpom23],     20(%[src])             \n\t"
        "lw       %[rpom12],     24(%[src])             \n\t"
        "lw       %[rpom22],     28(%[src])             \n\t"
        "mtc1     %[rpom13],     %[temp9]               \n\t"
        "mtc1     %[rpom23],     %[temp11]              \n\t"
        "mtc1     %[rpom12],     %[temp13]              \n\t"
        "mtc1     %[rpom22],     %[temp15]              \n\t"

        "addiu    %[src],        32                     \n\t"
        "cvt.s.w  %[temp1],      %[temp1]               \n\t"
        "cvt.s.w  %[temp3],      %[temp3]               \n\t"
        "cvt.s.w  %[temp5],      %[temp5]               \n\t"
        "cvt.s.w  %[temp7],      %[temp7]               \n\t"

        "cvt.s.w  %[temp9],      %[temp9]               \n\t"
        "cvt.s.w  %[temp11],     %[temp11]              \n\t"
        "cvt.s.w  %[temp13],     %[temp13]              \n\t"
        "cvt.s.w  %[temp15],     %[temp15]              \n\t"

        "mul.s   %[temp1],       %[temp1],    %[mul]    \n\t"
        "mul.s   %[temp3],       %[temp3],    %[mul]    \n\t"
        "mul.s   %[temp5],       %[temp5],    %[mul]    \n\t"
        "mul.s   %[temp7],       %[temp7],    %[mul]    \n\t"

        "mul.s   %[temp9],       %[temp9],    %[mul]    \n\t"
        "mul.s   %[temp11],      %[temp11],   %[mul]    \n\t"
        "mul.s   %[temp13],      %[temp13],   %[mul]    \n\t"
        "mul.s   %[temp15],      %[temp15],   %[mul]    \n\t"

        "swc1    %[temp1],       0(%[dst])              \n\t" /*dst[i] = src[i] * mul;    */
        "swc1    %[temp3],       4(%[dst])              \n\t" /*dst[i+1] = src[i+1] * mul;*/
        "swc1    %[temp5],       8(%[dst])              \n\t" /*dst[i+2] = src[i+2] * mul;*/
        "swc1    %[temp7],       12(%[dst])             \n\t" /*dst[i+3] = src[i+3] * mul;*/

        "swc1    %[temp9],       16(%[dst])             \n\t" /*dst[i+4] = src[i+4] * mul;*/
        "swc1    %[temp11],      20(%[dst])             \n\t" /*dst[i+5] = src[i+5] * mul;*/
        "swc1    %[temp13],      24(%[dst])             \n\t" /*dst[i+6] = src[i+6] * mul;*/
        "swc1    %[temp15],      28(%[dst])             \n\t" /*dst[i+7] = src[i+7] * mul;*/
        "addiu   %[dst],        32                      \n\t"
        "bne     %[src],        %[src_end], i32tf_lp%=  \n\t"
        : [temp1]"=&f"(temp1),   [temp11]"=&f"(temp11),
          [temp13]"=&f"(temp13), [temp15]"=&f"(temp15),
          [temp3]"=&f"(temp3),   [temp5]"=&f"(temp5),
          [temp7]"=&f"(temp7),   [temp9]"=&f"(temp9),
          [rpom1]"=&r"(rpom1),   [rpom2]"=&r"(rpom2),
          [rpom11]"=&r"(rpom11), [rpom21]"=&r"(rpom21),
          [rpom12]"=&r"(rpom12), [rpom22]"=&r"(rpom22),
          [rpom13]"=&r"(rpom13), [rpom23]"=&r"(rpom23),
          [dst]"+r"(dst),       [src]"+r"(src)
        : [mul]"f"(mul),        [src_end]"r"(src_end)
        : "memory"
    );
}

av_cold void ff_fmt_convert_init_mips(FmtConvertContext *c)
{
#if HAVE_MIPSDSPR1
    c->float_to_int16_interleave = float_to_int16_interleave_mips;
    c->float_to_int16 = float_to_int16_mips;
#endif
    c->int32_to_float_fmul_scalar = int32_to_float_fmul_scalar_mips;
}
