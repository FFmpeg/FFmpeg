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
#include "libavutil/attributes.h"
#include "libavcodec/fmtconvert.h"
#include "libavutil/mips/asmdefs.h"

#if HAVE_INLINE_ASM
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

        PTR_ADDIU "%[src],       32                     \n\t"
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
        PTR_ADDIU "%[dst],       32                     \n\t"
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
#endif /* HAVE_INLINE_ASM */

av_cold void ff_fmt_convert_init_mips(FmtConvertContext *c)
{
#if HAVE_INLINE_ASM
    c->int32_to_float_fmul_scalar = int32_to_float_fmul_scalar_mips;
#endif
}
