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

extern UINT8 zigzag_end[64];

static void dct_unquantize_h263_axp(MpegEncContext *s, 
				    DCTELEM *block, int n, int qscale)
{
    int i, level;
    UINT64 qmul, qadd;
    if (s->mb_intra) {
        if (n < 4) 
            block[0] = block[0] * s->y_dc_scale;
        else
            block[0] = block[0] * s->c_dc_scale;
	/* Catch up to aligned point.  */
	qmul = s->qscale << 1;
	qadd = (s->qscale - 1) | 1;
	for (i = 1; i < 4; ++i) {
	    level = block[i];
	    if (level) {
		if (level < 0) {
		    level = level * qmul - qadd;
		} else {
		    level = level * qmul + qadd;
		}
		block[i] = level;
	    }
	}
	block += 4;
	i = 60 / 4;
    } else {
        i = zigzag_end[s->block_last_index[n]] / 4;
    }
    qmul = s->qscale << 1;
    qadd = WORD_VEC((qscale - 1) | 1);
    do {
	UINT64 levels, negmask, zeromask, corr;
	levels = ldq(block);
	if (levels == 0)
	    continue;
	zeromask = cmpbge(0, levels);
	zeromask &= zeromask >> 1;
	/* Negate all negative words.  */
	negmask = maxsw4(levels, WORD_VEC(0xffff)); /* negative -> ffff (-1) */
	negmask = minsw4(negmask, 0);		    /* positive -> 0000 (0) */
	corr    = negmask & WORD_VEC(0x0001); /* twos-complement correction */
	levels ^= negmask;
	levels += corr;

	levels = levels * qmul;
	levels += zap(qadd, zeromask);

	/* Re-negate negative words.  */
	levels -= corr;
	levels ^= negmask;

	stq(levels, block);
    } while (block += 4, --i);
}

void MPV_common_init_axp(MpegEncContext *s)
{
    if (amask(AMASK_MVI) == 0) {
        if (s->out_format == FMT_H263)
	    s->dct_unquantize = dct_unquantize_h263_axp;
    }
}
