/*
 * VVC CTU(Coding Tree Unit) parser
 *
 * Copyright (C) 2022 Nuo Mi
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

#include "vvc_cabac.h"
#include "vvc_ctu.h"
#include "vvc_mvs.h"

void ff_vvc_set_neighbour_available(VVCLocalContext *lc,
    const int x0, const int y0, const int w, const int h)
{
    const int log2_ctb_size = lc->fc->ps.sps->ctb_log2_size_y;
    const int x0b = av_mod_uintp2(x0, log2_ctb_size);
    const int y0b = av_mod_uintp2(y0, log2_ctb_size);

    lc->na.cand_up       = (lc->ctb_up_flag   || y0b);
    lc->na.cand_left     = (lc->ctb_left_flag || x0b);
    lc->na.cand_up_left  = (x0b || y0b) ? lc->na.cand_left && lc->na.cand_up : lc->ctb_up_left_flag;
    lc->na.cand_up_right_sap =
            (x0b + w == 1 << log2_ctb_size) ? lc->ctb_up_right_flag && !y0b : lc->na.cand_up;
    lc->na.cand_up_right = lc->na.cand_up_right_sap && (x0 + w) < lc->end_of_tiles_x;
}


void ff_vvc_ep_init_stat_coeff(EntryPoint *ep,
    const int bit_depth, const int persistent_rice_adaptation_enabled_flag)
{
    for (size_t i = 0; i < FF_ARRAY_ELEMS(ep->stat_coeff); ++i) {
        ep->stat_coeff[i] =
            persistent_rice_adaptation_enabled_flag ? 2 * (av_log2(bit_depth - 10)) : 0;
    }
}
