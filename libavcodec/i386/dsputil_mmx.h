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

#ifndef FFMPEG_DSPUTIL_MMX_H
#define FFMPEG_DSPUTIL_MMX_H

#include <stdint.h>
#include "dsputil.h"

typedef struct { uint64_t a, b; } xmm_t;

extern const uint64_t ff_bone;
extern const uint64_t ff_wtwo;

extern const uint64_t ff_pdw_80000000[2];

extern const uint64_t ff_pw_3;
extern const uint64_t ff_pw_4;
extern const xmm_t    ff_pw_5;
extern const uint64_t ff_pw_8;
extern const uint64_t ff_pw_15;
extern const xmm_t    ff_pw_16;
extern const uint64_t ff_pw_20;
extern const xmm_t    ff_pw_28;
extern const xmm_t    ff_pw_32;
extern const uint64_t ff_pw_42;
extern const uint64_t ff_pw_64;
extern const uint64_t ff_pw_96;
extern const uint64_t ff_pw_128;
extern const uint64_t ff_pw_255;

extern const uint64_t ff_pb_1;
extern const uint64_t ff_pb_3;
extern const uint64_t ff_pb_7;
extern const uint64_t ff_pb_3F;
extern const uint64_t ff_pb_A1;
extern const uint64_t ff_pb_FC;

extern const double ff_pd_1[2];
extern const double ff_pd_2[2];

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

#ifdef ARCH_X86_64
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
    asm volatile ( \
    "pcmpeqd %%" #regd ", %%" #regd " \n\t" \
    "psrlw $15, %%" #regd ::)

void dsputilenc_init_mmx(DSPContext* c, AVCodecContext *avctx);

#endif /* FFMPEG_DSPUTIL_MMX_H */
