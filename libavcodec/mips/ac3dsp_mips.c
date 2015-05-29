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
 * Authors:  Branimir Vasic (bvasic@mips.com)
 *           Nedeljko Babic (nbabic@mips.com)
 *
 * Various AC-3 DSP Utils optimized for MIPS
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
 * Reference: libavcodec/ac3dsp.c
 */

#include "config.h"
#include "libavcodec/ac3dsp.h"
#include "libavcodec/ac3.h"
#include "libavutil/mips/asmdefs.h"

#if HAVE_INLINE_ASM
#if HAVE_MIPSDSPR1
static void ac3_bit_alloc_calc_bap_mips(int16_t *mask, int16_t *psd,
                                        int start, int end,
                                        int snr_offset, int floor,
                                        const uint8_t *bap_tab, uint8_t *bap)
{
    int band, band_end, cond;
    int m, address1, address2;
    int16_t *psd1, *psd_end;
    uint8_t *bap1;

    if (snr_offset == -960) {
        memset(bap, 0, AC3_MAX_COEFS);
        return;
    }

    psd1 = &psd[start];
    bap1 = &bap[start];
    band = ff_ac3_bin_to_band_tab[start];

    do {
        m = (FFMAX(mask[band] - snr_offset - floor, 0) & 0x1FE0) + floor;
        band_end = ff_ac3_band_start_tab[++band];
        band_end = FFMIN(band_end, end);
        psd_end = psd + band_end - 1;

        __asm__ volatile (
            "slt        %[cond],        %[psd1],        %[psd_end]  \n\t"
            "beqz       %[cond],        1f                          \n\t"
            "2:                                                     \n\t"
            "lh         %[address1],    0(%[psd1])                  \n\t"
            "lh         %[address2],    2(%[psd1])                  \n\t"
            PTR_ADDIU " %[psd1],        %[psd1],        4           \n\t"
            "subu       %[address1],    %[address1],    %[m]        \n\t"
            "sra        %[address1],    %[address1],    5           \n\t"
            "addiu      %[address1],    %[address1],    -32         \n\t"
            "shll_s.w   %[address1],    %[address1],    26          \n\t"
            "subu       %[address2],    %[address2],    %[m]        \n\t"
            "sra        %[address2],    %[address2],    5           \n\t"
            "sra        %[address1],    %[address1],    26          \n\t"
            "addiu      %[address1],    %[address1],    32          \n\t"
            "lbux       %[address1],    %[address1](%[bap_tab])     \n\t"
            "addiu      %[address2],    %[address2],    -32         \n\t"
            "shll_s.w   %[address2],    %[address2],    26          \n\t"
            "sb         %[address1],    0(%[bap1])                  \n\t"
            "slt        %[cond],        %[psd1],        %[psd_end]  \n\t"
            "sra        %[address2],    %[address2],    26          \n\t"
            "addiu      %[address2],    %[address2],    32          \n\t"
            "lbux       %[address2],    %[address2](%[bap_tab])     \n\t"
            "sb         %[address2],    1(%[bap1])                  \n\t"
            PTR_ADDIU " %[bap1],        %[bap1],        2           \n\t"
            "bnez       %[cond],        2b                          \n\t"
            PTR_ADDIU " %[psd_end],     %[psd_end],     2           \n\t"
            "slt        %[cond],        %[psd1],        %[psd_end]  \n\t"
            "beqz       %[cond],        3f                          \n\t"
            "1:                                                     \n\t"
            "lh         %[address1],    0(%[psd1])                  \n\t"
            PTR_ADDIU " %[psd1],        %[psd1],        2           \n\t"
            "subu       %[address1],    %[address1],    %[m]        \n\t"
            "sra        %[address1],    %[address1],    5           \n\t"
            "addiu      %[address1],    %[address1],    -32         \n\t"
            "shll_s.w   %[address1],    %[address1],    26          \n\t"
            "sra        %[address1],    %[address1],    26          \n\t"
            "addiu      %[address1],    %[address1],    32          \n\t"
            "lbux       %[address1],    %[address1](%[bap_tab])     \n\t"
            "sb         %[address1],    0(%[bap1])                  \n\t"
            PTR_ADDIU " %[bap1],        %[bap1],        1           \n\t"
            "3:                                                     \n\t"

            : [address1]"=&r"(address1), [address2]"=&r"(address2),
              [cond]"=&r"(cond), [bap1]"+r"(bap1),
              [psd1]"+r"(psd1), [psd_end]"+r"(psd_end)
            : [m]"r"(m), [bap_tab]"r"(bap_tab)
            : "memory"
        );
    } while (end > band_end);
}

static void ac3_update_bap_counts_mips(uint16_t mant_cnt[16], uint8_t *bap,
                                       int len)
{
    void *temp0, *temp2, *temp4, *temp5, *temp6, *temp7;
    int temp1, temp3;

    __asm__ volatile (
        "andi   %[temp3],   %[len],         3               \n\t"
        PTR_ADDU "%[temp2], %[bap],         %[len]          \n\t"
        PTR_ADDU "%[temp4], %[bap],         %[temp3]        \n\t"
        "beq    %[temp2],   %[temp4],       4f              \n\t"
        "1:                                                 \n\t"
        "lbu    %[temp0],   -1(%[temp2])                    \n\t"
        "lbu    %[temp5],   -2(%[temp2])                    \n\t"
        "lbu    %[temp6],   -3(%[temp2])                    \n\t"
        "sll    %[temp0],   %[temp0],       1               \n\t"
        PTR_ADDU "%[temp0], %[mant_cnt],    %[temp0]        \n\t"
        "sll    %[temp5],   %[temp5],       1               \n\t"
        PTR_ADDU "%[temp5], %[mant_cnt],    %[temp5]        \n\t"
        "lhu    %[temp1],   0(%[temp0])                     \n\t"
        "sll    %[temp6],   %[temp6],       1               \n\t"
        PTR_ADDU "%[temp6], %[mant_cnt],    %[temp6]        \n\t"
        "addiu  %[temp1],   %[temp1],       1               \n\t"
        "sh     %[temp1],   0(%[temp0])                     \n\t"
        "lhu    %[temp1],   0(%[temp5])                     \n\t"
        "lbu    %[temp7],   -4(%[temp2])                    \n\t"
        PTR_ADDIU "%[temp2],%[temp2],       -4              \n\t"
        "addiu  %[temp1],   %[temp1],       1               \n\t"
        "sh     %[temp1],   0(%[temp5])                     \n\t"
        "lhu    %[temp1],   0(%[temp6])                     \n\t"
        "sll    %[temp7],   %[temp7],       1               \n\t"
        PTR_ADDU "%[temp7], %[mant_cnt],    %[temp7]        \n\t"
        "addiu  %[temp1],   %[temp1],1                      \n\t"
        "sh     %[temp1],   0(%[temp6])                     \n\t"
        "lhu    %[temp1],   0(%[temp7])                     \n\t"
        "addiu  %[temp1],   %[temp1],       1               \n\t"
        "sh     %[temp1],   0(%[temp7])                     \n\t"
        "bne    %[temp2],   %[temp4],       1b              \n\t"
        "4:                                                 \n\t"
        "beqz   %[temp3],   2f                              \n\t"
        "3:                                                 \n\t"
        "addiu  %[temp3],   %[temp3],       -1              \n\t"
        "lbu    %[temp0],   -1(%[temp2])                    \n\t"
        PTR_ADDIU "%[temp2],%[temp2],       -1              \n\t"
        "sll    %[temp0],   %[temp0],       1               \n\t"
        PTR_ADDU "%[temp0], %[mant_cnt],    %[temp0]        \n\t"
        "lhu    %[temp1],   0(%[temp0])                     \n\t"
        "addiu  %[temp1],   %[temp1],       1               \n\t"
        "sh     %[temp1],   0(%[temp0])                     \n\t"
        "bgtz   %[temp3],   3b                              \n\t"
        "2:                                                 \n\t"

        : [temp0] "=&r" (temp0), [temp1] "=&r" (temp1),
          [temp2] "=&r" (temp2), [temp3] "=&r" (temp3),
          [temp4] "=&r" (temp4), [temp5] "=&r" (temp5),
          [temp6] "=&r" (temp6), [temp7] "=&r" (temp7)
        : [len] "r" (len), [bap] "r" (bap),
          [mant_cnt] "r" (mant_cnt)
        : "memory"
    );
}
#endif

#if HAVE_MIPSFPU
static void float_to_fixed24_mips(int32_t *dst, const float *src, unsigned int len)
{
    const float scale = 1 << 24;
    float src0, src1, src2, src3, src4, src5, src6, src7;
    int temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7;

    do {
        __asm__ volatile (
            "lwc1       %[src0],    0(%[src])               \n\t"
            "lwc1       %[src1],    4(%[src])               \n\t"
            "lwc1       %[src2],    8(%[src])               \n\t"
            "lwc1       %[src3],    12(%[src])              \n\t"
            "lwc1       %[src4],    16(%[src])              \n\t"
            "lwc1       %[src5],    20(%[src])              \n\t"
            "lwc1       %[src6],    24(%[src])              \n\t"
            "lwc1       %[src7],    28(%[src])              \n\t"
            "mul.s      %[src0],    %[src0],    %[scale]    \n\t"
            "mul.s      %[src1],    %[src1],    %[scale]    \n\t"
            "mul.s      %[src2],    %[src2],    %[scale]    \n\t"
            "mul.s      %[src3],    %[src3],    %[scale]    \n\t"
            "mul.s      %[src4],    %[src4],    %[scale]    \n\t"
            "mul.s      %[src5],    %[src5],    %[scale]    \n\t"
            "mul.s      %[src6],    %[src6],    %[scale]    \n\t"
            "mul.s      %[src7],    %[src7],    %[scale]    \n\t"
            "cvt.w.s    %[src0],    %[src0]                 \n\t"
            "cvt.w.s    %[src1],    %[src1]                 \n\t"
            "cvt.w.s    %[src2],    %[src2]                 \n\t"
            "cvt.w.s    %[src3],    %[src3]                 \n\t"
            "cvt.w.s    %[src4],    %[src4]                 \n\t"
            "cvt.w.s    %[src5],    %[src5]                 \n\t"
            "cvt.w.s    %[src6],    %[src6]                 \n\t"
            "cvt.w.s    %[src7],    %[src7]                 \n\t"
            "mfc1       %[temp0],   %[src0]                 \n\t"
            "mfc1       %[temp1],   %[src1]                 \n\t"
            "mfc1       %[temp2],   %[src2]                 \n\t"
            "mfc1       %[temp3],   %[src3]                 \n\t"
            "mfc1       %[temp4],   %[src4]                 \n\t"
            "mfc1       %[temp5],   %[src5]                 \n\t"
            "mfc1       %[temp6],   %[src6]                 \n\t"
            "mfc1       %[temp7],   %[src7]                 \n\t"
            "sw         %[temp0],   0(%[dst])               \n\t"
            "sw         %[temp1],   4(%[dst])               \n\t"
            "sw         %[temp2],   8(%[dst])               \n\t"
            "sw         %[temp3],   12(%[dst])              \n\t"
            "sw         %[temp4],   16(%[dst])              \n\t"
            "sw         %[temp5],   20(%[dst])              \n\t"
            "sw         %[temp6],   24(%[dst])              \n\t"
            "sw         %[temp7],   28(%[dst])              \n\t"

            : [dst] "+r" (dst), [src] "+r" (src),
              [src0] "=&f" (src0), [src1] "=&f" (src1),
              [src2] "=&f" (src2), [src3] "=&f" (src3),
              [src4] "=&f" (src4), [src5] "=&f" (src5),
              [src6] "=&f" (src6), [src7] "=&f" (src7),
              [temp0] "=r" (temp0), [temp1] "=r" (temp1),
              [temp2] "=r" (temp2), [temp3] "=r" (temp3),
              [temp4] "=r" (temp4), [temp5] "=r" (temp5),
              [temp6] "=r" (temp6), [temp7] "=r" (temp7)
            : [scale] "f" (scale)
            : "memory"
        );
        src = src + 8;
        dst = dst + 8;
        len -= 8;
    } while (len > 0);
}

static void ac3_downmix_mips(float **samples, float (*matrix)[2],
                          int out_ch, int in_ch, int len)
{
    int i, j, i1, i2, i3;
    float v0, v1, v2, v3;
    float v4, v5, v6, v7;
    float samples0, samples1, samples2, samples3, matrix_j, matrix_j2;
    float *samples_p, *samples_sw, *matrix_p, **samples_x, **samples_end;

    __asm__ volatile(
        ".set   push                                                \n\t"
        ".set   noreorder                                           \n\t"

        "li     %[i1],          2                                   \n\t"
        "sll    %[len],         2                                   \n\t"
        "move   %[i],           $zero                               \n\t"
        "sll    %[j],           %[in_ch],             " PTRLOG "    \n\t"

        "bne    %[out_ch],      %[i1],                  3f          \n\t"   // if (out_ch == 2)
        " li    %[i2],          1                                   \n\t"

        "2:                                                         \n\t"   // start of the for loop (for (i = 0; i < len; i+=4))
        "move   %[matrix_p],    %[matrix]                           \n\t"
        "move   %[samples_x],   %[samples]                          \n\t"
        "mtc1   $zero,          %[v0]                               \n\t"
        "mtc1   $zero,          %[v1]                               \n\t"
        "mtc1   $zero,          %[v2]                               \n\t"
        "mtc1   $zero,          %[v3]                               \n\t"
        "mtc1   $zero,          %[v4]                               \n\t"
        "mtc1   $zero,          %[v5]                               \n\t"
        "mtc1   $zero,          %[v6]                               \n\t"
        "mtc1   $zero,          %[v7]                               \n\t"
        "addiu  %[i1],          %[i],                  4            \n\t"
        "addiu  %[i2],          %[i],                  8            \n\t"
        PTR_L " %[samples_p],   0(%[samples_x])                     \n\t"
        "addiu  %[i3],          %[i],                  12           \n\t"
        PTR_ADDU "%[samples_end],%[samples_x],         %[j]         \n\t"
        "move   %[samples_sw],  %[samples_p]                        \n\t"

        "1:                                                         \n\t"   // start of the inner for loop (for (j = 0; j < in_ch; j++))
        "lwc1   %[matrix_j],    0(%[matrix_p])                      \n\t"
        "lwc1   %[matrix_j2],   4(%[matrix_p])                      \n\t"
        "lwxc1  %[samples0],    %[i](%[samples_p])                  \n\t"
        "lwxc1  %[samples1],    %[i1](%[samples_p])                 \n\t"
        "lwxc1  %[samples2],    %[i2](%[samples_p])                 \n\t"
        "lwxc1  %[samples3],    %[i3](%[samples_p])                 \n\t"
        PTR_ADDIU "%[matrix_p], 8                                   \n\t"
        PTR_ADDIU "%[samples_x]," PTRSIZE "                         \n\t"
        "madd.s %[v0],          %[v0],  %[samples0],    %[matrix_j] \n\t"
        "madd.s %[v1],          %[v1],  %[samples1],    %[matrix_j] \n\t"
        "madd.s %[v2],          %[v2],  %[samples2],    %[matrix_j] \n\t"
        "madd.s %[v3],          %[v3],  %[samples3],    %[matrix_j] \n\t"
        "madd.s %[v4],          %[v4],  %[samples0],    %[matrix_j2]\n\t"
        "madd.s %[v5],          %[v5],  %[samples1],    %[matrix_j2]\n\t"
        "madd.s %[v6],          %[v6],  %[samples2],    %[matrix_j2]\n\t"
        "madd.s %[v7],          %[v7],  %[samples3],    %[matrix_j2]\n\t"
        "bne    %[samples_x],   %[samples_end],         1b          \n\t"
        PTR_L " %[samples_p],   0(%[samples_x])                     \n\t"

        PTR_L " %[samples_p],  " PTRSIZE "(%[samples])              \n\t"
        "swxc1  %[v0],          %[i](%[samples_sw])                 \n\t"
        "swxc1  %[v1],          %[i1](%[samples_sw])                \n\t"
        "swxc1  %[v2],          %[i2](%[samples_sw])                \n\t"
        "swxc1  %[v3],          %[i3](%[samples_sw])                \n\t"
        "swxc1  %[v4],          %[i](%[samples_p])                  \n\t"
        "addiu  %[i],           16                                  \n\t"
        "swxc1  %[v5],          %[i1](%[samples_p])                 \n\t"
        "swxc1  %[v6],          %[i2](%[samples_p])                 \n\t"
        "bne    %[i],           %[len],                 2b          \n\t"
        " swxc1 %[v7],          %[i3](%[samples_p])                 \n\t"

        "3:                                                         \n\t"
        "bne    %[out_ch],      %[i2],                  6f          \n\t"   // if (out_ch == 1)
        " nop                                                       \n\t"

        "5:                                                         \n\t"   // start of the outer for loop (for (i = 0; i < len; i+=4))
        "move   %[matrix_p],    %[matrix]                           \n\t"
        "move   %[samples_x],   %[samples]                          \n\t"
        "mtc1   $zero,          %[v0]                               \n\t"
        "mtc1   $zero,          %[v1]                               \n\t"
        "mtc1   $zero,          %[v2]                               \n\t"
        "mtc1   $zero,          %[v3]                               \n\t"
        "addiu  %[i1],          %[i],                  4            \n\t"
        "addiu  %[i2],          %[i],                  8            \n\t"
        PTR_L " %[samples_p],   0(%[samples_x])                     \n\t"
        "addiu  %[i3],          %[i],                  12           \n\t"
        PTR_ADDU "%[samples_end],%[samples_x],         %[j]         \n\t"
        "move   %[samples_sw],  %[samples_p]                        \n\t"

        "4:                                                         \n\t"   // start of the inner for loop (for (j = 0; j < in_ch; j++))
        "lwc1   %[matrix_j],    0(%[matrix_p])                      \n\t"
        "lwxc1  %[samples0],    %[i](%[samples_p])                  \n\t"
        "lwxc1  %[samples1],    %[i1](%[samples_p])                 \n\t"
        "lwxc1  %[samples2],    %[i2](%[samples_p])                 \n\t"
        "lwxc1  %[samples3],    %[i3](%[samples_p])                 \n\t"
        PTR_ADDIU "%[matrix_p], 8                                   \n\t"
        PTR_ADDIU "%[samples_x]," PTRSIZE "                         \n\t"
        "madd.s %[v0],          %[v0],  %[samples0],    %[matrix_j] \n\t"
        "madd.s %[v1],          %[v1],  %[samples1],    %[matrix_j] \n\t"
        "madd.s %[v2],          %[v2],  %[samples2],    %[matrix_j] \n\t"
        "madd.s %[v3],          %[v3],  %[samples3],    %[matrix_j] \n\t"
        "bne    %[samples_x],   %[samples_end],         4b          \n\t"
        PTR_L " %[samples_p],   0(%[samples_x])                     \n\t"

        "swxc1  %[v0],          %[i](%[samples_sw])                 \n\t"
        "addiu  %[i],           16                                  \n\t"
        "swxc1  %[v1],          %[i1](%[samples_sw])                \n\t"
        "swxc1  %[v2],          %[i2](%[samples_sw])                \n\t"
        "bne    %[i],           %[len],                 5b          \n\t"
        " swxc1 %[v3],          %[i3](%[samples_sw])                \n\t"
        "6:                                                         \n\t"

        ".set   pop"
        :[samples_p]"=&r"(samples_p), [matrix_j]"=&f"(matrix_j), [matrix_j2]"=&f"(matrix_j2),
         [samples0]"=&f"(samples0), [samples1]"=&f"(samples1),
         [samples2]"=&f"(samples2), [samples3]"=&f"(samples3),
         [v0]"=&f"(v0), [v1]"=&f"(v1), [v2]"=&f"(v2), [v3]"=&f"(v3),
         [v4]"=&f"(v4), [v5]"=&f"(v5), [v6]"=&f"(v6), [v7]"=&f"(v7),
         [samples_x]"=&r"(samples_x), [matrix_p]"=&r"(matrix_p),
         [samples_end]"=&r"(samples_end), [samples_sw]"=&r"(samples_sw),
         [i1]"=&r"(i1), [i2]"=&r"(i2), [i3]"=&r"(i3), [i]"=&r"(i),
         [j]"=&r"(j), [len]"+r"(len)
        :[samples]"r"(samples), [matrix]"r"(matrix),
         [in_ch]"r"(in_ch), [out_ch]"r"(out_ch)
        :"memory"
    );
}
#endif
#endif /* HAVE_INLINE_ASM */

void ff_ac3dsp_init_mips(AC3DSPContext *c, int bit_exact) {
#if HAVE_INLINE_ASM
#if HAVE_MIPSDSPR1
    c->bit_alloc_calc_bap = ac3_bit_alloc_calc_bap_mips;
    c->update_bap_counts  = ac3_update_bap_counts_mips;
#endif
#if HAVE_MIPSFPU
    c->float_to_fixed24 = float_to_fixed24_mips;
    c->downmix          = ac3_downmix_mips;
#endif
#endif

}
