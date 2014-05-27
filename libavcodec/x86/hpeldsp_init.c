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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/hpeldsp.h"
#include "libavcodec/pixels.h"
#include "fpel.h"
#include "hpeldsp.h"

void ff_put_pixels8_x2_mmxext(uint8_t *block, const uint8_t *pixels,
                              ptrdiff_t line_size, int h);
void ff_put_pixels8_x2_3dnow(uint8_t *block, const uint8_t *pixels,
                             ptrdiff_t line_size, int h);
void ff_put_pixels16_x2_mmxext(uint8_t *block, const uint8_t *pixels,
                               ptrdiff_t line_size, int h);
void ff_put_pixels16_x2_3dnow(uint8_t *block, const uint8_t *pixels,
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
void ff_avg_pixels8_xy2_3dnow(uint8_t *block, const uint8_t *pixels,
                              ptrdiff_t line_size, int h);
void ff_avg_approx_pixels8_xy2_mmxext(uint8_t *block, const uint8_t *pixels,
                                      ptrdiff_t line_size, int h);
void ff_avg_approx_pixels8_xy2_3dnow(uint8_t *block, const uint8_t *pixels,
                                     ptrdiff_t line_size, int h);

#define avg_pixels8_mmx         ff_avg_pixels8_mmx
#define avg_pixels8_x2_mmx      ff_avg_pixels8_x2_mmx
#define avg_pixels16_mmx        ff_avg_pixels16_mmx
#define avg_pixels8_xy2_mmx     ff_avg_pixels8_xy2_mmx
#define avg_pixels16_xy2_mmx    ff_avg_pixels16_xy2_mmx
#define put_pixels8_mmx         ff_put_pixels8_mmx
#define put_pixels16_mmx        ff_put_pixels16_mmx
#define put_pixels8_xy2_mmx     ff_put_pixels8_xy2_mmx
#define put_pixels16_xy2_mmx    ff_put_pixels16_xy2_mmx
#define avg_no_rnd_pixels16_mmx ff_avg_pixels16_mmx
#define put_no_rnd_pixels8_mmx  ff_put_pixels8_mmx
#define put_no_rnd_pixels16_mmx ff_put_pixels16_mmx

#if HAVE_INLINE_ASM

/***********************************/
/* MMX no rounding */
#define DEF(x, y) x ## _no_rnd_ ## y ## _mmx
#define SET_RND  MOVQ_WONE
#define PAVGBP(a, b, c, d, e, f)        PAVGBP_MMX_NO_RND(a, b, c, d, e, f)
#define PAVGB(a, b, c, e)               PAVGB_MMX_NO_RND(a, b, c, e)
#define STATIC static

#include "rnd_template.c"
#include "hpeldsp_rnd_template.c"

#undef DEF
#undef SET_RND
#undef PAVGBP
#undef PAVGB
#undef STATIC

CALL_2X_PIXELS(avg_no_rnd_pixels16_y2_mmx, avg_no_rnd_pixels8_y2_mmx, 8)
CALL_2X_PIXELS(put_no_rnd_pixels16_y2_mmx, put_no_rnd_pixels8_y2_mmx, 8)

CALL_2X_PIXELS(avg_no_rnd_pixels16_xy2_mmx, avg_no_rnd_pixels8_xy2_mmx, 8)
CALL_2X_PIXELS(put_no_rnd_pixels16_xy2_mmx, put_no_rnd_pixels8_xy2_mmx, 8)

/***********************************/
/* MMX rounding */

#define DEF(x, y) x ## _ ## y ## _mmx
#define SET_RND  MOVQ_WTWO
#define PAVGBP(a, b, c, d, e, f)        PAVGBP_MMX(a, b, c, d, e, f)
#define PAVGB(a, b, c, e)               PAVGB_MMX(a, b, c, e)

#include "hpeldsp_rnd_template.c"

#undef DEF
#define DEF(x, y) ff_ ## x ## _ ## y ## _mmx
#define STATIC

#include "rnd_template.c"

#undef DEF
#undef SET_RND
#undef PAVGBP
#undef PAVGB

CALL_2X_PIXELS(avg_pixels16_y2_mmx, avg_pixels8_y2_mmx, 8)
CALL_2X_PIXELS(put_pixels16_y2_mmx, put_pixels8_y2_mmx, 8)

CALL_2X_PIXELS_EXPORT(ff_avg_pixels16_xy2_mmx, ff_avg_pixels8_xy2_mmx, 8)
CALL_2X_PIXELS_EXPORT(ff_put_pixels16_xy2_mmx, ff_put_pixels8_xy2_mmx, 8)

#endif /* HAVE_INLINE_ASM */


#if HAVE_YASM

#define HPELDSP_AVG_PIXELS16(CPUEXT)                      \
    CALL_2X_PIXELS(put_no_rnd_pixels16_x2 ## CPUEXT, ff_put_no_rnd_pixels8_x2 ## CPUEXT, 8) \
    CALL_2X_PIXELS(put_pixels16_y2        ## CPUEXT, ff_put_pixels8_y2        ## CPUEXT, 8) \
    CALL_2X_PIXELS(put_no_rnd_pixels16_y2 ## CPUEXT, ff_put_no_rnd_pixels8_y2 ## CPUEXT, 8) \
    CALL_2X_PIXELS(avg_pixels16           ## CPUEXT, ff_avg_pixels8           ## CPUEXT, 8) \
    CALL_2X_PIXELS(avg_pixels16_x2        ## CPUEXT, ff_avg_pixels8_x2        ## CPUEXT, 8) \
    CALL_2X_PIXELS(avg_pixels16_y2        ## CPUEXT, ff_avg_pixels8_y2        ## CPUEXT, 8) \
    CALL_2X_PIXELS(avg_pixels16_xy2       ## CPUEXT, ff_avg_pixels8_xy2       ## CPUEXT, 8) \
    CALL_2X_PIXELS(avg_approx_pixels16_xy2## CPUEXT, ff_avg_approx_pixels8_xy2## CPUEXT, 8)

HPELDSP_AVG_PIXELS16(_3dnow)
HPELDSP_AVG_PIXELS16(_mmxext)

#endif /* HAVE_YASM */

#define SET_HPEL_FUNCS_EXT(PFX, IDX, SIZE, CPU)                             \
    if (HAVE_MMX_EXTERNAL)                                                  \
    c->PFX ## _pixels_tab IDX [0] = PFX ## _pixels ## SIZE ## _     ## CPU;

#if HAVE_MMX_INLINE
#define SET_HPEL_FUNCS(PFX, IDX, SIZE, CPU)                                     \
    do {                                                                        \
        SET_HPEL_FUNCS_EXT(PFX, IDX, SIZE, CPU)                                 \
        c->PFX ## _pixels_tab IDX [1] = PFX ## _pixels ## SIZE ## _x2_  ## CPU; \
        c->PFX ## _pixels_tab IDX [2] = PFX ## _pixels ## SIZE ## _y2_  ## CPU; \
        c->PFX ## _pixels_tab IDX [3] = PFX ## _pixels ## SIZE ## _xy2_ ## CPU; \
    } while (0)
#else
#define SET_HPEL_FUNCS(PFX, IDX, SIZE, CPU)                                     \
    do {                                                                        \
        SET_HPEL_FUNCS_EXT(PFX, IDX, SIZE, CPU)                                 \
    } while (0)
#endif

static void hpeldsp_init_mmx(HpelDSPContext *c, int flags, int cpu_flags)
{
    SET_HPEL_FUNCS(put,        [0], 16, mmx);
    SET_HPEL_FUNCS(put_no_rnd, [0], 16, mmx);
    SET_HPEL_FUNCS(avg,        [0], 16, mmx);
    SET_HPEL_FUNCS(avg_no_rnd,    , 16, mmx);
    SET_HPEL_FUNCS(put,        [1],  8, mmx);
    SET_HPEL_FUNCS(put_no_rnd, [1],  8, mmx);
    if (HAVE_MMX_EXTERNAL) {
        c->avg_pixels_tab[1][0] = ff_avg_pixels8_mmx;
        c->avg_pixels_tab[1][1] = ff_avg_pixels8_x2_mmx;
    }
#if HAVE_MMX_INLINE
    c->avg_pixels_tab[1][2] = avg_pixels8_y2_mmx;
    c->avg_pixels_tab[1][3] = ff_avg_pixels8_xy2_mmx;
#endif
}

static void hpeldsp_init_mmxext(HpelDSPContext *c, int flags, int cpu_flags)
{
#if HAVE_MMXEXT_EXTERNAL
    c->put_pixels_tab[0][1] = ff_put_pixels16_x2_mmxext;
    c->put_pixels_tab[0][2] = put_pixels16_y2_mmxext;

    c->avg_pixels_tab[0][0] = avg_pixels16_mmxext;
    c->avg_pixels_tab[0][1] = avg_pixels16_x2_mmxext;
    c->avg_pixels_tab[0][2] = avg_pixels16_y2_mmxext;
    c->avg_pixels_tab[0][3] = avg_pixels16_xy2_mmxext;

    c->put_pixels_tab[1][1] = ff_put_pixels8_x2_mmxext;
    c->put_pixels_tab[1][2] = ff_put_pixels8_y2_mmxext;

    c->avg_pixels_tab[1][0] = ff_avg_pixels8_mmxext;
    c->avg_pixels_tab[1][1] = ff_avg_pixels8_x2_mmxext;
    c->avg_pixels_tab[1][2] = ff_avg_pixels8_y2_mmxext;
    c->avg_pixels_tab[1][3] = ff_avg_pixels8_xy2_mmxext;

    if (!(flags & CODEC_FLAG_BITEXACT)) {
        c->put_no_rnd_pixels_tab[0][1] = put_no_rnd_pixels16_x2_mmxext;
        c->put_no_rnd_pixels_tab[0][2] = put_no_rnd_pixels16_y2_mmxext;
        c->put_no_rnd_pixels_tab[1][1] = ff_put_no_rnd_pixels8_x2_mmxext;
        c->put_no_rnd_pixels_tab[1][2] = ff_put_no_rnd_pixels8_y2_mmxext;

        c->avg_pixels_tab[0][3] = avg_approx_pixels16_xy2_mmxext;
        c->avg_pixels_tab[1][3] = ff_avg_approx_pixels8_xy2_mmxext;
    }

    if (flags & CODEC_FLAG_BITEXACT && CONFIG_VP3_DECODER) {
        c->put_no_rnd_pixels_tab[1][1] = ff_put_no_rnd_pixels8_x2_exact_mmxext;
        c->put_no_rnd_pixels_tab[1][2] = ff_put_no_rnd_pixels8_y2_exact_mmxext;
    }
#endif /* HAVE_MMXEXT_EXTERNAL */
}

static void hpeldsp_init_3dnow(HpelDSPContext *c, int flags, int cpu_flags)
{
#if HAVE_AMD3DNOW_EXTERNAL
    c->put_pixels_tab[0][1] = ff_put_pixels16_x2_3dnow;
    c->put_pixels_tab[0][2] = put_pixels16_y2_3dnow;

    c->avg_pixels_tab[0][0] = avg_pixels16_3dnow;
    c->avg_pixels_tab[0][1] = avg_pixels16_x2_3dnow;
    c->avg_pixels_tab[0][2] = avg_pixels16_y2_3dnow;
    c->avg_pixels_tab[0][3] = avg_pixels16_xy2_3dnow;

    c->put_pixels_tab[1][1] = ff_put_pixels8_x2_3dnow;
    c->put_pixels_tab[1][2] = ff_put_pixels8_y2_3dnow;

    c->avg_pixels_tab[1][0] = ff_avg_pixels8_3dnow;
    c->avg_pixels_tab[1][1] = ff_avg_pixels8_x2_3dnow;
    c->avg_pixels_tab[1][2] = ff_avg_pixels8_y2_3dnow;
    c->avg_pixels_tab[1][3] = ff_avg_pixels8_xy2_3dnow;

    if (!(flags & CODEC_FLAG_BITEXACT)){
        c->put_no_rnd_pixels_tab[0][1] = put_no_rnd_pixels16_x2_3dnow;
        c->put_no_rnd_pixels_tab[0][2] = put_no_rnd_pixels16_y2_3dnow;
        c->put_no_rnd_pixels_tab[1][1] = ff_put_no_rnd_pixels8_x2_3dnow;
        c->put_no_rnd_pixels_tab[1][2] = ff_put_no_rnd_pixels8_y2_3dnow;

        c->avg_pixels_tab[0][3] = avg_approx_pixels16_xy2_3dnow;
        c->avg_pixels_tab[1][3] = ff_avg_approx_pixels8_xy2_3dnow;
    }

    if (flags & CODEC_FLAG_BITEXACT && CONFIG_VP3_DECODER) {
        c->put_no_rnd_pixels_tab[1][1] = ff_put_no_rnd_pixels8_x2_exact_3dnow;
        c->put_no_rnd_pixels_tab[1][2] = ff_put_no_rnd_pixels8_y2_exact_3dnow;
    }
#endif /* HAVE_AMD3DNOW_EXTERNAL */
}

static void hpeldsp_init_sse2(HpelDSPContext *c, int flags, int cpu_flags)
{
#if HAVE_SSE2_EXTERNAL
    if (!(cpu_flags & AV_CPU_FLAG_SSE2SLOW)) {
        // these functions are slower than mmx on AMD, but faster on Intel
        c->put_pixels_tab[0][0]        = ff_put_pixels16_sse2;
        c->put_no_rnd_pixels_tab[0][0] = ff_put_pixels16_sse2;
        c->put_pixels_tab[0][1]        = ff_put_pixels16_x2_sse2;
        c->put_pixels_tab[0][2]        = ff_put_pixels16_y2_sse2;
        c->put_pixels_tab[0][3]        = ff_put_pixels16_xy2_sse2;
        c->avg_pixels_tab[0][0]        = ff_avg_pixels16_sse2;
        c->avg_pixels_tab[0][1]        = ff_avg_pixels16_x2_sse2;
        c->avg_pixels_tab[0][2]        = ff_avg_pixels16_y2_sse2;
        c->avg_pixels_tab[0][3]        = ff_avg_pixels16_xy2_sse2;
    }
#endif /* HAVE_SSE2_EXTERNAL */
}

static void hpeldsp_init_ssse3(HpelDSPContext *c, int flags, int cpu_flags)
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
        hpeldsp_init_mmx(c, flags, cpu_flags);

    if (EXTERNAL_AMD3DNOW(cpu_flags))
        hpeldsp_init_3dnow(c, flags, cpu_flags);

    if (EXTERNAL_MMXEXT(cpu_flags))
        hpeldsp_init_mmxext(c, flags, cpu_flags);

    if (EXTERNAL_SSE2(cpu_flags))
        hpeldsp_init_sse2(c, flags, cpu_flags);

    if (EXTERNAL_SSSE3(cpu_flags))
        hpeldsp_init_ssse3(c, flags, cpu_flags);
}
