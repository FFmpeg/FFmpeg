/*
 * VVC motion vector decoder
 *
 * Copyright (C) 2023 Nuo Mi
 * Copyright (C) 2022 Xu Mu
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

#include "ctu.h"
#include "data.h"
#include "refs.h"
#include "mvs.h"

#define IS_SAME_MV(a, b) (AV_RN64A(a) == AV_RN64A(b))

//check if the two luma locations belong to the same motion estimation region
static av_always_inline int is_same_mer(const VVCFrameContext *fc, const int xN, const int yN, const int xP, const int yP)
{
    const uint8_t plevel = fc->ps.sps->log2_parallel_merge_level;

    return xN >> plevel == xP >> plevel &&
           yN >> plevel == yP >> plevel;
}

//return true if we have same mvs and ref_idxs
static av_always_inline int compare_mv_ref_idx(const MvField *n, const MvField *o)
{
    if (!o || n->pred_flag != o->pred_flag)
        return 0;
    for (int i = 0; i < 2; i++) {
        PredFlag mask = i + 1;
        if (n->pred_flag & mask) {
            const int same_ref_idx = n->ref_idx[i] == o->ref_idx[i];
            const int same_mv = IS_SAME_MV(n->mv + i, o->mv + i);
            if (!same_ref_idx || !same_mv)
                return 0;
        }
    }
    return 1;
}

// 8.5.2.15 Temporal motion buffer compression process for collocated motion vectors
static av_always_inline void mv_compression(Mv *motion)
{
    int mv[2] = {motion->x, motion->y};
    for (int i = 0; i < 2; i++) {
        const int s = mv[i] >> 17;
        const int f = av_log2((mv[i] ^ s) | 31) - 4;
        const int mask  = (-1 * (1 << f)) >> 1;
        const int round = (1 << f) >> 2;
        mv[i] = (mv[i] + round) & mask;
    }
    motion->x = mv[0];
    motion->y = mv[1];
}

void ff_vvc_mv_scale(Mv *dst, const Mv *src, int td, int tb)
{
    int tx, scale_factor;

    td = av_clip_int8(td);
    tb = av_clip_int8(tb);
    tx = (0x4000 + (abs(td) >> 1)) / td;
    scale_factor = av_clip_intp2((tb * tx + 32) >> 6, 12);
    dst->x = av_clip_intp2((scale_factor * src->x + 127 +
                           (scale_factor * src->x < 0)) >> 8, 17);
    dst->y = av_clip_intp2((scale_factor * src->y + 127 +
                           (scale_factor * src->y < 0)) >> 8, 17);
}

//part of 8.5.2.12 Derivation process for collocated motion vectors
static int check_mvset(Mv *mvLXCol, Mv *mvCol,
                       int colPic, int poc,
                       const RefPicList *refPicList, int X, int refIdxLx,
                       const RefPicList *refPicList_col, int listCol, int refidxCol)
{
    int cur_lt = refPicList[X].refs[refIdxLx].is_lt;
    int col_lt = refPicList_col[listCol].refs[refidxCol].is_lt;
    int col_poc_diff, cur_poc_diff;

    if (cur_lt != col_lt) {
        mvLXCol->x = 0;
        mvLXCol->y = 0;
        return 0;
    }

    col_poc_diff = colPic - refPicList_col[listCol].refs[refidxCol].poc;
    cur_poc_diff = poc    - refPicList[X].refs[refIdxLx].poc;

    mv_compression(mvCol);
    if (cur_lt || col_poc_diff == cur_poc_diff) {
        mvLXCol->x = av_clip_intp2(mvCol->x, 17);
        mvLXCol->y = av_clip_intp2(mvCol->y, 17);
    } else {
        ff_vvc_mv_scale(mvLXCol, mvCol, col_poc_diff, cur_poc_diff);
    }
    return 1;
}

#define CHECK_MVSET(l)                                          \
    check_mvset(mvLXCol, temp_col.mv + l,                       \
                colPic, fc->ps.ph.poc,                          \
                refPicList, X, refIdxLx,                        \
                refPicList_col, L ## l, temp_col.ref_idx[l])

//derive NoBackwardPredFlag
int ff_vvc_no_backward_pred_flag(const VVCLocalContext *lc)
{
    int check_diffpicount = 0;
    int i, j;
    const RefPicList *rpl = lc->sc->rpl;

    for (j = 0; j < 2; j++) {
        for (i = 0; i < lc->sc->sh.r->num_ref_idx_active[j]; i++) {
            if (rpl[j].refs[i].poc > lc->fc->ps.ph.poc) {
                check_diffpicount++;
                break;
            }
        }
    }
    return !check_diffpicount;
}

//8.5.2.12 Derivation process for collocated motion vectors
static int derive_temporal_colocated_mvs(const VVCLocalContext *lc, MvField temp_col,
                                         int refIdxLx, Mv *mvLXCol, int X,
                                         int colPic, const RefPicList *refPicList_col, int sb_flag)
{
    const VVCFrameContext *fc   = lc->fc;
    const SliceContext *sc      = lc->sc;
    RefPicList* refPicList      = sc->rpl;

    if (temp_col.pred_flag == PF_INTRA ||
        temp_col.pred_flag == PF_IBC   ||
        temp_col.pred_flag == PF_PLT)
        return 0;

    if (sb_flag){
        if (X == 0) {
            if (temp_col.pred_flag & PF_L0)
                return CHECK_MVSET(0);
            else if (ff_vvc_no_backward_pred_flag(lc) && (temp_col.pred_flag & PF_L1))
                return CHECK_MVSET(1);
        } else {
            if (temp_col.pred_flag & PF_L1)
                return CHECK_MVSET(1);
            else if (ff_vvc_no_backward_pred_flag(lc) && (temp_col.pred_flag & PF_L0))
                return CHECK_MVSET(0);
        }
    } else {
        if (!(temp_col.pred_flag & PF_L0))
            return CHECK_MVSET(1);
        else if (temp_col.pred_flag == PF_L0)
            return CHECK_MVSET(0);
        else if (temp_col.pred_flag == PF_BI) {
            if (ff_vvc_no_backward_pred_flag(lc)) {
                if (X == 0)
                    return CHECK_MVSET(0);
                else
                    return CHECK_MVSET(1);
            } else {
                if (!lc->sc->sh.r->sh_collocated_from_l0_flag)
                    return CHECK_MVSET(0);
                else
                    return CHECK_MVSET(1);
            }
        }
    }
    return 0;
}

#define TAB_MVF(x, y)                                                   \
    tab_mvf[((y) >> MIN_PU_LOG2) * min_pu_width + ((x) >> MIN_PU_LOG2)]

#define TAB_MVF_PU(v)                                                   \
    TAB_MVF(x ## v, y ## v)

#define TAB_CP_MV(lx, x, y)                                              \
    fc->tab.cp_mv[lx][((((y) >> min_cb_log2_size) * min_cb_width + ((x) >> min_cb_log2_size)) ) * MAX_CONTROL_POINTS]


#define DERIVE_TEMPORAL_COLOCATED_MVS(sb_flag)                          \
    derive_temporal_colocated_mvs(lc, temp_col,                          \
                                  refIdxLx, mvLXCol, X, colPic,         \
                                  ff_vvc_get_ref_list(fc, ref, x, y), sb_flag)

//8.5.2.11 Derivation process for temporal luma motion vector prediction
static int temporal_luma_motion_vector(const VVCLocalContext *lc,
    const int refIdxLx, Mv *mvLXCol, const int X, int check_center, int sb_flag)
{
    const VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps         = fc->ps.sps;
    const VVCPPS *pps         = fc->ps.pps;
    const CodingUnit *cu      = lc->cu;
    const int subpic_idx      = lc->sc->sh.r->curr_subpic_idx;
    int x, y, x_end, y_end, colPic, availableFlagLXCol = 0;
    int min_pu_width = fc->ps.pps->min_pu_width;
    VVCFrame *ref = fc->ref->collocated_ref;
    MvField *tab_mvf;
    MvField temp_col;

    if (!ref) {
        memset(mvLXCol, 0, sizeof(*mvLXCol));
        return 0;
    }

    if (!fc->ps.ph.r->ph_temporal_mvp_enabled_flag || (cu->cb_width * cu->cb_height <= 32))
        return 0;

    tab_mvf = ref->tab_dmvr_mvf;
    colPic  = ref->poc;

    //bottom right collocated motion vector
    x = cu->x0 + cu->cb_width;
    y = cu->y0 + cu->cb_height;

    x_end = pps->subpic_x[subpic_idx] + pps->subpic_width[subpic_idx];
    y_end = pps->subpic_y[subpic_idx] + pps->subpic_height[subpic_idx];

    if (tab_mvf &&
        (cu->y0 >> sps->ctb_log2_size_y) == (y >> sps->ctb_log2_size_y) &&
        x < x_end && y < y_end) {
        x                 &= ~7;
        y                 &= ~7;
        temp_col           = TAB_MVF(x, y);
        availableFlagLXCol = DERIVE_TEMPORAL_COLOCATED_MVS(sb_flag);
    }
    if (check_center) {
        // derive center collocated motion vector
        if (tab_mvf && !availableFlagLXCol) {
            x                  = cu->x0 + (cu->cb_width >> 1);
            y                  = cu->y0 + (cu->cb_height >> 1);
            x                 &= ~7;
            y                 &= ~7;
            temp_col           = TAB_MVF(x, y);
            availableFlagLXCol = DERIVE_TEMPORAL_COLOCATED_MVS(sb_flag);
        }
    }
    return availableFlagLXCol;
}

void ff_vvc_set_mvf(const VVCLocalContext *lc, const int x0, const int y0, const int w, const int h, const MvField *mvf)
{
    const VVCFrameContext *fc   = lc->fc;
    MvField *tab_mvf            = fc->tab.mvf;
    const int min_pu_width      = fc->ps.pps->min_pu_width;
    const int min_pu_size       = 1 << MIN_PU_LOG2;
    for (int dy = 0; dy < h; dy += min_pu_size) {
        for (int dx = 0; dx < w; dx += min_pu_size) {
            const int x = x0 + dx;
            const int y = y0 + dy;
            TAB_MVF(x, y) = *mvf;
        }
    }
}

void ff_vvc_set_intra_mvf(const VVCLocalContext *lc, const bool dmvr, const PredFlag pf, const bool ciip_flag)
{
    const VVCFrameContext *fc   = lc->fc;
    const CodingUnit *cu        = lc->cu;
    MvField *tab_mvf            = dmvr ? fc->ref->tab_dmvr_mvf : fc->tab.mvf;
    const int min_pu_width      = fc->ps.pps->min_pu_width;
    const int min_pu_size       = 1 << MIN_PU_LOG2;
    for (int dy = 0; dy < cu->cb_height; dy += min_pu_size) {
        for (int dx = 0; dx < cu->cb_width; dx += min_pu_size) {
            const int x = cu->x0 + dx;
            const int y = cu->y0 + dy;
            MvField *mv = &TAB_MVF(x, y);

            mv->pred_flag = pf;
            mv->ciip_flag = ciip_flag;
        }
    }
}

//cbProfFlagLX from 8.5.5.9 Derivation process for motion vector arrays from affine control point motion vectors
static int derive_cb_prof_flag_lx(const VVCLocalContext *lc, const PredictionUnit* pu, int lx, int is_fallback)
{
    const MotionInfo* mi    = &pu->mi;
    const Mv* cp_mv         = &mi->mv[lx][0];
    if (lc->fc->ps.ph.r->ph_prof_disabled_flag || is_fallback)
        return 0;
    if (mi->motion_model_idc == MOTION_4_PARAMS_AFFINE) {
        if (IS_SAME_MV(cp_mv, cp_mv + 1))
            return 0;
    }
    if (mi->motion_model_idc == MOTION_6_PARAMS_AFFINE) {
        if (IS_SAME_MV(cp_mv, cp_mv + 1) && IS_SAME_MV(cp_mv, cp_mv + 2))
            return 0;
    }
    if (lc->sc->rpl[lx].refs[mi->ref_idx[lx]].is_scaled)
        return 0;
    return 1;
}

typedef struct SubblockParams {
    int d_hor_x;
    int d_ver_x;
    int d_hor_y;
    int d_ver_y;
    int mv_scale_hor;
    int mv_scale_ver;
    int is_fallback;

    int cb_width;
    int cb_height;
} SubblockParams;

static int is_fallback_mode(const SubblockParams *sp, const PredFlag pred_flag)
{
    const int a = 4 * (2048 + sp->d_hor_x);
    const int b = 4 * sp->d_hor_y;
    const int c = 4 * (2048 + sp->d_ver_y);
    const int d = 4 * sp->d_ver_x;
    if (pred_flag == PF_BI) {
        const int max_w4 = FFMAX(0, FFMAX(a, FFMAX(b, a + b)));
        const int min_w4 = FFMIN(0, FFMIN(a, FFMIN(b, a + b)));
        const int max_h4 = FFMAX(0, FFMAX(c, FFMAX(d, c + d)));
        const int min_h4 = FFMIN(0, FFMIN(c, FFMIN(d, c + d)));
        const int bx_wx4 = ((max_w4 - min_w4) >> 11) + 9;
        const int bx_hx4 = ((max_h4 - min_h4) >> 11) + 9;
        return bx_wx4 * bx_hx4 > 225;
    } else {
        const int bx_wxh = (FFABS(a) >> 11) + 9;
        const int bx_hxh = (FFABS(d) >> 11) + 9;
        const int bx_wxv = (FFABS(b) >> 11) + 9;
        const int bx_hxv = (FFABS(c) >> 11) + 9;
        if (bx_wxh * bx_hxh <= 165 && bx_wxv * bx_hxv <= 165)
            return 0;
    }
    return 1;
}

static void init_subblock_params(SubblockParams *sp, const MotionInfo* mi,
    const int cb_width, const int cb_height, const int lx)
{
    const int log2_cbw  = av_log2(cb_width);
    const int log2_cbh  = av_log2(cb_height);
    const Mv* cp_mv     = mi->mv[lx];
    const int num_cp_mv = mi->motion_model_idc + 1;
    sp->d_hor_x = (cp_mv[1].x - cp_mv[0].x) * (1 << (MAX_CU_DEPTH - log2_cbw));
    sp->d_ver_x = (cp_mv[1].y - cp_mv[0].y) * (1 << (MAX_CU_DEPTH - log2_cbw));
    if (num_cp_mv == 3) {
        sp->d_hor_y = (cp_mv[2].x - cp_mv[0].x) * (1 << (MAX_CU_DEPTH - log2_cbh));
        sp->d_ver_y = (cp_mv[2].y - cp_mv[0].y) * (1 << (MAX_CU_DEPTH - log2_cbh));
    } else {
        sp->d_hor_y = -sp->d_ver_x;
        sp->d_ver_y = sp->d_hor_x;
    }
    sp->mv_scale_hor = (cp_mv[0].x) * (1 << MAX_CU_DEPTH);
    sp->mv_scale_ver = (cp_mv[0].y) * (1 << MAX_CU_DEPTH);
    sp->cb_width  = cb_width;
    sp->cb_height = cb_height;
    sp->is_fallback = is_fallback_mode(sp, mi->pred_flag);
}

static void derive_subblock_diff_mvs(const VVCLocalContext *lc, PredictionUnit* pu, const SubblockParams* sp, const int lx)
{
    pu->cb_prof_flag[lx] = derive_cb_prof_flag_lx(lc, pu, lx, sp->is_fallback);
    if (pu->cb_prof_flag[lx]) {
        const int dmv_limit = 1 << 5;
        const int pos_offset_x = 6 * (sp->d_hor_x + sp->d_hor_y);
        const int pos_offset_y = 6 * (sp->d_ver_x + sp->d_ver_y);
        for (int x = 0; x < AFFINE_MIN_BLOCK_SIZE; x++) {
            for (int y = 0; y < AFFINE_MIN_BLOCK_SIZE; y++) {
                LOCAL_ALIGNED_8(Mv, diff, [1]);
                diff->x = x * (sp->d_hor_x * (1 << 2)) + y * (sp->d_hor_y * (1 << 2)) - pos_offset_x;
                diff->y = x * (sp->d_ver_x * (1 << 2)) + y * (sp->d_ver_y * (1 << 2)) - pos_offset_y;
                ff_vvc_round_mv(diff, 0, 8);
                pu->diff_mv_x[lx][AFFINE_MIN_BLOCK_SIZE * y + x] = av_clip(diff->x, -dmv_limit + 1, dmv_limit - 1);
                pu->diff_mv_y[lx][AFFINE_MIN_BLOCK_SIZE * y + x] = av_clip(diff->y, -dmv_limit + 1, dmv_limit - 1);
            }
        }
    }
}

static void store_cp_mv(const VVCLocalContext *lc, const MotionInfo *mi, const int lx)
{
    VVCFrameContext *fc = lc->fc;
    const CodingUnit *cu = lc->cu;
    const int log2_min_cb_size = fc->ps.sps->min_cb_log2_size_y;
    const int min_cb_size = fc->ps.sps->min_cb_size_y;
    const int min_cb_width = fc->ps.pps->min_cb_width;
    const int num_cp_mv = mi->motion_model_idc + 1;

    for (int dy = 0; dy < cu->cb_height; dy += min_cb_size) {
        for (int dx = 0; dx < cu->cb_width; dx += min_cb_size) {
            const int x_cb = (cu->x0 + dx) >> log2_min_cb_size;
            const int y_cb = (cu->y0 + dy) >> log2_min_cb_size;
            const int offset = (y_cb * min_cb_width + x_cb) * MAX_CONTROL_POINTS;

            memcpy(&fc->tab.cp_mv[lx][offset], mi->mv[lx], sizeof(Mv) * num_cp_mv);
        }
    }
}

//8.5.5.9 Derivation process for motion vector arrays from affine control point motion vectors
void ff_vvc_store_sb_mvs(const VVCLocalContext *lc, PredictionUnit *pu)
{
    const CodingUnit *cu = lc->cu;
    const MotionInfo *mi = &pu->mi;
    const int sbw = cu->cb_width / mi->num_sb_x;
    const int sbh = cu->cb_height / mi->num_sb_y;
    SubblockParams params[2];
    MvField mvf = {0};

    mvf.pred_flag = mi->pred_flag;
    mvf.bcw_idx = mi->bcw_idx;
    mvf.hpel_if_idx = mi->hpel_if_idx;
    for (int i = 0; i < 2; i++) {
        const PredFlag mask = i + 1;
        if (mi->pred_flag & mask) {
            store_cp_mv(lc, mi, i);
            init_subblock_params(params + i, mi, cu->cb_width, cu->cb_height, i);
            derive_subblock_diff_mvs(lc, pu, params + i, i);
            mvf.ref_idx[i] = mi->ref_idx[i];
        }
    }

    for (int sby = 0; sby < mi->num_sb_y; sby++) {
        for (int sbx = 0; sbx < mi->num_sb_x; sbx++) {
            const int x0 = cu->x0 + sbx * sbw;
            const int y0 = cu->y0 + sby * sbh;
            for (int i = 0; i < 2; i++) {
                const PredFlag mask = i + 1;
                if (mi->pred_flag & mask) {
                    const SubblockParams* sp = params + i;
                    const int x_pos_cb = sp->is_fallback ? (cu->cb_width >> 1) : (2 + (sbx << MIN_CU_LOG2));
                    const int y_pos_cb = sp->is_fallback ? (cu->cb_height >> 1) : (2 + (sby << MIN_CU_LOG2));
                    Mv *mv = mvf.mv + i;

                    mv->x = sp->mv_scale_hor + sp->d_hor_x * x_pos_cb + sp->d_hor_y * y_pos_cb;
                    mv->y = sp->mv_scale_ver + sp->d_ver_x * x_pos_cb + sp->d_ver_y * y_pos_cb;
                    ff_vvc_round_mv(mv, 0, MAX_CU_DEPTH);
                    ff_vvc_clip_mv(mv);
                }
            }
            ff_vvc_set_mvf(lc, x0, y0, sbw, sbh, &mvf);
        }
    }
}

void ff_vvc_store_gpm_mvf(const VVCLocalContext *lc, const PredictionUnit *pu)
{
    const CodingUnit *cu     = lc->cu;
    const int angle_idx      = ff_vvc_gpm_angle_idx[pu->gpm_partition_idx];
    const int distance_idx   = ff_vvc_gpm_distance_idx[pu->gpm_partition_idx];
    const int displacement_x = ff_vvc_gpm_distance_lut[angle_idx];
    const int displacement_y = ff_vvc_gpm_distance_lut[(angle_idx + 8) % 32];
    const int is_flip        = angle_idx >= 13 &&angle_idx <= 27;
    const int shift_hor      = (angle_idx % 16 == 8 || (angle_idx % 16 && cu->cb_height >= cu->cb_width)) ? 0 : 1;
    const int sign           = angle_idx < 16 ? 1 : -1;
    const int block_size     = 4;
    int offset_x = (-cu->cb_width) >> 1;
    int offset_y = (-cu->cb_height) >> 1;

    if (!shift_hor)
        offset_y += sign * ((distance_idx * cu->cb_height) >> 3);
    else
        offset_x += sign * ((distance_idx * cu->cb_width) >> 3);

    for (int y = 0; y < cu->cb_height; y += block_size) {
        for (int x = 0; x < cu->cb_width; x += block_size) {
            const int motion_idx = (((x + offset_x) * (1 << 1)) + 5) * displacement_x +
                (((y + offset_y) * (1 << 1)) + 5) * displacement_y;
            const int s_type = FFABS(motion_idx) < 32 ? 2 : (motion_idx <= 0 ? (1 - is_flip) : is_flip);
            const int pred_flag = pu->gpm_mv[0].pred_flag | pu->gpm_mv[1].pred_flag;
            const int x0 = cu->x0 + x;
            const int y0 = cu->y0 + y;

            if (!s_type)
                ff_vvc_set_mvf(lc, x0, y0, block_size, block_size, pu->gpm_mv + 0);
            else if (s_type == 1 || (s_type == 2 && pred_flag != PF_BI))
                ff_vvc_set_mvf(lc, x0, y0, block_size, block_size, pu->gpm_mv + 1);
            else {
                MvField mvf  = pu->gpm_mv[0];
                const MvField *mv1 = &pu->gpm_mv[1];
                const int lx =  mv1->pred_flag - PF_L0;
                mvf.pred_flag = PF_BI;
                mvf.ref_idx[lx] = mv1->ref_idx[lx];
                mvf.mv[lx] = mv1->mv[lx];
                ff_vvc_set_mvf(lc, x0, y0, block_size, block_size, &mvf);
            }
        }
    }
}

void ff_vvc_store_mvf(const VVCLocalContext *lc, const MvField *mvf)
{
    const CodingUnit *cu = lc->cu;
    ff_vvc_set_mvf(lc, cu->x0, cu->y0, cu->cb_width, cu->cb_height, mvf);
}

void ff_vvc_store_mv(const VVCLocalContext *lc, const MotionInfo *mi)
{
    const CodingUnit *cu = lc->cu;
    MvField mvf = {0};

    mvf.hpel_if_idx = mi->hpel_if_idx;
    mvf.bcw_idx = mi->bcw_idx;
    mvf.pred_flag = mi->pred_flag;

    for (int i = 0; i < 2; i++) {
        const PredFlag mask = i + 1;
        if (mvf.pred_flag & mask) {
            mvf.mv[i] = mi->mv[i][0];
            mvf.ref_idx[i] = mi->ref_idx[i];
        }
    }
    ff_vvc_set_mvf(lc, cu->x0, cu->y0, cu->cb_width, cu->cb_height, &mvf);
}

typedef enum NeighbourIdx {
    A0,
    A1,
    A2,
    B0,
    B1,
    B2,
    B3,
    NUM_NBS,
    NB_IDX_NONE = NUM_NBS,
} NeighbourIdx;

typedef struct Neighbour {
    int x;
    int y;

    int checked;
    int available;
} Neighbour;

typedef struct NeighbourContext {
    Neighbour neighbours[NUM_NBS];
    const VVCLocalContext *lc;
} NeighbourContext;

static int is_available(const VVCFrameContext *fc, const int x0, const int y0)
{
    const VVCSPS *sps      = fc->ps.sps;
    const int x            = x0 >> sps->min_cb_log2_size_y;
    const int y            = y0 >> sps->min_cb_log2_size_y;
    const int min_cb_width = fc->ps.pps->min_cb_width;

    return SAMPLE_CTB(fc->tab.cb_width[0], x, y) != 0;
}

static int is_a0_available(const VVCLocalContext *lc, const CodingUnit *cu)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCSPS *sps           = fc->ps.sps;
    const int x0b               = av_zero_extend(cu->x0, sps->ctb_log2_size_y);
    int cand_bottom_left;

    if (!x0b && !lc->ctb_left_flag) {
        cand_bottom_left = 0;
    } else {
        const int max_y = FFMIN(fc->ps.pps->height, ((cu->y0 >> sps->ctb_log2_size_y) + 1) << sps->ctb_log2_size_y);
        if (cu->y0 + cu->cb_height >= max_y)
            cand_bottom_left = 0;
        else
            cand_bottom_left = is_available(fc, cu->x0 - 1, cu->y0 + cu->cb_height);
    }
    return cand_bottom_left;
}

static void init_neighbour_context(NeighbourContext *ctx, const VVCLocalContext *lc)
{
    const CodingUnit *cu            = lc->cu;
    const NeighbourAvailable *na    = &lc->na;
    const int x0                    = cu->x0;
    const int y0                    = cu->y0;
    const int cb_width              = cu->cb_width;
    const int cb_height             = cu->cb_height;
    const int a0_available          = is_a0_available(lc, cu);

    Neighbour neighbours[NUM_NBS] = {
        { x0 - 1,               y0 + cb_height,         !a0_available           }, //A0
        { x0 - 1,               y0 + cb_height - 1,     !na->cand_left          }, //A1
        { x0 - 1,               y0,                     !na->cand_left          }, //A2
        { x0 + cb_width,        y0 - 1,                 !na->cand_up_right      }, //B0
        { x0 + cb_width - 1,    y0 - 1,                 !na->cand_up            }, //B1
        { x0 - 1,               y0 - 1,                 !na->cand_up_left       }, //B2
        { x0,                   y0 - 1,                 !na->cand_up            }, //B3
    };

    memcpy(ctx->neighbours, neighbours, sizeof(neighbours));
    ctx->lc = lc;
}

static av_always_inline PredMode pred_flag_to_mode(PredFlag pred)
{
    static const PredMode lut[] = {
        MODE_INTRA, // PF_INTRA
        MODE_INTER, // PF_L0
        MODE_INTER, // PF_L1
        MODE_INTER, // PF_BI
        0,          // invalid
        MODE_IBC,   // PF_IBC
        0,          // invalid
        0,          // invalid
        MODE_PLT,   // PF_PLT
    };

    return lut[pred];
}

static int check_available(Neighbour *n, const VVCLocalContext *lc, const int check_mer)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCSPS *sps           = fc->ps.sps;
    const CodingUnit *cu        = lc->cu;
    const MvField *tab_mvf      = fc->tab.mvf;
    const int min_pu_width      = fc->ps.pps->min_pu_width;

    if (!n->checked) {
        n->checked = 1;
        n->available = !sps->r->sps_entropy_coding_sync_enabled_flag || ((n->x >> sps->ctb_log2_size_y) <= (cu->x0 >> sps->ctb_log2_size_y));
        n->available = n->available && is_available(fc, n->x, n->y) && cu->pred_mode == pred_flag_to_mode(TAB_MVF(n->x, n->y).pred_flag);
        if (check_mer)
            n->available = n->available && !is_same_mer(fc, n->x, n->y, cu->x0, cu->y0);
    }
    return n->available;
}

static const MvField *mv_merge_candidate(const VVCLocalContext *lc, const int x_cand, const int y_cand)
{
    const VVCFrameContext *fc   = lc->fc;
    const int min_pu_width      = fc->ps.pps->min_pu_width;
    const MvField* tab_mvf      = fc->tab.mvf;
    const MvField *mvf          = &TAB_MVF(x_cand, y_cand);

    return mvf;
}

static const MvField* mv_merge_from_nb(NeighbourContext *ctx, const NeighbourIdx nb)
{
    const VVCLocalContext *lc   = ctx->lc;
    Neighbour *n                = &ctx->neighbours[nb];

    if (check_available(n, lc, 1))
        return mv_merge_candidate(lc, n->x, n->y);
    return 0;
}
#define MV_MERGE_FROM_NB(nb) mv_merge_from_nb(&nctx, nb)

//8.5.2.3 Derivation process for spatial merging candidates
static int mv_merge_spatial_candidates(const VVCLocalContext *lc, const int merge_idx,
    const MvField **nb_list, MvField *cand_list, int *nb_merge_cand)
{
    const MvField *cand;
    int num_cands = 0;
    NeighbourContext nctx;

    static NeighbourIdx nbs[][2] = {
        {B1, NB_IDX_NONE },
        {A1, B1 },
        {B0, B1 },
        {A0, A1 },
    };

    init_neighbour_context(&nctx, lc);
    for (int i = 0; i < FF_ARRAY_ELEMS(nbs); i++) {
        NeighbourIdx nb    = nbs[i][0];
        NeighbourIdx old   = nbs[i][1];
        cand = nb_list[nb] = MV_MERGE_FROM_NB(nb);
        if (cand && !compare_mv_ref_idx(cand, nb_list[old])) {
            cand_list[num_cands] = *cand;
            if (merge_idx == num_cands)
                return 1;
            num_cands++;
        }
    }
    if (num_cands != 4) {
        cand = MV_MERGE_FROM_NB(B2);
        if (cand && !compare_mv_ref_idx(cand, nb_list[A1])
            && !compare_mv_ref_idx(cand, nb_list[B1])) {
            cand_list[num_cands] = *cand;
            if (merge_idx == num_cands)
                return 1;
            num_cands++;
        }
    }
    *nb_merge_cand = num_cands;
    return 0;
}

static int mv_merge_temporal_candidate(const VVCLocalContext *lc, MvField *cand)
{
    const VVCFrameContext *fc   = lc->fc;
    const CodingUnit *cu        = lc->cu;

    memset(cand, 0, sizeof(*cand));
    if (fc->ps.ph.r->ph_temporal_mvp_enabled_flag && (cu->cb_width * cu->cb_height > 32)) {
        int available_l0 = temporal_luma_motion_vector(lc, 0, cand->mv + 0, 0, 1, 0);
        int available_l1 = IS_B(lc->sc->sh.r) ?
            temporal_luma_motion_vector(lc, 0, cand->mv + 1, 1, 1, 0) : 0;
        cand->pred_flag = available_l0 + (available_l1 << 1);
    }
    return cand->pred_flag;
}

//8.5.2.6 Derivation process for history-based merging candidates
static int mv_merge_history_candidates(const VVCLocalContext *lc, const int merge_idx,
    const MvField **nb_list, MvField *cand_list, int *num_cands)
{
    const VVCSPS *sps       = lc->fc->ps.sps;
    const EntryPoint* ep    = lc->ep;
    for (int i = 1; i <= ep->num_hmvp && (*num_cands < sps->max_num_merge_cand - 1); i++) {
        const MvField *h = &ep->hmvp[ep->num_hmvp - i];
        const int same_motion = i <= 2 && (compare_mv_ref_idx(h, nb_list[A1]) || compare_mv_ref_idx(h, nb_list[B1]));
        if (!same_motion) {
            cand_list[*num_cands] = *h;
            if (merge_idx == *num_cands)
                return 1;
            (*num_cands)++;
        }
    }
    return 0;
}

//8.5.2.4 Derivation process for pairwise average merging candidate
static int mv_merge_pairwise_candidate(MvField *cand_list, const int num_cands, const int is_b)
{
    if (num_cands > 1) {
        const int num_ref_rists = is_b ? 2 : 1;
        const MvField* p0       = cand_list + 0;
        const MvField* p1       = cand_list + 1;
        MvField* cand           = cand_list + num_cands;

        cand->pred_flag = 0;
        for (int i = 0; i < num_ref_rists; i++) {
            PredFlag mask = i + 1;
            if (p0->pred_flag & mask) {
                cand->pred_flag |= mask;
                cand->ref_idx[i] = p0->ref_idx[i];
                if (p1->pred_flag & mask) {
                    Mv *mv = cand->mv + i;
                    mv->x = p0->mv[i].x + p1->mv[i].x;
                    mv->y = p0->mv[i].y + p1->mv[i].y;
                    ff_vvc_round_mv(mv, 0, 1);
                } else {
                    cand->mv[i] = p0->mv[i];
                }
            } else if (p1->pred_flag & mask) {
                cand->pred_flag |= mask;
                cand->mv[i] = p1->mv[i];
                cand->ref_idx[i] = p1->ref_idx[i];
            }
        }
        if (cand->pred_flag) {
            cand->hpel_if_idx = p0->hpel_if_idx == p1->hpel_if_idx ? p0->hpel_if_idx : 0;
            cand->bcw_idx = 0;
            cand->ciip_flag = 0;
            return 1;
        }
    }
    return 0;
}

//8.5.2.5 Derivation process for zero motion vector merging candidates
static void mv_merge_zero_motion_candidate(const VVCLocalContext *lc, const int merge_idx,
    MvField *cand_list, int num_cands)
{
    const VVCSPS *sps             = lc->fc->ps.sps;
    const H266RawSliceHeader *rsh = lc->sc->sh.r;
    const int num_ref_idx         = IS_P(rsh) ?
        rsh->num_ref_idx_active[L0] : FFMIN(rsh->num_ref_idx_active[L0], rsh->num_ref_idx_active[L1]);
    int zero_idx                  = 0;

    while (num_cands < sps->max_num_merge_cand) {
        MvField *cand = cand_list + num_cands;

        cand->pred_flag    = PF_L0 + (IS_B(rsh) << 1);
        AV_ZERO64(cand->mv + 0);
        AV_ZERO64(cand->mv + 1);
        cand->ref_idx[0]   = zero_idx < num_ref_idx ? zero_idx : 0;
        cand->ref_idx[1]   = zero_idx < num_ref_idx ? zero_idx : 0;
        cand->bcw_idx      = 0;
        cand->hpel_if_idx  = 0;
        if (merge_idx == num_cands)
            return;
        num_cands++;
        zero_idx++;
    }
}

static void mv_merge_mode(const VVCLocalContext *lc,  const int merge_idx, MvField *cand_list)
{
    int num_cands    = 0;
    const MvField *nb_list[NUM_NBS + 1] = { NULL };

    if (mv_merge_spatial_candidates(lc, merge_idx, nb_list, cand_list, &num_cands))
        return;

    if (mv_merge_temporal_candidate(lc, &cand_list[num_cands])) {
        if (merge_idx == num_cands)
            return;
        num_cands++;
    }

    if (mv_merge_history_candidates(lc, merge_idx, nb_list, cand_list, &num_cands))
        return;

    if (mv_merge_pairwise_candidate(cand_list, num_cands, IS_B(lc->sc->sh.r))) {
        if (merge_idx == num_cands)
            return;
        num_cands++;
    }

    mv_merge_zero_motion_candidate(lc, merge_idx, cand_list, num_cands);
}

//8.5.2.2 Derivation process for luma motion vectors for merge mode
void ff_vvc_luma_mv_merge_mode(VVCLocalContext *lc, const int merge_idx, const int ciip_flag, MvField *mv)
{
    const CodingUnit *cu = lc->cu;
    MvField cand_list[MRG_MAX_NUM_CANDS];

    ff_vvc_set_neighbour_available(lc, cu->x0, cu->y0, cu->cb_width, cu->cb_height);
    mv_merge_mode(lc, merge_idx, cand_list);
    *mv = cand_list[merge_idx];
    //ciip flag in not inhritable
    mv->ciip_flag = ciip_flag;
}

//8.5.4.2 Derivation process for luma motion vectors for geometric partitioning merge mode
void ff_vvc_luma_mv_merge_gpm(VVCLocalContext *lc, const int merge_gpm_idx[2], MvField *mv)
{
    const CodingUnit *cu = lc->cu;
    MvField cand_list[MRG_MAX_NUM_CANDS];

    const int idx[] = { merge_gpm_idx[0], merge_gpm_idx[1] + (merge_gpm_idx[1] >= merge_gpm_idx[0]) };

    ff_vvc_set_neighbour_available(lc, cu->x0, cu->y0, cu->cb_width, cu->cb_height);
    mv_merge_mode(lc, FFMAX(idx[0], idx[1]), cand_list);
    memset(mv, 0, 2 * sizeof(*mv));
    for (int i = 0; i < 2; i++) {
        int lx   = idx[i] & 1;
        int mask = lx + PF_L0;
        MvField *cand = cand_list + idx[i];
        if (!(cand->pred_flag & mask)) {
            lx   = !lx;
            mask = lx + PF_L0;
        }
        mv[i].pred_flag   = mask;
        mv[i].ref_idx[lx] = cand->ref_idx[lx];
        mv[i].mv[lx]      = cand->mv[lx];
    }

}

//8.5.5.5 Derivation process for luma affine control point motion vectors from a neighbouring block
static void affine_cps_from_nb(const VVCLocalContext *lc,
    const int x_nb, int y_nb, const int nbw, const int nbh, const int lx,
    Mv *cps, int num_cps)
{
    const VVCFrameContext *fc   = lc->fc;
    const CodingUnit *cu        = lc->cu;
    const int x0                = cu->x0;
    const int y0                = cu->y0;
    const int cb_width          = cu->cb_width;
    const int cb_height         = cu->cb_height;
    const MvField* tab_mvf      = fc->tab.mvf;
    const int min_cb_log2_size  = fc->ps.sps->min_cb_log2_size_y;
    const int min_cb_width      = fc->ps.pps->min_cb_width;

    const int log2_nbw          = ff_log2(nbw);
    const int log2_nbh          = ff_log2(nbh);
    const int is_ctb_boundary   = !((y_nb + nbh) % fc->ps.sps->ctb_size_y) && (y_nb + nbh == y0);
    const Mv *l, *r;
    int mv_scale_hor, mv_scale_ver, d_hor_x, d_ver_x, d_hor_y, d_ver_y, motion_model_idc_nb;
    if (is_ctb_boundary) {
        const int min_pu_width = fc->ps.pps->min_pu_width;
        l = &TAB_MVF(x_nb, y_nb + nbh - 1).mv[lx];
        r = &TAB_MVF(x_nb + nbw - 1, y_nb + nbh - 1).mv[lx];
    } else {
        const int x = x_nb >> min_cb_log2_size;
        const int y = y_nb >> min_cb_log2_size;
        motion_model_idc_nb  = SAMPLE_CTB(fc->tab.mmi, x, y);

        l = &TAB_CP_MV(lx, x_nb, y_nb);
        r = &TAB_CP_MV(lx, x_nb + nbw - 1, y_nb) + 1;
    }
    mv_scale_hor = l->x * (1 << 7);
    mv_scale_ver = l->y * (1 << 7);
    d_hor_x = (r->x - l->x) * (1 << (7 - log2_nbw));
    d_ver_x = (r->y - l->y) * (1 << (7 - log2_nbw));
    if (!is_ctb_boundary && motion_model_idc_nb == MOTION_6_PARAMS_AFFINE) {
        const Mv* lb = &TAB_CP_MV(lx, x_nb, y_nb + nbh - 1) + 2;
        d_hor_y = (lb->x - l->x) * (1 << (7 - log2_nbh));
        d_ver_y = (lb->y - l->y) * (1 << (7 - log2_nbh));
    } else {
        d_hor_y = -d_ver_x;
        d_ver_y = d_hor_x;
    }

    if (is_ctb_boundary) {
        y_nb = y0;
    }
    cps[0].x = mv_scale_hor + d_hor_x * (x0 - x_nb)  + d_hor_y * (y0 - y_nb);
    cps[0].y = mv_scale_ver + d_ver_x * (x0 - x_nb)  + d_ver_y * (y0 - y_nb);
    cps[1].x = mv_scale_hor + d_hor_x * (x0 + cb_width - x_nb)  + d_hor_y * (y0 - y_nb);
    cps[1].y = mv_scale_ver + d_ver_x * (x0 + cb_width - x_nb)  + d_ver_y * (y0 - y_nb);
    if (num_cps == 3) {
        cps[2].x = mv_scale_hor + d_hor_x * (x0 - x_nb)  + d_hor_y * (y0 + cb_height - y_nb);
        cps[2].y = mv_scale_ver + d_ver_x * (x0 - x_nb)  + d_ver_y * (y0 + cb_height - y_nb);
    }
    for (int i = 0; i < num_cps; i++) {
        ff_vvc_round_mv(cps + i, 0, 7);
        ff_vvc_clip_mv(cps + i);
    }
}

//derive affine neighbour's postion, width and height,
static int affine_neighbour_cb(const VVCFrameContext *fc, const int x_nb, const int y_nb, int *x_cb, int *y_cb, int *cbw, int *cbh)
{
    const int log2_min_cb_size  = fc->ps.sps->min_cb_log2_size_y;
    const int min_cb_width      = fc->ps.pps->min_cb_width;
    const int x                 = x_nb >> log2_min_cb_size;
    const int y                 = y_nb >> log2_min_cb_size;
    const int motion_model_idc  = SAMPLE_CTB(fc->tab.mmi, x, y);
    if (motion_model_idc) {
        *x_cb = SAMPLE_CTB(fc->tab.cb_pos_x[0],  x, y);
        *y_cb = SAMPLE_CTB(fc->tab.cb_pos_y[0],  x, y);
        *cbw  = SAMPLE_CTB(fc->tab.cb_width[0],  x, y);
        *cbh  = SAMPLE_CTB(fc->tab.cb_height[0], x, y);
    }
    return motion_model_idc;
}

//part of 8.5.5.2 Derivation process for motion vectors and reference indices in subblock merge mode
static int affine_merge_candidate(const VVCLocalContext *lc, const int x_cand, const int y_cand, MotionInfo* mi)
{
    const VVCFrameContext *fc = lc->fc;
    int x, y, w, h, motion_model_idc;

    motion_model_idc = affine_neighbour_cb(fc, x_cand, y_cand, &x, &y, &w, &h);
    if (motion_model_idc) {
        const int min_pu_width = fc->ps.pps->min_pu_width;
        const MvField* tab_mvf = fc->tab.mvf;
        const MvField *mvf = &TAB_MVF(x, y);

        mi->bcw_idx   = mvf->bcw_idx;
        mi->pred_flag = mvf->pred_flag;
        for (int i = 0; i < 2; i++) {
            PredFlag mask = i + 1;
            if (mi->pred_flag & mask) {
                affine_cps_from_nb(lc, x, y, w, h, i, &mi->mv[i][0], motion_model_idc + 1);
            }
            mi->ref_idx[i] = mvf->ref_idx[i];
        }
        mi->motion_model_idc = motion_model_idc;
    }
    return motion_model_idc;
}

static int affine_merge_from_nbs(NeighbourContext *ctx, const NeighbourIdx *nbs, const int num_nbs, MotionInfo* cand)
{
    const VVCLocalContext *lc = ctx->lc;
    for (int i = 0; i < num_nbs; i++) {
        Neighbour *n = &ctx->neighbours[nbs[i]];
        if (check_available(n, lc, 1) && affine_merge_candidate(lc, n->x, n->y, cand))
            return 1;
    }
    return 0;
}
#define AFFINE_MERGE_FROM_NBS(nbs) affine_merge_from_nbs(&nctx, nbs, FF_ARRAY_ELEMS(nbs), mi)


static const MvField* derive_corner_mvf(NeighbourContext *ctx, const NeighbourIdx *neighbour, const int num_neighbour)
{
    const VVCFrameContext *fc   = ctx->lc->fc;
    const MvField *tab_mvf      = fc->tab.mvf;
    const int min_pu_width      = fc->ps.pps->min_pu_width;
    for (int i = 0; i < num_neighbour; i++) {
        Neighbour *n = &ctx->neighbours[neighbour[i]];
        if (check_available(n, ctx->lc, 1)) {
            return &TAB_MVF(n->x, n->y);
        }
    }
    return NULL;
}

#define DERIVE_CORNER_MV(nbs) derive_corner_mvf(nctx, nbs, FF_ARRAY_ELEMS(nbs))

// check if the mv's and refidx are the same between A and B
static av_always_inline int compare_pf_ref_idx(const MvField *A, const struct MvField *B, const struct MvField *C, const int lx)
{

    const PredFlag mask = (lx + 1) & A->pred_flag;
    if (!(B->pred_flag & mask))
        return 0;
    if (A->ref_idx[lx] != B->ref_idx[lx])
        return 0;
    if (C) {
        if (!(C->pred_flag & mask))
            return 0;
        if (A->ref_idx[lx] != C->ref_idx[lx])
            return 0;
    }
    return 1;
}

static av_always_inline void sb_clip_location(const VVCLocalContext *lc,
    const int x_ctb, const int y_ctb, const Mv* temp_mv, int *x, int *y)
{
    const VVCFrameContext *fc = lc->fc;
    const VVCPPS *pps         = fc->ps.pps;
    const int ctb_log2_size   = fc->ps.sps->ctb_log2_size_y;
    const int subpic_idx      = lc->sc->sh.r->curr_subpic_idx;
    const int x_end           = pps->subpic_x[subpic_idx] + pps->subpic_width[subpic_idx];
    const int y_end           = pps->subpic_y[subpic_idx] + pps->subpic_height[subpic_idx];

    *x = av_clip(*x + temp_mv->x, x_ctb, FFMIN(x_end - 1, x_ctb + (1 << ctb_log2_size) + 3)) & ~7;
    *y = av_clip(*y + temp_mv->y, y_ctb, FFMIN(y_end - 1, y_ctb + (1 << ctb_log2_size) - 1)) & ~7;
}

static void sb_temproal_luma_motion(const VVCLocalContext *lc,
    const int x_ctb, const int y_ctb, const Mv *temp_mv,
    int x, int y, uint8_t *pred_flag, Mv *mv)
{
    MvField temp_col;
    Mv* mvLXCol;
    const int refIdxLx          = 0;
    const VVCFrameContext *fc   = lc->fc;
    const VVCSH *sh             = &lc->sc->sh;
    const int min_pu_width      = fc->ps.pps->min_pu_width;
    VVCFrame *ref               = fc->ref->collocated_ref;
    MvField *tab_mvf            = ref->tab_dmvr_mvf;
    int colPic                  = ref->poc;
    int X                       = 0;

    sb_clip_location(lc, x_ctb, y_ctb, temp_mv, &x, &y);

    temp_col    = TAB_MVF(x, y);
    mvLXCol     = mv + 0;
    *pred_flag = DERIVE_TEMPORAL_COLOCATED_MVS(1);
    if (IS_B(sh->r)) {
        X = 1;
        mvLXCol = mv + 1;
        *pred_flag |= (DERIVE_TEMPORAL_COLOCATED_MVS(1)) << 1;
    }
}

//8.5.5.4 Derivation process for subblock-based temporal merging base motion data
static int sb_temporal_luma_motion_data(const VVCLocalContext *lc, const MvField *a1,
    const int x_ctb, const int y_ctb, MvField *ctr_mvf, Mv *temp_mv)
{
    const VVCFrameContext *fc   = lc->fc;
    const RefPicList *rpl       = lc->sc->rpl;
    const CodingUnit *cu        = lc->cu;
    const int x                 = cu->x0  + cu->cb_width / 2;
    const int y                 = cu->y0  + cu->cb_height / 2;
    const VVCFrame *ref         = fc->ref->collocated_ref;

    int colPic;

    memset(temp_mv, 0, sizeof(*temp_mv));

    if (!ref) {
        memset(ctr_mvf, 0, sizeof(*ctr_mvf));
        return 0;
    }

    colPic  = ref->poc;

    if (a1) {
        if ((a1->pred_flag & PF_L0) && colPic == rpl[L0].refs[a1->ref_idx[L0]].poc)
            *temp_mv = a1->mv[0];
        else if ((a1->pred_flag & PF_L1) && colPic == rpl[L1].refs[a1->ref_idx[L1]].poc)
            *temp_mv = a1->mv[1];
        ff_vvc_round_mv(temp_mv, 0, 4);
    }
    sb_temproal_luma_motion(lc, x_ctb, y_ctb, temp_mv, x, y, &ctr_mvf->pred_flag , ctr_mvf->mv);

    return ctr_mvf->pred_flag;
}


//8.5.5.3 Derivation process for subblock-based temporal merging candidates
static int sb_temporal_merge_candidate(const VVCLocalContext* lc, NeighbourContext *nctx, PredictionUnit *pu)
{
    const VVCFrameContext *fc   = lc->fc;
    const CodingUnit *cu        = lc->cu;
    const VVCSPS *sps           = fc->ps.sps;
    const VVCPH *ph             = &fc->ps.ph;
    MotionInfo *mi              = &pu->mi;
    const int ctb_log2_size     = sps->ctb_log2_size_y;
    const int x0                = cu->x0;
    const int y0                = cu->y0;
    const NeighbourIdx n        = A1;
    const MvField *a1;
    MvField ctr_mvf;
    LOCAL_ALIGNED_8(Mv, temp_mv, [1]);
    const int x_ctb = (x0 >> ctb_log2_size) << ctb_log2_size;
    const int y_ctb = (y0 >> ctb_log2_size) << ctb_log2_size;


    if (!ph->r->ph_temporal_mvp_enabled_flag ||
        !sps->r->sps_sbtmvp_enabled_flag ||
        (cu->cb_width < 8 && cu->cb_height < 8))
        return 0;

    mi->num_sb_x = cu->cb_width >> 3;
    mi->num_sb_y = cu->cb_height >> 3;

    a1 = derive_corner_mvf(nctx, &n, 1);
    if (sb_temporal_luma_motion_data(lc, a1, x_ctb, y_ctb, &ctr_mvf, temp_mv)) {
        const int sbw = cu->cb_width / mi->num_sb_x;
        const int sbh = cu->cb_height / mi->num_sb_y;
        MvField mvf = {0};
        for (int sby = 0; sby < mi->num_sb_y; sby++) {
            for (int sbx = 0; sbx < mi->num_sb_x; sbx++) {
                int x = x0 + sbx * sbw;
                int y = y0 + sby * sbh;
                sb_temproal_luma_motion(lc, x_ctb, y_ctb, temp_mv, x + sbw / 2, y +  sbh / 2, &mvf.pred_flag, mvf.mv);
                if (!mvf.pred_flag) {
                    mvf.pred_flag = ctr_mvf.pred_flag;
                    memcpy(mvf.mv, ctr_mvf.mv, sizeof(mvf.mv));
                }
                ff_vvc_set_mvf(lc, x, y, sbw, sbh, &mvf);
            }
        }
        return 1;
    }
    return 0;
}

static int affine_merge_const1(const MvField *c0, const MvField *c1, const MvField *c2, MotionInfo *mi)
{
    if (c0 && c1 && c2) {
        mi->pred_flag = 0;
        for (int i = 0; i < 2; i++) {
            PredFlag mask = i + 1;
            if (compare_pf_ref_idx(c0, c1, c2, i)) {
                mi->pred_flag |= mask;
                mi->ref_idx[i] = c0->ref_idx[i];
                mi->mv[i][0] = c0->mv[i];
                mi->mv[i][1] = c1->mv[i];
                mi->mv[i][2] = c2->mv[i];
            }
        }
        if (mi->pred_flag) {
            if (mi->pred_flag == PF_BI)
                mi->bcw_idx = c0->bcw_idx;
            mi->motion_model_idc = MOTION_6_PARAMS_AFFINE;
            return 1;
        }
    }
    return 0;
}

static int affine_merge_const2(const MvField *c0, const MvField *c1, const MvField *c3, MotionInfo *mi)
{
    if (c0 && c1 && c3) {
        mi->pred_flag = 0;
        for (int i = 0; i < 2; i++) {
            PredFlag mask = i + 1;
            if (compare_pf_ref_idx(c0, c1, c3, i)) {
                mi->pred_flag |= mask;
                mi->ref_idx[i] = c0->ref_idx[i];
                mi->mv[i][0] = c0->mv[i];
                mi->mv[i][1] = c1->mv[i];
                mi->mv[i][2].x = c3->mv[i].x + c0->mv[i].x - c1->mv[i].x;
                mi->mv[i][2].y = c3->mv[i].y + c0->mv[i].y - c1->mv[i].y;
                ff_vvc_clip_mv(&mi->mv[i][2]);
            }
        }
        if (mi->pred_flag) {
            mi->bcw_idx = mi->pred_flag == PF_BI ? c0->bcw_idx : 0;
            mi->motion_model_idc = MOTION_6_PARAMS_AFFINE;
            return 1;
        }
    }
    return 0;
}

static int affine_merge_const3(const MvField *c0, const MvField *c2, const MvField *c3, MotionInfo *mi)
{
    if (c0 && c2 && c3) {
        mi->pred_flag = 0;
        for (int i = 0; i < 2; i++) {
            PredFlag mask = i + 1;
            if (compare_pf_ref_idx(c0, c2, c3, i)) {
                mi->pred_flag |= mask;
                mi->ref_idx[i] = c0->ref_idx[i];
                mi->mv[i][0] = c0->mv[i];
                mi->mv[i][1].x = c3->mv[i].x + c0->mv[i].x - c2->mv[i].x;
                mi->mv[i][1].y = c3->mv[i].y + c0->mv[i].y - c2->mv[i].y;
                ff_vvc_clip_mv(&mi->mv[i][1]);
                mi->mv[i][2] = c2->mv[i];
            }
        }
        if (mi->pred_flag) {
            mi->bcw_idx = mi->pred_flag == PF_BI ? c0->bcw_idx : 0;
            mi->motion_model_idc = MOTION_6_PARAMS_AFFINE;
            return 1;
        }
    }
    return 0;
}

static int affine_merge_const4(const MvField *c1, const MvField *c2, const MvField *c3, MotionInfo *mi)
{
    if (c1 && c2 && c3) {
        mi->pred_flag = 0;
        for (int i = 0; i < 2; i++) {
            PredFlag mask = i + 1;
            if (compare_pf_ref_idx(c1, c2, c3, i)) {
                mi->pred_flag |= mask;
                mi->ref_idx[i] = c1->ref_idx[i];
                mi->mv[i][0].x = c1->mv[i].x + c2->mv[i].x - c3->mv[i].x;
                mi->mv[i][0].y = c1->mv[i].y + c2->mv[i].y - c3->mv[i].y;
                ff_vvc_clip_mv(&mi->mv[i][0]);
                mi->mv[i][1] = c1->mv[i];
                mi->mv[i][2] = c2->mv[i];
            }
        }
        if (mi->pred_flag) {
            mi->bcw_idx = mi->pred_flag == PF_BI ? c1->bcw_idx : 0;
            mi->motion_model_idc = MOTION_6_PARAMS_AFFINE;
            return 1;
        }
    }
    return 0;
}

static int affine_merge_const5(const MvField *c0, const MvField *c1, MotionInfo *mi)
{
    if (c0 && c1) {
        mi->pred_flag = 0;
        for (int i = 0; i < 2; i++) {
            PredFlag mask = i + 1;
            if (compare_pf_ref_idx(c0, c1, NULL, i)) {
                mi->pred_flag |= mask;
                mi->ref_idx[i] = c0->ref_idx[i];
                mi->mv[i][0] = c0->mv[i];
                mi->mv[i][1] = c1->mv[i];
            }
        }
        if (mi->pred_flag) {
            if (mi->pred_flag == PF_BI)
                mi->bcw_idx = c0->bcw_idx;
            mi->motion_model_idc = MOTION_4_PARAMS_AFFINE;
            return 1;
        }
    }
    return 0;
}

static int affine_merge_const6(const MvField* c0, const MvField* c2, const int cb_width, const int cb_height, MotionInfo *mi)
{
    if (c0 && c2) {
        const int shift = 7 + av_log2(cb_width) - av_log2(cb_height);
        mi->pred_flag = 0;
        for (int i = 0; i < 2; i++) {
            PredFlag mask = i + 1;
            if (compare_pf_ref_idx(c0, c2, NULL, i)) {
                mi->pred_flag |= mask;
                mi->ref_idx[i] = c0->ref_idx[i];
                mi->mv[i][0] = c0->mv[i];
                mi->mv[i][1].x = (c0->mv[i].x * (1 << 7)) + ((c2->mv[i].y - c0->mv[i].y) * (1 << shift));
                mi->mv[i][1].y = (c0->mv[i].y * (1 << 7)) - ((c2->mv[i].x - c0->mv[i].x) * (1 << shift));
                ff_vvc_round_mv(&mi->mv[i][1], 0, 7);
                ff_vvc_clip_mv(&mi->mv[i][1]);
            }
        }
        if (mi->pred_flag) {
            if (mi->pred_flag == PF_BI)
                mi->bcw_idx = c0->bcw_idx;
            mi->motion_model_idc = MOTION_4_PARAMS_AFFINE;
            return 1;
        }
    }
    return 0;
}

static void affine_merge_zero_motion(const VVCLocalContext *lc, MotionInfo *mi)
{
    const CodingUnit *cu = lc->cu;

    memset(mi, 0, sizeof(*mi));
    mi->pred_flag    = PF_L0 + (IS_B(lc->sc->sh.r) << 1);
    mi->motion_model_idc = MOTION_4_PARAMS_AFFINE;
    mi->num_sb_x = cu->cb_width >> MIN_PU_LOG2;
    mi->num_sb_y = cu->cb_height >> MIN_PU_LOG2;
}

//8.5.5.6 Derivation process for constructed affine control point motion vector merging candidates
static int affine_merge_const_candidates(const VVCLocalContext *lc, MotionInfo *mi,
    NeighbourContext *nctx, const int merge_subblock_idx, int num_cands)
{
    const VVCFrameContext *fc   = lc->fc;
    const CodingUnit *cu        = lc->cu;
    const NeighbourIdx tl[]     = { B2, B3, A2 };
    const NeighbourIdx tr[]     = { B1, B0};
    const NeighbourIdx bl[]     = { A1, A0};
    const MvField *c0, *c1, *c2;

    c0 = DERIVE_CORNER_MV(tl);
    c1 = DERIVE_CORNER_MV(tr);
    c2 = DERIVE_CORNER_MV(bl);

    if (fc->ps.sps->r->sps_6param_affine_enabled_flag) {
        MvField corner3, *c3 = NULL;
        //Const1
        if (affine_merge_const1(c0, c1, c2, mi)) {
            if (merge_subblock_idx == num_cands)
                return 1;
            num_cands++;
        }

        memset(&corner3, 0, sizeof(corner3));
        if (fc->ps.ph.r->ph_temporal_mvp_enabled_flag){
            const int available_l0 = temporal_luma_motion_vector(lc, 0, corner3.mv + 0, 0, 0, 0);
            const int available_l1 = (lc->sc->sh.r->sh_slice_type == VVC_SLICE_TYPE_B) ?
                temporal_luma_motion_vector(lc, 0, corner3.mv + 1, 1, 0, 0) : 0;

            corner3.pred_flag = available_l0 + (available_l1 << 1);
            if (corner3.pred_flag)
                c3 = &corner3;
        }

        //Const2
        if (affine_merge_const2(c0, c1, c3, mi)) {
            if (merge_subblock_idx == num_cands)
                return 1;
            num_cands++;
        }

        //Const3
        if (affine_merge_const3(c0, c2, c3, mi)) {
           if (merge_subblock_idx == num_cands)
               return 1;
           num_cands++;
        }

        //Const4
        if (affine_merge_const4(c1, c2, c3, mi)) {
           if (merge_subblock_idx == num_cands)
               return 1;
           num_cands++;
        }
    }

    //Const5
    if (affine_merge_const5(c0, c1, mi)) {
        if (merge_subblock_idx == num_cands)
            return 1;
        num_cands++;
    }

    if (affine_merge_const6(c0, c2, cu->cb_width, cu->cb_height, mi)) {
        if (merge_subblock_idx == num_cands)
            return 1;
    }
    return 0;
}

//8.5.5.2 Derivation process for motion vectors and reference indices in subblock merge mode
//return 1 if candidate is SbCol
static int sb_mv_merge_mode(const VVCLocalContext *lc, const int merge_subblock_idx, PredictionUnit *pu)
{
    const VVCSPS *sps       = lc->fc->ps.sps;
    const CodingUnit *cu    = lc->cu;
    MotionInfo *mi          = &pu->mi;
    int num_cands           = 0;
    NeighbourContext nctx;

    init_neighbour_context(&nctx, lc);

    //SbCol
    if (sb_temporal_merge_candidate(lc, &nctx, pu)) {
        if (merge_subblock_idx == num_cands)
            return 1;
        num_cands++;
    }

    pu->inter_affine_flag = 1;
    mi->num_sb_x  = cu->cb_width >> MIN_PU_LOG2;
    mi->num_sb_y  = cu->cb_height >> MIN_PU_LOG2;

    if (sps->r->sps_affine_enabled_flag) {
        const NeighbourIdx ak[] = { A0, A1 };
        const NeighbourIdx bk[] = { B0, B1, B2 };
        //A
        if (AFFINE_MERGE_FROM_NBS(ak)) {
            if (merge_subblock_idx == num_cands)
                return 0;
            num_cands++;
        }

        //B
        if (AFFINE_MERGE_FROM_NBS(bk)) {
            if (merge_subblock_idx == num_cands)
                return 0;
            num_cands++;
        }

        //Const1 to Const6
        if (affine_merge_const_candidates(lc, mi, &nctx, merge_subblock_idx, num_cands))
            return 0;
    }
    //Zero
    affine_merge_zero_motion(lc, mi);
    return 0;
}

void ff_vvc_sb_mv_merge_mode(VVCLocalContext *lc, const int merge_subblock_idx, PredictionUnit *pu)
{
    const CodingUnit *cu = lc->cu;
    ff_vvc_set_neighbour_available(lc, cu->x0, cu->y0, cu->cb_width, cu->cb_height);
    if (!sb_mv_merge_mode(lc, merge_subblock_idx, pu)) {
        ff_vvc_store_sb_mvs(lc, pu);
    }
}

static int mvp_candidate(const VVCLocalContext *lc, const int x_cand, const int y_cand,
    const int lx, const int8_t *ref_idx, Mv *mv)
{
    const VVCFrameContext *fc       = lc->fc;
    const RefPicList *rpl           = lc->sc->rpl;
    const int min_pu_width          = fc->ps.pps->min_pu_width;
    const MvField* tab_mvf          = fc->tab.mvf;
    const MvField *mvf              = &TAB_MVF(x_cand, y_cand);
    const PredFlag maskx = lx + 1;
    const int poc = rpl[lx].refs[ref_idx[lx]].poc;
    int available = 0;

    if ((mvf->pred_flag & maskx) && rpl[lx].refs[mvf->ref_idx[lx]].poc == poc) {
        available = 1;
        *mv = mvf->mv[lx];
    } else {
        const int ly = !lx;
        const PredFlag masky = ly + 1;
        if ((mvf->pred_flag & masky) && rpl[ly].refs[mvf->ref_idx[ly]].poc == poc) {
            available = 1;
            *mv = mvf->mv[ly];
        }
    }

    return available;
}

static int affine_mvp_candidate(const VVCLocalContext *lc,
    const int x_cand, const int y_cand, const int lx, const int8_t *ref_idx,
    Mv *cps, const int num_cp)
{
    const VVCFrameContext *fc = lc->fc;
    int x_nb, y_nb, nbw, nbh, motion_model_idc, available = 0;

    motion_model_idc = affine_neighbour_cb(fc, x_cand, y_cand, &x_nb, &y_nb, &nbw, &nbh);
    if (motion_model_idc) {
        const int min_pu_width = fc->ps.pps->min_pu_width;
        const MvField* tab_mvf = fc->tab.mvf;
        const MvField *mvf = &TAB_MVF(x_nb, y_nb);
        RefPicList* rpl = lc->sc->rpl;
        const PredFlag maskx = lx + 1;
        const int poc = rpl[lx].refs[ref_idx[lx]].poc;

        if ((mvf->pred_flag & maskx) && rpl[lx].refs[mvf->ref_idx[lx]].poc == poc) {
            available = 1;
            affine_cps_from_nb(lc, x_nb, y_nb, nbw, nbh, lx, cps, num_cp);
        } else {
            const int ly = !lx;
            const PredFlag masky = ly + 1;
            if ((mvf->pred_flag & masky) && rpl[ly].refs[mvf->ref_idx[ly]].poc == poc) {
                available = 1;
                affine_cps_from_nb(lc, x_nb, y_nb, nbw, nbh, ly, cps, num_cp);
            }
        }

    }
    return available;
}

static int mvp_from_nbs(NeighbourContext *ctx,
    const NeighbourIdx *nbs, const int num_nbs, const int lx, const int8_t *ref_idx, const int amvr_shift,
    Mv *cps, const int num_cps)
{
    const VVCLocalContext *lc   = ctx->lc;
    int available               = 0;

    for (int i = 0; i < num_nbs; i++) {
        Neighbour *n = &ctx->neighbours[nbs[i]];
        if (check_available(n, lc, 0)) {
            if (num_cps > 1)
                available = affine_mvp_candidate(lc, n->x, n->y, lx, ref_idx, cps, num_cps);
            else
                available = mvp_candidate(lc, n->x, n->y, lx, ref_idx, cps);
            if (available) {
                for (int c = 0; c < num_cps; c++)
                    ff_vvc_round_mv(cps + c, amvr_shift, amvr_shift);
                return 1;
            }
        }
    }
    return 0;
}

//get mvp from neighbours
#define AFFINE_MVP_FROM_NBS(nbs)                                                         \
    mvp_from_nbs(&nctx, nbs, FF_ARRAY_ELEMS(nbs), lx, ref_idx, amvr_shift, cps, num_cp)  \

#define MVP_FROM_NBS(nbs)                                                                \
    mvp_from_nbs(&nctx, nbs, FF_ARRAY_ELEMS(nbs), lx, ref_idx, amvr_shift, mv, 1)        \

static int mvp_spatial_candidates(const VVCLocalContext *lc,
    const int mvp_lx_flag, const int lx, const int8_t* ref_idx, const int amvr_shift,
    Mv* mv, int *nb_merge_cand)
{
    const NeighbourIdx ak[] = { A0, A1 };
    const NeighbourIdx bk[] = { B0, B1, B2 };
    NeighbourContext nctx;
    int available_a, num_cands = 0;
    LOCAL_ALIGNED_8(Mv, mv_a, [1]);

    init_neighbour_context(&nctx, lc);

    available_a = MVP_FROM_NBS(ak);
    if (available_a) {
        if (mvp_lx_flag == num_cands)
            return 1;
        num_cands++;
        *mv_a = *mv;
    }
    if (MVP_FROM_NBS(bk)) {
        if (!available_a || !IS_SAME_MV(mv_a, mv)) {
            if (mvp_lx_flag == num_cands)
                return 1;
            num_cands++;
        }
    }
    *nb_merge_cand = num_cands;
    return 0;
}

static int mvp_temporal_candidates(const VVCLocalContext* lc,
    const int mvp_lx_flag, const int lx, const int8_t *ref_idx, const int amvr_shift,
    Mv* mv, int *num_cands)
{
    if (temporal_luma_motion_vector(lc, ref_idx[lx], mv, lx, 1, 0)) {
        if (mvp_lx_flag == *num_cands) {
            ff_vvc_round_mv(mv, amvr_shift, amvr_shift);
            return 1;
        }
        (*num_cands)++;
    }
    return 0;

}

static int mvp_history_candidates(const VVCLocalContext *lc,
    const int mvp_lx_flag, const int lx, const int8_t ref_idx, const int amvr_shift,
    Mv *mv, int num_cands)
{
    const EntryPoint* ep            = lc->ep;
    const RefPicList* rpl           = lc->sc->rpl;
    const int poc                   = rpl[lx].refs[ref_idx].poc;

    if (ep->num_hmvp == 0)
        return 0;
    for (int i = 1; i <= FFMIN(4, ep->num_hmvp); i++) {
        const MvField* h = &ep->hmvp[i - 1];
        for (int j = 0; j < 2; j++) {
            const int ly = (j ? !lx : lx);
            PredFlag mask = PF_L0 + ly;
            if ((h->pred_flag & mask) && poc == rpl[ly].refs[h->ref_idx[ly]].poc) {
                if (mvp_lx_flag == num_cands) {
                    *mv = h->mv[ly];
                    ff_vvc_round_mv(mv, amvr_shift, amvr_shift);
                    return 1;
                }
                num_cands++;
            }
        }
    }
    return 0;
}

//8.5.2.8 Derivation process for luma motion vector prediction
static void mvp(const VVCLocalContext *lc, const int mvp_lx_flag, const int lx,
    const int8_t *ref_idx, const int amvr_shift, Mv *mv)
{
    int num_cands;

    if (mvp_spatial_candidates(lc, mvp_lx_flag, lx, ref_idx, amvr_shift, mv, &num_cands))
        return;

    if (mvp_temporal_candidates(lc, mvp_lx_flag, lx, ref_idx, amvr_shift, mv, &num_cands))
        return;

    if (mvp_history_candidates(lc, mvp_lx_flag, lx, ref_idx[lx], amvr_shift, mv, num_cands))
        return;

    memset(mv, 0, sizeof(*mv));
}

void ff_vvc_mvp(VVCLocalContext *lc, const int *mvp_lx_flag, const int amvr_shift,  MotionInfo *mi)
{
    const CodingUnit *cu    = lc->cu;
    mi->num_sb_x            = 1;
    mi->num_sb_y            = 1;

    ff_vvc_set_neighbour_available(lc, cu->x0, cu->y0, cu->cb_width, cu->cb_height);
    if (mi->pred_flag != PF_L1)
        mvp(lc, mvp_lx_flag[L0], L0, mi->ref_idx, amvr_shift, &mi->mv[L0][0]);
    if (mi->pred_flag != PF_L0)
        mvp(lc, mvp_lx_flag[L1], L1, mi->ref_idx, amvr_shift, &mi->mv[L1][0]);
}

static int ibc_spatial_candidates(const VVCLocalContext *lc, const int merge_idx, Mv *const cand_list, int *nb_merge_cand)
{
    const CodingUnit *cu      = lc->cu;
    const VVCFrameContext *fc = lc->fc;
    const int min_pu_width    = fc->ps.pps->min_pu_width;
    const MvField *tab_mvf    = fc->tab.mvf;
    const int is_gt4by4       = (cu->cb_width * cu->cb_height) > 16;
    int num_cands             = 0;

    NeighbourContext nctx;
    Neighbour *a1 = &nctx.neighbours[A1];
    Neighbour *b1 = &nctx.neighbours[B1];

    if (!is_gt4by4) {
        *nb_merge_cand = 0;
        return 0;
    }

    init_neighbour_context(&nctx, lc);

    if (check_available(a1, lc, 0)) {
        cand_list[num_cands++] = TAB_MVF(a1->x, a1->y).mv[L0];
        if (num_cands > merge_idx)
            return 1;
    }
    if (check_available(b1, lc, 0)) {
        const MvField *mvf = &TAB_MVF(b1->x, b1->y);
        if (!num_cands || !IS_SAME_MV(&cand_list[0], mvf->mv)) {
            cand_list[num_cands++] = mvf->mv[L0];
            if (num_cands > merge_idx)
                return 1;
        }
    }

    *nb_merge_cand = num_cands;
    return 0;
}

static int ibc_history_candidates(const VVCLocalContext *lc,
    const int merge_idx, Mv *cand_list, int *nb_merge_cand)
{
    const CodingUnit *cu = lc->cu;
    const EntryPoint *ep = lc->ep;
    const int is_gt4by4  = (cu->cb_width * cu->cb_height) > 16;
    int num_cands        = *nb_merge_cand;

    for (int i = 1; i <= ep->num_hmvp_ibc; i++) {
        int same_motion = 0;
        const MvField *mvf = &ep->hmvp_ibc[ep->num_hmvp_ibc - i];
        for (int j = 0; j < *nb_merge_cand; j++) {
            same_motion = is_gt4by4 && i == 1 && IS_SAME_MV(&mvf->mv[L0], &cand_list[j]);
            if (same_motion)
                break;
        }
        if (!same_motion) {
            cand_list[num_cands++] = mvf->mv[L0];
            if (num_cands > merge_idx)
                return 1;
        }
    }

    *nb_merge_cand = num_cands;
    return 0;
}

#define MV_BITS 18
#define IBC_SHIFT(v) ((v) >= (1 << (MV_BITS - 1)) ? ((v) - (1 << MV_BITS)) : (v))

static inline void ibc_add_mvp(Mv *mv, Mv *mvp, const int amvr_shift)
{
    ff_vvc_round_mv(mv, amvr_shift, 0);
    ff_vvc_round_mv(mvp, amvr_shift, amvr_shift);
    mv->x = IBC_SHIFT(mv->x + mvp->x);
    mv->y = IBC_SHIFT(mv->y + mvp->y);
}

static void ibc_merge_candidates(VVCLocalContext *lc, const int merge_idx, Mv *mv)
{
    const CodingUnit *cu = lc->cu;
    LOCAL_ALIGNED_8(Mv, cand_list, [MRG_MAX_NUM_CANDS]);
    int nb_cands;

    ff_vvc_set_neighbour_available(lc, cu->x0, cu->y0, cu->cb_width, cu->cb_height);
    if (ibc_spatial_candidates(lc, merge_idx, cand_list, &nb_cands) ||
        ibc_history_candidates(lc, merge_idx, cand_list, &nb_cands)) {
        *mv = cand_list[merge_idx];
        return;
    }

    //zero mv
    memset(mv, 0, sizeof(*mv));
}

static int ibc_check_mv(VVCLocalContext *lc, Mv *mv)
{
    const VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps         = lc->fc->ps.sps;
    const CodingUnit *cu      = lc->cu;
    const Mv *bv              = &cu->pu.mi.mv[L0][0];

    if (sps->ctb_size_y < ((cu->y0 + (bv->y >> 4)) & (sps->ctb_size_y - 1)) + cu->cb_height) {
        av_log(fc->log_ctx, AV_LOG_ERROR, "IBC region spans multiple CTBs.\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

int ff_vvc_mvp_ibc(VVCLocalContext *lc, const int mvp_l0_flag, const int amvr_shift, Mv *mv)
{
    LOCAL_ALIGNED_8(Mv, mvp, [1]);

    ibc_merge_candidates(lc, mvp_l0_flag, mvp);
    ibc_add_mvp(mv, mvp, amvr_shift);
    return ibc_check_mv(lc, mv);
}

int ff_vvc_luma_mv_merge_ibc(VVCLocalContext *lc, const int merge_idx, Mv *mv)
{
    ibc_merge_candidates(lc, merge_idx, mv);
    return ibc_check_mv(lc, mv);
}

static int affine_mvp_constructed_cp(NeighbourContext *ctx,
    const NeighbourIdx *neighbour, const int num_neighbour,
    const int lx, const int8_t ref_idx, const int amvr_shift, Mv *cp)
{
    const VVCLocalContext *lc       = ctx->lc;
    const VVCFrameContext *fc       = lc->fc;
    const MvField *tab_mvf          = fc->tab.mvf;
    const int min_pu_width          = fc->ps.pps->min_pu_width;
    const RefPicList* rpl           = lc->sc->rpl;
    int available                   = 0;

    for (int i = 0; i < num_neighbour; i++) {
        Neighbour *n = &ctx->neighbours[neighbour[i]];
        if (check_available(n, ctx->lc, 0)) {
            const PredFlag maskx = lx + 1;
            const MvField* mvf = &TAB_MVF(n->x, n->y);
            const int poc = rpl[lx].refs[ref_idx].poc;
            if ((mvf->pred_flag & maskx) && rpl[lx].refs[mvf->ref_idx[lx]].poc == poc) {
                available = 1;
                *cp = mvf->mv[lx];
            } else {
                const int ly = !lx;
                const PredFlag masky = ly + 1;
                if ((mvf->pred_flag & masky) && rpl[ly].refs[mvf->ref_idx[ly]].poc == poc) {
                    available = 1;
                    *cp = mvf->mv[ly];
                }
            }
            if (available) {
                ff_vvc_round_mv(cp, amvr_shift, amvr_shift);
                return 1;
            }
        }
    }
    return 0;
}

#define AFFINE_MVP_CONSTRUCTED_CP(cands, cp)                                    \
    affine_mvp_constructed_cp(nctx, cands, FF_ARRAY_ELEMS(cands), lx, ref_idx,  \
        amvr_shift, cp)

//8.5.5.8 Derivation process for constructed affine control point motion vector prediction candidates
static int affine_mvp_const1(NeighbourContext* nctx,
    const int lx, const int8_t ref_idx, const int amvr_shift,
    Mv *cps, int *available)
{
    const NeighbourIdx tl[] = { B2, B3, A2 };
    const NeighbourIdx tr[] = { B1, B0 };
    const NeighbourIdx bl[] = { A1, A0 };

    available[0] = AFFINE_MVP_CONSTRUCTED_CP(tl, cps + 0);
    available[1] = AFFINE_MVP_CONSTRUCTED_CP(tr, cps + 1);
    available[2] = AFFINE_MVP_CONSTRUCTED_CP(bl, cps + 2);
    return available[0] && available[1];
}

//8.5.5.7 item 7
static void affine_mvp_const2(const int idx, Mv *cps, const int num_cp)
{
    const Mv mv = cps[idx];
    for (int j = 0; j < num_cp; j++)
        cps[j] = mv;
}

//8.5.5.7 Derivation process for luma affine control point motion vector predictors
static void affine_mvp(const VVCLocalContext *lc,
    const int mvp_lx_flag, const int lx, const int8_t *ref_idx, const int amvr_shift,
    MotionModelIdc motion_model_idc, Mv *cps)
{
    const NeighbourIdx ak[] = { A0, A1 };
    const NeighbourIdx bk[] = { B0, B1, B2 };
    const int num_cp = motion_model_idc + 1;
    NeighbourContext nctx;
    int available[MAX_CONTROL_POINTS];
    int num_cands    = 0;

    init_neighbour_context(&nctx, lc);
    //Ak
    if (AFFINE_MVP_FROM_NBS(ak)) {
        if (mvp_lx_flag == num_cands)
            return;
        num_cands++;
    }
    //Bk
    if (AFFINE_MVP_FROM_NBS(bk)) {
        if (mvp_lx_flag == num_cands)
            return;
        num_cands++;
    }

    //Const1
    if (affine_mvp_const1(&nctx, lx, ref_idx[lx], amvr_shift, cps, available)) {
        if (available[2] || motion_model_idc == MOTION_4_PARAMS_AFFINE) {
            if (mvp_lx_flag == num_cands)
                return;
            num_cands++;
        }
    }

    //Const2
    for (int i = 2; i >= 0; i--) {
        if (available[i]) {
            if (mvp_lx_flag == num_cands) {
                affine_mvp_const2(i, cps, num_cp);
                return;
            }
            num_cands++;
        }
    }
    if (temporal_luma_motion_vector(lc, ref_idx[lx], cps, lx, 1, 0)) {
        if (mvp_lx_flag == num_cands) {
            ff_vvc_round_mv(cps, amvr_shift, amvr_shift);
            for (int i = 1; i < num_cp; i++)
                cps[i] = cps[0];
            return;
        }
        num_cands++;
    }

    //Zero Mv
    memset(cps, 0, num_cp * sizeof(Mv));
}

void ff_vvc_affine_mvp(VVCLocalContext *lc, const int *mvp_lx_flag, const int amvr_shift,  MotionInfo *mi)
{
    const CodingUnit *cu = lc->cu;

    mi->num_sb_x = cu->cb_width >> MIN_PU_LOG2;
    mi->num_sb_y = cu->cb_height >> MIN_PU_LOG2;

    ff_vvc_set_neighbour_available(lc, cu->x0, cu->y0, cu->cb_width, cu->cb_height);
    if (mi->pred_flag != PF_L1)
        affine_mvp(lc, mvp_lx_flag[L0], L0, mi->ref_idx, amvr_shift, mi->motion_model_idc, &mi->mv[L0][0]);
    if (mi->pred_flag != PF_L0)
        affine_mvp(lc, mvp_lx_flag[L1], L1, mi->ref_idx, amvr_shift, mi->motion_model_idc, &mi->mv[L1][0]);
}

//8.5.2.14 Rounding process for motion vectors
void ff_vvc_round_mv(Mv *mv, const int lshift, const int rshift)
{
    if (rshift) {
        const int offset = 1 << (rshift - 1);
        mv->x = ((mv->x + offset - (mv->x >= 0)) >> rshift) * (1 << lshift);
        mv->y = ((mv->y + offset - (mv->y >= 0)) >> rshift) * (1 << lshift);
    } else {
        mv->x = mv->x * (1 << lshift);
        mv->y = mv->y * (1 << lshift);
    }
}

void ff_vvc_clip_mv(Mv *mv)
{
    mv->x = av_clip(mv->x, -(1 << 17), (1 << 17) - 1);
    mv->y = av_clip(mv->y, -(1 << 17), (1 << 17) - 1);
}

//8.5.2.1 Derivation process for motion vector components and reference indices
static av_always_inline int is_greater_mer(const VVCFrameContext *fc, const int x0, const int y0, const int x0_br, const int y0_br)
{
    const uint8_t plevel = fc->ps.sps->log2_parallel_merge_level;

    return x0_br >> plevel > x0 >> plevel &&
           y0_br >> plevel > y0 >> plevel;
}

static void update_hmvp(MvField *hmvp, int *num_hmvp, const MvField *mvf,
    int (*compare)(const MvField *n, const MvField *o))
{
    int i;
    for (i = 0; i < *num_hmvp; i++) {
        if (compare(mvf, hmvp + i)) {
            (*num_hmvp)--;
            break;
        }
    }
    if (i == MAX_NUM_HMVP_CANDS) {
        (*num_hmvp)--;
        i = 0;
    }

    memmove(hmvp + i, hmvp + i + 1, (*num_hmvp - i) * sizeof(MvField));
    hmvp[(*num_hmvp)++] = *mvf;
}

static int compare_l0_mv(const MvField *n, const MvField *o)
{
    return IS_SAME_MV(&n->mv[L0], &o->mv[L0]);
}

//8.6.2.4 Derivation process for IBC history-based block vector candidates
//8.5.2.16 Updating process for the history-based motion vector predictor candidate list
void ff_vvc_update_hmvp(VVCLocalContext *lc, const MotionInfo *mi)
{
    const VVCFrameContext *fc   = lc->fc;
    const CodingUnit *cu        = lc->cu;
    const int min_pu_width      = fc->ps.pps->min_pu_width;
    const MvField *tab_mvf      = fc->tab.mvf;
    EntryPoint *ep              = lc->ep;

    if (cu->pred_mode == MODE_IBC) {
        if (cu->cb_width * cu->cb_height <= 16)
            return;
        update_hmvp(ep->hmvp_ibc, &ep->num_hmvp_ibc, &TAB_MVF(cu->x0, cu->y0), compare_l0_mv);
    } else {
        if (!is_greater_mer(fc, cu->x0, cu->y0, cu->x0 + cu->cb_width, cu->y0 + cu->cb_height))
            return;
        update_hmvp(ep->hmvp, &ep->num_hmvp, &TAB_MVF(cu->x0, cu->y0), compare_mv_ref_idx);
    }
}

MvField* ff_vvc_get_mvf(const VVCFrameContext *fc, const int x0, const int y0)
{
    const int min_pu_width  = fc->ps.pps->min_pu_width;
    MvField* tab_mvf        = fc->tab.mvf;

    return &TAB_MVF(x0, y0);
}
