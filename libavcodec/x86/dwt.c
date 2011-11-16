/*
 * MMX optimized discrete wavelet transform
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2010 David Conrad
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

#include "libavutil/x86_cpu.h"
#include "dsputil_mmx.h"
#include "dwt.h"

#define COMPOSE_VERTICAL(ext, align) \
void ff_vertical_compose53iL0##ext(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, int width); \
void ff_vertical_compose_dirac53iH0##ext(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, int width); \
void ff_vertical_compose_dd137iL0##ext(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, IDWTELEM *b3, IDWTELEM *b4, int width); \
void ff_vertical_compose_dd97iH0##ext(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, IDWTELEM *b3, IDWTELEM *b4, int width); \
void ff_vertical_compose_haar##ext(IDWTELEM *b0, IDWTELEM *b1, int width); \
void ff_horizontal_compose_haar0i##ext(IDWTELEM *b, IDWTELEM *tmp, int w);\
void ff_horizontal_compose_haar1i##ext(IDWTELEM *b, IDWTELEM *tmp, int w);\
\
static void vertical_compose53iL0##ext(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, int width) \
{ \
    int i, width_align = width&~(align-1); \
\
    for(i=width_align; i<width; i++) \
        b1[i] = COMPOSE_53iL0(b0[i], b1[i], b2[i]); \
\
    ff_vertical_compose53iL0##ext(b0, b1, b2, width_align); \
} \
\
static void vertical_compose_dirac53iH0##ext(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, int width) \
{ \
    int i, width_align = width&~(align-1); \
\
    for(i=width_align; i<width; i++) \
        b1[i] = COMPOSE_DIRAC53iH0(b0[i], b1[i], b2[i]); \
\
    ff_vertical_compose_dirac53iH0##ext(b0, b1, b2, width_align); \
} \
\
static void vertical_compose_dd137iL0##ext(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, \
                                           IDWTELEM *b3, IDWTELEM *b4, int width) \
{ \
    int i, width_align = width&~(align-1); \
\
    for(i=width_align; i<width; i++) \
        b2[i] = COMPOSE_DD137iL0(b0[i], b1[i], b2[i], b3[i], b4[i]); \
\
    ff_vertical_compose_dd137iL0##ext(b0, b1, b2, b3, b4, width_align); \
} \
\
static void vertical_compose_dd97iH0##ext(IDWTELEM *b0, IDWTELEM *b1, IDWTELEM *b2, \
                                          IDWTELEM *b3, IDWTELEM *b4, int width) \
{ \
    int i, width_align = width&~(align-1); \
\
    for(i=width_align; i<width; i++) \
        b2[i] = COMPOSE_DD97iH0(b0[i], b1[i], b2[i], b3[i], b4[i]); \
\
    ff_vertical_compose_dd97iH0##ext(b0, b1, b2, b3, b4, width_align); \
} \
static void vertical_compose_haar##ext(IDWTELEM *b0, IDWTELEM *b1, int width) \
{ \
    int i, width_align = width&~(align-1); \
\
    for(i=width_align; i<width; i++) { \
        b0[i] = COMPOSE_HAARiL0(b0[i], b1[i]); \
        b1[i] = COMPOSE_HAARiH0(b1[i], b0[i]); \
    } \
\
    ff_vertical_compose_haar##ext(b0, b1, width_align); \
} \
static void horizontal_compose_haar0i##ext(IDWTELEM *b, IDWTELEM *tmp, int w)\
{\
    int w2= w>>1;\
    int x= w2 - (w2&(align-1));\
    ff_horizontal_compose_haar0i##ext(b, tmp, w);\
\
    for (; x < w2; x++) {\
        b[2*x  ] = tmp[x];\
        b[2*x+1] = COMPOSE_HAARiH0(b[x+w2], tmp[x]);\
    }\
}\
static void horizontal_compose_haar1i##ext(IDWTELEM *b, IDWTELEM *tmp, int w)\
{\
    int w2= w>>1;\
    int x= w2 - (w2&(align-1));\
    ff_horizontal_compose_haar1i##ext(b, tmp, w);\
\
    for (; x < w2; x++) {\
        b[2*x  ] = (tmp[x] + 1)>>1;\
        b[2*x+1] = (COMPOSE_HAARiH0(b[x+w2], tmp[x]) + 1)>>1;\
    }\
}\
\

#if HAVE_YASM
#if !ARCH_X86_64
COMPOSE_VERTICAL(_mmx, 4)
#endif
COMPOSE_VERTICAL(_sse2, 8)
#endif


void ff_horizontal_compose_dd97i_ssse3(IDWTELEM *b, IDWTELEM *tmp, int w);

static void horizontal_compose_dd97i_ssse3(IDWTELEM *b, IDWTELEM *tmp, int w)
{
    int w2= w>>1;
    int x= w2 - (w2&7);
    ff_horizontal_compose_dd97i_ssse3(b, tmp, w);

    for (; x < w2; x++) {
        b[2*x  ] = (tmp[x] + 1)>>1;
        b[2*x+1] = (COMPOSE_DD97iH0(tmp[x-1], tmp[x], b[x+w2], tmp[x+1], tmp[x+2]) + 1)>>1;
    }
}

void ff_spatial_idwt_init_mmx(DWTContext *d, enum dwt_type type)
{
#if HAVE_YASM
  int mm_flags = av_get_cpu_flags();

#if !ARCH_X86_64
    if (!(mm_flags & AV_CPU_FLAG_MMX))
        return;

    switch (type) {
    case DWT_DIRAC_DD9_7:
        d->vertical_compose_l0 = vertical_compose53iL0_mmx;
        d->vertical_compose_h0 = vertical_compose_dd97iH0_mmx;
        break;
    case DWT_DIRAC_LEGALL5_3:
        d->vertical_compose_l0 = vertical_compose53iL0_mmx;
        d->vertical_compose_h0 = vertical_compose_dirac53iH0_mmx;
        break;
    case DWT_DIRAC_DD13_7:
        d->vertical_compose_l0 = vertical_compose_dd137iL0_mmx;
        d->vertical_compose_h0 = vertical_compose_dd97iH0_mmx;
        break;
    case DWT_DIRAC_HAAR0:
        d->vertical_compose   = vertical_compose_haar_mmx;
        d->horizontal_compose = horizontal_compose_haar0i_mmx;
        break;
    case DWT_DIRAC_HAAR1:
        d->vertical_compose   = vertical_compose_haar_mmx;
        d->horizontal_compose = horizontal_compose_haar1i_mmx;
        break;
    }
#endif

    if (!(mm_flags & AV_CPU_FLAG_SSE2))
        return;

    switch (type) {
    case DWT_DIRAC_DD9_7:
        d->vertical_compose_l0 = vertical_compose53iL0_sse2;
        d->vertical_compose_h0 = vertical_compose_dd97iH0_sse2;
        break;
    case DWT_DIRAC_LEGALL5_3:
        d->vertical_compose_l0 = vertical_compose53iL0_sse2;
        d->vertical_compose_h0 = vertical_compose_dirac53iH0_sse2;
        break;
    case DWT_DIRAC_DD13_7:
        d->vertical_compose_l0 = vertical_compose_dd137iL0_sse2;
        d->vertical_compose_h0 = vertical_compose_dd97iH0_sse2;
        break;
    case DWT_DIRAC_HAAR0:
        d->vertical_compose   = vertical_compose_haar_sse2;
        d->horizontal_compose = horizontal_compose_haar0i_sse2;
        break;
    case DWT_DIRAC_HAAR1:
        d->vertical_compose   = vertical_compose_haar_sse2;
        d->horizontal_compose = horizontal_compose_haar1i_sse2;
        break;
    }

    if (!(mm_flags & AV_CPU_FLAG_SSSE3))
        return;

    switch (type) {
    case DWT_DIRAC_DD9_7:
        d->horizontal_compose = horizontal_compose_dd97i_ssse3;
        break;
    }
#endif // HAVE_YASM
}
