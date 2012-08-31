/*
 * Copyright (C) 2009 Loren Merritt <lorenm@u.washignton.edu>
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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/mem.h"
#include "libavutil/x86/asm.h"
#include "libavfilter/gradfun.h"

#if HAVE_INLINE_ASM

DECLARE_ALIGNED(16, static const uint16_t, pw_7f)[8] = {0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F};
DECLARE_ALIGNED(16, static const uint16_t, pw_ff)[8] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

#if HAVE_MMXEXT_INLINE
static void gradfun_filter_line_mmx2(uint8_t *dst, const uint8_t *src, const uint16_t *dc, int width, int thresh, const uint16_t *dithers)
{
    intptr_t x;
    if (width & 3) {
        x = width & ~3;
        ff_gradfun_filter_line_c(dst + x, src + x, dc + x / 2, width - x, thresh, dithers);
        width = x;
    }
    x = -width;
    __asm__ volatile(
        "movd          %4, %%mm5 \n"
        "pxor       %%mm7, %%mm7 \n"
        "pshufw $0, %%mm5, %%mm5 \n"
        "movq          %6, %%mm6 \n"
        "movq          %5, %%mm4 \n"
        "1: \n"
        "movd     (%2,%0), %%mm0 \n"
        "movd     (%3,%0), %%mm1 \n"
        "punpcklbw  %%mm7, %%mm0 \n"
        "punpcklwd  %%mm1, %%mm1 \n"
        "psllw         $7, %%mm0 \n"
        "pxor       %%mm2, %%mm2 \n"
        "psubw      %%mm0, %%mm1 \n" // delta = dc - pix
        "psubw      %%mm1, %%mm2 \n"
        "pmaxsw     %%mm1, %%mm2 \n"
        "pmulhuw    %%mm5, %%mm2 \n" // m = abs(delta) * thresh >> 16
        "psubw      %%mm6, %%mm2 \n"
        "pminsw     %%mm7, %%mm2 \n" // m = -max(0, 127-m)
        "pmullw     %%mm2, %%mm2 \n"
        "paddw      %%mm4, %%mm0 \n" // pix += dither
        "pmulhw     %%mm2, %%mm1 \n"
        "psllw         $2, %%mm1 \n" // m = m*m*delta >> 14
        "paddw      %%mm1, %%mm0 \n" // pix += m
        "psraw         $7, %%mm0 \n"
        "packuswb   %%mm0, %%mm0 \n"
        "movd       %%mm0, (%1,%0) \n" // dst = clip(pix>>7)
        "add           $4, %0 \n"
        "jl 1b \n"
        "emms \n"
        :"+r"(x)
        :"r"(dst+width), "r"(src+width), "r"(dc+width/2),
         "rm"(thresh), "m"(*dithers), "m"(*pw_7f)
        :"memory"
    );
}
#endif

#if HAVE_SSSE3_INLINE
static void gradfun_filter_line_ssse3(uint8_t *dst, const uint8_t *src, const uint16_t *dc, int width, int thresh, const uint16_t *dithers)
{
    intptr_t x;
    if (width & 7) {
        // could be 10% faster if I somehow eliminated this
        x = width & ~7;
        ff_gradfun_filter_line_c(dst + x, src + x, dc + x / 2, width - x, thresh, dithers);
        width = x;
    }
    x = -width;
    __asm__ volatile(
        "movd           %4, %%xmm5 \n"
        "pxor       %%xmm7, %%xmm7 \n"
        "pshuflw $0,%%xmm5, %%xmm5 \n"
        "movdqa         %6, %%xmm6 \n"
        "punpcklqdq %%xmm5, %%xmm5 \n"
        "movdqa         %5, %%xmm4 \n"
        "1: \n"
        "movq      (%2,%0), %%xmm0 \n"
        "movq      (%3,%0), %%xmm1 \n"
        "punpcklbw  %%xmm7, %%xmm0 \n"
        "punpcklwd  %%xmm1, %%xmm1 \n"
        "psllw          $7, %%xmm0 \n"
        "psubw      %%xmm0, %%xmm1 \n" // delta = dc - pix
        "pabsw      %%xmm1, %%xmm2 \n"
        "pmulhuw    %%xmm5, %%xmm2 \n" // m = abs(delta) * thresh >> 16
        "psubw      %%xmm6, %%xmm2 \n"
        "pminsw     %%xmm7, %%xmm2 \n" // m = -max(0, 127-m)
        "pmullw     %%xmm2, %%xmm2 \n"
        "psllw          $1, %%xmm2 \n"
        "paddw      %%xmm4, %%xmm0 \n" // pix += dither
        "pmulhrsw   %%xmm2, %%xmm1 \n" // m = m*m*delta >> 14
        "paddw      %%xmm1, %%xmm0 \n" // pix += m
        "psraw          $7, %%xmm0 \n"
        "packuswb   %%xmm0, %%xmm0 \n"
        "movq       %%xmm0, (%1,%0) \n" // dst = clip(pix>>7)
        "add            $8, %0 \n"
        "jl 1b \n"
        :"+&r"(x)
        :"r"(dst+width), "r"(src+width), "r"(dc+width/2),
         "rm"(thresh), "m"(*dithers), "m"(*pw_7f)
        :"memory"
    );
}
#endif /* HAVE_SSSE3_INLINE */

#if HAVE_SSE2_INLINE
static void gradfun_blur_line_sse2(uint16_t *dc, uint16_t *buf, const uint16_t *buf1, const uint8_t *src, int src_linesize, int width)
{
#define BLURV(load)\
    intptr_t x = -2*width;\
    __asm__ volatile(\
        "movdqa %6, %%xmm7 \n"\
        "1: \n"\
        load"   (%4,%0), %%xmm0 \n"\
        load"   (%5,%0), %%xmm1 \n"\
        "movdqa  %%xmm0, %%xmm2 \n"\
        "movdqa  %%xmm1, %%xmm3 \n"\
        "psrlw       $8, %%xmm0 \n"\
        "psrlw       $8, %%xmm1 \n"\
        "pand    %%xmm7, %%xmm2 \n"\
        "pand    %%xmm7, %%xmm3 \n"\
        "paddw   %%xmm1, %%xmm0 \n"\
        "paddw   %%xmm3, %%xmm2 \n"\
        "paddw   %%xmm2, %%xmm0 \n"\
        "paddw  (%2,%0), %%xmm0 \n"\
        "movdqa (%1,%0), %%xmm1 \n"\
        "movdqa  %%xmm0, (%1,%0) \n"\
        "psubw   %%xmm1, %%xmm0 \n"\
        "movdqa  %%xmm0, (%3,%0) \n"\
        "add        $16, %0 \n"\
        "jl 1b \n"\
        :"+&r"(x)\
        :"r"(buf+width),\
         "r"(buf1+width),\
         "r"(dc+width),\
         "r"(src+width*2),\
         "r"(src+width*2+src_linesize),\
         "m"(*pw_ff)\
        :"memory"\
    );
    if (((intptr_t) src | src_linesize) & 15) {
        BLURV("movdqu");
    } else {
        BLURV("movdqa");
    }
}
#endif /* HAVE_SSE2_INLINE */

#endif /* HAVE_INLINE_ASM */

av_cold void ff_gradfun_init_x86(GradFunContext *gf)
{
    int cpu_flags = av_get_cpu_flags();

#if HAVE_MMXEXT_INLINE
    if (cpu_flags & AV_CPU_FLAG_MMXEXT)
        gf->filter_line = gradfun_filter_line_mmx2;
#endif
#if HAVE_SSSE3_INLINE
    if (cpu_flags & AV_CPU_FLAG_SSSE3)
        gf->filter_line = gradfun_filter_line_ssse3;
#endif
#if HAVE_SSE2_INLINE
    if (cpu_flags & AV_CPU_FLAG_SSE2)
        gf->blur_line = gradfun_blur_line_sse2;
#endif
}
