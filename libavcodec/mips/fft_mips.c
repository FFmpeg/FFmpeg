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
 * Author:  Stanislav Ocovaj (socovaj@mips.com)
 * Author:  Zoran Lukic (zoranl@mips.com)
 *
 * Optimized MDCT/IMDCT and FFT transforms
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
#include "libavcodec/fft.h"
#include "fft_table.h"

/**
 * FFT transform
 */

#if HAVE_INLINE_ASM
static void ff_fft_calc_mips(FFTContext *s, FFTComplex *z)
{
    int nbits, i, n, num_transforms, offset, step;
    int n4, n2, n34;
    FFTSample tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8;
    FFTComplex *tmpz;
    float w_re, w_im;
    float *w_re_ptr, *w_im_ptr;
    const int fft_size = (1 << s->nbits);
    int s_n = s->nbits;
    int tem1, tem2;
    float pom,  pom1,  pom2,  pom3;
    float temp, temp1, temp3, temp4;
    FFTComplex * tmpz_n2, * tmpz_n34, * tmpz_n4;
    FFTComplex * tmpz_n2_i, * tmpz_n34_i, * tmpz_n4_i, * tmpz_i;

    /**
    *num_transforms = (0x2aab >> (16 - s->nbits)) | 1;
    */
    __asm__ volatile (
        "li   %[tem1], 16                                      \n\t"
        "sub  %[s_n],  %[tem1], %[s_n]                         \n\t"
        "li   %[tem2], 10923                                   \n\t"
        "srav %[tem2], %[tem2], %[s_n]                         \n\t"
        "ori  %[num_t],%[tem2], 1                              \n\t"
        : [num_t]"=r"(num_transforms), [s_n]"+r"(s_n),
          [tem1]"=&r"(tem1), [tem2]"=&r"(tem2)
    );


    for (n=0; n<num_transforms; n++) {
        offset = fft_offsets_lut[n] << 2;
        tmpz = z + offset;

        tmp1 = tmpz[0].re + tmpz[1].re;
        tmp5 = tmpz[2].re + tmpz[3].re;
        tmp2 = tmpz[0].im + tmpz[1].im;
        tmp6 = tmpz[2].im + tmpz[3].im;
        tmp3 = tmpz[0].re - tmpz[1].re;
        tmp8 = tmpz[2].im - tmpz[3].im;
        tmp4 = tmpz[0].im - tmpz[1].im;
        tmp7 = tmpz[2].re - tmpz[3].re;

        tmpz[0].re = tmp1 + tmp5;
        tmpz[2].re = tmp1 - tmp5;
        tmpz[0].im = tmp2 + tmp6;
        tmpz[2].im = tmp2 - tmp6;
        tmpz[1].re = tmp3 + tmp8;
        tmpz[3].re = tmp3 - tmp8;
        tmpz[1].im = tmp4 - tmp7;
        tmpz[3].im = tmp4 + tmp7;

    }

    if (fft_size < 8)
        return;

    num_transforms = (num_transforms >> 1) | 1;

    for (n=0; n<num_transforms; n++) {
        offset = fft_offsets_lut[n] << 3;
        tmpz = z + offset;

        __asm__ volatile (
            "lwc1  %[tmp1], 32(%[tmpz])                     \n\t"
            "lwc1  %[pom],  40(%[tmpz])                     \n\t"
            "lwc1  %[tmp3], 48(%[tmpz])                     \n\t"
            "lwc1  %[pom1], 56(%[tmpz])                     \n\t"
            "lwc1  %[tmp2], 36(%[tmpz])                     \n\t"
            "lwc1  %[pom2], 44(%[tmpz])                     \n\t"
            "lwc1  %[pom3], 60(%[tmpz])                     \n\t"
            "lwc1  %[tmp4], 52(%[tmpz])                     \n\t"
            "add.s %[tmp1], %[tmp1],    %[pom]              \n\t"  // tmp1 = tmpz[4].re + tmpz[5].re;
            "add.s %[tmp3], %[tmp3],    %[pom1]             \n\t"  // tmp3 = tmpz[6].re + tmpz[7].re;
            "add.s %[tmp2], %[tmp2],    %[pom2]             \n\t"  // tmp2 = tmpz[4].im + tmpz[5].im;
            "lwc1  %[pom],  40(%[tmpz])                     \n\t"
            "add.s %[tmp4], %[tmp4],    %[pom3]             \n\t"  // tmp4 = tmpz[6].im + tmpz[7].im;
            "add.s %[tmp5], %[tmp1],    %[tmp3]             \n\t"  // tmp5 = tmp1 + tmp3;
            "sub.s %[tmp7], %[tmp1],    %[tmp3]             \n\t"  // tmp7 = tmp1 - tmp3;
            "lwc1  %[tmp1], 32(%[tmpz])                     \n\t"
            "lwc1  %[pom1], 44(%[tmpz])                     \n\t"
            "add.s %[tmp6], %[tmp2],    %[tmp4]             \n\t"  // tmp6 = tmp2 + tmp4;
            "sub.s %[tmp8], %[tmp2],    %[tmp4]             \n\t"  // tmp8 = tmp2 - tmp4;
            "lwc1  %[tmp2], 36(%[tmpz])                     \n\t"
            "lwc1  %[pom2], 56(%[tmpz])                     \n\t"
            "lwc1  %[pom3], 60(%[tmpz])                     \n\t"
            "lwc1  %[tmp3], 48(%[tmpz])                     \n\t"
            "lwc1  %[tmp4], 52(%[tmpz])                     \n\t"
            "sub.s %[tmp1], %[tmp1],    %[pom]              \n\t"  // tmp1 = tmpz[4].re - tmpz[5].re;
            "lwc1  %[pom],  0(%[tmpz])                      \n\t"
            "sub.s %[tmp2], %[tmp2],    %[pom1]             \n\t"  // tmp2 = tmpz[4].im - tmpz[5].im;
            "sub.s %[tmp3], %[tmp3],    %[pom2]             \n\t"  // tmp3 = tmpz[6].re - tmpz[7].re;
            "lwc1  %[pom2], 4(%[tmpz])                      \n\t"
            "sub.s %[pom1], %[pom],     %[tmp5]             \n\t"
            "sub.s %[tmp4], %[tmp4],    %[pom3]             \n\t"  // tmp4 = tmpz[6].im - tmpz[7].im;
            "add.s %[pom3], %[pom],     %[tmp5]             \n\t"
            "sub.s %[pom],  %[pom2],    %[tmp6]             \n\t"
            "add.s %[pom2], %[pom2],    %[tmp6]             \n\t"
            "swc1  %[pom1], 32(%[tmpz])                     \n\t"  // tmpz[4].re = tmpz[0].re - tmp5;
            "swc1  %[pom3], 0(%[tmpz])                      \n\t"  // tmpz[0].re = tmpz[0].re + tmp5;
            "swc1  %[pom],  36(%[tmpz])                     \n\t"  // tmpz[4].im = tmpz[0].im - tmp6;
            "swc1  %[pom2], 4(%[tmpz])                      \n\t"  // tmpz[0].im = tmpz[0].im + tmp6;
            "lwc1  %[pom1], 16(%[tmpz])                     \n\t"
            "lwc1  %[pom3], 20(%[tmpz])                     \n\t"
            "li.s  %[pom],  0.7071067812                    \n\t"  // float pom = 0.7071067812f;
            "add.s %[temp1],%[tmp1],    %[tmp2]             \n\t"
            "sub.s %[temp], %[pom1],    %[tmp8]             \n\t"
            "add.s %[pom2], %[pom3],    %[tmp7]             \n\t"
            "sub.s %[temp3],%[tmp3],    %[tmp4]             \n\t"
            "sub.s %[temp4],%[tmp2],    %[tmp1]             \n\t"
            "swc1  %[temp], 48(%[tmpz])                     \n\t"  // tmpz[6].re = tmpz[2].re - tmp8;
            "swc1  %[pom2], 52(%[tmpz])                     \n\t"  // tmpz[6].im = tmpz[2].im + tmp7;
            "add.s %[pom1], %[pom1],    %[tmp8]             \n\t"
            "sub.s %[pom3], %[pom3],    %[tmp7]             \n\t"
            "add.s %[tmp3], %[tmp3],    %[tmp4]             \n\t"
            "mul.s %[tmp5], %[pom],     %[temp1]            \n\t"  // tmp5 = pom * (tmp1 + tmp2);
            "mul.s %[tmp7], %[pom],     %[temp3]            \n\t"  // tmp7 = pom * (tmp3 - tmp4);
            "mul.s %[tmp6], %[pom],     %[temp4]            \n\t"  // tmp6 = pom * (tmp2 - tmp1);
            "mul.s %[tmp8], %[pom],     %[tmp3]             \n\t"  // tmp8 = pom * (tmp3 + tmp4);
            "swc1  %[pom1], 16(%[tmpz])                     \n\t"  // tmpz[2].re = tmpz[2].re + tmp8;
            "swc1  %[pom3], 20(%[tmpz])                     \n\t"  // tmpz[2].im = tmpz[2].im - tmp7;
            "add.s %[tmp1], %[tmp5],    %[tmp7]             \n\t"  // tmp1 = tmp5 + tmp7;
            "sub.s %[tmp3], %[tmp5],    %[tmp7]             \n\t"  // tmp3 = tmp5 - tmp7;
            "add.s %[tmp2], %[tmp6],    %[tmp8]             \n\t"  // tmp2 = tmp6 + tmp8;
            "sub.s %[tmp4], %[tmp6],    %[tmp8]             \n\t"  // tmp4 = tmp6 - tmp8;
            "lwc1  %[temp], 8(%[tmpz])                      \n\t"
            "lwc1  %[temp1],12(%[tmpz])                     \n\t"
            "lwc1  %[pom],  24(%[tmpz])                     \n\t"
            "lwc1  %[pom2], 28(%[tmpz])                     \n\t"
            "sub.s %[temp4],%[temp],    %[tmp1]             \n\t"
            "sub.s %[temp3],%[temp1],   %[tmp2]             \n\t"
            "add.s %[temp], %[temp],    %[tmp1]             \n\t"
            "add.s %[temp1],%[temp1],   %[tmp2]             \n\t"
            "sub.s %[pom1], %[pom],     %[tmp4]             \n\t"
            "add.s %[pom3], %[pom2],    %[tmp3]             \n\t"
            "add.s %[pom],  %[pom],     %[tmp4]             \n\t"
            "sub.s %[pom2], %[pom2],    %[tmp3]             \n\t"
            "swc1  %[temp4],40(%[tmpz])                     \n\t"  // tmpz[5].re = tmpz[1].re - tmp1;
            "swc1  %[temp3],44(%[tmpz])                     \n\t"  // tmpz[5].im = tmpz[1].im - tmp2;
            "swc1  %[temp], 8(%[tmpz])                      \n\t"  // tmpz[1].re = tmpz[1].re + tmp1;
            "swc1  %[temp1],12(%[tmpz])                     \n\t"  // tmpz[1].im = tmpz[1].im + tmp2;
            "swc1  %[pom1], 56(%[tmpz])                     \n\t"  // tmpz[7].re = tmpz[3].re - tmp4;
            "swc1  %[pom3], 60(%[tmpz])                     \n\t"  // tmpz[7].im = tmpz[3].im + tmp3;
            "swc1  %[pom],  24(%[tmpz])                     \n\t"  // tmpz[3].re = tmpz[3].re + tmp4;
            "swc1  %[pom2], 28(%[tmpz])                     \n\t"  // tmpz[3].im = tmpz[3].im - tmp3;
            : [tmp1]"=&f"(tmp1), [pom]"=&f"(pom),   [pom1]"=&f"(pom1), [pom2]"=&f"(pom2),
              [tmp3]"=&f"(tmp3), [tmp2]"=&f"(tmp2), [tmp4]"=&f"(tmp4), [tmp5]"=&f"(tmp5),  [tmp7]"=&f"(tmp7),
              [tmp6]"=&f"(tmp6), [tmp8]"=&f"(tmp8), [pom3]"=&f"(pom3),[temp]"=&f"(temp), [temp1]"=&f"(temp1),
              [temp3]"=&f"(temp3), [temp4]"=&f"(temp4)
            : [tmpz]"r"(tmpz)
            : "memory"
        );
    }

    step = 1 << (MAX_LOG2_NFFT - 4);
    n4 = 4;

    for (nbits=4; nbits<=s->nbits; nbits++) {
        /*
        * num_transforms = (num_transforms >> 1) | 1;
        */
        __asm__ volatile (
            "sra %[num_t], %[num_t], 1               \n\t"
            "ori %[num_t], %[num_t], 1               \n\t"

            : [num_t] "+r" (num_transforms)
        );
        n2  = 2 * n4;
        n34 = 3 * n4;

        for (n=0; n<num_transforms; n++) {
            offset = fft_offsets_lut[n] << nbits;
            tmpz = z + offset;

            tmpz_n2  = tmpz +  n2;
            tmpz_n4  = tmpz +  n4;
            tmpz_n34 = tmpz +  n34;

            __asm__ volatile (
                "lwc1  %[pom1], 0(%[tmpz_n2])            \n\t"
                "lwc1  %[pom],  0(%[tmpz_n34])           \n\t"
                "lwc1  %[pom2], 4(%[tmpz_n2])            \n\t"
                "lwc1  %[pom3], 4(%[tmpz_n34])           \n\t"
                "lwc1  %[temp1],0(%[tmpz])               \n\t"
                "lwc1  %[temp3],4(%[tmpz])               \n\t"
                "add.s %[tmp5], %[pom1],      %[pom]     \n\t"   //  tmp5 = tmpz[ n2].re + tmpz[n34].re;
                "sub.s %[tmp1], %[pom1],      %[pom]     \n\t"   //  tmp1 = tmpz[ n2].re - tmpz[n34].re;
                "add.s %[tmp6], %[pom2],      %[pom3]    \n\t"   //  tmp6 = tmpz[ n2].im + tmpz[n34].im;
                "sub.s %[tmp2], %[pom2],      %[pom3]    \n\t"   //  tmp2 = tmpz[ n2].im - tmpz[n34].im;
                "sub.s %[temp], %[temp1],     %[tmp5]    \n\t"
                "add.s %[temp1],%[temp1],     %[tmp5]    \n\t"
                "sub.s %[temp4],%[temp3],     %[tmp6]    \n\t"
                "add.s %[temp3],%[temp3],     %[tmp6]    \n\t"
                "swc1  %[temp], 0(%[tmpz_n2])            \n\t"   //  tmpz[ n2].re = tmpz[ 0].re - tmp5;
                "swc1  %[temp1],0(%[tmpz])               \n\t"   //  tmpz[  0].re = tmpz[ 0].re + tmp5;
                "lwc1  %[pom1], 0(%[tmpz_n4])            \n\t"
                "swc1  %[temp4],4(%[tmpz_n2])            \n\t"   //  tmpz[ n2].im = tmpz[ 0].im - tmp6;
                "lwc1  %[temp], 4(%[tmpz_n4])            \n\t"
                "swc1  %[temp3],4(%[tmpz])               \n\t"   //  tmpz[  0].im = tmpz[ 0].im + tmp6;
                "sub.s %[pom],  %[pom1],      %[tmp2]    \n\t"
                "add.s %[pom1], %[pom1],      %[tmp2]    \n\t"
                "add.s %[temp1],%[temp],      %[tmp1]    \n\t"
                "sub.s %[temp], %[temp],      %[tmp1]    \n\t"
                "swc1  %[pom],  0(%[tmpz_n34])           \n\t"   //  tmpz[n34].re = tmpz[n4].re - tmp2;
                "swc1  %[pom1], 0(%[tmpz_n4])            \n\t"   //  tmpz[ n4].re = tmpz[n4].re + tmp2;
                "swc1  %[temp1],4(%[tmpz_n34])           \n\t"   //  tmpz[n34].im = tmpz[n4].im + tmp1;
                "swc1  %[temp], 4(%[tmpz_n4])            \n\t"   //  tmpz[ n4].im = tmpz[n4].im - tmp1;
                : [tmp5]"=&f"(tmp5),
                  [tmp1]"=&f"(tmp1), [pom]"=&f"(pom),        [pom1]"=&f"(pom1),        [pom2]"=&f"(pom2),
                  [tmp2]"=&f"(tmp2), [tmp6]"=&f"(tmp6),          [pom3]"=&f"(pom3),
                  [temp]"=&f"(temp), [temp1]"=&f"(temp1),     [temp3]"=&f"(temp3),       [temp4]"=&f"(temp4)
                : [tmpz]"r"(tmpz), [tmpz_n2]"r"(tmpz_n2), [tmpz_n34]"r"(tmpz_n34), [tmpz_n4]"r"(tmpz_n4)
                : "memory"
            );

            w_re_ptr = (float*)(ff_cos_65536 + step);
            w_im_ptr = (float*)(ff_cos_65536 + MAX_FFT_SIZE/4 - step);

            for (i=1; i<n4; i++) {
                w_re = w_re_ptr[0];
                w_im = w_im_ptr[0];
                tmpz_n2_i = tmpz_n2  + i;
                tmpz_n4_i = tmpz_n4  + i;
                tmpz_n34_i= tmpz_n34 + i;
                tmpz_i    = tmpz     + i;

                __asm__ volatile (
                    "lwc1     %[temp],  0(%[tmpz_n2_i])               \n\t"
                    "lwc1     %[temp1], 4(%[tmpz_n2_i])               \n\t"
                    "lwc1     %[pom],   0(%[tmpz_n34_i])              \n\t"
                    "lwc1     %[pom1],  4(%[tmpz_n34_i])              \n\t"
                    "mul.s    %[temp3], %[w_im],    %[temp]           \n\t"
                    "mul.s    %[temp4], %[w_im],    %[temp1]          \n\t"
                    "mul.s    %[pom2],  %[w_im],    %[pom1]           \n\t"
                    "mul.s    %[pom3],  %[w_im],    %[pom]            \n\t"
                    "msub.s   %[tmp2],  %[temp3],   %[w_re], %[temp1] \n\t"  // tmp2 = w_re * tmpz[ n2+i].im - w_im * tmpz[ n2+i].re;
                    "madd.s   %[tmp1],  %[temp4],   %[w_re], %[temp]  \n\t"  // tmp1 = w_re * tmpz[ n2+i].re + w_im * tmpz[ n2+i].im;
                    "msub.s   %[tmp3],  %[pom2],    %[w_re], %[pom]   \n\t"  // tmp3 = w_re * tmpz[n34+i].re - w_im * tmpz[n34+i].im;
                    "madd.s   %[tmp4],  %[pom3],    %[w_re], %[pom1]  \n\t"  // tmp4 = w_re * tmpz[n34+i].im + w_im * tmpz[n34+i].re;
                    "lwc1     %[temp],  0(%[tmpz_i])                  \n\t"
                    "lwc1     %[pom],   4(%[tmpz_i])                  \n\t"
                    "add.s    %[tmp5],  %[tmp1],    %[tmp3]           \n\t"  // tmp5 = tmp1 + tmp3;
                    "sub.s    %[tmp1],  %[tmp1],    %[tmp3]           \n\t"  // tmp1 = tmp1 - tmp3;
                    "add.s    %[tmp6],  %[tmp2],    %[tmp4]           \n\t"  // tmp6 = tmp2 + tmp4;
                    "sub.s    %[tmp2],  %[tmp2],    %[tmp4]           \n\t"  // tmp2 = tmp2 - tmp4;
                    "sub.s    %[temp1], %[temp],    %[tmp5]           \n\t"
                    "add.s    %[temp],  %[temp],    %[tmp5]           \n\t"
                    "sub.s    %[pom1],  %[pom],     %[tmp6]           \n\t"
                    "add.s    %[pom],   %[pom],     %[tmp6]           \n\t"
                    "lwc1     %[temp3], 0(%[tmpz_n4_i])               \n\t"
                    "lwc1     %[pom2],  4(%[tmpz_n4_i])               \n\t"
                    "swc1     %[temp1], 0(%[tmpz_n2_i])               \n\t"  // tmpz[ n2+i].re = tmpz[   i].re - tmp5;
                    "swc1     %[temp],  0(%[tmpz_i])                  \n\t"  // tmpz[    i].re = tmpz[   i].re + tmp5;
                    "swc1     %[pom1],  4(%[tmpz_n2_i])               \n\t"  // tmpz[ n2+i].im = tmpz[   i].im - tmp6;
                    "swc1     %[pom] ,  4(%[tmpz_i])                  \n\t"  // tmpz[    i].im = tmpz[   i].im + tmp6;
                    "sub.s    %[temp4], %[temp3],   %[tmp2]           \n\t"
                    "add.s    %[pom3],  %[pom2],    %[tmp1]           \n\t"
                    "add.s    %[temp3], %[temp3],   %[tmp2]           \n\t"
                    "sub.s    %[pom2],  %[pom2],    %[tmp1]           \n\t"
                    "swc1     %[temp4], 0(%[tmpz_n34_i])              \n\t"  // tmpz[n34+i].re = tmpz[n4+i].re - tmp2;
                    "swc1     %[pom3],  4(%[tmpz_n34_i])              \n\t"  // tmpz[n34+i].im = tmpz[n4+i].im + tmp1;
                    "swc1     %[temp3], 0(%[tmpz_n4_i])               \n\t"  // tmpz[ n4+i].re = tmpz[n4+i].re + tmp2;
                    "swc1     %[pom2],  4(%[tmpz_n4_i])               \n\t"  // tmpz[ n4+i].im = tmpz[n4+i].im - tmp1;
                    : [tmp1]"=&f"(tmp1), [tmp2]"=&f" (tmp2), [temp]"=&f"(temp), [tmp3]"=&f"(tmp3),
                      [tmp4]"=&f"(tmp4), [tmp5]"=&f"(tmp5), [tmp6]"=&f"(tmp6),
                      [temp1]"=&f"(temp1), [temp3]"=&f"(temp3), [temp4]"=&f"(temp4),
                      [pom]"=&f"(pom), [pom1]"=&f"(pom1), [pom2]"=&f"(pom2), [pom3]"=&f"(pom3)
                    : [w_re]"f"(w_re), [w_im]"f"(w_im),
                      [tmpz_i]"r"(tmpz_i),[tmpz_n2_i]"r"(tmpz_n2_i),
                      [tmpz_n34_i]"r"(tmpz_n34_i), [tmpz_n4_i]"r"(tmpz_n4_i)
                    : "memory"
                );
                w_re_ptr += step;
                w_im_ptr -= step;
            }
        }
        step >>= 1;
        n4   <<= 1;
    }
}

/**
 * MDCT/IMDCT transforms.
 */

static void ff_imdct_half_mips(FFTContext *s, FFTSample *output, const FFTSample *input)
{
    int k, n8, n4, n2, n, j;
    const uint16_t *revtab = s->revtab;
    const FFTSample *tcos = s->tcos;
    const FFTSample *tsin = s->tsin;
    const FFTSample *in1, *in2, *in3, *in4;
    FFTComplex *z = (FFTComplex *)output;

    int j1;
    const float *tcos1, *tsin1, *tcos2, *tsin2;
    float temp1, temp2, temp3, temp4, temp5, temp6, temp7, temp8,
        temp9, temp10, temp11, temp12, temp13, temp14, temp15, temp16;
    FFTComplex *z1, *z2;

    n = 1 << s->mdct_bits;
    n2 = n >> 1;
    n4 = n >> 2;
    n8 = n >> 3;

    /* pre rotation */
    in1 = input;
    in2 = input + n2 - 1;
    in3 = input + 2;
    in4 = input + n2 - 3;

    tcos1 = tcos;
    tsin1 = tsin;

    /* n4 = 64 or 128 */
    for(k = 0; k < n4; k += 2) {
        j  = revtab[k    ];
        j1 = revtab[k + 1];

        __asm__ volatile (
            "lwc1           %[temp1],       0(%[in2])                           \t\n"
            "lwc1           %[temp2],       0(%[tcos1])                         \t\n"
            "lwc1           %[temp3],       0(%[tsin1])                         \t\n"
            "lwc1           %[temp4],       0(%[in1])                           \t\n"
            "lwc1           %[temp5],       0(%[in4])                           \t\n"
            "mul.s          %[temp9],       %[temp1],   %[temp2]                \t\n"
            "mul.s          %[temp10],      %[temp1],   %[temp3]                \t\n"
            "lwc1           %[temp6],       4(%[tcos1])                         \t\n"
            "lwc1           %[temp7],       4(%[tsin1])                         \t\n"
            "nmsub.s        %[temp9],       %[temp9],   %[temp4],   %[temp3]    \t\n"
            "madd.s         %[temp10],      %[temp10],  %[temp4],   %[temp2]    \t\n"
            "mul.s          %[temp11],      %[temp5],   %[temp6]                \t\n"
            "mul.s          %[temp12],      %[temp5],   %[temp7]                \t\n"
            "lwc1           %[temp8],       0(%[in3])                           \t\n"
            "addiu          %[tcos1],       %[tcos1],   8                       \t\n"
            "addiu          %[tsin1],       %[tsin1],   8                       \t\n"
            "addiu          %[in1],         %[in1],     16                      \t\n"
            "nmsub.s        %[temp11],      %[temp11],  %[temp8],   %[temp7]    \t\n"
            "madd.s         %[temp12],      %[temp12],  %[temp8],   %[temp6]    \t\n"
            "addiu          %[in2],         %[in2],     -16                     \t\n"
            "addiu          %[in3],         %[in3],     16                      \t\n"
            "addiu          %[in4],         %[in4],     -16                     \t\n"

            : [temp1]"=&f"(temp1), [temp2]"=&f"(temp2),
              [temp3]"=&f"(temp3), [temp4]"=&f"(temp4),
              [temp5]"=&f"(temp5), [temp6]"=&f"(temp6),
              [temp7]"=&f"(temp7), [temp8]"=&f"(temp8),
              [temp9]"=&f"(temp9), [temp10]"=&f"(temp10),
              [temp11]"=&f"(temp11), [temp12]"=&f"(temp12),
              [tsin1]"+r"(tsin1), [tcos1]"+r"(tcos1),
              [in1]"+r"(in1), [in2]"+r"(in2),
              [in3]"+r"(in3), [in4]"+r"(in4)
        );

        z[j ].re = temp9;
        z[j ].im = temp10;
        z[j1].re = temp11;
        z[j1].im = temp12;
    }

    s->fft_calc(s, z);

    /* post rotation + reordering */
    /* n8 = 32 or 64 */
    for(k = 0; k < n8; k += 2) {
        tcos1 = &tcos[n8 - k - 2];
        tsin1 = &tsin[n8 - k - 2];
        tcos2 = &tcos[n8 + k];
        tsin2 = &tsin[n8 + k];
        z1 = &z[n8 - k - 2];
        z2 = &z[n8 + k    ];

        __asm__ volatile (
            "lwc1       %[temp1],   12(%[z1])                           \t\n"
            "lwc1       %[temp2],   4(%[tsin1])                         \t\n"
            "lwc1       %[temp3],   4(%[tcos1])                         \t\n"
            "lwc1       %[temp4],   8(%[z1])                            \t\n"
            "lwc1       %[temp5],   4(%[z1])                            \t\n"
            "mul.s      %[temp9],   %[temp1],   %[temp2]                \t\n"
            "mul.s      %[temp10],  %[temp1],   %[temp3]                \t\n"
            "lwc1       %[temp6],   0(%[tsin1])                         \t\n"
            "lwc1       %[temp7],   0(%[tcos1])                         \t\n"
            "nmsub.s    %[temp9],   %[temp9],   %[temp4],   %[temp3]    \t\n"
            "madd.s     %[temp10],  %[temp10],  %[temp4],   %[temp2]    \t\n"
            "mul.s      %[temp11],  %[temp5],   %[temp6]                \t\n"
            "mul.s      %[temp12],  %[temp5],   %[temp7]                \t\n"
            "lwc1       %[temp8],   0(%[z1])                            \t\n"
            "lwc1       %[temp1],   4(%[z2])                            \t\n"
            "lwc1       %[temp2],   0(%[tsin2])                         \t\n"
            "lwc1       %[temp3],   0(%[tcos2])                         \t\n"
            "nmsub.s    %[temp11],  %[temp11],  %[temp8],   %[temp7]    \t\n"
            "madd.s     %[temp12],  %[temp12],  %[temp8],   %[temp6]    \t\n"
            "mul.s      %[temp13],  %[temp1],   %[temp2]                \t\n"
            "mul.s      %[temp14],  %[temp1],   %[temp3]                \t\n"
            "lwc1       %[temp4],   0(%[z2])                            \t\n"
            "lwc1       %[temp5],   12(%[z2])                           \t\n"
            "lwc1       %[temp6],   4(%[tsin2])                         \t\n"
            "lwc1       %[temp7],   4(%[tcos2])                         \t\n"
            "nmsub.s    %[temp13],  %[temp13],  %[temp4],   %[temp3]    \t\n"
            "madd.s     %[temp14],  %[temp14],  %[temp4],   %[temp2]    \t\n"
            "mul.s      %[temp15],  %[temp5],   %[temp6]                \t\n"
            "mul.s      %[temp16],  %[temp5],   %[temp7]                \t\n"
            "lwc1       %[temp8],   8(%[z2])                            \t\n"
            "nmsub.s    %[temp15],  %[temp15],  %[temp8],   %[temp7]    \t\n"
            "madd.s     %[temp16],  %[temp16],  %[temp8],   %[temp6]    \t\n"
            : [temp1]"=&f"(temp1), [temp2]"=&f"(temp2),
              [temp3]"=&f"(temp3), [temp4]"=&f"(temp4),
              [temp5]"=&f"(temp5), [temp6]"=&f"(temp6),
              [temp7]"=&f"(temp7), [temp8]"=&f"(temp8),
              [temp9]"=&f"(temp9), [temp10]"=&f"(temp10),
              [temp11]"=&f"(temp11), [temp12]"=&f"(temp12),
              [temp13]"=&f"(temp13), [temp14]"=&f"(temp14),
              [temp15]"=&f"(temp15), [temp16]"=&f"(temp16)
            : [z1]"r"(z1), [z2]"r"(z2),
              [tsin1]"r"(tsin1), [tcos1]"r"(tcos1),
              [tsin2]"r"(tsin2), [tcos2]"r"(tcos2)
        );

        z1[1].re = temp9;
        z1[1].im = temp14;
        z2[0].re = temp13;
        z2[0].im = temp10;

        z1[0].re = temp11;
        z1[0].im = temp16;
        z2[1].re = temp15;
        z2[1].im = temp12;
    }
}
#endif /* HAVE_INLINE_ASM */

/**
 * Compute inverse MDCT of size N = 2^nbits
 * @param output N samples
 * @param input N/2 samples
 */
static void ff_imdct_calc_mips(FFTContext *s, FFTSample *output, const FFTSample *input)
{
    int k;
    int n = 1 << s->mdct_bits;
    int n2 = n >> 1;
    int n4 = n >> 2;

    ff_imdct_half_mips(s, output+n4, input);

    for(k = 0; k < n4; k+=4) {
        output[k] = -output[n2-k-1];
        output[k+1] = -output[n2-k-2];
        output[k+2] = -output[n2-k-3];
        output[k+3] = -output[n2-k-4];

        output[n-k-1] = output[n2+k];
        output[n-k-2] = output[n2+k+1];
        output[n-k-3] = output[n2+k+2];
        output[n-k-4] = output[n2+k+3];
    }
}

av_cold void ff_fft_init_mips(FFTContext *s)
{
    int n=0;

    ff_fft_lut_init(fft_offsets_lut, 0, 1 << 16, &n);

#if HAVE_INLINE_ASM
    s->fft_calc     = ff_fft_calc_mips;
#endif
#if CONFIG_MDCT
    s->imdct_calc   = ff_imdct_calc_mips;
    s->imdct_half   = ff_imdct_half_mips;
#endif
}
