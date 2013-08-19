/*
 * Copyright (c) 2009 David Conrad <lessen42@gmail.com>
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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/dsputil.h"
#include "libavcodec/vp3dsp.h"
#include "config.h"

void ff_vp3_idct_put_mmx(uint8_t *dest, int line_size, int16_t *block);
void ff_vp3_idct_add_mmx(uint8_t *dest, int line_size, int16_t *block);

void ff_vp3_idct_put_sse2(uint8_t *dest, int line_size, int16_t *block);
void ff_vp3_idct_add_sse2(uint8_t *dest, int line_size, int16_t *block);

void ff_vp3_idct_dc_add_mmxext(uint8_t *dest, int line_size,
                               int16_t *block);

void ff_vp3_v_loop_filter_mmxext(uint8_t *src, int stride,
                                 int *bounding_values);
void ff_vp3_h_loop_filter_mmxext(uint8_t *src, int stride,
                                 int *bounding_values);

#if HAVE_MMX_INLINE

#define MOVQ_BFE(regd)                                  \
    __asm__ volatile (                                  \
        "pcmpeqd %%"#regd", %%"#regd"   \n\t"           \
        "paddb   %%"#regd", %%"#regd"   \n\t" ::)

#define PAVGBP_MMX_NO_RND(rega, regb, regr,  regc, regd, regp)   \
    "movq  "#rega", "#regr"             \n\t"                    \
    "movq  "#regc", "#regp"             \n\t"                    \
    "pand  "#regb", "#regr"             \n\t"                    \
    "pand  "#regd", "#regp"             \n\t"                    \
    "pxor  "#rega", "#regb"             \n\t"                    \
    "pxor  "#regc", "#regd"             \n\t"                    \
    "pand    %%mm6, "#regb"             \n\t"                    \
    "pand    %%mm6, "#regd"             \n\t"                    \
    "psrlq      $1, "#regb"             \n\t"                    \
    "psrlq      $1, "#regd"             \n\t"                    \
    "paddb "#regb", "#regr"             \n\t"                    \
    "paddb "#regd", "#regp"             \n\t"

static void put_vp_no_rnd_pixels8_l2_mmx(uint8_t *dst, const uint8_t *a, const uint8_t *b, ptrdiff_t stride, int h)
{
//    START_TIMER
    MOVQ_BFE(mm6);
    __asm__ volatile(
        "1:                             \n\t"
        "movq   (%1), %%mm0             \n\t"
        "movq   (%2), %%mm1             \n\t"
        "movq   (%1,%4), %%mm2          \n\t"
        "movq   (%2,%4), %%mm3          \n\t"
        PAVGBP_MMX_NO_RND(%%mm0, %%mm1, %%mm4,   %%mm2, %%mm3, %%mm5)
        "movq   %%mm4, (%3)             \n\t"
        "movq   %%mm5, (%3,%4)          \n\t"

        "movq   (%1,%4,2), %%mm0        \n\t"
        "movq   (%2,%4,2), %%mm1        \n\t"
        "movq   (%1,%5), %%mm2          \n\t"
        "movq   (%2,%5), %%mm3          \n\t"
        "lea    (%1,%4,4), %1           \n\t"
        "lea    (%2,%4,4), %2           \n\t"
        PAVGBP_MMX_NO_RND(%%mm0, %%mm1, %%mm4,   %%mm2, %%mm3, %%mm5)
        "movq   %%mm4, (%3,%4,2)        \n\t"
        "movq   %%mm5, (%3,%5)          \n\t"
        "lea    (%3,%4,4), %3           \n\t"
        "subl   $4, %0                  \n\t"
        "jnz    1b                      \n\t"
        :"+r"(h), "+r"(a), "+r"(b), "+r"(dst)
        :"r"((x86_reg)stride), "r"((x86_reg)3L*stride)
        :"memory");
//    STOP_TIMER("put_vp_no_rnd_pixels8_l2_mmx")
}
#endif /* HAVE_MMX_INLINE */

av_cold void ff_vp3dsp_init_x86(VP3DSPContext *c, int flags)
{
    int cpu_flags = av_get_cpu_flags();

#if HAVE_MMX_INLINE
    c->put_no_rnd_pixels_l2 = put_vp_no_rnd_pixels8_l2_mmx;
#endif /* HAVE_MMX_INLINE */

#if ARCH_X86_32
    if (EXTERNAL_MMX(cpu_flags)) {
        c->idct_put  = ff_vp3_idct_put_mmx;
        c->idct_add  = ff_vp3_idct_add_mmx;
    }
#endif

    if (EXTERNAL_MMXEXT(cpu_flags)) {
        c->idct_dc_add = ff_vp3_idct_dc_add_mmxext;

        if (!(flags & CODEC_FLAG_BITEXACT)) {
            c->v_loop_filter = ff_vp3_v_loop_filter_mmxext;
            c->h_loop_filter = ff_vp3_h_loop_filter_mmxext;
        }
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->idct_put  = ff_vp3_idct_put_sse2;
        c->idct_add  = ff_vp3_idct_add_sse2;
    }
}
