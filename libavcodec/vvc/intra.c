/*
 * VVC intra prediction
 *
 * Copyright (C) 2021 Nuo Mi
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
#include "libavutil/frame.h"
#include "libavutil/imgutils.h"

#include "data.h"
#include "inter.h"
#include "intra.h"
#include "itx_1d.h"

static int is_cclm(enum IntraPredMode mode)
{
    return mode == INTRA_LT_CCLM || mode == INTRA_L_CCLM || mode == INTRA_T_CCLM;
}

static int derive_ilfnst_pred_mode_intra(const VVCLocalContext *lc, const TransformBlock *tb)
{
    const VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps = fc->ps.sps;
    const CodingUnit *cu          = lc->cu;
    const int x_tb                = tb->x0 >> fc->ps.sps->min_cb_log2_size_y;
    const int y_tb                = tb->y0 >> fc->ps.sps->min_cb_log2_size_y;
    const int x_c                 = (tb->x0 + (tb->tb_width << sps->hshift[1] >> 1) ) >> fc->ps.sps->min_cb_log2_size_y;
    const int y_c                 = (tb->y0 + (tb->tb_height << sps->vshift[1] >> 1)) >> fc->ps.sps->min_cb_log2_size_y;
    const int min_cb_width        = fc->ps.pps->min_cb_width;
    const int intra_mip_flag      = SAMPLE_CTB(fc->tab.imf, x_tb, y_tb);
    int pred_mode_intra = tb->c_idx == 0 ? cu->intra_pred_mode_y : cu->intra_pred_mode_c;
    if (intra_mip_flag && !tb->c_idx) {
        pred_mode_intra = INTRA_PLANAR;
    } else if (is_cclm(pred_mode_intra)) {
        int intra_mip_flag_c = SAMPLE_CTB(fc->tab.imf, x_c, y_c);
        int cu_pred_mode = SAMPLE_CTB(fc->tab.cpm[0], x_c, y_c);
        if (intra_mip_flag_c) {
            pred_mode_intra = INTRA_PLANAR;
        } else if (cu_pred_mode == MODE_IBC || cu_pred_mode == MODE_PLT) {
            pred_mode_intra = INTRA_DC;
        } else {
            pred_mode_intra = SAMPLE_CTB(fc->tab.ipm, x_c, y_c);
        }
    }
    pred_mode_intra = ff_vvc_wide_angle_mode_mapping(cu, tb->tb_width, tb->tb_height, tb->c_idx, pred_mode_intra);

    return pred_mode_intra;
}

//8.7.4 Transformation process for scaled transform coefficients
static void ilfnst_transform(const VVCLocalContext *lc, TransformBlock *tb)
{
    const VVCSPS *sps           = lc->fc->ps.sps;
    const CodingUnit *cu        = lc->cu;
    const int w                 = tb->tb_width;
    const int h                 = tb->tb_height;
    const int n_lfnst_out_size  = (w >= 8 && h >= 8) ? 48 : 16;                         ///< nLfnstOutSize
    const int log2_lfnst_size   = (w >= 8 && h >= 8) ? 3 : 2;                           ///< log2LfnstSize
    const int n_lfnst_size      = 1 << log2_lfnst_size;                                 ///< nLfnstSize
    const int non_zero_size     = ((w == 8 && h == 8) || (w == 4 && h == 4)) ? 8 : 16;  ///< nonZeroSize
    const int pred_mode_intra   = derive_ilfnst_pred_mode_intra(lc, tb);
    const int transpose         = pred_mode_intra > 34;
    int u[16], v[48];

    for (int x = 0; x < non_zero_size; x++) {
        int xc = ff_vvc_diag_scan_x[2][2][x];
        int yc = ff_vvc_diag_scan_y[2][2][x];
        u[x] = tb->coeffs[w * yc + xc];
    }
    ff_vvc_inv_lfnst_1d(v, u, non_zero_size, n_lfnst_out_size, pred_mode_intra,
                        cu->lfnst_idx, sps->log2_transform_range);
    if (transpose) {
        int *dst = tb->coeffs;
        const int *src = v;
        if (n_lfnst_size == 4) {
            for (int y = 0; y < 4; y++) {
                dst[0] = src[0];
                dst[1] = src[4];
                dst[2] = src[8];
                dst[3] = src[12];
                src++;
                dst += w;
            }
        } else {
            for (int y = 0; y < 8; y++) {
                dst[0] = src[0];
                dst[1] = src[8];
                dst[2] = src[16];
                dst[3] = src[24];
                if (y < 4) {
                    dst[4] = src[32];
                    dst[5] = src[36];
                    dst[6] = src[40];
                    dst[7] = src[44];
                }
                src++;
                dst += w;
            }
        }

    } else {
        int *dst = tb->coeffs;
        const int *src = v;
        for (int y = 0; y < n_lfnst_size; y++) {
            int size = (y < 4) ? n_lfnst_size : 4;
            memcpy(dst, src, size * sizeof(int));
            src += size;
            dst += w;
        }
    }
    tb->max_scan_x = n_lfnst_size - 1;
    tb->max_scan_y = n_lfnst_size - 1;
}

//part of 8.7.4 Transformation process for scaled transform coefficients
static void derive_transform_type(const VVCFrameContext *fc, const VVCLocalContext *lc, const TransformBlock *tb, enum VVCTxType *trh, enum VVCTxType *trv)
{
    const CodingUnit *cu = lc->cu;
    static const enum VVCTxType mts_to_trh[] = { VVC_DCT2, VVC_DST7, VVC_DCT8, VVC_DST7, VVC_DCT8 };
    static const enum VVCTxType mts_to_trv[] = { VVC_DCT2, VVC_DST7, VVC_DST7, VVC_DCT8, VVC_DCT8 };
    const VVCSPS *sps       = fc->ps.sps;
    int implicit_mts_enabled = 0;
    if (tb->c_idx || (cu->isp_split_type != ISP_NO_SPLIT && cu->lfnst_idx)) {
        *trh = *trv = VVC_DCT2;
        return;
    }

    if (sps->r->sps_mts_enabled_flag) {
        if (cu->isp_split_type != ISP_NO_SPLIT ||
            (cu->sbt_flag && FFMAX(tb->tb_width, tb->tb_height) <= 32) ||
            (!sps->r->sps_explicit_mts_intra_enabled_flag && cu->pred_mode == MODE_INTRA &&
            !cu->lfnst_idx && !cu->intra_mip_flag)) {
            implicit_mts_enabled = 1;
        }
    }
    if (implicit_mts_enabled) {
        const int w = tb->tb_width;
        const int h = tb->tb_height;
        if (cu->sbt_flag) {
            *trh = (cu->sbt_horizontal_flag  || cu->sbt_pos_flag) ? VVC_DST7 : VVC_DCT8;
            *trv = (!cu->sbt_horizontal_flag || cu->sbt_pos_flag) ? VVC_DST7 : VVC_DCT8;
        } else {
            *trh = (w >= 4 && w <= 16) ? VVC_DST7 : VVC_DCT2;
            *trv = (h >= 4 && h <= 16) ? VVC_DST7 : VVC_DCT2;
        }
        return;
    }
    *trh = mts_to_trh[cu->mts_idx];
    *trv = mts_to_trv[cu->mts_idx];
}

static void add_residual_for_joint_coding_chroma(VVCLocalContext *lc,
    const TransformUnit *tu, TransformBlock *tb, const int chroma_scale)
{
    const VVCFrameContext *fc  = lc->fc;
    const CodingUnit *cu = lc->cu;
    const int c_sign = 1 - 2 * fc->ps.ph.r->ph_joint_cbcr_sign_flag;
    const int shift  = tu->coded_flag[1] ^ tu->coded_flag[2];
    const int c_idx  = 1 + tu->coded_flag[1];
    const ptrdiff_t stride = fc->frame->linesize[c_idx];
    const int hs = fc->ps.sps->hshift[c_idx];
    const int vs = fc->ps.sps->vshift[c_idx];
    uint8_t *dst = &fc->frame->data[c_idx][(tb->y0 >> vs) * stride +
                                          ((tb->x0 >> hs) << fc->ps.sps->pixel_shift)];
    if (chroma_scale) {
        fc->vvcdsp.itx.pred_residual_joint(tb->coeffs, tb->tb_width, tb->tb_height, c_sign, shift);
        fc->vvcdsp.intra.lmcs_scale_chroma(lc, tb->coeffs, tb->coeffs, tb->tb_width, tb->tb_height, cu->x0, cu->y0);
        fc->vvcdsp.itx.add_residual(dst, tb->coeffs, tb->tb_width, tb->tb_height, stride);
    } else {
        fc->vvcdsp.itx.add_residual_joint(dst, tb->coeffs, tb->tb_width, tb->tb_height, stride, c_sign, shift);
    }
}

static int add_reconstructed_area(VVCLocalContext *lc, const int ch_type, const int x0, const int y0, const int w, const int h)
{
    const VVCSPS *sps       = lc->fc->ps.sps;
    const int hs = sps->hshift[ch_type];
    const int vs = sps->vshift[ch_type];
    ReconstructedArea *a;

    if (lc->num_ras[ch_type] >= FF_ARRAY_ELEMS(lc->ras[ch_type]))
        return AVERROR_INVALIDDATA;

    a = &lc->ras[ch_type][lc->num_ras[ch_type]];
    a->x = x0 >> hs;
    a->y = y0 >> vs;
    a->w = w >> hs;
    a->h = h >> vs;
    lc->num_ras[ch_type]++;

    return 0;
}

static void add_tu_area(const TransformUnit *tu, int *x0, int *y0, int *w, int *h)
{
    *x0 = tu->x0;
    *y0 = tu->y0;
    *w = tu->width;
    *h = tu->height;
}

#define MIN_ISP_PRED_WIDTH 4
static int get_luma_predict_unit(const CodingUnit *cu, const TransformUnit *tu, const int idx, int *x0, int *y0, int *w, int *h)
{
    int has_luma = 1;
    add_tu_area(tu, x0, y0, w, h);
    if (cu->isp_split_type == ISP_VER_SPLIT && tu->width < MIN_ISP_PRED_WIDTH) {
        *w = MIN_ISP_PRED_WIDTH;
        has_luma = !(idx % (MIN_ISP_PRED_WIDTH / tu->width));
    }
    return has_luma;
}

static int get_chroma_predict_unit(const CodingUnit *cu, const TransformUnit *tu, const int idx, int *x0, int *y0, int *w, int *h)
{
    if (cu->isp_split_type == ISP_NO_SPLIT) {
        add_tu_area(tu, x0, y0, w, h);
        return 1;
    }
    if (idx == cu->num_intra_subpartitions - 1) {
        *x0 = cu->x0;
        *y0 = cu->y0;
        *w = cu->cb_width;
        *h = cu->cb_height;
        return 1;
    }
    return 0;
}

//8.4.5.1 General decoding process for intra blocks
static void predict_intra(VVCLocalContext *lc, const TransformUnit *tu, const int idx, const int target_ch_type)
{
    const VVCFrameContext *fc         = lc->fc;
    const CodingUnit *cu        = lc->cu;
    const VVCTreeType tree_type = cu->tree_type;
    int x0, y0, w, h;
    if (cu->pred_mode != MODE_INTRA) {
        add_reconstructed_area(lc, target_ch_type, tu->x0, tu->y0, tu->width, tu->height);
        return;
    }
    if (!target_ch_type && tree_type != DUAL_TREE_CHROMA) {
        if (get_luma_predict_unit(cu, tu, idx, &x0, &y0, &w, &h)) {
            ff_vvc_set_neighbour_available(lc, x0, y0, w, h);
            fc->vvcdsp.intra.intra_pred(lc, x0, y0, w, h, 0);
            add_reconstructed_area(lc, 0, x0, y0, w, h);
        }
    }
    if (target_ch_type && tree_type != DUAL_TREE_LUMA) {
        if (get_chroma_predict_unit(cu, tu, idx, &x0, &y0, &w, &h)){
            ff_vvc_set_neighbour_available(lc, x0, y0, w, h);
            if (is_cclm(cu->intra_pred_mode_c)) {
                fc->vvcdsp.intra.intra_cclm_pred(lc, x0, y0, w, h);
            } else {
                fc->vvcdsp.intra.intra_pred(lc, x0, y0, w, h, 1);
                fc->vvcdsp.intra.intra_pred(lc, x0, y0, w, h, 2);
            }
            add_reconstructed_area(lc, 1, x0, y0, w, h);
        }
    }
}

static void scale_clip(int *coeff, const int nzw, const int w, const int h,
    const int shift, const int log2_transform_range)
{
    const int add = 1 << (shift - 1);
    for (int y = 0; y < h; y++) {
        int *p = coeff + y * w;
        for (int x = 0; x < nzw; x++) {
            *p = av_clip_intp2((*p + add) >> shift, log2_transform_range);
            p++;
        }
        memset(p, 0, sizeof(*p) * (w - nzw));
    }
}

static void scale(int *out, const int *in, const int w, const int h, const int shift)
{
    const int add = 1 << (shift - 1);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int *o = out + y * w + x;
            const int *i = in + y * w + x;
            *o = (*i + add) >> shift;
        }
    }
}

// part of 8.7.3 Scaling process for transform coefficients
static void derive_qp(const VVCLocalContext *lc, const TransformUnit *tu, TransformBlock *tb)
{
    const VVCSPS *sps               = lc->fc->ps.sps;
    const H266RawSliceHeader *rsh   = lc->sc->sh.r;
    const CodingUnit *cu            = lc->cu;
    int qp, qp_act_offset;

    if (tb->c_idx == 0) {
        //fix me
        qp = cu->qp[LUMA] + sps->qp_bd_offset;
        qp_act_offset = cu->act_enabled_flag ? -5 : 0;
    } else {
        const int is_jcbcr = tu->joint_cbcr_residual_flag && tu->coded_flag[CB] && tu->coded_flag[CR];
        const int idx = is_jcbcr ? JCBCR : tb->c_idx;
        qp = cu->qp[idx];
        qp_act_offset = cu->act_enabled_flag ? 1 : 0;
    }
    if (tb->ts) {
        const int qp_prime_ts_min = 4 + 6 * sps->r->sps_min_qp_prime_ts;

        tb->qp = av_clip(qp + qp_act_offset, qp_prime_ts_min, 63 + sps->qp_bd_offset);
        tb->rect_non_ts_flag = 0;
        tb->bd_shift = 10;
    } else {
        const int log_sum = tb->log2_tb_width + tb->log2_tb_height;
        const int rect_non_ts_flag = log_sum & 1;

        tb->qp = av_clip(qp + qp_act_offset, 0, 63 + sps->qp_bd_offset);
        tb->rect_non_ts_flag = rect_non_ts_flag;
        tb->bd_shift = sps->bit_depth + rect_non_ts_flag + (log_sum / 2)
                     + 10 - sps->log2_transform_range + rsh->sh_dep_quant_used_flag;
    }
    tb->bd_offset = (1 << tb->bd_shift) >> 1;
}

//8.7.3 Scaling process for transform coefficients
static av_always_inline int derive_scale(const TransformBlock *tb, const int sh_dep_quant_used_flag)
{
    static const uint8_t rem6[63 + 8 * 6 + 1] = {
         0,  1,  2,  3,  4,  5,  0,  1,  2,  3,  4,  5,  0,  1,  2,  3,  4,  5,  0,  1,  2,  3,  4,  5,
         0,  1,  2,  3,  4,  5,  0,  1,  2,  3,  4,  5,  0,  1,  2,  3,  4,  5,  0,  1,  2,  3,  4,  5,
         0,  1,  2,  3,  4,  5,  0,  1,  2,  3,  4,  5,  0,  1,  2,  3,  4,  5,  0,  1,  2,  3,  4,  5,
         0,  1,  2,  3,  4,  5,  0,  1,  2,  3,  4,  5,  0,  1,  2,  3,  4,  5,  0,  1,  2,  3,  4,  5,
         0,  1,  2,  3,  4,  5,  0,  1,  2,  3,  4,  5,  0,  1,  2,  3,
    };

    static const uint8_t div6[63 + 8 * 6 + 1] = {
         0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,
         4,  4,  4,  4,  4,  4,  5,  5,  5,  5,  5,  5,  6,  6,  6,  6,  6,  6,  7,  7,  7,  7,  7,  7,
         8,  8,  8,  8,  8,  8,  9,  9,  9,  9,  9,  9, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11,
        12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15,
        16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 17, 18, 18, 18, 18,
    };

    const static int level_scale[2][6] = {
        { 40, 45, 51, 57, 64, 72 },
        { 57, 64, 72, 80, 90, 102 }
    };
    const int addin = sh_dep_quant_used_flag && !tb->ts;
    const int qp    = tb->qp + addin;

    return level_scale[tb->rect_non_ts_flag][rem6[qp]] << div6[qp];
}

//8.7.3 Scaling process for transform coefficients
static const uint8_t* derive_scale_m(const VVCLocalContext *lc, const TransformBlock *tb, uint8_t *scale_m)
{
    //Table 38 – Specification of the scaling matrix identifier variable id according to predMode, cIdx, nTbW, and nTbH
    const int ids[2][3][6] = {
        {
            {  0,  2,  8, 14, 20, 26 },
            {  0,  3,  9, 15, 21, 21 },
            {  0,  4, 10, 16, 22, 22 }
        },
        {
            {  0,  5, 11, 17, 23, 27 },
            {  0,  6, 12, 18, 24, 24 },
            {  1,  7, 13, 19, 25, 25 },
        }
    };
    const VVCFrameParamSets *ps     = &lc->fc->ps;
    const VVCSPS *sps               = ps->sps;
    const H266RawSliceHeader *rsh   = lc->sc->sh.r;
    const CodingUnit *cu            = lc->cu;
    const VVCScalingList *sl        = ps->sl;
    const int id = ids[cu->pred_mode != MODE_INTRA][tb->c_idx][FFMAX(tb->log2_tb_height, tb->log2_tb_width) - 1];
    const int log2_matrix_size = (id < 2) ? 1 : (id < 8) ? 2 : 3;
    uint8_t *p = scale_m;

    av_assert0(!sps->r->sps_scaling_matrix_for_alternative_colour_space_disabled_flag);

    if (!rsh->sh_explicit_scaling_list_used_flag || tb->ts ||
        sps->r->sps_scaling_matrix_for_lfnst_disabled_flag && cu->apply_lfnst_flag[tb->c_idx])
        return ff_vvc_default_scale_m;

    if (!sl) {
        av_log(lc->fc->log_ctx, AV_LOG_WARNING, "bug: no scaling list aps, id = %d", ps->ph.r->ph_scaling_list_aps_id);
        return ff_vvc_default_scale_m;
    }

    for (int y = tb->min_scan_y; y <= tb->max_scan_y; y++) {
        const int off = y << log2_matrix_size >> tb->log2_tb_height << log2_matrix_size;
        const uint8_t *m = &sl->scaling_matrix_rec[id][off];

        for (int x = tb->min_scan_x; x <= tb->max_scan_x; x++)
            *p++ = m[x << log2_matrix_size >> tb->log2_tb_width];
    }
    if (id >= SL_START_16x16 && !tb->min_scan_x && !tb->min_scan_y)
        *scale_m = sl->scaling_matrix_dc_rec[id - SL_START_16x16];

    return scale_m;
}

//8.7.3 Scaling process for transform coefficients
static av_always_inline int scale_coeff(const TransformBlock *tb, int coeff,
    const int scale, const int scale_m, const int log2_transform_range)
{
    coeff = ((int64_t) coeff * scale * scale_m + tb->bd_offset) >> tb->bd_shift;
    coeff = av_clip_intp2(coeff, log2_transform_range);
    return coeff;
}

static void dequant(const VVCLocalContext *lc, const TransformUnit *tu, TransformBlock *tb)
{
    uint8_t tmp[MAX_TB_SIZE * MAX_TB_SIZE];
    const H266RawSliceHeader *rsh   = lc->sc->sh.r;
    const VVCSPS *sps               = lc->fc->ps.sps;
    const uint8_t *scale_m          = derive_scale_m(lc, tb, tmp);
    int scale;

    derive_qp(lc, tu, tb);
    scale = derive_scale(tb, rsh->sh_dep_quant_used_flag);

    for (int y = tb->min_scan_y; y <= tb->max_scan_y; y++) {
        for (int x = tb->min_scan_x; x <= tb->max_scan_x; x++) {
            int *coeff = tb->coeffs + y * tb->tb_width + x;

            if (*coeff)
                *coeff = scale_coeff(tb, *coeff, scale, *scale_m, sps->log2_transform_range);
            scale_m++;
        }
    }
}

//transmatrix[0][0]
#define DCT_A 64
static void itx_2d(const VVCFrameContext *fc, TransformBlock *tb, const enum VVCTxType trh, const enum VVCTxType trv)
{
    const VVCSPS *sps   = fc->ps.sps;
    const int w         = tb->tb_width;
    const int h         = tb->tb_height;
    const size_t nzw    = tb->max_scan_x + 1;
    const size_t nzh    = tb->max_scan_y + 1;
    const int shift[]   = { 7, 5 + sps->log2_transform_range - sps->bit_depth };

    if (w == h && nzw == 1 && nzh == 1 && trh == VVC_DCT2 && trv == VVC_DCT2) {
        const int add[] = { 1 << (shift[0] - 1), 1 << (shift[1] - 1) };
        const int t     = (tb->coeffs[0] * DCT_A + add[0]) >> shift[0];
        const int dc    = (t * DCT_A + add[1]) >> shift[1];

        for (int i = 0; i < w * h; i++)
            tb->coeffs[i] = dc;

        return;
    }

    for (int x = 0; x < nzw; x++)
        fc->vvcdsp.itx.itx[trv][tb->log2_tb_height - 1](tb->coeffs + x, w, nzh);
    scale_clip(tb->coeffs, nzw, w, h, shift[0], sps->log2_transform_range);

    for (int y = 0; y < h; y++)
        fc->vvcdsp.itx.itx[trh][tb->log2_tb_width - 1](tb->coeffs + y * w, 1, nzw);
    scale(tb->coeffs, tb->coeffs, w, h, shift[1]);
}

static void itx_1d(const VVCFrameContext *fc, TransformBlock *tb, const enum VVCTxType trh, const enum VVCTxType trv)
{
    const VVCSPS *sps   = fc->ps.sps;
    const int w         = tb->tb_width;
    const int h         = tb->tb_height;
    const size_t nzw    = tb->max_scan_x + 1;
    const size_t nzh    = tb->max_scan_y + 1;

    if ((w > 1 && nzw == 1 && trh == VVC_DCT2) || (h > 1 && nzh == 1 && trv == VVC_DCT2)) {
        const int shift = 6 + sps->log2_transform_range - sps->bit_depth;
        const int add   = 1 << (shift - 1);
        const int dc    = (tb->coeffs[0] * DCT_A + add) >> shift;

        for (int i = 0; i < w * h; i++)
            tb->coeffs[i] = dc;

        return;
    }

    if (w > 1)
        fc->vvcdsp.itx.itx[trh][tb->log2_tb_width - 1](tb->coeffs, 1, nzw);
    else
        fc->vvcdsp.itx.itx[trv][tb->log2_tb_height - 1](tb->coeffs, 1, nzh);
    scale(tb->coeffs, tb->coeffs, w, h, 6 + sps->log2_transform_range - sps->bit_depth);
}

static void transform_bdpcm(TransformBlock *tb, const VVCLocalContext *lc, const CodingUnit *cu)
{
    const VVCSPS *sps        = lc->fc->ps.sps;
    const IntraPredMode mode = tb->c_idx ? cu->intra_pred_mode_c : cu->intra_pred_mode_y;
    const int vertical       = mode == INTRA_VERT;
    lc->fc->vvcdsp.itx.transform_bdpcm(tb->coeffs, tb->tb_width, tb->tb_height,
                                       vertical, sps->log2_transform_range);
    if (vertical)
        tb->max_scan_y = tb->tb_height - 1;
    else
        tb->max_scan_x = tb->tb_width - 1;
}

static void itransform(VVCLocalContext *lc, TransformUnit *tu, const int tu_idx, const int target_ch_type)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCSPS *sps           = fc->ps.sps;
    const VVCSH *sh             = &lc->sc->sh;
    const CodingUnit *cu        = lc->cu;
    const int ps                = fc->ps.sps->pixel_shift;
    DECLARE_ALIGNED(32, int, temp)[MAX_TB_SIZE * MAX_TB_SIZE];

    for (int i = 0; i < tu->nb_tbs; i++) {
        TransformBlock *tb  = &tu->tbs[i];
        const int c_idx     = tb->c_idx;
        const int ch_type   = c_idx > 0;

        if (ch_type == target_ch_type && tb->has_coeffs) {
            const int w             = tb->tb_width;
            const int h             = tb->tb_height;
            const int chroma_scale  = ch_type && sh->r->sh_lmcs_used_flag && fc->ps.ph.r->ph_chroma_residual_scale_flag && (w * h > 4);
            const ptrdiff_t stride  = fc->frame->linesize[c_idx];
            const int hs            = sps->hshift[c_idx];
            const int vs            = sps->vshift[c_idx];
            uint8_t *dst            = &fc->frame->data[c_idx][(tb->y0 >> vs) * stride + ((tb->x0 >> hs) << ps)];

            if (cu->bdpcm_flag[tb->c_idx])
                transform_bdpcm(tb, lc, cu);
            dequant(lc, tu, tb);
            if (!tb->ts) {
                enum VVCTxType trh, trv;

                if (cu->apply_lfnst_flag[c_idx])
                    ilfnst_transform(lc, tb);
                derive_transform_type(fc, lc, tb, &trh, &trv);
                if (w > 1 && h > 1)
                    itx_2d(fc, tb, trh, trv);
                else
                    itx_1d(fc, tb, trh, trv);
            }

            if (chroma_scale)
                fc->vvcdsp.intra.lmcs_scale_chroma(lc, temp, tb->coeffs, w, h, cu->x0, cu->y0);
            // TODO: Address performance issue here by combining transform, lmcs_scale_chroma, and add_residual into one function.
            // Complete this task before implementing ASM code.
            fc->vvcdsp.itx.add_residual(dst, chroma_scale ? temp : tb->coeffs, w, h, stride);

            if (tu->joint_cbcr_residual_flag && tb->c_idx)
                add_residual_for_joint_coding_chroma(lc, tu, tb, chroma_scale);
        }
    }
}

static int reconstruct(VVCLocalContext *lc)
{
    VVCFrameContext *fc = lc->fc;
    CodingUnit *cu      = lc->cu;
    const int start     = cu->tree_type == DUAL_TREE_CHROMA;
    const int end       = fc->ps.sps->r->sps_chroma_format_idc && (cu->tree_type != DUAL_TREE_LUMA);

    for (int ch_type = start; ch_type <= end; ch_type++) {
        TransformUnit *tu = cu->tus.head;
        for (int i = 0; tu; i++) {
            predict_intra(lc, tu, i, ch_type);
            itransform(lc, tu, i, ch_type);
            tu = tu->next;
        }
    }
    return 0;
}

#define POS(c_idx, x, y)    \
    &fc->frame->data[c_idx][((y) >> fc->ps.sps->vshift[c_idx]) * fc->frame->linesize[c_idx] +   \
        (((x) >> fc->ps.sps->hshift[c_idx]) << fc->ps.sps->pixel_shift)]

#define IBC_POS(c_idx, x, y) \
    (fc->tab.ibc_vir_buf[c_idx] + \
        (x << ps) + (y + ((cu->y0 & ~(sps->ctb_size_y - 1)) >> vs)) * ibc_stride)
#define IBC_X(x)  ((x) & ((fc->tab.sz.ibc_buffer_width >> hs) - 1))
#define IBC_Y(y)  ((y) & ((1 << sps->ctb_log2_size_y >> vs) - 1))

static void intra_block_copy(const VVCLocalContext *lc, const int c_idx)
{
    const CodingUnit *cu      = lc->cu;
    const PredictionUnit *pu  = &cu->pu;
    const VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps         = fc->ps.sps;
    const Mv *bv              = &pu->mi.mv[L0][0];
    const int hs              = sps->hshift[c_idx];
    const int vs              = sps->vshift[c_idx];
    const int ps              = sps->pixel_shift;
    const int ref_x           = IBC_X((cu->x0 >> hs) + (bv->x >> (4 + hs)));
    const int ref_y           = IBC_Y((cu->y0 >> vs) + (bv->y >> (4 + vs)));
    const int w               = cu->cb_width >> hs;
    const int h               = cu->cb_height >> vs;
    const int ibc_buf_width   = fc->tab.sz.ibc_buffer_width >> hs;    ///< IbcBufWidthY and IbcBufWidthC
    const int rw              = FFMIN(w, ibc_buf_width - ref_x);
    const int ibc_stride      = ibc_buf_width << ps;
    const int dst_stride      = fc->frame->linesize[c_idx];
    const uint8_t *ibc_buf    = IBC_POS(c_idx, ref_x, ref_y);
    uint8_t *dst              = POS(c_idx, cu->x0, cu->y0);

    av_image_copy_plane(dst, dst_stride, ibc_buf, ibc_stride, rw << ps, h);

    if (w > rw) {
        //wrap around, left part
        ibc_buf = IBC_POS(c_idx, 0, ref_y);
        dst  += rw << ps;
        av_image_copy_plane(dst, dst_stride, ibc_buf, ibc_stride, (w - rw) << ps, h);
    }
}

static void vvc_predict_ibc(const VVCLocalContext *lc)
{
    const H266RawSPS *rsps = lc->fc->ps.sps->r;

    intra_block_copy(lc, LUMA);
    if (lc->cu->tree_type == SINGLE_TREE && rsps->sps_chroma_format_idc) {
        intra_block_copy(lc, CB);
        intra_block_copy(lc, CR);
    }
}

static void ibc_fill_vir_buf(const VVCLocalContext *lc, const CodingUnit *cu)
{
    const VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps         = fc->ps.sps;
    const int has_chroma      = sps->r->sps_chroma_format_idc && cu->tree_type != DUAL_TREE_LUMA;
    const int start           = cu->tree_type == DUAL_TREE_CHROMA;
    const int end             = has_chroma ? CR : LUMA;

    for (int c_idx = start; c_idx <= end; c_idx++) {
        const int hs = sps->hshift[c_idx];
        const int vs = sps->vshift[c_idx];
        const int ps = sps->pixel_shift;
        const int x  = IBC_X(cu->x0 >> hs);
        const int y  = IBC_Y(cu->y0 >> vs);
        const int src_stride = fc->frame->linesize[c_idx];
        const int ibc_stride = fc->tab.sz.ibc_buffer_width >> hs << ps;
        const uint8_t *src   = POS(c_idx, cu->x0, cu->y0);
        uint8_t *ibc_buf     = IBC_POS(c_idx, x, y);

        av_image_copy_plane(ibc_buf, ibc_stride, src, src_stride, cu->cb_width >> hs << ps , cu->cb_height >> vs);
    }
}

int ff_vvc_reconstruct(VVCLocalContext *lc, const int rs, const int rx, const int ry)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCSPS *sps           = fc->ps.sps;
    const int x_ctb             = rx << sps->ctb_log2_size_y;
    const int y_ctb             = ry << sps->ctb_log2_size_y;
    CodingUnit *cu              = fc->tab.cus[rs];
    int ret                     = 0;

    lc->num_ras[0] = lc->num_ras[1] = 0;
    lc->lmcs.x_vpdu = -1;
    lc->lmcs.y_vpdu = -1;
    ff_vvc_decode_neighbour(lc, x_ctb, y_ctb, rx, ry, rs);
    while (cu) {
        lc->cu = cu;

        if (cu->ciip_flag)
            ff_vvc_predict_ciip(lc);
        else if (cu->pred_mode == MODE_IBC)
            vvc_predict_ibc(lc);
        if (cu->coded_flag) {
            ret = reconstruct(lc);
        } else {
            if (cu->tree_type != DUAL_TREE_CHROMA)
                add_reconstructed_area(lc, LUMA, cu->x0, cu->y0, cu->cb_width, cu->cb_height);
            if (sps->r->sps_chroma_format_idc && cu->tree_type != DUAL_TREE_LUMA)
                add_reconstructed_area(lc, CHROMA, cu->x0, cu->y0, cu->cb_width, cu->cb_height);
        }
        if (sps->r->sps_ibc_enabled_flag)
            ibc_fill_vir_buf(lc, cu);
        cu = cu->next;
    }
    ff_vvc_ctu_free_cus(fc->tab.cus + rs);
    return ret;
}

