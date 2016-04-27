/*
 * HEVC video decoder
 *
 * Copyright (C) 2012 - 2013 Guillaume Martres
 * Copyright (C) 2013 Anand Meher Kotra
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

#include "hevc.h"

static const uint8_t l0_l1_cand_idx[12][2] = {
    { 0, 1, },
    { 1, 0, },
    { 0, 2, },
    { 2, 0, },
    { 1, 2, },
    { 2, 1, },
    { 0, 3, },
    { 3, 0, },
    { 1, 3, },
    { 3, 1, },
    { 2, 3, },
    { 3, 2, },
};

void ff_hevc_set_neighbour_available(HEVCContext *s, int x0, int y0,
                                     int nPbW, int nPbH)
{
    HEVCLocalContext *lc = &s->HEVClc;
    int x0b = x0 & ((1 << s->ps.sps->log2_ctb_size) - 1);
    int y0b = y0 & ((1 << s->ps.sps->log2_ctb_size) - 1);

    lc->na.cand_up       = (lc->ctb_up_flag   || y0b);
    lc->na.cand_left     = (lc->ctb_left_flag || x0b);
    lc->na.cand_up_left  = (!x0b && !y0b) ? lc->ctb_up_left_flag : lc->na.cand_left && lc->na.cand_up;
    lc->na.cand_up_right_sap =
            ((x0b + nPbW) == (1 << s->ps.sps->log2_ctb_size)) ?
                    lc->ctb_up_right_flag && !y0b : lc->na.cand_up;
    lc->na.cand_up_right =
            ((x0b + nPbW) == (1 << s->ps.sps->log2_ctb_size) ?
                    lc->ctb_up_right_flag && !y0b : lc->na.cand_up )
                     && (x0 + nPbW) < lc->end_of_tiles_x;
    lc->na.cand_bottom_left = ((y0 + nPbH) >= lc->end_of_tiles_y) ? 0 : lc->na.cand_left;
}

/*
 * 6.4.1 Derivation process for z-scan order block availability
 */
static int z_scan_block_avail(HEVCContext *s, int xCurr, int yCurr,
                              int xN, int yN)
{
#define MIN_TB_ADDR_ZS(x, y)                                            \
    s->ps.pps->min_tb_addr_zs[(y) * s->ps.sps->min_tb_width + (x)]
    int Curr = MIN_TB_ADDR_ZS(xCurr >> s->ps.sps->log2_min_tb_size,
                              yCurr >> s->ps.sps->log2_min_tb_size);
    int N;

    if (xN < 0 || yN < 0 ||
        xN >= s->ps.sps->width ||
        yN >= s->ps.sps->height)
        return 0;

    N = MIN_TB_ADDR_ZS(xN >> s->ps.sps->log2_min_tb_size,
                       yN >> s->ps.sps->log2_min_tb_size);

    return N <= Curr;
}

static int same_prediction_block(HEVCLocalContext *lc, int log2_cb_size,
                                 int x0, int y0, int nPbW, int nPbH,
                                 int xA1, int yA1, int partIdx)
{
    return !(nPbW << 1 == 1 << log2_cb_size &&
             nPbH << 1 == 1 << log2_cb_size && partIdx == 1 &&
             lc->cu.x + nPbW > xA1 &&
             lc->cu.y + nPbH <= yA1);
}

/*
 * 6.4.2 Derivation process for prediction block availability
 */
static int check_prediction_block_available(HEVCContext *s, int log2_cb_size,
                                            int x0, int y0, int nPbW, int nPbH,
                                            int xA1, int yA1, int partIdx)
{
    HEVCLocalContext *lc = &s->HEVClc;

    if (lc->cu.x < xA1 && lc->cu.y < yA1 &&
        (lc->cu.x + (1 << log2_cb_size)) > xA1 &&
        (lc->cu.y + (1 << log2_cb_size)) > yA1)
        return same_prediction_block(lc, log2_cb_size, x0, y0,
                                     nPbW, nPbH, xA1, yA1, partIdx);
    else
        return z_scan_block_avail(s, x0, y0, xA1, yA1);
}

//check if the two luma locations belong to the same motion estimation region
static int isDiffMER(HEVCContext *s, int xN, int yN, int xP, int yP)
{
    uint8_t plevel = s->ps.pps->log2_parallel_merge_level;

    return xN >> plevel == xP >> plevel &&
           yN >> plevel == yP >> plevel;
}

#define MATCH_MV(x) (AV_RN32A(&A.x) == AV_RN32A(&B.x))
#define MATCH(x) (A.x == B.x)

// check if the mv's and refidx are the same between A and B
static int compareMVrefidx(struct MvField A, struct MvField B)
{
    if (A.pred_flag[0] && A.pred_flag[1] && B.pred_flag[0] && B.pred_flag[1])
        return MATCH(ref_idx[0]) && MATCH_MV(mv[0]) &&
               MATCH(ref_idx[1]) && MATCH_MV(mv[1]);

    if (A.pred_flag[0] && !A.pred_flag[1] && B.pred_flag[0] && !B.pred_flag[1])
        return MATCH(ref_idx[0]) && MATCH_MV(mv[0]);

    if (!A.pred_flag[0] && A.pred_flag[1] && !B.pred_flag[0] && B.pred_flag[1])
        return MATCH(ref_idx[1]) && MATCH_MV(mv[1]);

    return 0;
}

static av_always_inline void mv_scale(Mv *dst, Mv *src, int td, int tb)
{
    int tx, scale_factor;

    td = av_clip_int8(td);
    tb = av_clip_int8(tb);
    tx = (0x4000 + abs(td / 2)) / td;
    scale_factor = av_clip((tb * tx + 32) >> 6, -4096, 4095);
    dst->x = av_clip_int16((scale_factor * src->x + 127 +
                             (scale_factor * src->x < 0)) >> 8);
    dst->y = av_clip_int16((scale_factor * src->y + 127 +
                             (scale_factor * src->y < 0)) >> 8);
}

static int check_mvset(Mv *mvLXCol, Mv *mvCol,
                       int colPic, int poc,
                       RefPicList *refPicList, int X, int refIdxLx,
                       RefPicList *refPicList_col, int listCol, int refidxCol)
{
    int cur_lt = refPicList[X].isLongTerm[refIdxLx];
    int col_lt = refPicList_col[listCol].isLongTerm[refidxCol];
    int col_poc_diff, cur_poc_diff;

    if (cur_lt != col_lt) {
        mvLXCol->x = 0;
        mvLXCol->y = 0;
        return 0;
    }

    col_poc_diff = colPic - refPicList_col[listCol].list[refidxCol];
    cur_poc_diff = poc    - refPicList[X].list[refIdxLx];

    if (!col_poc_diff)
        col_poc_diff = 1;  // error resilience

    if (cur_lt || col_poc_diff == cur_poc_diff) {
        mvLXCol->x = mvCol->x;
        mvLXCol->y = mvCol->y;
    } else {
        mv_scale(mvLXCol, mvCol, col_poc_diff, cur_poc_diff);
    }
    return 1;
}

#define CHECK_MVSET(l)                                          \
    check_mvset(mvLXCol, temp_col.mv + l,                       \
                colPic, s->poc,                                 \
                refPicList, X, refIdxLx,                        \
                refPicList_col, L ## l, temp_col.ref_idx[l])

// derive the motion vectors section 8.5.3.1.8
static int derive_temporal_colocated_mvs(HEVCContext *s, MvField temp_col,
                                         int refIdxLx, Mv *mvLXCol, int X,
                                         int colPic, RefPicList *refPicList_col)
{
    RefPicList *refPicList = s->ref->refPicList;

    if (temp_col.is_intra) {
        mvLXCol->x = 0;
        mvLXCol->y = 0;
        return 0;
    }

    if (temp_col.pred_flag[0] == 0)
        return CHECK_MVSET(1);
    else if (temp_col.pred_flag[0] == 1 && temp_col.pred_flag[1] == 0)
        return CHECK_MVSET(0);
    else if (temp_col.pred_flag[0] == 1 && temp_col.pred_flag[1] == 1) {
        int check_diffpicount = 0;
        int i = 0;
        for (i = 0; i < refPicList[0].nb_refs; i++) {
            if (refPicList[0].list[i] > s->poc)
                check_diffpicount++;
        }
        for (i = 0; i < refPicList[1].nb_refs; i++) {
            if (refPicList[1].list[i] > s->poc)
                check_diffpicount++;
        }
        if (check_diffpicount == 0 && X == 0)
            return CHECK_MVSET(0);
        else if (check_diffpicount == 0 && X == 1)
            return CHECK_MVSET(1);
        else {
            if (s->sh.collocated_list == L1)
                return CHECK_MVSET(0);
            else
                return CHECK_MVSET(1);
        }
    }

    return 0;
}

#define TAB_MVF(x, y)                                                   \
    tab_mvf[(y) * min_pu_width + x]

#define TAB_MVF_PU(v)                                                   \
    TAB_MVF(x ## v ## _pu, y ## v ## _pu)

#define DERIVE_TEMPORAL_COLOCATED_MVS                                   \
    derive_temporal_colocated_mvs(s, temp_col,                          \
                                  refIdxLx, mvLXCol, X, colPic,         \
                                  ff_hevc_get_ref_list(s, ref, x, y))

/*
 * 8.5.3.1.7  temporal luma motion vector prediction
 */
static int temporal_luma_motion_vector(HEVCContext *s, int x0, int y0,
                                       int nPbW, int nPbH, int refIdxLx,
                                       Mv *mvLXCol, int X)
{
    MvField *tab_mvf;
    MvField temp_col;
    int x, y, x_pu, y_pu;
    int min_pu_width = s->ps.sps->min_pu_width;
    int availableFlagLXCol = 0;
    int colPic;

    HEVCFrame *ref = s->ref->collocated_ref;

    if (!ref) {
        memset(mvLXCol, 0, sizeof(*mvLXCol));
        return 0;
    }

    tab_mvf = ref->tab_mvf;
    colPic  = ref->poc;

    //bottom right collocated motion vector
    x = x0 + nPbW;
    y = y0 + nPbH;

    if (tab_mvf &&
        (y0 >> s->ps.sps->log2_ctb_size) == (y >> s->ps.sps->log2_ctb_size) &&
        y < s->ps.sps->height &&
        x < s->ps.sps->width) {
        x                 &= ~15;
        y                 &= ~15;
        ff_thread_await_progress(&ref->tf, y, 0);
        x_pu               = x >> s->ps.sps->log2_min_pu_size;
        y_pu               = y >> s->ps.sps->log2_min_pu_size;
        temp_col           = TAB_MVF(x_pu, y_pu);
        availableFlagLXCol = DERIVE_TEMPORAL_COLOCATED_MVS;
    }

    // derive center collocated motion vector
    if (tab_mvf && !availableFlagLXCol) {
        x                  = x0 + (nPbW >> 1);
        y                  = y0 + (nPbH >> 1);
        x                 &= ~15;
        y                 &= ~15;
        ff_thread_await_progress(&ref->tf, y, 0);
        x_pu               = x >> s->ps.sps->log2_min_pu_size;
        y_pu               = y >> s->ps.sps->log2_min_pu_size;
        temp_col           = TAB_MVF(x_pu, y_pu);
        availableFlagLXCol = DERIVE_TEMPORAL_COLOCATED_MVS;
    }
    return availableFlagLXCol;
}

#define AVAILABLE(cand, v)                                      \
    (cand && !TAB_MVF_PU(v).is_intra)

#define PRED_BLOCK_AVAILABLE(v)                                 \
    check_prediction_block_available(s, log2_cb_size,           \
                                     x0, y0, nPbW, nPbH,        \
                                     x ## v, y ## v, part_idx)

#define COMPARE_MV_REFIDX(a, b)                                 \
    compareMVrefidx(TAB_MVF_PU(a), TAB_MVF_PU(b))

/*
 * 8.5.3.1.2  Derivation process for spatial merging candidates
 */
static void derive_spatial_merge_candidates(HEVCContext *s, int x0, int y0,
                                            int nPbW, int nPbH,
                                            int log2_cb_size,
                                            int singleMCLFlag, int part_idx,
                                            int merge_idx,
                                            struct MvField mergecandlist[])
{
    HEVCLocalContext *lc   = &s->HEVClc;
    RefPicList *refPicList = s->ref->refPicList;
    MvField *tab_mvf       = s->ref->tab_mvf;

    const int min_pu_width = s->ps.sps->min_pu_width;

    const int cand_bottom_left = lc->na.cand_bottom_left;
    const int cand_left        = lc->na.cand_left;
    const int cand_up_left     = lc->na.cand_up_left;
    const int cand_up          = lc->na.cand_up;
    const int cand_up_right    = lc->na.cand_up_right_sap;

    const int xA1    = x0 - 1;
    const int yA1    = y0 + nPbH - 1;
    const int xA1_pu = xA1 >> s->ps.sps->log2_min_pu_size;
    const int yA1_pu = yA1 >> s->ps.sps->log2_min_pu_size;

    const int xB1    = x0 + nPbW - 1;
    const int yB1    = y0 - 1;
    const int xB1_pu = xB1 >> s->ps.sps->log2_min_pu_size;
    const int yB1_pu = yB1 >> s->ps.sps->log2_min_pu_size;

    const int xB0    = x0 + nPbW;
    const int yB0    = y0 - 1;
    const int xB0_pu = xB0 >> s->ps.sps->log2_min_pu_size;
    const int yB0_pu = yB0 >> s->ps.sps->log2_min_pu_size;

    const int xA0    = x0 - 1;
    const int yA0    = y0 + nPbH;
    const int xA0_pu = xA0 >> s->ps.sps->log2_min_pu_size;
    const int yA0_pu = yA0 >> s->ps.sps->log2_min_pu_size;

    const int xB2    = x0 - 1;
    const int yB2    = y0 - 1;
    const int xB2_pu = xB2 >> s->ps.sps->log2_min_pu_size;
    const int yB2_pu = yB2 >> s->ps.sps->log2_min_pu_size;

    const int nb_refs = (s->sh.slice_type == P_SLICE) ?
                        s->sh.nb_refs[0] : FFMIN(s->sh.nb_refs[0], s->sh.nb_refs[1]);
    int check_MER   = 1;
    int check_MER_1 = 1;

    int zero_idx = 0;

    int nb_merge_cand = 0;
    int nb_orig_merge_cand = 0;

    int is_available_a0;
    int is_available_a1;
    int is_available_b0;
    int is_available_b1;
    int is_available_b2;
    int check_B0;
    int check_A0;

    //first left spatial merge candidate
    is_available_a1 = AVAILABLE(cand_left, A1);

    if (!singleMCLFlag && part_idx == 1 &&
        (lc->cu.part_mode == PART_Nx2N ||
         lc->cu.part_mode == PART_nLx2N ||
         lc->cu.part_mode == PART_nRx2N) ||
        isDiffMER(s, xA1, yA1, x0, y0)) {
        is_available_a1 = 0;
    }

    if (is_available_a1) {
        mergecandlist[0] = TAB_MVF_PU(A1);
        if (merge_idx == 0)
            return;
        nb_merge_cand++;
    }

    // above spatial merge candidate
    is_available_b1 = AVAILABLE(cand_up, B1);

    if (!singleMCLFlag && part_idx == 1 &&
        (lc->cu.part_mode == PART_2NxN ||
         lc->cu.part_mode == PART_2NxnU ||
         lc->cu.part_mode == PART_2NxnD) ||
        isDiffMER(s, xB1, yB1, x0, y0)) {
        is_available_b1 = 0;
    }

    if (is_available_a1 && is_available_b1)
        check_MER = !COMPARE_MV_REFIDX(B1, A1);

    if (is_available_b1 && check_MER)
        mergecandlist[nb_merge_cand++] = TAB_MVF_PU(B1);

    // above right spatial merge candidate
    check_MER = 1;
    check_B0  = PRED_BLOCK_AVAILABLE(B0);

    is_available_b0 = check_B0 && AVAILABLE(cand_up_right, B0);

    if (isDiffMER(s, xB0, yB0, x0, y0))
        is_available_b0 = 0;

    if (is_available_b1 && is_available_b0)
        check_MER = !COMPARE_MV_REFIDX(B0, B1);

    if (is_available_b0 && check_MER) {
        mergecandlist[nb_merge_cand] = TAB_MVF_PU(B0);
        if (merge_idx == nb_merge_cand)
            return;
        nb_merge_cand++;
    }

    // left bottom spatial merge candidate
    check_MER = 1;
    check_A0  = PRED_BLOCK_AVAILABLE(A0);

    is_available_a0 = check_A0 && AVAILABLE(cand_bottom_left, A0);

    if (isDiffMER(s, xA0, yA0, x0, y0))
        is_available_a0 = 0;

    if (is_available_a1 && is_available_a0)
        check_MER = !COMPARE_MV_REFIDX(A0, A1);

    if (is_available_a0 && check_MER) {
        mergecandlist[nb_merge_cand] = TAB_MVF_PU(A0);
        if (merge_idx == nb_merge_cand)
            return;
        nb_merge_cand++;
    }

    // above left spatial merge candidate
    check_MER = 1;

    is_available_b2 = AVAILABLE(cand_up_left, B2);

    if (isDiffMER(s, xB2, yB2, x0, y0))
        is_available_b2 = 0;

    if (is_available_a1 && is_available_b2)
        check_MER = !COMPARE_MV_REFIDX(B2, A1);

    if (is_available_b1 && is_available_b2)
        check_MER_1 = !COMPARE_MV_REFIDX(B2, B1);

    if (is_available_b2 && check_MER && check_MER_1 && nb_merge_cand != 4) {
        mergecandlist[nb_merge_cand] = TAB_MVF_PU(B2);
        if (merge_idx == nb_merge_cand)
            return;
        nb_merge_cand++;
    }

    // temporal motion vector candidate
    if (s->sh.slice_temporal_mvp_enabled_flag &&
        nb_merge_cand < s->sh.max_num_merge_cand) {
        Mv mv_l0_col = { 0 }, mv_l1_col = { 0 };
        int available_l0 = temporal_luma_motion_vector(s, x0, y0, nPbW, nPbH,
                                                       0, &mv_l0_col, 0);
        int available_l1 = (s->sh.slice_type == B_SLICE) ?
                           temporal_luma_motion_vector(s, x0, y0, nPbW, nPbH,
                                                       0, &mv_l1_col, 1) : 0;

        if (available_l0 || available_l1) {
            mergecandlist[nb_merge_cand].is_intra     = 0;
            mergecandlist[nb_merge_cand].pred_flag[0] = available_l0;
            mergecandlist[nb_merge_cand].pred_flag[1] = available_l1;
            AV_ZERO16(mergecandlist[nb_merge_cand].ref_idx);
            mergecandlist[nb_merge_cand].mv[0]      = mv_l0_col;
            mergecandlist[nb_merge_cand].mv[1]      = mv_l1_col;

            if (merge_idx == nb_merge_cand)
                return;
            nb_merge_cand++;
        }
    }

    nb_orig_merge_cand = nb_merge_cand;

    // combined bi-predictive merge candidates  (applies for B slices)
    if (s->sh.slice_type == B_SLICE && nb_orig_merge_cand > 1 &&
        nb_orig_merge_cand < s->sh.max_num_merge_cand) {
        int comb_idx;

        for (comb_idx = 0; nb_merge_cand < s->sh.max_num_merge_cand &&
                           comb_idx < nb_orig_merge_cand * (nb_orig_merge_cand - 1); comb_idx++) {
            int l0_cand_idx = l0_l1_cand_idx[comb_idx][0];
            int l1_cand_idx = l0_l1_cand_idx[comb_idx][1];
            MvField l0_cand = mergecandlist[l0_cand_idx];
            MvField l1_cand = mergecandlist[l1_cand_idx];

            if (l0_cand.pred_flag[0] && l1_cand.pred_flag[1] &&
                (refPicList[0].list[l0_cand.ref_idx[0]] !=
                 refPicList[1].list[l1_cand.ref_idx[1]] ||
                 AV_RN32A(&l0_cand.mv[0]) != AV_RN32A(&l1_cand.mv[1]))) {
                mergecandlist[nb_merge_cand].ref_idx[0]   = l0_cand.ref_idx[0];
                mergecandlist[nb_merge_cand].ref_idx[1]   = l1_cand.ref_idx[1];
                mergecandlist[nb_merge_cand].pred_flag[0] = 1;
                mergecandlist[nb_merge_cand].pred_flag[1] = 1;
                AV_COPY32(&mergecandlist[nb_merge_cand].mv[0], &l0_cand.mv[0]);
                AV_COPY32(&mergecandlist[nb_merge_cand].mv[1], &l1_cand.mv[1]);
                mergecandlist[nb_merge_cand].is_intra     = 0;
                if (merge_idx == nb_merge_cand)
                    return;
                nb_merge_cand++;
            }
        }
    }

    // append Zero motion vector candidates
    while (nb_merge_cand < s->sh.max_num_merge_cand) {
        mergecandlist[nb_merge_cand].pred_flag[0] = 1;
        mergecandlist[nb_merge_cand].pred_flag[1] = s->sh.slice_type == B_SLICE;
        AV_ZERO32(mergecandlist[nb_merge_cand].mv + 0);
        AV_ZERO32(mergecandlist[nb_merge_cand].mv + 1);
        mergecandlist[nb_merge_cand].is_intra     = 0;
        mergecandlist[nb_merge_cand].ref_idx[0]   = zero_idx < nb_refs ? zero_idx : 0;
        mergecandlist[nb_merge_cand].ref_idx[1]   = zero_idx < nb_refs ? zero_idx : 0;

        if (merge_idx == nb_merge_cand)
            return;
        nb_merge_cand++;
        zero_idx++;
    }
}

/*
 * 8.5.3.1.1 Derivation process of luma Mvs for merge mode
 */
void ff_hevc_luma_mv_merge_mode(HEVCContext *s, int x0, int y0, int nPbW,
                                int nPbH, int log2_cb_size, int part_idx,
                                int merge_idx, MvField *mv)
{
    int singleMCLFlag = 0;
    int nCS = 1 << log2_cb_size;
    LOCAL_ALIGNED(4, MvField, mergecand_list, [MRG_MAX_NUM_CANDS]);
    int nPbW2 = nPbW;
    int nPbH2 = nPbH;
    HEVCLocalContext *lc = &s->HEVClc;

    if (s->ps.pps->log2_parallel_merge_level > 2 && nCS == 8) {
        singleMCLFlag = 1;
        x0            = lc->cu.x;
        y0            = lc->cu.y;
        nPbW          = nCS;
        nPbH          = nCS;
        part_idx      = 0;
    }

    ff_hevc_set_neighbour_available(s, x0, y0, nPbW, nPbH);
    derive_spatial_merge_candidates(s, x0, y0, nPbW, nPbH, log2_cb_size,
                                    singleMCLFlag, part_idx,
                                    merge_idx, mergecand_list);

    if (mergecand_list[merge_idx].pred_flag[0] == 1 &&
        mergecand_list[merge_idx].pred_flag[1] == 1 &&
        (nPbW2 + nPbH2) == 12) {
        mergecand_list[merge_idx].ref_idx[1]   = -1;
        mergecand_list[merge_idx].pred_flag[1] = 0;
    }

    *mv = mergecand_list[merge_idx];
}

static av_always_inline void dist_scale(HEVCContext *s, Mv *mv,
                                        int min_pu_width, int x, int y,
                                        int elist, int ref_idx_curr, int ref_idx)
{
    RefPicList *refPicList = s->ref->refPicList;
    MvField *tab_mvf       = s->ref->tab_mvf;
    int ref_pic_elist      = refPicList[elist].list[TAB_MVF(x, y).ref_idx[elist]];
    int ref_pic_curr       = refPicList[ref_idx_curr].list[ref_idx];

    if (ref_pic_elist != ref_pic_curr) {
        int poc_diff = s->poc - ref_pic_elist;
        if (!poc_diff)
            poc_diff = 1;
        mv_scale(mv, mv, poc_diff, s->poc - ref_pic_curr);
    }
}

static int mv_mp_mode_mx(HEVCContext *s, int x, int y, int pred_flag_index,
                         Mv *mv, int ref_idx_curr, int ref_idx)
{
    MvField *tab_mvf = s->ref->tab_mvf;
    int min_pu_width = s->ps.sps->min_pu_width;

    RefPicList *refPicList = s->ref->refPicList;

    if (TAB_MVF(x, y).pred_flag[pred_flag_index] == 1 &&
        refPicList[pred_flag_index].list[TAB_MVF(x, y).ref_idx[pred_flag_index]] == refPicList[ref_idx_curr].list[ref_idx]) {
        *mv = TAB_MVF(x, y).mv[pred_flag_index];
        return 1;
    }
    return 0;
}

static int mv_mp_mode_mx_lt(HEVCContext *s, int x, int y, int pred_flag_index,
                            Mv *mv, int ref_idx_curr, int ref_idx)
{
    MvField *tab_mvf = s->ref->tab_mvf;
    int min_pu_width = s->ps.sps->min_pu_width;

    RefPicList *refPicList = s->ref->refPicList;
    int currIsLongTerm     = refPicList[ref_idx_curr].isLongTerm[ref_idx];

    int colIsLongTerm =
        refPicList[pred_flag_index].isLongTerm[(TAB_MVF(x, y).ref_idx[pred_flag_index])];

    if (TAB_MVF(x, y).pred_flag[pred_flag_index] &&
        colIsLongTerm == currIsLongTerm) {
        *mv = TAB_MVF(x, y).mv[pred_flag_index];
        if (!currIsLongTerm)
            dist_scale(s, mv, min_pu_width, x, y,
                       pred_flag_index, ref_idx_curr, ref_idx);
        return 1;
    }
    return 0;
}

#define MP_MX(v, pred, mx)                                      \
    mv_mp_mode_mx(s, x ## v ## _pu, y ## v ## _pu, pred,        \
                  &mx, ref_idx_curr, ref_idx)

#define MP_MX_LT(v, pred, mx)                                   \
    mv_mp_mode_mx_lt(s, x ## v ## _pu, y ## v ## _pu, pred,     \
                     &mx, ref_idx_curr, ref_idx)

void ff_hevc_luma_mv_mvp_mode(HEVCContext *s, int x0, int y0, int nPbW,
                              int nPbH, int log2_cb_size, int part_idx,
                              int merge_idx, MvField *mv,
                              int mvp_lx_flag, int LX)
{
    HEVCLocalContext *lc = &s->HEVClc;
    MvField *tab_mvf = s->ref->tab_mvf;
    int isScaledFlag_L0 = 0;
    int availableFlagLXA0 = 0;
    int availableFlagLXB0 = 0;
    int numMVPCandLX = 0;
    int min_pu_width = s->ps.sps->min_pu_width;

    int xA0, yA0;
    int xA0_pu, yA0_pu;
    int is_available_a0;

    int xA1, yA1;
    int xA1_pu, yA1_pu;
    int is_available_a1;

    int xB0, yB0;
    int xB0_pu, yB0_pu;
    int is_available_b0;

    int xB1, yB1;
    int xB1_pu = 0, yB1_pu = 0;
    int is_available_b1 = 0;

    int xB2, yB2;
    int xB2_pu = 0, yB2_pu = 0;
    int is_available_b2 = 0;
    Mv mvpcand_list[2] = { { 0 } };
    Mv mxA = { 0 };
    Mv mxB = { 0 };
    int ref_idx_curr = 0;
    int ref_idx = 0;
    int pred_flag_index_l0;
    int pred_flag_index_l1;
    int x0b = x0 & ((1 << s->ps.sps->log2_ctb_size) - 1);
    int y0b = y0 & ((1 << s->ps.sps->log2_ctb_size) - 1);

    int cand_up = (lc->ctb_up_flag || y0b);
    int cand_left = (lc->ctb_left_flag || x0b);
    int cand_up_left =
            (!x0b && !y0b) ? lc->ctb_up_left_flag : cand_left && cand_up;
    int cand_up_right =
            (x0b + nPbW == (1 << s->ps.sps->log2_ctb_size) ||
             x0  + nPbW >= lc->end_of_tiles_x) ? lc->ctb_up_right_flag && !y0b
                                               : cand_up;
    int cand_bottom_left = (y0 + nPbH >= lc->end_of_tiles_y) ? 0 : cand_left;

    ref_idx_curr       = LX;
    ref_idx            = mv->ref_idx[LX];
    pred_flag_index_l0 = LX;
    pred_flag_index_l1 = !LX;

    // left bottom spatial candidate
    xA0 = x0 - 1;
    yA0 = y0 + nPbH;
    xA0_pu = xA0 >> s->ps.sps->log2_min_pu_size;
    yA0_pu = yA0 >> s->ps.sps->log2_min_pu_size;

    is_available_a0 = PRED_BLOCK_AVAILABLE(A0) && AVAILABLE(cand_bottom_left, A0);

    //left spatial merge candidate
    xA1    = x0 - 1;
    yA1    = y0 + nPbH - 1;
    xA1_pu = xA1 >> s->ps.sps->log2_min_pu_size;
    yA1_pu = yA1 >> s->ps.sps->log2_min_pu_size;

    is_available_a1 = AVAILABLE(cand_left, A1);
    if (is_available_a0 || is_available_a1)
        isScaledFlag_L0 = 1;

    if (is_available_a0) {
        availableFlagLXA0 = MP_MX(A0, pred_flag_index_l0, mxA);
        if (!availableFlagLXA0)
            availableFlagLXA0 = MP_MX(A0, pred_flag_index_l1, mxA);
    }

    if (is_available_a1 && !availableFlagLXA0) {
        availableFlagLXA0 = MP_MX(A1, pred_flag_index_l0, mxA);
        if (!availableFlagLXA0)
            availableFlagLXA0 = MP_MX(A1, pred_flag_index_l1, mxA);
    }

    if (is_available_a0 && !availableFlagLXA0) {
        availableFlagLXA0 = MP_MX_LT(A0, pred_flag_index_l0, mxA);
        if (!availableFlagLXA0)
            availableFlagLXA0 = MP_MX_LT(A0, pred_flag_index_l1, mxA);
    }

    if (is_available_a1 && !availableFlagLXA0) {
        availableFlagLXA0 = MP_MX_LT(A1, pred_flag_index_l0, mxA);
        if (!availableFlagLXA0)
            availableFlagLXA0 = MP_MX_LT(A1, pred_flag_index_l1, mxA);
    }

    if (availableFlagLXA0 && !mvp_lx_flag) {
        mv->mv[LX] = mxA;
        return;
    }

    // B candidates
    // above right spatial merge candidate
    xB0    = x0 + nPbW;
    yB0    = y0 - 1;
    xB0_pu = xB0 >> s->ps.sps->log2_min_pu_size;
    yB0_pu = yB0 >> s->ps.sps->log2_min_pu_size;

    is_available_b0 = PRED_BLOCK_AVAILABLE(B0) && AVAILABLE(cand_up_right, B0);

    if (is_available_b0) {
        availableFlagLXB0 = MP_MX(B0, pred_flag_index_l0, mxB);
        if (!availableFlagLXB0)
            availableFlagLXB0 = MP_MX(B0, pred_flag_index_l1, mxB);
    }

    if (!availableFlagLXB0) {
        // above spatial merge candidate
        xB1    = x0 + nPbW - 1;
        yB1    = y0 - 1;
        xB1_pu = xB1 >> s->ps.sps->log2_min_pu_size;
        yB1_pu = yB1 >> s->ps.sps->log2_min_pu_size;

        is_available_b1 = AVAILABLE(cand_up, B1);

        if (is_available_b1) {
            availableFlagLXB0 = MP_MX(B1, pred_flag_index_l0, mxB);
            if (!availableFlagLXB0)
                availableFlagLXB0 = MP_MX(B1, pred_flag_index_l1, mxB);
        }
    }

    if (!availableFlagLXB0) {
        // above left spatial merge candidate
        xB2 = x0 - 1;
        yB2 = y0 - 1;
        xB2_pu = xB2 >> s->ps.sps->log2_min_pu_size;
        yB2_pu = yB2 >> s->ps.sps->log2_min_pu_size;
        is_available_b2 = AVAILABLE(cand_up_left, B2);

        if (is_available_b2) {
            availableFlagLXB0 = MP_MX(B2, pred_flag_index_l0, mxB);
            if (!availableFlagLXB0)
                availableFlagLXB0 = MP_MX(B2, pred_flag_index_l1, mxB);
        }
    }

    if (isScaledFlag_L0 == 0) {
        if (availableFlagLXB0) {
            availableFlagLXA0 = 1;
            mxA = mxB;
        }
        availableFlagLXB0 = 0;

        // XB0 and L1
        if (is_available_b0) {
            availableFlagLXB0 = MP_MX_LT(B0, pred_flag_index_l0, mxB);
            if (!availableFlagLXB0)
                availableFlagLXB0 = MP_MX_LT(B0, pred_flag_index_l1, mxB);
        }

        if (is_available_b1 && !availableFlagLXB0) {
            availableFlagLXB0 = MP_MX_LT(B1, pred_flag_index_l0, mxB);
            if (!availableFlagLXB0)
                availableFlagLXB0 = MP_MX_LT(B1, pred_flag_index_l1, mxB);
        }

        if (is_available_b2 && !availableFlagLXB0) {
            availableFlagLXB0 = MP_MX_LT(B2, pred_flag_index_l0, mxB);
            if (!availableFlagLXB0)
                availableFlagLXB0 = MP_MX_LT(B2, pred_flag_index_l1, mxB);
        }
    }

    if (availableFlagLXA0)
        mvpcand_list[numMVPCandLX++] = mxA;

    if (availableFlagLXB0 && (!availableFlagLXA0 || mxA.x != mxB.x || mxA.y != mxB.y))
        mvpcand_list[numMVPCandLX++] = mxB;

    //temporal motion vector prediction candidate
    if (numMVPCandLX < 2 && s->sh.slice_temporal_mvp_enabled_flag &&
        mvp_lx_flag == numMVPCandLX) {
        Mv mv_col;
        int available_col = temporal_luma_motion_vector(s, x0, y0, nPbW,
                                                        nPbH, ref_idx,
                                                        &mv_col, LX);
        if (available_col)
            mvpcand_list[numMVPCandLX++] = mv_col;
    }

    // insert zero motion vectors when the number of available candidates are less than 2
    while (numMVPCandLX < 2)
        mvpcand_list[numMVPCandLX++] = (Mv){ 0, 0 };

    mv->mv[LX].x = mvpcand_list[mvp_lx_flag].x;
    mv->mv[LX].y = mvpcand_list[mvp_lx_flag].y;
}
