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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavcodec/dsputil.h"
#include "libavcodec/h264dsp.h"
#include "libavcodec/mpegvideo.h"
#include "libavcodec/simple_idct.h"
#include "libavcodec/videodsp.h"
#include "dsputil_mmx.h"
#include "idct_xvid.h"
#include "diracdsp_mmx.h"

//#undef NDEBUG
//#include <assert.h>

/* pixel operations */
DECLARE_ALIGNED(8,  const uint64_t, ff_bone) = 0x0101010101010101ULL;
DECLARE_ALIGNED(8,  const uint64_t, ff_wtwo) = 0x0002000200020002ULL;

DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_1)    = { 0x0001000100010001ULL, 0x0001000100010001ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_2)    = { 0x0002000200020002ULL, 0x0002000200020002ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_3)    = { 0x0003000300030003ULL, 0x0003000300030003ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_4)    = { 0x0004000400040004ULL, 0x0004000400040004ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_5)    = { 0x0005000500050005ULL, 0x0005000500050005ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_8)    = { 0x0008000800080008ULL, 0x0008000800080008ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_9)    = { 0x0009000900090009ULL, 0x0009000900090009ULL };
DECLARE_ALIGNED(8,  const uint64_t, ff_pw_15)   =   0x000F000F000F000FULL;
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_16)   = { 0x0010001000100010ULL, 0x0010001000100010ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_17)   = { 0x0011001100110011ULL, 0x0011001100110011ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_18)   = { 0x0012001200120012ULL, 0x0012001200120012ULL };
DECLARE_ALIGNED(8,  const uint64_t, ff_pw_20)   =   0x0014001400140014ULL;
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_27)   = { 0x001B001B001B001BULL, 0x001B001B001B001BULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_28)   = { 0x001C001C001C001CULL, 0x001C001C001C001CULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_32)   = { 0x0020002000200020ULL, 0x0020002000200020ULL };
DECLARE_ALIGNED(8,  const uint64_t, ff_pw_42)   =   0x002A002A002A002AULL;
DECLARE_ALIGNED(8,  const uint64_t, ff_pw_53)   =   0x0035003500350035ULL;
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_63)   = { 0x003F003F003F003FULL, 0x003F003F003F003FULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_64)   = { 0x0040004000400040ULL, 0x0040004000400040ULL };
DECLARE_ALIGNED(8,  const uint64_t, ff_pw_96)   =   0x0060006000600060ULL;
DECLARE_ALIGNED(8,  const uint64_t, ff_pw_128)  =   0x0080008000800080ULL;
DECLARE_ALIGNED(8,  const uint64_t, ff_pw_255)  =   0x00ff00ff00ff00ffULL;
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_512)  = { 0x0200020002000200ULL, 0x0200020002000200ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pw_1019) = { 0x03FB03FB03FB03FBULL, 0x03FB03FB03FB03FBULL };

DECLARE_ALIGNED(16, const xmm_reg,  ff_pb_0)    = { 0x0000000000000000ULL, 0x0000000000000000ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pb_1)    = { 0x0101010101010101ULL, 0x0101010101010101ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pb_3)    = { 0x0303030303030303ULL, 0x0303030303030303ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pb_4)    = { 0x0404040404040404ULL, 0x0404040404040404ULL };
DECLARE_ALIGNED(8,  const uint64_t, ff_pb_7)    =   0x0707070707070707ULL;
DECLARE_ALIGNED(8,  const uint64_t, ff_pb_1F)   =   0x1F1F1F1F1F1F1F1FULL;
DECLARE_ALIGNED(8,  const uint64_t, ff_pb_3F)   =   0x3F3F3F3F3F3F3F3FULL;
DECLARE_ALIGNED(16, const xmm_reg,  ff_pb_80)   = { 0x8080808080808080ULL, 0x8080808080808080ULL };
DECLARE_ALIGNED(8,  const uint64_t, ff_pb_81)   =   0x8181818181818181ULL;
DECLARE_ALIGNED(16, const xmm_reg,  ff_pb_A1)   = { 0xA1A1A1A1A1A1A1A1ULL, 0xA1A1A1A1A1A1A1A1ULL };
DECLARE_ALIGNED(16, const xmm_reg,  ff_pb_F8)   = { 0xF8F8F8F8F8F8F8F8ULL, 0xF8F8F8F8F8F8F8F8ULL };
DECLARE_ALIGNED(8,  const uint64_t, ff_pb_FC)   =   0xFCFCFCFCFCFCFCFCULL;
DECLARE_ALIGNED(16, const xmm_reg,  ff_pb_FE)   = { 0xFEFEFEFEFEFEFEFEULL, 0xFEFEFEFEFEFEFEFEULL };

DECLARE_ALIGNED(16, const double, ff_pd_1)[2] = { 1.0, 1.0 };
DECLARE_ALIGNED(16, const double, ff_pd_2)[2] = { 2.0, 2.0 };


#if HAVE_YASM
void ff_put_pixels8_x2_mmxext(uint8_t *block, const uint8_t *pixels,
                              ptrdiff_t line_size, int h);
void ff_put_pixels8_x2_3dnow(uint8_t *block, const uint8_t *pixels,
                             ptrdiff_t line_size, int h);
void ff_put_pixels8_l2_mmxext(uint8_t *dst, uint8_t *src1, uint8_t *src2,
                              int dstStride, int src1Stride, int h);
void ff_put_no_rnd_pixels8_l2_mmxext(uint8_t *dst, uint8_t *src1,
                                     uint8_t *src2, int dstStride,
                                     int src1Stride, int h);
void ff_avg_pixels8_l2_mmxext(uint8_t *dst, uint8_t *src1, uint8_t *src2,
                              int dstStride, int src1Stride, int h);
void ff_put_pixels16_x2_mmxext(uint8_t *block, const uint8_t *pixels,
                               ptrdiff_t line_size, int h);
void ff_put_pixels16_x2_3dnow(uint8_t *block, const uint8_t *pixels,
                              ptrdiff_t line_size, int h);
void ff_put_pixels16_l2_mmxext(uint8_t *dst, uint8_t *src1, uint8_t *src2,
                               int dstStride, int src1Stride, int h);
void ff_avg_pixels16_l2_mmxext(uint8_t *dst, uint8_t *src1, uint8_t *src2,
                               int dstStride, int src1Stride, int h);
void ff_put_no_rnd_pixels16_l2_mmxext(uint8_t *dst, uint8_t *src1, uint8_t *src2,
                                      int dstStride, int src1Stride, int h);
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

void ff_put_pixels8_mmxext(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h);
static void ff_put_pixels16_mmxext(uint8_t *block, const uint8_t *pixels,
                                   ptrdiff_t line_size, int h)
{
    ff_put_pixels8_mmxext(block,     pixels,     line_size, h);
    ff_put_pixels8_mmxext(block + 8, pixels + 8, line_size, h);
}

void ff_put_mpeg4_qpel16_h_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                         int dstStride, int srcStride, int h);
void ff_avg_mpeg4_qpel16_h_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                         int dstStride, int srcStride, int h);
void ff_put_no_rnd_mpeg4_qpel16_h_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                                 int dstStride, int srcStride,
                                                 int h);
void ff_put_mpeg4_qpel8_h_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                        int dstStride, int srcStride, int h);
void ff_avg_mpeg4_qpel8_h_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                        int dstStride, int srcStride, int h);
void ff_put_no_rnd_mpeg4_qpel8_h_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                                int dstStride, int srcStride,
                                                int h);
void ff_put_mpeg4_qpel16_v_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                         int dstStride, int srcStride);
void ff_avg_mpeg4_qpel16_v_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                         int dstStride, int srcStride);
void ff_put_no_rnd_mpeg4_qpel16_v_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                                 int dstStride, int srcStride);
void ff_put_mpeg4_qpel8_v_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                        int dstStride, int srcStride);
void ff_avg_mpeg4_qpel8_v_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                        int dstStride, int srcStride);
void ff_put_no_rnd_mpeg4_qpel8_v_lowpass_mmxext(uint8_t *dst, uint8_t *src,
                                                int dstStride, int srcStride);
#define ff_put_no_rnd_pixels16_mmxext ff_put_pixels16_mmxext
#define ff_put_no_rnd_pixels8_mmxext ff_put_pixels8_mmxext
#endif /* HAVE_YASM */


#if HAVE_INLINE_ASM

#define JUMPALIGN()     __asm__ volatile (".p2align 3"::)
#define MOVQ_ZERO(regd) __asm__ volatile ("pxor %%"#regd", %%"#regd ::)

#define MOVQ_BFE(regd)                                  \
    __asm__ volatile (                                  \
        "pcmpeqd %%"#regd", %%"#regd"   \n\t"           \
        "paddb   %%"#regd", %%"#regd"   \n\t" ::)

#ifndef PIC
#define MOVQ_BONE(regd) __asm__ volatile ("movq %0, %%"#regd" \n\t" :: "m"(ff_bone))
#define MOVQ_WTWO(regd) __asm__ volatile ("movq %0, %%"#regd" \n\t" :: "m"(ff_wtwo))
#else
// for shared library it's better to use this way for accessing constants
// pcmpeqd -> -1
#define MOVQ_BONE(regd)                                 \
    __asm__ volatile (                                  \
        "pcmpeqd  %%"#regd", %%"#regd"  \n\t"           \
        "psrlw          $15, %%"#regd"  \n\t"           \
        "packuswb %%"#regd", %%"#regd"  \n\t" ::)

#define MOVQ_WTWO(regd)                                 \
    __asm__ volatile (                                  \
        "pcmpeqd %%"#regd", %%"#regd"   \n\t"           \
        "psrlw         $15, %%"#regd"   \n\t"           \
        "psllw          $1, %%"#regd"   \n\t"::)

#endif

// using regr as temporary and for the output result
// first argument is unmodifed and second is trashed
// regfe is supposed to contain 0xfefefefefefefefe
#define PAVGB_MMX_NO_RND(rega, regb, regr, regfe)                \
    "movq   "#rega", "#regr"            \n\t"                    \
    "pand   "#regb", "#regr"            \n\t"                    \
    "pxor   "#rega", "#regb"            \n\t"                    \
    "pand  "#regfe", "#regb"            \n\t"                    \
    "psrlq       $1, "#regb"            \n\t"                    \
    "paddb  "#regb", "#regr"            \n\t"

#define PAVGB_MMX(rega, regb, regr, regfe)                       \
    "movq   "#rega", "#regr"            \n\t"                    \
    "por    "#regb", "#regr"            \n\t"                    \
    "pxor   "#rega", "#regb"            \n\t"                    \
    "pand  "#regfe", "#regb"            \n\t"                    \
    "psrlq       $1, "#regb"            \n\t"                    \
    "psubb  "#regb", "#regr"            \n\t"

// mm6 is supposed to contain 0xfefefefefefefefe
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

#define PAVGBP_MMX(rega, regb, regr, regc, regd, regp)           \
    "movq  "#rega", "#regr"             \n\t"                    \
    "movq  "#regc", "#regp"             \n\t"                    \
    "por   "#regb", "#regr"             \n\t"                    \
    "por   "#regd", "#regp"             \n\t"                    \
    "pxor  "#rega", "#regb"             \n\t"                    \
    "pxor  "#regc", "#regd"             \n\t"                    \
    "pand    %%mm6, "#regb"             \n\t"                    \
    "pand    %%mm6, "#regd"             \n\t"                    \
    "psrlq      $1, "#regd"             \n\t"                    \
    "psrlq      $1, "#regb"             \n\t"                    \
    "psubb "#regb", "#regr"             \n\t"                    \
    "psubb "#regd", "#regp"             \n\t"

/***********************************/
/* MMX no rounding */
#define NO_RND 1
#define DEF(x, y) x ## _no_rnd_ ## y ## _mmx
#define SET_RND  MOVQ_WONE
#define PAVGBP(a, b, c, d, e, f)        PAVGBP_MMX_NO_RND(a, b, c, d, e, f)
#define PAVGB(a, b, c, e)               PAVGB_MMX_NO_RND(a, b, c, e)
#define OP_AVG(a, b, c, e)              PAVGB_MMX(a, b, c, e)

#include "dsputil_rnd_template.c"

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

#include "dsputil_rnd_template.c"

#undef DEF
#undef SET_RND
#undef PAVGBP
#undef PAVGB
#undef OP_AVG

#endif /* HAVE_INLINE_ASM */


#if HAVE_YASM

/***********************************/
/* 3Dnow specific */

#define DEF(x) x ## _3dnow

#include "dsputil_avg_template.c"

#undef DEF

/***********************************/
/* MMXEXT specific */

#define DEF(x) x ## _mmxext

#include "dsputil_avg_template.c"

#undef DEF

#endif /* HAVE_YASM */


#if HAVE_INLINE_ASM
#define put_no_rnd_pixels16_mmx put_pixels16_mmx
#define put_no_rnd_pixels8_mmx put_pixels8_mmx

/***********************************/
/* standard MMX */

void ff_put_pixels_clamped_mmx(const int16_t *block, uint8_t *pixels,
                               int line_size)
{
    const int16_t *p;
    uint8_t *pix;

    /* read the pixels */
    p   = block;
    pix = pixels;
    /* unrolled loop */
    __asm__ volatile (
        "movq      (%3), %%mm0          \n\t"
        "movq     8(%3), %%mm1          \n\t"
        "movq    16(%3), %%mm2          \n\t"
        "movq    24(%3), %%mm3          \n\t"
        "movq    32(%3), %%mm4          \n\t"
        "movq    40(%3), %%mm5          \n\t"
        "movq    48(%3), %%mm6          \n\t"
        "movq    56(%3), %%mm7          \n\t"
        "packuswb %%mm1, %%mm0          \n\t"
        "packuswb %%mm3, %%mm2          \n\t"
        "packuswb %%mm5, %%mm4          \n\t"
        "packuswb %%mm7, %%mm6          \n\t"
        "movq     %%mm0, (%0)           \n\t"
        "movq     %%mm2, (%0, %1)       \n\t"
        "movq     %%mm4, (%0, %1, 2)    \n\t"
        "movq     %%mm6, (%0, %2)       \n\t"
        :: "r"(pix), "r"((x86_reg)line_size), "r"((x86_reg)line_size * 3),
           "r"(p)
        : "memory");
    pix += line_size * 4;
    p   += 32;

    // if here would be an exact copy of the code above
    // compiler would generate some very strange code
    // thus using "r"
    __asm__ volatile (
        "movq       (%3), %%mm0         \n\t"
        "movq      8(%3), %%mm1         \n\t"
        "movq     16(%3), %%mm2         \n\t"
        "movq     24(%3), %%mm3         \n\t"
        "movq     32(%3), %%mm4         \n\t"
        "movq     40(%3), %%mm5         \n\t"
        "movq     48(%3), %%mm6         \n\t"
        "movq     56(%3), %%mm7         \n\t"
        "packuswb  %%mm1, %%mm0         \n\t"
        "packuswb  %%mm3, %%mm2         \n\t"
        "packuswb  %%mm5, %%mm4         \n\t"
        "packuswb  %%mm7, %%mm6         \n\t"
        "movq      %%mm0, (%0)          \n\t"
        "movq      %%mm2, (%0, %1)      \n\t"
        "movq      %%mm4, (%0, %1, 2)   \n\t"
        "movq      %%mm6, (%0, %2)      \n\t"
        :: "r"(pix), "r"((x86_reg)line_size), "r"((x86_reg)line_size * 3), "r"(p)
        : "memory");
}

#define put_signed_pixels_clamped_mmx_half(off)             \
    "movq          "#off"(%2), %%mm1        \n\t"           \
    "movq     16 + "#off"(%2), %%mm2        \n\t"           \
    "movq     32 + "#off"(%2), %%mm3        \n\t"           \
    "movq     48 + "#off"(%2), %%mm4        \n\t"           \
    "packsswb  8 + "#off"(%2), %%mm1        \n\t"           \
    "packsswb 24 + "#off"(%2), %%mm2        \n\t"           \
    "packsswb 40 + "#off"(%2), %%mm3        \n\t"           \
    "packsswb 56 + "#off"(%2), %%mm4        \n\t"           \
    "paddb              %%mm0, %%mm1        \n\t"           \
    "paddb              %%mm0, %%mm2        \n\t"           \
    "paddb              %%mm0, %%mm3        \n\t"           \
    "paddb              %%mm0, %%mm4        \n\t"           \
    "movq               %%mm1, (%0)         \n\t"           \
    "movq               %%mm2, (%0, %3)     \n\t"           \
    "movq               %%mm3, (%0, %3, 2)  \n\t"           \
    "movq               %%mm4, (%0, %1)     \n\t"

void ff_put_signed_pixels_clamped_mmx(const int16_t *block, uint8_t *pixels,
                                      int line_size)
{
    x86_reg line_skip = line_size;
    x86_reg line_skip3;

    __asm__ volatile (
        "movq "MANGLE(ff_pb_80)", %%mm0     \n\t"
        "lea         (%3, %3, 2), %1        \n\t"
        put_signed_pixels_clamped_mmx_half(0)
        "lea         (%0, %3, 4), %0        \n\t"
        put_signed_pixels_clamped_mmx_half(64)
        : "+&r"(pixels), "=&r"(line_skip3)
        : "r"(block), "r"(line_skip)
        : "memory");
}

void ff_add_pixels_clamped_mmx(const int16_t *block, uint8_t *pixels,
                               int line_size)
{
    const int16_t *p;
    uint8_t *pix;
    int i;

    /* read the pixels */
    p   = block;
    pix = pixels;
    MOVQ_ZERO(mm7);
    i = 4;
    do {
        __asm__ volatile (
            "movq        (%2), %%mm0    \n\t"
            "movq       8(%2), %%mm1    \n\t"
            "movq      16(%2), %%mm2    \n\t"
            "movq      24(%2), %%mm3    \n\t"
            "movq          %0, %%mm4    \n\t"
            "movq          %1, %%mm6    \n\t"
            "movq       %%mm4, %%mm5    \n\t"
            "punpcklbw  %%mm7, %%mm4    \n\t"
            "punpckhbw  %%mm7, %%mm5    \n\t"
            "paddsw     %%mm4, %%mm0    \n\t"
            "paddsw     %%mm5, %%mm1    \n\t"
            "movq       %%mm6, %%mm5    \n\t"
            "punpcklbw  %%mm7, %%mm6    \n\t"
            "punpckhbw  %%mm7, %%mm5    \n\t"
            "paddsw     %%mm6, %%mm2    \n\t"
            "paddsw     %%mm5, %%mm3    \n\t"
            "packuswb   %%mm1, %%mm0    \n\t"
            "packuswb   %%mm3, %%mm2    \n\t"
            "movq       %%mm0, %0       \n\t"
            "movq       %%mm2, %1       \n\t"
            : "+m"(*pix), "+m"(*(pix + line_size))
            : "r"(p)
            : "memory");
        pix += line_size * 2;
        p   += 16;
    } while (--i);
}

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

#define CLEAR_BLOCKS(name, n)                           \
static void name(int16_t *blocks)                       \
{                                                       \
    __asm__ volatile (                                  \
        "pxor %%mm7, %%mm7              \n\t"           \
        "mov     %1,        %%"REG_a"   \n\t"           \
        "1:                             \n\t"           \
        "movq %%mm7,   (%0, %%"REG_a")  \n\t"           \
        "movq %%mm7,  8(%0, %%"REG_a")  \n\t"           \
        "movq %%mm7, 16(%0, %%"REG_a")  \n\t"           \
        "movq %%mm7, 24(%0, %%"REG_a")  \n\t"           \
        "add    $32, %%"REG_a"          \n\t"           \
        "js      1b                     \n\t"           \
        :: "r"(((uint8_t *)blocks) + 128 * n),          \
           "i"(-128 * n)                                \
        : "%"REG_a                                      \
        );                                              \
}
CLEAR_BLOCKS(clear_blocks_mmx, 6)
CLEAR_BLOCKS(clear_block_mmx, 1)

static void clear_block_sse(int16_t *block)
{
    __asm__ volatile (
        "xorps  %%xmm0, %%xmm0          \n"
        "movaps %%xmm0,    (%0)         \n"
        "movaps %%xmm0,  16(%0)         \n"
        "movaps %%xmm0,  32(%0)         \n"
        "movaps %%xmm0,  48(%0)         \n"
        "movaps %%xmm0,  64(%0)         \n"
        "movaps %%xmm0,  80(%0)         \n"
        "movaps %%xmm0,  96(%0)         \n"
        "movaps %%xmm0, 112(%0)         \n"
        :: "r"(block)
        : "memory"
    );
}

static void clear_blocks_sse(int16_t *blocks)
{
    __asm__ volatile (
        "xorps  %%xmm0, %%xmm0              \n"
        "mov        %1,         %%"REG_a"   \n"
        "1:                                 \n"
        "movaps %%xmm0,    (%0, %%"REG_a")  \n"
        "movaps %%xmm0,  16(%0, %%"REG_a")  \n"
        "movaps %%xmm0,  32(%0, %%"REG_a")  \n"
        "movaps %%xmm0,  48(%0, %%"REG_a")  \n"
        "movaps %%xmm0,  64(%0, %%"REG_a")  \n"
        "movaps %%xmm0,  80(%0, %%"REG_a")  \n"
        "movaps %%xmm0,  96(%0, %%"REG_a")  \n"
        "movaps %%xmm0, 112(%0, %%"REG_a")  \n"
        "add      $128,         %%"REG_a"   \n"
        "js         1b                      \n"
        :: "r"(((uint8_t *)blocks) + 128 * 6),
           "i"(-128 * 6)
        : "%"REG_a
    );
}

static void add_bytes_mmx(uint8_t *dst, uint8_t *src, int w)
{
    x86_reg i = 0;
    __asm__ volatile (
        "jmp          2f                \n\t"
        "1:                             \n\t"
        "movq   (%1, %0), %%mm0         \n\t"
        "movq   (%2, %0), %%mm1         \n\t"
        "paddb     %%mm0, %%mm1         \n\t"
        "movq      %%mm1, (%2, %0)      \n\t"
        "movq  8(%1, %0), %%mm0         \n\t"
        "movq  8(%2, %0), %%mm1         \n\t"
        "paddb     %%mm0, %%mm1         \n\t"
        "movq      %%mm1, 8(%2, %0)     \n\t"
        "add         $16, %0            \n\t"
        "2:                             \n\t"
        "cmp          %3, %0            \n\t"
        "js           1b                \n\t"
        : "+r"(i)
        : "r"(src), "r"(dst), "r"((x86_reg)w - 15)
    );
    for ( ; i < w; i++)
        dst[i + 0] += src[i + 0];
}

#if HAVE_7REGS
static void add_hfyu_median_prediction_cmov(uint8_t *dst, const uint8_t *top,
                                            const uint8_t *diff, int w,
                                            int *left, int *left_top)
{
    x86_reg w2 = -w;
    x86_reg x;
    int l  = *left     & 0xff;
    int tl = *left_top & 0xff;
    int t;
    __asm__ volatile (
        "mov          %7, %3            \n"
        "1:                             \n"
        "movzbl (%3, %4), %2            \n"
        "mov          %2, %k3           \n"
        "sub         %b1, %b3           \n"
        "add         %b0, %b3           \n"
        "mov          %2, %1            \n"
        "cmp          %0, %2            \n"
        "cmovg        %0, %2            \n"
        "cmovg        %1, %0            \n"
        "cmp         %k3, %0            \n"
        "cmovg       %k3, %0            \n"
        "mov          %7, %3            \n"
        "cmp          %2, %0            \n"
        "cmovl        %2, %0            \n"
        "add    (%6, %4), %b0           \n"
        "mov         %b0, (%5, %4)      \n"
        "inc          %4                \n"
        "jl           1b                \n"
        : "+&q"(l), "+&q"(tl), "=&r"(t), "=&q"(x), "+&r"(w2)
        : "r"(dst + w), "r"(diff + w), "rm"(top + w)
    );
    *left     = l;
    *left_top = tl;
}
#endif
#endif /* HAVE_INLINE_ASM */

void ff_h263_v_loop_filter_mmx(uint8_t *src, int stride, int qscale);
void ff_h263_h_loop_filter_mmx(uint8_t *src, int stride, int qscale);

#if HAVE_INLINE_ASM
/* Draw the edges of width 'w' of an image of size width, height
 * this MMX version can only handle w == 8 || w == 16. */
static void draw_edges_mmx(uint8_t *buf, int wrap, int width, int height,
                           int w, int h, int sides)
{
    uint8_t *ptr, *last_line;
    int i;

    last_line = buf + (height - 1) * wrap;
    /* left and right */
    ptr = buf;
    if (w == 8) {
        __asm__ volatile (
            "1:                             \n\t"
            "movd            (%0), %%mm0    \n\t"
            "punpcklbw      %%mm0, %%mm0    \n\t"
            "punpcklwd      %%mm0, %%mm0    \n\t"
            "punpckldq      %%mm0, %%mm0    \n\t"
            "movq           %%mm0, -8(%0)   \n\t"
            "movq      -8(%0, %2), %%mm1    \n\t"
            "punpckhbw      %%mm1, %%mm1    \n\t"
            "punpckhwd      %%mm1, %%mm1    \n\t"
            "punpckhdq      %%mm1, %%mm1    \n\t"
            "movq           %%mm1, (%0, %2) \n\t"
            "add               %1, %0       \n\t"
            "cmp               %3, %0       \n\t"
            "jb                1b           \n\t"
            : "+r"(ptr)
            : "r"((x86_reg)wrap), "r"((x86_reg)width), "r"(ptr + wrap * height)
            );
    } else if(w==16){
        __asm__ volatile (
            "1:                                 \n\t"
            "movd            (%0), %%mm0        \n\t"
            "punpcklbw      %%mm0, %%mm0        \n\t"
            "punpcklwd      %%mm0, %%mm0        \n\t"
            "punpckldq      %%mm0, %%mm0        \n\t"
            "movq           %%mm0, -8(%0)       \n\t"
            "movq           %%mm0, -16(%0)      \n\t"
            "movq      -8(%0, %2), %%mm1        \n\t"
            "punpckhbw      %%mm1, %%mm1        \n\t"
            "punpckhwd      %%mm1, %%mm1        \n\t"
            "punpckhdq      %%mm1, %%mm1        \n\t"
            "movq           %%mm1,  (%0, %2)    \n\t"
            "movq           %%mm1, 8(%0, %2)    \n\t"
            "add               %1, %0           \n\t"
            "cmp               %3, %0           \n\t"
            "jb                1b               \n\t"
            : "+r"(ptr)
            : "r"((x86_reg)wrap), "r"((x86_reg)width), "r"(ptr + wrap * height)
            );
    } else {
        av_assert1(w == 4);
        __asm__ volatile (
            "1:                             \n\t"
            "movd            (%0), %%mm0    \n\t"
            "punpcklbw      %%mm0, %%mm0    \n\t"
            "punpcklwd      %%mm0, %%mm0    \n\t"
            "movd           %%mm0, -4(%0)   \n\t"
            "movd      -4(%0, %2), %%mm1    \n\t"
            "punpcklbw      %%mm1, %%mm1    \n\t"
            "punpckhwd      %%mm1, %%mm1    \n\t"
            "punpckhdq      %%mm1, %%mm1    \n\t"
            "movd           %%mm1, (%0, %2) \n\t"
            "add               %1, %0       \n\t"
            "cmp               %3, %0       \n\t"
            "jb                1b           \n\t"
            : "+r"(ptr)
            : "r"((x86_reg)wrap), "r"((x86_reg)width), "r"(ptr + wrap * height)
            );
    }

    /* top and bottom (and hopefully also the corners) */
    if (sides & EDGE_TOP) {
        for (i = 0; i < h; i += 4) {
            ptr = buf - (i + 1) * wrap - w;
            __asm__ volatile (
                "1:                             \n\t"
                "movq (%1, %0), %%mm0           \n\t"
                "movq    %%mm0, (%0)            \n\t"
                "movq    %%mm0, (%0, %2)        \n\t"
                "movq    %%mm0, (%0, %2, 2)     \n\t"
                "movq    %%mm0, (%0, %3)        \n\t"
                "add        $8, %0              \n\t"
                "cmp        %4, %0              \n\t"
                "jb         1b                  \n\t"
                : "+r"(ptr)
                : "r"((x86_reg)buf - (x86_reg)ptr - w), "r"((x86_reg) -wrap),
                  "r"((x86_reg) -wrap * 3), "r"(ptr + width + 2 * w)
                );
        }
    }

    if (sides & EDGE_BOTTOM) {
        for (i = 0; i < h; i += 4) {
            ptr = last_line + (i + 1) * wrap - w;
            __asm__ volatile (
                "1:                             \n\t"
                "movq (%1, %0), %%mm0           \n\t"
                "movq    %%mm0, (%0)            \n\t"
                "movq    %%mm0, (%0, %2)        \n\t"
                "movq    %%mm0, (%0, %2, 2)     \n\t"
                "movq    %%mm0, (%0, %3)        \n\t"
                "add        $8, %0              \n\t"
                "cmp        %4, %0              \n\t"
                "jb         1b                  \n\t"
                : "+r"(ptr)
                : "r"((x86_reg)last_line - (x86_reg)ptr - w),
                  "r"((x86_reg)wrap), "r"((x86_reg)wrap * 3),
                  "r"(ptr + width + 2 * w)
                );
        }
    }
}
#endif /* HAVE_INLINE_ASM */


#if HAVE_YASM
#define QPEL_OP(OPNAME, ROUNDER, RND, MMX)                              \
static void OPNAME ## qpel8_mc00_ ## MMX (uint8_t *dst, uint8_t *src,   \
                                          int stride)                   \
{                                                                       \
    ff_ ## OPNAME ## pixels8_ ## MMX(dst, src, stride, 8);              \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc10_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         int stride)                    \
{                                                                       \
    uint64_t temp[8];                                                   \
    uint8_t * const half = (uint8_t*)temp;                              \
    ff_put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(half, src, 8,        \
                                                   stride, 8);          \
    ff_ ## OPNAME ## pixels8_l2_ ## MMX(dst, src, half,                 \
                                        stride, stride, 8);             \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc20_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         int stride)                    \
{                                                                       \
    ff_ ## OPNAME ## mpeg4_qpel8_h_lowpass_ ## MMX(dst, src, stride,    \
                                                   stride, 8);          \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc30_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         int stride)                    \
{                                                                       \
    uint64_t temp[8];                                                   \
    uint8_t * const half = (uint8_t*)temp;                              \
    ff_put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(half, src, 8,        \
                                                   stride, 8);          \
    ff_ ## OPNAME ## pixels8_l2_ ## MMX(dst, src + 1, half, stride,     \
                                        stride, 8);                     \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc01_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         int stride)                    \
{                                                                       \
    uint64_t temp[8];                                                   \
    uint8_t * const half = (uint8_t*)temp;                              \
    ff_put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(half, src,           \
                                                   8, stride);          \
    ff_ ## OPNAME ## pixels8_l2_ ## MMX(dst, src, half,                 \
                                        stride, stride, 8);             \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc02_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         int stride)                    \
{                                                                       \
    ff_ ## OPNAME ## mpeg4_qpel8_v_lowpass_ ## MMX(dst, src,            \
                                                   stride, stride);     \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc03_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         int stride)                    \
{                                                                       \
    uint64_t temp[8];                                                   \
    uint8_t * const half = (uint8_t*)temp;                              \
    ff_put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(half, src,           \
                                                   8, stride);          \
    ff_ ## OPNAME ## pixels8_l2_ ## MMX(dst, src + stride, half, stride,\
                                        stride, 8);                     \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc11_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         int stride)                    \
{                                                                       \
    uint64_t half[8 + 9];                                               \
    uint8_t * const halfH  = ((uint8_t*)half) + 64;                     \
    uint8_t * const halfHV = ((uint8_t*)half);                          \
    ff_put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8,       \
                                                   stride, 9);          \
    ff_put ## RND ## pixels8_l2_ ## MMX(halfH, src, halfH, 8,           \
                                        stride, 9);                     \
    ff_put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(halfHV, halfH, 8, 8);\
    ff_ ## OPNAME ## pixels8_l2_ ## MMX(dst, halfH, halfHV,             \
                                        stride, 8, 8);                  \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc31_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         int stride)                    \
{                                                                       \
    uint64_t half[8 + 9];                                               \
    uint8_t * const halfH  = ((uint8_t*)half) + 64;                     \
    uint8_t * const halfHV = ((uint8_t*)half);                          \
    ff_put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8,       \
                                                   stride, 9);          \
    ff_put ## RND ## pixels8_l2_ ## MMX(halfH, src + 1, halfH, 8,       \
                                        stride, 9);                     \
    ff_put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(halfHV, halfH, 8, 8);\
    ff_ ## OPNAME ## pixels8_l2_ ## MMX(dst, halfH, halfHV,             \
                                        stride, 8, 8);                  \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc13_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         int stride)                    \
{                                                                       \
    uint64_t half[8 + 9];                                               \
    uint8_t * const halfH  = ((uint8_t*)half) + 64;                     \
    uint8_t * const halfHV = ((uint8_t*)half);                          \
    ff_put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8,       \
                                                   stride, 9);          \
    ff_put ## RND ## pixels8_l2_ ## MMX(halfH, src, halfH, 8,           \
                                        stride, 9);                     \
    ff_put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(halfHV, halfH, 8, 8);\
    ff_ ## OPNAME ## pixels8_l2_ ## MMX(dst, halfH + 8, halfHV,         \
                                        stride, 8, 8);                  \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc33_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         int stride)                    \
{                                                                       \
    uint64_t half[8 + 9];                                               \
    uint8_t * const halfH  = ((uint8_t*)half) + 64;                     \
    uint8_t * const halfHV = ((uint8_t*)half);                          \
    ff_put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8,       \
                                                   stride, 9);          \
    ff_put ## RND ## pixels8_l2_ ## MMX(halfH, src + 1, halfH, 8,       \
                                        stride, 9);                     \
    ff_put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(halfHV, halfH, 8, 8);\
    ff_ ## OPNAME ## pixels8_l2_ ## MMX(dst, halfH + 8, halfHV,         \
                                        stride, 8, 8);                  \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc21_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         int stride)                    \
{                                                                       \
    uint64_t half[8 + 9];                                               \
    uint8_t * const halfH  = ((uint8_t*)half) + 64;                     \
    uint8_t * const halfHV = ((uint8_t*)half);                          \
    ff_put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8,       \
                                                   stride, 9);          \
    ff_put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(halfHV, halfH, 8, 8);\
    ff_ ## OPNAME ## pixels8_l2_ ## MMX(dst, halfH, halfHV,             \
                                        stride, 8, 8);                  \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc23_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         int stride)                    \
{                                                                       \
    uint64_t half[8 + 9];                                               \
    uint8_t * const halfH  = ((uint8_t*)half) + 64;                     \
    uint8_t * const halfHV = ((uint8_t*)half);                          \
    ff_put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8,       \
                                                   stride, 9);          \
    ff_put ## RND ## mpeg4_qpel8_v_lowpass_ ## MMX(halfHV, halfH, 8, 8);\
    ff_ ## OPNAME ## pixels8_l2_ ## MMX(dst, halfH + 8, halfHV,         \
                                        stride, 8, 8);                  \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc12_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         int stride)                    \
{                                                                       \
    uint64_t half[8 + 9];                                               \
    uint8_t * const halfH = ((uint8_t*)half);                           \
    ff_put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8,       \
                                                   stride, 9);          \
    ff_put ## RND ## pixels8_l2_ ## MMX(halfH, src, halfH,              \
                                        8, stride, 9);                  \
    ff_ ## OPNAME ## mpeg4_qpel8_v_lowpass_ ## MMX(dst, halfH,          \
                                                   stride, 8);          \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc32_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         int stride)                    \
{                                                                       \
    uint64_t half[8 + 9];                                               \
    uint8_t * const halfH = ((uint8_t*)half);                           \
    ff_put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8,       \
                                                   stride, 9);          \
    ff_put ## RND ## pixels8_l2_ ## MMX(halfH, src + 1, halfH, 8,       \
                                        stride, 9);                     \
    ff_ ## OPNAME ## mpeg4_qpel8_v_lowpass_ ## MMX(dst, halfH,          \
                                                   stride, 8);          \
}                                                                       \
                                                                        \
static void OPNAME ## qpel8_mc22_ ## MMX(uint8_t *dst, uint8_t *src,    \
                                         int stride)                    \
{                                                                       \
    uint64_t half[9];                                                   \
    uint8_t * const halfH = ((uint8_t*)half);                           \
    ff_put ## RND ## mpeg4_qpel8_h_lowpass_ ## MMX(halfH, src, 8,       \
                                                   stride, 9);          \
    ff_ ## OPNAME ## mpeg4_qpel8_v_lowpass_ ## MMX(dst, halfH,          \
                                                   stride, 8);          \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc00_ ## MMX (uint8_t *dst, uint8_t *src,  \
                                           int stride)                  \
{                                                                       \
    ff_ ## OPNAME ## pixels16_ ## MMX(dst, src, stride, 16);            \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc10_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          int stride)                   \
{                                                                       \
    uint64_t temp[32];                                                  \
    uint8_t * const half = (uint8_t*)temp;                              \
    ff_put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(half, src, 16,      \
                                                    stride, 16);        \
    ff_ ## OPNAME ## pixels16_l2_ ## MMX(dst, src, half, stride,        \
                                         stride, 16);                   \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc20_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          int stride)                   \
{                                                                       \
    ff_ ## OPNAME ## mpeg4_qpel16_h_lowpass_ ## MMX(dst, src,           \
                                                    stride, stride, 16);\
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc30_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          int stride)                   \
{                                                                       \
    uint64_t temp[32];                                                  \
    uint8_t * const half = (uint8_t*)temp;                              \
    ff_put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(half, src, 16,      \
                                                    stride, 16);        \
    ff_ ## OPNAME ## pixels16_l2_ ## MMX(dst, src + 1, half,            \
                                         stride, stride, 16);           \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc01_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          int stride)                   \
{                                                                       \
    uint64_t temp[32];                                                  \
    uint8_t * const half = (uint8_t*)temp;                              \
    ff_put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(half, src, 16,      \
                                                    stride);            \
    ff_ ## OPNAME ## pixels16_l2_ ## MMX(dst, src, half, stride,        \
                                         stride, 16);                   \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc02_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          int stride)                   \
{                                                                       \
    ff_ ## OPNAME ## mpeg4_qpel16_v_lowpass_ ## MMX(dst, src,           \
                                                    stride, stride);    \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc03_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          int stride)                   \
{                                                                       \
    uint64_t temp[32];                                                  \
    uint8_t * const half = (uint8_t*)temp;                              \
    ff_put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(half, src, 16,      \
                                                    stride);            \
    ff_ ## OPNAME ## pixels16_l2_ ## MMX(dst, src+stride, half,         \
                                         stride, stride, 16);           \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc11_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          int stride)                   \
{                                                                       \
    uint64_t half[16 * 2 + 17 * 2];                                     \
    uint8_t * const halfH  = ((uint8_t*)half) + 256;                    \
    uint8_t * const halfHV = ((uint8_t*)half);                          \
    ff_put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16,     \
                                                    stride, 17);        \
    ff_put ## RND ## pixels16_l2_ ## MMX(halfH, src, halfH, 16,         \
                                         stride, 17);                   \
    ff_put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(halfHV, halfH,      \
                                                    16, 16);            \
    ff_ ## OPNAME ## pixels16_l2_ ## MMX(dst, halfH, halfHV,            \
                                         stride, 16, 16);               \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc31_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          int stride)                   \
{                                                                       \
    uint64_t half[16 * 2 + 17 * 2];                                     \
    uint8_t * const halfH  = ((uint8_t*)half) + 256;                    \
    uint8_t * const halfHV = ((uint8_t*)half);                          \
    ff_put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16,     \
                                                    stride, 17);        \
    ff_put ## RND ## pixels16_l2_ ## MMX(halfH, src + 1, halfH, 16,     \
                                         stride, 17);                   \
    ff_put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(halfHV, halfH,      \
                                                    16, 16);            \
    ff_ ## OPNAME ## pixels16_l2_ ## MMX(dst, halfH, halfHV,            \
                                         stride, 16, 16);               \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc13_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          int stride)                   \
{                                                                       \
    uint64_t half[16 * 2 + 17 * 2];                                     \
    uint8_t * const halfH  = ((uint8_t*)half) + 256;                    \
    uint8_t * const halfHV = ((uint8_t*)half);                          \
    ff_put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16,     \
                                                    stride, 17);        \
    ff_put ## RND ## pixels16_l2_ ## MMX(halfH, src, halfH, 16,         \
                                         stride, 17);                   \
    ff_put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(halfHV, halfH,      \
                                                    16, 16);            \
    ff_ ## OPNAME ## pixels16_l2_ ## MMX(dst, halfH + 16, halfHV,       \
                                         stride, 16, 16);               \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc33_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          int stride)                   \
{                                                                       \
    uint64_t half[16 * 2 + 17 * 2];                                     \
    uint8_t * const halfH  = ((uint8_t*)half) + 256;                    \
    uint8_t * const halfHV = ((uint8_t*)half);                          \
    ff_put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16,     \
                                                    stride, 17);        \
    ff_put ## RND ## pixels16_l2_ ## MMX(halfH, src + 1, halfH, 16,     \
                                         stride, 17);                   \
    ff_put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(halfHV, halfH,      \
                                                    16, 16);            \
    ff_ ## OPNAME ## pixels16_l2_ ## MMX(dst, halfH + 16, halfHV,       \
                                         stride, 16, 16);               \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc21_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          int stride)                   \
{                                                                       \
    uint64_t half[16 * 2 + 17 * 2];                                     \
    uint8_t * const halfH  = ((uint8_t*)half) + 256;                    \
    uint8_t * const halfHV = ((uint8_t*)half);                          \
    ff_put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16,     \
                                                    stride, 17);        \
    ff_put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(halfHV, halfH,      \
                                                    16, 16);            \
    ff_ ## OPNAME ## pixels16_l2_ ## MMX(dst, halfH, halfHV,            \
                                         stride, 16, 16);               \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc23_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          int stride)                   \
{                                                                       \
    uint64_t half[16 * 2 + 17 * 2];                                     \
    uint8_t * const halfH  = ((uint8_t*)half) + 256;                    \
    uint8_t * const halfHV = ((uint8_t*)half);                          \
    ff_put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16,     \
                                                    stride, 17);        \
    ff_put ## RND ## mpeg4_qpel16_v_lowpass_ ## MMX(halfHV, halfH,      \
                                                    16, 16);            \
    ff_ ## OPNAME ## pixels16_l2_ ## MMX(dst, halfH + 16, halfHV,       \
                                         stride, 16, 16);               \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc12_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          int stride)                   \
{                                                                       \
    uint64_t half[17 * 2];                                              \
    uint8_t * const halfH = ((uint8_t*)half);                           \
    ff_put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16,     \
                                                    stride, 17);        \
    ff_put ## RND ## pixels16_l2_ ## MMX(halfH, src, halfH, 16,         \
                                         stride, 17);                   \
    ff_ ## OPNAME ## mpeg4_qpel16_v_lowpass_ ## MMX(dst, halfH,         \
                                                    stride, 16);        \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc32_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          int stride)                   \
{                                                                       \
    uint64_t half[17 * 2];                                              \
    uint8_t * const halfH = ((uint8_t*)half);                           \
    ff_put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16,     \
                                                    stride, 17);        \
    ff_put ## RND ## pixels16_l2_ ## MMX(halfH, src + 1, halfH, 16,     \
                                         stride, 17);                   \
    ff_ ## OPNAME ## mpeg4_qpel16_v_lowpass_ ## MMX(dst, halfH,         \
                                                    stride, 16);        \
}                                                                       \
                                                                        \
static void OPNAME ## qpel16_mc22_ ## MMX(uint8_t *dst, uint8_t *src,   \
                                          int stride)                   \
{                                                                       \
    uint64_t half[17 * 2];                                              \
    uint8_t * const halfH = ((uint8_t*)half);                           \
    ff_put ## RND ## mpeg4_qpel16_h_lowpass_ ## MMX(halfH, src, 16,     \
                                                    stride, 17);        \
    ff_ ## OPNAME ## mpeg4_qpel16_v_lowpass_ ## MMX(dst, halfH,         \
                                                    stride, 16);        \
}

QPEL_OP(put_,          ff_pw_16, _,        mmxext)
QPEL_OP(avg_,          ff_pw_16, _,        mmxext)
QPEL_OP(put_no_rnd_,   ff_pw_15, _no_rnd_, mmxext)
#endif /* HAVE_YASM */


#if HAVE_INLINE_ASM
void ff_put_rv40_qpel8_mc33_mmx(uint8_t *dst, uint8_t *src, int stride)
{
  put_pixels8_xy2_mmx(dst, src, stride, 8);
}
void ff_put_rv40_qpel16_mc33_mmx(uint8_t *dst, uint8_t *src, int stride)
{
  put_pixels16_xy2_mmx(dst, src, stride, 16);
}
void ff_avg_rv40_qpel8_mc33_mmx(uint8_t *dst, uint8_t *src, int stride)
{
  avg_pixels8_xy2_mmx(dst, src, stride, 8);
}
void ff_avg_rv40_qpel16_mc33_mmx(uint8_t *dst, uint8_t *src, int stride)
{
  avg_pixels16_xy2_mmx(dst, src, stride, 16);
}

typedef void emulated_edge_mc_func(uint8_t *dst, const uint8_t *src,
                                   ptrdiff_t linesize, int block_w, int block_h,
                                   int src_x, int src_y, int w, int h);

static av_always_inline void gmc(uint8_t *dst, uint8_t *src,
                                 int stride, int h, int ox, int oy,
                                 int dxx, int dxy, int dyx, int dyy,
                                 int shift, int r, int width, int height,
                                 emulated_edge_mc_func *emu_edge_fn)
{
    const int w    = 8;
    const int ix   = ox  >> (16 + shift);
    const int iy   = oy  >> (16 + shift);
    const int oxs  = ox  >> 4;
    const int oys  = oy  >> 4;
    const int dxxs = dxx >> 4;
    const int dxys = dxy >> 4;
    const int dyxs = dyx >> 4;
    const int dyys = dyy >> 4;
    const uint16_t r4[4]   = { r, r, r, r };
    const uint16_t dxy4[4] = { dxys, dxys, dxys, dxys };
    const uint16_t dyy4[4] = { dyys, dyys, dyys, dyys };
    const uint64_t shift2 = 2 * shift;
#define MAX_STRIDE 4096U
#define MAX_H 8U
    uint8_t edge_buf[(MAX_H + 1) * MAX_STRIDE];
    int x, y;

    const int dxw = (dxx - (1 << (16 + shift))) * (w - 1);
    const int dyh = (dyy - (1 << (16 + shift))) * (h - 1);
    const int dxh = dxy * (h - 1);
    const int dyw = dyx * (w - 1);
    int need_emu =  (unsigned)ix >= width  - w ||
                    (unsigned)iy >= height - h;

    if ( // non-constant fullpel offset (3% of blocks)
        ((ox ^ (ox + dxw)) | (ox ^ (ox + dxh)) | (ox ^ (ox + dxw + dxh)) |
         (oy ^ (oy + dyw)) | (oy ^ (oy + dyh)) | (oy ^ (oy + dyw + dyh))) >> (16 + shift)
        // uses more than 16 bits of subpel mv (only at huge resolution)
        || (dxx | dxy | dyx | dyy) & 15
        || (need_emu && (h > MAX_H || stride > MAX_STRIDE))) {
        // FIXME could still use mmx for some of the rows
        ff_gmc_c(dst, src, stride, h, ox, oy, dxx, dxy, dyx, dyy,
                 shift, r, width, height);
        return;
    }

    src += ix + iy * stride;
    if (need_emu) {
        emu_edge_fn(edge_buf, src, stride, w + 1, h + 1, ix, iy, width, height);
        src = edge_buf;
    }

    __asm__ volatile (
        "movd         %0, %%mm6         \n\t"
        "pxor      %%mm7, %%mm7         \n\t"
        "punpcklwd %%mm6, %%mm6         \n\t"
        "punpcklwd %%mm6, %%mm6         \n\t"
        :: "r"(1<<shift)
    );

    for (x = 0; x < w; x += 4) {
        uint16_t dx4[4] = { oxs - dxys + dxxs * (x + 0),
                            oxs - dxys + dxxs * (x + 1),
                            oxs - dxys + dxxs * (x + 2),
                            oxs - dxys + dxxs * (x + 3) };
        uint16_t dy4[4] = { oys - dyys + dyxs * (x + 0),
                            oys - dyys + dyxs * (x + 1),
                            oys - dyys + dyxs * (x + 2),
                            oys - dyys + dyxs * (x + 3) };

        for (y = 0; y < h; y++) {
            __asm__ volatile (
                "movq      %0, %%mm4    \n\t"
                "movq      %1, %%mm5    \n\t"
                "paddw     %2, %%mm4    \n\t"
                "paddw     %3, %%mm5    \n\t"
                "movq   %%mm4, %0       \n\t"
                "movq   %%mm5, %1       \n\t"
                "psrlw    $12, %%mm4    \n\t"
                "psrlw    $12, %%mm5    \n\t"
                : "+m"(*dx4), "+m"(*dy4)
                : "m"(*dxy4), "m"(*dyy4)
            );

            __asm__ volatile (
                "movq      %%mm6, %%mm2 \n\t"
                "movq      %%mm6, %%mm1 \n\t"
                "psubw     %%mm4, %%mm2 \n\t"
                "psubw     %%mm5, %%mm1 \n\t"
                "movq      %%mm2, %%mm0 \n\t"
                "movq      %%mm4, %%mm3 \n\t"
                "pmullw    %%mm1, %%mm0 \n\t" // (s - dx) * (s - dy)
                "pmullw    %%mm5, %%mm3 \n\t" // dx * dy
                "pmullw    %%mm5, %%mm2 \n\t" // (s - dx) * dy
                "pmullw    %%mm4, %%mm1 \n\t" // dx * (s - dy)

                "movd         %4, %%mm5 \n\t"
                "movd         %3, %%mm4 \n\t"
                "punpcklbw %%mm7, %%mm5 \n\t"
                "punpcklbw %%mm7, %%mm4 \n\t"
                "pmullw    %%mm5, %%mm3 \n\t" // src[1, 1] * dx * dy
                "pmullw    %%mm4, %%mm2 \n\t" // src[0, 1] * (s - dx) * dy

                "movd         %2, %%mm5 \n\t"
                "movd         %1, %%mm4 \n\t"
                "punpcklbw %%mm7, %%mm5 \n\t"
                "punpcklbw %%mm7, %%mm4 \n\t"
                "pmullw    %%mm5, %%mm1 \n\t" // src[1, 0] * dx * (s - dy)
                "pmullw    %%mm4, %%mm0 \n\t" // src[0, 0] * (s - dx) * (s - dy)
                "paddw        %5, %%mm1 \n\t"
                "paddw     %%mm3, %%mm2 \n\t"
                "paddw     %%mm1, %%mm0 \n\t"
                "paddw     %%mm2, %%mm0 \n\t"

                "psrlw        %6, %%mm0 \n\t"
                "packuswb  %%mm0, %%mm0 \n\t"
                "movd      %%mm0, %0    \n\t"

                : "=m"(dst[x + y * stride])
                : "m"(src[0]), "m"(src[1]),
                  "m"(src[stride]), "m"(src[stride + 1]),
                  "m"(*r4), "m"(shift2)
            );
            src += stride;
        }
        src += 4 - h * stride;
    }
}

#if CONFIG_VIDEODSP
#if HAVE_YASM
#if ARCH_X86_32
static void gmc_mmx(uint8_t *dst, uint8_t *src,
                    int stride, int h, int ox, int oy,
                    int dxx, int dxy, int dyx, int dyy,
                    int shift, int r, int width, int height)
{
    gmc(dst, src, stride, h, ox, oy, dxx, dxy, dyx, dyy, shift, r,
        width, height, &ff_emulated_edge_mc_8);
}
#endif
static void gmc_sse(uint8_t *dst, uint8_t *src,
                    int stride, int h, int ox, int oy,
                    int dxx, int dxy, int dyx, int dyy,
                    int shift, int r, int width, int height)
{
    gmc(dst, src, stride, h, ox, oy, dxx, dxy, dyx, dyy, shift, r,
        width, height, &ff_emulated_edge_mc_8);
}
#else
static void gmc_mmx(uint8_t *dst, uint8_t *src,
                    int stride, int h, int ox, int oy,
                    int dxx, int dxy, int dyx, int dyy,
                    int shift, int r, int width, int height)
{
    gmc(dst, src, stride, h, ox, oy, dxx, dxy, dyx, dyy, shift, r,
        width, height, &ff_emulated_edge_mc_8);
}
#endif
#endif

#endif /* HAVE_INLINE_ASM */

void ff_put_pixels16_sse2(uint8_t *block, const uint8_t *pixels,
                          ptrdiff_t line_size, int h);
void ff_avg_pixels16_sse2(uint8_t *block, const uint8_t *pixels,
                          ptrdiff_t line_size, int h);

#if HAVE_INLINE_ASM

/* CAVS-specific */
void ff_put_cavs_qpel8_mc00_mmxext(uint8_t *dst, uint8_t *src, int stride)
{
    put_pixels8_mmx(dst, src, stride, 8);
}

void ff_avg_cavs_qpel8_mc00_mmxext(uint8_t *dst, uint8_t *src, int stride)
{
    avg_pixels8_mmx(dst, src, stride, 8);
}

void ff_put_cavs_qpel16_mc00_mmxext(uint8_t *dst, uint8_t *src, int stride)
{
    put_pixels16_mmx(dst, src, stride, 16);
}

void ff_avg_cavs_qpel16_mc00_mmxext(uint8_t *dst, uint8_t *src, int stride)
{
    avg_pixels16_mmx(dst, src, stride, 16);
}

/* VC-1-specific */
void ff_put_vc1_mspel_mc00_mmx(uint8_t *dst, const uint8_t *src,
                               int stride, int rnd)
{
    put_pixels8_mmx(dst, src, stride, 8);
}

#if CONFIG_DIRAC_DECODER
#define DIRAC_PIXOP(OPNAME2, OPNAME, EXT)\
void ff_ ## OPNAME2 ## _dirac_pixels8_ ## EXT(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
    if (h&3)\
        ff_ ## OPNAME2 ## _dirac_pixels8_c(dst, src, stride, h);\
    else\
        OPNAME ## _pixels8_ ## EXT(dst, src[0], stride, h);\
}\
void ff_ ## OPNAME2 ## _dirac_pixels16_ ## EXT(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
    if (h&3)\
        ff_ ## OPNAME2 ## _dirac_pixels16_c(dst, src, stride, h);\
    else\
        OPNAME ## _pixels16_ ## EXT(dst, src[0], stride, h);\
}\
void ff_ ## OPNAME2 ## _dirac_pixels32_ ## EXT(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
    if (h&3) {\
        ff_ ## OPNAME2 ## _dirac_pixels32_c(dst, src, stride, h);\
    } else {\
        OPNAME ## _pixels16_ ## EXT(dst   , src[0]   , stride, h);\
        OPNAME ## _pixels16_ ## EXT(dst+16, src[0]+16, stride, h);\
    }\
}

#if HAVE_MMX_INLINE
DIRAC_PIXOP(put, put, mmx)
DIRAC_PIXOP(avg, avg, mmx)
#endif

#if HAVE_YASM
DIRAC_PIXOP(avg, ff_avg, mmxext)

void ff_put_dirac_pixels16_sse2(uint8_t *dst, const uint8_t *src[5], int stride, int h)
{
    if (h&3)
        ff_put_dirac_pixels16_c(dst, src, stride, h);
    else
    ff_put_pixels16_sse2(dst, src[0], stride, h);
}
void ff_avg_dirac_pixels16_sse2(uint8_t *dst, const uint8_t *src[5], int stride, int h)
{
    if (h&3)
        ff_avg_dirac_pixels16_c(dst, src, stride, h);
    else
    ff_avg_pixels16_sse2(dst, src[0], stride, h);
}
void ff_put_dirac_pixels32_sse2(uint8_t *dst, const uint8_t *src[5], int stride, int h)
{
    if (h&3) {
        ff_put_dirac_pixels32_c(dst, src, stride, h);
    } else {
    ff_put_pixels16_sse2(dst   , src[0]   , stride, h);
    ff_put_pixels16_sse2(dst+16, src[0]+16, stride, h);
    }
}
void ff_avg_dirac_pixels32_sse2(uint8_t *dst, const uint8_t *src[5], int stride, int h)
{
    if (h&3) {
        ff_avg_dirac_pixels32_c(dst, src, stride, h);
    } else {
    ff_avg_pixels16_sse2(dst   , src[0]   , stride, h);
    ff_avg_pixels16_sse2(dst+16, src[0]+16, stride, h);
    }
}
#endif
#endif

/* XXX: Those functions should be suppressed ASAP when all IDCTs are
 * converted. */
#if CONFIG_GPL
static void ff_libmpeg2mmx_idct_put(uint8_t *dest, int line_size,
                                    int16_t *block)
{
    ff_mmx_idct(block);
    ff_put_pixels_clamped_mmx(block, dest, line_size);
}

static void ff_libmpeg2mmx_idct_add(uint8_t *dest, int line_size,
                                    int16_t *block)
{
    ff_mmx_idct(block);
    ff_add_pixels_clamped_mmx(block, dest, line_size);
}

static void ff_libmpeg2mmx2_idct_put(uint8_t *dest, int line_size,
                                     int16_t *block)
{
    ff_mmxext_idct(block);
    ff_put_pixels_clamped_mmx(block, dest, line_size);
}

static void ff_libmpeg2mmx2_idct_add(uint8_t *dest, int line_size,
                                     int16_t *block)
{
    ff_mmxext_idct(block);
    ff_add_pixels_clamped_mmx(block, dest, line_size);
}
#endif

static void vector_clipf_sse(float *dst, const float *src,
                             float min, float max, int len)
{
    x86_reg i = (len - 16) * 4;
    __asm__ volatile (
        "movss          %3, %%xmm4      \n\t"
        "movss          %4, %%xmm5      \n\t"
        "shufps $0, %%xmm4, %%xmm4      \n\t"
        "shufps $0, %%xmm5, %%xmm5      \n\t"
        "1:                             \n\t"
        "movaps   (%2, %0), %%xmm0      \n\t" // 3/1 on intel
        "movaps 16(%2, %0), %%xmm1      \n\t"
        "movaps 32(%2, %0), %%xmm2      \n\t"
        "movaps 48(%2, %0), %%xmm3      \n\t"
        "maxps      %%xmm4, %%xmm0      \n\t"
        "maxps      %%xmm4, %%xmm1      \n\t"
        "maxps      %%xmm4, %%xmm2      \n\t"
        "maxps      %%xmm4, %%xmm3      \n\t"
        "minps      %%xmm5, %%xmm0      \n\t"
        "minps      %%xmm5, %%xmm1      \n\t"
        "minps      %%xmm5, %%xmm2      \n\t"
        "minps      %%xmm5, %%xmm3      \n\t"
        "movaps     %%xmm0,   (%1, %0)  \n\t"
        "movaps     %%xmm1, 16(%1, %0)  \n\t"
        "movaps     %%xmm2, 32(%1, %0)  \n\t"
        "movaps     %%xmm3, 48(%1, %0)  \n\t"
        "sub           $64, %0          \n\t"
        "jge            1b              \n\t"
        : "+&r"(i)
        : "r"(dst), "r"(src), "m"(min), "m"(max)
        : "memory"
    );
}

#endif /* HAVE_INLINE_ASM */

int32_t ff_scalarproduct_int16_mmxext(const int16_t *v1, const int16_t *v2,
                                      int order);
int32_t ff_scalarproduct_int16_sse2(const int16_t *v1, const int16_t *v2,
                                    int order);
int32_t ff_scalarproduct_and_madd_int16_mmxext(int16_t *v1, const int16_t *v2,
                                               const int16_t *v3,
                                               int order, int mul);
int32_t ff_scalarproduct_and_madd_int16_sse2(int16_t *v1, const int16_t *v2,
                                             const int16_t *v3,
                                             int order, int mul);
int32_t ff_scalarproduct_and_madd_int16_ssse3(int16_t *v1, const int16_t *v2,
                                              const int16_t *v3,
                                              int order, int mul);

void ff_apply_window_int16_round_mmxext(int16_t *output, const int16_t *input,
                                        const int16_t *window, unsigned int len);
void ff_apply_window_int16_round_sse2(int16_t *output, const int16_t *input,
                                      const int16_t *window, unsigned int len);
void ff_apply_window_int16_mmxext(int16_t *output, const int16_t *input,
                                  const int16_t *window, unsigned int len);
void ff_apply_window_int16_sse2(int16_t *output, const int16_t *input,
                                const int16_t *window, unsigned int len);
void ff_apply_window_int16_ssse3(int16_t *output, const int16_t *input,
                                 const int16_t *window, unsigned int len);
void ff_apply_window_int16_ssse3_atom(int16_t *output, const int16_t *input,
                                      const int16_t *window, unsigned int len);

void ff_bswap32_buf_ssse3(uint32_t *dst, const uint32_t *src, int w);
void ff_bswap32_buf_sse2(uint32_t *dst, const uint32_t *src, int w);

void ff_add_hfyu_median_prediction_mmxext(uint8_t *dst, const uint8_t *top,
                                          const uint8_t *diff, int w,
                                          int *left, int *left_top);
int  ff_add_hfyu_left_prediction_ssse3(uint8_t *dst, const uint8_t *src,
                                       int w, int left);
int  ff_add_hfyu_left_prediction_sse4(uint8_t *dst, const uint8_t *src,
                                      int w, int left);

void ff_vector_clip_int32_mmx     (int32_t *dst, const int32_t *src,
                                   int32_t min, int32_t max, unsigned int len);
void ff_vector_clip_int32_sse2    (int32_t *dst, const int32_t *src,
                                   int32_t min, int32_t max, unsigned int len);
void ff_vector_clip_int32_int_sse2(int32_t *dst, const int32_t *src,
                                   int32_t min, int32_t max, unsigned int len);
void ff_vector_clip_int32_sse4    (int32_t *dst, const int32_t *src,
                                   int32_t min, int32_t max, unsigned int len);

#define SET_QPEL_FUNCS(PFX, IDX, SIZE, CPU, PREFIX)                          \
    do {                                                                     \
    c->PFX ## _pixels_tab[IDX][ 0] = PREFIX ## PFX ## SIZE ## _mc00_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][ 1] = PREFIX ## PFX ## SIZE ## _mc10_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][ 2] = PREFIX ## PFX ## SIZE ## _mc20_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][ 3] = PREFIX ## PFX ## SIZE ## _mc30_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][ 4] = PREFIX ## PFX ## SIZE ## _mc01_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][ 5] = PREFIX ## PFX ## SIZE ## _mc11_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][ 6] = PREFIX ## PFX ## SIZE ## _mc21_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][ 7] = PREFIX ## PFX ## SIZE ## _mc31_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][ 8] = PREFIX ## PFX ## SIZE ## _mc02_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][ 9] = PREFIX ## PFX ## SIZE ## _mc12_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][10] = PREFIX ## PFX ## SIZE ## _mc22_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][11] = PREFIX ## PFX ## SIZE ## _mc32_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][12] = PREFIX ## PFX ## SIZE ## _mc03_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][13] = PREFIX ## PFX ## SIZE ## _mc13_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][14] = PREFIX ## PFX ## SIZE ## _mc23_ ## CPU; \
    c->PFX ## _pixels_tab[IDX][15] = PREFIX ## PFX ## SIZE ## _mc33_ ## CPU; \
    } while (0)

#define SET_HPEL_FUNCS(PFX, IDX, SIZE, CPU)                                     \
    do {                                                                        \
        c->PFX ## _pixels_tab IDX [0] = PFX ## _pixels ## SIZE ## _     ## CPU; \
        c->PFX ## _pixels_tab IDX [1] = PFX ## _pixels ## SIZE ## _x2_  ## CPU; \
        c->PFX ## _pixels_tab IDX [2] = PFX ## _pixels ## SIZE ## _y2_  ## CPU; \
        c->PFX ## _pixels_tab IDX [3] = PFX ## _pixels ## SIZE ## _xy2_ ## CPU; \
    } while (0)

static av_cold void dsputil_init_mmx(DSPContext *c, AVCodecContext *avctx,
                                     int mm_flags)
{
    const int high_bit_depth = avctx->bits_per_raw_sample > 8;

#if HAVE_INLINE_ASM
    c->put_pixels_clamped        = ff_put_pixels_clamped_mmx;
    c->put_signed_pixels_clamped = ff_put_signed_pixels_clamped_mmx;
    c->add_pixels_clamped        = ff_add_pixels_clamped_mmx;

    if (!high_bit_depth) {
        c->clear_block  = clear_block_mmx;
        c->clear_blocks = clear_blocks_mmx;
        c->draw_edges   = draw_edges_mmx;

        SET_HPEL_FUNCS(put,        [0], 16, mmx);
        SET_HPEL_FUNCS(put_no_rnd, [0], 16, mmx);
        SET_HPEL_FUNCS(avg,        [0], 16, mmx);
        SET_HPEL_FUNCS(avg_no_rnd,    , 16, mmx);
        SET_HPEL_FUNCS(put,        [1],  8, mmx);
        SET_HPEL_FUNCS(put_no_rnd, [1],  8, mmx);
        SET_HPEL_FUNCS(avg,        [1],  8, mmx);
    }

#if CONFIG_VIDEODSP && (ARCH_X86_32 || !HAVE_YASM)
    c->gmc = gmc_mmx;
#endif

    c->add_bytes = add_bytes_mmx;
#endif /* HAVE_INLINE_ASM */

#if HAVE_YASM
    if (CONFIG_H263_DECODER || CONFIG_H263_ENCODER) {
        c->h263_v_loop_filter = ff_h263_v_loop_filter_mmx;
        c->h263_h_loop_filter = ff_h263_h_loop_filter_mmx;
    }

    c->vector_clip_int32 = ff_vector_clip_int32_mmx;
#endif

}

static av_cold void dsputil_init_mmxext(DSPContext *c, AVCodecContext *avctx,
                                        int mm_flags)
{
    const int bit_depth      = avctx->bits_per_raw_sample;
    const int high_bit_depth = bit_depth > 8;

#if HAVE_YASM
    SET_QPEL_FUNCS(avg_qpel,        0, 16, mmxext, );
    SET_QPEL_FUNCS(avg_qpel,        1,  8, mmxext, );

    SET_QPEL_FUNCS(put_qpel,        0, 16, mmxext, );
    SET_QPEL_FUNCS(put_qpel,        1,  8, mmxext, );
    SET_QPEL_FUNCS(put_no_rnd_qpel, 0, 16, mmxext, );
    SET_QPEL_FUNCS(put_no_rnd_qpel, 1,  8, mmxext, );

    if (!high_bit_depth) {
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
    }

    if (!(avctx->flags & CODEC_FLAG_BITEXACT)) {
        if (!high_bit_depth) {
            c->put_no_rnd_pixels_tab[0][1] = ff_put_no_rnd_pixels16_x2_mmxext;
            c->put_no_rnd_pixels_tab[0][2] = ff_put_no_rnd_pixels16_y2_mmxext;
            c->put_no_rnd_pixels_tab[1][1] = ff_put_no_rnd_pixels8_x2_mmxext;
            c->put_no_rnd_pixels_tab[1][2] = ff_put_no_rnd_pixels8_y2_mmxext;

            c->avg_pixels_tab[0][3] = ff_avg_pixels16_xy2_mmxext;
            c->avg_pixels_tab[1][3] = ff_avg_pixels8_xy2_mmxext;
        }
    }
#endif /* HAVE_YASM */

#if HAVE_MMXEXT_EXTERNAL
    if (CONFIG_VP3_DECODER && (avctx->codec_id == AV_CODEC_ID_VP3 ||
                               avctx->codec_id == AV_CODEC_ID_THEORA)) {
        c->put_no_rnd_pixels_tab[1][1] = ff_put_no_rnd_pixels8_x2_exact_mmxext;
        c->put_no_rnd_pixels_tab[1][2] = ff_put_no_rnd_pixels8_y2_exact_mmxext;
    }

    /* slower than cmov version on AMD */
    if (!(mm_flags & AV_CPU_FLAG_3DNOW))
        c->add_hfyu_median_prediction = ff_add_hfyu_median_prediction_mmxext;

    c->scalarproduct_int16          = ff_scalarproduct_int16_mmxext;
    c->scalarproduct_and_madd_int16 = ff_scalarproduct_and_madd_int16_mmxext;

    if (avctx->flags & CODEC_FLAG_BITEXACT) {
        c->apply_window_int16 = ff_apply_window_int16_mmxext;
    } else {
        c->apply_window_int16 = ff_apply_window_int16_round_mmxext;
    }
#endif /* HAVE_MMXEXT_EXTERNAL */
}

static av_cold void dsputil_init_3dnow(DSPContext *c, AVCodecContext *avctx,
                                       int mm_flags)
{
    const int high_bit_depth = avctx->bits_per_raw_sample > 8;

#if HAVE_YASM
    if (!high_bit_depth) {
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

        if (!(avctx->flags & CODEC_FLAG_BITEXACT)){
            c->put_no_rnd_pixels_tab[0][1] = ff_put_no_rnd_pixels16_x2_3dnow;
            c->put_no_rnd_pixels_tab[0][2] = ff_put_no_rnd_pixels16_y2_3dnow;
            c->put_no_rnd_pixels_tab[1][1] = ff_put_no_rnd_pixels8_x2_3dnow;
            c->put_no_rnd_pixels_tab[1][2] = ff_put_no_rnd_pixels8_y2_3dnow;

            c->avg_pixels_tab[0][3] = ff_avg_pixels16_xy2_3dnow;
            c->avg_pixels_tab[1][3] = ff_avg_pixels8_xy2_3dnow;
        }
    }

    if (CONFIG_VP3_DECODER && (avctx->codec_id == AV_CODEC_ID_VP3 ||
                               avctx->codec_id == AV_CODEC_ID_THEORA)) {
        c->put_no_rnd_pixels_tab[1][1] = ff_put_no_rnd_pixels8_x2_exact_3dnow;
        c->put_no_rnd_pixels_tab[1][2] = ff_put_no_rnd_pixels8_y2_exact_3dnow;
    }
#endif /* HAVE_YASM */
}

static av_cold void dsputil_init_sse(DSPContext *c, AVCodecContext *avctx,
                                     int mm_flags)
{
    const int high_bit_depth = avctx->bits_per_raw_sample > 8;

#if HAVE_INLINE_ASM
    if (!high_bit_depth) {
        if (!(CONFIG_MPEG_XVMC_DECODER && avctx->xvmc_acceleration > 1)) {
            /* XvMCCreateBlocks() may not allocate 16-byte aligned blocks */
            c->clear_block  = clear_block_sse;
            c->clear_blocks = clear_blocks_sse;
        }
    }

    c->vector_clipf = vector_clipf_sse;
#endif /* HAVE_INLINE_ASM */

#if HAVE_YASM
#if HAVE_INLINE_ASM && CONFIG_VIDEODSP
    c->gmc = gmc_sse;
#endif
#endif /* HAVE_YASM */
}

static av_cold void dsputil_init_sse2(DSPContext *c, AVCodecContext *avctx,
                                      int mm_flags)
{
    const int bit_depth      = avctx->bits_per_raw_sample;
    const int high_bit_depth = bit_depth > 8;

#if HAVE_SSE2_INLINE
    if (!high_bit_depth && avctx->idct_algo == FF_IDCT_XVIDMMX && avctx->lowres == 0) {
        c->idct_put              = ff_idct_xvid_sse2_put;
        c->idct_add              = ff_idct_xvid_sse2_add;
        c->idct                  = ff_idct_xvid_sse2;
        c->idct_permutation_type = FF_SSE2_IDCT_PERM;
    }
#endif /* HAVE_SSE2_INLINE */

#if HAVE_SSE2_EXTERNAL
    if (!(mm_flags & AV_CPU_FLAG_SSE2SLOW)) {
        // these functions are slower than mmx on AMD, but faster on Intel
        if (!high_bit_depth) {
            c->put_pixels_tab[0][0]        = ff_put_pixels16_sse2;
            c->put_no_rnd_pixels_tab[0][0] = ff_put_pixels16_sse2;
            c->avg_pixels_tab[0][0]        = ff_avg_pixels16_sse2;
        }
    }

    c->scalarproduct_int16          = ff_scalarproduct_int16_sse2;
    c->scalarproduct_and_madd_int16 = ff_scalarproduct_and_madd_int16_sse2;
    if (mm_flags & AV_CPU_FLAG_ATOM) {
        c->vector_clip_int32 = ff_vector_clip_int32_int_sse2;
    } else {
        c->vector_clip_int32 = ff_vector_clip_int32_sse2;
    }
    if (avctx->flags & CODEC_FLAG_BITEXACT) {
        c->apply_window_int16 = ff_apply_window_int16_sse2;
    } else if (!(mm_flags & AV_CPU_FLAG_SSE2SLOW)) {
        c->apply_window_int16 = ff_apply_window_int16_round_sse2;
    }
    c->bswap_buf = ff_bswap32_buf_sse2;
#endif /* HAVE_SSE2_EXTERNAL */
}

static av_cold void dsputil_init_ssse3(DSPContext *c, AVCodecContext *avctx,
                                       int mm_flags)
{
#if HAVE_SSSE3_EXTERNAL
    c->add_hfyu_left_prediction = ff_add_hfyu_left_prediction_ssse3;
    if (mm_flags & AV_CPU_FLAG_SSE4) // not really sse4, just slow on Conroe
        c->add_hfyu_left_prediction = ff_add_hfyu_left_prediction_sse4;

    if (mm_flags & AV_CPU_FLAG_ATOM)
        c->apply_window_int16 = ff_apply_window_int16_ssse3_atom;
    else
        c->apply_window_int16 = ff_apply_window_int16_ssse3;
    if (!(mm_flags & (AV_CPU_FLAG_SSE42|AV_CPU_FLAG_3DNOW))) // cachesplit
        c->scalarproduct_and_madd_int16 = ff_scalarproduct_and_madd_int16_ssse3;
    c->bswap_buf = ff_bswap32_buf_ssse3;
#endif /* HAVE_SSSE3_EXTERNAL */
}

static av_cold void dsputil_init_sse4(DSPContext *c, AVCodecContext *avctx,
                                      int mm_flags)
{
#if HAVE_SSE4_EXTERNAL
    c->vector_clip_int32 = ff_vector_clip_int32_sse4;
#endif /* HAVE_SSE4_EXTERNAL */
}

av_cold void ff_dsputil_init_mmx(DSPContext *c, AVCodecContext *avctx)
{
    int mm_flags = av_get_cpu_flags();

#if HAVE_7REGS && HAVE_INLINE_ASM
    if (mm_flags & AV_CPU_FLAG_CMOV)
        c->add_hfyu_median_prediction = add_hfyu_median_prediction_cmov;
#endif

    if (mm_flags & AV_CPU_FLAG_MMX) {
#if HAVE_INLINE_ASM
        const int idct_algo = avctx->idct_algo;

        if (avctx->lowres == 0 && avctx->bits_per_raw_sample <= 8) {
            if (idct_algo == FF_IDCT_AUTO || idct_algo == FF_IDCT_SIMPLEMMX) {
                c->idct_put              = ff_simple_idct_put_mmx;
                c->idct_add              = ff_simple_idct_add_mmx;
                c->idct                  = ff_simple_idct_mmx;
                c->idct_permutation_type = FF_SIMPLE_IDCT_PERM;
#if CONFIG_GPL
            } else if (idct_algo == FF_IDCT_LIBMPEG2MMX) {
                if (mm_flags & AV_CPU_FLAG_MMX2) {
                    c->idct_put = ff_libmpeg2mmx2_idct_put;
                    c->idct_add = ff_libmpeg2mmx2_idct_add;
                    c->idct     = ff_mmxext_idct;
                } else {
                    c->idct_put = ff_libmpeg2mmx_idct_put;
                    c->idct_add = ff_libmpeg2mmx_idct_add;
                    c->idct     = ff_mmx_idct;
                }
                c->idct_permutation_type = FF_LIBMPEG2_IDCT_PERM;
#endif
            } else if (idct_algo == FF_IDCT_XVIDMMX) {
                if (mm_flags & AV_CPU_FLAG_SSE2) {
                    c->idct_put              = ff_idct_xvid_sse2_put;
                    c->idct_add              = ff_idct_xvid_sse2_add;
                    c->idct                  = ff_idct_xvid_sse2;
                    c->idct_permutation_type = FF_SSE2_IDCT_PERM;
                } else if (mm_flags & AV_CPU_FLAG_MMXEXT) {
                    c->idct_put              = ff_idct_xvid_mmxext_put;
                    c->idct_add              = ff_idct_xvid_mmxext_add;
                    c->idct                  = ff_idct_xvid_mmxext;
                } else {
                    c->idct_put              = ff_idct_xvid_mmx_put;
                    c->idct_add              = ff_idct_xvid_mmx_add;
                    c->idct                  = ff_idct_xvid_mmx;
                }
            }
        }
#endif /* HAVE_INLINE_ASM */

        dsputil_init_mmx(c, avctx, mm_flags);
    }

    if (mm_flags & AV_CPU_FLAG_MMXEXT)
        dsputil_init_mmxext(c, avctx, mm_flags);

    if (mm_flags & AV_CPU_FLAG_3DNOW)
        dsputil_init_3dnow(c, avctx, mm_flags);

    if (mm_flags & AV_CPU_FLAG_SSE)
        dsputil_init_sse(c, avctx, mm_flags);

    if (mm_flags & AV_CPU_FLAG_SSE2)
        dsputil_init_sse2(c, avctx, mm_flags);

    if (mm_flags & AV_CPU_FLAG_SSSE3)
        dsputil_init_ssse3(c, avctx, mm_flags);

    if (mm_flags & AV_CPU_FLAG_SSE4)
        dsputil_init_sse4(c, avctx, mm_flags);

    if (CONFIG_ENCODERS)
        ff_dsputilenc_init_mmx(c, avctx);
}
