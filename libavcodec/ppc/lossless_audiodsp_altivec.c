/*
 * Copyright (c) 2007 Luca Barbato <lu_zero@gentoo.org>
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
#include "libavutil/ppc/cpu.h"
#include "libavutil/ppc/types_altivec.h"
#include "libavcodec/lossless_audiodsp.h"

#if HAVE_BIGENDIAN
#define GET_T(tt0,tt1,src,a,b){       \
        a = vec_ld(16, src);          \
        tt0 = vec_perm(b, a, align);  \
        b = vec_ld(32, src);          \
        tt1 = vec_perm(a, b, align);  \
 }
#else
#define GET_T(tt0,tt1,src,a,b){       \
        tt0 = vec_vsx_ld(0, src);     \
        tt1 = vec_vsx_ld(16, src);    \
 }
#endif

#if HAVE_ALTIVEC
static int32_t scalarproduct_and_madd_int16_altivec(int16_t *v1,
                                                    const int16_t *v2,
                                                    const int16_t *v3,
                                                    int order, int mul)
{
    LOAD_ZERO;
    vec_s16 *pv1 = (vec_s16 *) v1;
    register vec_s16 muls = { mul, mul, mul, mul, mul, mul, mul, mul };
    register vec_s16 t0, t1, i0, i1, i4, i2, i3;
    register vec_s32 res = zero_s32v;
#if HAVE_BIGENDIAN
    register vec_u8 align = vec_lvsl(0, v2);
    i2 = vec_ld(0, v2);
    i3 = vec_ld(0, v3);
#endif
    int32_t ires;

    order >>= 4;
    do {
        GET_T(t0,t1,v2,i1,i2);
        i0     = pv1[0];
        i1     = pv1[1];
        res    = vec_msum(t0, i0, res);
        res    = vec_msum(t1, i1, res);
        GET_T(t0,t1,v3,i4,i3);
        pv1[0] = vec_mladd(t0, muls, i0);
        pv1[1] = vec_mladd(t1, muls, i1);
        pv1   += 2;
        v2    += 16;
        v3    += 16;
    } while (--order);
    res = vec_splat(vec_sums(res, zero_s32v), 3);
    vec_ste(res, 0, &ires);

    return ires;
}
#endif /* HAVE_ALTIVEC */

av_cold void ff_llauddsp_init_ppc(LLAudDSPContext *c)
{
#if HAVE_ALTIVEC
    if (!PPC_ALTIVEC(av_get_cpu_flags()))
        return;

    c->scalarproduct_and_madd_int16 = scalarproduct_and_madd_int16_altivec;
#endif /* HAVE_ALTIVEC */
}
