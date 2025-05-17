/*
 * VVC filters
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

#include "ctu.h"
#include "data.h"
#include "filter.h"
#include "refs.h"

#define LEFT        0
#define TOP         1
#define RIGHT       2
#define BOTTOM      3
#define MAX_EDGES   4

#define DEFAULT_INTRA_TC_OFFSET 2

#define POS(c_idx, x, y)                                                                        \
    &fc->frame->data[c_idx][((y) >> fc->ps.sps->vshift[c_idx]) * fc->frame->linesize[c_idx] +   \
        (((x) >> fc->ps.sps->hshift[c_idx]) << fc->ps.sps->pixel_shift)]

//Table 43 Derivation of threshold variables beta' and tc' from input Q
static const uint16_t tctable[66] = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   3,   4,   4,   4,   4,   5,   5,   5,   5,   7,   7,   8,   9,  10,
     10,  11,  13,  14,  15,  17,  19,  21,  24,  25,  29,  33,  36,  41,  45,  51,
     57,  64,  71,  80,  89, 100, 112, 125, 141, 157, 177, 198, 222, 250, 280, 314,
    352, 395,
};

//Table 43 Derivation of threshold variables beta' and tc' from input Q
static const uint8_t betatable[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      6,   7,   8,   9,  10,  11,  12,  13,  14,  15,  16,  17,  18,  20,  22,  24,
     26,  28,  30,  32,  34,  36,  38,  40,  42,  44,  46,  48,  50,  52,  54,  56,
     58,  60,  62,  64,  66,  68,  70,  72,  74,  76,  78,  80,  82,  84,  86,  88,
};

// One vertical and one horizontal virtual boundary in a CTU at most. The CTU will be divided into 4 subblocks.
#define MAX_VBBS 4

static int get_virtual_boundary(const VVCFrameContext *fc, const int ctu_pos, const int vertical)
{
    const VVCSPS *sps    = fc->ps.sps;
    const VVCPH *ph      = &fc->ps.ph;
    const uint16_t *vbs  = vertical ? ph->vb_pos_x    : ph->vb_pos_y;
    const uint8_t nb_vbs = vertical ? ph->num_ver_vbs : ph->num_hor_vbs;
    const int pos        = ctu_pos << sps->ctb_log2_size_y;

    if (sps->r->sps_virtual_boundaries_enabled_flag) {
        for (int i = 0; i < nb_vbs; i++) {
            const int o = vbs[i] - pos;
            if (o >= 0 && o < sps->ctb_size_y)
                return vbs[i];
        }
    }
    return 0;
}

static int is_virtual_boundary(const VVCFrameContext *fc, const int pos, const int vertical)
{
    return get_virtual_boundary(fc, pos >> fc->ps.sps->ctb_log2_size_y, vertical) == pos;
}

static int get_qPc(const VVCFrameContext *fc, const int x0, const int y0, const int chroma)
{
    const int x            = x0 >> MIN_TU_LOG2;
    const int y            = y0 >> MIN_TU_LOG2;
    const int min_tu_width = fc->ps.pps->min_tu_width;
    return fc->tab.qp[chroma][x + y * min_tu_width];
}

static void copy_ctb(uint8_t *dst, const uint8_t *src, const int width, const int height,
    const ptrdiff_t dst_stride, const ptrdiff_t src_stride)
{
    for (int y = 0; y < height; y++) {
        memcpy(dst, src, width);

        dst += dst_stride;
        src += src_stride;
    }
}

static void copy_pixel(uint8_t *dst, const uint8_t *src, const int pixel_shift)
{
    if (pixel_shift)
        *(uint16_t *)dst = *(uint16_t *)src;
    else
        *dst = *src;
}

static void copy_vert(uint8_t *dst, const uint8_t *src, const int pixel_shift, const int height,
    const ptrdiff_t dst_stride, const ptrdiff_t src_stride)
{
    int i;
    if (pixel_shift == 0) {
        for (i = 0; i < height; i++) {
            *dst = *src;
            dst += dst_stride;
            src += src_stride;
        }
    } else {
        for (i = 0; i < height; i++) {
            *(uint16_t *)dst = *(uint16_t *)src;
            dst += dst_stride;
            src += src_stride;
        }
    }
}

static void copy_ctb_to_hv(VVCFrameContext *fc, const uint8_t *src,
    const ptrdiff_t src_stride, const int x, const int y, const int width, const int height,
    const int c_idx, const int rx, const int ry, const int top)
{
    const int ps = fc->ps.sps->pixel_shift;
    const int w  = fc->ps.pps->width >> fc->ps.sps->hshift[c_idx];
    const int h  = fc->ps.pps->height >> fc->ps.sps->vshift[c_idx];

    if (top) {
        /* top */
        memcpy(fc->tab.sao_pixel_buffer_h[c_idx] + (((2 * ry) * w + x) << ps),
            src, width << ps);
    } else {
        /* bottom */
        memcpy(fc->tab.sao_pixel_buffer_h[c_idx] + (((2 * ry + 1) * w + x) << ps),
            src + src_stride * (height - 1), width << ps);

        /* copy vertical edges */
        copy_vert(fc->tab.sao_pixel_buffer_v[c_idx] + (((2 * rx) * h + y) << ps), src, ps, height, 1 << ps, src_stride);
        copy_vert(fc->tab.sao_pixel_buffer_v[c_idx] + (((2 * rx + 1) * h + y) << ps), src + ((width - 1) << ps), ps, height, 1 << ps, src_stride);
    }
}

static void sao_copy_ctb_to_hv(VVCLocalContext *lc, const int rx, const int ry, const int top)
{
    VVCFrameContext *fc  = lc->fc;
    const int ctb_size_y = fc->ps.sps->ctb_size_y;
    const int x0         = rx << fc->ps.sps->ctb_log2_size_y;
    const int y0         = ry << fc->ps.sps->ctb_log2_size_y;

    for (int c_idx = 0; c_idx < (fc->ps.sps->r->sps_chroma_format_idc ? 3 : 1); c_idx++) {
        const int x                = x0 >> fc->ps.sps->hshift[c_idx];
        const int y                = y0 >> fc->ps.sps->vshift[c_idx];
        const ptrdiff_t src_stride = fc->frame->linesize[c_idx];
        const int ctb_size_h       = ctb_size_y >> fc->ps.sps->hshift[c_idx];
        const int ctb_size_v       = ctb_size_y >> fc->ps.sps->vshift[c_idx];
        const int width            = FFMIN(ctb_size_h, (fc->ps.pps->width  >> fc->ps.sps->hshift[c_idx]) - x);
        const int height           = FFMIN(ctb_size_v, (fc->ps.pps->height >> fc->ps.sps->vshift[c_idx]) - y);
        const uint8_t *src         = POS(c_idx, x0, y0);
        copy_ctb_to_hv(fc, src, src_stride, x, y, width, height, c_idx, rx, ry, top);
    }
}

void ff_vvc_sao_copy_ctb_to_hv(VVCLocalContext *lc, const int rx, const int ry, const int last_row)
{
    if (ry)
        sao_copy_ctb_to_hv(lc, rx, ry - 1, 0);

    sao_copy_ctb_to_hv(lc, rx, ry, 1);

    if (last_row)
        sao_copy_ctb_to_hv(lc, rx, ry, 0);
}

static int sao_can_cross_slices(const VVCFrameContext *fc, const int rx, const int ry, const int dx, const int dy)
{
    const uint8_t lfase = fc->ps.pps->r->pps_loop_filter_across_slices_enabled_flag;

    return lfase || CTB(fc->tab.slice_idx, rx, ry) == CTB(fc->tab.slice_idx, rx + dx, ry + dy);
}

static void sao_get_edges(uint8_t vert_edge[2], uint8_t horiz_edge[2], uint8_t diag_edge[4], int *restore,
    const VVCLocalContext *lc, const int edges[4], const int rx, const int ry)
{
    const VVCFrameContext *fc      = lc->fc;
    const VVCSPS *sps              = fc->ps.sps;
    const H266RawSPS *rsps         = sps->r;
    const VVCPPS *pps              = fc->ps.pps;
    const int subpic_idx           = lc->sc->sh.r->curr_subpic_idx;
    const uint8_t lfase            = fc->ps.pps->r->pps_loop_filter_across_slices_enabled_flag;
    const uint8_t no_tile_filter   = pps->r->num_tiles_in_pic > 1 && !pps->r->pps_loop_filter_across_tiles_enabled_flag;
    const uint8_t no_subpic_filter = rsps->sps_num_subpics_minus1 && !rsps->sps_loop_filter_across_subpic_enabled_flag[subpic_idx];
    uint8_t lf_edge[] = { 0, 0, 0, 0 };

    *restore = no_subpic_filter || no_tile_filter || !lfase || rsps->sps_virtual_boundaries_enabled_flag;

    if (!*restore)
        return;

    if (!edges[LEFT]) {
        lf_edge[LEFT]  = no_tile_filter && pps->ctb_to_col_bd[rx] == rx;
        lf_edge[LEFT] |= no_subpic_filter && rsps->sps_subpic_ctu_top_left_x[subpic_idx] == rx;
        lf_edge[LEFT] |= is_virtual_boundary(fc, rx << sps->ctb_log2_size_y, 1);
        vert_edge[0]   = !sao_can_cross_slices(fc, rx, ry, -1, 0) || lf_edge[LEFT];
    }
    if (!edges[RIGHT]) {
        lf_edge[RIGHT]  = no_tile_filter && pps->ctb_to_col_bd[rx] != pps->ctb_to_col_bd[rx + 1];
        lf_edge[RIGHT] |= no_subpic_filter && rsps->sps_subpic_ctu_top_left_x[subpic_idx] + rsps->sps_subpic_width_minus1[subpic_idx] == rx;
        lf_edge[RIGHT] |= is_virtual_boundary(fc, (rx + 1) << sps->ctb_log2_size_y, 1);
        vert_edge[1]    = !sao_can_cross_slices(fc, rx, ry, 1, 0) || lf_edge[RIGHT];
    }
    if (!edges[TOP]) {
        lf_edge[TOP]   = no_tile_filter && pps->ctb_to_row_bd[ry] == ry;
        lf_edge[TOP]  |= no_subpic_filter && rsps->sps_subpic_ctu_top_left_y[subpic_idx] == ry;
        lf_edge[TOP]  |= is_virtual_boundary(fc, ry << sps->ctb_log2_size_y, 0);
        horiz_edge[0]  = !sao_can_cross_slices(fc, rx, ry, 0, -1) || lf_edge[TOP];
    }
    if (!edges[BOTTOM]) {
        lf_edge[BOTTOM]  = no_tile_filter && pps->ctb_to_row_bd[ry] != pps->ctb_to_row_bd[ry + 1];
        lf_edge[BOTTOM] |= no_subpic_filter && rsps->sps_subpic_ctu_top_left_y[subpic_idx] + rsps->sps_subpic_height_minus1[subpic_idx] == ry;
        lf_edge[BOTTOM] |= is_virtual_boundary(fc, (ry + 1) << sps->ctb_log2_size_y, 0);
        horiz_edge[1]    = !sao_can_cross_slices(fc, rx, ry, 0, 1) || lf_edge[BOTTOM];
    }

    if (!edges[LEFT] && !edges[TOP])
        diag_edge[0] = !sao_can_cross_slices(fc, rx, ry, -1, -1) || lf_edge[LEFT] || lf_edge[TOP];

    if (!edges[TOP] && !edges[RIGHT])
        diag_edge[1] = !sao_can_cross_slices(fc, rx, ry,  1, -1) || lf_edge[RIGHT] || lf_edge[TOP];

    if (!edges[RIGHT] && !edges[BOTTOM])
        diag_edge[2] = !sao_can_cross_slices(fc, rx, ry,  1,  1) || lf_edge[RIGHT] || lf_edge[BOTTOM];

    if (!edges[LEFT] && !edges[BOTTOM])
        diag_edge[3] = !sao_can_cross_slices(fc, rx, ry, -1,  1) || lf_edge[LEFT] || lf_edge[BOTTOM];
}

static void sao_copy_hor(uint8_t *dst, const ptrdiff_t dst_stride,
    const uint8_t *src, const ptrdiff_t src_stride, const int width, const int edges[4], const int ps)
{
    const int left      = 1 - edges[LEFT];
    const int right     = 1 - edges[RIGHT];
    int pos             = 0;

    src -= left << ps;
    dst -= left << ps;

    if (left) {
        copy_pixel(dst, src, ps);
        pos += (1 << ps);
    }
    memcpy(dst + pos, src + pos, width << ps);
    if (right) {
        pos += width << ps;
        copy_pixel(dst + pos, src + pos, ps);
    }
}

static void sao_extends_edges(uint8_t *dst, const ptrdiff_t dst_stride,
    const uint8_t *src, const ptrdiff_t src_stride, const int width, const int height,
    const VVCFrameContext *fc, const int x0, const int y0, const int rx, const int ry, const int edges[4], const int c_idx)
{
    const uint8_t *sao_h = fc->tab.sao_pixel_buffer_h[c_idx];
    const uint8_t *sao_v = fc->tab.sao_pixel_buffer_v[c_idx];
    const int x          = x0 >> fc->ps.sps->hshift[c_idx];
    const int y          = y0 >> fc->ps.sps->vshift[c_idx];
    const int w          = fc->ps.pps->width >> fc->ps.sps->hshift[c_idx];
    const int h          = fc->ps.pps->height >> fc->ps.sps->vshift[c_idx];
    const int ps         = fc->ps.sps->pixel_shift;

    if (!edges[TOP])
        sao_copy_hor(dst - dst_stride, dst_stride, sao_h + (((2 * ry - 1) * w + x) << ps), src_stride, width, edges, ps);

    if (!edges[BOTTOM])
        sao_copy_hor(dst + height * dst_stride, dst_stride, sao_h + (((2 * ry + 2) * w + x) << ps), src_stride, width, edges, ps);

    if (!edges[LEFT])
        copy_vert(dst - (1 << ps), sao_v + (((2 * rx - 1) * h + y) << ps), ps, height, dst_stride, 1 << ps);

    if (!edges[RIGHT])
        copy_vert(dst + (width << ps), sao_v + (((2 * rx + 2) * h + y) << ps),  ps, height, dst_stride, 1 << ps);

    copy_ctb(dst, src, width << ps, height, dst_stride, src_stride);
}

static void sao_restore_vb(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src, ptrdiff_t src_stride,
    const int width, const int height, const int vb_pos, const int ps, const int vertical)
{
    int w = 2;
    int h = (vertical ? height : width);
    int dx = vb_pos - 1;
    int dy = 0;

    if (!vertical) {
        FFSWAP(int, w, h);
        FFSWAP(int, dx, dy);
    }
    dst += dy * dst_stride +(dx << ps);
    src += dy * src_stride +(dx << ps);

    av_image_copy_plane(dst, dst_stride, src, src_stride, w << ps, h);
}

void ff_vvc_sao_filter(VVCLocalContext *lc, int x0, int y0)
{
    VVCFrameContext *fc  = lc->fc;
    const VVCSPS *sps    = fc->ps.sps;
    const int rx         = x0 >> sps->ctb_log2_size_y;
    const int ry         = y0 >> sps->ctb_log2_size_y;
    const int edges[4]   = { !rx, !ry, rx == fc->ps.pps->ctb_width - 1, ry == fc->ps.pps->ctb_height - 1 };
    const SAOParams *sao = &CTB(fc->tab.sao, rx, ry);
    // flags indicating unfilterable edges
    uint8_t vert_edge[]  = { 0, 0 };
    uint8_t horiz_edge[] = { 0, 0 };
    uint8_t diag_edge[]  = { 0, 0, 0, 0 };
    int restore, vb_x = 0, vb_y = 0;;

    if (sps->r->sps_virtual_boundaries_enabled_flag) {
        vb_x = get_virtual_boundary(fc, rx, 1);
        vb_y = get_virtual_boundary(fc, ry, 0);
    }

    sao_get_edges(vert_edge, horiz_edge, diag_edge, &restore, lc, edges, rx, ry);

    for (int c_idx = 0; c_idx < (sps->r->sps_chroma_format_idc ? 3 : 1); c_idx++) {
        static const uint8_t sao_tab[16] = { 0, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8 };
        const ptrdiff_t src_stride       = fc->frame->linesize[c_idx];
        uint8_t *src                     = POS(c_idx, x0, y0);
        const int hs                     = sps->hshift[c_idx];
        const int vs                     = sps->vshift[c_idx];
        const int ps                     = sps->pixel_shift;
        const int width                  = FFMIN(sps->ctb_size_y, fc->ps.pps->width  - x0) >> hs;
        const int height                 = FFMIN(sps->ctb_size_y, fc->ps.pps->height - y0) >> vs;
        const int tab                    = sao_tab[(FFALIGN(width, 8) >> 3) - 1];
        const int sao_eo_class           = sao->eo_class[c_idx];

        switch (sao->type_idx[c_idx]) {
            case SAO_BAND:
                fc->vvcdsp.sao.band_filter[tab](src, src, src_stride, src_stride,
                    sao->offset_val[c_idx], sao->band_position[c_idx], width, height);
                break;
            case SAO_EDGE:
            {
                const ptrdiff_t dst_stride = 2 * MAX_PB_SIZE + AV_INPUT_BUFFER_PADDING_SIZE;
                uint8_t *dst               = lc->sao_buffer + dst_stride + AV_INPUT_BUFFER_PADDING_SIZE;

                sao_extends_edges(dst, dst_stride, src, src_stride, width, height, fc, x0, y0, rx, ry, edges, c_idx);

                fc->vvcdsp.sao.edge_filter[tab](src, dst, src_stride, sao->offset_val[c_idx],
                    sao->eo_class[c_idx], width, height);
                fc->vvcdsp.sao.edge_restore[restore](src, dst, src_stride, dst_stride,
                    sao, edges, width, height, c_idx, vert_edge, horiz_edge, diag_edge);

                if (vb_x > x0 &&  sao_eo_class != SAO_EO_VERT)
                    sao_restore_vb(src, src_stride, dst, dst_stride, width, height, (vb_x - x0) >> hs, ps, 1);
                if (vb_y > y0 &&  sao_eo_class != SAO_EO_HORIZ)
                    sao_restore_vb(src, src_stride, dst, dst_stride, width, height, (vb_y - y0) >> vs, ps, 0);

                break;
            }
        }
    }
}

#define TAB_BS(t, x, y)       (t)[((y) >> MIN_TU_LOG2) * (fc->ps.pps->min_tu_width) + ((x) >> MIN_TU_LOG2)]
#define TAB_MAX_LEN(t, x, y)  (t)[((y) >> MIN_TU_LOG2) * (fc->ps.pps->min_tu_width) + ((x) >> MIN_TU_LOG2)]

//8 samples a time
#define DEBLOCK_STEP            8
#define LUMA_GRID               4
#define CHROMA_GRID             8

static int boundary_strength(const VVCLocalContext *lc, const MvField *curr, const MvField *neigh,
    const RefPicList *neigh_rpl)
{
    RefPicList *rpl = lc->sc->rpl;

    if (curr->pred_flag == PF_PLT)
        return 0;

    if (curr->pred_flag == PF_IBC)
        return FFABS(neigh->mv[0].x - curr->mv[0].x) >= 8 || FFABS(neigh->mv[0].y - curr->mv[0].y) >= 8;

    if (curr->pred_flag == PF_BI &&  neigh->pred_flag == PF_BI) {
        // same L0 and L1
        if (rpl[L0].refs[curr->ref_idx[L0]].poc == neigh_rpl[L0].refs[neigh->ref_idx[L0]].poc  &&
            rpl[L0].refs[curr->ref_idx[L0]].poc == rpl[L1].refs[curr->ref_idx[L1]].poc &&
            neigh_rpl[L0].refs[neigh->ref_idx[L0]].poc == neigh_rpl[L1].refs[neigh->ref_idx[L1]].poc) {
            if ((FFABS(neigh->mv[0].x - curr->mv[0].x) >= 8 || FFABS(neigh->mv[0].y - curr->mv[0].y) >= 8 ||
                 FFABS(neigh->mv[1].x - curr->mv[1].x) >= 8 || FFABS(neigh->mv[1].y - curr->mv[1].y) >= 8) &&
                (FFABS(neigh->mv[1].x - curr->mv[0].x) >= 8 || FFABS(neigh->mv[1].y - curr->mv[0].y) >= 8 ||
                 FFABS(neigh->mv[0].x - curr->mv[1].x) >= 8 || FFABS(neigh->mv[0].y - curr->mv[1].y) >= 8))
                return 1;
            else
                return 0;
        } else if (neigh_rpl[L0].refs[neigh->ref_idx[L0]].poc == rpl[L0].refs[curr->ref_idx[L0]].poc &&
                   neigh_rpl[L1].refs[neigh->ref_idx[L1]].poc == rpl[L1].refs[curr->ref_idx[L1]].poc) {
            if (FFABS(neigh->mv[0].x - curr->mv[0].x) >= 8 || FFABS(neigh->mv[0].y - curr->mv[0].y) >= 8 ||
                FFABS(neigh->mv[1].x - curr->mv[1].x) >= 8 || FFABS(neigh->mv[1].y - curr->mv[1].y) >= 8)
                return 1;
            else
                return 0;
        } else if (neigh_rpl[L1].refs[neigh->ref_idx[L1]].poc == rpl[L0].refs[curr->ref_idx[L0]].poc &&
                   neigh_rpl[L0].refs[neigh->ref_idx[L0]].poc == rpl[L1].refs[curr->ref_idx[L1]].poc) {
            if (FFABS(neigh->mv[1].x - curr->mv[0].x) >= 8 || FFABS(neigh->mv[1].y - curr->mv[0].y) >= 8 ||
                FFABS(neigh->mv[0].x - curr->mv[1].x) >= 8 || FFABS(neigh->mv[0].y - curr->mv[1].y) >= 8)
                return 1;
            else
                return 0;
        } else {
            return 1;
        }
    } else if ((curr->pred_flag != PF_BI) && (neigh->pred_flag != PF_BI)){ // 1 MV
        Mv A, B;
        int ref_A, ref_B;

        if (curr->pred_flag & 1) {
            A     = curr->mv[0];
            ref_A = rpl[L0].refs[curr->ref_idx[L0]].poc;
        } else {
            A     = curr->mv[1];
            ref_A = rpl[L1].refs[curr->ref_idx[L1]].poc;
        }

        if (neigh->pred_flag & 1) {
            B     = neigh->mv[0];
            ref_B = neigh_rpl[L0].refs[neigh->ref_idx[L0]].poc;
        } else {
            B     = neigh->mv[1];
            ref_B = neigh_rpl[L1].refs[neigh->ref_idx[L1]].poc;
        }

        if (ref_A == ref_B) {
            if (FFABS(A.x - B.x) >= 8 || FFABS(A.y - B.y) >= 8)
                return 1;
            else
                return 0;
        } else
            return 1;
    }

    return 1;
}

//part of 8.8.3.3 Derivation process of transform block boundary
static void derive_max_filter_length_luma(const VVCFrameContext *fc, const int qx, const int qy,
    const int size_q, const int has_subblock, const int vertical, uint8_t *max_len_p, uint8_t *max_len_q)
{
    const int px =  vertical ? qx - 1 : qx;
    const int py = !vertical ? qy - 1 : qy;
    const uint8_t *tb_size = vertical ? fc->tab.tb_width[LUMA] : fc->tab.tb_height[LUMA];
    const int size_p = tb_size[(py >> MIN_TU_LOG2) * fc->ps.pps->min_tu_width + (px >> MIN_TU_LOG2)];
    const int min_cb_log2 = fc->ps.sps->min_cb_log2_size_y;
    const int off_p = (py >> min_cb_log2) * fc->ps.pps->min_cb_width + (px >> min_cb_log2);

    if (size_p <= 4 || size_q <= 4) {
        *max_len_p = *max_len_q = 1;
    } else {
        *max_len_p = *max_len_q = 3;
        if (size_p >= 32)
            *max_len_p = 7;
        if (size_q >= 32)
            *max_len_q = 7;
    }
    if (has_subblock)
        *max_len_q = FFMIN(5, *max_len_q);
    if (fc->tab.msf[off_p] || fc->tab.iaf[off_p])
        *max_len_p = FFMIN(5, *max_len_p);
}

static void vvc_deblock_subblock_bs(const VVCLocalContext *lc,
    const int cb, int x0, int y0, int width, int height, const int vertical)
{
    const VVCFrameContext  *fc = lc->fc;
    const MvField *tab_mvf     = fc->tab.mvf;
    const RefPicList *rpl      = lc->sc->rpl;
    int stridea                = fc->ps.pps->min_pu_width;
    int strideb                = 1;
    const int log2_min_pu_size = MIN_PU_LOG2;

    if (!vertical) {
        FFSWAP(int, x0, y0);
        FFSWAP(int, width, height);
        FFSWAP(int, stridea, strideb);
    }

    // bs for TU internal vertical PU boundaries
    for (int i = 8 - ((x0 - cb) % 8); i < width; i += 8) {
        const int is_vb = is_virtual_boundary(fc, x0 + i, vertical);
        const int xp_pu = (x0 + i - 1) >> log2_min_pu_size;
        const int xq_pu = (x0 + i)     >> log2_min_pu_size;

        for (int j = 0; j < height; j += 4) {
            const int y_pu       = (y0 + j) >> log2_min_pu_size;
            const MvField *mvf_p = &tab_mvf[y_pu * stridea + xp_pu * strideb];
            const MvField *mvf_q = &tab_mvf[y_pu * stridea + xq_pu * strideb];
            const int bs         = is_vb ? 0 : boundary_strength(lc, mvf_q, mvf_p, rpl);
            int x                = x0 + i;
            int y                = y0 + j;
            uint8_t max_len_p = 0, max_len_q = 0;

            if (!vertical)
                FFSWAP(int, x, y);

            TAB_BS(fc->tab.bs[vertical][LUMA], x, y) = bs;

            if (i == 4 || i == width - 4)
                max_len_p = max_len_q = 1;
            else if (i == 8 || i == width - 8)
                max_len_p = max_len_q = 2;
            else
                max_len_p = max_len_q = 3;

            TAB_MAX_LEN(fc->tab.max_len_p[vertical], x, y) = max_len_p;
            TAB_MAX_LEN(fc->tab.max_len_q[vertical], x, y) = max_len_q;
        }
    }
}

static av_always_inline int deblock_bs(const VVCLocalContext *lc,
    const int x_p, const int y_p, const int x_q, const int y_q, const CodingUnit *cu, const TransformUnit *tu,
    const RefPicList *rpl_p, const int c_idx, const int off_to_cb, const uint8_t has_sub_block)
{
    const VVCFrameContext *fc  = lc->fc;
    const MvField *tab_mvf     = fc->tab.mvf;
    const int log2_min_pu_size = MIN_PU_LOG2;
    const int log2_min_tu_size = MIN_TU_LOG2;
    const int log2_min_cb_size = fc->ps.sps->min_cb_log2_size_y;
    const int min_pu_width     = fc->ps.pps->min_pu_width;
    const int min_tu_width     = fc->ps.pps->min_tu_width;
    const int min_cb_width     = fc->ps.pps->min_cb_width;
    const int pu_p             = (y_p >> log2_min_pu_size) * min_pu_width  + (x_p >>  log2_min_pu_size);
    const int pu_q             = (y_q >> log2_min_pu_size) * min_pu_width  + (x_q >>  log2_min_pu_size);
    const MvField *mvf_p       = &tab_mvf[pu_p];
    const MvField *mvf_q       = &tab_mvf[pu_q];
    const uint8_t chroma       = !!c_idx;
    const int tu_p             = (y_p >> log2_min_tu_size) * min_tu_width  + (x_p >>  log2_min_tu_size);
    const int cb_p             = (y_p >> log2_min_cb_size) * min_cb_width  + (x_p >>  log2_min_cb_size);
    const uint8_t pcmf         = fc->tab.pcmf[chroma][cb_p] && cu->bdpcm_flag[chroma];
    const uint8_t intra        = fc->tab.cpm[chroma][cb_p] == MODE_INTRA || cu->pred_mode == MODE_INTRA;
    const uint8_t same_mode    = fc->tab.cpm[chroma][cb_p] == cu->pred_mode;

    if (pcmf)
        return 0;

    if (intra || mvf_p->ciip_flag || mvf_q->ciip_flag)
        return 2;

    if (chroma) {
        return fc->tab.tu_coded_flag[c_idx][tu_p] ||
               fc->tab.tu_joint_cbcr_residual_flag[tu_p] ||
               tu->coded_flag[c_idx] ||
               tu->joint_cbcr_residual_flag;
    }

    if (fc->tab.tu_coded_flag[LUMA][tu_p] || tu->coded_flag[LUMA])
        return 1;

    if ((off_to_cb && ((off_to_cb % 8) || !has_sub_block)))
        return 0;                                     // inside a cu, not aligned to 8 or with no subblocks

    if (!same_mode)
        return 1;

    return boundary_strength(lc, mvf_q, mvf_p, rpl_p);
}

static int deblock_is_boundary(const VVCLocalContext *lc, const int boundary,
    const int pos, const int rs, const int vertical)
{
    const VVCFrameContext *fc = lc->fc;
    const H266RawSPS *rsps    = fc->ps.sps->r;
    const H266RawPPS *rpps    = fc->ps.pps->r;
    int flag;
    if (boundary && (pos % fc->ps.sps->ctb_size_y) == 0) {
        flag = vertical ? BOUNDARY_LEFT_SLICE : BOUNDARY_UPPER_SLICE;
        if (lc->boundary_flags & flag &&
            !rpps->pps_loop_filter_across_slices_enabled_flag)
            return 0;

        flag = vertical ? BOUNDARY_LEFT_TILE : BOUNDARY_UPPER_TILE;
        if (lc->boundary_flags & flag &&
            !rpps->pps_loop_filter_across_tiles_enabled_flag)
            return 0;

        flag = vertical ? BOUNDARY_LEFT_SUBPIC : BOUNDARY_UPPER_SUBPIC;
        if (lc->boundary_flags & flag) {
            const int q_rs              = rs - (vertical ? 1 : fc->ps.pps->ctb_width);
            const SliceContext *q_slice = lc->fc->slices[lc->fc->tab.slice_idx[q_rs]];

            if (!rsps->sps_loop_filter_across_subpic_enabled_flag[q_slice->sh.r->curr_subpic_idx] ||
                !rsps->sps_loop_filter_across_subpic_enabled_flag[lc->sc->sh.r->curr_subpic_idx])
                return 0;
        }
    }
    return boundary;
}

static void vvc_deblock_bs_luma(const VVCLocalContext *lc,
    const int x0, const int y0, const int width, const int height,
    const CodingUnit *cu, const TransformUnit *tu,  int rs, const int vertical)
{
    const VVCFrameContext *fc = lc->fc;
    const PredictionUnit *pu  = &cu->pu;
    const int mask            = LUMA_GRID - 1;
    const int pos             = vertical ? x0 : y0;
    const int cb              = vertical ? cu->x0 : cu->y0;
    const int is_intra        = cu->pred_mode == MODE_INTRA;
    const int cb_size         = vertical ? cu->cb_width : cu->cb_height;
    const int has_sb          = !is_intra && (pu->merge_subblock_flag || pu->inter_affine_flag) && cb_size > 8;

    if (deblock_is_boundary(lc, pos > 0 && !(pos & mask), pos, rs, vertical)) {
        const int is_vb         = is_virtual_boundary(fc, pos, vertical);
        const int size          = vertical ? height : width;
        const int size_q        = vertical ? width  : height;
        const int off           = cb - pos;
        const int flag          = vertical ? BOUNDARY_LEFT_SLICE : BOUNDARY_UPPER_SLICE;
        const RefPicList *rpl_p =
            (lc->boundary_flags & flag) ? ff_vvc_get_ref_list(fc, fc->ref, x0 - vertical, y0 - !vertical) : lc->sc->rpl;

        for (int i = 0; i < size; i += 4) {
            const int x = x0 + i * !vertical;
            const int y = y0 + i * vertical;
            uint8_t max_len_p, max_len_q;
            const int bs = is_vb ? 0 : deblock_bs(lc, x - vertical, y - !vertical, x, y, cu, tu, rpl_p, LUMA, off, has_sb);

            TAB_BS(fc->tab.bs[vertical][LUMA], x, y) = bs;

            derive_max_filter_length_luma(fc, x, y, size_q, has_sb, vertical, &max_len_p, &max_len_q);
            TAB_MAX_LEN(fc->tab.max_len_p[vertical], x, y) = max_len_p;
            TAB_MAX_LEN(fc->tab.max_len_q[vertical], x, y) = max_len_q;
        }
    }

    if (has_sb)
        vvc_deblock_subblock_bs(lc, cb, x0, y0, width, height, vertical);
}

static void vvc_deblock_bs_chroma(const VVCLocalContext *lc,
    const int x0, const int y0, const int width, const int height,
    const CodingUnit *cu, const TransformUnit *tu, const int rs, const int vertical)
{
    const VVCFrameContext *fc = lc->fc;
    const int shift           = (vertical ? fc->ps.sps->hshift : fc->ps.sps->vshift)[CHROMA];
    const int mask            = (CHROMA_GRID << shift) - 1;
    const int pos             = vertical ? x0 : y0;

    if (deblock_is_boundary(lc, pos > 0 && !(pos & mask), pos, rs, vertical)) {
        const int is_vb = is_virtual_boundary(fc, pos, vertical);
        const int size  = vertical ? height : width;

        for (int c_idx = CB; c_idx <= CR; c_idx++) {
            for (int i = 0; i < size; i += 2) {
                const int x  = x0 + i * !vertical;
                const int y  = y0 + i * vertical;
                const int bs = is_vb ? 0 : deblock_bs(lc, x - vertical, y - !vertical, x, y, cu, tu, NULL, c_idx, 0, 0);

                TAB_BS(fc->tab.bs[vertical][c_idx], x, y) = bs;
            }
        }
    }
}

typedef void (*deblock_bs_fn)(const VVCLocalContext *lc, const int x0, const int y0,
    const int width, const int height, const int rs, const int vertical);

void ff_vvc_deblock_bs(VVCLocalContext *lc, const int rx, const int ry, const int rs)
{
    const VVCFrameContext *fc  = lc->fc;
    const VVCSPS *sps          = fc->ps.sps;
    const int x0               = rx << sps->ctb_log2_size_y;
    const int y0               = ry << sps->ctb_log2_size_y;

    ff_vvc_decode_neighbour(lc, x0, y0, rx, ry, rs);
    for (const CodingUnit *cu = fc->tab.cus[rs]; cu; cu = cu->next) {
        for (const TransformUnit *tu = cu->tus.head; tu; tu = tu->next) {
            for (int vertical = 0; vertical <= 1; vertical++) {
                if (tu->avail[LUMA])
                    vvc_deblock_bs_luma(lc, tu->x0, tu->y0, tu->width, tu->height, cu, tu, rs, vertical);
                if (tu->avail[CHROMA]) {
                    if (cu->isp_split_type != ISP_NO_SPLIT && cu->tree_type == SINGLE_TREE)
                        vvc_deblock_bs_chroma(lc, cu->x0, cu->y0, cu->cb_width, cu->cb_height, cu, tu, rs, vertical);
                    else
                        vvc_deblock_bs_chroma(lc, tu->x0, tu->y0, tu->width, tu->height, cu, tu, rs, vertical);
                }
            }
        }
    }
}

//part of 8.8.3.3 Derivation process of transform block boundary
static void max_filter_length_luma(const VVCFrameContext *fc, const int qx, const int qy,
                                   const int vertical, uint8_t *max_len_p, uint8_t *max_len_q)
{
    *max_len_p = TAB_MAX_LEN(fc->tab.max_len_p[vertical], qx, qy);
    *max_len_q = TAB_MAX_LEN(fc->tab.max_len_q[vertical], qx, qy);
}

//part of 8.8.3.3 Derivation process of transform block boundary
static void max_filter_length_chroma(const VVCFrameContext *fc, const int qx, const int qy,
                                     const int vertical, const int horizontal_ctu_edge, const int bs, uint8_t *max_len_p, uint8_t *max_len_q)
{
    const int px =  vertical ? qx - 1 : qx;
    const int py = !vertical ? qy - 1 : qy;
    const uint8_t *tb_size = vertical ? fc->tab.tb_width[CHROMA] : fc->tab.tb_height[CHROMA];

    const int size_p = tb_size[(py >> MIN_TU_LOG2) * fc->ps.pps->min_tu_width + (px >> MIN_TU_LOG2)];
    const int size_q = tb_size[(qy >> MIN_TU_LOG2) * fc->ps.pps->min_tu_width + (qx >> MIN_TU_LOG2)];
    if (size_p >= 8 && size_q >= 8) {
        *max_len_p = *max_len_q = 3;
        if (horizontal_ctu_edge)
            *max_len_p = 1;
    } else {
        //part of 8.8.3.6.4 Decision process for chroma block edges
        *max_len_p = *max_len_q = (bs == 2);
    }
}

static void max_filter_length(const VVCFrameContext *fc, const int qx, const int qy,
    const int c_idx, const int vertical, const int horizontal_ctu_edge, const int bs, uint8_t *max_len_p, uint8_t *max_len_q)
{
    if (!c_idx)
        max_filter_length_luma(fc, qx, qy, vertical, max_len_p, max_len_q);
    else
        max_filter_length_chroma(fc, qx, qy, vertical, horizontal_ctu_edge, bs, max_len_p, max_len_q);
}

#define TC_CALC(qp, bs)                                                 \
    tctable[av_clip((qp) + DEFAULT_INTRA_TC_OFFSET * ((bs) - 1) +       \
                    (tc_offset & -2),                                   \
                    0, MAX_QP + DEFAULT_INTRA_TC_OFFSET)]

// part of 8.8.3.6.2 Decision process for luma block edges
static int get_qp_y(const VVCFrameContext *fc, const uint8_t *src, const int x, const int y, const int vertical)
{
    const VVCSPS *sps = fc->ps.sps;
    const int qp      = (ff_vvc_get_qPy(fc, x - vertical, y - !vertical) + ff_vvc_get_qPy(fc, x, y) + 1) >> 1;
    int qp_offset     = 0;
    int level;

    if (!sps->r->sps_ladf_enabled_flag)
        return qp;

    level = fc->vvcdsp.lf.ladf_level[vertical](src, fc->frame->linesize[LUMA]);
    qp_offset = sps->r->sps_ladf_lowest_interval_qp_offset;
    for (int i = 0; i < sps->num_ladf_intervals - 1 && level > sps->ladf_interval_lower_bound[i + 1]; i++)
        qp_offset = sps->r->sps_ladf_qp_offset[i];

    return qp + qp_offset;
}

// part of 8.8.3.6.2 Decision process for luma block edges
static int get_qp_c(const VVCFrameContext *fc, const int x, const int y, const int c_idx, const int vertical)
{
    const VVCSPS *sps = fc->ps.sps;
    return (get_qPc(fc, x - vertical, y - !vertical, c_idx) + get_qPc(fc, x, y, c_idx) - 2 * sps->qp_bd_offset + 1) >> 1;
}

static int get_qp(const VVCFrameContext *fc, const uint8_t *src, const int x, const int y, const int c_idx, const int vertical)
{
    if (!c_idx)
        return get_qp_y(fc, src, x, y, vertical);
    return get_qp_c(fc, x, y, c_idx, vertical);
}

static void vvc_deblock(const VVCLocalContext *lc, int x0, int y0, const int rs, const int vertical)
{
    VVCFrameContext *fc        = lc->fc;
    const VVCSPS *sps          = fc->ps.sps;
    const int c_end            = sps->r->sps_chroma_format_idc ? VVC_MAX_SAMPLE_ARRAYS : 1;
    const int ctb_size         = fc->ps.sps->ctb_size_y;
    const DBParams *params     = fc->tab.deblock + rs;
    int x_end                  = FFMIN(x0 + ctb_size, fc->ps.pps->width);
    int y_end                  = FFMIN(y0 + ctb_size, fc->ps.pps->height);
    const int log2_min_cb_size = fc->ps.sps->min_cb_log2_size_y;
    const int min_cb_width     = fc->ps.pps->min_cb_width;

    if (!vertical) {
        FFSWAP(int, x_end, y_end);
        FFSWAP(int, x0, y0);
    }

    for (int c_idx = 0; c_idx < c_end; c_idx++) {
        const int hs          = (vertical ? sps->hshift : sps->vshift)[c_idx];
        const int vs          = (vertical ? sps->vshift : sps->hshift)[c_idx];
        const int grid        = c_idx ? (CHROMA_GRID << hs) : LUMA_GRID;
        const int tc_offset   = params->tc_offset[c_idx];
        const int beta_offset = params->beta_offset[c_idx];
        const int src_stride  = fc->frame->linesize[c_idx];

        for (int y = y0; y < y_end; y += (DEBLOCK_STEP << vs)) {
            for (int x = x0 ? x0 : grid; x < x_end; x += grid) {
                const uint8_t horizontal_ctu_edge = !vertical && !(x % ctb_size);
                int32_t bs[4], beta[4], tc[4] = { 0 }, all_zero_bs = 1;
                uint8_t max_len_p[4], max_len_q[4];
                uint8_t no_p[4] = { 0 };
                uint8_t no_q[4] = { 0 };

                for (int i = 0; i < DEBLOCK_STEP >> (2 - vs); i++) {
                    int tx         = x;
                    int ty         = y + (i << 2);
                    const int end  = ty >= y_end;

                    if (!vertical)
                        FFSWAP(int, tx, ty);

                    bs[i] = end ? 0 : TAB_BS(fc->tab.bs[vertical][c_idx], tx, ty);
                    if (bs[i]) {
                        const int qp = get_qp(fc, POS(c_idx, tx, ty), tx, ty, c_idx, vertical);
                        beta[i] = betatable[av_clip(qp + beta_offset, 0, MAX_QP)];
                        tc[i] = TC_CALC(qp, bs[i]) ;
                        max_filter_length(fc, tx, ty, c_idx, vertical, horizontal_ctu_edge, bs[i], &max_len_p[i], &max_len_q[i]);
                        all_zero_bs = 0;

                        if (sps->r->sps_palette_enabled_flag) {
                            const int cu_q = (ty             >> log2_min_cb_size) * min_cb_width + (tx            >> log2_min_cb_size);
                            const int cu_p = (ty - !vertical >> log2_min_cb_size) * min_cb_width + (tx - vertical >> log2_min_cb_size);
                            no_q[i] = fc->tab.cpm[!!c_idx][cu_q] == MODE_PLT;
                            no_p[i] = cu_p >= 0 && fc->tab.cpm[!!c_idx][cu_p] == MODE_PLT;
                        }
                    }
                }

                if (!all_zero_bs) {
                    uint8_t *src = vertical ? POS(c_idx, x, y) : POS(c_idx, y, x);
                    if (!c_idx)
                        fc->vvcdsp.lf.filter_luma[vertical](src, src_stride, beta, tc, no_p, no_q, max_len_p, max_len_q, horizontal_ctu_edge);
                    else
                        fc->vvcdsp.lf.filter_chroma[vertical](src, src_stride, beta, tc, no_p, no_q, max_len_p, max_len_q, vs);
                }
            }
        }
    }
}

void ff_vvc_deblock_vertical(const VVCLocalContext *lc, const int x0, const int y0, const int rs)
{
    vvc_deblock(lc, x0, y0, rs, 1);
}

void ff_vvc_deblock_horizontal(const VVCLocalContext *lc, const int x0, const int y0, const int rs)
{
    vvc_deblock(lc, x0, y0, rs, 0);
}

static void alf_copy_border(uint8_t *dst, const uint8_t *src,
    const int pixel_shift, int width, const int height, const ptrdiff_t dst_stride, const ptrdiff_t src_stride)
{
    width <<= pixel_shift;
    for (int i = 0; i < height; i++) {
        memcpy(dst, src, width);
        dst += dst_stride;
        src += src_stride;
    }
}

static void alf_extend_vert(uint8_t *_dst, const uint8_t *_src,
    const int pixel_shift, const int width, const int height, ptrdiff_t stride)
{
    if (pixel_shift == 0) {
        for (int i = 0; i < height; i++) {
            memset(_dst, *_src, width);
            _src += stride;
            _dst += stride;
        }
    } else {
        const uint16_t *src = (const uint16_t *)_src;
        uint16_t *dst = (uint16_t *)_dst;
        stride >>= pixel_shift;

        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++)
                dst[j] = *src;
            src += stride;
            dst += stride;
        }
    }
}

static void alf_extend_horz(uint8_t *dst, const uint8_t *src,
    const int pixel_shift, int width, const int height, const ptrdiff_t stride)
{
    width <<= pixel_shift;
    for (int i = 0; i < height; i++) {
        memcpy(dst, src, width);
        dst += stride;
    }
}

static void alf_copy_ctb_to_hv(VVCFrameContext *fc, const uint8_t *src, const ptrdiff_t src_stride,
    const int x, const int y, const int width, const int height, const int rx, const int ry, const int c_idx)
{
    const int ps            = fc->ps.sps->pixel_shift;
    const int w             = fc->ps.pps->width >> fc->ps.sps->hshift[c_idx];
    const int h             = fc->ps.pps->height >> fc->ps.sps->vshift[c_idx];
    const int border_pixels = (c_idx == 0) ? ALF_BORDER_LUMA : ALF_BORDER_CHROMA;
    const int offset_h[]    = { 0, height - border_pixels };
    const int offset_v[]    = { 0, width  - border_pixels };

    /* copy horizontal edges */
    for (int i = 0; i < FF_ARRAY_ELEMS(offset_h); i++) {
        alf_copy_border(fc->tab.alf_pixel_buffer_h[c_idx][i] + ((border_pixels * ry * w + x)<< ps),
            src + offset_h[i] * src_stride, ps, width, border_pixels, w << ps, src_stride);
    }
    /* copy vertical edges */
    for (int i = 0; i < FF_ARRAY_ELEMS(offset_v); i++) {
        alf_copy_border(fc->tab.alf_pixel_buffer_v[c_idx][i] + ((h * rx + y) * (border_pixels << ps)),
            src + (offset_v[i] << ps), ps, border_pixels, height, border_pixels << ps, src_stride);
    }
}

static void alf_fill_border_h(uint8_t *dst, const ptrdiff_t dst_stride, const uint8_t *src, const ptrdiff_t src_stride,
    const uint8_t *border, const int width, const int border_pixels, const int ps, const int edge)
{
    if (edge)
        alf_extend_horz(dst, border, ps, width, border_pixels, dst_stride);
    else
        alf_copy_border(dst, src, ps, width, border_pixels, dst_stride, src_stride);
}

static void alf_fill_border_v(uint8_t *dst, const ptrdiff_t dst_stride, const uint8_t *src,
    const uint8_t *border, const int border_pixels, const int height, const int pixel_shift, const int *edges, const int edge)
{
    const ptrdiff_t src_stride = (border_pixels << pixel_shift);

    if (edge) {
        alf_extend_vert(dst, border, pixel_shift, border_pixels, height + 2 * border_pixels, dst_stride);
        return;
    }

    //left/right
    alf_copy_border(dst + dst_stride * border_pixels * edges[TOP], src + src_stride * border_pixels * edges[TOP],
        pixel_shift, border_pixels, height + (!edges[TOP] + !edges[BOTTOM]) * border_pixels, dst_stride, src_stride);

    //top left/right
    if (edges[TOP])
        alf_extend_horz(dst, dst + dst_stride * border_pixels, pixel_shift, border_pixels, border_pixels, dst_stride);

    //bottom left/right
    if (edges[BOTTOM]) {
        dst += dst_stride * (border_pixels + height);
        alf_extend_horz(dst, dst - dst_stride, pixel_shift, border_pixels, border_pixels, dst_stride);
    }
}

static void alf_prepare_buffer(VVCFrameContext *fc, uint8_t *_dst, const uint8_t *_src, const int x, const int y,
    const int rx, const int ry, const int width, const int height, const ptrdiff_t dst_stride, const ptrdiff_t src_stride,
    const int c_idx, const int *edges)
{
    const int ps = fc->ps.sps->pixel_shift;
    const int w = fc->ps.pps->width >> fc->ps.sps->hshift[c_idx];
    const int h = fc->ps.pps->height >> fc->ps.sps->vshift[c_idx];
    const int border_pixels = c_idx == 0 ? ALF_BORDER_LUMA : ALF_BORDER_CHROMA;
    uint8_t *dst, *src;

    copy_ctb(_dst, _src, width << ps, height, dst_stride, src_stride);

    //top
    src = fc->tab.alf_pixel_buffer_h[c_idx][1] + (((border_pixels * w) << ps) * (ry - 1) + (x << ps));
    dst = _dst - border_pixels * dst_stride;
    alf_fill_border_h(dst, dst_stride, src, w  << ps, _dst, width, border_pixels, ps, edges[TOP]);

    //bottom
    src = fc->tab.alf_pixel_buffer_h[c_idx][0] + (((border_pixels * w) << ps) * (ry + 1) + (x << ps));
    dst = _dst + height * dst_stride;
    alf_fill_border_h(dst, dst_stride, src, w  << ps, _dst + (height - 1) * dst_stride, width, border_pixels, ps, edges[BOTTOM]);


    //left
    src = fc->tab.alf_pixel_buffer_v[c_idx][1] + (h * (rx - 1) + y - border_pixels) * (border_pixels << ps);
    dst = _dst - (border_pixels << ps) - border_pixels * dst_stride;
    alf_fill_border_v(dst, dst_stride, src,  dst + (border_pixels << ps), border_pixels, height, ps, edges, edges[LEFT]);

    //right
    src = fc->tab.alf_pixel_buffer_v[c_idx][0] + (h * (rx + 1) + y - border_pixels) * (border_pixels << ps);
    dst = _dst + (width << ps) - border_pixels * dst_stride;
    alf_fill_border_v(dst, dst_stride, src,  dst - (1 << ps), border_pixels, height, ps, edges, edges[RIGHT]);
}

#define ALF_MAX_BLOCKS_IN_CTU   (MAX_CTU_SIZE * MAX_CTU_SIZE / ALF_BLOCK_SIZE / ALF_BLOCK_SIZE)
#define ALF_MAX_FILTER_SIZE     (ALF_MAX_BLOCKS_IN_CTU * ALF_NUM_COEFF_LUMA)

static void alf_get_coeff_and_clip(VVCLocalContext *lc, int16_t *coeff, int16_t *clip,
    const uint8_t *src, ptrdiff_t src_stride, int width, int height, int vb_pos, const ALFParams *alf)
{
    const VVCFrameContext *fc     = lc->fc;
    const H266RawSliceHeader *rsh = lc->sc->sh.r;
    uint8_t fixed_clip_set[ALF_NUM_FILTERS_LUMA][ALF_NUM_COEFF_LUMA] = { 0 };
    const int16_t *coeff_set;
    const uint8_t *clip_idx_set;
    const uint8_t *class_to_filt;
    const int size = width * height / ALF_BLOCK_SIZE / ALF_BLOCK_SIZE;
    int class_idx[ALF_MAX_BLOCKS_IN_CTU];
    int transpose_idx[ALF_MAX_BLOCKS_IN_CTU];

    if (alf->ctb_filt_set_idx_y < 16) {
        coeff_set         = &ff_vvc_alf_fix_filt_coeff[0][0];
        clip_idx_set      = &fixed_clip_set[0][0];
        class_to_filt     = ff_vvc_alf_class_to_filt_map[alf->ctb_filt_set_idx_y];
    } else {
        const int id      = rsh->sh_alf_aps_id_luma[alf->ctb_filt_set_idx_y - 16];
        const VVCALF *aps = fc->ps.alf_list[id];
        coeff_set         = &aps->luma_coeff[0][0];
        clip_idx_set      = &aps->luma_clip_idx[0][0];
        class_to_filt     = ff_vvc_alf_aps_class_to_filt_map;
    }
    fc->vvcdsp.alf.classify(class_idx, transpose_idx, src, src_stride, width, height,
        vb_pos, lc->alf_gradient_tmp);
    fc->vvcdsp.alf.recon_coeff_and_clip(coeff, clip, class_idx, transpose_idx, size,
        coeff_set, clip_idx_set, class_to_filt);
}

static void alf_filter_luma(VVCLocalContext *lc, uint8_t *dst, const uint8_t *src,
    const ptrdiff_t dst_stride, const ptrdiff_t src_stride, const int x0, const int y0,
    const int width, const int height, const int _vb_pos, const ALFParams *alf)
{
    const VVCFrameContext *fc = lc->fc;
    int vb_pos                = _vb_pos - y0;
    int16_t *coeff            = (int16_t*)lc->tmp;
    int16_t *clip             = (int16_t *)lc->tmp1;

    av_assert0(ALF_MAX_FILTER_SIZE <= sizeof(lc->tmp));
    av_assert0(ALF_MAX_FILTER_SIZE * sizeof(int16_t) <= sizeof(lc->tmp1));

    alf_get_coeff_and_clip(lc, coeff, clip, src, src_stride, width, height, vb_pos, alf);
    fc->vvcdsp.alf.filter[LUMA](dst, dst_stride, src, src_stride, width, height, coeff, clip, vb_pos);
}

static int alf_clip_from_idx(const VVCFrameContext *fc, const int idx)
{
    const VVCSPS *sps  = fc->ps.sps;
    const int offset[] = {0, 3, 5, 7};

    return 1 << (sps->bit_depth - offset[idx]);
}

static void alf_filter_chroma(VVCLocalContext *lc, uint8_t *dst, const uint8_t *src,
    const ptrdiff_t dst_stride, const ptrdiff_t src_stride, const int c_idx,
    const int width, const int height, const int vb_pos, const ALFParams *alf)
{
    VVCFrameContext *fc           = lc->fc;
    const H266RawSliceHeader *rsh = lc->sc->sh.r;
    const VVCALF *aps             = fc->ps.alf_list[rsh->sh_alf_aps_id_chroma];
    const int idx                 = alf->alf_ctb_filter_alt_idx[c_idx - 1];
    const int16_t *coeff          = aps->chroma_coeff[idx];
    int16_t clip[ALF_NUM_COEFF_CHROMA];

    for (int i = 0; i < ALF_NUM_COEFF_CHROMA; i++)
        clip[i] = alf_clip_from_idx(fc, aps->chroma_clip_idx[idx][i]);

    fc->vvcdsp.alf.filter[CHROMA](dst, dst_stride, src, src_stride, width, height, coeff, clip, vb_pos);
}

static void alf_filter_cc(VVCLocalContext *lc, uint8_t *dst, const uint8_t *luma,
    const ptrdiff_t dst_stride, const ptrdiff_t luma_stride, const int c_idx,
    const int width, const int height, const int hs, const int vs, const int vb_pos, const ALFParams *alf)
{
    const VVCFrameContext *fc     = lc->fc;
    const H266RawSliceHeader *rsh = lc->sc->sh.r;
    const int idx                 = c_idx - 1;
    const int cc_aps_id           = c_idx == CB ? rsh->sh_alf_cc_cb_aps_id : rsh->sh_alf_cc_cr_aps_id;
    const VVCALF *aps             = fc->ps.alf_list[cc_aps_id];

    if (aps) {
        const int16_t *coeff = aps->cc_coeff[idx][alf->ctb_cc_idc[idx] - 1];

        fc->vvcdsp.alf.filter_cc(dst, dst_stride, luma, luma_stride, width, height, hs, vs, coeff, vb_pos);
    }
}

void ff_vvc_alf_copy_ctu_to_hv(VVCLocalContext* lc, const int x0, const int y0)
{
    VVCFrameContext *fc  = lc->fc;
    const int rx         = x0 >> fc->ps.sps->ctb_log2_size_y;
    const int ry         = y0 >> fc->ps.sps->ctb_log2_size_y;
    const int ctb_size_y = fc->ps.sps->ctb_size_y;
    const int c_end      = fc->ps.sps->r->sps_chroma_format_idc ? VVC_MAX_SAMPLE_ARRAYS : 1;

    for (int c_idx = 0; c_idx < c_end; c_idx++) {
        const int hs     = fc->ps.sps->hshift[c_idx];
        const int vs     = fc->ps.sps->vshift[c_idx];
        const int x      = x0 >> hs;
        const int y      = y0 >> vs;
        const int width  = FFMIN(fc->ps.pps->width - x0, ctb_size_y) >> hs;
        const int height = FFMIN(fc->ps.pps->height - y0, ctb_size_y) >> vs;

        const int src_stride = fc->frame->linesize[c_idx];
        uint8_t *src = POS(c_idx, x0, y0);

        alf_copy_ctb_to_hv(fc, src, src_stride, x, y, width, height, rx, ry, c_idx);
    }
}

static void alf_get_edges(const VVCLocalContext *lc, int edges[MAX_EDGES], const int rx, const int ry)
{
    VVCFrameContext *fc  = lc->fc;
    const VVCSPS *sps    = fc->ps.sps;
    const VVCPPS *pps    = fc->ps.pps;
    const int subpic_idx = lc->sc->sh.r->curr_subpic_idx;

    // we can't use |= instead of || in this function; |= is not a shortcut operator

    if (!pps->r->pps_loop_filter_across_tiles_enabled_flag) {
        edges[LEFT]   = edges[LEFT]   || (lc->boundary_flags & BOUNDARY_LEFT_TILE);
        edges[TOP]    = edges[TOP]    || (lc->boundary_flags & BOUNDARY_UPPER_TILE);
        edges[RIGHT]  = edges[RIGHT]  || pps->ctb_to_col_bd[rx] != pps->ctb_to_col_bd[rx + 1];
        edges[BOTTOM] = edges[BOTTOM] || pps->ctb_to_row_bd[ry] != pps->ctb_to_row_bd[ry + 1];
    }

    if (!pps->r->pps_loop_filter_across_slices_enabled_flag) {
        edges[LEFT]   = edges[LEFT]   || (lc->boundary_flags & BOUNDARY_LEFT_SLICE);
        edges[TOP]    = edges[TOP]    || (lc->boundary_flags & BOUNDARY_UPPER_SLICE);
        edges[RIGHT]  = edges[RIGHT]  || CTB(fc->tab.slice_idx, rx, ry) != CTB(fc->tab.slice_idx, rx + 1, ry);
        edges[BOTTOM] = edges[BOTTOM] || CTB(fc->tab.slice_idx, rx, ry) != CTB(fc->tab.slice_idx, rx, ry + 1);
    }

    if (!sps->r->sps_loop_filter_across_subpic_enabled_flag[subpic_idx]) {
        edges[LEFT]   = edges[LEFT]   || (lc->boundary_flags & BOUNDARY_LEFT_SUBPIC);
        edges[TOP]    = edges[TOP]    || (lc->boundary_flags & BOUNDARY_UPPER_SUBPIC);
        edges[RIGHT]  = edges[RIGHT]  || fc->ps.sps->r->sps_subpic_ctu_top_left_x[subpic_idx] + fc->ps.sps->r->sps_subpic_width_minus1[subpic_idx] == rx;
        edges[BOTTOM] = edges[BOTTOM] || fc->ps.sps->r->sps_subpic_ctu_top_left_y[subpic_idx] + fc->ps.sps->r->sps_subpic_height_minus1[subpic_idx] == ry;
    }

    if (sps->r->sps_virtual_boundaries_enabled_flag) {
        edges[LEFT]   = edges[LEFT]   || is_virtual_boundary(fc, rx << sps->ctb_log2_size_y, 1);
        edges[TOP]    = edges[TOP]    || is_virtual_boundary(fc, ry << sps->ctb_log2_size_y, 0);
        edges[RIGHT]  = edges[RIGHT]  || is_virtual_boundary(fc, (rx + 1) << sps->ctb_log2_size_y, 1);
        edges[BOTTOM] = edges[BOTTOM] || is_virtual_boundary(fc, (ry + 1) << sps->ctb_log2_size_y, 0);
    }
}

static void alf_init_subblock(VVCRect *sb, int sb_edges[MAX_EDGES], const VVCRect *b, const int edges[MAX_EDGES])
{
    *sb = *b;
    memcpy(sb_edges, edges, sizeof(int) * MAX_EDGES);
}

static void alf_get_subblock(VVCRect *sb, int edges[MAX_EDGES], const int bx, const int by, const int vb_pos[2], const int has_vb[2])
{
    int *pos[] = { &sb->l, &sb->t, &sb->r, &sb->b };

    for (int vertical = 0; vertical <= 1; vertical++) {
        if (has_vb[vertical]) {
            const int c = vertical ? (bx ? LEFT : RIGHT) : (by ? TOP : BOTTOM);
            *pos[c] = vb_pos[vertical];
            edges[c]  = 1;
        }
    }
}

static void alf_get_subblocks(const VVCLocalContext *lc, VVCRect sbs[MAX_VBBS], int sb_edges[MAX_VBBS][MAX_EDGES], int *nb_sbs,
    const int x0, const int y0, const int rx, const int ry)
{
    VVCFrameContext *fc  = lc->fc;
    const VVCSPS *sps    = fc->ps.sps;
    const VVCPPS *pps    = fc->ps.pps;
    const int ctu_size_y = sps->ctb_size_y;
    const int vb_pos[]   = { get_virtual_boundary(fc, ry, 0),  get_virtual_boundary(fc, rx, 1) };
    const int has_vb[]   = { vb_pos[0] > y0, vb_pos[1] > x0 };
    const VVCRect b      = { x0, y0, FFMIN(x0 + ctu_size_y, pps->width), FFMIN(y0 + ctu_size_y, pps->height) };
    int edges[MAX_EDGES] = { !rx, !ry, rx == pps->ctb_width - 1, ry == pps->ctb_height - 1 };
    int i                = 0;

    alf_get_edges(lc, edges, rx, ry);

    for (int by = 0; by <= has_vb[0]; by++) {
        for (int bx = 0; bx <= has_vb[1]; bx++, i++) {
            alf_init_subblock(sbs + i, sb_edges[i], &b, edges);
            alf_get_subblock(sbs + i, sb_edges[i], bx, by, vb_pos, has_vb);
        }
    }
    *nb_sbs = i;
}

void ff_vvc_alf_filter(VVCLocalContext *lc, const int x0, const int y0)
{
    VVCFrameContext *fc     = lc->fc;
    const VVCSPS *sps       = fc->ps.sps;
    const int rx            = x0 >> sps->ctb_log2_size_y;
    const int ry            = y0 >> sps->ctb_log2_size_y;
    const int ps            = sps->pixel_shift;
    const int padded_stride = EDGE_EMU_BUFFER_STRIDE << ps;
    const int padded_offset = padded_stride * ALF_PADDING_SIZE + (ALF_PADDING_SIZE << ps);
    const int c_end         = sps->r->sps_chroma_format_idc ? VVC_MAX_SAMPLE_ARRAYS : 1;
    const int has_chroma    = !!sps->r->sps_chroma_format_idc;
    const int ctu_end       = y0 + sps->ctb_size_y;
    const ALFParams *alf    = &CTB(fc->tab.alf, rx, ry);
    int sb_edges[MAX_VBBS][MAX_EDGES], nb_sbs;
    VVCRect sbs[MAX_VBBS];

    alf_get_subblocks(lc, sbs, sb_edges, &nb_sbs, x0, y0, rx, ry);

    for (int i = 0; i < nb_sbs; i++) {
        const VVCRect *sb = sbs + i;
        for (int c_idx = 0; c_idx < c_end; c_idx++) {
            const int hs         = fc->ps.sps->hshift[c_idx];
            const int vs         = fc->ps.sps->vshift[c_idx];
            const int x          = sb->l >> hs;
            const int y          = sb->t >> vs;
            const int width      = (sb->r - sb->l) >> hs;
            const int height     = (sb->b - sb->t) >> vs;
            const int src_stride = fc->frame->linesize[c_idx];
            uint8_t *src         = POS(c_idx, sb->l, sb->t);
            uint8_t *padded;

            if (alf->ctb_flag[c_idx] || (!c_idx && has_chroma && (alf->ctb_cc_idc[0] || alf->ctb_cc_idc[1]))) {
                padded = (c_idx ? lc->alf_buffer_chroma : lc->alf_buffer_luma) + padded_offset;
                alf_prepare_buffer(fc, padded, src, x, y, rx, ry, width, height,
                    padded_stride, src_stride, c_idx, sb_edges[i]);
            }
            if (alf->ctb_flag[c_idx]) {
                if (!c_idx)  {
                    alf_filter_luma(lc, src, padded, src_stride, padded_stride, x, y,
                        width, height, ctu_end - ALF_VB_POS_ABOVE_LUMA, alf);
                } else {
                    alf_filter_chroma(lc, src, padded, src_stride, padded_stride, c_idx,
                        width, height, ((ctu_end - sb->t) >> vs) - ALF_VB_POS_ABOVE_CHROMA, alf);
                }
            }
            if (c_idx && alf->ctb_cc_idc[c_idx - 1]) {
                padded = lc->alf_buffer_luma + padded_offset;
                alf_filter_cc(lc, src, padded, src_stride, padded_stride, c_idx,
                    width, height, hs, vs, ctu_end - sb->t - ALF_VB_POS_ABOVE_LUMA, alf);
            }
        }
    }
}


void ff_vvc_lmcs_filter(const VVCLocalContext *lc, const int x, const int y)
{
    const SliceContext *sc = lc->sc;
    const VVCFrameContext *fc = lc->fc;
    const int ctb_size = fc->ps.sps->ctb_size_y;
    const int width    = FFMIN(fc->ps.pps->width  - x, ctb_size);
    const int height   = FFMIN(fc->ps.pps->height - y, ctb_size);
    uint8_t *data      = POS(LUMA, x, y);
    if (sc->sh.r->sh_lmcs_used_flag)
        fc->vvcdsp.lmcs.filter(data, fc->frame->linesize[LUMA], width, height, &fc->ps.lmcs.inv_lut);
}
