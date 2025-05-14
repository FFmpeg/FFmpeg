/*
 * VVC intra prediction DSP
 *
 * Copyright (C) 2021-2023 Nuomi
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

#include "libavcodec/bit_depth_template.c"

#include "intra.h"

#define POS(x, y) src[(x) + stride * (y)]

static av_always_inline void FUNC(cclm_linear_pred)(VVCFrameContext *fc, const int x0, const int y0,
    const int w, const int h, const pixel* pdsy, const int *a, const int *b, const int *k)
{
    const VVCSPS *sps = fc->ps.sps;
    for (int i = 0; i < VVC_MAX_SAMPLE_ARRAYS - 1; i++) {
        const int c_idx = i + 1;
        const int x = x0 >> sps->hshift[c_idx];
        const int y = y0 >> sps->vshift[c_idx];
        const ptrdiff_t stride = fc->frame->linesize[c_idx] / sizeof(pixel);
        pixel *src = (pixel*)fc->frame->data[c_idx] + x + y * stride;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const int dsy = pdsy[y * w + x];
                const int pred = ((dsy * a[i]) >> k[i]) + b[i];
                POS(x, y) = CLIP(pred);
            }
        }
    }
}

#define MAX_PICK_POS 4
#define TOP  0
#define LEFT 1

static av_always_inline void FUNC(cclm_get_params_default)(int *a, int *b, int *k)
{
    for (int i = 0; i < 2; i++) {
        a[i] = k[i] = 0;
        b[i] = 1 << (BIT_DEPTH - 1);
    }
}

static av_always_inline int FUNC(cclm_get_select_pos)(const VVCLocalContext *lc,
    const int x, const int y, const int w, const int h, const int avail_t, const int avail_l,
    int cnt[2], int pos[2][MAX_PICK_POS])
{
    const enum IntraPredMode mode = lc->cu->intra_pred_mode_c;
    const int num_is4 = !avail_t || !avail_l || mode != INTRA_LT_CCLM;
    int num_samp[2];

    if (mode == INTRA_LT_CCLM) {
        num_samp[TOP]  = avail_t ? w : 0;
        num_samp[LEFT] = avail_l ? h : 0;
    } else {
        num_samp[TOP] = (avail_t && mode == INTRA_T_CCLM) ? ff_vvc_get_top_available(lc,  x, y, w + FFMIN(w, h), 1) : 0;
        num_samp[LEFT] = (avail_l && mode == INTRA_L_CCLM) ? ff_vvc_get_left_available(lc, x, y, h + FFMIN(w, h), 1) : 0;
    }
    if (!num_samp[TOP] && !num_samp[LEFT]) {
        return 0;
    }
    for (int i = TOP; i <= LEFT; i++) {
        const int start = num_samp[i] >> (2 + num_is4);
        const int step  = FFMAX(1, num_samp[i] >> (1 + num_is4)) ;
        cnt[i] = FFMIN(num_samp[i], (1 + num_is4) << 1);
        for (int c = 0; c < cnt[i]; c++)
            pos[i][c] = start + c * step;
    }
    return 1;
}

static av_always_inline void FUNC(cclm_select_luma_444)(const pixel *src, const int step,
    const int cnt, const int pos[MAX_PICK_POS],  pixel *sel_luma)
{
    for (int i = 0; i < cnt; i++)
        sel_luma[i] = src[pos[i] * step];
}

static av_always_inline void FUNC(cclm_select_luma)(const VVCFrameContext *fc,
    const int x0, const int y0, const int avail_t, const int avail_l, const int cnt[2], const int pos[2][MAX_PICK_POS],
    pixel *sel_luma)
{
    const VVCSPS *sps = fc->ps.sps;

    const int b_ctu_boundary = !av_zero_extend(y0, sps->ctb_log2_size_y);
    const int hs = sps->hshift[1];
    const int vs = sps->vshift[1];
    const ptrdiff_t stride = fc->frame->linesize[0] / sizeof(pixel);

    if (!hs && !vs) {
        const pixel* src = (pixel*)fc->frame->data[0] + x0 + y0 * stride;
        FUNC(cclm_select_luma_444)(src - avail_t * stride, 1, cnt[TOP], pos[TOP], sel_luma);
        FUNC(cclm_select_luma_444)(src - avail_l, stride, cnt[LEFT], pos[LEFT], sel_luma + cnt[TOP]);
    } else {
        // top
        if (vs && !b_ctu_boundary) {
            const pixel *source = (pixel *)fc->frame->data[0] + x0 + (y0 - 2) * stride;
            for (int i = 0; i < cnt[TOP]; i++) {
                const int x = pos[TOP][i] << hs;
                const pixel *src = source + x;
                const int has_left = x || avail_l;
                const pixel l = has_left ? POS(-1, 0) : POS(0, 0);
                if (sps->r->sps_chroma_vertical_collocated_flag) {
                    sel_luma[i] = (POS(0, -1) + l + 4 * POS(0, 0) + POS(1, 0) + POS(0, 1) + 4) >> 3;
                } else {
                    const pixel l1 = has_left ? POS(-1, 1) : POS(0, 1);
                    sel_luma[i] = (l + l1 + 2 * (POS(0, 0) + POS(0, 1)) + POS(1, 0) + POS(1, 1) + 4) >> 3;
                }
            }
        } else {
            const pixel *source = (pixel*)fc->frame->data[0] + x0 + (y0 - 1) * stride;
            for (int i = 0; i < cnt[TOP]; i++) {
                const int x = pos[TOP][i] << hs;
                const pixel *src = source + x;
                const int has_left = x || avail_l;
                const pixel l = has_left ? POS(-1, 0) : POS(0, 0);
                sel_luma[i] = (l + 2 * POS(0, 0) + POS(1, 0) + 2) >> 2;
            }
        }

        // left
        {
            const pixel *left;
            const pixel *source = (pixel *)fc->frame->data[0] + x0 + y0 * stride - (1 + hs) * avail_l;
            left = source - avail_l;

            for (int i = 0; i < cnt[LEFT]; i++) {
                const int y = pos[LEFT][i] << vs;
                const int offset = y * stride;
                const pixel *l   = left + offset;
                const pixel *src = source + offset;
                pixel pred;
                if (!vs) {
                    pred = (*l + 2 * POS(0, 0) + POS(1, 0) + 2) >> 2;
                } else {
                    if (sps->r->sps_chroma_vertical_collocated_flag) {
                        const int has_top = y || avail_t;
                        const pixel t = has_top ? POS(0, -1) : POS(0, 0);
                        pred = (*l + t + 4 * POS(0, 0) + POS(1, 0) + POS(0, 1) + 4) >> 3;
                    } else {
                        pred = (*l + *(l + stride) + 2 * POS(0, 0) + 2 * POS(0, 1) + POS(1, 0) + POS(1, 1) + 4) >> 3;
                    }
                }
                sel_luma[i + cnt[TOP]] = pred;
            }
        }
    }
}

static av_always_inline void FUNC(cclm_select_chroma)(const VVCFrameContext *fc,
    const int x, const int y, const int cnt[2], const int pos[2][MAX_PICK_POS],
    pixel sel[][MAX_PICK_POS * 2])
{
    for (int c_idx = 1; c_idx < VVC_MAX_SAMPLE_ARRAYS; c_idx++) {
        const ptrdiff_t stride = fc->frame->linesize[c_idx] / sizeof(pixel);

        //top
        const pixel *src = (pixel*)fc->frame->data[c_idx] + x + (y - 1)* stride;
        for (int i = 0; i < cnt[TOP]; i++) {
            sel[c_idx][i] = src[pos[TOP][i]];
        }

        //left
        src = (pixel*)fc->frame->data[c_idx] + x - 1 + y * stride;
        for (int i = 0; i < cnt[LEFT]; i++) {
            sel[c_idx][i + cnt[TOP]] = src[pos[LEFT][i] * stride];
        }
    }
}

static av_always_inline int FUNC(cclm_select_samples)(const VVCLocalContext *lc,
    const int x0, const int y0, const int w, const int h, const int avail_t, const int avail_l,
    pixel sel[][MAX_PICK_POS * 2])
{
    const VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps   = fc->ps.sps;
    const int x  = x0 >> sps->hshift[1];
    const int y  = y0 >> sps->vshift[1];
    int cnt[2], pos[2][MAX_PICK_POS];

    if (!FUNC(cclm_get_select_pos)(lc, x, y, w, h, avail_t, avail_l, cnt, pos))
        return 0;

    FUNC(cclm_select_luma)(fc, x0, y0, avail_t, avail_l, cnt, pos, sel[LUMA]);
    FUNC(cclm_select_chroma)(fc, x, y, cnt, pos, sel);

    if (cnt[TOP] + cnt[LEFT] == 2) {
        for (int c_idx = 0; c_idx < VVC_MAX_SAMPLE_ARRAYS; c_idx++) {
            sel[c_idx][3] = sel[c_idx][0];
            sel[c_idx][2] = sel[c_idx][1];
            sel[c_idx][0] = sel[c_idx][1];
            sel[c_idx][1] = sel[c_idx][3];
        }
    }
    return 1;
}

static av_always_inline void FUNC(cclm_get_min_max)(
    const pixel sel[][MAX_PICK_POS * 2], int *min, int *max)
{
    int min_grp_idx[] = { 0, 2 };
    int max_grp_idx[] = { 1, 3 };

    if (sel[LUMA][min_grp_idx[0]] > sel[LUMA][min_grp_idx[1]])
        FFSWAP(int, min_grp_idx[0], min_grp_idx[1]);
    if (sel[LUMA][max_grp_idx[0]] > sel[LUMA][max_grp_idx[1]])
        FFSWAP(int, max_grp_idx[0], max_grp_idx[1]);
    if (sel[LUMA][min_grp_idx[0]] > sel[LUMA][max_grp_idx[1]]) {
        FFSWAP(int, min_grp_idx[0], max_grp_idx[0]);
        FFSWAP(int, min_grp_idx[1], max_grp_idx[1]);
    }
    if (sel[LUMA][min_grp_idx[1]] > sel[LUMA][max_grp_idx[0]])
        FFSWAP(int, min_grp_idx[1], max_grp_idx[0]);
    for (int c_idx = 0; c_idx < VVC_MAX_SAMPLE_ARRAYS; c_idx++) {
        max[c_idx] = (sel[c_idx][max_grp_idx[0]] + sel[c_idx][max_grp_idx[1]] + 1) >> 1;
        min[c_idx] = (sel[c_idx][min_grp_idx[0]] + sel[c_idx][min_grp_idx[1]] + 1) >> 1;
    }
}

static av_always_inline void FUNC(cclm_get_params)(const VVCLocalContext *lc,
    const int x0, const int y0, const int w, const int h, const int avail_t, const int avail_l,
    int *a, int *b, int *k)
{
    pixel sel[VVC_MAX_SAMPLE_ARRAYS][MAX_PICK_POS * 2];
    int max[VVC_MAX_SAMPLE_ARRAYS], min[VVC_MAX_SAMPLE_ARRAYS];
    int diff;

    if (!FUNC(cclm_select_samples)(lc, x0, y0, w, h, avail_t, avail_l, sel)) {
        FUNC(cclm_get_params_default)(a, b, k);
        return;
    }

    FUNC(cclm_get_min_max)(sel, min, max);

    diff = max[LUMA] - min[LUMA];
    if (diff == 0) {
        for (int i = 0; i < 2; i++) {
            a[i] = k[i] = 0;
            b[i] = min[i + 1];
        }
        return;
    }
    for (int i = 0; i < 2; i++) {
        const static int div_sig_table[] = {0, 7, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1, 1, 1, 0};
        const int diffc = max[i + 1] - min[i + 1];
        int  x = av_log2(diff);
        int  y, v, sign, add;
        const int norm_diff = ((diff << 4) >> x) & 15;
        x += (norm_diff) ? 1 : 0;
        y = abs(diffc) > 0 ? av_log2(abs(diffc)) + 1 : 0;
        v = div_sig_table[norm_diff] | 8;
        add = (1 << y >> 1);
        a[i] = (diffc * v + add) >> y;
        k[i] = FFMAX(1, 3 + x -y);
        sign = a[i] < 0 ? -1 : (a[i] > 0);
        a[i] = ((3 + x - y) < 1) ?  sign * 15 : a[i];
        b[i] = min[i + 1] - ((a[i] * min[0]) >> k[i]);
    }

}

#undef TOP
#undef LEFT

static av_always_inline void FUNC(cclm_get_luma_rec_pixels)(const VVCFrameContext *fc,
    const int x0, const int y0, const int w, const int h, const int avail_t, const int avail_l,
    pixel *pdsy)
{
    const int hs            = fc->ps.sps->hshift[1];
    const int vs            = fc->ps.sps->vshift[1];
    const ptrdiff_t stride  = fc->frame->linesize[0] / sizeof(pixel);
    const pixel *source     = (pixel*)fc->frame->data[0] + x0 + y0 * stride;
    const pixel *left       = source - avail_l;
    const pixel *top        = source - avail_t * stride;

    const VVCSPS *sps = fc->ps.sps;
    if (!hs && !vs) {
        for (int i = 0; i < h; i++)
            memcpy(pdsy + i * w, source + i * stride, w * sizeof(pixel));
        return;
    }
    for (int i = 0; i < h; i++) {
        const pixel *src  = source;
        const pixel *l = left;
        const pixel *t = top;
        if (!vs) {
            for (int j = 0; j < w; j++) {
                pixel pred  = (*l + 2 * POS(0, 0) + POS(1, 0) + 2) >> 2;
                pdsy[i * w + j] = pred;
                src += 2;
                l = src - 1;
            }

        } else {
            if (sps->r->sps_chroma_vertical_collocated_flag)  {
                for (int j = 0; j < w; j++) {
                    pixel pred  = (*l + *t + 4 * POS(0, 0) + POS(1, 0) + POS(0, 1) + 4) >> 3;
                    pdsy[i * w + j] = pred;
                    src += 2;
                    t += 2;
                    l = src - 1;
                }
            } else {
                for (int j = 0; j < w; j++) {
                    pixel pred  = (*l + *(l + stride) + 2 * POS(0, 0) + 2 * POS(0, 1) + POS(1, 0) + POS(1, 1) + 4) >> 3;

                    pdsy[i * w + j] = pred;
                    src += 2;
                    l = src - 1;
                }
            }
        }
        source += (stride << vs);
        left   += (stride << vs);
        top    = source - stride;
    }
}

static av_always_inline void FUNC(cclm_pred_default)(VVCFrameContext *fc,
    const int x, const int y, const int w, const int h, const int avail_t, const int avail_l)
{
    for (int c_idx = 1; c_idx < VVC_MAX_SAMPLE_ARRAYS; c_idx++) {
        const ptrdiff_t stride = fc->frame->linesize[c_idx] / sizeof(pixel);
        pixel *dst = (pixel*)fc->frame->data[c_idx] + x + y * stride;
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                dst[j] = 1 << (BIT_DEPTH - 1);
            }
            dst += stride;
        }
    }
}

//8.4.5.2.14 Specification of INTRA_LT_CCLM, INTRA_L_CCLM and INTRA_T_CCLM intra prediction mode
static void FUNC(intra_cclm_pred)(const VVCLocalContext *lc, const int x0, const int y0,
    const int width, const int height)
{
    VVCFrameContext *fc     = lc->fc;
    const VVCSPS *sps = fc->ps.sps;
    const int avail_t = ff_vvc_get_top_available(lc, x0, y0, 1, 0);
    const int avail_l = ff_vvc_get_left_available(lc, x0, y0, 1, 0);
    const int hs = sps->hshift[1];
    const int vs = sps->vshift[1];
    const int x  = x0 >> hs;
    const int y  = y0 >> vs;
    const int w  = width >> hs;
    const int h  = height >> vs;
    int a[2], b[2], k[2];

    pixel dsy[MAX_TB_SIZE * MAX_TB_SIZE];
    if (!avail_t && !avail_l) {
        FUNC(cclm_pred_default)(fc, x, y, w, h, avail_t, avail_l);
        return;
    }
    FUNC(cclm_get_luma_rec_pixels)(fc, x0, y0, w, h, avail_t, avail_l, dsy);
    FUNC(cclm_get_params) (lc, x0, y0, w, h, avail_t, avail_l, a, b, k);
    FUNC(cclm_linear_pred)(fc, x0, y0, w, h, dsy, a, b, k);
}

static int FUNC(lmcs_sum_samples)(const pixel *start, ptrdiff_t stride, const int avail, const int target_size)
{
    const int size = FFMIN(avail, target_size);
    int sum = 0;
    for (int i = 0; i < size; i++) {
        sum += *start;
        start += stride;
    }
    sum += *(start - stride) * (target_size - size);
    return sum;
}

// 8.7.5.3 Picture reconstruction with luma dependent chroma residual scaling process for chroma samples
static int FUNC(lmcs_derive_chroma_scale)(VVCLocalContext *lc, const int x0, const int y0)
{
    VVCFrameContext *fc = lc->fc;
    const VVCLMCS *lmcs = &fc->ps.lmcs;
    const int size_y    = FFMIN(fc->ps.sps->ctb_size_y, 64);

    const int x = x0 & ~(size_y - 1);
    const int y = y0 & ~(size_y - 1);
    if (lc->lmcs.x_vpdu != x || lc->lmcs.y_vpdu != y) {
        int cnt = 0, luma = 0, i;
        const pixel *src = (const pixel *)(fc->frame->data[LUMA] + y * fc->frame->linesize[LUMA] + (x << fc->ps.sps->pixel_shift));
        const ptrdiff_t stride = fc->frame->linesize[LUMA] / sizeof(pixel);
        const int avail_t = ff_vvc_get_top_available (lc, x, y, 1, 0);
        const int avail_l = ff_vvc_get_left_available(lc, x, y, 1, 0);
        if (avail_l) {
            luma += FUNC(lmcs_sum_samples)(src - 1, stride, fc->ps.pps->height - y, size_y);
            cnt = size_y;
        }
        if (avail_t) {
            luma += FUNC(lmcs_sum_samples)(src - stride, 1, fc->ps.pps->width - x, size_y);
            cnt += size_y;
        }
        if (cnt)
            luma = (luma + (cnt >> 1)) >> av_log2(cnt);
        else
            luma = 1 << (BIT_DEPTH - 1);

        for (i = lmcs->min_bin_idx; i <= lmcs->max_bin_idx; i++) {
            if (luma < lmcs->pivot[i + 1])
                break;
        }
        i = FFMIN(i, LMCS_MAX_BIN_SIZE - 1);

        lc->lmcs.chroma_scale = lmcs->chroma_scale_coeff[i];
        lc->lmcs.x_vpdu = x;
        lc->lmcs.y_vpdu = y;
    }
    return lc->lmcs.chroma_scale;
}

// 8.7.5.3 Picture reconstruction with luma dependent chroma residual scaling process for chroma samples
static void FUNC(lmcs_scale_chroma)(VVCLocalContext *lc, int *coeff,
    const int width, const int height, const int x0_cu, const int y0_cu)
{
    const int chroma_scale = FUNC(lmcs_derive_chroma_scale)(lc, x0_cu, y0_cu);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const int c = av_clip_intp2(*coeff, BIT_DEPTH);

            if (c > 0)
                *coeff = (c * chroma_scale + (1 << 10)) >> 11;
            else
                *coeff = -((-c * chroma_scale + (1 << 10)) >> 11);
            coeff++;
        }
    }
}

static av_always_inline void FUNC(ref_filter)(const pixel *left, const pixel *top,
    pixel *filtered_left, pixel *filtered_top, const int left_size, const int top_size,
    const int unfilter_last_one)
{
    filtered_left[-1] = filtered_top[-1] = (left[0] +  2 * left[-1] + top[0] + 2 ) >> 2;
    for (int i = 0; i < left_size - unfilter_last_one; i++) {
        filtered_left[i] = (left[i- 1] + 2 * left[i] + left[i + 1] + 2) >> 2;
    }
    for (int i = 0; i < top_size - unfilter_last_one; i++) {
        filtered_top[i] = (top[i-1] + 2 * top[i] + top[i + 1] + 2) >> 2;
    }
    if (unfilter_last_one) {
        filtered_top[top_size - 1] = top[top_size - 1];
        filtered_left[left_size - 1] = left[left_size - 1];
    }
}

static av_always_inline void FUNC(prepare_intra_edge_params)(const VVCLocalContext *lc,
    IntraEdgeParams* edge, const pixel *src, const ptrdiff_t stride,
    const int x, int y, int w, int h, int c_idx, const int is_intra_mip,
    const int mode, const int ref_idx, const int need_pdpc)
{
#define EXTEND(ptr, val, len)         \
do {                                  \
    for (i = 0; i < (len); i++)       \
        *(ptr + i) = val;             \
} while (0)
    const CodingUnit *cu = lc->cu;
    const int ref_filter_flag = is_intra_mip ? 0 : ff_vvc_ref_filter_flag_derive(mode);
    const int filter_flag = !ref_idx && w * h > 32 && !c_idx &&
        cu->isp_split_type == ISP_NO_SPLIT && ref_filter_flag;
    int cand_up_left      = lc->na.cand_up_left;
    pixel  *left          = (pixel*)edge->left_array + MAX_TB_SIZE + 3;
    pixel  *top           = (pixel*)edge->top_array  + MAX_TB_SIZE + 3;
    pixel  *filtered_left = (pixel*)edge->filtered_left_array + MAX_TB_SIZE + 3;
    pixel  *filtered_top  = (pixel*)edge->filtered_top_array  + MAX_TB_SIZE + 3;
    const int ref_line = ref_idx == 3 ? -4 : (-1 - ref_idx);
    int left_size, top_size, unfilter_left_size, unfilter_top_size;
    int left_available, top_available;
    int refw, refh;
    int intra_pred_angle, inv_angle;
    int i;

    if (is_intra_mip || mode == INTRA_PLANAR) {
        left_size = h + 1;
        top_size  = w + 1;
        unfilter_left_size = left_size + filter_flag;
        unfilter_top_size  = top_size  + filter_flag;
    } else if (mode == INTRA_DC) {
        unfilter_left_size = left_size = h;
        unfilter_top_size = top_size  = w;
    } else if (mode == INTRA_VERT) {
        //we may need 1 pixel to predict the top left.
        unfilter_left_size = left_size = need_pdpc ? h : 1;
        unfilter_top_size = top_size  = w;
    } else if (mode == INTRA_HORZ) {
        unfilter_left_size = left_size = h;
        //even need_pdpc == 0, we may need 1 pixel to predict the top left.
        unfilter_top_size = top_size = need_pdpc ? w : 1;
    } else {
        if (cu->isp_split_type == ISP_NO_SPLIT || c_idx) {
            refw = w * 2;
            refh = h * 2;
        } else {
            refw = cu->cb_width + w;
            refh = cu->cb_height + h;
        }
        intra_pred_angle = ff_vvc_intra_pred_angle_derive(mode);
        inv_angle = ff_vvc_intra_inv_angle_derive(intra_pred_angle);
        unfilter_top_size = top_size  = refw;
        unfilter_left_size = left_size = refh;
    }

    left_available = ff_vvc_get_left_available(lc, x, y, unfilter_left_size, c_idx);
    for (i = 0; i < left_available; i++)
        left[i] = POS(ref_line, i);

    top_available = ff_vvc_get_top_available(lc, x, y, unfilter_top_size, c_idx);
    memcpy(top, src + ref_line * stride, top_available * sizeof(pixel));

    for (int i = -1; i >= ref_line; i--) {
        if (cand_up_left) {
            left[i] = POS(ref_line, i);
            top[i]  = POS(i, ref_line);
        } else if (left_available) {
            left[i] = top[i] = left[0];
        } else if (top_available) {
            left[i] = top[i] = top[0];
        } else {
            left[i] = top[i] = 1 << (BIT_DEPTH - 1);
        }
    }

    EXTEND(top + top_available, top[top_available-1], unfilter_top_size - top_available);
    EXTEND(left + left_available, left[left_available-1], unfilter_left_size - left_available);

    if (ref_filter_flag) {
        if (!ref_idx && w * h > 32 && !c_idx && cu->isp_split_type == ISP_NO_SPLIT ) {
            const int unfilter_last_one = left_size == unfilter_left_size;
            FUNC(ref_filter)(left, top, filtered_left, filtered_top, unfilter_left_size, unfilter_top_size, unfilter_last_one);
            left = filtered_left;
            top  = filtered_top;
        }
    }
    if (!is_intra_mip && mode != INTRA_PLANAR && mode != INTRA_DC) {
        if (ref_filter_flag || ref_idx || cu->isp_split_type != ISP_NO_SPLIT) {
            edge->filter_flag = 0;
        } else {
            const int min_dist_ver_hor = FFMIN(abs(mode - 50), abs(mode - 18));
            const int intra_hor_ver_dist_thres[] = {24, 14, 2, 0, 0};
            const int ntbs = (av_log2(w) + av_log2(h)) >> 1;
            edge->filter_flag = min_dist_ver_hor > intra_hor_ver_dist_thres[ntbs - 2];
        }

        if (mode != INTRA_VERT && mode != INTRA_HORZ) {
            if (mode >= INTRA_DIAG) {
                if (intra_pred_angle < 0) {
                    pixel *p = top - (ref_idx + 1);
                    for (int x = -h; x < 0; x++) {
                        const int idx = -1 - ref_idx + FFMIN((x*inv_angle + 256) >> 9, h);
                        p[x] = left[idx];
                    }
                } else {
                    for (int i = refw; i <= refw + FFMAX(1, w/h) * ref_idx + 1; i++)
                        top[i] = top[refw - 1];
                }
            } else {
                if (intra_pred_angle < 0) {
                    pixel *p = left - (ref_idx + 1);
                    for (int x = -w; x < 0; x++) {
                        const int idx = -1 - ref_idx + FFMIN((x*inv_angle + 256) >> 9, w);
                        p[x] = top[idx];
                    }
                } else {
                    for (int i = refh; i <= refh + FFMAX(1, h/w) * ref_idx + 1; i++)
                        left[i] = left[refh - 1];
                }
            }
        }
    }
    edge->left = (uint8_t*)left;
    edge->top  = (uint8_t*)top;
}

//8.4.1 General decoding process for coding units coded in intra prediction mode
static void FUNC(intra_pred)(const VVCLocalContext *lc, int x0, int y0,
    const int width, const int height, int c_idx)
{
    VVCFrameContext *fc     = lc->fc;
    const VVCSPS *sps = fc->ps.sps;
    const VVCPPS *pps = fc->ps.pps;
    const CodingUnit *cu = lc->cu;
    const int log2_min_cb_size    = sps->min_cb_log2_size_y;
    const int min_cb_width        = pps->min_cb_width;
    const int x_cb                = x0 >> log2_min_cb_size;
    const int y_cb                = y0 >> log2_min_cb_size;

    const int hshift = fc->ps.sps->hshift[c_idx];
    const int vshift = fc->ps.sps->vshift[c_idx];
    const int x = x0 >> hshift;
    const int y = y0 >> vshift;
    const int w = width >> hshift;
    const int h = height >> vshift;
    const ptrdiff_t stride = fc->frame->linesize[c_idx] / sizeof(pixel);

    const int pred_mode = c_idx ? cu->intra_pred_mode_c : cu->intra_pred_mode_y;
    const int mode = ff_vvc_wide_angle_mode_mapping(cu, w, h, c_idx, pred_mode);

    const int intra_mip_flag  = SAMPLE_CTB(fc->tab.imf, x_cb, y_cb);
    const int is_intra_mip    = intra_mip_flag && (!c_idx || cu->mip_chroma_direct_flag);
    const int ref_idx = c_idx ? 0 : cu->intra_luma_ref_idx;
    const int need_pdpc = ff_vvc_need_pdpc(w, h, cu->bdpcm_flag[c_idx], mode, ref_idx);


    pixel *src = (pixel*)fc->frame->data[c_idx] + x + y * stride;
    IntraEdgeParams edge;

    FUNC(prepare_intra_edge_params)(lc, &edge, src, stride, x, y, w, h, c_idx, is_intra_mip, mode, ref_idx, need_pdpc);

    if (is_intra_mip) {
        int intra_mip_transposed_flag;
        int intra_mip_mode;
        unpack_mip_info(&intra_mip_transposed_flag, &intra_mip_mode, intra_mip_flag);

        fc->vvcdsp.intra.pred_mip((uint8_t *)src, edge.top, edge.left,
                        w, h, stride, intra_mip_mode, intra_mip_transposed_flag);
    } else if (mode == INTRA_PLANAR) {
        fc->vvcdsp.intra.pred_planar((uint8_t *)src, edge.top, edge.left, w, h, stride);
    } else if (mode == INTRA_DC) {
        fc->vvcdsp.intra.pred_dc((uint8_t *)src, edge.top, edge.left, w, h, stride);
    } else if (mode == INTRA_VERT) {
        fc->vvcdsp.intra.pred_v((uint8_t *)src, edge.top, w, h, stride);
    } else if (mode == INTRA_HORZ) {
        fc->vvcdsp.intra.pred_h((uint8_t *)src, edge.left, w, h, stride);
    } else {
        if (mode >= INTRA_DIAG) {
            fc->vvcdsp.intra.pred_angular_v((uint8_t *)src, edge.top, edge.left,
                                  w, h, stride, c_idx, mode, ref_idx,
                                  edge.filter_flag, need_pdpc);
        } else {
            fc->vvcdsp.intra.pred_angular_h((uint8_t *)src, edge.top, edge.left,
                                  w, h, stride, c_idx, mode, ref_idx,
                                  edge.filter_flag, need_pdpc);
        }
    }
    if (need_pdpc) {
        //8.4.5.2.15 Position-dependent intra prediction sample filtering process
        if (!is_intra_mip && (mode == INTRA_PLANAR || mode == INTRA_DC ||
            mode == INTRA_VERT || mode == INTRA_HORZ)) {
            const int scale = (av_log2(w) + av_log2(h) - 2) >> 2;
            const pixel *left = (pixel*)edge.left;
            const pixel *top  = (pixel*)edge.top;
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    int l, t, wl, wt, pred;
                    pixel val;
                    if (mode == INTRA_PLANAR || mode == INTRA_DC) {
                        l  = left[y];
                        t = top[x];
                        wl = 32 >> FFMIN((x << 1) >> scale, 31);
                        wt = 32 >> FFMIN((y << 1) >> scale, 31);
                    } else {
                        l  = left[y] - left[-1] + POS(x,y);
                        t = top[x] - top[-1] + POS(x,y);
                        wl = (mode == INTRA_VERT) ?  (32 >> FFMIN((x << 1) >> scale, 31)) : 0;
                        wt = (mode == INTRA_HORZ) ?  (32 >> FFMIN((y << 1) >> scale, 31)) : 0;
                    }
                    val = POS(x, y);
                    pred  = val + ((wl * (l - val) + wt * (t - val) + 32) >> 6);
                    POS(x, y) = CLIP(pred);
                }
            }
        }
    }
}

//8.4.5.2.11 Specification of INTRA_PLANAR intra prediction mode
static av_always_inline void FUNC(pred_planar)(uint8_t *_src, const uint8_t *_top,
    const uint8_t *_left, const int w, const int h, const ptrdiff_t stride)
{
    int x, y;
    pixel *src        = (pixel *)_src;
    const pixel *top  = (const pixel *)_top;
    const pixel *left = (const pixel *)_left;
    const int logw  = av_log2(w);
    const int logh  = av_log2(h);
    const int size  =  w * h;
    const int shift = (logw + logh + 1);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            const int pred_v = ((h - 1 - y) * top[x]  + (y + 1) * left[h]) << logw;
            const int pred_h = ((w - 1 - x) * left[y] + (x + 1) * top[w]) << logh;
            const int pred = (pred_v + pred_h + size) >> shift;
            POS(x, y) = pred;
        }
    }
}

//8.4.5.2.3 MIP boundary sample downsampling process
static av_always_inline void FUNC(mip_downsampling)(int *reduced, const int boundary_size,
    const pixel *ref, const int n_tb_s)
{
    const int b_dwn = n_tb_s / boundary_size;
    const int log2 = av_log2(b_dwn);

    if (boundary_size == n_tb_s) {
        for (int i = 0; i < n_tb_s; i++)
            reduced[i] = ref[i];
        return;
    }
    for (int i = 0; i < boundary_size; i++) {
        int r;
        r = *ref++;
        for (int j = 1; j < b_dwn; j++)
            r += *ref++;
        reduced[i] = (r + (1 << (log2 - 1))) >> log2;
    }
}

static av_always_inline void FUNC(mip_reduced_pred)(pixel *src, const ptrdiff_t stride,
    const int up_hor, const int up_ver, const int pred_size, const int *reduced, const int reduced_size,
    const int ow, const int temp0, const uint8_t *matrix, int is_transposed)
{
    src = &POS(up_hor - 1, up_ver - 1);
    for (int y = 0; y < pred_size; y++) {
        for (int x = 0; x < pred_size; x++) {
            int pred = 0;
            for (int i = 0; i < reduced_size; i++)
                pred += reduced[i] * matrix[i];
            matrix += reduced_size;
            pred = ((pred + ow) >> 6) + temp0;
            pred = av_clip(pred, 0, (1<<BIT_DEPTH) - 1);
            if (is_transposed)
                POS(y * up_hor, x * up_ver) = pred;
            else
                POS(x * up_hor, y * up_ver) = pred;
        }
    }
}

static av_always_inline void FUNC(mip_upsampling_1d)(pixel *dst, const int dst_step, const int dst_stride, const int dst_height, const int factor,
    const pixel *boundary, const int boundary_step,  const int pred_size)
{

    for (int i = 0; i < dst_height; i++) {
        const pixel *before = boundary;
        const pixel *after  = dst - dst_step;
        pixel *d = dst;
        for (int j = 0; j < pred_size; j++) {
            after += dst_step * factor;
            for (int k = 1; k < factor; k++) {
                int mid = (factor - k) * (*before) + k * (*after);
                *d = (mid + factor / 2) / factor;
                d += dst_step;
            }
            before = after;
            d += dst_step;
        }
        boundary += boundary_step;
        dst += dst_stride;
    }
}

//8.4.5.2.2 Matrix-based intra sample prediction
static av_always_inline void FUNC(pred_mip)(uint8_t *_src, const uint8_t *_top,
    const uint8_t *_left, const int w, const int h, const ptrdiff_t stride,
    int mode_id, int is_transposed)
{
    pixel *src        = (pixel *)_src;
    const pixel *top  = (const pixel *)_top;
    const pixel *left = (const pixel *)_left;

    const int size_id = ff_vvc_get_mip_size_id(w, h);
    static const int boundary_sizes[] = {2, 4, 4};
    static const int pred_sizes[] = {4, 4, 8};
    const int boundary_size = boundary_sizes[size_id];
    const int pred_size     = pred_sizes[size_id];
    const int in_size = 2 * boundary_size - ((size_id == 2) ? 1 : 0);
    const uint8_t *matrix = ff_vvc_get_mip_matrix(size_id, mode_id);
    const int up_hor = w / pred_size;
    const int up_ver = h / pred_size;

    int reduced[16];
    int *red_t  = reduced;
    int *red_l  = reduced + boundary_size;
    int off = 1, ow = 0;
    int temp0;

    if (is_transposed) {
        FFSWAP(int*, red_t, red_l);
    }
    FUNC(mip_downsampling)(red_t, boundary_size, top, w);
    FUNC(mip_downsampling)(red_l, boundary_size, left, h);

    temp0 = reduced[0];
    if (size_id != 2) {
        off = 0;
        ow = (1 << (BIT_DEPTH - 1)) - temp0;
    } else {
        ow = reduced[1] - temp0;
    }
    reduced[0] = ow;
    for (int i = 1; i < in_size; i++) {
        reduced[i] = reduced[i + off] - temp0;
        ow += reduced[i];
    }
    ow = 32 - 32 * ow;

    FUNC(mip_reduced_pred)(src, stride, up_hor, up_ver, pred_size, reduced, in_size, ow, temp0, matrix, is_transposed);
    if (up_hor > 1 || up_ver > 1) {
        if (up_hor > 1)
            FUNC(mip_upsampling_1d)(&POS(0, up_ver - 1), 1, up_ver * stride, pred_size, up_hor, left + up_ver - 1, up_ver, pred_size);
        if (up_ver > 1)
            FUNC(mip_upsampling_1d)(src, stride, 1, w, up_ver, top, 1, pred_size);
    }
}

static av_always_inline pixel FUNC(pred_dc_val)(const pixel *top, const pixel *left,
    const int w, const int h)
{
    pixel dc_val;
    int sum = 0;
    unsigned int offset = (w == h) ? (w << 1) : FFMAX(w, h);
    const int shift = av_log2(offset);
    offset >>= 1;
    if (w >= h) {
        for (int i = 0; i < w; i++)
            sum += top[i];
    }
    if (w <= h) {
        for (int i = 0; i < h; i++)
            sum += left[i];
    }
    dc_val = (sum + offset) >> shift;
    return dc_val;
}

//8.4.5.2.12 Specification of INTRA_DC intra prediction mode
static av_always_inline void FUNC(pred_dc)(uint8_t *_src, const uint8_t *_top,
    const uint8_t *_left, const int w, const int h, const ptrdiff_t stride)
{
    int x, y;
    pixel *src          = (pixel *)_src;
    const pixel *top    = (const pixel *)_top;
    const pixel *left   = (const pixel *)_left;
    const pixel dc      = FUNC(pred_dc_val)(top, left, w, h);
    const pixel4 a      = PIXEL_SPLAT_X4(dc);
    for (y = 0; y < h; y++) {
        pixel *s = src;
        for (x = 0; x < w; x += 4) {
            AV_WN4P(s, a);
            s += 4;
        }
        src += stride;
    }
}

static av_always_inline void FUNC(pred_v)(uint8_t *_src, const uint8_t *_top,
    const int w, const int h, const ptrdiff_t stride)
{
    pixel *src          = (pixel *)_src;
    const pixel *top    = (const pixel *)_top;
    for (int y = 0; y < h; y++) {
        memcpy(src, top, sizeof(pixel)  * w);
        src += stride;
    }
}

static void FUNC(pred_h)(uint8_t *_src, const uint8_t *_left, const int w, const int h,
    const ptrdiff_t stride)
{
    pixel *src          = (pixel *)_src;
    const pixel *left    = (const pixel *)_left;
    for (int y = 0; y < h; y++) {
        const pixel4 a = PIXEL_SPLAT_X4(left[y]);
        for (int x = 0; x < w; x += 4) {
            AV_WN4P(&POS(x, y), a);
        }
    }
}

#define INTRA_LUMA_FILTER(p)    CLIP((p[0] * f[0] + p[1] * f[1] + p[2] * f[2] + p[3] * f[3] + 32) >> 6)
#define INTRA_CHROMA_FILTER(p)  (((32 - fact) * p[1] + fact * p[2] + 16) >> 5)

//8.4.5.2.13 Specification of INTRA_ANGULAR2..INTRA_ANGULAR66 intra prediction modes
static void FUNC(pred_angular_v)(uint8_t *_src, const uint8_t *_top, const uint8_t *_left,
    const int w, const int h, const ptrdiff_t stride, const int c_idx, const int mode,
    const int ref_idx, const int filter_flag, const int need_pdpc)
{
    pixel *src          = (pixel *)_src;
    const pixel *left   = (const pixel *)_left;
    const pixel *top    = (const pixel *)_top - (1 + ref_idx);
    const int intra_pred_angle = ff_vvc_intra_pred_angle_derive(mode);
    int pos = (1 + ref_idx) * intra_pred_angle;
    const int dp = intra_pred_angle;
    const int is_luma = !c_idx;
    int nscale, inv_angle;

    if (need_pdpc) {
        inv_angle = ff_vvc_intra_inv_angle_derive(intra_pred_angle);
        nscale = ff_vvc_nscale_derive(w, h, mode);
    }

    for (int y = 0; y < h; y++) {
        const int idx   = (pos >> 5) + ref_idx;
        const int fact = pos & 31;
        if (!fact && (!is_luma || !filter_flag)) {
            for (int x = 0; x < w; x++) {
                const pixel *p = top + x + idx + 1;
                POS(x, y) = *p;
            }
        } else {
            if (!c_idx) {
                const int8_t *f = ff_vvc_intra_luma_filter[filter_flag][fact];
                for (int x = 0; x < w; x++) {
                    const pixel *p = top + x + idx;
                    POS(x, y) = INTRA_LUMA_FILTER(p);
                }
            } else {
                for (int x = 0; x < w; x++) {
                    const pixel *p = top + x + idx;
                    POS(x, y) = INTRA_CHROMA_FILTER(p);
                }
            }
        }
        if (need_pdpc) {
            int inv_angle_sum = 256 + inv_angle;
            for (int x = 0; x < FFMIN(w, 3 << nscale); x++) {
                const pixel l   = left[y + (inv_angle_sum >> 9)];
                const pixel val = POS(x, y);
                const int wl    = 32 >> ((x << 1) >> nscale);
                const int pred  = val + (((l - val) * wl + 32) >> 6);
                POS(x, y) = CLIP(pred);
                inv_angle_sum += inv_angle;
            }
        }
        pos += dp;
    }
}

//8.4.5.2.13 Specification of INTRA_ANGULAR2..INTRA_ANGULAR66 intra prediction modes
static void FUNC(pred_angular_h)(uint8_t *_src, const uint8_t *_top, const uint8_t *_left,
    const int w, const int h, const ptrdiff_t stride, const int c_idx, const int mode,
    const int ref_idx, const int filter_flag, const int need_pdpc)
{
    pixel *src          = (pixel *)_src;
    const pixel *left   = (const pixel *)_left - (1 + ref_idx);
    const pixel *top    = (const pixel *)_top;
    const int is_luma = !c_idx;
    const int intra_pred_angle = ff_vvc_intra_pred_angle_derive(mode);
    const int dp = intra_pred_angle;
    int nscale = 0, inv_angle, inv_angle_sum;

    if (need_pdpc) {
        inv_angle = ff_vvc_intra_inv_angle_derive(intra_pred_angle);
        inv_angle_sum = 256 + inv_angle;
        nscale = ff_vvc_nscale_derive(w, h, mode);
    }

    for (int y = 0; y < h; y++) {
        int pos = (1 + ref_idx) * intra_pred_angle;
        int wt;
        if (need_pdpc)
            wt = (32 >> FFMIN(31, (y * 2) >> nscale));

        for (int x = 0; x < w; x++) {
            const int idx  = (pos >> 5) + ref_idx;
            const int fact = pos & 31;
            const pixel *p = left + y + idx;
            int pred;
            if (!fact && (!is_luma || !filter_flag)) {
                pred = p[1];
            } else {
                if (!c_idx) {
                    const int8_t *f = ff_vvc_intra_luma_filter[filter_flag][fact];
                    pred = INTRA_LUMA_FILTER(p);
                } else {
                    pred = INTRA_CHROMA_FILTER(p);
                }
            }
            if (need_pdpc) {
                if (y < (3 << nscale)) {
                    const pixel t = top[x + (inv_angle_sum >> 9)];
                    pred = CLIP(pred + (((t - pred) * wt + 32) >> 6));
                }
            }
            POS(x, y) = pred;
            pos += dp;
        }
        if (need_pdpc)
            inv_angle_sum += inv_angle;
    }
}

static void FUNC(ff_vvc_intra_dsp_init)(VVCIntraDSPContext *const intra)
{
    intra->lmcs_scale_chroma  = FUNC(lmcs_scale_chroma);
    intra->intra_cclm_pred    = FUNC(intra_cclm_pred);
    intra->intra_pred         = FUNC(intra_pred);
    intra->pred_planar        = FUNC(pred_planar);
    intra->pred_mip           = FUNC(pred_mip);
    intra->pred_dc            = FUNC(pred_dc);
    intra->pred_v             = FUNC(pred_v);
    intra->pred_h             = FUNC(pred_h);
    intra->pred_angular_v     = FUNC(pred_angular_v);
    intra->pred_angular_h     = FUNC(pred_angular_h);
}
