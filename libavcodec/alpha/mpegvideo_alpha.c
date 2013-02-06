/*
 * Alpha optimized DSP utils
 * Copyright (c) 2002 Falk Hueffner <falk@debian.org>
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

#include "libavutil/attributes.h"
#include "libavcodec/dsputil.h"
#include "libavcodec/mpegvideo.h"
#include "asm.h"

static void dct_unquantize_h263_axp(int16_t *block, int n_coeffs,
                                    uint64_t qscale, uint64_t qadd)
{
    uint64_t qmul = qscale << 1;
    uint64_t correction = WORD_VEC(qmul * 255 >> 8);
    int i;

    qadd = WORD_VEC(qadd);

    for(i = 0; i <= n_coeffs; block += 4, i += 4) {
        uint64_t levels, negmask, zeros, add, sub;

        levels = ldq(block);
        if (levels == 0)
            continue;

#ifdef __alpha_max__
        /* I don't think the speed difference justifies runtime
           detection.  */
        negmask = maxsw4(levels, -1); /* negative -> ffff (-1) */
        negmask = minsw4(negmask, 0); /* positive -> 0000 (0) */
#else
        negmask = cmpbge(WORD_VEC(0x7fff), levels);
        negmask &= (negmask >> 1) | (1 << 7);
        negmask = zap(-1, negmask);
#endif

        zeros = cmpbge(0, levels);
        zeros &= zeros >> 1;
        /* zeros |= zeros << 1 is not needed since qadd <= 255, so
           zapping the lower byte suffices.  */

        levels *= qmul;
        levels -= correction & (negmask << 16);

        add = qadd & ~negmask;
        sub = qadd &  negmask;
        /* Set qadd to 0 for levels == 0.  */
        add = zap(add, zeros);
        levels += add;
        levels -= sub;

        stq(levels, block);
    }
}

static void dct_unquantize_h263_intra_axp(MpegEncContext *s, int16_t *block,
                                    int n, int qscale)
{
    int n_coeffs;
    uint64_t qadd;
    int16_t block0 = block[0];

    if (!s->h263_aic) {
        if (n < 4)
            block0 *= s->y_dc_scale;
        else
            block0 *= s->c_dc_scale;
        qadd = (qscale - 1) | 1;
    } else {
        qadd = 0;
    }

    if(s->ac_pred)
        n_coeffs = 63;
    else
        n_coeffs = s->inter_scantable.raster_end[s->block_last_index[n]];

    dct_unquantize_h263_axp(block, n_coeffs, qscale, qadd);

    block[0] = block0;
}

static void dct_unquantize_h263_inter_axp(MpegEncContext *s, int16_t *block,
                                    int n, int qscale)
{
    int n_coeffs = s->inter_scantable.raster_end[s->block_last_index[n]];
    dct_unquantize_h263_axp(block, n_coeffs, qscale, (qscale - 1) | 1);
}

av_cold void ff_MPV_common_init_axp(MpegEncContext *s)
{
    s->dct_unquantize_h263_intra = dct_unquantize_h263_intra_axp;
    s->dct_unquantize_h263_inter = dct_unquantize_h263_inter_axp;
}
