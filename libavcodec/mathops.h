/*
 * simple math operations
 * Copyright (c) 2001, 2002 Fabrice Bellard.
 * Copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at> et al
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
#ifndef FFMPEG_MATHOPS_H
#define FFMPEG_MATHOPS_H

#include "common.h"

#ifdef ARCH_X86_32

#include "i386/mathops.h"

#elif defined(ARCH_ARMV4L)

#include "armv4l/mathops.h"

#elif defined(ARCH_POWERPC)

#include "ppc/mathops.h"

#elif defined(ARCH_BFIN)

#include "bfin/mathops.h"

#endif

/* generic implementation */

#ifndef MULL
#   define MULL(a,b) (((int64_t)(a) * (int64_t)(b)) >> FRAC_BITS)
#endif

#ifndef MULH
//gcc 3.4 creates an incredibly bloated mess out of this
//#    define MULH(a,b) (((int64_t)(a) * (int64_t)(b))>>32)

static av_always_inline int MULH(int a, int b){
    return ((int64_t)(a) * (int64_t)(b))>>32;
}
#endif

#ifndef MUL64
#   define MUL64(a,b) ((int64_t)(a) * (int64_t)(b))
#endif

/* signed 16x16 -> 32 multiply add accumulate */
#ifndef MAC16
#   define MAC16(rt, ra, rb) rt += (ra) * (rb)
#endif

/* signed 16x16 -> 32 multiply */
#ifndef MUL16
#   define MUL16(ra, rb) ((ra) * (rb))
#endif

#endif /* FFMPEG_MATHOPS_H */

