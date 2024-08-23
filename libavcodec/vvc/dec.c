/*
 * VVC video decoder
 *
 * Copyright (C) 2021 Nuo Mi
 * Copyright (C) 2022 Xu Mu
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
#include "libavcodec/codec_internal.h"
#include "libavcodec/decode.h"
#include "libavcodec/profiles.h"
#include "libavcodec/refstruct.h"
#include "libavutil/cpu.h"
#include "libavutil/mem.h"
#include "libavutil/thread.h"

#include "dec.h"
#include "ctu.h"
#include "data.h"
#include "refs.h"
#include "thread.h"

#define TAB_MAX 32

typedef struct Tab {
    void **tab;
    size_t size;
} Tab;

typedef struct TabList {
    Tab tabs[TAB_MAX];
    int nb_tabs;

    int zero;
    int realloc;
} TabList;

#define TL_ADD(t, s) do {                                \
    av_assert0(l->nb_tabs < TAB_MAX);                    \
    l->tabs[l->nb_tabs].tab  = (void**)&fc->tab.t;       \
    l->tabs[l->nb_tabs].size = sizeof(*fc->tab.t) * (s); \
    l->nb_tabs++;                                        \
} while (0)

static void tl_init(TabList *l, const int zero, const int realloc)
{
    l->nb_tabs = 0;
    l->zero = zero;
    l->realloc = realloc;
}

static int tl_free(TabList *l)
{
    for (int i = 0; i < l->nb_tabs; i++)
        av_freep(l->tabs[i].tab);

    return 0;
}

static int tl_create(TabList *l)
{
    if (l->realloc) {
        tl_free(l);

        for (int i = 0; i < l->nb_tabs; i++) {
            Tab *t = l->tabs + i;
            *t->tab = l->zero ? av_mallocz(t->size) : av_malloc(t->size);
            if (!*t->tab)
                return AVERROR(ENOMEM);
        }
    }
    return 0;
}

static int tl_zero(TabList *l)
{
    if (l->zero) {
        for (int i = 0; i < l->nb_tabs; i++) {
            Tab *t = l->tabs + i;
            memset(*t->tab, 0, t->size);
        }
    }
    return 0;
}

static void ctu_nz_tl_init(TabList *l, VVCFrameContext *fc)
{
    const VVCSPS *sps   = fc->ps.sps;
    const VVCPPS *pps   = fc->ps.pps;
    const int ctu_size  = sps ? (1 << sps->ctb_log2_size_y << sps->ctb_log2_size_y) : 0;
    const int ctu_count = pps ? pps->ctb_count : 0;
    const int changed   = fc->tab.sz.ctu_count != ctu_count || fc->tab.sz.ctu_size != ctu_size;

    tl_init(l, 0, changed);

    TL_ADD(cus,     ctu_count);
    TL_ADD(ctus,    ctu_count);
    TL_ADD(deblock, ctu_count);
    TL_ADD(sao,     ctu_count);
    TL_ADD(alf,     ctu_count);
    TL_ADD(slice_idx, ctu_count);
    TL_ADD(coeffs,    ctu_count * ctu_size * VVC_MAX_SAMPLE_ARRAYS);
}

static void min_cb_tl_init(TabList *l, VVCFrameContext *fc)
{
    const VVCPPS *pps            = fc->ps.pps;
    const int pic_size_in_min_cb = pps ? pps->min_cb_width * pps->min_cb_height : 0;
    const int changed            = fc->tab.sz.pic_size_in_min_cb != pic_size_in_min_cb;

    tl_init(l, 1, changed);

    TL_ADD(imf,  pic_size_in_min_cb);
    TL_ADD(imm,  pic_size_in_min_cb);

    for (int i = LUMA; i <= CHROMA; i++)
        TL_ADD(cb_width[i],  pic_size_in_min_cb);   //is_a0_available requires this
}

static void min_cb_nz_tl_init(TabList *l, VVCFrameContext *fc)
{
    const VVCPPS *pps            = fc->ps.pps;
    const int pic_size_in_min_cb = pps ? pps->min_cb_width * pps->min_cb_height : 0;
    const int changed            = fc->tab.sz.pic_size_in_min_cb != pic_size_in_min_cb;

    tl_init(l, 0, changed);

    TL_ADD(skip, pic_size_in_min_cb);
    TL_ADD(imtf, pic_size_in_min_cb);
    TL_ADD(ipm,  pic_size_in_min_cb);

    for (int i = LUMA; i <= CHROMA; i++) {
        TL_ADD(cqt_depth[i], pic_size_in_min_cb);
        TL_ADD(cb_pos_x[i],  pic_size_in_min_cb);
        TL_ADD(cb_pos_y[i],  pic_size_in_min_cb);
        TL_ADD(cb_height[i], pic_size_in_min_cb);
        TL_ADD(cp_mv[i],     pic_size_in_min_cb * MAX_CONTROL_POINTS);
        TL_ADD(cpm[i],       pic_size_in_min_cb);
    }
    // For luma, qp can only change at the CU level, so the qp tab size is related to the CU.
    TL_ADD(qp[LUMA], pic_size_in_min_cb);
}

static void min_pu_tl_init(TabList *l, VVCFrameContext *fc)
{
    const VVCPPS *pps            = fc->ps.pps;
    const int pic_size_in_min_pu = pps ? pps->min_pu_width * pps->min_pu_height : 0;
    const int changed            = fc->tab.sz.pic_size_in_min_pu != pic_size_in_min_pu;

    tl_init(l, 1, changed);

    TL_ADD(iaf, pic_size_in_min_pu);
}

static void min_pu_nz_tl_init(TabList *l, VVCFrameContext *fc)
{
    const VVCPPS *pps            = fc->ps.pps;
    const int pic_size_in_min_pu = pps ? pps->min_pu_width * pps->min_pu_height : 0;
    const int changed            = fc->tab.sz.pic_size_in_min_pu != pic_size_in_min_pu;

    tl_init(l, 0, changed);

    TL_ADD(msf, pic_size_in_min_pu);
    TL_ADD(mmi, pic_size_in_min_pu);
    TL_ADD(mvf, pic_size_in_min_pu);
}

static void min_tu_tl_init(TabList *l, VVCFrameContext *fc)
{
    const VVCPPS *pps            = fc->ps.pps;
    const int pic_size_in_min_tu = pps ? pps->min_tu_width * pps->min_tu_height : 0;
    const int changed            = fc->tab.sz.pic_size_in_min_tu != pic_size_in_min_tu;

    tl_init(l, 1, changed);

    TL_ADD(tu_joint_cbcr_residual_flag, pic_size_in_min_tu);

    for (int i = LUMA; i <= CHROMA; i++)
        TL_ADD(pcmf[i], pic_size_in_min_tu);

    for (int i = 0; i < VVC_MAX_SAMPLE_ARRAYS; i++) {
        TL_ADD(tu_coded_flag[i], pic_size_in_min_tu);

        for (int vertical = 0; vertical < 2; vertical++)
            TL_ADD(bs[vertical][i], pic_size_in_min_tu);
    }
}

static void min_tu_nz_tl_init(TabList *l, VVCFrameContext *fc)
{
    const VVCPPS *pps            = fc->ps.pps;
    const int pic_size_in_min_tu = pps ? pps->min_tu_width * pps->min_tu_height : 0;
    const int changed            = fc->tab.sz.pic_size_in_min_tu != pic_size_in_min_tu;

    tl_init(l, 0, changed);

    for (int i = LUMA; i <= CHROMA; i++) {
        TL_ADD(tb_pos_x0[i], pic_size_in_min_tu);
        TL_ADD(tb_pos_y0[i], pic_size_in_min_tu);
        TL_ADD(tb_width[i],  pic_size_in_min_tu);
        TL_ADD(tb_height[i], pic_size_in_min_tu);
    }

    for (int vertical = 0; vertical < 2; vertical++) {
        TL_ADD(max_len_p[vertical], pic_size_in_min_tu);
        TL_ADD(max_len_q[vertical], pic_size_in_min_tu);
    }

    // For chroma, considering the joint CbCr, the QP tab size is related to the TU.
    for (int i = CB; i < VVC_MAX_SAMPLE_ARRAYS; i++)
        TL_ADD(qp[i], pic_size_in_min_tu);
}

static void pixel_buffer_nz_tl_init(TabList *l, VVCFrameContext *fc)
{
    const VVCSPS *sps    = fc->ps.sps;
    const VVCPPS *pps    = fc->ps.pps;
    const int width      = pps ? pps->width : 0;
    const int height     = pps ? pps->height : 0;
    const int ctu_width  = pps ? pps->ctb_width : 0;
    const int ctu_height = pps ? pps->ctb_height : 0;
    const int chroma_idc = sps ? sps->r->sps_chroma_format_idc : 0;
    const int ps         = sps ? sps->pixel_shift : 0;
    const int c_end      = chroma_idc ? VVC_MAX_SAMPLE_ARRAYS : 1;
    const int changed    = fc->tab.sz.chroma_format_idc != chroma_idc ||
        fc->tab.sz.width != width || fc->tab.sz.height != height ||
        fc->tab.sz.ctu_width != ctu_width || fc->tab.sz.ctu_height != ctu_height ||
        fc->tab.sz.pixel_shift != ps;

    tl_init(l, 0, changed);

    for (int c_idx = 0; c_idx < c_end; c_idx++) {
        const int w = width  >> (sps ? sps->hshift[c_idx] : 0);
        const int h = height >> (sps ? sps->vshift[c_idx] : 0);
        TL_ADD(sao_pixel_buffer_h[c_idx], (w * 2 * ctu_height) << ps);
        TL_ADD(sao_pixel_buffer_v[c_idx], (h * 2 * ctu_width)  << ps);
    }

    for (int c_idx = 0; c_idx < c_end; c_idx++) {
        const int w = width  >> (sps ? sps->hshift[c_idx] : 0);
        const int h = height >> (sps ? sps->vshift[c_idx] : 0);
        const int border_pixels = c_idx ? ALF_BORDER_CHROMA : ALF_BORDER_LUMA;
        for (int i = 0; i < 2; i++) {
            TL_ADD(alf_pixel_buffer_h[c_idx][i], (w * border_pixels * ctu_height) << ps);
            TL_ADD(alf_pixel_buffer_v[c_idx][i], h * ALF_PADDING_SIZE * ctu_width);
        }
    }
}

static void msm_tl_init(TabList *l, VVCFrameContext *fc)
{
    const VVCPPS *pps = fc->ps.pps;
    const int w32     = pps ? AV_CEIL_RSHIFT(pps->width,  5) : 0;
    const int h32     = pps ? AV_CEIL_RSHIFT(pps->height, 5) : 0;
    const int changed = AV_CEIL_RSHIFT(fc->tab.sz.width,  5) != w32 ||
        AV_CEIL_RSHIFT(fc->tab.sz.height,  5) != h32;

    tl_init(l, 1, changed);

    for (int i = LUMA; i <= CHROMA; i++)
        TL_ADD(msm[i], w32 * h32);
}

static void ispmf_tl_init(TabList *l, VVCFrameContext *fc)
{
    const VVCPPS *pps = fc->ps.pps;
    const int w64     = pps ? AV_CEIL_RSHIFT(pps->width,  6) : 0;
    const int h64     = pps ? AV_CEIL_RSHIFT(pps->height, 6) : 0;
    const int changed = AV_CEIL_RSHIFT(fc->tab.sz.width,  6) != w64 ||
        AV_CEIL_RSHIFT(fc->tab.sz.height,  6) != h64;

    tl_init(l, 1, changed);

    TL_ADD(ispmf, w64 * h64);
}

static void ibc_tl_init(TabList *l, VVCFrameContext *fc)
{
    const VVCSPS *sps    = fc->ps.sps;
    const VVCPPS *pps    = fc->ps.pps;
    const int ctu_height = pps ? pps->ctb_height : 0;
    const int ctu_size   = sps ? sps->ctb_size_y : 0;
    const int ps         = sps ? sps->pixel_shift : 0;
    const int chroma_idc = sps ? sps->r->sps_chroma_format_idc : 0;
    const int has_ibc    = sps ? sps->r->sps_ibc_enabled_flag : 0;
    const int changed    = fc->tab.sz.chroma_format_idc != chroma_idc ||
        fc->tab.sz.ctu_height != ctu_height ||
        fc->tab.sz.ctu_size != ctu_size ||
        fc->tab.sz.pixel_shift != ps;

    fc->tab.sz.ibc_buffer_width = ctu_size ? 2 * MAX_CTU_SIZE * MAX_CTU_SIZE / ctu_size : 0;

    tl_init(l, has_ibc, changed);

    for (int i = LUMA; i < VVC_MAX_SAMPLE_ARRAYS; i++) {
        const int hs = sps ? sps->hshift[i] : 0;
        const int vs = sps ? sps->vshift[i] : 0;
        TL_ADD(ibc_vir_buf[i], fc->tab.sz.ibc_buffer_width * ctu_size * ctu_height << ps >> hs >> vs);
    }
}

typedef void (*tl_init_fn)(TabList *l, VVCFrameContext *fc);

static int frame_context_for_each_tl(VVCFrameContext *fc, int (*unary_fn)(TabList *l))
{
    const tl_init_fn init[] = {
        ctu_nz_tl_init,
        min_cb_tl_init,
        min_cb_nz_tl_init,
        min_pu_tl_init,
        min_pu_nz_tl_init,
        min_tu_tl_init,
        min_tu_nz_tl_init,
        pixel_buffer_nz_tl_init,
        msm_tl_init,
        ispmf_tl_init,
        ibc_tl_init,
    };

    for (int i = 0; i < FF_ARRAY_ELEMS(init); i++) {
        TabList l;
        int ret;

        init[i](&l, fc);
        ret = unary_fn(&l);
        if (ret < 0)
            return ret;
    }
    return 0;
}

static void free_cus(VVCFrameContext *fc)
{
    if (fc->tab.cus) {
        for (int i = 0; i < fc->tab.sz.ctu_count; i++)
            ff_vvc_ctu_free_cus(fc->tab.cus + i);
    }
}

static void pic_arrays_free(VVCFrameContext *fc)
{
    free_cus(fc);
    frame_context_for_each_tl(fc, tl_free);
    ff_refstruct_pool_uninit(&fc->rpl_tab_pool);
    ff_refstruct_pool_uninit(&fc->tab_dmvr_mvf_pool);

    memset(&fc->tab.sz, 0, sizeof(fc->tab.sz));
}

static int pic_arrays_init(VVCContext *s, VVCFrameContext *fc)
{
    const VVCSPS *sps            = fc->ps.sps;
    const VVCPPS *pps            = fc->ps.pps;
    const int ctu_count          = pps->ctb_count;
    const int pic_size_in_min_pu = pps->min_pu_width * pps->min_pu_height;
    int ret;

    free_cus(fc);

    ret = frame_context_for_each_tl(fc, tl_create);
    if (ret < 0)
        return ret;

    // for error handling case, we may call free_cus before VVC_TASK_STAGE_INIT, so we need to set cus to 0 here
    memset(fc->tab.cus, 0, sizeof(*fc->tab.cus) * ctu_count);

    memset(fc->tab.slice_idx, -1, sizeof(*fc->tab.slice_idx) * ctu_count);

    if (fc->tab.sz.ctu_count != ctu_count) {
        ff_refstruct_pool_uninit(&fc->rpl_tab_pool);
        fc->rpl_tab_pool = ff_refstruct_pool_alloc(ctu_count * sizeof(RefPicListTab), 0);
        if (!fc->rpl_tab_pool)
            return AVERROR(ENOMEM);
    }

    if (fc->tab.sz.pic_size_in_min_pu != pic_size_in_min_pu) {
        ff_refstruct_pool_uninit(&fc->tab_dmvr_mvf_pool);
        fc->tab_dmvr_mvf_pool = ff_refstruct_pool_alloc(
            pic_size_in_min_pu * sizeof(MvField), FF_REFSTRUCT_POOL_FLAG_ZERO_EVERY_TIME);
        if (!fc->tab_dmvr_mvf_pool)
            return AVERROR(ENOMEM);
    }

    fc->tab.sz.ctu_count          = pps->ctb_count;
    fc->tab.sz.ctu_size           = 1 << sps->ctb_log2_size_y << sps->ctb_log2_size_y;
    fc->tab.sz.pic_size_in_min_cb = pps->min_cb_width * pps->min_cb_height;
    fc->tab.sz.pic_size_in_min_pu = pic_size_in_min_pu;
    fc->tab.sz.pic_size_in_min_tu = pps->min_tu_width * pps->min_tu_height;
    fc->tab.sz.width              = pps->width;
    fc->tab.sz.height             = pps->height;
    fc->tab.sz.ctu_width          = pps->ctb_width;
    fc->tab.sz.ctu_height         = pps->ctb_height;
    fc->tab.sz.chroma_format_idc  = sps->r->sps_chroma_format_idc;
    fc->tab.sz.pixel_shift        = sps->pixel_shift;

    return 0;
}

int ff_vvc_per_frame_init(VVCFrameContext *fc)
{
    return frame_context_for_each_tl(fc, tl_zero);
}

static int min_positive(const int idx, const int diff, const int min_diff)
{
    return diff > 0 && (idx < 0 || diff < min_diff);
}

static int max_negtive(const int idx, const int diff, const int max_diff)
{
    return diff < 0 && (idx < 0 || diff > max_diff);
}

typedef int (*smvd_find_fxn)(const int idx, const int diff, const int old_diff);

static int8_t smvd_find(const VVCFrameContext *fc, const SliceContext *sc, int lx, smvd_find_fxn find)
{
    const H266RawSliceHeader *rsh = sc->sh.r;
    const RefPicList *rpl         = sc->rpl + lx;
    const int poc                 = fc->ref->poc;
    int8_t idx                    = -1;
    int old_diff                  = -1;
    for (int i = 0; i < rsh->num_ref_idx_active[lx]; i++) {
        if (!rpl->refs[i].is_lt) {
            int diff = poc - rpl->refs[i].poc;
            if (find(idx, diff, old_diff)) {
                idx = i;
                old_diff = diff;
            }
        }
    }
    return idx;
}

static void smvd_ref_idx(const VVCFrameContext *fc, SliceContext *sc)
{
    VVCSH *sh = &sc->sh;
    if (IS_B(sh->r)) {
        sh->ref_idx_sym[0] = smvd_find(fc, sc, 0, min_positive);
        sh->ref_idx_sym[1] = smvd_find(fc, sc, 1, max_negtive);
        if (sh->ref_idx_sym[0] == -1 || sh->ref_idx_sym[1] == -1) {
            sh->ref_idx_sym[0] = smvd_find(fc, sc, 0, max_negtive);
            sh->ref_idx_sym[1] = smvd_find(fc, sc, 1, min_positive);
        }
    }
}

static void eps_free(SliceContext *slice)
{
    av_freep(&slice->eps);
    slice->nb_eps = 0;
}

static void slices_free(VVCFrameContext *fc)
{
    if (fc->slices) {
        for (int i = 0; i < fc->nb_slices_allocated; i++) {
            SliceContext *slice = fc->slices[i];
            if (slice) {
                ff_refstruct_unref(&slice->ref);
                ff_refstruct_unref(&slice->sh.r);
                eps_free(slice);
                av_free(slice);
            }
        }
        av_freep(&fc->slices);
    }
    fc->nb_slices_allocated = 0;
    fc->nb_slices = 0;
}

static int slices_realloc(VVCFrameContext *fc)
{
    void *p;
    const int size = (fc->nb_slices_allocated + 1) * 3 / 2;

    if (fc->nb_slices < fc->nb_slices_allocated)
        return 0;

    p = av_realloc_array(fc->slices, size, sizeof(*fc->slices));
    if (!p)
        return AVERROR(ENOMEM);

    fc->slices = p;
    for (int i = fc->nb_slices_allocated; i < size; i++) {
        fc->slices[i] = av_mallocz(sizeof(*fc->slices[0]));
        if (!fc->slices[i]) {
            fc->nb_slices_allocated = i;
            return AVERROR(ENOMEM);
        }
        fc->slices[i]->slice_idx = i;
    }
    fc->nb_slices_allocated = size;

    return 0;
}

static int ep_init_cabac_decoder(SliceContext *sc, const int index,
    const H2645NAL *nal, GetBitContext *gb, const CodedBitstreamUnit *unit)
{
    const H266RawSlice *slice     = unit->content_ref;
    const H266RawSliceHeader *rsh = sc->sh.r;
    EntryPoint *ep                = sc->eps + index;
    int size;
    int ret;

    if (index < rsh->num_entry_points) {
        int skipped = 0;
        int64_t start =  (gb->index >> 3);
        int64_t end = start + rsh->sh_entry_point_offset_minus1[index] + 1;
        while (skipped < nal->skipped_bytes && nal->skipped_bytes_pos[skipped] <= start + slice->header_size) {
            skipped++;
        }
        while (skipped < nal->skipped_bytes && nal->skipped_bytes_pos[skipped] <= end + slice->header_size) {
            end--;
            skipped++;
        }
        size = end - start;
        size = av_clip(size, 0, get_bits_left(gb) / 8);
    } else {
        size = get_bits_left(gb) / 8;
    }
    av_assert0(gb->buffer + get_bits_count(gb) / 8 + size <= gb->buffer_end);
    ret = ff_init_cabac_decoder (&ep->cc, gb->buffer + get_bits_count(gb) / 8, size);
    if (ret < 0)
        return ret;
    skip_bits(gb, size * 8);
    return 0;
}

static int slice_init_entry_points(SliceContext *sc,
    VVCFrameContext *fc, const H2645NAL *nal, const CodedBitstreamUnit *unit)
{
    const VVCSH *sh           = &sc->sh;
    const H266RawSlice *slice = unit->content_ref;
    int nb_eps                = sh->r->num_entry_points + 1;
    int ctu_addr              = 0;
    GetBitContext gb;
    int ret;

    if (sc->nb_eps != nb_eps) {
        eps_free(sc);
        sc->eps = av_calloc(nb_eps, sizeof(*sc->eps));
        if (!sc->eps)
            return AVERROR(ENOMEM);
        sc->nb_eps = nb_eps;
    }

    ret = init_get_bits8(&gb, slice->data, slice->data_size);
    if (ret < 0)
        return ret;
    for (int i = 0; i < sc->nb_eps; i++)
    {
        EntryPoint *ep = sc->eps + i;

        ep->ctu_start = ctu_addr;
        ep->ctu_end   = (i + 1 == sc->nb_eps ? sh->num_ctus_in_curr_slice : sh->entry_point_start_ctu[i]);

        for (int j = ep->ctu_start; j < ep->ctu_end; j++) {
            const int rs = sc->sh.ctb_addr_in_curr_slice[j];
            fc->tab.slice_idx[rs] = sc->slice_idx;
        }

        ret = ep_init_cabac_decoder(sc, i, nal, &gb, unit);
        if (ret < 0)
            return ret;

        if (i + 1 < sc->nb_eps)
            ctu_addr = sh->entry_point_start_ctu[i];
    }

    return 0;
}

static VVCFrameContext* get_frame_context(const VVCContext *s, const VVCFrameContext *fc, const int delta)
{
    const int size = s->nb_fcs;
    const int idx  = (fc - s->fcs + delta  + size) % size;
    return s->fcs + idx;
}

static int ref_frame(VVCFrame *dst, const VVCFrame *src)
{
    int ret;

    ret = av_frame_ref(dst->frame, src->frame);
    if (ret < 0)
        return ret;

    ff_refstruct_replace(&dst->sps, src->sps);
    ff_refstruct_replace(&dst->pps, src->pps);

    ff_refstruct_replace(&dst->progress, src->progress);

    ff_refstruct_replace(&dst->tab_dmvr_mvf, src->tab_dmvr_mvf);

    ff_refstruct_replace(&dst->rpl_tab, src->rpl_tab);
    ff_refstruct_replace(&dst->rpl, src->rpl);
    dst->nb_rpl_elems = src->nb_rpl_elems;

    dst->poc = src->poc;
    dst->ctb_count = src->ctb_count;

    dst->scaling_win = src->scaling_win;
    dst->ref_width   = src->ref_width;
    dst->ref_height  = src->ref_height;

    dst->flags = src->flags;
    dst->sequence = src->sequence;

    return 0;
}

static av_cold void frame_context_free(VVCFrameContext *fc)
{
    slices_free(fc);

    ff_refstruct_pool_uninit(&fc->tu_pool);
    ff_refstruct_pool_uninit(&fc->cu_pool);

    for (int i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++) {
        ff_vvc_unref_frame(fc, &fc->DPB[i], ~0);
        av_frame_free(&fc->DPB[i].frame);
    }

    ff_vvc_frame_thread_free(fc);
    pic_arrays_free(fc);
    av_frame_free(&fc->output_frame);
    ff_vvc_frame_ps_free(&fc->ps);
}

static av_cold int frame_context_init(VVCFrameContext *fc, AVCodecContext *avctx)
{

    fc->log_ctx = avctx;

    fc->output_frame = av_frame_alloc();
    if (!fc->output_frame)
        return AVERROR(ENOMEM);

    for (int j = 0; j < FF_ARRAY_ELEMS(fc->DPB); j++) {
        fc->DPB[j].frame = av_frame_alloc();
        if (!fc->DPB[j].frame)
            return AVERROR(ENOMEM);
    }
    fc->cu_pool = ff_refstruct_pool_alloc(sizeof(CodingUnit), 0);
    if (!fc->cu_pool)
        return AVERROR(ENOMEM);

    fc->tu_pool = ff_refstruct_pool_alloc(sizeof(TransformUnit), 0);
    if (!fc->tu_pool)
        return AVERROR(ENOMEM);

    return 0;
}

static int frame_context_setup(VVCFrameContext *fc, VVCContext *s)
{
    int ret;

    fc->ref = NULL;

    // copy refs from the last frame
    if (s->nb_frames && s->nb_fcs > 1) {
        VVCFrameContext *prev = get_frame_context(s, fc, -1);
        for (int i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++) {
            ff_vvc_unref_frame(fc, &fc->DPB[i], ~0);
            if (prev->DPB[i].frame->buf[0]) {
                ret = ref_frame(&fc->DPB[i], &prev->DPB[i]);
                if (ret < 0)
                    return ret;
            }
        }
    }

    if (IS_IDR(s)) {
        s->seq_decode = (s->seq_decode + 1) & 0xff;
        ff_vvc_clear_refs(fc);
    }

    ret = pic_arrays_init(s, fc);
    if (ret < 0)
        return ret;
    ff_vvc_dsp_init(&fc->vvcdsp, fc->ps.sps->bit_depth);
    ff_videodsp_init(&fc->vdsp, fc->ps.sps->bit_depth);
    return 0;
}

static int frame_start(VVCContext *s, VVCFrameContext *fc, SliceContext *sc)
{
    const VVCPH *ph                 = &fc->ps.ph;
    const H266RawSliceHeader *rsh   = sc->sh.r;
    int ret;

    // 8.3.1 Decoding process for picture order count
    if (!s->temporal_id && !ph->r->ph_non_ref_pic_flag && !(IS_RASL(s) || IS_RADL(s)))
        s->poc_tid0 = ph->poc;

    if ((ret = ff_vvc_set_new_ref(s, fc, &fc->frame)) < 0)
        goto fail;

    if (!IS_IDR(s))
        ff_vvc_bump_frame(s, fc);

    av_frame_unref(fc->output_frame);

    if ((ret = ff_vvc_output_frame(s, fc, fc->output_frame,rsh->sh_no_output_of_prior_pics_flag, 0)) < 0)
        goto fail;

    if ((ret = ff_vvc_frame_rpl(s, fc, sc)) < 0)
        goto fail;

    if ((ret = ff_vvc_frame_thread_init(fc)) < 0)
        goto fail;
    return 0;
fail:
    if (fc->ref)
        ff_vvc_unref_frame(fc, fc->ref, ~0);
    fc->ref = NULL;
    return ret;
}

static int slice_start(SliceContext *sc, VVCContext *s, VVCFrameContext *fc,
    const CodedBitstreamUnit *unit, const int is_first_slice)
{
    VVCSH *sh = &sc->sh;
    int ret;

    ret = ff_vvc_decode_sh(sh, &fc->ps, unit);
    if (ret < 0)
        return ret;

    ff_refstruct_replace(&sc->ref, unit->content_ref);

    if (is_first_slice) {
        ret = frame_start(s, fc, sc);
        if (ret < 0)
            return ret;
    } else if (fc->ref) {
        if (!IS_I(sh->r)) {
            ret = ff_vvc_slice_rpl(s, fc, sc);
            if (ret < 0) {
                av_log(fc->log_ctx, AV_LOG_WARNING,
                       "Error constructing the reference lists for the current slice.\n");
                return ret;
            }
        }
    } else {
        av_log(fc->log_ctx, AV_LOG_ERROR, "First slice in a frame missing.\n");
        return ret;
    }

    if (!IS_I(sh->r))
        smvd_ref_idx(fc, sc);

    return 0;
}

static void export_frame_params(VVCContext *s, const VVCFrameContext *fc)
{
    AVCodecContext *c = s->avctx;
    const VVCSPS *sps = fc->ps.sps;
    const VVCPPS *pps = fc->ps.pps;

    c->pix_fmt      = sps->pix_fmt;
    c->coded_width  = pps->width;
    c->coded_height = pps->height;
    c->width        = pps->width  - ((pps->r->pps_conf_win_left_offset + pps->r->pps_conf_win_right_offset) << sps->hshift[CHROMA]);
    c->height       = pps->height - ((pps->r->pps_conf_win_top_offset + pps->r->pps_conf_win_bottom_offset) << sps->vshift[CHROMA]);
    c->has_b_frames = sps->r->sps_dpb_params.dpb_max_num_reorder_pics[sps->r->sps_max_sublayers_minus1];
}

static int frame_setup(VVCFrameContext *fc, VVCContext *s)
{
    int ret = ff_vvc_decode_frame_ps(&fc->ps, s);
    if (ret < 0)
        return ret;

    ret = frame_context_setup(fc, s);
    if (ret < 0)
        return ret;

    export_frame_params(s, fc);
    return ret;
}

static int decode_slice(VVCContext *s, VVCFrameContext *fc, const H2645NAL *nal, const CodedBitstreamUnit *unit)
{
    int ret;
    SliceContext *sc;
    const int is_first_slice = !fc->nb_slices;

    ret = slices_realloc(fc);
    if (ret < 0)
        return ret;

    sc = fc->slices[fc->nb_slices];

    s->vcl_unit_type = nal->type;
    if (is_first_slice) {
        ret = frame_setup(fc, s);
        if (ret < 0)
            return ret;
    }

    ret = slice_start(sc, s, fc, unit, is_first_slice);
    if (ret < 0)
        return ret;

    ret = slice_init_entry_points(sc, fc, nal, unit);
    if (ret < 0)
        return ret;
    fc->nb_slices++;

    return 0;
}

static int decode_nal_unit(VVCContext *s, VVCFrameContext *fc, const H2645NAL *nal, const CodedBitstreamUnit *unit)
{
    int  ret;

    s->temporal_id = nal->temporal_id;

    if (nal->nuh_layer_id > 0) {
        avpriv_report_missing_feature(fc->log_ctx,
                "Decoding of multilayer bitstreams");
        return AVERROR_PATCHWELCOME;
    }

    switch (unit->type) {
    case VVC_VPS_NUT:
    case VVC_SPS_NUT:
    case VVC_PPS_NUT:
        /* vps, sps, sps cached by s->cbc */
        break;
    case VVC_TRAIL_NUT:
    case VVC_STSA_NUT:
    case VVC_RADL_NUT:
    case VVC_RASL_NUT:
    case VVC_IDR_W_RADL:
    case VVC_IDR_N_LP:
    case VVC_CRA_NUT:
    case VVC_GDR_NUT:
        ret = decode_slice(s, fc, nal, unit);
        if (ret < 0)
            return ret;
        break;
    case VVC_PREFIX_APS_NUT:
    case VVC_SUFFIX_APS_NUT:
        ret = ff_vvc_decode_aps(&s->ps, unit);
        if (ret < 0)
            return ret;
        break;
    }

    return 0;
}

static int decode_nal_units(VVCContext *s, VVCFrameContext *fc, AVPacket *avpkt)
{
    const CodedBitstreamH266Context *h266 = s->cbc->priv_data;
    CodedBitstreamFragment *frame         = &s->current_frame;
    int ret = 0;
    s->last_eos = s->eos;
    s->eos = 0;

    ff_cbs_fragment_reset(frame);
    ret = ff_cbs_read_packet(s->cbc, frame, avpkt);
    if (ret < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Failed to read packet.\n");
        return ret;
    }
    /* decode the NAL units */
    for (int i = 0; i < frame->nb_units; i++) {
        const H2645NAL *nal            = h266->common.read_packet.nals + i;
        const CodedBitstreamUnit *unit = frame->units + i;

        if (unit->type == VVC_EOB_NUT || unit->type == VVC_EOS_NUT) {
            s->last_eos = 1;
        } else {
            ret = decode_nal_unit(s, fc, nal, unit);
            if (ret < 0) {
                av_log(s->avctx, AV_LOG_WARNING,
                        "Error parsing NAL unit #%d.\n", i);
                goto fail;
            }
        }
    }
    return 0;

fail:
    if (fc->ref)
        ff_vvc_report_frame_finished(fc->ref);
    return ret;
}

static int set_output_format(const VVCContext *s, const AVFrame *output)
{
    AVCodecContext *c = s->avctx;
    int ret;

    if (output->width != c->width || output->height != c->height) {
        if ((ret = ff_set_dimensions(c, output->width, output->height)) < 0)
            return ret;
    }
    c->pix_fmt = output->format;
    return 0;
}

static int wait_delayed_frame(VVCContext *s, AVFrame *output, int *got_output)
{
    VVCFrameContext *delayed = get_frame_context(s, s->fcs, s->nb_frames - s->nb_delayed);
    int ret                  = ff_vvc_frame_wait(s, delayed);

    if (!ret && delayed->output_frame->buf[0] && output) {
        av_frame_move_ref(output, delayed->output_frame);
        ret = set_output_format(s, output);
        if (!ret)
            *got_output = 1;
    }
    s->nb_delayed--;

    return ret;
}

static int submit_frame(VVCContext *s, VVCFrameContext *fc, AVFrame *output, int *got_output)
{
    int ret = ff_vvc_frame_submit(s, fc);

    if (ret < 0) {
        ff_vvc_report_frame_finished(fc->ref);
        return ret;
    }

    s->nb_frames++;
    s->nb_delayed++;

    if (s->nb_delayed >= s->nb_fcs) {
        if ((ret = wait_delayed_frame(s, output, got_output)) < 0)
            return ret;
    }
    return 0;
}

static int get_decoded_frame(VVCContext *s, AVFrame *output, int *got_output)
{
    int ret;
    while (s->nb_delayed) {
        if ((ret = wait_delayed_frame(s, output, got_output)) < 0)
            return ret;
        if (*got_output)
            return 0;
    }
    if (s->nb_frames) {
        //we still have frames cached in dpb.
        VVCFrameContext *last = get_frame_context(s, s->fcs, s->nb_frames - 1);

        ret = ff_vvc_output_frame(s, last, output, 0, 1);
        if (ret < 0)
            return ret;
        if (ret) {
            *got_output = ret;
            if ((ret = set_output_format(s, output)) < 0)
                return ret;
        }
    }
    return 0;
}

static int vvc_decode_frame(AVCodecContext *avctx, AVFrame *output,
    int *got_output, AVPacket *avpkt)
{
    VVCContext *s = avctx->priv_data;
    VVCFrameContext *fc;
    int ret;

    if (!avpkt->size)
        return get_decoded_frame(s, output, got_output);

    fc = get_frame_context(s, s->fcs, s->nb_frames);

    fc->nb_slices = 0;
    fc->decode_order = s->nb_frames;

    ret = decode_nal_units(s, fc, avpkt);
    if (ret < 0)
        return ret;

    if (!fc->ft)
        return avpkt->size;

    ret = submit_frame(s, fc, output, got_output);
    if (ret < 0)
        return ret;

    return avpkt->size;
}

static av_cold void vvc_decode_flush(AVCodecContext *avctx)
{
    VVCContext *s  = avctx->priv_data;
    int got_output = 0;

    while (s->nb_delayed)
        wait_delayed_frame(s, NULL, &got_output);

    if (s->fcs) {
        VVCFrameContext *last = get_frame_context(s, s->fcs, s->nb_frames - 1);
        ff_vvc_flush_dpb(last);
    }

    s->ps.sps_id_used = 0;

    s->eos = 1;
}

static av_cold int vvc_decode_free(AVCodecContext *avctx)
{
    VVCContext *s = avctx->priv_data;

    ff_cbs_fragment_free(&s->current_frame);
    vvc_decode_flush(avctx);
    ff_vvc_executor_free(&s->executor);
    if (s->fcs) {
        for (int i = 0; i < s->nb_fcs; i++)
            frame_context_free(s->fcs + i);
        av_free(s->fcs);
    }
    ff_vvc_ps_uninit(&s->ps);
    ff_cbs_close(&s->cbc);

    return 0;
}

static av_cold void init_default_scale_m(void)
{
    memset(&ff_vvc_default_scale_m, 16, sizeof(ff_vvc_default_scale_m));
}

#define VVC_MAX_DELAYED_FRAMES 16
static av_cold int vvc_decode_init(AVCodecContext *avctx)
{
    VVCContext *s                  = avctx->priv_data;
    static AVOnce init_static_once = AV_ONCE_INIT;
    const int cpu_count            = av_cpu_count();
    const int delayed              = FFMIN(cpu_count, VVC_MAX_DELAYED_FRAMES);
    int thread_count               = avctx->thread_count ? avctx->thread_count : delayed;
    int ret;

    s->avctx = avctx;

    ret = ff_cbs_init(&s->cbc, AV_CODEC_ID_VVC, avctx);
    if (ret)
        return ret;

    if (avctx->extradata_size > 0 && avctx->extradata) {
        ret = ff_cbs_read_extradata_from_codec(s->cbc, &s->current_frame, avctx);
        if (ret < 0)
            return ret;
    }

    s->nb_fcs = (avctx->flags & AV_CODEC_FLAG_LOW_DELAY) ? 1 : delayed;
    s->fcs = av_calloc(s->nb_fcs, sizeof(*s->fcs));
    if (!s->fcs)
        return AVERROR(ENOMEM);

    for (int i = 0; i < s->nb_fcs; i++) {
        VVCFrameContext *fc = s->fcs + i;
        ret = frame_context_init(fc, avctx);
        if (ret < 0)
            return ret;
    }

    if (thread_count == 1)
        thread_count = 0;
    s->executor = ff_vvc_executor_alloc(s, thread_count);
    if (!s->executor)
        return AVERROR(ENOMEM);

    s->eos = 1;
    GDR_SET_RECOVERED(s);
    ff_thread_once(&init_static_once, init_default_scale_m);

    return 0;
}

const FFCodec ff_vvc_decoder = {
    .p.name         = "vvc",
    .p.long_name    = NULL_IF_CONFIG_SMALL("VVC (Versatile Video Coding)"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_VVC,
    .priv_data_size = sizeof(VVCContext),
    .init           = vvc_decode_init,
    .close          = vvc_decode_free,
    FF_CODEC_DECODE_CB(vvc_decode_frame),
    .flush          = vvc_decode_flush,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY | AV_CODEC_CAP_OTHER_THREADS,
    .caps_internal  = FF_CODEC_CAP_EXPORTS_CROPPING | FF_CODEC_CAP_INIT_CLEANUP |
                      FF_CODEC_CAP_AUTO_THREADS,
    .p.profiles     = NULL_IF_CONFIG_SMALL(ff_vvc_profiles),
};
