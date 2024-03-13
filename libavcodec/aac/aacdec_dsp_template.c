/*
 * AAC decoder
 * Copyright (c) 2005-2006 Oded Shimon ( ods15 ods15 dyndns org )
 * Copyright (c) 2006-2007 Maxim Gavrilov ( maxim.gavrilov gmail com )
 * Copyright (c) 2008-2013 Alex Converse <alex.converse@gmail.com>
 *
 * AAC LATM decoder
 * Copyright (c) 2008-2010 Paul Kendall <paul@kcbbs.gen.nz>
 * Copyright (c) 2010      Janne Grunau <janne-libav@jannau.net>
 *
 * AAC decoder fixed-point implementation
 * Copyright (c) 2013
 *      MIPS Technologies, Inc., California.
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

#include "libavcodec/aacdec.h"
#include "libavcodec/aac_defines.h"

#include "libavcodec/aactab.h"

/**
 * Convert integer scalefactors to the decoder's native expected
 * scalefactor values.
 */
static void AAC_RENAME(dequant_scalefactors)(SingleChannelElement *sce)
{
    IndividualChannelStream *ics = &sce->ics;
    const enum BandType *band_type = sce->band_type;
    const int *band_type_run_end = sce->band_type_run_end;
    const int *sfo = sce->sfo;
    INTFLOAT *sf = sce->AAC_RENAME(sf);

    int g, i, idx = 0;
    for (g = 0; g < ics->num_window_groups; g++) {
        for (i = 0; i < ics->max_sfb;) {
            int run_end = band_type_run_end[idx];
            switch (band_type[idx]) {
            case ZERO_BT:
                for (; i < run_end; i++, idx++)
                    sf[idx] = FIXR(0.);
                break;
            case INTENSITY_BT: /* fallthrough */
            case INTENSITY_BT2:
                for (; i < run_end; i++, idx++) {
#if USE_FIXED
                    sf[idx] = 100 - sfo[idx];
#else
                    sf[idx] = ff_aac_pow2sf_tab[-sfo[idx] + POW_SF2_ZERO];
#endif /* USE_FIXED */
                }
                break;
            case NOISE_BT:
                for (; i < run_end; i++, idx++) {
#if USE_FIXED
                    sf[idx] = -(100 + sfo[idx]);
#else
                    sf[idx] = -ff_aac_pow2sf_tab[sfo[idx] + POW_SF2_ZERO];
#endif /* USE_FIXED */
                }
                break;
            default:
                for (; i < run_end; i++, idx++) {
#if USE_FIXED
                    sf[idx] = -sfo[idx];
#else
                    sf[idx] = -ff_aac_pow2sf_tab[sfo[idx] - 100 + POW_SF2_ZERO];
#endif /* USE_FIXED */
                }
                break;
            }
        }
    }
}

const AACDecDSP AAC_RENAME(aac_dsp) = {
    .dequant_scalefactors = &AAC_RENAME(dequant_scalefactors),
};
