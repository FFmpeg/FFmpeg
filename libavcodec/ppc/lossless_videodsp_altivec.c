/*
 * Copyright (c) 2002 Brian Foley
 * Copyright (c) 2002 Dieter Shirley
 * Copyright (c) 2003-2004 Romain Dolbeau <romain@dolbeau.org>
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

#include "config.h"

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/ppc/cpu.h"
#include "libavutil/ppc/util_altivec.h"

#include "libavcodec/lossless_videodsp.h"

#if HAVE_ALTIVEC
static void add_bytes_altivec(uint8_t *dst, uint8_t *src, ptrdiff_t w)
{
    register int i;
    register vector unsigned char vdst, vsrc;

    /* dst and src are 16 bytes-aligned (guaranteed). */
    for (i = 0; i + 15 < w; i += 16) {
        vdst = vec_ld(i, (unsigned char *) dst);
        vsrc = vec_ld(i, (unsigned char *) src);
        vdst = vec_add(vsrc, vdst);
        vec_st(vdst, i, (unsigned char *) dst);
    }
    /* If w is not a multiple of 16. */
    for (; i < w; i++)
        dst[i] = src[i];
}
#endif /* HAVE_ALTIVEC */

av_cold void ff_llviddsp_init_ppc(LLVidDSPContext *c)
{
#if HAVE_ALTIVEC
    if (!PPC_ALTIVEC(av_get_cpu_flags()))
        return;

    c->add_bytes = add_bytes_altivec;
#endif /* HAVE_ALTIVEC */
}
