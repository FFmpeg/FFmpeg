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

static void subpic_offset(int *x_off, int *y_off,
    const VVCSPS *sps, const VVCPPS *pps, const int subpic_idx, const int is_luma)
{
    *x_off -= pps->subpic_x[subpic_idx] >> sps->hshift[!is_luma];
    *y_off -= pps->subpic_y[subpic_idx] >> sps->vshift[!is_luma];
}

static void subpic_width_height(int *pic_width, int *pic_height,
    const VVCSPS *sps, const VVCPPS *pps, const int subpic_idx, const int is_luma)
{
    *pic_width  = pps->subpic_width[subpic_idx]  >> sps->hshift[!is_luma];
    *pic_height = pps->subpic_height[subpic_idx] >> sps->vshift[!is_luma];
}

static int emulated_edge(const VVCLocalContext *lc, uint8_t *dst, const uint8_t **src, ptrdiff_t *src_stride,
    int x_off, int y_off, const int block_w, const int block_h, const int is_luma)
{
    const VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps         = fc->ps.sps;
    const VVCPPS *pps         = fc->ps.pps;
    const int subpic_idx      = lc->sc->sh.r->curr_subpic_idx;
    const int extra_before    = is_luma ? LUMA_EXTRA_BEFORE : CHROMA_EXTRA_BEFORE;
    const int extra_after     = is_luma ? LUMA_EXTRA_AFTER : CHROMA_EXTRA_AFTER;
    const int extra           = is_luma ? LUMA_EXTRA : CHROMA_EXTRA;
    int pic_width, pic_height;

    subpic_offset(&x_off, &y_off, sps, pps, subpic_idx, is_luma);
    subpic_width_height(&pic_width, &pic_height, sps, pps, subpic_idx, is_luma);

    if (x_off < extra_before || y_off < extra_before ||
        x_off >= pic_width - block_w - extra_after ||
        y_off >= pic_height - block_h - extra_after) {
        const ptrdiff_t edge_emu_stride = EDGE_EMU_BUFFER_STRIDE << fc->ps.sps->pixel_shift;
        int offset     = extra_before * *src_stride      + (extra_before << fc->ps.sps->pixel_shift);
        int buf_offset = extra_before * edge_emu_stride + (extra_before << fc->ps.sps->pixel_shift);

        fc->vdsp.emulated_edge_mc(dst, *src - offset, edge_emu_stride, *src_stride,
            block_w + extra, block_h + extra, x_off - extra_before, y_off - extra_before,
            pic_width, pic_height);

        *src = dst + buf_offset;
        *src_stride = edge_emu_stride;
        return 1;
    }
    return 0;
}

static void emulated_edge_dmvr(const VVCLocalContext *lc, uint8_t *dst, const uint8_t **src, ptrdiff_t *src_stride,
    int x_sb, int y_sb, int x_off,  int y_off, const int block_w, const int block_h, const int is_luma)
{
    const VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps         = fc->ps.sps;
    const VVCPPS *pps         = fc->ps.pps;
    const int subpic_idx      = lc->sc->sh.r->curr_subpic_idx;
    const int extra_before    = is_luma ? LUMA_EXTRA_BEFORE : CHROMA_EXTRA_BEFORE;
    const int extra_after     = is_luma ? LUMA_EXTRA_AFTER : CHROMA_EXTRA_AFTER;
    const int extra           = is_luma ? LUMA_EXTRA : CHROMA_EXTRA;
    int pic_width, pic_height;

    subpic_offset(&x_off, &y_off, sps, pps, subpic_idx, is_luma);
    subpic_offset(&x_sb, &y_sb, sps, pps, subpic_idx, is_luma);
    subpic_width_height(&pic_width, &pic_height, sps, pps, subpic_idx, is_luma);

    if (x_off < extra_before || y_off < extra_before ||
        x_off >= pic_width - block_w - extra_after ||
        y_off >= pic_height - block_h - extra_after||
        (x_off != x_sb || y_off !=  y_sb)) {
        const int ps                    = fc->ps.sps->pixel_shift;
        const ptrdiff_t edge_emu_stride = EDGE_EMU_BUFFER_STRIDE << ps;
        const int offset                = extra_before * *src_stride + (extra_before << ps);
        const int buf_offset            = extra_before * edge_emu_stride + (extra_before << ps);

        const int start_x               = FFMIN(FFMAX(x_sb - extra_before, 0), pic_width  - 1);
        const int start_y               = FFMIN(FFMAX(y_sb - extra_before, 0), pic_height - 1);
        const int width                 = FFMAX(FFMIN(pic_width, x_sb + block_w + extra_after) - start_x, 1);
        const int height                = FFMAX(FFMIN(pic_height, y_sb + block_h + extra_after) - start_y, 1);

        fc->vdsp.emulated_edge_mc(dst, *src - offset, edge_emu_stride, *src_stride, block_w + extra, block_h + extra,
            x_off - start_x - extra_before, y_off - start_y - extra_before, width, height);

        *src = dst + buf_offset;
        *src_stride = edge_emu_stride;
   }
}

static void emulated_edge_bilinear(const VVCLocalContext *lc, uint8_t *dst, const uint8_t **src, ptrdiff_t *src_stride,
    int x_off, int y_off, const int block_w, const int block_h)
{
    const VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps         = fc->ps.sps;
    const VVCPPS *pps         = fc->ps.pps;
    const int subpic_idx      = lc->sc->sh.r->curr_subpic_idx;
    int pic_width, pic_height;

    subpic_offset(&x_off, &y_off, sps, pps, subpic_idx, 1);
    subpic_width_height(&pic_width, &pic_height, sps, pps, subpic_idx, 1);

    if (x_off < BILINEAR_EXTRA_BEFORE || y_off < BILINEAR_EXTRA_BEFORE ||
        x_off >= pic_width - block_w - BILINEAR_EXTRA_AFTER ||
        y_off >= pic_height - block_h - BILINEAR_EXTRA_AFTER) {
        const ptrdiff_t edge_emu_stride = EDGE_EMU_BUFFER_STRIDE << fc->ps.sps->pixel_shift;
        const int offset                = BILINEAR_EXTRA_BEFORE * *src_stride + (BILINEAR_EXTRA_BEFORE << fc->ps.sps->pixel_shift);
        const int buf_offset            = BILINEAR_EXTRA_BEFORE * edge_emu_stride + (BILINEAR_EXTRA_BEFORE << fc->ps.sps->pixel_shift);

        fc->vdsp.emulated_edge_mc(dst, *src - offset, edge_emu_stride, *src_stride, block_w + BILINEAR_EXTRA, block_h + BILINEAR_EXTRA,
            x_off - BILINEAR_EXTRA_BEFORE, y_off - BILINEAR_EXTRA_BEFORE,  pic_width, pic_height);

        *src = dst + buf_offset;
        *src_stride = edge_emu_stride;
    }
}


#define EMULATED_EDGE_LUMA(dst, src, src_stride, x_off, y_off)                      \
    emulated_edge(lc, dst, src, src_stride, x_off, y_off, block_w, block_h, 1)

#define EMULATED_EDGE_CHROMA(dst, src, src_stride, x_off, y_off)                    \
    emulated_edge(lc, dst, src, src_stride, x_off, y_off, block_w, block_h, 0)

#define EMULATED_EDGE_DMVR_LUMA(dst, src, src_stride, x_sb, y_sb, x_off, y_off)     \
    emulated_edge_dmvr(lc, dst, src, src_stride, x_sb, y_sb, x_off, y_off, block_w, block_h, 1)

#define EMULATED_EDGE_DMVR_CHROMA(dst, src, src_stride, x_sb, y_sb, x_off, y_off)   \
    emulated_edge_dmvr(lc, dst, src, src_stride, x_sb, y_sb, x_off, y_off, block_w, block_h, 0)

#define EMULATED_EDGE_BILINEAR(dst, src, src_stride, x_off, y_off)                  \
    emulated_edge_bilinear(lc, dst, src, src_stride, x_off, y_off, pred_w, pred_h)

// part of 8.5.6.6 Weighted sample prediction process
static int derive_weight_uni(int *denom, int *wx, int *ox,
    const VVCLocalContext *lc, const MvField *mvf, const int c_idx)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCPPS *pps           = fc->ps.pps;
    const VVCSH *sh             = &lc->sc->sh;
    const int weight_flag       = (IS_P(sh->r) && pps->r->pps_weighted_pred_flag) ||
                                  (IS_B(sh->r) && pps->r->pps_weighted_bipred_flag);
    if (weight_flag) {
        const int lx                = mvf->pred_flag - PF_L0;
        const PredWeightTable *w    = pps->r->pps_wp_info_in_ph_flag ? &fc->ps.ph.pwt : &sh->pwt;

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
    const VVCFrameContext *fc   = lc->fc;
    const VVCPPS *pps           = fc->ps.pps;
    const VVCSH *sh             = &lc->sc->sh;
    const int bcw_idx           = mvf->bcw_idx;
    const int weight_flag       = (IS_P(sh->r) && pps->r->pps_weighted_pred_flag) ||
                                  (IS_B(sh->r) && pps->r->pps_weighted_bipred_flag && !dmvr_flag);
    if ((!weight_flag && !bcw_idx) || (bcw_idx && lc->cu->ciip_flag))
        return 0;

    if (bcw_idx) {
        *denom = 2;
        *w1 = bcw_w_lut[bcw_idx];
        *w0 = 8 - *w1;
        *o0 = *o1 = 0;
    } else {
        const VVCPPS *pps = fc->ps.pps;
        const PredWeightTable *w = pps->r->pps_wp_info_in_ph_flag ? &fc->ps.ph.pwt : &sh->pwt;

        *denom = w->log2_denom[c_idx > 0];
        *w0 = w->weight[L0][c_idx][mvf->ref_idx[L0]];
        *w1 = w->weight[L1][c_idx][mvf->ref_idx[L1]];
        *o0 = w->offset[L0][c_idx][mvf->ref_idx[L0]];
        *o1 = w->offset[L1][c_idx][mvf->ref_idx[L1]];
    }
    return 1;
}

static void luma_mc(VVCLocalContext *lc, int16_t *dst, const AVFrame *ref, const Mv *mv,
    int x_off, int y_off, const int block_w, const int block_h)
{
    const VVCFrameContext *fc   = lc->fc;
    const uint8_t *src          = ref->data[0];
    ptrdiff_t src_stride        = ref->linesize[0];
    const int idx               = av_log2(block_w) - 1;
    const int mx                = mv->x & 0xf;
    const int my                = mv->y & 0xf;
    const int8_t *hf            = ff_vvc_inter_luma_filters[0][mx];
    const int8_t *vf            = ff_vvc_inter_luma_filters[0][my];

    x_off += mv->x >> 4;
    y_off += mv->y >> 4;
    src   += y_off * src_stride + (x_off * (1 << fc->ps.sps->pixel_shift));

    EMULATED_EDGE_LUMA(lc->edge_emu_buffer, &src, &src_stride, x_off, y_off);

    fc->vvcdsp.inter.put[LUMA][idx][!!my][!!mx](dst, src, src_stride, block_h, hf, vf, block_w);
}

static void chroma_mc(VVCLocalContext *lc, int16_t *dst, const AVFrame *ref, const Mv *mv,
    int x_off, int y_off, const int block_w, const int block_h, const int c_idx)
{
    const VVCFrameContext *fc   = lc->fc;
    const uint8_t *src          = ref->data[c_idx];
    ptrdiff_t src_stride        = ref->linesize[c_idx];
    int hs                      = fc->ps.sps->hshift[c_idx];
    int vs                      = fc->ps.sps->vshift[c_idx];
    const int idx               = av_log2(block_w) - 1;
    const intptr_t mx           = av_mod_uintp2(mv->x, 4 + hs) << (1 - hs);
    const intptr_t my           = av_mod_uintp2(mv->y, 4 + vs) << (1 - vs);
    const int8_t *hf            = ff_vvc_inter_chroma_filters[0][mx];
    const int8_t *vf            = ff_vvc_inter_chroma_filters[0][my];

    x_off += mv->x >> (4 + hs);
    y_off += mv->y >> (4 + vs);
    src  += y_off * src_stride + (x_off * (1 << fc->ps.sps->pixel_shift));

    EMULATED_EDGE_CHROMA(lc->edge_emu_buffer, &src, &src_stride, x_off, y_off);
    fc->vvcdsp.inter.put[CHROMA][idx][!!my][!!mx](dst, src, src_stride, block_h, hf, vf, block_w);
}

static void luma_mc_uni(VVCLocalContext *lc, uint8_t *dst, const ptrdiff_t dst_stride,
    const AVFrame *ref, const MvField *mvf, int x_off, int y_off, const int block_w, const int block_h,
    const int hf_idx, const int vf_idx)
{
    const VVCFrameContext *fc   = lc->fc;
    const int lx                = mvf->pred_flag - PF_L0;
    const Mv *mv                = mvf->mv + lx;
    const uint8_t *src          = ref->data[0];
    ptrdiff_t src_stride        = ref->linesize[0];
    const int idx               = av_log2(block_w) - 1;
    const int mx                = mv->x & 0xf;
    const int my                = mv->y & 0xf;
    const int8_t *hf            = ff_vvc_inter_luma_filters[hf_idx][mx];
    const int8_t *vf            = ff_vvc_inter_luma_filters[vf_idx][my];
    int denom, wx, ox;

    x_off += mv->x >> 4;
    y_off += mv->y >> 4;
    src   += y_off * src_stride + (x_off * (1 << fc->ps.sps->pixel_shift));

    EMULATED_EDGE_LUMA(lc->edge_emu_buffer, &src, &src_stride, x_off, y_off);

    if (derive_weight_uni(&denom, &wx, &ox, lc, mvf, LUMA)) {
        fc->vvcdsp.inter.put_uni_w[LUMA][idx][!!my][!!mx](dst, dst_stride, src, src_stride,
            block_h, denom, wx, ox, hf, vf, block_w);
    } else {
        fc->vvcdsp.inter.put_uni[LUMA][idx][!!my][!!mx](dst, dst_stride, src, src_stride,
            block_h, hf, vf, block_w);
    }
}

static void luma_mc_bi(VVCLocalContext *lc, uint8_t *dst, const ptrdiff_t dst_stride,
    const AVFrame *ref0, const Mv *mv0, const int x_off, const int y_off, const int block_w, const int block_h,
    const AVFrame *ref1, const Mv *mv1, const MvField *mvf, const int hf_idx, const int vf_idx,
    const MvField *orig_mv, const int sb_bdof_flag)
{
    const VVCFrameContext *fc   = lc->fc;
    const PredictionUnit *pu    = &lc->cu->pu;
    const int idx               = av_log2(block_w) - 1;
    const AVFrame *ref[]        = { ref0, ref1 };
    int16_t *tmp[]              = { lc->tmp + sb_bdof_flag * PROF_TEMP_OFFSET, lc->tmp1 + sb_bdof_flag * PROF_TEMP_OFFSET };
    int denom, w0, w1, o0, o1;
    const int weight_flag       = derive_weight(&denom, &w0, &w1, &o0, &o1, lc, mvf, LUMA, pu->dmvr_flag);

    for (int i = L0; i <= L1; i++) {
        const Mv *mv            = mvf->mv + i;
        const int mx            = mv->x & 0xf;
        const int my            = mv->y & 0xf;
        const int ox            = x_off + (mv->x >> 4);
        const int oy            = y_off + (mv->y >> 4);
        ptrdiff_t src_stride    = ref[i]->linesize[0];
        const uint8_t *src      = ref[i]->data[0] + oy * src_stride + (ox * (1 << fc->ps.sps->pixel_shift));
        const int8_t *hf        = ff_vvc_inter_luma_filters[hf_idx][mx];
        const int8_t *vf        = ff_vvc_inter_luma_filters[vf_idx][my];

        if (pu->dmvr_flag) {
            const int x_sb = x_off + (orig_mv->mv[i].x >> 4);
            const int y_sb = y_off + (orig_mv->mv[i].y >> 4);

            EMULATED_EDGE_DMVR_LUMA(lc->edge_emu_buffer, &src, &src_stride, x_sb, y_sb, ox, oy);
        } else {
            EMULATED_EDGE_LUMA(lc->edge_emu_buffer, &src, &src_stride, ox, oy);
        }
        fc->vvcdsp.inter.put[LUMA][idx][!!my][!!mx](tmp[i], src, src_stride, block_h, hf, vf, block_w);
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

static void chroma_mc_uni(VVCLocalContext *lc, uint8_t *dst, const ptrdiff_t dst_stride,
    const uint8_t *src, ptrdiff_t src_stride, int x_off, int y_off,
    const int block_w, const int block_h, const MvField *mvf, const int c_idx,
    const int hf_idx, const int vf_idx)
{
    const VVCFrameContext *fc   = lc->fc;
    const int lx                = mvf->pred_flag - PF_L0;
    const int hs                = fc->ps.sps->hshift[1];
    const int vs                = fc->ps.sps->vshift[1];
    const int idx               = av_log2(block_w) - 1;
    const Mv *mv                = &mvf->mv[lx];
    const intptr_t mx           = av_mod_uintp2(mv->x, 4 + hs) << (1 - hs);
    const intptr_t my           = av_mod_uintp2(mv->y, 4 + vs) << (1 - vs);
    const int8_t *hf            = ff_vvc_inter_chroma_filters[hf_idx][mx];
    const int8_t *vf            = ff_vvc_inter_chroma_filters[vf_idx][my];
    int denom, wx, ox;

    x_off += mv->x >> (4 + hs);
    y_off += mv->y >> (4 + vs);
    src  += y_off * src_stride + (x_off * (1 << fc->ps.sps->pixel_shift));


    EMULATED_EDGE_CHROMA(lc->edge_emu_buffer, &src, &src_stride, x_off, y_off);
    if (derive_weight_uni(&denom, &wx, &ox, lc, mvf, c_idx)) {
        fc->vvcdsp.inter.put_uni_w[CHROMA][idx][!!my][!!mx](dst, dst_stride, src, src_stride,
            block_h, denom, wx, ox, hf, vf, block_w);
    } else {
        fc->vvcdsp.inter.put_uni[CHROMA][idx][!!my][!!mx](dst, dst_stride, src, src_stride,
            block_h, hf, vf, block_w);
    }
}

static void chroma_mc_bi(VVCLocalContext *lc, uint8_t *dst, const ptrdiff_t dst_stride,
    const AVFrame *ref0, const AVFrame *ref1, const int x_off, const int y_off,
    const int block_w, const int block_h,  const MvField *mvf, const int c_idx,
    const int hf_idx, const int vf_idx, const MvField *orig_mv, const int dmvr_flag, const int ciip_flag)
{
    const VVCFrameContext *fc   = lc->fc;
    const int hs                = fc->ps.sps->hshift[1];
    const int vs                = fc->ps.sps->vshift[1];
    const int idx               = av_log2(block_w) - 1;
    const AVFrame *ref[]        = { ref0, ref1 };
    int16_t *tmp[]              = { lc->tmp, lc->tmp1 };
    int denom, w0, w1, o0, o1;
    const int weight_flag       = derive_weight(&denom, &w0, &w1, &o0, &o1, lc, mvf, c_idx, dmvr_flag);

    for (int i = L0; i <= L1; i++) {
        const Mv *mv            = mvf->mv + i;
        const int mx            = av_mod_uintp2(mv->x, 4 + hs) << (1 - hs);
        const int my            = av_mod_uintp2(mv->y, 4 + vs) << (1 - vs);
        const int ox            = x_off + (mv->x >> (4 + hs));
        const int oy            = y_off + (mv->y >> (4 + vs));
        ptrdiff_t src_stride    = ref[i]->linesize[c_idx];
        const uint8_t *src      = ref[i]->data[c_idx] + oy * src_stride + (ox * (1 << fc->ps.sps->pixel_shift));
        const int8_t *hf        = ff_vvc_inter_chroma_filters[hf_idx][mx];
        const int8_t *vf        = ff_vvc_inter_chroma_filters[vf_idx][my];
        if (dmvr_flag) {
            const int x_sb = x_off + (orig_mv->mv[i].x >> (4 + hs));
            const int y_sb = y_off + (orig_mv->mv[i].y >> (4 + vs));
            EMULATED_EDGE_DMVR_CHROMA(lc->edge_emu_buffer,  &src, &src_stride, x_sb, y_sb, ox, oy);
        } else {
            EMULATED_EDGE_CHROMA(lc->edge_emu_buffer, &src, &src_stride, ox, oy);
        }
        fc->vvcdsp.inter.put[CHROMA][idx][!!my][!!mx](tmp[i],  src, src_stride, block_h, hf, vf, block_w);
    }
    if (weight_flag)
        fc->vvcdsp.inter.w_avg(dst, dst_stride, tmp[L0], tmp[L1], block_w, block_h, denom, w0, w1, o0, o1);
    else
        fc->vvcdsp.inter.avg(dst, dst_stride, tmp[L0], tmp[L1], block_w, block_h);
}

static void luma_prof_uni(VVCLocalContext *lc, uint8_t *dst, const ptrdiff_t dst_stride,
    const AVFrame *ref, const MvField *mvf, int x_off, int y_off, const int block_w, const int block_h,
    const int cb_prof_flag, const int16_t *diff_mv_x, const int16_t *diff_mv_y)
{
    const VVCFrameContext *fc   = lc->fc;
    const uint8_t *src          = ref->data[0];
    ptrdiff_t src_stride        = ref->linesize[0];
    uint16_t *prof_tmp          = lc->tmp + PROF_TEMP_OFFSET;
    const int idx               = av_log2(block_w) - 1;
    const int lx                = mvf->pred_flag - PF_L0;
    const Mv *mv                = mvf->mv + lx;
    const int mx                = mv->x & 0xf;
    const int my                = mv->y & 0xf;
    const int8_t *hf            = ff_vvc_inter_luma_filters[2][mx];
    const int8_t *vf            = ff_vvc_inter_luma_filters[2][my];
    int denom, wx, ox;
    const int weight_flag       = derive_weight_uni(&denom, &wx, &ox, lc, mvf, LUMA);

    x_off += mv->x >> 4;
    y_off += mv->y >> 4;
    src   += y_off * src_stride + (x_off * (1 << fc->ps.sps->pixel_shift));

    EMULATED_EDGE_LUMA(lc->edge_emu_buffer, &src, &src_stride, x_off, y_off);
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

static void luma_prof_bi(VVCLocalContext *lc, uint8_t *dst, const ptrdiff_t dst_stride,
    const AVFrame *ref0, const AVFrame *ref1, const MvField *mvf, const int x_off, const int y_off,
    const int block_w, const int block_h)
{
    const VVCFrameContext *fc   = lc->fc;
    const PredictionUnit *pu    = &lc->cu->pu;
    const AVFrame *ref[]        = { ref0, ref1 };
    int16_t *tmp[]              = { lc->tmp, lc->tmp1 };
    uint16_t *prof_tmp          = lc->tmp2 + PROF_TEMP_OFFSET;
    const int idx               = av_log2(block_w) - 1;
    int denom, w0, w1, o0, o1;
    const int weight_flag       = derive_weight(&denom, &w0, &w1, &o0, &o1, lc, mvf, LUMA, 0);

    for (int i = L0; i <= L1; i++) {
        const Mv *mv            = mvf->mv + i;
        const int mx            = mv->x & 0xf;
        const int my            = mv->y & 0xf;
        const int ox            = x_off + (mv->x >> 4);
        const int oy            = y_off + (mv->y >> 4);
        ptrdiff_t src_stride    = ref[i]->linesize[0];
        const uint8_t *src      = ref[i]->data[0] + oy * src_stride + (ox * (1 << fc->ps.sps->pixel_shift));
        const int8_t *hf        = ff_vvc_inter_luma_filters[2][mx];
        const int8_t *vf        = ff_vvc_inter_luma_filters[2][my];

        EMULATED_EDGE_LUMA(lc->edge_emu_buffer, &src, &src_stride, ox, oy);
        if (!pu->cb_prof_flag[i]) {
            fc->vvcdsp.inter.put[LUMA][idx][!!my][!!mx](tmp[i], src, src_stride, block_h, hf, vf, block_w);
        } else {
            fc->vvcdsp.inter.put[LUMA][idx][!!my][!!mx](prof_tmp, src, src_stride, AFFINE_MIN_BLOCK_SIZE, hf, vf, AFFINE_MIN_BLOCK_SIZE);
            fc->vvcdsp.inter.fetch_samples(prof_tmp, src, src_stride, mx, my);
            fc->vvcdsp.inter.apply_prof(tmp[i], prof_tmp, pu->diff_mv_x[i], pu->diff_mv_y[i]);
        }
    }

    if (weight_flag)
        fc->vvcdsp.inter.w_avg(dst, dst_stride, tmp[L0], tmp[L1], block_w, block_h,  denom, w0, w1, o0, o1);
    else
        fc->vvcdsp.inter.avg(dst, dst_stride, tmp[L0], tmp[L1], block_w, block_h);
}

static int pred_get_refs(const VVCLocalContext *lc, VVCFrame *ref[2],  const MvField *mv)
{
    const RefPicList *rpl = lc->sc->rpl;

    for (int mask = PF_L0; mask <= PF_L1; mask++) {
        if (mv->pred_flag & mask) {
            const int lx = mask - PF_L0;
            ref[lx] = rpl[lx].ref[mv->ref_idx[lx]];
            if (!ref[lx])
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
    const VVCFrameContext *fc  = lc->fc;
    const CodingUnit *cu       = lc->cu;
    const PredictionUnit *pu   = &cu->pu;

    const uint8_t angle_idx   = ff_vvc_gpm_angle_idx[pu->gpm_partition_idx];
    const uint8_t weights_idx = ff_vvc_gpm_angle_to_weights_idx[angle_idx];
    const int w = av_log2(cu->cb_width) - 3;
    const int h = av_log2(cu->cb_height) - 3;
    const uint8_t off_x = ff_vvc_gpm_weights_offset_x[pu->gpm_partition_idx][h][w];
    const uint8_t off_y = ff_vvc_gpm_weights_offset_y[pu->gpm_partition_idx][h][w];
    const uint8_t mirror_type = ff_vvc_gpm_angle_to_mirror[angle_idx];
    const uint8_t *weights;

    const int c_end = fc->ps.sps->r->sps_chroma_format_idc ? 3 : 1;

    int16_t *tmp[2] = {lc->tmp, lc->tmp1};

    for (int c_idx = 0; c_idx < c_end; c_idx++) {
        const int hs     = fc->ps.sps->hshift[c_idx];
        const int vs     = fc->ps.sps->vshift[c_idx];
        const int x      = lc->cu->x0  >> hs;
        const int y      = lc->cu->y0  >> vs;
        const int width  = cu->cb_width >> hs;
        const int height = cu->cb_height >> vs;
        uint8_t *dst = POS(c_idx, lc->cu->x0, lc->cu->y0);
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
            const int lx = mv->pred_flag - PF_L0;
            VVCFrame *ref = lc->sc->rpl[lx].ref[mv->ref_idx[lx]];
            if (!ref)
                return;
            if (c_idx)
                chroma_mc(lc, tmp[i], ref->frame, mv->mv + lx, x, y, width, height, c_idx);
            else
                luma_mc(lc, tmp[i], ref->frame, mv->mv + lx, x, y, width, height);
        }
        fc->vvcdsp.inter.put_gpm(dst, dst_stride, width, height, tmp[0], tmp[1], weights, step_x, step_y);
    }
    return;
}

static int ciip_derive_intra_weight(const VVCLocalContext *lc, const int x0, const int y0,
    const int width, const int height)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCSPS *sps           = fc->ps.sps;
    const int x0b               = av_mod_uintp2(x0, sps->ctb_log2_size_y);
    const int y0b               = av_mod_uintp2(y0, sps->ctb_log2_size_y);
    const int available_l       = lc->ctb_left_flag || x0b;
    const int available_u       = lc->ctb_up_flag || y0b;
    const int min_pu_width      = fc->ps.pps->min_pu_width;

    int w = 1;

    if (available_u &&fc->tab.mvf[((y0 - 1) >> MIN_PU_LOG2) * min_pu_width + ((x0 - 1 + width)>> MIN_PU_LOG2)].pred_flag == PF_INTRA)
        w++;

    if (available_l && fc->tab.mvf[((y0 - 1 + height)>> MIN_PU_LOG2) * min_pu_width + ((x0 - 1) >> MIN_PU_LOG2)].pred_flag == PF_INTRA)
        w++;

    return w;
}

static void pred_regular_luma(VVCLocalContext *lc, const int hf_idx, const int vf_idx, const MvField *mv,
    const int x0, const int y0, const int sbw, const int sbh, const MvField *orig_mv, const int sb_bdof_flag)
{
    const SliceContext *sc          = lc->sc;
    const VVCFrameContext *fc       = lc->fc;
    const int ciip_flag             = lc->cu->ciip_flag;
    uint8_t *dst                    = POS(0, x0, y0);
    const ptrdiff_t dst_stride      = fc->frame->linesize[0];
    uint8_t *inter                  = ciip_flag ? (uint8_t *)lc->ciip_tmp1 : dst;
    const ptrdiff_t inter_stride    = ciip_flag ? (MAX_PB_SIZE * sizeof(uint16_t)) : dst_stride;
    VVCFrame *ref[2];

    if (pred_get_refs(lc, ref, mv) < 0)
        return;

    if (mv->pred_flag != PF_BI) {
        const int lx = mv->pred_flag - PF_L0;
        luma_mc_uni(lc, inter, inter_stride, ref[lx]->frame,
            mv, x0, y0, sbw, sbh, hf_idx, vf_idx);
    } else {
        luma_mc_bi(lc, inter, inter_stride, ref[0]->frame,
            &mv->mv[0], x0, y0, sbw, sbh, ref[1]->frame, &mv->mv[1], mv,
            hf_idx, vf_idx, orig_mv, sb_bdof_flag);
    }

    if (ciip_flag) {
        const int intra_weight = ciip_derive_intra_weight(lc, x0, y0, sbw, sbh);
        fc->vvcdsp.intra.intra_pred(lc, x0, y0, sbw, sbh, 0);
        if (sc->sh.r->sh_lmcs_used_flag)
            fc->vvcdsp.lmcs.filter(inter, inter_stride, sbw, sbh, &fc->ps.lmcs.fwd_lut);
        fc->vvcdsp.inter.put_ciip(dst, dst_stride, sbw, sbh, inter, inter_stride, intra_weight);

    }
}

static void pred_regular_chroma(VVCLocalContext *lc, const MvField *mv,
    const int x0, const int y0, const int sbw, const int sbh, const MvField *orig_mv, const int dmvr_flag)
{
    const VVCFrameContext *fc   = lc->fc;
    const int hs                = fc->ps.sps->hshift[1];
    const int vs                = fc->ps.sps->vshift[1];
    const int x0_c              = x0 >> hs;
    const int y0_c              = y0 >> vs;
    const int w_c               = sbw >> hs;
    const int h_c               = sbh >> vs;
    const int do_ciip           = lc->cu->ciip_flag && (w_c > 2);

    uint8_t* dst1               = POS(1, x0, y0);
    uint8_t* dst2               = POS(2, x0, y0);
    const ptrdiff_t dst1_stride = fc->frame->linesize[1];
    const ptrdiff_t dst2_stride = fc->frame->linesize[2];

    uint8_t *inter1 = do_ciip ? (uint8_t *)lc->ciip_tmp1 : dst1;
    const ptrdiff_t inter1_stride = do_ciip ? (MAX_PB_SIZE * sizeof(uint16_t)) : dst1_stride;

    uint8_t *inter2 = do_ciip ? (uint8_t *)lc->ciip_tmp2 : dst2;
    const ptrdiff_t inter2_stride = do_ciip ? (MAX_PB_SIZE * sizeof(uint16_t)) : dst2_stride;

    //fix me
    const int hf_idx = 0;
    const int vf_idx = 0;
    VVCFrame *ref[2];

    if (pred_get_refs(lc, ref, mv) < 0)
        return;

    if (mv->pred_flag != PF_BI) {
        const int lx = mv->pred_flag - PF_L0;
        if (!ref[lx])
            return;

        chroma_mc_uni(lc, inter1, inter1_stride, ref[lx]->frame->data[1], ref[lx]->frame->linesize[1],
            x0_c, y0_c, w_c, h_c, mv, CB, hf_idx, vf_idx);
        chroma_mc_uni(lc, inter2, inter2_stride, ref[lx]->frame->data[2], ref[lx]->frame->linesize[2],
            x0_c, y0_c, w_c, h_c, mv, CR, hf_idx, vf_idx);
    } else {
        if (!ref[0] || !ref[1])
            return;

        chroma_mc_bi(lc, inter1, inter1_stride, ref[0]->frame, ref[1]->frame,
            x0_c, y0_c, w_c, h_c, mv, CB, hf_idx, vf_idx, orig_mv, dmvr_flag, lc->cu->ciip_flag);

        chroma_mc_bi(lc, inter2, inter2_stride, ref[0]->frame, ref[1]->frame,
            x0_c, y0_c, w_c, h_c, mv, CR, hf_idx, vf_idx, orig_mv, dmvr_flag, lc->cu->ciip_flag);

    }
    if (do_ciip) {
        const int intra_weight = ciip_derive_intra_weight(lc, x0, y0, sbw, sbh);
        fc->vvcdsp.intra.intra_pred(lc, x0, y0, sbw, sbh, 1);
        fc->vvcdsp.intra.intra_pred(lc, x0, y0, sbw, sbh, 2);
        fc->vvcdsp.inter.put_ciip(dst1, dst1_stride, w_c, h_c, inter1, inter1_stride, intra_weight);
        fc->vvcdsp.inter.put_ciip(dst2, dst2_stride, w_c, h_c, inter2, inter2_stride, intra_weight);

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
    const AVFrame *ref0, const AVFrame *ref1, const int x_off, const int y_off, const int block_w, const int block_h)
{
    const VVCFrameContext *fc   = lc->fc;
    const int sr_range          = 2;
    const AVFrame *ref[]        = { ref0, ref1 };
    int16_t *tmp[]              = { lc->tmp, lc->tmp1 };
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
        ptrdiff_t src_stride    = ref[i]->linesize[LUMA];
        const uint8_t *src      = ref[i]->data[LUMA] + oy * src_stride + (ox * (1 << fc->ps.sps->pixel_shift));
        EMULATED_EDGE_BILINEAR(lc->edge_emu_buffer, &src, &src_stride, ox, oy);
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
        VVCFrame* ref[2];
        if (pred_get_refs(lc, ref, mv) < 0)
            return;
        dmvr_mv_refine(lc, mv, orig_mv, sb_bdof_flag, ref[0]->frame, ref[1]->frame, x0, y0, sbw, sbh);
        set_dmvr_info(fc, x0, y0, sbw, sbh, mv);
    }
}

static void pred_regular_blk(VVCLocalContext *lc, const int skip_ciip)
{
    const VVCFrameContext *fc   = lc->fc;
    const CodingUnit *cu        = lc->cu;
    PredictionUnit *pu          = &lc->cu->pu;
    const MotionInfo *mi        = &pu->mi;
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
            pred_regular_luma(lc, mi->hpel_if_idx, mi->hpel_if_idx, &mv, x0, y0, sbw, sbh, &orig_mv, sb_bdof_flag);
            if (fc->ps.sps->r->sps_chroma_format_idc)
                pred_regular_chroma(lc, &mv, x0, y0, sbw, sbh, &orig_mv, pu->dmvr_flag);
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
    const VVCFrameContext *fc  = lc->fc;
    const CodingUnit *cu       = lc->cu;
    const PredictionUnit *pu   = &cu->pu;
    const MotionInfo *mi       = &pu->mi;
    const int x0   = cu->x0;
    const int y0   = cu->y0;
    const int sbw  = cu->cb_width / mi->num_sb_x;
    const int sbh  = cu->cb_height / mi->num_sb_y;
    const int hs = fc->ps.sps->hshift[1];
    const int vs = fc->ps.sps->vshift[1];

    for (int sby = 0; sby < mi->num_sb_y; sby++) {
        for (int sbx = 0; sbx < mi->num_sb_x; sbx++) {
            const int x = x0 + sbx * sbw;
            const int y = y0 + sby * sbh;

            uint8_t *dst0 = POS(0, x, y);
            const MvField *mv = ff_vvc_get_mvf(fc, x, y);
            VVCFrame *ref[2];

            if (pred_get_refs(lc, ref, mv) < 0)
                return;

            if (mi->pred_flag != PF_BI) {
                const int lx = mi->pred_flag - PF_L0;
                luma_prof_uni(lc, dst0, fc->frame->linesize[0], ref[lx]->frame,
                    mv, x, y, sbw, sbh, pu->cb_prof_flag[lx],
                    pu->diff_mv_x[lx], pu->diff_mv_y[lx]);
            } else {
                luma_prof_bi(lc, dst0, fc->frame->linesize[0], ref[0]->frame, ref[1]->frame,
                    mv, x, y, sbw, sbh);
            }
            if (fc->ps.sps->r->sps_chroma_format_idc) {
                if (!av_mod_uintp2(sby, vs) && !av_mod_uintp2(sbx, hs)) {
                    MvField mvc;
                    derive_affine_mvc(&mvc, fc, mv, x, y, sbw, sbh);
                    pred_regular_chroma(lc, &mvc, x, y, sbw<<hs, sbh<<vs, NULL, 0);

                }
            }

        }
    }
}

static void predict_inter(VVCLocalContext *lc)
{
    const VVCFrameContext *fc   = lc->fc;
    const CodingUnit *cu        = lc->cu;
    const PredictionUnit *pu    = &cu->pu;

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
    const VVCFrameContext *fc   = lc->fc;
    const CTU *ctu              = fc->tab.ctus + rs;
    CodingUnit *cu              = ctu->cus;

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
