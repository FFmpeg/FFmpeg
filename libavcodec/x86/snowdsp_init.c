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

#include <stdint.h>
#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/snow_dwt.h"

void ff_snow_inner_add_yblock_ssse3(const uint8_t *obmc, const int obmc_stride,
                                    uint8_t **block, int b_w, int b_h, int src_x,
                                    int src_stride, IDWTELEM *const *lines,
                                    int add, uint8_t *dst8);

#if HAVE_INLINE_ASM

static void ff_snow_horizontal_compose97i_sse2(IDWTELEM *b, IDWTELEM *temp, int width){
    const int w2= (width+1)>>1;
    const int w_l= (width>>1);
    const int w_r= w2 - 1;
    int i;

    { // Lift 0
        IDWTELEM * const ref = b + w2 - 1;
        IDWTELEM b_0 = b[0]; //By allowing the first entry in b[0] to be calculated twice
        // (the first time erroneously), we allow the SSE2 code to run an extra pass.
        // The savings in code and time are well worth having to store this value and
        // calculate b[0] correctly afterwards.

        i = 0;
        __asm__ volatile(
            "pcmpeqd   %%xmm7, %%xmm7         \n\t"
            "pcmpeqd   %%xmm3, %%xmm3         \n\t"
            "psllw         $1, %%xmm3         \n\t"
            "paddw     %%xmm7, %%xmm3         \n\t"
            "psllw        $13, %%xmm3         \n\t"
        ::);
        for(; i<w_l-15; i+=16){
            __asm__ volatile(
                "movdqu   (%1), %%xmm1        \n\t"
                "movdqu 16(%1), %%xmm5        \n\t"
                "movdqu  2(%1), %%xmm2        \n\t"
                "movdqu 18(%1), %%xmm6        \n\t"
                "paddw  %%xmm1, %%xmm2        \n\t"
                "paddw  %%xmm5, %%xmm6        \n\t"
                "paddw  %%xmm7, %%xmm2        \n\t"
                "paddw  %%xmm7, %%xmm6        \n\t"
                "pmulhw %%xmm3, %%xmm2        \n\t"
                "pmulhw %%xmm3, %%xmm6        \n\t"
                "paddw    (%0), %%xmm2        \n\t"
                "paddw  16(%0), %%xmm6        \n\t"
                "movdqa %%xmm2, (%0)          \n\t"
                "movdqa %%xmm6, 16(%0)        \n\t"
                :: "r"(&b[i]), "r"(&ref[i])
                : "memory"
            );
        }
        snow_horizontal_compose_lift_lead_out(i, b, b, ref, width, w_l, 0, W_DM, W_DO, W_DS);
        b[0] = b_0 - ((W_DM * 2 * ref[1]+W_DO)>>W_DS);
    }

    { // Lift 1
        IDWTELEM * const dst = b+w2;

        i = 0;
        for(; (((x86_reg)&dst[i]) & 0x1F) && i<w_r; i++){
            dst[i] = dst[i] - (b[i] + b[i + 1]);
        }
        for(; i<w_r-15; i+=16){
            __asm__ volatile(
                "movdqu   (%1), %%xmm1        \n\t"
                "movdqu 16(%1), %%xmm5        \n\t"
                "movdqu  2(%1), %%xmm2        \n\t"
                "movdqu 18(%1), %%xmm6        \n\t"
                "paddw  %%xmm1, %%xmm2        \n\t"
                "paddw  %%xmm5, %%xmm6        \n\t"
                "movdqa   (%0), %%xmm0        \n\t"
                "movdqa 16(%0), %%xmm4        \n\t"
                "psubw  %%xmm2, %%xmm0        \n\t"
                "psubw  %%xmm6, %%xmm4        \n\t"
                "movdqa %%xmm0, (%0)          \n\t"
                "movdqa %%xmm4, 16(%0)        \n\t"
                :: "r"(&dst[i]), "r"(&b[i])
                : "memory"
            );
        }
        snow_horizontal_compose_lift_lead_out(i, dst, dst, b, width, w_r, 1, W_CM, W_CO, W_CS);
    }

    { // Lift 2
        IDWTELEM * const ref = b+w2 - 1;
        IDWTELEM b_0 = b[0];

        i = 0;
        __asm__ volatile(
            "psllw         $15, %%xmm7        \n\t"
            "pcmpeqw    %%xmm6, %%xmm6        \n\t"
            "psrlw         $13, %%xmm6        \n\t"
            "paddw      %%xmm7, %%xmm6        \n\t"
        ::);
        for(; i<w_l-15; i+=16){
            __asm__ volatile(
                "movdqu   (%1), %%xmm0        \n\t"
                "movdqu 16(%1), %%xmm4        \n\t"
                "movdqu  2(%1), %%xmm1        \n\t"
                "movdqu 18(%1), %%xmm5        \n\t" //FIXME try aligned reads and shifts
                "paddw  %%xmm6, %%xmm0        \n\t"
                "paddw  %%xmm6, %%xmm4        \n\t"
                "paddw  %%xmm7, %%xmm1        \n\t"
                "paddw  %%xmm7, %%xmm5        \n\t"
                "pavgw  %%xmm1, %%xmm0        \n\t"
                "pavgw  %%xmm5, %%xmm4        \n\t"
                "psubw  %%xmm7, %%xmm0        \n\t"
                "psubw  %%xmm7, %%xmm4        \n\t"
                "psraw      $1, %%xmm0        \n\t"
                "psraw      $1, %%xmm4        \n\t"
                "movdqa   (%0), %%xmm1        \n\t"
                "movdqa 16(%0), %%xmm5        \n\t"
                "paddw  %%xmm1, %%xmm0        \n\t"
                "paddw  %%xmm5, %%xmm4        \n\t"
                "psraw      $2, %%xmm0        \n\t"
                "psraw      $2, %%xmm4        \n\t"
                "paddw  %%xmm1, %%xmm0        \n\t"
                "paddw  %%xmm5, %%xmm4        \n\t"
                "movdqa %%xmm0, (%0)          \n\t"
                "movdqa %%xmm4, 16(%0)        \n\t"
                :: "r"(&b[i]), "r"(&ref[i])
                : "memory"
            );
        }
        snow_horizontal_compose_liftS_lead_out(i, b, b, ref, width, w_l);
        b[0] = b_0 + ((2 * ref[1] + W_BO-1 + 4 * b_0) >> W_BS);
    }

    { // Lift 3
        IDWTELEM * const src = b+w2;

        i = 0;
        for(; (((x86_reg)&temp[i]) & 0x1F) && i<w_r; i++){
            temp[i] = src[i] - ((-W_AM*(b[i] + b[i+1]))>>W_AS);
        }
        for(; i<w_r-7; i+=8){
            __asm__ volatile(
                "movdqu  2(%1), %%xmm2        \n\t"
                "movdqu 18(%1), %%xmm6        \n\t"
                "paddw    (%1), %%xmm2        \n\t"
                "paddw  16(%1), %%xmm6        \n\t"
                "movdqu   (%0), %%xmm0        \n\t"
                "movdqu 16(%0), %%xmm4        \n\t"
                "paddw  %%xmm2, %%xmm0        \n\t"
                "paddw  %%xmm6, %%xmm4        \n\t"
                "psraw      $1, %%xmm2        \n\t"
                "psraw      $1, %%xmm6        \n\t"
                "paddw  %%xmm0, %%xmm2        \n\t"
                "paddw  %%xmm4, %%xmm6        \n\t"
                "movdqa %%xmm2, (%2)          \n\t"
                "movdqa %%xmm6, 16(%2)        \n\t"
                :: "r"(&src[i]), "r"(&b[i]), "r"(&temp[i])
                 : "memory"
               );
        }
        snow_horizontal_compose_lift_lead_out(i, temp, src, b, width, w_r, 1, -W_AM, W_AO+1, W_AS);
    }

    {
        snow_interleave_line_header(&i, width, b, temp);

        for (; (i & 0x3E) != 0x3E; i-=2){
            b[i+1] = temp[i>>1];
            b[i] = b[i>>1];
        }
        for (i-=62; i>=0; i-=64){
            __asm__ volatile(
                "movdqa      (%1), %%xmm0       \n\t"
                "movdqa    16(%1), %%xmm2       \n\t"
                "movdqa    32(%1), %%xmm4       \n\t"
                "movdqa    48(%1), %%xmm6       \n\t"
                "movdqa      (%1), %%xmm1       \n\t"
                "movdqa    16(%1), %%xmm3       \n\t"
                "movdqa    32(%1), %%xmm5       \n\t"
                "movdqa    48(%1), %%xmm7       \n\t"
                "punpcklwd   (%2), %%xmm0       \n\t"
                "punpcklwd 16(%2), %%xmm2       \n\t"
                "punpcklwd 32(%2), %%xmm4       \n\t"
                "punpcklwd 48(%2), %%xmm6       \n\t"
                "movdqa    %%xmm0, (%0)         \n\t"
                "movdqa    %%xmm2, 32(%0)       \n\t"
                "movdqa    %%xmm4, 64(%0)       \n\t"
                "movdqa    %%xmm6, 96(%0)       \n\t"
                "punpckhwd   (%2), %%xmm1       \n\t"
                "punpckhwd 16(%2), %%xmm3       \n\t"
                "punpckhwd 32(%2), %%xmm5       \n\t"
                "punpckhwd 48(%2), %%xmm7       \n\t"
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

static void ff_snow_horizontal_compose97i_mmx(IDWTELEM *b, IDWTELEM *temp, int width){
    const int w2= (width+1)>>1;
    const int w_l= (width>>1);
    const int w_r= w2 - 1;
    int i;

    { // Lift 0
        IDWTELEM * const ref = b + w2 - 1;

        i = 1;
        b[0] = b[0] - ((W_DM * 2 * ref[1]+W_DO)>>W_DS);
        __asm__ volatile(
            "pcmpeqw    %%mm7, %%mm7         \n\t"
            "pcmpeqw    %%mm3, %%mm3         \n\t"
            "psllw         $1, %%mm3         \n\t"
            "paddw      %%mm7, %%mm3         \n\t"
            "psllw        $13, %%mm3         \n\t"
           ::);
        for(; i<w_l-7; i+=8){
            __asm__ volatile(
                "movq     (%1), %%mm2        \n\t"
                "movq    8(%1), %%mm6        \n\t"
                "paddw   2(%1), %%mm2        \n\t"
                "paddw  10(%1), %%mm6        \n\t"
                "paddw   %%mm7, %%mm2        \n\t"
                "paddw   %%mm7, %%mm6        \n\t"
                "pmulhw  %%mm3, %%mm2        \n\t"
                "pmulhw  %%mm3, %%mm6        \n\t"
                "paddw    (%0), %%mm2        \n\t"
                "paddw   8(%0), %%mm6        \n\t"
                "movq    %%mm2, (%0)         \n\t"
                "movq    %%mm6, 8(%0)        \n\t"
                :: "r"(&b[i]), "r"(&ref[i])
                 : "memory"
               );
        }
        snow_horizontal_compose_lift_lead_out(i, b, b, ref, width, w_l, 0, W_DM, W_DO, W_DS);
    }

    { // Lift 1
        IDWTELEM * const dst = b+w2;

        i = 0;
        for(; i<w_r-7; i+=8){
            __asm__ volatile(
                "movq     (%1), %%mm2        \n\t"
                "movq    8(%1), %%mm6        \n\t"
                "paddw   2(%1), %%mm2        \n\t"
                "paddw  10(%1), %%mm6        \n\t"
                "movq     (%0), %%mm0        \n\t"
                "movq    8(%0), %%mm4        \n\t"
                "psubw   %%mm2, %%mm0        \n\t"
                "psubw   %%mm6, %%mm4        \n\t"
                "movq    %%mm0, (%0)         \n\t"
                "movq    %%mm4, 8(%0)        \n\t"
                :: "r"(&dst[i]), "r"(&b[i])
                 : "memory"
               );
        }
        snow_horizontal_compose_lift_lead_out(i, dst, dst, b, width, w_r, 1, W_CM, W_CO, W_CS);
    }

    { // Lift 2
        IDWTELEM * const ref = b+w2 - 1;

        i = 1;
        b[0] = b[0] + (((2 * ref[1] + W_BO) + 4 * b[0]) >> W_BS);
        __asm__ volatile(
            "psllw         $15, %%mm7        \n\t"
            "pcmpeqw     %%mm6, %%mm6        \n\t"
            "psrlw         $13, %%mm6        \n\t"
            "paddw       %%mm7, %%mm6        \n\t"
           ::);
        for(; i<w_l-7; i+=8){
            __asm__ volatile(
                "movq     (%1), %%mm0        \n\t"
                "movq    8(%1), %%mm4        \n\t"
                "movq    2(%1), %%mm1        \n\t"
                "movq   10(%1), %%mm5        \n\t"
                "paddw   %%mm6, %%mm0        \n\t"
                "paddw   %%mm6, %%mm4        \n\t"
                "paddw   %%mm7, %%mm1        \n\t"
                "paddw   %%mm7, %%mm5        \n\t"
                "pavgw   %%mm1, %%mm0        \n\t"
                "pavgw   %%mm5, %%mm4        \n\t"
                "psubw   %%mm7, %%mm0        \n\t"
                "psubw   %%mm7, %%mm4        \n\t"
                "psraw      $1, %%mm0        \n\t"
                "psraw      $1, %%mm4        \n\t"
                "movq     (%0), %%mm1        \n\t"
                "movq    8(%0), %%mm5        \n\t"
                "paddw   %%mm1, %%mm0        \n\t"
                "paddw   %%mm5, %%mm4        \n\t"
                "psraw      $2, %%mm0        \n\t"
                "psraw      $2, %%mm4        \n\t"
                "paddw   %%mm1, %%mm0        \n\t"
                "paddw   %%mm5, %%mm4        \n\t"
                "movq    %%mm0, (%0)         \n\t"
                "movq    %%mm4, 8(%0)        \n\t"
                :: "r"(&b[i]), "r"(&ref[i])
                 : "memory"
               );
        }
        snow_horizontal_compose_liftS_lead_out(i, b, b, ref, width, w_l);
    }

    { // Lift 3
        IDWTELEM * const src = b+w2;
        i = 0;

        for(; i<w_r-7; i+=8){
            __asm__ volatile(
                "movq    2(%1), %%mm2        \n\t"
                "movq   10(%1), %%mm6        \n\t"
                "paddw    (%1), %%mm2        \n\t"
                "paddw   8(%1), %%mm6        \n\t"
                "movq     (%0), %%mm0        \n\t"
                "movq    8(%0), %%mm4        \n\t"
                "paddw   %%mm2, %%mm0        \n\t"
                "paddw   %%mm6, %%mm4        \n\t"
                "psraw      $1, %%mm2        \n\t"
                "psraw      $1, %%mm6        \n\t"
                "paddw   %%mm0, %%mm2        \n\t"
                "paddw   %%mm4, %%mm6        \n\t"
                "movq    %%mm2, (%2)         \n\t"
                "movq    %%mm6, 8(%2)        \n\t"
                :: "r"(&src[i]), "r"(&b[i]), "r"(&temp[i])
                 : "memory"
               );
        }
        snow_horizontal_compose_lift_lead_out(i, temp, src, b, width, w_r, 1, -W_AM, W_AO+1, W_AS);
    }

    {
        snow_interleave_line_header(&i, width, b, temp);

        for (; (i & 0x1E) != 0x1E; i-=2){
            b[i+1] = temp[i>>1];
            b[i] = b[i>>1];
        }
        for (i-=30; i>=0; i-=32){
            __asm__ volatile(
                "movq        (%1), %%mm0       \n\t"
                "movq       8(%1), %%mm2       \n\t"
                "movq      16(%1), %%mm4       \n\t"
                "movq      24(%1), %%mm6       \n\t"
                "movq        (%1), %%mm1       \n\t"
                "movq       8(%1), %%mm3       \n\t"
                "movq      16(%1), %%mm5       \n\t"
                "movq      24(%1), %%mm7       \n\t"
                "punpcklwd   (%2), %%mm0       \n\t"
                "punpcklwd  8(%2), %%mm2       \n\t"
                "punpcklwd 16(%2), %%mm4       \n\t"
                "punpcklwd 24(%2), %%mm6       \n\t"
                "movq       %%mm0, (%0)        \n\t"
                "movq       %%mm2, 16(%0)      \n\t"
                "movq       %%mm4, 32(%0)      \n\t"
                "movq       %%mm6, 48(%0)      \n\t"
                "punpckhwd   (%2), %%mm1       \n\t"
                "punpckhwd  8(%2), %%mm3       \n\t"
                "punpckhwd 16(%2), %%mm5       \n\t"
                "punpckhwd 24(%2), %%mm7       \n\t"
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

#if HAVE_7REGS
#define snow_vertical_compose_sse2_load_add(op,r,t0,t1,t2,t3)\
        ""op" ("r",%%"FF_REG_d"), %%"t0"      \n\t"\
        ""op" 16("r",%%"FF_REG_d"), %%"t1"    \n\t"\
        ""op" 32("r",%%"FF_REG_d"), %%"t2"    \n\t"\
        ""op" 48("r",%%"FF_REG_d"), %%"t3"    \n\t"

#define snow_vertical_compose_sse2_load(r,t0,t1,t2,t3)\
        snow_vertical_compose_sse2_load_add("movdqa",r,t0,t1,t2,t3)

#define snow_vertical_compose_sse2_add(r,t0,t1,t2,t3)\
        snow_vertical_compose_sse2_load_add("paddw",r,t0,t1,t2,t3)

#define snow_vertical_compose_r2r_sub(s0,s1,s2,s3,t0,t1,t2,t3)\
        "psubw %%"s0", %%"t0" \n\t"\
        "psubw %%"s1", %%"t1" \n\t"\
        "psubw %%"s2", %%"t2" \n\t"\
        "psubw %%"s3", %%"t3" \n\t"

#define snow_vertical_compose_sse2_store(w,s0,s1,s2,s3)\
        "movdqa %%"s0", ("w",%%"FF_REG_d")    \n\t"\
        "movdqa %%"s1", 16("w",%%"FF_REG_d")  \n\t"\
        "movdqa %%"s2", 32("w",%%"FF_REG_d")  \n\t"\
        "movdqa %%"s3", 48("w",%%"FF_REG_d")  \n\t"

#define snow_vertical_compose_sra(n,t0,t1,t2,t3)\
        "psraw $"n", %%"t0" \n\t"\
        "psraw $"n", %%"t1" \n\t"\
        "psraw $"n", %%"t2" \n\t"\
        "psraw $"n", %%"t3" \n\t"

#define snow_vertical_compose_r2r_add(s0,s1,s2,s3,t0,t1,t2,t3)\
        "paddw %%"s0", %%"t0" \n\t"\
        "paddw %%"s1", %%"t1" \n\t"\
        "paddw %%"s2", %%"t2" \n\t"\
        "paddw %%"s3", %%"t3" \n\t"

#define snow_vertical_compose_r2r_pmulhw(s0,s1,s2,s3,t0,t1,t2,t3)\
        "pmulhw %%"s0", %%"t0" \n\t"\
        "pmulhw %%"s1", %%"t1" \n\t"\
        "pmulhw %%"s2", %%"t2" \n\t"\
        "pmulhw %%"s3", %%"t3" \n\t"

#define snow_vertical_compose_sse2_move(s0,s1,s2,s3,t0,t1,t2,t3)\
        "movdqa %%"s0", %%"t0" \n\t"\
        "movdqa %%"s1", %%"t1" \n\t"\
        "movdqa %%"s2", %%"t2" \n\t"\
        "movdqa %%"s3", %%"t3" \n\t"

static void ff_snow_vertical_compose97i_sse2(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, IDWTELEM *b3, IDWTELEM *b4, IDWTELEM *b5, int width){
    x86_reg i = width;

    while(i & 0x1F)
    {
        i--;
        b4[i] -= (W_DM*(b3[i] + b5[i])+W_DO)>>W_DS;
        b3[i] -= (W_CM*(b2[i] + b4[i])+W_CO)>>W_CS;
        b2[i] += (W_BM*(b1[i] + b3[i])+4*b2[i]+W_BO)>>W_BS;
        b1[i] += (W_AM*(b0[i] + b2[i])+W_AO)>>W_AS;
    }
    i+=i;

         __asm__ volatile (
        "jmp 2f                                      \n\t"
        "1:                                          \n\t"
        snow_vertical_compose_sse2_load("%4","xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_add("%6","xmm0","xmm2","xmm4","xmm6")


        "pcmpeqw    %%xmm0, %%xmm0                   \n\t"
        "pcmpeqw    %%xmm2, %%xmm2                   \n\t"
        "paddw      %%xmm2, %%xmm2                   \n\t"
        "paddw      %%xmm0, %%xmm2                   \n\t"
        "psllw         $13, %%xmm2                   \n\t"
        snow_vertical_compose_r2r_add("xmm0","xmm0","xmm0","xmm0","xmm1","xmm3","xmm5","xmm7")
        snow_vertical_compose_r2r_pmulhw("xmm2","xmm2","xmm2","xmm2","xmm1","xmm3","xmm5","xmm7")
        snow_vertical_compose_sse2_add("%5","xmm1","xmm3","xmm5","xmm7")
        snow_vertical_compose_sse2_store("%5","xmm1","xmm3","xmm5","xmm7")
        snow_vertical_compose_sse2_load("%4","xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_add("%3","xmm1","xmm3","xmm5","xmm7")
        snow_vertical_compose_r2r_sub("xmm1","xmm3","xmm5","xmm7","xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_store("%4","xmm0","xmm2","xmm4","xmm6")

        "pcmpeqw %%xmm7, %%xmm7                      \n\t"
        "pcmpeqw %%xmm5, %%xmm5                      \n\t"
        "psllw $15, %%xmm7                           \n\t"
        "psrlw $13, %%xmm5                           \n\t"
        "paddw %%xmm7, %%xmm5                        \n\t"
        snow_vertical_compose_r2r_add("xmm5","xmm5","xmm5","xmm5","xmm0","xmm2","xmm4","xmm6")
        "movq   (%2,%%"FF_REG_d"), %%xmm1            \n\t"
        "movq  8(%2,%%"FF_REG_d"), %%xmm3            \n\t"
        "paddw %%xmm7, %%xmm1                        \n\t"
        "paddw %%xmm7, %%xmm3                        \n\t"
        "pavgw %%xmm1, %%xmm0                        \n\t"
        "pavgw %%xmm3, %%xmm2                        \n\t"
        "movq 16(%2,%%"FF_REG_d"), %%xmm1            \n\t"
        "movq 24(%2,%%"FF_REG_d"), %%xmm3            \n\t"
        "paddw %%xmm7, %%xmm1                        \n\t"
        "paddw %%xmm7, %%xmm3                        \n\t"
        "pavgw %%xmm1, %%xmm4                        \n\t"
        "pavgw %%xmm3, %%xmm6                        \n\t"
        snow_vertical_compose_r2r_sub("xmm7","xmm7","xmm7","xmm7","xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sra("1","xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_add("%3","xmm0","xmm2","xmm4","xmm6")

        snow_vertical_compose_sra("2","xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_add("%3","xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_store("%3","xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_add("%1","xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_move("xmm0","xmm2","xmm4","xmm6","xmm1","xmm3","xmm5","xmm7")
        snow_vertical_compose_sra("1","xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_r2r_add("xmm1","xmm3","xmm5","xmm7","xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_add("%2","xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_store("%2","xmm0","xmm2","xmm4","xmm6")

        "2:                                          \n\t"
        "sub $64, %%"FF_REG_d"                       \n\t"
        "jge 1b                                      \n\t"
        :"+d"(i)
        :"r"(b0),"r"(b1),"r"(b2),"r"(b3),"r"(b4),"r"(b5));
}

#define snow_vertical_compose_mmx_load_add(op,r,t0,t1,t2,t3)\
        ""op" ("r",%%"FF_REG_d"), %%"t0"   \n\t"\
        ""op" 8("r",%%"FF_REG_d"), %%"t1"  \n\t"\
        ""op" 16("r",%%"FF_REG_d"), %%"t2" \n\t"\
        ""op" 24("r",%%"FF_REG_d"), %%"t3" \n\t"

#define snow_vertical_compose_mmx_load(r,t0,t1,t2,t3)\
        snow_vertical_compose_mmx_load_add("movq",r,t0,t1,t2,t3)

#define snow_vertical_compose_mmx_add(r,t0,t1,t2,t3)\
        snow_vertical_compose_mmx_load_add("paddw",r,t0,t1,t2,t3)

#define snow_vertical_compose_mmx_store(w,s0,s1,s2,s3)\
        "movq %%"s0", ("w",%%"FF_REG_d")   \n\t"\
        "movq %%"s1", 8("w",%%"FF_REG_d")  \n\t"\
        "movq %%"s2", 16("w",%%"FF_REG_d") \n\t"\
        "movq %%"s3", 24("w",%%"FF_REG_d") \n\t"

#define snow_vertical_compose_mmx_move(s0,s1,s2,s3,t0,t1,t2,t3)\
        "movq %%"s0", %%"t0" \n\t"\
        "movq %%"s1", %%"t1" \n\t"\
        "movq %%"s2", %%"t2" \n\t"\
        "movq %%"s3", %%"t3" \n\t"


static void ff_snow_vertical_compose97i_mmx(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, IDWTELEM *b3, IDWTELEM *b4, IDWTELEM *b5, int width){
    x86_reg i = width;
    while(i & 15)
    {
        i--;
        b4[i] -= (W_DM*(b3[i] + b5[i])+W_DO)>>W_DS;
        b3[i] -= (W_CM*(b2[i] + b4[i])+W_CO)>>W_CS;
        b2[i] += (W_BM*(b1[i] + b3[i])+4*b2[i]+W_BO)>>W_BS;
        b1[i] += (W_AM*(b0[i] + b2[i])+W_AO)>>W_AS;
    }
    i+=i;
    __asm__ volatile(
        "jmp 2f                                      \n\t"
        "1:                                          \n\t"

        snow_vertical_compose_mmx_load("%4","mm1","mm3","mm5","mm7")
        snow_vertical_compose_mmx_add("%6","mm1","mm3","mm5","mm7")
        "pcmpeqw    %%mm0, %%mm0                     \n\t"
        "pcmpeqw    %%mm2, %%mm2                     \n\t"
        "paddw      %%mm2, %%mm2                     \n\t"
        "paddw      %%mm0, %%mm2                     \n\t"
        "psllw        $13, %%mm2                     \n\t"
        snow_vertical_compose_r2r_add("mm0","mm0","mm0","mm0","mm1","mm3","mm5","mm7")
        snow_vertical_compose_r2r_pmulhw("mm2","mm2","mm2","mm2","mm1","mm3","mm5","mm7")
        snow_vertical_compose_mmx_add("%5","mm1","mm3","mm5","mm7")
        snow_vertical_compose_mmx_store("%5","mm1","mm3","mm5","mm7")
        snow_vertical_compose_mmx_load("%4","mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_add("%3","mm1","mm3","mm5","mm7")
        snow_vertical_compose_r2r_sub("mm1","mm3","mm5","mm7","mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_store("%4","mm0","mm2","mm4","mm6")
        "pcmpeqw %%mm7, %%mm7                        \n\t"
        "pcmpeqw %%mm5, %%mm5                        \n\t"
        "psllw $15, %%mm7                            \n\t"
        "psrlw $13, %%mm5                            \n\t"
        "paddw %%mm7, %%mm5                          \n\t"
        snow_vertical_compose_r2r_add("mm5","mm5","mm5","mm5","mm0","mm2","mm4","mm6")
        "movq   (%2,%%"FF_REG_d"), %%mm1             \n\t"
        "movq  8(%2,%%"FF_REG_d"), %%mm3             \n\t"
        "paddw %%mm7, %%mm1                          \n\t"
        "paddw %%mm7, %%mm3                          \n\t"
        "pavgw %%mm1, %%mm0                          \n\t"
        "pavgw %%mm3, %%mm2                          \n\t"
        "movq 16(%2,%%"FF_REG_d"), %%mm1             \n\t"
        "movq 24(%2,%%"FF_REG_d"), %%mm3             \n\t"
        "paddw %%mm7, %%mm1                          \n\t"
        "paddw %%mm7, %%mm3                          \n\t"
        "pavgw %%mm1, %%mm4                          \n\t"
        "pavgw %%mm3, %%mm6                          \n\t"
        snow_vertical_compose_r2r_sub("mm7","mm7","mm7","mm7","mm0","mm2","mm4","mm6")
        snow_vertical_compose_sra("1","mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_add("%3","mm0","mm2","mm4","mm6")

        snow_vertical_compose_sra("2","mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_add("%3","mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_store("%3","mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_add("%1","mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_move("mm0","mm2","mm4","mm6","mm1","mm3","mm5","mm7")
        snow_vertical_compose_sra("1","mm0","mm2","mm4","mm6")
        snow_vertical_compose_r2r_add("mm1","mm3","mm5","mm7","mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_add("%2","mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_store("%2","mm0","mm2","mm4","mm6")

        "2:                                          \n\t"
        "sub $32, %%"FF_REG_d"                       \n\t"
        "jge 1b                                      \n\t"
        :"+d"(i)
        :"r"(b0),"r"(b1),"r"(b2),"r"(b3),"r"(b4),"r"(b5));
}
#endif //HAVE_7REGS

#endif /* HAVE_INLINE_ASM */

av_cold void ff_dwt_init_x86(SnowDWTContext *c)
{
    int cpuflags = av_get_cpu_flags();

#if HAVE_INLINE_ASM
    if (INLINE_MMXEXT(cpuflags)) {
        c->horizontal_compose97i = ff_snow_horizontal_compose97i_mmx;
#if HAVE_7REGS
        c->vertical_compose97i   = ff_snow_vertical_compose97i_mmx;
#endif
    }
    if (INLINE_SSE2(cpuflags) && 0) {
        c->horizontal_compose97i = ff_snow_horizontal_compose97i_sse2;
#if HAVE_7REGS
        c->vertical_compose97i   = ff_snow_vertical_compose97i_sse2;
#endif
    }
#endif /* HAVE_INLINE_ASM */
#if HAVE_SSSE3_EXTERNAL
    if (EXTERNAL_SSSE3(cpuflags)) {
        c->inner_add_yblock = ff_snow_inner_add_yblock_ssse3;
    }
#endif
}
