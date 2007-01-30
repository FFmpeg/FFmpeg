/*
 * MMX and SSE2 optimized snow DSP utils
 * Copyright (c) 2005-2006 Robert Edele <yartrebo@earthlink.net>
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

#include "../avcodec.h"
#include "../snow.h"
#include "x86_cpu.h"

void ff_snow_horizontal_compose97i_sse2(DWTELEM *b, int width){
    const int w2= (width+1)>>1;
    // SSE2 code runs faster with pointers aligned on a 32-byte boundary.
    DWTELEM temp_buf[(width>>1) + 4];
    DWTELEM * const temp = temp_buf + 4 - (((int)temp_buf & 0xF) >> 2);
    const int w_l= (width>>1);
    const int w_r= w2 - 1;
    int i;

    { // Lift 0
        DWTELEM * const ref = b + w2 - 1;
        DWTELEM b_0 = b[0]; //By allowing the first entry in b[0] to be calculated twice
        // (the first time erroneously), we allow the SSE2 code to run an extra pass.
        // The savings in code and time are well worth having to store this value and
        // calculate b[0] correctly afterwards.

        i = 0;
        asm volatile(
            "pcmpeqd   %%xmm7, %%xmm7         \n\t"
            "pslld        $31, %%xmm7         \n\t"
            "psrld        $29, %%xmm7         \n\t"
        ::);
        for(; i<w_l-7; i+=8){
            asm volatile(
                "movdqu   (%1), %%xmm1        \n\t"
                "movdqu 16(%1), %%xmm5        \n\t"
                "movdqu  4(%1), %%xmm2        \n\t"
                "movdqu 20(%1), %%xmm6        \n\t"
                "paddd  %%xmm1, %%xmm2        \n\t"
                "paddd  %%xmm5, %%xmm6        \n\t"
                "movdqa %%xmm2, %%xmm0        \n\t"
                "movdqa %%xmm6, %%xmm4        \n\t"
                "paddd  %%xmm2, %%xmm2        \n\t"
                "paddd  %%xmm6, %%xmm6        \n\t"
                "paddd  %%xmm0, %%xmm2        \n\t"
                "paddd  %%xmm4, %%xmm6        \n\t"
                "paddd  %%xmm7, %%xmm2        \n\t"
                "paddd  %%xmm7, %%xmm6        \n\t"
                "psrad      $3, %%xmm2        \n\t"
                "psrad      $3, %%xmm6        \n\t"
                "movdqa   (%0), %%xmm0        \n\t"
                "movdqa 16(%0), %%xmm4        \n\t"
                "psubd  %%xmm2, %%xmm0        \n\t"
                "psubd  %%xmm6, %%xmm4        \n\t"
                "movdqa %%xmm0, (%0)          \n\t"
                "movdqa %%xmm4, 16(%0)        \n\t"
                :: "r"(&b[i]), "r"(&ref[i])
                : "memory"
            );
        }
        snow_horizontal_compose_lift_lead_out(i, b, b, ref, width, w_l, 0, W_DM, W_DO, W_DS);
        b[0] = b_0 - ((W_DM * 2 * ref[1]+W_DO)>>W_DS);
    }

    { // Lift 1
        DWTELEM * const dst = b+w2;

        i = 0;
        for(; (((long)&dst[i]) & 0xF) && i<w_r; i++){
            dst[i] = dst[i] - (b[i] + b[i + 1]);
        }
        for(; i<w_r-7; i+=8){
            asm volatile(
                "movdqu   (%1), %%xmm1        \n\t"
                "movdqu 16(%1), %%xmm5        \n\t"
                "movdqu  4(%1), %%xmm2        \n\t"
                "movdqu 20(%1), %%xmm6        \n\t"
                "paddd  %%xmm1, %%xmm2        \n\t"
                "paddd  %%xmm5, %%xmm6        \n\t"
                "movdqa   (%0), %%xmm0        \n\t"
                "movdqa 16(%0), %%xmm4        \n\t"
                "psubd  %%xmm2, %%xmm0        \n\t"
                "psubd  %%xmm6, %%xmm4        \n\t"
                "movdqa %%xmm0, (%0)          \n\t"
                "movdqa %%xmm4, 16(%0)        \n\t"
                :: "r"(&dst[i]), "r"(&b[i])
                : "memory"
            );
        }
        snow_horizontal_compose_lift_lead_out(i, dst, dst, b, width, w_r, 1, W_CM, W_CO, W_CS);
    }

    { // Lift 2
        DWTELEM * const ref = b+w2 - 1;
        DWTELEM b_0 = b[0];

        i = 0;
        asm volatile(
            "pslld          $1, %%xmm7       \n\t" /* xmm7 already holds a '4' from 2 lifts ago. */
        ::);
        for(; i<w_l-7; i+=8){
            asm volatile(
                "movdqu   (%1), %%xmm1        \n\t"
                "movdqu 16(%1), %%xmm5        \n\t"
                "movdqu  4(%1), %%xmm0        \n\t"
                "movdqu 20(%1), %%xmm4        \n\t"
                "paddd  %%xmm1, %%xmm0        \n\t"
                "paddd  %%xmm5, %%xmm4        \n\t"
                "movdqa %%xmm7, %%xmm1        \n\t"
                "movdqa %%xmm7, %%xmm5        \n\t"
                "psubd  %%xmm0, %%xmm1        \n\t"
                "psubd  %%xmm4, %%xmm5        \n\t"
                "movdqa   (%0), %%xmm0        \n\t"
                "movdqa 16(%0), %%xmm4        \n\t"
                "pslld      $2, %%xmm0        \n\t"
                "pslld      $2, %%xmm4        \n\t"
                "psubd  %%xmm0, %%xmm1        \n\t"
                "psubd  %%xmm4, %%xmm5        \n\t"
                "psrad      $4, %%xmm1        \n\t"
                "psrad      $4, %%xmm5        \n\t"
                "movdqa   (%0), %%xmm0        \n\t"
                "movdqa 16(%0), %%xmm4        \n\t"
                "psubd  %%xmm1, %%xmm0        \n\t"
                "psubd  %%xmm5, %%xmm4        \n\t"
                "movdqa %%xmm0, (%0)          \n\t"
                "movdqa %%xmm4, 16(%0)        \n\t"
                :: "r"(&b[i]), "r"(&ref[i])
                : "memory"
            );
        }
        snow_horizontal_compose_liftS_lead_out(i, b, b, ref, width, w_l);
        b[0] = b_0 - (((-2 * ref[1] + W_BO) - 4 * b_0) >> W_BS);
    }

    { // Lift 3
        DWTELEM * const src = b+w2;

        i = 0;
        for(; (((long)&temp[i]) & 0xF) && i<w_r; i++){
            temp[i] = src[i] - ((-W_AM*(b[i] + b[i+1]))>>W_AS);
        }
        for(; i<w_r-7; i+=8){
            asm volatile(
                "movdqu  4(%1), %%xmm2        \n\t"
                "movdqu 20(%1), %%xmm6        \n\t"
                "paddd    (%1), %%xmm2        \n\t"
                "paddd  16(%1), %%xmm6        \n\t"
                "movdqa %%xmm2, %%xmm0        \n\t"
                "movdqa %%xmm6, %%xmm4        \n\t"
                "pslld      $2, %%xmm2        \n\t"
                "pslld      $2, %%xmm6        \n\t"
                "psubd  %%xmm2, %%xmm0        \n\t"
                "psubd  %%xmm6, %%xmm4        \n\t"
                "psrad      $1, %%xmm0        \n\t"
                "psrad      $1, %%xmm4        \n\t"
                "movdqu   (%0), %%xmm2        \n\t"
                "movdqu 16(%0), %%xmm6        \n\t"
                "psubd  %%xmm0, %%xmm2        \n\t"
                "psubd  %%xmm4, %%xmm6        \n\t"
                "movdqa %%xmm2, (%2)          \n\t"
                "movdqa %%xmm6, 16(%2)        \n\t"
                :: "r"(&src[i]), "r"(&b[i]), "r"(&temp[i])
                 : "memory"
               );
        }
        snow_horizontal_compose_lift_lead_out(i, temp, src, b, width, w_r, 1, -W_AM, W_AO, W_AS);
    }

    {
        snow_interleave_line_header(&i, width, b, temp);

        for (; (i & 0x1E) != 0x1E; i-=2){
            b[i+1] = temp[i>>1];
            b[i] = b[i>>1];
        }
        for (i-=30; i>=0; i-=32){
            asm volatile(
                "movdqa      (%1), %%xmm0       \n\t"
                "movdqa    16(%1), %%xmm2       \n\t"
                "movdqa    32(%1), %%xmm4       \n\t"
                "movdqa    48(%1), %%xmm6       \n\t"
                "movdqa      (%1), %%xmm1       \n\t"
                "movdqa    16(%1), %%xmm3       \n\t"
                "movdqa    32(%1), %%xmm5       \n\t"
                "movdqa    48(%1), %%xmm7       \n\t"
                "punpckldq   (%2), %%xmm0       \n\t"
                "punpckldq 16(%2), %%xmm2       \n\t"
                "punpckldq 32(%2), %%xmm4       \n\t"
                "punpckldq 48(%2), %%xmm6       \n\t"
                "movdqa    %%xmm0, (%0)         \n\t"
                "movdqa    %%xmm2, 32(%0)       \n\t"
                "movdqa    %%xmm4, 64(%0)       \n\t"
                "movdqa    %%xmm6, 96(%0)       \n\t"
                "punpckhdq   (%2), %%xmm1       \n\t"
                "punpckhdq 16(%2), %%xmm3       \n\t"
                "punpckhdq 32(%2), %%xmm5       \n\t"
                "punpckhdq 48(%2), %%xmm7       \n\t"
                "movdqa    %%xmm1, 16(%0)       \n\t"
                "movdqa    %%xmm3, 48(%0)       \n\t"
                "movdqa    %%xmm5, 80(%0)       \n\t"
                "movdqa    %%xmm7, 112(%0)      \n\t"
                :: "r"(&(b)[i]), "r"(&(b)[i>>1]), "r"(&(temp)[i>>1])
                 : "memory"
               );
        }
    }
}

void ff_snow_horizontal_compose97i_mmx(DWTELEM *b, int width){
    const int w2= (width+1)>>1;
    DWTELEM temp[width >> 1];
    const int w_l= (width>>1);
    const int w_r= w2 - 1;
    int i;

    { // Lift 0
        DWTELEM * const ref = b + w2 - 1;

        i = 1;
        b[0] = b[0] - ((W_DM * 2 * ref[1]+W_DO)>>W_DS);
        asm volatile(
            "pcmpeqd    %%mm7, %%mm7         \n\t"
            "pslld        $31, %%mm7         \n\t"
            "psrld        $29, %%mm7         \n\t"
           ::);
        for(; i<w_l-3; i+=4){
            asm volatile(
                "movq     (%1), %%mm2        \n\t"
                "movq    8(%1), %%mm6        \n\t"
                "paddd   4(%1), %%mm2        \n\t"
                "paddd  12(%1), %%mm6        \n\t"
                "movq    %%mm2, %%mm0        \n\t"
                "movq    %%mm6, %%mm4        \n\t"
                "paddd   %%mm2, %%mm2        \n\t"
                "paddd   %%mm6, %%mm6        \n\t"
                "paddd   %%mm0, %%mm2        \n\t"
                "paddd   %%mm4, %%mm6        \n\t"
                "paddd   %%mm7, %%mm2        \n\t"
                "paddd   %%mm7, %%mm6        \n\t"
                "psrad      $3, %%mm2        \n\t"
                "psrad      $3, %%mm6        \n\t"
                "movq     (%0), %%mm0        \n\t"
                "movq    8(%0), %%mm4        \n\t"
                "psubd   %%mm2, %%mm0        \n\t"
                "psubd   %%mm6, %%mm4        \n\t"
                "movq    %%mm0, (%0)         \n\t"
                "movq    %%mm4, 8(%0)        \n\t"
                :: "r"(&b[i]), "r"(&ref[i])
                 : "memory"
               );
        }
        snow_horizontal_compose_lift_lead_out(i, b, b, ref, width, w_l, 0, W_DM, W_DO, W_DS);
    }

    { // Lift 1
        DWTELEM * const dst = b+w2;

        i = 0;
        for(; i<w_r-3; i+=4){
            asm volatile(
                "movq     (%1), %%mm2        \n\t"
                "movq    8(%1), %%mm6        \n\t"
                "paddd   4(%1), %%mm2        \n\t"
                "paddd  12(%1), %%mm6        \n\t"
                "movq     (%0), %%mm0        \n\t"
                "movq    8(%0), %%mm4        \n\t"
                "psubd   %%mm2, %%mm0        \n\t"
                "psubd   %%mm6, %%mm4        \n\t"
                "movq    %%mm0, (%0)         \n\t"
                "movq    %%mm4, 8(%0)        \n\t"
                :: "r"(&dst[i]), "r"(&b[i])
                 : "memory"
               );
        }
        snow_horizontal_compose_lift_lead_out(i, dst, dst, b, width, w_r, 1, W_CM, W_CO, W_CS);
    }

    { // Lift 2
        DWTELEM * const ref = b+w2 - 1;

        i = 1;
        b[0] = b[0] - (((-2 * ref[1] + W_BO) - 4 * b[0]) >> W_BS);
        asm volatile(
            "pslld          $1, %%mm7       \n\t" /* xmm7 already holds a '4' from 2 lifts ago. */
           ::);
        for(; i<w_l-3; i+=4){
            asm volatile(
                "movq     (%1), %%mm0        \n\t"
                "movq    8(%1), %%mm4        \n\t"
                "paddd   4(%1), %%mm0        \n\t"
                "paddd  12(%1), %%mm4        \n\t"
                "movq    %%mm7, %%mm1        \n\t"
                "movq    %%mm7, %%mm5        \n\t"
                "psubd   %%mm0, %%mm1        \n\t"
                "psubd   %%mm4, %%mm5        \n\t"
                "movq     (%0), %%mm0        \n\t"
                "movq    8(%0), %%mm4        \n\t"
                "pslld      $2, %%mm0        \n\t"
                "pslld      $2, %%mm4        \n\t"
                "psubd   %%mm0, %%mm1        \n\t"
                "psubd   %%mm4, %%mm5        \n\t"
                "psrad      $4, %%mm1        \n\t"
                "psrad      $4, %%mm5        \n\t"
                "movq     (%0), %%mm0        \n\t"
                "movq    8(%0), %%mm4        \n\t"
                "psubd   %%mm1, %%mm0        \n\t"
                "psubd   %%mm5, %%mm4        \n\t"
                "movq    %%mm0, (%0)         \n\t"
                "movq    %%mm4, 8(%0)        \n\t"
                :: "r"(&b[i]), "r"(&ref[i])
                 : "memory"
               );
        }
        snow_horizontal_compose_liftS_lead_out(i, b, b, ref, width, w_l);
    }

    { // Lift 3
        DWTELEM * const src = b+w2;
        i = 0;

        for(; i<w_r-3; i+=4){
            asm volatile(
                "movq    4(%1), %%mm2        \n\t"
                "movq   12(%1), %%mm6        \n\t"
                "paddd    (%1), %%mm2        \n\t"
                "paddd   8(%1), %%mm6        \n\t"
                "movq    %%mm2, %%mm0        \n\t"
                "movq    %%mm6, %%mm4        \n\t"
                "pslld      $2, %%mm2        \n\t"
                "pslld      $2, %%mm6        \n\t"
                "psubd   %%mm2, %%mm0        \n\t"
                "psubd   %%mm6, %%mm4        \n\t"
                "psrad      $1, %%mm0        \n\t"
                "psrad      $1, %%mm4        \n\t"
                "movq     (%0), %%mm2        \n\t"
                "movq    8(%0), %%mm6        \n\t"
                "psubd   %%mm0, %%mm2        \n\t"
                "psubd   %%mm4, %%mm6        \n\t"
                "movq    %%mm2, (%2)         \n\t"
                "movq    %%mm6, 8(%2)        \n\t"
                :: "r"(&src[i]), "r"(&b[i]), "r"(&temp[i])
                 : "memory"
               );
        }
        snow_horizontal_compose_lift_lead_out(i, temp, src, b, width, w_r, 1, -W_AM, W_AO, W_AS);
    }

    {
        snow_interleave_line_header(&i, width, b, temp);

        for (; (i & 0xE) != 0xE; i-=2){
            b[i+1] = temp[i>>1];
            b[i] = b[i>>1];
        }
        for (i-=14; i>=0; i-=16){
            asm volatile(
                "movq        (%1), %%mm0       \n\t"
                "movq       8(%1), %%mm2       \n\t"
                "movq      16(%1), %%mm4       \n\t"
                "movq      24(%1), %%mm6       \n\t"
                "movq        (%1), %%mm1       \n\t"
                "movq       8(%1), %%mm3       \n\t"
                "movq      16(%1), %%mm5       \n\t"
                "movq      24(%1), %%mm7       \n\t"
                "punpckldq   (%2), %%mm0       \n\t"
                "punpckldq  8(%2), %%mm2       \n\t"
                "punpckldq 16(%2), %%mm4       \n\t"
                "punpckldq 24(%2), %%mm6       \n\t"
                "movq       %%mm0, (%0)        \n\t"
                "movq       %%mm2, 16(%0)      \n\t"
                "movq       %%mm4, 32(%0)      \n\t"
                "movq       %%mm6, 48(%0)      \n\t"
                "punpckhdq   (%2), %%mm1       \n\t"
                "punpckhdq  8(%2), %%mm3       \n\t"
                "punpckhdq 16(%2), %%mm5       \n\t"
                "punpckhdq 24(%2), %%mm7       \n\t"
                "movq       %%mm1, 8(%0)       \n\t"
                "movq       %%mm3, 24(%0)      \n\t"
                "movq       %%mm5, 40(%0)      \n\t"
                "movq       %%mm7, 56(%0)      \n\t"
                :: "r"(&b[i]), "r"(&b[i>>1]), "r"(&temp[i>>1])
                 : "memory"
               );
        }
    }
}

#define snow_vertical_compose_sse2_load_add(op,r,t0,t1,t2,t3)\
        ""op" (%%"r",%%"REG_d",4), %%"t0"      \n\t"\
        ""op" 16(%%"r",%%"REG_d",4), %%"t1"    \n\t"\
        ""op" 32(%%"r",%%"REG_d",4), %%"t2"    \n\t"\
        ""op" 48(%%"r",%%"REG_d",4), %%"t3"    \n\t"

#define snow_vertical_compose_sse2_load(r,t0,t1,t2,t3)\
        snow_vertical_compose_sse2_load_add("movdqa",r,t0,t1,t2,t3)

#define snow_vertical_compose_sse2_add(r,t0,t1,t2,t3)\
        snow_vertical_compose_sse2_load_add("paddd",r,t0,t1,t2,t3)

#define snow_vertical_compose_sse2_sub(s0,s1,s2,s3,t0,t1,t2,t3)\
        "psubd %%"s0", %%"t0" \n\t"\
        "psubd %%"s1", %%"t1" \n\t"\
        "psubd %%"s2", %%"t2" \n\t"\
        "psubd %%"s3", %%"t3" \n\t"

#define snow_vertical_compose_sse2_store(w,s0,s1,s2,s3)\
        "movdqa %%"s0", (%%"w",%%"REG_d",4)      \n\t"\
        "movdqa %%"s1", 16(%%"w",%%"REG_d",4)    \n\t"\
        "movdqa %%"s2", 32(%%"w",%%"REG_d",4)    \n\t"\
        "movdqa %%"s3", 48(%%"w",%%"REG_d",4)    \n\t"

#define snow_vertical_compose_sse2_sra(n,t0,t1,t2,t3)\
        "psrad $"n", %%"t0" \n\t"\
        "psrad $"n", %%"t1" \n\t"\
        "psrad $"n", %%"t2" \n\t"\
        "psrad $"n", %%"t3" \n\t"

#define snow_vertical_compose_sse2_r2r_add(s0,s1,s2,s3,t0,t1,t2,t3)\
        "paddd %%"s0", %%"t0" \n\t"\
        "paddd %%"s1", %%"t1" \n\t"\
        "paddd %%"s2", %%"t2" \n\t"\
        "paddd %%"s3", %%"t3" \n\t"

#define snow_vertical_compose_sse2_sll(n,t0,t1,t2,t3)\
        "pslld $"n", %%"t0" \n\t"\
        "pslld $"n", %%"t1" \n\t"\
        "pslld $"n", %%"t2" \n\t"\
        "pslld $"n", %%"t3" \n\t"

#define snow_vertical_compose_sse2_move(s0,s1,s2,s3,t0,t1,t2,t3)\
        "movdqa %%"s0", %%"t0" \n\t"\
        "movdqa %%"s1", %%"t1" \n\t"\
        "movdqa %%"s2", %%"t2" \n\t"\
        "movdqa %%"s3", %%"t3" \n\t"

void ff_snow_vertical_compose97i_sse2(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, DWTELEM *b3, DWTELEM *b4, DWTELEM *b5, int width){
    long i = width;

    while(i & 0xF)
    {
        i--;
        b4[i] -= (W_DM*(b3[i] + b5[i])+W_DO)>>W_DS;
        b3[i] -= (W_CM*(b2[i] + b4[i])+W_CO)>>W_CS;
        b2[i] += (W_BM*(b1[i] + b3[i])+4*b2[i]+W_BO)>>W_BS;
        b1[i] += (W_AM*(b0[i] + b2[i])+W_AO)>>W_AS;
    }

         asm volatile (
        "jmp 2f                                      \n\t"
        "1:                                          \n\t"

        "mov %6, %%"REG_a"                           \n\t"
        "mov %4, %%"REG_S"                           \n\t"

        snow_vertical_compose_sse2_load(REG_S,"xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_add(REG_a,"xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_move("xmm0","xmm2","xmm4","xmm6","xmm1","xmm3","xmm5","xmm7")
        snow_vertical_compose_sse2_sll("1","xmm0","xmm2","xmm4","xmm6")\
        snow_vertical_compose_sse2_r2r_add("xmm1","xmm3","xmm5","xmm7","xmm0","xmm2","xmm4","xmm6")

        "pcmpeqd %%xmm1, %%xmm1                      \n\t"
        "pslld $31, %%xmm1                           \n\t"
        "psrld $29, %%xmm1                           \n\t"
        "mov %5, %%"REG_a"                           \n\t"

        snow_vertical_compose_sse2_r2r_add("xmm1","xmm1","xmm1","xmm1","xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_sra("3","xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_load(REG_a,"xmm1","xmm3","xmm5","xmm7")
        snow_vertical_compose_sse2_sub("xmm0","xmm2","xmm4","xmm6","xmm1","xmm3","xmm5","xmm7")
        snow_vertical_compose_sse2_store(REG_a,"xmm1","xmm3","xmm5","xmm7")
        "mov %3, %%"REG_c"                           \n\t"
        snow_vertical_compose_sse2_load(REG_S,"xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_add(REG_c,"xmm1","xmm3","xmm5","xmm7")
        snow_vertical_compose_sse2_sub("xmm1","xmm3","xmm5","xmm7","xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_store(REG_S,"xmm0","xmm2","xmm4","xmm6")
        "mov %2, %%"REG_a"                           \n\t"
        snow_vertical_compose_sse2_load(REG_c,"xmm1","xmm3","xmm5","xmm7")
        snow_vertical_compose_sse2_add(REG_a,"xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_sll("2","xmm1","xmm3","xmm5","xmm7")\
        snow_vertical_compose_sse2_r2r_add("xmm1","xmm3","xmm5","xmm7","xmm0","xmm2","xmm4","xmm6")

        "pcmpeqd %%xmm1, %%xmm1                      \n\t"
        "pslld $31, %%xmm1                           \n\t"
        "psrld $28, %%xmm1                           \n\t"
        "mov %1, %%"REG_S"                           \n\t"

        snow_vertical_compose_sse2_r2r_add("xmm1","xmm1","xmm1","xmm1","xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_sra("4","xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_add(REG_c,"xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_store(REG_c,"xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_add(REG_S,"xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_move("xmm0","xmm2","xmm4","xmm6","xmm1","xmm3","xmm5","xmm7")
        snow_vertical_compose_sse2_sll("1","xmm0","xmm2","xmm4","xmm6")\
        snow_vertical_compose_sse2_r2r_add("xmm1","xmm3","xmm5","xmm7","xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_sra("1","xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_add(REG_a,"xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_store(REG_a,"xmm0","xmm2","xmm4","xmm6")

        "2:                                          \n\t"
        "sub $16, %%"REG_d"                          \n\t"
        "jge 1b                                      \n\t"
        :"+d"(i)
        :
        "m"(b0),"m"(b1),"m"(b2),"m"(b3),"m"(b4),"m"(b5):
        "%"REG_a"","%"REG_S"","%"REG_c"");
}

#define snow_vertical_compose_mmx_load_add(op,r,t0,t1,t2,t3)\
        ""op" (%%"r",%%"REG_d",4), %%"t0"   \n\t"\
        ""op" 8(%%"r",%%"REG_d",4), %%"t1"  \n\t"\
        ""op" 16(%%"r",%%"REG_d",4), %%"t2" \n\t"\
        ""op" 24(%%"r",%%"REG_d",4), %%"t3" \n\t"

#define snow_vertical_compose_mmx_load(r,t0,t1,t2,t3)\
        snow_vertical_compose_mmx_load_add("movq",r,t0,t1,t2,t3)

#define snow_vertical_compose_mmx_add(r,t0,t1,t2,t3)\
        snow_vertical_compose_mmx_load_add("paddd",r,t0,t1,t2,t3)

#define snow_vertical_compose_mmx_sub(s0,s1,s2,s3,t0,t1,t2,t3)\
        snow_vertical_compose_sse2_sub(s0,s1,s2,s3,t0,t1,t2,t3)

#define snow_vertical_compose_mmx_store(w,s0,s1,s2,s3)\
        "movq %%"s0", (%%"w",%%"REG_d",4)   \n\t"\
        "movq %%"s1", 8(%%"w",%%"REG_d",4)  \n\t"\
        "movq %%"s2", 16(%%"w",%%"REG_d",4) \n\t"\
        "movq %%"s3", 24(%%"w",%%"REG_d",4) \n\t"

#define snow_vertical_compose_mmx_sra(n,t0,t1,t2,t3)\
        snow_vertical_compose_sse2_sra(n,t0,t1,t2,t3)

#define snow_vertical_compose_mmx_r2r_add(s0,s1,s2,s3,t0,t1,t2,t3)\
        snow_vertical_compose_sse2_r2r_add(s0,s1,s2,s3,t0,t1,t2,t3)

#define snow_vertical_compose_mmx_sll(n,t0,t1,t2,t3)\
        snow_vertical_compose_sse2_sll(n,t0,t1,t2,t3)

#define snow_vertical_compose_mmx_move(s0,s1,s2,s3,t0,t1,t2,t3)\
        "movq %%"s0", %%"t0" \n\t"\
        "movq %%"s1", %%"t1" \n\t"\
        "movq %%"s2", %%"t2" \n\t"\
        "movq %%"s3", %%"t3" \n\t"

void ff_snow_vertical_compose97i_mmx(DWTELEM *b0, DWTELEM *b1, DWTELEM *b2, DWTELEM *b3, DWTELEM *b4, DWTELEM *b5, int width){
    long i = width;
    while(i & 0x7)
    {
        i--;
        b4[i] -= (W_DM*(b3[i] + b5[i])+W_DO)>>W_DS;
        b3[i] -= (W_CM*(b2[i] + b4[i])+W_CO)>>W_CS;
        b2[i] += (W_BM*(b1[i] + b3[i])+4*b2[i]+W_BO)>>W_BS;
        b1[i] += (W_AM*(b0[i] + b2[i])+W_AO)>>W_AS;
    }

    asm volatile(
        "jmp 2f                                      \n\t"
        "1:                                          \n\t"

        "mov %6, %%"REG_a"                           \n\t"
        "mov %4, %%"REG_S"                           \n\t"

        snow_vertical_compose_mmx_load(REG_S,"mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_add(REG_a,"mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_move("mm0","mm2","mm4","mm6","mm1","mm3","mm5","mm7")
        snow_vertical_compose_mmx_sll("1","mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_r2r_add("mm1","mm3","mm5","mm7","mm0","mm2","mm4","mm6")

        "pcmpeqd %%mm1, %%mm1                        \n\t"
        "pslld $31, %%mm1                            \n\t"
        "psrld $29, %%mm1                            \n\t"
        "mov %5, %%"REG_a"                           \n\t"

        snow_vertical_compose_mmx_r2r_add("mm1","mm1","mm1","mm1","mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_sra("3","mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_load(REG_a,"mm1","mm3","mm5","mm7")
        snow_vertical_compose_mmx_sub("mm0","mm2","mm4","mm6","mm1","mm3","mm5","mm7")
        snow_vertical_compose_mmx_store(REG_a,"mm1","mm3","mm5","mm7")
        "mov %3, %%"REG_c"                           \n\t"
        snow_vertical_compose_mmx_load(REG_S,"mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_add(REG_c,"mm1","mm3","mm5","mm7")
        snow_vertical_compose_mmx_sub("mm1","mm3","mm5","mm7","mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_store(REG_S,"mm0","mm2","mm4","mm6")
        "mov %2, %%"REG_a"                           \n\t"
        snow_vertical_compose_mmx_load(REG_c,"mm1","mm3","mm5","mm7")
        snow_vertical_compose_mmx_add(REG_a,"mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_sll("2","mm1","mm3","mm5","mm7")
        snow_vertical_compose_mmx_r2r_add("mm1","mm3","mm5","mm7","mm0","mm2","mm4","mm6")

        "pcmpeqd %%mm1, %%mm1                        \n\t"
        "pslld $31, %%mm1                            \n\t"
        "psrld $28, %%mm1                            \n\t"
        "mov %1, %%"REG_S"                           \n\t"

        snow_vertical_compose_mmx_r2r_add("mm1","mm1","mm1","mm1","mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_sra("4","mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_add(REG_c,"mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_store(REG_c,"mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_add(REG_S,"mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_move("mm0","mm2","mm4","mm6","mm1","mm3","mm5","mm7")
        snow_vertical_compose_mmx_sll("1","mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_r2r_add("mm1","mm3","mm5","mm7","mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_sra("1","mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_add(REG_a,"mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_store(REG_a,"mm0","mm2","mm4","mm6")

        "2:                                          \n\t"
        "sub $8, %%"REG_d"                           \n\t"
        "jge 1b                                      \n\t"
        :"+d"(i)
        :
        "m"(b0),"m"(b1),"m"(b2),"m"(b3),"m"(b4),"m"(b5):
        "%"REG_a"","%"REG_S"","%"REG_c"");
}

#define snow_inner_add_yblock_sse2_header \
    DWTELEM * * dst_array = sb->line + src_y;\
    long tmp;\
    asm volatile(\
             "mov  %7, %%"REG_c"             \n\t"\
             "mov  %6, %2                    \n\t"\
             "mov  %4, %%"REG_S"             \n\t"\
             "pxor %%xmm7, %%xmm7            \n\t" /* 0 */\
             "pcmpeqd %%xmm3, %%xmm3         \n\t"\
             "pslld $31, %%xmm3              \n\t"\
             "psrld $24, %%xmm3              \n\t" /* FRAC_BITS >> 1 */\
             "1:                             \n\t"\
             "mov %1, %%"REG_D"              \n\t"\
             "mov (%%"REG_D"), %%"REG_D"     \n\t"\
             "add %3, %%"REG_D"              \n\t"

#define snow_inner_add_yblock_sse2_start_8(out_reg1, out_reg2, ptr_offset, s_offset)\
             "mov "PTR_SIZE"*"ptr_offset"(%%"REG_a"), %%"REG_d"; \n\t"\
             "movq (%%"REG_d"), %%"out_reg1" \n\t"\
             "movq (%%"REG_d", %%"REG_c"), %%"out_reg2" \n\t"\
             "punpcklbw %%xmm7, %%"out_reg1" \n\t"\
             "punpcklbw %%xmm7, %%"out_reg2" \n\t"\
             "movq "s_offset"(%%"REG_S"), %%xmm0 \n\t"\
             "movq "s_offset"+16(%%"REG_S"), %%xmm4 \n\t"\
             "punpcklbw %%xmm7, %%xmm0       \n\t"\
             "punpcklbw %%xmm7, %%xmm4       \n\t"\
             "pmullw %%xmm0, %%"out_reg1"    \n\t"\
             "pmullw %%xmm4, %%"out_reg2"    \n\t"

#define snow_inner_add_yblock_sse2_start_16(out_reg1, out_reg2, ptr_offset, s_offset)\
             "mov "PTR_SIZE"*"ptr_offset"(%%"REG_a"), %%"REG_d"; \n\t"\
             "movq (%%"REG_d"), %%"out_reg1" \n\t"\
             "movq 8(%%"REG_d"), %%"out_reg2" \n\t"\
             "punpcklbw %%xmm7, %%"out_reg1" \n\t"\
             "punpcklbw %%xmm7, %%"out_reg2" \n\t"\
             "movq "s_offset"(%%"REG_S"), %%xmm0 \n\t"\
             "movq "s_offset"+8(%%"REG_S"), %%xmm4 \n\t"\
             "punpcklbw %%xmm7, %%xmm0       \n\t"\
             "punpcklbw %%xmm7, %%xmm4       \n\t"\
             "pmullw %%xmm0, %%"out_reg1"    \n\t"\
             "pmullw %%xmm4, %%"out_reg2"    \n\t"

#define snow_inner_add_yblock_sse2_accum_8(ptr_offset, s_offset) \
             snow_inner_add_yblock_sse2_start_8("xmm2", "xmm6", ptr_offset, s_offset)\
             "paddusw %%xmm2, %%xmm1         \n\t"\
             "paddusw %%xmm6, %%xmm5         \n\t"

#define snow_inner_add_yblock_sse2_accum_16(ptr_offset, s_offset) \
             snow_inner_add_yblock_sse2_start_16("xmm2", "xmm6", ptr_offset, s_offset)\
             "paddusw %%xmm2, %%xmm1         \n\t"\
             "paddusw %%xmm6, %%xmm5         \n\t"

#define snow_inner_add_yblock_sse2_end_common1\
             "add $32, %%"REG_S"             \n\t"\
             "add %%"REG_c", %0              \n\t"\
             "add %%"REG_c", "PTR_SIZE"*3(%%"REG_a");\n\t"\
             "add %%"REG_c", "PTR_SIZE"*2(%%"REG_a");\n\t"\
             "add %%"REG_c", "PTR_SIZE"*1(%%"REG_a");\n\t"\
             "add %%"REG_c", (%%"REG_a")     \n\t"

#define snow_inner_add_yblock_sse2_end_common2\
             "jnz 1b                         \n\t"\
             :"+m"(dst8),"+m"(dst_array),"=&r"(tmp)\
             :\
             "rm"((long)(src_x<<2)),"m"(obmc),"a"(block),"m"((long)b_h),"m"((long)src_stride):\
             "%"REG_c"","%"REG_S"","%"REG_D"","%"REG_d"");

#define snow_inner_add_yblock_sse2_end_8\
             "sal $1, %%"REG_c"              \n\t"\
             "add $"PTR_SIZE"*2, %1          \n\t"\
             snow_inner_add_yblock_sse2_end_common1\
             "sar $1, %%"REG_c"              \n\t"\
             "sub $2, %2                     \n\t"\
             snow_inner_add_yblock_sse2_end_common2

#define snow_inner_add_yblock_sse2_end_16\
             "add $"PTR_SIZE"*1, %1          \n\t"\
             snow_inner_add_yblock_sse2_end_common1\
             "dec %2                         \n\t"\
             snow_inner_add_yblock_sse2_end_common2

static void inner_add_yblock_bw_8_obmc_16_bh_even_sse2(const uint8_t *obmc, const long obmc_stride, uint8_t * * block, int b_w, long b_h,
                      int src_x, int src_y, long src_stride, slice_buffer * sb, int add, uint8_t * dst8){
snow_inner_add_yblock_sse2_header
snow_inner_add_yblock_sse2_start_8("xmm1", "xmm5", "3", "0")
snow_inner_add_yblock_sse2_accum_8("2", "8")
snow_inner_add_yblock_sse2_accum_8("1", "128")
snow_inner_add_yblock_sse2_accum_8("0", "136")

             "mov %0, %%"REG_d"              \n\t"
             "movdqa (%%"REG_D"), %%xmm0     \n\t"
             "movdqa %%xmm1, %%xmm2          \n\t"

             "punpckhwd %%xmm7, %%xmm1       \n\t"
             "punpcklwd %%xmm7, %%xmm2       \n\t"
             "paddd %%xmm2, %%xmm0           \n\t"
             "movdqa 16(%%"REG_D"), %%xmm2   \n\t"
             "paddd %%xmm1, %%xmm2           \n\t"
             "paddd %%xmm3, %%xmm0           \n\t"
             "paddd %%xmm3, %%xmm2           \n\t"

             "mov %1, %%"REG_D"              \n\t"
             "mov "PTR_SIZE"(%%"REG_D"), %%"REG_D";\n\t"
             "add %3, %%"REG_D"              \n\t"

             "movdqa (%%"REG_D"), %%xmm4     \n\t"
             "movdqa %%xmm5, %%xmm6          \n\t"
             "punpckhwd %%xmm7, %%xmm5       \n\t"
             "punpcklwd %%xmm7, %%xmm6       \n\t"
             "paddd %%xmm6, %%xmm4           \n\t"
             "movdqa 16(%%"REG_D"), %%xmm6   \n\t"
             "paddd %%xmm5, %%xmm6           \n\t"
             "paddd %%xmm3, %%xmm4           \n\t"
             "paddd %%xmm3, %%xmm6           \n\t"

             "psrad $8, %%xmm0               \n\t" /* FRAC_BITS. */
             "psrad $8, %%xmm2               \n\t" /* FRAC_BITS. */
             "packssdw %%xmm2, %%xmm0        \n\t"
             "packuswb %%xmm7, %%xmm0        \n\t"
             "movq %%xmm0, (%%"REG_d")       \n\t"

             "psrad $8, %%xmm4               \n\t" /* FRAC_BITS. */
             "psrad $8, %%xmm6               \n\t" /* FRAC_BITS. */
             "packssdw %%xmm6, %%xmm4        \n\t"
             "packuswb %%xmm7, %%xmm4        \n\t"
             "movq %%xmm4, (%%"REG_d",%%"REG_c");\n\t"
snow_inner_add_yblock_sse2_end_8
}

static void inner_add_yblock_bw_16_obmc_32_sse2(const uint8_t *obmc, const long obmc_stride, uint8_t * * block, int b_w, long b_h,
                      int src_x, int src_y, long src_stride, slice_buffer * sb, int add, uint8_t * dst8){
snow_inner_add_yblock_sse2_header
snow_inner_add_yblock_sse2_start_16("xmm1", "xmm5", "3", "0")
snow_inner_add_yblock_sse2_accum_16("2", "16")
snow_inner_add_yblock_sse2_accum_16("1", "512")
snow_inner_add_yblock_sse2_accum_16("0", "528")

             "mov %0, %%"REG_d"              \n\t"
             "movdqa %%xmm1, %%xmm0          \n\t"
             "movdqa %%xmm5, %%xmm4          \n\t"
             "punpcklwd %%xmm7, %%xmm0       \n\t"
             "paddd (%%"REG_D"), %%xmm0      \n\t"
             "punpckhwd %%xmm7, %%xmm1       \n\t"
             "paddd 16(%%"REG_D"), %%xmm1    \n\t"
             "punpcklwd %%xmm7, %%xmm4       \n\t"
             "paddd 32(%%"REG_D"), %%xmm4    \n\t"
             "punpckhwd %%xmm7, %%xmm5       \n\t"
             "paddd 48(%%"REG_D"), %%xmm5    \n\t"
             "paddd %%xmm3, %%xmm0           \n\t"
             "paddd %%xmm3, %%xmm1           \n\t"
             "paddd %%xmm3, %%xmm4           \n\t"
             "paddd %%xmm3, %%xmm5           \n\t"
             "psrad $8, %%xmm0               \n\t" /* FRAC_BITS. */
             "psrad $8, %%xmm1               \n\t" /* FRAC_BITS. */
             "psrad $8, %%xmm4               \n\t" /* FRAC_BITS. */
             "psrad $8, %%xmm5               \n\t" /* FRAC_BITS. */

             "packssdw %%xmm1, %%xmm0        \n\t"
             "packssdw %%xmm5, %%xmm4        \n\t"
             "packuswb %%xmm4, %%xmm0        \n\t"

             "movdqu %%xmm0, (%%"REG_d")       \n\t"

snow_inner_add_yblock_sse2_end_16
}

#define snow_inner_add_yblock_mmx_header \
    DWTELEM * * dst_array = sb->line + src_y;\
    long tmp;\
    asm volatile(\
             "mov  %7, %%"REG_c"             \n\t"\
             "mov  %6, %2                    \n\t"\
             "mov  %4, %%"REG_S"             \n\t"\
             "pxor %%mm7, %%mm7              \n\t" /* 0 */\
             "pcmpeqd %%mm3, %%mm3           \n\t"\
             "pslld $31, %%mm3               \n\t"\
             "psrld $24, %%mm3               \n\t" /* FRAC_BITS >> 1 */\
             "1:                             \n\t"\
             "mov %1, %%"REG_D"              \n\t"\
             "mov (%%"REG_D"), %%"REG_D"     \n\t"\
             "add %3, %%"REG_D"              \n\t"

#define snow_inner_add_yblock_mmx_start(out_reg1, out_reg2, ptr_offset, s_offset, d_offset)\
             "mov "PTR_SIZE"*"ptr_offset"(%%"REG_a"), %%"REG_d"; \n\t"\
             "movd "d_offset"(%%"REG_d"), %%"out_reg1" \n\t"\
             "movd "d_offset"+4(%%"REG_d"), %%"out_reg2" \n\t"\
             "punpcklbw %%mm7, %%"out_reg1" \n\t"\
             "punpcklbw %%mm7, %%"out_reg2" \n\t"\
             "movd "s_offset"(%%"REG_S"), %%mm0 \n\t"\
             "movd "s_offset"+4(%%"REG_S"), %%mm4 \n\t"\
             "punpcklbw %%mm7, %%mm0       \n\t"\
             "punpcklbw %%mm7, %%mm4       \n\t"\
             "pmullw %%mm0, %%"out_reg1"    \n\t"\
             "pmullw %%mm4, %%"out_reg2"    \n\t"

#define snow_inner_add_yblock_mmx_accum(ptr_offset, s_offset, d_offset) \
             snow_inner_add_yblock_mmx_start("mm2", "mm6", ptr_offset, s_offset, d_offset)\
             "paddusw %%mm2, %%mm1         \n\t"\
             "paddusw %%mm6, %%mm5         \n\t"

#define snow_inner_add_yblock_mmx_mix(read_offset, write_offset)\
             "mov %0, %%"REG_d"              \n\t"\
             "movq %%mm1, %%mm0              \n\t"\
             "movq %%mm5, %%mm4              \n\t"\
             "punpcklwd %%mm7, %%mm0         \n\t"\
             "paddd "read_offset"(%%"REG_D"), %%mm0 \n\t"\
             "punpckhwd %%mm7, %%mm1         \n\t"\
             "paddd "read_offset"+8(%%"REG_D"), %%mm1 \n\t"\
             "punpcklwd %%mm7, %%mm4         \n\t"\
             "paddd "read_offset"+16(%%"REG_D"), %%mm4 \n\t"\
             "punpckhwd %%mm7, %%mm5         \n\t"\
             "paddd "read_offset"+24(%%"REG_D"), %%mm5 \n\t"\
             "paddd %%mm3, %%mm0             \n\t"\
             "paddd %%mm3, %%mm1             \n\t"\
             "paddd %%mm3, %%mm4             \n\t"\
             "paddd %%mm3, %%mm5             \n\t"\
             "psrad $8, %%mm0                \n\t"\
             "psrad $8, %%mm1                \n\t"\
             "psrad $8, %%mm4                \n\t"\
             "psrad $8, %%mm5                \n\t"\
\
             "packssdw %%mm1, %%mm0          \n\t"\
             "packssdw %%mm5, %%mm4          \n\t"\
             "packuswb %%mm4, %%mm0          \n\t"\
             "movq %%mm0, "write_offset"(%%"REG_d") \n\t"

#define snow_inner_add_yblock_mmx_end(s_step)\
             "add $"s_step", %%"REG_S"             \n\t"\
             "add %%"REG_c", "PTR_SIZE"*3(%%"REG_a");\n\t"\
             "add %%"REG_c", "PTR_SIZE"*2(%%"REG_a");\n\t"\
             "add %%"REG_c", "PTR_SIZE"*1(%%"REG_a");\n\t"\
             "add %%"REG_c", (%%"REG_a")     \n\t"\
             "add $"PTR_SIZE"*1, %1          \n\t"\
             "add %%"REG_c", %0              \n\t"\
             "dec %2                         \n\t"\
             "jnz 1b                         \n\t"\
             :"+m"(dst8),"+m"(dst_array),"=&r"(tmp)\
             :\
             "rm"((long)(src_x<<2)),"m"(obmc),"a"(block),"m"((long)b_h),"m"((long)src_stride):\
             "%"REG_c"","%"REG_S"","%"REG_D"","%"REG_d"");

static void inner_add_yblock_bw_8_obmc_16_mmx(const uint8_t *obmc, const long obmc_stride, uint8_t * * block, int b_w, long b_h,
                      int src_x, int src_y, long src_stride, slice_buffer * sb, int add, uint8_t * dst8){
snow_inner_add_yblock_mmx_header
snow_inner_add_yblock_mmx_start("mm1", "mm5", "3", "0", "0")
snow_inner_add_yblock_mmx_accum("2", "8", "0")
snow_inner_add_yblock_mmx_accum("1", "128", "0")
snow_inner_add_yblock_mmx_accum("0", "136", "0")
snow_inner_add_yblock_mmx_mix("0", "0")
snow_inner_add_yblock_mmx_end("16")
}

static void inner_add_yblock_bw_16_obmc_32_mmx(const uint8_t *obmc, const long obmc_stride, uint8_t * * block, int b_w, long b_h,
                      int src_x, int src_y, long src_stride, slice_buffer * sb, int add, uint8_t * dst8){
snow_inner_add_yblock_mmx_header
snow_inner_add_yblock_mmx_start("mm1", "mm5", "3", "0", "0")
snow_inner_add_yblock_mmx_accum("2", "16", "0")
snow_inner_add_yblock_mmx_accum("1", "512", "0")
snow_inner_add_yblock_mmx_accum("0", "528", "0")
snow_inner_add_yblock_mmx_mix("0", "0")

snow_inner_add_yblock_mmx_start("mm1", "mm5", "3", "8", "8")
snow_inner_add_yblock_mmx_accum("2", "24", "8")
snow_inner_add_yblock_mmx_accum("1", "520", "8")
snow_inner_add_yblock_mmx_accum("0", "536", "8")
snow_inner_add_yblock_mmx_mix("32", "8")
snow_inner_add_yblock_mmx_end("32")
}

void ff_snow_inner_add_yblock_sse2(const uint8_t *obmc, const int obmc_stride, uint8_t * * block, int b_w, int b_h,
                           int src_x, int src_y, int src_stride, slice_buffer * sb, int add, uint8_t * dst8){

    if (b_w == 16)
        inner_add_yblock_bw_16_obmc_32_sse2(obmc, obmc_stride, block, b_w, b_h, src_x,src_y, src_stride, sb, add, dst8);
    else if (b_w == 8 && obmc_stride == 16) {
        if (!(b_h & 1))
            inner_add_yblock_bw_8_obmc_16_bh_even_sse2(obmc, obmc_stride, block, b_w, b_h, src_x,src_y, src_stride, sb, add, dst8);
        else
            inner_add_yblock_bw_8_obmc_16_mmx(obmc, obmc_stride, block, b_w, b_h, src_x,src_y, src_stride, sb, add, dst8);
    } else
         ff_snow_inner_add_yblock(obmc, obmc_stride, block, b_w, b_h, src_x,src_y, src_stride, sb, add, dst8);
}

void ff_snow_inner_add_yblock_mmx(const uint8_t *obmc, const int obmc_stride, uint8_t * * block, int b_w, int b_h,
                          int src_x, int src_y, int src_stride, slice_buffer * sb, int add, uint8_t * dst8){
    if (b_w == 16)
        inner_add_yblock_bw_16_obmc_32_mmx(obmc, obmc_stride, block, b_w, b_h, src_x,src_y, src_stride, sb, add, dst8);
    else if (b_w == 8 && obmc_stride == 16)
        inner_add_yblock_bw_8_obmc_16_mmx(obmc, obmc_stride, block, b_w, b_h, src_x,src_y, src_stride, sb, add, dst8);
    else
        ff_snow_inner_add_yblock(obmc, obmc_stride, block, b_w, b_h, src_x,src_y, src_stride, sb, add, dst8);
}
