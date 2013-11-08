/*
 * Copyright (c) 2006 Luca Barbato <lu_zero@gentoo.org>
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
#if HAVE_ALTIVEC_H
#include <altivec.h>
#endif
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavcodec/vorbisdsp.h"

#if HAVE_ALTIVEC
static void vorbis_inverse_coupling_altivec(float *mag, float *ang,
                                            intptr_t blocksize)
{
    int i;
    vector float m, a;
    vector bool int t0, t1;
    const vector unsigned int v_31 = //XXX
        vec_add(vec_add(vec_splat_u32(15),vec_splat_u32(15)),vec_splat_u32(1));
    for (i = 0; i < blocksize; i += 4) {
        m = vec_ld(0, mag+i);
        a = vec_ld(0, ang+i);
        t0 = vec_cmple(m, (vector float)vec_splat_u32(0));
        t1 = vec_cmple(a, (vector float)vec_splat_u32(0));
        a = vec_xor(a, (vector float) vec_sl((vector unsigned int)t0, v_31));
        t0 = (vector bool int)vec_and(a, t1);
        t1 = (vector bool int)vec_andc(a, t1);
        a = vec_sub(m, (vector float)t1);
        m = vec_add(m, (vector float)t0);
        vec_stl(a, 0, ang+i);
        vec_stl(m, 0, mag+i);
    }
}
#endif /* HAVE_ALTIVEC */

av_cold void ff_vorbisdsp_init_ppc(VorbisDSPContext *c)
{
#if HAVE_ALTIVEC
    if (!(av_get_cpu_flags() & AV_CPU_FLAG_ALTIVEC))
        return;

    c->vorbis_inverse_coupling = vorbis_inverse_coupling_altivec;
#endif /* HAVE_ALTIVEC */
}
