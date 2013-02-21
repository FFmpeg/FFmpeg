/*
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
 * 3. Neither the name of the MIPS Technologies, Inc., nor the names of its
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
 * Authors:  Darko Laus      (darko@mips.com)
 *           Djordje Pesut   (djordje@mips.com)
 *           Mirjana Vulin   (mvulin@mips.com)
 *
 * AAC Spectral Band Replication decoding functions optimized for MIPS
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

/**
 * @file
 * Reference: libavcodec/sbrdsp.c
 */

#include "config.h"
#include "libavcodec/sbrdsp.h"

#if HAVE_INLINE_ASM
static void sbr_neg_odd_64_mips(float *x)
{
    int Temp1, Temp2, Temp3, Temp4, Temp5;
    float *x1    = &x[1];
    float *x_end = x1 + 64;

    /* loop unrolled 4 times */
    __asm__ volatile (
        "lui    %[Temp5],   0x8000                  \n\t"
    "1:                                             \n\t"
        "lw     %[Temp1],   0(%[x1])                \n\t"
        "lw     %[Temp2],   8(%[x1])                \n\t"
        "lw     %[Temp3],   16(%[x1])               \n\t"
        "lw     %[Temp4],   24(%[x1])               \n\t"
        "xor    %[Temp1],   %[Temp1],   %[Temp5]    \n\t"
        "xor    %[Temp2],   %[Temp2],   %[Temp5]    \n\t"
        "xor    %[Temp3],   %[Temp3],   %[Temp5]    \n\t"
        "xor    %[Temp4],   %[Temp4],   %[Temp5]    \n\t"
        "sw     %[Temp1],   0(%[x1])                \n\t"
        "sw     %[Temp2],   8(%[x1])                \n\t"
        "sw     %[Temp3],   16(%[x1])               \n\t"
        "sw     %[Temp4],   24(%[x1])               \n\t"
        "addiu  %[x1],      %[x1],      32          \n\t"
        "bne    %[x1],      %[x_end],   1b          \n\t"

        : [Temp1]"=&r"(Temp1), [Temp2]"=&r"(Temp2),
          [Temp3]"=&r"(Temp3), [Temp4]"=&r"(Temp4),
          [Temp5]"=&r"(Temp5), [x1]"+r"(x1)
        : [x_end]"r"(x_end)
        : "memory"
    );
}

static void sbr_qmf_pre_shuffle_mips(float *z)
{
    int Temp1, Temp2, Temp3, Temp4, Temp5, Temp6;
    float *z1 = &z[66];
    float *z2 = &z[59];
    float *z3 = &z[2];
    float *z4 = z1 + 60;

    /* loop unrolled 5 times */
    __asm__ volatile (
        "lui    %[Temp6],   0x8000                  \n\t"
    "1:                                             \n\t"
        "lw     %[Temp1],   0(%[z2])                \n\t"
        "lw     %[Temp2],   4(%[z2])                \n\t"
        "lw     %[Temp3],   8(%[z2])                \n\t"
        "lw     %[Temp4],   12(%[z2])               \n\t"
        "lw     %[Temp5],   16(%[z2])               \n\t"
        "xor    %[Temp1],   %[Temp1],   %[Temp6]    \n\t"
        "xor    %[Temp2],   %[Temp2],   %[Temp6]    \n\t"
        "xor    %[Temp3],   %[Temp3],   %[Temp6]    \n\t"
        "xor    %[Temp4],   %[Temp4],   %[Temp6]    \n\t"
        "xor    %[Temp5],   %[Temp5],   %[Temp6]    \n\t"
        "addiu  %[z2],      %[z2],      -20         \n\t"
        "sw     %[Temp1],   32(%[z1])               \n\t"
        "sw     %[Temp2],   24(%[z1])               \n\t"
        "sw     %[Temp3],   16(%[z1])               \n\t"
        "sw     %[Temp4],   8(%[z1])                \n\t"
        "sw     %[Temp5],   0(%[z1])                \n\t"
        "lw     %[Temp1],   0(%[z3])                \n\t"
        "lw     %[Temp2],   4(%[z3])                \n\t"
        "lw     %[Temp3],   8(%[z3])                \n\t"
        "lw     %[Temp4],   12(%[z3])               \n\t"
        "lw     %[Temp5],   16(%[z3])               \n\t"
        "sw     %[Temp1],   4(%[z1])                \n\t"
        "sw     %[Temp2],   12(%[z1])               \n\t"
        "sw     %[Temp3],   20(%[z1])               \n\t"
        "sw     %[Temp4],   28(%[z1])               \n\t"
        "sw     %[Temp5],   36(%[z1])               \n\t"
        "addiu  %[z3],      %[z3],      20          \n\t"
        "addiu  %[z1],      %[z1],      40          \n\t"
        "bne    %[z1],      %[z4],      1b          \n\t"
        "lw     %[Temp1],   132(%[z])               \n\t"
        "lw     %[Temp2],   128(%[z])               \n\t"
        "lw     %[Temp3],   0(%[z])                 \n\t"
        "lw     %[Temp4],   4(%[z])                 \n\t"
        "xor    %[Temp1],   %[Temp1],   %[Temp6]    \n\t"
        "sw     %[Temp1],   504(%[z])               \n\t"
        "sw     %[Temp2],   508(%[z])               \n\t"
        "sw     %[Temp3],   256(%[z])               \n\t"
        "sw     %[Temp4],   260(%[z])               \n\t"

        : [Temp1]"=&r"(Temp1), [Temp2]"=&r"(Temp2),
          [Temp3]"=&r"(Temp3), [Temp4]"=&r"(Temp4),
          [Temp5]"=&r"(Temp5), [Temp6]"=&r"(Temp6),
          [z1]"+r"(z1), [z2]"+r"(z2), [z3]"+r"(z3)
        : [z4]"r"(z4), [z]"r"(z)
        : "memory"
    );
}

static void sbr_qmf_post_shuffle_mips(float W[32][2], const float *z)
{
    int Temp1, Temp2, Temp3, Temp4, Temp5;
    float *W_ptr = (float *)W;
    float *z1    = (float *)z;
    float *z2    = (float *)&z[60];
    float *z_end = z1 + 32;

     /* loop unrolled 4 times */
    __asm__ volatile (
        "lui    %[Temp5],   0x8000                  \n\t"
    "1:                                             \n\t"
        "lw     %[Temp1],   0(%[z2])                \n\t"
        "lw     %[Temp2],   4(%[z2])                \n\t"
        "lw     %[Temp3],   8(%[z2])                \n\t"
        "lw     %[Temp4],   12(%[z2])               \n\t"
        "xor    %[Temp1],   %[Temp1],   %[Temp5]    \n\t"
        "xor    %[Temp2],   %[Temp2],   %[Temp5]    \n\t"
        "xor    %[Temp3],   %[Temp3],   %[Temp5]    \n\t"
        "xor    %[Temp4],   %[Temp4],   %[Temp5]    \n\t"
        "addiu  %[z2],      %[z2],      -16         \n\t"
        "sw     %[Temp1],   24(%[W_ptr])            \n\t"
        "sw     %[Temp2],   16(%[W_ptr])            \n\t"
        "sw     %[Temp3],   8(%[W_ptr])             \n\t"
        "sw     %[Temp4],   0(%[W_ptr])             \n\t"
        "lw     %[Temp1],   0(%[z1])                \n\t"
        "lw     %[Temp2],   4(%[z1])                \n\t"
        "lw     %[Temp3],   8(%[z1])                \n\t"
        "lw     %[Temp4],   12(%[z1])               \n\t"
        "sw     %[Temp1],   4(%[W_ptr])             \n\t"
        "sw     %[Temp2],   12(%[W_ptr])            \n\t"
        "sw     %[Temp3],   20(%[W_ptr])            \n\t"
        "sw     %[Temp4],   28(%[W_ptr])            \n\t"
        "addiu  %[z1],      %[z1],      16          \n\t"
        "addiu  %[W_ptr],   %[W_ptr],   32          \n\t"
        "bne    %[z1],      %[z_end],   1b          \n\t"

        : [Temp1]"=&r"(Temp1), [Temp2]"=&r"(Temp2),
          [Temp3]"=&r"(Temp3), [Temp4]"=&r"(Temp4),
          [Temp5]"=&r"(Temp5), [z1]"+r"(z1),
          [z2]"+r"(z2), [W_ptr]"+r"(W_ptr)
        : [z_end]"r"(z_end)
        : "memory"
    );
}

#if HAVE_MIPSFPU
static void sbr_sum64x5_mips(float *z)
{
    int k;
    float *z1;
    float f1, f2, f3, f4, f5, f6, f7, f8;
    for (k = 0; k < 64; k += 8) {

        z1 = &z[k];

         /* loop unrolled 8 times */
        __asm__ volatile (
            "lwc1   $f0,    0(%[z1])        \n\t"
            "lwc1   $f1,    256(%[z1])      \n\t"
            "lwc1   $f2,    4(%[z1])        \n\t"
            "lwc1   $f3,    260(%[z1])      \n\t"
            "lwc1   $f4,    8(%[z1])        \n\t"
            "add.s  %[f1],  $f0,    $f1     \n\t"
            "lwc1   $f5,    264(%[z1])      \n\t"
            "add.s  %[f2],  $f2,    $f3     \n\t"
            "lwc1   $f6,    12(%[z1])       \n\t"
            "lwc1   $f7,    268(%[z1])      \n\t"
            "add.s  %[f3],  $f4,    $f5     \n\t"
            "lwc1   $f8,    16(%[z1])       \n\t"
            "lwc1   $f9,    272(%[z1])      \n\t"
            "add.s  %[f4],  $f6,    $f7     \n\t"
            "lwc1   $f10,   20(%[z1])       \n\t"
            "lwc1   $f11,   276(%[z1])      \n\t"
            "add.s  %[f5],  $f8,    $f9     \n\t"
            "lwc1   $f12,   24(%[z1])       \n\t"
            "lwc1   $f13,   280(%[z1])      \n\t"
            "add.s  %[f6],  $f10,   $f11    \n\t"
            "lwc1   $f14,   28(%[z1])       \n\t"
            "lwc1   $f15,   284(%[z1])      \n\t"
            "add.s  %[f7],  $f12,   $f13    \n\t"
            "lwc1   $f0,    512(%[z1])      \n\t"
            "lwc1   $f1,    516(%[z1])      \n\t"
            "add.s  %[f8],  $f14,   $f15    \n\t"
            "lwc1   $f2,    520(%[z1])      \n\t"
            "add.s  %[f1],  %[f1],  $f0     \n\t"
            "add.s  %[f2],  %[f2],  $f1     \n\t"
            "lwc1   $f3,    524(%[z1])      \n\t"
            "add.s  %[f3],  %[f3],  $f2     \n\t"
            "lwc1   $f4,    528(%[z1])      \n\t"
            "lwc1   $f5,    532(%[z1])      \n\t"
            "add.s  %[f4],  %[f4],  $f3     \n\t"
            "lwc1   $f6,    536(%[z1])      \n\t"
            "add.s  %[f5],  %[f5],  $f4     \n\t"
            "add.s  %[f6],  %[f6],  $f5     \n\t"
            "lwc1   $f7,    540(%[z1])      \n\t"
            "add.s  %[f7],  %[f7],  $f6     \n\t"
            "lwc1   $f0,    768(%[z1])      \n\t"
            "lwc1   $f1,    772(%[z1])      \n\t"
            "add.s  %[f8],  %[f8],  $f7     \n\t"
            "lwc1   $f2,    776(%[z1])      \n\t"
            "add.s  %[f1],  %[f1],  $f0     \n\t"
            "add.s  %[f2],  %[f2],  $f1     \n\t"
            "lwc1   $f3,    780(%[z1])      \n\t"
            "add.s  %[f3],  %[f3],  $f2     \n\t"
            "lwc1   $f4,    784(%[z1])      \n\t"
            "lwc1   $f5,    788(%[z1])      \n\t"
            "add.s  %[f4],  %[f4],  $f3     \n\t"
            "lwc1   $f6,    792(%[z1])      \n\t"
            "add.s  %[f5],  %[f5],  $f4     \n\t"
            "add.s  %[f6],  %[f6],  $f5     \n\t"
            "lwc1   $f7,    796(%[z1])      \n\t"
            "add.s  %[f7],  %[f7],  $f6     \n\t"
            "lwc1   $f0,    1024(%[z1])     \n\t"
            "lwc1   $f1,    1028(%[z1])     \n\t"
            "add.s  %[f8],  %[f8],  $f7     \n\t"
            "lwc1   $f2,    1032(%[z1])     \n\t"
            "add.s  %[f1],  %[f1],  $f0     \n\t"
            "add.s  %[f2],  %[f2],  $f1     \n\t"
            "lwc1   $f3,    1036(%[z1])     \n\t"
            "add.s  %[f3],  %[f3],  $f2     \n\t"
            "lwc1   $f4,    1040(%[z1])     \n\t"
            "lwc1   $f5,    1044(%[z1])     \n\t"
            "add.s  %[f4],  %[f4],  $f3     \n\t"
            "lwc1   $f6,    1048(%[z1])     \n\t"
            "add.s  %[f5],  %[f5],  $f4     \n\t"
            "add.s  %[f6],  %[f6],  $f5     \n\t"
            "lwc1   $f7,    1052(%[z1])     \n\t"
            "add.s  %[f7],  %[f7],  $f6     \n\t"
            "swc1   %[f1],  0(%[z1])        \n\t"
            "swc1   %[f2],  4(%[z1])        \n\t"
            "add.s  %[f8],  %[f8],  $f7     \n\t"
            "swc1   %[f3],  8(%[z1])        \n\t"
            "swc1   %[f4],  12(%[z1])       \n\t"
            "swc1   %[f5],  16(%[z1])       \n\t"
            "swc1   %[f6],  20(%[z1])       \n\t"
            "swc1   %[f7],  24(%[z1])       \n\t"
            "swc1   %[f8],  28(%[z1])       \n\t"

            : [f1]"=&f"(f1), [f2]"=&f"(f2), [f3]"=&f"(f3),
              [f4]"=&f"(f4), [f5]"=&f"(f5), [f6]"=&f"(f6),
              [f7]"=&f"(f7), [f8]"=&f"(f8)
            : [z1]"r"(z1)
            : "$f0", "$f1", "$f2", "$f3", "$f4", "$f5",
              "$f6", "$f7", "$f8", "$f9", "$f10", "$f11",
              "$f12", "$f13", "$f14", "$f15",
              "memory"
        );
    }
}

static float sbr_sum_square_mips(float (*x)[2], int n)
{
    float sum0 = 0.0f, sum1 = 0.0f;
    float *p_x;
    float temp0, temp1, temp2, temp3;
    float *loop_end;
    p_x = &x[0][0];
    loop_end = p_x + (n >> 1)*4 - 4;

    __asm__ volatile (
        ".set      push                                             \n\t"
        ".set      noreorder                                        \n\t"
        "lwc1      %[temp0],   0(%[p_x])                            \n\t"
        "lwc1      %[temp1],   4(%[p_x])                            \n\t"
        "lwc1      %[temp2],   8(%[p_x])                            \n\t"
        "lwc1      %[temp3],   12(%[p_x])                           \n\t"
    "1:                                                             \n\t"
        "addiu     %[p_x],     %[p_x],       16                     \n\t"
        "madd.s    %[sum0],    %[sum0],      %[temp0],   %[temp0]   \n\t"
        "lwc1      %[temp0],   0(%[p_x])                            \n\t"
        "madd.s    %[sum1],    %[sum1],      %[temp1],   %[temp1]   \n\t"
        "lwc1      %[temp1],   4(%[p_x])                            \n\t"
        "madd.s    %[sum0],    %[sum0],      %[temp2],   %[temp2]   \n\t"
        "lwc1      %[temp2],   8(%[p_x])                            \n\t"
        "madd.s    %[sum1],    %[sum1],      %[temp3],   %[temp3]   \n\t"
        "bne       %[p_x],     %[loop_end],  1b                     \n\t"
        " lwc1     %[temp3],   12(%[p_x])                           \n\t"
        "madd.s    %[sum0],    %[sum0],      %[temp0],   %[temp0]   \n\t"
        "madd.s    %[sum1],    %[sum1],      %[temp1],   %[temp1]   \n\t"
        "madd.s    %[sum0],    %[sum0],      %[temp2],   %[temp2]   \n\t"
        "madd.s    %[sum1],    %[sum1],      %[temp3],   %[temp3]   \n\t"
        ".set      pop                                              \n\t"

        : [temp0]"=&f"(temp0), [temp1]"=&f"(temp1), [temp2]"=&f"(temp2),
          [temp3]"=&f"(temp3), [sum0]"+f"(sum0), [sum1]"+f"(sum1),
          [p_x]"+r"(p_x)
        : [loop_end]"r"(loop_end)
        : "memory"
    );
    return sum0 + sum1;
}

static void sbr_qmf_deint_bfly_mips(float *v, const float *src0, const float *src1)
{
    int i;
    float temp0, temp1, temp2, temp3, temp4, temp5;
    float temp6, temp7, temp8, temp9, temp10, temp11;
    float *v0 = v;
    float *v1 = &v[127];
    float *psrc0 = (float*)src0;
    float *psrc1 = (float*)&src1[63];

    for (i = 0; i < 4; i++) {

         /* loop unrolled 16 times */
        __asm__ volatile(
            "lwc1       %[temp0],   0(%[src0])             \n\t"
            "lwc1       %[temp1],   0(%[src1])             \n\t"
            "lwc1       %[temp3],   4(%[src0])             \n\t"
            "lwc1       %[temp4],   -4(%[src1])            \n\t"
            "lwc1       %[temp6],   8(%[src0])             \n\t"
            "lwc1       %[temp7],   -8(%[src1])            \n\t"
            "lwc1       %[temp9],   12(%[src0])            \n\t"
            "lwc1       %[temp10],  -12(%[src1])           \n\t"
            "add.s      %[temp2],   %[temp0],   %[temp1]   \n\t"
            "add.s      %[temp5],   %[temp3],   %[temp4]   \n\t"
            "add.s      %[temp8],   %[temp6],   %[temp7]   \n\t"
            "add.s      %[temp11],  %[temp9],   %[temp10]  \n\t"
            "sub.s      %[temp0],   %[temp0],   %[temp1]   \n\t"
            "sub.s      %[temp3],   %[temp3],   %[temp4]   \n\t"
            "sub.s      %[temp6],   %[temp6],   %[temp7]   \n\t"
            "sub.s      %[temp9],   %[temp9],   %[temp10]  \n\t"
            "swc1       %[temp2],   0(%[v1])               \n\t"
            "swc1       %[temp0],   0(%[v0])               \n\t"
            "swc1       %[temp5],   -4(%[v1])              \n\t"
            "swc1       %[temp3],   4(%[v0])               \n\t"
            "swc1       %[temp8],   -8(%[v1])              \n\t"
            "swc1       %[temp6],   8(%[v0])               \n\t"
            "swc1       %[temp11],  -12(%[v1])             \n\t"
            "swc1       %[temp9],   12(%[v0])              \n\t"
            "lwc1       %[temp0],   16(%[src0])            \n\t"
            "lwc1       %[temp1],   -16(%[src1])           \n\t"
            "lwc1       %[temp3],   20(%[src0])            \n\t"
            "lwc1       %[temp4],   -20(%[src1])           \n\t"
            "lwc1       %[temp6],   24(%[src0])            \n\t"
            "lwc1       %[temp7],   -24(%[src1])           \n\t"
            "lwc1       %[temp9],   28(%[src0])            \n\t"
            "lwc1       %[temp10],  -28(%[src1])           \n\t"
            "add.s      %[temp2],   %[temp0],   %[temp1]   \n\t"
            "add.s      %[temp5],   %[temp3],   %[temp4]   \n\t"
            "add.s      %[temp8],   %[temp6],   %[temp7]   \n\t"
            "add.s      %[temp11],  %[temp9],   %[temp10]  \n\t"
            "sub.s      %[temp0],   %[temp0],   %[temp1]   \n\t"
            "sub.s      %[temp3],   %[temp3],   %[temp4]   \n\t"
            "sub.s      %[temp6],   %[temp6],   %[temp7]   \n\t"
            "sub.s      %[temp9],   %[temp9],   %[temp10]  \n\t"
            "swc1       %[temp2],   -16(%[v1])             \n\t"
            "swc1       %[temp0],   16(%[v0])              \n\t"
            "swc1       %[temp5],   -20(%[v1])             \n\t"
            "swc1       %[temp3],   20(%[v0])              \n\t"
            "swc1       %[temp8],   -24(%[v1])             \n\t"
            "swc1       %[temp6],   24(%[v0])              \n\t"
            "swc1       %[temp11],  -28(%[v1])             \n\t"
            "swc1       %[temp9],   28(%[v0])              \n\t"
            "lwc1       %[temp0],   32(%[src0])            \n\t"
            "lwc1       %[temp1],   -32(%[src1])           \n\t"
            "lwc1       %[temp3],   36(%[src0])            \n\t"
            "lwc1       %[temp4],   -36(%[src1])           \n\t"
            "lwc1       %[temp6],   40(%[src0])            \n\t"
            "lwc1       %[temp7],   -40(%[src1])           \n\t"
            "lwc1       %[temp9],   44(%[src0])            \n\t"
            "lwc1       %[temp10],  -44(%[src1])           \n\t"
            "add.s      %[temp2],   %[temp0],   %[temp1]   \n\t"
            "add.s      %[temp5],   %[temp3],   %[temp4]   \n\t"
            "add.s      %[temp8],   %[temp6],   %[temp7]   \n\t"
            "add.s      %[temp11],  %[temp9],   %[temp10]  \n\t"
            "sub.s      %[temp0],   %[temp0],   %[temp1]   \n\t"
            "sub.s      %[temp3],   %[temp3],   %[temp4]   \n\t"
            "sub.s      %[temp6],   %[temp6],   %[temp7]   \n\t"
            "sub.s      %[temp9],   %[temp9],   %[temp10]  \n\t"
            "swc1       %[temp2],   -32(%[v1])             \n\t"
            "swc1       %[temp0],   32(%[v0])              \n\t"
            "swc1       %[temp5],   -36(%[v1])             \n\t"
            "swc1       %[temp3],   36(%[v0])              \n\t"
            "swc1       %[temp8],   -40(%[v1])             \n\t"
            "swc1       %[temp6],   40(%[v0])              \n\t"
            "swc1       %[temp11],  -44(%[v1])             \n\t"
            "swc1       %[temp9],   44(%[v0])              \n\t"
            "lwc1       %[temp0],   48(%[src0])            \n\t"
            "lwc1       %[temp1],   -48(%[src1])           \n\t"
            "lwc1       %[temp3],   52(%[src0])            \n\t"
            "lwc1       %[temp4],   -52(%[src1])           \n\t"
            "lwc1       %[temp6],   56(%[src0])            \n\t"
            "lwc1       %[temp7],   -56(%[src1])           \n\t"
            "lwc1       %[temp9],   60(%[src0])            \n\t"
            "lwc1       %[temp10],  -60(%[src1])           \n\t"
            "add.s      %[temp2],   %[temp0],   %[temp1]   \n\t"
            "add.s      %[temp5],   %[temp3],   %[temp4]   \n\t"
            "add.s      %[temp8],   %[temp6],   %[temp7]   \n\t"
            "add.s      %[temp11],  %[temp9],   %[temp10]  \n\t"
            "sub.s      %[temp0],   %[temp0],   %[temp1]   \n\t"
            "sub.s      %[temp3],   %[temp3],   %[temp4]   \n\t"
            "sub.s      %[temp6],   %[temp6],   %[temp7]   \n\t"
            "sub.s      %[temp9],   %[temp9],   %[temp10]  \n\t"
            "swc1       %[temp2],   -48(%[v1])             \n\t"
            "swc1       %[temp0],   48(%[v0])              \n\t"
            "swc1       %[temp5],   -52(%[v1])             \n\t"
            "swc1       %[temp3],   52(%[v0])              \n\t"
            "swc1       %[temp8],   -56(%[v1])             \n\t"
            "swc1       %[temp6],   56(%[v0])              \n\t"
            "swc1       %[temp11],  -60(%[v1])             \n\t"
            "swc1       %[temp9],   60(%[v0])              \n\t"
            "addiu      %[src0],    %[src0],    64         \n\t"
            "addiu      %[src1],    %[src1],    -64        \n\t"
            "addiu      %[v0],      %[v0],      64         \n\t"
            "addiu      %[v1],      %[v1],      -64        \n\t"

            : [v0]"+r"(v0), [v1]"+r"(v1), [src0]"+r"(psrc0), [src1]"+r"(psrc1),
              [temp0]"=&f"(temp0), [temp1]"=&f"(temp1), [temp2]"=&f"(temp2),
              [temp3]"=&f"(temp3), [temp4]"=&f"(temp4), [temp5]"=&f"(temp5),
              [temp6]"=&f"(temp6), [temp7]"=&f"(temp7), [temp8]"=&f"(temp8),
              [temp9]"=&f"(temp9), [temp10]"=&f"(temp10), [temp11]"=&f"(temp11)
            :
            :"memory"
        );
    }
}

static void sbr_autocorrelate_mips(const float x[40][2], float phi[3][2][2])
{
    int i;
    float real_sum_0 = 0.0f;
    float real_sum_1 = 0.0f;
    float real_sum_2 = 0.0f;
    float imag_sum_1 = 0.0f;
    float imag_sum_2 = 0.0f;
    float *p_x, *p_phi;
    float temp0, temp1, temp2, temp3, temp4, temp5, temp6;
    float temp7, temp_r, temp_r1, temp_r2, temp_r3, temp_r4;
    p_x = (float*)&x[0][0];
    p_phi = &phi[0][0][0];

    __asm__ volatile (
        "lwc1    %[temp0],      8(%[p_x])                           \n\t"
        "lwc1    %[temp1],      12(%[p_x])                          \n\t"
        "lwc1    %[temp2],      16(%[p_x])                          \n\t"
        "lwc1    %[temp3],      20(%[p_x])                          \n\t"
        "lwc1    %[temp4],      24(%[p_x])                          \n\t"
        "lwc1    %[temp5],      28(%[p_x])                          \n\t"
        "mul.s   %[temp_r],     %[temp1],      %[temp1]             \n\t"
        "mul.s   %[temp_r1],    %[temp1],      %[temp3]             \n\t"
        "mul.s   %[temp_r2],    %[temp1],      %[temp2]             \n\t"
        "mul.s   %[temp_r3],    %[temp1],      %[temp5]             \n\t"
        "mul.s   %[temp_r4],    %[temp1],      %[temp4]             \n\t"
        "madd.s  %[temp_r],     %[temp_r],     %[temp0],  %[temp0]  \n\t"
        "madd.s  %[temp_r1],    %[temp_r1],    %[temp0],  %[temp2]  \n\t"
        "msub.s  %[temp_r2],    %[temp_r2],    %[temp0],  %[temp3]  \n\t"
        "madd.s  %[temp_r3],    %[temp_r3],    %[temp0],  %[temp4]  \n\t"
        "msub.s  %[temp_r4],    %[temp_r4],    %[temp0],  %[temp5]  \n\t"
        "add.s   %[real_sum_0], %[real_sum_0], %[temp_r]            \n\t"
        "add.s   %[real_sum_1], %[real_sum_1], %[temp_r1]           \n\t"
        "add.s   %[imag_sum_1], %[imag_sum_1], %[temp_r2]           \n\t"
        "add.s   %[real_sum_2], %[real_sum_2], %[temp_r3]           \n\t"
        "add.s   %[imag_sum_2], %[imag_sum_2], %[temp_r4]           \n\t"
        "addiu   %[p_x],        %[p_x],        8                    \n\t"

        : [temp0]"=&f"(temp0), [temp1]"=&f"(temp1), [temp2]"=&f"(temp2),
          [temp3]"=&f"(temp3), [temp4]"=&f"(temp4), [temp5]"=&f"(temp5),
          [real_sum_0]"+f"(real_sum_0), [real_sum_1]"+f"(real_sum_1),
          [imag_sum_1]"+f"(imag_sum_1), [real_sum_2]"+f"(real_sum_2),
          [temp_r]"=&f"(temp_r), [temp_r1]"=&f"(temp_r1), [temp_r2]"=&f"(temp_r2),
          [temp_r3]"=&f"(temp_r3), [temp_r4]"=&f"(temp_r4),
          [p_x]"+r"(p_x), [imag_sum_2]"+f"(imag_sum_2)
        :
        : "memory"
    );

    for (i = 0; i < 12; i++) {
        __asm__ volatile (
            "lwc1    %[temp0],      8(%[p_x])                           \n\t"
            "lwc1    %[temp1],      12(%[p_x])                          \n\t"
            "lwc1    %[temp2],      16(%[p_x])                          \n\t"
            "lwc1    %[temp3],      20(%[p_x])                          \n\t"
            "lwc1    %[temp4],      24(%[p_x])                          \n\t"
            "lwc1    %[temp5],      28(%[p_x])                          \n\t"
            "mul.s   %[temp_r],     %[temp1],      %[temp1]             \n\t"
            "mul.s   %[temp_r1],    %[temp1],      %[temp3]             \n\t"
            "mul.s   %[temp_r2],    %[temp1],      %[temp2]             \n\t"
            "mul.s   %[temp_r3],    %[temp1],      %[temp5]             \n\t"
            "mul.s   %[temp_r4],    %[temp1],      %[temp4]             \n\t"
            "madd.s  %[temp_r],     %[temp_r],     %[temp0],  %[temp0]  \n\t"
            "madd.s  %[temp_r1],    %[temp_r1],    %[temp0],  %[temp2]  \n\t"
            "msub.s  %[temp_r2],    %[temp_r2],    %[temp0],  %[temp3]  \n\t"
            "madd.s  %[temp_r3],    %[temp_r3],    %[temp0],  %[temp4]  \n\t"
            "msub.s  %[temp_r4],    %[temp_r4],    %[temp0],  %[temp5]  \n\t"
            "add.s   %[real_sum_0], %[real_sum_0], %[temp_r]            \n\t"
            "add.s   %[real_sum_1], %[real_sum_1], %[temp_r1]           \n\t"
            "add.s   %[imag_sum_1], %[imag_sum_1], %[temp_r2]           \n\t"
            "add.s   %[real_sum_2], %[real_sum_2], %[temp_r3]           \n\t"
            "add.s   %[imag_sum_2], %[imag_sum_2], %[temp_r4]           \n\t"
            "lwc1    %[temp0],      32(%[p_x])                          \n\t"
            "lwc1    %[temp1],      36(%[p_x])                          \n\t"
            "mul.s   %[temp_r],     %[temp3],      %[temp3]             \n\t"
            "mul.s   %[temp_r1],    %[temp3],      %[temp5]             \n\t"
            "mul.s   %[temp_r2],    %[temp3],      %[temp4]             \n\t"
            "mul.s   %[temp_r3],    %[temp3],      %[temp1]             \n\t"
            "mul.s   %[temp_r4],    %[temp3],      %[temp0]             \n\t"
            "madd.s  %[temp_r],     %[temp_r],     %[temp2],  %[temp2]  \n\t"
            "madd.s  %[temp_r1],    %[temp_r1],    %[temp2],  %[temp4]  \n\t"
            "msub.s  %[temp_r2],    %[temp_r2],    %[temp2],  %[temp5]  \n\t"
            "madd.s  %[temp_r3],    %[temp_r3],    %[temp2],  %[temp0]  \n\t"
            "msub.s  %[temp_r4],    %[temp_r4],    %[temp2],  %[temp1]  \n\t"
            "add.s   %[real_sum_0], %[real_sum_0], %[temp_r]            \n\t"
            "add.s   %[real_sum_1], %[real_sum_1], %[temp_r1]           \n\t"
            "add.s   %[imag_sum_1], %[imag_sum_1], %[temp_r2]           \n\t"
            "add.s   %[real_sum_2], %[real_sum_2], %[temp_r3]           \n\t"
            "add.s   %[imag_sum_2], %[imag_sum_2], %[temp_r4]           \n\t"
            "lwc1    %[temp2],      40(%[p_x])                          \n\t"
            "lwc1    %[temp3],      44(%[p_x])                          \n\t"
            "mul.s   %[temp_r],     %[temp5],      %[temp5]             \n\t"
            "mul.s   %[temp_r1],    %[temp5],      %[temp1]             \n\t"
            "mul.s   %[temp_r2],    %[temp5],      %[temp0]             \n\t"
            "mul.s   %[temp_r3],    %[temp5],      %[temp3]             \n\t"
            "mul.s   %[temp_r4],    %[temp5],      %[temp2]             \n\t"
            "madd.s  %[temp_r],     %[temp_r],     %[temp4],  %[temp4]  \n\t"
            "madd.s  %[temp_r1],    %[temp_r1],    %[temp4],  %[temp0]  \n\t"
            "msub.s  %[temp_r2],    %[temp_r2],    %[temp4],  %[temp1]  \n\t"
            "madd.s  %[temp_r3],    %[temp_r3],    %[temp4],  %[temp2]  \n\t"
            "msub.s  %[temp_r4],    %[temp_r4],    %[temp4],  %[temp3]  \n\t"
            "add.s   %[real_sum_0], %[real_sum_0], %[temp_r]            \n\t"
            "add.s   %[real_sum_1], %[real_sum_1], %[temp_r1]           \n\t"
            "add.s   %[imag_sum_1], %[imag_sum_1], %[temp_r2]           \n\t"
            "add.s   %[real_sum_2], %[real_sum_2], %[temp_r3]           \n\t"
            "add.s   %[imag_sum_2], %[imag_sum_2], %[temp_r4]           \n\t"
            "addiu   %[p_x],        %[p_x],        24                   \n\t"

            : [temp0]"=&f"(temp0), [temp1]"=&f"(temp1), [temp2]"=&f"(temp2),
              [temp3]"=&f"(temp3), [temp4]"=&f"(temp4), [temp5]"=&f"(temp5),
              [real_sum_0]"+f"(real_sum_0), [real_sum_1]"+f"(real_sum_1),
              [imag_sum_1]"+f"(imag_sum_1), [real_sum_2]"+f"(real_sum_2),
              [temp_r]"=&f"(temp_r), [temp_r1]"=&f"(temp_r1),
              [temp_r2]"=&f"(temp_r2), [temp_r3]"=&f"(temp_r3),
              [temp_r4]"=&f"(temp_r4), [p_x]"+r"(p_x),
              [imag_sum_2]"+f"(imag_sum_2)
            :
            : "memory"
        );
    }
    __asm__ volatile (
        "lwc1    %[temp0],    -296(%[p_x])                        \n\t"
        "lwc1    %[temp1],    -292(%[p_x])                        \n\t"
        "lwc1    %[temp2],    8(%[p_x])                           \n\t"
        "lwc1    %[temp3],    12(%[p_x])                          \n\t"
        "lwc1    %[temp4],    -288(%[p_x])                        \n\t"
        "lwc1    %[temp5],    -284(%[p_x])                        \n\t"
        "lwc1    %[temp6],    -280(%[p_x])                        \n\t"
        "lwc1    %[temp7],    -276(%[p_x])                        \n\t"
        "madd.s  %[temp_r],   %[real_sum_0], %[temp0],  %[temp0]  \n\t"
        "madd.s  %[temp_r1],  %[real_sum_0], %[temp2],  %[temp2]  \n\t"
        "madd.s  %[temp_r2],  %[real_sum_1], %[temp0],  %[temp4]  \n\t"
        "madd.s  %[temp_r3],  %[imag_sum_1], %[temp0],  %[temp5]  \n\t"
        "madd.s  %[temp_r],   %[temp_r],     %[temp1],  %[temp1]  \n\t"
        "madd.s  %[temp_r1],  %[temp_r1],    %[temp3],  %[temp3]  \n\t"
        "madd.s  %[temp_r2],  %[temp_r2],    %[temp1],  %[temp5]  \n\t"
        "nmsub.s  %[temp_r3], %[temp_r3],    %[temp1],  %[temp4]  \n\t"
        "lwc1    %[temp4],    16(%[p_x])                          \n\t"
        "lwc1    %[temp5],    20(%[p_x])                          \n\t"
        "swc1    %[temp_r],   40(%[p_phi])                        \n\t"
        "swc1    %[temp_r1],  16(%[p_phi])                        \n\t"
        "swc1    %[temp_r2],  24(%[p_phi])                        \n\t"
        "swc1    %[temp_r3],  28(%[p_phi])                        \n\t"
        "madd.s  %[temp_r],   %[real_sum_1], %[temp2],  %[temp4]  \n\t"
        "madd.s  %[temp_r1],  %[imag_sum_1], %[temp2],  %[temp5]  \n\t"
        "madd.s  %[temp_r2],  %[real_sum_2], %[temp0],  %[temp6]  \n\t"
        "madd.s  %[temp_r3],  %[imag_sum_2], %[temp0],  %[temp7]  \n\t"
        "madd.s  %[temp_r],   %[temp_r],     %[temp3],  %[temp5]  \n\t"
        "nmsub.s %[temp_r1],  %[temp_r1],    %[temp3],  %[temp4]  \n\t"
        "madd.s  %[temp_r2],  %[temp_r2],    %[temp1],  %[temp7]  \n\t"
        "nmsub.s %[temp_r3],  %[temp_r3],    %[temp1],  %[temp6]  \n\t"
        "swc1    %[temp_r],   0(%[p_phi])                         \n\t"
        "swc1    %[temp_r1],  4(%[p_phi])                         \n\t"
        "swc1    %[temp_r2],  8(%[p_phi])                         \n\t"
        "swc1    %[temp_r3],  12(%[p_phi])                        \n\t"

        : [temp0]"=&f"(temp0), [temp1]"=&f"(temp1), [temp2]"=&f"(temp2),
          [temp3]"=&f"(temp3), [temp4]"=&f"(temp4), [temp5]"=&f"(temp5),
          [temp6]"=&f"(temp6), [temp7]"=&f"(temp7), [temp_r]"=&f"(temp_r),
          [real_sum_0]"+f"(real_sum_0), [real_sum_1]"+f"(real_sum_1),
          [real_sum_2]"+f"(real_sum_2), [imag_sum_1]"+f"(imag_sum_1),
          [temp_r2]"=&f"(temp_r2), [temp_r3]"=&f"(temp_r3),
          [temp_r1]"=&f"(temp_r1), [p_phi]"+r"(p_phi),
          [imag_sum_2]"+f"(imag_sum_2)
        : [p_x]"r"(p_x)
        : "memory"
    );
}

static void sbr_hf_gen_mips(float (*X_high)[2], const float (*X_low)[2],
                         const float alpha0[2], const float alpha1[2],
                         float bw, int start, int end)
{
    float alpha[4];
    int i;
    float *p_x_low = (float*)&X_low[0][0] + 2*start;
    float *p_x_high = &X_high[0][0] + 2*start;
    float temp0, temp1, temp2, temp3, temp4, temp5, temp6;
    float temp7, temp8, temp9, temp10, temp11, temp12;

    alpha[0] = alpha1[0] * bw * bw;
    alpha[1] = alpha1[1] * bw * bw;
    alpha[2] = alpha0[0] * bw;
    alpha[3] = alpha0[1] * bw;

    for (i = start; i < end; i++) {
        __asm__ volatile (
            "lwc1    %[temp0],    -16(%[p_x_low])                        \n\t"
            "lwc1    %[temp1],    -12(%[p_x_low])                        \n\t"
            "lwc1    %[temp2],    -8(%[p_x_low])                         \n\t"
            "lwc1    %[temp3],    -4(%[p_x_low])                         \n\t"
            "lwc1    %[temp5],    0(%[p_x_low])                          \n\t"
            "lwc1    %[temp6],    4(%[p_x_low])                          \n\t"
            "lwc1    %[temp7],    0(%[alpha])                            \n\t"
            "lwc1    %[temp8],    4(%[alpha])                            \n\t"
            "lwc1    %[temp9],    8(%[alpha])                            \n\t"
            "lwc1    %[temp10],   12(%[alpha])                           \n\t"
            "addiu   %[p_x_high], %[p_x_high],     8                     \n\t"
            "addiu   %[p_x_low],  %[p_x_low],      8                     \n\t"
            "mul.s   %[temp11],   %[temp1],        %[temp8]              \n\t"
            "msub.s  %[temp11],   %[temp11],       %[temp0],  %[temp7]   \n\t"
            "madd.s  %[temp11],   %[temp11],       %[temp2],  %[temp9]   \n\t"
            "nmsub.s %[temp11],   %[temp11],       %[temp3],  %[temp10]  \n\t"
            "add.s   %[temp11],   %[temp11],       %[temp5]              \n\t"
            "swc1    %[temp11],   -8(%[p_x_high])                        \n\t"
            "mul.s   %[temp12],   %[temp1],        %[temp7]              \n\t"
            "madd.s  %[temp12],   %[temp12],       %[temp0],  %[temp8]   \n\t"
            "madd.s  %[temp12],   %[temp12],       %[temp3],  %[temp9]   \n\t"
            "madd.s  %[temp12],   %[temp12],       %[temp2],  %[temp10]  \n\t"
            "add.s   %[temp12],   %[temp12],       %[temp6]              \n\t"
            "swc1    %[temp12],   -4(%[p_x_high])                        \n\t"

            : [temp0]"=&f"(temp0), [temp1]"=&f"(temp1), [temp2]"=&f"(temp2),
              [temp3]"=&f"(temp3), [temp4]"=&f"(temp4), [temp5]"=&f"(temp5),
              [temp6]"=&f"(temp6), [temp7]"=&f"(temp7), [temp8]"=&f"(temp8),
              [temp9]"=&f"(temp9), [temp10]"=&f"(temp10), [temp11]"=&f"(temp11),
              [temp12]"=&f"(temp12), [p_x_high]"+r"(p_x_high),
              [p_x_low]"+r"(p_x_low)
            : [alpha]"r"(alpha)
            : "memory"
        );
    }
}

static void sbr_hf_g_filt_mips(float (*Y)[2], const float (*X_high)[40][2],
                            const float *g_filt, int m_max, intptr_t ixh)
{
    float *p_y, *p_x, *p_g;
    float temp0, temp1, temp2;
    int loop_end;

    p_g = (float*)&g_filt[0];
    p_y = &Y[0][0];
    p_x = (float*)&X_high[0][ixh][0];
    loop_end = (int)((int*)p_g + m_max);

    __asm__ volatile(
        ".set    push                                \n\t"
        ".set    noreorder                           \n\t"
    "1:                                              \n\t"
        "lwc1    %[temp0],   0(%[p_g])               \n\t"
        "lwc1    %[temp1],   0(%[p_x])               \n\t"
        "lwc1    %[temp2],   4(%[p_x])               \n\t"
        "mul.s   %[temp1],   %[temp1],     %[temp0]  \n\t"
        "mul.s   %[temp2],   %[temp2],     %[temp0]  \n\t"
        "addiu   %[p_g],     %[p_g],       4         \n\t"
        "addiu   %[p_x],     %[p_x],       320       \n\t"
        "swc1    %[temp1],   0(%[p_y])               \n\t"
        "swc1    %[temp2],   4(%[p_y])               \n\t"
        "bne     %[p_g],     %[loop_end],  1b        \n\t"
        " addiu  %[p_y],     %[p_y],       8         \n\t"
        ".set    pop                                 \n\t"

        : [temp0]"=&f"(temp0), [temp1]"=&f"(temp1),
          [temp2]"=&f"(temp2), [p_x]"+r"(p_x),
          [p_y]"+r"(p_y), [p_g]"+r"(p_g)
        : [loop_end]"r"(loop_end)
        : "memory"
    );
}

static void sbr_hf_apply_noise_0_mips(float (*Y)[2], const float *s_m,
                                 const float *q_filt, int noise,
                                 int kx, int m_max)
{
    int m;

    for (m = 0; m < m_max; m++){

        float *Y1=&Y[m][0];
        float *ff_table;
        float y0,y1, temp1, temp2, temp4, temp5;
        int temp0, temp3;
        const float *s_m1=&s_m[m];
        const float *q_filt1= &q_filt[m];

        __asm__ volatile(
            "lwc1    %[y0],       0(%[Y1])                                    \n\t"
            "lwc1    %[temp1],    0(%[s_m1])                                  \n\t"
            "addiu   %[noise],    %[noise],              1                    \n\t"
            "andi    %[noise],    %[noise],              0x1ff                \n\t"
            "sll     %[temp0],    %[noise], 3                                 \n\t"
            "addu    %[ff_table], %[ff_sbr_noise_table], %[temp0]             \n\t"
            "add.s   %[y0],       %[y0],                 %[temp1]             \n\t"
            "mfc1    %[temp3],    %[temp1]                                    \n\t"
            "bne     %[temp3],    $0,                    1f                   \n\t"
            "lwc1    %[y1],       4(%[Y1])                                    \n\t"
            "lwc1    %[temp2],    0(%[q_filt1])                               \n\t"
            "lwc1    %[temp4],    0(%[ff_table])                              \n\t"
            "lwc1    %[temp5],    4(%[ff_table])                              \n\t"
            "madd.s  %[y0],       %[y0],                 %[temp2],  %[temp4]  \n\t"
            "madd.s  %[y1],       %[y1],                 %[temp2],  %[temp5]  \n\t"
            "swc1    %[y1],       4(%[Y1])                                    \n\t"
        "1:                                                                   \n\t"
            "swc1    %[y0],       0(%[Y1])                                    \n\t"

            : [ff_table]"=&r"(ff_table), [y0]"=&f"(y0), [y1]"=&f"(y1),
              [temp0]"=&r"(temp0), [temp1]"=&f"(temp1), [temp2]"=&f"(temp2),
              [temp3]"=&r"(temp3), [temp4]"=&f"(temp4), [temp5]"=&f"(temp5)
            : [ff_sbr_noise_table]"r"(ff_sbr_noise_table), [noise]"r"(noise),
              [Y1]"r"(Y1), [s_m1]"r"(s_m1), [q_filt1]"r"(q_filt1)
            : "memory"
        );
    }
}

static void sbr_hf_apply_noise_1_mips(float (*Y)[2], const float *s_m,
                                 const float *q_filt, int noise,
                                 int kx, int m_max)
{
    float y0,y1,temp1, temp2, temp4, temp5;
    int temp0, temp3, m;
    float phi_sign = 1 - 2 * (kx & 1);

    for (m = 0; m < m_max; m++) {

        float *ff_table;
        float *Y1=&Y[m][0];
        const float *s_m1=&s_m[m];
        const float *q_filt1= &q_filt[m];

        __asm__ volatile(
            "lwc1   %[y1],       4(%[Y1])                                     \n\t"
            "lwc1   %[temp1],    0(%[s_m1])                                   \n\t"
            "lw     %[temp3],    0(%[s_m1])                                   \n\t"
            "addiu  %[noise],    %[noise],               1                    \n\t"
            "andi   %[noise],    %[noise],               0x1ff                \n\t"
            "sll    %[temp0],    %[noise],               3                    \n\t"
            "addu   %[ff_table], %[ff_sbr_noise_table], %[temp0]              \n\t"
            "madd.s %[y1],       %[y1],                 %[temp1], %[phi_sign] \n\t"
            "bne    %[temp3],    $0,                    1f                    \n\t"
            "lwc1   %[y0],       0(%[Y1])                                     \n\t"
            "lwc1   %[temp2],    0(%[q_filt1])                                \n\t"
            "lwc1   %[temp4],    0(%[ff_table])                               \n\t"
            "lwc1   %[temp5],    4(%[ff_table])                               \n\t"
            "madd.s %[y0],       %[y0],                 %[temp2], %[temp4]    \n\t"
            "madd.s %[y1],       %[y1],                 %[temp2], %[temp5]    \n\t"
            "swc1   %[y0],       0(%[Y1])                                     \n\t"
        "1:                                                                   \n\t"
            "swc1   %[y1],       4(%[Y1])                                     \n\t"

            : [ff_table] "=&r" (ff_table), [y0] "=&f" (y0), [y1] "=&f" (y1),
              [temp0] "=&r" (temp0), [temp1] "=&f" (temp1), [temp2] "=&f" (temp2),
              [temp3] "=&r" (temp3), [temp4] "=&f" (temp4), [temp5] "=&f" (temp5)
            : [ff_sbr_noise_table] "r" (ff_sbr_noise_table), [noise] "r" (noise),
              [Y1] "r" (Y1), [s_m1] "r" (s_m1), [q_filt1] "r" (q_filt1),
              [phi_sign] "f" (phi_sign)
            : "memory"
        );
        phi_sign = -phi_sign;
    }
}

static void sbr_hf_apply_noise_2_mips(float (*Y)[2], const float *s_m,
                                 const float *q_filt, int noise,
                                 int kx, int m_max)
{
    int m;
    float *ff_table;
    float y0,y1, temp0, temp1, temp2, temp3, temp4, temp5;

    for (m = 0; m < m_max; m++) {

        float *Y1=&Y[m][0];
        const float *s_m1=&s_m[m];
        const float *q_filt1= &q_filt[m];

        __asm__ volatile(
            "lwc1   %[y0],       0(%[Y1])                                  \n\t"
            "lwc1   %[temp1],    0(%[s_m1])                                \n\t"
            "addiu  %[noise],    %[noise],              1                  \n\t"
            "andi   %[noise],    %[noise],              0x1ff              \n\t"
            "sll    %[temp0],    %[noise],              3                  \n\t"
            "addu   %[ff_table], %[ff_sbr_noise_table], %[temp0]           \n\t"
            "sub.s  %[y0],       %[y0],                 %[temp1]           \n\t"
            "mfc1   %[temp3],    %[temp1]                                  \n\t"
            "bne    %[temp3],    $0,                    1f                 \n\t"
            "lwc1   %[y1],       4(%[Y1])                                  \n\t"
            "lwc1   %[temp2],    0(%[q_filt1])                             \n\t"
            "lwc1   %[temp4],    0(%[ff_table])                            \n\t"
            "lwc1   %[temp5],    4(%[ff_table])                            \n\t"
            "madd.s %[y0],       %[y0],                 %[temp2], %[temp4] \n\t"
            "madd.s %[y1],       %[y1],                 %[temp2], %[temp5] \n\t"
            "swc1   %[y1],       4(%[Y1])                                  \n\t"
        "1:                                                                \n\t"
            "swc1   %[y0],       0(%[Y1])                                  \n\t"

            : [temp0]"=&r"(temp0), [ff_table]"=&r"(ff_table), [y0]"=&f"(y0),
              [y1]"=&f"(y1), [temp1]"=&f"(temp1), [temp2]"=&f"(temp2),
              [temp3]"=&r"(temp3), [temp4]"=&f"(temp4), [temp5]"=&f"(temp5)
            : [ff_sbr_noise_table]"r"(ff_sbr_noise_table), [noise]"r"(noise),
              [Y1]"r"(Y1), [s_m1]"r"(s_m1), [q_filt1]"r"(q_filt1)
            : "memory"
        );
    }
}

static void sbr_hf_apply_noise_3_mips(float (*Y)[2], const float *s_m,
                                 const float *q_filt, int noise,
                                 int kx, int m_max)
{
    float phi_sign = 1 - 2 * (kx & 1);
    int m;

    for (m = 0; m < m_max; m++) {

        float *Y1=&Y[m][0];
        float *ff_table;
        float y0,y1, temp1, temp2, temp4, temp5;
        int temp0, temp3;
        const float *s_m1=&s_m[m];
        const float *q_filt1= &q_filt[m];

        __asm__ volatile(
            "lwc1    %[y1],       4(%[Y1])                                     \n\t"
            "lwc1    %[temp1],    0(%[s_m1])                                   \n\t"
            "addiu   %[noise],    %[noise],              1                     \n\t"
            "andi    %[noise],    %[noise],              0x1ff                 \n\t"
            "sll     %[temp0],    %[noise],              3                     \n\t"
            "addu    %[ff_table], %[ff_sbr_noise_table], %[temp0]              \n\t"
            "nmsub.s %[y1],       %[y1],                 %[temp1], %[phi_sign] \n\t"
            "mfc1    %[temp3],    %[temp1]                                     \n\t"
            "bne     %[temp3],    $0,                    1f                    \n\t"
            "lwc1    %[y0],       0(%[Y1])                                     \n\t"
            "lwc1    %[temp2],    0(%[q_filt1])                                \n\t"
            "lwc1    %[temp4],    0(%[ff_table])                               \n\t"
            "lwc1    %[temp5],    4(%[ff_table])                               \n\t"
            "madd.s  %[y0],       %[y0],                 %[temp2], %[temp4]    \n\t"
            "madd.s  %[y1],       %[y1],                 %[temp2], %[temp5]    \n\t"
            "swc1    %[y0],       0(%[Y1])                                     \n\t"
            "1:                                                                \n\t"
            "swc1    %[y1],       4(%[Y1])                                     \n\t"

            : [ff_table]"=&r"(ff_table), [y0]"=&f"(y0), [y1]"=&f"(y1),
              [temp0]"=&r"(temp0), [temp1]"=&f"(temp1), [temp2]"=&f"(temp2),
              [temp3]"=&r"(temp3), [temp4]"=&f"(temp4), [temp5]"=&f"(temp5)
            : [ff_sbr_noise_table]"r"(ff_sbr_noise_table), [noise]"r"(noise),
              [Y1]"r"(Y1), [s_m1]"r"(s_m1), [q_filt1]"r"(q_filt1),
              [phi_sign]"f"(phi_sign)
            : "memory"
        );
       phi_sign = -phi_sign;
    }
}
#endif /* HAVE_MIPSFPU */
#endif /* HAVE_INLINE_ASM */

void ff_sbrdsp_init_mips(SBRDSPContext *s)
{
#if HAVE_INLINE_ASM
    s->neg_odd_64 = sbr_neg_odd_64_mips;
    s->qmf_pre_shuffle = sbr_qmf_pre_shuffle_mips;
    s->qmf_post_shuffle = sbr_qmf_post_shuffle_mips;
#if HAVE_MIPSFPU
    s->sum64x5 = sbr_sum64x5_mips;
    s->sum_square = sbr_sum_square_mips;
    s->qmf_deint_bfly = sbr_qmf_deint_bfly_mips;
    s->autocorrelate = sbr_autocorrelate_mips;
    s->hf_gen = sbr_hf_gen_mips;
    s->hf_g_filt = sbr_hf_g_filt_mips;

    s->hf_apply_noise[0] = sbr_hf_apply_noise_0_mips;
    s->hf_apply_noise[1] = sbr_hf_apply_noise_1_mips;
    s->hf_apply_noise[2] = sbr_hf_apply_noise_2_mips;
    s->hf_apply_noise[3] = sbr_hf_apply_noise_3_mips;
#endif /* HAVE_MIPSFPU */
#endif /* HAVE_INLINE_ASM */
}
