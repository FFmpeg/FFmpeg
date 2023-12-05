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

#include "libavcodec/refstruct.h"

#include "vvc_cabac.h"
#include "vvc_ctu.h"
#include "vvc_mvs.h"

void ff_vvc_decode_neighbour(VVCLocalContext *lc, const int x_ctb, const int y_ctb,
    const int rx, const int ry, const int rs)
{
    VVCFrameContext *fc = lc->fc;
    const int ctb_size         = fc->ps.sps->ctb_size_y;

    lc->end_of_tiles_x = fc->ps.sps->width;
    lc->end_of_tiles_y = fc->ps.sps->height;
    if (fc->ps.pps->ctb_to_col_bd[rx] != fc->ps.pps->ctb_to_col_bd[rx + 1])
        lc->end_of_tiles_x = FFMIN(x_ctb + ctb_size, lc->end_of_tiles_x);
    if (fc->ps.pps->ctb_to_row_bd[ry] != fc->ps.pps->ctb_to_row_bd[ry + 1])
        lc->end_of_tiles_y = FFMIN(y_ctb + ctb_size, lc->end_of_tiles_y);

    lc->boundary_flags = 0;
    if (rx > 0 && fc->ps.pps->ctb_to_col_bd[rx] != fc->ps.pps->ctb_to_col_bd[rx - 1])
        lc->boundary_flags |= BOUNDARY_LEFT_TILE;
    if (rx > 0 && fc->tab.slice_idx[rs] != fc->tab.slice_idx[rs - 1])
        lc->boundary_flags |= BOUNDARY_LEFT_SLICE;
    if (ry > 0 && fc->ps.pps->ctb_to_row_bd[ry] != fc->ps.pps->ctb_to_row_bd[ry - 1])
        lc->boundary_flags |= BOUNDARY_UPPER_TILE;
    if (ry > 0 && fc->tab.slice_idx[rs] != fc->tab.slice_idx[rs - fc->ps.pps->ctb_width])
        lc->boundary_flags |= BOUNDARY_UPPER_SLICE;
    lc->ctb_left_flag = rx > 0 && !(lc->boundary_flags & BOUNDARY_LEFT_TILE);
    lc->ctb_up_flag   = ry > 0 && !(lc->boundary_flags & BOUNDARY_UPPER_TILE) && !(lc->boundary_flags & BOUNDARY_UPPER_SLICE);
    lc->ctb_up_right_flag = lc->ctb_up_flag && (fc->ps.pps->ctb_to_col_bd[rx] == fc->ps.pps->ctb_to_col_bd[rx + 1]) &&
        (fc->ps.pps->ctb_to_row_bd[ry] == fc->ps.pps->ctb_to_row_bd[ry - 1]);
    lc->ctb_up_left_flag = lc->ctb_left_flag && lc->ctb_up_flag;
}

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

void ff_vvc_ctu_free_cus(CTU *ctu)
{
    CodingUnit **cus  = &ctu->cus;
    while (*cus) {
        CodingUnit *cu          = *cus;
        TransformUnit **head    = &cu->tus.head;

        *cus = cu->next;

        while (*head) {
            TransformUnit *tu = *head;
            *head = tu->next;
            ff_refstruct_unref(&tu);
        }
        cu->tus.tail = NULL;

        ff_refstruct_unref(&cu);
    }
}

void ff_vvc_ep_init_stat_coeff(EntryPoint *ep,
    const int bit_depth, const int persistent_rice_adaptation_enabled_flag)
{
    for (size_t i = 0; i < FF_ARRAY_ELEMS(ep->stat_coeff); ++i) {
        ep->stat_coeff[i] =
            persistent_rice_adaptation_enabled_flag ? 2 * (av_log2(bit_depth - 10)) : 0;
    }
}
