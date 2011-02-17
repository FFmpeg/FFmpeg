/*
 * MMX optimized DSP utils
 * Copyright (c) 2007  Aurelien Jacobs <aurel@gnuage.org>
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

#ifndef AVCODEC_X86_DSPUTIL_MMX_H
#define AVCODEC_X86_DSPUTIL_MMX_H

#include <stdint.h>
#include "libavcodec/dsputil.h"
#include "libavutil/x86_cpu.h"

typedef struct { uint64_t a, b; } xmm_reg;

extern const uint64_t ff_bone;
extern const uint64_t ff_wtwo;

extern const uint64_t ff_pdw_80000000[2];

extern const xmm_reg  ff_pw_3;
extern const xmm_reg  ff_pw_4;
extern const xmm_reg  ff_pw_5;
extern const xmm_reg  ff_pw_8;
extern const uint64_t ff_pw_15;
extern const xmm_reg  ff_pw_16;
extern const xmm_reg  ff_pw_18;
extern const uint64_t ff_pw_20;
extern const xmm_reg  ff_pw_27;
extern const xmm_reg  ff_pw_28;
extern const xmm_reg  ff_pw_32;
extern const uint64_t ff_pw_42;
extern const uint64_t ff_pw_53;
extern const xmm_reg  ff_pw_63;
extern const xmm_reg  ff_pw_64;
extern const uint64_t ff_pw_96;
extern const uint64_t ff_pw_128;
extern const uint64_t ff_pw_255;

extern const xmm_reg  ff_pb_1;
extern const xmm_reg  ff_pb_3;
extern const uint64_t ff_pb_7;
extern const uint64_t ff_pb_1F;
extern const uint64_t ff_pb_3F;
extern const uint64_t ff_pb_81;
extern const xmm_reg  ff_pb_A1;
extern const xmm_reg  ff_pb_F8;
extern const uint64_t ff_pb_FC;
extern const xmm_reg  ff_pb_FE;

extern const double ff_pd_1[2];
extern const double ff_pd_2[2];

#define LOAD4(stride,in,a,b,c,d)\
    "movq 0*"#stride"+"#in", "#a"\n\t"\
    "movq 1*"#stride"+"#in", "#b"\n\t"\
    "movq 2*"#stride"+"#in", "#c"\n\t"\
    "movq 3*"#stride"+"#in", "#d"\n\t"

#define STORE4(stride,out,a,b,c,d)\
    "movq "#a", 0*"#stride"+"#out"\n\t"\
    "movq "#b", 1*"#stride"+"#out"\n\t"\
    "movq "#c", 2*"#stride"+"#out"\n\t"\
    "movq "#d", 3*"#stride"+"#out"\n\t"

/* in/out: mma=mma+mmb, mmb=mmb-mma */
#define SUMSUB_BA( a, b ) \
    "paddw "#b", "#a" \n\t"\
    "paddw "#b", "#b" \n\t"\
    "psubw "#a", "#b" \n\t"

#define SBUTTERFLY(a,b,t,n,m)\
    "mov" #m " " #a ", " #t "         \n\t" /* abcd */\
    "punpckl" #n " " #b ", " #a "     \n\t" /* aebf */\
    "punpckh" #n " " #b ", " #t "     \n\t" /* cgdh */\

#define TRANSPOSE4(a,b,c,d,t)\
    SBUTTERFLY(a,b,t,wd,q) /* a=aebf t=cgdh */\
    SBUTTERFLY(c,d,b,wd,q) /* c=imjn b=kolp */\
    SBUTTERFLY(a,c,d,dq,q) /* a=aeim d=bfjn */\
    SBUTTERFLY(t,b,c,dq,q) /* t=cgko c=dhlp */

static inline void transpose4x4(uint8_t *dst, uint8_t *src, x86_reg dst_stride, x86_reg src_stride){
    __asm__ volatile( //FIXME could save 1 instruction if done as 8x4 ...
        "movd  (%1), %%mm0              \n\t"
        "add   %3, %1                   \n\t"
        "movd  (%1), %%mm1              \n\t"
        "movd  (%1,%3,1), %%mm2         \n\t"
        "movd  (%1,%3,2), %%mm3         \n\t"
        "punpcklbw %%mm1, %%mm0         \n\t"
        "punpcklbw %%mm3, %%mm2         \n\t"
        "movq %%mm0, %%mm1              \n\t"
        "punpcklwd %%mm2, %%mm0         \n\t"
        "punpckhwd %%mm2, %%mm1         \n\t"
        "movd  %%mm0, (%0)              \n\t"
        "add   %2, %0                   \n\t"
        "punpckhdq %%mm0, %%mm0         \n\t"
        "movd  %%mm0, (%0)              \n\t"
        "movd  %%mm1, (%0,%2,1)         \n\t"
        "punpckhdq %%mm1, %%mm1         \n\t"
        "movd  %%mm1, (%0,%2,2)         \n\t"

        :  "+&r" (dst),
           "+&r" (src)
        :  "r" (dst_stride),
           "r" (src_stride)
        :  "memory"
    );
}

// e,f,g,h can be memory
// out: a,d,t,c
#define TRANSPOSE8x4(a,b,c,d,e,f,g,h,t)\
    "punpcklbw " #e ", " #a " \n\t" /* a0 e0 a1 e1 a2 e2 a3 e3 */\
    "punpcklbw " #f ", " #b " \n\t" /* b0 f0 b1 f1 b2 f2 b3 f3 */\
    "punpcklbw " #g ", " #c " \n\t" /* c0 g0 c1 g1 c2 g2 d3 g3 */\
    "punpcklbw " #h ", " #d " \n\t" /* d0 h0 d1 h1 d2 h2 d3 h3 */\
    SBUTTERFLY(a, b, t, bw, q)   /* a= a0 b0 e0 f0 a1 b1 e1 f1 */\
                                 /* t= a2 b2 e2 f2 a3 b3 e3 f3 */\
    SBUTTERFLY(c, d, b, bw, q)   /* c= c0 d0 g0 h0 c1 d1 g1 h1 */\
                                 /* b= c2 d2 g2 h2 c3 d3 g3 h3 */\
    SBUTTERFLY(a, c, d, wd, q)   /* a= a0 b0 c0 d0 e0 f0 g0 h0 */\
                                 /* d= a1 b1 c1 d1 e1 f1 g1 h1 */\
    SBUTTERFLY(t, b, c, wd, q)   /* t= a2 b2 c2 d2 e2 f2 g2 h2 */\
                                 /* c= a3 b3 c3 d3 e3 f3 g3 h3 */

#if ARCH_X86_64
// permutes 01234567 -> 05736421
#define TRANSPOSE8(a,b,c,d,e,f,g,h,t)\
    SBUTTERFLY(a,b,%%xmm8,wd,dqa)\
    SBUTTERFLY(c,d,b,wd,dqa)\
    SBUTTERFLY(e,f,d,wd,dqa)\
    SBUTTERFLY(g,h,f,wd,dqa)\
    SBUTTERFLY(a,c,h,dq,dqa)\
    SBUTTERFLY(%%xmm8,b,c,dq,dqa)\
    SBUTTERFLY(e,g,b,dq,dqa)\
    SBUTTERFLY(d,f,g,dq,dqa)\
    SBUTTERFLY(a,e,f,qdq,dqa)\
    SBUTTERFLY(%%xmm8,d,e,qdq,dqa)\
    SBUTTERFLY(h,b,d,qdq,dqa)\
    SBUTTERFLY(c,g,b,qdq,dqa)\
    "movdqa %%xmm8, "#g"              \n\t"
#else
#define TRANSPOSE8(a,b,c,d,e,f,g,h,t)\
    "movdqa "#h", "#t"                \n\t"\
    SBUTTERFLY(a,b,h,wd,dqa)\
    "movdqa "#h", 16"#t"              \n\t"\
    "movdqa "#t", "#h"                \n\t"\
    SBUTTERFLY(c,d,b,wd,dqa)\
    SBUTTERFLY(e,f,d,wd,dqa)\
    SBUTTERFLY(g,h,f,wd,dqa)\
    SBUTTERFLY(a,c,h,dq,dqa)\
    "movdqa "#h", "#t"                \n\t"\
    "movdqa 16"#t", "#h"              \n\t"\
    SBUTTERFLY(h,b,c,dq,dqa)\
    SBUTTERFLY(e,g,b,dq,dqa)\
    SBUTTERFLY(d,f,g,dq,dqa)\
    SBUTTERFLY(a,e,f,qdq,dqa)\
    SBUTTERFLY(h,d,e,qdq,dqa)\
    "movdqa "#h", 16"#t"              \n\t"\
    "movdqa "#t", "#h"                \n\t"\
    SBUTTERFLY(h,b,d,qdq,dqa)\
    SBUTTERFLY(c,g,b,qdq,dqa)\
    "movdqa 16"#t", "#g"              \n\t"
#endif

#define MOVQ_WONE(regd) \
    __asm__ volatile ( \
    "pcmpeqd %%" #regd ", %%" #regd " \n\t" \
    "psrlw $15, %%" #regd ::)

void dsputilenc_init_mmx(DSPContext* c, AVCodecContext *avctx);
void dsputil_init_pix_mmx(DSPContext* c, AVCodecContext *avctx);

void ff_add_pixels_clamped_mmx(const DCTELEM *block, uint8_t *pixels, int line_size);
void ff_put_pixels_clamped_mmx(const DCTELEM *block, uint8_t *pixels, int line_size);
void ff_put_signed_pixels_clamped_mmx(const DCTELEM *block, uint8_t *pixels, int line_size);

void ff_put_cavs_qpel8_mc00_mmx2(uint8_t *dst, uint8_t *src, int stride);
void ff_avg_cavs_qpel8_mc00_mmx2(uint8_t *dst, uint8_t *src, int stride);
void ff_put_cavs_qpel16_mc00_mmx2(uint8_t *dst, uint8_t *src, int stride);
void ff_avg_cavs_qpel16_mc00_mmx2(uint8_t *dst, uint8_t *src, int stride);

void ff_put_vc1_mspel_mc00_mmx(uint8_t *dst, const uint8_t *src, int stride, int rnd);
void ff_avg_vc1_mspel_mc00_mmx2(uint8_t *dst, const uint8_t *src, int stride, int rnd);

void ff_mmx_idct(DCTELEM *block);
void ff_mmxext_idct(DCTELEM *block);


void ff_deinterlace_line_mmx(uint8_t *dst,
                             const uint8_t *lum_m4, const uint8_t *lum_m3,
                             const uint8_t *lum_m2, const uint8_t *lum_m1,
                             const uint8_t *lum,
                             int size);

void ff_deinterlace_line_inplace_mmx(const uint8_t *lum_m4,
                                     const uint8_t *lum_m3,
                                     const uint8_t *lum_m2,
                                     const uint8_t *lum_m1,
                                     const uint8_t *lum, int size);

#endif /* AVCODEC_X86_DSPUTIL_MMX_H */
