/*
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
#include <stdint.h>
#if HAVE_ALTIVEC_H
#include <altivec.h>
#endif

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/ppc/cpu.h"
#include "libavutil/ppc/types_altivec.h"
#include "libavutil/ppc/util_altivec.h"
#include "libavcodec/mpegvideoencdsp.h"

#if HAVE_ALTIVEC

#if HAVE_VSX
static int pix_norm1_altivec(uint8_t *pix, int line_size)
{
    int i, s = 0;
    const vector unsigned int zero =
        (const vector unsigned int) vec_splat_u32(0);
    vector unsigned int sv = (vector unsigned int) vec_splat_u32(0);
    vector signed int sum;

    for (i = 0; i < 16; i++) {
        /* Read the potentially unaligned pixels. */
        //vector unsigned char pixl = vec_ld(0,  pix);
        //vector unsigned char pixr = vec_ld(15, pix);
        //vector unsigned char pixv = vec_perm(pixl, pixr, perm);
        vector unsigned char pixv = vec_vsx_ld(0,  pix);

        /* Square the values, and add them to our sum. */
        sv = vec_msum(pixv, pixv, sv);

        pix += line_size;
    }
    /* Sum up the four partial sums, and put the result into s. */
    sum = vec_sums((vector signed int) sv, (vector signed int) zero);
    sum = vec_splat(sum, 3);
    vec_vsx_st(sum, 0, &s);
    return s;
}
#else
static int pix_norm1_altivec(uint8_t *pix, int line_size)
{
    int i, s = 0;
    const vector unsigned int zero =
        (const vector unsigned int) vec_splat_u32(0);
    vector unsigned char perm = vec_lvsl(0, pix);
    vector unsigned int sv = (vector unsigned int) vec_splat_u32(0);
    vector signed int sum;

    for (i = 0; i < 16; i++) {
        /* Read the potentially unaligned pixels. */
        vector unsigned char pixl = vec_ld(0,  pix);
        vector unsigned char pixr = vec_ld(15, pix);
        vector unsigned char pixv = vec_perm(pixl, pixr, perm);

        /* Square the values, and add them to our sum. */
        sv = vec_msum(pixv, pixv, sv);

        pix += line_size;
    }
    /* Sum up the four partial sums, and put the result into s. */
    sum = vec_sums((vector signed int) sv, (vector signed int) zero);
    sum = vec_splat(sum, 3);
    vec_ste(sum, 0, &s);

    return s;
}
#endif /* HAVE_VSX */

#if HAVE_VSX
static int pix_sum_altivec(uint8_t *pix, int line_size)
{
    int i, s;
    const vector unsigned int zero =
        (const vector unsigned int) vec_splat_u32(0);
    vector unsigned int sad = (vector unsigned int) vec_splat_u32(0);
    vector signed int sumdiffs;

    for (i = 0; i < 16; i++) {
        /* Read the potentially unaligned 16 pixels into t1. */
        //vector unsigned char pixl = vec_ld(0,  pix);
        //vector unsigned char pixr = vec_ld(15, pix);
        //vector unsigned char t1   = vec_perm(pixl, pixr, perm);
        vector unsigned char t1   = vec_vsx_ld(0,  pix);

        /* Add each 4 pixel group together and put 4 results into sad. */
        sad = vec_sum4s(t1, sad);

        pix += line_size;
    }

    /* Sum up the four partial sums, and put the result into s. */
    sumdiffs = vec_sums((vector signed int) sad, (vector signed int) zero);
    sumdiffs = vec_splat(sumdiffs, 3);
    vec_vsx_st(sumdiffs, 0, &s);
    return s;
}
#else
static int pix_sum_altivec(uint8_t *pix, int line_size)
{
    int i, s;
    const vector unsigned int zero =
        (const vector unsigned int) vec_splat_u32(0);
    vector unsigned char perm = vec_lvsl(0, pix);
    vector unsigned int sad = (vector unsigned int) vec_splat_u32(0);
    vector signed int sumdiffs;

    for (i = 0; i < 16; i++) {
        /* Read the potentially unaligned 16 pixels into t1. */
        vector unsigned char pixl = vec_ld(0,  pix);
        vector unsigned char pixr = vec_ld(15, pix);
        vector unsigned char t1   = vec_perm(pixl, pixr, perm);

        /* Add each 4 pixel group together and put 4 results into sad. */
        sad = vec_sum4s(t1, sad);

        pix += line_size;
    }

    /* Sum up the four partial sums, and put the result into s. */
    sumdiffs = vec_sums((vector signed int) sad, (vector signed int) zero);
    sumdiffs = vec_splat(sumdiffs, 3);
    vec_ste(sumdiffs, 0, &s);

    return s;
}

#endif /* HAVE_VSX */

#endif /* HAVE_ALTIVEC */

av_cold void ff_mpegvideoencdsp_init_ppc(MpegvideoEncDSPContext *c,
                                         AVCodecContext *avctx)
{
#if HAVE_ALTIVEC
    if (!PPC_ALTIVEC(av_get_cpu_flags()))
        return;

    c->pix_norm1 = pix_norm1_altivec;
    c->pix_sum   = pix_sum_altivec;
#endif /* HAVE_ALTIVEC */
}
