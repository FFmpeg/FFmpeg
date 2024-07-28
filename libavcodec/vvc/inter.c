/*
 * VVC inter prediction
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
#include "libavutil/frame.h"

#include "data.h"
#include "inter.h"
#include "mvs.h"
#include "refs.h"

// +1 is enough, + 32 for asm alignment
#define PROF_TEMP_OFFSET (MAX_PB_SIZE + 32)
static const int bcw_w_lut[] = {4, 5, 3, 10, -2};

static void subpic_get_rect(VVCRect *r, const VVCFrame *src_frame, const int subpic_idx, const int is_chroma)
{
    const VVCSPS *sps = src_frame->sps;
    const VVCPPS *pps = src_frame->pps;
    const int hs      = sps->hshift[is_chroma];
    const int vs      = sps->vshift[is_chroma];

    r->l = pps->subpic_x[subpic_idx] >> hs;
    r->t = pps->subpic_y[subpic_idx] >> vs;
    r->r = r->l + (pps->subpic_width[subpic_idx]  >> hs);
    r->b = r->t + (pps->subpic_height[subpic_idx] >> vs);
}

// clip to subblock and subpicture process in 8.5.6.3.2 Luma sample interpolation filtering process
static void clip_to_subpic(int *x_off, int *y_off, int *pic_width, int *pic_height, const VVCRect *subpic, const VVCRect *sb, const int dmvr_clip)
{
    const int l = dmvr_clip ? FFMIN(FFMAX(subpic->l, sb->l), subpic->r - 1) : subpic->l;
    const int t = dmvr_clip ? FFMIN(FFMAX(subpic->t, sb->t), subpic->b - 1) : subpic->t;
    const int r = dmvr_clip ? FFMAX(FFMIN(subpic->r, sb->r), subpic->l + 1) : subpic->r;
    const int b = dmvr_clip ? FFMAX(FFMIN(subpic->b, sb->b), subpic->t + 1) : subpic->b;

    *x_off -= l;
    *y_off -= t;
    *pic_width  = r - l;
    *pic_height = b - t;
}

static void emulated_edge_no_wrap(const VVCLocalContext *lc, uint8_t *dst,
    const uint8_t **src, ptrdiff_t *src_stride,
    int x_off, int y_off, const int block_w, const int block_h,
    const int extra_before, const int extra_after,
    const VVCRect *subpic, const VVCRect *sb, const int dmvr_clip)
{
    const VVCFrameContext *fc = lc->fc;
    const int extra           = extra_before + extra_after;
    int pic_width, pic_height;

    *src  += y_off * *src_stride + (x_off * (1 << fc->ps.sps->pixel_shift));

    clip_to_subpic(&x_off, &y_off, &pic_width, &pic_height, subpic, sb, dmvr_clip);

    if (dmvr_clip || x_off < extra_before || y_off < extra_before ||
        x_off >= pic_width - block_w - extra_after ||
        y_off >= pic_height - block_h - extra_after) {
        const int ps                    = fc->ps.sps->pixel_shift;
        const ptrdiff_t edge_emu_stride = EDGE_EMU_BUFFER_STRIDE << ps;
        const int offset                = extra_before * *src_stride     + (extra_before << ps);
        const int buf_offset            = extra_before * edge_emu_stride + (extra_before << ps);

        fc->vdsp.emulated_edge_mc(dst, *src - offset, edge_emu_stride, *src_stride,
            block_w + extra, block_h + extra, x_off - extra_before, y_off - extra_before,
            pic_width, pic_height);

        *src = dst + buf_offset;
        *src_stride = edge_emu_stride;
    }
}

static void emulated_half(const VVCLocalContext *lc, uint8_t *dst, const ptrdiff_t dst_stride,
    const uint8_t *src, const ptrdiff_t src_stride, const int ps,
    int x_off, int y_off, const int block_w, const int block_h,
    const VVCRect *subpic,const VVCRect *half_sb, const int dmvr_clip)
{
    const VVCFrameContext *fc = lc->fc;
    int pic_width, pic_height;

    src += y_off * src_stride + x_off * (1 << ps);

    clip_to_subpic(&x_off, &y_off, &pic_width, &pic_height, subpic, half_sb, dmvr_clip);

    fc->vdsp.emulated_edge_mc(dst, src, dst_stride, src_stride,
        block_w, block_h, x_off, y_off, pic_width, pic_height);
}

static void sb_set_lr(VVCRect *sb, const int l, const int r)
{
    sb->l = l;
    sb->r = r;
}

static void sb_wrap(VVCRect *sb, const int wrap)
{
    sb_set_lr(sb, sb->l + wrap, sb->r + wrap);
}

static void emulated_edge(const VVCLocalContext *lc, uint8_t *dst,
    const uint8_t **src, ptrdiff_t *src_stride, const VVCFrame *src_frame,
    int x_sb, int y_sb, int x_off, int y_off, int block_w, int block_h, const int wrap_enabled,
    const int is_chroma, const int extra_before, const int extra_after)
{
    const VVCSPS *sps          = src_frame->sps;
    const VVCPPS *pps          = src_frame->pps;
    const int ps               = sps->pixel_shift;
    const int subpic_idx       = lc->sc->sh.r->curr_subpic_idx;
    const int extra            = extra_before + extra_after;
    const int dmvr_clip        = x_sb != x_off || y_sb != y_off;
    const int dmvr_left        = FFMAX(x_off, x_sb) - extra_before;
    const int dmvr_right       = FFMIN(x_off, x_sb) + block_w + extra_after;
    const int left             = x_off - extra_before;
    const int top              = y_off - extra_before;
    const int pic_width        = pps->width >> sps->hshift[is_chroma];
    const int wrap             = pps->ref_wraparound_offset << (sps->min_cb_log2_size_y - sps->hshift[is_chroma]);
    const ptrdiff_t dst_stride = EDGE_EMU_BUFFER_STRIDE << ps;
    VVCRect sb                 = { x_sb - extra_before, y_sb - extra_before, x_sb + block_w + extra_after, y_sb + block_h + extra_after };
    VVCRect subpic;

    subpic_get_rect(&subpic, src_frame, subpic_idx, is_chroma);

    if (!wrap_enabled || (dmvr_left >= 0 && dmvr_right <= pic_width)) {
        emulated_edge_no_wrap(lc, dst, src, src_stride,
            x_off, y_off, block_w, block_h, extra_before, extra_after, &subpic, &sb, dmvr_clip);
        return;
    }
    if (dmvr_right <= 0) {
        sb_wrap(&sb, wrap);
        emulated_edge_no_wrap(lc, dst, src, src_stride,
            x_off + wrap, y_off, block_w, block_h, extra_before, extra_after, &subpic, &sb, dmvr_clip);
        return;
    }
    if (dmvr_left >= pic_width) {
        sb_wrap(&sb, -wrap);
        emulated_edge_no_wrap(lc, dst, src, src_stride,
            x_off - wrap, y_off, block_w, block_h, extra_before, extra_after, &subpic, &sb, dmvr_clip);
        return;
    }

    block_w += extra;
    block_h += extra;

    // half block are wrapped
    if (dmvr_left < 0 ) {
        const int w     = -left;
        VVCRect half_sb = { sb.l + wrap, sb.t, 0 + wrap, sb.b };
        emulated_half(lc, dst, dst_stride, *src, *src_stride, ps,
            left + wrap, top, w, block_h, &subpic, &half_sb, dmvr_clip);

        sb_set_lr(&half_sb, 0, sb.r);
        emulated_half(lc, dst +  (w << ps), dst_stride, *src, *src_stride, ps,
            0, top, block_w - w, block_h, &subpic, &half_sb, dmvr_clip);
    } else {
        const int w     = pic_width - left;
        VVCRect half_sb = { sb.l, sb.t, pic_width, sb.b };
        emulated_half(lc, dst, dst_stride, *src, *src_stride, ps,
            left, top, w, block_h, &subpic, &half_sb, dmvr_clip);

        sb_set_lr(&half_sb, pic_width - wrap, sb.r - wrap);
        emulated_half(lc, dst +  (w << ps), dst_stride, *src, *src_stride, ps,
            pic_width - wrap , top, block_w - w, block_h, &subpic, &half_sb, dmvr_clip);
    }

    *src = dst + extra_before * dst_stride + (extra_before << ps);
    *src_stride = dst_stride;
}

#define MC_EMULATED_EDGE(dst, src, src_stride, x_off, y_off)                                                                    \
    emulated_edge(lc, dst, src, src_stride, ref, x_off, y_off, x_off, y_off, block_w, block_h, wrap_enabled, is_chroma,         \
        is_chroma ? CHROMA_EXTRA_BEFORE : LUMA_EXTRA_BEFORE, is_chroma ? CHROMA_EXTRA_AFTER : LUMA_EXTRA_AFTER)

#define MC_EMULATED_EDGE_DMVR(dst, src, src_stride, x_sb, y_sb, x_off, y_off)                                                   \
    emulated_edge(lc, dst, src, src_stride, ref, x_sb, y_sb, x_off, y_off, block_w, block_h, wrap_enabled, is_chroma,           \
        is_chroma ? CHROMA_EXTRA_BEFORE : LUMA_EXTRA_BEFORE, is_chroma ? CHROMA_EXTRA_AFTER : LUMA_EXTRA_AFTER)

#define MC_EMULATED_EDGE_BILINEAR(dst, src, src_stride, x_off, y_off)                                                           \
    emulated_edge(lc, dst, src, src_stride, ref, x_off, y_off, x_off, y_off, pred_w, pred_h, wrap_enabled, 0,                   \
        BILINEAR_EXTRA_BEFORE, BILINEAR_EXTRA_AFTER)

// part of 8.5.6.6 Weighted sample prediction process
static int derive_weight_uni(int *denom, int *wx, int *ox,
    const VVCLocalContext *lc, const MvField *mvf, const int c_idx)
{
    const VVCFrameContext *fc = lc->fc;
    const VVCPPS *pps         = fc->ps.pps;
    const VVCSH *sh           = &lc->sc->sh;
    const int weight_flag     = (IS_P(sh->r) && pps->r->pps_weighted_pred_flag) ||
                                  (IS_B(sh->r) && pps->r->pps_weighted_bipred_flag);
    if (weight_flag) {
        const int lx             = mvf->pred_flag - PF_L0;
        const PredWeightTable *w = pps->r->pps_wp_info_in_ph_flag ? &fc->ps.ph.pwt : &sh->pwt;

        *denom = w->log2_denom[c_idx > 0];
        *wx = w->weight[lx][c_idx][mvf->ref_idx[lx]];
        *ox = w->offset[lx][c_idx][mvf->ref_idx[lx]];
    }
    return weight_flag;
}

// part of 8.5.6.6 Weighted sample prediction process
static int derive_weight(int *denom, int *w0, int *w1, int *o0, int *o1,
    const VVCLocalContext *lc, const MvField *mvf, const int c_idx, const int dmvr_flag)
{
    const VVCFrameContext *fc = lc->fc;
    const VVCPPS *pps         = fc->ps.pps;
    const VVCSH *sh           = &lc->sc->sh;
    const int bcw_idx         = mvf->bcw_idx;
    const int weight_flag     = (IS_P(sh->r) && pps->r->pps_weighted_pred_flag) ||
                                  (IS_B(sh->r) && pps->r->pps_weighted_bipred_flag && !dmvr_flag);
    if ((!weight_flag && !bcw_idx) || (bcw_idx && lc->cu->ciip_flag))
        return 0;

    if (bcw_idx) {
        *denom = 2;
        *w1 = bcw_w_lut[bcw_idx];
        *w0 = 8 - *w1;
        *o0 = *o1 = 0;
    } else {
        const VVCPPS *pps        = fc->ps.pps;
        const PredWeightTable *w = pps->r->pps_wp_info_in_ph_flag ? &fc->ps.ph.pwt : &sh->pwt;

        *denom = w->log2_denom[c_idx > 0];
        *w0 = w->weight[L0][c_idx][mvf->ref_idx[L0]];
        *w1 = w->weight[L1][c_idx][mvf->ref_idx[L1]];
        *o0 = w->offset[L0][c_idx][mvf->ref_idx[L0]];
        *o1 = w->offset[L1][c_idx][mvf->ref_idx[L1]];
    }
    return 1;
}

#define INTER_FILTER(t, frac)  (is_chroma ? ff_vvc_inter_chroma_filters[t][frac] : ff_vvc_inter_luma_filters[t][frac])

static void mc(VVCLocalContext *lc, int16_t *dst, const VVCFrame *ref, const Mv *mv,
    int x_off, int y_off, const int block_w, const int block_h, const int c_idx)
{
    const VVCFrameContext *fc = lc->fc;
    const PredictionUnit *pu  = &lc->cu->pu;
    const uint8_t *src        = ref->frame->data[c_idx];
    ptrdiff_t src_stride      = ref->frame->linesize[c_idx];
    const int is_chroma       = !!c_idx;
    const int hs              = fc->ps.sps->hshift[c_idx];
    const int vs              = fc->ps.sps->vshift[c_idx];
    const int idx             = av_log2(block_w) - 1;
    const intptr_t mx         = av_zero_extend(mv->x, 4 + hs) << (is_chroma - hs);
    const intptr_t my         = av_zero_extend(mv->y, 4 + vs) << (is_chroma - vs);
    const int hpel_if_idx     = (is_chroma || pu->merge_gpm_flag) ? 0 : pu->mi.hpel_if_idx;
    const int8_t *hf          = INTER_FILTER(hpel_if_idx, mx);
    const int8_t *vf          = INTER_FILTER(hpel_if_idx, my);
    const int wrap_enabled    = fc->ps.pps->r->pps_ref_wraparound_enabled_flag;

    x_off += mv->x >> (4 + hs);
    y_off += mv->y >> (4 + vs);

    MC_EMULATED_EDGE(lc->edge_emu_buffer, &src, &src_stride, x_off, y_off);
    fc->vvcdsp.inter.put[is_chroma][idx][!!my][!!mx](dst, src, src_stride, block_h, hf, vf, block_w);
}

static void mc_uni(VVCLocalContext *lc, uint8_t *dst, const ptrdiff_t dst_stride,
    const VVCFrame *ref, const MvField *mvf, int x_off, int y_off, const int block_w, const int block_h,
    const int c_idx)
{
    const VVCFrameContext *fc = lc->fc;
    const PredictionUnit *pu  = &lc->cu->pu;
    const uint8_t *src        = ref->frame->data[c_idx];
    ptrdiff_t src_stride      = ref->frame->linesize[c_idx];
    const int lx              = mvf->pred_flag - PF_L0;
    const int hs              = fc->ps.sps->hshift[c_idx];
    const int vs              = fc->ps.sps->vshift[c_idx];
    const int idx             = av_log2(block_w) - 1;
    const Mv *mv              = &mvf->mv[lx];
    const int is_chroma       = !!c_idx;
    const intptr_t mx         = av_zero_extend(mv->x, 4 + hs) << (is_chroma - hs);
    const intptr_t my         = av_zero_extend(mv->y, 4 + vs) << (is_chroma - vs);
    const int hpel_if_idx     = is_chroma ? 0 : pu->mi.hpel_if_idx;
    const int8_t *hf          = INTER_FILTER(hpel_if_idx, mx);
    const int8_t *vf          = INTER_FILTER(hpel_if_idx, my);
    const int wrap_enabled    = fc->ps.pps->r->pps_ref_wraparound_enabled_flag;
    int denom, wx, ox;

    x_off += mv->x >> (4 + hs);
    y_off += mv->y >> (4 + vs);

    MC_EMULATED_EDGE(lc->edge_emu_buffer, &src, &src_stride, x_off, y_off);
    if (derive_weight_uni(&denom, &wx, &ox, lc, mvf, c_idx)) {
        fc->vvcdsp.inter.put_uni_w[is_chroma][idx][!!my][!!mx](dst, dst_stride, src, src_stride,
            block_h, denom, wx, ox, hf, vf, block_w);
    } else {
        fc->vvcdsp.inter.put_uni[is_chroma][idx][!!my][!!mx](dst, dst_stride, src, src_stride,
            block_h, hf, vf, block_w);
    }
}

static void mc_bi(VVCLocalContext *lc, uint8_t *dst, const ptrdiff_t dst_stride,
    const VVCFrame *ref0, const VVCFrame *ref1, const MvField *mvf, const MvField *orig_mv,
    const int x_off, const int y_off, const int block_w, const int block_h, const int c_idx,
    const int sb_bdof_flag)
{
    const VVCFrameContext *fc = lc->fc;
    const PredictionUnit *pu  = &lc->cu->pu;
    const int hs              = fc->ps.sps->hshift[c_idx];
    const int vs              = fc->ps.sps->vshift[c_idx];
    const int idx             = av_log2(block_w) - 1;
    const VVCFrame *refs[]    = { ref0, ref1 };
    int16_t *tmp[]            = { lc->tmp + sb_bdof_flag * PROF_TEMP_OFFSET, lc->tmp1 + sb_bdof_flag * PROF_TEMP_OFFSET };
    int denom, w0, w1, o0, o1;
    const int weight_flag     = derive_weight(&denom, &w0, &w1, &o0, &o1, lc, mvf, c_idx, pu->dmvr_flag);
    const int is_chroma       = !!c_idx;
    const int hpel_if_idx     = is_chroma ? 0 : pu->mi.hpel_if_idx;

    for (int i = L0; i <= L1; i++) {
        const Mv *mv           = mvf->mv + i;
        const int mx           = av_zero_extend(mv->x, 4 + hs) << (is_chroma - hs);
        const int my           = av_zero_extend(mv->y, 4 + vs) << (is_chroma - vs);
        const int ox           = x_off + (mv->x >> (4 + hs));
        const int oy           = y_off + (mv->y >> (4 + vs));
        const VVCFrame *ref    = refs[i];
        ptrdiff_t src_stride   = ref->frame->linesize[c_idx];
        const uint8_t *src     = ref->frame->data[c_idx];
        const int8_t *hf       = INTER_FILTER(hpel_if_idx, mx);
        const int8_t *vf       = INTER_FILTER(hpel_if_idx, my);
        const int wrap_enabled = fc->ps.pps->r->pps_ref_wraparound_enabled_flag;

        if (pu->dmvr_flag) {
            const int x_sb = x_off + (orig_mv->mv[i].x >> (4 + hs));
            const int y_sb = y_off + (orig_mv->mv[i].y >> (4 + vs));

            MC_EMULATED_EDGE_DMVR(lc->edge_emu_buffer,  &src, &src_stride, x_sb, y_sb, ox, oy);
        } else {
            MC_EMULATED_EDGE(lc->edge_emu_buffer, &src, &src_stride, ox, oy);
        }
        fc->vvcdsp.inter.put[is_chroma][idx][!!my][!!mx](tmp[i],  src, src_stride, block_h, hf, vf, block_w);
        if (sb_bdof_flag)
            fc->vvcdsp.inter.bdof_fetch_samples(tmp[i], src, src_stride, mx, my, block_w, block_h);
    }
    if (sb_bdof_flag)
        fc->vvcdsp.inter.apply_bdof(dst, dst_stride, tmp[L0], tmp[L1], block_w, block_h);
    else if (weight_flag)
        fc->vvcdsp.inter.w_avg(dst, dst_stride, tmp[L0], tmp[L1], block_w, block_h, denom, w0, w1, o0, o1);
    else
        fc->vvcdsp.inter.avg(dst, dst_stride, tmp[L0], tmp[L1], block_w, block_h);
}

static const int8_t* inter_filter_scaled(const int scale, const int is_chroma, const int is_affine)
{
#define SCALE_THRESHOLD_1 20480
#define SCALE_THRESHOLD_2 28672

    const int i = (scale > SCALE_THRESHOLD_2) + (scale > SCALE_THRESHOLD_1);

    if (!is_chroma) {
        if (!is_affine)
            return &ff_vvc_inter_luma_filters[i + !!i][0][0];   //hpel 1 is not needed for scaled
        return &ff_vvc_inter_luma_filters[VVC_INTER_LUMA_FILTER_TYPE_AFFINE + i][0][0];
    }

    return &ff_vvc_inter_chroma_filters[i][0][0];
}
#define INTER_FILTER_SCALED(scale) inter_filter_scaled(scale, is_chroma, is_affine)

#define SCALED_CHROMA_ADDIN(scale, collocated_flag) (is_chroma ? (collocated_flag ? 0 : 8 * (scale - (1 << 14))) : 0)
#define SCALED_REF_SB(off, scaling_off, ref_mv, scale, add, shift) ((((off - (scaling_off << shift)) << (4 + shift)) + ref_mv) * scale + add)
#define SCALED_REF(ref_sb, offset, shift) (FFSIGN(ref_sb) * ((FFABS(ref_sb) + (128 << is_chroma)) >> (8 + is_chroma)) + (offset << (10 - shift)) + (32 >> is_chroma))
#define SCALED_STEP(scale) ((scale + 8) >> 4)

static void scaled_ref_pos_and_step(const VVCLocalContext *lc, const VVCRefPic *refp, const Mv *mv, const int x_off, const int y_off, const int c_idx,
    int *x, int *y, int *dx, int *dy)
{
    const VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps         = fc->ps.sps;
    const int is_chroma       = !!c_idx;
    const int hs              = sps->hshift[c_idx];
    const int vs              = sps->vshift[c_idx];
    const int left_offset     = fc->ref->scaling_win.left_offset;
    const int top_offset      = fc->ref->scaling_win.top_offset;
    const int addx            = SCALED_CHROMA_ADDIN(refp->scale[0], sps->r->sps_chroma_horizontal_collocated_flag);
    const int addy            = SCALED_CHROMA_ADDIN(refp->scale[1], sps->r->sps_chroma_vertical_collocated_flag);
    const int refx_sb         = SCALED_REF_SB(x_off, left_offset, mv->x, refp->scale[0], addx, hs);
    const int refy_sb         = SCALED_REF_SB(y_off, top_offset,  mv->y, refp->scale[1], addy, vs);

    *x  = SCALED_REF(refx_sb, left_offset, hs);
    *y  = SCALED_REF(refy_sb, top_offset, vs);
    *dx = SCALED_STEP(refp->scale[0]);
    *dy = SCALED_STEP(refp->scale[1]);
}

static void emulated_edge_scaled(VVCLocalContext *lc, const uint8_t **src, ptrdiff_t *src_stride, int *src_height,
    const VVCFrame *ref, const int x, const int y, const int dx, const int dy, const int w, const int h, const int is_chroma)
{
    const int x_off         = SCALED_INT(x);
    const int y_off         = SCALED_INT(y);
    const int x_end         = SCALED_INT(x + w * dx);
    const int y_end         = SCALED_INT(y + h * dy);
    const int x_last        = SCALED_INT(x + (w - 1) * dx);
    const int y_last        = SCALED_INT(y + (h - 1) * dy);
    const int block_w       = x_end - x_off + (x_end == x_last);
    const int block_h       = *src_height = y_end - y_off + (y_end == y_last);
    const int wrap_enabled  = 0;

    MC_EMULATED_EDGE(lc->edge_emu_buffer, src, src_stride, x_off, y_off);
}

static void mc_scaled(VVCLocalContext *lc, int16_t *dst, const VVCRefPic *refp, const Mv *mv,
    int x_off, int y_off, const int block_w, const int block_h, const int c_idx)
{
    const VVCFrameContext *fc = lc->fc;
    const PredictionUnit *pu  = &lc->cu->pu;
    const uint8_t *src        = refp->ref->frame->data[c_idx];
    ptrdiff_t src_stride      = refp->ref->frame->linesize[c_idx];
    const int is_affine       = pu->inter_affine_flag;
    const int is_chroma       = !!c_idx;
    const int idx             = av_log2(block_w) - 1;
    const int8_t *hf          = INTER_FILTER_SCALED(refp->scale[0]);
    const int8_t *vf          = INTER_FILTER_SCALED(refp->scale[1]);
    int x, y, dx, dy, src_height;

    scaled_ref_pos_and_step(lc, refp, mv, x_off, y_off, c_idx, &x, &y, &dx, &dy);
    emulated_edge_scaled(lc, &src, &src_stride, &src_height, refp->ref, x, y, dx, dy, block_w, block_h, is_chroma);
    fc->vvcdsp.inter.put_scaled[is_chroma][idx](dst, src, src_stride, src_height, x, y, dx, dy, block_h, hf, vf, block_w);
}

static void mc_uni_scaled(VVCLocalContext *lc, uint8_t *dst, const ptrdiff_t dst_stride, const VVCRefPic *refp,
    const MvField *mvf, const int x_off, const int y_off, const int block_w, const int block_h, const int c_idx)
{
    const VVCFrameContext *fc = lc->fc;
    const PredictionUnit *pu  = &lc->cu->pu;
    const uint8_t *src        = refp->ref->frame->data[c_idx];
    ptrdiff_t src_stride      = refp->ref->frame->linesize[c_idx];
    const int lx              = mvf->pred_flag - PF_L0;
    const Mv *mv              = &mvf->mv[lx];
    const int is_affine       = pu->inter_affine_flag;
    const int is_chroma       = !!c_idx;
    const int idx             = av_log2(block_w) - 1;
    const int8_t *hf          = INTER_FILTER_SCALED(refp->scale[0]);
    const int8_t *vf          = INTER_FILTER_SCALED(refp->scale[1]);
    int denom, wx, ox, x, y, dx, dy, src_height;

    scaled_ref_pos_and_step(lc, refp, mv, x_off, y_off, c_idx, &x, &y, &dx, &dy);
    emulated_edge_scaled(lc, &src, &src_stride, &src_height, refp->ref, x, y, dx, dy, block_w, block_h, is_chroma);

    if (derive_weight_uni(&denom, &wx, &ox, lc, mvf, c_idx)) {
        fc->vvcdsp.inter.put_uni_w_scaled[is_chroma][idx](dst, dst_stride, src, src_stride, src_height,
            x, y, dx, dy, block_h, denom, wx, ox, hf, vf, block_w);
    } else {
        fc->vvcdsp.inter.put_uni_scaled[is_chroma][idx](dst, dst_stride, src, src_stride, src_height,
            x, y, dx, dy, block_h, hf, vf, block_w);
    }
}

static void mc_bi_scaled(VVCLocalContext *lc, uint8_t *dst, const ptrdiff_t dst_stride,
   const VVCRefPic *refp0, const VVCRefPic *refp1, const MvField *mvf,
   const int x_off, const int y_off, const int block_w, const int block_h, const int c_idx)
{
    int denom, w0, w1, o0, o1;
    const VVCFrameContext *fc = lc->fc;
    const int weight_flag     = derive_weight(&denom, &w0, &w1, &o0, &o1, lc, mvf, c_idx, lc->cu->pu.dmvr_flag);
    const VVCRefPic *refps[]  = { refp0, refp1 };
    int16_t *tmp[]            = { lc->tmp, lc->tmp1 };

    for (int i = L0; i <= L1; i++) {
        const Mv *mv          = mvf->mv + i;
        const VVCRefPic *refp = refps[i];

        if (refp->is_scaled)
            mc_scaled(lc, tmp[i], refp, mv, x_off, y_off, block_w, block_h, c_idx);
        else
            mc(lc, tmp[i], refp->ref, mv, x_off, y_off, block_w, block_h, c_idx);
    }
    if (weight_flag)
        fc->vvcdsp.inter.w_avg(dst, dst_stride, tmp[L0], tmp[L1], block_w, block_h, denom, w0, w1, o0, o1);
    else
        fc->vvcdsp.inter.avg(dst, dst_stride, tmp[L0], tmp[L1], block_w, block_h);
}

static void luma_prof_uni(VVCLocalContext *lc, uint8_t *dst, const ptrdiff_t dst_stride,
    const VVCFrame *ref, const MvField *mvf, int x_off, int y_off, const int block_w, const int block_h,
    const int cb_prof_flag, const int16_t *diff_mv_x, const int16_t *diff_mv_y)
{
    const VVCFrameContext *fc = lc->fc;
    const uint8_t *src        = ref->frame->data[LUMA];
    ptrdiff_t src_stride      = ref->frame->linesize[LUMA];
    uint16_t *prof_tmp        = lc->tmp + PROF_TEMP_OFFSET;
    const int idx             = av_log2(block_w) - 1;
    const int lx              = mvf->pred_flag - PF_L0;
    const Mv *mv              = mvf->mv + lx;
    const int mx              = mv->x & 0xf;
    const int my              = mv->y & 0xf;
    const int8_t *hf          = ff_vvc_inter_luma_filters[VVC_INTER_LUMA_FILTER_TYPE_AFFINE][mx];
    const int8_t *vf          = ff_vvc_inter_luma_filters[VVC_INTER_LUMA_FILTER_TYPE_AFFINE][my];
    int denom, wx, ox;
    const int weight_flag     = derive_weight_uni(&denom, &wx, &ox, lc, mvf, LUMA);
    const int wrap_enabled    = fc->ps.pps->r->pps_ref_wraparound_enabled_flag;
    const int is_chroma       = 0;

    x_off += mv->x >> 4;
    y_off += mv->y >> 4;

    MC_EMULATED_EDGE(lc->edge_emu_buffer, &src, &src_stride, x_off, y_off);
    if (cb_prof_flag) {
        fc->vvcdsp.inter.put[LUMA][idx][!!my][!!mx](prof_tmp, src, src_stride, AFFINE_MIN_BLOCK_SIZE, hf, vf, AFFINE_MIN_BLOCK_SIZE);
        fc->vvcdsp.inter.fetch_samples(prof_tmp, src, src_stride, mx, my);
        if (!weight_flag)
            fc->vvcdsp.inter.apply_prof_uni(dst, dst_stride, prof_tmp, diff_mv_x, diff_mv_y);
        else
            fc->vvcdsp.inter.apply_prof_uni_w(dst, dst_stride, prof_tmp, diff_mv_x, diff_mv_y, denom, wx, ox);
    } else {
        if (!weight_flag)
            fc->vvcdsp.inter.put_uni[LUMA][idx][!!my][!!mx](dst, dst_stride, src, src_stride, block_h, hf, vf, block_w);
        else
            fc->vvcdsp.inter.put_uni_w[LUMA][idx][!!my][!!mx](dst, dst_stride, src, src_stride, block_h, denom, wx, ox, hf, vf, block_w);
    }
}

static void luma_prof(VVCLocalContext *lc, int16_t *dst, const VVCFrame *ref,
    const Mv *mv , const int x_off, const int y_off, const int block_w, const int block_h, const int lx)
{
    const VVCFrameContext *fc = lc->fc;
    const PredictionUnit *pu  = &lc->cu->pu;
    const int mx              = mv->x & 0xf;
    const int my              = mv->y & 0xf;
    const int ox              = x_off + (mv->x >> 4);
    const int oy              = y_off + (mv->y >> 4);
    const int idx             = av_log2(block_w) - 1;
    const int is_chroma       = 0;
    uint16_t *prof_tmp        = lc->tmp2 + PROF_TEMP_OFFSET;
    ptrdiff_t src_stride      = ref->frame->linesize[LUMA];
    const uint8_t *src        = ref->frame->data[LUMA];
    const int8_t *hf          = ff_vvc_inter_luma_filters[VVC_INTER_LUMA_FILTER_TYPE_AFFINE][mx];
    const int8_t *vf          = ff_vvc_inter_luma_filters[VVC_INTER_LUMA_FILTER_TYPE_AFFINE][my];
    const int wrap_enabled    = fc->ps.pps->r->pps_ref_wraparound_enabled_flag;

    MC_EMULATED_EDGE(lc->edge_emu_buffer, &src, &src_stride, ox, oy);
    if (!pu->cb_prof_flag[lx]) {
        fc->vvcdsp.inter.put[LUMA][idx][!!my][!!mx](dst, src, src_stride, block_h, hf, vf, block_w);
    } else {
        fc->vvcdsp.inter.put[LUMA][idx][!!my][!!mx](prof_tmp, src, src_stride, AFFINE_MIN_BLOCK_SIZE, hf, vf, AFFINE_MIN_BLOCK_SIZE);
        fc->vvcdsp.inter.fetch_samples(prof_tmp, src, src_stride, mx, my);
        fc->vvcdsp.inter.apply_prof(dst, prof_tmp, pu->diff_mv_x[lx], pu->diff_mv_y[lx]);
    }
}

static void luma_prof_bi(VVCLocalContext *lc, uint8_t *dst, const ptrdiff_t dst_stride,
    const VVCRefPic *ref0, const VVCRefPic *ref1, const MvField *mvf, const int x_off, const int y_off,
    const int block_w, const int block_h)
{
    const VVCFrameContext *fc = lc->fc;
    const VVCRefPic *refps[]  = { ref0, ref1 };
    int16_t *tmp[]            = { lc->tmp, lc->tmp1 };
    int denom, w0, w1, o0, o1;
    const int weight_flag     = derive_weight(&denom, &w0, &w1, &o0, &o1, lc, mvf, LUMA, 0);

    for (int i = L0; i <= L1; i++) {
        const VVCRefPic *refp = refps[i];
        const Mv *mv          = mvf->mv + i;

        if (refp->is_scaled)
            mc_scaled(lc, tmp[i], refp, mv, x_off, y_off, block_w, block_h, LUMA);
        else
            luma_prof(lc, tmp[i], refp->ref, mv, x_off, y_off, block_w, block_h, i);
    }

    if (weight_flag)
        fc->vvcdsp.inter.w_avg(dst, dst_stride, tmp[L0], tmp[L1], block_w, block_h,  denom, w0, w1, o0, o1);
    else
        fc->vvcdsp.inter.avg(dst, dst_stride, tmp[L0], tmp[L1], block_w, block_h);
}

static int pred_get_refs(const VVCLocalContext *lc, VVCRefPic *refp[2], const MvField *mv)
{
    RefPicList *rpl = lc->sc->rpl;

    for (int mask = PF_L0; mask <= PF_L1; mask++) {
        if (mv->pred_flag & mask) {
            const int lx = mask - PF_L0;
            refp[lx] = rpl[lx].refs + mv->ref_idx[lx];
            if (!refp[lx]->ref)
                return AVERROR_INVALIDDATA;
        }
    }
    return 0;
}

#define POS(c_idx, x, y)                                                                            \
        &fc->frame->data[c_idx][((y) >> fc->ps.sps->vshift[c_idx]) * fc->frame->linesize[c_idx] +   \
            (((x) >> fc->ps.sps->hshift[c_idx]) << fc->ps.sps->pixel_shift)]

static void pred_gpm_blk(VVCLocalContext *lc)
{
    const VVCFrameContext *fc = lc->fc;
    const CodingUnit *cu      = lc->cu;
    const PredictionUnit *pu  = &cu->pu;

    const uint8_t angle_idx   = ff_vvc_gpm_angle_idx[pu->gpm_partition_idx];
    const uint8_t weights_idx = ff_vvc_gpm_angle_to_weights_idx[angle_idx];
    const int w               = av_log2(cu->cb_width) - 3;
    const int h               = av_log2(cu->cb_height) - 3;
    const uint8_t off_x       = ff_vvc_gpm_weights_offset_x[pu->gpm_partition_idx][h][w];
    const uint8_t off_y       = ff_vvc_gpm_weights_offset_y[pu->gpm_partition_idx][h][w];
    const uint8_t mirror_type = ff_vvc_gpm_angle_to_mirror[angle_idx];
    const uint8_t *weights;

    const int c_end = fc->ps.sps->r->sps_chroma_format_idc ? 3 : 1;

    int16_t *tmp[2] = {lc->tmp, lc->tmp1};

    for (int c_idx = 0; c_idx < c_end; c_idx++) {
        const int hs         = fc->ps.sps->hshift[c_idx];
        const int vs         = fc->ps.sps->vshift[c_idx];
        const int x          = lc->cu->x0  >> hs;
        const int y          = lc->cu->y0  >> vs;
        const int width      = cu->cb_width >> hs;
        const int height     = cu->cb_height >> vs;
        uint8_t *dst         = POS(c_idx, lc->cu->x0, lc->cu->y0);
        ptrdiff_t dst_stride = fc->frame->linesize[c_idx];

        int step_x = 1 << hs;
        int step_y = VVC_GPM_WEIGHT_SIZE << vs;
        if (!mirror_type) {
            weights = &ff_vvc_gpm_weights[weights_idx][off_y * VVC_GPM_WEIGHT_SIZE + off_x];
        } else if (mirror_type == 1) {
            step_x = -step_x;
            weights = &ff_vvc_gpm_weights[weights_idx][off_y * VVC_GPM_WEIGHT_SIZE + VVC_GPM_WEIGHT_SIZE - 1- off_x];
        } else {
            step_y = -step_y;
            weights = &ff_vvc_gpm_weights[weights_idx][(VVC_GPM_WEIGHT_SIZE - 1 - off_y) * VVC_GPM_WEIGHT_SIZE + off_x];
        }

        for (int i = 0; i < 2; i++) {
            const MvField *mv = pu->gpm_mv + i;
            const int lx      = mv->pred_flag - PF_L0;
            VVCRefPic *refp   = lc->sc->rpl[lx].refs + mv->ref_idx[lx];

            if (!refp->ref)
                return;
            if (refp->is_scaled)
                mc_scaled(lc, tmp[i], refp, mv->mv + lx, x, y, width, height, c_idx);
            else
                mc(lc, tmp[i], refp->ref, mv->mv + lx, x, y, width, height, c_idx);
        }
        fc->vvcdsp.inter.put_gpm(dst, dst_stride, width, height, tmp[0], tmp[1], weights, step_x, step_y);
    }
    return;
}

static int ciip_derive_intra_weight(const VVCLocalContext *lc, const int x0, const int y0,
    const int width, const int height)
{
    const VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps         = fc->ps.sps;
    const int x0b             = av_zero_extend(x0, sps->ctb_log2_size_y);
    const int y0b             = av_zero_extend(y0, sps->ctb_log2_size_y);
    const int available_l     = lc->ctb_left_flag || x0b;
    const int available_u     = lc->ctb_up_flag || y0b;
    const int min_pu_width    = fc->ps.pps->min_pu_width;
    int w                     = 1;

    if (available_u &&fc->tab.mvf[((y0 - 1) >> MIN_PU_LOG2) * min_pu_width + ((x0 - 1 + width)>> MIN_PU_LOG2)].pred_flag == PF_INTRA)
        w++;

    if (available_l && fc->tab.mvf[((y0 - 1 + height)>> MIN_PU_LOG2) * min_pu_width + ((x0 - 1) >> MIN_PU_LOG2)].pred_flag == PF_INTRA)
        w++;

    return w;
}

static void pred_regular(VVCLocalContext *lc, const MvField *mvf, const MvField *orig_mvf,
    const int x0, const int y0, const int sbw, const int sbh, const int sb_bdof_flag, const int c_start)
{
    const VVCFrameContext *fc = lc->fc;
    const int c_end           = fc->ps.sps->r->sps_chroma_format_idc ? CR : LUMA;
    VVCRefPic *refp[2];

    if (pred_get_refs(lc, refp, mvf) < 0)
        return;

    for (int c_idx = c_start; c_idx <= c_end; c_idx++) {
        uint8_t *dst                 = POS(c_idx, x0, y0);
        const ptrdiff_t dst_stride   = fc->frame->linesize[c_idx];
        const int hs                 = fc->ps.sps->hshift[c_idx];
        const int vs                 = fc->ps.sps->vshift[c_idx];
        const int x                  = x0 >> hs;
        const int y                  = y0 >> vs;
        const int w                  = sbw >> hs;
        const int h                  = sbh >> vs;
        const int is_luma            = !c_idx;
        const int do_ciip            = lc->cu->ciip_flag && (is_luma || (w > 2));
        uint8_t *inter               = do_ciip ? (uint8_t *)lc->ciip_tmp : dst;
        const ptrdiff_t inter_stride = do_ciip ? (MAX_PB_SIZE * sizeof(uint16_t)) : dst_stride;
        const int do_bdof            = is_luma && sb_bdof_flag;

        if (mvf->pred_flag != PF_BI) {
            const int lx = mvf->pred_flag - PF_L0;

            if (refp[lx]->is_scaled) {
                mc_uni_scaled(lc, inter, inter_stride, refp[lx], mvf,
                    x, y, w, h, c_idx);
            } else {
                mc_uni(lc, inter, inter_stride, refp[lx]->ref, mvf,
                    x, y, w, h, c_idx);
            }
        } else {
            if (refp[L0]->is_scaled || refp[L1]->is_scaled) {
                mc_bi_scaled(lc, inter, inter_stride, refp[L0], refp[L1], mvf,
                    x, y, w, h, c_idx);
            } else {
                mc_bi(lc, inter, inter_stride, refp[L0]->ref, refp[L1]->ref, mvf, orig_mvf,
                    x, y, w, h, c_idx, do_bdof);
            }
        }
        if (do_ciip) {
            const int intra_weight = ciip_derive_intra_weight(lc, x0, y0, sbw, sbh);
            fc->vvcdsp.intra.intra_pred(lc, x0, y0, sbw, sbh, c_idx);
            if (!c_idx && lc->sc->sh.r->sh_lmcs_used_flag)
                fc->vvcdsp.lmcs.filter(inter, inter_stride, w, h, &fc->ps.lmcs.fwd_lut);
            fc->vvcdsp.inter.put_ciip(dst, dst_stride, w, h, inter, inter_stride, intra_weight);
        }
    }
}

// 8.5.3.5 Parametric motion vector refinement process
static int parametric_mv_refine(const int *sad, const int stride)
{
    const int sad_minus  = sad[-stride];
    const int sad_center = sad[0];
    const int sad_plus   = sad[stride];
    int dmvc;
    int denom = (( sad_minus + sad_plus) - (sad_center << 1 ) ) << 3;
    if (!denom)
        dmvc = 0;
    else {
        if (sad_minus == sad_center)
            dmvc = -8;
        else if (sad_plus == sad_center)
            dmvc = 8;
        else {
            int num = ( sad_minus - sad_plus ) * (1 << 4);
            int sign_num = 0;
            int quotient = 0;
            int counter = 3;
            if (num < 0 ) {
                num = - num;
                sign_num = 1;
            }
            while (counter > 0) {
                counter = counter - 1;
                quotient = quotient << 1;
                if ( num >= denom ) {
                    num = num - denom;
                    quotient = quotient + 1;
                }
                denom = (denom >> 1);
            }
            if (sign_num == 1 )
                dmvc = -quotient;
            else
                dmvc = quotient;
        }
    }
    return dmvc;
}

#define SAD_ARRAY_SIZE 5
//8.5.3 Decoder-side motion vector refinement process
static void dmvr_mv_refine(VVCLocalContext *lc, MvField *mvf, MvField *orig_mv, int *sb_bdof_flag,
    const VVCFrame *ref0, const VVCFrame *ref1, const int x_off, const int y_off, const int block_w, const int block_h)
{
    const VVCFrameContext *fc = lc->fc;
    const int sr_range        = 2;
    const VVCFrame *refs[]    = { ref0, ref1 };
    int16_t *tmp[]            = { lc->tmp, lc->tmp1 };
    int sad[SAD_ARRAY_SIZE][SAD_ARRAY_SIZE];
    int min_dx, min_dy, min_sad, dx, dy;

    *orig_mv = *mvf;
    min_dx = min_dy = dx = dy = 2;

    for (int i = L0; i <= L1; i++) {
        const int pred_w        = block_w + 2 * sr_range;
        const int pred_h        = block_h + 2 * sr_range;
        const Mv *mv            = mvf->mv + i;
        const int mx            = mv->x & 0xf;
        const int my            = mv->y & 0xf;
        const int ox            = x_off + (mv->x >> 4) - sr_range;
        const int oy            = y_off + (mv->y >> 4) - sr_range;
        const VVCFrame *ref     = refs[i];
        ptrdiff_t src_stride    = ref->frame->linesize[LUMA];
        const uint8_t *src      = ref->frame->data[LUMA];
        const int wrap_enabled  = fc->ps.pps->r->pps_ref_wraparound_enabled_flag;

        MC_EMULATED_EDGE_BILINEAR(lc->edge_emu_buffer, &src, &src_stride, ox, oy);
        fc->vvcdsp.inter.dmvr[!!my][!!mx](tmp[i], src, src_stride, pred_h, mx, my, pred_w);
    }

    min_sad = fc->vvcdsp.inter.sad(tmp[L0], tmp[L1], dx, dy, block_w, block_h);
    min_sad -= min_sad >> 2;
    sad[dy][dx] = min_sad;

    if (min_sad >= block_w * block_h) {
        int dmv[2];
        // 8.5.3.4 Array entry selection process
        for (dy = 0; dy < SAD_ARRAY_SIZE; dy++) {
            for (dx = 0; dx < SAD_ARRAY_SIZE; dx++) {
                if (dx != sr_range || dy != sr_range) {
                    sad[dy][dx] = fc->vvcdsp.inter.sad(lc->tmp, lc->tmp1, dx, dy, block_w, block_h);
                    if (sad[dy][dx] < min_sad) {
                        min_sad = sad[dy][dx];
                        min_dx = dx;
                        min_dy = dy;
                    }
                }
            }
        }
        dmv[0] = (min_dx - sr_range) * (1 << 4);
        dmv[1] = (min_dy - sr_range) * (1 << 4);
        if (min_dx != 0 && min_dx != 4 && min_dy != 0 && min_dy != 4) {
            dmv[0] += parametric_mv_refine(&sad[min_dy][min_dx], 1);
            dmv[1] += parametric_mv_refine(&sad[min_dy][min_dx], SAD_ARRAY_SIZE);
        }

        for (int i = L0; i <= L1; i++) {
            Mv *mv = mvf->mv + i;
            mv->x += (1 - 2 * i) * dmv[0];
            mv->y += (1 - 2 * i) * dmv[1];
            ff_vvc_clip_mv(mv);
        }
    }
    if (min_sad < 2 * block_w * block_h) {
        *sb_bdof_flag = 0;
    }
}

static void set_dmvr_info(VVCFrameContext *fc, const int x0, const int y0,
    const int width, const int height, const MvField *mvf)

{
    const VVCPPS *pps = fc->ps.pps;

    for (int y = y0; y < y0 + height; y += MIN_PU_SIZE) {
        for (int x = x0; x < x0 + width; x += MIN_PU_SIZE) {
            const int idx = pps->min_pu_width * (y >> MIN_PU_LOG2) + (x >> MIN_PU_LOG2);
            fc->ref->tab_dmvr_mvf[idx] = *mvf;
        }
    }
}

static void derive_sb_mv(VVCLocalContext *lc, MvField *mv, MvField *orig_mv, int *sb_bdof_flag,
    const int x0, const int y0, const int sbw, const int sbh)
{
    VVCFrameContext *fc      = lc->fc;
    const PredictionUnit *pu = &lc->cu->pu;

    *orig_mv = *mv = *ff_vvc_get_mvf(fc, x0, y0);
    if (pu->bdof_flag)
        *sb_bdof_flag = 1;
    if (pu->dmvr_flag) {
        VVCRefPic *refp[2];
        if (pred_get_refs(lc, refp, mv) < 0)
            return;
        dmvr_mv_refine(lc, mv, orig_mv, sb_bdof_flag, refp[L0]->ref, refp[L1]->ref, x0, y0, sbw, sbh);
        set_dmvr_info(fc, x0, y0, sbw, sbh, mv);
    }
}

static void pred_regular_blk(VVCLocalContext *lc, const int skip_ciip)
{
    const CodingUnit *cu = lc->cu;
    PredictionUnit *pu   = &lc->cu->pu;
    const MotionInfo *mi = &pu->mi;
    MvField mv, orig_mv;
    int sbw, sbh, sb_bdof_flag = 0;

    if (cu->ciip_flag && skip_ciip)
        return;

    sbw = cu->cb_width / mi->num_sb_x;
    sbh = cu->cb_height / mi->num_sb_y;

    for (int sby = 0; sby < mi->num_sb_y; sby++) {
        for (int sbx = 0; sbx < mi->num_sb_x; sbx++) {
            const int x0 = cu->x0 + sbx * sbw;
            const int y0 = cu->y0 + sby * sbh;

            if (cu->ciip_flag)
                ff_vvc_set_neighbour_available(lc, x0, y0, sbw, sbh);

            derive_sb_mv(lc, &mv, &orig_mv, &sb_bdof_flag, x0, y0, sbw, sbh);
            pred_regular(lc, &mv, &orig_mv, x0, y0, sbw, sbh, sb_bdof_flag, LUMA);
        }
    }
}

static void derive_affine_mvc(MvField *mvc, const VVCFrameContext *fc, const MvField *mv,
    const int x0, const int y0, const int sbw, const int sbh)
{
    const int hs = fc->ps.sps->hshift[1];
    const int vs = fc->ps.sps->vshift[1];
    const MvField* mv2 = ff_vvc_get_mvf(fc, x0 + hs * sbw, y0 + vs * sbh);
    *mvc = *mv;

    // Due to different pred_flag, one of the motion vectors may have an invalid value.
    // Cast them to an unsigned type to avoid undefined behavior.
    mvc->mv[0].x += (unsigned int)mv2->mv[0].x;
    mvc->mv[0].y += (unsigned int)mv2->mv[0].y;
    mvc->mv[1].x += (unsigned int)mv2->mv[1].x;
    mvc->mv[1].y += (unsigned int)mv2->mv[1].y;
    ff_vvc_round_mv(mvc->mv + 0, 0, 1);
    ff_vvc_round_mv(mvc->mv + 1, 0, 1);
}

static void pred_affine_blk(VVCLocalContext *lc)
{
    const VVCFrameContext *fc = lc->fc;
    const CodingUnit *cu      = lc->cu;
    const PredictionUnit *pu  = &cu->pu;
    const MotionInfo *mi      = &pu->mi;
    const int x0              = cu->x0;
    const int y0              = cu->y0;
    const int sbw             = cu->cb_width / mi->num_sb_x;
    const int sbh             = cu->cb_height / mi->num_sb_y;
    const int hs              = fc->ps.sps->hshift[1];
    const int vs              = fc->ps.sps->vshift[1];
    const int dst_stride      = fc->frame->linesize[LUMA];

    for (int sby = 0; sby < mi->num_sb_y; sby++) {
        for (int sbx = 0; sbx < mi->num_sb_x; sbx++) {
            const int x = x0 + sbx * sbw;
            const int y = y0 + sby * sbh;

            uint8_t *dst0 = POS(0, x, y);
            const MvField *mv = ff_vvc_get_mvf(fc, x, y);
            VVCRefPic *refp[2];

            if (pred_get_refs(lc, refp, mv) < 0)
                return;

            if (mi->pred_flag != PF_BI) {
                const int lx = mi->pred_flag - PF_L0;
                if (refp[lx]->is_scaled) {
                    mc_uni_scaled(lc, dst0, dst_stride, refp[lx], mv, x, y, sbw, sbh, LUMA);
                } else {
                    luma_prof_uni(lc, dst0, dst_stride, refp[lx]->ref,
                        mv, x, y, sbw, sbh, pu->cb_prof_flag[lx],
                        pu->diff_mv_x[lx], pu->diff_mv_y[lx]);
                }
            } else {
                luma_prof_bi(lc, dst0, dst_stride, refp[L0], refp[L1], mv, x, y, sbw, sbh);
            }
            if (fc->ps.sps->r->sps_chroma_format_idc) {
                if (!av_zero_extend(sby, vs) && !av_zero_extend(sbx, hs)) {
                    MvField mvc;

                    derive_affine_mvc(&mvc, fc, mv, x, y, sbw, sbh);
                    pred_regular(lc, &mvc, NULL, x, y, sbw << hs, sbh << vs, 0, CB);
                }
            }

        }
    }
}

static void predict_inter(VVCLocalContext *lc)
{
    const VVCFrameContext *fc = lc->fc;
    const CodingUnit *cu      = lc->cu;
    const PredictionUnit *pu  = &cu->pu;

    if (pu->merge_gpm_flag)
        pred_gpm_blk(lc);
    else if (pu->inter_affine_flag)
        pred_affine_blk(lc);
    else
        pred_regular_blk(lc, 1);    //intra block is not ready yet, skip ciip

    if (lc->sc->sh.r->sh_lmcs_used_flag && !cu->ciip_flag) {
        uint8_t* dst0 = POS(0, cu->x0, cu->y0);
        fc->vvcdsp.lmcs.filter(dst0, fc->frame->linesize[LUMA], cu->cb_width, cu->cb_height, &fc->ps.lmcs.fwd_lut);
    }
}

static int has_inter_luma(const CodingUnit *cu)
{
    return (cu->pred_mode == MODE_INTER || cu->pred_mode == MODE_SKIP) && cu->tree_type != DUAL_TREE_CHROMA;
}

int ff_vvc_predict_inter(VVCLocalContext *lc, const int rs)
{
    const VVCFrameContext *fc = lc->fc;
    CodingUnit *cu            = fc->tab.cus[rs];

    while (cu) {
        lc->cu = cu;
        if (has_inter_luma(cu))
            predict_inter(lc);
        cu = cu->next;
    }

    return 0;
}

void ff_vvc_predict_ciip(VVCLocalContext *lc)
{
    av_assert0(lc->cu->ciip_flag);

    //todo: refact out ciip from pred_regular_blk
    pred_regular_blk(lc, 0);
}

#undef POS
