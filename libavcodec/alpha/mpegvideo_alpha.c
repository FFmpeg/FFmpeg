/*
 * Alpha optimized DSP utils
 * Copyright (c) 2002 Falk Hueffner <falk@debian.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "asm.h"
#include "../dsputil.h"
#include "../mpegvideo.h"

static void dct_unquantize_h263_intra_axp(MpegEncContext *s, DCTELEM *block,
                                    int n, int qscale)
{
    int i, n_coeffs;
    uint64_t qmul, qadd;
    uint64_t correction;
    DCTELEM *orig_block = block;
    DCTELEM block0;

    qadd = WORD_VEC((qscale - 1) | 1);
    qmul = qscale << 1;
    /* This mask kills spill from negative subwords to the next subword.  */ 
    correction = WORD_VEC((qmul - 1) + 1); /* multiplication / addition */

    if (!s->h263_aic) {
        if (n < 4) 
            block0 = block[0] * s->y_dc_scale;
        else
            block0 = block[0] * s->c_dc_scale;
    } else {
        qadd = 0;
    }
    n_coeffs = 63; // does not always use zigzag table 

    for(i = 0; i <= n_coeffs; block += 4, i += 4) {
        uint64_t levels, negmask, zeros, add;

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

        /* Negate qadd for negative levels.  */
        add = qadd ^ negmask;
        add += WORD_VEC(0x0001) & negmask;
        /* Set qadd to 0 for levels == 0.  */
        add = zap(add, zeros);

        levels += add;

        stq(levels, block);
    }

    if (s->mb_intra && !s->h263_aic)
        orig_block[0] = block0;
}

static void dct_unquantize_h263_inter_axp(MpegEncContext *s, DCTELEM *block,
                                    int n, int qscale)
{
    int i, n_coeffs;
    uint64_t qmul, qadd;
    uint64_t correction;
    DCTELEM *orig_block = block;
    DCTELEM block0;

    qadd = WORD_VEC((qscale - 1) | 1);
    qmul = qscale << 1;
    /* This mask kills spill from negative subwords to the next subword.  */ 
    correction = WORD_VEC((qmul - 1) + 1); /* multiplication / addition */

    n_coeffs = s->intra_scantable.raster_end[s->block_last_index[n]];

    for(i = 0; i <= n_coeffs; block += 4, i += 4) {
        uint64_t levels, negmask, zeros, add;

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

        /* Negate qadd for negative levels.  */
        add = qadd ^ negmask;
        add += WORD_VEC(0x0001) & negmask;
        /* Set qadd to 0 for levels == 0.  */
        add = zap(add, zeros);

        levels += add;

        stq(levels, block);
    }
}

void MPV_common_init_axp(MpegEncContext *s)
{
    s->dct_unquantize_h263_intra = dct_unquantize_h263_intra_axp;
    s->dct_unquantize_h263_inter = dct_unquantize_h263_inter_axp;
}
