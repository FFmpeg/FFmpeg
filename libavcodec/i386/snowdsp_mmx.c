/*
 * MMX and SSE2 optimized snow DSP utils
 * Copyright (c) 2005-2006 Robert Edele <yartrebo@earthlink.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "../avcodec.h"
#include "../snow.h"
#include "mmx.h"

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
        "mov %4, %%"REG_b"                           \n\t"

        snow_vertical_compose_sse2_load(REG_b,"xmm0","xmm2","xmm4","xmm6")
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
        snow_vertical_compose_sse2_load(REG_b,"xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_add(REG_c,"xmm1","xmm3","xmm5","xmm7")
        snow_vertical_compose_sse2_sub("xmm1","xmm3","xmm5","xmm7","xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_store(REG_b,"xmm0","xmm2","xmm4","xmm6")
        "mov %2, %%"REG_a"                           \n\t"
        snow_vertical_compose_sse2_load(REG_c,"xmm1","xmm3","xmm5","xmm7")
        snow_vertical_compose_sse2_add(REG_a,"xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_sll("2","xmm1","xmm3","xmm5","xmm7")\
        snow_vertical_compose_sse2_r2r_add("xmm1","xmm3","xmm5","xmm7","xmm0","xmm2","xmm4","xmm6")

        "pcmpeqd %%xmm1, %%xmm1                      \n\t"
        "pslld $31, %%xmm1                           \n\t"
        "psrld $28, %%xmm1                           \n\t"
        "mov %1, %%"REG_b"                           \n\t"

        snow_vertical_compose_sse2_r2r_add("xmm1","xmm1","xmm1","xmm1","xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_sra("4","xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_add(REG_c,"xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_store(REG_c,"xmm0","xmm2","xmm4","xmm6")
        snow_vertical_compose_sse2_add(REG_b,"xmm0","xmm2","xmm4","xmm6")
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
        "%"REG_a"","%"REG_b"","%"REG_c"");
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
        "mov %4, %%"REG_b"                           \n\t"

        snow_vertical_compose_mmx_load(REG_b,"mm0","mm2","mm4","mm6")
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
        snow_vertical_compose_mmx_load(REG_b,"mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_add(REG_c,"mm1","mm3","mm5","mm7")
        snow_vertical_compose_mmx_sub("mm1","mm3","mm5","mm7","mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_store(REG_b,"mm0","mm2","mm4","mm6")
        "mov %2, %%"REG_a"                           \n\t"
        snow_vertical_compose_mmx_load(REG_c,"mm1","mm3","mm5","mm7")
        snow_vertical_compose_mmx_add(REG_a,"mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_sll("2","mm1","mm3","mm5","mm7")
        snow_vertical_compose_mmx_r2r_add("mm1","mm3","mm5","mm7","mm0","mm2","mm4","mm6")

        "pcmpeqd %%mm1, %%mm1                        \n\t"
        "pslld $31, %%mm1                            \n\t"
        "psrld $28, %%mm1                            \n\t"
        "mov %1, %%"REG_b"                           \n\t"

        snow_vertical_compose_mmx_r2r_add("mm1","mm1","mm1","mm1","mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_sra("4","mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_add(REG_c,"mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_store(REG_c,"mm0","mm2","mm4","mm6")
        snow_vertical_compose_mmx_add(REG_b,"mm0","mm2","mm4","mm6")
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
        "%"REG_a"","%"REG_b"","%"REG_c"");
}
