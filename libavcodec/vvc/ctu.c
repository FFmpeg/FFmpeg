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

#include "libavutil/refstruct.h"

#include "cabac.h"
#include "ctu.h"
#include "inter.h"
#include "intra.h"
#include "mvs.h"

#define PROF_TEMP_SIZE (PROF_BLOCK_SIZE) * sizeof(int16_t)

#define TAB_MSM(fc, depth, x, y) fc->tab.msm[(depth)][((y) >> 5) * fc->ps.pps->width32 + ((x) >> 5)]
#define TAB_ISPMF(fc, x, y) fc->tab.ispmf[((y) >> 6) * fc->ps.pps->width64 + ((x) >> 6)]

typedef enum VVCModeType {
    MODE_TYPE_ALL,
    MODE_TYPE_INTER,
    MODE_TYPE_INTRA,
} VVCModeType;

static void set_tb_size(const VVCFrameContext *fc, const TransformBlock *tb)
{
    const int x_tb      = tb->x0 >> MIN_TU_LOG2;
    const int y_tb      = tb->y0 >> MIN_TU_LOG2;
    const int hs        = fc->ps.sps->hshift[tb->c_idx];
    const int vs        = fc->ps.sps->vshift[tb->c_idx];
    const int is_chroma = tb->c_idx != 0;
    const int width     = FFMAX(1, tb->tb_width >> (MIN_TU_LOG2 - hs));
    const int end       = y_tb + FFMAX(1, tb->tb_height >> (MIN_TU_LOG2 - vs));

    for (int y = y_tb; y < end; y++) {
        const int off = y * fc->ps.pps->min_tu_width + x_tb;
        memset(fc->tab.tb_width [is_chroma] + off, tb->tb_width,  width);
        memset(fc->tab.tb_height[is_chroma] + off, tb->tb_height, width);
    }
}

static void set_tb_tab(uint8_t *tab, uint8_t v, const VVCFrameContext *fc,
    const TransformBlock *tb)
{
    const int width  = tb->tb_width  << fc->ps.sps->hshift[tb->c_idx];
    const int height = tb->tb_height << fc->ps.sps->vshift[tb->c_idx];

    for (int h = 0; h < height; h += MIN_TU_SIZE) {
        const int y = (tb->y0 + h) >> MIN_TU_LOG2;
        const int off = y * fc->ps.pps->min_tu_width + (tb->x0 >> MIN_TU_LOG2);
        const int w = FFMAX(1, width >> MIN_TU_LOG2);
        memset(tab + off, v, w);
    }
}

// 8.7.1 Derivation process for quantization parameters
static int get_qp_y_pred(const VVCLocalContext *lc)
{
    const VVCFrameContext *fc     = lc->fc;
    const VVCSPS *sps       = fc->ps.sps;
    const VVCPPS *pps       = fc->ps.pps;
    const CodingUnit *cu    = lc->cu;
    const int ctb_log2_size = sps->ctb_log2_size_y;
    const int ctb_size_mask = (1 << ctb_log2_size) - 1;
    const int xQg           = lc->parse.cu_qg_top_left_x;
    const int yQg           = lc->parse.cu_qg_top_left_y;
    const int min_cb_width  = fc->ps.pps->min_cb_width;
    const int x_cb          = cu->x0 >> sps->min_cb_log2_size_y;
    const int y_cb          = cu->y0 >> sps->min_cb_log2_size_y;
    const int rx            = cu->x0 >> ctb_log2_size;
    const int ry            = cu->y0 >> ctb_log2_size;
    const int in_same_ctb_a = ((xQg - 1) >> ctb_log2_size) == rx && (yQg >> ctb_log2_size) == ry;
    const int in_same_ctb_b = (xQg >> ctb_log2_size) == rx && ((yQg - 1) >> ctb_log2_size) == ry;
    int qPy_pred, qPy_a, qPy_b;

    if (lc->na.cand_up) {
        const int first_qg_in_ctu = !(xQg & ctb_size_mask) &&  !(yQg & ctb_size_mask);
        const int qPy_up          = fc->tab.qp[LUMA][x_cb + (y_cb - 1) * min_cb_width];
        if (first_qg_in_ctu && pps->ctb_to_col_bd[xQg >> ctb_log2_size] == xQg >> ctb_log2_size)
            return qPy_up;
    }

    // qPy_pred
    qPy_pred = lc->ep->is_first_qg ? lc->sc->sh.slice_qp_y : lc->ep->qp_y;

    // qPy_b
    if (!lc->na.cand_up || !in_same_ctb_b)
        qPy_b = qPy_pred;
    else
        qPy_b = fc->tab.qp[LUMA][x_cb + (y_cb - 1) * min_cb_width];

    // qPy_a
    if (!lc->na.cand_left || !in_same_ctb_a)
        qPy_a = qPy_pred;
    else
        qPy_a = fc->tab.qp[LUMA][(x_cb - 1) + y_cb * min_cb_width];

    av_assert2(qPy_a >= -fc->ps.sps->qp_bd_offset && qPy_a <= 63);
    av_assert2(qPy_b >= -fc->ps.sps->qp_bd_offset && qPy_b <= 63);

    return (qPy_a + qPy_b + 1) >> 1;
}

static void set_cb_tab(const VVCLocalContext *lc, uint8_t *tab, const uint8_t v)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCPPS *pps           = fc->ps.pps;
    const CodingUnit *cu        = lc->cu;
    const int log2_min_cb_size  = fc->ps.sps->min_cb_log2_size_y;
    const int x_cb              = cu->x0 >> log2_min_cb_size;
    const int y_cb              = cu->y0 >> log2_min_cb_size;
    const int cb_width          = cu->cb_width;
    const int cb_height         = cu->cb_height;
    int x                       = y_cb * pps->min_cb_width + x_cb;

    for (int y = 0; y < (cb_height >> log2_min_cb_size); y++) {
        const int width = cb_width >> log2_min_cb_size;

        memset(&tab[x], v, width);
        x += pps->min_cb_width;
    }
}

static int set_qp_y(VVCLocalContext *lc, const int x0, const int y0, const int has_qp_delta)
{
    const VVCSPS *sps   = lc->fc->ps.sps;
    EntryPoint *ep      = lc->ep;
    CodingUnit *cu      = lc->cu;
    int cu_qp_delta     = 0;

    if (!lc->fc->ps.pps->r->pps_cu_qp_delta_enabled_flag) {
        ep->qp_y = lc->sc->sh.slice_qp_y;
    } else if (ep->is_first_qg || (lc->parse.cu_qg_top_left_x == x0 && lc->parse.cu_qg_top_left_y == y0)) {
        ep->qp_y = get_qp_y_pred(lc);
        ep->is_first_qg = 0;
    }

    if (has_qp_delta) {
        const int cu_qp_delta_abs = ff_vvc_cu_qp_delta_abs(lc);

        if (cu_qp_delta_abs)
            cu_qp_delta = ff_vvc_cu_qp_delta_sign_flag(lc) ? -cu_qp_delta_abs : cu_qp_delta_abs;
        if (cu_qp_delta > (31 + sps->qp_bd_offset / 2) || cu_qp_delta < -(32 + sps->qp_bd_offset / 2))
            return AVERROR_INVALIDDATA;
        lc->parse.is_cu_qp_delta_coded = 1;

        if (cu_qp_delta) {
            int off = sps->qp_bd_offset;
            ep->qp_y = FFUMOD(ep->qp_y + cu_qp_delta + 64 + 2 * off, 64 + off) - off;
        }
    }

    set_cb_tab(lc, lc->fc->tab.qp[LUMA], ep->qp_y);
    cu->qp[LUMA] = ep->qp_y;

    return 0;
}

static void set_qp_c_tab(const VVCLocalContext *lc, const TransformUnit *tu, const TransformBlock *tb)
{
    const int is_jcbcr = tu->joint_cbcr_residual_flag && tu->coded_flag[CB] && tu->coded_flag[CR];
    const int idx = is_jcbcr ? JCBCR : tb->c_idx;

    set_tb_tab(lc->fc->tab.qp[tb->c_idx], lc->cu->qp[idx], lc->fc, tb);
}

static void set_qp_c(VVCLocalContext *lc)
{
    const VVCFrameContext *fc       = lc->fc;
    const VVCSPS *sps               = fc->ps.sps;
    const VVCPPS *pps               = fc->ps.pps;
    const H266RawSliceHeader *rsh   = lc->sc->sh.r;
    CodingUnit *cu                  = lc->cu;
    const int x_center              = cu->x0 + cu->cb_width  / 2;
    const int y_center              = cu->y0 + cu->cb_height / 2;
    const int single_tree           = cu->tree_type == SINGLE_TREE;
    const int qp_luma               = (single_tree ? lc->ep->qp_y : ff_vvc_get_qPy(fc, x_center, y_center)) + sps->qp_bd_offset;
    const int qp_chroma             = av_clip(qp_luma, 0, MAX_QP + sps->qp_bd_offset);
    const int sh_chroma_qp_offset[] = {
        rsh->sh_cb_qp_offset,
        rsh->sh_cr_qp_offset,
        rsh->sh_joint_cbcr_qp_offset,
    };
    int qp;

    for (int i = CB - 1; i < CR + sps->r->sps_joint_cbcr_enabled_flag; i++) {
        qp = sps->chroma_qp_table[i][qp_chroma];
        qp = qp + pps->chroma_qp_offset[i] + sh_chroma_qp_offset[i] + lc->parse.chroma_qp_offset[i];
        qp = av_clip(qp, -sps->qp_bd_offset, MAX_QP) + sps->qp_bd_offset;
        cu->qp[i + 1] = qp;
    }
}

static TransformUnit* alloc_tu(VVCFrameContext *fc, CodingUnit *cu)
{
    TransformUnit *tu = av_refstruct_pool_get(fc->tu_pool);
    if (!tu)
        return NULL;

    tu->next = NULL;

    if (cu->tus.tail)
        cu->tus.tail->next =  tu;
    else
        cu->tus.head = tu;
    cu->tus.tail = tu;

    return tu;
}

static TransformUnit* add_tu(VVCFrameContext *fc, CodingUnit *cu, const int x0, const int y0, const int tu_width, const int tu_height)
{
    TransformUnit *tu = alloc_tu(fc, cu);

    if (!tu)
        return NULL;

    tu->x0 = x0;
    tu->y0 = y0;
    tu->width = tu_width;
    tu->height = tu_height;
    tu->joint_cbcr_residual_flag = 0;
    memset(tu->coded_flag, 0, sizeof(tu->coded_flag));
    tu->avail[LUMA] = tu->avail[CHROMA] = 0;
    tu->nb_tbs = 0;

    return tu;
}

static TransformBlock* add_tb(TransformUnit *tu, VVCLocalContext *lc,
    const int x0, const int y0, const int tb_width, const int tb_height, const int c_idx)
{
    TransformBlock *tb;

    tb = &tu->tbs[tu->nb_tbs++];
    tb->has_coeffs = 0;
    tb->x0 = x0;
    tb->y0 = y0;
    tb->tb_width  = tb_width;
    tb->tb_height = tb_height;
    tb->log2_tb_width  = av_log2(tb_width);
    tb->log2_tb_height = av_log2(tb_height);

    tb->max_scan_x = tb->max_scan_y = 0;
    tb->min_scan_x = tb->min_scan_y = 0;

    tb->c_idx = c_idx;
    tb->ts = 0;
    tb->coeffs = lc->coeffs;
    lc->coeffs += tb_width * tb_height;
    tu->avail[!!c_idx] = true;
    return tb;
}

static uint8_t tu_y_coded_flag_decode(VVCLocalContext *lc, const int is_sbt_not_coded,
    const int sub_tu_index, const int is_isp, const int is_chroma_coded)
{
    uint8_t tu_y_coded_flag = 0;
    const VVCSPS *sps       = lc->fc->ps.sps;
    CodingUnit *cu          = lc->cu;

    if (!is_sbt_not_coded) {
        int has_y_coded_flag = sub_tu_index < cu->num_intra_subpartitions - 1 || !lc->parse.infer_tu_cbf_luma;
        if (!is_isp) {
            const int is_large = cu->cb_width > sps->max_tb_size_y || cu->cb_height > sps->max_tb_size_y;
            has_y_coded_flag = (cu->pred_mode == MODE_INTRA && !cu->act_enabled_flag) || is_chroma_coded || is_large;
        }
        tu_y_coded_flag = has_y_coded_flag ? ff_vvc_tu_y_coded_flag(lc) : 1;
    }
    if (is_isp)
        lc->parse.infer_tu_cbf_luma = lc->parse.infer_tu_cbf_luma && !tu_y_coded_flag;
    return tu_y_coded_flag;
}

static void chroma_qp_offset_decode(VVCLocalContext *lc, const int is_128, const int is_chroma_coded)
{
    const VVCPPS *pps               = lc->fc->ps.pps;
    const H266RawSliceHeader *rsh   = lc->sc->sh.r;

    if ((is_128 || is_chroma_coded) &&
        rsh->sh_cu_chroma_qp_offset_enabled_flag && !lc->parse.is_cu_chroma_qp_offset_coded) {
        const int cu_chroma_qp_offset_flag = ff_vvc_cu_chroma_qp_offset_flag(lc);
        if (cu_chroma_qp_offset_flag) {
            int cu_chroma_qp_offset_idx = 0;
            if (pps->r->pps_chroma_qp_offset_list_len_minus1 > 0)
                cu_chroma_qp_offset_idx = ff_vvc_cu_chroma_qp_offset_idx(lc);
            for (int i = CB - 1; i < JCBCR; i++)
                lc->parse.chroma_qp_offset[i] = pps->chroma_qp_offset_list[cu_chroma_qp_offset_idx][i];
        } else {
            memset(lc->parse.chroma_qp_offset, 0, sizeof(lc->parse.chroma_qp_offset));
        }
        lc->parse.is_cu_chroma_qp_offset_coded = 1;
    }
}

static int hls_transform_unit(VVCLocalContext *lc, int x0, int y0,int tu_width, int tu_height, int sub_tu_index, int ch_type)
{
    VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps   = fc->ps.sps;
    const VVCPPS *pps   = fc->ps.pps;
    CodingUnit *cu      = lc->cu;
    TransformUnit *tu   = add_tu(fc, cu, x0, y0, tu_width, tu_height);
    const int min_cb_width      = pps->min_cb_width;
    const VVCTreeType tree_type = cu->tree_type;
    const int is_128            = cu->cb_width > 64 || cu->cb_height > 64;
    const int is_isp            = cu->isp_split_type != ISP_NO_SPLIT;
    const int is_isp_last_tu    = is_isp && (sub_tu_index == cu->num_intra_subpartitions - 1);
    const int is_sbt_not_coded  = cu->sbt_flag &&
        ((sub_tu_index == 0 && cu->sbt_pos_flag) || (sub_tu_index == 1 && !cu->sbt_pos_flag));
    const int chroma_available  = tree_type != DUAL_TREE_LUMA && sps->r->sps_chroma_format_idc &&
        (!is_isp || is_isp_last_tu);
    int ret, xc, yc, wc, hc, is_chroma_coded;

    if (!tu)
        return AVERROR_INVALIDDATA;

    if (tree_type == SINGLE_TREE && is_isp_last_tu) {
        const int x_cu = x0 >> fc->ps.sps->min_cb_log2_size_y;
        const int y_cu = y0 >> fc->ps.sps->min_cb_log2_size_y;
        xc = SAMPLE_CTB(fc->tab.cb_pos_x[ch_type],  x_cu, y_cu);
        yc = SAMPLE_CTB(fc->tab.cb_pos_y[ch_type],  x_cu, y_cu);
        wc = SAMPLE_CTB(fc->tab.cb_width[ch_type],  x_cu, y_cu);
        hc = SAMPLE_CTB(fc->tab.cb_height[ch_type], x_cu, y_cu);
    } else {
        xc = x0, yc = y0, wc = tu_width, hc = tu_height;
    }

    if (chroma_available && !is_sbt_not_coded) {
        tu->coded_flag[CB] = ff_vvc_tu_cb_coded_flag(lc);
        tu->coded_flag[CR] = ff_vvc_tu_cr_coded_flag(lc, tu->coded_flag[CB]);
    }

    is_chroma_coded = chroma_available && (tu->coded_flag[CB] || tu->coded_flag[CR]);

    if (tree_type != DUAL_TREE_CHROMA) {
        int has_qp_delta;
        tu->coded_flag[LUMA] = tu_y_coded_flag_decode(lc, is_sbt_not_coded, sub_tu_index, is_isp, is_chroma_coded);
        has_qp_delta = (is_128 || tu->coded_flag[LUMA] || is_chroma_coded) &&
            pps->r->pps_cu_qp_delta_enabled_flag && !lc->parse.is_cu_qp_delta_coded;
        ret = set_qp_y(lc, x0, y0, has_qp_delta);
        if (ret < 0)
            return ret;
        add_tb(tu, lc, x0, y0, tu_width, tu_height, LUMA);
    }
    if (tree_type != DUAL_TREE_LUMA) {
        chroma_qp_offset_decode(lc, is_128, is_chroma_coded);
        if (chroma_available) {
            const int hs = sps->hshift[CHROMA];
            const int vs = sps->vshift[CHROMA];
            add_tb(tu, lc, xc, yc, wc >> hs, hc >> vs, CB);
            add_tb(tu, lc, xc, yc, wc >> hs, hc >> vs, CR);
        }
    }
    if (sps->r->sps_joint_cbcr_enabled_flag && ((cu->pred_mode == MODE_INTRA &&
        (tu->coded_flag[CB] || tu->coded_flag[CR])) ||
        (tu->coded_flag[CB] && tu->coded_flag[CR])) &&
        chroma_available) {
        tu->joint_cbcr_residual_flag = ff_vvc_tu_joint_cbcr_residual_flag(lc, tu->coded_flag[1], tu->coded_flag[2]);
    }

    for (int i = 0; i < tu->nb_tbs; i++) {
        TransformBlock *tb  = &tu->tbs[i];
        const int is_chroma = tb->c_idx != LUMA;
        tb->has_coeffs = tu->coded_flag[tb->c_idx];
        if (tb->has_coeffs && is_chroma)
            tb->has_coeffs = tb->c_idx == CB ? 1 : !(tu->coded_flag[CB] && tu->joint_cbcr_residual_flag);
        if (tb->has_coeffs) {
            tb->ts = cu->bdpcm_flag[tb->c_idx];
            if (sps->r->sps_transform_skip_enabled_flag && !cu->bdpcm_flag[tb->c_idx] &&
                tb->tb_width <= sps->max_ts_size && tb->tb_height <= sps->max_ts_size &&
                !cu->sbt_flag && (is_chroma || !is_isp)) {
                tb->ts = ff_vvc_transform_skip_flag(lc, is_chroma);
            }
            ret = ff_vvc_residual_coding(lc, tb);
            if (ret < 0)
                return ret;
            set_tb_tab(fc->tab.tu_coded_flag[tb->c_idx], tu->coded_flag[tb->c_idx], fc, tb);
        } else if (cu->act_enabled_flag) {
            memset(tb->coeffs, 0, tb->tb_width * tb->tb_height * sizeof(*tb->coeffs));
        }
        if (tb->c_idx != CR)
            set_tb_size(fc, tb);
        if (tb->c_idx == CB)
            set_tb_tab(fc->tab.tu_joint_cbcr_residual_flag, tu->joint_cbcr_residual_flag, fc, tb);
    }

    return 0;
}

static int hls_transform_tree(VVCLocalContext *lc, int x0, int y0,int tu_width, int tu_height, int ch_type)
{
    const CodingUnit *cu = lc->cu;
    const VVCSPS *sps = lc->fc->ps.sps;
    int ret;

    lc->parse.infer_tu_cbf_luma = 1;
    if (cu->isp_split_type == ISP_NO_SPLIT && !cu->sbt_flag) {
        if (tu_width > sps->max_tb_size_y || tu_height > sps->max_tb_size_y) {
            const int ver_split_first = tu_width > sps->max_tb_size_y && tu_width > tu_height;
            const int trafo_width  =  ver_split_first ? (tu_width  / 2) : tu_width;
            const int trafo_height = !ver_split_first ? (tu_height / 2) : tu_height;

            #define TRANSFORM_TREE(x, y) do {                                           \
                ret = hls_transform_tree(lc, x, y, trafo_width, trafo_height, ch_type);  \
                if (ret < 0)                                                            \
                    return ret;                                                         \
            } while (0)

            TRANSFORM_TREE(x0, y0);
            if (ver_split_first)
                TRANSFORM_TREE(x0 + trafo_width, y0);
            else
                TRANSFORM_TREE(x0, y0 + trafo_height);

        } else {
            ret = hls_transform_unit(lc, x0, y0, tu_width, tu_height, 0, ch_type);
            if (ret < 0)
                return ret;

        }
    } else if (cu->sbt_flag) {
        if (!cu->sbt_horizontal_flag) {
            #define TRANSFORM_UNIT(x, width, idx) do {                              \
                ret = hls_transform_unit(lc, x, y0, width, tu_height, idx, ch_type); \
                if (ret < 0)                                                        \
                    return ret;                                                     \
            } while (0)

            const int trafo_width = tu_width * lc->parse.sbt_num_fourths_tb0 / 4;
            TRANSFORM_UNIT(x0, trafo_width, 0);
            TRANSFORM_UNIT(x0 + trafo_width, tu_width - trafo_width, 1);

            #undef TRANSFORM_UNIT
        } else {
            #define TRANSFORM_UNIT(y, height, idx) do {                             \
                ret = hls_transform_unit(lc, x0, y, tu_width, height, idx, ch_type); \
                if (ret < 0)                                                        \
                    return ret;                                                     \
            } while (0)

            const int trafo_height = tu_height * lc->parse.sbt_num_fourths_tb0 / 4;
            TRANSFORM_UNIT(y0, trafo_height, 0);
            TRANSFORM_UNIT(y0 + trafo_height, tu_height - trafo_height, 1);

            #undef TRANSFORM_UNIT
        }
    } else if (cu->isp_split_type == ISP_HOR_SPLIT) {
        const int trafo_height = tu_height / cu->num_intra_subpartitions;
        for (int i = 0; i < cu->num_intra_subpartitions; i++) {
            ret = hls_transform_unit(lc, x0, y0 + trafo_height * i, tu_width, trafo_height, i, 0);
            if (ret < 0)
                return ret;
        }
    } else if (cu->isp_split_type == ISP_VER_SPLIT) {
        const int trafo_width = tu_width / cu->num_intra_subpartitions;
        for (int i = 0; i < cu->num_intra_subpartitions; i++) {
            ret = hls_transform_unit(lc, x0 + trafo_width * i , y0, trafo_width, tu_height, i, 0);
            if (ret < 0)
                return ret;
        }
    }

    return 0;
}

static int skipped_transform_tree(VVCLocalContext *lc, int x0, int y0,int tu_width, int tu_height)
{
    VVCFrameContext *fc  = lc->fc;
    const CodingUnit *cu = lc->cu;
    const VVCSPS *sps    = fc->ps.sps;

    if (tu_width > sps->max_tb_size_y || tu_height > sps->max_tb_size_y) {
        const int ver_split_first = tu_width > sps->max_tb_size_y && tu_width > tu_height;
        const int trafo_width  =  ver_split_first ? (tu_width  / 2) : tu_width;
        const int trafo_height = !ver_split_first ? (tu_height / 2) : tu_height;

        #define SKIPPED_TRANSFORM_TREE(x, y) do {                                   \
            int ret = skipped_transform_tree(lc, x, y, trafo_width, trafo_height);  \
            if (ret < 0)                                                            \
                return ret;                                                         \
        } while (0)

        SKIPPED_TRANSFORM_TREE(x0, y0);
        if (ver_split_first)
            SKIPPED_TRANSFORM_TREE(x0 + trafo_width, y0);
        else
            SKIPPED_TRANSFORM_TREE(x0, y0 + trafo_height);
    } else {
        TransformUnit *tu    = add_tu(fc, lc->cu, x0, y0, tu_width, tu_height);
        int start, end;

        if (!tu)
            return AVERROR_INVALIDDATA;
        ff_vvc_channel_range(&start, &end, cu->tree_type, sps->r->sps_chroma_format_idc);
        for (int i = start; i < end; i++) {
            TransformBlock *tb = add_tb(tu, lc, x0, y0, tu_width >> sps->hshift[i], tu_height >> sps->vshift[i], i);
            if (i != CR)
                set_tb_size(fc, tb);
        }
    }

    return 0;
}

//6.4.1 Allowed quad split process
//6.4.2 Allowed binary split process
//6.4.3 Allowed ternary split process
static void can_split(const VVCLocalContext *lc, int x0, int y0,int cb_width, int cb_height,
     int mtt_depth, int depth_offset, int part_idx, VVCSplitMode last_split_mode,
     VVCTreeType tree_type, VVCModeType mode_type, VVCAllowedSplit* split)
{
    int min_qt_size, max_bt_size, max_tt_size, max_mtt_depth;
    const VVCFrameContext *fc   = lc->fc;
    const VVCSH *sh             = &lc->sc->sh;
    const VVCSPS *sps           = fc->ps.sps;
    const VVCPPS *pps           = fc->ps.pps;
    const int chroma            = tree_type == DUAL_TREE_CHROMA;
    int min_cb_size_y           = sps->min_cb_size_y;
    int *qt                     = &split->qt;
    int *btv                    = &split->btv;
    int *bth                    = &split->bth;
    int *ttv                    = &split->ttv;
    int *tth                    = &split->tth;

    *qt = *bth = *btv = *tth = *ttv = 1;

    if (mtt_depth)
        *qt = 0;

    min_qt_size = sh->min_qt_size[chroma];
    if (cb_width <= min_qt_size)
        *qt = 0;

    if (chroma) {
        int chroma_area = (cb_width >> sps->hshift[1]) * (cb_height >> sps->vshift[1]);
        int chroma_width = cb_width >> sps->hshift[1];

        if (chroma_width == 8)
            *ttv = 0;
        else if (chroma_width <= 4) {
            if (chroma_width == 4)
                *btv = 0;
            *qt = 0;
        }
        if (mode_type == MODE_TYPE_INTRA)
            *qt = *btv = *bth = *ttv = *tth = 0;
        if (chroma_area <= 32) {
            *ttv = *tth = 0;
            if (chroma_area <= 16)
                *btv = *bth = 0;
        }
    }
    max_bt_size = sh->max_bt_size[chroma];
    max_tt_size = sh->max_tt_size[chroma];
    max_mtt_depth = sh->max_mtt_depth[chroma] + depth_offset;

    if (mode_type == MODE_TYPE_INTER) {
        int area = cb_width * cb_height;
        if (area == 32)
            *btv = *bth = 0;
        else if (area == 64)
            *ttv = *tth = 0;
    }
    if (cb_width <= 2 * min_cb_size_y) {
        *ttv = 0;
        if (cb_width <= min_cb_size_y)
            *btv = 0;
    }
    if (cb_height <= 2 * min_cb_size_y) {
        *tth = 0;
        if (cb_height <= min_cb_size_y)
            *bth = 0;
    }
    if (cb_width > max_bt_size || cb_height > max_bt_size)
        *btv = *bth = 0;
    max_tt_size = FFMIN(64, max_tt_size);
    if (cb_width > max_tt_size || cb_height > max_tt_size)
        *ttv = *tth = 0;
    if (mtt_depth >= max_mtt_depth)
        *btv = *bth = *ttv = *tth = 0;
    if (x0 + cb_width > pps->width) {
        *ttv = *tth = 0;
        if (cb_height > 64)
            *btv = 0;
        if (y0 + cb_height <= pps->height)
            *bth = 0;
        else if (cb_width > min_qt_size)
            *btv = *bth = 0;
    }
    if (y0 + cb_height > pps->height) {
        *btv = *ttv = *tth = 0;
        if (cb_width > 64)
            *bth = 0;
    }
    if (mtt_depth > 0 && part_idx  == 1)  {
        if (last_split_mode == SPLIT_TT_VER)
            *btv = 0;
        else if (last_split_mode == SPLIT_TT_HOR)
            *bth = 0;
    }
    if (cb_width <= 64 && cb_height > 64)
        *btv = 0;
    if (cb_width > 64 && cb_height <= 64)
        *bth = 0;
}

static int get_num_intra_subpartitions(enum IspType isp_split_type, int cb_width, int cb_height)
{
    if (isp_split_type == ISP_NO_SPLIT)
        return 1;
    if ((cb_width == 4 && cb_height == 8) || (cb_width == 8 && cb_height == 4))
        return 2;
    return 4;
}

static int get_cclm_enabled(const VVCLocalContext *lc, const int x0, const int y0)
{
    const VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps   = fc->ps.sps;
    int enabled = 0;

    if (!sps->r->sps_cclm_enabled_flag)
        return 0;
    if (!sps->r->sps_qtbtt_dual_tree_intra_flag || !IS_I(lc->sc->sh.r) || sps->ctb_log2_size_y < 6)
        return 1;
    else {
        const int x64 = x0 >> 6 << 6;
        const int y64 = y0 >> 6 << 6;
        const int y32 = y0 >> 5 << 5;
        const int x64_cu = x64 >> fc->ps.sps->min_cb_log2_size_y;
        const int y64_cu = y64 >> fc->ps.sps->min_cb_log2_size_y;
        const int y32_cu = y32 >> fc->ps.sps->min_cb_log2_size_y;
        const int min_cb_width = fc->ps.pps->min_cb_width;
        const int depth = SAMPLE_CTB(fc->tab.cqt_depth[1], x64_cu, y64_cu);
        const int min_depth = fc->ps.sps->ctb_log2_size_y - 6;
        const VVCSplitMode msm64 = (VVCSplitMode)TAB_MSM(fc, 0, x64, y64);
        const VVCSplitMode msm32 = (VVCSplitMode)TAB_MSM(fc, 1, x64, y32);

        enabled = SAMPLE_CTB(fc->tab.cb_width[1], x64_cu, y64_cu) == 64 &&
            SAMPLE_CTB(fc->tab.cb_height[1], x64_cu, y64_cu) == 64;
        enabled |= depth == min_depth && msm64 == SPLIT_BT_HOR &&
            SAMPLE_CTB(fc->tab.cb_width[1], x64_cu, y32_cu) == 64 &&
            SAMPLE_CTB(fc->tab.cb_height[1], x64_cu, y32_cu) == 32;
        enabled |= depth > min_depth;
        enabled |= depth == min_depth && msm64 == SPLIT_BT_HOR && msm32 == SPLIT_BT_VER;

        if (enabled) {
            const int w = SAMPLE_CTB(fc->tab.cb_width[0], x64_cu, y64_cu);
            const int h = SAMPLE_CTB(fc->tab.cb_height[0], x64_cu, y64_cu);
            const int depth0 = SAMPLE_CTB(fc->tab.cqt_depth[0], x64_cu, y64_cu);
            if ((w == 64 && h == 64 && TAB_ISPMF(fc, x64, y64)) ||
                ((w < 64 || h < 64) && depth0 == min_depth))
                return 0;
        }

    }

    return enabled;
}

static int less(const void *a, const void *b)
{
    return *(const int*)a - *(const int*)b;
}

//8.4.2 Derivation process for luma intra prediction mode
static enum IntraPredMode luma_intra_pred_mode(VVCLocalContext* lc, const int intra_subpartitions_mode_flag)
{
    VVCFrameContext *fc     = lc->fc;
    CodingUnit *cu          = lc->cu;
    const int x0            = cu->x0;
    const int y0            = cu->y0;
    enum IntraPredMode pred;
    int intra_luma_not_planar_flag = 1;
    int intra_luma_mpm_remainder = 0;
    int intra_luma_mpm_flag = 1;
    int intra_luma_mpm_idx = 0;

    if (!cu->intra_luma_ref_idx)
        intra_luma_mpm_flag = ff_vvc_intra_luma_mpm_flag(lc);
    if (intra_luma_mpm_flag) {
        if (!cu->intra_luma_ref_idx)
            intra_luma_not_planar_flag = ff_vvc_intra_luma_not_planar_flag(lc, intra_subpartitions_mode_flag);
        if (intra_luma_not_planar_flag)
            intra_luma_mpm_idx = ff_vvc_intra_luma_mpm_idx(lc);
    } else {
        intra_luma_mpm_remainder = ff_vvc_intra_luma_mpm_remainder(lc);
    }

    if (!intra_luma_not_planar_flag) {
        pred = INTRA_PLANAR;
    } else {
        const VVCSPS *sps       = fc->ps.sps;
        const int x_a           = (x0 - 1) >> sps->min_cb_log2_size_y;
        const int y_a           = (y0 + cu->cb_height - 1) >> sps->min_cb_log2_size_y;
        const int x_b           = (x0 + cu->cb_width - 1) >> sps->min_cb_log2_size_y;
        const int y_b           = (y0 - 1) >> sps->min_cb_log2_size_y;
        int min_cb_width        = fc->ps.pps->min_cb_width;
        int x0b                 = av_zero_extend(x0, sps->ctb_log2_size_y);
        int y0b                 = av_zero_extend(y0, sps->ctb_log2_size_y);
        const int available_l   = lc->ctb_left_flag || x0b;
        const int available_u   = lc->ctb_up_flag || y0b;

        int a, b, cand[5];

       if (!available_l || (SAMPLE_CTB(fc->tab.cpm[0], x_a, y_a) != MODE_INTRA) ||
            SAMPLE_CTB(fc->tab.imf, x_a, y_a)) {
            a = INTRA_PLANAR;
        } else {
            a = SAMPLE_CTB(fc->tab.ipm, x_a, y_a);
        }

        if (!available_u || (SAMPLE_CTB(fc->tab.cpm[0], x_b, y_b) != MODE_INTRA) ||
            SAMPLE_CTB(fc->tab.imf, x_b, y_b) || !y0b) {
            b = INTRA_PLANAR;
        } else {
            b = SAMPLE_CTB(fc->tab.ipm, x_b, y_b);
        }

        if (a == b && a > INTRA_DC) {
            cand[0] = a;
            cand[1] = 2 + ((a + 61) % 64);
            cand[2] = 2 + ((a -  1) % 64);
            cand[3] = 2 + ((a + 60) % 64);
            cand[4] = 2 + (a % 64);
        } else {
            const int minab = FFMIN(a, b);
            const int maxab = FFMAX(a, b);
            if (a > INTRA_DC && b > INTRA_DC) {
                const int diff = maxab - minab;
                cand[0] = a;
                cand[1] = b;
                if (diff == 1) {
                    cand[2] = 2 + ((minab + 61) % 64);
                    cand[3] = 2 + ((maxab - 1) % 64);
                    cand[4] = 2 + ((minab + 60) % 64);
                } else if (diff >= 62) {
                    cand[2] = 2 + ((minab - 1) % 64);
                    cand[3] = 2 + ((maxab + 61) % 64);
                    cand[4] = 2 + (minab % 64);
                } else if (diff == 2) {
                    cand[2] = 2 + ((minab - 1) % 64);
                    cand[3] = 2 + ((minab + 61) % 64);
                    cand[4] = 2 + ((maxab - 1) % 64);
                } else {
                    cand[2] = 2 + ((minab + 61) % 64);
                    cand[3] = 2 + ((minab - 1) % 64);
                    cand[4] = 2 + ((maxab + 61) % 64);
                }
            } else if (a > INTRA_DC || b > INTRA_DC) {
                cand[0] = maxab;
                cand[1] = 2 + ((maxab + 61 ) % 64);
                cand[2] = 2 + ((maxab - 1) % 64);
                cand[3] = 2 + ((maxab + 60 ) % 64);
                cand[4] = 2 + (maxab % 64);
            } else {
                cand[0] = INTRA_DC;
                cand[1] = INTRA_VERT;
                cand[2] = INTRA_HORZ;
                cand[3] = INTRA_VERT - 4;
                cand[4] = INTRA_VERT + 4;
            }
        }
        if (intra_luma_mpm_flag) {
            pred = cand[intra_luma_mpm_idx];
        } else {
            qsort(cand, FF_ARRAY_ELEMS(cand), sizeof(cand[0]), less);
            pred = intra_luma_mpm_remainder + 1;
            for (int i = 0; i < FF_ARRAY_ELEMS(cand); i++) {
                if (pred >= cand[i])
                    pred++;
            }
        }
    }
    return pred;
}

static int lfnst_idx_decode(VVCLocalContext *lc)
{
    CodingUnit  *cu             = lc->cu;
    const VVCTreeType tree_type = cu->tree_type;
    const VVCSPS *sps           = lc->fc->ps.sps;
    const int cb_width          = cu->cb_width;
    const int cb_height         = cu->cb_height;
    const TransformUnit  *tu    = cu->tus.head;
    int lfnst_width, lfnst_height, min_lfnst;
    int lfnst_idx = 0;

    memset(cu->apply_lfnst_flag, 0, sizeof(cu->apply_lfnst_flag));

    if (!sps->r->sps_lfnst_enabled_flag || cu->pred_mode != MODE_INTRA || FFMAX(cb_width, cb_height) > sps->max_tb_size_y)
        return 0;

    while (tu) {
        for (int j = 0; j < tu->nb_tbs; j++) {
            const TransformBlock *tb = tu->tbs + j;
            if (tu->coded_flag[tb->c_idx] && tb->ts)
                return 0;
        }
        tu = tu->next;
    }

    if (tree_type == DUAL_TREE_CHROMA) {
        lfnst_width  = cb_width  >> sps->hshift[1];
        lfnst_height = cb_height >> sps->vshift[1];
    } else {
        const int vs = cu->isp_split_type == ISP_VER_SPLIT;
        const int hs = cu->isp_split_type == ISP_HOR_SPLIT;
        lfnst_width = vs ? cb_width / cu->num_intra_subpartitions : cb_width;
        lfnst_height = hs ? cb_height / cu->num_intra_subpartitions : cb_height;
    }
    min_lfnst = FFMIN(lfnst_width, lfnst_height);
    if (tree_type != DUAL_TREE_CHROMA && cu->intra_mip_flag && min_lfnst < 16)
        return 0;

    if (min_lfnst >= 4) {
        if ((cu->isp_split_type != ISP_NO_SPLIT || !lc->parse.lfnst_dc_only) && lc->parse.lfnst_zero_out_sig_coeff_flag)
            lfnst_idx = ff_vvc_lfnst_idx(lc, tree_type != SINGLE_TREE);
    }

    if (lfnst_idx) {
        cu->apply_lfnst_flag[LUMA] = tree_type != DUAL_TREE_CHROMA;
        cu->apply_lfnst_flag[CB] = cu->apply_lfnst_flag[CR] = tree_type == DUAL_TREE_CHROMA;
    }

    return lfnst_idx;
}

static MtsIdx mts_idx_decode(VVCLocalContext *lc)
{
    const CodingUnit *cu    = lc->cu;
    const VVCSPS     *sps   = lc->fc->ps.sps;
    const int cb_width      = cu->cb_width;
    const int cb_height     = cu->cb_height;
    const uint8_t transform_skip_flag = cu->tus.head->tbs[0].ts; //fix me
    int mts_idx = MTS_DCT2_DCT2;
    if (cu->tree_type != DUAL_TREE_CHROMA && !cu->lfnst_idx &&
        !transform_skip_flag && FFMAX(cb_width, cb_height) <= 32 &&
        cu->isp_split_type == ISP_NO_SPLIT && !cu->sbt_flag &&
        lc->parse.mts_zero_out_sig_coeff_flag && !lc->parse.mts_dc_only) {
        if ((cu->pred_mode == MODE_INTER && sps->r->sps_explicit_mts_inter_enabled_flag) ||
            (cu->pred_mode == MODE_INTRA && sps->r->sps_explicit_mts_intra_enabled_flag)) {
            mts_idx = ff_vvc_mts_idx(lc);
        }
    }

    return mts_idx;
}

static enum IntraPredMode derive_center_luma_intra_pred_mode(const VVCFrameContext *fc, const VVCSPS *sps, const VVCPPS *pps, const CodingUnit *cu)
{
    const int x_center            = (cu->x0 + cu->cb_width / 2) >> sps->min_cb_log2_size_y;
    const int y_center            = (cu->y0 + cu->cb_height / 2) >> sps->min_cb_log2_size_y;
    const int min_cb_width        = pps->min_cb_width;
    const int intra_mip_flag      = SAMPLE_CTB(fc->tab.imf, x_center, y_center);
    const int cu_pred_mode        = SAMPLE_CTB(fc->tab.cpm[0], x_center, y_center);
    const int intra_pred_mode_y   = SAMPLE_CTB(fc->tab.ipm, x_center, y_center);

    if (intra_mip_flag) {
        if (cu->tree_type == SINGLE_TREE && sps->r->sps_chroma_format_idc == CHROMA_FORMAT_444)
            return INTRA_INVALID;
        return INTRA_PLANAR;
    }
    if (cu_pred_mode == MODE_IBC || cu_pred_mode == MODE_PLT)
        return INTRA_DC;
    return intra_pred_mode_y;
}

static void derive_chroma_intra_pred_mode(VVCLocalContext *lc,
    const int cclm_mode_flag, const int cclm_mode_idx, const int intra_chroma_pred_mode)
{
    const VVCFrameContext *fc   = lc->fc;
    CodingUnit *cu              = lc->cu;
    const VVCSPS *sps           = fc->ps.sps;
    const VVCPPS *pps           = fc->ps.pps;
    const int x_cb              = cu->x0 >> sps->min_cb_log2_size_y;
    const int y_cb              = cu->y0 >> sps->min_cb_log2_size_y;
    const int min_cb_width      = pps->min_cb_width;
    const int intra_mip_flag    = SAMPLE_CTB(fc->tab.imf, x_cb, y_cb);
    enum IntraPredMode luma_intra_pred_mode = SAMPLE_CTB(fc->tab.ipm, x_cb, y_cb);

    if (cu->tree_type == SINGLE_TREE && sps->r->sps_chroma_format_idc == CHROMA_FORMAT_444 &&
        (intra_chroma_pred_mode == 4 || cu->act_enabled_flag) && intra_mip_flag) {
        cu->mip_chroma_direct_flag = 1;
        cu->intra_pred_mode_c = luma_intra_pred_mode;
        return;
    }
    luma_intra_pred_mode = derive_center_luma_intra_pred_mode(fc, sps, pps, cu);

    if (cu->act_enabled_flag) {
        cu->intra_pred_mode_c = luma_intra_pred_mode;
        return;
    }
    if (cclm_mode_flag) {
        cu->intra_pred_mode_c = INTRA_LT_CCLM + cclm_mode_idx;
    } else if (intra_chroma_pred_mode == 4){
        cu->intra_pred_mode_c = luma_intra_pred_mode;
    } else {
        const static IntraPredMode pred_mode_c[][4 + 1] = {
            {INTRA_VDIAG, INTRA_PLANAR, INTRA_PLANAR, INTRA_PLANAR, INTRA_PLANAR},
            {INTRA_VERT,  INTRA_VDIAG,  INTRA_VERT,   INTRA_VERT,   INTRA_VERT},
            {INTRA_HORZ,  INTRA_HORZ,   INTRA_VDIAG,  INTRA_HORZ,   INTRA_HORZ},
            {INTRA_DC,    INTRA_DC,     INTRA_DC,     INTRA_VDIAG,  INTRA_DC},
        };
        const int modes[4] = {INTRA_PLANAR, INTRA_VERT, INTRA_HORZ, INTRA_DC};
        int idx;

        // This workaround is necessary to have 4:4:4 video decode correctly
        // See VVC ticket https://jvet.hhi.fraunhofer.de/trac/vvc/ticket/1602
        // and VTM source https://vcgit.hhi.fraunhofer.de/jvet/VVCSoftware_VTM/-/blob/master/source/Lib/CommonLib/UnitTools.cpp#L736
        if (cu->tree_type == SINGLE_TREE && sps->r->sps_chroma_format_idc == CHROMA_FORMAT_444 && intra_mip_flag) {
            idx = 4;
        } else {
            for (idx = 0; idx < FF_ARRAY_ELEMS(modes); idx++) {
                if (modes[idx] == luma_intra_pred_mode)
                    break;
            }
        }

        cu->intra_pred_mode_c = pred_mode_c[intra_chroma_pred_mode][idx];
    }
    if (sps->r->sps_chroma_format_idc == CHROMA_FORMAT_422 && cu->intra_pred_mode_c <= INTRA_VDIAG) {
        const static int mode_map_422[INTRA_VDIAG + 1] = {
             0,  1, 61, 62, 63, 64, 65, 66,  2,  3,  5,  6,  8, 10, 12, 13,
            14, 16, 18, 20, 22, 23, 24, 26, 28, 30, 31, 33, 34, 35, 36, 37,
            38, 39, 40, 41, 41, 42, 43, 43, 44, 44, 45, 45, 46, 47, 48, 48,
            49, 49, 50, 51, 51, 52, 52, 53, 54, 55, 55, 56, 56, 57, 57, 58,
            59, 59, 60,
        };
        cu->intra_pred_mode_c = mode_map_422[cu->intra_pred_mode_c];
    }
}

static av_always_inline uint8_t pack_mip_info(int intra_mip_flag,
    int intra_mip_transposed_flag, int intra_mip_mode)
{
    return (intra_mip_mode << 2) | (intra_mip_transposed_flag << 1) | intra_mip_flag;
}

static void intra_luma_pred_modes(VVCLocalContext *lc)
{
    VVCFrameContext *fc             = lc->fc;
    const VVCSPS *sps               = fc->ps.sps;
    const VVCPPS *pps               = fc->ps.pps;
    CodingUnit *cu                  = lc->cu;
    const int log2_min_cb_size      = sps->min_cb_log2_size_y;
    const int x0                    = cu->x0;
    const int y0                    = cu->y0;
    const int x_cb                  = x0 >> log2_min_cb_size;
    const int y_cb                  = y0 >> log2_min_cb_size;
    const int cb_width              = cu->cb_width;
    const int cb_height             = cu->cb_height;

    cu->intra_luma_ref_idx  = 0;
    if (sps->r->sps_bdpcm_enabled_flag && cb_width <= sps->max_ts_size && cb_height <= sps->max_ts_size)
        cu->bdpcm_flag[LUMA] = ff_vvc_intra_bdpcm_luma_flag(lc);
    if (cu->bdpcm_flag[LUMA]) {
        cu->intra_pred_mode_y = ff_vvc_intra_bdpcm_luma_dir_flag(lc) ? INTRA_VERT : INTRA_HORZ;
    } else {
        if (sps->r->sps_mip_enabled_flag)
            cu->intra_mip_flag = ff_vvc_intra_mip_flag(lc, fc->tab.imf);
        if (cu->intra_mip_flag) {
            int intra_mip_transposed_flag = ff_vvc_intra_mip_transposed_flag(lc);
            int intra_mip_mode = ff_vvc_intra_mip_mode(lc);
            int x = y_cb * pps->min_cb_width + x_cb;
            for (int y = 0; y < (cb_height>>log2_min_cb_size); y++) {
                int width = cb_width>>log2_min_cb_size;
                const uint8_t mip_info = pack_mip_info(cu->intra_mip_flag,
                        intra_mip_transposed_flag, intra_mip_mode);
                memset(&fc->tab.imf[x], mip_info, width);
                x += pps->min_cb_width;
            }
            cu->intra_pred_mode_y = intra_mip_mode;
        } else {
            int intra_subpartitions_mode_flag = 0;
            if (sps->r->sps_mrl_enabled_flag && ((y0 % sps->ctb_size_y) > 0))
                cu->intra_luma_ref_idx = ff_vvc_intra_luma_ref_idx(lc);
            if (sps->r->sps_isp_enabled_flag && !cu->intra_luma_ref_idx &&
                (cb_width <= sps->max_tb_size_y && cb_height <= sps->max_tb_size_y) &&
                (cb_width * cb_height > MIN_TU_SIZE * MIN_TU_SIZE) &&
                !cu->act_enabled_flag)
                intra_subpartitions_mode_flag = ff_vvc_intra_subpartitions_mode_flag(lc);
            if (!(x0 & 63) && !(y0 & 63))
                TAB_ISPMF(fc, x0, y0) = intra_subpartitions_mode_flag;
            cu->isp_split_type = ff_vvc_isp_split_type(lc, intra_subpartitions_mode_flag);
            cu->num_intra_subpartitions = get_num_intra_subpartitions(cu->isp_split_type, cb_width, cb_height);
            cu->intra_pred_mode_y = luma_intra_pred_mode(lc, intra_subpartitions_mode_flag);
        }
    }
    set_cb_tab(lc, fc->tab.ipm, cu->intra_pred_mode_y);
}

static void intra_chroma_pred_modes(VVCLocalContext *lc)
{
    const VVCSPS *sps          = lc->fc->ps.sps;
    CodingUnit *cu             = lc->cu;
    const int hs               = sps->hshift[CHROMA];
    const int vs               = sps->vshift[CHROMA];
    int cclm_mode_flag         = 0;
    int cclm_mode_idx          = 0;
    int intra_chroma_pred_mode = 0;

    if (!cu->act_enabled_flag) {
        cu->mip_chroma_direct_flag = 0;
        if (sps->r->sps_bdpcm_enabled_flag &&
            (cu->cb_width  >> hs) <= sps->max_ts_size &&
            (cu->cb_height >> vs) <= sps->max_ts_size) {
            cu->bdpcm_flag[CB] = cu->bdpcm_flag[CR] = ff_vvc_intra_bdpcm_chroma_flag(lc);
        }
        if (cu->bdpcm_flag[CHROMA]) {
            cu->intra_pred_mode_c = ff_vvc_intra_bdpcm_chroma_dir_flag(lc) ? INTRA_VERT : INTRA_HORZ;
        } else {
            const int cclm_enabled = get_cclm_enabled(lc, cu->x0, cu->y0);

            if (cclm_enabled)
                cclm_mode_flag = ff_vvc_cclm_mode_flag(lc);

            if (cclm_mode_flag)
                cclm_mode_idx = ff_vvc_cclm_mode_idx(lc);
            else
                intra_chroma_pred_mode = ff_vvc_intra_chroma_pred_mode(lc);
        }
    }

    if (!cu->bdpcm_flag[CHROMA])
        derive_chroma_intra_pred_mode(lc, cclm_mode_flag, cclm_mode_idx, intra_chroma_pred_mode);
}

static PredMode pred_mode_decode(VVCLocalContext *lc,
                                 const VVCTreeType tree_type,
                                 const VVCModeType mode_type)
{
    const VVCFrameContext *fc       = lc->fc;
    CodingUnit *cu                  = lc->cu;
    const VVCSPS *sps               = fc->ps.sps;
    const H266RawSliceHeader *rsh   = lc->sc->sh.r;
    const int ch_type               = tree_type == DUAL_TREE_CHROMA ? 1 : 0;
    const int is_4x4                = cu->cb_width == 4 && cu->cb_height == 4;
    const int is_128                = cu->cb_width == 128 || cu->cb_height == 128;
    const int hs                    = sps->hshift[CHROMA];
    const int vs                    = sps->vshift[CHROMA];
    int pred_mode_flag;
    int pred_mode_ibc_flag;
    PredMode pred_mode;

    cu->skip_flag = 0;
    if (!IS_I(rsh) || sps->r->sps_ibc_enabled_flag) {
        if (tree_type != DUAL_TREE_CHROMA &&
            ((!is_4x4 && mode_type != MODE_TYPE_INTRA) ||
            (sps->r->sps_ibc_enabled_flag && !is_128))) {
            cu->skip_flag = ff_vvc_cu_skip_flag(lc, fc->tab.skip);
        }

        if (is_4x4 || mode_type == MODE_TYPE_INTRA || IS_I(rsh)) {
            pred_mode_flag = 1;
        } else if (mode_type == MODE_TYPE_INTER || cu->skip_flag) {
            pred_mode_flag = 0;
        } else  {
            pred_mode_flag = ff_vvc_pred_mode_flag(lc, ch_type);
        }
        pred_mode = pred_mode_flag ? MODE_INTRA : MODE_INTER;

        if (((IS_I(rsh) && !cu->skip_flag) ||
            (!IS_I(rsh) && (pred_mode != MODE_INTRA ||
            ((is_4x4 || mode_type == MODE_TYPE_INTRA) && !cu->skip_flag)))) &&
            !is_128 && mode_type != MODE_TYPE_INTER && sps->r->sps_ibc_enabled_flag &&
            tree_type != DUAL_TREE_CHROMA) {
            pred_mode_ibc_flag = ff_vvc_pred_mode_ibc_flag(lc, ch_type);
        } else if (cu->skip_flag && (is_4x4 || mode_type == MODE_TYPE_INTRA)) {
            pred_mode_ibc_flag = 1;
        } else if (is_128 || mode_type == MODE_TYPE_INTER || tree_type == DUAL_TREE_CHROMA) {
            pred_mode_ibc_flag = 0;
        } else {
            pred_mode_ibc_flag = (IS_I(rsh)) ? sps->r->sps_ibc_enabled_flag : 0;
        }
        if (pred_mode_ibc_flag)
            pred_mode = MODE_IBC;
    } else {
        pred_mode = MODE_INTRA;
    }

    if (pred_mode == MODE_INTRA && sps->r->sps_palette_enabled_flag && !is_128 && !cu->skip_flag &&
        mode_type != MODE_TYPE_INTER && ((cu->cb_width * cu->cb_height) >
            (tree_type != DUAL_TREE_CHROMA ? 16 : (16 << hs << vs))) &&
        (mode_type != MODE_TYPE_INTRA || tree_type != DUAL_TREE_CHROMA)) {
        if (ff_vvc_pred_mode_plt_flag(lc))
            pred_mode = MODE_PLT;
    }

    set_cb_tab(lc, fc->tab.cpm[cu->ch_type], pred_mode);
    if (tree_type == SINGLE_TREE)
        set_cb_tab(lc, fc->tab.cpm[CHROMA], pred_mode);

    return pred_mode;
}

static void sbt_info(VVCLocalContext *lc, const VVCSPS *sps)
{
    CodingUnit *cu      = lc->cu;
    const int cb_width  = cu->cb_width;
    const int cb_height = cu->cb_height;

    if (cu->pred_mode == MODE_INTER && sps->r->sps_sbt_enabled_flag && !cu->ciip_flag
        && cb_width <= sps->max_tb_size_y && cb_height <= sps->max_tb_size_y) {
        const int sbt_ver_h = cb_width  >= 8;
        const int sbt_hor_h = cb_height >= 8;
        cu->sbt_flag = 0;
        if (sbt_ver_h || sbt_hor_h)
            cu->sbt_flag = ff_vvc_sbt_flag(lc);
        if (cu->sbt_flag) {
            const int sbt_ver_q = cb_width  >= 16;
            const int sbt_hor_q = cb_height >= 16;
            int cu_sbt_quad_flag = 0;

            if ((sbt_ver_h || sbt_hor_h) && (sbt_ver_q || sbt_hor_q))
                cu_sbt_quad_flag = ff_vvc_sbt_quad_flag(lc);
            if (cu_sbt_quad_flag) {
                cu->sbt_horizontal_flag = sbt_hor_q;
                if (sbt_ver_q && sbt_hor_q)
                    cu->sbt_horizontal_flag = ff_vvc_sbt_horizontal_flag(lc);
            } else {
                cu->sbt_horizontal_flag = sbt_hor_h;
                if (sbt_ver_h && sbt_hor_h)
                    cu->sbt_horizontal_flag = ff_vvc_sbt_horizontal_flag(lc);
            }
            cu->sbt_pos_flag = ff_vvc_sbt_pos_flag(lc);

            {
                const int sbt_min = cu_sbt_quad_flag ? 1 : 2;
                lc->parse.sbt_num_fourths_tb0 = cu->sbt_pos_flag ? (4 - sbt_min) : sbt_min;
            }
        }
    }
}

static int skipped_transform_tree_unit(VVCLocalContext *lc)
{
    const H266RawSPS *rsps = lc->fc->ps.sps->r;
    const CodingUnit *cu   = lc->cu;
    int ret;

    if (cu->tree_type != DUAL_TREE_CHROMA)
        set_qp_y(lc, cu->x0, cu->y0, 0);
    if (rsps->sps_chroma_format_idc && cu->tree_type != DUAL_TREE_LUMA)
        set_qp_c(lc);
    ret = skipped_transform_tree(lc, cu->x0, cu->y0, cu->cb_width, cu->cb_height);
    if (ret < 0)
        return ret;
    return 0;
}

static void set_cb_pos(const VVCFrameContext *fc, const CodingUnit *cu)
{
    const VVCSPS *sps           = fc->ps.sps;
    const VVCPPS *pps           = fc->ps.pps;
    const int log2_min_cb_size  = sps->min_cb_log2_size_y;
    const int x_cb              = cu->x0 >> log2_min_cb_size;
    const int y_cb              = cu->y0 >> log2_min_cb_size;
    const int ch_type           = cu->ch_type;
    int x, y;

    x = y_cb * pps->min_cb_width + x_cb;
    for (y = 0; y < (cu->cb_height >> log2_min_cb_size); y++) {
        const int width = cu->cb_width >> log2_min_cb_size;

        for (int i = 0; i < width; i++) {
            fc->tab.cb_pos_x[ch_type][x + i] = cu->x0;
            fc->tab.cb_pos_y[ch_type][x + i] = cu->y0;
        }
        memset(&fc->tab.cb_width[ch_type][x], cu->cb_width, width);
        memset(&fc->tab.cb_height[ch_type][x], cu->cb_height, width);
        memset(&fc->tab.cqt_depth[ch_type][x], cu->cqt_depth, width);

        x += pps->min_cb_width;
    }
}

static CodingUnit* alloc_cu(VVCLocalContext *lc, const int x0, const int y0)
{
    VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps   = fc->ps.sps;
    const VVCPPS *pps   = fc->ps.pps;
    const int rx        = x0 >> sps->ctb_log2_size_y;
    const int ry        = y0 >> sps->ctb_log2_size_y;
    CodingUnit **cus    = fc->tab.cus + ry * pps->ctb_width + rx;
    CodingUnit *cu      = av_refstruct_pool_get(fc->cu_pool);

    if (!cu)
        return NULL;
    cu->next = NULL;

    if (lc->cu)
        lc->cu->next = cu;
    else
        *cus = cu;
    lc->cu = cu;

    return cu;
}

static CodingUnit* add_cu(VVCLocalContext *lc, const int x0, const int y0,
    const int cb_width, const int cb_height, const int cqt_depth, const VVCTreeType tree_type)
{
    VVCFrameContext *fc = lc->fc;
    const int ch_type   = tree_type == DUAL_TREE_CHROMA ? 1 : 0;
    CodingUnit *cu      = alloc_cu(lc, x0, y0);

    if (!cu)
        return NULL;

    memset(&cu->pu, 0, sizeof(cu->pu));

    lc->parse.prev_tu_cbf_y = 0;

    cu->sbt_flag = 0;
    cu->act_enabled_flag = 0;

    cu->tree_type = tree_type;
    cu->x0 = x0;
    cu->y0 = y0;
    cu->cb_width = cb_width;
    cu->cb_height = cb_height;
    cu->ch_type = ch_type;
    cu->cqt_depth = cqt_depth;
    cu->tus.head = cu->tus.tail = NULL;
    cu->bdpcm_flag[LUMA] = cu->bdpcm_flag[CB] = cu->bdpcm_flag[CR] = 0;
    cu->isp_split_type = ISP_NO_SPLIT;
    cu->intra_mip_flag = 0;
    cu->ciip_flag = 0;
    cu->coded_flag = 1;
    cu->num_intra_subpartitions = 1;
    cu->pu.dmvr_flag = 0;

    set_cb_pos(fc, cu);
    return cu;
}

static void set_cu_tabs(const VVCLocalContext *lc, const CodingUnit *cu)
{
    const VVCFrameContext *fc = lc->fc;
    const PredictionUnit *pu  = &cu->pu;
    const TransformUnit *tu   = cu->tus.head;

    set_cb_tab(lc, fc->tab.mmi, pu->mi.motion_model_idc);
    set_cb_tab(lc, fc->tab.msf, pu->merge_subblock_flag);
    if (cu->tree_type != DUAL_TREE_CHROMA) {
        set_cb_tab(lc, fc->tab.skip, cu->skip_flag);
        set_cb_tab(lc, fc->tab.pcmf[LUMA], cu->bdpcm_flag[LUMA]);
    }
    if (cu->tree_type != DUAL_TREE_LUMA)
        set_cb_tab(lc, fc->tab.pcmf[CHROMA], cu->bdpcm_flag[CHROMA]);

    while (tu) {
          for (int j = 0; j < tu->nb_tbs; j++) {
            const TransformBlock *tb = tu->tbs + j;
            if (tb->c_idx != LUMA)
                set_qp_c_tab(lc, tu, tb);
        }
        tu = tu->next;
    }
}

//8.5.2.7 Derivation process for merge motion vector difference
static void derive_mmvd(const VVCLocalContext *lc, MvField *mvf, const Mv *mmvd_offset)
{
    const SliceContext *sc  = lc->sc;
    Mv mmvd[2];

    if (mvf->pred_flag == PF_BI) {
        const RefPicList *rpl = sc->rpl;
        const int poc = lc->fc->ps.ph.poc;
        const int diff[] = {
            poc - rpl[L0].refs[mvf->ref_idx[L0]].poc,
            poc - rpl[L1].refs[mvf->ref_idx[L1]].poc
        };
        const int sign = FFSIGN(diff[0]) != FFSIGN(diff[1]);

        if (diff[0] == diff[1]) {
            mmvd[1] = mmvd[0] = *mmvd_offset;
        }
        else {
            const int i = FFABS(diff[0]) < FFABS(diff[1]);
            const int o = !i;
            mmvd[i] = *mmvd_offset;
            if (!rpl[L0].refs[mvf->ref_idx[L0]].is_lt && !rpl[L1].refs[mvf->ref_idx[L1]].is_lt) {
                ff_vvc_mv_scale(&mmvd[o], mmvd_offset, diff[i], diff[o]);
            }
            else {
                mmvd[o].x = sign ? -mmvd[i].x : mmvd[i].x;
                mmvd[o].y = sign ? -mmvd[i].y : mmvd[i].y;
            }
        }
        mvf->mv[0].x += mmvd[0].x;
        mvf->mv[0].y += mmvd[0].y;
        mvf->mv[1].x += mmvd[1].x;
        mvf->mv[1].y += mmvd[1].y;
    } else {
        const int idx = mvf->pred_flag - PF_L0;
        mvf->mv[idx].x += mmvd_offset->x;
        mvf->mv[idx].y += mmvd_offset->y;
    }

}

static void mvf_to_mi(const MvField *mvf, MotionInfo *mi)
{
    mi->pred_flag = mvf->pred_flag;
    mi->bcw_idx = mvf->bcw_idx;
    mi->hpel_if_idx = mvf->hpel_if_idx;
    for (int i = 0; i < 2; i++) {
        const PredFlag mask = i + 1;
        if (mvf->pred_flag & mask) {
            mi->mv[i][0] = mvf->mv[i];
            mi->ref_idx[i] = mvf->ref_idx[i];
        }
    }
}

static void mv_merge_refine_pred_flag(MvField *mvf, const int width, const int height)
{
    if (mvf->pred_flag == PF_BI && (width + height) == 12) {
        mvf->pred_flag = PF_L0;
        mvf->bcw_idx = 0;
    }
}

// subblock-based inter prediction data
static void merge_data_subblock(VVCLocalContext *lc)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCPH  *ph            = &fc->ps.ph;
    CodingUnit* cu              = lc->cu;
    PredictionUnit *pu          = &cu->pu;
    int merge_subblock_idx      = 0;

    if (ph->max_num_subblock_merge_cand > 1) {
        merge_subblock_idx = ff_vvc_merge_subblock_idx(lc, ph->max_num_subblock_merge_cand);
    }
    ff_vvc_sb_mv_merge_mode(lc, merge_subblock_idx, pu);
}

static void merge_data_regular(VVCLocalContext *lc)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCSPS *sps           = fc->ps.sps;
    const VVCPH  *ph            = &fc->ps.ph;
    const CodingUnit* cu        = lc->cu;
    PredictionUnit *pu          = &lc->cu->pu;
    int merge_idx               = 0;
    Mv mmvd_offset;
    MvField mvf;

    if (sps->r->sps_mmvd_enabled_flag)
        pu->mmvd_merge_flag = ff_vvc_mmvd_merge_flag(lc);
    if (pu->mmvd_merge_flag) {
        int mmvd_cand_flag = 0;
        if (sps->max_num_merge_cand > 1)
            mmvd_cand_flag = ff_vvc_mmvd_cand_flag(lc);
        ff_vvc_mmvd_offset_coding(lc, &mmvd_offset, ph->r->ph_mmvd_fullpel_only_flag);
        merge_idx = mmvd_cand_flag;
    } else if (sps->max_num_merge_cand > 1) {
        merge_idx = ff_vvc_merge_idx(lc);
    }
    ff_vvc_luma_mv_merge_mode(lc, merge_idx, 0, &mvf);
    if (pu->mmvd_merge_flag)
        derive_mmvd(lc, &mvf, &mmvd_offset);
    mv_merge_refine_pred_flag(&mvf, cu->cb_width, cu->cb_height);
    ff_vvc_store_mvf(lc, &mvf);
    mvf_to_mi(&mvf, &pu->mi);
}

static int ciip_flag_decode(VVCLocalContext *lc, const int ciip_avaiable, const int gpm_avaiable, const int is_128)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCSPS *sps           = fc->ps.sps;
    const CodingUnit *cu        = lc->cu;

    if (ciip_avaiable && gpm_avaiable)
        return ff_vvc_ciip_flag(lc);
    return sps->r->sps_ciip_enabled_flag && !cu->skip_flag &&
            !is_128 && (cu->cb_width * cu->cb_height >= 64);
}

static void merge_data_gpm(VVCLocalContext *lc)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCSPS *sps           = fc->ps.sps;
    PredictionUnit *pu          = &lc->cu->pu;
    int merge_gpm_idx[2];

    pu->merge_gpm_flag = 1;
    pu->gpm_partition_idx = ff_vvc_merge_gpm_partition_idx(lc);
    merge_gpm_idx[0] = ff_vvc_merge_gpm_idx(lc, 0);
    merge_gpm_idx[1] = 0;
    if (sps->max_num_gpm_merge_cand > 2)
        merge_gpm_idx[1] = ff_vvc_merge_gpm_idx(lc, 1);

    ff_vvc_luma_mv_merge_gpm(lc, merge_gpm_idx, pu->gpm_mv);
    ff_vvc_store_gpm_mvf(lc, pu);
}

static void merge_data_ciip(VVCLocalContext *lc)
{
    const VVCFrameContext* fc   = lc->fc;
    const VVCSPS* sps           = fc->ps.sps;
    CodingUnit *cu              = lc->cu;
    MotionInfo *mi              = &cu->pu.mi;
    int merge_idx               = 0;
    MvField mvf;

    if (sps->max_num_merge_cand > 1)
        merge_idx = ff_vvc_merge_idx(lc);
    ff_vvc_luma_mv_merge_mode(lc, merge_idx, 1, &mvf);
    mv_merge_refine_pred_flag(&mvf, cu->cb_width, cu->cb_height);
    ff_vvc_store_mvf(lc, &mvf);
    mvf_to_mi(&mvf, mi);
    cu->intra_pred_mode_y   = cu->intra_pred_mode_c = INTRA_PLANAR;
    cu->intra_luma_ref_idx  = 0;
    cu->intra_mip_flag      = 0;
}

// block-based inter prediction data
static void merge_data_block(VVCLocalContext *lc)
{
    const VVCFrameContext* fc       = lc->fc;
    const VVCSPS *sps               = fc->ps.sps;
    const H266RawSliceHeader *rsh   = lc->sc->sh.r;
    CodingUnit *cu                  = lc->cu;
    const int cb_width              = cu->cb_width;
    const int cb_height             = cu->cb_height;
    const int is_128                = cb_width == 128 || cb_height == 128;
    const int ciip_avaiable         = sps->r->sps_ciip_enabled_flag &&
        !cu->skip_flag && (cb_width * cb_height >= 64);
    const int gpm_avaiable          = sps->r->sps_gpm_enabled_flag && IS_B(rsh) &&
        (cb_width >= 8) && (cb_height >=8) &&
        (cb_width < 8 * cb_height) && (cb_height < 8 *cb_width);

    int regular_merge_flag = 1;

    if (!is_128 && (ciip_avaiable || gpm_avaiable))
        regular_merge_flag = ff_vvc_regular_merge_flag(lc, cu->skip_flag);
    if (regular_merge_flag) {
        merge_data_regular(lc);
    } else {
        cu->ciip_flag = ciip_flag_decode(lc, ciip_avaiable, gpm_avaiable, is_128);
        if (cu->ciip_flag)
            merge_data_ciip(lc);
        else
            merge_data_gpm(lc);
    }
}

static int merge_data_ibc(VVCLocalContext *lc)
{
    const VVCFrameContext* fc = lc->fc;
    const VVCSPS* sps         = fc->ps.sps;
    MotionInfo *mi            = &lc->cu->pu.mi;
    int merge_idx             = 0;
    int ret;

    mi->pred_flag = PF_IBC;

    if (sps->max_num_ibc_merge_cand > 1)
        merge_idx = ff_vvc_merge_idx(lc);

    ret = ff_vvc_luma_mv_merge_ibc(lc, merge_idx, &mi->mv[L0][0]);
    if (ret)
        return ret;
    ff_vvc_store_mv(lc, mi);

    return 0;
}

static int hls_merge_data(VVCLocalContext *lc)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCPH  *ph            = &fc->ps.ph;
    const CodingUnit *cu        = lc->cu;
    PredictionUnit *pu          = &lc->cu->pu;
    int ret;

    pu->merge_gpm_flag = 0;
    pu->mi.num_sb_x = pu->mi.num_sb_y = 1;
    if (cu->pred_mode == MODE_IBC) {
        ret = merge_data_ibc(lc);
        if (ret)
            return ret;
    } else {
        if (ph->max_num_subblock_merge_cand > 0 && cu->cb_width >= 8 && cu->cb_height >= 8)
            pu->merge_subblock_flag = ff_vvc_merge_subblock_flag(lc);
        if (pu->merge_subblock_flag)
            merge_data_subblock(lc);
        else
            merge_data_block(lc);
    }
    return 0;
}

static void hls_mvd_coding(VVCLocalContext *lc, Mv* mvd)
{
    int32_t mv[2];

    for (int i = 0; i < 2; i++) {
        mv[i] = ff_vvc_abs_mvd_greater0_flag(lc);
    }

    for (int i = 0; i < 2; i++) {
        if (mv[i])
            mv[i] += ff_vvc_abs_mvd_greater1_flag(lc);
    }

    for (int i = 0; i < 2; i++) {
        if (mv[i] > 0) {
            if (mv[i] == 2)
                mv[i] += ff_vvc_abs_mvd_minus2(lc);
            mv[i] = (1 - 2 * ff_vvc_mvd_sign_flag(lc)) * mv[i];
        }
    }
    mvd->x = mv[0];
    mvd->y = mv[1];
}

static int bcw_idx_decode(VVCLocalContext *lc, const MotionInfo *mi, const int cb_width, const int cb_height)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCSPS *sps           = fc->ps.sps;
    const VVCPPS *pps           = fc->ps.pps;
    const VVCPH  *ph            = &fc->ps.ph;
    const VVCSH *sh             = &lc->sc->sh;
    const PredWeightTable *w    = pps->r->pps_wp_info_in_ph_flag ? &ph->pwt : &sh->pwt;
    int bcw_idx                 = 0;

    if (sps->r->sps_bcw_enabled_flag && mi->pred_flag == PF_BI &&
        !w->weight_flag[L0][LUMA][mi->ref_idx[0]] &&
        !w->weight_flag[L1][LUMA][mi->ref_idx[1]] &&
        !w->weight_flag[L0][CHROMA][mi->ref_idx[0]] &&
        !w->weight_flag[L1][CHROMA][mi->ref_idx[1]] &&
        cb_width * cb_height >= 256) {
        bcw_idx = ff_vvc_bcw_idx(lc, ff_vvc_no_backward_pred_flag(lc));
    }
    return bcw_idx;
}

static int8_t ref_idx_decode(VVCLocalContext *lc, const VVCSH *sh, const int sym_mvd_flag, const int lx)
{
    const H266RawSliceHeader *rsh   = sh->r;
    int ref_idx                     = 0;

    if (rsh->num_ref_idx_active[lx] > 1 && !sym_mvd_flag)
        ref_idx = ff_vvc_ref_idx_lx(lc, rsh->num_ref_idx_active[lx]);
    else if (sym_mvd_flag)
        ref_idx = sh->ref_idx_sym[lx];
    return ref_idx;
}

static int mvds_decode(VVCLocalContext *lc, Mv mvds[2][MAX_CONTROL_POINTS],
    const int num_cp_mv, const int lx)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCPH *ph             = &fc->ps.ph;
    const PredictionUnit *pu    = &lc->cu->pu;
    const MotionInfo *mi        = &pu->mi;
    int has_no_zero_mvd         = 0;

    if (lx == L1 && ph->r->ph_mvd_l1_zero_flag && mi->pred_flag == PF_BI) {
        for (int j = 0; j < num_cp_mv; j++)
            AV_ZERO64(&mvds[lx][j]);
    } else {
        Mv *mvd0 = &mvds[lx][0];
        if (lx == L1 && pu->sym_mvd_flag) {
            mvd0->x = -mvds[L0][0].x;
            mvd0->y = -mvds[L0][0].y;
        } else {
            hls_mvd_coding(lc, mvd0);
        }
        has_no_zero_mvd |= (mvd0->x || mvd0->y);
        for (int j = 1; j < num_cp_mv; j++) {
            Mv *mvd = &mvds[lx][j];
            hls_mvd_coding(lc, mvd);
            mvd->x += mvd0->x;
            mvd->y += mvd0->y;
            has_no_zero_mvd |= (mvd->x || mvd->y);
        }
    }
    return has_no_zero_mvd;
}

static void mvp_add_difference(MotionInfo *mi, const int num_cp_mv,
    const Mv mvds[2][MAX_CONTROL_POINTS], const int amvr_shift)
{
    for (int i = 0; i < 2; i++) {
        const PredFlag mask = i + PF_L0;
        if (mi->pred_flag & mask) {
            for (int j = 0; j < num_cp_mv; j++) {
                const Mv *mvd = &mvds[i][j];
                mi->mv[i][j].x += mvd->x * (1 << amvr_shift);
                mi->mv[i][j].y += mvd->y * (1 << amvr_shift);
            }
        }
    }
}

static int mvp_data_ibc(VVCLocalContext *lc)
{
    const VVCFrameContext *fc = lc->fc;
    const CodingUnit *cu      = lc->cu;
    const PredictionUnit *pu  = &lc->cu->pu;
    const VVCSPS *sps         = fc->ps.sps;
    MotionInfo *mi            = &lc->cu->pu.mi;
    int mvp_l0_flag           = 0;
    int amvr_shift            = 4;
    Mv *mv                    = &mi->mv[L0][0];
    int ret;

    mi->pred_flag = PF_IBC;
    mi->num_sb_x  = 1;
    mi->num_sb_y  = 1;

    hls_mvd_coding(lc, mv);
    if (sps->max_num_ibc_merge_cand > 1)
        mvp_l0_flag = ff_vvc_mvp_lx_flag(lc);
    if (sps->r->sps_amvr_enabled_flag && (mv->x || mv->y))
        amvr_shift = ff_vvc_amvr_shift(lc, pu->inter_affine_flag, cu->pred_mode, 1);

    ret = ff_vvc_mvp_ibc(lc, mvp_l0_flag, amvr_shift, mv);
    if (ret)
        return ret;
    ff_vvc_store_mv(lc, mi);

    return 0;
}

static int mvp_data(VVCLocalContext *lc)
{
    const VVCFrameContext *fc       = lc->fc;
    const CodingUnit *cu            = lc->cu;
    PredictionUnit *pu              = &lc->cu->pu;
    const VVCSPS *sps               = fc->ps.sps;
    const VVCPH *ph                 = &fc->ps.ph;
    const VVCSH *sh                 = &lc->sc->sh;
    const H266RawSliceHeader *rsh   = sh->r;
    MotionInfo *mi                  = &pu->mi;
    const int cb_width              = cu->cb_width;
    const int cb_height             = cu->cb_height;

    int mvp_lx_flag[2] = {0};
    int cu_affine_type_flag = 0;
    int num_cp_mv;
    int amvr_enabled, has_no_zero_mvd = 0, amvr_shift;
    Mv mvds[2][MAX_CONTROL_POINTS];

    mi->pred_flag = ff_vvc_pred_flag(lc, IS_B(rsh));
    if (sps->r->sps_affine_enabled_flag && cb_width >= 16 && cb_height >= 16) {
        pu->inter_affine_flag = ff_vvc_inter_affine_flag(lc);
        set_cb_tab(lc, fc->tab.iaf, pu->inter_affine_flag);
        if (sps->r->sps_6param_affine_enabled_flag && pu->inter_affine_flag)
            cu_affine_type_flag = ff_vvc_cu_affine_type_flag(lc);
    }
    mi->motion_model_idc = pu->inter_affine_flag + cu_affine_type_flag;
    num_cp_mv = mi->motion_model_idc + 1;

    if (sps->r->sps_smvd_enabled_flag && !ph->r->ph_mvd_l1_zero_flag &&
        mi->pred_flag == PF_BI && !pu->inter_affine_flag &&
        sh->ref_idx_sym[0] > -1 && sh->ref_idx_sym[1] > -1)
        pu->sym_mvd_flag = ff_vvc_sym_mvd_flag(lc);

    for (int i = L0; i <= L1; i++) {
        const PredFlag pred_flag = PF_L0 + !i;
        if (mi->pred_flag != pred_flag) {
            mi->ref_idx[i] = ref_idx_decode(lc, sh, pu->sym_mvd_flag, i);
            has_no_zero_mvd |= mvds_decode(lc, mvds, num_cp_mv, i);
            mvp_lx_flag[i] = ff_vvc_mvp_lx_flag(lc);
        }
    }

    amvr_enabled = mi->motion_model_idc == MOTION_TRANSLATION ?
        sps->r->sps_amvr_enabled_flag : sps->r->sps_affine_amvr_enabled_flag;
    amvr_enabled &= has_no_zero_mvd;

    amvr_shift = ff_vvc_amvr_shift(lc, pu->inter_affine_flag, cu->pred_mode, amvr_enabled);

    mi->hpel_if_idx = amvr_shift == 3;
    mi->bcw_idx = bcw_idx_decode(lc, mi, cb_width, cb_height);

    if (mi->motion_model_idc)
        ff_vvc_affine_mvp(lc, mvp_lx_flag, amvr_shift, mi);
    else
        ff_vvc_mvp(lc, mvp_lx_flag, amvr_shift, mi);

    mvp_add_difference(mi, num_cp_mv, mvds, amvr_shift);

    if (mi->motion_model_idc)
        ff_vvc_store_sb_mvs(lc, pu);
    else
        ff_vvc_store_mv(lc, &pu->mi);

    return 0;
}

// derive bdofFlag from 8.5.6 Decoding process for inter blocks
// derive dmvr from 8.5.1 General decoding process for coding units coded in inter prediction mode
static void derive_dmvr_bdof_flag(const VVCLocalContext *lc, PredictionUnit *pu)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCPPS *pps           = fc->ps.pps;
    const VVCPH *ph             = &fc->ps.ph;
    const VVCSH *sh             = &lc->sc->sh;
    const int poc               = ph->poc;
    const MotionInfo *mi        = &pu->mi;
    const int8_t *ref_idx       = mi->ref_idx;
    const VVCRefPic *rp0        = &lc->sc->rpl[L0].refs[ref_idx[L0]];
    const VVCRefPic *rp1        = &lc->sc->rpl[L1].refs[ref_idx[L1]];
    const CodingUnit *cu        = lc->cu;
    const PredWeightTable *w    = pps->r->pps_wp_info_in_ph_flag ? &fc->ps.ph.pwt : &sh->pwt;

    pu->bdof_flag = 0;

    if (mi->pred_flag == PF_BI &&
        (poc - rp0->poc == rp1->poc - poc) &&
        !rp0->is_lt && !rp1->is_lt &&
        !cu->ciip_flag &&
        !mi->bcw_idx &&
        !w->weight_flag[L0][LUMA][ref_idx[L0]] && !w->weight_flag[L1][LUMA][ref_idx[L1]] &&
        !w->weight_flag[L0][CHROMA][ref_idx[L0]] && !w->weight_flag[L1][CHROMA][ref_idx[L1]] &&
        cu->cb_width >= 8 && cu->cb_height >= 8 &&
        (cu->cb_width * cu->cb_height >= 128) &&
        !rp0->is_scaled && !rp1->is_scaled) {
        if (!ph->r->ph_bdof_disabled_flag &&
            mi->motion_model_idc == MOTION_TRANSLATION &&
            !pu->merge_subblock_flag &&
            !pu->sym_mvd_flag)
            pu->bdof_flag = 1;
        if (!ph->r->ph_dmvr_disabled_flag &&
            pu->general_merge_flag &&
            !pu->mmvd_merge_flag)
            pu->dmvr_flag = 1;
    }
}

// part of 8.5.1 General decoding process for coding units coded in inter prediction mode
static void refine_regular_subblock(const VVCLocalContext *lc)
{
    const CodingUnit *cu    = lc->cu;
    PredictionUnit *pu      = &lc->cu->pu;

    derive_dmvr_bdof_flag(lc, pu);
    if (pu->dmvr_flag || pu->bdof_flag) {
        pu->mi.num_sb_x = (cu->cb_width > 16) ? (cu->cb_width >> 4) : 1;
        pu->mi.num_sb_y = (cu->cb_height > 16) ? (cu->cb_height >> 4) : 1;
    }
}

static void fill_dmvr_info(const VVCLocalContext *lc)
{
    const VVCFrameContext *fc = lc->fc;
    const CodingUnit *cu      = lc->cu;

    if (cu->pred_mode == MODE_IBC || cu->pred_mode == MODE_PLT) {
        ff_vvc_set_intra_mvf(lc, true, cu->pred_mode == MODE_IBC ? PF_IBC : PF_PLT, false);
    } else {
        const VVCPPS *pps = fc->ps.pps;
        const int w       = cu->cb_width >> MIN_PU_LOG2;

        for (int y = cu->y0 >> MIN_PU_LOG2; y < (cu->y0 + cu->cb_height) >> MIN_PU_LOG2; y++) {
            const int idx = pps->min_pu_width * y + (cu->x0 >> MIN_PU_LOG2);
            const MvField *mvf = fc->tab.mvf + idx;
            MvField *dmvr_mvf  = fc->ref->tab_dmvr_mvf + idx;

            memcpy(dmvr_mvf, mvf, sizeof(MvField) * w);
        }
    }
}

static int inter_data(VVCLocalContext *lc)
{
    const CodingUnit *cu    = lc->cu;
    PredictionUnit *pu      = &lc->cu->pu;
    const MotionInfo *mi    = &pu->mi;
    int ret                 = 0;

    pu->general_merge_flag = 1;
    if (!cu->skip_flag)
        pu->general_merge_flag = ff_vvc_general_merge_flag(lc);

    if (pu->general_merge_flag) {
        ret = hls_merge_data(lc);
    } else if (cu->pred_mode == MODE_IBC) {
        ret = mvp_data_ibc(lc);
    } else {
        ret = mvp_data(lc);
    }

    if (ret)
        return ret;

    if (cu->pred_mode == MODE_IBC) {
        ff_vvc_update_hmvp(lc, mi);
    } else if (!pu->merge_gpm_flag && !pu->inter_affine_flag && !pu->merge_subblock_flag) {
        refine_regular_subblock(lc);
        ff_vvc_update_hmvp(lc, mi);
    }

    if (!pu->dmvr_flag)
        fill_dmvr_info(lc);
    return ret;
}

static TransformUnit* palette_add_tu(VVCLocalContext *lc, const int start, const int end, const VVCTreeType tree_type)
{
    CodingUnit   *cu  = lc->cu;
    const VVCSPS *sps = lc->fc->ps.sps;
    TransformUnit *tu = add_tu(lc->fc, cu, cu->x0, cu->y0, cu->cb_width, cu->cb_height);

    if (!tu)
        return NULL;

    for (int c = start; c < end; c++) {
        const int w = tu->width >> sps->hshift[c];
        const int h = tu->height >> sps->vshift[c];
        TransformBlock *tb = add_tb(tu, lc, tu->x0, tu->y0, w, h, c);
        if (c != CR)
            set_tb_size(lc->fc, tb);
    }

    for (int i = 0; i < FF_ARRAY_ELEMS(cu->plt); i++)
        cu->plt[i].size = 0;

    return tu;
}

static int palette_predicted(VVCLocalContext *lc, const bool local_dual_tree, int start, int end,
    bool *predictor_reused, const int predictor_size, const int max_entries)
{
    CodingUnit  *cu  = lc->cu;
    int nb_predicted = 0;

    if (local_dual_tree) {
        start = LUMA;
        end = VVC_MAX_SAMPLE_ARRAYS;
    }

    for (int i = 0; i < predictor_size && nb_predicted < max_entries; i++) {
        const int run = ff_vvc_palette_predictor_run(lc);
        if (run == 1)
            break;

        if (run > 1)
            i += run - 1;

        if (i >= predictor_size)
            return AVERROR_INVALIDDATA;

        predictor_reused[i] = true;
        for (int c = start; c < end; c++)
            cu->plt[c].entries[nb_predicted] = lc->ep->pp[c].entries[i];
        nb_predicted++;
    }

    for (int c = start; c < end; c++)
        cu->plt[c].size = nb_predicted;

    return 0;
}

static int palette_signaled(VVCLocalContext *lc, const bool local_dual_tree,
    const int start, const int end, const int max_entries)
{
    const VVCSPS *sps         = lc->fc->ps.sps;
    CodingUnit  *cu           = lc->cu;
    const int nb_predicted    = cu->plt[start].size;
    const int nb_signaled     = nb_predicted < max_entries ? ff_vvc_num_signalled_palette_entries(lc) : 0;
    const int size            = nb_predicted + nb_signaled;
    const bool dual_tree_luma = local_dual_tree && cu->tree_type == DUAL_TREE_LUMA;

    if (size > max_entries)
        return AVERROR_INVALIDDATA;

    for (int c = start; c < end; c++) {
        Palette *plt = cu->plt + c;
        for (int i = nb_predicted; i < size; i++) {
            plt->entries[i] = ff_vvc_new_palette_entries(lc, sps->bit_depth);
            if (dual_tree_luma) {
                plt[CB].entries[i] = 1 << (sps->bit_depth - 1);
                plt[CR].entries[i] = 1 << (sps->bit_depth - 1);
            }
        }
        plt->size = size;
    }

    return 0;
}

static void palette_update_predictor(VVCLocalContext *lc, const bool local_dual_tree, int start, int end,
    bool *predictor_reused, const int predictor_size)
{
    CodingUnit  *cu         = lc->cu;
    const int max_predictor = VVC_MAX_NUM_PALETTE_PREDICTOR_SIZE >> (cu->tree_type != SINGLE_TREE && !local_dual_tree);

    if (local_dual_tree) {
        start = LUMA;
        end = VVC_MAX_SAMPLE_ARRAYS;
    }

    for (int c = start; c < end; c++) {
        Palette *pp  = lc->ep->pp + c;
        Palette *plt = cu->plt + c;
        int i = cu->plt[start].size;;

        // copy unused predictors to the end of plt
        for (int j = 0; j < predictor_size && i < max_predictor; j++) {
            if (!predictor_reused[j]) {
                plt->entries[i] = pp->entries[j];
                i++;
            }
        }

        memcpy(pp->entries, plt->entries, i * sizeof(pp->entries[0]));
        pp->size = i;
    }
}

static void palette_qp(VVCLocalContext *lc, VVCTreeType tree_type, const bool escape_present)
{
    const VVCFrameContext *fc     = lc->fc;
    const VVCPPS *pps             = fc->ps.pps;
    const H266RawSliceHeader *rsh = lc->sc->sh.r;
    const CodingUnit *cu          = lc->cu;

    if (tree_type != DUAL_TREE_CHROMA) {
        const bool has_qp_delta = escape_present &&
            pps->r->pps_cu_qp_delta_enabled_flag && !lc->parse.is_cu_qp_delta_coded;
        set_qp_y(lc, cu->x0, cu->y0, has_qp_delta);
    }

    if (tree_type != DUAL_TREE_LUMA) {
        if (rsh->sh_cu_chroma_qp_offset_enabled_flag && !lc->parse.is_cu_chroma_qp_offset_coded)
            chroma_qp_offset_decode(lc, 0, 1);
        set_qp_c(lc);
    }
}

#define PALETTE_SET_PIXEL(xc, yc, pix)                              \
    do {                                                            \
        const int off = ((xc) >> hs) + ((yc) >> vs) * tb->tb_width; \
        if (sps->bit_depth == 8)                                    \
            u8[off] = pix;                                          \
        else                                                        \
            u16[off] = pix;                                         \
    } while (0)

#define PALETTE_INDEX(x, y) index[(y) * cu->cb_width + (x)]

// 6.5.3 Horizontal and vertical traverse scan order array initialization process
// The hTravScan and vTravScan tables require approximately 576 KB of memory.
// To save space, we use a macro to achieve the same functionality.
#define TRAV_COL(p, wlog, mask) ((p & mask) ^ (-((p >> wlog) & 1) & mask))
#define TRAV_ROW(p, hlog) (p >> hlog)
#define TRAV(trans, p, wlog, hlog, mask)  (trans ? TRAV_ROW((p), hlog) : TRAV_COL((p), wlog, mask))
#define TRAV_X(pos) TRAV(transpose, pos, wlog2, hlog2, wmask)
#define TRAV_Y(pos) TRAV(!transpose, pos, hlog2, wlog2, hmask)

static int palette_subblock_data(VVCLocalContext *lc,
    const int max_index, const int subset_id, const bool transpose,
    uint8_t *run_type, uint8_t *index, int *prev_run_pos, bool *adjust)
{
    const CodingUnit *cu = lc->cu;
    TransformUnit *tu    = cu->tus.head;
    const VVCSPS *sps    = lc->fc->ps.sps;
    const int min_pos    = subset_id << 4;
    const int max_pos    = FFMIN(min_pos + 16, cu->cb_width * cu->cb_height);
    const int wmask      = cu->cb_width  - 1;
    const int hmask      = cu->cb_height - 1;
    const int wlog2      = av_log2(cu->cb_width);
    const int hlog2      = av_log2(cu->cb_height);
    const uint8_t esc    = cu->plt[tu->tbs[0].c_idx].size;
    uint8_t run_copy[16] = { 0 };

    for (int i = min_pos; i < max_pos; i++) {
        const int xc = TRAV_X(i);
        const int yc = TRAV_Y(i);

        if (i > 0 && max_index > 0)
            run_copy[i - min_pos] = ff_vvc_run_copy_flag(lc, run_type[i - 1], *prev_run_pos, i);

        run_type[i] = 0;
        if (max_index > 0 && !run_copy[i - min_pos]) {
            if (((!transpose && yc > 0) || (transpose && xc > 0))
                && i > 0 && !run_type[i - 1]) {
                run_type[i] = ff_vvc_copy_above_palette_indices_flag(lc);
            }
            *prev_run_pos = i;
        } else if (i > 0) {
            run_type[i] = run_type[i - 1];
        }
    }

    for (int i = min_pos; i < max_pos; i++) {
        const int xc = TRAV_X(i);
        const int yc = TRAV_Y(i);
        const int prev_xc = i > 0 ? TRAV_X(i - 1) : 0;
        const int prev_yc = i > 0 ? TRAV_Y(i - 1) : 0;

        int idx = 0;
        if (max_index > 0 && !run_copy[i - min_pos] && !run_type[i]) {
            if (max_index - *adjust > 0)
                idx = ff_vvc_palette_idx_idc(lc, max_index, *adjust);
            if (i > 0) {
                const int ref_idx = !run_type[i - 1] ?
                    PALETTE_INDEX(prev_xc, prev_yc) : PALETTE_INDEX(xc - transpose, yc - !transpose);
                idx += (idx >= ref_idx);
            }
            *adjust = true;
        } else {
            idx = PALETTE_INDEX(prev_xc, prev_yc);
        }

        if (!run_type[i])
            PALETTE_INDEX(xc, yc) = idx;
        else
            PALETTE_INDEX(xc, yc) = PALETTE_INDEX(xc - transpose, yc - !transpose);
    }

    for (int c = 0; c < tu->nb_tbs; c++) {
        TransformBlock *tb = &tu->tbs[c];
        const Palette *plt = cu->plt + tb->c_idx;
        const int scale    = ff_vvc_palette_derive_scale(lc, tu, tb);
        const int hs       = sps->hshift[c];
        const int vs       = sps->vshift[c];
        uint8_t *u8        = (uint8_t *)tb->coeffs;
        uint16_t *u16      = (uint16_t *)tb->coeffs;

        for (int i = min_pos; i < max_pos; i++) {
            const int xc = TRAV_X(i);
            const int yc = TRAV_Y(i);
            if (!(xc & hs) && !(yc & vs)) {
                const int v = PALETTE_INDEX(xc, yc);
                if (v == esc) {
                    const int coeff = ff_vvc_palette_escape_val(lc);
                    const int pixel = av_clip_intp2(RSHIFT(coeff * scale, 6), sps->bit_depth);
                    PALETTE_SET_PIXEL(xc, yc, pixel);
                } else {
                    PALETTE_SET_PIXEL(xc, yc, plt->entries[v]);
                }
            }
        }
    }

    return 0;
}

static int hls_palette_coding(VVCLocalContext *lc, const VVCTreeType tree_type)
{
    const VVCFrameContext *fc     = lc->fc;
    const VVCSPS *sps             = fc->ps.sps;
    const H266RawSliceHeader *rsh = lc->sc->sh.r;
    CodingUnit *cu                = lc->cu;
    Palette *pp                   = lc->ep->pp;
    const int max_entries         = tree_type == SINGLE_TREE ? 31 : 15;
    const bool local_dual_tree    = tree_type != SINGLE_TREE &&
                                        (!IS_I(rsh) || (IS_I(rsh) && !sps->r->sps_qtbtt_dual_tree_intra_flag));
    bool escape_present           = false;
    bool transpose                = false;
    bool adjust                   = false;
    int max_index                 = 0;
    int prev_run_pos              = 0;

    int predictor_size, start, end, ret;
    bool reused[VVC_MAX_NUM_PALETTE_PREDICTOR_SIZE];
    uint8_t run_type[MAX_PALETTE_CU_SIZE * MAX_PALETTE_CU_SIZE];
    uint8_t index[MAX_PALETTE_CU_SIZE * MAX_PALETTE_CU_SIZE];

    ff_vvc_channel_range(&start, &end, tree_type, sps->r->sps_chroma_format_idc);

    if (!palette_add_tu(lc, start, end, tree_type))
        return AVERROR(ENOMEM);

    predictor_size = pp[start].size;
    memset(reused, 0, sizeof(reused[0]) * predictor_size);

    ret = palette_predicted(lc, local_dual_tree, start, end, reused, predictor_size, max_entries);
    if (ret < 0)
        return ret;

    ret = palette_signaled(lc, local_dual_tree, start, end, max_entries);
    if (ret < 0)
        return ret;

    palette_update_predictor(lc, local_dual_tree, start, end, reused, predictor_size);

    if (cu->plt[start].size > 0)
        escape_present = ff_vvc_palette_escape_val_present_flag(lc);

    max_index = cu->plt[start].size - 1 + escape_present;
    if (max_index > 0) {
        adjust = false;
        transpose = ff_vvc_palette_transpose_flag(lc);
    }

    palette_qp(lc, tree_type, escape_present);

    index[0] = 0;
    for (int i = 0; i <= (cu->cb_width * cu->cb_height - 1) >> 4; i++)
        palette_subblock_data(lc, max_index, i, transpose,
            run_type, index, &prev_run_pos, &adjust);

    return 0;
}

static int intra_data(VVCLocalContext *lc)
{
    const VVCSPS *sps              = lc->fc->ps.sps;
    const CodingUnit *cu           = lc->cu;
    const VVCTreeType tree_type    = cu->tree_type;
    const bool  pred_mode_plt_flag = cu->pred_mode == MODE_PLT;
    int ret                        = 0;

    if (tree_type == SINGLE_TREE || tree_type == DUAL_TREE_LUMA) {
        if (pred_mode_plt_flag) {
            if ((ret = hls_palette_coding(lc, tree_type)) < 0)
                return ret;
            ff_vvc_set_intra_mvf(lc, false, PF_PLT, false);
        } else {
            intra_luma_pred_modes(lc);
            ff_vvc_set_intra_mvf(lc, false, PF_INTRA, cu->ciip_flag);
        }
    }
    if ((tree_type == SINGLE_TREE || tree_type == DUAL_TREE_CHROMA) && sps->r->sps_chroma_format_idc) {
        if (pred_mode_plt_flag && tree_type == DUAL_TREE_CHROMA) {
            if ((ret = hls_palette_coding(lc, tree_type)) < 0)
                return ret;
        } else if (!pred_mode_plt_flag) {
            intra_chroma_pred_modes(lc);
        }
    }

    return ret;
}

static int hls_coding_unit(VVCLocalContext *lc, int x0, int y0, int cb_width, int cb_height,
    int cqt_depth, const VVCTreeType tree_type, VVCModeType mode_type)
{
    const VVCFrameContext *fc     = lc->fc;
    const VVCSPS *sps             = fc->ps.sps;
    const H266RawSliceHeader *rsh = lc->sc->sh.r;
    const int is_128              = cb_width > 64 || cb_height > 64;
    int ret                       = 0;

    CodingUnit *cu = add_cu(lc, x0, y0, cb_width, cb_height, cqt_depth, tree_type);

    if (!cu)
        return AVERROR(ENOMEM);

    ff_vvc_set_neighbour_available(lc, cu->x0, cu->y0, cu->cb_width, cu->cb_height);

    if (IS_I(rsh) && is_128)
        mode_type = MODE_TYPE_INTRA;
    cu->pred_mode = pred_mode_decode(lc, tree_type, mode_type);

    if (cu->pred_mode == MODE_INTRA && sps->r->sps_act_enabled_flag && tree_type == SINGLE_TREE)
        cu->act_enabled_flag = ff_vvc_cu_act_enabled_flag(lc);

    if (cu->pred_mode == MODE_INTRA || cu->pred_mode == MODE_PLT)
        ret = intra_data(lc);
    else if (tree_type != DUAL_TREE_CHROMA) /* MODE_INTER or MODE_IBC */
        ret = inter_data(lc);

    if (ret < 0)
        return ret;

    if (cu->pred_mode != MODE_INTRA && cu->pred_mode != MODE_PLT && !lc->cu->pu.general_merge_flag)
        cu->coded_flag = ff_vvc_cu_coded_flag(lc);
    else
        cu->coded_flag = !(cu->skip_flag || cu->pred_mode == MODE_PLT);

    if (cu->coded_flag) {
        sbt_info(lc, sps);
        if (sps->r->sps_act_enabled_flag && cu->pred_mode != MODE_INTRA && tree_type == SINGLE_TREE)
            cu->act_enabled_flag = ff_vvc_cu_act_enabled_flag(lc);
        lc->parse.lfnst_dc_only = 1;
        lc->parse.lfnst_zero_out_sig_coeff_flag = 1;
        lc->parse.mts_dc_only = 1;
        lc->parse.mts_zero_out_sig_coeff_flag = 1;
        ret = hls_transform_tree(lc, x0, y0, cb_width, cb_height, cu->ch_type);
        if (ret < 0)
            return ret;
        cu->lfnst_idx = lfnst_idx_decode(lc);
        cu->mts_idx = mts_idx_decode(lc);
        set_qp_c(lc);
    } else if (cu->pred_mode != MODE_PLT) {
        ret = skipped_transform_tree_unit(lc);
        if (ret < 0)
            return ret;
    }
    set_cu_tabs(lc, cu);

    return 0;
}

static int derive_mode_type_condition(const VVCLocalContext *lc,
    const VVCSplitMode split, const int cb_width, const int cb_height, const VVCModeType mode_type_curr)
{
    const H266RawSliceHeader *rsh   = lc->sc->sh.r;
    const VVCSPS *sps               = lc->fc->ps.sps;
    const int area                  = cb_width * cb_height;

    if ((IS_I(rsh) && sps->r->sps_qtbtt_dual_tree_intra_flag) ||
        mode_type_curr != MODE_TYPE_ALL || !sps->r->sps_chroma_format_idc ||
        sps->r->sps_chroma_format_idc == CHROMA_FORMAT_444)
        return 0;
    if ((area == 64 && (split == SPLIT_QT || split == SPLIT_TT_HOR || split == SPLIT_TT_VER)) ||
        (area == 32 &&  (split == SPLIT_BT_HOR || split == SPLIT_BT_VER)))
        return 1;
    if ((area == 64 && (split == SPLIT_BT_HOR || split == SPLIT_BT_VER) && sps->r->sps_chroma_format_idc == CHROMA_FORMAT_420) ||
        (area == 128 && (split == SPLIT_TT_HOR || split == SPLIT_TT_VER) && sps->r->sps_chroma_format_idc == CHROMA_FORMAT_420) ||
        (cb_width == 8 && split == SPLIT_BT_VER) || (cb_width == 16 && split == SPLIT_TT_VER))
        return 1 + !IS_I(rsh);

    return 0;
}

static VVCModeType mode_type_decode(VVCLocalContext *lc, const int x0, const int y0,
    const int cb_width, const int cb_height, const VVCSplitMode split, const int ch_type,
    const VVCModeType mode_type_curr)
{
    VVCModeType mode_type;
    const int mode_type_condition = derive_mode_type_condition(lc, split, cb_width, cb_height, mode_type_curr);

    if (mode_type_condition == 1)
        mode_type = MODE_TYPE_INTRA;
    else if (mode_type_condition == 2) {
        mode_type = ff_vvc_non_inter_flag(lc, x0, y0, ch_type) ? MODE_TYPE_INTRA : MODE_TYPE_INTER;
    } else {
        mode_type = mode_type_curr;
    }

    return mode_type;
}

static int hls_coding_tree(VVCLocalContext *lc,
    int x0, int y0, int cb_width, int cb_height, int qg_on_y, int qg_on_c,
    int cb_sub_div, int cqt_depth, int mtt_depth, int depth_offset, int part_idx,
    VVCSplitMode last_split_mode, VVCTreeType tree_type_curr, VVCModeType mode_type_curr);

static int coding_tree_btv(VVCLocalContext *lc,
    int x0, int y0, int cb_width, int cb_height, int qg_on_y, int qg_on_c,
    int cb_sub_div, int cqt_depth, int mtt_depth, int depth_offset,
    VVCTreeType tree_type, VVCModeType mode_type)
{
#define CODING_TREE(x, idx) do { \
    ret = hls_coding_tree(lc, x, y0, cb_width / 2, cb_height, \
        qg_on_y, qg_on_c, cb_sub_div + 1, cqt_depth, mtt_depth + 1, \
        depth_offset, idx, SPLIT_BT_VER, tree_type, mode_type); \
    if (ret < 0) \
        return ret; \
} while (0);

    const VVCPPS *pps = lc->fc->ps.pps;
    const int x1 = x0 + cb_width / 2;
    int ret = 0;

    depth_offset += (x0 + cb_width > pps->width) ? 1 : 0;
    CODING_TREE(x0, 0);
    if (x1 < pps->width)
        CODING_TREE(x1, 1);

    return 0;

#undef CODING_TREE
}

static int coding_tree_bth(VVCLocalContext *lc,
    int x0, int y0, int cb_width, int cb_height, int qg_on_y, int qg_on_c,
    int cb_sub_div, int cqt_depth, int mtt_depth, int depth_offset,
    VVCTreeType tree_type, VVCModeType mode_type)
{
#define CODING_TREE(y, idx) do { \
        ret = hls_coding_tree(lc, x0, y, cb_width , cb_height / 2, \
            qg_on_y, qg_on_c, cb_sub_div + 1, cqt_depth, mtt_depth + 1, \
            depth_offset, idx, SPLIT_BT_HOR, tree_type, mode_type); \
        if (ret < 0) \
            return ret; \
    } while (0);

    const VVCPPS *pps = lc->fc->ps.pps;
    const int y1 = y0 + (cb_height / 2);
    int ret = 0;

    depth_offset += (y0 + cb_height > pps->height) ? 1 : 0;
    CODING_TREE(y0, 0);
    if (y1 < pps->height)
        CODING_TREE(y1, 1);

    return 0;

#undef CODING_TREE
}

static int coding_tree_ttv(VVCLocalContext *lc,
    int x0, int y0, int cb_width, int cb_height, int qg_on_y, int qg_on_c,
    int cb_sub_div, int cqt_depth, int mtt_depth, int depth_offset,
    VVCTreeType tree_type, VVCModeType mode_type)
{
#define CODING_TREE(x, w, sub_div, idx) do { \
        ret = hls_coding_tree(lc, x, y0, w, cb_height, \
            qg_on_y, qg_on_c, sub_div, cqt_depth, mtt_depth + 1, \
            depth_offset, idx, SPLIT_TT_VER, tree_type, mode_type); \
        if (ret < 0) \
            return ret; \
    } while (0);

    const VVCSH *sh = &lc->sc->sh;
    const int x1    = x0 + cb_width / 4;
    const int x2    = x0 + cb_width * 3 / 4;
    int ret;

    qg_on_y = qg_on_y && (cb_sub_div + 2 <= sh->cu_qp_delta_subdiv);
    qg_on_c = qg_on_c && (cb_sub_div + 2 <= sh->cu_chroma_qp_offset_subdiv);

    CODING_TREE(x0, cb_width / 4, cb_sub_div + 2, 0);
    CODING_TREE(x1, cb_width / 2, cb_sub_div + 1, 1);
    CODING_TREE(x2, cb_width / 4, cb_sub_div + 2, 2);

    return 0;

#undef CODING_TREE
}

static int coding_tree_tth(VVCLocalContext *lc,
    int x0, int y0, int cb_width, int cb_height, int qg_on_y, int qg_on_c,
    int cb_sub_div, int cqt_depth, int mtt_depth, int depth_offset,
    VVCTreeType tree_type, VVCModeType mode_type)
{
#define CODING_TREE(y, h, sub_div, idx) do { \
        ret = hls_coding_tree(lc, x0, y, cb_width, h, \
            qg_on_y, qg_on_c, sub_div, cqt_depth, mtt_depth + 1, \
            depth_offset, idx, SPLIT_TT_HOR, tree_type, mode_type); \
        if (ret < 0) \
            return ret; \
    } while (0);

    const VVCSH *sh = &lc->sc->sh;
    const int y1    = y0 + (cb_height / 4);
    const int y2    = y0 + (3 * cb_height / 4);
    int ret;

    qg_on_y = qg_on_y && (cb_sub_div + 2 <= sh->cu_qp_delta_subdiv);
    qg_on_c = qg_on_c && (cb_sub_div + 2 <= sh->cu_chroma_qp_offset_subdiv);

    CODING_TREE(y0, cb_height / 4, cb_sub_div + 2, 0);
    CODING_TREE(y1, cb_height / 2, cb_sub_div + 1, 1);
    CODING_TREE(y2, cb_height / 4, cb_sub_div + 2, 2);

    return 0;

#undef CODING_TREE
}

static int coding_tree_qt(VVCLocalContext *lc,
    int x0, int y0, int cb_width, int cb_height, int qg_on_y, int qg_on_c,
    int cb_sub_div, int cqt_depth, int mtt_depth, int depth_offset,
    VVCTreeType tree_type, VVCModeType mode_type)
{
#define CODING_TREE(x, y, idx) do { \
        ret = hls_coding_tree(lc, x, y, cb_width / 2, cb_height / 2, \
            qg_on_y, qg_on_c, cb_sub_div + 2, cqt_depth + 1, 0, 0, \
            idx, SPLIT_QT, tree_type, mode_type); \
        if (ret < 0) \
            return ret; \
    } while (0);

    const VVCPPS *pps = lc->fc->ps.pps;
    const int x1 = x0 + cb_width / 2;
    const int y1 = y0 + cb_height / 2;
    int ret = 0;

    CODING_TREE(x0, y0, 0);
    if (x1 < pps->width)
        CODING_TREE(x1, y0, 1);
    if (y1 < pps->height)
        CODING_TREE(x0, y1, 2);
    if (x1 < pps->width &&
        y1 < pps->height)
        CODING_TREE(x1, y1, 3);

    return 0;

#undef CODING_TREE
}

typedef int (*coding_tree_fn)(VVCLocalContext *lc,
    int x0, int y0, int cb_width, int cb_height, int qg_on_y, int qg_on_c,
    int cb_sub_div, int cqt_depth, int mtt_depth, int depth_offset,
    VVCTreeType tree_type, VVCModeType mode_type);

const static coding_tree_fn coding_tree[] = {
    coding_tree_tth,
    coding_tree_bth,
    coding_tree_ttv,
    coding_tree_btv,
    coding_tree_qt,
};

static int hls_coding_tree(VVCLocalContext *lc,
    int x0, int y0, int cb_width, int cb_height, int qg_on_y, int qg_on_c,
    int cb_sub_div, int cqt_depth, int mtt_depth, int depth_offset, int part_idx,
    VVCSplitMode last_split_mode, VVCTreeType tree_type_curr, VVCModeType mode_type_curr)
{
    VVCFrameContext *fc             = lc->fc;
    const VVCPPS *pps               = fc->ps.pps;
    const VVCSH *sh                 = &lc->sc->sh;
    const H266RawSliceHeader *rsh   = sh->r;
    const int ch_type               = tree_type_curr == DUAL_TREE_CHROMA;
    int ret;
    VVCAllowedSplit allowed;

    if (pps->r->pps_cu_qp_delta_enabled_flag && qg_on_y && cb_sub_div <= sh->cu_qp_delta_subdiv) {
        lc->parse.is_cu_qp_delta_coded = 0;
        lc->parse.cu_qg_top_left_x = x0;
        lc->parse.cu_qg_top_left_y = y0;
    }
    if (rsh->sh_cu_chroma_qp_offset_enabled_flag && qg_on_c &&
        cb_sub_div <= sh->cu_chroma_qp_offset_subdiv) {
        lc->parse.is_cu_chroma_qp_offset_coded = 0;
        memset(lc->parse.chroma_qp_offset, 0, sizeof(lc->parse.chroma_qp_offset));
    }

    can_split(lc, x0, y0, cb_width, cb_height, mtt_depth, depth_offset, part_idx,
        last_split_mode, tree_type_curr, mode_type_curr, &allowed);
    if (ff_vvc_split_cu_flag(lc, x0, y0, cb_width, cb_height, ch_type, &allowed)) {
        VVCSplitMode split      = ff_vvc_split_mode(lc, x0, y0, cb_width, cb_height, cqt_depth, mtt_depth, ch_type, &allowed);
        VVCModeType mode_type   = mode_type_decode(lc, x0, y0, cb_width, cb_height, split, ch_type, mode_type_curr);

        VVCTreeType tree_type   = (mode_type == MODE_TYPE_INTRA) ? DUAL_TREE_LUMA : tree_type_curr;

        if (split != SPLIT_QT) {
            if (!(x0 & 31) && !(y0 & 31) && mtt_depth <= 1)
                TAB_MSM(fc, mtt_depth, x0, y0) = split;
        }
        ret = coding_tree[split - 1](lc, x0, y0, cb_width, cb_height, qg_on_y, qg_on_c,
            cb_sub_div, cqt_depth, mtt_depth, depth_offset, tree_type, mode_type);
        if (ret < 0)
            return ret;
        if (mode_type_curr == MODE_TYPE_ALL && mode_type == MODE_TYPE_INTRA) {
            ret = hls_coding_tree(lc, x0, y0, cb_width, cb_height, 0, qg_on_c, cb_sub_div,
                cqt_depth, mtt_depth, 0, 0, split, DUAL_TREE_CHROMA, mode_type);
            if (ret < 0)
                return ret;
        }
    } else {
        ret = hls_coding_unit(lc, x0, y0, cb_width, cb_height, cqt_depth, tree_type_curr, mode_type_curr);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int dual_tree_implicit_qt_split(VVCLocalContext *lc,
    const int x0, const int y0, const int cb_size, const int cqt_depth)
{
    const VVCSH *sh                 = &lc->sc->sh;
    const H266RawSliceHeader *rsh   = sh->r;
    const VVCPPS *pps               = lc->fc->ps.pps;
    const int cb_subdiv             = 2 * cqt_depth;
    int ret;

    if (cb_size > 64) {
        #define DUAL_TREE(x, y) do {                                                \
            ret = dual_tree_implicit_qt_split(lc, x, y, cb_size / 2, cqt_depth + 1); \
            if (ret < 0)                                                            \
                return ret;                                                         \
        } while (0)

        const int x1 = x0 + (cb_size / 2);
        const int y1 = y0 + (cb_size / 2);
        if (pps->r->pps_cu_qp_delta_enabled_flag && cb_subdiv <= sh->cu_qp_delta_subdiv) {
            lc->parse.is_cu_qp_delta_coded = 0;
            lc->parse.cu_qg_top_left_x = x0;
            lc->parse.cu_qg_top_left_y = y0;
        }
        if (rsh->sh_cu_chroma_qp_offset_enabled_flag && cb_subdiv <= sh->cu_chroma_qp_offset_subdiv) {
            lc->parse.is_cu_chroma_qp_offset_coded = 0;
            memset(lc->parse.chroma_qp_offset, 0, sizeof(lc->parse.chroma_qp_offset));
        }
        DUAL_TREE(x0, y0);
        if (x1 < pps->width)
            DUAL_TREE(x1, y0);
        if (y1 < pps->height)
            DUAL_TREE(x0, y1);
        if (x1 < pps->width && y1 < pps->height)
            DUAL_TREE(x1, y1);
    #undef DUAL_TREE
    } else {
        #define CODING_TREE(tree_type) do {                                             \
            const int qg_on_y = tree_type == DUAL_TREE_LUMA;                            \
            ret = hls_coding_tree(lc, x0, y0, cb_size, cb_size, qg_on_y, !qg_on_y,           \
                 cb_subdiv, cqt_depth, 0, 0, 0, SPLIT_NONE, tree_type, MODE_TYPE_ALL);  \
            if (ret < 0)                                                                \
                return ret;                                                             \
        } while (0)
        CODING_TREE(DUAL_TREE_LUMA);
        CODING_TREE(DUAL_TREE_CHROMA);
        #undef CODING_TREE
    }
    return 0;
}

#define SET_SAO(elem, value)                            \
do {                                                    \
    if (!sao_merge_up_flag && !sao_merge_left_flag)     \
        sao->elem = value;                              \
    else if (sao_merge_left_flag)                       \
        sao->elem = CTB(fc->tab.sao, rx-1, ry).elem;         \
    else if (sao_merge_up_flag)                         \
        sao->elem = CTB(fc->tab.sao, rx, ry-1).elem;         \
    else                                                \
        sao->elem = 0;                                  \
} while (0)

static void hls_sao(VVCLocalContext *lc, const int rx, const int ry)
{
    VVCFrameContext *fc             = lc->fc;
    const H266RawSliceHeader *rsh   = lc->sc->sh.r;
    int sao_merge_left_flag         = 0;
    int sao_merge_up_flag           = 0;
    SAOParams *sao                  = &CTB(fc->tab.sao, rx, ry);
    int c_idx, i;

    if (rsh->sh_sao_luma_used_flag || rsh->sh_sao_chroma_used_flag) {
        if (rx > 0) {
            if (lc->ctb_left_flag)
                sao_merge_left_flag = ff_vvc_sao_merge_flag_decode(lc);
        }
        if (ry > 0 && !sao_merge_left_flag) {
            if (lc->ctb_up_flag)
                sao_merge_up_flag = ff_vvc_sao_merge_flag_decode(lc);
        }
    }

    for (c_idx = 0; c_idx < (fc->ps.sps->r->sps_chroma_format_idc ? 3 : 1); c_idx++) {
        const int sao_used_flag = !c_idx ? rsh->sh_sao_luma_used_flag : rsh->sh_sao_chroma_used_flag;
        if (!sao_used_flag) {
            sao->type_idx[c_idx] = SAO_NOT_APPLIED;
            continue;
        }

        if (c_idx == 2) {
            sao->type_idx[2] = sao->type_idx[1];
            sao->eo_class[2] = sao->eo_class[1];
        } else {
            SET_SAO(type_idx[c_idx], ff_vvc_sao_type_idx_decode(lc));
        }

        if (sao->type_idx[c_idx] == SAO_NOT_APPLIED)
            continue;

        for (i = 0; i < 4; i++)
            SET_SAO(offset_abs[c_idx][i], ff_vvc_sao_offset_abs_decode(lc));

        if (sao->type_idx[c_idx] == SAO_BAND) {
            for (i = 0; i < 4; i++) {
                if (sao->offset_abs[c_idx][i]) {
                    SET_SAO(offset_sign[c_idx][i],
                            ff_vvc_sao_offset_sign_decode(lc));
                } else {
                    sao->offset_sign[c_idx][i] = 0;
                }
            }
            SET_SAO(band_position[c_idx], ff_vvc_sao_band_position_decode(lc));
        } else if (c_idx != 2) {
            SET_SAO(eo_class[c_idx], ff_vvc_sao_eo_class_decode(lc));
        }

        // Inferred parameters
        sao->offset_val[c_idx][0] = 0;
        for (i = 0; i < 4; i++) {
            sao->offset_val[c_idx][i + 1] = sao->offset_abs[c_idx][i];
            if (sao->type_idx[c_idx] == SAO_EDGE) {
                if (i > 1)
                    sao->offset_val[c_idx][i + 1] = -sao->offset_val[c_idx][i + 1];
            } else if (sao->offset_sign[c_idx][i]) {
                sao->offset_val[c_idx][i + 1] = -sao->offset_val[c_idx][i + 1];
            }
            sao->offset_val[c_idx][i + 1] *= 1 << (fc->ps.sps->bit_depth - FFMIN(10, fc->ps.sps->bit_depth));
        }
    }
}

static void alf_params(VVCLocalContext *lc, const int rx, const int ry)
{
    const VVCFrameContext *fc     = lc->fc;
    const H266RawSliceHeader *sh  = lc->sc->sh.r;
    ALFParams *alf                = &CTB(fc->tab.alf, rx, ry);

    alf->ctb_flag[LUMA] = alf->ctb_flag[CB] = alf->ctb_flag[CR] = 0;
    alf->ctb_cc_idc[0] = alf->ctb_cc_idc[1] = 0;
    if (sh->sh_alf_enabled_flag) {
        alf->ctb_flag[LUMA] = ff_vvc_alf_ctb_flag(lc, rx, ry, LUMA);
        if (alf->ctb_flag[LUMA]) {
            uint8_t alf_use_aps_flag = 0;
            if (sh->sh_num_alf_aps_ids_luma > 0)
                alf_use_aps_flag = ff_vvc_alf_use_aps_flag(lc);
            if (alf_use_aps_flag) {
                alf->ctb_filt_set_idx_y = 16;
                if (sh->sh_num_alf_aps_ids_luma > 1)
                    alf->ctb_filt_set_idx_y += ff_vvc_alf_luma_prev_filter_idx(lc);
            } else {
                alf->ctb_filt_set_idx_y = ff_vvc_alf_luma_fixed_filter_idx(lc);
            }
        }
        for (int c_idx = CB; c_idx <= CR; c_idx++) {
            const uint8_t alf_enabled_flag =
                c_idx == CB ? sh->sh_alf_cb_enabled_flag : sh->sh_alf_cr_enabled_flag;
            if (alf_enabled_flag) {
                const VVCALF *aps = fc->ps.alf_list[sh->sh_alf_aps_id_chroma];
                alf->ctb_flag[c_idx] = ff_vvc_alf_ctb_flag(lc, rx, ry, c_idx);
                alf->alf_ctb_filter_alt_idx[c_idx - 1] = 0;
                if (alf->ctb_flag[c_idx] && aps->num_chroma_filters > 1)
                    alf->alf_ctb_filter_alt_idx[c_idx - 1] = ff_vvc_alf_ctb_filter_alt_idx(lc, c_idx, aps->num_chroma_filters);
            }
        }
    }
    if (fc->ps.sps->r->sps_ccalf_enabled_flag) {
        const uint8_t cc_enabled[] = { sh->sh_alf_cc_cb_enabled_flag, sh->sh_alf_cc_cr_enabled_flag };
        const uint8_t cc_aps_id[]  = { sh->sh_alf_cc_cb_aps_id, sh->sh_alf_cc_cr_aps_id };
        for (int i = 0; i < 2; i++) {
            if (cc_enabled[i]) {
                const VVCALF *aps = fc->ps.alf_list[cc_aps_id[i]];
                alf->ctb_cc_idc[i] = ff_vvc_alf_ctb_cc_idc(lc, rx, ry, i, aps->num_cc_filters[i]);
            }
        }
    }
}

static void deblock_params(VVCLocalContext *lc, const int rx, const int ry)
{
    VVCFrameContext *fc = lc->fc;
    const VVCSH *sh     = &lc->sc->sh;
    CTB(fc->tab.deblock, rx, ry) = sh->deblock;
}

static int hls_coding_tree_unit(VVCLocalContext *lc,
    const int x0, const int y0, const int ctu_idx, const int rx, const int ry)
{
    const VVCFrameContext *fc       = lc->fc;
    const VVCSPS *sps               = fc->ps.sps;
    const VVCPPS *pps               = fc->ps.pps;
    const VVCSH *sh                 = &lc->sc->sh;
    const H266RawSliceHeader *rsh   = sh->r;
    const unsigned int ctb_size     = sps->ctb_size_y;
    int ret                         = 0;

    memset(lc->parse.chroma_qp_offset, 0, sizeof(lc->parse.chroma_qp_offset));

    hls_sao(lc, x0 >> sps->ctb_log2_size_y, y0 >> sps->ctb_log2_size_y);
    alf_params(lc, x0 >> sps->ctb_log2_size_y, y0 >> sps->ctb_log2_size_y);
    deblock_params(lc, x0 >> sps->ctb_log2_size_y, y0 >> sps->ctb_log2_size_y);

    if (IS_I(rsh) && sps->r->sps_qtbtt_dual_tree_intra_flag)
        ret = dual_tree_implicit_qt_split(lc, x0, y0, ctb_size, 0);
    else
        ret = hls_coding_tree(lc, x0, y0, ctb_size, ctb_size,
            1, 1, 0, 0, 0, 0, 0, SPLIT_NONE, SINGLE_TREE, MODE_TYPE_ALL);
    if (ret < 0)
        return ret;

    if (rx == pps->ctb_to_col_bd[rx + 1] - 1) {
        if (ctu_idx == sh->num_ctus_in_curr_slice - 1) {
            const int end_of_slice_one_bit = ff_vvc_end_of_slice_flag_decode(lc);
            if (!end_of_slice_one_bit)
                return AVERROR_INVALIDDATA;
        } else {
            if (ry == pps->ctb_to_row_bd[ry + 1] - 1) {
                const int end_of_tile_one_bit = ff_vvc_end_of_tile_one_bit(lc);
                if (!end_of_tile_one_bit)
                    return AVERROR_INVALIDDATA;
            } else {
                if (fc->ps.sps->r->sps_entropy_coding_sync_enabled_flag) {
                    const int end_of_subset_one_bit = ff_vvc_end_of_subset_one_bit(lc);
                    if (!end_of_subset_one_bit)
                        return AVERROR_INVALIDDATA;
                }
            }
        }
    }

    return 0;
}

static int has_inter_luma(const CodingUnit *cu)
{
    return cu->pred_mode != MODE_INTRA && cu->pred_mode != MODE_PLT && cu->tree_type != DUAL_TREE_CHROMA;
}

static int pred_get_y(const VVCLocalContext *lc, const int y0, const Mv *mv, const int height)
{
    const VVCPPS *pps = lc->fc->ps.pps;
    const int idx     = lc->sc->sh.r->curr_subpic_idx;
    const int top     = pps->subpic_y[idx];
    const int bottom  = top + pps->subpic_height[idx];

    return av_clip(y0 + (mv->y >> 4) + height, top, bottom);
}

static void cu_get_max_y(const CodingUnit *cu, int max_y[2][VVC_MAX_REF_ENTRIES], const VVCLocalContext *lc)
{
    const VVCFrameContext *fc   = lc->fc;
    const PredictionUnit *pu    = &cu->pu;

    if (pu->merge_gpm_flag) {
        for (int i = 0; i < FF_ARRAY_ELEMS(pu->gpm_mv); i++) {
            const MvField *mvf  = pu->gpm_mv + i;
            const int lx        = mvf->pred_flag - PF_L0;
            const int idx       = mvf->ref_idx[lx];
            const int y         = pred_get_y(lc, cu->y0, mvf->mv + lx, cu->cb_height);

            max_y[lx][idx]      = FFMAX(max_y[lx][idx], y);
        }
    } else {
        const MotionInfo *mi    = &pu->mi;
        const int max_dmvr_off  = (!pu->inter_affine_flag && pu->dmvr_flag) ? 2 : 0;
        const int sbw           = cu->cb_width / mi->num_sb_x;
        const int sbh           = cu->cb_height / mi->num_sb_y;
        for (int sby = 0; sby < mi->num_sb_y; sby++) {
            for (int sbx = 0; sbx < mi->num_sb_x; sbx++) {
                const int x0        = cu->x0 + sbx * sbw;
                const int y0        = cu->y0 + sby * sbh;
                const MvField *mvf  = ff_vvc_get_mvf(fc, x0, y0);
                for (int lx = 0; lx < 2; lx++) {
                    const PredFlag mask = 1 << lx;
                    if (mvf->pred_flag & mask) {
                        const int idx   = mvf->ref_idx[lx];
                        const int y     = pred_get_y(lc, y0, mvf->mv + lx, sbh);

                        max_y[lx][idx]  = FFMAX(max_y[lx][idx], y + max_dmvr_off);
                    }
                }
            }
        }
    }
}

static void ctu_get_pred(VVCLocalContext *lc, const int rs)
{
    const VVCFrameContext *fc       = lc->fc;
    const H266RawSliceHeader *rsh   = lc->sc->sh.r;
    CTU *ctu                        = fc->tab.ctus + rs;
    const CodingUnit *cu            = fc->tab.cus[rs];

    ctu->has_dmvr = 0;

    if (IS_I(rsh))
        return;

    for (int lx = 0; lx < 2; lx++)
        memset(ctu->max_y[lx], -1, sizeof(ctu->max_y[0][0]) * rsh->num_ref_idx_active[lx]);

    while (cu) {
        if (has_inter_luma(cu)) {
            cu_get_max_y(cu, ctu->max_y, lc);
            ctu->has_dmvr |= cu->pu.dmvr_flag;
        }
        cu = cu->next;
    }
    ctu->max_y_idx[0] = ctu->max_y_idx[1] = 0;
}

int ff_vvc_coding_tree_unit(VVCLocalContext *lc,
    const int ctu_idx, const int rs, const int rx, const int ry)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCSPS *sps           = fc->ps.sps;
    const VVCPPS *pps           = fc->ps.pps;
    const int x_ctb             = rx << sps->ctb_log2_size_y;
    const int y_ctb             = ry << sps->ctb_log2_size_y;
    const int ctb_size          = 1 << sps->ctb_log2_size_y << sps->ctb_log2_size_y;
    EntryPoint* ep              = lc->ep;
    int ret;

    if (rx == pps->ctb_to_col_bd[rx]) {
        ep->num_hmvp = 0;
        ep->num_hmvp_ibc = 0;
        ep->is_first_qg = ry == pps->ctb_to_row_bd[ry] || !ctu_idx;
    }

    lc->coeffs = fc->tab.coeffs + rs * ctb_size * VVC_MAX_SAMPLE_ARRAYS;
    lc->cu     = NULL;

    ff_vvc_cabac_init(lc, ctu_idx, rx, ry);
    ff_vvc_decode_neighbour(lc, x_ctb, y_ctb, rx, ry, rs);
    ret = hls_coding_tree_unit(lc, x_ctb, y_ctb, ctu_idx, rx, ry);
    if (ret < 0)
        return ret;
    ctu_get_pred(lc, rs);

    return 0;
}

void ff_vvc_decode_neighbour(VVCLocalContext *lc, const int x_ctb, const int y_ctb,
    const int rx, const int ry, const int rs)
{
    VVCFrameContext *fc = lc->fc;
    const int ctb_size         = fc->ps.sps->ctb_size_y;

    lc->end_of_tiles_x = fc->ps.pps->width;
    lc->end_of_tiles_y = fc->ps.pps->height;
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
    if (fc->ps.sps->r->sps_subpic_ctu_top_left_x[lc->sc->sh.r->curr_subpic_idx] == rx)
        lc->boundary_flags |= BOUNDARY_LEFT_SUBPIC;
    if (fc->ps.sps->r->sps_subpic_ctu_top_left_y[lc->sc->sh.r->curr_subpic_idx] == ry)
        lc->boundary_flags |= BOUNDARY_UPPER_SUBPIC;
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
    const int x0b = av_zero_extend(x0, log2_ctb_size);
    const int y0b = av_zero_extend(y0, log2_ctb_size);

    lc->na.cand_up       = (lc->ctb_up_flag   || y0b);
    lc->na.cand_left     = (lc->ctb_left_flag || x0b);
    lc->na.cand_up_left  = (x0b || y0b) ? lc->na.cand_left && lc->na.cand_up : lc->ctb_up_left_flag;
    lc->na.cand_up_right_sap =
            (x0b + w == 1 << log2_ctb_size) ? lc->ctb_up_right_flag && !y0b : lc->na.cand_up;
    lc->na.cand_up_right = lc->na.cand_up_right_sap && (x0 + w) < lc->end_of_tiles_x;
}

void ff_vvc_ctu_free_cus(CodingUnit **cus)
{
    while (*cus) {
        CodingUnit *cu          = *cus;
        TransformUnit **head    = &cu->tus.head;

        *cus = cu->next;

        while (*head) {
            TransformUnit *tu = *head;
            *head = tu->next;
            av_refstruct_unref(&tu);
        }
        cu->tus.tail = NULL;

        av_refstruct_unref(&cu);
    }
}

int ff_vvc_get_qPy(const VVCFrameContext *fc, const int xc, const int yc)
{
    const int min_cb_log2_size_y = fc->ps.sps->min_cb_log2_size_y;
    const int x                  = xc >> min_cb_log2_size_y;
    const int y                  = yc >> min_cb_log2_size_y;
    return fc->tab.qp[LUMA][x + y * fc->ps.pps->min_cb_width];
}

void ff_vvc_ep_init_stat_coeff(EntryPoint *ep,
    const int bit_depth, const int persistent_rice_adaptation_enabled_flag)
{
    for (size_t i = 0; i < FF_ARRAY_ELEMS(ep->stat_coeff); ++i) {
        ep->stat_coeff[i] =
            persistent_rice_adaptation_enabled_flag ? 2 * (av_log2(bit_depth - 10)) : 0;
    }
}

void ff_vvc_channel_range(int *start, int *end, const VVCTreeType tree_type, const uint8_t chroma_format_idc)
{
    const bool has_chroma = chroma_format_idc && tree_type != DUAL_TREE_LUMA;
    const bool has_luma   = tree_type != DUAL_TREE_CHROMA;

    *start = has_luma   ? LUMA : CB;
    *end   = has_chroma ? VVC_MAX_SAMPLE_ARRAYS : CB;
}
