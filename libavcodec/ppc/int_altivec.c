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
 ** @file libavcodec/ppc/int_altivec.c
 ** integer misc ops.
 **/

#include "libavcodec/dsputil.h"

#include "gcc_fixes.h"

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

static void add_int16_altivec(int16_t * v1, int16_t * v2, int order)
{
    int i;
    register vec_s16 vec, *pv;

    for(i = 0; i < order; i += 8){
        pv = (vec_s16*)v2;
        vec = vec_perm(pv[0], pv[1], vec_lvsl(0, v2));
        vec_st(vec_add(vec_ld(0, v1), vec), 0, v1);
        v1 += 8;
        v2 += 8;
    }
}

static void sub_int16_altivec(int16_t * v1, int16_t * v2, int order)
{
    int i;
    register vec_s16 vec, *pv;

    for(i = 0; i < order; i += 8){
        pv = (vec_s16*)v2;
        vec = vec_perm(pv[0], pv[1], vec_lvsl(0, v2));
        vec_st(vec_sub(vec_ld(0, v1), vec), 0, v1);
        v1 += 8;
        v2 += 8;
    }
}

static int32_t scalarproduct_int16_altivec(int16_t * v1, int16_t * v2, int order, const int shift)
{
    int i;
    LOAD_ZERO;
    register vec_s16 vec1, *pv;
    register vec_s32 res = vec_splat_s32(0), t;
    register vec_u32 shifts;
    DECLARE_ALIGNED_16(int32_t, ires);

    shifts = zero_u32v;
    if(shift & 0x10) shifts = vec_add(shifts, vec_sl(vec_splat_u32(0x08), vec_splat_u32(0x1)));
    if(shift & 0x08) shifts = vec_add(shifts, vec_splat_u32(0x08));
    if(shift & 0x04) shifts = vec_add(shifts, vec_splat_u32(0x04));
    if(shift & 0x02) shifts = vec_add(shifts, vec_splat_u32(0x02));
    if(shift & 0x01) shifts = vec_add(shifts, vec_splat_u32(0x01));

    for(i = 0; i < order; i += 8){
        pv = (vec_s16*)v1;
        vec1 = vec_perm(pv[0], pv[1], vec_lvsl(0, v1));
        t = vec_msum(vec1, vec_ld(0, v2), zero_s32v);
        t = vec_sr(t, shifts);
        res = vec_sums(t, res);
        v1 += 8;
        v2 += 8;
    }
    res = vec_splat(res, 3);
    vec_ste(res, 0, &ires);
    return ires;
}

void int_init_altivec(DSPContext* c, AVCodecContext *avctx)
{
    c->ssd_int8_vs_int16 = ssd_int8_vs_int16_altivec;
    c->add_int16 = add_int16_altivec;
    c->sub_int16 = sub_int16_altivec;
    c->scalarproduct_int16 = scalarproduct_int16_altivec;
}
