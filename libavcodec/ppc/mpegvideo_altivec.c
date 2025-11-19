/*
 * Copyright (c) 2002 Dieter Shirley
 *
 * dct_unquantize_h263_altivec:
 * Copyright (c) 2003 Romain Dolbeau <romain@dolbeau.org>
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

#include <stdlib.h>
#include <stdio.h>

#include "config.h"

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/cpu.h"
#include "libavutil/mem_internal.h"
#include "libavutil/ppc/cpu.h"
#include "libavutil/ppc/util_altivec.h"

#include "libavcodec/mpegvideo.h"
#include "libavcodec/mpegvideo_unquantize.h"

#if HAVE_ALTIVEC

/* AltiVec version of dct_unquantize_h263
   this code assumes `block' is 16 bytes-aligned */
static av_always_inline
void dct_unquantize_h263_altivec(int16_t *block, int nb_coeffs, int qadd, int qmul)
{
    register const vector signed short vczero = (const vector signed short)vec_splat_s16(0);
    DECLARE_ALIGNED(16, short, qmul8) = qmul;
    DECLARE_ALIGNED(16, short, qadd8) = qadd;
    register vector signed short blockv, qmulv, qaddv, nqaddv, temp1;
    register vector bool short blockv_null, blockv_neg;

    qmulv = vec_splat((vec_s16)vec_lde(0, &qmul8), 0);
    qaddv = vec_splat((vec_s16)vec_lde(0, &qadd8), 0);
    nqaddv = vec_sub(vczero, qaddv);

    // vectorize all the 16 bytes-aligned blocks
    // of 8 elements
    for (register int j = 0; j <= nb_coeffs; j += 8) {
        blockv = vec_ld(j << 1, block);
        blockv_neg = vec_cmplt(blockv, vczero);
        blockv_null = vec_cmpeq(blockv, vczero);
        // choose between +qadd or -qadd as the third operand
        temp1 = vec_sel(qaddv, nqaddv, blockv_neg);
        // multiply & add (block{i,i+7} * qmul [+-] qadd)
        temp1 = vec_mladd(blockv, qmulv, temp1);
        // put 0 where block[{i,i+7} used to have 0
        blockv = vec_sel(temp1, blockv, blockv_null);
        vec_st(blockv, j << 1, block);
    }
}

static void dct_unquantize_h263_intra_altivec(const MPVContext *s,
                                              int16_t *block, int n, int qscale)
{
    int qadd = (qscale - 1) | 1;
    int qmul = qscale << 1;
    int block0 = block[0];
    if (!s->h263_aic) {
        block0 *= n < 4 ? s->y_dc_scale : s->c_dc_scale;
    } else
        qadd = 0;
    int nb_coeffs = s->ac_pred ? 63 : s->intra_scantable.raster_end[s->block_last_index[n]];

    dct_unquantize_h263_altivec(block, nb_coeffs, qadd, qmul);

    // cheat. this avoid special-casing the first iteration
    block[0] = block0;
}

static void dct_unquantize_h263_inter_altivec(const MPVContext *s,
                                              int16_t *block, int n, int qscale)
{
    int qadd = (qscale - 1) | 1;
    int qmul = qscale << 1;
    av_assert2(s->block_last_index[n]>=0);
    int nb_coeffs = s->inter_scantable.raster_end[s->block_last_index[n]];

    dct_unquantize_h263_altivec(block, nb_coeffs, qadd, qmul);
}
#endif /* HAVE_ALTIVEC */

av_cold void ff_mpv_unquantize_init_ppc(MPVUnquantDSPContext *s, int bitexact)
{
#if HAVE_ALTIVEC
    if (!PPC_ALTIVEC(av_get_cpu_flags()))
        return;

    s->dct_unquantize_h263_intra = dct_unquantize_h263_intra_altivec;
    s->dct_unquantize_h263_inter = dct_unquantize_h263_inter_altivec;
#endif /* HAVE_ALTIVEC */
}
