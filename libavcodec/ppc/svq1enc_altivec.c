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

#include <stdint.h>

#include "config.h"
#if HAVE_ALTIVEC_H
#include <altivec.h>
#endif

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/ppc/cpu.h"
#include "libavutil/ppc/types_altivec.h"
#include "libavutil/ppc/util_altivec.h"
#include "libavcodec/svq1enc.h"

#if HAVE_ALTIVEC && HAVE_BIGENDIAN
static int ssd_int8_vs_int16_altivec(const int8_t *pix1, const int16_t *pix2,
                                     int size)
{
    int i, size16 = size >> 4;
    vector signed char vpix1;
    vector signed short vpix2, vdiff, vpix1l, vpix1h;
    union {
        vector signed int vscore;
        int32_t score[4];
    } u = { .vscore = vec_splat_s32(0) };

    while (size16) {
        // score += (pix1[i] - pix2[i]) * (pix1[i] - pix2[i]);
        // load pix1 and the first batch of pix2

        vpix1 = vec_unaligned_load(pix1);
        vpix2 = vec_unaligned_load(pix2);
        pix2 += 8;
        // unpack
        vpix1h = vec_unpackh(vpix1);
        vdiff  = vec_sub(vpix1h, vpix2);
        vpix1l = vec_unpackl(vpix1);
        // load another batch from pix2
        vpix2    = vec_unaligned_load(pix2);
        u.vscore = vec_msum(vdiff, vdiff, u.vscore);
        vdiff    = vec_sub(vpix1l, vpix2);
        u.vscore = vec_msum(vdiff, vdiff, u.vscore);
        pix1    += 16;
        pix2    += 8;
        size16--;
    }
    u.vscore = vec_sums(u.vscore, vec_splat_s32(0));

    size %= 16;
    for (i = 0; i < size; i++)
        u.score[3] += (pix1[i] - pix2[i]) * (pix1[i] - pix2[i]);

    return u.score[3];
}
#endif /* HAVE_ALTIVEC */

av_cold void ff_svq1enc_init_ppc(SVQ1EncContext *c)
{
#if HAVE_ALTIVEC && HAVE_BIGENDIAN
    if (!PPC_ALTIVEC(av_get_cpu_flags()))
        return;

    c->ssd_int8_vs_int16 = ssd_int8_vs_int16_altivec;
#endif /* HAVE_ALTIVEC */
}
