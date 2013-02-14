/*
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
 */

#ifndef AVUTIL_X86_EMMS_H
#define AVUTIL_X86_EMMS_H

#include "config.h"
#include "libavutil/attributes.h"

void avpriv_emms_yasm(void);

#if HAVE_MMX_INLINE
#   define emms_c emms_c
/**
 * Empty mmx state.
 * this must be called between any dsp function and float/double code.
 * for example sin(); dsp->idct_put(); emms_c(); cos()
 */
static av_always_inline void emms_c(void)
{
    __asm__ volatile ("emms" ::: "memory");
}
#elif HAVE_MMX && HAVE_MM_EMPTY
#   include <mmintrin.h>
#   define emms_c _mm_empty
#elif HAVE_MMX_EXTERNAL
#   define emms_c avpriv_emms_yasm
#endif /* HAVE_MMX_INLINE */

#endif /* AVUTIL_X86_EMMS_H */
