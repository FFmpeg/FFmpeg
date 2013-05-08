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

#include <stddef.h>
#include <stdint.h>

#include "libavcodec/dsputil.h"
#include "libavutil/x86/asm.h"

extern const uint64_t ff_wtwo;

extern const xmm_reg  ff_pw_3;
extern const xmm_reg  ff_pw_4;
extern const xmm_reg  ff_pw_5;
extern const xmm_reg  ff_pw_8;
extern const uint64_t ff_pw_15;
extern const xmm_reg  ff_pw_16;
extern const xmm_reg  ff_pw_18;
extern const uint64_t ff_pw_20;
extern const xmm_reg  ff_pw_32;
extern const uint64_t ff_pw_42;
extern const uint64_t ff_pw_53;
extern const xmm_reg  ff_pw_64;
extern const uint64_t ff_pw_96;
extern const uint64_t ff_pw_128;
extern const uint64_t ff_pw_255;

extern const xmm_reg  ff_pb_1;
extern const xmm_reg  ff_pb_3;
extern const xmm_reg  ff_pb_F8;
extern const uint64_t ff_pb_FC;

extern const double ff_pd_1[2];
extern const double ff_pd_2[2];

#define SBUTTERFLY(a,b,t,n,m)\
    "mov" #m " " #a ", " #t "         \n\t" /* abcd */\
    "punpckl" #n " " #b ", " #a "     \n\t" /* aebf */\
    "punpckh" #n " " #b ", " #t "     \n\t" /* cgdh */\

#define TRANSPOSE4(a,b,c,d,t)\
    SBUTTERFLY(a,b,t,wd,q) /* a=aebf t=cgdh */\
    SBUTTERFLY(c,d,b,wd,q) /* c=imjn b=kolp */\
    SBUTTERFLY(a,c,d,dq,q) /* a=aeim d=bfjn */\
    SBUTTERFLY(t,b,c,dq,q) /* t=cgko c=dhlp */

#define MOVQ_WONE(regd) \
    __asm__ volatile ( \
    "pcmpeqd %%" #regd ", %%" #regd " \n\t" \
    "psrlw $15, %%" #regd ::)

#define JUMPALIGN()     __asm__ volatile (".p2align 3"::)
#define MOVQ_ZERO(regd) __asm__ volatile ("pxor %%"#regd", %%"#regd ::)

#define MOVQ_BFE(regd)                                  \
    __asm__ volatile (                                  \
        "pcmpeqd %%"#regd", %%"#regd"   \n\t"           \
        "paddb   %%"#regd", %%"#regd"   \n\t" ::)

#ifndef PIC
#define MOVQ_WTWO(regd) __asm__ volatile ("movq %0, %%"#regd" \n\t" :: "m"(ff_wtwo))
#else
// for shared library it's better to use this way for accessing constants
// pcmpeqd -> -1
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

void ff_dsputilenc_init_mmx(DSPContext* c, AVCodecContext *avctx);
void ff_dsputil_init_pix_mmx(DSPContext* c, AVCodecContext *avctx);

void ff_add_pixels_clamped_mmx(const int16_t *block, uint8_t *pixels, int line_size);
void ff_put_pixels_clamped_mmx(const int16_t *block, uint8_t *pixels, int line_size);
void ff_put_signed_pixels_clamped_mmx(const int16_t *block, uint8_t *pixels, int line_size);


void ff_avg_pixels8_mmx(uint8_t *block, const uint8_t *pixels,
                        ptrdiff_t line_size, int h);
void ff_avg_pixels16_mmx(uint8_t *block, const uint8_t *pixels,
                         ptrdiff_t line_size, int h);
void ff_put_pixels8_mmx(uint8_t *block, const uint8_t *pixels,
                        ptrdiff_t line_size, int h);
void ff_put_pixels16_mmx(uint8_t *block, const uint8_t *pixels,
                         ptrdiff_t line_size, int h);
void ff_avg_pixels8_mmxext(uint8_t *block, const uint8_t *pixels,
                           ptrdiff_t line_size, int h);
void ff_put_pixels8_mmxext(uint8_t *block, const uint8_t *pixels,
                           ptrdiff_t line_size, int h);
void ff_avg_pixels16_sse2(uint8_t *block, const uint8_t *pixels,
                          ptrdiff_t line_size, int h);
void ff_put_pixels16_sse2(uint8_t *block, const uint8_t *pixels,
                          ptrdiff_t line_size, int h);

void ff_avg_pixels8_x2_mmx(uint8_t *block, const uint8_t *pixels,
                           ptrdiff_t line_size, int h);

void ff_avg_pixels8_xy2_mmx(uint8_t *block, const uint8_t *pixels,
                            ptrdiff_t line_size, int h);
void ff_avg_pixels16_xy2_mmx(uint8_t *block, const uint8_t *pixels,
                             ptrdiff_t line_size, int h);

void ff_put_pixels8_xy2_mmx(uint8_t *block, const uint8_t *pixels,
                            ptrdiff_t line_size, int h);
void ff_put_pixels16_xy2_mmx(uint8_t *block, const uint8_t *pixels,
                             ptrdiff_t line_size, int h);


void ff_mmx_idct(int16_t *block);
void ff_mmxext_idct(int16_t *block);

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
