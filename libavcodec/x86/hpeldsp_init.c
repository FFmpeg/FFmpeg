/*
 * SIMD-optimized halfpel functions
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

#include <stddef.h>
#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/hpeldsp.h"
#include "libavcodec/pixels.h"
#include "fpel.h"
#include "hpeldsp.h"

void ff_put_pixels8_x2_mmxext(uint8_t *block, const uint8_t *pixels,
                              ptrdiff_t line_size, int h);
void ff_put_pixels16_x2_mmxext(uint8_t *block, const uint8_t *pixels,
                               ptrdiff_t line_size, int h);
void ff_put_pixels16_x2_sse2(uint8_t *block, const uint8_t *pixels,
                             ptrdiff_t line_size, int h);
void ff_avg_pixels16_x2_sse2(uint8_t *block, const uint8_t *pixels,
                             ptrdiff_t line_size, int h);
void ff_put_pixels16_y2_sse2(uint8_t *block, const uint8_t *pixels,
                             ptrdiff_t line_size, int h);
void ff_avg_pixels16_y2_sse2(uint8_t *block, const uint8_t *pixels,
                             ptrdiff_t line_size, int h);
void ff_put_no_rnd_pixels8_x2_mmxext(uint8_t *block, const uint8_t *pixels,
                                     ptrdiff_t line_size, int h);
void ff_put_no_rnd_pixels8_x2_exact_mmxext(uint8_t *block,
                                           const uint8_t *pixels,
                                           ptrdiff_t line_size, int h);
void ff_put_pixels8_y2_mmxext(uint8_t *block, const uint8_t *pixels,
                              ptrdiff_t line_size, int h);
void ff_put_no_rnd_pixels8_y2_mmxext(uint8_t *block, const uint8_t *pixels,
                                     ptrdiff_t line_size, int h);
void ff_put_no_rnd_pixels8_y2_exact_mmxext(uint8_t *block,
                                           const uint8_t *pixels,
                                           ptrdiff_t line_size, int h);
void ff_avg_pixels8_x2_mmxext(uint8_t *block, const uint8_t *pixels,
                              ptrdiff_t line_size, int h);
void ff_avg_pixels8_y2_mmxext(uint8_t *block, const uint8_t *pixels,
                              ptrdiff_t line_size, int h);
void ff_avg_approx_pixels8_xy2_mmxext(uint8_t *block, const uint8_t *pixels,
                                      ptrdiff_t line_size, int h);

#define put_pixels8_mmx         ff_put_pixels8_mmx
#define put_pixels16_mmx        ff_put_pixels16_mmx
#define put_pixels8_xy2_mmx     ff_put_pixels8_xy2_mmx
#define put_no_rnd_pixels8_mmx  ff_put_pixels8_mmx
#define put_no_rnd_pixels16_mmx ff_put_pixels16_mmx

#if HAVE_INLINE_ASM

/***********************************/
/* MMX no rounding */
#define DEF(x, y) x ## _no_rnd_ ## y ## _mmx
#define SET_RND  MOVQ_WONE
#define STATIC static

#include "rnd_template.c"

#undef DEF
#undef SET_RND
#undef STATIC

// this routine is 'slightly' suboptimal but mostly unused
static void avg_no_rnd_pixels8_xy2_mmx(uint8_t *block, const uint8_t *pixels,
                                       ptrdiff_t line_size, int h)
{
    MOVQ_ZERO(mm7);
    MOVQ_WONE(mm6); // =2 for rnd  and  =1 for no_rnd version
    __asm__ volatile(
        "movq   (%1), %%mm0             \n\t"
        "movq   1(%1), %%mm4            \n\t"
        "movq   %%mm0, %%mm1            \n\t"
        "movq   %%mm4, %%mm5            \n\t"
        "punpcklbw %%mm7, %%mm0         \n\t"
        "punpcklbw %%mm7, %%mm4         \n\t"
        "punpckhbw %%mm7, %%mm1         \n\t"
        "punpckhbw %%mm7, %%mm5         \n\t"
        "paddusw %%mm0, %%mm4           \n\t"
        "paddusw %%mm1, %%mm5           \n\t"
        "xor    %%"FF_REG_a", %%"FF_REG_a" \n\t"
        "add    %3, %1                  \n\t"
        ".p2align 3                     \n\t"
        "1:                             \n\t"
        "movq   (%1, %%"FF_REG_a"), %%mm0  \n\t"
        "movq   1(%1, %%"FF_REG_a"), %%mm2 \n\t"
        "movq   %%mm0, %%mm1            \n\t"
        "movq   %%mm2, %%mm3            \n\t"
        "punpcklbw %%mm7, %%mm0         \n\t"
        "punpcklbw %%mm7, %%mm2         \n\t"
        "punpckhbw %%mm7, %%mm1         \n\t"
        "punpckhbw %%mm7, %%mm3         \n\t"
        "paddusw %%mm2, %%mm0           \n\t"
        "paddusw %%mm3, %%mm1           \n\t"
        "paddusw %%mm6, %%mm4           \n\t"
        "paddusw %%mm6, %%mm5           \n\t"
        "paddusw %%mm0, %%mm4           \n\t"
        "paddusw %%mm1, %%mm5           \n\t"
        "psrlw  $2, %%mm4               \n\t"
        "psrlw  $2, %%mm5               \n\t"
                "movq   (%2, %%"FF_REG_a"), %%mm3  \n\t"
        "packuswb  %%mm5, %%mm4         \n\t"
                "pcmpeqd %%mm2, %%mm2   \n\t"
                "paddb %%mm2, %%mm2     \n\t"
                PAVGB_MMX(%%mm3, %%mm4, %%mm5, %%mm2)
                "movq   %%mm5, (%2, %%"FF_REG_a")  \n\t"
        "add    %3, %%"FF_REG_a"        \n\t"

        "movq   (%1, %%"FF_REG_a"), %%mm2  \n\t" // 0 <-> 2   1 <-> 3
        "movq   1(%1, %%"FF_REG_a"), %%mm4 \n\t"
        "movq   %%mm2, %%mm3            \n\t"
        "movq   %%mm4, %%mm5            \n\t"
        "punpcklbw %%mm7, %%mm2         \n\t"
        "punpcklbw %%mm7, %%mm4         \n\t"
        "punpckhbw %%mm7, %%mm3         \n\t"
        "punpckhbw %%mm7, %%mm5         \n\t"
        "paddusw %%mm2, %%mm4           \n\t"
        "paddusw %%mm3, %%mm5           \n\t"
        "paddusw %%mm6, %%mm0           \n\t"
        "paddusw %%mm6, %%mm1           \n\t"
        "paddusw %%mm4, %%mm0           \n\t"
        "paddusw %%mm5, %%mm1           \n\t"
        "psrlw  $2, %%mm0               \n\t"
        "psrlw  $2, %%mm1               \n\t"
                "movq   (%2, %%"FF_REG_a"), %%mm3  \n\t"
        "packuswb  %%mm1, %%mm0         \n\t"
                "pcmpeqd %%mm2, %%mm2   \n\t"
                "paddb %%mm2, %%mm2     \n\t"
                PAVGB_MMX(%%mm3, %%mm0, %%mm1, %%mm2)
                "movq   %%mm1, (%2, %%"FF_REG_a")  \n\t"
        "add    %3, %%"FF_REG_a"           \n\t"

        "subl   $2, %0                  \n\t"
        "jnz    1b                      \n\t"
        :"+g"(h), "+S"(pixels)
        :"D"(block), "r"((x86_reg)line_size)
        :FF_REG_a, "memory");
}

static void put_no_rnd_pixels8_x2_mmx(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
{
    MOVQ_BFE(mm6);
    __asm__ volatile(
        "lea    (%3, %3), %%"FF_REG_a"  \n\t"
        ".p2align 3                     \n\t"
        "1:                             \n\t"
        "movq   (%1), %%mm0             \n\t"
        "movq   1(%1), %%mm1            \n\t"
        "movq   (%1, %3), %%mm2         \n\t"
        "movq   1(%1, %3), %%mm3        \n\t"
        PAVGBP_MMX_NO_RND(%%mm0, %%mm1, %%mm4,   %%mm2, %%mm3, %%mm5)
        "movq   %%mm4, (%2)             \n\t"
        "movq   %%mm5, (%2, %3)         \n\t"
        "add    %%"FF_REG_a", %1        \n\t"
        "add    %%"FF_REG_a", %2        \n\t"
        "movq   (%1), %%mm0             \n\t"
        "movq   1(%1), %%mm1            \n\t"
        "movq   (%1, %3), %%mm2         \n\t"
        "movq   1(%1, %3), %%mm3        \n\t"
        PAVGBP_MMX_NO_RND(%%mm0, %%mm1, %%mm4,   %%mm2, %%mm3, %%mm5)
        "movq   %%mm4, (%2)             \n\t"
        "movq   %%mm5, (%2, %3)         \n\t"
        "add    %%"FF_REG_a", %1        \n\t"
        "add    %%"FF_REG_a", %2        \n\t"
        "subl   $4, %0                  \n\t"
        "jnz    1b                      \n\t"
        :"+g"(h), "+S"(pixels), "+D"(block)
        :"r"((x86_reg)line_size)
        :FF_REG_a, "memory");
}

static void put_no_rnd_pixels16_x2_mmx(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
{
    MOVQ_BFE(mm6);
    __asm__ volatile(
        "lea    (%3, %3), %%"FF_REG_a"  \n\t"
        ".p2align 3                     \n\t"
        "1:                             \n\t"
        "movq   (%1), %%mm0             \n\t"
        "movq   1(%1), %%mm1            \n\t"
        "movq   (%1, %3), %%mm2         \n\t"
        "movq   1(%1, %3), %%mm3        \n\t"
        PAVGBP_MMX_NO_RND(%%mm0, %%mm1, %%mm4,   %%mm2, %%mm3, %%mm5)
        "movq   %%mm4, (%2)             \n\t"
        "movq   %%mm5, (%2, %3)         \n\t"
        "movq   8(%1), %%mm0            \n\t"
        "movq   9(%1), %%mm1            \n\t"
        "movq   8(%1, %3), %%mm2        \n\t"
        "movq   9(%1, %3), %%mm3        \n\t"
        PAVGBP_MMX_NO_RND(%%mm0, %%mm1, %%mm4,   %%mm2, %%mm3, %%mm5)
        "movq   %%mm4, 8(%2)            \n\t"
        "movq   %%mm5, 8(%2, %3)        \n\t"
        "add    %%"FF_REG_a", %1        \n\t"
        "add    %%"FF_REG_a", %2        \n\t"
        "movq   (%1), %%mm0             \n\t"
        "movq   1(%1), %%mm1            \n\t"
        "movq   (%1, %3), %%mm2         \n\t"
        "movq   1(%1, %3), %%mm3        \n\t"
        PAVGBP_MMX_NO_RND(%%mm0, %%mm1, %%mm4,   %%mm2, %%mm3, %%mm5)
        "movq   %%mm4, (%2)             \n\t"
        "movq   %%mm5, (%2, %3)         \n\t"
        "movq   8(%1), %%mm0            \n\t"
        "movq   9(%1), %%mm1            \n\t"
        "movq   8(%1, %3), %%mm2        \n\t"
        "movq   9(%1, %3), %%mm3        \n\t"
        PAVGBP_MMX_NO_RND(%%mm0, %%mm1, %%mm4,   %%mm2, %%mm3, %%mm5)
        "movq   %%mm4, 8(%2)            \n\t"
        "movq   %%mm5, 8(%2, %3)        \n\t"
        "add    %%"FF_REG_a", %1        \n\t"
        "add    %%"FF_REG_a", %2        \n\t"
        "subl   $4, %0                  \n\t"
        "jnz    1b                      \n\t"
        :"+g"(h), "+S"(pixels), "+D"(block)
        :"r"((x86_reg)line_size)
        :FF_REG_a, "memory");
}

static void put_no_rnd_pixels8_y2_mmx(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
{
    MOVQ_BFE(mm6);
    __asm__ volatile(
        "lea (%3, %3), %%"FF_REG_a"     \n\t"
        "movq (%1), %%mm0               \n\t"
        ".p2align 3                     \n\t"
        "1:                             \n\t"
        "movq   (%1, %3), %%mm1         \n\t"
        "movq   (%1, %%"FF_REG_a"),%%mm2\n\t"
        PAVGBP_MMX_NO_RND(%%mm1, %%mm0, %%mm4,   %%mm2, %%mm1, %%mm5)
        "movq   %%mm4, (%2)             \n\t"
        "movq   %%mm5, (%2, %3)         \n\t"
        "add    %%"FF_REG_a", %1        \n\t"
        "add    %%"FF_REG_a", %2        \n\t"
        "movq   (%1, %3), %%mm1         \n\t"
        "movq   (%1, %%"FF_REG_a"),%%mm0\n\t"
        PAVGBP_MMX_NO_RND(%%mm1, %%mm2, %%mm4,   %%mm0, %%mm1, %%mm5)
        "movq   %%mm4, (%2)             \n\t"
        "movq   %%mm5, (%2, %3)         \n\t"
        "add    %%"FF_REG_a", %1        \n\t"
        "add    %%"FF_REG_a", %2        \n\t"
        "subl   $4, %0                  \n\t"
        "jnz    1b                      \n\t"
        :"+g"(h), "+S"(pixels), "+D"(block)
        :"r"((x86_reg)line_size)
        :FF_REG_a, "memory");
}

static void avg_no_rnd_pixels16_x2_mmx(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
{
    MOVQ_BFE(mm6);
        __asm__ volatile(
            ".p2align 3                 \n\t"
            "1:                         \n\t"
            "movq  (%1), %%mm0          \n\t"
            "movq  1(%1), %%mm1         \n\t"
            "movq  (%2), %%mm3          \n\t"
            PAVGB_MMX_NO_RND(%%mm0, %%mm1, %%mm2, %%mm6)
            PAVGB_MMX(%%mm3, %%mm2, %%mm0, %%mm6)
            "movq  %%mm0, (%2)          \n\t"
            "movq  8(%1), %%mm0         \n\t"
            "movq  9(%1), %%mm1         \n\t"
            "movq  8(%2), %%mm3         \n\t"
            PAVGB_MMX_NO_RND(%%mm0, %%mm1, %%mm2, %%mm6)
            PAVGB_MMX(%%mm3, %%mm2, %%mm0, %%mm6)
            "movq  %%mm0, 8(%2)         \n\t"
            "add    %3, %1              \n\t"
            "add    %3, %2              \n\t"
            "subl   $1, %0              \n\t"
            "jnz    1b                  \n\t"
            :"+g"(h), "+S"(pixels), "+D"(block)
            :"r"((x86_reg)line_size)
            :"memory");
}

static void avg_no_rnd_pixels8_y2_mmx(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
{
    MOVQ_BFE(mm6);
    __asm__ volatile(
        "lea    (%3, %3), %%"FF_REG_a"  \n\t"
        "movq   (%1), %%mm0             \n\t"
        ".p2align 3                     \n\t"
        "1:                             \n\t"
        "movq   (%1, %3), %%mm1         \n\t"
        "movq   (%1, %%"FF_REG_a"), %%mm2 \n\t"
        PAVGBP_MMX_NO_RND(%%mm1, %%mm0, %%mm4,   %%mm2, %%mm1, %%mm5)
        "movq   (%2), %%mm3             \n\t"
        PAVGB_MMX(%%mm3, %%mm4, %%mm0, %%mm6)
        "movq   (%2, %3), %%mm3         \n\t"
        PAVGB_MMX(%%mm3, %%mm5, %%mm1, %%mm6)
        "movq   %%mm0, (%2)             \n\t"
        "movq   %%mm1, (%2, %3)         \n\t"
        "add    %%"FF_REG_a", %1        \n\t"
        "add    %%"FF_REG_a", %2        \n\t"

        "movq   (%1, %3), %%mm1         \n\t"
        "movq   (%1, %%"FF_REG_a"), %%mm0 \n\t"
        PAVGBP_MMX_NO_RND(%%mm1, %%mm2, %%mm4,   %%mm0, %%mm1, %%mm5)
        "movq   (%2), %%mm3             \n\t"
        PAVGB_MMX(%%mm3, %%mm4, %%mm2, %%mm6)
        "movq   (%2, %3), %%mm3         \n\t"
        PAVGB_MMX(%%mm3, %%mm5, %%mm1, %%mm6)
        "movq   %%mm2, (%2)             \n\t"
        "movq   %%mm1, (%2, %3)         \n\t"
        "add    %%"FF_REG_a", %1        \n\t"
        "add    %%"FF_REG_a", %2        \n\t"

        "subl   $4, %0                  \n\t"
        "jnz    1b                      \n\t"
        :"+g"(h), "+S"(pixels), "+D"(block)
        :"r"((x86_reg)line_size)
        :FF_REG_a, "memory");
}

#if HAVE_MMX
CALL_2X_PIXELS(avg_no_rnd_pixels16_y2_mmx, avg_no_rnd_pixels8_y2_mmx, 8)
CALL_2X_PIXELS(put_no_rnd_pixels16_y2_mmx, put_no_rnd_pixels8_y2_mmx, 8)

CALL_2X_PIXELS(avg_no_rnd_pixels16_xy2_mmx, avg_no_rnd_pixels8_xy2_mmx, 8)
CALL_2X_PIXELS(put_no_rnd_pixels16_xy2_mmx, put_no_rnd_pixels8_xy2_mmx, 8)
#endif

/***********************************/
/* MMX rounding */

#define SET_RND  MOVQ_WTWO
#define DEF(x, y) ff_ ## x ## _ ## y ## _mmx
#define STATIC

#include "rnd_template.c"

#undef NO_AVG
#undef DEF
#undef SET_RND

#if HAVE_MMX
CALL_2X_PIXELS(put_pixels16_xy2_mmx, ff_put_pixels8_xy2_mmx, 8)
#endif

#endif /* HAVE_INLINE_ASM */


#if HAVE_X86ASM

#define HPELDSP_AVG_PIXELS16(CPUEXT)                      \
    CALL_2X_PIXELS(put_no_rnd_pixels16_x2 ## CPUEXT, ff_put_no_rnd_pixels8_x2 ## CPUEXT, 8) \
    CALL_2X_PIXELS(put_pixels16_y2        ## CPUEXT, ff_put_pixels8_y2        ## CPUEXT, 8) \
    CALL_2X_PIXELS(put_no_rnd_pixels16_y2 ## CPUEXT, ff_put_no_rnd_pixels8_y2 ## CPUEXT, 8) \
    CALL_2X_PIXELS(avg_pixels16_x2        ## CPUEXT, ff_avg_pixels8_x2        ## CPUEXT, 8) \
    CALL_2X_PIXELS(avg_pixels16_y2        ## CPUEXT, ff_avg_pixels8_y2        ## CPUEXT, 8) \
    CALL_2X_PIXELS(avg_pixels16_xy2       ## CPUEXT, ff_avg_pixels8_xy2       ## CPUEXT, 8) \
    CALL_2X_PIXELS(avg_approx_pixels16_xy2## CPUEXT, ff_avg_approx_pixels8_xy2## CPUEXT, 8)

HPELDSP_AVG_PIXELS16(_mmxext)

#endif /* HAVE_X86ASM */

#define SET_HPEL_FUNCS_EXT(PFX, IDX, SIZE, CPU)                             \
    if (HAVE_MMX_EXTERNAL)                                                  \
        c->PFX ## _pixels_tab IDX [0] = PFX ## _pixels ## SIZE ## _ ## CPU

#define SET_HPEL_FUNCS03(PFX, IDX, SIZE, CPU)                                   \
    do {                                                                        \
        SET_HPEL_FUNCS_EXT(PFX, IDX, SIZE, CPU);                                \
        c->PFX ## _pixels_tab IDX [3] = PFX ## _pixels ## SIZE ## _xy2_ ## CPU; \
    } while (0)
#define SET_HPEL_FUNCS12(PFX, IDX, SIZE, CPU)                                   \
    do {                                                                        \
        c->PFX ## _pixels_tab IDX [1] = PFX ## _pixels ## SIZE ## _x2_  ## CPU; \
        c->PFX ## _pixels_tab IDX [2] = PFX ## _pixels ## SIZE ## _y2_  ## CPU; \
    } while (0)
#define SET_HPEL_FUNCS(PFX, IDX, SIZE, CPU)                                     \
    do {                                                                        \
        SET_HPEL_FUNCS03(PFX, IDX, SIZE, CPU);                                  \
        SET_HPEL_FUNCS12(PFX, IDX, SIZE, CPU);                                  \
    } while (0)

static void hpeldsp_init_mmx(HpelDSPContext *c, int flags)
{
#if HAVE_MMX_INLINE
    SET_HPEL_FUNCS03(put,      [0], 16, mmx);
    SET_HPEL_FUNCS(put_no_rnd, [0], 16, mmx);
    SET_HPEL_FUNCS12(avg_no_rnd,  , 16, mmx);
    c->avg_no_rnd_pixels_tab[3] = avg_no_rnd_pixels16_xy2_mmx;
    SET_HPEL_FUNCS03(put,      [1],  8, mmx);
    SET_HPEL_FUNCS(put_no_rnd, [1],  8, mmx);
#endif
}

static void hpeldsp_init_mmxext(HpelDSPContext *c, int flags)
{
#if HAVE_MMXEXT_EXTERNAL
    c->put_pixels_tab[0][1] = ff_put_pixels16_x2_mmxext;
    c->put_pixels_tab[0][2] = put_pixels16_y2_mmxext;

    c->avg_pixels_tab[0][0] = ff_avg_pixels16_mmxext;
    c->avg_pixels_tab[0][1] = avg_pixels16_x2_mmxext;
    c->avg_pixels_tab[0][2] = avg_pixels16_y2_mmxext;
    c->avg_pixels_tab[0][3] = avg_pixels16_xy2_mmxext;

    c->put_pixels_tab[1][1] = ff_put_pixels8_x2_mmxext;
    c->put_pixels_tab[1][2] = ff_put_pixels8_y2_mmxext;

    c->avg_pixels_tab[1][0] = ff_avg_pixels8_mmxext;
    c->avg_pixels_tab[1][1] = ff_avg_pixels8_x2_mmxext;
    c->avg_pixels_tab[1][2] = ff_avg_pixels8_y2_mmxext;
    c->avg_pixels_tab[1][3] = ff_avg_pixels8_xy2_mmxext;

    c->put_no_rnd_pixels_tab[1][1] = ff_put_no_rnd_pixels8_x2_exact_mmxext;
    c->put_no_rnd_pixels_tab[1][2] = ff_put_no_rnd_pixels8_y2_exact_mmxext;

    c->avg_no_rnd_pixels_tab[0] = ff_avg_pixels16_mmxext;

    if (!(flags & AV_CODEC_FLAG_BITEXACT)) {
        c->put_no_rnd_pixels_tab[0][1] = put_no_rnd_pixels16_x2_mmxext;
        c->put_no_rnd_pixels_tab[0][2] = put_no_rnd_pixels16_y2_mmxext;
        c->put_no_rnd_pixels_tab[1][1] = ff_put_no_rnd_pixels8_x2_mmxext;
        c->put_no_rnd_pixels_tab[1][2] = ff_put_no_rnd_pixels8_y2_mmxext;

        c->avg_pixels_tab[0][3] = avg_approx_pixels16_xy2_mmxext;
        c->avg_pixels_tab[1][3] = ff_avg_approx_pixels8_xy2_mmxext;
    }
#endif /* HAVE_MMXEXT_EXTERNAL */
}

static void hpeldsp_init_sse2_fast(HpelDSPContext *c, int flags)
{
#if HAVE_SSE2_EXTERNAL
    c->put_pixels_tab[0][0]        = ff_put_pixels16_sse2;
    c->put_no_rnd_pixels_tab[0][0] = ff_put_pixels16_sse2;
    c->put_pixels_tab[0][1]        = ff_put_pixels16_x2_sse2;
    c->put_pixels_tab[0][2]        = ff_put_pixels16_y2_sse2;
    c->put_pixels_tab[0][3]        = ff_put_pixels16_xy2_sse2;
    c->avg_pixels_tab[0][0]        = ff_avg_pixels16_sse2;
    c->avg_pixels_tab[0][1]        = ff_avg_pixels16_x2_sse2;
    c->avg_pixels_tab[0][2]        = ff_avg_pixels16_y2_sse2;
    c->avg_pixels_tab[0][3]        = ff_avg_pixels16_xy2_sse2;
    c->avg_no_rnd_pixels_tab[0]    = ff_avg_pixels16_sse2;
#endif /* HAVE_SSE2_EXTERNAL */
}

static void hpeldsp_init_ssse3(HpelDSPContext *c, int flags)
{
#if HAVE_SSSE3_EXTERNAL
    c->put_pixels_tab[0][3]            = ff_put_pixels16_xy2_ssse3;
    c->avg_pixels_tab[0][3]            = ff_avg_pixels16_xy2_ssse3;
    c->put_pixels_tab[1][3]            = ff_put_pixels8_xy2_ssse3;
    c->avg_pixels_tab[1][3]            = ff_avg_pixels8_xy2_ssse3;
#endif
}

av_cold void ff_hpeldsp_init_x86(HpelDSPContext *c, int flags)
{
    int cpu_flags = av_get_cpu_flags();

    if (INLINE_MMX(cpu_flags))
        hpeldsp_init_mmx(c, flags);

    if (EXTERNAL_MMXEXT(cpu_flags))
        hpeldsp_init_mmxext(c, flags);

    if (EXTERNAL_SSE2_FAST(cpu_flags))
        hpeldsp_init_sse2_fast(c, flags);

    if (EXTERNAL_SSSE3(cpu_flags))
        hpeldsp_init_ssse3(c, flags);
}
