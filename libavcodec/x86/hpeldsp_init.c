/*
 * MMX optimized DSP utils
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
 *
 * MMX optimization by Nick Kurshev <nickols_k@mail.ru>
 */

#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavcodec/hpeldsp.h"
#include "dsputil_mmx.h"

void ff_put_pixels8_x2_mmxext(uint8_t *block, const uint8_t *pixels,
                              ptrdiff_t line_size, int h);
void ff_put_pixels8_x2_3dnow(uint8_t *block, const uint8_t *pixels,
                             ptrdiff_t line_size, int h);
void ff_put_pixels16_x2_mmxext(uint8_t *block, const uint8_t *pixels,
                               ptrdiff_t line_size, int h);
void ff_put_pixels16_x2_3dnow(uint8_t *block, const uint8_t *pixels,
                              ptrdiff_t line_size, int h);
void ff_put_no_rnd_pixels8_x2_mmxext(uint8_t *block, const uint8_t *pixels,
                                     ptrdiff_t line_size, int h);
void ff_put_no_rnd_pixels8_x2_3dnow(uint8_t *block, const uint8_t *pixels,
                                    ptrdiff_t line_size, int h);
void ff_put_no_rnd_pixels8_x2_exact_mmxext(uint8_t *block,
                                           const uint8_t *pixels,
                                           ptrdiff_t line_size, int h);
void ff_put_no_rnd_pixels8_x2_exact_3dnow(uint8_t *block,
                                          const uint8_t *pixels,
                                          ptrdiff_t line_size, int h);
void ff_put_pixels8_y2_mmxext(uint8_t *block, const uint8_t *pixels,
                              ptrdiff_t line_size, int h);
void ff_put_pixels8_y2_3dnow(uint8_t *block, const uint8_t *pixels,
                             ptrdiff_t line_size, int h);
void ff_put_no_rnd_pixels8_y2_mmxext(uint8_t *block, const uint8_t *pixels,
                                     ptrdiff_t line_size, int h);
void ff_put_no_rnd_pixels8_y2_3dnow(uint8_t *block, const uint8_t *pixels,
                                    ptrdiff_t line_size, int h);
void ff_put_no_rnd_pixels8_y2_exact_mmxext(uint8_t *block,
                                           const uint8_t *pixels,
                                           ptrdiff_t line_size, int h);
void ff_put_no_rnd_pixels8_y2_exact_3dnow(uint8_t *block,
                                          const uint8_t *pixels,
                                          ptrdiff_t line_size, int h);
void ff_avg_pixels8_3dnow(uint8_t *block, const uint8_t *pixels,
                          ptrdiff_t line_size, int h);
void ff_avg_pixels8_x2_mmxext(uint8_t *block, const uint8_t *pixels,
                              ptrdiff_t line_size, int h);
void ff_avg_pixels8_x2_3dnow(uint8_t *block, const uint8_t *pixels,
                             ptrdiff_t line_size, int h);
void ff_avg_pixels8_y2_mmxext(uint8_t *block, const uint8_t *pixels,
                              ptrdiff_t line_size, int h);
void ff_avg_pixels8_y2_3dnow(uint8_t *block, const uint8_t *pixels,
                             ptrdiff_t line_size, int h);
void ff_avg_pixels8_xy2_mmxext(uint8_t *block, const uint8_t *pixels,
                               ptrdiff_t line_size, int h);
void ff_avg_pixels8_xy2_3dnow(uint8_t *block, const uint8_t *pixels,
                              ptrdiff_t line_size, int h);


#if HAVE_INLINE_ASM

/***********************************/
/* MMX no rounding */
#define NO_RND 1
#define DEF(x, y) x ## _no_rnd_ ## y ## _mmx
#define SET_RND  MOVQ_WONE
#define PAVGBP(a, b, c, d, e, f)        PAVGBP_MMX_NO_RND(a, b, c, d, e, f)
#define PAVGB(a, b, c, e)               PAVGB_MMX_NO_RND(a, b, c, e)
#define OP_AVG(a, b, c, e)              PAVGB_MMX(a, b, c, e)

#include "hpeldsp_rnd_template.c"

#undef DEF
#undef SET_RND
#undef PAVGBP
#undef PAVGB
#undef NO_RND
/***********************************/
/* MMX rounding */

#define DEF(x, y) x ## _ ## y ## _mmx
#define SET_RND  MOVQ_WTWO
#define PAVGBP(a, b, c, d, e, f)        PAVGBP_MMX(a, b, c, d, e, f)
#define PAVGB(a, b, c, e)               PAVGB_MMX(a, b, c, e)

#include "hpeldsp_rnd_template.c"

#undef DEF
#undef SET_RND
#undef PAVGBP
#undef PAVGB
#undef OP_AVG

#endif /* HAVE_INLINE_ASM */


#if HAVE_YASM
#define ff_put_pixels8_mmx ff_put_pixels8_mmxext

/***********************************/
/* 3Dnow specific */

#define DEF(x) x ## _3dnow

#include "hpeldsp_avg_template.c"

#undef DEF

/***********************************/
/* MMXEXT specific */

#define DEF(x) x ## _mmxext

#include "hpeldsp_avg_template.c"

#undef DEF

#endif /* HAVE_YASM */


#if HAVE_INLINE_ASM
#define put_no_rnd_pixels16_mmx put_pixels16_mmx
#define put_no_rnd_pixels8_mmx put_pixels8_mmx
#define put_pixels16_mmxext put_pixels16_mmx
#define put_pixels8_mmxext put_pixels8_mmx
#define put_pixels4_mmxext put_pixels4_mmx
#define put_no_rnd_pixels16_mmxext put_no_rnd_pixels16_mmx
#define put_no_rnd_pixels8_mmxext put_no_rnd_pixels8_mmx

static void put_pixels8_mmx(uint8_t *block, const uint8_t *pixels,
                            ptrdiff_t line_size, int h)
{
    __asm__ volatile (
        "lea   (%3, %3), %%"REG_a"      \n\t"
        ".p2align     3                 \n\t"
        "1:                             \n\t"
        "movq  (%1    ), %%mm0          \n\t"
        "movq  (%1, %3), %%mm1          \n\t"
        "movq     %%mm0, (%2)           \n\t"
        "movq     %%mm1, (%2, %3)       \n\t"
        "add  %%"REG_a", %1             \n\t"
        "add  %%"REG_a", %2             \n\t"
        "movq  (%1    ), %%mm0          \n\t"
        "movq  (%1, %3), %%mm1          \n\t"
        "movq     %%mm0, (%2)           \n\t"
        "movq     %%mm1, (%2, %3)       \n\t"
        "add  %%"REG_a", %1             \n\t"
        "add  %%"REG_a", %2             \n\t"
        "subl        $4, %0             \n\t"
        "jnz         1b                 \n\t"
        : "+g"(h), "+r"(pixels),  "+r"(block)
        : "r"((x86_reg)line_size)
        : "%"REG_a, "memory"
        );
}

static void put_pixels16_mmx(uint8_t *block, const uint8_t *pixels,
                             ptrdiff_t line_size, int h)
{
    __asm__ volatile (
        "lea   (%3, %3), %%"REG_a"      \n\t"
        ".p2align     3                 \n\t"
        "1:                             \n\t"
        "movq  (%1    ), %%mm0          \n\t"
        "movq 8(%1    ), %%mm4          \n\t"
        "movq  (%1, %3), %%mm1          \n\t"
        "movq 8(%1, %3), %%mm5          \n\t"
        "movq     %%mm0,  (%2)          \n\t"
        "movq     %%mm4, 8(%2)          \n\t"
        "movq     %%mm1,  (%2, %3)      \n\t"
        "movq     %%mm5, 8(%2, %3)      \n\t"
        "add  %%"REG_a", %1             \n\t"
        "add  %%"REG_a", %2             \n\t"
        "movq  (%1    ), %%mm0          \n\t"
        "movq 8(%1    ), %%mm4          \n\t"
        "movq  (%1, %3), %%mm1          \n\t"
        "movq 8(%1, %3), %%mm5          \n\t"
        "movq     %%mm0,  (%2)          \n\t"
        "movq     %%mm4, 8(%2)          \n\t"
        "movq     %%mm1,  (%2, %3)      \n\t"
        "movq     %%mm5, 8(%2, %3)      \n\t"
        "add  %%"REG_a", %1             \n\t"
        "add  %%"REG_a", %2             \n\t"
        "subl        $4, %0             \n\t"
        "jnz         1b                 \n\t"
        : "+g"(h), "+r"(pixels),  "+r"(block)
        : "r"((x86_reg)line_size)
        : "%"REG_a, "memory"
        );
}
#endif /* HAVE_INLINE_ASM */

#define SET_HPEL_FUNCS(PFX, IDX, SIZE, CPU)                                     \
    do {                                                                        \
        c->PFX ## _pixels_tab IDX [0] = PFX ## _pixels ## SIZE ## _     ## CPU; \
        c->PFX ## _pixels_tab IDX [1] = PFX ## _pixels ## SIZE ## _x2_  ## CPU; \
        c->PFX ## _pixels_tab IDX [2] = PFX ## _pixels ## SIZE ## _y2_  ## CPU; \
        c->PFX ## _pixels_tab IDX [3] = PFX ## _pixels ## SIZE ## _xy2_ ## CPU; \
    } while (0)

static void hpeldsp_init_mmx(HpelDSPContext *c, int flags, int mm_flags)
{
#if HAVE_INLINE_ASM
    SET_HPEL_FUNCS(put,        [0], 16, mmx);
    SET_HPEL_FUNCS(put_no_rnd, [0], 16, mmx);
    SET_HPEL_FUNCS(avg,        [0], 16, mmx);
    SET_HPEL_FUNCS(avg_no_rnd,    , 16, mmx);
    SET_HPEL_FUNCS(put,        [1],  8, mmx);
    SET_HPEL_FUNCS(put_no_rnd, [1],  8, mmx);
    SET_HPEL_FUNCS(avg,        [1],  8, mmx);
#endif /* HAVE_INLINE_ASM */
}

static void hpeldsp_init_mmxext(HpelDSPContext *c, int flags, int mm_flags)
{
#if HAVE_YASM
    c->put_pixels_tab[0][1] = ff_put_pixels16_x2_mmxext;
    c->put_pixels_tab[0][2] = ff_put_pixels16_y2_mmxext;

    c->avg_pixels_tab[0][0] = ff_avg_pixels16_mmxext;
    c->avg_pixels_tab[0][1] = ff_avg_pixels16_x2_mmxext;
    c->avg_pixels_tab[0][2] = ff_avg_pixels16_y2_mmxext;

    c->put_pixels_tab[1][1] = ff_put_pixels8_x2_mmxext;
    c->put_pixels_tab[1][2] = ff_put_pixels8_y2_mmxext;

    c->avg_pixels_tab[1][0] = ff_avg_pixels8_mmxext;
    c->avg_pixels_tab[1][1] = ff_avg_pixels8_x2_mmxext;
    c->avg_pixels_tab[1][2] = ff_avg_pixels8_y2_mmxext;

    if (!(flags & CODEC_FLAG_BITEXACT)) {
        c->put_no_rnd_pixels_tab[0][1] = ff_put_no_rnd_pixels16_x2_mmxext;
        c->put_no_rnd_pixels_tab[0][2] = ff_put_no_rnd_pixels16_y2_mmxext;
        c->put_no_rnd_pixels_tab[1][1] = ff_put_no_rnd_pixels8_x2_mmxext;
        c->put_no_rnd_pixels_tab[1][2] = ff_put_no_rnd_pixels8_y2_mmxext;

        c->avg_pixels_tab[0][3] = ff_avg_pixels16_xy2_mmxext;
        c->avg_pixels_tab[1][3] = ff_avg_pixels8_xy2_mmxext;
    }

    if (flags & CODEC_FLAG_BITEXACT && CONFIG_VP3_DECODER) {
        c->put_no_rnd_pixels_tab[1][1] = ff_put_no_rnd_pixels8_x2_exact_mmxext;
        c->put_no_rnd_pixels_tab[1][2] = ff_put_no_rnd_pixels8_y2_exact_mmxext;
    }
#endif /* HAVE_YASM */
}

static void hpeldsp_init_3dnow(HpelDSPContext *c, int flags, int mm_flags)
{
#if HAVE_YASM
    c->put_pixels_tab[0][1] = ff_put_pixels16_x2_3dnow;
    c->put_pixels_tab[0][2] = ff_put_pixels16_y2_3dnow;

    c->avg_pixels_tab[0][0] = ff_avg_pixels16_3dnow;
    c->avg_pixels_tab[0][1] = ff_avg_pixels16_x2_3dnow;
    c->avg_pixels_tab[0][2] = ff_avg_pixels16_y2_3dnow;

    c->put_pixels_tab[1][1] = ff_put_pixels8_x2_3dnow;
    c->put_pixels_tab[1][2] = ff_put_pixels8_y2_3dnow;

    c->avg_pixels_tab[1][0] = ff_avg_pixels8_3dnow;
    c->avg_pixels_tab[1][1] = ff_avg_pixels8_x2_3dnow;
    c->avg_pixels_tab[1][2] = ff_avg_pixels8_y2_3dnow;

    if (!(flags & CODEC_FLAG_BITEXACT)){
        c->put_no_rnd_pixels_tab[0][1] = ff_put_no_rnd_pixels16_x2_3dnow;
        c->put_no_rnd_pixels_tab[0][2] = ff_put_no_rnd_pixels16_y2_3dnow;
        c->put_no_rnd_pixels_tab[1][1] = ff_put_no_rnd_pixels8_x2_3dnow;
        c->put_no_rnd_pixels_tab[1][2] = ff_put_no_rnd_pixels8_y2_3dnow;

        c->avg_pixels_tab[0][3] = ff_avg_pixels16_xy2_3dnow;
        c->avg_pixels_tab[1][3] = ff_avg_pixels8_xy2_3dnow;
    }

    if (flags & CODEC_FLAG_BITEXACT && CONFIG_VP3_DECODER) {
        c->put_no_rnd_pixels_tab[1][1] = ff_put_no_rnd_pixels8_x2_exact_3dnow;
        c->put_no_rnd_pixels_tab[1][2] = ff_put_no_rnd_pixels8_y2_exact_3dnow;
    }
#endif /* HAVE_YASM */
}

static void hpeldsp_init_sse2(HpelDSPContext *c, int flags, int mm_flags)
{
#if HAVE_YASM
    if (!(mm_flags & AV_CPU_FLAG_SSE2SLOW)) {
        // these functions are slower than mmx on AMD, but faster on Intel
        c->put_pixels_tab[0][0]        = ff_put_pixels16_sse2;
        c->put_no_rnd_pixels_tab[0][0] = ff_put_pixels16_sse2;
        c->avg_pixels_tab[0][0]        = ff_avg_pixels16_sse2;
    }
#endif /* HAVE_YASM */
}

void ff_hpeldsp_init_x86(HpelDSPContext *c, int flags)
{
    int mm_flags = av_get_cpu_flags();

    if (HAVE_MMX && mm_flags & AV_CPU_FLAG_MMX)
        hpeldsp_init_mmx(c, flags, mm_flags);

    if (mm_flags & AV_CPU_FLAG_MMXEXT)
        hpeldsp_init_mmxext(c, flags, mm_flags);

    if (mm_flags & AV_CPU_FLAG_3DNOW)
        hpeldsp_init_3dnow(c, flags, mm_flags);

    if (mm_flags & AV_CPU_FLAG_SSE2)
        hpeldsp_init_sse2(c, flags, mm_flags);
}
