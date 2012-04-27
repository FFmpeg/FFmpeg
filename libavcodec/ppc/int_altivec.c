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

/**
 ** @file
 ** integer misc ops.
 **/

#include "config.h"
#if HAVE_ALTIVEC_H
#include <altivec.h>
#endif

#include "libavcodec/dsputil.h"

#include "dsputil_altivec.h"

#include "types_altivec.h"

static int ssd_int8_vs_int16_altivec(const int8_t *pix1, const int16_t *pix2,
                                     int size) {
    int i, size16;
    vector signed char vpix1;
    vector signed short vpix2, vdiff, vpix1l,vpix1h;
    union { vector signed int vscore;
            int32_t score[4];
          } u;
    u.vscore = vec_splat_s32(0);
//
//XXX lazy way, fix it later

#define vec_unaligned_load(b) \
    vec_perm(vec_ld(0,b),vec_ld(15,b),vec_lvsl(0, b));

    size16 = size >> 4;
    while(size16) {
//        score += (pix1[i]-pix2[i])*(pix1[i]-pix2[i]);
        //load pix1 and the first batch of pix2

        vpix1 = vec_unaligned_load(pix1);
        vpix2 = vec_unaligned_load(pix2);
        pix2 += 8;
        //unpack
        vpix1h = vec_unpackh(vpix1);
        vdiff  = vec_sub(vpix1h, vpix2);
        vpix1l = vec_unpackl(vpix1);
        // load another batch from pix2
        vpix2 = vec_unaligned_load(pix2);
        u.vscore = vec_msum(vdiff, vdiff, u.vscore);
        vdiff  = vec_sub(vpix1l, vpix2);
        u.vscore = vec_msum(vdiff, vdiff, u.vscore);
        pix1 += 16;
        pix2 += 8;
        size16--;
    }
    u.vscore = vec_sums(u.vscore, vec_splat_s32(0));

    size %= 16;
    for (i = 0; i < size; i++) {
        u.score[3] += (pix1[i]-pix2[i])*(pix1[i]-pix2[i]);
    }
    return u.score[3];
}

static int32_t scalarproduct_int16_altivec(const int16_t *v1, const int16_t *v2,
                                           int order)
{
    int i;
    LOAD_ZERO;
    const vec_s16 *pv;
    register vec_s16 vec1;
    register vec_s32 res = vec_splat_s32(0), t;
    int32_t ires;

    for(i = 0; i < order; i += 8){
        pv = (const vec_s16*)v1;
        vec1 = vec_perm(pv[0], pv[1], vec_lvsl(0, v1));
        t = vec_msum(vec1, vec_ld(0, v2), zero_s32v);
        res = vec_sums(t, res);
        v1 += 8;
        v2 += 8;
    }
    res = vec_splat(res, 3);
    vec_ste(res, 0, &ires);
    return ires;
}

static int32_t scalarproduct_and_madd_int16_altivec(int16_t *v1, const int16_t *v2, const int16_t *v3, int order, int mul)
{
    LOAD_ZERO;
    vec_s16 *pv1 = (vec_s16*)v1;
    register vec_s16 muls = {mul,mul,mul,mul,mul,mul,mul,mul};
    register vec_s16 t0, t1, i0, i1, i4;
    register vec_s16 i2 = vec_ld(0, v2), i3 = vec_ld(0, v3);
    register vec_s32 res = zero_s32v;
    register vec_u8 align = vec_lvsl(0, v2);
    int32_t ires;
    order >>= 4;
    do {
        i1 = vec_ld(16, v2);
        t0 = vec_perm(i2, i1, align);
        i2 = vec_ld(32, v2);
        t1 = vec_perm(i1, i2, align);
        i0 = pv1[0];
        i1 = pv1[1];
        res = vec_msum(t0, i0, res);
        res = vec_msum(t1, i1, res);
        i4 = vec_ld(16, v3);
        t0 = vec_perm(i3, i4, align);
        i3 = vec_ld(32, v3);
        t1 = vec_perm(i4, i3, align);
        pv1[0] = vec_mladd(t0, muls, i0);
        pv1[1] = vec_mladd(t1, muls, i1);
        pv1 += 2;
        v2  += 8;
        v3  += 8;
    } while(--order);
    res = vec_splat(vec_sums(res, zero_s32v), 3);
    vec_ste(res, 0, &ires);
    return ires;
}

void ff_int_init_altivec(DSPContext* c, AVCodecContext *avctx)
{
    c->ssd_int8_vs_int16 = ssd_int8_vs_int16_altivec;
    c->scalarproduct_int16 = scalarproduct_int16_altivec;
    c->scalarproduct_and_madd_int16 = scalarproduct_and_madd_int16_altivec;
}
