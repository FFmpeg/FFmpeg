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

/**
 * @file
 * miscellaneous audio operations
 */

#include "config.h"
#if HAVE_ALTIVEC_H
#include <altivec.h>
#endif

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/ppc/cpu.h"
#include "libavutil/ppc/types_altivec.h"
#include "libavutil/ppc/util_altivec.h"
#include "libavcodec/audiodsp.h"

#if HAVE_ALTIVEC

static int32_t scalarproduct_int16_altivec(const int16_t *v1, const int16_t *v2,
                                           int order)
{
    int i;
    LOAD_ZERO;
    register vec_s16 vec1;
    register vec_s32 res = vec_splat_s32(0), t;
    int32_t ires;

    for (i = 0; i < order; i += 8) {
        vec1 = vec_unaligned_load(v1);
        t    = vec_msum(vec1, vec_ld(0, v2), zero_s32v);
        res  = vec_sums(t, res);
        v1  += 8;
        v2  += 8;
    }
    res = vec_splat(res, 3);
    vec_ste(res, 0, &ires);

    return ires;
}

#endif /* HAVE_ALTIVEC */

av_cold void ff_audiodsp_init_ppc(AudioDSPContext *c)
{
#if HAVE_ALTIVEC
    if (!PPC_ALTIVEC(av_get_cpu_flags()))
        return;

    c->scalarproduct_int16 = scalarproduct_int16_altivec;
#endif /* HAVE_ALTIVEC */
}
