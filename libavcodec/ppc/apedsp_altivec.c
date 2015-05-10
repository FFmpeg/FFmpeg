/*
 * Copyright (c) 2007 Luca Barbato <lu_zero@gentoo.org>
 *
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

#include "config.h"
#if HAVE_ALTIVEC_H
#include <altivec.h>
#endif

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/ppc/cpu.h"
#include "libavutil/ppc/types_altivec.h"
#include "libavcodec/apedsp.h"

#if HAVE_ALTIVEC && HAVE_BIGENDIAN
static int32_t scalarproduct_and_madd_int16_altivec(int16_t *v1,
                                                    const int16_t *v2,
                                                    const int16_t *v3,
                                                    int order, int mul)
{
    LOAD_ZERO;
    vec_s16 *pv1 = (vec_s16 *) v1;
    register vec_s16 muls = { mul, mul, mul, mul, mul, mul, mul, mul };
    register vec_s16 t0, t1, i0, i1, i4;
    register vec_s16 i2 = vec_ld(0, v2), i3 = vec_ld(0, v3);
    register vec_s32 res = zero_s32v;
    register vec_u8 align = vec_lvsl(0, v2);
    int32_t ires;

    order >>= 4;
    do {
        i1     = vec_ld(16, v2);
        t0     = vec_perm(i2, i1, align);
        i2     = vec_ld(32, v2);
        t1     = vec_perm(i1, i2, align);
        i0     = pv1[0];
        i1     = pv1[1];
        res    = vec_msum(t0, i0, res);
        res    = vec_msum(t1, i1, res);
        i4     = vec_ld(16, v3);
        t0     = vec_perm(i3, i4, align);
        i3     = vec_ld(32, v3);
        t1     = vec_perm(i4, i3, align);
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

av_cold void ff_apedsp_init_ppc(APEDSPContext *c)
{
#if HAVE_ALTIVEC && HAVE_BIGENDIAN
    if (!PPC_ALTIVEC(av_get_cpu_flags()))
        return;

    c->scalarproduct_and_madd_int16 = scalarproduct_and_madd_int16_altivec;
#endif /* HAVE_ALTIVEC */
}
