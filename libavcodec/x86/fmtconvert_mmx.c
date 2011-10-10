/*
 * Format Conversion Utils
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * MMX optimization by Nick Kurshev <nickols_k@mail.ru>
 */

#include "libavutil/cpu.h"
#include "libavutil/x86_cpu.h"
#include "libavcodec/fmtconvert.h"

#if HAVE_YASM

void ff_int32_to_float_fmul_scalar_sse (float *dst, const int *src, float mul, int len);
void ff_int32_to_float_fmul_scalar_sse2(float *dst, const int *src, float mul, int len);

void ff_float_to_int16_3dnow(int16_t *dst, const float *src, long len);
void ff_float_to_int16_sse  (int16_t *dst, const float *src, long len);
void ff_float_to_int16_sse2 (int16_t *dst, const float *src, long len);

void ff_float_to_int16_interleave6_sse(int16_t *dst, const float **src, int len);
void ff_float_to_int16_interleave6_3dnow(int16_t *dst, const float **src, int len);
void ff_float_to_int16_interleave6_3dn2(int16_t *dst, const float **src, int len);

#define ff_float_to_int16_interleave6_sse2 ff_float_to_int16_interleave6_sse

#define FLOAT_TO_INT16_INTERLEAVE(cpu, body) \
/* gcc pessimizes register allocation if this is in the same function as float_to_int16_interleave_sse2*/\
static av_noinline void float_to_int16_interleave_misc_##cpu(int16_t *dst, const float **src, long len, int channels){\
    DECLARE_ALIGNED(16, int16_t, tmp)[len];\
    int i,j,c;\
    for(c=0; c<channels; c++){\
        ff_float_to_int16_##cpu(tmp, src[c], len);\
        for(i=0, j=c; i<len; i++, j+=channels)\
            dst[j] = tmp[i];\
    }\
}\
\
static void float_to_int16_interleave_##cpu(int16_t *dst, const float **src, long len, int channels){\
    if(channels==1)\
        ff_float_to_int16_##cpu(dst, src[0], len);\
    else if(channels==2){\
        x86_reg reglen = len; \
        const float *src0 = src[0];\
        const float *src1 = src[1];\
        __asm__ volatile(\
            "shl $2, %0 \n"\
            "add %0, %1 \n"\
            "add %0, %2 \n"\
            "add %0, %3 \n"\
            "neg %0 \n"\
            body\
            :"+r"(reglen), "+r"(dst), "+r"(src0), "+r"(src1)\
        );\
    }else if(channels==6){\
        ff_float_to_int16_interleave6_##cpu(dst, src, len);\
    }else\
        float_to_int16_interleave_misc_##cpu(dst, src, len, channels);\
}

FLOAT_TO_INT16_INTERLEAVE(3dnow,
    "1:                         \n"
    "pf2id     (%2,%0), %%mm0   \n"
    "pf2id    8(%2,%0), %%mm1   \n"
    "pf2id     (%3,%0), %%mm2   \n"
    "pf2id    8(%3,%0), %%mm3   \n"
    "packssdw    %%mm1, %%mm0   \n"
    "packssdw    %%mm3, %%mm2   \n"
    "movq        %%mm0, %%mm1   \n"
    "punpcklwd   %%mm2, %%mm0   \n"
    "punpckhwd   %%mm2, %%mm1   \n"
    "movq        %%mm0,  (%1,%0)\n"
    "movq        %%mm1, 8(%1,%0)\n"
    "add $16, %0                \n"
    "js 1b                      \n"
    "femms                      \n"
)

FLOAT_TO_INT16_INTERLEAVE(sse,
    "1:                         \n"
    "cvtps2pi  (%2,%0), %%mm0   \n"
    "cvtps2pi 8(%2,%0), %%mm1   \n"
    "cvtps2pi  (%3,%0), %%mm2   \n"
    "cvtps2pi 8(%3,%0), %%mm3   \n"
    "packssdw    %%mm1, %%mm0   \n"
    "packssdw    %%mm3, %%mm2   \n"
    "movq        %%mm0, %%mm1   \n"
    "punpcklwd   %%mm2, %%mm0   \n"
    "punpckhwd   %%mm2, %%mm1   \n"
    "movq        %%mm0,  (%1,%0)\n"
    "movq        %%mm1, 8(%1,%0)\n"
    "add $16, %0                \n"
    "js 1b                      \n"
    "emms                       \n"
)

FLOAT_TO_INT16_INTERLEAVE(sse2,
    "1:                         \n"
    "cvtps2dq  (%2,%0), %%xmm0  \n"
    "cvtps2dq  (%3,%0), %%xmm1  \n"
    "packssdw   %%xmm1, %%xmm0  \n"
    "movhlps    %%xmm0, %%xmm1  \n"
    "punpcklwd  %%xmm1, %%xmm0  \n"
    "movdqa     %%xmm0, (%1,%0) \n"
    "add $16, %0                \n"
    "js 1b                      \n"
)

static void float_to_int16_interleave_3dn2(int16_t *dst, const float **src, long len, int channels){
    if(channels==6)
        ff_float_to_int16_interleave6_3dn2(dst, src, len);
    else
        float_to_int16_interleave_3dnow(dst, src, len, channels);
}

void ff_float_interleave2_mmx(float *dst, const float **src, unsigned int len);
void ff_float_interleave2_sse(float *dst, const float **src, unsigned int len);

void ff_float_interleave6_mmx(float *dst, const float **src, unsigned int len);
void ff_float_interleave6_sse(float *dst, const float **src, unsigned int len);

static void float_interleave_mmx(float *dst, const float **src,
                                 unsigned int len, int channels)
{
    if (channels == 2) {
        ff_float_interleave2_mmx(dst, src, len);
    } else if (channels == 6)
        ff_float_interleave6_mmx(dst, src, len);
    else
        ff_float_interleave_c(dst, src, len, channels);
}

static void float_interleave_sse(float *dst, const float **src,
                                 unsigned int len, int channels)
{
    if (channels == 2) {
        ff_float_interleave2_sse(dst, src, len);
    } else if (channels == 6)
        ff_float_interleave6_sse(dst, src, len);
    else
        ff_float_interleave_c(dst, src, len, channels);
}
#endif

void ff_fmt_convert_init_x86(FmtConvertContext *c, AVCodecContext *avctx)
{
    int mm_flags = av_get_cpu_flags();

#if HAVE_YASM
    if (mm_flags & AV_CPU_FLAG_MMX) {
        c->float_interleave = float_interleave_mmx;

        if (HAVE_AMD3DNOW && mm_flags & AV_CPU_FLAG_3DNOW) {
            if(!(avctx->flags & CODEC_FLAG_BITEXACT)){
                c->float_to_int16 = ff_float_to_int16_3dnow;
                c->float_to_int16_interleave = float_to_int16_interleave_3dnow;
            }
        }
        if (HAVE_AMD3DNOWEXT && mm_flags & AV_CPU_FLAG_3DNOWEXT) {
            if(!(avctx->flags & CODEC_FLAG_BITEXACT)){
                c->float_to_int16_interleave = float_to_int16_interleave_3dn2;
            }
        }
        if (HAVE_SSE && mm_flags & AV_CPU_FLAG_SSE) {
            c->int32_to_float_fmul_scalar = ff_int32_to_float_fmul_scalar_sse;
            c->float_to_int16 = ff_float_to_int16_sse;
            c->float_to_int16_interleave = float_to_int16_interleave_sse;
            c->float_interleave = float_interleave_sse;
        }
        if (HAVE_SSE && mm_flags & AV_CPU_FLAG_SSE2) {
            c->int32_to_float_fmul_scalar = ff_int32_to_float_fmul_scalar_sse2;
            c->float_to_int16 = ff_float_to_int16_sse2;
            c->float_to_int16_interleave = float_to_int16_interleave_sse2;
        }
    }
#endif
}
