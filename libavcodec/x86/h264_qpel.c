/*
 * Copyright (c) 2004-2005 Michael Niedermayer, Loren Merritt
 * Copyright (c) 2011 Daniel Kang
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

#include "dsputil_mmx.h"

#if HAVE_INLINE_ASM

/***********************************/
/* motion compensation */

#define QPEL_H264V_MM(A,B,C,D,E,F,OP,T,Z,d,q)\
        "mov"#q" "#C", "#T"         \n\t"\
        "mov"#d" (%0), "#F"         \n\t"\
        "paddw "#D", "#T"           \n\t"\
        "psllw $2, "#T"             \n\t"\
        "psubw "#B", "#T"           \n\t"\
        "psubw "#E", "#T"           \n\t"\
        "punpcklbw "#Z", "#F"       \n\t"\
        "pmullw "MANGLE(ff_pw_5)", "#T"\n\t"\
        "paddw "MANGLE(ff_pw_16)", "#A"\n\t"\
        "add %2, %0                 \n\t"\
        "paddw "#F", "#A"           \n\t"\
        "paddw "#A", "#T"           \n\t"\
        "psraw $5, "#T"             \n\t"\
        "packuswb "#T", "#T"        \n\t"\
        OP(T, (%1), A, d)\
        "add %3, %1                 \n\t"

#define QPEL_H264HV_MM(A,B,C,D,E,F,OF,T,Z,d,q)\
        "mov"#q" "#C", "#T"         \n\t"\
        "mov"#d" (%0), "#F"         \n\t"\
        "paddw "#D", "#T"           \n\t"\
        "psllw $2, "#T"             \n\t"\
        "paddw "MANGLE(ff_pw_16)", "#A"\n\t"\
        "psubw "#B", "#T"           \n\t"\
        "psubw "#E", "#T"           \n\t"\
        "punpcklbw "#Z", "#F"       \n\t"\
        "pmullw "MANGLE(ff_pw_5)", "#T"\n\t"\
        "paddw "#F", "#A"           \n\t"\
        "add %2, %0                 \n\t"\
        "paddw "#A", "#T"           \n\t"\
        "mov"#q" "#T", "#OF"(%1)    \n\t"

#define QPEL_H264V(A,B,C,D,E,F,OP) QPEL_H264V_MM(A,B,C,D,E,F,OP,%%mm6,%%mm7,d,q)
#define QPEL_H264HV(A,B,C,D,E,F,OF) QPEL_H264HV_MM(A,B,C,D,E,F,OF,%%mm6,%%mm7,d,q)
#define QPEL_H264V_XMM(A,B,C,D,E,F,OP) QPEL_H264V_MM(A,B,C,D,E,F,OP,%%xmm6,%%xmm7,q,dqa)
#define QPEL_H264HV_XMM(A,B,C,D,E,F,OF) QPEL_H264HV_MM(A,B,C,D,E,F,OF,%%xmm6,%%xmm7,q,dqa)


#define QPEL_H264(OPNAME, OP, MMX)\
static av_noinline void OPNAME ## h264_qpel4_h_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    int h=4;\
\
    __asm__ volatile(\
        "pxor %%mm7, %%mm7          \n\t"\
        "movq "MANGLE(ff_pw_5) ", %%mm4\n\t"\
        "movq "MANGLE(ff_pw_16)", %%mm5\n\t"\
        "1:                         \n\t"\
        "movd  -1(%0), %%mm1        \n\t"\
        "movd    (%0), %%mm2        \n\t"\
        "movd   1(%0), %%mm3        \n\t"\
        "movd   2(%0), %%mm0        \n\t"\
        "punpcklbw %%mm7, %%mm1     \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpcklbw %%mm7, %%mm3     \n\t"\
        "punpcklbw %%mm7, %%mm0     \n\t"\
        "paddw %%mm0, %%mm1         \n\t"\
        "paddw %%mm3, %%mm2         \n\t"\
        "movd  -2(%0), %%mm0        \n\t"\
        "movd   3(%0), %%mm3        \n\t"\
        "punpcklbw %%mm7, %%mm0     \n\t"\
        "punpcklbw %%mm7, %%mm3     \n\t"\
        "paddw %%mm3, %%mm0         \n\t"\
        "psllw $2, %%mm2            \n\t"\
        "psubw %%mm1, %%mm2         \n\t"\
        "pmullw %%mm4, %%mm2        \n\t"\
        "paddw %%mm5, %%mm0         \n\t"\
        "paddw %%mm2, %%mm0         \n\t"\
        "psraw $5, %%mm0            \n\t"\
        "packuswb %%mm0, %%mm0      \n\t"\
        OP(%%mm0, (%1),%%mm6, d)\
        "add %3, %0                 \n\t"\
        "add %4, %1                 \n\t"\
        "decl %2                    \n\t"\
        " jnz 1b                    \n\t"\
        : "+a"(src), "+c"(dst), "+g"(h)\
        : "d"((x86_reg)srcStride), "S"((x86_reg)dstStride)\
        : "memory"\
    );\
}\
static av_noinline void OPNAME ## h264_qpel4_h_lowpass_l2_ ## MMX(uint8_t *dst, uint8_t *src, uint8_t *src2, int dstStride, int src2Stride){\
    int h=4;\
    __asm__ volatile(\
        "pxor %%mm7, %%mm7          \n\t"\
        "movq %0, %%mm4             \n\t"\
        "movq %1, %%mm5             \n\t"\
        :: "m"(ff_pw_5), "m"(ff_pw_16)\
    );\
    do{\
    __asm__ volatile(\
        "movd  -1(%0), %%mm1        \n\t"\
        "movd    (%0), %%mm2        \n\t"\
        "movd   1(%0), %%mm3        \n\t"\
        "movd   2(%0), %%mm0        \n\t"\
        "punpcklbw %%mm7, %%mm1     \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpcklbw %%mm7, %%mm3     \n\t"\
        "punpcklbw %%mm7, %%mm0     \n\t"\
        "paddw %%mm0, %%mm1         \n\t"\
        "paddw %%mm3, %%mm2         \n\t"\
        "movd  -2(%0), %%mm0        \n\t"\
        "movd   3(%0), %%mm3        \n\t"\
        "punpcklbw %%mm7, %%mm0     \n\t"\
        "punpcklbw %%mm7, %%mm3     \n\t"\
        "paddw %%mm3, %%mm0         \n\t"\
        "psllw $2, %%mm2            \n\t"\
        "psubw %%mm1, %%mm2         \n\t"\
        "pmullw %%mm4, %%mm2        \n\t"\
        "paddw %%mm5, %%mm0         \n\t"\
        "paddw %%mm2, %%mm0         \n\t"\
        "movd   (%2), %%mm3         \n\t"\
        "psraw $5, %%mm0            \n\t"\
        "packuswb %%mm0, %%mm0      \n\t"\
        PAVGB" %%mm3, %%mm0         \n\t"\
        OP(%%mm0, (%1),%%mm6, d)\
        "add %4, %0                 \n\t"\
        "add %4, %1                 \n\t"\
        "add %3, %2                 \n\t"\
        : "+a"(src), "+c"(dst), "+d"(src2)\
        : "D"((x86_reg)src2Stride), "S"((x86_reg)dstStride)\
        : "memory"\
    );\
    }while(--h);\
}\
static av_noinline void OPNAME ## h264_qpel4_v_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    src -= 2*srcStride;\
    __asm__ volatile(\
        "pxor %%mm7, %%mm7          \n\t"\
        "movd (%0), %%mm0           \n\t"\
        "add %2, %0                 \n\t"\
        "movd (%0), %%mm1           \n\t"\
        "add %2, %0                 \n\t"\
        "movd (%0), %%mm2           \n\t"\
        "add %2, %0                 \n\t"\
        "movd (%0), %%mm3           \n\t"\
        "add %2, %0                 \n\t"\
        "movd (%0), %%mm4           \n\t"\
        "add %2, %0                 \n\t"\
        "punpcklbw %%mm7, %%mm0     \n\t"\
        "punpcklbw %%mm7, %%mm1     \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpcklbw %%mm7, %%mm3     \n\t"\
        "punpcklbw %%mm7, %%mm4     \n\t"\
        QPEL_H264V(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, OP)\
        QPEL_H264V(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, OP)\
        QPEL_H264V(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, OP)\
        QPEL_H264V(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, OP)\
         \
        : "+a"(src), "+c"(dst)\
        : "S"((x86_reg)srcStride), "D"((x86_reg)dstStride), "m"(ff_pw_5), "m"(ff_pw_16)\
        : "memory"\
    );\
}\
static av_noinline void OPNAME ## h264_qpel4_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, uint8_t *src, int dstStride, int tmpStride, int srcStride){\
    int h=4;\
    int w=3;\
    src -= 2*srcStride+2;\
    while(w--){\
        __asm__ volatile(\
            "pxor %%mm7, %%mm7      \n\t"\
            "movd (%0), %%mm0       \n\t"\
            "add %2, %0             \n\t"\
            "movd (%0), %%mm1       \n\t"\
            "add %2, %0             \n\t"\
            "movd (%0), %%mm2       \n\t"\
            "add %2, %0             \n\t"\
            "movd (%0), %%mm3       \n\t"\
            "add %2, %0             \n\t"\
            "movd (%0), %%mm4       \n\t"\
            "add %2, %0             \n\t"\
            "punpcklbw %%mm7, %%mm0 \n\t"\
            "punpcklbw %%mm7, %%mm1 \n\t"\
            "punpcklbw %%mm7, %%mm2 \n\t"\
            "punpcklbw %%mm7, %%mm3 \n\t"\
            "punpcklbw %%mm7, %%mm4 \n\t"\
            QPEL_H264HV(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, 0*8*3)\
            QPEL_H264HV(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, 1*8*3)\
            QPEL_H264HV(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, 2*8*3)\
            QPEL_H264HV(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, 3*8*3)\
             \
            : "+a"(src)\
            : "c"(tmp), "S"((x86_reg)srcStride)\
            : "memory"\
        );\
        tmp += 4;\
        src += 4 - 9*srcStride;\
    }\
    tmp -= 3*4;\
    __asm__ volatile(\
        "1:                         \n\t"\
        "movq     (%0), %%mm0       \n\t"\
        "paddw  10(%0), %%mm0       \n\t"\
        "movq    2(%0), %%mm1       \n\t"\
        "paddw   8(%0), %%mm1       \n\t"\
        "movq    4(%0), %%mm2       \n\t"\
        "paddw   6(%0), %%mm2       \n\t"\
        "psubw %%mm1, %%mm0         \n\t"/*a-b   (abccba)*/\
        "psraw $2, %%mm0            \n\t"/*(a-b)/4 */\
        "psubw %%mm1, %%mm0         \n\t"/*(a-b)/4-b */\
        "paddsw %%mm2, %%mm0        \n\t"\
        "psraw $2, %%mm0            \n\t"/*((a-b)/4-b+c)/4 */\
        "paddw %%mm2, %%mm0         \n\t"/*(a-5*b+20*c)/16 */\
        "psraw $6, %%mm0            \n\t"\
        "packuswb %%mm0, %%mm0      \n\t"\
        OP(%%mm0, (%1),%%mm7, d)\
        "add $24, %0                \n\t"\
        "add %3, %1                 \n\t"\
        "decl %2                    \n\t"\
        " jnz 1b                    \n\t"\
        : "+a"(tmp), "+c"(dst), "+g"(h)\
        : "S"((x86_reg)dstStride)\
        : "memory"\
    );\
}\
\
static av_noinline void OPNAME ## h264_qpel8_h_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    int h=8;\
    __asm__ volatile(\
        "pxor %%mm7, %%mm7          \n\t"\
        "movq "MANGLE(ff_pw_5)", %%mm6\n\t"\
        "1:                         \n\t"\
        "movq    (%0), %%mm0        \n\t"\
        "movq   1(%0), %%mm2        \n\t"\
        "movq %%mm0, %%mm1          \n\t"\
        "movq %%mm2, %%mm3          \n\t"\
        "punpcklbw %%mm7, %%mm0     \n\t"\
        "punpckhbw %%mm7, %%mm1     \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpckhbw %%mm7, %%mm3     \n\t"\
        "paddw %%mm2, %%mm0         \n\t"\
        "paddw %%mm3, %%mm1         \n\t"\
        "psllw $2, %%mm0            \n\t"\
        "psllw $2, %%mm1            \n\t"\
        "movq   -1(%0), %%mm2       \n\t"\
        "movq    2(%0), %%mm4       \n\t"\
        "movq %%mm2, %%mm3          \n\t"\
        "movq %%mm4, %%mm5          \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpckhbw %%mm7, %%mm3     \n\t"\
        "punpcklbw %%mm7, %%mm4     \n\t"\
        "punpckhbw %%mm7, %%mm5     \n\t"\
        "paddw %%mm4, %%mm2         \n\t"\
        "paddw %%mm3, %%mm5         \n\t"\
        "psubw %%mm2, %%mm0         \n\t"\
        "psubw %%mm5, %%mm1         \n\t"\
        "pmullw %%mm6, %%mm0        \n\t"\
        "pmullw %%mm6, %%mm1        \n\t"\
        "movd   -2(%0), %%mm2       \n\t"\
        "movd    7(%0), %%mm5       \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpcklbw %%mm7, %%mm5     \n\t"\
        "paddw %%mm3, %%mm2         \n\t"\
        "paddw %%mm5, %%mm4         \n\t"\
        "movq "MANGLE(ff_pw_16)", %%mm5\n\t"\
        "paddw %%mm5, %%mm2         \n\t"\
        "paddw %%mm5, %%mm4         \n\t"\
        "paddw %%mm2, %%mm0         \n\t"\
        "paddw %%mm4, %%mm1         \n\t"\
        "psraw $5, %%mm0            \n\t"\
        "psraw $5, %%mm1            \n\t"\
        "packuswb %%mm1, %%mm0      \n\t"\
        OP(%%mm0, (%1),%%mm5, q)\
        "add %3, %0                 \n\t"\
        "add %4, %1                 \n\t"\
        "decl %2                    \n\t"\
        " jnz 1b                    \n\t"\
        : "+a"(src), "+c"(dst), "+g"(h)\
        : "d"((x86_reg)srcStride), "S"((x86_reg)dstStride)\
        : "memory"\
    );\
}\
\
static av_noinline void OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(uint8_t *dst, uint8_t *src, uint8_t *src2, int dstStride, int src2Stride){\
    int h=8;\
    __asm__ volatile(\
        "pxor %%mm7, %%mm7          \n\t"\
        "movq "MANGLE(ff_pw_5)", %%mm6\n\t"\
        "1:                         \n\t"\
        "movq    (%0), %%mm0        \n\t"\
        "movq   1(%0), %%mm2        \n\t"\
        "movq %%mm0, %%mm1          \n\t"\
        "movq %%mm2, %%mm3          \n\t"\
        "punpcklbw %%mm7, %%mm0     \n\t"\
        "punpckhbw %%mm7, %%mm1     \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpckhbw %%mm7, %%mm3     \n\t"\
        "paddw %%mm2, %%mm0         \n\t"\
        "paddw %%mm3, %%mm1         \n\t"\
        "psllw $2, %%mm0            \n\t"\
        "psllw $2, %%mm1            \n\t"\
        "movq   -1(%0), %%mm2       \n\t"\
        "movq    2(%0), %%mm4       \n\t"\
        "movq %%mm2, %%mm3          \n\t"\
        "movq %%mm4, %%mm5          \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpckhbw %%mm7, %%mm3     \n\t"\
        "punpcklbw %%mm7, %%mm4     \n\t"\
        "punpckhbw %%mm7, %%mm5     \n\t"\
        "paddw %%mm4, %%mm2         \n\t"\
        "paddw %%mm3, %%mm5         \n\t"\
        "psubw %%mm2, %%mm0         \n\t"\
        "psubw %%mm5, %%mm1         \n\t"\
        "pmullw %%mm6, %%mm0        \n\t"\
        "pmullw %%mm6, %%mm1        \n\t"\
        "movd   -2(%0), %%mm2       \n\t"\
        "movd    7(%0), %%mm5       \n\t"\
        "punpcklbw %%mm7, %%mm2     \n\t"\
        "punpcklbw %%mm7, %%mm5     \n\t"\
        "paddw %%mm3, %%mm2         \n\t"\
        "paddw %%mm5, %%mm4         \n\t"\
        "movq "MANGLE(ff_pw_16)", %%mm5\n\t"\
        "paddw %%mm5, %%mm2         \n\t"\
        "paddw %%mm5, %%mm4         \n\t"\
        "paddw %%mm2, %%mm0         \n\t"\
        "paddw %%mm4, %%mm1         \n\t"\
        "psraw $5, %%mm0            \n\t"\
        "psraw $5, %%mm1            \n\t"\
        "movq (%2), %%mm4           \n\t"\
        "packuswb %%mm1, %%mm0      \n\t"\
        PAVGB" %%mm4, %%mm0         \n\t"\
        OP(%%mm0, (%1),%%mm5, q)\
        "add %5, %0                 \n\t"\
        "add %5, %1                 \n\t"\
        "add %4, %2                 \n\t"\
        "decl %3                    \n\t"\
        "jg 1b                      \n\t"\
        : "+a"(src), "+c"(dst), "+d"(src2), "+g"(h)\
        : "D"((x86_reg)src2Stride), "S"((x86_reg)dstStride)\
        : "memory"\
    );\
}\
\
static av_noinline void OPNAME ## h264_qpel8or16_v_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride, int h){\
    int w= 2;\
    src -= 2*srcStride;\
    \
    while(w--){\
        __asm__ volatile(\
            "pxor %%mm7, %%mm7          \n\t"\
            "movd (%0), %%mm0           \n\t"\
            "add %2, %0                 \n\t"\
            "movd (%0), %%mm1           \n\t"\
            "add %2, %0                 \n\t"\
            "movd (%0), %%mm2           \n\t"\
            "add %2, %0                 \n\t"\
            "movd (%0), %%mm3           \n\t"\
            "add %2, %0                 \n\t"\
            "movd (%0), %%mm4           \n\t"\
            "add %2, %0                 \n\t"\
            "punpcklbw %%mm7, %%mm0     \n\t"\
            "punpcklbw %%mm7, %%mm1     \n\t"\
            "punpcklbw %%mm7, %%mm2     \n\t"\
            "punpcklbw %%mm7, %%mm3     \n\t"\
            "punpcklbw %%mm7, %%mm4     \n\t"\
            QPEL_H264V(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, OP)\
            QPEL_H264V(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, OP)\
            QPEL_H264V(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, OP)\
            QPEL_H264V(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, OP)\
            QPEL_H264V(%%mm4, %%mm5, %%mm0, %%mm1, %%mm2, %%mm3, OP)\
            QPEL_H264V(%%mm5, %%mm0, %%mm1, %%mm2, %%mm3, %%mm4, OP)\
            QPEL_H264V(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, OP)\
            QPEL_H264V(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, OP)\
            "cmpl $16, %4               \n\t"\
            "jne 2f                     \n\t"\
            QPEL_H264V(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, OP)\
            QPEL_H264V(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, OP)\
            QPEL_H264V(%%mm4, %%mm5, %%mm0, %%mm1, %%mm2, %%mm3, OP)\
            QPEL_H264V(%%mm5, %%mm0, %%mm1, %%mm2, %%mm3, %%mm4, OP)\
            QPEL_H264V(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, OP)\
            QPEL_H264V(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, OP)\
            QPEL_H264V(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, OP)\
            QPEL_H264V(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, OP)\
            "2:                         \n\t"\
            \
            : "+a"(src), "+c"(dst)\
            : "S"((x86_reg)srcStride), "D"((x86_reg)dstStride), "rm"(h)\
            : "memory"\
        );\
        src += 4-(h+5)*srcStride;\
        dst += 4-h*dstStride;\
    }\
}\
static av_always_inline void OPNAME ## h264_qpel8or16_hv1_lowpass_ ## MMX(int16_t *tmp, uint8_t *src, int tmpStride, int srcStride, int size){\
    int w = (size+8)>>2;\
    src -= 2*srcStride+2;\
    while(w--){\
        __asm__ volatile(\
            "pxor %%mm7, %%mm7      \n\t"\
            "movd (%0), %%mm0       \n\t"\
            "add %2, %0             \n\t"\
            "movd (%0), %%mm1       \n\t"\
            "add %2, %0             \n\t"\
            "movd (%0), %%mm2       \n\t"\
            "add %2, %0             \n\t"\
            "movd (%0), %%mm3       \n\t"\
            "add %2, %0             \n\t"\
            "movd (%0), %%mm4       \n\t"\
            "add %2, %0             \n\t"\
            "punpcklbw %%mm7, %%mm0 \n\t"\
            "punpcklbw %%mm7, %%mm1 \n\t"\
            "punpcklbw %%mm7, %%mm2 \n\t"\
            "punpcklbw %%mm7, %%mm3 \n\t"\
            "punpcklbw %%mm7, %%mm4 \n\t"\
            QPEL_H264HV(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, 0*48)\
            QPEL_H264HV(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, 1*48)\
            QPEL_H264HV(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, 2*48)\
            QPEL_H264HV(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, 3*48)\
            QPEL_H264HV(%%mm4, %%mm5, %%mm0, %%mm1, %%mm2, %%mm3, 4*48)\
            QPEL_H264HV(%%mm5, %%mm0, %%mm1, %%mm2, %%mm3, %%mm4, 5*48)\
            QPEL_H264HV(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, 6*48)\
            QPEL_H264HV(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, 7*48)\
            "cmpl $16, %3           \n\t"\
            "jne 2f                 \n\t"\
            QPEL_H264HV(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1,  8*48)\
            QPEL_H264HV(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2,  9*48)\
            QPEL_H264HV(%%mm4, %%mm5, %%mm0, %%mm1, %%mm2, %%mm3, 10*48)\
            QPEL_H264HV(%%mm5, %%mm0, %%mm1, %%mm2, %%mm3, %%mm4, 11*48)\
            QPEL_H264HV(%%mm0, %%mm1, %%mm2, %%mm3, %%mm4, %%mm5, 12*48)\
            QPEL_H264HV(%%mm1, %%mm2, %%mm3, %%mm4, %%mm5, %%mm0, 13*48)\
            QPEL_H264HV(%%mm2, %%mm3, %%mm4, %%mm5, %%mm0, %%mm1, 14*48)\
            QPEL_H264HV(%%mm3, %%mm4, %%mm5, %%mm0, %%mm1, %%mm2, 15*48)\
            "2:                     \n\t"\
            : "+a"(src)\
            : "c"(tmp), "S"((x86_reg)srcStride), "rm"(size)\
            : "memory"\
            );\
        tmp += 4;\
        src += 4 - (size+5)*srcStride;\
    }\
}\
static av_always_inline void OPNAME ## h264_qpel8or16_hv2_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, int dstStride, int tmpStride, int size){\
    int w = size>>4;\
    do{\
    int h = size;\
    __asm__ volatile(\
        "1:                         \n\t"\
        "movq     (%0), %%mm0       \n\t"\
        "movq    8(%0), %%mm3       \n\t"\
        "movq    2(%0), %%mm1       \n\t"\
        "movq   10(%0), %%mm4       \n\t"\
        "paddw   %%mm4, %%mm0       \n\t"\
        "paddw   %%mm3, %%mm1       \n\t"\
        "paddw  18(%0), %%mm3       \n\t"\
        "paddw  16(%0), %%mm4       \n\t"\
        "movq    4(%0), %%mm2       \n\t"\
        "movq   12(%0), %%mm5       \n\t"\
        "paddw   6(%0), %%mm2       \n\t"\
        "paddw  14(%0), %%mm5       \n\t"\
        "psubw %%mm1, %%mm0         \n\t"\
        "psubw %%mm4, %%mm3         \n\t"\
        "psraw $2, %%mm0            \n\t"\
        "psraw $2, %%mm3            \n\t"\
        "psubw %%mm1, %%mm0         \n\t"\
        "psubw %%mm4, %%mm3         \n\t"\
        "paddsw %%mm2, %%mm0        \n\t"\
        "paddsw %%mm5, %%mm3        \n\t"\
        "psraw $2, %%mm0            \n\t"\
        "psraw $2, %%mm3            \n\t"\
        "paddw %%mm2, %%mm0         \n\t"\
        "paddw %%mm5, %%mm3         \n\t"\
        "psraw $6, %%mm0            \n\t"\
        "psraw $6, %%mm3            \n\t"\
        "packuswb %%mm3, %%mm0      \n\t"\
        OP(%%mm0, (%1),%%mm7, q)\
        "add $48, %0                \n\t"\
        "add %3, %1                 \n\t"\
        "decl %2                    \n\t"\
        " jnz 1b                    \n\t"\
        : "+a"(tmp), "+c"(dst), "+g"(h)\
        : "S"((x86_reg)dstStride)\
        : "memory"\
    );\
    tmp += 8 - size*24;\
    dst += 8 - size*dstStride;\
    }while(w--);\
}\
\
static void OPNAME ## h264_qpel8_v_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    OPNAME ## h264_qpel8or16_v_lowpass_ ## MMX(dst  , src  , dstStride, srcStride, 8);\
}\
static av_noinline void OPNAME ## h264_qpel16_v_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    OPNAME ## h264_qpel8or16_v_lowpass_ ## MMX(dst  , src  , dstStride, srcStride, 16);\
    OPNAME ## h264_qpel8or16_v_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride, 16);\
}\
\
static void OPNAME ## h264_qpel16_h_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst  , src  , dstStride, srcStride);\
    OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride);\
    src += 8*srcStride;\
    dst += 8*dstStride;\
    OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst  , src  , dstStride, srcStride);\
    OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride);\
}\
\
static av_noinline void OPNAME ## h264_qpel16_h_lowpass_l2_ ## MMX(uint8_t *dst, uint8_t *src, uint8_t *src2, int dstStride, int src2Stride){\
    OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst  , src  , src2  , dstStride, src2Stride);\
    OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst+8, src+8, src2+8, dstStride, src2Stride);\
    src += 8*dstStride;\
    dst += 8*dstStride;\
    src2 += 8*src2Stride;\
    OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst  , src  , src2  , dstStride, src2Stride);\
    OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst+8, src+8, src2+8, dstStride, src2Stride);\
}\
\
static av_noinline void OPNAME ## h264_qpel8or16_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, uint8_t *src, int dstStride, int tmpStride, int srcStride, int size){\
          put_h264_qpel8or16_hv1_lowpass_ ## MMX(tmp, src, tmpStride, srcStride, size);\
    OPNAME ## h264_qpel8or16_hv2_lowpass_ ## MMX(dst, tmp, dstStride, tmpStride, size);\
}\
static void OPNAME ## h264_qpel8_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, uint8_t *src, int dstStride, int tmpStride, int srcStride){\
    OPNAME ## h264_qpel8or16_hv_lowpass_ ## MMX(dst  , tmp  , src  , dstStride, tmpStride, srcStride, 8);\
}\
\
static void OPNAME ## h264_qpel16_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, uint8_t *src, int dstStride, int tmpStride, int srcStride){\
    OPNAME ## h264_qpel8or16_hv_lowpass_ ## MMX(dst  , tmp  , src  , dstStride, tmpStride, srcStride, 16);\
}\
\
static av_noinline void OPNAME ## pixels4_l2_shift5_ ## MMX(uint8_t *dst, int16_t *src16, uint8_t *src8, int dstStride, int src8Stride, int h)\
{\
    __asm__ volatile(\
        "movq      (%1), %%mm0          \n\t"\
        "movq    24(%1), %%mm1          \n\t"\
        "psraw      $5,  %%mm0          \n\t"\
        "psraw      $5,  %%mm1          \n\t"\
        "packuswb %%mm0, %%mm0          \n\t"\
        "packuswb %%mm1, %%mm1          \n\t"\
        PAVGB"     (%0), %%mm0          \n\t"\
        PAVGB"  (%0,%3), %%mm1          \n\t"\
        OP(%%mm0, (%2),    %%mm4, d)\
        OP(%%mm1, (%2,%4), %%mm5, d)\
        "lea  (%0,%3,2), %0             \n\t"\
        "lea  (%2,%4,2), %2             \n\t"\
        "movq    48(%1), %%mm0          \n\t"\
        "movq    72(%1), %%mm1          \n\t"\
        "psraw      $5,  %%mm0          \n\t"\
        "psraw      $5,  %%mm1          \n\t"\
        "packuswb %%mm0, %%mm0          \n\t"\
        "packuswb %%mm1, %%mm1          \n\t"\
        PAVGB"     (%0), %%mm0          \n\t"\
        PAVGB"  (%0,%3), %%mm1          \n\t"\
        OP(%%mm0, (%2),    %%mm4, d)\
        OP(%%mm1, (%2,%4), %%mm5, d)\
        :"+a"(src8), "+c"(src16), "+d"(dst)\
        :"S"((x86_reg)src8Stride), "D"((x86_reg)dstStride)\
        :"memory");\
}\
static av_noinline void OPNAME ## pixels8_l2_shift5_ ## MMX(uint8_t *dst, int16_t *src16, uint8_t *src8, int dstStride, int src8Stride, int h)\
{\
    do{\
    __asm__ volatile(\
        "movq      (%1), %%mm0          \n\t"\
        "movq     8(%1), %%mm1          \n\t"\
        "movq    48(%1), %%mm2          \n\t"\
        "movq  8+48(%1), %%mm3          \n\t"\
        "psraw      $5,  %%mm0          \n\t"\
        "psraw      $5,  %%mm1          \n\t"\
        "psraw      $5,  %%mm2          \n\t"\
        "psraw      $5,  %%mm3          \n\t"\
        "packuswb %%mm1, %%mm0          \n\t"\
        "packuswb %%mm3, %%mm2          \n\t"\
        PAVGB"     (%0), %%mm0          \n\t"\
        PAVGB"  (%0,%3), %%mm2          \n\t"\
        OP(%%mm0, (%2), %%mm5, q)\
        OP(%%mm2, (%2,%4), %%mm5, q)\
        ::"a"(src8), "c"(src16), "d"(dst),\
          "r"((x86_reg)src8Stride), "r"((x86_reg)dstStride)\
        :"memory");\
        src8 += 2L*src8Stride;\
        src16 += 48;\
        dst += 2L*dstStride;\
    }while(h-=2);\
}\
static void OPNAME ## pixels16_l2_shift5_ ## MMX(uint8_t *dst, int16_t *src16, uint8_t *src8, int dstStride, int src8Stride, int h)\
{\
    OPNAME ## pixels8_l2_shift5_ ## MMX(dst  , src16  , src8  , dstStride, src8Stride, h);\
    OPNAME ## pixels8_l2_shift5_ ## MMX(dst+8, src16+8, src8+8, dstStride, src8Stride, h);\
}\


#if ARCH_X86_64
#define QPEL_H264_H16_XMM(OPNAME, OP, MMX)\
static av_noinline void OPNAME ## h264_qpel16_h_lowpass_l2_ ## MMX(uint8_t *dst, uint8_t *src, uint8_t *src2, int dstStride, int src2Stride){\
    int h=16;\
    __asm__ volatile(\
        "pxor %%xmm15, %%xmm15      \n\t"\
        "movdqa %6, %%xmm14         \n\t"\
        "movdqa %7, %%xmm13         \n\t"\
        "1:                         \n\t"\
        "lddqu    6(%0), %%xmm1     \n\t"\
        "lddqu   -2(%0), %%xmm7     \n\t"\
        "movdqa  %%xmm1, %%xmm0     \n\t"\
        "punpckhbw %%xmm15, %%xmm1  \n\t"\
        "punpcklbw %%xmm15, %%xmm0  \n\t"\
        "punpcklbw %%xmm15, %%xmm7  \n\t"\
        "movdqa  %%xmm1, %%xmm2     \n\t"\
        "movdqa  %%xmm0, %%xmm6     \n\t"\
        "movdqa  %%xmm1, %%xmm3     \n\t"\
        "movdqa  %%xmm0, %%xmm8     \n\t"\
        "movdqa  %%xmm1, %%xmm4     \n\t"\
        "movdqa  %%xmm0, %%xmm9     \n\t"\
        "movdqa  %%xmm0, %%xmm12    \n\t"\
        "movdqa  %%xmm1, %%xmm11    \n\t"\
        "palignr $10,%%xmm0, %%xmm11\n\t"\
        "palignr $10,%%xmm7, %%xmm12\n\t"\
        "palignr $2, %%xmm0, %%xmm4 \n\t"\
        "palignr $2, %%xmm7, %%xmm9 \n\t"\
        "palignr $4, %%xmm0, %%xmm3 \n\t"\
        "palignr $4, %%xmm7, %%xmm8 \n\t"\
        "palignr $6, %%xmm0, %%xmm2 \n\t"\
        "palignr $6, %%xmm7, %%xmm6 \n\t"\
        "paddw   %%xmm0 ,%%xmm11    \n\t"\
        "palignr $8, %%xmm0, %%xmm1 \n\t"\
        "palignr $8, %%xmm7, %%xmm0 \n\t"\
        "paddw   %%xmm12,%%xmm7     \n\t"\
        "paddw   %%xmm3, %%xmm2     \n\t"\
        "paddw   %%xmm8, %%xmm6     \n\t"\
        "paddw   %%xmm4, %%xmm1     \n\t"\
        "paddw   %%xmm9, %%xmm0     \n\t"\
        "psllw   $2,     %%xmm2     \n\t"\
        "psllw   $2,     %%xmm6     \n\t"\
        "psubw   %%xmm1, %%xmm2     \n\t"\
        "psubw   %%xmm0, %%xmm6     \n\t"\
        "paddw   %%xmm13,%%xmm11    \n\t"\
        "paddw   %%xmm13,%%xmm7     \n\t"\
        "pmullw  %%xmm14,%%xmm2     \n\t"\
        "pmullw  %%xmm14,%%xmm6     \n\t"\
        "lddqu   (%2),   %%xmm3     \n\t"\
        "paddw   %%xmm11,%%xmm2     \n\t"\
        "paddw   %%xmm7, %%xmm6     \n\t"\
        "psraw   $5,     %%xmm2     \n\t"\
        "psraw   $5,     %%xmm6     \n\t"\
        "packuswb %%xmm2,%%xmm6     \n\t"\
        "pavgb   %%xmm3, %%xmm6     \n\t"\
        OP(%%xmm6, (%1), %%xmm4, dqa)\
        "add %5, %0                 \n\t"\
        "add %5, %1                 \n\t"\
        "add %4, %2                 \n\t"\
        "decl %3                    \n\t"\
        "jg 1b                      \n\t"\
        : "+a"(src), "+c"(dst), "+d"(src2), "+g"(h)\
        : "D"((x86_reg)src2Stride), "S"((x86_reg)dstStride),\
          "m"(ff_pw_5), "m"(ff_pw_16)\
        : XMM_CLOBBERS("%xmm0" , "%xmm1" , "%xmm2" , "%xmm3" , \
                       "%xmm4" , "%xmm5" , "%xmm6" , "%xmm7" , \
                       "%xmm8" , "%xmm9" , "%xmm10", "%xmm11", \
                       "%xmm12", "%xmm13", "%xmm14", "%xmm15",)\
          "memory"\
    );\
}
#else // ARCH_X86_64
#define QPEL_H264_H16_XMM(OPNAME, OP, MMX)\
static av_noinline void OPNAME ## h264_qpel16_h_lowpass_l2_ ## MMX(uint8_t *dst, uint8_t *src, uint8_t *src2, int dstStride, int src2Stride){\
    OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst  , src  , src2  , dstStride, src2Stride);\
    OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst+8, src+8, src2+8, dstStride, src2Stride);\
    src += 8*dstStride;\
    dst += 8*dstStride;\
    src2 += 8*src2Stride;\
    OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst  , src  , src2  , dstStride, src2Stride);\
    OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(dst+8, src+8, src2+8, dstStride, src2Stride);\
}
#endif // ARCH_X86_64

#define QPEL_H264_H_XMM(OPNAME, OP, MMX)\
static av_noinline void OPNAME ## h264_qpel8_h_lowpass_l2_ ## MMX(uint8_t *dst, uint8_t *src, uint8_t *src2, int dstStride, int src2Stride){\
    int h=8;\
    __asm__ volatile(\
        "pxor %%xmm7, %%xmm7        \n\t"\
        "movdqa "MANGLE(ff_pw_5)", %%xmm6\n\t"\
        "1:                         \n\t"\
        "lddqu   -2(%0), %%xmm1     \n\t"\
        "movdqa  %%xmm1, %%xmm0     \n\t"\
        "punpckhbw %%xmm7, %%xmm1   \n\t"\
        "punpcklbw %%xmm7, %%xmm0   \n\t"\
        "movdqa  %%xmm1, %%xmm2     \n\t"\
        "movdqa  %%xmm1, %%xmm3     \n\t"\
        "movdqa  %%xmm1, %%xmm4     \n\t"\
        "movdqa  %%xmm1, %%xmm5     \n\t"\
        "palignr $2, %%xmm0, %%xmm4 \n\t"\
        "palignr $4, %%xmm0, %%xmm3 \n\t"\
        "palignr $6, %%xmm0, %%xmm2 \n\t"\
        "palignr $8, %%xmm0, %%xmm1 \n\t"\
        "palignr $10,%%xmm0, %%xmm5 \n\t"\
        "paddw   %%xmm5, %%xmm0     \n\t"\
        "paddw   %%xmm3, %%xmm2     \n\t"\
        "paddw   %%xmm4, %%xmm1     \n\t"\
        "psllw   $2,     %%xmm2     \n\t"\
        "movq    (%2),   %%xmm3     \n\t"\
        "psubw   %%xmm1, %%xmm2     \n\t"\
        "paddw "MANGLE(ff_pw_16)", %%xmm0\n\t"\
        "pmullw  %%xmm6, %%xmm2     \n\t"\
        "paddw   %%xmm0, %%xmm2     \n\t"\
        "psraw   $5,     %%xmm2     \n\t"\
        "packuswb %%xmm2, %%xmm2    \n\t"\
        "pavgb   %%xmm3, %%xmm2     \n\t"\
        OP(%%xmm2, (%1), %%xmm4, q)\
        "add %5, %0                 \n\t"\
        "add %5, %1                 \n\t"\
        "add %4, %2                 \n\t"\
        "decl %3                    \n\t"\
        "jg 1b                      \n\t"\
        : "+a"(src), "+c"(dst), "+d"(src2), "+g"(h)\
        : "D"((x86_reg)src2Stride), "S"((x86_reg)dstStride)\
        : XMM_CLOBBERS("%xmm0", "%xmm1", "%xmm2", "%xmm3", \
                       "%xmm4", "%xmm5", "%xmm6", "%xmm7",)\
          "memory"\
    );\
}\
QPEL_H264_H16_XMM(OPNAME, OP, MMX)\
\
static av_noinline void OPNAME ## h264_qpel8_h_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    int h=8;\
    __asm__ volatile(\
        "pxor %%xmm7, %%xmm7        \n\t"\
        "movdqa "MANGLE(ff_pw_5)", %%xmm6\n\t"\
        "1:                         \n\t"\
        "lddqu   -2(%0), %%xmm1     \n\t"\
        "movdqa  %%xmm1, %%xmm0     \n\t"\
        "punpckhbw %%xmm7, %%xmm1   \n\t"\
        "punpcklbw %%xmm7, %%xmm0   \n\t"\
        "movdqa  %%xmm1, %%xmm2     \n\t"\
        "movdqa  %%xmm1, %%xmm3     \n\t"\
        "movdqa  %%xmm1, %%xmm4     \n\t"\
        "movdqa  %%xmm1, %%xmm5     \n\t"\
        "palignr $2, %%xmm0, %%xmm4 \n\t"\
        "palignr $4, %%xmm0, %%xmm3 \n\t"\
        "palignr $6, %%xmm0, %%xmm2 \n\t"\
        "palignr $8, %%xmm0, %%xmm1 \n\t"\
        "palignr $10,%%xmm0, %%xmm5 \n\t"\
        "paddw   %%xmm5, %%xmm0     \n\t"\
        "paddw   %%xmm3, %%xmm2     \n\t"\
        "paddw   %%xmm4, %%xmm1     \n\t"\
        "psllw   $2,     %%xmm2     \n\t"\
        "psubw   %%xmm1, %%xmm2     \n\t"\
        "paddw   "MANGLE(ff_pw_16)", %%xmm0\n\t"\
        "pmullw  %%xmm6, %%xmm2     \n\t"\
        "paddw   %%xmm0, %%xmm2     \n\t"\
        "psraw   $5,     %%xmm2     \n\t"\
        "packuswb %%xmm2, %%xmm2    \n\t"\
        OP(%%xmm2, (%1), %%xmm4, q)\
        "add %3, %0                 \n\t"\
        "add %4, %1                 \n\t"\
        "decl %2                    \n\t"\
        " jnz 1b                    \n\t"\
        : "+a"(src), "+c"(dst), "+g"(h)\
        : "D"((x86_reg)srcStride), "S"((x86_reg)dstStride)\
        : XMM_CLOBBERS("%xmm0", "%xmm1", "%xmm2", "%xmm3", \
                       "%xmm4", "%xmm5", "%xmm6", "%xmm7",)\
          "memory"\
    );\
}\
static void OPNAME ## h264_qpel16_h_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst  , src  , dstStride, srcStride);\
    OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride);\
    src += 8*srcStride;\
    dst += 8*dstStride;\
    OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst  , src  , dstStride, srcStride);\
    OPNAME ## h264_qpel8_h_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride);\
}\

#define QPEL_H264_V_XMM(OPNAME, OP, MMX)\
static av_noinline void OPNAME ## h264_qpel8or16_v_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride, int h){\
    src -= 2*srcStride;\
    \
    __asm__ volatile(\
        "pxor %%xmm7, %%xmm7        \n\t"\
        "movq (%0), %%xmm0          \n\t"\
        "add %2, %0                 \n\t"\
        "movq (%0), %%xmm1          \n\t"\
        "add %2, %0                 \n\t"\
        "movq (%0), %%xmm2          \n\t"\
        "add %2, %0                 \n\t"\
        "movq (%0), %%xmm3          \n\t"\
        "add %2, %0                 \n\t"\
        "movq (%0), %%xmm4          \n\t"\
        "add %2, %0                 \n\t"\
        "punpcklbw %%xmm7, %%xmm0   \n\t"\
        "punpcklbw %%xmm7, %%xmm1   \n\t"\
        "punpcklbw %%xmm7, %%xmm2   \n\t"\
        "punpcklbw %%xmm7, %%xmm3   \n\t"\
        "punpcklbw %%xmm7, %%xmm4   \n\t"\
        QPEL_H264V_XMM(%%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, OP)\
        QPEL_H264V_XMM(%%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, OP)\
        QPEL_H264V_XMM(%%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1, OP)\
        QPEL_H264V_XMM(%%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1, %%xmm2, OP)\
        QPEL_H264V_XMM(%%xmm4, %%xmm5, %%xmm0, %%xmm1, %%xmm2, %%xmm3, OP)\
        QPEL_H264V_XMM(%%xmm5, %%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, OP)\
        QPEL_H264V_XMM(%%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, OP)\
        QPEL_H264V_XMM(%%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, OP)\
        "cmpl $16, %4               \n\t"\
        "jne 2f                     \n\t"\
        QPEL_H264V_XMM(%%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1, OP)\
        QPEL_H264V_XMM(%%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1, %%xmm2, OP)\
        QPEL_H264V_XMM(%%xmm4, %%xmm5, %%xmm0, %%xmm1, %%xmm2, %%xmm3, OP)\
        QPEL_H264V_XMM(%%xmm5, %%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, OP)\
        QPEL_H264V_XMM(%%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, OP)\
        QPEL_H264V_XMM(%%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, OP)\
        QPEL_H264V_XMM(%%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1, OP)\
        QPEL_H264V_XMM(%%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1, %%xmm2, OP)\
        "2:                          \n\t"\
        \
        : "+a"(src), "+c"(dst)\
        : "S"((x86_reg)srcStride), "D"((x86_reg)dstStride), "rm"(h)\
        : XMM_CLOBBERS("%xmm0", "%xmm1", "%xmm2", "%xmm3", \
                       "%xmm4", "%xmm5", "%xmm6", "%xmm7",)\
          "memory"\
    );\
}\
static void OPNAME ## h264_qpel8_v_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    OPNAME ## h264_qpel8or16_v_lowpass_ ## MMX(dst  , src  , dstStride, srcStride, 8);\
}\
static av_noinline void OPNAME ## h264_qpel16_v_lowpass_ ## MMX(uint8_t *dst, uint8_t *src, int dstStride, int srcStride){\
    OPNAME ## h264_qpel8or16_v_lowpass_ ## MMX(dst  , src  , dstStride, srcStride, 16);\
    OPNAME ## h264_qpel8or16_v_lowpass_ ## MMX(dst+8, src+8, dstStride, srcStride, 16);\
}

static av_always_inline void put_h264_qpel8or16_hv1_lowpass_sse2(int16_t *tmp, uint8_t *src, int tmpStride, int srcStride, int size){
    int w = (size+8)>>3;
    src -= 2*srcStride+2;
    while(w--){
        __asm__ volatile(
            "pxor %%xmm7, %%xmm7        \n\t"
            "movq (%0), %%xmm0          \n\t"
            "add %2, %0                 \n\t"
            "movq (%0), %%xmm1          \n\t"
            "add %2, %0                 \n\t"
            "movq (%0), %%xmm2          \n\t"
            "add %2, %0                 \n\t"
            "movq (%0), %%xmm3          \n\t"
            "add %2, %0                 \n\t"
            "movq (%0), %%xmm4          \n\t"
            "add %2, %0                 \n\t"
            "punpcklbw %%xmm7, %%xmm0   \n\t"
            "punpcklbw %%xmm7, %%xmm1   \n\t"
            "punpcklbw %%xmm7, %%xmm2   \n\t"
            "punpcklbw %%xmm7, %%xmm3   \n\t"
            "punpcklbw %%xmm7, %%xmm4   \n\t"
            QPEL_H264HV_XMM(%%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, 0*48)
            QPEL_H264HV_XMM(%%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, 1*48)
            QPEL_H264HV_XMM(%%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1, 2*48)
            QPEL_H264HV_XMM(%%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1, %%xmm2, 3*48)
            QPEL_H264HV_XMM(%%xmm4, %%xmm5, %%xmm0, %%xmm1, %%xmm2, %%xmm3, 4*48)
            QPEL_H264HV_XMM(%%xmm5, %%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, 5*48)
            QPEL_H264HV_XMM(%%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, 6*48)
            QPEL_H264HV_XMM(%%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, 7*48)
            "cmpl $16, %3               \n\t"
            "jne 2f                     \n\t"
            QPEL_H264HV_XMM(%%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1,  8*48)
            QPEL_H264HV_XMM(%%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1, %%xmm2,  9*48)
            QPEL_H264HV_XMM(%%xmm4, %%xmm5, %%xmm0, %%xmm1, %%xmm2, %%xmm3, 10*48)
            QPEL_H264HV_XMM(%%xmm5, %%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, 11*48)
            QPEL_H264HV_XMM(%%xmm0, %%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, 12*48)
            QPEL_H264HV_XMM(%%xmm1, %%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, 13*48)
            QPEL_H264HV_XMM(%%xmm2, %%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1, 14*48)
            QPEL_H264HV_XMM(%%xmm3, %%xmm4, %%xmm5, %%xmm0, %%xmm1, %%xmm2, 15*48)
            "2:                         \n\t"
            : "+a"(src)
            : "c"(tmp), "S"((x86_reg)srcStride), "rm"(size)
            : XMM_CLOBBERS("%xmm0", "%xmm1", "%xmm2", "%xmm3",
                           "%xmm4", "%xmm5", "%xmm6", "%xmm7",)
              "memory"
        );
        tmp += 8;
        src += 8 - (size+5)*srcStride;
    }
}

#define QPEL_H264_HV2_XMM(OPNAME, OP, MMX)\
static av_always_inline void OPNAME ## h264_qpel8or16_hv2_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, int dstStride, int tmpStride, int size){\
    int h = size;\
    if(size == 16){\
        __asm__ volatile(\
            "1:                         \n\t"\
            "movdqa 32(%0), %%xmm4      \n\t"\
            "movdqa 16(%0), %%xmm5      \n\t"\
            "movdqa   (%0), %%xmm7      \n\t"\
            "movdqa %%xmm4, %%xmm3      \n\t"\
            "movdqa %%xmm4, %%xmm2      \n\t"\
            "movdqa %%xmm4, %%xmm1      \n\t"\
            "movdqa %%xmm4, %%xmm0      \n\t"\
            "palignr $10, %%xmm5, %%xmm0 \n\t"\
            "palignr  $8, %%xmm5, %%xmm1 \n\t"\
            "palignr  $6, %%xmm5, %%xmm2 \n\t"\
            "palignr  $4, %%xmm5, %%xmm3 \n\t"\
            "palignr  $2, %%xmm5, %%xmm4 \n\t"\
            "paddw  %%xmm5, %%xmm0      \n\t"\
            "paddw  %%xmm4, %%xmm1      \n\t"\
            "paddw  %%xmm3, %%xmm2      \n\t"\
            "movdqa %%xmm5, %%xmm6      \n\t"\
            "movdqa %%xmm5, %%xmm4      \n\t"\
            "movdqa %%xmm5, %%xmm3      \n\t"\
            "palignr  $8, %%xmm7, %%xmm4 \n\t"\
            "palignr  $2, %%xmm7, %%xmm6 \n\t"\
            "palignr $10, %%xmm7, %%xmm3 \n\t"\
            "paddw  %%xmm6, %%xmm4      \n\t"\
            "movdqa %%xmm5, %%xmm6      \n\t"\
            "palignr  $6, %%xmm7, %%xmm5 \n\t"\
            "palignr  $4, %%xmm7, %%xmm6 \n\t"\
            "paddw  %%xmm7, %%xmm3      \n\t"\
            "paddw  %%xmm6, %%xmm5      \n\t"\
            \
            "psubw  %%xmm1, %%xmm0      \n\t"\
            "psubw  %%xmm4, %%xmm3      \n\t"\
            "psraw      $2, %%xmm0      \n\t"\
            "psraw      $2, %%xmm3      \n\t"\
            "psubw  %%xmm1, %%xmm0      \n\t"\
            "psubw  %%xmm4, %%xmm3      \n\t"\
            "paddw  %%xmm2, %%xmm0      \n\t"\
            "paddw  %%xmm5, %%xmm3      \n\t"\
            "psraw      $2, %%xmm0      \n\t"\
            "psraw      $2, %%xmm3      \n\t"\
            "paddw  %%xmm2, %%xmm0      \n\t"\
            "paddw  %%xmm5, %%xmm3      \n\t"\
            "psraw      $6, %%xmm0      \n\t"\
            "psraw      $6, %%xmm3      \n\t"\
            "packuswb %%xmm0, %%xmm3    \n\t"\
            OP(%%xmm3, (%1), %%xmm7, dqa)\
            "add $48, %0                \n\t"\
            "add %3, %1                 \n\t"\
            "decl %2                    \n\t"\
            " jnz 1b                    \n\t"\
            : "+a"(tmp), "+c"(dst), "+g"(h)\
            : "S"((x86_reg)dstStride)\
            : XMM_CLOBBERS("%xmm0", "%xmm1", "%xmm2", "%xmm3", \
                           "%xmm4", "%xmm5", "%xmm6", "%xmm7",)\
              "memory"\
        );\
    }else{\
        __asm__ volatile(\
            "1:                         \n\t"\
            "movdqa 16(%0), %%xmm1      \n\t"\
            "movdqa   (%0), %%xmm0      \n\t"\
            "movdqa %%xmm1, %%xmm2      \n\t"\
            "movdqa %%xmm1, %%xmm3      \n\t"\
            "movdqa %%xmm1, %%xmm4      \n\t"\
            "movdqa %%xmm1, %%xmm5      \n\t"\
            "palignr $10, %%xmm0, %%xmm5 \n\t"\
            "palignr  $8, %%xmm0, %%xmm4 \n\t"\
            "palignr  $6, %%xmm0, %%xmm3 \n\t"\
            "palignr  $4, %%xmm0, %%xmm2 \n\t"\
            "palignr  $2, %%xmm0, %%xmm1 \n\t"\
            "paddw  %%xmm5, %%xmm0      \n\t"\
            "paddw  %%xmm4, %%xmm1      \n\t"\
            "paddw  %%xmm3, %%xmm2      \n\t"\
            "psubw  %%xmm1, %%xmm0      \n\t"\
            "psraw      $2, %%xmm0      \n\t"\
            "psubw  %%xmm1, %%xmm0      \n\t"\
            "paddw  %%xmm2, %%xmm0      \n\t"\
            "psraw      $2, %%xmm0      \n\t"\
            "paddw  %%xmm2, %%xmm0      \n\t"\
            "psraw      $6, %%xmm0      \n\t"\
            "packuswb %%xmm0, %%xmm0    \n\t"\
            OP(%%xmm0, (%1), %%xmm7, q)\
            "add $48, %0                \n\t"\
            "add %3, %1                 \n\t"\
            "decl %2                    \n\t"\
            " jnz 1b                    \n\t"\
            : "+a"(tmp), "+c"(dst), "+g"(h)\
            : "S"((x86_reg)dstStride)\
            : XMM_CLOBBERS("%xmm0", "%xmm1", "%xmm2", "%xmm3", \
                           "%xmm4", "%xmm5", "%xmm6", "%xmm7",)\
              "memory"\
        );\
    }\
}

#define QPEL_H264_HV_XMM(OPNAME, OP, MMX)\
static av_noinline void OPNAME ## h264_qpel8or16_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, uint8_t *src, int dstStride, int tmpStride, int srcStride, int size){\
          put_h264_qpel8or16_hv1_lowpass_sse2(tmp, src, tmpStride, srcStride, size);\
    OPNAME ## h264_qpel8or16_hv2_lowpass_ ## MMX(dst, tmp, dstStride, tmpStride, size);\
}\
static void OPNAME ## h264_qpel8_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, uint8_t *src, int dstStride, int tmpStride, int srcStride){\
    OPNAME ## h264_qpel8or16_hv_lowpass_ ## MMX(dst, tmp, src, dstStride, tmpStride, srcStride, 8);\
}\
static void OPNAME ## h264_qpel16_hv_lowpass_ ## MMX(uint8_t *dst, int16_t *tmp, uint8_t *src, int dstStride, int tmpStride, int srcStride){\
    OPNAME ## h264_qpel8or16_hv_lowpass_ ## MMX(dst, tmp, src, dstStride, tmpStride, srcStride, 16);\
}\

#define put_pixels8_l2_sse2 put_pixels8_l2_mmx2
#define avg_pixels8_l2_sse2 avg_pixels8_l2_mmx2
#define put_pixels16_l2_sse2 put_pixels16_l2_mmx2
#define avg_pixels16_l2_sse2 avg_pixels16_l2_mmx2
#define put_pixels8_l2_ssse3 put_pixels8_l2_mmx2
#define avg_pixels8_l2_ssse3 avg_pixels8_l2_mmx2
#define put_pixels16_l2_ssse3 put_pixels16_l2_mmx2
#define avg_pixels16_l2_ssse3 avg_pixels16_l2_mmx2

#define put_pixels8_l2_shift5_sse2 put_pixels8_l2_shift5_mmx2
#define avg_pixels8_l2_shift5_sse2 avg_pixels8_l2_shift5_mmx2
#define put_pixels16_l2_shift5_sse2 put_pixels16_l2_shift5_mmx2
#define avg_pixels16_l2_shift5_sse2 avg_pixels16_l2_shift5_mmx2
#define put_pixels8_l2_shift5_ssse3 put_pixels8_l2_shift5_mmx2
#define avg_pixels8_l2_shift5_ssse3 avg_pixels8_l2_shift5_mmx2
#define put_pixels16_l2_shift5_ssse3 put_pixels16_l2_shift5_mmx2
#define avg_pixels16_l2_shift5_ssse3 avg_pixels16_l2_shift5_mmx2

#define put_h264_qpel8_h_lowpass_l2_sse2 put_h264_qpel8_h_lowpass_l2_mmx2
#define avg_h264_qpel8_h_lowpass_l2_sse2 avg_h264_qpel8_h_lowpass_l2_mmx2
#define put_h264_qpel16_h_lowpass_l2_sse2 put_h264_qpel16_h_lowpass_l2_mmx2
#define avg_h264_qpel16_h_lowpass_l2_sse2 avg_h264_qpel16_h_lowpass_l2_mmx2

#define put_h264_qpel8_v_lowpass_ssse3 put_h264_qpel8_v_lowpass_sse2
#define avg_h264_qpel8_v_lowpass_ssse3 avg_h264_qpel8_v_lowpass_sse2
#define put_h264_qpel16_v_lowpass_ssse3 put_h264_qpel16_v_lowpass_sse2
#define avg_h264_qpel16_v_lowpass_ssse3 avg_h264_qpel16_v_lowpass_sse2

#define put_h264_qpel8or16_hv2_lowpass_sse2 put_h264_qpel8or16_hv2_lowpass_mmx2
#define avg_h264_qpel8or16_hv2_lowpass_sse2 avg_h264_qpel8or16_hv2_lowpass_mmx2

#define H264_MC(OPNAME, SIZE, MMX, ALIGN) \
H264_MC_C(OPNAME, SIZE, MMX, ALIGN)\
H264_MC_V(OPNAME, SIZE, MMX, ALIGN)\
H264_MC_H(OPNAME, SIZE, MMX, ALIGN)\
H264_MC_HV(OPNAME, SIZE, MMX, ALIGN)\

static void put_h264_qpel16_mc00_sse2 (uint8_t *dst, uint8_t *src, int stride){
    put_pixels16_sse2(dst, src, stride, 16);
}
static void avg_h264_qpel16_mc00_sse2 (uint8_t *dst, uint8_t *src, int stride){
    avg_pixels16_sse2(dst, src, stride, 16);
}
#define put_h264_qpel8_mc00_sse2 put_h264_qpel8_mc00_mmx2
#define avg_h264_qpel8_mc00_sse2 avg_h264_qpel8_mc00_mmx2

#define H264_MC_C(OPNAME, SIZE, MMX, ALIGN) \
static void OPNAME ## h264_qpel ## SIZE ## _mc00_ ## MMX (uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## pixels ## SIZE ## _ ## MMX(dst, src, stride, SIZE);\
}\

#define H264_MC_H(OPNAME, SIZE, MMX, ALIGN) \
static void OPNAME ## h264_qpel ## SIZE ## _mc10_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src, src, stride, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc20_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## h264_qpel ## SIZE ## _h_lowpass_ ## MMX(dst, src, stride, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc30_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src, src+1, stride, stride);\
}\

#define H264_MC_V(OPNAME, SIZE, MMX, ALIGN) \
static void OPNAME ## h264_qpel ## SIZE ## _mc01_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(ALIGN, uint8_t, temp)[SIZE*SIZE];\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(temp, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## MMX(dst, src, temp, stride, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc02_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## h264_qpel ## SIZE ## _v_lowpass_ ## MMX(dst, src, stride, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc03_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(ALIGN, uint8_t, temp)[SIZE*SIZE];\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(temp, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## MMX(dst, src+stride, temp, stride, stride, SIZE);\
}\

#define H264_MC_HV(OPNAME, SIZE, MMX, ALIGN) \
static void OPNAME ## h264_qpel ## SIZE ## _mc11_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(ALIGN, uint8_t, temp)[SIZE*SIZE];\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(temp, src, SIZE, stride);\
    OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src, temp, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc31_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(ALIGN, uint8_t, temp)[SIZE*SIZE];\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(temp, src+1, SIZE, stride);\
    OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src, temp, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc13_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(ALIGN, uint8_t, temp)[SIZE*SIZE];\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(temp, src, SIZE, stride);\
    OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src+stride, temp, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc33_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(ALIGN, uint8_t, temp)[SIZE*SIZE];\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## MMX(temp, src+1, SIZE, stride);\
    OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src+stride, temp, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc22_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(ALIGN, uint16_t, temp)[SIZE*(SIZE<8?12:24)];\
    OPNAME ## h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(dst, temp, src, stride, SIZE, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc21_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(ALIGN, uint8_t, temp)[SIZE*(SIZE<8?12:24)*2 + SIZE*SIZE];\
    uint8_t * const halfHV= temp;\
    int16_t * const halfV= (int16_t*)(temp + SIZE*SIZE);\
    assert(((int)temp & 7) == 0);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(halfHV, halfV, src, SIZE, SIZE, stride);\
    OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src, halfHV, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc23_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(ALIGN, uint8_t, temp)[SIZE*(SIZE<8?12:24)*2 + SIZE*SIZE];\
    uint8_t * const halfHV= temp;\
    int16_t * const halfV= (int16_t*)(temp + SIZE*SIZE);\
    assert(((int)temp & 7) == 0);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(halfHV, halfV, src, SIZE, SIZE, stride);\
    OPNAME ## h264_qpel ## SIZE ## _h_lowpass_l2_ ## MMX(dst, src+stride, halfHV, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc12_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(ALIGN, uint8_t, temp)[SIZE*(SIZE<8?12:24)*2 + SIZE*SIZE];\
    uint8_t * const halfHV= temp;\
    int16_t * const halfV= (int16_t*)(temp + SIZE*SIZE);\
    assert(((int)temp & 7) == 0);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(halfHV, halfV, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_shift5_ ## MMX(dst, halfV+2, halfHV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc32_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(ALIGN, uint8_t, temp)[SIZE*(SIZE<8?12:24)*2 + SIZE*SIZE];\
    uint8_t * const halfHV= temp;\
    int16_t * const halfV= (int16_t*)(temp + SIZE*SIZE);\
    assert(((int)temp & 7) == 0);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## MMX(halfHV, halfV, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_shift5_ ## MMX(dst, halfV+3, halfHV, stride, SIZE, SIZE);\
}\

#define H264_MC_4816(MMX)\
H264_MC(put_, 4, MMX, 8)\
H264_MC(put_, 8, MMX, 8)\
H264_MC(put_, 16,MMX, 8)\
H264_MC(avg_, 4, MMX, 8)\
H264_MC(avg_, 8, MMX, 8)\
H264_MC(avg_, 16,MMX, 8)\

#define H264_MC_816(QPEL, XMM)\
QPEL(put_, 8, XMM, 16)\
QPEL(put_, 16,XMM, 16)\
QPEL(avg_, 8, XMM, 16)\
QPEL(avg_, 16,XMM, 16)\

#define PAVGB "pavgusb"
QPEL_H264(put_,       PUT_OP, 3dnow)
QPEL_H264(avg_, AVG_3DNOW_OP, 3dnow)
#undef PAVGB
#define PAVGB "pavgb"
QPEL_H264(put_,       PUT_OP, mmx2)
QPEL_H264(avg_,  AVG_MMX2_OP, mmx2)
QPEL_H264_V_XMM(put_,       PUT_OP, sse2)
QPEL_H264_V_XMM(avg_,  AVG_MMX2_OP, sse2)
QPEL_H264_HV_XMM(put_,       PUT_OP, sse2)
QPEL_H264_HV_XMM(avg_,  AVG_MMX2_OP, sse2)
#if HAVE_SSSE3_INLINE
QPEL_H264_H_XMM(put_,       PUT_OP, ssse3)
QPEL_H264_H_XMM(avg_,  AVG_MMX2_OP, ssse3)
QPEL_H264_HV2_XMM(put_,       PUT_OP, ssse3)
QPEL_H264_HV2_XMM(avg_,  AVG_MMX2_OP, ssse3)
QPEL_H264_HV_XMM(put_,       PUT_OP, ssse3)
QPEL_H264_HV_XMM(avg_,  AVG_MMX2_OP, ssse3)
#endif
#undef PAVGB

H264_MC_4816(3dnow)
H264_MC_4816(mmx2)
H264_MC_816(H264_MC_V, sse2)
H264_MC_816(H264_MC_HV, sse2)
#if HAVE_SSSE3_INLINE
H264_MC_816(H264_MC_H, ssse3)
H264_MC_816(H264_MC_HV, ssse3)
#endif

#endif /* HAVE_INLINE_ASM */

//10bit
#define LUMA_MC_OP(OP, NUM, DEPTH, TYPE, OPT) \
void ff_ ## OP ## _h264_qpel ## NUM ## _ ## TYPE ## _ ## DEPTH ## _ ## OPT \
    (uint8_t *dst, uint8_t *src, int stride);

#define LUMA_MC_ALL(DEPTH, TYPE, OPT) \
    LUMA_MC_OP(put,  4, DEPTH, TYPE, OPT) \
    LUMA_MC_OP(avg,  4, DEPTH, TYPE, OPT) \
    LUMA_MC_OP(put,  8, DEPTH, TYPE, OPT) \
    LUMA_MC_OP(avg,  8, DEPTH, TYPE, OPT) \
    LUMA_MC_OP(put, 16, DEPTH, TYPE, OPT) \
    LUMA_MC_OP(avg, 16, DEPTH, TYPE, OPT)

#define LUMA_MC_816(DEPTH, TYPE, OPT) \
    LUMA_MC_OP(put,  8, DEPTH, TYPE, OPT) \
    LUMA_MC_OP(avg,  8, DEPTH, TYPE, OPT) \
    LUMA_MC_OP(put, 16, DEPTH, TYPE, OPT) \
    LUMA_MC_OP(avg, 16, DEPTH, TYPE, OPT)

LUMA_MC_ALL(10, mc00, mmxext)
LUMA_MC_ALL(10, mc10, mmxext)
LUMA_MC_ALL(10, mc20, mmxext)
LUMA_MC_ALL(10, mc30, mmxext)
LUMA_MC_ALL(10, mc01, mmxext)
LUMA_MC_ALL(10, mc11, mmxext)
LUMA_MC_ALL(10, mc21, mmxext)
LUMA_MC_ALL(10, mc31, mmxext)
LUMA_MC_ALL(10, mc02, mmxext)
LUMA_MC_ALL(10, mc12, mmxext)
LUMA_MC_ALL(10, mc22, mmxext)
LUMA_MC_ALL(10, mc32, mmxext)
LUMA_MC_ALL(10, mc03, mmxext)
LUMA_MC_ALL(10, mc13, mmxext)
LUMA_MC_ALL(10, mc23, mmxext)
LUMA_MC_ALL(10, mc33, mmxext)

LUMA_MC_816(10, mc00, sse2)
LUMA_MC_816(10, mc10, sse2)
LUMA_MC_816(10, mc10, sse2_cache64)
LUMA_MC_816(10, mc10, ssse3_cache64)
LUMA_MC_816(10, mc20, sse2)
LUMA_MC_816(10, mc20, sse2_cache64)
LUMA_MC_816(10, mc20, ssse3_cache64)
LUMA_MC_816(10, mc30, sse2)
LUMA_MC_816(10, mc30, sse2_cache64)
LUMA_MC_816(10, mc30, ssse3_cache64)
LUMA_MC_816(10, mc01, sse2)
LUMA_MC_816(10, mc11, sse2)
LUMA_MC_816(10, mc21, sse2)
LUMA_MC_816(10, mc31, sse2)
LUMA_MC_816(10, mc02, sse2)
LUMA_MC_816(10, mc12, sse2)
LUMA_MC_816(10, mc22, sse2)
LUMA_MC_816(10, mc32, sse2)
LUMA_MC_816(10, mc03, sse2)
LUMA_MC_816(10, mc13, sse2)
LUMA_MC_816(10, mc23, sse2)
LUMA_MC_816(10, mc33, sse2)

#define QPEL16_OPMC(OP, MC, MMX)\
void ff_ ## OP ## _h264_qpel16_ ## MC ## _10_ ## MMX(uint8_t *dst, uint8_t *src, int stride){\
    ff_ ## OP ## _h264_qpel8_ ## MC ## _10_ ## MMX(dst   , src   , stride);\
    ff_ ## OP ## _h264_qpel8_ ## MC ## _10_ ## MMX(dst+16, src+16, stride);\
    src += 8*stride;\
    dst += 8*stride;\
    ff_ ## OP ## _h264_qpel8_ ## MC ## _10_ ## MMX(dst   , src   , stride);\
    ff_ ## OP ## _h264_qpel8_ ## MC ## _10_ ## MMX(dst+16, src+16, stride);\
}

#define QPEL16_OP(MC, MMX)\
QPEL16_OPMC(put, MC, MMX)\
QPEL16_OPMC(avg, MC, MMX)

#define QPEL16(MMX)\
QPEL16_OP(mc00, MMX)\
QPEL16_OP(mc01, MMX)\
QPEL16_OP(mc02, MMX)\
QPEL16_OP(mc03, MMX)\
QPEL16_OP(mc10, MMX)\
QPEL16_OP(mc11, MMX)\
QPEL16_OP(mc12, MMX)\
QPEL16_OP(mc13, MMX)\
QPEL16_OP(mc20, MMX)\
QPEL16_OP(mc21, MMX)\
QPEL16_OP(mc22, MMX)\
QPEL16_OP(mc23, MMX)\
QPEL16_OP(mc30, MMX)\
QPEL16_OP(mc31, MMX)\
QPEL16_OP(mc32, MMX)\
QPEL16_OP(mc33, MMX)

#if CONFIG_H264QPEL && ARCH_X86_32 && HAVE_YASM // ARCH_X86_64 implies sse2+
QPEL16(mmxext)
#endif
